/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";
import { webrtcUI } from "resource:///modules/webrtcUI.sys.mjs";

/*
 * TabUnloader is used to discard tabs when memory or resource constraints
 * are reached. The discarded tabs are determined using a heuristic that
 * accounts for when the tab was last used, how many resources the tab uses,
 * and whether the tab is likely to affect the user if it is closed.
 */

// If there are only this many or fewer tabs open, just sort by weight, and close
// the lowest tab. Otherwise, do a more intensive computation that determines the
// tabs to close based on memory and process use.
const MIN_TABS_COUNT = 10;

// Weight for non-discardable tabs.
const NEVER_DISCARD = 100000;

// Use a lightweight page for active-tab replacement.
const RECOVERY_TAB_URL = "about:blank";

// If the memory threshold remains continuously exceeded, the memory watcher may
// repeatedly call unloadTabAsync(). This cooldown throttles only selected-tab
// replacement per browser window. Do not use a global cooldown here, because
// each window must be able to get its own recovery tab independently.
const SELECTED_TAB_REPLACEMENT_WINDOW_COOLDOWN_MS = 30000;

// Live pref: when disabled, the selected tab in the foreground browser window
// is not eligible as a last resort unload target.
const kUnloadActiveForegroundTabPref =
  "browser.tabs.unloadActiveForegroundTab";

// Default minimum inactive duration. Tabs that were accessed in the last
// period of this duration are not unloaded.
const kMinInactiveDurationInMs = Services.prefs.getIntPref(
  "browser.tabs.min_inactive_duration_before_unload",
  0
);

// Dedicated per-window recovery tab marker.
const kRecoveryTabKey = Symbol("TabUnloaderRecoveryTab");
const kRecoveryTabCleanupKey = Symbol("TabUnloaderRecoveryTabCleanup");
const RECOVERY_TAB_ATTRIBUTE = "tabunloader-recovery";

// Track exactly one temporary recovery tab per browser window. The key is the
// window's gBrowser, so every browser window can have its own independent
// recovery tab, but never more than one.
const gRecoveryStateByBrowser = new WeakMap();

const gSelectedTabReplacementByBrowser = new WeakMap();

function isRecoveryTab(tab) {
  return !!(
    tab?.[kRecoveryTabKey] ||
    tab?.getAttribute?.(RECOVERY_TAB_ATTRIBUTE) === "true"
  );
}

function getTabSpec(tab) {
  return tab?.linkedBrowser?.currentURI?.spec;
}

function isForegroundWindow(win) {
  return win === Services.wm.getMostRecentWindow("navigator:browser");
}

function isTemporaryRecoverySpec(spec) {
  // about:newtab is accepted so an older already-marked recovery tab can be
  // reused/cleaned up after updating the code. New recovery tabs are created as
  // about:blank.
  return spec === RECOVERY_TAB_URL || spec === "about:newtab";
}

function isUsableRecoveryTab(tab, tabToReplace = null) {
  if (!tab || tab === tabToReplace || tab.closing || tab.isDestroyed) {
    return false;
  }

  if (!isRecoveryTab(tab)) {
    return false;
  }

  const spec = getTabSpec(tab);

  // Trust an already-marked recovery tab while currentURI is temporarily
  // unavailable during creation/loading. This prevents one-window recovery
  // state from being discarded and recreated repeatedly in Firefox 140+.
  return !spec || isTemporaryRecoverySpec(spec);
}

function canReplaceSelectedTab(gBrowser) {
  const now = Date.now();
  const lastForWindow = gSelectedTabReplacementByBrowser.get(gBrowser) || 0;

  if (
    now - lastForWindow <
    SELECTED_TAB_REPLACEMENT_WINDOW_COOLDOWN_MS
  ) {
    return false;
  }

  return true;
}

function noteSelectedTabReplacement(gBrowser) {
  const now = Date.now();
  gSelectedTabReplacementByBrowser.set(gBrowser, now);

  const state = gRecoveryStateByBrowser.get(gBrowser);
  if (state) {
    state.lastSelectedReplacementAt = now;
  }
}

