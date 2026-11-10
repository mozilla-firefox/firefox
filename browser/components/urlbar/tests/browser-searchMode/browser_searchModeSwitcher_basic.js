/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  PromiseTestUtils: "resource://testing-common/PromiseTestUtils.sys.mjs",
});

const CONFIG = [
  { identifier: "engine1" },
  {
    identifier: "engine2",
    base: {
      name: "Engine2",
      urls: {
        search: {
          base: "https://test2.example.com/",
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "enginePost",
    base: {
      urls: {
        search: {
          base: "https://example.com/post",
          method: "POST",
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "lastEngine",
    base: {
      urls: {
        search: {
          base: "https://example.com/last",
          searchTermParamName: "q",
        },
      },
    },
  },
];

function focusEngineItem(win, popup, engineName) {
  let itemCount = popup.querySelectorAll("panel-item").length;
  for (
    let i = 0;
    i < itemCount &&
    win.document.activeElement?.dataset?.engineName != engineName;
    i++
  ) {
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  }
  Assert.equal(
    win.document.activeElement?.dataset?.engineName,
    engineName,
    `${engineName} is focused`
  );
}

add_setup(async function setup() {
  requestLongerTimeout(5);

  await SearchTestUtils.updateRemoteSettingsConfig(CONFIG);

  UrlbarTestUtils.registerZeroPrefixProvider();
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(
      "browser.urlbar.perplexity.hasBeenInSearchMode"
    );
  });
});

add_task(async function open_settings() {
  await UrlbarTestUtils.openSearchModeSwitcher(window);

  let settingsLoaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    "about:preferences#search"
  );
  EventUtils.synthesizeKey("KEY_ArrowUp");
  EventUtils.synthesizeKey("KEY_Enter");
  await settingsLoaded;

  Assert.ok(true, "Opened settings page");

  // Clean up.
  let onLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  gBrowser.selectedBrowser.loadURI(Services.io.newURI("about:newtab"), {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });
  await onLoaded;
});

add_task(async function open_settings_with_there_is_already_opened_settings() {
  info("Open settings page in a tab");
  let startTab = gBrowser.selectedTab;
  let preferencesTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:preferences#search"
  );
  gBrowser.selectedTab = startTab;

  info("Open new window");
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  await UrlbarTestUtils.openSearchModeSwitcher(newWin);

  info(
    "Choose open settings item and wait until the window having perference page will get focus"
  );
  let onFocus = BrowserTestUtils.waitForEvent(window, "focus", true);
  EventUtils.synthesizeKey("KEY_ArrowUp", {}, newWin);
  EventUtils.synthesizeKey("KEY_Enter", {}, newWin);
  await onFocus;
  Assert.ok(true, "The window that has perference page got focus");

  await TestUtils.waitForCondition(
    () => window.gBrowser.selectedTab == preferencesTab
  );
  Assert.ok(true, "Focus opened settings page");

  BrowserTestUtils.removeTab(preferencesTab);
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function disabled_unified_button() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });

  await TestUtils.waitForCondition(() => {
    return !BrowserTestUtils.isVisible(
      gURLBar.querySelector(".searchmode-switcher")
    );
  });

  Assert.equal(
    BrowserTestUtils.isVisible(gURLBar.querySelector(".searchmode-switcher")),
    false,
    "Unified Search Button should not be visible."
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  Assert.equal(
    BrowserTestUtils.isVisible(gURLBar.querySelector(".searchmode-switcher")),
    false,
    "Unified Search Button should not be visible."
  );

  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
  });

  Assert.equal(
    BrowserTestUtils.isVisible(
      gURLBar.querySelector(".searchmode-switcher-title")
    ),
    false,
    "Title label associated with Unified Search Button should not be visible."
  );
  Assert.equal(
    BrowserTestUtils.isVisible(
      gURLBar.querySelector(".searchmode-switcher-close")
    ),
    false,
    "Close button associated with Unified Search Button should not be visible."
  );

  await UrlbarTestUtils.exitSearchMode(window);
  await SpecialPowers.popPrefEnv();
});

