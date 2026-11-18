"use strict";

const { BrowserUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/BrowserUtils.sys.mjs"
);
const { EnrollmentsContext, MatchStatus } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);
const { NimbusTelemetry } = ChromeUtils.importESModule(
  "resource://nimbus/lib/Telemetry.sys.mjs"
);
const { PanelTestProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/PanelTestProvider.sys.mjs"
);
const { TelemetryEnvironment } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryEnvironment.sys.mjs"
);
const { UnenrollmentCause } = ChromeUtils.importESModule(
  "resource://nimbus/lib/ExperimentManager.sys.mjs"
);

function assertEnrollments(store, expectedActive, expectedInactive) {
  for (const slug of expectedActive) {
    Assert.ok(store.get(slug), `${slug} is present in the store`);
    Assert.ok(store.get(slug).active, `${slug} is active`);
  }

  for (const slug of expectedInactive) {
    Assert.ok(store.get(slug), `${slug} is present in the store`);
    Assert.ok(!store.get(slug).active, `${slug} is not active`);
  }

  for (const enrollment of store.getAll()) {
    const slug = enrollment.slug;

    if (!expectedActive.includes(slug) && !expectedInactive.includes(slug)) {
      Assert.ok(
        false,
        `Store has unexpected ${enrollment.active ? "active" : "inactive"} enrollment with slug ${slug}`
      );
    }
  }
}

async function assertSyncTimestamps(expectedTimestamps, message) {
  await NimbusTestUtils.flushStore();

  const timestamps = await NimbusEnrollments.loadSyncTimestamps();

  Assert.deepEqual(
    Object.fromEntries(timestamps.entries()),
    expectedTimestamps,
    message
  );
}

async function updateAndAssertEnrollmentsUpdatedNotified(
  loader,
  shouldNotify,
  message
) {
  const didObserve = await Promise.race([
    BrowserUtils.promiseObserved("nimbus:enrollments-updated").then(() => true),
    loader.updateRecipes("test").then(() => false),
  ]);

  Assert.equal(
    didObserve,
    shouldNotify,
    `${shouldNotify ? "should" : "should not"} send nimbus:enrollments-updated: ${message}`
  );
}

add_setup(async function setup() {
  Services.fog.initializeFOG();
});

function setupTest({ ...args } = {}) {
  return NimbusTestUtils.setupTest({ ...args, clearTelemetry: true });
}

add_task(async function test_updateRecipes_invalidFeatureId() {
  const badRecipe = NimbusTestUtils.factories.recipe("foo", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "invalid-feature-id",
            value: { hello: "world" },
          },
        ],
      },
      {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "invalid-feature-id",
            value: { hello: "goodbye" },
          },
        ],
      },
    ],
  });

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "enroll");
  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);

  await loader.updateRecipes("test");

  Assert.ok(
    manager.onRecipe.calledOnceWith(badRecipe, "rs-loader", {
      ok: false,
      reason: "invalid-feature",
      featureIds: ["invalid-feature-id"],
    }),
    "should call onRecipe with invalid-feature"
  );
  Assert.ok(manager.enroll.notCalled, "Would not enroll");

  Assert.deepEqual(
    Glean.nimbusEvents.validationFailed
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [],
    "Did not submit telemetry"
  );

  await cleanup();
});

add_task(async function test_updateRecipes_invalidFeatureValue() {
  const badRecipe = NimbusTestUtils.factories.recipe("foo", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "spotlight",
            value: {
              template: "spotlight",
            },
          },
        ],
      },
      {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "spotlight",
            value: {
              template: "spotlight",
            },
          },
        ],
      },
    ],
  });

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "enroll");
  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);

  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(badRecipe, "rs-loader", {
      ok: false,
      reason: "invalid-branch",
      branchSlugs: ["control", "treatment"],
    }),
    "Should call onRecipe with invalid-branch"
  );
  Assert.ok(manager.enroll.notCalled, "Would not enroll");

  await cleanup();
});

add_task(async function test_updateRecipes_invalidRecipe() {
  const badRecipe = NimbusTestUtils.factories.recipe("foo");
  delete badRecipe.slug;

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "enroll");
  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);

  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(badRecipe, "rs-loader", {
      ok: false,
      reason: "invalid-recipe",
    }),
    "Should call onRecipe with invalid-recipe"
  );
  Assert.ok(manager.enroll.notCalled, "Would not enroll");

  await cleanup();
});

add_task(async function test_updateRecipes_invalidRecipeAfterUpdate() {
  const recipe = NimbusTestUtils.factories.recipe("foo");

  const badRecipe = { ...recipe };
  delete badRecipe.branches;

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "_unenroll");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_AND_BUCKETING,
    }),
    "Should call onRecipe with targeting and bucketing match"
  );
  Assert.ok(
    manager.enroll.calledOnceWith(recipe, "rs-loader"),
    "Should enroll"
  );

  info("Replacing recipe with an invalid one");

  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      badRecipe,
      "rs-loader",
      {
        ok: false,
        reason: "invalid-recipe",
      }
    ),
    "Should call onRecipe with invalid-recipe"
  );
  Assert.ok(
    manager._unenroll.calledOnceWith(sinon.match({ slug: recipe.slug }), {
      reason: "invalid-recipe",
    }),
    "Should unenroll"
  );

  await cleanup();
});

add_task(async function test_updateRecipes_invalidBranchAfterUpdate() {
  const message = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(m => m.id === "MULTISTAGE_SPOTLIGHT_MESSAGE")
  );

  const recipe = NimbusTestUtils.factories.recipe("recipe", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "spotlight",
            value: { ...message },
          },
        ],
      },
      {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "spotlight",
            value: { ...message },
          },
        ],
      },
    ],
  });

  const badRecipe = {
    ...recipe,
    branches: [
      { ...recipe.branches[0] },
      {
        ...recipe.branches[1],
        features: [
          {
            ...recipe.branches[1].features[0],
            value: { ...message },
          },
        ],
      },
    ],
  };
  delete badRecipe.branches[1].features[0].value.template;

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "_unenroll");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_AND_BUCKETING,
    }),
    "should call onRecipe with targeting and bucketing match"
  );
  Assert.ok(
    manager.enroll.calledOnceWith(recipe, "rs-loader"),
    "should enroll"
  );

  info("Replacing recipe with an invalid one");

  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      badRecipe,
      "rs-loader",
      {
        ok: false,
        reason: "invalid-branch",
        branchSlugs: ["treatment"],
      }
    ),
    "Should call updateEnrollment with invalid-branch"
  );
  Assert.ok(
    manager._unenroll.calledOnceWith(sinon.match({ slug: "recipe" }), {
      reason: "invalid-branch",
    }),
    "should unenroll"
  );

  await cleanup();
});

add_task(async function test_updateRecipes_simpleFeatureInvalidAfterUpdate() {
  const recipe = NimbusTestUtils.factories.recipe("recipe");
  const badRecipe = {
    ...recipe,
    branches: [
      {
        ...recipe.branches[0],
        features: [
          {
            featureId: "testFeature",
            value: { testInt: "abc123", enabled: true },
          },
        ],
      },
      {
        ...recipe.branches[1],
        features: [
          {
            featureId: "testFeature",
            value: { testInt: 456, enabled: true },
          },
        ],
      },
    ],
  };

  const EXPECTED_SCHEMA = {
    $schema: "https://json-schema.org/draft/2019-09/schema",
    title: "testFeature",
    description: NimbusFeatures.testFeature.manifest.description,
    type: "object",
    properties: {
      testInt: {
        type: "integer",
      },
      enabled: {
        type: "boolean",
      },
      testSetString: {
        type: "string",
      },
    },
    additionalProperties: true,
  };

  const { sandbox, loader, manager, cleanup } = await setupTest({
    init: false,
    experiments: [recipe],
  });

  sandbox.spy(loader, "updateRecipes");
  sandbox.spy(EnrollmentsContext.prototype, "_generateVariablesOnlySchema");
  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "_unenroll");

  await ExperimentAPI.init();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_AND_BUCKETING,
    }),
    "should call .onRecipe with targeting and bucketing match"
  );
  Assert.ok(
    manager.enroll.calledOnceWith(recipe, "rs-loader"),
    "Should enroll"
  );

  Assert.ok(
    EnrollmentsContext.prototype._generateVariablesOnlySchema.calledOnce,
    "Should have generated a schema for testFeature"
  );

  Assert.deepEqual(
    EnrollmentsContext.prototype._generateVariablesOnlySchema.returnValues[0],
    EXPECTED_SCHEMA,
    "should have generated a schema with three fields"
  );

  info("Replacing recipe with an invalid one");

  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      badRecipe,
      "rs-loader",
      {
        ok: false,
        reason: "invalid-branch",
        branchSlugs: ["control"],
      }
    ),
    "Should call updateEnrollment with invalid-branch"
  );
  Assert.ok(
    manager._unenroll.calledOnceWith(sinon.match({ slug: recipe.slug }), {
      reason: "invalid-branch",
    }),
    "Should unenroll"
  );

  await cleanup();
});

add_task(async function test_updateRecipes_invalidFeatureAfterUpdate() {
  const featureConfig = { featureId: "bogus", value: {} };

  const { manager, cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(
        NimbusTestUtils.factories.recipe.withFeatureConfig(
          "recipe",
          featureConfig
        ),
        { store }
      );
    },
    experiments: [
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "recipe",
        featureConfig
      ),
    ],
    migrationState: NimbusTestUtils.migrationState.LATEST,
  });

  const enrollment = manager.store.get("recipe");
  Assert.ok(!enrollment.active, "Should have unenrolled");
  Assert.equal(
    enrollment.unenrollReason,
    "invalid-feature",
    "Should have unenrolled"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.validationFailed
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [],
    "Should not have submitted any validationFailed telemetry"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        experiment: "recipe",
        branch: enrollment.branch.slug,
        reason: "invalid-feature",
      },
    ]
  );

  await cleanup();
});

