/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutoTabGroupingSuggestions:
    "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "AutoTabGrouping",
    maxLogLevelPref: "browser.smartwindow.autoTabGrouping.loglevel",
  })
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minTabsPerGroup",
  "browser.smartwindow.autoTabGrouping.minTabsPerGroup",
  2
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minCandidateTabs",
  "browser.smartwindow.autoTabGrouping.minCandidateTabs",
  4
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "timeoutMs",
  "browser.smartwindow.autoTabGrouping.timeoutMs",
  8000
);

const BUTTON_ITEM_ID = "smartwindow-group-tabs-button";
const BUTTON_ID = "smartwindow-group-tabs-button-inner";
const PANEL_ID = "smartwindow-group-tabs-panel";
const FLYOUT_ID = "smartwindow-group-tabs-flyout";
const CARD_TAG = "smartwindow-group-tabs-card";
const FLYOUT_HIDE_DELAY_MS = 160;

/**
 * Total length of the given tabs' titles. The titles are the only text the
 * on-device model is given, and the telemetry events report how much of it
 * there was rather than the text itself.
 *
 * @param {MozTabbrowserTab[]} tabs
 * @returns {number}
 */
function titleLength(tabs) {
  return tabs.reduce((total, tab) => total + (tab.label?.length ?? 0), 0);
}

/**
 * Front-end orchestrator for the Smart Window "Organize Tabs" feature. Opens a
 * toolbar-anchored panel that suggests tab groups (clustered on-device by
 * AutoTabGroupingSuggestions) and creates the ones the user picks.
 *
 * The whole feature is gated behind browser.smartwindow.autoTabGrouping.enabled
 * (default false). This module owns the XUL panels, their lifecycle, and all ML
 * and tab-group work; the panel and flyout markup are rendered by the
 * smartwindow-group-tabs-card / -flyout custom elements, which report user
 * intent back through events.
 */