let criteriaTypes = [
  ["isNonDiscardable", NEVER_DISCARD],
  ["isSelectedInBackgroundWindow", 100],
  ["isSelectedInForegroundWindow", 1000],
  ["isLoading", 8],
  ["usingPictureInPicture", NEVER_DISCARD],
  ["playingMedia", NEVER_DISCARD],
  ["usingWebRTC", NEVER_DISCARD],
  ["isPinned", 2],
  ["isPrivate", NEVER_DISCARD],
];

// Indices into the criteriaTypes lists.
let CRITERIA_METHOD = 0;
let CRITERIA_WEIGHT = 1;

/**
 * This is an object that supplies methods that determine details about
 * each tab. This default object is used if another one is not passed
 * to the tab unloader functions. This allows tests to override the methods
 * with tab specific data rather than creating test tabs.
 */
let DefaultTabUnloaderMethods = {
  isNonDiscardable(tab, weight) {
    if (tab.undiscardable) {
      return weight;
    }

    if (!tab.linkedBrowser.isConnected) {
      return -1;
    }

    // A dedicated recovery tab should never itself become an unload target.
    if (isRecoveryTab(tab)) {
      return -1;
    }

    const currentSpec = getTabSpec(tab);

    // Protect ordinary about: pages from being unloaded. This also protects the
    // about:blank recovery tab if it reaches this check before the marker check.
    if (currentSpec?.startsWith("about:")) {
      return -1;
    }

    return 0;
  },

  isSelectedInBackgroundWindow(tab, weight) {
    if (!tab.selected) {
      return 0;
    }

    const foregroundWindow = Services.wm.getMostRecentWindow(
      "navigator:browser"
    );

    return tab.documentGlobal !== foregroundWindow ? weight : 0;
  },

  isSelectedInForegroundWindow(tab, weight) {
    if (!tab.selected) {
      return 0;
    }

    const foregroundWindow = Services.wm.getMostRecentWindow(
      "navigator:browser"
    );

    if (tab.documentGlobal !== foregroundWindow) {
      return 0;
    }

    return Services.prefs.getBoolPref(kUnloadActiveForegroundTabPref, false)
      ? weight
      : -1;
  },

  isPinned(tab, weight) {
    return tab.pinned ? weight : 0;
  },

  isLoading() {
    return 0;
  },

  usingPictureInPicture(tab, weight) {
    // This has higher weight even when paused.
    return tab.pictureinpicture ? weight : 0;
  },

  playingMedia(tab, weight) {
    return tab.soundPlaying ? weight : 0;
  },

  usingWebRTC(tab, weight) {
    const browser = tab.linkedBrowser;
    if (!browser) {
      return 0;
    }

    let browserHasStreams = false;
    try {
      browserHasStreams = !!webrtcUI?.browserHasStreams?.(browser);
    } catch (ex) {
      Cu.reportError(ex);
    }

    let hasActivePeerConnections = false;
    try {
      hasActivePeerConnections = !!browser.browsingContext?.currentWindowGlobal
        ?.hasActivePeerConnections?.();
    } catch (ex) {
      Cu.reportError(ex);
    }

    return browserHasStreams || hasActivePeerConnections ? weight : 0;
  },

  isPrivate(tab, weight) {
    return PrivateBrowsingUtils.isBrowserPrivate(tab.linkedBrowser) ? weight : 0;
  },

  getMinTabCount() {
    return MIN_TABS_COUNT;
  },

  getNow() {
    return Date.now();
  },

  *iterateTabs() {
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      for (let tab of win.gBrowser.tabs) {
        yield { tab, gBrowser: win.gBrowser };
      }
    }
  },

  *iterateBrowsingContexts(bc) {
    yield bc;
    for (let childBC of bc.children) {
      yield* this.iterateBrowsingContexts(childBC);
    }
  },

  *iterateProcesses(tab) {
    let bc = tab?.linkedBrowser?.browsingContext;
    if (!bc) {
      return;
    }

    const iter = this.iterateBrowsingContexts(bc);
    for (let childBC of iter) {
      if (childBC?.currentWindowGlobal) {
        yield childBC.currentWindowGlobal.osPid;
      }
    }
  },

  /**
   * Add the amount of memory used by each process to the process map.
   *
   * @param processMap map of processes returned by getAllProcesses.
   */
  async calculateMemoryUsage(processMap) {
    let parentProcessInfo = await ChromeUtils.requestProcInfo();
    let childProcessInfoList = parentProcessInfo.children;
    for (let childProcInfo of childProcessInfoList) {
      let processInfo = processMap.get(childProcInfo.pid);
      if (!processInfo) {
        processInfo = { count: 0, topCount: 0, tabSet: new Set() };
        processMap.set(childProcInfo.pid, processInfo);
      }
      processInfo.memory = childProcInfo.memory;
    }
  },
};

