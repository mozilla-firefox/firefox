/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const HOMEPAGE_PREF = "browser.startup.homepage";
const NEWTAB_ENABLED_PREF = "browser.newtabpage.enabled";
const DEFAULT_HOMEPAGE_URL = "about:home";

// manageTopics visibility prefs
const STORIES_SYSTEM_PREF =
  "browser.newtabpage.activity-stream.feeds.system.topstories";
const STORIES_PREF =
  "browser.newtabpage.activity-stream.feeds.section.topstories";
const SECTIONS_ENABLED_PREF =
  "browser.newtabpage.activity-stream.discoverystream.sections.enabled";
const SECTIONS_PERSONALIZATION_ENABLED_PREF =
  "browser.newtabpage.activity-stream.discoverystream.sections.personalization.enabled";
const SECTIONS_CUSTOMIZE_MENU_PANEL_ENABLED_PREF =
  "browser.newtabpage.activity-stream.discoverystream.sections.customizeMenuPanel.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["identity.fxaccounts.account.device.name", ""],
      // Both "New windows" and "New tabs" are Firefox Home.
      [HOMEPAGE_PREF, DEFAULT_HOMEPAGE_URL],
      [NEWTAB_ENABLED_PREF, true],
      // manageTopics visibility prereqs
      [STORIES_SYSTEM_PREF, true],
      [STORIES_PREF, true],
      [SECTIONS_ENABLED_PREF, true],
      [SECTIONS_PERSONALIZATION_ENABLED_PREF, true],
      [SECTIONS_CUSTOMIZE_MENU_PANEL_ENABLED_PREF, true],
    ],
  });
});

add_task(async function test_manage_topics_opens_in_tab() {
  await assertHomeSettingLinkOpens({
    settingId: "manageTopics",
    expectedUrl: "about:home#customize-topics?source=settings",
    expectedWhere: "tab",
  });
});
