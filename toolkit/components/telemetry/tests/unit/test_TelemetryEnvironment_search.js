/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { TelemetryEnvironment } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryEnvironment.sys.mjs"
);
const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);

const { TelemetryEnvironmentTesting } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryEnvironmentTesting.sys.mjs"
);

const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);

SearchTestUtils.init(this);
ExtensionTestUtils.init(this);

function promiseNextTick() {
  return new Promise(resolve => executeSoon(resolve));
}

// The webserver hosting the addons.
var gHttpServer = null;
// The URL of the webserver root.
var gHttpRoot = null;
// The URL of the data directory, on the webserver.
var gDataRoot = null;

const SEARCH_CONFIG = [
  {
    identifier: "telemetrySearchIdentifier",
    base: {
      name: "telemetrySearchIdentifier",
      urls: {
        search: {
          base: "https://ar.wikipedia.org/wiki/%D8%AE%D8%A7%D8%B5:%D8%A8%D8%AD%D8%AB",
          params: [
            {
              name: "sourceId",
              value: "Mozilla-search",
            },
          ],
          searchTermParamName: "search",
        },
      },
    },
  },
];

add_setup(async function setup() {
  TelemetryEnvironmentTesting.registerFakeSysInfo();
  TelemetryEnvironmentTesting.spoofGfxAdapter();
  do_get_profile();

  // We need to ensure FOG is initialized, otherwise we will panic trying to get test values.
  Services.fog.initializeFOG();

  await loadAddonManager(APP_ID, APP_NAME, APP_VERSION, PLATFORM_VERSION);

  TelemetryEnvironmentTesting.init(gAppInfo);

  // The test runs in a fresh profile so starting the AddonManager causes
  // the addons database to be created (as does setting new theme).
  // For test_addonsStartup below, we want to test a "warm" startup where
  // there is already a database on disk.  Simulate that here by just
  // restarting the AddonManager.
  await AddonTestUtils.promiseShutdownManager();
  await AddonTestUtils.overrideBuiltIns({ builtins: [] });
  AddonTestUtils.addonStartup.remove(true);
  await AddonTestUtils.promiseStartupManager();

  // Setup a webserver to serve Addons, etc.
  gHttpServer = new HttpServer();
  gHttpServer.start(-1);
  let port = gHttpServer.identity.primaryPort;
  gHttpRoot = "http://localhost:" + port + "/";
  gDataRoot = gHttpRoot + "data/";
  gHttpServer.registerDirectory("/data/", do_get_cwd());
  registerCleanupFunction(() => gHttpServer.stop(() => {}));

  // Create the attribution data file, so that settings.attribution will exist.
  // The attribution functionality only exists in Firefox.
  if (AppConstants.MOZ_BUILD_APP == "browser") {
    TelemetryEnvironmentTesting.spoofAttributionData();
    registerCleanupFunction(async function () {
      await TelemetryEnvironmentTesting.cleanupAttributionData;
    });
  }

  await TelemetryEnvironmentTesting.spoofProfileReset();
  await TelemetryEnvironment.delayedInit();
  await SearchTestUtils.setRemoteSettingsConfig(SEARCH_CONFIG);

  // Now continue with startup.
  let initPromise = TelemetryEnvironment.onInitialized();
  finishAddonManagerStartup();

  // Fake the delayed startup event for intl data to load.
  fakeIntlReady();

  let environmentData = await initPromise;
  TelemetryEnvironmentTesting.checkEnvironmentData(environmentData, {
    isInitial: true,
  });

  TelemetryEnvironmentTesting.spoofPartnerInfo();
  Services.obs.notifyObservers(null, DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC);

  environmentData = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(environmentData, {
    assertProcessData: true,
  });
});

