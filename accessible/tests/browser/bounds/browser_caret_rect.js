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

  const caretBounds = [caretX.value, caretY.value, caretW.value, caretH.value];

  info(`Caret bounds: ${caretBounds}`);

  return caretBounds;
}

function getCollapsedRangeExtents(acc, offset) {
  const rangeX = {};
  const rangeY = {};
  const rangeW = {};
  const rangeH = {};
  acc.getRangeExtents(
    offset,
    offset,
    rangeX,
    rangeY,
    rangeW,
    rangeH,
    COORDTYPE_SCREEN_RELATIVE
  );

  const rangeBounds = [rangeX.value, rangeY.value, rangeW.value, rangeH.value];

  info(`Range ${offset}-${offset} bounds: ${rangeBounds}`);

  return rangeBounds;
}

async function fetchCollapsedRangeBounds(docAcc, acc) {
  const state = {};
  acc.getState(state, {});
  if (state.value & nsIAccessibleStates.STATE_FOCUSABLE) {
    // This pre-scrolls the accessible into view as a focus would do.
    let focused = waitForEvent(EVENT_FOCUS, acc);
    acc.takeFocus();
    await focused;

    // We need to blur the accessible so the caret does not interfere with
    // the bounds fetch.
    focused = waitForEvent(EVENT_FOCUS, docAcc);
    docAcc.takeFocus();
    await focused;
  }

  acc.QueryInterface(nsIAccessibleHyperText);

  // If this is a 0 length text accessible, we need to ensure that
  // characterCount is at least 1.
  const characterCount = acc.characterCount ? acc.characterCount : 1;
  const bounds = [];
  for (let offset = 0; offset < characterCount; offset++) {
    let linkIndex = acc.getLinkIndexAtOffset(offset);
    if (linkIndex != -1) {
      const link = acc
        .getLinkAt(linkIndex)
        .QueryInterface(nsIAccessibleText)
        .QueryInterface(nsIAccessible);
      bounds.push(...(await fetchCollapsedRangeBounds(docAcc, link)));
    } else {
      let rect = getCollapsedRangeExtents(acc, offset);
      bounds.push(rect);
    }
  }

  return bounds;
}

function testCaretRect(
  docAcc,
  id,
  offset,
  fetchedBounds,
  atLineEnd = false,
  isVertical = false
) {
  const acc = findAccessibleChildByID(docAcc, id, [nsIAccessibleText]);
  is(acc.caretOffset, offset, `Caret at offset ${offset}`);
  const atEnd = offset == acc.characterCount;
  const empty = offset == 0 && atEnd;
  let queryOffset = atEnd && !empty ? offset - 1 : offset;
  const atEndInNewLine = atEnd && acc.getCharacterAtOffset(queryOffset) == "\n";

  const [rangeX, rangeY, , rangeH] =
    fetchedBounds.length > queryOffset
      ? fetchedBounds[queryOffset]
      : [0, 0, 0, 0];

  const [caretX, caretY, caretW, caretH] = getCaretRect(docAcc, id);

  if (!empty) {
    // In case of an empty input `getRangeExtents()` will return the full accessible bounds.
    let [currRangeX, currRangeY, currRangeW] = getCollapsedRangeExtents(
      acc,
      acc.caretOffset
    );
    Assert.deepEqual(
      [caretX, caretY, caretW],
      [currRangeX, currRangeY, currRangeW],
      "Collapsed range extents at caret position should be identical caret rect"
    );
  }

  if (atEndInNewLine) {
    Assert.lessOrEqual(caretX, rangeX, "Caret x before range x");
  } else if (atEnd || atLineEnd) {
    Assert.greater(caretX, rangeX, "Caret x after last range x");
  } else {
    // Caret width changes depending on device pixel ratio. In RTL
    // text that would change the x where the caret is drawn by a pixel or two.
    isWithin(caretX, rangeX, 3, "Caret x similar to range x");
  }

  if (isVertical && atEnd) {
    Assert.greaterOrEqual(caretY, rangeY, "Caret y below range y");
  } else if (atEndInNewLine) {
    Assert.greater(caretY, rangeY, "Caret y below range y");
  } else if (atLineEnd) {
    Assert.less(caretY, rangeY, "Caret y above start line range.");
  } else {
    isWithin(caretY, rangeY, 3, "Caret y similar to range y");
  }

  ok(caretW, "Caret width is greater than 0");

  if (!empty) {
    // Depending on glyph, the range can be taller.
    isWithin(caretH, rangeH, 2, "Caret height similar to range height");
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

      let fetchedBounds = await fetchCollapsedRangeBounds(docAcc, input);

      info("Focusing input");
      let caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
      input.takeFocus();
      await caretMoved;
      testCaretRect(docAcc, "input", 0, fetchedBounds);
      info("Setting caretOffset to 1");
      caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
      input.caretOffset = 1;
      await caretMoved;
      testCaretRect(docAcc, "input", 1, fetchedBounds);
      info("Setting caretOffset to 2");
      caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
      input.caretOffset = 2;
      await caretMoved;
      testCaretRect(docAcc, "input", 2, fetchedBounds);
      info("Resetting caretOffset to 0");
      input.caretOffset = 0;

      const emptyInput = findAccessibleChildByID(docAcc, "emptyInput", [
        nsIAccessibleText,
      ]);

      fetchedBounds = await fetchCollapsedRangeBounds(docAcc, emptyInput);

      info("Focusing emptyInput");
      caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, emptyInput);
      emptyInput.takeFocus();
      await caretMoved;
      testCaretRect(docAcc, "emptyInput", 0, fetchedBounds);
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
  <textarea id="textarea">1234
56789
</textarea>
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

    const textarea = findAccessibleChildByID(docAcc, "textarea", [
      nsIAccessibleText,
    ]);
    let fetchedBounds = await fetchCollapsedRangeBounds(docAcc, textarea);

    info("Focusing textarea");
    let [offset, isAtLineEnd] = await moveCaret();
    is(offset, 0, "Caret at offset 0");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Moving caret right");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 1, "Caret at offset 1");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Moving caret right again");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 2, "Caret at offset 2");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Moving caret right again again");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 3, "Caret at offset 3");
    is(isAtLineEnd, true, "Caret at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Moving caret right stays at same offset, but on new line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 3, "Caret at offset 3");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Moving caret right in second line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 4, "Caret at offset 4");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Move caret right to new line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowRight");
    is(offset, 5, "Caret at offset 5");
    is(isAtLineEnd, false, "Caret not at end of line");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

    info("Move caret to end of previous line");
    [offset, isAtLineEnd] = await moveCaret("KEY_ArrowLeft");
    is(offset, 4, "Caret at offset 4");
    is(isAtLineEnd, false, "Caret at end line break");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);

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
    is(offset, 11, "Caret at offset 11");
    is(isAtLineEnd, false, "Caret at end line break");
    testCaretRect(docAcc, "textarea", offset, fetchedBounds, isAtLineEnd);
  }
);

