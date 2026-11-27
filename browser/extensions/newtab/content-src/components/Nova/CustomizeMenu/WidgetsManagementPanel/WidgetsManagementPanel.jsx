/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// @nova-cleanup(move-directory): Move to components/CustomizeMenu/WidgetsManagementPanel/ after Nova ships

import React, { useRef } from "react";
import { useDispatch, useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  getWidgets,
  isWeatherAvailable,
  isWidgetToggleVisible,
  resolveWidgetSize,
} from "common/WidgetsRegistry.mjs";
// eslint-disable-next-line no-shadow
import { CSSTransition } from "react-transition-group";
import { useWidgetLabels } from "./useWidgetLabels.jsx";
import {
  isWidgetPlacementActive,
  resolvePinnedWidgetIds,
} from "common/PageLayoutVariants.mjs";
import { setWidgetOnHome } from "content-src/components/Widgets/WidgetPlacement";

function WidgetsManagementPanel({ togglePanel, showPanel, setPref }) {
  const prefs = useSelector(state => state.Prefs.values);
  const extensionWidgets = useSelector(state => state.ExtensionWidgets);
  const arrowButtonRef = useRef(null);
  const panelRef = useRef(null);
  const dispatch = useDispatch();

  const activeWidgets = getWidgets(WIDGET_REGISTRY, extensionWidgets).filter(
    w => !w.retired
  );
  // Weather also needs the legacy showWeather prefs the widget itself checks.
  const visibleWidgets = activeWidgets.filter(
    widget =>
      isWidgetToggleVisible(widget, prefs) &&
      (widget.id !== "weather" || isWeatherAvailable(prefs))
  );

  // No toggle renders until the labels resolve; an unsorted list would reorder
  // under the user once they did.
  const widgetLabels = useWidgetLabels(activeWidgets);
  const sortedWidgets = widgetLabels
    ? [...visibleWidgets].sort((a, b) =>
        widgetLabels.get(a.id).localeCompare(widgetLabels.get(b.id))
      )
    : [];

  const handlePanelEntered = () => {
    arrowButtonRef.current?.focus();
  };

  const onToggleWidget = (widget, e) => {
    // The outer Widgets toggle listens for toggle events too and would record
    // a second telemetry pair for every flip.
    e.stopPropagation();
    const value = e.target.pressed;

    dispatch(
      ac.UserEvent({
        event: "PREF_CHANGED",
        source: widget.customizeEventSource,
        value: { status: value, menu_source: "CUSTOMIZE_MENU" },
      })
    );

    dispatch(
      ac.OnlyToMain({
        type: at.WIDGETS_ENABLED,
        data: {
          widget_name: widget.telemetryName,
          widget_source: "customize_panel",
          enabled: value,
          widget_size: resolveWidgetSize(widget, prefs),
        },
      })
    );

    setPref(widget.enabledPref, value);
    // A widget turned off leaves Home, so turning it back on doesn't quietly
    // put it back there.
    if (
      !value &&
      isWidgetPlacementActive(prefs) &&
      resolvePinnedWidgetIds(prefs).includes(widget.id)
    ) {
      setWidgetOnHome(dispatch, prefs, widget.id, false);
    }
  };

  const isRTL = typeof document !== "undefined" && document.dir === "rtl";
  const arrowIconSrc = `chrome://global/skin/icons/shaft-arrow-${isRTL ? "right" : "left"}.svg`;

  return (
    <div id="widgets-management-panel" className="widgets-mgmt-panel-container">
      <moz-box-button
        onClick={togglePanel}
        data-l10n-id="newtab-widget-manage-widget-button"
      ></moz-box-button>
      <CSSTransition
        nodeRef={panelRef}
        in={showPanel}
        timeout={300}
        classNames="widgets-mgmt-panel"
        unmountOnExit={true}
        onEntered={handlePanelEntered}
      >
        <div ref={panelRef} className="widgets-mgmt-panel">
          <div className="panel-content">
            <div className="arrow-wrapper">
              <moz-button
                ref={arrowButtonRef}
                type="ghost"
                className="arrow-button"
                iconSrc={arrowIconSrc}
                data-l10n-id="newtab-customize-panel-back-button"
                onClick={togglePanel}
              ></moz-button>
              <h2 data-l10n-id="newtab-widget-manage-title"></h2>
            </div>
            <div className="settings-widgets">
              {sortedWidgets.map(widget => (
                <moz-toggle
                  key={widget.id}
                  id={`${widget.id}-toggle`}
                  pressed={prefs[widget.enabledPref] || null}
                  // The toggles render once their labels resolve, which can be
                  // after CustomizeMenu's locked-pref sweep when a link opens
                  // this subpanel on load, so each one applies the lock itself.
                  disabled={
                    prefs.lockedPrefs?.includes(widget.enabledPref) || null
                  }
                  data-lock-managed=""
                  ontoggle={e => onToggleWidget(widget, e)}
                  data-preference={widget.enabledPref}
                  // Widgets with a customizeL10nId are labeled by the .label
                  // attribute in that message, and extension widgets without
                  // one are labeled with their name.
                  label={widget.name}
                  data-l10n-id={widget.customizeL10nId}
                />
              ))}
            </div>
          </div>
        </div>
      </CSSTransition>
    </div>
  );
}

export { WidgetsManagementPanel };
