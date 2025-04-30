/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const { recordTargetingContext } = ChromeUtils.importESModule(
  "resource://nimbus/lib/TargetingContextRecorder.sys.mjs"
);

const { JsonSchema } = ChromeUtils.importESModule(
  "resource://gre/modules/JsonSchema.sys.mjs"
);

add_setup(function setup() {
  Services.fog.initializeFOG();
});

const SCHEMAS = {
  get nimbusTelemetry() {
    return fetch(
      "resource://nimbus/schemas/NimbusTelemetryFeature.schema.json",
      { credentials: "omit" }
    ).then(rsp => rsp.json());
  },
};

function nimbusTargetingContextTelemetryDisabled() {
  return !Services.prefs.getBoolPref(
    "nimbus.telemetry.targetingContextEnabled"
  );
}

function setupTest({ ...args } = {}) {
  return NimbusTestUtils.setupTest({ ...args, clearTelemetry: true });
}

add_task(
  { skip_if: nimbusTargetingContextTelemetryDisabled },
  async function test_enrollAndUnenroll_gleanMetricConfiguration() {
    info(
      "Testing the interaction of gleanMetricConfiguration with submission of enrollment status and targeting context telemetry"
    );

    const experiment = ExperimentFakes.recipe("experiment", {
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
              featureId: "nimbusTelemetry",
              value: {
                gleanMetricConfiguration: {
                  metrics_enabled: {
                    "nimbus_targeting_environment.targeting_context_value": true,
                  },
                },
              },
            },
          ],
        },
      ],
    });

    const rollout = ExperimentFakes.recipe("rollout", {
      bucketConfig: experiment.bucketConfig,
      isRollout: true,
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: "nimbusTelemetry",
              value: {
                gleanMetricConfiguration: {
                  metrics_enabled: {
                    "nimbus_events.enrollment_status": true,
                  },
                },
              },
            },
          ],
        },
      ],
    });

    const { manager, cleanup } = await setupTest();

    // We don't call recordTargetingContext() here because we dont actually want
    // to do all the work when we're just testing whether or not the metrics are
    // being recorded.
    Glean.nimbusTargetingEnvironment.targetingContextValue.set(
      "nothing-active-0"
    );

    // We're submitting a bogus event here -- we shouldn't see it recorded.
    Glean.nimbusEvents.enrollmentStatus.record({ reason: "nothing-active-0" });

    Assert.equal(
      Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue(),
      null,
      "targetingContextValue not recorded by default"
    );
    Assert.equal(
      Glean.nimbusEvents.enrollmentStatus.testGetValue("events"),
      null,
      "enrollmentStatus not recorded by default"
    );

    // Because the feature listener gets triggered before we submit telemetry,
    // this will actually cause it to submit its own enrollment status telemetry
    // for enrollment.
    await manager.enroll(rollout, "rs-loader");

    Glean.nimbusTargetingEnvironment.targetingContextValue.set(
      "rollout-active-1"
    );

    Assert.equal(
      Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue(),
      null,
      "targetingContextValue not recorded by default"
    );

    Assert.deepEqual(
      Glean.nimbusEvents.enrollmentStatus
        .testGetValue("events")
        ?.map(ev => ev.extra),
      [
        {
          slug: rollout.slug,
          branch: rollout.branches[0].slug,
          status: "Enrolled",
          reason: "Qualified",
        },
      ],
      "Should have recorded enrollmentStatus (rollout enabled metric)"
    );

    // Likewise, because the feature listener gets triggered before we submit
    // telemetry, this will disable the metric before we submit it, so we
    // shouldn't see another event.
    await manager.enroll(experiment, "rs-loader");
    Assert.ok(
      manager.store.get(experiment.slug)?.active,
      "Experiment enrolled and active"
    );

    Glean.nimbusTargetingEnvironment.targetingContextValue.set(
      "experiment-active-2"
    );

    Assert.equal(
      Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue(),
      "experiment-active-2",
      "Targeting context metric was recorded"
    );

    Assert.deepEqual(
      Glean.nimbusEvents.enrollmentStatus
        .testGetValue("events")
        ?.map(ev => ev.extra),
      [
        {
          slug: rollout.slug,
          branch: rollout.branches[0].slug,
          status: "Enrolled",
          reason: "Qualified",
        },
      ],
      "Should not have recorded enrollmentStatus again (experiment disabled metric)"
    );

    // Now the listener triggers again and the metric re-enables. We should see
    // telemetry for this unenrollment but not setting the targeting context
    // value.
    await manager.unenroll(experiment.slug, { reason: "recipe-not-seen" });

    Glean.nimbusTargetingEnvironment.targetingContextValue.set(
      "rollout-active-3"
    );

    Assert.equal(
      Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue(),
      "experiment-active-2",
      "targetingContextValue was not recorded again"
    );

    Assert.deepEqual(
      Glean.nimbusEvents.enrollmentStatus
        .testGetValue("events")
        .map(ev => ev.extra),
      [
        {
          slug: rollout.slug,
          branch: rollout.branches[0].slug,
          status: "Enrolled",
          reason: "Qualified",
        },
        {
          slug: experiment.slug,
          branch: experiment.branches[0].slug,
          status: "WasEnrolled",
        },
      ]
    );

    // Finally, this disables the enrollment status metric in the onUpdate
    // handler. We don't see its unenrollment.
    await manager.unenroll(rollout.slug, { reason: "recipe-not-seen" });

    Glean.nimbusTargetingEnvironment.targetingContextValue.set(
      "nothing-active-0"
    );

    Assert.equal(
      Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue(),
      "experiment-active-2",
      "targetingContextValue was not recorded again"
    );

    Assert.deepEqual(
      Glean.nimbusEvents.enrollmentStatus
        .testGetValue("events")
        .map(ev => ev.extra),
      [
        {
          slug: rollout.slug,
          branch: rollout.branches[0].slug,
          status: "Enrolled",
          reason: "Qualified",
        },
        {
          slug: experiment.slug,
          branch: experiment.branches[0].slug,
          status: "WasEnrolled",
        },
      ]
    );

    Services.fog.testResetFOG();

    cleanup();
  }
);

