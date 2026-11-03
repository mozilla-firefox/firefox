/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback } from "react";
import { batch } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { WidgetMenuFooter } from "./WidgetMenuFooter";
import { SizeSubmenu } from "./SizeSubmenu";
import { useWidgetTelemetry } from "./useWidgetTelemetry";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
};

/**
 * The moz-widget card of a WIDGET_REGISTRY entry drawn by a widget document.
 * Its menu holds the shared widget items.
 *
 * @param {object} props
 * @param {Function} props.dispatch - Redux dispatch.
 * @param {object} props.entry - the WIDGET_REGISTRY entry, merged with its
 *   ExtensionWidget
 * @param {string} props.size - the widget's size
 * @param {string} props.src - URL of the widget's document
 * @param {object} props.widgetEnabledMap - Map of widget id to whether it is
 *   currently active, used by the Move submenu.
 */
export function MozWidgetCard({
  dispatch,
  entry,
  size,
  src,
  widgetEnabledMap,
}) {
  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: entry,
    widgetSize: size,
  });

  const handleChangeSize = useCallback(
    newSize => {
      batch(() => {
        dispatch(
          ac.OnlyToMain({
            type: at.SET_PREF,
            data: { name: entry.sizePref, value: newSize },
          })
        );
        recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
          source: "context_menu",
          value: newSize,
          size: newSize,
        });
      });
    },
    [dispatch, entry.sizePref, recordUserAction]
  );

  const handleLearnMore = () => {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, { source: "context_menu" });
  };

  return (
    <moz-widget
      ref={impressionRef}
      widget-id={entry.id}
      size={size}
      padded={true}
      src={src}
      title={entry.name}
      data-l10n-id={entry.l10nId}
    >
      <moz-widget-header
        slot="header"
        title={entry.name}
        data-l10n-id={entry.l10nId}
      >
        {entry.icon && (
          <img
            slot="leading"
            className="moz-widget-card-icon"
            src={entry.icon}
            alt=""
          />
        )}
      </moz-widget-header>
      <panel-list slot="menu" className="panel-list-no-icons">
        <WidgetMenuFooter
          dispatch={dispatch}
          widgetId={entry.id}
          widgetEnabledMap={widgetEnabledMap}
          widgetName={entry.telemetryName}
          enabledPref={entry.enabledPref}
          widgetSize={size}
          learnMoreL10nId="newtab-widget-section-menu-learn-more"
          onLearnMore={handleLearnMore}
          showDivider={false}
          sizeSubmenu={
            entry.validSizes.length > 1 ? (
              <SizeSubmenu
                submenuId={`${entry.id}-size-submenu`}
                sizes={entry.validSizes}
                checkedSize={size}
                onChangeSize={handleChangeSize}
              />
            ) : null
          }
        />
      </panel-list>
    </moz-widget>
  );
}
