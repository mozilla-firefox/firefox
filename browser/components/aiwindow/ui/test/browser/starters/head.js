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
 * Turns memories off for the file, so resume cards only load where a test turns
 * memories on.
 */
async function pushStarterPrefs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", false],
      ["browser.smartwindow.memories.generateFromHistory", false],
      ["places.semanticHistory.smartwindow.featureGate", false],
    ],
  });
}

const MEMORIES_ON_PREFS = [
  ["browser.smartwindow.memories.generateFromConversation", true],
  ["browser.smartwindow.memories.generateFromHistory", true],
];

/**
 * Opens a fresh Smart Window full page in the shared window and waits for its
 * starter prompts to replace the loading skeletons.
 *
 * @param {ChromeWindow} win
 * @returns {Promise<{browser: MozBrowser, aiWindow: Element, buttons: Element[]}>}
 */
async function openStartersPage(win) {
  const aiWindow = await openOnlyAIWindowTab(win);
  const browser = win.gBrowser.selectedBrowser;
  const buttons = await TestUtils.waitForCondition(async () => {
    const found = await getPromptButtons(browser);
    return found.length ? [...found] : false;
  }, "Wait for prompt buttons to replace loading skeletons");
  return { browser, aiWindow, buttons };
}

function getSidebarPromptButtons(win) {
  const sidebarBrowser = win.document.getElementById("ai-window-browser");
  const aiWindowEl =
    sidebarBrowser?.contentDocument?.querySelector("ai-window");
  const promptsEl = aiWindowEl?.shadowRoot?.querySelector(
    "smartwindow-prompts"
  );
  if (!promptsEl) {
    return [];
  }
  const buttons = promptsEl.shadowRoot.querySelectorAll(".sw-prompt-button");
  return Array.from(buttons).map(b => b.textContent.trim());
}

async function navigateTo(url, window) {
  const parentBrowser = window.gBrowser.selectedBrowser;
  const loaded = BrowserTestUtils.browserLoaded(parentBrowser);
  BrowserTestUtils.startLoadingURIString(parentBrowser, url);
  await loaded;
}

async function openBackgroundTab(url, window) {
  let tab = BrowserTestUtils.addTab(window.gBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  return tab;
}

/**
 * Answers every sidebar starter request with responseContent[0] as it reaches
 * the model, waiting for `released` first while that is set.
 *
 * @param {string[]} responseContent
 * @returns {{mockEngineManager: MockEngineManager, requestCount: number, released: ?Promise}}
 */
function answerStarterRequests(responseContent) {
  const starters = {
    mockEngineManager: new MockEngineManager(),
    requestCount: 0,
    released: null,
  };
  (async () => {
    for (;;) {
      const { respond } = await starters.mockEngineManager.captureRequest({
        purpose: "convo-starters-sidebar",
      });
      starters.requestCount++;
      await starters.released;
      respond(responseContent[0]);
    }
  })().catch(() => {});
  return starters;
}
