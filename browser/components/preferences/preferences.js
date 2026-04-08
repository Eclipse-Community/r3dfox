/* - This Source Code Form is subject to the terms of the Mozilla Public
   - License, v. 2.0. If a copy of the MPL was not distributed with this file,
   - You can obtain one at http://mozilla.org/MPL/2.0/. */

// Import globals from the files imported by the .xul files.
/* import-globals-from main.js */
/* import-globals-from home.js */
/* import-globals-from search.js */
/* import-globals-from containers.js */
/* import-globals-from privacy.js */
/* import-globals-from sync.js */
/* import-globals-from experimental.js */
/* import-globals-from moreFromMozilla.js */
/* import-globals-from findInPage.js */
/* import-globals-from /browser/base/content/utilityOverlay.js */
/* import-globals-from /toolkit/content/preferencesBindings.js */

/** @import MozButton from "chrome://global/content/elements/moz-button.mjs" */
/** @import {SettingConfig, SettingEmitChange} from "chrome://global/content/preferences/Setting.mjs" */
/** @import {SettingControlConfig, SettingOptionConfig} from "chrome://browser/content/preferences/widgets/setting-control.mjs" */
/** @import {SettingGroup} from "chrome://browser/content/preferences/widgets/setting-group.mjs" */
/** @import {SettingPane, SettingPaneConfig} from "chrome://browser/content/preferences/widgets/setting-pane.mjs" */

"use strict";

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

var { Downloads } = ChromeUtils.importESModule(
  "resource://gre/modules/Downloads.sys.mjs"
);
var { Integration } = ChromeUtils.importESModule(
  "resource://gre/modules/Integration.sys.mjs"
);
/* global DownloadIntegration */
Integration.downloads.defineESModuleGetter(
  this,
  "DownloadIntegration",
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);

var { PrivateBrowsingUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
);

var { Weave } = ChromeUtils.importESModule(
  "resource://services-sync/main.sys.mjs"
);

var { FxAccounts, getFxAccountsSingleton } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccounts.sys.mjs"
);
var fxAccounts = getFxAccountsSingleton();

XPCOMUtils.defineLazyServiceGetters(this, {
  gApplicationUpdateService: [
    "@mozilla.org/updates/update-service;1",
    Ci.nsIApplicationUpdateService,
  ],

  listManager: [
    "@mozilla.org/url-classifier/listmanager;1",
    Ci.nsIUrlListManager,
  ],
  gHandlerService: [
    "@mozilla.org/uriloader/handler-service;1",
    Ci.nsIHandlerService,
  ],
  gMIMEService: ["@mozilla.org/mime;1", Ci.nsIMIMEService],
});

if (Cc["@mozilla.org/gio-service;1"]) {
  XPCOMUtils.defineLazyServiceGetter(
    this,
    "gGIOService",
    "@mozilla.org/gio-service;1",
    Ci.nsIGIOService
  );
} else {
  this.gGIOService = null;
}

ChromeUtils.defineESModuleGetters(this, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  DownloadUtils: "resource://gre/modules/DownloadUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExtensionPreferencesManager:
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  FirefoxRelay: "resource://gre/modules/FirefoxRelay.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  LangPackMatcher: "resource://gre/modules/LangPackMatcher.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SelectionChangedMenulist:
    "resource:///modules/SelectionChangedMenulist.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  SiteDataManager: "resource:///modules/SiteDataManager.sys.mjs",
  TransientPrefs: "resource:///modules/TransientPrefs.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "gSubDialog", function () {
  const { SubDialogManager } = ChromeUtils.importESModule(
    "resource://gre/modules/SubDialog.sys.mjs"
  );
  return new SubDialogManager({
    dialogStack: document.getElementById("dialogStack"),
    dialogTemplate: document.getElementById("dialogTemplate"),
    dialogOptions: {
      styleSheets: [
        "chrome://browser/skin/preferences/dialog.css",
        "chrome://browser/skin/preferences/preferences.css",
      ],
      resizeCallback: async ({ title, frame }) => {
        // Search within main document and highlight matched keyword.
        await gSearchResultsPane.searchWithinNode(
          title,
          gSearchResultsPane.query
        );

        // Search within sub-dialog document and highlight matched keyword.
        await gSearchResultsPane.searchWithinNode(
          frame.contentDocument.firstElementChild,
          gSearchResultsPane.query
        );

        // Creating tooltips for all the instances found
        for (let node of gSearchResultsPane.listSearchTooltips) {
          if (!node.tooltipNode) {
            gSearchResultsPane.createSearchTooltip(
              node,
              gSearchResultsPane.query
            );
          }
        }
      },
    },
  });
});

