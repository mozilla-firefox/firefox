/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Main tests for SearchSERPTelemetry - general engine visiting and link clicking.
 *
 * NOTE: As this test file is already fairly long-running, adding to this file
 * will likely cause timeout errors with test-verify jobs on Treeherder.
 * Therefore, please do not add further tasks to this file.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
});

const TEST_PROVIDER_INFO = [
  {
    telemetryId: "example",
    searchPageRegexp:
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetry(?:Ad)?/,
    queryParamNames: ["s"],
    codeParamName: "abc",
    taggedCodes: ["ff"],
    followOnParamNames: ["a"],
    extraAdServersRegexps: [/^https:\/\/example\.com\/ad2?/],
    components: [
      {
        type: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        default: true,
      },
    ],
  },
];

/**
 * Returns the index of the first search suggestion in the urlbar results.
 *
 * @returns {Promise<number>} An index, or -1 if there are no search suggestions.
 */
async function getFirstSuggestionIndex() {
  const matchCount = UrlbarTestUtils.getResultCount(window);
  for (let i = 0; i < matchCount; i++) {
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (
      result.type == UrlbarUtils.RESULT_TYPE.SEARCH &&
      result.searchParams.suggestion
    ) {
      return i;
    }
  }
  return -1;
}

SearchTestUtils.init(this);

add_setup(async function () {
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      [
        "browser.newtabpage.activity-stream.improvesearch.handoffToAwesomebar",
        true,
      ],
      // Ensure to add search suggestion telemetry as search_suggestion not search_formhistory.
      ["browser.urlbar.maxHistoricalSearchSuggestions", 0],
    ],
  });
  // Enable local telemetry recording for the duration of the tests.
  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  await SearchTestUtils.installSearchExtension(
    {
      search_url: getPageUrl(true),
      search_url_get_params: "s={searchTerms}&abc=ff",
      suggest_url:
        "https://example.org/browser/browser/components/search/test/browser/searchSuggestionEngine.sjs",
      suggest_url_get_params: "query={searchTerms}",
      name: "Example",
    },
    { setAsDefault: true }
  );

  await gCUITestUtils.addSearchBar();

  registerCleanupFunction(async () => {
    gCUITestUtils.removeSearchBar();
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    Services.telemetry.canRecordExtended = oldCanRecord;
    resetTelemetry();
  });
});

async function track_ad_click(
  expectedHistogramSource,
  expectedScalarSource,
  searchAdsFn,
  cleanupFn
) {
  searchCounts.clear();
  Services.telemetry.clearScalars();

  let expectedContentScalarKey = "example:tagged:ff";
  let expectedScalarKey = "example:tagged";
  let expectedHistogramSAPSourceKey = `other-Example.${expectedHistogramSource}`;
  let expectedContentScalar = `browser.search.content.${expectedScalarSource}`;
  let expectedWithAdsScalar = `browser.search.withads.${expectedScalarSource}`;
  let expectedAdClicksScalar = `browser.search.adclicks.${expectedScalarSource}`;

  let adImpressionPromise = waitForPageWithAdImpressions();
  let tab = await searchAdsFn();

  await assertSearchSourcesTelemetry(
    {
      [expectedHistogramSAPSourceKey]: 1,
    },
    {
      [expectedContentScalar]: { [expectedContentScalarKey]: 1 },
      [expectedWithAdsScalar]: { [expectedScalarKey]: 1 },
    }
  );

  await adImpressionPromise;

  let pageLoadPromise = BrowserTestUtils.waitForLocationChange(gBrowser);
  BrowserTestUtils.synthesizeMouseAtCenter("#ad1", {}, tab.linkedBrowser);
  await pageLoadPromise;
  await promiseWaitForAdLinkCheck();

  await assertSearchSourcesTelemetry(
    {
      [expectedHistogramSAPSourceKey]: 1,
    },
    {
      [expectedContentScalar]: { [expectedContentScalarKey]: 1 },
      [expectedWithAdsScalar]: { [expectedScalarKey]: 1 },
      [expectedAdClicksScalar]: { [expectedScalarKey]: 1 },
    }
  );

  assertSERPTelemetry([
    {
      impression: {
        provider: "example",
        tagged: "true",
        partner_code: "ff",
        source: expectedScalarSource,
        is_shopping_page: "false",
        is_private: "false",
        shopping_tab_displayed: "false",
        is_signed_in: "false",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
  ]);

  await cleanupFn();

  Services.fog.testResetFOG();
}

add_task(async function test_source_urlbar() {
  let tab;
  await track_ad_click(
    "urlbar",
    "urlbar",
    async () => {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "searchSuggestion",
      });
      let idx = await getFirstSuggestionIndex();
      Assert.greaterOrEqual(idx, 0, "there should be a first suggestion");
      let loadPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      while (idx--) {
        EventUtils.sendKey("down");
      }
      EventUtils.sendKey("return");
      await loadPromise;
      return tab;
    },
    async () => {
      BrowserTestUtils.removeTab(tab);
    }
  );
});

