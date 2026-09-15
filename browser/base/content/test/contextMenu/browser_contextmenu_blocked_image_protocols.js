/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "contextmenu_common.js",
  this
);

const IMAGE_PROTOCOLS_IDS = ["moz-icon", "page-icon", "moz-remote-image"];

add_task(async function test_blocked() {
  for (const protocol of IMAGE_PROTOCOLS_IDS) {
    info(`Testing contextmenu with the ${protocol}: protocol`);

    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: getRootDirectory(gTestPath) + "file_blocked_image_protocols.html",
      },
      async browser => {
        await SpecialPowers.spawn(browser, [`#${protocol}`], async selector => {
          const img = content.document.querySelector(selector);
          if (!img.complete) {
            await ContentTaskUtils.waitForEvent(img, "load");
          }
        });

        let contextMenu = document.getElementById("contentAreaContextMenu");
        let popupShown = BrowserTestUtils.waitForEvent(
          contextMenu,
          "popupshown"
        );

        await BrowserTestUtils.synthesizeMouse(
          `#${protocol}`,
          2,
          2,
          { type: "contextmenu", button: 2 },
          browser
        );
        await popupShown;

        let viewImageItem = document.getElementById("context-viewimage");
        ok(viewImageItem.hidden, "View Image menu item should be hidden");

        let saveImageItem = document.getElementById("context-saveimage");
        ok(saveImageItem.hidden, "Save Image menu item should be hidden");

        let sendImageItem = document.getElementById("context-sendimage");
        ok(!sendImageItem.hidden, "Send Image menu item should be shown");

        contextMenu.hidePopup();
      }
    );
  }
});
