/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests the zero prefix result layout used when `temporal.featureGate` is
// enabled with top sites, recent searches, trending and temporal search
// suggestions.

"use strict";

const QUERIES = ["q1", "q2", "q3", "q4", "q5", "q6", "q7"];
const NUM_TOP_SITES = 16;
const NUM_RECENT_SEARCHES = 15;

// This test is slow in chaos mode and can time out in verify mode.
requestLongerTimeout(2);

let gEngine;
let gFeature;

function topSitesPref(count = NUM_TOP_SITES) {
  return Array.from(
    { length: count },
    (_, i) => `https://site${i}.example.com/`
  ).join(",");
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.search.suggest.enabled", true],
      ["browser.urlbar.suggest.topsites", true],
      ["browser.urlbar.suggest.recentsearches", true],
      ["browser.urlbar.recentsearches.featureGate", true],
      ["browser.urlbar.suggest.quickactions", false],
      ["browser.urlbar.trending.featureGate", false],
      ["browser.newtabpage.activity-stream.topSitesRows", 2],
      ["browser.newtabpage.activity-stream.default.sites", topSitesPref()],
    ],
  });
  await updateTopSites(sites => sites?.length >= NUM_TOP_SITES);

  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      identifier: "temporalTest",
      base: {
        name: "TemporalTest",
        urls: {
          search: {
            base: "https://example.com/",
            searchTermParamName: "q",
          },
          trending: {
            base: "https://example.com/browser/browser/components/search/test/browser/trendingSuggestionEngine.sjs",
            method: "GET",
          },
        },
      },
    },
  ]);
  gEngine = SearchService.defaultEngine;

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
    await UrlbarTestUtils.formHistory.clear();
  });
});

/**
 * Replaces the recent searches, most recent last.
 *
 * @param {number|string[]} searches
 *   The number of generic searches to add, or the values of the searches.
 */
async function addRecentSearches(searches) {
  let values = Array.isArray(searches)
    ? searches
    : Array.from({ length: searches }, (_, i) => `search ${i}`);
  await UrlbarTestUtils.formHistory.clear();
  let now = Date.now();
  await UrlbarTestUtils.formHistory.add(
    values.map((value, i) => ({
      value,
      source: gEngine.name,
      lastUsed: (now - (values.length - i) * 1000) * 1000,
    }))
  );
}

/**
 * Opens the zero prefix view and returns its results in order.
 *
 * @returns {Promise<UrlbarResult[]>}
 */
async function getZeroPrefixResults() {
  gFeature._reset();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
    fireInputEvent: true,
  });

  let results = [];
  let count = UrlbarTestUtils.getResultCount(window);
  for (let i = 0; i < count; i++) {
    let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    results.push(result);
  }
  await UrlbarTestUtils.promisePopupClose(window);
  return results;
}

/**
 * Opens the zero prefix view and returns the telemetry types of its rows in
 * order, with consecutive rows of the same type collapsed into `[type, count]`
 * pairs.
 *
 * @returns {Promise<Array>}
 */
async function getZeroPrefixLayout() {
  let layout = [];
  for (let result of await getZeroPrefixResults()) {
    let type = UrlbarShared.searchEngagementTelemetryType(result);
    if (layout.at(-1)?.[0] == type) {
      layout.at(-1)[1]++;
    } else {
      layout.push([type, 1]);
    }
  }
  info(`Layout: ${JSON.stringify(layout)}`);
  return layout;
}

// Top sites, recent searches and temporal suggestions share the view 7:3:3,
// with temporal suggestions after recent searches.
add_task(async function allFilled() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  Assert.deepEqual(await getZeroPrefixLayout(), [
    ["top_site", 7],
    ["recent_search", 3],
    ["rust_temporal", 3],
  ]);
});

// Temporal suggestions grow to `temporal.rowCount` without recent searches.
add_task(async function noRecentSearches() {
  await UrlbarTestUtils.formHistory.clear();
  Assert.deepEqual(await getZeroPrefixLayout(), [
    ["top_site", 8],
    ["rust_temporal", 5],
  ]);
});

// The default zero prefix layout and `maxRichResults` are used when the user
// turns off temporal suggestions or every temporal type.
add_task(async function suggestTemporalDisabled() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  for (let pref of [
    "browser.urlbar.suggest.temporal",
    "browser.urlbar.suggest.temporal.seasonal",
  ]) {
    info(`Turning off ${pref}`);
    await SpecialPowers.pushPrefEnv({ set: [[pref, false]] });
    Assert.deepEqual(await getZeroPrefixLayout(), [
      ["top_site", 7],
      ["recent_search", 3],
    ]);
    await SpecialPowers.popPrefEnv();
  }
});

// The default zero prefix layout and `maxRichResults` are used when no temporal
// suggestion is eligible.
add_task(async function noEligibleSuggestions() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  for (let now of [
    "2026-11-11T12:00[America/New_York]",
    "2026-11-28T12:00[America/New_York]",
  ]) {
    info(`Checking now: ${now}`);
    UrlbarTestUtils.stubNowZonedDateTime(now);
    Assert.deepEqual(await getZeroPrefixLayout(), [
      ["top_site", 7],
      ["recent_search", 3],
    ]);
  }
  UrlbarTestUtils.stubNowZonedDateTime("2026-11-15T12:00[America/New_York]");
});

// Recent searches take the space temporal suggestions don't use.
add_task(async function fewTemporal() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.temporal.rowCount", 1]],
  });
  Assert.deepEqual(await getZeroPrefixLayout(), [
    ["top_site", 7],
    ["recent_search", 5],
    ["rust_temporal", 1],
  ]);
  await SpecialPowers.popPrefEnv();
});

