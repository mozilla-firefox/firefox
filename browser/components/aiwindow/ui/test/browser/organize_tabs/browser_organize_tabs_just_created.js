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

  describe("creating groups", () => {
    it("creates one group at a time and keeps the panel open", async () => {
      const panel = await openPanelWithSuggestions(win);

      const groupsBefore = win.gBrowser.tabGroups.length;
      panel.querySelector(".swgt-suggestion").click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 1,
        "Clicking one suggestion created one group"
      );

      const remaining = await TestUtils.waitForCondition(() => {
        const rows = panel.querySelectorAll(".swgt-suggestion");
        return rows.length === 1 ? rows[0] : null;
      }, "The other suggestion is still offered");
      Assert.ok(
        !panel.querySelector(".swgt-create-all"),
        "With one suggestion left, its own row is the only way to create it"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Panel stays open so the next group can be created"
      );
      Assert.equal(
        panel.querySelectorAll(".swgt-recent-row").length,
        1,
        "The created group is listed under 'Just created'"
      );
      await TestUtils.waitForCondition(
        () => win.document.activeElement === remaining,
        "Focus moves to the suggestion that took its place"
      );

      remaining.click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "The second suggestion created the second group without reopening"
      );
      const accepted = Glean.smartWindow.autoTabGroupAccepted.testGetValue();
      Assert.equal(accepted?.length, 2, "Two 'accepted' events recorded");
      Assert.ok(
        accepted.every(e => e.extra.source === "individual_accept"),
        "Both are recorded as individual accepts"
      );
    });

    it("lists created groups under 'Just created' and ungroups them all", async () => {
      const panel = await openPanelWithSuggestions(win);

      const groupsBefore = win.gBrowser.tabGroups.length;
      const tabsBefore = win.gBrowser.tabs.length;
      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "Both groups created"
      );
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 2,
        "Both created groups are listed under 'Just created'"
      );
      const groupIcons = [
        ...panel.querySelectorAll(".swgt-recent-row .swgt-group-icon"),
      ];
      Assert.ok(
        groupIcons.every(icon =>
          icon.src.endsWith("tabbrowser/tab-group-chicklet.svg")
        ),
        "Created groups are marked with the shared tab group chicklet"
      );
      Assert.deepEqual(
        groupIcons
          .map(icon =>
            icon.style.getPropertyValue("--tab-group-background-color")
          )
          .sort(),
        win.gBrowser.tabGroups.map(g => `var(--tab-group-${g.color})`).sort(),
        "Each chicklet is tinted with its own group's color"
      );
      Assert.ok(
        panel.querySelector(".swgt-ungroup"),
        "'Ungroup' footer is shown"
      );
      Assert.equal(
        panel
          .querySelector(".swgt-note .swgt-message")
          .getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-all-sorted",
        "Having created groups, the panel credits the work even with tabs left ungrouped"
      );

      const ungroup = panel.querySelector(".swgt-ungroup");
      ungroup.focus();
      Assert.equal(
        win.document.activeElement,
        ungroup,
        "The 'Ungroup' row can take focus"
      );
      ungroup.click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore,
        "Ungroup reverses every created group"
      );
      Assert.equal(
        win.gBrowser.tabs.length,
        tabsBefore,
        "Ungroup keeps the tabs open"
      );
      const requested =
        Glean.smartWindow.autoTabUngroupRequested.testGetValue();
      Assert.equal(requested?.length, 2, "One 'ungroup requested' per group");
      Assert.ok(
        requested.every(e => e.extra.source === "collective_ungroup"),
        "Both are recorded as a collective ungroup"
      );
      const ungrouped =
        Glean.smartWindow.autoTabUngroupCompleted.testGetValue();
      Assert.equal(ungrouped?.length, 2, "One 'ungroup completed' per group");
      Assert.ok(
        ungrouped.every(e => e.extra.success === "true"),
        "Both groups were ungrouped successfully"
      );
      Assert.deepEqual(
        ungrouped.map(e => e.extra.grouped_id),
        requested.map(e => e.extra.grouped_id),
        "Ungrouping reports the grouped_id the group was suggested under"
      );
      await TestUtils.waitForCondition(
        () => !panel.querySelectorAll(".swgt-recent-row").length,
        "The 'Just created' list is cleared"
      );
      await TestUtils.waitForCondition(
        () => win.document.activeElement === panel.querySelector(".swgt-card"),
        "Focus lands on the card once the 'Ungroup' row it was on is gone"
      );
    });

    it("switches to a group from its 'Just created' row", async () => {
      const panel = await openPanelWithSuggestions(win);

      panel.querySelector(".swgt-suggestion").click();
      const row = await TestUtils.waitForCondition(
        () => panel.querySelector(".swgt-recent-row"),
        "The created group is listed under 'Just created'"
      );
      const entry = AutoTabGrouping._getState(win).recent[0];

      Assert.ok(
        [...panel.querySelectorAll(".swgt-row")].includes(row),
        "The row is one of the rows the arrow keys move through"
      );
      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(
        () => accService.getAccessibleFor(row)?.name === entry.label,
        "The row is announced by the name of the group it made"
      );

      row.focus();
      Assert.equal(
        win.document.activeElement,
        row,
        "The row takes keyboard focus"
      );
      EventUtils.synthesizeKey("KEY_Enter", {}, win);
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Choosing the group closes the panel"
      );
      Assert.equal(
        win.gBrowser.selectedTab.group,
        entry.group,
        "The group's first tab is selected"
      );
    });

    it("only lists groups created while the panel was open", async () => {
      let panel = await openPanelWithSuggestions(win);

      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 2,
        "Both created groups are listed under 'Just created'"
      );

      await closePanel(win);
      panel = await openPanel(win);
      const card = panel.querySelector(".swgt-card");
      await card.updateComplete;

      Assert.ok(
        card.querySelector(".swgt-header"),
        "The reopened card rendered"
      );
      Assert.deepEqual(card.recent, [], "The 'Just created' list starts empty");
      Assert.ok(
        !card.querySelector(
          '[data-l10n-id="smartwindow-group-tabs-just-created-heading"]'
        ),
        "The 'Just created' heading is gone"
      );
      Assert.ok(
        !card.querySelector(".swgt-recent-row"),
        "None of the groups made last time are listed"
      );
      Assert.ok(
        !card.querySelector(".swgt-ungroup"),
        "The 'Ungroup' row is gone with them"
      );
      Assert.equal(
        win.gBrowser.tabGroups.length,
        2,
        "The groups themselves are left alone"
      );
    });
  });

  describe("when there is nothing left to suggest", () => {
    it("credits work done this time and forgets it the next", async () => {
      let panel = await openPanelWithSuggestions(win);
      const noteId = () =>
        panel
          .querySelector(".swgt-note .swgt-message")
          ?.getAttribute("data-l10n-id");

      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => panel.querySelectorAll(".swgt-recent-row").length === 2,
        "Both created groups are listed under 'Just created'"
      );
      Assert.equal(
        noteId(),
        "smartwindow-group-tabs-all-sorted",
        "Groups created this time earn the 'nice work' note"
      );

      await closePanel(win);
      panel = await openPanel(win);
      await TestUtils.waitForCondition(
        () => noteId() === "smartwindow-group-tabs-empty",
        "Reopened with nothing created, the panel just says it has no suggestions"
      );
    });
  });
});
