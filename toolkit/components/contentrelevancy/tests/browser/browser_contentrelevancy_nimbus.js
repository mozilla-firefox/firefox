/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { ContentRelevancyManager } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contentrelevancy/ContentRelevancyManager.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { TimerManager } = ChromeUtils.importESModule(
  "resource://gre/modules/UpdateTimerManager.sys.mjs"
);

let gSandbox;

add_setup(() => {
  gSandbox = sinon.createSandbox();

  registerCleanupFunction(() => {
    gSandbox.restore();
  });
});

/**
 * Test Nimbus integration - enable.
 */
add_task(async function test_NimbusIntegration_enable() {
  gSandbox.spy(ContentRelevancyManager, "notify");
  gSandbox.spy(TimerManager.prototype, "registerTimer");

  await ExperimentAPI.ready();
  const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "contentRelevancy",
    value: {
      enabled: true,
      minInputUrls: 1,
      maxInputUrls: 3,
      // Set the timer interval to 0 will trigger the timer right away.
      timerInterval: 0,
      ingestEnabled: false,
    },
  });

  Assert.ok(ContentRelevancyManager.shouldEnable, "Should enable via Nimbus");
  Assert.ok(
    TimerManager.prototype.registerTimer.calledWith(
      ContentRelevancyManager.TIMER_ID
    ),
    "Should register timer when enabled"
  );

  await TestUtils.waitForCondition(
    () => ContentRelevancyManager.notify.called,
    "The timer callback should be called"
  );

  await doExperimentCleanup();
  gSandbox.restore();
});

/**
 * Test Nimbus integration - disable.
 */
add_task(async function test_NimbusIntegration_disable() {
  gSandbox.spy(TimerManager.prototype, "registerTimer");

  await ExperimentAPI.ready();
  const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "contentRelevancy",
    value: {
      enabled: false,
      minInputUrls: 1,
      maxInputUrls: 3,
      // Set the timer interval to 0 will trigger the timer right away.
      timerInterval: 0,
      ingestEnabled: false,
    },
  });

  Assert.ok(!ContentRelevancyManager.shouldEnable, "Should disable via Nimbus");
  Assert.ok(
    !TimerManager.prototype.registerTimer.calledWith(
      ContentRelevancyManager.TIMER_ID
    ),
    "Should not register timer"
  );

  await doExperimentCleanup();
  gSandbox.restore();
});
