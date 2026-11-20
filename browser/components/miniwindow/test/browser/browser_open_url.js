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

add_task(async function test_open_link_in_mini_window_from_context_menu() {
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    let link = content.document.createElement("a");
    link.id = "link";
    link.href = "https://example.org/";
    link.textContent = "link";
    content.document.body.prepend(link);
  });

  let contextMenu = document.getElementById("contentAreaContextMenu");
  let shown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link",
    { type: "contextmenu" },
    tab.linkedBrowser
  );
  await shown;

  let item = document.getElementById("context-openlinkminiwindow");
  Assert.ok(!item.hidden, "Open Link in Mini Window item is shown");

  let newWin = BrowserTestUtils.waitForNewWindow({
    url: "https://example.org/",
  });
  contextMenu.activateItem(item);
  let miniWin = await newWin;

  Assert.equal(MiniWindowManager._miniwindows.size, 1, "One mini registered");
  let mini = [...MiniWindowManager._miniwindows][0];
  await TestUtils.waitForCondition(
    () => mini.state == "active",
    "Waiting for the mini window to activate"
  );
  let created = Glean.miniWindow.created.testGetValue();
  Assert.equal(created.length, 1, "One created event was recorded");
  Assert.equal(created[0].extra.type, "url", "Reports the url flavour");
  Assert.equal(
    created[0].extra.entry_point,
    "link_context_menu",
    "Reports the link context menu entry point"
  );
  Assert.equal(
    miniWin.gBrowser.selectedBrowser.contentPrincipal.origin,
    "https://example.org",
    "Link loaded in the mini window"
  );

  let restore = miniWin.document.getElementById("mini-window-restore-button");
  Assert.equal(
    restore.getAttribute("data-l10n-id"),
    "mini-window-link-restore-button",
    "Restore button uses the link label"
  );
  Assert.ok(BrowserTestUtils.isVisible(restore), "Restore button is visible");

  let miniContextMenu = miniWin.document.getElementById(
    "contentAreaContextMenu"
  );
  let miniShown = BrowserTestUtils.waitForEvent(miniContextMenu, "popupshown");
  await SpecialPowers.spawn(miniWin.gBrowser.selectedBrowser, [], () => {
    let link = content.document.createElement("a");
    link.id = "link";
    link.href = "https://example.com/";
    link.textContent = "link";
    content.document.body.prepend(link);
  });
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link",
    { type: "contextmenu" },
    miniWin.gBrowser.selectedBrowser
  );
  await miniShown;
  Assert.ok(
    miniWin.document.getElementById("context-openlinkminiwindow").hidden,
    "Open Link in Mini Window item is hidden inside a mini window"
  );
  let miniHidden = BrowserTestUtils.waitForEvent(
    miniContextMenu,
    "popuphidden"
  );
  miniContextMenu.hidePopup();
  await miniHidden;

  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  BrowserTestUtils.removeTab(tab);
});
