/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that search mode is stored per tab and restored when switching tabs.
 */

"use strict";

// Enters search mode using the one-off buttons.
add_task(async function switchTabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });

  // Open three tabs.  We'll enter search mode in tabs 0 and 2.
  let tabs = [];
  for (let i = 0; i < 3; i++) {
    let tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: "http://example.com/" + i,
    });
    tabs.push(tab);
  }

  // Switch to tab 0.
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);

  // Do a search and enter search mode.  Pass fireInputEvent so that
  // userTypedValue is set and restored when we switch back to this tab.  This
  // isn't really necessary but it simulates the user's typing, and it also
  // means that we'll start a search when we switch back to this tab.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
    fireInputEvent: true,
  });
  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
  });

  // Switch to tab 1.  Search mode should be exited.
  await BrowserTestUtils.switchTab(gBrowser, tabs[1]);
  await UrlbarTestUtils.assertSearchMode(window, null);

  // Switch back to tab 0.  We should do a search (for "test") and re-enter
  // search mode.
  let searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "oneoff",
  });
  Assert.equal(
    gURLBar.value,
    "test",
    "Value should remain the search string after switching back"
  );

  // Switch to tab 2.  Search mode should be exited.
  await BrowserTestUtils.switchTab(gBrowser, tabs[2]);
  await UrlbarTestUtils.assertSearchMode(window, null);

  // Do another search (in tab 2) and enter search mode.  Use a different source
  // from tab 0 just to use something different.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test tab 2",
    fireInputEvent: true,
  });
  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.TABS,
  });

  // Switch back to tab 0.  We should do a search and still be in search mode.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "oneoff",
  });
  Assert.equal(
    gURLBar.value,
    "test",
    "Value should remain the search string after switching back"
  );

  // Switch to tab 1.  Search mode should be exited.
  await BrowserTestUtils.switchTab(gBrowser, tabs[1]);
  await UrlbarTestUtils.assertSearchMode(window, null);

  // Switch back to tab 2.  We should do a search and re-enter search mode.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[2]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.TABS,
    entry: "oneoff",
  });
  Assert.equal(
    gURLBar.value,
    "test tab 2",
    "Value should remain the search string after switching back"
  );

  // Exit search mode.
  await UrlbarTestUtils.exitSearchMode(window, { clickClose: true });

  // Switch to tab 0.  We should do a search and re-enter search mode.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "oneoff",
  });
  Assert.equal(
    gURLBar.value,
    "test",
    "Value should remain the search string after switching back"
  );

  // Switch back to tab 2.  We should do a search but search mode should be
  // inactive.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[2]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, null);
  Assert.equal(
    gURLBar.value,
    "test tab 2",
    "Value should remain the search string after switching back"
  );

  // Switch back to tab 0.  We should do a search and re-enter search mode.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "oneoff",
  });
  Assert.equal(
    gURLBar.value,
    "test",
    "Value should remain the search string after switching back"
  );

  // Exit search mode.
  await UrlbarTestUtils.exitSearchMode(window, { clickClose: true });

  // Switch back to tab 2.  We should do a search but search mode should be
  // inactive.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[2]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, null);
  Assert.equal(
    gURLBar.value,
    "test tab 2",
    "Value should remain the search string after switching back"
  );

  // Switch back to tab 0.  We should do a search but search mode should be
  // inactive.
  searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, null);
  Assert.equal(
    gURLBar.value,
    "test",
    "Value should remain the search string after switching back"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  for (let tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
});

// Start loading a SERP from search mode then immediately switch to a new tab so
// the SERP finishes loading in the background. Switch back to the SERP tab and
// observe that we don't re-enter search mode despite having an entry for that
// tab in UrlbarInput._searchModesByBrowser. See bug 1675926.
//
// This subtest intermittently does not test bug 1675926 (NB: this does not mean
// it is an intermittent failure). The false-positive occurs if the SERP page
// finishes loading before we switch tabs. We include this subtest so we have
// one covering real-world behaviour. A subtest that is guaranteed to test this
// behaviour that does not simulate real world behaviour is included below.
add_task(async function slow_load() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.searches", false]],
  });
  const engineName = "Test";
  let extension = await SearchTestUtils.installSearchExtension(
    {
      name: engineName,
    },
    { skipUnload: true }
  );

  const originalTab = gBrowser.selectedTab;
  const newTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
    fireInputEvent: true,
  });
  await UrlbarTestUtils.enterSearchMode(window, { engineName });

  const loadPromise = BrowserTestUtils.browserLoaded(newTab.linkedBrowser);
  // Select the search mode heuristic to load the example.com SERP.
  EventUtils.synthesizeKey("KEY_Enter");
  // Switch away from the tab before we let it load.
  await BrowserTestUtils.switchTab(gBrowser, originalTab);
  await loadPromise;

  // Switch back to the search mode tab and confirm we don't restore search
  // mode.
  await BrowserTestUtils.switchTab(gBrowser, newTab);
  await UrlbarTestUtils.assertSearchMode(window, null);

  BrowserTestUtils.removeTab(newTab);
  await SpecialPowers.popPrefEnv();
  await extension.unload();
});

// Tests the same behaviour as slow_load, but in a more reliable way using
// non-real-world behaviour.
add_task(async function slow_load_guaranteed() {
  const engineName = "Test";
  let extension = await SearchTestUtils.installSearchExtension(
    {
      name: engineName,
    },
    { skipUnload: true }
  );

  const backgroundTab = BrowserTestUtils.addTab(gBrowser);

  // Simulate a tab that was in search mode, loaded a SERP, then was switched
  // away from before setURI was called.
  backgroundTab.documentGlobal.gURLBar.searchMode = { engineName };
  let loadPromise = BrowserTestUtils.browserLoaded(backgroundTab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    backgroundTab.linkedBrowser,
    "http://example.com/?search=test"
  );
  await loadPromise;

  // Switch to the background mode tab and confirm we don't restore search mode.
  await BrowserTestUtils.switchTab(gBrowser, backgroundTab);
  await UrlbarTestUtils.assertSearchMode(window, null);

  BrowserTestUtils.removeTab(backgroundTab);
  await extension.unload();
});

// Enters search mode by typing a restriction char with no search string.
// Search mode and the search string should be restored after switching back to
// the tab.
add_task(async function userTypedValue_empty() {
  await doUserTypedValueTest("");
});

// Enters search mode by typing a restriction char followed by a search string.
// Search mode and the search string should be restored after switching back to
// the tab.
add_task(async function userTypedValue_nonEmpty() {
  await doUserTypedValueTest("foo bar");
});

/**
 * Enters search mode by typing a restriction char followed by a search string,
 * opens a new tab and immediately closes it so we switch back to the search
 * mode tab, and checks the search mode state and input value.
 *
 * @param {string} searchString
 *   The search string to enter search mode with.
 */
async function doUserTypedValueTest(searchString) {
  let value = `${UrlbarShared.RESTRICT_TOKENS.BOOKMARK} ${searchString}`;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value,
    fireInputEvent: true,
  });
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "typed",
    restrictType: "symbol",
  });
  Assert.equal(
    gURLBar.value,
    searchString,
    "Sanity check: Value is the search string"
  );

  let tab = await BrowserTestUtils.openNewForegroundTab({ gBrowser });
  BrowserTestUtils.removeTab(tab);

  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "typed",
    restrictType: "symbol",
  });
  Assert.equal(
    gURLBar.value,
    searchString,
    "Value should remain the search string after switching back"
  );

  gURLBar.handleRevert();
}
