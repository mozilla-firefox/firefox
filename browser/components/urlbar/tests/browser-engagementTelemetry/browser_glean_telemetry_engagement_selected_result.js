/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for the following data of engagement telemetry.
// - selected_result
// - selected_position
// - provider
// - results
// - actions

ChromeUtils.defineESModuleGetters(this, {
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
});

// This test has many subtests and can time out in verify mode.
requestLongerTimeout(5);

add_setup(async function () {
  await setup();
});

add_task(async function selected_result_autofill_about() {
  await doTest(async () => {
    await openPopup("about:about");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_about",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
        results: "autofill_about",
      },
    ]);
  });
});

add_task(async function selected_result_autofill_adaptive() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill.adaptiveHistory.enabled", true],
      ["browser.urlbar.autoFill.adaptiveHistory.urlMinPicks", 1],
    ],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits({
      url: "https://example.com/test",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
    await UrlbarUtils.addToInputHistory("https://example.com/test", "exa");
    await openPopup("exa");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_adaptive_url",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
        results: "autofill_adaptive_url",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_dismiss_autofill_adaptive_origin() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill.adaptiveHistory.enabled", true],
      ["browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.useCountThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.urlMinPicks", 1],
    ],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits({
      url: "https://example.com/",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
    await UrlbarUtils.addToInputHistory("https://example.com/", "exa");

    await openPopup("exa");
    await UrlbarTestUtils.openResultMenuAndClickItem(
      window,
      "dismiss_autofill",
      {
        resultIndex: 0,
      }
    );
    await UrlbarTestUtils.promiseSearchComplete(window);

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_adaptive_origin",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
        results: "autofill_adaptive_origin",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_dismiss_autofill_adaptive_url() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill.adaptiveHistory.enabled", true],
      ["browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.useCountThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.urlMinPicks", 1],
    ],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits({
      url: "https://example.com/test",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
    await UrlbarUtils.addToInputHistory("https://example.com/test", "exa");

    await openPopup("exa");
    await UrlbarTestUtils.openResultMenuAndClickItem(
      window,
      "dismiss_autofill",
      {
        resultIndex: 0,
      }
    );
    await UrlbarTestUtils.promiseSearchComplete(window);

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_adaptive_url",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(
  async function selected_result_dismiss_autofill_adaptive_url_history() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.urlbar.autoFill.adaptiveHistory.enabled", true],
        ["browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold", 0],
        ["browser.urlbar.autoFill.adaptiveHistory.useCountThreshold", 0],
        ["browser.urlbar.autoFill.adaptiveHistory.urlMinPicks", 1],
      ],
    });

    await doTest(async () => {
      await PlacesTestUtils.addVisits({
        url: "https://example.com/test",
        transition: PlacesUtils.history.TRANSITION_TYPED,
      });
      await UrlbarUtils.addToInputHistory("https://example.com/test", "exa");

      await openPopup("exa");
      await UrlbarTestUtils.openResultMenuAndClickItem(window, "dismiss", {
        resultIndex: 0,
      });
      await UrlbarTestUtils.promiseSearchComplete(window);

      await assertEngagementTelemetry([
        {
          selected_result: "autofill_adaptive_url",
          selected_position: 1,
          provider: "UrlbarProviderAutofill",
        },
      ]);
    });

    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function selected_result_dismiss_autofill_origin() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill.adaptiveHistory.enabled", true],
      ["browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.useCountThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.urlMinPicks", 1],
    ],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits({
      url: "https://example.com/",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

    await openPopup("exa");
    await UrlbarTestUtils.openResultMenuAndClickItem(
      window,
      "dismiss_autofill",
      {
        resultIndex: 0,
      }
    );
    await UrlbarTestUtils.promiseSearchComplete(window);

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_origin",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_autofill_origin() {
  await doTest(async () => {
    await PlacesTestUtils.addVisits({
      url: "https://example.com/test",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    await openPopup("exa");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_origin",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
        results: "autofill_origin,history",
      },
    ]);
  });
});

