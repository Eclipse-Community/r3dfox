/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

addAccessibleTask(
  `test`,
  async function testRecreation(browser, iframeDocAcc, topDocAcc) {
    let iframe = findAccessibleChildByID(topDocAcc, DEFAULT_IFRAME_ID);
    is(iframeDocAcc.parent, iframe, "iframe doc's parent is iframe");
    // The ARIA role currently causes re-creation. If that ever changes, we'll
    // need to switch to another technique here.
    info("Change iframe's role to recreate it");
    let shown = waitForEvent(EVENT_SHOW, DEFAULT_IFRAME_ID);
    let reordered = waitForEvent(EVENT_REORDER, DEFAULT_IFRAME_ID);
    await SpecialPowers.spawn(
      topDocAcc.browsingContext,
      [DEFAULT_IFRAME_ID],
      id => {
        content.document.getElementById(id).role = "foo";
      }
    );
    iframe = (await shown).accessible;
    await reordered;
    is(iframeDocAcc.parent, iframe, "iframe doc's parent is iframe");
  },
  { chrome: false, topLevel: false, iframe: true, remoteIframe: true }
);