add_task(async function test_updateRecipes_validationTelemetry() {
  const invalidRecipe = NimbusTestUtils.factories.recipe("invalid-recipe");
  delete invalidRecipe.channel;

  const invalidBranch = NimbusTestUtils.factories.recipe("invalid-branch");
  invalidBranch.branches[0].features[0].value.testInt = "hello";
  invalidBranch.branches[1].features[0].value.testInt = "world";

  const invalidFeature = NimbusTestUtils.factories.recipe("invalid-feature", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "unknown-feature",
            value: { foo: "bar" },
          },
          {
            featureId: "second-unknown-feature",
            value: { baz: "qux" },
          },
        ],
      },
    ],
  });

  const TEST_CASES = [
    {
      recipe: invalidRecipe,
      reason: "invalid-recipe",
      events: [{}],
      callCount: 1,
    },
    {
      recipe: invalidBranch,
      reason: "invalid-branch",
      events: invalidBranch.branches.map(branch => ({ branch: branch.slug })),
      callCount: 2,
    },
    {
      recipe: invalidFeature,
      reason: "invalid-feature",
      events: [],
      callCount: 0,
    },
  ];

  const LEGACY_FILTER = {
    category: "normandy",
    method: "validationFailed",
    object: "nimbus_experiment",
  };

  for (const { recipe, reason, events, callCount } of TEST_CASES) {
    info(`Testing validation failed telemetry for reason = "${reason}" ...`);

    const { sandbox, cleanup } = await setupTest({
      init: false,
      experiments: [recipe],
    });

    sandbox.spy(NimbusTelemetry, "recordValidationFailure");

    await ExperimentAPI.init();

    Assert.equal(
      NimbusTelemetry.recordValidationFailure.callCount,
      callCount,
      `Should call recordValidationFailure ${callCount} times for reason ${reason}`
    );

    const gleanEvents =
      Glean.nimbusEvents.validationFailed
        .testGetValue("events")
        ?.map(event => event.extra) ?? [];

    const expectedGleanEvents = events.map(event => ({
      experiment: recipe.slug,
      reason,
      ...event,
    }));

    Assert.deepEqual(
      gleanEvents,
      expectedGleanEvents,
      "Glean telemetry matches"
    );

    const expectedLegacyEvents = events.map(event => ({
      ...LEGACY_FILTER,
      value: recipe.slug,
      extra: {
        reason,
        ...event,
      },
      LEGACY_FILTER,
    }));

    TelemetryTestUtils.assertEvents(expectedLegacyEvents, LEGACY_FILTER);

    await cleanup();
  }
});

add_task(async function test_updateRecipes_validationDisabled() {
  Services.prefs.setBoolPref("nimbus.validation.enabled", false);

  const invalidRecipe = NimbusTestUtils.factories.recipe("invalid-recipe");
  delete invalidRecipe.channel;

  const invalidBranch = NimbusTestUtils.factories.recipe("invalid-branch");
  invalidBranch.branches[0].features[0].value.testInt = "hello";
  invalidBranch.branches[1].features[0].value.testInt = "world";

  const invalidFeature = NimbusTestUtils.factories.recipe("invalid-feature", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "unknown-feature",
            value: { foo: "bar" },
          },
          {
            featureId: "second-unknown-feature",
            value: { baz: "qux" },
          },
        ],
      },
    ],
  });

  for (const recipe of [invalidRecipe, invalidBranch, invalidFeature]) {
    const { sandbox, manager, cleanup } = await setupTest({
      init: false,
      experiments: [recipe],
    });

    sandbox.stub(manager, "enroll");
    sandbox.spy(manager, "onRecipe");
    sandbox.spy(NimbusTelemetry, "recordValidationFailure");

    await ExperimentAPI.init();

    Assert.ok(
      NimbusTelemetry.recordValidationFailure.notCalled,
      "Should not send validation failed telemetry"
    );
    Assert.ok(
      manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
        ok: true,
        status: MatchStatus.TARGETING_AND_BUCKETING,
      }),
      "Should call onRecipe with no validation issues"
    );

    Assert.ok(
      manager.enroll.calledOnceWith(recipe, "rs-loader"),
      "Would enroll"
    );

    await cleanup();
  }

  Services.prefs.clearUserPref("nimbus.validation.enabled");
});

add_task(async function test_updateRecipes_appId() {
  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "background-task-recipe",
    { featureId: "backgroundTaskMessage" }
  );

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.stub(manager, "enroll");

  info("Testing updateRecipes() with the default application ID");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);

  await GleanPings.nimbusTargetingContext.testSubmission(
    () => {
      Assert.deepEqual(
        Glean.nimbusEvents.enrollmentStatus
          .testGetValue("nimbus-targeting-context")
          ?.map(ev => ev.extra) ?? [],
        [],
        "No enrollment status events submitted"
      );
    },
    () => loader.updateRecipes()
  );

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: false,
      reason: "unsupported-feature",
      featureIds: ["backgroundTaskMessage"],
    }),
    "Should call onRecipe with unsupported-feature"
  );
  Assert.ok(manager.enroll.notCalled, "Would not enroll");

  Assert.deepEqual(
    Glean.nimbusEvents.validationFailed.testGetValue("events") ?? [],
    [],
    "There should be no validation failed event"
  );

  info("Testing updateRecipes() with a custom application ID");
  manager.onRecipe.resetHistory();

  Services.prefs.setStringPref(
    "nimbus.appId",
    "firefox-desktop-background-task"
  );

  await loader.updateRecipes();
  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_AND_BUCKETING,
    }),
    `onRecipe called`
  );
  Assert.ok(manager.enroll.calledOnceWith(recipe, "rs-loader"), "Would enroll");

  Services.prefs.clearUserPref("nimbus.appId");

  await cleanup();
});

add_task(async function test_updateRecipes_withPropNotInManifest() {
  const recipe = NimbusTestUtils.factories.recipe("foo", {
    branches: [
      {
        features: [
          {
            enabled: true,
            featureId: "testFeature",
            value: {
              enabled: true,
              testInt: 5,
              testSetString: "foo",
              additionalPropNotInManifest: 7,
            },
          },
        ],
        ratio: 1,
        slug: "treatment-2",
      },
    ],
    channel: "nightly",
    schemaVersion: "1.9.0",
    targeting: "true",
  });

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.stub(manager, "onRecipe");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_AND_BUCKETING,
    }),
    "should call onRecipe with this recipe"
  );

  await cleanup();
});

add_task(async function test_updateRecipes_recipeAppId() {
  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "mobile-experiment",
    { featureId: "mobile-feature", value: { enabled: true } },
    { appId: "org.mozilla.firefox" }
  );

  const { sandbox, loader, manager, cleanup } = await setupTest();
  sandbox.stub(manager, "onRecipe");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(manager.onRecipe.notCalled, ".onRecipe was never called");

  await cleanup();
});

add_task(async function test_updateRecipes_featureValidationOptOut() {
  const invalidFeatureRecipe =
    NimbusTestUtils.factories.recipe.withFeatureConfig("invalid-recipe", {
      featureId: "testFeature",
      value: { enabled: "true", testInt: false },
    });

  const message = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(m => m.id === "MULTISTAGE_SPOTLIGHT_MESSAGE")
  );
  delete message.template;

  const invalidMsgRecipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "invalid-recipe",
    { featureId: "spotlight", value: message }
  );

  for (const invalidRecipe of [invalidFeatureRecipe, invalidMsgRecipe]) {
    const optOutRecipe = {
      ...invalidMsgRecipe,
      slug: "optout-recipe",
      featureValidationOptOut: true,
    };

    const { sandbox, loader, manager, cleanup } = await setupTest();
    sandbox.stub(manager, "onRecipe");

    loader.remoteSettingsClients.experiments.get.resolves([
      invalidRecipe,
      optOutRecipe,
    ]);
    await loader.updateRecipes();

    Assert.equal(manager.onRecipe.callCount, 2);
    Assert.ok(
      manager.onRecipe.calledWith(
        invalidRecipe,
        "rs-loader",
        sinon.match({ ok: false })
      ),
      "should call onRecipe for invalidRecipe with an error"
    );

    Assert.ok(
      manager.onRecipe.calledWith(optOutRecipe, "rs-loader", {
        ok: true,
        status: MatchStatus.TARGETING_AND_BUCKETING,
      }),
      "should call onRecipe for optOutRecipe with targeting and bucketing match"
    );

    await cleanup();
  }
});

add_task(async function test_updateRecipes_invalidFeature_mismatch() {
  info(
    "Testing that we do not submit validation telemetry when the targeting does not match"
  );
  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "recipe",
    { featureId: "bogus", value: { bogus: "bogus" } },
    { targeting: "false" }
  );

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.stub(manager, "onRecipe");
  sandbox.stub(NimbusTelemetry, "recordValidationFailure");
  sandbox.spy(EnrollmentsContext.prototype, "checkTargeting");
  sandbox.spy(EnrollmentsContext.prototype, "checkRecipe");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(
    EnrollmentsContext.prototype.checkTargeting.calledOnce,
    "Should have checked targeting for recipe"
  );
  Assert.ok(
    !(await EnrollmentsContext.prototype.checkTargeting.returnValues[0]),
    "Targeting should not have matched"
  );
  Assert.deepEqual(
    await EnrollmentsContext.prototype.checkRecipe.returnValues[0],
    { ok: true, status: MatchStatus.NO_MATCH },
    "Recipe should be considered a targeting mismatch"
  );
  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.NO_MATCH,
    }),
    "should call onRecipe for the recipe"
  );
  Assert.ok(
    NimbusTelemetry.recordValidationFailure.notCalled,
    "Should not have submitted validation failed telemetry"
  );

  await cleanup();
});

add_task(async function test_updateRecipes_rollout_bucketing() {
  const experiment = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment",
    { featureId: "testFeature" }
  );
  const rollout = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout",
    { branchSlug: "rollout", featureId: "testFeature" },
    { isRollout: true }
  );

  const { loader, manager, cleanup } = await setupTest();

  loader.remoteSettingsClients.experiments.get.resolves([experiment, rollout]);
  await loader.updateRecipes();

  Assert.equal(
    manager.store.getExperimentForFeature("testFeature")?.slug,
    experiment.slug,
    "Should enroll in experiment"
  );
  Assert.equal(
    manager.store.getRolloutForFeature("testFeature")?.slug,
    rollout.slug,
    "Should enroll in rollout"
  );

  experiment.bucketConfig.count = 0;
  rollout.bucketConfig.count = 0;
  await loader.updateRecipes();

  Assert.equal(
    manager.store.getExperimentForFeature("testFeature")?.slug,
    experiment.slug,
    "Should stay enrolled in experiment -- experiments cannot be resized"
  );
  Assert.ok(
    !manager.store.getRolloutForFeature("testFeature"),
    "Should unenroll from rollout"
  );

  const unenrollmentEvents =
    Glean.nimbusEvents.unenrollment.testGetValue("events");
  Assert.equal(
    unenrollmentEvents.length,
    1,
    "Should be one unenrollment event"
  );
  Assert.equal(
    unenrollmentEvents[0].extra.experiment,
    rollout.slug,
    "Experiment slug should match"
  );
  Assert.equal(
    unenrollmentEvents[0].extra.reason,
    "bucketing",
    "Reason should match"
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: rollout.slug,
        extra: {
          reason: "bucketing",
        },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    }
  );

  manager.unenroll(experiment.slug);

  await cleanup();
});

