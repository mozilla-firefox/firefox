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
const TEST_PAGES =
  "https://example.com/browser/browser/components/aiwindow/ui/test/browser/";
const EXAMPLE_DOMAIN_PAGE = `${TEST_PAGES}test_context_url_page.html`;
const WEBSITE_TITLE_PAGE = `${TEST_PAGES}test_context_url_page2.html`;

/**
 * Mocks the model for the file's tests.
 *
 * @returns {MockEngineManager}
 */
function mockEngineForFile() {
  const mockEngine = new MockEngineManager();
  registerCleanupFunction(async () => {
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
  });
  return mockEngine;
}

/**
 * Makes a page the window's only tab, which opens the sidebar for it.
 *
 * @param {ChromeWindow} win
 * @param {string} url
 * @returns {Promise<MozBrowser>} The sidebar browser.
 */
async function openOnlyPageTab(win, url) {
  await openOnlyAIWindowTab(win);
  await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, url);
  return waitForSidebarReady(win);
}
