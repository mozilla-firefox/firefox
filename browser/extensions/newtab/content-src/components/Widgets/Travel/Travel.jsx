/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useCallback, useRef } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  FRAMED_WIDGET_SANDBOX,
  WIDGET_REGISTRY,
  resolveWidgetFrame,
  resolveWidgetSize,
} from "common/WidgetsRegistry.mjs";
import { SizeSubmenu } from "../SizeSubmenu";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { WidgetMenuPoweredBy } from "../WidgetMenuPoweredBy";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import {
  useFramedWidgetReady,
  useFramedWidgetSize,
} from "../useFramedWidgetMessages";

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

  const handleInteraction = useCallback(
    () => handleUserInteraction("travel"),
    [handleUserInteraction]
  );

  const iframeRef = useRef(null);
  // Explore travel asks for a larger size, which also counts as the user
  // interacting and clears the New badge. Loading messages don't count.
  const { requestedSize, clearRequestedSize } = useFramedWidgetSize({
    widget: TRAVEL_ENTRY,
    src,
    iframeRef,
    widgetSize,
    onAction(message) {
      if (message === "change_size") {
        handleInteraction();
      }
    },
  });
  // Like Crossword's growth, a layout that can't host a larger widget keeps
  // the widget at its own size.
  const displaySize =
    widgetsMayBeMaximized && requestedSize ? requestedSize : widgetSize;

  const { impressionRef, recordUserAction, recordError } = useWidgetTelemetry({
    dispatch,
    widget: TRAVEL_ENTRY,
    widgetSize: displaySize,
  });
  // The frame stays hidden until the widget's document sends widget_ready. A
  // load timeout or terminal widget_error hides the whole widget.
  const ready = useFramedWidgetReady({
    widget: TRAVEL_ENTRY,
    src,
    iframeRef,
    onError(reason, terminal) {
      recordError(reason);
      if (terminal) {
        dispatch({
          type: at.FRAMED_WIDGET_UNAVAILABLE,
          data: { id: TRAVEL_ENTRY.id },
        });
      }
    },
  });

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
      // The user's choice replaces any size the frame asked for.
      clearRequestedSize();
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction, clearRequestedSize]
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
      className={`travel widget col-4 ${displaySize}-widget`}
      data-ready={ready || undefined}
      ref={impressionRef}
      aria-labelledby="travel-widget-label"
    >
      <h2
        id="travel-widget-label"
        className="newtab-widget-title sr-only"
        data-l10n-id="newtab-travel-widget-title"
      />
      <div className="travel-overlay">
        {!hasInteracted && (
          <moz-badge
            className="travel-new-badge"
            data-l10n-id="newtab-widget-lists-label-new"
          ></moz-badge>
        )}
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
              widgetSize={displaySize}
              learnMoreL10nId="newtab-travel-menu-learn-more"
              onLearnMore={handleLearnMore}
              sizeSubmenu={
                widgetsMayBeMaximized ? (
                  <SizeSubmenu
                    submenuId="travel-size-submenu"
                    sizes={TRAVEL_ENTRY.validSizes}
                    checkedSize={displaySize}
                    onChangeSize={handleChangeSize}
                  />
                ) : null
              }
            />
          </panel-list>
        </div>
      </div>
      <iframe
        // A new document gets a new window, so late messages from the old
        // one fail the source check.
        key={src}
        ref={iframeRef}
        className="travel-frame"
        aria-labelledby="travel-widget-label"
        src={src}
        sandbox={FRAMED_WIDGET_SANDBOX}
      />
    </article>
  );
}

export { Travel };
