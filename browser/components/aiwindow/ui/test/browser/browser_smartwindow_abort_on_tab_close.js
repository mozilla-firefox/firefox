/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global openAIWindowWithSidebar, skipSignIn, submitSmartbar, typeInSmartbar */

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

// Closing the website tab that owns the captured browsingContext aborts the
// in-flight sidebar generation.
add_task(async function test_tab_close_aborts_sidebar_generation() {
  const mockEngineManager = new MockEngineManager();
  const restoreSignIn = skipSignIn();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const tab = win.gBrowser.selectedTab;

  // Keep the window alive after removing the captured tab; otherwise window
  // teardown would abort the request and mask whether the per-tab listener
  // actually fires.
  await BrowserTestUtils.openNewForegroundTab(win.gBrowser, "about:blank");
  win.gBrowser.selectedTab = tab;

  try {
    await typeInSmartbar(sidebarBrowser, "from sidebar");
    await submitSmartbar(sidebarBrowser);
    await TestUtils.waitForCondition(
      () => Chat.fetchWithHistory.called,
      "Wait for the sidebar chat turn"
    );
    const { signal } = Chat.fetchWithHistory.firstCall.args[0];

    Assert.ok(
      !signal.aborted,
      "Signal should not be aborted while the tab is alive"
    );

    await BrowserTestUtils.removeTab(tab);
    await TestUtils.waitForCondition(
      () => signal.aborted,
      "Signal should be aborted after the captured tab is closed"
    );
    Assert.ok(
      !win.closed,
      "Window should remain open; abort came from the single tab close"
    );
  } finally {
    if (!win.closed) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    restoreSignIn();
  }
});