async function checkDefaultSearch(privateOn, reInitSearchService) {
  // Start off with separate default engine for private browsing turned off.
  Services.prefs.setBoolPref(
    "browser.search.separatePrivateDefault.ui.enabled",
    privateOn
  );
  Services.prefs.setBoolPref(
    "browser.search.separatePrivateDefault",
    privateOn
  );

  let data;
  if (privateOn) {
    data = await TelemetryEnvironment.testCleanRestart().onInitialized();
  } else {
    data = TelemetryEnvironment.currentEnvironment;
  }

  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.ok(!("defaultSearchEngine" in data.settings));
  Assert.ok(!("defaultSearchEngineData" in data.settings));
  Assert.ok(!("defaultPrivateSearchEngine" in data.settings));
  Assert.ok(!("defaultPrivateSearchEngineData" in data.settings));

  // Load the engines definitions from a xpcshell data: that's needed so that
  // the search provider reports an engine identifier.

  // Initialize the search service.
  if (reInitSearchService) {
    Services.search.wrappedJSObject.reset();
  }
  await Services.search.init();
  await promiseNextTick();

  // Our default engine from the JAR file has an identifier. Check if it is correctly
  // reported.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.equal(data.settings.defaultSearchEngine, "telemetrySearchIdentifier");
  let expectedSearchEngineData = {
    name: "telemetrySearchIdentifier",
    loadPath: "[app]telemetrySearchIdentifier",
    submissionURL:
      "https://ar.wikipedia.org/wiki/%D8%AE%D8%A7%D8%B5:%D8%A8%D8%AD%D8%AB?sourceId=Mozilla-search&search=",
  };
  Assert.deepEqual(
    data.settings.defaultSearchEngineData,
    expectedSearchEngineData
  );
  if (privateOn) {
    Assert.equal(
      data.settings.defaultPrivateSearchEngine,
      "telemetrySearchIdentifier"
    );
    Assert.deepEqual(
      data.settings.defaultPrivateSearchEngineData,
      expectedSearchEngineData,
      "Should have the correct data for the private search engine"
    );
  } else {
    Assert.ok(
      !("defaultPrivateSearchEngine" in data.settings),
      "Should not have private name recorded as the pref for separate is off"
    );
    Assert.ok(
      !("defaultPrivateSearchEngineData" in data.settings),
      "Should not have private data recorded as the pref for separate is off"
    );
  }

  // Add a new search engine (this will have no engine identifier).
  const SEARCH_ENGINE_ID = privateOn
    ? "telemetry_private"
    : "telemetry_default";
  const SEARCH_ENGINE_URL = `https://www.example.org/${
    privateOn ? "private" : ""
  }`;
  await SearchTestUtils.installSearchExtension({
    id: `${SEARCH_ENGINE_ID}@test.engine`,
    name: SEARCH_ENGINE_ID,
    search_url: SEARCH_ENGINE_URL,
  });

  // Register a new change listener and then wait for the search engine change to be notified.
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "testWatch_SearchDefault",
    deferred.resolve
  );
  if (privateOn) {
    // As we had no default and no search engines, the normal mode engine will
    // assume the same as the added engine. To ensure the telemetry is different
    // we enforce a different default here.
    const engine = await Services.search.getEngineByName(
      "telemetrySearchIdentifier"
    );
    engine.hidden = false;
    await Services.search.setDefault(
      engine,
      Ci.nsISearchService.CHANGE_REASON_UNKNOWN
    );
    await Services.search.setDefaultPrivate(
      Services.search.getEngineByName(SEARCH_ENGINE_ID),
      Ci.nsISearchService.CHANGE_REASON_UNKNOWN
    );
  } else {
    await Services.search.setDefault(
      Services.search.getEngineByName(SEARCH_ENGINE_ID),
      Ci.nsISearchService.CHANGE_REASON_UNKNOWN
    );
  }
  await deferred.promise;

  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  const EXPECTED_SEARCH_ENGINE = "other-" + SEARCH_ENGINE_ID;
  const EXPECTED_SEARCH_ENGINE_DATA = {
    name: SEARCH_ENGINE_ID,
    loadPath: `[addon]${SEARCH_ENGINE_ID}@test.engine`,
  };
  if (privateOn) {
    Assert.equal(
      data.settings.defaultSearchEngine,
      "telemetrySearchIdentifier"
    );
    Assert.deepEqual(
      data.settings.defaultSearchEngineData,
      expectedSearchEngineData
    );
    Assert.equal(
      data.settings.defaultPrivateSearchEngine,
      EXPECTED_SEARCH_ENGINE
    );
    Assert.deepEqual(
      data.settings.defaultPrivateSearchEngineData,
      EXPECTED_SEARCH_ENGINE_DATA
    );
  } else {
    Assert.equal(data.settings.defaultSearchEngine, EXPECTED_SEARCH_ENGINE);
    Assert.deepEqual(
      data.settings.defaultSearchEngineData,
      EXPECTED_SEARCH_ENGINE_DATA
    );
  }
  TelemetryEnvironment.unregisterChangeListener("testWatch_SearchDefault");
}

