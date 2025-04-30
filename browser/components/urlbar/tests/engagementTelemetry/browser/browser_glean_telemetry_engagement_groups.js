/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for the following data of engagement telemetry.
// - groups
// - results
// - n_results

// This test has many subtests and can time out in verify mode.
requestLongerTimeout(5);

add_setup(async function () {
  await initGroupTest();
});

add_task(async function heuristics() {
  await doHeuristicsTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        { groups: "heuristic", results: "search_engine" },
      ]),
  });
});

add_task(async function adaptive_history() {
  await doAdaptiveHistoryTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,history",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function search_history() {
  await doSearchHistoryTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,search_history,search_history",
          results: "search_engine,search_history,search_history",
          n_results: 3,
        },
      ]),
  });
});

add_task(async function recent_search() {
  await doRecentSearchTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "recent_search",
          results: "recent_search",
          n_results: 1,
        },
      ]),
  });
});

add_task(async function search_suggest() {
  await doSearchSuggestTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,search_suggest,search_suggest",
          results: "search_engine,search_suggest,search_suggest",
          n_results: 3,
        },
      ]),
  });

  await doTailSearchSuggestTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,search_suggest",
          results: "search_engine,search_suggest",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function top_pick() {
  await doTopPickTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,top_pick,search_suggest,search_suggest",
          results:
            "search_engine,merino_top_picks,search_suggest,search_suggest",
          n_results: 4,
        },
      ]),
  });
});

add_task(async function top_site() {
  await doTopSiteTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "top_site",
          results: "top_site",
          n_results: 1,
        },
      ]),
  });
});

add_task(async function clipboard() {
  await doClipboardTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "general",
          results: "clipboard",
          n_results: 1,
        },
      ]),
  });
});

add_task(async function remote_tab() {
  await doRemoteTabTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,remote_tab",
          results: "search_engine,remote_tab",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function addon() {
  await doAddonTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "addon",
          results: "addon",
          n_results: 1,
        },
      ]),
  });
});

add_task(async function general() {
  await doGeneralBookmarkTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,suggested_index,general",
          results: "search_engine,action,bookmark",
          n_results: 3,
        },
      ]),
  });

  await doGeneralHistoryTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,general",
          results: "search_engine,history",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function restrict_keywords() {
  const telemetryTemplate = {
    groups:
      "general,general,general,general,general,general,restrict_keyword," +
      "restrict_keyword,restrict_keyword,restrict_keyword",
    results:
      "search_engine,search_engine,search_engine,search_engine," +
      "search_engine,search_engine,restrict_keyword_bookmarks," +
      "restrict_keyword_tabs,restrict_keyword_history,restrict_keyword_actions",
    n_results: 10,
  };
  let telemetry = [];
  await doRestrictKeywordsTest({
    trigger: async (rowToSelect, category) => {
      await triggerKeywordTest(rowToSelect, category);
    },
    assert: () => assertEngagementTelemetry(telemetry),
  });

  async function triggerKeywordTest(rowToSelect, category) {
    EventUtils.synthesizeMouseAtCenter(rowToSelect, {});
    await UrlbarTestUtils.exitSearchMode(window);
    await UrlbarTestUtils.promisePopupClose(window, () => {
      EventUtils.synthesizeKey("KEY_Escape");
    });

    const telemetryItem = {
      ...telemetryTemplate,
      selected_result: `restrict_keyword_${category}`,
    };
    telemetry.push(telemetryItem);
  }
});

add_task(async function suggest() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.engines", false]],
  });
  await doSuggestTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,suggest",
          results: "search_engine,rust_adm_nonsponsored",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function about_page() {
  await doAboutPageTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,about_page,about_page",
          results: "search_engine,history,history",
          n_results: 3,
        },
      ]),
  });
});

add_task(async function suggested_index() {
  await doSuggestedIndexTest({
    trigger: () =>
      SimpleTest.promiseClipboardChange("100 cm", () => {
        EventUtils.synthesizeKey("KEY_Enter");
      }),
    assert: () =>
      assertEngagementTelemetry([
        {
          groups: "heuristic,suggested_index",
          results: "search_engine,unit",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function always_empty_if_drop_go() {
  const expected = [
    {
      engagement_type: "drop_go",
      groups: "",
      results: "",
      n_results: 0,
    },
  ];

  await doTest(async () => {
    await doDropAndGo("example.com");

    assertEngagementTelemetry(expected);
  });

  await doTest(async () => {
    // Open the results view once.
    await addTopSites("https://example.com/");
    await showResultByArrowDown();
    await UrlbarTestUtils.promisePopupClose(window);

    await doDropAndGo("example.com");

    assertEngagementTelemetry(expected);
  });
});

add_task(async function always_empty_if_paste_go() {
  const expected = [
    {
      engagement_type: "paste_go",
      groups: "",
      results: "",
      n_results: 0,
    },
  ];

  await doTest(async () => {
    await doPasteAndGo("example.com");

    assertEngagementTelemetry(expected);
  });

  await doTest(async () => {
    // Open the results view once.
    await addTopSites("https://example.com/");
    await showResultByArrowDown();
    await UrlbarTestUtils.promisePopupClose(window);

    await doPasteAndGo("example.com");

    assertEngagementTelemetry(expected);
  });
});

add_task(async function actions_search_mode() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", true]],
  });

  const expected = [
    {
      engagement_type: "enter",
      groups: "general",
      results: "action",
      n_results: 1,
    },
  ];

  await doTest(async () => {
    await openPopup("> addon");
    await UrlbarTestUtils.promisePopupClose(window, () => {
      EventUtils.synthesizeKey("KEY_Tab");
      EventUtils.synthesizeKey("KEY_Enter");
    });

    assertEngagementTelemetry(expected);
  });
});
