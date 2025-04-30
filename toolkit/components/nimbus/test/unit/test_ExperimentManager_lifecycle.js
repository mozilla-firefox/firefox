"use strict";

const { Sampling } = ChromeUtils.importESModule(
  "resource://gre/modules/components-utils/Sampling.sys.mjs"
);

const { MatchStatus } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);

const { NimbusTelemetry } = ChromeUtils.importESModule(
  "resource://nimbus/lib/Telemetry.sys.mjs"
);
const { UnenrollmentCause } = ChromeUtils.importESModule(
  "resource://nimbus/lib/ExperimentManager.sys.mjs"
);

/**
 * onStartup()
 * - should set call setExperimentActive for each active experiment
 */
add_task(async function test_onStartup_setExperimentActive_called() {
  let storePath;

  {
    const store = NimbusTestUtils.stubs.store();
    await store.init();

    store.addEnrollment(NimbusTestUtils.factories.experiment("foo"));
    store.addEnrollment(NimbusTestUtils.factories.rollout("bar"));
    store.addEnrollment(
      NimbusTestUtils.factories.experiment("baz", { active: false })
    );
    store.addEnrollment(
      NimbusTestUtils.factories.rollout("qux", { active: false })
    );

    storePath = await NimbusTestUtils.saveStore(store);
  }

  const { sandbox, manager, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({ storePath, init: false });

  sandbox.stub(NimbusTelemetry, "setExperimentActive");

  await initExperimentAPI();

  Assert.ok(
    NimbusTelemetry.setExperimentActive.calledWith(sinon.match({ slug: "foo" }))
  );
  Assert.ok(
    NimbusTelemetry.setExperimentActive.calledWith(sinon.match({ slug: "bar" }))
  );
  Assert.ok(
    !NimbusTelemetry.setExperimentActive.calledWith(
      sinon.match({ slug: "baz" })
    )
  );
  Assert.ok(
    !NimbusTelemetry.setExperimentActive.calledWith(
      sinon.match({ slug: "qux" })
    )
  );

  manager.unenroll("foo");
  manager.unenroll("bar");

  cleanup();
});

add_task(async function test_startup_unenroll() {
  Services.prefs.setBoolPref("app.shield.optoutstudies.enabled", false);

  let storePath;
  {
    const store = NimbusTestUtils.stubs.store();
    await store.init();

    store.addEnrollment(
      NimbusTestUtils.factories.experiment("startup_unenroll")
    );

    storePath = await NimbusTestUtils.saveStore(store);
  }

  const { sandbox, manager, initExperimentAPI, cleanup } =
    await NimbusTestUtils.setupTest({ storePath, init: false });

  sandbox.spy(manager, "_unenroll");

  await initExperimentAPI();

  Assert.ok(
    manager._unenroll.calledOnceWith(
      sinon.match({ slug: "startup_unenroll" }),
      {
        reason: "studies-opt-out",
      }
    ),
    "Called unenroll for expected recipe"
  );

  Services.prefs.clearUserPref("app.shield.optoutstudies.enabled");

  cleanup();
});

add_task(async function test_onRecipe_enroll() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.stub(manager, "isInBucketAllocation").resolves(true);
  sandbox.stub(Sampling, "bucketSample").resolves(true);
  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "updateEnrollment");

  const recipe = ExperimentFakes.recipe("foo");

  Assert.deepEqual(
    manager.store.getAllActiveExperiments(),
    [],
    "There should be no active experiments"
  );

  await manager.onRecipe(recipe, "test", {
    ok: true,
    status: MatchStatus.TARGETING_AND_BUCKETING,
  });

  Assert.equal(
    manager.enroll.calledWith(recipe),
    true,
    "should call .enroll() the first time a recipe is seen"
  );
  Assert.equal(
    manager.store.has("foo"),
    true,
    "should add recipe to the store"
  );

  manager.unenroll(recipe.slug);

  cleanup();
});

add_task(async function test_onRecipe_update() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "updateEnrollment");

  const recipe = NimbusTestUtils.factories.recipe("foo");

  await manager.onStartup();
  await manager.enroll(recipe, "test");
  await manager.onRecipe(recipe, "test", {
    ok: true,
    status: MatchStatus.TARGETING_AND_BUCKETING,
  });

  Assert.equal(
    manager.updateEnrollment.calledWith(
      sinon.match({ slug: recipe.slug }),
      recipe,
      "test",
      {
        ok: true,
        status: MatchStatus.TARGETING_AND_BUCKETING,
      }
    ),
    true,
    "should call .updateEnrollment() if the recipe has already been enrolled"
  );

  manager.unenroll(recipe.slug);

  cleanup();
});

