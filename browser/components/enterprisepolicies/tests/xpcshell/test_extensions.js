/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.appInfo = getAppInfo();

const server = AddonTestUtils.createHttpServer({ hosts: ["example.com"] });
const BASE_URL = `http://example.com/data`;

let addonID = "policytest2@mozilla.com";

add_task(async function setup() {
  await AddonTestUtils.promiseStartupManager();

  let webExtensionFile = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: addonID,
        },
      },
    },
  });

  server.registerFile("/data/policy_test.xpi", webExtensionFile);
});

add_task(async function test_addon_forceinstalled_remote() {
  await Promise.all([
    AddonTestUtils.promiseInstallEvent("onInstallEnded"),
    setupPolicyEngineWithJson({
      policies: {
        Extensions: {
          Install: [BASE_URL + "/policy_test.xpi"],
          Locked: [addonID],
        },
      },
    }),
  ]);
  let addon = await AddonManager.getAddonByID(addonID);
  notEqual(addon, null, "Addon should not be null");
  equal(addon.appDisabled, false, "Addon should not be disabled");
  equal(
    addon.permissions & AddonManager.PERM_CAN_UNINSTALL,
    0,
    "Addon should not be able to be uninstalled."
  );
  equal(
    addon.permissions & AddonManager.PERM_CAN_DISABLE,
    0,
    "Addon should not be able to be disabled."
  );
  await assertManagementAPIInstallType(addon.id, "admin");

  await Assert.rejects(
    addon.disable(),
    /disallowed by enterprise policy/,
    "Addon should not be able to be disabled through the AddonManager API."
  );
  equal(addon.userDisabled, false, "Addon should still be enabled");

  await addon.uninstall();
});

add_task(async function test_addon_forceinstalled_local() {
  let addonID2 = "policytest@mozilla.com";

  let file = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
  file.append("policytest_v0.1.xpi");
  await Promise.all([
    AddonTestUtils.promiseInstallEvent("onInstallEnded"),
    setupPolicyEngineWithJson({
      policies: {
        Extensions: {
          Install: [file.path],
        },
      },
    }),
  ]);
  let addon = await AddonManager.getAddonByID(addonID2);
  notEqual(addon, null, "Addon should not be null");
  await addon.uninstall();
});
