/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useCallback } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  FRAMED_WIDGET_SANDBOX,
  WIDGET_REGISTRY,
  resolveWidgetFrame,
  resolveWidgetSize,
} from "common/WidgetsRegistry.mjs";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { WidgetMenuPoweredBy } from "../WidgetMenuPoweredBy";
import { SizeSubmenu } from "../SizeSubmenu";
import { useWidgetTelemetry } from "../useWidgetTelemetry";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
  POWERED_BY_YAHOO: "powered_by_yahoo",
};

const YAHOO_URL = "https://www.yahoo.com/";

const HOROSCOPES_ENTRY = WIDGET_REGISTRY.find(w => w.id === "horoscopes");

/**
 * Horoscopes widget.
 *
 * @param {object} props
 * @param {Function} props.dispatch - Redux dispatch.
 * @param {Function} props.handleUserInteraction - Marks the widget as
 *   interacted with, which removes the "New" badge.
 * @param {boolean} props.widgetsMayBeMaximized - Whether the current layout
 *   allows resizing, which gates the Change size submenu.
 * @param {object} props.widgetEnabledMap - Map of widget id to whether it is
 *   currently active, used by the Move submenu.
 */
const Horoscopes = ({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) => {
  const prefs = useSelector(state => state.Prefs.values);
  const framedWidgets = useSelector(state => state.FramedWidgets);
  const { src, state: frameState } = resolveWidgetFrame(
    HOROSCOPES_ENTRY,
    prefs,
    framedWidgets
  );

  const widgetSize = resolveWidgetSize(HOROSCOPES_ENTRY, prefs);

  // Show the "New" badge until the user first interacts with the widget.
  const hasInteracted = prefs["widgets.horoscopes.interaction"];

  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: HOROSCOPES_ENTRY,
    widgetSize,
  });

  const handleInteraction = useCallback(
    () => handleUserInteraction("horoscopes"),
    [handleUserInteraction]
  );

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: HOROSCOPES_ENTRY.sizePref, value: size },
        })
      );
      // `value` is action_value; `size` overrides the reported widget_size,
      // which would otherwise still be the pre-change size.
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: "context_menu",
        value: size,
        size,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  // The shared footer opens the support link; here we only record the click.
  const handleLearnMore = () => {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, { source: "context_menu" });
    handleInteraction();
  };

  if (frameState === "hidden") {
    return null;
  }

  return (
    <article
      className={`horoscopes widget col-4 ${widgetSize}-widget`}
      ref={impressionRef}
      aria-labelledby="horoscopes-widget-label"
    >
      <div className="horoscopes-title-wrapper">
        <div className="horoscopes-badge-title-wrapper">
          {!hasInteracted && (
            <moz-badge
              className="horoscopes-new-badge"
              data-l10n-id="newtab-widget-lists-label-new"
            ></moz-badge>
          )}
          <h2
            id="horoscopes-widget-label"
            className="newtab-widget-title"
            data-l10n-id="newtab-horoscopes-widget-title"
          />
        </div>
        <div className="horoscopes-context-menu-wrapper">
          <WidgetMenuButton
            className="horoscopes-context-menu-button"
            menuId="horoscopes-context-menu"
            l10nId="newtab-horoscopes-widget-open-menu-button"
          />
          <panel-list
            className="panel-list-no-icons"
            id="horoscopes-context-menu"
          >
            <WidgetMenuPoweredBy
              dispatch={dispatch}
              url={YAHOO_URL}
              l10nId="newtab-horoscopes-menu-powered-by-yahoo"
              recordUserAction={recordUserAction}
              userAction={USER_ACTION_TYPES.POWERED_BY_YAHOO}
              onClick={handleInteraction}
            />
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId={HOROSCOPES_ENTRY.id}
              widgetEnabledMap={widgetEnabledMap}
              widgetName={HOROSCOPES_ENTRY.telemetryName}
              enabledPref={HOROSCOPES_ENTRY.enabledPref}
              widgetSize={widgetSize}
              learnMoreL10nId="newtab-horoscopes-menu-learn-more"
              onLearnMore={handleLearnMore}
              sizeSubmenu={
                widgetsMayBeMaximized ? (
                  <SizeSubmenu
                    submenuId="horoscopes-size-submenu"
                    sizes={HOROSCOPES_ENTRY.validSizes}
                    checkedSize={widgetSize}
                    onChangeSize={handleChangeSize}
                  />
                ) : null
              }
            />
          </panel-list>
        </div>
      </div>

      <div className="horoscopes-body">
        <iframe
          className="horoscopes-frame"
          aria-labelledby="horoscopes-widget-label"
          src={src}
          sandbox={FRAMED_WIDGET_SANDBOX}
        />
      </div>
    </article>
  );
};

export { Horoscopes };
