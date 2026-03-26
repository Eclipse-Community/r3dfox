/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */

ChromeUtils.defineLazyGetter(this, "L10n", () => {
  return new Localization([
    "branding/brand.ftl",
    "browser/preferences/preferences.ftl",
  ]);
});

Preferences.addAll([
  // IPv6
  { id: "network.dns.disableIPv6", type: "bool" },
  // Firefox Accounts
  { id: "identity.fxaccounts.enabled", type: "bool" },
  // WebGL
  { id: "librewolf.webgl.prompt", type: "bool" },
  { id: "librewolf.webgl.prompt.hide", type: "bool" },
  // Automatically Update Extensions
  { id: "extensions.update.enabled", type: "bool" },
  { id: "extensions.update.autoUpdateDefault", type: "bool" },
  // Clipboard autocopy/paste
  { id: "clipboard.autocopy", type: "bool" },
  { id: "middlemouse.paste", type: "bool" },
  // XOrigin referrers
  { id: "network.http.referer.XOriginPolicy", type: "int" },
  // Harden
  { id: "privacy.resistFingerprinting.letterboxing", type: "bool" },
  // Google Safe Browsing
  //{ id: "browser.safebrowsing.malware.enabled", type: "bool" }, // Already loaded
  //{ id: "browser.safebrowsing.phishing.enabled", type: "bool" },
  { id: "browser.safebrowsing.blockedURIs.enabled", type: "bool" },
  { id: "browser.safebrowsing.provider.google4.gethashURL", type: "string" },
  { id: "browser.safebrowsing.provider.google4.updateURL", type: "string" },
  { id: "browser.safebrowsing.provider.google.gethashURL", type: "string" },
  { id: "browser.safebrowsing.provider.google.updateURL", type: "string" },
  /**** Prefs that require changing a lockPref ****/
  // Google safe browsing check downloads
  //{ id: "browser.safebrowsing.downloads.enabled", type: "bool" }, //Also already added
  { id: "toolkit.legacyUserProfileCustomizations.stylesheets", type: "bool" },
]);

Preferences.addSetting({
  id: "librewolfExtensionUpdateEnabled",
  pref: "extensions.update.enabled",
});
Preferences.addSetting({
  id: "librewolfExtensionAutoUpdateEnabled",
  pref: "extensions.update.autoUpdateDefault",
});
Preferences.addSetting({
  id: "librewolfExtensionUpdate",
  deps: ["librewolfExtensionUpdateEnabled","librewolfExtensionAutoUpdateEnabled"],
  get: (_, deps) => deps.librewolfExtensionUpdateEnabled.value && deps.librewolfExtensionAutoUpdateEnabled.value,
  set: (value, deps) => {
      deps.librewolfExtensionUpdateEnabled.value = value;
      deps.librewolfExtensionAutoUpdateEnabled.value = value;
  },
});

Preferences.addSetting({
  id: "librewolfSync",
  pref: "identity.fxaccounts.enabled",
  onUserChange() {
    confirmRestartPrompt(
      Services.prefs.getBoolPref("identity.fxaccounts.enabled"),
      1,
      true,
      false
    ).then(buttonIndex => {
      if (buttonIndex == CONFIRM_RESTART_PROMPT_RESTART_NOW) {
          Services.startup.quit(
            Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
          );
          return
        }
    });
  }
});

Preferences.addSetting({
  id: "librewolfAutocopy",
  pref: "clipboard.autocopy",
});
Preferences.addSetting({
  id: "librewolfPaste",
  pref: "middlemouse.paste",
});
Preferences.addSetting({
  id: "librewolfMiddleClick",
  deps: ["librewolfAutocopy","librewolfPaste"],
  get: (_, deps) => deps.librewolfAutocopy.value && deps.librewolfPaste.value,
  set: (value, deps) => {
      deps.librewolfAutocopy.value = value;
      deps.librewolfPaste.value = value;
  },
});

Preferences.addSetting({
  id: "librewolfUserChrome",
  pref: "toolkit.legacyUserProfileCustomizations.stylesheets",
});

Preferences.addSetting({
  id: "librewolfIPv6",
  pref: "network.dns.disableIPv6",
  get: (value) => value.value = !value,
  set: (value) => value.value = !value,
});

Preferences.addSetting({
  id: "librewolfCrossOrigin",
  pref: "network.http.referer.XOriginPolicy",
  get: (value) => {
    if (value == 2) {
      return true;
    } else {
      return false;
    }
  },
  set: (value) => value ? 2 : 0,
});

Preferences.addSetting({
  id: "librewolfRFP",
  pref: "privacy.resistFingerprinting",
});
Preferences.addSetting({
  id: "librewolfLetterboxing",
  pref: "privacy.resistFingerprinting.letterboxing",
});

Preferences.addSetting({
  id: "librewolfWebGLPrompt",
  pref: "librewolf.webgl.prompt",
  get: (value) => value.value = !value,
  set: (value) => value.value = !value,
});
Preferences.addSetting({
  id: "librewolfWebGLPromptHide",
  pref: "librewolf.webgl.prompt.hide",
  deps: ["librewolfWebGLPrompt"],
  disabled: ({librewolfWebGLPrompt}) => {
    return librewolfWebGLPrompt.value;
  },
});


function openProfileDirectory() {
  // Get the profile directory.
  let currProfD = Services.dirsvc.get("ProfD", Ci.nsIFile);
  let profileDir = currProfD.path;

  // Show the profile directory.
  let nsLocalFile = Components.Constructor(
    "@mozilla.org/file/local;1",
    "nsIFile",
    "initWithPath"
  );
  new nsLocalFile(profileDir).reveal();
}

function openAboutConfig() {
  window.open("about:config", "_blank");
}

var gLibrewolfPane = {
  _pane: null,

  // called when the document is first parsed
  init() {
    this._pane = document.getElementById("paneLibrewolf");
    initSettingGroup("librewolfBehavior");
    initSettingGroup("librewolfNetworking");
    initSettingGroup("librewolfPrivacy");
    initSettingGroup("librewolfFingerprinting");

    // Set event listener on open profile directory button
    setEventListener("librewolf-open-profile", "command", openProfileDirectory);
    // Set event listener on open about:config button
    setEventListener("librewolf-config-link", "click", openAboutConfig);

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "librewolf-pane-loaded");
  },
};
