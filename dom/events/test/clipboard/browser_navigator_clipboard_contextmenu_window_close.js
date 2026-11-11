/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const kContentFileUrl = kBaseUrlForContent + "file_toplevel.html";

async function readText(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => {
    content.document.notifyUserGestureActivation();
    return content.eval(`navigator.clipboard.readText();`);
  });
}

add_task(async function test_paste_contextmenu_after_window_close() {
  const clipboardText = await promiseWritingRandomTextToClipboard();

  info("Open a new browser window");
  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  const tab = await BrowserTestUtils.openNewForegroundTab(
    newWin.gBrowser,
    kContentFileUrl
  );
  const browser = tab.linkedBrowser;
  await SimpleTest.promiseFocus(browser);

  info("Trigger paste context menu in the new browser window");
  const pasteButtonIsShown = promisePasteButtonIsShown(newWin.document);
  readText(browser).catch(() => {});
  await pasteButtonIsShown;

  info("Close the new browser window while paste context menu is shown");
  await BrowserTestUtils.closeWindow(newWin);
  await SimpleTest.promiseFocus(window);

  await BrowserTestUtils.withNewTab(kContentFileUrl, async aBrowser => {
    info("Trigger paste context menu in the original browser window");
    const pasteButtonIsShown = promisePasteButtonIsShown();
    const readTextRequest = readText(aBrowser);
    await pasteButtonIsShown;

    info("Click the paste context menu");
    const pasteButtonIsHidden = promisePasteButtonIsHidden();
    await promiseClickPasteButton();
    await pasteButtonIsHidden;

    is(
      await readTextRequest,
      clipboardText,
      "readText() should be resolved with clipboard data"
    );
  });
});
