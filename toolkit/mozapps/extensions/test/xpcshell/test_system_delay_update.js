/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This verifies that delaying a system add-on update works.

PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Message manager disconnected/
);

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

const profileDir = gProfD.clone();
profileDir.append("extensions");

const IGNORE_ID = "system_delay_ignore@tests.mozilla.org";
const COMPLETE_ID = "system_delay_complete@tests.mozilla.org";
const DEFER_ID = "system_delay_defer@tests.mozilla.org";
const DEFER2_ID = "system_delay_defer2@tests.mozilla.org";
const DEFER_ALSO_ID = "system_delay_defer_also@tests.mozilla.org";
const NORMAL_ID = "system1@tests.mozilla.org";

const distroDir = FileUtils.getDir("ProfD", ["sysfeatures"]);
distroDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
registerDirectory("XREAppFeat", distroDir);

registerCleanupFunction(() => {
  distroDir.remove(true);
});

AddonTestUtils.usePrivilegedSignatures = () => "system";

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "42");

function promiseInstallPostponed(addonID1, addonID2) {
  return new Promise((resolve, reject) => {
    let seen = [];
    let listener = {
      onInstallFailed: () => {
        AddonManager.removeInstallListener(listener);
        reject("extension installation should not have failed");
      },
      onInstallEnded: install => {
        AddonManager.removeInstallListener(listener);
        reject(
          `extension installation should not have ended for ${install.addon.id}`
        );
      },
      onInstallPostponed: install => {
        seen.push(install.addon.id);
        if (seen.includes(addonID1) && seen.includes(addonID2)) {
          AddonManager.removeInstallListener(listener);
          resolve();
        }
      },
    };

    AddonManager.addInstallListener(listener);
  });
}

function promiseInstallResumed(addonID1, addonID2) {
  return new Promise((resolve, reject) => {
    let seenPostponed = [];
    let seenEnded = [];
    let listener = {
      onInstallFailed: () => {
        AddonManager.removeInstallListener(listener);
        reject("extension installation should not have failed");
      },
      onInstallEnded: install => {
        seenEnded.push(install.addon.id);
        if (
          seenEnded.includes(addonID1) &&
          seenEnded.includes(addonID2) &&
          seenPostponed.includes(addonID1) &&
          seenPostponed.includes(addonID2)
        ) {
          AddonManager.removeInstallListener(listener);
          resolve();
        }
      },
      onInstallPostponed: install => {
        seenPostponed.push(install.addon.id);
      },
    };

    AddonManager.addInstallListener(listener);
  });
}

function promiseInstallDeferred(addonID1, addonID2) {
  return new Promise((resolve, reject) => {
    let seenEnded = [];
    let listener = {
      onInstallFailed: () => {
        AddonManager.removeInstallListener(listener);
        reject("extension installation should not have failed");
      },
      onInstallEnded: install => {
        seenEnded.push(install.addon.id);
        if (seenEnded.includes(addonID1) && seenEnded.includes(addonID2)) {
          AddonManager.removeInstallListener(listener);
          resolve();
        }
      },
    };

    AddonManager.addInstallListener(listener);
  });
}

async function checkAddon(addonID, { version }) {
  let addon = await promiseAddonByID(addonID);
  Assert.notEqual(addon, null);
  Assert.equal(addon.version, version);
  Assert.ok(addon.isCompatible);
  Assert.ok(!addon.appDisabled);
  Assert.ok(addon.isActive);
  Assert.equal(addon.type, "extension");
}

// Tests below have webextension background scripts inline.
/* globals browser */

