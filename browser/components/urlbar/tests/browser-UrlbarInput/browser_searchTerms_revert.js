/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// These tests check the behavior of the Urlbar when search terms are shown
// and the user reverts the Urlbar.

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

function synthesizeRevert() {
  gURLBar.focus();
  info("Escape to revert Urlbar.");
  EventUtils.synthesizeKey("KEY_Escape");
}

// Users should be able to revert the URL bar
add_task(async function double_escape_revert() {
  let { tab, expectedSearchUrl } = await searchWithTab(SEARCH_STRING);
  synthesizeRevert();

  Assert.equal(
    gURLBar.value,
    UrlbarTestUtils.trimURL(expectedSearchUrl),
    `Urlbar should have the reverted URI ${expectedSearchUrl} as its value.`
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function click_revert() {
  let { tab, expectedSearchUrl } = await searchWithTab(SEARCH_STRING);

  info("Click on Revert button.");
  let revertButton = gURLBar.querySelector(".urlbar-revert-button");
  revertButton.click();

  Assert.equal(
    gURLBar.value,
    UrlbarTestUtils.trimURL(expectedSearchUrl),
    `Urlbar should have the reverted URI ${expectedSearchUrl} as its value.`
  );

  Assert.ok(gURLBar.focused, "Urlbar is focused.");

  BrowserTestUtils.removeTab(tab);
});

// Users should be able to revert the URL bar,
// and go to the same page.
add_task(async function revert_and_press_enter() {
  let { tab, expectedSearchUrl } = await searchWithTab(SEARCH_STRING);
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedSearchUrl
  );

  synthesizeRevert();
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  BrowserTestUtils.removeTab(tab);
});

// Users should be able to revert the URL, and then if they navigate
// to another tab, the tab that was reverted will show the search term again.
add_task(async function revert_and_change_tab() {
  let { tab, expectedSearchUrl } = await searchWithTab(SEARCH_STRING);

  synthesizeRevert();

  Assert.notEqual(
    gURLBar.value,
    SEARCH_STRING,
    `Search string ${SEARCH_STRING} should not be in the url bar`
  );
  Assert.equal(
    gURLBar.value,
    UrlbarTestUtils.trimURL(expectedSearchUrl),
    `Urlbar should have ${expectedSearchUrl} as value.`
  );

  // Open another tab
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  // Switch back to the original tab.
  await BrowserTestUtils.switchTab(gBrowser, tab);

  assertSearchStringIsInUrlbar(SEARCH_STRING, {
    pageProxyState: "invalid",
  });

  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(tab2);
});

// If a user reverts a tab, and then does another search,
// they should be able to see the search term again.
add_task(async function revert_and_search_again() {
  let { tab } = await searchWithTab(SEARCH_STRING);
  synthesizeRevert();
  await searchWithTab("another search string", tab);
  BrowserTestUtils.removeTab(tab);
});

// If a user reverts the Urlbar while on a default SERP,
// and they navigate away from the page by visiting another
// link or using the back/forward buttons, the Urlbar should
// show the search term again when returning back to the default SERP.
add_task(async function revert_when_using_content() {
  let { tab } = await searchWithTab(SEARCH_STRING);
  synthesizeRevert();
  await searchWithTab("another search string", tab);

  // Revert the page, and then go back and forth in history.
  // The search terms should show up.
  synthesizeRevert();
  let pageShowPromise = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  tab.linkedBrowser.goBack();
  Assert.ok(gURLBar.focused, "Address bar is focused.");
  await pageShowPromise;
  assertSearchStringIsInUrlbar(SEARCH_STRING);

  pageShowPromise = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  tab.linkedBrowser.goForward();
  await pageShowPromise;
  Assert.ok(gURLBar.focused, "Address bar is focused.");
  assertSearchStringIsInUrlbar("another search string");

  BrowserTestUtils.removeTab(tab);
});
