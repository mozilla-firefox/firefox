/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests search mode preview.
 */

"use strict";

const TEST_ENGINE_NAME = "Test";

add_setup(async function () {
  // The topSites task needs a search shortcut Top Site, so set up the
  // configuration to make Baidu available as a search engine.
  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      identifier: "google",
      base: {
        name: "Google",
        aliases: ["google"],
        urls: {
          search: {
            base: "https://www.google.com/search",
            searchTermParamName: "q",
          },
        },
      },
    },
    {
      identifier: "baidu",
      base: {
        name: "百度",
        aliases: ["百度", "baidu"],
        urls: {
          search: {
            base: "https://www.baidu.com/baidu",
            searchTermParamName: "wd",
          },
        },
      },
    },
  ]);

  await SearchTestUtils.installSearchExtension({
    name: TEST_ENGINE_NAME,
    keyword: "@test",
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      // baidu.com dedupes with the Baidu search shortcut that TopSites pins.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://www.baidu.com/",
      ],
      // The shortcuts to pin are region-derived, and Baidu is only listed for
      // CN, so ask for it explicitly.
      [
        "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.searchEngines",
        "baidu",
      ],
    ],
  });
  if (UrlbarPrefs.getScotchBonnetPref("searchRestrictKeywords.featureGate")) {
    await SpecialPowers.pushPrefEnv({
      set: [
        // Restrict keyword results adds 4 rows to the result panel, reaching
        // the maximum number of results displayed.
        // We need to increase the result limit to ensure the installed engines
        // in the test are visible and not hidden.
        ["browser.urlbar.maxRichResults", 99],
      ],
    });
  }

  // A profile pins a search shortcut for whichever engine its region lists, and
  // that pin persists in browser.newtabpage.pinned for the whole session. Once
  // the configuration above drops that engine the shortcut is left out of the
  // list but keeps its slot, so Baidu would never be the first Top Site.
  // Turning the experiment off unpins the search shortcuts, leaving the tasks
  // below to pin Baidu at the front.
  await updateTopSites(sites => !sites.some(s => s?.searchTopSite), false);

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

/**
 * @param {Node} button
 *   A one-off button.
 * @param {boolean} [isPreview]
 *  Whether the expected search mode should be a preview. Defaults to true.
 * @returns {object}
 *   The search mode object expected when that one-off is selected.
 */
function getExpectedSearchMode(button, isPreview = true) {
  let expectedSearchMode = {
    entry: "oneoff",
    isPreview,
  };
  if (button.engine) {
    expectedSearchMode.engineName = button.engine.name;
    let engine = SearchService.getEngineByName(button.engine.name);
    if (engine.isGeneralPurposeEngine) {
      expectedSearchMode.source = UrlbarUtils.RESULT_SOURCE.SEARCH;
    }
  } else {
    expectedSearchMode.source = button.source;
  }

  return expectedSearchMode;
}

// Tests that cycling through token alias engines enters search mode preview.
add_task(async function tokenAlias() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@",
  });

  let details;
  while (gURLBar.searchMode?.engineName != TEST_ENGINE_NAME) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
    let index = UrlbarTestUtils.getSelectedRowIndex(window);
    details = await UrlbarTestUtils.getDetailsOfResultAt(window, index);

    let expectedSearchMode =
      details.type === UrlbarUtils.RESULT_TYPE.RESTRICT
        ? gURLBar.searchModeForToken(details.result.payload.keyword)
        : {
            engineName: details.searchParams.engine,
            isPreview: true,
            entry: "keywordoffer",
          };

    expectedSearchMode.isPreview = true;
    expectedSearchMode.entry = "keywordoffer";

    if (details.type !== UrlbarUtils.RESULT_TYPE.RESTRICT) {
      let engine = SearchService.getEngineByName(details.searchParams.engine);
      if (engine.isGeneralPurposeEngine) {
        expectedSearchMode.source = UrlbarUtils.RESULT_SOURCE.SEARCH;
      }
    }
    await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  }

  let searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  EventUtils.synthesizeKey("KEY_Enter");
  await searchPromise;

  // Test that we are in confirmed search mode.
  let searchMode;
  if (details.type === UrlbarUtils.RESULT_TYPE.RESTRICT) {
    searchMode = gURLBar.searchModeForToken(details.result.payload.keyword);
  } else {
    searchMode = {
      engineName: details.searchParams.engine,
    };
  }

  searchMode.entry = "keywordoffer";

  await UrlbarTestUtils.assertSearchMode(window, searchMode);
  await UrlbarTestUtils.exitSearchMode(window);
});

