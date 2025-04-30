/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests for the default engine telemetry event that can be tested via xpcshell,
 * related to changing or selecting a different configuration.
 * Other tests are typically in browser mochitests.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AppProvidedSearchEngine:
    "resource://gre/modules/AppProvidedSearchEngine.sys.mjs",
});

const BASE_CONFIG = [
  {
    identifier: "originalDefault",
    base: {
      name: "Original Default",
      urls: {
        search: {
          base: "https://example.com/search",
          searchTermParamName: "q",
        },
      },
    },
  },
];

const MAIN_CONFIG = [
  {
    identifier: "originalDefault",
    base: {
      name: "Original Default",
      urls: {
        search: {
          base: "https://www.example.com/search",
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "newDefault",
    base: {
      name: "New Default",
      urls: {
        search: {
          base: "https://www.example.com/new",
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "defaultInLocaleFRNotRegionDE",
    base: {
      name: "Default in Locale FR and not Region DE",
      urls: {
        search: {
          base: "https://www.example.com/fr",
          params: [
            {
              name: "ie",
              value: "iso-8859-1",
            },
            {
              name: "oe",
              value: "iso-8859-1",
            },
          ],
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "defaultInRegionDE",
    base: {
      name: "Default in Region DE",
      urls: {
        search: {
          base: "https://www.example.org/de",
          params: [
            {
              name: "code",
              experimentConfig: "code",
            },
            {
              name: "test",
              experimentConfig: "test",
            },
          ],
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "defaultForExperiment",
    base: {
      name: "Default for Experiment",
      urls: {
        search: {
          base: "https://www.example.org/experiment",
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    globalDefault: "newDefault",
    specificDefaults: [
      {
        default: "defaultInLocaleFRNotRegionDE",
        environment: { excludedRegions: ["DE"], locales: ["fr"] },
      },
      {
        default: "defaultInRegionDE",
        environment: { regions: ["DE"] },
      },
      {
        default: "defaultForExperiment",
        environment: { experiment: "test1" },
      },
    ],
  },
];

const CONFIG_WITH_MODIFIED_CLASSIFICATION = [
  {
    identifier: "originalDefault",
    base: {
      name: "Original Default",
      urls: {
        search: {
          base: "https://example.com/search",
          searchTermParamName: "q",
        },
      },
      classification: "unknown",
    },
  },
];

const CONFIG_WITH_MODIFIED_NAME = [
  {
    identifier: "originalDefault",
    base: {
      name: "Modified Engine Name",
      urls: {
        search: {
          base: "https://example.com/search",
          searchTermParamName: "q",
        },
      },
      classification: "general",
    },
  },
];

const testSearchEngine = {
  id: "originalDefault",
  name: "Original Default",
  loadPath: "[app]originalDefault",
  submissionURL: "https://www.example.com/search?q=",
};
const testNewDefaultEngine = {
  id: "newDefault",
  name: "New Default",
  loadPath: "[app]newDefault",
  submissionURL: "https://www.example.com/new?q=",
};
const testDefaultInLocaleFRNotRegionDEEngine = {
  id: "defaultInLocaleFRNotRegionDE",
  name: "Default in Locale FR and not Region DE",
  loadPath: "[app]defaultInLocaleFRNotRegionDE",
  submissionURL: "https://www.example.com/fr?ie=iso-8859-1&oe=iso-8859-1&q=",
};
const testPrefEngine = {
  id: "defaultInRegionDE",
  name: "Default in Region DE",
  loadPath: "[app]defaultInRegionDE",
  submissionURL: "https://www.example.org/de?q=",
};
const testDefaultForExperiment = {
  id: "defaultForExperiment",
  name: "Default for Experiment",
  loadPath: "[app]defaultForExperiment",
  submissionURL: "https://www.example.org/experiment?q=",
};

function clearTelemetry() {
  Services.telemetry.clearEvents();
  Services.fog.testResetFOG();
}

async function checkTelemetry(
  source,
  prevEngine,
  newEngine,
  checkPrivate = false,
  additionalEventsExpected = false
) {
  let snapshot;
  if (checkPrivate) {
    snapshot = await Glean.searchEnginePrivate.changed.testGetValue();
  } else {
    snapshot = await Glean.searchEngineDefault.changed.testGetValue();
  }

  // additionalEventsExpected should be true whenever we expect something
  // stored in AppProvidedSearchEngine.#prevEngineInfo to have changed.
  if (additionalEventsExpected) {
    delete snapshot[0].timestamp;
    Assert.deepEqual(
      snapshot[0],
      {
        category: checkPrivate
          ? "search.engine.private"
          : "search.engine.default",
        name: "changed",
        extra: {
          change_reason: "engine-update",
          previous_engine_id: prevEngine?.id ?? "",
          new_engine_id: prevEngine?.id ?? "",
          new_display_name: prevEngine?.name ?? "",
          new_load_path: prevEngine?.loadPath ?? "",
          new_submission_url: prevEngine?.submissionURL ?? "",
        },
      },
      "Should have received the correct event details"
    );
    snapshot.shift();
  }

  delete snapshot[0].timestamp;
  Assert.deepEqual(
    snapshot[0],
    {
      category: checkPrivate
        ? "search.engine.private"
        : "search.engine.default",
      name: "changed",
      extra: {
        change_reason: source,
        previous_engine_id: prevEngine?.id ?? "",
        new_engine_id: newEngine?.id ?? "",
        new_display_name: newEngine?.name ?? "",
        new_load_path: newEngine?.loadPath ?? "",
        new_submission_url: newEngine?.submissionURL ?? "",
      },
    },
    "Should have received the correct event details"
  );
}

add_setup(async () => {
  Region._setHomeRegion("US", false);
  Services.locale.availableLocales = [
    ...Services.locale.availableLocales,
    "en",
    "fr",
  ];
  Services.locale.requestedLocales = ["en"];

  Services.fog.initializeFOG();
  sinon.stub(
    Services.search.wrappedJSObject,
    "_showRemovalOfSearchEngineNotificationBox"
  );

  SearchTestUtils.setRemoteSettingsConfig(BASE_CONFIG);

  await Services.search.init();

  registerCleanupFunction(async () => {
    sinon.restore();
  });
});

add_task(async function test_configuration_changes_default() {
  clearTelemetry();

  await SearchTestUtils.updateRemoteSettingsConfig(MAIN_CONFIG);

  await checkTelemetry(
    "config",
    testSearchEngine,
    testNewDefaultEngine,
    false,
    true
  );
});

add_task(async function test_experiment_changes_default() {
  clearTelemetry();

  let reloadObserved =
    SearchTestUtils.promiseSearchNotification("engines-reloaded");
  Services.prefs.setStringPref("browser.search.experiment", "test1");
  await reloadObserved;

  await checkTelemetry(
    "experiment",
    testNewDefaultEngine,
    testDefaultForExperiment,
    false
  );

  // Clear the pref so that we are no longer in an experiment.
  reloadObserved =
    SearchTestUtils.promiseSearchNotification("engines-reloaded");
  Services.prefs.clearUserPref("browser.search.experiment");
  await reloadObserved;
});

add_task(async function test_locale_changes_default() {
  clearTelemetry();

  let reloadObserved =
    SearchTestUtils.promiseSearchNotification("engines-reloaded");
  Services.locale.requestedLocales = ["fr"];
  await reloadObserved;

  await checkTelemetry(
    "locale",
    testNewDefaultEngine,
    testDefaultInLocaleFRNotRegionDEEngine,
    false
  );
});

add_task(async function test_region_changes_default() {
  clearTelemetry();

  let reloadObserved =
    SearchTestUtils.promiseSearchNotification("engines-reloaded");
  Region._setHomeRegion("DE", true);
  await reloadObserved;

  await checkTelemetry(
    "region",
    testDefaultInLocaleFRNotRegionDEEngine,
    testPrefEngine,
    false
  );
});

add_task(async function test_user_changes_separate_private_pref() {
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );

  await Services.search.setDefaultPrivate(
    Services.search.getEngineById("newDefault"),
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );

  Assert.notEqual(
    await Services.search.getDefault(),
    await Services.search.getDefaultPrivate(),
    "Should have different engines for the pre-condition"
  );

  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    false
  );

  clearTelemetry();

  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    false
  );

  await checkTelemetry("user_private_split", testNewDefaultEngine, null, true);
});

add_task(async function test_ui_enabled_with_separate_default_notifies() {
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    false
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );

  clearTelemetry();

  let defaultChanged = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );

  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );
  await defaultChanged;

  await checkTelemetry(
    "user_private_pref_enabled",
    null,
    testNewDefaultEngine,
    true
  );

  clearTelemetry();

  // Reset the pref so that we are no longer in an experiment.
  defaultChanged = SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    false
  );
  await defaultChanged;

  await checkTelemetry(
    "user_private_pref_enabled",
    testNewDefaultEngine,
    null,
    true
  );
});

add_task(async function test_default_engine_update() {
  clearTelemetry();
  let extension = await SearchTestUtils.installSearchExtension(
    {
      name: "engine",
      id: "engine@tests.mozilla.org",
      search_url_get_params: `q={searchTerms}&version=1.0`,
      search_url: "https://www.google.com/search",
      version: "1.0",
    },
    { skipUnload: true }
  );
  let engine = Services.search.getEngineByName("engine");

  Assert.ok(!!engine, "Should have loaded the engine");

  await Services.search.setDefault(
    engine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );

  clearTelemetry();

  let promiseChanged = TestUtils.topicObserved(
    "browser-search-engine-modified",
    (eng, verb) => verb == "engine-changed"
  );
  let manifest = SearchTestUtils.createEngineManifest({
    name: "Bar",
    id: "engine@tests.mozilla.org",
    search_url_get_params: `q={searchTerms}&version=2.0`,
    search_url: "https://www.google.com/search",
    version: "2.0",
  });

  await extension.upgrade({
    useAddonManager: "permanent",
    manifest,
  });
  await AddonTestUtils.waitForSearchProviderStartup(extension);
  await promiseChanged;

  const defaultEngineData = {
    id: engine.telemetryId,
    name: "Bar",
    loadPath: engine.wrappedJSObject._loadPath,
    submissionURL: "https://www.google.com/search?q=&version=2.0",
  };
  await checkTelemetry("engine-update", defaultEngineData, defaultEngineData);
  await extension.unload();
});

add_task(async function test_only_notify_on_relevant_engine_property_change() {
  clearTelemetry();
  await SearchTestUtils.updateRemoteSettingsConfig(BASE_CONFIG);

  // Since SearchUtils.notifyAction can be called for multiple different search
  // engine topics, `resetPrevEngineInfo` is a better way to track
  // notifications in this case.
  let notificationSpy = sinon.spy(
    AppProvidedSearchEngine.prototype,
    "_resetPrevEngineInfo"
  );

  // Change an engine property that is not stored in
  // AppProvidedSearchEngine.#prevEngineInfo.
  let reloadObserved =
    SearchTestUtils.promiseSearchNotification("engines-reloaded");
  await SearchTestUtils.updateRemoteSettingsConfig(
    CONFIG_WITH_MODIFIED_CLASSIFICATION
  );
  await reloadObserved;

  Assert.equal(
    notificationSpy.callCount,
    0,
    "Should not have sent a notification"
  );

  notificationSpy.restore();
});

add_task(
  async function test_multiple_updates_only_notify_on_relevant_engine_property_change() {
    clearTelemetry();
    await SearchTestUtils.updateRemoteSettingsConfig(BASE_CONFIG);

    // Since SearchUtils.notifyAction can be called for multiple different search
    // engine topics, `resetPrevEngineInfo` is a better way to track
    // notifications in this case.
    let notificationSpy = sinon.spy(
      AppProvidedSearchEngine.prototype,
      "_resetPrevEngineInfo"
    );

    // Change an engine property that is not stored in
    // AppProvidedSearchEngine.#prevEngineInfo.
    let reloadObserved1 =
      SearchTestUtils.promiseSearchNotification("engines-reloaded");
    await SearchTestUtils.updateRemoteSettingsConfig(
      CONFIG_WITH_MODIFIED_CLASSIFICATION
    );
    await reloadObserved1;

    Assert.equal(
      notificationSpy.callCount,
      0,
      "Should not have sent a notification"
    );

    // Now change an engine property that is stored in
    // AppProvidedSearchEngine.#prevEngineInfo.
    let reloadObserved2 =
      SearchTestUtils.promiseSearchNotification("engines-reloaded");
    await SearchTestUtils.updateRemoteSettingsConfig(CONFIG_WITH_MODIFIED_NAME);
    await reloadObserved2;

    Assert.equal(
      notificationSpy.callCount,
      1,
      "Should have sent a notification"
    );

    notificationSpy.restore();
  }
);
