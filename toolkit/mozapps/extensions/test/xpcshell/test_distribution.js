/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This verifies that add-ons distributed with the application get installed
// correctly

// Allow distributed add-ons to install
Services.prefs.setBoolPref("extensions.installDistroAddons", true);

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");

const profileDir = gProfD.clone();
profileDir.append("extensions");
const distroDir = gProfD.clone();
distroDir.append("distribution");
distroDir.append("extensions");
registerDirectory("XREAppDist", distroDir.parent);

async function setOldModificationTime() {
  // Make sure the installed extension has an old modification time so any
  // changes will be detected
  await promiseShutdownManager();
  let extension = gProfD.clone();
  extension.append("extensions");
  extension.append(`${ID}.xpi`);
  setExtensionModifiedTime(extension, Date.now() - MAKE_FILE_OLD_DIFFERENCE);
  await promiseStartupManager();
}

const ID = "addon@tests.mozilla.org";

async function writeDistroAddon(version) {
  let xpi = await createTempWebExtensionFile({
    manifest: {
      version,
      browser_specific_settings: { gecko: { id: ID } },
    },
  });
  xpi.copyTo(distroDir, `${ID}.xpi`);
}

// Tests that on the first startup the add-on gets installed
add_task(async function run_test_1() {
  await writeDistroAddon("1.0");
  await promiseStartupManager();

  let a1 = await AddonManager.getAddonByID(ID);
  Assert.notEqual(a1, null);
  Assert.equal(a1.version, "1.0");
  Assert.ok(a1.isActive);
  Assert.equal(a1.scope, AddonManager.SCOPE_PROFILE);
  Assert.ok(!a1.foreignInstall);
});

// Tests that starting with a newer version in the distribution dir doesn't
// install it yet
add_task(async function run_test_2() {
  await setOldModificationTime();

  await writeDistroAddon("2.0");
  await promiseRestartManager();

  let a1 = await AddonManager.getAddonByID(ID);
  Assert.notEqual(a1, null);
  Assert.equal(a1.version, "1.0");
  Assert.ok(a1.isActive);
  Assert.equal(a1.scope, AddonManager.SCOPE_PROFILE);
});

// Test that an app upgrade installs the newer version
add_task(async function run_test_3() {
  await promiseRestartManager("2");

  let a1 = await AddonManager.getAddonByID(ID);
  Assert.notEqual(a1, null);
  Assert.equal(a1.version, "2.0");
  Assert.ok(a1.isActive);
  Assert.equal(a1.scope, AddonManager.SCOPE_PROFILE);
  Assert.ok(!a1.foreignInstall);
});

// Test that an app upgrade doesn't downgrade the extension
add_task(async function run_test_4() {
  await setOldModificationTime();

  await writeDistroAddon("1.0");
  await promiseRestartManager("3");

  let a1 = await AddonManager.getAddonByID(ID);
  Assert.notEqual(a1, null);
  Assert.equal(a1.version, "2.0");
  Assert.ok(a1.isActive);
  Assert.equal(a1.scope, AddonManager.SCOPE_PROFILE);
});

// Tests that after uninstalling a restart doesn't re-install the extension
add_task(async function run_test_5() {
  let a1 = await AddonManager.getAddonByID(ID);
  await a1.uninstall();

  await promiseRestartManager();

  let a1_2 = await AddonManager.getAddonByID(ID);
  Assert.equal(a1_2, null);
});

// Tests that upgrading the application still doesn't re-install the uninstalled
// extension
add_task(async function run_test_6() {
  await promiseRestartManager("4");

  let a1 = await AddonManager.getAddonByID(ID);
  Assert.equal(a1, null);
});

// Tests that a distribution add-on that fails signature verification is not
// marked as installed, so it is installed once it verifies on a later startup
add_task(async function test_unsigned_retried() {
  const UNSIGNED_ID = "unsigned@tests.mozilla.org";
  let xpi = await createTempWebExtensionFile({
    manifest: {
      version: "1.0",
      browser_specific_settings: { gecko: { id: UNSIGNED_ID } },
    },
  });
  xpi.copyTo(distroDir, `${UNSIGNED_ID}.xpi`);

  gUseRealCertChecks = true;
  await promiseRestartManager();

  Assert.equal(await AddonManager.getAddonByID(UNSIGNED_ID), null);
  Assert.ok(
    !Services.prefs.prefHasUserValue(
      `extensions.installedDistroAddon.${UNSIGNED_ID}`
    )
  );

  gUseRealCertChecks = false;
  await promiseRestartManager();

  let addon = await AddonManager.getAddonByID(UNSIGNED_ID);
  Assert.notEqual(addon, null);
  Assert.ok(addon.isActive);
  Assert.ok(
    Services.prefs.getBoolPref(`extensions.installedDistroAddon.${UNSIGNED_ID}`)
  );
});

// Tests that distribution entries that hit an exception when stat-ed
// are not breaking startup and other add-ons are still installed as expected.
add_task(
  { skip_if: () => AppConstants.platform == "win" },
  async function test_unreadable_distribution_entries() {
    const PROFILE_ID = "profile-addon@tests.mozilla.org";
    await promiseInstallWebExtension({
      manifest: { browser_specific_settings: { gecko: { id: PROFILE_ID } } },
    });

    let localeDir = distroDir.clone();
    localeDir.append("locale-xx");
    localeDir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o755);

    // Without the search permission the directory entries can be listed but
    // nsIFile.isDirectory() calls in XPIProvider installDistributionAddons
    // throw NS_ERROR_FILE_ACCESS_DENIED.
    await IOUtils.setPermissions(distroDir.path, 0o444);
    let messages;
    try {
      ({ messages } = await AddonTestUtils.promiseConsoleOutput(() =>
        promiseRestartManager("5")
      ));
    } finally {
      await IOUtils.setPermissions(distroDir.path, 0o755);
    }

    // Expect one warning from the locale scan and one from the add-ons scan.
    const expectedWarning = {
      message: new RegExp(
        `Ignoring distribution entry ${RegExp.escape(localeDir.path)}.*NS_ERROR_FILE_ACCESS_DENIED`
      ),
    };
    AddonTestUtils.checkMessages(
      messages,
      { expected: [expectedWarning, expectedWarning] },
      "Got the expected warnings for the unreadable distribution entries"
    );

    let addon = await AddonManager.getAddonByID(PROFILE_ID);
    notEqual(addon, null);
    ok(addon.isActive, "Profile add-on is still active");

    await addon.uninstall();
    await IOUtils.remove(localeDir.path, { recursive: true });
  }
);
