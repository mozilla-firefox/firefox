/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// These tests check the behavior of the Urlbar when using search mode

// The main search string used in tests
const SEARCH_STRING = "chocolate cake";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.showSearchTerms.featureGate", true],
      ["browser.urlbar.scotchBonnet.enableOverride", false],
    ],
  });
  let cleanup = await installPersistTestEngines();
  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    cleanup();
  });
});

async function searchWithNonDefaultSearchMode(tab) {
  let engine = SearchService.getEngineByName("MochiSearch");
  Assert.notEqual(
    engine.name,
    SearchService.defaultEngine.name,
    "Engine is non-default."
  );

  let [expectedSearchUrl] = UrlbarUtils.getSearchQueryUrl(
    engine,
    SEARCH_STRING
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedSearchUrl,
    true
  );
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: SEARCH_STRING,
  });
  await UrlbarTestUtils.enterSearchMode(window, {
    engineName: engine.name,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
  });
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  assertSearchStringIsInUrlbar(SEARCH_STRING);
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    isGeneralPurposeEngine: true,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    isPreview: false,
    entry: "oneoff",
  });

  return { expectedSearchUrl };
}

// When a user does a search with search mode, they should
// see the search term in the URL bar for that engine.
add_task(async function non_default_search() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await searchWithNonDefaultSearchMode(tab);
  BrowserTestUtils.removeTab(tab);
});

// Load default SERP, enter search mode.
add_task(async function different_search_mode_from_default_serp() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  info("Click on address bar.");
  EventUtils.synthesizeMouseAtCenter(window.gURLBar.inputField, {});
  await TestUtils.waitForCondition(() => window.gURLBar.focused);

  info("Enter search mode.");
  let engine = SearchService.getEngineByName("MochiSearch");
  Assert.notEqual(
    engine.name,
    SearchService.defaultEngine.name,
    "Engine is non-default."
  );
  await UrlbarTestUtils.enterSearchMode(window, {
    engineName: engine.name,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
  });
  Assert.ok(
    !window.gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  info("Blur address bar.");
  gURLBar.blur();
  await TestUtils.waitForCondition(() => !window.gURLBar.focused);
  Assert.ok(
    !window.gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  BrowserTestUtils.removeTab(tab);
});

// Load non-default SERP, escape search mode.
add_task(async function escape_search_mode_from_non_default_serp() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await searchWithNonDefaultSearchMode(tab);

  info("Exit search mode.");
  await UrlbarTestUtils.exitSearchMode(window, {
    clickClose: true,
    waitForSearch: false,
  });
  Assert.ok(
    !window.gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  info("Blur address bar.");
  gURLBar.blur();
  await TestUtils.waitForCondition(() => !window.gURLBar.focused);
  Assert.ok(
    !window.gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function clear_search_mode_refresh() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  await searchWithNonDefaultSearchMode(tab);

  info("Exit search mode.");
  await UrlbarTestUtils.exitSearchMode(window, {
    clickClose: true,
    waitForSearch: false,
  });
  await UrlbarTestUtils.promisePopupClose(window);
  await UrlbarTestUtils.assertSearchMode(window, null);

  let currentUrl = gBrowser.selectedBrowser.currentURI.spec;
  info("Reload page.");
  let promise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    currentUrl
  );
  gBrowser.selectedBrowser.reload();
  await promise;

  await TestUtils.waitForCondition(
    () => gURLBar.searchMode,
    "Waiting for search mode."
  );

  // UserTypedValue is set when the search mode has to be recovered.
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
  });
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    isGeneralPurposeEngine: true,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    isPreview: false,
    entry: "other",
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function clear_search_mode_switch_tab() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  await searchWithNonDefaultSearchMode(tab);

  info("Exit search mode.");
  await UrlbarTestUtils.exitSearchMode(window, {
    clickClose: true,
    waitForSearch: false,
  });
  await UrlbarTestUtils.promisePopupClose(window);
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
    persistSearchTerms: false,
  });
  await UrlbarTestUtils.assertSearchMode(window, null);

  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await BrowserTestUtils.switchTab(gBrowser, tab);

  // UserTypedValue is set when the search mode has to be recovered.
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
    persistSearchTerms: false,
  });
  await UrlbarTestUtils.assertSearchMode(window, null);

  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function tabhistory_searchmode_non_default() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  let { expectedSearchUrl } = await searchWithNonDefaultSearchMode(tab);

  info("Load a non-SERP URL.");
  let promise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    "https://www.example.ca/",
    true
  );
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://www.example.ca"
  );
  await promise;

  info(`Go back to ${expectedSearchUrl}`);
  promise = BrowserTestUtils.waitForContentEvent(tab.linkedBrowser, "pageshow");
  gBrowser.goBack(false);
  await promise;

  await TestUtils.waitForCondition(
    () => gURLBar.searchMode,
    "Waiting for search mode."
  );

  // UserTypedValue is set when the search mode has to be recovered.
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
  });
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    isGeneralPurposeEngine: true,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    isPreview: false,
    entry: "other",
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function tabhistory_searchmode_default_engine() {
  info("Load a search with a default search provider.");
  let { tab, expectedSearchUrl: defaultSearchUrl } =
    await searchWithTab(SEARCH_STRING);

  info("Load a search with a non-default search provider.");
  let { expectedSearchUrl: searchModeUrl } =
    await searchWithNonDefaultSearchMode(tab);

  info(`Go back to ${defaultSearchUrl}`);
  let promise = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  gBrowser.goBack(false);
  await promise;

  await TestUtils.waitForCondition(
    () => !gURLBar.searchMode,
    "Waiting for search mode to be absent."
  );
  assertSearchStringIsInUrlbar(SEARCH_STRING);
  await UrlbarTestUtils.assertSearchMode(window, null);

  info(`Go forward to ${searchModeUrl}`);
  promise = BrowserTestUtils.waitForContentEvent(tab.linkedBrowser, "pageshow");
  gBrowser.goForward(false);
  await promise;

  await TestUtils.waitForCondition(
    () => gURLBar.searchMode,
    "Waiting for search mode."
  );
  // UserTypedValue is set when the search mode has to be recovered.
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
  });
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    isGeneralPurposeEngine: true,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    isPreview: false,
    entry: "other",
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function search_mode_switch_tab_to_default() {
  let { tab: tab1 } = await searchWithTab(SEARCH_STRING);
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: SEARCH_STRING,
  });
  let engine = SearchService.getEngineByName("MochiSearch");
  await UrlbarTestUtils.enterSearchMode(window, {
    engineName: engine.name,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
  });

  await BrowserTestUtils.switchTab(gBrowser, tab1);

  info("Check default SERP is not in a search mode.");
  await UrlbarTestUtils.assertSearchMode(window, null);
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
  });

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function search_mode_switch_tab_to_non_default() {
  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await searchWithNonDefaultSearchMode(tab1);

  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: SEARCH_STRING,
  });
  let engine = SearchService.getEngineByName("Example");
  await UrlbarTestUtils.enterSearchMode(window, {
    engineName: engine.name,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
  });

  await BrowserTestUtils.switchTab(gBrowser, tab1);

  info("Check non-default SERP is showing the correct search mode.");
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "MochiSearch",
    isGeneralPurposeEngine: true,
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    isPreview: false,
    entry: "oneoff",
  });
  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    userTypedValue: SEARCH_STRING,
  });

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});
