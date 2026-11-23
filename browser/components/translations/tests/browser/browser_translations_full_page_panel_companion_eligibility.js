/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures a pinned source page has no companion action and
 * unpinning restores it through either panel entry point.
 */
add_task(async function test_companion_hidden_while_source_tab_is_pinned() {
  for (const openFromAppMenu of [false, true]) {
    const { cleanup, tab: sourceTab } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      prefs: [
        ["browser.tabs.splitView.enabled", true],
        ["sidebar.verticalTabs.dragToPinPromo.dismissed", true],
      ],
    });

    try {
      const pinned = BrowserTestUtils.waitForEvent(sourceTab, "TabPinned");
      gBrowser.pinTab(sourceTab);
      await pinned;
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: () =>
          FullPageTranslationsTestUtils.assertPanelViewIntro({
            companionButtonVisible: false,
          }),
      });
      await FullPageTranslationsTestUtils.clickCancelButton();

      info("Unpin the source page and reopen the panel");
      const unpinned = BrowserTestUtils.waitForEvent(sourceTab, "TabUnpinned");
      gBrowser.unpinTab(sourceTab);
      await unpinned;
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
      });
      await FullPageTranslationsTestUtils.clickCancelButton();
    } finally {
      await cleanup();
    }
  }
});

/**
 * This test case ensures the companion action is unavailable when Split View
 * is disabled, even for a standalone source page.
 */
add_task(async function test_companion_hidden_when_split_view_disabled() {
  await FullPageTranslationsTestUtils.assertCompanionButtonHiddenWhenSplitViewDisabled(
    {
      openFromAppMenu: false,
    }
  );
});

/**
 * This test case ensures the companion action remains unavailable with Split
 * View disabled when the panel opens from the app menu.
 */
add_task(
  async function test_companion_hidden_when_split_view_disabled_app_menu() {
    await FullPageTranslationsTestUtils.assertCompanionButtonHiddenWhenSplitViewDisabled(
      {
        openFromAppMenu: true,
      }
    );
  }
);