/**
 * This module is responsible for detecting low-memory scenarios and unloading
 * tabs in response to them.
 */
export var TabUnloader = {
  _isUnloading: false,

  /**
   * Initialize low-memory detection and tab auto-unloading.
   */
  init() {
    const watcher = Cc["@mozilla.org/xpcom/memory-watcher;1"].getService(
      Ci.nsIAvailableMemoryWatcherBase
    );
    watcher.registerTabUnloader(this);
  },

  isDiscardable(tabInfo) {
    return typeof tabInfo.weight == "number" && tabInfo.weight < NEVER_DISCARD;
  },

  clearRecoveryTab(tab, gBrowser = null) {
    if (!tab) {
      return;
    }

    if (gBrowser) {
      const state = gRecoveryStateByBrowser.get(gBrowser);
      if (state?.tab === tab) {
        gRecoveryStateByBrowser.delete(gBrowser);
      }
    }

    if (typeof tab[kRecoveryTabCleanupKey] == "function") {
      tab[kRecoveryTabCleanupKey]();
      return;
    }

    try {
      tab.removeAttribute?.(RECOVERY_TAB_ATTRIBUTE);
    } catch (ex) {
      Cu.reportError(ex);
    }

    delete tab[kRecoveryTabCleanupKey];
    delete tab[kRecoveryTabKey];
  },

  getTrackedRecoveryTab(gBrowser, tabToReplace = null) {
    const state = gRecoveryStateByBrowser.get(gBrowser);
    if (!state) {
      return null;
    }

    if (state.tab && isUsableRecoveryTab(state.tab, tabToReplace)) {
      return state.tab;
    }

    // The tracked tab was closed, navigated, or otherwise became invalid.
    // Clear the stale state so a later selected-tab replacement can create one.
    if (state.tab) {
      this.clearRecoveryTab(state.tab, gBrowser);
    } else {
      gRecoveryStateByBrowser.delete(gBrowser);
    }

    return null;
  },

  markRecoveryTab(gBrowser, recoveryTab, tabToReplace = null) {
    if (!gBrowser || !recoveryTab) {
      return null;
    }

    const currentState = gRecoveryStateByBrowser.get(gBrowser);
    if (currentState?.tab === recoveryTab && isRecoveryTab(recoveryTab)) {
      return recoveryTab;
    }

    for (let tab of gBrowser.tabs) {
      if (tab !== recoveryTab && isRecoveryTab(tab)) {
        this.clearRecoveryTab(tab, gBrowser);
      }
    }

    if (typeof recoveryTab[kRecoveryTabCleanupKey] == "function") {
      recoveryTab[kRecoveryTabCleanupKey]();
    }

    recoveryTab[kRecoveryTabKey] = true;
    try {
      recoveryTab.setAttribute?.(RECOVERY_TAB_ATTRIBUTE, "true");
    } catch (ex) {
      Cu.reportError(ex);
    }

    const browser = recoveryTab.linkedBrowser;
    const self = this;
    const oldState = gRecoveryStateByBrowser.get(gBrowser);

    gRecoveryStateByBrowser.set(gBrowser, {
      tab: recoveryTab,
      replacingTab: tabToReplace,
      createdAt: oldState?.createdAt || Date.now(),
      lastSelectedReplacementAt: oldState?.lastSelectedReplacementAt || 0,
    });

    const onTabSelect = event => {
      // Ignore the switch *to* the recovery tab. Only react when the user or
      // browser later selects a different tab.
      if (event.target === recoveryTab) {
        return;
      }

      // Do not auto-remove during an active unload transaction. Firefox 140+
      // can fire programmatic TabSelect churn while discardBrowser() is still
      // running, and removing the recovery tab here can cause replacement loops.
      if (self._isUnloading) {
        return;
      }

      Services.tm.dispatchToMainThread(() => {
        if (self._isUnloading) {
          return;
        }

        if (
          recoveryTab.closing ||
          recoveryTab.isDestroyed ||
          !isRecoveryTab(recoveryTab)
        ) {
          return;
        }

        // If the recovery tab has been navigated away from the temporary
        // recovery URL, it is now a normal tab. Clear the marker/state but do
        // not remove the tab.
        const spec = getTabSpec(recoveryTab);
        if (!isTemporaryRecoverySpec(spec)) {
          self.clearRecoveryTab(recoveryTab, gBrowser);
          return;
        }

        // Keep the recovery tab alive after another tab is selected. This lets
        // each window maintain and reuse its own recovery tab instead of
        // removing/recreating it under repeated memory pressure.
        return;
      });
    };

    const onTabClose = event => {
      if (event.target === recoveryTab) {
        self.clearRecoveryTab(recoveryTab, gBrowser);
      }
    };

    const progressListener = {
      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),

      onLocationChange(webProgress, request, location, flags) {
        const spec = location?.spec;
        if (!spec || isTemporaryRecoverySpec(spec)) {
          return;
        }

        Services.tm.dispatchToMainThread(() => {
          if (
            recoveryTab.closing ||
            recoveryTab.isDestroyed ||
            !isRecoveryTab(recoveryTab)
          ) {
            return;
          }

          // Once the recovery tab is navigated somewhere real, stop treating it
          // as a tracked temporary recovery tab, but leave the tab open.
          self.clearRecoveryTab(recoveryTab, gBrowser);
        });
      },

      onStateChange() {},
      onProgressChange() {},
      onStatusChange() {},
      onSecurityChange() {},
      onContentBlockingEvent() {},
    };

    browser.addProgressListener(
      progressListener,
      Ci.nsIWebProgress.NOTIFY_LOCATION
    );

    recoveryTab[kRecoveryTabCleanupKey] = () => {
      gBrowser.tabContainer.removeEventListener("TabSelect", onTabSelect);
      gBrowser.tabContainer.removeEventListener("TabClose", onTabClose);

      try {
        browser.removeProgressListener(progressListener);
      } catch (ex) {
        Cu.reportError(ex);
      }

      const state = gRecoveryStateByBrowser.get(gBrowser);
      if (state?.tab === recoveryTab) {
        gRecoveryStateByBrowser.delete(gBrowser);
      }

      try {
        recoveryTab.removeAttribute?.(RECOVERY_TAB_ATTRIBUTE);
      } catch (ex) {
        Cu.reportError(ex);
      }

      delete recoveryTab[kRecoveryTabCleanupKey];
      delete recoveryTab[kRecoveryTabKey];
    };

    gBrowser.tabContainer.addEventListener("TabSelect", onTabSelect);
    gBrowser.tabContainer.addEventListener("TabClose", onTabClose);

    return recoveryTab;
  },

  getOrCreateRecoveryTab(gBrowser, tabToReplace) {
    if (!gBrowser) {
      return null;
    }

    // 1. Reuse this window's tracked recovery tab if it already exists.
    const trackedRecoveryTab = this.getTrackedRecoveryTab(gBrowser, tabToReplace);
    if (trackedRecoveryTab) {
      return trackedRecoveryTab;
    }

    // 2. Reuse any already-marked recovery tab in this same window. This covers
    // cases where the WeakMap state was lost but the tab marker still exists.
    for (let tab of gBrowser.tabs) {
      if (isUsableRecoveryTab(tab, tabToReplace)) {
        return this.markRecoveryTab(gBrowser, tab, tabToReplace);
      }
    }

    // 3. No existing recovery tab found for this window, so create exactly one.
    const newTab = gBrowser.addTrustedTab(RECOVERY_TAB_URL, {
      relatedToCurrent: false,
      inBackground: true,
      skipAnimation: true,
    });
    return this.markRecoveryTab(gBrowser, newTab, tabToReplace);
  },

  // This method is exposed on nsITabUnloader.
  async unloadTabAsync(minInactiveDuration = kMinInactiveDurationInMs) {
    const watcher = Cc["@mozilla.org/xpcom/memory-watcher;1"].getService(
      Ci.nsIAvailableMemoryWatcherBase
    );

    if (!Services.prefs.getBoolPref("browser.tabs.unloadOnLowMemory", true)) {
      watcher.onUnloadAttemptCompleted(Cr.NS_ERROR_NOT_AVAILABLE);
      return;
    }

    if (this._isUnloading) {
      // Don't post multiple unloading requests. The situation may be solved
      // when the active unloading task is completed.
      Services.console.logStringMessage("Unloading a tab is in progress.");
      watcher.onUnloadAttemptCompleted(Cr.NS_ERROR_ABORT);
      return;
    }

    let isTabUnloaded = false;
    this._isUnloading = true;

    try {
      isTabUnloaded = await this.unloadLeastRecentlyUsedTab(
        minInactiveDuration
      );
    } catch (ex) {
      Cu.reportError(ex);
    } finally {
      this._isUnloading = false;
      watcher.onUnloadAttemptCompleted(
        isTabUnloaded ? Cr.NS_OK : Cr.NS_ERROR_NOT_AVAILABLE
      );
    }
  },

  /**
   * Get a list of tabs that can be discarded. This list includes all tabs in
   * all windows and is sorted based on a weighting described below.
   *
   * @param minInactiveDuration If this value is a number, non-selected tabs
   *        that were accessed in the last |minInactiveDuration| msec are not
   *        unloaded even if they are least-recently-used. Selected tabs are
   *        kept in the list so they can be final fallback candidates.
   *
   * @param tabMethods a helper object with methods called by this algorithm.
   */
  async getSortedTabs(
    minInactiveDuration = kMinInactiveDurationInMs,
    tabMethods = DefaultTabUnloaderMethods
  ) {
    let tabs = [];

    const now = tabMethods.getNow();

    let lowestWeight = 1000;
    for (let tab of tabMethods.iterateTabs()) {
      // Do not skip non-selected tabs just because they are fresh. All
      // non-active tabs, old or fresh, should be considered before any active
      // selected tab is used as a last-resort unload target.
      let weight = determineTabBaseWeight(tab, tabMethods);

      // Don't add tabs that have a weight of -1.
      if (weight != -1) {
        tab.weight = weight;
        tabs.push(tab);
        if (weight < lowestWeight) {
          lowestWeight = weight;
        }
      }
    }

    tabs = tabs.sort((a, b) => {
      if (a.weight != b.weight) {
        return a.weight - b.weight;
      }

      return a.tab.lastAccessed - b.tab.lastAccessed;
    });

    // If the lowest priority tab is not discardable, no need to continue.
    if (!tabs.length || !this.isDiscardable(tabs[0])) {
      return tabs;
    }

    // Determine the lowest weight that the tabs have. The tabs with the
    // lowest weight will be additionally weighted by the number of processes
    // and memory that they use.
    let higherWeightedCount = 0;
    for (let idx = 0; idx < tabs.length; idx++) {
      if (tabs[idx].weight != lowestWeight) {
        higherWeightedCount = tabs.length - idx;
        break;
      }
    }

    // Don't continue to reweight the last few tabs, the number of which is
    // determined by getMinTabCount. This prevents extra work when there are
    // only a few tabs, or for the last few tabs that have likely been used
    // recently.
    let minCount = tabMethods.getMinTabCount();
    if (higherWeightedCount < minCount) {
      higherWeightedCount = minCount;
    }

    // If |lowestWeightedCount| is 1, no benefit from calculating the tab's
    // memory and additional weight.
    const lowestWeightedCount = tabs.length - higherWeightedCount;
    if (lowestWeightedCount > 1) {
      let processMap = getAllProcesses(tabs, tabMethods);

      let higherWeightedTabs = tabs.splice(-higherWeightedCount);

      await adjustForResourceUse(tabs, processMap, tabMethods);
      tabs = tabs.concat(higherWeightedTabs);
    }

    return tabs;
  },

  /**
   * Select and discard one tab.
   *
   * @returns true if a tab was unloaded, otherwise false.
   */
  async unloadLeastRecentlyUsedTab(
    minInactiveDuration = kMinInactiveDurationInMs
  ) {
    const sortedTabs = await this.getSortedTabs(minInactiveDuration);

    for (let tabInfo of sortedTabs) {
      if (!this.isDiscardable(tabInfo)) {
        // Since |sortedTabs| is sorted, once we see a non-discardable tab
        // no need to continue the loop.
        return false;
      }

      const { tab } = tabInfo;
      const gBrowser = tab?.documentGlobal?.gBrowser || tabInfo.gBrowser;
      if (!tab || !gBrowser) {
        continue;
      }

      if (tab.selected) {
        // If memory pressure remains continuously above threshold, the memory
        // watcher may keep calling unloadTabAsync(). Do not repeatedly replace
        // selected tabs, because changing selectedTab repaints browser chrome
        // and causes flicker. Background tab unloading is not throttled.
        if (!canReplaceSelectedTab(gBrowser)) {
          continue;
        }

        const recoveryTab = this.getOrCreateRecoveryTab(gBrowser, tab);
        if (!recoveryTab || recoveryTab === tab) {
          continue;
        }

        if (gBrowser.selectedTab !== recoveryTab) {
          gBrowser.selectedTab = recoveryTab;
        }

        noteSelectedTabReplacement(gBrowser);
      }

      const remoteType = tab?.linkedBrowser?.remoteType;

      // prepareDiscardBrowser()
      if (typeof gBrowser.prepareDiscardBrowser == "function") {
        await gBrowser.prepareDiscardBrowser(tab);
      }

      if (gBrowser.discardBrowser(tab)) {
        Services.console.logStringMessage(
          `TabUnloader discarded <${remoteType}>`
        );
        tab.updateLastUnloadedByTabUnloader?.();
        return true;
      }
    }
    return false;
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsITabUnloader",
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),
};

