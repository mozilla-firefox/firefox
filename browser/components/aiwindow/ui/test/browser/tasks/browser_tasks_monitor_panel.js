/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TOTAL_NUM_MONITORS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs"
);

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const { MonitorPanel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/MonitorPanel.sys.mjs"
);

let win;

add_setup(async function () {
  await setupMonitorButton();
  win = await openSharedAIWindow();
});

describe("Monitor panel", () => {
  let sandbox;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
  });

  afterEach(async () => {
    await closeMonitorPanel(win);
    sandbox.restore();
    AIWindowToolbarButtons.clearMonitorAttention();
  });

  describe("when the button is clicked", () => {
    it("opens anchored to the button on the list view", async () => {
      const { panel, contents } = await openMonitorPanel(win);

      Assert.equal(panel.id, PANEL_ID, "Clicking the button opens the panel");
      Assert.equal(
        panel.anchorNode?.id,
        WIDGET_ID,
        "Panel is anchored to the monitor button"
      );
      Assert.equal(
        getMonitorButton(win).getAttribute("aria-expanded"),
        "true",
        "Button reports the panel as expanded"
      );
      const title = panel.querySelector(".panel-header > h1");
      await win.document.l10n.translateFragment(panel);
      Assert.equal(title.textContent, "Tasks", "Panel has a localized title");
      Assert.equal(
        panel.getAttribute("aria-labelledby"),
        title.id,
        "Panel is labelled by its title for assistive technology"
      );
      Assert.equal(contents.view, "list", "Panel opens on the list view");
      Assert.equal(
        contents.maxMonitors,
        TOTAL_NUM_MONITORS,
        "Contents know how many tasks are allowed"
      );
    });

    // The rollup on mousedown and the command event that follows must not
    // fight each other.
    it("closes on a second click and stays closed", async () => {
      const { panel } = await openMonitorPanel(win);
      let reopened = false;
      const onShown = () => {
        reopened = true;
      };
      const popupSet = win.document.getElementById("mainPopupSet");
      popupSet.addEventListener("popupshown", onShown);
      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");

      EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);

      await hidden;
      await TestUtils.waitForTick();
      popupSet.removeEventListener("popupshown", onShown);
      Assert.ok(!reopened, "Closing click does not reopen the panel");
      Assert.equal(
        win.document.getElementById(PANEL_ID),
        null,
        "Panel is removed from the DOM once hidden"
      );
      Assert.equal(
        getMonitorButton(win).getAttribute("aria-expanded"),
        "false",
        "Button reports the panel as collapsed again"
      );
    });

    // A click that lands while the panel is still opening hides it before it
    // was ever shown, so teardown runs without the observer the shown panel
    // would have added. If teardown doesn't survive that, the panel node is
    // left behind and the button is dead for the rest of the session.
    it("keeps working after a click lands while it is opening", async () => {
      MonitorPanel.toggleMonitorPanel(win);
      MonitorPanel.toggleMonitorPanel(win);
      await TestUtils.waitForCondition(
        () => !win.document.getElementById(PANEL_ID),
        "Panel hidden before it was shown is still removed from the DOM"
      );
      Assert.equal(
        getMonitorButton(win).getAttribute("aria-expanded"),
        "false",
        "Button reports the panel as collapsed"
      );

      const { panel } = await openMonitorPanel(win);

      Assert.equal(panel.id, PANEL_ID, "The button still opens the panel");
    });
  });

  describe("listing tasks", () => {
    // States when each task checks rather than when it last ran.
    it("names each task with its schedule and status", async () => {
      sandbox.stub(MonitorAgent, "listMonitors").resolves([
        monitor(1, {
          title: "Concert tickets",
          enabled: false,
          schedule: { type: "weekly", hour: 14, minute: 30, weekday: 3 },
        }),
        monitor(2, { title: "Ticket price" }),
      ]);

      const { contents } = await openMonitorPanel(win, 2);

      const rows = contents.shadowRoot.querySelectorAll(".monitor-row");
      Assert.deepEqual(
        [...rows].map(
          row => row.querySelector(".monitor-row-title").textContent
        ),
        ["Ticket price", "Concert tickets"],
        "Rows name the monitors, newest first"
      );
      const metas = [...rows].map(row =>
        row.querySelector(".monitor-row-meta")
      );
      Assert.deepEqual(
        metas.map(meta => meta.getAttribute("data-l10n-id")),
        [
          "ai-tasks-alert-schedule-daily-at",
          "ai-tasks-alert-schedule-weekly-wednesday",
        ],
        "Rows state the schedule the monitor checks on"
      );
      Assert.deepEqual(
        metas.map(meta => JSON.parse(meta.getAttribute("data-l10n-args")).time),
        [
          new Date(new Date().setHours(9, 0, 0, 0)).getTime(),
          new Date(new Date().setHours(14, 30, 0, 0)).getTime(),
        ],
        "The scheduled time of day is passed to the string"
      );
      await TestUtils.waitForCondition(() => metas.every(m => m.textContent));
      const count = contents.shadowRoot.querySelector(".monitor-footer-count");
      Assert.equal(
        JSON.parse(count.getAttribute("data-l10n-args")).used,
        1,
        "The footer counts only active monitors toward the limit"
      );
      const chips = [...rows].map(row =>
        row.querySelector("monitor-status-chip")
      );
      Assert.deepEqual(
        chips.map(chip => chip.kind),
        ["watching", "paused"],
        "Rows wear the same status pill the task cards do"
      );
      await TestUtils.waitForCondition(() =>
        chips.every(chip => chip.shadowRoot.querySelector("span")?.textContent)
      );
      Assert.deepEqual(
        chips.map(chip => chip.shadowRoot.querySelector("span").textContent),
        ["Active", "Paused"],
        "The pills are localized inside the panel"
      );
    });

    // The freshest result is nearest the top, whatever order the tasks were
    // created in.
    it("orders tasks by when they last checked", async () => {
      // The oldest task ran most recently, and the newest has never run.
      sandbox.stub(MonitorAgent, "listMonitors").resolves([
        monitor(1, {
          title: "Ran today",
          lastRunTime: "2026-03-10T09:00:00.000Z",
        }),
        monitor(2, {
          title: "Ran last week",
          lastRunTime: "2026-03-03T09:00:00.000Z",
        }),
        // Monitor defaults an unrun monitor's last run to its creation time.
        monitor(3, {
          title: "Never ran",
          createdAt: "2026-02-15T00:00:00.000Z",
          lastRunTime: "2026-02-15T00:00:00.000Z",
        }),
      ]);

      const { contents } = await openMonitorPanel(win, 3);

      Assert.deepEqual(
        rowTitles(contents),
        ["Ran today", "Ran last week", "Never ran"],
        "Tasks are listed by last check, most recent first"
      );
    });

    it("marks a task whose last check failed", async () => {
      sandbox
        .stub(MonitorAgent, "listMonitors")
        .resolves([
          monitor(1, { history: [{ status: "error", conditionMet: false }] }),
        ]);
      notifyRunFailed("monitor-1");

      const { contents } = await openMonitorPanel(win, 1);

      Assert.deepEqual(
        sectionLabels(contents),
        ["smartwindow-monitor-panel-watching"],
        "A failed check does not open a New matches section"
      );
      const result = contents.shadowRoot.querySelector(".monitor-row-result");
      Assert.ok(
        result.classList.contains("could-not-check"),
        "The row's result is marked as a failed check"
      );
      Assert.equal(
        result.getAttribute("data-l10n-id"),
        "smartwindow-monitor-panel-result-could-not-check",
        "The row says the check failed rather than that nothing matched"
      );
      // The row and the task card report the same run, so they have to say
      // the same thing about it.
      const [rowCopy, cardCopy] =
        await contents.ownerDocument.l10n.formatValues([
          { id: "smartwindow-monitor-panel-result-could-not-check" },
          { id: "ai-tasks-alert-last-result-could-not-check" },
        ]);
      Assert.equal(
        rowCopy,
        cardCopy,
        "The panel row reads the same as the task card's last result"
      );
    });
  });

  describe("with new matches", () => {
    it("lists them in a New matches section above Recent", async () => {
      sandbox.stub(MonitorAgent, "listMonitors").resolves([
        monitor(1, { title: "Concert tickets" }),
        monitor(2, {
          title: "Ticket price",
          history: [{ conditionMet: true }],
        }),
      ]);
      notifyMatch("monitor-2");

      const { contents } = await openMonitorPanel(win, 2);

      Assert.deepEqual(
        sectionLabels(contents),
        [
          "smartwindow-monitor-panel-new-matches",
          "smartwindow-monitor-panel-watching",
        ],
        "The New matches section is listed above Recent"
      );
      Assert.deepEqual(
        sectionTitles(contents),
        [["Ticket price"], ["Concert tickets"]],
        "The newly matched monitor is under New matches, the rest under Recent"
      );
      const newMatches = contents.shadowRoot.querySelector(".monitor-rows");
      Assert.ok(
        newMatches.querySelector(
          ".monitor-row-result.match .monitor-row-match-dot"
        ),
        "The matched row shows a green dot beside its Match status"
      );
    });

    // Opening consumes the match.
    it("moves them back under Recent once the panel has been opened", async () => {
      sandbox.stub(MonitorAgent, "listMonitors").resolves([
        monitor(1, { title: "Concert tickets" }),
        monitor(2, {
          title: "Ticket price",
          history: [{ conditionMet: true }],
        }),
      ]);
      notifyMatch("monitor-2");
      await openMonitorPanel(win, 2);
      await closeMonitorPanel(win);

      const { contents } = await openMonitorPanel(win, 2);

      Assert.deepEqual(
        sectionLabels(contents),
        ["smartwindow-monitor-panel-watching"],
        "Reopening drops the New matches section"
      );
      Assert.deepEqual(
        rowTitles(contents),
        ["Ticket price", "Concert tickets"],
        "The previously new match now sits under Recent with the others"
      );
      Assert.ok(
        !contents.shadowRoot.querySelector(".monitor-row-match-dot"),
        "The green dot is gone once the match moved back to Recent"
      );
    });

    // The panel lists a handful of tasks rather than all of them, and new
    // matches take those slots first. Checked oldest first, so the panel
    // renders "Task 7" down to "Task 1".
    it("leave the remaining slots to Recent when there are few", async () => {
      sandbox.stub(MonitorAgent, "listMonitors").resolves(sevenMatchedTasks());
      notifyMatch("monitor-1");
      notifyMatch("monitor-2");

      const { contents } = await openMonitorPanel(win, 7);

      Assert.deepEqual(
        sectionTitles(contents),
        [
          ["Task 2", "Task 1"],
          ["Task 7", "Task 6", "Task 5"],
        ],
        "Both new matches show and Recent fills the three slots left"
      );
      Assert.equal(
        contents.shadowRoot
          .querySelector(".monitor-footer-count")
          .getAttribute("data-l10n-args"),
        JSON.stringify({ used: 7, max: TOTAL_NUM_MONITORS }),
        "The footer still counts every task, not just the listed ones"
      );
    });

    it("fill the panel on their own when there are enough", async () => {
      sandbox.stub(MonitorAgent, "listMonitors").resolves(sevenMatchedTasks());
      for (let index = 1; index <= 6; index++) {
        notifyMatch(`monitor-${index}`);
      }

      const { contents } = await openMonitorPanel(win, 7);

      Assert.deepEqual(
        sectionTitles(contents),
        [["Task 6", "Task 5", "Task 4", "Task 3", "Task 2"]],
        "The newest five matches fill the panel and Recent is dropped"
      );
      Assert.deepEqual(
        sectionLabels(contents),
        ["smartwindow-monitor-panel-new-matches"],
        "Only the New matches section is labelled"
      );
    });
  });
});

