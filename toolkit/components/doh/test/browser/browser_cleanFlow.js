/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

add_task(async function testCleanFlow() {
  // Set up a passing environment and enable DoH.
  setPassingHeuristics();
  let promise = waitForDoorhanger();
  let prefPromise = TestUtils.waitForPrefChange(prefs.BREADCRUMB_PREF);
  Preferences.set(prefs.ENABLED_PREF, true);

  await prefPromise;
  is(Preferences.get(prefs.BREADCRUMB_PREF), true, "Breadcrumb saved.");
  is(
    Preferences.get(prefs.TRR_SELECT_URI_PREF),
    "https://example.com/dns-query",
    "TRR selection complete."
  );
  await checkTRRSelectionTelemetry();

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, EXAMPLE_URL);
  let panel = await promise;

  prefPromise = TestUtils.waitForPrefChange(
    prefs.DOORHANGER_USER_DECISION_PREF
  );

  // Click the doorhanger's "accept" button.
  let button = panel.querySelector(".popup-notification-primary-button");
  promise = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  EventUtils.synthesizeMouseAtCenter(button, {});
  await promise;

  await ensureTRRMode(2);
  await checkHeuristicsTelemetry("enable_doh", "startup");

  await prefPromise;
  is(
    Preferences.get(prefs.DOORHANGER_USER_DECISION_PREF),
    "UIOk",
    "Doorhanger decision saved."
  );
  is(Preferences.get(prefs.BREADCRUMB_PREF), true, "Breadcrumb not cleared.");

  BrowserTestUtils.removeTab(tab);

  // Change the environment to failing and simulate a network change.
  setFailingHeuristics();
  simulateNetworkChange();
  await ensureTRRMode(0);
  await checkHeuristicsTelemetry("disable_doh", "netchange");

  // Trigger another network change.
  simulateNetworkChange();
  await ensureNoTRRModeChange(0);
  await checkHeuristicsTelemetry("disable_doh", "netchange");

  // Restart the controller for good measure.
  await restartDoHController();
  ensureNoTRRSelectionTelemetry();
  // The mode technically changes from undefined/empty to 0 here.
  await ensureTRRMode(0);
  await checkHeuristicsTelemetry("disable_doh", "startup");

  // Set a passing environment and simulate a network change.
  setPassingHeuristics();
  simulateNetworkChange();
  await ensureTRRMode(2);
  await checkHeuristicsTelemetry("enable_doh", "netchange");

  // Again, repeat and check nothing changed.
  simulateNetworkChange();
  await ensureNoTRRModeChange(2);
  await checkHeuristicsTelemetry("enable_doh", "netchange");

  // Test the clearModeOnShutdown pref. `restartDoHController` does the actual
  // test for us between shutdown and startup.
  Preferences.set(prefs.CLEAR_ON_SHUTDOWN_PREF, false);
  await restartDoHController();
  ensureNoTRRSelectionTelemetry();
  await ensureNoTRRModeChange(2);
  await checkHeuristicsTelemetry("enable_doh", "startup");
  Preferences.set(prefs.CLEAR_ON_SHUTDOWN_PREF, true);
});
