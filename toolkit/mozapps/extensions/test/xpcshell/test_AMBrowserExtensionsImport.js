/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { AMBrowserExtensionsImport } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const mockAddonRepository = ({
  addons = [],
  expectedBrowserID = null,
  expectedExtensionIDs = null,
  matchedIDs = [],
  unmatchedIDs = [],
}) => {
  return {
    async getMappedAddons(browserID, extensionIDs) {
      if (expectedBrowserID) {
        Assert.equal(browserID, expectedBrowserID, "expected browser ID");
      }
      if (expectedExtensionIDs) {
        Assert.deepEqual(
          extensionIDs,
          expectedExtensionIDs,
          "expected extension IDs"
        );
      }

      return Promise.resolve({
        addons,
        matchedIDs,
        unmatchedIDs,
      });
    },
  };
};

const assertStageInstallsResult = (result, importedAddonIDs) => {
  // Sort the results to always assert the elements in the same order.
  result.importedAddonIDs.sort();
  Assert.deepEqual(result, { importedAddonIDs }, "expected results");
  Assert.ok(
    AMBrowserExtensionsImport.hasPendingImportedAddons,
    "expected pending imported add-ons"
  );
};

const cancelInstalls = async importedAddonIDs => {
  const promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-cancelled"
  );
  // We want to verify that we received a `onInstallCancelled` event per
  // (cancelled) install (i.e. per imported add-on).
  const cancelledPromises = importedAddonIDs.map(id =>
    AddonTestUtils.promiseInstallEvent(
      "onInstallCancelled",
      (install, cancelledByUser) => {
        Assert.equal(cancelledByUser, false, "Not user-cancelled");
        return install.addon.id == id;
      }
    )
  );
  await AMBrowserExtensionsImport.cancelInstalls();
  await Promise.all([promiseTopic, ...cancelledPromises]);
  Assert.ok(
    !AMBrowserExtensionsImport.hasPendingImportedAddons,
    "expected no pending imported add-ons"
  );
};

const TEST_SERVER = createHttpServer({ hosts: ["example.com"] });

const ADDONS = {
  ext1: {
    manifest: {
      name: "Ext 1",
      version: "1.0",
      browser_specific_settings: { gecko: { id: "ff@ext-1" } },
    },
  },
  ext2: {
    manifest: {
      name: "Ext 2",
      version: "1.0",
      browser_specific_settings: { gecko: { id: "ff@ext-2" } },
    },
  },
};
// Populated in `setup()`.
const XPIS = {};
// Populated in `setup()`.
const ADDON_SEARCH_RESULTS = {};

AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

add_setup(async function setup() {
  for (const [name, data] of Object.entries(ADDONS)) {
    XPIS[name] = AddonTestUtils.createTempWebExtensionFile(data);
    TEST_SERVER.registerFile(`/addons/${name}.xpi`, XPIS[name]);

    ADDON_SEARCH_RESULTS[name] = {
      id: data.manifest.browser_specific_settings.gecko.id,
      name: data.manifest.name,
      version: data.manifest.version,
      sourceURI: Services.io.newURI(`http://example.com/addons/${name}.xpi`),
      type: "extension",
      icons: {},
    };
  }

  await AddonTestUtils.promiseStartupManager();

  // FOG needs a profile directory to put its data in.
  const profileDir = do_get_profile();

  // FOG needs to be initialized in order for data to flow.
  Services.fog.initializeFOG();

  // When we stage installs and then cancel them, `XPIInstall` won't be able to
  // remove the staging directory (which is expected to be empty) until the
  // next restart. This causes an `AddonTestUtils` assertion to fail because we
  // don't expect any staging directory at the end of the tests. That's why we
  // remove this directory in the cleanup function defined below.
  //
  // We only remove the staging directory and that will only works if the
  // directory is empty, otherwise an unchaught error will be thrown (on
  // purpose).
  registerCleanupFunction(() => {
    const stagingDir = profileDir.clone();
    stagingDir.append("extensions");
    stagingDir.append("staged");
    stagingDir.exists() && stagingDir.remove(/* recursive */ false);

    // Clear the add-on repository override.
    AMBrowserExtensionsImport._addonRepository = null;
  });
});

