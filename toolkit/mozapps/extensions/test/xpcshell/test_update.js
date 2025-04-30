/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This verifies that add-on update checks work

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

// The test extension uses an insecure update url.
Services.prefs.setBoolPref(PREF_EM_CHECK_UPDATE_SECURITY, false);
// This test uses add-on versions that follow the toolkit version but we
// started to encourage the use of a simpler format in Bug 1793925. We disable
// the pref below to avoid install errors.
Services.prefs.setBoolPref(
  "extensions.webextensions.warnings-as-errors",
  false
);

const updateFile = "test_update.json";

const profileDir = gProfD.clone();
profileDir.append("extensions");

const ADDONS = {
  test_update: {
    id: "addon1@tests.mozilla.org",
    version: "2.0",
    name: "Test 1",
  },
  test_update8: {
    id: "addon8@tests.mozilla.org",
    version: "2.0",
    name: "Test 8",
  },
  test_update12: {
    id: "addon12@tests.mozilla.org",
    version: "2.0",
    name: "Test 12",
  },
  test_install2_1: {
    id: "addon2@tests.mozilla.org",
    version: "2.0",
    name: "Real Test 2",
  },
  test_install2_2: {
    id: "addon2@tests.mozilla.org",
    version: "3.0",
    name: "Real Test 3",
  },
};

var testserver = createHttpServer({ hosts: ["example.com"] });
testserver.registerDirectory("/data/", do_get_file("data"));

const XPIS = {};

add_setup(async function setup() {
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1");

  Services.locale.requestedLocales = ["fr-FR"];

  for (let [name, info] of Object.entries(ADDONS)) {
    XPIS[name] = createTempWebExtensionFile({
      manifest: {
        name: info.name,
        version: info.version,
        browser_specific_settings: { gecko: { id: info.id } },
      },
    });
    testserver.registerFile(`/addons/${name}.xpi`, XPIS[name]);
  }

  AddonTestUtils.updateReason = AddonManager.UPDATE_WHEN_USER_REQUESTED;

  await promiseStartupManager();
});

// Verify that an update is available and can be installed.
add_task(async function test_apply_update() {
  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 1",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon1@tests.mozilla.org",
          update_url: `http://example.com/data/${updateFile}`,
        },
      },
    },
  });

  let a1 = await AddonManager.getAddonByID("addon1@tests.mozilla.org");
  notEqual(a1, null);
  equal(a1.version, "1.0");
  equal(a1.applyBackgroundUpdates, AddonManager.AUTOUPDATE_DEFAULT);
  equal(a1.releaseNotesURI, null);
  notEqual(a1.syncGUID, null);

  let originalSyncGUID = a1.syncGUID;

  await expectEvents(
    {
      ignorePlugins: true,
      addonEvents: {
        "addon1@tests.mozilla.org": [
          {
            event: "onPropertyChanged",
            properties: ["applyBackgroundUpdates"],
          },
        ],
      },
    },
    async () => {
      a1.applyBackgroundUpdates = AddonManager.AUTOUPDATE_DISABLE;
    }
  );

  a1.applyBackgroundUpdates = AddonManager.AUTOUPDATE_DISABLE;

  let install;
  await expectEvents(
    {
      ignorePlugins: true,
      installEvents: [{ event: "onNewInstall" }],
    },
    async () => {
      ({ updateAvailable: install } =
        await AddonTestUtils.promiseFindAddonUpdates(a1));
    }
  );

  let installs = await AddonManager.getAllInstalls();
  equal(installs.length, 1);
  equal(installs[0], install);

  equal(install.name, a1.name);
  equal(install.version, "2.0");
  equal(install.state, AddonManager.STATE_AVAILABLE);
  equal(install.existingAddon, a1);
  equal(install.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");

  // Verify that another update check returns the same AddonInstall
  let { updateAvailable: install2 } =
    await AddonTestUtils.promiseFindAddonUpdates(a1);

  installs = await AddonManager.getAllInstalls();
  equal(installs.length, 1);
  equal(installs[0], install);
  equal(install2, install);

  await expectEvents(
    {
      ignorePlugins: true,
      installEvents: [
        { event: "onDownloadStarted" },
        { event: "onDownloadEnded", returnValue: false },
      ],
    },
    () => {
      install.install();
    }
  );

  equal(install.state, AddonManager.STATE_DOWNLOADED);

  // Continue installing the update.
  // Verify that another update check returns no new update
  let { updateAvailable } = await AddonTestUtils.promiseFindAddonUpdates(
    install.existingAddon
  );

  ok(
    !updateAvailable,
    "Should find no available updates when one is already downloading"
  );

  installs = await AddonManager.getAllInstalls();
  equal(installs.length, 1);
  equal(installs[0], install);

  await expectEvents(
    {
      ignorePlugins: true,
      addonEvents: {
        "addon1@tests.mozilla.org": [
          { event: "onInstalling" },
          { event: "onInstalled" },
        ],
      },
      installEvents: [
        { event: "onInstallStarted" },
        { event: "onInstallEnded" },
      ],
    },
    () => {
      install.install();
    }
  );

  await AddonTestUtils.loadAddonsList(true);

  // Grab the current time so we can check the mtime of the add-on below
  // without worrying too much about how long other tests take.
  let startupTime = Date.now();

  ok(isExtensionInBootstrappedList(profileDir, "addon1@tests.mozilla.org"));

  a1 = await AddonManager.getAddonByID("addon1@tests.mozilla.org");
  notEqual(a1, null);
  equal(a1.version, "2.0");
  ok(isExtensionInBootstrappedList(profileDir, a1.id));
  equal(a1.applyBackgroundUpdates, AddonManager.AUTOUPDATE_DISABLE);
  equal(a1.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");
  notEqual(a1.syncGUID, null);
  equal(originalSyncGUID, a1.syncGUID);

  // Make sure that the extension lastModifiedTime was updated.
  let testFile = getAddonFile(a1);
  let difference = testFile.lastModifiedTime - startupTime;
  Assert.less(Math.abs(difference), MAX_TIME_DIFFERENCE);

  await a1.uninstall();
});

