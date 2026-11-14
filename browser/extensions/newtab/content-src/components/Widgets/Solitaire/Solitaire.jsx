/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useCallback, useEffect, useRef, useState } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  FRAMED_WIDGET_SANDBOX,
  WIDGET_REGISTRY,
  resolveWidgetFrame,
  resolveWidgetSize,
  WIDGET_USER_ACTIONS,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { WidgetMenuPoweredBy } from "../WidgetMenuPoweredBy";
import { SizeSubmenu } from "../SizeSubmenu";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { useFramedWidgetMessages } from "../useFramedWidgetMessages";

const USER_ACTION_TYPES = {
  ...WIDGET_USER_ACTIONS,
  POWERED_BY_YAHOO: "powered_by_yahoo",
};

const YAHOO_URL = "https://www.yahoo.com/";

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

  const iframeRef = useRef(null);
  const dialogRef = useRef(null);
  const [playing, setPlaying] = useState(false);
  // Bumped to remount the iframe so the widget reloads into "Resume game".
  const [frameKey, setFrameKey] = useState(0);

  useFramedWidgetMessages({
    widget: SOLITAIRE_ENTRY,
    src,
    iframeRef,
    onMessage(type, message) {
      if (type !== "action") {
        return;
      }
      if (message === "start_game") {
        setPlaying(true);
        handleInteraction();
      } else if (message === "close_game" || message === "reset_game") {
        setPlaying(false);
      }
    },
  });

  const playingRef = useRef(playing);
  playingRef.current = playing;

  useEffect(() => setPlaying(false), [src]);

  // In the card the dialog stays closed and CSS shows it, since show() would
  // move focus into the game and dismiss open popovers. It stays mounted, so
  // the same iframe document keeps running in the modal.
  useEffect(() => {
    if (playing) {
      dialogRef.current?.showModal();
    } else if (dialogRef.current?.open) {
      dialogRef.current.close();
    }
  }, [playing]);

  const handleCloseModal = useCallback(() => {
    setPlaying(false);
    setFrameKey(key => key + 1);
  }, []);

  // Closes from the effect above happen after playing is already false, so
  // only a close by the user (Esc or a backdrop click) gets here while
  // playing.
  const handleClose = useCallback(() => {
    if (playingRef.current) {
      handleCloseModal();
    }
  }, [handleCloseModal]);

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
        source: WIDGET_SOURCES.CONTEXT_MENU,
        value: size,
        size,
      });
      handleInteraction();
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  // The shared footer opens the support link; here we only record the click.
  const handleLearnMore = () => {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, {
      source: WIDGET_SOURCES.CONTEXT_MENU,
    });
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
      {/* Hidden rather than unmounted, since its title names the dialog. */}
      <div className="solitaire-title-wrapper" hidden={playing}>
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
            <WidgetMenuPoweredBy
              dispatch={dispatch}
              url={YAHOO_URL}
              l10nId="newtab-solitaire-menu-powered-by-yahoo"
              recordUserAction={recordUserAction}
              userAction={USER_ACTION_TYPES.POWERED_BY_YAHOO}
              onClick={handleInteraction}
            />
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId={SOLITAIRE_ENTRY.id}
              widgetEnabledMap={widgetEnabledMap}
              widgetName={SOLITAIRE_ENTRY.telemetryName}
              enabledPref={SOLITAIRE_ENTRY.enabledPref}
              widgetSize={widgetSize}
              learnMoreL10nId="newtab-solitaire-menu-learn-more"
              onLearnMore={handleLearnMore}
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

      <dialog
        ref={dialogRef}
        className="solitaire-dialog"
        role={playing ? undefined : "none"}
        aria-labelledby={playing ? "solitaire-widget-label" : undefined}
        // eslint-disable-next-line react/no-unknown-property
        closedby="any"
        onClose={handleClose}
      >
        {playing && (
          <moz-button
            className="solitaire-dialog-close"
            type="icon ghost"
            iconSrc="chrome://global/skin/icons/close.svg"
            data-l10n-id="newtab-solitaire-widget-close-button"
            onClick={handleCloseModal}
          />
        )}
        <iframe
          key={frameKey}
          ref={iframeRef}
          className="solitaire-frame"
          aria-labelledby="solitaire-widget-label"
          src={src}
          sandbox={FRAMED_WIDGET_SANDBOX}
        />
      </dialog>
    </article>
  );
}

export { Solitaire };
