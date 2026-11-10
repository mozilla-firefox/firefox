/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// These tests check the behavior of the Urlbar when loading a page
// whose url matches that of the default search engine.

// The main search string used in tests
const SEARCH_STRING = "chocolate cake";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.recentsearches.featureGate", false],
      ["browser.urlbar.showSearchTerms.featureGate", true],
    ],
  });
  let cleanup = await installPersistTestEngines();
  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    cleanup();
  });
});

// If a user does a search, goes to another page, and then
// goes back to the SERP, the search term should show.
add_task(async function go_back() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  let browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "http://www.example.com/some_url"
  );
  await browserLoadedPromise;

  let pageShowPromise = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  tab.linkedBrowser.goBack(false);
  await pageShowPromise;

  assertSearchStringIsInUrlbar(SEARCH_STRING);

  BrowserTestUtils.removeTab(tab);
});

add_task(async function go_back_to_newtab() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );

  let [expectedSearchUrl] = UrlbarUtils.getSearchQueryUrl(
    SearchService.defaultEngine,
    SEARCH_STRING
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedSearchUrl
  );

  gURLBar.focus();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: SEARCH_STRING,
    fireInputEvent: true,
    selectionStart: 0,
    selectionEnd: SEARCH_STRING.length - 1,
  });
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  Assert.equal(
    gURLBar.value,
    SEARCH_STRING,
    "Search string should be in the address bar after loading the search results page."
  );

  let pageShowPromise = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  tab.linkedBrowser.goBack(false);
  await pageShowPromise;

  Assert.equal(
    gURLBar.value,
    "",
    "Search string should be blank after going back to about:newtab."
  );

  BrowserTestUtils.removeTab(tab);
});

// Manually loading a url that matches a search query url
// should show the search term in the Urlbar.
add_task(async function load_url() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  let [expectedSearchUrl] = UrlbarUtils.getSearchQueryUrl(
    SearchService.defaultEngine,
    SEARCH_STRING
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedSearchUrl
  );
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, expectedSearchUrl);
  await browserLoadedPromise;
  assertSearchStringIsInUrlbar(SEARCH_STRING);

  BrowserTestUtils.removeTab(tab);
});

// Focusing and blurring the urlbar while the search terms
// persist should not change the "persistsearchterms" attribute.
add_task(async function focus_and_unfocus() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  Assert.ok(
    gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar has persistsearchterms attribute."
  );

  gURLBar.blur();
  Assert.ok(
    gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar has persistsearchterms attribute."
  );

  BrowserTestUtils.removeTab(tab);
});

// If the user modifies the search term, blurring the
// urlbar should remove persistsearchterms.
add_task(async function focus_and_unfocus_modified() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  Assert.ok(
    gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar has persistsearchterms attribute."
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: "another search term",
    fireInputEvent: true,
  });
  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "invalid",
    "Should have matching pageproxystate."
  );

  gURLBar.blur();
  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "invalid",
    "Should have matching pageproxystate."
  );
  Assert.ok(
    !gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  BrowserTestUtils.removeTab(tab);
});

// If Top Sites is cached in the UrlbarView, don't show it if the search terms
// persist in the Urlbar.
add_task(async function focus_after_top_sites() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.feeds.topsites", true]],
  });

  // Populate Top Sites on a clean version of Places.
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesTestUtils.promiseAsyncUpdates();
  await TestUtils.waitForTick();

  const urls = [];
  const N_TOP_SITES = 5;
  const N_VISITS = 5;

  for (let i = 0; i < N_TOP_SITES; i++) {
    let url = `https://${i}.example.com/hello_world${i}`;
    urls.unshift(url);
    // Each URL needs to be added several times to boost its frecency enough to
    // qualify as a top site.
    for (let j = 0; j < N_VISITS; j++) {
      await PlacesTestUtils.addVisits(url);
    }
  }

  let changedPromise = TestUtils.topicObserved("newtab-top-sites-changed").then(
    () => info("Observed newtab-top-sites-changed")
  );
  await updateTopSites(sites => sites?.length == N_TOP_SITES);
  await changedPromise;

  // Ensure Top Sites is cached.
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    N_TOP_SITES,
    `The number of results should be the same as the number of Top Sites ${N_TOP_SITES}.`
  );
  for (let i = 0; i < urls.length; i++) {
    let { url } = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.equal(url, urls[i], "The result url should be a Top Site.");
  }

  let [expectedSearchUrl] = UrlbarUtils.getSearchQueryUrl(
    SearchService.defaultEngine,
    SEARCH_STRING
  );
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: SEARCH_STRING,
    fireInputEvent: true,
  });
  EventUtils.synthesizeKey("KEY_Enter");

  await BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    expectedSearchUrl
  );
  let state = window.gURLBar.getBrowserState(window.gBrowser.selectedBrowser);
  Assert.equal(
    state.persist.searchTerms,
    SEARCH_STRING,
    "The search term should be in the Urlbar."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.notEqual(
    details.url,
    urls[0],
    "The first result should not be a Top Site."
  );
  Assert.equal(
    details.heuristic,
    true,
    "The first result should be the heuristic result."
  );
  Assert.equal(
    details.url,
    expectedSearchUrl,
    "The first result url should be the same as the SERP."
  );
  Assert.equal(
    details.type,
    UrlbarShared.RESULT_TYPE.SEARCH,
    "The first result be a search result."
  );
  Assert.equal(
    details.searchParams?.query,
    SEARCH_STRING,
    "The first result should have a matching query."
  );

  // Clean up.
  SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
});
