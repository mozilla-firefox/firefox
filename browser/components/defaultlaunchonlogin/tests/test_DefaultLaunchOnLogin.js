/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

/* import-globals-from ../../../../testing/xpcshell/head.js */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const {
  DefaultLaunchOnLogin,
  DEFAULT_LAUNCH_ON_LOGIN_NIMBUS_FEATURE_ID,
  DEFAULT_LAUNCH_ON_LOGIN_PREF,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/defaultlaunchonlogin/DefaultLaunchOnLogin.sys.mjs"
);
const { LaunchOnLogin } = ChromeUtils.importESModule(
  "resource://gre/modules/LaunchOnLogin.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

const CATEGORY_NAME = "browser-idle-startup";
const MODULE_URI =
  "moz-src:///browser/components/defaultlaunchonlogin/DefaultLaunchOnLogin.sys.mjs";

NimbusTestUtils.init(this);

let registry = null;
add_setup(async () => {
  // FOG needs a profile
  do_get_profile();

  Services.fog.initializeFOG();
  Services.fog.testResetFOG();
  updateAppInfo();

  const { cleanup: nimbusTestCleanup } = await NimbusTestUtils.setupTest();

  registerCleanupFunction(() => {
    nimbusTestCleanup();
  });
});

// Runs enableOnFirstRunIfNeeded with the Nimbus wait stubbed out and the
// LaunchOnLogin side effects stubbed. Sets the defaultEnabled pref to
// prefValue -- in production Nimbus writes that pref via setPref, so driving the
// pref directly exercises the module's decision logic; the Nimbus -> pref
// linkage is covered separately by test_nimbus_enrollment_sets_pref.
async function runWith(
  isFirstRun,
  isOfficialBuild,
  approved,
  alreadyApplied,
  prefValue
) {
  let sandbox = sinon.createSandbox();
  sandbox.stub(DefaultLaunchOnLogin, "waitForNimbusReady").resolves();
  let approvedStub = sandbox
    .stub(LaunchOnLogin, "isAllowed")
    .resolves(approved);
  let createStub = sandbox.stub(LaunchOnLogin, "enable").resolves();

  Services.prefs.setBoolPref(DEFAULT_LAUNCH_ON_LOGIN_PREF, prefValue);

  try {
    await DefaultLaunchOnLogin.enableOnFirstRunIfNeeded(
      isFirstRun,
      isOfficialBuild,
      alreadyApplied
    );
    return { approvedStub, createStub };
  } finally {
    sandbox.restore();
    Services.prefs.clearUserPref(DEFAULT_LAUNCH_ON_LOGIN_PREF);
  }
}

add_task(async function test_is_registered_in_idle_startup() {
  const entry = Services.catMan.getCategoryEntry(CATEGORY_NAME, MODULE_URI);
  Assert.equal(
    entry,
    "DefaultLaunchOnLogin.maybeEnableOnFirstRun",
    "Entry should point to `maybeEnableOnFirstRun` in `browser-idle-startup`"
  );
});

add_task(
  {
    skip_if: () => AppConstants.platform !== "linux",
  },
  async function test_disabled_when_pref_off() {
    let { createStub } = await runWith(true, true, true, false, false);
    Assert.ok(
      !createStub.called,
      "LaunchOnLogin.enable should not be called when the pref is off"
    );
  }
);

add_task(
  {
    skip_if: () => AppConstants.platform !== "linux",
  },
  async function test_enabled_when_pref_on() {
    let { createStub } = await runWith(true, true, true, false, true);
    Assert.ok(
      createStub.calledOnce,
      "LaunchOnLogin.enable should be called when the pref is on"
    );
  }
);

// The Nimbus -> pref linkage: enrolling in the feature should write the
// defaultEnabled pref via the setPref mapping in FeatureManifest.yaml.
add_task(
  {
    skip_if: () => AppConstants.platform !== "linux",
  },
  async function test_nimbus_enrollment_sets_pref() {
    let cleanup = await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: DEFAULT_LAUNCH_ON_LOGIN_NIMBUS_FEATURE_ID,
        value: { enabled: true },
      },
      { isRollout: true }
    );
    Assert.ok(
      Services.prefs.getBoolPref(DEFAULT_LAUNCH_ON_LOGIN_PREF, false),
      "enrolling with enabled:true sets the defaultEnabled pref to true"
    );
    await cleanup();

    cleanup = await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: DEFAULT_LAUNCH_ON_LOGIN_NIMBUS_FEATURE_ID,
        value: { enabled: false },
      },
      { isRollout: true }
    );
    Assert.ok(
      !Services.prefs.getBoolPref(DEFAULT_LAUNCH_ON_LOGIN_PREF, true),
      "enrolling with enabled:false sets the defaultEnabled pref to false"
    );
    await cleanup();
  }
);

add_task(async function test_skips_when_not_first_run() {
  let { createStub } = await runWith(false, true, true, false, true);
  Assert.ok(
    !createStub.called,
    "LaunchOnLogin.enable should not be called when isFirstRun is false"
  );
});

add_task(async function test_skips_on_unofficial_build() {
  let { createStub } = await runWith(true, false, true, false, true);
  Assert.ok(
    !createStub.called,
    "LaunchOnLogin.enable should not be called on developer builds"
  );
});

add_task(
  {
    skip_if: () => AppConstants.platform !== "linux",
  },
  async function test_skips_when_windows_policy_denies() {
    let { createStub, approvedStub } = await runWith(
      true,
      true,
      false,
      false,
      true
    );
    Assert.ok(
      approvedStub.calledOnce,
      "policy approval should be consulted when first run and the pref is on"
    );
    Assert.ok(
      !createStub.called,
      "LaunchOnLogin should not be called when Windows policy denies"
    );
  }
);

add_task(
  {
    skip_if: () => AppConstants.platform !== "linux",
  },
  async function test_skips_when_already_applied() {
    let { createStub, approvedStub } = await runWith(
      true,
      true,
      true,
      true,
      true
    );
    Assert.ok(
      !approvedStub.called,
      "policy approval should not be consulted when already applied is true"
    );

    Assert.ok(
      !createStub.called,
      "LaunchOnLogin.enable should not be called when already applied is true"
    );
  }
);
