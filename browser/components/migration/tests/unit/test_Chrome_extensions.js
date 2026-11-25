/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AMBrowserExtensionsImport, AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { ChromeMigrationUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeMigrationUtils.sys.mjs"
);
const { ChromeProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeProfileMigrator.sys.mjs"
);

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

const TEST_SERVER = AddonTestUtils.createHttpServer({ hosts: ["example.com"] });

const PROFILE = {
  id: "Default",
  name: "Person 1",
};

const IMPORTED_ADDON_1 = {
  name: "A Firefox extension",
  version: "1.0",
  browser_specific_settings: { gecko: { id: "some-ff@extension" } },
};

// Setup chrome user data path for all platforms.
ChromeMigrationUtils.getDataPath = () => {
  return Promise.resolve(
    do_get_file("Library/Application Support/Google/Chrome/").path
  );
};

const mockAddonRepository = ({ addons = [] } = {}) => {
  return {
    async getMappedAddons(browserID, extensionIDs) {
      Assert.equal(browserID, "chrome", "expected browser ID");
      // Sort extension IDs to have a predictable order.
      extensionIDs.sort();
      Assert.deepEqual(
        extensionIDs,
        ["fake-extension-1", "fake-extension-2"],
        "expected an array of 2 extension IDs"
      );
      return Promise.resolve({
        addons,
        matchedIDs: [],
        unmatchedIDs: [],
      });
    },
  };
};

add_setup(async function setup() {
  await AddonTestUtils.promiseStartupManager();

  // Create a Firefox XPI that we can use during the import.
  const xpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: IMPORTED_ADDON_1,
  });
  TEST_SERVER.registerFile(`/addons/addon-1.xpi`, xpi);

  // Override the `AddonRepository` in `AMBrowserExtensionsImport` with our own
  // mock so that we control the add-ons that are mapped to the list of Chrome
  // extension IDs.
  const addons = [
    {
      id: IMPORTED_ADDON_1.browser_specific_settings.gecko.id,
      name: IMPORTED_ADDON_1.name,
      version: IMPORTED_ADDON_1.version,
      sourceURI: Services.io.newURI(`http://example.com/addons/addon-1.xpi`),
      icons: {},
    },
  ];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({ addons });

  registerCleanupFunction(() => {
    // Clear the add-on repository override.
    AMBrowserExtensionsImport._addonRepository = null;
  });
});

add_task(
  { pref_set: [["browser.migrate.chrome.extensions.enabled", true]] },
  async function test_import_extensions() {
    const migrator = await MigrationUtils.getMigrator("chrome");
    Assert.ok(
      await migrator.isSourceAvailable(),
      "Sanity check the source exists"
    );

    let promiseTopic = TestUtils.topicObserved(
      "webextension-imported-addons-pending"
    );
    await promiseMigration(
      migrator,
      MigrationUtils.resourceTypes.EXTENSIONS,
      PROFILE,
      true
    );
    await promiseTopic;
    // When this property is `true`, the UI should show a badge on the appmenu
    // button, and the user can finalize the import later.
    Assert.ok(
      AMBrowserExtensionsImport.canCompleteOrCancelInstalls,
      "expected some add-ons to have been imported"
    );

    // Let's actually complete the import programatically below.
    promiseTopic = TestUtils.topicObserved(
      "webextension-imported-addons-complete"
    );
    await AMBrowserExtensionsImport.completeInstalls();
    await Promise.all([
      AddonTestUtils.promiseInstallEvent("onInstallEnded"),
      promiseTopic,
    ]);

    // The add-on should be installed and therefore it can be uninstalled.
    const addon = await AddonManager.getAddonByID(
      IMPORTED_ADDON_1.browser_specific_settings.gecko.id
    );
    await addon.uninstall();
  }
);

/**
 * Test that, for now at least, the extension resource type is only made
 * available for Chrome and none of the derivitive browsers.
 */
add_task(
  { pref_set: [["browser.migrate.chrome.extensions.enabled", true]] },
  async function test_only_chrome_migrates_extensions() {
    for (const key of MigrationUtils.availableMigratorKeys) {
      let migrator = await MigrationUtils.getMigrator(key);

      if (migrator instanceof ChromeProfileMigrator && key != "chrome") {
        info("Testing migrator with key " + key);
        Assert.ok(
          await migrator.isSourceAvailable(),
          "First check the source exists"
        );
        let resourceTypes = await migrator.getMigrateData(PROFILE);
        Assert.ok(
          !(resourceTypes & MigrationUtils.resourceTypes.EXTENSIONS),
          "Should not offer the extension resource type"
        );
      }
    }
  }
);
