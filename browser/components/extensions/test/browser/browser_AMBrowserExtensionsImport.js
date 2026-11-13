/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { AMBrowserExtensionsImport } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

// This test verifies the appmenu UI when there are pending imported add-ons.
// The UI in `about:addons` is covered by tests in
// `toolkit/mozapps/extensions/test/browser/browser_AMBrowserExtensionsImport.js`.

AddonTestUtils.initMochitest(this);

const TEST_SERVER = AddonTestUtils.createHttpServer();

const ADDONS = {
  ext1: {
    manifest: {
      name: "Ext 1",
      version: "1.0",
      browser_specific_settings: { gecko: { id: "ff@ext-1" } },
      permissions: ["history"],
    },
  },
  ext2: {
    manifest: {
      name: "Ext 2",
      version: "1.0",
      browser_specific_settings: { gecko: { id: "ff@ext-2" } },
      permissions: ["history"],
    },
  },
};
// Populated in `setup()`.
const XPIS = {};
// Populated in `setup()`.
const ADDON_SEARCH_RESULTS = {};

const mockAddonRepository = ({ addons = [] }) => {
  return {
    async getMappedAddons() {
      return Promise.resolve({
        addons,
        matchedIDs: [],
        unmatchedIDs: [],
      });
    },
  };
};

add_setup(async function setup() {
  for (const [name, data] of Object.entries(ADDONS)) {
    XPIS[name] = AddonTestUtils.createTempWebExtensionFile(data);
    TEST_SERVER.registerFile(`/addons/${name}.xpi`, XPIS[name]);

    ADDON_SEARCH_RESULTS[name] = {
      id: data.manifest.browser_specific_settings.gecko.id,
      name: data.manifest.name,
      version: data.manifest.version,
      sourceURI: Services.io.newURI(
        `http://localhost:${TEST_SERVER.identity.primaryPort}/addons/${name}.xpi`
      ),
      icons: {},
    };
  }

  registerCleanupFunction(() => {
    // Clear the add-on repository override.
    AMBrowserExtensionsImport._addonRepository = null;
  });
});

add_task(async function test_appmenu_notification() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
  });
  const menuButton = document.getElementById("PanelUI-menu-button");

  let promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  // Start a first import...
  await AMBrowserExtensionsImport.stageInstalls(browserID, extensionIDs);
  await promiseTopic;
  Assert.equal(
    menuButton.getAttribute("badge-status"),
    "addon-alert",
    "expected menu button to have the addon-alert badge"
  );

  // ...then cancel it.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-cancelled"
  );
  await AMBrowserExtensionsImport.cancelInstalls();
  await promiseTopic;
  Assert.ok(
    !menuButton.hasAttribute("badge-status"),
    "expected menu button to no longer have the addon-alert badge"
  );

  // We start a second import here, then we complete it with the UI.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  Assert.equal(
    menuButton.getAttribute("badge-status"),
    "addon-alert",
    "expected menu button to have the addon-alert badge"
  );

  // Open the appmenu panel in order to verify the notification.
  const menuPanelShown = BrowserTestUtils.waitForEvent(
    PanelUI.panel,
    "ViewShown"
  );
  menuButton.click();
  await menuPanelShown;

  const notifications = PanelUI.addonNotificationContainer;
  Assert.equal(
    notifications.children.length,
    1,
    "expected a notification about the imported add-ons"
  );

  const endedPromises = result.importedAddonIDs.map(() =>
    AddonTestUtils.promiseInstallEvent("onInstallEnded")
  );
  const menuPanelHidden = BrowserTestUtils.waitForEvent(
    PanelUI.panel,
    "popuphidden"
  );
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-complete"
  );
  // Complete the installation of the add-ons by clicking on the notification.
  notifications.children[0].click();
  await Promise.all([...endedPromises, promiseTopic, menuPanelHidden]);
  Assert.ok(
    !menuButton.hasAttribute("badge-status"),
    "expected menu button to no longer have the addon-alert badge"
  );

  for (const id of result.importedAddonIDs) {
    const addon = await AddonManager.getAddonByID(id);
    Assert.ok(addon.isActive, `expected add-on "${id}" to be enabled`);
    await addon.uninstall();
  }
});