// Check that an update check finds compatibility updates and applies them
add_task(async function test_compat_update() {
  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 2",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon2@tests.mozilla.org",
          update_url: "http://example.com/data/" + updateFile,
          strict_max_version: "0",
        },
      },
    },
  });

  let a2 = await AddonManager.getAddonByID("addon2@tests.mozilla.org");
  notEqual(a2, null);
  ok(a2.isActive);
  ok(a2.isCompatible);
  ok(!a2.appDisabled);
  ok(a2.isCompatibleWith("0", "0"));

  let result = await AddonTestUtils.promiseFindAddonUpdates(a2);
  ok(result.compatibilityUpdate, "Should have seen a compatibility update");
  ok(!result.updateAvailable, "Should not have seen a version update");

  ok(a2.isCompatible);
  ok(!a2.appDisabled);
  ok(a2.isActive);

  await promiseRestartManager();

  a2 = await AddonManager.getAddonByID("addon2@tests.mozilla.org");
  notEqual(a2, null);
  ok(a2.isActive);
  ok(a2.isCompatible);
  ok(!a2.appDisabled);
  await a2.uninstall();
});

// Checks that we see no compatibility information when there is none.
add_task(async function test_no_compat() {
  gAppInfo.platformVersion = "5";
  await promiseRestartManager("5");
  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 3",
      browser_specific_settings: {
        gecko: {
          id: "addon3@tests.mozilla.org",
          update_url: `http://example.com/data/${updateFile}`,
          strict_min_version: "5",
        },
      },
    },
  });

  gAppInfo.platformVersion = "1";
  await promiseRestartManager("1");

  let a3 = await AddonManager.getAddonByID("addon3@tests.mozilla.org");
  notEqual(a3, null);
  ok(!a3.isActive);
  ok(!a3.isCompatible);
  ok(a3.appDisabled);
  ok(a3.isCompatibleWith("5", "5"));
  ok(!a3.isCompatibleWith("2", "2"));

  let result = await AddonTestUtils.promiseFindAddonUpdates(a3);
  ok(
    !result.compatibilityUpdate,
    "Should not have seen a compatibility update"
  );
  ok(!result.updateAvailable, "Should not have seen a version update");
});

// Checks that compatibility info for future apps are detected but don't make
// the item compatibile.
add_task(async function test_future_compat() {
  let a3 = await AddonManager.getAddonByID("addon3@tests.mozilla.org");
  notEqual(a3, null);
  ok(!a3.isActive);
  ok(!a3.isCompatible);
  ok(a3.appDisabled);
  ok(a3.isCompatibleWith("5", "5"));
  ok(!a3.isCompatibleWith("2", "2"));

  let result = await AddonTestUtils.promiseFindAddonUpdates(
    a3,
    undefined,
    "3.0",
    "3.0"
  );
  ok(result.compatibilityUpdate, "Should have seen a compatibility update");
  ok(!result.updateAvailable, "Should not have seen a version update");

  ok(!a3.isActive);
  ok(!a3.isCompatible);
  ok(a3.appDisabled);

  await promiseRestartManager();

  a3 = await AddonManager.getAddonByID("addon3@tests.mozilla.org");
  notEqual(a3, null);
  ok(!a3.isActive);
  ok(!a3.isCompatible);
  ok(a3.appDisabled);

  await a3.uninstall();
});

