/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser/head-common.js",
  this
);

ChromeUtils.defineESModuleGetters(this, {
  CustomizableUITestUtils:
    "resource://testing-common/CustomizableUITestUtils.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
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

ChromeUtils.defineLazyGetter(this, "GeolocationTestUtils", () => {
  const { GeolocationTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/GeolocationTestUtils.sys.mjs"
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
        type: "dynamic-suggestions",
        suggestion_type: "test-exposure-aaa",
        score: 1.0,
        attachment: [
          {
            keywords: ["aaa keyword"],
            data: {
              result: {
                isHiddenExposure: true,
              },
            },
          },
        ],
      },
      {
        type: "dynamic-suggestions",
        suggestion_type: "important_dates",
        score: 1.0,
        attachment: [
          {
            keywords: ["important dates"],
            data: {
              result: {
                payload: {
                  dates: [
                    // These dates are always in the future, so that we will
                    // always get a match in the test when we expect one.
                    Temporal.Now.plainDateISO().add({ months: 1 }).toString(),
                    Temporal.Now.plainDateISO().add({ years: 1 }).toString(),
                  ],
                  name: "Event 1",
                },
              },
            },
          },
        ],
      },
    ],
    ...args,
  });
}

async function doBlur(testUtils = UrlbarTestUtils, win = window) {
  await testUtils.promisePopupClose(win, () => {
    testUtils.getUrlbar(win).blur();
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

async function doEnter(modifier = {}, win = window) {
  const onLoad = BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);
  EventUtils.synthesizeKey("KEY_Enter", modifier, win);
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
  const contextMenu = window.EditContextMenu.popup;
  const onPopup = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {
    type: "contextmenu",
    button: 2,
  });
  await onPopup;
  const onLoad = BrowserTestUtils.browserLoaded(browser);
  const menuitem = contextMenu.querySelector(
    '[anonid="paste-and-go"]:not([hidden])'
  );
  contextMenu.activateItem(menuitem);
  await onLoad;
}

async function doTest(testFn) {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  gURLBar.controller.session.reset();
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
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-groups.js",
    this
  );
  await setup();
}

async function initInteractionTest() {
  /* import-globals-from head-interaction.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-interaction.js",
    this
  );
  await setup();
}

async function initNCharsAndNWordsTest() {
  /* import-globals-from head-n_chars_n_words.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-n_chars_n_words.js",
    this
  );
  await setup();
}

async function initSapTest() {
  /* import-globals-from head-sap.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-sap.js",
    this
  );
  await setup();
}

async function initSearchEngineDefaultIdTest() {
  /* import-globals-from head-search_engine_default_id.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-search_engine_default_id.js",
    this
  );
  await setup();
}

async function initSearchModeTest() {
  /* import-globals-from head-search_mode.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-search_mode.js",
    this
  );
  await setup();
}

async function initExposureTest() {
  /* import-globals-from head-exposure.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-engagementTelemetry/head-exposure.js",
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
        icon: UrlbarShared.ICON.DEFAULT,
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

async function openPopup(input, testUtils = UrlbarTestUtils, win = window) {
  await testUtils.promisePopupOpen(win, async () => {
    await testUtils.inputIntoURLBar(win, input);
  });
  await testUtils.promiseSearchComplete(win);
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
  const originalDefaultEngine = await SearchService.getDefault();
  await SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);
  await SearchService.moveEngine(engine, 0);

  registerCleanupFunction(async function () {
    // Tests verify that no prefs have been changed so clear any
    // so clear any prefs we may have touched while running tests.
    let prefs = [
      "services.sync.lastTabFetch",
      "services.settings.clock_skew_seconds",
      "services.settings.last_update_seconds",
      "services.settings.last_etag",
      "browser.urlbar.recentsearches.lastDefaultChanged",
      "browser.search.totalSearches",
      "browser.urlbar.events.bounce.maxSecondsFromLastSearch",
    ];
    prefs.forEach(pref => Services.prefs.clearUserPref(pref));
    await SpecialPowers.popPrefEnv();
    await SearchService.setDefault(
      originalDefaultEngine,
      SearchService.CHANGE_REASON.UNKNOWN
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

/**
 * Helper to run a semantic history test.
 *
 * @param {Array<UrlbarResult>} results array of results to return
 * @param {Function} task async function to wrap
 * @param {number} [embeddingSize] size of embeddings
 */
async function doTestWithSemantic(results, task, embeddingSize = 16) {
  /**
   * A mock object that pretends to be an MLEngine.
   */
  class MockMLEngine {
    async run(request) {
      const texts = request.args[0];
      return texts.map(text => {
        if (typeof text !== "string" || text.trim() === "") {
          throw new Error("Invalid input: text must be a non-empty string");
        }
        // Return a mock embedding vector (e.g., an array of zeros)
        return Array(embeddingSize).fill(0);
      });
    }
  }
  const { getPlacesSemanticHistoryManager } = ChromeUtils.importESModule(
    "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
  );
  let semanticManager = getPlacesSemanticHistoryManager();
  let canUseSemanticStub = sinon.stub(semanticManager, "canUseSemanticSearch");
  canUseSemanticStub.get(() => true);
  let hasSufficientEntriesStub = sinon
    .stub(semanticManager, "hasSufficientEntriesForSearching")
    .resolves(true);
  semanticManager.embedder.setEngine(new MockMLEngine());
  let inferStub = sinon.stub(semanticManager, "infer").resolves({ results });
  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.featureGate", true]],
  });
  try {
    await doTest(task);
  } finally {
    await SpecialPowers.popPrefEnv();
    hasSufficientEntriesStub.restore();
    canUseSemanticStub.restore();
    inferStub.restore();
  }
}
