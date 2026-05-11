/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(globalThis, {
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
});
const { ProfileAge } = ChromeUtils.importESModule(
  "resource://gre/modules/ProfileAge.sys.mjs"
);

const HAS_IMPORTED_HISTORY_PREF = "browser.migrate.interactions.history";
const IMPORT_HISTORY_DISMISSED_PREF =
  "browser.tabs.firefox-view.importHistory.dismissed";
const NEVER_REMEMBER_HISTORY_PREF = "browser.privatebrowsing.autostart";

const DAY_MS = 24 * 60 * 60 * 1000;
const today = new Date();
const yesterday = new Date(Date.now() - DAY_MS);
const twoDaysAgo = new Date(Date.now() - DAY_MS * 2);
const threeDaysAgo = new Date(Date.now() - DAY_MS * 3);
const fourDaysAgo = new Date(Date.now() - DAY_MS * 4);
const oneMonthAgo = new Date(today);
const dates = [
  today,
  yesterday,
  twoDaysAgo,
  threeDaysAgo,
  fourDaysAgo,
  oneMonthAgo,
];

// Set the date for the first day of the last month
oneMonthAgo.setDate(1);
if (oneMonthAgo.getMonth() === 0) {
  // If today's date is in January, use first day in December from the previous year
  oneMonthAgo.setMonth(11);
  oneMonthAgo.setFullYear(oneMonthAgo.getFullYear() - 1);
} else {
  oneMonthAgo.setMonth(oneMonthAgo.getMonth() - 1);
}

function isElInViewport(element) {
  const boundingRect = element.getBoundingClientRect();
  return (
    boundingRect.top >= 0 &&
    boundingRect.left >= 0 &&
    boundingRect.bottom <=
      (window.innerHeight || document.documentElement.clientHeight) &&
    boundingRect.right <=
      (window.innerWidth || document.documentElement.clientWidth)
  );
}

async function historyComponentReady(historyComponent, expectedHistoryItems) {
  await TestUtils.waitForCondition(
    () => historyComponent.controller.totalVisitsCount === expectedHistoryItems,
    "History component ready"
  );

  let expected = historyComponent.controller.historyVisits.length;
  let actual = historyComponent.cards.length;

  is(expected, actual, `Total number of cards should be ${expected}`);
}

async function addHistoryItems(dateAdded) {
  await PlacesUtils.history.insert({
    url: URLs[0],
    title: "Example Domain 1",
    visits: [{ date: dateAdded }],
  });
  await PlacesUtils.history.insert({
    url: URLs[1],
    title: "Example Domain 2",
    visits: [{ date: dateAdded }],
  });
  await PlacesUtils.history.insert({
    url: URLs[2],
    title: "Example Domain 3",
    visits: [{ date: dateAdded }],
  });
  await PlacesUtils.history.insert({
    url: URLs[3],
    title: "Example Domain 4",
    visits: [{ date: dateAdded }],
  });
}

function createHistoryEntries() {
  let historyEntries = [];
  for (let i = 0; i < 4; i++) {
    historyEntries.push({
      url: URLs[i],
      title: `Example Domain ${i}`,
      visits: dates.map(date => [{ date }]),
    });
  }
  return historyEntries;
}

add_setup(async () => {
  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    await PlacesUtils.history.clear();
  });
});

