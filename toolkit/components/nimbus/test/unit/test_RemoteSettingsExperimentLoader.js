"use strict";

const { FirstStartup } = ChromeUtils.importESModule(
  "resource://gre/modules/FirstStartup.sys.mjs"
);
const { EnrollmentsContext, MatchStatus } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const RUN_INTERVAL_PREF = "app.normandy.run_interval_seconds";
const STUDIES_OPT_OUT_PREF = "app.shield.optoutstudies.enabled";
const UPLOAD_PREF = "datareporting.healthreport.uploadEnabled";
const DEBUG_PREF = "nimbus.debug";
const AI_FEATURES_ENABLED_PREF = "browser.ai.control.default";

add_setup(async function setup() {
  Services.fog.initializeFOG();
});

add_task(async function test_lazy_pref_getters() {
  const { sandbox, loader, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.stub(loader, "updateRecipes").resolves();

  Services.prefs.setIntPref(RUN_INTERVAL_PREF, 123456);
  equal(
    loader.intervalInSeconds,
    123456,
    `should set intervalInSeconds to the value of ${RUN_INTERVAL_PREF}`
  );

  Services.prefs.clearUserPref(RUN_INTERVAL_PREF);

  await cleanup();
});

add_task(async function test_init_with_opt_in() {
  const { sandbox, loader, cleanup } = await NimbusTestUtils.setupTest({
    init: false,
    migrationState: NimbusTestUtils.migrationState.LATEST,
  });
  sandbox.spy(loader, "setTimer");
  sandbox.spy(loader, "updateRecipes");

  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, false);

  await ExperimentAPI.init();

  Assert.equal(
    loader.setTimer.callCount,
    1,
    `should initialize even if ${STUDIES_OPT_OUT_PREF} pref is false`
  );

  Assert.equal(
    loader.updateRecipes.callCount,
    1,
    "should call updateRecipes()"
  );

  await cleanup();

  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);
});

add_task(async function test_updateRecipes() {
  const passRecipe = NimbusTestUtils.factories.recipe("pass", {
    bucketConfig: {
      ...NimbusTestUtils.factories.recipe.bucketConfig,
      count: 0,
    },
    targeting: "true",
  });
  const failRecipe = NimbusTestUtils.factories.recipe("fail", {
    targeting: "false",
  });

  const { sandbox, loader, manager, cleanup } = await NimbusTestUtils.setupTest(
    {
      init: false,
      experiments: [passRecipe, failRecipe],
    }
  );

  sandbox.spy(loader, "updateRecipes");
  sandbox.stub(manager, "onRecipe").resolves();

  await ExperimentAPI.init();

  Assert.ok(loader.updateRecipes.calledOnce, "should call .updateRecipes");
  Assert.equal(
    loader.manager.onRecipe.callCount,
    2,
    "should call .onRecipe only for all recipes"
  );

  Assert.ok(
    loader.manager.onRecipe.calledWith(passRecipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_ONLY,
    }),
    "should call .onRecipe for pass recipe with TARGETING_ONLY"
  );
  Assert.ok(
    loader.manager.onRecipe.calledWith(failRecipe, "rs-loader", {
      ok: true,
      status: MatchStatus.NO_MATCH,
    }),
    "should call .onRecipe for fail recipe with NO_MATCH"
  );

  await cleanup();
});

add_task(async function test_enrollmentsContextFirstStartup() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.stub(FirstStartup, "state").get(() => FirstStartup.IN_PROGRESS);

  const ctx = new EnrollmentsContext(manager);

  Assert.ok(
    await ctx.checkTargeting(
      NimbusTestUtils.factories.recipe("is-first-startup", {
        targeting: "isFirstStartup",
      })
    ),
    "isFirstStartup targeting works when true"
  );

  sandbox.stub(FirstStartup, "state").get(() => FirstStartup.NOT_STARTED);

  Assert.ok(
    await ctx.checkTargeting(
      NimbusTestUtils.factories.recipe("not-first-startup", {
        targeting: "!isFirstStartup",
      })
    ),
    "isFirstStartup targeting works when false"
  );

  await cleanup();
});

