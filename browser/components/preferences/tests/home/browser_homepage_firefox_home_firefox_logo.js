/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const HIDE_LOGO_PREF = "browser.newtabpage.activity-stream.hideLogo";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["identity.fxaccounts.account.device.name", ""],
      // Default state: logo visible (hideLogo=false).
      [HIDE_LOGO_PREF, false],
    ],
  });
});

add_task(async function test_firefox_logo_toggle() {
  let { win, tab } = await openHomePreferences();

  try {
    let control = await settingControlRenders("firefoxLogo", win);
    ok(BrowserTestUtils.isVisible(control), "firefoxLogo control is visible");

    let toggle = control.querySelector("moz-toggle");
    ok(toggle, "firefoxLogo renders a moz-toggle");

    ok(
      toggle.pressed,
      "Toggle is ON by default (logo visible, hideLogo=false)"
    );

    let prefChanged = waitForPrefChange(HIDE_LOGO_PREF, true);
    toggle.click();
    await prefChanged;
    await waitForToggleState(toggle, false);

    is(
      Services.prefs.getBoolPref(HIDE_LOGO_PREF),
      true,
      "hideLogo is now true (logo hidden)"
    );

    prefChanged = waitForPrefChange(HIDE_LOGO_PREF, false);
    toggle.click();
    await prefChanged;
    await waitForToggleState(toggle, true);

    is(
      Services.prefs.getBoolPref(HIDE_LOGO_PREF),
      false,
      "hideLogo is now false (logo visible)"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});
