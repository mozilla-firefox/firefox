/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const TASK_TITLE = "Le Gran (Alsta) Day-Date price";
const DAY_DATE_URLS = [
  "https://example.com/day-date",
  "https://example.org/day-date",
];

let win;

add_setup(async function () {
  await setupMonitorButton();
  win = await openSharedAIWindow();
});

describe("Monitor panel task row", () => {
  let sandbox;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
  });

  afterEach(async () => {
    await closeMonitorPanel(win);
    sandbox.restore();
    while (win.gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(win.gBrowser.tabs.at(-1));
    }
  });

  describe("for a task that watches pages", () => {
    beforeEach(() => {
      sandbox.stub(MonitorAgent, "listMonitors").resolves([dayDateMonitor()]);
    });

    it("opens the pages in a tab group named after the task", async () => {
      const { panel, contents } = await openMonitorPanel(win, 1);
      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");

      contents.shadowRoot.querySelector(".monitor-row").click();

      await hidden;
      Assert.equal(
        win.gBrowser.tabGroups.length,
        1,
        "Activating a task creates one tab group"
      );
      const [group] = win.gBrowser.tabGroups;
      Assert.equal(
        group.label,
        TASK_TITLE,
        "The group is named after the task that was activated"
      );
      // The eagerly loaded tab reports about:blank until its load starts.
      await TestUtils.waitForCondition(
        () => group.tabs[0].linkedBrowser.currentURI.spec === DAY_DATE_URLS[0],
        "Waiting for the first watched page to start loading"
      );
      Assert.deepEqual(
        group.tabs.map(tab => tab.linkedBrowser.currentURI.spec),
        DAY_DATE_URLS,
        "The group holds one tab per watched page"
      );
      Assert.equal(
        win.gBrowser.selectedTab,
        group.tabs[0],
        "The group's first tab is selected"
      );
      Assert.ok(
        group.tabs[0].linkedPanel,
        "The tab that ends up selected loads right away"
      );
      Assert.ok(
        !group.tabs[1].linkedPanel,
        "The rest wait to load until they are selected"
      );
    });

    // Coming back to the same task returns to the group it already has
    // rather than building a second identical one.
    it("returns to the task's collapsed group and expands it", async () => {
      await activateRow();
      const [group] = win.gBrowser.tabGroups;
      group.collapsed = true;
      win.gBrowser.selectedTab = win.gBrowser.tabs[0];

      await activateRow();

      Assert.equal(
        win.gBrowser.tabGroups.length,
        1,
        "Coming back does not build a second group"
      );
      Assert.equal(
        win.gBrowser.tabGroups[0],
        group,
        "The task keeps the group it was already given"
      );
      Assert.equal(
        group.tabs.length,
        DAY_DATE_URLS.length,
        "Reopening the task does not open its pages a second time"
      );
      Assert.ok(
        !group.collapsed,
        "The group is expanded so its tabs are shown"
      );
      Assert.equal(
        win.gBrowser.selectedTab,
        group.tabs[0],
        "The group's first tab is selected again"
      );
    });

    it("opens the task's pages when Enter is pressed on it", async () => {
      const { panel, contents } = await openMonitorPanel(win, 1);
      const row = contents.shadowRoot.querySelector(".monitor-row");
      row.focus();
      Assert.equal(
        contents.shadowRoot.activeElement,
        row,
        "The row can take focus"
      );
      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");

      EventUtils.synthesizeKey("KEY_Enter", {}, win);

      await hidden;
      Assert.equal(
        win.gBrowser.tabGroups[0]?.label,
        TASK_TITLE,
        "Enter on the row opens the task's pages as a group named after it"
      );
    });

    // The group is already named after the task, so the built-in "name your
    // group" editor must not pop over the pages that were just opened.
    it("does not open the group editor", async () => {
      await activateRow();
      await TestUtils.waitForTick();

      Assert.equal(
        win.document.getElementById("tab-group-editor").panel.state,
        "closed",
        "The group editor stays closed for a group that is already named"
      );
    });
  });

  // A task that watches nothing has nothing to open.
  describe("for a task that watches no pages", () => {
    beforeEach(() => {
      sandbox
        .stub(MonitorAgent, "listMonitors")
        .resolves([dayDateMonitor({ watchUrls: [] })]);
    });

    it("cannot be activated", async () => {
      const { panel, contents } = await openMonitorPanel(win, 1);
      const row = contents.shadowRoot.querySelector(".monitor-row");
      Assert.equal(
        row.getAttribute("aria-disabled"),
        "true",
        "The row reports that it cannot be activated"
      );

      row.click();

      await TestUtils.waitForTick();
      Assert.equal(
        win.gBrowser.tabGroups.length,
        0,
        "Activating a task with no pages creates no tab group"
      );
      Assert.equal(panel.state, "open", "The panel stays open");
    });
  });
});

function dayDateMonitor({ watchUrls = DAY_DATE_URLS } = {}) {
  return {
    id: "monitor-1",
    title: TASK_TITLE,
    monitorPrompt: "the price drops below $270",
    watchUrls,
    enabled: true,
    createdAt: "2026-01-01T00:00:00.000Z",
    schedule: { type: "daily", hour: 9, minute: 0 },
    history: [],
  };
}

/**
 * Opens the panel and activates its only task row with the mouse.
 */
async function activateRow() {
  const { panel, contents } = await openMonitorPanel(win, 1);
  const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  contents.shadowRoot.querySelector(".monitor-row").click();
  await hidden;
}
