/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
// eslint-disable-next-line mozilla/no-redeclare-with-import-autofix
const { ExtensionTestCommon } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionTestCommon.sys.mjs"
);

AddonTestUtils.initMochitest(this);

async function checkNoFileOrJarURLs(extensionURL) {
  // Load the root of the extension using moz-extension:// protocol
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: extensionURL,
    },
    async browser => {
      info("Successfully loaded extension root page");

      // Check the page title
      let title = await SpecialPowers.spawn(browser, [], () => {
        return content.document.title;
      });
      is(
        title,
        `Index of ${extensionURL}`,
        `Page title should start with "Index of moz-extension://", got: ${title}`
      );

      // Check that at least the manifest entry is there
      let hasManifest = await SpecialPowers.spawn(browser, [], () => {
        for (let elem of content.document.querySelectorAll("[href]")) {
          let url = elem.href;
          if (url.endsWith("manifest.json")) {
            return true;
          }
        }
        return false;
      });
      ok(hasManifest, "Directory listing should include manifest.json");

      // Check that no URLs are file:// or jar://
      let invalidURLs = await SpecialPowers.spawn(browser, [], () => {
        let invalid = [];
        for (let elem of content.document.querySelectorAll("[href],[src]")) {
          let url = elem.href || elem.src;
          if (url.startsWith("file:") || url.startsWith("jar:")) {
            invalid.push(elem.outerHTML);
          }
        }
        return invalid;
      });

      Assert.deepEqual(
        invalidURLs,
        [],
        "No URLs should use file:// or jar:// schemes."
      );
    }
  );
}

// Test that all the URLs in the dirlisting of an extensions root dir
// does not include any file or jar URLs when extensions.webextensions.protocol.remote
// is false.
add_task(async function test_webextension_dir_listing() {
  // Verify the pref is set correctly
  is(
    Services.prefs.getBoolPref("extensions.webextensions.protocol.remote"),
    false,
    "extensions.webextensions.protocol.remote should be false"
  );

  // Get the extension's base URL. This is a packed extension
  // that happens to already be a part of the test environment.
  let policy = WebExtensionPolicy.getByID("mochikit@mozilla.org");
  ok(policy, "This extension must exist");

  let extensionURL = policy.getURL("/");
  info(`Extension base URL: ${extensionURL}`);
  await checkNoFileOrJarURLs(extensionURL);
});

add_task(async function test_temp_webextension_dir_listing() {
  // Verify the pref is set correctly
  is(
    Services.prefs.getBoolPref("extensions.webextensions.protocol.remote"),
    false,
    "extensions.webextensions.protocol.remote should be false"
  );

  const ID = "addon@tests.mozilla.org";

  const addonDir = AddonTestUtils.tempDir.clone().clone();
  addonDir.append("exttest");
  await AddonTestUtils.promiseWriteFilesToDir(
    addonDir.path,
    ExtensionTestCommon.generateFiles({
      manifest: {
        browser_specific_settings: { gecko: { id: ID } },
        version: "1.0",
      },
    })
  );

  // Wait for the extension to start up
  let startupPromise = AddonTestUtils.promiseWebExtensionStartup(ID);
  let addon = await AddonManager.installTemporaryAddon(addonDir);
  await startupPromise;

  // Get the extension policy
  let policy = WebExtensionPolicy.getByID(ID);
  ok(policy, "Extension policy found");

  // Get the extension's base URL and check it
  let extensionURL = policy.getURL("/");
  info(`Temporary extension base URL: ${extensionURL}`);
  await checkNoFileOrJarURLs(extensionURL);

  // Unload the extension
  await addon.uninstall();

  // Clean up the temporary directory
  addonDir.remove(true);
});
