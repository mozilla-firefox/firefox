/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests for Yelp suggestions served by the Suggest ML backend.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  MLSuggest: "resource:///modules/urlbar/private/MLSuggest.sys.mjs",
});

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "yelp-suggestions",
    attachment: {
      // The "coffee" subject is important: It's how `YelpSuggestions` looks up
      // the Yelp icon and score right now.
      subjects: ["coffee"],
      preModifiers: [],
      postModifiers: [],
      locationSigns: ["in", "nearby"],
      yelpModifiers: [],
      icon: "1234",
      score: 0.5,
    },
  },
  QuickSuggestTestUtils.geonamesRecord(),
];

const WATERLOO_RESULT = {
  url: "https://www.yelp.com/search?find_desc=burgers&find_loc=Waterloo%2C+IA",
  title: "burgers in Waterloo, IA",
};

const YOKOHAMA_RESULT = {
  url: "https://www.yelp.com/search?find_desc=burgers&find_loc=Yokohama%2C+Kanagawa",
  title: "burgers in Yokohama, Kanagawa",
};

let gSandbox;
let gMakeSuggestionsStub;

add_setup(async function init() {
  // Stub `MLSuggest`.
  gSandbox = sinon.createSandbox();
  gSandbox.stub(MLSuggest, "initialize");
  gSandbox.stub(MLSuggest, "shutdown");
  gMakeSuggestionsStub = gSandbox.stub(MLSuggest, "makeSuggestions");

  // Set up Rust Yelp suggestions that can be matched on the keyword "coffee".
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    prefs: [
      ["browser.ml.enable", true],
      ["quicksuggest.mlEnabled", true],
      ["suggest.quicksuggest.nonsponsored", true],
      ["suggest.quicksuggest.sponsored", true],
      ["yelp.mlEnabled", true],
    ],
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
  });

  await MerinoTestUtils.initGeolocation();
});

