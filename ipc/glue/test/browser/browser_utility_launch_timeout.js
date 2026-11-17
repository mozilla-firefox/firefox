/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Several tasks wait out a launch timeout.
requestLongerTimeout(2);

// A process that is already running would be reused instead of launched.
add_setup(async function ensureNoExistingProcess() {
  await killUtilityProcesses();
});

// Launches with prefs set, which must get the launch abandoned, then checks
// that the next launch succeeds.
async function testAbandonedLaunchRecovers(prefs) {
  await SpecialPowers.pushPrefEnv({ set: prefs });

  await Assert.rejects(
    startUtilityProcess(),
    /NS_ERROR_FAILURE/,
    "An abandoned launch rejects instead of hanging"
  );

  await TestUtils.waitForCondition(
    async () => !(await getUtilityProcesses(undefined, { quiet: true })).length,
    "No utility process is left behind by the abandoned launch"
  );

  await SpecialPowers.popPrefEnv();

  const pid = await startUtilityProcess([kGenericUtilityActor]);
  Assert.greater(pid, 0, "The next launch succeeds after an abandoned one");

  await cleanUtilityProcessShutdown(kGenericUtilityActor);
}

add_task(async function test_launch_abandoned_before_connecting() {
  await testAbandonedLaunchRecovers([
    // The launch can never complete, however the timeout is scheduled.
    ["dom.ipc.utilityProcess.testing.skipInitCompleted", true],
    // Shorter than any real launch, so the child normally has not connected
    // yet. Either way, the launch must be abandoned.
    ["dom.ipc.utilityProcess.startup_timeout_ms", 1],
  ]);
});

add_task(async function test_launch_abandoned_after_connecting() {
  await testAbandonedLaunchRecovers([
    ["dom.ipc.utilityProcess.testing.skipInitCompleted", true],
    // Normally leaves the child time to connect before the launch is
    // abandoned. Either way, the launch must be abandoned.
    ["dom.ipc.utilityProcess.startup_timeout_ms", 1000],
  ]);
});

add_task(async function test_launch_ended_before_timeout() {
  const timeoutMs = 1000;
  await SpecialPowers.pushPrefEnv({
    set: [["dom.ipc.utilityProcess.startup_timeout_ms", timeoutMs]],
  });

  const launch = startUtilityProcess();
  utilityProcessTest().stopProcess();
  await Assert.rejects(
    launch,
    /NS_ERROR_FAILURE/,
    "A launch shut down before its timeout rejects"
  );

  await SpecialPowers.popPrefEnv();

  const pid = await startUtilityProcess([kGenericUtilityActor]);
  Assert.greater(pid, 0, "The next launch succeeds");

  // Lets the timeout of the launch that was shut down expire.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 2 * timeoutMs));

  Assert.equal(
    await tryGetUtilityPid(kGenericUtilityActor),
    pid,
    "The expired timeout leaves the next process alone"
  );

  await cleanUtilityProcessShutdown(kGenericUtilityActor);
});

add_task(async function test_launch_outlives_its_timeout() {
  // Left at the production default, so no launch that is valid in production
  // fails here.
  const timeoutMs = Services.prefs.getIntPref(
    "dom.ipc.utilityProcess.startup_timeout_ms"
  );

  const pid = await startUtilityProcess([kGenericUtilityActor]);
  Assert.greater(pid, 0, "The launch completes within its timeout");

  // Lets the timeout of the completed launch expire.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, timeoutMs));

  Assert.equal(
    await tryGetUtilityPid(kGenericUtilityActor),
    pid,
    "The expired timeout leaves the launched process alone"
  );

  await cleanUtilityProcessShutdown(kGenericUtilityActor);
});
