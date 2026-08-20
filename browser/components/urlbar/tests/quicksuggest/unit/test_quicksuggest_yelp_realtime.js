/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests Market suggestions.

"use strict";

const OPT_IN_REMOTE_SETTINGS = [
  {
    type: "dynamic-suggestions",
    suggestion_type: "yelpRealtime_opt_in",
    attachment: [
      {
        keywords: ["coffee"],
      },
    ],
  },
];

const YELP_MERINO_SINGLE = [
  {
    provider: "yelp",
    is_sponsored: true,
    custom_details: {
      yelp: {
        values: [
          {
            name: "MochaZilla. - Toronto",
            url: "https://example.com/mochazilla-toronto",
            image_url: "https://example.com/mochazilla.jpg",
            address: "123 Firefox Avenue Toronto",
            pricing: "$$",
            rating: 4.8,
            review_count: 989,
            business_hours: [
              {
                open: [
                  { is_overnight: false, start: "0700", end: "1500", day: 0 },
                ],
                hours_type: "REGULAR",
                is_open_now: false,
              },
            ],
          },
        ],
      },
    },
  },
];

add_setup(async function init() {
  // Disable search suggestions so we don't hit the network.
  await SearchService.init();
  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: OPT_IN_REMOTE_SETTINGS,
    merinoSuggestions: YELP_MERINO_SINGLE,
    prefs: [
      ["yelpRealtime.featureGate", true],
      ["suggest.yelpRealtime", true],
      ["suggest.quicksuggest.sponsored", true],
    ],
  });
});

add_task(async function telemetry_type_on_realtime() {
  Assert.equal(
    QuickSuggest.getFeature(
      "YelpRealtimeSuggestions"
    ).getSuggestionTelemetryType({}),
    "yelpRealtime",
    "Telemetry type should be 'yelpRealtime'"
  );
});

add_task(async function disabledPrefs() {
  let prefs = [
    "quicksuggest.enabled",
    "suggest.yelpRealtime",
    "suggest.quicksuggest.all",
  ];

  for (let pref of prefs) {
    info("Testing pref: " + pref);

    // First make sure the suggestion is added.
    await check_results({
      context: createContext("coffee", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [yelpMerinoResult()],
    });

    // Now disable them.
    UrlbarPrefs.set(pref, false);
    await check_results({
      context: createContext("coffee", {
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

add_task(async function not_interested_on_realtime() {
  await doDismissAllTest({
    result: yelpMerinoResult(),
    command: "not_interested",
    feature: QuickSuggest.getFeature("YelpRealtimeSuggestions"),
    pref: "suggest.yelpRealtime",
    queries: [{ query: "coffee" }],
  });
});

add_task(async function show_less_frequently_on_realtime() {
  UrlbarPrefs.clear("yelpRealtime.showLessFrequentlyCount");
  UrlbarPrefs.clear("yelpRealtime.minKeywordLength");

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    realtimeMinKeywordLength: 0,
    realtimeShowLessFrequentlyCap: 3,
  });

  let result = yelpMerinoResult();

  const testData = [
    {
      input: "cof",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 0,
        minKeywordLength: 0,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 4,
      },
    },
    {
      input: "coffe",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 1,
        minKeywordLength: 4,
      },
      after: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 6,
      },
    },
    {
      input: "coffee",
      before: {
        canShowLessFrequently: true,
        showLessFrequentlyCount: 2,
        minKeywordLength: 6,
      },
      after: {
        canShowLessFrequently: false,
        showLessFrequentlyCount: 3,
        minKeywordLength: 7,
      },
    },
  ];

  for (let { input, before, after } of testData) {
    let feature = QuickSuggest.getFeature("YelpRealtimeSuggestions");

    await check_results({
      context: createContext(input, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [result],
    });

    Assert.equal(
      UrlbarPrefs.get("yelpRealtime.minKeywordLength"),
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
      UrlbarPrefs.get("yelpRealtime.minKeywordLength"),
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
  UrlbarPrefs.clear("yelpRealtime.showLessFrequentlyCount");
  UrlbarPrefs.clear("yelpRealtime.minKeywordLength");
});

add_task(async function opt_in_on_opt_in_prompt() {
  setupOptInPromptTest();

  let input = "coffee";
  let optInResult = yelpOptInResult();
  let merinoResult = yelpMerinoResult();

  info("Confirm whether the opt-in result is shown");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [optInResult],
  });

  info("Trigger opt_in command");
  triggerCommand({
    result: optInResult,
    feature: QuickSuggest.getFeature("YelpRealtimeSuggestions"),
    command: "opt_in",
    searchString: input,
    expectedCountsByCall: {
      startQuery: 1,
    },
  });

  info("Check the prefs after triggering");
  Assert.ok(UrlbarPrefs.get("quicksuggest.online.enabled"));

  info("Check the result after triggering");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [merinoResult],
  });
});

add_task(async function not_now_on_opt_in_prompt() {
  setupOptInPromptTest();

  let input = "coffee";
  let beforeNotNowResult = yelpOptInResult();
  let afterNotNowResult = yelpOptInResult({ dismissButton: true });

  info("Confirm whether the opt-in result is shown");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [beforeNotNowResult],
  });

  info("Trigger not_now command");
  triggerCommand({
    result: beforeNotNowResult,
    feature: QuickSuggest.getFeature("YelpRealtimeSuggestions"),
    command: "not_now",
    searchString: input,
    expectedCountsByCall: {
      removeResult: 1,
    },
  });

  info("Check the prefs after triggering");
  Assert.greaterOrEqual(
    Date.now() / 1000,
    UrlbarPrefs.get("quicksuggest.realtimeOptIn.notNowTimeSeconds")
  );
  Assert.ok(
    UrlbarPrefs.get("quicksuggest.realtimeOptIn.notNowTypes").has(
      "yelpRealtime"
    )
  );

  info("Check the result after triggering");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });

  info("Simulate the passage of some days to show the result again");
  let notNowTimeSeconds = UrlbarPrefs.get(
    "quicksuggest.realtimeOptIn.notNowTimeSeconds"
  );
  let notNowReshowAfterPeriodDays = UrlbarPrefs.get(
    "quicksuggest.realtimeOptIn.notNowReshowAfterPeriodDays"
  );
  let newTime =
    notNowTimeSeconds - notNowReshowAfterPeriodDays * 24 * 60 * 60 - 60;
  UrlbarPrefs.set("quicksuggest.realtimeOptIn.notNowTimeSeconds", newTime);

  info("Check the result again");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [afterNotNowResult],
  });
});

