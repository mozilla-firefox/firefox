/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head.js */

"use strict";

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
  const tabs = [];
  for (const path of paths) {
    const url = `https://example.com/${path}`;
    const tab = BrowserTestUtils.addTab(win.gBrowser, url, {
      skipAnimation: true,
    });
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, url);
    tabs.push(tab);
  }
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
  AIWindowUI.toggleGroupTabsPanel(win);
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

async function closePanel(win) {
  AIWindowUI.toggleGroupTabsPanel(win);
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
