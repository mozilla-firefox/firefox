/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "sidebar.newTool.migration.resourcemonitor",
        JSON.stringify({ alreadyShown: true }),
      ],
      ["browser.resourceMonitor.enabled", true],
    ],
  });
  await SidebarTestUtils.waitForInitialized(window);
  SidebarController.hide();
  registerCleanupFunction(() => SidebarController.hide());
});

// panel_opened is recorded during the panel's second visible poll.
async function waitForPanelOpened(win, count) {
  let { browser } = win.SidebarController;
  if (
    !browser.currentURI.spec.startsWith("about:processes") ||
    browser.contentDocument.readyState != "complete"
  ) {
    await BrowserTestUtils.waitForEvent(browser, "load", true, () =>
      browser.currentURI.spec.startsWith("about:processes")
    );
  }
  while (
    (Glean.resourceMonitor.panelOpened.testGetValue()?.length ?? 0) < count
  ) {
    await BrowserTestUtils.waitForEvent(
      browser.contentDocument,
      "AboutProcessesUpdated"
    );
  }
  return Glean.resourceMonitor.panelOpened.testGetValue();
}

add_task(async function test_resource_monitor_sidebar_launch() {
  Services.fog.testResetFOG();
  try {
    await SidebarController.show("viewResourceMonitorSidebar");
    const events = await waitForPanelOpened(window, 1);
    Assert.deepEqual(
      events.map(event => event.extra.open_type),
      ["sidebar_launch"],
      "Opening Resource Monitor from the sidebar records one user launch"
    );
  } finally {
    SidebarController.hide();
  }
});

add_task(async function test_resource_monitor_inherited_by_new_window() {
  Services.fog.testResetFOG();
  let newWin;
  try {
    await SidebarController.show("viewResourceMonitorSidebar");
    await waitForPanelOpened(window, 1);
    newWin = await BrowserTestUtils.openNewBrowserWindow({
      openerWindow: window,
    });
    await SidebarTestUtils.waitForInitialized(newWin);
    Assert.equal(
      newWin.SidebarController.currentID,
      "viewResourceMonitorSidebar",
      "The new window inherited the open Resource Monitor panel"
    );
    const events = await waitForPanelOpened(newWin, 2);
    Assert.deepEqual(
      events.map(event => event.extra.open_type),
      ["sidebar_launch", "restored"],
      "An inherited panel records a restoration instead of another user launch"
    );
  } finally {
    if (newWin) {
      await BrowserTestUtils.closeWindow(newWin);
    }
    SidebarController.hide();
  }
});

add_task(async function test_other_sidebar_url_unchanged() {
  Services.fog.testResetFOG();
  try {
    await SidebarController.show("viewHistorySidebar");
    Assert.equal(
      SidebarController.browser.contentDocument.documentURI,
      "chrome://browser/content/sidebar/sidebar-history.html",
      "The history panel loads its normal URL without an openType parameter"
    );
    Assert.equal(
      Glean.resourceMonitor.panelOpened.testGetValue(),
      null,
      "Opening history does not record a Resource Monitor panel opening"
    );
  } finally {
    SidebarController.hide();
  }
});
