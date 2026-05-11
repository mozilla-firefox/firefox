/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

async function unloadSearchExtension(extension) {
  let settingsWritten = SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
  await extension.unload();
  await settingsWritten;
}

add_setup(async function setup() {
  requestLongerTimeout(5);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", true]],
  });
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

  await BrowserTestUtils.waitForCondition(
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
    source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
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

  info("Press on the bing menu button and enter search mode");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  popup.querySelector("panel-item[data-engine-id=bing]").click();
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Bing",
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
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

  let target = popup.querySelector("panel-item[data-engine-id=bing]");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  EventUtils.synthesizeMouseAtCenter(target, { type: "mousemove" });
  EventUtils.synthesizeMouseAtCenter(target, { type: "mouseup" });
  await popupHidden;

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Bing",
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);
});

function updateEngine(fun) {
  let updated = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.CHANGED,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
  fun();
  return updated;
}

add_task(async function new_window() {
  let oldEngine = SearchService.getEngineByName("Bing");
  await updateEngine(() => {
    oldEngine.hidden = true;
  });

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
    !popup.querySelector(`panel-item[data-engine-id=${oldEngine.id}]`),
    "List has been redrawn"
  );
  popup.querySelector("panel-item[data-engine-id=google]").click();
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
    "panel-item[data-engine-id=bing]"
  );
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Bing",
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);

  await BrowserTestUtils.waitForCondition(() => {
    return (
      gURLBar.querySelector(".searchmode-switcher-title").textContent == ""
    );
  }, "The searchMode name has been removed when we exit search mode");
});

async function setDefaultEngine(name) {
  let engine = (await SearchService.getEngines()).find(e => e.name == name);
  Assert.ok(engine);
  await SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);
}

add_task(async function test_icon_new_window() {
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let expectedIcon = await SearchService.defaultEngine.getIconURL();

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    expectedIcon,
    "The search mode switcher should already have the engine favicon."
  );

  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_search_icon_change() {
  await SpecialPowers.pushPrefEnv({
    set: [["keyword.enabled", false]],
  });

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  const globeIconUrl = UrlbarUtils.ICON.GLOBE;

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    globeIconUrl,
    "The search mode switcher should have the globe icon url since keyword.enabled is false"
  );

  let popup = UrlbarTestUtils.searchModeSwitcherPopup(newWin);
  let bing = SearchService.getEngineByName("Bing");
  info("Open the urlbar and searchmode switcher popup");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: newWin,
    value: "",
  });
  await UrlbarTestUtils.openSearchModeSwitcher(newWin);
  info("Press on the bing menu button and enter search mode");
  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(newWin);
  popup.querySelector(`panel-item[data-engine-id=${bing.id}]`).click();
  await popupHidden;

  const bingSearchEngineIconUrl = await bing.getIconURL();

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    bingSearchEngineIconUrl,
    "The search mode switcher should have the bing icon url since we are in \
     search mode"
  );
  await UrlbarTestUtils.assertSearchMode(newWin, {
    engineName: bing.name,
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and exit search mode");
  newWin.gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(newWin, null);

  let searchModeSwitcherIconUrl = await BrowserTestUtils.waitForCondition(
    () => UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    "Waiting for the search mode switcher icon to update after exiting search mode."
  );

  Assert.equal(
    searchModeSwitcherIconUrl,
    globeIconUrl,
    "The search mode switcher should have the globe icon url since keyword.enabled is false"
  );

  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function open_engine_page_directly() {
  const TEST_DATA = [
    {
      action: "click",
      input: "",
      expected: "https://example.com/",
    },
    {
      action: "click",
      input: "a b c",
      expected: "https://example.com/?q=a+b+c",
    },
    {
      action: "key",
      input: "",
      expected: "https://example.com/",
    },
    {
      action: "key",
      input: "a b c",
      expected: "https://example.com/?q=a+b+c",
    },
  ];

  let searchExtension = await SearchTestUtils.installSearchExtension(
    {
      name: "MozSearch",
      search_url: "https://example.com/",
      favicon_url: "https://example.com/favicon.ico",
    },
    { setAsDefault: true, skipUnload: true }
  );

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

    info(`Do action of [${action}] on MozSearch menuitem`);
    let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(newWin);
    let pageLoaded = BrowserTestUtils.browserLoaded(
      newWin.gBrowser.selectedBrowser,
      false,
      expected
    );

    if (action == "click") {
      EventUtils.synthesizeMouseAtCenter(
        popup.querySelector(`panel-item[data-engine-name=MozSearch]`),
        {
          shiftKey: true,
        },
        newWin
      );
    } else {
      let panelItems = Array.from(popup.querySelectorAll(`panel-item`));
      let index = panelItems.findIndex(
        item => item.dataset.engineName == "MozSearch"
      );
      for (let i = 0; i < index; i++) {
        EventUtils.synthesizeKey("KEY_ArrowDown", {}, newWin);
      }
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
  await unloadSearchExtension(searchExtension);
});

add_task(async function test_searchWithPostEngine() {
  let searchExtension = await SearchTestUtils.installSearchExtension(
    {
      name: "MozSearch",
      search_url: "https://example.com/",
      search_url_post_params: "q={searchTerms}",
      favicon_url: "https://example.com/favicon.ico",
    },
    { skipUnload: true }
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "a b c",
  });

  let spy = sinon.spy(window, "openTrustedLinkIn");

  let browserLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-name=MozSearch]"
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoaded;

  Assert.equal(spy.firstCall.args[0], "https://example.com/", "Correct URL");
  let postData = spy.firstCall.args[2].postData;
  Assert.equal(
    NetUtil.readInputStreamToString(postData, postData.available()),
    "q=a+b+c",
    "postData contains the search terms"
  );

  // Cleanup.
  spy.restore();
  await PlacesUtils.history.clear();
  await unloadSearchExtension(searchExtension);
});

