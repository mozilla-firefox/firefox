/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { WindowLock } = ChromeUtils.importESModule(
  "moz-src:///browser/modules/WindowLock.sys.mjs"
);

const FEATURE_GATE_PREF =
  "browser.privatebrowsing.lockOnScreenLock.featureGate";
const LOCK_PRIVATE_WINDOWS_PREF =
  "browser.privatebrowsing.lockOnScreenLock.enabled";

NimbusTestUtils.init(this);

const defaultBranch = Services.prefs.getDefaultBranch("");
const originalDefault = defaultBranch.getBoolPref(FEATURE_GATE_PREF);

add_setup(async function () {
  const { cleanup } = await NimbusTestUtils.setupTest();
  await ExperimentAPI.ready();
  Services.prefs.setBoolPref(LOCK_PRIVATE_WINDOWS_PREF, true);

  registerCleanupFunction(async () => {
    await cleanup();
    defaultBranch.setBoolPref(FEATURE_GATE_PREF, originalDefault);
    Services.prefs.clearUserPref(LOCK_PRIVATE_WINDOWS_PREF);
  });
});

add_task(function test_feature_gate_default() {
  Assert.equal(
    originalDefault,
    AppConstants.NIGHTLY_BUILD,
    "Only enabled by default on Nightly"
  );
});

add_task(async function test_nimbus_enables_feature() {
  // Set the default so the enrollment has to change it, regardless of the
  // channel's default.
  defaultBranch.setBoolPref(FEATURE_GATE_PREF, false);
  Assert.ok(!WindowLock.enabled, "Disabled before enrolling");

  const doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "windowLock",
    value: { enabled: true },
  });
  Assert.ok(
    defaultBranch.getBoolPref(FEATURE_GATE_PREF),
    "The enrollment enables the feature"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(FEATURE_GATE_PREF),
    "No user value"
  );
  Assert.ok(WindowLock.enabled, "Locking windows is enabled");

  await doCleanup();
  Assert.ok(
    !defaultBranch.getBoolPref(FEATURE_GATE_PREF),
    "Unenrolling restores the default"
  );
  Assert.ok(!WindowLock.enabled, "Locking windows is disabled again");
});