add_task(async function selected_result_autofill_url() {
  await doTest(async () => {
    await PlacesTestUtils.addVisits({
      url: "https://example.com/test",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    await openPopup("https://example.com/test");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "autofill_url",
        selected_position: 1,
        provider: "UrlbarProviderAutofill",
        results: "autofill_url",
      },
    ]);
  });
});

add_task(async function selected_result_bookmark() {
  await doTest(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.secondaryActions.featureGate", false]],
    });
    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: "https://example.com/bookmark",
      title: "bookmark",
    });

    await openPopup("bookmark");
    await selectRowByURL("https://example.com/bookmark");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "bookmark",
        selected_position: 2,
        provider: "UrlbarProviderPlaces",
        results: "search_engine,bookmark",
      },
    ]);

    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function selected_result_bookmark_adaptive() {
  await doTest(async () => {
    await PlacesTestUtils.addVisits("https://example.com/test");

    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: "https://example.com/test",
      title: "bookmark",
    });

    await UrlbarUtils.addToInputHistory("https://example.com/test", "test");

    await openPopup("test");
    await selectRowByProvider("UrlbarProviderInputHistory");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "bookmark_adaptive",
        selected_position: 2,
        provider: "UrlbarProviderInputHistory",
        results: "search_engine,bookmark_adaptive",
      },
    ]);
  });
});

add_task(async function selected_result_bookmark_serp() {
  await doTest(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.urlbar.autoFill", false],
        ["browser.urlbar.secondaryActions.featureGate", false],
      ],
    });

    let defaultEngine = await SearchService.getDefault();
    let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;

    await PlacesTestUtils.addVisits(serpUrl);

    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: serpUrl,
      title: "bookmark",
    });

    await openPopup("test");
    await selectRowByURL(serpUrl);
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "bookmark_serp",
        selected_position: 2,
        provider: "UrlbarProviderPlaces",
        results: "search_engine,bookmark_serp",
      },
    ]);

    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function selected_result_bookmark_adaptive_serp() {
  await doTest(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.autoFill", false]],
    });

    let defaultEngine = await SearchService.getDefault();
    let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;

    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: serpUrl,
      title: "bookmark",
    });

    await PlacesTestUtils.addVisits(serpUrl);
    await UrlbarUtils.addToInputHistory(serpUrl, "test");

    await openPopup("test");
    await selectRowByProvider("UrlbarProviderInputHistory");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "bookmark_adaptive_serp",
        selected_position: 2,
        provider: "UrlbarProviderInputHistory",
        results: "search_engine,bookmark_adaptive_serp",
      },
    ]);

    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function selected_result_history() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.autoFill", false]],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits("https://example.com/test");

    await openPopup("example");
    await selectRowByURL("https://example.com/test");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "history",
        selected_position: 2,
        provider: "UrlbarProviderPlaces",
        results: "search_engine,history",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_history_adaptive() {
  await doTest(async () => {
    await PlacesTestUtils.addVisits("https://example.com/test");
    await UrlbarUtils.addToInputHistory("https://example.com/test", "exa");

    await openPopup("exa");
    await selectRowByProvider("UrlbarProviderInputHistory");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "history_adaptive",
        selected_position: 2,
        provider: "UrlbarProviderInputHistory",
        results: "search_engine,history_adaptive",
      },
    ]);
  });
});

add_task(async function selected_result_history_adaptive_serp() {
  await doTest(async () => {
    let defaultEngine = await SearchService.getDefault();
    let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;

    await PlacesTestUtils.addVisits(serpUrl);
    await UrlbarUtils.addToInputHistory(serpUrl, "test sea");

    await openPopup("test sea");
    await selectRowByProvider("UrlbarProviderInputHistory");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "history_adaptive_serp",
        selected_position: 2,
        provider: "UrlbarProviderInputHistory",
        results: "search_engine,history_adaptive_serp",
      },
    ]);
  });
});

