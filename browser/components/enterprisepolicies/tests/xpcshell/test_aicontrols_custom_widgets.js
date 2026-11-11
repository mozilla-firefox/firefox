/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const CONTROL_PREF = "browser.ai.control.generatedWidgets";
const LABS_PREF = "browser.widgets.generated.enabled";

function reset() {
  Services.prefs.unlockPref(CONTROL_PREF);
  Services.prefs.unlockPref(LABS_PREF);
  const defaults = Services.prefs.getDefaultBranch("");
  defaults.setStringPref(CONTROL_PREF, "default");
  defaults.setBoolPref(LABS_PREF, false);
  Services.prefs.clearUserPref(CONTROL_PREF);
  Services.prefs.clearUserPref(LABS_PREF);
}

registerCleanupFunction(reset);

add_task(async function test_available_never_turns_on_custom_widgets() {
  reset();
  Services.prefs.setBoolPref(LABS_PREF, true);

  await setupPolicyEngineWithJson({
    policies: {
      AIControls: {
        Default: { Value: "available", Locked: true },
      },
    },
  });

  checkLockedPref(CONTROL_PREF, "available");
  checkUnlockedPref(LABS_PREF, true);
  Services.prefs.clearUserPref(LABS_PREF);
  checkUnlockedPref(LABS_PREF, false);
});

add_task(async function test_a_locked_default_block_leaves_labs_alone() {
  reset();
  Services.prefs.setBoolPref(LABS_PREF, true);

  await setupPolicyEngineWithJson({
    policies: {
      AIControls: {
        Default: { Value: "blocked", Locked: true },
      },
    },
  });

  checkLockedPref(CONTROL_PREF, "blocked");
  checkUnlockedPref(LABS_PREF, true);
});

add_task(async function test_a_locked_feature_block_leaves_labs_alone() {
  reset();
  Services.prefs.setBoolPref(LABS_PREF, true);

  await setupPolicyEngineWithJson({
    policies: {
      AIControls: {
        Default: { Value: "available" },
        CustomWidgets: { Value: "blocked", Locked: true },
      },
    },
  });

  checkLockedPref(CONTROL_PREF, "blocked");
  checkUnlockedPref(LABS_PREF, true);
});

add_task(async function test_an_available_override_leaves_labs_alone() {
  reset();
  Services.prefs.setBoolPref(LABS_PREF, true);

  await setupPolicyEngineWithJson({
    policies: {
      AIControls: {
        Default: { Value: "blocked", Locked: true },
        CustomWidgets: { Value: "available", Locked: false },
      },
    },
  });

  checkUnlockedPref(CONTROL_PREF, "available");
  checkUnlockedPref(LABS_PREF, true);
});
