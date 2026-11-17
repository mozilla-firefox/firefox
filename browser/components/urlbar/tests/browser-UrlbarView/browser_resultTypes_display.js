/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SyncedTabs } = ChromeUtils.importESModule(
  "resource://services-sync/SyncedTabs.sys.mjs"
);
const { WeaveService } = ChromeUtils.importESModule(
  "moz-src:///services/sync/Weave.sys.mjs"
);

const TEST_ENGINE_BASENAME = "searchSuggestionEngine.xml";

function assertElementsDisplayed(details, expected) {
  Assert.equal(
    details.type,
    expected.type,
    "Should be displaying a row of the correct type"
  );
  Assert.equal(
    details.title,
    expected.title,
    "Should be displaying the correct title"
  );
  Assert.equal(
    UrlbarTestUtils.isSeparatorVisible(details.element.separator),
    expected.separator,
    `Should${expected.separator ? " " : " not "}be displaying a separator`
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", false],
      // Disable search suggestions in the urlbar.
      ["browser.urlbar.suggest.searches", false],
      // Clear historical search suggestions to avoid interference from previous
      // tests.
      ["browser.urlbar.maxHistoricalSearchSuggestions", 0],
      // Turn autofill off.
      ["browser.urlbar.autoFill", false],
    ],
  });

  await SearchTestUtils.installOpenSearchEngine({
    url: getRootDirectory(gTestPath) + TEST_ENGINE_BASENAME,
    setAsDefault: true,
  });

  // Move the mouse away from the results panel, because hovering a result may
  // change its aspect (e.g. by showing a " - search with Engine" suffix).
  await EventUtils.promiseNativeMouseEvent({
    type: "mousemove",
    target: gURLBar.inputField,
    offsetX: 0,
    offsetY: 0,
  });
});

add_task(async function test_tab_switch_result() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:mozilla"
  );

  await BrowserTestUtils.withNewTab({ gBrowser }, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "about:mozilla",
      fireInputEvent: true,
    });

    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

    assertElementsDisplayed(details, {
      separator: false,
      title: "about:mozilla",
      type: UrlbarShared.RESULT_TYPE.TAB_SWITCH,
    });
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_search_result() {
  Services.prefs.setBoolPref("browser.urlbar.suggest.searches", true);

  await BrowserTestUtils.withNewTab({ gBrowser }, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "foo",
      fireInputEvent: true,
    });

    let index = await UrlbarTestUtils.promiseSuggestionsPresent(window);
    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, index);

    // We'll initially display no separator.
    assertElementsDisplayed(details, {
      separator: false,
      title: "foofoo",
      type: UrlbarShared.RESULT_TYPE.SEARCH,
    });

    // Down to select the first search suggestion.
    for (let i = index; i > 0; --i) {
      EventUtils.synthesizeKey("KEY_ArrowDown");
    }

    // We should now be displaying one.
    assertElementsDisplayed(details, {
      separator: true,
      title: "foofoo",
      type: UrlbarShared.RESULT_TYPE.SEARCH,
    });
  });

  await PlacesUtils.history.clear();
  Services.prefs.setBoolPref("browser.urlbar.suggest.searches", false);
});

add_task(async function test_url_result() {
  await PlacesTestUtils.addVisits([
    {
      uri: "http://example.com",
      title: "example",
      transition: Ci.nsINavHistoryService.TRANSITION_TYPED,
    },
  ]);

  await BrowserTestUtils.withNewTab({ gBrowser }, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "example",
      fireInputEvent: true,
    });

    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

    assertElementsDisplayed(details, {
      separator: true,
      title: "example",
      type: UrlbarShared.RESULT_TYPE.URL,
    });
  });

  await PlacesUtils.history.clear();
});

add_task(async function test_keyword_result() {
  const TEST_URL = `${TEST_BASE_URL}print_postdata.sjs`;

  await PlacesUtils.keywords.insert({
    keyword: "get",
    url: TEST_URL + "?q=%s",
  });

  await BrowserTestUtils.withNewTab({ gBrowser }, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "get ",
      fireInputEvent: true,
    });

    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

    // Because only the keyword is typed, we show the bookmark url.
    assertElementsDisplayed(details, {
      separator: true,
      title: TEST_URL.substring("https://".length) + "?q=",
      type: UrlbarShared.RESULT_TYPE.KEYWORD,
    });

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "get test",
      fireInputEvent: true,
    });

    details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

    assertElementsDisplayed(details, {
      separator: false,
      title: "example.com: test",
      type: UrlbarShared.RESULT_TYPE.KEYWORD,
    });
  });
});

add_task(async function test_omnibox_result() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      omnibox: {
        keyword: "omniboxtest",
      },
    },

    background() {
      /* global browser */
      browser.omnibox.setDefaultSuggestion({
        description: "doit",
      });
      // Just do nothing for this test.
      browser.omnibox.onInputEntered.addListener(() => {});
      browser.omnibox.onInputChanged.addListener((text, suggest) => {
        suggest([]);
      });
    },
  });

  await extension.startup();

  await BrowserTestUtils.withNewTab({ gBrowser }, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "omniboxtest ",
      fireInputEvent: true,
    });

    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);

    assertElementsDisplayed(details, {
      separator: true,
      title: "doit",
      type: UrlbarShared.RESULT_TYPE.OMNIBOX,
    });
  });

  await extension.unload();
});

add_task(async function test_remote_tab_result() {
  await SpecialPowers.pushPrefEnv({
    set: [["services.sync.username", "fake"]],
  });
  // Clear history so that history added by previous tests doesn't mess up this
  // test when it selects results in the urlbar.
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  const REMOTE_TAB = {
    id: "7cqCr77ptzX3",
    type: "client",
    lastModified: 1492201200,
    name: "zcarter's Nightly on MacBook-Pro-25",
    clientType: "desktop",
    tabs: [
      {
        type: "tab",
        title: "Test Remote",
        url: "http://example.com",
        icon: UrlbarShared.ICON.DEFAULT,
        client: "7cqCr77ptzX3",
        lastUsed: Math.floor(Date.now() / 1000),
      },
    ],
  };

  const sandbox = sinon.createSandbox();

  let originalSyncedTabsInternal = SyncedTabs._internal;
  SyncedTabs._internal = {
    isConfiguredToSyncTabs: true,
    hasSyncedThisSession: true,
    getTabClients() {
      return Promise.resolve([]);
    },
    syncTabs() {
      return Promise.resolve();
    },
  };

  // Tell the Sync service it is initialized.
  let oldWeaveServiceReady = WeaveService.ready;
  WeaveService.ready = true;

  sandbox
    .stub(SyncedTabs._internal, "getTabClients")
    .callsFake(() => Promise.resolve(Cu.cloneInto([REMOTE_TAB], {})));

  // Reset internal cache in UrlbarProviderRemoteTabs.
  Services.obs.notifyObservers(null, "weave:engine:sync:finish", "tabs");

  registerCleanupFunction(async function () {
    sandbox.restore();
    WeaveService.ready = oldWeaveServiceReady;
    SyncedTabs._internal = originalSyncedTabsInternal;
    await PlacesUtils.history.clear();
    await PlacesUtils.bookmarks.eraseEverything();
  });

  await BrowserTestUtils.withNewTab({ gBrowser }, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "example",
      fireInputEvent: true,
    });

    const details = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

    assertElementsDisplayed(details, {
      separator: false,
      title: "Test Remote",
      type: UrlbarShared.RESULT_TYPE.REMOTE_TAB,
    });
  });
  await SpecialPowers.popPrefEnv();
});
