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
  async function test_container_list_accessible_name() {
    let tab;
    try {
      await openPreferencesViaOpenPreferencesAPI("containers", {
        leaveOpen: true,
      });
      tab = gBrowser.selectedTab;
      let win = gBrowser.contentWindow;
      let boxGroup = (await settingControlRenders("containers-list", win))
        .controlEl;
      await assertReorderableListAccessibleName(boxGroup);
    } finally {
      if (tab) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
  }
);