// Test that background update checks work
add_task(async function test_background_update() {
  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 1",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon1@tests.mozilla.org",
          update_url: `http://example.com/data/${updateFile}`,
          strict_min_version: "1",
          strict_max_version: "1",
        },
      },
    },
  });

  function checkInstall(install) {
    notEqual(install.existingAddon, null);
    equal(install.existingAddon.id, "addon1@tests.mozilla.org");
  }

  await expectEvents(
    {
      ignorePlugins: true,
      addonEvents: {
        "addon1@tests.mozilla.org": [
          { event: "onInstalling" },
          { event: "onInstalled" },
        ],
      },
      installEvents: [
        { event: "onNewInstall" },
        { event: "onDownloadStarted" },
        { event: "onDownloadEnded", callback: checkInstall },
        { event: "onInstallStarted" },
        { event: "onInstallEnded" },
      ],
    },
    () => {
      AddonManagerPrivate.backgroundUpdateCheck();
    }
  );

  let a1 = await AddonManager.getAddonByID("addon1@tests.mozilla.org");
  notEqual(a1, null);
  equal(a1.version, "2.0");
  equal(a1.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");

  await a1.uninstall();
});

const STATE_BLOCKED = Ci.nsIBlocklistService.STATE_BLOCKED;

const PARAMS =
  "?" +
  [
    "req_version=%REQ_VERSION%",
    "item_id=%ITEM_ID%",
    "item_version=%ITEM_VERSION%",
    "item_maxappversion=%ITEM_MAXAPPVERSION%",
    "item_status=%ITEM_STATUS%",
    "app_id=%APP_ID%",
    "app_version=%APP_VERSION%",
    "current_app_version=%CURRENT_APP_VERSION%",
    "app_os=%APP_OS%",
    "app_abi=%APP_ABI%",
    "app_locale=%APP_LOCALE%",
    "update_type=%UPDATE_TYPE%",
  ].join("&");

const PARAM_ADDONS = {
  "addon1@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 1",
      version: "5.0",
      browser_specific_settings: {
        gecko: {
          id: "addon1@tests.mozilla.org",
          update_url: `http://example.com/data/param_test.json${PARAMS}`,
          strict_min_version: "1",
          strict_max_version: "2",
        },
      },
    },
    params: {
      item_version: "5.0",
      item_maxappversion: "2",
      item_status: "userEnabled",
      app_version: "1",
      update_type: "97",
    },
    updateType: [AddonManager.UPDATE_WHEN_USER_REQUESTED],
  },

  "addon2@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 2",
      version: "67.0.5b1",
      browser_specific_settings: {
        gecko: {
          id: "addon2@tests.mozilla.org",
          update_url: "http://example.com/data/param_test.json" + PARAMS,
          strict_min_version: "0",
          strict_max_version: "3",
        },
      },
    },
    initialState: {
      userDisabled: true,
    },
    params: {
      item_version: "67.0.5b1",
      item_maxappversion: "3",
      item_status: "userDisabled",
      app_version: "1",
      update_type: "49",
    },
    updateType: [AddonManager.UPDATE_WHEN_ADDON_INSTALLED],
    compatOnly: true,
  },

  "addon3@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 3",
      version: "1.3+",
      browser_specific_settings: {
        gecko: {
          id: "addon3@tests.mozilla.org",
          update_url: `http://example.com/data/param_test.json${PARAMS}`,
        },
      },
    },
    params: {
      item_version: "1.3+",
      item_status: "userEnabled",
      app_version: "1",
      update_type: "112",
    },
    updateType: [AddonManager.UPDATE_WHEN_PERIODIC_UPDATE],
  },

  "addon4@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 4",
      version: "0.5ab6",
      browser_specific_settings: {
        gecko: {
          id: "addon4@tests.mozilla.org",
          update_url: `http://example.com/data/param_test.json${PARAMS}`,
          strict_min_version: "1",
          strict_max_version: "5",
        },
      },
    },
    params: {
      item_version: "0.5ab6",
      item_maxappversion: "5",
      item_status: "userEnabled",
      app_version: "2",
      update_type: "98",
    },
    updateType: [AddonManager.UPDATE_WHEN_NEW_APP_DETECTED, "2"],
  },

  "addon5@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 5",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon5@tests.mozilla.org",
          update_url: `http://example.com/data/param_test.json${PARAMS}`,
          strict_min_version: "1",
          strict_max_version: "1",
        },
      },
    },
    params: {
      item_version: "1.0",
      item_maxappversion: "1",
      item_status: "userEnabled",
      app_version: "1",
      update_type: "35",
    },
    updateType: [AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED],
    compatOnly: true,
  },

  "addon6@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 6",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon6@tests.mozilla.org",
          update_url: `http://example.com/data/param_test.json${PARAMS}`,
          strict_min_version: "1",
          strict_max_version: "1",
        },
      },
    },
    params: {
      item_version: "1.0",
      item_maxappversion: "1",
      item_status: "userEnabled",
      app_version: "1",
      update_type: "99",
    },
    updateType: [AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED],
  },

  "blocklist2@tests.mozilla.org": {
    manifest: {
      name: "Test Addon 1",
      version: "5.0",
      browser_specific_settings: {
        gecko: {
          id: "blocklist2@tests.mozilla.org",
          update_url: `http://example.com/data/param_test.json${PARAMS}`,
          strict_min_version: "1",
          strict_max_version: "2",
        },
      },
    },
    params: {
      item_version: "5.0",
      item_maxappversion: "2",
      item_status: "userEnabled,blocklisted",
      app_version: "1",
      update_type: "97",
    },
    updateType: [AddonManager.UPDATE_WHEN_USER_REQUESTED],
    blocklistState: STATE_BLOCKED,
  },
};

