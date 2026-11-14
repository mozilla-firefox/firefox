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
const TOOLBAR_PREF = "browser.smartwindow.actionsToolbar.enabled";
const CONSENT_PREF = "browser.smartwindow.tos.consentTime";
const TOOLBAR_ID = "smart-actions-toolbar";
const PAGE_URL = "https://example.com/";
const OTHER_URL = "https://example.org/";
const STARTER = "What is this page about";
const STARTER_PURPOSE = "convo-starters-sidebar";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [TOOLBAR_PREF, true],
      [CONSENT_PREF, 1],
      // The sidebar's own starter requests would share the mocked purpose.
      ["browser.smartwindow.sidebar.openByDefault", false],
    ],
  });
});

/**
 * Opens a Smart Window with model requests mocked, closed after the file's
 * tests.
 *
 * @returns {Promise<{win: ChromeWindow, tab: MozTabbrowserTab, mockEngine: MockEngineManager}>}
 */
async function openToolbarWindow() {
  const mockEngine = new MockEngineManager();
  const win = await BrowserTestUtils.openNewBrowserWindow();
  AIWindow.toggleAIWindow(win, true);
  registerCleanupFunction(async () => {
    await BrowserTestUtils.closeWindow(win);
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
  });
  return { win, tab: win.gBrowser.selectedTab, mockEngine };
}

let pageCount = 0;

/**
 * @returns {string} A page the toolbar hasn't loaded actions for, so it
 *   requests them.
 */
function newPageUrl() {
  return `${PAGE_URL}?${++pageCount}`;
}

function isToolbarShown(win) {
  const toolbar = win.document.getElementById(TOOLBAR_ID);
  return !!toolbar && !toolbar.hidden;
}

/**
 * Answers the toolbar's starter request and waits for the starter button.
 *
 * @param {ChromeWindow} win
 * @param {MockEngineManager} mockEngine
 * @returns {Promise<{request: object, button: MozButton}>}
 */
async function respondWithStarter(win, mockEngine) {
  const { request, respond } = await mockEngine.captureRequest({
    purpose: STARTER_PURPOSE,
  });
  respond(STARTER);
  const button = await TestUtils.waitForCondition(() => {
    const found = win.document.querySelector(`#${TOOLBAR_ID} moz-button`);
    return (
      isToolbarShown(win) &&
      found?.label === STARTER &&
      !found.disabled &&
      found
    );
  }, "Waiting for the starter to render");
  return { request, button };
}

function toolbarLabels(win) {
  return [...win.document.querySelectorAll(`#${TOOLBAR_ID} moz-button`)].map(
    button => button.label
  );
}

function loadInTab(tab, uriString) {
  return BrowserTestUtils.loadURIString({
    browser: tab.linkedBrowser,
    uriString,
  });
}
