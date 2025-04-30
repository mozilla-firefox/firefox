/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { AddonManager, AddonManagerPrivate } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { TelemetryEnvironment } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryEnvironment.sys.mjs"
);
const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);
const { TelemetryEnvironmentTesting } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryEnvironmentTesting.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  ExtensionTestUtils:
    "resource://testing-common/ExtensionXPCShellUtils.sys.mjs",
});

async function installXPIFromURL(url) {
  let install = await AddonManager.getInstallForURL(url);
  return install.install();
}

// The webserver hosting the addons.
var gHttpServer = null;
// The URL of the webserver root.
var gHttpRoot = null;
// The URL of the data directory, on the webserver.
var gDataRoot = null;

function MockAddonWrapper(aAddon) {
  this.addon = aAddon;
}
MockAddonWrapper.prototype = {
  get id() {
    return this.addon.id;
  },

  get type() {
    return this.addon.type;
  },

  get appDisabled() {
    return false;
  },

  get isCompatible() {
    return true;
  },

  get isPlatformCompatible() {
    return true;
  },

  get scope() {
    return AddonManager.SCOPE_PROFILE;
  },

  get foreignInstall() {
    return false;
  },

  get providesUpdatesSecurely() {
    return true;
  },

  get blocklistState() {
    return 0; // Not blocked.
  },

  get pendingOperations() {
    return AddonManager.PENDING_NONE;
  },

  get permissions() {
    return AddonManager.PERM_CAN_UNINSTALL | AddonManager.PERM_CAN_DISABLE;
  },

  get isActive() {
    return true;
  },

  get name() {
    return this.addon.name;
  },

  get version() {
    return this.addon.version;
  },

  get creator() {
    return new AddonManagerPrivate.AddonAuthor(this.addon.author);
  },

  get userDisabled() {
    return this.appDisabled;
  },
};

function createMockAddonProvider(aName) {
  let mockProvider = {
    _addons: [],

    get name() {
      return aName;
    },

    addAddon(aAddon) {
      this._addons.push(aAddon);
      AddonManagerPrivate.callAddonListeners(
        "onInstalled",
        new MockAddonWrapper(aAddon)
      );
    },

    async getAddonsByTypes(aTypes) {
      return this._addons
        .filter(a => !aTypes || aTypes.includes(a.type))
        .map(a => new MockAddonWrapper(a));
    },

    shutdown() {
      return Promise.resolve();
    },
  };

  return mockProvider;
}

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
  // The environment needs the search service initialised, so use a dummy
  // configuration.
  await SearchTestUtils.setRemoteSettingsConfig([{ identifier: "unused" }]);
});

add_task(async function test_checkEnvironment() {
  // During startup we have partial addon records.
  // First make sure we haven't yet read the addons DB, then test that
  // we have some partial addons data.
  Assert.equal(
    AddonManagerPrivate.isDBLoaded(),
    false,
    "addons database is not loaded"
  );

  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkAddonsSection(data, false, true);

  // Check that settings.intl is lazily loaded.
  Assert.equal(
    typeof data.settings.intl,
    "object",
    "intl is initially an object"
  );
  Assert.equal(
    Object.keys(data.settings.intl).length,
    0,
    "intl is initially empty"
  );

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

add_task(async function test_prefWatchPolicies() {
  const PREF_TEST_1 = "toolkit.telemetry.test.pref_new";
  const PREF_TEST_2 = "toolkit.telemetry.test.pref1";
  const PREF_TEST_3 = "toolkit.telemetry.test.pref2";
  const PREF_TEST_4 = "toolkit.telemetry.test.pref_old";
  const PREF_TEST_5 = "toolkit.telemetry.test.requiresRestart";

  const expectedValue = "some-test-value";
  const unexpectedValue = "unexpected-test-value";

  const PREFS_TO_WATCH = new Map([
    [PREF_TEST_1, { what: TelemetryEnvironment.RECORD_PREF_VALUE }],
    [PREF_TEST_2, { what: TelemetryEnvironment.RECORD_PREF_STATE }],
    [PREF_TEST_3, { what: TelemetryEnvironment.RECORD_PREF_STATE }],
    [PREF_TEST_4, { what: TelemetryEnvironment.RECORD_PREF_VALUE }],
    [
      PREF_TEST_5,
      { what: TelemetryEnvironment.RECORD_PREF_VALUE, requiresRestart: true },
    ],
  ]);

  Services.prefs.setStringPref(PREF_TEST_4, expectedValue);
  Services.prefs.setStringPref(PREF_TEST_5, expectedValue);

  // Set the Environment preferences to watch.
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);
  let deferred = Promise.withResolvers();

  // Check that the pref values are missing or present as expected
  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST_1],
    undefined
  );
  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST_4],
    expectedValue
  );
  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST_5],
    expectedValue
  );

  TelemetryEnvironment.registerChangeListener(
    "testWatchPrefs",
    (reason, data) => deferred.resolve(data)
  );
  let oldEnvironmentData = TelemetryEnvironment.currentEnvironment;

  // Trigger a change in the watched preferences.
  Services.prefs.setStringPref(PREF_TEST_1, expectedValue);
  Services.prefs.setBoolPref(PREF_TEST_2, false);
  Services.prefs.setStringPref(PREF_TEST_5, unexpectedValue);
  let eventEnvironmentData = await deferred.promise;

  // Unregister the listener.
  TelemetryEnvironment.unregisterChangeListener("testWatchPrefs");

  // Check environment contains the correct data.
  Assert.deepEqual(oldEnvironmentData, eventEnvironmentData);
  let userPrefs = TelemetryEnvironment.currentEnvironment.settings.userPrefs;

  Assert.equal(
    userPrefs[PREF_TEST_1],
    expectedValue,
    "Environment contains the correct preference value."
  );
  Assert.equal(
    userPrefs[PREF_TEST_2],
    "<user-set>",
    "Report that the pref was user set but the value is not shown."
  );
  Assert.ok(
    !(PREF_TEST_3 in userPrefs),
    "Do not report if preference not user set."
  );
  Assert.equal(
    userPrefs[PREF_TEST_5],
    expectedValue,
    "The pref value in the environment data should still be the same"
  );
});

