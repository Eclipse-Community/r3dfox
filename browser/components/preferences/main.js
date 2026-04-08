/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */
/* import-globals-from /toolkit/mozapps/preferences/fontbuilder.js */
/* import-globals-from /browser/base/content/aboutDialog-appUpdater.js */
/* global MozXULElement */

/**
 * @import { Setting } from "chrome://global/content/preferences/Setting.mjs"
 */

ChromeUtils.defineESModuleGetters(this, {
  BackgroundUpdate: "resource://gre/modules/BackgroundUpdate.sys.mjs",
  UpdateListener: "resource://gre/modules/UpdateListener.sys.mjs",
  LinkPreview: "moz-src:///browser/components/genai/LinkPreview.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  WindowsLaunchOnLogin: "resource://gre/modules/WindowsLaunchOnLogin.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  FormAutofillPreferences:
    "resource://autofill/FormAutofillPreferences.sys.mjs",
  getMozRemoteImageURL: "moz-src:///toolkit/modules/FaviconUtils.sys.mjs",
});

// Constants & Enumeration Values
const TYPE_PDF = "application/pdf";

const PREF_PDFJS_DISABLED = "pdfjs.disabled";

// Pref for when containers is being controlled
const PREF_CONTAINERS_EXTENSION = "privacy.userContext.extension";

// Strings to identify ExtensionSettingsStore overrides
const CONTAINERS_KEY = "privacy.containers";

const FORCED_COLORS_QUERY = matchMedia("(forced-colors)");

const AUTO_UPDATE_CHANGED_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.auto"].observerTopic;
const BACKGROUND_UPDATE_CHANGED_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.background.enabled"]
    .observerTopic;

const ICON_URL_APP =
  AppConstants.platform == "linux"
    ? "moz-icon://dummy.exe?size=16"
    : "chrome://browser/skin/preferences/application.png";

// For CSS. Can be one of "ask", "save" or "handleInternally". If absent, the icon URL
// was set by us to a custom handler icon and CSS should not try to override it.
const APP_ICON_ATTR_NAME = "appHandlerIcon";

const OPEN_EXTERNAL_LINK_NEXT_TO_ACTIVE_TAB_VALUE =
  Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT;

/**
 * @param {Setting} featureSetting
 * @param {Setting} defaultSetting
 */
function canShowAiFeature(featureSetting, defaultSetting) {
  return (
    featureSetting.value != "blocked" &&
    !(featureSetting.value == "default" && defaultSetting.value == "blocked")
  );
}

Preferences.addAll([
  // Startup
  { id: "browser.startup.page", type: "int" },
  { id: "browser.startup.windowsLaunchOnLogin.enabled", type: "bool" },
  { id: "browser.privatebrowsing.autostart", type: "bool" },

  // Downloads
  { id: "browser.download.useDownloadDir", type: "bool", inverted: true },
  { id: "browser.download.enableDeletePrivate", type: "bool" },
  { id: "browser.download.deletePrivate", type: "bool" },
  { id: "browser.download.always_ask_before_handling_new_types", type: "bool" },
  { id: "browser.download.folderList", type: "int" },
  { id: "browser.download.dir", type: "file" },

  // AI Controls, these pref values can affect settings on the main pane and
  // have base Settings here
  { id: "browser.ai.control.default", type: "string" },
  { id: "browser.ai.control.translations", type: "string" },
  { id: "browser.ai.control.pdfjsAltText", type: "string" },
  { id: "browser.ai.control.smartTabGroups", type: "string" },
  { id: "browser.ai.control.linkPreviewKeyPoints", type: "string" },
  { id: "browser.ai.control.sidebarChatbot", type: "string" },

  /* Tab preferences
  Preferences:

  browser.link.open_newwindow
      1 opens such links in the most recent window or tab,
      2 opens such links in a new window,
      3 opens such links in a new tab
  browser.link.open_newwindow.override.external
    - this setting overrides `browser.link.open_newwindow` for externally
      opened links.
    - see `nsIBrowserDOMWindow` constants for the meaning of each value.
  browser.tabs.loadInBackground
  - true if display should switch to a new tab which has been opened from a
    link, false if display shouldn't switch
  browser.tabs.warnOnClose
  - true if when closing a window with multiple tabs the user is warned and
    allowed to cancel the action, false to just close the window
  browser.tabs.warnOnOpen
  - true if the user should be warned if he attempts to open a lot of tabs at
    once (e.g. a large folder of bookmarks), false otherwise
  browser.warnOnQuitShortcut
  - true if the user should be warned if they quit using the keyboard shortcut
  browser.taskbar.previews.enable
  - true if tabs are to be shown in the Windows 7 taskbar
  */

  { id: "browser.link.open_newwindow", type: "int" },
  { id: "browser.link.open_newwindow.override.external", type: "int" },
  { id: "browser.tabs.loadInBackground", type: "bool", inverted: true },
  { id: "browser.tabs.warnOnClose", type: "bool" },
  { id: "browser.warnOnQuitShortcut", type: "bool" },
  { id: "browser.tabs.warnOnOpen", type: "bool" },
  { id: "browser.ctrlTab.sortByRecentlyUsed", type: "bool" },
  { id: "browser.tabs.hoverPreview.enabled", type: "bool" },
  { id: "browser.tabs.hoverPreview.showThumbnails", type: "bool" },
  { id: "browser.tabs.groups.enabled", type: "bool" },
  { id: "browser.tabs.groups.smart.userEnabled", type: "bool" },
  { id: "browser.tabs.groups.smart.enabled", type: "bool" },
  { id: "privacy.userContext.ui.enabled", type: "bool" },

  { id: "sidebar.verticalTabs", type: "bool" },
  { id: "sidebar.revamp", type: "bool" },

  // CFR
  {
    id: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
    type: "bool",
  },
  {
    id: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
    type: "bool",
  },

  // High Contrast
  { id: "browser.display.document_color_use", type: "int" },

  // Fonts
  { id: "font.language.group", type: "string" },

  // Languages
  { id: "intl.regional_prefs.use_os_locales", type: "bool" },

  { id: "intl.accept_languages", type: "string" },
  { id: "privacy.spoof_english", type: "int" },
  // General tab

  /* Accessibility
   * accessibility.browsewithcaret
     - true enables keyboard navigation and selection within web pages using a
       visible caret, false uses normal keyboard navigation with no caret
   * accessibility.typeaheadfind
     - when set to true, typing outside text areas and input boxes will
       automatically start searching for what's typed within the current
       document; when set to false, no search action happens */
  { id: "accessibility.browsewithcaret", type: "bool" },
  { id: "accessibility.typeaheadfind", type: "bool" },
  { id: "accessibility.blockautorefresh", type: "bool" },

  /* Zoom */
  { id: "browser.zoom.full", type: "bool" },

  /* Browsing
   * general.autoScroll
     - when set to true, clicking the scroll wheel on the mouse activates a
       mouse mode where moving the mouse down scrolls the document downward with
       speed correlated with the distance of the cursor from the original
       position at which the click occurred (and likewise with movement upward);
       if false, this behavior is disabled
   * general.smoothScroll
     - set to true to enable finer page scrolling than line-by-line on page-up,
       page-down, and other such page movements */
  { id: "general.autoScroll", type: "bool" },
  { id: "general.smoothScroll", type: "bool" },
  { id: "widget.gtk.overlay-scrollbars.enabled", type: "bool", inverted: true },
  { id: "layout.css.always_underline_links", type: "bool" },
  { id: "layout.spellcheckDefault", type: "int" },
  { id: "accessibility.tabfocus", type: "int" },

  { id: "browser.ml.linkPreview.enabled", type: "bool" },
  { id: "browser.ml.linkPreview.optin", type: "bool" },
  { id: "browser.ml.linkPreview.longPress", type: "bool" },

  {
    id: "browser.preferences.defaultPerformanceSettings.enabled",
    type: "bool",
  },
  { id: "dom.ipc.processCount", type: "int" },
  { id: "dom.ipc.processCount.web", type: "int" },
  { id: "layers.acceleration.disabled", type: "bool", inverted: true },

  // Files and Applications
  { id: "pref.downloads.disable_button.edit_actions", type: "bool" },

  // DRM content
  { id: "media.eme.enabled", type: "bool" },

  // Update
  { id: "browser.preferences.advanced.selectedTabIndex", type: "int" },
  { id: "browser.search.update", type: "bool" },

  { id: "privacy.userContext.enabled", type: "bool" },
  {
    id: "privacy.userContext.newTabContainerOnLeftClick.enabled",
    type: "bool",
  },
  { id: "nimbus.rollouts.enabled", type: "bool" },

  // Picture-in-Picture
  {
    id: "media.videocontrols.picture-in-picture.video-toggle.enabled",
    type: "bool",
  },
  {
    id: "media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled",
    type: "bool",
  },

  // Media
  { id: "media.hardwaremediakeys.enabled", type: "bool" },

  // Appearance
  { id: "layout.css.prefers-color-scheme.content-override", type: "int" },

  // Translations
  { id: "browser.translations.automaticallyPopup", type: "bool" },
]);

if (AppConstants.HAVE_SHELL_SERVICE) {
  Preferences.addAll([
    { id: "browser.shell.checkDefaultBrowser", type: "bool" },
    { id: "pref.general.disable_button.default_browser", type: "bool" },
  ]);
}

if (AppConstants.platform === "win") {
  Preferences.addAll([
    { id: "browser.taskbar.previews.enable", type: "bool" },
    { id: "ui.osk.enabled", type: "bool" },
  ]);
}

if (AppConstants.MOZ_UPDATER) {
  Preferences.addAll([
    { id: "app.update.disable_button.showUpdateHistory", type: "bool" },
  ]);

  if (AppConstants.NIGHTLY_BUILD) {
    Preferences.addAll([{ id: "app.update.suppressPrompts", type: "bool" }]);
  }
}

Preferences.addSetting({
  id: "privateBrowsingAutoStart",
  pref: "browser.privatebrowsing.autostart",
});

Preferences.addSetting(
  /** @type {{ _getLaunchOnLoginApprovedCachedValue: boolean } & SettingConfig} */ ({
    id: "launchOnLoginApproved",
    _getLaunchOnLoginApprovedCachedValue: true,
    get() {
      return this._getLaunchOnLoginApprovedCachedValue;
    },
    // Check for a launch on login registry key
    // This accounts for if a user manually changes it in the registry
    // Disabling in Task Manager works outside of just deleting the registry key
    // in HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run
    // but it is not possible to change it back to enabled as the disabled value is just a random
    // hexadecimal number
    setup() {
      if (AppConstants.platform !== "win") {
        /**
         * WindowsLaunchOnLogin isnt available if not on windows
         * but this setup function still fires, so must prevent
         * WindowsLaunchOnLogin.getLaunchOnLoginApproved
         * below from executing unnecessarily.
         */
        return;
      }
      // @ts-ignore bug 1996860
      WindowsLaunchOnLogin.getLaunchOnLoginApproved().then(val => {
        this._getLaunchOnLoginApprovedCachedValue = val;
      });
    },
  })
);

Preferences.addSetting({
  id: "windowsLaunchOnLoginEnabled",
  pref: "browser.startup.windowsLaunchOnLogin.enabled",
});

Preferences.addSetting(
  /** @type {{_getLaunchOnLoginEnabledValue: boolean, startWithLastProfile: boolean} & SettingConfig} */ ({
    id: "windowsLaunchOnLogin",
    deps: ["launchOnLoginApproved", "windowsLaunchOnLoginEnabled"],
    _getLaunchOnLoginEnabledValue: false,
    get startWithLastProfile() {
      return Cc["@mozilla.org/toolkit/profile-service;1"].getService(
        Ci.nsIToolkitProfileService
      ).startWithLastProfile;
    },
    get() {
      return this._getLaunchOnLoginEnabledValue;
    },
    setup(emitChange) {
      if (AppConstants.platform !== "win") {
        /**
         * WindowsLaunchOnLogin isnt available if not on windows
         * but this setup function still fires, so must prevent
         * WindowsLaunchOnLogin.getLaunchOnLoginEnabled
         * below from executing unnecessarily.
         */
        return;
      }

      /** @type {boolean} */
      let getLaunchOnLoginEnabledValue;
      let maybeEmitChange = () => {
        if (
          getLaunchOnLoginEnabledValue !== this._getLaunchOnLoginEnabledValue
        ) {
          this._getLaunchOnLoginEnabledValue = getLaunchOnLoginEnabledValue;
          emitChange();
        }
      };
      if (!this.startWithLastProfile) {
        getLaunchOnLoginEnabledValue = false;
        maybeEmitChange();
      } else {
        // @ts-ignore bug 1996860
        WindowsLaunchOnLogin.getLaunchOnLoginEnabled().then(val => {
          getLaunchOnLoginEnabledValue = val;
          maybeEmitChange();
        });
      }
    },
    visible: ({ windowsLaunchOnLoginEnabled }) => {
      let isVisible =
        AppConstants.platform === "win" && windowsLaunchOnLoginEnabled.value;
      if (isVisible) {
        // @ts-ignore bug 1996860
        NimbusFeatures.windowsLaunchOnLogin.recordExposureEvent({
          once: true,
        });
      }
      return isVisible;
    },
    disabled({ launchOnLoginApproved }) {
      return !this.startWithLastProfile || !launchOnLoginApproved.value;
    },
    onUserChange(checked) {
      if (checked) {
        // windowsLaunchOnLogin has been checked: create registry key or shortcut
        // The shortcut is created with the same AUMID as Firefox itself. However,
        // this is not set during browser tests and the fallback of checking the
        // registry fails. As such we pass an arbitrary AUMID for the purpose
        // of testing.
        // @ts-ignore bug 1996860
        WindowsLaunchOnLogin.createLaunchOnLogin();
        Services.prefs.setBoolPref(
          "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
          true
        );
      } else {
        // windowsLaunchOnLogin has been unchecked: delete registry key and shortcut
        // @ts-ignore bug 1996860
        WindowsLaunchOnLogin.removeLaunchOnLogin();
      }
    },
  })
);

Preferences.addSetting({
  id: "windowsLaunchOnLoginDisabledProfileBox",
  deps: ["windowsLaunchOnLoginEnabled"],
  visible: ({ windowsLaunchOnLoginEnabled }) => {
    if (AppConstants.platform !== "win") {
      return false;
    }
    let startWithLastProfile = Cc[
      "@mozilla.org/toolkit/profile-service;1"
    ].getService(Ci.nsIToolkitProfileService).startWithLastProfile;

    return !startWithLastProfile && windowsLaunchOnLoginEnabled.value;
  },
});

Preferences.addSetting({
  id: "windowsLaunchOnLoginDisabledBox",
  deps: ["launchOnLoginApproved", "windowsLaunchOnLoginEnabled"],
  visible: ({ launchOnLoginApproved, windowsLaunchOnLoginEnabled }) => {
    if (AppConstants.platform !== "win") {
      return false;
    }
    let startWithLastProfile = Cc[
      "@mozilla.org/toolkit/profile-service;1"
    ].getService(Ci.nsIToolkitProfileService).startWithLastProfile;

    return (
      startWithLastProfile &&
      !launchOnLoginApproved.value &&
      windowsLaunchOnLoginEnabled.value
    );
  },
});

Preferences.addSetting({
  /**
   * The "Open previous windows and tabs" option on about:preferences page.
   */
  id: "browserRestoreSession",
  pref: "browser.startup.page",
  deps: ["privateBrowsingAutoStart"],
  get:
    /**
     * Returns the value of the "Open previous windows and tabs" option based
     * on the value of the browser.privatebrowsing.autostart pref.
     *
     * @param {number | undefined} value
     * @returns {boolean}
     */
    value => {
      const pbAutoStartPref = Preferences.get(
        "browser.privatebrowsing.autostart"
      );
      let newValue = pbAutoStartPref.value
        ? false
        : value === gMainPane.STARTUP_PREF_RESTORE_SESSION;

      return newValue;
    },
  set: checked => {
    const startupPref = Preferences.get("browser.startup.page");
    let newValue;

    if (checked) {
      // We need to restore the blank homepage setting in our other pref
      if (startupPref.value === gMainPane.STARTUP_PREF_BLANK) {
        // @ts-ignore bug 1996860
        HomePage.safeSet("about:blank");
      }
      newValue = gMainPane.STARTUP_PREF_RESTORE_SESSION;
    } else {
      newValue = gMainPane.STARTUP_PREF_HOMEPAGE;
    }
    return newValue;
  },
  disabled: deps => {
    return deps.privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting({
  id: "useAutoScroll",
  pref: "general.autoScroll",
});
Preferences.addSetting({
  id: "useSmoothScrolling",
  pref: "general.smoothScroll",
});

Preferences.addSetting({
  id: "useOverlayScrollbars",
  pref: "widget.gtk.overlay-scrollbars.enabled",
  visible: () => AppConstants.MOZ_WIDGET_GTK,
});
Preferences.addSetting({
  id: "useOnScreenKeyboard",
  // Bug 1993053: Restore the pref to `ui.osk.enabled` after changing
  // the PrefereceNotFoundError throwing behavior.
  pref: AppConstants.platform == "win" ? "ui.osk.enabled" : undefined,
  visible: () => AppConstants.platform == "win",
});
Preferences.addSetting({
  id: "useCursorNavigation",
  pref: "accessibility.browsewithcaret",
});
Preferences.addSetting(
  /** @type {{ _storedFullKeyboardNavigation: number } & SettingConfig} */ ({
    _storedFullKeyboardNavigation: -1,
    id: "useFullKeyboardNavigation",
    pref: "accessibility.tabfocus",
    visible: () => AppConstants.platform == "macosx",
    /**
     * Returns true if any full keyboard nav is enabled and false otherwise, caching
     * the current value to enable proper pref restoration if the checkbox is
     * never changed.
     *
     * accessibility.tabfocus
     * - an integer controlling the focusability of:
     *     1  text controls
     *     2  form elements
     *     4  links
     *     7  all of the above
     */
    get(prefVal) {
      this._storedFullKeyboardNavigation = prefVal;
      return prefVal == 7;
    },
    /**
     * Returns the value of the full keyboard nav preference represented by UI,
     * preserving the preference's "hidden" value if the preference is
     * unchanged and represents a value not strictly allowed in UI.
     */
    set(checked) {
      if (checked) {
        return 7;
      }
      if (this._storedFullKeyboardNavigation != 7) {
        // 1/2/4 values set via about:config should persist
        return this._storedFullKeyboardNavigation;
      }
      // When the checkbox is unchecked, default to just text controls.
      return 1;
    },
  })
);

Preferences.addSetting({
  id: "linkPreviewEnabled",
  pref: "browser.ml.linkPreview.enabled",
  deps: ["aiControlDefault", "aiControlLinkPreviews"],
  visible: ({ aiControlDefault, aiControlLinkPreviews }) => {
    return (
      canShowAiFeature(aiControlLinkPreviews, aiControlDefault) &&
      // @ts-ignore bug 1996860
      LinkPreview.canShowPreferences
    );
  },
});
Preferences.addSetting({
  id: "linkPreviewKeyPoints",
  pref: "browser.ml.linkPreview.optin",
  // LinkPreview.canShowKeyPoints depends on the global genai pref.
  // @ts-ignore bug 1996860
  visible: () => LinkPreview.canShowKeyPoints,
});
Preferences.addSetting({
  id: "linkPreviewLongPress",
  pref: "browser.ml.linkPreview.longPress",
});
Preferences.addSetting({
  id: "alwaysUnderlineLinks",
  pref: "layout.css.always_underline_links",
});
Preferences.addSetting({
  id: "searchStartTyping",
  pref: "accessibility.typeaheadfind",
});
Preferences.addSetting({
  id: "pictureInPictureToggleEnabled",
  pref: "media.videocontrols.picture-in-picture.video-toggle.enabled",
  visible: () =>
    Services.prefs.getBoolPref(
      "media.videocontrols.picture-in-picture.enabled"
    ),
  onUserChange(checked) {
    if (!checked) {
      Glean.pictureinpictureSettings.disableSettings.record();
    }
  },
});
Preferences.addSetting({
  id: "pictureInPictureEnableWhenSwitchingTabs",
  pref: "media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled",
  deps: ["pictureInPictureToggleEnabled"],
  onUserChange(checked) {
    if (checked) {
      Glean.pictureinpictureSettings.enableAutotriggerSettings.record();
    }
  },
});
Preferences.addSetting({
  id: "mediaControlToggleEnabled",
  pref: "media.hardwaremediakeys.enabled",
  // For media control toggle button, we support it on Windows, macOS and
  // gtk-based Linux.
  visible: () =>
    AppConstants.platform == "win" ||
    AppConstants.platform == "macosx" ||
    AppConstants.MOZ_WIDGET_GTK,
});
Preferences.addSetting({
  id: "playDRMContent",
  pref: "media.eme.enabled",
  visible: () => {
    if (!Services.prefs.getBoolPref("browser.eme.ui.enabled", false)) {
      return false;
    }
    if (AppConstants.platform == "win") {
      try {
        return parseFloat(Services.sysinfo.get("version")) >= 6;
      } catch (ex) {
        return false;
      }
    }
    return true;
  },
});
Preferences.addSetting({
  id: "cfrRecommendations",
  pref: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
});
Preferences.addSetting({
  id: "cfrRecommendations-features",
  pref: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
});
Preferences.addSetting({
  id: "browserLayoutRadioGroup",
  pref: "sidebar.verticalTabs",
  get: prefValue => (prefValue ? "true" : "false"),
  set: value => value === "true",
});
Preferences.addSetting({
  id: "browserLayoutShowSidebar",
  pref: "sidebar.revamp",
  onUserChange(checked) {
    if (checked) {
      window.browsingContext.topChromeWindow.SidebarController?.enabledViaSettings(
        true
      );
    }
  },
});
Preferences.addSetting({
  id: "web-appearance-override-warning",
  setup: emitChange => {
    FORCED_COLORS_QUERY.addEventListener("change", emitChange);
    return () => FORCED_COLORS_QUERY.removeEventListener("change", emitChange);
  },
  visible: () => {
    return FORCED_COLORS_QUERY.matches;
  },
});

Preferences.addSetting(
  /** @type {{ themeNames: string[] } & SettingConfig}} */ ({
    id: "web-appearance-chooser",
    themeNames: ["dark", "light", "auto"],
    pref: "layout.css.prefers-color-scheme.content-override",
    setup(emitChange) {
      Services.obs.addObserver(emitChange, "look-and-feel-changed");
      return () =>
        Services.obs.removeObserver(emitChange, "look-and-feel-changed");
    },
    get(val, _, setting) {
      return (
        this.themeNames[val] ||
        this.themeNames[/** @type {number} */ (setting.pref.defaultValue)]
      );
    },
    /** @param {string} val */
    set(val) {
      return this.themeNames.indexOf(val);
    },
    getControlConfig(config) {
      // Set the auto theme image to the light/dark that matches.
      let systemThemeIndex = Services.appinfo
        .contentThemeDerivedColorSchemeIsDark
        ? 2
        : 1;
      config.options[0].controlAttrs = {
        ...config.options[0].controlAttrs,
        imagesrc: config.options[systemThemeIndex].controlAttrs.imagesrc,
      };
      return config;
    },
  })
);

Preferences.addSetting({
  id: "web-appearance-manage-themes-link",
  onUserClick: e => {
    e.preventDefault();
    // @ts-ignore topChromeWindow global
    window.browsingContext.topChromeWindow.BrowserAddonUI.openAddonsMgr(
      "addons://list/theme"
    );
  },
});

Preferences.addSetting({
  id: "acceptLanguages",
  pref: "intl.accept_languages",
  get(prefVal, _, setting) {
    return setting.pref.defaultValue != prefVal
      ? prefVal.toLowerCase()
      : Services.locale.acceptLanguages.toLowerCase();
  },
});
Preferences.addSetting({
  id: "availableLanguages",
  deps: ["acceptLanguages"],
  get(_, { acceptLanguages }) {
    let re = /\s*(?:,|$)\s*/;
    let _acceptLanguages = acceptLanguages.value.split(re);
    let availableLanguages = [];
    let localeCodes = [];
    let localeValues = [];
    let bundle = Services.strings.createBundle(
      "resource://gre/res/language.properties"
    );

    for (let currString of bundle.getSimpleEnumeration()) {
      let property = currString.key.split(".");
      if (property[1] == "accept") {
        localeCodes.push(property[0]);
        localeValues.push(currString.value);
      }
    }

    let localeNames = Services.intl.getLocaleDisplayNames(
      undefined,
      localeCodes
    );

    for (let i in localeCodes) {
      let isVisible =
        localeValues[i] == "true" &&
        (!_acceptLanguages.includes(localeCodes[i]) ||
          !_acceptLanguages[localeCodes[i]]);
      let locale = {
        code: localeCodes[i],
        displayName: localeNames[i],
        isVisible,
      };
      availableLanguages.push(locale);
    }

    return availableLanguages;
  },
});

Preferences.addSetting({
  id: "websiteLanguageWrapper",
  deps: ["acceptLanguages"],
  onUserReorder(event, deps) {
    const { draggedIndex, targetIndex } = event.detail;

    let re = /\s*(?:,|$)\s*/;
    let languages = deps.acceptLanguages.value.split(re).filter(lang => lang);

    const [draggedLang] = languages.splice(draggedIndex, 1);

    languages.splice(targetIndex, 0, draggedLang);

    deps.acceptLanguages.value = languages.join(",");
  },
  getControlConfig(config, deps) {
    let languagePref = deps.acceptLanguages.value;
    let localeCodes = languagePref
      .toLowerCase()
      .split(/\s*,\s*/)
      .filter(code => code.length);
    let localeDisplayNames = Services.intl.getLocaleDisplayNames(
      undefined,
      localeCodes
    );
    /** @type {SettingOptionConfig[]} */
    let availableLanguages = [];
    for (let i = 0; i < localeCodes.length; i++) {
      let displayName = localeDisplayNames[i];
      let localeCode = localeCodes[i];
      availableLanguages.push({
        l10nId: "languages-code-format",
        l10nArgs: {
          locale: displayName,
          code: localeCode,
        },
        control: "moz-box-item",
        key: localeCode,
        options: [
          {
            control: "moz-button",
            slot: "actions-start",
            iconSrc: "chrome://global/skin/icons/delete.svg",
            l10nId: "website-remove-language-button",
            l10nArgs: {
              locale: displayName,
              code: localeCode,
            },
            controlAttrs: {
              locale: localeCode,
              action: "remove",
            },
          },
        ],
      });
    }
    config.options = [config.options[0], ...availableLanguages];
    return config;
  },
  onUserClick(e, deps) {
    let code = e.target.getAttribute("locale");
    let action = e.target.getAttribute("action");
    if (code && action) {
      if (action === "remove") {
        let re = /\s*(?:,|$)\s*/;
        let acceptedLanguages = deps.acceptLanguages.value.split(re);
        let filteredLanguages = acceptedLanguages.filter(
          acceptedCode => acceptedCode !== code
        );
        deps.acceptLanguages.value = filteredLanguages.join(",");
        let closestBoxItem = e.target.closest("moz-box-item");
        closestBoxItem.nextElementSibling
          ? closestBoxItem.nextElementSibling.focus()
          : closestBoxItem.previousElementSibling.focus();
      }
    }
  },
});

Preferences.addSetting({
  id: "websiteLanguageAddLanguage",
  deps: ["websiteLanguagePicker", "acceptLanguages"],
  onUserClick(e, deps) {
    let selectedLanguage = deps.websiteLanguagePicker.value;
    if (selectedLanguage == "-1") {
      return;
    }

    let re = /\s*(?:,|$)\s*/;
    let currentLanguages = deps.acceptLanguages.value.split(re);
    let isAlreadyAccepted = currentLanguages.includes(selectedLanguage);

    if (isAlreadyAccepted) {
      return;
    }

    currentLanguages.unshift(selectedLanguage);
    deps.acceptLanguages.value = currentLanguages.join(",");
  },
});

Preferences.addSetting(
  /** @type {{inputValue: string} & SettingConfig } */ ({
    id: "websiteLanguagePicker",
    deps: ["availableLanguages", "acceptLanguages"],
    inputValue: "-1",
    getControlConfig(config, deps) {
      let re = /\s*(?:,|$)\s*/;
      let availableLanguages =
        /** @type {{ locale: string, code: string, displayName: string, isVisible: boolean }[]} */
        deps.availableLanguages.value;

      let acceptLanguages = new Set(
        /** @type {string} */ (deps.acceptLanguages.value).split(re)
      );

      let sortedOptions = availableLanguages.map(locale => ({
        l10nId: "languages-code-format",
        l10nArgs: {
          locale: locale.displayName,
          code: locale.code,
        },
        hidden: locale.isVisible && acceptLanguages.has(locale.code),
        value: locale.code,
      }));
      // Sort the list of languages by name
      let comp = new Services.intl.Collator(undefined, {
        usage: "sort",
      });

      sortedOptions.sort((a, b) => {
        return comp.compare(a.l10nArgs.locale, b.l10nArgs.locale);
      });

      // Take the existing "Add Language" option and prepend it.
      config.options = [config.options[0], ...sortedOptions];
      return config;
    },
    get(_, deps) {
      if (
        !this.inputValue ||
        deps.acceptLanguages.value.split(",").includes(this.inputValue)
      ) {
        this.inputValue = "-1";
      }
      return this.inputValue;
    },
    set(inputVal) {
      this.inputValue = String(inputVal);
    },
  })
);

Preferences.addSetting({
  id: "containersPane",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneContainers2");
  },
});
Preferences.addSetting({ id: "containersPlaceholder" });
Preferences.addSetting({
  id: "legacyTranslationsVisible",
  deps: ["aiControlDefault", "aiControlTranslations"],
  visible: ({ aiControlDefault, aiControlTranslations }) =>
    !Services.prefs.getBoolPref("browser.settings-redesign.enable", false) &&
    canShowAiFeature(aiControlTranslations, aiControlDefault),
});

Preferences.addSetting({
  id: "offerTranslations",
  pref: "browser.translations.automaticallyPopup",
  deps: ["aiControlDefault", "aiControlTranslations"],
  visible: ({ aiControlDefault, aiControlTranslations }) =>
    canShowAiFeature(aiControlTranslations, aiControlDefault),
});

Preferences.addSetting({
  id: "checkSpelling",
  pref: "layout.spellcheckDefault",
  get: prefVal => prefVal != 0,
  set: val => (val ? 1 : 0),
});

Preferences.addSetting({
  id: "downloadDictionaries",
});

Preferences.addSetting({
  id: "spellCheckPromo",
});

function createNeverTranslateSitesDescription() {
  const description = document.createElement("span");
  description.dataset.l10nId =
    "settings-translations-subpage-never-translate-sites-description";

  for (const [name, src] of [
    ["translations-icon", "chrome://browser/skin/translations.svg"],
    ["settings-icon", "chrome://global/skin/icons/settings.svg"],
  ]) {
    const icon = document.createElement("img");
    icon.src = src;

    icon.dataset.l10nName = name;
    icon.style.verticalAlign = "middle";

    icon.setAttribute("role", "presentation");
    icon.setAttribute("width", "16");
    icon.setAttribute("height", "16");

    description.appendChild(icon);
  }

  return description;
}

Preferences.addSetting({
  id: "translationsDownloadLanguagesGroup",
});

Preferences.addSetting({
  id: "translationsDownloadLanguagesRow",
});

Preferences.addSetting({
  id: "translationsDownloadLanguagesSelect",
});

Preferences.addSetting({
  id: "translationsDownloadLanguagesButton",
});

Preferences.addSetting({
  id: "translationsDownloadLanguagesNoneRow",
});

Preferences.addSetting({
  id: "translationsAlwaysTranslateLanguagesGroup",
});

Preferences.addSetting({
  id: "translationsAlwaysTranslateLanguagesRow",
});

Preferences.addSetting({
  id: "translationsAlwaysTranslateLanguagesSelect",
});

Preferences.addSetting({
  id: "translationsAlwaysTranslateLanguagesNoneRow",
});

Preferences.addSetting({
  id: "translationsAlwaysTranslateLanguagesButton",
});

Preferences.addSetting({
  id: "translationsNeverTranslateLanguagesNoneRow",
});

Preferences.addSetting({
  id: "translationsNeverTranslateLanguagesButton",
});

Preferences.addSetting({
  id: "translationsNeverTranslateLanguagesGroup",
});

Preferences.addSetting({
  id: "translationsNeverTranslateLanguagesRow",
});

Preferences.addSetting({
  id: "translationsNeverTranslateLanguagesSelect",
});

Preferences.addSetting({
  id: "translationsNeverTranslateSitesGroup",
});

Preferences.addSetting({
  id: "translationsNeverTranslateSitesRow",
});

Preferences.addSetting({
  id: "translationsNeverTranslateSitesNoneRow",
});

Preferences.addSetting({
  id: "translationsManageButton",
  deps: ["aiControlDefault", "aiControlTranslations"],
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneTranslations");
  },
  visible: ({ aiControlDefault, aiControlTranslations }) =>
    canShowAiFeature(aiControlTranslations, aiControlDefault),
});

Preferences.addSetting({
  id: "data-migration",
  visible: () =>
    !Services.policies || Services.policies.isAllowed("profileImport"),
  onUserClick() {
    const browserWindow = window.browsingContext.topChromeWindow;
    MigrationUtils.showMigrationWizard(browserWindow, {
      entrypoint: MigrationUtils.MIGRATION_ENTRYPOINTS.PREFERENCES,
    });
  },
});

Preferences.addSetting({
  id: "connectionSettings",
  onUserClick: () => gMainPane.showConnections(),
  controllingExtensionInfo: {
    storeId: PROXY_KEY,
    l10nId: "extension-controlling-proxy-config",
    allowControl: true,
  },
});

Preferences.addSetting({
  id: "profilesPane",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneProfiles");
  },
});
Preferences.addSetting({
  id: "profilesSettings",
  visible() {
    return SelectableProfileService.isEnabled;
  },
  onUserClick: e => {
    e.preventDefault();
    gotoPref("profiles");
  },
});
Preferences.addSetting({
  id: "manageProfiles",
  onUserClick: e => {
    e.preventDefault();
    // Using the existing function for now, since privacy.js also calls it
    gMainPane.manageProfiles();
  },
});
Preferences.addSetting({
  id: "copyProfile",
  deps: ["copyProfileSelect"],
  disabled: ({ copyProfileSelect }) => !copyProfileSelect.value,
  onUserClick: (e, { copyProfileSelect }) => {
    e.preventDefault();
    SelectableProfileService.getProfile(copyProfileSelect.value).then(
      profile => {
        profile?.copyProfile();
        copyProfileSelect.config.set("");
      }
    );
  },
});
Preferences.addSetting({
  id: "copyProfileBox",
  visible: () => SelectableProfileService.initialized,
});
Preferences.addSetting({
  id: "copyProfileError",
  _hasError: false,
  setup(emitChange) {
    this.emitChange = emitChange;
  },
  visible() {
    return this._hasError;
  },
  setError(value) {
    this._hasError = !!value;
    this.emitChange();
  },
});
Preferences.addSetting(
  class ProfileList extends Preferences.AsyncSetting {
    static id = "profileList";
    static PROFILE_UPDATED_OBS = "sps-profiles-updated";
    setup() {
      Services.obs.addObserver(
        this.emitChange,
        ProfileList.PROFILE_UPDATED_OBS
      );
      return () => {
        Services.obs.removeObserver(
          this.emitChange,
          ProfileList.PROFILE_UPDATED_OBS
        );
      };
    }

    async get() {
      let profiles = await SelectableProfileService.getAllProfiles();
      return profiles;
    }
  }
);
Preferences.addSetting({
  id: "copyProfileSelect",
  deps: ["profileList"],
  _selectedProfile: null,
  setup(emitChange) {
    this.emitChange = emitChange;
    document.l10n
      .formatValue("preferences-copy-profile-select")
      .then(result => (this.placeholderString = result));
  },
  get() {
    return this._selectedProfile;
  },
  set(inputVal) {
    this._selectedProfile = inputVal;
    this.emitChange();
  },
  getControlConfig(config, { profileList }) {
    config.options = profileList.value.map(profile => {
      return { controlAttrs: { label: profile.name }, value: profile.id };
    });

    // Put the placeholder at the front of the list.
    config.options.unshift({
      controlAttrs: { label: this.placeholderString },
      value: "",
    });

    return config;
  },
});
Preferences.addSetting({
  id: "copyProfileHeader",
  visible: () => SelectableProfileService.initialized,
});

