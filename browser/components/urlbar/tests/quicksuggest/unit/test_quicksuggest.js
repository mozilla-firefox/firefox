/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Basic tests for the quick suggest provider using the remote settings source.
// See also test_quicksuggest_merino.js.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AmpMatchingStrategy: "resource://gre/modules/RustSuggest.sys.mjs",
  AmpSuggestions: "resource:///modules/urlbar/private/AmpSuggestions.sys.mjs",
  SuggestionProvider: "resource://gre/modules/RustSuggest.sys.mjs",
});

const SPONSORED_SEARCH_STRING = "amp";
const NONSPONSORED_SEARCH_STRING = "wikipedia";
const SPONSORED_AND_NONSPONSORED_SEARCH_STRING = "sponsored and non-sponsored";

const HTTP_SEARCH_STRING = "http prefix";
const HTTPS_SEARCH_STRING = "https prefix";
const PREFIX_SUGGESTIONS_STRIPPED_URL = "example.com/prefix-test";

const ONE_CHAR_SEARCH_STRINGS = ["x", "x ", " x", " x "];

const { TIMESTAMP_TEMPLATE, TIMESTAMP_LENGTH } = AmpSuggestions;
const TIMESTAMP_SEARCH_STRING = "timestamp";
const TIMESTAMP_SUGGESTION_URL = `http://example.com/timestamp-${TIMESTAMP_TEMPLATE}`;
const TIMESTAMP_SUGGESTION_CLICK_URL = `http://click.reporting.test.com/timestamp-${TIMESTAMP_TEMPLATE}-foo`;

const REMOTE_SETTINGS_RESULTS = [
  QuickSuggestTestUtils.ampRemoteSettings({
    keywords: [
      SPONSORED_SEARCH_STRING,
      SPONSORED_AND_NONSPONSORED_SEARCH_STRING,
    ],
  }),
  QuickSuggestTestUtils.wikipediaRemoteSettings({
    keywords: [
      NONSPONSORED_SEARCH_STRING,
      SPONSORED_AND_NONSPONSORED_SEARCH_STRING,
    ],
  }),
  {
    id: 3,
    url: "http://" + PREFIX_SUGGESTIONS_STRIPPED_URL,
    title: "HTTP Suggestion",
    keywords: [HTTP_SEARCH_STRING],
    full_keywords: [[HTTP_SEARCH_STRING, 1]],
    click_url: "http://example.com/http-click",
    impression_url: "http://example.com/http-impression",
    advertiser: "HttpAdvertiser",
    iab_category: "22 - Shopping",
    icon: "1234",
  },
  {
    id: 4,
    url: "https://" + PREFIX_SUGGESTIONS_STRIPPED_URL,
    title: "https suggestion",
    keywords: [HTTPS_SEARCH_STRING],
    full_keywords: [[HTTPS_SEARCH_STRING, 1]],
    click_url: "http://click.reporting.test.com/prefix",
    impression_url: "http://impression.reporting.test.com/prefix",
    advertiser: "TestAdvertiserPrefix",
    iab_category: "22 - Shopping",
    icon: "1234",
  },
  {
    id: 5,
    url: TIMESTAMP_SUGGESTION_URL,
    title: "Timestamp suggestion",
    keywords: [TIMESTAMP_SEARCH_STRING],
    full_keywords: [[TIMESTAMP_SEARCH_STRING, 1]],
    click_url: TIMESTAMP_SUGGESTION_CLICK_URL,
    impression_url: "http://impression.reporting.test.com/timestamp",
    advertiser: "TestAdvertiserTimestamp",
    iab_category: "22 - Shopping",
    icon: "1234",
  },
  QuickSuggestTestUtils.ampRemoteSettings({
    keywords: [...ONE_CHAR_SEARCH_STRINGS, "12", "a longer keyword"],
    title: "Suggestion with 1-char keyword",
    url: "http://example.com/1-char-keyword",
  }),
  QuickSuggestTestUtils.ampRemoteSettings({
    keywords: [
      "amp full key",
      "amp full keyw",
      "amp full keywo",
      "amp full keywor",
      "amp full keyword",
      "xyz",
    ],
    full_keywords: [
      ["amp full keyword", 5],
      ["xyz", 1],
    ],
    title: "AMP suggestion with full keyword and prefix keywords",
    url: "https://example.com/amp-full-keyword",
  }),
  QuickSuggestTestUtils.wikipediaRemoteSettings({
    keywords: [
      "wikipedia full key",
      "wikipedia full keyw",
      "wikipedia full keywo",
      "wikipedia full keywor",
      "wikipedia full keyword",
    ],
    full_keywords: [["wikipedia full keyword", 5]],
    title: "Wikipedia suggestion with full keyword and prefix keywords",
    url: "https://example.com/wikipedia-full-keyword",
  }),
];

let gMaxResultsSuggestionsCount;

function expectedSponsoredPriorityResult() {
  return {
    ...QuickSuggestTestUtils.ampResult(),
    isBestMatch: true,
    suggestedIndex: 1,
    isSuggestedIndexRelativeToGroup: false,
  };
}

function expectedHttpResult() {
  let suggestion = REMOTE_SETTINGS_RESULTS[2];
  return QuickSuggestTestUtils.ampResult({
    keyword: HTTP_SEARCH_STRING,
    title: suggestion.title,
    url: suggestion.url,
    originalUrl: suggestion.url,
    impressionUrl: suggestion.impression_url,
    clickUrl: suggestion.click_url,
    blockId: suggestion.id,
    advertiser: suggestion.advertiser,
  });
}

function expectedHttpsResult() {
  let suggestion = REMOTE_SETTINGS_RESULTS[3];
  return QuickSuggestTestUtils.ampResult({
    keyword: HTTPS_SEARCH_STRING,
    title: suggestion.title,
    url: suggestion.url,
    originalUrl: suggestion.url,
    impressionUrl: suggestion.impression_url,
    clickUrl: suggestion.click_url,
    blockId: suggestion.id,
    advertiser: suggestion.advertiser,
  });
}

