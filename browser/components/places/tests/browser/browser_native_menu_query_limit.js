/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

const MAX_RESULTS = 100;
const RESULT_COUNT = MAX_RESULTS * 2 + 2;
let folder;
let queryFolder;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.places.nativeMenu.maxFolderResults", 0]],
  });
  registerCleanupFunction(async () => {
    SidebarController.hide();
    await PlacesUtils.bookmarks.eraseEverything();
    await PlacesUtils.history.clear();
  });
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  let urls = Array.from(
    { length: RESULT_COUNT },
    (_, i) => `https://example.com/native-menu-limit/${i}`
  );
  folder = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [
      {
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "Query limit test",
        children: urls.map((url, index) => ({
          url,
          title:
            index < MAX_RESULTS
              ? "native-menu-limit exact-limit"
              : "native-menu-limit",
          tags: ["native-menu-limit"],
        })),
      },
    ],
  });
  queryFolder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Query menu tests",
  });
  await PlacesTestUtils.addVisits(urls.map(uri => ({ uri })));
});

function createMenu(place, isNative = true) {
  let menu = document.createXULElement("menu");
  let popup = document.createXULElement("menupopup");
  menu.appendChild(popup);
  let parent = isNative
    ? document.getElementById("bookmarksMenuPopup")
    : document.getElementById("mainPopupSet");
  parent.appendChild(menu);
  let view = new PlacesMenu({ target: popup, originalTarget: popup }, place);
  return { menu, popup, view };
}

async function checkMenu(place, expectedCount, sidebarId, isNative = true) {
  let query;
  let {
    menu,
    popup,
    view: rootView,
  } = createMenu(
    isNative ? `place:parent=${queryFolder.guid}` : place,
    isNative
  );
  try {
    if (isNative) {
      query = await PlacesUtils.bookmarks.insert({
        parentGuid: queryFolder.guid,
        url: place,
        title: "Saved query",
      });
      popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
      let queryMenu = Array.from(popup.children).find(
        child => child._placesNode?.bookmarkGuid == query.guid
      );
      popup = queryMenu.menupopup;
      popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
      Assert.equal(
        PlacesUIUtils.getViewForNode(popup),
        rootView,
        "Query submenus use the parent view"
      );
    }
    Assert.equal(popup._placesNode.childCount, expectedCount);
    Assert.equal(
      Array.from(popup.children).filter(child => child._placesNode).length,
      expectedCount,
      "The menu contains the expected number of results"
    );
    Assert.equal(!!popup._moreMenuitem, !!sidebarId);
    if (sidebarId) {
      let shown = BrowserTestUtils.waitForEvent(window, "SidebarShown");
      popup._moreMenuitem.doCommand();
      await shown;
      Assert.equal(SidebarController.currentID, sidebarId);
      SidebarController.hide();
    }
  } finally {
    rootView.uninit();
    menu.remove();
    if (query) {
      await PlacesUtils.bookmarks.remove(query.guid);
    }
  }
}

add_task(async function test_query_limits() {
  await checkMenu("place:sort=4", MAX_RESULTS + 1, "viewHistorySidebar");
  await checkMenu(
    "place:queryType=1&terms=native-menu-limit",
    MAX_RESULTS + 1,
    "viewBookmarksSidebar"
  );
  await checkMenu(
    "place:tag=native-menu-limit",
    MAX_RESULTS + 1,
    "viewBookmarksSidebar"
  );
  await checkMenu("place:sort=4&maxResults=10", 10);
  await checkMenu("place:sort=4&maxResults=50", 50);
  await checkMenu("place:sort=4&maxResults=100", MAX_RESULTS);
  await checkMenu("place:queryType=1&terms=exact-limit", MAX_RESULTS);
  await checkMenu(
    "place:sort=4&maxResults=200",
    MAX_RESULTS + 1,
    "viewHistorySidebar"
  );
  await checkMenu("place:sort=4", RESULT_COUNT, null, false);
  await checkMenu(`place:parent=${folder[0].guid}`, RESULT_COUNT);
});

