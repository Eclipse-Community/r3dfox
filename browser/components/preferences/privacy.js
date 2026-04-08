/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */

const PREF_UPLOAD_ENABLED = "datareporting.healthreport.uploadEnabled";

const TRACKING_PROTECTION_KEY = "websites.trackingProtectionMode";
const TRACKING_PROTECTION_PREFS = [
  "privacy.trackingprotection.enabled",
  "privacy.trackingprotection.pbmode.enabled",
];
const CONTENT_BLOCKING_PREFS = [
  "privacy.trackingprotection.enabled",
  "privacy.trackingprotection.pbmode.enabled",
  "network.cookie.cookieBehavior",
  "privacy.trackingprotection.fingerprinting.enabled",
  "privacy.trackingprotection.cryptomining.enabled",
  "privacy.firstparty.isolate",
  "privacy.trackingprotection.emailtracking.enabled",
  "privacy.trackingprotection.emailtracking.pbmode.enabled",
  "privacy.fingerprintingProtection",
  "privacy.fingerprintingProtection.pbmode",
  "privacy.trackingprotection.allow_list.baseline.enabled",
  "privacy.trackingprotection.allow_list.convenience.enabled",
];

const PREF_OPT_OUT_STUDIES_ENABLED = "app.shield.optoutstudies.enabled";
const PREF_NORMANDY_ENABLED = "app.normandy.enabled";

const PREF_ADDON_RECOMMENDATIONS_ENABLED = "browser.discovery.enabled";

const PREF_PASSWORD_GENERATION_AVAILABLE = "signon.generation.available";
const { BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN } = Ci.nsICookieService;

const PASSWORD_MANAGER_PREF_ID = "services.passwordSavingEnabled";

ChromeUtils.defineLazyGetter(this, "AlertsServiceDND", function () {
  try {
    let alertsService = Cc["@mozilla.org/alerts-service;1"]
      .getService(Ci.nsIAlertsService)
      .QueryInterface(Ci.nsIAlertsDoNotDisturb);
    // This will throw if manualDoNotDisturb isn't implemented.
    alertsService.manualDoNotDisturb;
    return alertsService;
  } catch (ex) {
    return undefined;
  }
});

ChromeUtils.defineLazyGetter(lazy, "AboutLoginsL10n", () => {
  return new Localization(["branding/brand.ftl", "browser/aboutLogins.ftl"]);
});

ChromeUtils.defineLazyGetter(lazy, "gParentalControlsService", () =>
  "@mozilla.org/parental-controls-service;1" in Cc
    ? Cc["@mozilla.org/parental-controls-service;1"].getService(
        Ci.nsIParentalControlsService
      )
    : null
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gIsFirstPartyIsolated",
  "privacy.firstparty.isolate",
  false
);

ChromeUtils.defineESModuleGetters(this, {
  AppUpdater: "resource://gre/modules/AppUpdater.sys.mjs",
  DoHConfigController: "moz-src:///toolkit/components/doh/DoHConfig.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  BANDWIDTH: "chrome://browser/content/ipprotection/ipprotection-constants.mjs",
});

const SANITIZE_ON_SHUTDOWN_MAPPINGS = {
  history: "privacy.clearOnShutdown.history",
  downloads: "privacy.clearOnShutdown.downloads",
  formdata: "privacy.clearOnShutdown.formdata",
  sessions: "privacy.clearOnShutdown.sessions",
  siteSettings: "privacy.clearOnShutdown.siteSettings",
  cookies: "privacy.clearOnShutdown.cookies",
  cache: "privacy.clearOnShutdown.cache",
  offlineApps: "privacy.clearOnShutdown.offlineApps",
};

/*
 * Prefs that are unique to sanitizeOnShutdown and are not shared
 * with the deleteOnClose mechanism like privacy.clearOnShutdown.cookies, -cache and -offlineApps
 */
const SANITIZE_ON_SHUTDOWN_PREFS_ONLY = [
  "privacy.clearOnShutdown.history",
  "privacy.clearOnShutdown.downloads",
  "privacy.clearOnShutdown.sessions",
  "privacy.clearOnShutdown.formdata",
  "privacy.clearOnShutdown.siteSettings",
];

const SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2 = [
  "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
  "privacy.clearOnShutdown_v2.siteSettings",
];

const SECURITY_PRIVACY_STATUS_CARD_ENABLED =
  Services.prefs.getBoolPref("browser.settings-redesign.enabled", false) ||
  Services.prefs.getBoolPref(
    "browser.settings-redesign.securityPrivacyStatus.enabled",
    false
  );

Preferences.addAll([
  // Content blocking / Tracking Protection
  { id: "privacy.trackingprotection.enabled", type: "bool" },
  { id: "privacy.trackingprotection.pbmode.enabled", type: "bool" },
  { id: "privacy.trackingprotection.fingerprinting.enabled", type: "bool" },
  { id: "privacy.trackingprotection.cryptomining.enabled", type: "bool" },
  { id: "privacy.trackingprotection.emailtracking.enabled", type: "bool" },
  {
    id: "privacy.trackingprotection.emailtracking.pbmode.enabled",
    type: "bool",
  },
  {
    id: "privacy.trackingprotection.allow_list.baseline.enabled",
    type: "bool",
  },
  {
    id: "privacy.trackingprotection.allow_list.convenience.enabled",
    type: "bool",
  },

  // Fingerprinting Protection
  { id: "privacy.fingerprintingProtection", type: "bool" },
  { id: "privacy.fingerprintingProtection.pbmode", type: "bool" },

  // Resist Fingerprinting
  { id: "privacy.resistFingerprinting", type: "bool" },
  { id: "privacy.resistFingerprinting.pbmode", type: "bool" },

  // Social tracking
  { id: "privacy.trackingprotection.socialtracking.enabled", type: "bool" },
  { id: "privacy.socialtracking.block_cookies.enabled", type: "bool" },

  // Tracker list
  { id: "urlclassifier.trackingTable", type: "string" },

  // Button prefs
  { id: "pref.privacy.disable_button.cookie_exceptions", type: "bool" },
  {
    id: "pref.privacy.disable_button.tracking_protection_exceptions",
    type: "bool",
  },

  // History
  { id: "places.history.enabled", type: "bool" },
  { id: "browser.formfill.enable", type: "bool" },
  { id: "privacy.history.custom", type: "bool" },

  // Cookies
  { id: "network.cookie.cookieBehavior", type: "int" },
  { id: "network.cookie.blockFutureCookies", type: "bool" },
  // Content blocking category
  { id: "browser.contentblocking.category", type: "string" },
  { id: "browser.contentblocking.features.strict", type: "string" },

  // Clear Private Data
  { id: "privacy.sanitize.sanitizeOnShutdown", type: "bool" },
  { id: "privacy.sanitize.timeSpan", type: "int" },
  { id: "privacy.clearOnShutdown.cookies", type: "bool" },
  { id: "privacy.clearOnShutdown_v2.cookiesAndStorage", type: "bool" },
  { id: "privacy.clearOnShutdown.cache", type: "bool" },
  { id: "privacy.clearOnShutdown_v2.cache", type: "bool" },
  { id: "privacy.clearOnShutdown.offlineApps", type: "bool" },
  { id: "privacy.clearOnShutdown.history", type: "bool" },
  {
    id: "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
    type: "bool",
  },
  { id: "privacy.clearOnShutdown.downloads", type: "bool" },
  { id: "privacy.clearOnShutdown.sessions", type: "bool" },
  { id: "privacy.clearOnShutdown.formdata", type: "bool" },
  { id: "privacy.clearOnShutdown.siteSettings", type: "bool" },
  { id: "privacy.clearOnShutdown_v2.siteSettings", type: "bool" },

  // Do not track and Global Privacy Control
  { id: "privacy.donottrackheader.enabled", type: "bool" },
  { id: "privacy.globalprivacycontrol.functionality.enabled", type: "bool" },
  { id: "privacy.globalprivacycontrol.enabled", type: "bool" },
  {
    id: "browser.preferences.config_warning.donottrackheader.dismissed",
    type: "bool",
  },

  // Firefox VPN
  { id: "browser.ipProtection.enabled", type: "bool" },
  { id: "browser.ipProtection.entitlementCache", type: "string" },
  { id: "browser.ipProtection.features.siteExceptions", type: "bool" },
  { id: "browser.ipProtection.features.autoStart", type: "bool" },
  { id: "browser.ipProtection.autoStartEnabled", type: "bool" },
  { id: "browser.ipProtection.autoStartPrivateEnabled", type: "bool" },
  { id: "browser.ipProtection.bandwidth.enabled", type: "bool" },
  { id: "browser.ipProtection.usageCache", type: "string" },

  // Media
  { id: "media.autoplay.default", type: "int" },

  // Popups
  { id: "dom.disable_open_during_load", type: "bool" },

  // Passwords
  { id: "signon.rememberSignons", type: "bool" },
  { id: "signon.generation.enabled", type: "bool" },
  { id: "signon.autofillForms", type: "bool" },
  { id: "signon.management.page.breach-alerts.enabled", type: "bool" },
  { id: "signon.firefoxRelay.feature", type: "string" },

  // Buttons
  { id: "pref.privacy.disable_button.view_passwords", type: "bool" },
  { id: "pref.privacy.disable_button.view_passwords_exceptions", type: "bool" },

  /* Certificates tab
   * security.default_personal_cert
   *   - a string:
   *       "Select Automatically"   select a certificate automatically when a site
   *                                requests one
   *       "Ask Every Time"         present a dialog to the user so he can select
   *                                the certificate to use on a site which
   *                                requests one
   */
  { id: "security.default_personal_cert", type: "string" },

  { id: "security.disable_button.openCertManager", type: "bool" },

  { id: "security.disable_button.openDeviceManager", type: "bool" },

  { id: "security.enterprise_roots.enabled", type: "bool" },

  // Add-ons, malware, phishing
  { id: "xpinstall.whitelist.required", type: "bool" },

  { id: "browser.safebrowsing.malware.enabled", type: "bool" },
  { id: "browser.safebrowsing.phishing.enabled", type: "bool" },

  { id: "browser.safebrowsing.downloads.enabled", type: "bool" },

  { id: "urlclassifier.malwareTable", type: "string" },

  {
    id: "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
    type: "bool",
  },
  { id: "browser.safebrowsing.downloads.remote.block_uncommon", type: "bool" },

  // First-Party Isolation
  { id: "privacy.firstparty.isolate", type: "bool" },

  // HTTPS-Only
  { id: "dom.security.https_only_mode", type: "bool" },
  { id: "dom.security.https_only_mode_pbm", type: "bool" },
  { id: "dom.security.https_first", type: "bool" },
  { id: "dom.security.https_first_pbm", type: "bool" },

  // Windows SSO
  { id: "network.http.windows-sso.enabled", type: "bool" },

  // Cookie Banner Handling
  { id: "cookiebanners.ui.desktop.enabled", type: "bool" },
  { id: "cookiebanners.service.mode.privateBrowsing", type: "int" },

  // DoH
  { id: "network.trr.mode", type: "int" },
  { id: "network.trr.uri", type: "string" },
  { id: "network.trr.default_provider_uri", type: "string" },
  { id: "network.trr.custom_uri", type: "string" },
  { id: "network.trr_ui.fallback_was_checked", type: "bool" },
  { id: "doh-rollout.disable-heuristics", type: "bool" },

  // Local Network Access
  { id: "network.lna.blocking", type: "bool" },

  // Permissions
  { id: "media.setsinkid.enabled", type: "bool" },

  // Security and Privacy Warnings
  { id: "browser.preferences.config_warning.dismissAll", type: "bool" },
  {
    id: "browser.preferences.config_warning.warningSafeBrowsing.dismissed",
    type: "bool",
  },
]);

if (SECURITY_PRIVACY_STATUS_CARD_ENABLED) {
  Preferences.addAll([
    // Security and Privacy Warnings
    { id: "privacy.ui.status_card.testing.show_issue", type: "bool" },
    {
      id: "browser.preferences.config_warning.warningTest.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningAllowFingerprinters.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningThirdPartyCookies.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningPasswordManager.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningPopupBlocker.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningExtensionInstall.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningDoH.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningECH.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningProxyAutodetection.dismissed",
      type: "bool",
    },
    {
      id: "services.passwordSavingEnabled",
      type: "bool",
    },
    {
      id: "network.dns.echconfig.enabled",
      type: "bool",
    },
    {
      id: "network.dns.http3_echconfig.enabled",
      type: "bool",
    },
    {
      id: "network.proxy.type",
      type: "int",
    },
  ]);

  Preferences.addSetting({
    id: "etpStrictEnabled",
    pref: "browser.contentblocking.category",
    get: prefValue => prefValue == "strict",
  });
  Preferences.addSetting({
    id: "etpCustomEnabled",
    pref: "browser.contentblocking.category",
    get: prefValue => prefValue == "custom",
  });
  Preferences.addSetting(
    /** @type {{ cachedValue: number, loadTrackerCount: (emitChange: SettingEmitChange) => Promise<void> } & SettingConfig} */ ({
      id: "trackerCount",
      cachedValue: null,
      async loadTrackerCount(emitChange) {
        const now = Date.now();
        const aMonthAgo = new Date(now - 30 * 24 * 60 * 60 * 1000);
        /** @type {{ getResultByName: (_: string) => number }[]} */
        const events = await lazy.TrackingDBService.getEventsByDateRange(
          now,
          aMonthAgo
        );

        const total = events.reduce((acc, day) => {
          return acc + day.getResultByName("count");
        }, 0);
        this.cachedValue = total;
        emitChange();
      },
      setup(emitChange) {
        this.loadTrackerCount(emitChange);
      },
      get() {
        return this.cachedValue;
      },
    })
  );
  Preferences.addSetting(
    /** @type {{ cachedValue: any } & SettingConfig} */ ({
      id: "appUpdateStatus",
      cachedValue: AppUpdater.STATUS.NO_UPDATER,
      setup(emitChange) {
        if (AppConstants.MOZ_UPDATER && !gIsPackagedApp) {
          let appUpdater = new AppUpdater();
          /**
           * @param {number} status
           * @param {any[]} _args
           */
          let listener = (status, ..._args) => {
            this.cachedValue = status;
            emitChange();
          };
          appUpdater.addListener(listener);
          appUpdater.check();
          return () => {
            appUpdater.removeListener(listener);
            appUpdater.stop();
          };
        }
        return () => {};
      },
      get() {
        return this.cachedValue;
      },
      set(value) {
        this.cachedValue = value;
      },
    })
  );
}

Preferences.addSetting({
  id: "savePasswords",
  pref: "signon.rememberSignons",
  controllingExtensionInfo: {
    storeId: "services.passwordSavingEnabled",
    l10nId: "extension-controlling-password-saving",
  },
});

Preferences.addSetting({
  id: "managePasswordExceptions",
  onUserClick: () => {
    gPrivacyPane.showPasswordExceptions();
  },
});

Preferences.addSetting({
  id: "fillUsernameAndPasswords",
  pref: "signon.autofillForms",
});

Preferences.addSetting({
  id: "suggestStrongPasswords",
  pref: "signon.generation.enabled",
  visible: () => Services.prefs.getBoolPref("signon.generation.available"),
});

Preferences.addSetting({
  id: "requireOSAuthForPasswords",
  visible: () => OSKeyStore.canReauth(),
  get: () => LoginHelper.getOSAuthEnabled(),
  async set(checked) {
    const [messageText, captionText] = await Promise.all([
      lazy.AboutLoginsL10n.formatValue("about-logins-os-auth-dialog-message"),
      lazy.AboutLoginsL10n.formatValue("about-logins-os-auth-dialog-caption"),
    ]);

    await LoginHelper.trySetOSAuthEnabled(
      window,
      checked,
      messageText,
      captionText
    );

    // Trigger change event to keep checkbox UI in sync with pref value
    Services.obs.notifyObservers(null, "PasswordsOSAuthEnabledChange");
  },
  setup: emitChange => {
    Services.obs.addObserver(emitChange, "PasswordsOSAuthEnabledChange");
    return () =>
      Services.obs.removeObserver(emitChange, "PasswordsOSAuthEnabledChange");
  },
});

Preferences.addSetting({
  id: "allowWindowSSO",
  pref: "network.http.windows-sso.enabled",
  visible: () => AppConstants.platform === "win",
});

Preferences.addSetting({
  id: "manageSavedPasswords",
  onUserClick: ({ target }) => {
    target.ownerGlobal.gPrivacyPane.showPasswords();
  },
});

Preferences.addSetting({
  id: "additionalProtectionsGroup",
});

Preferences.addSetting({
  id: "primaryPasswordNotSet",
  setup(emitChange) {
    const topic = "passwordmgr-primary-pw-changed";
    Services.obs.addObserver(emitChange, topic);
    return () => Services.obs.removeObserver(emitChange, topic);
  },
  visible: () => {
    return !LoginHelper.isPrimaryPasswordSet();
  },
});

Preferences.addSetting({
  id: "usePrimaryPassword",
  deps: ["primaryPasswordNotSet"],
});

Preferences.addSetting({
  id: "addPrimaryPassword",
  deps: ["primaryPasswordNotSet"],
  onUserClick: ({ target }) => {
    target.ownerGlobal.gPrivacyPane.changeMasterPassword();
  },
  disabled: () => {
    return !Services.policies.isAllowed("createMasterPassword");
  },
});

Preferences.addSetting({
  id: "primaryPasswordSet",
  setup(emitChange) {
    const topic = "passwordmgr-primary-pw-changed";
    Services.obs.addObserver(emitChange, topic);
    return () => Services.obs.removeObserver(emitChange, topic);
  },
  visible: () => {
    return LoginHelper.isPrimaryPasswordSet();
  },
});

Preferences.addSetting({
  id: "statusPrimaryPassword",
  deps: ["primaryPasswordSet"],
  onUserClick: e => {
    if (e.target.localName == "moz-button") {
      e.target.ownerGlobal.gPrivacyPane._removeMasterPassword();
    }
  },
  getControlConfig(config) {
    config.options[0].controlAttrs = {
      ...config.options[0].controlAttrs,
      ...(!Services.policies.isAllowed("removeMasterPassword")
        ? { disabled: "" }
        : {}),
    };
    return config;
  },
});

Preferences.addSetting({
  id: "changePrimaryPassword",
  deps: ["primaryPasswordSet"],
  onUserClick: ({ target }) => {
    target.ownerGlobal.gPrivacyPane.changeMasterPassword();
  },
});

Preferences.addSetting({
  id: "breachAlerts",
  pref: "signon.management.page.breach-alerts.enabled",
});

/**
 * This class is used to create Settings that are used to warn the user about
 * potential misconfigurations. It should be passed into Preferences.addSetting
 * to create the Preference for a <moz-box-item> because it creates
 * separate members on pref.config
 *
 * @implements {SettingConfig}
 */
class WarningSettingConfig {
  /**
   * This callback type specifies the most important part of a WarningSettingConfig: how to know
   * when to warn.
   *
   * @callback problematicCallback
   * @param {WarningSettingConfig} self - this is a Setting config created by the constructor below,
   * that has been `setup` and not yet cleaned up. Its prefMapping is setup into its properties.
   * @returns {boolean} Should this Setting show a warning to the user if not yet dismissed?
   */

