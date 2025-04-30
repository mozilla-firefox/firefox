/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests the configurable indexes of sponsored and non-sponsored ("Firefox
// Suggest") quick suggest results.

"use strict";

const SUGGESTIONS_FIRST_PREF = "browser.urlbar.showSearchSuggestionsFirst";
const SUGGESTIONS_PREF = "browser.urlbar.suggest.searches";

const TEST_ENGINE_BASENAME = "searchSuggestionEngine.xml";
const MAX_RESULTS = UrlbarPrefs.get("maxRichResults");

const SPONSORED_INDEX_PREF = "browser.urlbar.quicksuggest.sponsoredIndex";
const NON_SPONSORED_INDEX_PREF =
  "browser.urlbar.quicksuggest.nonSponsoredIndex";

const SPONSORED_SEARCH_STRING = "frabbits";
const NON_SPONSORED_SEARCH_STRING = "nonspon";

const TEST_URL = "http://example.com/quicksuggest";

const REMOTE_SETTINGS_RESULTS = [
  QuickSuggestTestUtils.ampRemoteSettings({
    keywords: [SPONSORED_SEARCH_STRING],
  }),
  QuickSuggestTestUtils.wikipediaRemoteSettings({
    keywords: [NON_SPONSORED_SEARCH_STRING],
  }),
];

// Trying to avoid timeouts.
requestLongerTimeout(3);

add_setup(async function () {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await UrlbarTestUtils.formHistory.clear();

  // Set default engine so we don't hit the network. It doesn't provide search
  // suggestions so the suggested index of sponsored suggestions should be -1.
  await SearchTestUtils.installSearchExtension({}, { setAsDefault: true });

  let isAmp = suggestion => suggestion.iab_category == "22 - Shopping";
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
        type: QuickSuggestTestUtils.RS_TYPE.AMP,
        attachment: REMOTE_SETTINGS_RESULTS.filter(isAmp),
      },
      {
        collection: QuickSuggestTestUtils.RS_COLLECTION.OTHER,
        type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
        attachment: REMOTE_SETTINGS_RESULTS.filter(s => !isAmp(s)),
      },
    ],
    prefs: [["quicksuggest.ampTopPickCharThreshold", 0]],
  });

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

// Tests with search suggestions first and an engine that returns suggestions
add_task(async function searchSuggestionsFirst_withSuggestions() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, true]],
  });
  await withSuggestions(async () => {
    await doTestPermutations(({ withHistory, generalIndex }) => ({
      // Since suggestions are shown first and the engine returns suggestions,
      // the sponsored suggested index can be 0.
      expectedSuggestedIndex: generalIndex,
      expectedResultCount: withHistory ? MAX_RESULTS : 4,
      expectedViewIndex:
        generalIndex == 0 || !withHistory ? 3 : MAX_RESULTS - 1,
    }));
  });
  await SpecialPowers.popPrefEnv();
});

// Tests with search suggestions first and an engine that does not return
// suggestions (the default engine)
add_task(async function searchSuggestionsFirst_withoutSuggestions() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, true]],
  });
  await doTestPermutations(({ isSponsored, withHistory, generalIndex }) => ({
    // Although suggestions are shown first, the default engine does not return
    // suggestions, so the sponsored suggested index should always be -1.
    expectedSuggestedIndex: isSponsored ? -1 : generalIndex,
    expectedResultCount: withHistory ? MAX_RESULTS : 2,
    expectedViewIndex:
      (generalIndex == 0 && !isSponsored) || !withHistory ? 1 : MAX_RESULTS - 1,
  }));
  await SpecialPowers.popPrefEnv();
});

// Tests with search suggestions first, an engine that returns suggestions, but
// suggestions are disabled
add_task(async function searchSuggestionsFirst_suggestionsDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, true]],
  });
  await withSuggestions(async () => {
    await doTestPermutations(({ isSponsored, withHistory, generalIndex }) => ({
      // Since suggestions are disabled, the sponsored suggested index should
      // always be -1.
      expectedSuggestedIndex: isSponsored ? -1 : generalIndex,
      expectedResultCount: withHistory ? MAX_RESULTS : 2,
      expectedViewIndex:
        (generalIndex == 0 && !isSponsored) || !withHistory
          ? 1
          : MAX_RESULTS - 1,
    }));
  }, false);
  await SpecialPowers.popPrefEnv();
});