add_task(async function selected_result_keyword() {
  await doTest(async () => {
    await PlacesUtils.keywords.insert({
      keyword: "keyword",
      url: "https://example.com/?q=%s",
    });

    await openPopup("keyword test");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "keyword",
        selected_position: 1,
        provider: "UrlbarProviderBookmarkKeywords",
        results: "keyword",
      },
    ]);

    await PlacesUtils.keywords.remove("keyword");
  });
});

add_task(async function selected_result_search_engine() {
  await doTest(async () => {
    await openPopup("x");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "search_engine",
        selected_position: 1,
        provider: "UrlbarProviderHeuristicFallback",
        results: "search_engine",
      },
    ]);
  });
});

add_task(async function selected_result_search_suggest() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.maxHistoricalSearchSuggestions", 2],
    ],
  });

  await doTest(async () => {
    await openPopup("foo");
    await selectRowByURL("http://mochi.test:8888/?terms=foofoo");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "search_suggest",
        selected_position: 2,
        provider: "UrlbarProviderSearchSuggestions",
        results: "search_engine,search_suggest,search_suggest",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_search_history() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.maxHistoricalSearchSuggestions", 2],
    ],
  });

  await doTest(async () => {
    await UrlbarTestUtils.formHistory.add(["foofoo", "foobar"]);

    await openPopup("foo");
    await selectRowByURL("http://mochi.test:8888/?terms=foofoo");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "search_history",
        selected_position: 3,
        provider: "UrlbarProviderSearchSuggestions",
        results: "search_engine,search_history,search_history",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_url() {
  await doTest(async () => {
    await openPopup("https://example.com/");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "url",
        selected_position: 1,
        provider: "UrlbarProviderHeuristicFallback",
        results: "url",
      },
    ]);
  });
});

