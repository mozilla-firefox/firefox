/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * The Smart Window's toolbar buttons: the ask button in the toolbox markup, and
 * the window switcher, "Organize Tabs" and monitor CustomizableUI buttons.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

export const GROUP_TABS_WIDGET_ID = "smartwindow-group-tabs-button";
export const MONITOR_WIDGET_ID = "smartwindow-monitor-button";
const ASK_BUTTON_ID = "smartwindow-ask-button";
const SWITCHER_WIDGET_ID = "ai-window-toggle";
const PREF_AGENT_SUPPORTED_REGIONS =
  "browser.smartwindow.agent.supportedRegions";
const PREF_AUTO_TAB_GROUPING_LOCALES =
  "browser.smartwindow.autoTabGrouping.supportedLocales";
// Nimbus-controlled, on the default branch: whether to announce the feature as
// new. Dismissing writes false to the user branch, which outranks the default
// branch, so the announcement does not come back when Nimbus re-applies it.
const PREF_MONITOR_ANNOUNCEMENT =
  "browser.smartwindow.agent.monitorAnnouncement";

const lazy = XPCOMUtils.declareLazy({
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AutoTabGrouping:
    "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGrouping.sys.mjs",
  AutoTabGroupingSuggestions:
    "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  MONITOR_CONDITION_MET_TOPIC:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MONITOR_RUN_FAILED_TOPIC:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MonitorAttention:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorAttention.sys.mjs",
  MonitorPanel:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorPanel.sys.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  agentEnabled: {
    pref: "browser.smartwindow.agent.enabled",
    onUpdate: () => AIWindowToolbarButtons.updateMonitorRegistration(),
  },
  agentToolbarEnabled: {
    pref: "browser.smartwindow.agent.toolbar.enabled",
    onUpdate: () => AIWindowToolbarButtons.updateMonitorRegistration(),
  },
  autoTabGroupingEnabled: {
    pref: "browser.smartwindow.autoTabGrouping.enabled",
    onUpdate: () => AIWindowToolbarButtons.updateRegistrations(),
  },
  // Enrolling mid-session has to light the dot without waiting for a restart.
  monitorAnnouncement: {
    pref: PREF_MONITOR_ANNOUNCEMENT,
    onUpdate: () => AIWindowToolbarButtons.updateMonitorButtons(),
  },
});

/**
 * Each button is registered only while its feature is on and Smart Window is
 * not blocked by browser.ai.control, so it appears in neither the toolbar nor
 * the customize palette otherwise. Placement is global across windows, so the
 * buttons that belong to Smart Windows are hidden per window in Classic ones.
 */
class ToolbarButtons {
  #switcherCreated = false;
  #groupTabsCreated = false;
  #monitorCreated = false;
  #initializedSwitcherViews = new WeakSet();

  init() {
    Services.obs.addObserver(this, "browser-region-updated");
    Services.obs.addObserver(this, "intl:app-locales-changed");
    Services.prefs.addObserver(PREF_AGENT_SUPPORTED_REGIONS, this);
    Services.prefs.addObserver(PREF_AUTO_TAB_GROUPING_LOCALES, this);
    this.updateRegistrations();
  }

  uninit() {
    Services.obs.removeObserver(this, "browser-region-updated");
    Services.obs.removeObserver(this, "intl:app-locales-changed");
    Services.prefs.removeObserver(PREF_AGENT_SUPPORTED_REGIONS, this);
    Services.prefs.removeObserver(PREF_AUTO_TAB_GROUPING_LOCALES, this);
  }

