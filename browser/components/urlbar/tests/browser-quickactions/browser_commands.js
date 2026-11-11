/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test QuickActions.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AboutAddonsTestUtils:
    "resource://testing-common/AboutAddonsTestUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
});

async function setupLabsTest() {
  NimbusTestUtils.init({ Assert });
  await SpecialPowers.pushPrefEnv({
    set: [
      ["app.normandy.run_interval_seconds", 0],
      ["app.shield.optoutstudies.enabled", true],
      ["datareporting.healthreport.uploadEnabled", true],
    ],
    clear: [
      ["browser.preferences.experimental"],
      ["browser.preferences.experimental.hidden"],
    ],
  });
  await ExperimentAPI.ready();
  await ExperimentAPI._rsLoader.finishedUpdating();

  const recipes = [
    NimbusTestUtils.factories.recipe("nimbus-qa-1", {
      targeting: "true",
      isRollout: true,
      isFirefoxLabsOptIn: true,
      firefoxLabsTitle: "experimental-features-ime-search",
      firefoxLabsDescription: "experimental-features-ime-search-description",
      firefoxLabsDescriptionLinks: null,
      firefoxLabsGroup: "experimental-features-group-customize-browsing",
      requiresRestart: false,
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            { featureId: "nimbus-qa-1", value: { value: "recipe-value-1" } },
          ],
        },
      ],
    }),
  ];

  await ExperimentAPI._rsLoader.remoteSettingsClients.experiments.db.importChanges(
    {},
    Date.now(),
    recipes,
    { clear: true }
  );
  await ExperimentAPI._rsLoader.remoteSettingsClients.secureExperiments.db.importChanges(
    {},
    Date.now(),
    [],
    { clear: true }
  );
  await ExperimentAPI._rsLoader.updateRecipes("test");

  return async function cleanup() {
    await NimbusTestUtils.assert.storeIsEmpty();
    await SpecialPowers.popPrefEnv();
  };
}

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.quickactions.enabled", true],
      ["browser.urlbar.secondaryActions.featureGate", true],
      ["browser.preferences.experimental.hidden", false],
    ],
  });

  registerCleanupFunction(NimbusTestUtils.disableSignatureVerification());
});

const LOAD_TYPE = {
  CURRENT_TAB: 1,
  NEW_TAB: 2,
  PRE_LOADED: 3,
};

async function loadInCurrentTab(uri) {
  const onLoad = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    uri
  );
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, uri);
  await onLoad;
}

