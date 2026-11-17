/* eslint-disable sdl/no-insecure-url */
const { ASRouterTriggerListeners } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterTriggerListeners.sys.mjs"
);

const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);

ChromeUtils.defineLazyGetter(this, "SearchTestUtils", () => {
  const { SearchTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/SearchTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineESModuleGetters(this, {
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  ProxyUsage:
    "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTestUtils: "resource://testing-common/UrlbarTestUtils.sys.mjs",
  WeaveService: "moz-src:///services/sync/Weave.sys.mjs",
});

const mockIdleService = {
  _observers: new Set(),
  _fireObservers(state) {
    for (let observer of this._observers.values()) {
      // Pass idle time in seconds as a string in the data parameter,
      // matching the real nsIUserIdleService behavior
      const data = state === "idle" ? "1200" : null;
      observer.observe(this, state, data);
    }
  },
  QueryInterface: ChromeUtils.generateQI(["nsIUserIdleService"]),
  idleTime: 1200000,
  addIdleObserver(observer) {
    this._observers.add(observer);
  },
  removeIdleObserver(observer) {
    this._observers.delete(observer);
  },
};

const sleepMs = (ms = 0) => new Promise(resolve => setTimeout(resolve, ms)); // eslint-disable-line mozilla/no-arbitrary-setTimeout

const inChaosMode = !!parseInt(Services.env.get("MOZ_CHAOSMODE"), 16);

async function waitForUrlLoad(url) {
  let browser = gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(browser, url);
  await BrowserTestUtils.browserLoaded(browser, false, url);
}

async function test_formAutofillTrigger() {
  const sandbox = sinon.createSandbox();
  const handlerStub = sandbox.stub();
  const formAutofillTrigger = ASRouterTriggerListeners.get("formAutofill");
  sandbox.stub(formAutofillTrigger, "_triggerDelay").value(0);
  formAutofillTrigger.uninit();
  formAutofillTrigger.init(handlerStub);

  function notifyCreditCardSaved() {
    Services.obs.notifyObservers(
      {
        wrappedJSObject: { sourceSync: false, collectionName: "creditCards" },
      },
      formAutofillTrigger._topic,
      "add"
    );
  }

  // Saving credit cards for autofill currently fails for some hardware
  // configurations, so mock the event instead of really adding a card.
  notifyCreditCardSaved();
  await sleepMs(1);
  Assert.ok(handlerStub.called, "Called after event");

  // Test that the trigger doesn't fire when the credit card manager is open.
  handlerStub.resetHistory();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        const savedPaymentsBtn = await ContentTaskUtils.waitForCondition(
          () => content.document.querySelector("#savedPaymentsButton"),
          "Waiting for credit card manager button"
        );

        savedPaymentsBtn.click();

        const paymentsPage = content.document.querySelector(
          '[data-category="paneManagePayments"]'
        );

        await ContentTaskUtils.waitForCondition(
          () => paymentsPage && !paymentsPage.hidden,
          "Payments page failed to show."
        );
      });

      notifyCreditCardSaved();
      await sleepMs(1);
    }
  );

  formAutofillTrigger.uninit();
  handlerStub.resetHistory();
  notifyCreditCardSaved();
  await sleepMs(1);
  Assert.ok(handlerStub.notCalled, "Not called after uninit");

  sandbox.restore();
  formAutofillTrigger.uninit();
}

add_setup(async function () {
  // Runtime increases in chaos mode on Mac.
  if (inChaosMode && AppConstants.platform === "macosx") {
    requestLongerTimeout(2);
  }

  UrlbarTestUtils.init(this);

  // Installing this search extension prevents errors that
  // are generated from contacting the outside world
  // when completing searches during tests
  await SearchTestUtils.installSearchExtension(
    {
      search_url:
        "https://example.com/browser/browser/components/search/test/browser/test.html",
      search_url_get_params: "s={searchTerms}&abc=ff",
      suggest_url:
        "https://example.com/browser/browser/components/search/test/browser/searchSuggestionEngine.sjs",
      suggest_url_get_params: "query={searchTerms}",
    },
    { setAsDefault: true }
  );

  registerCleanupFunction(() => {
    const trigger = ASRouterTriggerListeners.get("openURL");
    trigger.uninit();
  });
});

add_task(async function test_openURL_visit_counter() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  trigger.init(stub, ["example.com"]);

  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("https://example.com/");
  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("http://example.com/");
  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("http://example.com/");

  Assert.equal(stub.callCount, 3, "Stub called 3 times for example.com host");
  Assert.equal(
    stub.firstCall.args[1].context.visitsCount,
    1,
    "First call should have count 1"
  );
  Assert.equal(
    stub.thirdCall.args[1].context.visitsCount,
    2,
    "Third call should have count 2 for http://example.com"
  );
  Assert.equal(
    stub.firstCall.args[1].context.host,
    "example.com",
    "context.host is the navigated host"
  );
  Assert.equal(
    stub.firstCall.args[1].context.url,
    "https://example.com/",
    "context.url is the navigated URL spec"
  );
});

add_task(async function test_openURL_visit_counter_withPattern() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  // Match any valid URL
  trigger.init(stub, [], ["*://*/*"]);

  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("https://example.com/");
  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("http://example.com/");
  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("http://example.com/");

  Assert.equal(stub.callCount, 3, "Stub called 3 times for example.com host");
  Assert.equal(
    stub.firstCall.args[1].context.visitsCount,
    1,
    "First call should have count 1"
  );
  Assert.equal(
    stub.thirdCall.args[1].context.visitsCount,
    2,
    "Third call should have count 2 for http://example.com"
  );
});

