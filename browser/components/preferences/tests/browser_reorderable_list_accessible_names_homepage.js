/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.account.device.name", ""]],
  });
});

add_task(
  { skip_if: () => !AppConstants.ACCESSIBILITY },
  async function test_custom_homepage_list_accessible_name() {
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "browser.startup.homepage",
          "https://example.com|https://test.org|https://mozilla.org",
        ],
      ],
    });

    let tab;
    try {
      await openPreferencesViaOpenPreferencesAPI("customHomepage", {
        leaveOpen: true,
      });
      tab = gBrowser.selectedTab;
      let win = gBrowser.contentWindow;
      let boxGroup = (
        await settingControlRenders("customHomepageBoxGroup", win)
      ).controlEl;
      await assertReorderableListAccessibleName(boxGroup);
    } finally {
      try {
        if (tab) {
          await BrowserTestUtils.removeTab(tab);
        }
      } finally {
        await SpecialPowers.popPrefEnv();
      }
    }
  }
);