add_task(async function open_engine_page_in_tab() {
  const ACTIONS = ["click", "middleclick", "key"];
  const TEST_DATA = [
    {
      input: "",
      expected: "https://example.com/",
    },
    {
      input: "a b c",
      expected: "https://example.com/?q=a+b+c",
    },
  ];

  let searchExtension = await SearchTestUtils.installSearchExtension(
    {
      name: "MozSearch",
      search_url: "https://example.com/",
      favicon_url: "https://example.com/favicon.ico",
    },
    { setAsDefault: true, skipUnload: true }
  );

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

      info(`Do action "${action}" on MozSearch panel-item`);
      let newTabOpened = BrowserTestUtils.waitForNewTab(
        newWin.gBrowser,
        expected,
        true
      );

      if (action == "click") {
        EventUtils.synthesizeMouseAtCenter(
          popup.querySelector(`panel-item[data-engine-name=MozSearch]`),
          {
            accelKey: true,
          },
          newWin
        );
      } else if (action == "middleclick") {
        EventUtils.synthesizeMouseAtCenter(
          popup.querySelector(`panel-item[data-engine-name=MozSearch]`),
          {
            button: 1,
          },
          newWin
        );
      } else /* action == "key" */ {
        let panelItems = Array.from(popup.querySelectorAll("panel-item"));
        let index = panelItems.findIndex(
          item => item.dataset.engineName == "MozSearch"
        );
        for (let i = 0; i < index; i++) {
          EventUtils.synthesizeKey("KEY_ArrowDown", {}, newWin);
        }
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
  await unloadSearchExtension(searchExtension);
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
      source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
      entry: expectedEntry,
      restrictType: "keyword",
    });

    info("Check the suggestions");
    Assert.equal(UrlbarTestUtils.getResultCount(window), 1);
    const bookmark = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    Assert.equal(bookmark.result.source, UrlbarUtils.RESULT_SOURCE.BOOKMARKS);
    Assert.equal(bookmark.result.type, UrlbarUtils.RESULT_TYPE.URL);
    Assert.equal(bookmark.result.payload.url, "https://example.com/");
    Assert.equal(bookmark.result.payload.title, "BOOKMARK");

    info("Choose any search engine from the switcher");
    await UrlbarTestUtils.activateSearchModeSwitcherItem(
      window,
      "panel-item[data-engine-id=bing]"
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
            source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
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
    "search"
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
  let newWin = await BrowserTestUtils.openNewBrowserWindow();

  const stub = sinon
    .stub(UrlbarSearchUtils, "init")
    .rejects(new Error("Initialization failed"));

  SearchService.forceInitializationStatusForTests("failed");

  // Force updateSearchIcon to be triggered
  await SpecialPowers.pushPrefEnv({
    set: [["keyword.enabled", false]],
  });

  let searchModeSwitcherIconUrl = await BrowserTestUtils.waitForCondition(
    () => UrlbarTestUtils.getSearchModeSwitcherIcon(newWin),
    "Waiting for the search mode switcher icon to update after exiting search mode."
  );

  Assert.equal(
    searchModeSwitcherIconUrl,
    UrlbarUtils.ICON.GLOBE,
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

  stub.restore();

  SearchService.forceInitializationStatusForTests("success");
  UrlbarSearchUtils.resetInitPromiseForTests();
  await UrlbarSearchUtils.init();

  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_search_mode_switcher_engine_no_icon() {
  const testEngineName = "TestEngineNoIcon";
  let searchExtension = await SearchTestUtils.installSearchExtension(
    {
      name: testEngineName,
      search_url: "https://www.example.com/search?q=",
      favicon_url: "",
    },
    { skipUnload: true }
  );

  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    `panel-item[data-engine-name="${testEngineName}"]`
  );

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(window),
    UrlbarUtils.ICON.SEARCH_GLASS,
    "The search mode switcher should display the default search glass icon when the engine has no icon."
  );

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);

  await unloadSearchExtension(searchExtension);
});

