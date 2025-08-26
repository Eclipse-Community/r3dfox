/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

function getCaretRect(docAcc, id) {
  const acc = findAccessibleChildByID(docAcc, id, [nsIAccessibleText]);
  const caretX = {};
  const caretY = {};
  const caretW = {};
  const caretH = {};
  acc.getCaretRect(caretX, caretY, caretW, caretH);
  info(
    `Caret bounds: ${[caretX.value, caretY.value, caretW.value, caretH.value]}`
  );
  return [caretX.value, caretY.value, caretW.value, caretH.value];
}

function testCaretRect(browser, docAcc, id, offset, atLineEnd = false) {
  const acc = findAccessibleChildByID(docAcc, id, [nsIAccessibleText]);
  is(acc.caretOffset, offset, `Caret at offset ${offset}`);
  const charX = {};
  const charY = {};
  const charW = {};
  const charH = {};
  const atEnd = offset == acc.characterCount;
  const empty = offset == 0 && atEnd;
  let queryOffset = atEnd && !empty ? offset - 1 : offset;
  const atEndInNewLine = atEnd && acc.getCharacterAtOffset(queryOffset) == "\n";
  if (atEndInNewLine) {
    queryOffset--;
  }
  acc.getCharacterExtents(
    queryOffset,
    charX,
    charY,
    charW,
    charH,
    COORDTYPE_SCREEN_RELATIVE
  );
  info(
    `Character ${queryOffset} bounds: ${charX.value}, ${charY.value}, ${charW.value}, ${charH.value}`
  );
  const [caretX, caretY, caretW, caretH] = getCaretRect(docAcc, id);

  if (atEndInNewLine) {
    Assert.lessOrEqual(caretX, charX.value, "Caret x before character x");
  } else if (atEnd || atLineEnd) {
    Assert.greater(caretX, charX.value, "Caret x after last character x");
  } else {
    is(caretX, charX.value, "Caret x same as character x");
  }

  if (atEndInNewLine) {
    Assert.greater(caretY, charY.value, "Caret y below character y");
  } else if (atLineEnd) {
    Assert.less(caretY, charY.value, "Caret y above start line character.");
  } else {
    is(caretY, charY.value, "Caret y same as character y");
  }

  ok(caretW, "Caret width is greater than 0");

  if (!empty) {
    is(caretH, charH.value, "Caret height same as character height");
  }
}

function getAccBounds(acc) {
  const x = {};
  const y = {};
  const w = {};
  const h = {};
  acc.getBounds(x, y, w, h);
  return [x.value, y.value, w.value, h.value];
}

/**
 * Test the caret rect in content documents.
 */
addAccessibleTask(
  `
<input id="input" value="ab">
<input id="emptyInput">
  `,
  async function (browser, docAcc) {
    async function runTests() {
      const input = findAccessibleChildByID(docAcc, "input", [
        nsIAccessibleText,
      ]);
      info("Focusing input");
      let caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
      input.takeFocus();
      await caretMoved;
      testCaretRect(browser, docAcc, "input", 0);
      info("Setting caretOffset to 1");
      caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
      input.caretOffset = 1;
      await caretMoved;
      testCaretRect(browser, docAcc, "input", 1);
      info("Setting caretOffset to 2");
      caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
      input.caretOffset = 2;
      await caretMoved;
      testCaretRect(browser, docAcc, "input", 2);
      info("Resetting caretOffset to 0");
      input.caretOffset = 0;

      const emptyInput = findAccessibleChildByID(docAcc, "emptyInput", [
        nsIAccessibleText,
      ]);
      info("Focusing emptyInput");
      caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, emptyInput);
      emptyInput.takeFocus();
      await caretMoved;
      testCaretRect(browser, docAcc, "emptyInput", 0);
    }

    await runTests();

    // Check that the caret rect is correct when the title bar is shown.
    if (LINUX || Services.env.get("MOZ_HEADLESS")) {
      // Disabling tabs in title bar doesn't change the bounds on Linux or in
      // headless mode.
      info("Skipping title bar tests");
      return;
    }
    const [, origDocY] = getAccBounds(docAcc);
    info("Showing title bar");
    let titleBarChanged = BrowserTestUtils.waitForMutationCondition(
      document.documentElement,
      { attributes: true, attributeFilter: ["tabsintitlebar"] },
      () => !document.documentElement.hasAttribute("tabsintitlebar")
    );
    await SpecialPowers.pushPrefEnv({
      set: [["browser.tabs.inTitlebar", false]],
    });
    await titleBarChanged;
    const [, newDocY] = getAccBounds(docAcc);
    Assert.greater(
      newDocY,
      origDocY,
      "Doc has larger y after title bar change"
    );
    await runTests();
    await SpecialPowers.popPrefEnv();
  },
  { chrome: true, topLevel: true }
);

/**
 * Test the caret rect in multiline content.
 */
addAccessibleTask(
  `<style>
    @font-face {
      font-family: Ahem;
      src: url(${CURRENT_CONTENT_DIR}e10s/fonts/Ahem.sjs);
    }
   textarea {
      font: 10px/10px Ahem;
      width: 30px;
      height: 80px;
    }
  </style>
  <textarea id="textarea">123456789</textarea>
  `,
  async function testMultiline(browser, docAcc) {
    async function moveCaret(key, keyopts = {}) {
      let caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, "textarea");
      if (key) {
        EventUtils.synthesizeKey(key, keyopts);
      } else {
        // If no key is provided, just focus the textarea.
        findAccessibleChildByID(docAcc, "textarea").takeFocus();
      }

      let evt = await caretMoved;
      evt.QueryInterface(nsIAccessibleCaretMoveEvent);
      return [evt.caretOffset, evt.isAtEndOfLine];
    }

    info("Focusing textarea");
    let [offset, isAtLineEnd] = await moveCaret();
    is(offset, 0, "Caret at offset 0");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Moving caret right");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 1, "Caret at offset 1");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Moving caret right again");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 2, "Caret at offset 2");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Moving caret right again again");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 3, "Caret at offset 3");
    is(isAtLineEnd, true, "Caret at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Moving caret right stays at same offset, but on new line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 3, "Caret at offset 3");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Moving caret right in second line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 4, "Caret at offset 4");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Pressing enter and breaking line");
    [offset, isAtLineEnd] = await moveCaret("KEY_Enter");
    is(offset, 5, "Caret at offset 5");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Move caret to end of previous line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowLeft");
    is(offset, 4, "Caret at offset 4");
    is(isAtLineEnd, false, "Caret at end line break");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Move caret to end of text");
    if (AppConstants.platform == "macosx") {
      [offset, isAtLineEnd] = await moveCaret("KEY_PageDown", {
        altKey: true,
      });
    } else {
      [offset, isAtLineEnd] = await moveCaret("KEY_End", {
        ctrlKey: true,
      });
    }
    is(offset, 10, "Caret at offset 10");
    is(isAtLineEnd, false, "Caret at end line break");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);

    info("Pressing enter and creating a new line");
    [offset, isAtLineEnd] = await moveCaret("KEY_Enter");
    is(offset, 11, "Caret at offset 11");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(browser, docAcc, "textarea", offset, isAtLineEnd);
  }
);