// Downloads
/*
 * Preferences:
 *
 * browser.download.useDownloadDir - bool
 *   True - Save files directly to the folder configured via the
 *   browser.download.folderList preference.
 *   False - Always ask the user where to save a file and default to
 *  browser.download.lastDir when displaying a folder picker dialog.
 *  browser.download.deletePrivate - bool
 *   True - Delete files that were downloaded in a private browsing session
 *   on close of the session
 *   False - Keep files that were downloaded in a private browsing
 *   session
 * browser.download.always_ask_before_handling_new_types - bool
 *   Defines the default behavior for new file handlers.
 *   True - When downloading a file that doesn't match any existing
 *   handlers, ask the user whether to save or open the file.
 *   False - Save the file. The user can change the default action in
 *   the Applications section in the preferences UI.
 * browser.download.dir - local file handle
 *   A local folder the user may have selected for downloaded files to be
 *   saved. Migration of other browser settings may also set this path.
 *   This folder is enabled when folderList equals 2.
 * browser.download.lastDir - local file handle
 *   May contain the last folder path accessed when the user browsed
 *   via the file save-as dialog. (see contentAreaUtils.js)
 * browser.download.folderList - int
 *   Indicates the location users wish to save downloaded files too.
 *   It is also used to display special file labels when the default
 *   download location is either the Desktop or the Downloads folder.
 *   Values:
 *     0 - The desktop is the default download location.
 *     1 - The system's downloads folder is the default download location.
 *     2 - The default download location is elsewhere as specified in
 *         browser.download.dir.
 * browser.download.downloadDir
 *   deprecated.
 * browser.download.defaultFolder
 *   deprecated.
 */

/**
 * Helper object for managing the various downloads related settings.
 */
const DownloadsHelpers = new (class DownloadsHelpers {
  folder;
  folderPath;
  folderHostPath;
  displayName;
  downloadsDir;
  desktopDir;
  downloadsFolderLocalizedName;
  desktopFolderLocalizedName;

  setupDownloadsHelpersFields = async () => {
    this.downloadsDir = await this._getDownloadsFolder("Downloads");
    this.desktopDir = await this._getDownloadsFolder("Desktop");
    [this.downloadsFolderLocalizedName, this.desktopFolderLocalizedName] =
      await document.l10n.formatValues([
        { id: "downloads-folder-name" },
        { id: "desktop-folder-name" },
      ]);
  };

  /**
   * Returns the Downloads folder.  If aFolder is "Desktop", then the Downloads
   * folder returned is the desktop folder; otherwise, it is a folder whose name
   * indicates that it is a download folder and whose path is as determined by
   * the XPCOM directory service via the download manager's attribute
   * defaultDownloadsDirectory.
   *
   * @throws if aFolder is not "Desktop" or "Downloads"
   */
  async _getDownloadsFolder(aFolder) {
    switch (aFolder) {
      case "Desktop":
        return Services.dirsvc.get("Desk", Ci.nsIFile);
      case "Downloads": {
        let downloadsDir = await Downloads.getSystemDownloadsDirectory();
        return new FileUtils.File(downloadsDir);
      }
    }
    throw new Error(
      "ASSERTION FAILED: folder type should be 'Desktop' or 'Downloads'"
    );
  }

  _getSystemDownloadFolderDetails(folderIndex) {
    let currentDirPref = Preferences.get("browser.download.dir");

    let file;
    let firefoxLocalizedName;
    if (folderIndex == 2 && currentDirPref.value) {
      file = currentDirPref.value;
      if (file.equals(this.downloadsDir)) {
        folderIndex = 1;
      } else if (file.equals(this.desktopDir)) {
        folderIndex = 0;
      }
    }
    switch (folderIndex) {
      case 2: // custom path, handled above.
        break;

      case 1: {
        // downloads
        file = this.downloadsDir;
        firefoxLocalizedName = this.downloadsFolderLocalizedName;
        break;
      }

      case 0:
      // fall through
      default: {
        file = this.desktopDir;
        firefoxLocalizedName = this.desktopFolderLocalizedName;
      }
    }

    if (file) {
      let displayName = file.path;

      // Attempt to translate path to the path as exists on the host
      // in case the provided path comes from the document portal
      if (AppConstants.platform == "linux") {
        if (this.folderHostPath && displayName == this.folderPath) {
          displayName = this.folderHostPath;
          if (displayName == this.downloadsDir.path) {
            firefoxLocalizedName = this.downloadsFolderLocalizedName;
          } else if (displayName == this.desktopDir.path) {
            firefoxLocalizedName = this.desktopFolderLocalizedName;
          }
        } else if (displayName != this.folderPath) {
          this.folderHostPath = null;
          try {
            file.hostPath().then(folderHostPath => {
              this.folderHostPath = folderHostPath;
              Preferences.getSetting("downloadFolder")?.onChange();
            });
          } catch (error) {
            /* ignored */
          }
        }
      }

      if (firefoxLocalizedName) {
        let folderDisplayName, leafName;
        // Either/both of these can throw, so check for failures in both cases
        // so we don't just break display of the download pref:
        try {
          folderDisplayName = file.displayName;
        } catch (ex) {
          /* ignored */
        }
        try {
          leafName = file.leafName;
        } catch (ex) {
          /* ignored */
        }

        // If we found a localized name that's different from the leaf name,
        // use that:
        if (folderDisplayName && folderDisplayName != leafName) {
          return { file, folderDisplayName };
        }

        // Otherwise, check if we've got a localized name ourselves.
        // You can't move the system download or desktop dir on macOS,
        // so if those are in use just display them. On other platforms
        // only do so if the folder matches the localized name.
        if (
          AppConstants.platform == "macosx" ||
          leafName == firefoxLocalizedName
        ) {
          return { file, folderDisplayName: firefoxLocalizedName };
        }
      }

      // If we get here, attempts to use a "pretty" name failed. Just display
      // the full path:
      // Force the left-to-right direction when displaying a custom path.
      return { file, folderDisplayName: `\u2066${displayName}\u2069` };
    }

    // Don't even have a file - fall back to desktop directory for the
    // use of the icon, and an empty label:
    file = this.desktopDir;
    return { file, folderDisplayName: "" };
  }

  /**
   * Determines the type of the given folder.
   *
   * @param   aFolder
   *          the folder whose type is to be determined
   * @returns integer
   *          0 if aFolder is the Desktop or is unspecified,
   *          1 if aFolder is the Downloads folder,
   *          2 otherwise
   */
  _folderToIndex(aFolder) {
    if (!aFolder || aFolder.equals(this.desktopDir)) {
      return 0;
    } else if (aFolder.equals(this.downloadsDir)) {
      return 1;
    }
    return 2;
  }

  getFolderDetails() {
    let folderIndex = Preferences.get("browser.download.folderList").value;
    let { folderDisplayName, file } =
      this._getSystemDownloadFolderDetails(folderIndex);

    this.folderPath = file?.path ?? "";
    this.displayName = folderDisplayName;
  }

  setFolder(folder) {
    this.folder = folder;

    let folderListPref = Preferences.get("browser.download.folderList");
    folderListPref.value = this._folderToIndex(this.folder);
  }
})();

Preferences.addSetting({
  id: "browserDownloadFolderList",
  pref: "browser.download.folderList",
});
Preferences.addSetting({
  id: "downloadFolder",
  pref: "browser.download.dir",
  deps: ["browserDownloadFolderList"],
  get() {
    DownloadsHelpers.getFolderDetails();
    return DownloadsHelpers.folderPath;
  },
  set(folder) {
    DownloadsHelpers.setFolder(folder);
    return DownloadsHelpers.folder;
  },
  getControlConfig(config) {
    if (DownloadsHelpers.displayName) {
      return {
        ...config,
        controlAttrs: {
          ...config.controlAttrs,
          ".displayValue": DownloadsHelpers.displayName,
        },
      };
    }
    return {
      ...config,
    };
  },
  setup(emitChange) {
    DownloadsHelpers.setupDownloadsHelpersFields().then(emitChange);
  },
  disabled: ({ browserDownloadFolderList }) => {
    return browserDownloadFolderList.locked;
  },
});
Preferences.addSetting({
  id: "alwaysAsk",
  pref: "browser.download.useDownloadDir",
});
Preferences.addSetting({
  id: "enableDeletePrivate",
  pref: "browser.download.enableDeletePrivate",
});
Preferences.addSetting({
  id: "deletePrivate",
  pref: "browser.download.deletePrivate",
  deps: ["enableDeletePrivate"],
  visible: ({ enableDeletePrivate }) => enableDeletePrivate.value,
  onUserChange() {
    Services.prefs.setBoolPref("browser.download.deletePrivate.chosen", true);
  },
});
/**
 * A helper object containing all logic related to
 * setting the browser as the user's default.
 */
const DefaultBrowserHelper = {
  /**
   * @type {number}
   */
  _backoffIndex: 0,

  /**
   * @type {number | undefined}
   */
  _pollingTimer: undefined,

  /**
   * Keeps track of the last known browser
   * default value set to compare while polling.
   *
   * @type {boolean | undefined}
   */
  _lastPolledIsDefault: undefined,

  /**
   * @type {typeof import('../shell/ShellService.sys.mjs').ShellService | undefined}
   */
  get shellSvc() {
    return (
      AppConstants.HAVE_SHELL_SERVICE &&
      // @ts-ignore from utilityOverlay.js
      getShellService()
    );
  },

  /**
   * Sets up polling of whether the browser is set to default,
   * and calls provided hasChanged function when the state changes.
   *
   * @param {Function} hasChanged
   */
  pollForDefaultChanges(hasChanged) {
    if (this._pollingTimer) {
      return;
    }
    this._lastPolledIsDefault = this.isBrowserDefault;

    // Exponential backoff mechanism will delay the polling times if user doesn't
    // trigger SetDefaultBrowser for a long time.
    const backoffTimes = [
      1000, 1000, 1000, 1000, 2000, 2000, 2000, 5000, 5000, 10000,
    ];

    const pollForDefaultBrowser = () => {
      if (
        (location.hash == "" ||
          location.hash == "#general" ||
          location.hash == "#sync") &&
        document.visibilityState == "visible"
      ) {
        const { isBrowserDefault } = this;
        if (isBrowserDefault !== this._lastPolledIsDefault) {
          this._lastPolledIsDefault = isBrowserDefault;
          hasChanged();
        }
      }

      if (!this._pollingTimer) {
        return;
      }

      // approximately a "requestIdleInterval"
      this._pollingTimer = window.setTimeout(
        () => {
          window.requestIdleCallback(pollForDefaultBrowser);
        },
        backoffTimes[
          this._backoffIndex + 1 < backoffTimes.length
            ? this._backoffIndex++
            : backoffTimes.length - 1
        ]
      );
    };

    this._pollingTimer = window.setTimeout(() => {
      window.requestIdleCallback(pollForDefaultBrowser);
    }, backoffTimes[this._backoffIndex]);
  },

  /**
   * Stops timer for polling changes.
   */
  clearPollingForDefaultChanges() {
    if (this._pollingTimer) {
      clearTimeout(this._pollingTimer);
      this._pollingTimer = undefined;
    }
  },

  /**
   *  Checks if the browser is default through the shell service.
   */
  get isBrowserDefault() {
    if (!this.canCheck) {
      return false;
    }
    return this.shellSvc?.isDefaultBrowser(false, true);
  },

  /**
   * Attempts to set the browser as the user's
   * default through the shell service.
   *
   * @returns {Promise<void>}
   */
  async setDefaultBrowser() {
    // Reset exponential backoff delay time in order to do visual update in pollForDefaultBrowser.
    this._backoffIndex = 0;

    try {
      await this.shellSvc?.setDefaultBrowser(false);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Checks whether the browser is capable of being made default.
   *
   * @type {boolean}
   */
  get canCheck() {
    return (
      this.shellSvc &&
      /**
       * Flatpak does not support setting nor detection of default browser
       */
      !gGIOService?.isRunningUnderFlatpak
    );
  },
};

Preferences.addSetting({
  id: "alwaysCheckDefault",
  pref: "browser.shell.checkDefaultBrowser",
  setup: emitChange => {
    if (!DefaultBrowserHelper.canCheck) {
      return;
    }
    DefaultBrowserHelper.pollForDefaultChanges(emitChange);
    // eslint-disable-next-line consistent-return
    return () => DefaultBrowserHelper.clearPollingForDefaultChanges();
  },
  /**
   * Show button for setting browser as default browser or information that
   * browser is already the default browser.
   */
  visible: () => DefaultBrowserHelper.canCheck,
  disabled: (_, setting) =>
    !DefaultBrowserHelper.canCheck ||
    setting.locked ||
    DefaultBrowserHelper.isBrowserDefault,
});

Preferences.addSetting({
  id: "isDefaultPane",
  deps: ["alwaysCheckDefault"],
  visible: () =>
    DefaultBrowserHelper.canCheck && DefaultBrowserHelper.isBrowserDefault,
});

Preferences.addSetting({
  id: "isNotDefaultPane",
  deps: ["alwaysCheckDefault"],
  visible: () =>
    DefaultBrowserHelper.canCheck && !DefaultBrowserHelper.isBrowserDefault,
  onUserClick: (e, { alwaysCheckDefault }) => {
    if (!DefaultBrowserHelper.canCheck) {
      return;
    }
    const setDefaultButton = /** @type {MozButton} */ (e.target);

    if (!setDefaultButton) {
      return;
    }
    if (setDefaultButton.disabled) {
      return;
    }

    /**
     * Disable the set default button, so that the user
     * doesn't try to hit it again while browser is being set to default.
     */
    setDefaultButton.disabled = true;
    alwaysCheckDefault.value = true;
    DefaultBrowserHelper.setDefaultBrowser().finally(() => {
      setDefaultButton.disabled = false;
    });
  },
});

// Firefox support settings
Preferences.addSetting({
  id: "supportLinksGroup",
});
Preferences.addSetting({
  id: "supportGetHelp",
});
Preferences.addSetting({
  id: "supportShareIdeas",
});

// Performance settings
Preferences.addSetting({
  id: "contentProcessCount",
  pref: "dom.ipc.processCount",
});
Preferences.addSetting({
  id: "allowHWAccel",
  pref: "layers.acceleration.disabled",
  deps: ["useRecommendedPerformanceSettings"],
  visible({ useRecommendedPerformanceSettings }) {
    return !useRecommendedPerformanceSettings.value;
  },
});
Preferences.addSetting({
  id: "useRecommendedPerformanceSettings",
  pref: "browser.preferences.defaultPerformanceSettings.enabled",
  deps: ["contentProcessCount", "allowHWAccel"],
  get(val, { allowHWAccel, contentProcessCount }) {
    if (
      allowHWAccel.value != allowHWAccel.pref.defaultValue ||
      contentProcessCount.value != contentProcessCount.pref.defaultValue
    ) {
      return false;
    }
    return val;
  },
  set(val, { allowHWAccel, contentProcessCount }) {
    if (val) {
      contentProcessCount.value = contentProcessCount.pref.defaultValue;
      allowHWAccel.value = allowHWAccel.pref.defaultValue;
    }
    return val;
  },
});

Preferences.addSetting({
  id: "payment-item",
  async onUserClick(e) {
    const action = e.target.getAttribute("action");
    const guid = e.target.getAttribute("guid");
    if (action === "remove") {
      let [title, confirm, cancel] = await document.l10n.formatValues([
        { id: "payments-delete-payment-prompt-title" },
        { id: "payments-delete-payment-prompt-confirm-button" },
        { id: "payments-delete-payment-prompt-cancel-button" },
      ]);
      FormAutofillPreferences.prototype.openRemovePaymentDialog(
        guid,
        window.browsingContext.topChromeWindow.browsingContext,
        title,
        confirm,
        cancel
      );
    } else if (action === "edit") {
      FormAutofillPreferences.prototype.openEditCreditCardDialog(guid, window);
    }
  },
});

Preferences.addSetting({
  id: "add-payment-button",
  deps: ["saveAndFillPayments"],
  setup: (emitChange, _, setting) => {
    function updateDepsAndChange() {
      setting._deps = null;
      emitChange();
    }
    Services.obs.addObserver(
      updateDepsAndChange,
      "formautofill-preferences-initialized"
    );
    return () =>
      Services.obs.removeObserver(
        updateDepsAndChange,
        "formautofill-preferences-initialized"
      );
  },
  onUserClick: ({ target }) => {
    target.ownerGlobal.gSubDialog.open(
      "chrome://formautofill/content/editCreditCard.xhtml"
    );
  },
  disabled: ({ saveAndFillPayments }) => !saveAndFillPayments?.value,
});

Preferences.addSetting({
  id: "payments-list-header",
});

Preferences.addSetting({
  id: "no-payments-stored",
});

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "payments-list";

    /** @type {Promise<any[]>} */
    paymentMethods;

    beforeRefresh() {
      this.paymentMethods = this.getPaymentMethods();
    }

    async getPaymentMethods() {
      await FormAutofillPreferences.prototype.initializePaymentsStorage();
      return FormAutofillPreferences.prototype.makePaymentsListItems();
    }

    async getControlConfig() {
      return {
        items: await this.paymentMethods,
      };
    }

    async visible() {
      return Boolean((await this.paymentMethods).length);
    }

    setup() {
      Services.obs.addObserver(this.emitChange, "formautofill-storage-changed");
      return () =>
        Services.obs.removeObserver(
          this.emitChange,
          "formautofill-storage-changed"
        );
    }
  }
);

// Tabs settings

// "Opening" tabs settings
Preferences.addSetting({
  id: "tabsOpening",
});
/**
 * browser.link.open_newwindow - int
 *   Determines where links targeting new windows should open.
 *   Values:
 *     1 - Open in the current window or tab.
 *     2 - Open in a new window.
 *     3 - Open in a new tab in the most recent window.
 */
Preferences.addSetting({
  id: "linkTargeting",
  pref: "browser.link.open_newwindow",
  /**
   * Determines where a link which opens a new window will open.
   *
   * @returns |true| if such links should be opened in new tabs
   */
  get: prefVal => {
    return prefVal != 2;
  },
  /**
   * Determines where a link which opens a new window will open.
   *
   * @returns 2 if such links should be opened in new windows,
   *          3 if such links should be opened in new tabs
   */
  set: checked => {
    return checked ? 3 : 2;
  },
});
/**
 * browser.tabs.loadInBackground - bool
 *  True - Whether browser should switch to a new tab opened from a link.
 */
Preferences.addSetting({
  id: "switchToNewTabs",
  pref: "browser.tabs.loadInBackground",
});
Preferences.addSetting({
  id: "openAppLinksNextToActiveTab",
  pref: "browser.link.open_newwindow.override.external",
  /**
   * @returns {boolean}
   *   Whether the "Open links in tabs instead of new windows" settings
   *   checkbox should be checked. Should only be checked if the
   *   `browser.link.open_newwindow.override.external` pref is set to the
   *   value of 7 (nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT).
   */
  get: prefVal => {
    return prefVal == Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT;
  },
  /**
   * This pref has at least 8 valid values but we are offering a checkbox
   * to set one specific value (`7`).
   *
   * @param {boolean} checked
   * @returns {number}
   *   - `7` (`nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT`) if checked
   *   - the default value of
   *     `browser.link.open_newwindow.override.external` if not checked
   */
  set: (checked, _, setting) => {
    return checked
      ? Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT
      : setting.pref.defaultValue;
  },
  onUserChange: checked => {
    Glean.linkHandling.openNextToActiveTabSettingsEnabled.set(checked);
    Glean.linkHandling.openNextToActiveTabSettingsChange.record({
      checked,
    });
  },
});
/**
 * browser.tabs.warnOnOpen - bool
 *   True - Whether the user should be warned when trying to open a lot of
 *          tabs at once (e.g. a large folder of bookmarks), allowing to
 *          cancel the action.
 */
