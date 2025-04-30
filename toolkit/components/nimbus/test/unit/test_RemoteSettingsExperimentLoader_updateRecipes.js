"use strict";

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
const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
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

add_setup(async function setup() {
  Services.fog.initializeFOG();
});

function setupTest({ ...args } = {}) {
  return NimbusTestUtils.setupTest({ ...args, clearTelemetry: true });
}

add_task(async function test_updateRecipes_invalidFeatureId() {
  const badRecipe = ExperimentFakes.recipe("foo", {
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

  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(badRecipe, "rs-loader", {
      ok: false,
      reason: "invalid-feature",
      featureIds: ["invalid-feature-id"],
    }),
    "should call onRecipe with invalid-feature"
  );
  Assert.ok(manager.enroll.notCalled, "Would not enroll");

  cleanup();
});

add_task(async function test_updateRecipes_invalidFeatureValue() {
  const badRecipe = ExperimentFakes.recipe("foo", {
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

  cleanup();
});

add_task(async function test_updateRecipes_invalidRecipe() {
  const badRecipe = ExperimentFakes.recipe("foo");
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

  cleanup();
});

add_task(async function test_updateRecipes_invalidRecipeAfterUpdate() {
  const recipe = ExperimentFakes.recipe("foo", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });

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

  cleanup();
});

add_task(async function test_updateRecipes_invalidBranchAfterUpdate() {
  const message = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(m => m.id === "MULTISTAGE_SPOTLIGHT_MESSAGE")
  );

  const recipe = ExperimentFakes.recipe("recipe", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
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

  cleanup();
});

add_task(async function test_updateRecipes_simpleFeatureInvalidAfterUpdate() {
  const recipe = ExperimentFakes.recipe("recipe", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });
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

  const { sandbox, loader, manager, initExperimentAPI, cleanup } =
    await setupTest({ init: false, experiments: [recipe] });

  sandbox.spy(loader, "updateRecipes");
  sandbox.spy(EnrollmentsContext.prototype, "_generateVariablesOnlySchema");
  sandbox.spy(manager, "onRecipe");
  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "enroll");
  sandbox.spy(manager, "_unenroll");

  await initExperimentAPI();

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

  cleanup();
});

add_task(async function test_updateRecipes_validationTelemetry() {
  const invalidRecipe = ExperimentFakes.recipe("invalid-recipe");
  delete invalidRecipe.channel;

  const invalidBranch = ExperimentFakes.recipe("invalid-branch");
  invalidBranch.branches[0].features[0].value.testInt = "hello";
  invalidBranch.branches[1].features[0].value.testInt = "world";

  const invalidFeature = ExperimentFakes.recipe("invalid-feature", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
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
      events: invalidFeature.branches[0].features.map(feature => ({
        feature: feature.featureId,
      })),
      callCount: 2,
    },
  ];

  const LEGACY_FILTER = {
    category: "normandy",
    method: "validationFailed",
    object: "nimbus_experiment",
  };

  for (const { recipe, reason, events, callCount } of TEST_CASES) {
    info(`Testing validation failed telemetry for reason = "${reason}" ...`);

    const { sandbox, initExperimentAPI, cleanup } = await setupTest({
      init: false,
      experiments: [recipe],
    });

    sandbox.spy(NimbusTelemetry, "recordValidationFailure");

    await initExperimentAPI();

    Assert.equal(
      NimbusTelemetry.recordValidationFailure.callCount,
      callCount,
      `Should call recordValidationFailure ${callCount} times for reason ${reason}`
    );

    const gleanEvents = Glean.nimbusEvents.validationFailed
      .testGetValue("events")
      .map(event => {
        event = { ...event };
        // We do not care about the timestamp.
        delete event.timestamp;
        return event;
      });

    const expectedGleanEvents = events.map(event => ({
      category: "nimbus_events",
      name: "validation_failed",
      extra: {
        experiment: recipe.slug,
        reason,
        ...event,
      },
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

    cleanup();
  }
});

add_task(async function test_updateRecipes_validationDisabled() {
  Services.prefs.setBoolPref("nimbus.validation.enabled", false);

  const invalidRecipe = ExperimentFakes.recipe("invalid-recipe", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });
  delete invalidRecipe.channel;

  const invalidBranch = ExperimentFakes.recipe("invalid-branch", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });
  invalidBranch.branches[0].features[0].value.testInt = "hello";
  invalidBranch.branches[1].features[0].value.testInt = "world";

  const invalidFeature = ExperimentFakes.recipe("invalid-feature", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
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
    const { sandbox, manager, initExperimentAPI, cleanup } = await setupTest({
      init: false,
      experiments: [recipe],
    });

    sandbox.stub(manager, "enroll");
    sandbox.spy(manager, "onRecipe");
    sandbox.spy(NimbusTelemetry, "recordValidationFailure");

    await initExperimentAPI();

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

    cleanup();
  }

  Services.prefs.clearUserPref("nimbus.validation.enabled");
});

