/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */

ChromeUtils.defineESModuleGetters(this, {
  ProvidersManager:
    "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs",
});

async function doHeuristicsTest({ trigger, assert }) {
  await doTest(async () => {
    await openPopup("x");
    await trigger();
    await assert();
  });
}

async function doAdaptiveHistoryTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.autoFill", false]],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits(["https://example.com/test"]);
    await UrlbarUtils.addToInputHistory("https://example.com/test", "examp");

    await openPopup("exa");
    await selectRowByURL("https://example.com/test");

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doAdaptiveHistorySerpHistoryTest({ trigger, assert }) {
  let defaultEngine = await SearchService.getDefault();
  let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;

  await doTest(async () => {
    await PlacesTestUtils.addVisits(serpUrl);
    await UrlbarUtils.addToInputHistory(serpUrl, "test sea");

    await openPopup("test sea");
    await selectRowByProvider("UrlbarProviderInputHistory");

    await trigger();
    await assert();
  });
}

async function doAdaptiveHistoryBookmarkTest({ trigger, assert }) {
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

    await trigger();
    await assert();
  });
}

async function doAdaptiveHistoryBookmarkSerpHistoryTest({ trigger, assert }) {
  await doTest(async () => {
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

    await trigger();
    await assert();
  });
}

async function doSearchHistoryTest({ trigger, assert }) {
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

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doRecentSearchTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.recentsearches.featureGate", true]],
  });

  await doTest(async () => {
    await UrlbarTestUtils.formHistory.add([
      { value: "foofoo", source: SearchService.defaultEngine.name },
    ]);

    await openPopup("");
    await selectRowByURL("http://mochi.test:8888/?terms=foofoo");

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doSearchSuggestTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.maxHistoricalSearchSuggestions", 2],
    ],
  });

  await doTest(async () => {
    await openPopup("foo");
    await selectRowByURL("http://mochi.test:8888/?terms=foofoo");

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doTailSearchSuggestTest({ trigger, assert }) {
  const cleanup = await _useTailSuggestionsEngine();

  await doTest(async () => {
    await openPopup("hello");
    await selectRowByProvider("UrlbarProviderSearchSuggestions");

    await trigger();
    await assert();
  });

  await cleanup();
}

async function doTopPickTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.searches", true]],
  });
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
    await openPopup("navigational");
    await selectRowByURL("https://example.com/navigational-suggestion");

    await trigger();
    await assert();
  });

  await cleanupQuickSuggest();
  await SpecialPowers.popPrefEnv();
}

async function doTopSiteTest({ trigger, assert }) {
  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.com/");

    await showResultByArrowDown();
    await selectRowByURL("https://example.com/");

    await trigger();
    await assert();

    await cleanupTopSites();
  });
}

async function doClipboardTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.clipboard.featureGate", true],
      ["browser.urlbar.suggest.clipboard", true],
    ],
  });
  SpecialPowers.clipboardCopyString("https://example.com/clipboard");
  await doTest(async () => {
    await showResultByArrowDown();
    await selectRowByURL("https://example.com/clipboard");

    await trigger();
    await assert();
  });
  SpecialPowers.clipboardCopyString("");
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager
    .getProvider("UrlbarProviderClipboard")
    .setPreviousClipboardValue("");
  await SpecialPowers.popPrefEnv();
}

async function doRemoteTabTest({ trigger, assert }) {
  const remoteTab = await loadRemoteTab("https://example.com");

  await doTest(async () => {
    await openPopup("example");
    await selectRowByProvider("UrlbarProviderRemoteTabs");

    await trigger();
    await assert();
  });

  await remoteTab.unload();
}

async function doAddonTest({ trigger, assert }) {
  const addon = loadOmniboxAddon({ keyword: "omni" });
  await addon.startup();

  await doTest(async () => {
    await openPopup("omni test");

    await trigger();
    await assert();
  });

  await addon.unload();
}

async function doGeneralBookmarkTest({ trigger, assert }) {
  await doTest(async () => {
    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: "https://example.com/bookmark",
      title: "bookmark",
    });

    await openPopup("bookmark");
    await selectRowByURL("https://example.com/bookmark");

    await trigger();
    await assert();
  });
}

