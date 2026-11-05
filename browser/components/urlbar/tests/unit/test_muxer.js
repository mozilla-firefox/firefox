/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

let sandbox;

add_setup(async function () {
  sandbox = lazy.sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function test_muxer() {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  Assert.throws(
    () => providersManager.registerMuxer(),
    /invalid muxer/,
    "Should throw with no arguments"
  );
  Assert.throws(
    () => providersManager.registerMuxer({}),
    /invalid muxer/,
    "Should throw with empty object"
  );
  Assert.throws(
    () =>
      providersManager.registerMuxer({
        name: "",
      }),
    /invalid muxer/,
    "Should throw with empty name"
  );
  Assert.throws(
    () =>
      providersManager.registerMuxer({
        name: "test",
        sort: "no",
      }),
    /invalid muxer/,
    "Should throw with invalid sort"
  );

  let matches = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.TAB_SWITCH,
      source: UrlbarShared.RESULT_SOURCE.TABS,
      payload: { url: "http://mozilla.org/tab/" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
      payload: { url: "http://mozilla.org/bookmark/" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/history/" },
    }),
  ];

  let provider = registerBasicTestProvider(matches);
  let context = createContext(undefined, { providers: [provider.name] });
  let controller = UrlbarTestUtils.mockChildController();
  /**
   * A test muxer.
   */
  class TestMuxer extends UrlbarMuxer {
    get name() {
      return "TestMuxer";
    }
    sort(queryContext, unsortedResults) {
      queryContext.results = [...unsortedResults].sort((a, b) => {
        if (b.source == UrlbarShared.RESULT_SOURCE.TABS) {
          return -1;
        }
        if (b.source == UrlbarShared.RESULT_SOURCE.BOOKMARKS) {
          return 1;
        }
        return a.source == UrlbarShared.RESULT_SOURCE.BOOKMARKS ? -1 : 1;
      });
    }
  }
  let muxer = new TestMuxer();

  providersManager.registerMuxer(muxer);
  context.muxer = "TestMuxer";

  info("Check results, the order should be: bookmark, history, tab");
  await providersManager.startQuery(context, controller.parentController);
  Assert.deepEqual(context.results, [matches[1], matches[2], matches[0]]);

  // Sanity check, should not throw.
  providersManager.unregisterMuxer(muxer);
  providersManager.unregisterMuxer("TestMuxer"); // no-op.
});

add_task(async function test_preselectedHeuristic_singleProvider() {
  let matches = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/a" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      heuristic: true,
      payload: { url: "http://mozilla.org/b" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/c" },
    }),
  ];

  let provider = registerBasicTestProvider(matches);
  let context = createContext(undefined, {
    providers: [provider.name],
  });

  info("Check results, the order should be: b (heuristic), a, c");
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.deepEqual(context.results, [matches[1], matches[0], matches[2]]);
});

add_task(async function test_preselectedHeuristic_multiProviders() {
  let matches1 = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/a" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/b" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/c" },
    }),
  ];

  let matches2 = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/d" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      heuristic: true,
      payload: { url: "http://mozilla.org/e" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/f" },
    }),
  ];

  let provider1 = registerBasicTestProvider(matches1);
  let provider2 = registerBasicTestProvider(matches2);

  let context = createContext(undefined, {
    providers: [provider1.name, provider2.name],
  });

  info("Check results, the order should be: e (heuristic), a, b, c, d, f");
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.deepEqual(context.results, [
    matches2[1],
    ...matches1,
    matches2[0],
    matches2[2],
  ]);
});

