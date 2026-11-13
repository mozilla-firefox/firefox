/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { WIDGET_SOURCES } from "common/WidgetsRegistry.mjs";
import { MoveSubmenu } from "./MoveSubmenu";
import { WidgetHideMenuItem } from "./WidgetPlacement";

const LEARN_MORE_URL = "https://support.mozilla.org/kb/firefox-new-tab-widgets";

// Shared trailing block for the widget menus that use it, in a fixed order:
// divider, Change size, Move, Hide widget, Learn more. Size submenu is passed in.
export const WidgetMenuFooter = ({
  dispatch,
  // widgetId is the registry id used by Move (e.g. "focusTimer"); widgetName is
  // the telemetry name (e.g. "focus_timer").
  widgetId,
  widgetEnabledMap,
  widgetName,
  enabledPref,
  widgetSize,
  learnMoreL10nId,
  sizeSubmenu,
  onAfterHide,
  onLearnMore,
  // Suppress the divider when the widget has no items above the footer, so it
  // doesn't render as a stray leading separator.
  showDivider = true,
}) => {
  const handleHide = () => {
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: { name: enabledPref, value: false },
      })
    );
    dispatch(
      ac.OnlyToMain({
        type: at.WIDGETS_ENABLED,
        data: {
          widget_name: widgetName,
          widget_source: WIDGET_SOURCES.CONTEXT_MENU,
          enabled: false,
          widget_size: widgetSize,
        },
      })
    );
    onAfterHide?.();
  };

  const handleLearnMore = () => {
    dispatch(
      ac.OnlyToMain({
        type: at.OPEN_LINK,
        data: { url: LEARN_MORE_URL, where: "tab" },
      })
    );
    onLearnMore?.();
  };

  return (
    <>
      {showDivider && <hr />}
      {sizeSubmenu}
      <MoveSubmenu widgetId={widgetId} widgetEnabledMap={widgetEnabledMap} />
      <WidgetHideMenuItem widgetId={widgetId} onHide={handleHide} />
      <panel-item data-l10n-id={learnMoreL10nId} onClick={handleLearnMore} />
    </>
  );
};