add_task(async function test_openURL_total_visit_counter() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  // Match any valid URL
  trigger.init(stub, [], ["*://*/*"]);

  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("https://example.com/");
  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("http://example.com/");
  await waitForUrlLoad("about:blank");
  await waitForUrlLoad("https://example.com/?v=2");

  Assert.equal(stub.callCount, 3, "Stub called for each matched page load");
  Assert.deepEqual(
    stub.getCalls().map(call => call.args[1].context.totalVisitsCount),
    [1, 2, 3],
    "totalVisitsCount accumulates across distinct URLs matching the pattern"
  );
  Assert.deepEqual(
    stub.getCalls().map(call => call.args[1].context.visitsCount),
    [1, 1, 1],
    "visitsCount stays per-URL"
  );

  trigger.uninit();
});

add_task(async function test_openURL_isAddressBarUrlNavigation_typed() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  trigger.init(stub, ["example.com"]);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "https://example.com/",
  });
  const loaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    "https://example.com/"
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await loaded;

  Assert.equal(stub.callCount, 1, "Trigger fired for the typed navigation");
  Assert.equal(
    stub.firstCall.args[1].context.isAddressBarUrlNavigation,
    true,
    "Navigation caused by typing a URL into the address bar and hitting " +
      "Enter should be flagged as such"
  );

  trigger.uninit();
});

add_task(async function test_openURL_isAddressBarUrlNavigation_notTyped() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  trigger.init(stub, ["example.com"]);

  await waitForUrlLoad("https://example.com/");

  Assert.equal(
    stub.callCount,
    1,
    "Trigger fired for the programmatic navigation"
  );
  Assert.equal(
    stub.firstCall.args[1].context.isAddressBarUrlNavigation,
    false,
    "A navigation not preceded by an address bar interaction should not " +
      "be flagged as an address bar URL navigation"
  );

  trigger.uninit();
});

add_task(
  async function test_openURL_isAddressBarUrlNavigation_notLeakedForward() {
    const trigger = ASRouterTriggerListeners.get("openURL");
    const stub = sinon.stub();
    trigger.uninit();

    trigger.init(stub, ["example.com", "example.org"]);

    // Type a URL that doesn't match either host we're listening for, so it
    // won't itself cause the trigger to fire, but it does record a direct
    // navigation urlbar interaction for the current browser.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "https://example.net/",
    });
    const firstLoaded = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      false,
      "https://example.net/"
    );
    EventUtils.synthesizeKey("KEY_Enter");
    await firstLoaded;

    Assert.equal(
      stub.callCount,
      0,
      "Trigger should not fire for a host it isn't listening for"
    );

    // A second, unrelated (non-address-bar) navigation on the same browser
    // shouldn't inherit the flag from the previous one.
    await waitForUrlLoad("https://example.com/");

    Assert.equal(stub.callCount, 1, "Trigger fired for example.com");
    Assert.equal(
      stub.firstCall.args[1].context.isAddressBarUrlNavigation,
      false,
      "The earlier address bar navigation to a different host must not be " +
        "attributed to this one"
    );

    trigger.uninit();
  }
);

add_task(
  async function test_openURL_isAddressBarUrlNavigation_backNavigationAfterSearch() {
    const trigger = ASRouterTriggerListeners.get("openURL");
    const stub = sinon.stub();
    trigger.uninit();

    trigger.init(stub, ["example.com"]);

    let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

    // Simulate a SAP search from the address bar (e.g. searching "iphone"),
    // landing on a search-results page hosted on example.com.
    const searchLoaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    gURLBar.value = "test search term";
    gURLBar.focus();
    EventUtils.synthesizeKey("KEY_Enter");
    await searchLoaded;
    const serpUrl = tab.linkedBrowser.currentURI.spec;

    Assert.equal(
      stub.callCount,
      1,
      "Trigger fired for the initial search-results visit"
    );
    Assert.equal(
      stub.firstCall.args[1].context.isAddressBarUrlNavigation,
      false,
      "The initial SAP search should not be flagged as an address bar URL " +
        "navigation"
    );

    // Simulate clicking a result that navigates away from the SERP (e.g. a
    // shopping result), then clicking Back to return to it.
    await waitForUrlLoad("https://example.org/");

    const backLoaded = BrowserTestUtils.waitForLocationChange(
      gBrowser,
      serpUrl
    );
    tab.linkedBrowser.goBack();
    await backLoaded;

    Assert.equal(
      stub.callCount,
      2,
      "Trigger fired again when navigating back to the search-results page"
    );
    Assert.equal(
      stub.secondCall.args[1].context.isAddressBarUrlNavigation,
      false,
      "Navigating back to a SAP search-results page must not be flagged " +
        "as an address bar URL navigation, even though the address bar " +
        "was used earlier in the session"
    );

    trigger.uninit();
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_openURL_isAddressBarUrlNavigation_bookmark() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  trigger.init(stub, ["example.com"]);

  const bookmarkURL = "https://example.com/bookmarked-path";
  const bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: bookmarkURL,
    title: "bookmark for isAddressBarUrlNavigation test",
  });
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  // -1 means the matching row hasn't been found yet.
  let bookmarkRowIndex = -1;

  // Use bookmarks keyword to filter by bookmarks
  await TestUtils.waitForCondition(async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "@bookmarks",
    });

    EventUtils.synthesizeKey("KEY_Enter", {}, window);

    // Then search the actual bookmark
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "bookmarked-path",
    });

    let resultCount = UrlbarTestUtils.getResultCount(window);
    for (let i = 0; i < resultCount; i++) {
      let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
      if (
        details.result.payload.url === bookmarkURL &&
        details.result.source === UrlbarShared.RESULT_SOURCE.BOOKMARKS
      ) {
        bookmarkRowIndex = i;
        return true;
      }
    }
    return false;
  }, "Waiting for the newly-inserted bookmark to appear in the urlbar results");

  let loadPromise = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  UrlbarTestUtils.setSelectedRowIndex(window, bookmarkRowIndex);
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  await loadPromise;

  Assert.equal(stub.callCount, 1, "Trigger fired for the bookmark navigation");
  Assert.equal(
    stub.firstCall.args[1].context.isAddressBarUrlNavigation,
    true,
    "Picking a bookmark match from the address bar dropdown should be " +
      "flagged as an address bar navigation"
  );

  trigger.uninit();
  await PlacesUtils.bookmarks.remove(bookmark.guid);
});

