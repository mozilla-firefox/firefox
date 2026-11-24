/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  win = await openSharedGroupingWindow();
});

describe("Auto Tab Grouping toolbar button", () => {
  let mockEngineManager;

  beforeEach(async () => {
    await resetGroupingWindow(win);
    mockEngineManager = setupAutoTabGroupingTest();
  });

  afterEach(async () => {
    await closePanelIfOpen(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  });

  describe("viewing existing tab groups", () => {
    it("closes the list when the window is left behind", async () => {
      const panel = await openPanelWithSuggestions(win);
      const row = await showViewTabGroupsRow(panel);

      row.click();
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel?.state === "open",
        "The list is open"
      );

      win.dispatchEvent(new win.Event("deactivate"));
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Switching away from the window closes the list"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "The panel itself is left open"
      );
    });

    it("stays open and up to date when a group is deleted from the list", async () => {
      const panel = await openPanelWithSuggestions(win);

      panel.querySelector(".swgt-create-all").click();
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-view-tab-groups"),
        "The row appears once the created groups exist"
      );
      row.click();
      const groupRows = await TestUtils.waitForCondition(() => {
        const rows = panel._flyoutPanel?.querySelectorAll(".tab-group-row");
        return rows?.length === 2 ? [...rows] : null;
      }, "The list is open on both groups");
      const group = win.gBrowser.getTabGroupById(
        groupRows[0].dataset.tabGroupId
      );

      const contextMenu = win.document.getElementById(
        "open-tab-group-context-menu"
      );
      // A trusted contextmenu must be a PointerEvent (MouseEvent.cpp asserts
      // on it in debug builds).
      groupRows[0].dispatchEvent(
        new win.PointerEvent("contextmenu", { bubbles: true, cancelable: true })
      );
      await TestUtils.waitForCondition(
        () => contextMenu.state === "open",
        "The group's context menu opens"
      );
      contextMenu.dispatchEvent(
        new win.MouseEvent("mousedown", {
          bubbles: true,
        })
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Clicking in the menu the row raised is not a click outside the panel"
      );
      contextMenu.dispatchEvent(
        new win.MouseEvent("mouseup", { bubbles: true })
      );

      contextMenu.activateItem(
        win.document.getElementById("open-tab-group-context-menu_delete")
      );
      await TestUtils.waitForCondition(
        () => !win.gBrowser.getTabGroupById(group.id),
        "The group is deleted"
      );

      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Deleting a group leaves the panel open"
      );
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel.state === "open" &&
          panel._flyoutPanel.querySelectorAll(".tab-group-row").length === 1,
        "The list stays open, without the group that is gone"
      );
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 1,
        "'Just created' drops it too"
      );
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.classList.contains("tab-group-row"),
        "Focus lands back on the list the deleted row was in"
      );
    });

    it("leaves focus alone when the deletion did not take it", async () => {
      const panel = await openPanelWithSuggestions(win);

      panel.querySelector(".swgt-create-all").click();
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-view-tab-groups"),
        "The row appears once the created groups exist"
      );
      row.click();
      const groupRows = await TestUtils.waitForCondition(() => {
        const rows = panel._flyoutPanel?.querySelectorAll(".tab-group-row");
        return rows?.length === 2 ? [...rows] : null;
      }, "The list is open on both groups");
      // Deleting a group closes its tabs, and closing the selected tab moves
      // focus on its own; take the group that leaves the selection alone.
      const group = groupRows
        .map(r => win.gBrowser.getTabGroupById(r.dataset.tabGroupId))
        .find(g => !g.tabs.includes(win.gBrowser.selectedTab));

      // The click hands focus into the list once the flyout finishes
      // opening; let it land before putting focus back on the row.
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.contains(win.document.activeElement),
        "The click's focus hand-off into the list has settled"
      );

      // The user is on a row of the main panel when the group goes away on
      // its own, with no context menu in between to drop their focus.
      row.focus();
      Assert.equal(win.document.activeElement, row, "The panel row has focus");

      win.gBrowser.removeTabGroup(group);
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel.state === "open" &&
          panel._flyoutPanel.querySelectorAll(".tab-group-row").length === 1,
        "The list refreshes to the remaining group"
      );
      Assert.equal(
        win.document.activeElement,
        row,
        "Focus stays on the row the user was on"
      );
    });

    it("stops watching for group changes when the window closes", async () => {
      const observers = () =>
        [
          ...Services.obs.enumerateObservers(
            "browser-tabgroup-removed-from-dom"
          ),
        ].length;

      const ownWin = await openWindowWithWebTabs();
      const beforePanel = observers();
      await openPanelWithSuggestions(ownWin);
      Assert.equal(
        observers(),
        beforePanel + 1,
        "The open panel watches for group changes"
      );

      await BrowserTestUtils.closeWindow(ownWin);
      Assert.equal(
        observers(),
        beforePanel,
        "Closing the window with the panel open stops the watching"
      );
    });
  });
});
