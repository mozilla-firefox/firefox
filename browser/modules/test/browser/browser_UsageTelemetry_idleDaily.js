/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PREF_NAME = "browser.link.open_newwindow.override.external";
const PREF_VALUE_FEATURE_ON = Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT;
const PREF_VALUE_FEATURE_OFF = Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_BACKGROUND;

const NOVA_PREF = "browser.nova.enabled";

// Bookkeeping prefs written by other idle-daily observers, reset below so this
// test doesn't leak state into the rest of its chunk.
const COLLATERAL_PREFS = [
  "privacy.purge_trackers.last_purge",
  "privacy.purge_trackers.date_in_cookie_database",
];

async function resetTelemetry() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
}

add_setup(async function () {
  await resetTelemetry();
  registerCleanupFunction(async () => {
    for (let pref of COLLATERAL_PREFS) {
      Services.prefs.clearUserPref(pref);
    }
    await resetTelemetry();
  });
});

/**
 * BrowserUsageTelemetry reports the value of a handful of prefs at startup and
 * again on each daily idle, so that changes made mid-session are picked up.
 */
add_task(async function test_idleDailyRerecordsPrefValues() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NAME, PREF_VALUE_FEATURE_ON]],
  });

  // Nothing observes this pref directly, so the metric should still be unset.
  // This is what makes the assertions below attributable to idle-daily.
  Assert.equal(
    Glean.linkHandling.openNextToActiveTabSettingsEnabled.testGetValue(),
    null,
    "setting the pref on its own should not record the metric"
  );

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () =>
      Glean.linkHandling.openNextToActiveTabSettingsEnabled.testGetValue() ===
      true,
    "wait for idle-daily to record the enabled pref value"
  );
  Assert.equal(
    Glean.linkHandling.openNextToActiveTabSettingsEnabled.testGetValue(),
    true,
    "idle-daily should have recorded the enabled pref value"
  );

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NAME, PREF_VALUE_FEATURE_OFF]],
  });

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () =>
      Glean.linkHandling.openNextToActiveTabSettingsEnabled.testGetValue() ===
      false,
    "wait for idle-daily to record the updated pref value"
  );
  Assert.equal(
    Glean.linkHandling.openNextToActiveTabSettingsEnabled.testGetValue(),
    false,
    "idle-daily should have picked up the mid-session pref change"
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * browser.nova.enabled is reported on the same schedule.
 * While the pref isn't locked in yet (see Bug 2082898), verify it is
 * correctly recorded so we can tell how many people turn the Nova redesign
 * off after the pref is enabled by default.
 */
add_task(async function test_idleDailyRecordsNovaEnabled() {
  // The task above already fired idle-daily, so clear that value: otherwise the
  // first iteration below could pass without anything having been recorded.
  await resetTelemetry();
  Assert.equal(
    Glean.nova.enabled.testGetValue(),
    null,
    "the metric should start out unset"
  );

  let novaEnabled = Services.prefs.getBoolPref(NOVA_PREF, true);

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => Glean.nova.enabled.testGetValue() === novaEnabled,
    `wait for idle-daily to record browser.nova.enabled=${novaEnabled}`
  );
  Assert.equal(
    Glean.nova.enabled.testGetValue(),
    novaEnabled,
    `idle-daily should have recorded browser.nova.enabled=${novaEnabled}`
  );
});

/**
 * The theme picker's appearance and native theme settings are reported on the
 * same schedule, so that we can tell how often they differ from the default.
 */
add_task(async function test_idleDailyRecordsThemePickerValues() {
  await resetTelemetry();
  Assert.equal(
    Glean.themePicker.appearance.testGetValue(),
    null,
    "appearance should start out unset"
  );
  Assert.equal(
    Glean.themePicker.nativeTheme.testGetValue(),
    null,
    "nativeTheme should start out unset"
  );

  const cases = [
    { prefs: [["ui.systemUsesDarkTheme", 0]], appearance: "light" },
    { prefs: [["ui.systemUsesDarkTheme", 1]], appearance: "dark" },
    { prefs: [], appearance: "device" },
  ];
  for (let { prefs, appearance } of cases) {
    for (let nativeTheme of [true, false]) {
      await SpecialPowers.pushPrefEnv({
        set: [...prefs, ["browser.theme.native-theme", nativeTheme]],
      });

      Services.obs.notifyObservers(null, "idle-daily");

      await TestUtils.waitForCondition(
        () =>
          Glean.themePicker.appearance.testGetValue() === appearance &&
          Glean.themePicker.nativeTheme.testGetValue() === nativeTheme,
        `wait for idle-daily to record appearance=${appearance} nativeTheme=${nativeTheme}`
      );
      Assert.equal(
        Glean.themePicker.appearance.testGetValue(),
        appearance,
        `idle-daily should have recorded appearance=${appearance}`
      );
      Assert.equal(
        Glean.themePicker.nativeTheme.testGetValue(),
        nativeTheme,
        `idle-daily should have recorded nativeTheme=${nativeTheme}`
      );

      await SpecialPowers.popPrefEnv();
    }
  }
});