add_task(async function basic() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.unifiedSearchButton.historyInSearchMode", false]],
  });

  info("Open the urlbar and searchmode switcher popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  Assert.ok(
    !BrowserTestUtils.isVisible(gURLBar.view.panel),
    "The UrlbarView is not visible"
  );

  info("Press on the engine1 menu button and enter search mode");
  let viewOpened = false;
  let observer = new MutationObserver(() => {
    viewOpened ||= gURLBar.view.isOpen;
  });
  observer.observe(gURLBar, { attributeFilter: ["open"] });
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  popup.querySelector("panel-item[data-engine-id=engine1]").click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "engine1",
    entry: "searchbutton",
    source: 3,
  });
  await gURLBar.lastQueryContextPromise;
  observer.disconnect();
  Assert.ok(
    !viewOpened,
    "The view never opened, since the engine had no results to show"
  );

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);

  await SpecialPowers.popPrefEnv();
});

add_task(async function privileged_chicklet() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    window.gBrowser,
    "about:config"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      tab.documentGlobal.document.querySelector("#identity-box")
    ),
    "Chicklet is visible on privileged pages."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function select_with_single_click() {
  info("Open the urlbar and searchmode switcher popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  let button = gURLBar.querySelector(".searchmode-switcher");
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window, () => {
    EventUtils.synthesizeMouseAtCenter(button, { type: "mousedown" });
  });

  let target = popup.querySelector("panel-item[data-engine-id=engine1]");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  EventUtils.synthesizeMouseAtCenter(target, { type: "mousemove" });
  EventUtils.synthesizeMouseAtCenter(target, { type: "mouseup" });
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "engine1",
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

add_task(async function new_window() {
  let oldEngine = SearchService.getEngineById("engine2");
  oldEngine.hidden = true;

  let newWin = await BrowserTestUtils.openNewBrowserWindow();

  info("Open the urlbar and searchmode switcher popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: newWin,
    value: "",
  });
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(newWin);

  info("Open popup and check list of engines is redrawn");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(newWin);
  Assert.ok(
    !popup.querySelector(`panel-item[data-engine-id=engine2]`),
    "List has been redrawn"
  );
  popup.querySelector("panel-item[data-engine-id=engine1]").click();
  await popupHidden;
  newWin.gURLBar.querySelector(".searchmode-switcher-close").click();

  await SearchService.restoreDefaultEngines();
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function detect_searchmode_changes() {
  info("Open the urlbar and searchmode switcher popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-id=engine1]"
  );
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "engine1",
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);

  await TestUtils.waitForCondition(() => {
    return (
      gURLBar.querySelector(".searchmode-switcher-title").textContent == ""
    );
  }, "The searchMode name has been removed when we exit search mode");
});

add_task(async function open_engine_page_directly() {
  const TEST_DATA = [
    {
      action: "click",
      input: "",
      expected: "https://test2.example.com/",
    },
    {
      action: "click",
      input: "a b c",
      expected: "https://test2.example.com/?q=a+b+c",
    },
    {
      action: "key",
      input: "",
      expected: "https://test2.example.com/",
    },
    {
      action: "key",
      input: "a b c",
      expected: "https://test2.example.com/?q=a+b+c",
    },
  ];

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.showSearchTerms.enabled", false]],
  });

  for (let { action, input, expected } of TEST_DATA) {
    info(`Test for ${JSON.stringify({ action, input, expected })}`);

    info("Open a window");
    let newWin = await BrowserTestUtils.openNewBrowserWindow();

    info(`Open the result popup with [${input}]`);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window: newWin,
      value: input,
    });

    info("Open the mode switcher");
    let popup = await UrlbarTestUtils.openSearchModeSwitcher(newWin);

    info(`Do action of [${action}] on Engine2 menuitem`);
    let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(newWin);
    let pageLoaded = BrowserTestUtils.browserLoaded(
      newWin.gBrowser.selectedBrowser,
      false,
      expected
    );

    if (action == "click") {
      EventUtils.synthesizeMouseAtCenter(
        popup.querySelector(`panel-item[data-engine-id=engine2]`),
        {
          shiftKey: true,
        },
        newWin
      );
    } else {
      focusEngineItem(newWin, popup, "Engine2");
      EventUtils.synthesizeKey("KEY_Enter", { shiftKey: true }, newWin);
    }

    await popupHidden;
    await pageLoaded;
    await UrlbarTestUtils.assertSearchMode(newWin, null);
    Assert.ok(true, "The popup was hidden and expected page was loaded");

    await BrowserTestUtils.closeWindow(newWin);
  }

  // Cleanup.
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_searchWithPostEngine() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "a b c",
  });

  let spy = sinon.spy(URILoadingHelper, "openTrustedLinkIn");

  let browserLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-id=enginePost]"
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoaded;

  Assert.equal(
    spy.firstCall.args[1],
    "https://example.com/post",
    "Correct URL"
  );
  let postData = spy.firstCall.args[3].postData;
  Assert.equal(
    NetUtil.readInputStreamToString(postData, postData.available()),
    "q=a+b+c",
    "postData contains the search terms"
  );

  // Cleanup.
  spy.restore();
  await PlacesUtils.history.clear();
});

