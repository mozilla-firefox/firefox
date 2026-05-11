let gInitialTab;
let gInitialTabURL;

const NUMBER_OF_TABS = 6;

const syncedTabsData = [
  {
    id: 1,
    name: "This Device",
    isCurrentDevice: true,
    type: "desktop",
    tabs: Array(NUMBER_OF_TABS)
      .fill({
        type: "tab",
        title: "Internet for people, not profits - Mozilla",
        icon: "https://www.mozilla.org/media/img/favicons/mozilla/favicon.d25d81d39065.ico",
        client: 1,
      })
      .map((tab, i) => ({ ...tab, url: URLs[i] })),
  },
];

const cleanUp = () => {
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
  }
};

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });

  gInitialTab = gBrowser.selectedTab;
  gInitialTabURL = gBrowser.selectedBrowser.currentURI.spec;
  registerCleanupFunction(async () => {
    clearHistory();
  });
});

add_task(async function test_search_initiated_telemetry() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    Services.fog.testResetFOG();

    is(document.location.hash, "", "Searching within recent browsing.");
    const recentBrowsing = document.querySelector("view-recentbrowsing");
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(
      () => Glean.firefoxviewNext.searchInitiatedSearch.testGetValue(),
      "Awaiting search to be reported."
    );
    let searchEvents =
      Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "recentbrowsing" }, searchEvents[0].extra);

    await navigateToViewAndWait(document, "opentabs");
    Services.fog.testResetFOG();
    is(document.location.hash, "#opentabs", "Searching within open tabs.");
    const openTabs = document.querySelector("named-deck > view-opentabs");
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(openTabs.searchTextbox, {}, content);
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(
      () => Glean.firefoxviewNext.searchInitiatedSearch.testGetValue(),
      "Awaiting search to be reported."
    );
    searchEvents = Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "opentabs" }, searchEvents[0].extra);

    await navigateToViewAndWait(document, "recentlyclosed");
    Services.fog.testResetFOG();
    is(
      document.location.hash,
      "#recentlyclosed",
      "Searching within recently closed."
    );
    const recentlyClosed = document.querySelector(
      "named-deck > view-recentlyclosed"
    );
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(
      recentlyClosed.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(
      () => Glean.firefoxviewNext.searchInitiatedSearch.testGetValue(),
      "Awaiting search to be reported."
    );
    searchEvents = Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "recentlyclosed" }, searchEvents[0].extra);

    await navigateToViewAndWait(document, "syncedtabs");
    Services.fog.testResetFOG();
    is(document.location.hash, "#syncedtabs", "Searching within synced tabs.");
    const syncedTabs = document.querySelector("named-deck > view-syncedtabs");
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(syncedTabs.searchTextbox, {}, content);
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(
      () => Glean.firefoxviewNext.searchInitiatedSearch.testGetValue(),
      "Awaiting search to be reported."
    );
    searchEvents = Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "syncedtabs" }, searchEvents[0].extra);

    await navigateToViewAndWait(document, "history");
    Services.fog.testResetFOG();
    is(document.location.hash, "#history", "Searching within history.");
    const history = document.querySelector("named-deck > view-history");
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(history.searchTextbox, {}, content);
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(
      () => Glean.firefoxviewNext.searchInitiatedSearch.testGetValue(),
      "Awaiting search to be reported."
    );
    searchEvents = Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "history" }, searchEvents[0].extra);
  });
});

add_task(async function test_show_all_recentlyclosed_telemetry() {
  for (let i = 0; i < NUMBER_OF_TABS; i++) {
    await open_then_close(URLs[1]);
  }
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    const recentBrowsing = document.querySelector("view-recentbrowsing");

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("example.com", content);
    const recentlyclosedSlot = recentBrowsing.querySelector(
      "[slot='recentlyclosed']"
    );
    await TestUtils.waitForCondition(
      () =>
        recentlyclosedSlot.tabList.rowEls.length === 5 &&
        recentlyclosedSlot.shadowRoot.querySelector(
          "[data-l10n-id='firefoxview-show-all']"
        ),
      "Expected search results are not shown yet."
    );
    Services.fog.testResetFOG();

    info("Click the Show All link.");
    const showAllButton = recentlyclosedSlot.shadowRoot.querySelector(
      "[data-l10n-id='firefoxview-show-all']"
    );
    await TestUtils.waitForCondition(
      () => !showAllButton.hidden,
      "Waiting for the show all button to be visible"
    );
    ok(!showAllButton.hidden, "Show all button is visible");
    await TestUtils.waitForCondition(() => {
      EventUtils.synthesizeMouseAtCenter(showAllButton, {}, content);
      if (recentlyclosedSlot.tabList.rowEls.length === NUMBER_OF_TABS) {
        return true;
      }
      return false;
    }, "All search results are not shown.");

    const searchEvents =
      Glean.firefoxviewNext.searchShowAllShowallbutton.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ section: "recentlyclosed" }, searchEvents[0].extra);
  });
});

