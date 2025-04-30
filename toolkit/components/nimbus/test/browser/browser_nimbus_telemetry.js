"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const TELEMETRY_CATEGORY = "normandy";
const TELEMETRY_OBJECT = "nimbus_experiment";
// Included with active experiment information
const EXPERIMENT_TYPE = "nimbus";
const EVENT_FILTER = { category: TELEMETRY_CATEGORY };

add_setup(async function () {
  let sandbox = sinon.createSandbox();
  // stub the `observe` method to make sure the Experiment Manager
  // pref listener doesn't trigger and cause side effects
  sandbox.stub(ExperimentManager, "observe");
  await SpecialPowers.pushPrefEnv({
    set: [["app.shield.optoutstudies.enabled", true]],
  });

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
  });
});

add_task(async function test_experiment_enroll_unenroll_Telemetry() {
  Services.telemetry.clearEvents();
  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "test-feature",
    value: { enabled: false },
  });

  const experiment = ExperimentAPI.getExperimentMetaData({
    featureId: "test-feature",
  });

  Assert.ok(!!experiment, "Should be enrolled in the experiment");
  TelemetryTestUtils.assertEvents(
    [
      {
        method: "enroll",
        object: TELEMETRY_OBJECT,
        value: experiment.slug,
        extra: {
          experimentType: EXPERIMENT_TYPE,
          branch: experiment.branch.slug,
        },
      },
    ],
    EVENT_FILTER
  );

  cleanup();

  TelemetryTestUtils.assertEvents(
    [
      {
        method: "unenroll",
        object: TELEMETRY_OBJECT,
        value: experiment.slug,
        extra: {
          reason: "unknown",
          branch: experiment.branch.slug,
        },
      },
    ],
    EVENT_FILTER
  );
});

add_task(async function test_experiment_expose_Telemetry() {
  const feature = new ExperimentFeature("test-feature", {
    description: "Test feature",
    exposureDescription: "Used in tests",
  });

  const cleanup = await ExperimentFakes.enrollWithFeatureConfig({
    featureId: "test-feature",
    value: { enabled: false },
  });

  let experiment = ExperimentAPI.getExperimentMetaData({
    featureId: "test-feature",
  });

  Services.telemetry.clearEvents();
  feature.recordExposureEvent();

  TelemetryTestUtils.assertEvents(
    [
      {
        method: "expose",
        object: TELEMETRY_OBJECT,
        value: experiment.slug,
        extra: {
          branchSlug: experiment.branch.slug,
          featureId: "test-feature",
        },
      },
    ],
    EVENT_FILTER
  );

  cleanup();
});

add_task(async function test_rollout_expose_Telemetry() {
  const featureManifest = {
    description: "Test feature",
    exposureDescription: "Used in tests",
  };
  const cleanup = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: "test-feature",
      value: { enabled: false },
    },
    { isRollout: true }
  );

  let rollout = ExperimentAPI.getRolloutMetaData({
    featureId: "test-feature",
  });

  Assert.ok(rollout.slug, "Found enrolled experiment");

  const feature = new ExperimentFeature("test-feature", featureManifest);

  Services.telemetry.clearEvents();
  feature.recordExposureEvent();

  TelemetryTestUtils.assertEvents(
    [
      {
        method: "expose",
        object: TELEMETRY_OBJECT,
        value: rollout.slug,
        extra: {
          branchSlug: rollout.branch.slug,
          featureId: feature.featureId,
        },
      },
    ],
    EVENT_FILTER
  );

  cleanup();
});