add_task(async function test_list_ordering() {
  await PlacesUtils.history.clear();
  const historyEntries = createHistoryEntries();
  await PlacesUtils.history.insertMany(historyEntries);
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    await navigateToViewAndWait(document, "history");

    let historyComponent = await TestUtils.waitForCondition(
      () => document.querySelector("view-history"),
      "History component rendered"
    );
    historyComponent.profileAge = 8;

    await historyComponentReady(historyComponent, historyEntries.length);

    let firstCard = historyComponent.cards[0];

    info("The first card should have a header for 'Today'.");
    await BrowserTestUtils.waitForMutationCondition(
      firstCard.querySelector("[slot=header]"),
      { attributes: true },
      () =>
        document.l10n.getAttributes(firstCard.querySelector("[slot=header]"))
          .id === "firefoxview-history-date-today"
    );

    // Select first history item in first card
    Services.fog.testResetFOG();
    await TestUtils.waitForCondition(
      () => historyComponent.lists[0].rowEls.length,
      "The first history list to have row elements"
    );
    let firstHistoryLink = historyComponent.lists[0].rowEls[0].mainEl;
    let promiseHidden = BrowserTestUtils.waitForEvent(
      document,
      "visibilitychange"
    );
    await EventUtils.synthesizeMouseAtCenter(firstHistoryLink, {}, content);
    Assert.equal(
      1,
      Glean.firefoxviewNext.historyVisits.testGetValue().length,
      "Expected one history event."
    );
    await promiseHidden;
    await openFirefoxViewTab(browser.documentGlobal);

    // Test number of cards when sorted by site/domain
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
    let sortEvents = Glean.firefoxviewNext.sortHistoryTabs.testGetValue();
    Assert.equal(1, sortEvents.length, "Expected one sort event.");
    Assert.deepEqual(
      { sort_type: "site", search_start: "false" },
      sortEvents[0].extra
    );

    let expectedNumOfCards = historyComponent.controller.historyVisits.length;

    info(`Total number of cards should be ${expectedNumOfCards}`);
    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.shadowRoot,
      { childList: true, subtree: true },
      () => expectedNumOfCards === historyComponent.cards.length
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
    sortEvents = Glean.firefoxviewNext.sortHistoryTabs.testGetValue();
    Assert.equal(1, sortEvents.length, "Expected one sort event.");
    Assert.deepEqual(
      { sort_type: "date", search_start: "false" },
      sortEvents[0].extra
    );

    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
});

add_task(async function test_empty_states() {
  await PlacesUtils.history.clear();
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    await navigateToViewAndWait(document, "history");

    let historyComponent = document.querySelector("view-history");
    historyComponent.profileAge = 8;
    await TestUtils.waitForCondition(
      () => historyComponent.emptyState,
      "Waiting for the history component to be in the empty state"
    );
    let emptyStateCard = historyComponent.emptyState;
    ok(
      emptyStateCard.headerEl.textContent.includes(
        "Get back to where you’ve been"
      ),
      "Initial empty state header has the expected text."
    );
    ok(
      emptyStateCard.descriptionEls[0].textContent.includes(
        "As you browse, the pages you visit will be listed here."
      ),
      "Initial empty state description has the expected text."
    );

    // Test empty state when History mode is set to never remember
    Services.prefs.setBoolPref(NEVER_REMEMBER_HISTORY_PREF, true);
    // Manually update the history component from the test, since changing this setting
    // in about:preferences will require a browser reload
    historyComponent.requestUpdate();
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    emptyStateCard = historyComponent.emptyState;
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
    // Manually update the history component from the test, since changing this setting
    // in about:preferences will require a browser reload
    historyComponent.requestUpdate();
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );

    // Test import history banner shows if profile age is 7 days or less and
    // user hasn't already imported history from another browser
    Services.prefs.setBoolPref(IMPORT_HISTORY_DISMISSED_PREF, false);
    Services.prefs.setBoolPref(HAS_IMPORTED_HISTORY_PREF, true);
    ok(!historyComponent.cards.length, "Import history banner not shown yet");
    historyComponent.profileAge = 0;
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    ok(
      !historyComponent.cards.length,
      "Import history banner still not shown yet"
    );
    Services.prefs.setBoolPref(HAS_IMPORTED_HISTORY_PREF, false);
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    ok(
      historyComponent.cards[0].textContent.includes(
        "Import history from another browser"
      ),
      "Import history banner is shown"
    );
    let importHistoryCloseButton =
      historyComponent.cards[0].querySelector("moz-button.close");
    importHistoryCloseButton.click();
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    ok(
      Services.prefs.getBoolPref(IMPORT_HISTORY_DISMISSED_PREF, true) &&
        !historyComponent.cards.length,
      "Import history banner has been dismissed."
    );
    // Reset profileAge to greater than 7 to avoid affecting other tests
    historyComponent.profileAge = 8;
    Services.prefs.setBoolPref(IMPORT_HISTORY_DISMISSED_PREF, false);

    gBrowser.removeTab(gBrowser.selectedTab);
  });
});