add_task(async function test_reenroll_rollout_resized() {
  const rollout = NimbusTestUtils.factories.recipe("rollout", {
    isRollout: true,
  });

  const { loader, manager, cleanup } = await setupTest();

  loader.remoteSettingsClients.experiments.get.resolves([rollout]);
  await loader.updateRecipes();

  Assert.equal(
    manager.store.getRolloutForFeature("testFeature")?.slug,
    rollout.slug,
    "Should enroll in rollout"
  );

  rollout.bucketConfig.count = 0;
  await loader.updateRecipes();

  Assert.ok(
    !manager.store.getRolloutForFeature("testFeature"),
    "Should unenroll from rollout"
  );

  const enrollment = manager.store.get(rollout.slug);
  Assert.equal(enrollment.unenrollReason, "bucketing");

  rollout.bucketConfig.count = 1000;
  await loader.updateRecipes();

  Assert.equal(
    manager.store.getRolloutForFeature("testFeature")?.slug,
    rollout.slug,
    "Should re-enroll in rollout"
  );

  const newEnrollment = manager.store.get(rollout.slug);
  Assert.ok(
    !Object.is(enrollment, newEnrollment),
    "Should have new enrollment object"
  );
  Assert.ok(
    !("unenrollReason" in newEnrollment),
    "New enrollment should not have unenroll reason"
  );

  manager.unenroll(rollout.slug);

  await cleanup();
});

add_task(async function test_rollout_reenroll_recipe_not_seen() {
  const rollout = NimbusTestUtils.factories.recipe("rollout", {
    isRollout: true,
  });

  const { loader, manager, cleanup } = await setupTest();

  loader.remoteSettingsClients.experiments.get.resolves([rollout]);
  await loader.updateRecipes();

  Assert.equal(
    manager.store.getRolloutForFeature("testFeature")?.slug,
    rollout.slug,
    "Should enroll in rollout"
  );

  loader.remoteSettingsClients.experiments.get.resolves([]);
  await loader.updateRecipes();

  Assert.ok(
    !manager.store.getRolloutForFeature("testFeature"),
    "Should unenroll from rollout when recipe is no longer seen"
  );

  const enrollment = manager.store.get(rollout.slug);
  Assert.equal(
    enrollment.unenrollReason,
    NimbusTelemetry.UnenrollReason.RECIPE_NOT_SEEN
  );

  loader.remoteSettingsClients.experiments.get.resolves([rollout]);
  await loader.updateRecipes();

  Assert.equal(
    manager.store.getRolloutForFeature("testFeature")?.slug,
    rollout.slug,
    "Should re-enroll in rollout when recipe is seen again"
  );

  const newEnrollment = manager.store.get(rollout.slug);
  Assert.strictEqual(
    typeof newEnrollment.unenrollReason,
    "undefined",
    "New enrollment should not have unenroll reason"
  );

  manager.unenroll(rollout.slug);

  await cleanup();
});

add_task(async function testRolloutReenrollPrefFlips() {
  const rollout = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout",
    {
      featureId: "prefFlips",
      value: {
        prefs: {
          "test-pref.please.ignore": {
            branch: "user",
            value: "value",
          },
        },
      },
    },
    { isRollout: true }
  );

  const expectedPrefFlips = {
    originalValues: { "test-pref.please.ignore": null },
  };

  const { loader, manager, cleanup } = await setupTest({
    secureExperiments: [rollout],
  });
  await NimbusTestUtils.flushStore();

  {
    const inMemoryEnrollment = manager.store.get("rollout");
    Assert.ok(inMemoryEnrollment?.active, "enrolled in rollout");
    Assert.deepEqual(
      inMemoryEnrollment.prefs,
      [],
      "in-memory enrollment has no setPrefs"
    );
    Assert.deepEqual(
      inMemoryEnrollment.prefFlips,
      expectedPrefFlips,
      "in-memory enrollment prefFlips are correct"
    );

    const dbEnrollment = await NimbusTestUtils.queryEnrollment("rollout");
    Assert.deepEqual(
      inMemoryEnrollment.prefs,
      dbEnrollment.setPrefs,
      "in-memory enrollment setPrefs match db"
    );
    Assert.deepEqual(
      inMemoryEnrollment.prefFlips,
      dbEnrollment.prefFlips,
      "in-memory enrollment prefFlips match db"
    );
  }

  info("trigger unenrollment...");
  loader.remoteSettingsClients.secureExperiments.get.resolves([]);
  await loader.updateRecipes("timer");
  await NimbusTestUtils.flushStore();

  {
    const inMemoryEnrollment = manager.store.get("rollout");
    Assert.ok(!inMemoryEnrollment.active, "unenrolled from rollout");
    Assert.deepEqual(
      inMemoryEnrollment.prefs,
      null,
      "in-memory enrollment has null setPrefs"
    );
    Assert.deepEqual(
      inMemoryEnrollment.prefFlips,
      null,
      "in-memory enrollment prefFlips are null"
    );

    const dbEnrollment = await NimbusTestUtils.queryEnrollment("rollout");
    Assert.deepEqual(
      inMemoryEnrollment.prefs,
      dbEnrollment.setPrefs,
      "in-memory enrollment setPrefs match db"
    );
    Assert.deepEqual(
      inMemoryEnrollment.prefFlips,
      dbEnrollment.prefFlips,
      "in-memory enrollment prefFlips match db"
    );
  }

  info("triggering re-enroll...");
  loader.remoteSettingsClients.secureExperiments.get.resolves([rollout]);
  await loader.updateRecipes("timer");
  await NimbusTestUtils.flushStore();

  {
    const inMemoryEnrollment = manager.store.get("rollout");
    Assert.ok(!!inMemoryEnrollment, "in-memory enrollment exists");
    Assert.ok(inMemoryEnrollment.active, "in-memory enrollment active");
    Assert.notEqual(
      inMemoryEnrollment.prefFlips,
      null,
      "in-memory prefFlips are not null"
    );
    Assert.equal(
      inMemoryEnrollment.setPrefs,
      null,
      "in-memory setPrefs are null"
    );

    const dbEnrollment = await NimbusTestUtils.queryEnrollment("rollout");
    Assert.deepEqual(
      inMemoryEnrollment.prefs,
      dbEnrollment.setPrefs,
      "in-memory enrollment setPrefs match db"
    );
    Assert.deepEqual(
      inMemoryEnrollment.prefFlips,
      dbEnrollment.prefFlips,
      "in-memory enrollment prefFlips match db"
    );
  }

  await NimbusTestUtils.cleanupManager(["rollout"]);
  await cleanup();
});

add_task(async function test_experiment_reenroll() {
  const experiment = NimbusTestUtils.factories.recipe("experiment");

  const { loader, manager, cleanup } = await setupTest();

  await manager.enroll(experiment, "test");
  Assert.equal(
    manager.store.getExperimentForFeature("testFeature")?.slug,
    experiment.slug,
    "Should enroll in experiment"
  );

  manager.unenroll(experiment.slug);
  Assert.ok(
    !manager.store.getExperimentForFeature("testFeature"),
    "Should unenroll from experiment"
  );

  loader.remoteSettingsClients.experiments.get.resolves([experiment]);
  await loader.updateRecipes();

  Assert.ok(
    !manager.store.getExperimentForFeature("testFeature"),
    "Should not re-enroll in experiment"
  );

  await cleanup();
});

add_task(async function test_rollout_reenroll_optout() {
  const rollout = NimbusTestUtils.factories.recipe("rollout", {
    isRollout: true,
  });

  const { loader, manager, cleanup } = await setupTest({
    experiments: [rollout],
  });

  Assert.ok(
    manager.store.getRolloutForFeature("testFeature"),
    "Should enroll in rollout"
  );

  manager.unenroll(
    rollout.slug,
    UnenrollmentCause.fromReason(
      NimbusTelemetry.UnenrollReason.INDIVIDUAL_OPT_OUT
    )
  );

  await loader.updateRecipes();

  Assert.ok(
    !manager.store.getRolloutForFeature("testFeature"),
    "Should not re-enroll in rollout"
  );

  await cleanup();
});

add_task(async function test_rollout_reenroll_prefchanged() {
  const rollout = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout",
    { featureId: "nimbus-qa-2", value: { value: "rollout-value" } },
    { isRollout: true }
  );

  const { loader, manager, cleanup } = await setupTest({
    experiments: [rollout],
  });

  Assert.ok(manager.store.get("rollout")?.active, "rollout is active");

  Services.prefs.setStringPref("nimbus.qa.pref-2", "override");

  {
    const enrollment = manager.store.get("rollout");
    Assert.ok(!enrollment.active, "Rollout no longer active");
    Assert.equal(
      enrollment.unenrollReason,
      NimbusTelemetry.UnenrollReason.CHANGED_PREF
    );
  }

  await loader.updateRecipes();

  Assert.ok(!manager.store.get("rollout").active, "Did not re-enroll");

  await cleanup();
});

add_task(async function test_active_and_past_experiment_targeting() {
  const cleanupFeatures = NimbusTestUtils.addTestFeatures(
    new ExperimentFeature("feature-a", {
      isEarlyStartup: false,
      variables: {},
    }),
    new ExperimentFeature("feature-b", {
      isEarlyStartup: false,
      variables: {},
    }),
    new ExperimentFeature("feature-c", { isEarlyStartup: false, variables: {} })
  );

  const experimentA = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-a",
    { featureId: "feature-a" }
  );
  const experimentB = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-b",
    { featureId: "feature-b" },
    { targeting: "'experiment-a' in activeExperiments" }
  );
  const experimentC = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-c",
    { featureId: "feature-c" },
    { targeting: "'experiment-a' in previousExperiments" }
  );

  const rolloutA = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-a",
    { featureId: "feature-a" },
    { isRollout: true }
  );
  const rolloutB = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-b",
    { featureId: "feature-b" },
    { targeting: "'rollout-a' in activeRollouts", isRollout: true }
  );
  const rolloutC = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-c",
    { featureId: "feature-c" },
    { targeting: "'rollout-a' in previousRollouts", isRollout: true }
  );

  const { loader, manager, cleanup } = await setupTest();

  // Enroll in A.
  loader.remoteSettingsClients.experiments.get.resolves([
    experimentA,
    rolloutA,
  ]);
  await loader.updateRecipes();

  assertEnrollments(manager.store, ["experiment-a", "rollout-a"], []);

  loader.remoteSettingsClients.experiments.get.resolves([
    experimentA,
    experimentB,
    experimentC,
    rolloutA,
    rolloutB,
    rolloutC,
  ]);
  await loader.updateRecipes();

  // B will enroll becuase A is enrolled. C will not enroll because A is still
  // enrolled.
  assertEnrollments(
    manager.store,
    ["experiment-a", "experiment-b", "rollout-a", "rollout-b"],
    []
  );

  loader.remoteSettingsClients.experiments.get.resolves([
    experimentB,
    experimentC,
    rolloutB,
    rolloutC,
  ]);
  await loader.updateRecipes();

  // Remove experiment A and rollout A to cause them to unenroll. B should
  // unenroll as a result and C should enroll.
  assertEnrollments(
    manager.store,
    ["experiment-c", "rollout-c"],
    ["experiment-a", "experiment-b", "rollout-a", "rollout-b"]
  );

  manager.unenroll("experiment-c");
  manager.unenroll("rollout-c");

  cleanupFeatures();
  await cleanup();
});

