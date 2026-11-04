/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useCallback } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  resolveWidgetFrame,
  resolveWidgetSize,
} from "common/WidgetsRegistry.mjs";
import { SizeSubmenu } from "../SizeSubmenu";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { WidgetMenuPoweredBy } from "../WidgetMenuPoweredBy";
import { useWidgetTelemetry } from "../useWidgetTelemetry";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
  POWERED_BY_KAYAK: "powered_by_kayak",
};

const KAYAK_URL = "https://www.kayak.com/";

const TRAVEL_ENTRY = WIDGET_REGISTRY.find(w => w.id === "travel");

/**
 * Travel widget.
 *
 * @param {object} props
 * @param {Function} props.dispatch - Redux dispatch.
 * @param {Function} props.handleUserInteraction - Marks the widget as
 *   interacted with, which removes the "New" badge.
 * @param {boolean} props.widgetsMayBeMaximized - Whether the current layout
 *   allows resizing, which decides whether the Change size submenu shows.
 * @param {object} props.widgetEnabledMap - Map of widget id to whether it is
 *   currently active, used by the Move submenu.
 */
function Travel({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const framedWidgets = useSelector(state => state.FramedWidgets);
  const { src, state: frameState } = resolveWidgetFrame(
    TRAVEL_ENTRY,
    prefs,
    framedWidgets
  );
  const widgetSize = resolveWidgetSize(TRAVEL_ENTRY, prefs);
  const hasInteracted = prefs["widgets.travel.interaction"];

  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: TRAVEL_ENTRY,
    widgetSize,
  });

  const handleInteraction = useCallback(
    () => handleUserInteraction("travel"),
    [handleUserInteraction]
  );

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: TRAVEL_ENTRY.sizePref, value: size },
        })
      );
      // Without `size`, the event would report the size before the change.
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: "context_menu",
        value: size,
        size,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  function handleLearnMore() {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, {
      source: "context_menu",
    });
    handleInteraction();
  }

  if (frameState === "hidden") {
    return null;
  }

  return (
    <article
      className={`travel widget col-4 ${widgetSize}-widget`}
      ref={impressionRef}
      aria-labelledby="travel-widget-label"
    >
      <div className="travel-title-wrapper">
        <div className="travel-badge-title-wrapper">
          {!hasInteracted && (
            <moz-badge
              className="travel-new-badge"
              data-l10n-id="newtab-widget-lists-label-new"
            ></moz-badge>
          )}
          <h2
            id="travel-widget-label"
            className="newtab-widget-title"
            data-l10n-id="newtab-travel-widget-title"
          />
        </div>
        <div className="travel-context-menu-wrapper">
          <WidgetMenuButton
            className="travel-context-menu-button"
            menuId="travel-context-menu"
            l10nId="newtab-travel-widget-open-menu-button"
          />
          <panel-list className="panel-list-no-icons" id="travel-context-menu">
            <WidgetMenuPoweredBy
              dispatch={dispatch}
              url={KAYAK_URL}
              l10nId="newtab-travel-menu-powered-by-kayak"
              recordUserAction={recordUserAction}
              userAction={USER_ACTION_TYPES.POWERED_BY_KAYAK}
              onClick={handleInteraction}
            />
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId={TRAVEL_ENTRY.id}
              widgetEnabledMap={widgetEnabledMap}
              widgetName={TRAVEL_ENTRY.telemetryName}
              enabledPref={TRAVEL_ENTRY.enabledPref}
              widgetSize={widgetSize}
              learnMoreL10nId="newtab-travel-menu-learn-more"
              onLearnMore={handleLearnMore}
              sizeSubmenu={
                widgetsMayBeMaximized ? (
                  <SizeSubmenu
                    submenuId="travel-size-submenu"
                    sizes={TRAVEL_ENTRY.validSizes}
                    checkedSize={widgetSize}
                    onChangeSize={handleChangeSize}
                  />
                ) : null
              }
            />
          </panel-list>
        </div>
      </div>
      <div className="travel-body">
        <iframe
          className="travel-frame"
          aria-labelledby="travel-widget-label"
          src={src}
          // Same sandbox as the Crossword frame, including allow-same-origin,
          // which is under the same security review.
          sandbox="allow-scripts allow-same-origin allow-popups allow-popups-to-escape-sandbox"
        />
      </div>
    </article>
  );
}

export { Travel };
