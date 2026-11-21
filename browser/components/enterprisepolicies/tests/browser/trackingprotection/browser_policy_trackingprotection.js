/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_enabletrackingprotection_strict() {
  await BrowserTestUtils.withNewTab("about:preferences#etp", async browser => {
    let strictRadio = browser.contentDocument.getElementById("etpLevelStrict");
    is(strictRadio.checked, true, "Strict checkbox should be selected");
    is(
      strictRadio.disabled,
      false,
      "Selected strict radio should stay enabled for its nested controls"
    );
    let doc = browser.contentDocument;
    is(
      doc.getElementById("etpLevelStandard").disabled,
      true,
      "Standard radio should be disabled"
    );
    is(
      doc.getElementById("etpLevelCustom").disabled,
      true,
      "Custom radio should be disabled"
    );
    let baselineCheckbox = doc.getElementById("etpAllowListBaselineEnabled");
    ok(
      !baselineCheckbox.disabled && !baselineCheckbox.parentDisabled,
      "Baseline exceptions checkbox should be enabled since its pref is not locked"
    );
  });
});

add_task(async function test_enabletrackingprotection_strict_preferences() {
  is(
    Services.prefs.prefIsLocked("privacy.trackingprotection.enabled"),
    true,
    "Preference should be locked"
  );
  is(
    Services.prefs.getBoolPref("privacy.trackingprotection.enabled"),
    true,
    "Preference should be true"
  );
  is(
    Services.prefs.prefIsLocked(
      "privacy.trackingprotection.allow_list.baseline.enabled"
    ),
    false,
    "Preference should NOT be locked"
  );
  is(
    Services.prefs.prefIsLocked(
      "privacy.trackingprotection.allow_list.convenience.enabled"
    ),
    false,
    "Preference should NOT be locked"
  );
  is(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.allow_list.baseline.enabled"
    ),
    true,
    "Default value for privacy.trackingprotection.allow_list.baseline.enabled should be true"
  );
  is(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.allow_list.convenience.enabled"
    ),
    false,
    "Default value for privacy.trackingprotection.allow_list.convenience.enabled should be false"
  );
});
