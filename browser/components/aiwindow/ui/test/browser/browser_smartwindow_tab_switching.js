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

// Switching back to a tab with an active conversation reopens the sidebar
add_task(
  async function test_switch_back_to_tab_with_conversation_reopens_sidebar() {
    let win, newTab, originalTab;

    const mockEngineManager = new MockEngineManager();

    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      originalTab = win.gBrowser.selectedTab;
      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

      // Navigate away from AIWINDOW_URL (simulates user browsing after starting a chat)
      await promiseNavigateAndLoad(browser, "https://example.com/");

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open after navigating away"
      );

      // Open a new AI Window tab - sidebar should close
      newTab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        AIWINDOW_URL
      );
      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should close when switching to new AI Window tab"
      );

      // Switch back to the original tab - sidebar should reopen
      await BrowserTestUtils.switchTab(win.gBrowser, originalTab);

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should reopen when switching back to tab with conversation"
      );
    } finally {
      if (newTab) {
        BrowserTestUtils.removeTab(newTab);
      }

      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
    }
  }
);

// Switching between tabs with different conversations maintains correct state
add_task(
  async function test_switch_between_tabs_with_different_conversations() {
    const mockEngineManager = new MockEngineManager();

    let tabA, tabB, win;
    try {
      win = await openAIWindow();
      const browserA = win.gBrowser.selectedBrowser;
      tabA = win.gBrowser.selectedTab;

      await typeInSmartbar(browserA, "hello");
      await submitSmartbar(browserA);
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });
      await promiseNavigateAndLoad(browserA, "https://example.com/");

      // Open tab B with a different conversation
      tabB = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        AIWINDOW_URL
      );

      const browserB = win.gBrowser.selectedBrowser;
      await typeInSmartbar(browserB, "hello");
      await submitSmartbar(browserB);
      await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });
      await promiseNavigateAndLoad(browserB, "https://example.org/");

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open for tab B"
      );

      // Switch to tab A - sidebar should update to conversation A
      await BrowserTestUtils.switchTab(win.gBrowser, tabA);

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should remain open when switching to tab A with conversation"
      );

      // Switch back to tab B
      await BrowserTestUtils.switchTab(win.gBrowser, tabB);

      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should remain open when switching back to tab B"
      );
    } finally {
      if (tabB) {
        BrowserTestUtils.removeTab(tabB);
      }

      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
    }
  }
);

// Switching between tabs with empty and non-empty conversations
add_task(async function test_switch_between_empty_and_nonempty_conversations() {
  const mockEngineManager = new MockEngineManager();

  let tabA, tabB, win;
  try {
    win = await openAIWindow();
    const browserA = win.gBrowser.selectedBrowser;
    tabA = win.gBrowser.selectedTab;

    await typeInSmartbar(browserA, "hello");
    await submitSmartbar(browserA);
    await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });
    await promiseNavigateAndLoad(browserA, "https://example.com/");

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should be open for tab A with messages"
    );

    // Open tab B with an empty conversation
    tabB = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );

    const browserB = win.gBrowser.selectedBrowser;
    await promiseNavigateAndLoad(browserB, "https://example.org/");

    // Open sidebar for tab B (empty conversation)
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should be open for tab B with empty conversation"
    );

    // Switch to tab A - sidebar should remain open with conversation A
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain open when switching to tab A"
    );

    // Switch back to tab B - sidebar should remain open
    await BrowserTestUtils.switchTab(win.gBrowser, tabB);

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain open when switching to tab B with empty conversation"
    );
  } finally {
    if (tabB) {
      BrowserTestUtils.removeTab(tabB);
    }

    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_sidebar_state_after_multiple_navigations() {
  const mockEngineManager = new MockEngineManager();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should be closed on Smart Window URL"
    );

    // Navigate away - sidebar should open because conversation has messages
    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");

    // Navigate back to Smart Window URL - sidebar should close
    await promiseNavigateAndLoad(browser, AIWINDOW_URL);

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should close when returning to Smart Window URL"
    );

    // Wait for the fullpage ai-window to connect so the tab's mode is
    // restored to "fullpage" before we navigate away again.
    await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window:defined"),
      "Fullpage ai-window should be ready"
    );

    // Navigate away again - sidebar should open again
    await promiseNavigateAndLoad(browser, "https://example.org/");

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should open again when navigating away"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

// Closing sidebar via Ask button keeps it closed when switching tabs
add_task(async function test_ask_button_close_persists_across_tab_switches() {
  let win, tab2;

  const mockEngineManager = new MockEngineManager();

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const originalTab = win.gBrowser.selectedTab;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.querySelector("ai-window");
      await ContentTaskUtils.waitForCondition(
        () => el.conversationMessageCount > 0,
        "Wait for chat response before navigating"
      );
    });

    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should open after navigating away with active conversation"
    );

    AIWindowUI.toggleSidebar(win);

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should be closed after Ask button toggle"
    );

    tab2 = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
    await BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
    await BrowserTestUtils.switchTab(win.gBrowser, tab2);
    await TestUtils.waitForTick();

    await BrowserTestUtils.switchTab(win.gBrowser, originalTab);
    await TestUtils.waitForTick();

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain closed after switching back to tab where user closed it"
    );
  } finally {
    if (tab2) {
      BrowserTestUtils.removeTab(tab2);
    }

    // Navigate away from the remote page before closing so the content
    // process shuts down cleanly and doesn't leave an unhandled rejection.
    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, "about:blank");

    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

// Bug 2037378: closing the sidebar via its X close button must persist across
// tab switches the same way the toolbar Ask button toggle does.
add_task(async function test_x_close_persists_across_tab_switches() {
  let win, tab2;

  const mockEngineManager = new MockEngineManager();

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const originalTab = win.gBrowser.selectedTab;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

    await SpecialPowers.spawn(browser, [], async () => {
      const el = content.document.querySelector("ai-window");
      await ContentTaskUtils.waitForCondition(
        () => el.conversationMessageCount > 0,
        "Wait for chat response before navigating"
      );
    });

    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should open after navigating away with active conversation"
    );

    const aiBrowser = win.document.getElementById("ai-window-browser");
    const aiWindow = await TestUtils.waitForCondition(
      () => aiBrowser.contentDocument?.querySelector("ai-window:defined"),
      "Wait for sidebar ai-window to be defined"
    );
    const closeButton = await TestUtils.waitForCondition(
      () => aiWindow.shadowRoot?.querySelector(".close-sidebar-button"),
      "Wait for sidebar close button"
    );
    closeButton.click();

    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Sidebar should be closed after X close button"
    );

    tab2 = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
    await BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
    await BrowserTestUtils.switchTab(win.gBrowser, tab2);
    await TestUtils.waitForTick();

    await BrowserTestUtils.switchTab(win.gBrowser, originalTab);
    await TestUtils.waitForTick();

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain closed after switching back to tab where user closed it via X"
    );
  } finally {
    if (tab2) {
      BrowserTestUtils.removeTab(tab2);
    }

    // Navigate away from the remote page before closing so the content
    // process shuts down cleanly and doesn't leave an unhandled rejection.
    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, "about:blank");

    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});