/** @type {Record<string, boolean>} */
const srdSectionPrefs = {};
XPCOMUtils.defineLazyPreferenceGetter(
  srdSectionPrefs,
  "all",
  "browser.settings-redesign.enabled",
  false
);

/**
 * @param {string} section
 */
function srdSectionEnabled(section) {
  if (!(section in srdSectionPrefs)) {
    XPCOMUtils.defineLazyPreferenceGetter(
      srdSectionPrefs,
      section,
      `browser.settings-redesign.${section}.enabled`,
      false
    );
  }
  return srdSectionPrefs.all || srdSectionPrefs[section];
}

var SettingPaneManager = {
  /** @type {Map<string, SettingPaneConfig>} */
  _data: new Map(),

  /**
   * @param {string} id
   */
  get(id) {
    if (!this._data.has(id)) {
      throw new Error(`Setting pane "${id}" not found`);
    }
    return this._data.get(id);
  },

  /**
   * @param {string} id
   * @param {SettingPaneConfig} config
   */
  registerPane(id, config) {
    if (this._data.has(id)) {
      throw new Error(`Setting pane "${id}" already registered`);
    }
    this._data.set(id, config);
    let subPane = friendlyPrefCategoryNameToInternalName(id);
    let settingPane = /** @type {SettingPane} */ (
      document.createElement("setting-pane")
    );
    settingPane.name = subPane;
    settingPane.config = config;
    settingPane.isSubPane = !!config.parent;
    document.getElementById("mainPrefPane").append(settingPane);
    register_module(subPane, {
      init() {
        settingPane.init();
      },
    });
  },

  /**
   * @param {Record<string, SettingPaneConfig>} paneConfigs
   */
  registerPanes(paneConfigs) {
    for (let id in paneConfigs) {
      this.registerPane(id, paneConfigs[id]);
    }
  },
};

var SettingGroupManager = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/SettingGroupManager.mjs",
  {
    global: "current",
  }
).SettingGroupManager;

/**
 * Register initial config-based setting panes here. If you need to register a
 * pane elsewhere, use {@link SettingPaneManager['registerPane']}.
 *
 * @type {Record<string, SettingPaneConfig>}
 */
const CONFIG_PANES = Object.freeze({
  dnsOverHttps: {
    parent: "privacy",
    l10nId: "preferences-doh-header2",
    groupIds: ["dnsOverHttpsAdvanced"],
  },
  managePayments: {
    parent: "privacy",
    l10nId: "autofill-payment-methods-manage-payments-title",
    groupIds: ["managePayments"],
    iconSrc: "chrome://browser/skin/payment-methods-16.svg",
  },
  paneProfiles: {
    parent: "general",
    l10nId: "preferences-profiles-group-header",
    groupIds: ["profilePane"],
  },
  etp: {
    parent: "privacy",
    l10nId: "preferences-etp-header",
    groupIds: ["etpBanner", "etpAdvanced"],
  },
  etpCustomize: {
    parent: "etp",
    l10nId: "preferences-etp-customize-header",
    groupIds: ["etpCustomize", "etpReset"],
  },
  manageAddresses: {
    parent: "privacy",
    l10nId: "autofill-addresses-manage-addresses-title",
    groupIds: ["manageAddresses"],
    iconSrc: "chrome://browser/skin/notification-icons/geo.svg",
  },
  translations: {
    parent: "general",
    l10nId: "settings-translations-subpage-header",
    groupIds: [
      "translationsAutomaticTranslation",
      "translationsDownloadLanguages",
    ],
    iconSrc: "chrome://browser/skin/translations.svg",
  },
  ai: {
    l10nId: "preferences-ai-controls-header",
    iconSrc: "chrome://global/skin/icons/highlights.svg",
    groupIds: ["aiControlsDescription", "aiFeatures", "aiStatesDescription"],
    module: "chrome://browser/content/preferences/config/aiFeatures.mjs",
    visible: () =>
      Services.prefs.getBoolPref("browser.preferences.aiControls", false),
  },
  history: {
    parent: "privacy",
    l10nId: "history-header2",
    groupIds: ["historyAdvanced"],
  },
  customHomepage: {
    parent: "home",
    l10nId: "home-custom-homepage-subpage",
    groupIds: ["customHomepage"],
  },
  personalizeSmartWindow: {
    parent: "ai",
    l10nId: "ai-window-personalize-header",
    iconSrc: "chrome://devtools/skin/images/globe.svg",
    groupIds: ["assistantModelGroup", "memoriesGroup"],
    module: "chrome://browser/content/preferences/config/aiFeatures.mjs",
  },
  manageMemories: {
    parent: "personalizeSmartWindow",
    l10nId: "ai-window-manage-memories-header",
    groupIds: ["manageMemories"],
    module: "chrome://browser/content/preferences/config/aiFeatures.mjs",
    supportPage: "smart-window-memories",
  },
});