add_task(async function test_show_all_opentabs_telemetry() {
  for (let i = 0; i < NUMBER_OF_TABS; i++) {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, URLs[1]);
  }
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    const recentBrowsing = document.querySelector("view-recentbrowsing");

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString(URLs[1], content);
    const opentabsSlot = recentBrowsing.querySelector("[slot='opentabs']");
    await TestUtils.waitForCondition(
      () => opentabsSlot.viewCards[0].tabList.rowEls.length === 5,
      "Expected search results are not shown yet."
    );
    Services.fog.testResetFOG();

    info("Click the Show All link.");
    const showAllButton = opentabsSlot.viewCards[0].shadowRoot.querySelector(
      "[data-l10n-id='firefoxview-show-all']"
    );
    await TestUtils.waitForCondition(
      () => !showAllButton.hidden,
      "Waiting for the show all button to be visible"
    );
    ok(!showAllButton.hidden, "Show all button is visible");
    await TestUtils.waitForCondition(() => {
      EventUtils.synthesizeMouseAtCenter(showAllButton, {}, content);
      if (opentabsSlot.viewCards[0].tabList.rowEls.length === NUMBER_OF_TABS) {
        return true;
      }
      return false;
    }, "All search results are not shown.");

    const searchEvents =
      Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "recentbrowsing" }, searchEvents[0].extra);
    const showAllEvents =
      Glean.firefoxviewNext.searchShowAllShowallbutton.testGetValue();
    Assert.equal(1, showAllEvents.length, "Expected one search event.");
    Assert.deepEqual({ section: "opentabs" }, showAllEvents[0].extra);
  });

  await SimpleTest.promiseFocus(window);
  await promiseAllButPrimaryWindowClosed();
  await BrowserTestUtils.switchTab(gBrowser, gInitialTab);
  await closeFirefoxViewTab(window);

  cleanUp();
});

add_task(async function test_show_all_syncedtabs_telemetry() {
  TabsSetupFlowManager.resetInternalState();

  const sandbox = setupRecentDeviceListMocks();
  const syncedTabsMock = sandbox.stub(SyncedTabs, "getRecentTabs");
  let mockTabs1 = getMockTabData(syncedTabsData);
  let getRecentTabsResult = mockTabs1;
  syncedTabsMock.callsFake(() => {
    info(
      `Stubbed SyncedTabs.getRecentTabs returning a promise that resolves to ${getRecentTabsResult.length} tabs\n`
    );
    return Promise.resolve(getRecentTabsResult);
  });
  sandbox.stub(SyncedTabs, "getTabClients").callsFake(() => {
    return Promise.resolve(syncedTabsData);
  });

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    const recentBrowsing = document.querySelector("view-recentbrowsing");

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("mozilla", content);
    const syncedtabsSlot = recentBrowsing.querySelector("[slot='syncedtabs']");
    await TestUtils.waitForCondition(
      () =>
        syncedtabsSlot.fullyUpdated &&
        syncedtabsSlot.tabLists.length === 1 &&
        Promise.all(
          Array.from(syncedtabsSlot.tabLists).map(
            tabList => tabList.updateComplete
          )
        ),
      "Synced Tabs component is done updating."
    );
    syncedtabsSlot.tabLists[0].scrollIntoView();
    await TestUtils.waitForCondition(
      () => syncedtabsSlot.tabLists[0].rowEls.length === 5,
      "Expected search results are not shown yet."
    );
    Services.fog.testResetFOG();

    const showAllButton = await TestUtils.waitForCondition(
      () =>
        syncedtabsSlot.shadowRoot.querySelector(
          "[data-l10n-id='firefoxview-show-all']"
        ),
      "Waiting for the show all button to be available in the synced tabs slot"
    );
    info("Scroll show all button into view.");
    showAllButton.scrollIntoView();
    await TestUtils.waitForCondition(
      () => !showAllButton.hidden,
      "Waiting for the show all button to be visible"
    );
    ok(!showAllButton.hidden, "Show all button is visible");
    info("Click the Show All link.");
    await TestUtils.waitForCondition(() => {
      EventUtils.synthesizeMouseAtCenter(showAllButton, {}, content);
      if (syncedtabsSlot.tabLists[0].rowEls.length === NUMBER_OF_TABS) {
        return true;
      }
      return false;
    }, "All search results are not shown.");

    const searchEvents =
      Glean.firefoxviewNext.searchInitiatedSearch.testGetValue();
    Assert.equal(1, searchEvents.length, "Expected one search event.");
    Assert.deepEqual({ page: "recentbrowsing" }, searchEvents[0].extra);
    const showAllEvents =
      Glean.firefoxviewNext.searchShowAllShowallbutton.testGetValue();
    Assert.equal(1, showAllEvents.length, "Expected one search event.");
    Assert.deepEqual({ section: "syncedtabs" }, showAllEvents[0].extra);
  });

  await tearDown(sandbox);
});

