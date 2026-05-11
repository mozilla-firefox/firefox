/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BackgroundUpdate } = ChromeUtils.importESModule(
  "resource://gre/modules/BackgroundUpdate.sys.mjs"
);
let reasons = () => BackgroundUpdate._reasonsToNotUpdateInstallation();
let REASON = BackgroundUpdate.REASON;
const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);
const { setupPolicyEngineWithJson } = EnterprisePolicyTesting;
EnterprisePolicyTesting.pathResolver = path => do_get_file(path).path;
const { UpdateService } = ChromeUtils.importESModule(
  "resource://gre/modules/UpdateService.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

// We can't reasonably check NO_MOZ_BACKGROUNDTASKS, nor NO_OMNIJAR.

function setup_enterprise_policy_testing() {
  // This initializes the policy engine for xpcshell tests
  let policies = Cc["@mozilla.org/enterprisepolicies;1"].getService(
    Ci.nsIObserver
  );
  policies.observe(null, "policies-startup", null);
}
setup_enterprise_policy_testing();

add_setup(async function test_setup() {
  // These tests use per-installation prefs, and those are a shared resource, so
  // they require some non-trivial setup.
  setupTestCommon(null);
  await standardInit();

  // FOG needs a profile directory to put its data in.
  do_get_profile();

  // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
  Services.fog.initializeFOG(undefined, "firefox.desktop.background.update");

  setupProfileService();
});

add_task(async function test_reasons_update_no_app_update_auto() {
  let prev = await UpdateUtils.getAppUpdateAutoEnabled();
  try {
    await UpdateUtils.setAppUpdateAutoEnabled(false);
    let result = await reasons();
    Assert.ok(result.includes(REASON.NO_APP_UPDATE_AUTO));
    result = await checkGleanPing();
    Assert.ok(result.includes(REASON.NO_APP_UPDATE_AUTO));

    await UpdateUtils.setAppUpdateAutoEnabled(true);
    result = await reasons();
    Assert.ok(!result.includes(REASON.NO_APP_UPDATE_AUTO));

    result = await checkGleanPing();
    Assert.ok(!result.includes(REASON.NO_APP_UPDATE_AUTO));
  } finally {
    await UpdateUtils.setAppUpdateAutoEnabled(prev);
  }
});

add_task(async function test_reasons_update_no_app_update_background_enabled() {
  let prev = await UpdateUtils.readUpdateConfigSetting(
    "app.update.background.enabled"
  );
  try {
    await UpdateUtils.writeUpdateConfigSetting(
      "app.update.background.enabled",
      false
    );
    let result = await reasons();
    Assert.ok(result.includes(REASON.NO_APP_UPDATE_BACKGROUND_ENABLED));
    result = await checkGleanPing();
    Assert.ok(result.includes(REASON.NO_APP_UPDATE_BACKGROUND_ENABLED));

    await UpdateUtils.writeUpdateConfigSetting(
      "app.update.background.enabled",
      true
    );
    result = await reasons();
    Assert.ok(!result.includes(REASON.NO_APP_UPDATE_BACKGROUND_ENABLED));
    result = await checkGleanPing();
    Assert.ok(!result.includes(REASON.NO_APP_UPDATE_BACKGROUND_ENABLED));
  } finally {
    await UpdateUtils.writeUpdateConfigSetting(
      "app.update.background.enabled",
      prev
    );
  }
});

add_task(async function test_reasons_update_cannot_usually_check() {
  // It's difficult to arrange the conditions in a testing environment, so
  // we'll use mocks to get a little assurance.
  let result = await reasons();
  Assert.ok(!result.includes(REASON.CANNOT_USUALLY_CHECK));

  let sandbox = sinon.createSandbox();
  try {
    sandbox
      .stub(UpdateService.prototype, "canUsuallyCheckForUpdates")
      .get(() => false);
    result = await reasons();
    Assert.ok(result.includes(REASON.CANNOT_USUALLY_CHECK));
    result = await checkGleanPing();
    Assert.ok(result.includes(REASON.CANNOT_USUALLY_CHECK));
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_reasons_update_can_usually_stage_or_appl() {
  // It's difficult to arrange the conditions in a testing environment, so
  // we'll use mocks to get a little assurance.
  let sandbox = sinon.createSandbox();
  try {
    sandbox
      .stub(UpdateService.prototype, "canUsuallyStageUpdates")
      .get(() => true);
    sandbox
      .stub(UpdateService.prototype, "canUsuallyApplyUpdates")
      .get(() => true);
    let result = await reasons();
    Assert.ok(
      !result.includes(REASON.CANNOT_USUALLY_STAGE_AND_CANNOT_USUALLY_APPLY)
    );
    result = await checkGleanPing();
    Assert.ok(
      !result.includes(REASON.CANNOT_USUALLY_STAGE_AND_CANNOT_USUALLY_APPLY)
    );

    sandbox
      .stub(UpdateService.prototype, "canUsuallyStageUpdates")
      .get(() => false);
    sandbox
      .stub(UpdateService.prototype, "canUsuallyApplyUpdates")
      .get(() => false);
    result = await reasons();
    Assert.ok(
      result.includes(REASON.CANNOT_USUALLY_STAGE_AND_CANNOT_USUALLY_APPLY)
    );
    result = await checkGleanPing();
    Assert.ok(
      result.includes(REASON.CANNOT_USUALLY_STAGE_AND_CANNOT_USUALLY_APPLY)
    );
  } finally {
    sandbox.restore();
  }
});

add_task(
  {
    skip_if: () =>
      !AppConstants.MOZ_BITS_DOWNLOAD || AppConstants.platform != "win",
  },
  async function test_reasons_update_can_usually_use_bits() {
    let prev = Services.prefs.getBoolPref("app.update.BITS.enabled");

    // Here we use mocks to "get by" preconditions that are not
    // satisfied in the testing environment.
    let sandbox = sinon.createSandbox();
    try {
      sandbox
        .stub(UpdateService.prototype, "canUsuallyStageUpdates")
        .get(() => true);
      sandbox
        .stub(UpdateService.prototype, "canUsuallyApplyUpdates")
        .get(() => true);

      Services.prefs.setBoolPref("app.update.BITS.enabled", false);
      let result = await reasons();
      Assert.ok(result.includes(REASON.WINDOWS_CANNOT_USUALLY_USE_BITS));
      result = await checkGleanPing();
      Assert.ok(
        result.includes(REASON.WINDOWS_CANNOT_USUALLY_USE_BITS),
        "result : " + result.join("', '") + "']"
      );

      Services.prefs.setBoolPref("app.update.BITS.enabled", true);
      result = await reasons();
      Assert.ok(!result.includes(REASON.WINDOWS_CANNOT_USUALLY_USE_BITS));
      result = await checkGleanPing();
      Assert.ok(!result.includes(REASON.WINDOWS_CANNOT_USUALLY_USE_BITS));
    } finally {
      sandbox.restore();
      Services.prefs.setBoolPref("app.update.BITS.enabled", prev);
    }
  }
);

add_task(async function test_reasons_update_manual_update_only() {
  await setupPolicyEngineWithJson({
    policies: {
      ManualAppUpdateOnly: true,
    },
  });
  Assert.equal(
    Services.policies.status,
    Ci.nsIEnterprisePolicies.ACTIVE,
    "Engine is active"
  );

  let result = await reasons();
  Assert.ok(result.includes(REASON.MANUAL_UPDATE_ONLY));
  result = await checkGleanPing();
  Assert.ok(result.includes(REASON.MANUAL_UPDATE_ONLY));

  await setupPolicyEngineWithJson({});

  result = await reasons();
  Assert.ok(!result.includes(REASON.MANUAL_UPDATE_ONLY));
  result = await checkGleanPing();
  Assert.ok(!result.includes(REASON.MANUAL_UPDATE_ONLY));
});

add_task(
  {
    skip_if: () => AppConstants.platform != "win",
  },
  async function test_unelevated_nimbus_enabled() {
    // Enable feature.
    Services.prefs.setBoolPref(
      "app.update.background.allowUpdatesForUnelevatedInstallations",
      true
    );
    registerCleanupFunction(() => {
      Services.prefs.clearUserPref(
        "app.update.background.allowUpdatesForUnelevatedInstallations"
      );
    });

    // execute!
    let r = await reasons();
    Assert.ok(
      !r.includes(BackgroundUpdate.REASON.SERVICE_REGISTRY_KEY_MISSING),
      `no SERVICE_REGISTRY_KEY_MISSING in ${JSON.stringify(r)}`
    );
    Assert.ok(
      !r.includes(BackgroundUpdate.REASON.APPBASEDIR_NOT_WRITABLE),
      `no APPBASEDIR_NOT_WRITABLE in ${JSON.stringify(r)}`
    );

    // the test directory usually is writable, but we now create a file and keep
    // it open, so that the test file can neither be deleted nor recreated and
    // appears to be locked. With that we re-execute the test and expect to find
    // APPBASEDIR_NOT_WRITABLE in the telemetry data.
    let appDirTestFile = Services.dirsvc.get(
      XRE_EXECUTABLE_FILE,
      Ci.nsIFile
    ).parent;
    appDirTestFile.append(FILE_UPDATE_TEST);
    appDirTestFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);

    var outputStream = Cc[
      "@mozilla.org/network/file-output-stream;1"
    ].createInstance(Ci.nsIFileOutputStream);
    //                              WR_ONLY|CREATE|TRUNC
    outputStream.init(appDirTestFile, 0x02 | 0x08 | 0x20, 0o644, null);
    registerCleanupFunction(() => {
      outputStream.close();
      appDirTestFile.remove(false);
    });
    // after the preperation: execute again!
    r = await reasons();
    Assert.ok(
      r.includes(BackgroundUpdate.REASON.APPBASEDIR_NOT_WRITABLE),
      `no APPBASEDIR_NOT_WRITABLE in ${JSON.stringify(r)}`
    );
  }
);

add_task(
  {
    skip_if: () => AppConstants.platform != "win",
  },
  async function test_unelevated_nimbus_disabled() {
    // Disable feature.
    Services.prefs.setBoolPref(
      "app.update.background.allowUpdatesForUnelevatedInstallations",
      false
    );
    registerCleanupFunction(() => {
      Services.prefs.clearUserPref(
        "app.update.background.allowUpdatesForUnelevatedInstallations"
      );
    });

    let r = await reasons();
    Assert.ok(
      r.includes(BackgroundUpdate.REASON.SERVICE_REGISTRY_KEY_MISSING),
      `SERVICE_REGISTRY_KEY_MISSING in ${JSON.stringify(r)}`
    );
  }
);

add_task(async () => {
  // `setupTestCommon()` calls `do_test_pending()`; this calls
  // `do_test_finish()`.  The `add_task` schedules this to run after all the
  // other tests have completed.
  await doTestFinish();
});
