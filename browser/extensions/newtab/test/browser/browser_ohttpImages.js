/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

add_task(async function test_nimbus_experiment_enabled() {
  const onActionSpy = sinon.spy(DiscoveryStreamFeed.prototype, "onAction");

  await ExperimentAPI.ready();
  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "newtabOhttpImages",
    value: { enabled: true },
  });

  // Wait for the specific expected broadcast to happen
  await TestUtils.waitForCondition(() => {
    return onActionSpy
      .getCalls()
      .some(call => call.args[0]?.data?.name === "ohttpImagesConfig");
  }, "Wait for ohttpImagesConfig config onAction");

  // Find our specific action
  const matchingCall = onActionSpy
    .getCalls()
    .find(call => call.args[0]?.data?.name === "ohttpImagesConfig");

  ok(matchingCall, "Found Nimbus config action");

  is(
    matchingCall.args[0].data.name,
    "ohttpImagesConfig",
    "Nimbus config update happened"
  );

  ok(matchingCall.args[0].data.value.enabled, "Nimbus value update happened");

  await doExperimentCleanup();
  onActionSpy.restore();
});
