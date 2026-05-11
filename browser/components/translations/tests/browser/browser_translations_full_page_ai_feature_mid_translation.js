/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case verifies that when the Translations feature is disabled while a translation
 * is active, the button disappears and the page is left in a partially translated state.
 * Upon re-enabling the Translations feature, the page in the active tab should automatically
 * reload to restore the original content.
 */
add_task(async function test_mid_translation_disable_and_reenable_active_tab() {
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

  info("Verifying that H1 is translated but final paragraph is not.");
  await FullPageTranslationsTestUtils.assertPageH1ContentIsTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage,
  });

  await FullPageTranslationsTestUtils.assertPageFinalParagraphContentIsNotTranslated(
    { runInPage }
  );

  info("Disabling the Translations feature while translation is active.");
  const browser = gBrowser.selectedBrowser;
  const reloadPromise = BrowserTestUtils.browserLoaded(browser);

  await TranslationsFeature.block();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden after disabling the Translations feature."
  );

  info("Scrolling to bottom to verify no new translations occur.");
  await scrollToBottomOfPage(runInPage);

  await FullPageTranslationsTestUtils.assertPageFinalParagraphContentIsNotTranslated(
    {
      runInPage,
      message:
        "The final paragraph should not be translated after feature was disabled.",
    }
  );

  info(
    "Re-enabling the Translations feature. Page should automatically reload."
  );
  await TranslationsFeature.enable();

  info("Waiting for page reload to complete.");
  await reloadPromise;

  info("Scrolling to top of page after reload.");
  await scrollToTopOfPage(runInPage);

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible after page reload."
  );

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);

  info("Verifying that a new translation can be started successfully.");
  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewDefault,
  });

  await FullPageTranslationsTestUtils.clickTranslateButton();

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage,
  });

  await cleanup();
});

/**
 * This test case verifies that when the Translations feature is disabled while a translation
 * is active, the button disappears and the page is left in a partially translated state.
 * Upon re-enabling the Translations feature, the page in the background tab should show the
 * Translations button, but not automatically refresh. Upon switching to the background tab,
 * the page should refresh itself to restore the original content, after which a new
 * translation can be invoked.
 */
add_task(
  async function test_mid_translation_disable_and_reenable_background_tab() {
    const { cleanup, runInPage: runInPage1 } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      languagePairs: LANGUAGE_PAIRS,
      autoDownloadFromRemoteSettings: true,
      prefs: [
        ["browser.ai.control.default", "available"],
        ["browser.ai.control.translations", "default"],
      ],
    });

    const tab1 = gBrowser.selectedTab;

    await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);

    await TranslationsFeature.enable();
    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: false, icon: true },
      "The URL bar translate button is visible in tab 1."
    );

    await FullPageTranslationsTestUtils.openPanel({
      expectedFromLanguage: "es",
      expectedToLanguage: "en",
      onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewIntro,
    });

    await FullPageTranslationsTestUtils.clickTranslateButton();

    await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
      {
        fromLanguage: "es",
        toLanguage: "en",
        runInPage: runInPage1,
      }
    );

    info("Verifying that H1 is translated but final paragraph is not.");
    await FullPageTranslationsTestUtils.assertPageH1ContentIsTranslated({
      fromLanguage: "es",
      toLanguage: "en",
      runInPage: runInPage1,
    });

    await FullPageTranslationsTestUtils.assertPageFinalParagraphContentIsNotTranslated(
      { runInPage: runInPage1 }
    );

    info("Disabling the Translations feature while translation is active.");
    await TranslationsFeature.block();

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: false },
      "The URL bar translate button is hidden after disabling the Translations feature."
    );

    info("Opening a second tab.");
    const { removeTab: removeTab2 } = await addTab(
      SPANISH_PAGE_URL,
      "Opening second Spanish tab"
    );

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: false },
      "The URL bar translate button is hidden in tab 2 while feature is disabled."
    );

    info("Re-enabling the Translations feature from tab 2.");
    await TranslationsFeature.enable();

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: false, icon: true },
      "The URL bar translate button is visible in tab 2 after re-enabling."
    );

    info("Setting up reload promise for tab 1 before switching to it.");
    const browser1 = tab1.linkedBrowser;
    const reloadPromise = BrowserTestUtils.browserLoaded(browser1);

    info("Switching back to tab 1. Page should reload.");
    await switchTab(tab1, "Switching to tab 1");

    info("Waiting for tab 1 to reload.");
    await reloadPromise;

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: false, icon: true },
      "The URL bar translate button is visible in tab 1 after reload, indicating page is not translated."
    );

    await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage1);

    info("Verifying that a new translation can be started successfully.");
    await FullPageTranslationsTestUtils.openPanel({
      expectedFromLanguage: "es",
      expectedToLanguage: "en",
      onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewDefault,
    });

    await FullPageTranslationsTestUtils.clickTranslateButton();

    await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
      {
        fromLanguage: "es",
        toLanguage: "en",
        runInPage: runInPage1,
      }
    );

    await FullPageTranslationsTestUtils.assertTranslationsButton(
      { button: true, circleArrows: false, locale: true, icon: true },
      "The URL bar translate button shows locale indicator after translation."
    );

    await removeTab2();
    await cleanup();
  }
);

