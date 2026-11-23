/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures the displayed translation's language becomes the
 * companion source and its target remains unselected when no distinct preferred
 * language exists.
 */
add_task(
  async function test_companion_languages_after_translation_without_fallback() {
    const webLanguages = ["en"];
    const targetLanguage = "";
    for (const openFromAppMenu of [false, true]) {
      const {
        cleanup,
        tab: sourceTab,
        resolveDownloads,
        runInPage,
      } = await loadTestPage({
        page: SPANISH_PAGE_URL,
        webLanguages,
        languagePairs: LANGUAGE_PAIRS,
        prefs: [["browser.tabs.splitView.enabled", true]],
      });
      let companionTab = null;

      try {
        info(`Translate Spanish to English with preferences ${webLanguages}`);
        await FullPageTranslationsTestUtils.openPanel({
          openFromAppMenu,
          expectedFromLanguage: "es",
          expectedToLanguage: "en",
        });
        await FullPageTranslationsTestUtils.clickTranslateButton({
          downloadHandler: resolveDownloads,
        });
        await FullPageTranslationsTestUtils.assertOnlyIntersectingNodesAreTranslated(
          {
            fromLanguage: "es",
            toLanguage: "en",
            runInPage,
          }
        );

        info(
          "Change the panel target without changing the displayed translation"
        );
        await FullPageTranslationsTestUtils.openPanel({
          openFromAppMenu,
          onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewRevisit,
        });
        await FullPageTranslationsTestUtils.changeSelectedToLanguage({
          langTag: "es",
        });
        FullPageTranslationsTestUtils.assertSelectedToLanguage({
          langTag: "es",
        });

        info("Open the companion from the translated page");
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
        [companionTab] = await Promise.all([companionOpened, splitCreated]);

        await AboutTranslationsTestUtils.assertSelectedLanguages(
          companionTab.linkedBrowser,
          { sourceLanguage: "en", targetLanguage }
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
);