/**
 * Determine the base weight of the tab without accounting for resource use.
 *
 * @param tab tab to use
 * @returns the tab's base weight
 */
function determineTabBaseWeight(tab, tabMethods) {
  let totalWeight = 0;

  for (let criteriaType of criteriaTypes) {
    let weight = tabMethods[criteriaType[CRITERIA_METHOD]](
      tab.tab,
      criteriaType[CRITERIA_WEIGHT]
    );

    // If a criteria returns -1, then never discard this tab.
    if (weight == -1) {
      return -1;
    }

    totalWeight += weight;
  }

  return totalWeight;
}

/**
 * Construct a map of the processes that are used by the supplied tabs.
 * The map will map process ids to an object with three properties:
 *   count - the number of tabs or subframes that use this process
 *   topCount - the number of top-level tabs that use this process
 *   tabSet - the indices of the tabs hosted by this process
 *
 * @param tabs array of tabs
 * @param tabMethods a helper object with methods called by this algorithm.
 * @returns process map
 */
function getAllProcesses(tabs, tabMethods) {
  // Determine the number of tabs that reference each process. This
  // is stored in the map 'processMap' where the key is the process
  // and the value is that number of browsing contexts that use that
  // process.
  // XXXndeakin this should be unique processes per tab, in the case multiple
  // subframes use the same process?

  let processMap = new Map();

  for (let tabIndex = 0; tabIndex < tabs.length; ++tabIndex) {
    const tab = tabs[tabIndex];

    // The per-tab map will map process ids to an object with three properties:
    //   isTopLevel - whether the process hosts the tab's top-level frame or not
    //   frameCount - the number of frames hosted by the process
    //                (a top frame contributes 2 and a sub frame contributes 1)
    //   entryToProcessMap - the reference to the object in |processMap|
    tab.processes = new Map();

    let topLevel = true;
    for (let pid of tabMethods.iterateProcesses(tab.tab)) {
      let processInfo = processMap.get(pid);
      if (processInfo) {
        processInfo.count++;
        processInfo.tabSet.add(tabIndex);
      } else {
        processInfo = { count: 1, topCount: 0, tabSet: new Set([tabIndex]) };
        processMap.set(pid, processInfo);
      }

      let tabProcessEntry = tab.processes.get(pid);
      if (tabProcessEntry) {
        ++tabProcessEntry.frameCount;
      } else {
        tabProcessEntry = {
          isTopLevel: topLevel,
          frameCount: 1,
          entryToProcessMap: processInfo,
        };
        tab.processes.set(pid, tabProcessEntry);
      }

      if (topLevel) {
        topLevel = false;
        processInfo.topCount = processInfo.topCount
          ? processInfo.topCount + 1
          : 1;
        // Top-level frame contributes two frame counts.
        ++tabProcessEntry.frameCount;
      }
    }
  }

  return processMap;
}

