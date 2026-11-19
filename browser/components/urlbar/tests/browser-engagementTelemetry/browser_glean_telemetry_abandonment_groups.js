/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for the following data of abandonment telemetry.
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        { groups: "heuristic", results: "search_engine" },
      ]),
  });
});

add_task(async function adaptive_history() {
  await doAdaptiveHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,history_adaptive",
          n_results: 2,
        },
      ]),
  });

  await doAdaptiveHistoryBookmarkTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,bookmark_adaptive",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function search_history() {
  await doSearchHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,search_suggest,search_suggest",
          results: "search_engine,search_suggest,search_suggest",
          n_results: 3,
        },
      ]),
  });

  await doTailSearchSuggestTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,suggested_index,general",
          results: "search_engine,action,bookmark",
          n_results: 3,
        },
      ]),
  });

  await doGeneralHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,general",
          results: "search_engine,history",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function restrict_keywords() {
  let telemetry = {
    groups:
      "general,general,general,general,general,general,restrict_keyword," +
      "restrict_keyword,restrict_keyword,restrict_keyword",
    results:
      "search_engine,search_engine,search_engine,search_engine,search_engine," +
      "search_engine,restrict_keyword_bookmarks,restrict_keyword_tabs," +
      "restrict_keyword_history,restrict_keyword_actions",
    n_results: 10,
  };
  await doRestrictKeywordsTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([telemetry, telemetry, telemetry, telemetry]),
  });
});

add_task(async function suggest() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.engines", false]],
  });
  await doSuggestTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,suggest",
          results: "search_engine,rust_adm_nonsponsored",
          n_results: 2,
        },
      ]),
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function about_page() {
  await doAboutPageTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
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
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,suggested_index",
          results: "search_engine,unit",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function history_semantic() {
  await doSemanticHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,general",
          results: "search_engine,history_semantic",
          semantic_distances: ",25",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function history_serp() {
  await doSerpHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,general",
          results: "search_engine,history_serp",
          n_results: 2,
        },
      ]),
  });

  await doBookmarkSerpHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,general",
          results: "search_engine,bookmark_serp",
          n_results: 2,
        },
      ]),
  });

  await doAdaptiveHistorySerpHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,history_adaptive_serp",
          n_results: 2,
        },
      ]),
  });

  await doAdaptiveHistoryBookmarkSerpHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,bookmark_adaptive_serp",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function tab_adaptive() {
  await doTabAdaptiveTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,tab_adaptive",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function tab_adaptive_serp() {
  await doTabAdaptiveSerpHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,adaptive_history",
          results: "search_engine,tab_adaptive_serp",
          n_results: 2,
        },
      ]),
  });
});

add_task(async function tab_serp() {
  await doTabSerpHistoryTest({
    trigger: () => doBlur(),
    assert: () =>
      assertAbandonmentTelemetry([
        {
          groups: "heuristic,general",
          results: "search_engine,tab_serp",
          n_results: 2,
        },
      ]),
  });
});
