/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const WIDGET_ID = "smartwindow-monitor-button";
const PANEL_ID = "smartwindow-monitor-panel";
const TEST_REGION = "US";
const TEST_MARKETS = JSON.stringify([[TEST_REGION, ["en-*"]]]);

const {
  TOTAL_NUM_MONITORS,
  MONITOR_CONDITION_MET_TOPIC,
  MONITOR_RUN_FAILED_TOPIC,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs"
);

const { MonitorAgent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs"
);

const { MonitorPanel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/MonitorPanel.sys.mjs"
);

const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);

function notifyMatch(monitorId) {
  Services.obs.notifyObservers(null, MONITOR_CONDITION_MET_TOPIC, monitorId);
}

function notifyRunFailed(monitorId) {
  Services.obs.notifyObservers(null, MONITOR_RUN_FAILED_TOPIC, monitorId);
}

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.urlbar.suggest.searches", false],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.firstrun.hasCompleted", true],
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.agent.enabled", true],
      ["browser.smartwindow.agent.toolbar.enabled", true],
      ["browser.smartwindow.agent.supportedRegions", TEST_MARKETS],
    ],
  });

  // Pin the home region, otherwise the region gate decides whether the button
  // exists at all and every test here becomes a coin toss.
  const originalRegion = Region.home;
  Region._setHomeRegion(TEST_REGION, false);

  AIWindow._updateMonitorWidgetRegistration();

  registerCleanupFunction(() => {
    Region._setHomeRegion(originalRegion, false);
    Services.prefs.clearUserPref(
      "browser.smartwindow.lastSmartWindowUsageTime"
    );
    Services.prefs.clearUserPref("browser.smartwindow.lastLLMTelemetryRunTime");
  });
});

function getMonitorButton(win) {
  return win.document.getElementById(WIDGET_ID);
}

/**
 * Clicking the button opens a panel anchored to it, and clicking again closes
 * it. The panel is removed from the DOM once hidden.
 */
