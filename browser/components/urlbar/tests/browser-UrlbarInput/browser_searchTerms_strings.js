/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This test checks whether certain patterns of search terms will show
// in the Urlbar as a search term.

ChromeUtils.defineESModuleGetters(this, {
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

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

// Search terms should show up in the url bar if the pref is on
// and the SERP url matches the one constructed in Firefox
add_task(async function search_strings() {
  const searches = [
    // Single word
    "chocolate",
    // Word with space
    "chocolate cake",
    // Allowable special characters.
    "chocolate;,?@&=+$-_!~*'()#cake",
    // Period used after the first word.
    "what is 255.255.255.255",
    // Protocol used after the first word.
    "what is https://",
    // Search with special characters
    '"chocolate cake" -recipes',
    "window.location how to use",
    "http",
    "https",
    // Long string within threshold.
    "h".repeat(UrlbarShared.MAX_TEXT_LENGTH),
  ];

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  for (let searchString of searches) {
    info("Search for term:", searchString);
    let [searchUrl] = UrlbarUtils.getSearchQueryUrl(
      SearchService.defaultEngine,
      searchString
    );
    let browserLoadedPromise = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      searchUrl
    );
    BrowserTestUtils.startLoadingURIString(gBrowser, searchUrl);
    await browserLoadedPromise;
    assertSearchStringIsInUrlbar(searchString);

    info("Check that no formatting is applied.");
    await UrlbarTestUtils.checkFormatting(window, searchString);
  }

  BrowserTestUtils.removeTab(tab);
});
