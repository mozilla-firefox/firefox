/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import { createReorderFlip } from "content-src/lib/useReorderFlip.jsx";

// macOS reports 4 for ui.dragThresholdX/Y; Windows and Linux take theirs from
// the OS. Newtab content cannot read those prefs, so one value is used
// everywhere. Below it the gesture is a click and nothing is committed.
const DRAG_THRESHOLD_PX = 4;

// Used for the drop animation and passed to the FLIP engine, so a dropped item
// and the tiles it displaced settle at the same moment.
const DROP_GLIDE_MS = 160;

/**
 * Finds which slot the cursor is over, using the positions measured when the
 * drag started. Returns null when the cursor is in a gap between items, which
 * the caller treats as no change, so moving through a gap does not jump the
 * item to whichever slot happens to be nearest.
 */
export function cursorToSlot(slotRects, clientX, clientY) {
  if (!slotRects) {
    return null;
  }
  for (let i = 0; i < slotRects.length; i++) {
    const rect = slotRects[i];
    if (
      rect &&
      clientX >= rect.left &&
      clientX <= rect.right &&
      clientY >= rect.top &&
      clientY <= rect.bottom
    ) {
      return i;
    }
  }
  return null;
}

/**
 * Measures where every item currently sits, in the same sequence as `order`.
 * An id that is not in the container gets null, and there is no result at all
 * without a container. The caller measures once when the drag starts, so the
 * cursor keeps mapping to the same slot even as the preview moves items around
 * underneath it.
 */
export function captureSlotRects(container, order, itemSelector, idAttr) {
  if (!container) {
    return null;
  }
  const rectsById = Object.fromEntries(
    [...container.querySelectorAll(itemSelector)].map(el => [
      el.dataset[idAttr],
      el.getBoundingClientRect(),
    ])
  );
  return order.map(id => rectsById[id] || null);
}

// In RTL layouts the scrollbar can sit on the left, so the usable area does not
// always start at zero; clientLeft accounts for that. Read when a drag starts
// and on each reorder, never on a pointer move, to keep layout reads off that
// path.
function readViewport() {
  const root = document.documentElement;
  return {
    viewportLeft: root.clientLeft,
    viewportRight: root.clientLeft + root.clientWidth,
  };
}

function reorder(order, id, slot) {
  const next = order.filter(x => x !== id);
  next.splice(slot, 0, id);
  return next;
}

// Re-reads everything the transform maths depends on: where the item's cell now
// is, how wide the item is, and the viewport bounds, which a reorder may follow
// a resize into. Leaves the item snapped to its cell, so callers must reapply
// the transform before anything is painted.
function measureCell(d) {
  d.el.style.transform = "";
  const rect = d.el.getBoundingClientRect();
  d.cellOriginX = rect.left;
  d.cellOriginY = rect.top;
  d.width = rect.width;
  Object.assign(d, readViewport());
}

// Capture keeps sending pointer events here once the cursor moves over a
// child's out-of-process iframe. The drag still works without it, so being
// refused is not a problem.
function capturePointer(el, pointerId) {
  try {
    el.setPointerCapture?.(pointerId);
  } catch {
    // Nothing to do.
  }
}

function releasePointer(el, pointerId) {
  try {
    el.releasePointerCapture?.(pointerId);
  } catch {
    // The pointer may already have been released.
  }
}

