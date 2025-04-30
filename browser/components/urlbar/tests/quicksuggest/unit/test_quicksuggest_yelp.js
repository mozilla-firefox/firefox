/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests Yelp suggestions.

"use strict";

const { GEOLOCATION } = MerinoTestUtils;

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "yelp-suggestions",
    attachment: {
      subjects: ["ramen", "ab", "alongerkeyword", "1234"],
      preModifiers: ["best"],
      postModifiers: ["delivery"],
      locationSigns: ["in", "nearby"],
      yelpModifiers: [],
      icon: "1234",
      score: 0.5,
    },
  },
  QuickSuggestTestUtils.geonamesRecord(),
];

const TOKYO_RESULT = {
  url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Tokyo%2C+Tokyo-to",
  title: "ramen in Tokyo, Tokyo-to",
};

const AB_RESULT = {
  url: "https://www.yelp.com/search?find_desc=ab&find_loc=Yokohama%2C+Kanagawa",
  title: "ab in Yokohama, Kanagawa",
};

const ALONGERKEYWORD_RESULT = {
  url: "https://www.yelp.com/search?find_desc=alongerkeyword&find_loc=Yokohama%2C+Kanagawa",
  title: "alongerkeyword in Yokohama, Kanagawa",
};

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [["suggest.quicksuggest.sponsored", true]],
  });

  await MerinoTestUtils.initGeolocation();

  // Many parts of this test assume the default minKeywordLength is 4. Please
  // update it if the default changes.
  Assert.equal(
    UrlbarPrefs.get("yelp.minKeywordLength"),
    4,
    "Sanity check: This test assumes the default minKeywordLength is 4"
  );
});

add_task(async function basic() {
  const TEST_DATA = [
    {
      description: "Basic",
      query: "best ramen delivery in tokyo",
      expected: {
        url: "https://www.yelp.com/search?find_desc=best+ramen+delivery&find_loc=Tokyo%2C+Tokyo-to",
        title: "best ramen delivery in Tokyo, Tokyo-to",
      },
    },
    {
      description: "With upper case",
      query: "BeSt RaMeN dElIvErY iN tOkYo",
      expected: {
        url: "https://www.yelp.com/search?find_desc=BeSt+RaMeN+dElIvErY&find_loc=Tokyo%2C+Tokyo-to",
        title: "BeSt RaMeN dElIvErY iN Tokyo, Tokyo-to",
      },
    },
    {
      description: "No specific location with location-sign",
      query: "ramen in",
      expected: {
        url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "ramen in Yokohama, Kanagawa",
      },
    },
    {
      description: "No specific location with location-modifier",
      query: "ramen nearby",
      expected: {
        url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "ramen nearby Yokohama, Kanagawa",
      },
    },
    {
      description: "Query too short, no subject exact match: ra",
      query: "ra",
      expected: null,
    },
    {
      description: "Query too short, no subject not exact match: ram",
      query: "ram",
      expected: null,
    },
    {
      description: "Query length == minKeywordLength, no subject exact match",
      query: "rame",
      expected: {
        url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "ramen in Yokohama, Kanagawa",
      },
    },
    {
      description:
        "Query length == minKeywordLength, subject exact match: 1234",
      query: "1234",
      expected: {
        url: "https://www.yelp.com/search?find_desc=1234&find_loc=Yokohama%2C+Kanagawa",
        title: "1234 in Yokohama, Kanagawa",
      },
    },
    {
      description:
        "Query length > minKeywordLength, subject exact match: ramen",
      query: "ramen",
      expected: {
        url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "ramen in Yokohama, Kanagawa",
      },
    },
    {
      description: "Pre-modifier only",
      query: "best",
      expected: null,
    },
    {
      description: "Pre-modifier only with trailing space",
      query: "best ",
      expected: null,
    },
    {
      description: "Pre-modifier, subject too short",
      query: "best r",
      expected: null,
    },
    {
      description: "Pre-modifier, query long enough, subject long enough",
      query: "best ra",
      expected: {
        url: "https://www.yelp.com/search?find_desc=best+ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "best ramen in Yokohama, Kanagawa",
      },
    },
    {
      description: "Subject exact match with length < minKeywordLength",
      query: "ab",
      expected: AB_RESULT,
    },
    {
      description:
        "Subject exact match with length < minKeywordLength, showLessFrequentlyCount non-zero",
      query: "ab",
      showLessFrequentlyCount: 1,
      expected: null,
    },
    {
      description:
        "Subject exact match with length == minKeywordLength, showLessFrequentlyCount non-zero",
      query: "1234",
      showLessFrequentlyCount: 1,
      expected: {
        url: "https://www.yelp.com/search?find_desc=1234&find_loc=Yokohama%2C+Kanagawa",
        title: "1234 in Yokohama, Kanagawa",
      },
    },
    {
      description:
        "Subject exact match with length > minKeywordLength, showLessFrequentlyCount non-zero",
      query: "ramen",
      showLessFrequentlyCount: 1,
      expected: {
        url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "ramen in Yokohama, Kanagawa",
      },
    },
    {
      description: "Query too short: alo",
      query: "alo",
      expected: null,
    },
    {
      description: "Query length == minKeywordLength, subject not exact match",
      query: "alon",
      expected: ALONGERKEYWORD_RESULT,
    },
    {
      description: "Query length > minKeywordLength, subject not exact match",
      query: "along",
      expected: ALONGERKEYWORD_RESULT,
    },
    {
      description:
        "Query length == minKeywordLength, subject not exact match, showLessFrequentlyCount non-zero",
      query: "alon",
      showLessFrequentlyCount: 1,
      expected: ALONGERKEYWORD_RESULT,
    },
    {
      description:
        "Query length == minKeywordLength + showLessFrequentlyCount, subject not exact match",
      query: "along",
      showLessFrequentlyCount: 1,
      expected: ALONGERKEYWORD_RESULT,
    },
    {
      description:
        "Query length < minKeywordLength + showLessFrequentlyCount, subject not exact match",
      query: "along",
      showLessFrequentlyCount: 2,
      expected: ALONGERKEYWORD_RESULT,
    },
    {
      description:
        "Query length == minKeywordLength + showLessFrequentlyCount, subject not exact match",
      query: "alonge",
      showLessFrequentlyCount: 2,
      expected: ALONGERKEYWORD_RESULT,
    },
  ];

  for (let {
    description,
    query,
    showLessFrequentlyCount,
    expected,
  } of TEST_DATA) {
    info(
      "Doing basic subtest: " +
        JSON.stringify({
          description,
          query,
          showLessFrequentlyCount,
          expected,
        })
    );

    if (typeof showLessFrequentlyCount == "number") {
      UrlbarPrefs.set("yelp.showLessFrequentlyCount", showLessFrequentlyCount);
    }

    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expected ? [QuickSuggestTestUtils.yelpResult(expected)] : [],
    });

    UrlbarPrefs.clear("yelp.showLessFrequentlyCount");
  }
});