add_task(async function test_openURL_isAddressBarUrlNavigation_remoteTab() {
  const { SyncedTabs } = ChromeUtils.importESModule(
    "resource://services-sync/SyncedTabs.sys.mjs"
  );

  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  trigger.init(stub, ["example.com"]);

  const sandbox = sinon.createSandbox();
  const originalSyncedTabsInternal = SyncedTabs._internal;
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

  const oldWeaveServiceReady = WeaveService.ready;
  WeaveService.ready = true;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill", false],
      ["services.sync.username", "fake"],
    ],
  });

  const remoteTabURL = "https://example.com/remote-tab";
  const REMOTE_TAB = {
    id: "openURLTriggerRemoteClient",
    type: "client",
    lastModified: 1492201200,
    name: "Remote client",
    clientType: "desktop",
    tabs: [
      {
        type: "tab",
        title: "Remote example",
        url: remoteTabURL,
        icon: UrlbarShared.ICON.DEFAULT,
        client: "openURLTriggerRemoteClient",
        lastUsed: Math.floor(Date.now() / 1000),
      },
    ],
  };
  sandbox
    .stub(SyncedTabs._internal, "getTabClients")
    .callsFake(() => Promise.resolve(Cu.cloneInto([REMOTE_TAB], {})));

  try {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "Remote example",
    });
    await UrlbarTestUtils.pickResultAndWaitForLoad(
      window,
      remoteTabURL,
      UrlbarShared.RESULT_TYPE.REMOTE_TAB
    );

    Assert.equal(
      stub.callCount,
      1,
      "Trigger fired for the remote tab navigation"
    );
    Assert.equal(
      stub.firstCall.args[1].context.isAddressBarUrlNavigation,
      false,
      "Picking a synced-device (remote tab) result should not be flagged " +
        "as an address bar URL navigation"
    );
  } finally {
    trigger.uninit();
    sandbox.restore();
    WeaveService.ready = oldWeaveServiceReady;
    SyncedTabs._internal = originalSyncedTabsInternal;
    await SpecialPowers.popPrefEnv();
  }
});

function notifyUrlbarUserStartNavigation(result) {
  Services.obs.notifyObservers(
    { wrappedJSObject: { result } },
    "urlbar-user-start-navigation"
  );
}

add_task(
  async function test_openURL_isAddressBarUrlNavigation_sponsoredSuggestion() {
    const trigger = ASRouterTriggerListeners.get("openURL");
    const stub = sinon.stub();
    trigger.uninit();

    trigger.init(stub, ["example.com"]);

    // Sponsored/Suggest results (AMP, Yelp, Weather, Wikipedia, Addon,
    // Dynamic) are RESULT_TYPE.URL with RESULT_SOURCE.SEARCH: search-intent
    // results, not a direct URL navigation.
    notifyUrlbarUserStartNavigation({
      type: UrlbarShared.RESULT_TYPE.URL,
      source: UrlbarShared.RESULT_SOURCE.SEARCH,
    });

    await waitForUrlLoad("https://example.com/");

    Assert.equal(
      stub.callCount,
      1,
      "Trigger fired for the sponsored-suggestion-triggered navigation"
    );
    Assert.equal(
      stub.firstCall.args[1].context.isAddressBarUrlNavigation,
      false,
      "Picking a sponsored/Suggest result should not be flagged as an " +
        "address bar URL navigation"
    );

    trigger.uninit();
  }
);

add_task(async function test_openURL_isAddressBarUrlNavigation_mdnSuggestion() {
  const trigger = ASRouterTriggerListeners.get("openURL");
  const stub = sinon.stub();
  trigger.uninit();

  trigger.init(stub, ["example.com"]);

  // MDN suggestions are RESULT_TYPE.URL with RESULT_SOURCE.OTHER_NETWORK,
  // so they need their own check separate from the SEARCH-sourced Suggest
  // results above.
  notifyUrlbarUserStartNavigation({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.OTHER_NETWORK,
  });

  await waitForUrlLoad("https://example.com/");

  Assert.equal(
    stub.callCount,
    1,
    "Trigger fired for the MDN-suggestion-triggered navigation"
  );
  Assert.equal(
    stub.firstCall.args[1].context.isAddressBarUrlNavigation,
    false,
    "Picking an MDN suggestion should not be flagged as an address bar " +
      "URL navigation"
  );

  trigger.uninit();
});

add_task(async function test_captivePortalLogin() {
  const stub = sinon.stub();
  const captivePortalTrigger =
    ASRouterTriggerListeners.get("captivePortalLogin");

  captivePortalTrigger.init(stub);

  Services.obs.notifyObservers(this, "captive-portal-login-success", {});

  Assert.ok(stub.called, "Called after login event");

  captivePortalTrigger.uninit();

  Services.obs.notifyObservers(this, "captive-portal-login-success", {});

  Assert.equal(stub.callCount, 1, "Not called after uninit");
});

add_task(async function test_preferenceObserver() {
  const stub = sinon.stub();
  const poTrigger = ASRouterTriggerListeners.get("preferenceObserver");

  poTrigger.uninit();

  poTrigger.init(stub, ["foo.bar", "bar.foo"]);

  Services.prefs.setStringPref("foo.bar", "foo.bar");

  Assert.ok(stub.calledOnce, "Called for pref foo.bar");
  Assert.deepEqual(
    stub.firstCall.args[1],
    {
      id: "preferenceObserver",
      param: { type: "foo.bar" },
    },
    "Called with expected arguments"
  );

  Services.prefs.setStringPref("bar.foo", "bar.foo");
  Assert.ok(stub.calledTwice, "Called again for second pref.");
  Services.prefs.clearUserPref("foo.bar");
  Assert.ok(stub.calledThrice, "Called when clearing the pref as well.");

  stub.resetHistory();
  poTrigger.uninit();

  Services.prefs.clearUserPref("bar.foo");
  Assert.ok(stub.notCalled, "Not called after uninit");
});

