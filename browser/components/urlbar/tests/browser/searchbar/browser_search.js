/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const CANONIZE_MODIFIERS =
  AppConstants.platform == "macosx" ? { metaKey: true } : { ctrlKey: true };
const UNKNOWN_REASON = SearchService.CHANGE_REASON.UNKNOWN;

let searchbar;
let engine1;
let engine2;

add_setup(async function () {
  searchbar = document.getElementById("searchbar-new");
  await SearchTestUtils.updateRemoteSettingsConfig([
    { identifier: "engine1" },
    {
      identifier: "engine2",
      base: {
        urls: {
          search: {
            base: "https://example.com/2",
            searchTermParamName: "q",
          },
        },
      },
    },
  ]);
  engine1 = SearchService.defaultEngine;
  engine2 = SearchService.getEngineById("engine2");
});

/**
 * Waits until the searchbar's engine store reflects the given default engine.
 * The store is updated through the actor, so it can still hold the previous
 * default when SearchService.setDefault() resolves.
 *
 * @param {object} engine - The expected default engine.
 */
function promiseObservedDefaultEngine(engine) {
  return TestUtils.waitForCondition(
    () => searchbar.controller.engineStore.default?.name == engine.name,
    "Waiting for the searchbar to observe the new default engine"
  );
}

function revertUsingEscape() {
  Assert.ok(searchbar.value, "Searchbar is not empty");
  SearchbarTestUtils.promisePopupOpen(window, () => searchbar.focus());
  SearchbarTestUtils.promisePopupClose(window, () =>
    EventUtils.synthesizeKey("KEY_Escape")
  );
  EventUtils.synthesizeKey("KEY_Escape");
  Assert.ok(!searchbar.value, "Searchbar was cleared");
}

add_task(async function test_simple() {
  // This pref should not affect the searchbar.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.openintab", true]],
  });

  let searchTerm = "test";
  searchbar.focus();
  EventUtils.sendString(searchTerm);
  EventUtils.synthesizeKey("KEY_Enter");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;
  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  searchbar.handleRevert();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_no_canonization() {
  let searchTerm = "test2";
  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;

  searchbar.focus();
  EventUtils.sendString(searchTerm);

  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
  EventUtils.synthesizeKey("KEY_Enter", CANONIZE_MODIFIERS);
  let newTab = await newTabPromise;
  let newBrowser = gBrowser.getBrowserForTab(newTab);

  Assert.equal(gBrowser.selectedBrowser, newBrowser, "Opened in foreground");
  Assert.equal(newBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  searchbar.handleRevert();
  BrowserTestUtils.removeTab(newTab);
});