add_task(async function telemetryType() {
  Assert.equal(
    QuickSuggest.getFeature("YelpSuggestions").getSuggestionTelemetryType({}),
    "yelp",
    "Telemetry type should be 'yelp'"
  );
});

// When sponsored suggestions are disabled, Yelp suggestions should be
// disabled.
add_task(async function sponsoredDisabled() {
  UrlbarPrefs.set("suggest.quicksuggest.nonsponsored", false);

  // First make sure the suggestion is added when non-sponsored
  // suggestions are enabled, if the rust is enabled.
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [QuickSuggestTestUtils.yelpResult(TOKYO_RESULT)],
  });

  // Now disable the pref.
  UrlbarPrefs.set("suggest.quicksuggest.sponsored", false);
  Assert.ok(
    !QuickSuggest.getFeature("YelpSuggestions").isEnabled,
    "Yelp should be disabled"
  );
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  UrlbarPrefs.set("suggest.quicksuggest.sponsored", true);
  UrlbarPrefs.clear("suggest.quicksuggest.nonsponsored");
  await QuickSuggestTestUtils.forceSync();

  // Make sure Yelp is enabled again.
  Assert.ok(
    QuickSuggest.getFeature("YelpSuggestions").isEnabled,
    "Yelp should be re-enabled"
  );
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [QuickSuggestTestUtils.yelpResult(TOKYO_RESULT)],
  });
});

