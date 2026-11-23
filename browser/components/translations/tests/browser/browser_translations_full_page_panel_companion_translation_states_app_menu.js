/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures the companion is unavailable during translation and
 * returns after translation and restoration through the app menu.
 */
add_task(
  async function test_companion_visible_after_translation_and_restore_from_app_menu() {
    const openFromAppMenu = true;
    const { cleanup, resolveDownloads, runInPage } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      prefs: [["browser.tabs.splitView.enabled", true]],
    });

    try {
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
      });
      await FullPageTranslationsTestUtils.clickTranslateButton();
      await FullPageTranslationsTestUtils.assertTranslationsButton(
        { button: true, circleArrows: true, locale: false, icon: true },
        "The icon presents the loading indicator."
      );

      info("The companion is hidden while translation is loading");
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewLoading,
      });
      await FullPageTranslationsTestUtils.clickCancelButton();
      await resolveDownloads(1);
      await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
        {
          fromLanguage: "es",
          toLanguage: "en",
          runInPage,
        }
      );

      info("The companion is available after the page has been translated");
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewRevisit,
      });
      await FullPageTranslationsTestUtils.clickRestoreButton();
      await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);

      info("The companion remains available after restoring the original page");
      await FullPageTranslationsTestUtils.openPanel({ openFromAppMenu });
      FullPageTranslationsTestUtils.assertCompanionButtonVisibility({
        visible: true,
      });
      await FullPageTranslationsTestUtils.clickCancelButton();
    } finally {
      await cleanup();
    }
  }
);
