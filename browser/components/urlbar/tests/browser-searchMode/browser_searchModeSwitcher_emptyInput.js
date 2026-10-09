/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the Unified Search Button with an empty search input.
 */

"use strict";

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      // Tests get no default Top Sites, and the empty-string opening used to
      // reveal the Unified Search Button would otherwise leave the view closed
      // with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

// Tests that search mode chiclet remains when input is empty and
// urlbar is unfocused.
add_task(async function test_search_mode_chiclet_unfocus_home_page() {
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    ".search-button-bookmarks"
  );

  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    entry: "searchbutton",
  });

  // Shift focus away from urlbar, similar to clicking on page content
  // area.
  EventUtils.synthesizeKey("KEY_Tab");

  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    entry: "searchbutton",
  });
});

// Tests that search mode chiclet remains on loaded sites as well.
add_task(async function test_search_mode_chiclet_unfocus_loaded_sites() {
  let newTab = await BrowserTestUtils.openNewForegroundTab(
    window.gBrowser,
    "https://example.com"
  );

  // Make unified search button appear
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    ".search-button-bookmarks"
  );

  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    entry: "searchbutton",
  });

  // Shift focus away from urlbar, similar to clicking on page content
  // area.
  EventUtils.synthesizeKey("KEY_Tab");

  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    entry: "searchbutton",
  });

  BrowserTestUtils.removeTab(newTab);
});
