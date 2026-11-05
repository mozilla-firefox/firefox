/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

/* global promiseNavigateAndLoad, AIWINDOW_URL, openAIWindow */

const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.urlbar.suggest.searches", false],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.sidebar.openByDefault", true],
      ["ui.prefersReducedMotion", 1],
    ],
  });
});

// Switching to classic mode tears down tab state management
add_task(async function test_classic_mode_disables_tab_state_events() {
  const { restore } = await stubEngineNetworkBoundaries();

  let win, newTab;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const originalTab = win.gBrowser.selectedTab;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);

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
      "Sidebar should open in smart mode"
    );

    AIWindow.toggleAIWindow(win, false);

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should be closed after switching to Classic Window"
    );

    // Wait for AIWINDOW_URL to finish loading before switching the tab
    // again to prevent an unhandled promise error that happens from
    // AsyncTabSwitch.sys.mjs:380
    //  let isLocalAbout = requestedBrowser.currentURI.schemeIs("about")
    //
    newTab = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
    await BrowserTestUtils.browserLoaded(newTab.linkedBrowser);
    await BrowserTestUtils.switchTab(win.gBrowser, newTab);
    await TestUtils.waitForTick();

    await BrowserTestUtils.switchTab(win.gBrowser, originalTab);
    await TestUtils.waitForTick();

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain closed in Classic Window after tab switch"
    );
  } finally {
    if (newTab) {
      BrowserTestUtils.removeTab(newTab);
    }

    // Navigate away from the remote page before closing so the content
    // process shuts down cleanly and doesn't leave an unhandled rejection.
    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, "about:blank");

    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

add_task(async function test_classic_mode_smartbar_toggle_button() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    Assert.ok(
      aiWindowElement && !aiWindowElement.hidden,
      "ai-window element should be visible pre-toggle"
    );

    // Simulate active chat
    aiWindowElement.classList.add("chat-active");
  });

  AIWindow.toggleAIWindow(win, false);

  await BrowserTestUtils.waitForMutationCondition(
    win.document.documentElement,
    { attributes: true, attributeFilter: ["ai-window"] },
    () => !AIWindowTestUtils.isAIWindow(win)
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    Assert.ok(
      aiWindowElement && !aiWindowElement.hidden,
      "ai-window element should be visible in Classic Window after toggle from Smart Window"
    );

    const button = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot.querySelector("#smartbar-toggle-button"),
      "Toggle button should be in DOM in Classic Window"
    );

    Assert.ok(
      !button.hidden,
      "Toggle button should be visible in Classic Window"
    );

    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    Assert.ok(smartbar?.hidden, "Smartbar should be hidden in Classic Window");

    button.click();
  });

  await BrowserTestUtils.waitForMutationCondition(
    win.document.documentElement,
    { attributes: true, attributeFilter: ["ai-window"] },
    () => AIWindowTestUtils.isAIWindow(win)
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    Assert.ok(
      aiWindowElement && !aiWindowElement.hidden,
      "ai-window element should still be visible after toggling back to Smart Window"
    );

    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    await ContentTaskUtils.waitForCondition(
      () => smartbar && !smartbar.hidden,
      "Smartbar should be visible after toggling back to Smart Window"
    );

    const toggleButton = aiWindowElement.shadowRoot.querySelector(
      "#smartbar-toggle-button"
    );
    Assert.ok(
      toggleButton?.hidden,
      "Toggle button should be hidden in Smart Window"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});
