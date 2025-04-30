"use strict";

const { TelemetryEnvironment } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryEnvironment.sys.mjs"
);
const STUDIES_OPT_OUT_PREF = "app.shield.optoutstudies.enabled";
const UPLOAD_ENABLED_PREF = "datareporting.healthreport.uploadEnabled";

add_setup(function test_setup() {
  Services.fog.initializeFOG();
});

function setupTest({ ...args } = {}) {
  return NimbusTestUtils.setupTest({ ...args, clearTelemetry: true });
}

/**
 * Normal unenrollment for experiments:
 * - set .active to false
 * - set experiment inactive in telemetry
 * - send unrollment event
 */
add_task(async function test_set_inactive() {
  const { manager, cleanup } = await setupTest();

  await manager.store.addEnrollment(ExperimentFakes.experiment("foo"));
  manager.unenroll("foo");

  Assert.equal(
    manager.store.get("foo").active,
    false,
    "should set .active to false"
  );

  cleanup();
});

add_task(async function test_unenroll_opt_out() {
  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);

  const { manager, cleanup } = await setupTest();
  const experiment = ExperimentFakes.experiment("foo");
  await manager.store.addEnrollment(experiment);

  // Check that there aren't any Glean normandy unenrollNimbusExperiment events yet
  Assert.equal(
    Glean.normandy.unenrollNimbusExperiment.testGetValue("events"),
    undefined,
    "no Glean normandy unenrollNimbusExperiment events before unenrollment"
  );

  // Check that there aren't any Glean unenrollment events yet
  Assert.equal(
    Glean.nimbusEvents.unenrollment.testGetValue("events"),
    undefined,
    "no Glean unenrollment events before unenrollment"
  );

  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, false);

  Assert.equal(
    manager.store.get(experiment.slug).active,
    false,
    "should set .active to false"
  );

  // We expect only one event and that that one event matches the expected enrolled experiment
  Assert.deepEqual(
    Glean.normandy.unenrollNimbusExperiment
      .testGetValue("events")
      .map(ev => ev.extra),
    [
      {
        value: experiment.slug,
        branch: experiment.branch.slug,
        reason: "studies-opt-out",
      },
    ]
  );

  // We expect only one event and that that one event matches the expected enrolled experiment
  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment.testGetValue("events").map(ev => ev.extra),
    [
      {
        experiment: experiment.slug,
        branch: experiment.branch.slug,
        reason: "studies-opt-out",
      },
    ]
  );

  cleanup();
  Services.prefs.clearUserPref(STUDIES_OPT_OUT_PREF);
});

add_task(async function test_unenroll_rollout_opt_out() {
  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, true);

  const { manager, cleanup } = await setupTest();
  const rollout = ExperimentFakes.rollout("foo");
  manager.store.addEnrollment(rollout);

  // Check that there aren't any Glean normandy unenrollNimbusExperiment events yet
  Assert.equal(
    Glean.normandy.unenrollNimbusExperiment.testGetValue("events"),
    undefined,
    "no Glean normandy unenrollNimbusExperiment events before unenrollment"
  );

  // Check that there aren't any Glean unenrollment events yet
  Assert.equal(
    Glean.nimbusEvents.unenrollment.testGetValue("events"),
    undefined,
    "no Glean unenrollment events before unenrollment"
  );

  Services.prefs.setBoolPref(STUDIES_OPT_OUT_PREF, false);

  Assert.equal(
    manager.store.get(rollout.slug).active,
    false,
    "should set .active to false"
  );

  // We expect only one event and that that one event matches the expected enrolled experiment
  Assert.deepEqual(
    Glean.normandy.unenrollNimbusExperiment
      .testGetValue("events")
      .map(ev => ev.extra),
    [
      {
        value: rollout.slug,
        branch: rollout.branch.slug,
        reason: "studies-opt-out",
      },
    ]
  );

  // We expect only one event and that that one event matches the expected enrolled experiment
  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment.testGetValue("events").map(ev => ev.extra),
    [
      {
        experiment: rollout.slug,
        branch: rollout.branch.slug,
        reason: "studies-opt-out",
      },
    ]
  );

  cleanup();
  Services.prefs.clearUserPref(STUDIES_OPT_OUT_PREF);
});

add_task(async function test_unenroll_uploadPref() {
  const { manager, cleanup } = await setupTest();
  const recipe = ExperimentFakes.recipe("foo");

  await manager.onStartup();
  await ExperimentFakes.enrollmentHelper(recipe, { manager });

  Assert.equal(
    manager.store.get(recipe.slug).active,
    true,
    "Should set .active to true"
  );

  Services.prefs.setBoolPref(UPLOAD_ENABLED_PREF, false);

  Assert.equal(
    manager.store.get(recipe.slug).active,
    false,
    "Should set .active to false"
  );

  cleanup();
  Services.prefs.clearUserPref(UPLOAD_ENABLED_PREF);
});

add_task(async function test_setExperimentInactive_called() {
  const { sandbox, manager, cleanup } = await setupTest();
  sandbox.spy(TelemetryEnvironment, "setExperimentInactive");

  const experiment = ExperimentFakes.recipe("foo", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
  });

  await manager.enroll(experiment);

  // Test Glean experiment API interaction
  Assert.notEqual(
    undefined,
    Services.fog.testGetExperimentData(experiment.slug),
    "experiment should be active before unenroll"
  );

  manager.unenroll("foo");

  Assert.ok(
    TelemetryEnvironment.setExperimentInactive.calledWith("foo"),
    "should call TelemetryEnvironment.setExperimentInactive with slug"
  );

  // Test Glean experiment API interaction
  Assert.equal(
    undefined,
    Services.fog.testGetExperimentData(experiment.slug),
    "experiment should be inactive after unenroll"
  );

  cleanup();
});