add_task(async function test_monitor_panel_toggles() {
  const win = await openAIWindow();
  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );

    const monitorButton = getMonitorButton(win);
    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(monitorButton, {}, win);
    const panel = (await shown).target;

    Assert.equal(panel.id, PANEL_ID, "Clicking the button opens the panel");
    Assert.equal(
      panel.anchorNode?.id,
      WIDGET_ID,
      "Panel is anchored to the monitor button"
    );
    Assert.equal(
      monitorButton.getAttribute("aria-expanded"),
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

    const contents = panel.querySelector("agent-monitor-panel");
    Assert.ok(contents, "Panel hosts the agent-monitor-panel element");
    await contents.updateComplete;
    Assert.equal(contents.view, "list", "Panel opens on the list view");
    Assert.equal(
      contents.maxMonitors,
      TOTAL_NUM_MONITORS,
      "Contents know how many tasks are allowed"
    );

    // A second click must close the panel and leave it closed: the rollup on
    // mousedown and the command event that follows must not fight each other.
    let reopened = false;
    const onShown = () => {
      reopened = true;
    };
    const popupSet = win.document.getElementById("mainPopupSet");
    popupSet.addEventListener("popupshown", onShown);
    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    EventUtils.synthesizeMouseAtCenter(monitorButton, {}, win);
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
      monitorButton.getAttribute("aria-expanded"),
      "false",
      "Button reports the panel as collapsed again"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * A click that lands while the panel is still opening hides it before it was
 * ever shown, so teardown runs without the observer the shown panel would have
 * added. Teardown has to survive that: if it doesn't, the panel node is left
 * behind and every later click toggles a panel the user cannot see, leaving the
 * button dead for the rest of the session.
 */
add_task(async function test_monitor_panel_click_while_opening() {
  const win = await openAIWindow();
  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );
    const monitorButton = getMonitorButton(win);

    // Two clicks in the same turn, so the second one lands before the panel
    // has finished opening.
    MonitorPanel.toggleMonitorPanel(win);
    MonitorPanel.toggleMonitorPanel(win);

    await TestUtils.waitForCondition(
      () => !win.document.getElementById(PANEL_ID),
      "Panel hidden before it was shown is still removed from the DOM"
    );
    Assert.equal(
      monitorButton.getAttribute("aria-expanded"),
      "false",
      "Button reports the panel as collapsed"
    );

    // The button has to keep working afterwards.
    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(monitorButton, {}, win);
    const panel = (await shown).target;
    Assert.equal(panel.id, PANEL_ID, "The button still opens the panel");

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The list names each task the user is watching, most recently checked first,
 * and states when it checks rather than when it last ran.
 */
add_task(async function test_monitor_panel_list_rows() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([
    {
      id: "monitor-1",
      title: "Concert tickets",
      monitorPrompt: "tickets go on sale",
      watchUrls: ["https://example.com/tickets"],
      enabled: false,
      createdAt: "2026-01-01T00:00:00.000Z",
      lastRunTime: "2026-01-01T00:00:00.000Z",
      schedule: { type: "weekly", hour: 14, minute: 30, weekday: 3 },
      history: [],
    },
    {
      id: "monitor-2",
      title: "Ticket price",
      monitorPrompt: "price drops",
      watchUrls: ["https://example.com/price"],
      enabled: true,
      createdAt: "2026-02-01T00:00:00.000Z",
      lastRunTime: "2026-02-01T00:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [],
    },
  ]);
  const win = await openAIWindow();
  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(() => contents.monitors.length === 2);
    await contents.updateComplete;

    const rows = contents.shadowRoot.querySelectorAll(".monitor-row");
    Assert.equal(rows.length, 2, "Every monitor gets a row");
    Assert.deepEqual(
      [...rows].map(row => row.querySelector(".monitor-row-title").textContent),
      ["Ticket price", "Concert tickets"],
      "Rows name the monitors, newest first"
    );

    const metas = [...rows].map(row => row.querySelector(".monitor-row-meta"));
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

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

const DAY_DATE_URLS = [
  "https://example.com/day-date",
  "https://example.org/day-date",
];

function dayDateMonitor({ watchUrls = DAY_DATE_URLS } = {}) {
  return {
    id: "monitor-1",
    title: "Le Gran (Alsta) Day-Date price",
    monitorPrompt: "the price drops below $270",
    watchUrls,
    enabled: true,
    createdAt: "2026-01-01T00:00:00.000Z",
    schedule: { type: "daily", hour: 9, minute: 0 },
    history: [],
  };
}

async function openMonitorPanel(win, expectedMonitors) {
  const shown = BrowserTestUtils.waitForEvent(
    win.document.getElementById("mainPopupSet"),
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
  const panel = (await shown).target;
  const contents = panel.querySelector("agent-monitor-panel");
  await TestUtils.waitForCondition(
    () => contents.monitors.length === expectedMonitors
  );
  await contents.updateComplete;
  return { panel, contents };
}

/**
 * Activating a task opens every page it watches in a tab group named after the
 * task, dismisses the panel and selects the group's first tab.
 */
add_task(async function test_monitor_panel_row_opens_tab_group() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([dayDateMonitor()]);
  const win = await openAIWindow();
  try {
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
      "Le Gran (Alsta) Day-Date price",
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
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * Coming back to the same task returns to the group it already has rather than
 * building a second identical one, expanding it if it was collapsed.
 */
add_task(async function test_monitor_panel_row_reuses_tab_group() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([dayDateMonitor()]);
  const win = await openAIWindow();
  try {
    const activateRow = async () => {
      const { panel, contents } = await openMonitorPanel(win, 1);
      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      contents.shadowRoot.querySelector(".monitor-row").click();
      await hidden;
    };

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
    Assert.ok(!group.collapsed, "The group is expanded so its tabs are shown");
    Assert.equal(
      win.gBrowser.selectedTab,
      group.tabs[0],
      "The group's first tab is selected again"
    );
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * A task that watches nothing has nothing to open, so its row says it cannot be
 * activated and activating it anyway does nothing.
 */
add_task(async function test_monitor_panel_row_without_pages() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([
    dayDateMonitor({ watchUrls: [] }),
  ]);
  const win = await openAIWindow();
  try {
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

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The row is a button, so it is reachable and activatable from the keyboard.
 */
add_task(async function test_monitor_panel_row_keyboard_activation() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([dayDateMonitor()]);
  const win = await openAIWindow();
  try {
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
      win.gBrowser.tabGroups.length,
      1,
      "Enter on the row opens the task's pages as a group"
    );
    Assert.equal(
      win.gBrowser.tabGroups[0].label,
      "Le Gran (Alsta) Day-Date price",
      "The group is named after the task the keyboard activated"
    );
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The group is already named after the task, so the built-in "name your group"
 * editor must not pop over the pages that were just opened.
 */
add_task(async function test_monitor_panel_row_skips_group_editor() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([dayDateMonitor()]);
  const win = await openAIWindow();
  try {
    const { panel, contents } = await openMonitorPanel(win, 1);
    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    contents.shadowRoot.querySelector(".monitor-row").click();
    await hidden;
    await TestUtils.waitForTick();

    Assert.equal(
      win.gBrowser.tabGroups.length,
      1,
      "The task's group was created"
    );
    Assert.equal(
      win.document.getElementById("tab-group-editor").panel.state,
      "closed",
      "The group editor stays closed for a group that is already named"
    );
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * Monitors that newly matched since the panel was last opened are pulled into a
 * "New matches" section above "Recent", and their Match result wears a green dot.
 * Opening consumes the match, so reopening drops the section and the monitor
 * falls back under "Recent".
 */
add_task(async function test_monitor_panel_new_matches_section() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([
    {
      id: "monitor-1",
      title: "Concert tickets",
      monitorPrompt: "tickets go on sale",
      watchUrls: ["https://example.com/tickets"],
      enabled: true,
      createdAt: "2026-01-01T00:00:00.000Z",
      lastRunTime: "2026-01-01T00:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [],
    },
    {
      id: "monitor-2",
      title: "Ticket price",
      monitorPrompt: "price drops",
      watchUrls: ["https://example.com/price"],
      enabled: true,
      createdAt: "2026-02-01T00:00:00.000Z",
      lastRunTime: "2026-02-01T00:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [{ conditionMet: true }],
    },
  ]);
  const win = await openAIWindow();
  try {
    notifyMatch("monitor-2");
    await TestUtils.waitForCondition(() => AIWindow.hasMonitorAttention);

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(() => contents.monitors.length === 2);
    await contents.updateComplete;

    const labels = [
      ...contents.shadowRoot.querySelectorAll(".monitor-section-label"),
    ];
    Assert.deepEqual(
      labels.map(label => label.getAttribute("data-l10n-id")),
      [
        "smartwindow-monitor-panel-new-matches",
        "smartwindow-monitor-panel-watching",
      ],
      "The New matches section is listed above Recent"
    );

    const sections = [...contents.shadowRoot.querySelectorAll(".monitor-rows")];
    Assert.deepEqual(
      [...sections[0].querySelectorAll(".monitor-row-title")].map(
        title => title.textContent
      ),
      ["Ticket price"],
      "The newly matched monitor is under New matches"
    );
    Assert.deepEqual(
      [...sections[1].querySelectorAll(".monitor-row-title")].map(
        title => title.textContent
      ),
      ["Concert tickets"],
      "The rest stay under Recent"
    );

    Assert.ok(
      sections[0].querySelector(
        ".monitor-row-result.match .monitor-row-match-dot"
      ),
      "The matched row shows a green dot beside its Match status"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;

    // Opening consumed the match, so reopening has no New matches section and the
    // monitor now sits under Recent.
    const shownAgain = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panelAgain = (await shownAgain).target;
    const contentsAgain = panelAgain.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(() => contentsAgain.monitors.length === 2);
    await contentsAgain.updateComplete;

    Assert.deepEqual(
      [
        ...contentsAgain.shadowRoot.querySelectorAll(".monitor-section-label"),
      ].map(label => label.getAttribute("data-l10n-id")),
      ["smartwindow-monitor-panel-watching"],
      "Reopening drops the New matches section"
    );
    Assert.deepEqual(
      [...contentsAgain.shadowRoot.querySelectorAll(".monitor-row-title")].map(
        title => title.textContent
      ),
      ["Ticket price", "Concert tickets"],
      "The previously new match now sits under Recent with the others"
    );
    Assert.ok(
      !contentsAgain.shadowRoot.querySelector(".monitor-row-match-dot"),
      "The green dot is gone once the match moved back to Recent"
    );

    const hiddenAgain = BrowserTestUtils.waitForEvent(
      panelAgain,
      "popuphidden"
    );
    panelAgain.hidePopup();
    await hiddenAgain;
  } finally {
    sb.restore();
    AIWindow.clearMonitorAttention();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The list is ordered by when each task last checked rather than by when it
 * was created, so the freshest result is nearest the top.
 */
add_task(async function test_monitor_panel_orders_by_last_run() {
  const sb = this.sinon.createSandbox();
  // Deliberately at odds with creation order: the oldest task ran most
  // recently, and the newest has never run at all.
  sb.stub(MonitorAgent, "listMonitors").resolves([
    {
      id: "monitor-1",
      title: "Ran today",
      monitorPrompt: "tickets go on sale",
      watchUrls: ["https://example.com/tickets"],
      enabled: true,
      createdAt: "2026-01-01T00:00:00.000Z",
      lastRunTime: "2026-03-10T09:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [],
    },
    {
      id: "monitor-2",
      title: "Ran last week",
      monitorPrompt: "price drops",
      watchUrls: ["https://example.com/price"],
      enabled: true,
      createdAt: "2026-02-01T00:00:00.000Z",
      lastRunTime: "2026-03-03T09:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [],
    },
    {
      id: "monitor-3",
      title: "Never ran",
      monitorPrompt: "back in stock",
      watchUrls: ["https://example.com/stock"],
      enabled: true,
      createdAt: "2026-02-15T00:00:00.000Z",
      // Monitor defaults an unrun monitor's last run to its creation time.
      lastRunTime: "2026-02-15T00:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [],
    },
  ]);
  const win = await openAIWindow();
  try {
    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(() => contents.monitors.length === 3);
    await contents.updateComplete;

    Assert.deepEqual(
      [...contents.shadowRoot.querySelectorAll(".monitor-row-title")].map(
        title => title.textContent
      ),
      ["Ran today", "Ran last week", "Never ran"],
      "Tasks are listed by last check, most recent first, whatever order they were created in"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * A monitor whose last check failed says so on its own row rather than
 * claiming "No match", and it is not pulled into the "New matches" section
 * even though it lit the same dot.
 */
add_task(async function test_monitor_panel_failed_check_row() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([
    {
      id: "monitor-1",
      title: "Concert tickets",
      monitorPrompt: "tickets go on sale",
      watchUrls: ["https://example.com/tickets"],
      enabled: true,
      createdAt: "2026-01-01T00:00:00.000Z",
      lastRunTime: "2026-01-01T00:00:00.000Z",
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [{ status: "error", conditionMet: false }],
    },
  ]);
  const win = await openAIWindow();
  try {
    notifyRunFailed("monitor-1");
    await TestUtils.waitForCondition(() => AIWindow.hasMonitorAttention);

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(() => contents.monitors.length === 1);
    await contents.updateComplete;

    Assert.deepEqual(
      [...contents.shadowRoot.querySelectorAll(".monitor-section-label")].map(
        label => label.getAttribute("data-l10n-id")
      ),
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
    // The row and the task card report the same run, so they have to say the
    // same thing about it.
    const [rowCopy, cardCopy] = await contents.ownerDocument.l10n.formatValues([
      { id: "smartwindow-monitor-panel-result-could-not-check" },
      { id: "ai-tasks-alert-last-result-could-not-check" },
    ]);
    Assert.equal(
      rowCopy,
      cardCopy,
      "The panel row reads the same as the task card's last result"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    AIWindow.clearMonitorAttention();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The panel lists a handful of tasks rather than all of them, and new matches
 * take those slots first: enough of them fill the panel on their own, fewer
 * leave the remainder to Recent. The count in the footer still counts them all.
 */
add_task(async function test_monitor_panel_caps_visible_rows() {
  // Checked oldest first, so the panel renders "Task 7" down to "Task 1" and
  // a section that was capped reads differently from one that was not.
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves(
    Array.from({ length: 7 }, (_, index) => ({
      id: `monitor-${index + 1}`,
      title: `Task ${index + 1}`,
      monitorPrompt: "the page changed",
      watchUrls: ["https://example.com/"],
      enabled: true,
      createdAt: `2026-01-0${index + 1}T00:00:00.000Z`,
      lastRunTime: `2026-01-0${index + 1}T00:00:00.000Z`,
      schedule: { type: "daily", hour: 9, minute: 0 },
      history: [{ conditionMet: true }],
    }))
  );

  const openPanel = async win => {
    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(() => contents.monitors.length === 7);
    await contents.updateComplete;
    return { panel, contents };
  };
  const closePanel = async panel => {
    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  };
  const sectionTitles = contents =>
    [...contents.shadowRoot.querySelectorAll(".monitor-rows")].map(section =>
      [...section.querySelectorAll(".monitor-row-title")].map(
        title => title.textContent
      )
    );

  const win = await openAIWindow();
  try {
    // The two oldest tasks matched, so they show despite everything newer.
    notifyMatch("monitor-1");
    notifyMatch("monitor-2");
    await TestUtils.waitForCondition(() => AIWindow.hasMonitorAttention);

    let { panel, contents } = await openPanel(win);
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
    await closePanel(panel);

    // More new matches than there are slots: they take the panel entirely.
    for (let index = 1; index <= 6; index++) {
      notifyMatch(`monitor-${index}`);
    }
    await TestUtils.waitForCondition(() => AIWindow.hasMonitorAttention);

    ({ panel, contents } = await openPanel(win));
    Assert.deepEqual(
      sectionTitles(contents),
      [["Task 6", "Task 5", "Task 4", "Task 3", "Task 2"]],
      "The newest five matches fill the panel and Recent is dropped"
    );
    Assert.deepEqual(
      [...contents.shadowRoot.querySelectorAll(".monitor-section-label")].map(
        label => label.getAttribute("data-l10n-id")
      ),
      ["smartwindow-monitor-panel-new-matches"],
      "Only the New matches section is labelled"
    );
    await closePanel(panel);
  } finally {
    sb.restore();
    AIWindow.clearMonitorAttention();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * "Create new task" swaps the panel to the agent-monitor-item create form, and
 * submitting it creates a monitor through MonitorAgent and returns to the list.
 */
add_task(async function test_monitor_panel_create_view() {
  const sb = this.sinon.createSandbox();
  const createMonitor = sb.stub(MonitorAgent, "createMonitor").resolves("id-1");
  sb.stub(MonitorAgent, "listMonitors").resolves([]);
  const win = await openAIWindow();
  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await contents.updateComplete;

    contents.shadowRoot.querySelector(".monitor-footer-row").click();
    await contents.updateComplete;

    Assert.equal(contents.view, "create", "Panel switches to the create view");
    const form = contents.shadowRoot.querySelector("agent-monitor-item");
    Assert.ok(form, "Create view renders the monitor item form");
    Assert.equal(form.mode, "create", "Monitor item is in create mode");
    Assert.ok(
      !form.selfContained,
      "The panel frames and titles the form itself"
    );

    form.dispatchEvent(
      new win.CustomEvent("agent-monitor-item:submit", {
        detail: {
          mode: "create",
          monitorName: "Ticket price",
          condition: "price drops",
          watchUrls: ["https://example.com/"],
          schedule: { frequency: "weekly", time: "09:30", weekday: 3 },
        },
        bubbles: true,
        composed: true,
      })
    );
    await TestUtils.waitForCondition(() => contents.view === "list");

    Assert.ok(createMonitor.calledOnce, "Submitting creates one monitor");
    Assert.deepEqual(
      createMonitor.firstCall.args[0],
      {
        prompt: "price drops",
        watchUrls: ["https://example.com/"],
        pageTitle: "Ticket price",
        schedule: { type: "weekly", hour: 9, minute: 30, weekday: 3 },
        source: "toolbar_panel",
      },
      "Form values are mapped to the shape MonitorAgent stores"
    );
    Assert.equal(
      contents.justCreatedId,
      "id-1",
      "The new monitor is named so its row can animate in"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_monitor_panel_show_create_form() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([]);
  const win = await openAIWindow();
  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    AIWindowUI.showMonitorCreateForm(win);
    const panel = (await shown).target;
    Assert.equal(panel.id, PANEL_ID, "showMonitorCreateForm opens the panel");

    const contents = panel.querySelector("agent-monitor-panel");
    await TestUtils.waitForCondition(
      () => contents.view === "create",
      "Panel opens straight to the create view"
    );
    await contents.updateComplete;

    const form = contents.shadowRoot.querySelector("agent-monitor-item");
    Assert.ok(form, "Create view renders the monitor item form");
    Assert.equal(form.mode, "create", "Monitor item is in create mode");

    await TestUtils.waitForCondition(
      () => form.pageUrls.length === 1,
      "The create form is seeded with the current page"
    );
    Assert.deepEqual(
      form.pageUrls,
      ["https://example.com/"],
      "The seeded page is the one the user is on"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The OPEN_SMARTWINDOW_MONITOR_CREATE special message action - the callout's
 * primary button
 */
add_task(async function test_open_monitor_create_special_action() {
  const win = await openAIWindow();
  const sb = this.sinon.createSandbox();
  const showCreateForm = sb.stub(AIWindowUI, "showMonitorCreateForm");
  try {
    await SpecialMessageActions.handleAction(
      { type: "OPEN_SMARTWINDOW_MONITOR_CREATE" },
      { documentGlobal: win }
    );

    Assert.ok(
      showCreateForm.calledOnceWith(win),
      "The action opens the create form for the action's window"
    );
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The create form opens watching the page the user is on, unless that page is
 * one a monitor cannot watch.
 */
add_task(async function test_monitor_panel_create_seeds_current_page() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([]);
  const win = await openAIWindow();

  const openCreateForm = async () => {
    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await contents.updateComplete;
    contents.shadowRoot.querySelector(".monitor-footer-row").click();
    await contents.updateComplete;
    const form = contents.shadowRoot.querySelector("agent-monitor-item");
    await form.updateComplete;
    return { panel, form };
  };

  const closePanel = async panel => {
    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  };

  const pageChips = form =>
    form.shadowRoot.querySelectorAll(".page-pills-row smartwindow-chip");

  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );

    let { panel, form } = await openCreateForm();
    // pageUrls is seeded a render before the chip is drawn, so wait for it.
    await TestUtils.waitForCondition(
      () => pageChips(form).length === 1,
      "Waiting for the seeded page to render as a chip"
    );
    Assert.deepEqual(
      form.pageUrls,
      ["https://example.com/"],
      "The form opens watching the page the user is on"
    );
    Assert.equal(
      pageChips(form).length,
      1,
      "The seeded page is shown as a chip, so the user can drop it"
    );
    await closePanel(panel);

    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, "about:robots");
    ({ panel, form } = await openCreateForm());
    Assert.deepEqual(
      form.pageUrls,
      [],
      "A page no monitor can watch seeds nothing"
    );
    await closePanel(panel);
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_monitor_panel_create_chip_shows_page_title() {
  const OPEN_URL =
    "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_context_url_page.html";
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([]);
  const win = await openAIWindow();

  try {
    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, OPEN_URL);
    const openTitle = win.gBrowser.selectedBrowser.contentTitle;
    Assert.ok(openTitle, "The open test page has a title");

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    await contents.updateComplete;
    contents.shadowRoot.querySelector(".monitor-footer-row").click();
    await contents.updateComplete;
    const form = contents.shadowRoot.querySelector("agent-monitor-item");

    const chip = () =>
      form.shadowRoot.querySelector(".page-pills-row smartwindow-chip");
    await TestUtils.waitForCondition(
      () => chip()?.label === openTitle,
      "Waiting for the page title to reach the seeded chip"
    );
    Assert.equal(
      chip().label,
      openTitle,
      "The page the form was opened from is labelled with its title"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The panel header names whichever view is showing and only offers a back button
 * while there is somewhere to go back to.
 */
add_task(async function test_monitor_panel_header_follows_view() {
  const sb = this.sinon.createSandbox();
  sb.stub(MonitorAgent, "listMonitors").resolves([]);
  const win = await openAIWindow();
  try {
    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      "https://example.com/"
    );

    const shown = BrowserTestUtils.waitForEvent(
      win.document.getElementById("mainPopupSet"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
    const panel = (await shown).target;
    const contents = panel.querySelector("agent-monitor-panel");
    const title = panel.querySelector(".panel-header > h1 > span");
    await contents.updateComplete;

    Assert.ok(
      !panel.querySelector(".subviewbutton-back"),
      "The list view has no back button"
    );

    const openCreateView = async () => {
      contents.shadowRoot.querySelector(".monitor-footer-row").click();
      await contents.updateComplete;
      await win.document.l10n.translateFragment(panel);
    };

    await openCreateView();
    Assert.equal(
      title.textContent,
      "Create new task",
      "Header names the create view"
    );
    const backButton = panel.querySelector(".subviewbutton-back");
    Assert.ok(backButton, "Create view offers a back button");
    Assert.equal(
      backButton.getAttribute("aria-label"),
      "Back",
      "Back button is labelled for assistive technology"
    );

    backButton.doCommand();
    await contents.updateComplete;
    await win.document.l10n.translateFragment(panel);
    Assert.equal(contents.view, "list", "Back returns to the list view");
    Assert.equal(
      title.textContent,
      "Tasks",
      "Header names the list view again"
    );
    Assert.ok(
      !panel.querySelector(".subviewbutton-back"),
      "Back button is removed with the create view so the title stays centered"
    );

    // Cancelling the form is the other way back out of the create view.
    await openCreateView();
    contents.shadowRoot.querySelector("agent-monitor-item").dispatchEvent(
      new win.CustomEvent("agent-monitor-item:cancel", {
        detail: {},
        bubbles: true,
        composed: true,
      })
    );
    await contents.updateComplete;
    await win.document.l10n.translateFragment(panel);
    Assert.equal(contents.view, "list", "Cancelling returns to the list view");
    Assert.equal(
      title.textContent,
      "Tasks",
      "Header follows the cancelled view back"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  } finally {
    sb.restore();
    await BrowserTestUtils.closeWindow(win);
  }
});