add_task(async function test_configurable_query_limit() {
  Assert.equal(
    Services.prefs.getIntPref("browser.places.nativeMenu.maxResults"),
    MAX_RESULTS,
    "The default native menu query limit is 100"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["browser.places.nativeMenu.maxResults", 200]],
  });
  try {
    await checkMenu("place:sort=4", 201, "viewHistorySidebar");
    await checkMenu("place:sort=4&maxResults=10", 10);
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_disabled_query_limit() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.places.nativeMenu.maxResults", 0]],
  });
  try {
    await checkMenu("place:sort=4", RESULT_COUNT);
    await checkMenu("place:sort=4&maxResults=10", 10);
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_existing_query_pref_changes() {
  let { menu, popup, view } = createMenu("place:type=3&sort=4");
  try {
    popup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
    for (let limit of [100, 200, 0]) {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.places.nativeMenu.maxResults", limit]],
      });
      try {
        popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
        let dayPopup = Array.from(popup.children).find(
          child => child._placesNode
        ).menupopup;
        dayPopup.dispatchEvent(
          new MouseEvent("popupshowing", { bubbles: true })
        );
        Assert.equal(
          Array.from(dayPopup.children).filter(child => child._placesNode)
            .length,
          limit ? limit + 1 : RESULT_COUNT,
          "Reopening the menu uses the current pref value"
        );
        Assert.equal(
          !!dayPopup.querySelector(
            '[data-l10n-id="menu-places-show-more-in-sidebar"]'
          ),
          limit > 0
        );
        dayPopup.dispatchEvent(
          new MouseEvent("popuphidden", { bubbles: true })
        );
      } finally {
        popup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
        await SpecialPowers.popPrefEnv();
      }
    }
  } finally {
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_cached_query_pref_changes() {
  let { menu, popup, view } = createMenu("place:type=3&sort=4");
  try {
    let dayPopup = Array.from(popup.children).find(
      child => child._placesNode
    ).menupopup;
    dayPopup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    dayPopup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
    for (let limit of [200, 0, 50]) {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.places.nativeMenu.maxResults", limit]],
      });
      try {
        dayPopup.dispatchEvent(
          new MouseEvent("popupshowing", { bubbles: true })
        );
        Assert.equal(
          Array.from(dayPopup.children).filter(child => child._placesNode)
            .length,
          MAX_RESULTS + 1,
          "Pref changes preserve the cached results"
        );
        Assert.ok(
          dayPopup.querySelector(
            '[data-l10n-id="menu-places-show-more-in-sidebar"]'
          ),
          "The sidebar command remains available for cached limited results"
        );
      } finally {
        dayPopup.dispatchEvent(
          new MouseEvent("popuphidden", { bubbles: true })
        );
        await SpecialPowers.popPrefEnv();
      }
    }
  } finally {
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_nested_history_query() {
  let { menu, popup, view } = createMenu("place:type=3&sort=4");
  try {
    let dayMenu = Array.from(popup.children).find(child => child._placesNode);
    let dayPopup = dayMenu.menupopup;
    dayPopup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    let originalResult = view.result;
    Assert.equal(PlacesUIUtils.getViewForNode(dayPopup), view);
    Assert.equal(dayPopup._placesNode.childCount, MAX_RESULTS + 1);
    Assert.ok(dayPopup._moreMenuitem, "The date query offers more results");
    dayPopup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
    dayPopup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    Assert.equal(dayPopup._placesNode.childCount, MAX_RESULTS + 1);
    Assert.equal(
      dayPopup.querySelectorAll(
        '[data-l10n-id="menu-places-show-more-in-sidebar"]'
      ).length,
      1
    );
    popup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
    popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    Assert.equal(
      view.result,
      originalResult,
      "Rebuilding the parent keeps the same view and result"
    );
  } finally {
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_saved_queries_and_shortcuts() {
  let parent = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Saved queries",
  });
  let query = await PlacesUtils.bookmarks.insert({
    parentGuid: parent.guid,
    url: "place:sort=4",
    title: "History query",
  });
  await PlacesUtils.bookmarks.insert({
    parentGuid: parent.guid,
    url: `place:parent=${folder[0].guid}`,
    title: "Folder shortcut",
  });
  let { menu, popup, view } = createMenu(`place:parent=${parent.guid}`);
  try {
    let queryMenu = Array.from(popup.children).find(
      child => child._placesNode?.bookmarkGuid == query.guid
    );
    queryMenu.menupopup.dispatchEvent(
      new MouseEvent("popupshowing", { bubbles: true })
    );
    let originalResult = view.result;
    Assert.equal(PlacesUIUtils.getViewForNode(queryMenu.menupopup), view);
    Assert.equal(queryMenu.menupopup._placesNode.childCount, MAX_RESULTS + 1);
    Assert.ok(queryMenu.menupopup._moreMenuitem);
    Assert.equal(
      PlacesUtils.asQuery(view.result.root).queryOptions.maxResults,
      0,
      "The parent query options are unchanged"
    );
    Assert.equal(
      (await PlacesUtils.bookmarks.fetch(query.guid)).url.href,
      "place:sort=4",
      "Limiting the results does not change the saved query"
    );
    Assert.equal(
      queryMenu.menupopup._endMarker.nextElementSibling,
      queryMenu.menupopup._moreMenuitem,
      "Show More in Sidebar appears directly after the results"
    );
    Assert.ok(
      queryMenu.menupopup.querySelector(".openintabs-menuitem"),
      "Saved queries retain the Open All in Tabs command"
    );
    let shortcutMenu = Array.from(popup.children).find(
      child =>
        child._placesNode &&
        PlacesUtils.nodeIsFolderOrShortcut(child._placesNode)
    );
    let shortcutPopup = shortcutMenu.menupopup;
    shortcutPopup.dispatchEvent(
      new MouseEvent("popupshowing", { bubbles: true })
    );
    Assert.equal(shortcutPopup._placesNode.childCount, RESULT_COUNT);
    Assert.ok(!shortcutPopup._moreMenuitem, "Folder shortcuts are not limited");
    let firstResult = Array.from(queryMenu.menupopup.children).find(
      child => child._placesNode
    );
    await PlacesUtils.history.remove(firstResult._placesNode.uri);
    Assert.equal(
      Array.from(queryMenu.menupopup.children).filter(
        child => child._placesNode
      ).length,
      MAX_RESULTS,
      "Removing one result leaves 100 displayed items"
    );
    Assert.ok(
      queryMenu.menupopup.querySelector(
        '[data-l10n-id="menu-places-show-more-in-sidebar"]'
      ),
      "The sidebar command remains available after removing a result"
    );
    await PlacesUtils.bookmarks.remove(query.guid);
    Assert.equal(
      view.result,
      originalResult,
      "Removing a saved query keeps the parent view and result"
    );
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(parent.guid);
  }
});

