/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_setup(function () {
  // We don't bother resetting this pref after the test because there is a
  // single test per-process.
  Services.prefs.setBoolPref("nimbus.telemetry.targetingContextEnabled", true);
});

add_task(async function test() {
  const { sandbox, loader, cleanup } = await NimbusTestUtils.setupTest();

  Services.prefs.setBoolPref("nimbus.firstUpdateComplete", false);
  loader.remoteSettingsClients.experiments.get.resetHistory();

  // Make recordTargetingContext() never resolve. The returned promise will
  // resolve once it is blocked, allowing us to test #raceShutdown().
  const blocker = promiseRecordTargetingContextBlocks(sandbox);
  const updatePromise = loader.updateRecipes("test");

  await blocker;

  // Advance shutdown, which should trip #raceShutdown() and cause
  // updateRecipes() to reject with a ShutdownStartedError.
  Services.startup.advanceShutdownPhase(
    Services.startup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
  );

  await Assert.rejects(updatePromise, /Shutdown started/);

  Assert.ok(
    loader.remoteSettingsClients.experiments.get.notCalled,
    "Did not progress to fetching recipes"
  );

  Assert.equal(
    Services.prefs.getBoolPref("nimbus.firstUpdateComplete"),
    false,
    "Update was not completed"
  );

  await cleanup();
});
