/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

// This verifies that system addon about:config prefs
// are honored during startup/restarts/upgrades.
createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "2");

let distroDir = FileUtils.getDir("ProfD", ["sysfeatures", "empty"]);
distroDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
registerDirectory("XREAppFeat", distroDir);

AddonTestUtils.usePrivilegedSignatures = "system";

BootstrapMonitor.init();

add_setup(async function setup() {
  await initSystemAddonDirs();
  let xpi = await getSystemAddonXPI(1, "1.0");
  await AddonTestUtils.manuallyInstall(xpi, distroDir);
});

add_task(async function systemAddonPreffedOff() {
  const id = "system1@tests.mozilla.org";
  Services.prefs.setBoolPref("extensions.system1.enabled", false);

  await overrideBuiltIns({
    builtins: [await getSystemBuiltin(1, "1.0")],
  });

  await promiseStartupManager("1.0");

  BootstrapMonitor.checkInstalled(id);
  BootstrapMonitor.checkNotStarted(id);

  await promiseRestartManager();

  BootstrapMonitor.checkNotStarted(id);

  await promiseShutdownManager({ clearOverrides: false });
});

add_task(async function systemAddonPreffedOn() {
  const id = "system1@tests.mozilla.org";
  Services.prefs.setBoolPref("extensions.system1.enabled", true);

  await overrideBuiltIns({
    builtins: [await getSystemBuiltin(1, "1.0")],
  });

  await promiseStartupManager("2.0");

  BootstrapMonitor.checkInstalled(id);
  BootstrapMonitor.checkStarted(id);

  await promiseRestartManager();

  BootstrapMonitor.checkStarted(id);

  await promiseShutdownManager({ clearOverrides: false });
});
