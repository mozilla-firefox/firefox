/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const PREF_IS_DEFAULT_WINDOW = "browser.smartwindow.isDefaultWindow";
const PREF_SMARTWINDOW_ENABLED = "browser.smartwindow.enabled";
const PREF_AGENT_ENABLED = "browser.smartwindow.agent.enabled";
const PREF_AGENT_SUPPORTED_REGIONS =
  "browser.smartwindow.agent.supportedRegions";

/**
 * Enrolling in the smartWindow feature with isDefault=true sets the
 * isDefaultWindow pref on the user branch.
 */
add_task(async function test_nimbus_enrollment_sets_pref() {
  ok(
    !Services.prefs.prefHasUserValue(PREF_IS_DEFAULT_WINDOW),
    "No user value for isDefaultWindow before enrollment"
  );

  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "smartWindow",
    value: { isDefault: true },
  });

  ok(
    Services.prefs.getBoolPref(PREF_IS_DEFAULT_WINDOW),
    "isDefaultWindow pref is true after enrollment"
  );
  ok(
    Services.prefs.prefHasUserValue(PREF_IS_DEFAULT_WINDOW),
    "isDefaultWindow pref is set on the user branch"
  );

  await cleanup();
  Services.prefs.clearUserPref(PREF_IS_DEFAULT_WINDOW);
});

/**
 * Unenrolling restores the pref to its pre-enrollment value, providing
 * rollback without a code change.
 */
add_task(async function test_nimbus_unenrollment_restores_pref() {
  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "smartWindow",
    value: { isDefault: true },
  });

  ok(
    Services.prefs.getBoolPref(PREF_IS_DEFAULT_WINDOW),
    "isDefaultWindow pref is true while enrolled"
  );

  await cleanup();

  ok(
    !Services.prefs.getBoolPref(PREF_IS_DEFAULT_WINDOW, false),
    "isDefaultWindow pref is false after unenrollment"
  );
});

/**
 * Existing eligibility checks still apply: isDefaultWindow requires
 * browser.smartwindow.enabled. Nimbus enrollment alone does not bypass
 * this check.
 */
add_task(async function test_nimbus_enrollment_respects_eligibility_checks() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", false]],
  });

  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "smartWindow",
    value: { isDefault: true },
  });

  ok(
    !AIWindow.isDefaultWindow,
    "isDefaultWindow is false even when enrolled because browser.smartwindow.enabled is false"
  );

  await cleanup();
  Services.prefs.clearUserPref(PREF_IS_DEFAULT_WINDOW);
  await SpecialPowers.popPrefEnv();
});

/**
 * Enrolling in the smartWindow feature with enabled=true flips the
 * browser.smartwindow.enabled pref to true via AIWindow.onNimbusUpdate.
 */
add_task(async function test_nimbus_enabled_variable_sets_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_SMARTWINDOW_ENABLED, false]],
  });

  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "smartWindow",
    value: { enabled: true },
  });

  ok(
    Services.prefs.getBoolPref(PREF_SMARTWINDOW_ENABLED),
    "browser.smartwindow.enabled is true after enrolling with enabled=true"
  );

  await cleanup();
  Services.prefs.clearUserPref(PREF_SMARTWINDOW_ENABLED);
  await SpecialPowers.popPrefEnv();
});

/**
 * Turning off the enabled variable does not disable the feature: the
 * browser.smartwindow.enabled pref is only ever flipped on, never off.
 */
add_task(async function test_nimbus_enabled_false_does_not_disable() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_SMARTWINDOW_ENABLED, true]],
  });

  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "smartWindow",
    value: { enabled: false },
  });

  ok(
    Services.prefs.getBoolPref(PREF_SMARTWINDOW_ENABLED),
    "browser.smartwindow.enabled remains true; disabling the variable does not disable the feature"
  );

  await cleanup();
  await SpecialPowers.popPrefEnv();
});

/**
 * Enrolling in the smartWindowAgentWatch feature writes its setPref variables
 * (default branch), so the agent watch capability can be controlled remotely,
 * and unenrolling restores the built-in defaults.
 */
add_task(async function test_nimbus_agent_watch_sets_and_restores_prefs() {
  is(
    Services.prefs.getBoolPref(PREF_AGENT_ENABLED),
    true,
    "agent.enabled defaults to true before enrollment"
  );
  is(
    Services.prefs.getCharPref(PREF_AGENT_SUPPORTED_REGIONS),
    "US,CA",
    "agent.supportedRegions defaults to US,CA before enrollment"
  );

  const cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "smartWindowAgentWatch",
    value: { enabled: false, supportedRegions: "GB" },
  });

  is(
    Services.prefs.getBoolPref(PREF_AGENT_ENABLED),
    false,
    "agent.enabled reflects the enrolled value"
  );
  is(
    Services.prefs.getCharPref(PREF_AGENT_SUPPORTED_REGIONS),
    "GB",
    "agent.supportedRegions reflects the enrolled value"
  );

  await cleanup();

  is(
    Services.prefs.getBoolPref(PREF_AGENT_ENABLED),
    true,
    "agent.enabled is restored to its default after unenrollment"
  );
  is(
    Services.prefs.getCharPref(PREF_AGENT_SUPPORTED_REGIONS),
    "US,CA",
    "agent.supportedRegions is restored to its default after unenrollment"
  );
});