/**
 * This test case verifies that when translation is active in a background tab,
 * and the Translations feature is disabled from an active tab that has no actor
 * instance (such as about:blank), then the translate button is properly
 * hidden when switching back to a background tab that had an active translation.
 */
add_task(async function test_mid_translation_disable_from_tab_without_actor() {
  const { cleanup, runInPage } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
    ],
  });

  const tab1 = gBrowser.selectedTab;

  await FullPageTranslationsTestUtils.assertPageIsNotTranslated(runInPage);

  await TranslationsFeature.enable();
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible in tab 1."
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

  info("Opening about:blank tab.");
  const { removeTab } = await addTab("about:blank", "Opening about:blank tab");

  info("Disabling the Translations feature from about:blank tab.");
  await TranslationsFeature.block();

  info("Switching back to tab 1 with active translation.");
  await switchTab(tab1, "Switching to tab 1");

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden in tab 1 after disabling from about:blank."
  );

  await removeTab();
  await cleanup();
});

/**
 * This test case verifies behavior when the Translations feature is disabled while language models
 * are still loading, then the feature is re-enabled and a subsequent translation attempt is invoked
 * for the same language pair.
 */
add_task(async function test_disable_feature_during_model_loading() {
  const { cleanup, runInPage, resolveDownloads } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    autoDownloadFromRemoteSettings: false,
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

  info(
    "Clicking translate button without resolving downloads to simulate models loading."
  );
  await FullPageTranslationsTestUtils.clickTranslateButton({
    downloadHandler: null,
  });

  info("Waiting for the button to show the loading indicator.");
  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: true, locale: false, icon: true },
    "The button shows the loading indicator while models are downloading."
  );

  info(
    "Disabling the Translations feature while models are still loading (downloads not resolved)."
  );
  await TranslationsFeature.block();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: false },
    "The URL bar translate button is hidden after disabling the Translations feature."
  );

  info("Re-enabling the Translations feature.");
  await TranslationsFeature.enable();

  await FullPageTranslationsTestUtils.assertTranslationsButton(
    { button: true, circleArrows: false, locale: false, icon: true },
    "The URL bar translate button is visible after re-enabling."
  );

  info("Attempting to translate again with the same language pair.");
  await FullPageTranslationsTestUtils.openPanel({
    expectedFromLanguage: "es",
    expectedToLanguage: "en",
    onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewDefault,
  });

  info("This time, resolve downloads so translation can complete.");
  await FullPageTranslationsTestUtils.clickTranslateButton({
    downloadHandler: resolveDownloads,
  });

  await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated({
    fromLanguage: "es",
    toLanguage: "en",
    runInPage,
  });

  await cleanup();
});
