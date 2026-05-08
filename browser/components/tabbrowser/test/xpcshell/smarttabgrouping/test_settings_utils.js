/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SmartTabGroupingManager, SMART_TAB_GROUPING_CONFIG } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
  );

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { MLUninstallService } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const PREF_ENABLED = "browser.tabs.groups.smart.enabled";
const PREF_USER_ENABLED = "browser.tabs.groups.smart.userEnabled";
const PREF_OPTIN = "browser.tabs.groups.smart.optin";

function clearSTGPrefs() {
  for (const pref of [PREF_ENABLED, PREF_USER_ENABLED, PREF_OPTIN]) {
    if (Services.prefs.prefHasUserValue(pref)) {
      Services.prefs.clearUserPref(pref);
    }
  }
}

// Ensure we actually create a user value even if the default is already the same.
function setUserBoolPrefDifferentFromDefault(prefName) {
  let defaultValue = Services.prefs.getDefaultBranch("").getBoolPref(prefName);
  Services.prefs.setBoolPref(prefName, !defaultValue);
}

registerCleanupFunction(() => {
  clearSTGPrefs();
  sinon.restore();
});

add_task(function test_id_is_feature_id() {
  Assert.equal(
    SmartTabGroupingManager.id,
    "smart-tab-grouping",
    "SmartTabGroupingManager.id should be the feature id"
  );
});

add_task(function test_contract_values() {
  Assert.ok(
    SmartTabGroupingManager.hasDistinctEnabledState,
    "SmartTabGroupingManager should expose a distinct enabled state"
  );
  Assert.ok(
    SmartTabGroupingManager.canRunOnDevice,
    "SmartTabGroupingManager should report that it can run on this device"
  );
});

add_task(function test_isEnabled_requires_all_prefs() {
  clearSTGPrefs();

  Assert.ok(
    !SmartTabGroupingManager.isEnabled,
    "With no user prefs set, isEnabled should be false"
  );

  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setBoolPref(PREF_USER_ENABLED, true);
  Services.prefs.setBoolPref(PREF_OPTIN, true);

  Assert.ok(
    SmartTabGroupingManager.isEnabled,
    "isEnabled should be true when all three prefs are true"
  );

  Services.prefs.setBoolPref(PREF_OPTIN, false);
  Assert.ok(
    !SmartTabGroupingManager.isEnabled,
    "isEnabled should be false if optin=false"
  );

  Services.prefs.setBoolPref(PREF_OPTIN, true);
  Services.prefs.setBoolPref(PREF_USER_ENABLED, false);
  Assert.ok(
    !SmartTabGroupingManager.isEnabled,
    "isEnabled should be false if userEnabled=false"
  );

  Services.prefs.setBoolPref(PREF_USER_ENABLED, true);
  Services.prefs.setBoolPref(PREF_ENABLED, false);
  Assert.ok(
    !SmartTabGroupingManager.isEnabled,
    "isEnabled should be false if enabled=false"
  );
});

add_task(async function test_enable_sets_all_prefs_true() {
  clearSTGPrefs();

  await SmartTabGroupingManager.enable();

  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED, false),
    true,
    "enable() should set browser.tabs.groups.smart.enabled=true"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_USER_ENABLED, false),
    true,
    "enable() should set browser.tabs.groups.smart.userEnabled=true"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_OPTIN, false),
    true,
    "enable() should set browser.tabs.groups.smart.optin=true"
  );

  Assert.ok(
    SmartTabGroupingManager.isEnabled,
    "After enable(), isEnabled should be true"
  );
  Assert.equal(
    SmartTabGroupingManager.aiControlState,
    "enabled",
    "After enable(), aiControlState should be enabled"
  );
});

