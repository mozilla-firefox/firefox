/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures that when the panel is reopened from the app menu
 * during translation, the loading view retains the manually selected target.
 */
add_task(
  async function test_translations_panel_loading_selected_target_app_menu() {
    const openFromAppMenu = true;

    const { cleanup, resolveDownloads, runInPage } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      webLanguages: ["en"],
      languagePairs: LANGUAGE_PAIRS,
    });

    try {
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        expectedFromLanguage: "es",
        expectedToLanguage: "en",
      });
      await FullPageTranslationsTestUtils.changeSelectedToLanguage({
        langTag: "fr",
      });
      await FullPageTranslationsTestUtils.clickTranslateButton();
      await FullPageTranslationsTestUtils.assertTranslationsButton(
        { button: true, circleArrows: true, locale: false, icon: true },
        "The icon presents the loading indicator."
      );

      info("Reopen the panel before the translation downloads complete");
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: () => {},
      });
      FullPageTranslationsTestUtils.assertSelectedFromLanguage({
        langTag: "es",
      });
      FullPageTranslationsTestUtils.assertSelectedToLanguage({ langTag: "fr" });
      FullPageTranslationsTestUtils.assertPanelViewLoading();
      await FullPageTranslationsTestUtils.clickCancelButton();

      await resolveDownloads(2);
      await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
        {
          fromLanguage: "es",
          toLanguage: "fr",
          runInPage,
        }
      );

      info("The completed translation offers the preferred target again");
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        expectedToLanguage: "en",
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewRevisit,
      });
      await FullPageTranslationsTestUtils.clickRestoreButton();
      await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);
    } finally {
      await cleanup();
    }
  }
);
