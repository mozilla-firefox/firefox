/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback, useContext, useEffect, useRef } from "react";
import { useDispatch, useSelector } from "react-redux";
import { actionCreators as ac } from "common/Actions.mjs";
import {
  PREF_WIDGETS_ORDER,
  resolveWidgetOrder,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import { WIDGET_PLACEMENTS } from "common/PageLayoutVariants.mjs";
import { WidgetPlacementContext, setWidgetOnHome } from "./WidgetPlacement";

// Action is order-based and direction-agnostic: onMoveLeft always swaps with
// the previous item in the order array. The Fluent strings handle the visual
// flip for RTL locales by translating "Left" as "Right" (and vice versa).
export function buildMoveProps(id, order, enabledMap, dispatch) {
  const visible = order.filter(w => enabledMap?.[w]);
  const idx = visible.indexOf(id);
  const swap = delta => () => {
    const target = visible[idx + delta];
    if (!target) {
      return;
    }
    const newOrder = [...order];
    const a = newOrder.indexOf(id);
    const b = newOrder.indexOf(target);
    [newOrder[a], newOrder[b]] = [newOrder[b], newOrder[a]];
    dispatch(ac.SetPref(PREF_WIDGETS_ORDER, newOrder.join(",")));
  };
  return {
    canMoveLeft: visible[idx - 1] !== undefined,
    canMoveRight: visible[idx + 1] !== undefined,
    onMoveLeft: swap(-1),
    onMoveRight: swap(+1),
  };
}

// Submenu panel-list children are moved into the panel-item's shadow DOM
// by the panel-list custom element, so React's synthetic onClick doesn't
// reach them. Listen at the panel-list root and walk composedPath() to
// find the clicked item by its data-move-dir attribute.
export function MoveSubmenu({ widgetId, widgetEnabledMap }) {
  const prefs = useSelector(state => state.Prefs.values);
  const dispatch = useDispatch();
  const placement = useContext(WidgetPlacementContext);
  const moveProps = buildMoveProps(
    widgetId,
    resolveWidgetOrder(prefs),
    widgetEnabledMap,
    dispatch
  );
  // Where the user pins widgets to Home, the widget can also move between Home
  // and the widget library.
  const toHome = placement === WIDGET_PLACEMENTS.LIBRARY;
  const spaceMove = placement && { dir: toHome ? "home" : "widgets" };
  moveProps.onMoveSpace = () =>
    setWidgetOnHome(
      dispatch,
      prefs,
      widgetId,
      toHome,
      WIDGET_SOURCES.CONTEXT_MENU
    );

  // Read the latest moveProps via a ref so the ref callback stays stable and
  // doesn't re-attach the listener every render.
  const movePropsRef = useRef(moveProps);
  useEffect(() => {
    movePropsRef.current = moveProps;
  }, [moveProps]);

  // A ref callback is required because the submenu is gated
  // behind an early return and only mounts once the widget is
  // movable; the callback fires whenever the node attaches.
  const cleanupRef = useRef(null);
  const submenuRef = useCallback(el => {
    cleanupRef.current?.();
    cleanupRef.current = null;
    if (!el) {
      return;
    }
    const listener = e => {
      const item = e.composedPath().find(n => n.dataset?.moveDir);
      if (!item) {
        return;
      }
      if (item.dataset.moveDir === "left") {
        movePropsRef.current.onMoveLeft();
      } else if (item.dataset.moveDir === "right") {
        movePropsRef.current.onMoveRight();
      } else if (["home", "widgets"].includes(item.dataset.moveDir)) {
        movePropsRef.current.onMoveSpace();
      }
    };
    el.addEventListener("click", listener);
    cleanupRef.current = () => el.removeEventListener("click", listener);
  }, []);

  if (!moveProps.canMoveLeft && !moveProps.canMoveRight && !spaceMove) {
    return null;
  }
  const submenuId = `${widgetId}-move-submenu`;
  return (
    <panel-item submenu={submenuId}>
      <span data-l10n-id="newtab-widget-menu-move"></span>
      <panel-list ref={submenuRef} slot="submenu" id={submenuId}>
        <panel-item
          data-l10n-id="newtab-widget-menu-move-left"
          data-move-dir="left"
          {...(moveProps.canMoveLeft ? {} : { disabled: true })}
        ></panel-item>
        <panel-item
          data-l10n-id="newtab-widget-menu-move-right"
          data-move-dir="right"
          {...(moveProps.canMoveRight ? {} : { disabled: true })}
        ></panel-item>
        {spaceMove && (
          <panel-item
            data-l10n-id={`newtab-widget-menu-move-to-${spaceMove.dir}`}
            data-move-dir={spaceMove.dir}
          ></panel-item>
        )}
      </panel-list>
    </panel-item>
  );
}
