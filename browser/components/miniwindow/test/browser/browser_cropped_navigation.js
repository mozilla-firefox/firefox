/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A cropped mini window frames one specific page, so any real navigation
// returns the tab to its origin window, where the load continues. Full-tab
// mini windows keep normal in-window navigation, and same-document
// housekeeping (replaceState) never ejects.

const EXAMPLE_URL = "https://example.com/";
const NEXT_URL = REDIRECT_URL + "?abracadabra=1";

function croppedInfoFor(browser) {
  return {
    left: 10,
    top: 10,
    width: Math.floor(browser.clientWidth / 2),
    height: Math.floor(browser.clientHeight / 2),
    viewportWidth: browser.clientWidth,
    viewportHeight: browser.clientHeight,
    fullZoom: 1,
  };
}

add_task(async function test_cropped_navigation_returns_tab_home() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, REDIRECT_URL);
  let miniWin = await popTabForTest(tab, croppedInfoFor(tab.linkedBrowser));
  let mini = [...MiniWindowManager._miniwindows][0];

  // Make sure the window is focussed
  await SimpleTest.promiseFocus(miniWin);

  // A user-initiated navigation away from the cropped page: click the
  // fixture button, which sets location.href.
  let closed = BrowserTestUtils.domWindowClosed(miniWin);
  await BrowserTestUtils.synthesizeMouseAtCenter("#redirect", {}, mini.browser);
  await closed;

  assertNoMiniWindowsOpen();
  await TestUtils.waitForCondition(
    () => gBrowser.selectedBrowser.currentURI.spec === NEXT_URL,
    "the navigation continued in the origin window"
  );
  Assert.ok(true, "Successfully navigated");

  removeTestTabs(EXAMPLE_URL);
});

/**
 * Pop a cropped mini window, then have its page navigate on its own (no user
 * gesture) and resolve to the tab once it's back home.
 *
 * @param {object} options
 * @param {boolean} options.miniWindowActive - whether the user is using the
 *   mini window when the page navigates.
 */
async function navigateFromPage({ miniWindowActive }) {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, REDIRECT_URL);
  let miniWin = await popTabForTest(tab, croppedInfoFor(tab.linkedBrowser));
  let mini = [...MiniWindowManager._miniwindows][0];
  await SimpleTest.promiseFocus(miniWindowActive ? miniWin : window);

  let closed = BrowserTestUtils.domWindowClosed(miniWin);
  await SpecialPowers.spawn(mini.browser, [NEXT_URL], url => {
    content.location.href = url;
  });
  await closed;

  assertNoMiniWindowsOpen();
  return TestUtils.waitForCondition(
    () => gBrowser.tabs.find(t => t.linkedBrowser.currentURI.spec === NEXT_URL),
    "the tab came back and the navigation continued"
  );
}

add_task(async function test_page_navigation_returns_tab_quietly() {
  let returned = await navigateFromPage({ miniWindowActive: false });
  Assert.notEqual(gBrowser.selectedTab, returned, "the tab isn't selected");
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_page_navigation_in_active_mini_window_focuses() {
  let returned = await navigateFromPage({ miniWindowActive: true });
  Assert.equal(gBrowser.selectedTab, returned, "the tab is selected");
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_replace_state_does_not_eject() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, EXAMPLE_URL);
  let miniWin = await popTabForTest(tab, croppedInfoFor(tab.linkedBrowser));
  let mini = [...MiniWindowManager._miniwindows][0];

  // On-load URL housekeeping must not count as leaving the page.
  await SpecialPowers.spawn(mini.browser, [], () => {
    content.history.replaceState(null, "", "?pos=42");
  });
  // The eject is deferred to the main thread; give it a chance to (not) run.
  await TestUtils.waitForTick();
  await TestUtils.waitForTick();

  ok(!miniWin.closed, "mini window survived a replaceState");
  is(MiniWindowManager._miniwindows.size, 1, "mini window is still registered");

  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_full_tab_navigation_stays() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, REDIRECT_URL);
  let miniWin = await popWholeTabForTest(tab);
  let mini = [...MiniWindowManager._miniwindows][0];

  let loaded = BrowserTestUtils.browserLoaded(mini.browser, false, NEXT_URL);
  await BrowserTestUtils.synthesizeMouseAtCenter("#redirect", {}, mini.browser);
  await loaded;

  ok(!miniWin.closed, "full-tab mini window survived the navigation");
  is(MiniWindowManager._miniwindows.size, 1, "mini window is still registered");
  is(
    mini.browser.currentURI.spec,
    NEXT_URL,
    "the navigation happened inside the mini window"
  );

  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs(EXAMPLE_URL);
});

add_task(async function test_back_to_cropped_page_is_unframed() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, REDIRECT_URL);
  let browser = tab.linkedBrowser;
  let homeWidth = await SpecialPowers.spawn(
    browser,
    [],
    () => content.innerWidth
  );

  // Hold the layout wider than the origin tab so the override is detectable.
  let miniWin = await popTabForTest(tab, {
    ...croppedInfoFor(browser),
    viewportWidth: homeWidth * 2,
  });
  let mini = [...MiniWindowManager._miniwindows][0];
  await SimpleTest.promiseFocus(miniWin);

  let resized = BrowserTestUtils.waitForEvent(miniWin, "resize");
  miniWin.resizeTo(miniWin.outerWidth + 100, miniWin.outerHeight + 50);
  await resized;

  await SpecialPowers.spawn(mini.browser, [homeWidth * 2], async width => {
    await ContentTaskUtils.waitForCondition(
      () => content.innerWidth === width,
      "the mini window holds the page at the crop's layout width"
    );
    Assert.notEqual(
      content.windowUtils.getResolution(),
      1,
      "the page is zoomed onto the crop"
    );
  });

  let closed = BrowserTestUtils.domWindowClosed(miniWin);
  await BrowserTestUtils.synthesizeMouseAtCenter("#redirect", {}, mini.browser);
  await closed;
  assertNoMiniWindowsOpen();

  let returned = await TestUtils.waitForCondition(
    () => gBrowser.tabs.find(t => t.linkedBrowser.currentURI.spec === NEXT_URL),
    "the tab came back and the navigation continued"
  );
  browser = returned.linkedBrowser;
  await SpecialPowers.spawn(browser, [], () =>
    ContentTaskUtils.waitForCondition(
      () => content.document.readyState === "complete",
      "the next page finished loading"
    )
  );

  let shown = BrowserTestUtils.waitForContentEvent(browser, "pageshow");
  browser.goBack();
  await shown;

  await SpecialPowers.spawn(browser, [homeWidth], width => {
    Assert.equal(
      content.windowUtils.getResolution(),
      1,
      "the page isn't zoomed after going back"
    );
    Assert.equal(
      content.innerWidth,
      width,
      "the page lays out at the origin tab's width after going back"
    );
  });

  removeTestTabs(EXAMPLE_URL);
});
