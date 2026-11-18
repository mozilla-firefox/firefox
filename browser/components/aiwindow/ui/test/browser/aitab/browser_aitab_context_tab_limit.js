/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

/* import-globals-from ../head_aitab.js */
Services.scriptloader.loadSubScript(
  new URL("../head_aitab.js", getRootDirectory(gTestPath)).href,
  this
);

const { MAX_AITAB_CONTEXT_TABS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
);

const MORE_THAN_LIMIT_TAB_COUNT = MAX_AITAB_CONTEXT_TABS + 2;

const tabUrl = index => `https://example.com/?tab=${index}`;

describe("when selecting URLs to build an AITab", () => {
  describe("when the user chooses more tabs than the limit", () => {
    let gWin;
    let gMockEngineManager;
    let gCleanup;
    let gUrlsOldestFirst;
    let gUrlsNewestFirst;

    async function createAITabFromAllTabs() {
      const { selectedTab } = gWin.gBrowser;
      const selectMenu = await openTabContextMenu(gWin, selectedTab);
      await activateItemAndWait(
        selectMenu,
        gWin.document.getElementById("context_selectAllTabs")
      );
      const createMenu = await openTabContextMenu(gWin, selectedTab);
      return activateAndWaitForAITab(gWin, () =>
        activateItemAndWait(
          createMenu,
          gWin.document.getElementById("context_createAITab")
        )
      );
    }

    beforeEach(async () => {
      ({
        win: gWin,
        mockEngineManager: gMockEngineManager,
        cleanup: gCleanup,
      } = await openAITabTestWindow());
      gUrlsOldestFirst = Array.from(
        { length: MORE_THAN_LIMIT_TAB_COUNT },
        (_, i) => tabUrl(i)
      );
      gUrlsNewestFirst = gUrlsOldestFirst.toReversed();
      await openTabsOldestFirst(gWin, gUrlsOldestFirst);
    });

    afterEach(async () => {
      await gCleanup?.();
      gCleanup = null;
    });

    it("should keep only the most recently accessed tabs, up to the limit", async () => {
      const aiTab = await createAITabFromAllTabs();
      await assertAITabCreationPrompt(
        aiTab,
        gUrlsNewestFirst.slice(0, MAX_AITAB_CONTEXT_TABS),
        gMockEngineManager
      );
    });

    it("should keep an open AITab page that was accessed most recently", async () => {
      const smartPageUrl = "about:smartpage?page=some_page";
      await BrowserTestUtils.openNewForegroundTab(gWin.gBrowser, smartPageUrl);

      const aiTab = await createAITabFromAllTabs();
      await assertAITabCreationPrompt(
        aiTab,
        [
          smartPageUrl,
          ...gUrlsNewestFirst.slice(0, MAX_AITAB_CONTEXT_TABS - 1),
        ],
        gMockEngineManager
      );
    });
  });
});