add_task(async function test_search_mode_switcher_private_engine_icon() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.search.separatePrivateDefault.ui.enabled", true]],
  });

  const testEngineName = "DefaultPrivateEngine";
  let searchExtension = await SearchTestUtils.installSearchExtension(
    {
      name: testEngineName,
      search_url: "https://www.example.com/search?q=",
      icons: {
        16: "private.png",
      },
    },
    { skipUnload: true }
  );

  const defaultPrivateEngine = SearchService.getEngineByName(testEngineName);
  const defaultPrivateEngineIcon = `moz-extension://${searchExtension.uuid}/private.png`;
  const defaultEngine = await SearchService.getDefault();
  const defaultEngineIcon = await defaultEngine.getIconURL();

  SearchService.setDefaultPrivate(
    defaultPrivateEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  Assert.notEqual(
    defaultEngine.id,
    defaultPrivateEngine.id,
    "Default engine is not private engine."
  );
  Assert.equal(
    (await SearchService.getDefault()).id,
    defaultEngine.id,
    "Default engine is still correct."
  );
  Assert.equal(
    (await SearchService.getDefaultPrivate()).id,
    defaultPrivateEngine.id,
    "Default private engine is correct."
  );

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(window),
    defaultEngineIcon,
    "Is the icon of the default engine."
  );

  info("Open a private window");
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  info("Input any text to update the icon");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: privateWin,
    value: "abc",
  });

  Assert.equal(
    UrlbarTestUtils.getSearchModeSwitcherIcon(privateWin),
    defaultPrivateEngineIcon,
    "Is the icon of the default private engine."
  );

  info("Changing the default private engine.");
  SearchService.setDefaultPrivate(
    defaultEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  info("Waiting for the icon to be updated.");
  await TestUtils.waitForCondition(
    () =>
      UrlbarTestUtils.getSearchModeSwitcherIcon(privateWin) == defaultEngineIcon
  );
  Assert.ok(true, "The icon was updated.");

  await BrowserTestUtils.closeWindow(privateWin);
  await unloadSearchExtension(searchExtension);
  await SpecialPowers.popPrefEnv();
});

add_task(async function open_with_alt_option_with_open_view() {
  info(
    "Open the urlbar and searchmode switcher popup with Arrow Down + Alt/Option keys while the results view is open"
  );
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  await UrlbarTestUtils.openSearchModeSwitcher(window, () => {
    EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  });

  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  EventUtils.synthesizeKey("KEY_Escape");
  await popupHidden;
});

add_task(async function open_with_alt_option_with_closed_view() {
  info(
    "Open the urlbar and searchmode switcher popup with Arrow Up + Alt/Option keys while the results view is closed"
  );
  await UrlbarTestUtils.openSearchModeSwitcher(window, () => {
    EventUtils.synthesizeKey("KEY_ArrowUp", { altKey: true });
  });

  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  EventUtils.synthesizeKey("KEY_Escape");
  await popupHidden;
});

add_task(async function change_engines_with_accel_updown() {
  info("Navigate engines with Accel+Up/Down");

  let win = await BrowserTestUtils.openNewBrowserWindow();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "",
  });

  EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, win);

  await BrowserTestUtils.waitForCondition(
    () => !!win.gURLBar.searchMode,
    "We entered searchmode"
  );

  let firstEngine = win.gURLBar.searchMode.engineName;
  EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, win);

  await BrowserTestUtils.waitForCondition(
    () => win.gURLBar.searchMode.engineName != firstEngine,
    "We navigated to another engine"
  );

  await BrowserTestUtils.waitForCondition(() => {
    EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, win);
    return win.gURLBar.searchMode?.engineName == firstEngine;
  }, "We navigated back to first engine");
  await UrlbarTestUtils.exitSearchMode(win);
  await BrowserTestUtils.closeWindow(win);
});