Preferences.addSetting({
  id: "warnOpenMany",
  pref: "browser.tabs.warnOnOpen",
  // The "opening multiple tabs might slow down Firefox" warning provides
  // an option for not showing this warning again. When the user disables it,
  // we provide checkboxes to re-enable the warning.
  visible: () => TransientPrefs.prefShouldBeVisible("browser.tabs.warnOnOpen"),
});

// AI Control pref settings
Preferences.addSetting({
  id: "aiControlDefault",
  pref: "browser.ai.control.default",
});
Preferences.addSetting({
  id: "aiControlTranslations",
  pref: "browser.ai.control.translations",
});
Preferences.addSetting({
  id: "aiControlPdfjsAltText",
  pref: "browser.ai.control.pdfjsAltText",
});
Preferences.addSetting({
  id: "aiControlSmartTabGroups",
  pref: "browser.ai.control.smartTabGroups",
});
Preferences.addSetting({
  id: "aiControlLinkPreviews",
  pref: "browser.ai.control.linkPreviewKeyPoints",
});
Preferences.addSetting({
  id: "aiControlSidebarChatbot",
  pref: "browser.ai.control.sidebarChatbot",
});

// "Interaction" tabs settings
Preferences.addSetting({
  id: "tabsInteraction",
});
Preferences.addSetting({
  id: "ctrlTabRecentlyUsedOrder",
  pref: "browser.ctrlTab.sortByRecentlyUsed",
  onUserClick: () => {
    Services.prefs.clearUserPref("browser.ctrlTab.migrated");
  },
});
Preferences.addSetting({
  id: "tabHoverPreview",
  pref: "browser.tabs.hoverPreview.enabled",
});
Preferences.addSetting({
  id: "tabPreviewShowThumbnails",
  pref: "browser.tabs.hoverPreview.showThumbnails",
  deps: ["tabHoverPreview"],
  visible: ({ tabHoverPreview }) => !!tabHoverPreview.value,
});
Preferences.addSetting({
  id: "tabGroups",
  pref: "browser.tabs.groups.enabled",
});
Preferences.addSetting({
  id: "smartTabGroups",
  pref: "browser.tabs.groups.smart.enabled",
});
Preferences.addSetting({
  id: "tabGroupSuggestions",
  pref: "browser.tabs.groups.smart.userEnabled",
  deps: [
    "tabGroups",
    "smartTabGroups",
    "aiControlDefault",
    "aiControlSmartTabGroups",
  ],
  visible: ({
    smartTabGroups,
    tabGroups,
    aiControlDefault,
    aiControlSmartTabGroups,
  }) => {
    return (
      canShowAiFeature(aiControlSmartTabGroups, aiControlDefault) &&
      !!tabGroups.value &&
      !!smartTabGroups.value &&
      Services.locale.appLocaleAsBCP47.startsWith("en")
    );
  },
});
if (AppConstants.platform === "win") {
  /**
   * browser.taskbar.previews.enable - bool
   *   True - Tabs are to be shown in Windows 7 taskbar.
   *   False - Only the window is to be shown in Windows 7 taskbar.
   */
  Preferences.addSetting({
    id: "showTabsInTaskbar",
    pref: "browser.taskbar.previews.enable",
    // Functionality for "Show tabs in taskbar" on Windows 7 and up.
    visible: () => {
      if (AppConstants.platform !== "win") {
        return false;
      }

      try {
        let ver = parseFloat(Services.sysinfo.getProperty("version"));
        return ver >= 6.1;
      } catch (ex) {
        return false;
      }
    },
  });
} else {
  // Not supported unless we're on Windows
  Preferences.addSetting({ id: "showTabsInTaskbar", visible: () => false });
}

// "Containers" tabs settings
Preferences.addSetting({
  id: "privacyUserContextUI",
  pref: "privacy.userContext.ui.enabled",
});
Preferences.addSetting({
  id: "browserContainersbox",
  deps: ["privacyUserContextUI"],
  visible: ({ privacyUserContextUI }) => !!privacyUserContextUI.value,
});
Preferences.addSetting({
  id: "browserContainersCheckbox",
  pref: "privacy.userContext.enabled",
  controllingExtensionInfo: {
    storeId: "privacy.containers",
    l10nId: "extension-controlling-privacy-containers",
  },
  async promptToCloseTabsAndDisable(count, setting) {
    let [title, message, okButton, cancelButton] =
      await document.l10n.formatValues([
        { id: "containers-disable-alert-title" },
        { id: "containers-disable-alert-desc", args: { tabCount: count } },
        { id: "containers-disable-alert-ok-button", args: { tabCount: count } },
        { id: "containers-disable-alert-cancel-button" },
      ]);

    let buttonFlags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1;

    let rv = Services.prompt.confirmEx(
      window,
      title,
      message,
      buttonFlags,
      okButton,
      cancelButton,
      null,
      null,
      {}
    );

    // User confirmed - disable containers and close container tabs.
    if (rv == 0) {
      await ContextualIdentityService.closeContainerTabs();
      setting.pref.value = false;
    }

    // Keep the checkbox checked when the user opts not to close tabs.
    return true;
  },
  set(val, _, setting) {
    // When enabling container tabs, just set the pref value.
    if (val) {
      return val;
    }

    // When disabling container tabs, check if there are container tabs currently
    // open. If there aren't, then proceed with disabling.
    let count = ContextualIdentityService.countContainerTabs();
    if (count == 0) {
      return false;
    }

    // When disabling container tabs with container tabs currently open show a
    // dialog to determine whether or not the tabs should be closed.
    return this.promptToCloseTabsAndDisable(count, setting);
  },
});
Preferences.addSetting({
  id: "browserContainersSettings",
  deps: ["browserContainersCheckbox"],
  /**
   * Displays container panel for customising and adding containers.
   */
  onUserClick: () => {
    gotoPref("containers");
  },
  getControlConfig: config => {
    let searchKeywords = [
      "user-context-personal",
      "user-context-work",
      "user-context-banking",
      "user-context-shopping",
    ]
      .map(ContextualIdentityService.formatContextLabel)
      .join(" ");
    config.controlAttrs.searchkeywords = searchKeywords;
    return config;
  },
  disabled: ({ browserContainersCheckbox }) => !browserContainersCheckbox.value,
});

// "Closing" tabs settings
Preferences.addSetting({
  id: "tabsClosing",
});
/**
 * browser.tabs.warnOnClose - bool
 *   True - If when closing a window with multiple tabs the user is warned and
 *          allowed to cancel the action, false to just close the window.
 */
Preferences.addSetting({
  id: "warnCloseMultiple",
  pref: "browser.tabs.warnOnClose",
});
/**
 * browser.warnOnQuitShortcut - bool
 *   True - If the keyboard shortcut (Ctrl/Cmd+Q) is pressed, the user should
 *          be warned, false to just quit without prompting.
 */
Preferences.addSetting({
  id: "warnOnQuitKey",
  pref: "browser.warnOnQuitShortcut",
  setup() {
    let quitKeyElement =
      window.browsingContext.topChromeWindow.document.getElementById(
        "key_quitApplication"
      );
    if (quitKeyElement) {
      this.quitKey = ShortcutUtils.prettifyShortcut(quitKeyElement);
    }
  },
  visible() {
    return AppConstants.platform !== "win" && this.quitKey;
  },
  getControlConfig(config) {
    return {
      ...config,
      l10nArgs: { quitKey: this.quitKey },
    };
  },
});

/**
 * Helper object for managing the various zoom related settings.
 */
const ZoomHelpers = {
  win: window.browsingContext.topChromeWindow,
  get FullZoom() {
    return this.win.FullZoom;
  },
  get ZoomManager() {
    return this.win.ZoomManager;
  },

  /**
   * Set the global default zoom value.
   *
   * @param {number} newZoom - The new zoom
   * @returns {Promise<void>}
   */
  async setDefaultZoom(newZoom) {
    let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );
    let nonPrivateLoadContext = Cu.createLoadContext();
    let resolvers = Promise.withResolvers();
    /* Because our setGlobal function takes in a browsing context, and
     * because we want to keep this property consistent across both private
     * and non-private contexts, we create a non-private context and use that
     * to set the property, regardless of our actual context.
     */
    cps2.setGlobal(this.FullZoom.name, newZoom, nonPrivateLoadContext, {
      handleCompletion: resolvers.resolve,
      handleError: resolvers.reject,
    });
    return resolvers.promise;
  },

  async getDefaultZoom() {
    /** @import { ZoomUI as GlobalZoomUI } from "resource:///modules/ZoomUI.sys.mjs" */
    /** @type {GlobalZoomUI} */
    let ZoomUI = this.win.ZoomUI;
    return await ZoomUI.getGlobalValue();
  },

  /**
   * The possible zoom values.
   *
   * @returns {number[]}
   */
  get zoomValues() {
    return this.ZoomManager.zoomValues;
  },

  toggleFullZoom() {
    this.ZoomManager.toggleZoom();
  },
};
Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "defaultZoom";
    /** @type {Record<"options", object[]>} */
    optionsConfig;

    /**
     * @param {string} val - zoom value as a string
     */
    async set(val) {
      ZoomHelpers.setDefaultZoom(
        parseFloat((parseInt(val, 10) / 100).toFixed(2))
      );
    }
    async get() {
      return Math.round((await ZoomHelpers.getDefaultZoom()) * 100);
    }
    async getControlConfig() {
      if (!this.optionsConfig) {
        this.optionsConfig = {
          options: ZoomHelpers.zoomValues.map(a => {
            let value = Math.round(a * 100);
            return {
              value,
              l10nId: "preferences-default-zoom-value",
              l10nArgs: { percentage: value },
            };
          }),
        };
      }
      return this.optionsConfig;
    }
  }
);
Preferences.addSetting({
  id: "zoomTextPref",
  pref: "browser.zoom.full",
});
Preferences.addSetting({
  id: "zoomText",
  deps: ["zoomTextPref"],
  // Use the Setting since the ZoomManager getter may not have updated yet.
  get: (_, { zoomTextPref }) => !zoomTextPref.value,
  set: () => ZoomHelpers.toggleFullZoom(),
  disabled: ({ zoomTextPref }) => zoomTextPref.locked,
});
Preferences.addSetting({
  id: "zoomWarning",
  deps: ["zoomText"],
  visible: ({ zoomText }) => Boolean(zoomText.value),
});

/**
 * Helper object for managing font-related settings.
 */
const FontHelpers = {
  _enumerator: null,
  _allFonts: null,

  get enumerator() {
    if (!this._enumerator) {
      this._enumerator = Cc["@mozilla.org/gfx/fontenumerator;1"].createInstance(
        Ci.nsIFontEnumerator
      );
    }
    return this._enumerator;
  },

  ensurePref(prefId, type) {
    let pref = Preferences.get(prefId);
    if (!pref) {
      pref = Preferences.add({ id: prefId, type });
    }
    return pref;
  },

  get langGroup() {
    return Services.locale.fontLanguageGroup;
  },

  getFontTypePrefId(langGroup) {
    return `font.default.${langGroup}`;
  },

  getFontType(langGroup) {
    const prefId = this.getFontTypePrefId(langGroup);
    return Services.prefs.getCharPref(prefId, "serif");
  },

  getFontPrefId(langGroup) {
    const fontType = this.getFontType(langGroup);
    return `font.name.${fontType}.${langGroup}`;
  },

  getSizePrefId(langGroup) {
    return `font.size.variable.${langGroup}`;
  },

  buildFontOptions(langGroup, fontType) {
    let fonts = this.enumerator.EnumerateFonts(langGroup, fontType);
    let defaultFont = null;
    if (fonts.length) {
      defaultFont = this.enumerator.getDefaultFont(langGroup, fontType);
    } else {
      fonts = this.enumerator.EnumerateFonts(langGroup, "");
      if (fonts.length) {
        defaultFont = this.enumerator.getDefaultFont(langGroup, "");
      }
    }

    if (!this._allFonts) {
      this._allFonts = this.enumerator.EnumerateAllFonts();
    }

    const options = [];

    if (fonts.length) {
      if (defaultFont) {
        options.push({
          value: "",
          l10nId: "fonts-label-default",
          l10nArgs: { name: defaultFont },
        });
      } else {
        options.push({
          value: "",
          l10nId: "fonts-label-default-unnamed",
        });
      }

      for (const font of fonts) {
        options.push({
          value: font,
          controlAttrs: { label: font },
        });
      }
    }

    if (this._allFonts.length > fonts.length) {
      const fontSet = new Set(fonts);
      for (const font of this._allFonts) {
        if (!fontSet.has(font)) {
          options.push({
            value: font,
            controlAttrs: { label: font },
          });
        }
      }
    }

    return options;
  },

  fontSizeOptions: [
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36,
    40, 44, 48, 56, 64, 72,
  ].map(size => ({ value: size, controlAttrs: { label: String(size) } })),
};

Preferences.addSetting({
  id: "fontLanguageGroup",
  pref: "font.language.group",
});

Preferences.addSetting({
  id: "fontType",
  deps: ["fontLanguageGroup"],
  setup(emitChange, deps, setting) {
    const handleChange = () => {
      setting.pref = FontHelpers.ensurePref(
        FontHelpers.getFontTypePrefId(FontHelpers.langGroup),
        "string"
      );
      emitChange();
    };

    handleChange();
    deps.fontLanguageGroup.on("change", handleChange);
    return () => deps.fontLanguageGroup.off("change", handleChange);
  },
});

Preferences.addSetting({
  id: "defaultFont",
  deps: ["fontType"],
  optionsConfig: null,
  setup(emitChange, deps, setting) {
    const handleChange = () => {
      setting.pref = FontHelpers.ensurePref(
        FontHelpers.getFontPrefId(FontHelpers.langGroup),
        "fontname"
      );
      this.optionsConfig = null;
      emitChange();
    };
    handleChange();
    deps.fontType.on("change", handleChange);
    return () => deps.fontType.off("change", handleChange);
  },
  getControlConfig(config) {
    if (!this.optionsConfig) {
      this.optionsConfig = {
        ...config,
        options: FontHelpers.buildFontOptions(
          FontHelpers.langGroup,
          FontHelpers.getFontType(FontHelpers.langGroup)
        ),
      };
    }
    return this.optionsConfig;
  },
});

Preferences.addSetting({
  id: "defaultFontSize",
  deps: ["fontLanguageGroup"],
  setup(emitChange, deps, setting) {
    const handleLangChange = () => {
      setting.pref = FontHelpers.ensurePref(
        FontHelpers.getSizePrefId(FontHelpers.langGroup),
        "int"
      );
      emitChange();
    };
    handleLangChange();
    deps.fontLanguageGroup.on("change", handleLangChange);
    return () => deps.fontLanguageGroup.off("change", handleLangChange);
  },
  getControlConfig(config) {
    return { ...config, options: FontHelpers.fontSizeOptions };
  },
});

Preferences.addSetting({
  id: "advancedFonts",
  onUserClick: () => gMainPane.configureFonts(),
});

Preferences.addSetting({
  id: "contrastControlSettings",
  pref: "browser.display.document_color_use",
});
Preferences.addSetting({
  id: "colors",
  onUserClick() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/colors.xhtml",
      { features: "resizable=no" }
    );
  },
});

Preferences.addSetting({
  /** @type {{ _removeAddressDialogStrings: string[] } & SettingConfig} */
  id: "address-item",
  _removeAddressDialogStrings: [],
  onUserClick(e) {
    const action = e.target.getAttribute("action");
    const guid = e.target.getAttribute("guid");
    if (action === "remove") {
      let [title, confirm, cancel] = this._removeAddressDialogStrings;
      FormAutofillPreferences.prototype.openRemoveAddressDialog(
        guid,
        window.browsingContext.topChromeWindow.browsingContext,
        title,
        confirm,
        cancel
      );
    } else if (action === "edit") {
      FormAutofillPreferences.prototype.openEditAddressDialog(guid, window);
    }
  },
  setup(emitChange) {
    document.l10n
      .formatValues([
        { id: "addresses-delete-address-prompt-title" },
        { id: "addresses-delete-address-prompt-confirm-button" },
        { id: "addresses-delete-address-prompt-cancel-button" },
      ])
      .then(val => (this._removeAddressDialogStrings = val))
      .then(emitChange);
  },
  disabled() {
    return !!this._removeAddressDialogStrings.length;
  },
});

Preferences.addSetting({
  id: "add-address-button",
  deps: ["saveAndFillAddresses"],
  setup: (emitChange, _, setting) => {
    function updateDepsAndChange() {
      setting._deps = null;
      emitChange();
    }
    Services.obs.addObserver(
      updateDepsAndChange,
      "formautofill-preferences-initialized"
    );
    return () =>
      Services.obs.removeObserver(
        updateDepsAndChange,
        "formautofill-preferences-initialized"
      );
  },
  onUserClick: () => {
    FormAutofillPreferences.prototype.openEditAddressDialog(undefined, window);
  },
  disabled: ({ saveAndFillAddresses }) => !saveAndFillAddresses?.value,
});

Preferences.addSetting({
  id: "addresses-list-header",
});

Preferences.addSetting({
  id: "no-addresses-stored",
});

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "addresses-list";

    async getAddresses() {
      await FormAutofillPreferences.prototype.initializeAddressesStorage();
      return FormAutofillPreferences.prototype.makeAddressesListItems();
    }

    async getControlConfig() {
      return {
        items: await this.getAddresses(),
      };
    }

    setup() {
      Services.obs.addObserver(this.emitChange, "formautofill-storage-changed");
      return () =>
        Services.obs.removeObserver(
          this.emitChange,
          "formautofill-storage-changed"
        );
    }

    async visible() {
      const items = await this.getAddresses();
      return !!items.length;
    }
  }
);

function createDefaultBrowserConfig({
  includeIsDefaultPane = true,
  inProgress = false,
} = {}) {
  const isDefaultPane = {
    id: "isDefaultPane",
    l10nId: "is-default-browser-2",
    control: "moz-promo",
  };

  const isNotDefaultPane = {
    id: "isNotDefaultPane",
    l10nId: "is-not-default-browser-2",
    control: "moz-promo",
    options: [
      {
        control: "moz-button",
        l10nId: "set-as-my-default-browser-2",
        id: "setDefaultButton",
        slot: "actions",
        controlAttrs: {
          type: "primary",
        },
      },
    ],
  };

  const items = includeIsDefaultPane
    ? [isDefaultPane, isNotDefaultPane]
    : [isNotDefaultPane];

  return {
    l10nId: "home-default-browser-title",
    headingLevel: 2,
    items,
    ...(inProgress && { inProgress }),
  };
}