add_task(async function test_enrollment_targeting() {
  const cleanupFeatures = NimbusTestUtils.addTestFeatures(
    new ExperimentFeature("feature-a", {
      isEarlyStartup: false,
      variables: {},
    }),
    new ExperimentFeature("feature-b", {
      isEarlyStartup: false,
      variables: {},
    }),
    new ExperimentFeature("feature-c", {
      isEarlyStartup: false,
      variables: {},
    }),
    new ExperimentFeature("feature-d", {
      isEarlyStartup: false,
      variables: {},
    })
  );

  function recipe(
    name,
    featureId,
    { targeting = "true", isRollout = false } = {}
  ) {
    return NimbusTestUtils.factories.recipe.withFeatureConfig(
      name,
      { featureId },
      { targeting, isRollout }
    );
  }

  const experimentA = recipe("experiment-a", "feature-a", {
    targeting: "!('rollout-c' in enrollments)",
  });
  const experimentB = recipe("experiment-b", "feature-b", {
    targeting: "'rollout-a' in enrollments",
  });
  const experimentC = recipe("experiment-c", "feature-c");

  const rolloutA = recipe("rollout-a", "feature-a", {
    targeting: "!('experiment-c' in enrollments)",
    isRollout: true,
  });
  const rolloutB = recipe("rollout-b", "feature-b", {
    targeting: "'experiment-a' in enrollments",
    isRollout: true,
  });
  const rolloutC = recipe("rollout-c", "feature-c", { isRollout: true });

  const { loader, manager, cleanup } = await setupTest();

  async function check(current, past, unenrolled) {
    await loader.updateRecipes();

    for (const slug of current) {
      const enrollment = manager.store.get(slug);
      Assert.equal(
        enrollment?.active,
        true,
        `Enrollment exists for ${slug} and is active`
      );
    }

    for (const slug of past) {
      const enrollment = manager.store.get(slug);
      Assert.equal(
        enrollment?.active,
        false,
        `Enrollment exists for ${slug} and is inactive`
      );
    }

    for (const slug of unenrolled) {
      Assert.ok(
        !manager.store.get(slug),
        `Enrollment does not exist for ${slug}`
      );
    }
  }

  loader.remoteSettingsClients.experiments.get.resolves([
    experimentB,
    rolloutB,
  ]);
  await check(
    [],
    [],
    [
      "experiment-a",
      "experiment-b",
      "experiment-c",
      "rollout-a",
      "rollout-b",
      "rollout-c",
    ]
  );

  // Order matters -- B will be checked before A.
  loader.remoteSettingsClients.experiments.get.resolves([
    experimentB,
    rolloutB,
    experimentA,
    rolloutA,
  ]);
  await check(
    ["experiment-a", "rollout-a"],
    [],
    ["experiment-b", "experiment-c", "rollout-b", "rollout-c"]
  );

  // B will see A enrolled.
  loader.remoteSettingsClients.experiments.get.resolves([
    experimentB,
    rolloutB,
    experimentA,
    rolloutA,
  ]);
  await check(
    ["experiment-a", "experiment-b", "rollout-a", "rollout-b"],
    [],
    ["experiment-c", "rollout-c"]
  );

  // Order matters -- A will be checked before C.
  loader.remoteSettingsClients.experiments.get.resolves([
    experimentB,
    rolloutB,
    experimentA,
    rolloutA,
    experimentC,
    rolloutC,
  ]);
  await check(
    [
      "experiment-a",
      "experiment-b",
      "experiment-c",
      "rollout-a",
      "rollout-b",
      "rollout-c",
    ],
    [],
    []
  );

  // A will see C has enrolled and unenroll. B will stay enrolled.
  await check(
    ["experiment-b", "experiment-c", "rollout-b", "rollout-c"],
    ["experiment-a", "rollout-a"],
    []
  );

  // A being unenrolled does not affect B. Rollout A will not re-enroll due to targeting.
  await check(
    ["experiment-b", "experiment-c", "rollout-b", "rollout-c"],
    ["experiment-a", "rollout-a"],
    []
  );

  await NimbusTestUtils.cleanupManager([
    "experiment-b",
    "experiment-c",
    "rollout-b",
    "rollout-c",
  ]);

  cleanupFeatures();
  await cleanup();
});

add_task(async function test_update_experiments_ordered_by_published_date() {
  // These are intentionally out of order so that we can test the order below.
  const recipes = [
    NimbusTestUtils.factories.recipe("published-date-2", {
      publishedDate: "2024-01-05T12:00:00Z",
    }),
    NimbusTestUtils.factories.recipe("no-published-date-1"),
    NimbusTestUtils.factories.recipe("published-date-1", {
      publishedDate: "2024-01-03T12:00:00Z",
    }),
    NimbusTestUtils.factories.recipe("no-published-date-2"),
  ];

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.stub(manager, "onRecipe");

  loader.remoteSettingsClients.experiments.get.resolves(recipes);
  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe
      .getCall(0)
      .calledWith(sinon.match({ slug: "no-published-date-1" }), "rs-loader")
  );
  Assert.ok(
    manager.onRecipe
      .getCall(1)
      .calledWith(sinon.match({ slug: "no-published-date-2" }), "rs-loader")
  );
  Assert.ok(
    manager.onRecipe
      .getCall(2)
      .calledWith(sinon.match({ slug: "published-date-1" }), "rs-loader")
  );
  Assert.ok(
    manager.onRecipe
      .getCall(3)
      .calledWith(sinon.match({ slug: "published-date-2" }), "rs-loader")
  );

  await cleanup();
});

add_task(
  async function test_record_is_ready_no_value_for_nimbus_is_ready_feature() {
    const { loader, cleanup } = await setupTest();

    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    await loader.updateRecipes();
    const isReadyEvents = Glean.nimbusEvents.isReady.testGetValue("events");
    Assert.equal(isReadyEvents.length, 1);

    await cleanup();
  }
);

add_task(
  async function test_record_is_ready_set_value_for_nimbus_is_ready_feature() {
    const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig("foo", {
      branchSlug: "wsup",
      featureId: "nimbusIsReady",
      value: { eventCount: 3 },
    });

    const { loader, manager, cleanup } = await setupTest();

    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    loader.remoteSettingsClients.experiments.get.resolves([recipe]);
    await loader.updateRecipes();

    const enrollment = manager.store.get(recipe.slug);
    Assert.equal(enrollment?.active, true, `Enrollment exists and is active`);

    const isReadyEvents = Glean.nimbusEvents.isReady.testGetValue("events");

    Assert.equal(isReadyEvents.length, 3);
    manager.unenroll(recipe.slug);

    await cleanup();
  }
);

add_task(async function test_updateRecipes_secure() {
  // This recipe is allowed from the secure collection but not the regular collection.
  const prefFlipRecipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "pref-flip",
    {
      featureId: "prefFlips",
      value: { prefs: {} },
    }
  );

  const testFeatureRecipe = NimbusTestUtils.factories.recipe("test-feature");

  const multiFeatureRecipe = NimbusTestUtils.factories.recipe("multi-feature", {
    branches: [
      NimbusTestUtils.factories.branch("control", {
        features: [
          prefFlipRecipe.branches[0].features[0],
          testFeatureRecipe.branches[0].features[0],
        ],
      }),
    ],
  });

  const TEST_CASES = [
    {
      experiments: [prefFlipRecipe],
      secureExperiments: [testFeatureRecipe],
      shouldEnroll: [],
    },
    {
      experiments: [testFeatureRecipe],
      secureExperiments: [prefFlipRecipe],
      shouldEnroll: [testFeatureRecipe, prefFlipRecipe],
    },
    {
      experiments: [multiFeatureRecipe],
      secureExperiments: [],
      shouldEnroll: [],
    },
    {
      experiments: [],
      secureExperiments: [multiFeatureRecipe],
      shouldEnroll: [multiFeatureRecipe],
    },
  ];

  for (const [
    idx,
    { experiments, secureExperiments, shouldEnroll },
  ] of TEST_CASES.entries()) {
    info(`Running test ${idx}`);

    const { sandbox, manager, cleanup } = await setupTest({
      init: false,
      experiments,
      secureExperiments,
    });

    sandbox.stub(manager, "onRecipe");

    await ExperimentAPI.init();

    const enrolledSlugs = manager.onRecipe
      .getCalls()
      .map(call => call.args[0].slug)
      .sort();

    const shouldEnrollSlugs = shouldEnroll.map(r => r.slug).sort();
    Assert.deepEqual(
      shouldEnrollSlugs,
      enrolledSlugs,
      `Should enroll in expected number of recipes`
    );

    for (const expectedRecipe of shouldEnroll) {
      Assert.ok(
        manager.onRecipe.calledWith(expectedRecipe, "rs-loader", {
          ok: true,
          status: MatchStatus.TARGETING_AND_BUCKETING,
        }),
        `Should enroll in ${expectedRecipe.slug}`
      );
    }

    await cleanup();
  }
});

add_task(async function test_updateRecipesClearsOptIns() {
  const now = new Date().getTime();
  const recipes = [
    NimbusTestUtils.factories.recipe("opt-in-1", {
      isFirefoxLabsOptIn: true,
      isRollout: true,
      targeting: "true",
      publishedDate: new Date(now).toISOString(),
    }),
    NimbusTestUtils.factories.recipe("opt-in-2", {
      isFirefoxLabsOptIn: true,
      isRollout: true,
      targeting: "false",
      publishedDate: new Date(now + 10000).toISOString(),
    }),
  ];

  const { loader, manager, cleanup } = await setupTest();
  loader.remoteSettingsClients.experiments.get.resolves(recipes);

  await loader.updateRecipes();

  const expectedLabs = recipes.map(recipe => ({ recipe, source: "rs-loader" }));
  const forceEnroll = NimbusTestUtils.factories.recipe("force-enroll", {
    isFirefoxLabsOptIn: true,
    isRollout: true,
    targeting: "true",
    publishedDate: new Date(now + 20000).toISOString(),
  });

  Assert.ok(manager.registerOptIn(forceEnroll, "force-enrollment"));
  expectedLabs.push({ recipe: forceEnroll, source: "force-enrollment" });

  Assert.deepEqual(manager.optIns, expectedLabs);

  await loader.updateRecipes();

  Assert.deepEqual(
    manager.optIns.toSorted(orderByRecipePublishedDate),
    expectedLabs
  );

  recipes.splice(1, 1);
  expectedLabs.splice(1, 1);

  await loader.updateRecipes();

  Assert.deepEqual(
    manager.optIns.toSorted(orderByRecipePublishedDate),
    expectedLabs
  );

  recipes.splice(0, 1);
  expectedLabs.splice(0, 1);

  await loader.updateRecipes();
  Assert.deepEqual(
    manager.optIns.toSorted(orderByRecipePublishedDate),
    expectedLabs
  );

  await cleanup();
});

add_task(async function test_updateRecipes_optInsStayEnrolled() {
  info("testing opt-ins stay enrolled after update");

  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "opt-in",
    { featureId: "no-feature-firefox-desktop" },
    {
      isFirefoxLabsOptIn: true,
      isRollout: true,
    }
  );

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "control" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  await loader.updateRecipes();
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in stayed enrolled");

  manager.unenroll("opt-in");

  await cleanup();
});

