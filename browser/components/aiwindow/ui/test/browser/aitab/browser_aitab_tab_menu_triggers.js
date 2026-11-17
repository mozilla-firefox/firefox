/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * The "Create AI Tab" entries in tab context menus:
 * when they show, and the background tab activating one opens.
 */

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

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const TEST_URL = "https://example.com/";

async function hidePopupAndWait(menu) {
  const hidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.hidePopup();
  await hidden;
}

describe("Create AI Tab menu triggers in a Smart Window", () => {
  let gWin;
  let gRestoreSignIn;
  let gMockEngineManager;

  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.aitab.enabled", true],
        ["browser.smartwindow.aitab.menus", true],
      ],
    });
    gRestoreSignIn = skipSignIn();
    gMockEngineManager = new MockEngineManager();
    gWin = await openAIWindow();
  });

  afterEach(async () => {
    // Unanswered requests hold suspended async chains that keep the window
    // from being GC'd.
    await gMockEngineManager.rejectAllRequests();
    gMockEngineManager.cleanupMocks();
    gRestoreSignIn();
    await BrowserTestUtils.closeWindow(gWin);
    await SpecialPowers.popPrefEnv();
  });

  describe("tab context menu", () => {
    describe("on an http tab", () => {
      let gTab;
      let gMenu;

      beforeEach(async () => {
        gTab = await BrowserTestUtils.openNewForegroundTab(
          gWin.gBrowser,
          TEST_URL
        );
        gMenu = await openTabContextMenu(gWin, gTab);
      });

      it("shows the entry", async () => {
        Assert.ok(
          !gWin.document.getElementById("context_createAITab").hidden,
          "Create AI Tab is shown for an http tab"
        );
        await hidePopupAndWait(gMenu);
      });

      it("opens a background tab whose chat asks for the tab URL", async () => {
        const aiTab = await activateAndWaitForAITab(gWin, () =>
          activateItemAndWait(
            gMenu,
            gWin.document.getElementById("context_createAITab")
          )
        );
        await assertAITabCreationPrompt(aiTab, [TEST_URL], gMockEngineManager);
      });

      it("opens another tab and conversation on a second activation", async () => {
        const aiTab = await activateAndWaitForAITab(gWin, () =>
          activateItemAndWait(
            gMenu,
            gWin.document.getElementById("context_createAITab")
          )
        );
        await assertAITabCreationPrompt(aiTab, [TEST_URL], gMockEngineManager);

        const secondAITab = await activateAndWaitForAITab(gWin, async () => {
          const menu = await openTabContextMenu(gWin, gTab);
          await activateItemAndWait(
            menu,
            gWin.document.getElementById("context_createAITab")
          );
        });
        Assert.notEqual(secondAITab, aiTab, "Each activation opens a new tab");
        Assert.notEqual(
          getTabConversation(secondAITab).id,
          getTabConversation(aiTab).id,
          "Each activation starts a new conversation"
        );
        await assertAITabCreationPrompt(
          secondAITab,
          [TEST_URL],
          gMockEngineManager
        );
      });
    });

    describe("with a preloaded new tab page", () => {
      let gTab;
      let gPreloadedBrowser;

      beforeEach(async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.newtab.preload", true]],
        });
        gTab = await BrowserTestUtils.openNewForegroundTab(
          gWin.gBrowser,
          TEST_URL
        );
        NewTabPagePreloading.maybeCreatePreloadedBrowser(gWin);
        await TestUtils.waitForCondition(
          () =>
            gWin.gBrowser.preloadedBrowser?.contentDocument?.querySelector(
              "ai-window"
            )?.conversation,
          "Wait for the preloaded new tab page's chat to connect"
        );
        gPreloadedBrowser = gWin.gBrowser.preloadedBrowser;
      });

      afterEach(async () => {
        NewTabPagePreloading.removePreloadedBrowser(gWin);
        await SpecialPowers.popPrefEnv();
      });

      it("submits into the preloaded page", async () => {
        const aiTab = await activateAndWaitForAITab(
          gWin,
          async () => {
            const menu = await openTabContextMenu(gWin, gTab);
            await activateItemAndWait(
              menu,
              gWin.document.getElementById("context_createAITab")
            );
          },
          { expectCurrentChatUntouched: false }
        );
        Assert.equal(
          aiTab.linkedBrowser,
          gPreloadedBrowser,
          "The new tab took the preloaded new tab page"
        );
        await assertAITabCreationPrompt(aiTab, [TEST_URL], gMockEngineManager);
      });
    });

    describe("on a non-http tab", () => {
      it("hides the entry", async () => {
        // The Smart Window's initial tab is a chrome page.
        const menu = await openTabContextMenu(gWin, gWin.gBrowser.selectedTab);
        Assert.ok(
          gWin.document.getElementById("context_createAITab").hidden,
          "Create AI Tab is hidden for a non-http tab"
        );
        await hidePopupAndWait(menu);
      });
    });

    describe("with the AI Tab pref off", () => {
      beforeEach(async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.smartwindow.aitab.enabled", false]],
        });
      });

      afterEach(async () => {
        await SpecialPowers.popPrefEnv();
      });

      it("hides the entry", async () => {
        const tab = await BrowserTestUtils.openNewForegroundTab(
          gWin.gBrowser,
          TEST_URL
        );
        const menu = await openTabContextMenu(gWin, tab);
        Assert.ok(
          gWin.document.getElementById("context_createAITab").hidden,
          "Create AI Tab is hidden when the AI Tab pref is off"
        );
        await hidePopupAndWait(menu);
      });
    });
  });
});

describe("Create AI Tab menu triggers in a classic window", () => {
  let gTab;

  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.aitab.enabled", true],
        ["browser.smartwindow.aitab.menus", true],
      ],
    });
    gTab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  });

  afterEach(async () => {
    BrowserTestUtils.removeTab(gTab);
    await SpecialPowers.popPrefEnv();
  });

  it("hides the entry in the tab context menu", async () => {
    const menu = await openTabContextMenu(window, gTab);
    Assert.ok(
      window.document.getElementById("context_createAITab").hidden,
      "Create AI Tab is hidden in a classic window"
    );
    await hidePopupAndWait(menu);
  });
});
