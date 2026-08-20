"use strict";

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

const server = AddonTestUtils.createHttpServer();

const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);

// This initializes the policy engine for xpcshell tests
let policies = Cc["@mozilla.org/enterprisepolicies;1"].getService(
  Ci.nsIObserver
);
policies.observe(null, "policies-startup", null);

const TEST_ADDON_ID = "test_management_addon@tests.mozilla.com";

let addon;

add_setup(async () => {
  await ExtensionTestUtils.startAddonManager();

  addon = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Simple addon test",
      version: "1.0",
      description: "test addon",
      browser_specific_settings: {
        gecko: {
          id: TEST_ADDON_ID,
        },
      },
    },
    useAddonManager: "temporary",
  });
  await addon.startup();

  registerCleanupFunction(async () => {
    await addon.unload();
  });
});

add_task(async function test_enterprise_management_works() {
  const TEST_ID = "test_management_policy@tests.mozilla.com";

  async function background(TEST_ADDON_ID) {
    let addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertTrue(addon.enabled, "addon is enabled");
    await browser.management.setEnabled(addon.id, false);
    addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertFalse(addon.enabled, "addon is disabled");
    // Disabling an already-disabled add-on is a no-op, not an error.
    await browser.management.setEnabled(addon.id, false);
    await browser.management.setEnabled(addon.id, true);
    addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertTrue(addon.enabled, "addon is enabled");
    browser.test.sendMessage("done");
  }

  let originalXpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: TEST_ID,
        },
      },
      name: TEST_ID,
      permissions: ["management"],
    },
    background: `(${background})("${TEST_ADDON_ID}")`,
  });

  let serverHost = `http://localhost:${server.identity.primaryPort}`;

  let xpiFilename = `/admin.xpi`;
  server.registerFile(xpiFilename, originalXpi);

  info("Testing add-on installed via ExtensionSettings policy");
  let extension = ExtensionTestUtils.expectExtension(TEST_ID);
  await Promise.all([
    AddonTestUtils.promiseInstallEvent("onInstallEnded"),
    EnterprisePolicyTesting.setupPolicyEngineWithJson({
      policies: {
        ExtensionSettings: {
          [TEST_ID]: {
            installation_mode: "force_installed",
            install_url: serverHost + xpiFilename,
          },
        },
      },
    }),
  ]);
  await extension.awaitStartup();
  await extension.awaitMessage("done");
  await extension.unload();

  info("Testing add-on installed & locked via Extensions policy");
  extension = ExtensionTestUtils.expectExtension(TEST_ID);
  await Promise.all([
    AddonTestUtils.promiseInstallEvent("onInstallEnded"),
    EnterprisePolicyTesting.setupPolicyEngineWithJson({
      policies: {
        ExtensionSettings: {},
        Extensions: {
          Install: [serverHost + xpiFilename],
          Locked: [TEST_ID],
        },
      },
    }),
  ]);
  await extension.awaitStartup();
  await extension.awaitMessage("done");
  await extension.unload();
});

add_task(async function test_enterprise_management_locked_addon() {
  const TEST_ID = "test_management_policy_locked@tests.mozilla.com";

  async function background(TEST_ADDON_ID) {
    let addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertTrue(addon.enabled, "addon is enabled");
    await browser.test.assertRejects(
      browser.management.setEnabled(TEST_ADDON_ID, false),
      `Cannot disable add-on ${TEST_ADDON_ID}: disallowed by enterprise policy`,
      "setEnabled should fail for a locked add-on"
    );
    addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertTrue(addon.enabled, "addon is still enabled");
    browser.test.sendMessage("done");
  }

  let originalXpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: TEST_ID,
        },
      },
      name: TEST_ID,
      permissions: ["management"],
    },
    background: `(${background})("${TEST_ADDON_ID}")`,
  });

  let serverHost = `http://localhost:${server.identity.primaryPort}`;

  let xpiFilename = `/locked.xpi`;
  server.registerFile(xpiFilename, originalXpi);

  let extension = ExtensionTestUtils.expectExtension(TEST_ID);
  await Promise.all([
    AddonTestUtils.promiseInstallEvent("onInstallEnded"),
    EnterprisePolicyTesting.setupPolicyEngineWithJson({
      policies: {
        ExtensionSettings: {
          [TEST_ID]: {
            installation_mode: "force_installed",
            install_url: serverHost + xpiFilename,
          },
        },
        Extensions: {
          Locked: [TEST_ADDON_ID],
        },
      },
    }),
  ]);
  await extension.awaitStartup();
  await extension.awaitMessage("done");
  await extension.unload();

  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
});

add_task(async function test_no_enterprise_management_fails() {
  const TEST_ID = "test_management_no_policy@tests.mozilla.com";

  async function background(TEST_ADDON_ID) {
    let addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertTrue(addon.enabled, "addon is enabled");
    await browser.test.assertRejects(
      browser.management.setEnabled(addon.id, false),
      "setEnabled can only be used for themes or by addons installed by enterprise policy",
      "setEnabled should fail"
    );
    addon = await browser.management.get(TEST_ADDON_ID);
    browser.test.assertTrue(addon.enabled, "addon is still enabled");
    browser.test.sendMessage("done");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: TEST_ID,
        },
      },
      permissions: ["management"],
    },
    background: `(${background})("${TEST_ADDON_ID}")`,
    useAddonManager: "temporary",
  });
  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});
