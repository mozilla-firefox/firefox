/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const EXAMPLE_URL = "https://example.com/";

add_task(async function test_popTab_opens_full_tab_mini_window() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, EXAMPLE_URL);

  let miniWin = await popWholeTabForTest(tab);
  let doc = miniWin.document;

  ok(
    doc.documentElement.hasAttribute("mini-window"),
    "full-tab mini window is a mini window"
  );
  ok(
    !doc.documentElement.hasAttribute("cropped-mini-window"),
    "full-tab mini window is not marked as cropped"
  );

  // Full-tab mini windows keep history navigation.
  let back = doc.getElementById("goBackKb");
  ok(back && !back.hasAttribute("disabled"), "back shortcut stays enabled");
  isnot(
    miniWin.getComputedStyle(doc.getElementById("back-button")).display,
    "none",
    "back button is not hidden"
  );

  // A full-tab mini window is not framed: no transform, no page-sized browser
  // box - it lays out and scrolls like a normal window.
  let mini = [...MiniWindowManager._miniwindows][0];
  is(mini.browser.style.transform, "", "full-tab browser is not transformed");
  is(
    mini.browser.style.width,
    "",
    "full-tab browser is not laid out at a fixed page size"
  );

  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_cropped_mini_window_drops_history_navigation() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, EXAMPLE_URL);
  let browser = tab.linkedBrowser;
  let cropInfo = {
    left: 10,
    top: 10,
    width: Math.floor(browser.clientWidth / 2),
    height: Math.floor(browser.clientHeight / 2),
    viewportWidth: browser.clientWidth,
    viewportHeight: browser.clientHeight,
    fullZoom: 1,
  };

  let miniWin = await popTabForTest(tab, cropInfo);
  let doc = miniWin.document;

  ok(
    doc.documentElement.hasAttribute("cropped-mini-window"),
    "region pop is marked as a cropped mini window"
  );

  // A crop frames one page: history navigation is disabled and hidden.
  for (let id of ["goBackKb", "goBackKb2", "goForwardKb", "goForwardKb2"]) {
    let key = doc.getElementById(id);
    if (key) {
      is(key.getAttribute("disabled"), "true", `${id} is disabled`);
    }
  }
  is(
    miniWin.getComputedStyle(doc.getElementById("back-button")).display,
    "none",
    "back button is hidden in a cropped mini window"
  );
  is(
    miniWin.getComputedStyle(doc.getElementById("forward-button")).display,
    "none",
    "forward button is hidden in a cropped mini window"
  );

  // Mute stays available in all mini windows.
  let mute = doc.getElementById("key_toggleMute");
  ok(mute && !mute.hasAttribute("disabled"), "mute shortcut stays enabled");

  let mini = [...MiniWindowManager._miniwindows][0];
  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_move_tab_context_menu_entry() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, EXAMPLE_URL);
  let menu = document.getElementById("tabContextMenu");
  let item = document.getElementById("context_openTabInMiniWindow");

  async function openTabContextMenu() {
    let shown = BrowserTestUtils.waitForEvent(menu, "popupshown");
    EventUtils.synthesizeMouseAtCenter(tab, { type: "contextmenu" });
    await shown;
  }
  async function closeTabContextMenu() {
    let hidden = BrowserTestUtils.waitForEvent(menu, "popuphidden");
    menu.hidePopup();
    await hidden;
  }

  // Hidden while the feature is disabled.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.mini-window.enabled", false]],
  });
  await openTabContextMenu();
  ok(item.hidden, "menu item is hidden when the pref is off");
  await closeTabContextMenu();
  await SpecialPowers.popPrefEnv();

  // Shown and functional with the feature enabled (the manifest default).
  await openTabContextMenu();
  ok(!item.hidden, "menu item is shown when the pref is on");

  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  item.doCommand();
  await closeTabContextMenu();
  let miniWin = await opened;

  await TestUtils.waitForCondition(
    () => MiniWindowManager._miniwindows.size === 1,
    "mini window registered"
  );
  ok(
    miniWin.document.documentElement.hasAttribute("mini-window"),
    "menu item opened a mini window"
  );
  ok(
    !miniWin.document.documentElement.hasAttribute("cropped-mini-window"),
    "menu item opens the full-tab flavor"
  );

  let mini = [...MiniWindowManager._miniwindows][0];
  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});

/**
 * @returns {string[]} the URL each open mini window holds, sorted.
 */
function miniWindowURLs() {
  return [...MiniWindowManager._miniwindows]
    .map(mini => mini.browser.currentURI.spec)
    .sort();
}

