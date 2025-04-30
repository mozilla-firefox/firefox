/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests Suggest exposure suggestions.

"use strict";

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "exposure-suggestions",
    suggestion_type: "aaa",
    attachment: {
      keywords: ["aaa keyword", "aaa bbb keyword", "wikipedia"],
    },
  },
  {
    type: "exposure-suggestions",
    suggestion_type: "bbb",
    attachment: {
      keywords: ["bbb keyword", "aaa bbb keyword", "wikipedia"],
    },
  },
  {
    type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
    attachment: [QuickSuggestTestUtils.wikipediaRemoteSettings()],
  },
];

add_setup(async function () {
  // Add many exposure and AMP suggestions that have the "maxresults" keyword.
  let maxResults = UrlbarPrefs.get("maxRichResults");
  Assert.greater(maxResults, 0, "This test expects maxRichResults to be > 0");
  let ampRecord = {
    collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
    type: QuickSuggestTestUtils.RS_TYPE.AMP,
    attachment: [],
  };
  REMOTE_SETTINGS_RECORDS.push(ampRecord);
  for (let i = 0; i < maxResults; i++) {
    ampRecord.attachment.push(
      QuickSuggestTestUtils.ampRemoteSettings({
        keywords: ["maxresults"],
        title: "maxresults " + i,
        url: "https://example.com/maxresults/" + i,
      })
    );
    REMOTE_SETTINGS_RECORDS.push({
      type: "exposure-suggestions",
      suggestion_type: "maxresults" + i,
      attachment: { keywords: ["maxresults"] },
    });
  }

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      ["quicksuggest.exposureSuggestionTypes", "aaa,bbb"],
      ["suggest.quicksuggest.nonsponsored", true],
      ["quicksuggest.ampTopPickCharThreshold", 0],
    ],
  });
});

add_task(async function telemetryType() {
  Assert.equal(
    QuickSuggest.getFeature("ExposureSuggestions").getSuggestionTelemetryType(),
    "exposure",
    "Telemetry type should be correct"
  );
});

// Results for enabled exposure suggestions should always be added and have
// hidden `exposureTelemetry` no matter the value of the `exposureResults` or
// `showExposureResults` prefs.
add_task(async function basic() {
  let queries = [
    {
      query: "no match",
      expected: [],
    },
    {
      query: "aaa keyword",
      expected: [makeExpectedResult("aaa")],
    },
    {
      query: "bbb keyword",
      expected: [makeExpectedResult("bbb")],
    },
    {
      query: "aaa bbb keyword",
      // The order of the results is reversed since they have the same suggested
      // index, due to how the muxer handles that.
      expected: [makeExpectedResult("bbb"), makeExpectedResult("aaa")],
    },
  ];

  let exposureResultsList = [
    undefined,
    "",
    "some_other_result",
    "exposure",
    "some_other_result,exposure",
  ];

  for (let exposureResults of exposureResultsList) {
    if (exposureResults === undefined) {
      UrlbarPrefs.clear("exposureResults");
    } else {
      UrlbarPrefs.set("exposureResults", exposureResults);
    }

    for (let showExposureResults of [undefined, true, false]) {
      if (showExposureResults === undefined) {
        UrlbarPrefs.clear("showExposureResults");
      } else {
        UrlbarPrefs.set("showExposureResults", showExposureResults);
      }

      info(
        "Doing basic subtest: " +
          JSON.stringify({
            exposureResults,
            showExposureResults,
          })
      );

      await doQueries(queries);
    }
  }

  UrlbarPrefs.clear("exposureResults");
  UrlbarPrefs.clear("showExposureResults");
});

// When only one exposure suggestion type is enabled, only its result should be
// returned. This task assumes multiples types were added to remote settings in
// the setup task.
add_task(async function oneSuggestionType() {
  await withExposureSuggestionTypesPref("bbb", async () => {
    await doQueries([
      {
        query: "aaa keyword",
        expected: [],
      },
      {
        query: "bbb keyword",
        expected: [makeExpectedResult("bbb")],
      },
      {
        query: "aaa bbb keyword",
        expected: [makeExpectedResult("bbb")],
      },
    ]);
  });
});

// When no exposure suggestion types are enabled, no results should be added.
add_task(async function disabled() {
  await withExposureSuggestionTypesPref("", async () => {
    await doQueries(
      ["aaa keyword", "bbb keyword", "aaa bbb keyword"].map(query => ({
        query,
        expected: [],
      }))
    );
  });
});