SettingGroupManager.registerGroups({
  profilePane: {
    headingLevel: 2,
    id: "browserProfilesGroupPane",
    l10nId: "preferences-profiles-subpane-description",
    supportPage: "profile-management",
    items: [
      {
        id: "manageProfiles",
        control: "moz-box-button",
        l10nId: "preferences-manage-profiles-button",
      },
      {
        id: "copyProfileHeader",
        l10nId: "preferences-copy-profile-header",
        headingLevel: 2,
        supportPage: "profile-management",
        control: "moz-fieldset",
        items: [
          {
            id: "copyProfileBox",
            l10nId: "preferences-profile-to-copy",
            control: "moz-box-item",
            items: [
              {
                id: "copyProfileSelect",
                control: "moz-select",
                slot: "actions",
              },
              {
                id: "copyProfile",
                l10nId: "preferences-copy-profile-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  type: "primary",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  profiles: {
    id: "profilesGroup",
    l10nId: "preferences-profiles-section-header",
    headingLevel: 2,
    supportPage: "profile-management",
    items: [
      {
        id: "profilesSettings",
        control: "moz-box-button",
        l10nId: "preferences-profiles-settings-button",
      },
    ],
  },
  defaultBrowser: createDefaultBrowserConfig(),
  startup: {
    l10nId: "startup-group",
    headingLevel: 2,
    items: [
      {
        id: "browserRestoreSession",
        l10nId: "startup-restore-windows-and-tabs",
      },
      {
        id: "windowsLaunchOnLogin",
        l10nId: "windows-launch-on-login",
      },
      {
        id: "windowsLaunchOnLoginDisabledBox",
        control: "moz-message-bar",
        options: [
          {
            control: "span",
            l10nId: "windows-launch-on-login-disabled",
            slot: "message",
            options: [
              {
                control: "a",
                controlAttrs: {
                  "data-l10n-name": "startup-link",
                  href: "ms-settings:startupapps",
                  target: "_self",
                },
              },
            ],
          },
        ],
      },
      {
        id: "windowsLaunchOnLoginDisabledProfileBox",
        control: "moz-message-bar",
        l10nId: "startup-windows-launch-on-login-profile-disabled",
      },
      {
        id: "alwaysCheckDefault",
        l10nId: "always-check-default",
      },
    ],
  },
  importBrowserData: {
    l10nId: "preferences-data-migration-group",
    headingLevel: 2,
    items: [
      {
        id: "data-migration",
        l10nId: "preferences-data-migration-button",
        control: "moz-box-button",
      },
    ],
  },
  homepage: {
    inProgress: true,
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/window-firefox.svg",
    l10nId: "home-homepage-title",
    items: [
      {
        id: "homepageNewWindows",
        control: "moz-select",
        l10nId: "home-homepage-new-windows",
        options: [
          {
            value: "home",
            l10nId: "home-mode-choice-default-fx",
          },
          { value: "blank", l10nId: "home-mode-choice-blank" },
          { value: "custom", l10nId: "home-mode-choice-custom" },
        ],
      },
      {
        id: "homepageGoToCustomHomepageUrlPanel",
        control: "moz-box-button",
        l10nId: "home-homepage-custom-homepage-button",
      },
      {
        id: "homepageNewTabs",
        control: "moz-select",
        l10nId: "home-homepage-new-tabs",
        options: [
          {
            value: "true",
            l10nId: "home-mode-choice-default-fx",
          },
          { value: "false", l10nId: "home-mode-choice-blank" },
        ],
      },
      {
        id: "homepageRestoreDefaults",
        control: "moz-button",
        iconSrc: "chrome://global/skin/icons/arrow-counterclockwise-16.svg",
        l10nId: "home-restore-defaults",
        controlAttrs: { id: "restoreDefaultHomePageBtn" },
      },
    ],
  },
  customHomepage: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "home-custom-homepage-card-header",
    iconSrc: "chrome://global/skin/icons/link.svg",
    items: [
      {
        id: "customHomepageBoxGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
      },
    ],
  },
  home: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "home-prefs-content-header",
    iconSrc: "chrome://browser/skin/home.svg",
    items: [
      {
        id: "webSearch",
        l10nId: "home-prefs-search-header2",
        control: "moz-toggle",
      },
      {
        id: "weather",
        l10nId: "home-prefs-weather-header",
        control: "moz-toggle",
      },
      {
        id: "widgets",
        l10nId: "home-prefs-widgets-header",
        control: "moz-toggle",
        items: [
          {
            id: "lists",
            l10nId: "home-prefs-lists-header",
          },
          {
            id: "timer",
            l10nId: "home-prefs-timer-header",
          },
        ],
      },
      {
        id: "shortcuts",
        l10nId: "home-prefs-shortcuts-header",
        control: "moz-toggle",
        items: [
          {
            id: "shortcutsRows",
            control: "moz-select",
            options: [
              {
                value: 1,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 1 },
              },
              {
                value: 2,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 2 },
              },
              {
                value: 3,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 3 },
              },
              {
                value: 4,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 4 },
              },
            ],
          },
        ],
      },
      {
        id: "stories",
        l10nId: "home-prefs-stories-header2",
        control: "moz-toggle",
        items: [
          {
            id: "manageTopics",
            l10nId: "home-prefs-manage-topics-link2",
            control: "moz-box-link",
            controlAttrs: {
              href: "about:newtab#customize-topics",
            },
          },
        ],
      },
      {
        id: "supportFirefox",
        l10nId: "home-prefs-support-firefox-header",
        control: "moz-toggle",
        items: [
          {
            id: "sponsoredShortcuts",
            l10nId: "home-prefs-shortcuts-by-option-sponsored",
          },
          {
            id: "sponsoredStories",
            l10nId: "home-prefs-recommended-by-option-sponsored-stories",
          },
          {
            id: "supportFirefoxPromo",
            l10nId: "home-prefs-mission-message2",
            control: "moz-promo",
            options: [
              {
                control: "a",
                l10nId: "home-prefs-mission-message-learn-more-link",
                slot: "support-link",
                controlAttrs: {
                  is: "moz-support-link",
                  "support-page": "sponsor-privacy",
                  "utm-content": "inproduct",
                },
              },
            ],
          },
        ],
      },
      {
        id: "recentActivity",
        l10nId: "home-prefs-recent-activity-header",
        control: "moz-toggle",
        items: [
          {
            id: "recentActivityRows",
            control: "moz-select",
            options: [
              {
                value: 1,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 1 },
              },
              {
                value: 2,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 2 },
              },
              {
                value: 3,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 3 },
              },
              {
                value: 4,
                l10nId: "home-prefs-sections-rows-option",
                l10nArgs: { num: 4 },
              },
            ],
          },
          {
            id: "recentActivityVisited",
            l10nId: "home-prefs-highlights-option-visited-pages",
          },
          {
            id: "recentActivityBookmarks",
            l10nId: "home-prefs-highlights-options-bookmarks",
          },
          {
            id: "recentActivityDownloads",
            l10nId: "home-prefs-highlights-option-most-recent-download",
          },
        ],
      },
      {
        id: "chooseWallpaper",
        l10nId: "home-prefs-choose-wallpaper-link2",
        control: "moz-box-link",
        controlAttrs: {
          href: "about:newtab#customize",
        },
        iconSrc: "chrome://browser/skin/customize.svg",
      },
    ],
  },
  zoom: {
    l10nId: "preferences-zoom-header2",
    headingLevel: 2,
    items: [
      {
        id: "defaultZoom",
        l10nId: "preferences-default-zoom-label",
        control: "moz-select",
      },
      {
        id: "zoomText",
        l10nId: "preferences-zoom-text-only",
      },
      {
        id: "zoomWarning",
        l10nId: "preferences-text-zoom-override-warning",
        control: "moz-message-bar",
        controlAttrs: {
          type: "warning",
        },
      },
    ],
  },
  fonts: {
    l10nId: "preferences-fonts-header2",
    headingLevel: 2,
    items: [
      {
        id: "defaultFont",
        l10nId: "default-font-2",
        control: "moz-select",
      },
      {
        id: "defaultFontSize",
        l10nId: "default-font-size-2",
        control: "moz-select",
      },
      {
        id: "advancedFonts",
        l10nId: "advanced-fonts",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "fonts-window.title,fonts-langgroup-header,fonts-proportional-size,fonts-proportional-header,fonts-serif,fonts-sans-serif,fonts-monospace,fonts-langgroup-arabic.label,fonts-langgroup-armenian.label,fonts-langgroup-bengali.label,fonts-langgroup-simpl-chinese.label,fonts-langgroup-trad-chinese-hk.label,fonts-langgroup-trad-chinese.label,fonts-langgroup-cyrillic.label,fonts-langgroup-devanagari.label,fonts-langgroup-ethiopic.label,fonts-langgroup-georgian.label,fonts-langgroup-el.label,fonts-langgroup-gujarati.label,fonts-langgroup-gurmukhi.label,fonts-langgroup-japanese.label,fonts-langgroup-hebrew.label,fonts-langgroup-kannada.label,fonts-langgroup-khmer.label,fonts-langgroup-korean.label,fonts-langgroup-latin.label,fonts-langgroup-malayalam.label,fonts-langgroup-math.label,fonts-langgroup-odia.label,fonts-langgroup-sinhala.label,fonts-langgroup-tamil.label,fonts-langgroup-telugu.label,fonts-langgroup-thai.label,fonts-langgroup-tibetan.label,fonts-langgroup-canadian.label,fonts-langgroup-other.label,fonts-minsize,fonts-minsize-none.label,fonts-default-serif.label,fonts-default-sans-serif.label,fonts-allow-own.label",
        },
      },
    ],
  },
  translations: {
    inProgress: true,
    l10nId: "settings-translations-header",
    iconSrc: "chrome://browser/skin/translations.svg",
    supportPage: "website-translation",
    headingLevel: 2,
    items: [
      {
        id: "offerTranslations",
        l10nId: "settings-translations-offer-to-translate-label",
      },
      {
        id: "translationsManageButton",
        l10nId: "settings-translations-more-settings-button",
        control: "moz-box-button",
      },
    ],
  },
  spellCheck: {
    l10nId: "settings-spellcheck-header",
    iconSrc: "chrome://global/skin/icons/check.svg",
    headingLevel: 2,
    items: [
      {
        id: "checkSpelling",
        l10nId: "check-user-spelling",
        supportPage: "how-do-i-use-firefox-spell-checker",
      },
      {
        id: "downloadDictionaries",
        l10nId: "spellcheck-download-dictionaries",
        control: "moz-box-link",
        controlAttrs: {
          href: Services.urlFormatter.formatURLPref(
            "browser.dictionaries.download.url"
          ),
        },
      },
      {
        id: "spellCheckPromo",
        l10nId: "spellcheck-promo",
        control: "moz-promo",
        controlAttrs: {
          imagesrc:
            "chrome://browser/content/preferences/spell-check-promo.svg",
        },
      },
    ],
  },
  browserLayout: {
    l10nId: "browser-layout-header2",
    headingLevel: 2,
    items: [
      {
        id: "browserLayoutRadioGroup",
        control: "moz-visual-picker",
        options: [
          {
            id: "browserLayoutHorizontalTabs",
            value: "false",
            l10nId: "browser-layout-horizontal-tabs2",
            controlAttrs: {
              class: "setting-chooser-item",
              imagesrc:
                "chrome://browser/content/preferences/browser-layout-horizontal.svg",
            },
          },
          {
            id: "browserLayoutVerticalTabs",
            value: "true",
            l10nId: "browser-layout-vertical-tabs2",
            controlAttrs: {
              class: "setting-chooser-item",
              imagesrc:
                "chrome://browser/content/preferences/browser-layout-vertical.svg",
            },
          },
        ],
      },
      {
        id: "browserLayoutShowSidebar",
        l10nId: "browser-layout-show-sidebar2",
      },
    ],
  },
  appearance: {
    l10nId: "appearance-group",
    headingLevel: 2,
    items: [
      {
        id: "web-appearance-override-warning",
        l10nId: "preferences-web-appearance-override-warning3",
        control: "moz-message-bar",
      },
      {
        id: "web-appearance-chooser",
        control: "moz-visual-picker",
        options: [
          {
            value: "auto",
            l10nId: "preferences-web-appearance-choice-auto2",
            controlAttrs: {
              id: "preferences-web-appearance-choice-auto",
              class: "setting-chooser-item",
              imagesrc:
                "chrome://browser/content/preferences/web-appearance-light.svg",
            },
          },
          {
            value: "light",
            l10nId: "preferences-web-appearance-choice-light2",
            controlAttrs: {
              id: "preferences-web-appearance-choice-light",
              class: "setting-chooser-item",
              imagesrc:
                "chrome://browser/content/preferences/web-appearance-light.svg",
            },
          },
          {
            value: "dark",
            l10nId: "preferences-web-appearance-choice-dark2",
            controlAttrs: {
              id: "preferences-web-appearance-choice-dark",
              class: "setting-chooser-item",
              imagesrc:
                "chrome://browser/content/preferences/web-appearance-dark.svg",
            },
          },
        ],
      },
      {
        id: "web-appearance-manage-themes-link",
        l10nId: "preferences-web-appearance-link",
        control: "moz-box-link",
        controlAttrs: {
          href: "about:addons",
        },
      },
    ],
  },
  websiteLanguage: {
    inProgress: true,
    l10nId: "website-language-heading",
    headingLevel: 2,
    items: [
      {
        id: "websiteLanguageWrapper",
        control: "moz-box-group",
        controlAttrs: {
          type: "reorderable-list",
        },
        options: [
          {
            id: "websiteLanguagePickerWrapper",
            l10nId: "website-preferred-language",
            key: "addlanguage",
            control: "moz-box-item",
            slot: "header",
            items: [
              {
                id: "websiteLanguagePicker",
                slot: "actions",
                control: "moz-select",
                options: [
                  {
                    control: "moz-option",
                    l10nId: "website-add-language",
                    controlAttrs: {
                      value: "-1",
                    },
                  },
                ],
              },
              {
                id: "websiteLanguageAddLanguage",
                slot: "actions",
                control: "moz-button",
                iconSrc: "chrome://global/skin/icons/plus.svg",
                l10nId: "website-add-language-button",
              },
            ],
          },
        ],
      },
    ],
  },
  downloads: {
    l10nId: "downloads-header-2",
    headingLevel: 2,
    items: [
      {
        id: "downloadFolder",
        l10nId: "download-save-where-2",
        control: "moz-input-folder",
        controlAttrs: {
          id: "chooseFolder",
        },
      },
      {
        id: "alwaysAsk",
        l10nId: "download-always-ask-where",
      },
      {
        id: "deletePrivate",
        l10nId: "download-private-browsing-delete",
      },
    ],
  },
  drm: {
    l10nId: "drm-group",
    headingLevel: 2,
    subcategory: "drm",
    items: [
      {
        id: "playDRMContent",
        l10nId: "play-drm-content",
        supportPage: "drm-content",
      },
    ],
  },
  contrast: {
    l10nId: "preferences-contrast-control-group",
    headingLevel: 2,
    items: [
      {
        id: "contrastControlSettings",
        control: "moz-radio-group",
        l10nId: "preferences-contrast-control-radio-group",
        options: [
          {
            id: "contrastSettingsAuto",
            value: 0,
            l10nId: "preferences-contrast-control-use-platform-settings",
          },
          {
            id: "contrastSettingsOff",
            value: 1,
            l10nId: "preferences-contrast-control-off",
          },
          {
            id: "contrastSettingsOn",
            value: 2,
            l10nId: "preferences-contrast-control-custom",
            items: [
              {
                id: "colors",
                l10nId: "preferences-colors-manage-button",
                control: "moz-box-button",
                controlAttrs: {
                  "search-l10n-ids":
                    "colors-text-and-background, colors-text.label, colors-text-background.label, colors-links-header, colors-links-unvisited.label, colors-links-visited.label",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  browsing: {
    l10nId: "browsing-group",
    headingLevel: 1,
    items: [
      {
        id: "useAutoScroll",
        l10nId: "browsing-use-autoscroll",
      },
      {
        id: "useSmoothScrolling",
        l10nId: "browsing-use-smooth-scrolling",
      },
      {
        id: "useOverlayScrollbars",
        l10nId: "browsing-gtk-use-non-overlay-scrollbars",
      },
      {
        id: "useOnScreenKeyboard",
        l10nId: "browsing-use-onscreen-keyboard",
      },
      {
        id: "useCursorNavigation",
        l10nId: "browsing-use-cursor-navigation",
      },
      {
        id: "useFullKeyboardNavigation",
        l10nId: "browsing-use-full-keyboard-navigation",
      },
      {
        id: "alwaysUnderlineLinks",
        l10nId: "browsing-always-underline-links",
      },
      {
        id: "searchStartTyping",
        l10nId: "browsing-search-on-start-typing",
      },
      {
        id: "pictureInPictureToggleEnabled",
        l10nId: "browsing-picture-in-picture-toggle-enabled",
        supportPage: "picture-in-picture",
        items: [
          {
            id: "pictureInPictureEnableWhenSwitchingTabs",
            l10nId: "browsing-picture-in-picture-enable-when-switching-tabs",
          },
        ],
      },
      {
        id: "mediaControlToggleEnabled",
        l10nId: "browsing-media-control",
        supportPage: "media-keyboard-control",
      },
      {
        id: "cfrRecommendations",
        l10nId: "browsing-cfr-recommendations",
        supportPage: "extensionrecommendations",
        subcategory: "cfraddons",
      },
      {
        id: "cfrRecommendations-features",
        l10nId: "browsing-cfr-features",
        supportPage: "extensionrecommendations",
        subcategory: "cfrfeatures",
      },
      {
        id: "linkPreviewEnabled",
        l10nId: "link-preview-settings-enable",
        subcategory: "link-preview",
        items: [
          {
            id: "linkPreviewKeyPoints",
            l10nId: "link-preview-settings-key-points",
          },
          {
            id: "linkPreviewLongPress",
            l10nId: "link-preview-settings-long-press",
          },
        ],
      },
    ],
  },
  httpsOnly: {
    l10nId: "httpsonly-group",
    supportPage: "https-only-prefs",
    headingLevel: 2,
    items: [
      {
        id: "httpsOnlyRadioGroup",
        control: "moz-radio-group",
        l10nId: "httpsonly-label2",
        options: [
          {
            id: "httpsOnlyRadioEnabled",
            value: "enabled",
            l10nId: "httpsonly-radio-enabled",
          },
          {
            id: "httpsOnlyRadioEnabledPBM",
            value: "privateOnly",
            l10nId: "httpsonly-radio-enabled-pbm",
          },
          {
            id: "httpsOnlyRadioDisabled",
            value: "disabled",
            l10nId: "httpsonly-radio-disabled3",
            supportPage: "connection-upgrades",
          },
        ],
      },
      {
        id: "httpsOnlyExceptionButton",
        l10nId: "sitedata-cookies-exceptions",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "permissions-address,permissions-allow.label,permissions-remove.label,permissions-remove-all.label,permissions-exceptions-https-only-desc2",
        },
      },
    ],
  },
  certificates: {
    l10nId: "certs-description3",
    supportPage: "secure-website-certificate",
    headingLevel: 2,
    items: [
      {
        id: "certEnableThirdPartyToggle",
        l10nId: "certs-thirdparty-toggle",
        supportPage: "automatically-trust-third-party-certificates",
      },
      {
        id: "certificateButtonGroup",
        control: "moz-box-group",
        items: [
          {
            id: "viewCertificatesButton",
            l10nId: "certs-view2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "certmgr-tab-mine.label,certmgr-tab-people.label,certmgr-tab-servers.label,certmgr-tab-ca.label,certmgr-mine,certmgr-people,certmgr-server,certmgr-ca,certmgr-cert-name.label,certmgr-token-name.label,certmgr-view.label,certmgr-export.label,certmgr-delete.label",
            },
          },
          {
            id: "viewSecurityDevicesButton",
            l10nId: "certs-devices2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "devmgr-window.title,devmgr-devlist.label,devmgr-header-details.label,devmgr-header-value.label,devmgr-button-login.label,devmgr-button-logout.label,devmgr-button-changepw.label,devmgr-button-load.label,devmgr-button-unload.label,certs-devices-enable-fips",
            },
          },
        ],
      },
    ],
  },
  browsingProtection: {
    l10nId: "browsing-protection-group2",
    headingLevel: 2,
    items: [
      {
        id: "enableSafeBrowsing",
        l10nId: "security-enable-safe-browsing",
        supportPage: "phishing-malware",
        control: "moz-checkbox",
        items: [
          {
            id: "blockDownloads",
            l10nId: "security-block-downloads",
          },
          {
            id: "blockUncommonUnwanted",
            l10nId: "security-block-uncommon-software",
          },
        ],
      },
      {
        id: "safeBrowsingWarningMessageBox",
        l10nId: "security-safe-browsing-warning",
        control: "moz-message-bar",
        controlAttrs: {
          type: "warning",
          dismissable: true,
        },
      },
    ],
  },
  nonTechnicalPrivacy: {
    l10nId: "non-technical-privacy-group",
    headingLevel: 2,
    items: [
      {
        id: "gpcEnabled",
        l10nId: "global-privacy-control-description",
        supportPage: "global-privacy-control",
        controlAttrs: {
          "search-l10n-ids": "global-privacy-control-search",
        },
      },
      {
        id: "dntRemoval",
        l10nId: "do-not-track-removal2",
        control: "moz-box-link",
        supportPage: "how-do-i-turn-do-not-track-feature",
      },
    ],
  },
  nonTechnicalPrivacy2: {
    inProgress: true,
    l10nId: "non-technical-privacy-heading",
    iconSrc: "chrome://browser/skin/controlcenter/tracking-protection.svg",
    headingLevel: 2,
    items: [
      {
        id: "gpcEnabled",
        l10nId: "global-privacy-control-description",
        supportPage: "global-privacy-control",
        controlAttrs: {
          "search-l10n-ids": "global-privacy-control-search",
        },
      },
      {
        id: "relayIntegration",
        l10nId: "preferences-privacy-relay-available",
        supportPage: "firefox-relay-integration",
      },
      {
        id: "dntRemoval",
        l10nId: "do-not-track-removal3",
        control: "moz-message-bar",
        supportPage: "how-do-i-turn-do-not-track-feature",
        controlAttrs: {
          dismissable: true,
        },
      },
    ],
  },
  securityPrivacyStatus: {
    inProgress: true,
    card: "never",
    items: [
      {
        id: "privacyCard",
        control: "security-privacy-card",
      },
    ],
  },
  securityPrivacyWarnings: {
    inProgress: true,
    card: "never",
    items: [
      {
        id: "warningCard",
        l10nId: "security-privacy-issue-card",
        control: "moz-card",
        controlAttrs: {
          type: "accordion",
        },
        items: [
          {
            id: "securityWarningsGroup",
            control: "moz-box-group",
            controlAttrs: {
              type: "list",
            },
          },
        ],
      },
    ],
  },
  support: {
    inProgress: true,
    l10nId: "support-application-heading",
    headingLevel: 2,
    items: [
      {
        id: "supportLinksGroup",
        control: "moz-box-group",
        items: [
          {
            id: "supportGetHelp",
            l10nId: "support-get-help",
            control: "moz-box-link",
            supportPage: "preferences",
          },
          {
            id: "supportShareIdeas",
            l10nId: "support-share-ideas",
            control: "moz-box-link",
            controlAttrs: {
              href: "https://connect.mozilla.org/",
            },
          },
        ],
      },
    ],
  },
  performance: {
    l10nId: "performance-group",
    headingLevel: 1,
    items: [
      {
        id: "useRecommendedPerformanceSettings",
        l10nId: "performance-use-recommended-settings-checkbox",
        supportPage: "performance",
      },
      {
        id: "allowHWAccel",
        l10nId: "performance-allow-hw-accel",
      },
    ],
  },
  ipprotection: {
    l10nId: "ip-protection-description",
    headingLevel: 2,
    supportPage: "built-in-vpn",
    items: [
      {
        id: "ipProtectionNotOptedInSection",
        l10nId: "ip-protection-not-opted-in-2",
        l10nArgs: {
          maxUsage: "50",
        },
        control: "moz-promo",
        controlAttrs: {
          imagesrc:
            "chrome://browser/content/ipprotection/assets/vpn-settings-get-started.svg",
          imagealignment: "end",
        },
        items: [
          {
            id: "getStartedButton",
            l10nId: "ip-protection-not-opted-in-button",
            control: "moz-button",
            slot: "actions",
            controlAttrs: {
              type: "primary",
            },
          },
        ],
      },
      {
        id: "ipProtectionExceptions",
        control: "moz-fieldset",
        controlAttrs: {
          ".headingLevel": 3,
        },
        items: [
          {
            id: "ipProtectionExceptionAllListButton",
            control: "moz-box-button",
          },
        ],
      },
      {
        id: "ipProtectionAutoStart",
        l10nId: "ip-protection-autostart",
        control: "moz-fieldset",
        items: [
          {
            id: "ipProtectionAutoStartCheckbox",
            l10nId: "ip-protection-autostart-checkbox",
            control: "moz-checkbox",
          },
          {
            id: "ipProtectionAutoStartPrivateCheckbox",
            l10nId: "ip-protection-autostart-private-checkbox",
            control: "moz-checkbox",
          },
        ],
      },
      {
        id: "ipProtectionBandwidthSection",
        control: "moz-box-item",
        items: [{ id: "ipProtectionBandwidth", control: "bandwidth-usage" }],
      },
      {
        id: "ipProtectionLinks",
        control: "moz-box-link",
        l10nId: "ip-protection-vpn-upgrade-link",
        controlAttrs: {
          href: "https://www.mozilla.org/products/vpn/?utm_medium=fx-desktop&utm_campaign=fx-vpn&utm_source=settings",
        },
      },
    ],
  },
  cookiesAndSiteData: {
    l10nId: "cookies-site-data-group",
    headingLevel: 2,
    subcategory: "sitedata",
    items: [
      {
        id: "clearSiteDataButton",
        l10nId: "sitedata-clear2",
        control: "moz-box-button",
        iconSrc: "chrome://browser/skin/flame.svg",
        controlAttrs: {
          "search-l10n-ids": `
            clear-site-data-cookies-empty.label,
            clear-site-data-cache-empty.label
          `,
        },
      },
      {
        id: "deleteOnCloseInfo",
        l10nId: "sitedata-delete-on-close-private-browsing3",
        control: "moz-message-bar",
      },
      {
        id: "manageDataSettingsGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "default",
        },
        items: [
          {
            id: "siteDataSize",
            l10nId: "sitedata-total-size-calculating",
            control: "moz-box-item",
            supportPage: "sitedata-learn-more",
          },
          {
            id: "siteDataSettings",
            l10nId: "sitedata-settings2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                site-data-settings-window.title,
                site-data-column-host.label,
                site-data-column-cookies.label,
                site-data-column-storage.label,
                site-data-settings-description,
                site-data-remove-all.label,
              `,
            },
          },
          {
            id: "cookieExceptions",
            l10nId: "sitedata-cookies-exceptions2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                permissions-address,
                permissions-block.label,
                permissions-allow.label,
                permissions-remove.label,
                permissions-remove-all.label,
                permissions-exceptions-cookie-desc
              `,
            },
          },
        ],
      },
      {
        id: "deleteOnClose",
        l10nId: "sitedata-delete-on-close2",
      },
    ],
  },
  cookiesAndSiteData2: {
    inProgress: true,
    l10nId: "sitedata-heading",
    iconSrc: "chrome://browser/skin/controlcenter/3rdpartycookies.svg",
    headingLevel: 2,
    items: [
      {
        id: "siteDataSize",
        l10nId: "sitedata-total-size-calculating",
        control: "moz-box-item",
        supportPage: "sitedata-learn-more",
      },
      {
        id: "manageDataSettingsGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "default",
        },
        items: [
          {
            id: "clearSiteDataButton",
            l10nId: "sitedata-clear2",
            control: "moz-box-button",
            iconSrc: "chrome://browser/skin/flame.svg",
            controlAttrs: {
              "search-l10n-ids": `
                clear-site-data-cookies-empty.label,
                clear-site-data-cache-empty.label
              `,
            },
          },
          {
            id: "siteDataSettings",
            l10nId: "sitedata-settings3",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                site-data-settings-window.title,
                site-data-column-host.label,
                site-data-column-cookies.label,
                site-data-column-storage.label,
                site-data-settings-description,
                site-data-remove-all.label,
              `,
            },
          },
          {
            id: "cookieExceptions",
            l10nId: "sitedata-cookies-exceptions3",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                permissions-address,
                permissions-block.label,
                permissions-allow.label,
                permissions-remove.label,
                permissions-remove-all.label,
                permissions-exceptions-cookie-desc
              `,
            },
          },
        ],
      },
      {
        id: "deleteOnClose",
        l10nId: "sitedata-delete-on-close2",
      },
    ],
  },
  networkProxy: {
    l10nId: "network-proxy-group2",
    iconSrc: "chrome://devtools/skin/images/globe.svg",
    headingLevel: 1,
    supportPage: "prefs-connection-settings",
    subcategory: "netsettings",
    items: [
      {
        id: "connectionSettings",
        l10nId: "network-proxy-connection-settings2",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "connection-window2.title,connection-proxy-option-no.label,connection-proxy-option-auto.label,connection-proxy-option-system.label,connection-proxy-option-wpad.label,connection-proxy-option-manual.label,connection-proxy-http,connection-proxy-https,connection-proxy-http-port,connection-proxy-socks,connection-proxy-socks4,connection-proxy-socks5,connection-proxy-noproxy,connection-proxy-noproxy-desc,connection-proxy-https-sharing.label,connection-proxy-autotype.label,connection-proxy-reload.label,connection-proxy-autologin-checkbox.label,connection-proxy-socks-remote-dns.label",
        },
      },
    ],
  },
  passwords: {
    inProgress: true,
    id: "passwordsGroup",
    l10nId: "forms-passwords-header",
    headingLevel: 2,
    items: [
      {
        id: "savePasswords",
        l10nId: "forms-ask-to-save-passwords",
        items: [
          {
            id: "managePasswordExceptions",
            l10nId: "forms-manage-password-exceptions",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "permissions-address,permissions-exceptions-saved-passwords-window.title,permissions-exceptions-saved-passwords-desc,",
            },
          },
          {
            id: "fillUsernameAndPasswords",
            l10nId: "forms-fill-usernames-and-passwords-2",
            controlAttrs: {
              "search-l10n-ids": "forms-saved-passwords-searchkeywords",
            },
          },
          {
            id: "suggestStrongPasswords",
            l10nId: "forms-suggest-passwords",
            supportPage: "how-generate-secure-password-firefox",
          },
        ],
      },
      {
        id: "requireOSAuthForPasswords",
        l10nId: "forms-os-reauth-2",
      },
      {
        id: "allowWindowSSO",
        l10nId: "forms-windows-sso",
        supportPage: "windows-sso",
      },
      {
        id: "manageSavedPasswords",
        l10nId: "forms-saved-passwords-2",
        control: "moz-box-link",
      },
      {
        id: "additionalProtectionsGroup",
        l10nId: "forms-additional-protections-header",
        control: "moz-fieldset",
        controlAttrs: {
          headingLevel: 2,
        },
        items: [
          {
            id: "primaryPasswordNotSet",
            control: "moz-box-group",
            items: [
              {
                id: "usePrimaryPassword",
                l10nId: "forms-primary-pw-use-2",
                control: "moz-box-item",
                supportPage: "primary-password-stored-logins",
              },
              {
                id: "addPrimaryPassword",
                l10nId: "forms-primary-pw-set",
                control: "moz-box-button",
              },
            ],
          },
          {
            id: "primaryPasswordSet",
            control: "moz-box-group",
            items: [
              {
                id: "statusPrimaryPassword",
                l10nId: "forms-primary-pw-on",
                control: "moz-box-item",
                controlAttrs: {
                  iconsrc: "chrome://global/skin/icons/check-filled.svg",
                },
                options: [
                  {
                    id: "turnOffPrimaryPassword",
                    l10nId: "forms-primary-pw-turn-off",
                    control: "moz-button",
                    slot: "actions",
                  },
                ],
              },
              {
                id: "changePrimaryPassword",
                l10nId: "forms-primary-pw-change-2",
                control: "moz-box-button",
              },
            ],
          },
          {
            id: "breachAlerts",
            l10nId: "forms-breach-alerts",
            supportPage: "lockwise-alerts",
          },
        ],
      },
    ],
  },
  history: {
    l10nId: "history-group",
    headingLevel: 2,
    items: [
      {
        id: "historyMode",
        control: "moz-select",
        options: [
          {
            value: "remember",
            l10nId: "history-remember-option-all",
          },
          { value: "dontremember", l10nId: "history-remember-option-never2" },
          { value: "custom", l10nId: "history-remember-option-custom2" },
        ],
        controlAttrs: {
          "search-l10n-ids": `
            history-remember-description4,
            history-dontremember-description4,
            history-custom-description4,
            history-private-browsing-permanent.label,
            history-remember-browser-option.label,
            history-remember-search-option.label,
            history-clear-on-close-option.label,
            history-clear-on-close-settings.label
          `,
        },
      },
      {
        id: "privateBrowsingAutoStart",
        l10nId: "history-private-browsing-permanent",
      },
      {
        id: "rememberHistory",
        l10nId: "history-remember-browser-option",
      },
      {
        id: "rememberForms",
        l10nId: "history-remember-search-option",
      },
      {
        id: "alwaysClear",
        l10nId: "history-clear-on-close-option",
      },
      {
        id: "clearDataSettings",
        l10nId: "history-clear-on-close-settings",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids": `
            clear-data-settings-label,
            history-section-label,
            item-history-and-downloads.label,
            item-cookies.label,
            item-active-logins.label,
            item-cache.label,
            item-form-search-history.label,
            data-section-label,
            item-site-settings.label,
            item-offline-apps.label
          `,
        },
      },
      {
        id: "clearHistoryButton",
        l10nId: "history-clear-button",
        control: "moz-box-button",
      },
    ],
  },
  history2: {
    inProgress: true,
    l10nId: "history-section-header",
    iconSrc: "chrome://browser/skin/controlcenter/3rdpartycookies.svg",
    items: [
      {
        id: "deleteOnCloseInfo",
        l10nId: "sitedata-delete-on-close-private-browsing4",
        control: "moz-message-bar",
      },
      {
        id: "historyMode",
        control: "moz-radio-group",
        options: [
          {
            value: "remember",
            l10nId: "history-remember-option-all",
          },
          { value: "dontremember", l10nId: "history-remember-option-never2" },
          {
            value: "custom",
            l10nId: "history-remember-option-custom2",
            items: [
              {
                id: "customHistoryButton",
                control: "moz-box-button",
                l10nId: "history-custom-button",
              },
            ],
          },
        ],
        controlAttrs: {
          "search-l10n-ids": `
            history-remember-description3,
            history-dontremember-description3,
            history-private-browsing-permanent.label,
            history-remember-browser-option.label,
            history-remember-search-option.label,
            history-clear-on-close-option.label,
            history-clear-on-close-settings.label
          `,
        },
      },
    ],
  },
  historyAdvanced: {
    l10nId: "history-custom-section-header",
    headingLevel: 2,
    items: [
      {
        id: "privateBrowsingAutoStart",
        l10nId: "history-private-browsing-permanent",
      },
      {
        id: "rememberHistory",
        l10nId: "history-remember-browser-option",
      },
      {
        id: "rememberForms",
        l10nId: "history-remember-search-option",
      },
      {
        id: "alwaysClear",
        l10nId: "history-clear-on-close-option",
        items: [
          {
            id: "clearDataSettings",
            l10nId: "history-clear-on-close-settings",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                    clear-data-settings-label,
                    history-section-label,
                    item-history-and-downloads.label,
                    item-cookies.label,
                    item-active-logins.label,
                    item-cache.label,
                    item-form-search-history.label,
                    data-section-label,
                    item-site-settings.label,
                    item-offline-apps.label
                  `,
            },
          },
        ],
      },
    ],
  },
  permissions: {
    id: "permissions",
    l10nId: "permissions-header3",
    headingLevel: 2,
    items: [
      {
        id: "permissionBox",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
        items: [
          {
            id: "locationSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-location2",
            controlAttrs: {
              ".iconSrc": "chrome://browser/skin/notification-icons/geo.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-location-window2.title,permissions-site-location-desc,permissions-site-location-disable-label,permissions-site-location-disable-desc",
            },
          },
          {
            id: "cameraSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-camera2",
            controlAttrs: {
              ".iconSrc": "chrome://browser/skin/notification-icons/camera.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-camera-window2.title,permissions-site-camera-desc,permissions-site-camera-disable-label,permissions-site-camera-disable-desc,",
            },
          },
          {
            id: "localHostSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-localhost2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/local-host.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-localhost-window.title,permissions-site-localhost-desc,permissions-site-localhost-disable-label,permissions-site-localhost-disable-desc,",
            },
          },
          {
            id: "localNetworkSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-local-network2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/local-network.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-local-network-window.title,permissions-site-local-network-desc,permissions-site-local-network-disable-label,permissions-site-local-network-disable-desc,",
            },
          },
          {
            id: "microphoneSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-microphone2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/microphone.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-microphone-window2.title,permissions-site-microphone-desc,permissions-site-microphone-disable-label,permissions-site-microphone-disable-desc,",
            },
          },
          {
            id: "speakerSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-speaker2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/speaker.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-speaker-window.title,permissions-site-speaker-desc,",
            },
          },
          {
            id: "notificationSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-notification2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/desktop-notification.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-notification-window2.title,permissions-site-notification-desc,permissions-site-notification-disable-label,permissions-site-notification-disable-desc,",
            },
          },
          {
            id: "autoplaySettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-autoplay2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/autoplay-media.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-autoplay-window2.title,permissions-site-autoplay-desc,",
            },
          },
          {
            id: "xrSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-xr2",
            controlAttrs: {
              ".iconSrc": "chrome://browser/skin/notification-icons/xr.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-xr-window2.title,permissions-site-xr-desc,permissions-site-xr-disable-label,permissions-site-xr-disable-desc,",
            },
          },
        ],
      },
      {
        id: "popupPolicy",
        l10nId: "permissions-block-popups2",
        subcategory: "permissions-block-popups",
        items: [
          {
            id: "popupPolicyButton",
            l10nId: "permissions-block-popups-exceptions-button2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "permissions-address,permissions-exceptions-popup-window3.title,permissions-exceptions-popup-desc2",
            },
          },
        ],
      },
      {
        id: "warnAddonInstall",
        l10nId: "permissions-addon-install-warning3",
        items: [
          {
            id: "addonExceptions",
            l10nId: "permissions-addon-exceptions2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "permissions-address,permissions-allow.label,permissions-remove.label,permissions-remove-all.label,permissions-exceptions-addons-window2.title,permissions-exceptions-addons-desc",
            },
          },
        ],
      },
      {
        id: "notificationsDoNotDisturb",
        l10nId: "permissions-notification-pause",
      },
    ],
  },
  dnsOverHttps: {
    l10nId: "dns-over-https-group2",
    headingLevel: 1,
    inProgress: true,
    items: [
      {
        id: "dohBox",
        control: "moz-box-group",
        items: [
          {
            id: "dohModeBoxItem",
            control: "moz-box-item",
          },
          {
            id: "dohAdvancedButton",
            l10nId: "preferences-doh-advanced-button",
            control: "moz-box-button",
          },
        ],
      },
    ],
  },
  defaultEngine: {
    l10nId: "search-engine-group",
    headingLevel: 2,
    items: [
      {
        id: "defaultEngineNormal",
        l10nId: "search-default-engine",
        control: "moz-select",
      },
      {
        id: "searchShowSearchTermCheckbox",
        l10nId: "search-show-search-term-option-2",
      },
      {
        id: "browserSeparateDefaultEngine",
        l10nId: "search-separate-default-engine-2",
        items: [
          {
            id: "defaultPrivateEngine",
            l10nId: "search-separate-default-engine-dropdown",
            control: "moz-select",
          },
        ],
      },
    ],
  },
  searchSuggestions: {
    l10nId: "search-suggestions-header-2",
    headingLevel: 2,
    items: [
      {
        id: "suggestionsInSearchFieldsCheckbox",
        l10nId: "search-show-suggestions-option",
        items: [
          {
            id: "urlBarSuggestionCheckbox",
            l10nId: "search-show-suggestions-url-bar-option",
          },
          {
            id: "showSearchSuggestionsFirstCheckbox",
            l10nId: "search-show-suggestions-above-history-option-2",
          },
          {
            id: "showSearchSuggestionsPrivateWindowsCheckbox",
            l10nId: "search-show-suggestions-private-windows-2",
          },
          {
            id: "showTrendingSuggestionsCheckbox",
            l10nId: "addressbar-locbar-showtrendingsuggestions-option-2",
            supportPage: "google-trending-searches-on-awesomebar",
          },
          {
            id: "urlBarSuggestionPermanentPBMessage",
            l10nId: "search-suggestions-cant-show-2",
            control: "moz-message-bar",
          },
        ],
      },
    ],
  },
  searchShortcuts: {
    inProgress: true,
    l10nId: "search-one-click-header-3",
    headingLevel: 2,
    items: [
      {
        id: "updateSearchEngineSuccess",
        l10nId: "update-search-engine-success",
        control: "moz-message-bar",
        controlAttrs: {
          type: "success",
          dismissable: true,
        },
      },
      {
        id: "addEngineButton",
        l10nId: "search-add-engine-2",
        control: "moz-button",
        iconSrc: "chrome://global/skin/icons/plus.svg",
      },
      {
        id: "engineList",
        control: "moz-box-group",
        controlAttrs: {
          type: "reorderable-list",
        },
      },
    ],
  },
  dnsOverHttpsAdvanced: {
    inProgress: true,
    l10nId: "preferences-doh-advanced-section",
    supportPage: "dns-over-https",
    headingLevel: 2,
    items: [
      {
        id: "dohStatusBox",
        control: "moz-message-bar",
      },
      {
        id: "dohRadioGroup",
        control: "moz-radio-group",
        options: [
          {
            id: "dohRadioDefault",
            value: "default",
            l10nId: "preferences-doh-radio-default",
          },
          {
            id: "dohRadioCustom",
            value: "custom",
            l10nId: "preferences-doh-radio-custom",
            items: [
              {
                id: "dohFallbackIfCustom",
                l10nId: "preferences-doh-fallback-label",
              },
              {
                id: "dohProviderSelect",
                l10nId: "preferences-doh-select-resolver-label",
                control: "moz-select",
              },
              {
                id: "dohCustomProvider",
                control: "moz-input-text",
                l10nId: "preferences-doh-custom-provider-label",
              },
            ],
          },
          {
            id: "dohRadioOff",
            value: "off",
            l10nId: "preferences-doh-radio-off",
          },
        ],
      },
      {
        id: "dohExceptionsButton",
        l10nId: "preferences-doh-manage-exceptions2",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "permissions-doh-entry-field,permissions-doh-add-exception.label,permissions-doh-remove.label,permissions-doh-remove-all.label,permissions-exceptions-doh-window.title,permissions-exceptions-manage-doh-desc,",
        },
      },
    ],
  },
  managePayments: {
    items: [
      {
        id: "add-payment-button",
        control: "moz-button",
        l10nId: "autofill-payment-methods-add-button",
      },
      {
        id: "payments-list",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
      },
    ],
  },
  tabs: {
    l10nId: "tabs-group-header2",
    headingLevel: 2,
    items: [
      {
        id: "tabsOpening",
        control: "moz-fieldset",
        l10nId: "tabs-opening-heading",
        headingLevel: 3,
        items: [
          {
            id: "linkTargeting",
            l10nId: "open-new-link-as-tabs",
          },
          {
            id: "switchToNewTabs",
            l10nId: "switch-to-new-tabs",
          },
          {
            id: "openAppLinksNextToActiveTab",
            l10nId: "open-external-link-next-to-active-tab",
          },
          {
            id: "warnOpenMany",
            l10nId: "warn-on-open-many-tabs",
          },
        ],
      },
      {
        id: "tabsInteraction",
        control: "moz-fieldset",
        l10nId: "tabs-interaction-heading",
        headingLevel: 3,
        items: [
          {
            id: "ctrlTabRecentlyUsedOrder",
            l10nId: "ctrl-tab-recently-used-order",
          },
          {
            id: "tabPreviewShowThumbnails",
            l10nId: "settings-tabs-show-image-in-preview",
          },
          {
            id: "tabGroupSuggestions",
            l10nId: "settings-tabs-show-group-and-tab-suggestions",
          },
          {
            id: "showTabsInTaskbar",
            l10nId: "show-tabs-in-taskbar",
          },
        ],
      },
      {
        id: "browserContainersbox",
        control: "moz-fieldset",
        l10nId: "tabs-containers-heading",
        headingLevel: 3,
        items: [
          {
            id: "browserContainersCheckbox",
            l10nId: "browser-containers-enabled",
            supportPage: "containers",
          },
          {
            id: "browserContainersSettings",
            l10nId: "browser-containers-settings",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "containers-add-button.label, containers-settings-button.label, containers-remove-button.label, containers-new-tab-check.label",
            },
          },
        ],
      },
      {
        id: "tabsClosing",
        control: "moz-fieldset",
        l10nId: "tabs-closing-heading",
        headingLevel: 3,
        items: [
          {
            id: "warnCloseMultiple",
            l10nId: "ask-on-close-multiple-tabs",
          },
          {
            id: "warnOnQuitKey",
            l10nId: "ask-on-quit-with-key",
          },
        ],
      },
    ],
  },
  etpStatus: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-status-header",
    supportPage: "enhanced-tracking-protection",
    iconSrc: "chrome://browser/skin/controlcenter/tracking-protection.svg",
    items: [
      {
        id: "etpStatusBoxGroup",
        control: "moz-box-group",
        items: [
          {
            id: "etpStatusItem",
            l10nId: "preferences-etp-level-standard",
            control: "moz-box-item",
          },
          {
            id: "etpStatusAdvancedButton",
            l10nId: "preferences-etp-status-advanced-button",
            control: "moz-box-button",
          },
        ],
      },
      {
        id: "protectionsDashboardLink",
        l10nId: "preferences-etp-status-protections-dashboard-link",
        control: "moz-box-link",
        controlAttrs: {
          href: "about:protections",
        },
      },
    ],
  },
  etpBanner: {
    inProgress: true,
    card: "never",
    items: [
      {
        id: "etpBannerEl",
        control: "moz-card",
      },
    ],
  },
  etpAdvanced: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-advanced-settings-group",
    supportPage: "enhanced-tracking-protection",
    items: [
      {
        id: "contentBlockingCategoryRadioGroup",
        control: "moz-radio-group",
        options: [
          {
            id: "etpLevelStandard",
            value: "standard",
            l10nId: "preferences-etp-level-standard",
          },
          {
            id: "etpLevelStrict",
            value: "strict",
            l10nId: "preferences-etp-level-strict",
            items: [
              {
                id: "etpAllowListBaselineEnabled",
                l10nId: "content-blocking-baseline-exceptions-3",
                supportPage: "manage-enhanced-tracking-protection-exceptions",
                control: "moz-checkbox",
                items: [
                  {
                    id: "etpAllowListConvenienceEnabled",
                    l10nId: "content-blocking-convenience-exceptions-3",
                    control: "moz-checkbox",
                  },
                ],
              },
            ],
          },
          {
            id: "etpLevelCustom",
            value: "custom",
            l10nId: "preferences-etp-level-custom",
            items: [
              {
                id: "etpCustomizeButton",
                l10nId: "preferences-etp-customize-button",
                control: "moz-box-button",
              },
            ],
          },
        ],
      },
      {
        id: "reloadTabsHint",
        control: "moz-message-bar",
        l10nId: "preferences-etp-reload-tabs-hint",
        options: [
          {
            control: "moz-button",
            l10nId: "preferences-etp-reload-tabs-hint-button",
            slot: "actions",
          },
        ],
      },
      {
        id: "rfpWarning",
        control: "moz-message-bar",
        l10nId: "preferences-etp-rfp-warning-message",
        supportPage: "resist-fingerprinting",
      },
      {
        id: "etpLevelWarning",
        control: "moz-promo",
        l10nId: "preferences-etp-level-warning-message",
        controlAttrs: {
          ".imageAlignment": "end",
          ".imageSrc":
            "chrome://browser/content/preferences/etp-toggle-promo.svg",
        },
      },
      {
        id: "etpManageExceptionsButton",
        l10nId: "preferences-etp-manage-exceptions-button",
        control: "moz-box-button",
      },
    ],
  },
  etpReset: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-reset",
    items: [
      {
        id: "etpResetButtonGroup",
        control: "div",
        items: [
          {
            id: "etpResetStandardButton",
            control: "moz-button",
            l10nId: "preferences-etp-reset-standard-button",
          },
          {
            id: "etpResetStrictButton",
            control: "moz-button",
            l10nId: "preferences-etp-reset-strict-button",
          },
        ],
      },
    ],
  },
  etpCustomize: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-custom-control-group",
    items: [
      {
        id: "etpAllowListBaselineEnabledCustom",
        l10nId: "content-blocking-baseline-exceptions-3",
        supportPage: "manage-enhanced-tracking-protection-exceptions",
        control: "moz-checkbox",
        items: [
          {
            id: "etpAllowListConvenienceEnabledCustom",
            l10nId: "content-blocking-convenience-exceptions-3",
            control: "moz-checkbox",
          },
        ],
      },
      {
        id: "etpCustomCookiesEnabled",
        l10nId: "preferences-etp-custom-cookies-enabled",
        control: "moz-toggle",
        items: [
          {
            id: "cookieBehavior",
            l10nId: "preferences-etp-custom-cookie-behavior",
            control: "moz-select",
            options: [
              {
                value: Ci.nsICookieService.BEHAVIOR_ACCEPT.toString(),
                l10nId: "preferences-etpc-custom-cookie-behavior-accept-all",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER.toString(),
                l10nId: "sitedata-option-block-cross-site-trackers",
              },
              {
                value:
                  Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN.toString(),
                l10nId: "sitedata-option-block-cross-site-cookies2",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN.toString(),
                l10nId: "sitedata-option-block-unvisited",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN.toString(),
                l10nId: "sitedata-option-block-all-cross-site-cookies",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_REJECT.toString(),
                l10nId: "sitedata-option-block-all",
              },
            ],
          },
        ],
      },
      {
        id: "etpCustomTrackingProtectionEnabled",
        l10nId: "preferences-etp-custom-tracking-protection-enabled",
        control: "moz-toggle",
        items: [
          {
            id: "etpCustomTrackingProtectionEnabledContext",
            l10nId:
              "preferences-etp-custom-tracking-protection-enabled-context",
            control: "moz-select",
            options: [
              {
                value: "all",
                l10nId:
                  "content-blocking-tracking-protection-option-all-windows",
              },
              {
                value: "pbmOnly",
                l10nId: "content-blocking-option-private",
              },
            ],
          },
        ],
      },
      {
        id: "etpCustomCryptominingProtectionEnabled",
        l10nId: "preferences-etp-custom-crypto-mining-protection-enabled",
        control: "moz-toggle",
      },
      {
        id: "etpCustomKnownFingerprintingProtectionEnabled",
        l10nId:
          "preferences-etp-custom-known-fingerprinting-protection-enabled",
        control: "moz-toggle",
      },
      {
        id: "etpCustomSuspectFingerprintingProtectionEnabled",
        l10nId:
          "preferences-etp-custom-suspect-fingerprinting-protection-enabled",
        control: "moz-toggle",
        items: [
          {
            id: "etpCustomSuspectFingerprintingProtectionEnabledContext",
            l10nId:
              "preferences-etp-custom-suspect-fingerprinting-protection-enabled-context",
            control: "moz-select",
            options: [
              {
                value: "all",
                l10nId:
                  "content-blocking-tracking-protection-option-all-windows",
              },
              {
                value: "pbmOnly",
                l10nId: "content-blocking-option-private",
              },
            ],
          },
        ],
      },
    ],
  },
  manageAddresses: {
    items: [
      {
        id: "add-address-button",
        control: "moz-button",
        l10nId: "autofill-addresses-add-button",
      },
      {
        id: "addresses-list",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
      },
    ],
  },
  defaultBrowserSync: createDefaultBrowserConfig({
    includeIsDefaultPane: false,
    inProgress: true,
  }),
  sync: {
    inProgress: true,
    l10nId: "sync-group-label",
    headingLevel: 2,
    items: [
      {
        id: "syncNoFxaSignIn",
        l10nId: "sync-signedout-account-signin-4",
        control: "moz-box-link",
        iconSrc: "chrome://global/skin/icons/warning.svg",
        controlAttrs: {
          id: "noFxaSignIn",
        },
      },
      {
        id: "syncConfigured",
        control: "moz-box-group",
        items: [
          {
            id: "syncStatus",
            l10nId: "prefs-syncing-on-2",
            control: "moz-box-item",
            iconSrc: "chrome://global/skin/icons/check-filled.svg",
            items: [
              {
                id: "syncNow",
                control: "moz-button",
                l10nId: "prefs-sync-now-button-2",
                slot: "actions",
              },
              {
                id: "syncing",
                control: "moz-button",
                l10nId: "prefs-syncing-button-2",
                slot: "actions",
              },
            ],
          },
          {
            id: "syncEnginesList",
            control: "sync-engines-list",
          },
          {
            id: "syncChangeOptions",
            control: "moz-box-button",
            l10nId: "sync-manage-options-2",
          },
        ],
      },
      {
        id: "syncNotConfigured",
        l10nId: "prefs-syncing-off-2",
        control: "moz-box-item",
        iconSrc: "chrome://global/skin/icons/warning.svg",
        items: [
          {
            id: "syncSetup",
            control: "moz-button",
            l10nId: "prefs-sync-turn-on-syncing-2",
            slot: "actions",
          },
        ],
      },
      {
        id: "fxaDeviceNameSection",
        l10nId: "sync-device-name-header-2",
        control: "moz-fieldset",
        controlAttrs: {
          ".headingLevel": 3,
        },
        items: [
          {
            id: "fxaDeviceNameGroup",
            control: "moz-box-group",
            items: [
              {
                id: "fxaDeviceName",
                control: "sync-device-name",
              },
              {
                id: "fxaConnectAnotherDevice",
                l10nId: "sync-connect-another-device-2",
                control: "moz-box-link",
                iconSrc: "chrome://browser/skin/device-phone.svg",
                controlAttrs: {
                  id: "connect-another-device",
                  href: "https://accounts.firefox.com/pair",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  account: {
    inProgress: true,
    l10nId: "account-group-label",
    headingLevel: 2,
    items: [
      {
        id: "noFxaAccountGroup",
        control: "moz-box-group",
        items: [
          {
            id: "noFxaAccount",
            control: "placeholder-message",
            l10nId: "account-placeholder",
            controlAttrs: {
              imagesrc: "chrome://global/skin/illustrations/security-error.svg",
            },
          },
          {
            id: "noFxaSignIn",
            control: "moz-box-link",
            l10nId: "sync-signedout-account-short",
          },
        ],
      },
      {
        id: "fxaSignedInGroup",
        control: "moz-box-group",
        items: [
          {
            id: "fxaLoginVerified",
            control: "moz-box-item",
            l10nId: "sync-account-signed-in",
            l10nArgs: { email: "" },
            iconSrc: "chrome://browser/skin/fxa/avatar-color.svg",
            controlAttrs: {
              layout: "large-icon",
            },
          },
          {
            id: "verifiedManage",
            control: "moz-box-link",
            l10nId: "sync-manage-account2",
            controlAttrs: {
              href: "https://accounts.firefox.com/settings",
            },
          },
          {
            id: "fxaUnlinkButton",
            control: "moz-box-button",
            l10nId: "sync-sign-out2",
          },
        ],
      },
      {
        id: "fxaUnverifiedGroup",
        control: "moz-box-group",
        items: [
          {
            id: "fxaLoginUnverified",
            control: "placeholder-message",
            l10nId: "sync-signedin-unverified2",
            l10nArgs: { email: "" },
            controlAttrs: {
              imagesrc: "chrome://global/skin/illustrations/security-error.svg",
            },
          },
          {
            id: "verifyFxaAccount",
            control: "moz-box-link",
            l10nId: "sync-verify-account",
          },
          {
            id: "unverifiedUnlinkFxaAccount",
            control: "moz-box-button",
            l10nId: "sync-remove-account",
          },
        ],
      },
      {
        id: "fxaLoginRejectedGroup",
        control: "moz-box-group",
        items: [
          {
            id: "fxaLoginRejected",
            control: "placeholder-message",
            l10nId: "sync-signedin-login-failure2",
            l10nArgs: { email: "" },
            controlAttrs: {
              imagesrc: "chrome://global/skin/illustrations/security-error.svg",
            },
          },
          {
            id: "rejectReSignIn",
            control: "moz-box-link",
            l10nId: "sync-sign-in",
          },
          {
            id: "rejectUnlinkFxaAccount",
            control: "moz-box-button",
            l10nId: "sync-remove-account",
          },
        ],
      },
    ],
  },
  translationsAutomaticTranslation: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "settings-translations-subpage-automatic-translation-header",
    items: [
      {
        id: "translationsAlwaysTranslateLanguagesGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
        items: [
          {
            id: "translationsAlwaysTranslateLanguagesRow",
            l10nId: "settings-translations-subpage-always-translate-header",
            control: "moz-box-item",
            slot: "header",
            controlAttrs: {
              class: "box-header-bold",
            },
            items: [
              {
                id: "translationsAlwaysTranslateLanguagesSelect",
                slot: "actions",
                control: "moz-select",
                options: [
                  {
                    value: "",
                    l10nId:
                      "settings-translations-subpage-language-select-option",
                  },
                ],
              },
              {
                id: "translationsAlwaysTranslateLanguagesButton",
                l10nId: "settings-translations-subpage-language-add-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  type: "icon",
                  iconsrc: "chrome://global/skin/icons/plus.svg",
                },
              },
            ],
          },
          {
            id: "translationsAlwaysTranslateLanguagesNoneRow",
            l10nId: "settings-translations-subpage-no-languages-added",
            control: "moz-box-item",
            controlAttrs: {
              class: "description-deemphasized",
            },
          },
        ],
      },
      {
        id: "translationsNeverTranslateLanguagesGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
        items: [
          {
            id: "translationsNeverTranslateLanguagesRow",
            l10nId: "settings-translations-subpage-never-translate-header",
            control: "moz-box-item",
            slot: "header",
            controlAttrs: {
              class: "box-header-bold",
            },
            items: [
              {
                id: "translationsNeverTranslateLanguagesSelect",
                slot: "actions",
                control: "moz-select",
                options: [
                  {
                    value: "",
                    l10nId:
                      "settings-translations-subpage-language-select-option",
                  },
                ],
              },
              {
                id: "translationsNeverTranslateLanguagesButton",
                l10nId: "settings-translations-subpage-language-add-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  type: "icon",
                  iconsrc: "chrome://global/skin/icons/plus.svg",
                },
              },
            ],
          },
          {
            id: "translationsNeverTranslateLanguagesNoneRow",
            l10nId: "settings-translations-subpage-no-languages-added",
            control: "moz-box-item",
            controlAttrs: {
              class: "description-deemphasized",
            },
          },
        ],
      },
      {
        id: "translationsNeverTranslateSitesGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
        items: [
          {
            id: "translationsNeverTranslateSitesRow",
            l10nId:
              "settings-translations-subpage-never-translate-sites-header",
            control: "moz-box-item",
            controlAttrs: {
              class: "box-header-bold",
              ".description": createNeverTranslateSitesDescription(),
            },
          },
          {
            id: "translationsNeverTranslateSitesNoneRow",
            l10nId: "settings-translations-subpage-no-sites-added",
            control: "moz-box-item",
            controlAttrs: {
              class: "description-deemphasized",
            },
          },
        ],
      },
    ],
  },
  translationsDownloadLanguages: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "settings-translations-subpage-speed-up-translation-header",
    items: [
      {
        id: "translationsDownloadLanguagesGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
        items: [
          {
            id: "translationsDownloadLanguagesRow",
            l10nId: "settings-translations-subpage-download-languages-header",
            control: "moz-box-item",
            slot: "header",
            controlAttrs: {
              class: "box-header-bold",
            },
            items: [
              {
                id: "translationsDownloadLanguagesSelect",
                slot: "actions",
                control: "moz-select",
                options: [
                  {
                    value: "",
                    l10nId:
                      "settings-translations-subpage-download-languages-select-option",
                  },
                ],
              },
              {
                id: "translationsDownloadLanguagesButton",
                l10nId:
                  "settings-translations-subpage-download-languages-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  type: "icon",
                  iconsrc: "chrome://browser/skin/downloads/downloads.svg",
                },
              },
            ],
          },
          {
            id: "translationsDownloadLanguagesNoneRow",
            l10nId: "settings-translations-subpage-no-languages-downloaded",
            control: "moz-box-item",
            controlAttrs: {
              class: "description-deemphasized",
            },
          },
        ],
      },
    ],
  },
  firefoxSuggest: {
    id: "locationBarGroup",
    items: [
      {
        id: "locationBarGroupHeader",
        l10nId: "addressbar-header-1",
        supportPage: "firefox-suggest",
        control: "moz-fieldset",
        controlAttrs: {
          headinglevel: 2,
        },
        items: [
          {
            id: "historySuggestion",
            l10nId: "addressbar-locbar-history-option",
          },
          {
            id: "bookmarkSuggestion",
            l10nId: "addressbar-locbar-bookmarks-option",
          },
          {
            id: "clipboardSuggestion",
            l10nId: "addressbar-locbar-clipboard-option",
          },
          {
            id: "openpageSuggestion",
            l10nId: "addressbar-locbar-openpage-option",
          },
          {
            id: "topSitesSuggestion",
            l10nId: "addressbar-locbar-shortcuts-option",
          },
          {
            id: "enableRecentSearches",
            l10nId: "addressbar-locbar-showrecentsearches-option-2",
          },
          {
            id: "enginesSuggestion",
            l10nId: "addressbar-locbar-engines-option-1",
          },
          {
            id: "enableQuickActions",
            l10nId: "addressbar-locbar-quickactions-option",
            supportPage: "quick-actions-firefox-search-bar",
          },
          {
            id: "firefoxSuggestAll",
            l10nId: "addressbar-locbar-suggest-all-option-2",
            items: [
              {
                id: "firefoxSuggestSponsored",
                l10nId: "addressbar-locbar-suggest-sponsored-option-2",
              },
              {
                id: "firefoxSuggestOnlineEnabledToggle",
                l10nId: "addressbar-firefox-suggest-online",
                supportPage: "firefox-suggest",
                subcategory: "w_what-is-firefox-suggest",
              },
            ],
          },
          {
            id: "dismissedSuggestionsDescription",
            l10nId: "addressbar-dismissed-suggestions-label-2",
            control: "moz-fieldset",
            controlAttrs: {
              headinglevel: 3,
            },
            items: [
              {
                id: "restoreDismissedSuggestions",
                l10nId: "addressbar-restore-dismissed-suggestions-button-2",
                control: "moz-button",
                iconSrc:
                  "chrome://global/skin/icons/arrow-counterclockwise-16.svg",
              },
            ],
          },
        ],
      },
    ],
  },
});

/**
 * @param {string} id - ID of {@link SettingGroup} custom element.
 */
function initSettingGroup(id) {
  /** @type {SettingGroup} */
  let group = document.querySelector(`setting-group[groupid=${id}]`);
  const config = SettingGroupManager.get(id);
  if (group && config) {
    if (config.inProgress && !srdSectionEnabled(id)) {
      group.remove();
      return;
    }

    let legacySections = document.querySelectorAll(`[data-srd-groupid=${id}]`);
    for (let section of legacySections) {
      section.hidden = true;
      section.removeAttribute("data-category");
      section.setAttribute("data-hidden-from-search", "true");
    }
    group.config = config;
    group.getSetting = Preferences.getSetting.bind(Preferences);
    group.srdEnabled = srdSectionPrefs.all;
  }
}

ChromeUtils.defineLazyGetter(this, "gIsPackagedApp", () => {
  return Services.sysinfo.getProperty("isPackagedApp");
});

// A promise that resolves when the list of application handlers is loaded.
// We store this in a global so tests can await it.
var promiseLoadHandlersList;

// Load the preferences string bundle for other locales with fallbacks.
function getBundleForLocales(newLocales) {
  let locales = Array.from(
    new Set([
      ...newLocales,
      ...Services.locale.requestedLocales,
      Services.locale.lastFallbackLocale,
    ])
  );
  return new Localization(
    ["browser/preferences/preferences.ftl", "branding/brand.ftl"],
    false,
    undefined,
    locales
  );
}

var gNodeToObjectMap = new WeakMap();

var gMainPane = {
  // The set of types the app knows how to handle.  A hash of HandlerInfoWrapper
  // objects, indexed by type.
  _handledTypes: {},

  // The list of types we can show, sorted by the sort column/direction.
  // An array of HandlerInfoWrapper objects.  We build this list when we first
  // load the data and then rebuild it when users change a pref that affects
  // what types we can show or change the sort column/direction.
  // Note: this isn't necessarily the list of types we *will* show; if the user
  // provides a filter string, we'll only show the subset of types in this list
  // that match that string.
  _visibleTypes: [],

  // browser.startup.page values
  STARTUP_PREF_BLANK: 0,
  STARTUP_PREF_HOMEPAGE: 1,
  STARTUP_PREF_RESTORE_SESSION: 3,

  // Convenience & Performance Shortcuts

  get _list() {
    delete this._list;
    return (this._list = document.getElementById("handlersView"));
  },

  get _filter() {
    delete this._filter;
    return (this._filter = document.getElementById("filter"));
  },

  /**
   * Initialization of gMainPane.
   */
  init() {
    /**
     * @param {string} aId
     * @param {string} aEventType
     * @param {(ev: Event) => void} aCallback
     */
    function setEventListener(aId, aEventType, aCallback) {
      document
        .getElementById(aId)
        .addEventListener(aEventType, aCallback.bind(gMainPane));
    }

    this.displayUseSystemLocale();

    if (Services.prefs.getBoolPref("intl.multilingual.enabled")) {
      gMainPane.initPrimaryBrowserLanguageUI();
    }

    gMainPane.initTranslations();

    // Initialize settings groups from the config object.
    initSettingGroup("browserLayout");
    initSettingGroup("appearance");
    initSettingGroup("downloads");
    initSettingGroup("drm");
    initSettingGroup("contrast");
    initSettingGroup("websiteLanguage");
    initSettingGroup("browsing");
    initSettingGroup("zoom");
    initSettingGroup("fonts");
    initSettingGroup("support");
    initSettingGroup("translations");
    initSettingGroup("spellCheck");
    initSettingGroup("performance");
    initSettingGroup("defaultBrowser");
    initSettingGroup("startup");
    initSettingGroup("importBrowserData");
    initSettingGroup("networkProxy");
    initSettingGroup("tabs");
    initSettingGroup("profiles");
    initSettingGroup("profilePane");

    setEventListener("manageBrowserLanguagesButton", "command", function () {
      gMainPane.showBrowserLanguagesSubDialog({ search: false });
    });
    if (AppConstants.MOZ_UPDATER) {
      // These elements are only compiled in when the updater is enabled
      setEventListener("checkForUpdatesButton", "command", function () {
        gAppUpdater.checkForUpdates();
      });
      setEventListener("downloadAndInstallButton", "command", function () {
        gAppUpdater.startDownload();
      });
      setEventListener("updateButton", "command", function () {
        gAppUpdater.buttonRestartAfterDownload();
      });
      setEventListener("checkForUpdatesButton2", "command", function () {
        gAppUpdater.checkForUpdates();
      });
      setEventListener("checkForUpdatesButton3", "command", function () {
        gAppUpdater.checkForUpdates();
      });
      setEventListener("checkForUpdatesButton4", "command", function () {
        gAppUpdater.checkForUpdates();
      });
    }

    setEventListener("chooseLanguage", "command", gMainPane.showLanguages);
    // TODO (Bug 1817084) Remove this code when we disable the extension
    setEventListener(
      "fxtranslateButton",
      "command",
      gMainPane.showTranslationExceptions
    );

    document
      .getElementById("migrationWizardDialog")
      .addEventListener("MigrationWizard:Close", function (e) {
        e.currentTarget.close();
      });

    // Firefox Translations settings panel
    // TODO (Bug 1817084) Remove this code when we disable the extension
    const fxtranslationsDisabledPrefName = "extensions.translations.disabled";
    if (!Services.prefs.getBoolPref(fxtranslationsDisabledPrefName, true)) {
      let fxtranslationRow = document.getElementById("fxtranslationsBox");
      fxtranslationRow.hidden = false;
    }

    // Initialize the Firefox Updates section.
    let version = AppConstants.MOZ_APP_VERSION_DISPLAY;

    // Include the build ID if this is an "a#" (nightly) build
    if (/a\d+$/.test(version)) {
      let buildID = Services.appinfo.appBuildID;
      let year = buildID.slice(0, 4);
      let month = buildID.slice(4, 6);
      let day = buildID.slice(6, 8);
      version += ` (${year}-${month}-${day})`;
    }

    // Append "(32-bit)" or "(64-bit)" build architecture to the version number:
    let bundle = Services.strings.createBundle(
      "chrome://browser/locale/browser.properties"
    );
    let archResource = Services.appinfo.is64Bit
      ? "aboutDialog.architecture.sixtyFourBit"
      : "aboutDialog.architecture.thirtyTwoBit";
    let arch = bundle.GetStringFromName(archResource);
    version += ` (${arch})`;

    document.l10n.setAttributes(
      document.getElementById("updateAppInfo"),
      "update-application-version",
      { version }
    );

    // Show a release notes link if we have a URL.
    let relNotesLink = document.getElementById("releasenotes");
    let relNotesPrefType = Services.prefs.getPrefType("app.releaseNotesURL");
    if (relNotesPrefType != Services.prefs.PREF_INVALID) {
      let relNotesURL = Services.urlFormatter.formatURLPref(
        "app.releaseNotesURL"
      );
      if (relNotesURL != "about:blank") {
        relNotesLink.href = relNotesURL;
        relNotesLink.hidden = false;
      }
    }

    let defaults = Services.prefs.getDefaultBranch(null);
    let distroId = defaults.getCharPref("distribution.id", "");
    if (distroId) {
      let distroString = distroId;

      let distroVersion = defaults.getCharPref("distribution.version", "");
      if (distroVersion) {
        distroString += " - " + distroVersion;
      }

      let distroIdField = document.getElementById("distributionId");
      distroIdField.value = distroString;
      distroIdField.hidden = false;

      let distroAbout = defaults.getStringPref("distribution.about", "");
      if (distroAbout) {
        let distroField = document.getElementById("distribution");
        distroField.value = distroAbout;
        distroField.hidden = false;
      }
    }

    if (AppConstants.MOZ_UPDATER) {
      gAppUpdater = new appUpdater();
      setEventListener("showUpdateHistory", "command", gMainPane.showUpdates);

      let updateDisabled =
        Services.policies && !Services.policies.isAllowed("appUpdate");

      if (gIsPackagedApp) {
        // When we're running inside an app package, there's no point in
        // displaying any update content here, and it would get confusing if we
        // did, because our updater is not enabled.
        // We can't rely on the hidden attribute for the toplevel elements,
        // because of the pane hiding/showing code interfering.
        document
          .getElementById("updatesCategory")
          .setAttribute("style", "display: none !important");
        document
          .getElementById("updateApp")
          .setAttribute("style", "display: none !important");
      } else if (
        updateDisabled ||
        UpdateUtils.appUpdateAutoSettingIsLocked() ||
        gApplicationUpdateService.manualUpdateOnly
      ) {
        document.getElementById("updateAllowDescription").hidden = true;
        document.getElementById("updateSettingsContainer").hidden = true;
      } else {
        // Start with no option selected since we are still reading the value
        document.getElementById("autoDesktop").removeAttribute("selected");
        document.getElementById("manualDesktop").removeAttribute("selected");
        // Start reading the correct value from the disk
        this.readUpdateAutoPref();
        setEventListener("updateRadioGroup", "command", event => {
          if (event.target.id == "backgroundUpdate") {
            this.writeBackgroundUpdatePref();
          } else {
            this.writeUpdateAutoPref();
          }
        });
        if (this.isBackgroundUpdateUIAvailable()) {
          document.getElementById("backgroundUpdate").hidden = false;
          // Start reading the background update pref's value from the disk.
          this.readBackgroundUpdatePref();
        }
      }

      if (AppConstants.platform == "win") {
        // On Windows, the Application Update setting is an installation-
        // specific preference, not a profile-specific one. Show a warning to
        // inform users of this.
        let updateContainer = document.getElementById(
          "updateSettingsContainer"
        );
        updateContainer.classList.add("updateSettingCrossUserWarningContainer");
        document.getElementById("updateSettingCrossUserWarningDesc").hidden =
          false;
      }
    }

    // Initilize Application section.

    // Observe preferences that influence what we display so we can rebuild
    // the view when they change.
    Services.obs.addObserver(this, AUTO_UPDATE_CHANGED_TOPIC);
    Services.obs.addObserver(this, BACKGROUND_UPDATE_CHANGED_TOPIC);

    setEventListener("filter", "MozInputSearch:search", gMainPane.filter);
    setEventListener("typeColumn", "click", gMainPane.sort);
    setEventListener("actionColumn", "click", gMainPane.sort);

    // Listen for window unload so we can remove our preference observers.
    window.addEventListener("unload", this);

    // Figure out how we should be sorting the list.  We persist sort settings
    // across sessions, so we can't assume the default sort column/direction.
    // XXX should we be using the XUL sort service instead?
    if (document.getElementById("actionColumn").hasAttribute("sortDirection")) {
      this._sortColumn = document.getElementById("actionColumn");
      // The typeColumn element always has a sortDirection attribute,
      // either because it was persisted or because the default value
      // from the xul file was used.  If we are sorting on the other
      // column, we should remove it.
      document.getElementById("typeColumn").removeAttribute("sortDirection");
    } else {
      this._sortColumn = document.getElementById("typeColumn");
    }

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "main-pane-loaded");
    this.setInitialized();
  },

  preInit() {
    promiseLoadHandlersList = new Promise((resolve, reject) => {
      // Load the data and build the list of handlers for applications pane.
      // By doing this after pageshow, we ensure it doesn't delay painting
      // of the preferences page.
      window.addEventListener(
        "pageshow",
        async () => {
          await this.initialized;
          try {
            this._initListEventHandlers();
            this._loadData();
            await this._rebuildVisibleTypes();
            await this._rebuildView();
            await this._sortListView();
            resolve();
          } catch (ex) {
            reject(ex);
          }
        },
        { once: true }
      );
    });
  },

  handleSubcategory(subcategory) {
    if (Services.policies && !Services.policies.isAllowed("profileImport")) {
      return false;
    }
    if (subcategory == "migrate") {
      this.showMigrationWizardDialog();
      return true;
    }

    if (subcategory == "migrate-autoclose") {
      this.showMigrationWizardDialog({ closeTabWhenDone: true });
    }

    return false;
  },

  // CONTAINERS

  /*
   * preferences:
   *
   * privacy.userContext.enabled
   * - true if containers is enabled
   */

  async onGetStarted() {
    if (!AppConstants.MOZ_DEV_EDITION) {
      return;
    }
    const win = Services.wm.getMostRecentWindow("navigator:browser");
    if (!win) {
      return;
    }
    const user = await fxAccounts.getSignedInUser();
    if (user) {
      // We have a user, open Sync preferences in the same tab
      win.openTrustedLinkIn("about:preferences#sync", "current");
      return;
    }
    if (!(await FxAccounts.canConnectAccount())) {
      return;
    }
    let url =
      await FxAccounts.config.promiseConnectAccountURI("dev-edition-setup");
    let accountsTab = win.gBrowser.addWebTab(url);
    win.gBrowser.selectedTab = accountsTab;
  },

  // HOME PAGE
  /*
   * Preferences:
   *
   * browser.startup.page
   * - what page(s) to show when the user starts the application, as an integer:
   *
   *     0: a blank page (DEPRECATED - this can be set via browser.startup.homepage)
   *     1: the home page (as set by the browser.startup.homepage pref)
   *     2: the last page the user visited (DEPRECATED)
   *     3: windows and tabs from the last session (a.k.a. session restore)
   *
   *   The deprecated option is not exposed in UI; however, if the user has it
   *   selected and doesn't change the UI for this preference, the deprecated
   *   option is preserved.
   */

  /**
   * Utility function to enable/disable the button specified by aButtonID based
   * on the value of the Boolean preference specified by aPreferenceID.
   */
  updateButtons(aButtonID, aPreferenceID) {
    var button = document.getElementById(aButtonID);
    var preference = Preferences.get(aPreferenceID);
    button.disabled = !preference.value;
    return undefined;
  },

  /**
   * Initialize the translations view.
   */
  async initTranslations() {
    let legacyTranslationsVisible = Preferences.getSetting(
      "legacyTranslationsVisible"
    );
    /**
     * Which phase a language download is in.
     *
     * @typedef {"downloaded" | "loading" | "uninstalled"} DownloadPhase
     */

    let translationsGroup = document.getElementById("translationsGroup");
    let setTranslationsGroupVisbility = () => {
      // Immediately show the group so that the async load of the component does
      // not cause the layout to jump. The group will be empty initially.
      translationsGroup.hidden = !legacyTranslationsVisible.visible;
      translationsGroup.classList.toggle(
        "setting-hidden",
        translationsGroup.hidden
      );
    };
    setTranslationsGroupVisbility();

    legacyTranslationsVisible.on("change", setTranslationsGroupVisbility);
    window.addEventListener(
      "unload",
      () =>
        legacyTranslationsVisible.off("change", setTranslationsGroupVisbility),
      { once: true }
    );

    class TranslationsState {
      /**
       * The fully initialized state.
       *
       * @param {object} supportedLanguages
       * @param {Array<{ langTag: string, displayName: string}>} languageList
       * @param {Map<string, DownloadPhase>} downloadPhases
       */
      constructor(supportedLanguages, languageList, downloadPhases) {
        this.supportedLanguages = supportedLanguages;
        this.languageList = languageList;
        this.downloadPhases = downloadPhases;
      }

      /**
       * Handles all of the async initialization logic.
       */
      static async create() {
        const supportedLanguages =
          await TranslationsParent.getSupportedLanguages();
        const languageList =
          TranslationsParent.getLanguageList(supportedLanguages);
        const downloadPhases =
          await TranslationsState.createDownloadPhases(languageList);

        if (supportedLanguages.languagePairs.length === 0) {
          throw new Error(
            "The supported languages list was empty. RemoteSettings may not be available at the moment."
          );
        }

        return new TranslationsState(
          supportedLanguages,
          languageList,
          downloadPhases
        );
      }

      /**
       * Determine the download phase of each language file.
       *
       * @param {Array<{ langTag: string, displayName: string}>} languageList
       * @returns {Promise<Map<string, DownloadPhase>>} Map the language tag to whether it is downloaded.
       */
      static async createDownloadPhases(languageList) {
        const downloadPhases = new Map();
        for (const { langTag } of languageList) {
          downloadPhases.set(
            langTag,
            (await TranslationsParent.hasAllFilesForLanguage(langTag))
              ? "downloaded"
              : "uninstalled"
          );
        }
        return downloadPhases;
      }
    }

    class TranslationsView {
      /** @type {Map<string, XULButton>} */
      deleteButtons = new Map();
      /** @type {Map<string, XULButton>} */
      downloadButtons = new Map();

      /**
       * @param {TranslationsState} state
       */
      constructor(state) {
        this.state = state;
        this.elements = {
          settingsButton: document.getElementById(
            "translations-manage-settings-button"
          ),
          installList: document.getElementById(
            "translations-manage-install-list"
          ),
          installAll: document.getElementById(
            "translations-manage-install-all"
          ),
          deleteAll: document.getElementById("translations-manage-delete-all"),
          error: document.getElementById("translations-manage-error"),
        };
        this.setup();
      }

      setup() {
        this.buildLanguageList();

        this.elements.settingsButton.addEventListener(
          "command",
          gMainPane.showTranslationsSettings
        );
        this.elements.installAll.addEventListener(
          "command",
          this.handleInstallAll
        );
        this.elements.deleteAll.addEventListener(
          "command",
          this.handleDeleteAll
        );

        Services.obs.addObserver(this, "intl:app-locales-changed");
      }

      destroy() {
        Services.obs.removeObserver(this, "intl:app-locales-changed");
      }

      handleInstallAll = async () => {
        this.hideError();
        this.disableButtons(true);
        try {
          await TranslationsParent.downloadAllFiles();
          this.markAllDownloadPhases("downloaded");
        } catch (error) {
          TranslationsView.showError(
            "translations-manage-error-download",
            error
          );
          await this.reloadDownloadPhases();
          this.updateAllButtons();
        }
        this.disableButtons(false);
      };

      handleDeleteAll = async () => {
        this.hideError();
        this.disableButtons(true);
        try {
          await TranslationsUtils.deleteAllLanguageFiles();
          this.markAllDownloadPhases("uninstalled");
        } catch (error) {
          TranslationsView.showError("translations-manage-error-remove", error);
          // The download phases are invalidated with the error and must be reloaded.
          await this.reloadDownloadPhases();
          console.error(error);
        }
        this.disableButtons(false);
      };

      /**
       * @param {string} langTag
       * @returns {Function}
       */
      getDownloadButtonHandler(langTag) {
        return async () => {
          this.hideError();
          this.updateDownloadPhase(langTag, "loading");
          try {
            await TranslationsParent.downloadLanguageFiles(langTag);
            this.updateDownloadPhase(langTag, "downloaded");
          } catch (error) {
            TranslationsView.showError(
              "translations-manage-error-download",
              error
            );
            this.updateDownloadPhase(langTag, "uninstalled");
          }
        };
      }

      /**
       * @param {string} langTag
       * @returns {Function}
       */
      getDeleteButtonHandler(langTag) {
        return async () => {
          this.hideError();
          this.updateDownloadPhase(langTag, "loading");
          try {
            await TranslationsParent.deleteLanguageFiles(langTag);
            this.updateDownloadPhase(langTag, "uninstalled");
          } catch (error) {
            TranslationsView.showError(
              "translations-manage-error-remove",
              error
            );
            // The download phases are invalidated with the error and must be reloaded.
            await this.reloadDownloadPhases();
          }
        };
      }

      buildLanguageList() {
        const listFragment = document.createDocumentFragment();

        for (const { langTag, displayName } of this.state.languageList) {
          const hboxRow = document.createXULElement("hbox");
          hboxRow.classList.add("translations-manage-language");
          hboxRow.setAttribute("data-lang-tag", langTag);

          const languageLabel = document.createXULElement("label");
          languageLabel.textContent = displayName; // The display name is already localized.

          const downloadButton = document.createXULElement("button");
          const deleteButton = document.createXULElement("button");

          downloadButton.addEventListener(
            "command",
            this.getDownloadButtonHandler(langTag)
          );
          deleteButton.addEventListener(
            "command",
            this.getDeleteButtonHandler(langTag)
          );

          document.l10n.setAttributes(
            downloadButton,
            "translations-manage-language-download-button"
          );
          document.l10n.setAttributes(
            deleteButton,
            "translations-manage-language-remove-button"
          );

          downloadButton.hidden = true;
          deleteButton.hidden = true;

          this.deleteButtons.set(langTag, deleteButton);
          this.downloadButtons.set(langTag, downloadButton);

          hboxRow.appendChild(languageLabel);
          hboxRow.appendChild(downloadButton);
          hboxRow.appendChild(deleteButton);
          listFragment.appendChild(hboxRow);
        }
        this.updateAllButtons();
        this.elements.installList.appendChild(listFragment);
      }

      /**
       * Update the DownloadPhase for a single langTag.
       *
       * @param {string} langTag
       * @param {DownloadPhase} downloadPhase
       */
      updateDownloadPhase(langTag, downloadPhase) {
        this.state.downloadPhases.set(langTag, downloadPhase);
        this.updateButton(langTag, downloadPhase);
        this.updateHeaderButtons();
      }

      /**
       * Recreates the download map when the state is invalidated.
       */
      async reloadDownloadPhases() {
        this.state.downloadPhases =
          await TranslationsState.createDownloadPhases(this.state.languageList);
        this.updateAllButtons();
      }

      /**
       * Set all the downloads.
       *
       * @param {DownloadPhase} downloadPhase
       */
      markAllDownloadPhases(downloadPhase) {
        const { downloadPhases } = this.state;
        for (const key of downloadPhases.keys()) {
          downloadPhases.set(key, downloadPhase);
        }
        this.updateAllButtons();
      }

      /**
       * If all languages are downloaded, or no languages are downloaded then
       * the visibility of the buttons need to change.
       */
      updateHeaderButtons() {
        let allDownloaded = true;
        let allUninstalled = true;
        for (const downloadPhase of this.state.downloadPhases.values()) {
          if (downloadPhase === "loading") {
            // Don't count loading towards this calculation.
            continue;
          }
          allDownloaded &&= downloadPhase === "downloaded";
          allUninstalled &&= downloadPhase === "uninstalled";
        }

        this.elements.installAll.hidden = allDownloaded;
        this.elements.deleteAll.hidden = allUninstalled;
      }

      /**
       * Update the buttons according to their download state.
       */
      updateAllButtons() {
        this.updateHeaderButtons();
        for (const [langTag, downloadPhase] of this.state.downloadPhases) {
          this.updateButton(langTag, downloadPhase);
        }
      }

      /**
       * @param {string} langTag
       * @param {DownloadPhase} downloadPhase
       */
      updateButton(langTag, downloadPhase) {
        const downloadButton = this.downloadButtons.get(langTag);
        const deleteButton = this.deleteButtons.get(langTag);
        switch (downloadPhase) {
          case "downloaded":
            downloadButton.hidden = true;
            deleteButton.hidden = false;
            downloadButton.removeAttribute("disabled");
            break;
          case "uninstalled":
            downloadButton.hidden = false;
            deleteButton.hidden = true;
            downloadButton.removeAttribute("disabled");
            break;
          case "loading":
            downloadButton.hidden = false;
            deleteButton.hidden = true;
            downloadButton.setAttribute("disabled", "true");
            break;
        }
      }

      /**
       * @param {boolean} isDisabled
       */
      disableButtons(isDisabled) {
        this.elements.installAll.disabled = isDisabled;
        this.elements.deleteAll.disabled = isDisabled;
        for (const button of this.downloadButtons.values()) {
          button.disabled = isDisabled;
        }
        for (const button of this.deleteButtons.values()) {
          button.disabled = isDisabled;
        }
      }

      /**
       * This method is static in case an error happens during the creation of the
       * TranslationsState.
       *
       * @param {string} l10nId
       * @param {Error} error
       */
      static showError(l10nId, error) {
        console.error(error);
        const errorMessage = document.getElementById(
          "translations-manage-error"
        );
        errorMessage.hidden = false;
        document.l10n.setAttributes(errorMessage, l10nId);
      }

      hideError() {
        this.elements.error.hidden = true;
      }

      observe(_subject, topic, _data) {
        if (topic === "intl:app-locales-changed") {
          this.refreshLanguageListDisplay();
        }
      }

      refreshLanguageListDisplay() {
        try {
          const languageDisplayNames =
            TranslationsParent.createLanguageDisplayNames();

          for (const row of this.elements.installList.children) {
            const rowLangTag = row.getAttribute("data-lang-tag");
            if (!rowLangTag) {
              continue;
            }

            const label = row.querySelector("label");
            if (label) {
              const newDisplayName = languageDisplayNames.of(rowLangTag);
              if (label.textContent !== newDisplayName) {
                label.textContent = newDisplayName;
              }
            }
          }
        } catch (error) {
          console.error(error);
        }
      }
    }

    TranslationsState.create().then(
      state => {
        this._translationsView = new TranslationsView(state);
      },
      error => {
        // This error can happen when a user is not connected to the internet, or
        // RemoteSettings is down for some reason.
        TranslationsView.showError("translations-manage-error-list", error);
      }
    );
  },

  initPrimaryBrowserLanguageUI() {
    // This will register the "command" listener.
    let menulist = document.getElementById("primaryBrowserLocale");
    new SelectionChangedMenulist(menulist, event => {
      gMainPane.onPrimaryBrowserLanguageMenuChange(event);
    });

    gMainPane.updatePrimaryBrowserLanguageUI(Services.locale.appLocaleAsBCP47);
  },

  /**
   * Update the available list of locales and select the locale that the user
   * is "selecting". This could be the currently requested locale or a locale
   * that the user would like to switch to after confirmation.
   *
   * @param {string} selected - The selected BCP 47 locale.
   */
  async updatePrimaryBrowserLanguageUI(selected) {
    let available = await LangPackMatcher.getAvailableLocales();
    let localeNames = Services.intl.getLocaleDisplayNames(
      undefined,
      available,
      { preferNative: true }
    );
    let locales = available.map((code, i) => ({ code, name: localeNames[i] }));
    locales.sort((a, b) => a.name > b.name);

    let fragment = document.createDocumentFragment();
    for (let { code, name } of locales) {
      let menuitem = document.createXULElement("menuitem");
      menuitem.setAttribute("value", code);
      menuitem.setAttribute("label", name);
      fragment.appendChild(menuitem);
    }

    // Add an option to search for more languages if downloading is supported.
    if (Services.prefs.getBoolPref("intl.multilingual.downloadEnabled")) {
      let menuitem = document.createXULElement("menuitem");
      menuitem.id = "primaryBrowserLocaleSearch";
      menuitem.setAttribute(
        "label",
        await document.l10n.formatValue("browser-languages-search")
      );
      menuitem.setAttribute("value", "search");
      fragment.appendChild(menuitem);
    }

    let menulist = document.getElementById("primaryBrowserLocale");
    let menupopup = menulist.querySelector("menupopup");
    menupopup.textContent = "";
    menupopup.appendChild(fragment);
    menulist.value = selected;

    document.getElementById("browserLanguagesBox").hidden = false;
  },

  /* Show the confirmation message bar to allow a restart into the new locales. */
  async showConfirmLanguageChangeMessageBar(locales) {
    let messageBar = document.getElementById("confirmBrowserLanguage");

    // Get the bundle for the new locale.
    let newBundle = getBundleForLocales(locales);

    // Find the messages and labels.
    let messages = await Promise.all(
      [newBundle, document.l10n].map(async bundle =>
        bundle.formatValue("confirm-browser-language-change-description")
      )
    );
    let buttonLabels = await Promise.all(
      [newBundle, document.l10n].map(async bundle =>
        bundle.formatValue("confirm-browser-language-change-button")
      )
    );

    // If both the message and label are the same, just include one row.
    if (messages[0] == messages[1] && buttonLabels[0] == buttonLabels[1]) {
      messages.pop();
      buttonLabels.pop();
    }

    let contentContainer = messageBar.querySelector(
      ".message-bar-content-container"
    );
    contentContainer.textContent = "";

    for (let i = 0; i < messages.length; i++) {
      let messageContainer = document.createXULElement("hbox");
      messageContainer.classList.add("message-bar-content");
      messageContainer.style.flex = "1 50%";
      messageContainer.setAttribute("align", "center");

      let description = document.createXULElement("description");
      description.classList.add("message-bar-description");

      if (i == 0 && Services.intl.getScriptDirection(locales[0]) === "rtl") {
        description.classList.add("rtl-locale");
      }
      description.setAttribute("flex", "1");
      description.textContent = messages[i];
      messageContainer.appendChild(description);

      let button = document.createXULElement("button");
      button.addEventListener(
        "command",
        gMainPane.confirmBrowserLanguageChange
      );
      button.classList.add("message-bar-button");
      button.setAttribute("locales", locales.join(","));
      button.setAttribute("label", buttonLabels[i]);
      messageContainer.appendChild(button);

      contentContainer.appendChild(messageContainer);
    }

    messageBar.hidden = false;
    gMainPane.selectedLocalesForRestart = locales;
  },

  hideConfirmLanguageChangeMessageBar() {
    let messageBar = document.getElementById("confirmBrowserLanguage");
    messageBar.hidden = true;
    let contentContainer = messageBar.querySelector(
      ".message-bar-content-container"
    );
    contentContainer.textContent = "";
    gMainPane.requestingLocales = null;
  },

  /* Confirm the locale change and restart the browser in the new locale. */
  confirmBrowserLanguageChange(event) {
    let localesString = (event.target.getAttribute("locales") || "").trim();
    if (!localesString || !localesString.length) {
      return;
    }
    let locales = localesString.split(",");
    Services.locale.requestedLocales = locales;

    // Record the change in telemetry before we restart.
    gMainPane.recordBrowserLanguagesTelemetry("apply");

    // Restart with the new locale.
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );
    if (!cancelQuit.data) {
      Services.startup.quit(
        Services.startup.eAttemptQuit | Services.startup.eRestart
      );
    }
  },

  /* Show or hide the confirm change message bar based on the new locale. */
  onPrimaryBrowserLanguageMenuChange(event) {
    let locale = event.target.value;

    if (locale == "search") {
      gMainPane.showBrowserLanguagesSubDialog({ search: true });
      return;
    } else if (locale == Services.locale.appLocaleAsBCP47) {
      this.hideConfirmLanguageChangeMessageBar();
      return;
    }

    let newLocales = Array.from(
      new Set([locale, ...Services.locale.requestedLocales]).values()
    );

    gMainPane.recordBrowserLanguagesTelemetry("reorder");

    switch (gMainPane.getLanguageSwitchTransitionType(newLocales)) {
      case "requires-restart":
        // Prepare to change the locales, as they were different.
        gMainPane.showConfirmLanguageChangeMessageBar(newLocales);
        gMainPane.updatePrimaryBrowserLanguageUI(newLocales[0]);
        break;
      case "live-reload":
        Services.locale.requestedLocales = newLocales;
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      case "locales-match":
        // They matched, so we can reset the UI.
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
  },

  /**
   *  Shows a window dialog containing the profile selector page.
   */
  manageProfiles() {
    const win = window.browsingContext.topChromeWindow;

    win.toOpenWindowByType(
      "about:profilemanager",
      "about:profilemanager",
      "chrome,extrachrome,menubar,resizable,scrollbars,status,toolbar,centerscreen"
    );
  },

  /**
   * Shows a dialog in which the preferred language for web content may be set.
   */
  showLanguages() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/languages.xhtml"
    );
  },

  recordBrowserLanguagesTelemetry(method, value = null) {
    Glean.intlUiBrowserLanguage[method + "Main"].record(
      value ? { value } : undefined
    );
  },

  /**
   * Open the browser languages sub dialog in either the normal mode, or search mode.
   * The search mode is only available from the menu to change the primary browser
   * language.
   *
   * @param {{ search: boolean }} search
   */
  showBrowserLanguagesSubDialog({ search }) {
    // Record the telemetry event with an id to associate related actions.
    let telemetryId = parseInt(
      Services.telemetry.msSinceProcessStart(),
      10
    ).toString();
    let method = search ? "search" : "manage";
    gMainPane.recordBrowserLanguagesTelemetry(method, telemetryId);

    let opts = {
      selectedLocalesForRestart: gMainPane.selectedLocalesForRestart,
      search,
      telemetryId,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/browserLanguages.xhtml",
      { closingCallback: this.browserLanguagesClosed },
      opts
    );
  },

  /**
   * Determine the transition strategy for switching the locale based on prefs
   * and the switched locales.
   *
   * @param {Array<string>} newLocales - List of BCP 47 locale identifiers.
   * @returns {"locales-match" | "requires-restart" | "live-reload"}
   */
  getLanguageSwitchTransitionType(newLocales) {
    const { appLocalesAsBCP47 } = Services.locale;
    if (appLocalesAsBCP47.join(",") === newLocales.join(",")) {
      // The selected locales match, the order matters.
      return "locales-match";
    }

    if (Services.prefs.getBoolPref("intl.multilingual.liveReload")) {
      if (
        Services.intl.getScriptDirection(newLocales[0]) !==
          Services.intl.getScriptDirection(appLocalesAsBCP47[0]) &&
        !Services.prefs.getBoolPref("intl.multilingual.liveReloadBidirectional")
      ) {
        // Bug 1750852: The directionality of the text changed, which requires a restart
        // until the quality of the switch can be improved.
        return "requires-restart";
      }

      return "live-reload";
    }

    return "requires-restart";
  },

  /* Show or hide the confirm change message bar based on the updated ordering. */
  browserLanguagesClosed() {
    // When the subdialog is closed, settings are stored on gBrowserLanguagesDialog.
    // The next time the dialog is opened, a new gBrowserLanguagesDialog is created.
    let { selected } = this.gBrowserLanguagesDialog;

    this.gBrowserLanguagesDialog.recordTelemetry(
      selected ? "accept" : "cancel"
    );

    if (!selected) {
      // No locales were selected. Cancel the operation.
      return;
    }

    // Track how often locale fallback order is changed.
    // Drop the first locale and filter to only include the overlapping set
    const prevLocales = Services.locale.requestedLocales.filter(
      lc => selected.indexOf(lc) > 0
    );
    const newLocales = selected.filter(
      (lc, i) => i > 0 && prevLocales.includes(lc)
    );
    if (prevLocales.some((lc, i) => newLocales[i] != lc)) {
      this.gBrowserLanguagesDialog.recordTelemetry("setFallback");
    }

    switch (gMainPane.getLanguageSwitchTransitionType(selected)) {
      case "requires-restart":
        gMainPane.showConfirmLanguageChangeMessageBar(selected);
        gMainPane.updatePrimaryBrowserLanguageUI(selected[0]);
        break;
      case "live-reload":
        Services.locale.requestedLocales = selected;

        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      case "locales-match":
        // They matched, so we can reset the UI.
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
  },

  displayUseSystemLocale() {
    let appLocale = Services.locale.appLocaleAsBCP47;
    let regionalPrefsLocales = Services.locale.regionalPrefsLocales;
    if (!regionalPrefsLocales.length) {
      return;
    }
    let systemLocale = regionalPrefsLocales[0];
    let localeDisplayname = Services.intl.getLocaleDisplayNames(
      undefined,
      [systemLocale],
      { preferNative: true }
    );
    if (!localeDisplayname.length) {
      return;
    }
    let localeName = localeDisplayname[0];
    if (appLocale.split("-u-")[0] != systemLocale.split("-u-")[0]) {
      let checkbox = document.getElementById("useSystemLocale");
      document.l10n.setAttributes(checkbox, "use-system-locale", {
        localeName,
      });
      checkbox.hidden = false;
    }
  },

  /**
   * Displays the translation exceptions dialog where specific site and language
   * translation preferences can be set.
   */
  // TODO (Bug 1817084) Remove this code when we disable the extension
  showTranslationExceptions() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/translationExceptions.xhtml"
    );
  },

  showTranslationsSettings() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/translations.xhtml"
    );
  },

  /**
   * Displays the fonts dialog, where web page font names and sizes can be
   * configured.
   */
  configureFonts() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/fonts.xhtml",
      { features: "resizable=no" }
    );
  },

  // NETWORK
  /**
   * Displays a dialog in which proxy settings may be changed.
   */
  showConnections() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/connection.xhtml"
    );
  },

  /**
   * Displays the migration wizard dialog in an HTML dialog.
   */
  async showMigrationWizardDialog({ closeTabWhenDone = false } = {}) {
    let migrationWizardDialog = document.getElementById(
      "migrationWizardDialog"
    );

    if (migrationWizardDialog.open) {
      return;
    }

    await customElements.whenDefined("migration-wizard");

    // If we've been opened before, remove the old wizard and insert a
    // new one to put it back into its starting state.
    if (!migrationWizardDialog.firstElementChild) {
      let wizard = document.createElement("migration-wizard");
      wizard.toggleAttribute("dialog-mode", true);
      migrationWizardDialog.appendChild(wizard);
    }
    migrationWizardDialog.firstElementChild.requestState();

    migrationWizardDialog.addEventListener(
      "close",
      () => {
        // Let others know that the wizard is closed -- potentially because of a
        // user action within the dialog that dispatches "MigrationWizard:Close"
        // but this also covers cases like hitting Escape.
        Services.obs.notifyObservers(
          migrationWizardDialog,
          "MigrationWizard:Closed"
        );
        if (closeTabWhenDone) {
          window.close();
        }
      },
      { once: true }
    );

    migrationWizardDialog.showModal();
  },

  _minUpdatePrefDisableTime: 1000,
  /**
   * Selects the correct item in the update radio group
   */
  async readUpdateAutoPref() {
    if (
      AppConstants.MOZ_UPDATER &&
      (!Services.policies || Services.policies.isAllowed("appUpdate")) &&
      !gIsPackagedApp
    ) {
      let radiogroup = document.getElementById("updateRadioGroup");

      radiogroup.disabled = true;
      let enabled = await UpdateUtils.getAppUpdateAutoEnabled();
      radiogroup.value = enabled;
      radiogroup.disabled = false;

      this.maybeDisableBackgroundUpdateControls();
    }
  },

  /**
   * Writes the value of the automatic update radio group to the disk
   */
  async writeUpdateAutoPref() {
    if (
      AppConstants.MOZ_UPDATER &&
      (!Services.policies || Services.policies.isAllowed("appUpdate")) &&
      !gIsPackagedApp
    ) {
      let radiogroup = document.getElementById("updateRadioGroup");
      let updateAutoValue = radiogroup.value == "true";
      let _disableTimeOverPromise = new Promise(r =>
        setTimeout(r, this._minUpdatePrefDisableTime)
      );
      radiogroup.disabled = true;
      try {
        await UpdateUtils.setAppUpdateAutoEnabled(updateAutoValue);
        await _disableTimeOverPromise;
        radiogroup.disabled = false;
      } catch (error) {
        console.error(error);
        await Promise.all([
          this.readUpdateAutoPref(),
          this.reportUpdatePrefWriteError(),
        ]);
        return;
      }

      this.maybeDisableBackgroundUpdateControls();

      // If the value was changed to false the user should be given the option
      // to discard an update if there is one.
      if (!updateAutoValue) {
        await this.checkUpdateInProgress();
      }
      // For tests:
      radiogroup.dispatchEvent(new CustomEvent("ProcessedUpdatePrefChange"));
    }
  },

  isBackgroundUpdateUIAvailable() {
    return (
      AppConstants.MOZ_UPDATE_AGENT &&
      // This UI controls a per-installation pref. It won't necessarily work
      // properly if per-installation prefs aren't supported.
      UpdateUtils.PER_INSTALLATION_PREFS_SUPPORTED &&
      (!Services.policies || Services.policies.isAllowed("appUpdate")) &&
      !gIsPackagedApp &&
      !UpdateUtils.appUpdateSettingIsLocked("app.update.background.enabled")
    );
  },

  maybeDisableBackgroundUpdateControls() {
    if (this.isBackgroundUpdateUIAvailable()) {
      let radiogroup = document.getElementById("updateRadioGroup");
      let updateAutoEnabled = radiogroup.value == "true";

      // This control is only active if auto update is enabled.
      document.getElementById("backgroundUpdate").disabled = !updateAutoEnabled;
    }
  },

  async readBackgroundUpdatePref() {
    const prefName = "app.update.background.enabled";
    if (this.isBackgroundUpdateUIAvailable()) {
      let backgroundCheckbox = document.getElementById("backgroundUpdate");

      // When the page first loads, the checkbox is unchecked until we finish
      // reading the config file from the disk. But, ideally, we don't want to
      // give the user the impression that this setting has somehow gotten
      // turned off and they need to turn it back on. We also don't want the
      // user interacting with the control, expecting a particular behavior, and
      // then have the read complete and change the control in an unexpected
      // way. So we disable the control while we are reading.
      // The only entry points for this function are page load and user
      // interaction with the control. By disabling the control to prevent
      // further user interaction, we prevent the possibility of entering this
      // function a second time while we are still reading.
      backgroundCheckbox.disabled = true;

      // If we haven't already done this, it might result in the effective value
      // of the Background Update pref changing. Thus, we should do it before
      // we tell the user what value this pref has.
      await BackgroundUpdate.ensureExperimentToRolloutTransitionPerformed();

      let enabled = await UpdateUtils.readUpdateConfigSetting(prefName);
      backgroundCheckbox.checked = enabled;
      this.maybeDisableBackgroundUpdateControls();
    }
  },

  async writeBackgroundUpdatePref() {
    const prefName = "app.update.background.enabled";
    if (this.isBackgroundUpdateUIAvailable()) {
      let backgroundCheckbox = document.getElementById("backgroundUpdate");
      backgroundCheckbox.disabled = true;
      let backgroundUpdateEnabled = backgroundCheckbox.checked;
      try {
        await UpdateUtils.writeUpdateConfigSetting(
          prefName,
          backgroundUpdateEnabled
        );
      } catch (error) {
        console.error(error);
        await this.readBackgroundUpdatePref();
        await this.reportUpdatePrefWriteError();
        return;
      }

      this.maybeDisableBackgroundUpdateControls();
    }
  },

  async reportUpdatePrefWriteError() {
    let [title, message] = await document.l10n.formatValues([
      { id: "update-setting-write-failure-title2" },
      {
        id: "update-setting-write-failure-message2",
        args: { path: UpdateUtils.configFilePath },
      },
    ]);

    // Set up the Ok Button
    let buttonFlags =
      Services.prompt.BUTTON_POS_0 * Services.prompt.BUTTON_TITLE_OK;
    Services.prompt.confirmEx(
      window,
      title,
      message,
      buttonFlags,
      null,
      null,
      null,
      null,
      {}
    );
  },

  async checkUpdateInProgress() {
    const aus = Cc["@mozilla.org/updates/update-service;1"].getService(
      Ci.nsIApplicationUpdateService
    );
    let um = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    // We don't want to see an idle state just because the updater hasn't
    // initialized yet.
    await aus.init();
    if (aus.currentState == Ci.nsIApplicationUpdateService.STATE_IDLE) {
      return;
    }

    let [title, message, okButton, cancelButton] =
      await document.l10n.formatValues([
        { id: "update-in-progress-title" },
        { id: "update-in-progress-message" },
        { id: "update-in-progress-ok-button" },
        { id: "update-in-progress-cancel-button" },
      ]);

    // Continue is the cancel button which is BUTTON_POS_1 and is set as the
    // default so pressing escape or using a platform standard method of closing
    // the UI will not discard the update.
    let buttonFlags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1 +
      Ci.nsIPrompt.BUTTON_POS_1_DEFAULT;

    let rv = Services.prompt.confirmEx(
      window,
      title,
      message,
      buttonFlags,
      okButton,
      cancelButton,
      null,
      null,
      {}
    );
    if (rv != 1) {
      await aus.stopDownload();
      await um.cleanupActiveUpdates();
      UpdateListener.clearPendingAndActiveNotifications();
    }
  },

  /**
   * Displays the history of installed updates.
   */
  showUpdates() {
    gSubDialog.open("chrome://mozapps/content/update/history.xhtml");
  },

  destroy() {
    window.removeEventListener("unload", this);
    Services.obs.removeObserver(this, AUTO_UPDATE_CHANGED_TOPIC);
    Services.obs.removeObserver(this, BACKGROUND_UPDATE_CHANGED_TOPIC);

    // Clean up the TranslationsView instance if it exists
    if (this._translationsView) {
      this._translationsView.destroy();
      this._translationsView = null;
    }
  },

  // nsISupports

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  // nsIObserver

  async observe(aSubject, aTopic, aData) {
    if (aTopic == "nsPref:changed") {
      if (aData == PREF_CONTAINERS_EXTENSION) {
        return;
      }
      // Rebuild the list when there are changes to preferences that influence
      // whether or not to show certain entries in the list.
      if (!this._storingAction) {
        await this._rebuildView();
      }
    } else if (aTopic == AUTO_UPDATE_CHANGED_TOPIC) {
      if (!AppConstants.MOZ_UPDATER) {
        return;
      }
      if (aData != "true" && aData != "false") {
        throw new Error("Invalid preference value for app.update.auto");
      }
      document.getElementById("updateRadioGroup").value = aData;
      this.maybeDisableBackgroundUpdateControls();
    } else if (aTopic == BACKGROUND_UPDATE_CHANGED_TOPIC) {
      if (!AppConstants.MOZ_UPDATE_AGENT) {
        return;
      }
      if (aData != "true" && aData != "false") {
        throw new Error(
          "Invalid preference value for app.update.background.enabled"
        );
      }
      document.getElementById("backgroundUpdate").checked = aData == "true";
    }
  },

  // EventListener

  handleEvent(aEvent) {
    if (aEvent.type == "unload") {
      this.destroy();
      if (AppConstants.MOZ_UPDATER) {
        onUnload();
      }
    }
  },

  // Composed Model Construction

  _loadData() {
    this._loadInternalHandlers();
    this._loadApplicationHandlers();
  },

  /**
   * Load higher level internal handlers so they can be turned on/off in the
   * applications menu.
   */
  _loadInternalHandlers() {
    let internalHandlers = [new PDFHandlerInfoWrapper()];

    let enabledHandlers = Services.prefs
      .getCharPref("browser.download.viewableInternally.enabledTypes", "")
      .trim();
    if (enabledHandlers) {
      for (let ext of enabledHandlers.split(",")) {
        internalHandlers.push(
          new ViewableInternallyHandlerInfoWrapper(null, ext.trim())
        );
      }
    }
    for (let internalHandler of internalHandlers) {
      if (internalHandler.enabled) {
        this._handledTypes[internalHandler.type] = internalHandler;
      }
    }
  },

  /**
   * Load the set of handlers defined by the application datastore.
   */
  _loadApplicationHandlers() {
    for (let wrappedHandlerInfo of gHandlerService.enumerate()) {
      let type = wrappedHandlerInfo.type;

      let handlerInfoWrapper;
      if (type in this._handledTypes) {
        handlerInfoWrapper = this._handledTypes[type];
      } else {
        if (DownloadIntegration.shouldViewDownloadInternally(type)) {
          handlerInfoWrapper = new ViewableInternallyHandlerInfoWrapper(type);
        } else {
          handlerInfoWrapper = new HandlerInfoWrapper(type, wrappedHandlerInfo);
        }
        this._handledTypes[type] = handlerInfoWrapper;
      }
    }
  },

  // View Construction

  selectedHandlerListItem: null,

  _initListEventHandlers() {
    this._list.addEventListener("select", event => {
      if (event.target != this._list) {
        return;
      }

      let handlerListItem =
        this._list.selectedItem &&
        HandlerListItem.forNode(this._list.selectedItem);
      if (this.selectedHandlerListItem == handlerListItem) {
        return;
      }

      if (this.selectedHandlerListItem) {
        this.selectedHandlerListItem.showActionsMenu = false;
      }
      this.selectedHandlerListItem = handlerListItem;
      if (handlerListItem) {
        this.rebuildActionsMenu();
        handlerListItem.showActionsMenu = true;
      }
    });
  },

  async _rebuildVisibleTypes() {
    this._visibleTypes = [];

    // Map whose keys are string descriptions and values are references to the
    // first visible HandlerInfoWrapper that has this description. We use this
    // to determine whether or not to annotate descriptions with their types to
    // distinguish duplicate descriptions from each other.
    let visibleDescriptions = new Map();
    for (let type in this._handledTypes) {
      // Yield before processing each handler info object to avoid monopolizing
      // the main thread, as the objects are retrieved lazily, and retrieval
      // can be expensive on Windows.
      await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

      let handlerInfo = this._handledTypes[type];

      // We couldn't find any reason to exclude the type, so include it.
      this._visibleTypes.push(handlerInfo);

      let key = JSON.stringify(handlerInfo.description);
      let otherHandlerInfo = visibleDescriptions.get(key);
      if (!otherHandlerInfo) {
        // This is the first type with this description that we encountered
        // while rebuilding the _visibleTypes array this time. Make sure the
        // flag is reset so we won't add the type to the description.
        handlerInfo.disambiguateDescription = false;
        visibleDescriptions.set(key, handlerInfo);
      } else {
        // There is at least another type with this description. Make sure we
        // add the type to the description on both HandlerInfoWrapper objects.
        handlerInfo.disambiguateDescription = true;
        otherHandlerInfo.disambiguateDescription = true;
      }
    }
  },

  async _rebuildView() {
    let lastSelectedType =
      this.selectedHandlerListItem &&
      this.selectedHandlerListItem.handlerInfoWrapper.type;
    this.selectedHandlerListItem = null;

    // Clear the list of entries.
    this._list.textContent = "";

    var visibleTypes = this._visibleTypes;

    let items = visibleTypes.map(
      visibleType => new HandlerListItem(visibleType)
    );
    let itemsFragment = document.createDocumentFragment();
    let lastSelectedItem;
    for (let item of items) {
      item.createNode(itemsFragment);
      if (item.handlerInfoWrapper.type == lastSelectedType) {
        lastSelectedItem = item;
      }
    }

    for (let item of items) {
      item.setupNode();
      this.rebuildActionsMenu(item.node, item.handlerInfoWrapper);
      item.refreshAction();
    }

    // If the user is filtering the list, then only show matching types.
    // If we filter, we need to first localize the fragment, to
    // be able to filter by localized values.
    if (this._filter.value) {
      await document.l10n.translateFragment(itemsFragment);

      this._filterView(itemsFragment);

      document.l10n.pauseObserving();
      this._list.appendChild(itemsFragment);
      document.l10n.resumeObserving();
    } else {
      // Otherwise we can just append the fragment and it'll
      // get localized via the Mutation Observer.
      this._list.appendChild(itemsFragment);
    }

    if (lastSelectedItem) {
      this._list.selectedItem = lastSelectedItem.node;
    }
  },

  /**
   * Whether or not the given handler app is valid.
   *
   * @param aHandlerApp {nsIHandlerApp} the handler app in question
   *
   * @returns {boolean} whether or not it's valid
   */
  isValidHandlerApp(aHandlerApp) {
    if (!aHandlerApp) {
      return false;
    }

    if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
      return this._isValidHandlerExecutable(aHandlerApp.executable);
    }

    if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
      return aHandlerApp.uriTemplate;
    }

    if (aHandlerApp instanceof Ci.nsIGIOMimeApp) {
      return aHandlerApp.command;
    }
    if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
      return aHandlerApp.id;
    }

    return false;
  },

  _isValidHandlerExecutable(aExecutable) {
    let leafName;
    if (AppConstants.platform == "win") {
      leafName = `${AppConstants.MOZ_APP_NAME}.exe`;
    } else if (AppConstants.platform == "macosx") {
      leafName = AppConstants.MOZ_MACBUNDLE_NAME;
    } else {
      leafName = `${AppConstants.MOZ_APP_NAME}-bin`;
    }
    return (
      aExecutable &&
      aExecutable.exists() &&
      aExecutable.isExecutable() &&
      // XXXben - we need to compare this with the running instance executable
      //          just don't know how to do that via script...
      // XXXmano TBD: can probably add this to nsIShellService
      aExecutable.leafName != leafName
    );
  },

  /**
   * Rebuild the actions menu for the selected entry.  Gets called by
   * the richlistitem constructor when an entry in the list gets selected.
   */
  rebuildActionsMenu(
    typeItem = this._list.selectedItem,
    handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper
  ) {
    var menu = typeItem.querySelector(".actionsMenu");
    var menuPopup = menu.menupopup;

    // Clear out existing items.
    while (menuPopup.hasChildNodes()) {
      menuPopup.removeChild(menuPopup.lastChild);
    }

    let internalMenuItem;
    // Add the "Open in Firefox" option for optional internal handlers.
    if (
      handlerInfo instanceof InternalHandlerInfoWrapper &&
      !handlerInfo.preventInternalViewing
    ) {
      internalMenuItem = document.createXULElement("menuitem");
      internalMenuItem.setAttribute(
        "action",
        Ci.nsIHandlerInfo.handleInternally
      );
      internalMenuItem.className = "menuitem-iconic";
      document.l10n.setAttributes(internalMenuItem, "applications-open-inapp");
      internalMenuItem.setAttribute(APP_ICON_ATTR_NAME, "handleInternally");
      menuPopup.appendChild(internalMenuItem);
    }

    var askMenuItem = document.createXULElement("menuitem");
    askMenuItem.setAttribute("action", Ci.nsIHandlerInfo.alwaysAsk);
    askMenuItem.className = "menuitem-iconic";
    document.l10n.setAttributes(askMenuItem, "applications-always-ask");
    askMenuItem.setAttribute(APP_ICON_ATTR_NAME, "ask");
    menuPopup.appendChild(askMenuItem);

    // Create a menu item for saving to disk.
    // Note: this option isn't available to protocol types, since we don't know
    // what it means to save a URL having a certain scheme to disk.
    if (handlerInfo.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      var saveMenuItem = document.createXULElement("menuitem");
      saveMenuItem.setAttribute("action", Ci.nsIHandlerInfo.saveToDisk);
      document.l10n.setAttributes(saveMenuItem, "applications-action-save");
      saveMenuItem.setAttribute(APP_ICON_ATTR_NAME, "save");
      saveMenuItem.className = "menuitem-iconic";
      menuPopup.appendChild(saveMenuItem);
    }

    // Add a separator to distinguish these items from the helper app items
    // that follow them.
    let menuseparator = document.createXULElement("menuseparator");
    menuPopup.appendChild(menuseparator);

    // Create a menu item for the OS default application, if any.
    if (handlerInfo.hasDefaultHandler) {
      var defaultMenuItem = document.createXULElement("menuitem");
      defaultMenuItem.setAttribute(
        "action",
        Ci.nsIHandlerInfo.useSystemDefault
      );
      // If an internal option is available, don't show the application
      // name for the OS default to prevent two options from appearing
      // that may both say "Firefox".
      if (internalMenuItem) {
        document.l10n.setAttributes(
          defaultMenuItem,
          "applications-use-os-default"
        );
        defaultMenuItem.setAttribute("image", ICON_URL_APP);
      } else {
        document.l10n.setAttributes(
          defaultMenuItem,
          "applications-use-app-default",
          {
            "app-name": handlerInfo.defaultDescription,
          }
        );
        let image = handlerInfo.iconURLForSystemDefault;
        if (image) {
          defaultMenuItem.setAttribute("image", image);
        }
      }

      menuPopup.appendChild(defaultMenuItem);
    }

    // Create menu items for possible handlers.
    let preferredApp = handlerInfo.preferredApplicationHandler;
    var possibleAppMenuItems = [];
    for (let possibleApp of handlerInfo.possibleApplicationHandlers.enumerate()) {
      if (!this.isValidHandlerApp(possibleApp)) {
        continue;
      }

      let menuItem = document.createXULElement("menuitem");
      menuItem.setAttribute("action", Ci.nsIHandlerInfo.useHelperApp);
      let label;
      if (possibleApp instanceof Ci.nsILocalHandlerApp) {
        label = getFileDisplayName(possibleApp.executable);
      } else {
        label = possibleApp.name;
      }
      document.l10n.setAttributes(menuItem, "applications-use-app", {
        "app-name": label,
      });
      let image = this._getIconURLForHandlerApp(possibleApp);
      if (image) {
        menuItem.setAttribute("image", image);
      }

      // Attach the handler app object to the menu item so we can use it
      // to make changes to the datastore when the user selects the item.
      menuItem.handlerApp = possibleApp;

      menuPopup.appendChild(menuItem);
      possibleAppMenuItems.push(menuItem);
    }
    // Add gio handlers
    if (gGIOService) {
      var gioApps = gGIOService.getAppsForURIScheme(handlerInfo.type);
      let possibleHandlers = handlerInfo.possibleApplicationHandlers;
      for (let handler of gioApps.enumerate(Ci.nsIHandlerApp)) {
        // OS handler share the same name, it's most likely the same app, skipping...
        if (handler.name == handlerInfo.defaultDescription) {
          continue;
        }
        // Check if the handler is already in possibleHandlers
        let appAlreadyInHandlers = false;
        for (let i = possibleHandlers.length - 1; i >= 0; --i) {
          let app = possibleHandlers.queryElementAt(i, Ci.nsIHandlerApp);
          // nsGIOMimeApp::Equals is able to compare with nsILocalHandlerApp
          if (handler.equals(app)) {
            appAlreadyInHandlers = true;
            break;
          }
        }
        if (!appAlreadyInHandlers) {
          let menuItem = document.createXULElement("menuitem");
          menuItem.setAttribute("action", Ci.nsIHandlerInfo.useHelperApp);
          document.l10n.setAttributes(menuItem, "applications-use-app", {
            "app-name": handler.name,
          });

          let image = this._getIconURLForHandlerApp(handler);
          if (image) {
            menuItem.setAttribute("image", image);
          }

          // Attach the handler app object to the menu item so we can use it
          // to make changes to the datastore when the user selects the item.
          menuItem.handlerApp = handler;

          menuPopup.appendChild(menuItem);
          possibleAppMenuItems.push(menuItem);
        }
      }
    }

    // Create a menu item for selecting a local application.
    let canOpenWithOtherApp = true;
    if (AppConstants.platform == "win") {
      // On Windows, selecting an application to open another application
      // would be meaningless so we special case executables.
      let executableType = Cc["@mozilla.org/mime;1"]
        .getService(Ci.nsIMIMEService)
        .getTypeFromExtension("exe");
      canOpenWithOtherApp = handlerInfo.type != executableType;
    }
    if (canOpenWithOtherApp) {
      let menuItem = document.createXULElement("menuitem");
      menuItem.className = "choose-app-item";
      menuItem.addEventListener("command", function (e) {
        gMainPane.chooseApp(e);
      });
      document.l10n.setAttributes(menuItem, "applications-use-other");
      menuPopup.appendChild(menuItem);
    }

    // Create a menu item for managing applications.
    if (possibleAppMenuItems.length) {
      let menuItem = document.createXULElement("menuseparator");
      menuPopup.appendChild(menuItem);
      menuItem = document.createXULElement("menuitem");
      menuItem.className = "manage-app-item";
      menuItem.addEventListener("command", function (e) {
        gMainPane.manageApp(e);
      });
      document.l10n.setAttributes(menuItem, "applications-manage-app");
      menuPopup.appendChild(menuItem);
    }

    // Select the item corresponding to the preferred action.  If the always
    // ask flag is set, it overrides the preferred action.  Otherwise we pick
    // the item identified by the preferred action (when the preferred action
    // is to use a helper app, we have to pick the specific helper app item).
    if (handlerInfo.alwaysAskBeforeHandling) {
      menu.selectedItem = askMenuItem;
    } else {
      // The nsHandlerInfoAction enumeration values in nsIHandlerInfo identify
      // the actions the application can take with content of various types.
      // But since we've stopped support for plugins, there's no value
      // identifying the "use plugin" action, so we use this constant instead.
      const kActionUsePlugin = 5;

      switch (handlerInfo.preferredAction) {
        case Ci.nsIHandlerInfo.handleInternally:
          if (internalMenuItem) {
            menu.selectedItem = internalMenuItem;
          } else {
            console.error("No menu item defined to set!");
          }
          break;
        case Ci.nsIHandlerInfo.useSystemDefault:
          // We might not have a default item if we're not aware of an
          // OS-default handler for this type:
          menu.selectedItem = defaultMenuItem || askMenuItem;
          break;
        case Ci.nsIHandlerInfo.useHelperApp:
          if (preferredApp) {
            let preferredItem = possibleAppMenuItems.find(v =>
              v.handlerApp.equals(preferredApp)
            );
            if (preferredItem) {
              menu.selectedItem = preferredItem;
            } else {
              // This shouldn't happen, but let's make sure we end up with a
              // selected item:
              let possible = possibleAppMenuItems
                .map(v => v.handlerApp && v.handlerApp.name)
                .join(", ");
              console.error(
                new Error(
                  `Preferred handler for ${handlerInfo.type} not in list of possible handlers!? (List: ${possible})`
                )
              );
              menu.selectedItem = askMenuItem;
            }
          }
          break;
        case kActionUsePlugin:
          // We no longer support plugins, select "ask" instead:
          menu.selectedItem = askMenuItem;
          break;
        case Ci.nsIHandlerInfo.saveToDisk:
          menu.selectedItem = saveMenuItem;
          break;
      }
    }
  },

  // Sorting & Filtering

  _sortColumn: null,

  /**
   * Sort the list when the user clicks on a column header.
   */
  sort(event) {
    if (event.button != 0) {
      return;
    }
    var column = event.target;

    // If the user clicked on a new sort column, remove the direction indicator
    // from the old column.
    if (this._sortColumn && this._sortColumn != column) {
      this._sortColumn.removeAttribute("sortDirection");
    }

    this._sortColumn = column;

    // Set (or switch) the sort direction indicator.
    if (column.getAttribute("sortDirection") == "ascending") {
      column.setAttribute("sortDirection", "descending");
    } else {
      column.setAttribute("sortDirection", "ascending");
    }

    this._sortListView();
  },

  async _sortListView() {
    if (!this._sortColumn) {
      return;
    }
    let comp = new Services.intl.Collator(undefined, {
      usage: "sort",
    });

    await document.l10n.translateFragment(this._list);
    let items = Array.from(this._list.children);

    let textForNode;
    if (this._sortColumn.getAttribute("value") === "type") {
      textForNode = n => n.querySelector(".typeDescription").textContent;
    } else {
      textForNode = n => n.querySelector(".actionsMenu").getAttribute("label");
    }

    let sortDir = this._sortColumn.getAttribute("sortDirection");
    let multiplier = sortDir == "descending" ? -1 : 1;
    items.sort(
      (a, b) => multiplier * comp.compare(textForNode(a), textForNode(b))
    );

    // Re-append items in the correct order:
    items.forEach(item => this._list.appendChild(item));
  },

  _filterView(frag = this._list) {
    const filterValue = this._filter.value.toLowerCase();
    for (let elem of frag.children) {
      const typeDescription =
        elem.querySelector(".typeDescription").textContent;
      const actionDescription = elem
        .querySelector(".actionDescription")
        .getAttribute("value");
      elem.hidden =
        !typeDescription.toLowerCase().includes(filterValue) &&
        !actionDescription.toLowerCase().includes(filterValue);
    }
  },

  /**
   * Filter the list when the user enters a filter term into the filter field.
   */
  filter() {
    this._rebuildView(); // FIXME: Should this be await since bug 1508156?
  },

  focusFilterBox() {
    this._filter.focus();
    this._filter.select();
  },

  // Changes

  // Whether or not we are currently storing the action selected by the user.
  // We use this to suppress notification-triggered updates to the list when
  // we make changes that may spawn such updates.
  // XXXgijs: this was definitely necessary when we changed feed preferences
  // from within _storeAction and its calltree. Now, it may still be
  // necessary, to avoid calling _rebuildView. bug 1499350 has more details.
  _storingAction: false,

  onSelectAction(aActionItem) {
    this._storingAction = true;

    try {
      this._storeAction(aActionItem);
    } finally {
      this._storingAction = false;
    }
  },

  _storeAction(aActionItem) {
    var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

    let action = parseInt(aActionItem.getAttribute("action"));

    // Set the preferred application handler.
    // We leave the existing preferred app in the list when we set
    // the preferred action to something other than useHelperApp so that
    // legacy datastores that don't have the preferred app in the list
    // of possible apps still include the preferred app in the list of apps
    // the user can choose to handle the type.
    if (action == Ci.nsIHandlerInfo.useHelperApp) {
      handlerInfo.preferredApplicationHandler = aActionItem.handlerApp;
    }

    // Set the "always ask" flag.
    if (action == Ci.nsIHandlerInfo.alwaysAsk) {
      handlerInfo.alwaysAskBeforeHandling = true;
    } else {
      handlerInfo.alwaysAskBeforeHandling = false;
    }

    // Set the preferred action.
    handlerInfo.preferredAction = action;

    handlerInfo.store();

    // Update the action label and image to reflect the new preferred action.
    this.selectedHandlerListItem.refreshAction();
  },

  manageApp(aEvent) {
    // Don't let the normal "on select action" handler get this event,
    // as we handle it specially ourselves.
    aEvent.stopPropagation();

    var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

    let onComplete = () => {
      // Rebuild the actions menu so that we revert to the previous selection,
      // or "Always ask" if the previous default application has been removed
      this.rebuildActionsMenu();

      // update the richlistitem too. Will be visible when selecting another row
      this.selectedHandlerListItem.refreshAction();
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/applicationManager.xhtml",
      { features: "resizable=no", closingCallback: onComplete },
      handlerInfo
    );
  },

  async chooseApp(aEvent) {
    // Don't let the normal "on select action" handler get this event,
    // as we handle it specially ourselves.
    aEvent.stopPropagation();

    var handlerApp;
    let chooseAppCallback = aHandlerApp => {
      // Rebuild the actions menu whether the user picked an app or canceled.
      // If they picked an app, we want to add the app to the menu and select it.
      // If they canceled, we want to go back to their previous selection.
      this.rebuildActionsMenu();

      // If the user picked a new app from the menu, select it.
      if (aHandlerApp) {
        let typeItem = this._list.selectedItem;
        let actionsMenu = typeItem.querySelector(".actionsMenu");
        let menuItems = actionsMenu.menupopup.childNodes;
        for (let i = 0; i < menuItems.length; i++) {
          let menuItem = menuItems[i];
          if (menuItem.handlerApp && menuItem.handlerApp.equals(aHandlerApp)) {
            actionsMenu.selectedIndex = i;
            this.onSelectAction(menuItem);
            break;
          }
        }
      }
    };

    if (AppConstants.platform == "win") {
      var params = {};
      var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

      params.mimeInfo = handlerInfo.wrappedHandlerInfo;
      params.title = await document.l10n.formatValue(
        "applications-select-helper"
      );
      if ("id" in handlerInfo.description) {
        params.description = await document.l10n.formatValue(
          handlerInfo.description.id,
          handlerInfo.description.args
        );
      } else {
        params.description = handlerInfo.typeDescription.raw;
      }
      params.filename = null;
      params.handlerApp = null;

      let onAppSelected = () => {
        if (this.isValidHandlerApp(params.handlerApp)) {
          handlerApp = params.handlerApp;

          // Add the app to the type's list of possible handlers.
          handlerInfo.addPossibleApplicationHandler(handlerApp);
        }

        chooseAppCallback(handlerApp);
      };

      gSubDialog.open(
        "chrome://global/content/appPicker.xhtml",
        { closingCallback: onAppSelected },
        params
      );
    } else {
      let winTitle = await document.l10n.formatValue(
        "applications-select-helper"
      );
      let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
      let fpCallback = aResult => {
        if (
          aResult == Ci.nsIFilePicker.returnOK &&
          fp.file &&
          this._isValidHandlerExecutable(fp.file)
        ) {
          handlerApp = Cc[
            "@mozilla.org/uriloader/local-handler-app;1"
          ].createInstance(Ci.nsILocalHandlerApp);
          handlerApp.name = getFileDisplayName(fp.file);
          handlerApp.executable = fp.file;

          // Add the app to the type's list of possible handlers.
          let handler = this.selectedHandlerListItem.handlerInfoWrapper;
          handler.addPossibleApplicationHandler(handlerApp);

          chooseAppCallback(handlerApp);
        }
      };

      // Prompt the user to pick an app.  If they pick one, and it's a valid
      // selection, then add it to the list of possible handlers.
      fp.init(window.browsingContext, winTitle, Ci.nsIFilePicker.modeOpen);
      fp.appendFilters(Ci.nsIFilePicker.filterApps);
      fp.open(fpCallback);
    }
  },

  _getIconURLForHandlerApp(aHandlerApp) {
    if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
      return this._getIconURLForFile(aHandlerApp.executable);
    }

    if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
      return this._getIconURLForWebApp(aHandlerApp.uriTemplate);
    }

    if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
      return this._getIconURLForAppId(aHandlerApp.id);
    }

    // We know nothing about other kinds of handler apps.
    return "";
  },

  _getIconURLForAppId(aAppId) {
    return "moz-icon://" + aAppId + "?size=16";
  },

  _getIconURLForFile(aFile) {
    var fph = Services.io
      .getProtocolHandler("file")
      .QueryInterface(Ci.nsIFileProtocolHandler);
    var urlSpec = fph.getURLSpecFromActualFile(aFile);

    return "moz-icon://" + urlSpec + "?size=16";
  },

  _getIconURLForWebApp(aWebAppURITemplate) {
    var uri = Services.io.newURI(aWebAppURITemplate);

    // Unfortunately we can't use the favicon service to get the favicon,
    // because the service looks in the annotations table for a record with
    // the exact URL we give it, and users won't have such records for URLs
    // they don't visit, and users won't visit the web app's URL template,
    // they'll only visit URLs derived from that template (i.e. with %s
    // in the template replaced by the URL of the content being handled).

    if (
      /^https?$/.test(uri.scheme) &&
      Services.prefs.getBoolPref("browser.chrome.site_icons")
    ) {
      // As the favicon originates from web content and is displayed in the parent process,
      // use the moz-remote-image: protocol to safely re-encode it.
      return getMozRemoteImageURL(uri.prePath + "/favicon.ico", { size: 16 });
    }

    return "";
  },
};

