/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SearchTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SearchTestUtils.sys.mjs"
);

SearchTestUtils.init(this);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.account.device.name", ""]],
  });
});

add_task(
  { skip_if: () => !AppConstants.ACCESSIBILITY },
  async function test_search_engine_list_accessible_name() {
    await SearchTestUtils.updateRemoteSettingsConfig([
      { identifier: "a" },
      { identifier: "b" },
      { identifier: "c" },
      { identifier: "d" },
      { identifier: "e" },
    ]);

    let tab;
    try {
      await openPreferencesViaOpenPreferencesAPI("search", { leaveOpen: true });
      tab = gBrowser.selectedTab;
      let win = gBrowser.contentWindow;
      let boxGroup = (await settingControlRenders("engineList", win)).controlEl;
      await assertReorderableListAccessibleName(boxGroup);
    } finally {
      if (tab) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
  }
);
