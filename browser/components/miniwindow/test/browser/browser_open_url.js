/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(async function test_open_url_in_mini_window() {
  let tabCountBefore = gBrowser.tabs.length;

  let mini = await MiniWindowManager.openURL(
    window,
    "https://example.com/",
    "test",
    { triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal() }
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

add_task(async function test_open_url_without_principal_throws() {
  for (let loadParams of [undefined, {}, { triggeringPrincipal: null }]) {
    await Assert.rejects(
      MiniWindowManager.openURL(
        window,
        "https://example.com/",
        "test",
        loadParams
      ),
      /must include a triggeringPrincipal/,
      `openURL throws without a triggeringPrincipal: ${JSON.stringify(loadParams)}`
    );
  }
  assertNoMiniWindowsOpen();
});

/**
 * Adds a link to `href` in `browser`'s page and returns once its context menu
 * is open.
 *
 * @param {Window} win - the window `browser` lives in.
 * @param {MozBrowser} browser
 * @param {string} href
 * @returns {Promise<XULPopupElement>} the open context menu.
 */
async function openLinkContextMenu(win, browser, href) {
  await SpecialPowers.spawn(browser, [href], linkHref => {
    let link = content.document.createElement("a");
    link.id = "link";
    link.href = linkHref;
    link.textContent = "link";
    content.document.body.prepend(link);
  });
  let contextMenu = win.document.getElementById("contentAreaContextMenu");
  let shown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link",
    { type: "contextmenu" },
    browser
  );
  await shown;
  return contextMenu;
}

/**
 * Opens `href` from a link in `tab` through "Open Link in Mini Window".
 *
 * @param {MozTabbrowserTab} tab
 * @param {string} href
 * @returns {Promise<object>} the active MiniWindow.
 */
async function openLinkInMiniWindowForTest(tab, href) {
  let contextMenu = await openLinkContextMenu(window, tab.linkedBrowser, href);
  let item = document.getElementById("context-openlinkminiwindow");
  Assert.ok(!item.hidden, "Open Link in Mini Window item is shown");

  let newWin = BrowserTestUtils.waitForNewWindow({ url: href });
  contextMenu.activateItem(item);
  await newWin;

  Assert.equal(MiniWindowManager._miniwindows.size, 1, "One mini registered");
  let mini = [...MiniWindowManager._miniwindows][0];
  await TestUtils.waitForCondition(
    () => mini.state == "active",
    "Waiting for the mini window to activate"
  );
  return mini;
}

add_task(async function test_open_link_in_mini_window_from_context_menu() {
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  let mini = await openLinkInMiniWindowForTest(tab, "https://example.org/");
  let { miniWin } = mini;

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

  let referrer = await SpecialPowers.spawn(
    miniWin.gBrowser.selectedBrowser,
    [],
    () => content.document.referrer
  );
  Assert.equal(referrer, "https://example.com/", "Link load sent a referrer");

  let restore = miniWin.document.getElementById("mini-window-restore-button");
  Assert.equal(
    restore.getAttribute("data-l10n-id"),
    "mini-window-link-restore-button",
    "Restore button uses the link label"
  );
  Assert.ok(BrowserTestUtils.isVisible(restore), "Restore button is visible");

  let miniContextMenu = await openLinkContextMenu(
    miniWin,
    miniWin.gBrowser.selectedBrowser,
    "https://example.net/"
  );
  let miniItem = miniWin.document.getElementById("context-openlinkminiwindow");
  Assert.ok(
    !miniItem.hidden,
    "Open Link in Mini Window item is shown inside a mini window"
  );
  let nestedWinPromise = BrowserTestUtils.waitForNewWindow({
    url: "https://example.net/",
  });
  miniContextMenu.activateItem(miniItem);
  let nestedWin = await nestedWinPromise;
  let nested = [...MiniWindowManager._miniwindows].find(
    m => m.miniWin === nestedWin
  );
  await TestUtils.waitForCondition(
    () => nested.state == "active",
    "Waiting for the nested mini window to activate"
  );
  Assert.equal(
    nested.originWin,
    window,
    "A link opened from a mini window belongs to the browser window"
  );

  let tabCount = gBrowser.tabs.length;
  nested.returnToOriginWin(true);
  await BrowserTestUtils.domWindowClosed(nestedWin);
  Assert.equal(
    gBrowser.tabs.length,
    tabCount + 1,
    "Nested tab went back to the browser window"
  );
  Assert.equal(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://example.net/",
    "Returned tab is the nested link"
  );
  Assert.equal(mini.state, "active", "First mini window stays open");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_open_link_in_mini_window_keeps_container() {
  let tab = BrowserTestUtils.addTab(gBrowser, "https://example.com/", {
    userContextId: 1,
  });
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  let mini = await openLinkInMiniWindowForTest(tab, "https://example.org/");
  let { miniWin } = mini;
  Assert.equal(
    miniWin.gBrowser.selectedBrowser.browsingContext.originAttributes
      .userContextId,
    1,
    "Link loaded in the source tab's container"
  );
  let referrer = await SpecialPowers.spawn(
    miniWin.gBrowser.selectedBrowser,
    [],
    () => content.document.referrer
  );
  Assert.equal(
    referrer,
    "https://example.com/",
    "Same-container link load keeps the referrer"
  );

  mini.returnToOriginWin(true);
  await BrowserTestUtils.domWindowClosed(miniWin);
  Assert.equal(
    gBrowser.selectedTab.userContextId,
    1,
    "Returned tab stays in the container"
  );
  Assert.equal(
    gBrowser.selectedBrowser.currentURI.spec,
    "https://example.org/",
    "Returned tab is the link tab"
  );

  assertNoMiniWindowsOpen();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  BrowserTestUtils.removeTab(tab);
});