add_task(async function test_suggestions() {
  Services.prefs.setIntPref("browser.urlbar.maxHistoricalSearchSuggestions", 1);

  let matches = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/a" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/b" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.SEARCH,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: {
        engine: "mozSearch",
        query: "moz",
        suggestion: "mozzarella",
        lowerCaseSuggestion: "mozzarella",
      },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.SEARCH,
      source: UrlbarShared.RESULT_SOURCE.SEARCH,
      payload: {
        engine: "mozSearch",
        query: "moz",
        suggestion: "mozilla",
        lowerCaseSuggestion: "mozilla",
      },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.SEARCH,
      source: UrlbarShared.RESULT_SOURCE.SEARCH,
      payload: {
        engine: "mozSearch",
        query: "moz",
        providesSearchMode: true,
        keyword: "@moz",
      },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://mozilla.org/c" },
    }),
  ];

  let provider = registerBasicTestProvider(matches);

  let context = createContext(undefined, {
    providers: [provider.name],
  });

  info("Check results, the order should be: mozzarella, moz, a, b, @moz, c");
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.deepEqual(context.results, [
    matches[2],
    matches[3],
    matches[0],
    matches[1],
    matches[4],
    matches[5],
  ]);

  Services.prefs.clearUserPref("browser.urlbar.maxHistoricalSearchSuggestions");
});

add_task(async function test_deduplicate_for_unitConversion() {
  const searchSuggestion = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.SEARCH,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    payload: {
      engine: "Google",
      query: "10cm to m",
      suggestion: "= 0.1 meters",
    },
  });
  const searchProvider = registerBasicTestProvider(
    [searchSuggestion],
    null,
    UrlbarShared.PROVIDER_TYPE.PROFILE
  );

  const unitConversionSuggestion = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    suggestedIndex: 1,
    payload: {
      dynamicType: "unitConversion",
      output: "0.1 m",
      input: "10cm to m",
    },
  });

  const unitConversion = registerBasicTestProvider(
    [unitConversionSuggestion],
    null,
    UrlbarShared.PROVIDER_TYPE.PROFILE,
    "UrlbarProviderUnitConversion"
  );

  const context = createContext(undefined, {
    providers: [searchProvider.name, unitConversion.name],
  });
  const controller = UrlbarTestUtils.mockChildController();
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    controller.parentController
  );
  Assert.deepEqual(context.results, [unitConversionSuggestion]);
});

// These results are used in the badHeuristicGroups tests below.  The order of
// the results in the array isn't important because they all get added at the
// same time.  It's the resultGroups in each test that is important.
const BAD_HEURISTIC_RESULTS = [
  // heuristic
  new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    heuristic: true,
    payload: { url: "http://mozilla.org/heuristic-0" },
  }),
  // heuristic
  new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    heuristic: true,
    payload: { url: "http://mozilla.org/heuristic-1" },
  }),
  // non-heuristic
  new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: { url: "http://mozilla.org/non-heuristic-0" },
  }),
  // non-heuristic
  new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: { url: "http://mozilla.org/non-heuristic-1" },
  }),
];

const BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC = BAD_HEURISTIC_RESULTS[0];
const BAD_HEURISTIC_RESULTS_GENERAL = [
  BAD_HEURISTIC_RESULTS[2],
  BAD_HEURISTIC_RESULTS[3],
];

