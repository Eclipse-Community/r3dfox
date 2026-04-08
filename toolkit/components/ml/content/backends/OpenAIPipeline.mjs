/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

/**
 * @typedef {import("../../content/Utils.sys.mjs").ProgressAndStatusCallbackParams} ProgressAndStatusCallbackParams
 */

/* eslint-disable-next-line mozilla/reject-import-system-module-from-non-system */
import { Progress } from "chrome://global/content/ml/Utils.sys.mjs";

/* eslint-disable-next-line mozilla/reject-import-system-module-from-non-system */
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/**
 * Log level set by the pipeline.
 *
 * @type {string}
 */
let _logLevel = "Error";

/**
 * Lazy initialization container.
 *
 * @type {object}
 */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevel: _logLevel, // we can't use maxLogLevelPref in workers.
    prefix: "ML:OpenAIPipeline",
  });
});
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    setLogLevel: "chrome://global/content/ml/Utils.sys.mjs",
  },
  { global: "current" }
);

export class OpenAIPipeline {
  #errorFactory = null;
  #options = null;
  OpenAILib = null;

  constructor(options, errorFactory) {
    this.#errorFactory = errorFactory;
    this.#options = options;
  }

  static async initialize(mlEngineWorker, wasm, options = {}, errorFactory) {
    let initStart = ChromeUtils.now();
    lazy.console.debug("Initializing OpenAI pipeline");
    let importStart = ChromeUtils.now();
    if (AppConstants.NIGHTLY_BUILD) {
      OpenAIPipeline.OpenAILib =
        await import("chrome://global/content/ml/openai-dev.mjs");
    } else {
      OpenAIPipeline.OpenAILib =
        await import("chrome://global/content/ml/openai.mjs");
    }
    ChromeUtils.addProfilerMarker(
      "MLEngine:OpenAI",
      { startTime: importStart },
      `Library loaded`
    );
    if (options.logLevel) {
      _logLevel = options.logLevel;
      lazy.setLogLevel(options.logLevel); // setting Utils log level
    }
    let config = {};
    options.applyToConfig(config);
    config.backend = config.backend || "openai";

    // reapply logLevel if it has changed.
    if (lazy.console.logLevel != config.logLevel) {
      lazy.console.logLevel = config.logLevel;
    }

    ChromeUtils.addProfilerMarker(
      "MLEngine:OpenAI",
      { startTime: initStart },
      `Initialized`
    );

    return new OpenAIPipeline(config, errorFactory);
  }

