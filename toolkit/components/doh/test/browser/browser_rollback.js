/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

requestLongerTimeout(2);

add_task(setup);

add_task(async function testRollback() {
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

  // Rollback!
  setPassingHeuristics();
  Preferences.reset(prefs.ENABLED_PREF);
  await waitForStateTelemetry(["shutdown", "rollback"]);
  await ensureTRRMode(undefined);
  ensureNoTRRSelectionTelemetry();
  await ensureNoHeuristicsTelemetry();
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  await ensureNoHeuristicsTelemetry();

  // Re-enable.
  Preferences.set(prefs.ENABLED_PREF, true);

  await ensureTRRMode(2);
  ensureNoTRRSelectionTelemetry();
  await checkHeuristicsTelemetry("enable_doh", "startup");

  // Change the environment to failing and simulate a network change.
  setFailingHeuristics();
  simulateNetworkChange();
  await ensureTRRMode(0);
  await checkHeuristicsTelemetry("disable_doh", "netchange");

  // Rollback again for good measure! This time with failing heuristics.
  Preferences.reset(prefs.ENABLED_PREF);
  await waitForStateTelemetry(["shutdown", "rollback"]);
  await ensureTRRMode(undefined);
  ensureNoTRRSelectionTelemetry();
  await ensureNoHeuristicsTelemetry();
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  await ensureNoHeuristicsTelemetry();

  // Re-enable.
  Preferences.set(prefs.ENABLED_PREF, true);

  await ensureTRRMode(0);
  ensureNoTRRSelectionTelemetry();
  await checkHeuristicsTelemetry("disable_doh", "startup");

  // Change the environment to passing and simulate a network change.
  setPassingHeuristics();
  simulateNetworkChange();
  await ensureTRRMode(2);
  await checkHeuristicsTelemetry("enable_doh", "netchange");

  // Rollback again, this time with TRR mode set to 2 prior to doing so.
  Preferences.reset(prefs.ENABLED_PREF);
  await waitForStateTelemetry(["shutdown", "rollback"]);
  await ensureTRRMode(undefined);
  ensureNoTRRSelectionTelemetry();
  await ensureNoHeuristicsTelemetry();
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  await ensureNoHeuristicsTelemetry();

  // Re-enable.
  Preferences.set(prefs.ENABLED_PREF, true);

  await ensureTRRMode(2);
  ensureNoTRRSelectionTelemetry();
  await checkHeuristicsTelemetry("enable_doh", "startup");
  simulateNetworkChange();
  await ensureNoTRRModeChange(2);
  await checkHeuristicsTelemetry("enable_doh", "netchange");

  // Rollback again. This time, uninit DoHController first to ensure it reacts
  // correctly at startup.
  await DoHController._uninit();
  await waitForStateTelemetry(["shutdown"]);
  Preferences.reset(prefs.ENABLED_PREF);
  await DoHController.init();
  await ensureTRRMode(undefined);
  ensureNoTRRSelectionTelemetry();
  await ensureNoHeuristicsTelemetry();
  await waitForStateTelemetry(["rollback"]);
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  await ensureNoHeuristicsTelemetry();
});
