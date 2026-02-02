/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

add_setup(async function () {
  await setCookieBehaviorPref(
    BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN,
    false
  );
});

const TEST_TOP_A_PAGE = TEST_TOP_PAGE_HTTPS;
const TEST_IFRAME_B_PAGE = TEST_3RD_PARTY_PAGE;
const TEST_NESTED_B_REDIRECT_TO_NESTED_A_PAGE =
  TEST_3RD_PARTY_DOMAIN + TEST_PATH + "redirect.sjs?" + TEST_TOP_PAGE_HTTPS;

/**
 *
 */
add_task(async function runTest() {
  info("Creating the top-level A tab");
  let tab = BrowserTestUtils.addTab(gBrowser, TEST_TOP_A_PAGE);
  gBrowser.selectedTab = tab;

  let browser = tab.linkedBrowser;
  await BrowserTestUtils.browserLoaded(browser);

  info("Creating the AB depth=1 iframe");
  let ifrBC = await SpecialPowers.spawn(
    browser,
    [TEST_IFRAME_B_PAGE],
    async page => {
      let ifr = content.document.createElement("iframe");

      let loading = ContentTaskUtils.waitForEvent(ifr, "load");
      ifr.src = page;
      content.document.body.appendChild(ifr);
      await loading;

      return ifr.browsingContext;
    }
  );

  info("Creating the ABB iframe that will redirect to ABA");
  let ifrABABC = await SpecialPowers.spawn(
    ifrBC,
    [TEST_NESTED_B_REDIRECT_TO_NESTED_A_PAGE],
    async page => {
      let ifr = content.document.createElement("iframe");

      let loading = ContentTaskUtils.waitForEvent(ifr, "load");
      ifr.src = page;
      content.document.body.appendChild(ifr);
      await loading;

      return ifr.browsingContext;
    }
  );

  info("Register a ServiceWorker in the ABA iframe");
  await SpecialPowers.spawn(ifrABABC, [], async () => {
    const sw = content.navigator.serviceWorker;
    const controlled = ContentTaskUtils.waitForEvent(sw, "controllerchange");
    await sw.register("serviceWorker.js");
    await controlled;
  });

  // This hasn't actually caused crashes.
  info("Create an additional inheriting about:blank iframe in the ABA iframe");
  let abaInheritingControlled = await SpecialPowers.spawn(
    ifrABABC,
    ["about:blank"],
    async page => {
      if (!content.navigator.serviceWorker.controller) {
        throw new Error("should have been controlled");
      }

      let ifr = content.document.createElement("iframe");

      let loading = ContentTaskUtils.waitForEvent(ifr, "load");
      ifr.src = page;
      content.document.body.appendChild(ifr);
      await loading;

      return !!ifr.contentWindow.navigator.serviceWorker.controller;
    }
  );
  ok(
    abaInheritingControlled,
    "The iframe nested in the ABA should be controlled."
  );

  // Before the fix in bug 1910011 we would see the following assertion with
  // Signature:[@ mozilla::dom::ClientHandle::Control]
  // Mozilla crash reason: MOZ_RELEASE_ASSERT(ClientMatchPrincipalInfo(mClientInfo.PrincipalInfo(), aServiceWorker.PrincipalInfo()))
  info(
    "Re-navigate the ABA iframe back to ABB to have it redirect to ABA again"
  );
  const redirABABC = await SpecialPowers.spawn(
    ifrBC,
    [TEST_NESTED_B_REDIRECT_TO_NESTED_A_PAGE],
    async page => {
      let ifr = content.document.getElementsByTagName("iframe")[0];

      let loading = ContentTaskUtils.waitForEvent(ifr, "load");
      ifr.src = page;
      await loading;
      return ifr.browsingContext;
    }
  );

  const abaControlled = await SpecialPowers.spawn(redirABABC, [], async () => {
    return !!content.navigator.serviceWorker.controller;
  });

  ok(abaControlled, "The post-redirect iframe should be controlled.");

  info("Clean up");
  BrowserTestUtils.removeTab(tab);
  await new Promise(resolve => {
    Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, () =>
      resolve()
    );
  });
});