// Tests with history first and an engine that returns suggestions
add_task(async function historyFirst_withSuggestions() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, false]],
  });
  await withSuggestions(async () => {
    await doTestPermutations(({ isSponsored, withHistory, generalIndex }) => ({
      // Since history is shown first, the sponsored suggested index should
      // always be -1.
      expectedSuggestedIndex: isSponsored ? -1 : generalIndex,
      expectedResultCount: withHistory ? MAX_RESULTS : 4,
      expectedViewIndex:
        (generalIndex == 0 && !isSponsored) || !withHistory
          ? 1
          : MAX_RESULTS - 3,
    }));
  });
  await SpecialPowers.popPrefEnv();
});

// Tests with history first and an engine that doesn't return suggestions (the
// default engine)
add_task(async function historyFirst_withoutSuggestions() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, false]],
  });
  await doTestPermutations(({ isSponsored, withHistory, generalIndex }) => ({
    // History is shown first and there are no suggestions. Either condition by
    // itself means the sponsored suggested index should always be -1.
    expectedSuggestedIndex: isSponsored ? -1 : generalIndex,
    expectedResultCount: withHistory ? MAX_RESULTS : 2,
    expectedViewIndex:
      (generalIndex == 0 && !isSponsored) || !withHistory ? 1 : MAX_RESULTS - 1,
  }));
  await SpecialPowers.popPrefEnv();
});

// Tests with history first, an engine that returns suggestions, but suggestions
// are disabled
add_task(async function historyFirst_suggestionsDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, false]],
  });
  await withSuggestions(async () => {
    await doTestPermutations(({ isSponsored, withHistory, generalIndex }) => ({
      // History is shown first and suggestions are disabled. Either condition
      // by itself means the sponsored suggested index should always be -1.
      expectedSuggestedIndex: isSponsored ? -1 : generalIndex,
      expectedResultCount: withHistory ? MAX_RESULTS : 2,
      expectedViewIndex:
        (generalIndex == 0 && !isSponsored) || !withHistory
          ? 1
          : MAX_RESULTS - 1,
    }));
  }, false);
  await SpecialPowers.popPrefEnv();
});

// Tests with history only plus a suggestedIndex result with a resultSpan
add_task(async function otherSuggestedIndex_noSuggestions() {
  await doSuggestedIndexTest([
    // heuristic
    { heuristic: true },
    // TestProvider result
    { suggestedIndex: 1, resultSpan: 2 },
    // history
    { type: UrlbarUtils.RESULT_TYPE.URL },
    { type: UrlbarUtils.RESULT_TYPE.URL },
    { type: UrlbarUtils.RESULT_TYPE.URL },
    { type: UrlbarUtils.RESULT_TYPE.URL },
    { type: UrlbarUtils.RESULT_TYPE.URL },
    { type: UrlbarUtils.RESULT_TYPE.URL },
    // quick suggest
    {
      type: UrlbarUtils.RESULT_TYPE.URL,
      providerName: UrlbarProviderQuickSuggest.name,
    },
  ]);
});

// Tests with suggestions followed by history plus a suggestedIndex result with
// a resultSpan
add_task(async function otherSuggestedIndex_suggestionsFirst() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, true]],
  });
  await withSuggestions(async () => {
    await doSuggestedIndexTest([
      // heuristic
      { heuristic: true },
      // TestProvider result
      { suggestedIndex: 1, resultSpan: 2 },
      // search suggestions
      {
        type: UrlbarUtils.RESULT_TYPE.SEARCH,
        payload: { suggestion: SPONSORED_SEARCH_STRING + "foo" },
      },
      {
        type: UrlbarUtils.RESULT_TYPE.SEARCH,
        payload: { suggestion: SPONSORED_SEARCH_STRING + "bar" },
      },
      // quick suggest
      {
        type: UrlbarUtils.RESULT_TYPE.URL,
        providerName: UrlbarProviderQuickSuggest.name,
      },
      // history
      { type: UrlbarUtils.RESULT_TYPE.URL },
      { type: UrlbarUtils.RESULT_TYPE.URL },
      { type: UrlbarUtils.RESULT_TYPE.URL },
      { type: UrlbarUtils.RESULT_TYPE.URL },
    ]);
  });
  await SpecialPowers.popPrefEnv();
});

