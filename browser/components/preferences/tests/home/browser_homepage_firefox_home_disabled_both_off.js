/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const HOMEPAGE_PREF = "browser.startup.homepage";
const NEWTAB_ENABLED_PREF = "browser.newtabpage.enabled";
const BLANK_HOMEPAGE_URL = "chrome://browser/content/blanktab.html";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["identity.fxaccounts.account.device.name", ""],
      // Set both to non-Firefox-Home values
      [HOMEPAGE_PREF, BLANK_HOMEPAGE_URL],
      [NEWTAB_ENABLED_PREF, false],
      // Enable conditionally-visible settings so we can test they get disabled
      ["browser.newtabpage.activity-stream.widgets.system.enabled", true],
      ["browser.newtabpage.activity-stream.feeds.system.topstories", true],
      // manageTopics needs this pref to be on, and the default varies
      // by region and language.
      [
        "browser.newtabpage.activity-stream.discoverystream.sections.enabled",
        true,
      ],
    ],
  });
});

async function assertSectionDisabled(win) {
  let noticeControl = await settingControlRenders(
    "firefoxHomeDisabledNotice",
    win
  );
  ok(noticeControl, "Disabled notice control exists");

  let messageBar = noticeControl.querySelector("moz-message-bar");
  ok(messageBar, "Message bar element exists");
  ok(
    BrowserTestUtils.isVisible(noticeControl),
    "Disabled notice is visible when both settings are not Firefox Home"
  );

  // With the widgets container on, Weather is managed with the other widgets.
  for (let settingId of [
    "webSearch",
    "widgets",
    "shortcuts",
    "stories",
    "supportFirefox",
    "recentActivity",
    "firefoxLogo",
  ]) {
    let control = await settingControlRenders(settingId, win);
    ok(
      control.controlEl.disabled,
      `${settingId} is disabled when Firefox Home is not active`
    );
  }
}

add_task(async function test_firefox_home_disabled_when_both_off() {
  let { win, tab } = await openHomePreferences();
  await assertSectionDisabled(win);

  let manageTopicsControl = getSettingControl("manageTopics", win);
  ok(
    !manageTopicsControl || BrowserTestUtils.isHidden(manageTopicsControl),
    "manageTopics box link is hidden when Firefox Home is not active"
  );
  let chooseWallpaperControl = getSettingControl("chooseWallpaper", win);
  ok(
    !chooseWallpaperControl ||
      BrowserTestUtils.isHidden(chooseWallpaperControl),
    "chooseWallpaper box link is hidden when Firefox Home is not active"
  );
  let manageWidgetsControl = getSettingControl("manageWidgets", win);
  ok(
    !manageWidgetsControl || BrowserTestUtils.isHidden(manageWidgetsControl),
    "manageWidgets box link is hidden when Firefox Home is not active"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_weather_standalone_disabled_when_both_off() {
  // Weather only gets its own row when the Widgets group is hidden.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.newtabpage.activity-stream.widgets.system.enabled", false],
      [
        "browser.newtabpage.activity-stream.widgets.system.weather.enabled",
        true,
      ],
    ],
  });

  let { win, tab } = await openHomePreferences();
  let control = await settingControlRenders("weatherStandalone", win);
  ok(
    control.controlEl.disabled,
    "weatherStandalone is disabled when Firefox Home is not active"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
