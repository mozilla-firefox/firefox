// Tests that only allowed built-in system add-ons are loaded on startup.

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "0");

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

// Ensure that only allowed built-in system add-ons are loaded.
add_task(async function test_allowed_builtin_system_addons() {
  // Build the test set
  let overrideEntrySystem1 = await getSystemBuiltin(1, "1.0");
  let overrideEntrySystem2 = await getSystemBuiltin(2, "1.0");
  let overrideEntrySystem3 = await getSystemBuiltin(3, "1.0");

  // 1 and 2 are allowed, 3 is not.
  let validAddons = {
    builtins: [overrideEntrySystem1, overrideEntrySystem2],
  };
  await overrideBuiltIns(validAddons);

  await promiseStartupManager();

  let addon = await AddonManager.getAddonByID("system1@tests.mozilla.org");
  notEqual(addon, null);

  addon = await AddonManager.getAddonByID("system2@tests.mozilla.org");
  notEqual(addon, null);

  addon = await AddonManager.getAddonByID("system3@tests.mozilla.org");
  Assert.equal(addon, null);

  // 3 is now allowed, 1 and 2 are not.
  validAddons = { builtins: [overrideEntrySystem3] };
  await overrideBuiltIns(validAddons);

  await promiseRestartManager();

  addon = await AddonManager.getAddonByID("system1@tests.mozilla.org");
  equal(addon, null);

  addon = await AddonManager.getAddonByID("system2@tests.mozilla.org");
  equal(addon, null);

  addon = await AddonManager.getAddonByID("system3@tests.mozilla.org");
  notEqual(addon, null);

  await promiseShutdownManager();
});
