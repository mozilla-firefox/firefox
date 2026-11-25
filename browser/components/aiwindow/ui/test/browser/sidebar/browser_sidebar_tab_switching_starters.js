/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let mockEngine;

add_setup(async function () {
  ({ win, mockEngine } = await openSidebarTestWindow());
});

describe("Sidebar conversation starters across tab switches", () => {
  let chatTab;

  beforeEach(async () => {
    await openOnlyAIWindowTab(win);
    chatTab = win.gBrowser.selectedTab;
    await chatAndBrowseAway(win, mockEngine, "https://example.com/");
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("show for an empty conversation and hide for one with messages", async () => {
    const sidebarAIWindow = await waitForSidebarAIWindow(win);
    await TestUtils.waitForCondition(
      () => sidebarAIWindow.conversationMessageCount > 0,
      "Sidebar loads the conversation with messages"
    );
    const emptyTab = await openAIWindowPageTab(win);
    await promiseNavigateAndLoad(
      emptyTab.linkedBrowser,
      "https://example.com/"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
    await TestUtils.waitForCondition(
      () => !sidebarAIWindow.showStarters,
      "Starters hide for the conversation with messages"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, emptyTab);
    await TestUtils.waitForCondition(
      () => sidebarAIWindow.showStarters,
      "Starters show for the empty conversation"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
    await TestUtils.waitForCondition(
      () => !sidebarAIWindow.showStarters,
      "Starters hide again when switching back to the conversation with messages"
    );
  });

  it("are not shown again for a conversation with messages", async () => {
    await openAIWindowPageTab(win);
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar closes on a new Smart Window tab"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);

    const sidebarAIWindow = await waitForSidebarAIWindow(win);
    await TestUtils.waitForCondition(
      () => sidebarAIWindow.conversationMessageCount > 0,
      "Sidebar is back on the conversation with messages"
    );
    Assert.ok(
      !sidebarAIWindow.showStarters,
      "Starter prompts are not shown for an existing conversation"
    );
  });
});
