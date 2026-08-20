/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests flight status suggestions.

"use strict";

const TEST_MERINO_SINGLE = [
  {
    provider: "flightaware",
    is_sponsored: false,
    score: 0,
    title: "Flight Suggestion",
    custom_details: {
      flightaware: {
        values: [
          {
            flight_number: "flight",
            airline: {
              name: null,
              code: null,
              icon: null,
            },
            origin: {
              city: "Origin",
              code: "O",
            },
            destination: {
              city: "Destination",
              code: "D",
            },
            departure: {
              scheduled_time: "2025-09-17T14:05:00Z",
            },
            arrival: {
              scheduled_time: "2025-09-17T18:30:00Z",
            },
            status: "Scheduled",
            url: "https://example.com/A1",
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
    merinoSuggestions: TEST_MERINO_SINGLE,
    prefs: [
      ["flightStatus.featureGate", true],
      ["suggest.flightStatus", true],
      ["suggest.quicksuggest.all", true],
    ],
  });
});

add_task(async function telemetry_type() {
  Assert.equal(
    QuickSuggest.getFeature(
      "FlightStatusSuggestions"
    ).getSuggestionTelemetryType({}),
    "flights",
    "Telemetry type should be 'flights'"
  );
});

add_task(async function disabledPrefs() {
  let prefs = [
    "quicksuggest.enabled",
    "suggest.flightStatus",
    "suggest.quicksuggest.all",
  ];

  for (let pref of prefs) {
    info("Testing pref: " + pref);

    // First make sure the suggestion is added.
    await check_results({
      context: createContext("test", {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [merinoResult()],
    });

    // Now disable them.
    UrlbarPrefs.set(pref, false);
    await check_results({
      context: createContext("test", {
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
    result: merinoResult(),
    command: "not_interested",
    feature: QuickSuggest.getFeature("FlightStatusSuggestions"),
    pref: "suggest.flightStatus",
    queries: [{ query: "a1" }],
  });
});

add_task(async function show_less_frequently() {
  UrlbarPrefs.clear("flightStatus.showLessFrequentlyCount");
  UrlbarPrefs.clear("flightStatus.minKeywordLength");

  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature({
    realtimeMinKeywordLength: 0,
    realtimeShowLessFrequentlyCap: 3,
  });

  let result = merinoResult();

  const testData = [
    {
      input: "fli",
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
      input: "fligh",
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
      input: "flight",
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
    let feature = QuickSuggest.getFeature("FlightStatusSuggestions");

    await check_results({
      context: createContext(input, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [result],
    });

    Assert.equal(
      UrlbarPrefs.get("flightStatus.minKeywordLength"),
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
      UrlbarPrefs.get("flightStatus.minKeywordLength"),
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
  UrlbarPrefs.clear("flightStatus.showLessFrequentlyCount");
  UrlbarPrefs.clear("flightStatus.minKeywordLength");
});

function merinoResult() {
  return {
    type: UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    isBestMatch: true,
    heuristic: false,
    payload: {
      source: "merino",
      provider: "flightaware",
      dynamicType: "realtime-flightStatus",
      telemetryType: "flights",
      isSponsored: false,
      items: [
        {
          flight_number: "flight",
          airline: {
            name: null,
            code: null,
            icon: null,
          },
          origin: {
            city: "Origin",
            code: "O",
          },
          destination: { city: "Destination", code: "D" },
          departure: {
            scheduled_time: "2025-09-17T14:05:00Z",
          },
          arrival: {
            scheduled_time: "2025-09-17T18:30:00Z",
          },
          status: "Scheduled",
          url: "https://example.com/A1",
        },
      ],
    },
  };
}
