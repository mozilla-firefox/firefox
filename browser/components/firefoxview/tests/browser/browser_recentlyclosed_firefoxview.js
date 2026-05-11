/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

requestLongerTimeout(3);

ChromeUtils.defineESModuleGetters(globalThis, {
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
});

const NEVER_REMEMBER_HISTORY_PREF = "browser.privatebrowsing.autostart";
const RECENTLY_CLOSED_EVENT = [
  ["firefoxview_next", "recently_closed", "tabs", undefined],
];
const DISMISS_CLOSED_TAB_EVENT = [
  ["firefoxview_next", "dismiss_closed_tab", "tabs", undefined],
];
const initialTab = gBrowser.selectedTab;

async function restore_tab(itemElem, browser, expectedURL) {
  info(`Restoring tab ${itemElem.url}`);
  let tabRestored = BrowserTestUtils.waitForNewTab(
    browser.getTabBrowser(),
    expectedURL
  );
  await click_recently_closed_tab_item(itemElem, "main");
  await tabRestored;
}

async function dismiss_tab(itemElem) {
  info(`Dismissing tab ${itemElem.url}`);
  return click_recently_closed_tab_item(itemElem, "dismiss");
}

async function tabTestCleanup() {
  await promiseAllButPrimaryWindowClosed();
  for (let tab of gBrowser.visibleTabs) {
    if (tab == initialTab) {
      continue;
    }
    await TabStateFlusher.flush(tab.linkedBrowser);
    let sessionUpdatePromise = BrowserTestUtils.waitForSessionStoreUpdate(tab);
    BrowserTestUtils.removeTab(tab);
    await sessionUpdatePromise;
  }
  Services.obs.notifyObservers(null, "browser:purge-session-history");
}

async function prepareSingleClosedTab() {
  Services.obs.notifyObservers(null, "browser:purge-session-history");
  is(
    SessionStore.getClosedTabCount(window),
    0,
    "Closed tab count after purging session history"
  );
  await open_then_close(URLs[0]);
  return {
    cleanup: tabTestCleanup,
  };
}

async function prepareClosedTabs() {
  Services.obs.notifyObservers(null, "browser:purge-session-history");
  is(
    SessionStore.getClosedTabCount(window),
    0,
    "Closed tab count after purging session history"
  );
  is(
    SessionStore.getClosedTabCountFromClosedWindows(),
    0,
    "Closed tab count after purging session history"
  );

  await open_then_close(URLs[0]);
  await open_then_close(URLs[1]);

  // create 1 recently-closed tabs in a 2nd window
  info("Opening win2 and open/closing tabs in it");
  const win2 = await BrowserTestUtils.openNewBrowserWindow();
  // open a non-transitory, worth-keeping tab to ensure window data is saved on close
  await BrowserTestUtils.openNewForegroundTab(win2.gBrowser, "about:mozilla");
  await open_then_close(URLs[2], win2);

  info("Opening win3 and open/closing a tab in it");
  const win3 = await BrowserTestUtils.openNewBrowserWindow();
  // open a non-transitory, worth-keeping tab to ensure window data is saved on close
  await BrowserTestUtils.openNewForegroundTab(win3.gBrowser, "about:mozilla");
  await open_then_close(URLs[3], win3);

  // close the 3rd window with its 1 recently-closed tab
  info("closing win3 and waiting for sessionstore-closed-objects-changed");
  await BrowserTestUtils.closeWindow(win3);

  // refocus and bring the initial window to the foreground
  await SimpleTest.promiseFocus(window);

  // this is the order we expect for all the recently-closed tabs
  const expectedURLs = [
    "https://example.org/", // URLS[3]
    "https://example.net/", // URLS[2]
    "https://www.example.com/", // URLS[1]
    "http://mochi.test:8888/browser/", // URLS[0]
  ];
  const preparedClosedTabCount = expectedURLs.length;

  const closedTabsFromClosedWindowsCount =
    SessionStore.getClosedTabCountFromClosedWindows();
  is(
    closedTabsFromClosedWindowsCount,
    1,
    "Expected 1 closed tab from a closed window"
  );

  const closedTabsFromOpenWindowsCount = SessionStore.getClosedTabCount({
    sourceWindow: window,
    closedTabsFromClosedWindows: false,
  });
  const actualClosedTabCount = SessionStore.getClosedTabCount();
  is(
    closedTabsFromOpenWindowsCount,
    3,
    "Expected 3 closed tabs currently-open windows"
  );

  is(
    actualClosedTabCount,
    preparedClosedTabCount,
    `SessionStore reported the expected number (${actualClosedTabCount}) of closed tabs`
  );

  return {
    cleanup: tabTestCleanup,
    // return a list of the tab urls we closed in the order we closed them
    closedTabURLs: [...URLs.slice(0, 4)],
    expectedURLs,
  };
}

