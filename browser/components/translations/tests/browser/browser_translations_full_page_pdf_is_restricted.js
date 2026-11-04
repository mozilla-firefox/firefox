/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The Spanish PDF fixture for full-page translation restrictions.
 *
 * @type {string}
 */
const SPANISH_PDF_PAGE_URL = SPANISH_PAGE_URL.replace(".html", ".pdf");

/**
 * This test case ensures that detecting a supported language does not expose
 * full-page translation controls on a PDF.
 */
add_task(async function test_spanish_pdf_manual_translation_is_restricted() {
  const { tab, cleanup } = await loadTestPage({
    page: BLANK_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.translations.enable", false],
      ["browser.ai.control.translations", "blocked"],
    ],
  });

  try {
    info("Loading the Spanish PDF before enabling Translations.");
    await SharedTranslationsTestUtils.loadPdfPage({
      browser: tab.linkedBrowser,
      url: SPANISH_PDF_PAGE_URL,
    });
    await SharedTranslationsTestUtils.assertDetectedPageLanguage({
      browser: tab.linkedBrowser,
      language: "es",
      confident: true,
    });

    await TranslationsFeature.enable();
    await FullPageTranslationsTestUtils.assertPageTranslationIsRestricted({
      browser: tab.linkedBrowser,
      languagePair: { sourceLanguage: "es", targetLanguage: "en" },
    });

    info("Reloading the PDF before enabling Translations again.");
    await TranslationsFeature.block();
    await SharedTranslationsTestUtils.loadPdfPage({
      browser: tab.linkedBrowser,
      url: SPANISH_PDF_PAGE_URL,
      reload: true,
    });
    await SharedTranslationsTestUtils.assertDetectedPageLanguage({
      browser: tab.linkedBrowser,
      language: "es",
      confident: true,
    });

    await TranslationsFeature.enable();
    await FullPageTranslationsTestUtils.assertPageTranslationIsRestricted({
      browser: tab.linkedBrowser,
      languagePair: { sourceLanguage: "es", targetLanguage: "en" },
    });
  } finally {
    await cleanup();
  }
});

/**
 * This test case ensures that an always-translate preference cannot override
 * PDF page restrictions.
 */
add_task(async function test_spanish_pdf_auto_translation_is_restricted() {
  const { tab, cleanup } = await loadTestPage({
    page: BLANK_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.translations.enable", false],
      ["browser.ai.control.translations", "blocked"],
      ["browser.translations.alwaysTranslateLanguages", "es"],
    ],
  });

  try {
    info("Loading the Spanish PDF with always-translate enabled for Spanish.");
    await SharedTranslationsTestUtils.loadPdfPage({
      browser: tab.linkedBrowser,
      url: SPANISH_PDF_PAGE_URL,
    });
    await SharedTranslationsTestUtils.assertDetectedPageLanguage({
      browser: tab.linkedBrowser,
      language: "es",
      confident: true,
    });

    await TranslationsFeature.enable();
    await FullPageTranslationsTestUtils.assertPageTranslationIsRestricted({
      browser: tab.linkedBrowser,
      languagePair: { sourceLanguage: "es", targetLanguage: "en" },
    });

    info("Reloading the Spanish PDF with always-translate still enabled.");
    await TranslationsFeature.block();
    await SharedTranslationsTestUtils.loadPdfPage({
      browser: tab.linkedBrowser,
      url: SPANISH_PDF_PAGE_URL,
      reload: true,
    });
    await SharedTranslationsTestUtils.assertDetectedPageLanguage({
      browser: tab.linkedBrowser,
      language: "es",
      confident: true,
    });

    await TranslationsFeature.enable();
    await FullPageTranslationsTestUtils.assertPageTranslationIsRestricted({
      browser: tab.linkedBrowser,
      languagePair: { sourceLanguage: "es", targetLanguage: "en" },
    });
  } finally {
    await cleanup();
  }
});