// Tests with history followed by suggestions plus a suggestedIndex result with
// a resultSpan
add_task(async function otherSuggestedIndex_suggestionsLast() {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_FIRST_PREF, false]],
  });
  await withSuggestions(async () => {
    await doSuggestedIndexTest([
      // heuristic
      { heuristic: true },
      // TestProvider result
      { suggestedIndex: 1, resultSpan: 2 },
      // history
      { type: UrlbarUtils.RESULT_TYPE.URL },
      { type: UrlbarUtils.RESULT_TYPE.URL },
      { type: UrlbarUtils.RESULT_TYPE.URL },
      { type: UrlbarUtils.RESULT_TYPE.URL },
      // quick suggest
      {
        type: UrlbarUtils.RESULT_TYPE.URL,
        providerName: UrlbarProviderQuickSuggest.name,
      },
      // search suggestions
      {
        type: UrlbarUtils.RESULT_TYPE.SEARCH,
        payload: { suggestion: SPONSORED_SEARCH_STRING + "foo" },
      },
      {
        type: UrlbarUtils.RESULT_TYPE.SEARCH,
        payload: { suggestion: SPONSORED_SEARCH_STRING + "bar" },
      },
    ]);
  });
  await SpecialPowers.popPrefEnv();
});

/**
 * A test provider that returns one result with a suggestedIndex and resultSpan.
 */
class TestProvider extends UrlbarTestUtils.TestProvider {
  constructor() {
    super({
      results: [
        Object.assign(
          new UrlbarResult(
            UrlbarUtils.RESULT_TYPE.URL,
            UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
            { url: "http://example.com/test" }
          ),
          {
            suggestedIndex: 1,
            resultSpan: 2,
          }
        ),
      ],
    });
  }
}

/**
 * Does a round of test permutations.
 *
 * @param {Function} callback
 *   For each permutation, this will be called with the arguments of `doTest()`,
 *   and it should return an object with the appropriate values of
 *   `expectedResultCount` and `expectedViewIndex`.
 */
async function doTestPermutations(callback) {
  for (let isSponsored of [true, false]) {
    for (let withHistory of [true, false]) {
      for (let generalIndex of [0, -1]) {
        let opts = {
          isSponsored,
          withHistory,
          generalIndex,
        };
        await doTest(Object.assign(opts, callback(opts)));
      }
    }
  }
}

/**
 * Does one test run.
 *
 * @param {object} options
 *   Options for the test.
 * @param {boolean} options.isSponsored
 *   True to use a sponsored result, false to use a non-sponsored result.
 * @param {boolean} options.withHistory
 *   True to run with a bunch of history, false to run with no history.
 * @param {number} options.generalIndex
 *   The value to set as the relevant index pref, i.e., the index within the
 *   general group of the quick suggest result.
 * @param {number} options.expectedSuggestedIndex
 *   The expected suggested index of the quick suggest result.
 * @param {number} options.expectedResultCount
 *   The expected total result count for sanity checking.
 * @param {number} options.expectedViewIndex
 *   The expected actual index in the view of the quick suggest result.
 */
