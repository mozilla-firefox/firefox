/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  MONITOR_AGENTS_CHANGED_TOPIC:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MonitorAgent:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs",
  TOTAL_NUM_MONITORS:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
  isAllowedWatchUrl:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "gBundle", function () {
  return Services.strings.createBundle(
    "chrome://browser/locale/browser.properties"
  );
});

const BUTTON_ID = "smartwindow-monitor-button";
const PANEL_ID = "smartwindow-monitor-panel";
const TITLE_ID = "smartwindow-monitor-panel-title";
const TASKS_PAGE_URL = "about:smartwindowtasks";

// The header names whichever view is showing.
const VIEW_TITLE_L10N_IDS = {
  list: "smartwindow-monitor-panel-title",
  create: "smartwindow-monitor-panel-create-title",
};

/**
 * The Smart Window "Tasks" panel, anchored to its toolbar button. The panel is
 * created on demand and removed when it closes.
 *
 * Monitors live in MonitorAgent and change from its own timers as well as from
 * other surfaces, so this module owns the data: it pushes monitors into the
 * agent-monitor-panel element and refreshes them while the panel is open. The
 * element only renders and reports what the user did.
 */
export const MonitorPanel = {
  /**
   * @param {ChromeWindow} win
   */
  toggleMonitorPanel(win) {
    const doc = win?.document;
    if (!doc) {
      return;
    }

    const existing = this._getOpenPanel(doc);
    if (existing) {
      existing.hidePopup();
      return;
    }

    this.showMonitorPanel(win);
  },

  /**
   * Find opened panel.
   *
   * @param {Document} doc
   * @returns {?XULElement}
   */
  _getOpenPanel(doc) {
    const panel = doc.getElementById(PANEL_ID);
    if (panel?.state === "closed") {
      panel.remove();
      return null;
    }
    return panel;
  },

  /**
   * Open the panel straight to the create form.
   *
   * @param {ChromeWindow} win
   */
  showCreateForm(win) {
    const doc = win?.document;
    if (!doc) {
      return;
    }

    const existing = this._getOpenPanel(doc);
    if (existing) {
      this._openCreateView(existing, win);
      return;
    }

    this.showMonitorPanel(win, { create: true });
  },

  /**
   * @param {ChromeWindow} win
   * @param {object} [options]
   * @param {boolean} [options.create] Open straight to the create form.
   */
  showMonitorPanel(win, { create = false } = {}) {
    const doc = win.document;
    const button = doc.getElementById(BUTTON_ID);
    const popupSet = doc.getElementById("mainPopupSet");
    if (!button || !popupSet) {
      return;
    }

    // Opening the panel answers the attention dot. Taking the ids and
    // it only clears once the panel has committed to opening.
    const attentionIds = lazy.AIWindow.takeMonitorAttentionIds();

    // Anchor to the overflow button rather than the button itself when the
    // widget has been moved into the overflow panel.
    const anchor =
      lazy.CustomizableUI.getWidget(BUTTON_ID)?.forWindow(win)?.anchor ??
      button;

    const panel = this._createPanel(win);
    panel._contents.attentionIds = attentionIds;
    const onMonitorsChanged = () => this._syncContents(panel);
    let observingMonitors = false;

    panel.addEventListener(
      "popupshown",
      () => {
        button.setAttribute("aria-expanded", "true");
        observingMonitors = true;
        Services.obs.addObserver(
          onMonitorsChanged,
          lazy.MONITOR_AGENTS_CHANGED_TOPIC
        );
        if (create) {
          this._openCreateView(panel, win);
        }
      },
      { once: true }
    );
    const teardown = () => {
      win.removeEventListener("unload", teardown);
      button.setAttribute("aria-expanded", "false");
      // A click landing while the panel is still opening hides it without it
      // ever having been shown, so the observer may never have been added.
      // Removing one that isn't registered throws, which used to leave the
      // panel node behind and the button unable to open anything again.
      if (observingMonitors) {
        observingMonitors = false;
        Services.obs.removeObserver(
          onMonitorsChanged,
          lazy.MONITOR_AGENTS_CHANGED_TOPIC
        );
      }
      panel.remove();
    };
    panel.addEventListener("popuphidden", teardown, { once: true });
    win.addEventListener("unload", teardown, { once: true });

    popupSet.appendChild(panel);
    panel.openPopup(anchor, "bottomright topright", 0, 4, false, false);
    this._syncContents(panel);
  },

  /**
   * @param {ChromeWindow} win
   * @returns {XULElement} A detached panel with its header and contents.
   */
  _createPanel(win) {
    const doc = win.document;

    const panel = doc.createXULElement("panel");
    panel.id = PANEL_ID;
    panel.setAttribute("type", "arrow");
    panel.setAttribute("orient", "vertical");
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-labelledby", TITLE_ID);

    const header = doc.createXULElement("box");
    header.className = "panel-header";
    const heading = doc.createElement("h1");
    heading.id = TITLE_ID;
    const title = doc.createElement("span");
    heading.appendChild(title);
    header.appendChild(heading);
    panel._header = header;
    panel._title = title;

    const contents = doc.createElement("agent-monitor-panel");
    contents.maxMonitors = lazy.TOTAL_NUM_MONITORS;
    contents.addEventListener("agent-monitor-panel:create-task", () =>
      this._openCreateView(panel, win)
    );
    contents.addEventListener("agent-monitor-item:cancel", () =>
      this._setView(panel, "list")
    );
    contents.addEventListener("agent-monitor-item:submit", event =>
      this._onCreateSubmit(panel, event.detail)
    );
    // The form is torn down whenever the panel closes or the view changes, so
    // the draft is held out here to survive that.
    contents.addEventListener("agent-monitor-item:draft-change", event => {
      contents.draft = event.detail.draft;
    });
    contents.addEventListener("agent-monitor-item:resolve-titles", event =>
      lazy.MonitorUIUtils.answerResolveTitles(event)
    );
    contents.addEventListener("agent-monitor-panel:manage-tasks", () => {
      win.switchToTabHavingURI(TASKS_PAGE_URL, true);
      panel.hidePopup();
    });
    contents.addEventListener("agent-monitor-panel:open-task", event =>
      this._onOpenTask(panel, win, event.detail.id)
    );
    panel._contents = contents;

    panel.append(header, doc.createXULElement("toolbarseparator"), contents);
    this._setView(panel, "list");
    return panel;
  },

  /**
   * The tab group each task was last opened into, per window, so that opening
   * the same task twice returns to its group rather than building a second
   * identical one. Groups are tracked by id because a label is neither unique
   * nor stable - the user can rename either the task or the group.
   *
   * @type {WeakMap<ChromeWindow, Map<string, string>>}
   */
  _taskTabGroupIds: new WeakMap(),

  /**
   * Opens the pages a task watches together in a tab group named after it, so
   * its result stays separable from the rest of the session. The panel closes
   * on the way, like the other things it can take you to.
   *
   * @param {XULElement} panel
   * @param {ChromeWindow} win
   * @param {string} id - The monitor whose row was activated.
   */
  _onOpenTask(panel, win, id) {
    // Read the task before hiding the panel, which tears the contents down.
    const monitor = panel._contents.monitors?.find(m => m.id === id);
    const urls = (monitor?.watchUrls ?? []).filter(lazy.isAllowedWatchUrl);
    if (!urls.length) {
      return;
    }
    panel.hidePopup();

    const existingGroup = this._existingTaskGroup(win, id);
    if (existingGroup) {
      existingGroup.collapsed = false;
      win.gBrowser.selectedTab = existingGroup.tabs[0];
      return;
    }

    const tabs = this._openTaskTabs(win, urls);
    this._groupTaskTabs(win, id, tabs, monitor.monitorName);
    win.gBrowser.selectedTab = tabs[0];
  },

  /**
   * @param {ChromeWindow} win
   * @param {string} id - The monitor whose row was activated.
   * @returns {?MozTabbrowserTabGroup} The group this task was already opened
   *   into in this window, if it is still around.
   */
  _existingTaskGroup(win, id) {
    const groupId = this._taskTabGroupIds.get(win)?.get(id);
    if (!groupId) {
      return null;
    }
    // Scoped to this window rather than gBrowser.getTabGroupById, which
    // searches every window and so can answer with a group that was since
    // dragged out of this one.
    return win.gBrowser.tabGroups.find(group => group.id === groupId) ?? null;
  },

  /**
   * Opens a tab per watched page. Only the tab that ends up selected loads
   * now, so opening a five-page task doesn't start five page loads at once.
   *
   * @param {ChromeWindow} win
   * @param {string[]} urls
   * @returns {MozTabbrowserTab[]} The tabs, in the order the pages were given.
   */
  _openTaskTabs(win, urls) {
    // Open in the default container, which is what the monitor itself used to
    // check these pages, so what the user is shown matches what was checked.
    // Load them as a null principal: no page asked for these, so they get
    // their own opaque origin rather than inheriting anyone's privileges.
    const triggeringPrincipal =
      Services.scriptSecurityManager.createNullPrincipal({});
    return urls.map((url, index) =>
      win.gBrowser.addTab(url, {
        triggeringPrincipal,
        inBackground: true,
        bulkOrderedOpen: true,
        createLazyBrowser: index > 0,
      })
    );
  },

  /**
   * Puts the tabs in a group named after the task and remembers it, so coming
   * back to the task returns to that group rather than building another.
   *
   * @param {ChromeWindow} win
   * @param {string} id - The monitor the tabs were opened for.
   * @param {MozTabbrowserTab[]} tabs
   * @param {string} label
   */
  _groupTaskTabs(win, id, tabs, label) {
    const group = win.gBrowser.addTabGroup(tabs, {
      label,
      metricsContext: {
        isUserTriggered: false,
        telemetrySource: lazy.TabMetrics.METRIC_SOURCE.SMART_WINDOW_TASKS,
      },
    });
    if (!group) {
      return;
    }
    const groupIds = this._taskTabGroupIds.get(win) ?? new Map();
    groupIds.set(id, group.id);
    this._taskTabGroupIds.set(win, groupIds);
  },

  /**
   * @param {ChromeWindow} win
   * @returns {string} The URL of the page the user is on, or "" when it isn't a
   *   page a monitor can watch (about: pages, view-source, and so on).
   */
  _watchableUrl(win) {
    const url = win.gBrowser?.currentURI?.spec ?? "";
    return lazy.isAllowedWatchUrl(url) ? url : "";
  },

  // Switching to the create view while the list is already showing animates
  // the form sliding in.
  _openCreateView(panel, win) {
    panel._contents.agent = { url: this._watchableUrl(win) };
    this._setView(panel, "create");
  },

  /**
   * The header names the view and offers the way back out of it, so it and the
   * contents are switched together from here rather than tracking the view in
   * two places.
   *
   * @param {XULElement} panel
   * @param {"list"|"create"} view
   */
  _setView(panel, view) {
    const doc = panel.ownerDocument;
    panel._contents.view = view;
    doc.l10n.setAttributes(panel._title, VIEW_TITLE_L10N_IDS[view]);

    // The back button is added and removed rather than hidden: the stylesheet
    // centers the title by compensating for a back button whenever one precedes
    // it, hidden or not.
    const existing = panel._header.querySelector(".subviewbutton-back");
    if (view === "list") {
      existing?.remove();
      return;
    }
    if (existing) {
      return;
    }
    const backButton = doc.createXULElement("toolbarbutton");
    backButton.className =
      "subviewbutton subviewbutton-iconic subviewbutton-back";
    backButton.setAttribute("tabindex", "0");
    backButton.setAttribute("closemenu", "none");
    backButton.setAttribute(
      "aria-label",
      lazy.gBundle.GetStringFromName("panel.back")
    );
    backButton.addEventListener("command", () => this._setView(panel, "list"));
    panel._header.prepend(backButton);
  },

  /**
   * @param {XULElement} panel
   */
  async _syncContents(panel) {
    let monitors;
    try {
      monitors = await lazy.MonitorAgent.listMonitors();
    } catch (error) {
      console.error("Failed to list monitors:", error);
      return;
    }
    // The panel can close while the monitors are being read.
    if (!panel.isConnected) {
      return;
    }
    // Most recently checked first, so what the panel has to say about a
    // monitor is what is nearest the top. A monitor that has never run carries
    // its creation time as its last run, which sorts it as newly added putting it at the top
    // like we intend.
    panel._contents.monitors = monitors
      .sort((a, b) => new Date(b.lastRunTime) - new Date(a.lastRunTime))
      .map(monitor => lazy.MonitorUIUtils.formatMonitorForDisplay(monitor));
  },

  /**
   * @param {XULElement} panel
   * @param {object} detail - See agent-monitor-item's ":submit" event.
   */
  async _onCreateSubmit(panel, detail) {
    const { monitorName, condition, watchUrls, schedule } = detail;
    let id;
    try {
      id = await lazy.MonitorAgent.createMonitor({
        prompt: condition,
        watchUrls,
        pageTitle: monitorName,
        schedule: this._toAgentSchedule(schedule),
        source: "toolbar_panel",
      });
    } catch (error) {
      console.error("Failed to create monitor:", error);
      return;
    }
    if (!panel.isConnected) {
      return;
    }
    panel._contents.draft = null;
    panel._contents.justCreatedId = id;
    this._setView(panel, "list");
    // createMonitor notifies observers, which refreshes the list.
  },

  /**
   * agent-monitor-item reports the schedule the way its form is laid out, which
   * is not the shape MonitorAgent stores.
   *
   * @param {?object} schedule
   * @returns {?object}
   */
  _toAgentSchedule(schedule) {
    if (!schedule) {
      return null;
    }
    const [hour, minute] = schedule.time.split(":").map(Number);
    return {
      type: schedule.frequency,
      hour,
      minute,
      ...(schedule.weekday != null && { weekday: Number(schedule.weekday) }),
    };
  },
};