function todoIsWithin(expected, got, within, msg) {
  if (Math.abs(got - expected) <= within) {
    todo(true, `${msg} - Got ${got}`);
  } else {
    todo(
      false,
      `${msg} - Got ${got}, expected ${expected} with error of ${within}`
    );
  }
}

/**
 * Test the caret rect and collapsed range in bidi text
 */
addAccessibleTask(
  `
<input id="input1" value="hello שלום world">
<input id="input2" dir="rtl" value="שלום hello עולם">
<textarea id="textarea" dir="rtl">שלום
עולם</textarea>
  `,
  async function testRTL(browser, docAcc) {
    const input1 = findAccessibleChildByID(docAcc, "input1", [
      nsIAccessibleText,
    ]);

    let fetchedBounds = await fetchCollapsedRangeBounds(docAcc, input1);

    info("Focusing input1");
    let caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input1);
    input1.takeFocus();
    await caretMoved;
    testCaretRect(docAcc, "input1", 0, fetchedBounds);

    info("Setting caretOffset to 1");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input1);
    input1.caretOffset = 1;
    await caretMoved;
    testCaretRect(docAcc, "input1", 1, fetchedBounds);

    info("Setting caretOffset to 6 (first in embedded RTL)");
    // Retrieving rangeX before caret goes there
    let [rangeX] = getCollapsedRangeExtents(input1, 6);
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input1);
    input1.caretOffset = 6;
    await caretMoved;
    let [caretX] = getCaretRect(docAcc, "input1");
    todoIsWithin(caretX, rangeX, 2, "Caret x similar to range x");

    info("Setting caretOffset to 7 (in embedded RTL)");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input1);
    input1.caretOffset = 7;
    await caretMoved;
    testCaretRect(docAcc, "input1", 7, fetchedBounds);

    info("Resetting caretOffset to 0");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input1);
    input1.caretOffset = 0;
    await caretMoved;

    const input2 = findAccessibleChildByID(docAcc, "input2", [
      nsIAccessibleText,
    ]);

    fetchedBounds = await fetchCollapsedRangeBounds(docAcc, input2);

    info("Focusing input2");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input2);
    input2.takeFocus();
    await caretMoved;
    testCaretRect(docAcc, "input2", 0, fetchedBounds);

    info("Setting caretOffset to 1");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input2);
    input2.caretOffset = 1;
    await caretMoved;
    testCaretRect(docAcc, "input2", 1, fetchedBounds);

    info("Setting caretOffset to 5 (first in embedded LTR)");
    // Retrieving rangeX before caret goes there
    [rangeX] = getCollapsedRangeExtents(input2, 5);
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input2);
    input2.caretOffset = 5;
    await caretMoved;
    [caretX] = getCaretRect(docAcc, "input1");
    todoIsWithin(caretX, rangeX, 2, "Caret x similar to range x");

    info("Setting caretOffset to 7 (in embedded LTR)");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input2);
    input2.caretOffset = 7;
    await caretMoved;
    testCaretRect(docAcc, "input2", 7, fetchedBounds);

    const textarea = findAccessibleChildByID(docAcc, "textarea", [
      nsIAccessibleText,
    ]);

    fetchedBounds = await fetchCollapsedRangeBounds(docAcc, textarea);

    info("Focusing textarea");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, textarea);
    textarea.takeFocus();
    await caretMoved;
    testCaretRect(docAcc, "textarea", 0, fetchedBounds);

    info("Setting caretOffset to 1");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, textarea);
    textarea.caretOffset = 1;
    await caretMoved;
    testCaretRect(docAcc, "textarea", 1, fetchedBounds);

    info("Setting caretOffset to 4 (before newline)");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, textarea);
    textarea.caretOffset = 4;
    await caretMoved;
    testCaretRect(docAcc, "textarea", 4, fetchedBounds);

    info("Setting caretOffset to 5 (after newline)");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, textarea);
    textarea.caretOffset = 5;
    await caretMoved;
    testCaretRect(docAcc, "textarea", 5, fetchedBounds);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test the caret rect in vertical text.
 */
