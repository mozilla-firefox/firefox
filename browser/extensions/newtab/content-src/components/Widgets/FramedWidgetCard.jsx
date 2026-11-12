/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef, useState } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { getChosenOption } from "common/WidgetsRegistry.mjs";
import { MozWidgetCard } from "./MozWidgetCard";
import { useWidgetTelemetry } from "./useWidgetTelemetry";
import { useSizeSubmenu } from "../../lib/utils";

const ERROR_MESSAGE = "widget_error";
const READY_MESSAGE = "widget_ready";
// The framed-widgets protocol's reason token rule.
const REASON_PATTERN = /^[a-z][a-z0-9_]{0,63}$/;

// The host actions a WidgetMenuItem of type "action" can run.
const MENU_ACTIONS = {
  "show-customize": (dispatch, entry) =>
    dispatch({
      type: at.SHOW_PERSONALIZE,
      data: { source: entry.telemetryName },
    }),
};

const withoutFragment = url => url.split("#")[0];

/**
 * A submenu of checkable options for a WidgetMenuItem of type "choice".
 *
 * @param {object} props
 * @param {string} props.submenuId - id of the submenu panel-list
 * @param {object} props.item - the WidgetMenuItem
 * @param {string} props.checkedValue - the chosen value
 * @param {Function} props.onChoose - called with the value of a clicked option
 */
function ChoiceSubmenu({ submenuId, item, checkedValue, onChoose }) {
  const submenuRef = useSizeSubmenu(onChoose, "value");
  return (
    <panel-item submenu={submenuId}>
      <span data-l10n-id={item.l10nId}></span>
      <panel-list ref={submenuRef} slot="submenu" id={submenuId}>
        {item.options.map(option => (
          <panel-item
            key={option.value}
            type="checkbox"
            checked={checkedValue === option.value || undefined}
            data-value={option.value}
            data-l10n-id={option.l10nId}
          />
        ))}
      </panel-list>
    </panel-item>
  );
}

/**
 * The card of a WIDGET_REGISTRY entry with `card: "FramedWidgetCard"`, whose document a
 * hosted page provides. It is a MozWidgetCard with the entry's menu items, and
 * it passes the entry's choices to the document as parameters in its URL
 * fragment, without adding session history entries when they change.
 *
 * A web page reports its state to this card with
 * `window.parent.postMessage({ channel: "<telemetryName>_widget", type, payload }, "*")`,
 * where `type` is "widget_error" or "widget_ready". The payload of an error is
 * `{ reason, terminal }`, and only a terminal error covers the frame. The card
 * accepts only messages from the window of its frame and the origin of its
 * document. The protocol is in docs/host-integration.md of
 * mozilla/framed-widgets.
 *
 * @param {object} props - the props of MozWidgetCard
 */
export function FramedWidgetCard(props) {
  const { dispatch, entry, size, src } = props;
  const prefs = useSelector(state => state.Prefs.values);
  const menu = entry.menu ?? [];
  const choices = menu.filter(item => item.type === "choice");
  const frameSrc =
    src && choices.length
      ? `${withoutFragment(src)}#${new URLSearchParams(
          choices.map(item => [item.id, getChosenOption(item, prefs).value])
        )}`
      : src;

  // The frame keeps the URL it first loaded. A change to only its fragment is
  // passed with location.replace() instead, since changing the frame's URL
  // would add a session history entry to the tab.
  const cardRef = useRef(null);
  const [loadedSrc, setLoadedSrc] = useState(frameSrc);
  if (withoutFragment(loadedSrc) !== withoutFragment(frameSrc)) {
    setLoadedSrc(frameSrc);
  }
  const shownSrcRef = useRef(frameSrc);
  useEffect(() => {
    const shownSrc = shownSrcRef.current;
    shownSrcRef.current = frameSrc;
    if (
      frameSrc !== shownSrc &&
      withoutFragment(frameSrc) === withoutFragment(shownSrc)
    ) {
      cardRef.current?.frame?.contentWindow?.location.replace(frameSrc);
    }
  }, [frameSrc]);

  let widgetOrigin = null;
  try {
    widgetOrigin = new URL(loadedSrc).origin;
  } catch {}
  // An opaque origin serialises as "null", which cannot identify the sender.
  if (widgetOrigin === "null") {
    widgetOrigin = null;
  }
  /**
   * @backward-compat { version 161 }
   * moz-widget gained reportError() and reportReady() in 161. On 160 they are
   * missing, so the optional calls do nothing. Drop the `?.` once 161 reaches
   * Release.
   */
  useEffect(() => {
    if (!widgetOrigin) {
      return undefined;
    }
    const channel = `${entry.telemetryName}_widget`;
    function handleMessage(event) {
      const frameWindow = cardRef.current?.frame?.contentWindow;
      if (
        event.origin !== widgetOrigin ||
        !frameWindow ||
        event.source !== frameWindow ||
        event.data?.channel !== channel
      ) {
        return;
      }
      // A widget sends widget_ready when its page loads, even after an earlier
      // terminal error, so a ready must not clear that error.
      if (event.data.type === ERROR_MESSAGE) {
        const { payload } = event.data;
        const reason = payload?.reason;
        if (
          payload?.terminal === true &&
          typeof reason === "string" &&
          REASON_PATTERN.test(reason)
        ) {
          cardRef.current.reportError?.(reason);
        }
      } else if (event.data.type === READY_MESSAGE && !cardRef.current.error) {
        cardRef.current.reportReady?.();
      }
    }
    window.addEventListener("message", handleMessage);
    return () => window.removeEventListener("message", handleMessage);
  }, [widgetOrigin, entry.telemetryName]);

  const { recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: entry,
    widgetSize: size,
  });

  const setPref = (pref, value) =>
    dispatch(ac.OnlyToMain({ type: at.SET_PREF, data: { name: pref, value } }));

  const handleChoose = (item, value) => {
    setPref(item.pref, value);
    recordUserAction(`change_${item.id}`, {
      source: "context_menu",
      value,
    });
  };

  const handleAction = item => {
    MENU_ACTIONS[item.action]?.(dispatch, entry);
    recordUserAction(item.id, { source: "context_menu" });
  };

  // A choice whose option needs more room than the new size switches to the
  // first of its options that fits.
  const handleChangeSize = newSize => {
    const sizes = entry.validSizes;
    const fits = option => sizes.indexOf(option.size) <= sizes.indexOf(newSize);
    for (const item of choices) {
      if (!fits(getChosenOption(item, prefs))) {
        const fitting = item.options.find(fits);
        if (fitting) {
          setPref(item.pref, fitting.value);
        }
      }
    }
  };

  const menuItems = menu.length
    ? menu.map(item =>
        item.type === "choice" ? (
          <ChoiceSubmenu
            key={item.id}
            submenuId={`${entry.id}-${item.id}-submenu`}
            item={item}
            checkedValue={getChosenOption(item, prefs).value}
            onChoose={value => handleChoose(item, value)}
          />
        ) : (
          <panel-item
            key={item.id}
            data-l10n-id={item.l10nId}
            onClick={() => handleAction(item)}
          />
        )
      )
    : null;

  return (
    <MozWidgetCard
      {...props}
      src={loadedSrc}
      readyOnLoad={false}
      menuItems={menuItems}
      onChangeSize={handleChangeSize}
      cardRef={cardRef}
    />
  );
}