export const AutoTabGrouping = {
  _nextId: 1,

  /**
   * A suggested group the panel offers (not yet created).
   *
   * @typedef {object} GroupSuggestion
   * @property {number} id - Stable id used to find the row across re-renders.
   * @property {string} label - Suggested group name.
   * @property {string} color - Tab-group color name (a --tab-group-<name>).
   * @property {MozTabbrowserTab[]} tabs - Tabs the group would contain.
   * @property {object[]} tabInfos - Per-tab display data (tile color, letter,
   *   site name, title) for the rows and flyout.
   */

  /**
   * A group the feature created and can still ungroup.
   *
   * @typedef {object} RecentGroup
   * @property {number} id - Stable id used to find the row across re-renders.
   * @property {number} suggestionId - Id of the suggestion this group was
   *   created from, so the telemetry events about it all report the same
   *   grouped_id from suggestion through to ungrouping.
   * @property {MozTabGroup} group - The created tab group, used to ungroup it.
   * @property {string} label - The created group's name.
   * @property {string} color - The created group's color name.
   */

  /**
   * Per-window state that must survive the panel being closed and rebuilt.
   * suggestions holds the not-yet-created proposals; consumed proposals are
   * dropped and are not recomputed, so a suggestion the user acts on does not
   * resurface.
   *
   * computePromise memoizes the in-flight clustering run (it resolves to
   * undefined once suggestions are stored) so a panel reopened while it is
   * still running awaits the same computation instead of getting stuck on the
   * loading state.
   *
   * recent holds the groups created from suggestions (newest first); the panel
   * lists them under "Just created" and the "Ungroup" button reverses all of
   * them at once. It is emptied when the panel closes: a group is only "just
   * created" while the panel that created it stays open.
   *
   * @type {WeakMap<ChromeWindow, {computed: boolean, computing: boolean,
   *   computePromise: ?Promise<void>, suggestions: GroupSuggestion[],
   *   recent: RecentGroup[]}>}
   */
  _state: new WeakMap(),

  /**
   * The main panel currently in the DOM for a window, if any.
   *
   * @type {WeakMap<ChromeWindow, XULElement>}
   */
  _panels: new WeakMap(),

  _getState(win) {
    let state = this._state.get(win);
    if (!state) {
      state = {
        computed: false,
        computing: false,
        computePromise: null,
        suggestions: [],
        recent: [],
      };
      this._state.set(win, state);
    }
    return state;
  },

  /**
   * Toggle the "Organize Tabs" panel from the toolbar button.
   *
   * @param {ChromeWindow} win - The Smart Window.
   * @param {object} [options]
   * @param {string} [options.source] - What asked for the panel, recorded on
   *   the menu_opened event: "button", "callout_click", or "message".
   */
  toggleGroupTabsPanel(win, { source = "button" } = {}) {
    const existing = this._panels.get(win);
    if (existing) {
      existing.hidePopup();
      return;
    }
    this.showGroupTabsPanel(win, { source }).catch(e =>
      lazy.console.warn("showGroupTabsPanel failed", e)
    );
  },

  /**
   * Build the panel, show it, then populate it once the clustering models have
   * run (or immediately if suggestions were already computed).
   *
   * @param {ChromeWindow} win
   * @param {object} [options]
   * @param {string} [options.source] - See toggleGroupTabsPanel.
   */
  async showGroupTabsPanel(win, { source = "button" } = {}) {
    const openedAt = Date.now();
    if (!win?.gBrowser || win.closed) {
      return;
    }
    const doc = win.document;
    const anchor = doc.getElementById(BUTTON_ITEM_ID);
    const button = doc.getElementById(BUTTON_ID);
    const popupSet = doc.getElementById("mainPopupSet");
    if (!anchor || !popupSet) {
      return;
    }

    Glean.smartWindow.autoTabGroupMenuOpened.record({
      source,
      tabs: win.gBrowser.tabs.length,
    });

    const panel = this._buildPanelSkeleton(win);
    popupSet.appendChild(panel);
    this._panels.set(win, panel);

    // Both panels are noautohide so hovering the flyout (a second popup) cannot
    // roll up the main panel; dismiss it ourselves on Escape or a click outside
    // both popups and the toolbar button.
    const onMouseDown = event => {
      const target = event.target;
      if (
        panel.contains(target) ||
        panel._flyoutPanel?.contains(target) ||
        anchor.contains(target)
      ) {
        return;
      }
      panel.hidePopup();
    };
    const onKeyDown = event => {
      if (event.key !== "Escape") {
        return;
      }
      event.preventDefault();
      const flyoutState = panel._flyoutPanel?.state;
      if (flyoutState && flyoutState !== "closed") {
        this._leaveFlyout(panel);
        return;
      }
      panel._restoreFocus = true;
      panel.hidePopup();
    };

    panel.addEventListener(
      "popupshown",
      () => {
        button?.setAttribute("aria-expanded", "true");
        panel._card.focus();
        win.addEventListener("mousedown", onMouseDown, true);
        win.addEventListener("keydown", onKeyDown, true);
      },
      { once: true }
    );
    panel.addEventListener(
      "popuphidden",
      () => {
        button?.setAttribute("aria-expanded", "false");
        win.removeEventListener("mousedown", onMouseDown, true);
        win.removeEventListener("keydown", onKeyDown, true);
        this._cancelHideFlyout(panel);
        panel._flyoutPanel?.remove();
        this._getState(win).recent = [];
        if (this._panels.get(win) === panel) {
          this._panels.delete(win);
        }
        panel.remove();
        if (panel._restoreFocus) {
          button?.focus();
        }
      },
      { once: true }
    );

    panel.openPopup(anchor, "after_end", 0, 6, false, false);

    const state = this._getState(win);
    if (!state.computing) {
      state.computed = false;
      state.suggestions = [];
    }
    const done = this._computeSuggestions(win);
    this._syncCard(win, panel);
    await done;
    if (this._panels.get(win) !== panel) {
      return;
    }
    this._syncCard(win, panel);

    await panel._card.updateComplete;
    if (this._panels.get(win) !== panel) {
      return;
    }
    Glean.smartWindow.autoTabGroupWindowDisplay.record({
      suggested_groups: state.suggestions.length,
      groups: state.recent.length,
      time: Date.now() - openedAt,
    });
  },

  /**
   * @param {ChromeWindow} win
   * @returns {XULElement} A detached main panel hosting the card element, with
   *   its events wired to the model actions.
   */
  _buildPanelSkeleton(win) {
    const doc = win.document;

    const panel = this._createPanel(win, PANEL_ID);

    const card = doc.createElement(CARD_TAG);
    card.addEventListener("create-all", () =>
      this._createSuggestions(
        win,
        panel,
        this._getState(win).suggestions.slice(),
        { source: "collective_accept" }
      )
    );
    card.addEventListener("create-one", e =>
      this._createById(win, panel, e.detail.id)
    );
    card.addEventListener("ungroup", () => this._ungroupRecent(win, panel));
    card.addEventListener("close-duplicates", () =>
      this._closeDuplicateTabs(win, panel)
    );
    card.addEventListener("view-tab-groups", e =>
      this._showGroupsFlyout(win, panel, e.detail.anchor)
    );
    card.addEventListener("preview", e => {
      // Focusing a row whose flyout was just dismissed must not reopen it;
      // pointing at it again is a fresh request and does.
      if (
        e.detail.source === "focus" &&
        panel._dismissedRow === e.detail.anchor
      ) {
        return;
      }
      if (e.detail.source === "hover" && this._flyoutHasFocus(panel)) {
        return;
      }
      panel._dismissedRow = null;
      this._showFlyoutById(win, panel, e.detail.id, e.detail.anchor);
    });
    card.addEventListener("preview-end", () => this._scheduleHideFlyout(panel));
    card.addEventListener("mouseover", e =>
      this._dismissPreviewOnRow(panel, e)
    );
    card.addEventListener("preview-enter", e => {
      panel._dismissedRow = null;
      this._showFlyoutById(win, panel, e.detail.id, e.detail.anchor);
      this._focusFlyout(panel);
    });
    panel.appendChild(card);

    panel._card = card;
    panel._flyoutPanel = null;
    panel._activeRow = null;
    panel._hideTimer = 0;
    panel._dismissedRow = null;
    panel._focusFlyoutController = null;
    panel._restoreFocus = false;
    return panel;
  },

  /**
   * Create a non-auto-hiding panel that hosts one of our cards. Shared by the
   * main panel and the hover flyout. `type="arrow"` is what makes popup.css
   * paint the panel from the --panel-* design tokens; the rest of the styling
   * lives in the smartwindowGroupTabs.css theme sheet, scoped to the panel ids
   * and swgt- classes.
   *
   * @param {ChromeWindow} win
   * @param {string} id
   * @returns {XULElement}
   */
  _createPanel(win, id) {
    const panel = win.document.createXULElement("panel");
    panel.id = id;
    panel.setAttribute("type", "arrow");
    panel.setAttribute("orient", "vertical");
    panel.setAttribute("noautofocus", "true");
    panel.setAttribute("noautohide", "true");
    panel.setAttribute("ignorekeys", "true");
    return panel;
  },

  /**
   * Lazily create the flyout sub-panel that floats past the end side of a
   * hovered suggestion row.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel - The main panel.
   * @returns {XULElement}
   */
  _ensureFlyoutPanel(win, panel) {
    if (panel._flyoutPanel?.parentNode) {
      return panel._flyoutPanel;
    }
    const flyoutPanel = this._createPanel(win, FLYOUT_ID);
    flyoutPanel.setAttribute("animate", "false");
    // Slide along the block axis to stay on screen near the bottom edge, and
    // keep flipping to the panel's other side when there is no room beside it.
    flyoutPanel.setAttribute("flip", "slide");
    const flyoutEl = win.document.createElement(FLYOUT_ID);
    flyoutEl.addEventListener("select-tab", e =>
      this._selectTab(win, panel, e.detail.id, e.detail.index)
    );
    flyoutEl.addEventListener("close-flyout", () => this._leaveFlyout(panel));
    flyoutEl.addEventListener("close-panel", () => panel.hidePopup());
    flyoutPanel.appendChild(flyoutEl);
    flyoutPanel.addEventListener("mouseenter", () =>
      this._cancelHideFlyout(panel)
    );
    flyoutPanel.addEventListener("mouseleave", () =>
      this._scheduleHideFlyout(panel)
    );
    flyoutPanel.addEventListener("focusin", () =>
      this._cancelHideFlyout(panel)
    );
    flyoutPanel.addEventListener("focusout", () =>
      this._scheduleHideFlyout(panel)
    );
    flyoutPanel._flyoutEl = flyoutEl;

    win.document.getElementById("mainPopupSet").appendChild(flyoutPanel);
    panel._flyoutPanel = flyoutPanel;
    return flyoutPanel;
  },

  /**
   * Push the window's state onto the card element, which re-renders. Called
   * after clustering finishes and whenever a suggestion or recent group is
   * consumed. New arrays are passed so the element sees a change.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   */
  _syncCard(win, panel) {
    const state = this._getState(win);
    this._pruneRecent(win);
    const hidden = this._hideFlyout(panel);

    const card = panel._card;
    card.computing = state.computing;
    card.suggestions = [...state.suggestions];
    card.recent = [...state.recent];
    card.duplicates = win.gBrowser.getAllDuplicateTabsToClose().length;
    card.tabGroups = this._tabGroupCount(win);
    return hidden;
  },

  /**
   * @param {ChromeWindow} win
   * @returns {number}
   */
  _tabGroupCount(win) {
    const saved = lazy.PrivateBrowsingUtils.isWindowPrivate(win)
      ? 0
      : win.SessionStore.savedGroups.length;
    return win.gBrowser.getAllTabGroups().length + saved;
  },

  _createById(win, panel, id) {
    const suggestion = this._getState(win).suggestions.find(s => s.id === id);
    if (suggestion) {
      this._createSuggestions(win, panel, [suggestion], {
        source: "individual_accept",
      });
    }
  },

  _showFlyoutById(win, panel, id, anchorRow) {
    const suggestion = this._getState(win).suggestions.find(s => s.id === id);
    if (suggestion) {
      this._showFlyout(win, panel, anchorRow, suggestion);
    }
  },

  /**
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   * @param {Element} anchorRow
   */
  _showGroupsFlyout(win, panel, anchorRow) {
    this._showFlyout(win, panel, anchorRow, null);
    this._focusFlyout(panel);
  },

  /**
   * Show the flyout beside a row, listing either the given suggestion's tabs or,
   * with no suggestion, the user's tab groups.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   * @param {Element} anchorRow
   * @param {?GroupSuggestion} suggestion
   */
  _showFlyout(win, panel, anchorRow, suggestion) {
    this._cancelHideFlyout(panel);
    const flyoutPanel = this._ensureFlyoutPanel(win, panel);
    flyoutPanel._flyoutEl.suggestion = suggestion;
    flyoutPanel._flyoutEl.groupsListId = suggestion ? 0 : this._nextId++;

    if (panel._activeRow && panel._activeRow !== anchorRow) {
      panel._activeRow.classList.remove("is-active");
      panel._activeRow.setAttribute("aria-expanded", "false");
    }
    anchorRow.classList.add("is-active");
    anchorRow.setAttribute("aria-expanded", "true");
    panel._activeRow = anchorRow;

    // Float the flyout past the end of the hovered row, top-aligned with it.
    // moveToAnchor repositions without a hide/show flicker when the pointer
    // slides between rows; the panel is an arrow panel, so it keeps following
    // its anchor instead of freezing to the screen position of the first row.
    const flyoutState = flyoutPanel.state;
    if (flyoutState === "open" || flyoutState === "showing") {
      flyoutPanel.moveToAnchor(anchorRow, "end_before", 0, 0);
    } else {
      flyoutPanel.openPopup(anchorRow, "end_before", 0, 0, false, false);
    }
  },

  /**
   * Hide the flyout, resolving once the popup has finished hiding: it hands
   * focus back as it goes, so anything setting focus afterwards must wait.
   *
   * @param {XULElement} panel
   * @returns {Promise<void>}
   */
  _hideFlyout(panel) {
    this._cancelHideFlyout(panel);
    const flyoutPanel = panel._flyoutPanel;
    const hidden =
      flyoutPanel && flyoutPanel.state !== "closed"
        ? new Promise(resolve =>
            flyoutPanel.addEventListener("popuphidden", resolve, { once: true })
          )
        : Promise.resolve();
    panel._focusFlyoutController?.abort();
    flyoutPanel?.hidePopup();
    if (panel._activeRow) {
      panel._activeRow.classList.remove("is-active");
      panel._activeRow.setAttribute("aria-expanded", "false");
      panel._activeRow = null;
    }
    return hidden;
  },

  async _focusFlyout(panel) {
    const flyoutPanel = panel._flyoutPanel;
    if (!flyoutPanel) {
      return;
    }
    // Focus is refused while the popup is still opening, and the request goes
    // stale if the flyout hides before it opens.
    const focusFirstTab = () => flyoutPanel._flyoutEl.focusFirstRow();
    if (flyoutPanel.state === "open") {
      await flyoutPanel._flyoutEl.updateComplete;
      if (flyoutPanel.state === "open") {
        focusFirstTab();
      }
      return;
    }
    panel._focusFlyoutController?.abort();
    panel._focusFlyoutController = new AbortController();
    flyoutPanel.addEventListener("popupshown", focusFirstTab, {
      once: true,
      signal: panel._focusFlyoutController.signal,
    });
  },

  /**
   * Back out of the flyout, closing it and returning focus to its row.
   *
   * @param {XULElement} panel
   */
  _leaveFlyout(panel) {
    const row = panel._activeRow;
    // Hiding a popup that holds focus hands it back to the row, whose own
    // preview would reopen the flyout we are closing.
    panel._dismissedRow = row;
    this._hideFlyout(panel);
    row?.focus();
  },

  /**
   * Switch to one of the tabs listed in the flyout. The suggestion is left
   * alone: previewing a group's tabs is not the same as creating it.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   * @param {number} id - Suggestion id.
   * @param {number} index - Position of the tab within the suggestion.
   */
  _selectTab(win, panel, id, index) {
    const suggestion = this._getState(win).suggestions.find(s => s.id === id);
    const tab = suggestion?.tabs[index];
    if (!tab || tab.closing || !win.gBrowser.tabs.includes(tab)) {
      return;
    }
    win.gBrowser.selectedTab = tab;
    panel.hidePopup();
  },

  _flyoutHasFocus(panel) {
    const active = panel.ownerDocument.activeElement;
    return !!active && !!panel._flyoutPanel?.contains(active);
  },

  _dismissPreviewOnRow(panel, event) {
    if (!panel._activeRow || this._flyoutHasFocus(panel)) {
      return;
    }
    // .swgt-row is every row the card makes actionable ("Create Groups", the
    // suggestions, "Ungroup Tabs", "Close Duplicate Tabs"); .swgt-recent-row is
    // a "Just created" entry, which is only there to be read. Another
    // suggestion is left out because it repositions the flyout instead.
    const row = event.target.closest(".swgt-row, .swgt-recent-row");
    if (
      row &&
      row !== panel._activeRow &&
      !row.classList.contains("swgt-suggestion")
    ) {
      this._hideFlyout(panel);
    }
  },

  _scheduleHideFlyout(panel) {
    this._cancelHideFlyout(panel);
    if (this._flyoutHasFocus(panel)) {
      return;
    }
    panel._hideTimer = lazy.setTimeout(() => {
      panel._hideTimer = 0;
      this._hideFlyout(panel);
    }, FLYOUT_HIDE_DELAY_MS);
  },

  _cancelHideFlyout(panel) {
    if (panel._hideTimer) {
      lazy.clearTimeout(panel._hideTimer);
      panel._hideTimer = 0;
    }
  },

  /**
   * Create the given suggestions as tab groups and drop them from the
   * suggestion list. The panel stays open so the user can create the remaining
   * groups one at a time.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   * @param {object[]} suggestions
   * @param {object} options
   * @param {string} options.source - Whether the user picked one group
   *   ("individual_accept") or created them all at once ("collective_accept").
   */
  _createSuggestions(win, panel, suggestions, { source }) {
    const state = this._getState(win);
    const windowTabs = new Set(win.gBrowser.tabs);
    // Our groups arrive fully formed (label and color chosen by the
    // suggestion), so create them non-user-triggered: a user-triggered
    // addTabGroup dispatches TabGroupCreateByUser, which pops the built-in
    // "name your group" editor. We track creation with our own Glean event.
    // (bug 2024819 tracks decoupling that editor trigger from the standard
    // tab-group metrics; if it lands we may need to revisit this call.)
    const metricsContext = {
      isUserTriggered: false,
      telemetrySource:
        lazy.TabMetrics.METRIC_SOURCE.SMART_WINDOW_GROUP_SUGGESTIONS,
    };
    for (const suggestion of suggestions) {
      const tabs = this._creatableTabs(windowTabs, suggestion);
      if (tabs.length < lazy.minTabsPerGroup) {
        continue;
      }
      Glean.smartWindow.autoTabGroupAccepted.record({
        grouped_tabs: tabs.length,
        grouped_id: suggestion.id,
        source,
      });
      let group;
      let errorType = "";
      try {
        group = win.gBrowser.addTabGroup(tabs, {
          label: suggestion.label,
          color: suggestion.color,
          metricsContext,
        });
      } catch (e) {
        lazy.console.warn("addTabGroup failed", e);
        errorType = e.name;
      }
      if (group) {
        state.recent.unshift({
          id: this._nextId++,
          suggestionId: suggestion.id,
          group,
          label: suggestion.label,
          color: suggestion.color,
        });
      }
      Glean.smartWindow.autoTabGroupCompleted.record({
        grouped_tabs: group ? tabs.length : 0,
        grouped_id: suggestion.id,
        source,
        success: !!group,
        error_type: errorType,
      });
    }

    const consumed = new Set(suggestions.map(s => s.id));
    const focusIndex = state.suggestions.findIndex(s => consumed.has(s.id));
    state.suggestions = state.suggestions.filter(s => !consumed.has(s.id));
    this._pruneSuggestions(win);
    const hidden = this._syncCard(win, panel);
    this._focusAfterRowRemoved(panel, hidden, focusIndex);
  },

  /**
   * Tabs of a suggestion that can still be grouped: creating one group can
   * leave a later suggestion short of tabs, so this is re-checked every time.
   *
   * @param {Set<MozTabbrowserTab>} windowTabs
   * @param {object} suggestion
   * @returns {MozTabbrowserTab[]}
   */
  _creatableTabs(windowTabs, suggestion) {
    return suggestion.tabs.filter(
      t => !t.closing && !t.group && windowTabs.has(t)
    );
  },

  /**
   * Drop suggestions that can no longer be created, so the panel never offers
   * a row that would do nothing.
   *
   * @param {ChromeWindow} win
   */
  _pruneSuggestions(win) {
    const state = this._getState(win);
    const windowTabs = new Set(win.gBrowser.tabs);
    state.suggestions = state.suggestions.filter(
      s => this._creatableTabs(windowTabs, s).length >= lazy.minTabsPerGroup
    );
  },

  /**
   * Move focus off a row the re-render is about to destroy, so the keyboard
   * can keep working: onto the suggestion that took its place, or the card
   * itself when there is none.
   *
   * @param {XULElement} panel
   * @param {Promise<void>} hidden - Resolves once any flyout has finished
   *   hiding, which hands focus back and would otherwise undo this.
   * @param {number} [index] - Position the removed suggestion held.
   */
  async _focusAfterRowRemoved(panel, hidden, index = -1) {
    const card = panel._card;
    await Promise.all([card.updateComplete, hidden]);
    if (!panel.parentNode) {
      return;
    }
    const rows = card.querySelectorAll(".swgt-suggestion");
    const row = rows[Math.min(index, rows.length - 1)];
    const target = row?.isConnected ? row : card;
    panel._dismissedRow = target;
    target.focus();
  },

  _metricsContext() {
    return lazy.TabMetrics.userTriggeredContext(
      lazy.TabMetrics.METRIC_SOURCE.SMART_WINDOW_GROUP_SUGGESTIONS
    );
  },

  _isGroupLive(win, group) {
    return win.gBrowser.tabGroups.includes(group);
  },

  /**
   * Ungroup every recent group at once: dissolve each group (its tabs stay
   * open) and clear the "Just created" list. Reverses everything the feature
   * created since the panel was last cleared.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   */
  _ungroupRecent(win, panel) {
    const state = this._getState(win);
    const entries = state.recent.slice();
    for (const entry of entries) {
      // A group that is already gone (closed, or ungrouped from the tab strip)
      // is nothing to undo, so it is left out of the events entirely.
      if (!this._isGroupLive(win, entry.group)) {
        continue;
      }
      const groupedTabs = entry.group.tabs.length;
      const source = "collective_ungroup";
      Glean.smartWindow.autoTabUngroupRequested.record({
        grouped_tabs: groupedTabs,
        grouped_id: entry.suggestionId,
        source,
      });
      let errorType = "";
      try {
        entry.group.ungroupTabs(this._metricsContext());
      } catch (e) {
        lazy.console.warn("ungroupTabs failed", e);
        errorType = e.name;
      }
      Glean.smartWindow.autoTabUngroupCompleted.record({
        grouped_tabs: groupedTabs,
        grouped_id: entry.suggestionId,
        source,
        success: !errorType,
        error_type: errorType,
      });
    }
    this._forgetEntries(win, entries);
    this._focusAfterRowRemoved(panel, this._syncCard(win, panel));
  },

  /**
   * Close this window's duplicate tabs, reusing the same tabbrowser action the
   * All Tabs menu offers. The panel is dismissed first: closing tabs raises a
   * confirmation hint anchored to the ATG button, which an open panel
   * would cover, and the warning prompt is modal.
   *
   * @param {ChromeWindow} win
   * @param {XULElement} panel
   */
  _closeDuplicateTabs(win, panel) {
    panel._restoreFocus = true;
    panel.hidePopup();

    const tabs = win.gBrowser.tabs.length;
    Glean.smartWindow.closeDuplicateTabsRequested.record({ tabs });

    const startedAt = Date.now();
    Glean.smartWindow.closeDuplicateTabsStarted.record({
      tabs,
      total_length: titleLength(win.gBrowser.tabs),
    });
    let duplicates = [];
    let errorType = "";
    try {
      duplicates = win.gBrowser.getAllDuplicateTabsToClose();
    } catch (e) {
      lazy.console.warn("getAllDuplicateTabsToClose failed", e);
      errorType = e.name;
    }
    Glean.smartWindow.closeDuplicateTabsCompleted.record({
      tabs,
      duplicate_tabs: duplicates.length,
      success: !errorType,
      error_type: errorType,
      time: Date.now() - startedAt,
    });
    if (errorType) {
      return;
    }

    try {
      win.gBrowser.removeAllDuplicateTabs({
        confirmationAnchor: win.document.getElementById(BUTTON_ID),
      });
    } catch (e) {
      lazy.console.warn("removeAllDuplicateTabs failed", e);
      errorType = e.name;
    }
    const closed = duplicates.filter(
      tab => tab.closing || !win.gBrowser.tabs.includes(tab)
    );
    // Closing enough tabs raises a modal warning the user can back out of,
    // which leaves nothing closed and nothing to record.
    if (!errorType && !closed.length) {
      return;
    }
    Glean.smartWindow.duplicateTabsClosed.record({
      duplicate_tabs: closed.length,
      success: !errorType,
      error_type: errorType,
    });
  },

  _forgetEntries(win, entries) {
    const state = this._getState(win);
    const ids = new Set(entries.map(e => e.id));
    state.recent = state.recent.filter(e => !ids.has(e.id));
  },

  _pruneRecent(win) {
    const state = this._getState(win);
    const dead = state.recent.filter(e => !this._isGroupLive(win, e.group));
    if (dead.length) {
      this._forgetEntries(win, dead);
    }
  },

  _withTimeout(promise, ms) {
    let timer;
    const timeout = new Promise((_, reject) => {
      timer = lazy.setTimeout(
        () =>
          reject(
            new DOMException("Auto Tab Grouping timed out", "TimeoutError")
          ),
        ms
      );
    });
    return Promise.race([promise, timeout]).finally(() =>
      lazy.clearTimeout(timer)
    );
  },

  /**
   * Run clustering + labeling once and cache the resulting suggestions on the
   * window state. Stays uncomputed when there are too few tabs so a later open
   * retries once more tabs exist.
   *
   * @param {ChromeWindow} win
   */
  _computeSuggestions(win) {
    const state = this._getState(win);
    if (state.computed) {
      return Promise.resolve();
    }
    if (state.computePromise) {
      return state.computePromise;
    }
    const tabs = win.gBrowser.tabs.length;
    Glean.smartWindow.autoTabGroupingRequested.record({ tabs });
    if (!lazy.AutoTabGroupingSuggestions.isAvailable) {
      return Promise.resolve();
    }
    const candidates = lazy.AutoTabGroupingSuggestions.getCandidateTabs(win);
    if (candidates.length < lazy.minCandidateTabs) {
      return Promise.resolve();
    }
    state.computing = true;
    state.computePromise = (async () => {
      const startedAt = Date.now();
      Glean.smartWindow.autoTabGroupingStarted.record({
        tabs,
        total_length: titleLength(candidates),
      });
      let suggestions = [];
      let errorType = "";
      try {
        const proposals = await this._withTimeout(
          lazy.AutoTabGroupingSuggestions.buildProposals(candidates),
          lazy.timeoutMs
        );
        suggestions = proposals.map((proposal, index) => ({
          id: this._nextId++,
          ...lazy.AutoTabGroupingSuggestions.toSuggestionData(proposal, index),
        }));
        state.suggestions = suggestions;
        state.computed = true;
      } catch (e) {
        lazy.console.warn("Building group proposals failed", e);
        errorType = e.name;
      } finally {
        state.computing = false;
        state.computePromise = null;
      }

      const groupedTabs = suggestions.flatMap(s => s.tabs);
      Glean.smartWindow.autoTabGroupingCompleted.record({
        tabs,
        groups: suggestions.length,
        grouped_tabs: groupedTabs.length,
        success: !errorType,
        error_type: errorType,
        total_length: titleLength(groupedTabs),
        time: Date.now() - startedAt,
      });
      for (const suggestion of suggestions) {
        Glean.smartWindow.autoTabGroupSuggested.record({
          grouped_tabs: suggestion.tabs.length,
          total_length: titleLength(suggestion.tabs),
          grouped_id: suggestion.id,
        });
      }
    })();
    return state.computePromise;
  },
};
