/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests temporal search suggestions in the zero prefix view.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SearchUITestUtils: "resource://testing-common/SearchUITestUtils.sys.mjs",
});

SearchUITestUtils.init(this);

const LABEL = "Seasonal Searches";
const QUERIES = ["q1", "q2", "q3", "q4", "q5", "q6", "q7"];
const TOP_SITES = [
  "https://example-1.com/",
  "https://example-2.com/",
  "https://example-3.com/",
];
const OTHER_ENGINE_NAME = "TemporalOther";

let gEngine;
let gFeature;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.suggest.recentsearches", false],
      ["browser.search.suggest.enabled", true],
      ["browser.urlbar.suggest.topsites", true],
      ["browser.newtabpage.activity-stream.default.sites", TOP_SITES.join(",")],
    ],
  });
  await updateTopSites(sites => sites?.length);

  await SearchTestUtils.installSearchExtension(
    {
      name: "TemporalTest",
      search_url: "https://example.com/",
      search_url_get_params: "q={searchTerms}",
    },
    { setAsDefault: true }
  );
  gEngine = SearchService.defaultEngine;
  await SearchTestUtils.installSearchExtension({
    name: OTHER_ENGINE_NAME,
    search_url: "https://example.org/",
  });

  UrlbarTestUtils.stubNowZonedDateTime("2026-11-15T12:00[America/New_York]");

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        type: "dynamic-suggestions",
        suggestion_type: "temporal",
        attachment: QUERIES.map(query => ({
          keywords: [""],
          dismissal_key: query,
          data: {
            group: "test-group",
            title: "Test group",
            type: "seasonal",
            startDate: "2026-11-12",
            endDate: "2026-11-27",
            engines: [gEngine.id],
            query,
          },
        })),
      },
    ],
    prefs: [["temporal.featureGate", true]],
  });
  gFeature = QuickSuggest.getFeature("TemporalSuggestions");

  registerCleanupFunction(async () => {
    gFeature._reset();
    UrlbarTestUtils.stubNowZonedDateTime(null);
    await PlacesUtils.history.clear();
  });
});

async function openZeroPrefix(win = window) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "",
    fireInputEvent: true,
  });
}

async function closeView(win = window) {
  await UrlbarTestUtils.promisePopupClose(win, () => win.gURLBar.blur());
}

/**
 * @param {Window} [win]
 * @returns {Promise<object[]>}
 *   The details of the temporal rows in the view.
 */
async function getTemporalRows(win = window) {
  let rows = [];
  let count = UrlbarTestUtils.getResultCount(win);
  for (let i = 0; i < count; i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(win, i);
    if (QuickSuggest.getFeatureByResult(details.result) == gFeature) {
      rows.push({ ...details, index: i });
    }
  }
  return rows;
}

async function getTemporalQueries(win = window) {
  return (await getTemporalRows(win)).map(r => r.result.payload.suggestion);
}

async function checkLabel(rows) {
  let { row } = rows[0].element;
  await TestUtils.waitForCondition(
    () => row.getAttribute("label") == LABEL,
    "Waiting for the label on the first temporal row"
  );
  Assert.equal(
    getComputedStyle(row, "::before").content,
    `"${LABEL}"`,
    "The label is rendered"
  );
  let groupAriaLabel = row.querySelector(".urlbarView-group-aria-label");
  await TestUtils.waitForCondition(
    () => groupAriaLabel?.getAttribute("aria-label") == LABEL,
    "Waiting for the aria label"
  );
  for (let other of rows.slice(1)) {
    Assert.ok(
      !other.element.row.hasAttribute("label"),
      "Other temporal rows have no label"
    );
  }
}

// Five temporal rows appear after the top sites, using the search glass icon.
add_task(async function rowsBelowTopSites() {
  gFeature._reset();
  await openZeroPrefix();
  let rows = await getTemporalRows();
  Assert.equal(rows.length, 5, "Five temporal rows");

  let firstIndex = rows[0].index;
  Assert.greater(firstIndex, 0, "Temporal rows come after other rows");
  for (let i = 0; i < firstIndex; i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.equal(
      details.result.providerName,
      "UrlbarProviderTopSites",
      `Row ${i} is a top site`
    );
  }
  for (let row of rows) {
    Assert.equal(
      row.image,
      UrlbarShared.ICON.SEARCH_GLASS,
      "Temporal rows use the search glass icon"
    );
  }
  await closeView();
});

// Temporal rows show the "Search with <engine>" action only when selected.
add_task(async function actionOnlyWhenSelected() {
  gFeature._reset();
  await openZeroPrefix();
  let rows = await getTemporalRows();
  Assert.equal(rows.length, 5, "Five temporal rows");
  for (let row of rows) {
    let { row: rowElement, action } = row.element;
    Assert.equal(
      rowElement.getAttribute("type"),
      "search",
      "Temporal rows have the search type"
    );
    Assert.ok(
      rowElement.hasAttribute("has-action"),
      "Temporal rows have an action"
    );
    Assert.equal(
      row.displayed.action,
      `Search with ${gEngine.name}`,
      "Temporal rows have the search action text"
    );
    Assert.ok(
      !BrowserTestUtils.isVisible(action),
      "The action is hidden when the row isn't selected"
    );

    UrlbarTestUtils.setSelectedRowIndex(window, row.index);
    Assert.ok(
      BrowserTestUtils.isVisible(action),
      "The action is visible when the row is selected"
    );
    UrlbarTestUtils.setSelectedRowIndex(window, -1);
  }
  await closeView();
});

