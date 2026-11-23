/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures a changed panel source overrides the detected page
 * language in the companion through either panel entry point.
 */
add_task(async function test_companion_uses_selected_source_language() {
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
        expectedFromLanguage: "es",
      });
      await FullPageTranslationsTestUtils.changeSelectedFromLanguage({
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
        { sourceLanguage: "fr", targetLanguage: "en" }
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
 * This test case ensures an unselected panel source opens the companion with
 * automatic detection rather than the detected page language.
 */
add_task(async function test_companion_preserves_unselected_source_language() {
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
        expectedFromLanguage: "es",
      });
      await FullPageTranslationsTestUtils.changeSelectedFromLanguage({
        langTag: "",
      });

      FullPageTranslationsTestUtils.assertSelectedFromLanguage({
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
        new URLSearchParams(url.hash.slice(1)).get("src"),
        "",
        "The companion URL keeps the source language unselected"
      );
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
  }
});
