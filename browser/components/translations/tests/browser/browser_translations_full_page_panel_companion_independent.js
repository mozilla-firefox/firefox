/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures opening a second companion leaves the first companion
 * and its Split View unchanged.
 */
add_task(async function test_independent_companions() {
  const { cleanup, tab: firstSourceTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  let firstCompanionTab = null;
  let secondSourceTab = null;
  let secondCompanionTab = null;

  try {
    info("Open the first source page's translations companion");
    firstCompanionTab =
      await FullPageTranslationsTestUtils.openTranslationsCompanion({
        openFromAppMenu: false,
      });
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      firstCompanionTab.linkedBrowser,
      { sourceLanguage: "es", targetLanguage: "en" }
    );
    const firstSplitView = firstSourceTab.splitview;
    Assert.ok(firstSplitView, "The first source page is in Split View");
    Assert.equal(
      firstCompanionTab.splitview,
      firstSplitView,
      "The first companion is paired with its source page"
    );

    info("Open a second source page while the first companion remains open");
    secondSourceTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      BLANK_PAGE
    );
    await loadNewPage(secondSourceTab.linkedBrowser, FRENCH_PAGE_URL);
    secondCompanionTab =
      await FullPageTranslationsTestUtils.openTranslationsCompanion({
        openFromAppMenu: false,
      });

    await AboutTranslationsTestUtils.assertSelectedLanguages(
      secondCompanionTab.linkedBrowser,
      { sourceLanguage: "fr", targetLanguage: "en" }
    );
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      firstCompanionTab.linkedBrowser,
      { sourceLanguage: "es", targetLanguage: "en" }
    );

    Assert.notEqual(
      firstCompanionTab,
      secondCompanionTab,
      "The second source page gets a new translations companion"
    );
    Assert.ok(secondSourceTab.splitview, "The second source page is split");
    Assert.notEqual(
      firstSplitView,
      secondSourceTab.splitview,
      "Each source page has its own Split View"
    );
    Assert.equal(
      secondCompanionTab.splitview,
      secondSourceTab.splitview,
      "The second companion is paired with its own source page"
    );
    Assert.equal(
      firstSourceTab.splitview,
      firstSplitView,
      "Opening the second companion preserves the first Split View"
    );
    Assert.equal(
      firstCompanionTab.splitview,
      firstSplitView,
      "The first companion remains paired with its original source page"
    );
  } finally {
    firstSourceTab.splitview?.unsplitTabs();
    secondSourceTab?.splitview?.unsplitTabs();
    for (const tab of [
      firstCompanionTab,
      secondCompanionTab,
      secondSourceTab,
    ]) {
      if (tab?.isConnected) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
    await cleanup();
  }
});

/**
 * This test case ensures separate source pages keep independent companions and
 * Split Views when opened through the app menu.
 */
add_task(async function test_independent_companions_app_menu() {
  const { cleanup, tab: firstSourceTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  let firstCompanionTab = null;
  let secondSourceTab = null;
  let secondCompanionTab = null;

  try {
    info("Open the first source page's translations companion");
    firstCompanionTab =
      await FullPageTranslationsTestUtils.openTranslationsCompanion({
        openFromAppMenu: true,
      });
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      firstCompanionTab.linkedBrowser,
      { sourceLanguage: "es", targetLanguage: "en" }
    );
    const firstSplitView = firstSourceTab.splitview;
    Assert.ok(firstSplitView, "The first source page is in Split View");
    Assert.equal(
      firstCompanionTab.splitview,
      firstSplitView,
      "The first companion is paired with its source page"
    );

    info("Open a second source page while the first companion remains open");
    secondSourceTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      BLANK_PAGE
    );
    await loadNewPage(secondSourceTab.linkedBrowser, FRENCH_PAGE_URL);
    secondCompanionTab =
      await FullPageTranslationsTestUtils.openTranslationsCompanion({
        openFromAppMenu: true,
      });

    await AboutTranslationsTestUtils.assertSelectedLanguages(
      secondCompanionTab.linkedBrowser,
      { sourceLanguage: "fr", targetLanguage: "en" }
    );
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      firstCompanionTab.linkedBrowser,
      { sourceLanguage: "es", targetLanguage: "en" }
    );

    Assert.notEqual(
      firstCompanionTab,
      secondCompanionTab,
      "The second source page gets a new translations companion"
    );
    Assert.ok(secondSourceTab.splitview, "The second source page is split");
    Assert.notEqual(
      firstSplitView,
      secondSourceTab.splitview,
      "Each source page has its own Split View"
    );
    Assert.equal(
      secondCompanionTab.splitview,
      secondSourceTab.splitview,
      "The second companion is paired with its own source page"
    );
    Assert.equal(
      firstSourceTab.splitview,
      firstSplitView,
      "Opening the second companion preserves the first Split View"
    );
    Assert.equal(
      firstCompanionTab.splitview,
      firstSplitView,
      "The first companion remains paired with its original source page"
    );
  } finally {
    firstSourceTab.splitview?.unsplitTabs();
    secondSourceTab?.splitview?.unsplitTabs();
    for (const tab of [
      firstCompanionTab,
      secondCompanionTab,
      secondSourceTab,
    ]) {
      if (tab?.isConnected) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
    await cleanup();
  }
});