// The first temporal row is labeled, unless group labels are disabled.
add_task(async function label() {
  gFeature._reset();
  await openZeroPrefix();
  let rows = await getTemporalRows();
  Assert.greater(rows[0].index, 0, "Temporal rows come after other rows");
  await checkLabel(rows);
  await closeView();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.groupLabels.enabled", false]],
  });
  await openZeroPrefix();
  rows = await getTemporalRows();
  Assert.ok(
    !rows[0].element.row.hasAttribute("label"),
    "No label when group labels are disabled"
  );
  await closeView();
  await SpecialPowers.popPrefEnv();
});

// The first temporal row is still labeled when it's the first row in the view.
add_task(async function labelWithoutPrecedingRows() {
  gFeature._reset();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.default.sites", ""]],
  });
  await updateTopSites(sites => !sites?.length);

  await openZeroPrefix();
  let rows = await getTemporalRows();
  Assert.equal(rows.length, 5, "Five temporal rows");
  Assert.equal(rows[0].index, 0, "Temporal rows come first");
  await checkLabel(rows);
  await closeView();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.newtabpage.activity-stream.default.sites", TOP_SITES.join(",")],
    ],
  });
  await updateTopSites(sites => sites?.length);
});

// Temporal rows are hidden while typing, and refocusing rotates to a different
// set of queries.
add_task(async function rotation() {
  gFeature._reset();
  await openZeroPrefix();
  let first = await getTemporalQueries();
  Assert.equal(first.length, 5, "Five temporal rows");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "x",
    fireInputEvent: true,
  });
  Assert.deepEqual(await getTemporalQueries(), [], "No rows while typing");
  await closeView();

  await openZeroPrefix();
  let second = await getTemporalQueries();
  Assert.equal(second.length, 5, "Five temporal rows after refocus");
  Assert.ok(
    QUERIES.filter(q => !first.includes(q)).every(q => second.includes(q)),
    "Refocusing rotates to the queries that weren't shown"
  );
  await closeView();
});

// No temporal rows are shown in private windows.
add_task(async function privateWindow() {
  gFeature._reset();
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  await openZeroPrefix(win);
  Assert.deepEqual(
    await getTemporalQueries(win),
    [],
    "No rows in a private window"
  );
  await closeView(win);
  await BrowserTestUtils.closeWindow(win);
});

// No temporal rows are shown in search mode, whether or not Suggest is active
// in search mode.
add_task(async function searchMode() {
  for (let historyInSearchMode of [false, true]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "browser.urlbar.unifiedSearchButton.historyInSearchMode",
          historyInSearchMode,
        ],
      ],
    });
    for (let engineName of [gEngine.name, OTHER_ENGINE_NAME]) {
      info(`historyInSearchMode=${historyInSearchMode} engine=${engineName}`);
      gFeature._reset();
      await openZeroPrefix();
      let searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
      await UrlbarTestUtils.activateSearchModeSwitcherItem(
        window,
        `panel-item[data-engine-name="${engineName}"]`
      );
      await searchPromise;
      Assert.equal(
        gURLBar.searchMode?.engineName,
        engineName,
        "In search mode"
      );

      await openZeroPrefix();
      Assert.deepEqual(
        await getTemporalQueries(),
        [],
        "No rows in search mode"
      );
      await UrlbarTestUtils.exitSearchMode(window);
      await closeView();
    }
    await SpecialPowers.popPrefEnv();
  }
});

// Picking a temporal row searches for its query with the default engine and
// records SAP telemetry.
add_task(async function pick() {
  gFeature._reset();
  TelemetryTestUtils.getAndClearKeyedHistogram("SEARCH_COUNTS");
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await openZeroPrefix();
  let rows = await getTemporalRows();
  let query = rows[0].result.payload.suggestion;
  let expectedUrl = gEngine.getSubmission(query).uri.spec;

  let loadPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedUrl
  );
  EventUtils.synthesizeMouseAtCenter(rows[0].element.row, {});
  await loadPromise;
  Assert.equal(
    tab.linkedBrowser.currentURI.spec,
    expectedUrl,
    "Picking a row loads the same URL as searching for the query"
  );

  await SearchUITestUtils.assertSAPTelemetry({
    engineName: gEngine.name,
    source: "urlbar",
    count: 1,
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function dontShow() {
  gFeature._reset();
  await openZeroPrefix();
  let rows = await getTemporalRows();
  Assert.equal(rows.length, 5, "Five temporal rows");
  let resultCount = UrlbarTestUtils.getResultCount(window);

  await UrlbarTestUtils.openResultMenuAndClickItem(window, "not_interested", {
    resultIndex: rows[0].index,
    openByMouse: true,
  });
  await TestUtils.waitForCondition(
    () =>
      UrlbarTestUtils.getResultCount(window) == resultCount - (rows.length - 1),
    "Waiting for the temporal rows to be removed"
  );

  Assert.ok(
    !Services.prefs.getBoolPref("browser.urlbar.suggest.temporal.seasonal"),
    "suggest.temporal.seasonal is false"
  );
  Assert.ok(
    Services.prefs.getBoolPref("browser.urlbar.suggest.temporal"),
    "suggest.temporal is true"
  );
  Assert.ok(gURLBar.view.isOpen, "The view stays open");
  Assert.deepEqual(await getTemporalQueries(), [], "No temporal rows remain");

  let details = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    rows[0].index
  );
  Assert.equal(
    details.type,
    UrlbarShared.RESULT_TYPE.TIP,
    "The temporal rows are replaced by a tip"
  );
  Assert.equal(
    details.result.payload.type,
    "dismissalAcknowledgment",
    "The tip acknowledges the dismissal"
  );

  await closeView();
  Services.prefs.clearUserPref("browser.urlbar.suggest.temporal.seasonal");
  await QuickSuggestTestUtils.forceSync();
});
