/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";

export let WebGLPermissionPromptHelper = {
  _permissionsPrompt: "webgl-permissions-prompt",
  _permissionsPromptHideDoorHanger: "webgl-permissions-prompt-hide-doorhanger",
  _notificationIcon: "webgl-notification-icon",

  // aSubject is an nsIBrowser (e10s) or an nsIDOMWindow (non-e10s).
  // aData is an Origin string.
  observe(aSubject, aTopic, aData) {
    if (
      aTopic != this._permissionsPrompt &&
      aTopic != this._permissionsPromptHideDoorHanger
    ) {
      return;
    }

    let browser;
    if (aSubject instanceof Ci.nsIDOMWindow) {
      browser = aSubject.docShell.chromeEventHandler;
    } else {
      browser = aSubject;
    }

    let window = browser?.ownerGlobal;
    if (!window) {
      // Without knowing where this came from, we can't show the prompt.
      return;
    }

    let { gNavigatorBundle, gBrowserBundle } = window;
    let message = gNavigatorBundle.getFormattedString(
      "webgl.siteprompt2",
      ["<>"],
      1
    );

    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(aData);

    function setWebGLPermission(aPerm, aPersistent) {
      Services.perms.addFromPrincipal(
        principal,
        "webgl",
        aPerm,
        aPersistent
          ? Ci.nsIPermissionManager.EXPIRE_NEVER
          : Ci.nsIPermissionManager.EXPIRE_SESSION
      );
    }

    let mainAction = {
      label: gNavigatorBundle.getString("webgl.allow2"),
      accessKey: gNavigatorBundle.getString("webgl.allow2.accesskey"),
      callback(state) {
        setWebGLPermission(
          Ci.nsIPermissionManager.ALLOW_ACTION,
          state && state.checkboxChecked
        );
          let tab = window?.gBrowser?.getTabForBrowser(browser);
          if (tab) {
            window.gBrowser.reloadTab(tab);
          }
      },
    };

    let secondaryActions = [
      {
        label: gNavigatorBundle.getString("webgl.block"),
        accessKey: gNavigatorBundle.getString("webgl.block.accesskey"),
        callback(state) {
          setWebGLPermission(
            Ci.nsIPermissionManager.DENY_ACTION,
            state && state.checkboxChecked
          );
        },
      },
    ];

    let checkbox = {
      // In PB mode, we don't want the "always remember" checkbox
      show: !PrivateBrowsingUtils.isWindowPrivate(window),
    };
    if (checkbox.show) {
      checkbox.checked = true;
      checkbox.label = gBrowserBundle.GetStringFromName("webgl.remember2");
    }

    let options = {
      checkbox,
      name: principal.host,
      learnMoreURL:
        Services.urlFormatter.formatURLPref("app.support.baseURL") +
        "webgl-prompt",
      dismissed: aTopic == this._permissionsPromptHideDoorHanger,
      eventCallback(e) {
        if (e == "showing" && this?.browser?.ownerDocument) {
          this.browser.ownerDocument.getElementById(
            "webgl-permissions-prompt-warning"
          ).textContent = gBrowserBundle.GetStringFromName(
            "webgl.siteprompt2.warning"
          );
        }
      },
    };
    window.PopupNotifications.show(
      browser,
      this._permissionsPrompt,
      message,
      this._notificationIcon,
      mainAction,
      secondaryActions,
      options
    );
  },
};
