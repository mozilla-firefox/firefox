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
  async function test_website_language_list_accessible_name() {
    await SpecialPowers.pushPrefEnv({
      set: [["intl.accept_languages", "en,fr,de,es,ja"]],
    });

    let tab;
    try {
      await openPreferencesViaOpenPreferencesAPI("paneLanguages", {
        leaveOpen: true,
      });
      tab = gBrowser.selectedTab;
      let win = gBrowser.contentWindow;
      let boxGroup = (
        await settingControlRenders("websiteLanguageWrapper", win)
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