add_task(async function test_source_urlbar_newtab() {
  let tab;
  await track_ad_click(
    "urlbar",
    "urlbar",
    async () => {
      // Load a page because alt doesn't open new tabs on about:newtab.
      BrowserTestUtils.startLoadingURIString(
        gBrowser.selectedBrowser,
        "https://example.com"
      );
      await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "searchSuggestion",
      });
      let idx = await getFirstSuggestionIndex();
      Assert.greaterOrEqual(idx, 0, "there should be a first suggestion");
      while (idx--) {
        EventUtils.sendKey("down");
      }

      let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
      EventUtils.synthesizeKey("VK_RETURN", { altKey: true });
      tab = await newTabPromise;
      return tab;
    },
    async () => {
      BrowserTestUtils.removeTab(tab);
    }
  );
});

add_task(async function test_source_urlbar_oneoffs_newtab() {
  // Enable legacy one off buttons.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });
  let tab;
  await track_ad_click(
    "urlbar",
    "urlbar",
    async () => {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "searchSuggestion",
      });

      let oneOffs =
        UrlbarTestUtils.getOneOffSearchButtons(window).getSelectableButtons(
          true
        );

      let engines = await Services.search.getEngines();
      let index = engines.findIndex(e => e.name == "Example");
      let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
      EventUtils.synthesizeMouseAtCenter(oneOffs[index], {
        accelKey: true,
        shiftKey: true,
      });
      tab = await newTabPromise;
      return tab;
    },
    async () => {
      BrowserTestUtils.removeTab(tab);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_source_urlbar_handoff() {
  let tab;
  await track_ad_click(
    "urlbar-handoff",
    "urlbar_handoff",
    async () => {
      Services.fog.testResetFOG();
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
      BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, "about:newtab");
      await BrowserTestUtils.browserStopped(tab.linkedBrowser, "about:newtab");

      info("Focus on search input in newtab content");
      await BrowserTestUtils.synthesizeMouseAtCenter(
        ".fake-editable",
        {},
        tab.linkedBrowser
      );

      info("Get suggestions");
      for (const c of "searchSuggestion".split("")) {
        EventUtils.synthesizeKey(c);
        /* eslint-disable mozilla/no-arbitrary-setTimeout */
        await new Promise(r => setTimeout(r, 50));
      }
      await TestUtils.waitForCondition(async () => {
        const index = await getFirstSuggestionIndex();
        return index >= 0;
      }, "Wait until suggestions are ready");

      let idx = await getFirstSuggestionIndex();
      Assert.greaterOrEqual(idx, 0, "there should be a first suggestion");
      const onLoaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      while (idx--) {
        EventUtils.sendKey("down");
      }
      EventUtils.sendKey("return");
      await onLoaded;

      return tab;
    },
    async () => {
      const issueRecords = Glean.newtabSearch.issued.testGetValue();
      Assert.ok(!!issueRecords, "Must have recorded a search issuance");
      Assert.equal(issueRecords.length, 1, "One search, one event");
      const newtabVisitId = issueRecords[0].extra.newtab_visit_id;
      Assert.ok(!!newtabVisitId, "Must have a visit id");
      Assert.deepEqual(
        {
          // Yes, this is tautological. But I want to use deepEqual.
          newtab_visit_id: newtabVisitId,
          search_access_point: "urlbar_handoff",
          telemetry_id: "other-Example",
        },
        issueRecords[0].extra,
        "Must have recorded the expected information."
      );
      const impRecords = Glean.newtabSearchAd.impression.testGetValue();
      Assert.equal(impRecords.length, 1, "One impression, one event.");
      Assert.deepEqual(
        {
          newtab_visit_id: newtabVisitId,
          search_access_point: "urlbar_handoff",
          telemetry_id: "example",
          is_tagged: "true",
          is_follow_on: "false",
        },
        impRecords[0].extra,
        "Must have recorded the expected information."
      );
      const clickRecords = Glean.newtabSearchAd.click.testGetValue();
      Assert.equal(clickRecords.length, 1, "One click, one event.");
      Assert.deepEqual(
        {
          newtab_visit_id: newtabVisitId,
          search_access_point: "urlbar_handoff",
          telemetry_id: "example",
          is_tagged: "true",
          is_follow_on: "false",
        },
        clickRecords[0].extra,
        "Must have recorded the expected information."
      );
      BrowserTestUtils.removeTab(tab);
    }
  );
});

