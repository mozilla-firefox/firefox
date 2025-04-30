/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests Pocket quick suggest results.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  Suggestion: "resource://gre/modules/RustSuggest.sys.mjs",
});

const LOW_KEYWORD = "low one two";
const HIGH_KEYWORD = "high three";

const REMOTE_SETTINGS_DATA = [
  {
    type: "pocket-suggestions",
    attachment: [
      {
        url: "https://example.com/pocket-0",
        title: "Pocket Suggestion 0",
        description: "Pocket description 0",
        lowConfidenceKeywords: [LOW_KEYWORD, "how to low"],
        highConfidenceKeywords: [HIGH_KEYWORD],
        score: 0.25,
      },
      {
        url: "https://example.com/pocket-1",
        title: "Pocket Suggestion 1",
        description: "Pocket description 1",
        lowConfidenceKeywords: ["other low"],
        highConfidenceKeywords: ["another high"],
        score: 0.25,
      },
    ],
  },
];

add_setup(async () => {
  // Disable search suggestions so we don't hit the network.
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_DATA,
    prefs: [
      ["suggest.quicksuggest.nonsponsored", true],
      ["pocket.featureGate", true],
    ],
  });
});

// Tests the `pocketSuggestIndex` Nimbus variable, which controls the
// group-relative suggestedIndex. The default Pocket suggestedIndex is 0.
add_task(async function nimbusSuggestedIndex() {
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    pocketSuggestIndex: 0,
  });
  await QuickSuggestTestUtils.forceSync();

  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({
        searchString: LOW_KEYWORD,
        suggestedIndex: 0,
      }),
    ],
  });
  await check_results({
    context: createContext(HIGH_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({
        searchString: HIGH_KEYWORD,
        suggestedIndex: 1,
        isTopPick: true,
      }),
    ],
  });

  await cleanUpNimbusEnable();
  await QuickSuggestTestUtils.forceSync();

  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({
        searchString: LOW_KEYWORD,
        suggestedIndex: -1,
      }),
    ],
  });
});

add_task(async function telemetryType() {
  Assert.equal(
    QuickSuggest.getFeature("PocketSuggestions").getSuggestionTelemetryType({}),
    "pocket",
    "Telemetry type should be 'pocket'"
  );
});

// When non-sponsored suggestions are disabled, Pocket suggestions should be
// disabled.
add_task(async function nonsponsoredDisabled() {
  // Disable sponsored suggestions. Pocket suggestions are non-sponsored, so
  // doing this should not prevent them from being enabled.
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);

  // First make sure the suggestion is added when non-sponsored suggestions are
  // enabled.
  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult({ searchString: LOW_KEYWORD })],
  });

  // Now disable them.
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);
  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", true);
  UrlbarPrefs.clear("suggest.quicksuggest.sponsored");
  await QuickSuggestTestUtils.forceSync();
});

// When Pocket-specific preferences are disabled, suggestions should not be
// added.
add_task(async function pocketSpecificPrefsDisabled() {
  const prefs = ["suggest.pocket", "pocket.featureGate"];
  for (const pref of prefs) {
    // First make sure the suggestion is added.
    await check_results({
      context: createContext(LOW_KEYWORD, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [makeExpectedResult({ searchString: LOW_KEYWORD })],
    });

    // Now disable the pref.
    UrlbarPrefs.set(pref, false);
    await check_results({
      context: createContext(LOW_KEYWORD, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });

    // Revert.
    UrlbarPrefs.set(pref, true);
    await QuickSuggestTestUtils.forceSync();
  }
});

// Check wheather the Pocket suggestions will be shown by the setup of Nimbus
// variable.
add_task(async function nimbus() {
  // Disable the fature gate.
  UrlbarPrefs.set("pocket.featureGate", false);
  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  // Enable by Nimbus.
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    pocketFeatureGate: true,
  });
  await QuickSuggestTestUtils.forceSync();
  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult({ searchString: LOW_KEYWORD })],
  });
  await cleanUpNimbusEnable();

  // Enable locally.
  UrlbarPrefs.set("pocket.featureGate", true);
  await QuickSuggestTestUtils.forceSync();

  // Disable by Nimbus.
  const cleanUpNimbusDisable = await UrlbarTestUtils.initNimbusFeature({
    pocketFeatureGate: false,
  });
  await check_results({
    context: createContext(LOW_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });
  await cleanUpNimbusDisable();

  // Revert.
  UrlbarPrefs.set("pocket.featureGate", true);
  await QuickSuggestTestUtils.forceSync();
});