add_task(async function test_updateRecipes_appId() {
  const recipe = ExperimentFakes.recipe("background-task-recipe", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "backgroundTaskMessage",
            value: {},
          },
        ],
      },
    ],
  });

  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(manager, "onRecipe");
  sandbox.stub(manager, "enroll");

  info("Testing updateRecipes() with the default application ID");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: false,
      reason: "unsupported-feature",
      featureIds: ["backgroundTaskMessage"],
    }),
    "Should call onRecipe with unsupported-feature"
  );
  Assert.ok(manager.enroll.notCalled, "Would not enroll");

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

  cleanup();
});

add_task(async function test_updateRecipes_withPropNotInManifest() {
  const recipe = ExperimentFakes.recipe("foo", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
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

  cleanup();
});

add_task(async function test_updateRecipes_recipeAppId() {
  const recipe = ExperimentFakes.recipe("mobile-experiment", {
    appId: "org.mozilla.firefox",
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "mobile-feature",
            value: {
              enabled: true,
            },
          },
        ],
      },
    ],
  });

  const { sandbox, loader, manager, cleanup } = await setupTest();
  sandbox.stub(manager, "onRecipe");

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(manager.onRecipe.notCalled, ".onRecipe was never called");

  cleanup();
});

add_task(async function test_updateRecipes_featureValidationOptOut() {
  const invalidFeatureRecipe = ExperimentFakes.recipe("invalid-recipe", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "testFeature",
            value: {
              enabled: "true",
              testInt: false,
            },
          },
        ],
      },
    ],
  });

  const message = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(m => m.id === "MULTISTAGE_SPOTLIGHT_MESSAGE")
  );
  delete message.template;

  const invalidMsgRecipe = ExperimentFakes.recipe("invalid-recipe", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "spotlight",
            value: message,
          },
        ],
      },
    ],
  });

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

    cleanup();
  }
});

add_task(async function test_updateRecipes_invalidFeature_mismatch() {
  info(
    "Testing that we do not submit validation telemetry when the targeting does not match"
  );
  const recipe = ExperimentFakes.recipe("recipe", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "bogus",
            value: {
              bogus: "bogus",
            },
          },
        ],
      },
    ],
    targeting: "false",
  });

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
  ok(
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

  cleanup();
});

add_task(async function test_updateRecipes_rollout_bucketing() {
  const experiment = ExperimentFakes.recipe("experiment", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "testFeature",
            value: {},
          },
        ],
      },
    ],
    bucketConfig: {
      namespace: "nimbus-test-utils",
      randomizationUnit: "normandy_id",
      start: 0,
      count: 1000,
      total: 1000,
    },
  });
  const rollout = ExperimentFakes.recipe("rollout", {
    isRollout: true,
    branches: [
      {
        slug: "rollout",
        ratio: 1,
        features: [
          {
            featureId: "testFeature",
            value: {},
          },
        ],
      },
    ],
    bucketConfig: {
      namespace: "nimbus-test-utils",
      randomizationUnit: "normandy_id",
      start: 0,
      count: 1000,
      total: 1000,
    },
  });

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

  cleanup();
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

  cleanup();
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

  cleanup();
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

  cleanup();
});