add_task(async function test_rebuilt_query_popup() {
  let query = await PlacesUtils.bookmarks.insert({
    parentGuid: queryFolder.guid,
    url: "place:sort=4&maxResults=200",
    title: "Limited history query",
  });
  let { menu, popup, view } = createMenu(`place:parent=${queryFolder.guid}`);
  let getQueryPopup = () =>
    Array.from(popup.children).find(
      child => child._placesNode?.bookmarkGuid == query.guid
    ).menupopup;
  try {
    let queryPopup = getQueryPopup();
    queryPopup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    let queryNode = queryPopup._placesNode;
    Assert.equal(
      PlacesUtils.asQuery(queryNode).queryOptions.maxResults,
      MAX_RESULTS + 1
    );
    queryPopup.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));

    view.result.sortingMode =
      Ci.nsINavHistoryQueryOptions.SORT_BY_TITLE_ASCENDING;
    popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    let rebuiltPopup = getQueryPopup();
    Assert.notEqual(rebuiltPopup, queryPopup, "Sorting rebuilds the popup");
    Assert.equal(
      rebuiltPopup._placesNode,
      queryNode,
      "The query node survives"
    );
    await SpecialPowers.pushPrefEnv({
      set: [["browser.places.nativeMenu.maxResults", 0]],
    });
    try {
      rebuiltPopup.dispatchEvent(
        new MouseEvent("popupshowing", { bubbles: true })
      );
      Assert.equal(
        Array.from(rebuiltPopup.children).filter(child => child._placesNode)
          .length,
        MAX_RESULTS + 1,
        "The rebuilt popup retains the cached results"
      );
      Assert.ok(
        rebuiltPopup.querySelector(
          '[data-l10n-id="menu-places-show-more-in-sidebar"]'
        ),
        "The cached query keeps its sidebar command when the pref is disabled"
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.remove(query.guid);
  }
});

add_task(async function test_sidebar_from_library() {
  let { menu, view } = createMenu("place:sort=4");
  let library;
  try {
    library = await promiseLibrary("AllBookmarks");
    let shown = BrowserTestUtils.waitForEvent(window, "SidebarShown");
    await PlacesUIUtils.showSidebarForNode(view.result.root);
    await shown;
    Assert.equal(SidebarController.currentID, "viewHistorySidebar");
  } finally {
    SidebarController.hide();
    if (library) {
      await promiseLibraryClosed(library);
    }
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_sidebar_without_browser_window() {
  let { BrowserWindowTracker } = ChromeUtils.importESModule(
    "resource:///modules/BrowserWindowTracker.sys.mjs"
  );
  let { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );
  let { menu, view } = createMenu("place:sort=4");
  let getTopWindow = sinon
    .stub(BrowserWindowTracker, "getTopWindow")
    .callThrough();
  getTopWindow.onFirstCall().returns(null);
  let browserWindow;
  try {
    let opened = BrowserTestUtils.waitForNewWindow();
    let showing = PlacesUIUtils.showSidebarForNode(view.result.root);
    browserWindow = await opened;
    await showing;
    Assert.equal(
      browserWindow.SidebarController.currentID,
      "viewHistorySidebar",
      "A new browser window opens with the requested sidebar"
    );
  } finally {
    getTopWindow.restore();
    if (browserWindow) {
      await BrowserTestUtils.closeWindow(browserWindow);
    }
    view.uninit();
    menu.remove();
  }
});

add_task(async function test_sidebar_ignores_popup_window() {
  let { menu, view } = createMenu("place:sort=4");
  let popupWindow;
  try {
    popupWindow = await BrowserTestUtils.openNewBrowserWindow({
      all: false,
      features: "toolbar=no",
    });
    await SimpleTest.promiseFocus(popupWindow);
    Assert.ok(!popupWindow.toolbar.visible, "The popup has no browser toolbar");
    let shown = BrowserTestUtils.waitForEvent(window, "SidebarShown");
    await PlacesUIUtils.showSidebarForNode(view.result.root);
    await shown;
    Assert.equal(
      SidebarController.currentID,
      "viewHistorySidebar",
      "The sidebar opens in the full browser window"
    );
  } finally {
    SidebarController.hide();
    if (popupWindow) {
      await BrowserTestUtils.closeWindow(popupWindow);
    }
    view.uninit();
    menu.remove();
  }
});
