/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case verifies that language detection works correctly when the
 * feature is disabled and immediately re-enabled on the same page without
 * navigation.
 */
add_task(async function test_toggle_without_navigation_detects_language() {
  const { cleanup } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
    ],
  });

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible on Spanish page."
  );

  info("Disabling and immediately re-enabling without navigation.");
  await TranslationsFeature.block();
  await TranslationsFeature.enable();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible after toggling."
  );

  info("Verifying Spanish is still detected correctly by translating.");
  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await cleanup();
});

/**
 * This test case verifies that when the Translations feature is disabled, then the user
 * navigates to a different-language page, and the feature is re-enabled, the
 * detected languages correctly reflect the new page's language.
 */
add_task(async function test_navigation_while_disabled_detects_current_page() {
  const { cleanup } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
    ],
  });

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible on Spanish page."
  );

  info("Disabling the Translations feature while on Spanish page.");
  await TranslationsFeature.block();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden after disabling the Translations feature."
  );

  await navigate("Navigating to French page while feature is disabled", {
    url: FRENCH_PAGE_URL,
  });

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is still hidden on French page."
  );

  info("Re-enabling the Translations feature on French page.");
  await TranslationsFeature.enable();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible after re-enabling."
  );

  info("Verifying French is detected by translating the page.");
  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "fr",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await cleanup();
});

/**
 * This test case verifies that after multiple navigations while the Translations
 * feature is disabled, the detected languages correctly reflect the current page's
 * language when the feature is re-enabled, not any previously visited pages.
 */
add_task(async function test_multiple_navigations_while_disabled() {
  const { cleanup } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
    ],
  });

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible on Spanish page."
  );

  info("Disabling the Translations feature on Spanish page.");
  await TranslationsFeature.block();

  await navigate("Navigating to French page while feature is disabled", {
    url: FRENCH_PAGE_URL,
  });

  await navigate(
    "Navigating back to Spanish page, still with feature disabled",
    {
      url: SPANISH_PAGE_URL,
    }
  );

  info("Re-enabling the Translations feature on Spanish page.");
  await TranslationsFeature.enable();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible after re-enabling."
  );

  info("Verifying Spanish is detected by translating the page.");
  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await cleanup();
});