add_task(async function open_engine_page_in_tab() {
  const ACTIONS = ["click", "middleclick", "key"];
  const TEST_DATA = [
    {
      input: "",
      expected: "https://test2.example.com/",
    },
    {
      input: "a b c",
      expected: "https://test2.example.com/?q=a+b+c",
    },
  ];

  for (let action of ACTIONS) {
    for (let { input, expected } of TEST_DATA) {
      info(`Test for ${JSON.stringify({ action, input, expected })}`);

      info("Open a window");
      let newWin = await BrowserTestUtils.openNewBrowserWindow();

      info(`Open the result popup with [${input}]`);
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window: newWin,
        value: input,
      });

      info("Open the mode switcher");
      let popup = await UrlbarTestUtils.openSearchModeSwitcher(newWin);

      info(`Do action "${action}" on Engine2 panel-item`);
      let newTabOpened = BrowserTestUtils.waitForNewTab(
        newWin.gBrowser,
        expected,
        true
      );

      if (action == "click") {
        EventUtils.synthesizeMouseAtCenter(
          popup.querySelector(`panel-item[data-engine-id=engine2]`),
          {
            accelKey: true,
          },
          newWin
        );
      } else if (action == "middleclick") {
        EventUtils.synthesizeMouseAtCenter(
          popup.querySelector(`panel-item[data-engine-id=engine2]`),
          {
            button: 1,
          },
          newWin
        );
      } else /* action == "key" */ {
        focusEngineItem(newWin, popup, "Engine2");
        EventUtils.synthesizeKey("KEY_Enter", { accelKey: true }, newWin);
      }

      await UrlbarTestUtils.assertSearchMode(newWin, null);
      let tab = await newTabOpened;
      Assert.ok(true, "Expected page was loaded in a new tab");
      Assert.ok(!tab.selected, "New tab opened in the background");

      await BrowserTestUtils.closeWindow(newWin);
    }
  }

  // Cleanup.
  await PlacesUtils.history.clear();
});