gMainPane.initialized = new Promise(res => {
  gMainPane.setInitialized = res;
});

// Utilities

function getFileDisplayName(file) {
  if (AppConstants.platform == "win") {
    if (file instanceof Ci.nsILocalFileWin) {
      try {
        return file.getVersionInfoField("FileDescription");
      } catch (e) {}
    }
  }
  if (AppConstants.platform == "macosx") {
    if (file instanceof Ci.nsILocalFileMac) {
      try {
        return file.bundleDisplayName;
      } catch (e) {}
    }
  }
  return file.leafName;
}

function getLocalHandlerApp(aFile) {
  var localHandlerApp = Cc[
    "@mozilla.org/uriloader/local-handler-app;1"
  ].createInstance(Ci.nsILocalHandlerApp);
  localHandlerApp.name = getFileDisplayName(aFile);
  localHandlerApp.executable = aFile;

  return localHandlerApp;
}

// eslint-disable-next-line no-undef
let gHandlerListItemFragment = MozXULElement.parseXULToFragment(`
  <richlistitem>
    <hbox class="typeContainer" flex="1" align="center">
      <html:img class="typeIcon" width="16" height="16" />
      <label class="typeDescription" flex="1" crop="end"/>
    </hbox>
    <hbox class="actionContainer" flex="1" align="center">
      <html:img class="actionIcon" width="16" height="16"/>
      <label class="actionDescription" flex="1" crop="end"/>
    </hbox>
    <hbox class="actionsMenuContainer" flex="1">
      <menulist class="actionsMenu" flex="1" crop="end" selectedIndex="1" aria-labelledby="actionColumn">
        <menupopup/>
      </menulist>
    </hbox>
  </richlistitem>
`);

