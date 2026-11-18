/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head_aitab.js */

ChromeUtils.defineESModuleGetters(this, {
  AIWindowTestUtils: "resource://testing-common/AIWindowTestUtils.sys.mjs",
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

AIWindowTestUtils.init(this, window);

/**
 * Stubs AuthProvider.maybeSignIn to skip sign-in flow
 * Call the returned restore function to clean up the stub
 *
 * @returns {Function} restore function to clean up the stub
 */
function skipSignIn() {
  const stub = sinon.stub(AuthProvider, "maybeSignIn").resolves(true);
  return () => stub.restore();
}

/**
 * Open and visit tabs oldest first, then return to the original tab.
 *
 * @param {Window} win - The browser window to open the tabs in
 * @param {string[]} urlsOldestFirst - One URL per tab, oldest first
 * @returns {Promise<MozTabbrowserTab[]>} The new tabs, oldest first
 */
async function openTabsOldestFirst(win, urlsOldestFirst) {
  const originalTab = win.gBrowser.selectedTab;
  const tabs = urlsOldestFirst.map(url =>
    BrowserTestUtils.addTab(win.gBrowser, url)
  );
  await Promise.all(
    tabs.map((tab, i) =>
      BrowserTestUtils.browserLoaded(
        tab.linkedBrowser,
        false,
        urlsOldestFirst[i]
      )
    )
  );
  for (const tab of [...tabs, originalTab]) {
    await selectAITabTestTab(tab);
  }
  return tabs;
}

/**
 * Multiselect tabs with the newest one active, preserving their access order.
 *
 * @param {Window} win - The browser window the tabs are in
 * @param {MozTabbrowserTab[]} tabs - The tabs to select
 */
async function selectTabs(win, tabs) {
  await selectAITabTestTab(tabs.at(-1));
  for (const tab of tabs.slice(0, -1)) {
    tab.scrollIntoView({ behavior: "instant" });
    EventUtils.synthesizeMouseAtCenter(tab, { accelKey: true }, win);
  }
  Assert.deepEqual(
    win.gBrowser.selectedTabs,
    tabs,
    "The tabs are multiselected"
  );
}

/**
 * Open an AI Window for AITab tests, with AITab enabled, sign-in skipped and
 * the model engine mocked. Call the returned cleanup function to undo all of
 * it, including closing the window.
 *
 * @returns {Promise<{win: Window, mockEngineManager: MockEngineManager, cleanup: Function}>}
 */
async function openAITabTestWindow() {
  const { MockEngineManager } = ChromeUtils.importESModule(
    "resource://testing-common/AIWindowTestUtils.sys.mjs"
  );
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.aitab.enabled", true],
      ["browser.smartwindow.aitab.menus", true],
      ["ui.prefersReducedMotion", 1],
    ],
  });
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();

  let win;
  try {
    win = await AIWindowTestUtils.openReadyAIWindow();
  } catch (error) {
    mockEngineManager.cleanupMocks();
    restoreSignIn();
    await SpecialPowers.popPrefEnv();
    throw error;
  }

  return {
    win,
    mockEngineManager,
    async cleanup() {
      await mockEngineManager.rejectAllRequests();
      await BrowserTestUtils.closeWindow(win);
      mockEngineManager.cleanupMocks();
      restoreSignIn();
      await SpecialPowers.popPrefEnv();
    },
  };
}
