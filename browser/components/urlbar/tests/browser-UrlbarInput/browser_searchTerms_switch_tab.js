/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// These tests check the behavior of the Urlbar when search terms are shown
// and the user switches between tabs.

// The main search keyword used in tests
const SEARCH_STRING = "chocolate cake";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.showSearchTerms.featureGate", true]],
  });
  let cleanup = await installPersistTestEngines();
  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    cleanup();
  });
});

// Users should be able to search, change the tab, and come
// back to the original tab to see the search term again
add_task(async function change_tab() {
  let { tab: tab1 } = await searchWithTab(SEARCH_STRING);
  let { tab: tab2 } = await searchWithTab("another keyword");
  let { tab: tab3 } = await searchWithTab("yet another keyword");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  assertSearchStringIsInUrlbar(SEARCH_STRING);

  await BrowserTestUtils.switchTab(gBrowser, tab2);
  assertSearchStringIsInUrlbar("another keyword");

  await BrowserTestUtils.switchTab(gBrowser, tab3);
  assertSearchStringIsInUrlbar("yet another keyword");

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab3);
});

// If a user types in the URL bar, and the user goes to a
// different tab, the original tab should still contain the
// text written by the user.
add_task(async function user_overwrites_search_term() {
  let { tab: tab1 } = await searchWithTab(SEARCH_STRING);

  let modifiedSearchTerm = SEARCH_STRING + " ideas";
  await UrlbarTestUtils.inputIntoURLBar(window, modifiedSearchTerm);
  gURLBar.blur();

  Assert.notEqual(
    gURLBar.value,
    SEARCH_STRING,
    `Search string ${SEARCH_STRING} should not be in the url bar`
  );

  Assert.ok(
    !gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "invalid",
    "Page proxy state."
  );

  // Open a new tab, switch back to the first and
  // check that the user typed value is still there.
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  Assert.equal(
    gURLBar.value,
    modifiedSearchTerm,
    `${modifiedSearchTerm} should be in the url bar`
  );

  Assert.ok(
    !gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "invalid",
    "Page proxy state."
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

// If a user clears the URL bar, and goes to a different tab,
// and returns to the initial tab, it should show the search term again.
add_task(async function user_overwrites_search_term_with_blank_string() {
  let { tab: tab1 } = await searchWithTab(SEARCH_STRING);

  gURLBar.focus();
  gURLBar.select();
  EventUtils.sendKey("delete");

  Assert.equal(gURLBar.value, "", "Empty string should be in url bar.");
  gURLBar.blur();

  // Open a new tab, switch back to the first and check
  // the blank string is replaced with the search string.
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Technically, the userTypedValue is a blank string instead of null because
  // they cleared it.
  assertSearchStringIsInUrlbar(SEARCH_STRING);

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_behavior_with_navigation() {
  let { tab: searchTab } = await searchWithTab(SEARCH_STRING);
  let originalURL = searchTab.linkedBrowser.currentURI.spec;

  let testURL = "http://mochi.test:8888/";

  let parseResult = SearchService.parseSubmissionURL(testURL);
  Assert.equal(parseResult.engine, null, "Url doesn't match an engine.");

  Assert.notEqual(
    new URL(originalURL).origin,
    new URL(testURL).origin,
    "Different origins."
  );

  let dummyTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  let loadPromise = BrowserTestUtils.browserLoaded(
    searchTab.linkedBrowser,
    false,
    testURL,
    true
  );
  BrowserTestUtils.startLoadingURIString(searchTab.linkedBrowser, testURL);
  await loadPromise;

  await BrowserTestUtils.switchTab(gBrowser, searchTab);

  Assert.ok(
    !window.gURLBar.hasAttribute("persistsearchterms"),
    "Search terms should not persist."
  );

  Assert.equal(
    window.gURLBar.getAttribute("pageproxystate"),
    "valid",
    "Urlbar has a valid pageproxystate."
  );

  Assert.equal(
    window.gURLBar.value,
    "http://mochi.test:8888",
    "Urlbar displays the expected url."
  );

  BrowserTestUtils.removeTab(searchTab);
  BrowserTestUtils.removeTab(dummyTab);
});