add_task(async function test_prefWatch_prefReset() {
  const PREF_TEST = "toolkit.telemetry.test.pref1";
  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_STATE }],
  ]);

  // Set the preference to a non-default value.
  Services.prefs.setBoolPref(PREF_TEST, false);

  // Set the Environment preferences to watch.
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "testWatchPrefs_reset",
    deferred.resolve
  );

  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST],
    "<user-set>"
  );

  // Trigger a change in the watched preferences.
  Services.prefs.clearUserPref(PREF_TEST);
  await deferred.promise;

  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST],
    undefined
  );

  // Unregister the listener.
  TelemetryEnvironment.unregisterChangeListener("testWatchPrefs_reset");
});

add_task(async function test_prefDefault() {
  const PREF_TEST = "toolkit.telemetry.test.defaultpref1";
  const expectedValue = "some-test-value";

  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_DEFAULTPREF_VALUE }],
  ]);

  // Set the preference to a default value.
  Services.prefs.getDefaultBranch(null).setCharPref(PREF_TEST, expectedValue);

  // Set the Environment preferences to watch.
  // We're not watching, but this function does the setup we need.
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);

  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST],
    expectedValue
  );
});

add_task(async function test_prefDefaultState() {
  const PREF_TEST = "toolkit.telemetry.test.defaultpref2";
  const expectedValue = "some-test-value";

  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_DEFAULTPREF_STATE }],
  ]);

  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);

  Assert.equal(
    PREF_TEST in TelemetryEnvironment.currentEnvironment.settings.userPrefs,
    false
  );

  // Set the preference to a default value.
  Services.prefs.getDefaultBranch(null).setCharPref(PREF_TEST, expectedValue);

  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST],
    "<set>"
  );
});

add_task(async function test_prefInvalid() {
  const PREF_TEST_1 = "toolkit.telemetry.test.invalid1";
  const PREF_TEST_2 = "toolkit.telemetry.test.invalid2";

  const PREFS_TO_WATCH = new Map([
    [PREF_TEST_1, { what: TelemetryEnvironment.RECORD_DEFAULTPREF_VALUE }],
    [PREF_TEST_2, { what: TelemetryEnvironment.RECORD_DEFAULTPREF_STATE }],
  ]);

  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);

  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST_1],
    undefined
  );
  Assert.strictEqual(
    TelemetryEnvironment.currentEnvironment.settings.userPrefs[PREF_TEST_2],
    undefined
  );
});

add_task(async function test_addonsWatch_InterestingChange() {
  const ADDON_INSTALL_URL = gDataRoot + "restartless.xpi";
  const ADDON_ID = "tel-restartless-webext@tests.mozilla.org";
  // We only expect a single notification for each install, uninstall, enable, disable.
  const EXPECTED_NOTIFICATIONS = 4;

  let receivedNotifications = 0;

  let registerCheckpointPromise = aExpected => {
    return new Promise(resolve =>
      TelemetryEnvironment.registerChangeListener(
        "testWatchAddons_Changes" + aExpected,
        reason => {
          Assert.equal(reason, "addons-changed");
          receivedNotifications++;
          resolve();
        }
      )
    );
  };

  let assertCheckpoint = aExpected => {
    Assert.equal(receivedNotifications, aExpected);
    TelemetryEnvironment.unregisterChangeListener(
      "testWatchAddons_Changes" + aExpected
    );
  };

  // Test for receiving one notification after each change.
  let checkpointPromise = registerCheckpointPromise(1);
  await installXPIFromURL(ADDON_INSTALL_URL);
  await checkpointPromise;
  assertCheckpoint(1);
  Assert.ok(
    ADDON_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons
  );

  checkpointPromise = registerCheckpointPromise(2);
  let addon = await AddonManager.getAddonByID(ADDON_ID);
  await addon.disable();
  await checkpointPromise;
  assertCheckpoint(2);
  Assert.ok(
    !(ADDON_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons)
  );

  checkpointPromise = registerCheckpointPromise(3);
  let startupPromise = AddonTestUtils.promiseWebExtensionStartup(ADDON_ID);
  await addon.enable();
  await checkpointPromise;
  assertCheckpoint(3);
  Assert.ok(
    ADDON_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons
  );
  await startupPromise;

  checkpointPromise = registerCheckpointPromise(4);
  (await AddonManager.getAddonByID(ADDON_ID)).uninstall();
  await checkpointPromise;
  assertCheckpoint(4);
  Assert.ok(
    !(ADDON_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons)
  );

  Assert.equal(
    receivedNotifications,
    EXPECTED_NOTIFICATIONS,
    "We must only receive the notifications we expect."
  );
});

