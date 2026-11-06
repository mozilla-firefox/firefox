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
import { SizeSubmenu } from "../SizeSubmenu";
import { useWidgetTelemetry } from "../useWidgetTelemetry";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
};

const SOLITAIRE_ENTRY = WIDGET_REGISTRY.find(w => w.id === "solitaire");

/**
 * Solitaire widget.
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
function Solitaire({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const framedWidgets = useSelector(state => state.FramedWidgets);
  const { src, state: frameState } = resolveWidgetFrame(
    SOLITAIRE_ENTRY,
    prefs,
    framedWidgets
  );

  const widgetSize = resolveWidgetSize(SOLITAIRE_ENTRY, prefs);

  // Show the "New" badge until the user first interacts with the widget.
  const hasInteracted = prefs["widgets.solitaire.interaction"];

  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: SOLITAIRE_ENTRY,
    widgetSize,
  });

  const handleInteraction = useCallback(
    () => handleUserInteraction("solitaire"),
    [handleUserInteraction]
  );

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: SOLITAIRE_ENTRY.sizePref, value: size },
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
      className={`solitaire widget col-4 ${widgetSize}-widget`}
      ref={impressionRef}
      aria-labelledby="solitaire-widget-label"
    >
      <div className="solitaire-title-wrapper">
        <div className="solitaire-badge-title-wrapper">
          {!hasInteracted && (
            <moz-badge
              className="solitaire-new-badge"
              data-l10n-id="newtab-widget-lists-label-new"
            ></moz-badge>
          )}
          <h2
            id="solitaire-widget-label"
            className="newtab-widget-title"
            data-l10n-id="newtab-solitaire-widget-title"
          />
        </div>
        <div className="solitaire-context-menu-wrapper">
          <WidgetMenuButton
            className="solitaire-context-menu-button"
            menuId="solitaire-context-menu"
            l10nId="newtab-solitaire-widget-open-menu-button"
          />
          <panel-list
            className="panel-list-no-icons"
            id="solitaire-context-menu"
          >
            {/* No items above the footer yet, so its leading divider is off.
                Add widget items here and set showDivider back to true. */}
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId={SOLITAIRE_ENTRY.id}
              widgetEnabledMap={widgetEnabledMap}
              widgetName={SOLITAIRE_ENTRY.telemetryName}
              enabledPref={SOLITAIRE_ENTRY.enabledPref}
              widgetSize={widgetSize}
              learnMoreL10nId="newtab-solitaire-menu-learn-more"
              onLearnMore={handleLearnMore}
              showDivider={false}
              sizeSubmenu={
                widgetsMayBeMaximized ? (
                  <SizeSubmenu
                    submenuId="solitaire-size-submenu"
                    sizes={SOLITAIRE_ENTRY.validSizes}
                    checkedSize={widgetSize}
                    onChangeSize={handleChangeSize}
                  />
                ) : null
              }
            />
          </panel-list>
        </div>
      </div>

      <div className="solitaire-body">
        <iframe
          className="solitaire-frame"
          aria-labelledby="solitaire-widget-label"
          src={src}
          sandbox={FRAMED_WIDGET_SANDBOX}
        />
      </div>
    </article>
  );
}

export { Solitaire };
