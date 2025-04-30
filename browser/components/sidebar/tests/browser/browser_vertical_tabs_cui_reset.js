/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(() =>
  SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, true]],
  })
);

add_task(async function test_cui_reset_vertical_tabs() {
  ok(Services.prefs.getBoolPref(VERTICAL_TABS_PREF), "Vertical tabs enabled");
  CustomizableUI.reset();
  ok(!Services.prefs.getBoolPref(VERTICAL_TABS_PREF), "Vertical tabs disabled");
});
