/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useEffect, useState } from "react";
import { actionCreators as ac } from "../../../common/Actions.mjs";
import { PREF_WIDGETS_ORDER } from "common/WidgetsRegistry.mjs";
import { usePointerReorder } from "content-src/lib/usePointerReorder.jsx";

// A pointerdown on one of these starts an interaction, not a widget reorder.
// Anchors are deliberately absent: dragging one moves the widget, and a click
// still navigates because a gesture below the threshold commits nothing.
// [draggable='true'] covers nested reorderable lists, such as the tasks inside
// the Lists widget, which own their own drag. panel-list is the widget's menu:
// pressing anywhere in it, even on a separator, shouldn't move the widget.
const INTERACTIVE_DESCENDANT_SELECTOR = [
  "panel-list",
  "[draggable='true']",
  "button",
  "moz-button",
  "moz-checkbox",
  "moz-toggle",
  "moz-radio",
  "moz-select",
  "moz-input-text",
  "moz-input-password",
  "moz-input-search",
  "input",
  "textarea",
  "select",
  "dialog",
  "[contenteditable='true']",
  "[role='button']",
  "[role='checkbox']",
  "[role='switch']",
  "[role='textbox']",
].join(", ");

/**
 * Widgets-row adapter over the generic pointer reorder hook. Keeps the order
 * the user just chose on screen until the pref write comes back.
 */
export function useWidgetDnD({ widgetOrder, prefs, dispatch, enabled = true }) {
  const [optimisticOrder, setOptimisticOrder] = useState(null);

  useEffect(() => {
    if (
      optimisticOrder &&
      prefs[PREF_WIDGETS_ORDER] === optimisticOrder.join(",")
    ) {
      setOptimisticOrder(null);
    }
  }, [prefs, optimisticOrder]);

  const effectiveOrder = optimisticOrder || widgetOrder;

  function commitOrder(newOrder) {
    setOptimisticOrder(newOrder);
    dispatch(ac.SetPref(PREF_WIDGETS_ORDER, newOrder.join(",")));
  }

  const { containerRef, draggedId, previewOrder, getItemProps } =
    usePointerReorder({
      order: effectiveOrder,
      onCommit: commitOrder,
      itemSelector: "[data-widget-id]",
      idAttr: "widgetId",
      ignoreSelector: INTERACTIVE_DESCENDANT_SELECTOR,
      enabled,
    });

  return {
    effectiveOrder,
    containerRef,
    draggedId,
    previewOrder,
    getItemProps,
  };
}
