/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures the companion inherits the panel's initial source and
 * target selections, even when a recent target matches the source language.
 */
add_task(async function test_companion_initializes_preferred_languages() {
  for (const {
    page,
    webLanguages,
    recentTargetLanguage,
    sourceLanguage,
    targetLanguage,
  } of [
    {
      page: FRENCH_PAGE_URL,
      webLanguages: ["de", "es", "en"],
      recentTargetLanguage: "",
      sourceLanguage: "fr",
      targetLanguage: "es",
    },
    {
      page: SPANISH_PAGE_URL,
      webLanguages: ["de", "fr", "en"],
      recentTargetLanguage: "es",
      sourceLanguage: "es",
      targetLanguage: "fr",
    },
  ]) {
    for (const openFromAppMenu of [false, true]) {
      info(
        `Open a ${sourceLanguage} companion with preferences ${webLanguages}, app menu: ${openFromAppMenu}`
      );
      const { cleanup, tab: sourceTab } = await loadTestPage({
        page,
        webLanguages,
        languagePairs: LANGUAGE_PAIRS,
        prefs: [
          ["browser.tabs.splitView.enabled", true],
          [
            "browser.translations.mostRecentTargetLanguages",
            recentTargetLanguage,
          ],
        ],
      });
      let companionTab = null;

      try {
        companionTab =
          await FullPageTranslationsTestUtils.openTranslationsCompanion({
            openFromAppMenu,
          });
        await AboutTranslationsTestUtils.assertSelectedLanguages(
          companionTab.linkedBrowser,
          { sourceLanguage, targetLanguage }
        );
      } finally {
        sourceTab.splitview?.unsplitTabs();
        if (companionTab?.isConnected) {
          await BrowserTestUtils.removeTab(companionTab);
        }
        await cleanup();
      }
    }
  }
});

/**
 * This test case ensures an unsupported-language warning has no companion
 * action, but changing the source enables it with automatic detection.
 */
add_task(async function test_companion_with_unsupported_source_language() {
  const { cleanup, tab: sourceTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS_WITHOUT_SPANISH,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  let companionTab = null;

  try {
    await FullPageTranslationsTestUtils.openPanel({
      openFromAppMenu: true,
      onOpenPanel:
        FullPageTranslationsTestUtils.assertPanelViewUnsupportedLanguage,
    });
    FullPageTranslationsTestUtils.assertCompanionButtonVisibility({
      visible: false,
    });

    info("Changing the source language exposes usable translation controls");
    await FullPageTranslationsTestUtils.clickChangeSourceLanguageButton({
      intro: true,
    });
    FullPageTranslationsTestUtils.assertCompanionButtonVisibility({
      visible: true,
    });

    const companionOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:translations"),
      true
    );
    const splitCreated = BrowserTestUtils.waitForEvent(
      gBrowser.tabContainer,
      "SplitViewCreated"
    );
    await FullPageTranslationsTestUtils.clickCompanionButton();
    companionTab = await companionOpened;
    await splitCreated;
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      companionTab.linkedBrowser,
      { sourceLanguage: "detect", targetLanguage: "en" }
    );
  } finally {
    sourceTab.splitview?.unsplitTabs();
    if (companionTab?.isConnected) {
      await BrowserTestUtils.removeTab(companionTab);
    }
    await cleanup();
  }
});
