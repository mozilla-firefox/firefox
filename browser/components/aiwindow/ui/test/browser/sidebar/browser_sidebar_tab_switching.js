/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let mockEngine;

add_setup(async function () {
  ({ win, mockEngine } = await openSidebarTestWindow());
});

describe("Sidebar across tab switches", () => {
  let chatTab;

  beforeEach(async () => {
    await openOnlyAIWindowTab(win);
    chatTab = win.gBrowser.selectedTab;
    await chatAndBrowseAway(win, mockEngine, "https://example.com/");
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("reopens when switching back to the tab with a conversation", async () => {
    await openAIWindowPageTab(win);
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar closes on a new Smart Window tab"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar reopens when switching back to the tab with a conversation"
    );
  });

  it("stays open between tabs with different conversations", async () => {
    const otherChatTab = await openAIWindowPageTab(win);
    await chatAndBrowseAway(win, mockEngine, "https://example.org/");

    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar stays open switching to the first conversation's tab"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, otherChatTab);
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar stays open switching back to the second conversation's tab"
    );
  });

  it("stays open between a conversation and an empty one", async () => {
    const emptyTab = await openAIWindowPageTab(win);
    await promiseNavigateAndLoad(
      emptyTab.linkedBrowser,
      "https://example.org/"
    );
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar opens by default for a page with an empty conversation"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar stays open switching to the tab with a conversation"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, emptyTab);
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar stays open switching to the tab with an empty conversation"
    );
  });
});