/**
 * Adjust the tab info and reweight the tabs based on the process and memory
 * use that is used, as described by getSortedTabs.
 *
 * @param tabs array of tabs
 * @param processMap map of processes returned by getAllProcesses
 * @param tabMethods a helper object with methods called by this algorithm.
 */
async function adjustForResourceUse(tabs, processMap, tabMethods) {
  // The second argument is accepted by some tests/overrides.
  await tabMethods.calculateMemoryUsage(processMap, tabs);

  let sortWeight = 0;
  for (let tab of tabs) {
    tab.sortWeight = ++sortWeight;

    let uniqueCount = 0;
    let totalMemory = 0;
    for (const procEntry of tab.processes.values()) {
      const processInfo = procEntry.entryToProcessMap;
      if (processInfo.tabSet.size == 1) {
        uniqueCount++;
      }

      // Guess how much memory the frame might be using by dividing
      // the total memory used by a process by the number of tabs and
      // frames that are using that process. Assume that any subframes take up
      // only half as much memory as a process loaded in a top level tab.
      // So for example, if a process is used in four top level tabs and two
      // subframes, the top level tabs share 80% of the memory and the subframes
      // use 20% of the memory.
      const denominator =
        processInfo.topCount * 2 + (processInfo.count - processInfo.topCount);
      const perFrameMemory = denominator ? processInfo.memory / denominator : 0;
      totalMemory += perFrameMemory * procEntry.frameCount;
    }

    tab.uniqueCount = uniqueCount;
    tab.memory = totalMemory;
  }

  tabs.sort((a, b) => {
    return b.uniqueCount - a.uniqueCount;
  });
  sortWeight = 0;
  for (let tab of tabs) {
    tab.sortWeight += ++sortWeight;
    if (tab.uniqueCount > 1) {
      // If the tab has a number of processes that are only used by this tab,
      // subtract off an additional amount to the sorting weight value. That
      // way, tabs that use lots of processes are more likely to be discarded.
      tab.sortWeight -= tab.uniqueCount - 1;
    }
  }

  tabs.sort((a, b) => {
    return b.memory - a.memory;
  });
  sortWeight = 0;
  for (let tab of tabs) {
    tab.sortWeight += ++sortWeight;
  }

  tabs.sort((a, b) => {
    if (a.sortWeight != b.sortWeight) {
      return a.sortWeight - b.sortWeight;
    }
    return a.tab.lastAccessed - b.tab.lastAccessed;
  });
}