add_task(async function test_nthTabClosed() {
  const handlerStub = sinon.stub();
  const tabClosedTrigger = ASRouterTriggerListeners.get("nthTabClosed");
  tabClosedTrigger.uninit();
  tabClosedTrigger.init(handlerStub);

  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  BrowserTestUtils.removeTab(tab1);
  Assert.ok(handlerStub.calledOnce, "Called once after first tab closed");

  BrowserTestUtils.removeTab(tab2);
  Assert.ok(handlerStub.calledTwice, "Called twice after second tab closed");

  handlerStub.resetHistory();
  tabClosedTrigger.uninit();

  Assert.ok(handlerStub.notCalled, "Not called after uninit");
});

add_task(async function test_nthTabOpened() {
  const handlerStub = sinon.stub();
  const tabOpenedTrigger = ASRouterTriggerListeners.get("nthTabOpened");
  tabOpenedTrigger.uninit();
  tabOpenedTrigger.init(handlerStub);

  const win = await BrowserTestUtils.openNewBrowserWindow();

  await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
  Assert.ok(handlerStub.calledOnce, "Called once after first tab opened");

  await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
  Assert.ok(handlerStub.calledTwice, "Called twice after second tab opened");

  BrowserTestUtils.closeWindow(win);
  handlerStub.resetHistory();
  tabOpenedTrigger.uninit();

  Assert.ok(handlerStub.notCalled, "Not called after uninit");
});

add_task(async function test_nthTabClosed_with_actionSource_marker() {
  const handlerStub = sinon.stub();
  const tabClosedTrigger = ASRouterTriggerListeners.get("nthTabClosed");
  tabClosedTrigger.uninit();
  tabClosedTrigger.init(handlerStub);

  const markedTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  markedTab.smartWindowActionSource = "close_current_tab";
  BrowserTestUtils.removeTab(markedTab);

  Assert.ok(handlerStub.calledOnce, "Handler called once");
  const [, triggerData] = handlerStub.firstCall.args;
  Assert.equal(
    triggerData.context.actionSource,
    "close_current_tab",
    "action field populated from tab marker"
  );

  // with unmarked tab action should be absent
  const plainTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  BrowserTestUtils.removeTab(plainTab);
  Assert.ok(handlerStub.calledTwice, "Handler called a second time");
  const [, secondContext] = handlerStub.secondCall.args;
  Assert.ok(
    !("actionSource" in secondContext.context),
    "actionSource field absent on unmarked close"
  );

  tabClosedTrigger.uninit();
});

function getIdleTriggerMock() {
  const idleTrigger = ASRouterTriggerListeners.get("activityAfterIdle");
  idleTrigger.uninit();
  const sandbox = sinon.createSandbox();
  const handlerStub = sandbox.stub();
  sandbox.stub(idleTrigger, "_triggerDelay").value(0);
  sandbox.stub(idleTrigger, "_wakeDelay").value(30);
  sandbox.stub(idleTrigger, "_idleService").value(mockIdleService);
  let restored = false;
  const restore = () => {
    if (restored) {
      return;
    }
    restored = true;
    idleTrigger.uninit();
    sandbox.restore();
  };
  registerCleanupFunction(restore);
  idleTrigger.init(handlerStub);
  return { idleTrigger, handlerStub, restore };
}

// Test that the trigger fires under normal conditions.
add_task(async function test_activityAfterIdle() {
  const { handlerStub, restore } = getIdleTriggerMock();
  let firedOnActive = new Promise(resolve =>
    handlerStub.callsFake(() => resolve(true))
  );
  mockIdleService._fireObservers("idle");
  await TestUtils.waitForTick();
  ok(handlerStub.notCalled, "Not called when idle");
  mockIdleService._fireObservers("active");
  ok(await firedOnActive, "Called once when active after idle");
  restore();
});

// Test that the trigger does not fire when the active window is private.
add_task(async function test_activityAfterIdlePrivateWindow() {
  const { handlerStub, restore } = getIdleTriggerMock();
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  ok(PrivateBrowsingUtils.isWindowPrivate(privateWin), "Window is private");
  await TestUtils.waitForTick();
  mockIdleService._fireObservers("idle");
  await TestUtils.waitForTick();
  mockIdleService._fireObservers("active");
  await TestUtils.waitForTick();
  ok(handlerStub.notCalled, "Not called when active window is private");
  await BrowserTestUtils.closeWindow(privateWin);
  restore();
});

// Test that the trigger does not fire when the window is minimized, but does
// fire after the window is restored.
add_task(async function test_activityAfterIdleHiddenWindow() {
  const { handlerStub, restore } = getIdleTriggerMock();
  let firedOnRestore = new Promise(resolve =>
    handlerStub.callsFake(() => resolve(true))
  );
  window.minimize();
  await TestUtils.waitForCondition(
    () => window.windowState === window.STATE_MINIMIZED,
    "Window should be minimized"
  );
  mockIdleService._fireObservers("idle");
  await TestUtils.waitForTick();
  mockIdleService._fireObservers("active");
  await TestUtils.waitForTick();
  ok(handlerStub.notCalled, "Not called when window is minimized");
  window.restore();
  ok(await firedOnRestore, "Called once after restoring minimized window");
  restore();
});

// Test that the trigger does not fire immediately after waking from sleep.
add_task(async function test_activityAfterIdleWake() {
  const { handlerStub, restore } = getIdleTriggerMock();
  let firedAfterWake = new Promise(resolve =>
    handlerStub.callsFake(() => resolve(true))
  );
  mockIdleService._fireObservers("wake_notification");
  mockIdleService._fireObservers("idle");
  await sleepMs(1);
  mockIdleService._fireObservers("active");
  await sleepMs(inChaosMode ? 32 : 300);
  ok(handlerStub.notCalled, "Not called immediately after waking from sleep");

  mockIdleService._fireObservers("idle");
  await TestUtils.waitForTick();
  mockIdleService._fireObservers("active");
  ok(
    await firedAfterWake,
    "Called once after waiting for wake delay before firing idle"
  );
  restore();
});