add_task(async function dismiss_on_opt_in_prompt() {
  setupOptInPromptTest();

  let input = "coffee";
  let result = yelpOptInResult();

  info("Confirm whether the opt-in result is shown");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [result],
  });

  info("Trigger dismiss command");
  triggerCommand({
    result,
    feature: QuickSuggest.getFeature("YelpRealtimeSuggestions"),
    command: "dismiss",
    searchString: input,
    expectedCountsByCall: {
      removeResult: 1,
    },
  });

  info("Check the prefs after triggering");
  Assert.ok(
    UrlbarPrefs.get("quicksuggest.realtimeOptIn.dismissTypes").has(
      "yelpRealtime"
    )
  );

  info("Check the result after triggering");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });
});

add_task(async function not_interested_on_opt_in_prompt() {
  setupOptInPromptTest();

  let input = "coffee";
  let result = yelpOptInResult();

  info("Confirm whether the opt-in result is shown");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [result],
  });

  info("Trigger not_interested command");
  triggerCommand({
    result,
    feature: QuickSuggest.getFeature("YelpRealtimeSuggestions"),
    command: "not_interested",
    searchString: input,
    expectedCountsByCall: {
      removeResult: 1,
    },
  });

  info("Check the prefs after triggering");
  Assert.ok(!UrlbarPrefs.get("suggest.realtimeOptIn"));

  info("Check the result after triggering");
  await check_results({
    context: createContext(input, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: [],
  });
});

function setupOptInPromptTest() {
  UrlbarPrefs.set("suggest.realtimeOptIn", true);
  UrlbarPrefs.set("quicksuggest.online.available", true);
  UrlbarPrefs.set("quicksuggest.online.enabled", false);
  UrlbarPrefs.set("quicksuggest.realtimeOptIn.dismissTypes", "");
  UrlbarPrefs.set("quicksuggest.realtimeOptIn.notNowTimeSeconds", 0);
  UrlbarPrefs.set("quicksuggest.realtimeOptIn.notNowTypes", "");
}

function yelpOptInResult({ dismissButton = false } = {}) {
  let dismissOrNotNowButton = dismissButton
    ? {
        command: "dismiss",
        l10n: {
          id: "urlbar-result-realtime-opt-in-dismiss",
        },
      }
    : {
        command: "not_now",
        l10n: {
          id: "urlbar-result-realtime-opt-in-not-now",
        },
      };
  return {
    type: UrlbarShared.RESULT_TYPE.TIP,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    heuristic: false,
    isBestMatch: true,
    payload: {
      source: "rust",
      provider: "Dynamic",
      type: "realtime_opt_in",
      suggestionType: "yelpRealtime_opt_in",
      telemetryType: "yelpRealtime_opt_in",
      isSponsored: true,
      icon: "chrome://browser/skin/illustrations/yelpRealtime-opt-in.svg",
      titleL10n: {
        id: "urlbar-result-yelp-realtime-opt-in-title",
      },
      descriptionL10n: {
        id: "urlbar-result-yelp-realtime-opt-in-description",
        parseMarkup: true,
      },
      descriptionLearnMoreTopic: "firefox-suggest",
      buttons: [
        {
          command: "opt_in",
          l10n: {
            id: "urlbar-result-realtime-opt-in-allow",
          },
          input: "coffee",
          attributes: {
            primary: "",
          },
        },
        {
          ...dismissOrNotNowButton,
          menu: [
            {
              name: "not_interested",
              l10n: {
                id: "urlbar-result-realtime-opt-in-dismiss-all2",
              },
            },
          ],
        },
      ],
    },
  };
}

function yelpMerinoResult() {
  return {
    type: UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    heuristic: false,
    isBestMatch: true,
    payload: {
      source: "merino",
      provider: "yelp",
      dynamicType: "realtime-yelpRealtime",
      telemetryType: "yelpRealtime",
      isSponsored: true,
      items: [
        {
          name: "MochaZilla. - Toronto",
          url: "https://example.com/mochazilla-toronto",
          image_url: "https://example.com/mochazilla.jpg",
          address: "123 Firefox Avenue Toronto",
          pricing: "$$",
          rating: 4.8,
          review_count: 989,
          business_hours: [
            {
              open: [
                { is_overnight: false, start: "0700", end: "1500", day: 0 },
              ],
              hours_type: "REGULAR",
              is_open_now: false,
            },
          ],
        },
      ],
    },
  };
}
