/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

// Because of the indirection with which we call into these modules, eslint
// does not realize which of these modules are used, so we have to disable
// the relevant lint rule. However, this does mean that when modifying the
// list of observers or the modules they are associated with, you must be
// more careful to ensure we remove unused modules.
/* eslint-disable mozilla/valid-lazy */
let lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  CanvasPermissionPromptHelper:
    "moz-src:///browser/modules/CanvasPermissionPromptHelper.sys.mjs",
  FilePickerCrashed: "resource:///modules/FilePickerCrashed.sys.mjs",
  PluginManager: "resource:///actors/PluginParent.sys.mjs",
  UnexpectedScriptObserver:
    "moz-src:///browser/modules/UnexpectedScriptObserver.sys.mjs",
  WebAuthnPromptHelper:
    "moz-src:///browser/modules/WebAuthnPromptHelper.sys.mjs",
});

// Map from observer topic to a list of exported objects whose observe()
// method should be called when the topic is fired. The exported objects
// are expected to be defined in the modules imported above.
let gObservers = {
  "canvas-permissions-prompt": ["CanvasPermissionPromptHelper"],
  "canvas-permissions-prompt-hide-doorhanger": ["CanvasPermissionPromptHelper"],

  "UnexpectedJavaScriptLoad-Live": ["UnexpectedScriptObserver"],
  "UnexpectedJavaScriptLoad-UserTookAction": ["UnexpectedScriptObserver"],

  "file-picker-crashed": ["FilePickerCrashed"],
  "gmp-plugin-crash": ["PluginManager"],
  "plugin-crashed": ["PluginManager"],

  "webauthn-prompt": ["WebAuthnPromptHelper"],
};
if (Cu.isInAutomation) {
  gObservers["UnexpectedJavaScriptLoad-ResetNotification"] = [
    "UnexpectedScriptObserver",
  ];
}

if (AppConstants.MOZ_UPDATER) {
  ChromeUtils.defineESModuleGetters(lazy, {
    UpdateListener: "resource://gre/modules/UpdateListener.sys.mjs",
  });

  gObservers["update-downloading"] = ["UpdateListener"];
  gObservers["update-staged"] = ["UpdateListener"];
  gObservers["update-downloaded"] = ["UpdateListener"];
  gObservers["update-available"] = ["UpdateListener"];
  gObservers["update-error"] = ["UpdateListener"];
  gObservers["update-swap"] = ["UpdateListener"];
}

/**
 * Utility to avoid eagerly loading modules that are only needed
 * once a given observer topic is fired. Note that in an ideal world,
 * these observers would be added in parts of the same component that
 * are already loaded at startup, but not all components have such code.
 *
 * This module exists to avoid each component having to reinvent that wheel.
 */
export const ObserverForwarder = {
  init() {
    for (const topic of Object.keys(gObservers)) {
      Services.obs.addObserver(this, topic);
    }
  },

  observe(subject, topic, data) {
    for (let objectName of gObservers[topic]) {
      try {
        lazy[objectName].observe(subject, topic, data);
      } catch (e) {
        console.error(e);
      }
    }
  },
};
