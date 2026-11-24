/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

/* global promiseNavigateAndLoad, AIWINDOW_URL, openAIWindow */

const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.urlbar.suggest.searches", false],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.sidebar.openByDefault", true],
    ],
  });
});

// Switching between non-empty and empty conversations shows/hides starters
add_task(
  async function test_tab_switch_shows_starters_for_empty_conversation() {
    const mockEngineManager = new MockEngineManager();
    const sb = sinon.createSandbox();

    let tabA, tabB, win;
    try {
      win = await openAIWindow();
      const browserA = win.gBrowser.selectedBrowser;
      tabA = win.gBrowser.selectedTab;

      await typeInSmartbar(browserA, "hello");
      await submitSmartbar(browserA);

      // Wait for first LLM request to resolve
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

      // Spy on findConversationById before navigating so we can
      // await promises from findConversationById before swtching tabs
      const findSpy = sb.spy(AIWindow.chatStore, "findConversationById");

      await promiseNavigateAndLoad(browserA, "https://example.com/");
      await TestUtils.waitForTick();
      Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should open for tab A");

      // Wait for the sidebar to load conversation A with messages.
      const sidebarBrowser = win.document.getElementById(AIWindowUI.BROWSER_ID);
      await TestUtils.waitForCondition(() => {
        const el = sidebarBrowser?.contentDocument?.querySelector("ai-window");
        return el && el.conversationMessageCount > 0;
      }, "Sidebar should load conversation A with messages");

      // Wait for promises to resolve from findConversationById before
      // starting a new tab
      await Promise.all(findSpy.returnValues);

      const aiWindowEl =
        sidebarBrowser.contentDocument.querySelector("ai-window");

      // Open tab B with an empty conversation
      tabB = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        AIWINDOW_URL
      );
      const browserB = win.gBrowser.selectedBrowser;
      await promiseNavigateAndLoad(browserB, "https://example.com/");
      await TestUtils.waitForTick();

      // Switch to tab A (with messages) - starters should not show
      await BrowserTestUtils.switchTab(win.gBrowser, tabA);
      Assert.ok(!aiWindowEl.showStarters, "Starters should not be showing");

      // Switch to tab B (empty) - starters should show
      await BrowserTestUtils.switchTab(win.gBrowser, tabB);
      Assert.ok(aiWindowEl.showStarters, "Starters should be showing");

      // Switch back to tab A - starters should hide again
      await BrowserTestUtils.switchTab(win.gBrowser, tabA);
      await TestUtils.waitForCondition(
        () => !aiWindowEl.showStarters,
        "Conversation A should reload with starters hidden"
      );
      Assert.ok(!aiWindowEl.showStarters, "Starters should not be showing");

      await BrowserTestUtils.switchTab(win.gBrowser, tabB);

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should remain open when switching to tab B with conversation"
      );
      Assert.ok(aiWindowEl.showStarters, "Starters should be showing");
    } finally {
      if (tabB) {
        BrowserTestUtils.removeTab(tabB);
      }

      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      sb.restore();
    }
  }
);

add_task(
  async function test_tab_switch_does_not_refresh_starters_for_existing_conversation() {
    const sb = sinon.createSandbox();
    const mockEngineManager = new MockEngineManager();

    let win, newTab, originalTab;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      originalTab = win.gBrowser.selectedTab;

      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);

      // wait for first request to LLM to prevent promise races during test
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

      await promiseNavigateAndLoad(browser, "https://example.com/");

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open for tab A with messages"
      );

      newTab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        AIWINDOW_URL
      );
      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should be closed for tab A with messages in fullwindow mode"
      );

      await BrowserTestUtils.switchTab(win.gBrowser, originalTab);
      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open for tab A with messages"
      );

      const sidebarBrowser = win.document.getElementById(AIWindowUI.BROWSER_ID);
      const sidebarAiWindow =
        sidebarBrowser.contentDocument.querySelector("ai-window");

      Assert.greater(
        sidebarAiWindow.conversationMessageCount,
        0,
        "Sidebar should be on tab A's conversation with messages"
      );
      Assert.ok(
        !sidebarAiWindow.showStarters,
        "Starter prompts should not be shown for an existing conversation"
      );
    } finally {
      if (newTab) {
        BrowserTestUtils.removeTab(newTab);
      }

      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
      sb.restore();
    }
  }
);
