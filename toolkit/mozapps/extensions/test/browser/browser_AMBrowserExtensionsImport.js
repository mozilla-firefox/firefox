/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { AMBrowserExtensionsImport } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

// This test verifies the global notification in `about:addons` when there are
// pending imported add-ons. The appmenu UI is covered by tests in:
// `browser/components/extensions/test/browser/browser_AMBrowserExtensionsImport.js`.

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

// All message bars (except for imported-addons) are already checked in
// toolkit/mozapps/extensions/test/browser/browser_globalwarnings.js
// This test serves as a sanity check that the other message bars still behave
// as expected when the imported-addons messagebar is shown.
const assertWarningShown = async (
  win,
  stack,
  {
    expectedWarningType = "imported-addons",
    expectedWarningL10nId,
    expectAction = true,
  } = {}
) => {
  Assert.equal(stack.childElementCount, 1, "expected a global warning");
  const messageBar = stack.firstElementChild;
  Assert.equal(
    messageBar.getAttribute("warning-type"),
    expectedWarningType,
    `expected a warning for ${expectedWarningType}`
  );
  Assert.equal(
    messageBar.getAttribute("data-l10n-id"),
    expectedWarningL10nId || `extensions-warning-${expectedWarningType}2`,
    "expected correct l10n ID"
  );
  await win.document.l10n.translateElements([messageBar]);

  if (expectAction) {
    const button = messageBar.querySelector("button");
    Assert.equal(
      button.getAttribute("action"),
      expectedWarningType,
      `expected a button for ${expectedWarningType}`
    );
    Assert.equal(
      button.getAttribute("data-l10n-id"),
      `extensions-warning-${expectedWarningType}-button`,
      "expected correct l10n ID on the button"
    );
    await win.document.l10n.translateElements([button]);
  }
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

add_task(async function test_aboutaddons_global_message() {
  const browserID = "some-browser-id";
  const extensionIDs = ["ext-1", "ext-2"];
  AMBrowserExtensionsImport._addonRepository = mockAddonRepository({
    addons: Object.values(ADDON_SEARCH_RESULTS),
  });

  // Global warnings should be displayed in all the `about:addons` views but
  // the migration wizard links to the default view. That's why we load this
  // view here, too (as opposed to, e.g., `"extensions"`).
  const win = await loadInitialView();
  const stack = win.document.querySelector("global-warnings");

  Assert.equal(stack.childElementCount, 0, "expected no global warning");

  let promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  // Start a first import...
  await AMBrowserExtensionsImport.stageInstalls(browserID, extensionIDs);
  await promiseTopic;
  // We expect a warning about the imported add-ons to be shown.
  await assertWarningShown(win, stack);

  // ...then cancel it.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-cancelled"
  );
  await AMBrowserExtensionsImport.cancelInstalls();
  await promiseTopic;

  // At this point, the warning about the imported add-ons should be hidden.
  Assert.equal(stack.childElementCount, 0, "expected no global warning");

  // We start a second import here, then we make sure an imported-addons
  // messagebar doesn't prevent the other global warning types to be shown.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-pending"
  );
  const result = await AMBrowserExtensionsImport.stageInstalls(
    browserID,
    extensionIDs
  );
  await promiseTopic;
  await assertWarningShown(win, stack);

  info("Verify safe-mode is not hidden by an imported-addons messagebar");
  stack.inSafeMode = true;
  stack.refresh();
  await assertWarningShown(win, stack, {
    expectedWarningType: "safe-mode",
    expectedWarningL10nId: "extensions-warning-safe-mode3",
    expectAction: false,
  });
  stack.inSafeMode = false;

  info(
    "Verify check-compatibility is not hidden by an imported-addons messagebar"
  );
  AddonManager.checkCompatibility = false;
  stack.refresh();
  await assertWarningShown(win, stack, {
    expectedWarningType: "check-compatibility",
  });
  AddonManager.checkCompatibility = true;

  info("Verify update-security is not hidden by an imported-addons messagebar");
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.checkUpdateSecurity", false]],
  });
  stack.refresh();
  await assertWarningShown(win, stack, {
    expectedWarningType: "update-security",
  });
  await SpecialPowers.popPrefEnv();

  // After making sure the imported-addons messagebar is visible again, we
  // finally complete the pending import with the UI from the global warning.
  info(
    "Verify pending imported addons can be completed from the messagebar action"
  );
  stack.refresh();
  await assertWarningShown(win, stack, {
    expectedWarningType: "imported-addons",
  });

  // Complete the installation of the add-ons by clicking on the button in the
  // global warning.
  promiseTopic = TestUtils.topicObserved(
    "webextension-imported-addons-complete"
  );
  const endedPromises = result.importedAddonIDs.map(id =>
    AddonTestUtils.promiseInstallEvent(
      "onInstallEnded",
      install => install.addon.id === id
    )
  );
  stack.firstElementChild.querySelector("button").click();
  await Promise.all([...endedPromises, promiseTopic]);

  // At this point, the warning about the imported add-ons should be hidden
  // because the add-ons are installed.
  Assert.equal(stack.childElementCount, 0, "expected no global warning");

  for (const id of result.importedAddonIDs) {
    const addon = await AddonManager.getAddonByID(id);
    Assert.ok(addon.isActive, `expected add-on "${id}" to be enabled`);
    await addon.uninstall();
  }

  await closeView(win);
});
