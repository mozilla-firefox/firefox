/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

add_task(async function testDoorhangerUserReject() {
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

  await ensureTRRMode(2);
  await checkHeuristicsTelemetry("enable_doh", "startup");

  checkScalars([
    ["networking.doh_heuristics_attempts", { value: 1 }],
    ["networking.doh_heuristics_pass_count", { value: 1 }],
    ["networking.doh_heuristics_result", { value: Heuristics.Telemetry.pass }],
    // All of the heuristics must be false.
    falseExpectations([]),
  ]);

  prefPromise = TestUtils.waitForPrefChange(
    prefs.DOORHANGER_USER_DECISION_PREF
  );

  // Click the doorhanger's "reject" button.
  let button = panel.querySelector(".popup-notification-secondary-button");
  promise = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  EventUtils.synthesizeMouseAtCenter(button, {});
  await promise;

  await prefPromise;

  is(
    Preferences.get(prefs.DOORHANGER_USER_DECISION_PREF),
    "UIDisabled",
    "Doorhanger decision saved."
  );

  BrowserTestUtils.removeTab(tab);

  await ensureTRRMode(undefined);
  ensureNoHeuristicsTelemetry();
  is(Preferences.get(prefs.BREADCRUMB_PREF), undefined, "Breadcrumb cleared.");

  checkScalars([
    ["networking.doh_heuristics_attempts", { value: 1 }],
    ["networking.doh_heuristics_pass_count", { value: 1 }],
    [
      "networking.doh_heuristics_result",
      { value: Heuristics.Telemetry.optOut },
    ],
    // All of the heuristics must be false.
    falseExpectations([]),
  ]);

  // Simulate a network change.
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();

  // Restart the controller for good measure.
  await restartDoHController();
  ensureNoTRRSelectionTelemetry();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();

  // Set failing environment and trigger another network change.
  setFailingHeuristics();
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();
});
