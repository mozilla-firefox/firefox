/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests Fakespot suggestions.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  Suggestion: "resource://gre/modules/RustSuggest.sys.mjs",
});

const REMOTE_SETTINGS_RECORDS = [
  {
    collection: "fakespot-suggest-products",
    type: "fakespot-suggestions",
    attachment: [
      {
        url: "https://example.com/fakespot-0",
        title: "Example Fakespot suggestion",
        rating: 4.6,
        fakespot_grade: "A",
        total_reviews: 167,
        product_id: "amazon-0",
        score: 0.68416834,
        keywords: "",
        product_type: "",
      },
      {
        url: "https://example.com/fakespot-1",
        title: "Another Fakespot suggestion",
        rating: 3.5,
        fakespot_grade: "B",
        total_reviews: 100,
        product_id: "amazon-1",
        score: 0.5,
        keywords: "",
        product_type: "",
      },
    ],
  },
];

const PRIMARY_SEARCH_STRING = "example";
const PRIMARY_TITLE = REMOTE_SETTINGS_RECORDS[0].attachment[0].title;
const PRIMARY_URL = REMOTE_SETTINGS_RECORDS[0].attachment[0].url;

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["suggest.fakespot", true],
      ["fakespot.featureGate", true],
    ],
  });

  // Many parts of this test assume the default minKeywordLength is 4. Please
  // update it if the default changes.
  Assert.equal(
    UrlbarPrefs.get("fakespot.minKeywordLength"),
    4,
    "Sanity check: This test assumes the default minKeywordLength is 4"
  );
});

add_task(async function basic() {
  const TEST_DATA = [
    {
      description: "Basic",
      query: "example fakespot suggestion",
      expected: {
        url: PRIMARY_URL,
        title: PRIMARY_TITLE,
      },
    },
    {
      description: "With upper case",
      query: "ExAmPlE fAKeSpOt SuGgEsTiOn",
      expected: {
        url: PRIMARY_URL,
        title: PRIMARY_TITLE,
      },
    },
    {
      description: "Prefix match 1",
      query: "ex",
      expected: null,
    },
    {
      description: "Prefix match 2",
      query: "examp",
      expected: {
        url: PRIMARY_URL,
        title: PRIMARY_TITLE,
      },
    },
    {
      description: "First full word",
      query: "example",
      expected: {
        url: PRIMARY_URL,
        title: PRIMARY_TITLE,
      },
    },
    {
      description: "First full word + prefix",
      query: "example f",
      expected: {
        url: PRIMARY_URL,
        title: PRIMARY_TITLE,
      },
    },
  ];

  for (let { description, query, expected } of TEST_DATA) {
    info(
      "Doing basic subtest: " +
        JSON.stringify({
          description,
          query,
          expected,
        })
    );

    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expected ? [makeExpectedResult(expected)] : [],
    });
  }
});

add_task(async function telemetryType() {
  let tests = [
    { productId: "amazon-123", expected: "fakespot_amazon" },
    { productId: "bestbuy-345", expected: "fakespot_bestbuy" },
    { productId: "walmart-789", expected: "fakespot_walmart" },
    { productId: "bogus-123", expected: "fakespot_other" },
    // We should maybe record "other" for this but "amazon" is just as fine.
    { productId: "amazon", expected: "fakespot_amazon" },
    // productId values below don't follow the expected `{provider}-{id}` format
    // and should therefore be recorded as "other".
    { productId: "amazon123", expected: "fakespot_other" },
    { productId: "", expected: "fakespot_other" },
    { productId: null, expected: "fakespot_other" },
    { productId: undefined, expected: "fakespot_other" },
    { expected: "fakespot_other" },
  ];
  for (let { productId, expected } of tests) {
    Assert.equal(
      QuickSuggest.getFeature("FakespotSuggestions").getSuggestionTelemetryType(
        { productId }
      ),
      expected,
      "Telemetry type should be correct for productId: " + productId
    );
  }
});

// When sponsored suggestions are disabled, Fakespot suggestions should be
// disabled.
add_task(async function sponsoredDisabled() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);

  // First make sure the suggestion is added when sponsored suggestions are
  // enabled, if the rust is enabled.
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult()],
  });

  // Now disable the pref.
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  Assert.ok(
    !QuickSuggest.getFeature("FakespotSuggestions").isEnabled,
    "Fakespot should be disabled"
  );
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.clear("suggest.quicksuggest.nonsponsored");
  await QuickSuggestTestUtils.forceSync();

  // Make sure Fakespot is enabled again.
  Assert.ok(
    QuickSuggest.getFeature("FakespotSuggestions").isEnabled,
    "Fakespot should be re-enabled"
  );
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult()],
  });
});