async function doTest({
  isSponsored,
  withHistory,
  generalIndex,
  expectedSuggestedIndex,
  expectedResultCount,
  expectedViewIndex,
}) {
  info(
    "Running test with options: " +
      JSON.stringify({
        isSponsored,
        withHistory,
        generalIndex,
        expectedResultCount,
        expectedViewIndex,
      })
  );

  // Set the index pref.
  let indexPref = isSponsored ? SPONSORED_INDEX_PREF : NON_SPONSORED_INDEX_PREF;
  await SpecialPowers.pushPrefEnv({
    set: [[indexPref, generalIndex]],
  });

  // Add history.
  if (withHistory) {
    await addHistory();
  }

  // Do a search.
  let value = isSponsored
    ? SPONSORED_SEARCH_STRING
    : NON_SPONSORED_SEARCH_STRING;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value,
  });

  // Check the result count and quick suggest result.
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    expectedResultCount,
    "Expected result count"
  );
  let details = await QuickSuggestTestUtils.assertIsQuickSuggest({
    window,
    isSponsored,
    index: expectedViewIndex,
    url: isSponsored
      ? REMOTE_SETTINGS_RESULTS[0].url
      : REMOTE_SETTINGS_RESULTS[1].url,
  });

  Assert.equal(
    details.result.suggestedIndex,
    expectedSuggestedIndex,
    "Expected suggestedIndex"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
}

/**
 * Adds history that matches the sponsored and non-sponsored search strings.
 */
async function addHistory() {
  for (let i = 0; i < MAX_RESULTS; i++) {
    await PlacesTestUtils.addVisits([
      "http://example.com/" + SPONSORED_SEARCH_STRING + i,
      "http://example.com/" + NON_SPONSORED_SEARCH_STRING + i,
    ]);
  }
}

/**
 * Adds a search engine that provides suggestions, calls your callback, and then
 * removes the engine.
 *
 * @param {Function} callback
 *   Your callback function.
 * @param {boolean} enableSuggestions
 *   Whether `browser.urlbar.suggest.searches` should be enabled.
 */
async function withSuggestions(callback, enableSuggestions = true) {
  await SpecialPowers.pushPrefEnv({
    set: [[SUGGESTIONS_PREF, enableSuggestions]],
  });
  let engine = await SearchTestUtils.installOpenSearchEngine({
    url: getRootDirectory(gTestPath) + TEST_ENGINE_BASENAME,
  });
  let oldDefaultEngine = await Services.search.getDefault();
  await Services.search.setDefault(
    engine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
  try {
    await callback(engine);
  } finally {
    await Services.search.setDefault(
      oldDefaultEngine,
      Ci.nsISearchService.CHANGE_REASON_UNKNOWN
    );
    await Services.search.removeEngine(engine);
    await SpecialPowers.popPrefEnv();
  }
}

/**
 * Registers a test provider that returns a result with a suggestedIndex and
 * resultSpan and asserts the given expected results match the actual results.
 *
 * @param {Array} expectedProps
 *   See `checkResults()`.
 */
async function doSuggestedIndexTest(expectedProps) {
  await addHistory();
  let provider = new TestProvider();
  UrlbarProvidersManager.registerProvider(provider);

  let context = await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: SPONSORED_SEARCH_STRING,
  });
  checkResults(context.results, expectedProps);
  await UrlbarTestUtils.promisePopupClose(window);

  UrlbarProvidersManager.unregisterProvider(provider);
  await PlacesUtils.history.clear();
}

/**
 * Asserts the given actual and expected results match.
 *
 * @param {Array} actualResults
 *   Array of actual results.
 * @param {Array} expectedProps
 *   Array of expected result-like objects. Only the properties defined in each
 *   of these objects are compared against the corresponding actual result.
 */
function checkResults(actualResults, expectedProps) {
  Assert.equal(
    actualResults.length,
    expectedProps.length,
    "Expected result count"
  );

  let actualProps = actualResults.map((actual, i) => {
    if (expectedProps.length <= i) {
      return actual;
    }
    let props = {};
    let expected = expectedProps[i];
    for (let [key, expectedValue] of Object.entries(expected)) {
      if (key != "payload") {
        props[key] = actual[key];
      } else {
        props.payload = {};
        for (let pkey of Object.keys(expectedValue)) {
          props.payload[pkey] = actual.payload[pkey];
        }
      }
    }
    return props;
  });
  Assert.deepEqual(actualProps, expectedProps);
}
