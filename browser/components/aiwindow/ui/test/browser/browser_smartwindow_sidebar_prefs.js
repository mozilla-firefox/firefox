/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

/* global promiseNavigateAndLoad, AIWINDOW_URL, openAIWindow, typeInSmartbar, submitSmartbar */

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.urlbar.suggest.searches", false],
      ["browser.smartwindow.sidebar.openByDefault", true],
    ],
  });
});

// Sidebar stays closed when sidebar.openByDefault pref is false and no explicit toggle
add_task(
  async function test_sidebar_stays_closed_when_open_by_default_pref_false() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.sidebar.openByDefault", false]],
    });
    const mockEngineManager = new MockEngineManager();
    let win, tab;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      tab = win.gBrowser.selectedTab;

      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

      await promiseNavigateAndLoad(browser, "https://example.com/");
      await new Promise(resolve => win.setTimeout(resolve, 100));

      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should not open when sidebar.openByDefault pref is false"
      );
    } finally {
      if (tab) {
        await BrowserTestUtils.removeTab(tab);
      }
      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      await SpecialPowers.popPrefEnv();
    }
  }
);

// Changing sidebar.openByDefault pref applies to tabs that have not been explicitly toggled
add_task(
  async function test_pref_change_applies_to_tabs_without_explicit_toggle() {
    const mockEngineManager = new MockEngineManager();
    let win, tabA, tabB;
    try {
      win = await openAIWindow();
      const browserA = win.gBrowser.selectedBrowser;
      tabA = win.gBrowser.selectedTab;

      await typeInSmartbar(browserA, "hello");
      await submitSmartbar(browserA);
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

      // Navigate away with openByDefault = true - sidebar opens
      await promiseNavigateAndLoad(browserA, "https://example.com/");
      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(win),
        "Sidebar should open for tab A when sidebar.openByDefault pref is true"
      );

      // Open tab B — sidebar should remain open with sidebar.openByDefault = true
      tabB = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.org/"
      );
      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open for tab B"
      );

      // Change pref to false
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.sidebar.openByDefault", false]],
      });

      // Switch to tab A - sidebar should close because no explicit toggle was set
      await BrowserTestUtils.switchTab(win.gBrowser, tabA);
      await TestUtils.waitForCondition(
        () => !AIWindowUI.isSidebarOpen(win),
        "Sidebar should close for tab A after sidebar.openByDefault pref changes to false"
      );
      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should close for tab A after sidebar.openByDefault pref changes to false"
      );

      await SpecialPowers.popPrefEnv();
    } finally {
      if (tabA) {
        await BrowserTestUtils.removeTab(tabA);
      }
      if (tabB) {
        await BrowserTestUtils.removeTab(tabB);
      }
      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
    }
  }
);

// Explicitly toggled sidebar state persists regardless of sidebar.openByDefault pref changes
add_task(async function test_explicit_toggle_persists_across_pref_change() {
  const mockEngineManager = new MockEngineManager();
  let win, tab;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    tab = win.gBrowser.selectedTab;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

    await promiseNavigateAndLoad(browser, "https://example.com/");
    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Sidebar should open when sidebar.openByDefault is true"
    );

    // User explicitly toggles sidebar closed then open — keepSidebarOpen: true
    AIWindowUI.toggleSidebar(win);
    Assert.ok(!AIWindowUI.isSidebarOpen(win), "Sidebar closed after toggle");
    AIWindowUI.toggleSidebar(win);
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar opened after toggle");

    // Change sidebar.openByDefault pref to false
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.sidebar.openByDefault", false]],
    });

    // Switch away to an AIWINDOW_URL tab (sidebar closes there) then back
    const newTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );
    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Sidebar should close on AIWINDOW_URL tab"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, tab);
    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Sidebar should reopen because user explicitly toggled it open"
    );
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Explicitly opened sidebar persists even when sidebar.openByDefault pref changes to false"
    );

    await BrowserTestUtils.removeTab(newTab);
    await SpecialPowers.popPrefEnv();
  } finally {
    if (tab) {
      await BrowserTestUtils.removeTab(tab);
    }
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});
