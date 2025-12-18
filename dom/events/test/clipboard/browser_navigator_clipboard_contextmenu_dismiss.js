/* -*- Mode: JavaScript; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const kBaseUrlForContent = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);
const kContentFileUrl = kBaseUrlForContent + "file_toplevel.html";
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_native_event_utils.js",
  this
);

async function readText(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => {
    content.document.notifyUserGestureActivation();
    content.eval(`navigator.clipboard.readText();`);
  });
}

add_task(async function test_context_menu_dimiss_tab_navigate() {
  await BrowserTestUtils.withNewTab(kContentFileUrl, async aBrowser => {
    info(`Randomized text to avoid overlappings with other tests`);
    await promiseWritingRandomTextToClipboard();

    info(`Wait for paste context menu is shown`);
    let pasteButtonIsShown = promisePasteButtonIsShown();
    await readText(aBrowser);
    await pasteButtonIsShown;

    info("Navigate tab");
    let pasteButtonIsHidden = promisePasteButtonIsHidden();
    aBrowser.loadURI(Services.io.newURI("https://example.com/"), {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });

    info(`Wait for paste context menu is hidden`);
    await pasteButtonIsHidden;
  });
});

add_task(async function test_context_menu_dimiss_tab_reload() {
  await BrowserTestUtils.withNewTab(kContentFileUrl, async aBrowser => {
    info(`Randomized text to avoid overlappings with other tests`);
    await promiseWritingRandomTextToClipboard();

    info(`Wait for paste context menu is shown`);
    let pasteButtonIsShown = promisePasteButtonIsShown();
    await readText(aBrowser);
    await pasteButtonIsShown;

    info("Reload tab");
    let pasteButtonIsHidden = promisePasteButtonIsHidden();
    await BrowserTestUtils.reloadTab(gBrowser.selectedTab);

    info(`Wait for paste context menu is hidden`);
    await pasteButtonIsHidden;
  });
});