add_setup(async function init() {
  // Add a bunch of suggestions that have the same keyword so we can verify the
  // provider respects its `queryContext.maxResults` cap when adding results.
  let maxResults = UrlbarPrefs.get("maxRichResults");
  Assert.greater(maxResults, 0, "This test expects maxRichResults to be > 0");
  gMaxResultsSuggestionsCount = 2 * maxResults;
  for (let i = 0; i < gMaxResultsSuggestionsCount; i++) {
    REMOTE_SETTINGS_RESULTS.push(
      QuickSuggestTestUtils.ampRemoteSettings({
        keywords: ["maxresults"],
        title: "maxresults " + i,
        url: "https://example.com/maxresults/" + i,
      })
    );
  }

  // Install a default test engine.
  let engine = await addTestSuggestionsEngine();
  await Services.search.setDefault(
    engine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );

  UrlbarPrefs.set("scotchBonnet.enableOverride", false);
  UrlbarPrefs.set("quicksuggest.ampTopPickCharThreshold", 0);

  await QuickSuggestTestUtils.ensureQuickSuggestInit();
  await resetRemoteSettingsData();
});

add_task(async function telemetryType_sponsored() {
  Assert.equal(
    QuickSuggest.getFeature("AmpSuggestions").getSuggestionTelemetryType({}),
    "adm_sponsored",
    "Telemetry type should be 'adm_sponsored'"
  );
});

add_task(async function telemetryType_nonsponsored() {
  Assert.equal(
    QuickSuggest.getFeature(
      "OfflineWikipediaSuggestions"
    ).getSuggestionTelemetryType({}),
    "adm_nonsponsored",
    "Telemetry type should be 'adm_nonsponsored'"
  );
});

// Tests with only non-sponsored suggestions enabled with a matching search
// string.
add_task(async function nonsponsoredOnly_match() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(NONSPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.wikipediaResult()],
  });

  // The title should include the full keyword and em dash, and the part of the
  // title that the search string does not match should be highlighted.
  let result = context.results[0];
  Assert.equal(
    result.title,
    `${NONSPONSORED_SEARCH_STRING} — Wikipedia Suggestion`,
    "result.title should be correct"
  );
  Assert.deepEqual(
    result.titleHighlights,
    [],
    "result.titleHighlights should be correct"
  );
});

// Tests with only non-sponsored suggestions enabled with a non-matching search
// string.
add_task(async function nonsponsoredOnly_noMatch() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({ context, matches: [] });
});

// Tests with only sponsored suggestions enabled with a matching search string.
add_task(async function sponsoredOnly_sponsored() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.ampResult()],
  });

  // The title should include the full keyword and em dash, and the part of the
  // title that the search string does not match should be highlighted.
  let result = context.results[0];
  Assert.equal(
    result.title,
    `${SPONSORED_SEARCH_STRING} — Amp Suggestion`,
    "result.title should be correct"
  );
  Assert.deepEqual(
    result.titleHighlights,
    [],
    "result.titleHighlights should be correct"
  );
});

// Tests with only sponsored suggestions enabled with a non-matching search
// string.
add_task(async function sponsoredOnly_nonsponsored() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(NONSPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({ context, matches: [] });
});

// Tests with both sponsored and non-sponsored suggestions enabled with a
// search string that matches the sponsored suggestion.
add_task(async function both_sponsored() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.ampResult()],
  });
});

// Tests with both sponsored and non-sponsored suggestions enabled with a
// search string that matches the non-sponsored suggestion.
add_task(async function both_nonsponsored() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(NONSPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.wikipediaResult()],
  });
});

// Tests with both sponsored and non-sponsored suggestions enabled with a
// search string that doesn't match either suggestion.
add_task(async function both_noMatch() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext("this doesn't match anything", {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({ context, matches: [] });
});

// Tests with both the main and sponsored prefs disabled with a search string
// that matches the sponsored suggestion.
add_task(async function neither_sponsored() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);

  let context = createContext(SPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({ context, matches: [] });
});

// Tests with both the main and sponsored prefs disabled with a search string
// that matches the non-sponsored suggestion.
add_task(async function neither_nonsponsored() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);

  let context = createContext(NONSPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({ context, matches: [] });
});

// Search string matching should be case insensitive and ignore leading spaces.
add_task(async function caseInsensitiveAndLeadingSpaces() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext("  " + SPONSORED_SEARCH_STRING.toUpperCase(), {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.ampResult()],
  });
});

// The provider should not be active for search strings that are empty or
// contain only spaces.
add_task(async function emptySearchStringsAndSpaces() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let searchStrings = ["", " ", "  ", "              "];
  for (let str of searchStrings) {
    let msg = JSON.stringify(str) + ` (length = ${str.length})`;
    info("Testing search string: " + msg);

    let context = createContext(str, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    });
    await check_results({
      context,
      matches: [],
    });
    Assert.ok(
      !UrlbarProviderQuickSuggest.isActive(context),
      "Provider should not be active for search string: " + msg
    );
  }
});

// Results should be returned even when `browser.search.suggest.enabled` is
// false.
add_task(async function browser_search_suggest_disabled() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", false);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.ampResult({ suggestedIndex: -1 })],
  });

  UrlbarPrefs.clear("browser.search.suggest.enabled");
});

// Results should be returned even when `browser.urlbar.suggest.searches` is
// false.
add_task(async function browser_suggest_searches_disabled() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("suggest.searches", false);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [QuickSuggestTestUtils.ampResult({ suggestedIndex: -1 })],
  });

  UrlbarPrefs.clear("suggest.searches");
});

// Neither sponsored nor non-sponsored results should appear in private contexts
// even when suggestions in private windows are enabled.
add_task(async function privateContext() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  for (let privateSuggestionsEnabled of [true, false]) {
    UrlbarPrefs.set(
      "browser.search.suggest.enabled.private",
      privateSuggestionsEnabled
    );
    let context = createContext(SPONSORED_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: true,
    });
    await check_results({
      context,
      matches: [],
    });
  }

  UrlbarPrefs.clear("browser.search.suggest.enabled.private");
});

// When search suggestions come before general results and the only general
// result is a quick suggest result, it should come last.
add_task(async function suggestionsBeforeGeneral_only() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", true);
  UrlbarPrefs.set("suggest.searches", true);
  UrlbarPrefs.set("showSearchSuggestionsFirst", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        heuristic: true,
        query: SPONSORED_SEARCH_STRING,
        engineName: Services.search.defaultEngine.name,
      }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " foo",
        engineName: Services.search.defaultEngine.name,
      }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " bar",
        engineName: Services.search.defaultEngine.name,
      }),
      QuickSuggestTestUtils.ampResult(),
    ],
  });

  UrlbarPrefs.clear("browser.search.suggest.enabled");
  UrlbarPrefs.clear("suggest.searches");
  UrlbarPrefs.clear("showSearchSuggestionsFirst");
});

