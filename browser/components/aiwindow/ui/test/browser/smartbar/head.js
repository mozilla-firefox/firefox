/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

/**
 * Turns on urlbar engagement telemetry and answers the intent model by
 * keyword: URL-shaped input navigates, "search", "find" and "look up" search,
 * and anything else chats.
 *
 * @returns {Promise<string>} The model the smartbar reports in its telemetry.
 */
async function setupSmartbarTelemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.searchEngagementTelemetry.enabled", true],
      ["browser.smartwindow.firstrun.modelChoice", "1"],
    ],
  });
  // Typing in the smartbar records this as it would for a user.
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(
      "browser.urlbar.quickactions.timesShownOnboardingLabel"
    );
  });

  gIntentEngineStub.resolves({
    run({ args: [[query]] }) {
      const prompt = query.toLowerCase();
      if (
        ["https://", "www.", ".com"].some(keyword => prompt.includes(keyword))
      ) {
        return [
          { label: "navigate", score: 0.95 },
          { label: "chat", score: 0.05 },
        ];
      }
      if (
        ["search", "find", "look up"].some(keyword => prompt.includes(keyword))
      ) {
        return [
          { label: "search", score: 0.95 },
          { label: "chat", score: 0.05 },
        ];
      }
      return [
        { label: "chat", score: 0.95 },
        { label: "search", score: 0.05 },
      ];
    },
  });

  return modelFor("1");
}

async function resetTelemetry() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
}

/**
 * Opens a page in a background tab, so the smartbar has a tab to offer.
 *
 * @param {ChromeWindow} win
 * @param {string} url
 */
async function addBackgroundTab(win, url) {
  const tab = BrowserTestUtils.addTab(win.gBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, { wantLoad: url });
}

/**
 * Waits for the smartbar's panel list to offer an item.
 *
 * @param {MozBrowser} browser
 * @param {object} [options]
 * @param {boolean} [options.click] whether to then click the first item
 */
async function waitForPanelItem(browser, { click = false } = {}) {
  await SpecialPowers.spawn(browser, [click], async shouldClick => {
    const panel = content.document
      .querySelector("ai-window")
      .shadowRoot.querySelector("#ai-window-smartbar smartwindow-panel-list")
      .shadowRoot.querySelector("panel-list");
    const selector = "panel-item:not(.panel-section-header)";
    await ContentTaskUtils.waitForMutationCondition(
      panel,
      { childList: true, subtree: true },
      () => panel.querySelector(selector)
    );
    if (shouldClick) {
      panel.querySelector(selector).click();
    }
  });
}

/**
 * Waits for the smartbar's panel list to offer an item and clicks the first.
 *
 * @param {MozBrowser} browser
 */
async function clickFirstPanelItem(browser) {
  await waitForPanelItem(browser, { click: true });
}

/**
 * Clicks the smartbar's "add tabs" context button.
 *
 * @param {MozBrowser} browser
 */
async function clickAddTabsButton(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    content.document
      .querySelector("ai-window")
      .shadowRoot.querySelector("#ai-window-smartbar context-icon-button")
      .shadowRoot.querySelector("moz-button")
      .click();
  });
}

/**
 * Opens the smartbar action menu from its chevron button.
 *
 * @param {MozBrowser} browser
 * @param {string} [pickIcon] - Also pick the menu item with this icon.
 */
async function openActionMenu(browser, pickIcon) {
  await SpecialPowers.spawn(browser, [pickIcon], async icon => {
    const inputCta = content.document
      .querySelector("ai-window")
      .shadowRoot.querySelector("#ai-window-smartbar input-cta");
    const mozButton = inputCta.shadowRoot.querySelector("moz-button");
    const chevronButton = await ContentTaskUtils.waitForCondition(
      () => mozButton.shadowRoot.querySelector("#chevron-button"),
      "Wait for chevron button to be rendered"
    );
    const panelList = inputCta.shadowRoot.querySelector("panel-list");
    chevronButton.click();
    await ContentTaskUtils.waitForEvent(panelList, "shown");
    if (icon) {
      panelList.querySelector(`panel-item[icon="${icon}"]`).click();
    }
  });
}

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