add_task(async function selected_result_tab() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.searches", true]],
  });

  const tab = BrowserTestUtils.addTab(gBrowser, "https://example.com/");

  await doTest(async () => {
    await openPopup("example");
    await selectRowByProvider("UrlbarProviderPlaces");
    EventUtils.synthesizeKey("KEY_Enter");
    await TestUtils.waitForCondition(() => gBrowser.selectedTab === tab);

    await assertEngagementTelemetry([
      {
        selected_result: "tab",
        selected_position: 4,
        provider: "UrlbarProviderPlaces",
        results: "search_engine,search_suggest,search_suggest,tab",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function selected_result_tab_adaptive() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", false],
      ["browser.urlbar.autoFill", false],
    ],
  });

  const tab = BrowserTestUtils.addTab(gBrowser, "https://example.com/");

  await doTest(async () => {
    await PlacesTestUtils.addVisits("https://example.com/");
    await UrlbarUtils.addToInputHistory("https://example.com/", "exa");

    await openPopup("exa");
    await selectRowByProvider("UrlbarProviderInputHistory");
    EventUtils.synthesizeKey("KEY_Enter");
    await TestUtils.waitForCondition(
      () => gBrowser.selectedTab === tab,
      "Waiting for selected tab to be the tab we opened."
    );

    await assertEngagementTelemetry([
      {
        selected_result: "tab_adaptive",
        selected_position: 2,
        provider: "UrlbarProviderInputHistory",
        results: "search_engine,tab_adaptive",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function selected_result_tab_adaptive_serp() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", false],
      ["browser.urlbar.autoFill", false],
    ],
  });

  let defaultEngine = await SearchService.getDefault();
  let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;
  let tab = BrowserTestUtils.addTab(gBrowser, serpUrl);

  await doTest(async () => {
    await PlacesTestUtils.addVisits(serpUrl);
    await UrlbarUtils.addToInputHistory(serpUrl, "test sea");

    await openPopup("test sea");
    await selectRowByProvider("UrlbarProviderInputHistory");

    EventUtils.synthesizeKey("KEY_Enter");
    await TestUtils.waitForCondition(
      () => gBrowser.selectedTab === tab,
      "Waiting for selected tab to be the tab we opened."
    );

    await assertEngagementTelemetry([
      {
        selected_result: "tab_adaptive_serp",
        selected_position: 2,
        provider: "UrlbarProviderInputHistory",
        results: "search_engine,tab_adaptive_serp",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function selected_result_tab_serp() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", false],
      ["browser.urlbar.autoFill", false],
    ],
  });

  let defaultEngine = await SearchService.getDefault();
  let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;
  let tab = BrowserTestUtils.addTab(gBrowser, serpUrl);

  await doTest(async () => {
    await PlacesTestUtils.addVisits(serpUrl);

    await openPopup("test sea");
    await selectRowByProvider("UrlbarProviderPlaces");
    EventUtils.synthesizeKey("KEY_Enter");
    await TestUtils.waitForCondition(() => gBrowser.selectedTab === tab);

    await assertEngagementTelemetry([
      {
        selected_result: "tab_serp",
        selected_position: 2,
        provider: "UrlbarProviderPlaces",
        results: "search_engine,tab_serp",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function selected_result_remote_tab() {
  const remoteTab = await loadRemoteTab("https://example.com");

  await doTest(async () => {
    await openPopup("example");
    await selectRowByProvider("UrlbarProviderRemoteTabs");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "remote_tab",
        selected_position: 2,
        provider: "UrlbarProviderRemoteTabs",
        results: "search_engine,remote_tab",
      },
    ]);
  });

  await remoteTab.unload();
});

add_task(async function selected_result_addon() {
  const addon = loadOmniboxAddon({ keyword: "omni" });
  await addon.startup();

  await doTest(async () => {
    await openPopup("omni test");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "addon",
        selected_position: 1,
        provider: "UrlbarProviderOmnibox",
        results: "addon",
      },
    ]);
  });

  await addon.unload();
});

add_task(async function selected_result_tab_to_search() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.tabToSearch.onboard.interactionsLeft", 0],
      ["browser.urlbar.secondaryActions.featureGate", false],
    ],
  });

  await SearchTestUtils.installSearchExtension({
    name: "mozengine",
    search_url: "https://mozengine/",
  });

  await doTest(async () => {
    for (let i = 0; i < 3; i++) {
      await PlacesTestUtils.addVisits(["https://mozengine/"]);
    }

    await openPopup("moze");
    await selectRowByProvider("UrlbarProviderTabToSearch");
    const onComplete = UrlbarTestUtils.promiseSearchComplete(window);
    EventUtils.synthesizeKey("KEY_Enter");
    await onComplete;

    await assertEngagementTelemetry([
      {
        selected_result: "tab_to_search",
        selected_position: 2,
        provider: "UrlbarProviderTabToSearch",
        results: "search_engine,tab_to_search,history",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_top_site() {
  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.com/");
    await showResultByArrowDown();
    await selectRowByURL("https://example.com/");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "top_site",
        selected_position: 1,
        provider: "UrlbarProviderTopSites",
        results: "top_site",
      },
    ]);

    await cleanupTopSites();
  });
});

add_task(async function selected_result_calc() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.calculator", true]],
  });

  await doTest(async () => {
    await openPopup("8*8");
    await selectRowByProvider("UrlbarProviderCalculator");
    await SimpleTest.promiseClipboardChange("64", () => {
      EventUtils.synthesizeKey("KEY_Enter");
    });

    await assertEngagementTelemetry([
      {
        selected_result: "calc",
        selected_position: 2,
        provider: "UrlbarProviderCalculator",
        results: "search_engine,calc",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_clipboard() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.clipboard.featureGate", true],
      ["browser.urlbar.suggest.clipboard", true],
    ],
  });
  SpecialPowers.clipboardCopyString(
    "https://example.com/selected_result_clipboard"
  );

  await doTest(async () => {
    await openPopup("");
    await selectRowByProvider("UrlbarProviderClipboard");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "clipboard",
        selected_position: 1,
        provider: "UrlbarProviderClipboard",
        results: "clipboard",
      },
    ]);
  });

  SpecialPowers.clipboardCopyString("");
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager
    .getProvider("UrlbarProviderClipboard")
    .setPreviousClipboardValue("");
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_unit() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.unitConversion.enabled", true]],
  });

  await doTest(async () => {
    await openPopup("1m to cm");
    await selectRowByProvider("UrlbarProviderUnitConversion");
    await SimpleTest.promiseClipboardChange("100 cm", () => {
      EventUtils.synthesizeKey("KEY_Enter");
    });

    await assertEngagementTelemetry([
      {
        selected_result: "unit",
        selected_position: 2,
        provider: "UrlbarProviderUnitConversion",
        results: "search_engine,unit",
      },
    ]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_input_field() {
  const expected = [
    {
      selected_result: "input_field",
      selected_position: 0,
      provider: null,
      results: "",
    },
  ];

  await doTest(async () => {
    await doDropAndGo("example.com");

    await assertEngagementTelemetry(expected);
  });

  await doTest(async () => {
    await expectNoConsoleErrors(async () => {
      await doPasteAndGo("example.com");
    });
  });
});

add_task(async function selected_result_weather() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quickactions.enabled", false]],
  });

  const cleanupQuickSuggest = await ensureQuickSuggestInit();
  await MerinoTestUtils.initWeather();
  const cleanupGeolocation = GeolocationTestUtils.stubGeolocation(
    GeolocationTestUtils.SAN_FRANCISCO
  );

  await doTest(async () => {
    let provider = "UrlbarProviderQuickSuggest";

    await openPopup("weather");
    await selectRowByProvider(provider);
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "weather",
        selected_position: 2,
        provider,
        results: "search_engine,weather",
      },
    ]);
  });

  await cleanupGeolocation();
  await cleanupQuickSuggest();
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_navigational() {
  const cleanupQuickSuggest = await ensureQuickSuggestInit({
    merinoSuggestions: [
      {
        title: "Navigational suggestion",
        url: "https://example.com/navigational-suggestion",
        provider: "top_picks",
        is_sponsored: false,
        score: 0.25,
        block_id: 0,
        is_top_pick: true,
      },
    ],
  });

  await doTest(async () => {
    await openPopup("only match the Merino suggestion");
    await selectRowByProvider("UrlbarProviderQuickSuggest");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "merino_top_picks",
        selected_position: 2,
        provider: "UrlbarProviderQuickSuggest",
        results: "search_engine,merino_top_picks",
      },
    ]);
  });

  await cleanupQuickSuggest();
});