/**
 * @param {number} index - Numbers the monitor's id and default title.
 * @param {object} [overrides] - Fields that differ from an enabled daily task.
 * @returns {object} A monitor as MonitorAgent.listMonitors returns it.
 */
function monitor(index, overrides = {}) {
  return {
    id: `monitor-${index}`,
    title: `Task ${index}`,
    monitorPrompt: "the page changed",
    watchUrls: [`https://example.com/${index}`],
    enabled: true,
    createdAt: `2026-0${index}-01T00:00:00.000Z`,
    lastRunTime: `2026-0${index}-01T00:00:00.000Z`,
    schedule: { type: "daily", hour: 9, minute: 0 },
    history: [],
    ...overrides,
  };
}

function sevenMatchedTasks() {
  return Array.from({ length: 7 }, (_, index) =>
    monitor(index + 1, { history: [{ conditionMet: true }] })
  );
}

function rowTitles(contents) {
  return [...contents.shadowRoot.querySelectorAll(".monitor-row-title")].map(
    title => title.textContent
  );
}

function sectionLabels(contents) {
  return [
    ...contents.shadowRoot.querySelectorAll(".monitor-section-label"),
  ].map(label => label.getAttribute("data-l10n-id"));
}

function sectionTitles(contents) {
  return [...contents.shadowRoot.querySelectorAll(".monitor-rows")].map(
    section =>
      [...section.querySelectorAll(".monitor-row-title")].map(
        title => title.textContent
      )
  );
}