let COMMANDS_TESTS = [
  {
    cmd: "open view",
    uri: "about:firefoxview",
    loadType: LOAD_TYPE.PRE_LOADED,
    testFun: async () => {
      await TestUtils.waitForCondition(() => {
        return (
          window.gBrowser.selectedBrowser.currentURI.spec == "about:firefoxview"
        );
      });
      return true;
    },
  },
  {
    cmd: "labs",
    uri: "about:preferences#experimental",
    loadType: LOAD_TYPE.PRE_LOADED,
    setup: async () => {
      const cleanup = await setupLabsTest();
      registerCleanupFunction(cleanup);
    },
    testFun: async () => {
      await TestUtils.waitForCondition(() => {
        return (
          window.gBrowser.selectedBrowser.currentURI.spec ==
          "about:preferences#experimental"
        );
      });
      return true;
    },
  },
  {
    cmd: "add-ons",
    uri: "about:addons",
    // AboutAddonsTestUtils.isCategoryButtonSelected is actually synchronous
    // but we are leaving testFun as an async function so that the caller can
    // assume all entries' testFun function to be always returning a promise.
    testFun: async () =>
      AboutAddonsTestUtils.isCategoryButtonSelected(
        gBrowser.selectedBrowser.contentWindow,
        "discover"
      ),
  },
  {
    cmd: "extensions",
    uri: "about:addons",
    numTabPress: 2,
    testFun: async () =>
      AboutAddonsTestUtils.isCategoryButtonSelected(
        gBrowser.selectedBrowser.contentWindow,
        "extension"
      ),
  },
  {
    cmd: "themes",
    uri: "about:addons",
    numTabPress: 2,
    testFun: async () =>
      AboutAddonsTestUtils.isCategoryButtonSelected(
        gBrowser.selectedBrowser.contentWindow,
        "theme"
      ),
  },
  {
    cmd: "add-ons",
    setup: () => loadInCurrentTab("https://example.com/"),
    uri: "about:addons",
    loadType: LOAD_TYPE.NEW_TAB,
    testFun: async () =>
      AboutAddonsTestUtils.isCategoryButtonSelected(
        gBrowser.selectedBrowser.contentWindow,
        "discover"
      ),
  },
  {
    cmd: "extensions",
    setup: () => loadInCurrentTab("https://example.com/"),
    uri: "about:addons",
    loadType: LOAD_TYPE.NEW_TAB,
    testFun: async () =>
      AboutAddonsTestUtils.isCategoryButtonSelected(
        gBrowser.selectedBrowser.contentWindow,
        "extension"
      ),
    numTabPress: 2,
  },
  {
    cmd: "themes",
    setup: () => loadInCurrentTab("https://example.com/"),
    uri: "about:addons",
    loadType: LOAD_TYPE.NEW_TAB,
    testFun: async () =>
      AboutAddonsTestUtils.isCategoryButtonSelected(
        gBrowser.selectedBrowser.contentWindow,
        "theme"
      ),
    numTabPress: 2,
  },
  {
    cmd: "library",
    testFun: async () => {
      await TestUtils.waitForCondition(() => {
        return Services.wm.getMostRecentWindow("Places:Organizer");
      });
      const libraryWindow = Services.wm.getMostRecentWindow("Places:Organizer");
      libraryWindow?.close();
      return true;
    },
  },
  {
    // An about:preferences tab that is already open has to be navigated to the
    // pane the action asks for, rather than only focused.
    cmd: "manage ai",
    setup: () => loadInCurrentTab("about:preferences"),
    uri: "about:preferences#ai",
    loadType: LOAD_TYPE.PRE_LOADED,
    testFun: async () => {
      await TestUtils.waitForCondition(
        () =>
          gBrowser.selectedBrowser.currentURI.spec == "about:preferences#ai",
        "waiting for the open preferences tab to switch pane"
      );
      return true;
    },
  },
  {
    // "edit pdf" also phrase-matches the savepdf action ("pdf, save page"), so
    // both actions are offered. editpdf is picked first because it is declared
    // before savepdf in DEFAULT_ACTIONS; the about:pdf URI check below fails if
    // that ordering ever changes and the print dialog is triggered instead.
    cmd: "edit pdf",
    uri: "about:pdf",
    testFun: async () =>
      gBrowser.selectedBrowser.currentURI.spec == "about:pdf",
  },
];

add_task(async function test_pages() {
  for (const {
    cmd,
    uri,
    setup,
    loadType,
    testFun,
    numTabPress = 1,
  } of COMMANDS_TESTS) {
    info(`Testing ${cmd} command is triggered`);
    let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

    if (setup) {
      info("Setup");
      await setup();
    }

    let onLoad;
    if (loadType == LOAD_TYPE.NEW_TAB) {
      onLoad = BrowserTestUtils.waitForNewTab(gBrowser, uri, true);
    } else if (uri) {
      onLoad = BrowserTestUtils.browserLoaded(
        gBrowser.selectedBrowser,
        false,
        uri
      );
    } else {
      onLoad = null;
    }

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: cmd,
    });
    for (let i = 0; i < numTabPress; i++) {
      EventUtils.synthesizeKey("KEY_Tab", {}, window);
    }
    EventUtils.synthesizeKey("KEY_Enter", {}, window);

    let newTab = null;
    if (loadType != LOAD_TYPE.PRE_LOADED && onLoad) {
      newTab = (await onLoad) ?? null;
    }

    Assert.ok(
      await testFun(),
      `The command "${cmd}" passed completed its test`
    );

    if (loadType == LOAD_TYPE.PRE_LOADED) {
      // The action opens and selects the tab from a parent-side engagement
      // hook, which is async on the actor message path, so capture it only
      // after testFun has confirmed the page is loaded.
      newTab = gBrowser.selectedTab;
    }

    if ([LOAD_TYPE.NEW_TAB, LOAD_TYPE.PRE_LOADED].includes(loadType)) {
      await BrowserTestUtils.removeTab(newTab);
    }
    await BrowserTestUtils.removeTab(tab);
  }
});