add_task(async function test_updateRecipes_optInsUnerollOnFalseTargeting() {
  info("testing opt-ins unenroll after targeting becomes false");

  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "opt-in",
    { featureId: "no-feature-firefox-desktop" },
    {
      isFirefoxLabsOptIn: true,
      isRollout: true,
    }
  );

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "control" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  recipe.targeting = "false";
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in")?.active, "Opt-in unenrolled");

  await cleanup();
});

add_task(async function test_updateRecipes_bucketingCausesOptInUnenrollments() {
  info("testing opt-in rollouts unenroll after if bucketing changes");

  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "opt-in",
    { featureId: "no-feature-firefox-desktop" },
    {
      isFirefoxLabsOptIn: true,
      isRollout: true,
    }
  );

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "control" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  recipe.bucketConfig.count = 0;
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in")?.active, "Opt-in unenrolled");

  await cleanup();
});

add_task(async function test_updateRecipes_reEnrollRolloutOptin() {
  info(
    "testing opt-in rollouts do not re-enroll automatically if bucketing changes"
  );
  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "opt-in",
    { featureId: "no-feature-firefox-desktop" },
    {
      isFirefoxLabsOptIn: true,
      isRollout: true,
    }
  );

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "control" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  recipe.bucketConfig.count = 0;
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in").active, "Opt-in unenrolled");

  recipe.bucketConfig.count = 1000;
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in").active, "Opt-in not re-enrolled");

  await cleanup();
});

add_task(async function test_updateRecipes_enrollmentStatus_telemetry() {
  // Create a feature for each experiment so that they aren't competing.
  const features = [
    new ExperimentFeature("test-feature-0", { variables: {} }),
    new ExperimentFeature("test-feature-1", { variables: {} }),
    new ExperimentFeature("test-feature-2", { variables: {} }),
    new ExperimentFeature("test-feature-3", { variables: {} }),
    new ExperimentFeature("test-feature-4", { variables: {} }),
    new ExperimentFeature("test-feature-5", {
      variables: {
        foo: { type: "string" },
      },
    }),
    new ExperimentFeature("test-feature-6", { variables: {} }),
    new ExperimentFeature("test-feature-7", { variables: {} }),
    new ExperimentFeature("test-feature-8", { variables: {} }),
  ];

  const cleanupFeatures = NimbusTestUtils.addTestFeatures(...features);

  function recipe(slug, featureId, value = null) {
    return NimbusTestUtils.factories.recipe.withFeatureConfig(slug, {
      featureId,
      value: value ?? {},
    });
  }
  const { loader, manager, cleanup } = await setupTest();

  // Prime the store with currently valid recipes.
  await manager.enroll(recipe("was-enrolled", "test-feature-0"), "rs-loader");
  await manager.enroll(recipe("stays-enrolled", "test-feature-2"), "rs-loader");
  await manager.enroll(
    recipe("recipe-mismatch", "test-feature-3"),
    "rs-loader"
  );
  await manager.enroll(recipe("invalid-recipe", "test-feature-4"), "rs-loader");
  await manager.enroll(recipe("invalid-branch", "test-feature-5"), "rs-loader");
  await manager.enroll(
    recipe("invalid-feature", "test-feature-6"),
    "rs-loader"
  );
  await manager.enroll(
    recipe("l10n-missing-locale", "test-feature-7"),
    "rs-loader"
  );
  await manager.enroll(
    recipe("l10n-missing-entry", "test-feature-8"),
    "rs-loader"
  );

  // Create another set of recipes, most of which are invalid, and trigger the
  // RSEL with those recipes.
  const recipes = [
    recipe("enrolls", "test-feature-1"),
    recipe("stays-enrolled", "test-feature-2"),
    {
      ...recipe("recipe-mismatch", "test-feature-3"),
      targeting: "false",
    },
    {
      ...recipe("invalid-recipe", "test-feature-4"),
      isRollout: "true",
    },
    recipe("invalid-branch", "test-feature-5", {
      foo: 1,
    }),
    recipe("invalid-feature", "unknown-feature"),
    {
      ...recipe("l10n-missing-locale", "test-feature-6"),
      localizations: {},
    },
    {
      ...recipe("l10n-missing-entry", "test-feature-7", {
        foo: {
          $l10n: {
            id: "foo-string",
            comment: "foo comment",
            text: "foo text",
          },
        },
      }),
      localizations: {
        "en-US": {},
      },
    },
  ];

  loader.remoteSettingsClients.experiments.get.resolves(recipes);

  await GleanPings.nimbusTargetingContext.testSubmission(
    () => {
      Assert.deepEqual(
        Glean.nimbusEvents.enrollmentStatus
          .testGetValue("nimbus-targeting-context")
          ?.map(ev => ev.extra),
        [
          {
            reason: "Qualified",
            branch: "control",
            slug: "was-enrolled",
            status: "Enrolled",
          },
          {
            branch: "control",
            reason: "Qualified",
            status: "Enrolled",
            slug: "stays-enrolled",
          },
          {
            branch: "control",
            slug: "recipe-mismatch",
            status: "Enrolled",
            reason: "Qualified",
          },
          {
            branch: "control",
            slug: "invalid-recipe",
            reason: "Qualified",
            status: "Enrolled",
          },
          {
            slug: "invalid-branch",
            reason: "Qualified",
            status: "Enrolled",
            branch: "control",
          },
          {
            status: "Enrolled",
            reason: "Qualified",
            slug: "invalid-feature",
            branch: "control",
          },
          {
            slug: "l10n-missing-locale",
            status: "Enrolled",
            branch: "control",
            reason: "Qualified",
          },
          {
            status: "Enrolled",
            slug: "l10n-missing-entry",
            reason: "Qualified",
            branch: "control",
          },
          {
            branch: "control",
            slug: "was-enrolled",
            status: "WasEnrolled",
          },
          {
            status: "Disqualified",
            branch: "control",
            slug: "recipe-mismatch",
            reason: "NotTargeted",
          },
          {
            slug: "invalid-recipe",
            error_string: "invalid-recipe",
            status: "Disqualified",
            reason: "Error",
            branch: "control",
          },
          {
            branch: "control",
            status: "Disqualified",
            slug: "invalid-branch",
            error_string: "invalid-branch",
            reason: "Error",
          },
          {
            slug: "invalid-feature",
            status: "Disqualified",
            branch: "control",
            reason: "Error",
            error_string: "invalid-feature",
          },
          {
            reason: "Error",
            status: "Disqualified",
            branch: "control",
            slug: "l10n-missing-locale",
            error_string: "l10n-missing-locale",
          },
          {
            slug: "l10n-missing-entry",
            reason: "Error",
            branch: "control",
            status: "Disqualified",
            error_string: "l10n-missing-entry",
          },
          {
            slug: "enrolls",
            reason: "Qualified",
            branch: "control",
            status: "Enrolled",
          },
        ]
      );
    },
    async () => await loader.updateRecipes("test")
  );

  manager.unenroll("stays-enrolled");
  manager.unenroll("enrolls");

  cleanupFeatures();
  await cleanup();
});

add_task(async function test_updateRecipes_enrollmentStatus_notEnrolled() {
  const features = [
    new ExperimentFeature("test-feature-0", { variables: {} }),
    new ExperimentFeature("test-feature-1", { variables: {} }),
    new ExperimentFeature("test-feature-2", { variables: {} }),
    new ExperimentFeature("test-feature-3", { variables: {} }),
    new ExperimentFeature("test-feature-4", { variables: {} }),
    new ExperimentFeature("test-feature-5", { variables: {} }),
    new ExperimentFeature("test-feature-6", { variables: {} }),
    new ExperimentFeature("test-feature-7", { variables: {} }),
    new ExperimentFeature("test-feature-8", { variables: {} }),
  ];

  const cleanupFeatures = NimbusTestUtils.addTestFeatures(...features);

  function recipe(slug, featureId) {
    return NimbusTestUtils.factories.recipe.withFeatureConfig(slug, {
      featureId,
    });
  }

  const recipes = [
    {
      ...recipe("enrollment-paused", "test-feature-0"),
      isEnrollmentPaused: true,
    },
    {
      ...recipe("no-match", "test-feature-1"),
      targeting: "false",
    },
    {
      ...recipe("targeting-only", "test-feature-2"),
      bucketConfig: NimbusTestUtils.factories.bucketConfig({
        count: 0,
      }),
    },
    {
      ...recipe("already-enrolled-rollout", "test-feature-3"),
      isRollout: true,
    },
    recipe("already-enrolled-experiment", "test-feature-3"),
  ];

  const { loader, manager, cleanup } = await setupTest();

  await manager.enroll(
    { ...recipe("enrolled-rollout", "test-feature-3"), isRollout: true },
    "force-enrollment"
  );
  await manager.enroll(
    recipe("enrolled-experiment", "test-feature-3"),
    "force-enrollment"
  );

  loader.remoteSettingsClients.experiments.get.resolves(recipes);

  await GleanPings.nimbusTargetingContext.testSubmission(
    () => {
      Assert.deepEqual(
        Glean.nimbusEvents.enrollmentStatus
          .testGetValue("nimbus-targeting-context")
          ?.map(ev => ev.extra),
        [
          {
            reason: "OptIn",
            status: "Enrolled",
            branch: "control",
            slug: "enrolled-rollout",
          },
          {
            branch: "control",
            reason: "OptIn",
            status: "Enrolled",
            slug: "enrolled-experiment",
          },
          {
            slug: "enrollment-paused",
            status: "NotEnrolled",
            reason: "EnrollmentsPaused",
          },
          {
            slug: "no-match",
            status: "NotEnrolled",
            reason: "NotTargeted",
          },
          {
            slug: "targeting-only",
            reason: "NotSelected",
            status: "NotEnrolled",
          },
          {
            conflict_slug: "enrolled-rollout",
            slug: "already-enrolled-rollout",
            reason: "FeatureConflict",
            status: "NotEnrolled",
          },
          {
            slug: "already-enrolled-experiment",
            status: "NotEnrolled",
            conflict_slug: "enrolled-experiment",
            reason: "FeatureConflict",
          },
        ]
      );
    },
    async () => await loader.updateRecipes("timer")
  );

  manager.unenroll("enrolled-experiment");
  manager.unenroll("enrolled-rollout");

  cleanupFeatures();
  await cleanup();
});

