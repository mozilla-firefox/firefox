/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case verifies that when the Translations feature is re-enabled for pages
 * that would auto-translate, the active tab translates immediately, but background tabs
 * will not translate until invoked, in this case by page reload.
 */
add_task(
  async function test_auto_translate_on_reenable_requires_reload_in_background_tab() {
    const { cleanup, runInPage: runInPage1 } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      autoDownloadFromRemoteSettings: true,
      prefs: [
        ["browser.translations.enable", false],
        ["browser.ai.control.default", "blocked"],
        ["browser.ai.control.translations", "blocked"],
        ["browser.translations.alwaysTranslateLanguages", "es"],
      ],
    });

    const tab1 = gBrowser.selectedTab;

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: false },
      "The URL bar translate button is hidden in tab 1 with Translations feature disabled."
    );

    await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);

    info("Opening a second tab while Translations feature is still disabled.");
    const { removeTab: removeTab2, runInPage: runInPage2 } = await addTab(
      SPANISH_PAGE_URL,
      "Opening second Spanish tab"
    );

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: false },
      "The URL bar translate button is hidden in tab 2 with Translations feature disabled."
    );

    await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage2);

    info("Re-enabling the Translations feature from tab 2 (active tab).");
    await TranslationsFeature.enable();

    await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
      {
        fromLanguage: "es",
        toLanguage: "en",
        runInPage: runInPage2,
      }
    );

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: true, icon: true },
      "The URL bar translate button shows locale indicator after auto-translation in tab 2."
    );

    info("Switching to tab 1 to verify it does not auto-translate.");
    await switchTab(tab1, "Switching to tab 1");

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: false, icon: true },
      "The URL bar translate button is visible in tab 1 but without circleArrows, indicating no auto-translate occurred."
    );

    await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);

    await navigate("Manually reloading tab 1. Auto-translate should trigger", {
      url: SPANISH_PAGE_URL,
    });

    await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
      {
        fromLanguage: "es",
        toLanguage: "en",
        runInPage: runInPage1,
      }
    );

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: true, icon: true },
      "The URL bar translate button shows locale indicator after auto-translation."
    );

    await removeTab2();
    await cleanup();
  }
);

/**
 * This test case verifies that when the Translations feature is re-enabled for pages
 * that would auto-translate, the active tab translates immediately, but background tabs
 * will not translate until invoked, in this case via the Translations panel.
 */
add_task(async function test_manual_translate_on_reenable_in_background_tab() {
  const { cleanup, runInPage: runInPage1 } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.translations.enable", false],
      ["browser.ai.control.default", "blocked"],
      ["browser.ai.control.translations", "blocked"],
      ["browser.translations.alwaysTranslateLanguages", "es"],
    ],
  });

  const tab1 = gBrowser.selectedTab;

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden in tab 1 with Translations feature disabled."
  );

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);

  info("Opening a second tab while Translations feature is still disabled.");
  const { removeTab: removeTab2, runInPage: runInPage2 } = await addTab(
    SPANISH_PAGE_URL,
    "Opening second Spanish tab"
  );

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden in tab 2 with Translations feature disabled."
  );

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage2);

  info("Re-enabling the Translations feature from tab 2 (active tab).");
  await TranslationsFeature.enable();

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage: runInPage2,
  });

  info("Switching to tab 1 to verify it does not auto-translate.");
  await switchTab(tab1, "Switching to tab 1");

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible in tab 1 but no auto-translate occurred."
  );

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);

  info("Manually translating tab 1 by opening the panel.");
  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewDefault,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage: runInPage1,
  });

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: true, icon: true },
    "The URL bar translate button shows locale indicator after translation."
  );

  await removeTab2();
  await cleanup();
});
