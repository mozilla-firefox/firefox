/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Checks that exposure suggestions can be enabled in Nimbus experiments
// regardless of region and locale, even for regions and locales where Suggest
// is normally disabled.

"use strict";

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "dynamic-suggestions",
    suggestion_type: "test-exposure-aaa",
    score: 1.0,
    attachment: [
      {
        keywords: ["aaa keyword", "aaa bbb keyword", "amp", "wikipedia"],
        data: {
          result: {
            isHiddenExposure: true,
            payload: {
              rsSuggestionType: "test-exposure-aaa",
            },
          },
        },
      },
    ],
  },
  {
    type: "dynamic-suggestions",
    suggestion_type: "test-exposure-bbb",
    score: 1.0,
    attachment: {
      keywords: ["bbb keyword", "aaa bbb keyword", "amp", "wikipedia"],
      data: {
        result: {
          isHiddenExposure: true,
          payload: {
            rsSuggestionType: "test-exposure-bbb",
          },
        },
      },
    },
  },
  {
    collection: QuickSuggestTestUtils.RS_COLLECTION.AMP,
    type: QuickSuggestTestUtils.RS_TYPE.AMP,
    attachment: [QuickSuggestTestUtils.ampRemoteSettings()],
  },
  {
    type: QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
    attachment: [QuickSuggestTestUtils.wikipediaRemoteSettings()],
  },
];

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
  });

  // `ensureQuickSuggestInit()` enabled Suggest, but we want to start with it
  // disabled so that when we change locales, we can verify Suggest is properly
  // disabled or enabled depending on the locale.
  UrlbarPrefs.clear("quicksuggest.enabled");
});

add_task(async function suggestEnabledLocales() {
  let queries = [
    {
      query: "amp",
      expectedResults: [
        QuickSuggestTestUtils.ampResult({ suggestedIndex: -1 }),
        makeExpectedExposureResult("test-exposure-bbb"),
        makeExpectedExposureResult("test-exposure-aaa"),
      ],
    },
    {
      query: "wikipedia",
      expectedResults: [
        QuickSuggestTestUtils.wikipediaResult(),
        makeExpectedExposureResult("test-exposure-bbb"),
        makeExpectedExposureResult("test-exposure-aaa"),
      ],
    },
    {
      query: "aaa keyword",
      expectedResults: [makeExpectedExposureResult("test-exposure-aaa")],
    },
    {
      query: "aaa bbb keyword",
      expectedResults: [
        makeExpectedExposureResult("test-exposure-bbb"),
        makeExpectedExposureResult("test-exposure-aaa"),
      ],
    },
  ];

  let tests = [
    {
      homeRegion: "US",
      locales: ["en-US", "en-CA", "en-GB"],
      expectedQuickSuggestEnabled: true,
      queries,
    },
    {
      homeRegion: "CA",
      locales: ["en-US", "en-CA", "en-GB"],
      expectedQuickSuggestEnabled: true,
      queries,
    },
    {
      homeRegion: "CA",
      locales: ["fr"],
      expectedQuickSuggestEnabled: true,
      expectedSponsoredEnabled: false,
      queries: [
        {
          query: "amp",
          expectedResults: [
            makeExpectedExposureResult("test-exposure-bbb"),
            makeExpectedExposureResult("test-exposure-aaa"),
          ],
        },
        ...queries.filter(q => q.query != "amp"),
      ],
    },
  ];

  for (let test of tests) {
    await doLocaleTest(test);
  }
});