// Yelp ML should be disabled when the relevant prefs are disabled.
add_task(async function yelpDisabled() {
  gMakeSuggestionsStub.returns({ intent: "yelp_intent", subject: "burgers" });
  let expectedResult = makeExpectedResult(YOKOHAMA_RESULT);

  let tests = [
    // These disable the Yelp feature itself, including Rust suggestions.
    "suggest.quicksuggest.sponsored",
    "suggest.yelp",
    "yelp.featureGate",

    // These disable Yelp ML suggestions but leave the Yelp feature enabled.
    // This test doesn't add any Yelp data to remote settings, but if it did,
    // Yelp Rust suggestions would still be triggered.
    "yelp.mlEnabled",
    "browser.ml.enable",
    "quicksuggest.mlEnabled",

    // pref combinations
    {
      prefs: {
        "suggest.quicksuggest.sponsored": true,
        "suggest.quicksuggest.nonsponsored": true,
      },
      expected: true,
    },
    {
      prefs: {
        "suggest.quicksuggest.sponsored": true,
        "suggest.quicksuggest.nonsponsored": false,
      },
      expected: true,
    },
    {
      prefs: {
        "suggest.quicksuggest.sponsored": false,
        "suggest.quicksuggest.nonsponsored": true,
      },
      expected: false,
    },
    {
      prefs: {
        "suggest.quicksuggest.sponsored": false,
        "suggest.quicksuggest.nonsponsored": false,
      },
      expected: false,
    },
  ];
  for (let test of tests) {
    info("Starting subtest: " + JSON.stringify(test));

    let prefs;
    let expected;
    if (typeof test == "string") {
      // A string value is a pref name, and we'll set it to false and expect no
      // suggestions.
      prefs = { [test]: false };
      expected = false;
    } else {
      ({ prefs, expected } = test);
    }

    // Before setting the prefs, first make sure the suggestion is added.
    info("Doing search 1");
    await check_results({
      context: createContext("burgers", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [expectedResult],
    });

    // Also get the original pref values.
    let originalPrefs = Object.fromEntries(
      Object.keys(prefs).map(name => [name, UrlbarPrefs.get(name)])
    );

    // Now set the prefs.
    info("Setting prefs and doing search 2");
    for (let [name, value] of Object.entries(prefs)) {
      UrlbarPrefs.set(name, value);
    }
    await check_results({
      context: createContext("burgers", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expected ? [expectedResult] : [],
    });

    // Revert.
    for (let [name, value] of Object.entries(originalPrefs)) {
      UrlbarPrefs.set(name, value);
    }
    await QuickSuggestTestUtils.forceSync();

    // Make sure Yelp is added again.
    info("Doing search 3 after reverting the prefs");
    await check_results({
      context: createContext("burgers", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [expectedResult],
    });
  }
});

// Runs through a variety of mock intents.
add_task(async function intents() {
  let tests = [
    {
      desc: "subject with no location",
      ml: { intent: "yelp_intent", subject: "burgers" },
      expected: YOKOHAMA_RESULT,
    },
    {
      desc: "subject with null city and null state",
      ml: {
        intent: "yelp_intent",
        subject: "burgers",
        location: { city: null, state: null },
      },
      expected: YOKOHAMA_RESULT,
    },
    {
      desc: "subject with city",
      ml: {
        intent: "yelp_intent",
        subject: "burgers",
        location: { city: "Waterloo", state: null },
      },
      expected: WATERLOO_RESULT,
    },
    {
      desc: "subject with state abbreviation",
      ml: {
        intent: "yelp_intent",
        subject: "burgers",
        location: { city: null, state: "IA" },
      },
      expected: null,
    },
    {
      desc: "subject with state name",
      ml: {
        intent: "yelp_intent",
        subject: "burgers",
        location: { city: null, state: "Iowa" },
      },
      expected: {
        url: "https://www.yelp.com/search?find_desc=burgers&find_loc=Iowa",
        title: "burgers in Iowa",
      },
    },
    {
      desc: "subject with city and state",
      ml: {
        intent: "yelp_intent",
        subject: "burgers",
        location: { city: "Waterloo", state: "IA" },
      },
      expected: WATERLOO_RESULT,
    },
    {
      desc: "no subject with no location",
      ml: {
        intent: "yelp_intent",
        subject: "",
      },
      expected: null,
    },
    {
      desc: "no subject with null city and null state",
      ml: {
        intent: "yelp_intent",
        subject: "",
        location: { city: null, state: null },
      },
      expected: null,
    },
    {
      desc: "no subject with city",
      ml: {
        intent: "yelp_intent",
        subject: "",
        location: { city: "Waterloo", state: null },
      },
      expected: null,
    },
    {
      desc: "no subject with state",
      ml: {
        intent: "yelp_intent",
        subject: "",
        location: { city: null, state: "IA" },
      },
      expected: null,
    },
    {
      desc: "no subject with city and state",
      ml: {
        intent: "yelp_intent",
        subject: "",
        location: { city: "Waterloo", state: "IA" },
      },
      expected: null,
    },
    {
      desc: "unrecognized intent",
      ml: { intent: "unrecognized_intent" },
      expected: null,
    },
    {
      desc: "only Rust returns a suggestion",
      query: "coffee",
      ml: null,
      expected: {
        source: "rust",
        provider: "Yelp",
        url: "https://www.yelp.com/search?find_desc=coffee&find_loc=Yokohama%2C+Kanagawa",
        title: "coffee in Yokohama, Kanagawa",
      },
    },

    {
      desc: "both ML and Rust return a suggestion",
      query: "coffee",
      ml: {
        intent: "yelp_intent",
        subject: "coffee",
        location: { city: "Waterloo", state: null },
      },
      expected: {
        url: "https://www.yelp.com/search?find_desc=coffee&find_loc=Waterloo%2C+IA",
        title: "coffee in Waterloo, IA",
      },
    },
  ];

  for (let { desc, ml, expected, query = "test" } of tests) {
    info("Doing subtest: " + JSON.stringify({ desc, query, ml }));

    // Do a query with ML enabled. If the expected result is from Rust, the
    // query shouldn't return any results because *only* ML results are returned
    // when ML is enabled.
    gMakeSuggestionsStub.returns(ml);
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches:
        expected && expected.source != "rust"
          ? [makeExpectedResult(expected)]
          : [],
    });

    // If the expected result is from Rust, disable ML and query again to make
    // sure it matches.
    if (expected?.source == "rust") {
      UrlbarPrefs.set("yelp.mlEnabled", false);
      await check_results({
        context: createContext(query, {
          providers: [UrlbarProviderQuickSuggest.name],
          isPrivate: false,
        }),
        matches: [makeExpectedResult(expected)],
      });
      UrlbarPrefs.set("yelp.mlEnabled", true);
    }
  }
});

// The search string passed in to `MLSuggest.makeSuggestions()` should be
// trimmed and lowercased.
add_task(async function searchString() {
  let searchStrings = [];
  gMakeSuggestionsStub.callsFake(str => {
    searchStrings.push(str);
    return {
      intent: "yelp_intent",
      subject: "burgers",
    };
  });

  await check_results({
    context: createContext("  AaA   bBb     CcC   ", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult(YOKOHAMA_RESULT)],
  });

  Assert.deepEqual(
    searchStrings,
    ["aaa   bbb     ccc"],
    "The search string passed in to MLSuggest should be trimmed and lowercased"
  );
});

// The metadata cache should be populated from the "coffee" Rust suggestion when
// it's present in remote settings.
add_task(async function cache_fromRust() {
  await doCacheTest({
    expectedScore: REMOTE_SETTINGS_RECORDS[0].attachment.score,
    expectedRust: {
      source: "rust",
      provider: "Yelp",
      url: "https://www.yelp.com/search?find_desc=coffee&find_loc=Yokohama%2C+Kanagawa",
      title: "coffee in Yokohama, Kanagawa",
    },
  });
});

// The metadata cache should be populated with default values when the "coffee"
// Rust suggestion is not present in remote settings.
add_task(async function cache_defaultValues() {
  await QuickSuggestTestUtils.setRemoteSettingsRecords([
    QuickSuggestTestUtils.geonamesRecord(),
  ]);
  await doCacheTest({
    // This value is hardcoded in `YelpSuggestions` as the default.
    expectedScore: 0.25,
    expectedRust: null,
  });
  await QuickSuggestTestUtils.setRemoteSettingsRecords(REMOTE_SETTINGS_RECORDS);
});

async function doCacheTest({ expectedScore, expectedRust }) {
  // Do a search with ML disabled to verify the Rust suggestion is matched as
  // expected.
  info("Doing search with ML disabled");
  UrlbarPrefs.set("yelp.mlEnabled", false);
  await check_results({
    context: createContext("coffee", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: expectedRust ? [makeExpectedResult(expectedRust)] : [],
  });
  UrlbarPrefs.set("yelp.mlEnabled", true);

  gMakeSuggestionsStub.returns({
    intent: "yelp_intent",
    subject: "burgers",
    location: { city: "Waterloo", state: null },
  });

  // Stub `YelpSuggestions.makeResult()` so we can get the suggestion object
  // passed into it.
  let passedSuggestion;
  let feature = QuickSuggest.getFeature("YelpSuggestions");
  let stub = gSandbox
    .stub(feature, "makeResult")
    .callsFake((queryContext, suggestion, searchString) => {
      passedSuggestion = suggestion;
      return stub.wrappedMethod.call(
        feature,
        queryContext,
        suggestion,
        searchString
      );
    });

  // Do a search with ML enabled.
  info("Doing search with ML enabled");
  feature._test_invalidateMetadataCache();
  await check_results({
    context: createContext("test", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult(WATERLOO_RESULT)],
  });

  stub.restore();

  // The score of the ML suggestion passed into `makeResult()` should have been
  // taken from the metadata cache.
  Assert.ok(
    passedSuggestion,
    "makeResult should have been called and passed the suggestion"
  );
  Assert.equal(
    passedSuggestion.score,
    expectedScore,
    "The suggestion should have borrowed its score from the Rust 'coffee' suggestion"
  );
}

// Tests the "Not relevant" command: a dismissed suggestion shouldn't be added.
add_task(async function notRelevant() {
  let burgersIntent = { intent: "yelp_intent", subject: "burgers" };
  let waterlooIntent = {
    intent: "yelp_intent",
    subject: "burgers",
    location: { city: "Waterloo" },
  };

  gMakeSuggestionsStub.returns(burgersIntent);
  let result = makeExpectedResult(YOKOHAMA_RESULT);

  info("Doing initial search to verify the suggestion is matched");
  await check_results({
    context: createContext("burgers", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [result],
  });

  let dismissalPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );
  triggerCommand({
    result,
    command: "not_relevant",
    feature: QuickSuggest.getFeature("YelpSuggestions"),
    expectedCountsByCall: {
      removeResult: 1,
    },
  });
  await dismissalPromise;

  Assert.ok(
    await QuickSuggest.isResultDismissed(result),
    "The result should be dismissed"
  );

  info("Doing search for dismissed suggestion");
  await check_results({
    context: createContext("burgers", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  // Yelp suggestions are dismissed by URL excluding location, so all
  // "ramen in <valid location>" results should be dismissed.
  gMakeSuggestionsStub.returns(waterlooIntent);
  await check_results({
    context: createContext("burgers in waterloo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  info("Doing search for a suggestion that wasn't dismissed");
  gMakeSuggestionsStub.returns({ intent: "yelp_intent", subject: "ramen" });
  await check_results({
    context: createContext("ramen", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      makeExpectedResult({
        url: "https://www.yelp.com/search?find_desc=ramen&find_loc=Yokohama%2C+Kanagawa",
        title: "ramen in Yokohama, Kanagawa",
      }),
    ],
  });

  info("Clearing dismissed suggestions");
  await QuickSuggest.clearDismissedSuggestions();

  info("Doing search for un-dismissed suggestion");
  gMakeSuggestionsStub.returns(burgersIntent);
  await check_results({
    context: createContext("burgers", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [result],
  });
  gMakeSuggestionsStub.returns(waterlooIntent);
  await check_results({
    context: createContext("burgers in waterloo", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [makeExpectedResult(WATERLOO_RESULT)],
  });
});

function makeExpectedResult({
  url,
  title,
  source = "ml",
  provider = "yelp_intent",
  originalUrl = undefined,
  displayUrl = undefined,
}) {
  return QuickSuggestTestUtils.yelpResult({
    url,
    title,
    source,
    provider,
    originalUrl,
    displayUrl,
  });
}
