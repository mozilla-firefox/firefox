/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

/* global promiseNavigateAndLoad, AIWINDOW_URL, openAIWindow */

const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);

const { require: devtoolsRequire } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/Loader.sys.mjs"
);
const { gDevTools } = devtoolsRequire(
  "resource://devtools/client/framework/devtools.js"
);

// Opening the sidebar and devtools toolbox sets prefs at runtime.
registerCleanupFunction(() => {
  for (const pref of [
    "browser.smartwindow.lastSmartWindowUsageTime",
    "browser.smartwindow.lastLLMTelemetryRunTime",
    "browser.smartwindow.sidebar.emptyCloseCount",
    "places.semanticHistory.initialized",
    "devtools.everOpened",
    "devtools.toolsidebar-width.inspector",
    "devtools.toolsidebar-height.inspector",
    "devtools.toolsidebar-width.inspector.splitsidebar",
  ]) {
    Services.prefs.clearUserPref(pref);
  }
});

// Switching to a new AIWindow tab from a tab with the sidebar open closes the sidebar
add_task(async function test_new_tab_closes_opened_sidebar_convo() {
  const { restore } = await stubEngineNetworkBoundaries();

  let win, newTab;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should be opened by AIWindowUI.openSidebar()"
    );

    newTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should not be opened after switching to a fresh AIWindow tab"
    );
  } finally {
    if (newTab) {
      BrowserTestUtils.removeTab(newTab);
    }

    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

// Navigating to a website moves an active fullwindow chat to the sidebar
add_task(
  async function test_navigate_to_url_with_active_chat_move_convo_to_sidebar() {
    const { restore } = await stubEngineNetworkBoundaries();

    let win;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      await promiseNavigateAndLoad(browser, "https://example.com/");

      Assert.ok(AIWindowUI.isSidebarOpen(win), "The sidebar should be open");
    } finally {
      await BrowserTestUtils.closeWindow(win);
      await restore();
    }
  }
);

add_task(async function test_navigate_back_to_aiwindow_closes_sidebar() {
  const { restore } = await stubEngineNetworkBoundaries();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should be open after navigating away"
    );

    // Navigate back to Smart Window URL
    await promiseNavigateAndLoad(browser, AIWINDOW_URL);
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should close when navigating back to Smart Window URL"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

// Navigating with an empty conversation (no messages) opens the sidebar
add_task(async function test_navigate_with_empty_conversation_opens_sidebar() {
  let win;

  const { restore } = await stubEngineNetworkBoundaries();

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await promiseNavigateAndLoad(browser, "https://example.com/");

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "The sidebar should be open even with an empty conversation"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

// Cleared conversations keep sidebar open but without conversation content
add_task(
  async function test_cleared_conversation_keeps_sidebar_open_on_tab_switch() {
    let win, newTab, originalTab;

    const { restore } = await stubEngineNetworkBoundaries();

    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      originalTab = win.gBrowser.selectedTab;

      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      await promiseNavigateAndLoad(browser, "https://example.com/");
      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should be open after navigating away"
      );

      await clickNewChatButton(win);

      // Open a new AI Window tab - sidebar should close
      newTab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        AIWINDOW_URL
      );
      Assert.ok(
        !AIWindowUI.isSidebarOpen(win),
        "Sidebar should be closed when switching to new tab"
      );

      // Switch back to the original tab - sidebar should stay open but without conversation
      await BrowserTestUtils.switchTab(win.gBrowser, originalTab);
      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should remain open when switching back to tab with cleared conversation"
      );
    } finally {
      if (newTab) {
        BrowserTestUtils.removeTab(newTab);
      }

      await BrowserTestUtils.closeWindow(win);
      await restore();
    }
  }
);

