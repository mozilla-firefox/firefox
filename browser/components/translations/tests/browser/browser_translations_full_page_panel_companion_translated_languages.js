/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures the displayed translation's language becomes the
 * companion source, with a distinct preferred target.
 */
add_task(async function test_companion_languages_after_translation() {
  const webLanguages = ["en", "fr"];
  const targetLanguage = "fr";
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
});

/**
 * This test case ensures a companion request is canceled if its source stops
 * being the selected, standalone document while its target is being resolved.
 */
add_task(async function test_companion_lookup_cancels_when_source_changes() {
  for (const change of ["switch tabs", "navigate", "split"]) {
    const {
      cleanup,
      tab: sourceTab,
      resolveDownloads,
      runInPage,
    } = await loadTestPage({
      page: SPANISH_PAGE_URL,
      webLanguages: ["en", "fr"],
      languagePairs: LANGUAGE_PAIRS,
      prefs: [
        ["browser.tabs.splitView.enabled", true],
        ["browser.tabs.splitview.hasUsed", true],
      ],
    });
    const panel = FullPageTranslationsPanel;
    const { onCompanionButton } = panel;
    let targetLookup;
    const initialTabs = new Set(gBrowser.tabs);
    let otherTab;
    let companionRequest;

    try {
      await FullPageTranslationsTestUtils.openPanel({
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

      otherTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
      await FullPageTranslationsTestUtils.openPanel({
        onOpenPanel: FullPageTranslationsTestUtils.assertPanelViewRevisit,
      });

      info(
        `Change the companion source during target-language lookup: ${change}`
      );
      let splitView;
      const initialTabCount = gBrowser.tabs.length;
      targetLookup =
        FullPageTranslationsTestUtils.holdNextPreferredTargetResult(["en"]);
      panel.onCompanionButton = () => {
        companionRequest = onCompanionButton.call(panel);
        return companionRequest;
      };
      await FullPageTranslationsTestUtils.clickCompanionButton();
      await targetLookup.entered;

      switch (change) {
        case "switch tabs":
          await BrowserTestUtils.switchTab(gBrowser, otherTab);
          break;
        case "navigate": {
          const url = "about:blank";
          const browser = sourceTab.linkedBrowser;
          const loaded = BrowserTestUtils.browserLoaded(browser, false, url);
          BrowserTestUtils.startLoadingURIString(browser, url);
          await loaded;
          break;
        }
        case "split":
          await SplitViewTranslationsTestUtils.openExistingTabsInSplitView(
            sourceTab,
            otherTab
          );
          splitView = sourceTab.splitview;
          Assert.equal(
            gBrowser.selectedTab,
            sourceTab,
            "The source tab remains selected after opening Split View"
          );
          break;
      }
      targetLookup.release();
      await companionRequest;

      Assert.equal(
        otherTab.splitview,
        splitView ?? null,
        "The other tab keeps its original Split View membership"
      );
      Assert.equal(
        gBrowser.tabs.length,
        initialTabCount,
        "A stale request does not open a companion tab"
      );
    } finally {
      delete panel.onCompanionButton;
      targetLookup?.restore();
      await companionRequest?.catch(() => {});
      sourceTab.splitview?.unsplitTabs();
      for (const tab of [...gBrowser.tabs]) {
        if (!initialTabs.has(tab)) {
          await BrowserTestUtils.removeTab(tab);
        }
      }
      await cleanup();
    }
  }
});