async function doGeneralHistoryTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.autoFill", false]],
  });

  await doTest(async () => {
    await PlacesTestUtils.addVisits("https://example.com/test");

    await openPopup("example");
    await selectRowByURL("https://example.com/test");

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doSuggestTest({ trigger, assert }) {
  const cleanupQuickSuggest = await ensureQuickSuggestInit();

  await doTest(async () => {
    await openPopup("wikipedia");
    await selectRowByURL("https://example.com/wikipedia");

    await trigger();
    await assert();
  });

  await cleanupQuickSuggest();
}

async function doTemporalTest({ trigger, assert }) {
  await doTest(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.search.suggest.enabled", true],
        ["browser.urlbar.suggest.searches", true],
        ["browser.urlbar.suggest.topsites", false],
      ],
    });
    let today = Temporal.Now.plainDateISO();
    const cleanupQuickSuggest = await ensureQuickSuggestInit({
      remoteSettingsRecords: [
        {
          type: "dynamic-suggestions",
          suggestion_type: "temporal",
          attachment: [
            {
              keywords: [""],
              dismissal_key: "temporal",
              data: {
                group: "test-group",
                title: "Test group",
                type: "seasonal",
                startDate: today.subtract({ days: 1 }).toString(),
                endDate: today.add({ days: 1 }).toString(),
                engines: [SearchService.defaultEngine.id],
                query: "temporal",
              },
            },
          ],
        },
      ],
      prefs: [["temporal.featureGate", true]],
    });

    await openPopup("");
    await selectRowByProvider("UrlbarProviderQuickSuggest");

    await trigger();
    await assert();

    await cleanupQuickSuggest();
    await SpecialPowers.popPrefEnv();
  });
}

async function doAboutPageTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.maxRichResults", 3]],
  });

  await doTest(async () => {
    await openPopup("about:");
    await selectRowByURL("about:robots");

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doSuggestedIndexTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.unitConversion.enabled", true]],
  });

  await doTest(async () => {
    await openPopup("1m to cm");
    await selectRowByProvider("UrlbarProviderUnitConversion");

    await trigger();
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doRestrictKeywordsTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.searchRestrictKeywords.featureGate", true]],
  });

  await doTest(async () => {
    await openPopup("@");

    let totalResults = await UrlbarTestUtils.getResultCount(window);
    let restrictSymbols = Object.values(UrlbarShared.RESTRICT_TOKENS);

    for (let i = 0; i < totalResults; i++) {
      let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
      let symbol = details.result.payload.keyword;
      let keyword = details.result.payload.l10nRestrictKeywords
        ?.at(0)
        .toLowerCase();

      if (restrictSymbols.includes(symbol)) {
        let rowToSelect = await UrlbarTestUtils.waitForAutocompleteResultAt(
          window,
          i
        );

        await trigger(rowToSelect, keyword);
        await openPopup("@");
      }
    }
    await assert();
  });

  await SpecialPowers.popPrefEnv();
}

async function doSemanticHistoryTest({ trigger, assert }) {
  const historyUrl = "https://www.example.com/semantic/";
  await doTestWithSemantic(
    [
      {
        id: 1,
        title: "Test Page",
        url: historyUrl,
        frecency: 100,
        distance: 0.25,
      },
    ],
    async () => {
      // Must be longer than `browser.urlbar.suggest.semanticHistory.minLength`.
      await openPopup("testing semantic");
      await selectRowByURL(historyUrl);

      await trigger();
      await assert();
    }
  );
}

async function doSerpHistoryTest({ trigger, assert }) {
  let defaultEngine = await SearchService.getDefault();
  const searchUrl = defaultEngine.getSubmission("serp history", null).uri.spec;

  await doTest(async () => {
    await PlacesTestUtils.addVisits(searchUrl);

    await openPopup("serp");
    await selectRowByURL(searchUrl);

    await trigger();
    await assert();
  });
}

