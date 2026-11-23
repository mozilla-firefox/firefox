/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case verifies that the proper error message is displayed in
 * the FullPageTranslationsPanel if the panel tries to open, but the language
 * dropdown menus fail to initialize.
 */
add_task(async function test_full_page_translations_panel_init_failure() {
  const { cleanup } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
  });

  TranslationsPanelShared.simulateLangListError();
  await FullPageTranslationsTestUtils.openPanel({
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewInitFailure,
  });

  await FullPageTranslationsTestUtils.clickCancelButton();

  await cleanup();
});

/**
 * This test case ensures the companion remains unavailable when language-list
 * loading fails and returns after recovery through either panel entry point.
 */
add_task(async function test_companion_hidden_until_language_lists_recover() {
  for (const openFromAppMenu of [false, true]) {
    const { cleanup } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      prefs: [["browser.tabs.splitView.enabled", true]],
    });

    try {
      info(`Fail language-list loading, app menu: ${openFromAppMenu}`);
      TranslationsPanelShared.simulateLangListError();
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewInitFailure,
      });
      FullPageTranslationsTestUtils.assertCompanionButtonVisibility({
        visible: false,
      });

      info("Retry loading the languages through the panel's retry button");
      await FullPageTranslationsTestUtils.clickLanguageListRetryButton();
      FullPageTranslationsTestUtils.assertCompanionButtonVisibility({
        visible: true,
      });
      await FullPageTranslationsTestUtils.clickCancelButton();

      info("The companion remains available when reopening after recovery");
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