add_task(async function test_stage_and_complete_installs() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });
  const importedAddonIDs = ["ff@ext-1", "ff@ext-2"];

  let promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  assertStageInstallsResult(result, importedAddonIDs);

  // Make sure the prompt handler is the one from `AMBrowserExtensionsImport`
  // since we don't want to show a permission prompt during an import.
  for (const install of AMBrowserExtensionsImport._pendingInstallsMap.values()) {
    Assert.equal(
      install.promptHandler,
      AMBrowserExtensionsImport._installPromptHandler,
      "expected prompt handler to be the one set by AMBrowserExtensionsImport"
    );
  }

  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-complete"
  );
  const endedPromises = importedAddonIDs.map(id =>
    AddonTestUtils.promiseInstallEvent(
      "onInstallEnded",
      install => install.addon.id == id
    )
  );
  await AMBrowserExtensionsImport.completeInstalls();
  await Promise.all([promiseTopic, ...endedPromises]);

  Assert.ok(
    !AMBrowserExtensionsImport.hasPendingImportedAddons,
    "expected no pending imported add-ons"
  );
  Assert.ok(
    !AMBrowserExtensionsImport._canCompleteOrCancelInstalls &&
      !AMBrowserExtensionsImport._importInProgress,
    "expected internal state to be consistent"
  );

  for (const id of importedAddonIDs) {
    const addon = await AddonManager.getAddonByID(id);
    Assert.ok(addon.isActive, `expected add-on "${id}" to be enabled`);
    await addon.uninstall();
  }
});

add_task(async function test_stage_and_cancel_installs() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });
  const importedAddonIDs = ["ff@ext-1", "ff@ext-2"];

  const promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  assertStageInstallsResult(result, importedAddonIDs);

  await cancelInstalls(importedAddonIDs);
});

add_task(async function test_stageInstalls_telemetry() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  const unmatchedIDs = ["unmatched-1", "unmatched-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
    matchedIDs: ["ext-1", "ext-2"],
    unmatchedIDs,
  });
  const importedAddonIDs = ["ff@ext-1", "ff@ext-2"];

  const promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  assertStageInstallsResult(result, importedAddonIDs);

  Assert.deepEqual(
    Glean.browserMigration.matchedExtensions.testGetValue(),
    extensionIDs
  );
  Assert.deepEqual(
    Glean.browserMigration.unmatchedExtensions.testGetValue(),
    unmatchedIDs
  );

  await cancelInstalls(importedAddonIDs);
});

add_task(async function test_call_stageInstalls_twice() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    // Only return one extension.
    addons: Object.values(ADDON_SEARCH_RESULTS).slice(0, 1),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });
  const importedAddonIDs = ["ff@ext-1"];

  const promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  let result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  assertStageInstallsResult(result, importedAddonIDs);

  await Assert.rejects(
    AMBrowserExtensionsImport.stageInstalls(browserID, []),
    /Cannot stage installs because there are pending imported add-ons/,
    "expected rejection because there are pending imported add-ons"
  );

  // Cancel the installs for the previous import.
  await cancelInstalls(importedAddonIDs);

  // We should now be able to stage installs again.
  result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  assertStageInstallsResult(result, importedAddonIDs);

  await cancelInstalls(importedAddonIDs);
});

add_task(async function test_call_stageInstalls_no_addons() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-123456"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    // Returns no mapped add-ons.
    addons: [],
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });

  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );

  Assert.deepEqual(result, { importedAddonIDs: [] }, "expected result");
  Assert.ok(
    !AMBrowserExtensionsImport.hasPendingImportedAddons,
    "expected no pending imported add-ons"
  );
  Assert.ok(
    !AMBrowserExtensionsImport._canCompleteOrCancelInstalls &&
      !AMBrowserExtensionsImport._importInProgress,
    "expected internal state to be consistent"
  );
});

add_task(async function test_import_twice() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });
  const importedAddonIDs = ["ff@ext-1", "ff@ext-2"];

  let promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  let result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  assertStageInstallsResult(result, importedAddonIDs);

  // Finalize the installs.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-complete"
  );
  const endedPromises = importedAddonIDs.map(id =>
    AddonTestUtils.promiseInstallEvent(
      "onInstallEnded",
      install => install.addon.id == id
    )
  );
  await AMBrowserExtensionsImport.completeInstalls();
  await Promise.all([promiseTopic, ...endedPromises]);

  // Try to import the same add-ons again. Because these add-ons are already
  // installed, we shouldn't re-import them again.
  result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  Assert.deepEqual(result, { importedAddonIDs: [] }, "expected result");
  Assert.ok(
    !AMBrowserExtensionsImport.hasPendingImportedAddons,
    "expected no pending imported add-ons"
  );
  Assert.ok(
    !AMBrowserExtensionsImport._canCompleteOrCancelInstalls &&
      !AMBrowserExtensionsImport._importInProgress,
    "expected internal state to be consistent"
  );

  for (const id of importedAddonIDs) {
    const addon = await AddonManager.getAddonByID(id);
    Assert.ok(addon.isActive, `expected add-on "${id}" to be enabled`);
    await addon.uninstall();
  }
});

add_task(async function test_call_cancelInstalls_without_pending_import() {
  await Assert.rejects(
    AMBrowserExtensionsImport.cancelInstalls(),
    /No import in progress/,
    "expected an error"
  );
});

