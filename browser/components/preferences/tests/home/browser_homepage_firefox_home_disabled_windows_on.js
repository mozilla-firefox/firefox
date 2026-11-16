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
      // New windows = Firefox Home, New tabs = blank
      [HOMEPAGE_PREF, DEFAULT_HOMEPAGE_URL],
      [NEWTAB_ENABLED_PREF, false],
      // Enable conditionally-visible settings so we can test they stay enabled
      ["browser.newtabpage.activity-stream.widgets.system.enabled", true],
      ["browser.newtabpage.activity-stream.feeds.system.topstories", true],
    ],
  });
});

async function assertSectionEnabled(win) {
  let noticeControl = await settingControlRenders(
    "firefoxHomeDisabledNotice",
    win
  );
  // The setting-control wrapper stays visible in the DOM; only the inner
  // message bar gets the hidden attribute, so we check that directly.
  let messageBar = noticeControl.querySelector("moz-message-bar");
  ok(messageBar, "Message bar element exists");
  ok(
    messageBar.hidden,
    "Message bar is hidden when New windows is Firefox Home"
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
      !control.controlEl.disabled,
      `${settingId} is enabled when New windows is Firefox Home`
    );
  }
}

add_task(async function test_firefox_home_enabled_when_windows_is_home() {
  let { win, tab } = await openHomePreferences();
  await assertSectionEnabled(win);
  BrowserTestUtils.removeTab(tab);
});
