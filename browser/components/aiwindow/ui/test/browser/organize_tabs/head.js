/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

const { AutoTabGrouping } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGrouping.sys.mjs"
);
const { AutoTabGroupingSuggestions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs"
);
const { TabGroupTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TabGroupTestUtils.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

function fakeTwoGroupManager() {
  return {
    async generateClusters(tabList) {
      return {
        clusterRepresentations: [
          { tabs: tabList.slice(0, 2), cohesion: 0.9 },
          { tabs: tabList.slice(2, 4), cohesion: 0.9 },
        ],
      };
    },
    async getPredictedLabelForGroup() {
      return "Test Group";
    },
    async preloadAllModels() {},
  };
}

async function addWebTabs(win, paths = ["a", "b", "c", "d"]) {
  const tabs = paths.map(path =>
    BrowserTestUtils.addTab(win.gBrowser, `https://example.com/${path}`, {
      skipAnimation: true,
    })
  );
  await Promise.all(
    tabs.map((tab, i) =>
      BrowserTestUtils.browserLoaded(
        tab.linkedBrowser,
        false,
        `https://example.com/${paths[i]}`
      )
    )
  );
  // Clustering skips a tab that is still busy or untitled, and both can
  // outlast the load event.
  await TestUtils.waitForCondition(() => {
    const candidates = AutoTabGroupingSuggestions.getCandidateTabs(win);
    return tabs.every(tab => candidates.includes(tab));
  }, "Every added tab is clusterable");
}

async function navigateToContent(win, url = "https://example.com/") {
  const browser = win.gBrowser.selectedTab.linkedBrowser;
  const loaded = BrowserTestUtils.browserLoaded(browser, false, url);
  BrowserTestUtils.startLoadingURIString(browser, url);
  await loaded;
}

async function openGroupingWindowWithTabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
  });
  const win = await openAIWindow();
  await navigateToContent(win);
  await addWebTabs(win);
  return win;
}

async function openPanel(win) {
  AutoTabGrouping.toggleGroupTabsPanel(win);
  const panel = await TestUtils.waitForCondition(() =>
    win.document.getElementById("smartwindow-group-tabs-panel")
  );
  await TestUtils.waitForCondition(
    () => panel.state === "open",
    "Panel finished opening"
  );
  return panel;
}

async function openPanelWithSuggestions(win) {
  const panel = await openPanel(win);
  await TestUtils.waitForCondition(
    () => panel.querySelectorAll(".swgt-suggestion").length === 2,
    "Two suggested group rows render once clustering finishes"
  );
  return panel;
}

/**
 * Creates the first suggested group, which is what makes the "View Tab Groups"
 * row appear, leaving a suggestion still listed above it.
 *
 * @param {XULPopupElement} panel - The open Organize Tabs panel.
 * @returns {Promise<Element>} The "View Tab Groups" row.
 */
async function showViewTabGroupsRow(panel) {
  panel.querySelector(".swgt-suggestion").click();
  return TestUtils.waitForCondition(
    () => panel.querySelector(".swgt-view-tab-groups"),
    "The row appears once the created group exists"
  );
}

async function closePanel(win) {
  AutoTabGrouping.toggleGroupTabsPanel(win);
  await TestUtils.waitForCondition(
    () => !win.document.getElementById("smartwindow-group-tabs-panel")
  );
}

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.firstrun.hasCompleted", true],
      ["browser.ml.enable", true],
      ["browser.ml.modelHubRootUrl", "http://localhost:0/"],
      ["browser.tabs.groups.smart.enabled", true],
      ["browser.tabs.groups.smart.userEnabled", true],
      ["browser.tabs.groups.smart.optin", true],
      // Navigating a Smart Window off about:aiwindow opens the sidebar, whose
      // composer then takes focus out of the panel a test has just opened.
      ["browser.smartwindow.sidebar.openByDefault", false],
    ],
  });

  const originalManager = AutoTabGroupingSuggestions._manager;
  const originalLlmLabel = AutoTabGroupingSuggestions._llmLabelForGroup;
  AutoTabGroupingSuggestions._llmLabelForGroup = async () => {
    throw new Error("force on-device");
  };
  registerCleanupFunction(() => {
    AutoTabGroupingSuggestions._manager = originalManager;
    AutoTabGroupingSuggestions._llmLabelForGroup = originalLlmLabel;
    AutoTabGroupingSuggestions._preloadPromise = null;
    Services.prefs.clearUserPref("browser.tabs.groups.saveHintShownCount");
  });
});

function setupAutoTabGroupingTest() {
  const mockEngineManager = new MockEngineManager();
  AutoTabGroupingSuggestions._manager = fakeTwoGroupManager();
  AutoTabGroupingSuggestions._preloadPromise = null;
  Services.fog.testResetFOG();
  return mockEngineManager;
}

async function cleanupAutoTabGroupingTest(win, mockEngineManager) {
  await mockEngineManager.rejectAllRequests();
  if (win) {
    await BrowserTestUtils.closeWindow(win);
  }
  TabGroupTestUtils.forgetSavedTabGroups();
  // Popping the feature pref while the window is alive would tear the widget
  // down under a panel that may still be open.
  await SpecialPowers.popPrefEnv();
  mockEngineManager.cleanupMocks();
}

/**
 * Opens the Smart Window that a file's tests share, with the feature on for
 * the rest of the file. Closed after the file's tests.
 *
 * @returns {Promise<ChromeWindow>}
 */
async function openSharedGroupingWindow() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
  });
  return openSharedAIWindow();
}

const WEB_TAB_URLS = ["a", "b", "c", "d"].map(
  path => `https://example.com/${path}`
);

/**
 * Brings a window back to what openGroupingWindowWithTabs gives a new one: a
 * content page followed by four clusterable tabs, with no tab groups. Tabs a
 * test left in that order are ungrouped and kept, since loading them again is
 * most of the cost of a test.
 *
 * @param {ChromeWindow} win
 */
async function resetGroupingWindow(win) {
  await closePanelIfOpen(win);
  for (const group of win.gBrowser.tabGroups) {
    group.ungroupTabs();
  }
  TabGroupTestUtils.forgetSavedTabGroups();
  const expected = ["https://example.com/", ...WEB_TAB_URLS];
  const urls = win.gBrowser.tabs.map(tab => tab.linkedBrowser.currentURI.spec);
  if (
    urls.length == expected.length &&
    urls.every((u, i) => u == expected[i])
  ) {
    win.gBrowser.selectedTab = win.gBrowser.tabs[0];
    return;
  }
  const url = expected[0];
  const tab = BrowserTestUtils.addTab(win.gBrowser, url, {
    skipAnimation: true,
  });
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, url);
  win.gBrowser.selectedTab = tab;
  for (const other of win.gBrowser.tabs.filter(t => t != tab)) {
    BrowserTestUtils.removeTab(other, { skipSessionStore: true });
  }
  await addWebTabs(win);
}

/**
 * Opens a Smart Window of its own on a content page and four clusterable tabs,
 * for a test that closes it.
 *
 * @returns {Promise<ChromeWindow>}
 */
async function openWindowWithWebTabs() {
  const win = await openAIWindow();
  await navigateToContent(win);
  await addWebTabs(win);
  return win;
}

/**
 * @param {ChromeWindow} win
 */
async function closePanelIfOpen(win) {
  if (win.document.getElementById("smartwindow-group-tabs-panel")) {
    await closePanel(win);
  }
}
