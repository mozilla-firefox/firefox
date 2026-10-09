/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  TopSites: "resource:///modules/topsites/TopSites.sys.mjs",
});

// Uses Baidu for a top site, so that it works as a search shortcut.
const DEFAULT_TOPSITES =
  "https://www.youtube.com/,https://www.facebook.com/,https://www.baidu.com/,https://www.reddit.com/,about:robots,https://twitter.com/";

// The Baidu search shortcut keyword, see SEARCH_SHORTCUTS in
// SearchShortcuts.sys.mjs.
const BAIDU_KEYWORD = "@百度";

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

async function getTopSites() {
  if (Services.prefs.getBoolPref("browser.topsites.component.enabled")) {
    return await TopSites.getSites();
  }
  return AboutNewTab.getTopSites();
}

async function checkDoesNotOpenOnFocus(win = window) {
  // The view should not open when the input is focused programmatically.
  win.gURLBar.blur();
  win.gURLBar.focus();
  Assert.ok(!win.gURLBar.view.isOpen, "check urlbar panel is not open");
  win.gURLBar.blur();

  // Check the keyboard shortcut.
  win.document.getElementById("Browser:OpenLocation").doCommand();
  // Because the panel opening may not be immediate, we must wait a bit.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 500));
  Assert.ok(!win.gURLBar.view.isOpen, "check urlbar panel is not open");
  win.gURLBar.blur();

  // Focus with the mouse.
  EventUtils.synthesizeMouseAtCenter(win.gURLBar.inputField, {}, win);
  // Because the panel opening may not be immediate, we must wait a bit.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 500));
  Assert.ok(!win.gURLBar.view.isOpen, "check urlbar panel is not open");
  win.gURLBar.blur();
}

add_setup(async function () {
  // Set up the configuration so that Baidu is available as a search engine, so
  // the search shortcut works.
  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      identifier: "google",
      base: {
        name: "Google",
        aliases: ["google"],
        urls: {
          search: {
            base: "https://www.google.com/search",
            searchTermParamName: "q",
          },
        },
      },
    },
    {
      identifier: "baidu",
      base: {
        name: "百度",
        aliases: ["百度", "baidu"],
        urls: {
          search: {
            base: "https://www.baidu.com/baidu",
            searchTermParamName: "wd",
          },
        },
      },
    },
  ]);

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.topsites", true],
      ["browser.urlbar.suggest.quickactions", false],
      ["browser.newtabpage.activity-stream.default.sites", DEFAULT_TOPSITES],
      [
        "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.searchEngines",
        "baidu",
      ],
    ],
  });

  await updateTopSites(
    sites => sites && sites.length == DEFAULT_TOPSITES.split(",").length
  );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://example.com/"
  );

  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function topSitesShown() {
  let sites = await getTopSites();

  for (let prefVal of [true, false]) {
    // This test should work regardless of whether Top Sites are enabled on
    // about:newtab.
    await SpecialPowers.pushPrefEnv({
      set: [["browser.newtabpage.activity-stream.feeds.topsites", prefVal]],
    });
    // We don't expect this to change, but we run updateTopSites just in case
    // feeds.topsites were to have an effect on the composition of Top Sites.
    await updateTopSites(siteList => siteList.length == 6);

    await UrlbarTestUtils.promisePopupOpen(window, () => {
      if (gURLBar.getAttribute("pageproxystate") == "invalid") {
        gURLBar.handleRevert();
      }
      EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
    });
    Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
    await UrlbarTestUtils.promiseSearchComplete(window);
    Assert.equal(
      UrlbarTestUtils.getResultCount(window),
      sites.length,
      "The number of results should be the same as the number of Top Sites (6)."
    );

    for (let i = 0; i < sites.length; i++) {
      let site = sites[i];
      let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
      if (site.searchTopSite) {
        Assert.equal(
          result.searchParams.keyword,
          site.label,
          "The search Top Site should have an alias."
        );
        continue;
      }

      Assert.equal(
        site.url,
        result.url,
        "The Top Site URL and the result URL shoud match."
      );
      Assert.equal(
        site.label || site.title || site.hostname,
        result.title,
        "The Top Site title and the result title shoud match."
      );
    }
    await UrlbarTestUtils.promisePopupClose(window, () => {
      gURLBar.blur();
    });
    // This pops updateTopSites changes.
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function selectSearchTopSite() {
  await updateTopSites(
    sites => sites && sites[0] && sites[0].searchTopSite,
    true
  );
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  await UrlbarTestUtils.promiseSearchComplete(window);

  let baiduSearch = await UrlbarTestUtils.waitForAutocompleteResultAt(
    window,
    0
  );

  Assert.equal(
    baiduSearch.result.type,
    UrlbarUtils.RESULT_TYPE.SEARCH,
    "First result should have SEARCH type."
  );

  Assert.equal(
    baiduSearch.result.payload.keyword,
    BAIDU_KEYWORD,
    "First result should have the Baidu keyword."
  );

  let searchPromise = UrlbarTestUtils.promiseSearchComplete(window);
  EventUtils.synthesizeMouseAtCenter(baiduSearch, {});
  await searchPromise;
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: baiduSearch.result.payload.engine,
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    entry: "topsites_urlbar",
  });
  await UrlbarTestUtils.exitSearchMode(window, { backspace: true });

  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });
});

