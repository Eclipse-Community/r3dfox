"use strict";

const PATH_NET = TEST_PATH + "file_dummy.html";
const PATH_ORG = PATH_NET.replace("example.net", "example.org");

async function runTest(defaultZoom) {
  let tab1, tab1Zoom;

  tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, PATH_NET);

  tab1Zoom = ZoomManager.getZoomForBrowser(tab1.linkedBrowser);
  is(tab1Zoom, defaultZoom, "We are starting with the default zoom.");

  await FullZoom.setZoom(1.25, tab1.linkedBrowser);
  tab1Zoom = ZoomManager.getZoomForBrowser(tab1.linkedBrowser);

  await new Promise(resolve => {
    Services.clearData.deleteDataFromHost(
      PATH_NET,
      true /* user request */,
      Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE,
      _ => {
        resolve();
      }
    );
  });

  is(
    tab1Zoom,
    1.25,
    `privacy.resistFingerprinting is false, site-specific zoom should not be reset when clearing FPP state`
  );

  await SpecialPowers.pushPrefEnv({
    set: [["privacy.resistFingerprinting", true]],
  });

  await new Promise(resolve => {
    Services.clearData.deleteDataFromHost(
      PATH_NET,
      true /* user request */,
      Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE,
      _ => {
        resolve();
      }
    );
  });

  tab1Zoom = ZoomManager.getZoomForBrowser(tab1.linkedBrowser);

  is(
    tab1Zoom,
    defaultZoom,
    "privacy.resistFingerprinting is true, site-specific zoom should be reset when clearing FPP state for tab1"
  );

  await FullZoom.reset();

  BrowserTestUtils.removeTab(tab1);

  await SpecialPowers.popPrefEnv();
}

add_task(async function () {
  await runTest(1.0);

  let defaultZoom = 1.5;
  let context = Cu.createLoadContext();
  let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
    Ci.nsIContentPrefService2
  );
  let { promise, resolve, reject } = Promise.withResolvers();
  cps2.setGlobal(FullZoom.name, defaultZoom, context, {
    handleError(error) {
      reject(error);
    },
    handleCompletion() {
      resolve();
    },
  });
  await promise;
  try {
    await runTest(defaultZoom);
  } finally {
    cps2.removeGlobal(FullZoom.name, context);
  }
});
