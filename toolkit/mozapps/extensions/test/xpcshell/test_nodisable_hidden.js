/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This test verifies that hidden add-ons cannot be user disabled.

// for system add-ons
const distroDir = FileUtils.getDir("ProfD", ["sysfeatures"]);
distroDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
registerDirectory("XREAppFeat", distroDir);

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

const NORMAL_ID = "normal@tests.mozilla.org";
const SYSTEM_ID = "system@tests.mozilla.org";

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "42");

// normal add-ons can be user disabled.
add_task(async function () {
  await promiseStartupManager();

  await promiseInstallWebExtension({
    manifest: {
      name: "Test disabling hidden add-ons, non-hidden add-on case.",
      version: "1.0",
      browser_specific_settings: { gecko: { id: NORMAL_ID } },
    },
  });

  let addon = await promiseAddonByID(NORMAL_ID);
  Assert.notEqual(addon, null);
  Assert.equal(addon.version, "1.0");
  Assert.equal(
    addon.name,
    "Test disabling hidden add-ons, non-hidden add-on case."
  );
  Assert.ok(addon.isCompatible);
  Assert.ok(!addon.appDisabled);
  Assert.ok(!addon.userDisabled);
  Assert.ok(addon.isActive);
  Assert.equal(addon.type, "extension");

  // normal add-ons can be disabled by the user.
  await addon.disable();

  Assert.notEqual(addon, null);
  Assert.equal(addon.version, "1.0");
  Assert.equal(
    addon.name,
    "Test disabling hidden add-ons, non-hidden add-on case."
  );
  Assert.ok(addon.isCompatible);
  Assert.ok(!addon.appDisabled);
  Assert.ok(addon.userDisabled);
  Assert.ok(!addon.isActive);
  Assert.equal(addon.type, "extension");

  await addon.uninstall();

  await promiseShutdownManager();
});

// system add-ons installed in the system builtin location can never be user disabled.
add_task(async function test_legacy_system_defaults_builtin_location() {
  const addon_res_url_path = "test-builtin-systemaddon";
  await setupBuiltinExtension(
    {
      manifest: {
        name: "Test disabling hidden add-ons, hidden system add-on case.",
        version: "1.0",
        browser_specific_settings: { gecko: { id: SYSTEM_ID } },
      },
    },
    addon_res_url_path
  );
  await overrideBuiltIns({
    builtins: [
      {
        addon_id: SYSTEM_ID,
        addon_version: "1.0",
        res_url: `resource://${addon_res_url_path}/`,
      },
    ],
  });

  await promiseStartupManager();

  let addon = await promiseAddonByID(SYSTEM_ID);
  Assert.notEqual(addon, null);
  Assert.equal(addon.version, "1.0");
  Assert.equal(
    addon.name,
    "Test disabling hidden add-ons, hidden system add-on case."
  );
  Assert.ok(addon.isCompatible);
  Assert.ok(!addon.appDisabled);
  Assert.ok(!addon.userDisabled);
  Assert.ok(addon.isActive);
  Assert.equal(addon.type, "extension");

  // system add-ons cannot be disabled by the user.
  await Assert.rejects(
    addon.disable(),
    err => err.message == `Cannot disable system add-on ${SYSTEM_ID}`,
    "disable() on a hidden add-on should fail"
  );

  Assert.ok(!addon.userDisabled);
  Assert.ok(addon.isActive);

  await promiseShutdownManager();
});
