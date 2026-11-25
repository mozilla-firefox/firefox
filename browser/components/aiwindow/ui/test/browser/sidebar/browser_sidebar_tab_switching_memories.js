/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let mockEngine;

add_setup(async function () {
  ({ win, mockEngine } = await openSidebarTestWindow());
});

describe("Sidebar memories toggle across tab switches", () => {
  let chatTab;

  beforeEach(async () => {
    await openOnlyAIWindowTab(win);
    chatTab = win.gBrowser.selectedTab;
    await chatAndBrowseAway(win, mockEngine, "https://example.com/");
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("stays on for the conversation it was turned on for", async () => {
    const sidebarAIWindow = await waitForSidebarAIWindow(win);
    const memoriesButton = await TestUtils.waitForCondition(
      () => sidebarAIWindow.shadowRoot?.querySelector("memories-icon-button"),
      "Wait for memories button to be available"
    );
    memoriesButton.click();
    await TestUtils.waitForCondition(
      () => memoriesButton.pressed,
      "Memories button is pressed after clicking"
    );

    await openAIWindowPageTab(win);
    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Sidebar closes on a new Smart Window tab"
    );
    await BrowserTestUtils.switchTab(win.gBrowser, chatTab);

    await TestUtils.waitForCondition(
      () =>
        sidebarAIWindow.shadowRoot?.querySelector("memories-icon-button")
          ?.pressed,
      "Memories button is still pressed after switching back"
    );
  });
});
