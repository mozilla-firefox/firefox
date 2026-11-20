/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(async function test_open_url_in_mini_window() {
  let tabCountBefore = gBrowser.tabs.length;

  let mini = await MiniWindowManager.openURL(
    window,
    "https://example.com/",
    "test",
    Services.scriptSecurityManager.getSystemPrincipal()
  );
  Assert.ok(mini, "Mini window opened");
  let miniWin = mini.miniWin;
  await TestUtils.waitForCondition(
    () => mini.browser.currentURI.spec == "https://example.com/",
    "Waiting for the URL to load in the mini window"
  );

  Assert.equal(mini.state, "active", "Mini window reached active state");
  Assert.ok(!mini.isCropped, "Mini window is full-tab");
  Assert.ok(
    miniWin.document.documentElement.hasAttribute("mini-window"),
    "Window is styled as a mini window"
  );
  Assert.equal(miniWin.gBrowser.tabs.length, 1, "Mini window has one tab");
  Assert.equal(
    gBrowser.tabs.length,
    tabCountBefore,
    "Origin window tabs are untouched"
  );
  Assert.equal(MiniWindowManager._miniwindows.size, 1, "One mini registered");

  mini.returnToOriginWin(true);
  await BrowserTestUtils.domWindowClosed(miniWin);
  Assert.equal(
    gBrowser.tabs.length,
    tabCountBefore + 1,
    "Tab moved into the origin window"
  );
  Assert.equal(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://example.com/",
    "Returned tab is selected"
  );
  assertNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_open_url_without_principal_fails() {
  let mini = await MiniWindowManager.openURL(
    window,
    "https://example.com/",
    "test",
    null
  );
  Assert.equal(mini, null, "Open without a triggeringPrincipal fails cleanly");
  assertNoMiniWindowsOpen();
});