// add-on registers upgrade listener, and ignores update.
add_task(async function test_systemaddon_upgrade_on_restart_builtin() {
  // discard system addon updates
  Services.prefs.setCharPref(PREF_SYSTEM_ADDON_SET, "");

  // Version 1.0 of an extension that ignores updates.
  function background() {
    browser.runtime.onUpdateAvailable.addListener(() => {
      browser.test.sendMessage("got-update");
    });
  }

  await setupBuiltinExtension(
    {
      background,
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: IGNORE_ID } },
      },
    },
    "test-systemaddon-ignore"
  );
  await setupBuiltinExtension(
    {
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: NORMAL_ID } },
      },
    },
    "test-systemaddon-normal"
  );
  await overrideBuiltIns({
    builtins: [
      {
        addon_id: IGNORE_ID,
        addon_version: "1.0",
        res_url: `resource://test-systemaddon-ignore/`,
      },
      {
        addon_id: NORMAL_ID,
        addon_version: "1.0",
        res_url: `resource://test-systemaddon-normal/`,
      },
    ],
  });

  // Version 2.0 of the same extension.
  let xpi2 = await createTempWebExtensionFile({
    manifest: {
      version: "2.0",
      browser_specific_settings: { gecko: { id: IGNORE_ID } },
    },
  });

  let extension = ExtensionTestUtils.expectExtension(IGNORE_ID);

  await Promise.all([promiseStartupManager(), extension.awaitStartup()]);

  let updateList = [
    {
      id: IGNORE_ID,
      version: "2.0",
      path: "system_delay_ignore_2.xpi",
      xpi: xpi2,
    },
    {
      id: NORMAL_ID,
      version: "2.0",
      path: "system1_2.xpi",
      xpi: await getSystemAddonXPI(1, "2.0"),
    },
  ];

  await Promise.all([
    promiseInstallPostponed(IGNORE_ID, NORMAL_ID),
    installSystemAddons(buildSystemAddonUpdates(updateList)),
    extension.awaitMessage("got-update"),
  ]);

  // addon upgrade has been delayed.
  await checkAddon(IGNORE_ID, { version: "1.0" });
  // other addons in the set are delayed as well.
  await checkAddon(NORMAL_ID, { version: "1.0" });

  // restarting allows upgrades to proceed
  await Promise.all([promiseRestartManager(), extension.awaitStartup()]);

  await checkAddon(IGNORE_ID, { version: "2.0" });
  await checkAddon(NORMAL_ID, { version: "2.0" });

  await promiseShutdownManager();

  // Destroy the test extension wrappers (fixes failure hit
  // due to the extension wrapper from a previous call being
  // still active when the same function is called again).
  extension.destroy();
});

// add-on registers upgrade listener, and allows update.
add_task(async function test_systemaddon_upgrade_on_reload_builtin() {
  // discard system addon updates
  Services.prefs.setCharPref(PREF_SYSTEM_ADDON_SET, "");

  // Version 1.0 of an extension that listens for and immediately
  // applies updates.
  function background() {
    browser.runtime.onUpdateAvailable.addListener(function listener() {
      browser.runtime.onUpdateAvailable.removeListener(listener);
      browser.test.sendMessage("got-update");
      browser.runtime.reload();
    });
  }

  // Version 2.0 of the same extension.
  let xpi2 = await createTempWebExtensionFile({
    manifest: {
      version: "2.0",
      browser_specific_settings: { gecko: { id: COMPLETE_ID } },
    },
  });

  await setupBuiltinExtension(
    {
      background,
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: COMPLETE_ID } },
      },
    },
    "test-systemaddon-complete"
  );
  await setupBuiltinExtension(
    {
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: NORMAL_ID } },
      },
    },
    "test-systemaddon-normal"
  );
  await overrideBuiltIns({
    builtins: [
      {
        addon_id: COMPLETE_ID,
        addon_version: "1.0",
        res_url: `resource://test-systemaddon-complete/`,
      },
      {
        addon_id: NORMAL_ID,
        addon_version: "1.0",
        res_url: `resource://test-systemaddon-normal/`,
      },
    ],
  });

  let extension = ExtensionTestUtils.expectExtension(COMPLETE_ID);

  await Promise.all([promiseStartupManager(), extension.awaitStartup()]);

  let updateList = [
    {
      id: COMPLETE_ID,
      version: "2.0",
      path: "system_delay_complete_2.xpi",
      xpi: xpi2,
    },
    {
      id: NORMAL_ID,
      version: "2.0",
      path: "system1_2.xpi",
      xpi: await getSystemAddonXPI(1, "2.0"),
    },
  ];

  // initial state
  await checkAddon(COMPLETE_ID, { version: "1.0" });
  await checkAddon(NORMAL_ID, { version: "1.0" });

  // We should see that the onUpdateListener executed, then see the
  // update resume.
  await Promise.all([
    extension.awaitMessage("got-update"),
    promiseInstallResumed(COMPLETE_ID, NORMAL_ID),
    installSystemAddons(buildSystemAddonUpdates(updateList)),
  ]);
  await extension.awaitStartup();

  // addon upgrade has been allowed
  await checkAddon(COMPLETE_ID, { version: "2.0" });
  // other upgrades in the set are allowed as well
  await checkAddon(NORMAL_ID, { version: "2.0" });

  // restarting changes nothing
  await Promise.all([promiseRestartManager(), extension.awaitStartup()]);

  await checkAddon(COMPLETE_ID, { version: "2.0" });
  await checkAddon(NORMAL_ID, { version: "2.0" });

  await promiseShutdownManager();

  // Destroy the test extension wrappers (fixes failure hit
  // due to the extension wrapper from a previous call being
  // still active when the same function is called again).
  extension.destroy();
});

