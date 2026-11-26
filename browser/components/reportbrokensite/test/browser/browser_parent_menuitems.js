/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Test that the Report Broken Site menu items are disabled
 * when the active tab is not on a reportable URL, and is hidden
 * when the feature is disabled via pref. Also ensure that the
 * Report Broken Site item that is automatically generated in
 * the app menu's help sub-menu is hidden.
 */

"use strict";

add_common_setup();

add_task(async function testMenus() {
  ensureReportBrokenSitePreffedOff();

  const appMenu = AppMenu();
  const helpMenu = HelpMenu();
  const protectionsPanel = ProtectionsPanel();

  async function ensure(menu, fn, test) {
    // the hidden/disabled state of all of the menuitems may not update until one
    // is rendered; then the related <command>'s state is propagated to them all.
    await menu.open();
    await menu.close();

    await menu.open();
    fn(menu.reportBrokenSite, `${fn.name}(${menu.menuDescription}) ${test} - `);
    await menu.close();
  }

  await withNewTab("about:blank", async (_, tab) => {
    ReportBrokenSite.enableOrDisableMenuitems(tab);
    const test = "on invalid page when preffed off";
    await ensure(appMenu, isMenuItemDisabled, test);
    await ensure(helpMenu, isMenuItemDisabled, test);
    ensureProtectionsPanelHidden(test);
  });

  await withNewTab(REPORTABLE_PAGE_URL, async (_, tab) => {
    ReportBrokenSite.enableOrDisableMenuitems(tab);
    const test =
      "on valid page when preffed off (fallback to original reporter)";
    await ensure(appMenu, isMenuItemEnabled, test);
    await ensure(helpMenu, isMenuItemEnabled, test);
    await ensure(protectionsPanel, isMenuItemEnabled, test);
  });

  ensureReportBrokenSitePreffedOn();

  await withNewTab("about:blank", async (_, tab) => {
    ReportBrokenSite.enableOrDisableMenuitems(tab);
    const test = "on invalid page when preffed on";
    await ensure(appMenu, isMenuItemDisabled, test);
    await ensure(helpMenu, isMenuItemDisabled, test);
    ensureProtectionsPanelHidden(test);
  });

  await withNewTab(REPORTABLE_PAGE_URL, async (_, tab) => {
    ReportBrokenSite.enableOrDisableMenuitems(tab);
    const test = "on valid page when preffed on";
    await ensure(appMenu, isMenuItemEnabled, test);
    await ensure(helpMenu, isMenuItemEnabled, test);
    await ensure(protectionsPanel, isMenuItemEnabled, test);
  });

  ensureReportBrokenSitePreffedOff();

  await withNewTab(REPORTABLE_PAGE_URL, async (_, tab) => {
    ReportBrokenSite.enableOrDisableMenuitems(tab);
    const test = "still active when pref toggled back off";
    await ensure(appMenu, isMenuItemEnabled, test);
    await ensure(helpMenu, isMenuItemEnabled, test);
    await ensure(protectionsPanel, isMenuItemEnabled, test);
  });

  ensureReportBrokenSitePreffedOn();
});
