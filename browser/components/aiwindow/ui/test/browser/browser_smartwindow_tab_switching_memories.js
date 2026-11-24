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

// Memories toggle state persists when switching tabs
add_task(async function test_memories_toggle_state_persists_on_tab_switch() {
  const mockEngineManager = new MockEngineManager();

  let originalTab, newTab, win, aiWindowEl, sidebarBrowser, browser;
  try {
    win = await openAIWindow();
    browser = win.gBrowser.selectedBrowser;
    originalTab = win.gBrowser.selectedTab;

    // Type and submit a message to start a conversation
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({ purpose: "chat", response: "Mock" });

    // Navigate to example.com to trigger sidebar mode
    await promiseNavigateAndLoad(browser, "https://example.com/");

    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Sidebar should be open after navigating away"
    );

    sidebarBrowser = win.document.getElementById(AIWindowUI.BROWSER_ID);
    await TestUtils.waitForCondition(
      () => !!sidebarBrowser.contentDocument.querySelector("ai-window"),
      "aiWindow element should be available"
    );

    aiWindowEl = sidebarBrowser.contentDocument.querySelector("ai-window");

    // Click the memories button to toggle it on
    await SpecialPowers.spawn(sidebarBrowser, [], async () => {
      const aiWindow = content.document.querySelector("ai-window");
      const memoriesButton = await ContentTaskUtils.waitForCondition(
        () => aiWindow.shadowRoot?.querySelector("memories-icon-button"),
        "Wait for memories button to be available"
      );

      // Click to toggle memories on
      memoriesButton.click();

      await ContentTaskUtils.waitForCondition(
        () => memoriesButton.pressed === true,
        "Wait for memories button to be pressed"
      );
    });

    // Verify memories button is pressed
    Assert.ok(
      aiWindowEl.shadowRoot.querySelector("memories-icon-button").pressed,
      "Memories button should be pressed after clicking"
    );

    // Now open a new AI Window tab to test that switching tabs preserves the memories toggle
    // state of the original tab's conversation
    newTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );
    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Sidebar should close when switching to new AI Window tab"
    );

    // Switch back to the original tab - sidebar should reopen with same memories state
    await BrowserTestUtils.switchTab(win.gBrowser, originalTab);

    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Sidebar should reopen when switching back to tab with conversation"
    );

    // Wait for aiWindow to be available again after tab switch
    await TestUtils.waitForCondition(
      () => !!sidebarBrowser.contentDocument.querySelector("ai-window"),
      "aiWindow element should be available after tab switch"
    );

    // Verify memories button state is preserved
    await TestUtils.waitForCondition(() => {
      const memoriesButton = aiWindowEl.shadowRoot?.querySelector(
        "memories-icon-button"
      );
      return memoriesButton?.pressed === true;
    }, "Memories button should still be pressed after tab switch");

    Assert.ok(
      aiWindowEl.shadowRoot.querySelector("memories-icon-button").pressed,
      "Memories state should be preserved after tab switch"
    );
  } finally {
    browser = null;
    sidebarBrowser = null;
    aiWindowEl = null;

    if (newTab) {
      await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, "about:blank");
      BrowserTestUtils.removeTab(newTab);
    }

    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, "about:blank");
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});
