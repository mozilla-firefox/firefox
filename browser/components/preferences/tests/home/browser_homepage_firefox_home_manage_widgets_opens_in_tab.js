/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const HOMEPAGE_PREF = "browser.startup.homepage";
const NEWTAB_ENABLED_PREF = "browser.newtabpage.enabled";
const DEFAULT_HOMEPAGE_URL = "about:home";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["identity.fxaccounts.account.device.name", ""],
      // Both "New windows" and "New tabs" are Firefox Home.
      [HOMEPAGE_PREF, DEFAULT_HOMEPAGE_URL],
      [NEWTAB_ENABLED_PREF, true],
      ["browser.newtabpage.activity-stream.widgets.system.enabled", true],
      ["browser.newtabpage.activity-stream.widgets.enabled", true],
    ],
  });
});

add_task(async function test_manage_widgets_opens_in_tab() {
  await assertHomeSettingLinkOpens({
    settingId: "manageWidgets",
    expectedUrl: "about:home#customize-widgets?source=settings",
    expectedWhere: "tab",
  });
});