// The suggestion should be shown as a top pick when a high-confidence keyword
// is matched.
add_task(async function topPick() {
  await check_results({
    context: createContext(HIGH_KEYWORD, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({ searchString: HIGH_KEYWORD, isTopPick: true }),
    ],
  });
});

// Low-confidence keywords should do prefix matching starting at the first word.
add_task(async function lowPrefixes() {
  // search string -> should match
  let tests = {
    l: false,
    lo: false,
    low: true,
    "low ": true,
    "low o": true,
    "low on": true,
    "low one": true,
    "low one ": true,
    "low one t": true,
    "low one tw": true,
    "low one two": true,
    "low one two ": false,
  };
  for (let [searchString, shouldMatch] of Object.entries(tests)) {
    info("Doing search: " + JSON.stringify({ searchString, shouldMatch }));
    await check_results({
      context: createContext(searchString, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: shouldMatch
        ? [makeExpectedResult({ searchString, fullKeyword: LOW_KEYWORD })]
        : [],
    });
  }
});

// High-confidence keywords should not do prefix matching at all.
add_task(async function highPrefixes() {
  // search string -> should match
  let tests = {
    h: false,
    hi: false,
    hig: false,
    high: false,
    "high ": false,
    "high t": false,
    "high th": false,
    "high thr": false,
    "high thre": false,
    "high three": true,
    "high three ": false,
  };
  for (let [searchString, shouldMatch] of Object.entries(tests)) {
    info("Doing search: " + JSON.stringify({ searchString, shouldMatch }));
    await check_results({
      context: createContext(searchString, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: shouldMatch
        ? [
            makeExpectedResult({
              searchString,
              fullKeyword: HIGH_KEYWORD,
              isTopPick: true,
            }),
          ]
        : [],
    });
  }
});

// Keyword matching should be case insenstive.
add_task(async function uppercase() {
  await check_results({
    context: createContext(LOW_KEYWORD.toUpperCase(), {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({
        searchString: LOW_KEYWORD.toUpperCase(),
        fullKeyword: LOW_KEYWORD,
      }),
    ],
  });
  await check_results({
    context: createContext(HIGH_KEYWORD.toUpperCase(), {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({
        searchString: HIGH_KEYWORD.toUpperCase(),
        fullKeyword: HIGH_KEYWORD,
        isTopPick: true,
      }),
    ],
  });
});

// Tests the "Not relevant" command: a dismissed suggestion shouldn't be added.
add_task(async function notRelevant() {
  await doDismissOneTest({
    result: makeExpectedResult({ searchString: LOW_KEYWORD }),
    command: "not_relevant",
    feature: QuickSuggest.getFeature("PocketSuggestions"),
    queriesForDismissals: [
      { query: LOW_KEYWORD },
      {
        query: HIGH_KEYWORD,
        expectedResults: [
          makeExpectedResult({
            searchString: HIGH_KEYWORD,
            suggestedIndex: 1,
            isTopPick: true,
          }),
        ],
      },
    ],
    queriesForOthers: [
      {
        query: "other low",
        expectedResults: [
          makeExpectedResult({
            searchString: "other low",
            suggestion: REMOTE_SETTINGS_DATA[0].attachment[1],
          }),
        ],
      },
    ],
  });
});

// Tests the "Not interested" command: all Yelp suggestions should be disabled
// and not added anymore.
add_task(async function notInterested() {
  await doDismissAllTest({
    result: makeExpectedResult({ searchString: LOW_KEYWORD }),
    command: "not_interested",
    feature: QuickSuggest.getFeature("PocketSuggestions"),
    pref: "suggest.pocket",
    queries: [
      {
        query: LOW_KEYWORD,
      },
      {
        query: "other low",
        expectedResults: [
          makeExpectedResult({
            searchString: "other low",
            suggestion: REMOTE_SETTINGS_DATA[0].attachment[1],
          }),
        ],
      },
    ],
  });
});

// Tests the "show less frequently" behavior.
add_task(async function showLessFrequently() {
  await doShowLessFrequentlyTests({
    feature: QuickSuggest.getFeature("PocketSuggestions"),
    showLessFrequentlyCountPref: "pocket.showLessFrequentlyCount",
    nimbusCapVariable: "pocketShowLessFrequentlyCap",
    expectedResult: searchString =>
      makeExpectedResult({ searchString, fullKeyword: LOW_KEYWORD }),
    keyword: LOW_KEYWORD,
  });
});

// The `Pocket` Rust provider should be passed to the Rust component when
// querying depending on whether Pocket suggestions are enabled.
add_task(async function rustProviders() {
  // TODO bug 1874074: The Rust component fetches Pocket suggestions when the
  // AMO provider is specified regardless of whether the Pocket provider is
  // specified. AMO suggestions are enabled by default, so disable them first so
  // that the Rust backend does not pass in the AMO provider.
  UrlbarPrefs.set("suggest.addons", false);

  await doRustProvidersTests({
    searchString: LOW_KEYWORD,
    tests: [
      {
        prefs: {
          "suggest.pocket": true,
        },
        expectedUrls: ["https://example.com/pocket-0"],
      },
      {
        prefs: {
          "suggest.pocket": false,
        },
        expectedUrls: [],
      },
    ],
  });

  UrlbarPrefs.clear("suggest.addons");
  UrlbarPrefs.clear("suggest.pocket");
  await QuickSuggestTestUtils.forceSync();
});

function makeExpectedResult({
  searchString,
  fullKeyword = searchString,
  suggestion = REMOTE_SETTINGS_DATA[0].attachment[0],
  source = "rust",
  isTopPick = false,
  suggestedIndex,
} = {}) {
  let provider;
  let keywordSubstringNotTyped = fullKeyword.substring(searchString.length);
  let description = suggestion.description;
  switch (source) {
    case "rust":
      provider = "Pocket";
      // Rust suggestions currently do not include full keyword or description.
      keywordSubstringNotTyped = "";
      description = suggestion.title;
      break;
    case "merino":
      provider = "pocket";
      break;
  }

  let url = new URL(suggestion.url);
  url.searchParams.set("utm_medium", "firefox-desktop");
  url.searchParams.set("utm_source", "firefox-suggest");
  url.searchParams.set("utm_campaign", "pocket-collections-in-the-address-bar");
  url.searchParams.set("utm_content", "treatment");

  let expectedSuggestedIndex = -1;
  if (suggestedIndex !== undefined) {
    expectedSuggestedIndex = suggestedIndex;
  } else if (isTopPick) {
    expectedSuggestedIndex = 1;
  }

  let result = {
    isBestMatch: isTopPick,
    suggestedIndex: expectedSuggestedIndex,
    type: UrlbarUtils.RESULT_TYPE.URL,
    source: UrlbarUtils.RESULT_SOURCE.OTHER_NETWORK,
    heuristic: false,
    payload: {
      source,
      provider,
      telemetryType: "pocket",
      title: suggestion.title,
      url: url.href,
      displayUrl: url.href.replace(/^https:\/\//, ""),
      originalUrl: suggestion.url,
      description: isTopPick ? description : "",
      icon: isTopPick
        ? "chrome://global/skin/icons/pocket.svg"
        : "chrome://global/skin/icons/pocket-favicon.ico",
      isSponsored: false,
      helpUrl: QuickSuggest.HELP_URL,
      shouldShowUrl: true,
      bottomTextL10n: {
        id: "firefox-suggest-pocket-bottom-text",
        args: {
          keywordSubstringTyped: searchString,
          keywordSubstringNotTyped,
        },
      },
    },
  };

  if (source == "rust") {
    result.payload.suggestionObject = new Suggestion.Pocket(
      suggestion.title,
      suggestion.url,
      0.2, // score
      isTopPick
    );
  }

  return result;
}
