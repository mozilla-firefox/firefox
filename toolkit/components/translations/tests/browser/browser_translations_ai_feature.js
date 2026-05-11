/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const AI_CONTROL_DEFAULT_PREF = "browser.ai.control.default";
const AI_CONTROL_TRANSLATIONS_PREF = "browser.ai.control.translations";
const TRANSLATIONS_ENABLE_PREF = "browser.translations.enable";

/**
 * This test case ensures that the Translations feature contains the proper id.
 */
add_task(async function test_ai_feature_id() {
  is(
    TranslationsFeature.id,
    "translations",
    "AIFeature exposes the translations id"
  );
});

/**
 * This test case ensures that the Translations feature exposes the required
 * AI feature contract values.
 */
add_task(async function test_ai_feature_contract_values() {
  const feature = TranslationsFeature;

  is(
    feature.hasDistinctEnabledState,
    false,
    "Translations does not expose a distinct enabled AI Controls state"
  );
  is(
    feature.canRunOnDevice,
    TranslationsParent.getIsTranslationsEngineSupported(),
    "Translations device support matches the engine capability check"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.translations.simulateUnsupportedEngine", true]],
  });

  try {
    is(
      feature.canRunOnDevice,
      false,
      "Translations reports unsupported devices through canRunOnDevice"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * This test case ensures that the Translations feature state is correct for
 * all combinations of the prefs that control its enabled state.
 */
add_task(async function test_ai_feature_state_combinations() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AI_CONTROL_DEFAULT_PREF, "available"],
      [AI_CONTROL_TRANSLATIONS_PREF, "default"],
      [TRANSLATIONS_ENABLE_PREF, false],
    ],
  });

  const feature = TranslationsFeature;

  const cases = [
    {
      defaultPref: "available",
      translationsPref: "blocked",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "available",
      translationsPref: "blocked",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "available",
      translationsPref: "enabled",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "available",
      translationsPref: "enabled",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "available",
      translationsPref: "default",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "available",
      translationsPref: "default",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "available",
      translationsPref: "invalid",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "available",
      translationsPref: "invalid",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "blocked",
      translationsPref: "blocked",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "blocked",
      translationsPref: "blocked",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "blocked",
      translationsPref: "enabled",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "blocked",
      translationsPref: "enabled",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "blocked",
      translationsPref: "default",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "blocked",
      translationsPref: "default",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "blocked",
      translationsPref: "invalid",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "blocked",
      translationsPref: "invalid",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "invalid",
      translationsPref: "blocked",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "invalid",
      translationsPref: "blocked",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "invalid",
      translationsPref: "enabled",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "invalid",
      translationsPref: "enabled",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "invalid",
      translationsPref: "default",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "invalid",
      translationsPref: "default",
      enabledPref: true,
      expectEnabled: true,
    },
    {
      defaultPref: "invalid",
      translationsPref: "invalid",
      enabledPref: false,
      expectEnabled: false,
    },
    {
      defaultPref: "invalid",
      translationsPref: "invalid",
      enabledPref: true,
      expectEnabled: true,
    },
  ];

  is(cases.length, 24, "Covers all combinations");

  for (const {
    translationsPref,
    defaultPref,
    enabledPref,
    expectEnabled,
  } of cases) {
    const description = `default=${defaultPref} translations=${translationsPref} enabled=${enabledPref}`;
    info(`Translations feature state: ${description}`);
    Services.prefs.setStringPref(AI_CONTROL_DEFAULT_PREF, defaultPref);
    Services.prefs.setStringPref(
      AI_CONTROL_TRANSLATIONS_PREF,
      translationsPref
    );
    Services.prefs.setBoolPref(TRANSLATIONS_ENABLE_PREF, enabledPref);

    is(feature.isAllowed, true, `${description} (allowed state)`);
    is(feature.isBlocked, !expectEnabled, `${description} (blocked state)`);
    is(feature.isEnabled, expectEnabled, `${description} (enabled state)`);
    is(
      feature.aiControlState,
      expectEnabled ? "available" : "blocked",
      `${description} (AI Controls state)`
    );
  }

  await SpecialPowers.popPrefEnv();
});

