/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ProfileDataUpgrader } = ChromeUtils.importESModule(
  "moz-src:///browser/components/ProfileDataUpgrader.sys.mjs"
);

const PREF_SEMANTIC_HISTORY_SMARTWINDOW_FEATURE_GATE =
  "places.semanticHistory.smartwindow.featureGate";
const PREF_SMARTWINDOW_ENABLED = "browser.smartwindow.enabled";

function clearTestPrefs() {
  Services.prefs.clearUserPref(PREF_SEMANTIC_HISTORY_SMARTWINDOW_FEATURE_GATE);
  Services.prefs.clearUserPref(PREF_SMARTWINDOW_ENABLED);
}

add_task(function test_smartwindow_semantic_history_pref_enabled_for_users() {
  clearTestPrefs();
  Services.prefs.setBoolPref(PREF_SMARTWINDOW_ENABLED, true);

  ProfileDataUpgrader.upgrade(171, 172);

  Assert.ok(
    Services.prefs.getBoolPref(
      PREF_SEMANTIC_HISTORY_SMARTWINDOW_FEATURE_GATE,
      false
    ),
    "Semantic history should be enabled for existing Smart Window users."
  );
});

add_task(function test_smartwindow_disabled_does_not_enable_semantic() {
  clearTestPrefs();

  ProfileDataUpgrader.upgrade(171, 172);

  Assert.ok(
    !Services.prefs.prefHasUserValue(
      PREF_SEMANTIC_HISTORY_SMARTWINDOW_FEATURE_GATE
    ),
    "Semantic history pref should not be set when Smart Window is disabled."
  );
});