add_task(async function test_featureConfigSchema_gleanMetricConfiguration() {
  function metricConfig(metric, value) {
    return {
      gleanMetricConfiguration: {
        metrics_enabled: {
          [metric]: value,
        },
      },
    };
  }

  const validator = new JsonSchema.Validator(await SCHEMAS.nimbusTelemetry);

  const ALLOWED = [
    "nimbus_targeting_environment.targeting_context_value",
    "nimbus_targeting_environment.pref_type_errors",
    "nimbus_targeting_environment.attr_eval_errors",
    "nimbus_targeting_environment.user_set_prefs",
    "nimbus_targeting_environment.pref_values",
    "nimbus_targeting_context.active_experiments",
    "nimbus_targeting_context.active_rollouts",
    "nimbus_targeting_context.addresses_saved",
    "nimbus_events.enrollment",
    "nimbus_events.is_ready",
    "nimbus_events.enrollment_status",
  ];

  for (const metric of ALLOWED) {
    for (const value of [true, false]) {
      Assert.ok(
        validator.validate(metricConfig(metric, value)).valid,
        `Can control ${metric} with nimbusTelemetry`
      );
    }
  }

  Assert.ok(
    !validator.validate(metricConfig("nimbus_events.enrollment", "true")).valid,
    "Schema enforces boolean values"
  );

  Assert.ok(
    !validator.validate(metricConfig("bogus", true)).valid,
    "Schema restricts metrics to nimbus metrics"
  );
});

add_task(async function test_featureConfigSchema_nimbusTargetingEnvironment() {
  const validator = new JsonSchema.Validator(await SCHEMAS.nimbusTelemetry);

  Assert.ok(
    validator.validate({
      nimbusTargetingEnvironment: {
        recordAttrs: ["activeExperiments"],
      },
    }).valid,
    "Schema validates"
  );

  Assert.ok(
    !validator.validate({
      nimbusTargetingEnvironment: {
        recordAttrs: "activeExperiments",
      },
    }).valid,
    "Schema enforces types for recordAttrs"
  );
});

add_task(
  { skip_if: nimbusTargetingContextTelemetryDisabled },
  async function test_nimbusTargetingEnvironment_recordAttrs() {
    const { manager, cleanup } = await setupTest();

    const cleanupExperiment = await ExperimentFakes.enrollWithFeatureConfig(
      {
        featureId: "nimbusTelemetry",
        value: {
          gleanMetricConfiguration: {
            metrics_enabled: {
              "nimbus_targeting_environment.targeting_context_value": true,
            },
          },
          nimbusTargetingEnvironment: {
            recordAttrs: [
              "activeExperiments",
              "activeRollouts",
              "enrollmentsMap",
            ],
          },
        },
      },
      {
        manager,
        source: "test",
        slug: "config",
      }
    );

    Assert.equal(
      Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue(),
      null,
      "The targetingContextValue metric has not been recorded yet."
    );

    GleanPings.nimbusTargetingContext.testBeforeNextSubmit(() => {
      Assert.deepEqual(
        JSON.parse(
          Glean.nimbusTargetingEnvironment.targetingContextValue.testGetValue()
        ),
        {
          activeExperiments: ["config"],
          activeRollouts: [],
          enrollmentsMap: [
            {
              experimentSlug: "config",
              branchSlug: "control",
            },
          ],
        }
      );
    });
    await recordTargetingContext();

    cleanupExperiment();
    cleanup();
  }
);
