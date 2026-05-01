/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { BrowserUtils } from "resource://gre/modules/BrowserUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const APPLE_COPY_LINK = "com.apple.share.CopyLink.invite";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QRCodeGenerator:
    "moz-src:///browser/components/qrcode/QRCodeGenerator.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  WindowsUIUtils: ["@mozilla.org/windows-ui-utils;1", Ci.nsIWindowsUIUtils],
});

// Use a non-caching getter so tests can swap out the service via MockRegistrar.
Object.defineProperty(lazy, "MacSharingService", {
  get() {
    return Cc["@mozilla.org/widget/macsharingservice;1"].getService(
      Ci.nsIMacSharingService
    );
  },
});

class SharingUtilsCls {
  /**
   * Updates a sharing item in a given menu, creating it if necessary.
   *
   * @param {MozBrowser} contextBrowser
   *   The browser of the right-clicked (context) tab. Used for sharing a
   *   single URL in native share dialogs on Windows and macOS.
   * @param {MozBrowser[]|null} browsers
   *   All selected browsers in tab strip order. Used for copy links.
   *   Pass null for single URL sharing.
   * @param {Element} insertAfterEl
   *   The menu item after which the share item is inserted.
   */
  updateShareURLMenuItem(contextBrowser, browsers, insertAfterEl) {
    if (!Services.prefs.getBoolPref("browser.menu.share_url.allow", true)) {
      return;
    }

    // We only support "share URL" on macOS and on Windows 10:
    if (
      AppConstants.platform != "macosx" &&
      // Windows 10's internal NT version number was initially 6.4
      !AppConstants.isPlatformAndVersionAtLeast("win", "6.4")
    ) {
      return;
    }
    let isMultiTab = browsers !== null;
    let shareableCount;
    if (isMultiTab) {
      shareableCount = browsers.filter(b =>
        BrowserUtils.getShareableURL(b.currentURI)
      ).length;
    } else {
      shareableCount = BrowserUtils.getShareableURL(contextBrowser.currentURI)
        ? 1
        : 0;
    }

    let shareURL;
    let oldElement = insertAfterEl.nextElementSibling;

    if (oldElement?.matches(".share-tab-url-item")) {
      if (AppConstants.platform == "macosx") {
        shareURL = oldElement;
      } else if (AppConstants.platform == "win") {
        // On Windows, single-tab uses a share sheet item while multi-tab uses a
        // copy-link item. Recreate if that changed.
        // Avoid removing the item during popupshowing otherwise, as DOM
        // mutations can prevent the popup from appearing.
        let existingIsMultiTab = oldElement.matches(".share-copy-link");
        if (existingIsMultiTab !== isMultiTab) {
          oldElement.remove();
          shareURL = this.#createShareURLMenuItem(
            insertAfterEl,
            shareableCount,
            isMultiTab
          );
        } else {
          shareURL = oldElement;
          if (isMultiTab) {
            insertAfterEl.ownerDocument.l10n.setAttributes(
              shareURL,
              "menu-share-copy-links",
              { count: Math.max(1, shareableCount) }
            );
          }
        }
      } else {
        // Linux always uses a copy-link item, just update the count.
        shareURL = oldElement;
        insertAfterEl.ownerDocument.l10n.setAttributes(
          shareURL,
          "menu-share-copy-links",
          { count: Math.max(1, shareableCount) }
        );
      }
    } else {
      shareURL = this.#createShareURLMenuItem(
        insertAfterEl,
        shareableCount,
        isMultiTab
      );
    }

    shareURL.contextBrowserToShare = Cu.getWeakReference(contextBrowser);
    shareURL.browsersToShare =
      browsers?.map(b => Cu.getWeakReference(b)) ?? null;

    if (AppConstants.platform != "macosx") {
      // On macOS, we keep the item visible and handle visibility
      // inside the menupopup.
      // Everywhere else, we hide the item, as there's no submenu.
      shareURL.hidden = shareableCount === 0;
    }
  }

  /**
   * Creates and returns the "Share" menu item.
   *
   * @param {Element} insertAfterEl
   * @param {number} shareableCount
   * @param {boolean} isMultiTab
   */
  #createShareURLMenuItem(insertAfterEl, shareableCount, isMultiTab) {
    let menu = insertAfterEl.parentNode;
    let shareURL = null;
    let document = insertAfterEl.ownerDocument;

    let l10nID =
      menu.id == "tabContextMenu"
        ? "tab-context-share-url"
        : "menu-file-share-url";

    switch (AppConstants.platform) {
      case "win":
        if (isMultiTab) {
          shareURL = this.#buildCopyLinkItem(document, shareableCount);
        } else {
          shareURL = this.#buildShareURLItem(document);
          document.l10n.setAttributes(shareURL, l10nID);
        }
        break;
      case "macosx":
        shareURL = this.#buildShareURLMenu(document);
        document.l10n.setAttributes(shareURL, l10nID);
        break;
      default:
        shareURL = this.#buildCopyLinkItem(document, shareableCount);
        break;
    }

    shareURL.classList.add("share-tab-url-item");

    menu.insertBefore(shareURL, insertAfterEl.nextSibling);
    return shareURL;
  }

  /**
   * Returns a menu item specifically for accessing Windows sharing services.
   */
  #buildShareURLItem(document) {
    let shareURLMenuItem = document.createXULElement("menuitem");
    shareURLMenuItem.addEventListener("command", this);
    return shareURLMenuItem;
  }

  /**
   * Returns a menu specifically for accessing macOS sharing services.
   */
  #buildShareURLMenu(document) {
    let menu = document.createXULElement("menu");
    let menuPopup = document.createXULElement("menupopup");
    menuPopup.addEventListener("popupshowing", this);
    menu.appendChild(menuPopup);
    return menu;
  }

  /**
   * Return a menuitem that only copies the link.
   * Used for OSes where we do not yet have full share support, like Linux, or
   * on macOS, where Apple does not provide the share service option for this.
   *
   * Also supports copying multiple links on Windows, where the share sheet
   * only supports copying one link at a time.
   */
  #buildCopyLinkItem(document, shareableCount) {
    let shareURLMenuItem = document.createXULElement("menuitem");
    document.l10n.setAttributes(shareURLMenuItem, "menu-share-copy-links", {
      // shareableCount can be zero in cases where one or more about:blank tabs
      // are selected, but no "real" tabs are in the selection. It should be 1
      // minimum here to ensure the localized string is correct.
      count: Math.max(1, shareableCount),
    });
    shareURLMenuItem.classList.add("share-copy-link");

    if (AppConstants.platform == "macosx") {
      shareURLMenuItem.classList.add("menuitem-iconic");
      shareURLMenuItem.setAttribute(
        "image",
        "chrome://global/skin/icons/link.svg"
      );
    } else {
      // On macOS the command handling happens by virtue of the submenu
      // command event listener.
      shareURLMenuItem.addEventListener("command", this);
    }
    return shareURLMenuItem;
  }

  async #showQRCodePanel(win, browser, url) {
    let qrCodeDataURI = null;
    try {
      qrCodeDataURI = await lazy.QRCodeGenerator.generateQRCode(
        url,
        win.document
      );
    } catch (error) {
      console.error("Failed to generate QR code:", error);
    }

    let params = {
      url,
      qrCodeDataURI,
    };

    win.gBrowser
      .getTabDialogBox(browser)
      .open(
        "chrome://browser/content/qrcode/qrcode-dialog.html",
        { features: "resizable=no", allowDuplicateDialogs: false },
        params
      );
  }

  /**
   * Get the sharing data for the context browser on a DOM node.
   */
  getLinkToShare(node) {
    let browser = node.contextBrowserToShare?.get();
    let urlToShare = null;
    let titleToShare = null;

    if (browser) {
      let maybeToShare = BrowserUtils.getShareableURL(browser.currentURI);
      if (maybeToShare) {
        let { gURLBar } = node.ownerGlobal;
        urlToShare = gURLBar.makeURIReadable(maybeToShare).displaySpec;
        titleToShare = browser.contentTitle;
      }
    }
    return { urlToShare, titleToShare };
  }

  /**
   * Get the link data for all browsers stored on a DOM node.
   *
   * @returns {Array<{url: string, title: string}>}
   */
  getLinksToShare(node) {
    let browsers = node.browsersToShare ?? [node.contextBrowserToShare];
    let links = [];
    for (let weakRef of browsers) {
      let browser = weakRef.get();
      if (!browser) {
        continue;
      }
      let maybeToShare = BrowserUtils.getShareableURL(browser.currentURI);
      if (maybeToShare) {
        let { gURLBar } = node.ownerGlobal;
        links.push({
          url: gURLBar.makeURIReadable(maybeToShare).displaySpec,
          title: browser.contentTitle,
        });
      }
    }
    return links;
  }

  initializeShareURLPopup(menuPopup) {
    if (AppConstants.platform != "macosx") {
      return;
    }

    this.populateShareMenu(menuPopup);

    menuPopup.parentNode
      .closest("menupopup")
      .addEventListener("popuphiding", this);
    menuPopup.setAttribute("data-initialized", true);
  }

  populateShareMenu(menuPopup) {
    // Clear existing
    while (menuPopup.firstChild) {
      menuPopup.firstChild.remove();
    }

    let document = menuPopup.ownerDocument;
    let node = menuPopup.parentNode;
    let isMultiTab = node.browsersToShare !== null;

    let { urlToShare } = this.getLinkToShare(node);

    // If we can't share the current URL, we display the items disabled,
    // but enable the "more..." item at the bottom, to allow the user to
    // change sharing preferences in the system dialog.
    let shouldEnable = !!urlToShare;
    if (!urlToShare) {
      // Fake it so we can ask the sharing service for services:
      urlToShare = "https://mozilla.org/";
    }

    let services = lazy.MacSharingService.getSharingProviders(urlToShare);

    if (!menuPopup.hasAttribute("data-command-listener")) {
      menuPopup.addEventListener("command", event => {
        if (event.target.classList.contains("share-qrcode-item")) {
          let { urlToShare: url } = this.getLinkToShare(node);
          let browser = node.contextBrowserToShare?.get();
          if (url && browser) {
            Glean.qrcode.opened.add(1);
            this.#showQRCodePanel(node.ownerGlobal, browser, url);
          }
        } else if (event.target.classList.contains("share-more-button")) {
          this.openMacSharePreferences();
        } else if (event.target.classList.contains("share-copy-link")) {
          this.copyLink(node);
        } else if (event.target.dataset.shareName) {
          this.shareOnMac(node, event.target.dataset.shareName);
        }
      });

      menuPopup.setAttribute("data-command-listener", "true");
    }

    // Apple seems reluctant to provide copy link as a feature. Add it at the
    // start if it's not there.
    if (!services.some(s => s.name == APPLE_COPY_LINK)) {
      let shareableCount;
      if (isMultiTab) {
        shareableCount = this.getLinksToShare(node).length;
      } else {
        shareableCount = shouldEnable ? 1 : 0;
      }
      let copyLinkEnabled = shareableCount > 0;
      let item = this.#buildCopyLinkItem(document, shareableCount);
      if (!copyLinkEnabled) {
        item.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(item);
    }

    if (Services.prefs.getBoolPref("browser.shareqrcode.enabled", false)) {
      let qrCodeItem = document.createXULElement("menuitem");
      qrCodeItem.classList.add("menuitem-iconic", "share-qrcode-item");
      document.l10n.setAttributes(qrCodeItem, "menu-file-share-qrcode");
      qrCodeItem.setAttribute("image", "chrome://browser/skin/qrcode.svg");
      if (!shouldEnable) {
        qrCodeItem.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(qrCodeItem);
    }

    if (services.length) {
      menuPopup.appendChild(document.createXULElement("menuseparator"));
    }

    // Share service items
    services.forEach(share => {
      let item = document.createXULElement("menuitem");
      item.classList.add("menuitem-iconic");
      item.setAttribute("label", share.menuItemTitle);
      item.setAttribute("data-share-name", share.name);
      item.setAttribute("image", ChromeUtils.encodeURIForSrcset(share.image));
      if (!shouldEnable) {
        item.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(item);
    });
    menuPopup.appendChild(document.createXULElement("menuseparator"));

    // More item
    let moreItem = document.createXULElement("menuitem");
    document.l10n.setAttributes(moreItem, "menu-share-more");
    moreItem.classList.add("menuitem-iconic", "share-more-button");
    moreItem.setAttribute("data-share-name", "share_macosx_more");
    menuPopup.appendChild(moreItem);
  }

  onShareURLCommand(event) {
    // Only call sharing services for the "Share" menu item. These services
    // are accessed from a submenu popup for MacOS or the "Share" menu item
    // for Windows. Use .closest() as a hack to find either the item itself
    // or a parent with the right class.
    let target = event.target.closest(".share-tab-url-item");
    if (!target) {
      return;
    }

    // urlToShare/titleToShare may be null, in which case only the "more"
    // item is enabled, so handle that case first:
    if (event.target.classList.contains("share-more-button")) {
      this.openMacSharePreferences();
      return;
    }

    if (event.target.classList.contains("share-copy-link")) {
      this.copyLink(target);
    } else if (AppConstants.platform == "win") {
      this.shareOnWindows(target);
    } else {
      // On macOS platforms
      let shareName = event.target.getAttribute("data-share-name");
      if (shareName) {
        this.shareOnMac(target, shareName);
      }
    }
  }

  onPopupHiding(event) {
    // We don't want to rebuild the contents of the "Share" menupopup if only its submenu is
    // hidden. So bail if this isn't the top menupopup in the DOM tree:
    if (event.target.parentNode.closest("menupopup")) {
      return;
    }
    // Otherwise, clear its "data-initialized" attribute.
    let menupopup = event.target.querySelector(
      ".share-tab-url-item"
    )?.menupopup;
    menupopup?.removeAttribute("data-initialized");

    event.target.removeEventListener("popuphiding", this);
  }

  onPopupShowing(event) {
    if (!event.target.hasAttribute("data-initialized")) {
      this.initializeShareURLPopup(event.target);
    }
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "command":
        this.onShareURLCommand(aEvent);
        break;
      case "popuphiding":
        this.onPopupHiding(aEvent);
        break;
      case "popupshowing":
        this.onPopupShowing(aEvent);
        break;
    }
  }

  copyLink(node) {
    let links = this.getLinksToShare(node);
    if (links.length) {
      BrowserUtils.copyLinks(links);
    }
  }

  shareOnWindows(node) {
    let { urlToShare, titleToShare } = this.getLinkToShare(node);
    if (!urlToShare) {
      return;
    }

    lazy.WindowsUIUtils.shareUrl(urlToShare, titleToShare);
  }

  shareOnMac(node, serviceName) {
    let { urlToShare, titleToShare } = this.getLinkToShare(node);
    if (!urlToShare) {
      return;
    }

    lazy.MacSharingService.shareUrl(serviceName, urlToShare, titleToShare);
  }

  openMacSharePreferences() {
    lazy.MacSharingService.openSharingPreferences();
  }

  testOnlyMockUIUtils(mock) {
    if (!Cu.isInAutomation) {
      throw new Error("Can only mock utils in automation.");
    }
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(lazy, "WindowsUIUtils", {
      get() {
        if (mock) {
          return mock;
        }
        return Cc["@mozilla.org/windows-ui-utils;1"].getService(
          Ci.nsIWindowsUIUtils
        );
      },
    });
  }
}

export let SharingUtils = new SharingUtilsCls();