add_task(async function test_formAutofill() {
  await test_formAutofillTrigger(true);
});

add_task(async function test_pageActionInUrlbarTrigger() {
  const sandbox = sinon.createSandbox();
  const receivedTrigger = new Promise(resolve => {
    sandbox
      .stub(ASRouter, "sendTriggerMessage")
      .callsFake(({ id, context }) => {
        if (
          id === "pageActionInUrlbar" &&
          context?.pageAction === "picture-in-picture-button"
        ) {
          resolve(true);
        }
      });
  });
  sandbox
    .stub(PictureInPicture, "getEligiblePipVideoCount")
    .returns({ totalPipCount: 1, totalPipDisabled: 0 });

  await SpecialPowers.pushPrefEnv({
    set: [
      ["media.videocontrols.picture-in-picture.enabled", true],
      ["media.videocontrols.picture-in-picture.urlbar-button.enabled", true],
    ],
  });

  PictureInPicture.updateUrlbarToggle(gBrowser.selectedBrowser);

  let pageAction = await receivedTrigger;
  ok(pageAction, "pageActionInUrlbar trigger sent with PiP button id");

  is(
    gBrowser.selectedBrowser.currentURI.host,
    "example.com",
    "host is example.com"
  );

  await SpecialPowers.popPrefEnv();
  sandbox.restore();

  PictureInPicture.updateUrlbarToggle(gBrowser.selectedBrowser);
});