/**
 * Pointer-driven reorder for a container whose children are placed by CSS
 * `order`. The dragged child is moved by translating the real element rather
 * than by giving the browser a picture of it to drag, so anything inside it
 * keeps rendering for the whole drag. That is why this exists instead of HTML
 * drag and drop: `setDragImage` takes that picture through
 * `PresShell::RenderNode`, which cannot paint an out-of-process iframe, so an
 * iframe child's body comes out blank.
 *
 * The dragged element carries data-dragging until its drop animation ends, and
 * the FLIP engine excludes that attribute from its childSelector. skipSelector
 * would not be enough: it only covers the part of the engine that animates,
 * while the part that measures clears inline transforms on everything it can
 * see. Leaving the dragged element visible to the engine would cancel its drop
 * animation on the next render.
 *
 * Consumers must supply CSS:
 *   the container is a flex or grid container, so that the `order` this hook
 *     sets on each item actually places it. Nothing works without this.
 *   [data-dragging] { z-index: 1; }
 *     without it the dragged item paints under its neighbours.
 *   <container>:has([data-dragging]) iframe { pointer-events: none; }
 *     insurance for embedded content. Pointer capture already keeps the drag
 *     alive over an out-of-process iframe, but only once it has been granted.
 *
 * `itemSelector` must match exactly the elements that receive getItemProps, and
 * each of them must carry the data attribute named by `idAttr`. Putting the
 * attribute on a different element fails silently. `itemSelector` is read once
 * when the FLIP engine is built, so changing it later has no effect.
 */