add_task(async function test_addonsWatch_NotInterestingChange() {
  // Plugins from GMPProvider are listed separately in addons.activeGMPlugins.
  // We simulate the "plugin" type in this test and verify that it is excluded.
  const PLUGIN_ID = "tel-fake-gmp-plugin@tests.mozilla.org";
  // "theme" type is already covered by addons.theme, so we aren't interested.
  const THEME_ID = "tel-theme@tests.mozilla.org";
  // "dictionary" type should be in addon.activeAddons.
  const DICT_ID = "tel-dict@tests.mozilla.org";

  let receivedNotification = false;
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener("testNotInteresting", () => {
    Assert.ok(
      !receivedNotification,
      "Should not receive multiple notifications"
    );
    receivedNotification = true;
    deferred.resolve();
  });

  // "plugin" type, to verify that non-XPIProvider types such as the "plugin"
  // type from GMPProvider are not included in activeAddons.
  let fakePluginProvider = createMockAddonProvider("Fake GMPProvider");
  AddonManagerPrivate.registerProvider(fakePluginProvider);
  fakePluginProvider.addAddon({
    id: PLUGIN_ID,
    name: "Fake plugin",
    version: "1",
    type: "plugin",
  });

  // "theme" type.
  let themeXpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      theme: {},
      browser_specific_settings: { gecko: { id: THEME_ID } },
    },
  });
  let themeAddon = (await AddonTestUtils.promiseInstallFile(themeXpi)).addon;

  let dictXpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      dictionaries: {},
      browser_specific_settings: { gecko: { id: DICT_ID } },
    },
  });
  let dictAddon = (await AddonTestUtils.promiseInstallFile(dictXpi)).addon;

  await deferred.promise;
  Assert.ok(
    !(PLUGIN_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons),
    "GMP plugins should not appear in active addons."
  );
  Assert.ok(
    !(THEME_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons),
    "Themes should not appear in active addons."
  );
  Assert.ok(
    DICT_ID in TelemetryEnvironment.currentEnvironment.addons.activeAddons,
    "Dictionaries should appear in active addons."
  );

  TelemetryEnvironment.unregisterChangeListener("testNotInteresting");

  AddonManagerPrivate.unregisterProvider(fakePluginProvider);
  await themeAddon.uninstall();
  await dictAddon.uninstall();
});

add_task(async function test_addons() {
  const ADDON_INSTALL_URL = gDataRoot + "restartless.xpi";
  const ADDON_ID = "tel-restartless-webext@tests.mozilla.org";
  const ADDON_INSTALL_DATE = truncateToDays(Date.now());
  const EXPECTED_ADDON_DATA = {
    blocklisted: false,
    description: "A restartless addon which gets enabled without a reboot.",
    name: "XPI Telemetry Restartless Test",
    userDisabled: false,
    appDisabled: false,
    version: "1.0",
    scope: AddonManager.SCOPE_PROFILE,
    type: "extension",
    foreignInstall: false,
    hasBinaryComponents: false,
    installDay: ADDON_INSTALL_DATE,
    updateDay: ADDON_INSTALL_DATE,
    signedState: AddonManager.SIGNEDSTATE_PRIVILEGED,
    isSystem: false,
    isWebExtension: true,
    multiprocessCompatible: true,
    quarantineIgnoredByUser: false,
    // quarantineIgnoredByApp expected to be true because
    // the test addon is signed as privileged (see signedState).
    quarantineIgnoredByApp: true,
  };
  const SYSTEM_ADDON_ID = "tel-system-xpi@tests.mozilla.org";
  const EXPECTED_SYSTEM_ADDON_DATA = {
    blocklisted: false,
    description: "A system addon which is shipped with Firefox.",
    name: "XPI Telemetry System Add-on Test",
    userDisabled: false,
    appDisabled: false,
    version: "1.0",
    scope: AddonManager.SCOPE_APPLICATION,
    type: "extension",
    foreignInstall: false,
    hasBinaryComponents: false,
    installDay: 0,
    updateDay: 0,
    signedState: undefined,
    isSystem: true,
    isWebExtension: true,
    multiprocessCompatible: true,
    quarantineIgnoredByUser: false,
    // quarantineIgnoredByApp expected to be true because
    // the test addon is a system addon (see isSystem).
    quarantineIgnoredByApp: true,
  };

  const WEBEXTENSION_ADDON_ID = "tel-webextension-xpi@tests.mozilla.org";
  const WEBEXTENSION_ADDON_INSTALL_DATE = truncateToDays(Date.now());
  const EXPECTED_WEBEXTENSION_ADDON_DATA = {
    blocklisted: false,
    description: "A webextension addon.",
    name: "XPI Telemetry WebExtension Add-on Test",
    userDisabled: false,
    appDisabled: false,
    version: "1.0",
    scope: AddonManager.SCOPE_PROFILE,
    type: "extension",
    foreignInstall: false,
    hasBinaryComponents: false,
    installDay: WEBEXTENSION_ADDON_INSTALL_DATE,
    updateDay: WEBEXTENSION_ADDON_INSTALL_DATE,
    signedState: AddonManager.SIGNEDSTATE_PRIVILEGED,
    isSystem: false,
    isWebExtension: true,
    multiprocessCompatible: true,
    quarantineIgnoredByUser: false,
    // quarantineIgnoredByApp expected to be true because
    // the test addon is signed as privileged (see signedState).
    quarantineIgnoredByApp: true,
  };

  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener("test_WebExtension", reason => {
    Assert.equal(reason, "addons-changed");
    deferred.resolve();
  });

  // Install an add-on so we have some data.
  let addon = await installXPIFromURL(ADDON_INSTALL_URL);

  // Install a webextension as well.
  // Note: all addons are webextensions, so doing this again is redundant...
  ExtensionTestUtils.init(this);

  let webextension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      name: "XPI Telemetry WebExtension Add-on Test",
      description: "A webextension addon.",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: WEBEXTENSION_ADDON_ID,
        },
      },
    },
  });

  await webextension.startup();
  await deferred.promise;
  TelemetryEnvironment.unregisterChangeListener("test_WebExtension");

  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  // Check addon data.
  Assert.ok(
    ADDON_ID in data.addons.activeAddons,
    "We must have one active addon."
  );
  let targetAddon = data.addons.activeAddons[ADDON_ID];
  for (let f in EXPECTED_ADDON_DATA) {
    Assert.equal(
      targetAddon[f],
      EXPECTED_ADDON_DATA[f],
      f + " must have the correct value."
    );
  }

  // Check system add-on data.
  Assert.ok(
    SYSTEM_ADDON_ID in data.addons.activeAddons,
    "We must have one active system addon."
  );
  let targetSystemAddon = data.addons.activeAddons[SYSTEM_ADDON_ID];
  for (let f in EXPECTED_SYSTEM_ADDON_DATA) {
    Assert.equal(
      targetSystemAddon[f],
      EXPECTED_SYSTEM_ADDON_DATA[f],
      f + " must have the correct value."
    );
  }

  // Check webextension add-on data.
  Assert.ok(
    WEBEXTENSION_ADDON_ID in data.addons.activeAddons,
    "We must have one active webextension addon."
  );
  let targetWebExtensionAddon = data.addons.activeAddons[WEBEXTENSION_ADDON_ID];
  for (let f in EXPECTED_WEBEXTENSION_ADDON_DATA) {
    Assert.equal(
      targetWebExtensionAddon[f],
      EXPECTED_WEBEXTENSION_ADDON_DATA[f],
      f + " must have the correct value."
    );
  }

  await webextension.unload();

  // Uninstall the addon.
  await addon.startupPromise;
  await addon.uninstall();
});

