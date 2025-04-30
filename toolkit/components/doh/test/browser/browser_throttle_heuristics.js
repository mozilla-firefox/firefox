/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

add_task(async function testHeuristicsThrottling() {
  // Use a zero throttle timeout for the test. This both ensures the test has a
  // short runtime as well as preventing intermittents because we thought
  // something was deterministic when it wasn't.
  let throttleTimeout = 0;
  let rateLimit = 1;
  let throttleDoneTopic = "doh:heuristics-throttle-done";
  let throttleExtendTopic = "doh:heuristics-throttle-extend";

  Preferences.set(prefs.HEURISTICS_THROTTLE_TIMEOUT_PREF, throttleTimeout);
  Preferences.set(prefs.HEURISTICS_THROTTLE_RATE_LIMIT_PREF, rateLimit);

  // Set up a passing environment and enable DoH.
  let throttledPromise = TestUtils.topicObserved(throttleDoneTopic);
  setPassingHeuristics();
  let prefPromise = TestUtils.waitForPrefChange(prefs.BREADCRUMB_PREF);
  Preferences.set(prefs.ENABLED_PREF, true);

  await prefPromise;
  is(Preferences.get(prefs.BREADCRUMB_PREF), true, "Breadcrumb saved.");
  await ensureTRRMode(2);
  info("waiting for throttle done");
  await throttledPromise;
  await checkHeuristicsTelemetry("enable_doh", "startup");

  // Change the environment to failing and simulate a network change.
  throttledPromise = TestUtils.topicObserved(throttleDoneTopic);
  simulateNetworkChange();
  info("waiting for throttle done");
  await throttledPromise;
  await checkHeuristicsTelemetry("enable_doh", "netchange");

  /* Simulate two consecutive network changes and check that we throttled the
   * second heuristics run. */

  // We wait for the throttle timer to fire twice - the first time, a fresh
  // heuristics run will be performed since it was queued while throttling.
  // This triggers another throttle timeout which is the second one we wait for.
  throttledPromise = TestUtils.topicObserved(throttleDoneTopic).then(() =>
    TestUtils.topicObserved(throttleDoneTopic)
  );
  simulateNetworkChange();
  simulateNetworkChange();
  info("waiting for throttle done");
  await throttledPromise;
  await checkHeuristicsTelemetryMultiple(["netchange", "throttled"]);

  /* Simulate several consecutive network changes at a rate that exceeds the
   * rate limit and check that we only record two heuristics runs in total -
   * one for the initial netchange and one throttled run at the end. */

  // We wait for the throttle timer to be extended twice.
  let throttleExtendPromise = TestUtils.topicObserved(throttleExtendTopic);
  let throttleExtendPromise2 = throttleExtendPromise.then(() =>
    TestUtils.topicObserved(throttleExtendTopic)
  );

  // Again, we wait for the timer to fire twice - once for the volatile period
  // which results in a throttled heuristics run, and once after it with no run.
  throttledPromise = throttleExtendPromise2
    .then(() => TestUtils.topicObserved(throttleDoneTopic))
    .then(() => TestUtils.topicObserved(throttleDoneTopic));

  // Simulate three network changes:
  // - The first one starts the throttle timer
  // - The second one is within the limit of 1.
  // - The third one exceeds the limit and extends the throttle period.
  simulateNetworkChange();
  simulateNetworkChange();
  simulateNetworkChange();

  // First throttle extension should happen now.
  info("waiting for throttle extend");
  await throttleExtendPromise;

  // Two more network changes to once again extend the throttle period.
  simulateNetworkChange();
  simulateNetworkChange();

  // Now the second extension should be detected.
  info("waiting for throttle done");
  await throttleExtendPromise2;

  // Finally, we wait for the throttle period to finish.
  info("waiting for throttle done");
  await throttledPromise;

  await checkHeuristicsTelemetryMultiple(["netchange", "throttled"]);
});