  /**
   *
   * @param {string} id - The unique setting ID for the setting created by this config
   * @param {{[key: string]: string}} prefMapping - A map from member name (to be used in the
   * `problematic` arg's arg) to pref string, containing all of the preferences this Setting
   * relies upon. On setup, this object will create properties for each entry here, where the
   * value is the result of Preferences.get(key).
   * @param {problematicCallback} problematic - How we determine whether or not to show this
   * setting initially
   * @param {boolean} isDismissable - A boolean indicating whether or not we should support dismissing
   * this setting
   */
  constructor(id, prefMapping, problematic, isDismissable) {
    this.id = id;
    this.prefMapping = prefMapping;
    if (isDismissable) {
      this.dismissedPrefId = `browser.preferences.config_warning.${this.id}.dismissed`;
      this.prefMapping.dismissed = this.dismissedPrefId;
      this.dismissAllPrefId = `browser.preferences.config_warning.dismissAll`;
      this.prefMapping.dismissAll = this.dismissAllPrefId;
    }
    this.problematic = problematic;
  }

  /**
   * This item in a warning moz-box-group should be visible if the `problematic` argument
   * from the constructor says we should, and it isn't hidden.
   *
   * @returns {boolean} Whether or not to show this configuration as a warning to the user
   */
  visible() {
    return (
      !this.dismissAll?.value &&
      !this.dismissed?.value &&
      this.problematic(this)
    );
  }

  /**
   * This resets all of the preferernces in the `prefMapping` from the constructor that have
   * user-specified values. This includes the dismiss pref as well.
   */
  reset() {
    for (let getter of Object.keys(this.prefMapping)) {
      if (this[getter].hasUserValue) {
        this[getter].reset();
      }
    }
  }

  /**
   * When invoked, this sets a pref that persistently hides this setting. See visible().
   */
  dismiss() {
    if (this.dismissed) {
      this.dismissed.value = true;
    }
  }

  /**
   * This initializes the Setting created with this config, starting listeners for all dependent
   * Preferences and providing a cleanup callback to remove them
   *
   * @param {() => any} emitChange - a callback to be invoked any time that the Setting created
   * with this config is changed
   * @returns {() => any} a function that cleans up the state from this Setting, namely pref change listeners.
   */
  setup(emitChange) {
    for (let [getter, prefId] of Object.entries(this.prefMapping)) {
      this[getter] = Preferences.get(prefId);
      this[getter].on("change", emitChange);
    }
    return () => {
      for (let getter of Object.keys(this.prefMapping)) {
        this[getter].off(emitChange);
      }
    };
  }

  /**
   * Setting helper to handle clicks of our warning. They may be a "reset" or
   * "dismiss" action depending on the target, and those callbacks are defined
   * in this class.
   *
   * @param {PointerEvent} event - The event for the user click
   */
  onUserClick(event) {
    switch (event.target.id) {
      case "reset": {
        this.reset();
        Glean.securityPreferencesWarnings.warningFixed.record();
        break;
      }
      case "dismiss": {
        this.dismiss();
        Glean.securityPreferencesWarnings.warningDismissed.record();
        break;
      }
    }
  }
}

if (SECURITY_PRIVACY_STATUS_CARD_ENABLED) {
  Preferences.addSetting(
    new WarningSettingConfig(
      "warningTest",
      {
        showIssue: "privacy.ui.status_card.testing.show_issue",
      },
      ({ showIssue }) => showIssue.hasUserValue && !showIssue.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningAllowFingerprinters",
      {
        fingerprintingEnabled:
          "privacy.trackingprotection.fingerprinting.enabled",
      },
      ({ fingerprintingEnabled }) =>
        !fingerprintingEnabled.value && !fingerprintingEnabled.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningThirdPartyCookies",
      {
        cookieBehavior: "network.cookie.cookieBehavior",
      },
      ({ cookieBehavior }) =>
        (cookieBehavior.value == 0 ||
          cookieBehavior.value == 3 ||
          cookieBehavior.value == 4) &&
        !cookieBehavior.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningPasswordManager",
      {
        enabled: "signon.rememberSignons",
        extentionAllows: "services.passwordSavingEnabled",
      },
      ({ enabled, extentionAllows }) =>
        !enabled.value && !enabled.locked && !extentionAllows.value,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningPopupBlocker",
      {
        enabled: "dom.disable_open_during_load",
      },
      ({ enabled }) => !enabled.value && !enabled.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningExtensionInstall",
      {
        blockInstalls: "xpinstall.whitelist.required",
      },
      ({ blockInstalls }) => !blockInstalls.value && !blockInstalls.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningDoH",
      {
        dohMode: "network.trr.mode",
      },
      ({ dohMode }) => dohMode.value == 5 && !dohMode.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningECH",
      {
        echEnabled: "network.dns.echconfig.enabled",
        https3echEnabled: "network.dns.http3_echconfig.enabled",
      },
      ({ echEnabled, https3echEnabled }) =>
        (!echEnabled.value && !echEnabled.locked) ||
        (!https3echEnabled.value && !https3echEnabled.locked),
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningProxyAutodetection",
      {
        proxyType: "network.proxy.type",
      },
      ({ proxyType }) => proxyType.value == 2 && !proxyType.locked,
      true
    )
  );

  /** @type {SettingControlConfig[]} */
  const SECURITY_WARNINGS = [
    {
      l10nId: "security-privacy-issue-warning-test",
      id: "warningTest",
    },
    {
      l10nId: "security-privacy-issue-warning-fingerprinters",
      id: "warningAllowFingerprinters",
    },
    {
      l10nId: "security-privacy-issue-warning-third-party-cookies",
      id: "warningThirdPartyCookies",
    },
    {
      l10nId: "security-privacy-issue-warning-password-manager",
      id: "warningPasswordManager",
    },
    {
      l10nId: "security-privacy-issue-warning-popup-blocker",
      id: "warningPopupBlocker",
    },
    {
      l10nId: "security-privacy-issue-warning-extension-install",
      id: "warningExtensionInstall",
    },
    {
      l10nId: "security-privacy-issue-warning-safe-browsing",
      id: "warningSafeBrowsing",
    },
    {
      l10nId: "security-privacy-issue-warning-doh",
      id: "warningDoH",
    },
    {
      l10nId: "security-privacy-issue-warning-ech",
      id: "warningECH",
    },

    {
      l10nId: "security-privacy-issue-warning-proxy-autodetection",
      id: "warningProxyAutodetection",
    },
  ];

  Preferences.addSetting(
    /** @type {{ makeSecurityWarningItems: () => SettingControlConfig[] } & SettingConfig} */ ({
      id: "securityWarningsGroup",
      makeSecurityWarningItems() {
        return SECURITY_WARNINGS.map(({ id, l10nId }) => ({
          id,
          l10nId,
          control: "moz-box-item",
          options: [
            {
              control: "moz-button",
              l10nId: "issue-card-reset-button",
              controlAttrs: { slot: "actions", size: "small", id: "reset" },
            },
            {
              control: "moz-button",
              l10nId: "issue-card-dismiss-button",
              controlAttrs: {
                slot: "actions",
                size: "small",
                iconsrc: "chrome://global/skin/icons/close.svg",
                id: "dismiss",
              },
            },
          ],
        }));
      },
      getControlConfig(config) {
        if (!config.items) {
          return { ...config, items: this.makeSecurityWarningItems() };
        }
        return config;
      },
    })
  );

  Preferences.addSetting({
    id: "privacyCard",
    deps: [
      "appUpdateStatus",
      "trackerCount",
      "etpStrictEnabled",
      "etpCustomEnabled",
      ...SECURITY_WARNINGS.map(warning => warning.id),
    ],
  });

  Preferences.addSetting({
    id: "warningCard",
    deps: SECURITY_WARNINGS.map(warning => warning.id),
    visible: deps => {
      const count = Object.values(deps).filter(
        depSetting => depSetting.visible
      ).length;
      if (!this._telemetrySent) {
        Glean.securityPreferencesWarnings.warningsShown.record({ count });
        this._telemetrySent = true;
      }
      return count > 0;
    },
  });
}

Preferences.addSetting({
  id: "ipProtectionVisible",
  pref: "browser.ipProtection.enabled",
});
Preferences.addSetting({
  id: "ipProtectionNotOptedIn",
  pref: "browser.ipProtection.entitlementCache",
  get: prefVal => !prefVal,
});
Preferences.addSetting({
  id: "ipProtectionNotOptedInSection",
  deps: ["ipProtectionVisible", "ipProtectionNotOptedIn"],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "getStartedButton",
  deps: ["ipProtectionVisible", "ipProtectionNotOptedIn"],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && ipProtectionNotOptedIn.value,
  onUserClick() {
    IPProtection.getPanel(window.browsingContext.topChromeWindow)?.enroll({
      entrypoint: "vpn_integration_settings",
      utm_source: "settings",
    });
  },
});