add_task(async function test_checkTargeting() {
  const loader = NimbusTestUtils.stubs.rsLoader();

  const ctx = new EnrollmentsContext(loader.manager);
  Assert.equal(
    await ctx.checkTargeting({}),
    true,
    "should return true if .targeting is not defined"
  );
  Assert.equal(
    await ctx.checkTargeting({
      targeting: "'foo'",
      slug: "test_checkTargeting",
    }),
    true,
    "should return true for truthy expression"
  );
  Assert.equal(
    await ctx.checkTargeting({
      targeting: "aPropertyThatDoesNotExist",
      slug: "test_checkTargeting",
    }),
    false,
    "should return false for falsey expression"
  );
});

add_task(async function test_checkExperimentSelfReference() {
  const loader = NimbusTestUtils.stubs.rsLoader();
  const ctx = new EnrollmentsContext(loader.manager);
  const PASS_FILTER_RECIPE = NimbusTestUtils.factories.recipe("foo", {
    targeting:
      "experiment.slug == 'foo' && experiment.branches[0].slug == 'control'",
  });

  const FAIL_FILTER_RECIPE = NimbusTestUtils.factories.recipe("foo", {
    targeting: "experiment.slug == 'bar'",
  });

  Assert.equal(
    await ctx.checkTargeting(PASS_FILTER_RECIPE),
    true,
    "Should return true for matching on slug name and branch"
  );
  Assert.equal(
    await ctx.checkTargeting(FAIL_FILTER_RECIPE),
    false,
    "Should fail targeting"
  );
});

add_task(async function test_optIn_debug_disabled() {
  info("Testing users cannot opt-in when nimbus.debug is false");

  const recipe = NimbusTestUtils.factories.recipe("foo", {
    targeting: "false",
  });
  const { loader, cleanup } = await NimbusTestUtils.setupTest({
    init: false,
    experiments: [recipe],
  });

  await ExperimentAPI.init();

  Services.prefs.setBoolPref(DEBUG_PREF, false);
  Services.prefs.setBoolPref(UPLOAD_PREF, true);
  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);

  await Assert.rejects(
    loader._optInToExperiment({
      slug: recipe.slug,
      branchSlug: recipe.branches[0].slug,
    }),
    /Could not opt in/
  );

  Services.prefs.clearUserPref(DEBUG_PREF);
  Services.prefs.clearUserPref(UPLOAD_PREF);
  Services.prefs.clearUserPref(STUDIES_OPT_OUT_PREF);

  await cleanup();
});

add_task(async function test_optIn_studies_disabled() {
  info(
    "Testing users cannot opt-in when telemetry is disabled or studies are disabled."
  );

  const recipe = NimbusTestUtils.factories.recipe("foo", {
    targeting: "false",
  });
  const { loader, cleanup } = await NimbusTestUtils.setupTest({
    init: false,
    experiments: [recipe],
    migrationState: NimbusTestUtils.migrationState.LATEST,
  });

  await ExperimentAPI.init();

  Services.prefs.setBoolPref(DEBUG_PREF, true);

  for (const pref of [UPLOAD_PREF, STUDIES_OPT_OUT_PREF]) {
    Services.prefs.setBoolPref(UPLOAD_PREF, true);
    Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);

    Services.prefs.setBoolPref(pref, false);

    await Assert.rejects(
      loader._optInToExperiment({
        slug: recipe.slug,
        branchSlug: recipe.branches[0].slug,
      }),
      /Could not opt in: studies are disabled/
    );
  }

  Services.prefs.clearUserPref(DEBUG_PREF);
  Services.prefs.clearUserPref(UPLOAD_PREF);
  Services.prefs.clearUserPref(STUDIES_OPT_OUT_PREF);

  await cleanup();
});

add_task(async function test_enrollment_changed_notification() {
  const recipe = NimbusTestUtils.factories.recipe("foo");

  const { sandbox, loader, cleanup } = await NimbusTestUtils.setupTest({
    init: false,
    experiments: [recipe],
  });
  sandbox.spy(loader, "updateRecipes");
  sandbox.stub(loader.manager, "onRecipe").resolves();

  const enrollmentChanged = promiseEnrollmentsUpdated();

  await ExperimentAPI.init();
  await enrollmentChanged;

  Assert.ok(loader.updateRecipes.called, "should call .updateRecipes");

  await cleanup();
});

