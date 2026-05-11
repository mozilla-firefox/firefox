/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { StartupTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/StartupTelemetry.sys.mjs"
);

let WindowsLaunchOnLogin;
if (AppConstants.platform == "win") {
  ({ WindowsLaunchOnLogin } = ChromeUtils.importESModule(
    "resource://gre/modules/WindowsLaunchOnLogin.sys.mjs"
  ));
}

add_setup(function test_setup() {
  do_get_profile();
  Services.fog.initializeFOG();
});

add_task(async function test_not_supported_on_non_windows() {
  if (AppConstants.platform == "win") {
    return;
  }
  Services.fog.testResetFOG();

  await StartupTelemetry.launchOnLoginState();

  Assert.equal(
    Glean.osEnvironment.launchOnLoginState.testGetValue(),
    "not_supported",
    "Should report not_supported on non-Windows platforms"
  );
});

add_task(async function test_enabled() {
  if (AppConstants.platform != "win") {
    return;
  }
  Services.fog.testResetFOG();

  let original = WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails;
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = async () => ({
    isEnabled: true,
    isSupported: true,
    isAllowedByPolicy: true,
  });

  await StartupTelemetry.launchOnLoginState();

  Assert.equal(
    Glean.osEnvironment.launchOnLoginState.testGetValue(),
    "enabled",
    "Should report enabled when launch on login is active"
  );
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = original;
});

add_task(async function test_disabled() {
  if (AppConstants.platform != "win") {
    return;
  }
  Services.fog.testResetFOG();

  let original = WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails;
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = async () => ({
    isEnabled: false,
    isSupported: true,
    isAllowedByPolicy: true,
  });

  await StartupTelemetry.launchOnLoginState();

  Assert.equal(
    Glean.osEnvironment.launchOnLoginState.testGetValue(),
    "disabled",
    "Should report disabled when user has not enabled launch on login"
  );
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = original;
});

add_task(async function test_disabled_by_settings() {
  if (AppConstants.platform != "win") {
    return;
  }
  Services.fog.testResetFOG();

  let original = WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails;
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = async () => ({
    isEnabled: false,
    isSupported: true,
    isAllowedByPolicy: false,
  });

  await StartupTelemetry.launchOnLoginState();

  Assert.equal(
    Glean.osEnvironment.launchOnLoginState.testGetValue(),
    "disabled_by_settings",
    "Should report disabled_by_settings when OS settings or policy block the feature"
  );
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = original;
});

add_task(async function test_error_on_exception() {
  if (AppConstants.platform != "win") {
    return;
  }
  Services.fog.testResetFOG();

  let original = WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails;
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = async () => {
    throw new Error("simulated enablement details failure");
  };

  await StartupTelemetry.launchOnLoginState();

  Assert.equal(
    Glean.osEnvironment.launchOnLoginState.testGetValue(),
    "error",
    "Should report error when an exception occurs"
  );
  WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails = original;
});
