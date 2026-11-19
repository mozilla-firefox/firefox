/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  BackgroundUpdate: "resource://gre/modules/BackgroundUpdate.sys.mjs",
  MigrationUtils:
    "moz-src:///browser/components/migration/MigrationUtils.sys.mjs",
  PermissionTestUtils: "resource://testing-common/PermissionTestUtils.sys.mjs",
  WindowsLaunchOnLogin:
    "resource://gre/modules/launchonlogin/WindowsLaunchOnLogin.sys.mjs",
});

const { MockRegistry } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistry.sys.mjs"
);

let profileService = Cc["@mozilla.org/toolkit/profile-service;1"].getService(
  Ci.nsIToolkitProfileService
);
let startWithLastProfileOriginal = profileService.startWithLastProfile;
let registry = null;

const STARTUP_PANE = "paneHome";
// The StartupApproved value Windows writes when the user disables a startup
// app: an odd first byte, followed by a timestamp.
const STARTUP_APPROVED_DISABLED = "\x03" + "\x00".repeat(11);

add_setup(() => {
  registry = new MockRegistry();
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
    "",
    ""
  );
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
    "",
    ""
  );
  registerCleanupFunction(() => {
    registry.shutdown();
  });
});

add_task(async function test_check_uncheck_checkbox() {
  await ExperimentAPI.ready();
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "windowsLaunchOnLogin",
    value: { enabled: true },
  });
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    Services.fog.testResetFOG();

    // Open preferences to general pane
    await openPreferencesViaOpenPreferencesAPI(STARTUP_PANE, {
      leaveOpen: true,
    });
    let doc = gBrowser.contentDocument;
    await TestUtils.waitForCondition(
      () => doc.getElementById("windowsLaunchOnLogin"),
      "windowsLaunchOnLogin checkbox rendered"
    );

    let launchOnLoginCheckbox = doc.getElementById("windowsLaunchOnLogin");
    let launchOnLoginControl = launchOnLoginCheckbox.parentElement;

    ok(!launchOnLoginControl.hidden, "Autostart control is visible");

    ok(
      !launchOnLoginCheckbox.checked,
      "Autostart checkbox NOT checked by default"
    );

    synthesizeClick(launchOnLoginCheckbox);

    ok(launchOnLoginCheckbox.checked, "Autostart checkbox checked after click");

    ok(
      wrk.hasValue(WindowsLaunchOnLogin.getLaunchOnLoginRegistryName()),
      "Key exists"
    );

    let toggleEvents = Glean.launchOnLogin.userToggle.testGetValue();
    Assert.equal(toggleEvents.length, 1, "One toggle event after checking");
    Assert.equal(
      toggleEvents[0].extra.enabled,
      "true",
      "Toggle event reports enabled=true"
    );

    synthesizeClick(launchOnLoginCheckbox);
    ok(!launchOnLoginCheckbox.checked, "Autostart checkbox unchecked");

    toggleEvents = Glean.launchOnLogin.userToggle.testGetValue();
    Assert.equal(toggleEvents.length, 2, "Two toggle events after unchecking");
    Assert.equal(
      toggleEvents[1].extra.enabled,
      "false",
      "Toggle event reports enabled=false"
    );

    await TestUtils.waitForCondition(
      () => !wrk.hasValue(WindowsLaunchOnLogin.getLaunchOnLoginRegistryName()),
      "Waiting for Autostart registry key to be removed",
      undefined,
      200
    );

    gBrowser.removeCurrentTab();
  });
  await doCleanup();
});

add_task(async function create_external_regkey() {
  if (Services.sysinfo.getProperty("hasWinPackageId")) {
    return;
  }
  await ExperimentAPI.ready();
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "windowsLaunchOnLogin",
    value: { enabled: true },
  });
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    // Delete any existing entries before testing
    // Both functions are install specific so it's safe to run them
    // like this.
    wrk.removeValue(WindowsLaunchOnLogin.getLaunchOnLoginRegistryName());
    await WindowsLaunchOnLogin._removeLaunchOnLoginShortcuts();
    // Create registry key without using settings pane to check if
    // this is reflected in the settings
    let autostartPath =
      WindowsLaunchOnLogin.quoteString(
        Services.dirsvc.get("XREExeF", Ci.nsIFile).path
      ) + " -os-autostart";
    wrk.writeStringValue(
      WindowsLaunchOnLogin.getLaunchOnLoginRegistryName(),
      autostartPath
    );

    // Open preferences to general pane
    await openPreferencesViaOpenPreferencesAPI(STARTUP_PANE, {
      leaveOpen: true,
    });
    let doc = gBrowser.contentDocument;
    await TestUtils.waitForCondition(
      () => doc.getElementById("windowsLaunchOnLogin"),
      "windowsLaunchOnLogin checkbox rendered"
    );

    let launchOnLoginCheckbox = doc.getElementById("windowsLaunchOnLogin");
    ok(
      launchOnLoginCheckbox.checked,
      "Autostart checkbox automatically checked"
    );

    gBrowser.removeCurrentTab();
  });
  await doCleanup();
});

add_task(async function testRemoveLaunchOnLoginGuard() {
  let registryName = WindowsLaunchOnLogin.getLaunchOnLoginRegistryName();
  // Simulate launch on login disabled from Windows settings
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
    registryName,
    STARTUP_APPROVED_DISABLED,
    Ci.nsIWindowsRegKey.TYPE_BINARY
  );
  // This function should now be non-op
  WindowsLaunchOnLogin.removeLaunchOnLogin();
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    ok(wrk.hasValue(registryName), "Registry value is not deleted");
  });
});