add_task(async function test_badHeuristicGroups_multiple_0() {
  await doBadHeuristicGroupsTest(
    [
      // 2 heuristics with child groups
      {
        maxResultCount: 2,
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_1() {
  await doBadHeuristicGroupsTest(
    [
      // infinite heuristics with child groups
      {
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_2() {
  await doBadHeuristicGroupsTest(
    [
      // 2 heuristics
      {
        maxResultCount: 2,
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_3() {
  await doBadHeuristicGroupsTest(
    [
      // infinite heuristics
      {
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_4() {
  await doBadHeuristicGroupsTest(
    [
      // 1 heuristic with child groups
      {
        maxResultCount: 1,
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // 1 heuristic with child groups
      {
        maxResultCount: 1,
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_5() {
  await doBadHeuristicGroupsTest(
    [
      // infinite heuristics with child groups
      {
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // infinite heuristics with child groups
      {
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_6() {
  await doBadHeuristicGroupsTest(
    [
      // 1 heuristic
      {
        maxResultCount: 1,
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // 1 heuristic
      {
        maxResultCount: 1,
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicGroups_multiple_7() {
  await doBadHeuristicGroupsTest(
    [
      // infinite heuristics
      {
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
      // infinite general
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // infinite heuristics
      {
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
    ],
    [BAD_HEURISTIC_RESULTS_FIRST_HEURISTIC, ...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicsGroups_notFirst_0() {
  await doBadHeuristicGroupsTest(
    [
      // infinite general first
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // 1 heuristic with child groups second
      {
        maxResultCount: 1,
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
    ],
    [...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicsGroups_notFirst_1() {
  await doBadHeuristicGroupsTest(
    [
      // infinite general first
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // infinite heuristics with child groups second
      {
        children: [{ group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST }],
      },
    ],
    [...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicsGroups_notFirst_2() {
  await doBadHeuristicGroupsTest(
    [
      // infinite general first
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // 1 heuristic second
      {
        maxResultCount: 1,
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
    ],
    [...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicsGroups_notFirst_3() {
  await doBadHeuristicGroupsTest(
    [
      // infinite general first
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // infinite heuristics second
      {
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
    ],
    [...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

add_task(async function test_badHeuristicsGroups_notFirst_4() {
  await doBadHeuristicGroupsTest(
    [
      // 1 general first
      {
        maxResultCount: 1,
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
      // infinite heuristics second
      {
        group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST,
      },
      // infinite general third
      {
        group: UrlbarShared.RESULT_GROUP.GENERAL,
      },
    ],
    [...BAD_HEURISTIC_RESULTS_GENERAL]
  );
});

/**
 * Sets the resultGroups pref, performs a search, and then checks the results.
 * Regardless of the groups, the muxer should include at most one heuristic in
 * its results and it should always be the first result.
 *
 * @param {Array} resultGroups
 *   The result groups.
 * @param {Array} expectedResults
 *   The expected results.
 */
async function doBadHeuristicGroupsTest(resultGroups, expectedResults) {
  sandbox
    .stub(UrlbarPrefs, "getResultGroups")
    .returns({ children: resultGroups });

  let provider = registerBasicTestProvider(BAD_HEURISTIC_RESULTS);
  let context = createContext("foo", { providers: [provider.name] });
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.deepEqual(context.results, expectedResults);

  sandbox.restore();
}

// When `maxRichResults` is positive and taken up by suggested-index result(s),
// both the heuristic and suggested-index results should be included because we
// (a) make room for the heuristic and (b) assume all suggested-index results
// should be included even if it means exceeding `maxRichResults`. The specified
// `maxRichResults` span will be exceeded in this case.
add_task(async function roomForHeuristic_suggestedIndex() {
  let results = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      heuristic: true,
      payload: { url: "http://example.com/heuristic" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      suggestedIndex: 1,
      payload: { url: "http://example.com/suggestedIndex" },
    }),
  ];

  UrlbarPrefs.set("maxRichResults", 1);

  let provider = registerBasicTestProvider(results);
  let context = createContext(undefined, { providers: [provider.name] });
  await check_results({
    context,
    matches: results,
  });

  UrlbarPrefs.clear("maxRichResults");
});

// When `maxRichResults` is positive but less than the heuristic's result span,
// the heuristic should be included because we make room for it even if it means
// exceeding `maxRichResults`. The specified `maxRichResults` span will be
// exceeded in this case.
add_task(async function roomForHeuristic_largeResultSpan() {
  let results = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      heuristic: true,
      resultSpan: 2,
      payload: { url: "http://example.com/heuristic" },
    }),
  ];

  UrlbarPrefs.set("maxRichResults", 1);

  let provider = registerBasicTestProvider(results);
  let context = createContext(undefined, { providers: [provider.name] });
  await check_results({
    context,
    matches: results,
  });

  UrlbarPrefs.clear("maxRichResults");
});

// When `maxRichResults` is zero and there are no suggested-index results, the
// heuristic should not be included.
add_task(async function roomForHeuristic_maxRichResultsZero() {
  let results = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      heuristic: true,
      payload: { url: "http://example.com/heuristic" },
    }),
  ];

  UrlbarPrefs.set("maxRichResults", 0);

  let provider = registerBasicTestProvider(results);
  let context = createContext(undefined, { providers: [provider.name] });
  await check_results({
    context,
    matches: [],
  });

  UrlbarPrefs.clear("maxRichResults");
});

// When `maxRichResults` is zero and suggested-index results are present,
// neither the heuristic nor the suggested-index results should be included.
add_task(async function roomForHeuristic_maxRichResultsZero_suggestedIndex() {
  let results = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      heuristic: true,
      payload: { url: "http://example.com/heuristic" },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      suggestedIndex: 1,
      payload: { url: "http://example.com/suggestedIndex" },
    }),
  ];

  UrlbarPrefs.set("maxRichResults", 0);

  let provider = registerBasicTestProvider(results);
  let context = createContext(undefined, { providers: [provider.name] });
  await check_results({
    context,
    matches: [],
  });

  UrlbarPrefs.clear("maxRichResults");
});

add_task(async function test_orderBy() {
  // The GENERAL groups has an orderBy property, so let's just add to history.
  let results1 = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://example.com/test1", frecency: 10 },
    }),
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://example.com/test2", frecency: 1000 },
    }),
  ];
  let provider1 = registerBasicTestProvider(results1);
  let results2 = [
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url: "http://example.com/test3", frecency: 100 },
    }),
  ];
  let provider2 = registerBasicTestProvider(results2);

  let context = createContext(undefined, {
    providers: [provider1.name, provider2.name],
  });
  await check_results({
    context,
    matches: [
      results1[1], // 1000
      results2[0], // 100
      results1[0], // 10
    ],
  });
});

// A result returned by both the semantic history provider and a non-semantic
// provider for the same page should be deduped down to the non-semantic
// result, regardless of which provider's result is added first and regardless
// of scheme/www differences or result type (URL vs switch-to-tab).
async function checkSemanticDedupe({
  semanticFirst,
  semanticUrl = "https://example.com/foo/",
  nonSemanticUrl = "https://example.com/foo/",
  semanticType = UrlbarShared.RESULT_TYPE.URL,
  nonSemanticType = UrlbarShared.RESULT_TYPE.URL,
  description = "",
}) {
  let makeMatch = (type, url) =>
    new UrlbarResult({
      type,
      source:
        type == UrlbarShared.RESULT_TYPE.TAB_SWITCH
          ? UrlbarShared.RESULT_SOURCE.TABS
          : UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url, title: "Example Page" },
    });
  let semanticMatch = makeMatch(semanticType, semanticUrl);
  let nonSemanticMatch = makeMatch(nonSemanticType, nonSemanticUrl);

  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  // `unregisterProvider` matches by name, so clear any strays left under the
  // semantic name by a previous run for a clean slate.
  for (let p of [...providersManager.providers]) {
    if (p.name == "UrlbarProviderSemanticHistorySearch") {
      providersManager.unregisterProvider(p);
    }
  }

  // Register the providers in the order under test so the matching result is
  // processed first by the muxer.
  let semanticProvider, nonSemanticProvider;
  let registerSemantic = () =>
    (semanticProvider = registerBasicTestProvider(
      [semanticMatch],
      undefined,
      undefined,
      "UrlbarProviderSemanticHistorySearch"
    ));
  let registerNonSemantic = () =>
    (nonSemanticProvider = registerBasicTestProvider([nonSemanticMatch]));
  if (semanticFirst) {
    registerSemantic();
    registerNonSemantic();
  } else {
    registerNonSemantic();
    registerSemantic();
  }

  let context = createContext(undefined, {
    providers: semanticFirst
      ? [semanticProvider.name, nonSemanticProvider.name]
      : [nonSemanticProvider.name, semanticProvider.name],
  });
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );

  Assert.deepEqual(
    context.results,
    [nonSemanticMatch],
    `Only the non-semantic result should survive (${description}semanticFirst=${semanticFirst})`
  );

  providersManager.unregisterProvider(semanticProvider);
  providersManager.unregisterProvider(nonSemanticProvider);
}

add_task(async function test_dedupe_semantic_seen_first() {
  await checkSemanticDedupe({ semanticFirst: true });
});

add_task(async function test_dedupe_nonSemantic_seen_first() {
  await checkSemanticDedupe({ semanticFirst: false });
});

// The semantic and non-semantic results may have the same stripped URL but
// different schemes (http vs https). The non-semantic result should still win.
add_task(async function test_dedupe_semantic_scheme_mismatch() {
  for (let semanticFirst of [true, false]) {
    await checkSemanticDedupe({
      semanticFirst,
      semanticUrl: "https://example.com/foo/",
      nonSemanticUrl: "http://example.com/foo/",
      description: "scheme mismatch, ",
    });
  }
});

// Same as above but the prefixes differ by www. The non-semantic result wins.
add_task(async function test_dedupe_semantic_www_mismatch() {
  for (let semanticFirst of [true, false]) {
    await checkSemanticDedupe({
      semanticFirst,
      semanticUrl: "https://www.example.com/foo/",
      nonSemanticUrl: "https://example.com/foo/",
      description: "www mismatch, ",
    });
  }
});

// A semantic switch-to-tab result that dupes a non-semantic switch-to-tab
// result for the same open tab should be deduped to the non-semantic one.
add_task(async function test_dedupe_semantic_tab_switch() {
  for (let semanticFirst of [true, false]) {
    await checkSemanticDedupe({
      semanticFirst,
      semanticUrl: "https://example.com/tab/",
      nonSemanticUrl: "https://example.com/tab/",
      semanticType: UrlbarShared.RESULT_TYPE.TAB_SWITCH,
      nonSemanticType: UrlbarShared.RESULT_TYPE.TAB_SWITCH,
      description: "tab switch, ",
    });
  }
});

// A semantic result with no non-semantic counterpart must not be suppressed,
// whether it's a plain URL or a switch-to-tab result.
add_task(async function test_semantic_only_survives() {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  for (let p of [...providersManager.providers]) {
    if (p.name == "UrlbarProviderSemanticHistorySearch") {
      providersManager.unregisterProvider(p);
    }
  }

  let urlMatch = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: { url: "https://semantic-only.example.com/page/", title: "Page" },
  });
  let tabMatch = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.TAB_SWITCH,
    source: UrlbarShared.RESULT_SOURCE.TABS,
    payload: { url: "https://semantic-only.example.com/tab/", title: "Tab" },
  });
  let semanticProvider = registerBasicTestProvider(
    [tabMatch, urlMatch],
    undefined,
    undefined,
    "UrlbarProviderSemanticHistorySearch"
  );

  let context = createContext(undefined, {
    providers: [semanticProvider.name],
  });
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );

  Assert.deepEqual(
    context.results.map(r => r.payload.url).sort(),
    [tabMatch.payload.url, urlMatch.payload.url].sort(),
    "Semantic results with no non-semantic counterpart are kept"
  );

  providersManager.unregisterProvider(semanticProvider);
});

// Two semantic results that are prefix variants of the same page (and have no
// non-semantic counterpart) should still be deduped to a single result by the
// normal prefix-rank logic, since only suppressed semantic dupes are excluded
// from that bookkeeping.
add_task(async function test_dedupe_two_semantic_prefixes() {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  for (let p of [...providersManager.providers]) {
    if (p.name == "UrlbarProviderSemanticHistorySearch") {
      providersManager.unregisterProvider(p);
    }
  }

  let httpsMatch = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: { url: "https://example.com/foo/", title: "Example Page" },
  });
  let httpMatch = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: { url: "http://example.com/foo/", title: "Example Page" },
  });
  let semanticProvider = registerBasicTestProvider(
    [httpsMatch, httpMatch],
    undefined,
    undefined,
    "UrlbarProviderSemanticHistorySearch"
  );

  let context = createContext(undefined, {
    providers: [semanticProvider.name],
  });
  await ProvidersManager.getInstanceForSap("urlbar").startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );

  Assert.deepEqual(
    context.results,
    [httpsMatch],
    "Only the highest-prefix-rank semantic variant should survive"
  );

  providersManager.unregisterProvider(semanticProvider);
});

// When the separateGroup setting is enabled with semantic seearch, semantic results
// share the general area with non-semantic history at a 9:1 ratio: at most ~1 in every
// 10 of these slots is semantic, semantic results are ordered by frecency, and
// they fill the remaining space (without evicting) when there's little other
// history.
//
// The ratio 9:1 ratio enables one sementic result to be visible in the history view
// when there are many semantic results.

add_task(async function test_semantic_history_separate_group_ratio() {
  // The separate semantic group only exists when this pref is enabled.
  UrlbarPrefs.set("suggest.semanticHistory.separateGroup", true);

  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  // `unregisterProvider` matches by name, and earlier tests register providers
  // under the same semantic name, so clear any strays for a clean slate.
  for (let p of [...providersManager.providers]) {
    if (p.name == "UrlbarProviderSemanticHistorySearch") {
      providersManager.unregisterProvider(p);
    }
  }

  let makeUrlResult = (url, frecency) =>
    new UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.HISTORY,
      payload: { url, title: "Page", frecency },
    });
  let isSemantic = r => r.providerName == "UrlbarProviderSemanticHistorySearch";

  // 12 general results, frecency descending, distinct URLs so nothing dedupes.
  let exactMatches = Array.from({ length: 12 }, (_, i) =>
    makeUrlResult(`https://example.com/exact${i}`, 120 - i * 10)
  );
  let semanticMatches = [
    makeUrlResult("https://example.com/semantic1", 50),
    makeUrlResult("https://example.com/semantic2", 200),
    makeUrlResult("https://example.com/semantic3", 100),
  ];

  let exactProvider = registerBasicTestProvider(exactMatches);
  let semanticProvider = registerBasicTestProvider(
    semanticMatches,
    undefined,
    undefined,
    "UrlbarProviderSemanticHistorySearch"
  );

  // Typical search results with competing search engine resuls. Only room for 3 history related
  // results, so semantic results are supprressed to keep from evicting an exact match result
  let limitedResultsContext = createContext(undefined, {
    providers: [exactProvider.name, semanticProvider.name],
    maxResults: 3,
  });
  await providersManager.startQuery(
    limitedResultsContext,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.equal(limitedResultsContext.results.length, 3, "Fills the budget");
  Assert.equal(
    limitedResultsContext.results.filter(isSemantic).length,
    0,
    "At no semantic results are shown"
  );
  // History view with with lots of of competing history: the general area holds 10,
  //  so the 9:1 split yields 9 general + 1 semantic.
  let historyViewContext = createContext(undefined, {
    providers: [exactProvider.name, semanticProvider.name],
    maxResults: 10,
  });
  await providersManager.startQuery(
    historyViewContext,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.equal(historyViewContext.results.length, 10, "Fills the budget");
  Assert.equal(
    historyViewContext.results.filter(isSemantic).length,
    1,
    "At most 1 in 10 results is semantic"
  );
  Assert.deepEqual(
    historyViewContext.results.slice(0, 9),
    exactMatches.slice(0, 9),
    "The 9 highest-frecency general results are kept, ordered by frecency"
  );
  Assert.equal(
    historyViewContext.results[9],
    semanticMatches[1],
    "The shown semantic result is the highest-frecency one (sorted by frecency)"
  );

  // Little competing history: semantic fills the leftover space without
  // evicting the general results.
  let fewExactProvider = registerBasicTestProvider(exactMatches.slice(0, 2));
  let sparseContext = createContext(undefined, {
    providers: [fewExactProvider.name, semanticProvider.name],
    maxResults: 10,
  });
  await providersManager.startQuery(
    sparseContext,
    UrlbarTestUtils.mockChildController().parentController
  );
  Assert.equal(
    sparseContext.results.filter(r => !isSemantic(r)).length,
    2,
    "Both general results are kept (not evicted)"
  );
  Assert.equal(
    sparseContext.results.filter(isSemantic).length,
    3,
    "Semantic results fill the leftover space"
  );
  Assert.deepEqual(
    sparseContext.results.filter(isSemantic),
    [semanticMatches[1], semanticMatches[2], semanticMatches[0]],
    "Multiple shown semantic results are ordered by frecency (descending)"
  );

  providersManager.unregisterProvider(exactProvider);
  providersManager.unregisterProvider(fewExactProvider);
  providersManager.unregisterProvider(semanticProvider);
  UrlbarPrefs.clear("suggest.semanticHistory.separateGroup");
});

/**
 * Does a zero prefix query with history results and, optionally, 3 temporal
 * search suggestions. The default of 15 history results is more than any max
 * result count the tests expect, so the number of results is limited by
 * `maxResultsActual` and not by the number of available results. Any number of
 * temporal suggestions makes the muxer use `temporal.zeroPrefixMaxRichResults`.
 * 3 matches the temporal row count of the 7:3:3 zero prefix layout.
 *
 * @param {object} options
 * @param {boolean} options.temporal
 *   Whether temporal search suggestions are included.
 * @param {number} [options.historyCount]
 *   The number of history results.
 * @returns {Promise<UrlbarQueryContext>}
 */
async function doMaxResultsActualQuery({ temporal, historyCount = 15 }) {
  let results = Array.from(
    { length: historyCount },
    (_, i) =>
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.URL,
        source: UrlbarShared.RESULT_SOURCE.HISTORY,
        payload: { url: `https://example.com/${i}`, title: "Page" },
      })
  );
  if (temporal) {
    for (let query of ["q1", "q2", "q3"]) {
      results.push(
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.SEARCH,
          source: UrlbarShared.RESULT_SOURCE.SEARCH,
          group: UrlbarShared.RESULT_GROUP.TEMPORAL_SUGGESTION,
          payload: {
            engine: "Test",
            suggestion: query,
            lowerCaseSuggestion: query,
            title: query,
          },
        })
      );
    }
  }

  let provider = registerBasicTestProvider(results);
  let context = createContext("", { providers: [provider.name] });
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  await providersManager.startQuery(
    context,
    UrlbarTestUtils.mockChildController().parentController
  );
  providersManager.unregisterProvider(provider);
  return context;
}

// Without temporal suggestions, maxResultsActual is maxResults.
add_task(async function test_maxResultsActual_noTemporalSuggestions() {
  let context = await doMaxResultsActualQuery({ temporal: false });
  Assert.equal(context.maxResultsActual, 10);
  Assert.equal(context.results.length, 10);
});

// With temporal suggestions, temporal.zeroPrefixMaxRichResults is used.
add_task(async function test_maxResultsActual_temporalSuggestions() {
  let context = await doMaxResultsActualQuery({ temporal: true });
  Assert.equal(context.maxResultsActual, 13);
  Assert.equal(context.results.length, 13);
});

// maxResultsActual follows a modified temporal.zeroPrefixMaxRichResults.
add_task(async function test_maxResultsActual_zeroPrefixMaxRichResults() {
  UrlbarPrefs.set("temporal.zeroPrefixMaxRichResults", 8);
  let context = await doMaxResultsActualQuery({ temporal: true });
  Assert.equal(context.maxResultsActual, 8);
  Assert.equal(context.results.length, 8);
  UrlbarPrefs.clear("temporal.zeroPrefixMaxRichResults");
});

// A user-set maxRichResults takes precedence over temporal suggestions.
add_task(async function test_maxResultsActual_userMaxRichResults() {
  UrlbarPrefs.set("maxRichResults", 5);
  let context = await doMaxResultsActualQuery({ temporal: true });
  Assert.equal(context.maxResultsActual, 5);
  Assert.equal(context.results.length, 5);
  UrlbarPrefs.clear("maxRichResults");
});

// maxResultsActual is a max, so there can be fewer results than it.
add_task(async function test_maxResultsActual_fewerResults() {
  let context = await doMaxResultsActualQuery({
    temporal: true,
    historyCount: 5,
  });
  Assert.equal(context.maxResultsActual, 13);
  Assert.equal(context.results.length, 8);
});