export function usePointerReorder({
  order,
  onCommit,
  itemSelector,
  idAttr,
  ignoreSelector = null,
  enabled = true,
}) {
  const [draggedId, setDraggedId] = useState(null);
  const [targetSlot, setTargetSlot] = useState(null);
  // Bumped when a drop begins, purely so the layout effect below is guaranteed
  // to run once the item is in its final cell.
  const [settleRequest, setSettleRequest] = useState(0);

  const containerRef = useRef(null);
  const dragRef = useRef(null);
  const orderRef = useRef(order);
  const onCommitRef = useRef(onCommit);
  orderRef.current = order;
  onCommitRef.current = onCommit;

  const flipRef = useRef(null);
  if (!flipRef.current) {
    flipRef.current = createReorderFlip({
      childSelector: `${itemSelector}:not([data-dragging])`,
      durationMs: DROP_GLIDE_MS,
    });
  }

  const applyTransform = useCallback(d => {
    let dx = d.lastX - d.grabOffsetX - d.cellOriginX;
    const dy = d.lastY - d.grabOffsetY - d.cellOriginY;

    // Keep the item within the viewport so dragging cannot raise a horizontal
    // scrollbar. An item wider than the viewport has no position that fits, so
    // it keeps tracking the cursor instead of being pinned to an arbitrary
    // edge. Only the horizontal axis is clamped: the page scrolls vertically
    // anyway, and pinning that axis would stop the item being lifted clear of
    // the row.
    const minDx = d.viewportLeft - d.cellOriginX;
    const maxDx = d.viewportRight - d.width - d.cellOriginX;
    if (maxDx >= minDx) {
      dx = Math.min(Math.max(dx, minDx), maxDx);
    }

    d.el.style.transform = `translate(${dx}px, ${dy}px)`;
  }, []);

  const previewOrder =
    draggedId !== null && targetSlot !== null
      ? reorder(order, draggedId, targetSlot)
      : null;
  const previewKey = (previewOrder || order).join(",");
  const prevKeyRef = useRef(previewKey);

  const handlePointerDown = useCallback(
    (e, id) => {
      // An earlier gesture can still be recorded here: one that never passed
      // the threshold and was released somewhere we cannot see, such as inside
      // a child iframe, or one still playing its drop animation. Clear it
      // instead of refusing the new drag, which would otherwise leave the row
      // unable to drag anything until the window lost focus. This runs on every
      // pointerdown, including ones that never start a drag.
      dragRef.current?.retire();

      // e.target can't see into shadow roots, so a press on the menu button in
      // moz-widget-header would look like a press on the header itself. The
      // composed path still has the button in it.
      const path = e.nativeEvent.composedPath();
      if (
        !enabled ||
        (e.pointerType !== "mouse" && e.pointerType !== "pen") ||
        e.button !== 0 ||
        (ignoreSelector &&
          path
            .slice(0, path.indexOf(e.currentTarget))
            .some(el => el.matches?.(ignoreSelector)))
      ) {
        return;
      }

      const el = e.currentTarget;
      const rect = el.getBoundingClientRect();
      const d = {
        id,
        el,
        pointerId: e.pointerId,
        startX: e.clientX,
        startY: e.clientY,
        lastX: e.clientX,
        lastY: e.clientY,
        grabOffsetX: e.clientX - rect.left,
        grabOffsetY: e.clientY - rect.top,
        cellOriginX: rect.left,
        cellOriginY: rect.top,
        width: rect.width,
        ...readViewport(),
        slotRects: null,
        targetSlot: null,
        // pressed: the pointer is down but has not passed the threshold.
        // dragging: the item is following the cursor.
        // awaitingGlide: the drop has begun and the layout effect has yet to
        //   start the return animation. It can only be set up after the render
        //   that puts the item in its final cell.
        // gliding: that animation is running.
        phase: "pressed",
        endGlide: null,
      };
      dragRef.current = d;

      const settle = () => {
        d.endGlide?.();
        d.el.style.transition = "";
        d.el.style.transform = "";
        dragRef.current = null;
        setDraggedId(null);
        setTargetSlot(null);
      };

      // Animates the item from where the cursor left it into the cell it now
      // owns. Callers must already have measured that cell and reapplied the
      // transform. An interrupted animation reports transitioncancel rather
      // than transitionend, and only one of the two ever arrives, so either
      // ends the drag.
      d.glideHome = () => {
        let timer = 0;
        const done = evt => {
          if (evt && evt.target !== d.el) {
            return;
          }
          settle();
        };
        d.endGlide = () => {
          d.endGlide = null;
          window.clearTimeout(timer);
          d.el.removeEventListener("transitionend", done);
          d.el.removeEventListener("transitioncancel", done);
        };
        d.el.addEventListener("transitionend", done);
        d.el.addEventListener("transitioncancel", done);
        // Nothing fires when the item is already at its target, so a timer ends
        // the drag in that case.
        timer = window.setTimeout(done, DROP_GLIDE_MS * 2);
        d.el.style.transition = `transform ${DROP_GLIDE_MS}ms ease`;
        d.el.style.transform = "";
      };

      const listeners = [];

      const listen = (type, fn, capture = false) => {
        listeners.push([type, fn, capture]);
        window.addEventListener(type, fn, capture);
      };

      const detach = () => {
        for (const [type, fn, capture] of listeners) {
          window.removeEventListener(type, fn, capture);
        }
        listeners.length = 0;
      };

      // Abandon this gesture without animating, whatever stage it reached.
      // Safe to call twice.
      d.retire = () => {
        detach();
        if (d.phase !== "pressed") {
          settle();
        } else {
          dragRef.current = null;
        }
      };

      // Used when the component unmounts: remove the listeners and leave React
      // state alone.
      d.detachOnly = detach;

      const finish = ({ commit }) => {
        detach();
        releasePointer(d.el, d.pointerId);
        if (d.phase === "pressed") {
          dragRef.current = null;
          return;
        }
        const sourceIdx = orderRef.current.indexOf(d.id);
        if (sourceIdx === -1) {
          // The item left the order mid-drag, so there is nothing to commit and
          // no cell to return to.
          settle();
          return;
        }
        if (commit && d.targetSlot !== null && d.targetSlot !== sourceIdx) {
          onCommitRef.current(reorder(orderRef.current, d.id, d.targetSlot));
        } else {
          d.targetSlot = sourceIdx;
          setTargetSlot(sourceIdx);
        }
        if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
          settle();
          return;
        }
        // Hand the item to the layout effect rather than animating from here.
        // Cancelling moves the item's cell back, and that has not happened yet:
        // starting the animation now would aim it at the cell the item is about
        // to leave. previewKey does not always change on the committing path,
        // where the committed order equals the preview it replaces, so bump
        // settleRequest to make the effect run.
        d.phase = "awaitingGlide";
        setSettleRequest(n => n + 1);
      };

      const onMove = evt => {
        if (evt.pointerId !== d.pointerId) {
          return;
        }
        d.lastX = evt.clientX;
        d.lastY = evt.clientY;

        if (d.phase === "pressed") {
          if (
            Math.abs(evt.clientX - d.startX) < DRAG_THRESHOLD_PX &&
            Math.abs(evt.clientY - d.startY) < DRAG_THRESHOLD_PX
          ) {
            return;
          }
          d.phase = "dragging";
          d.slotRects = captureSlotRects(
            containerRef.current,
            orderRef.current,
            itemSelector,
            idAttr
          );
          d.targetSlot = orderRef.current.indexOf(d.id);
          capturePointer(d.el, d.pointerId);
          d.el.style.transition = "none";
          // The few pixels before the threshold can start a text selection, and
          // user-select cannot undo one already in progress.
          document.getSelection()?.removeAllRanges();
          setDraggedId(d.id);
          setTargetSlot(d.targetSlot);
        }

        applyTransform(d);

        const slot = cursorToSlot(d.slotRects, evt.clientX, evt.clientY);
        if (slot !== null && slot !== d.targetSlot) {
          d.targetSlot = slot;
          setTargetSlot(slot);
        }
      };

      const onUp = evt => {
        // A mouse reports every button on one pointer id, so releasing a
        // secondary button must not drop the item.
        if (evt.pointerId !== d.pointerId || evt.button !== 0) {
          return;
        }
        finish({ commit: true });
      };

      const onPointerCancel = evt => {
        if (evt.pointerId !== d.pointerId) {
          return;
        }
        finish({ commit: false });
      };

      const onBlur = () => finish({ commit: false });

      const onKeyDown = evt => {
        if (evt.key === "Escape") {
          finish({ commit: false });
        }
      };

      listen("pointermove", onMove);
      listen("pointerup", onUp);
      listen("pointercancel", onPointerCancel);
      listen("blur", onBlur);
      listen("contextmenu", onBlur);
      listen("keydown", onKeyDown, true);
    },
    [applyTransform, enabled, idAttr, ignoreSelector, itemSelector]
  );

  useLayoutEffect(() => {
    // Read dragRef.current here: a new gesture can retire a settling one before
    // this effect runs.
    const d = dragRef.current;
    const orderChanged = previewKey !== prevKeyRef.current;
    prevKeyRef.current = previewKey;

    // A run woken only to start the return animation has nothing for the engine
    // to animate, and the engine's measurement pass would cut short the slide
    // the displaced items are still playing.
    if (orderChanged || d?.phase !== "awaitingGlide") {
      flipRef.current.sync(containerRef.current, {
        enabled,
        reset: !orderChanged,
      });
    }

    if (!d || d.phase === "pressed") {
      return;
    }

    if (d.phase === "awaitingGlide") {
      // The item is now in the cell it will keep. Reapply the transform so it
      // stays where the cursor left it, then read offsetWidth so the browser
      // takes that as the animation's starting point. Move out of this phase
      // first, or a later run of this effect starts the animation twice.
      d.phase = "gliding";
      measureCell(d);
      applyTransform(d);
      void d.el.offsetWidth;
      d.glideHome();
      return;
    }

    if (d.phase === "dragging") {
      // The engine cannot see the dragged item, so clear its transform to
      // measure the cell it now sits in, then put the transform back before
      // anything is painted.
      measureCell(d);
      applyTransform(d);
    }
  }, [previewKey, settleRequest, enabled, draggedId, applyTransform]);

  // A gesture left running past unmount would hold on to the removed element
  // and its timer, and the timer would then try to update a component that no
  // longer exists.
  useEffect(
    () => () => {
      const d = dragRef.current;
      d?.endGlide?.();
      d?.detachOnly?.();
      dragRef.current = null;
    },
    []
  );

  const previewOrderMap = previewOrder
    ? Object.fromEntries(previewOrder.map((id, i) => [id, i]))
    : null;

  const getItemProps = id => ({
    onPointerDown: e => handlePointerDown(e, id),
    // Images and links start a native drag by themselves, which cancels the
    // pointer events this reorder depends on. Stop that only once this item
    // owns a gesture, so a nested list keeps its own dragging.
    onDragStart: e => {
      if (dragRef.current?.el === e.currentTarget) {
        e.preventDefault();
      }
    },
    style: previewOrderMap ? { order: previewOrderMap[id] } : undefined,
    "data-dragging": draggedId === id ? "" : undefined,
  });

  return { containerRef, draggedId, previewOrder, getItemProps };
}
