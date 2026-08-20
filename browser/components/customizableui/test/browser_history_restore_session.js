/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

add_task(async function testRestoreSession() {
  info("Check history panel's restore previous session button");

  let win = await BrowserTestUtils.openNewBrowserWindow();
  CustomizableUI.addWidgetToArea(
    "history-panelmenu",
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  registerCleanupFunction(() => CustomizableUI.reset());

  // We need to make sure the history is cleared before starting the test
  await Sanitizer.sanitize(["history"]);

  await openHistoryPanel(win.document);

  let restorePrevSessionBtn = win.document.getElementById(
    "appMenu-restoreSession"
  );

  Assert.ok(
    restorePrevSessionBtn.hidden,
    "Restore previous session button is not visible"
  );
  await hideHistoryPanel(win.document);

  BrowserTestUtils.addTab(win.gBrowser, "about:mozilla");
  await BrowserTestUtils.closeWindow(win);

  win = await BrowserTestUtils.openNewBrowserWindow();

  let lastSession = ChromeUtils.importESModule(
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs"
  )._LastSession;
  lastSession.setState(true);

  await openHistoryPanel(win.document);

  restorePrevSessionBtn = win.document.getElementById("appMenu-restoreSession");
  Assert.ok(
    !restorePrevSessionBtn.hidden,
    "Restore previous session button is visible"
  );

  await hideHistoryPanel(win.document);
  await BrowserTestUtils.closeWindow(win);
});
