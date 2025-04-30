"use strict";

const { NimbusTelemetry } = ChromeUtils.importESModule(
  "resource://nimbus/lib/Telemetry.sys.mjs"
);
const { PrefUtils } = ChromeUtils.importESModule(
  "resource://normandy/lib/PrefUtils.sys.mjs"
);

const TEST_FALLBACK_PREF = "testprefbranch.config";
const FAKE_FEATURE_MANIFEST = {
  description: "Test feature",
  exposureDescription: "Used in tests",
  variables: {
    enabled: {
      type: "boolean",
      fallbackPref: "testprefbranch.enabled",
    },
    config: {
      type: "json",
      fallbackPref: TEST_FALLBACK_PREF,
    },
    remoteValue: {
      type: "boolean",
    },
    test: {
      type: "boolean",
    },
    title: {
      type: "string",
    },
  },
};

add_setup(function test_setup() {
  Services.fog.initializeFOG();
});

function setupTest(options) {
  return NimbusTestUtils.setupTest({ ...options, clearTelemetry: true });
}

add_task(async function test_ExperimentFeature_test_helper_ready() {
  const { manager, cleanup } = await setupTest();
  await manager.store.ready();

  const featureInstance = new ExperimentFeature("foo", FAKE_FEATURE_MANIFEST);

  const cleanupExperiment = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: "foo",
      value: { remoteValue: "mochitest", enabled: true },
    },
    {
      manager,
      isRollout: true,
    }
  );

  Assert.equal(
    featureInstance.getVariable("remoteValue"),
    "mochitest",
    "set by remote config"
  );

  cleanupExperiment();
  cleanup();
});

add_task(async function test_record_exposure_event() {
  Services.fog.testResetFOG();

  const { sandbox, manager, cleanup } = await setupTest();

  const featureInstance = new ExperimentFeature("foo", FAKE_FEATURE_MANIFEST);
  const exposureSpy = sandbox.spy(NimbusTelemetry, "recordExposure");
  const getExperimentSpy = sandbox.spy(ExperimentAPI, "getExperimentMetaData");

  NimbusTestUtils.assert.storeIsEmpty(manager.store);
  featureInstance.recordExposureEvent();

  Assert.ok(
    exposureSpy.notCalled,
    "should not emit an exposure event when no experiment is active"
  );

  // Check that there aren't any Glean exposure events yet
  var exposureEvents = Glean.nimbusEvents.exposure.testGetValue("events");
  Assert.equal(
    undefined,
    exposureEvents,
    "no Glean exposure events before exposure"
  );

  await manager.store.addEnrollment(
    ExperimentFakes.experiment("blah", {
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "foo",
            value: { enabled: false },
          },
        ],
      },
    })
  );

  featureInstance.recordExposureEvent();

  Assert.ok(
    exposureSpy.calledOnce,
    "should emit an exposure event when there is an experiment"
  );
  Assert.equal(getExperimentSpy.callCount, 2, "Should be called every time");

  // Check that the Glean exposure event was recorded.
  exposureEvents = Glean.nimbusEvents.exposure.testGetValue("events");
  // We expect only one event
  Assert.equal(1, exposureEvents.length);
  // And that one event matches the expected
  Assert.equal(
    "blah",
    exposureEvents[0].extra.experiment,
    "Glean.nimbusEvents.exposure recorded with correct experiment slug"
  );
  Assert.equal(
    "treatment",
    exposureEvents[0].extra.branch,
    "Glean.nimbusEvents.exposure recorded with correct branch slug"
  );
  Assert.equal(
    "foo",
    exposureEvents[0].extra.feature_id,
    "Glean.nimbusEvents.exposure recorded with correct feature id"
  );

  sandbox.restore();

  manager.unenroll("blah");
  cleanup();
});

add_task(async function test_record_exposure_event_once() {
  const { sandbox, manager, cleanup } = await setupTest();

  const featureInstance = new ExperimentFeature("foo", FAKE_FEATURE_MANIFEST);
  const exposureSpy = sandbox.spy(NimbusTelemetry, "recordExposure");

  await manager.store.addEnrollment(
    ExperimentFakes.experiment("blah", {
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "foo",
            value: { enabled: false },
          },
        ],
      },
    })
  );

  featureInstance.recordExposureEvent({ once: true });
  featureInstance.recordExposureEvent({ once: true });
  featureInstance.recordExposureEvent({ once: true });

  Assert.ok(
    exposureSpy.calledOnce,
    "Should emit a single exposure event when the once param is true."
  );

  // Check that the Glean exposure event was recorded.
  let exposureEvents = Glean.nimbusEvents.exposure.testGetValue("events");
  // We expect only one event
  Assert.equal(1, exposureEvents.length);

  manager.unenroll("blah");
  cleanup();
});

add_task(async function test_allow_multiple_exposure_events() {
  const { sandbox, manager, cleanup } = await setupTest();

  const featureInstance = new ExperimentFeature("foo", FAKE_FEATURE_MANIFEST);
  const exposureSpy = sandbox.spy(NimbusTelemetry, "recordExposure");

  // Clear any pre-existing data in Glean
  Services.fog.testResetFOG();

  let doExperimentCleanup = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: "foo",
      value: { enabled: false },
    },
    { manager }
  );

  featureInstance.recordExposureEvent();
  featureInstance.recordExposureEvent();
  featureInstance.recordExposureEvent();

  Assert.equal(
    exposureSpy.callCount,
    3,
    "Should emit an exposure event for each function call"
  );

  // Check that the Glean exposure event was recorded.
  let exposureEvents = Glean.nimbusEvents.exposure.testGetValue("events");
  // We expect 3 events
  Assert.equal(3, exposureEvents.length);

  doExperimentCleanup();
  cleanup();
});

add_task(async function test_onUpdate_after_store_ready() {
  const { sandbox, manager, cleanup } = await setupTest();
  const stub = sandbox.stub();

  const featureInstance = new ExperimentFeature(
    "test-feature",
    FAKE_FEATURE_MANIFEST
  );

  const rollout = ExperimentFakes.rollout("foo", {
    branch: {
      slug: "slug",
      features: [
        {
          featureId: featureInstance.featureId,
          value: {
            title: "hello",
            enabled: true,
          },
        },
      ],
    },
  });

  sandbox.stub(manager.store, "getAllActiveRollouts").returns([rollout]);

  featureInstance.onUpdate(stub);

  Assert.ok(stub.calledOnce, "Callback called");
  Assert.equal(stub.firstCall.args[0], "featureUpdate:test-feature");
  Assert.equal(stub.firstCall.args[1], "rollout-updated");

  PrefUtils.setPref(TEST_FALLBACK_PREF, JSON.stringify({ foo: true }), {
    branch: "default",
  });

  Assert.deepEqual(
    featureInstance.getVariable("config"),
    { foo: true },
    "Feature is ready even when initialized after store update"
  );
  Assert.equal(
    featureInstance.getVariable("title"),
    "hello",
    "Returns the NimbusTestUtils rollout default value"
  );

  cleanup();
});
