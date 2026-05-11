/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Tests the behavior of the Urlbar when Persisted Search and
 * the Unified Button (Search Mode Switcher) are both enabled.
 */

let nonDefaultEngine;

// The main search keyword used in tests
const SEARCH_STRING = "chocolate cake";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      ["browser.urlbar.showSearchTerms.featureGate", true],
    ],
  });
  let cleanup = await installPersistTestEngines();
  nonDefaultEngine = SearchService.getEngineByName("MochiSearch");
  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    cleanup();
  });
});

add_task(async function visibility_of_elements() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  let urlbar = window.gURLBar;

  Assert.equal(
    BrowserTestUtils.isVisible(urlbar.querySelector(".searchmode-switcher")),
    true,
    "Unified Search Button is visible."
  );

  Assert.equal(
    BrowserTestUtils.isVisible(urlbar.querySelector(".urlbar-revert-button")),
    true,
    "Revert button is visible."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function select_default_engine_and_search() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  info("Press on the Example menu button and enter Search Mode.");
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-name=Example]"
  );

  info("Search with the default engine.");
  let [url] = UrlbarUtils.getSearchQueryUrl(
    SearchService.defaultEngine,
    SEARCH_STRING
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    url
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  Assert.equal(gURLBar.value, SEARCH_STRING, "Urlbar value");

  BrowserTestUtils.removeTab(tab);
});

add_task(async function select_non_default_engine_and_search() {
  let { tab } = await searchWithTab(SEARCH_STRING);

  let [url] = UrlbarUtils.getSearchQueryUrl(nonDefaultEngine, SEARCH_STRING);
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    url,
    true
  );

  info("Press on the non default menu button and perform search.");
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-name=MochiSearch]"
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  assertSearchStringIsInUrlbar(SEARCH_STRING);

  BrowserTestUtils.removeTab(tab);
});

add_task(async function revert_button() {
  let { tab, expectedSearchUrl } = await searchWithTab(SEARCH_STRING);

  info("Click revert button.");
  let urlbar = window.gURLBar;
  let revertButton = urlbar.querySelector(".urlbar-revert-button");
  EventUtils.synthesizeMouseAtCenter(revertButton, {}, window);

  Assert.notEqual(
    gURLBar.value,
    SEARCH_STRING,
    `Search string ${SEARCH_STRING} should not be in the url bar`
  );

  let expectedUrl = UrlbarTestUtils.trimURL(expectedSearchUrl);
  Assert.equal(
    gURLBar.value,
    expectedUrl,
    `Urlbar should have ${expectedUrl} as value.`
  );

  Assert.ok(
    BrowserTestUtils.isHidden(revertButton),
    "Revert button is hidden."
  );

  Assert.ok(
    !gURLBar.hasAttribute("persistsearchterms"),
    "Urlbar does not have persistsearchterms attribute."
  );

  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "valid",
    "Page proxy state."
  );

  BrowserTestUtils.removeTab(tab);
});