var gLastCategory = { category: undefined, subcategory: undefined };
const gXULDOMParser = new DOMParser();
var gCategoryModules = new Map();
var gCategoryInits = new Map();

function register_module(categoryName, categoryObject) {
  gCategoryModules.set(categoryName, categoryObject);
  gCategoryInits.set(categoryName, {
    _initted: false,
    init() {
      let startTime = ChromeUtils.now();
      if (this._initted) {
        return;
      }
      this._initted = true;
      let template = document.getElementById("template-" + categoryName);
      if (template) {
        // Replace the template element with the nodes inside of it.
        template.replaceWith(template.content);

        // We've inserted elements that rely on 'preference' attributes.
        // So we need to update those by reading from the prefs.
        // The bindings will do this using idle dispatch and avoid
        // repeated runs if called multiple times before the task runs.
        Preferences.queueUpdateOfAllElements();
      }

      categoryObject.init();
      ChromeUtils.addProfilerMarker(
        "Preferences",
        { startTime },
        categoryName + " init"
      );
    },
  });
}

document.addEventListener("DOMContentLoaded", init_all, { once: true });

function init_all() {
  Preferences.forceEnableInstantApply();

  // Asks Preferences to queue an update of the attribute values of
  // the entire document.
  Preferences.queueUpdateOfAllElements();

  register_module("paneGeneral", gMainPane);
  register_module("paneHome", gHomePane);
  register_module("paneSearch", gSearchPane);
  register_module("panePrivacy", gPrivacyPane);
  register_module("paneContainers", gContainersPane);

  for (let [id, config] of Object.entries(CONFIG_PANES)) {
    SettingPaneManager.registerPane(id, config);
  }

  if (ExperimentAPI.labsEnabled) {
    // Set hidden based on previous load's hidden value or if Nimbus is
    // disabled.
    document.getElementById("category-experimental").hidden =
      Services.prefs.getBoolPref(
        "browser.preferences.experimental.hidden",
        false
      );
    register_module("paneExperimental", gExperimentalPane);
  } else {
    document.getElementById("category-experimental").hidden = true;
  }

  NimbusFeatures.moreFromMozilla.recordExposureEvent({ once: true });
  if (NimbusFeatures.moreFromMozilla.getVariable("enabled")) {
    document.getElementById("category-more-from-mozilla").hidden = false;
    gMoreFromMozillaPane.option =
      NimbusFeatures.moreFromMozilla.getVariable("template");
    register_module("paneMoreFromMozilla", gMoreFromMozillaPane);
  }
  // The Sync category needs to be the last of the "real" categories
  // registered and inititalized since many tests wait for the
  // "sync-pane-loaded" observer notification before starting the test.
  if (Services.prefs.getBoolPref("identity.fxaccounts.enabled")) {
    document.getElementById("category-sync").hidden = false;
    register_module("paneSync", gSyncPane);
  }
  register_module("paneSearchResults", gSearchResultsPane);
  gSearchResultsPane.init();
  gMainPane.preInit();

  let categories = document.getElementById("categories");
  categories.addEventListener("select", event => gotoPref(event.target.value));

  document.documentElement.addEventListener("keydown", function (event) {
    if (event.keyCode == KeyEvent.DOM_VK_TAB) {
      categories.setAttribute("keyboard-navigation", "true");
    }
  });
  categories.addEventListener("mousedown", function () {
    this.removeAttribute("keyboard-navigation");
  });

  maybeDisplayPoliciesNotice();

  window.addEventListener("hashchange", onHashChange);

  document.getElementById("focusSearch1").addEventListener("command", () => {
    gSearchResultsPane.searchInput.focus();
  });

  gotoPref().then(() => {
    document.getElementById("addonsButton").addEventListener("click", e => {
      e.preventDefault();
      if (e.button >= 2) {
        // Ignore right clicks.
        return;
      }
      let mainWindow = window.browsingContext.topChromeWindow;
      mainWindow.BrowserAddonUI.openAddonsMgr();
    });

    document.dispatchEvent(
      new CustomEvent("Initialized", {
        bubbles: true,
        cancelable: true,
      })
    );
  });
}