async function doEnrollmentStatusOptOutTest(
  expectedEvents,
  expectedEnrollments
) {
  const recipes = [
    NimbusTestUtils.factories.recipe.withFeatureConfig("previous-experiment", {
      featureId: "no-feature-firefox-desktop",
    }),
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "previous-rollout",
      { featureId: "no-feature-firefox-desktop" },
      { isRollout: true }
    ),
    NimbusTestUtils.factories.recipe.withFeatureConfig("experiment", {
      featureId: "no-feature-firefox-desktop",
    }),
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "rollout",
      { featureId: "no-feature-firefox-desktop" },
      { isRollout: true }
    ),
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "labs",
      { featureId: "no-feature-firefox-desktop" },
      {
        isRollout: true,
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      }
    ),
  ];

  const { cleanup } = await setupTest({
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(recipes[0], {
        store,
        extra: {
          active: false,
          unenrollReason: NimbusTelemetry.UnenrollReason.STUDIES_OPT_OUT,
        },
      });

      NimbusTestUtils.addEnrollmentForRecipe(recipes[1], {
        store,
        extra: {
          active: false,
          unenrollReason: NimbusTelemetry.UnenrollReason.ROLLOUTS_OPT_OUT,
        },
      });
    },
    migrationState: NimbusTestUtils.migrationState.LATEST,
    experiments: recipes,
    init: false,
  });

  await GleanPings.nimbusTargetingContext.testSubmission(
    () => {
      Assert.deepEqual(
        Glean.nimbusEvents.enrollmentStatus
          .testGetValue("nimbus-targeting-context")
          ?.map(ev => ev.extra),
        expectedEvents
      );
    },
    () => ExperimentAPI.init()
  );

  await ExperimentAPI._rsLoader.finishedUpdating();

  await NimbusTestUtils.cleanupManager(expectedEnrollments);
  await cleanup();
}

add_task(async function testUpdateRecipesEnrollmentStatusStudiesOptOut() {
  Services.prefs.setBoolPref("app.shield.optoutstudies.enabled", false);
  await doEnrollmentStatusOptOutTest(
    [
      {
        slug: "previous-experiment",
        status: "NotEnrolled",
        reason: "OptOut",
      },
      {
        slug: "previous-rollout",
        status: "Enrolled",
        reason: "Qualified",
        branch: "control",
      },
      {
        slug: "experiment",
        status: "NotEnrolled",
        reason: "OptOut",
      },
      {
        slug: "rollout",
        status: "Enrolled",
        reason: "Qualified",
        branch: "control",
      },
    ],
    ["previous-rollout", "rollout"]
  );
  Services.prefs.setBoolPref("app.shield.optoutstudies.enabled", true);
});

add_task(async function testUpdateRecipesEnrollmentStatusRolloutsOptOut() {
  Services.prefs.setBoolPref("nimbus.rollouts.enabled", false);

  await doEnrollmentStatusOptOutTest(
    [
      {
        slug: "previous-rollout",
        status: "NotEnrolled",
        reason: "OptOut",
      },
      {
        slug: "experiment",
        status: "Enrolled",
        reason: "Qualified",
        branch: "control",
      },
      {
        slug: "rollout",
        status: "NotEnrolled",
        reason: "OptOut",
      },
    ],
    ["experiment"]
  );
  Services.prefs.setBoolPref("nimbus.rollouts.enabled", true);
});

add_task(async function test_updateRecipesWithPausedEnrollment() {
  const recipe = NimbusTestUtils.factories.recipe("foo", {
    isEnrollmentPaused: true,
  });

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "_enroll");
  loader.remoteSettingsClients.experiments.get.resolves([recipe]);

  await loader.updateRecipes("test");

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.ENROLLMENT_PAUSED,
    }),
    "Should call onRecipe with enrollments paused"
  );
  Assert.ok(
    manager._enroll.notCalled,
    "Should not call enroll for paused recipe"
  );

  await cleanup();
});

add_task(async function test_updateRecipesUnenrollsNotSeenRecipes() {
  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(TelemetryEnvironment, "setExperimentActive");
  sandbox.spy(TelemetryEnvironment, "setExperimentInactive");
  sandbox.spy(manager, "updateEnrollment");

  const recipe = NimbusTestUtils.factories.recipe("rollout", {
    isRollout: true,
  });

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(manager.store.get("rollout")?.active, "rollout is active");
  Assert.ok(
    TelemetryEnvironment.setExperimentActive.calledOnceWith("rollout"),
    "set experiment as active"
  );

  Assert.equal(
    Glean.nimbusEvents.enrollFailed.testGetValue("events"),
    undefined,
    "no enrollment failure events"
  );

  loader.remoteSettingsClients.experiments.get.resolves([]);
  await loader.updateRecipes();

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: "rollout" }),
      undefined,
      "rs-loader",
      { ok: true, status: MatchStatus.NOT_SEEN }
    ),
    "Should call updateEnrollment with recipe-not-seen"
  );

  Assert.ok(!manager.store.get("rollout").active, "rollout is inactive");
  Assert.equal(
    manager.store.get("rollout").unenrollReason,
    "recipe-not-seen",
    "rollout unenrolled for correct reason"
  );

  Assert.ok(
    TelemetryEnvironment.setExperimentInactive.calledOnceWith("rollout"),
    "set experiment as active"
  );

  Assert.equal(
    Glean.nimbusEvents.unenrollFailed.testGetValue("events"),
    undefined,
    "No unenrollment failure events"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment.testGetValue("events").map(e => e.extra),
    [
      {
        experiment: "rollout",
        branch: "control",
        reason: "recipe-not-seen",
      },
    ],
    "One unenrollment event"
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "rollout",
        extra: {
          reason: "recipe-not-seen",
        },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    },
    {
      clear: true,
    }
  );

  await cleanup();
});

add_task(async function test_updateRecipesUnenrollsTargetingMismatch() {
  const { loader, manager, cleanup } = await setupTest();

  const recipe = NimbusTestUtils.factories.recipe("only-once", {
    targeting: "!(experiment.slug in activeExperiments)",
  });

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(manager.store.get("only-once")?.active, "Enrolled");

  await loader.updateRecipes();

  Assert.ok(!manager.store.get("only-once").active, "Unenrolled");
  Assert.equal(
    manager.store.get("only-once").unenrollReason,
    "targeting-mismatch",
    "Unenroll reason matches"
  );

  await cleanup();
});

add_task(async function testUnenrollsFirst() {
  const e1 = NimbusTestUtils.factories.recipe("e1");
  const e2 = NimbusTestUtils.factories.recipe("e2");
  const e3 = NimbusTestUtils.factories.recipe("e3");
  const r1 = NimbusTestUtils.factories.recipe("r1", { isRollout: true });
  const r2 = NimbusTestUtils.factories.recipe("r2", { isRollout: true });
  const r3 = NimbusTestUtils.factories.recipe("r3", { isRollout: true });

  const { loader, manager, cleanup } = await setupTest();
  loader.remoteSettingsClients.experiments.get.resolves([
    e1,
    e2,
    e3,
    r1,
    r2,
    r3,
  ]);

  // e1 and r1 should enroll. The rest cannot enroll because of feature conflicts.
  await loader.updateRecipes();
  assertEnrollments(manager.store, ["e1", "r1"], []);

  // No change.
  await loader.updateRecipes();
  assertEnrollments(manager.store, ["e1", "r1"], []);

  // Remove e1. e1 should unenroll. e2 should enroll.
  loader.remoteSettingsClients.experiments.get.resolves([e2, e3, r1, r2, r3]);
  await loader.updateRecipes();
  assertEnrollments(manager.store, ["e2", "r1"], ["e1"]);

  // Remove r1. r1 should unenroll. r2 should enroll.
  loader.remoteSettingsClients.experiments.get.resolves([e2, e3, r2, r3]);
  await loader.updateRecipes();
  assertEnrollments(manager.store, ["e2", "r2"], ["e1", "r1"]);

  // Remove e2 and r2. e2 and r2 should unenroll. e3 and r3 should enroll.
  loader.remoteSettingsClients.experiments.get.resolves([e3, r3]);
  await loader.updateRecipes();
  assertEnrollments(manager.store, ["e3", "r3"], ["e1", "e2", "r1", "r2"]);

  manager.unenroll("e3");
  manager.unenroll("r3");

  await cleanup();
});

async function testRsClientGetThrows(collectionName) {
  info(`Testing with collectionName = ${collectionName}\n`);

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(loader, "getRecipesFromAllCollections");
  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "_unenroll");

  loader.remoteSettingsClients[collectionName].get.throws();

  await manager.enroll(
    NimbusTestUtils.factories.recipe.withFeatureConfig("recipe", {
      featureId: "no-feature-firefox-desktop",
    }),
    "rs-loader"
  );

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "collection threw"
  );

  Assert.ok(
    loader.getRecipesFromAllCollections.calledOnce,
    "getRecipesFromAllCollections called once"
  );

  Assert.ok(
    manager.updateEnrollment.notCalled,
    "updateEnrollment never called"
  );
  Assert.ok(manager._unenroll.notCalled, "unenroll not called");

  Assert.ok(manager.store.get("recipe").active, "experiment is still active");

  manager.unenroll("recipe");
  await cleanup();
}

add_task(async function testExperimentsRsClientGetThrows() {
  await testRsClientGetThrows("experiments");
});

add_task(async function testSecureExperimentsRsClientGetThrows() {
  await testRsClientGetThrows("secureExperiments");
});

add_task(async function testUnenrolledInAnotherProfileBeforeUpdate() {
  const otherProfileId1 = Services.uuid.generateUUID().toString().slice(1, -1);
  const otherProfileId2 = Services.uuid.generateUUID().toString().slice(1, -1);
  const e1 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-1",
    { featureId: "no-feature-firefox-desktop" }
  );
  const e2 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-2",
    { featureId: "no-feature-firefox-desktop" }
  );
  const e3 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-3",
    { featureId: "no-feature-firefox-desktop" }
  );
  const r1 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-1",
    { featureId: "no-feature-firefox-desktop" },
    { isRollout: true }
  );
  const r2 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-2",
    { featureId: "no-feature-firefox-desktop" },
    { isRollout: true }
  );
  await NimbusTestUtils.insertEnrollment(e1, "control", {
    extra: { active: false },
    profileId: otherProfileId1,
  });
  await NimbusTestUtils.insertEnrollment(e2, "control", {
    extra: { active: false },
    profileId: otherProfileId2,
  });
  await NimbusTestUtils.insertEnrollment(r1, "control", {
    extra: { active: false },
    profileId: otherProfileId1,
  });
  await NimbusTestUtils.insertEnrollment(r2, "control", {
    extra: { active: false },
    profileId: otherProfileId2,
  });

  let cleanup, store;
  await GleanPings.nimbusTargetingContext.testSubmission(
    () => {
      Assert.deepEqual(
        Glean.nimbusEvents.enrollmentStatus
          .testGetValue("nimbus-targeting-context")
          ?.map(ev => ev.extra),
        [
          {
            slug: "experiment-1",
            status: "NotEnrolled",
            reason: "UnenrolledInAnotherProfile",
          },
          {
            slug: "experiment-2",
            status: "NotEnrolled",
            reason: "UnenrolledInAnotherProfile",
          },
          {
            slug: "experiment-3",
            branch: "control",
            status: "Enrolled",
            reason: "Qualified",
          },
          {
            slug: "rollout-1",
            branch: "control",
            status: "Enrolled",
            reason: "Qualified",
          },
          {
            slug: "rollout-2",
            branch: "control",
            status: "Enrolled",
            reason: "Qualified",
          },
        ]
      );
    },
    async () =>
      ({ cleanup, store } = await setupTest({
        experiments: [e1, e2, e3, r1, r2],
      }))
  );

  Assert.ok(
    !store.has("experiment-1"),
    "Enrollment for experiment-1 does not exist"
  );
  Assert.ok(
    !store.has("experiment-2"),
    "Enrollment for experiment-1 does not exist"
  );
  Assert.ok(store.get("experiment-3").active, "Enrolled in experiment-3");
  Assert.ok(store.get("rollout-1").active, "Enrolled in rollout-1");
  Assert.ok(store.get("rollout-2").active, "Enrolled in rollout-2");

  await NimbusTestUtils.flushStore();

  Assert.equal(
    await NimbusTestUtils.queryEnrollment("experiment-1"),
    null,
    "Enrollment for experiment-1 does not exist in the database"
  );

  Assert.equal(
    await NimbusTestUtils.queryEnrollment("experiment-2"),
    null,
    "Enrollment for experiment-1 does not exist in the database"
  );

  await NimbusTestUtils.cleanupManager([
    "experiment-3",
    "rollout-1",
    "rollout-2",
  ]);
  await NimbusTestUtils.deleteEnrollmentsFromProfiles([
    otherProfileId1,
    otherProfileId2,
  ]);

  await cleanup();
});

