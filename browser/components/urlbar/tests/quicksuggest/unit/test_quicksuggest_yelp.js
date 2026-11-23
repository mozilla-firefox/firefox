/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests Yelp suggestions.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  UrlbarProviderSearchSuggestions:
    "moz-src:///browser/components/urlbar/UrlbarProviderSearchSuggestions.sys.mjs",
});

const { GEOLOCATION } = MerinoTestUtils;

const CONFIG_V2 = [
  {
    identifier: "engine_with_suggestions",
    base: {
      urls: {
        suggestions: {
          // This url will never respond with search suggestions.
          base: "https://mochi.test/",
          searchTermParamName: "q",
        },
      },
    },
  },
];

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "yelp-suggestions",
    attachment: {
      subjects: ["a service"],
      businessSubjects: ["the shop", "ab", "alongerkeyword", "1234"],
      preModifiers: ["best", "local"],
      postModifiers: ["delivery"],
      locationSigns: ["in", "nearby"],
      yelpModifiers: [],
      icon: "1234",
      score: 0.5,
    },
  },
  ...QuickSuggestTestUtils.geonamesRecords(),
  ...QuickSuggestTestUtils.geonamesAlternatesRecords(),
];

const TOKYO_RESULT = {
  url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Tokyo%2C+Tokyo-to",
  title: "the shop in Tokyo, Tokyo-to",
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
  // Add a search engine with search suggestions so sponsored suggestions can
  // be shown first. See the `quickSuggestSponsoredIndex` pref for more info.
  SearchTestUtils.setRemoteSettingsConfig(CONFIG_V2);
  await SearchService.init();
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["yelp.serviceResultDistinction", true],
    ],
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
      description: "Basic service subject",
      query: "best a service delivery in tokyo",
      expected: {
        url: "https://www.yelp.com/search?find_desc=best+a+service+delivery&find_loc=Tokyo%2C+Tokyo-to",
        titleL10n: {
          id: "firefox-suggest-yelp-service-title",
          args: {
            service: "best a service delivery in Tokyo, Tokyo-to",
          },
        },
      },
    },
    {
      description: "Basic business subject",
      query: "local the shop delivery in tokyo",
      expected: {
        url: "https://www.yelp.com/search?find_desc=local+the+shop+delivery&find_loc=Tokyo%2C+Tokyo-to",
        title: "local the shop delivery in Tokyo, Tokyo-to",
      },
    },
    {
      description: "With upper case",
      query: "LoCaL tHe ShOp dElIvErY iN tOkYo",
      expected: {
        url: "https://www.yelp.com/search?find_desc=LoCaL+tHe+ShOp+dElIvErY&find_loc=Tokyo%2C+Tokyo-to",
        title: "LoCaL tHe ShOp dElIvErY iN Tokyo, Tokyo-to",
      },
    },
    {
      description: "No specific location with location-sign",
      query: "the shop in",
      expected: {
        url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "the shop in Yokohama, Kanagawa",
      },
    },
    {
      description: "No specific location with location-modifier",
      query: "the shop nearby",
      expected: {
        url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "the shop nearby Yokohama, Kanagawa",
      },
    },
    {
      description: "Query too short, no subject exact match: th",
      query: "th",
      expected: null,
    },
    {
      description: "Query too short, no subject not exact match: the",
      query: "the",
      expected: null,
    },
    {
      description: "Query too short, no subject not exact match: the ",
      query: "the ",
      expected: {
        url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "the shop in Yokohama, Kanagawa",
      },
    },
    {
      description: "Query length == minKeywordLength, no subject exact the s",
      query: "the s",
      expected: {
        url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "the shop in Yokohama, Kanagawa",
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
        "Query length > minKeywordLength, subject exact match: the shop",
      query: "the shop",
      expected: {
        url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "the shop in Yokohama, Kanagawa",
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
      query: "local th",
      expected: {
        url: "https://www.yelp.com/search?find_desc=local+the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "local the shop in Yokohama, Kanagawa",
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
      query: "the shop",
      showLessFrequentlyCount: 1,
      expected: {
        url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Yokohama%2C+Kanagawa",
        title: "the shop in Yokohama, Kanagawa",
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
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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

// When relevant Yelp and Suggest prefs are disabled, suggestions should not be
// added.
add_task(async function prefsDisabled() {
  const prefs = [
    "suggest.yelp",
    "yelp.featureGate",
    "suggest.quicksuggest.all",
    "suggest.quicksuggest.sponsored",
    "quicksuggest.enabled",
  ];
  for (const pref of prefs) {
    info("Doing test with pref: " + pref);

    // First make sure the suggestion is added, if the rust is enabled.
    await check_results({
      context: createContext("the shop in tokyo", {
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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
      context: createContext("the shop in tokyo", {
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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
      context: createContext("the shop in tokyo", {
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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
    context: createContext("the shop in tokyo", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
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

// Tests the "Dismiss" command: a dismissed suggestion shouldn't be added.
add_task(async function dismiss() {
  await doDismissOneTest({
    result: QuickSuggestTestUtils.yelpResult(TOKYO_RESULT),
    command: "dismiss",
    feature: QuickSuggest.getFeature("YelpSuggestions"),
    queriesForDismissals: [
      // Yelp suggestions are dismissed by URL excluding location, so all
      // "ramen in <valid location>" results should be dismissed.
      {
        query: "the shop in tokyo",
      },
      {
        query: "the shop in waterloo",
        expectedResults: [
          QuickSuggestTestUtils.yelpResult({
            url: "https://www.yelp.com/search?find_desc=the+shop&find_loc=Waterloo%2C+IA",
            title: "the shop in Waterloo, IA",
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
    providers: [
      UrlbarProviderQuickSuggest.name,
      UrlbarProviderSearchSuggestions.name,
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
  url.searchParams.set("find_desc", "local the shop");
  url.searchParams.set("find_loc", location);

  let result = QuickSuggestTestUtils.yelpResult({
    url: url.toString(),
    title: `local the shop in ${location}`,
  });

  const testData = [
    {
      input: "local the ",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 0,
        minKeywordLength: 4,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 11,
      },
    },
    {
      input: "local the s",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 11,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 12,
      },
    },
    {
      input: "local the sh",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 12,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 13,
      },
    },
    {
      input: "local the sho",
      before: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 13,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 14,
      },
    },
    {
      input: "local the shop",
      before: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 14,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 15,
      },
    },
  ];

  for (let { input, before, after } of testData) {
    let feature = QuickSuggest.getFeature("YelpSuggestions");

    await check_results({
      context: createContext(input, {
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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
        updateResultMenuCommands: after.canShowLessFrequently ? 0 : 1,
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
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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
    searchString: "the shop in tokyo",
    tests: [
      {
        prefs: {
          "suggest.yelp": true,
        },
        expectedUrls: [
          "https://www.yelp.com/search?find_desc=the+shop&find_loc=tokyo",
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

// Check wheather the special title for service will be shown by the setup of
// Nimbus variable.
add_task(async function yelpServiceResultDistinction() {
  // Disable the pref.
  UrlbarPrefs.set("yelp.serviceResultDistinction", false);
  await check_results({
    context: createContext("a service", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        url: "https://www.yelp.com/search?find_desc=a+service&find_loc=Yokohama%2C+Kanagawa",
        title: "a service in Yokohama, Kanagawa",
      }),
    ],
  });

  // Enable by Nimbus.
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature({
    yelpServiceResultDistinction: true,
  });
  await QuickSuggestTestUtils.forceSync();
  await check_results({
    context: createContext("a service", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        url: "https://www.yelp.com/search?find_desc=a+service&find_loc=Yokohama%2C+Kanagawa",
        titleL10n: {
          id: "firefox-suggest-yelp-service-title",
          args: {
            service: "a service in Yokohama, Kanagawa",
          },
        },
      }),
    ],
  });
  await cleanUpNimbusEnable();

  // Enable locally.
  UrlbarPrefs.set("yelp.serviceResultDistinction", true);
  await QuickSuggestTestUtils.forceSync();

  // Disable by Nimbus.
  const cleanUpNimbusDisable = await UrlbarTestUtils.initNimbusFeature({
    yelpServiceResultDistinction: false,
  });
  await check_results({
    context: createContext("a service", {
      providers: [
        UrlbarProviderQuickSuggest.name,
        UrlbarProviderSearchSuggestions.name,
      ],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.yelpResult({
        url: "https://www.yelp.com/search?find_desc=a+service&find_loc=Yokohama%2C+Kanagawa",
        title: "a service in Yokohama, Kanagawa",
      }),
    ],
  });
  await cleanUpNimbusDisable();

  // Revert.
  UrlbarPrefs.set("yelp.serviceResultDistinction", true);
  await QuickSuggestTestUtils.forceSync();
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
        providers: [
          UrlbarProviderQuickSuggest.name,
          UrlbarProviderSearchSuggestions.name,
        ],
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