add_task(async function test_signedTheme() {
  AddonTestUtils.useRealCertChecks = true;

  const { PKCS7_WITH_SHA1, COSE_WITH_SHA256 } = Ci.nsIAppSignatureInfo;

  const ADDON_THEME_INSTALL_URL = gDataRoot + "webext-implicit-id.xpi";
  const ADDON_THEME_ID = "{46607a7b-1b2a-40ce-9afe-91cda52c46a6}";

  // Install the theme.
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_signedAddon",
    deferred.resolve
  );
  let theme = await installXPIFromURL(ADDON_THEME_INSTALL_URL);
  await theme.enable();
  ok(theme.isActive, "Theme should be active");

  // Install an extension to force the telemetry environment to be
  // updated (currently theme add-ons changes do not seem to be
  // notified as changes, see EnvironmentAddonBuilder _updateAddons
  // method for how changes to the environment.addons property are
  // being detected).
  const ADDON_INSTALL_URL = gDataRoot + "amosigned.xpi";
  let addon = await installXPIFromURL(ADDON_INSTALL_URL);

  await deferred.promise;
  TelemetryEnvironment.unregisterChangeListener("test_signedAddon");

  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  // Check signedState and signedTypes on active theme data
  // (NOTE: other properties of active theme are technically
  // not covered by any other test task in this xpcshell test).
  Assert.equal(
    data.addons.theme.id,
    ADDON_THEME_ID,
    "Theme should be in the environment."
  );
  Assert.equal(
    data.addons.theme.signedState,
    AddonManager.SIGNEDSTATE_SIGNED,
    "Got expected signedState on activeTheme"
  );
  Assert.equal(
    data.addons.theme.signedTypes,
    JSON.stringify([COSE_WITH_SHA256, PKCS7_WITH_SHA1]),
    "Got expected signedTypes on activeTheme"
  );

  AddonTestUtils.useRealCertChecks = false;
  await addon.startupPromise;
  await addon.uninstall();
  await theme.startupPromise;
  await theme.uninstall();
});

add_task(async function test_signedAddon() {
  AddonTestUtils.useRealCertChecks = true;

  const { PKCS7_WITH_SHA1, COSE_WITH_SHA256 } = Ci.nsIAppSignatureInfo;

  const ADDON_INSTALL_URL = gDataRoot + "amosigned.xpi";
  const ADDON_ID = "amosigned-xpi@tests.mozilla.org";
  const ADDON_INSTALL_DATE = truncateToDays(Date.now());
  const EXPECTED_ADDON_DATA = {
    blocklisted: false,
    description: null,
    name: "XPI Test",
    userDisabled: false,
    appDisabled: false,
    version: "2.2",
    scope: AddonManager.SCOPE_PROFILE,
    type: "extension",
    foreignInstall: false,
    hasBinaryComponents: false,
    installDay: ADDON_INSTALL_DATE,
    updateDay: ADDON_INSTALL_DATE,
    signedState: AddonManager.SIGNEDSTATE_SIGNED,
    signedTypes: JSON.stringify([COSE_WITH_SHA256, PKCS7_WITH_SHA1]),
    quarantineIgnoredByUser: false,
    // quarantineIgnoredByApp expected to be false because
    // the test addon is signed as a non-privileged (see signedState),
    // and it doesn't include any recommendations.
    quarantineIgnoredByApp: false,
  };

  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_signedAddon",
    deferred.resolve
  );

  // Install the addon.
  let addon = await installXPIFromURL(ADDON_INSTALL_URL);

  await deferred.promise;
  // Unregister the listener.
  TelemetryEnvironment.unregisterChangeListener("test_signedAddon");

  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  // Check addon data.
  Assert.ok(
    ADDON_ID in data.addons.activeAddons,
    "Add-on should be in the environment."
  );
  let targetAddon = data.addons.activeAddons[ADDON_ID];
  for (let f in EXPECTED_ADDON_DATA) {
    Assert.equal(
      targetAddon[f],
      EXPECTED_ADDON_DATA[f],
      f + " must have the correct value."
    );
  }

  // Make sure quarantineIgnoredByUser property is updated also in the
  // telemetry environment in response to the user changing it.
  deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_quarantineIgnoreByUser_changed",
    deferred.resolve
  );

  addon.quarantineIgnoredByUser = true;
  await deferred.promise;
  // Unregister the listener.
  TelemetryEnvironment.unregisterChangeListener(
    "test_quarantineIgnoreByUser_changed"
  );

  Assert.equal(
    TelemetryEnvironment.currentEnvironment.addons.activeAddons[ADDON_ID]
      .quarantineIgnoredByUser,
    true,
    "Expect quarantineIgnoredByUser to be set to true"
  );

  AddonTestUtils.useRealCertChecks = false;
  await addon.startupPromise;
  await addon.uninstall();
});

