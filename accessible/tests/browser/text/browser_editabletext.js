/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

async function testEditable(browser, acc, aBefore = "", aAfter = "") {
  async function resetInput() {
    if (acc.childCount <= 1) {
      return;
    }

    let emptyInputEvent = waitForEvent(EVENT_TEXT_VALUE_CHANGE, "input");
    await invokeContentTask(browser, [], async () => {
      content.document.getElementById("input").innerHTML = "";
    });

    await emptyInputEvent;
  }

  // ////////////////////////////////////////////////////////////////////////
  // insertText
  await testInsertText(acc, "hello", 0, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hello", aAfter]);
  await testInsertText(acc, "ma ", 0, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "ma hello", aAfter]);
  await testInsertText(acc, "ma", 2, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "mama hello", aAfter]);
  await testInsertText(acc, " hello", 10, aBefore.length);
  await isFinalValueCorrect(browser, acc, [
    aBefore,
    "mama hello hello",
    aAfter,
  ]);

  // ////////////////////////////////////////////////////////////////////////
  // deleteText
  await testDeleteText(acc, 0, 5, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hello hello", aAfter]);
  await testDeleteText(acc, 5, 6, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hellohello", aAfter]);
  await testDeleteText(acc, 5, 10, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hello", aAfter]);
  await testDeleteText(acc, 0, 5, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "", aAfter]);

  // XXX: clipboard operation tests don't work well with editable documents.
  if (acc.role == ROLE_DOCUMENT) {
    return;
  }

  await resetInput();

  // copyText and pasteText
  await testInsertText(acc, "hello", 0, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hello", aAfter]);

  await testCopyText(acc, 0, 1, aBefore.length, browser, "h");
  await testPasteText(acc, 1, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hhello", aAfter]);

  await testCopyText(acc, 5, 6, aBefore.length, browser, "o");
  await testPasteText(acc, 6, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hhelloo", aAfter]);

  await testCopyText(acc, 2, 3, aBefore.length, browser, "e");
  await testPasteText(acc, 1, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "hehelloo", aAfter]);

  // cut & paste
  await testCutText(acc, 0, 1, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "ehelloo", aAfter]);
  await testPasteText(acc, 2, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "ehhelloo", aAfter]);

  await testCutText(acc, 3, 4, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "ehhlloo", aAfter]);
  await testPasteText(acc, 6, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "ehhlloeo", aAfter]);

  await testCutText(acc, 0, 8, aBefore.length);
  await isFinalValueCorrect(browser, acc, [aBefore, "", aAfter]);

  await resetInput();

  // ////////////////////////////////////////////////////////////////////////
  // setTextContents
  await testSetTextContents(acc, "hello", aBefore.length, [
    EVENT_TEXT_INSERTED,
  ]);
  await isFinalValueCorrect(browser, acc, [aBefore, "hello", aAfter]);
  await testSetTextContents(acc, "katze", aBefore.length, [
    EVENT_TEXT_REMOVED,
    EVENT_TEXT_INSERTED,
  ]);
  await isFinalValueCorrect(browser, acc, [aBefore, "katze", aAfter]);
}

addAccessibleTask(
  `<input id="input"/>`,
  async function (browser, docAcc) {
    await testEditable(browser, findAccessibleChildByID(docAcc, "input"));
  },
  { chrome: true, topLevel: true }
);

addAccessibleTask(
  `<div id="input" contenteditable="true" role="textbox"></div>`,
  async function (browser, docAcc) {
    await testEditable(
      browser,
      findAccessibleChildByID(docAcc, "input"),
      "",
      ""
    );
  },
  { chrome: true, topLevel: false /* bug 1834129 */ }
);

addAccessibleTask(
  `<style>
  #input::after {
    content: "pseudo element";
  }
</style>
<div id="input" contenteditable="true" role="textbox"></div>`,
  async function (browser, docAcc) {
    await testEditable(
      browser,
      findAccessibleChildByID(docAcc, "input"),
      "",
      "pseudo element"
    );
  },
  { chrome: true, topLevel: false /* bug 1834129 */ }
);

addAccessibleTask(
  `<style>
  #input::before {
    content: "pseudo element";
  }
</style>
<div id="input" contenteditable="true" role="textbox"></div>`,
  async function (browser, docAcc) {
    await testEditable(
      browser,
      findAccessibleChildByID(docAcc, "input"),
      "pseudo element"
    );
  },
  { chrome: true, topLevel: false /* bug 1834129 */ }
);

addAccessibleTask(
  `<style>
  #input::before {
    content: "before";
  }
  #input::after {
    content: "after";
  }
</style>
<div id="input" contenteditable="true" role="textbox"></div>`,
  async function (browser, docAcc) {
    await testEditable(
      browser,
      findAccessibleChildByID(docAcc, "input"),
      "before",
      "after"
    );
  },
  { chrome: true, topLevel: false /* bug 1834129 */ }
);

addAccessibleTask(
  `<style>
  br {
    position: fixed;
  }
</style>
<div id="input" contenteditable="true" role="textbox"></div>`,
  async function (browser, docAcc) {
    document.execCommand("insertText", false, "a");
    document.execCommand("delete");
    await testEditable(browser, findAccessibleChildByID(docAcc, "input"));
  },
  { chrome: true, topLevel: false /* bug 1834129 */ }
);

addAccessibleTask(
  `<style>
  #input {
    white-space: pre;
  }
  #input::before {
    content: "before";
  }
  #input::after {
    content: "after";
  }
</style>
<div id="input" contenteditable="plaintext-only" role="textbox"></div>`,
  async function (browser, docAcc) {
    await testEditable(
      browser,
      findAccessibleChildByID(docAcc, "input"),
      "before",
      "after"
    );
  },
  { chrome: true, topLevel: false /* bug 1834129 */ }
);

addAccessibleTask(
  ``,
  async function (browser, docAcc) {
    await testEditable(browser, docAcc);
  },
  {
    chrome: true,
    topLevel: true,
    contentDocBodyAttrs: { contentEditable: "true" },
  }
);

/**
 * Test PasteText replacement of selected text.
 */
addAccessibleTask(
  `<input id="input" value="abcdef">`,
  async function testPasteTextReplace(browser, docAcc) {
    const input = findAccessibleChildByID(docAcc, "input");
    let focused = waitForEvent(EVENT_FOCUS, input);
    info("Focusing input");
    input.takeFocus();
    await focused;
    info("Copying ef");
    input.QueryInterface(nsIAccessibleEditableText);
    let selected = waitForEvent(EVENT_TEXT_SELECTION_CHANGED, input);
    input.copyText(4, 6);
    await selected;
    info("Selecting bc");
    selected = waitForEvent(EVENT_TEXT_SELECTION_CHANGED, input);
    await invokeContentTask(browser, [], () => {
      const inputDom = content.document.getElementById("input");
      inputDom.selectionStart = 1;
      inputDom.selectionEnd = 3;
    });
    await selected;
    info("Pasting at caret");
    let changed = waitForEvents([
      [EVENT_TEXT_REMOVED, input],
      [EVENT_TEXT_INSERTED, input],
      [EVENT_TEXT_VALUE_CHANGE, input],
    ]);
    input.pasteText(nsIAccessibleText.TEXT_OFFSET_CARET);
    await changed;
    is(input.value, "aefdef", "input value correct after pasting");
  }
);