// `temporal.zeroPrefixMaxRichResults` controls the number of results.
add_task(async function zeroPrefixMaxRichResults() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.temporal.zeroPrefixMaxRichResults", 10]],
  });
  Assert.deepEqual(await getZeroPrefixLayout(), [
    ["top_site", 5],
    ["recent_search", 3],
    ["rust_temporal", 2],
  ]);
  await SpecialPowers.popPrefEnv();
});

// A user-set `maxRichResults` takes precedence over
// `temporal.zeroPrefixMaxRichResults`, whether it is smaller or larger.
add_task(async function userSetMaxRichResults() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  let tests = [
    {
      maxRichResults: 5,
      expected: [
        ["top_site", 3],
        ["recent_search", 1],
        ["rust_temporal", 1],
      ],
    },
    {
      maxRichResults: 16,
      expected: [
        ["top_site", 9],
        ["recent_search", 4],
        ["rust_temporal", 3],
      ],
    },
  ];
  for (let { maxRichResults, expected } of tests) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.maxRichResults", maxRichResults]],
    });
    Assert.deepEqual(await getZeroPrefixLayout(), expected);
    await SpecialPowers.popPrefEnv();
  }
});

// Trending suggestions fill the space recent searches and temporal suggestions
// don't use, and appear after them.
add_task(async function trending() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.trending.featureGate", true],
      ["browser.urlbar.suggest.trending", true],
      ["browser.urlbar.trending.requireSearchMode", false],
    ],
  });

  let tests = [
    {
      desc: "Recent searches and temporal suggestions fill the search space",
      numRecentSearches: NUM_RECENT_SEARCHES,
      expected: [
        ["top_site", 7],
        ["recent_search", 3],
        ["rust_temporal", 3],
      ],
    },
    {
      desc: "One recent search",
      numRecentSearches: 1,
      expected: [
        ["top_site", 7],
        ["recent_search", 1],
        ["rust_temporal", 5],
      ],
    },
    {
      desc: "No recent searches",
      numRecentSearches: 0,
      expected: [
        ["top_site", 7],
        ["rust_temporal", 5],
        ["trending_search", 1],
      ],
    },
    {
      desc: "One recent search and two temporal suggestions",
      numRecentSearches: 1,
      prefs: [["browser.urlbar.temporal.rowCount", 2]],
      expected: [
        ["top_site", 7],
        ["recent_search", 1],
        ["rust_temporal", 2],
        ["trending_search", 3],
      ],
    },
    {
      desc: "No recent searches and temporal suggestions turned off",
      numRecentSearches: 0,
      prefs: [["browser.urlbar.suggest.temporal", false]],
      expected: [
        ["top_site", 7],
        ["trending_search", 3],
      ],
    },
  ];
  for (let { desc, numRecentSearches, prefs = [], expected } of tests) {
    info(desc);
    await addRecentSearches(numRecentSearches);
    await SpecialPowers.pushPrefEnv({ set: prefs });
    Assert.deepEqual(await getZeroPrefixLayout(), expected, desc);
    await SpecialPowers.popPrefEnv();
  }

  await SpecialPowers.popPrefEnv();
});

// Search results take the space top sites don't use when there are fewer top
// sites than fit in their share of the view.
add_task(async function fewTopSites() {
  await addRecentSearches(NUM_RECENT_SEARCHES);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.newtabpage.activity-stream.default.sites", topSitesPref(2)],
    ],
  });
  await updateTopSites(sites => sites?.length == 2);

  info("Without trending");
  Assert.deepEqual(await getZeroPrefixLayout(), [
    ["top_site", 2],
    ["recent_search", 5],
    ["rust_temporal", 5],
  ]);

  info("With trending");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.trending.featureGate", true],
      ["browser.urlbar.suggest.trending", true],
      ["browser.urlbar.trending.requireSearchMode", false],
    ],
  });
  Assert.deepEqual(await getZeroPrefixLayout(), [
    ["top_site", 2],
    ["recent_search", 5],
    ["rust_temporal", 5],
    ["trending_search", 1],
  ]);
  await SpecialPowers.popPrefEnv();

  // Pop the env pushed by `updateTopSites` and then the one setting the sites.
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
  await updateTopSites(sites => sites?.length >= NUM_TOP_SITES);
});

// A temporal suggestion that dupes a recent search isn't shown.
add_task(async function dedupeRecentSearch() {
  let [dupe] = QUERIES;
  await addRecentSearches([dupe]);
  await SpecialPowers.pushPrefEnv({
    set: [
      // Disable top sites so that we have more space for temporal results.
      ["browser.urlbar.suggest.topsites", false],
      // Set the row count to the length of the queries so all the queries
      // show at once.
      ["browser.urlbar.temporal.rowCount", QUERIES.length],
    ],
  });

  let rows = (await getZeroPrefixResults()).map(result => ({
    type: UrlbarShared.searchEngagementTelemetryType(result),
    suggestion: result.payload.suggestion,
  }));
  info(`Rows: ${JSON.stringify(rows)}`);

  Assert.deepEqual(
    rows.filter(r => r.suggestion == dupe),
    [{ type: "recent_search", suggestion: dupe }],
    "The query is shown once, as a recent search"
  );
  Assert.equal(
    rows.filter(r => r.type == "rust_temporal").length,
    QUERIES.length - 1,
    "Every other temporal query is shown"
  );

  await SpecialPowers.popPrefEnv();
  await UrlbarTestUtils.formHistory.clear();
});