add_task(async function test_enter_searchmode_by_key_if_single_result() {
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: "https://example.com/",
    title: "BOOKMARK",
  });

  const TEST_DATA = [
    {
      key: "KEY_Enter",
      expectedEntry: "keywordoffer",
    },
    {
      key: "KEY_Tab",
      expectedEntry: "keywordoffer",
    },
    {
      key: "VK_RIGHT",
      expectedEntry: "typed",
    },
    {
      key: "VK_DOWN",
      expectedEntry: "keywordoffer",
    },
  ];
  for (let { key, expectedEntry } of TEST_DATA) {
    info(`Test for entering search mode by ${key}`);

    info("Open urlbar with a query that shows bookmarks");
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "@book",
    });

    // Sanity check.
    const autofill = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.equal(
      autofill.result.providerName,
      "UrlbarProviderRestrictKeywordsAutofill"
    );
    Assert.equal(autofill.result.payload.autofillKeyword, "@bookmarks");

    info("Choose the search mode suggestion");
    EventUtils.synthesizeKey(key, {});
    await UrlbarTestUtils.promiseSearchComplete(window);
    await UrlbarTestUtils.assertSearchMode(window, {
      source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
      entry: expectedEntry,
      restrictType: "keyword",
    });

    info("Check the suggestions");
    Assert.equal(UrlbarTestUtils.getResultCount(window), 1);
    const bookmark = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.equal(bookmark.result.source, UrlbarShared.RESULT_SOURCE.BOOKMARKS);
    Assert.equal(bookmark.result.type, UrlbarShared.RESULT_TYPE.URL);
    Assert.equal(bookmark.result.payload.url, "https://example.com/");
    Assert.equal(bookmark.result.payload.title, "BOOKMARK");

    info("Choose any search engine from the switcher");
    await UrlbarTestUtils.activateSearchModeSwitcherItem(
      window,
      "panel-item[data-engine-id=engine1]"
    );
    Assert.equal(gURLBar.value, "", "The value of urlbar should be empty");

    // Clean up.
    gURLBar.querySelector(".searchmode-switcher-close").click();
    await UrlbarTestUtils.assertSearchMode(window, null);
  }

  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(
  async function test_enter_searchmode_as_preview_by_key_if_multiple_results() {
    await PlacesTestUtils.addBookmarkWithDetails({
      uri: "https://example.com/",
      title: "BOOKMARK",
    });

    for (let key of ["KEY_Tab", "VK_DOWN"]) {
      info(`Test for entering search mode by ${key}`);

      info("Open urlbar with a query that shows bookmarks");
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "@",
      });

      info("Choose the bookmark search mode");
      let resultCount = UrlbarTestUtils.getResultCount(window);
      for (let i = 0; i < resultCount; i++) {
        EventUtils.synthesizeKey(key, {});

        let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
        if (
          result.providerName == "UrlbarProviderRestrictKeywords" &&
          result.payload.keyword == "*"
        ) {
          await UrlbarTestUtils.assertSearchMode(window, {
            source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
            entry: "keywordoffer",
            restrictType: "keyword",
            isPreview: true,
          });
          break;
        }
      }

      // Clean up.
      gURLBar.querySelector(".searchmode-switcher-close").click();
      await UrlbarTestUtils.assertSearchMode(window, null);
    }

    await PlacesUtils.bookmarks.eraseEverything();
  }
);

add_task(async function test_open_state() {
  let popup = UrlbarTestUtils.searchModeSwitcherPopup(window);
  let switcher = gURLBar.querySelector(".searchmode-switcher");

  for (let className of [
    "searchmode-switcher",
    "searchmode-switcher-dropmarker",
  ]) {
    info(`Open search mode switcher popup by clicking on [${className}]`);
    let button = gURLBar.querySelector("." + className);
    await UrlbarTestUtils.openSearchModeSwitcher(window, () => {
      EventUtils.synthesizeMouseAtCenter(button, {}, window);
    });
    Assert.ok(
      switcher.hasAttribute("open"),
      "The 'open' attribute should be set"
    );

    info("Close the popup");
    popup.hide();
    await TestUtils.waitForCondition(() => !switcher.hasAttribute("open"));
    Assert.ok(true, "The 'open' attribute should not be set");
  }
});

add_task(async function nimbusScotchBonnetEnableOverride() {
  info("Setup initial local pref");
  let defaultBranch = Services.prefs.getDefaultBranch("browser.urlbar.");
  let initialValue = defaultBranch.getBoolPref("scotchBonnet.enableOverride");
  defaultBranch.setBoolPref("scotchBonnet.enableOverride", false);
  UrlbarPrefs.clear("scotchBonnet.enableOverride");

  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.isHidden(
      gURLBar.querySelector(".searchmode-switcher")
    );
  });
  Assert.ok(true, "Search mode switcher should be hidden");

  info("Setup Numbus value");
  const cleanUpNimbusEnable = await UrlbarTestUtils.initNimbusFeature(
    { scotchBonnetEnableOverride: true },
    { featureId: "search" }
  );
  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.isVisible(
      gURLBar.querySelector(".searchmode-switcher")
    );
  });
  Assert.ok(true, "Search mode switcher should be visible");

  await cleanUpNimbusEnable();
  defaultBranch.setBoolPref("scotchBonnet.enableOverride", initialValue);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", true]],
  });
});