// When search suggestions come before general results and there are other
// general results besides quick suggest, the quick suggest result should come
// last.
add_task(async function suggestionsBeforeGeneral_others() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", true);
  UrlbarPrefs.set("suggest.searches", true);
  UrlbarPrefs.set("showSearchSuggestionsFirst", true);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, { isPrivate: false });

  // Add some history that will match our query below.
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let historyResults = [];
  for (let i = 0; i < maxResults; i++) {
    let url = "http://example.com/" + SPONSORED_SEARCH_STRING + i;
    historyResults.push(
      makeVisitResult(context, {
        uri: url,
        title: "test visit for " + url,
      })
    );
    await PlacesTestUtils.addVisits(url);
  }
  historyResults = historyResults.reverse().slice(0, historyResults.length - 4);

  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        heuristic: true,
        query: SPONSORED_SEARCH_STRING,
        engineName: Services.search.defaultEngine.name,
      }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " foo",
        engineName: Services.search.defaultEngine.name,
      }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " bar",
        engineName: Services.search.defaultEngine.name,
      }),
      QuickSuggestTestUtils.ampResult(),
      ...historyResults,
    ],
  });

  UrlbarPrefs.clear("browser.search.suggest.enabled");
  UrlbarPrefs.clear("suggest.searches");
  UrlbarPrefs.clear("showSearchSuggestionsFirst");
  await PlacesUtils.history.clear();
});

// When general results come before search suggestions and the only general
// result is a quick suggest result, it should come before suggestions.
add_task(async function generalBeforeSuggestions_only() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", true);
  UrlbarPrefs.set("suggest.searches", true);
  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        heuristic: true,
        query: SPONSORED_SEARCH_STRING,
        engineName: Services.search.defaultEngine.name,
      }),
      QuickSuggestTestUtils.ampResult({ suggestedIndex: -1 }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " foo",
        engineName: Services.search.defaultEngine.name,
      }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " bar",
        engineName: Services.search.defaultEngine.name,
      }),
    ],
  });

  UrlbarPrefs.clear("browser.search.suggest.enabled");
  UrlbarPrefs.clear("suggest.searches");
  UrlbarPrefs.clear("showSearchSuggestionsFirst");
});

// When general results come before search suggestions and there are other
// general results besides quick suggest, the quick suggest result should be the
// last general result.
add_task(async function generalBeforeSuggestions_others() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", true);
  UrlbarPrefs.set("suggest.searches", true);
  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  await QuickSuggestTestUtils.forceSync();

  let context = createContext(SPONSORED_SEARCH_STRING, { isPrivate: false });

  // Add some history that will match our query below.
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let historyResults = [];
  for (let i = 0; i < maxResults; i++) {
    let url = "http://example.com/" + SPONSORED_SEARCH_STRING + i;
    historyResults.push(
      makeVisitResult(context, {
        uri: url,
        title: "test visit for " + url,
      })
    );
    await PlacesTestUtils.addVisits(url);
  }
  historyResults = historyResults.reverse().slice(0, historyResults.length - 4);

  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        heuristic: true,
        query: SPONSORED_SEARCH_STRING,
        engineName: Services.search.defaultEngine.name,
      }),
      ...historyResults,
      QuickSuggestTestUtils.ampResult({ suggestedIndex: -1 }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " foo",
        engineName: Services.search.defaultEngine.name,
      }),
      makeSearchResult(context, {
        query: SPONSORED_SEARCH_STRING,
        suggestion: SPONSORED_SEARCH_STRING + " bar",
        engineName: Services.search.defaultEngine.name,
      }),
    ],
  });

  UrlbarPrefs.clear("browser.search.suggest.enabled");
  UrlbarPrefs.clear("suggest.searches");
  UrlbarPrefs.clear("showSearchSuggestionsFirst");
  await PlacesUtils.history.clear();
});