add_task(async function test_onSearchIncrement() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  Services.prefs.setIntPref("browser.search.totalSearches", 0);

  const onLoaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let SEARCH_TERM = "test search term";
  gURLBar.value = SEARCH_TERM;
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoaded;

  const totalSearches = Services.prefs.getIntPref(
    "browser.search.totalSearches",
    0
  );
  Assert.equal(totalSearches, 1, "Total searches has incremented by 1");

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_onSearchIncrement_cap() {
  const cap = 100;
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  Services.prefs.setIntPref("browser.search.totalSearches", cap);

  const onLoaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let SEARCH_TERM = "test search term";
  gURLBar.value = SEARCH_TERM;
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoaded;

  const totalSearches = Services.prefs.getIntPref(
    "browser.search.totalSearches",
    0
  );
  Assert.equal(
    totalSearches,
    cap,
    `Total searches has not incremented past ${cap}`
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_onSearchTrigger() {
  const sandbox = sinon.createSandbox();

  const receivedTrigger = new Promise(resolve => {
    sandbox
      .stub(ASRouter, "sendTriggerMessage")
      .callsFake(({ id, context }) => {
        if (
          id === "onSearch" &&
          context.isSuggestion === false &&
          context.isOneOff === false &&
          context.searchSource === "urlbar"
        ) {
          resolve(true);
        }
      });
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  const onLoaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let SEARCH_TERM = "test search term";
  gURLBar.value = SEARCH_TERM;
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoaded;

  let onSearch = await receivedTrigger;

  ok(onSearch, "onSearch trigger sent");

  sandbox.restore();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_selectableProfilesUpdated_remote_vs_local() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("selectableProfilesUpdated");
  trigger.uninit();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.discovery.enabled", false]],
  });

  trigger.init(handlerStub);

  // Send remote notification with tracked pref change, which should trigger firing
  Services.prefs.setBoolPref("browser.discovery.enabled", true);
  Services.obs.notifyObservers(null, "sps-profiles-updated", "remote");
  Assert.ok(
    handlerStub.calledOnce,
    "Fired on remote when tracked pref changed"
  );
  Assert.deepEqual(handlerStub.firstCall.args[1].param, { type: "remote" });

  // Send local notification with tracked pref change, which should not trigger firing
  handlerStub.resetHistory();
  Services.prefs.setBoolPref("browser.discovery.enabled", false);
  Services.obs.notifyObservers(null, "sps-profiles-updated", "local");
  Assert.ok(handlerStub.notCalled, "Did not fire on local");

  trigger.uninit();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_elementClicked_trigger() {
  const handlerStub = sinon.stub();
  const xulElButtonId = "PanelUI-menu-button";

  const buttonClickTrigger = ASRouterTriggerListeners.get("elementClicked");
  buttonClickTrigger.uninit();
  buttonClickTrigger.init(handlerStub, ["testButtonId", xulElButtonId]);

  // Test button click event
  const button = document.createElement("button");
  button.id = "testButtonId";
  // Button text content is required to prevent test failures
  button.textContent = "Test Button";
  document.documentElement.appendChild(button);

  await TestUtils.waitForTick();
  const clickEvent = new PointerEvent("click", {
    bubbles: true,
    cancelable: true,
    button: 0,
  });
  button.dispatchEvent(clickEvent);

  Assert.ok(handlerStub.calledOnce, "handler called once for click event");
  Assert.deepEqual(
    handlerStub.firstCall.args[1].param,
    { type: "testButtonId" },
    "handler called with correct param type for click event"
  );

  // Test space keypress event
  handlerStub.resetHistory();
  const spaceKeypressEvent = new KeyboardEvent("keypress", {
    bubbles: true,
    cancelable: true,
    key: " ",
    charCode: 32,
  });
  button.dispatchEvent(spaceKeypressEvent);

  Assert.ok(
    handlerStub.calledOnce,
    "handler called once for space keypress event"
  );
  Assert.deepEqual(
    handlerStub.firstCall.args[1].param,
    { type: "testButtonId" },
    "handler called with correct param type for space keypress event"
  );

  // Test Enter keypress event
  handlerStub.resetHistory();
  const enterKeypressEvent = new KeyboardEvent("keypress", {
    bubbles: true,
    cancelable: true,
    key: "Enter",
    charCode: 13,
  });
  button.dispatchEvent(enterKeypressEvent);

  Assert.ok(
    handlerStub.calledOnce,
    "handler called once for enter keypress event"
  );
  Assert.deepEqual(
    handlerStub.firstCall.args[1].param,
    { type: "testButtonId" },
    "handler called with correct param type for enter keypress event"
  );

  // Test a non-space && non-enter keypress event
  handlerStub.resetHistory();
  const tabKeypressEvent = new KeyboardEvent("keypress", {
    bubbles: true,
    cancelable: true,
    key: "Tab",
    charCode: 9,
  });
  button.dispatchEvent(tabKeypressEvent);

  Assert.ok(handlerStub.notCalled, "handler not called for tab keypress event");

  // Test a space keypress event on a xul element
  handlerStub.resetHistory();
  const xulElement = document.getElementById(xulElButtonId);
  xulElement.dispatchEvent(spaceKeypressEvent);

  Assert.ok(
    handlerStub.calledOnce,
    "handler called once for space keypress event"
  );
  Assert.deepEqual(
    handlerStub.firstCall.args[1].param,
    { type: xulElButtonId },
    "handler called with correct param type for space keypress event on xul element"
  );

  // Test a click event on a xul element
  handlerStub.resetHistory();
  xulElement.dispatchEvent(clickEvent);

  Assert.ok(handlerStub.calledOnce, "handler called once for click event");
  Assert.deepEqual(
    handlerStub.firstCall.args[1].param,
    { type: xulElButtonId },
    "handler called with correct param type for click keypress event on xul element"
  );

  buttonClickTrigger.uninit();
  document.documentElement.removeChild(button);
});

add_task(async function test_ipprotection_ready() {
  const sandbox = sinon.createSandbox();
  const receivedTrigger = new Promise(resolve => {
    sandbox.stub(ASRouter, "sendTriggerMessage").callsFake(({ id }) => {
      if (id === "ipProtectionReady") {
        resolve(true);
      }
    });
  });

  IPProtection.init();

  let ipProtectionReadyTrigger = await receivedTrigger;
  Assert.ok(ipProtectionReadyTrigger, "ipProtectionReady trigger sent");

  IPProtection.uninit();
  sandbox.restore();

  if (PanelUI.panel.state === "showing") {
    await BrowserTestUtils.waitForEvent(PanelUI.panel, "popupshown");
  }
  if (PanelUI.panel.state !== "closed") {
    let panelHidden = BrowserTestUtils.waitForEvent(
      PanelUI.panel,
      "popuphidden"
    );
    PanelUI.hide();
    await panelHidden;
  }
});

add_task(async function test_tabSwitch() {
  const sandbox = sinon.createSandbox();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.splitview.hasUsed", false]],
  });

  const receivedTrigger = new Promise(resolve => {
    sandbox.stub(ASRouter, "sendTriggerMessage").callsFake(triggerData => {
      if (triggerData.id === "tabSwitch") {
        resolve(triggerData);
      }
    });
  });

  let tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  let tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.org"
  );

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await BrowserTestUtils.switchTab(gBrowser, tab2);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  const triggerData = await receivedTrigger;
  Assert.ok(
    ASRouter.sendTriggerMessage.calledWith(sinon.match({ id: "tabSwitch" })),
    "tabSwitch trigger sent after switching between tabs 3 times"
  );
  Assert.ok(
    triggerData.context &&
      typeof triggerData.context.currentTabsOpen === "number",
    "tabSwitch trigger includes currentTabsOpen in context"
  );
  Assert.equal(triggerData.context.currentTabsOpen, 3, "currentTabsOpen is 3");

  ASRouter.sendTriggerMessage.resetHistory();

  let aboutTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await BrowserTestUtils.switchTab(gBrowser, aboutTab);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  await sleepMs(100);
  Assert.ok(
    !ASRouter.sendTriggerMessage.calledWith(sinon.match({ id: "tabSwitch" })),
    "tabSwitch trigger not sent when switching to/from about: pages"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(aboutTab);

  await SpecialPowers.popPrefEnv();
  sandbox.restore();
});

add_task(async function test_ipprotection_panel_closed() {
  const sandbox = sinon.createSandbox();
  const receivedTrigger = new Promise(resolve => {
    sandbox.stub(ASRouter, "sendTriggerMessage").callsFake(({ id }) => {
      if (id === "ipProtectionPanelClosed") {
        resolve(true);
      }
    });
  });

  IPProtection.init();

  // Get the panel for the window
  const panel = IPProtection.getPanel(window);

  // Open the panel
  await panel.open(window);

  // Close the panel, which should trigger ipProtectionPanelClosed
  let panelHidden = BrowserTestUtils.waitForEvent(panel.panel, "popuphidden");
  panel.close();
  await panelHidden;

  Assert.ok(
    await receivedTrigger,
    "ipProtectionPanelClosed trigger sent on closing IP Protection panel"
  );

  // Open and close the panel again
  await panel.open(window);
  panelHidden = BrowserTestUtils.waitForEvent(panel.panel, "popuphidden");
  panel.close();
  await panelHidden;

  // Clean up prefs
  Services.prefs.clearUserPref("browser.ipProtection.added");
  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");

  IPProtection.uninit();
  await SpecialPowers.popPrefEnv();
  sandbox.restore();
});

add_task(async function test_ipprotection_bandwidth_reset() {
  const sandbox = sinon.createSandbox();
  const receivedTrigger = new Promise(resolve => {
    sandbox.stub(ASRouter, "sendTriggerMessage").callsFake(({ id }) => {
      if (id === "ipProtectionBandwidthReset") {
        resolve(true);
      }
    });
  });

  IPProtection.init();
  IPProtection.getPanel(window);

  const oldResetDate = new Date(Date.now() - 86400000).toISOString();
  const newResetDate = new Date(Date.now() + 86400000).toISOString();
  const max = "5368709120";

  Services.prefs.setStringPref(
    "browser.ipProtection.bandwidthResetDate",
    oldResetDate
  );

  const usage = new ProxyUsage(max, max, newResetDate);
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:UsageChanged", {
      bubbles: true,
      composed: true,
      detail: { usage },
    })
  );

  Assert.ok(
    await receivedTrigger,
    "ipProtectionBandwidthReset trigger sent when bandwidth resets"
  );

  Services.prefs.clearUserPref("browser.ipProtection.bandwidthResetDate");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");

  IPProtection.uninit();
  sandbox.restore();
});