Preferences.addSetting({
  id: "ipProtectionSiteExceptionsFeatureEnabled",
  pref: "browser.ipProtection.features.siteExceptions",
});
Preferences.addSetting({
  id: "ipProtectionExceptions",
  deps: [
    "ipProtectionVisible",
    "ipProtectionSiteExceptionsFeatureEnabled",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionSiteExceptionsFeatureEnabled,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionSiteExceptionsFeatureEnabled.value &&
    !ipProtectionNotOptedIn.value,
});

Preferences.addSetting({
  id: "ipProtectionExceptionAllListButton",
  deps: [
    "ipProtectionVisible",
    "ipProtectionSiteExceptionsFeatureEnabled",
    "ipProtectionNotOptedIn",
  ],
  setup(emitChange) {
    let permObserver = {
      observe(subject, topic, _data) {
        if (subject && topic === "perm-changed") {
          let permission = subject.QueryInterface(Ci.nsIPermission);
          if (permission.type === "ipp-vpn") {
            emitChange();
          }
        }
      },
    };
    Services.obs.addObserver(permObserver, "perm-changed");
    return () => {
      Services.obs.removeObserver(permObserver, "perm-changed");
    };
  },
  visible: ({
    ipProtectionVisible,
    ipProtectionSiteExceptionsFeatureEnabled,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionSiteExceptionsFeatureEnabled.value &&
    !ipProtectionNotOptedIn.value,
  onUserClick() {
    let params = {
      addVisible: true,
      hideStatusColumn: true,
      prefilledHost: "",
      permissionType: "ipp-vpn",
      capabilityFilter: Ci.nsIPermissionManager.DENY_ACTION,
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },
  getControlConfig(config) {
    let l10nId = "ip-protection-site-exceptions-all-sites-button";

    let savedExceptions = Services.perms.getAllByTypes(["ipp-vpn"]);
    let numberOfExclusions = savedExceptions.filter(
      perm => perm.capability === Ci.nsIPermissionManager.DENY_ACTION
    ).length;

    let l10nArgs = {
      count: numberOfExclusions,
    };

    return {
      ...config,
      l10nId,
      l10nArgs,
    };
  },
});
Preferences.addSetting({
  id: "ipProtectionAutoStartFeatureEnabled",
  pref: "browser.ipProtection.features.autoStart",
  get: prefVal => prefVal,
});
Preferences.addSetting({
  id: "ipProtectionAutoStart",
  deps: [
    "ipProtectionVisible",
    "ipProtectionAutoStartFeatureEnabled",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionAutoStartFeatureEnabled,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionAutoStartFeatureEnabled.value &&
    !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionAutoStartCheckbox",
  pref: "browser.ipProtection.autoStartEnabled",
  deps: [
    "ipProtectionVisible",
    "ipProtectionAutoStart",
    "ipProtectionNotOptedIn",
  ],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionAutoStartPrivateCheckbox",
  pref: "browser.ipProtection.autoStartPrivateEnabled",
  deps: [
    "ipProtectionVisible",
    "ipProtectionAutoStart",
    "ipProtectionNotOptedIn",
  ],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionBandwidthVisible",
  deps: ["ipProtectionVisible"],
  pref: "browser.ipProtection.bandwidth.enabled",
});
Preferences.addSetting({
  id: "ipProtectionBandwidthSection",
  deps: [
    "ipProtectionVisible",
    "ipProtectionBandwidthVisible",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionBandwidthVisible,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionBandwidthVisible.value &&
    !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionBandwidth",
  deps: [
    "ipProtectionVisible",
    "ipProtectionBandwidthVisible",
    "ipProtectionBandwidthSection",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionBandwidthVisible,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionBandwidthVisible.value &&
    !ipProtectionNotOptedIn.value,
  pref: "browser.ipProtection.usageCache",
  getControlConfig: config => {
    const usagePref = Services.prefs.getStringPref(
      "browser.ipProtection.usageCache",
      ""
    );
    let usage;
    if (usagePref) {
      usage = JSON.parse(usagePref);
    } else {
      usage = {
        max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
        remaining: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
      };
    }

    return {
      ...config,
      controlAttrs: usage,
    };
  },
});
Preferences.addSetting({
  id: "ipProtectionLinks",
  deps: ["ipProtectionVisible", "ipProtectionNotOptedIn"],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && !ipProtectionNotOptedIn.value,
});

// Study opt out
if (AppConstants.MOZ_DATA_REPORTING) {
  Preferences.addAll([
    // Preference instances for prefs that we need to monitor while the page is open.
    { id: PREF_OPT_OUT_STUDIES_ENABLED, type: "bool" },
    { id: PREF_ADDON_RECOMMENDATIONS_ENABLED, type: "bool" },
    { id: PREF_UPLOAD_ENABLED, type: "bool" },
    { id: "datareporting.usage.uploadEnabled", type: "bool" },
    { id: "dom.private-attribution.submission.enabled", type: "bool" },
  ]);
}
// Privacy segmentation section
Preferences.add({
  id: "browser.dataFeatureRecommendations.enabled",
  type: "bool",
});

// Data Choices tab
if (AppConstants.MOZ_CRASHREPORTER) {
  Preferences.add({
    id: "browser.crashReports.unsubmittedCheck.autoSubmit2",
    type: "bool",
  });
}

Preferences.addSetting({
  id: "gpcFunctionalityEnabled",
  pref: "privacy.globalprivacycontrol.functionality.enabled",
});
Preferences.addSetting({
  id: "gpcEnabled",
  pref: "privacy.globalprivacycontrol.enabled",
  deps: ["gpcFunctionalityEnabled"],
  visible: ({ gpcFunctionalityEnabled }) => {
    return gpcFunctionalityEnabled.value;
  },
});
Preferences.addSetting({
  id: "relayFeature",
  pref: "signon.firefoxRelay.feature",
});
Preferences.addSetting({
  id: "relayIntegration",
  deps: ["savePasswords", "relayFeature"],
  visible: () => {
    return FirefoxRelay.isAvailable;
  },
  disabled: ({ savePasswords, relayFeature }) => {
    return !savePasswords.value || relayFeature.pref.locked;
  },
  get() {
    return FirefoxRelay.isAvailable && !FirefoxRelay.isDisabled;
  },
  set(checked) {
    if (checked) {
      FirefoxRelay.markAsAvailable();
    } else {
      FirefoxRelay.markAsDisabled();
    }
  },
  onUserChange(checked) {
    if (checked) {
      Glean.relayIntegration.enabledPrefChange.record();
    } else {
      Glean.relayIntegration.disabledPrefChange.record();
    }
  },
});
Preferences.addSetting({
  id: "dntHeaderEnabled",
  pref: "privacy.donottrackheader.enabled",
});
Preferences.addSetting({
  id: "dntRemoval",
  pref: "browser.preferences.config_warning.donottrackheader.dismissed",
  deps: ["dntHeaderEnabled"],
  visible: ({ dntHeaderEnabled }, setting) => {
    return dntHeaderEnabled.value && !setting.value;
  },
  onUserClick: (event, _deps, setting) => {
    let dismissButton = event.target?.shadowRoot?.querySelector(".close");
    if (
      dismissButton?.shadowRoot &&
      event.originalTarget &&
      dismissButton.shadowRoot.contains(event.originalTarget)
    ) {
      setting.value = true;
    }
  },
});

Preferences.addSetting({
  id: "httpsOnlyEnabled",
  pref: "dom.security.https_only_mode",
});
Preferences.addSetting({
  id: "httpsOnlyEnabledPBM",
  pref: "dom.security.https_only_mode_pbm",
});
Preferences.addSetting({
  id: "httpsOnlyRadioGroup",
  deps: ["httpsOnlyEnabled", "httpsOnlyEnabledPBM"],
  get: (_value, deps) => {
    if (deps.httpsOnlyEnabled.value) {
      return "enabled";
    }
    if (deps.httpsOnlyEnabledPBM.value) {
      return "privateOnly";
    }
    return "disabled";
  },
  set: (value, deps) => {
    if (value == "enabled") {
      deps.httpsOnlyEnabled.value = true;
      deps.httpsOnlyEnabledPBM.value = false;
    } else if (value == "privateOnly") {
      deps.httpsOnlyEnabled.value = false;
      deps.httpsOnlyEnabledPBM.value = true;
    } else if (value == "disabled") {
      deps.httpsOnlyEnabled.value = false;
      deps.httpsOnlyEnabledPBM.value = false;
    }
  },
  disabled: deps => {
    return deps.httpsOnlyEnabled.locked || deps.httpsOnlyEnabledPBM.locked;
  },
});
Preferences.addSetting({
  id: "httpsFirstEnabled",
  pref: "dom.security.https_first",
});
Preferences.addSetting({
  id: "httpsFirstEnabledPBM",
  pref: "dom.security.https_first_pbm",
});
Preferences.addSetting({
  id: "httpsOnlyExceptionButton",
  deps: [
    "httpsOnlyEnabled",
    "httpsOnlyEnabledPBM",
    "httpsFirstEnabled",
    "httpsFirstEnabledPBM",
  ],
  disabled: deps => {
    return (
      !deps.httpsOnlyEnabled.value &&
      !deps.httpsOnlyEnabledPBM.value &&
      !deps.httpsFirstEnabled.value &&
      !deps.httpsFirstEnabledPBM.value
    );
  },
  onUserClick: () => {
    gPrivacyPane.showHttpsOnlyModeExceptions();
  },
});

Preferences.addSetting({
  id: "enableSafeBrowsingPhishing",
  pref: "browser.safebrowsing.phishing.enabled",
});
Preferences.addSetting({
  id: "enableSafeBrowsingMalware",
  pref: "browser.safebrowsing.malware.enabled",
});
Preferences.addSetting({
  id: "enableSafeBrowsing",
  deps: ["enableSafeBrowsingPhishing", "enableSafeBrowsingMalware"],
  get: (_value, deps) => {
    return (
      deps.enableSafeBrowsingPhishing.value &&
      deps.enableSafeBrowsingMalware.value
    );
  },
  set: (value, deps) => {
    deps.enableSafeBrowsingPhishing.value = value;
    deps.enableSafeBrowsingMalware.value = value;
  },
  disabled: deps => {
    return (
      deps.enableSafeBrowsingPhishing.locked ||
      deps.enableSafeBrowsingMalware.locked
    );
  },
});
Preferences.addSetting(
  new WarningSettingConfig(
    "warningSafeBrowsing",
    {
      malware: "browser.safebrowsing.malware.enabled",
      phishing: "browser.safebrowsing.phishing.enabled",
      downloads: "browser.safebrowsing.downloads.enabled",
      unwantedDownloads:
        "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
      uncommonDownloads:
        "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
    },
    ({ malware, phishing, downloads, unwantedDownloads, uncommonDownloads }) =>
      (!malware.value && !malware.locked) ||
      (!phishing.value && !phishing.locked) ||
      (!downloads.value && !downloads.locked) ||
      (!unwantedDownloads.value && !unwantedDownloads.locked) ||
      (!uncommonDownloads.value && !uncommonDownloads.locked),
    true
  )
);
Preferences.addSetting({
  id: "safeBrowsingWarningMessageBox",
  deps: ["warningSafeBrowsing"],
  visible({ warningSafeBrowsing }) {
    return warningSafeBrowsing.visible;
  },
  onMessageBarDismiss(_, { warningSafeBrowsing }) {
    warningSafeBrowsing.config.dismiss();
  },
});
Preferences.addSetting({
  id: "blockDownloads",
  pref: "browser.safebrowsing.downloads.enabled",
  deps: ["enableSafeBrowsing"],
  disabled: (deps, self) => {
    return !deps.enableSafeBrowsing.value || self.locked;
  },
});
Preferences.addSetting({
  id: "malwareTable",
  pref: "urlclassifier.malwareTable",
});
Preferences.addSetting({
  id: "blockUncommonDownloads",
  pref: "browser.safebrowsing.downloads.remote.block_uncommon",
});
Preferences.addSetting({
  id: "blockUnwantedDownloads",
  pref: "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
});
Preferences.addSetting({
  id: "blockUncommonUnwanted",
  deps: [
    "enableSafeBrowsing",
    "blockDownloads",
    "blockUncommonDownloads",
    "blockUnwantedDownloads",
  ],
  get: (_value, deps) => {
    return (
      deps.blockUncommonDownloads.value && deps.blockUnwantedDownloads.value
    );
  },
  set: (value, deps) => {
    deps.blockUncommonDownloads.value = value;
    deps.blockUnwantedDownloads.value = value;

    let malwareTable = Preferences.get("urlclassifier.malwareTable");
    let malware = /** @type {string} */ (malwareTable.value)
      .split(",")
      .filter(
        x =>
          x !== "goog-unwanted-proto" &&
          x !== "goog-unwanted-shavar" &&
          x !== "moztest-unwanted-simple"
      );

    if (value) {
      if (malware.includes("goog-malware-shavar")) {
        malware.push("goog-unwanted-shavar");
      } else {
        malware.push("goog-unwanted-proto");
      }
      malware.push("moztest-unwanted-simple");
    }

    // sort alphabetically to keep the pref consistent
    malware.sort();
    malwareTable.value = malware.join(",");

    // Force an update after changing the malware table.
    listManager.forceUpdates(malwareTable.value);
  },
  disabled: deps => {
    return (
      !deps.enableSafeBrowsing.value ||
      !deps.blockDownloads.value ||
      deps.blockUncommonDownloads.locked ||
      deps.blockUnwantedDownloads.locked
    );
  },
});
Preferences.addSetting({
  id: "manageDataSettingsGroup",
});
Preferences.addSetting(
  /** @type {{ isUpdatingSites: boolean, usage: { value: number, unit: string } | void } & SettingConfig} */ ({
    id: "siteDataSize",
    usage: null,
    isUpdatingSites: false,
    setup(emitChange) {
      let onUsageChanged = async () => {
        let [siteDataUsage, cacheUsage] = await Promise.all([
          SiteDataManager.getTotalUsage(),
          SiteDataManager.getCacheSize(),
        ]);
        let totalUsage = siteDataUsage + cacheUsage;
        let [value, unit] = DownloadUtils.convertByteUnits(totalUsage);
        this.usage = { value, unit };

        this.isUpdatingSites = false;
        emitChange();
      };

      let onUpdatingSites = () => {
        this.isUpdatingSites = true;
        emitChange();
      };

      Services.obs.addObserver(onUsageChanged, "sitedatamanager:sites-updated");
      Services.obs.addObserver(
        onUpdatingSites,
        "sitedatamanager:updating-sites"
      );

      return () => {
        Services.obs.removeObserver(
          onUsageChanged,
          "sitedatamanager:sites-updated"
        );
        Services.obs.removeObserver(
          onUpdatingSites,
          "sitedatamanager:updating-sites"
        );
      };
    },
    getControlConfig(config) {
      if (this.isUpdatingSites || !this.usage) {
        // Data not retrieved yet, show a loading state.
        return {
          ...config,
          l10nId: "sitedata-total-size-calculating",
        };
      }

      let { value, unit } = this.usage;
      return {
        ...config,
        l10nId: "sitedata-total-size3",
        l10nArgs: {
          value,
          unit,
        },
      };
    },
  })
);

Preferences.addSetting({
  id: "deleteOnCloseInfo",
  deps: ["privateBrowsingAutoStart"],
  visible({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting(
  /** @type {{ isUpdatingSites: boolean } & SettingConfig} */ ({
    id: "clearSiteDataButton",
    isUpdatingSites: false,
    setup(emitChange) {
      let onSitesUpdated = async () => {
        this.isUpdatingSites = false;
        emitChange();
      };

      let onUpdatingSites = () => {
        this.isUpdatingSites = true;
        emitChange();
      };

      Services.obs.addObserver(onSitesUpdated, "sitedatamanager:sites-updated");
      Services.obs.addObserver(
        onUpdatingSites,
        "sitedatamanager:updating-sites"
      );

      return () => {
        Services.obs.removeObserver(
          onSitesUpdated,
          "sitedatamanager:sites-updated"
        );
        Services.obs.removeObserver(
          onUpdatingSites,
          "sitedatamanager:updating-sites"
        );
      };
    },
    onUserClick() {
      gSubDialog.open(
        "chrome://browser/content/sanitize_v2.xhtml",
        {
          features: "resizable=no",
        },
        {
          mode: "clearSiteData",
        }
      );
    },
    disabled() {
      return this.isUpdatingSites;
    },
  })
);
Preferences.addSetting(
  /** @type {{ isUpdatingSites: boolean } & SettingConfig} */ ({
    id: "siteDataSettings",
    isUpdatingSites: false,
    setup(emitChange) {
      let onSitesUpdated = async () => {
        this.isUpdatingSites = false;
        emitChange();
      };

      let onUpdatingSites = () => {
        this.isUpdatingSites = true;
        emitChange();
      };

      Services.obs.addObserver(onSitesUpdated, "sitedatamanager:sites-updated");
      Services.obs.addObserver(
        onUpdatingSites,
        "sitedatamanager:updating-sites"
      );

      return () => {
        Services.obs.removeObserver(
          onSitesUpdated,
          "sitedatamanager:sites-updated"
        );
        Services.obs.removeObserver(
          onUpdatingSites,
          "sitedatamanager:updating-sites"
        );
      };
    },
    onUserClick() {
      gSubDialog.open(
        "chrome://browser/content/preferences/dialogs/siteDataSettings.xhtml"
      );
    },
    disabled() {
      return this.isUpdatingSites;
    },
  })
);
Preferences.addSetting({
  id: "cookieExceptions",
  onUserClick() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      {},
      {
        blockVisible: true,
        sessionVisible: true,
        allowVisible: true,
        prefilledHost: "",
        permissionType: "cookie",
      }
    );
  },
});

function isCookiesAndStorageClearingOnShutdown() {
  return (
    Preferences.get("privacy.sanitize.sanitizeOnShutdown").value &&
    Preferences.get("privacy.clearOnShutdown_v2.cookiesAndStorage").value &&
    Preferences.get("privacy.clearOnShutdown_v2.cache").value
  );
}

/*
 * Unsets cleaning prefs that do not belong to DeleteOnClose
 */
function resetCleaningPrefs() {
  return SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2.forEach(
    pref => (Preferences.get(pref).value = false)
  );
}

Preferences.addSetting({
  id: "clearOnCloseCookies",
  pref: "privacy.clearOnShutdown_v2.cookiesAndStorage",
});
Preferences.addSetting({
  id: "clearOnCloseCache",
  pref: "privacy.clearOnShutdown_v2.cache",
});
Preferences.addSetting({
  id: "clearOnCloseStorage",
  pref: "privacy.clearOnShutdown_v2.cookiesAndStorage",
});
Preferences.addSetting({
  id: "sanitizeOnShutdown",
  pref: "privacy.sanitize.sanitizeOnShutdown",
});
Preferences.addSetting({
  id: "historyModeCustom",
  pref: "privacy.history.custom",
});
Preferences.addSetting({
  id: "cookieBehavior",
  pref: "network.cookie.cookieBehavior",
});
Preferences.addSetting({
  id: "deleteOnClose",
  deps: [
    "clearOnCloseCookies",
    "clearOnCloseCache",
    "clearOnCloseStorage",
    "sanitizeOnShutdown",
    "privateBrowsingAutoStart",
    "cookieBehavior",
    "alwaysClear",
  ],
  setup() {
    // Make sure to do the migration for the clear history dialog before implementing logic for delete on close
    // This needs to be done to make sure the migration is done before any pref changes are made to avoid unintentionally
    // overwriting prefs
    Sanitizer.maybeMigratePrefs("clearOnShutdown");
  },
  disabled({ privateBrowsingAutoStart, cookieBehavior }) {
    return (
      privateBrowsingAutoStart.value ||
      cookieBehavior.value == Ci.nsICookieService.BEHAVIOR_REJECT
    );
  },
  get(_, { privateBrowsingAutoStart }) {
    return (
      isCookiesAndStorageClearingOnShutdown() || privateBrowsingAutoStart.value
    );
  },
  set(
    value,
    {
      clearOnCloseCookies,
      clearOnCloseCache,
      clearOnCloseStorage,
      sanitizeOnShutdown,
    }
  ) {
    clearOnCloseCookies.value = value;
    clearOnCloseCache.value = value;
    clearOnCloseStorage.value = value;

    // Sync the cleaning prefs with the deleteOnClose box.

    // Forget the current pref selection if sanitizeOnShutdown is disabled,
    // to not over clear when it gets enabled by the sync mechanism
    if (!sanitizeOnShutdown.value) {
      resetCleaningPrefs();
    }
    // If no other cleaning category is selected, sanitizeOnShutdown gets synced with deleteOnClose
    sanitizeOnShutdown.value =
      gPrivacyPane._isCustomCleaningPrefPresent() || value;
  },
});

Preferences.addSetting({
  id: "historyModeCustom",
  pref: "privacy.history.custom",
});
Preferences.addSetting({
  id: "historyEnabled",
  pref: "places.history.enabled",
});
Preferences.addSetting({
  id: "formFillEnabled",
  pref: "browser.formfill.enable",
});

// Store this on the window so tests can suppress the prompt.
window._shouldPromptForRestartPBM = true;
async function onChangePrivateBrowsingAutoStart(value, revertFn) {
  if (!window._shouldPromptForRestartPBM) {
    return false;
  }

  // The PBM autostart pref has changed so we need to prompt for restart.
  let buttonIndex = await confirmRestartPrompt(value, 1, true, false);

  // User accepts, restart the browser.
  if (buttonIndex == CONFIRM_RESTART_PROMPT_RESTART_NOW) {
    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
    );
    return false;
  }

  // Don't prompt for the revert operation itself.
  window._shouldPromptForRestartPBM = false;
  revertFn();
  window._shouldPromptForRestartPBM = true;

  // User cancels, do nothing. The caller will clean up the pref change.
  return true;
}

Preferences.addSetting({
  id: "historyMode",
  deps: [
    "historyModeCustom",
    "privateBrowsingAutoStart",
    "historyEnabled",
    "formFillEnabled",
    "sanitizeOnShutdown",
  ],
  get(
    _,
    {
      historyModeCustom,
      privateBrowsingAutoStart,
      historyEnabled,
      formFillEnabled,
      sanitizeOnShutdown,
    }
  ) {
    if (historyModeCustom.value) {
      return "custom";
    }

    if (privateBrowsingAutoStart.value) {
      return "dontremember";
    }

    if (
      historyEnabled.value &&
      formFillEnabled.value &&
      !sanitizeOnShutdown.value
    ) {
      return "remember";
    }

    return "custom";
  },
  set(
    value,
    {
      historyModeCustom,
      privateBrowsingAutoStart,
      historyEnabled,
      formFillEnabled,
      sanitizeOnShutdown,
    }
  ) {
    let lastHistoryModeCustom = historyModeCustom.value;
    let lastHistoryEnabled = historyEnabled.value;
    let lastFormFillEnabled = formFillEnabled.value;
    let lastSanitizeOnShutdown = sanitizeOnShutdown.value;
    let lastPrivateBrowsingAutoStart = privateBrowsingAutoStart.value;

    historyModeCustom.value = value == "custom";

    if (value == "remember") {
      historyEnabled.value = true;
      formFillEnabled.value = true;
      sanitizeOnShutdown.value = false;
      privateBrowsingAutoStart.value = false;
    } else if (value == "dontremember") {
      privateBrowsingAutoStart.value = true;
    }

    if (privateBrowsingAutoStart.value !== lastPrivateBrowsingAutoStart) {
      // The PBM autostart pref has changed so we need to prompt for restart.
      onChangePrivateBrowsingAutoStart(privateBrowsingAutoStart.value, () => {
        // User cancelled the action, revert the change.
        // Simply reverting the setting value itself is not enough, because a
        // state transition to "custom" does not override any of the sub-prefs.
        // We need to update them all manually.
        historyModeCustom.value = lastHistoryModeCustom;
        historyEnabled.value = lastHistoryEnabled;
        formFillEnabled.value = lastFormFillEnabled;
        sanitizeOnShutdown.value = lastSanitizeOnShutdown;
        privateBrowsingAutoStart.value = lastPrivateBrowsingAutoStart;
      });
    }
  },
  disabled({ privateBrowsingAutoStart }) {
    // Disable history dropdown if PBM autostart is locked on.
    return privateBrowsingAutoStart.locked && privateBrowsingAutoStart.value;
  },
  getControlConfig(config, { privateBrowsingAutoStart }, setting) {
    let l10nId = null;
    if (!srdSectionEnabled("history2")) {
      if (setting.value == "remember") {
        l10nId = "history-remember-description4";
      } else if (setting.value == "dontremember") {
        l10nId = "history-dontremember-description4";
      } else if (setting.value == "custom") {
        l10nId = "history-custom-description4";
      }
    }

    let dontRememberOption = config.options.find(
      opt => opt.value == "dontremember"
    );

    // If PBM is unavailable hide the "Never remember history" option.
    dontRememberOption.hidden = !PrivateBrowsingUtils.enabled;

    // If the PBM autostart pref is locked disable the "Never remember history"
    // option.
    dontRememberOption.disabled =
      privateBrowsingAutoStart.locked && !privateBrowsingAutoStart.value;

    return {
      ...config,
      l10nId,
    };
  },
});

Preferences.addSetting({
  id: "customHistoryButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneHistory");
  },
});

Preferences.addSetting({
  id: "privateBrowsingAutoStart",
  pref: "browser.privatebrowsing.autostart",
  deps: ["historyMode"],
  onUserChange(value, _, setting) {
    onChangePrivateBrowsingAutoStart(value, () => {
      // User cancelled the action, revert the setting.
      setting.value = !value;
    });
  },
  visible({ historyMode }) {
    return PrivateBrowsingUtils.enabled && historyMode.value == "custom";
  },
});
Preferences.addSetting({
  id: "rememberHistory",
  pref: "places.history.enabled",
  deps: ["historyMode", "privateBrowsingAutoStart"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});
Preferences.addSetting({
  id: "rememberForms",
  pref: "browser.formfill.enable",
  deps: ["historyMode", "privateBrowsingAutoStart"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});
Preferences.addSetting({
  id: "alwaysClear",
  pref: "privacy.sanitize.sanitizeOnShutdown",
  deps: ["historyMode", "privateBrowsingAutoStart"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting({
  id: "clearDataSettings",
  deps: ["historyMode", "alwaysClear"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ alwaysClear }) {
    return !alwaysClear.value || alwaysClear.disabled;
  },
  onUserClick() {
    gSubDialog.open(
      "chrome://browser/content/sanitize_v2.xhtml",
      {
        features: "resizable=no",
      },
      {
        mode: "clearOnShutdown",
      }
    );
  },
});

Preferences.addSetting({
  id: "clearHistoryButton",
  deps: ["historyMode"],
  onUserClick(_, { historyMode }) {
    gPrivacyPane.clearPrivateDataNow(historyMode.value == "dontremember");
  },
});

Preferences.addSetting({
  id: "certificateButtonGroup",
});
Preferences.addSetting({
  id: "disableOpenCertManager",
  pref: "security.disable_button.openCertManager",
});
Preferences.addSetting({
  id: "disableOpenDeviceManager",
  pref: "security.disable_button.openDeviceManager",
});
Preferences.addSetting({
  id: "viewCertificatesButton",
  deps: ["disableOpenCertManager"],
  disabled: deps => {
    return deps.disableOpenCertManager.value;
  },
  onUserClick: () => {
    gPrivacyPane.showCertificates();
  },
});
Preferences.addSetting({
  id: "viewSecurityDevicesButton",
  deps: ["disableOpenDeviceManager"],
  disabled: deps => {
    return deps.disableOpenDeviceManager.value;
  },
  onUserClick: () => {
    gPrivacyPane.showSecurityDevices();
  },
});
Preferences.addSetting({
  id: "certEnableThirdPartyToggle",
  pref: "security.enterprise_roots.enabled",
  visible: () => {
    // Third-party certificate import is only implemented for Windows and Mac,
    // and we should not expose this as a user-configurable setting if there's
    // an enterprise policy controlling it (either to enable _or_ disable it).
    return (
      (AppConstants.platform == "win" || AppConstants.platform == "macosx") &&
      typeof Services.policies.getActivePolicies()?.Certificates
        ?.ImportEnterpriseRoots == "undefined"
    );
  },
});

Preferences.addSetting({
  id: "permissionBox",
});
Preferences.addSetting({
  id: "popupPolicy",
  pref: "dom.disable_open_during_load",
});
Preferences.addSetting({
  id: "popupPolicyButton",
  deps: ["popupPolicy"],
  onUserClick: () => gPrivacyPane.showPopupExceptions(),
  disabled: ({ popupPolicy }) => {
    return !popupPolicy.value || popupPolicy.locked;
  },
});
Preferences.addSetting({
  id: "warnAddonInstall",
  pref: "xpinstall.whitelist.required",
});
Preferences.addSetting({
  id: "addonExceptions",
  deps: ["warnAddonInstall"],
  onUserClick: () => gPrivacyPane.showAddonExceptions(),
  disabled: ({ warnAddonInstall }) => {
    return !warnAddonInstall.value || warnAddonInstall.locked;
  },
});
Preferences.addSetting({
  id: "notificationsDoNotDisturb",
  get: () => {
    return AlertsServiceDND?.manualDoNotDisturb ?? false;
  },
  set: value => {
    if (AlertsServiceDND) {
      AlertsServiceDND.manualDoNotDisturb = value;
    }
  },
  visible: () => {
    return AlertsServiceDND != undefined;
  },
});
Preferences.addSetting({
  id: "locationSettingsButton",
  onUserClick: () => gPrivacyPane.showLocationExceptions(),
});
Preferences.addSetting({
  id: "cameraSettingsButton",
  onUserClick: () => gPrivacyPane.showCameraExceptions(),
});
Preferences.addSetting({
  id: "enabledLNA",
  pref: "network.lna.blocking",
});
Preferences.addSetting({
  id: "localNetworkSettingsButton",
  onUserClick: () => gPrivacyPane.showLocalNetworkExceptions(),
  deps: ["enabledLNA"],
  visible: deps => {
    return deps.enabledLNA.value;
  },
});
Preferences.addSetting({
  id: "localHostSettingsButton",
  onUserClick: () => gPrivacyPane.showLocalHostExceptions(),
  deps: ["enabledLNA"],
  visible: deps => {
    return deps.enabledLNA.value;
  },
});
Preferences.addSetting({
  id: "microphoneSettingsButton",
  onUserClick: () => gPrivacyPane.showMicrophoneExceptions(),
});
Preferences.addSetting({
  id: "enabledSpeakerControl",
  pref: "media.setsinkid.enabled",
});
Preferences.addSetting({
  id: "speakerSettingsButton",
  onUserClick: () => gPrivacyPane.showSpeakerExceptions(),
  deps: ["enabledSpeakerControl"],
  visible: ({ enabledSpeakerControl }) => {
    return enabledSpeakerControl.value;
  },
});
Preferences.addSetting({
  id: "notificationSettingsButton",
  onUserClick: () => gPrivacyPane.showNotificationExceptions(),
});
Preferences.addSetting({
  id: "autoplaySettingsButton",
  onUserClick: () => gPrivacyPane.showAutoplayMediaExceptions(),
});
Preferences.addSetting({
  id: "xrSettingsButton",
  onUserClick: () => gPrivacyPane.showXRExceptions(),
});

Preferences.addSetting({
  id: "dohBox",
});

Preferences.addSetting({
  id: "dohAdvancedButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneDnsOverHttps");
  },
});

Preferences.addSetting({
  id: "dohExceptionsButton",
  onUserClick: () => gPrivacyPane.showDoHExceptions(),
});

Preferences.addSetting({
  id: "dohMode",
  pref: "network.trr.mode",
  setup(emitChange) {
    Services.obs.addObserver(emitChange, "network:trr-mode-changed");
    Services.obs.addObserver(emitChange, "network:trr-confirmation");
    return () => {
      Services.obs.removeObserver(emitChange, "network:trr-mode-changed");
      Services.obs.removeObserver(emitChange, "network:trr-confirmation");
    };
  },
});

Preferences.addSetting({
  id: "dohURL",
  pref: "network.trr.uri",
  setup(emitChange) {
    Services.obs.addObserver(emitChange, "network:trr-uri-changed");
    Services.obs.addObserver(emitChange, "network:trr-confirmation");
    return () => {
      Services.obs.removeObserver(emitChange, "network:trr-uri-changed");
      Services.obs.removeObserver(emitChange, "network:trr-confirmation");
    };
  },
});

Preferences.addSetting({
  id: "dohDefaultURL",
  pref: "network.trr.default_provider_uri",
});

Preferences.addSetting({
  id: "dohDisableHeuristics",
  pref: "doh-rollout.disable-heuristics",
});

Preferences.addSetting({
  id: "dohModeBoxItem",
  deps: ["dohMode"],
  getControlConfig: (config, deps) => {
    let l10nId = "preferences-doh-overview-off";
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_NATIVEONLY) {
      l10nId = "preferences-doh-overview-default";
    } else if (
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST ||
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY
    ) {
      l10nId = "preferences-doh-overview-custom";
    }
    return {
      ...config,
      l10nId,
    };
  },
});

Preferences.addSetting({
  id: "dohStatusBox",
  deps: ["dohMode", "dohURL"],
  getControlConfig: config => {
    let l10nId = "preferences-doh-status-item-off";
    let l10nArgs = {};
    let supportPage = "";
    let controlAttrs = { type: "info" };

    let trrURI = Services.dns.currentTrrURI;
    let hostname = URL.parse(trrURI)?.hostname;

    let name = hostname || trrURI;
    let nameFound = false;
    let steering = false;
    for (let resolver of DoHConfigController.currentConfig.providerList) {
      if (resolver.uri == trrURI) {
        name = resolver.UIName || name;
        nameFound = true;
        break;
      }
    }
    if (!nameFound) {
      for (let resolver of DoHConfigController.currentConfig.providerSteering
        .providerList) {
        if (resolver.uri == trrURI) {
          steering = true;
          name = resolver.UIName || name;
          break;
        }
      }
    }

    let mode = Services.dns.currentTrrMode;
    if (
      (mode == Ci.nsIDNSService.MODE_TRRFIRST ||
        mode == Ci.nsIDNSService.MODE_TRRONLY) &&
      lazy.gParentalControlsService?.parentalControlsEnabled
    ) {
      l10nId = "preferences-doh-status-item-not-active";
      supportPage = "doh-status";
      l10nArgs = {
        reason: Services.dns.getTRRSkipReasonName(
          Ci.nsITRRSkipReason.TRR_PARENTAL_CONTROL
        ),
        name,
      };
    } else {
      let confirmationState = Services.dns.currentTrrConfirmationState;
      if (
        mode != Ci.nsIDNSService.MODE_TRRFIRST &&
        mode != Ci.nsIDNSService.MODE_TRRONLY
      ) {
        l10nId = "preferences-doh-status-item-off";
      } else if (
        confirmationState == Ci.nsIDNSService.CONFIRM_TRYING_OK ||
        confirmationState == Ci.nsIDNSService.CONFIRM_OK ||
        confirmationState == Ci.nsIDNSService.CONFIRM_DISABLED
      ) {
        if (steering) {
          l10nId = "preferences-doh-status-item-active-local";
          controlAttrs = { type: "success" };
        } else {
          l10nId = "preferences-doh-status-item-active";
          controlAttrs = { type: "success" };
        }
      } else if (steering) {
        l10nId = "preferences-doh-status-item-not-active-local";
        supportPage = "doh-status";
        controlAttrs = { type: "warning" };
      } else {
        l10nId = "preferences-doh-status-item-not-active";
        supportPage = "doh-status";
        controlAttrs = { type: "warning" };
      }

      let confirmationStatus = Services.dns.lastConfirmationStatus;
      if (confirmationStatus != Cr.NS_OK) {
        l10nArgs = {
          reason: ChromeUtils.getXPCOMErrorName(confirmationStatus),
          name,
        };
      } else {
        l10nArgs = {
          reason: Services.dns.getTRRSkipReasonName(
            Services.dns.lastConfirmationSkipReason
          ),
          name,
        };
        if (
          Services.dns.lastConfirmationSkipReason ==
            Ci.nsITRRSkipReason.TRR_BAD_URL ||
          !name
        ) {
          l10nId = "preferences-doh-status-item-not-active-bad-url";
          supportPage = "doh-status";
          controlAttrs = { type: "warning" };
        }
      }
    }

    return {
      ...config,
      l10nId,
      l10nArgs,
      supportPage,
      controlAttrs,
    };
  },
});

Preferences.addSetting({
  id: "dohRadioGroup",
  // These deps are complicated:
  // this radio group, along with dohFallbackIfCustom controls the mode and URL.
  // Therefore, we set dohMode and dohURL as deps here. This is a smell, but needed
  // for the mismatch of control-to-pref.
  deps: ["dohFallbackIfCustom", "dohMode", "dohURL"],
  onUserChange: (val, deps) => {
    let value = null;
    if (val == "default") {
      value = "dohDefaultRadio";
    } else if (val == "off") {
      value = "dohOffRadio";
    } else if (val == "custom" && deps.dohFallbackIfCustom.value) {
      value = "dohEnabledRadio";
    } else if (val == "custom" && !deps.dohFallbackIfCustom.value) {
      value = "dohStrictRadio";
    }
    if (value) {
      Glean.securityDohSettings.modeChangedButton.record({
        value,
      });
    }
  },
  get: (_val, deps) => {
    switch (deps.dohMode.value) {
      case Ci.nsIDNSService.MODE_NATIVEONLY:
        return "default";
      case Ci.nsIDNSService.MODE_TRRFIRST:
      case Ci.nsIDNSService.MODE_TRRONLY:
        return "custom";
      case Ci.nsIDNSService.MODE_TRROFF:
      case Ci.nsIDNSService.MODE_RESERVED1:
      case Ci.nsIDNSService.MODE_RESERVED4:
      default:
        return "off";
    }
  },
  set: (val, deps) => {
    if (val == "custom") {
      if (deps.dohFallbackIfCustom.value) {
        deps.dohMode.value = Ci.nsIDNSService.MODE_TRRFIRST;
      } else {
        deps.dohMode.value = Ci.nsIDNSService.MODE_TRRONLY;
      }
    } else if (val == "off") {
      deps.dohMode.value = Ci.nsIDNSService.MODE_TRROFF;
    } else {
      deps.dohMode.value = Ci.nsIDNSService.MODE_NATIVEONLY;
    }

    // When the mode is set to 0 we need to clear the URI so
    // doh-rollout can kick in.
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_NATIVEONLY) {
      deps.dohURL.pref.value = undefined;
      Services.prefs.clearUserPref("doh-rollout.disable-heuristics");
    }

    // Bug 1861285
    // When the mode is set to 2 or 3, we need to check if network.trr.uri is a empty string.
    // In this case, we need to update network.trr.uri to default to fallbackProviderURI.
    // This occurs when the mode is previously set to 0 (Default Protection).
    if (
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST ||
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY
    ) {
      if (!deps.dohURL.value) {
        deps.dohURL.value =
          DoHConfigController.currentConfig.fallbackProviderURI;
      }
    }

    // Bug 1900672
    // When the mode is set to 5, clear the pref to ensure that
    // network.trr.uri is set to fallbackProviderURIwhen the mode is set to 2 or 3 afterwards
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRROFF) {
      deps.dohURL.pref.value = undefined;
    }
  },
});

Preferences.addSetting({
  id: "dohFallbackIfCustom",
  pref: "network.trr_ui.fallback_was_checked",
  // These deps are complicated:
  // this checkbox, along with dohRadioGroup controls the mode and URL.
  // Therefore, we set dohMode as a dep here. This is a smell, but needed
  // for the mismatch of control-to-pref.
  deps: ["dohMode"],
  onUserChange: val => {
    if (val) {
      Glean.securityDohSettings.modeChangedButton.record({
        value: "dohEnabledRadio",
      });
    } else {
      Glean.securityDohSettings.modeChangedButton.record({
        value: "dohStrictRadio",
      });
    }
  },
  get: (val, deps) => {
    // If we are in a custom mode, we need to get the value from the Setting
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST) {
      return true;
    }
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY) {
      return false;
    }

    // Propagate the preference otherwise
    return val;
  },
  set: (val, deps) => {
    // Toggle the preference that controls the setting if are in a custom mode
    // This should be the only case where the checkbox is enabled, but we can be
    // careful and test.
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST && !val) {
      deps.dohMode.value = Ci.nsIDNSService.MODE_TRRONLY;
    } else if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY && val) {
      deps.dohMode.value = Ci.nsIDNSService.MODE_TRRFIRST;
    }
    // Propagate to the real preference
    return val;
  },
});

Preferences.addSetting({
  id: "dohCustomProvider",
  deps: ["dohProviderSelect", "dohURL"],
  _value: null,
  visible: deps => {
    return deps.dohProviderSelect.value == "custom";
  },
  get(_val, deps) {
    if (this._value === null) {
      return deps.dohURL.value;
    }
    return this._value;
  },
  set(val, deps) {
    this._value = val;
    if (val == "") {
      val = " ";
    }
    deps.dohURL.value = val;
  },
});

Preferences.addSetting({
  id: "dohProviderSelect",
  deps: ["dohURL", "dohDefaultURL"],
  _custom: false,
  onUserChange: value => {
    Glean.securityDohSettings.providerChoiceValue.record({
      value,
    });
  },
  getControlConfig(config, deps) {
    let options = [];

    let resolvers = DoHConfigController.currentConfig.providerList;
    // if there's no default, we'll hold its position with an empty string
    let defaultURI = DoHConfigController.currentConfig.fallbackProviderURI;
    let defaultFound = resolvers.some(p => p.uri == defaultURI);
    if (!defaultFound && defaultURI) {
      // the default value for the pref isn't included in the resolvers list
      // so we'll make a stub for it. Without an id, we'll have to use the url as the label
      resolvers.unshift({ uri: defaultURI });
    }
    let currentURI = deps.dohURL.value;
    if (currentURI && !resolvers.some(p => p.uri == currentURI)) {
      this._custom = true;
    }

    options = resolvers.map(resolver => {
      let option = {
        value: resolver.uri,
        l10nArgs: {
          name: resolver.UIName || resolver.uri,
        },
      };
      if (resolver.uri == defaultURI) {
        option.l10nId = "connection-dns-over-https-url-item-default";
      } else {
        option.l10nId = "connection-dns-over-https-url-item";
      }
      return option;
    });
    options.push({
      value: "custom",
      l10nId: "connection-dns-over-https-url-custom",
    });

    return {
      options,
      ...config,
    };
  },
  get(_val, deps) {
    if (this._custom) {
      return "custom";
    }
    let currentURI = deps.dohURL.value;
    if (!currentURI) {
      currentURI = deps.dohDefaultURL.value;
    }
    return currentURI;
  },
  set(val, deps, setting) {
    if (val != "custom") {
      this._custom = false;
      deps.dohURL.value = val;
    } else {
      this._custom = true;
    }
    setting.emit("change");
    return val;
  },
});

function shouldDisableETPCategoryControls() {
  let policy = Services.policies.getActivePolicies();
  return policy?.EnableTrackingProtection?.Locked || policy?.Cookies?.Locked;
}

Preferences.addSetting({
  id: "contentBlockingCategory",
  pref: "browser.contentblocking.category",
});

// We need a separate setting for the radio group for custom disable behavior.
// Setter and getter simply write to the pref.
Preferences.addSetting({
  id: "contentBlockingCategoryRadioGroup",
  deps: ["contentBlockingCategory"],
  get(_, { contentBlockingCategory }) {
    return contentBlockingCategory.value;
  },
  set(value, { contentBlockingCategory }) {
    contentBlockingCategory.value = value;
  },
  getControlConfig(config, _, setting) {
    if (!shouldDisableETPCategoryControls()) {
      return config;
    }

    let { options } = config;

    // If ETP level is set to custom keep the radio button enabled so the "customize" button works even when the category selection itself is locked.
    for (let option of options) {
      option.disabled =
        option.id != "etpLevelCustom" || setting.value != "custom";
    }

    return config;
  },
});

Preferences.addSetting({
  id: "etpStatusBoxGroup",
});

Preferences.addSetting({
  id: "etpStatusItem",
  deps: ["contentBlockingCategory"],
  getControlConfig(config, { contentBlockingCategory }) {
    // Display a different description and label depending on the content blocking category (= ETP level).
    let categoryToL10nId = {
      standard: "preferences-etp-level-standard",
      strict: "preferences-etp-level-strict",
      custom: "preferences-etp-level-custom",
    };

    return {
      ...config,
      l10nId:
        categoryToL10nId[contentBlockingCategory.value] ??
        "preferences-etp-level-standard",
    };
  },
});

Preferences.addSetting({
  id: "etpStatusAdvancedButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("etp");
  },
});