add_task(async function test_addonsFieldsLimit() {
  const ADDON_INSTALL_URL = gDataRoot + "long-fields.xpi";
  const ADDON_ID = "tel-longfields-webext@tests.mozilla.org";

  // Install the addon and wait for the TelemetryEnvironment to pick it up.
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_longFieldsAddon",
    deferred.resolve
  );
  let addon = await installXPIFromURL(ADDON_INSTALL_URL);
  await deferred.promise;
  TelemetryEnvironment.unregisterChangeListener("test_longFieldsAddon");

  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  // Check that the addon is available and that the string fields are limited.
  Assert.ok(
    ADDON_ID in data.addons.activeAddons,
    "Add-on should be in the environment."
  );
  let targetAddon = data.addons.activeAddons[ADDON_ID];

  // TelemetryEnvironment limits the length of string fields for activeAddons to 100 chars,
  // to mitigate misbehaving addons.
  Assert.lessOrEqual(
    targetAddon.version.length,
    100,
    "The version string must have been limited"
  );
  Assert.lessOrEqual(
    targetAddon.name.length,
    100,
    "The name string must have been limited"
  );
  Assert.lessOrEqual(
    targetAddon.description.length,
    100,
    "The description string must have been limited"
  );

  await addon.startupPromise;
  await addon.uninstall();
});

add_task(async function test_collectionWithbrokenAddonData() {
  const BROKEN_ADDON_ID = "telemetry-test2.example.com@services.mozilla.org";
  const BROKEN_MANIFEST = {
    id: "telemetry-test2.example.com@services.mozilla.org",
    name: "telemetry broken addon",
    origin: "https://telemetry-test2.example.com",
    version: 1, // This is intentionally not a string.
    signedState: AddonManager.SIGNEDSTATE_SIGNED,
    type: "extension",
  };

  const ADDON_INSTALL_URL = gDataRoot + "restartless.xpi";
  const ADDON_ID = "tel-restartless-webext@tests.mozilla.org";
  const ADDON_INSTALL_DATE = truncateToDays(Date.now());
  const EXPECTED_ADDON_DATA = {
    blocklisted: false,
    description: "A restartless addon which gets enabled without a reboot.",
    name: "XPI Telemetry Restartless Test",
    userDisabled: false,
    appDisabled: false,
    version: "1.0",
    scope: AddonManager.SCOPE_PROFILE,
    type: "extension",
    foreignInstall: false,
    hasBinaryComponents: false,
    installDay: ADDON_INSTALL_DATE,
    updateDay: ADDON_INSTALL_DATE,
    signedState: AddonManager.SIGNEDSTATE_MISSING,
  };

  let receivedNotifications = 0;

  let registerCheckpointPromise = aExpected => {
    return new Promise(resolve =>
      TelemetryEnvironment.registerChangeListener(
        "testBrokenAddon_collection" + aExpected,
        reason => {
          Assert.equal(reason, "addons-changed");
          receivedNotifications++;
          resolve();
        }
      )
    );
  };

  let assertCheckpoint = aExpected => {
    Assert.equal(receivedNotifications, aExpected);
    TelemetryEnvironment.unregisterChangeListener(
      "testBrokenAddon_collection" + aExpected
    );
  };

  // Register the broken provider and install the broken addon.
  let checkpointPromise = registerCheckpointPromise(1);
  let brokenAddonProvider = createMockAddonProvider(
    "Broken Extensions Provider"
  );
  AddonManagerPrivate.registerProvider(brokenAddonProvider);
  brokenAddonProvider.addAddon(BROKEN_MANIFEST);
  await checkpointPromise;
  assertCheckpoint(1);

  // Now install an addon which returns the correct information.
  checkpointPromise = registerCheckpointPromise(2);
  let addon = await installXPIFromURL(ADDON_INSTALL_URL);
  await checkpointPromise;
  assertCheckpoint(2);

  // Check that the new environment contains the info from the broken provider,
  // despite the addon missing some details.
  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data, {
    expectBrokenAddons: true,
  });

  let activeAddons = data.addons.activeAddons;
  Assert.ok(
    BROKEN_ADDON_ID in activeAddons,
    "The addon with the broken manifest must be reported."
  );
  Assert.equal(
    activeAddons[BROKEN_ADDON_ID].version,
    null,
    "null should be reported for invalid data."
  );
  Assert.ok(ADDON_ID in activeAddons, "The valid addon must be reported.");
  Assert.equal(
    activeAddons[ADDON_ID].description,
    EXPECTED_ADDON_DATA.description,
    "The description for the valid addon should be correct."
  );

  // Unregister the broken provider so we don't mess with other tests.
  AddonManagerPrivate.unregisterProvider(brokenAddonProvider);

  // Uninstall the valid addon.
  await addon.startupPromise;
  await addon.uninstall();
});

