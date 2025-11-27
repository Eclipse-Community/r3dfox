/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_hidden_reveal_password() {
  let login = Cc["@mozilla.org/login-manager/loginInfo;1"].createInstance(
    Ci.nsILoginInfo
  );
  login.init("https://example.com", "", null, "username", "password");
  login.QueryInterface(Ci.nsILoginMetaInfo);
  login.timePasswordChanged = Date.now();
  await Services.logins.addLoginAsync(login);

  await setupPolicyEngineWithJson({
    policies: {
      DisablePasswordReveal: true,
    },
  });

  let aboutLoginsTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: "about:logins",
  });

  let browser = gBrowser.selectedBrowser;

  await SpecialPowers.spawn(browser, [], () => {
    let loginItem = Cu.waiveXrays(content.document.querySelector("login-item"));

    let passwordReveal = loginItem.shadowRoot.querySelector(
      ".reveal-password-checkbox"
    );
    is(passwordReveal.hidden, true, "Password reveal button should be hidden");
  });
  BrowserTestUtils.removeTab(aboutLoginsTab);

  await Services.logins.removeAllLogins();
});

add_task(async function test_bug_1696948() {
  await setupPolicyEngineWithJson({
    policies: {
      DisablePasswordReveal: true,
    },
  });

  let aboutLoginsTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: "about:logins",
  });

  let browser = gBrowser.selectedBrowser;

  await SpecialPowers.spawn(browser, [], () => {
    let loginList = Cu.waiveXrays(content.document.querySelector("login-list"));
    let createButton = loginList._createLoginButton;
    ok(
      !createButton.disabled,
      "Create button should not be disabled initially"
    );
    let loginItem = Cu.waiveXrays(content.document.querySelector("login-item"));

    createButton.click();

    let passwordReveal = loginItem.shadowRoot.querySelector(
      ".reveal-password-checkbox"
    );
    is(passwordReveal.hidden, true, "Password reveal button should be hidden");

    // Bug 1696948
    let passwordInput = loginItem.shadowRoot.querySelector(
      "input[name='password']"
    );
    isnot(passwordInput, null, "Password field should be in the DOM");
  });
  BrowserTestUtils.removeTab(aboutLoginsTab);
});