function delayBackground() {
  browser.test.onMessage.addListener(msg => {
    if (msg !== "reload") {
      browser.test.fail(`Got unexpected test message: ${msg}`);
    }
    browser.runtime.reload();
  });
  browser.runtime.onUpdateAvailable.addListener(async function listener() {
    browser.runtime.onUpdateAvailable.removeListener(listener);
    browser.test.sendMessage("got-update");
  });
}

// Upgrade listener initially defers then proceeds after a pause.
add_task(async function test_systemaddon_upgrade_after_pause_builtin() {
  // discard system addon updates
  Services.prefs.setCharPref(PREF_SYSTEM_ADDON_SET, "");

  await setupBuiltinExtension(
    {
      background: delayBackground,
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: DEFER_ID } },
      },
    },
    "test-systemaddon-defer"
  );
  await setupBuiltinExtension(
    {
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: NORMAL_ID } },
      },
    },
    "test-systemaddon-normal"
  );
  await overrideBuiltIns({
    builtins: [
      {
        addon_id: DEFER_ID,
        addon_version: "1.0",
        res_url: `resource://test-systemaddon-defer/`,
      },
      {
        addon_id: NORMAL_ID,
        addon_version: "1.0",
        res_url: `resource://test-systemaddon-normal/`,
      },
    ],
  });

  // Version 2.0 of the same xtension.
  let xpi2 = await createTempWebExtensionFile({
    manifest: {
      version: "2.0",
      browser_specific_settings: { gecko: { id: DEFER_ID } },
    },
  });

  let extension = ExtensionTestUtils.expectExtension(DEFER_ID);

  await Promise.all([promiseStartupManager(), extension.awaitStartup()]);

  let updateList = [
    {
      id: DEFER_ID,
      version: "2.0",
      path: "system_delay_defer_2.xpi",
      xpi: xpi2,
    },
    {
      id: NORMAL_ID,
      version: "2.0",
      path: "system1_2.xpi",
      xpi: await getSystemAddonXPI(1, "2.0"),
    },
  ];

  await Promise.all([
    promiseInstallPostponed(DEFER_ID, NORMAL_ID),
    installSystemAddons(buildSystemAddonUpdates(updateList)),
    extension.awaitMessage("got-update"),
  ]);

  // upgrade is initially postponed
  await checkAddon(DEFER_ID, { version: "1.0" });
  // other addons in the set are postponed as well.
  await checkAddon(NORMAL_ID, { version: "1.0" });

  let deferred = promiseInstallDeferred(DEFER_ID, NORMAL_ID);

  // Tell the extension to proceed with the update.
  extension.setRestarting();
  extension.sendMessage("reload");

  await Promise.all([deferred, extension.awaitStartup()]);

  // addon upgrade has been allowed
  await checkAddon(DEFER_ID, { version: "2.0" });
  // other addons in the set are allowed as well.
  await checkAddon(NORMAL_ID, { version: "2.0" });

  // restarting changes nothing
  await promiseRestartManager();
  await extension.awaitStartup();

  await checkAddon(DEFER_ID, { version: "2.0" });
  await checkAddon(NORMAL_ID, { version: "2.0" });

  await promiseShutdownManager();

  // Destroy the test extension wrappers (fixes failure hit
  // due to the extension wrapper from a previous call being
  // still active when the same function is called again).
  extension.destroy();
});