add_task(async function testUnenrolledInAnotherProfileBetweenUpdates() {
  const otherProfileId1 = Services.uuid.generateUUID().toString().slice(1, -1);
  const otherProfileId2 = Services.uuid.generateUUID().toString().slice(1, -1);
  const e1 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-1",
    { featureId: "no-feature-firefox-desktop" }
  );
  const e2 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-2",
    { featureId: "no-feature-firefox-desktop" }
  );
  const e3 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "experiment-3",
    { featureId: "no-feature-firefox-desktop" }
  );
  const r1 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-1",
    { featureId: "no-feature-firefox-desktop" },
    { isRollout: true }
  );
  const r2 = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "rollout-2",
    { featureId: "no-feature-firefox-desktop" },
    { isRollout: true }
  );

  const { cleanup, loader, store } = await setupTest({
    experiments: [e1, e2, e3, r1, r2],
  });

  Assert.ok(store.get("experiment-1").active, "Enrolled in experiment-1");
  Assert.ok(store.get("experiment-2").active, "Enrolled in experiment-2");
  Assert.ok(store.get("experiment-3").active, "Enrolled in experiment-3");
  Assert.ok(store.get("rollout-1").active, "Enrolled in rollout-1");
  Assert.ok(store.get("rollout-2").active, "Enrolled in rollout-2");

  await loader.updateRecipes("timer");

  Assert.ok(store.get("experiment-1").active, "Still enrolled in experiment-1");
  Assert.ok(store.get("experiment-2").active, "Still enrolled in experiment-2");
  Assert.ok(store.get("experiment-3").active, "Still enrolled in experiment-3");
  Assert.ok(store.get("rollout-1").active, "Still enrolled in rollout-1");
  Assert.ok(store.get("rollout-2").active, "Still enrolled in rollout-2");

  await NimbusTestUtils.insertEnrollment(e1, "control", {
    extra: { active: false },
    profileId: otherProfileId1,
  });
  await NimbusTestUtils.insertEnrollment(e2, "control", {
    extra: { active: false },
    profileId: otherProfileId2,
  });
  await NimbusTestUtils.insertEnrollment(r1, "control", {
    extra: { active: false },
    profileId: otherProfileId1,
  });
  await NimbusTestUtils.insertEnrollment(r2, "control", {
    extra: { active: false },
    profileId: otherProfileId2,
  });

  await GleanPings.nimbusTargetingContext.testSubmission(
    () => {
      Assert.deepEqual(
        Glean.nimbusEvents.enrollmentStatus
          .testGetValue("nimbus-targeting-context")
          ?.map(ev => ev.extra),
        [
          {
            slug: "experiment-1",
            branch: "control",
            status: "Disqualified",
            reason: "UnenrolledInAnotherProfile",
          },
          {
            slug: "experiment-2",
            branch: "control",
            status: "Disqualified",
            reason: "UnenrolledInAnotherProfile",
          },
        ]
      );
    },
    async () => await loader.updateRecipes("timer")
  );

  {
    const enrollment = store.get("experiment-1");
    Assert.ok(!enrollment.active, "Unenrolled from experiment-1");
    Assert.equal(
      enrollment.unenrollReason,
      NimbusTelemetry.UnenrollReason.UNENROLLED_IN_ANOTHER_PROFILE
    );
  }
  {
    const enrollment = store.get("experiment-2");
    Assert.ok(!enrollment.active, "Unenrolled from experiment-2");
    Assert.equal(
      enrollment.unenrollReason,
      NimbusTelemetry.UnenrollReason.UNENROLLED_IN_ANOTHER_PROFILE
    );
  }
  Assert.ok(store.get("experiment-3").active, "Enrolled in experiment-3");
  Assert.ok(store.get("rollout-1").active, "Enrolled in rollout-1");
  Assert.ok(store.get("rollout-2").active, "Enrolled in rollout-2");

  await NimbusTestUtils.flushStore();

  {
    const dbEnrollment = await NimbusTestUtils.queryEnrollment("experiment-1");
    Assert.ok(
      !dbEnrollment.active,
      "experiment-1 enrollment is inactive in the database"
    );
    Assert.equal(
      dbEnrollment.unenrollReason,
      NimbusTelemetry.UnenrollReason.UNENROLLED_IN_ANOTHER_PROFILE
    );
  }
  {
    const dbEnrollment = await NimbusTestUtils.queryEnrollment("experiment-2");
    Assert.ok(
      !dbEnrollment.active,
      "experiment-2 enrollment is inactive in the database"
    );
    Assert.equal(
      dbEnrollment.unenrollReason,
      NimbusTelemetry.UnenrollReason.UNENROLLED_IN_ANOTHER_PROFILE
    );
  }

  await NimbusTestUtils.cleanupManager([
    "experiment-3",
    "rollout-1",
    "rollout-2",
  ]);
  await NimbusTestUtils.deleteEnrollmentsFromProfiles([
    otherProfileId1,
    otherProfileId2,
  ]);

  await cleanup();
});

add_task(async function test_remoteSettingsSyncError_backwardsSync() {
  const experiment = NimbusTestUtils.factories.recipe("experiment");
  const secureExperiment = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "secureExperiment",
    {
      featureId: "prefFlips",
      value: {
        prefs: {},
      },
    }
  );

  await assertSyncTimestamps({}, "No timestamps");

  const { manager, loader, cleanup } = await setupTest();
  Services.fog.testResetFOG(); // Clear the empty events that were triggered during initialization.

  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 0,
      "nimbus-secure-experiments (stubbed)": 0,
    },
    "timestamps updated"
  );

  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(12345);
  loader.remoteSettingsClients.experiments.get.resolves([experiment]);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(
    23456
  );
  loader.remoteSettingsClients.secureExperiments.get.resolves([
    secureExperiment,
  ]);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "collections healthy"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 12345,
      "nimbus-secure-experiments (stubbed)": 23456,
    },
    "timestamps updated"
  );
  Assert.ok(manager.store.get("experiment")?.active, "experiment enrolled");
  Assert.ok(
    manager.store.get("secureExperiment").active,
    "secureExperiment enrolled"
  );

  Services.fog.testResetFOG();

  // If a collections goes backwards, we will not sync.
  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(1);
  loader.remoteSettingsClients.experiments.get.resolves([]);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(
    34567
  );
  loader.remoteSettingsClients.secureExperiments.get.resolves([]);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "experiments moved backwards"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 12345,
      "nimbus-secure-experiments (stubbed)": 23456,
    },
    "timestamps not updated"
  );
  Assert.ok(manager.store.get("experiment")?.active, "experiment enrolled");
  Assert.ok(
    manager.store.get("secureExperiment")?.active,
    "secureExperiment enrolled"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "backwards-sync",
      },
    ],
    "experiments moved backwards"
  );

  Services.fog.testResetFOG();

  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(45678);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(1);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "experiments moved backwards"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 12345,
      "nimbus-secure-experiments (stubbed)": 23456,
    },
    "timestamps not updated"
  );
  Assert.ok(manager.store.get("experiment")?.active, "experiment enrolled");
  Assert.ok(
    manager.store.get("secureExperiment")?.active,
    "secureExperiment enrolled"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
      {
        collection: "nimbus-secure-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "backwards-sync",
      },
    ],
    "secureExperiments moved backwards"
  );

  Services.fog.testResetFOG();

  // If both collections move backwards, we will only see a single error.
  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(1);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(1);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "both collections moved backwards"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 12345,
      "nimbus-secure-experiments (stubbed)": 23456,
    },
    "timestamps not updated"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "backwards-sync",
      },
    ],
    "experiments moved backwards"
  );

  Services.fog.testResetFOG();

  // If both collections are the same we won't see any errors.
  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(12345);
  loader.remoteSettingsClients.experiments.get.resolves([experiment]);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(
    23456
  );
  loader.remoteSettingsClients.secureExperiments.get.resolves([
    secureExperiment,
  ]);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "collections healthy"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 12345,
      "nimbus-secure-experiments (stubbed)": 23456,
    },
    "timestamps not updated"
  );
  Assert.ok(manager.store.get("experiment").active, "experiment enrolled");
  Assert.ok(
    manager.store.get("secureExperiment").active,
    "secureExperiment enrolled"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [],
    "No events"
  );

  // Both collections should be allowed to move forwards.
  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(23456);
  loader.remoteSettingsClients.experiments.get.resolves([]);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "collections healthy"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 23456,
      "nimbus-secure-experiments (stubbed)": 23456,
    },
    "timestamps updated"
  );
  Assert.ok(!manager.store.get("experiment").active, "experiment unenrolled");
  Assert.ok(
    manager.store.get("secureExperiment").active,
    "secureExperiment enrolled"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
    ],
    "Experiment collection is empty"
  );

  Services.fog.testResetFOG();

  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(
    34567
  );
  loader.remoteSettingsClients.secureExperiments.get.resolves([]);

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "collections healthy"
  );
  await assertSyncTimestamps(
    {
      "nimbus-desktop-experiments (stubbed)": 23456,
      "nimbus-secure-experiments (stubbed)": 34567,
    },
    "timestamps updated"
  );
  Assert.ok(!manager.store.get("experiment").active, "experiment unenrolled");
  Assert.ok(
    !manager.store.get("secureExperiment").active,
    "secureExperiment unenrolled"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
    ],
    "Collections empty"
  );

  await cleanup();
});

