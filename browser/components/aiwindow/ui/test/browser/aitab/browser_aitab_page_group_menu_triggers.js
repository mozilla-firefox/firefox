/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * The "Create AI Tab" entries in page and tab group context menus:
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
const TEST_URL2 = "https://example.org/";
// A page with real body content, needed to synthesize a contextmenu event.
const TEST_PAGE_URL =
  "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_context_url_page.html";

async function openPageContextMenu(win) {
  const menu = win.document.getElementById("contentAreaContextMenu");
  const shown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "p",
    { type: "contextmenu", button: 2 },
    win.gBrowser.selectedBrowser
  );
  await shown;
  return menu;
}

async function openTabGroupMenu(win, group) {
  const { panel } = win.gBrowser.tabGroupMenu;
  const shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  win.gBrowser.tabGroupMenu.openEditModal(group);
  await shown;
  return panel;
}

async function clickAndWaitForHidden(panel, button, win) {
  const hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  EventUtils.synthesizeMouseAtCenter(button, {}, win);
  await hidden;
}

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

  it("hides all three entries when the menu pref is off", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.aitab.menus", false]],
    });
    try {
      const tab = await BrowserTestUtils.openNewForegroundTab(
        gWin.gBrowser,
        TEST_PAGE_URL
      );
      const tabMenu = await openTabContextMenu(gWin, tab);
      Assert.ok(
        gWin.document.getElementById("context_createAITab").hidden,
        "The tab menu entry is hidden"
      );
      await hidePopupAndWait(tabMenu);

      const pageMenu = await openPageContextMenu(gWin);
      Assert.ok(
        gWin.document.getElementById("context-create-aitab").hidden,
        "The page menu entry is hidden"
      );
      await hidePopupAndWait(pageMenu);

      const group = gWin.gBrowser.addTabGroup([tab]);
      const groupMenu = await openTabGroupMenu(gWin, group);
      Assert.ok(
        gWin.document.getElementById("tabGroupEditor_createAITab").hidden,
        "The tab group menu entry is hidden"
      );
      await hidePopupAndWait(groupMenu);
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  });

  describe("page context menu", () => {
    describe("on an http page", () => {
      let gMenu;

      beforeEach(async () => {
        await BrowserTestUtils.openNewForegroundTab(
          gWin.gBrowser,
          TEST_PAGE_URL
        );
        gMenu = await openPageContextMenu(gWin);
      });

      it("shows the entry", async () => {
        Assert.ok(
          !gWin.document.getElementById("context-create-aitab").hidden,
          "Create AI Tab is shown on an http page"
        );
        await hidePopupAndWait(gMenu);
      });

      it("opens a background tab whose chat asks for the page URL", async () => {
        const aiTab = await activateAndWaitForAITab(gWin, () =>
          activateItemAndWait(
            gMenu,
            gWin.document.getElementById("context-create-aitab")
          )
        );
        await assertAITabCreationPrompt(
          aiTab,
          [TEST_PAGE_URL],
          gMockEngineManager
        );
      });
    });
  });

  describe("tab group menu", () => {
    describe("for a group with http tabs", () => {
      let gPanel;

      beforeEach(async () => {
        const tab1 = await BrowserTestUtils.openNewForegroundTab(
          gWin.gBrowser,
          TEST_URL
        );
        const tab2 = await BrowserTestUtils.openNewForegroundTab(
          gWin.gBrowser,
          TEST_URL2
        );
        const group = gWin.gBrowser.addTabGroup([tab1, tab2]);
        gPanel = await openTabGroupMenu(gWin, group);
      });

      it("shows the entry", async () => {
        Assert.ok(
          !gWin.document.getElementById("tabGroupEditor_createAITab").hidden,
          "Create AI Tab is shown for the group"
        );
        await hidePopupAndWait(gPanel);
      });

      it("opens a background tab whose chat asks for every tab URL", async () => {
        const aiTab = await activateAndWaitForAITab(gWin, () =>
          clickAndWaitForHidden(
            gPanel,
            gWin.document.getElementById("tabGroupEditor_createAITab"),
            gWin
          )
        );
        await assertAITabCreationPrompt(
          aiTab,
          [TEST_URL2, TEST_URL],
          gMockEngineManager
        );
      });
    });
  });
});