add_task(async function test_observers_removed_when_view_is_hidden() {
  await PlacesUtils.history.clear();
  const NEW_TAB_URL = "https://example.com";
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    NEW_TAB_URL
  );
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "history");
    const historyComponent = document.querySelector("view-history");
    historyComponent.profileAge = 8;
    let visitList = await TestUtils.waitForCondition(
      () => historyComponent.cards?.[0]?.querySelector("fxview-tab-list"),
      "the first history card to have a tab list"
    );
    info("The list should show a visit from the new tab.");
    await TestUtils.waitForCondition(
      () => visitList.rowEls.length === 1,
      "visit list to have exactly one row element"
    );

    let promiseHidden = BrowserTestUtils.waitForEvent(
      document,
      "visibilitychange"
    );
    await BrowserTestUtils.switchTab(gBrowser, tab);
    await promiseHidden;
    const { date } = await PlacesUtils.history
      .fetch(NEW_TAB_URL, {
        includeVisits: true,
      })
      .then(({ visits }) => visits[0]);
    await addHistoryItems(date);
    is(
      visitList.rowEls.length,
      1,
      "The list does not update when Firefox View is hidden."
    );

    info("The list should update when Firefox View is visible.");
    await openFirefoxViewTab(browser.documentGlobal);
    visitList = await TestUtils.waitForCondition(
      () => historyComponent.cards?.[0]?.querySelector("fxview-tab-list"),
      "the first history card to have a tab list"
    );
    await TestUtils.waitForCondition(
      () => visitList.rowEls.length > 1,
      "visit list to have more than one row element"
    );

    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function test_show_all_history_telemetry() {
  await PlacesUtils.history.clear();
  const historyEntries = createHistoryEntries();
  await PlacesUtils.history.insertMany(historyEntries);
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    await navigateToViewAndWait(document, "history");

    let historyComponent = document.querySelector("view-history");
    historyComponent.profileAge = 8;
    await historyComponentReady(historyComponent, historyEntries.length);

    Services.fog.testResetFOG();
    let showAllHistoryBtn = historyComponent.showAllHistoryBtn;
    showAllHistoryBtn.scrollIntoView();
    await EventUtils.synthesizeMouseAtCenter(showAllHistoryBtn, {}, content);
    Assert.equal(
      1,
      Glean.firefoxviewNext.showAllHistoryTabs.testGetValue().length,
      "Expected one show-all-history event."
    );

    // Make sure library window is shown
    await TestUtils.waitForCondition(
      () => Services.wm.getMostRecentWindow("Places:Organizer"),
      "Waiting for the Places Organizer window to be open"
    );
    let library = Services.wm.getMostRecentWindow("Places:Organizer");
    await BrowserTestUtils.closeWindow(library);
    gBrowser.removeTab(gBrowser.selectedTab);
  });
});