  /**
   * Sends progress updates to both the port and inference progress callback.
   *
   * @private
   * @param {object} args - The arguments object
   * @param {string} args.content - The text content to send in the progress update
   * @param {string|null} args.requestId - Unique identifier for the request
   * @param {Function|null} args.inferenceProgressCallback - Callback function to report inference progress
   * @param {MessagePort|null} args.port - Port for posting messages to the caller
   * @param {boolean} args.isDone - Whether this is the final progress update
   */
  #sendProgress(args) {
    const {
      content,
      requestId,
      inferenceProgressCallback,
      port,
      isDone,
      toolCalls,
    } = args;
    port?.postMessage({
      text: content,
      ...(toolCalls ? { toolCalls } : {}),
      ...(isDone && { done: true, finalOutput: content }),
      ok: true,
    });
    inferenceProgressCallback?.({
      ok: true,
      metadata: {
        text: content,
        requestId,
        tokens: [],
        ...(toolCalls ? { toolCalls } : {}),
      },
      type: Progress.ProgressType.INFERENCE,
      statusText: isDone
        ? Progress.ProgressStatusText.DONE
        : Progress.ProgressStatusText.IN_PROGRESS,
    });
  }

  /**
   * Finalizes the tool calls by sorting them by index and returning an array of call objects.
   *
   * @param {map} acc
   * @returns {Array}
   */

  #finalizeToolCalls(acc) {
    // Convert Map entries ([index, call]) to an array
    const entries = Array.from(acc.entries());
    // Sort by the numeric index
    entries.sort((a, b) => a[0] - b[0]);
    // Return just the call objects (drop the index)
    return entries.map(([_index, call]) => call);
  }

  /**
   * Because we are streaming here, we may get multiple partial tool_calls deltas and need to merge them together.
   * This helper does that by looking at the index property on each tool call fragment.
   *
   * @param {map} acc - Accumulated tool calls map
   * @param {Array} deltas - New tool call fragments to merge
   * @returns {map} Merged tool calls map
   */
  #mergeToolDeltas(acc, deltas) {
    // If no deltas, return acc unchanged
    if (!Array.isArray(deltas) || deltas.length === 0) {
      return acc;
    }

    let next = new Map(acc); // shallow copy to keep immutability

    for (const toolCall of deltas) {
      const idx = toolCall.index ?? 0;
      const existing = next.get(idx) || {
        id: null,
        type: "function",
        function: { name: "", arguments: "" },
      };
      const nameFrag = toolCall.function?.name ?? "";
      const argsFrag = toolCall.function?.arguments ?? "";

      // Merge fragments into previous entry
      const merged = {
        id: toolCall.id ?? existing.id,
        type: "function",
        function: {
          name: nameFrag
            ? existing.function.name + nameFrag
            : existing.function.name,
          arguments: argsFrag
            ? existing.function.arguments + argsFrag
            : existing.function.arguments,
        },
      };
      next.set(idx, merged);
    }
    return next;
  }

  /**
   * Handles streaming response from the OpenAI API.
   * Processes each chunk as it arrives and sends progress updates.
   *
   * @private
   * @param {object} args - The arguments object
   * @param {OpenAI} args.client - OpenAI client instance
   * @param {object} args.completionParams - Parameters for the completion request
   * @param {string|null} args.requestId - Unique identifier for the request
   * @param {Function|null} args.inferenceProgressCallback - Callback function to report inference progress
   * @param {MessagePort|null} args.port - Port for posting messages to the caller
   * @returns {Promise<object>} Result object with done, finalOutput, ok, and metrics properties
   */
  async #handleStreamingResponse(args) {
    const {
      client,
      completionParams,
      requestId,
      inferenceProgressCallback,
      port,
    } = args;

    const stream = await client.chat.completions.create(completionParams);

    let streamOutput = "";
    let toolAcc = new Map();
    let sawToolCallsFinish = false;

    for await (const chunk of stream) {
      const choice = chunk?.choices?.[0];
      const delta = choice?.delta ?? {};

      // Normal text tokens
      if (delta.content) {
        streamOutput += delta.content;
        this.#sendProgress({
          content: delta.content,
          requestId,
          inferenceProgressCallback,
          port,
          isDone: false,
        });
      }

      if (Array.isArray(delta.tool_calls) && delta.tool_calls.length) {
        toolAcc = this.#mergeToolDeltas(toolAcc, delta.tool_calls);
      }

      // If the model signals it wants tools now
      if (choice?.finish_reason === "tool_calls") {
        sawToolCallsFinish = true;
        const toolCalls = this.#finalizeToolCalls(toolAcc);

        // Emit the completed tool calls to the caller so they can execute them.
        this.#sendProgress({
          content: "", // no user-visible text here
          requestId,
          inferenceProgressCallback,
          port,
          isDone: false,
          toolCalls,
        });

        // Typically end this assistant turn here.
        break;
      }
    }

    // Final message: does not carry full content to avoid duplication
    this.#sendProgress({
      requestId,
      inferenceProgressCallback,
      port,
      isDone: true,
    });

    return {
      finalOutput: streamOutput,
      metrics: [],
      ...(sawToolCallsFinish
        ? { toolCalls: this.#finalizeToolCalls(toolAcc) }
        : {}),
    };
  }

  /**
   * Handles non-streaming response from the OpenAI API.
   * Waits for the complete response and sends it as a single update.
   *
   * @private
   * @param {object} args - The arguments object
   * @param {OpenAI} args.client - OpenAI client instance
   * @param {object} args.completionParams - Parameters for the completion request
   * @param {string|null} args.requestId - Unique identifier for the request
   * @param {Function|null} args.inferenceProgressCallback - Callback function to report inference progress
   * @param {MessagePort|null} args.port - Port for posting messages to the caller
   * @returns {Promise<object>} Result object with done, finalOutput, ok, and metrics properties
   */
  async #handleNonStreamingResponse(args) {
    const {
      client,
      completionParams,
      requestId,
      inferenceProgressCallback,
      port,
    } = args;

    const completion = await client.chat.completions.create(completionParams);
    const message = completion.choices[0].message;
    const output = message.content || "";
    const toolCalls = message.tool_calls || null;

    this.#sendProgress({
      content: output,
      requestId,
      inferenceProgressCallback,
      port,
      isDone: true,
      toolCalls,
    });

    return {
      finalOutput: output,
      metrics: [],
      ...(toolCalls ? { toolCalls } : {}),
    };
  }

  /**
   * Executes the OpenAI pipeline with the given request.
   * Supports both streaming and non-streaming modes based on options configuration.
   *
   * @param {object} request - The request object containing the messages
   * @param {Array} request.args - Array of message objects for the chat completion
   * @param {string|null} [requestId=null] - Unique identifier for this request
   * @param {Function|null} [inferenceProgressCallback=null] - Callback function to report progress during inference
   * @param {MessagePort|null} [port=null] - Port for posting messages back to the caller
   * @returns {Promise<object>} Result object containing completion status, output, and metrics
   * @throws {Error} Throws backend error if the API request fails
   */
  async run(
    request,
    requestId = null,
    inferenceProgressCallback = null,
    port = null
  ) {
    lazy.console.debug("Running OpenAI pipeline");
    try {
      const { baseURL, apiKey, modelId, serviceType, extraHeaders, engineId } =
        this.#options;
      const fxAccountToken = request.fxAccountToken
        ? request.fxAccountToken
        : null;

      const client = new OpenAIPipeline.OpenAILib.OpenAI({
        baseURL: baseURL ? baseURL : "http://localhost:11434/v1",
        apiKey: apiKey || fxAccountToken || "apiKey",
        defaultHeaders: {
          ...extraHeaders,
          "service-type": serviceType || "ai",
          "x-engine-id": engineId,
        },
      });
      const stream = request.streamOptions?.enabled || false;
      const tools = request.tools || [];

      const completionParams = {
        model: modelId,
        messages: request.args,
        stream,
        tools,
      };

      const args = {
        client,
        completionParams,
        requestId,
        inferenceProgressCallback,
        port,
      };

      return stream
        ? await this.#handleStreamingResponse(args)
        : await this.#handleNonStreamingResponse(args);
    } catch (error) {
      const backendError = this.#errorFactory(error);
      port?.postMessage({ done: true, ok: false, error: backendError });
      inferenceProgressCallback?.({
        ok: false,
        metadata: {
          text: "",
          requestId,
          tokens: [],
        },
        type: Progress.ProgressType.INFERENCE,
        statusText: Progress.ProgressStatusText.DONE,
        ...(error.status && { status: error.status }),
      });

      throw backendError;
    }
  }
}
