/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../../../extensions/newtab/test/xpcshell/head.js */

/* import-globals-from head_nimbus_trainhop.js */

const { AboutHomeStartupCache } = ChromeUtils.importESModule(
  "resource:///modules/AboutHomeStartupCache.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { FirstStartup } = ChromeUtils.importESModule(
  "resource://gre/modules/FirstStartup.sys.mjs"
);
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

const PREF_CATEGORY_TASKS = "first-startup.category-tasks-enabled";
const CATEGORY_NAME = "first-startup-new-profile";

add_setup(async () => {
  Services.fog.testResetFOG();
  updateAppInfo();
});

/**
 * Test that AboutNewTabResourceMapping has a first-startup-new-profile
 * category entry registered for it for the
 * AboutNewTabResourceMapping.firstStartupNewProfile method.
 */
add_task(async function test_is_firstStartupNewProfile_registered() {
  const entry = Services.catMan.getCategoryEntry(
    CATEGORY_NAME,
    "resource:///modules/AboutNewTabResourceMapping.sys.mjs"
  );
  Assert.ok(
    entry,
    "An entry should exist for resource:///modules/AboutNewTabResourceMapping.sys.mjs"
  );
  Assert.equal(
    entry,
    "AboutNewTabResourceMapping.firstStartupNewProfile",
    "Entry value should point to the `firstStartupNewProfile` method"
  );
});

/**
 * Test that the firstStartupNewProfile hook gets called during FirstStartup
 * and performs a restartless install of a train-hop add-on when Nimbus is
 * configured with one.
 */
add_task(async function test_firstStartup_trainhop_restartless_install() {
  // Enable category tasks for first startup
  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, true);
  FirstStartup.resetForTesting();

  // Reset AboutNewTabResourceMapping state so firstStartupNewProfile can run
  mockAboutNewTabUninit();

  // Sanity check - verify built-in add-on resources have been mapped
  assertNewTabResourceMapping();
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
  });
  assertTrainhopAddonNimbusExposure({ expectedExposure: false });

  const updateAddonVersion = `${BUILTIN_ADDON_VERSION}.123`;

  const { nimbusFeatureCleanup } = await setupNimbusTrainhopAddon({
    updateAddonVersion,
  });

  // Track whether firstStartupNewProfile was called
  let sandbox = sinon.createSandbox();

  let firstStartupNewProfileSpy = sandbox.spy(
    AboutNewTabResourceMapping,
    "firstStartupNewProfile"
  );
  let aboutHomeStartupClearCacheStub = sandbox.stub(
    AboutHomeStartupCache,
    "clearCacheAndUninit"
  );

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      Assert.equal(FirstStartup.state, FirstStartup.SUCCESS);
      resolve();
    });
  });

  // Run FirstStartup which should trigger our category hook
  FirstStartup.init(true /* newProfile */);

  await submissionPromise;
  assertTrainhopAddonVersionPref(updateAddonVersion);

  Assert.ok(
    firstStartupNewProfileSpy.calledOnce,
    "firstStartupNewProfile should have been called"
  );
  Assert.ok(
    aboutHomeStartupClearCacheStub.calledOnce,
    "AboutHomeStartupCache.clearCacheAndUninit called after installing train-hop"
  );

  // The train-hop add-on should have been installed restartlessly
  let addon = await asyncAssertNewTabAddon({
    locationName: PROFILE_LOCATION_NAME,
    version: updateAddonVersion,
  });

  Assert.ok(addon, "Train-hop add-on should be installed");

  // No pending installs should remain since we did a restartless install
  Assert.deepEqual(
    await AddonManager.getAllInstalls(),
    [],
    "Expect no pending install for restartless install"
  );

  sandbox.restore();

  await nimbusFeatureCleanup();
  info(
    "Simulated browser restart while newtabTrainhopAddonDeployment nimbus feature is unenrolled"
  );
  mockAboutNewTabUninit();
  await AddonTestUtils.promiseRestartManager();
  AboutNewTab.init();

  // Expected bundled newtab resources mapping for this session.
  assertNewTabResourceMapping();
  await AboutNewTabResourceMapping.updateTrainhopAddonState();
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
    version: BUILTIN_ADDON_VERSION,
  });

  assertTrainhopAddonVersionPref("");
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});