add_task(
  { skip_if: () => AppConstants.MOZ_APP_NAME == "thunderbird" },
  async function test_delayed_defaultBrowser() {
    // Skip this test on Thunderbird since it is not a browser, so it cannot
    // be the default browser.

    // Make sure we don't have anything already cached for this test.
    await TelemetryEnvironment.testCleanRestart().onInitialized();

    let environmentData = TelemetryEnvironment.currentEnvironment;
    TelemetryEnvironmentTesting.checkEnvironmentData(environmentData);
    Assert.equal(
      environmentData.settings.isDefaultBrowser,
      null,
      "isDefaultBrowser must be null before the session is restored."
    );

    Services.obs.notifyObservers(null, "sessionstore-windows-restored");
    // Session restore triggers search service init asynchronously.
    // If this completes during shutdown, it throws an exception.
    // Await the search service init to make this deterministic (bug 1885310).
    await Services.search.promiseInitialized;

    environmentData = TelemetryEnvironment.currentEnvironment;
    TelemetryEnvironmentTesting.checkEnvironmentData(environmentData);
    Assert.ok(
      "isDefaultBrowser" in environmentData.settings,
      "isDefaultBrowser must be available after the session is restored."
    );
    Assert.equal(
      typeof environmentData.settings.isDefaultBrowser,
      "boolean",
      "isDefaultBrowser must be of the right type."
    );

    // Make sure pref-flipping doesn't overwrite the browser default state.
    const PREF_TEST = "toolkit.telemetry.test.pref1";
    const PREFS_TO_WATCH = new Map([
      [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_STATE }],
    ]);
    Services.prefs.clearUserPref(PREF_TEST);

    // Watch the test preference.
    await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);
    let deferred = Promise.withResolvers();
    TelemetryEnvironment.registerChangeListener(
      "testDefaultBrowser_pref",
      deferred.resolve
    );
    // Trigger an environment change.
    Services.prefs.setIntPref(PREF_TEST, 1);
    await deferred.promise;
    TelemetryEnvironment.unregisterChangeListener("testDefaultBrowser_pref");

    // Check that the data is still available.
    environmentData = TelemetryEnvironment.currentEnvironment;
    TelemetryEnvironmentTesting.checkEnvironmentData(environmentData);
    Assert.ok(
      "isDefaultBrowser" in environmentData.settings,
      "isDefaultBrowser must still be available after a pref is flipped."
    );
  }
);

add_task(async function test_osstrings() {
  // First test that numbers in sysinfo properties are converted to string fields
  // in system.os.
  TelemetryEnvironmentTesting.setSysInfoOverrides({
    version: 1,
    name: 2,
    kernel_version: 3,
  });

  await TelemetryEnvironment.testCleanRestart().onInitialized();
  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  Assert.equal(data.system.os.version, "1");
  Assert.equal(data.system.os.name, "2");
  if (AppConstants.platform == "android") {
    Assert.equal(data.system.os.kernelVersion, "3");
  }

  // Check that null values are also handled.
  TelemetryEnvironmentTesting.setSysInfoOverrides({
    version: null,
    name: null,
    kernel_version: null,
  });

  await TelemetryEnvironment.testCleanRestart().onInitialized();
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  Assert.equal(data.system.os.version, null);
  Assert.equal(data.system.os.name, null);
  if (AppConstants.platform == "android") {
    Assert.equal(data.system.os.kernelVersion, null);
  }

  // Clean up.
  TelemetryEnvironmentTesting.setSysInfoOverrides({});
  await TelemetryEnvironment.testCleanRestart().onInitialized();
});

add_task(async function test_experimentsAPI() {
  const EXPERIMENT1 = "experiment-1";
  const EXPERIMENT1_BRANCH = "nice-branch";
  const EXPERIMENT2 = "experiment-2";
  const EXPERIMENT2_BRANCH = "other-branch";

  let checkExperiment = (environmentData, id, branch) => {
    Assert.ok(
      "experiments" in environmentData,
      "The current environment must report the experiment annotations."
    );
    Assert.ok(
      id in environmentData.experiments,
      "The experiments section must contain the expected experiment id."
    );
    Assert.equal(
      environmentData.experiments[id].branch,
      branch,
      "The experiment branch must be correct."
    );
  };

  // Clean the environment and check that it's reporting the correct info.
  await TelemetryEnvironment.testCleanRestart().onInitialized();
  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  // We don't expect the experiments section to be there if no annotation
  // happened.
  Assert.ok(
    !("experiments" in data),
    "No experiments section must be reported if nothing was annotated."
  );

  // Add a change listener and add an experiment annotation.
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_experimentsAPI",
    (reason, env) => {
      deferred.resolve(env);
    }
  );
  TelemetryEnvironment.setExperimentActive(EXPERIMENT1, EXPERIMENT1_BRANCH);
  let eventEnvironmentData = await deferred.promise;

  // Check that the old environment does not contain the experiments.
  TelemetryEnvironmentTesting.checkEnvironmentData(eventEnvironmentData);
  Assert.ok(
    !("experiments" in eventEnvironmentData),
    "No experiments section must be reported in the old environment."
  );

  // Check that the current environment contains the right experiment.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  checkExperiment(data, EXPERIMENT1, EXPERIMENT1_BRANCH);

  TelemetryEnvironment.unregisterChangeListener("test_experimentsAPI");

  // Add a second annotation and check that both experiments are there.
  deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_experimentsAPI2",
    (reason, env) => {
      deferred.resolve(env);
    }
  );
  TelemetryEnvironment.setExperimentActive(EXPERIMENT2, EXPERIMENT2_BRANCH);
  eventEnvironmentData = await deferred.promise;

  // Check that the current environment contains both the experiment.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  checkExperiment(data, EXPERIMENT1, EXPERIMENT1_BRANCH);
  checkExperiment(data, EXPERIMENT2, EXPERIMENT2_BRANCH);

  // The previous environment should only contain the first experiment.
  checkExperiment(eventEnvironmentData, EXPERIMENT1, EXPERIMENT1_BRANCH);
  Assert.ok(
    !(EXPERIMENT2 in eventEnvironmentData),
    "The old environment must not contain the new experiment annotation."
  );

  TelemetryEnvironment.unregisterChangeListener("test_experimentsAPI2");

  // Check that removing an unknown experiment annotation does not trigger
  // a notification.
  TelemetryEnvironment.registerChangeListener("test_experimentsAPI3", () => {
    Assert.ok(
      false,
      "Removing an unknown experiment annotation must not trigger a change."
    );
  });
  TelemetryEnvironment.setExperimentInactive("unknown-experiment-id");
  // Also make sure that passing non-string parameters arguments doesn't throw nor
  // trigger a notification.
  TelemetryEnvironment.setExperimentActive({}, "some-branch");
  TelemetryEnvironment.setExperimentActive("some-id", {});
  TelemetryEnvironment.unregisterChangeListener("test_experimentsAPI3");

  // Check that removing a known experiment leaves the other in place and triggers
  // a change.
  deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener(
    "test_experimentsAPI4",
    (reason, env) => {
      deferred.resolve(env);
    }
  );
  TelemetryEnvironment.setExperimentInactive(EXPERIMENT1);
  eventEnvironmentData = await deferred.promise;

  // Check that the current environment contains just the second experiment.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.ok(
    !(EXPERIMENT1 in data),
    "The current environment must not contain the removed experiment annotation."
  );
  checkExperiment(data, EXPERIMENT2, EXPERIMENT2_BRANCH);

  // The previous environment should contain both annotations.
  checkExperiment(eventEnvironmentData, EXPERIMENT1, EXPERIMENT1_BRANCH);
  checkExperiment(eventEnvironmentData, EXPERIMENT2, EXPERIMENT2_BRANCH);

  // Set an experiment with a type and check that it correctly shows up.
  TelemetryEnvironment.setExperimentActive(
    "typed-experiment",
    "random-branch",
    { type: "ab-test" }
  );
  data = TelemetryEnvironment.currentEnvironment;
  checkExperiment(data, "typed-experiment", "random-branch", "ab-test");
});