add_task(async function suggestDisabledLocales() {
  let queries = [
    {
      query: "amp",
      expectedResults: [
        // No AMP result!
        makeExpectedExposureResult("test-exposure-bbb"),
        makeExpectedExposureResult("test-exposure-aaa"),
      ],
    },
    {
      query: "wikipedia",
      expectedResults: [
        // No Wikipedia result!
        makeExpectedExposureResult("test-exposure-bbb"),
        makeExpectedExposureResult("test-exposure-aaa"),
      ],
    },
    {
      query: "aaa keyword",
      expectedResults: [makeExpectedExposureResult("test-exposure-aaa")],
    },
    {
      query: "aaa bbb keyword",
      expectedResults: [
        makeExpectedExposureResult("test-exposure-bbb"),
        makeExpectedExposureResult("test-exposure-aaa"),
      ],
    },
  ];

  let tests = [
    {
      homeRegion: "US",
      locales: ["de", "fr", "ja"],
      expectedQuickSuggestEnabled: false,
      queries,
    },
    {
      homeRegion: "XX",
      locales: ["de", "en-US", "fr"],
      expectedQuickSuggestEnabled: false,
      queries,
    },
  ];

  for (let test of tests) {
    await doLocaleTest(test);
  }
});

async function doLocaleTest({
  homeRegion,
  locales,
  expectedQuickSuggestEnabled,
  expectedSponsoredEnabled = expectedQuickSuggestEnabled,
  queries,
}) {
  for (let locale of locales) {
    info("Doing locale test: " + JSON.stringify({ homeRegion, locale }));

    // Set the region and locale.
    await QuickSuggestTestUtils.withRegionAndLocale({
      locale,
      region: homeRegion,
      callback: async () => {
        // Sanity-check prefs. At this point, the value of `quickSuggestEnabled`
        // will be the value of its fallback pref, `quicksuggest.enabled`.
        assertSuggestPrefs(
          expectedQuickSuggestEnabled,
          expectedSponsoredEnabled
        );
        Assert.equal(
          UrlbarPrefs.get("quickSuggestEnabled"),
          expectedQuickSuggestEnabled,
          "quickSuggestEnabled Nimbus variable should be correct after setting locale"
        );

        // Install an experiment that enables Suggest and exposures.
        let nimbusCleanup = await UrlbarTestUtils.initNimbusFeature({
          quickSuggestEnabled: true,
          quickSuggestDynamicSuggestionTypes:
            "test-exposure-aaa,test-exposure-bbb",
        });
        await QuickSuggestTestUtils.forceSync();

        // All default- and user-branch Suggest prefs should remain the same.
        assertSuggestPrefs(
          expectedQuickSuggestEnabled,
          expectedSponsoredEnabled
        );

        // But `quickSuggestEnabled` should be true, since we just installed
        // an experiment with it set to true.
        Assert.ok(
          UrlbarPrefs.get("quickSuggestEnabled"),
          "quickSuggestEnabled Nimbus variable should be enabled after installing experiment"
        );

        // Do a search and check the results.
        for (let { query, expectedResults } of queries) {
          await check_results({
            context: createContext(query, {
              providers: [UrlbarProviderQuickSuggest.name],
              isPrivate: false,
            }),
            matches: expectedResults,
          });
        }

        await nimbusCleanup();
        await QuickSuggestTestUtils.forceSync();
      },
    });
  }
}

function assertSuggestPrefs(
  expectedEnabled,
  expectedSponsored = expectedEnabled
) {
  let prefs = {
    "browser.urlbar.quicksuggest.enabled": expectedEnabled,
    "browser.urlbar.suggest.quicksuggest.all": expectedEnabled,
    "browser.urlbar.suggest.quicksuggest.sponsored": expectedSponsored,
  };
  for (let [p, expected] of Object.entries(prefs)) {
    Assert.equal(
      Services.prefs.getDefaultBranch("").getBoolPref(p),
      expected,
      "Default-branch value should be correct: " + p
    );
    Assert.equal(
      Services.prefs.getBranch("").getBoolPref(p),
      expected,
      "User-branch value should be correct: " + p
    );
  }
}

function makeExpectedExposureResult(rsSuggestionType) {
  return {
    type: UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    heuristic: false,
    exposureTelemetry: UrlbarShared.EXPOSURE_TELEMETRY.HIDDEN,
    payload: {
      rsSuggestionType,
      source: "rust",
      dynamicType: "exposure",
      provider: "Dynamic",
      suggestionType: rsSuggestionType,
      telemetryType: "exposure",
      isSponsored: false,
    },
  };
}
