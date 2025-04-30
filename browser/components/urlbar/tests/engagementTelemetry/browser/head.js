/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser/head-common.js",
  this
);

ChromeUtils.defineESModuleGetters(this, {
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "QuickSuggestTestUtils", () => {
  const { QuickSuggestTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/QuickSuggestTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "MerinoTestUtils", () => {
  const { MerinoTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/MerinoTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineESModuleGetters(lazy, {
  UrlbarTestUtils: "resource://testing-common/UrlbarTestUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "PlacesFrecencyRecalculator", () => {
  return Cc["@mozilla.org/places/frecency-recalculator;1"].getService(
    Ci.nsIObserver
  ).wrappedJSObject;
});

async function addTopSites(url) {
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits(url);
  }
  await updateTopSites(sites => {
    return sites && sites[0] && sites[0].url == url;
  });
}

function assertAbandonmentTelemetry(expectedExtraList) {
  assertGleanTelemetry("abandonment", expectedExtraList);
}

function assertEngagementTelemetry(expectedExtraList) {
  assertGleanTelemetry("engagement", expectedExtraList);
}

function assertExposureTelemetry(expectedExtraList) {
  assertGleanTelemetry("exposure", expectedExtraList);
}

function assertGleanTelemetry(telemetryName, expectedExtraList) {
  const camelName = telemetryName.replaceAll(/_(.)/g, (match, p1) =>
    p1.toUpperCase()
  );
  const telemetries = Glean.urlbar[camelName].testGetValue() ?? [];
  info(
    "Asserting Glean telemetry is correct, actual events are: " +
      JSON.stringify(telemetries)
  );
  Assert.equal(
    telemetries.length,
    expectedExtraList.length,
    "Telemetry event length matches expected event length."
  );

  for (let i = 0; i < telemetries.length; i++) {
    const telemetry = telemetries[i];
    Assert.equal(telemetry.category, "urlbar");
    Assert.equal(telemetry.name, telemetryName);

    const expectedExtra = expectedExtraList[i];
    for (const key of Object.keys(expectedExtra)) {
      Assert.equal(
        telemetry.extra[key],
        expectedExtra[key],
        `${key} is correct`
      );
    }
  }
}

async function ensureQuickSuggestInit({ ...args } = {}) {
  return lazy.QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        collection: lazy.QuickSuggestTestUtils.RS_COLLECTION.AMP,
        type: lazy.QuickSuggestTestUtils.RS_TYPE.AMP,
        attachment: [
          lazy.QuickSuggestTestUtils.ampRemoteSettings({
            keywords: ["amp", "amp and wikipedia"],
          }),
        ],
      },
      {
        collection: lazy.QuickSuggestTestUtils.RS_COLLECTION.OTHER,
        type: lazy.QuickSuggestTestUtils.RS_TYPE.WIKIPEDIA,
        attachment: [
          lazy.QuickSuggestTestUtils.wikipediaRemoteSettings({
            keywords: ["wikipedia", "amp and wikipedia"],
          }),
        ],
      },
      lazy.QuickSuggestTestUtils.weatherRecord(),
      {
        type: "exposure-suggestions",
        suggestion_type: "aaa",
        attachment: {
          keywords: ["aaa keyword"],
        },
      },
    ],
    ...args,
  });
}

async function doBlur() {
  await UrlbarTestUtils.promisePopupClose(window, () => {
    gURLBar.blur();
  });
}

async function doClick() {
  const selected = UrlbarTestUtils.getSelectedRow(window);
  const onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.synthesizeMouseAtCenter(selected, {});
  await onLoad;
}

async function doClickSubButton(selector) {
  const selected = UrlbarTestUtils.getSelectedElement(window);
  const button = selected.closest(".urlbarView-row").querySelector(selector);
  EventUtils.synthesizeMouseAtCenter(button, {});
}

async function doDropAndGo(data) {
  const onLoad = BrowserTestUtils.browserLoaded(browser);
  EventUtils.synthesizeDrop(
    document.getElementById("back-button"),
    gURLBar.inputField,
    [[{ type: "text/plain", data }]],
    "copy",
    window
  );
  await onLoad;
}

async function doEnter(modifier = {}) {
  const onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.synthesizeKey("KEY_Enter", modifier);
  await onLoad;
}

async function doPaste(data) {
  await SimpleTest.promiseClipboardChange(data, () => {
    clipboardHelper.copyString(data);
  });

  gURLBar.focus();
  gURLBar.select();
  document.commandDispatcher
    .getControllerForCommand("cmd_paste")
    .doCommand("cmd_paste");
  await UrlbarTestUtils.promiseSearchComplete(window);
}

async function doPasteAndGo(data) {
  await SimpleTest.promiseClipboardChange(data, () => {
    clipboardHelper.copyString(data);
  });
  const inputBox = gURLBar.querySelector("moz-input-box");
  const contextMenu = inputBox.menupopup;
  const onPopup = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {
    type: "contextmenu",
    button: 2,
  });
  await onPopup;
  const onLoad = BrowserTestUtils.browserLoaded(browser);
  const menuitem = inputBox.getMenuItem("paste-and-go");
  contextMenu.activateItem(menuitem);
  await onLoad;
}

async function doTest(testFn) {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  gURLBar.controller.engagementEvent.reset();
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesTestUtils.clearHistoryVisits();
  await PlacesTestUtils.clearInputHistory();
  await UrlbarTestUtils.formHistory.clear(window);
  await QuickSuggest.clearDismissedSuggestions();
  await updateTopSites(() => true);
  await BrowserTestUtils.withNewTab(gBrowser, testFn);
}