add_task(async function test_experimentsAPI_limits() {
  const EXPERIMENT =
    "experiment-2-experiment-2-experiment-2-experiment-2-experiment-2" +
    "-experiment-2-experiment-2-experiment-2-experiment-2";
  const EXPERIMENT_BRANCH =
    "other-branch-other-branch-other-branch-other-branch-other" +
    "-branch-other-branch-other-branch-other-branch-other-branch";
  const EXPERIMENT_TRUNCATED = EXPERIMENT.substring(0, 100);
  const EXPERIMENT_BRANCH_TRUNCATED = EXPERIMENT_BRANCH.substring(0, 100);

  // Clean the environment and check that it's reporting the correct info.
  await TelemetryEnvironment.testCleanRestart().onInitialized();
  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  // We don't expect the experiments section to be there if no annotation
  // happened.
  Assert.ok(
    !("experiments" in data),
    "No experiments section must be reported if nothing was annotated."
  );

  // Add a change listener and wait for the annotation to happen.
  let deferred = Promise.withResolvers();
  TelemetryEnvironment.registerChangeListener("test_experimentsAPI", () =>
    deferred.resolve()
  );
  TelemetryEnvironment.setExperimentActive(EXPERIMENT, EXPERIMENT_BRANCH);
  await deferred.promise;

  // Check that the current environment contains the truncated values
  // for the experiment data.
  data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);
  Assert.ok(
    "experiments" in data,
    "The environment must contain an experiments section."
  );
  Assert.ok(
    EXPERIMENT_TRUNCATED in data.experiments,
    "The experiments must be reporting the truncated id."
  );
  Assert.ok(
    !(EXPERIMENT in data.experiments),
    "The experiments must not be reporting the full id."
  );
  Assert.equal(
    EXPERIMENT_BRANCH_TRUNCATED,
    data.experiments[EXPERIMENT_TRUNCATED].branch,
    "The experiments must be reporting the truncated branch."
  );

  TelemetryEnvironment.unregisterChangeListener("test_experimentsAPI");

  // Check that an overly long type is truncated.
  const longType = "a0123456678901234567890123456789";
  TelemetryEnvironment.setExperimentActive("exp", "some-branch", {
    type: longType,
  });
  data = TelemetryEnvironment.currentEnvironment;
  Assert.equal(data.experiments.exp.type, longType.substring(0, 20));
});

if (gIsWindows) {
  add_task(async function test_environmentHDDInfo() {
    await TelemetryEnvironment.testCleanRestart().onInitialized();
    let data = TelemetryEnvironment.currentEnvironment;
    let empty = { model: null, revision: null, type: null };
    Assert.deepEqual(
      data.system.hdd,
      { binary: empty, profile: empty, system: empty },
      "Should have no data yet."
    );
    await TelemetryEnvironment.delayedInit();
    data = TelemetryEnvironment.currentEnvironment;
    for (let k of TelemetryEnvironmentTesting.EXPECTED_HDD_FIELDS) {
      TelemetryEnvironmentTesting.checkString(data.system.hdd[k].model);
      TelemetryEnvironmentTesting.checkString(data.system.hdd[k].revision);
      TelemetryEnvironmentTesting.checkString(data.system.hdd[k].type);
    }
  });

  add_task(async function test_environmentProcessInfo() {
    await TelemetryEnvironment.testCleanRestart().onInitialized();
    let data = TelemetryEnvironment.currentEnvironment;
    Assert.deepEqual(data.system.isWow64, null, "Should have no data yet.");
    await TelemetryEnvironment.delayedInit();
    data = TelemetryEnvironment.currentEnvironment;
    Assert.equal(
      typeof data.system.isWow64,
      "boolean",
      "isWow64 must be a boolean."
    );
    Assert.equal(
      typeof data.system.isWowARM64,
      "boolean",
      "isWowARM64 must be a boolean."
    );
    Assert.equal(
      typeof data.system.hasWinPackageId,
      "boolean",
      "hasWinPackageId must be a boolean."
    );
    // This is only sent for Mozilla produced MSIX packages
    Assert.ok(
      !("winPackageFamilyName" in data.system) ||
        data.system.winPackageFamilyName === null ||
        typeof data.system.winPackageFamilyName === "string",
      "winPackageFamilyName must be a string if non null"
    );
    // These should be numbers if they are not null
    for (let f of [
      "count",
      "model",
      "family",
      "stepping",
      "l2cacheKB",
      "l3cacheKB",
      "speedMHz",
      "cores",
    ]) {
      Assert.ok(
        !(f in data.system.cpu) ||
          data.system.cpu[f] === null ||
          Number.isFinite(data.system.cpu[f]),
        f + " must be a number if non null."
      );
    }
    Assert.ok(
      TelemetryEnvironmentTesting.checkString(data.system.cpu.vendor),
      "vendor must be a valid string."
    );
  });

  add_task(async function test_environmentOSInfo() {
    await TelemetryEnvironment.testCleanRestart().onInitialized();
    let data = TelemetryEnvironment.currentEnvironment;
    Assert.deepEqual(
      data.system.os.installYear,
      null,
      "Should have no data yet."
    );
    await TelemetryEnvironment.delayedInit();
    data = TelemetryEnvironment.currentEnvironment;
    Assert.ok(
      Number.isFinite(data.system.os.installYear),
      "Install year must be a number."
    );
  });
}

