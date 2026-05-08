/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures that the URL-bar translate button responds to the
 * Translations feature becoming enabled when the page was loaded while
 * the feature was disabled, and that Translations works correctly once invoked.
 */
add_task(async function test_urlbar_button_ai_feature_toggle_from_disabled() {
  const { cleanup, runInPage } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.translations.enable", false],
      ["browser.ai.control.default", "blocked"],
      ["browser.ai.control.translations", "blocked"],
    ],
  });

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden when the page loads with Translations feature disabled."
  );

  await TranslationsFeature.enable();
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible when the Translations feature is enabled."
  );

  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage,
  });

  await FullPageTranslationsTestUtils.openPanel({
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewRevisit,
  });

  await FullPageTranslationsTestUtils.clickRestoreButton();

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);

  await TranslationsFeature.block();
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden after disabling the Translations feature."
  );

  await cleanup();
});

/**
 * This test case ensures that the URL bar translate button responds to the
 * Translations feature becoming disabled, and then enabled again, and that
 * Translations continues to function correctly when invoked after re-enabling.
 */
add_task(async function test_urlbar_button_ai_feature_toggle_from_enabled() {
  const { cleanup, runInPage } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
    ],
  });

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);

  await TranslationsFeature.enable();
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible when the Translations feature is enabled."
  );

  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage,
  });

  await FullPageTranslationsTestUtils.openPanel({
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewRevisit,
  });

  await FullPageTranslationsTestUtils.clickRestoreButton();

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);

  await TranslationsFeature.block();
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden when the Translations feature is disabled."
  );

  await TranslationsFeature.enable();
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible after enabling the Translations feature."
  );

  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewDefault,
  });

  await FullPageTranslationsTestUtils.changeSelectedToLanguage({
    langTag: "fr",
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "fr",
    runInPage,
  });

  await cleanup();
});

/**
 * This test case verifies that the URL-bar translate button state is correctly
 * updated across multiple tabs when the Translations feature is toggled. When
 * disabled, the button should be hidden in all tabs, and when re-enabled, the
 * button should be visible in each tab with a translatable page.
 */
add_task(async function test_urlbar_button_ai_feature_toggle_multiple_tabs() {
  const { runInPage: runInPage1, cleanup: cleanupWindow1 } = await loadTestPage(
    {
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      prefs: [
        ["browser.ai.control.default", "available"],
        ["browser.ai.control.translations", "default"],
      ],
    }
  );

  const tab1 = gBrowser.selectedTab;

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible in tab 1."
  );

  const { removeTab, runInPage: runInPage2 } = await addTab(
    SPANISH_PAGE_URL,
    "Creating a second Spanish page tab"
  );

  const tab2 = gBrowser.selectedTab;

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage2);
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible in tab 2."
  );

  await TranslationsFeature.block();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden in tab 2 after disabling."
  );

  await switchTab(tab1, "Switching to tab 1");

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden in tab 1 after disabling."
  );

  await TranslationsFeature.enable();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible in tab 1 after re-enabling."
  );

  await switchTab(tab2, "Switching to tab 2");

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible in tab 2 after re-enabling."
  );

  await removeTab();
  await cleanupWindow1();
});

/**
 * This test case verifies that when the Translations feature is disabled from an active
 * tab that has no actor (such as about:blank), then translate button is properly hidden
 * when switching back to a background tab that previously would have had the button visible.
 */
add_task(
  async function test_urlbar_button_ai_feature_disable_from_tab_without_actor() {
    const { cleanup } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      autoDownloadFromRemoteSettings: true,
      prefs: [
        ["browser.ai.control.default", "available"],
        ["browser.ai.control.translations", "default"],
      ],
    });

    const tab1 = gBrowser.selectedTab;

    await TranslationsFeature.enable();
    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: false, icon: true },
      "The URL bar translate button is visible in tab 1."
    );

    const { removeTab } = await addTab(
      "about:blank",
      "Opening about:blank tab"
    );

    await TranslationsFeature.block();

    await switchTab(tab1, "Switching to tab 1");

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: false },
      "The URL bar translate button is hidden in tab 1 after disabling from about:blank."
    );

    await removeTab();
    await cleanup();
  }
);

/**
 * This test case verifies that when the Translations feature is enabled from an active tab
 * that has no actor (such as about:blank), then the translate button is properly shown when
 * switching back to a background tab whose page would display the translate url-bar button.
 */
add_task(
  async function test_urlbar_button_ai_feature_enable_from_tab_without_actor() {
    const { cleanup } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      autoDownloadFromRemoteSettings: true,
      prefs: [
        ["browser.ai.control.default", "available"],
        ["browser.ai.control.translations", "blocked"],
      ],
    });

    const tab1 = gBrowser.selectedTab;

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: false },
      "The URL bar translate button is hidden when feature is disabled."
    );

    const { removeTab } = await addTab(
      "about:blank",
      "Opening about:blank tab"
    );

    await TranslationsFeature.enable();

    await switchTab(tab1, "Switching to tab 1");

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: false, icon: true },
      "The URL bar translate button is visible in tab 1 after enabling from about:blank."
    );

    await removeTab();
    await cleanup();
  }
);
