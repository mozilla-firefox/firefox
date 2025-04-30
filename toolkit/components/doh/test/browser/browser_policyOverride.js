/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);

add_task(async function testPolicyOverride() {
  // Set up an arbitrary enterprise policy. Its existence should be sufficient
  // to disable heuristics.
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {
      EnableTrackingProtection: {
        Value: true,
      },
    },
  });
  is(
    Services.policies.status,
    Ci.nsIEnterprisePolicies.ACTIVE,
    "Policy engine is active."
  );

  Preferences.set(prefs.ENABLED_PREF, true);
  await waitForStateTelemetry(["shutdown", "policyDisabled"]);
  is(
    Preferences.get(prefs.BREADCRUMB_PREF),
    undefined,
    "Breadcrumb not saved."
  );
  is(
    Preferences.get(prefs.TRR_SELECT_URI_PREF),
    undefined,
    "TRR selection not performed."
  );
  is(
    Preferences.get(prefs.SKIP_HEURISTICS_PREF),
    true,
    "Pref set to suppress CFR."
  );
  ensureNoTRRSelectionTelemetry();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();

  checkScalars(
    [
      [
        "networking.doh_heuristics_result",
        { value: Heuristics.Telemetry.enterprisePresent },
      ],
      // All of the heuristics must be false.
    ].concat(falseExpectations([]))
  );

  // Simulate a network change.
  simulateNetworkChange();
  await ensureNoTRRModeChange(undefined);
  ensureNoHeuristicsTelemetry();

  // Clean up.
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {},
  });
  EnterprisePolicyTesting.resetRunOnceState();

  is(
    Services.policies.status,
    Ci.nsIEnterprisePolicies.INACTIVE,
    "Policy engine is inactive at the end of the test"
  );
});