const PARAM_IDS = Object.keys(PARAM_ADDONS);

// Verify the parameter escaping in update urls.
add_task(async function test_params() {
  let blocked = [];
  for (let [id, options] of Object.entries(PARAM_ADDONS)) {
    if (options.blocklistState == STATE_BLOCKED) {
      blocked.push(`${id}:${options.manifest.version}`);
    }
  }
  let extensionsMLBF = [{ stash: { blocked, unblocked: [] }, stash_time: 0 }];
  await AddonTestUtils.loadBlocklistRawData({ extensionsMLBF });

  for (let [id, options] of Object.entries(PARAM_ADDONS)) {
    await promiseInstallWebExtension({ manifest: options.manifest });

    if (options.initialState) {
      let addon = await AddonManager.getAddonByID(id);
      await setInitialState(addon, options.initialState);
    }
  }

  let resultsPromise = new Promise(resolve => {
    let results = new Map();

    testserver.registerPathHandler("/data/param_test.json", function (request) {
      let params = new URLSearchParams(request.queryString);
      let itemId = params.get("item_id");
      ok(
        !results.has(itemId),
        `Should not see a duplicate request for item ${itemId}`
      );

      results.set(itemId, params);

      if (results.size === PARAM_IDS.length) {
        resolve(results);
      }

      request.setStatusLine(null, 500, "Server Error");
    });
  });

  let addons = await getAddons(PARAM_IDS);
  for (let [id, options] of Object.entries(PARAM_ADDONS)) {
    // Having an onUpdateAvailable callback in the listener automagically adds
    // UPDATE_TYPE_NEWVERSION to the update type flags in the request.
    let listener = options.compatOnly ? {} : { onUpdateAvailable() {} };

    addons.get(id).findUpdates(listener, ...options.updateType);
  }

  let baseParams = {
    req_version: "2",
    app_id: "xpcshell@tests.mozilla.org",
    current_app_version: "1",
    app_os: "XPCShell",
    app_abi: "noarch-spidermonkey",
    app_locale: "fr-FR",
  };

  let results = await resultsPromise;
  for (let [id, options] of Object.entries(PARAM_ADDONS)) {
    info(`Checking update params for ${id}`);

    let expected = Object.assign({}, baseParams, options.params);
    let params = results.get(id);

    for (let [prop, value] of Object.entries(expected)) {
      equal(params.get(prop), value, `Expected value for ${prop}`);
    }
  }

  for (let [, addon] of await getAddons(PARAM_IDS)) {
    await addon.uninstall();
  }
});

