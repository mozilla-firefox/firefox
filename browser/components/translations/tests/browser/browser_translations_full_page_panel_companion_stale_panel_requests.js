/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures the companion uses the initiating document's
 * languages when the user switches away and back while the panel opens.
 */
add_task(async function test_companion_languages_after_tab_switches() {
  const { cleanup, tab: sourceTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    webLanguages: ["en"],
    languagePairs: LANGUAGE_PAIRS,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  let panelSetup;
  let sourceLookup;
  let otherTab;
  let companionTab;

  try {
    otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      FRENCH_PAGE_URL
    );
    await BrowserTestUtils.switchTab(gBrowser, sourceTab);

    panelSetup = FullPageTranslationsTestUtils.holdNextLanguageListSetup();
    const panelOpened = FullPageTranslationsTestUtils.openPanel({
      expectedToLanguage: "en",
    });
    await panelSetup.entered;

    info("Switch away while panel setup is pending");
    await BrowserTestUtils.switchTab(gBrowser, otherTab);
    sourceLookup =
      FullPageTranslationsTestUtils.holdNextLanguageDetectionResult(sourceTab);
    panelSetup.release();
    await sourceLookup.entered;

    info("Switch back while language detection is pending");
    await BrowserTestUtils.switchTab(gBrowser, sourceTab);
    sourceLookup.release();
    await panelOpened;
    Assert.equal(gBrowser.selectedTab, sourceTab, "The source tab is active");

    info("Open the companion for the original Spanish document");
    const companionOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:translations"),
      true
    );
    await FullPageTranslationsTestUtils.clickCompanionButton();
    companionTab = await companionOpened;

    Assert.ok(sourceTab.splitview, "The original source tab enters Split View");
    Assert.equal(
      sourceTab.splitview,
      companionTab.splitview,
      "The companion opens beside the original source document"
    );
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      companionTab.linkedBrowser,
      { sourceLanguage: "es", targetLanguage: "en" }
    );
  } finally {
    panelSetup?.restore();
    sourceLookup?.restore();
    await cleanup();
    if (companionTab?.isConnected) {
      await BrowserTestUtils.removeTab(companionTab);
    }
    if (otherTab?.isConnected) {
      await BrowserTestUtils.removeTab(otherTab);
    }
  }
});

/**
 * This test case ensures a source-language lookup from a previous tab cannot
 * overwrite the languages used by the companion for the current tab.
 */
add_task(async function test_companion_ignores_stale_change_source_language() {
  const { cleanup, tab: sourceTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    webLanguages: ["en"],
    languagePairs: LANGUAGE_PAIRS_WITHOUT_SPANISH,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  const panel = FullPageTranslationsPanel;
  const { onChangeSourceLanguageButton } = panel;
  let sourceLookup;
  let changeSourceLanguage;
  let otherTab;
  let companionTab;

  try {
    otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      FRENCH_PAGE_URL
    );
    await BrowserTestUtils.switchTab(gBrowser, sourceTab);
    await FullPageTranslationsTestUtils.openPanel({
      openFromAppMenu: true,
      onOpenPanel:
        FullPageTranslationsTestUtils.assertPanelViewUnsupportedLanguage,
    });

    info("Hold the language lookup triggered by Change Source Language");
    sourceLookup =
      FullPageTranslationsTestUtils.holdNextLanguageDetectionResult(sourceTab);
    panel.onChangeSourceLanguageButton = event => {
      changeSourceLanguage = onChangeSourceLanguageButton.call(panel, event);
    };
    await FullPageTranslationsTestUtils.waitForPanelPopupEvent(
      "popuphidden",
      () => panel.elements.changeSourceLanguageButton.click()
    );
    await sourceLookup.entered;

    info("Open the French page's panel while the previous lookup is pending");
    await BrowserTestUtils.switchTab(gBrowser, otherTab);
    await FullPageTranslationsTestUtils.openPanel({
      expectedFromLanguage: "fr",
      expectedToLanguage: "en",
    });

    sourceLookup.release();
    await changeSourceLanguage;

    info("Open the companion after the stale lookup finishes");
    const companionOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:translations"),
      true
    );
    await FullPageTranslationsTestUtils.clickCompanionButton();
    companionTab = await companionOpened;
    Assert.ok(otherTab.splitview, "The French tab enters Split View");
    Assert.equal(
      otherTab.splitview,
      companionTab.splitview,
      "The companion opens beside the French tab"
    );
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      companionTab.linkedBrowser,
      { sourceLanguage: "fr", targetLanguage: "en" }
    );
  } finally {
    delete panel.onChangeSourceLanguageButton;
    sourceLookup?.restore();
    await changeSourceLanguage?.catch(() => {});
    otherTab?.splitview?.unsplitTabs();
    if (companionTab?.isConnected) {
      await BrowserTestUtils.removeTab(companionTab);
    }
    if (otherTab?.isConnected) {
      await BrowserTestUtils.removeTab(otherTab);
    }
    await cleanup();
  }
});

