/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const FULLSCREEN_PATH =
  "/browser/browser/base/content/test/fullscreen/fullscreen.html";

function getWarningDomain(warning) {
  let textElem = warning.querySelector(".pointerlockfswarning-domain-text");
  if (textElem.hidden) {
    return null;
  }
  let args = textElem.getAttribute("data-l10n-args");
  return args ? JSON.parse(args).domain : null;
}

async function waitForWarningState(aWarningElement, aExpectedState) {
  await BrowserTestUtils.waitForAttribute(aExpectedState, aWarningElement, "");
}

add_setup(async function init() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["full-screen-api.enabled", true],
      ["full-screen-api.allow-trusted-requests-only", false],
    ],
  });
});

// Bug 2021080 - Verify the fullscreen warning always displays the top-level domain,
// not the origin of the cross-origin frame that requested fullscreen.
add_task(async function test_fullscreen_warning_cross_origin_shows_toplevel() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    let warning = document.getElementById("fullscreen-warning");

    await SpecialPowers.spawn(browser, [FULLSCREEN_PATH], async path => {
      let iframe = content.document.createElement("iframe");
      iframe.allow = "fullscreen";
      iframe.src = `https://example.org${path}`;
      let loaded = new Promise(r =>
        iframe.addEventListener("load", r, { once: true })
      );
      content.document.body.appendChild(iframe);
      await loaded;
    });

    let warningShown = waitForWarningState(warning, "onscreen");
    await SpecialPowers.spawn(browser, [], async () => {
      let frame = content.document.querySelector("iframe");
      frame.focus();
      await SpecialPowers.spawn(frame, [], () => {
        content.document.getElementById("request").click();
      });
    });
    await warningShown;

    let activeOrigin = await SpecialPowers.spawn(browser, [], async () => {
      let frame = content.document.querySelector("iframe");
      return SpecialPowers.spawn(frame, [], () => content.location.hostname);
    });
    is(
      activeOrigin,
      "example.org",
      "Cross-origin frame (example.org) is the active fullscreen document"
    );
    is(
      getWarningDomain(warning),
      "example.com",
      "Warning shows top-level domain, not the active fullscreen frame's domain"
    );

    let warningHidden = waitForWarningState(warning, "hidden");
    let exitPromise = BrowserTestUtils.waitForEvent(
      document,
      "fullscreenchange",
      false,
      () => !document.fullscreenElement
    );
    document.getElementById("fullscreen-exit-button").click();
    await Promise.all([exitPromise, warningHidden]);
  });
});

// Bug 2021080 - Verify the fullscreen warning shows the top-level domain when each
// of three nested cross-origin frames (top, middle, inner) requests fullscreen.
add_task(async function test_fullscreen_warning_three_nested_origins() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    let warning = document.getElementById("fullscreen-warning");

    // Build a 3-level nested structure:
    // example.com (div > iframe[example.org (div > iframe[example.net])])
    await SpecialPowers.spawn(browser, [FULLSCREEN_PATH], async path => {
      let topDiv = content.document.createElement("div");
      content.document.body.appendChild(topDiv);

      let middleFrame = content.document.createElement("iframe");
      middleFrame.allow = "fullscreen";
      middleFrame.src = `https://example.org${path}`;
      let loaded = new Promise(r =>
        middleFrame.addEventListener("load", r, { once: true })
      );
      topDiv.appendChild(middleFrame);
      await loaded;

      await SpecialPowers.spawn(middleFrame, [path], async innerPath => {
        let middleDiv = content.document.createElement("div");
        content.document.body.appendChild(middleDiv);

        let innerFrame = content.document.createElement("iframe");
        innerFrame.allow = "fullscreen";
        innerFrame.src = `https://example.net${innerPath}`;
        let innerLoaded = new Promise(r =>
          innerFrame.addEventListener("load", r, { once: true })
        );
        middleDiv.appendChild(innerFrame);
        await innerLoaded;
      });
    });

    async function exitFullscreen() {
      let warningHidden = waitForWarningState(warning, "hidden");
      let exitPromise = BrowserTestUtils.waitForEvent(
        document,
        "fullscreenchange",
        false,
        () => !document.fullscreenElement
      );
      document.getElementById("fullscreen-exit-button").click();
      await Promise.all([exitPromise, warningHidden]);
    }

    // Step 1: Top-level (example.com) requests fullscreen on its div.
    let warningShown = waitForWarningState(warning, "onscreen");
    await SpecialPowers.spawn(browser, [], () => {
      content.document.querySelector("div").requestFullscreen();
    });
    await warningShown;
    is(
      getWarningDomain(warning),
      "example.com",
      "Top-level fullscreen: warning shows top-level domain"
    );
    await exitFullscreen();

    // Step 2: Middle frame (example.org) requests fullscreen on its div.
    warningShown = waitForWarningState(warning, "onscreen");
    await SpecialPowers.spawn(browser, [], async () => {
      let middleFrame = content.document.querySelector("iframe");
      middleFrame.focus();
      await SpecialPowers.spawn(middleFrame, [], () => {
        content.document.querySelector("div").requestFullscreen();
      });
    });
    await warningShown;
    is(
      getWarningDomain(warning),
      "example.com",
      "Middle frame fullscreen: warning shows top-level domain"
    );
    await exitFullscreen();

    // Step 3: Inner frame (example.net) requests fullscreen on an element.
    warningShown = waitForWarningState(warning, "onscreen");
    await SpecialPowers.spawn(browser, [], async () => {
      let middleFrame = content.document.querySelector("iframe");
      await SpecialPowers.spawn(middleFrame, [], async () => {
        let innerFrame = content.document.querySelector("iframe");
        innerFrame.focus();
        await SpecialPowers.spawn(innerFrame, [], () => {
          content.document.getElementById("request").click();
        });
      });
    });
    await warningShown;
    is(
      getWarningDomain(warning),
      "example.com",
      "Inner frame fullscreen: warning shows top-level domain"
    );
    await exitFullscreen();
  });
});