// Tests that if a manifest claims compatibility then the add-on will be
// seen as compatible regardless of what the update payload says.
add_task(async function test_manifest_compat() {
  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 1",
      version: "5.0",
      browser_specific_settings: {
        gecko: {
          id: "addon4@tests.mozilla.org",
          update_url: `http://example.com/data/${updateFile}`,
          strict_min_version: "0",
          strict_max_version: "1",
        },
      },
    },
  });

  let a4 = await AddonManager.getAddonByID("addon4@tests.mozilla.org");
  ok(a4.isActive, "addon4 is active");
  ok(a4.isCompatible, "addon4 is compatible");

  // Test that a normal update check won't decrease a targetApplication's
  // maxVersion but an update check for a new application will.
  await AddonTestUtils.promiseFindAddonUpdates(
    a4,
    AddonManager.UPDATE_WHEN_PERIODIC_UPDATE
  );
  ok(a4.isActive, "addon4 is active");
  ok(a4.isCompatible, "addon4 is compatible");

  await AddonTestUtils.promiseFindAddonUpdates(
    a4,
    AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED
  );
  ok(!a4.isActive, "addon4 is not active");
  ok(!a4.isCompatible, "addon4 is not compatible");

  await a4.uninstall();
});

// Test that the background update check doesn't update an add-on that isn't
// allowed to update automatically.
add_task(async function test_no_auto_update() {
  // Have an add-on there that will be updated so we see some events from it
  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 1",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon1@tests.mozilla.org",
          update_url: `http://example.com/data/${updateFile}`,
        },
      },
    },
  });

  await promiseInstallWebExtension({
    manifest: {
      name: "Test Addon 8",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: "addon8@tests.mozilla.org",
          update_url: `http://example.com/data/${updateFile}`,
        },
      },
    },
  });

  let a8 = await AddonManager.getAddonByID("addon8@tests.mozilla.org");
  a8.applyBackgroundUpdates = AddonManager.AUTOUPDATE_DISABLE;

  // The background update check will find updates for both add-ons but only
  // proceed to install one of them.
  let listener;
  await new Promise(resolve => {
    listener = {
      onNewInstall(aInstall) {
        let id = aInstall.existingAddon.id;
        ok(
          id == "addon1@tests.mozilla.org" || id == "addon8@tests.mozilla.org",
          "Saw unexpected onNewInstall for " + id
        );
      },

      onDownloadStarted(aInstall) {
        equal(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
      },

      onDownloadEnded(aInstall) {
        equal(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
      },

      onDownloadFailed() {
        ok(false, "Should not have seen onDownloadFailed event");
      },

      onDownloadCancelled() {
        ok(false, "Should not have seen onDownloadCancelled event");
      },

      onInstallStarted(aInstall) {
        equal(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
      },

      onInstallEnded(aInstall) {
        equal(aInstall.existingAddon.id, "addon1@tests.mozilla.org");

        resolve();
      },

      onInstallFailed() {
        ok(false, "Should not have seen onInstallFailed event");
      },

      onInstallCancelled() {
        ok(false, "Should not have seen onInstallCancelled event");
      },
    };
    AddonManager.addInstallListener(listener);
    AddonManagerPrivate.backgroundUpdateCheck();
  });
  AddonManager.removeInstallListener(listener);

  let a1;
  [a1, a8] = await AddonManager.getAddonsByIDs([
    "addon1@tests.mozilla.org",
    "addon8@tests.mozilla.org",
  ]);
  notEqual(a1, null);
  equal(a1.version, "2.0");
  await a1.uninstall();

  notEqual(a8, null);
  equal(a8.version, "1.0");
  await a8.uninstall();
});

// Test that the update check returns nothing for addons in locked install
// locations.
add_task(async function run_test_locked_install() {
  const ADDON_ID = "addon13@tests.mozilla.org";
  const testExtensionProps = {
    manifest: {
      name: "Test Addon 13",
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: ADDON_ID,
          update_url: "http://example.com/data/test_update.json",
        },
      },
    },
  };

  await promiseShutdownManager();

  await setupBuiltinExtension(testExtensionProps, "builtin-addon13-ext");
  await overrideBuiltIns({
    builtins: [
      {
        addon_id: ADDON_ID,
        addon_version: "1.0",
        res_url: "resource://builtin-addon13-ext/",
      },
    ],
  });

  await promiseStartupManager();

  let a13 = await AddonManager.getAddonByID(ADDON_ID);
  notEqual(a13, null);

  equal(
    a13.getResourceURI().spec,
    "resource://builtin-addon13-ext/",
    "Expect addon root uri to be a resource:// URI"
  );

  let result = await AddonTestUtils.promiseFindAddonUpdates(a13);
  ok(
    !result.compatibilityUpdate,
    "Should not have seen a compatibility update"
  );
  ok(!result.updateAvailable, "Should not have seen a version update");

  let installs = await AddonManager.getAllInstalls();
  equal(installs.length, 0);
});