// Tests that starting to type a query exits search mode preview in favour of
// full search mode.
add_task(async function startTyping() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@",
  });
  while (gURLBar.searchMode?.engineName != TEST_ENGINE_NAME) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
  }

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: TEST_ENGINE_NAME,
    isPreview: true,
    entry: "keywordoffer",
  });

  let searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  EventUtils.synthesizeKey("M");
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: TEST_ENGINE_NAME,
    entry: "keywordoffer",
  });
  await UrlbarTestUtils.exitSearchMode(window);
});

// Tests that highlighting a search shortcut Top Site enters search mode
// preview.
add_task(async function topSites() {
  // Enable search shortcut Top Sites.
  await PlacesUtils.history.clear();
  await updateTopSites(
    sites => sites && sites[0] && sites[0].searchTopSite,
    true
  );
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
    fireInputEvent: true,
  });

  // We previously verified that the first Top Site is a search shortcut.
  EventUtils.synthesizeKey("KEY_ArrowDown");
  let searchTopSite = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: searchTopSite.searchParams.engine,
    // Baidu is a general purpose engine, so history results are hidden.
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    isPreview: true,
    entry: "topsites_urlbar",
  });
  await UrlbarTestUtils.exitSearchMode(window);
});

// Tests that search mode preview is exited when the view is closed.
add_task(async function closeView() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@",
  });

  while (gURLBar.searchMode?.engineName != TEST_ENGINE_NAME) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
  }
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: TEST_ENGINE_NAME,
    isPreview: true,
    entry: "keywordoffer",
  });

  // We should close search mode when closing the view.
  await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());
  await UrlbarTestUtils.assertSearchMode(window, null);

  // Check search mode isn't re-entered when re-opening the view.
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  await UrlbarTestUtils.assertSearchMode(window, null);
  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests that search more preview is exited when the user switches tabs.
add_task(async function tabSwitch() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@",
  });

  while (gURLBar.searchMode?.engineName != TEST_ENGINE_NAME) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
  }
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: TEST_ENGINE_NAME,
    isPreview: true,
    entry: "keywordoffer",
  });

  // Open a new tab then switch back to the original tab.
  let tab1 = gBrowser.selectedTab;
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  await UrlbarTestUtils.assertSearchMode(window, null);
  BrowserTestUtils.removeTab(tab2);
});

// Tests that search mode is previewed when the user down arrows through the
// one-offs.
add_task(async function oneOff_downArrow() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let oneOffs = UrlbarTestUtils.getOneOffSearchButtons(window);
  await TestUtils.waitForCondition(
    () => !oneOffs._rebuilding,
    "Waiting for one-offs to finish rebuilding"
  );
  let resultCount = UrlbarTestUtils.getResultCount(window);

  // Key down through all results.
  for (let i = 0; i < resultCount; i++) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
  }

  // Key down again. The first one-off should be selected.
  EventUtils.synthesizeKey("KEY_ArrowDown");

  // Check for the one-off's search mode previews.
  while (oneOffs.selectedButton != oneOffs.settingsButton) {
    await UrlbarTestUtils.assertSearchMode(
      window,
      getExpectedSearchMode(oneOffs.selectedButton)
    );
    EventUtils.synthesizeKey("KEY_ArrowDown");
  }

  // Check that selecting the search settings button leaves search mode preview.
  Assert.equal(
    oneOffs.selectedButton,
    oneOffs.settingsButton,
    "The settings button is selected."
  );
  await UrlbarTestUtils.assertSearchMode(window, null);

  // Closing the view should also exit search mode preview.
  await UrlbarTestUtils.promisePopupClose(window);
  await UrlbarTestUtils.assertSearchMode(window, null);
});

