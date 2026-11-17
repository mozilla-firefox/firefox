/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useCallback, useState } from "react";
import { createPortal } from "react-dom";

/**
 * Renders a grid container with each item in its own `display: contents`
 * slot, through a portal. A reorder moves the slots with moveBefore, so an
 * iframe inside an item keeps its state instead of reloading, as it would if
 * React moved the item with insertBefore.
 *
 * The slots are ordered in the container's ref callback. It is memoized on
 * the keys, so React calls it again whenever they change, and React calls it
 * before the layout effects of the items and of the parent. React does not
 * document that order, so the tests check it.
 *
 * Any other props go on the container div. Never pass children or
 * dangerouslySetInnerHTML, which would replace the slots.
 *
 * Delete once React ships enableMoveBefore in a stable release.
 *
 * @param {object} props
 * @param {React.ReactElement[]} props.items - in DOM order, each with a key
 *   that is unique among the items and contains no comma
 * @param {{current: ?Element}} props.containerRef - set to the container div
 */
export function OrderedSlots({ items, containerRef, ...containerProps }) {
  const [slots] = useState(() => new Map());

  const slotFor = key => {
    if (!slots.has(key)) {
      const slot = document.createElement("div");
      slot.style.display = "contents";
      slot.dataset.slotKey = key;
      slots.set(key, slot);
    }
    return slots.get(key);
  };

  const keyString = items.map(el => el.key).join(",");
  const sync = useCallback(
    node => {
      containerRef.current = node;
      if (node) {
        const currentKeys = keyString ? keyString.split(",") : [];
        const keySet = new Set(currentKeys);
        for (const [key, slot] of slots) {
          if (!keySet.has(key)) {
            slot.remove();
            slots.delete(key);
          }
        }

        let anchor = null;
        for (let i = currentKeys.length - 1; i >= 0; i--) {
          const slot = slots.get(currentKeys[i]);
          if (slot.parentNode !== node) {
            node.insertBefore(slot, anchor);
          } else if (slot.nextSibling !== anchor) {
            node.moveBefore(slot, anchor);
          }
          anchor = slot;
        }
      }
    },
    [keyString, containerRef, slots]
  );

  return (
    <>
      <div ref={sync} {...containerProps} />
      {items.map(el => createPortal(el, slotFor(el.key), el.key))}
    </>
  );
}
