/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * Opens the Smart Window a file's tests share, with model requests mocked.
 * Both are cleaned up after the file's tests.
 *
 * @returns {Promise<{win: ChromeWindow, mockEngine: MockEngineManager}>}
 */
async function openSidebarTestWindow() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.sidebar.openByDefault", true]],
  });
  const mockEngine = new MockEngineManager();
  registerCleanupFunction(() => mockEngine.cleanupMocks());
  const win = await openSharedAIWindow();
  return { win, mockEngine };
}

/**
 * Chats in the selected Smart Window full page, then browses away so the
 * conversation moves to the sidebar.
 *
 * @param {ChromeWindow} win
 * @param {MockEngineManager} mockEngine
 * @param {string} url - The page to browse to, with a trailing slash.
 */
async function chatAndBrowseAway(win, mockEngine, url) {
  const browser = win.gBrowser.selectedBrowser;
  await typeInSmartbar(browser, "hello");
  await submitSmartbar(browser);
  await mockEngine.respondTo({ purpose: "chat", response: "Mock" });
  await TestUtils.waitForCondition(
    () =>
      browser.contentDocument?.querySelector("ai-window")
        ?.conversationMessageCount > 0,
    "Wait for the chat to be answered"
  );
  await promiseNavigateAndLoad(browser, url);
}

/**
 * @param {ChromeWindow} win
 * @returns {Promise<MozTabbrowserTab>} A new Smart Window full page tab.
 */
function openAIWindowPageTab(win) {
  return BrowserTestUtils.openNewForegroundTab(win.gBrowser, AIWINDOW_URL);
}

/**
 * @param {ChromeWindow} win
 * @returns {Promise<Element>} The sidebar's ai-window element.
 */
function waitForSidebarAIWindow(win) {
  const sidebarBrowser = win.document.getElementById(AIWindowUI.BROWSER_ID);
  return TestUtils.waitForCondition(
    () => sidebarBrowser?.contentDocument?.querySelector("ai-window:defined"),
    "Wait for the sidebar ai-window to be defined"
  );
}