/**
 * This test case ensures that enabling the Translations feature updates prefs without deleting artifacts.
 */
add_task(async function test_ai_feature_enable() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AI_CONTROL_DEFAULT_PREF, "available"],
      [AI_CONTROL_TRANSLATIONS_PREF, "default"],
      [TRANSLATIONS_ENABLE_PREF, false],
    ],
  });

  const feature = TranslationsFeature;
  const originalDeleteAllLanguageFiles =
    TranslationsUtils.deleteAllLanguageFiles;
  let deleteCalls = 0;

  TranslationsUtils.deleteAllLanguageFiles = async () => {
    deleteCalls += 1;
    return [];
  };

  try {
    await feature.enable();
    is(
      Services.prefs.getStringPref(AI_CONTROL_TRANSLATIONS_PREF),
      "enabled",
      "Enable sets the AI control pref"
    );
    is(
      Services.prefs.getBoolPref(TRANSLATIONS_ENABLE_PREF),
      true,
      "Enable turns on translations"
    );
    is(feature.isEnabled, true, "Enable leaves translations enabled");
    is(
      feature.aiControlState,
      "available",
      "Enable reports the available AI Controls state"
    );
    is(deleteCalls, 0, "Enable does not delete artifacts");

    await TestTranslationsTelemetry.assertEvent(
      Glean.translationsFeature.enable,
      {
        expectedEventCount: 1,
      }
    );
  } finally {
    TranslationsUtils.deleteAllLanguageFiles = originalDeleteAllLanguageFiles;
    await SpecialPowers.popPrefEnv();
    TestTranslationsTelemetry.cleanup();
  }
});

/**
 * This test case ensures that disabling the Translations feature updates prefs and deletes artifacts.
 */
add_task(async function test_ai_feature_disable() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AI_CONTROL_DEFAULT_PREF, "available"],
      [AI_CONTROL_TRANSLATIONS_PREF, "enabled"],
      [TRANSLATIONS_ENABLE_PREF, true],
    ],
  });

  const feature = TranslationsFeature;
  const originalDeleteAllLanguageFiles =
    TranslationsUtils.deleteAllLanguageFiles;
  let deleteCalls = 0;
  let shouldThrow = false;

  TranslationsUtils.deleteAllLanguageFiles = async () => {
    deleteCalls += 1;
    if (shouldThrow) {
      throw new Error("Delete failed");
    }
    return [];
  };

  try {
    shouldThrow = true;
    await feature.block();
    is(
      Services.prefs.getStringPref(AI_CONTROL_TRANSLATIONS_PREF),
      "blocked",
      "Disable sets the AI control pref"
    );
    is(
      Services.prefs.getBoolPref(TRANSLATIONS_ENABLE_PREF),
      false,
      "Disable turns off translations"
    );
    is(feature.isBlocked, true, "Disable leaves translations blocked");
    is(
      feature.aiControlState,
      "blocked",
      "Disable reports the blocked AI Controls state"
    );
    is(deleteCalls, 1, "Disable deletes artifacts");

    await TestTranslationsTelemetry.assertEvent(
      Glean.translationsFeature.disable,
      {
        expectedEventCount: 1,
      }
    );
  } finally {
    TranslationsUtils.deleteAllLanguageFiles = originalDeleteAllLanguageFiles;
    await SpecialPowers.popPrefEnv();
    TestTranslationsTelemetry.cleanup();
  }
});

/**
 * This test case ensures that resetting the Translations feature clears prefs and deletes artifacts.
 */