// Multiple add-ons register update listeners, initially defers then
// each unblock in turn.
add_task(async function test_multiple_systemaddon_upgrade_postpone_builtin() {
  // discard system addon updates.
  Services.prefs.setCharPref(PREF_SYSTEM_ADDON_SET, "");

  let updateList = [];
  let xpi;
  let overrideBuiltInsData = {
    system: [],
    builtins: [],
  };

  await setupBuiltinExtension(
    {
      background: delayBackground,
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: DEFER2_ID } },
      },
    },
    "test-systemaddon-defer2"
  );
  overrideBuiltInsData.builtins.push({
    addon_id: DEFER2_ID,
    addon_version: "1.0",
    res_url: "resource://test-systemaddon-defer2/",
  });

  xpi = await createTempWebExtensionFile({
    manifest: {
      version: "2.0",
      browser_specific_settings: { gecko: { id: DEFER2_ID } },
    },
  });
  updateList.push({
    id: DEFER2_ID,
    version: "2.0",
    path: "system_delay_defer_2.xpi",
    xpi,
  });

  await setupBuiltinExtension(
    {
      background: delayBackground,
      manifest: {
        version: "1.0",
        browser_specific_settings: { gecko: { id: DEFER_ALSO_ID } },
      },
    },
    "test-systemaddon-defer-also"
  );
  overrideBuiltInsData.builtins.push({
    addon_id: DEFER_ALSO_ID,
    addon_version: "1.0",
    res_url: "resource://test-systemaddon-defer-also/",
  });

  xpi = await createTempWebExtensionFile({
    manifest: {
      version: "2.0",
      browser_specific_settings: { gecko: { id: DEFER_ALSO_ID } },
    },
  });
  updateList.push({
    id: DEFER_ALSO_ID,
    version: "2.0",
    path: "system_delay_defer_also_2.xpi",
    xpi,
  });

  await overrideBuiltIns(overrideBuiltInsData);

  let extension1 = ExtensionTestUtils.expectExtension(DEFER2_ID);
  let extension2 = ExtensionTestUtils.expectExtension(DEFER_ALSO_ID);

  await Promise.all([
    promiseStartupManager(),
    extension1.awaitStartup(),
    extension2.awaitStartup(),
  ]);

  await Promise.all([
    promiseInstallPostponed(DEFER2_ID, DEFER_ALSO_ID),
    installSystemAddons(buildSystemAddonUpdates(updateList)),
    extension1.awaitMessage("got-update"),
    extension2.awaitMessage("got-update"),
  ]);

  // upgrade is initially postponed
  await checkAddon(DEFER2_ID, { version: "1.0" });
  // other addons in the set are postponed as well.
  await checkAddon(DEFER_ALSO_ID, { version: "1.0" });

  let deferred = promiseInstallDeferred(DEFER2_ID, DEFER_ALSO_ID);

  // Let one extension request that the update proceed.
  extension1.setRestarting();
  extension1.sendMessage("reload");

  // Upgrade blockers still present.
  await checkAddon(DEFER2_ID, { version: "1.0" });
  await checkAddon(DEFER_ALSO_ID, { version: "1.0" });

  // Let the second extension allow the update to proceed.
  extension2.setRestarting();
  extension2.sendMessage("reload");

  await Promise.all([
    deferred,
    extension1.awaitStartup(),
    extension2.awaitStartup(),
  ]);

  // addon upgrade has been allowed
  await checkAddon(DEFER2_ID, { version: "2.0" });
  await checkAddon(DEFER_ALSO_ID, { version: "2.0" });

  // restarting changes nothing
  await Promise.all([
    promiseRestartManager(),
    extension1.awaitStartup(),
    extension2.awaitStartup(),
  ]);

  await checkAddon(DEFER2_ID, { version: "2.0" });
  await checkAddon(DEFER_ALSO_ID, { version: "2.0" });

  await promiseShutdownManager();

  // Destroy the test extension wrappers (fixes failure hit
  // due to the extension wrapper from a previous call being
  // still active when the same function is called again).
  extension1.destroy();
  extension2.destroy();
});