add_task(
  { skip_if: () => AppConstants.MOZ_APP_NAME == "thunderbird" },
  async function test_environmentServicesInfo() {
    let cache = TelemetryEnvironment.testCleanRestart();
    await cache.onInitialized();
    let oldGetFxaSignedInUser = cache._getFxaSignedInUser;
    try {
      // Test the 'yes to both' case.

      // This makes the weave service return that the usere is definitely a sync user
      Services.prefs.setStringPref(
        "services.sync.username",
        "c00lperson123@example.com"
      );
      let calledFxa = false;
      cache._getFxaSignedInUser = () => {
        calledFxa = true;
        return null;
      };

      await cache._updateServicesInfo();
      ok(
        !calledFxa,
        "Shouldn't need to ask FxA if they're definitely signed in"
      );
      deepEqual(cache.currentEnvironment.services, {
        accountEnabled: true,
        syncEnabled: true,
      });

      // Test the fxa-but-not-sync case.
      Services.prefs.clearUserPref("services.sync.username");
      // We don't actually inspect the returned object, just t
      cache._getFxaSignedInUser = async () => {
        return {};
      };
      await cache._updateServicesInfo();
      deepEqual(cache.currentEnvironment.services, {
        accountEnabled: true,
        syncEnabled: false,
      });
      // Test the "no to both" case.
      cache._getFxaSignedInUser = async () => {
        return null;
      };
      await cache._updateServicesInfo();
      deepEqual(cache.currentEnvironment.services, {
        accountEnabled: false,
        syncEnabled: false,
      });
      // And finally, the 'fxa is in an error state' case.
      cache._getFxaSignedInUser = () => {
        throw new Error("You'll never know");
      };
      await cache._updateServicesInfo();
      equal(cache.currentEnvironment.services, null);
    } finally {
      cache._getFxaSignedInUser = oldGetFxaSignedInUser;
      Services.prefs.clearUserPref("services.sync.username");
    }
  }
);

add_task(async function test_normandyTestPrefsGoneAfter91() {
  const testPrefBool = "app.normandy.test-prefs.bool";
  const testPrefInteger = "app.normandy.test-prefs.integer";
  const testPrefString = "app.normandy.test-prefs.string";

  Services.prefs.setBoolPref(testPrefBool, true);
  Services.prefs.setIntPref(testPrefInteger, 10);
  Services.prefs.setCharPref(testPrefString, "test-string");

  const data = TelemetryEnvironment.currentEnvironment;

  if (Services.vc.compare(data.build.version, "91") > 0) {
    Assert.equal(
      data.settings.userPrefs["app.normandy.test-prefs.bool"],
      null,
      "This probe should expire in FX91. bug 1686105 "
    );
    Assert.equal(
      data.settings.userPrefs["app.normandy.test-prefs.integer"],
      null,
      "This probe should expire in FX91. bug 1686105 "
    );
    Assert.equal(
      data.settings.userPrefs["app.normandy.test-prefs.string"],
      null,
      "This probe should expire in FX91. bug 1686105 "
    );
  }
});

add_task(async function test_environmentShutdown() {
  // Define and reset the test preference.
  const PREF_TEST = "toolkit.telemetry.test.pref1";
  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_STATE }],
  ]);
  Services.prefs.clearUserPref(PREF_TEST);

  // Set up the preferences and listener, then the trigger shutdown
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);
  TelemetryEnvironment.registerChangeListener(
    "test_environmentShutdownChange",
    () => {
      // Register a new change listener that asserts if change is propogated
      Assert.ok(false, "No change should be propagated after shutdown.");
    }
  );
  TelemetryEnvironment.shutdown();

  // Flipping  the test preference after shutdown should not trigger the listener
  Services.prefs.setIntPref(PREF_TEST, 1);

  // Unregister the listener.
  TelemetryEnvironment.unregisterChangeListener(
    "test_environmentShutdownChange"
  );
});

add_task(async function test_environmentDidntChange() {
  // Clean the environment and check that it's reporting the correct info.
  await TelemetryEnvironment.testCleanRestart().onInitialized();
  let data = TelemetryEnvironment.currentEnvironment;
  TelemetryEnvironmentTesting.checkEnvironmentData(data);

  const LISTENER_NAME = "test_environmentDidntChange";
  TelemetryEnvironment.registerChangeListener(LISTENER_NAME, () => {
    Assert.ok(false, "The environment didn't actually change.");
  });

  // Don't actually change the environment, but notify of a compositor abort.
  const COMPOSITOR_ABORTED_TOPIC = "compositor:process-aborted";
  Services.obs.notifyObservers(null, COMPOSITOR_ABORTED_TOPIC);

  TelemetryEnvironment.unregisterChangeListener(LISTENER_NAME);
});
