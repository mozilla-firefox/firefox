/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const CONFIG_V2 = [
  {
    recordType: "engine",
    identifier: "basic",
    base: {
      name: "basic",
      urls: {
        search: {
          base: "https://example.com",
          searchTermParamName: "q",
        },
        trending: {
          base: "https://example.com/browser/browser/components/search/test/browser/trendingSuggestionEngine.sjs",
          method: "GET",
        },
      },
      aliases: ["basic"],
    },
    variants: [
      {
        environment: { allRegionsAndLocales: true },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "private",
    base: {
      name: "private",
      urls: {
        search: {
          base: "https://example.com",
          searchTermParamName: "q",
        },
        suggestions: {
          base: "https://example.com",
          method: "GET",
          searchTermParamName: "search",
        },
      },
      aliases: ["private"],
    },
    variants: [
      {
        environment: { allRegionsAndLocales: true },
      },
    ],
  },
  {
    recordType: "defaultEngines",
    globalDefault: "basic",
    specificDefaults: [],
  },
  {
    recordType: "engineOrders",
    orders: [],
  },
];

SearchTestUtils.init(this);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.recentsearches.featureGate", false],
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.suggest.trending", true],
    ],
  });

  await SearchTestUtils.updateRemoteSettingsConfig(CONFIG_V2);
  Services.telemetry.clearScalars();
});

add_task(async function test_trending_results() {
  await check_results({
    featureEnabled: true,
    searchMode: "@basic ",
    expectedResults: 2,
  });
  await check_results({
    featureEnabled: true,
    requireSearchModeEnabled: false,
    expectedResults: 2,
  });
  await check_results({
    featureEnabled: true,
    requireSearchModeEnabled: false,
    searchMode: "@basic ",
    expectedResults: 2,
  });
  await check_results({
    featureEnabled: false,
    searchMode: "@basic ",
    expectedResults: 0,
  });
  await check_results({
    featureEnabled: false,
    expectedResults: 0,
  });
  await check_results({
    featureEnabled: false,
    requireSearchModeEnabled: false,
    expectedResults: 0,
  });
  await check_results({
    featureEnabled: false,
    requireSearchModeEnabled: false,
    searchMode: "@basic ",
    expectedResults: 0,
  });

  // The private engine is not configured with any trending url.
  await check_results({
    featureEnabled: true,
    searchMode: "@private ",
    expectedResults: 0,
  });

  // Check we can configure the maximum number of results.
  await check_results({
    featureEnabled: true,
    searchMode: "@basic ",
    maxResultsSearchMode: 5,
    expectedResults: 5,
  });
  await check_results({
    featureEnabled: true,
    requireSearchModeEnabled: false,
    maxResultsNoSearchMode: 5,
    expectedResults: 5,
  });
});

add_task(async function test_block_trending() {
  Services.telemetry.clearScalars();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.trending.featureGate", true],
      ["browser.urlbar.trending.requireSearchMode", false],
    ],
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
    waitForFocus: SimpleTest.waitForFocus,
  });

  Assert.equal(UrlbarTestUtils.getResultCount(window), 2);
  let { result: trendingResult } = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    0
  );
  Assert.equal(trendingResult.payload.trending, true);

  await UrlbarTestUtils.openResultMenuAndPressAccesskey(window, "n", {
    resultIndex: 0,
  });

  await TestUtils.waitForCondition(
    () => UrlbarTestUtils.getResultCount(window) == 1
  );
  let { result: heuristicResult } = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    0
  );
  Assert.notEqual(heuristicResult.payload.trending, true);

  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent", false, true),
    "urlbar.trending.block",
    1
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
  await SpecialPowers.popPrefEnv();
});

async function check_results({
  featureEnabled = false,
  requireSearchModeEnabled = true,
  searchMode = "",
  expectedResults = 0,
  maxResultsSearchMode = 2,
  maxResultsNoSearchMode = 2,
}) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.trending.maxResultsSearchMode", maxResultsSearchMode],
      [
        "browser.urlbar.trending.maxResultsNoSearchMode",
        maxResultsNoSearchMode,
      ],
      ["browser.urlbar.trending.featureGate", featureEnabled],
      ["browser.urlbar.trending.requireSearchMode", requireSearchModeEnabled],
    ],
  });

  // If we are not in a search mode and there are no results. The urlbar
  // will not open.
  if (!searchMode && !expectedResults) {
    window.gURLBar.inputField.focus();
    Assert.ok(!UrlbarTestUtils.isPopupOpen(window));
    return;
  }

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: searchMode,
    waitForFocus: SimpleTest.waitForFocus,
  });

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    expectedResults,
    "We matched the expected number of results"
  );

  if (expectedResults) {
    for (let i = 0; i < expectedResults; i++) {
      let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
      Assert.equal(result.type, UrlbarShared.RESULT_TYPE.SEARCH);
      Assert.equal(result.providerName, "UrlbarProviderSearchSuggestions");
      Assert.equal(result.payload.engine, "basic");
      Assert.equal(result.payload.trending, true);
    }
  }

  if (searchMode) {
    await UrlbarTestUtils.exitSearchMode(window);
  }
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
  await SpecialPowers.popPrefEnv();
}