function onHashChange() {
  gotoPref(null, "Hash");
}

/**
 * @param {string} [aCategory] The pane to show, defaults to the hash of URL or general
 * @param {"Click"|"Initial"|"Hash"} [aShowReason]
 *   What triggered the navigation. Defaults to "Click" if aCategory is provided,
 *   otherwise "Initial".
 */
async function gotoPref(
  aCategory,
  aShowReason = aCategory ? "Click" : "Initial"
) {
  let categories = document.getElementById("categories");
  const kDefaultCategoryInternalName = "paneGeneral";
  const kDefaultCategory = "general";
  let hash = document.location.hash;
  let category = aCategory || hash.substring(1) || kDefaultCategoryInternalName;

  let breakIndex = category.indexOf("-");
  // Subcategories allow for selecting smaller sections of the preferences
  // until proper search support is enabled (bug 1353954).
  let subcategory = breakIndex != -1 && category.substring(breakIndex + 1);
  if (subcategory) {
    category = category.substring(0, breakIndex);
  }
  category = friendlyPrefCategoryNameToInternalName(category);
  if (category != "paneSearchResults") {
    gSearchResultsPane.query = null;
    gSearchResultsPane.searchInput.value = "";
    gSearchResultsPane.removeAllSearchIndicators(window, true);
  } else if (!gSearchResultsPane.searchInput.value) {
    // Something tried to send us to the search results pane without
    // a query string. Default to the General pane instead.
    category = kDefaultCategoryInternalName;
    document.location.hash = kDefaultCategory;
    gSearchResultsPane.query = null;
  }

  // Updating the hash (below) or changing the selected category
  // will re-enter gotoPref.
  if (gLastCategory.category == category && !subcategory) {
    return;
  }

  let item;
  let unknownCategory = false;
  if (category != "paneSearchResults") {
    // Hide second level headers in normal view
    for (let element of document.querySelectorAll(".search-header")) {
      element.hidden = true;
    }

    item = /** @type {HTMLElement} */ (
      categories.querySelector(".category[value=" + CSS.escape(category) + "]")
    );
    if (!item || item.hidden) {
      unknownCategory = true;
      category = kDefaultCategoryInternalName;
      item = categories.querySelector(".category[value=" + category + "]");
    }
  }

  if (
    gLastCategory.category ||
    unknownCategory ||
    category != kDefaultCategoryInternalName ||
    subcategory
  ) {
    let friendlyName = internalPrefCategoryNameToFriendlyName(category);
    // Overwrite the hash, unless there is no hash and we're switching to the
    // default category, e.g. by using the 'back' button after navigating to
    // a different category.

    // Note: Bug 1983388 - If there is an element in the DOM that has the same
    // ID as the `friendlyName`, then focus will be lost when navigating the
    // category navigation via keyboard when that `friendlyName` category is selected.
    if (
      !(!document.location.hash && category == kDefaultCategoryInternalName)
    ) {
      document.location.hash = friendlyName;
    }
  }
  // Need to set the gLastCategory before setting categories.selectedItem since
  // the categories 'select' event will re-enter the gotoPref codepath.
  gLastCategory.category = category;
  gLastCategory.subcategory = subcategory;
  if (item) {
    // @ts-ignore MozElements.RichListBox
    categories.selectedItem = item;
  } else {
    // @ts-ignore MozElements.RichListBox
    categories.clearSelection();
  }
  window.history.replaceState(category, document.title);

  let categoryInfo = gCategoryInits.get(category);
  if (!categoryInfo) {
    let err = new Error(
      "Unknown in-content prefs category! Can't init " + category
    );
    console.error(err);
    throw err;
  }
  categoryInfo.init();

  if (document.hasPendingL10nMutations) {
    await new Promise(r =>
      document.addEventListener("L10nMutationsFinished", r, { once: true })
    );
    // Bail out of this goToPref if the category
    // or subcategory changed during async operation.
    if (
      gLastCategory.category !== category ||
      gLastCategory.subcategory !== subcategory
    ) {
      return;
    }
  }

  search(category, "data-category");

  if (aShowReason != "Initial") {
    document.querySelector(".main-content").scrollTop = 0;
  }

  // Check to see if the category module wants to do any special
  // handling of the subcategory - for example, opening a SubDialog.
  //
  // If not, just do a normal spotlight on the subcategory.
  let categoryModule = gCategoryModules.get(category);
  if (!categoryModule.handleSubcategory?.(subcategory)) {
    spotlight(subcategory, category);
  }

  // Handle any visibility changes that are controlled by pref logic.
  //
  // Take caution when trying to flip the hidden state to true since the
  // element might show up unexpectedly on different pages in about:preferences.
  //
  // See Bug 1999032 to remove this in favor of config-based prefs.
  categoryModule.handlePrefControlledSection?.();

  // Record which category is shown
  let gleanId = /** @type {"showClick" | "showHash" | "showInitial"} */ (
    "show" + aShowReason
  );
  Glean.aboutpreferences[gleanId].record({ value: category });

  document.dispatchEvent(
    new CustomEvent("paneshown", {
      bubbles: true,
      cancelable: true,
      detail: {
        category,
      },
    })
  );
}

