/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  TopSites: "moz-src:///browser/components/topsites/TopSites.sys.mjs",
});

const EN_US_TOPSITES =
  "https://www.youtube.com/,https://www.facebook.com/,https://www.amazon.com/,https://www.reddit.com/,https://www.wikipedia.org/,https://twitter.com/";

async function addTestVisits() {
  // Add some visits to a URL.
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits("http://example.com/");
  }

  // Wait for example.com to be listed first.
  await updateTopSites(sites => {
    return sites && sites[0] && sites[0].url == "http://example.com/";
  });

  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "https://www.youtube.com/",
    title: "YouTube",
  });

  // Adding a bookmark will refresh the TopSites feed.
  if (Services.prefs.getBoolPref("browser.topsites.component.enabled")) {
    await TestUtils.topicObserved("topsites-refreshed");
  }
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.topsites", true],
      ["browser.urlbar.suggest.quickactions", false],
      ["browser.newtabpage.activity-stream.default.sites", EN_US_TOPSITES],
    ],
  });

  await updateTopSites(
    sites => sites && sites.length == EN_US_TOPSITES.split(",").length
  );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://example.com/"
  );

  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function topSitesPrivateWindow() {
  // Top Sites should also be shown in private windows.
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await addTestVisits();
  let sites;
  if (Services.prefs.getBoolPref("browser.topsites.component.enabled")) {
    sites = await TopSites.getSites();
  } else {
    sites = AboutNewTab.getTopSites();
  }
  Assert.equal(
    sites.length,
    7,
    "The test suite browser should have 7 Top Sites."
  );
  let urlbar = privateWin.gURLBar;
  await UrlbarTestUtils.promisePopupOpen(privateWin, () => {
    if (urlbar.getAttribute("pageproxystate") == "invalid") {
      urlbar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(urlbar.inputField, {}, privateWin);
  });
  Assert.ok(urlbar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(privateWin);

  Assert.equal(
    UrlbarTestUtils.getResultCount(privateWin),
    7,
    "The number of results should be the same as the number of Top Sites (7)."
  );

  // Top sites should also be shown in a private window if the search string
  // gets cleared.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: privateWin,
    value: "example",
  });
  urlbar.select();
  EventUtils.synthesizeKey("KEY_Backspace", {}, privateWin);
  Assert.ok(urlbar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(privateWin);
  Assert.equal(
    UrlbarTestUtils.getResultCount(privateWin),
    7,
    "The number of results should be the same as the number of Top Sites (7)."
  );

  await BrowserTestUtils.closeWindow(privateWin);

  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });
});

add_task(async function topSitesTabSwitch() {
  // Add some visits
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits(["http://example.com/"]);
  }

  // Switch to the originating tab, to check for switch to the current tab.
  gBrowser.selectedTab = gBrowser.tabs[0];

  // Wait for the expected number of Top sites.
  await updateTopSites(sites => sites?.length == 7);
  let sites;
  if (Services.prefs.getBoolPref("browser.topsites.component.enabled")) {
    sites = await TopSites.getSites();
  } else {
    sites = AboutNewTab.getTopSites();
  }
  Assert.equal(
    sites.length,
    7,
    "The test suite browser should have 7 Top Sites."
  );

  async function checkResults(win, expectedResultType) {
    let resultCount = UrlbarTestUtils.getResultCount(win);
    let result;
    for (let i = 0; i < resultCount; ++i) {
      result = await UrlbarTestUtils.getDetailsOfResultAt(win, i);
      if (result.url == "http://example.com/") {
        break;
      }
    }
    Assert.equal(
      result.type,
      expectedResultType,
      `Should provide a result of type ${expectedResultType}.`
    );
  }

  info("Test in a non-private window");
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);
  await checkResults(window, UrlbarShared.RESULT_TYPE.TAB_SWITCH);
  await UrlbarTestUtils.promisePopupClose(window);

  info("Test in a private window, switch to tab should not be offered");
  // Top Sites should also be shown in private windows.
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  let urlbar = privateWin.gURLBar;
  await UrlbarTestUtils.promisePopupOpen(privateWin, () => {
    if (urlbar.getAttribute("pageproxystate") == "invalid") {
      urlbar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(urlbar.inputField, {}, privateWin);
  });

  Assert.ok(urlbar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(privateWin);
  await checkResults(privateWin, UrlbarShared.RESULT_TYPE.URL);
  await UrlbarTestUtils.promisePopupClose(privateWin);
  await BrowserTestUtils.closeWindow(privateWin);

  await PlacesUtils.history.clear();
});