Preferences.addSetting({
  id: "protectionsDashboardLink",
});

Preferences.addSetting({
  id: "etpBannerEl",
});

Preferences.addSetting({
  id: "etpAllowListBaselineEnabled",
  pref: "privacy.trackingprotection.allow_list.baseline.enabled",
  deps: ["contentBlockingCategory"],
  visible({ contentBlockingCategory }) {
    return contentBlockingCategory.value == "strict";
  },
  onUserChange(value, _deps, setting) {
    gPrivacyPane.onBaselineAllowListSettingChange(value, setting);
  },
});

Preferences.addSetting({
  id: "etpAllowListConvenienceEnabled",
  pref: "privacy.trackingprotection.allow_list.convenience.enabled",
  onUserChange() {
    gPrivacyPane.maybeNotifyUserToReload();
  },
});

Preferences.addSetting({
  id: "etpCustomizeButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("etpCustomize");
  },
});

Preferences.addSetting({
  id: "reloadTabsHint",
  _showHint: false,
  set(value, _, setting) {
    this._showHint = value;
    setting.emit("change");
  },
  get() {
    return this._showHint;
  },
  visible(_, setting) {
    return setting.value;
  },
  onUserClick() {
    gPrivacyPane.reloadAllOtherTabs();
  },
});

Preferences.addSetting({
  id: "resistFingerprinting",
  pref: "privacy.resistFingerprinting",
});

Preferences.addSetting({
  id: "resistFingerprintingPBM",
  pref: "privacy.resistFingerprinting.pbmode",
});

Preferences.addSetting({
  id: "rfpWarning",
  deps: ["resistFingerprinting", "resistFingerprintingPBM"],
  visible({ resistFingerprinting, resistFingerprintingPBM }) {
    return resistFingerprinting.value || resistFingerprintingPBM.value;
  },
});

Preferences.addSetting({
  id: "etpLevelWarning",
  deps: ["contentBlockingCategory"],
  visible({ contentBlockingCategory }) {
    return contentBlockingCategory.value != "standard";
  },
});

