/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Sandboxes compute their reduce timer precision caller type when they are
// created. Make sure it is refreshed when the RFP prefs change afterwards.

const PRECISION_MS = 100;

function sandboxTimeIsRounded(sandbox) {
  const values = [];
  let last = -1;
  while (values.length < 5) {
    const now = Cu.evalInSandbox("Date.now()", sandbox);
    if (now !== last) {
      values.push(now);
      last = now;
    }
  }
  return values.every(v => v % PRECISION_MS === 0);
}

add_setup(() => {
  Services.prefs.setBoolPref("privacy.reduceTimerPrecision", false);
  Services.prefs.setIntPref(
    "privacy.resistFingerprinting.reduceTimerPrecision.microseconds",
    PRECISION_MS * 1000
  );
  Services.prefs.setBoolPref("privacy.resistFingerprinting.pbmode", false);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("privacy.reduceTimerPrecision");
    Services.prefs.clearUserPref(
      "privacy.resistFingerprinting.reduceTimerPrecision.microseconds"
    );
    Services.prefs.clearUserPref("privacy.resistFingerprinting.pbmode");
    Services.prefs.clearUserPref("privacy.resistFingerprinting");
  });
});

add_task(function test_enable_rfp_after_sandbox_creation() {
  Services.prefs.setBoolPref("privacy.resistFingerprinting", false);
  const sandbox = Cu.Sandbox("https://example.com");
  Assert.ok(
    !sandboxTimeIsRounded(sandbox),
    "Sandbox time is not rounded without RFP"
  );

  Services.prefs.setBoolPref("privacy.resistFingerprinting", true);
  Assert.ok(
    sandboxTimeIsRounded(sandbox),
    "Sandbox time is rounded after enabling RFP"
  );
});

add_task(function test_disable_rfp_after_sandbox_creation() {
  Services.prefs.setBoolPref("privacy.resistFingerprinting", true);
  const sandbox = Cu.Sandbox("https://example.com");
  Assert.ok(sandboxTimeIsRounded(sandbox), "Sandbox time is rounded with RFP");

  Services.prefs.setBoolPref("privacy.resistFingerprinting", false);
  Assert.ok(
    !sandboxTimeIsRounded(sandbox),
    "Sandbox time is not rounded after disabling RFP"
  );
});