async function initGroupTest() {
  /* import-globals-from head-groups.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-groups.js",
    this
  );
  await setup();
}

async function initInteractionTest() {
  /* import-globals-from head-interaction.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-interaction.js",
    this
  );
  await setup();
}

async function initNCharsAndNWordsTest() {
  /* import-globals-from head-n_chars_n_words.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-n_chars_n_words.js",
    this
  );
  await setup();
}

async function initSapTest() {
  /* import-globals-from head-sap.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-sap.js",
    this
  );
  await setup();
}

async function initSearchEngineDefaultIdTest() {
  /* import-globals-from head-search_engine_default_id.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-search_engine_default_id.js",
    this
  );
  await setup();
}

async function initSearchModeTest() {
  /* import-globals-from head-search_mode.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-search_mode.js",
    this
  );
  await setup();
}

async function initExposureTest() {
  /* import-globals-from head-exposure.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/engagementTelemetry/browser/head-exposure.js",
    this
  );
  await setup();
}

function loadOmniboxAddon({ keyword }) {
  return ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
      omnibox: {
        keyword,
      },
    },
    background() {
      /* global browser */
      browser.omnibox.setDefaultSuggestion({
        description: "doit",
      });
      browser.omnibox.onInputEntered.addListener(() => {
        browser.tabs.update({ url: "https://example.com/" });
      });
      browser.omnibox.onInputChanged.addListener((text, suggest) => {
        suggest([]);
      });
    },
  });
}

async function loadRemoteTab(url) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", false],
      ["browser.urlbar.maxHistoricalSearchSuggestions", 0],
      ["browser.urlbar.autoFill", false],
      ["services.sync.username", "fake"],
    ],
  });

  const REMOTE_TAB = {
    id: "test",
    type: "client",
    lastModified: 1492201200,
    name: "test",
    clientType: "desktop",
    tabs: [
      {
        type: "tab",
        title: "tesrt",
        url,
        icon: UrlbarUtils.ICON.DEFAULT,
        client: "test",
        lastUsed: Math.floor(Date.now() / 1000),
      },
    ],
  };

  const sandbox = lazy.sinon.createSandbox();
  // eslint-disable-next-line no-undef
  const syncedTabs = SyncedTabs;
  const originalSyncedTabsInternal = syncedTabs._internal;
  syncedTabs._internal = {
    isConfiguredToSyncTabs: true,
    hasSyncedThisSession: true,
    getTabClients() {
      return Promise.resolve([]);
    },
    syncTabs() {
      return Promise.resolve();
    },
  };
  const weaveXPCService = Cc["@mozilla.org/weave/service;1"].getService(
    Ci.nsISupports
  ).wrappedJSObject;
  const oldWeaveServiceReady = weaveXPCService.ready;
  weaveXPCService.ready = true;
  sandbox
    .stub(syncedTabs._internal, "getTabClients")
    .callsFake(() => Promise.resolve(Cu.cloneInto([REMOTE_TAB], {})));

  return {
    async unload() {
      sandbox.restore();
      weaveXPCService.ready = oldWeaveServiceReady;
      syncedTabs._internal = originalSyncedTabsInternal;
      // Reset internal cache in UrlbarProviderRemoteTabs.
      Services.obs.notifyObservers(null, "weave:engine:sync:finish", "tabs");
      await SpecialPowers.popPrefEnv();
    },
  };
}

async function openPopup(input) {
  await UrlbarTestUtils.promisePopupOpen(window, async () => {
    await UrlbarTestUtils.inputIntoURLBar(window, input);
  });
  await UrlbarTestUtils.promiseSearchComplete(window);
}

async function selectRowByURL(url) {
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    const detail = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (detail.url === url) {
      UrlbarTestUtils.setSelectedRowIndex(window, i);
      return;
    }
  }
}

async function selectRowByProvider(provider) {
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    const detail = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (detail.result.providerName === provider) {
      UrlbarTestUtils.setSelectedRowIndex(window, i);
      break;
    }
  }
}

async function selectRowByType(type) {
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    const detail = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (detail.result.payload.type === type) {
      UrlbarTestUtils.setSelectedRowIndex(window, i);
      return;
    }
  }
}

async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.searchEngagementTelemetry.enabled", true],
      ["browser.urlbar.quickactions.enabled", true],
      ["browser.urlbar.secondaryActions.featureGate", true],
      ["browser.urlbar.scotchBonnet.enableOverride", false],
    ],
  });

  const engine = await SearchTestUtils.installOpenSearchEngine({
    url: "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser/searchSuggestionEngine.xml",
  });
  const originalDefaultEngine = await Services.search.getDefault();
  await Services.search.setDefault(
    engine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
  await Services.search.moveEngine(engine, 0);

  registerCleanupFunction(async function () {
    await SpecialPowers.popPrefEnv();
    await Services.search.setDefault(
      originalDefaultEngine,
      Ci.nsISearchService.CHANGE_REASON_UNKNOWN
    );
  });
}

async function setupNimbus(variables) {
  return lazy.UrlbarTestUtils.initNimbusFeature(variables);
}

async function showResultByArrowDown() {
  gURLBar.value = "";
  gURLBar.select();
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeKey("KEY_ArrowDown");
  });
  await UrlbarTestUtils.promiseSearchComplete(window);
}

async function expectNoConsoleErrors(task) {
  let endConsoleListening = TestUtils.listenForConsoleMessages();
  let msgs;
  let taskResult;

  try {
    taskResult = await task();
  } finally {
    msgs = await endConsoleListening();
  }

  for (let msg of msgs) {
    if (msg.level === "error") {
      throw new Error(`Console error detected: ${msg.arguments[0]}`);
    }
  }

  return taskResult;
}