/**
 * Test that if AboutNewTabResourceMapping.init() has already been called
 * by the time firstStartupNewProfile runs, it logs an error and exits early.
 * This is not an expected or realistic condition, but we cover it all the same.
 */
add_task(async function test_firstStartup_after_initialization() {
  // Initialize AboutNewTabResourceMapping before FirstStartup runs.
  AboutNewTabResourceMapping.init();
  Assert.ok(
    AboutNewTabResourceMapping.initialized,
    "AboutNewTabResourceMapping should be initialized"
  );

  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, true);
  FirstStartup.resetForTesting();

  const updateAddonVersion = `${BUILTIN_ADDON_VERSION}.456`;

  const { nimbusFeatureCleanup } = await setupNimbusTrainhopAddon({
    updateAddonVersion,
  });

  // Track error logging
  let errorLogged = false;
  let sandbox = sinon.createSandbox();
  sandbox.stub(AboutNewTabResourceMapping.logger, "error").callsFake(() => {
    errorLogged = true;
  });

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      resolve();
    });
  });

  FirstStartup.init(true /* newProfile */);
  await submissionPromise;

  Assert.ok(
    errorLogged,
    "An error should have been logged when trying to run after initialization"
  );

  // The add-on should NOT have been installed since we were too late
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
    version: BUILTIN_ADDON_VERSION,
  });

  sandbox.restore();
  await nimbusFeatureCleanup();
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});

/**
 * Test that firstStartupNewProfile doesn't run when the category tasks pref
 * is disabled.
 */
add_task(async function test_firstStartup_category_disabled() {
  // Disable category tasks
  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, false);
  FirstStartup.resetForTesting();

  // Reset AboutNewTabResourceMapping state
  mockAboutNewTabUninit();

  const updateAddonVersion = `${BUILTIN_ADDON_VERSION}.789`;

  const { nimbusFeatureCleanup } = await setupNimbusTrainhopAddon({
    updateAddonVersion,
  });

  let sandbox = sinon.createSandbox();
  let firstStartupNewProfileSpy = sandbox.spy(
    AboutNewTabResourceMapping,
    "firstStartupNewProfile"
  );

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      resolve();
    });
  });

  FirstStartup.init(true /* newProfile */);
  await submissionPromise;

  Assert.ok(
    !firstStartupNewProfileSpy.called,
    "firstStartupNewProfile should not have been called when pref is disabled"
  );

  // The add-on should still be the builtin version
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
    version: BUILTIN_ADDON_VERSION,
  });

  sandbox.restore();
  await nimbusFeatureCleanup();
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});

/**
 * Test that if AboutNewTabResourceMapping.init() is called after the XPI
 * download has started but before onInstallPostponed is called, we skip
 * attempting to force the restartless install and fall back to a staged
 * install instead.
 */