add_task(async function test_remoteSettingsSyncError_empty() {
  const experiment = NimbusTestUtils.factories.recipe("experiment");
  const secureExperiment = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "secureExperiment",
    {
      featureId: "prefFlips",
      value: {
        prefs: {},
      },
    }
  );

  Services.fog.testResetFOG();
  const { manager, loader, cleanup } = await setupTest();

  // If both collections are empty we will see telemetry for both. There is a
  // migration event and an enabled event during startup.
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "enabled",
        reason: "empty",
      },
    ],
    "Submitted initial remoteSettingsSyncError telemetry"
  );
  Services.fog.testResetFOG();

  // We should observe an enrollment update.
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "empty collections don't prevent enrollment update"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
    ],
    "Empty event telemetry submitted"
  );

  Services.fog.testResetFOG();

  // If secureExperiments is empty we will see a single event and observe that
  // enrollments updated.
  loader.remoteSettingsClients.experiments.get.resolves([experiment]);
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "empty collections don't prevent enrollment update"
  );

  Assert.ok(manager.store.get("experiment")?.active, "experiment is active");
  Assert.ok(
    !manager.store.has("secureExperiment"),
    "secureExperiment is not enrolled"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [],
    "Empty secureExperiments collection does not emit telemetry"
  );

  Services.fog.testResetFOG();

  // If experiments is empty we will see a single event and observe that
  // enrollments updated.
  loader.remoteSettingsClients.experiments.get.resolves([]);
  loader.remoteSettingsClients.secureExperiments.get.resolves([
    secureExperiment,
  ]);
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "empty collections don't prevent enrollment update"
  );

  Assert.ok(
    !manager.store.get("experiment").active,
    "experiment is not active"
  );
  Assert.ok(
    manager.store.get("secureExperiment")?.active,
    "secureExperiment is active"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
    ],
    "Empty experiments collection"
  );

  Services.fog.testResetFOG();

  // If both collections contain recipes we will see no events.
  loader.remoteSettingsClients.experiments.get.resolves([
    { ...experiment, slug: "experiment2" },
  ]);
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "healthy collections"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [],
    "No events"
  );
  Assert.ok(
    !manager.store.get("experiment").active,
    "experiment is not active (ended)"
  );
  Assert.ok(manager.store.get("experiment2")?.active, "experiment2 is active");
  Assert.ok(
    manager.store.get("secureExperiment")?.active,
    "secureExperiment is active"
  );

  await NimbusTestUtils.cleanupManager(["experiment2", "secureExperiment"]);
  await cleanup();
});

add_task(async function test_remoteSettingsSyncError_getException() {
  const { loader, cleanup } = await setupTest();
  Services.fog.testResetFOG(); // Clear the empty events that were triggered during initialization.

  loader.remoteSettingsClients.experiments.get.rejects(new Error("ruh roh"));
  await updateAndAssertEnrollmentsUpdatedNotified(loader, false, "get threw");

  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "get-exception",
      },
    ],
    "get() threw"
  );

  await cleanup();
});

add_task(async function test_remoteSettingsSyncError_invalidLastModified() {
  const { loader, cleanup } = await setupTest();
  Services.fog.testResetFOG(); // Clear the empty events that were triggered during initialization.

  loader.remoteSettingsClients.experiments.db.getLastModified.resolves("never");
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "lastModified invalid"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "invalid-last-modified",
      },
    ],
    "getLastModified should have thrown"
  );

  await cleanup();
});

add_task(async function test_remoteSettingsSyncError_lastModifiedException() {
  const { loader, cleanup } = await setupTest();
  Services.fog.testResetFOG(); // Clear the empty events that were triggered during initialization.

  loader.remoteSettingsClients.experiments.db.getLastModified.rejects(
    new Error("ruh roh")
  );

  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "lastModified threw"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "last-modified-exception",
      },
    ],
    "getLastModified should have thrown"
  );

  await cleanup();
});

add_task(async function test_remoteSettingsSyncError_nullLastModified() {
  const experiment = NimbusTestUtils.factories.recipe("experiment");
  const secureExperiment = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "secureExperiment",
    {
      featureId: "prefFlips",
      value: {
        prefs: {},
      },
    }
  );

  const { manager, loader, cleanup } = await setupTest();
  Services.fog.testResetFOG(); // Clear the empty events that were triggered during initialization.

  // If the experiments collection contains an experiment but the
  // secureExperiments collection does not and has null lastModified then
  // enrollments will not update.
  loader.remoteSettingsClients.experiments.get.resolves([experiment]);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(
    null
  );
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "secureExperiments has null lastModified"
  );

  Assert.equal(manager.store.getAll().length, 0, "Enrollments did not update");
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-secure-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "null-last-modified",
      },
    ],
    "secureExperiments has null lastModified"
  );
  Services.fog.testResetFOG();

  // If the secureExperiments collection contains an experiment but the
  // experiments collection does not and has null lastModified then enrollments
  // will not update.
  loader.remoteSettingsClients.experiments.get.resolves([]);
  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(null);
  loader.remoteSettingsClients.secureExperiments.get.resolves([
    secureExperiment,
  ]);
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "experiments has null lastModified"
  );

  Assert.equal(manager.store.getAll().length, 0, "Enrollments did not update");
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "null-last-modified",
      },
    ],
    "Submitted failure telemetry"
  );
  Services.fog.testResetFOG();

  // If both collections are empty with null lastModified then enrollments will
  // not update but we will only see a single null-last-modified event.
  loader.remoteSettingsClients.secureExperiments.get.resolves([]);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(
    null
  );
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    false,
    "lastModified is not null for both collections"
  );

  Assert.equal(manager.store.getAll().length, 0, "Enrollments did not update");
  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "null-last-modified",
      },
    ],
    "Submitted failure telemetry"
  );
  Services.fog.testResetFOG();

  // If neither collection has null lastModified we should see no
  // null-last-modified events, but we will see empty events.
  loader.remoteSettingsClients.experiments.db.getLastModified.resolves(0);
  loader.remoteSettingsClients.secureExperiments.db.getLastModified.resolves(0);
  await updateAndAssertEnrollmentsUpdatedNotified(
    loader,
    true,
    "empty collections don't prevent enrollment update"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.remoteSettingsSyncError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        collection: "nimbus-desktop-experiments (stubbed)",
        force_sync: "false",
        trigger: "test",
        reason: "empty",
      },
    ],
    "Submitted failure telemetry"
  );

  await cleanup();
});

add_task(async function testUpdateRecipesOnlyFeatureIds() {
  const recipeA = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "previous-recipe",
    { featureId: "no-feature-firefox-desktop" }
  );

  const recipeB = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "new-recipe",
    { featureId: "testFeature" }
  );

  const { cleanup, loader, manager } = await setupTest({
    experiments: [recipeA],
  });

  Assert.ok(
    manager.store.get(recipeA.slug)?.active,
    "Enrolled in previous-recipe"
  );

  // recipeB becomes published.
  loader.remoteSettingsClients.experiments.get.resolves([recipeA, recipeB]);

  // We look only for testFeature recipes. recipeA should stay enrolled and
  // recipeB should be newly enrolled.
  await loader.updateRecipes("test", {
    onlyFeatureIds: new Set(["testFeature"]),
  });

  Assert.ok(
    manager.store.get(recipeA.slug).active,
    "Still enrolled in recipeA"
  );
  Assert.ok(manager.store.get(recipeB.slug)?.active, "Enrolled in recipeB");

  // We update only feature "unknown", which does not find any nor does it
  // affect current enrollments.
  await loader.updateRecipes("test", { onlyFeatureIds: new Set("unknown") });

  Assert.ok(
    manager.store.get(recipeA.slug).active,
    "Still enrolled in recipeA"
  );
  Assert.ok(
    manager.store.get(recipeB.slug).active,
    "Still enrolled in recipeB"
  );

  // recipeA becomes unpublished.
  loader.remoteSettingsClients.experiments.get.resolves([recipeB]);

  // We update only testFeature and so recipeA does not unenroll.
  await loader.updateRecipes("test", {
    onlyFeatureIds: new Set(["testFeature"]),
  });

  Assert.ok(
    manager.store.get(recipeA.slug).active,
    "Still enrolled in recipeA"
  );
  Assert.ok(
    manager.store.get(recipeB.slug).active,
    "Still enrolled in recipeB"
  );

  // recipeB becomes unpublished.
  loader.remoteSettingsClients.experiments.get.resolves([]);

  // We update only no-feature-firefox-desktop. recipeA unenrolls but recipeB stays enrolled.
  await loader.updateRecipes("test", {
    onlyFeatureIds: new Set(["no-feature-firefox-desktop"]),
  });
  {
    const enrollment = manager.store.get(recipeA.slug);
    Assert.ok(!enrollment.active, "No longer enrolled in recipeA");
    Assert.equal(enrollment.unenrollReason, "recipe-not-seen");

    Assert.ok(
      manager.store.get(recipeB.slug).active,
      "Still enrolled in recipeB"
    );
  }

  // Finally a regular update unenrolls recipeB.
  await loader.updateRecipes("test");
  Assert.ok(
    !manager.store.get(recipeB.slug).active,
    "No longer enrolled in recipeB"
  );

  await cleanup();
});

add_task(async function testUpdateRecipesOnlyFeatureIdsLabs() {
  const now = new Date().getTime();
  const experiments = [
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "no-feature-firefox-desktop",
      { featureId: "no-feature-firefox-desktop" },
      {
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "opt-in-1-title",
        firefoxLabsDescription: "opt-in-1-desc",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
        isRollout: true,
        targeting: "true",
        publishedDate: new Date(now).toISOString(),
      }
    ),
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "updated-separately",
      { featureId: "updated-separately" },
      {
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "opt-in-2-title",
        firefoxLabsDescription: "opt-in-2-desc",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
        isRollout: true,
        targeting: "false",
        publishedDate: new Date(now - 10000).toISOString(),
      }
    ),
  ];

  const features = [
    new ExperimentFeature("updated-separately", {
      isEarlyStartup: false,
      variables: {},
    }),
  ];

  const { cleanup, manager, loader } = await NimbusTestUtils.setupTest({
    experiments,
    features,
  });

  Assert.deepEqual(
    manager.optIns.toSorted(orderByRecipePublishedDate).map(r => r.recipe.slug),
    ["updated-separately", "no-feature-firefox-desktop"]
  );

  await loader.updateRecipes("test", {
    onlyFeatureIds: new Set(["updated-separately"]),
  });

  Assert.deepEqual(
    manager.optIns.toSorted(orderByRecipePublishedDate).map(r => r.recipe.slug),
    ["updated-separately", "no-feature-firefox-desktop"]
  );

  await cleanup();
});

add_task(async function testFinishedUpdatingResolvesAfterException() {
  const { sandbox, loader, cleanup } = await NimbusTestUtils.setupTest();

  // We need just need to trigger an exception inside #updateImpl.
  sandbox.stub(loader, "_partitionRecipes").throws(new Error("uh oh"));

  // This will only progress to the first await.
  const updatePromise = loader.updateRecipes("test");
  // This promise should resolve after updatePromise rejects.
  const finishedUpdatingPromise = loader.finishedUpdating();

  await Assert.rejects(updatePromise, /uh oh/);
  await finishedUpdatingPromise;

  Assert.ok(!loader._updating, "No longer updating");

  Assert.deepEqual(
    Glean.nimbusEvents.updateError
      .testGetValue("events")
      ?.map(ev => ev.extra) ?? [],
    [
      {
        error: "Error",
        trigger: "test",
        during_shutdown: "false",
      },
    ]
  );

  await cleanup();
});