/**
 * @param {string} aQuery
 * @param {string} aAttribute
 */
function search(aQuery, aAttribute) {
  let mainPrefPane = document.getElementById("mainPrefPane");
  let elements = /** @type {HTMLElement[]} */ (
    Array.from(mainPrefPane.children)
  );
  for (let element of elements) {
    // If the "data-hidden-from-search" is "true", the
    // element will not get considered during search.
    if (
      element.getAttribute("data-hidden-from-search") != "true" ||
      element.getAttribute("data-subpanel") == "true"
    ) {
      let attributeValue = element.getAttribute(aAttribute);
      if (attributeValue == aQuery) {
        element.hidden = false;
      } else {
        element.hidden = true;
      }
    } else if (
      element.getAttribute("data-hidden-from-search") == "true" &&
      !element.hidden
    ) {
      element.hidden = true;
    }
    element.classList.remove("visually-hidden");
  }
}

function spotlight(subcategory, category) {
  let highlightedElements = document.querySelectorAll(".spotlight");
  if (highlightedElements.length) {
    for (let element of highlightedElements) {
      element.classList.remove("spotlight");
    }
  }
  if (subcategory) {
    scrollAndHighlight(subcategory, category);
  }
}

function scrollAndHighlight(subcategory) {
  let elements = document.querySelectorAll(
    `[data-subcategory~="${subcategory}"]`
  );
  if (!elements.length) {
    return;
  }

  elements[0].scrollIntoView({
    behavior: "smooth",
    block: "center",
  });
  for (let element of elements) {
    element.classList.add("spotlight");
  }
}

function friendlyPrefCategoryNameToInternalName(aName) {
  if (aName.startsWith("pane")) {
    return aName;
  }
  return "pane" + aName.substring(0, 1).toUpperCase() + aName.substr(1);
}

// This function is duplicated inside of utilityOverlay.js's openPreferences.
function internalPrefCategoryNameToFriendlyName(aName) {
  return (aName || "").replace(/^pane./, function (toReplace) {
    return toReplace[4].toLowerCase();
  });
}