add_task(async function test_button_stuck() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  info("Show the SearchModeSwitcher");
  await UrlbarTestUtils.openSearchModeSwitcher(win);

  info("Hide the SearchModeSwitcher");
  let button = win.gURLBar.querySelector(".searchmode-switcher");
  let promiseMenuClosed = UrlbarTestUtils.searchModeSwitcherPopupClosed(win);

  // Need native mouse event as the popup will be closed on blur.
  EventUtils.synthesizeNativeMouseEvent({
    type: "click",
    target: button,
    atCenter: true,
    win,
  });
  await promiseMenuClosed;
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_readonly() {
  let popupOpened = BrowserTestUtils.waitForNewWindow({ url: "about:blank" });
  BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<html><script>popup=open('about:blank','','width=300,height=200')</script>"
  );
  let win = await popupOpened;

  Assert.ok(win.gURLBar, "location bar exists in the popup");
  Assert.ok(win.gURLBar.readOnly, "location bar is read-only in the popup");

  Assert.equal(
    BrowserTestUtils.isVisible(
      win.gURLBar.querySelector(".searchmode-switcher")
    ),
    false,
    "Unified Search Button should not be visible in readonly windows"
  );

  let closedPopupPromise = BrowserTestUtils.windowClosed(win);
  win.close();
  await closedPopupPromise;
  gBrowser.removeCurrentTab();
});

add_task(async function test_search_service_fail() {
  // Exiting search mode requeries, and if the view opens and hides while the
  // stub is active, the one-offs may reject trying to get the default engine.
  PromiseTestUtils.allowMatchingRejectionsGlobally(/Initialization failed/);
  let stub = sinon
    .stub(SearchService, "promiseInitialized")
    .get(() => Promise.reject(new Error("Initialization failed")));
  SearchService.forceInitializationStatusForTests("failed");

  let newWin = await BrowserTestUtils.openNewBrowserWindow();

  // Force updateSearchIcon to be triggered
  await SpecialPowers.pushPrefEnv({
    set: [["keyword.enabled", false]],
  });

  let searchModeSwitcherIconUrl = await TestUtils.waitForCondition(
    () => UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    "Waiting for the search mode switcher icon to update after exiting search mode."
  );

  Assert.equal(
    searchModeSwitcherIconUrl,
    UrlbarShared.ICON.GLOBE,
    "The search mode switcher should have the globe icon url since the search service init failed."
  );

  info("Open search mode switcher");
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(newWin);

  info("Ensure local search modes are present in popup");
  let localSearchModes = ["bookmarks", "history", "tabs"];
  for (let searchMode of localSearchModes) {
    popup.querySelector(`.search-button-${searchMode}`);
    Assert.ok("Local search modes should be present");
  }

  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(newWin);
  popup.querySelector(`.search-button-${localSearchModes[0]}`).click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(newWin, {
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    entry: "searchbutton",
  });
  await UrlbarTestUtils.assertSearchModeSwitcherIcon(
    newWin,
    UrlbarShared.LOCAL_SEARCH_MODES.find(
      m => m.source == UrlbarShared.RESULT_SOURCE.BOOKMARKS
    ).icon,
    "The search mode switcher should have the bookmarks icon."
  );

  info("Press the close button and exit search mode");
  newWin.gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(newWin, null);

  await UrlbarTestUtils.assertSearchModeSwitcherIcon(
    newWin,
    UrlbarShared.ICON.GLOBE,
    "The search mode switcher should have the globe icon again."
  );

  await SpecialPowers.popPrefEnv();
  await TestUtils.waitForCondition(
    () =>
      UrlbarTestUtils.searchModeSwitcherIconIs(
        newWin,
        UrlbarShared.ICON.SEARCH_GLASS
      ),
    "Should have the search glass icon again."
  );

  stub.restore();
  SearchService.forceInitializationStatusForTests("success");

  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_local_search_mode_shortcuts() {
  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);

  let tabsItem = popup.querySelector("panel-item.search-button-tabs");
  Assert.equal(
    tabsItem.getAttribute("shortcut"),
    ShortcutUtils.prettifyShortcut(document.getElementById("key_searchTabs")),
    "The Tabs item shows the Search Tabs shortcut"
  );

  for (let item of popup.querySelectorAll(
    "panel-item.searchmode-switcher-local:not(.search-button-tabs)"
  )) {
    Assert.ok(
      !item.hasAttribute("shortcut"),
      `${item.className} has no shortcut`
    );
  }

  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  popup.hide();
  await popupHidden;
});