// Closing a tab with an active sidebar cleans up properly
add_task(async function test_close_tab_with_active_sidebar() {
  const { restore } = await stubEngineNetworkBoundaries();

  let win, newTab;
  try {
    win = await openAIWindow();
    const originalTab = win.gBrowser.selectedTab;
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");

    // Open a new tab to switch to before closing original
    newTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should close when switching to new tab"
    );

    // Close the original tab with conversation - should not throw
    BrowserTestUtils.removeTab(originalTab);

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should be closed after tab with conversation is removed"
    );
  } finally {
    if (newTab) {
      BrowserTestUtils.removeTab(newTab);
    }

    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

// Switching to tab with no state keeps sidebar open by default
add_task(async function test_tab_with_no_state_should_keep_sidebar() {
  let win, newTab;

  const { restore } = await stubEngineNetworkBoundaries();

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should be opened by AIWindowUI.openSidebar()"
    );

    newTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain open when switching to tab with no state (shouldOpenSidebar defaults to true)"
    );
  } finally {
    if (newTab) {
      BrowserTestUtils.removeTab(newTab);
    }

    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

// Closing sidebar via Ask button prevents reopening on same-tab navigation
add_task(async function test_ask_button_close_persists_across_navigation() {
  let win;

  const { restore } = await stubEngineNetworkBoundaries();

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar should open after navigating away with active conversation"
    );

    AIWindowUI.toggleSidebar(win);

    await promiseNavigateAndLoad(browser, "https://example.org/");

    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar should remain closed after navigating when user explicitly closed it"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});

// Only animate sidebar open/close on button click
add_task(async function test_slide_reserved_for_ask_close_button() {
  await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 0]] });

  const { restore } = await stubEngineNetworkBoundaries();

  let win;
  try {
    win = await openAIWindow();
    const box = win.document.getElementById(AIWindowUI.BOX_ID);
    const browser = win.gBrowser.selectedBrowser;

    await promiseNavigateAndLoad(browser, "https://example.com/");
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should start open");

    // Close button: slides
    AIWindowUI.closeSidebar(win, "toggle");
    Assert.greater(
      box.getAnimations().length,
      0,
      "Closing via the Close button should animate"
    );
    await waitForSidebarClosed(win);

    // Ask button (via toggleSidebar): slides
    AIWindowUI.toggleSidebar(win);
    Assert.greater(
      box.getAnimations().length,
      0,
      "Opening via the Ask button should animate"
    );
    await waitForSidebarOpen(win);

    // Tab-switch close (no source): instant
    AIWindowUI.closeSidebar(win);
    Assert.equal(
      box.getAnimations().length,
      0,
      "A sourceless close should commit instantly without animating"
    );
    Assert.ok(!AIWindowUI.isSidebarOpen(win), "Sidebar should be closed");

    // Tab-switch open: instant
    const openPromise = AIWindowUI.openSidebar(win);
    Assert.equal(
      box.getAnimations().length,
      0,
      "openSidebar should commit instantly without animating"
    );
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
    await openPromise;
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_tabs_after_first_should_open_sidebar_on_site_navigation() {
    let gAiWindow, newTab;

    const { restore } = await stubEngineNetworkBoundaries();

    try {
      gAiWindow = await openAIWindow();

      await promiseNavigateAndLoad(gAiWindow.gBrowser, "https://example.com/");
      Assert.ok(
        AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should open after navigating away with active conversation"
      );

      newTab = await BrowserTestUtils.openNewForegroundTab(
        gAiWindow.gBrowser,
        "https://example.net/"
      );
      Assert.ok(
        AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should open after navigating away with active conversation"
      );
    } finally {
      if (newTab) {
        BrowserTestUtils.removeTab(newTab);
      }

      await BrowserTestUtils.closeWindow(gAiWindow);
      await restore();
    }
  }
);

// Attempting to overdrag the sidebar does not extend into the content area.
add_task(async function test_sidebar_splitter_clamps_at_max_width() {
  let gAiWindow;

  try {
    gAiWindow = await openAIWindow();
    await promiseNavigateAndLoad(
      gAiWindow.gBrowser.selectedBrowser,
      "https://example.com/"
    );
    AIWindowUI.openSidebar(gAiWindow, null);
    await waitForSidebarOpen(gAiWindow);

    const { box, splitter } = AIWindowUI._getSidebarElements(gAiWindow);
    const sidebarMaxWidth = parseInt(gAiWindow.getComputedStyle(box).maxWidth);

    const maxDragDistance = -1 * gAiWindow.innerWidth;
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    EventUtils.synthesizeMouseAtCenter(
      splitter,
      { type: "mousedown" },
      gAiWindow
    );
    // Attempt to overdrag by 1px.
    EventUtils.synthesizeMouse(
      splitter,
      maxDragDistance - 1,
      0,
      { type: "mousemove" },
      gAiWindow
    );
    EventUtils.synthesizeMouse(splitter, 0, 0, { type: "mouseup" }, gAiWindow);
    AccessibilityUtils.resetEnv();

    const sidebarWidth = box.getBoundingClientRect().width;
    Assert.equal(
      sidebarWidth,
      sidebarMaxWidth,
      "The sidebar can’t be dragged past its max width."
    );
  } finally {
    AIWindowUI.closeSidebar(gAiWindow);
    await BrowserTestUtils.closeWindow(gAiWindow);
  }
});

// Overdragging the sidebar does not extend over a side-docked devtools toolbox.
add_task(async function test_sidebar_splitter_does_not_drag_over_devtools() {
  let gAiWindow, toolbox;
  try {
    gAiWindow = await openAIWindow();
    await promiseNavigateAndLoad(
      gAiWindow.gBrowser.selectedBrowser,
      "https://example.com/"
    );
    AIWindowUI.openSidebar(gAiWindow, null);
    await waitForSidebarOpen(gAiWindow);

    toolbox = await gDevTools.showToolboxForTab(
      gAiWindow.gBrowser.selectedTab,
      {
        hostType: "right",
      }
    );
    const toolboxWidth = toolbox.win.innerWidth;
    const browserStack = gAiWindow.gBrowser
      .getPanel()
      .querySelector(".browserStack");
    const contentAreaMin = parseInt(
      gAiWindow.getComputedStyle(browserStack).minWidth
    );

    const { splitter } = AIWindowUI._getSidebarElements(gAiWindow);
    const maxDragDistance = -1 * gAiWindow.innerWidth;
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    EventUtils.synthesizeMouseAtCenter(
      splitter,
      { type: "mousedown" },
      gAiWindow
    );
    // Attempt to overdrag by 1px.
    EventUtils.synthesizeMouse(
      splitter,
      maxDragDistance - 1,
      0,
      { type: "mousemove" },
      gAiWindow
    );
    EventUtils.synthesizeMouse(splitter, 0, 0, { type: "mouseup" }, gAiWindow);
    AccessibilityUtils.resetEnv();

    // Sidebar splitter stops at the devtools toolbox.
    Assert.equal(
      browserStack.getBoundingClientRect().width,
      contentAreaMin,
      "The page content keeps its minimum width."
    );
    Assert.equal(
      Math.abs(toolbox.win.innerWidth - toolboxWidth),
      0,
      "The devtools toolbox is not covered by the sidebar."
    );
  } finally {
    await toolbox?.destroy();
    AIWindowUI.closeSidebar(gAiWindow);
    await BrowserTestUtils.closeWindow(gAiWindow);
  }
});
