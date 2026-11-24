/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function assertSelectTooltip(mozSelect, keyword) {
  let tooltip = mozSelect.tooltipNode;
  ok(tooltip, "A search tooltip should be anchored on the moz-select");
  ok(
    BrowserTestUtils.isVisible(tooltip),
    "The moz-select search tooltip should be visible"
  );
  is(tooltip.textContent, keyword, "Tooltip should display the search keyword");
}

/**
 * Searching for text that appears in a moz-select's options should anchor a search tooltip
 * Covers both moz-select rendering modes: a native <select> (defaultZoom, no
 * option icons) and a panel-list (defaultEngineNormal, option icons).
 */

add_task(async function test_moz_select_search_tooltips() {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, { leaveOpen: true });
  let doc = gBrowser.selectedBrowser.contentDocument;

  // moz-select without option icons (Default zoom)

  await runSearchInput("150%");

  let zoomSelect = doc.querySelector("#defaultZoom");
  ok(zoomSelect, "defaultZoom moz-select exists");

  assertSelectTooltip(zoomSelect, "150%");

  await clearSearch(doc);

  // moz-select with option icons (Default search engine)

  let appProvidedEngines = await SearchService.getAppProvidedEngines();
  let engineKeyword = appProvidedEngines.at(-1).name.toLowerCase();

  // The engine options are only rendered once all the engine icons have
  // loaded, which can take longer than the first search.
  let engineSelect = doc.querySelector("#defaultEngineNormal");
  let engineCount = (await SearchService.getVisibleEngines()).length;
  await BrowserTestUtils.waitForMutationCondition(
    engineSelect,
    { childList: true, subtree: true },
    () => engineSelect.querySelectorAll("moz-option").length == engineCount
  );

  await runSearchInput(engineKeyword);

  assertSelectTooltip(engineSelect, engineKeyword);

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