// The provider should not add more than `queryContext.maxResults` results.
add_task(async function maxResults() {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let searchString = "maxresults";
  let suggestions = await QuickSuggest.rustBackend.query(searchString);
  Assert.equal(
    suggestions.length,
    gMaxResultsSuggestionsCount,
    "The backend should return all matching suggestions"
  );

  let context = createContext(searchString, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  // Spy on `muxer.sort()` so we can verify the provider limited the number of
  // results it added to the query.
  let muxerName = context.muxer || "UnifiedComplete";
  let muxer = UrlbarProvidersManager.muxers.get(muxerName);
  Assert.ok(!!muxer, "Muxer should exist");

  let sandbox = sinon.createSandbox();
  let spy = sandbox.spy(muxer, "sort");

  // Use `check_results()` to do the query.
  await check_results({
    context,
    matches: [
      QuickSuggestTestUtils.ampResult({
        keyword: "maxresults",
        title: "maxresults 0",
        url: "https://example.com/maxresults/0",
      }),
    ],
  });

  // Check the `sort()` calls.
  let calls = spy.getCalls();
  Assert.greater(
    calls.length,
    0,
    "muxer.sort() should have been called at least once"
  );

  for (let c of calls) {
    let unsortedResults = c.args[1];
    Assert.lessOrEqual(
      unsortedResults.length,
      UrlbarPrefs.get("maxRichResults"),
      "Provider should have added no more than maxRichResults results"
    );
  }

  sandbox.restore();
});

// When the Suggest provider adds more than one result and they are not hidden
// exposures, the muxer should add the first one to the final results list and
// discard the rest, and the discarded results should not prevent the muxer from
// adding other non-Suggest results.
add_task(async function manySuggestResults_visible() {
  await doManySuggestResultsTest({
    expectedSuggestResults: [
      QuickSuggestTestUtils.ampResult({
        keyword: "maxresults",
        title: "maxresults 0",
        url: "https://example.com/maxresults/0",
      }),
    ],
    expectedOtherResultsCount: UrlbarPrefs.get("maxRichResults") - 1,
  });
});

// When the Suggest provider adds more than one result and they are hidden
// exposures, the muxer should add up to `queryContext.maxResults` of them to
// the final results list, and they should not prevent the muxer from adding
// other non-Suggest results.
add_task(async function manySuggestResults_hiddenExposures() {
  UrlbarPrefs.set("exposureResults", "rust_adm_sponsored");
  UrlbarPrefs.set("showExposureResults", false);

  // Build the list of expected Suggest results.
  let results = [];
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let suggestResultsCount = Math.min(gMaxResultsSuggestionsCount, maxResults);
  for (let i = 0; i < suggestResultsCount; i++) {
    let index = maxResults - 1 - i;
    results.push({
      ...QuickSuggestTestUtils.ampResult({
        keyword: "maxresults",
        title: "maxresults " + index,
        url: "https://example.com/maxresults/" + index,
      }),
      exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
    });
  }

  await doManySuggestResultsTest({
    expectedSuggestResults: results,
    expectedOtherResultsCount: maxResults,
  });

  UrlbarPrefs.clear("exposureResults");
  UrlbarPrefs.clear("showExposureResults");
});

async function doManySuggestResultsTest({
  expectedSuggestResults,
  expectedOtherResultsCount,
}) {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  // Make sure many Suggest suggestions match the search string.
  let searchString = "maxresults";
  let suggestions = await QuickSuggest.rustBackend.query(searchString);
  Assert.equal(
    suggestions.length,
    gMaxResultsSuggestionsCount,
    "Sanity check: The backend should return all matching suggestions"
  );
  Assert.greater(
    suggestions.length,
    1,
    "Sanity check: There should be more than 1 matching suggestion"
  );

  // Register a test provider that adds a bunch of history results.
  let otherResults = [];
  let maxResults = UrlbarPrefs.get("maxRichResults");
  for (let i = 0; i < maxResults; i++) {
    otherResults.push(
      new UrlbarResult(
        UrlbarUtils.RESULT_TYPE.URL,
        UrlbarUtils.RESULT_SOURCE.HISTORY,
        { url: "http://example.com/history/" + i }
      )
    );
  }

  let provider = new UrlbarTestUtils.TestProvider({ results: otherResults });
  UrlbarProvidersManager.registerProvider(provider);

  // Do a search that matches all the Suggest suggestions and the test
  // provider's results. The Suggest suggestion(s) should be first since its
  // `suggestedIndex` is 0.
  await check_results({
    context: createContext(searchString, {
      providers: [UrlbarProviderQuickSuggest.name, provider.name],
      isPrivate: false,
    }),
    matches: [
      ...expectedSuggestResults,
      ...otherResults.slice(0, expectedOtherResultsCount),
    ],
  });

  UrlbarProvidersManager.unregisterProvider(provider);
}

add_task(async function dedupeAgainstURL_samePrefix() {
  await doDedupeAgainstURLTest({
    searchString: HTTP_SEARCH_STRING,
    expectedQuickSuggestResult: expectedHttpResult(),
    otherPrefix: "http://",
    expectOther: false,
  });
});

add_task(async function dedupeAgainstURL_higherPrefix() {
  await doDedupeAgainstURLTest({
    searchString: HTTPS_SEARCH_STRING,
    expectedQuickSuggestResult: expectedHttpsResult(),
    otherPrefix: "http://",
    expectOther: false,
  });
});

add_task(async function dedupeAgainstURL_lowerPrefix() {
  await doDedupeAgainstURLTest({
    searchString: HTTP_SEARCH_STRING,
    expectedQuickSuggestResult: expectedHttpResult(),
    otherPrefix: "https://",
    expectOther: true,
  });
});

/**
 * Tests how the muxer dedupes URL results against quick suggest results.
 * Depending on prefix rank, quick suggest results should be preferred over
 * other URL results with the same stripped URL: Other results should be
 * discarded when their prefix rank is lower than the prefix rank of the quick
 * suggest. They should not be discarded when their prefix rank is higher, and
 * in that case both results should be included.
 *
 * This function adds a visit to the URL formed by the given `otherPrefix` and
 * `PREFIX_SUGGESTIONS_STRIPPED_URL`. The visit's title will be set to the given
 * `searchString` so that both the visit and the quick suggest will match it.
 *
 * @param {object} options
 *   Options object.
 * @param {string} options.searchString
 *   The search string that should trigger one of the mock prefix-test quick
 *   suggest results.
 * @param {object} options.expectedQuickSuggestResult
 *   The expected quick suggest result.
 * @param {string} options.otherPrefix
 *   The visit will be created with a URL with this prefix, e.g., "http://".
 * @param {boolean} options.expectOther
 *   Whether the visit result should appear in the final results.
 */
async function doDedupeAgainstURLTest({
  searchString,
  expectedQuickSuggestResult,
  otherPrefix,
  expectOther,
}) {
  // Disable search suggestions. This means the expected suggestedIndex for
  // sponsored suggestions will now be -1. We assume expectedQuickSuggestResult
  // is sponsored, so set its suggestedIndex now.
  UrlbarPrefs.set("suggest.searches", false);
  expectedQuickSuggestResult.suggestedIndex = -1;

  // Add a visit that will match our query below.
  let otherURL = otherPrefix + PREFIX_SUGGESTIONS_STRIPPED_URL;
  await PlacesTestUtils.addVisits({ uri: otherURL, title: searchString });

  // First, do a search with quick suggest disabled to make sure the search
  // string matches the visit.
  info("Doing first query");
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  let context = createContext(searchString, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        heuristic: true,
        query: searchString,
        engineName: Services.search.defaultEngine.name,
      }),
      makeVisitResult(context, {
        uri: otherURL,
        title: searchString,
      }),
    ],
  });

  // Now do another search with quick suggest enabled.
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  context = createContext(searchString, { isPrivate: false });

  let expectedResults = [
    makeSearchResult(context, {
      heuristic: true,
      query: searchString,
      engineName: Services.search.defaultEngine.name,
    }),
  ];

  if (expectOther) {
    expectedResults.push(
      makeVisitResult(context, {
        uri: otherURL,
        title: searchString,
      })
    );
  }

  // The expected result is last since its expected suggestedIndex is -1.
  expectedResults.push(expectedQuickSuggestResult);

  info("Doing second query");
  await check_results({ context, matches: expectedResults });

  UrlbarPrefs.clear("suggest.quicksuggest.nonsponsored");
  UrlbarPrefs.clear("suggest.quicksuggest.sponsored");
  await QuickSuggestTestUtils.forceSync();

  UrlbarPrefs.clear("suggest.searches");
  await PlacesUtils.history.clear();
}

// Timestamp templates in URLs should be replaced with real timestamps.
add_task(async function timestamps() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  // Do a search.
  let context = createContext(TIMESTAMP_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  let controller = UrlbarTestUtils.newMockController();
  await controller.startQuery(context);

  // Should be one quick suggest result.
  Assert.equal(context.results.length, 1, "One result returned");
  let result = context.results[0];

  QuickSuggestTestUtils.assertTimestampsReplaced(result, {
    url: TIMESTAMP_SUGGESTION_URL,
    sponsoredClickUrl: TIMESTAMP_SUGGESTION_CLICK_URL,
  });
});