// When Fakespot-specific preferences are disabled, suggestions should not be
// added.
add_task(async function fakespotSpecificPrefsDisabled() {
  const prefs = ["suggest.fakespot", "fakespot.featureGate"];
  for (const pref of prefs) {
    // First make sure the suggestion is added.
    await check_results({
      context: createContext(PRIMARY_SEARCH_STRING, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [makeExpectedResult()],
    });

    // Now disable the pref.
    UrlbarPrefs.set(pref, false);
    Assert.ok(
      !QuickSuggest.getFeature("FakespotSuggestions").isEnabled,
      "Fakespot should be disabled"
    );
    await check_results({
      context: createContext(PRIMARY_SEARCH_STRING, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });

    // Revert.
    UrlbarPrefs.set(pref, true);
    await QuickSuggestTestUtils.forceSync();

    // Make sure Fakespot is enabled again.
    Assert.ok(
      QuickSuggest.getFeature("FakespotSuggestions").isEnabled,
      "Fakespot should be re-enabled"
    );
    await check_results({
      context: createContext(PRIMARY_SEARCH_STRING, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [makeExpectedResult()],
    });
  }
});

// Check whether suggestions will be shown by the setup of Nimbus variable.
add_task(async function featureGate() {
  // Disable the feature gate.
  UrlbarPrefs.set("fakespot.featureGate", false);
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  // Enable by Nimbus.
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    fakespotFeatureGate: true,
  });
  await QuickSuggestTestUtils.forceSync();
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult()],
  });
  await cleanUpNimbusEnable();

  // Enable locally.
  UrlbarPrefs.set("fakespot.featureGate", true);
  await QuickSuggestTestUtils.forceSync();

  // Disable by Nimbus.
  const cleanUpNimbusDisable = await UrlbarTestUtils.initNimbusFeature({
    fakespotFeatureGate: false,
  });
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });
  await cleanUpNimbusDisable();

  // Revert.
  UrlbarPrefs.set("fakespot.featureGate", true);
  await QuickSuggestTestUtils.forceSync();
});

// Tests the "Not relevant" command: a dismissed suggestion shouldn't be added.
add_task(async function notRelevant() {
  await doDismissOneTest({
    result: makeExpectedResult(),
    command: "not_relevant",
    feature: QuickSuggest.getFeature("FakespotSuggestions"),
    queriesForDismissals: [
      {
        query: "example",
      },
    ],
    queriesForOthers: [
      {
        query: "another",
        expectedResults: [
          makeExpectedResult({
            url: REMOTE_SETTINGS_RECORDS[0].attachment[1].url,
            title: REMOTE_SETTINGS_RECORDS[0].attachment[1].title,
            rating: REMOTE_SETTINGS_RECORDS[0].attachment[1].rating,
            totalReviews:
              REMOTE_SETTINGS_RECORDS[0].attachment[1].total_reviews,
            fakespotGrade:
              REMOTE_SETTINGS_RECORDS[0].attachment[1].fakespot_grade,
          }),
        ],
      },
    ],
  });
});

// Tests the "Not interested" command: all suggestions should be disabled and
// not added anymore.
add_task(async function notInterested() {
  await doDismissAllTest({
    result: makeExpectedResult(),
    command: "not_interested",
    feature: QuickSuggest.getFeature("FakespotSuggestions"),
    pref: "suggest.fakespot",
    queries: [
      {
        query: "example",
      },
      {
        query: "another",
        expectedResults: [
          makeExpectedResult({
            url: REMOTE_SETTINGS_RECORDS[0].attachment[1].url,
            title: REMOTE_SETTINGS_RECORDS[0].attachment[1].title,
            rating: REMOTE_SETTINGS_RECORDS[0].attachment[1].rating,
            totalReviews:
              REMOTE_SETTINGS_RECORDS[0].attachment[1].total_reviews,
            fakespotGrade:
              REMOTE_SETTINGS_RECORDS[0].attachment[1].fakespot_grade,
          }),
        ],
      },
    ],
  });
});