async function doBookmarkSerpHistoryTest({ trigger, assert }) {
  await doTest(async () => {
    let defaultEngine = await SearchService.getDefault();
    let serpUrl = defaultEngine.getSubmission("test search", null).uri.spec;

    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: serpUrl,
      title: "bookmark",
    });

    await openPopup("test");
    await selectRowByURL(serpUrl);

    await trigger();
    await assert();
  });
}

async function doTabAdaptiveTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.autoFill", false]],
  });
  let visited = PlacesTestUtils.waitForNotification("page-visited", visits =>
    visits.some(({ url }) => url == "https://example.com/")
  );
  let tab = BrowserTestUtils.addTab(gBrowser, "https://example.com/");
  await visited;

  await doTest(async () => {
    await PlacesTestUtils.addVisits("https://example.com/");
    await UrlbarUtils.addToInputHistory("https://example.com/", "exa");

    await openPopup("exa");
    await selectRowByProvider("UrlbarProviderInputHistory");

    await trigger();
    await assert();
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
}

async function doTabAdaptiveSerpHistoryTest({ trigger, assert }) {
  let defaultEngine = await SearchService.getDefault();
  const searchUrl = defaultEngine.getSubmission("serp history", null).uri.spec;
  let visited = PlacesTestUtils.waitForNotification("page-visited", visits =>
    visits.some(({ url }) => url == searchUrl)
  );
  let tab = BrowserTestUtils.addTab(gBrowser, searchUrl);
  await visited;

  await doTest(async () => {
    await PlacesTestUtils.addVisits(searchUrl);
    await UrlbarUtils.addToInputHistory(searchUrl, "serp");

    await openPopup("serp");
    await selectRowByURL(searchUrl);

    await trigger();
    await assert();
  });

  BrowserTestUtils.removeTab(tab);
}

async function doTabSerpHistoryTest({ trigger, assert }) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill", false],
      ["browser.urlbar.maxHistoricalSearchSuggestions", 0],
    ],
  });
  let defaultEngine = await SearchService.getDefault();
  const searchUrl = defaultEngine.getSubmission("serp history", null).uri.spec;
  let visited = PlacesTestUtils.waitForNotification("page-visited", visits =>
    visits.some(({ url }) => url == searchUrl)
  );
  let tab = BrowserTestUtils.addTab(gBrowser, searchUrl);
  await visited;

  await doTest(async () => {
    await openPopup("serp");
    await selectRowByURL(searchUrl);

    await trigger();
    await assert();
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
}

/**
 * Creates a search engine that returns tail suggestions and sets it as the
 * default engine.
 *
 * @returns {Function}
 *   A cleanup function that will revert the default search engine and stop http
 *   server.
 */
async function _useTailSuggestionsEngine() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", true],
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.richSuggestions.tail", true],
    ],
  });

  const engineName = "TailSuggestions";
  const httpServer = new HttpServer();
  httpServer.start(-1);
  httpServer.registerPathHandler("/suggest", (req, resp) => {
    const params = new URLSearchParams(req.queryString);
    const searchStr = params.get("q");
    const suggestions = [
      searchStr,
      [searchStr + "-tail"],
      [],
      {
        "google:suggestdetail": [{ t: "-tail", mp: "… " }],
      },
    ];
    resp.setHeader("Content-Type", "application/json", false);
    resp.write(JSON.stringify(suggestions));
  });

  await SearchTestUtils.installSearchExtension({
    name: engineName,
    search_url: `http://localhost:${httpServer.identity.primaryPort}/search`,
    suggest_url: `http://localhost:${httpServer.identity.primaryPort}/suggest`,
    suggest_url_get_params: "?q={searchTerms}",
  });

  const tailEngine = SearchService.getEngineByName(engineName);
  const originalEngine = await SearchService.getDefault();
  SearchService.setDefault(tailEngine, SearchService.CHANGE_REASON.UNKNOWN);

  return async () => {
    SearchService.setDefault(
      originalEngine,
      SearchService.CHANGE_REASON.UNKNOWN
    );
    httpServer.stop(() => {});
    await SpecialPowers.popPrefEnv();
  };
}