// Real quick suggest URLs include a timestamp template that
// UrlbarProviderQuickSuggest fills in when it fetches suggestions. When the
// user picks a quick suggest, its URL with its particular timestamp is added to
// history. If the user triggers the quick suggest again later, its new
// timestamp may be different from the one in the user's history. In that case,
// the two URLs should be treated as dupes and only the quick suggest should be
// shown, not the URL from history.
add_task(async function dedupeAgainstURL_timestamps() {
  // Disable search suggestions. This means the expected suggestedIndex for
  // sponsored suggestions will now be -1.
  UrlbarPrefs.set("suggest.searches", false);

  // Add a visit that will match the query below and dupe the quick suggest.
  let dupeURL = TIMESTAMP_SUGGESTION_URL.replace(
    TIMESTAMP_TEMPLATE,
    "2013051113"
  );

  // Add other visits that will match the query and almost dupe the quick
  // suggest but not quite because they have invalid timestamps.
  let badTimestamps = [
    // not numeric digits
    "x".repeat(TIMESTAMP_LENGTH),
    // too few digits
    "5".repeat(TIMESTAMP_LENGTH - 1),
    // empty string, too few digits
    "",
  ];
  let badTimestampURLs = badTimestamps.map(str =>
    TIMESTAMP_SUGGESTION_URL.replace(TIMESTAMP_TEMPLATE, str)
  );

  await PlacesTestUtils.addVisits(
    [dupeURL, ...badTimestampURLs].map(uri => ({
      uri,
      title: TIMESTAMP_SEARCH_STRING,
    }))
  );

  // First, do a search with quick suggest disabled to make sure the search
  // string matches all the other URLs.
  info("Doing first query");
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  let context = createContext(TIMESTAMP_SEARCH_STRING, { isPrivate: false });

  let expectedHeuristic = makeSearchResult(context, {
    heuristic: true,
    query: TIMESTAMP_SEARCH_STRING,
    engineName: Services.search.defaultEngine.name,
  });
  let expectedDupeResult = makeVisitResult(context, {
    uri: dupeURL,
    title: TIMESTAMP_SEARCH_STRING,
  });
  let expectedBadTimestampResults = [...badTimestampURLs].reverse().map(uri =>
    makeVisitResult(context, {
      uri,
      title: TIMESTAMP_SEARCH_STRING,
    })
  );

  await check_results({
    context,
    matches: [
      expectedHeuristic,
      ...expectedBadTimestampResults,
      expectedDupeResult,
    ],
  });

  // Now do another search with quick suggest enabled.
  info("Doing second query");
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();
  context = createContext(TIMESTAMP_SEARCH_STRING, { isPrivate: false });

  let expectedQuickSuggest = QuickSuggestTestUtils.ampResult({
    originalUrl: TIMESTAMP_SUGGESTION_URL,
    keyword: TIMESTAMP_SEARCH_STRING,
    title: "Timestamp suggestion",
    impressionUrl: "http://impression.reporting.test.com/timestamp",
    blockId: 5,
    advertiser: "TestAdvertiserTimestamp",
    iabCategory: "22 - Shopping",
    // suggestedIndex is -1 since search suggestions are disabled.
    suggestedIndex: -1,
  });

  let expectedResults = [expectedHeuristic, ...expectedBadTimestampResults];

  const QUICK_SUGGEST_INDEX = expectedResults.length;
  expectedResults.push(expectedQuickSuggest);

  let controller = UrlbarTestUtils.newMockController();
  await controller.startQuery(context);
  info("Actual results: " + JSON.stringify(context.results));

  Assert.equal(
    context.results.length,
    expectedResults.length,
    "Found the expected number of results"
  );

  function getPayload(result, keysToIgnore = []) {
    keysToIgnore.push("suggestionObject");
    let payload = {};
    for (let [key, value] of Object.entries(result.payload)) {
      if (value !== undefined && !keysToIgnore.includes(key)) {
        payload[key] = value;
      }
    }
    return payload;
  }

  // Check actual vs. expected result properties.
  for (let i = 0; i < expectedResults.length; i++) {
    let actual = context.results[i];
    let expected = expectedResults[i];
    info(
      `Comparing results at index ${i}:` +
        " actual=" +
        JSON.stringify(actual) +
        " expected=" +
        JSON.stringify(expected)
    );
    Assert.equal(
      actual.type,
      expected.type,
      `result.type at result index ${i}`
    );
    Assert.equal(
      actual.source,
      expected.source,
      `result.source at result index ${i}`
    );
    Assert.equal(
      actual.heuristic,
      expected.heuristic,
      `result.heuristic at result index ${i}`
    );

    // Check payloads except for the quick suggest.
    if (i != QUICK_SUGGEST_INDEX) {
      Assert.deepEqual(
        getPayload(context.results[i], ["lastVisit"]),
        getPayload(expectedResults[i], ["lastVisit"]),
        "Payload at index " + i
      );
    }
  }

  // Check the quick suggest's payload excluding the timestamp-related
  // properties.
  let actualQuickSuggest = context.results[QUICK_SUGGEST_INDEX];
  let timestampKeys = [
    "displayUrl",
    "sponsoredClickUrl",
    "url",
    "urlTimestampIndex",
  ];
  Assert.deepEqual(
    getPayload(actualQuickSuggest, timestampKeys),
    getPayload(expectedQuickSuggest, timestampKeys),
    "Quick suggest payload excluding timestamp-related keys"
  );

  // Now check the timestamps in the payload.
  QuickSuggestTestUtils.assertTimestampsReplaced(actualQuickSuggest, {
    url: TIMESTAMP_SUGGESTION_URL,
    sponsoredClickUrl: TIMESTAMP_SUGGESTION_CLICK_URL,
  });

  // Clean up.
  UrlbarPrefs.clear("suggest.quicksuggest.nonsponsored");
  UrlbarPrefs.clear("suggest.quicksuggest.sponsored");
  await QuickSuggestTestUtils.forceSync();

  UrlbarPrefs.clear("suggest.searches");
  await PlacesUtils.history.clear();
});