add_task(async function test_userBookmarkFolderActivity_folderInsert() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
  trigger.uninit();
  trigger.init(handlerStub);

  const folder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "user folder",
  });

  Assert.ok(handlerStub.calledOnce, "Handler called once for folder insert");
  Assert.deepEqual(
    handlerStub.firstCall.args[1],
    { id: "userBookmarkFolderActivity" },
    "Fires userBookmarkFolderActivity trigger"
  );

  handlerStub.resetHistory();
  trigger.uninit();
  await PlacesUtils.bookmarks.remove(folder.guid);
});

add_task(async function test_userBookmarkFolderActivity_bookmarkInUserFolder() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
  trigger.uninit();
  trigger.init(handlerStub);

  const userFolder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "user folder",
  });
  handlerStub.resetHistory();

  await PlacesUtils.bookmarks.insert({
    parentGuid: userFolder.guid,
    url: "https://example.com/",
    title: "nested bookmark",
  });

  Assert.ok(handlerStub.calledOnce, "Handler called once for nested bookmark");
  Assert.deepEqual(
    handlerStub.firstCall.args[1],
    { id: "userBookmarkFolderActivity" },
    "Fires userBookmarkFolderActivity trigger when parent is user-created"
  );

  handlerStub.resetHistory();
  trigger.uninit();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(
  async function test_userBookmarkFolderActivity_bookmarkInBuiltInFolder() {
    const handlerStub = sinon.stub();
    const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
    trigger.uninit();
    trigger.init(handlerStub);

    for (const parentGuid of [
      PlacesUtils.bookmarks.unfiledGuid,
      PlacesUtils.bookmarks.menuGuid,
      PlacesUtils.bookmarks.toolbarGuid,
      PlacesUtils.bookmarks.mobileGuid,
    ]) {
      handlerStub.resetHistory();
      const bm = await PlacesUtils.bookmarks.insert({
        parentGuid,
        url: "https://example.com/",
        title: "top-level bookmark",
      });
      Assert.ok(
        handlerStub.notCalled,
        `Handler not called when bookmark added to built-in folder ${parentGuid}`
      );
      await PlacesUtils.bookmarks.remove(bm.guid);
    }

    trigger.uninit();
  }
);

add_task(async function test_userBookmarkFolderActivity_uninit() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
  trigger.uninit();
  trigger.init(handlerStub);
  trigger.uninit();

  const folder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "post-uninit folder",
  });

  Assert.ok(handlerStub.notCalled, "Handler not called after uninit");
  await PlacesUtils.bookmarks.remove(folder.guid);
});

add_task(async function test_userBookmarkFolderActivity_privateWindow() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
  trigger.uninit();
  trigger.init(handlerStub);

  const privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  Assert.ok(
    PrivateBrowsingUtils.isWindowPrivate(privateWin),
    "Most-recent window is private"
  );

  const folder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "folder while private",
  });
  Assert.ok(
    handlerStub.notCalled,
    "Handler not called for folder insert while private window is active"
  );

  const bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: folder.guid,
    url: "https://example.com/",
    title: "bookmark while private",
  });
  Assert.ok(
    handlerStub.notCalled,
    "Handler not called for bookmark-in-user-folder while private window is active"
  );

  await PlacesUtils.bookmarks.remove(bookmark.guid);
  await PlacesUtils.bookmarks.remove(folder.guid);
  await BrowserTestUtils.closeWindow(privateWin);
  trigger.uninit();
});

add_task(async function test_userBookmarkFolderActivity_tagging() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
  trigger.uninit();
  trigger.init(handlerStub);

  const bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "https://example.com/tagged",
    title: "to be tagged",
  });
  handlerStub.resetHistory();

  PlacesUtils.tagging.tagURI(Services.io.newURI(bookmark.url), ["sample-tag"]);

  Assert.ok(
    handlerStub.notCalled,
    "Handler not called when the user tags a URL"
  );

  PlacesUtils.tagging.untagURI(Services.io.newURI(bookmark.url), [
    "sample-tag",
  ]);
  await PlacesUtils.bookmarks.remove(bookmark.guid);
  trigger.uninit();
});

add_task(async function test_userBookmarkFolderActivity_nonDefaultSource() {
  const handlerStub = sinon.stub();
  const trigger = ASRouterTriggerListeners.get("userBookmarkFolderActivity");
  trigger.uninit();
  trigger.init(handlerStub);

  const folder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "sync-source folder",
    source: PlacesUtils.bookmarks.SOURCES.SYNC,
  });

  Assert.ok(
    handlerStub.notCalled,
    "Handler not called for folder insert with non-default source"
  );

  await PlacesUtils.bookmarks.remove(folder.guid, {
    source: PlacesUtils.bookmarks.SOURCES.SYNC,
  });
  trigger.uninit();
});

/**
 * Sets up the `splitViewUsed` trigger for a test, creates an initial Split
 * View (`tab1` + `tab2`) before calling `testFn`, and tears everything down
 * afterward, even if `testFn` throws.
 *
 * By the time `testFn` runs, the initial `TabSplitViewActivate` has already
 * fired.
 *
 * @param {number} delayMs Value for browser.tabs.splitview.trigger.delay_ms
 *   for this test, so tests don't have to wait out the real 15s default.
 * @param {Function} testFn Called with:
 *   - handlerStub: sinon stub standing in for ASRouter's trigger handler.
 *   - trigger: the `splitViewUsed` trigger listener instance.
 *   - tab1, tab2: the two tabs already in the initial Split View.
 *   - splitview: the initial Split View wrapper.
 *   - trackTab(tab): registers an extra tab opened by `testFn` for cleanup.
 *   - createSplitView(tabs): creates and registers an additional Split View for cleanup.
 */
