/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let mockEngine;

add_setup(async function () {
  ({ win, mockEngine } = await openSidebarTestWindow());
});

describe("Sidebar for a tab with a conversation", () => {
  let chatTab;

  beforeEach(async () => {
    await openOnlyAIWindowTab(win);
    chatTab = win.gBrowser.selectedTab;
    await chatAndBrowseAway(win, mockEngine, "https://example.com/");
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("closes on the Smart Window page and reopens when browsing away again", async () => {
    const browser = chatTab.linkedBrowser;

    await promiseNavigateAndLoad(browser, AIWINDOW_URL);
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar closes when returning to the Smart Window page"
    );

    // Wait for the full page ai-window to connect so the tab's mode is
    // restored to "fullpage" before we navigate away again.
    await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window:defined"),
      "Fullpage ai-window should be ready"
    );
    await promiseNavigateAndLoad(browser, "https://example.org/");

    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "Sidebar opens again when browsing away"
    );
  });
});