// Tests `UrlbarResult` dismissal.
add_task(async function dismissResult() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let tests = [
    // [suggestion, expected result]
    [REMOTE_SETTINGS_RESULTS[0], QuickSuggestTestUtils.ampResult()],
    [REMOTE_SETTINGS_RESULTS[1], QuickSuggestTestUtils.wikipediaResult()],
    [REMOTE_SETTINGS_RESULTS[2], expectedHttpResult()],
    [REMOTE_SETTINGS_RESULTS[3], expectedHttpsResult()],
  ];

  for (let [suggestion, expectedResult] of tests) {
    info("Testing suggestion: " + JSON.stringify(suggestion));

    // Do a search to get a real `UrlbarResult` created for the suggestion.
    let context = createContext(suggestion.keywords[0], {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    });
    await check_results({
      context,
      matches: [expectedResult],
    });

    // Dismiss it.
    await QuickSuggest.dismissResult(context.results[0]);
    Assert.ok(
      await QuickSuggest.isResultDismissed(context.results[0]),
      "isResultDismissed should return true"
    );
    Assert.ok(
      await QuickSuggest.canClearDismissedSuggestions(),
      "canClearDismissedSuggestions should return true"
    );

    // Do another search. The result shouldn't be added.
    await check_results({
      context: createContext(suggestion.keywords[0], {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });

    await QuickSuggest.clearDismissedSuggestions();
    Assert.ok(
      !(await QuickSuggest.isResultDismissed(context.results[0])),
      "isResultDismissed should return false"
    );
    Assert.ok(
      !(await QuickSuggest.canClearDismissedSuggestions()),
      "canClearDismissedSuggestions should return false"
    );
  }
});

// Tests dismissing a `UrlbarResult` whose URL has a timestamp template.
add_task(async function dismissResultWithTimestamp() {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  await QuickSuggestTestUtils.forceSync();

  // Do a search.
  let context = createContext(TIMESTAMP_SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });
  let controller = UrlbarTestUtils.newMockController();
  await controller.startQuery(context);

  // Should be one quick suggest result.
  Assert.equal(context.results.length, 1, "One result returned");
  let result = context.results[0];

  QuickSuggestTestUtils.assertTimestampsReplaced(result, {
    url: TIMESTAMP_SUGGESTION_URL,
    sponsoredClickUrl: TIMESTAMP_SUGGESTION_CLICK_URL,
  });

  Assert.ok(result.payload.originalUrl, "The actual result has an originalUrl");
  Assert.equal(
    result.payload.originalUrl,
    REMOTE_SETTINGS_RESULTS[4].url,
    "The actual result's originalUrl should be the raw suggestion URL with a timestamp template"
  );

  // Dismiss the result.
  await QuickSuggest.dismissResult(result);
  Assert.ok(
    await QuickSuggest.isResultDismissed(result),
    "isResultDismissed should return true"
  );
  Assert.ok(
    await QuickSuggest.canClearDismissedSuggestions(),
    "canClearDismissedSuggestions should return true"
  );

  // Do another search. The result shouldn't be added.
  await check_results({
    context: createContext(TIMESTAMP_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  await QuickSuggest.clearDismissedSuggestions();
  Assert.ok(
    !(await QuickSuggest.isResultDismissed(context.results[0])),
    "isResultDismissed should return false"
  );
  Assert.ok(
    !(await QuickSuggest.canClearDismissedSuggestions()),
    "canClearDismissedSuggestions should return false"
  );
});

add_task(async function sponsoredPriority_normal() {
  await doSponsoredPriorityTest({
    searchWord: SPONSORED_SEARCH_STRING,
    remoteSettingsData: [REMOTE_SETTINGS_RESULTS[0]],
    expectedMatches: [expectedSponsoredPriorityResult()],
  });
});

add_task(async function sponsoredPriority_nonsponsoredSuggestion() {
  // Not affect to except sponsored suggestion.
  await doSponsoredPriorityTest({
    searchWord: NONSPONSORED_SEARCH_STRING,
    remoteSettingsData: [REMOTE_SETTINGS_RESULTS[1]],
    expectedMatches: [QuickSuggestTestUtils.wikipediaResult()],
  });
});

add_task(async function sponsoredPriority_sponsoredIndex() {
  await doSponsoredPriorityTest({
    nimbusSettings: { quickSuggestSponsoredIndex: 2 },
    searchWord: SPONSORED_SEARCH_STRING,
    remoteSettingsData: [REMOTE_SETTINGS_RESULTS[0]],
    expectedMatches: [expectedSponsoredPriorityResult()],
  });
});

async function doSponsoredPriorityTest({
  remoteSettingsConfig = {},
  nimbusSettings = {},
  searchWord,
  remoteSettingsData,
  expectedMatches,
}) {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    ...nimbusSettings,
    quickSuggestSponsoredPriority: true,
  });

  await resetRemoteSettingsData(remoteSettingsData);
  await QuickSuggestTestUtils.setConfig(remoteSettingsConfig);

  await check_results({
    context: createContext(searchWord, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: expectedMatches,
  });

  await cleanUpNimbusEnable();
  await resetRemoteSettingsData();
  await QuickSuggestTestUtils.setConfig(QuickSuggestTestUtils.DEFAULT_CONFIG);
}

// When a Suggest best match and a tab-to-search (TTS) are shown in the same
// search, both will have a `suggestedIndex` value of 1. The TTS should appear
// first.
add_task(async function tabToSearch() {
  // We'll use a sponsored priority result as the best match result. Different
  // types of Suggest results can appear as best matches, and they all should
  // have the same behavior.
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  Services.prefs.setBoolPref(
    "browser.urlbar.quicksuggest.sponsoredPriority",
    true
  );

  // Disable tab-to-search onboarding results so we get a regular TTS result,
  // which we can test a little more easily with `makeSearchResult()`.
  UrlbarPrefs.set("tabToSearch.onboard.interactionsLeft", 0);

  // Disable search suggestions so we don't need to expect them below.
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  // Install a test engine. The main part of its domain name needs to match the
  // best match result too so we can trigger both its TTS and the best match.
  let engineURL = `https://foo.${SPONSORED_SEARCH_STRING}.com/`;
  let extension = await SearchTestUtils.installSearchExtension(
    {
      name: "Test",
      search_url: engineURL,
    },
    { skipUnload: true }
  );
  let engine = Services.search.getEngineByName("Test");

  // Also need to add a visit to trigger TTS.
  await PlacesTestUtils.addVisits(engineURL);

  let context = createContext(SPONSORED_SEARCH_STRING, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      // search heuristic
      makeSearchResult(context, {
        engineName: Services.search.defaultEngine.name,
        engineIconUri: await Services.search.defaultEngine.getIconURL(),
        heuristic: true,
      }),
      // tab to search
      makeSearchResult(context, {
        engineName: engine.name,
        engineIconUri: UrlbarUtils.ICON.SEARCH_GLASS,
        searchUrlDomainWithoutSuffix: UrlbarUtils.stripPublicSuffixFromHost(
          engine.searchUrlDomain
        ),
        providesSearchMode: true,
        query: "",
        providerName: "TabToSearch",
        satisfiesAutofillThreshold: true,
      }),
      // Suggest best match
      expectedSponsoredPriorityResult(),
      // visit
      makeVisitResult(context, {
        uri: engineURL,
        title: `test visit for ${engineURL}`,
      }),
    ],
  });

  await cleanupPlaces();
  await extension.unload();

  UrlbarPrefs.clear("tabToSearch.onboard.interactionsLeft");
  Services.prefs.clearUserPref("browser.search.suggest.enabled");
  Services.prefs.clearUserPref("browser.urlbar.quicksuggest.sponsoredPriority");
});