add_task(async function test_search_history() {
  await PlacesUtils.history.clear();
  const historyEntries = createHistoryEntries();
  await PlacesUtils.history.insertMany(historyEntries);
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "history");
    const historyComponent = document.querySelector("view-history");
    historyComponent.profileAge = 8;
    await historyComponentReady(historyComponent, historyEntries.length);
    const searchTextbox = await TestUtils.waitForCondition(
      () => historyComponent.searchTextbox,
      "The search textbox is displayed."
    );

    info("Input a search query.");
    // Clear any popovers that might obscure content.
    EventUtils.synthesizeKey("VK_ESCAPE", {}, window);
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString("Example Domain 1", content);
    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.shadowRoot,
      { childList: true, subtree: true },
      () =>
        historyComponent.cards.length === 1 &&
        document.l10n.getAttributes(
          historyComponent.cards[0].querySelector("[slot=header]")
        ).id === "firefoxview-search-results-header"
    );
    await TestUtils.waitForCondition(() => {
      const { rowEls } = historyComponent.lists[0];
      return rowEls.length === 1 && rowEls[0].mainEl.href === URLs[1];
    }, "There is one matching search result.");

    info("Input a bogus search query.");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString("Bogus Query", content);
    await TestUtils.waitForCondition(() => {
      const tabList = historyComponent.lists[0];
      return tabList?.emptyState;
    }, "There are no matching search results.");

    info("Clear the search query.");
    searchTextbox.select();
    EventUtils.synthesizeKey("VK_BACK_SPACE");
    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.shadowRoot,
      { childList: true, subtree: true },
      () =>
        historyComponent.cards.length ===
        historyComponent.controller.historyVisits.length
    );
    searchTextbox.blur();

    info("Input a bogus search query with keyboard.");
    EventUtils.synthesizeKey("f", { accelKey: true }, content);
    EventUtils.sendString("Bogus Query", content);
    await TestUtils.waitForCondition(() => {
      const tabList = historyComponent.lists[0];
      return tabList?.emptyState;
    }, "There are no matching search results.");

    info("Clear the search query with keyboard.");
    is(
      historyComponent.shadowRoot.activeElement,
      searchTextbox,
      "Search input is focused"
    );
    let clearButton = SpecialPowers.getInputButton(searchTextbox.inputEl);
    EventUtils.synthesizeMouseAtCenter(clearButton, {}, content);
    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.shadowRoot,
      { childList: true, subtree: true },
      () =>
        historyComponent.cards.length ===
        historyComponent.controller.historyVisits.length
    );
  });
});

add_task(async function test_search_ignores_stale_queries() {
  await PlacesUtils.history.clear();
  const historyEntries = createHistoryEntries();
  await PlacesUtils.history.insertMany(historyEntries);

  let bogusQueryInProgress = false;
  const searchDeferred = Promise.withResolvers();
  const realDatabase = await PlacesUtils.promiseLargeCacheDBConnection();
  const mockDatabase = {
    executeCached: async (sql, options) => {
      if (options.query === "Bogus Query") {
        bogusQueryInProgress = true;
        await searchDeferred.promise;
      }
      return realDatabase.executeCached(sql, options);
    },
    interrupt: () => searchDeferred.reject(),
  };
  const stub = sinon
    .stub(PlacesUtils, "promiseLargeCacheDBConnection")
    .resolves(mockDatabase);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "history");
    const historyComponent = document.querySelector("view-history");
    historyComponent.profileAge = 8;
    await historyComponentReady(historyComponent, historyEntries.length);
    const searchTextbox = await TestUtils.waitForCondition(
      () => historyComponent.searchTextbox,
      "The search textbox is displayed."
    );

    info("Input a bogus search query.");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString("Bogus Query", content);
    await TestUtils.waitForCondition(
      () => bogusQueryInProgress,
      "The bogus query to be in progress"
    );

    info("Clear the bogus query.");
    let clearButton = SpecialPowers.getInputButton(searchTextbox.inputEl);
    EventUtils.synthesizeMouseAtCenter(clearButton, {}, content);
    await searchTextbox.updateComplete;

    info("Input a real search query.");
    EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, content);
    EventUtils.sendString("Example Domain 1", content);
    await TestUtils.waitForCondition(() => {
      const { rowEls } = historyComponent.lists[0];
      return rowEls.length === 1 && rowEls[0].mainEl.href === URLs[1];
    }, "There is one matching search result.");
    searchDeferred.resolve();
    await TestUtils.waitForTick();
    const tabList = historyComponent.lists[0];
    ok(!tabList.emptyState, "Empty state should not be shown.");
  });

  stub.restore();
});