add_task(async function test_active_and_past_experiment_targeting() {
  const cleanupFeatures = ExperimentTestUtils.addTestFeatures(
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

  const experimentA = ExperimentFakes.recipe("experiment-a", {
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [{ featureId: "feature-a", value: {} }],
      },
    ],
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });
  const experimentB = ExperimentFakes.recipe("experiment-b", {
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [{ featureId: "feature-b", value: {} }],
      },
    ],
    bucketConfig: experimentA.bucketConfig,
    targeting: "'experiment-a' in activeExperiments",
  });
  const experimentC = ExperimentFakes.recipe("experiment-c", {
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [{ featureId: "feature-c", value: {} }],
      },
    ],
    bucketConfig: experimentA.bucketConfig,
    targeting: "'experiment-a' in previousExperiments",
  });

  const rolloutA = ExperimentFakes.recipe("rollout-a", {
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [{ featureId: "feature-a", value: {} }],
      },
    ],
    bucketConfig: experimentA.bucketConfig,
    isRollout: true,
  });
  const rolloutB = ExperimentFakes.recipe("rollout-b", {
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [{ featureId: "feature-b", value: {} }],
      },
    ],
    bucketConfig: experimentA.bucketConfig,
    targeting: "'rollout-a' in activeRollouts",
    isRollout: true,
  });
  const rolloutC = ExperimentFakes.recipe("rollout-c", {
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [{ featureId: "feature-c", value: {} }],
      },
    ],
    bucketConfig: experimentA.bucketConfig,
    targeting: "'rollout-a' in previousRollouts",
    isRollout: true,
  });

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
  cleanup();
});

add_task(async function test_enrollment_targeting() {
  const cleanupFeatures = ExperimentTestUtils.addTestFeatures(
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
    return ExperimentFakes.recipe(name, {
      branches: [
        {
          ...ExperimentFakes.recipe.branches[0],
          features: [{ featureId, value: {} }],
        },
      ],
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      targeting,
      isRollout,
    });
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

  for (const slug of [
    "experiment-b",
    "experiment-c",
    "rollout-b",
    "rollout-c",
  ]) {
    manager.unenroll(slug);
  }

  cleanupFeatures();
  cleanup();
});

add_task(async function test_update_experiments_ordered_by_published_date() {
  // These are intentionally out of order so that we can test the order below.
  const recipes = [
    ExperimentFakes.recipe("published-date-2", {
      publishedDate: "2024-01-05T12:00:00Z",
    }),
    ExperimentFakes.recipe("no-published-date-1"),
    ExperimentFakes.recipe("published-date-1", {
      publishedDate: "2024-01-03T12:00:00Z",
    }),
    ExperimentFakes.recipe("no-published-date-2"),
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

  cleanup();
});

add_task(
  async function test_record_is_ready_no_value_for_nimbus_is_ready_feature() {
    const { loader, cleanup } = await NimbusTestUtils.setupTest({
      clearTelemetry: true,
    });

    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    await loader.updateRecipes();
    const isReadyEvents = Glean.nimbusEvents.isReady.testGetValue("events");
    Assert.equal(isReadyEvents.length, 1);

    cleanup();
  }
);

add_task(
  async function test_record_is_ready_set_value_for_nimbus_is_ready_feature() {
    const recipe = ExperimentFakes.recipe("foo", {
      branches: [
        {
          slug: "wsup",
          ratio: 1,
          features: [
            {
              featureId: "nimbusIsReady",
              value: { eventCount: 3 },
            },
          ],
        },
      ],
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
    });

    const { loader, manager, cleanup } = await NimbusTestUtils.setupTest();

    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    loader.remoteSettingsClients.experiments.get.resolves([recipe]);
    await loader.updateRecipes();

    const enrollment = manager.store.get(recipe.slug);
    Assert.equal(enrollment?.active, true, `Enrollment exists and is active`);

    const isReadyEvents = Glean.nimbusEvents.isReady.testGetValue("events");

    Assert.equal(isReadyEvents.length, 3);
    manager.unenroll(recipe.slug);

    cleanup();
  }
);

add_task(async function test_updateRecipes_secure() {
  // This recipe is allowed from the secure collection but not the regular collection.
  const prefFlipRecipe = ExperimentFakes.recipe("pref-flip", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [
          {
            featureId: "prefFlips",
            value: {
              prefs: {},
            },
          },
        ],
      },
    ],
  });

  const testFeatureRecipe = ExperimentFakes.recipe("test-feature", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });

  const multiFeatureRecipe = ExperimentFakes.recipe("mutli-feature", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        features: [
          prefFlipRecipe.branches[0].features[0],
          testFeatureRecipe.branches[0].features[0],
        ],
      },
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
      shouldEnroll: [],
    },
  ];

  for (const [
    idx,
    { experiments, secureExperiments, shouldEnroll },
  ] of TEST_CASES.entries()) {
    info(`Running test ${idx}`);

    const { sandbox, manager, initExperimentAPI, cleanup } = await setupTest({
      init: false,
      experiments,
      secureExperiments,
    });

    sandbox.stub(manager, "onRecipe");

    await initExperimentAPI();

    const enrolledSlugs = manager.onRecipe
      .getCalls()
      .map(call => call.args[0].slug);

    Assert.equal(
      manager.onRecipe.callCount,
      shouldEnroll.length,
      `Should enroll in expected number of recipes (enrolled in ${enrolledSlugs})`
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

    cleanup();
  }
});