  /**
   * @param {nsISupports} _subject
   * @param {string} topic
   * @param {string} data - The changed pref, or the monitor id for the monitor
   *   topics.
   */
  observe(_subject, topic, data) {
    switch (topic) {
      case "nsPref:changed":
        if (data === PREF_AUTO_TAB_GROUPING_LOCALES) {
          // Nimbus applies this pref after session restore, so windows that
          // built the button before then have to catch up.
          this.#updateGroupTabsButtons();
        } else {
          this.updateMonitorRegistration();
        }
        break;
      // The supported markets, the home region and the app locale are all
      // inputs to the region gate on the monitor button.
      case "browser-region-updated":
      case "intl:app-locales-changed":
        this.updateMonitorRegistration();
        break;
      case lazy.MONITOR_CONDITION_MET_TOPIC:
        lazy.MonitorAttention.recordMatch(data);
        this.updateMonitorButtons();
        break;
      // A monitor that could not check is worth the same dot as one that
      // matched: either way there is something in the panel the user has not
      // seen.
      case lazy.MONITOR_RUN_FAILED_TOPIC:
        lazy.MonitorAttention.recordError(data);
        this.updateMonitorButtons();
        break;
    }
  }

  /**
   * Registers or removes every button, and updates each one's visibility, after
   * a change to whether Smart Window is enabled or blocked.
   */
  updateRegistrations() {
    if (lazy.AIWindow.isBlocked) {
      this.#destroySwitcherWidget();
    } else {
      this.#createSwitcherWidget();
    }
    if (lazy.autoTabGroupingEnabled && !lazy.AIWindow.isBlocked) {
      this.#createGroupTabsWidget();
    } else {
      this.#destroyGroupTabsWidget();
    }
    this.updateMonitorRegistration();
    for (const node of this.#nodes(SWITCHER_WIDGET_ID)) {
      node.hidden = !lazy.AIWindow.isAIWindowEnabled();
    }
  }

  updateMonitorRegistration() {
    if (this.#monitorButtonEnabled && !lazy.AIWindow.isBlocked) {
      this.#createMonitorWidget();
    } else {
      this.#destroyMonitorWidget();
    }
  }

  /**
   * Updates the ask and monitor buttons in a window after it switched between
   * Smart and Classic, or entered or left the immersive view. The ask button is
   * hidden in the immersive view, but the monitor button stays.
   *
   * @param {Window} win
   */
  updateWindow(win) {
    const doc = win.document;
    const askButton = doc.getElementById(ASK_BUTTON_ID);
    if (askButton) {
      askButton.hidden =
        !lazy.AIWindow.isAIWindowActive(win) ||
        doc.documentElement.hasAttribute("aiwindow-immersive-view");
    }
    const monitorButton = doc.getElementById(MONITOR_WIDGET_ID);
    if (monitorButton) {
      this.#updateMonitorNode(monitorButton);
    }
  }