// Tests that search mode is previewed when the user Alt+down arrows through the
// one-offs. This subtest also highlights a keywordoffer result (the first Top
// Site) before Alt+Arrowing to the one-offs. This checks that the search mode
// previews from keywordoffer results are overwritten by selected one-offs.
add_task(async function oneOff_alt_downArrow() {
  // Add some visits to a URL so we have multiple Top Sites.
  await PlacesUtils.history.clear();
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits("https://example.com/");
  }
  await updateTopSites(
    sites =>
      sites &&
      sites[0]?.searchTopSite &&
      sites[1]?.url == "https://example.com/",
    true
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let oneOffs = UrlbarTestUtils.getOneOffSearchButtons(window);
  await TestUtils.waitForCondition(
    () => !oneOffs._rebuilding,
    "Waiting for one-offs to finish rebuilding"
  );

  // Key down to the first result and check that it enters search mode preview.
  EventUtils.synthesizeKey("KEY_ArrowDown");
  let searchTopSite = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: searchTopSite.searchParams.engine,
    // Baidu is a general purpose engine, so history results are hidden.
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    isPreview: true,
    entry: "topsites_urlbar",
  });

  // Alt+down. The first one-off should be selected.
  EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  // Check for the one-offs' search mode previews.
  while (oneOffs.selectedButton) {
    await UrlbarTestUtils.assertSearchMode(
      window,
      getExpectedSearchMode(oneOffs.selectedButton)
    );
    EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  }

  // Now key down without a modifier. We should move to the second result and
  // have no search mode preview.
  EventUtils.synthesizeKey("KEY_ArrowDown");
  Assert.equal(
    UrlbarTestUtils.getSelectedRowIndex(window),
    1,
    "The second result is selected."
  );
  await UrlbarTestUtils.assertSearchMode(window, null);

  // Arrow back up to the keywordoffer result and check for search mode preview.
  EventUtils.synthesizeKey("KEY_ArrowUp");
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: searchTopSite.searchParams.engine,
    // Baidu is a general purpose engine, so history results are hidden.
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    isPreview: true,
    entry: "topsites_urlbar",
  });

  await PlacesUtils.history.clear();
  await UrlbarTestUtils.promisePopupClose(window);
  await UrlbarTestUtils.assertSearchMode(window, null);
});

// Tests that search mode is previewed when the user is in full search mode
// and down arrows through the one-offs.
add_task(async function fullSearchMode_oneOff_downArrow() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let oneOffs = UrlbarTestUtils.getOneOffSearchButtons(window);
  await TestUtils.waitForCondition(
    () => !oneOffs._rebuilding,
    "Waiting for one-offs to finish rebuilding"
  );
  let oneOffButtons = oneOffs.getSelectableButtons(true);

  await UrlbarTestUtils.enterSearchMode(window);
  let expectedSearchMode = getExpectedSearchMode(oneOffButtons[0], false);
  // Sanity check: we are in the correct search mode.
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);

  // Key down through all results.
  let resultCount = UrlbarTestUtils.getResultCount(window);
  for (let i = 0; i < resultCount; i++) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
    // If the result is a shortcut, it will enter preview mode.
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    await UrlbarTestUtils.assertSearchMode(
      window,
      Object.assign(expectedSearchMode, {
        isPreview: !!result.searchParams.keyword,
      })
    );
  }

  // Key down again. The first one-off should be selected.
  EventUtils.synthesizeKey("KEY_ArrowDown");
  // Check that we show the correct preview as we cycle through the one-offs.
  while (oneOffs.selectedButton != oneOffs.settingsButton) {
    await UrlbarTestUtils.assertSearchMode(
      window,
      getExpectedSearchMode(oneOffs.selectedButton, true)
    );
    EventUtils.synthesizeKey("KEY_ArrowDown");
  }

  // We should still be in the same search mode after cycling through all the
  // one-offs.
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  await UrlbarTestUtils.exitSearchMode(window);
  await UrlbarTestUtils.promisePopupClose(window);
});

