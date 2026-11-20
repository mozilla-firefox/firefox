/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This test ensures that we produce good labels for a11y purposes.
 */

const { CommonUtils } = ChromeUtils.importESModule(
  "chrome://mochitests/content/browser/accessible/tests/browser/Common.sys.mjs"
);

const SUGGEST_ALL_PREF = "browser.search.suggest.enabled";
const SUGGEST_URLBAR_PREF = "browser.urlbar.suggest.searches";
const TEST_ENGINE_BASENAME = "searchSuggestionEngine.xml";

let accService;

async function getResultText(element, expectedValue, description = "") {
  await TestUtils.waitForCondition(
    () => {
      let accessible = accService.getAccessibleFor(element);
      return accessible !== null && accessible.name === expectedValue;
    },
    description,
    200
  );
}

/**
 * Initializes the accessibility service and registers a cleanup function to
 * shut it down. If it's not shut down properly, it can crash the current tab
 * and cause the test to fail, especially in verify mode.
 *
 * This function is adapted from from tests in accessible/tests/browser and its
 * helper functions are adapted or copied from functions of the same names in
 * the same directory.
 */
async function initAccessibilityService() {
  const [a11yInitObserver, a11yInit] = initAccService();
  await a11yInitObserver;
  accService = Cc["@mozilla.org/accessibilityService;1"].getService(
    Ci.nsIAccessibilityService
  );
  await a11yInit;

  registerCleanupFunction(async () => {
    const [a11yShutdownObserver, a11yShutdownPromise] = shutdownAccService();
    await a11yShutdownObserver;
    accService = null;
    forceGC();
    await a11yShutdownPromise;
  });
}

// Adapted from `initAccService()` in accessible/tests/browser/head.js
function initAccService() {
  return [
    CommonUtils.addAccServiceInitializedObserver(),
    CommonUtils.observeAccServiceInitialized(),
  ];
}

// Adapted from `shutdownAccService()` in accessible/tests/browser/head.js
function shutdownAccService() {
  return [
    CommonUtils.addAccServiceShutdownObserver(),
    CommonUtils.observeAccServiceShutdown(),
  ];
}

// Copied from accessible/tests/browser/shared-head.js
function forceGC() {
  SpecialPowers.gc();
  SpecialPowers.forceShrinkingGC();
  SpecialPowers.forceCC();
  SpecialPowers.gc();
  SpecialPowers.forceShrinkingGC();
  SpecialPowers.forceCC();
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });

  await initAccessibilityService();
});

add_task(async function switchToTab() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:robots");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "% robots",
  });

  let index = 0;
  let result = await UrlbarTestUtils.getDetailsOfResultAt(window, index);
  Assert.equal(
    result.type,
    UrlbarShared.RESULT_TYPE.TAB_SWITCH,
    "Should have a switch tab result"
  );

  let element = await UrlbarTestUtils.waitForAutocompleteResultAt(
    window,
    index
  );
  // The a11y text will include the "Firefox Suggest" pseudo-element label shown
  // before the result.
  await getResultText(
    element._content,
    "Firefox Suggest about:robots — Switch to Tab",
    "Result a11y text is correct"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
  gBrowser.removeTab(tab);
});

add_task(async function searchSuggestions() {
  await SearchTestUtils.installOpenSearchEngine({
    url: getRootDirectory(gTestPath) + TEST_ENGINE_BASENAME,
    setAsDefault: true,
  });
  Services.prefs.setBoolPref(SUGGEST_ALL_PREF, true);
  let suggestionsEnabled = Services.prefs.getBoolPref(SUGGEST_URLBAR_PREF);
  Services.prefs.setBoolPref(SUGGEST_URLBAR_PREF, true);
  registerCleanupFunction(async function () {
    Services.prefs.clearUserPref(SUGGEST_ALL_PREF);
    Services.prefs.setBoolPref(SUGGEST_URLBAR_PREF, suggestionsEnabled);
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "foo",
  });
  let length = await UrlbarTestUtils.getResultCount(window);
  // Don't assume that the search doesn't match history or bookmarks left around
  // by earlier tests.
  Assert.greaterOrEqual(
    length,
    3,
    "Should get at least heuristic result + two search suggestions"
  );
  // The first expected search is the search term itself since the heuristic
  // result will come before the search suggestions.
  let searchTerm = "foo";
  let expectedSearches = [searchTerm, "foofoo", "foobar"];
  for (let i = 0; i < length; i++) {
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (result.type === UrlbarShared.RESULT_TYPE.SEARCH) {
      Assert.greaterOrEqual(
        expectedSearches.length,
        0,
        "Should still have expected searches remaining"
      );

      let element = await UrlbarTestUtils.waitForAutocompleteResultAt(
        window,
        i
      );

      // Select the row so we see the expanded text.
      gURLBar.view.selectedRowIndex = i;

      if (result.searchParams.inPrivateWindow) {
        await getResultText(
          element._content,
          searchTerm + " — Search in a Private Window",
          "Check result label for search in private window"
        );
      } else {
        let suggestion = expectedSearches.shift();
        await getResultText(
          element._content,
          suggestion +
            " — Search with browser_searchSuggestionEngine searchSuggestionEngine.xml",
          "Check result label for non-private search"
        );
      }
    }
  }
  Assert.ok(!expectedSearches.length);

  await UrlbarTestUtils.promisePopupClose(window);
});
