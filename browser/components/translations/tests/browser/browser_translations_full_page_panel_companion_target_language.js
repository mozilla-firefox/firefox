/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures a changed panel target overrides the preferred target
 * in the companion through either panel entry point.
 */
add_task(async function test_companion_uses_selected_target_language() {
  for (const openFromAppMenu of [false, true]) {
    const { cleanup, tab: sourceTab } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      webLanguages: ["en"],
      languagePairs: LANGUAGE_PAIRS,
      prefs: [["browser.tabs.splitView.enabled", true]],
    });
    let companionTab = null;

    try {
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        expectedToLanguage: "en",
      });
      await FullPageTranslationsTestUtils.changeSelectedToLanguage({
        langTag: "fr",
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
      [companionTab] = await Promise.all([companionOpened, splitCreated]);

      await AboutTranslationsTestUtils.assertSelectedLanguages(
        companionTab.linkedBrowser,
        { sourceLanguage: "es", targetLanguage: "fr" }
      );
    } finally {
      sourceTab.splitview?.unsplitTabs();
      if (companionTab?.isConnected) {
        await BrowserTestUtils.removeTab(companionTab);
      }
      await cleanup();
    }
  }
});

/**
 * This test case ensures an unselected panel target remains unselected in the
 * companion, even when the page language is the only preferred target.
 */
add_task(async function test_companion_preserves_unselected_target_language() {
  for (const {
    page,
    webLanguages,
    openFromAppMenu,
    sourceLanguage,
    expectedToLanguage,
  } of [
    {
      page: SPANISH_PAGE_URL,
      webLanguages: ["fr", "en"],
      openFromAppMenu: false,
      sourceLanguage: "es",
      expectedToLanguage: "fr",
    },
    {
      page: SPANISH_PAGE_URL,
      webLanguages: ["fr", "en"],
      openFromAppMenu: true,
      sourceLanguage: "es",
      expectedToLanguage: "fr",
    },
    {
      page: ENGLISH_PAGE_URL,
      webLanguages: ["en"],
      openFromAppMenu: true,
      sourceLanguage: "en",
      expectedToLanguage: "",
    },
  ]) {
    const { cleanup, tab: sourceTab } = await loadTestPage({
      page,
      webLanguages,
      languagePairs: LANGUAGE_PAIRS,
      prefs: [["browser.tabs.splitView.enabled", true]],
    });
    let companionTab = null;

    try {
      await FullPageTranslationsTestUtils.openPanel({
        openFromAppMenu,
        expectedToLanguage,
      });
      if (expectedToLanguage) {
        await FullPageTranslationsTestUtils.changeSelectedToLanguage({
          langTag: "",
        });
      }

      FullPageTranslationsTestUtils.assertSelectedToLanguage({
        langTag: "",
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
      [companionTab] = await Promise.all([companionOpened, splitCreated]);

      const url = new URL(companionTab.linkedBrowser.currentURI.spec);
      Assert.equal(
        new URLSearchParams(url.hash.slice(1)).get("trg"),
        "",
        "The companion URL keeps the target language unselected"
      );
      await AboutTranslationsTestUtils.assertSelectedLanguages(
        companionTab.linkedBrowser,
        {
          sourceLanguage,
          targetLanguage: "",
        }
      );
    } finally {
      sourceTab.splitview?.unsplitTabs();
      if (companionTab?.isConnected) {
        await BrowserTestUtils.removeTab(companionTab);
      }
      await cleanup();
    }
  }
});