// When Yelp-specific preferences are disabled, suggestions should not be
// added.
add_task(async function yelpSpecificPrefsDisabled() {
  const prefs = ["suggest.yelp", "yelp.featureGate"];
  for (const pref of prefs) {
    // First make sure the suggestion is added, if the rust is enabled.
    await check_results({
      context: createContext("ramen in tokyo", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [QuickSuggestTestUtils.yelpResult(TOKYO_RESULT)],
    });

    // Now disable the pref.
    UrlbarPrefs.set(pref, false);
    Assert.ok(
      !QuickSuggest.getFeature("YelpSuggestions").isEnabled,
      "Yelp should be disabled"
    );
    await check_results({
      context: createContext("ramen in tokyo", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });

    // Revert.
    UrlbarPrefs.set(pref, true);
    await QuickSuggestTestUtils.forceSync();

    // Make sure Yelp is enabled again.
    Assert.ok(
      QuickSuggest.getFeature("YelpSuggestions").isEnabled,
      "Yelp should be re-enabled"
    );
    await check_results({
      context: createContext("ramen in tokyo", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [QuickSuggestTestUtils.yelpResult(TOKYO_RESULT)],
    });
  }
});

// Check wheather the Yelp suggestions will be shown by the setup of Nimbus
// variable.
add_task(async function featureGate() {
  // Disable the fature gate.
  UrlbarPrefs.set("yelp.featureGate", false);
  await check_results({
    context: createContext("ramem in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  // Enable by Nimbus.
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    yelpFeatureGate: true,
  });
  await QuickSuggestTestUtils.forceSync();
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [QuickSuggestTestUtils.yelpResult(TOKYO_RESULT)],
  });
  await cleanUpNimbusEnable();

  // Enable locally.
  UrlbarPrefs.set("yelp.featureGate", true);
  await QuickSuggestTestUtils.forceSync();

  // Disable by Nimbus.
  const cleanUpNimbusDisable = await UrlbarTestUtils.initNimbusFeature({
    yelpFeatureGate: false,
  });
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });
  await cleanUpNimbusDisable();

  // Revert.
  UrlbarPrefs.set("yelp.featureGate", true);
  await QuickSuggestTestUtils.forceSync();
});

// Check wheather the Yelp suggestions will be shown as top_pick by the Nimbus
// variable.
add_task(async function yelpSuggestPriority() {
  // Enable by Nimbus.
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    yelpSuggestPriority: true,
  });
  await QuickSuggestTestUtils.forceSync();

  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        ...TOKYO_RESULT,
        isTopPick: true,
      }),
    ],
  });

  await cleanUpNimbusEnable();
  await QuickSuggestTestUtils.forceSync();

  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        ...TOKYO_RESULT,
        isTopPick: false,
      }),
    ],
  });
});

// Tests the `yelpSuggestNonPriorityIndex` Nimbus variable, which controls the
// group-relative suggestedIndex.
add_task(async function nimbusSuggestedIndex() {
  // When the Nimbus variable is defined, it should override the default
  // suggested index used for Yelp. We use -2 here since that's unlikely to ever
  // be the default Yelp index.
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    yelpSuggestNonPriorityIndex: -2,
  });
  await QuickSuggestTestUtils.forceSync();

  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        ...TOKYO_RESULT,
        isTopPick: false,
        suggestedIndex: -2,
      }),
    ],
  });

  await cleanUpNimbusEnable();
  await QuickSuggestTestUtils.forceSync();

  // When the Nimbus variable isn't defined, the suggested index should be the
  // default index used for Yelp, which is the sponsored suggestions index, 0.
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        ...TOKYO_RESULT,
        isTopPick: false,
        suggestedIndex: 0,
      }),
    ],
  });
});

// Tests the suggestedIndex if the browser.urlbar.showSearchSuggestionsFirst pref
// is false.
add_task(async function showSearchSuggestionsFirstDisabledSuggestedIndex() {
  info("Disable browser.urlbar.showSearchSuggestionsFirst pref");
  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        ...TOKYO_RESULT,
        isTopPick: false,
        suggestedIndex: -1,
      }),
    ],
  });

  info("Enable browser.urlbar.showSearchSuggestionsFirst pref");
  UrlbarPrefs.set("showSearchSuggestionsFirst", true);
  await check_results({
    context: createContext("ramen in tokyo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        ...TOKYO_RESULT,
        isTopPick: false,
        suggestedIndex: 0,
      }),
    ],
  });

  UrlbarPrefs.clear("showSearchSuggestionsFirst");
});