add_task(async function test_persist_collapse_card_after_view_change() {
  await PlacesUtils.history.clear();
  await addHistoryItems(today);
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "history");
    const historyComponent = document.querySelector("view-history");
    historyComponent.profileAge = 8;
    await TestUtils.waitForCondition(
      () => historyComponent.controller.totalVisitsCount === 4,
      "The history component to have a total visit count of 4"
    );
    let firstHistoryCard = historyComponent.cards[0];
    ok(
      firstHistoryCard.isExpanded,
      "The first history card is expanded initially."
    );

    // Collapse history card
    EventUtils.synthesizeMouseAtCenter(firstHistoryCard.summaryEl, {}, content);
    is(
      firstHistoryCard.detailsEl.hasAttribute("open"),
      false,
      "The first history card is now collapsed."
    );

    // Switch to a new view and then back to History
    await navigateToViewAndWait(document, "syncedtabs");
    await navigateToViewAndWait(document, "history");

    // Check that first history card is still collapsed after changing view
    ok(
      !firstHistoryCard.isExpanded,
      "The first history card is still collapsed after changing view."
    );

    await PlacesUtils.history.clear();
    gBrowser.removeTab(gBrowser.selectedTab);
  });
});

add_task(async function test_forget_about_this_site_option() {
  await PlacesUtils.history.clear();
  const TEST_URL = "https://example.com/";
  await PlacesUtils.history.insert({
    url: TEST_URL,
    title: "Example Domain",
    visits: [{ date: new Date() }],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "history");
    const historyComponent = document.querySelector("view-history");

    await TestUtils.waitForCondition(() => historyComponent.fullyUpdated);

    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.shadowRoot,
      { childList: true, subtree: true },
      () => historyComponent.lists[0]
    );
    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.lists[0].shadowRoot,
      { subtree: true, childList: true },
      () => historyComponent.lists[0].rowEls.length === 1
    );

    const firstTabList = historyComponent.lists[0];
    const firstItem = firstTabList.rowEls[0];
    const panelList = historyComponent.panelList;
    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    const panelItems = Array.from(panelList.children).filter(
      panelItem => panelItem.nodeName === "PANEL-ITEM"
    );

    const forgetOption = panelItems.find(
      item => item.dataset.l10nId === "firefoxview-history-context-forget-site"
    );

    ok(
      forgetOption.textContent.includes("Forget"),
      "Forget About This Site option is present in the context menu."
    );
    let dialogOpened = BrowserTestUtils.promiseAlertDialogOpen(
      null,
      "chrome://browser/content/places/clearDataForSite.xhtml",
      { isSubDialog: true }
    );
    const promiseForgotten =
      PlacesTestUtils.waitForNotification("page-removed");
    EventUtils.synthesizeMouseAtCenter(forgetOption, {}, content);
    info("Forget About This Site option clicked.");

    let dialog = await dialogOpened;
    info("Dialog opened.");

    let removeButton = dialog.document
      .querySelector("dialog")
      .getButton("accept");

    removeButton.click();
    info("Clear Data button clicked.");
    await Promise.all([
      BrowserTestUtils.waitForEvent(dialog, "unload"),
      promiseForgotten,
    ]);
    info("Site forgotten successfully.");

    await BrowserTestUtils.waitForMutationCondition(
      historyComponent.shadowRoot,
      { childList: true, subtree: true },
      () => !historyComponent.lists[0]?.rowEls.length
    );
    ok(
      !historyComponent.lists[0]?.rowEls.length,
      "The site has been removed from the history list."
    );
    gBrowser.removeTab(gBrowser.selectedTab);
  });
});
