/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_close_panel() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document } = win;
  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");
  await sidebar.updateComplete;
  await SidebarTestUtils.showPanel(win, "viewCustomizeSidebar");
  const sidebarPanelHeader = win.SidebarController.panelHeader;
  let closeButton = sidebarPanelHeader.closeButton;
  closeButton.click();
  ok(
    !document.querySelector("sidebar-customize"),
    "Sidebar customize panel has been closed."
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_actions_slot() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const button = win.document.createElement("button");
  try {
    const sidebar = win.document.querySelector("sidebar-main");
    await sidebar.updateComplete;
    await SidebarTestUtils.showPanel(win, "viewCustomizeSidebar");
    const panelHeader = win.SidebarController.panelHeader;
    button.slot = "actions";
    button.textContent = "More";
    panelHeader.append(button);
    await panelHeader.updateComplete;

    const heading = panelHeader.renderRoot.querySelector(
      ".sidebar-panel-heading"
    );
    const title = panelHeader.renderRoot.querySelector(
      "h4.sidebar-panel-title"
    );
    const buttonRect = button.getBoundingClientRect();
    const closeButtonRect = panelHeader.closeButton.getBoundingClientRect();
    const headingRect = heading.getBoundingClientRect();
    const titleRect = title.getBoundingClientRect();
    const gap = parseFloat(win.getComputedStyle(button).marginInlineEnd);

    Assert.greater(buttonRect.width, 0, "The actions button renders.");
    Assert.greater(gap, 0, "The actions button has an end margin.");
    Assert.lessOrEqual(
      Math.abs(closeButtonRect.left - buttonRect.right - gap),
      1,
      "The actions button is separated from the close button by its end margin."
    );
    Assert.lessOrEqual(
      Math.abs(titleRect.left - headingRect.left),
      1,
      "The title starts at the left edge of the heading."
    );
  } finally {
    button.remove();
    await BrowserTestUtils.closeWindow(win);
  }
});