add_task(async function selected_result_dynamic_wikipedia() {
  const cleanupQuickSuggest = await ensureQuickSuggestInit({
    merinoSuggestions: [
      {
        block_id: 1,
        url: "https://example.com/dynamic-wikipedia",
        title: "Dynamic Wikipedia suggestion",
        click_url: "https://example.com/click",
        impression_url: "https://example.com/impression",
        advertiser: "dynamic-wikipedia",
        provider: "wikipedia",
        iab_category: "5 - Education",
      },
    ],
  });

  await doTest(async () => {
    await openPopup("only match the Merino suggestion");
    await selectRowByProvider("UrlbarProviderQuickSuggest");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "merino_wikipedia",
        selected_position: 2,
        provider: "UrlbarProviderQuickSuggest",
        results: "search_engine,merino_wikipedia",
      },
    ]);
  });

  await cleanupQuickSuggest();
});

add_task(async function selected_result_important_dates() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quickactions.enabled", false]],
  });

  const cleanupQuickSuggest = await ensureQuickSuggestInit();

  let provider = "UrlbarProviderQuickSuggest";
  await doTest(async () => {
    await openPopup("important dates");
    await selectRowByProvider(provider);
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "rust_important_dates",
        selected_position: 2,
        provider,
        results: "search_engine,rust_important_dates",
      },
    ]);
  });

  await cleanupQuickSuggest();
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_search_shortcut_button() {
  await doTest(async () => {
    const oneOffSearchButtons = UrlbarTestUtils.getOneOffSearchButtons(window);
    await openPopup("x");
    Assert.ok(!oneOffSearchButtons.selectedButton);

    // Select oneoff button added for test in setup().
    for (;;) {
      EventUtils.synthesizeKey("KEY_ArrowDown");
      if (!oneOffSearchButtons.selectedButton) {
        continue;
      }

      if (
        oneOffSearchButtons.selectedButton.engine.name.includes(
          "searchSuggestionEngine.xml"
        )
      ) {
        break;
      }
    }

    // Search immediately.
    await doEnter({ shiftKey: true });

    await assertEngagementTelemetry([
      {
        selected_result: "search_shortcut_button",
        selected_position: 0,
        provider: null,
        results: "search_engine",
      },
    ]);
  });
});

