/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A common list of systems, surfaces, controls, etc. from which user
 * interactions with tabs could originate. These "source" values
 * should be sent as extra data with tab-related metrics events.
 */
const METRIC_SOURCE = Object.freeze({
  // Tab overflow menu/"list all tabs" menu
  TAB_OVERFLOW_MENU: "tab_overflow",
  // Tab group context menu (right-clicking on a tab group label)
  TAB_GROUP_MENU: "tab_group",
  CANCEL_TAB_GROUP_CREATION: "cancel_create",
  // Tab context menu (right-clicking on a tab)
  TAB_MENU: "tab_menu",
  TAB_STRIP: "tab_strip",
  DRAG_AND_DROP: "drag",
  // Keyboard shortcut (Ctrl+W, Ctrl+F4, Ctrl+Shift+PageUp/Down, etc.)
  KEYBOARD: "keyboard",
  // Middle-clicking on a tab
  MIDDLE_CLICK: "middle_click",
  // Mouse wheel scrolling on the tab strip
  MOUSE_WHEEL: "mouse_wheel",
  // A trackpad or touch screen gesture
  GESTURE: "gesture",
  // "Search & Suggest," i.e. URL bar suggested actions while typing
  SUGGEST: "suggest",
  // History > Recently Closed Tabs menu, undo recently closed tab, etc.
  RECENT_TABS: "recent",
  // A messaging action.
  MESSAGING: "messaging",
  // The ctrl-tab UI.
  CTRL_TAB: "ctrl_tab",
  // The Smart Window "Group my tabs" suggestion panel.
  SMART_WINDOW_GROUP_SUGGESTIONS: "smartwindow_group_suggestions",
  // The Smart Window "Tasks" panel.
  SMART_WINDOW_TASKS: "smartwindow_tasks",
  APP_MENU_GROUP_LIST: "appmenu_group_list",
  TOOLBAR_GROUP_LIST: "toolbar_group_list",
  UNKNOWN: "unknown",
});

/**
 * The action for telemetry. When adding to this list you MUST update the labels
 * for the tab.action and tab.tab_count counters in `metrics.yaml`.
 */
const METRIC_ACTION = Object.freeze({
  // A tab became the displayed tab.
  ACTIVATE: "activate",
  // Tabs were moved to a previously existing window.
  ADOPT: "adopt",
  // Tabs were detached to a new window.
  DETACH: "detach",
  // Tabs were closed.
  CLOSE: "close",
  // Tabs were moved within the same window.
  MOVE: "move",
  // Tabs were pinned.
  PIN: "pin",
  // Tabs were unpinned.
  UNPIN: "unpin",
});

const METRIC_TABS_LAYOUT = Object.freeze({
  HORIZONTAL: "horizontal",
  VERTICAL: "vertical",
});

const METRIC_REOPEN_TYPE = Object.freeze({
  SAVED: "saved",
  DELETED: "deleted",
});

const METRIC_GROUP_TYPE = Object.freeze({
  EXPANDED: "expanded",
  COLLAPSED: "collapsed",
  SAVED: "saved",
});

/**
 * @typedef {object} TabMetricsContext
 * @property {boolean} [isUserTriggered=false]
 *   Should be true if there was an explicit action/request from the user
 *   (as opposed to some action being taken internally or for technical
 *   bookkeeping reasons alone). This causes telemetry events to fire.
 * @property {string} [telemetrySource="unknown"]
 *   The system, surface, or control the user used to take this action, one of
 *   the TabMetrics.METRIC_SOURCE values. Defaults to "unknown".
 * @property {boolean} [isDecomposed=false]
 *   Some combination operations are handled internally individually, moving
 *   five tabs is split into five moves for instance. This flag is set to true
 *   if this is one of the individual events
 */

/**
 * An unknown context.
 *
 * @type {TabMetricsContext}
 */
const UNKNOWN_CONTEXT = Object.freeze({
  isUserTriggered: false,
  telemetrySource: METRIC_SOURCE.UNKNOWN,
});

/**
 * Creates a `TabMetricsContext` object for a user event originating from
 * the specified source.
 *
 * @param {string} telemetrySource
 *   @see TabMetrics.METRIC_SOURCE
 * @returns {TabMetricsContext}
 */
function userTriggeredContext(telemetrySource) {
  telemetrySource = telemetrySource || METRIC_SOURCE.UNKNOWN;
  return {
    isUserTriggered: true,
    telemetrySource,
  };
}

/**
 * Creates a copy of the context for decomposed events.
 *
 * @param {TabMetricsContext} metricsContext
 * @returns {TabMetricsContext}
 */
function decomposedContext(metricsContext) {
  return {
    ...metricsContext,
    isDecomposed: true,
  };
}

function sourceForEvent(event) {
  if (!event) {
    return METRIC_SOURCE.UNKNOWN;
  }

  if (event.type == "DOMMouseScroll") {
    return METRIC_SOURCE.MOUSE_WHEEL;
  }

  if (SimpleGestureEvent.isInstance(event)) {
    return METRIC_SOURCE.GESTURE;
  }

  if (KeyboardEvent.isInstance(event)) {
    return METRIC_SOURCE.KEYBOARD;
  }

  return METRIC_SOURCE.UNKNOWN;
}

export const TabMetrics = {
  METRIC_SOURCE,
  METRIC_ACTION,
  METRIC_TABS_LAYOUT,
  METRIC_REOPEN_TYPE,
  METRIC_GROUP_TYPE,
  UNKNOWN_CONTEXT,
  sourceForEvent,
  userTriggeredContext,
  decomposedContext,
};