// Tests that search mode is previewed when the user is in full search mode
// and alt+down arrows through the one-offs. This subtest also checks that
// exiting full search mode while alt+arrowing through the one-offs enters
// search mode preview for subsequent one-offs.
add_task(async function fullSearchMode_oneOff_alt_downArrow() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let oneOffs = UrlbarTestUtils.getOneOffSearchButtons(window);
  let oneOffButtons = oneOffs.getSelectableButtons(true);
  await TestUtils.waitForCondition(
    () => !oneOffs._rebuilding,
    "Waiting for one-offs to finish rebuilding"
  );

  await UrlbarTestUtils.enterSearchMode(window);
  let expectedSearchMode = getExpectedSearchMode(oneOffButtons[0], false);
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);

  // Key down to the first result.
  EventUtils.synthesizeKey("KEY_ArrowDown");

  // Alt+down. The first one-off should be selected.
  EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  // Cycle through the first half of the one-offs and verify that search mode
  // preview is entered.
  Assert.greater(
    oneOffButtons.length,
    1,
    "Sanity check: We should have at least two one-offs."
  );
  for (let i = 1; i < oneOffButtons.length / 2; i++) {
    await UrlbarTestUtils.assertSearchMode(
      window,
      getExpectedSearchMode(oneOffs.selectedButton, true)
    );
    EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  }
  // Now click out of search mode.
  await UrlbarTestUtils.exitSearchMode(window, { clickClose: true });
  // Now check for the remaining one-offs' search mode previews.
  EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  while (oneOffs.selectedButton) {
    await UrlbarTestUtils.assertSearchMode(
      window,
      getExpectedSearchMode(oneOffs.selectedButton, true)
    );
    EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  }

  await UrlbarTestUtils.promisePopupClose(window);
  await UrlbarTestUtils.assertSearchMode(window, null);
});

// Tests that the original search mode is preserved when going through some
// one-off buttons and then back down in the results list.
add_task(async function fullSearchMode_oneOff_restore_on_down() {
  info("Add a few visits to top sites");
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits([
      "https://1.example.com/",
      "https://2.example.com/",
      "https://3.example.com/",
    ]);
  }
  await updateTopSites(sites => sites?.length > 2, false);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let oneOffs = UrlbarTestUtils.getOneOffSearchButtons(window);
  let oneOffButtons = oneOffs.getSelectableButtons(true);
  await TestUtils.waitForCondition(
    () => !oneOffs._rebuilding,
    "Waiting for one-offs to finish rebuilding"
  );

  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.HISTORY,
  });
  let expectedSearchMode = getExpectedSearchMode(
    oneOffButtons.find(b => b.source == UrlbarUtils.RESULT_SOURCE.HISTORY),
    false
  );
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  info("Down to the first result");
  EventUtils.synthesizeKey("KEY_ArrowDown");
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  info("Alt+down to the first one-off.");
  Assert.greater(
    oneOffButtons.length,
    1,
    "Sanity check: We should have at least two one-offs."
  );
  EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  await UrlbarTestUtils.assertSearchMode(
    window,
    getExpectedSearchMode(oneOffs.selectedButton, true)
  );
  info("Go again down through the list of results");
  EventUtils.synthesizeKey("KEY_ArrowDown");
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);

  // Now do a similar test without initial search mode.
  info("Exit search mode.");
  await UrlbarTestUtils.exitSearchMode(window);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  info("Down to the first result");
  EventUtils.synthesizeKey("KEY_ArrowDown");
  await UrlbarTestUtils.assertSearchMode(window, null);
  info("select a one-off to start preview");
  EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  await UrlbarTestUtils.assertSearchMode(
    window,
    getExpectedSearchMode(oneOffs.selectedButton, true)
  );
  info("Go again through the list of results");
  EventUtils.synthesizeKey("KEY_ArrowDown");
  await UrlbarTestUtils.assertSearchMode(window, null);

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});