// Tests the "show less frequently" behavior.
add_task(async function showLessFrequently() {
  UrlbarPrefs.clear("fakespot.showLessFrequentlyCount");
  UrlbarPrefs.clear("fakespot.minKeywordLength");

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    fakespotMinKeywordLength: 0,
    fakespotShowLessFrequentlyCap: 3,
  });

  let result = makeExpectedResult();

  const testData = [
    {
      input: "example",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 0,
        minKeywordLength: 4,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 8,
      },
    },
    {
      input: "example f",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 8,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 10,
      },
    },
    {
      input: "example fa",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 10,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 11,
      },
    },
    {
      input: "example fak",
      before: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 11,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 12,
      },
    },
  ];

  for (let { input, before, after } of testData) {
    let feature = QuickSuggest.getFeature("FakespotSuggestions");

    await check_results({
      context: createContext(input, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [result],
    });

    Assert.equal(
      UrlbarPrefs.get("fakespot.minKeywordLength"),
      before.minKeywordLength
    );
    Assert.equal(feature.canShowLessFrequently, before.canShowLessFrequently);
    Assert.equal(
      feature.showLessFrequentlyCount,
      before.showLessFrequentlyCount
    );

    triggerCommand({
      result,
      feature,
      command: "show_less_frequently",
      searchString: input,
    });

    Assert.equal(
      UrlbarPrefs.get("fakespot.minKeywordLength"),
      after.minKeywordLength
    );
    Assert.equal(feature.canShowLessFrequently, after.canShowLessFrequently);
    Assert.equal(
      feature.showLessFrequentlyCount,
      after.showLessFrequentlyCount
    );

    await check_results({
      context: createContext(input, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });
  }

  await cleanUpNimbus();
  UrlbarPrefs.clear("fakespot.showLessFrequentlyCount");
  UrlbarPrefs.clear("fakespot.minKeywordLength");
});

// The `Fakespot` Rust provider should be passed to the Rust component when
// querying depending on whether Fakespot suggestions are enabled.
add_task(async function rustProviders() {
  await doRustProvidersTests({
    searchString: PRIMARY_SEARCH_STRING,
    tests: [
      {
        prefs: {
          "suggest.fakespot": true,
        },
        expectedUrls: [PRIMARY_URL],
      },
      {
        prefs: {
          "suggest.fakespot": false,
        },
        expectedUrls: [],
      },
    ],
  });

  UrlbarPrefs.clear("suggest.fakespot");
  await QuickSuggestTestUtils.forceSync();
});