add_task(async function test_firstStartup_init_during_download() {
  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, true);
  FirstStartup.resetForTesting();

  // Reset AboutNewTabResourceMapping state so firstStartupNewProfile can run
  mockAboutNewTabUninit();

  assertNewTabResourceMapping();
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
  });

  const updateAddonVersion = `${BUILTIN_ADDON_VERSION}.999`;

  const { nimbusFeatureCleanup } = await setupNimbusTrainhopAddon({
    updateAddonVersion,
  });

  // Stub updateTrainhopAddonState to call init() in the middle of its execution
  let sandbox = sinon.createSandbox();
  let aboutNewTabInitSpy = sandbox.spy(AboutNewTabResourceMapping, "init");

  let originalUpdateTrainhopAddonState =
    AboutNewTabResourceMapping.updateTrainhopAddonState.bind(
      AboutNewTabResourceMapping
    );

  let updateTrainhopStarted = false;
  sandbox
    .stub(AboutNewTabResourceMapping, "updateTrainhopAddonState")
    .callsFake(async function (forceRestartlessInstall) {
      updateTrainhopStarted = true;

      // Start the update process
      let updatePromise = originalUpdateTrainhopAddonState(
        forceRestartlessInstall
      );

      // Call init immediately after starting the update, simulating
      // the browser window opening during the XPI download
      info(
        "Calling AboutNewTabResourceMapping.init() during updateTrainhopAddonState"
      );
      AboutNewTabResourceMapping.init();

      // Wait for the update to complete
      await updatePromise;
    });

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      Assert.equal(FirstStartup.state, FirstStartup.SUCCESS);
      resolve();
    });
  });

  FirstStartup.init(true /* newProfile */);
  await submissionPromise;
  assertTrainhopAddonVersionPref(updateAddonVersion);

  Assert.ok(
    updateTrainhopStarted,
    "updateTrainhopAddonState should have started"
  );
  Assert.ok(
    aboutNewTabInitSpy.calledOnce,
    "AboutNewTabResourceMapping.init should have been called"
  );

  // The add-on should be staged for install, not installed restartlessly
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
    version: BUILTIN_ADDON_VERSION,
  });

  // Verify there's a pending install
  const pendingInstall = (await AddonManager.getAllInstalls()).find(
    install => install.addon.id === BUILTIN_ADDON_ID
  );
  Assert.ok(pendingInstall, "Should have a pending install");
  Assert.equal(
    pendingInstall.state,
    AddonManager.STATE_POSTPONED,
    "Install should be postponed"
  );
  Assert.equal(
    pendingInstall.addon.version,
    updateAddonVersion,
    "Pending install should be for the train-hop version"
  );

  // Clean up
  await cancelPendingInstall(pendingInstall);
  sandbox.restore();
  await nimbusFeatureCleanup();
  await AboutNewTabResourceMapping.updateTrainhopAddonState();
  assertTrainhopAddonVersionPref("");
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});

/**
 * Test that the TRAINHOP_NIMBUS_FIRST_STARTUP_FEATURE_ID Nimbus feature can be
 * used to remotely disable the FirstStartup force-install flow.
 */
add_task(async function test_firstStartup_remote_disable() {
  // Enable category tasks for first startup
  Services.prefs.setBoolPref(PREF_CATEGORY_TASKS, true);
  FirstStartup.resetForTesting();

  // Reset AboutNewTabResourceMapping state so firstStartupNewProfile can run
  mockAboutNewTabUninit();

  // Sanity check - verify built-in add-on resources have been mapped
  assertNewTabResourceMapping();
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
  });
  assertTrainhopAddonNimbusExposure({ expectedExposure: false });

  const updateAddonVersion = `${BUILTIN_ADDON_VERSION}.123`;

  const { nimbusFeatureCleanup } = await setupNimbusTrainhopAddon({
    updateAddonVersion,
  });

  const firstStartupFeatureCleanup =
    await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: TRAINHOP_NIMBUS_FIRST_STARTUP_FEATURE_ID,
        value: { enabled: false },
      },
      { isRollout: true }
    );

  // Track whether firstStartupNewProfile was called
  let sandbox = sinon.createSandbox();
  let firstStartupNewProfileSpy = sandbox.spy(
    AboutNewTabResourceMapping,
    "firstStartupNewProfile"
  );

  let submissionPromise = new Promise(resolve => {
    GleanPings.firstStartup.testBeforeNextSubmit(() => {
      Assert.equal(FirstStartup.state, FirstStartup.SUCCESS);
      resolve();
    });
  });

  // Run FirstStartup which should trigger our category hook
  FirstStartup.init(true /* newProfile */);

  await submissionPromise;

  Assert.ok(
    firstStartupNewProfileSpy.calledOnce,
    "firstStartupNewProfile should have been called"
  );

  // The add-on should still be the builtin version
  await asyncAssertNewTabAddon({
    locationName: BUILTIN_LOCATION_NAME,
    version: BUILTIN_ADDON_VERSION,
  });

  sandbox.restore();
  await nimbusFeatureCleanup();
  await firstStartupFeatureCleanup();
  assertTrainhopAddonVersionPref("");
  Services.prefs.clearUserPref(PREF_CATEGORY_TASKS);
});