add_task(async function test_appmenu_notification_with_sideloaded_addon() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.autoDisableScopes", 15]],
  });

  const menuButton = document.getElementById("PanelUI-menu-button");

  // Load a sideloaded add-on, which will be user-disabled by default.
  const sideloadedXPI = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      name: "sideloaded add-on",
      version: "1.0",
      browser_specific_settings: { gecko: { id: "ff@sideloaded" } },
    },
  });
  await AddonTestUtils.manuallyInstall(sideloadedXPI);
  const changePromise = new Promise(resolve =>
    ExtensionsUI.once("change", resolve)
  );
  ExtensionsUI._checkForSideloaded();
  await changePromise;

  // We should have a badge for the sideloaded add-on on the appmenu button.
  Assert.equal(
    menuButton.getAttribute("badge-status"),
    "addon-alert",
    "expected menu button to have the addon-alert badge"
  );

  // Now, we start an import...
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
  });

  let promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  await AMBrowserExtensionsImport.stageInstalls(browserID, extensionIDs);
  await promiseTopic;

  // Badge should still be shown.
  Assert.equal(
    menuButton.getAttribute("badge-status"),
    "addon-alert",
    "expected menu button to have the addon-alert badge"
  );

  // Open the appmenu panel in order to verify the notifications.
  const menuPanelShown = BrowserTestUtils.waitForEvent(
    PanelUI.panel,
    "ViewShown"
  );
  menuButton.click();
  await menuPanelShown;

  // We expect two notifications: one for the imported add-ons (listed first),
  // and the second about the sideloaded add-on.
  const notifications = PanelUI.addonNotificationContainer;
  Assert.equal(notifications.children.length, 2, "expected two notifications");
  Assert.equal(
    notifications.children[0].id,
    "webext-imported-addons",
    "expected a notification for the imported add-ons"
  );
  Assert.equal(
    notifications.children[1].id,
    "webext-perms-sideload-menu-item",
    "expected a notification for the sideloaded add-on"
  );

  // Cancel the import to make sure the UI is updated accordingly.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-cancelled"
  );
  await AMBrowserExtensionsImport.cancelInstalls();
  await promiseTopic;

  // Badge should still be shown since there is still the sideloaded
  // notification but we do not expect the notification for the imported
  // add-ons anymore.
  Assert.ok(
    menuButton.hasAttribute("badge-status"),
    "expected menu button to have the addon-alert badge"
  );
  Assert.equal(notifications.children.length, 1, "expected a notification");
  Assert.equal(
    notifications.children[0].id,
    "webext-perms-sideload-menu-item",
    "expected a notification for the sideloaded add-on"
  );

  // Click the sideloaded add-on notification.
  const menuPanelHidden = BrowserTestUtils.waitForEvent(
    PanelUI.panel,
    "popuphidden"
  );
  const popupPromise = promisePopupNotificationShown(
    "addon-webext-permissions"
  );
  notifications.children[0].click();
  const [popup] = await Promise.all([popupPromise, menuPanelHidden]);

  // The user should see a new popup asking them about enabling the sideloaded
  // add-on. Let's keep the add-on disabled.
  popup.secondaryButton.click();

  // The badge should be hidden now, and there shouldn't be any notification in
  // the panel anymore.
  Assert.ok(
    !menuButton.hasAttribute("badge-status"),
    "expected menu button to no longer have the addon-alert badge"
  );
  Assert.equal(notifications.children.length, 0, "expected no notification");

  // Unload the sideloaded add-on.
  const addon = await AddonManager.getAddonByID("ff@sideloaded");
  Assert.ok(addon.userDisabled, "expected add-on to be disabled");
  await addon.uninstall();

  await SpecialPowers.popPrefEnv();
});