add_task(async function test_updateRecipesClearsOptIns() {
  const now = new Date().getTime();
  const recipes = [
    ExperimentFakes.recipe("opt-in-1", {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      isFirefoxLabsOptIn: true,
      firefoxLabsTitle: "opt-in-1-title",
      firefoxLabsDescription: "opt-in-1-desc",
      firefoxLabsDescriptionLinks: null,
      firefoxLabsGroup: "group",
      requiresRestart: false,
      isRollout: true,
      branches: [ExperimentFakes.recipe.branches[0]],
      targeting: "true",
      publishedDate: new Date(now).toISOString(),
    }),
    ExperimentFakes.recipe("opt-in-2", {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      isFirefoxLabsOptIn: true,
      firefoxLabsTitle: "opt-in-2-title",
      firefoxLabsDescription: "opt-in-2-desc",
      firefoxLabsDescriptionLinks: null,
      firefoxLabsGroup: "group",
      requiresRestart: false,
      isRollout: true,
      branches: [ExperimentFakes.recipe.branches[0]],
      targeting: "false",
      publishedDate: new Date(now + 10000).toISOString(),
    }),
  ];

  const { loader, manager, cleanup } = await setupTest();
  loader.remoteSettingsClients.experiments.get.resolves(recipes);

  await loader.updateRecipes();

  Assert.deepEqual(manager.optInRecipes, recipes);

  await loader.updateRecipes();

  Assert.deepEqual(manager.optInRecipes, recipes);

  cleanup();
});

add_task(async function test_updateRecipes_optInsStayEnrolled() {
  info("testing opt-ins stay enrolled after update");

  const recipe = ExperimentFakes.recipe("opt-in", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        slug: "branch-0",
        firefoxLabsTitle: "branch-0-title",
      },
      {
        ...ExperimentFakes.recipe.branches[1],
        slug: "branch-1",
        firefoxLabsTitle: "branch-1-title",
      },
    ],
    targeting: "true",
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "opt-in-title",
    firefoxLabsDescription: "opt-in-desc",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "group",
    requiresRestart: false,
  });

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "branch-0" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  await loader.updateRecipes();
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in stayed enrolled");

  manager.unenroll("opt-in");

  cleanup();
});

add_task(async function test_updateRecipes_optInsUnerollOnFalseTargeting() {
  info("testing opt-ins unenroll after targeting becomes false");

  const recipe = ExperimentFakes.recipe("opt-in", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        slug: "branch-0",
        firefoxLabsTitle: "branch-0-title",
      },
      {
        ...ExperimentFakes.recipe.branches[1],
        slug: "branch-1",
        firefoxLabsTitle: "branch-1-title",
      },
    ],
    targeting: "true",
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "opt-in-title",
    firefoxLabsDescription: "opt-in-desc",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "group",
    requiresRestart: false,
  });

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "branch-0" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  recipe.targeting = "false";
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in")?.active, "Opt-in unenrolled");

  cleanup();
});

add_task(async function test_updateRecipes_bucketingCausesOptInUnenrollments() {
  info("testing opt-in rollouts unenroll after if bucketing changes");

  const recipe = ExperimentFakes.recipe("opt-in", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        slug: "branch-0",
      },
    ],
    targeting: "true",
    isFirefoxLabsOptIn: true,
    isRollout: true,
    firefoxLabsTitle: "opt-in-title",
    firefoxLabsDescription: "opt-in-desc",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "group",
    requiresRestart: false,
  });

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "branch-0" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  recipe.bucketConfig.count = 0;
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in")?.active, "Opt-in unenrolled");

  cleanup();
});