add_task(async function selected_result_trending() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.trending.featureGate", true],
      ["browser.urlbar.trending.requireSearchMode", false],
      ["browser.urlbar.trending.maxResultsNoSearchMode", 1],
      ["browser.urlbar.weather.featureGate", false],
    ],
  });

  let defaultEngine = await SearchService.getDefault();

  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      recordType: "engine",
      identifier: "mozengine",
      base: {
        name: "mozengine",
        urls: {
          search: {
            base: "https://example.org/",
            searchTermParamName: "q",
          },
          trending: {
            base: "https://example.com/browser/browser/components/search/test/browser/trendingSuggestionEngine.sjs",
            method: "GET",
          },
        },
      },
      variants: [
        {
          environment: { allRegionsAndLocales: true },
        },
      ],
    },
    {
      recordType: "defaultEngines",
      globalDefault: "mozengine",
      specificDefaults: [],
    },
    {
      recordType: "engineOrders",
      orders: [],
    },
  ]);

  let engine = SearchService.getEngineByName("mozengine");
  await SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);

  await doTest(async () => {
    await openPopup("");
    await selectRowByProvider("UrlbarProviderSearchSuggestions");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "trending_search",
        selected_position: 1,
        provider: "UrlbarProviderSearchSuggestions",
        results: "trending_search",
      },
    ]);
  });

  await SearchService.removeEngine(engine);

  await SearchService.setDefault(
    defaultEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );
  let settingsWritten = SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
  await SearchTestUtils.updateRemoteSettingsConfig();
  await settingsWritten;
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_trending_rich() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.richSuggestions.featureGate", true],
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.trending.featureGate", true],
      ["browser.urlbar.trending.requireSearchMode", false],
      ["browser.urlbar.trending.maxResultsNoSearchMode", 1],
      ["browser.urlbar.weather.featureGate", false],
    ],
  });

  let defaultEngine = await SearchService.getDefault();

  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      recordType: "engine",
      identifier: "mozengine",
      base: {
        name: "mozengine",
        urls: {
          search: {
            base: "https://example.org/",
            searchTermParamName: "q",
          },
          trending: {
            base: "https://example.com/browser/browser/components/search/test/browser/trendingSuggestionEngine.sjs",
            method: "GET",
            params: [
              {
                name: "richsuggestions",
                value: "true",
              },
            ],
          },
        },
      },
      variants: [
        {
          environment: { allRegionsAndLocales: true },
        },
      ],
    },
    {
      recordType: "defaultEngines",
      globalDefault: "mozengine",
      specificDefaults: [],
    },
    {
      recordType: "engineOrders",
      orders: [],
    },
  ]);

  let engine = SearchService.getEngineByName("mozengine");
  await SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);

  await doTest(async () => {
    await openPopup("");
    await selectRowByProvider("UrlbarProviderSearchSuggestions");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "trending_search_rich",
        selected_position: 1,
        provider: "UrlbarProviderSearchSuggestions",
        results: "trending_search_rich",
      },
    ]);
  });

  await SearchService.removeEngine(engine);

  await SearchService.setDefault(
    defaultEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );
  let settingsWritten = SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
  await SearchTestUtils.updateRemoteSettingsConfig();
  await settingsWritten;
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_addons() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.addons.featureGate", true],
      ["browser.urlbar.suggest.searches", false],
    ],
  });

  const cleanupQuickSuggest = await ensureQuickSuggestInit({
    merinoSuggestions: [
      {
        provider: "amo",
        icon: "https://example.com/good-addon.svg",
        url: "https://example.com/good-addon",
        title: "Good Addon",
        description: "This is a good addon",
        custom_details: {
          amo: {
            rating: "4.8",
            number_of_ratings: "1234567",
            guid: "good@addon",
          },
        },
        is_top_pick: true,
      },
    ],
  });

  await doTest(async () => {
    await openPopup("only match the Merino suggestion");
    await selectRowByProvider("UrlbarProviderQuickSuggest");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "merino_amo",
        selected_position: 2,
        provider: "UrlbarProviderQuickSuggest",
        results: "search_engine,merino_amo",
      },
    ]);
  });

  await cleanupQuickSuggest();
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_rust_adm_sponsored() {
  const cleanupQuickSuggest = await ensureQuickSuggestInit();

  await doTest(async () => {
    await openPopup("amp");
    await selectRowByURL("https://example.com/amp");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "rust_adm_sponsored",
        selected_position: 2,
        provider: "UrlbarProviderQuickSuggest",
        results: "search_engine,rust_adm_sponsored",
      },
    ]);
  });

  await cleanupQuickSuggest();
});

