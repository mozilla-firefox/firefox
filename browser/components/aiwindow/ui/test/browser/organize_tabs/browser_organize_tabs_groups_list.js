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
    it("offers the row once groups exist and switches to the one clicked", async () => {
      const panel = await openPanelWithSuggestions(win);

      Assert.equal(
        win.gBrowser.getAllTabGroups().length,
        0,
        "No tab group exists yet"
      );
      Assert.ok(
        !panel.querySelector(".swgt-view-tab-groups"),
        "No 'View Tab Groups' row while the user has no groups"
      );

      panel.querySelector(".swgt-create-all").click();
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-view-tab-groups"),
        "The row appears once the created groups exist"
      );
      Assert.equal(
        row.querySelector(".swgt-row-label").getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-view-tab-groups",
        "The row's label is localized via Fluent"
      );

      row.click();
      const list = await TestUtils.waitForCondition(
        () => panel._flyoutPanel?.querySelector("tab-groups-list"),
        "Activating the row opens the tab groups list"
      );
      Assert.equal(
        panel._flyoutPanel
          .querySelector(".swgt-flyout-list")
          .getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-groups-list",
        "The list's accessible name is localized via Fluent"
      );
      Assert.equal(
        row.getAttribute("aria-expanded"),
        "true",
        "The row reports its list as expanded"
      );
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.contains(win.document.activeElement),
        "Activating the row moves focus into the list"
      );

      // Reopening must rebuild the list, so groups created in between show up.
      AutoTabGrouping._hideFlyout(panel);
      row.click();
      const groupRows = await TestUtils.waitForCondition(() => {
        const rebuilt = panel._flyoutPanel.querySelector("tab-groups-list");
        const rows = rebuilt?.querySelectorAll(".tab-group-row");
        return rebuilt !== list && rows?.length === 2 ? [...rows] : null;
      }, "Reopening builds a fresh list of both groups");

      const group = win.gBrowser.getTabGroupById(
        groupRows[1].dataset.tabGroupId
      );
      Assert.ok(group, "Each row names a tab group");
      groupRows[1].click();
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Selecting a group closes the panel"
      );
      Assert.equal(
        win.gBrowser.selectedTab.group,
        group,
        "The clicked group's tab is selected"
      );
    });

    it("exposes open and saved groups as menu items", async () => {
      await addWebTabs(win, ["open", "saved"]);
      const [openTab, savedTab] = win.gBrowser.tabs.slice(-2);
      win.gBrowser.addTabGroup([openTab], { label: "Open group" });
      await TabGroupTestUtils.saveAndCloseTabGroup(
        win.gBrowser.addTabGroup([savedTab], { label: "Saved group" })
      );

      const panel = await openPanel(win);
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-view-tab-groups"),
        "The row appears for the open and the saved group"
      );
      Assert.equal(
        row.getAttribute("aria-haspopup"),
        "menu",
        "The row says activating it opens a menu"
      );

      row.click();
      const [openRow, savedRow] = await TestUtils.waitForCondition(() => {
        const rows = panel._flyoutPanel?.querySelectorAll(".tab-group-row");
        return rows?.length === 2 ? [...rows] : null;
      }, "The list shows both groups");

      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      const list = panel._flyoutPanel.querySelector(".swgt-flyout-list");
      await TestUtils.waitForCondition(
        () =>
          accService.getAccessibleFor(list)?.role ===
          Ci.nsIAccessibleRole.ROLE_MENUPOPUP,
        "The list is exposed as a menu"
      );
      for (const [groupRow, name] of [
        [openRow, "Open group"],
        [savedRow, "Saved group"],
      ]) {
        // The row's label text can reach the accessibility tree after the row
        // itself, so wait for the name instead of reading it right away.
        await TestUtils.waitForCondition(
          () => accService.getAccessibleFor(groupRow)?.name === name,
          `${name} gets its accessible name`
        );
        const acc = accService.getAccessibleFor(groupRow);
        Assert.equal(
          acc.role,
          Ci.nsIAccessibleRole.ROLE_MENUITEM,
          `${name} is exposed as a menu item`
        );
        Assert.equal(acc.name, name, `${name} is named after the group`);
      }
    });

    it("takes focus when the list replaces an open suggestion flyout", async () => {
      const panel = await openPanelWithSuggestions(win);
      const row = await showViewTabGroupsRow(panel);

      const suggestion = panel.querySelector(".swgt-suggestion");
      suggestion.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel?.state === "open" &&
          panel._flyoutPanel.querySelector(".swgt-flyout-tab"),
        "The remaining suggestion's flyout is open"
      );

      row.click();
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.classList.contains("tab-group-row"),
        "Focus lands in the tab groups list, not on the render it replaced"
      );
    });

    it("opens the list on hover and hands it on with the pointer", async () => {
      const panel = await openPanelWithSuggestions(win);
      const row = await showViewTabGroupsRow(panel);

      row.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel?.state === "open" &&
          panel._flyoutPanel.querySelector("tab-groups-list"),
        "Pointing at the row is enough to open the tab groups list"
      );
      Assert.ok(
        !panel._flyoutPanel.contains(win.document.activeElement),
        "Hovering leaves focus where it was, as the suggestions do"
      );

      const suggestion = panel.querySelector(".swgt-suggestion");
      row.dispatchEvent(new win.MouseEvent("mouseleave"));
      suggestion.dispatchEvent(
        new win.MouseEvent("mouseover", { bubbles: true })
      );
      suggestion.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.querySelector(".swgt-flyout-tab"),
        "Moving on to a suggestion replaces the list with its tabs"
      );
      Assert.equal(
        row.getAttribute("aria-expanded"),
        "false",
        "The row no longer reports a list as expanded"
      );

      suggestion.dispatchEvent(new win.MouseEvent("mouseleave"));
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Leaving the rows behind closes the flyout"
      );
    });

    it("lets the pointer take over the list a click opened", async () => {
      const panel = await openPanelWithSuggestions(win);
      const row = await showViewTabGroupsRow(panel);

      row.click();
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.classList.contains("tab-group-row"),
        "Clicking the row moves focus into the list"
      );

      const suggestion = panel.querySelector(".swgt-suggestion");
      suggestion.dispatchEvent(
        new win.MouseEvent("mouseover", { bubbles: true })
      );
      suggestion.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.querySelector(".swgt-flyout-tab"),
        "The suggestion's tabs replace the list the click opened"
      );
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.classList.contains("swgt-flyout-tab"),
        "Focus moves onto the tabs, since the list it was on is gone"
      );
      Assert.equal(
        panel._hideTimer,
        0,
        "The flyout the pointer took over is not left waiting to hide"
      );
    });
  });
});
