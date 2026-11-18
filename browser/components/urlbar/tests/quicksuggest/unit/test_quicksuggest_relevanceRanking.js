/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests relevance ranking integration with UrlbarProviderQuickSuggest.

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentRelevancyManager:
    "moz-src:///toolkit/components/contentrelevancy/ContentRelevancyManager.sys.mjs",
  InterestVector:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustRelevancy.sys.mjs",
});

const PREF_CONTENT_RELEVANCY_ENABLED = "toolkit.contentRelevancy.enabled";
const PREF_RANKING_MODE = "browser.urlbar.quicksuggest.rankingMode";

function makeTestSuggestions() {
  return [
    {
      title: "suggestion_about_education",
      categories: [6], // "Education"
      score: 0.2,
    },
    {
      title: "suggestion_about_animals",
      categories: [1], // "Animals"
      score: 0.2,
    },
  ];
}

function makeTestSuggestionsWithInvalidCategories() {
  return [
    {
      title: "suggestion",
      categories: [-1], // "Education"
      score: 0.2,
    },
  ];
}

const MERINO_SUGGESTIONS = [
  {
    provider: "adm",
    full_keyword: "amp",
    title: "Amp Suggestion",
    url: "https://example.com/amp",
    icon: null,
    impression_url: "https://example.com/amp-impression",
    click_url: "https://example.com/amp-click",
    block_id: 1,
    advertiser: "Amp",
    iab_category: "22 - Shopping",
    is_sponsored: true,
    categories: [1], // Animals
    score: 0.3,
    custom_details: {
      amp: {
        suggestion_id: "amp-suggestion-id",
      },
    },
  },
  {
    title: "Wikipedia Suggestion",
    url: "https://example.com/wikipedia",
    provider: "wikipedia",
    full_keyword: "wikipedia",
    icon: null,
    block_id: 0,
    advertiser: "dynamic-Wikipedia",
    is_sponsored: false,
    categories: [6], // Education
    score: 0.23,
  },
];

const SEARCH_STRING = "frab";

const EXPECTED_AMP_RESULT = QuickSuggestTestUtils.ampResult({
  source: "merino",
  provider: "adm",
  requestId: "request_id",
  suggestedIndex: -1,
});
const EXPECTED_WIKIPEDIA_RESULT = QuickSuggestTestUtils.wikipediaResult({
  source: "merino",
  provider: "wikipedia",
  telemetryType: "wikipedia",
});

let gSandbox;

add_setup(async () => {
  // FOG needs a profile directory to put its data in.
  do_get_profile();

  // FOG needs to be initialized in order for data to flow.
  Services.fog.initializeFOG();

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    merinoSuggestions: MERINO_SUGGESTIONS,
    prefs: [
      ["suggest.quicksuggest.all", true],
      ["suggest.quicksuggest.sponsored", true],

      // Turn off higher-placement sponsored so this test doesn't need to worry
      // about best matches.
      ["quicksuggest.ampTopPickCharThreshold", 0],
    ],
  });
  gSandbox = sinon.createSandbox();

  const fakeStore = {
    close: gSandbox.fake(),
    userInterestVector: gSandbox.stub(),
  };
  const rustRelevancyStore = {
    init: gSandbox.fake.returns(fakeStore),
  };
  fakeStore.userInterestVector.resolves(
    new lazy.InterestVector({
      animals: 0,
      arts: 0,
      autos: 0,
      business: 0,
      career: 0,
      education: 50,
      fashion: 0,
      finance: 0,
      food: 0,
      government: 0,
      hobbies: 0,
      home: 0,
      news: 0,
      realEstate: 0,
      society: 0,
      sports: 0,
      tech: 0,
      travel: 0,
      inconclusive: 0,
    })
  );

  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, true);
  lazy.ContentRelevancyManager.init(rustRelevancyStore);

  registerCleanupFunction(() => {
    lazy.ContentRelevancyManager.uninit();
    Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
    gSandbox.restore();
  });
});