add_task(async function test_updateRecipes_reEnrollRolloutOptin() {
  info(
    "testing opt-in rollouts do not re-enroll automatically if bucketing changes"
  );

  const recipe = ExperimentFakes.recipe("opt-in", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        slug: "branch-0",
      },
    ],
    targeting: "true",
    isFirefoxLabsOptIn: true,
    isRollout: true,
    firefoxLabsTitle: "opt-in-title",
    firefoxLabsDescription: "opt-in-desc",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "group",
    requiresRestart: false,
  });

  const { loader, manager, cleanup } = await setupTest({
    experiments: [recipe],
  });

  await manager.enroll(recipe, "rs-loader", { branchSlug: "branch-0" });
  Assert.ok(manager.store.get("opt-in")?.active, "Opt-in was enrolled");

  recipe.bucketConfig.count = 0;
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in").active, "Opt-in unenrolled");

  recipe.bucketConfig.count = 1000;
  await loader.updateRecipes();
  Assert.ok(!manager.store.get("opt-in").active, "Opt-in not re-enrolled");

  cleanup();
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

  const cleanupFeatures = ExperimentTestUtils.addTestFeatures(...features);

  function recipe(slug, featureId, value = null) {
    return ExperimentFakes.recipe(slug, {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      branches: [
        {
          ratio: 1,
          slug: "control",
          features: [
            {
              featureId,
              value: value ?? {},
            },
          ],
        },
      ],
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

  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "nimbus_events.enrollment_status": true,
      },
    })
  );

  await loader.updateRecipes("test");

  const events = Glean.nimbusEvents.enrollmentStatus.testGetValue("events");

  Assert.deepEqual(events?.map(ev => ev.extra) ?? [], [
    {
      status: "WasEnrolled",
      branch: "control",
      slug: "was-enrolled",
    },
    {
      status: "Enrolled",
      reason: "Qualified",
      branch: "control",
      slug: "stays-enrolled",
    },
    {
      status: "Disqualified",
      reason: "NotTargeted",
      branch: "control",
      slug: "recipe-mismatch",
    },
    {
      status: "Disqualified",
      reason: "Error",
      error_string: "invalid-recipe",
      branch: "control",
      slug: "invalid-recipe",
    },
    {
      status: "Disqualified",
      reason: "Error",
      error_string: "invalid-branch",
      branch: "control",
      slug: "invalid-branch",
    },
    {
      status: "Disqualified",
      reason: "Error",
      error_string: "invalid-feature",
      branch: "control",
      slug: "invalid-feature",
    },
    {
      status: "Disqualified",
      reason: "Error",
      error_string: "l10n-missing-locale",
      branch: "control",
      slug: "l10n-missing-locale",
    },
    {
      status: "Disqualified",
      reason: "Error",
      error_string: "l10n-missing-entry",
      branch: "control",
      slug: "l10n-missing-entry",
    },
    {
      status: "Enrolled",
      reason: "Qualified",
      branch: "control",
      slug: "enrolls",
    },
  ]);

  manager.unenroll("stays-enrolled");
  manager.unenroll("enrolls");

  cleanupFeatures();
  cleanup();
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

  const cleanupFeatures = ExperimentTestUtils.addTestFeatures(...features);

  function recipe(slug, featureId) {
    return ExperimentFakes.recipe(slug, {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      branches: [
        {
          ratio: 1,
          slug: "control",
          features: [
            {
              featureId,
              value: {},
            },
          ],
        },
      ],
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
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 0,
      },
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

  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "nimbus_events.enrollment_status": true,
      },
    })
  );

  await loader.updateRecipes("timer");

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("events")
      ?.map(ev => ev.extra),
    [
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
        status: "NotEnrolled",
        reason: "NotSelected",
      },
      {
        slug: "already-enrolled-rollout",
        status: "NotEnrolled",
        reason: "FeatureConflict",
        conflict_slug: "enrolled-rollout",
      },
      {
        slug: "already-enrolled-experiment",
        status: "NotEnrolled",
        reason: "FeatureConflict",
        conflict_slug: "enrolled-experiment",
      },
    ]
  );

  manager.unenroll("enrolled-experiment");
  manager.unenroll("enrolled-rollout");

  cleanupFeatures();
  cleanup();
});

add_task(async function test_updateRecipesWithPausedEnrollment() {
  const recipe = ExperimentFakes.recipe("foo", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
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

  cleanup();
});

add_task(async function test_updateRecipesUnenrollsNotSeenRecipes() {
  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.spy(TelemetryEnvironment, "setExperimentActive");
  sandbox.spy(TelemetryEnvironment, "setExperimentInactive");
  sandbox.spy(manager, "updateEnrollment");

  const recipe = ExperimentFakes.recipe("rollout", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [ExperimentFakes.recipe.branches[0]],
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

  cleanup();
});

add_task(async function test_updateRecipesUnenrollsTargetingMismatch() {
  const { loader, manager, cleanup } = await setupTest();

  const recipe = ExperimentFakes.recipe("only-once", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
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

  cleanup();
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

  cleanup();
});