// Tests the "Not relevant" command: a dismissed suggestion shouldn't be added.
add_task(async function notRelevant() {
  await doDismissOneTest({
    result: QuickSuggestTestUtils.yelpResult(TOKYO_RESULT),
    command: "not_relevant",
    feature: QuickSuggest.getFeature("YelpSuggestions"),
    queriesForDismissals: [
      // Yelp suggestions are dismissed by URL excluding location, so all
      // "ramen in <valid location>" results should be dismissed.
      {
        query: "ramen in tokyo",
      },
      {
        query: "ramen in waterloo",
        expectedResults: [
          QuickSuggestTestUtils.yelpResult({
            url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Waterloo%2C+IA",
            title: "ramen in Waterloo, IA",
          }),
        ],
      },
    ],
    queriesForOthers: [
      {
        query: "alongerkeyword in tokyo",
        expectedResults: [
          QuickSuggestTestUtils.yelpResult({
            url: "https://www.yelp.com/search?find_desc=alongerkeyword&find_loc=Tokyo%2C+Tokyo-to",
            title: "alongerkeyword in Tokyo, Tokyo-to",
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
    result: QuickSuggestTestUtils.yelpResult(TOKYO_RESULT),
    command: "not_interested",
    feature: QuickSuggest.getFeature("YelpSuggestions"),
    pref: "suggest.yelp",
    queries: [
      {
        query: "ramen in tokyo",
      },
      {
        query: "alongerkeyword in tokyo",
        expectedResults: [
          QuickSuggestTestUtils.yelpResult({
            url: "https://www.yelp.com/search?find_desc=alongerkeyword&find_loc=Tokyo%2C+Tokyo-to",
            title: "alongerkeyword in Tokyo, Tokyo-to",
          }),
        ],
      },
    ],
  });
});

// Tests the "show less frequently" behavior.
add_task(async function showLessFrequently() {
  UrlbarPrefs.clear("yelp.showLessFrequentlyCount");
  UrlbarPrefs.clear("yelp.minKeywordLength");

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    yelpShowLessFrequentlyCap: 3,
  });

  let location = `${GEOLOCATION.city}, ${GEOLOCATION.region}`;
  let url = new URL("https://www.yelp.com/search");
  url.searchParams.set("find_desc", "best ramen");
  url.searchParams.set("find_loc", location);

  let result = QuickSuggestTestUtils.yelpResult({
    url: url.toString(),
    title: `best ramen in ${location}`,
  });

  const testData = [
    {
      input: "best ra",
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
      input: "best ram",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 8,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 9,
      },
    },
    {
      input: "best rame",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 9,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 10,
      },
    },
    {
      input: "best ramen",
      before: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 10,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 11,
      },
    },
  ];

  for (let { input, before, after } of testData) {
    let feature = QuickSuggest.getFeature("YelpSuggestions");

    await check_results({
      context: createContext(input, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [result],
    });

    Assert.equal(
      UrlbarPrefs.get("yelp.minKeywordLength"),
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
      expectedCountsByCall: {
        acknowledgeFeedback: 1,
        invalidateResultMenuCommands: after.canShowLessFrequently ? 0 : 1,
      },
    });

    Assert.equal(
      UrlbarPrefs.get("yelp.minKeywordLength"),
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
  UrlbarPrefs.clear("yelp.showLessFrequentlyCount");
  UrlbarPrefs.clear("yelp.minKeywordLength");
});

// The `Yelp` Rust provider should be passed to the Rust component when
// querying depending on whether Yelp suggestions are enabled.
add_task(async function rustProviders() {
  await doRustProvidersTests({
    searchString: "ramen in tokyo",
    tests: [
      {
        prefs: {
          "suggest.yelp": true,
        },
        expectedUrls: [
          "https://www.yelp.com/search?find_desc=ramen&find_loc=tokyo",
        ],
      },
      {
        prefs: {
          "suggest.yelp": false,
        },
        expectedUrls: [],
      },
    ],
  });

  UrlbarPrefs.clear("suggest.yelp");
  await QuickSuggestTestUtils.forceSync();
});

add_task(async function minKeywordLength_defaultPrefValue() {
  await doMinKeywordLengthTest({
    // expected min length: 5 (Nimbus value should override default pref value)
    prefUserValue: null,
    nimbusValue: 5,
    tests: [
      {
        query: "al",
        expected: null,
      },
      {
        query: "alo",
        expected: null,
      },
      {
        query: "alon",
        expected: null,
      },
      {
        query: "along",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "alongerkeyword",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "best a",
        expected: null,
      },
      {
        query: "best al",
        expected: {
          url: "https://www.yelp.com/search?find_desc=best+alongerkeyword&find_loc=Yokohama%2C+Kanagawa",
          title: "best alongerkeyword in Yokohama, Kanagawa",
        },
      },
      {
        query: "ab",
        expected: AB_RESULT,
      },
    ],
  });
});

add_task(async function minKeywordLength_smallerPrefUserValue() {
  await doMinKeywordLengthTest({
    // expected min length: 5 (pref user value)
    prefUserValue: 5,
    nimbusValue: 6,
    tests: [
      {
        query: "al",
        expected: null,
      },
      {
        query: "alo",
        expected: null,
      },
      {
        query: "alon",
        expected: null,
      },
      {
        query: "along",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "alongerkeyword",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "best a",
        expected: null,
      },
      {
        query: "best al",
        expected: {
          url: "https://www.yelp.com/search?find_desc=best+alongerkeyword&find_loc=Yokohama%2C+Kanagawa",
          title: "best alongerkeyword in Yokohama, Kanagawa",
        },
      },
      {
        query: "ab",
        expected: AB_RESULT,
      },
    ],
  });
});

add_task(async function minKeywordLength_largerPrefUserValue() {
  await doMinKeywordLengthTest({
    // expected min length: 6 (pref user value)
    prefUserValue: 6,
    nimbusValue: 5,
    tests: [
      {
        query: "al",
        expected: null,
      },
      {
        query: "alo",
        expected: null,
      },
      {
        query: "alon",
        expected: null,
      },
      {
        query: "along",
        expected: null,
      },
      {
        query: "alonge",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "alongerkeyword",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "best a",
        expected: null,
      },
      {
        query: "best al",
        expected: {
          url: "https://www.yelp.com/search?find_desc=best+alongerkeyword&find_loc=Yokohama%2C+Kanagawa",
          title: "best alongerkeyword in Yokohama, Kanagawa",
        },
      },
      {
        query: "ab",
        expected: AB_RESULT,
      },
    ],
  });
});

add_task(async function minKeywordLength_onlyPrefValue() {
  await doMinKeywordLengthTest({
    // expected min length: 5 (pref user value)
    prefUserValue: 5,
    nimbusValue: null,
    tests: [
      {
        query: "al",
        expected: null,
      },
      {
        query: "alo",
        expected: null,
      },
      {
        query: "alon",
        expected: null,
      },
      {
        query: "along",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "alongerkeyword",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "best a",
        expected: null,
      },
      {
        query: "best al",
        expected: {
          url: "https://www.yelp.com/search?find_desc=best+alongerkeyword&find_loc=Yokohama%2C+Kanagawa",
          title: "best alongerkeyword in Yokohama, Kanagawa",
        },
      },
      {
        query: "ab",
        expected: AB_RESULT,
      },
    ],
  });
});

add_task(async function minKeywordLength_noNimbusOrPrefUserValue() {
  await doMinKeywordLengthTest({
    // expected min length: 4 (pref default value)
    prefUserValue: null,
    nimbusValue: null,
    tests: [
      {
        query: "al",
        expected: null,
      },
      {
        query: "alo",
        expected: null,
      },
      {
        query: "alon",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "along",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "alongerkeyword",
        expected: ALONGERKEYWORD_RESULT,
      },
      {
        query: "best a",
        expected: null,
      },
      {
        query: "best al",
        expected: {
          url: "https://www.yelp.com/search?find_desc=best+alongerkeyword&find_loc=Yokohama%2C+Kanagawa",
          title: "best alongerkeyword in Yokohama, Kanagawa",
        },
      },
      {
        query: "ab",
        expected: AB_RESULT,
      },
    ],
  });
});

async function doMinKeywordLengthTest({ prefUserValue, nimbusValue, tests }) {
  // Set or clear the pref.
  let originalPrefUserValue = Services.prefs.prefHasUserValue(
    "browser.urlbar.yelp.minKeywordLength"
  )
    ? UrlbarPrefs.get("yelp.minKeywordLength")
    : null;
  if (typeof prefUserValue == "number") {
    UrlbarPrefs.set("yelp.minKeywordLength", prefUserValue);
  } else {
    UrlbarPrefs.clear("yelp.minKeywordLength");
  }

  // Set up Nimbus.
  let cleanUpNimbus;
  if (typeof nimbusValue == "number") {
    cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
      yelpMinKeywordLength: nimbusValue,
    });
  }

  for (let { query, expected } of tests) {
    info("Running min keyword length test with query: " + query);
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expected ? [QuickSuggestTestUtils.yelpResult(expected)] : [],
    });
  }

  await cleanUpNimbus?.();

  if (originalPrefUserValue === null) {
    UrlbarPrefs.clear("yelp.minKeywordLength");
  } else {
    UrlbarPrefs.set("yelp.minKeywordLength", originalPrefUserValue);
  }
}