addAccessibleTask(
  `
<input id="input" value="ab" style="writing-mode: vertical-lr;">
<input id="emptyInput" style="writing-mode: vertical-lr;">
  `,
  async function testVerticalInputs(browser, docAcc) {
    const input = findAccessibleChildByID(docAcc, "input", [nsIAccessibleText]);
    let fetchedBounds = await fetchCollapsedRangeBounds(docAcc, input);

    info("Focusing input");
    let caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
    input.takeFocus();
    await caretMoved;
    testCaretRect(docAcc, "input", 0, fetchedBounds, false, true);
    info("Setting caretOffset to 1");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
    input.caretOffset = 1;
    await caretMoved;
    testCaretRect(docAcc, "input", 1, fetchedBounds, false, true);
    info("Setting caretOffset to 2");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, input);
    input.caretOffset = 2;
    await caretMoved;
    testCaretRect(docAcc, "input", 2, fetchedBounds, false, true);
    info("Resetting caretOffset to 0");
    input.caretOffset = 0;

    const emptyInput = findAccessibleChildByID(docAcc, "emptyInput", [
      nsIAccessibleText,
    ]);
    fetchedBounds = await fetchCollapsedRangeBounds(docAcc, emptyInput);

    info("Focusing emptyInput");
    caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED, emptyInput);
    emptyInput.takeFocus();
    await caretMoved;
    testCaretRect(docAcc, "emptyInput", 0, fetchedBounds, false, true);
  }
);

/**
 * Test contenteditable caret
 */
addAccessibleTask(
  `
<div contenteditable="true" id="input" role="textbox">one <strong>two</strong> <i>three</i></div>
  `,
  async function testRichTextEdit(browser, docAcc) {
    const input = findAccessibleChildByID(docAcc, "input", [nsIAccessibleText]);
    let fetchedBounds = await fetchCollapsedRangeBounds(docAcc, input);

    let focused = waitForEvent(EVENT_FOCUS, input);
    input.takeFocus();
    info("Focusing input");
    await focused;
    testCaretRect(docAcc, "input", 0, fetchedBounds);

    for (let offset = 1; offset < fetchedBounds.length - 1; offset++) {
      info("Pressing ArrowRight to move caret to index " + offset);
      let caretMoved = waitForEvent(EVENT_TEXT_CARET_MOVED);
      EventUtils.synthesizeKey("KEY_ArrowRight");

      let evt = (await caretMoved).QueryInterface(nsIAccessibleCaretMoveEvent);

      let acc = evt.accessible.QueryInterface(nsIAccessibleHyperText);
      let caretOffset = evt.caretOffset;
      let linkIndex = acc.getLinkIndexAtOffset(evt.caretOffset);
      if (linkIndex != -1) {
        acc = acc
          .getLinkAt(linkIndex)
          .QueryInterface(nsIAccessibleText)
          .QueryInterface(nsIAccessible);
        caretOffset = 0;
      }

      const [rangeX, rangeY, rangeW, rangeH] =
        fetchedBounds.length > offset ? fetchedBounds[offset] : [0, 0, 0, 0];
      const [caretX, caretY, caretW, caretH] = getCollapsedRangeExtents(
        acc,
        caretOffset
      );

      info(
        `${offset}: Caret rect: ${caretX}, ${caretY}, ${caretW}, ${caretH}, Prefetched rect: ${rangeX}, ${rangeY}, ${rangeW}, ${rangeH}`
      );

      isWithin(rangeX, caretX, 2, "Caret x similar to range x");
      isWithin(rangeY, caretY, 2, "Caret y similar to range y");
      isWithin(rangeW, caretW, 2, "Caret width similar to range width");
    }
  }
);