Preferences.addSetting({
  id: "etpManageExceptionsButton",
  onUserClick() {
    let params = {
      permissionType: "trackingprotection",
      disableETPVisible: true,
      prefilledHost: "",
      hideStatusColumn: true,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },
});

Preferences.addSetting({
  id: "etpResetButtonGroup",
});

Preferences.addSetting({
  id: "etpResetStandardButton",
  deps: ["contentBlockingCategory"],
  onUserClick(_, { contentBlockingCategory }) {
    contentBlockingCategory.value = "standard";
  },
  disabled({ contentBlockingCategory }) {
    return (
      contentBlockingCategory.value == "standard" ||
      shouldDisableETPCategoryControls()
    );
  },
});

Preferences.addSetting({
  id: "etpResetStrictButton",
  deps: ["contentBlockingCategory"],
  onUserClick(_, { contentBlockingCategory }) {
    contentBlockingCategory.value = "strict";
  },
  disabled({ contentBlockingCategory }) {
    return (
      contentBlockingCategory.value == "strict" ||
      shouldDisableETPCategoryControls()
    );
  },
});

Preferences.addSetting({
  id: "etpAllowListBaselineEnabledCustom",
  pref: "privacy.trackingprotection.allow_list.baseline.enabled",
  onUserChange(value, _deps, setting) {
    gPrivacyPane.onBaselineAllowListSettingChange(value, setting);
  },
});

Preferences.addSetting({
  id: "etpAllowListConvenienceEnabledCustom",
  pref: "privacy.trackingprotection.allow_list.convenience.enabled",
  onUserChange() {
    gPrivacyPane.maybeNotifyUserToReload();
  },
});

Preferences.addSetting({
  id: "etpCustomCookiesEnabled",
  deps: ["cookieBehavior"],
  disabled: ({ cookieBehavior }) => {
    return cookieBehavior.locked;
  },
  get(_, { cookieBehavior }) {
    return cookieBehavior.value != Ci.nsICookieService.BEHAVIOR_ACCEPT;
  },
  set(value, { cookieBehavior }) {
    if (!value) {
      cookieBehavior.value = Ci.nsICookieService.BEHAVIOR_ACCEPT;
    } else {
      // When the user enabled cookie blocking, set the cookie behavior to the default.
      cookieBehavior.value = cookieBehavior.pref.defaultValue;
    }
  },
});

Preferences.addSetting({
  id: "trackingProtectionEnabled",
  pref: "privacy.trackingprotection.enabled",
});

Preferences.addSetting({
  id: "trackingProtectionEnabledPBM",
  pref: "privacy.trackingprotection.pbmode.enabled",
});

Preferences.addSetting({
  id: "etpCustomTrackingProtectionEnabledContext",
  deps: ["trackingProtectionEnabled", "trackingProtectionEnabledPBM"],
  get(_, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    if (trackingProtectionEnabled.value && trackingProtectionEnabledPBM.value) {
      return "all";
    } else if (trackingProtectionEnabledPBM) {
      return "pbmOnly";
    }
    return null;
  },
  set(value, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    if (value == "all") {
      trackingProtectionEnabled.value = true;
      trackingProtectionEnabledPBM.value = true;
    } else if (value == "pbmOnly") {
      trackingProtectionEnabled.value = false;
      trackingProtectionEnabledPBM.value = true;
    }
  },
});

Preferences.addSetting({
  id: "etpCustomTrackingProtectionEnabled",
  deps: ["trackingProtectionEnabled", "trackingProtectionEnabledPBM"],
  disabled: ({ trackingProtectionEnabled, trackingProtectionEnabledPBM }) => {
    return (
      trackingProtectionEnabled.locked || trackingProtectionEnabledPBM.locked
    );
  },
  get(_, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    return (
      trackingProtectionEnabled.value || trackingProtectionEnabledPBM.value
    );
  },
  set(value, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    if (value) {
      trackingProtectionEnabled.value = false;
      trackingProtectionEnabledPBM.value = true;
    } else {
      trackingProtectionEnabled.value = false;
      trackingProtectionEnabledPBM.value = false;
    }
  },
});

Preferences.addSetting({
  id: "etpCustomCryptominingProtectionEnabled",
  pref: "privacy.trackingprotection.cryptomining.enabled",
});

Preferences.addSetting({
  id: "etpCustomKnownFingerprintingProtectionEnabled",
  pref: "privacy.trackingprotection.fingerprinting.enabled",
});

Preferences.addSetting({
  id: "etpCustomFingerprintingProtectionEnabled",
  pref: "privacy.fingerprintingProtection",
});

Preferences.addSetting({
  id: "etpCustomFingerprintingProtectionEnabledPBM",
  pref: "privacy.fingerprintingProtection.pbmode",
});

Preferences.addSetting({
  id: "etpCustomSuspectFingerprintingProtectionEnabled",
  deps: [
    "etpCustomFingerprintingProtectionEnabled",
    "etpCustomFingerprintingProtectionEnabledPBM",
  ],
  disabled({
    etpCustomFingerprintingProtectionEnabled,
    etpCustomFingerprintingProtectionEnabledPBM,
  }) {
    return (
      etpCustomFingerprintingProtectionEnabled.locked ||
      etpCustomFingerprintingProtectionEnabledPBM.locked
    );
  },
  get(
    _,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    return (
      etpCustomFingerprintingProtectionEnabled.value ||
      etpCustomFingerprintingProtectionEnabledPBM.value
    );
  },
  set(
    value,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    if (value) {
      etpCustomFingerprintingProtectionEnabled.value = false;
      etpCustomFingerprintingProtectionEnabledPBM.value = true;
    } else {
      etpCustomFingerprintingProtectionEnabled.value = false;
      etpCustomFingerprintingProtectionEnabledPBM.value = false;
    }
  },
});

Preferences.addSetting({
  id: "etpCustomSuspectFingerprintingProtectionEnabledContext",
  deps: [
    "etpCustomFingerprintingProtectionEnabled",
    "etpCustomFingerprintingProtectionEnabledPBM",
  ],
  get(
    _,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    if (
      etpCustomFingerprintingProtectionEnabled.value &&
      etpCustomFingerprintingProtectionEnabledPBM.value
    ) {
      return "all";
    } else if (etpCustomFingerprintingProtectionEnabledPBM) {
      return "pbmOnly";
    }
    return null;
  },
  set(
    value,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    if (value == "all") {
      etpCustomFingerprintingProtectionEnabled.value = true;
      etpCustomFingerprintingProtectionEnabledPBM.value = true;
    } else if (value == "pbmOnly") {
      etpCustomFingerprintingProtectionEnabled.value = false;
      etpCustomFingerprintingProtectionEnabledPBM.value = true;
    }
  },
});

function setEventListener(aId, aEventType, aCallback) {
  document
    .getElementById(aId)
    .addEventListener(aEventType, aCallback.bind(gPrivacyPane));
}

function setSyncFromPrefListener(aId, aCallback) {
  Preferences.addSyncFromPrefListener(document.getElementById(aId), aCallback);
}

function setSyncToPrefListener(aId, aCallback) {
  Preferences.addSyncToPrefListener(document.getElementById(aId), aCallback);
}

function dataCollectionCheckboxHandler({
  checkbox,
  pref,
  matchPref = () => true,
  isDisabled = () => false,
}) {
  function updateCheckbox() {
    let collectionEnabled = Services.prefs.getBoolPref(
      PREF_UPLOAD_ENABLED,
      false
    );

    if (collectionEnabled && matchPref()) {
      checkbox.toggleAttribute(
        "checked",
        Services.prefs.getBoolPref(pref, false)
      );
      checkbox.setAttribute("preference", pref);
    } else {
      checkbox.removeAttribute("preference");
      checkbox.removeAttribute("checked");
    }

    checkbox.disabled =
      !collectionEnabled || Services.prefs.prefIsLocked(pref) || isDisabled();
  }

  Preferences.get(PREF_UPLOAD_ENABLED).on("change", updateCheckbox);
  updateCheckbox();
}

// Sets the "Learn how" SUMO link in the Strict/Custom options of Content Blocking.
function setUpContentBlockingWarnings() {
  document.getElementById("fpiIncompatibilityWarning").hidden =
    !gIsFirstPartyIsolated;

  document.getElementById("rfpIncompatibilityWarning").hidden =
    !Preferences.get("privacy.resistFingerprinting").value &&
    !Preferences.get("privacy.resistFingerprinting.pbmode").value;
}

function initTCPStandardSection() {
  let cookieBehaviorPref = Preferences.get("network.cookie.cookieBehavior");
  let updateTCPSectionVisibilityState = () => {
    document.getElementById("etpStandardTCPBox").hidden =
      cookieBehaviorPref.value !=
      Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN;
  };

  cookieBehaviorPref.on("change", updateTCPSectionVisibilityState);

  updateTCPSectionVisibilityState();
}

var gPrivacyPane = {
  _pane: null,

  /**
   * Whether the prompt to restart Firefox should appear when changing the autostart pref.
   */
  _shouldPromptForRestart: true,

  /**
   * Update the tracking protection UI to deal with extension control.
   */
  _updateTrackingProtectionUI() {
    let cBPrefisLocked = CONTENT_BLOCKING_PREFS.some(pref =>
      Services.prefs.prefIsLocked(pref)
    );
    let tPPrefisLocked = TRACKING_PROTECTION_PREFS.some(pref =>
      Services.prefs.prefIsLocked(pref)
    );

    function setInputsDisabledState(isControlled) {
      let tpDisabled = tPPrefisLocked || isControlled;
      let disabled = cBPrefisLocked || isControlled;
      let tpCheckbox = document.getElementById(
        "contentBlockingTrackingProtectionCheckbox"
      );
      // Only enable the TP menu if Detect All Trackers is enabled.
      document.getElementById("trackingProtectionMenu").disabled =
        tpDisabled || !tpCheckbox.checked;
      tpCheckbox.disabled = tpDisabled;

      document.getElementById("standardRadio").disabled = disabled;
      document.getElementById("strictRadio").disabled = disabled;
      document
        .getElementById("contentBlockingOptionStrict")
        .classList.toggle("disabled", disabled);
      document
        .getElementById("contentBlockingOptionStandard")
        .classList.toggle("disabled", disabled);
      let arrowButtons = document.querySelectorAll("button.arrowhead");
      for (let button of arrowButtons) {
        button.disabled = disabled;
      }

      // Notify observers that the TP UI has been updated.
      // This is needed since our tests need to be notified about the
      // trackingProtectionMenu element getting disabled/enabled at the right time.
      Services.obs.notifyObservers(window, "privacy-pane-tp-ui-updated");
    }

    if (shouldDisableETPCategoryControls()) {
      setInputsDisabledState(true);
    }
    if (tPPrefisLocked) {
      // An extension can't control this setting if either pref is locked.
      hideControllingExtension(TRACKING_PROTECTION_KEY);
      setInputsDisabledState(false);
    } else {
      handleControllingExtension(
        PREF_SETTING_TYPE,
        TRACKING_PROTECTION_KEY
      ).then(setInputsDisabledState);
    }
  },

  /**
   * Set up handlers for showing and hiding controlling extension info
   * for tracking protection.
   */
  _initTrackingProtectionExtensionControl() {
    setEventListener(
      "contentBlockingDisableTrackingProtectionExtension",
      "command",
      makeDisableControllingExtension(
        PREF_SETTING_TYPE,
        TRACKING_PROTECTION_KEY
      )
    );

    let trackingProtectionObserver = {
      observe() {
        gPrivacyPane._updateTrackingProtectionUI();
      },
    };

    for (let pref of TRACKING_PROTECTION_PREFS) {
      Services.prefs.addObserver(pref, trackingProtectionObserver);
    }
    window.addEventListener("unload", () => {
      for (let pref of TRACKING_PROTECTION_PREFS) {
        Services.prefs.removeObserver(pref, trackingProtectionObserver);
      }
    });
  },

  /**
   * Ensure the tracking protection exception list is migrated before the privacy
   * preferences UI is shown.
   * If the migration has already been run, this is a no-op.
   */
  _ensureTrackingProtectionExceptionListMigration() {
    // Let's check the migration pref here as well to avoid the extra xpcom call
    // for the common case where we've already migrated.
    if (
      Services.prefs.getBoolPref(
        "privacy.trackingprotection.allow_list.hasMigratedCategoryPrefs",
        false
      )
    ) {
      return;
    }

    let exceptionListService = Cc[
      "@mozilla.org/url-classifier/exception-list-service;1"
    ].getService(Ci.nsIUrlClassifierExceptionListService);

    exceptionListService.maybeMigrateCategoryPrefs();
  },

  get dnsOverHttpsResolvers() {
    let providers = DoHConfigController.currentConfig.providerList;
    // if there's no default, we'll hold its position with an empty string
    let defaultURI = DoHConfigController.currentConfig.fallbackProviderURI;
    let defaultIndex = providers.findIndex(p => p.uri == defaultURI);
    if (defaultIndex == -1 && defaultURI) {
      // the default value for the pref isn't included in the resolvers list
      // so we'll make a stub for it. Without an id, we'll have to use the url as the label
      providers.unshift({ uri: defaultURI });
    }
    return providers;
  },

  updateDoHResolverList(mode) {
    let resolvers = this.dnsOverHttpsResolvers;
    let currentURI = Preferences.get("network.trr.uri").value;
    if (!currentURI) {
      currentURI = Preferences.get("network.trr.default_provider_uri").value;
    }
    let menu = document.getElementById(`${mode}ResolverChoices`);

    let selectedIndex = currentURI
      ? resolvers.findIndex(r => r.uri == currentURI)
      : 0;
    if (selectedIndex == -1) {
      // select the last "Custom" item
      selectedIndex = menu.itemCount - 1;
    }
    menu.selectedIndex = selectedIndex;

    let customInput = document.getElementById(`${mode}InputField`);
    customInput.hidden = menu.value != "custom";
  },

  populateDoHResolverList(mode) {
    let resolvers = this.dnsOverHttpsResolvers;
    let defaultURI = DoHConfigController.currentConfig.fallbackProviderURI;
    let menu = document.getElementById(`${mode}ResolverChoices`);

    // populate the DNS-Over-HTTPS resolver list
    menu.removeAllItems();
    for (let resolver of resolvers) {
      let item = menu.appendItem(undefined, resolver.uri);
      if (resolver.uri == defaultURI) {
        document.l10n.setAttributes(
          item,
          "connection-dns-over-https-url-item-default",
          {
            name: resolver.UIName || resolver.uri,
          }
        );
      } else {
        item.label = resolver.UIName || resolver.uri;
      }
    }
    let lastItem = menu.appendItem(undefined, "custom");
    document.l10n.setAttributes(
      lastItem,
      "connection-dns-over-https-url-custom"
    );

    // set initial selection in the resolver provider picker
    this.updateDoHResolverList(mode);

    let customInput = document.getElementById(`${mode}InputField`);

    function updateURIPref() {
      if (customInput.value == "") {
        // Setting the pref to empty string will make it have the default
        // pref value which makes us fallback to using the default TRR
        // resolver in network.trr.default_provider_uri.
        // If the input is empty we set it to "(space)" which is essentially
        // the same.
        Services.prefs.setStringPref("network.trr.uri", " ");
      } else {
        Services.prefs.setStringPref("network.trr.uri", customInput.value);
      }
    }

    menu.addEventListener("command", () => {
      if (menu.value == "custom") {
        customInput.hidden = false;
        updateURIPref();
      } else {
        customInput.hidden = true;
        Services.prefs.setStringPref("network.trr.uri", menu.value);
      }
      Glean.securityDohSettings.providerChoiceValue.record({
        value: menu.value,
      });

      // Update other menu too.
      let otherMode = mode == "dohEnabled" ? "dohStrict" : "dohEnabled";
      let otherMenu = document.getElementById(`${otherMode}ResolverChoices`);
      let otherInput = document.getElementById(`${otherMode}InputField`);
      otherMenu.value = menu.value;
      otherInput.hidden = otherMenu.value != "custom";
    });

    // Change the URL when you press ENTER in the input field it or loses focus
    customInput.addEventListener("change", () => {
      updateURIPref();
    });
  },

  async updateDoHStatus() {
    let trrURI = Services.dns.currentTrrURI;
    let hostname = URL.parse(trrURI)?.hostname;
    if (!hostname) {
      hostname = await document.l10n.formatValue("preferences-doh-bad-url");
    }

    let steering = document.getElementById("dohSteeringStatus");
    steering.hidden = true;

    let dohResolver = document.getElementById("dohResolver");
    dohResolver.hidden = true;

    let status = document.getElementById("dohStatus");

    async function setStatus(localizedStringName, options) {
      let opts = options || {};
      let statusString = await document.l10n.formatValue(
        localizedStringName,
        opts
      );
      document.l10n.setAttributes(status, "preferences-doh-status", {
        status: statusString,
      });
    }

    function computeStatus() {
      let mode = Services.dns.currentTrrMode;
      if (
        mode == Ci.nsIDNSService.MODE_TRRFIRST ||
        mode == Ci.nsIDNSService.MODE_TRRONLY
      ) {
        if (lazy.gParentalControlsService?.parentalControlsEnabled) {
          return "preferences-doh-status-not-active";
        }
        let confirmationState = Services.dns.currentTrrConfirmationState;
        switch (confirmationState) {
          case Ci.nsIDNSService.CONFIRM_TRYING_OK:
          case Ci.nsIDNSService.CONFIRM_OK:
          case Ci.nsIDNSService.CONFIRM_DISABLED:
            return "preferences-doh-status-active";
          default:
            return "preferences-doh-status-not-active";
        }
      }

      return "preferences-doh-status-disabled";
    }

    let errReason = "";
    let confirmationStatus = Services.dns.lastConfirmationStatus;
    let mode = Services.dns.currentTrrMode;
    if (
      (mode == Ci.nsIDNSService.MODE_TRRFIRST ||
        mode == Ci.nsIDNSService.MODE_TRRONLY) &&
      lazy.gParentalControlsService?.parentalControlsEnabled
    ) {
      errReason = Services.dns.getTRRSkipReasonName(
        Ci.nsITRRSkipReason.TRR_PARENTAL_CONTROL
      );
    } else if (confirmationStatus != Cr.NS_OK) {
      errReason = ChromeUtils.getXPCOMErrorName(confirmationStatus);
    } else {
      errReason = Services.dns.getTRRSkipReasonName(
        Services.dns.lastConfirmationSkipReason
      );
    }
    let statusLabel = computeStatus();
    // setStatus will format and set the statusLabel asynchronously.
    setStatus(statusLabel, { reason: errReason });
    dohResolver.hidden = statusLabel == "preferences-doh-status-disabled";

    let statusLearnMore = document.getElementById("dohStatusLearnMore");
    statusLearnMore.hidden = statusLabel != "preferences-doh-status-not-active";

    // No need to set the resolver name since we're not going to show it.
    if (statusLabel == "preferences-doh-status-disabled") {
      return;
    }

    function nameOrDomain() {
      for (let resolver of DoHConfigController.currentConfig.providerList) {
        if (resolver.uri == trrURI) {
          return resolver.UIName || hostname || trrURI;
        }
      }

      // Also check if this is a steering provider.
      for (let resolver of DoHConfigController.currentConfig.providerSteering
        .providerList) {
        if (resolver.uri == trrURI) {
          steering.hidden = false;
          return resolver.UIName || hostname || trrURI;
        }
      }

      return hostname;
    }

    let resolverNameOrDomain = nameOrDomain();
    document.l10n.setAttributes(dohResolver, "preferences-doh-resolver", {
      name: resolverNameOrDomain,
    });
  },

  highlightDoHCategoryAndUpdateStatus() {
    let value = Preferences.get("network.trr.mode").value;
    let defaultOption = document.getElementById("dohOptionDefault");
    let enabledOption = document.getElementById("dohOptionEnabled");
    let strictOption = document.getElementById("dohOptionStrict");
    let offOption = document.getElementById("dohOptionOff");
    defaultOption.classList.remove("selected");
    enabledOption.classList.remove("selected");
    strictOption.classList.remove("selected");
    offOption.classList.remove("selected");

    switch (value) {
      case Ci.nsIDNSService.MODE_NATIVEONLY:
        defaultOption.classList.add("selected");
        break;
      case Ci.nsIDNSService.MODE_TRRFIRST:
        enabledOption.classList.add("selected");
        break;
      case Ci.nsIDNSService.MODE_TRRONLY:
        strictOption.classList.add("selected");
        break;
      case Ci.nsIDNSService.MODE_TRROFF:
        offOption.classList.add("selected");
        break;
      default:
        // The pref is set to a random value.
        // This shouldn't happen, but let's make sure off is selected.
        offOption.classList.add("selected");
        document.getElementById("dohCategoryRadioGroup").selectedIndex = 3;
        break;
    }

    // When the mode is set to 0 we need to clear the URI so
    // doh-rollout can kick in.
    if (value == Ci.nsIDNSService.MODE_NATIVEONLY) {
      Services.prefs.clearUserPref("network.trr.uri");
      Services.prefs.clearUserPref("doh-rollout.disable-heuristics");
    }

    // Bug 1861285
    // When the mode is set to 2 or 3, we need to check if network.trr.uri is a empty string.
    // In this case, we need to update network.trr.uri to default to fallbackProviderURI.
    // This occurs when the mode is previously set to 0 (Default Protection).
    if (
      value == Ci.nsIDNSService.MODE_TRRFIRST ||
      value == Ci.nsIDNSService.MODE_TRRONLY
    ) {
      if (!Services.prefs.getStringPref("network.trr.uri")) {
        Services.prefs.setStringPref(
          "network.trr.uri",
          DoHConfigController.currentConfig.fallbackProviderURI
        );
      }
    }

    // Bug 1900672
    // When the mode is set to 5, clear the pref to ensure that
    // network.trr.uri is set to fallbackProviderURIwhen the mode is set to 2 or 3 afterwards
    if (value == Ci.nsIDNSService.MODE_TRROFF) {
      Services.prefs.clearUserPref("network.trr.uri");
    }

    gPrivacyPane.updateDoHStatus();
  },

  /**
   * Init DoH corresponding prefs
   */
  initDoH() {
    setEventListener("dohDefaultArrow", "command", this.toggleExpansion);
    setEventListener("dohEnabledArrow", "command", this.toggleExpansion);
    setEventListener("dohStrictArrow", "command", this.toggleExpansion);

    function modeButtonPressed(e) {
      // Clicking the active mode again should not generate another event
      if (
        parseInt(e.target.value) == Preferences.get("network.trr.mode").value
      ) {
        return;
      }
      Glean.securityDohSettings.modeChangedButton.record({
        value: e.target.id,
      });
    }

    setEventListener("dohDefaultRadio", "command", modeButtonPressed);
    setEventListener("dohEnabledRadio", "command", modeButtonPressed);
    setEventListener("dohStrictRadio", "command", modeButtonPressed);
    setEventListener("dohOffRadio", "command", modeButtonPressed);

    this.populateDoHResolverList("dohEnabled");
    this.populateDoHResolverList("dohStrict");

    Preferences.get("network.trr.uri").on("change", () => {
      gPrivacyPane.updateDoHResolverList("dohEnabled");
      gPrivacyPane.updateDoHResolverList("dohStrict");
      gPrivacyPane.updateDoHStatus();
    });

    // Update status box and hightlightling when the pref changes
    Preferences.get("network.trr.mode").on(
      "change",
      gPrivacyPane.highlightDoHCategoryAndUpdateStatus
    );
    this.highlightDoHCategoryAndUpdateStatus();

    Services.obs.addObserver(this, "network:trr-uri-changed");
    Services.obs.addObserver(this, "network:trr-mode-changed");
    Services.obs.addObserver(this, "network:trr-confirmation");
    let unload = () => {
      Services.obs.removeObserver(this, "network:trr-uri-changed");
      Services.obs.removeObserver(this, "network:trr-mode-changed");
      Services.obs.removeObserver(this, "network:trr-confirmation");
    };
    window.addEventListener("unload", unload, { once: true });

    let uriPref = Services.prefs.getStringPref("network.trr.uri");
    // If the value isn't one of the providers, we need to update the
    // custom_uri pref to make sure the input box contains the correct URL.
    if (uriPref && !this.dnsOverHttpsResolvers.some(e => e.uri == uriPref)) {
      Services.prefs.setStringPref(
        "network.trr.custom_uri",
        Services.prefs.getStringPref("network.trr.uri")
      );
    }

    if (Services.prefs.prefIsLocked("network.trr.mode")) {
      document.getElementById("dohCategoryRadioGroup").disabled = true;
      Services.prefs.setStringPref("network.trr.custom_uri", uriPref);
    }
  },

  initWebAuthn() {
    document.getElementById("openWindowsPasskeySettings").hidden =
      !Services.prefs.getBoolPref(
        "security.webauthn.show_ms_settings_link",
        true
      );
  },

  /**
   * Sets up the UI for the number of days of history to keep, and updates the
   * label of the "Clear Now..." button.
   */
  init() {
    initSettingGroup("nonTechnicalPrivacy");
    initSettingGroup("nonTechnicalPrivacy2");
    initSettingGroup("securityPrivacyStatus");
    initSettingGroup("securityPrivacyWarnings");
    initSettingGroup("httpsOnly");
    initSettingGroup("browsingProtection");
    initSettingGroup("cookiesAndSiteData");
    initSettingGroup("cookiesAndSiteData2");
    initSettingGroup("certificates");
    initSettingGroup("ipprotection");
    initSettingGroup("history");
    initSettingGroup("history2");
    initSettingGroup("permissions");
    initSettingGroup("dnsOverHttps");
    initSettingGroup("dnsOverHttpsAdvanced");
    initSettingGroup("etpStatus");
    initSettingGroup("etpBanner");
    initSettingGroup("etpAdvanced");
    initSettingGroup("etpReset");
    initSettingGroup("etpCustomize");

    /* Initialize Content Blocking */
    this.initContentBlocking();

    this.trackingProtectionReadPrefs();
    this.fingerprintingProtectionReadPrefs();
    this.networkCookieBehaviorReadPrefs();
    this._initTrackingProtectionExtensionControl();
    this._ensureTrackingProtectionExceptionListMigration();
    this._initProfilesInfo();

    Preferences.get("privacy.trackingprotection.enabled").on(
      "change",
      gPrivacyPane.trackingProtectionReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("privacy.trackingprotection.pbmode.enabled").on(
      "change",
      gPrivacyPane.trackingProtectionReadPrefs.bind(gPrivacyPane)
    );

    // Watch all of the prefs that the new Cookies & Site Data UI depends on
    Preferences.get("network.cookie.cookieBehavior").on(
      "change",
      gPrivacyPane.networkCookieBehaviorReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("browser.privatebrowsing.autostart").on(
      "change",
      gPrivacyPane.networkCookieBehaviorReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("privacy.firstparty.isolate").on(
      "change",
      gPrivacyPane.networkCookieBehaviorReadPrefs.bind(gPrivacyPane)
    );

    Preferences.get("privacy.fingerprintingProtection").on(
      "change",
      gPrivacyPane.fingerprintingProtectionReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("privacy.fingerprintingProtection.pbmode").on(
      "change",
      gPrivacyPane.fingerprintingProtectionReadPrefs.bind(gPrivacyPane)
    );

    setEventListener(
      "trackingProtectionExceptions",
      "command",
      gPrivacyPane.showTrackingProtectionExceptions
    );

    setEventListener(
      "dohExceptionsButton",
      "command",
      gPrivacyPane.showDoHExceptions
    );
    setEventListener(
      "passwordExceptions",
      "command",
      gPrivacyPane.showPasswordExceptions
    );
    setEventListener(
      "useMasterPassword",
      "command",
      gPrivacyPane.updateMasterPasswordButton
    );
    setEventListener(
      "changeMasterPassword",
      "command",
      gPrivacyPane.changeMasterPassword
    );
    setEventListener("showPasswords", "command", gPrivacyPane.showPasswords);

    this._pane = document.getElementById("panePrivacy");

    this._initPasswordGenerationUI();
    this._initRelayIntegrationUI();
    this._initMasterPasswordUI();
    this._initOSAuthentication();

    // Init passwords settings group
    initSettingGroup("passwords");

    this.initListenersForExtensionControllingPasswordManager();

    setSyncFromPrefListener("contentBlockingBlockCookiesCheckbox", () =>
      this.readBlockCookies()
    );
    setSyncToPrefListener("contentBlockingBlockCookiesCheckbox", () =>
      this.writeBlockCookies()
    );
    setSyncFromPrefListener("blockCookiesMenu", () =>
      this.readBlockCookiesFrom()
    );
    setSyncToPrefListener("blockCookiesMenu", () =>
      this.writeBlockCookiesFrom()
    );

    setSyncFromPrefListener("savePasswords", () => this.readSavePasswords());

    this.initSiteDataControls();

    this.initCookieBannerHandling();

    this.initDataCollection();

    if (AppConstants.MOZ_DATA_REPORTING) {
      this.updateSubmitHealthReportFromPref();
      Preferences.get(PREF_UPLOAD_ENABLED).on(
        "change",
        gPrivacyPane.updateSubmitHealthReportFromPref
      );
      setEventListener(
        "submitHealthReportBox",
        "command",
        gPrivacyPane.updateSubmitHealthReportToPref
      );
      if (AppConstants.MOZ_NORMANDY) {
        this.initOptOutStudyCheckbox();
      }
      this.initAddonRecommendationsCheckbox();
    }

    let signonBundle = document.getElementById("signonBundle");
    appendSearchKeywords("showPasswords", [
      signonBundle.getString("loginsDescriptionAll2"),
    ]);

    setEventListener(
      "contentBlockingBaselineExceptionsStrict",
      "change",
      gPrivacyPane.onBaselineCheckboxChange
    );

    setEventListener(
      "contentBlockingBaselineExceptionsCustom",
      "change",
      gPrivacyPane.onBaselineCheckboxChange
    );

    setEventListener(
      "contentBlockingConvenienceExceptionsStrict",
      "change",
      gPrivacyPane.maybeNotifyUserToReload
    );

    setEventListener(
      "contentBlockingConvenienceExceptionsCustom",
      "change",
      gPrivacyPane.maybeNotifyUserToReload
    );

    this.initDoH();

    this.initWebAuthn();

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "privacy-pane-loaded");
  },

  initSiteDataControls() {
    SiteDataManager.updateSites();
  },

  // CONTENT BLOCKING

  /**
   * Initializes the content blocking section.
   */
  initContentBlocking() {
    setEventListener(
      "contentBlockingTrackingProtectionCheckbox",
      "command",
      this.trackingProtectionWritePrefs
    );
    setEventListener(
      "contentBlockingTrackingProtectionCheckbox",
      "command",
      this._updateTrackingProtectionUI
    );
    setEventListener(
      "contentBlockingCryptominersCheckbox",
      "command",
      this.updateCryptominingLists
    );
    setEventListener(
      "contentBlockingFingerprintersCheckbox",
      "command",
      this.updateFingerprintingLists
    );
    setEventListener(
      "trackingProtectionMenu",
      "command",
      this.trackingProtectionWritePrefs
    );
    setEventListener(
      "contentBlockingFingerprintingProtectionCheckbox",
      "command",
      e => {
        const extra = { checked: e.target.checked };
        Glean.privacyUiFppClick.checkbox.record(extra);
        this.fingerprintingProtectionWritePrefs();
      }
    );
    setEventListener("fingerprintingProtectionMenu", "command", e => {
      const extra = { value: e.target.value };
      Glean.privacyUiFppClick.menu.record(extra);
      this.fingerprintingProtectionWritePrefs();
    });
    setEventListener("standardArrow", "command", this.toggleExpansion);
    setEventListener("strictArrow", "command", this.toggleExpansion);
    setEventListener("customArrow", "command", this.toggleExpansion);

    Preferences.get("network.cookie.cookieBehavior").on(
      "change",
      gPrivacyPane.readBlockCookies.bind(gPrivacyPane)
    );
    Preferences.get("browser.contentblocking.category").on(
      "change",
      gPrivacyPane.highlightCBCategory
    );

    // If any relevant content blocking pref changes, show a warning that the changes will
    // not be implemented until they refresh their tabs.
    for (let pref of CONTENT_BLOCKING_PREFS) {
      // Skip registering change listeners for baseline and convenience allow list prefs.
      // Their UI is handled in gPrivacyPane.onBaselineCheckboxChange to prevent redundant reload
      // warnings when user toggles the checkboxes.
      if (
        pref == "privacy.trackingprotection.allow_list.baseline.enabled" ||
        pref == "privacy.trackingprotection.allow_list.convenience.enabled"
      ) {
        continue;
      }
      Preferences.get(pref).on("change", gPrivacyPane.maybeNotifyUserToReload);
      // If the value changes, run populateCategoryContents, since that change might have been
      // triggered by a default value changing in the standard category.
      Preferences.get(pref).on("change", gPrivacyPane.populateCategoryContents);
    }
    Preferences.get("urlclassifier.trackingTable").on(
      "change",
      gPrivacyPane.maybeNotifyUserToReload
    );
    for (let button of document.querySelectorAll(".reload-tabs-button")) {
      button.addEventListener("command", gPrivacyPane.reloadAllOtherTabs);
    }

    let cryptoMinersOption = document.getElementById(
      "contentBlockingCryptominersOption"
    );
    let fingerprintersOption = document.getElementById(
      "contentBlockingFingerprintersOption"
    );
    let trackingAndIsolateOption = document.querySelector(
      "#blockCookiesMenu menuitem[value='trackers-plus-isolate']"
    );
    cryptoMinersOption.hidden = !Services.prefs.getBoolPref(
      "browser.contentblocking.cryptomining.preferences.ui.enabled"
    );
    fingerprintersOption.hidden = !Services.prefs.getBoolPref(
      "browser.contentblocking.fingerprinting.preferences.ui.enabled"
    );
    let updateTrackingAndIsolateOption = () => {
      trackingAndIsolateOption.hidden =
        !Services.prefs.getBoolPref(
          "browser.contentblocking.reject-and-isolate-cookies.preferences.ui.enabled",
          false
        ) || gIsFirstPartyIsolated;
    };
    Preferences.get("privacy.firstparty.isolate").on(
      "change",
      updateTrackingAndIsolateOption
    );
    updateTrackingAndIsolateOption();

    Preferences.get("browser.contentblocking.features.strict").on(
      "change",
      this.populateCategoryContents
    );
    this.populateCategoryContents();
    this.highlightCBCategory();
    this.readBlockCookies();

    // Toggles the text "Cross-site and social media trackers" based on the
    // social tracking pref. If the pref is false, the text reads
    // "Cross-site trackers".
    const STP_COOKIES_PREF = "privacy.socialtracking.block_cookies.enabled";
    if (Services.prefs.getBoolPref(STP_COOKIES_PREF)) {
      let contentBlockOptionSocialMedia = document.getElementById(
        "blockCookiesSocialMedia"
      );

      document.l10n.setAttributes(
        contentBlockOptionSocialMedia,
        "sitedata-option-block-cross-site-tracking-cookies"
      );
    }

    Preferences.get("privacy.resistFingerprinting").on(
      "change",
      setUpContentBlockingWarnings
    );
    Preferences.get("privacy.resistFingerprinting.pbmode").on(
      "change",
      setUpContentBlockingWarnings
    );

    setUpContentBlockingWarnings();

    initTCPStandardSection();
  },

  populateCategoryContents() {
    for (let type of ["strict", "standard"]) {
      let rulesArray = [];
      let selector;
      if (type == "strict") {
        selector = "#contentBlockingOptionStrict";
        rulesArray = Services.prefs
          .getStringPref("browser.contentblocking.features.strict")
          .split(",");
        if (gIsFirstPartyIsolated) {
          let idx = rulesArray.indexOf("cookieBehavior5");
          if (idx != -1) {
            rulesArray[idx] = "cookieBehavior4";
          }
        }
      } else {
        selector = "#contentBlockingOptionStandard";
        // In standard show/hide UI items based on the default values of the relevant prefs.
        let defaults = Services.prefs.getDefaultBranch("");

        let cookieBehavior = defaults.getIntPref(
          "network.cookie.cookieBehavior"
        );
        switch (cookieBehavior) {
          case Ci.nsICookieService.BEHAVIOR_ACCEPT:
            rulesArray.push("cookieBehavior0");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
            rulesArray.push("cookieBehavior1");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT:
            rulesArray.push("cookieBehavior2");
            break;
          case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
            rulesArray.push("cookieBehavior3");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
            rulesArray.push("cookieBehavior4");
            break;
          case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
            rulesArray.push(
              gIsFirstPartyIsolated ? "cookieBehavior4" : "cookieBehavior5"
            );
            break;
        }
        let cookieBehaviorPBM = defaults.getIntPref(
          "network.cookie.cookieBehavior.pbmode"
        );
        switch (cookieBehaviorPBM) {
          case Ci.nsICookieService.BEHAVIOR_ACCEPT:
            rulesArray.push("cookieBehaviorPBM0");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
            rulesArray.push("cookieBehaviorPBM1");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT:
            rulesArray.push("cookieBehaviorPBM2");
            break;
          case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
            rulesArray.push("cookieBehaviorPBM3");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
            rulesArray.push("cookieBehaviorPBM4");
            break;
          case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
            rulesArray.push(
              gIsFirstPartyIsolated
                ? "cookieBehaviorPBM4"
                : "cookieBehaviorPBM5"
            );
            break;
        }
        rulesArray.push(
          defaults.getBoolPref(
            "privacy.trackingprotection.cryptomining.enabled"
          )
            ? "cryptoTP"
            : "-cryptoTP"
        );
        rulesArray.push(
          defaults.getBoolPref(
            "privacy.trackingprotection.fingerprinting.enabled"
          )
            ? "fp"
            : "-fp"
        );
        rulesArray.push(
          Services.prefs.getBoolPref(
            "privacy.socialtracking.block_cookies.enabled"
          )
            ? "stp"
            : "-stp"
        );
        rulesArray.push(
          defaults.getBoolPref("privacy.trackingprotection.enabled")
            ? "tp"
            : "-tp"
        );
        rulesArray.push(
          defaults.getBoolPref("privacy.trackingprotection.pbmode.enabled")
            ? "tpPrivate"
            : "-tpPrivate"
        );
      }

      // Hide all cookie options first, until we learn which one should be showing.
      document.querySelector(selector + " .all-cookies-option").hidden = true;
      document.querySelector(selector + " .unvisited-cookies-option").hidden =
        true;
      document.querySelector(selector + " .cross-site-cookies-option").hidden =
        true;
      document.querySelector(
        selector + " .third-party-tracking-cookies-option"
      ).hidden = true;
      document.querySelector(
        selector + " .all-third-party-cookies-private-windows-option"
      ).hidden = true;
      document.querySelector(
        selector + " .all-third-party-cookies-option"
      ).hidden = true;
      document.querySelector(selector + " .social-media-option").hidden = true;

      for (let item of rulesArray) {
        // Note "cookieBehavior0", will result in no UI changes, so is not listed here.
        switch (item) {
          case "tp":
            document.querySelector(selector + " .trackers-option").hidden =
              false;
            break;
          case "-tp":
            document.querySelector(selector + " .trackers-option").hidden =
              true;
            break;
          case "tpPrivate":
            document.querySelector(selector + " .pb-trackers-option").hidden =
              false;
            break;
          case "-tpPrivate":
            document.querySelector(selector + " .pb-trackers-option").hidden =
              true;
            break;
          case "fp":
            document.querySelector(
              selector + " .fingerprinters-option"
            ).hidden = false;
            break;
          case "-fp":
            document.querySelector(
              selector + " .fingerprinters-option"
            ).hidden = true;
            break;
          case "cryptoTP":
            document.querySelector(selector + " .cryptominers-option").hidden =
              false;
            break;
          case "-cryptoTP":
            document.querySelector(selector + " .cryptominers-option").hidden =
              true;
            break;
          case "stp": {
            // Store social tracking cookies pref
            const STP_COOKIES_PREF =
              "privacy.socialtracking.block_cookies.enabled";

            if (Services.prefs.getBoolPref(STP_COOKIES_PREF)) {
              document.querySelector(
                selector + " .social-media-option"
              ).hidden = false;
            }
            break;
          }
          case "-stp":
            // Store social tracking cookies pref
            document.querySelector(selector + " .social-media-option").hidden =
              true;
            break;
          case "cookieBehavior1":
            document.querySelector(
              selector + " .all-third-party-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehavior2":
            document.querySelector(selector + " .all-cookies-option").hidden =
              false;
            break;
          case "cookieBehavior3":
            document.querySelector(
              selector + " .unvisited-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehavior4":
            document.querySelector(
              selector + " .third-party-tracking-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehavior5":
            document.querySelector(
              selector + " .cross-site-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehaviorPBM5":
            // We only need to show the cookie option for private windows if the
            // cookieBehaviors are different between regular windows and private
            // windows.
            if (!rulesArray.includes("cookieBehavior5")) {
              document.querySelector(
                selector + " .all-third-party-cookies-private-windows-option"
              ).hidden = false;
            }
            break;
        }
      }
      // Hide the "tracking protection in private browsing" list item
      // if the "tracking protection enabled in all windows" list item is showing.
      if (!document.querySelector(selector + " .trackers-option").hidden) {
        document.querySelector(selector + " .pb-trackers-option").hidden = true;
      }
    }
  },

  highlightCBCategory() {
    let value = Preferences.get("browser.contentblocking.category").value;
    let standardEl = document.getElementById("contentBlockingOptionStandard");
    let strictEl = document.getElementById("contentBlockingOptionStrict");
    let customEl = document.getElementById("contentBlockingOptionCustom");
    standardEl.classList.remove("selected");
    strictEl.classList.remove("selected");
    customEl.classList.remove("selected");

    switch (value) {
      case "strict":
        strictEl.classList.add("selected");
        break;
      case "custom":
        customEl.classList.add("selected");
        break;
      case "standard":
      /* fall through */
      default:
        standardEl.classList.add("selected");
        break;
    }
  },

  updateCryptominingLists() {
    let listPrefs = [
      "urlclassifier.features.cryptomining.blacklistTables",
      "urlclassifier.features.cryptomining.whitelistTables",
    ];

    let listValue = listPrefs
      .map(l => Services.prefs.getStringPref(l))
      .join(",");
    listManager.forceUpdates(listValue);
  },

  updateFingerprintingLists() {
    let listPrefs = [
      "urlclassifier.features.fingerprinting.blacklistTables",
      "urlclassifier.features.fingerprinting.whitelistTables",
    ];

    let listValue = listPrefs
      .map(l => Services.prefs.getStringPref(l))
      .join(",");
    listManager.forceUpdates(listValue);
  },

  // TRACKING PROTECTION MODE

  /**
   * Selects the right item of the Tracking Protection menulist and checkbox.
   */
  trackingProtectionReadPrefs() {
    let enabledPref = Preferences.get("privacy.trackingprotection.enabled");
    let pbmPref = Preferences.get("privacy.trackingprotection.pbmode.enabled");
    let tpMenu = document.getElementById("trackingProtectionMenu");
    let tpCheckbox = document.getElementById(
      "contentBlockingTrackingProtectionCheckbox"
    );

    this._updateTrackingProtectionUI();

    // Global enable takes precedence over enabled in Private Browsing.
    if (enabledPref.value) {
      tpMenu.value = "always";
      tpCheckbox.checked = true;
    } else if (pbmPref.value) {
      tpMenu.value = "private";
      tpCheckbox.checked = true;
    } else {
      tpMenu.value = "never";
      tpCheckbox.checked = false;
    }
  },

  /**
   * Selects the right item of the Fingerprinting Protection menulist and
   * checkbox.
   */
  fingerprintingProtectionReadPrefs() {
    let enabledPref = Preferences.get("privacy.fingerprintingProtection");
    let pbmPref = Preferences.get("privacy.fingerprintingProtection.pbmode");
    let fppMenu = document.getElementById("fingerprintingProtectionMenu");
    let fppCheckbox = document.getElementById(
      "contentBlockingFingerprintingProtectionCheckbox"
    );

    // Global enable takes precedence over enabled in Private Browsing.
    if (enabledPref.value) {
      fppMenu.value = "always";
      fppCheckbox.checked = true;
    } else if (pbmPref.value) {
      fppMenu.value = "private";
      fppCheckbox.checked = true;
    } else {
      fppMenu.value = "never";
      fppCheckbox.checked = false;
    }
    fppMenu.disabled = !fppCheckbox.checked || enabledPref.locked;
    fppCheckbox.disabled = enabledPref.locked;
  },

  /**
   * Selects the right items of the new Cookies & Site Data UI.
   */
  networkCookieBehaviorReadPrefs() {
    let behavior = Services.cookies.getCookieBehavior(false);
    let blockCookiesMenu = document.getElementById("blockCookiesMenu");
    let blockCookies = behavior != Ci.nsICookieService.BEHAVIOR_ACCEPT;
    let cookieBehaviorLocked = Services.prefs.prefIsLocked(
      "network.cookie.cookieBehavior"
    );
    let blockCookiesControlsDisabled = !blockCookies || cookieBehaviorLocked;
    blockCookiesMenu.disabled = blockCookiesControlsDisabled;

    switch (behavior) {
      case Ci.nsICookieService.BEHAVIOR_ACCEPT:
        break;
      case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
        blockCookiesMenu.value = "all-third-parties";
        break;
      case Ci.nsICookieService.BEHAVIOR_REJECT:
        blockCookiesMenu.value = "always";
        break;
      case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
        blockCookiesMenu.value = "unvisited";
        break;
      case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
        blockCookiesMenu.value = "trackers";
        break;
      case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
        blockCookiesMenu.value = "trackers-plus-isolate";
        break;
    }
  },

  /**
   * Sets the pref values based on the selected item of the radiogroup.
   */
  trackingProtectionWritePrefs() {
    let enabledPref = Preferences.get("privacy.trackingprotection.enabled");
    let pbmPref = Preferences.get("privacy.trackingprotection.pbmode.enabled");
    let stpPref = Preferences.get(
      "privacy.trackingprotection.socialtracking.enabled"
    );
    let stpCookiePref = Preferences.get(
      "privacy.socialtracking.block_cookies.enabled"
    );
    // Currently, we don't expose the email tracking protection setting on our
    // privacy UI. Instead, we use the existing tracking protection checkbox to
    // control the email tracking protection.
    let emailTPPref = Preferences.get(
      "privacy.trackingprotection.emailtracking.enabled"
    );
    let emailTPPBMPref = Preferences.get(
      "privacy.trackingprotection.emailtracking.pbmode.enabled"
    );
    let tpMenu = document.getElementById("trackingProtectionMenu");
    let tpCheckbox = document.getElementById(
      "contentBlockingTrackingProtectionCheckbox"
    );

    let value;
    if (tpCheckbox.checked) {
      if (tpMenu.value == "never") {
        tpMenu.value = "private";
      }
      value = tpMenu.value;
    } else {
      tpMenu.value = "never";
      value = "never";
    }

    switch (value) {
      case "always":
        enabledPref.value = true;
        pbmPref.value = true;
        emailTPPref.value = true;
        emailTPPBMPref.value = true;
        if (stpCookiePref.value) {
          stpPref.value = true;
        }
        break;
      case "private":
        enabledPref.value = false;
        pbmPref.value = true;
        emailTPPref.value = false;
        emailTPPBMPref.value = true;
        if (stpCookiePref.value) {
          stpPref.value = false;
        }
        break;
      case "never":
        enabledPref.value = false;
        pbmPref.value = false;
        emailTPPref.value = false;
        emailTPPBMPref.value = false;
        if (stpCookiePref.value) {
          stpPref.value = false;
        }
        break;
    }
  },

  fingerprintingProtectionWritePrefs() {
    let enabledPref = Preferences.get("privacy.fingerprintingProtection");
    let pbmPref = Preferences.get("privacy.fingerprintingProtection.pbmode");
    let fppMenu = document.getElementById("fingerprintingProtectionMenu");
    let fppCheckbox = document.getElementById(
      "contentBlockingFingerprintingProtectionCheckbox"
    );

    let value;
    if (fppCheckbox.checked) {
      if (fppMenu.value == "never") {
        fppMenu.value = "private";
      }
      value = fppMenu.value;
    } else {
      fppMenu.value = "never";
      value = "never";
    }

    fppMenu.disabled = !fppCheckbox.checked;

    switch (value) {
      case "always":
        enabledPref.value = true;
        pbmPref.value = true;
        break;
      case "private":
        enabledPref.value = false;
        pbmPref.value = true;
        break;
      case "never":
        enabledPref.value = false;
        pbmPref.value = false;
        break;
    }
  },

  toggleExpansion(e) {
    let carat = e.target;
    carat.classList.toggle("up");
    carat.closest(".privacy-detailedoption").classList.toggle("expanded");
    carat.setAttribute(
      "aria-expanded",
      carat.getAttribute("aria-expanded") === "false"
    );
  },

  // CLEAR PRIVATE DATA

  /*
   * Preferences:
   *
   * privacy.sanitize.sanitizeOnShutdown
   * - true if the user's private data is cleared on startup according to the
   *   Clear Private Data settings, false otherwise
   */

  /**
   * Displays the Clear Private Data settings dialog.
   */
  showClearPrivateDataSettings() {
    let dialogFile = "chrome://browser/content/sanitize_v2.xhtml";

    gSubDialog.open(
      dialogFile,
      {
        features: "resizable=no",
      },
      {
        mode: "clearOnShutdown",
      }
    );
  },

  /**
   * Displays a dialog from which individual parts of private data may be
   * cleared.
   */
  clearPrivateDataNow(aClearEverything) {
    var ts = Preferences.get("privacy.sanitize.timeSpan");
    var timeSpanOrig = ts.value;

    if (aClearEverything) {
      ts.value = 0;
    }

    let dialogFile = "chrome://browser/content/sanitize_v2.xhtml";

    gSubDialog.open(dialogFile, {
      features: "resizable=no",
      closingCallback: () => {
        // reset the timeSpan pref
        if (aClearEverything) {
          ts.value = timeSpanOrig;
        }

        Services.obs.notifyObservers(null, "clear-private-data");
      },
    });
  },

  /*
   Checks if the user set cleaning prefs that do not belong to DeleteOnClose
   */
  _isCustomCleaningPrefPresent() {
    let sanitizeOnShutdownPrefsArray = SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2;

    return sanitizeOnShutdownPrefsArray.some(
      pref => Preferences.get(pref).value
    );
  },

  /**
   * Displays fine-grained, per-site preferences for tracking protection.
   */
  showTrackingProtectionExceptions() {
    let params = {
      permissionType: "trackingprotection",
      disableETPVisible: true,
      prefilledHost: "",
      hideStatusColumn: true,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  // COOKIES AND SITE DATA

  /*
   * Preferences:
   *
   * network.cookie.cookieBehavior
   * - determines how the browser should handle cookies:
   *     0   means enable all cookies
   *     1   means reject all third party cookies
   *     2   means disable all cookies
   *     3   means reject third party cookies unless at least one is already set for the eTLD
   *     4   means reject all trackers
   *     5   means reject all trackers and partition third-party cookies
   *         see netwerk/cookie/src/CookieService.cpp for details
   */

  /**
   * Reads the network.cookie.cookieBehavior preference value and
   * enables/disables the "blockCookiesMenu" menulist accordingly.
   */
  readBlockCookies() {
    let bcControl = document.getElementById("blockCookiesMenu");
    bcControl.disabled =
      Services.cookies.getCookieBehavior(false) ==
      Ci.nsICookieService.BEHAVIOR_ACCEPT;
  },

  /**
   * Updates the "accept third party cookies" menu based on whether the
   * "contentBlockingBlockCookiesCheckbox" checkbox is checked.
   */
  writeBlockCookies() {
    let block = document.getElementById("contentBlockingBlockCookiesCheckbox");
    let blockCookiesMenu = document.getElementById("blockCookiesMenu");

    if (block.checked) {
      // Automatically select 'third-party trackers' as the default.
      blockCookiesMenu.selectedIndex = 0;
      return this.writeBlockCookiesFrom();
    }
    return Ci.nsICookieService.BEHAVIOR_ACCEPT;
  },

  readBlockCookiesFrom() {
    switch (Services.cookies.getCookieBehavior(false)) {
      case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
        return "all-third-parties";
      case Ci.nsICookieService.BEHAVIOR_REJECT:
        return "always";
      case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
        return "unvisited";
      case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
        return "trackers";
      case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
        return "trackers-plus-isolate";
      default:
        return undefined;
    }
  },

  writeBlockCookiesFrom() {
    let block = document.getElementById("blockCookiesMenu").selectedItem;
    switch (block.value) {
      case "trackers":
        return Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER;
      case "unvisited":
        return Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN;
      case "always":
        return Ci.nsICookieService.BEHAVIOR_REJECT;
      case "all-third-parties":
        return Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN;
      case "trackers-plus-isolate":
        return Ci.nsICookieService
          .BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN;
      default:
        return undefined;
    }
  },

  /**
   * Discard the browsers of all tabs in all windows. Pinned tabs, as
   * well as tabs for which discarding doesn't succeed (e.g. selected
   * tabs, tabs with beforeunload listeners), are reloaded.
   */
  reloadAllOtherTabs() {
    let ourTab = window.browsingContext.topChromeWindow.gBrowser.selectedTab;
    BrowserWindowTracker.orderedWindows.forEach(win => {
      let otherGBrowser = win.gBrowser;
      for (let tab of otherGBrowser.tabs) {
        if (tab == ourTab) {
          // Don't reload our preferences tab.
          continue;
        }

        if (tab.pinned || tab.selected) {
          otherGBrowser.reloadTab(tab);
        } else {
          otherGBrowser.discardBrowser(tab);
        }
      }
    });

    for (let notification of document.querySelectorAll(".reload-tabs")) {
      notification.hidden = true;
    }

    Preferences.getSetting("reloadTabsHint").value = false;
  },

  /**
   * If there are more tabs than just the preferences tab, show a warning to the user that
   * they need to reload their tabs to apply the setting.
   */
  maybeNotifyUserToReload() {
    let shouldShow = false;
    if (window.BrowserWindowTracker.orderedWindows.length > 1) {
      shouldShow = true;
    } else {
      let tabbrowser = window.browsingContext.topChromeWindow.gBrowser;
      if (tabbrowser.tabs.length > 1) {
        shouldShow = true;
      }
    }
    if (shouldShow) {
      for (let notification of document.querySelectorAll(".reload-tabs")) {
        notification.hidden = false;
      }
    }

    Preferences.getSetting("reloadTabsHint").value = true;
  },

  /**
   * Displays per-site preferences for HTTPS-Only Mode exceptions.
   */
  showHttpsOnlyModeExceptions() {
    var params = {
      blockVisible: false,
      sessionVisible: true,
      allowVisible: false,
      prefilledHost: "",
      permissionType: "https-only-load-insecure",
      forcedHTTP: true,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  showDoHExceptions() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/dohExceptions.xhtml",
      undefined
    );
  },

  /**
   * Initializes the cookie banner handling subgroup on the privacy pane.
   *
   * This UI is shown if the "cookiebanners.ui.desktop.enabled" pref is true.
   *
   * The cookie banner handling checkbox reflects the cookie banner feature
   * state. It is enabled when the service enabled via the
   * cookiebanners.service.mode pref. If detection-only mode is enabled the
   * checkbox is unchecked, since in this mode no banners are handled. It is
   * only used for detection for banners which means we may prompt the user to
   * enable the feature via other UI surfaces such as the onboarding doorhanger.
   *
   * If the user checks the checkbox, the pref value is set to
   * nsICookieBannerService.MODE_REJECT_OR_ACCEPT.
   *
   * If the user unchecks the checkbox, the mode pref value is set to
   * nsICookieBannerService.MODE_DISABLED.
   *
   * Advanced users can choose other int-valued modes via about:config.
   */
  initCookieBannerHandling() {
    setSyncFromPrefListener("handleCookieBanners", () =>
      this.readCookieBannerMode()
    );
    setSyncToPrefListener("handleCookieBanners", () =>
      this.writeCookieBannerMode()
    );

    let preference = Preferences.get("cookiebanners.ui.desktop.enabled");
    preference.on("change", () => this.updateCookieBannerHandlingVisibility());

    this.updateCookieBannerHandlingVisibility();
  },

  /**
   * Reads the cookiebanners.service.mode.privateBrowsing pref,
   * interpreting the multiple modes as a true/false value
   */
  readCookieBannerMode() {
    return (
      Preferences.get("cookiebanners.service.mode.privateBrowsing").value !=
      Ci.nsICookieBannerService.MODE_DISABLED
    );
  },

  /**
   * Translates user clicks on the cookie banner handling checkbox to the
   * corresponding integer-valued cookie banner mode preference.
   */
  writeCookieBannerMode() {
    let checkbox = document.getElementById("handleCookieBanners");
    if (!checkbox.checked) {
      /* because we removed UI control for the non-PBM pref, disabling it here
         provides an off-ramp for profiles where it had previously been enabled from the UI */
      Services.prefs.setIntPref(
        "cookiebanners.service.mode",
        Ci.nsICookieBannerService.MODE_DISABLED
      );
      return Ci.nsICookieBannerService.MODE_DISABLED;
    }
    return Ci.nsICookieBannerService.MODE_REJECT;
  },

  /**
   * Shows or hides the cookie banner handling section based on the value of
   * the "cookiebanners.ui.desktop.enabled" pref.
   */
  updateCookieBannerHandlingVisibility() {
    let groupbox = document.getElementById("cookieBannerHandlingGroup");
    let isEnabled = Preferences.get("cookiebanners.ui.desktop.enabled").value;

    // Because the top-level pane showing code unsets the hidden attribute, we
    // manually hide the section when cookie banner handling is preffed off.
    if (isEnabled) {
      groupbox.removeAttribute("style");
    } else {
      groupbox.setAttribute("style", "display: none !important");
    }
  },

  // GEOLOCATION

  /**
   * Displays the location exceptions dialog where specific site location
   * preferences can be set.
   */
  showLocationExceptions() {
    let params = { permissionType: "geo" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // LOCALHOST

  /**
   * Displays the localhost exceptions dialog where specific site localhost
   * preferences can be set.
   */
  showLocalHostExceptions() {
    let params = { permissionType: "localhost" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // LOCAL-NETWORK

  /**
   * Displays the local network exceptions dialog where specific site local network
   * preferences can be set.
   */
  showLocalNetworkExceptions() {
    let params = { permissionType: "local-network" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // XR

  /**
   * Displays the XR exceptions dialog where specific site XR
   * preferences can be set.
   */
  showXRExceptions() {
    let params = { permissionType: "xr" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // CAMERA

  /**
   * Displays the camera exceptions dialog where specific site camera
   * preferences can be set.
   */
  showCameraExceptions() {
    let params = { permissionType: "camera" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // MICROPHONE

  /**
   * Displays the microphone exceptions dialog where specific site microphone
   * preferences can be set.
   */
  showMicrophoneExceptions() {
    let params = { permissionType: "microphone" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // SPEAKER

  /**
   * Displays the speaker exceptions dialog where specific site speaker
   * preferences can be set.
   */
  showSpeakerExceptions() {
    let params = { permissionType: "speaker" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // NOTIFICATIONS

  /**
   * Displays the notifications exceptions dialog where specific site notification
   * preferences can be set.
   */
  showNotificationExceptions() {
    let params = { permissionType: "desktop-notification" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // MEDIA

  showAutoplayMediaExceptions() {
    var params = { permissionType: "autoplay-media" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // POP-UPS

  /**
   * Displays the popup exceptions dialog where specific site popup preferences
   * can be set.
   */
  showPopupExceptions() {
    var params = {
      blockVisible: false,
      sessionVisible: false,
      allowVisible: true,
      prefilledHost: "",
      permissionType: "popup",
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // UTILITY FUNCTIONS

  /**
   * Utility function to enable/disable the button specified by aButtonID based
   * on the value of the Boolean preference specified by aPreferenceID.
   */
  updateButtons(aButtonID, aPreferenceID) {
    var button = document.getElementById(aButtonID);
    var preference = Preferences.get(aPreferenceID);
    button.disabled = !preference.value || preference.locked;
    return undefined;
  },

  // BEGIN UI CODE

  /*
   * Preferences:
   *
   * dom.disable_open_during_load
   * - true if popups are blocked by default, false otherwise
   */

  // POP-UPS

  /**
   * Displays a dialog in which the user can view and modify the list of sites
   * where passwords are never saved.
   */
  showPasswordExceptions() {
    var params = {
      blockVisible: true,
      sessionVisible: false,
      allowVisible: false,
      hideStatusColumn: true,
      prefilledHost: "",
      permissionType: "login-saving",
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  /**
   * Initializes master password UI: the "use master password" checkbox, selects
   * the master password button to show, and enables/disables it as necessary.
   * The master password is controlled by various bits of NSS functionality, so
   * the UI for it can't be controlled by the normal preference bindings.
   */
  _initMasterPasswordUI() {
    var noMP = !LoginHelper.isPrimaryPasswordSet();

    var button = document.getElementById("changeMasterPassword");
    button.disabled = noMP;

    var checkbox = document.getElementById("useMasterPassword");
    checkbox.checked = !noMP;
    checkbox.disabled =
      (noMP && !Services.policies.isAllowed("createMasterPassword")) ||
      (!noMP && !Services.policies.isAllowed("removeMasterPassword"));
  },

  /**
   * Enables/disables the master password button depending on the state of the
   * "use master password" checkbox, and prompts for master password removal if
   * one is set.
   */
  async updateMasterPasswordButton() {
    var checkbox = document.getElementById("useMasterPassword");
    var button = document.getElementById("changeMasterPassword");
    button.disabled = !checkbox.checked;

    // unchecking the checkbox should try to immediately remove the master
    // password, because it's impossible to non-destructively remove the master
    // password used to encrypt all the passwords without providing it (by
    // design), and it would be extremely odd to pop up that dialog when the
    // user closes the prefwindow and saves his settings
    if (!checkbox.checked) {
      await this._removeMasterPassword();
    } else {
      await this.changeMasterPassword();
    }

    this._initMasterPasswordUI();
  },

  /**
   * Displays the "remove master password" dialog to allow the user to remove
   * the current master password.  When the dialog is dismissed, master password
   * UI is automatically updated.
   */
  async _removeMasterPassword() {
    const fipsUtils = Cc["@mozilla.org/security/fipsutils;1"].getService(
      Ci.nsIFIPSUtils
    );
    if (fipsUtils.isFIPSEnabled) {
      let title = document.getElementById("fips-title").textContent;
      let desc = document.getElementById("fips-desc").textContent;
      Services.prompt.alert(window, title, desc);
      this._initMasterPasswordUI();
    } else {
      gSubDialog.open("chrome://mozapps/content/preferences/removemp.xhtml", {
        closingCallback: () => {
          Services.obs.notifyObservers(null, "passwordmgr-primary-pw-changed");
          this._initMasterPasswordUI();
        },
      });
    }
  },

  /**
   * Displays a dialog in which the primary password may be changed.
   */
  async changeMasterPassword() {
    // Require OS authentication before the user can set a Primary Password.
    // OS reauthenticate functionality is not available on Linux yet (bug 1527745)
    if (!LoginHelper.isPrimaryPasswordSet() && LoginHelper.getOSAuthEnabled()) {
      // Uses primary-password-os-auth-dialog-message-win and
      // primary-password-os-auth-dialog-message-macosx via concatenation:
      let messageId =
        "primary-password-os-auth-dialog-message-" + AppConstants.platform;
      let [messageText, captionText] = await document.l10n.formatMessages([
        {
          id: messageId,
        },
        {
          id: "master-password-os-auth-dialog-caption",
        },
      ]);
      let win = Services.wm.getMostRecentBrowserWindow();

      // Note on Glean collection: because OSKeyStore.ensureLoggedIn() is not wrapped in
      // verifyOSAuth(), it will be documenting "success" for unsupported platforms
      // and won't record "fail_error", only "fail_user_canceled"
      let loggedIn = await OSKeyStore.ensureLoggedIn(
        messageText.value,
        captionText.value,
        win,
        false
      );

      const result = loggedIn.authenticated ? "success" : "fail_user_canceled";
      Glean.pwmgr.promptShownOsReauth.record({
        trigger: "toggle_pref_primary_password",
        result,
      });

      if (!loggedIn.authenticated) {
        return;
      }
    }

    gSubDialog.open("chrome://mozapps/content/preferences/changemp.xhtml", {
      features: "resizable=no",
      closingCallback: () => {
        Services.obs.notifyObservers(null, "passwordmgr-primary-pw-changed");
        this._initMasterPasswordUI();
      },
    });
  },

  /**
   * Set up the initial state for the password generation UI.
   * It will be hidden unless the .available pref is true
   */
  _initPasswordGenerationUI() {
    // we don't watch the .available pref for runtime changes
    let prefValue = Services.prefs.getBoolPref(
      PREF_PASSWORD_GENERATION_AVAILABLE,
      false
    );
    document.getElementById("generatePasswordsBox").hidden = !prefValue;
  },

  toggleRelayIntegration() {
    const checkbox = document.getElementById("relayIntegration");
    if (checkbox.checked) {
      FirefoxRelay.markAsAvailable();
      Glean.relayIntegration.enabledPrefChange.record();
    } else {
      FirefoxRelay.markAsDisabled();
      Glean.relayIntegration.disabledPrefChange.record();
    }
  },

  _updateRelayIntegrationUI() {
    document.getElementById("relayIntegrationBox").hidden =
      !FirefoxRelay.isAvailable;
    document.getElementById("relayIntegration").checked =
      FirefoxRelay.isAvailable && !FirefoxRelay.isDisabled;
  },

  _initRelayIntegrationUI() {
    document
      .getElementById("relayIntegrationLearnMoreLink")
      .setAttribute("href", FirefoxRelay.learnMoreUrl);

    setEventListener(
      "relayIntegration",
      "command",
      gPrivacyPane.toggleRelayIntegration.bind(gPrivacyPane)
    );
    Preferences.get("signon.firefoxRelay.feature").on(
      "change",
      gPrivacyPane._updateRelayIntegrationUI.bind(gPrivacyPane)
    );

    this._updateRelayIntegrationUI();
  },

  async _toggleOSAuth() {
    let osReauthCheckbox = document.getElementById("osReauthCheckbox");

    const messageText = await lazy.AboutLoginsL10n.formatValue(
      "about-logins-os-auth-dialog-message"
    );
    const captionText = await lazy.AboutLoginsL10n.formatValue(
      "about-logins-os-auth-dialog-caption"
    );
    let win =
      osReauthCheckbox.ownerGlobal.docShell.chromeEventHandler.ownerGlobal;

    // Calling OSKeyStore.ensureLoggedIn() instead of LoginHelper.verifyOSAuth()
    // since we want to authenticate user each time this setting is changed.

    // Note on Glean collection: because OSKeyStore.ensureLoggedIn() is not wrapped in
    // verifyOSAuth(), it will be documenting "success" for unsupported platforms
    // and won't record "fail_error", only "fail_user_canceled"
    let isAuthorized = (
      await OSKeyStore.ensureLoggedIn(messageText, captionText, win, false)
    ).authenticated;

    Glean.pwmgr.promptShownOsReauth.record({
      trigger: "toggle_pref_os_auth",
      result: isAuthorized ? "success" : "fail_user_canceled",
    });

    if (!isAuthorized) {
      osReauthCheckbox.checked = !osReauthCheckbox.checked;
      return;
    }

    // If osReauthCheckbox is checked enable osauth.
    LoginHelper.setOSAuthEnabled(osReauthCheckbox.checked);

    Glean.pwmgr.requireOsReauthToggle.record({
      toggle_state: osReauthCheckbox.checked,
    });
  },

  _initOSAuthentication() {
    let osReauthCheckbox = document.getElementById("osReauthCheckbox");
    if (
      !OSKeyStore.canReauth() ||
      Services.prefs.getBoolPref("security.nocertdb", false)
    ) {
      osReauthCheckbox.hidden = true;
      return;
    }

    osReauthCheckbox.toggleAttribute("checked", LoginHelper.getOSAuthEnabled());

    setEventListener(
      "osReauthCheckbox",
      "command",
      gPrivacyPane._toggleOSAuth.bind(gPrivacyPane)
    );
  },

  /**
   * Shows the sites where the user has saved passwords and the associated login
   * information.
   */
  showPasswords() {
    let loginManager = window.windowGlobalChild.getActor("LoginManager");
    loginManager.sendAsyncMessage("PasswordManager:OpenPreferences", {
      entryPoint: "Preferences",
    });
  },

  /**
   * Enables/disables dependent controls related to password saving
   * When password saving is not enabled, we need to also disable the password generation checkbox
   * The Exceptions button is used to configure sites where passwords are never saved.
   */
  readSavePasswords() {
    var prefValue = Preferences.get("signon.rememberSignons").value;
    document.getElementById("passwordExceptions").disabled = !prefValue;
    document.getElementById("generatePasswords").disabled = !prefValue;
    document.getElementById("passwordAutofillCheckbox").disabled = !prefValue;
    document.getElementById("relayIntegration").disabled =
      !prefValue || Services.prefs.prefIsLocked("signon.firefoxRelay.feature");
    // don't override pref value in UI
    return undefined;
  },

  /**
   * Initalizes pref listeners for the password manager.
   *
   * This ensures that the user is always notified if an extension is controlling the password manager.
   */
  initListenersForExtensionControllingPasswordManager() {
    this._passwordManagerCheckbox = document.getElementById("savePasswords");
    this._disableExtensionButton = document.getElementById(
      "disablePasswordManagerExtension"
    );

    this._disableExtensionButton.addEventListener(
      "command",
      makeDisableControllingExtension(
        PREF_SETTING_TYPE,
        PASSWORD_MANAGER_PREF_ID
      )
    );

    initListenersForPrefChange(
      PREF_SETTING_TYPE,
      PASSWORD_MANAGER_PREF_ID,
      this._passwordManagerCheckbox
    );
  },

  /**
   * Displays the exceptions lists for add-on installation warnings.
   */
  showAddonExceptions() {
    var params = this._addonParams;

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  /**
   * Parameters for the add-on install permissions dialog.
   */
  _addonParams: {
    blockVisible: false,
    sessionVisible: false,
    allowVisible: true,
    prefilledHost: "",
    permissionType: "install",
  },

  /**
   * Displays the user's certificates and associated options.
   */
  showCertificates() {
    gSubDialog.open("chrome://pippki/content/certManager.xhtml");
  },

  /**
   * Displays a dialog from which the user can manage his security devices.
   */
  showSecurityDevices() {
    gSubDialog.open("chrome://pippki/content/device_manager.xhtml");
  },

  initDataCollection() {
    if (
      !AppConstants.MOZ_DATA_REPORTING &&
      !Services.prefs.getBoolPref(
        "browser.privacySegmentation.preferences.show"
      )
    ) {
      // Nothing to control in the data collection section, remove it.
      document.getElementById("dataCollectionCategory").remove();
      document.getElementById("dataCollectionGroup").remove();
      return;
    }

    this._setupLearnMoreLink(
      "toolkit.datacollection.infoURL",
      "dataCollectionPrivacyNotice"
    );
    this.initPrivacySegmentation();
  },

  initPrivacySegmentation() {
    // Section visibility
    let section = document.getElementById("privacySegmentationSection");
    let updatePrivacySegmentationSectionVisibilityState = () => {
      section.hidden = !Services.prefs.getBoolPref(
        "browser.privacySegmentation.preferences.show"
      );
    };

    Services.prefs.addObserver(
      "browser.privacySegmentation.preferences.show",
      updatePrivacySegmentationSectionVisibilityState
    );

    window.addEventListener("unload", () => {
      Services.prefs.removeObserver(
        "browser.privacySegmentation.preferences.show",
        updatePrivacySegmentationSectionVisibilityState
      );
    });

    updatePrivacySegmentationSectionVisibilityState();
  },

  /**
   * Set up or hide the Learn More links for various data collection options
   */
  _setupLearnMoreLink(pref, element) {
    // set up the Learn More link with the correct URL
    let url = Services.urlFormatter.formatURLPref(pref);
    let el = document.getElementById(element);

    if (url) {
      el.setAttribute("href", url);
    } else {
      el.hidden = true;
    }
  },

  /**
   * Update the health report service checkbox from preference.
   */
  updateSubmitHealthReportFromPref() {
    let checkbox = document.getElementById("submitHealthReportBox");
    let telemetryContainer = document.getElementById("telemetry-container");

    // Telemetry is only sending data if MOZ_TELEMETRY_REPORTING is defined.
    // We still want to display the preferences panel if that's not the case, but
    // we want it to be disabled and unchecked.
    if (
      Services.prefs.prefIsLocked(PREF_UPLOAD_ENABLED) ||
      !AppConstants.MOZ_TELEMETRY_REPORTING
    ) {
      checkbox.setAttribute("disabled", "true");
      return;
    }

    checkbox.checked =
      Services.prefs.getBoolPref(PREF_UPLOAD_ENABLED) &&
      AppConstants.MOZ_TELEMETRY_REPORTING;
    telemetryContainer.hidden = checkbox.checked;
  },

  /**
   * Update the health report preference with state from checkbox.
   */
  updateSubmitHealthReportToPref() {
    let checkbox = document.getElementById("submitHealthReportBox");
    let telemetryContainer = document.getElementById("telemetry-container");

    Services.prefs.setBoolPref(PREF_UPLOAD_ENABLED, checkbox.checked);
    telemetryContainer.hidden = checkbox.checked;
  },

  /**
   * Initialize the opt-out-study preference checkbox into about:preferences and
   * handles events coming from the UI for it.
   */
  initOptOutStudyCheckbox() {
    // The checkbox should be disabled if any of the below are true. This
    // prevents the user from changing the value in the box.
    //
    // * the policy forbids shield
    // * Normandy is disabled
    //
    // The checkbox should match the value of the preference only if all of
    // these are true. Otherwise, the checkbox should remain unchecked. This
    // is because in these situations, Shield studies are always disabled, and
    // so showing a checkbox would be confusing.
    //
    // * the policy allows Shield
    // * Normandy is enabled

    const allowedByPolicy = Services.policies.isAllowed("Shield");
    const checkbox = document.getElementById("optOutStudiesEnabled");

    function updateCheckbox() {
      if (
        allowedByPolicy &&
        Services.prefs.getBoolPref(PREF_UPLOAD_ENABLED, false) &&
        Services.prefs.getBoolPref(PREF_NORMANDY_ENABLED, false)
      ) {
        checkbox.toggleAttribute(
          "checked",
          Services.prefs.getBoolPref(PREF_OPT_OUT_STUDIES_ENABLED, false)
        );
        checkbox.setAttribute("preference", PREF_OPT_OUT_STUDIES_ENABLED);
        checkbox.removeAttribute("disabled");
      } else {
        checkbox.removeAttribute("preference");
        checkbox.removeAttribute("checked");
        checkbox.setAttribute("disabled", "true");
      }
    }
    Preferences.get(PREF_UPLOAD_ENABLED).on("change", updateCheckbox);
    updateCheckbox();
  },

  initAddonRecommendationsCheckbox() {
    // Setup the checkbox.
    dataCollectionCheckboxHandler({
      checkbox: document.getElementById("addonRecommendationEnabled"),
      pref: PREF_ADDON_RECOMMENDATIONS_ENABLED,
    });
  },

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "network:trr-uri-changed":
      case "network:trr-mode-changed":
      case "network:trr-confirmation":
        gPrivacyPane.updateDoHStatus();
        break;
    }
  },

  _initProfilesInfo() {
    setEventListener(
      "dataCollectionViewProfiles",
      "click",
      gMainPane.manageProfiles
    );

    let listener = () => gPrivacyPane.updateProfilesPrivacyInfo();
    SelectableProfileService.on("enableChanged", listener);
    window.addEventListener("unload", () =>
      SelectableProfileService.off("enableChanged", listener)
    );
    this.updateProfilesPrivacyInfo();
  },

  updateProfilesPrivacyInfo() {
    let profilesInfo = document.getElementById("preferences-privacy-profiles");
    profilesInfo.hidden = !SelectableProfileService.isEnabled;
  },

  /**
   * Handles change events on baseline and convenience exception checkboxes for content blocking preferences.
   *
   * - For baseline checkboxes: If the user attempts to uncheck, shows a confirmation dialog.
   *   If confirmed, disables the baseline allow list preference.
   * - For other cases: Toggles the checkbox and updates the corresponding preference.
   *
   * @param {Event} event - The change event triggered by the checkbox.
   */
  async onBaselineCheckboxChange(event) {
    // Ignore events from nested checkboxes
    if (event.target.slot === "nested") {
      return;
    }

    // If the user is checking the checkbox, don't show a confirmation prompt.
    if (event.target.checked) {
      this.maybeNotifyUserToReload();
      return;
    }

    const confirmed = await this._confirmBaselineAllowListDisable();

    if (confirmed) {
      // User confirmed, set the checkbox to false.
      event.target.checked = false;
      this.maybeNotifyUserToReload();
    } else {
      // User cancelled, set the checkbox and the baseline pref to true.
      event.target.checked = true;
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.allow_list.baseline.enabled",
        true
      );
    }
  },

  async onBaselineAllowListSettingChange(value, setting) {
    if (value) {
      this.maybeNotifyUserToReload();
      return;
    }

    const confirmed = await this._confirmBaselineAllowListDisable();
    if (confirmed) {
      this.maybeNotifyUserToReload();
      return;
    }

    setting.value = true;
  },

  async _confirmBaselineAllowListDisable() {
    let [title, body, okButtonText, cancelButtonText] =
      await document.l10n.formatValues([
        { id: "content-blocking-baseline-uncheck-warning-dialog-title" },
        { id: "content-blocking-baseline-uncheck-warning-dialog-body" },
        { id: "content-blocking-baseline-uncheck-warning-dialog-ok-button" },
        {
          id: "content-blocking-baseline-uncheck-warning-dialog-cancel-button",
        },
      ]);

    let flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_1 +
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_POS_0_DEFAULT;

    const result = await Services.prompt.asyncConfirmEx(
      window.browsingContext,
      Services.prompt.MODAL_TYPE_CONTENT,
      title,
      body,
      flags,
      cancelButtonText,
      okButtonText,
      null,
      null,
      false,
      {
        useTitle: true,
      }
    );

    const propertyBag = result.QueryInterface(Ci.nsIPropertyBag2);
    return propertyBag.get("buttonNumClicked") == 1;
  },
};