add_task(async function test_experiment_optin_targeting() {
  Services.prefs.setBoolPref(DEBUG_PREF, true);

  const { sandbox, loader, manager, cleanup } =
    await NimbusTestUtils.setupTest();

  const recipe = NimbusTestUtils.factories.recipe("foo", {
    targeting: "false",
  });

  sandbox.stub(RemoteSettings("nimbus-preview"), "get").resolves([recipe]);

  await Assert.rejects(
    loader._optInToExperiment({
      slug: recipe.slug,
      branch: recipe.branches[0].slug,
      collection: "nimbus-preview",
      applyTargeting: true,
    }),
    /Recipe foo did not match targeting/,
    "optInToExperiment should throw"
  );

  Assert.ok(
    !manager.store.getExperimentForFeature("testFeature"),
    "Should not enroll in experiment"
  );

  await loader._optInToExperiment({
    slug: recipe.slug,
    branch: recipe.branches[0].slug,
    collection: "nimbus-preview",
  });

  Assert.equal(
    manager.store.getExperimentForFeature("testFeature").slug,
    `optin-${recipe.slug}`,
    "Should enroll in experiment"
  );

  manager.unenroll(`optin-${recipe.slug}`);

  Services.prefs.clearUserPref(DEBUG_PREF);

  await cleanup();
});

add_task(async function testUpdateIfAiPrefChanges() {
  const AVAILABLE = "available";
  const BLOCKED = "blocked";

  const AI_TARGETING = `'${AI_FEATURES_ENABLED_PREF}'|preferenceValue == '${AVAILABLE}'`;

  Services.prefs.setStringPref(AI_FEATURES_ENABLED_PREF, AVAILABLE);

  const experiment = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment",
    { featureId: "no-feature-firefox-desktop" },
    {
      targeting: AI_TARGETING,
    }
  );

  const rollout = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout",
    { featureId: "no-feature-firefox-desktop" },
    {
      isRollout: true,
      targeting: AI_TARGETING,
    }
  );

  const { cleanup, manager } = await NimbusTestUtils.setupTest({
    experiments: [experiment, rollout],
    migrationState: NimbusTestUtils.migrationState.LATEST,
  });

  Assert.ok(
    manager.store.get(experiment.slug)?.active,
    "Enrolled in experiment"
  );

  Assert.ok(manager.store.get(rollout.slug)?.active, "Enrolled in rollout");

  info("Disabling AI features");
  {
    const updatedPromise = promiseEnrollmentsUpdated();
    Services.prefs.setStringPref(AI_FEATURES_ENABLED_PREF, BLOCKED);
    await updatedPromise;
    await ExperimentAPI._rsLoader.finishedUpdating();

    const experimentEnrollment = manager.store.get(experiment.slug);
    Assert.ok(!experimentEnrollment.active, "Experiment no longer active");
    Assert.equal(experimentEnrollment.unenrollReason, "targeting-mismatch");

    const rolloutEnrollment = manager.store.get(rollout.slug);
    Assert.ok(!rolloutEnrollment.active, "Rollout no longer active");
    Assert.equal(rolloutEnrollment.unenrollReason, "targeting-mismatch");
  }

  info("Enabling AI features");
  {
    const updatedPromise = promiseEnrollmentsUpdated();
    Services.prefs.setStringPref(AI_FEATURES_ENABLED_PREF, AVAILABLE);
    await updatedPromise;
    await ExperimentAPI._rsLoader.finishedUpdating();

    const experimentEnrollment = manager.store.get(experiment.slug);
    Assert.ok(!experimentEnrollment.active, "Experiment is not active");
    Assert.equal(experimentEnrollment.unenrollReason, "targeting-mismatch");

    const rolloutEnrollment = manager.store.get(rollout.slug);
    Assert.ok(rolloutEnrollment.active, "Rollout is active again");
  }

  await manager.unenroll(rollout.slug, "test");

  await cleanup();

  Services.prefs.clearUserPref(AI_FEATURES_ENABLED_PREF);
});
