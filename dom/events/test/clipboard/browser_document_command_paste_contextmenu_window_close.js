/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const kContentFileUrl = kBaseUrlForContent + "simple_page_ext.html";

function promiseExecCommandPaste(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => {
    let clipboardData = null;
    content.document.addEventListener(
      "paste",
      e => {
        clipboardData = e.clipboardData.getData("text/plain");
      },
      { once: true }
    );

    content.document.notifyUserGestureActivation();
    const execCommandResult = Cu.waiveXrays(content.document).execCommand(
      "paste"
    );

    return { execCommandResult, clipboardData };
  });
}

function execCommandPasteWithoutWait(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => {
    SpecialPowers.executeSoon(() => {
      content.document.notifyUserGestureActivation();
      const execCommandResult = Cu.waiveXrays(content.document).execCommand(
        "paste"
      );
    });
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
  execCommandPasteWithoutWait(browser);
  await pasteButtonIsShown;

  info("Close the new browser window while paste context menu is shown");
  await BrowserTestUtils.closeWindow(newWin);
  await SimpleTest.promiseFocus(window);

  await BrowserTestUtils.withNewTab(kContentFileUrl, async aBrowser => {
    info("Trigger paste context menu in the original browser window");
    const pasteButtonIsShown = promisePasteButtonIsShown();
    const pasteCommandResult = promiseExecCommandPaste(aBrowser);
    await pasteButtonIsShown;

    info("Click the paste context menu");
    const pasteButtonIsHidden = promisePasteButtonIsHidden();
    await promiseClickPasteButton();
    await pasteButtonIsHidden;

    const { execCommandResult, clipboardData } = await pasteCommandResult;
    ok(execCommandResult, `execCommand("paste") should succeed`);
    is(clipboardData, clipboardText, `Check clipboard data`);
  });
});