add_task(async function test_interest_mode() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "interest");

  const suggestions = makeTestSuggestions();
  await applyRanking(suggestions);

  Assert.greater(
    suggestions[0].score,
    0.2,
    "The score should be boosted for relevant suggestions"
  );
  Assert.less(
    suggestions[1].score,
    0.2,
    "The score should be lowered for irrelevant suggestion"
  );

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function test_default_mode() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "default");

  const suggestions = makeTestSuggestions();
  await applyRanking(suggestions);

  Assert.equal(
    suggestions[0].score,
    0.2,
    "The score should be unchanged for the default mode"
  );
  Assert.equal(
    suggestions[1].score,
    0.2,
    "The score should be unchanged for the default mode"
  );

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function test_random_mode() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "random");

  const suggestions = makeTestSuggestions();
  await applyRanking(suggestions);

  for (let s of suggestions) {
    Assert.equal(typeof s.score, "number", "Suggestion should have a score");
    Assert.greaterOrEqual(s.score, 0, "Suggestion score should be >= 0");
    Assert.lessOrEqual(s.score, 1, "Suggestion score should be <= 1");
    Assert.notEqual(
      s.score,
      0.2,
      "Suggestion score should be different from its initial value (probably!)"
    );
  }

  let uniqueScores = new Set(suggestions.map(s => s.score));
  Assert.equal(
    uniqueScores.size,
    suggestions.length,
    "Suggestion scores should be unique (probably!)"
  );

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function test_default_mode_end2end() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "default");

  let context = createContext(SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  await check_results({
    context,
    matches: [EXPECTED_AMP_RESULT],
  });

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function test_interest_mode_end2end() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "interest");

  let context = createContext(SEARCH_STRING, {
    providers: [UrlbarProviderQuickSuggest.name],
    isPrivate: false,
  });

  await check_results({
    context,
    matches: [EXPECTED_WIKIPEDIA_RESULT],
  });

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function test_telemetry_interest_mode() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "interest");

  Services.fog.testResetFOG();

  Assert.equal(null, Glean.suggestRelevance.status.success.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.status.failure.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.outcome.boosted.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.outcome.decreased.testGetValue());

  const suggestions = makeTestSuggestions();
  await applyRanking(suggestions);

  // The scoring should succeed for both suggestions with one boosted score
  // and one decreased score.
  Assert.equal(2, Glean.suggestRelevance.status.success.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.status.failure.testGetValue());
  Assert.equal(1, Glean.suggestRelevance.outcome.boosted.testGetValue());
  Assert.equal(1, Glean.suggestRelevance.outcome.decreased.testGetValue());

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function test_telemetry_interest_mode_with_failures() {
  Services.prefs.setStringPref(PREF_RANKING_MODE, "interest");

  Services.fog.testResetFOG();

  Assert.equal(null, Glean.suggestRelevance.status.success.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.status.failure.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.outcome.boosted.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.outcome.decreased.testGetValue());

  const suggestions = makeTestSuggestionsWithInvalidCategories();
  await applyRanking(suggestions);

  // The scoring should fail.
  Assert.equal(null, Glean.suggestRelevance.status.success.testGetValue());
  Assert.equal(1, Glean.suggestRelevance.status.failure.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.outcome.boosted.testGetValue());
  Assert.equal(null, Glean.suggestRelevance.outcome.decreased.testGetValue());

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
});

add_task(async function offline_interest_mode_end2end() {
  // Interest mode should return the suggestion whose category has the largest
  // interest vector value: the Education suggestion.
  await doOfflineTest({
    mode: "interest",
    expectedResultArgs: {
      url: "https://example.com/6-education",
      title: "Suggestion with category 6 (Education)",
    },
  });
});

add_task(async function offline_default_mode_end2end() {
  // Default mode should return the first suggestion with the highest score,
  // which is just the first suggestion returned by the backend since they all
  // have the same score.
  await doOfflineTest({
    mode: "default",
    expectedResultArgs: {
      url: "https://example.com/no-categories",
      title: "Suggestion with no categories",
    },
  });
});

async function doOfflineTest({ mode, expectedResultArgs }) {
  // Turn off Merino.
  UrlbarPrefs.set("quicksuggest.online.enabled", false);

  Services.prefs.setStringPref(PREF_RANKING_MODE, mode);

  // TODO: For now we stub `query()` on the Rust backend so that it returns AMP
  // suggestions that have the `categories` property. Once the Rust component
  // actually returns AMP suggestions with `categories`, we should be able to
  // remove this and instead pass appropriate AMP data in the setup task to
  // `QuickSuggestTestUtils.ensureQuickSuggestInit()`. When we do, make sure all
  // suggestions have the same keyword! We want Rust to return all of them in
  // response to a single query so that they are sorted and chosen by relevancy
  // ranking.
  let sandbox = sinon.createSandbox();
  let queryStub = sandbox.stub(QuickSuggest.rustBackend, "query");
  queryStub.returns([
    mockRustAmpSuggestion({
      keyword: "offline",
      title: "Suggestion with no categories",
      url: "https://example.com/no-categories",
      categories: [],
    }),
    mockRustAmpSuggestion({
      keyword: "offline",
      url: "https://example.com/6-education",
      title: "Suggestion with category 6 (Education)",
      categories: [6],
    }),
    mockRustAmpSuggestion({
      keyword: "offline",
      title: "Suggestion with category 1 (Animals)",
      url: "https://example.com/1-animals",
      categories: [1],
    }),
  ]);

  await check_results({
    context: createContext("offline", {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [
      QuickSuggestTestUtils.ampResult({
        ...expectedResultArgs,
        keyword: "offline",
        suggestedIndex: -1,
      }),
    ],
  });

  Services.prefs.clearUserPref(PREF_RANKING_MODE);
  UrlbarPrefs.clear("quicksuggest.online.enabled");
  sandbox.restore();
}

async function applyRanking(suggestions) {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  let quickSuggestProviderInstance = providersManager.getProvider(
    UrlbarProviderQuickSuggest.name
  );
  for (let s of suggestions) {
    await quickSuggestProviderInstance._test_applyRanking(s);
  }
}

function mockRustAmpSuggestion({ keyword, url, title, categories }) {
  let suggestion = QuickSuggestTestUtils.ampRemoteSettings({
    url,
    title,
    keywords: [keyword],
  });
  return {
    ...suggestion,
    rawUrl: suggestion.url,
    impressionUrl: suggestion.impression_url,
    clickUrl: suggestion.click_url,
    blockId: suggestion.id,
    iabCategory: suggestion.iab_category,
    suggestionId: suggestion.suggestion_id,
    icon: null,
    fullKeyword: keyword,
    source: "rust",
    provider: "Amp",
    categories,
  };
}