add_task(async function topSitesBookmarksAndTabs() {
  await addTestVisits();
  let sites = await getTopSites();
  Assert.equal(
    sites.length,
    7,
    "The test suite browser should have 7 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    7,
    "The number of results should be the same as the number of Top Sites (7)."
  );

  let exampleResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    exampleResult.url,
    "http://example.com/",
    "The example.com Top Site should be the first result."
  );

  Assert.equal(
    exampleResult.source,
    UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
    "The example.com Top Site should not appear in the view as an open tab result since it is the current tab."
  );

  let youtubeResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  Assert.equal(
    youtubeResult.url,
    "https://www.youtube.com/",
    "The YouTube Top Site should be the second result."
  );
  Assert.equal(
    youtubeResult.source,
    UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    "The YouTube Top Site should appear in the view as a bookmark result."
  );
  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });

  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
});

add_task(async function topSitesKeywordNavigationPageproxystate() {
  await addTestVisits();
  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "valid",
    "Sanity check initial state"
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  let count = UrlbarTestUtils.getResultCount(window);
  Assert.equal(count, 7, "The number of results should be the expected one.");

  for (let i = 0; i < count; ++i) {
    EventUtils.synthesizeKey("KEY_ArrowDown");
    Assert.equal(
      gURLBar.getAttribute("pageproxystate"),
      "invalid",
      "Moving through results"
    );
  }
  for (let i = 0; i < count; ++i) {
    EventUtils.synthesizeKey("KEY_ArrowUp");
    Assert.equal(
      gURLBar.getAttribute("pageproxystate"),
      "invalid",
      "Moving through results"
    );
  }

  // Double ESC should restore state.
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
  EventUtils.synthesizeKey("KEY_Escape");
  Assert.equal(
    gURLBar.getAttribute("pageproxystate"),
    "valid",
    "Double ESC should restore state"
  );
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
});

add_task(async function topSitesPinned() {
  await addTestVisits();
  let info = { url: "http://example.com/" };
  NewTabUtils.pinnedLinks.pin(info, 0);

  await updateTopSites(sites => sites && sites[0] && sites[0].isPinned);

  let sites = await getTopSites();
  Assert.equal(
    sites.length,
    7,
    "The test suite browser should have 7 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    7,
    "The number of results should be the same as the number of Top Sites (7)."
  );

  let exampleResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    exampleResult.url,
    "http://example.com/",
    "The example.com Top Site should be the first result."
  );

  Assert.equal(
    exampleResult.source,
    UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
    "The example.com Top Site should not appear in the view as an open tab result since it is the current tab."
  );

  Assert.ok(
    exampleResult.element.row.hasAttribute("pinned"),
    "The example.com Top Site should have the pinned property."
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });
  NewTabUtils.pinnedLinks.unpin(info);

  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
});