add_task(async function test_send_unenroll_event() {
  const { manager, cleanup } = await setupTest();
  const experiment = ExperimentFakes.experiment("foo");

  manager.store.addEnrollment(experiment);

  // Check that there aren't any Glean normandy unenrollNimbusExperiment events yet
  Assert.equal(
    Glean.normandy.unenrollNimbusExperiment.testGetValue("events"),
    undefined,
    "no Glean normandy unenrollNimbusExperiment events before unenrollment"
  );

  // Check that there aren't any Glean unenrollment events yet
  Assert.equal(
    Glean.nimbusEvents.unenrollment.testGetValue("events"),
    undefined,
    "no Glean unenrollment events before unenrollment"
  );

  manager.unenroll("foo", { reason: "some-reason" });

  // We expect only one event and that that one event matches the expected enrolled experiment
  Assert.deepEqual(
    Glean.normandy.unenrollNimbusExperiment
      .testGetValue("events")
      .map(ev => ev.extra),
    [
      {
        value: experiment.slug,
        branch: experiment.branch.slug,
        reason: "some-reason",
      },
    ]
  );

  // We expect only one event and that that one event matches the expected enrolled experiment
  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment.testGetValue("events").map(ev => ev.extra),
    [
      {
        experiment: experiment.slug,
        branch: experiment.branch.slug,
        reason: "some-reason",
      },
    ]
  );

  cleanup();
});

add_task(async function test_undefined_reason() {
  const { manager, cleanup } = await setupTest();
  const experiment = ExperimentFakes.experiment("foo");

  manager.store.addEnrollment(experiment);

  manager.unenroll("foo");

  // We expect only one event and that that one event reason matches the expected reason
  Assert.deepEqual(
    Glean.normandy.unenrollNimbusExperiment
      .testGetValue("events")
      .map(ev => ev.extra.reason),
    ["unknown"]
  );

  // We expect only one event and that that one event reason matches the expected reason
  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment
      .testGetValue("events")
      .map(ev => ev.extra.reason),
    ["unknown"]
  );

  cleanup();
});

/**
 * Normal unenrollment for rollouts:
 * - remove stored enrollment and synced data (prefs)
 * - set rollout inactive in telemetry
 * - send unrollment event
 */

add_task(async function test_remove_rollouts() {
  const { sandbox, manager, cleanup } = await setupTest();
  sandbox.spy(manager.store, "updateExperiment");
  const rollout = ExperimentFakes.rollout("foo");

  await manager.enroll(
    NimbusTestUtils.factories.recipe("foo", { isRollout: true })
  );
  Assert.ok(
    manager.store.updateExperiment.notCalled,
    "Should not have called updateExperiment when enrolling"
  );

  manager.unenroll("foo", { reason: "some-reason" });

  Assert.ok(
    manager.store.updateExperiment.calledOnce,
    "Called to set the rollout as inactive"
  );
  Assert.ok(
    manager.store.updateExperiment.calledWith(rollout.slug, {
      active: false,
      unenrollReason: "some-reason",
    }),
    "Called with expected parameters"
  );

  cleanup();
});

add_task(async function test_unenroll_individualOptOut_statusTelemetry() {
  const { manager, cleanup } = await setupTest();

  await manager.enroll(
    ExperimentFakes.recipe("foo", {
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      branches: [ExperimentFakes.recipe.branches[0]],
    })
  );

  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "nimbus_events.enrollment_status": true,
      },
    })
  );

  manager.unenroll("foo", { reason: "individual-opt-out" });

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("events")
      ?.map(ev => ev.extra),
    [
      {
        slug: "foo",
        branch: "control",
        status: "Disqualified",
        reason: "OptOut",
      },
    ]
  );

  cleanup();
});

add_task(async function testUnenrollBogusReason() {
  const { manager, cleanup } = await setupTest();

  await manager.enroll(
    NimbusTestUtils.factories.recipe("bogus", {
      branches: [NimbusTestUtils.factories.recipe.branches[0]],
    })
  );

  Assert.ok(manager.store.get("bogus").active, "Enrollment active");

  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "nimbus_events.enrollment_status": true,
      },
    })
  );

  manager.unenroll("bogus", "bogus");

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("events")
      ?.map(ev => ev.extra),
    [
      {
        slug: "bogus",
        branch: "control",
        status: "Disqualified",
        reason: "Error",
        error_string: "unknown",
      },
    ]
  );

  Assert.deepEqual(
    Glean.nimbusEvents.unenrollment.testGetValue("events")?.map(ev => ev.extra),
    [
      {
        experiment: "bogus",
        branch: "control",
        reason: "unknown",
      },
    ]
  );

  Assert.deepEqual(
    Glean.normandy.unenrollNimbusExperiment
      .testGetValue("events")
      ?.map(ev => ev.extra),
    [
      {
        value: "bogus",
        branch: "control",
        reason: "unknown",
      },
    ]
  );

  cleanup();
});