// Put up a confirm dialog with "ok to restart", "revert without restarting"
// and "restart later" buttons and returns the index of the button chosen.
// We can choose not to display the "restart later", or "revert" buttons,
// altough the later still lets us revert by using the escape key.
//
// The constants are useful to interpret the return value of the function.
const CONFIRM_RESTART_PROMPT_RESTART_NOW = 0;
const CONFIRM_RESTART_PROMPT_CANCEL = 1;
const CONFIRM_RESTART_PROMPT_RESTART_LATER = 2;
async function confirmRestartPrompt(
  aRestartToEnable,
  aDefaultButtonIndex,
  aWantRevertAsCancelButton,
  aWantRestartLaterButton
) {
  let [
    msg,
    title,
    restartButtonText,
    noRestartButtonText,
    restartLaterButtonText,
  ] = await document.l10n.formatValues([
    {
      id: aRestartToEnable
        ? "feature-enable-requires-restart"
        : "feature-disable-requires-restart",
    },
    { id: "should-restart-title" },
    { id: "should-restart-ok" },
    { id: "cancel-no-restart-button" },
    { id: "restart-later" },
  ]);

  // Set up the first (index 0) button:
  let buttonFlags =
    Services.prompt.BUTTON_POS_0 * Services.prompt.BUTTON_TITLE_IS_STRING;

  // Set up the second (index 1) button:
  if (aWantRevertAsCancelButton) {
    buttonFlags +=
      Services.prompt.BUTTON_POS_1 * Services.prompt.BUTTON_TITLE_IS_STRING;
  } else {
    noRestartButtonText = null;
    buttonFlags +=
      Services.prompt.BUTTON_POS_1 * Services.prompt.BUTTON_TITLE_CANCEL;
  }

  // Set up the third (index 2) button:
  if (aWantRestartLaterButton) {
    buttonFlags +=
      Services.prompt.BUTTON_POS_2 * Services.prompt.BUTTON_TITLE_IS_STRING;
  } else {
    restartLaterButtonText = null;
  }

  switch (aDefaultButtonIndex) {
    case 0:
      buttonFlags += Services.prompt.BUTTON_POS_0_DEFAULT;
      break;
    case 1:
      buttonFlags += Services.prompt.BUTTON_POS_1_DEFAULT;
      break;
    case 2:
      buttonFlags += Services.prompt.BUTTON_POS_2_DEFAULT;
      break;
    default:
      break;
  }

  let button = await Services.prompt.asyncConfirmEx(
    window.browsingContext,
    Ci.nsIPrompt.MODAL_TYPE_CONTENT,
    title,
    msg,
    buttonFlags,
    restartButtonText,
    noRestartButtonText,
    restartLaterButtonText,
    null,
    {}
  );

  let buttonIndex = button.get("buttonNumClicked");

  // If we have the second confirmation dialog for restart, see if the user
  // cancels out at that point.
  if (buttonIndex == CONFIRM_RESTART_PROMPT_RESTART_NOW) {
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );
    if (cancelQuit.data) {
      buttonIndex = CONFIRM_RESTART_PROMPT_CANCEL;
    }
  }
  return buttonIndex;
}

// This function is used to append search keywords found
// in the related subdialog to the button that will activate the subdialog.
function appendSearchKeywords(aId, keywords) {
  let element = document.getElementById(aId);
  let searchKeywords = element.getAttribute("searchkeywords");
  if (searchKeywords) {
    keywords.push(searchKeywords);
  }
  element.setAttribute("searchkeywords", keywords.join(" "));
}

async function ensureScrollPadding() {
  let stickyContainer = document.querySelector(".sticky-container");
  let height = await window.browsingContext.topChromeWindow
    .promiseDocumentFlushed(() => stickyContainer.clientHeight)
    .catch(console.error); // Can reject if the window goes away.

  // Make it a bit more, to ensure focus rectangles etc. don't get cut off.
  // This being 8px causes us to end up with 90px if the policies container
  // is not visible (the common case), which matches the CSS and thus won't
  // cause a style change, repaint, or other changes.
  height += 8;
  stickyContainer
    .closest(".main-content")
    .style.setProperty("scroll-padding-top", height + "px");
}

function maybeDisplayPoliciesNotice() {
  if (Services.policies.status == Services.policies.ACTIVE) {
    document.getElementById("policies-container").removeAttribute("hidden");
  }
  ensureScrollPadding();
}