/**
 * This is associated to <richlistitem> elements in the handlers view.
 */
class HandlerListItem {
  static forNode(node) {
    return gNodeToObjectMap.get(node);
  }

  constructor(handlerInfoWrapper) {
    this.handlerInfoWrapper = handlerInfoWrapper;
  }

  setOrRemoveAttributes(iterable) {
    for (let [selector, name, value] of iterable) {
      let node = selector ? this.node.querySelector(selector) : this.node;
      if (value) {
        node.setAttribute(name, value);
      } else {
        node.removeAttribute(name);
      }
    }
  }

  createNode(list) {
    list.appendChild(document.importNode(gHandlerListItemFragment, true));
    this.node = list.lastChild;
    gNodeToObjectMap.set(this.node, this);
  }

  setupNode() {
    this.node
      .querySelector(".actionsMenu")
      .addEventListener("command", event =>
        gMainPane.onSelectAction(event.originalTarget)
      );

    let typeDescription = this.handlerInfoWrapper.typeDescription;
    this.setOrRemoveAttributes([
      [null, "type", this.handlerInfoWrapper.type],
      [".typeIcon", "srcset", this.handlerInfoWrapper.iconSrcSet],
    ]);
    localizeElement(
      this.node.querySelector(".typeDescription"),
      typeDescription
    );
    this.showActionsMenu = false;
  }

