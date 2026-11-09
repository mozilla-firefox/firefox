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
const { JsonSchema } = ChromeUtils.importESModule(
  "resource://gre/modules/JsonSchema.sys.mjs"
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

// See properties.payload in
// https://github.com/mozilla-services/mozilla-pipeline-schemas/blob/main/schemas/telemetry/heartbeat/heartbeat.4.schema.json

const PAYLOAD_SCHEMA = {
  additionalProperties: false,
  anyOf: [
    {
      required: ["closedTS"],
    },
    {
      required: ["windowClosedTS"],
    },
  ],
  properties: {
    closedTS: {
      minimum: 0,
      type: "integer",
    },
    engagedTS: {
      minimum: 0,
      type: "integer",
    },
    expiredTS: {
      minimum: 0,
      type: "integer",
    },
    flowId: {
      pattern:
        "^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$",
      type: "string",
    },
    learnMoreTS: {
      minimum: 0,
      type: "integer",
    },
    offeredTS: {
      minimum: 0,
      type: "integer",
    },
    score: {
      minimum: 1,
      type: "integer",
    },
    surveyId: {
      type: "string",
    },
    surveyVersion: {
      pattern: "^([0-9]+|[a-fA-F0-9]{64})$",
      type: "string",
    },
    testing: {
      type: "boolean",
    },
    version: {
      maximum: 1,
      minimum: 1,
      type: "number",
    },
    votedTS: {
      minimum: 0,
      type: "integer",
    },
    windowClosedTS: {
      minimum: 0,
      type: "integer",
    },
  },
  required: ["version", "flowId", "offeredTS", "surveyId", "surveyVersion"],
  type: "object",
};

function assertSurvey(actual, expected) {
  for (const key of Object.keys(actual)) {
    if (["flowId", "postAnswerUrl", "surveyVersion"].includes(key)) {
      continue;
    }

    Assert.equal(
      actual[key],
      expected[key],
      `Heartbeat should receive correct ${key} parameter`
    );
  }

  Assert.equal(actual.surveyVersion, "1");
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

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["app.normandy.enabled", true],
      ["app.normandy.api_url", "https://example.com/api/v1"],
    ],
  });
});

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

decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testDisabled({ heartbeatClassStub }) {
    const doEnrollmentCleanup = await enrollWithoutAutomaticRun();

    for (const prefs of [
      [["app.normandy.enabled", false]],
      [["app.normandy.api_url", ""]],
      // eslint-disable-next-line sdl/no-insecure-url
      [["app.normandy.api_url", "http://example.com/api/v1"]],
    ]) {
      await SpecialPowers.pushPrefEnv({ set: prefs });
      await LegacyHeartbeatFeature.run();
      await SpecialPowers.popPrefEnv();
    }

    Assert.equal(
      heartbeatClassStub.args.length,
      0,
      "Heartbeat should not be shown when disabled"
    );

    await doEnrollmentCleanup();
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

      const result = JsonSchema.validate(payload, PAYLOAD_SCHEMA);
      Assert.ok(result.valid);
      Assert.equal(payload.surveyVersion, "1");
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