add_task(async function test_source_searchbar() {
  let tab;
  await track_ad_click(
    "searchbar",
    "searchbar",
    async () => {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

      let sb = document.getElementById("searchbar");
      // Write the search query in the searchbar.
      sb.focus();
      sb.value = "searchSuggestion";
      sb.textbox.controller.startSearch("searchSuggestion");
      // Wait for the popup to show.
      await BrowserTestUtils.waitForEvent(sb.textbox.popup, "popupshown");
      // And then for the search to complete.
      await BrowserTestUtils.waitForCondition(
        () =>
          sb.textbox.controller.searchStatus >=
          Ci.nsIAutoCompleteController.STATUS_COMPLETE_NO_MATCH,
        "The search in the searchbar must complete."
      );

      let loadPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      EventUtils.synthesizeKey("KEY_Enter");
      await loadPromise;
      return tab;
    },
    async () => {
      BrowserTestUtils.removeTab(tab);
    }
  );
});

add_task(async function test_source_searchbar_newtab() {
  let tab;
  await track_ad_click(
    "searchbar",
    "searchbar",
    async () => {
      let sb = document.getElementById("searchbar");
      // Write the search query in the searchbar.
      sb.focus();
      sb.value = "searchSuggestion";
      sb.textbox.controller.startSearch("searchSuggestion");
      // Wait for the popup to show.
      await BrowserTestUtils.waitForEvent(sb.textbox.popup, "popupshown");
      // And then for the search to complete.
      await BrowserTestUtils.waitForCondition(
        () =>
          sb.textbox.controller.searchStatus >=
          Ci.nsIAutoCompleteController.STATUS_COMPLETE_NO_MATCH,
        "The search in the searchbar must complete."
      );

      let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
      EventUtils.synthesizeKey("VK_RETURN", { altKey: true });
      tab = await newTabPromise;
      return tab;
    },
    async () => {
      BrowserTestUtils.removeTab(tab);
    }
  );
});

add_task(async function test_source_system() {
  let tab;
  await track_ad_click(
    "system",
    "system",
    async () => {
      tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

      let loadPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);

      // This is not quite the same as calling from the commandline, but close
      // enough for this test.
      SearchUIUtils.loadSearchFromCommandLine(
        window,
        "searchSuggestion",
        false,
        Services.scriptSecurityManager.getSystemPrincipal(),
        gBrowser.selectedBrowser.csp
      );

      await loadPromise;
      return tab;
    },
    async () => {
      BrowserTestUtils.removeTab(tab);
    }
  );
});