add_task(async function test_onRecipe_rollout_update() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "_unenroll");
  sandbox.spy(manager, "updateEnrollment");

  const recipe = NimbusTestUtils.factories.recipe("foo", { isRollout: true });

  await manager.enroll(recipe, "test");
  await manager.onRecipe(recipe, "test", {
    ok: true,
    status: MatchStatus.TARGETING_AND_BUCKETING,
  });

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      recipe,
      "test",
      { ok: true, status: MatchStatus.TARGETING_AND_BUCKETING }
    ),
    "should call .updateEnrollment() if the recipe has already been enrolled"
  );
  Assert.ok(
    manager.updateEnrollment.alwaysReturned(Promise.resolve(true)),
    "updateEnrollment will confirm the enrolled branch still exists in the recipe and exit"
  );
  Assert.ok(
    manager._unenroll.notCalled,
    "Should not call if the branches did not change"
  );

  manager.updateEnrollment.resetHistory();

  const updatedRecipe = NimbusTestUtils.factories.recipe(recipe.slug, {
    isRollout: true,
    branches: [
      {
        ...recipe.branches[0],
        slug: "control-v2",
      },
    ],
  });
  await manager.onRecipe(updatedRecipe, "test", {
    ok: true,
    status: MatchStatus.TARGETING_AND_BUCKETING,
  });

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      updatedRecipe,
      "test",
      { ok: true, status: MatchStatus.TARGETING_AND_BUCKETING }
    ),
    "should call .updateEnrollment() if the recipe has already been enrolled"
  );
  Assert.ok(
    manager._unenroll.calledOnceWith(sinon.match({ slug: recipe.slug }), {
      reason: "branch-removed",
    }),
    "updateEnrollment will unenroll because the branch slug changed"
  );

  cleanup();
});

add_task(async function test_onRecipe_isFirefoxLabsOptin_recipe() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();

  sandbox.stub(manager, "enroll");

  const optInRecipe = NimbusTestUtils.factories.recipe("opt-in", {
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "title",
    firefoxLabsDescription: "description",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "group",
    requiresRestart: false,
  });
  const recipe = NimbusTestUtils.factories.recipe("recipe");

  await manager.onRecipe(optInRecipe, "test", {
    ok: true,
    status: MatchStatus.TARGETING_AND_BUCKETING,
  });
  await manager.onRecipe(recipe, "test", {
    ok: true,
    status: MatchStatus.TARGETING_AND_BUCKETING,
  });

  Assert.equal(
    manager.optInRecipes.length,
    1,
    "should only have one opt-in recipe"
  );
  Assert.equal(
    manager.optInRecipes[0],
    optInRecipe,
    "should add the recipe to OptInRecipes list if recipe is firefox labs opt-in"
  );
  Assert.equal(
    manager.enroll.calledOnceWith(recipe, "test"),
    true,
    "should try to enroll the fxLabsOptOutRecipe since it is a targetting match"
  );

  cleanup();
});

add_task(async function test_context_paramters() {
  const { manager, cleanup } = await NimbusTestUtils.setupTest();

  const experiment = NimbusTestUtils.factories.recipe("experiment");
  const rollout = NimbusTestUtils.factories.recipe("rollout", {
    isRollout: true,
  });

  let targetingCtx = manager.createTargetingContext();

  Assert.deepEqual(await targetingCtx.activeExperiments, []);
  Assert.deepEqual(await targetingCtx.activeRollouts, []);
  Assert.deepEqual(await targetingCtx.previousExperiments, []);
  Assert.deepEqual(await targetingCtx.previousRollouts, []);
  Assert.deepEqual(await targetingCtx.enrollments, []);

  await manager.enroll(experiment, "test");
  await manager.enroll(rollout, "test");

  targetingCtx = manager.createTargetingContext();
  Assert.deepEqual(await targetingCtx.activeExperiments, ["experiment"]);
  Assert.deepEqual(await targetingCtx.activeRollouts, ["rollout"]);
  Assert.deepEqual(await targetingCtx.previousExperiments, []);
  Assert.deepEqual(await targetingCtx.previousRollouts, []);
  Assert.deepEqual([...(await targetingCtx.enrollments)].sort(), [
    "experiment",
    "rollout",
  ]);

  manager.unenroll(experiment.slug);
  manager.unenroll(rollout.slug);

  targetingCtx = manager.createTargetingContext();
  Assert.deepEqual(await targetingCtx.activeExperiments, []);
  Assert.deepEqual(await targetingCtx.activeRollouts, []);
  Assert.deepEqual(await targetingCtx.previousExperiments, ["experiment"]);
  Assert.deepEqual(await targetingCtx.previousRollouts, ["rollout"]);
  Assert.deepEqual([...(await targetingCtx.enrollments)].sort(), [
    "experiment",
    "rollout",
  ]);

  cleanup();
});

add_task(async function test_experimentStore_updateEvent() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();
  const stub = sandbox.stub();

  manager.store.on("update", stub);

  await manager.enroll(
    NimbusTestUtils.factories.recipe("experiment"),
    "rs-loader"
  );
  Assert.ok(
    stub.calledOnceWith("update", { slug: "experiment", active: true })
  );
  stub.resetHistory();

  manager.unenroll(
    "experiment",
    UnenrollmentCause.fromReason(
      NimbusTelemetry.UnenrollReason.INDIVIDUAL_OPT_OUT
    )
  );
  Assert.ok(
    stub.calledOnceWith("update", {
      slug: "experiment",
      active: false,
      unenrollReason: "individual-opt-out",
    })
  );

  cleanup();
});