add_task(async function test_sort_history_search_telemetry() {
  for (let i = 0; i < NUMBER_OF_TABS; i++) {
    await open_then_close(URLs[i]);
  }

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "history");
    const historyComponent = document.querySelector("view-history");

    const searchTextbox = await TestUtils.waitForCondition(
      () => historyComponent.searchTextbox,
      "The search textbox is displayed."
    );

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString("example.com", content);
    await TestUtils.waitForCondition(() => {
      const { rowEls } = historyComponent.lists[0];
      return rowEls.length === 1;
    }, "There is one matching search result.");
    Services.fog.testResetFOG();
    // Select sort by site option
    await EventUtils.synthesizeMouseAtCenter(
      historyComponent.sortInputs[1],
      {},
      content
    );
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    let historyEvents = Glean.firefoxviewNext.sortHistoryTabs.testGetValue();
    Assert.equal(1, historyEvents.length, "Expected one history event.");
    Assert.deepEqual(
      { sort_type: "site", search_start: "true" },
      historyEvents[0].extra
    );

    Services.fog.testResetFOG();

    // Select sort by date option
    await EventUtils.synthesizeMouseAtCenter(
      historyComponent.sortInputs[0],
      {},
      content
    );
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    historyEvents = Glean.firefoxviewNext.sortHistoryTabs.testGetValue();
    Assert.equal(1, historyEvents.length, "Expected one history event.");
    Assert.deepEqual(
      { sort_type: "date", search_start: "true" },
      historyEvents[0].extra
    );
  });
});

add_task(async function test_cumulative_searches_recent_browsing_telemetry() {
  await PlacesUtils.history.clear();
  await open_then_close(URLs[0]);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    is(document.location.hash, "", "Searching within recent browsing.");
    const recentBrowsing = document.querySelector("view-recentbrowsing");
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString(URLs[0], content);
    const recentlyclosedSlot = recentBrowsing.querySelector(
      "[slot='recentlyclosed']"
    );
    await TestUtils.waitForCondition(
      () =>
        recentlyclosedSlot?.tabList?.rowEls?.length &&
        recentlyclosedSlot?.searchQuery,
      "Expected search results are not shown yet."
    );

    EventUtils.synthesizeMouseAtCenter(
      recentlyclosedSlot.tabList.rowEls[0].mainEl,
      {},
      content
    );
    Assert.equal(
      1,
      Glean.firefoxview.cumulativeSearches.testGetValue().recentbrowsing.sum
    );
  });

  cleanUp();
});

add_task(async function test_cumulative_searches_recently_closed_telemetry() {
  await PlacesUtils.history.clear();
  await open_then_close(URLs[0]);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    await navigateToViewAndWait(document, "recentlyclosed");
    is(
      document.location.hash,
      "#recentlyclosed",
      "Searching within recently closed."
    );
    const recentlyClosed = document.querySelector(
      "named-deck > view-recentlyclosed"
    );
    const searchTextbox = await TestUtils.waitForCondition(() => {
      return recentlyClosed.searchTextbox;
    }, "Waiting for the recently closed search textbox to be available");
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString(URLs[0], content);
    // eslint-disable-next-line no-unused-vars
    const [recentlyclosedSlot, tabList] =
      await waitForRecentlyClosedTabsList(document);
    await TestUtils.waitForCondition(
      () => recentlyclosedSlot?.searchQuery,
      "Waiting for the recently closed slot to have a search query"
    );

    await click_recently_closed_tab_item(tabList[0]);

    Assert.equal(
      1,
      Glean.firefoxview.cumulativeSearches.testGetValue().recentlyclosed.sum
    );
  });

  cleanUp();
});

