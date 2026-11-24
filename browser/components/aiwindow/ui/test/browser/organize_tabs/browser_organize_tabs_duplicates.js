/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    // Skip the one-time "we'll close duplicate tabs" confirmation.
    set: [["browser.tabs.haveShownCloseAllDuplicateTabsWarning", true]],
  });
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

  describe("closing duplicate tabs", () => {
    // Open the panel on a window holding duplicates, and preview them.
    async function openDuplicatesFlyout() {
      // Three copies of /a and two of /b, so three tabs would close.
      await addWebTabs(win, ["a", "b"]);
      await addWebTabs(win, ["a"]);

      const panel = await openPanelWithSuggestions(win);
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-close-duplicates"),
        "The 'Close Duplicate Tabs' row appears once duplicates exist"
      );
      row.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel?.state === "open",
        "Pointing at the row opens its preview"
      );
      await panel._flyoutPanel._flyoutEl.updateComplete;
      return {
        panel,
        row,
        tabRows: [...panel._flyoutPanel.querySelectorAll(".swgt-flyout-tab")],
      };
    }

    it("hides the row when there is nothing to close", async () => {
      const panel = await openPanelWithSuggestions(win);

      Assert.ok(
        !panel.querySelector(".swgt-close-duplicates"),
        "No 'Close Duplicate Tabs' row while every tab is unique"
      );
    });

    it("closes the duplicates and leaves one of each tab", async () => {
      await addWebTabs(win, ["a", "b"]);
      const tabsBefore = win.gBrowser.tabs.length;

      const panel = await openPanelWithSuggestions(win);
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-close-duplicates"),
        "The 'Close Duplicate Tabs' row appears once duplicates exist"
      );

      const duplicates = win.gBrowser.getAllDuplicateTabsToClose().length;
      Assert.equal(duplicates, 2, "The two added tabs are the duplicates");
      Assert.equal(
        JSON.parse(row.getAttribute("data-l10n-args")).tabCount,
        duplicates,
        "The row is told how many tabs activating it closes"
      );
      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(
        () => accService.getAccessibleFor(row)?.name?.includes(`${duplicates}`),
        "The row's label reports that count"
      );

      const hintShown = BrowserTestUtils.waitForEvent(
        win.document,
        "popupshown",
        true,
        event => event.target.id === "confirmation-hint"
      );
      row.click();
      const { target: hint } = await hintShown;
      Assert.equal(
        hint.anchorNode?.id,
        "smartwindow-group-tabs-button",
        "The hint points at our button, not the All Tabs button which may be absent"
      );
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabs.length === tabsBefore - 2,
        "Both duplicate tabs are closed"
      );
      const urls = win.gBrowser.tabs.map(t => t.linkedBrowser.currentURI.spec);
      Assert.equal(
        new Set(urls).size,
        urls.length,
        "Every remaining tab is at a distinct URL"
      );

      const requested =
        Glean.smartWindow.closeDuplicateTabsRequested.testGetValue();
      Assert.equal(requested?.length, 1, "One 'requested' event recorded");
      Assert.equal(
        requested[0].extra.tabs,
        String(tabsBefore),
        "Recorded the tab count from before the duplicates were closed"
      );

      const started =
        Glean.smartWindow.closeDuplicateTabsStarted.testGetValue();
      Assert.equal(started?.length, 1, "One 'started' event recorded");
      Assert.greater(
        Number(started[0].extra.total_length),
        0,
        "Reported how much tab-title text was searched"
      );

      const completed =
        Glean.smartWindow.closeDuplicateTabsCompleted.testGetValue();
      Assert.equal(completed?.length, 1, "One 'completed' event recorded");
      Assert.equal(completed[0].extra.success, "true", "The search succeeded");
      Assert.equal(
        completed[0].extra.duplicate_tabs,
        "2",
        "Found both duplicates"
      );

      const closed = Glean.smartWindow.duplicateTabsClosed.testGetValue();
      Assert.equal(closed?.length, 1, "One 'closed' event recorded");
      Assert.equal(closed[0].extra.duplicate_tabs, "2", "Closed both");
      Assert.equal(closed[0].extra.success, "true", "Closing succeeded");
    });

    it("previews the tabs it would close and switches to the one clicked", async () => {
      const { panel, row, tabRows } = await openDuplicatesFlyout();

      Assert.equal(
        win.gBrowser.getAllDuplicateTabsToClose().length,
        3,
        "Three tabs would close: two extra copies of /a and one of /b"
      );
      Assert.equal(
        tabRows.length,
        3,
        "One row per tab, listing each copy that would close"
      );
      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(
        () =>
          accService.getAccessibleFor(
            panel._flyoutPanel.querySelector(".swgt-flyout-list")
          )?.role === Ci.nsIAccessibleRole.ROLE_MENUPOPUP,
        "The preview is exposed as a menu"
      );
      Assert.ok(
        tabRows.every(
          tabRow =>
            accService.getAccessibleFor(tabRow).role ===
            Ci.nsIAccessibleRole.ROLE_MENUITEM
        ),
        "Each tab in it is a menu item"
      );
      Assert.equal(
        row.getAttribute("aria-expanded"),
        "true",
        "The row reports the list it opened"
      );
      Assert.ok(
        row.classList.contains("is-active"),
        "The row stays highlighted while its preview is up"
      );

      const tabsBefore = win.gBrowser.tabs.length;
      const previewed = panel._duplicateTabs[1];
      tabRows[1].click();
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Selecting a tab closes the panel"
      );
      Assert.equal(
        win.gBrowser.selectedTab,
        previewed,
        "The clicked duplicate is selected"
      );
      Assert.equal(
        win.gBrowser.tabs.length,
        tabsBefore,
        "Looking at a duplicate does not close anything"
      );
    });

    it("keeps the preview through the keyboard and the pointer", async () => {
      const { panel, row, tabRows } = await openDuplicatesFlyout();

      row.focus();
      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "The forward arrow moves focus into the preview"
      );
      EventUtils.synthesizeKey("KEY_ArrowLeft", {}, win);
      await TestUtils.waitForCondition(
        () =>
          win.document.activeElement === row &&
          panel._flyoutPanel.state === "closed",
        "The back arrow closes it again and returns focus to the row"
      );

      // Sliding over from a suggestion swaps what the flyout lists rather than
      // closing it on the way.
      const suggestion = panel.querySelector(".swgt-suggestion");
      suggestion.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel._flyoutEl.suggestion &&
          panel._flyoutPanel.state !== "closed",
        "The suggestion took the flyout over"
      );
      row.dispatchEvent(new win.MouseEvent("mouseover", { bubbles: true }));
      row.dispatchEvent(new win.MouseEvent("mouseenter"));
      Assert.notEqual(
        panel._flyoutPanel.state,
        "closed",
        "The flyout never closed on the way back"
      );
      Assert.equal(
        panel._flyoutPanel._flyoutEl.duplicates.length,
        3,
        "It lists the duplicates again"
      );

      panel
        .querySelector(".swgt-create-all")
        .dispatchEvent(new win.MouseEvent("mouseover", { bubbles: true }));
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Moving onto a row with no preview of its own closes it"
      );
    });
  });
});