add_task(async function minKeywordLength_defaultPrefValue() {
  await doMinKeywordLengthTest({
    // expected min length: 5 (Nimbus value should override default pref value)
    prefValue: null,
    nimbusValue: 5,
    expectedValue: 5,
    tests: [
      {
        query: "ex",
        expected: null,
      },
      {
        query: "exa",
        expected: null,
      },
      {
        query: "exam",
        expected: null,
      },
      {
        query: "examp",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "example",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "example f",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
    ],
  });
});

add_task(async function minKeywordLength_smallerPrefUserValue() {
  await doMinKeywordLengthTest({
    // expected min length: 5 (pref value)
    prefValue: 5,
    nimbusValue: 6,
    expectedValue: 5,
    tests: [
      {
        query: "ex",
        expected: null,
      },
      {
        query: "exa",
        expected: null,
      },
      {
        query: "exam",
        expected: null,
      },
      {
        query: "examp",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "exampl",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "example f",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
    ],
  });
});

add_task(async function minKeywordLength_largerPrefUserValue() {
  await doMinKeywordLengthTest({
    // expected min length: 6 (pref value)
    prefValue: 6,
    nimbusValue: 5,
    expectedValue: 6,
    tests: [
      {
        query: "ex",
        expected: null,
      },
      {
        query: "exa",
        expected: null,
      },
      {
        query: "exam",
        expected: null,
      },
      {
        query: "examp",
        expected: null,
      },
      {
        query: "exampl",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "example f",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
    ],
  });
});

add_task(async function minKeywordLength_onlyPrefValue() {
  await doMinKeywordLengthTest({
    // expected min length: 5 (pref value)
    prefValue: 5,
    nimbusValue: null,
    expectedValue: 5,
    tests: [
      {
        query: "ex",
        expected: null,
      },
      {
        query: "exa",
        expected: null,
      },
      {
        query: "exam",
        expected: null,
      },
      {
        query: "examp",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "example f",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
    ],
  });
});

// When no min length is defined in Nimbus and the pref doesn't have a user
// value, we should fall back to the default pref value of 4.
add_task(async function minKeywordLength_noNimbusOrPrefUserValue() {
  await doMinKeywordLengthTest({
    // expected min length: 4 (pref default value)
    prefValue: null,
    nimbusValue: null,
    expectedValue: 4,
    tests: [
      {
        query: "ex",
        expected: null,
      },
      {
        query: "exa",
        expected: null,
      },
      {
        query: "exam",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
      {
        query: "example f",
        expected: {
          url: PRIMARY_URL,
          title: PRIMARY_TITLE,
        },
      },
    ],
  });
});

async function doMinKeywordLengthTest({
  prefValue,
  nimbusValue,
  expectedValue,
  tests,
}) {
  // Set or clear the pref.
  let originalPrefValue = Services.prefs.prefHasUserValue(
    "browser.urlbar.fakespot.minKeywordLength"
  )
    ? UrlbarPrefs.get("fakespot.minKeywordLength")
    : null;
  if (typeof prefValue == "number") {
    UrlbarPrefs.set("fakespot.minKeywordLength", prefValue);
  } else {
    UrlbarPrefs.clear("fakespot.minKeywordLength");
  }

  // Set up Nimbus.
  let cleanUpNimbus;
  if (typeof nimbusValue == "number") {
    cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
      fakespotMinKeywordLength: nimbusValue,
    });
  }

  // Check the min length directly.
  Assert.equal(
    QuickSuggest.getFeature("FakespotSuggestions")._test_minKeywordLength,
    expectedValue,
    "minKeywordLength should be correct"
  );

  // The min length should be used when searching.
  for (let { query, expected } of tests) {
    info("Running min keyword length test with query: " + query);
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expected ? [makeExpectedResult(expected)] : [],
    });
  }

  await cleanUpNimbus?.();

  if (originalPrefValue === null) {
    UrlbarPrefs.clear("fakespot.minKeywordLength");
  } else {
    UrlbarPrefs.set("fakespot.minKeywordLength", originalPrefValue);
  }
}

// Tests the `fakespotSuggestedIndex` Nimbus variable.
add_task(async function suggestedIndex() {
  // If this fails, please update this task. Otherwise it's not actually testing
  // a non-default suggested index.
  Assert.notEqual(
    UrlbarPrefs.get("fakespotSuggestedIndex"),
    0,
    "Sanity check: Default value of fakespotSuggestedIndex is not 0"
  );

  let cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    fakespotSuggestedIndex: 0,
  });
  await check_results({
    context: createContext(PRIMARY_SEARCH_STRING, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult({ suggestedIndex: 0 })],
  });
  await cleanUpNimbusEnable();
});

function makeExpectedResult({
  url = PRIMARY_URL,
  title = PRIMARY_TITLE,
  suggestedIndex = -1,
  isSuggestedIndexRelativeToGroup = true,
  originalUrl = undefined,
  displayUrl = undefined,
  fakespotProvider = "amazon",
  rating = REMOTE_SETTINGS_RECORDS[0].attachment[0].rating,
  totalReviews = REMOTE_SETTINGS_RECORDS[0].attachment[0].total_reviews,
  fakespotGrade = REMOTE_SETTINGS_RECORDS[0].attachment[0].fakespot_grade,
} = {}) {
  originalUrl ??= url;

  displayUrl =
    displayUrl ??
    url
      .replace(/^https:\/\//, "")
      .replace(/^www[.]/, "")
      .replace("%20", " ")
      .replace("%2C", ",");

  return {
    type: UrlbarUtils.RESULT_TYPE.DYNAMIC,
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    isBestMatch: false,
    suggestedIndex,
    isSuggestedIndexRelativeToGroup,
    heuristic: false,
    payload: {
      source: "rust",
      provider: "Fakespot",
      telemetryType: "fakespot_" + fakespotProvider,
      url,
      originalUrl,
      title,
      displayUrl,
      rating,
      totalReviews,
      fakespotGrade,
      fakespotProvider,
      isSponsored: true,
      dynamicType: "fakespot",
      icon: null,
      suggestionObject: new Suggestion.Fakespot(
        fakespotGrade,
        "", // productId
        rating,
        title,
        totalReviews,
        originalUrl, // url
        null, // icon
        null, // iconMimetype
        0.2, // score
        null // matchInfo
      ),
    },
  };
}