  /**
   * Showing the "Organize Tabs" button preloads its models, so it is only
   * updated when a window switches between Smart and Classic.
   *
   * @param {Window} win
   */
  updateGroupTabsButton(win) {
    this.#updateGroupTabsButton(
      win.document.getElementById(GROUP_TABS_WIDGET_ID)
    );
  }

  /**
   * @param {Window} win
   * @param {boolean} expanded - Whether the sidebar the ask button opens is
   *   open.
   */
  setAskButtonExpanded(win, expanded) {
    win.document
      .getElementById("smartwindow-ask-button-inner")
      ?.setAttribute("aria-expanded", String(expanded));
  }

  /**
   * @param {Window} win
   * @param {string} id - GROUP_TABS_WIDGET_ID or MONITOR_WIDGET_ID.
   * @returns {Element | null} What a panel for the button anchors to: the
   *   button, or the overflow menu's chevron when the button is in that menu.
   */
  getAnchor(win, id) {
    return lazy.CustomizableUI.getWidget(id)?.forWindow(win)?.anchor ?? null;
  }

  updateMonitorButtons() {
    for (const node of this.#nodes(MONITOR_WIDGET_ID)) {
      this.#updateMonitorNode(node);
    }
  }

  /**
   * Monitors that matched their condition and that the user has not been shown
   * the panel for since, newest match first. A monitor that only failed to
   * check is not here: it lights the dot but the panel leaves it where it is.
   *
   * @returns {string[]} Monitor ids.
   */
  get monitorAttentionIds() {
    return lazy.MonitorAttention.matchedIds;
  }

  /**
   * Whether the feature is still being announced as new. How long the
   * announcement runs is the rollout's business: unenrolling restores the
   * default branch and the dot goes with it.
   *
   * @returns {boolean}
   */
  get hasMonitorAnnouncement() {
    return lazy.monitorAnnouncement;
  }

  /**
   * The dot means "there is something here for you" whichever reason put it
   * there, so the button does not need to tell the two apart.
   *
   * @returns {boolean} Whether the monitor button should carry the dot.
   */
  get hasMonitorAttention() {
    return lazy.MonitorAttention.hasAttention || this.hasMonitorAnnouncement;
  }

  /**
   * The ids to highlight, clearing them so the next panel opening starts fresh.
   * Reading and clearing are one step because the two must not drift: clearing
   * first loses the highlight, reading first risks never clearing the dot.
   *
   * @returns {string[]} Monitor ids, newest match first.
   */
  takeMonitorAttentionIds() {
    const ids = this.monitorAttentionIds;
    this.clearMonitorAttention();
    return ids;
  }

  /**
   * Retires every reason the dot is showing. Opening the panel answers the
   * announcement as much as it answers a match, so both go at once.
   */
  clearMonitorAttention() {
    lazy.MonitorAttention.clearAttention();
    // Only dismiss an announcement that is actually running so we do not mask
    // a rollout that starts later. Its pref observer updates the buttons.
    if (lazy.monitorAnnouncement) {
      Services.prefs.setBoolPref(PREF_MONITOR_ANNOUNCEMENT, false);
      return;
    }
    this.updateMonitorButtons();
  }

  /**
   * The monitor button is the toolbar surface of the Smart Window agent, so it
   * needs Smart Window to be enabled, the agent feature and its own toolbar
   * gate, and the same region gate the rest of the feature uses. Where the
   * button cannot create a monitor it should not appear.
   *
   * @returns {boolean}
   */
  get #monitorButtonEnabled() {
    return (
      lazy.AIWindow.isAIWindowEnabled() &&
      lazy.agentEnabled &&
      lazy.agentToolbarEnabled &&
      lazy.MonitorUIUtils.isMonitorRegionSupported()
    );
  }

  /**
   * @param {string} id
   * @returns {Element[]} The widget's node in each window that has one.
   */
  #nodes(id) {
    return (lazy.CustomizableUI.getWidget(id)?.instances ?? [])
      .map(({ node }) => node)
      .filter(Boolean);
  }

  #createSwitcherWidget() {
    if (this.#switcherCreated) {
      return;
    }
    lazy.CustomizableUI.createWidget({
      id: SWITCHER_WIDGET_ID,
      l10nId: "toolbar-switcher-customizable-label-v2",
      type: "view",
      viewId: "ai-window-toggle-view",
      defaultArea: lazy.CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: lazy.CustomizableUI.AREA_NAVBAR,
      removable: true,
      showInPrivateBrowsing: false,
      onCreated: node => {
        node.setAttribute("badged", "true");
        node.classList.add("subviewbutton-nav");
        node.setAttribute("aria-haspopup", "true");
        node.hidden = !lazy.AIWindow.isAIWindowEnabled();
      },
      onViewShowing: event =>
        this.#onSwitcherViewShowing(event.target.documentGlobal),
    });
    this.#switcherCreated = true;
  }

  #destroySwitcherWidget() {
    if (this.#switcherCreated) {
      lazy.CustomizableUI.destroyWidget(SWITCHER_WIDGET_ID);
      this.#switcherCreated = false;
    }
  }

  /**
   * @param {Window} win - The window showing the switcher panel.
   */
  #onSwitcherViewShowing(win) {
    const view = lazy.PanelMultiView.getViewNode(
      win.document,
      "ai-window-toggle-view"
    );
    if (!lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      const isAIActive = lazy.AIWindow.isAIWindowActive(win);
      const classicSwitchButton = view.querySelector(
        "#ai-window-switch-classic"
      );
      const smartSwitchButton = view.querySelector("#ai-window-switch-ai");
      classicSwitchButton.hidden = false;
      smartSwitchButton.hidden = false;
      classicSwitchButton.toggleAttribute("checked", !isAIActive);
      smartSwitchButton.toggleAttribute("checked", isAIActive);
    }

    if (this.#initializedSwitcherViews.has(view)) {
      return;
    }
    view.addEventListener("command", event => {
      switch (event.target.id) {
        case "ai-window-switch-classic":
          lazy.AIWindow.toggleAIWindow(win, false);
          break;
        case "ai-window-switch-ai":
          lazy.AIWindow.launchWindow(
            win.gBrowser.selectedBrowser,
            false,
            "switch"
          );
          break;
      }
    });
    this.#initializedSwitcherViews.add(view);
  }

  #createGroupTabsWidget() {
    if (this.#groupTabsCreated) {
      return;
    }
    lazy.CustomizableUI.createWidget({
      id: GROUP_TABS_WIDGET_ID,
      l10nId: "smartwindow-organize-tabs-button",
      defaultArea: lazy.CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: lazy.CustomizableUI.AREA_NAVBAR,
      removable: true,
      showInPrivateBrowsing: false,
      onCreated: node => {
        node.setAttribute("aria-haspopup", "dialog");
        node.setAttribute("aria-expanded", "false");
        this.#updateGroupTabsButton(node);
      },
      onCommand: event => {
        lazy.AutoTabGrouping.toggleGroupTabsPanel(event.target.documentGlobal);
      },
    });
    this.#groupTabsCreated = true;
  }

  #destroyGroupTabsWidget() {
    if (this.#groupTabsCreated) {
      lazy.CustomizableUI.destroyWidget(GROUP_TABS_WIDGET_ID);
      this.#groupTabsCreated = false;
    }
  }

  /**
   * @param {Element} [node] This window's button, if it has one: the user can
   *   remove it from the toolbar.
   */
  #updateGroupTabsButton(node) {
    if (!node) {
      return;
    }
    node.hidden = !(
      lazy.autoTabGroupingEnabled &&
      lazy.AIWindow.isAIWindowActive(node.documentGlobal) &&
      lazy.AutoTabGroupingSuggestions.isAvailable
    );
    if (!node.hidden) {
      lazy.AutoTabGroupingSuggestions.preloadModels();
    }
  }

  #updateGroupTabsButtons() {
    for (const node of this.#nodes(GROUP_TABS_WIDGET_ID)) {
      this.#updateGroupTabsButton(node);
    }
  }

  #createMonitorWidget() {
    if (this.#monitorCreated) {
      return;
    }
    lazy.CustomizableUI.createWidget({
      id: MONITOR_WIDGET_ID,
      l10nId: "smartwindow-monitor-button",
      defaultArea: lazy.CustomizableUI.AREA_NAVBAR,
      removable: true,
      showInPrivateBrowsing: false,
      onCreated: node => {
        node.setAttribute("aria-haspopup", "dialog");
        node.setAttribute("aria-expanded", "false");
        // Must be set before the node is connected, the toolbarbutton custom
        // element only builds the badge stack once, on its first render.
        node.setAttribute("badged", "true");
        this.#updateMonitorNode(node);
      },
      onCommand: event => {
        lazy.MonitorPanel.toggleMonitorPanel(event.view);
      },
    });
    Services.obs.addObserver(this, lazy.MONITOR_CONDITION_MET_TOPIC);
    Services.obs.addObserver(this, lazy.MONITOR_RUN_FAILED_TOPIC);
    this.#monitorCreated = true;
  }

  #destroyMonitorWidget() {
    if (!this.#monitorCreated) {
      return;
    }
    Services.obs.removeObserver(this, lazy.MONITOR_CONDITION_MET_TOPIC);
    Services.obs.removeObserver(this, lazy.MONITOR_RUN_FAILED_TOPIC);
    lazy.CustomizableUI.destroyWidget(MONITOR_WIDGET_ID);
    this.#monitorCreated = false;
  }

  /**
   * @param {Element} node
   */
  #updateMonitorNode(node) {
    node.hidden = !(
      this.#monitorButtonEnabled &&
      lazy.AIWindow.isAIWindowActive(node.documentGlobal)
    );
    node.toggleAttribute("monitor-attention", this.hasMonitorAttention);
  }
}

export const AIWindowToolbarButtons = new ToolbarButtons();