async function withSplitViewUsedTrigger(delayMs, testFn) {
  const sandbox = sinon.createSandbox();
  const handlerStub = sandbox.stub();
  const trigger = ASRouterTriggerListeners.get("splitViewUsed");
  const extraTabs = [];
  const extraSplitviews = [];

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.splitview.trigger.delay_ms", delayMs],
      ["browser.tabs.splitview.trigger.createCount", 0],
    ],
  });

  trigger.uninit();
  trigger.init(handlerStub);

  let splitview;
  let tab1;
  let tab2;
  try {
    tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");
    tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
    splitview = gBrowser.addTabSplitView([tab1, tab2]);

    await testFn({
      handlerStub,
      trigger,
      tab1,
      tab2,
      splitview,
      // Tabs opened by testFn that need cleanup even if an assertion throws.
      trackTab(tab) {
        extraTabs.push(tab);
        return tab;
      },
      // Creates an additional, distinct Split View that also needs cleanup.
      createSplitView(tabs) {
        const sv = gBrowser.addTabSplitView(tabs);
        extraSplitviews.push(sv);
        return sv;
      },
    });
  } finally {
    trigger.uninit();
    for (const sv of [...extraSplitviews, splitview]) {
      sv?.unsplitTabs();
    }
    for (const tab of [...extraTabs, tab1, tab2]) {
      if (tab?.isConnected) {
        BrowserTestUtils.removeTab(tab);
      }
    }
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
  }
}

// Verifies the trigger fires after the configured delay and reports a
// create count of 1 for the initial Split View.
add_task(async function test_splitViewUsed_fires_after_delay() {
  await withSplitViewUsedTrigger(10, async ({ handlerStub }) => {
    await TestUtils.waitForCondition(
      () => handlerStub.calledOnce,
      "Waiting for splitViewUsed to fire"
    );

    const [, trigger] = handlerStub.firstCall.args;
    Assert.equal(trigger.id, "splitViewUsed", "Fired with the trigger id");
    Assert.equal(
      trigger.context.splitViewCreateCount,
      1,
      "First Split View created has create count 1"
    );
  });
});

// Verifies leaving Split View before the delay cancels the trigger without
// affecting the create count.
add_task(async function test_splitViewUsed_cancelled_on_deactivate() {
  await withSplitViewUsedTrigger(50, async ({ handlerStub, trackTab }) => {
    trackTab(
      await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank")
    );

    await sleepMs(100);
    Assert.ok(
      handlerStub.notCalled,
      "splitViewUsed trigger cancelled when Split View deactivated before delay"
    );
    Assert.equal(
      Services.prefs.getIntPref("browser.tabs.splitview.trigger.createCount"),
      1,
      "Split View creation remains counted when the active-use timer is cancelled"
    );
  });
});

// Verifies switching between tabs in the same Split View does not fire the
// trigger again or increment the create count.
add_task(async function test_splitViewUsed_no_refire_within_same_visit() {
  await withSplitViewUsedTrigger(10, async ({ handlerStub, tab1, tab2 }) => {
    await TestUtils.waitForCondition(
      () => handlerStub.calledOnce,
      "Waiting for splitViewUsed to fire"
    );

    await BrowserTestUtils.switchTab(gBrowser, tab2);
    await BrowserTestUtils.switchTab(gBrowser, tab1);
    await sleepMs(50);

    Assert.ok(
      handlerStub.calledOnce,
      "Still fired only once after switching between Split View tabs"
    );
    Assert.equal(
      Services.prefs.getIntPref("browser.tabs.splitview.trigger.createCount"),
      1,
      "Create count unaffected by the intra-visit tab switch"
    );
  });
});

// Verifies returning to the same Split View starts a new active-use visit
// without incrementing the create count.
add_task(
  async function test_splitViewUsed_refires_on_return_without_incrementing_createCount() {
    await withSplitViewUsedTrigger(
      10,
      async ({ handlerStub, tab1, trackTab }) => {
        await TestUtils.waitForCondition(
          () => handlerStub.calledOnce,
          "Waiting for splitViewUsed to fire on the first visit"
        );

        trackTab(
          await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank")
        );
        await BrowserTestUtils.switchTab(gBrowser, tab1);

        await TestUtils.waitForCondition(
          () => handlerStub.calledTwice,
          "Waiting for splitViewUsed to fire on the return visit"
        );

        Assert.equal(
          handlerStub.callCount,
          2,
          "Trigger fires once for each continuous active-use period"
        );
        const [, trigger] = handlerStub.secondCall.args;
        Assert.equal(
          trigger.context.splitViewCreateCount,
          1,
          "Returning to the same Split View does not increment create count"
        );
      }
    );
  }
);

// Verifies creating a second, distinct Split View fires the trigger again
// and increments the create count.
add_task(
  async function test_splitViewUsed_createCount_increments_on_new_split_view() {
    await withSplitViewUsedTrigger(
      10,
      async ({ handlerStub, trackTab, createSplitView }) => {
        await TestUtils.waitForCondition(
          () => handlerStub.calledOnce,
          "Waiting for splitViewUsed to fire on the first Split View"
        );

        const tab3 = trackTab(
          await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank")
        );
        const tab4 = trackTab(BrowserTestUtils.addTab(gBrowser, "about:blank"));
        createSplitView([tab3, tab4]);

        await TestUtils.waitForCondition(
          () => handlerStub.calledTwice,
          "Waiting for splitViewUsed to fire on the second Split View"
        );

        const [, trigger] = handlerStub.secondCall.args;
        Assert.equal(
          trigger.context.splitViewCreateCount,
          2,
          "Creating a second Split View increments the create count"
        );
      }
    );
  }
);

// Verifies uninitializing the trigger cancels a pending timer.
add_task(async function test_splitViewUsed_uninit_cancels_pending_timer() {
  await withSplitViewUsedTrigger(50, async ({ handlerStub, trigger }) => {
    trigger.uninit();
    await sleepMs(100);

    Assert.ok(
      handlerStub.notCalled,
      "splitViewUsed trigger does not fire after uninit even if a timer was pending"
    );
  });
});
