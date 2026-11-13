/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { createContext, useContext } from "react";
import { useDispatch, useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  PREF_HOME_WIDGETS,
  PREF_WIDGETS_ORDER,
  WIDGET_REGISTRY,
  resolveWidgetOrder,
  resolveWidgetSize,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import {
  WIDGET_PLACEMENTS,
  resolveHomeWidgetIds,
} from "common/PageLayoutVariants.mjs";

// The WIDGET_PLACEMENTS role of the space a widget renders in, or null outside
// a layout where the user pins widgets to Home.
export const WidgetPlacementContext = createContext(null);

// Where on the page a widget renders, reported in widget telemetry.
export const WIDGET_LOCATIONS = {
  HOME_SPACE: "home_space",
  WIDGET_SPACE: "widget_space",
  WIDGET_CONTAINER: "widget_container",
  SIDEBAR: "sidebar",
  STORY_CARD: "story_card",
};

// A WIDGET_LOCATIONS value, set by each component that renders widgets.
export const WidgetLocationContext = createContext(null);

/**
 * Provides the placement and the location for the widgets section.
 *
 * @param {object} props
 * @param {string|null} [props.placement] - a WIDGET_PLACEMENTS value
 * @param {React.ReactNode} props.children
 */
export function WidgetPlacementProvider({ placement, children }) {
  let widgetLocation = WIDGET_LOCATIONS.WIDGET_CONTAINER;
  if (placement === WIDGET_PLACEMENTS.HOME) {
    widgetLocation = WIDGET_LOCATIONS.HOME_SPACE;
  } else if (placement === WIDGET_PLACEMENTS.LIBRARY) {
    widgetLocation = WIDGET_LOCATIONS.WIDGET_SPACE;
  }
  return (
    <WidgetPlacementContext.Provider value={placement ?? null}>
      <WidgetLocationContext.Provider value={widgetLocation}>
        {children}
      </WidgetLocationContext.Provider>
    </WidgetPlacementContext.Provider>
  );
}

export const HOME_USER_ACTIONS = {
  PIN: "add_to_home",
  UNPIN: "remove_from_home",
};

/**
 * Pins a widget to Home or moves it back to the widget library. A pinned
 * widget goes first, in the Home list and in the shared widget order, so it
 * leads the Home column.
 *
 * @param {Function} dispatch - Redux dispatch
 * @param {object} prefs - current pref values from the Redux store
 * @param {string} widgetId - a widget registry id
 * @param {boolean} onHome - true to pin, false to move back to the library
 * @param {string} [source] - telemetry widget_source for a user's own move;
 *   omitted when the move only follows from another action, which records
 *   itself
 */
export function setWidgetOnHome(dispatch, prefs, widgetId, onHome, source) {
  const others = resolveHomeWidgetIds(prefs).filter(id => id !== widgetId);
  const widget = WIDGET_REGISTRY.find(w => w.id === widgetId);
  dispatch(
    ac.SetPref(
      PREF_HOME_WIDGETS,
      JSON.stringify(onHome ? [widgetId, ...others] : others)
    )
  );
  if (onHome) {
    const order = resolveWidgetOrder(prefs).filter(id => id !== widgetId);
    dispatch(ac.SetPref(PREF_WIDGETS_ORDER, [widgetId, ...order].join(",")));
  }
  if (source && widget) {
    dispatch(
      ac.OnlyToMain({
        type: at.WIDGETS_USER_EVENT,
        data: {
          widget_name: widget.telemetryName,
          widget_source: source,
          widget_size: resolveWidgetSize(widget, prefs),
          user_action: onHome ? HOME_USER_ACTIONS.PIN : HOME_USER_ACTIONS.UNPIN,
        },
      })
    );
  }
}

/**
 * The widget menu's "Hide widget" item. On Home it moves the widget back to
 * the widget library; everywhere else it turns the widget off.
 *
 * @param {object} props
 * @param {string} props.widgetId - a widget registry id
 * @param {Function} props.onHide - turns the widget off
 */
export function WidgetHideMenuItem({ widgetId, onHide }) {
  const placement = useContext(WidgetPlacementContext);
  const prefs = useSelector(state => state.Prefs.values);
  const dispatch = useDispatch();
  const onClick =
    placement === WIDGET_PLACEMENTS.HOME
      ? () =>
          setWidgetOnHome(
            dispatch,
            prefs,
            widgetId,
            false,
            WIDGET_SOURCES.CONTEXT_MENU
          )
      : onHide;
  return (
    <panel-item data-l10n-id="newtab-widget-menu-hide" onClick={onClick} />
  );
}