add_task(async function test_defaultSearchEngine() {
  await checkDefaultSearch(false);
  // Cleanly install an engine from an xml file.
  let promise = new Promise(resolve => {
    TelemetryEnvironment.registerChangeListener(
      "testWatch_SearchDefault",
      resolve
    );
  });
  await SearchTestUtils.installOpenSearchEngine({
    url: gDataRoot + "engine.xml",
    setAsDefault: true,
    skipReset: true,
  });
  await promise;
  TelemetryEnvironment.unregisterChangeListener("testWatch_SearchDefault");
  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.deepEqual(data.settings.defaultSearchEngineData, {
    name: "engine-telemetry",
    loadPath: "[http]localhost/engine-telemetry.xml",
  });

  const SEARCH_ENGINE_ID = "telemetry_default";
  const EXPECTED_SEARCH_ENGINE = "other-" + SEARCH_ENGINE_ID;
  // Work around bug 1165341: Intentionally set the default engine.
  await Services.search.setDefault(
    Services.search.getEngineByName(SEARCH_ENGINE_ID),
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );

  // Double-check the default for the next part of the test.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.equal(data.settings.defaultSearchEngine, EXPECTED_SEARCH_ENGINE);

  // Define and reset the test preference.
  const PREF_TEST = "toolkit.telemetry.test.pref1";
  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_STATE }],
  ]);
  Services.prefs.clearUserPref(PREF_TEST);

  // Watch the test preference.
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "testSearchEngine_pref",
    deferred.resolve
  );
  // Trigger an environment change.
  Services.prefs.setIntPref(PREF_TEST, 1);
  await deferred.promise;
  TelemetryEnvironment.unregisterChangeListener("testSearchEngine_pref");

  // Check that the search engine information is correctly retained when prefs change.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.equal(data.settings.defaultSearchEngine, EXPECTED_SEARCH_ENGINE);
});

add_task(async function test_defaultPrivateSearchEngine() {
  await checkDefaultSearch(true, true);
});

add_task(async function test_defaultSearchEngine_paramsChanged() {
  let extension = await SearchTestUtils.installSearchExtension(
    {
      name: "TestEngine",
      search_url: "https://www.google.com/fake1",
    },
    { skipUnload: true }
  );

  let promise = new Promise(resolve => {
    TelemetryEnvironment.registerChangeListener(
      "testWatch_SearchDefault",
      resolve
    );
  });
  let engine = Services.search.getEngineByName("TestEngine");
  await Services.search.setDefault(
    engine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
  await promise;

  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.deepEqual(data.settings.defaultSearchEngineData, {
    name: "TestEngine",
    loadPath: "[addon]testengine@tests.mozilla.org",
    submissionURL: "https://www.google.com/fake1?q=",
  });

  promise = new Promise(resolve => {
    TelemetryEnvironment.registerChangeListener(
      "testWatch_SearchDefault",
      resolve
    );
  });

  let manifest = SearchTestUtils.createEngineManifest({
    name: "TestEngine",
    version: "1.2",
    search_url: "https://www.google.com/fake2",
  });
  await extension.upgrade({
    useAddonManager: "permanent",
    manifest,
  });
  await AddonTestUtils.waitForSearchProviderStartup(extension);
  await promise;

  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.deepEqual(data.settings.defaultSearchEngineData, {
    name: "TestEngine",
    loadPath: "[addon]testengine@tests.mozilla.org",
    submissionURL: "https://www.google.com/fake2?q=",
  });

  await extension.unload();
});