add_setup(async () => {
  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    clearHistory();
  });
});

/**
 * Asserts that we get the expected initial recently-closed tab list item
 */
add_task(async function test_initial_closed_tab() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    is(document.location.href, getFirefoxViewURL());
    await navigateToViewAndWait(document, "recentlyclosed");
    let { cleanup } = await prepareSingleClosedTab();
    await switchToFxViewTab(window);
    let [listItems] = await waitForRecentlyClosedTabsList(document);

    Assert.strictEqual(
      listItems.rowEls.length,
      1,
      "Initial list item is rendered."
    );

    await cleanup();
  });
});

/**
 * Asserts that we get the expected order recently-closed tab list items given a known
 * sequence of tab closures
 */
add_task(async function test_list_ordering() {
  let { cleanup, expectedURLs } = await prepareClosedTabs();
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");
    let [cardMainSlotNode, listItems] =
      await waitForRecentlyClosedTabsList(document);

    is(
      cardMainSlotNode.tagName.toLowerCase(),
      "fxview-tab-list",
      "The tab list component is rendered."
    );

    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "The initial list has rendered the expected tab items in the right order"
    );
  });
  await cleanup();
});

/**
 * Asserts that an out-of-band update to recently-closed tabs results in the correct update to the tab list
 */
add_task(async function test_list_updates() {
  let { cleanup, expectedURLs } = await prepareClosedTabs();

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");

    let [listElem, listItems] = await waitForRecentlyClosedTabsList(document);
    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "The initial list has rendered the expected tab items in the right order"
    );

    // the first tab we opened and closed is the last in the list
    let closedTabItem = listItems[listItems.length - 1];
    is(
      closedTabItem.url,
      "http://mochi.test:8888/browser/",
      "Sanity-check the least-recently closed tab is https://example.org/"
    );
    info(
      `Restore the last (least-recently) closed tab ${closedTabItem.url}, closedId: ${closedTabItem.closedId} and wait for sessionstore-closed-objects-changed`
    );
    let promiseClosedObjectsChanged = TestUtils.topicObserved(
      "sessionstore-closed-objects-changed"
    );
    SessionStore.undoCloseById(closedTabItem.closedId);
    await promiseClosedObjectsChanged;
    await clickFirefoxViewButton(window);

    // we expect the last item to be removed
    expectedURLs.pop();
    listItems = listElem.rowEls;

    is(
      listItems.length,
      3,
      `Three tabs are shown in the list: ${Array.from(listItems).map(
        el => el.url
      )}, of ${expectedURLs.length} expectedURLs: ${expectedURLs}`
    );
    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "The updated list has rendered the expected tab items in the right order"
    );

    // forget the window the most-recently closed tab was in and verify the list is correctly updated
    closedTabItem = listItems[0];
    promiseClosedObjectsChanged = TestUtils.topicObserved(
      "sessionstore-closed-objects-changed"
    );
    SessionStore.forgetClosedWindowById(closedTabItem.sourceClosedId);
    await promiseClosedObjectsChanged;
    await clickFirefoxViewButton(window);

    listItems = listElem.rowEls;
    expectedURLs.shift(); // we expect to have removed the firsts URL from the list
    is(listItems.length, 2, "Two tabs are shown in the list.");
    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "After forgetting the closed window that owned the last recent tab, we have expected tab items in the right order"
    );
  });
  await cleanup();
});

/**
 * Asserts that tabs that have been recently closed can be
 * restored by clicking on the list item
 */
add_task(async function test_restore_tab() {
  Services.fog.testResetFOG();
  let { cleanup, expectedURLs } = await prepareClosedTabs();

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");

    let [listElem, listItems] = await waitForRecentlyClosedTabsList(document);
    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "The initial list has rendered the expected tab items in the right order"
    );
    let closeTabItem = listItems[0];
    info(
      `Restoring the first closed tab ${closeTabItem.url}, closedId: ${closeTabItem.closedId}, sourceClosedId: ${closeTabItem.sourceClosedId}  and waiting for sessionstore-closed-objects-changed`
    );
    await restore_tab(closeTabItem, browser, closeTabItem.url);
    Assert.equal(
      1,
      Glean.firefoxviewNext.recentlyClosedTabs.testGetValue().length,
      "Expect one recently-closed event."
    );
    await clickFirefoxViewButton(window);

    listItems = listElem.rowEls;
    is(listItems.length, 3, "Three tabs are shown in the list.");

    closeTabItem = listItems[listItems.length - 1];
    Services.fog.testResetFOG();
    await restore_tab(closeTabItem, browser, closeTabItem.url);
    Assert.equal(
      1,
      Glean.firefoxviewNext.recentlyClosedTabs.testGetValue().length,
      "Expect one recently-closed event."
    );
    await clickFirefoxViewButton(window);

    listItems = listElem.rowEls;
    is(listItems.length, 2, "Two tabs are shown in the list.");

    listItems = listElem.rowEls;
    is(listItems.length, 2, "Two tabs are shown in the list.");
  });
  await cleanup();
});

