/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let mockEngine;

add_setup(async function () {
  ({ win, mockEngine } = await openSidebarTestWindow());
});

describe("Sidebar closed by the user", () => {
  let chatTab;

  beforeEach(async () => {
    await openOnlyAIWindowTab(win);
    chatTab = win.gBrowser.selectedTab;
    await chatAndBrowseAway(win, mockEngine, "https://example.com/");
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("stays closed after the Ask button closed it", async () => {
    AIWindowUI.toggleSidebar(win);
    Assert.ok(
      !AIWindowUI.isSidebarOpen(win),
      "Sidebar closes from the Ask button toggle"
    );

    await assertStaysClosedAcrossTabSwitch(chatTab);
  });

  // Bug 2037378: closing the sidebar via its X close button must persist
  // across tab switches the same way the toolbar Ask button toggle does.
  it("stays closed after its X button closed it", async () => {
    const sidebarAIWindow = await waitForSidebarAIWindow(win);
    const closeButton = await TestUtils.waitForCondition(
      () => sidebarAIWindow.shadowRoot?.querySelector(".close-sidebar-button"),
      "Wait for sidebar close button"
    );
    closeButton.click();
    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Sidebar closes from its X button"
    );

    await assertStaysClosedAcrossTabSwitch(chatTab);
  });
});

/**
 * Switches to another Smart Window tab and back, and checks the sidebar the
 * user closed stays closed.
 *
 * @param {MozTabbrowserTab} chatTab - The tab where the user closed it.
 */
async function assertStaysClosedAcrossTabSwitch(chatTab) {
  const otherTab = BrowserTestUtils.addTab(win.gBrowser, AIWINDOW_URL);
  await BrowserTestUtils.browserLoaded(otherTab.linkedBrowser);
  await BrowserTestUtils.switchTab(win.gBrowser, otherTab);

  await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
  await TestUtils.waitForTick();

  Assert.ok(
    !AIWindowUI.isSidebarOpen(win),
    "Sidebar stays closed switching back to the tab where the user closed it"
  );
}