add_task(async function selected_result_rust_adm_nonsponsored() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.secondaryActions.featureGate", false]],
  });
  const cleanupQuickSuggest = await ensureQuickSuggestInit();

  await doTest(async () => {
    await openPopup("wikipedia");
    await selectRowByURL("https://example.com/wikipedia");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "rust_adm_nonsponsored",
        selected_position: 2,
        provider: "UrlbarProviderQuickSuggest",
        results: "search_engine,rust_adm_nonsponsored",
      },
    ]);
  });

  await cleanupQuickSuggest();
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_action() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      ["browser.urlbar.quickactions.timesToShowOnboardingLabel", 0],
    ],
  });
  await doTest(async () => {
    await openPopup("settings");
    EventUtils.synthesizeKey("KEY_Tab");
    await doEnter();

    await assertEngagementTelemetry([
      {
        selected_result: "action_settings",
        selected_position: 2,
        provider: "UrlbarProviderGlobalActions",
        results: "search_engine,action",
        actions: "none,settings",
      },
    ]);
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function selected_result_semantic() {
  const historyUrl = "https://www.example.com/semantic/";
  let defaultEngine = await SearchService.getDefault();
  const searchUrl = defaultEngine.getSubmission("semantic", null).uri.spec;
  await doTestWithSemantic(
    [
      {
        id: 1,
        title: "Test Page",
        url: historyUrl,
        frecency: 100,
      },
      {
        id: 2,
        title: "Test SERP",
        url: searchUrl,
        frecency: 10,
      },
    ],
    async () => {
      // Must be longer than `browser.urlbar.suggest.semanticHistory.minLength`.
      await openPopup("testing semantic");
      await selectRowByURL(historyUrl);
      await doEnter();

      await assertEngagementTelemetry([
        {
          selected_result: "history_semantic",
          selected_position: 2,
          provider: "UrlbarProviderSemanticHistorySearch",
          results: "search_engine,history_semantic,history_semantic_serp",
        },
      ]);
    }
  );
});