add_task(async function test_restore_tab_from_deleted_group() {
  Services.fog.testResetFOG();
  let tab = await add_new_tab("about:mozilla");
  let group = gBrowser.addTabGroup([tab]);
  Assert.equal(gBrowser.visibleTabs.length, 2, "2 tabs are open");
  Assert.ok(tab.group, "New tab is grouped");
  await gBrowser.removeTabGroup(group);
  Assert.equal(
    gBrowser.visibleTabs.length,
    1,
    "1 tab is open after group deletion"
  );

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");

    let [, listItems] = await waitForRecentlyClosedTabsList(document);

    let closedTabItem = listItems[0];
    info("Restoring the closed tab");
    Services.fog.testResetFOG();
    await restore_tab(closedTabItem, browser, closedTabItem.url);
    Assert.equal(
      1,
      Glean.firefoxviewNext.recentlyClosedTabs.testGetValue().length,
      "Expect one recently-closed event."
    );
    Assert.equal(gBrowser.visibleTabs.length, 2, "Tab was restored");
  });
  BrowserTestUtils.removeTab(gBrowser.tabs[1]);
});

/**
 * Asserts that tabs that have been recently closed can be
 * dismissed by clicking on their respective dismiss buttons.
 */
add_task(async function test_dismiss_tab() {
  Services.fog.testResetFOG();
  let { cleanup, expectedURLs } = await prepareClosedTabs();

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");

    let [listElem, listItems] = await waitForRecentlyClosedTabsList(document);
    Services.fog.testResetFOG();

    info("calling dismiss_tab on the top, most-recently closed tab");
    let closedTabItem = listItems[0];
    // the most recently closed tab was in window 3 which got closed
    // so we expect a sourceClosedId on the item element
    ok(
      !isNaN(closedTabItem.sourceClosedId),
      "Item has a sourceClosedId property"
    );

    // dismiss the first tab and verify the list is correctly updated
    await dismiss_tab(closedTabItem);
    await listElem.getUpdateComplete;

    info("check telemetry results");
    Assert.equal(
      1,
      Glean.firefoxviewNext.dismissClosedTabTabs.testGetValue().length,
      "Expect one dismiss event."
    );

    listItems = listElem.rowEls;
    expectedURLs.shift(); // we expect to have removed the first URL from the list
    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "After dismissing the most-recent tab we have expected tab items in the right order"
    );

    // dismiss the last tab and verify the list is correctly updated
    closedTabItem = listItems[listItems.length - 1];
    ok(
      isNaN(closedTabItem.sourceClosedId),
      "Item does not have a sourceClosedId property"
    );
    ok(closedTabItem.sourceWindowId, "Item has a sourceWindowId property");

    await dismiss_tab(closedTabItem);
    await listElem.getUpdateComplete;

    listItems = listElem.rowEls;
    expectedURLs.pop(); // we expect to have removed the last URL from the list
    let actualClosedTabCount =
      SessionStore.getClosedTabCount(window) +
      SessionStore.getClosedTabCountFromClosedWindows();
    Assert.equal(
      actualClosedTabCount,
      2,
      "After dismissing the least-recent tab, SessionStore has 2 left"
    );
    Assert.deepEqual(
      Array.from(listItems).map(el => el.url),
      expectedURLs,
      "After dismissing the least-recent tab we have expected tab items in the right order"
    );
  });
  await cleanup();
});

add_task(async function test_empty_states() {
  Services.obs.notifyObservers(null, "browser:purge-session-history");
  is(
    SessionStore.getClosedTabCount(window),
    0,
    "Closed tab count after purging session history"
  );
  is(
    SessionStore.getClosedTabCountFromClosedWindows(),
    0,
    "Closed tabs-from-closed-windows count after purging session history"
  );

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    is(document.location.href, "about:firefoxview");

    await navigateToViewAndWait(document, "recentlyclosed");
    let recentlyClosedComponent = document.querySelector(
      "view-recentlyclosed:not([slot=recentlyclosed])"
    );

    await TestUtils.waitForCondition(
      () => recentlyClosedComponent.emptyState,
      "Waiting for the recently closed component to be in the empty state"
    );
    let emptyStateCard = recentlyClosedComponent.emptyState;
    ok(
      emptyStateCard.headerEl.textContent.includes("Closed a tab too soon"),
      "Initial empty state header has the expected text."
    );
    ok(
      emptyStateCard.descriptionEls[0].textContent.includes(
        "Here you’ll find the tabs you recently closed"
      ),
      "Initial empty state description has the expected text."
    );

    // Test empty state when History mode is set to never remember
    Services.prefs.setBoolPref(NEVER_REMEMBER_HISTORY_PREF, true);
    // Manually update the recentlyclosed component from the test, since changing this setting
    // in about:preferences will require a browser reload
    recentlyClosedComponent.requestUpdate();
    await TestUtils.waitForCondition(
      () => recentlyClosedComponent.fullyUpdated,
      "The recently closed component to be fully updated"
    );
    emptyStateCard = recentlyClosedComponent.emptyState;
    ok(
      emptyStateCard.headerEl.textContent.includes("You’re in control"),
      "Empty state with never remember history header has the expected text."
    );
    ok(
      emptyStateCard.descriptionEls[0].textContent.includes(
        "does not remember your browsing activity"
      ),
      "Empty state with never remember history description has the expected text."
    );
    // Reset History mode to Remember
    Services.prefs.setBoolPref(NEVER_REMEMBER_HISTORY_PREF, false);
    gBrowser.removeTab(gBrowser.selectedTab);
  });
});

