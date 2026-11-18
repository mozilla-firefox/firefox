/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Test that the eyedropper can copy colors to the clipboard

const { TYPES } = ChromeUtils.importESModule(
  "resource://devtools/shared/highlighters.mjs"
);
const HIGHLIGHTER_TYPE = TYPES.EYEDROPPER;
const ID = "eye-dropper-";
const TEST_URI =
  "data:text/html;charset=utf-8,<style>html{background:red}</style>";

add_task(async function () {
  const helper = await openInspectorForURL(TEST_URI).then(
    getHighlighterHelperFor(HIGHLIGHTER_TYPE)
  );
  helper.prefix = ID;

  const {
    show,
    finalize,
    waitForElementAttributeSet,
    waitForElementAttributeRemoved,
  } = helper;

  info("Show the eyedropper with the copyOnSelect option");
  await show("html", { copyOnSelect: true });

  info(
    "Make sure to wait until the eyedropper is done taking a screenshot of the page"
  );
  await waitForElementAttributeSet("root", "drawn", helper);

  await waitForClipboardPromise(() => {
    info("Activate the eyedropper so the background color is copied");
    EventUtils.synthesizeKey("KEY_Enter");
  }, "#ff0000");

  ok(true, "The clipboard contains the right value");

  await waitForElementAttributeRemoved("root", "drawn", helper);
  await waitForElementAttributeSet("root", "hidden", helper);
  ok(true, "The eyedropper is now hidden");

  info("Check that the clipboard still contains the copied color");
  is(SpecialPowers.getClipboardData("text/plain"), "#ff0000");

  info("Replace the clipboard content with another text");
  SpecialPowers.clipboardCopyString("not-a-color");
  is(SpecialPowers.getClipboardData("text/plain"), "not-a-color");

  info("Click on the page again, check the clipboard was not updated");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "body",
    {},
    gBrowser.selectedBrowser
  );
  // Wait 500ms because nothing is observable when the test is successful.
  await wait(500);
  is(SpecialPowers.getClipboardData("text/plain"), "not-a-color");

  finalize();
});
