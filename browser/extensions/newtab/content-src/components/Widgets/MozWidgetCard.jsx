/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback, useEffect, useRef, useState } from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { WidgetMenuFooter } from "./WidgetMenuFooter";
import { SizeSubmenu } from "./SizeSubmenu";
import { WidgetError } from "./WidgetError";
import { useWidgetTelemetry } from "./useWidgetTelemetry";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
};

/**
 * The moz-widget card of a WIDGET_REGISTRY entry drawn by a widget document.
 * Its menu holds the shared widget items, below any menuItems. Without a
 * document, the card shows a placeholder.
 *
 * @param {object} props
 * @param {Function} props.dispatch - Redux dispatch.
 * @param {object} props.entry - the WIDGET_REGISTRY entry, merged with its
 *   ExtensionWidget
 * @param {string} props.size - the widget's size
 * @param {string} [props.displaySize] - the size the widget is drawn at, which
 *   can be larger than its size
 * @param {string} props.src - URL of the widget's document, or "" for none
 * @param {boolean} [props.readyOnLoad=true] - count the document's load as ready.
 * @param {object} props.widgetEnabledMap - Map of widget id to whether it is
 *   currently active, used by the Move submenu.
 * @param {React.ReactNode} [props.menuItems] - menu items drawn above the
 *   shared ones; omit for none
 * @param {Function} [props.onChangeSize] - called with the size the user
 *   chooses, after it is stored
 * @param {{current: Element|null}} [props.cardRef] - set to the moz-widget
 */
export function MozWidgetCard({
  dispatch,
  entry,
  size,
  displaySize = size,
  src,
  readyOnLoad = true,
  widgetEnabledMap,
  menuItems,
  onChangeSize,
  cardRef,
}) {
  const [lastSrc, setLastSrc] = useState(src);
  const [failure, setFailure] = useState(null);
  const [isReady, setIsReady] = useState(false);
  const [seen, setSeen] = useState(false);
  const sizeAtImpression = useRef(null);

  if (src !== lastSrc) {
    setLastSrc(src);
    setFailure(null);
    setIsReady(false);
  }

  const { impressionRef, recordUserAction, recordError, recordReady } =
    useWidgetTelemetry({
      dispatch,
      widget: entry,
      widgetSize: size,
      onImpression: data => {
        sizeAtImpression.current = data.widget_size;
        setSeen(true);
      },
    });

  // Wait for both signals, in either order, so widgets_ready counts the same
  // population as widgets_impression. The size is the one the impression
  // recorded, since the user can resize between the two.
  const readyRecorded = useRef(false);
  useEffect(() => {
    if (seen && isReady && !readyRecorded.current) {
      readyRecorded.current = true;
      recordReady({ size: sizeAtImpression.current });
    }
  }, [seen, isReady, recordReady]);

  const widgetRef = useRef(null);
  const setCardRef = useCallback(
    el => {
      widgetRef.current = el;
      impressionRef(el);
      if (cardRef) {
        cardRef.current = el;
      }
    },
    [impressionRef, cardRef]
  );

  /**
   * @backward-compat { version 161 }
   * moz-widget gained the widget-error and widget-ready events in 161. On 160
   * they never fire, so a framed widget there shows no message and records
   * neither widgets_error nor widgets_ready. Nothing breaks; remove this note
   * once 161 reaches Release.
   */
  useEffect(() => {
    const card = widgetRef.current;
    if (!card) {
      return undefined;
    }
    const onError = event => {
      setIsReady(false);
      setFailure({ title: card.title, reason: event.detail?.reason });
    };
    const onReady = () => {
      setIsReady(true);
      setFailure(null);
    };
    card.addEventListener("widget-error", onError);
    card.addEventListener("widget-ready", onReady);
    return () => {
      card.removeEventListener("widget-error", onError);
      card.removeEventListener("widget-ready", onReady);
    };
  }, []);

  const handleChangeSize = useCallback(
    newSize => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: entry.sizePref, value: newSize },
        })
      );
      onChangeSize?.(newSize);
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: "context_menu",
        value: newSize,
        size: newSize,
      });
    },
    [dispatch, entry.sizePref, onChangeSize, recordUserAction]
  );

  const handleLearnMore = () => {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, { source: "context_menu" });
  };

  return (
    <moz-widget
      ref={setCardRef}
      widget-id={entry.id}
      size={displaySize}
      padded={true}
      ready-on-load={readyOnLoad ? "" : undefined}
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
      {!src && <p data-l10n-id="newtab-widget-placeholder" />}
      {failure && (
        <WidgetError
          slot="error"
          recordError={recordError}
          title={entry.name ?? failure.title}
          errorType={failure.reason}
          l10nId={entry.errorL10nId}
        />
      )}
      <panel-list slot="menu" className="panel-list-no-icons">
        {menuItems}
        <WidgetMenuFooter
          dispatch={dispatch}
          widgetId={entry.id}
          widgetEnabledMap={widgetEnabledMap}
          widgetName={entry.telemetryName}
          enabledPref={entry.enabledPref}
          widgetSize={size}
          learnMoreL10nId="newtab-widget-section-menu-learn-more"
          onLearnMore={handleLearnMore}
          showDivider={!!menuItems}
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