add_task(async function test_call_completeInstalls_without_pending_import() {
  await Assert.rejects(
    AMBrowserExtensionsImport.completeInstalls(),
    /No import in progress/,
    "expected an error"
  );
});

add_task(async function test_stage_installs_with_download_aborted() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });
  const importedAddonIDs = ["ff@ext-2"];

  // This listener will be triggered once (for the first imported add-on). Its
  // goal is to cancel the download of an imported add-on and make sure it
  // doesn't break everything. We still expect the second add-on to import to
  // be staged for install.
  const onNewInstall = AddonTestUtils.promiseInstallEvent(
    "onNewInstall",
    install => {
      install.addListener({
        onDownloadStarted: () => false,
      });
      return true;
    }
  );
  const promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await Promise.all([onNewInstall, promiseTopic]);
  assertStageInstallsResult(result, importedAddonIDs);

  Assert.ok(
    AMBrowserExtensionsImport.hasPendingImportedAddons,
    "expected pending imported add-ons"
  );
  Assert.ok(
    AMBrowserExtensionsImport._canCompleteOrCancelInstalls &&
      AMBrowserExtensionsImport._importInProgress,
    "expected internal state to be consistent"
  );

  // Let's cancel the pending installs.
  await cancelInstalls(importedAddonIDs);
});

add_task(async function test_stage_installs_validate_addon_details() {
  const testCases = [
    [
      { sourceURI: ADDON_SEARCH_RESULTS.ext2.sourceURI },
      AddonManager.ERROR_INCORRECT_ID,
      "Importing extension with incorrect ID",
    ],
    [
      { type: "theme" },
      AddonManager.ERROR_UNEXPECTED_ADDON_TYPE,
      "Importing extension with incorrect type",
    ],
    [
      { version: "999.0" },
      AddonManager.ERROR_UNEXPECTED_ADDON_VERSION,
      "Importing extension with incorrect version",
    ],
    [{}, 0, "Importing valid extension"],
  ];

  for (const [wrongAddonDetails, expectedError, description] of testCases) {
    const descriptionPrefix = `[${description}]: `;
    AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
      addons: [
        {
          ...ADDON_SEARCH_RESULTS.ext1,
          ...wrongAddonDetails,
        },
      ],
    });

    const newInstall = AddonTestUtils.promiseInstallEvent("onNewInstall");
    const installEnded = AddonTestUtils.promiseInstallEvent("onInstallEnded");
    const result = await AMBrowserExtensionsImport.stageInstalls(
      "some-browser-id",
      ["ext-1"]
    );
    const [install] = await newInstall;

    if (!expectedError) {
      await Promise.all([
        installEnded,
        AMBrowserExtensionsImport.completeInstalls(),
      ]);
    }

    Assert.equal(
      install.error,
      expectedError,
      descriptionPrefix + "Expected error (if any) given"
    );
    Assert.deepEqual(
      result,
      { importedAddonIDs: expectedError ? [] : ["ff@ext-1"] },
      descriptionPrefix + "Correct add-on IDs imported"
    );

    const addon = await AddonManager.getAddonByID("ff@ext-1");
    Assert.equal(
      !!addon?.isActive,
      !expectedError,
      descriptionPrefix + "Extension is/isn't installed and enabled"
    );
    await addon?.uninstall();
  }
});

add_task(async function test_stageInstalls_then_restart_addonManager() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  const EXPECTED_SOURCE_URI_SPECS = {
    ["ff@ext-1"]: "http://example.com/addons/ext1.xpi",
    ["ff@ext-2"]: "http://example.com/addons/ext2.xpi",
  };
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
    expectedBrowserID: browserID,
    expectedExtensionIDs: extensionIDs,
  });
  const importedAddonIDs = ["ff@ext-1", "ff@ext-2"];

  let promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  let result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  assertStageInstallsResult(result, importedAddonIDs);

  // We restart the add-ons manager to simulate a browser restart. It isn't
  // quite the same but that should be enough.
  await AddonTestUtils.promiseRestartManager();

  for (const id of importedAddonIDs) {
    const addon = await AddonManager.getAddonByID(id);
    Assert.ok(addon.isActive, `expected add-on "${id}" to be enabled`);
    // Verify that the sourceURI and installTelemetryInfo also match
    // the values expected for the addons installed from the browser
    // imports install flow.
    Assert.deepEqual(
      {
        id: addon.id,
        sourceURI: addon.sourceURI?.spec,
        installTelemetryInfo: addon.installTelemetryInfo,
      },
      {
        id,
        sourceURI: EXPECTED_SOURCE_URI_SPECS[id],
        installTelemetryInfo: {
          source: AMBrowserExtensionsImport.TELEMETRY_SOURCE,
        },
      },
      "Got the expected AddonWrapper properties"
    );
    await addon.uninstall();
  }
});