add_task(async function test_popTabs_gives_each_tab_its_own_window() {
  let urls = [`${EXAMPLE_URL}?tab=1`, `${EXAMPLE_URL}?tab=2`];
  let tabs = [];
  for (let url of urls) {
    tabs.push(await BrowserTestUtils.openNewForegroundTab(gBrowser, url));
  }

  await MiniWindowManager.popTabs(tabs, "tab_context_menu");

  Assert.equal(
    MiniWindowManager._miniwindows.size,
    tabs.length,
    "one mini window per tab"
  );
  Assert.deepEqual(miniWindowURLs(), urls, "each tab is in a mini window");

  await closeAllMiniWindows();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_popTabs_separates_a_split_view() {
  Services.fog.testResetFOG();
  let urls = [`${EXAMPLE_URL}?split=1`, `${EXAMPLE_URL}?split=2`];
  let tabs = [];
  for (let url of urls) {
    tabs.push(await BrowserTestUtils.openNewForegroundTab(gBrowser, url));
  }
  gBrowser.addTabSplitView(tabs);

  // Moving one half of a split view moves both halves, each on its own.
  Assert.deepEqual(
    MiniWindowManager.tabsToPop([tabs[0]]).map(
      t => t.linkedBrowser.currentURI.spec
    ),
    urls,
    "the whole split view moves"
  );
  await MiniWindowManager.popTabs([tabs[0]], "tab_context_menu");

  Assert.deepEqual(miniWindowURLs(), urls, "one mini window per split tab");
  let ended = Glean.splitview.end.testGetValue() ?? [];
  ok(
    ended.some(event => event.extra.trigger === "mini_window"),
    "the split view ended with the mini_window trigger"
  );

  await closeAllMiniWindows();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_popRegion_breaks_a_split_view() {
  Services.fog.testResetFOG();
  let urls = [`${EXAMPLE_URL}?region=1`, `${EXAMPLE_URL}?region=2`];
  let tabs = [];
  for (let url of urls) {
    tabs.push(await BrowserTestUtils.openNewForegroundTab(gBrowser, url));
  }
  gBrowser.addTabSplitView(tabs);

  await popTabForTest(tabs[0]);

  Assert.deepEqual(
    miniWindowURLs(),
    [urls[0]],
    "only the cropped tab moves to a mini window"
  );
  ok(!tabs[1].splitview, "the other half is no longer in a split view");
  is(tabs[1].ownerGlobal, window, "the other half stays in the window");
  let ended = Glean.splitview.end.testGetValue() ?? [];
  ok(
    ended.some(event => event.extra.trigger === "mini_window"),
    "the split view ended with the mini_window trigger"
  );

  await closeAllMiniWindows();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_menu_label_counts_tabs() {
  let tabs = [];
  for (let i = 0; i < 2; i++) {
    tabs.push(
      await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        `${EXAMPLE_URL}?label=${i}`
      )
    );
  }
  gBrowser.addToMultiSelectedTabs(tabs[1]);

  let menu = document.getElementById("tabContextMenu");
  let shown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  EventUtils.synthesizeMouseAtCenter(tabs[0], { type: "contextmenu" });
  await shown;

  let item = document.getElementById("context_openTabInMiniWindow");
  Assert.deepEqual(
    document.l10n.getAttributes(item).args,
    { tabCount: 2 },
    "the label counts both selected tabs"
  );
  await TestUtils.waitForCondition(
    () => item.label === "Move to Mini Windows",
    "the label is plural"
  );

  let hidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.hidePopup();
  await hidden;
  gBrowser.clearMultiSelectedTabs();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_popTab_moves_a_discarded_tab() {
  let selected = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `${EXAMPLE_URL}?selected`
  );
  let url = `${EXAMPLE_URL}?discarded`;
  let tab = BrowserTestUtils.addTab(gBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await gBrowser.prepareDiscardBrowser(tab);
  gBrowser.discardBrowser(tab, { forceDiscard: true });
  ok(tab.hasAttribute("pending"), "the tab is pending");
  let tabCount = gBrowser.tabs.length;

  let miniWin = await popWholeTabForTest(tab);

  let mini = [...MiniWindowManager._miniwindows][0];
  await TestUtils.waitForCondition(
    () => mini.browser.currentURI.spec === url,
    "the mini window shows the moved tab's page"
  );
  is(gBrowser.tabs.length, tabCount - 1, "the tab left the origin window");
  ok(!tab.isConnected, "nothing of the tab is left behind");
  is(gBrowser.selectedTab, selected, "the selected tab is unchanged");

  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_mini_window_is_not_saved_by_session_store() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `${EXAMPLE_URL}?ephemeral`
  );
  let windowCount = JSON.parse(SessionStore.getBrowserState()).windows.length;
  let closedWindowCount = SessionStore.getClosedWindowCount();

  let miniWin = await popWholeTabForTest(tab);
  Assert.ok(
    SessionStore.getWindowId(miniWin),
    "session store tracks the mini window"
  );
  is(
    JSON.parse(SessionStore.getBrowserState()).windows.length,
    windowCount,
    "the mini window isn't part of the saved state"
  );

  [...MiniWindowManager._miniwindows][0].close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  is(
    SessionStore.getClosedWindowCount(),
    closedWindowCount,
    "closing the mini window doesn't add a closed window"
  );
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});