// When another visible suggestion also matches, both it and the exposure
// suggestion should be added.
add_task(async function otherVisibleSuggestionsAlsoMatched_1() {
  await withExposureSuggestionTypesPref("aaa", async () => {
    await doQueries([
      {
        query: "aaa keyword",
        expected: [makeExpectedResult("aaa")],
      },
      {
        query: "wikipedia",
        expected: [
          {
            ...QuickSuggestTestUtils.wikipediaResult(),
            exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.NONE,
          },
          makeExpectedResult("aaa"),
        ],
      },
    ]);
  });
});

// When another visible suggestion also matches, both it and all specified
// exposure suggestions should be added.
add_task(async function otherVisibleSuggestionsAlsoMatched_2() {
  await withExposureSuggestionTypesPref("aaa,bbb", async () => {
    await doQueries([
      {
        query: "aaa keyword",
        expected: [makeExpectedResult("aaa")],
      },
      {
        query: "bbb keyword",
        expected: [makeExpectedResult("bbb")],
      },
      {
        query: "aaa bbb keyword",
        expected: [makeExpectedResult("bbb"), makeExpectedResult("aaa")],
      },
      {
        query: "wikipedia",
        expected: [
          {
            ...QuickSuggestTestUtils.wikipediaResult(),
            exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.NONE,
          },
          makeExpectedResult("bbb"),
          makeExpectedResult("aaa"),
        ],
      },
    ]);
  });
});

// Tests with `maxResults` exposures. All should be added.
add_task(async function maxResults_exposuresOnly() {
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let exposureResults = [];
  for (let i = 0; i < maxResults; i++) {
    exposureResults.unshift(makeExpectedResult("maxresults" + i));
  }

  await doMaxResultsTest({
    expectedResults: [...exposureResults],
  });
});

// Tests with `maxResults` exposures and and `maxResults` history. All exposures
// and history should be added since exposures are hidden.
add_task(async function maxResults_exposuresHistory() {
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let exposureResults = [];
  let historyResults = [];
  for (let i = 0; i < maxResults; i++) {
    exposureResults.unshift(makeExpectedResult("maxresults" + i));
    historyResults.push(
      new UrlbarResult(
        UrlbarUtils.RESULT_TYPE.URL,
        UrlbarUtils.RESULT_SOURCE.HISTORY,
        { url: "http://example.com/history/" + i }
      )
    );
  }

  await doMaxResultsTest({
    includeHistory: true,
    expectedResults: [...historyResults, ...exposureResults],
  });
});

// Tests with `maxResults` exposures and and `maxResults` AMP suggestions. The
// first AMP result should be added since it's visible and only one visible
// Suggest result should be added. All exposures should be added since exposures
// are hidden.
add_task(async function maxResults_exposuresAmp() {
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let exposureResults = [];
  for (let i = 0; i < maxResults; i++) {
    exposureResults.unshift(makeExpectedResult("maxresults" + i));
  }

  await doMaxResultsTest({
    includeAmp: true,
    expectedResults: [
      QuickSuggestTestUtils.ampResult({
        keyword: "maxresults",
        title: "maxresults 0",
        url: "https://example.com/maxresults/0",
      }),
      ...exposureResults,
    ],
  });
});

// Tests with `maxResults` exposures, history, and AMP suggestions. The first
// AMP result should be added since it's visible and only one visible Suggest
// result should be added. `maxResults` - 1 history results should be added. All
// exposures should be added since exposures are hidden.
add_task(async function maxResults_exposuresHistoryAmp() {
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let exposureResults = [];
  let historyResults = [];
  for (let i = 0; i < maxResults; i++) {
    exposureResults.unshift(makeExpectedResult("maxresults" + i));
    historyResults.push(
      new UrlbarResult(
        UrlbarUtils.RESULT_TYPE.URL,
        UrlbarUtils.RESULT_SOURCE.HISTORY,
        { url: "http://example.com/history/" + i }
      )
    );
  }

  await doMaxResultsTest({
    includeAmp: true,
    includeHistory: true,
    expectedResults: [
      QuickSuggestTestUtils.ampResult({
        keyword: "maxresults",
        title: "maxresults 0",
        url: "https://example.com/maxresults/0",
      }),
      ...historyResults.slice(0, maxResults - 1),
      ...exposureResults,
    ],
  });
});

