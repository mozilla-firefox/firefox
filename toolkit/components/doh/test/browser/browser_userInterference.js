/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

add_task(async function testUserInterference() {
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
  await prefPromise;

  is(
    Preferences.get(prefs.DOORHANGER_USER_DECISION_PREF),
    "UIOk",
    "Doorhanger decision saved."
  );

  BrowserTestUtils.removeTab(tab);

  await ensureTRRMode(2);
  await checkHeuristicsTelemetry("enable_doh", "startup");

  // Set the TRR mode pref manually and ensure we respect this.
  Preferences.set(prefs.NETWORK_TRR_MODE_PREF, 3);
  await ensureTRRMode(undefined);

  // Simulate a network change.
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();

  is(
    Preferences.get(prefs.DISABLED_PREF, false),
    true,
    "Manual disable recorded."
  );
  is(Preferences.get(prefs.BREADCRUMB_PREF), undefined, "Breadcrumb cleared.");

  // Simulate another network change.
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();

  // Restart the controller for good measure.
  await restartDoHController();
  await ensureNoTRRModeChange(undefined);
  ensureNoTRRSelectionTelemetry();
  ensureNoHeuristicsTelemetry();

  // Simulate another network change.
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();
});