add_task(
  async function test_reset_sets_available_state_and_uninstalls_models() {
    clearSTGPrefs();

    // Force actual user values even if defaults match.
    setUserBoolPrefDifferentFromDefault(PREF_ENABLED);
    setUserBoolPrefDifferentFromDefault(PREF_USER_ENABLED);
    setUserBoolPrefDifferentFromDefault(PREF_OPTIN);

    const uninstallStub = sinon
      .stub(MLUninstallService, "uninstall")
      .resolves();

    await SmartTabGroupingManager.makeAvailable();

    Assert.equal(
      Services.prefs.getBoolPref(PREF_ENABLED, false),
      true,
      "reset() should set browser.tabs.groups.smart.enabled=true"
    );
    Assert.equal(
      Services.prefs.getBoolPref(PREF_USER_ENABLED, false),
      true,
      "reset() should set browser.tabs.groups.smart.userEnabled=true"
    );
    Assert.equal(
      Services.prefs.getBoolPref(PREF_OPTIN, true),
      false,
      "reset() should set browser.tabs.groups.smart.optin=false"
    );
    Assert.ok(
      !SmartTabGroupingManager.isEnabled,
      "After reset(), isEnabled should be false"
    );
    Assert.ok(
      !SmartTabGroupingManager.isBlocked,
      "After reset(), isBlocked should be false"
    );
    Assert.equal(
      SmartTabGroupingManager.aiControlState,
      "available",
      "After reset(), aiControlState should be available"
    );

    Assert.ok(
      uninstallStub.calledOnce,
      "reset() should uninstall ML engine files via MLUninstallService.uninstall()"
    );

    const expectedEngineIds = [
      SMART_TAB_GROUPING_CONFIG.topicGeneration.engineId,
      SMART_TAB_GROUPING_CONFIG.embedding.engineId,
    ].sort();

    const uninstallArgs = uninstallStub.getCall(0).args[0];
    Assert.deepEqual(
      (uninstallArgs.engineIds || []).slice().sort(),
      expectedEngineIds,
      "reset() should uninstall files for both STG engines"
    );
    Assert.equal(
      uninstallArgs.actor,
      "SmartTabGrouping",
      "reset() should pass the expected actor attribution"
    );

    uninstallStub.restore();
  }
);

add_task(function test_isBlocked_reflects_enabled_and_userEnabled() {
  clearSTGPrefs();

  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setBoolPref(PREF_USER_ENABLED, true);
  Assert.ok(
    !SmartTabGroupingManager.isBlocked,
    "isBlocked() should be false when enabled=true and userEnabled=true"
  );

  Services.prefs.setBoolPref(PREF_ENABLED, false);
  Services.prefs.setBoolPref(PREF_USER_ENABLED, true);
  Assert.ok(
    SmartTabGroupingManager.isBlocked,
    "isBlocked() should be true when enabled=false"
  );

  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setBoolPref(PREF_USER_ENABLED, false);
  Assert.ok(
    SmartTabGroupingManager.isBlocked,
    "isBlocked() should be true when userEnabled=false"
  );
});

add_task(async function test_disable_sets_prefs_false_and_uninstalls_models() {
  clearSTGPrefs();
  await SmartTabGroupingManager.enable();
  Assert.ok(
    SmartTabGroupingManager.isEnabled,
    "Sanity check: enabled before disable()"
  );

  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SmartTabGroupingManager.block();

  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED, true),
    false,
    "disable() should set browser.tabs.groups.smart.enabled=false"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_USER_ENABLED, true),
    false,
    "disable() should set browser.tabs.groups.smart.userEnabled=false"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_OPTIN, true),
    false,
    "disable() should set browser.tabs.groups.smart.optin=false"
  );

  Assert.ok(
    !SmartTabGroupingManager.isEnabled,
    "After disable(), isEnabled should be false"
  );
  Assert.equal(
    SmartTabGroupingManager.aiControlState,
    "blocked",
    "After disable(), aiControlState should be blocked"
  );

  Assert.ok(
    uninstallStub.calledOnce,
    "disable() should uninstall ML engine files via MLUninstallService.uninstall()"
  );

  const expectedEngineIds = [
    SMART_TAB_GROUPING_CONFIG.topicGeneration.engineId,
    SMART_TAB_GROUPING_CONFIG.embedding.engineId,
  ].sort();

  const uninstallArgs = uninstallStub.getCall(0).args[0];
  Assert.deepEqual(
    (uninstallArgs.engineIds || []).slice().sort(),
    expectedEngineIds,
    "disable() should uninstall files for both STG engines"
  );
  Assert.equal(
    uninstallArgs.actor,
    "SmartTabGrouping",
    "disable() should pass the expected actor attribution"
  );

  uninstallStub.restore();
});
