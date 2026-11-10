/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser/head-common.js",
  this
);

registerCleanupFunction(async () => {
  await UrlbarTestUtils.promisePopupClose(window);
});

/**
 * Asserts a search term is in the url bar and state values are
 * what they should be.
 *
 * @param {string} searchString
 *   String that should be matched in the url bar.
 * @param {object | null} options
 *   Options for the assertions.
 * @param {Window | null} options.window
 *   Window to use for tests.
 * @param {string | null} options.pageProxyState
 *   The pageproxystate that should be expected.
 * @param {string | null} options.userTypedValue
 *   The userTypedValue that should be expected.
 * @param {boolean | null} options.persistSearchTerms
 *   The attribute persistsearchterms that should be expected.
 */
function assertSearchStringIsInUrlbar(
  searchString,
  {
    win = window,
    pageProxyState = "invalid",
    userTypedValue = searchString,
    persistSearchTerms = true,
  } = {}
) {
  Assert.equal(
    win.gURLBar.value,
    searchString,
    `Search string should be the urlbar value.`
  );
  let state = win.gURLBar.getBrowserState(win.gBrowser.selectedBrowser);
  Assert.equal(
    state.persist?.searchTerms,
    searchString,
    `Search terms should match.`
  );
  Assert.equal(
    win.gBrowser.userTypedValue,
    userTypedValue,
    "userTypedValue should match."
  );
  Assert.equal(
    win.gURLBar.getAttribute("pageproxystate"),
    pageProxyState,
    "Pageproxystate should match."
  );
  if (persistSearchTerms) {
    Assert.ok(
      win.gURLBar.hasAttribute("persistsearchterms"),
      "Urlbar has persistsearchterms attribute."
    );
  } else {
    Assert.ok(
      !win.gURLBar.hasAttribute("persistsearchterms"),
      "Urlbar does not have persistsearchterms attribute."
    );
  }
}

async function searchWithTab(
  searchString,
  tab = null,
  engine = SearchService.defaultEngine,
  expectedPersistedSearchTerms = true
) {
  if (!tab) {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  }

  let [expectedSearchUrl] = UrlbarUtils.getSearchQueryUrl(engine, searchString);
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedSearchUrl
  );

  gURLBar.focus();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: searchString,
    fireInputEvent: true,
    selectionStart: 0,
    selectionEnd: searchString.length - 1,
  });
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  if (expectedPersistedSearchTerms) {
    info("Load a tab with search terms persisting in the urlbar.");
    assertSearchStringIsInUrlbar(searchString);
  }

  return { tab, expectedSearchUrl };
}
