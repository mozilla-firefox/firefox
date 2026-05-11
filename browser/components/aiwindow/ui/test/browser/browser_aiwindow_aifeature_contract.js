/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PREF_AI_CONTROL_DEFAULT = "browser.ai.control.default";
const PREF_AI_CONTROL_SMARTWINDOW = "browser.ai.control.smartWindow";
const PREF_SMARTWINDOW_CONSENT_TIME = "browser.smartwindow.tos.consentTime";

add_task(async function test_aifeature_contract() {
  Assert.equal(AIWindow.id, "smartWindow", "Smart Window has the expected id");
  Assert.equal(
    AIWindow.hasDistinctEnabledState,
    true,
    "Smart Window has a distinct enabled state"
  );
  Assert.equal(
    AIWindow.canRunOnDevice,
    true,
    "Smart Window can run on this device"
  );

  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_AI_CONTROL_DEFAULT, "available"],
      [PREF_AI_CONTROL_SMARTWINDOW, "default"],
      ["browser.smartwindow.enabled", true],
      [PREF_SMARTWINDOW_CONSENT_TIME, 1770830464],
    ],
  });

  try {
    Assert.equal(
      AIWindow.aiControlState,
      "enabled",
      "Smart Window is enabled after consent"
    );

    Services.prefs.clearUserPref(PREF_SMARTWINDOW_CONSENT_TIME);
    Assert.equal(
      AIWindow.aiControlState,
      "available",
      "Smart Window is available before consent"
    );

    await AIWindow.makeAvailable();
    Assert.ok(
      !Services.prefs.prefHasUserValue(PREF_SMARTWINDOW_CONSENT_TIME),
      "makeAvailable() clears consent"
    );
    Assert.equal(
      AIWindow.aiControlState,
      "available",
      "makeAvailable() restores the available AI Controls state"
    );

    await AIWindow.block();
    Assert.equal(
      AIWindow.aiControlState,
      "blocked",
      "block() sets the blocked state"
    );

    Services.prefs.setIntPref(PREF_SMARTWINDOW_CONSENT_TIME, 1770830464);
    await AIWindow.enable();
    Assert.equal(
      AIWindow.aiControlState,
      "enabled",
      "enable() keeps Smart Window enabled when consent is present"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});