  refreshAction() {
    let { actionIconClass } = this.handlerInfoWrapper;
    this.setOrRemoveAttributes([
      [null, APP_ICON_ATTR_NAME, actionIconClass],
      [
        ".actionIcon",
        "srcset",
        actionIconClass ? null : this.handlerInfoWrapper.actionIconSrcset,
      ],
    ]);
    const selectedItem = this.node.querySelector("[selected=true]");
    if (!selectedItem) {
      console.error("No selected item for " + this.handlerInfoWrapper.type);
      return;
    }
    const { id, args } = document.l10n.getAttributes(selectedItem);
    const messageIDs = {
      "applications-action-save": "applications-action-save-label",
      "applications-always-ask": "applications-always-ask-label",
      "applications-open-inapp": "applications-open-inapp-label",
      "applications-use-app-default": "applications-use-app-default-label",
      "applications-use-app": "applications-use-app-label",
      "applications-use-os-default": "applications-use-os-default-label",
      "applications-use-other": "applications-use-other-label",
    };
    localizeElement(this.node.querySelector(".actionDescription"), {
      id: messageIDs[id],
      args,
    });
    localizeElement(this.node.querySelector(".actionsMenu"), { id, args });
  }

  set showActionsMenu(value) {
    this.setOrRemoveAttributes([
      [".actionContainer", "hidden", value],
      [".actionsMenuContainer", "hidden", !value],
    ]);
  }
}