add_task(async function test_newtab_alt() {
  let searchTerm = "test3";
  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;

  searchbar.focus();
  EventUtils.sendString(searchTerm);

  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
  EventUtils.synthesizeKey("KEY_Enter", { altKey: true });
  let newTab = await newTabPromise;
  let newBrowser = gBrowser.getBrowserForTab(newTab);

  Assert.equal(gBrowser.selectedBrowser, newBrowser, "Opened in foreground");
  Assert.equal(newBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  searchbar.handleRevert();
  BrowserTestUtils.removeTab(newTab);
});

add_task(async function test_newtab_pref() {
  SpecialPowers.pushPrefEnv({
    set: [["browser.search.openintab", true]],
  });
  let searchTerm = "test4";
  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;

  searchbar.focus();
  EventUtils.sendString(searchTerm);

  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
  EventUtils.synthesizeKey("KEY_Enter");
  let newTab = await newTabPromise;
  let newBrowser = gBrowser.getBrowserForTab(newTab);

  Assert.equal(gBrowser.selectedBrowser, newBrowser, "Opened in foreground");
  Assert.equal(newBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  searchbar.handleRevert();
  BrowserTestUtils.removeTab(newTab);
  SpecialPowers.popPrefEnv();
});

// See bug 2013883.
add_task(async function test_switch_engine() {
  let searchTerm = "test5";
  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;
  let expectedUrl2 = engine2.getSubmission(searchTerm).uri.spec;

  searchbar.focus();
  EventUtils.sendString(searchTerm);

  EventUtils.synthesizeKey("KEY_Enter");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  await SearchService.setDefault(engine2, UNKNOWN_REASON);
  await promiseObservedDefaultEngine(engine2);

  EventUtils.synthesizeMouseAtCenter(searchbar.goButton, {});
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  Assert.equal(gBrowser.currentURI.spec, expectedUrl2, "Used engine2");

  searchbar.handleRevert();
  await SearchService.setDefault(engine1, UNKNOWN_REASON);
  await promiseObservedDefaultEngine(engine1);
});

add_task(async function test_paste_and_go() {
  // Search for a URL to make sure it doesn't simply open it.
  let searchTerm = "https://example.com/test6/";
  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;

  await SimpleTest.promiseClipboardChange(searchTerm, () => {
    clipboardHelper.copyString(searchTerm);
  });

  await SearchbarTestUtils.activateContextMenuItem(window, "paste-and-go");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Started the search");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");
  searchbar.handleRevert();
});

add_task(async function test_revert_and_go_visibility() {
  let goButton = searchbar.goButton;
  let searchTerm = "test7";
  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;

  Assert.ok(!BrowserTestUtils.isVisible(goButton), "Go button is not visible");
  searchbar.focus();
  EventUtils.sendString(searchTerm);
  Assert.ok(BrowserTestUtils.isVisible(goButton), "Go button becomes visible");

  EventUtils.synthesizeKey("KEY_Enter");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  revertUsingEscape();
  Assert.ok(!BrowserTestUtils.isVisible(goButton), "Go button is invisible");
});

add_task(async function test_privateDefault() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.separatePrivateDefault.enabled", true],
      ["browser.search.separatePrivateDefault.featureGate", true],
    ],
  });
  let searchTerm = "test8";
  let expectedUrl = engine2.getSubmission(searchTerm).uri.spec;
  info("Changing private engine.");
  await SearchService.setDefaultPrivate(engine2, UNKNOWN_REASON);

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let searchbarPrivateWin = win.document.querySelector("#searchbar-new");

  info("Searching in private window.");
  searchbarPrivateWin.focus();
  EventUtils.sendString(searchTerm, win);
  EventUtils.synthesizeKey("KEY_Enter", {}, win);
  await BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);
  Assert.equal(
    win.gBrowser.currentURI.spec,
    expectedUrl,
    "Used private engine"
  );

  await BrowserTestUtils.closeWindow(win);
  await SearchService.setDefaultPrivate(engine1, UNKNOWN_REASON);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_searchForm() {
  let expectedUrl = engine1.searchForm + "/";

  info("Open searchform in current tab.");
  searchbar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  info("Waiting for load");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Opened searchform");

  info("Open searchform in new tab.");
  searchbar.focus();
  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
  // CANONIZE_MODIFIERS in searchbar means new tab because
  // canonization is not supported in the search bar.
  EventUtils.synthesizeKey("KEY_Enter", CANONIZE_MODIFIERS);
  let newTab = await newTabPromise;
  let newBrowser = gBrowser.getBrowserForTab(newTab);
  Assert.equal(gBrowser.selectedBrowser, newBrowser, "Opened in foreground");
  Assert.equal(newBrowser.currentURI.spec, expectedUrl, "Opened searchform");
  BrowserTestUtils.removeTab(newTab);

  info("Open searchform in new window.");
  searchbar.focus();
  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  EventUtils.synthesizeKey("KEY_Enter", { shiftKey: true });
  let newWin = await newWindowPromise;
  newBrowser = newWin.gBrowser.selectedBrowser;
  if (newBrowser.currentURI.spec != expectedUrl) {
    await BrowserTestUtils.browserLoaded(newBrowser);
  }
  Assert.equal(newBrowser.currentURI.spec, expectedUrl, "Opened searchform");
  await BrowserTestUtils.closeWindow(newWin);
});