add_task(async function disabled_in_windows_settings() {
  if (Services.sysinfo.getProperty("hasWinPackageId")) {
    return;
  }
  await ExperimentAPI.ready();
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "windowsLaunchOnLogin",
    value: { enabled: true },
  });
  let registryName = WindowsLaunchOnLogin.getLaunchOnLoginRegistryName();
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    await WindowsLaunchOnLogin._removeLaunchOnLoginShortcuts();
    wrk.writeStringValue(
      registryName,
      WindowsLaunchOnLogin.quoteString(
        Services.dirsvc.get("XREExeF", Ci.nsIFile).path
      ) + " -os-autostart"
    );
    // Simulate launch on login disabled from Windows settings, which leaves
    // the Run value in place.
    registry.setValue(
      Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
      "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
      registryName,
      STARTUP_APPROVED_DISABLED,
      Ci.nsIWindowsRegKey.TYPE_BINARY
    );

    let details =
      await WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails();
    ok(!details.isEnabled, "Not enabled while disabled in Windows settings");
    ok(!details.isAllowedByPolicy, "Not allowed while disabled in Windows");

    await openPreferencesViaOpenPreferencesAPI(STARTUP_PANE, {
      leaveOpen: true,
    });
    let doc = gBrowser.contentDocument;
    await TestUtils.waitForCondition(
      () => doc.getElementById("windowsLaunchOnLogin")?.disabled,
      "windowsLaunchOnLogin checkbox rendered and disabled"
    );

    let launchOnLoginCheckbox = doc.getElementById("windowsLaunchOnLogin");
    ok(!launchOnLoginCheckbox.checked, "Autostart checkbox unchecked");
    ok(
      BrowserTestUtils.isVisible(
        doc.getElementById("windowsLaunchOnLoginDisabledBox")
      ),
      "Disabled in Windows message is visible"
    );

    gBrowser.removeCurrentTab();
    wrk.removeValue(registryName);
  });
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
    registryName,
    null
  );
  await doCleanup();
});

add_task(async function delete_external_regkey() {
  if (Services.sysinfo.getProperty("hasWinPackageId")) {
    return;
  }
  await ExperimentAPI.ready();
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "windowsLaunchOnLogin",
    value: { enabled: true },
  });
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    // Delete registry key without using settings pane to check if
    // this is reflected in the settings
    wrk.removeValue(WindowsLaunchOnLogin.getLaunchOnLoginRegistryName());

    // Open preferences to general pane
    await openPreferencesViaOpenPreferencesAPI(STARTUP_PANE, {
      leaveOpen: true,
    });
    let doc = gBrowser.contentDocument;
    await TestUtils.waitForCondition(
      () => doc.getElementById("windowsLaunchOnLogin"),
      "windowsLaunchOnLogin checkbox rendered"
    );

    let launchOnLoginCheckbox = doc.getElementById("windowsLaunchOnLogin");
    ok(
      !launchOnLoginCheckbox.checked,
      "Launch on login checkbox automatically unchecked"
    );

    gBrowser.removeCurrentTab();
  });
  await doCleanup();
});

add_task(async function test_launch_on_login_with_profile_manager() {
  profileService.startWithLastProfile = false;
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
    WindowsLaunchOnLogin.getLaunchOnLoginRegistryName(),
    null
  );
  await ExperimentAPI.ready();
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "windowsLaunchOnLogin",
    value: { enabled: true },
  });
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    let registryName = WindowsLaunchOnLogin.getLaunchOnLoginRegistryName();
    if (wrk.hasValue(registryName)) {
      wrk.removeValue(registryName);
    }
    await WindowsLaunchOnLogin._removeLaunchOnLoginShortcuts();

    await openPreferencesViaOpenPreferencesAPI(STARTUP_PANE, {
      leaveOpen: true,
    });
    let doc = gBrowser.contentDocument;
    await TestUtils.waitForCondition(
      () => doc.getElementById("windowsLaunchOnLogin"),
      "windowsLaunchOnLogin checkbox rendered"
    );

    let launchOnLoginCheckbox = doc.getElementById("windowsLaunchOnLogin");
    ok(!launchOnLoginCheckbox.disabled, "Autostart checkbox enabled");
    ok(!launchOnLoginCheckbox.checked, "Autostart checkbox unchecked");

    synthesizeClick(launchOnLoginCheckbox);
    ok(launchOnLoginCheckbox.checked, "Autostart checkbox checked");
    ok(wrk.hasValue(registryName), "Key exists");

    gBrowser.removeCurrentTab();
  });
  profileService.startWithLastProfile = startWithLastProfileOriginal;
  await doCleanup();
});

registerCleanupFunction(async function () {
  await WindowsLaunchOnLogin._removeLaunchOnLoginShortcuts();
  await WindowsLaunchOnLogin.withLaunchOnLoginRegistryKey(async wrk => {
    let registryName = WindowsLaunchOnLogin.getLaunchOnLoginRegistryName();
    if (wrk.hasValue(registryName)) {
      wrk.removeValue(registryName);
    }
  });
  profileService.startWithLastProfile = startWithLastProfileOriginal;
});