add_task(async function topSitesBookmarksAndTabsDisabled() {
  await addTestVisits();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.openpage", false],
      ["browser.urlbar.suggest.bookmark", false],
    ],
  });

  let sites = await getTopSites();
  Assert.equal(
    sites.length,
    7,
    "The test suite browser should have 7 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    7,
    "The number of results should be the same as the number of Top Sites (7)."
  );

  let exampleResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    exampleResult.url,
    "http://example.com/",
    "The example.com Top Site should be the second result."
  );
  Assert.equal(
    exampleResult.source,
    UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
    "The example.com Top Site should appear as a normal result even though it's open in a tab."
  );

  let youtubeResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  Assert.equal(
    youtubeResult.url,
    "https://www.youtube.com/",
    "The YouTube Top Site should be the third result."
  );
  Assert.equal(
    youtubeResult.source,
    UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
    "The YouTube Top Site should appear as a normal result even though it's bookmarked."
  );
  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });

  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

add_task(async function topSitesDisabled() {
  // Disable Top Sites feed.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.feeds.system.topsites", false]],
  });
  await checkDoesNotOpenOnFocus();
  await SpecialPowers.popPrefEnv();

  // Top Sites should also not be shown when Urlbar Top Sites are disabled.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.topsites", false]],
  });
  await checkDoesNotOpenOnFocus();
  await SpecialPowers.popPrefEnv();
});

add_task(async function topSitesNumber() {
  // Add some visits
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits([
      "http://example-a.com/",
      "http://example-b.com/",
      "http://example-c.com/",
      "http://example-d.com/",
      "http://example-e.com/",
    ]);
  }

  // Wait for the expected number of Top sites.
  await updateTopSites(sites => sites && sites.length == 8);
  Assert.equal(
    (await getTopSites()).length,
    8,
    "The test suite browser should have 8 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    8,
    "The number of results should be the default (8)."
  );
  await UrlbarTestUtils.promisePopupClose(window);

  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.topSitesRows", 2]],
  });
  // Wait for the expected number of Top sites.
  await updateTopSites(sites => sites && sites.length == 11);
  Assert.equal(
    (await getTopSites()).length,
    11,
    "The test suite browser should have 11 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    10,
    "The number of results should be maxRichResults (10)."
  );
  await UrlbarTestUtils.promisePopupClose(window);

  await SpecialPowers.popPrefEnv();
  await PlacesUtils.history.clear();
});

add_task(async function tabSwitchBehavior() {
  let exampleTab = gBrowser.selectedTab;
  let aboutRobotsTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );

  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(aboutRobotsTab);
  });

  await BrowserTestUtils.switchTab(gBrowser, exampleTab);
  await TestUtils.waitForCondition(
    async () => (await getTopSites()).length == 6,
    "Waiting for Top Sites to settle after the previous test"
  );

  let sites = AboutNewTab.getTopSites();
  Assert.equal(
    sites.length,
    6,
    "The test suite browser should have 6 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    6,
    "The number of results should be the same as the number of Top Sites (6)."
  );

  let aboutRobotsResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 4);
  Assert.equal(
    aboutRobotsResult.url,
    "about:robots",
    "The about:robots Top Site should be the 5th result."
  );
  Assert.equal(
    aboutRobotsResult.source,
    UrlbarUtils.RESULT_SOURCE.TABS,
    "The about:robots Top Site should appear as an open tab result."
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });

  await BrowserTestUtils.switchTab(gBrowser, aboutRobotsTab);

  sites = AboutNewTab.getTopSites();
  Assert.equal(
    sites.length,
    6,
    "The test suite browser should have 6 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    if (gURLBar.getAttribute("pageproxystate") == "invalid") {
      gURLBar.handleRevert();
    }
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    6,
    "The number of results should be the same as the number of Top Sites (6)."
  );

  aboutRobotsResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 4);
  Assert.equal(
    aboutRobotsResult.url,
    "about:robots",
    "The about:robots Top Site should be the 5th result."
  );
  Assert.equal(
    aboutRobotsResult.source,
    UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
    "The about:robots Top Site should appear as a regular result."
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });

  await PlacesUtils.history.clear();
});
