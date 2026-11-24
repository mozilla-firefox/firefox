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
    it("shows the button, lists suggestions, and creates all groups", async () => {
      const button = win.document.getElementById(
        "smartwindow-group-tabs-button"
      );
      await TestUtils.waitForCondition(
        () => BrowserTestUtils.isVisible(button),
        "Group tabs button is visible on a content tab with the pref on"
      );

      const panel = await openPanelWithSuggestions(win);

      await TestUtils.waitForCondition(
        () => button.getAttribute("aria-expanded") === "true",
        "Button is marked expanded while the panel is open"
      );

      const createAll = panel.querySelector(".swgt-create-all");
      Assert.ok(createAll, "'Create all suggested groups' row exists");

      const groupsBefore = win.gBrowser.tabGroups.length;
      createAll.click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "Clicking 'Create all suggested groups' created both groups"
      );

      await TestUtils.waitForCondition(
        () => !panel.querySelectorAll(".swgt-suggestion").length,
        "The created suggestions leave the list"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Panel stays open after creating groups"
      );
      Assert.equal(
        button.getAttribute("aria-expanded"),
        "true",
        "Button is still marked expanded"
      );
    });

    it("does not suggest a name the user's groups already use", async () => {
      const tab = BrowserTestUtils.addTab(
        win.gBrowser,
        "https://example.com/grouped"
      );
      win.gBrowser.addTabGroup([tab], { label: "Test Group" });

      const panel = await openPanelWithSuggestions(win);
      Assert.deepEqual(
        [...panel.querySelectorAll(".swgt-suggestion .swgt-row-label")].map(
          el => el.textContent
        ),
        ["Test Group 2", "Test Group 3"],
        "Both suggestions are named apart from the existing group and from each other"
      );
    });

    it("opens the panel when the button is clicked", async () => {
      const button = win.document.getElementById(
        "smartwindow-group-tabs-button"
      );
      await TestUtils.waitForCondition(() =>
        BrowserTestUtils.isVisible(button)
      );

      EventUtils.synthesizeMouseAtCenter(button, {}, win);
      await TestUtils.waitForCondition(
        () => win.document.getElementById("smartwindow-group-tabs-panel"),
        "Clicking the button opens the panel"
      );
      await TestUtils.waitForCondition(
        () => button.getAttribute("aria-expanded") === "true",
        "The clicked button is marked expanded"
      );
      await closePanel(win);
    });

    it("records the clustering pipeline, and asks again when reopened", async () => {
      const tabs = String(win.gBrowser.tabs.length);

      await openPanelWithSuggestions(win);

      const opened = Glean.smartWindow.autoTabGroupMenuOpened.testGetValue();
      Assert.equal(opened?.length, 1, "One 'menu opened' event recorded");
      Assert.equal(opened[0].extra.source, "button", "Opened by the button");
      Assert.equal(opened[0].extra.tabs, tabs, "Recorded the open tab count");

      const requested =
        Glean.smartWindow.autoTabGroupingRequested.testGetValue();
      Assert.equal(requested?.length, 1, "One 'requested' event recorded");
      Assert.equal(
        requested[0].extra.tabs,
        tabs,
        "Recorded the open tab count"
      );

      const started = Glean.smartWindow.autoTabGroupingStarted.testGetValue();
      Assert.equal(started?.length, 1, "One 'started' event recorded");
      Assert.greater(
        Number(started[0].extra.total_length),
        0,
        "Reported how much tab-title text the model was given"
      );

      const completed =
        Glean.smartWindow.autoTabGroupingCompleted.testGetValue();
      Assert.equal(completed?.length, 1, "One 'completed' event recorded");
      Assert.equal(completed[0].extra.success, "true", "Clustering succeeded");
      Assert.equal(completed[0].extra.groups, "2", "Suggested two groups");
      Assert.equal(
        completed[0].extra.grouped_tabs,
        "4",
        "Placed four tabs into those groups"
      );
      Assert.greater(
        Number(completed[0].extra.total_length),
        0,
        "Reported how much tab-title text ended up in the suggestions"
      );

      const suggested = Glean.smartWindow.autoTabGroupSuggested.testGetValue();
      Assert.equal(suggested?.length, 2, "One event per suggested group");
      Assert.ok(
        suggested.every(e => e.extra.grouped_tabs === "2"),
        "Each suggested group holds two tabs"
      );
      Assert.equal(
        new Set(suggested.map(e => e.extra.grouped_id)).size,
        2,
        "The two groups are told apart by grouped_id"
      );

      const displayed =
        Glean.smartWindow.autoTabGroupWindowDisplay.testGetValue();
      Assert.equal(displayed?.length, 1, "One 'window display' event recorded");
      Assert.equal(
        displayed[0].extra.suggested_groups,
        "2",
        "Showed both suggestions"
      );
      Assert.equal(displayed[0].extra.groups, "0", "Nothing created yet");
      Assert.equal(
        displayed[0].extra.waited_out,
        "false",
        "Clustering finished before the panel stopped waiting"
      );

      await closePanel(win);
      await openPanelWithSuggestions(win);
      Assert.equal(
        Glean.smartWindow.autoTabGroupingRequested.testGetValue().length,
        2,
        "Reopening the panel asks for suggestions again"
      );
    });

    it("records accepting every suggestion at once", async () => {
      const panel = await openPanelWithSuggestions(win);
      const suggestedIds = Glean.smartWindow.autoTabGroupSuggested
        .testGetValue()
        .map(e => e.extra.grouped_id);

      const groupsBefore = win.gBrowser.tabGroups.length;
      panel.querySelector(".swgt-create-all").click();
      await TestUtils.waitForCondition(
        () => win.gBrowser.tabGroups.length === groupsBefore + 2,
        "Both groups created"
      );

      const accepted = Glean.smartWindow.autoTabGroupAccepted.testGetValue();
      Assert.equal(accepted?.length, 2, "One 'accepted' event per group");
      Assert.ok(
        accepted.every(e => e.extra.source === "collective_accept"),
        "Both are recorded as a collective accept"
      );
      Assert.deepEqual(
        accepted.map(e => e.extra.grouped_id),
        suggestedIds,
        "Accepting reports the same grouped_id the suggestion did"
      );

      const completed = Glean.smartWindow.autoTabGroupCompleted.testGetValue();
      Assert.equal(completed?.length, 2, "One 'completed' event per group");
      Assert.ok(
        completed.every(e => e.extra.success === "true"),
        "Both groups were created successfully"
      );
      Assert.ok(
        completed.every(e => e.extra.grouped_tabs === "2"),
        "Each created group holds two tabs"
      );
    });
  });
});