/**
 * This test case ensures a stale revisit lookup cannot change the
 * target language used by the companion for the current tab.
 */
add_task(async function test_companion_ignores_stale_revisit_lookup() {
  const { cleanup, tab, resolveDownloads } = await loadTestPage({
    page: FRENCH_PAGE_URL,
    webLanguages: ["en", "uk"],
    languagePairs: LANGUAGE_PAIRS_WITHOUT_SPANISH,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  const panel = FullPageTranslationsPanel;
  let sourceLookup;
  let revisitLookup;
  let otherTab;
  let companionTab;

  try {
    await FullPageTranslationsTestUtils.openPanel({
      expectedFromLanguage: "fr",
      expectedToLanguage: "en",
    });
    const engineReady =
      FullPageTranslationsTestUtils.waitForTranslationEngineReady(tab);
    await FullPageTranslationsTestUtils.clickTranslateButton({
      downloadHandler: resolveDownloads,
    });
    await engineReady;

    otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      SPANISH_PAGE_URL
    );
    await FullPageTranslationsTestUtils.openPanel({
      openFromAppMenu: true,
      onOpenPanel:
        FullPageTranslationsTestUtils.assertPanelViewUnsupportedLanguage,
    });

    info(
      "Start Change Source Language for the Spanish page and hold its language lookup"
    );
    sourceLookup =
      FullPageTranslationsTestUtils.holdNextLanguageDetectionResult(otherTab);
    await FullPageTranslationsTestUtils.waitForPanelPopupEvent(
      "popuphidden",
      () => click(panel.elements.changeSourceLanguageButton)
    );
    await sourceLookup.entered;

    info(
      "Switch to the translated French page and hold its revisit target lookup"
    );
    await BrowserTestUtils.switchTab(gBrowser, tab);
    revisitLookup = FullPageTranslationsTestUtils.holdNextPreferredTargetResult(
      ["fr", "en"]
    );
    await click(panel.buttonElements.button);
    await revisitLookup.entered;

    info(
      "Return to the Spanish page and let Change Source Language reopen its panel"
    );
    await BrowserTestUtils.switchTab(gBrowser, otherTab);
    await FullPageTranslationsTestUtils.waitForPanelPopupEvent(
      "popupshown",
      () => sourceLookup.release()
    );
    FullPageTranslationsTestUtils.assertSelectedToLanguage({ langTag: "en" });

    info(
      "Release the French page's lookup before opening the Spanish companion"
    );
    Assert.equal(
      await revisitLookup.release(),
      "uk",
      "The French page chose Ukrainian"
    );

    info("Open the companion for the Spanish page");
    const companionOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:translations"),
      true
    );
    await FullPageTranslationsTestUtils.clickCompanionButton();
    companionTab = await companionOpened;

    Assert.ok(otherTab.splitview, "The Spanish tab enters Split View");
    Assert.equal(
      companionTab.splitview,
      otherTab.splitview,
      "The companion opens beside the Spanish tab"
    );
    const url = new URL(companionTab.linkedBrowser.currentURI.spec);
    Assert.equal(
      new URLSearchParams(url.hash.slice(1)).get("trg"),
      "en",
      "The companion keeps the Spanish panel's English target"
    );
  } finally {
    sourceLookup?.restore();
    revisitLookup?.restore();
    await closeAllOpenPanelsAndMenus(window);
    otherTab?.splitview?.unsplitTabs();
    if (companionTab?.isConnected) {
      await BrowserTestUtils.removeTab(companionTab);
    }
    if (otherTab?.isConnected) {
      await BrowserTestUtils.removeTab(otherTab);
    }
    await cleanup();
  }
});
