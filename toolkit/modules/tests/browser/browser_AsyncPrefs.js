"use strict";

const kBoolTestPref = "testing.allowed-prefs.some-bool-pref";
const kCharTestPref = "testing.allowed-prefs.some-char-pref";
const kIntTestPref = "testing.allowed-prefs.some-int-pref";

// We have to use a real pref because we don't want to include testing
// prefs for the web content process.
const kRealTestPref = "reader.font_size";

function resetPrefs() {
  for (let pref of [
    kBoolTestPref,
    kCharTestPref,
    kIntTestPref,
    kRealTestPref,
  ]) {
    Services.prefs.clearUserPref(pref);
  }
}

registerCleanupFunction(resetPrefs);

Services.prefs
  .getDefaultBranch("testing.allowed-prefs.")
  .setBoolPref("some-bool-pref", false);
Services.prefs
  .getDefaultBranch("testing.allowed-prefs.")
  .setCharPref("some-char-pref", "");
Services.prefs
  .getDefaultBranch("testing.allowed-prefs.")
  .setIntPref("some-int-pref", 0);

async function runTest() {
  let { AsyncPrefs } = ChromeUtils.importESModule(
    "resource://gre/modules/AsyncPrefs.sys.mjs"
  );

  // Need to define these again because when run in a content task we have no scope access.
  const kNotAllowed = "some.pref.thats.not.allowed";
  const kBoolTestPref = "testing.allowed-prefs.some-bool-pref";
  const kCharTestPref = "testing.allowed-prefs.some-char-pref";
  const kIntTestPref = "testing.allowed-prefs.some-int-pref";
  const kRealTestPref = "reader.font_size";

  let procDesc;
  switch (Services.appinfo.remoteType) {
    case null:
      procDesc = "parent process";
      break;
    case "privilegedabout":
      procDesc = "privileged about: process";
      break;
    default:
      procDesc = `${Services.appinfo.remoteType} child process`;
      break;
  }

  const valueResultMap = [
    [true, "Bool"],
    [false, "Bool"],
    [10, "Int"],
    [-1, "Int"],
    ["", "Char"],
    ["stuff", "Char"],
    [[], false],
    [{}, false],
    [Services.io.newURI("http://mozilla.org/"), false],
  ];

  const prefMap = [
    ["Bool", kBoolTestPref],
    ["Char", kCharTestPref],
    ["Int", kIntTestPref],
  ];

  function doesFail(pref, value) {
    let msg = `Should not succeed setting ${pref} to ${value} in ${procDesc}`;
    return AsyncPrefs.set(pref, value).then(
      () => ok(false, msg),
      error => ok(true, msg + "; " + error)
    );
  }

  function doesWork(pref, value) {
    let msg = `Should be able to set ${pref} to ${value} in ${procDesc}`;
    return AsyncPrefs.set(pref, value).then(
      () => ok(true, msg),
      error => ok(false, msg + "; " + error)
    );
  }

  function doReset(pref) {
    let msg = `Should be able to reset ${pref} in ${procDesc}`;
    return AsyncPrefs.reset(pref).then(
      () => ok(true, msg),
      () => ok(false, msg)
    );
  }

  for (let [val] of valueResultMap) {
    await doesFail(kNotAllowed, val);
    is(
      Services.prefs.prefHasUserValue(kNotAllowed),
      false,
      "Pref shouldn't get changed"
    );
  }

  let resetMsg = `Should not succeed resetting ${kNotAllowed} in ${procDesc}`;
  AsyncPrefs.reset(kNotAllowed).then(
    () => ok(false, resetMsg),
    error => ok(true, resetMsg + "; " + error)
  );

  let haveSomePrivilege =
    Services.appinfo.remoteType == null ||
    Services.appinfo.remoteType == "privilegedabout";
  for (let [type, pref] of prefMap) {
    for (let [val, result] of valueResultMap) {
      if (haveSomePrivilege && result == type) {
        await doesWork(pref, val);
        is(
          Services.prefs["get" + type + "Pref"](pref),
          val,
          "Pref should have been updated"
        );
        await doReset(pref);
      } else {
        await doesFail(pref, val);
        is(
          Services.prefs.prefHasUserValue(pref),
          false,
          `Pref ${pref} shouldn't get changed`
        );
      }
    }
  }

  let oldValue = Services.prefs.getIntPref(kRealTestPref);
  await AsyncPrefs.set(kRealTestPref, 2 * oldValue);
  Assert.equal(
    Services.prefs.getIntPref(kRealTestPref),
    2 * oldValue,
    `Should have been able to set ${kRealTestPref} from ${procDesc}`
  );
  await AsyncPrefs.reset(kRealTestPref);
  Assert.equal(
    Services.prefs.getIntPref(kRealTestPref),
    oldValue,
    `Should have been able to reset ${kRealTestPref} from ${procDesc}`
  );
}

describe("AsyncPrefs", function runInParent() {
  afterEach(resetPrefs);

  it("should work in the parent process", async function runInParent() {
    await runTest();
  });

  it("should work in the privileged about process", async function runInPrivilegedAbout() {
    await BrowserTestUtils.withNewTab(
      "about:privatebrowsing",
      async function (browser) {
        ok(
          browser.isRemoteBrowser,
          "Should actually run this in child process"
        );
        Assert.equal(
          browser.browsingContext.currentRemoteType,
          "privilegedabout",
          "Should be in a privileged about process"
        );
        await SpecialPowers.spawn(browser, [], runTest);
      }
    );
  });

  it("should work in the web child process", async function runInWebChild() {
    await BrowserTestUtils.withNewTab(
      "https://example.com/somewhere404",
      async function (browser) {
        ok(
          browser.isRemoteBrowser,
          "Should actually run this in child process"
        );
        Assert.equal(
          browser.browsingContext.currentRemoteType,
          "webIsolated=https://example.com",
          "Should be in a web isolated process"
        );
        await SpecialPowers.spawn(browser, [], runTest);
      }
    );
  });
});
