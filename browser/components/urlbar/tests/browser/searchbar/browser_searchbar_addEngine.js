/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test installing an opensearch engine using the searchbar. Only tests basic
 * functionality. More thorough tests happen on the urlbar which shares the code.
 */

const ENGINE_TEST_URL =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/opensearch.html";

add_setup(async function () {
  await SearchTestUtils.updateRemoteSettingsConfig([
    { identifier: "defaultEngine" },
  ]);
  // This page defines multiple opensearch engines.
  // We only use the first which has the name "Foo".
  await BrowserTestUtils.loadURIString({
    browser: gBrowser.selectedBrowser,
    uriString: ENGINE_TEST_URL,
  });
});

// Adapted from ../browser_searchModeSwitcher_opensearchInstall.js.
add_task(async function test_usingSearchModeSwitcher() {
  let promiseEngineAdded = SearchTestUtils.promiseEngine("Foo");
  await SearchbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-name=engine1]"
  );
  let engine = await promiseEngineAdded;
  Assert.ok(true, "The engine was installed.");

  await SearchbarTestUtils.assertSearchMode(window, {
    engineName: "Foo",
    entry: "searchbutton",
  });
  Assert.ok(true, "Entered search mode.");

  await SearchbarTestUtils.exitSearchMode(window, {
    backspace: true,
    waitForSearch: false,
  });

  await SearchService.removeEngine(engine);
});

// Adapted from ../browser_add_search_engine.js.
add_task(async function test_usingContextMenu() {
  let enginePromise = SearchTestUtils.promiseEngine("Foo");
  await SearchbarTestUtils.withContextMenu(window, popup => {
    info("Installing via context menu.");
    popup.activateItem(popup.querySelector('[anonid="add-engine-0"]'));
  });
  let engine = await enginePromise;
  Assert.ok(true, "The engine was installed.");
  await SearchService.removeEngine(engine);
});
