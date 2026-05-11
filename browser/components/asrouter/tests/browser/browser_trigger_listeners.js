/* eslint-disable @microsoft/sdl/no-insecure-url */
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

async function test_formAutofillTrigger(settingsRedesignEnabled) {
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

  await SpecialPowers.pushPrefEnv({
    set: [["browser.settings-redesign.enabled", settingsRedesignEnabled]],
  });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async browser => {
      const redesignEnabled = Services.prefs.getBoolPref(
        "browser.settings-redesign.enabled",
        false
      );
      if (!redesignEnabled) {
        await SpecialPowers.spawn(browser, [], async () =>
          (
            await ContentTaskUtils.waitForCondition(
              () =>
                content.document.querySelector(
                  "setting-group[groupid=payments] #savedPaymentsButton"
                ),
              "Waiting for credit card manager button"
            )
          )?.click()
        );
        await BrowserTestUtils.waitForCondition(
          () => browser.contentWindow?.gSubDialog?.dialogs.length
        );
      } else {
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
      }

      notifyCreditCardSaved();
      await sleepMs(1);

      if (!redesignEnabled) {
        Assert.ok(
          handlerStub.notCalled,
          "Not called when credit card manager is open"
        );
      }
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

add_task(async function test_cookieBannerDetected() {
  const handlerStub = sinon.stub();
  const bannerDetectedTrigger = ASRouterTriggerListeners.get(
    "cookieBannerDetected"
  );
  bannerDetectedTrigger.uninit();
  bannerDetectedTrigger.init(handlerStub);

  const win = await BrowserTestUtils.openNewBrowserWindow();
  let eventWait = BrowserTestUtils.waitForEvent(win, "cookiebannerdetected");
  win.dispatchEvent(new Event("cookiebannerdetected"));
  await eventWait;
  let closeWindow = BrowserTestUtils.closeWindow(win);

  Assert.ok(
    handlerStub.called,
    "Called after `cookiebannerdetected` event fires"
  );

  handlerStub.resetHistory();
  bannerDetectedTrigger.uninit();

  Assert.ok(handlerStub.notCalled, "Not called after uninit");
  await closeWindow;
});

add_task(async function test_cookieBannerHandled() {
  const handlerStub = sinon.stub();
  const bannerHandledTrigger = ASRouterTriggerListeners.get(
    "cookieBannerHandled"
  );
  bannerHandledTrigger.uninit();
  bannerHandledTrigger.init(handlerStub);

  const win = await BrowserTestUtils.openNewBrowserWindow();
  win.focus();
  let eventWait = BrowserTestUtils.waitForEvent(win, "cookiebannerhandled");
  win.windowUtils.dispatchEventToChromeOnly(
    win,
    new CustomEvent("cookiebannerhandled", {
      bubbles: true,
      cancelable: false,
      detail: {
        windowContext: {
          rootFrameLoader: { ownerElement: win.gBrowser.selectedBrowser },
        },
      },
    })
  );
  await eventWait;
  let closeWindow = BrowserTestUtils.closeWindow(win);

  Assert.ok(
    handlerStub.called,
    "Called after `cookiebannerhandled` event fires"
  );

  handlerStub.resetHistory();
  bannerHandledTrigger.uninit();

  Assert.ok(handlerStub.notCalled, "Not called after uninit");
  await closeWindow;
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
  await BrowserTestUtils.waitForCondition(
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
  await test_formAutofillTrigger(false);
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
  panel.close();

  Assert.ok(
    await receivedTrigger,
    "ipProtectionPanelClosed trigger sent on closing IP Protection panel"
  );

  // Open and close the panel again
  await panel.open(window);
  panel.close();

  await TestUtils.waitForTick();

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
