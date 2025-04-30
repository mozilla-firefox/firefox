/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

add_task(async function test_TRRRacer_cleanRun() {
  Services.fog.testResetFOG();
  let deferred = Promise.withResolvers();
  let racer = new TRRRacer(() => {
    deferred.resolve();
    deferred.resolved = true;
  }, trrList);
  racer.run();

  await deferred.promise;
  Assert.equal(racer._retryCount, 1);

  let events =
    await Glean.securityDohTrrPerformance.resolvedRecord.testGetValue();
  events = events.filter(e => e.category == "security.doh.trr_performance");
  Assert.equal(events.length, racer._aggregator.totalLookups);

  Services.fog.testResetFOG();

  // Simulate network changes and ensure no re-runs since it's already complete.
  async function testNetworkChange(captivePortal = false) {
    if (captivePortal) {
      Services.obs.notifyObservers(null, "captive-portal-login");
    } else {
      Services.obs.notifyObservers(null, "network:link-status-changed", "down");
    }

    Assert.ok(!racer._aggregator.aborted);

    if (captivePortal) {
      Services.obs.notifyObservers(null, "captive-portal-login-success");
    } else {
      Services.obs.notifyObservers(null, "network:link-status-changed", "up");
    }

    Assert.equal(racer._retryCount, 1);
    await ensureNoTelemetry();

    if (captivePortal) {
      Services.obs.notifyObservers(null, "captive-portal-login-abort");
    }
  }

  testNetworkChange(false);
  testNetworkChange(true);
});

async function test_TRRRacer_networkFlux_helper(captivePortal = false) {
  Services.fog.testResetFOG();
  let deferred = Promise.withResolvers();
  let racer = new TRRRacer(() => {
    deferred.resolve();
    deferred.resolved = true;
  }, trrList);
  racer.run();

  if (captivePortal) {
    Services.obs.notifyObservers(null, "captive-portal-login");
  } else {
    Services.obs.notifyObservers(null, "network:link-status-changed", "down");
  }

  Assert.ok(racer._aggregator.aborted);
  await ensureNoTelemetry();
  Assert.equal(racer._retryCount, 1);
  Assert.ok(!deferred.resolved);

  if (captivePortal) {
    Services.obs.notifyObservers(null, "captive-portal-login-success");
  } else {
    Services.obs.notifyObservers(null, "network:link-status-changed", "up");
  }

  Assert.ok(!racer._aggregator.aborted);
  await deferred.promise;

  Assert.equal(racer._retryCount, 2);

  let events =
    await Glean.securityDohTrrPerformance.resolvedRecord.testGetValue();
  events = events.filter(e => e.category == "security.doh.trr_performance");
  Assert.equal(events.length, racer._aggregator.totalLookups);

  Services.fog.testResetFOG();
  if (captivePortal) {
    Services.obs.notifyObservers(null, "captive-portal-login-abort");
  }
}

add_task(async function test_TRRRacer_networkFlux() {
  await test_TRRRacer_networkFlux_helper(false);
  await test_TRRRacer_networkFlux_helper(true);
});

async function test_TRRRacer_maxRetries_helper(captivePortal = false) {
  let deferred = Promise.withResolvers();
  let racer = new TRRRacer(() => {
    deferred.resolve();
    deferred.resolved = true;
  }, trrList);
  racer.run();
  info("ran new racer");
  // Start at i = 1 since we're already at retry #1.
  for (let i = 1; i < 5; ++i) {
    if (captivePortal) {
      Services.obs.notifyObservers(null, "captive-portal-login");
    } else {
      Services.obs.notifyObservers(null, "network:link-status-changed", "down");
    }

    info("notified observers");

    Assert.ok(racer._aggregator.aborted);
    await ensureNoTelemetry();
    Assert.equal(racer._retryCount, i);
    Assert.ok(!deferred.resolved);

    if (captivePortal) {
      Services.obs.notifyObservers(null, "captive-portal-login-success");
    } else {
      Services.obs.notifyObservers(null, "network:link-status-changed", "up");
    }
  }

  // Simulate a "down" network event and ensure we still send telemetry
  // since we've maxed out our retry count.
  if (captivePortal) {
    Services.obs.notifyObservers(null, "captive-portal-login");
  } else {
    Services.obs.notifyObservers(null, "network:link-status-changed", "down");
  }
  Assert.ok(!racer._aggregator.aborted);
  await deferred.promise;
  Assert.equal(racer._retryCount, 5);

  let events =
    await Glean.securityDohTrrPerformance.resolvedRecord.testGetValue();
  events = events.filter(e => e.category == "security.doh.trr_performance");
  Assert.equal(events.length, racer._aggregator.totalLookups);

  Services.fog.testResetFOG();
  if (captivePortal) {
    Services.obs.notifyObservers(null, "captive-portal-login-abort");
  }
}

add_task(async function test_TRRRacer_maxRetries() {
  await test_TRRRacer_maxRetries_helper(false);
  await test_TRRRacer_maxRetries_helper(true);
});

add_task(async function test_TRRRacer_getFastestTRRFromResults() {
  let results = [
    { trr: "trr1", time: 10 },
    { trr: "trr2", time: 100 },
    { trr: "trr1", time: 1000 },
    { trr: "trr2", time: 110 },
    { trr: "trr3", time: -1 },
    { trr: "trr4", time: -1 },
    { trr: "trr4", time: -1 },
    { trr: "trr4", time: 1 },
    { trr: "trr4", time: 1 },
    { trr: "trr5", time: 10 },
    { trr: "trr5", time: 20 },
    { trr: "trr5", time: 1000 },
  ];
  let racer = new TRRRacer(undefined, trrList);
  let fastest = racer._getFastestTRRFromResults(results);
  // trr1's geometric mean is 100
  // trr2's geometric mean is 110
  // trr3 has no valid times, excluded
  // trr4 has 50% invalid times, excluded
  // trr5's geometric mean is ~58.5, it's the winner.
  Assert.equal(fastest, "trr5");

  // When no valid entries are available, undefined is the default output.
  results = [
    { trr: "trr1", time: -1 },
    { trr: "trr2", time: -1 },
  ];

  fastest = racer._getFastestTRRFromResults(results);
  Assert.equal(fastest, undefined);

  // When passing `returnRandomDefault = true`, verify that both TRRs are
  // possible outputs. The probability that the randomization is working
  // correctly and we consistently get the same output after 50 iterations is
  // 0.5^50 ~= 8.9*10^-16.
  let firstResult = racer._getFastestTRRFromResults(results, true);
  while (racer._getFastestTRRFromResults(results, true) == firstResult) {
    continue;
  }
  Assert.ok(true, "Both TRRs were possible outputs when all results invalid.");
});
