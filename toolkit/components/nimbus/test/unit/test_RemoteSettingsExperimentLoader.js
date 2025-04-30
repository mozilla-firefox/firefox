"use strict";

const { ExperimentManager } = ChromeUtils.importESModule(
  "resource://nimbus/lib/ExperimentManager.sys.mjs"
);
const { FirstStartup } = ChromeUtils.importESModule(
  "resource://gre/modules/FirstStartup.sys.mjs"
);
const { RemoteSettingsExperimentLoader, EnrollmentsContext, MatchStatus } =
  ChromeUtils.importESModule(
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
  );
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const RUN_INTERVAL_PREF = "app.normandy.run_interval_seconds";
const STUDIES_OPT_OUT_PREF = "app.shield.optoutstudies.enabled";
const UPLOAD_PREF = "datareporting.healthreport.uploadEnabled";
const DEBUG_PREF = "nimbus.debug";

add_task(async function test_real_exp_manager() {
  equal(
    RemoteSettingsExperimentLoader.manager,
    ExperimentManager,
    "should reference ExperimentManager singleton by default"
  );
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

  cleanup();
});

add_task(async function test_init() {
  const { sandbox, loader, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({ init: false });
  sandbox.spy(loader, "setTimer");
  sandbox.spy(loader, "updateRecipes");

  await initExperimentAPI();

  Assert.ok(loader.setTimer.calledOnce, "should call .setTimer");
  Assert.ok(loader.updateRecipes.calledOnce, "should call .updateRecipes");

  cleanup();
});

add_task(async function test_init_with_opt_in() {
  const { sandbox, loader, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({ init: false });
  sandbox.spy(loader, "setTimer");
  sandbox.spy(loader, "updateRecipes");

  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, false);

  await initExperimentAPI();

  equal(
    loader.setTimer.callCount,
    0,
    `should not initialize if ${STUDIES_OPT_OUT_PREF} pref is false`
  );

  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);
  Assert.ok(loader.setTimer.calledOnce, "should call .setTimer");
  Assert.ok(loader.updateRecipes.calledOnce, "should call .updateRecipes");

  cleanup();
});

add_task(async function test_updateRecipes() {
  const passRecipe = ExperimentFakes.recipe("pass", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 0,
    },
    targeting: "true",
  });
  const failRecipe = ExperimentFakes.recipe("fail", {
    targeting: "false",
  });

  const { sandbox, loader, manager, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({
      init: false,
      experiments: [passRecipe, failRecipe],
    });

  sandbox.spy(loader, "updateRecipes");
  sandbox.stub(manager, "onRecipe").resolves();

  await initExperimentAPI();

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

  cleanup();
});

add_task(async function test_enrollmentsContextFirstStartup() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.stub(FirstStartup, "state").get(() => FirstStartup.IN_PROGRESS);

  const ctx = new EnrollmentsContext(manager);

  Assert.ok(
    await ctx.checkTargeting(
      ExperimentFakes.recipe("is-first-startup", {
        targeting: "isFirstStartup",
      })
    ),
    "isFirstStartup targeting works when true"
  );

  sandbox.stub(FirstStartup, "state").get(() => FirstStartup.NOT_STARTED);

  Assert.ok(
    await ctx.checkTargeting(
      ExperimentFakes.recipe("not-first-startup", {
        targeting: "!isFirstStartup",
      })
    ),
    "isFirstStartup targeting works when false"
  );

  cleanup();
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
  const PASS_FILTER_RECIPE = ExperimentFakes.recipe("foo", {
    targeting:
      "experiment.slug == 'foo' && experiment.branches[0].slug == 'control'",
  });

  const FAIL_FILTER_RECIPE = ExperimentFakes.recipe("foo", {
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

  const recipe = ExperimentFakes.recipe("foo");
  const { sandbox, loader, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({
      init: false,
      experiments: [recipe],
    });
  sandbox.stub(loader, "updateRecipes").resolves();

  await initExperimentAPI();

  Services.prefs.setBoolPref(DEBUG_PREF, false);
  Services.prefs.setBoolPref(UPLOAD_PREF, true);
  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);

  await Assert.rejects(
    loader.optInToExperiment({
      slug: recipe.slug,
      branchSlug: recipe.branches[0].slug,
    }),
    /Could not opt in/
  );

  Services.prefs.clearUserPref(DEBUG_PREF);
  Services.prefs.clearUserPref(UPLOAD_PREF);
  Services.prefs.clearUserPref(STUDIES_OPT_OUT_PREF);

  cleanup();
});

add_task(async function test_optIn_studies_disabled() {
  info(
    "Testing users cannot opt-in when telemetry is disabled or studies are disabled."
  );

  const recipe = NimbusTestUtils.factories.recipe("foo");
  const { sandbox, loader, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({ init: false, experiments: [recipe] });

  sandbox.stub(loader, "updateRecipes").resolves();

  await initExperimentAPI();

  Services.prefs.setBoolPref(DEBUG_PREF, true);

  for (const pref of [UPLOAD_PREF, STUDIES_OPT_OUT_PREF]) {
    Services.prefs.setBoolPref(UPLOAD_PREF, true);
    Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);

    Services.prefs.setBoolPref(pref, false);

    await Assert.rejects(
      loader.optInToExperiment({
        slug: recipe.slug,
        branchSlug: recipe.branches[0].slug,
      }),
      /Could not opt in: studies are disabled/
    );
  }

  Services.prefs.clearUserPref(DEBUG_PREF);
  Services.prefs.clearUserPref(UPLOAD_PREF);
  Services.prefs.clearUserPref(STUDIES_OPT_OUT_PREF);

  cleanup();
});

add_task(async function test_enrollment_changed_notification() {
  const recipe = NimbusTestUtils.factories.recipe("foo");

  const { sandbox, loader, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({ init: false, experiments: [recipe] });
  sandbox.spy(loader, "updateRecipes");
  sandbox.stub(loader.manager, "onRecipe").resolves();

  const enrollmentChanged = TestUtils.topicObserved(
    "nimbus:enrollments-updated"
  );

  await initExperimentAPI();
  await enrollmentChanged;

  Assert.ok(loader.updateRecipes.called, "should call .updateRecipes");

  cleanup();
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
    loader.optInToExperiment({
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

  await loader.optInToExperiment({
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

  cleanup();
});