/**
 * This API facilitates dual-model of some localization APIs which
 * may operate on raw strings of l10n id/args pairs.
 *
 * The l10n can be:
 *
 * {raw: string} - raw strings to be used as text value of the element
 * {id: string} - l10n-id
 * {id: string, args: object} - l10n-id + l10n-args
 */
function localizeElement(node, l10n) {
  if (l10n.hasOwnProperty("raw")) {
    node.removeAttribute("data-l10n-id");
    node.textContent = l10n.raw;
  } else {
    document.l10n.setAttributes(node, l10n.id, l10n.args);
  }
}

/**
 * This object wraps nsIHandlerInfo with some additional functionality
 * the Applications prefpane needs to display and allow modification of
 * the list of handled types.
 *
 * We create an instance of this wrapper for each entry we might display
 * in the prefpane, and we compose the instances from various sources,
 * including the handler service.
 *
 * We don't implement all the original nsIHandlerInfo functionality,
 * just the stuff that the prefpane needs.
 */
class HandlerInfoWrapper {
  constructor(type, handlerInfo) {
    this.type = type;
    this.wrappedHandlerInfo = handlerInfo;
    this.disambiguateDescription = false;
  }

  get description() {
    if (this.wrappedHandlerInfo.description) {
      return { raw: this.wrappedHandlerInfo.description };
    }

    if (this.primaryExtension) {
      var extension = this.primaryExtension.toUpperCase();
      return { id: "applications-file-ending", args: { extension } };
    }

    return { raw: this.type };
  }

  /**
   * Describe, in a human-readable fashion, the type represented by the given
   * handler info object.  Normally this is just the description, but if more
   * than one object presents the same description, "disambiguateDescription"
   * is set and we annotate the duplicate descriptions with the type itself
   * to help users distinguish between those types.
   */
  get typeDescription() {
    if (this.disambiguateDescription) {
      const description = this.description;
      if (description.id) {
        // Pass through the arguments:
        let { args = {} } = description;
        args.type = this.type;
        return {
          id: description.id + "-with-type",
          args,
        };
      }

      return {
        id: "applications-type-description-with-type",
        args: {
          "type-description": description.raw,
          type: this.type,
        },
      };
    }

    return this.description;
  }

  get actionIconClass() {
    if (this.alwaysAskBeforeHandling) {
      return "ask";
    }

    switch (this.preferredAction) {
      case Ci.nsIHandlerInfo.saveToDisk:
        return "save";

      case Ci.nsIHandlerInfo.handleInternally:
        if (this instanceof InternalHandlerInfoWrapper) {
          return "handleInternally";
        }
        break;
    }

    return "";
  }

  get actionIconSrcset() {
    let icon = this.actionIcon;
    if (!icon || !icon.startsWith("moz-icon:")) {
      return icon;
    }
    // We rely on the icon already having the ?size= parameter.
    let srcset = [];
    for (let scale of [1, 2, 3]) {
      let scaledIcon = icon + "&scale=" + scale;
      srcset.push(`${scaledIcon} ${scale}x`);
    }
    return srcset.join(", ");
  }

  get actionIcon() {
    switch (this.preferredAction) {
      case Ci.nsIHandlerInfo.useSystemDefault:
        return this.iconURLForSystemDefault;

      case Ci.nsIHandlerInfo.useHelperApp: {
        let preferredApp = this.preferredApplicationHandler;
        if (gMainPane.isValidHandlerApp(preferredApp)) {
          return gMainPane._getIconURLForHandlerApp(preferredApp);
        }
      }
      // This should never happen, but if preferredAction is set to some weird
      // value, then fall back to the generic application icon.
      // Explicit fall-through
      default:
        return ICON_URL_APP;
    }
  }

  get iconURLForSystemDefault() {
    // Handler info objects for MIME types on some OSes implement a property bag
    // interface from which we can get an icon for the default app, so if we're
    // dealing with a MIME type on one of those OSes, then try to get the icon.
    if (
      this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo &&
      this.wrappedHandlerInfo instanceof Ci.nsIPropertyBag
    ) {
      try {
        let url = this.wrappedHandlerInfo.getProperty(
          "defaultApplicationIconURL"
        );
        if (url) {
          return url + "?size=16";
        }
      } catch (ex) {}
    }

    // If this isn't a MIME type object on an OS that supports retrieving
    // the icon, or if we couldn't retrieve the icon for some other reason,
    // then use a generic icon.
    return ICON_URL_APP;
  }

  get preferredApplicationHandler() {
    return this.wrappedHandlerInfo.preferredApplicationHandler;
  }

  set preferredApplicationHandler(aNewValue) {
    this.wrappedHandlerInfo.preferredApplicationHandler = aNewValue;

    // Make sure the preferred handler is in the set of possible handlers.
    if (aNewValue) {
      this.addPossibleApplicationHandler(aNewValue);
    }
  }

  get possibleApplicationHandlers() {
    return this.wrappedHandlerInfo.possibleApplicationHandlers;
  }

  addPossibleApplicationHandler(aNewHandler) {
    for (let app of this.possibleApplicationHandlers.enumerate()) {
      if (app.equals(aNewHandler)) {
        return;
      }
    }
    this.possibleApplicationHandlers.appendElement(aNewHandler);
  }

  removePossibleApplicationHandler(aHandler) {
    var defaultApp = this.preferredApplicationHandler;
    if (defaultApp && aHandler.equals(defaultApp)) {
      // If the app we remove was the default app, we must make sure
      // it won't be used anymore
      this.alwaysAskBeforeHandling = true;
      this.preferredApplicationHandler = null;
    }

    var handlers = this.possibleApplicationHandlers;
    for (var i = 0; i < handlers.length; ++i) {
      var handler = handlers.queryElementAt(i, Ci.nsIHandlerApp);
      if (handler.equals(aHandler)) {
        handlers.removeElementAt(i);
        break;
      }
    }
  }

  get hasDefaultHandler() {
    return this.wrappedHandlerInfo.hasDefaultHandler;
  }

  get defaultDescription() {
    return this.wrappedHandlerInfo.defaultDescription;
  }

  // What to do with content of this type.
  get preferredAction() {
    // If the action is to use a helper app, but we don't have a preferred
    // handler app, then switch to using the system default, if any; otherwise
    // fall back to saving to disk, which is the default action in nsMIMEInfo.
    // Note: "save to disk" is an invalid value for protocol info objects,
    // but the alwaysAskBeforeHandling getter will detect that situation
    // and always return true in that case to override this invalid value.
    if (
      this.wrappedHandlerInfo.preferredAction ==
        Ci.nsIHandlerInfo.useHelperApp &&
      !gMainPane.isValidHandlerApp(this.preferredApplicationHandler)
    ) {
      if (this.wrappedHandlerInfo.hasDefaultHandler) {
        return Ci.nsIHandlerInfo.useSystemDefault;
      }
      return Ci.nsIHandlerInfo.saveToDisk;
    }

    return this.wrappedHandlerInfo.preferredAction;
  }

  set preferredAction(aNewValue) {
    this.wrappedHandlerInfo.preferredAction = aNewValue;
  }

  get alwaysAskBeforeHandling() {
    // If this is a protocol type and the preferred action is "save to disk",
    // which is invalid for such types, then return true here to override that
    // action.  This could happen when the preferred action is to use a helper
    // app, but the preferredApplicationHandler is invalid, and there isn't
    // a default handler, so the preferredAction getter returns save to disk
    // instead.
    if (
      !(this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) &&
      this.preferredAction == Ci.nsIHandlerInfo.saveToDisk
    ) {
      return true;
    }

    return this.wrappedHandlerInfo.alwaysAskBeforeHandling;
  }

  set alwaysAskBeforeHandling(aNewValue) {
    this.wrappedHandlerInfo.alwaysAskBeforeHandling = aNewValue;
  }

  // The primary file extension associated with this type, if any.
  get primaryExtension() {
    try {
      if (
        this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo &&
        this.wrappedHandlerInfo.primaryExtension
      ) {
        return this.wrappedHandlerInfo.primaryExtension;
      }
    } catch (ex) {}

    return null;
  }

  store() {
    gHandlerService.store(this.wrappedHandlerInfo);
  }

  get iconSrcSet() {
    let srcset = [];
    for (let scale of [1, 2]) {
      let icon = this._getIcon(16, scale);
      if (!icon) {
        return null;
      }
      srcset.push(`${icon} ${scale}x`);
    }
    return srcset.join(", ");
  }

  _getIcon(aSize, aScale = 1) {
    if (this.primaryExtension) {
      return `moz-icon://goat.${this.primaryExtension}?size=${aSize}&scale=${aScale}`;
    }

    if (this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      return `moz-icon://goat?size=${aSize}&scale=${aScale}&contentType=${this.type}`;
    }

    // FIXME: consider returning some generic icon when we can't get a URL for
    // one (for example in the case of protocol schemes).  Filed as bug 395141.
    return null;
  }
}

/**
 * InternalHandlerInfoWrapper provides a basic mechanism to create an internal
 * mime type handler that can be enabled/disabled in the applications preference
 * menu.
 */
class InternalHandlerInfoWrapper extends HandlerInfoWrapper {
  constructor(mimeType, extension) {
    let type = gMIMEService.getFromTypeAndExtension(mimeType, extension);
    super(mimeType || type.type, type);
  }

  // Override store so we so we can notify any code listening for registration
  // or unregistration of this handler.
  store() {
    super.store();
  }

  get preventInternalViewing() {
    return false;
  }

  get enabled() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }
}

class PDFHandlerInfoWrapper extends InternalHandlerInfoWrapper {
  constructor() {
    super(TYPE_PDF, null);
  }

  get preventInternalViewing() {
    return Services.prefs.getBoolPref(PREF_PDFJS_DISABLED);
  }

  // PDF is always shown in the list, but the 'show internally' option is
  // hidden when the internal PDF viewer is disabled.
  get enabled() {
    return true;
  }
}

class ViewableInternallyHandlerInfoWrapper extends InternalHandlerInfoWrapper {
  get enabled() {
    return DownloadIntegration.shouldViewDownloadInternally(this.type);
  }
}