add_task(async function test_observers_removed_when_view_is_hidden() {
  clearHistory();

  await open_then_close(URLs[0]);

  await withFirefoxView({}, async function (browser) {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");
    const [listElem] = await waitForRecentlyClosedTabsList(document);
    is(listElem.rowEls.length, 1);

    const gBrowser = browser.getTabBrowser();
    const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, URLs[1]);
    await open_then_close(URLs[2]);
    await open_then_close(URLs[3]);
    await open_then_close(URLs[4]);
    is(
      listElem.rowEls.length,
      1,
      "The list does not update when Firefox View is hidden."
    );

    await switchToFxViewTab(browser.documentGlobal);
    info("The list should update when Firefox View is visible.");
    await BrowserTestUtils.waitForMutationCondition(
      listElem,
      { childList: true },
      () => listElem.rowEls.length === 4
    );

    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function test_search() {
  let { cleanup, expectedURLs } = await prepareClosedTabs();
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentlyclosed");
    const [listElem] = await waitForRecentlyClosedTabsList(document);
    const recentlyClosedComponent = document.querySelector(
      "view-recentlyclosed:not([slot=recentlyclosed])"
    );
    const { searchTextbox, tabList } = recentlyClosedComponent;

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(
      () => listElem.rowEls.length === 1,
      "There is one matching search result."
    );

    info("Clear the search query.");
    let clearButton = SpecialPowers.getInputButton(searchTextbox.inputEl);
    EventUtils.synthesizeMouseAtCenter(clearButton, {}, content);
    await TestUtils.waitForCondition(
      () => listElem.rowEls.length === expectedURLs.length,
      "The original list is restored."
    );
    searchTextbox.blur();

    info("Input a bogus search query with keyboard.");
    EventUtils.synthesizeKey("f", { accelKey: true }, content);
    EventUtils.sendString("Bogus Query", content);
    await TestUtils.waitForCondition(
      () => tabList.shadowRoot.querySelector("fxview-empty-state"),
      "There are no matching search results."
    );

    info("Clear the search query.");
    clearButton = SpecialPowers.getInputButton(searchTextbox.inputEl);
    EventUtils.synthesizeMouseAtCenter(clearButton, {}, content);
    await TestUtils.waitForCondition(
      () => listElem.rowEls.length === expectedURLs.length,
      "The original list is restored."
    );
  });
  await cleanup();
});

add_task(async function test_search_recent_browsing() {
  const NUMBER_OF_TABS = 6;
  clearHistory();
  for (let i = 0; i < NUMBER_OF_TABS; i++) {
    await open_then_close(URLs[1]);
  }
  await withFirefoxView({}, async function (browser) {
    const { document } = browser.contentWindow;

    info("Input a search query.");
    await navigateToViewAndWait(document, "recentbrowsing");
    const recentBrowsing = document.querySelector("view-recentbrowsing");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("example.com", content);
    const slot = recentBrowsing.querySelector("[slot='recentlyclosed']");
    await TestUtils.waitForCondition(
      () =>
        slot.tabList.rowEls.length === 5 &&
        slot.shadowRoot.querySelector("[data-l10n-id='firefoxview-show-all']"),
      "Not all search results are shown yet."
    );

    info("Click the Show All link.");
    const showAllLink = slot.shadowRoot.querySelector(
      "[data-l10n-id='firefoxview-show-all']"
    );
    is(showAllLink.role, "link", "The show all control is a link.");
    EventUtils.synthesizeMouseAtCenter(showAllLink, {}, content);
    await TestUtils.waitForCondition(
      () => slot.tabList.rowEls.length === NUMBER_OF_TABS,
      "All search results are shown."
    );
    ok(BrowserTestUtils.isHidden(showAllLink), "The show all link is hidden.");
  });
});