// The `Amp` and `Wikipedia` Rust providers should be passed to the Rust
// component when querying depending on whether sponsored and non-sponsored
// suggestions are enabled.
add_task(async function rustProviders() {
  await doRustProvidersTests({
    searchString: SPONSORED_AND_NONSPONSORED_SEARCH_STRING,
    tests: [
      {
        prefs: {
          "suggest.quicksuggest.nonsponsored": true,
          "suggest.quicksuggest.sponsored": true,
        },
        expectedUrls: [
          "https://example.com/amp",
          "https://example.com/wikipedia",
        ],
      },
      {
        prefs: {
          "suggest.quicksuggest.nonsponsored": true,
          "suggest.quicksuggest.sponsored": false,
        },
        expectedUrls: ["https://example.com/wikipedia"],
      },
      {
        prefs: {
          "suggest.quicksuggest.nonsponsored": false,
          "suggest.quicksuggest.sponsored": true,
        },
        expectedUrls: ["https://example.com/amp"],
      },
      {
        prefs: {
          "suggest.quicksuggest.nonsponsored": false,
          "suggest.quicksuggest.sponsored": false,
        },
        expectedUrls: [],
      },
    ],
  });
});

// Tests the keyword/search-string-length threshold. Keywords/search strings
// must be at least two characters long to be matched.
add_task(async function keywordLengthThreshold() {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  let tests = [
    ...ONE_CHAR_SEARCH_STRINGS.map(keyword => ({ keyword, expected: false })),
    { keyword: "12", expected: true },
    { keyword: "a longer keyword", expected: true },
  ];

  for (let { keyword, expected } of tests) {
    await check_results({
      context: createContext(keyword, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: !expected
        ? []
        : [
            QuickSuggestTestUtils.ampResult({
              keyword,
              title: "Suggestion with 1-char keyword",
              url: "http://example.com/1-char-keyword",
              originalUrl: "http://example.com/1-char-keyword",
            }),
          ],
    });
  }
});

// AMP should be a top pick when `quicksuggest.ampTopPickCharThreshold` is
// non-zero and the query length meets the threshold; otherwise it should not be
// a top pick. It shouldn't matter whether the query is one of the suggestion's
// full keywords.
add_task(async function ampTopPickCharThreshold() {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  await QuickSuggestTestUtils.forceSync();

  UrlbarPrefs.set(
    "quicksuggest.ampTopPickCharThreshold",
    "amp full keywo".length
  );

  let tests = [
    // No top pick: Matches an AMP suggestion but the query is shorter than the
    // threshold.
    { keyword: "amp full key", amp: true, isTopPick: false },
    { keyword: "amp full keyw", amp: true, isTopPick: false },
    { keyword: "                 amp full key", amp: true, isTopPick: false },
    { keyword: "                 amp full keyw", amp: true, isTopPick: false },

    // Top pick: Matches an AMP suggestion and the query meets the threshold.
    { keyword: "amp full keywo", amp: true, isTopPick: true },
    { keyword: "amp full keywor", amp: true, isTopPick: true },
    { keyword: "amp full keyword", amp: true, isTopPick: true },
    { keyword: "AmP FuLl KeYwOrD", amp: true, isTopPick: true },
    { keyword: "               amp full keywo", amp: true, isTopPick: true },
    { keyword: "               amp full keywor", amp: true, isTopPick: true },
    { keyword: "               amp full keyword", amp: true, isTopPick: true },
    { keyword: "               AmP FuLl KeYwOrD", amp: true, isTopPick: true },

    // No top pick: Matches an AMP suggestion but the query is shorter than the
    // threshold. It doesn't matter that the query is equal to the suggestion's
    // full keyword.
    { keyword: "xyz", fullKeyword: "xyz", amp: true, isTopPick: false },
    { keyword: "XyZ", fullKeyword: "xyz", amp: true, isTopPick: false },
    {
      keyword: "                            xyz",
      fullKeyword: "xyz",
      amp: true,
      isTopPick: false,
    },
    {
      keyword: "                            XyZ",
      fullKeyword: "xyz",
      amp: true,
      isTopPick: false,
    },

    // No top pick: Matches a Wikipedia suggestion and some queries meet the
    // threshold, but Wikipedia should not be top pick.
    { keyword: "wikipedia full key", isTopPick: false },
    { keyword: "wikipedia full keyw", isTopPick: false },
    { keyword: "wikipedia full keywo", isTopPick: false },
    { keyword: "wikipedia full keywor", isTopPick: false },
    { keyword: "wikipedia full keyword", isTopPick: false },

    // No match: These shouldn't match anything at all since they have extra
    // spaces at the end, but they're included for completeness.
    { keyword: "                 amp full key   ", noMatch: true },
    { keyword: "                 amp full keyw   ", noMatch: true },
    { keyword: "               amp full keywo   ", noMatch: true },
    { keyword: "               amp full keywor   ", noMatch: true },
    { keyword: "               amp full keyword   ", noMatch: true },
    { keyword: "               AmP FuLl KeYwOrD   ", noMatch: true },
    { keyword: "                            xyz   ", noMatch: true },
    { keyword: "                            XyZ   ", noMatch: true },
  ];

  for (let { keyword, fullKeyword, amp, isTopPick, noMatch } of tests) {
    fullKeyword ??= amp ? "amp full keyword" : "wikipedia full keyword";
    info(
      "Running subtest: " +
        JSON.stringify({ keyword, fullKeyword, amp, isTopPick })
    );

    let expectedResult;
    if (!noMatch) {
      if (!amp) {
        expectedResult = QuickSuggestTestUtils.wikipediaResult({
          keyword,
          fullKeyword,
          title: "Wikipedia suggestion with full keyword and prefix keywords",
          url: "https://example.com/wikipedia-full-keyword",
        });
      } else if (isTopPick) {
        expectedResult = QuickSuggestTestUtils.ampResult({
          keyword,
          fullKeyword,
          title: "AMP suggestion with full keyword and prefix keywords",
          url: "https://example.com/amp-full-keyword",
          suggestedIndex: 1,
          isSuggestedIndexRelativeToGroup: false,
          isBestMatch: true,
          descriptionL10n: null,
        });
      } else {
        expectedResult = QuickSuggestTestUtils.ampResult({
          keyword,
          fullKeyword,
          title: "AMP suggestion with full keyword and prefix keywords",
          url: "https://example.com/amp-full-keyword",
        });
      }
    }

    await check_results({
      context: createContext(keyword, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expectedResult ? [expectedResult] : [],
    });
  }

  UrlbarPrefs.clear("quicksuggest.ampTopPickCharThreshold");
});

// AMP should not be shown as a top pick when the threshold is zero.
add_task(async function ampTopPickCharThreshold_zero() {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  await QuickSuggestTestUtils.forceSync();

  UrlbarPrefs.set("quicksuggest.ampTopPickCharThreshold", 0);

  let tests = [
    { keyword: "amp full key", amp: true },
    { keyword: "amp full keyw", amp: true },
    { keyword: "amp full keywo", amp: true },
    { keyword: "amp full keywor", amp: true },
    { keyword: "amp full keyword", amp: true },
    { keyword: "AmP FuLl KeYwOrD", amp: true },
    { keyword: "xyz", fullKeyword: "xyz", amp: true },
    { keyword: "XyZ", fullKeyword: "xyz", amp: true },
    { keyword: "wikipedia full key" },
    { keyword: "wikipedia full keyw" },
    { keyword: "wikipedia full keywo" },
    { keyword: "wikipedia full keywor" },
    { keyword: "wikipedia full keyword" },
  ];

  for (let { keyword, fullKeyword, amp } of tests) {
    fullKeyword ??= amp ? "amp full keyword" : "wikipedia full keyword";
    info("Running subtest: " + JSON.stringify({ keyword, fullKeyword, amp }));

    let expectedResult;
    if (!amp) {
      expectedResult = QuickSuggestTestUtils.wikipediaResult({
        keyword,
        fullKeyword,
        title: "Wikipedia suggestion with full keyword and prefix keywords",
        url: "https://example.com/wikipedia-full-keyword",
      });
    } else {
      expectedResult = QuickSuggestTestUtils.ampResult({
        keyword,
        fullKeyword,
        title: "AMP suggestion with full keyword and prefix keywords",
        url: "https://example.com/amp-full-keyword",
      });
    }

    await check_results({
      context: createContext(keyword, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [expectedResult],
    });
  }

  UrlbarPrefs.clear("quicksuggest.ampTopPickCharThreshold");
});

// Tests `ampMatchingStrategy`.
add_task(async function ampMatchingStrategy() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  await QuickSuggestTestUtils.forceSync();

  // Test each strategy in `AmpMatchingStrategy`. There are only a few.
  for (let [key, value] of Object.entries(AmpMatchingStrategy)) {
    await doAmpMatchingStrategyTest({ key, value });

    // Reset back to the default strategy just to make sure that works.
    await doAmpMatchingStrategyTest({
      key: "(default)",
      value: 0,
    });
  }

  // Test an invalid strategy integer value. The default strategy should
  // actually be used. First we need to set a valid non-default strategy.
  await doAmpMatchingStrategyTest({
    key: "FTS_AGAINST_TITLE",
    value: AmpMatchingStrategy.FTS_AGAINST_TITLE,
  });
  await doAmpMatchingStrategyTest({
    key: "(invalid)",
    value: 99,
    expectedStrategy: 0,
  });

  Services.prefs.clearUserPref(
    "browser.urlbar.quicksuggest.ampMatchingStrategy"
  );
  await QuickSuggestTestUtils.forceSync();
});

async function doAmpMatchingStrategyTest({
  key,
  value,
  expectedStrategy = value,
}) {
  info("Doing ampMatchingStrategy test: " + JSON.stringify({ key, value }));

  let sandbox = sinon.createSandbox();
  let ingestSpy = sandbox.spy(QuickSuggest.rustBackend._test_store, "ingest");

  // Set the strategy. It should trigger ingest. (Assuming it's different from
  // the current strategy. If it's not, ingest won't happen.)
  Services.prefs.setIntPref(
    "browser.urlbar.quicksuggest.ampMatchingStrategy",
    value
  );

  let ingestCall = await TestUtils.waitForCondition(() => {
    return ingestSpy.getCalls().find(call => {
      let ingestConstraints = call.args[0];
      return ingestConstraints?.providers[0] == SuggestionProvider.AMP;
    });
  }, "Waiting for ingest() to be called with Amp provider");

  // Check the provider constraints in the ingest constraints.
  let { providerConstraints } = ingestCall.args[0];
  if (!expectedStrategy) {
    Assert.ok(
      !providerConstraints,
      "ingest() should not have been called with provider constraints"
    );
  } else {
    Assert.ok(
      providerConstraints,
      "ingest() should have been called with provider constraints"
    );
    Assert.strictEqual(
      providerConstraints.ampAlternativeMatching,
      expectedStrategy,
      "ampAlternativeMatching should have been set"
    );
  }

  // Now do a query to make sure it also uses the correct provider constraints.
  // No need to use `check_results()`. We only need to trigger a query, and
  // checking the right results unnecessarily complicates things.
  let querySpy = sandbox.spy(
    QuickSuggest.rustBackend._test_store,
    "queryWithMetrics"
  );

  let controller = UrlbarTestUtils.newMockController();
  await controller.startQuery(
    createContext(SPONSORED_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    })
  );

  let queryCalls = querySpy.getCalls();
  Assert.equal(queryCalls.length, 1, "query() should have been called once");

  let query = queryCalls[0].args[0];
  Assert.ok(query, "query() should have been called with a query object");
  Assert.ok(
    query.providerConstraints,
    "query() should have been called with provider constraints"
  );

  if (!expectedStrategy) {
    Assert.strictEqual(
      query.providerConstraints.ampAlternativeMatching,
      null,
      "ampAlternativeMatching should not have been set on query provider constraints"
    );
  } else {
    Assert.strictEqual(
      query.providerConstraints.ampAlternativeMatching,
      expectedStrategy,
      "ampAlternativeMatching should have been set on query provider constraints"
    );
  }

  sandbox.restore();
}

async function resetRemoteSettingsData(data = REMOTE_SETTINGS_RESULTS) {
  let isAmp = suggestion => suggestion.iab_category == "22 - Shopping";
  await QuickSuggestTestUtils.setRemoteSettingsRecords([
    {
      collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
      type: QuickSuggestTestUtils.RS_TYPE.AMP,
      attachment: data.filter(isAmp),
    },
    {
      collection: QuickSuggestTestUtils.RS_COLLECTION.OTHER,
      type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
      attachment: data.filter(s => !isAmp(s)),
    },
  ]);
}