add_task(async function test_ai_feature_reset() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AI_CONTROL_DEFAULT_PREF, "available"],
      [AI_CONTROL_TRANSLATIONS_PREF, "enabled"],
      [TRANSLATIONS_ENABLE_PREF, true],
    ],
  });

  const feature = TranslationsFeature;
  const originalDeleteAllLanguageFiles =
    TranslationsUtils.deleteAllLanguageFiles;
  let deleteCalls = 0;

  TranslationsUtils.deleteAllLanguageFiles = async () => {
    deleteCalls += 1;
    return [];
  };

  try {
    await feature.makeAvailable();
    is(
      Services.prefs.getStringPref(AI_CONTROL_TRANSLATIONS_PREF),
      "available",
      "makeAvailable sets the AI control pref to 'available'"
    );
    is(
      Services.prefs.getBoolPref(TRANSLATIONS_ENABLE_PREF),
      true,
      "makeAvailable enables translations"
    );
    is(feature.isEnabled, true, "makeAvailable leaves translations enabled");
    is(
      feature.aiControlState,
      "available",
      "makeAvailable reports the available AI Controls state"
    );
    is(deleteCalls, 1, "Reset deletes artifacts");

    await TestTranslationsTelemetry.assertEvent(
      Glean.translationsFeature.reset,
      {
        expectedEventCount: 1,
      }
    );
  } finally {
    TranslationsUtils.deleteAllLanguageFiles = originalDeleteAllLanguageFiles;
    await SpecialPowers.popPrefEnv();
    TestTranslationsTelemetry.cleanup();
  }
});

/**
 * This test case ensures that policy-managed translations enable pref rejects Translations feature changes.
 */
add_task(async function test_ai_feature_policy_lock_enable_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AI_CONTROL_DEFAULT_PREF, "available"],
      [AI_CONTROL_TRANSLATIONS_PREF, "default"],
      [TRANSLATIONS_ENABLE_PREF, false],
    ],
  });

  const feature = TranslationsFeature;

  ok(!feature.isManagedByPolicy, "Policy managed state starts off");
  Services.prefs.lockPref(TRANSLATIONS_ENABLE_PREF);
  try {
    ok(
      feature.isManagedByPolicy,
      "Policy managed state reflects translations pref management"
    );
    let threw = false;
    try {
      await feature.enable();
    } catch (error) {
      threw = true;
    }
    ok(threw, "Enable rejects when prefs are managed by policy");

    threw = false;
    try {
      await feature.block();
    } catch (error) {
      threw = true;
    }
    ok(threw, "Disable rejects when prefs are managed by policy");

    threw = false;
    try {
      await feature.makeAvailable();
    } catch (error) {
      threw = true;
    }
    ok(threw, "Reset rejects when prefs are managed by policy");
  } finally {
    Services.prefs.unlockPref(TRANSLATIONS_ENABLE_PREF);
    ok(
      !feature.isManagedByPolicy,
      "Policy managed state clears when prefs are unlocked"
    );
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * This test case ensures that policy-managed AI control pref rejects Translations feature changes.
 */
add_task(async function test_ai_feature_policy_lock_ai_control_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AI_CONTROL_DEFAULT_PREF, "available"],
      [AI_CONTROL_TRANSLATIONS_PREF, "default"],
      [TRANSLATIONS_ENABLE_PREF, false],
    ],
  });

  const feature = TranslationsFeature;

  ok(!feature.isManagedByPolicy, "Policy managed state starts off");
  Services.prefs.lockPref(AI_CONTROL_TRANSLATIONS_PREF);
  try {
    ok(
      feature.isManagedByPolicy,
      "Policy managed state reflects AI control pref management"
    );
    let threw = false;
    try {
      await feature.enable();
    } catch (error) {
      threw = true;
    }
    ok(threw, "Enable rejects when AI control pref is managed by policy");

    threw = false;
    try {
      await feature.block();
    } catch (error) {
      threw = true;
    }
    ok(threw, "Disable rejects when AI control pref is managed by policy");

    threw = false;
    try {
      await feature.makeAvailable();
    } catch (error) {
      threw = true;
    }
    ok(threw, "Reset rejects when AI control pref is managed by policy");
  } finally {
    Services.prefs.unlockPref(AI_CONTROL_TRANSLATIONS_PREF);
    ok(
      !feature.isManagedByPolicy,
      "Policy managed state clears when prefs are unlocked"
    );
    await SpecialPowers.popPrefEnv();
  }
});