add_task(async function test_cumulative_searches_open_tabs_telemetry() {
  await PlacesUtils.history.clear();
  await BrowserTestUtils.openNewForegroundTab(gBrowser, URLs[0]);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    await navigateToViewAndWait(document, "opentabs");
    is(document.location.hash, "#opentabs", "Searching within open tabs.");
    const openTabs = document.querySelector("named-deck > view-opentabs");

    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(openTabs.searchTextbox, {}, content);
    EventUtils.sendString(URLs[0], content);
    let cards;
    await TestUtils.waitForCondition(() => {
      cards = getOpenTabsCards(openTabs);
      return cards.length == 1;
    }, "There is one open tab card");
    await TestUtils.waitForCondition(
      () => cards[0].tabList.rowEls.length === 1 && openTabs?.searchQuery,
      "Expected search results are not shown yet."
    );

    info("Click a search result tab");
    EventUtils.synthesizeMouseAtCenter(
      cards[0].tabList.rowEls[0].mainEl,
      {},
      content
    );
  });

  Assert.equal(
    1,
    Glean.firefoxview.cumulativeSearches.testGetValue().opentabs.sum
  );

  cleanUp();
});

add_task(async function test_cumulative_searches_history_telemetry() {
  await PlacesUtils.history.clear();
  await open_then_close(URLs[0]);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    await navigateToViewAndWait(document, "history");
    is(document.location.hash, "#history", "Searching within history.");
    const history = document.querySelector("named-deck > view-history");
    const searchTextbox = await TestUtils.waitForCondition(() => {
      return history.searchTextbox;
    });

    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString(URLs[0], content);
    await TestUtils.waitForCondition(
      () =>
        history.fullyUpdated &&
        history?.lists[0].rowEls?.length === 1 &&
        history?.controller?.searchQuery,
      "Expected search results are not shown yet."
    );

    info("Click a search result tab");
    EventUtils.synthesizeMouseAtCenter(
      history.lists[0].rowEls[0].mainEl,
      {},
      content
    );

    Assert.equal(
      1,
      Glean.firefoxview.cumulativeSearches.testGetValue().history.sum
    );
  });

  cleanUp();
});

add_task(async function test_cumulative_searches_syncedtabs_telemetry() {
  await PlacesUtils.history.clear();
  TabsSetupFlowManager.resetInternalState();

  const sandbox = setupRecentDeviceListMocks();
  const syncedTabsMock = sandbox.stub(SyncedTabs, "getRecentTabs");
  let mockTabs1 = getMockTabData(syncedTabsData);
  let getRecentTabsResult = mockTabs1;
  syncedTabsMock.callsFake(() => {
    info(
      `Stubbed SyncedTabs.getRecentTabs returning a promise that resolves to ${getRecentTabsResult.length} tabs\n`
    );
    return Promise.resolve(getRecentTabsResult);
  });
  sandbox.stub(SyncedTabs, "getTabClients").callsFake(() => {
    return Promise.resolve(syncedTabsData);
  });

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    await navigateToViewAndWait(document, "syncedtabs");
    is(document.location.hash, "#syncedtabs", "Searching within synced tabs.");
    let syncedTabs = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(syncedTabs.searchTextbox, {}, content);
    EventUtils.sendString(URLs[0], content);
    await TestUtils.waitForCondition(
      () =>
        syncedTabs.fullyUpdated &&
        syncedTabs.tabLists.length === 1 &&
        Promise.all(
          Array.from(syncedTabs.tabLists).map(tabList => tabList.updateComplete)
        ),
      "Synced Tabs component is done updating."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabs.tabLists[0].rowEls.length === 1 &&
        syncedTabs.controller.searchQuery,
      "Expected search results are not shown yet."
    );

    info("Click a search result tab");
    EventUtils.synthesizeMouseAtCenter(
      syncedTabs.tabLists[0].rowEls[0].mainEl,
      {},
      content
    );

    Assert.equal(
      1,
      Glean.firefoxview.cumulativeSearches.testGetValue().syncedtabs.sum
    );
  });

  cleanUp();
  await tearDown(sandbox);
});
