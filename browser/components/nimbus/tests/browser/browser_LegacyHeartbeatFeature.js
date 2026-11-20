/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Heartbeat } = ChromeUtils.importESModule(
  "moz-src:///browser/components/nimbus/Heartbeat.sys.mjs"
);
const { LegacyHeartbeatFeature } = ChromeUtils.importESModule(
  "moz-src:///browser/components/nimbus/LegacyHeartbeatFeature.sys.mjs"
);

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const TIMER_LAST_UPDATE_PREF = "app.update.lastUpdateTime.legacy-heartbeat";

const SURVEY = {
  surveyId: "a survey",
  message: "test message",
  engagementButtonLabel: "",
  thanksMessage: "thanks!",
  postAnswerUrl: "https://example.com",
  learnMoreMessage: "Learn More",
  learnMoreUrl: "https://example.com",
  repeatOption: "once",
};

function assertSurvey(actual, expected) {
  for (const key of Object.keys(actual)) {
    if (["flowId", "postAnswerUrl"].includes(key)) {
      continue;
    }

    Assert.equal(
      actual[key],
      expected[key],
      `Heartbeat should receive correct ${key} parameter`
    );
  }

  Assert.ok(actual.postAnswerUrl.startsWith(expected.postAnswerUrl));
}

function enroll(value = { survey: SURVEY }) {
  return NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "legacyHeartbeat",
    value,
  });
}

async function enrollWithoutAutomaticRun() {
  const runStub = sinon.stub(LegacyHeartbeatFeature, "run").resolves();

  try {
    return await enroll();
  } finally {
    runStub.restore();
  }
}

decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testLegacyHeartbeatTrigger({ heartbeatClassStub }) {
    const doEnrollmentCleanup = await enrollWithoutAutomaticRun();

    await LegacyHeartbeatFeature.run();
    Assert.equal(
      heartbeatClassStub.args.length,
      1,
      "Heartbeat should be instantiated once"
    );
    assertSurvey(heartbeatClassStub.args[0][1], SURVEY);

    await doEnrollmentCleanup();
  }
);

decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testFeatureUpdateTrigger({ heartbeatClassStub }) {
    Services.prefs.clearUserPref(TIMER_LAST_UPDATE_PREF);

    const doEnrollmentCleanup = await enroll();

    await TestUtils.waitForCondition(
      () => heartbeatClassStub.called,
      "Enrolling should trigger a Heartbeat"
    );
    assertSurvey(heartbeatClassStub.args[0][1], SURVEY);
    await TestUtils.waitForCondition(
      () => Services.prefs.prefHasUserValue(TIMER_LAST_UPDATE_PREF),
      "A run outside the timer should update timer's last update time"
    );

    await doEnrollmentCleanup();
    Services.prefs.clearUserPref(TIMER_LAST_UPDATE_PREF);
  }
);

decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testTimerRunDoesNotUpdateLastUpdateTime({
    heartbeatClassStub,
  }) {
    Services.prefs.clearUserPref(TIMER_LAST_UPDATE_PREF);
    const doEnrollmentCleanup = await enrollWithoutAutomaticRun();

    await LegacyHeartbeatFeature.run({ trigger: "timer" });
    Assert.equal(heartbeatClassStub.args.length, 1, "Heartbeat should show");
    Assert.ok(
      !Services.prefs.prefHasUserValue(TIMER_LAST_UPDATE_PREF),
      "A run triggered by timer should not update timer's last update time"
    );

    await doEnrollmentCleanup();
  }
);

decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testNoEnrollment({ heartbeatClassStub }) {
    await LegacyHeartbeatFeature.run();
    Assert.equal(
      heartbeatClassStub.args.length,
      0,
      "Heartbeat should not be shown without an enrollment"
    );
  }
);

add_task(async function testRunIntervalPrefChange() {
  const setTimerStub = sinon.stub(LegacyHeartbeatFeature, "setTimer");
  try {
    await SpecialPowers.pushPrefEnv({
      set: [["app.normandy.run_interval_seconds", 60]],
    });
    Assert.ok(
      setTimerStub.calledOnce,
      "Changing the run interval should re-register the timer"
    );
    await SpecialPowers.popPrefEnv();
  } finally {
    setTimerStub.restore();
  }
});

decorate_task(
  withClearStorage(),
  async function testLegacyHeartbeatPingPayload() {
    const doEnrollmentCleanup = await enrollWithoutAutomaticRun();

    // Override Heartbeat so we can get the instance and manipulate it directly.
    const heartbeatDeferred = Promise.withResolvers();
    class TestHeartbeat extends Heartbeat {
      constructor(...args) {
        super(...args);
        heartbeatDeferred.resolve(this);
      }
    }
    HeartbeatSurvey.overrideHeartbeatForTests(TestHeartbeat);

    try {
      await LegacyHeartbeatFeature.run();
      const heartbeat = await heartbeatDeferred.promise;
      // We are going to simulate the timer timing out, so we do not want it to
      // *actually* time out.
      heartbeat.endTimerIfPresent("surveyEndTimer");
      const notice = await heartbeat.noticePromise;
      await notice.updateComplete;

      const telemetrySentPromise = heartbeat.eventEmitter.once("TelemetrySent");

      // This method would be triggered when the timer timed out. This will
      // trigger telemetry to be submitted.
      await heartbeat.close();

      const payload = await telemetrySentPromise;

      Assert.equal(payload.surveyId, SURVEY.surveyId);
      Assert.equal(payload.flowId, heartbeat.options.flowId);
      Assert.equal(typeof payload.offeredTS, "number");
      Assert.equal(typeof payload.closedTS, "number");
    } finally {
      HeartbeatSurvey.overrideHeartbeatForTests();
      await doEnrollmentCleanup();
    }
  }
);

decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testExistingEnrollmentAtStartup({ heartbeatClassStub }) {
    const doEnrollmentCleanup = await enrollWithoutAutomaticRun();

    await LegacyHeartbeatFeature.uninit();

    const setTimerStub = sinon.stub(LegacyHeartbeatFeature, "setTimer");

    try {
      Assert.equal(
        heartbeatClassStub.callCount,
        0,
        "Heartbeat should not be shown before reinitialization"
      );
      LegacyHeartbeatFeature.init();

      await TestUtils.waitForCondition(
        () => heartbeatClassStub.called,
        "An existing enrollment should trigger Heartbeat at startup"
      );

      assertSurvey(heartbeatClassStub.args[0][1], SURVEY);
    } finally {
      setTimerStub.restore();
      LegacyHeartbeatFeature.setTimer();
      await doEnrollmentCleanup();
    }
  }
);