async function doMaxResultsTest({
  expectedResults,
  includeAmp = false,
  includeHistory = false,
}) {
  let maxResults = UrlbarPrefs.get("maxRichResults");
  let providerNames = [UrlbarProviderQuickSuggest.name];

  // If history results should be included, register a test provider that adds a
  // bunch of history results.
  let historyProvider;
  let historyResults = [];
  if (includeHistory) {
    for (let i = 0; i < maxResults; i++) {
      historyResults.push(
        new UrlbarResult(
          UrlbarUtils.RESULT_TYPE.URL,
          UrlbarUtils.RESULT_SOURCE.HISTORY,
          { url: "http://example.com/history/" + i }
        )
      );
    }
    historyProvider = new UrlbarTestUtils.TestProvider({
      results: historyResults,
    });
    UrlbarProvidersManager.registerProvider(historyProvider);
    providerNames.push(historyProvider.name);
  }

  // If AMP results should be included, enable sponsored suggestions.
  if (includeAmp) {
    UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
    await QuickSuggestTestUtils.forceSync();
  }

  // Generate the list of exposure suggestion types so we can trigger the
  // exposure suggestions.
  let exposureTypes = [];
  for (let i = 0; i < maxResults; i++) {
    exposureTypes.push("maxresults" + i);
  }

  await withExposureSuggestionTypesPref(exposureTypes.join(","), async () => {
    await check_results({
      context: createContext("maxresults", {
        providers: providerNames,
        isPrivate: false,
      }),
      matches: expectedResults,
    });
  });

  if (historyProvider) {
    UrlbarProvidersManager.unregisterProvider(historyProvider);
  }
  if (includeAmp) {
    UrlbarPrefs.clear("suggest.quicksuggest.sponsored");
    await QuickSuggestTestUtils.forceSync();
  }
}

// Exposure suggestions are neither sponsored nor nonsponsored, so they should
// be added even when sponsored and nonsponsored are disabled.
add_task(async function sponsoredAndNonsponsoredDisabled() {
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);

  await withExposureSuggestionTypesPref("aaa", async () => {
    await doQueries([
      {
        query: "aaa keyword",
        expected: [makeExpectedResult("aaa")],
      },
      {
        query: "aaa bbb keyword",
        expected: [makeExpectedResult("aaa")],
      },
      {
        query: "wikipedia",
        expected: [makeExpectedResult("aaa")],
      },
      {
        query: "doesn't match",
        expected: [],
      },
    ]);
  });

  UrlbarPrefs.clear("suggest.quicksuggest.sponsored");
  UrlbarPrefs.clear("suggest.quicksuggest.nonsponsored");
});

// Tests the `quickSuggestExposureSuggestionTypes` Nimbus variable.
add_task(async function nimbus() {
  let cleanup = await UrlbarTestUtils.initNimbusFeature({
    quickSuggestExposureSuggestionTypes: "aaa,bbb",
  });
  await QuickSuggestTestUtils.forceSync();
  await doQueries([
    {
      query: "aaa keyword",
      expected: [makeExpectedResult("aaa")],
    },
    {
      query: "aaa bbb keyword",
      expected: [makeExpectedResult("bbb"), makeExpectedResult("aaa")],
    },
    {
      query: "wikipedia",
      expected: [makeExpectedResult("bbb"), makeExpectedResult("aaa")],
    },
    {
      query: "doesn't match",
      expected: [],
    },
  ]);
  await cleanup();
});

async function doQueries(queries) {
  for (let { query, expected } of queries) {
    info(
      "Doing query: " +
        JSON.stringify({
          query,
          expected,
        })
    );

    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expected,
    });
  }
}

async function withExposureSuggestionTypesPref(prefValue, callback) {
  // Use `Services` to get the original pref value since `UrlbarPrefs` will
  // parse the string value into a `Set`.
  let originalPrefValue = Services.prefs.getCharPref(
    "browser.urlbar.quicksuggest.exposureSuggestionTypes"
  );

  // Changing the pref (or Nimbus variable) to a different value will trigger
  // ingest, so force sync afterward (or at least wait for ingest to finish).
  UrlbarPrefs.set("quicksuggest.exposureSuggestionTypes", prefValue);
  await QuickSuggestTestUtils.forceSync();

  await callback();

  UrlbarPrefs.set("quicksuggest.exposureSuggestionTypes", originalPrefValue);
  await QuickSuggestTestUtils.forceSync();
}

function makeExpectedResult(exposureSuggestionType) {
  return {
    type: UrlbarUtils.RESULT_TYPE.DYNAMIC,
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    heuristic: false,
    exposureTelemetry: UrlbarUtils.EXPOSURE_TELEMETRY.HIDDEN,
    payload: {
      exposureSuggestionType,
      source: "rust",
      dynamicType: "exposure",
      provider: "Exposure",
      telemetryType: "exposure",
      isSponsored: false,
    },
  };
}
