/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures that the Select Translations flow responds to the Translations
 * AIFeature becoming enabled, and then disabled again.
 */
add_task(
  async function test_select_translations_ai_feature_toggle_from_disabled() {
    const { cleanup, runInPage } = await loadTestPage({
      page: SELECT_TEST_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      autoDownloadFromRemoteSettings: true,
      prefs: [
        ["browser.translations.enable", false],
        ["browser.ai.control.default", "blocked"],
        ["browser.ai.control.translations", "blocked"],
        ["browser.translations.select.enable", true],
      ],
    });

    await TranslationsFeature.block();

    await SelectTranslationsTestUtils.assertContextMenuTranslateSelectionItem(
      runInPage,
      {
        selectSpanishSentence: true,
        openAtSpanishSentence: true,
        expectMenuItemVisible: false,
      },
      "The translate-selection context menu item is hidden when the Translations feature is disabled."
    );

    await TranslationsFeature.enable();

    await SelectTranslationsTestUtils.openPanel(runInPage, {
      selectSpanishSentence: true,
      openAtSpanishSentence: true,
      expectedFromLanguage: "es",
      expectedToLanguage: "en",
      onOpenPanel: SelectTranslationsTestUtils.assertPanelViewTranslated,
    });

    await SelectTranslationsTestUtils.clickDoneButton();

    await TranslationsFeature.block();

    await SelectTranslationsTestUtils.assertContextMenuTranslateSelectionItem(
      runInPage,
      {
        selectSpanishSentence: true,
        openAtSpanishSentence: true,
        expectMenuItemVisible: false,
      },
      "The translate-selection context menu item is hidden after disabling the Translations feature."
    );

    await cleanup();
  }
);

/**
 * This test case ensures that the Select Translations flow responds to the Translations
 * AIFeature becoming disabled, and then re-enabled again.
 */
add_task(
  async function test_select_translations_ai_feature_toggle_from_enabled() {
    const { cleanup, runInPage } = await loadTestPage({
      page: SELECT_TEST_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      autoDownloadFromRemoteSettings: true,
      prefs: [
        ["browser.ai.control.default", "available"],
        ["browser.ai.control.translations", "default"],
        ["browser.translations.select.enable", true],
      ],
    });

    await TranslationsFeature.enable();

    await SelectTranslationsTestUtils.openPanel(runInPage, {
      selectSpanishSentence: true,
      openAtSpanishSentence: true,
      expectedFromLanguage: "es",
      expectedToLanguage: "en",
      onOpenPanel: SelectTranslationsTestUtils.assertPanelViewTranslated,
    });

    await SelectTranslationsTestUtils.clickDoneButton();

    await TranslationsFeature.block();

    await SelectTranslationsTestUtils.assertContextMenuTranslateSelectionItem(
      runInPage,
      {
        selectSpanishSentence: true,
        openAtSpanishSentence: true,
        expectMenuItemVisible: false,
      },
      "The translate-selection context menu item is hidden after disabling the Translations feature."
    );

    await TranslationsFeature.enable();

    await SelectTranslationsTestUtils.openPanel(runInPage, {
      selectSpanishSentence: true,
      openAtSpanishSentence: true,
      expectedFromLanguage: "es",
      expectedToLanguage: "en",
      onOpenPanel: SelectTranslationsTestUtils.assertPanelViewTranslated,
    });

    await SelectTranslationsTestUtils.clickDoneButton();

    await cleanup();
  }
);
