/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, render } from "@testing-library/react";
import {
  captureSlotRects,
  cursorToSlot,
  usePointerReorder,
} from "content-src/lib/usePointerReorder.jsx";

const ORDER = ["a", "b", "c"];

// Keys a tile's rect to its position in the DOM, which is enough for tests that
// do not depend on a tile changing cells. Use setOrderAwareRects below when the
// test needs a cell to move.
function setRect(el, index) {
  el.getBoundingClientRect = () => ({
    left: index * 100,
    right: index * 100 + 100,
    top: 0,
    bottom: 100,
    width: 100,
    height: 100,
    x: index * 100,
    y: 0,
    toJSON() {},
  });
}

let latest;

function Grid({ order = ORDER, onCommit = () => {}, enabled = true }) {
  const api = usePointerReorder({
    order,
    onCommit,
    itemSelector: "[data-tile-id]",
    idAttr: "tileId",
    ignoreSelector: "button",
    enabled,
  });
  latest = api;
  return (
    <div ref={api.containerRef}>
      {order.map(id => (
        <div key={id} data-tile-id={id} {...api.getItemProps(id)}>
          <button type="button">menu</button>
        </div>
      ))}
    </div>
  );
}

function mount(props) {
  const utils = render(<Grid {...props} />);
  const tiles = [...utils.container.querySelectorAll("[data-tile-id]")];
  tiles.forEach(setRect);
  return { ...utils, tiles };
}

function pointer(type, opts = {}) {
  return new PointerEvent(type, {
    bubbles: true,
    pointerId: 1,
    pointerType: "mouse",
    button: 0,
    buttons: type === "pointermove" ? 1 : 0,
    clientX: 0,
    clientY: 0,
    ...opts,
  });
}

function move(x, y) {
  act(() => {
    window.dispatchEvent(pointer("pointermove", { clientX: x, clientY: y }));
  });
}

function press(tile, x = 50) {
  act(() => {
    tile.dispatchEvent(pointer("pointerdown", { clientX: x, clientY: 50 }));
  });
}

function translateX(el) {
  return Number(/^(-?[\d.]+)px/.exec(el.style.translate)?.[1]);
}

// jsdom has no layout, so the viewport is stubbed. The default is wide enough
// that the clamp never bites, leaving tests that are not about it unaffected;
// the clamp tests narrow it themselves.
function setViewport(width, left = 0) {
  Object.defineProperty(document.documentElement, "clientWidth", {
    value: width,
    configurable: true,
  });
  Object.defineProperty(document.documentElement, "clientLeft", {
    value: left,
    configurable: true,
  });
}

beforeEach(() => {
  window.matchMedia = jest.fn(() => ({ matches: false }));
  setViewport(1000);
});

afterEach(() => {
  jest.useRealTimers();
});

describe("cursorToSlot", () => {
  const rects = [
    { left: 0, right: 100, top: 0, bottom: 100 },
    { left: 100, right: 200, top: 0, bottom: 100 },
  ];

  it("returns the slot containing the cursor", () => {
    expect(cursorToSlot(rects, 50, 50)).toBe(0);
    expect(cursorToSlot(rects, 150, 50)).toBe(1);
  });

  it("returns null in a gap so the caller holds its last slot", () => {
    expect(cursorToSlot(rects, 250, 50)).toBe(null);
    expect(cursorToSlot(rects, 50, 300)).toBe(null);
  });

  it("returns null without frozen rects", () => {
    expect(cursorToSlot(null, 50, 50)).toBe(null);
  });

  it("skips holes left by a missing tile", () => {
    expect(cursorToSlot([null, rects[1]], 50, 50)).toBe(null);
    expect(cursorToSlot([null, rects[1]], 150, 50)).toBe(1);
  });
});

describe("captureSlotRects", () => {
  it("returns rects in the requested order, with holes for missing ids", () => {
    const { container, tiles } = mount();
    const rects = captureSlotRects(
      container.firstChild,
      ["c", "a", "missing"],
      "[data-tile-id]",
      "tileId"
    );
    expect(rects[0].left).toBe(tiles[2].getBoundingClientRect().left);
    expect(rects[1].left).toBe(tiles[0].getBoundingClientRect().left);
    expect(rects[2]).toBe(null);
  });

  it("returns null without a container", () => {
    expect(captureSlotRects(null, ORDER, "[data-tile-id]", "tileId")).toBe(
      null
    );
  });
});

describe("usePointerReorder activation", () => {
  it("ignores touch pointers", () => {
    const { tiles } = mount();
    act(() => {
      tiles[0].dispatchEvent(
        pointer("pointerdown", {
          clientX: 10,
          clientY: 10,
          pointerType: "touch",
        })
      );
    });
    move(60, 10);
    expect(latest.draggedId).toBe(null);
  });

  it("ignores non-primary buttons", () => {
    const { tiles } = mount();
    act(() => {
      tiles[0].dispatchEvent(
        pointer("pointerdown", { clientX: 10, clientY: 10, button: 2 })
      );
    });
    move(60, 10);
    expect(latest.draggedId).toBe(null);
  });

  it("does not drag when the pointerdown was on an ignored descendant", () => {
    const { tiles } = mount();
    const button = tiles[0].querySelector("button");
    act(() => {
      button.dispatchEvent(
        pointer("pointerdown", { clientX: 10, clientY: 10 })
      );
    });
    move(60, 10);
    expect(latest.draggedId).toBe(null);
  });

  it("clears the text selection when the drag starts", () => {
    const removeAllRanges = jest.fn();
    jest.spyOn(document, "getSelection").mockReturnValue({ removeAllRanges });
    const { tiles } = mount();
    press(tiles[0], 10);
    expect(removeAllRanges).not.toHaveBeenCalled();
    move(14, 50);
    expect(removeAllRanges).toHaveBeenCalledTimes(1);
    document.getSelection.mockRestore();
  });

  it("does not drag when disabled", () => {
    const { tiles } = mount({ enabled: false });
    act(() => {
      tiles[0].dispatchEvent(
        pointer("pointerdown", { clientX: 10, clientY: 10 })
      );
    });
    move(60, 10);
    expect(latest.draggedId).toBe(null);
  });
});

describe("usePointerReorder threshold", () => {
  function click(el) {
    const evt = new MouseEvent("click", { bubbles: true, cancelable: true });
    act(() => {
      el.dispatchEvent(evt);
    });
    return evt;
  }

  it("does not mark the tile before the threshold, however long the press", () => {
    jest.useFakeTimers();
    const { tiles } = mount();
    press(tiles[0]);
    act(() => {
      jest.advanceTimersByTime(1000);
    });
    move(53, 50);
    expect(latest.draggedId).toBe(null);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
  });

  it.each([
    ["pointerup", () => pointer("pointerup", { button: 0 })],
    ["Escape", () => new KeyboardEvent("keydown", { key: "Escape" })],
    ["blur", () => new Event("blur")],
    ["contextmenu", () => new Event("contextmenu")],
    ["pointercancel", () => pointer("pointercancel", { button: 0 })],
  ])("does not drag after %s ends the press", (type, createEvent) => {
    const { container, tiles } = mount();
    press(tiles[0]);
    act(() => {
      window.dispatchEvent(createEvent());
    });
    move(54, 50);
    expect(container.querySelector("[data-dragging]")).toBe(null);
  });

  it("ends a press when a move reports no buttons pressed", () => {
    const { container, tiles } = mount();
    press(tiles[0]);
    act(() => {
      window.dispatchEvent(
        pointer("pointermove", { clientX: 54, clientY: 50, buttons: 0 })
      );
    });
    expect(container.querySelector("[data-dragging]")).toBe(null);
    expect(latest.draggedId).toBe(null);
  });

  it("starts the drag once a move reaches the threshold", () => {
    const { tiles } = mount();
    press(tiles[0]);
    move(53, 50);
    expect(latest.draggedId).toBe(null);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    move(54, 50);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(true);
    expect(tiles[0].style.translate).not.toBe("");
    expect(latest.previewOrder).toEqual(ORDER);
  });

  it("starts the drag when the threshold is reached on the vertical axis", () => {
    const { tiles } = mount();
    press(tiles[0]);
    move(50, 54);
    expect(latest.draggedId).toBe("a");
    expect(tiles[0].style.translate).not.toBe("");
  });

  it("suppresses the click that follows a drag", () => {
    jest.useFakeTimers();
    const { tiles } = mount();
    const button = tiles[0].querySelector("button");
    const onClick = jest.fn();
    button.addEventListener("click", onClick);
    press(tiles[0]);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    const evt = click(button);
    expect(onClick).not.toHaveBeenCalled();
    expect(evt.defaultPrevented).toBe(true);

    act(() => {
      jest.runOnlyPendingTimers();
    });
    click(button);
    expect(onClick).toHaveBeenCalledTimes(1);
  });

  it("stops suppressing clicks on the next tick if none arrived", () => {
    jest.useFakeTimers();
    const { tiles } = mount();
    const button = tiles[0].querySelector("button");
    const onClick = jest.fn();
    button.addEventListener("click", onClick);
    press(tiles[0]);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    act(() => {
      jest.runOnlyPendingTimers();
    });
    click(button);
    expect(onClick).toHaveBeenCalledTimes(1);
  });

  it("lets the click through after a press that never reached the threshold", () => {
    const { tiles } = mount();
    const button = tiles[0].querySelector("button");
    const onClick = jest.fn();
    button.addEventListener("click", onClick);
    press(tiles[0]);
    move(53, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 53, clientY: 50 }));
    });
    const evt = click(button);
    expect(onClick).toHaveBeenCalledTimes(1);
    expect(evt.defaultPrevented).toBe(false);
  });

  it("ends the gesture without committing when a move reports no buttons pressed", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    press(tiles[0]);
    move(150, 50);
    act(() => {
      window.dispatchEvent(
        pointer("pointermove", { clientX: 250, clientY: 50, buttons: 0 })
      );
    });
    expect(onCommit).not.toHaveBeenCalled();
    expect(tiles[0].style.transition).toContain("translate");

    act(() => {
      tiles[0].dispatchEvent(new Event("transitionend", { bubbles: true }));
    });
    expect(latest.draggedId).toBe(null);
    press(tiles[1], 150);
    move(154, 50);
    expect(latest.draggedId).toBe("b");
  });

  it("cancels a gesture that is held when dragging is disabled", () => {
    const onCommit = jest.fn();
    const { tiles, rerender } = mount({ onCommit });
    press(tiles[0]);
    rerender(<Grid onCommit={onCommit} enabled={false} />);
    move(54, 50);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    expect(latest.draggedId).toBe(null);
    expect(onCommit).not.toHaveBeenCalled();
  });

  it("cancels an active drag when dragging is disabled", () => {
    const onCommit = jest.fn();
    const { tiles, rerender } = mount({ onCommit });
    const release = jest.spyOn(tiles[0], "releasePointerCapture");
    press(tiles[0]);
    move(54, 50);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(true);
    rerender(<Grid onCommit={onCommit} enabled={false} />);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    expect(release).toHaveBeenCalled();
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    expect(latest.draggedId).toBe(null);
    expect(onCommit).not.toHaveBeenCalled();
  });
});

describe("usePointerReorder preview", () => {
  function startDragOnFirstTile(tiles) {
    press(tiles[0]);
    move(60, 50);
  }

  it("previews the dragged tile into the slot under the cursor", () => {
    const { tiles } = mount();
    startDragOnFirstTile(tiles);
    move(150, 50);
    expect(latest.previewOrder).toEqual(["b", "a", "c"]);
  });

  it("holds the last slot while the cursor is in a gap", () => {
    const { tiles } = mount();
    startDragOnFirstTile(tiles);
    move(150, 50);
    move(900, 50);
    expect(latest.previewOrder).toEqual(["b", "a", "c"]);
  });

  it("marks only the dragged tile with data-dragging", () => {
    const { tiles } = mount();
    startDragOnFirstTile(tiles);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(true);
    expect(tiles[1].hasAttribute("data-dragging")).toBe(false);
  });

  it("assigns a CSS order to every tile from the preview", () => {
    const { tiles } = mount();
    startDragOnFirstTile(tiles);
    move(250, 50);
    expect(tiles[0].style.order).toBe("2");
    expect(tiles[1].style.order).toBe("0");
    expect(tiles[2].style.order).toBe("1");
  });
});

describe("usePointerReorder commit and cancel", () => {
  function up(x, y) {
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: x, clientY: y }));
    });
  }

  function dragFirstTileTo(tiles, x) {
    press(tiles[0]);
    move(60, 50);
    move(x, 50);
  }

  // A cancelled drag stays alive through its return animation, so previewOrder
  // is still set; this is what the row shows in both phases.
  function shownOrder() {
    return latest.previewOrder ?? ORDER;
  }

  it("commits the previewed order on release", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 150);
    up(150, 50);
    expect(onCommit).toHaveBeenCalledWith(["b", "a", "c"]);
  });

  it("commits a release in a gap, using the last previewed slot", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 250);
    move(900, 50);
    up(900, 50);
    expect(onCommit).toHaveBeenCalledWith(["b", "c", "a"]);
  });

  it("does not commit when the tile returns to its own slot", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 150);
    move(50, 50);
    up(50, 50);
    expect(onCommit).not.toHaveBeenCalled();
  });

  it("does not commit a click that never passed the threshold", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    act(() => {
      tiles[0].dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(53, 50);
    up(53, 50);
    expect(onCommit).not.toHaveBeenCalled();
    expect(latest.draggedId).toBe(null);
  });

  it("reverts what the row shows, and commits nothing, on Escape", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 150);
    expect(shownOrder()).toEqual(["b", "a", "c"]);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });
    expect(onCommit).not.toHaveBeenCalled();
    expect(shownOrder()).toEqual(ORDER);
  });

  it("commits nothing on pointercancel", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(pointer("pointercancel"));
    });
    expect(onCommit).not.toHaveBeenCalled();
    expect(shownOrder()).toEqual(ORDER);
  });

  it("commits nothing when the window loses focus", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(new Event("blur"));
    });
    expect(onCommit).not.toHaveBeenCalled();
    expect(shownOrder()).toEqual(ORDER);
  });

  it("stops listening after a drag ends", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    dragFirstTileTo(tiles, 150);
    up(150, 50);
    onCommit.mockClear();
    move(250, 50);
    up(250, 50);
    expect(onCommit).not.toHaveBeenCalled();
  });
});

describe("usePointerReorder visuals", () => {
  function dragFirstTileTo(tiles, x) {
    press(tiles[0]);
    move(60, 50);
    move(x, 50);
  }

  it("translates the dragged tile by the cursor delta", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 130);
    // Grabbed at x 50 inside a tile whose cell starts at 0, so the offset is 50.
    expect(tiles[0].style.translate).toBe("80px 0px");
    // `transform` would be scaled by the `scale` on [data-dragging].
    expect(tiles[0].style.transform).toBe("");
  });

  it("keeps the offset when a preview reorder moves the tile's cell", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 150);
    expect(latest.previewOrder).toEqual(["b", "a", "c"]);
    expect(tiles[0].style.translate).not.toBe("");
  });

  it("does not let the FLIP engine clear the dragged tile's offset", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 150);
    const dragged = tiles[0].style.translate;
    move(250, 50);
    expect(tiles[0].style.translate).not.toBe("");
    expect(tiles[0].style.translate).not.toBe(dragged);
  });

  it("glides home on release and holds data-dragging until it ends", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    expect(tiles[0].style.transition).toContain("translate");
    expect(tiles[0].hasAttribute("data-dragging")).toBe(true);

    act(() => {
      tiles[0].dispatchEvent(new Event("transitionend", { bubbles: true }));
    });
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    expect(tiles[0].style.translate).toBe("");
  });

  it("glides back to the original cell on Escape, then clears the drag", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });
    expect(latest.previewOrder).toEqual(ORDER);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(true);

    act(() => {
      tiles[0].dispatchEvent(new Event("transitionend", { bubbles: true }));
    });
    expect(latest.draggedId).toBe(null);
    expect(latest.previewOrder).toBe(null);
  });

  // Cancelling reverts the preview, and the animation is set up on that render.
  // A later render must not restore the drag offset and cut the return
  // animation short.
  it("keeps the return animation running after a cancel re-renders the row", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });
    expect(tiles[0].style.transition).toContain("translate");
    expect(tiles[0].style.translate).toBe("");
  });

  it("keeps the drop animation running when the committed order re-renders", () => {
    const { tiles, rerender } = mount();
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    act(() => {
      rerender(<Grid order={["b", "a", "c"]} />);
    });
    expect(tiles[0].style.transition).toContain("translate");
    expect(tiles[0].style.translate).toBe("");
  });

  it("ends the drag on transitioncancel as well as transitionend", () => {
    const { tiles } = mount();
    dragFirstTileTo(tiles, 150);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    act(() => {
      tiles[0].dispatchEvent(new Event("transitioncancel", { bubbles: true }));
    });
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    expect(latest.draggedId).toBe(null);
  });
});

describe("usePointerReorder session lifecycle", () => {
  it("ignores a pointermove belonging to a different pointer", () => {
    const { tiles } = mount();
    press(tiles[0]);
    act(() => {
      window.dispatchEvent(
        pointer("pointermove", { clientX: 150, clientY: 50, pointerId: 2 })
      );
    });
    expect(latest.previewOrder).toBe(null);
    expect(tiles[0].style.translate).toBeFalsy();
  });

  it("ignores a pointercancel belonging to a different pointer", () => {
    const { tiles } = mount();
    press(tiles[0]);
    move(60, 50);
    act(() => {
      window.dispatchEvent(pointer("pointercancel", { pointerId: 2 }));
    });
    expect(latest.draggedId).toBe("a");
  });

  it("does not commit when a secondary button is released", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(
        pointer("pointerup", { clientX: 150, clientY: 50, button: 2 })
      );
    });
    expect(onCommit).not.toHaveBeenCalled();
    expect(latest.draggedId).toBe("a");
  });

  // A release inside a child iframe is never seen here, so the gesture would
  // otherwise stay recorded and block every later drag.
  it("recovers when a previous press never released", () => {
    const { tiles } = mount();
    press(tiles[0]);
    press(tiles[1]);
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    move(54, 50);
    expect(latest.draggedId).toBe("b");
    expect(tiles[0].style.translate).toBeFalsy();
  });

  it("allows a new drag once the previous one has settled", () => {
    const onCommit = jest.fn();
    const { tiles } = mount({ onCommit });
    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    act(() => {
      tiles[0].dispatchEvent(new Event("transitionend", { bubbles: true }));
    });
    press(tiles[1]);
    move(54, 50);
    expect(latest.draggedId).toBe("b");
  });

  it("stops a drag cleanly when the component unmounts", () => {
    const { tiles, unmount } = mount();
    press(tiles[0]);
    move(60, 50);
    expect(() => unmount()).not.toThrow();
    expect(() => move(250, 50)).not.toThrow();
  });

  it("stops the return animation when the component unmounts mid-drop", () => {
    jest.useFakeTimers();
    const { tiles, unmount } = mount();
    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    unmount();
    expect(() => jest.runAllTimers()).not.toThrow();
  });

  it("skips the return animation under prefers-reduced-motion", () => {
    window.matchMedia = jest.fn(() => ({ matches: true }));
    const { tiles } = mount();
    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    expect(tiles[0].hasAttribute("data-dragging")).toBe(false);
    expect(tiles[0].style.translate).toBe("");
  });
});

describe("usePointerReorder viewport clamp", () => {
  // 300 fits the three 100-wide tiles exactly, which keeps the bounds easy to
  // reason about.
  beforeEach(() => {
    setViewport(300);
  });

  it("does not let the dragged tile pass the right edge", () => {
    const { tiles } = mount();
    press(tiles[0]);
    move(60, 50);
    move(5000, 50);
    expect(translateX(tiles[0])).toBe(200);
  });

  it("does not let the dragged tile pass the left edge", () => {
    const { tiles } = mount();
    press(tiles[2], 250);
    move(240, 50);
    move(-5000, 50);
    expect(translateX(tiles[2])).toBe(-200);
  });

  it("clamps to a viewport that a left-side scrollbar has narrowed", () => {
    setViewport(300, 15);
    const { tiles } = mount();
    press(tiles[0]);
    move(60, 50);
    move(-5000, 50);
    expect(translateX(tiles[0])).toBe(15);
  });

  it("leaves the drag unclamped when the tile is wider than the viewport", () => {
    setViewport(60);
    const { tiles } = mount();
    press(tiles[0]);
    move(60, 50);
    move(900, 50);
    expect(translateX(tiles[0])).toBe(850);
  });

  it("keeps using the cursor for hit testing while the tile is pinned", () => {
    const { tiles } = mount();
    press(tiles[0]);
    move(60, 50);
    move(250, 50);
    expect(latest.previewOrder).toEqual(["b", "c", "a"]);
    // Past the right edge the tile stops moving, but the preview keeps the last
    // slot the cursor was over; the clamp does not change it.
    move(5000, 50);
    expect(translateX(tiles[0])).toBe(200);
    expect(latest.previewOrder).toEqual(["b", "c", "a"]);
  });
});

// The default stub keys a tile's rect to its DOM index, so it cannot see a cell
// move when only CSS `order` changes. These tests need that movement, so the
// rect is derived from the tile's current `style.order` instead.
function setOrderAwareRects(tiles) {
  tiles.forEach((el, index) => {
    el.getBoundingClientRect = () => {
      const slot = el.style.order === "" ? index : Number(el.style.order);
      return {
        left: slot * 100,
        right: slot * 100 + 100,
        top: 0,
        bottom: 100,
        width: 100,
        height: 100,
        x: slot * 100,
        y: 0,
        toJSON() {},
      };
    };
  });
}

describe("usePointerReorder settle geometry", () => {
  // Cancelling moves the tile's cell back, so the animation has to be set up
  // against that restored cell rather than the preview cell it was sitting in.
  it("aims a cancelled drag animation at the restored cell", () => {
    const { tiles } = mount();
    setOrderAwareRects(tiles);
    const writes = [];
    const [el] = tiles;
    Object.defineProperty(el, "offsetWidth", {
      get() {
        writes.push(`flush:${el.style.translate}|${el.style.transition}`);
        return 100;
      },
      configurable: true,
    });

    press(el);
    move(60, 50);
    move(150, 50);
    expect(latest.previewOrder).toEqual(["b", "a", "c"]);

    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });

    // At the flush the tile must be back in slot 0, still translated to where
    // the cursor left it, and with transitions off, so the animation runs from
    // there.
    const flush = writes.find(w => w.startsWith("flush:"));
    expect(flush).toBe("flush:100px 0px|none");
    expect(el.style.transition).toContain("translate");
    expect(el.style.translate).toBe("");
  });

  it("flushes the dragged tile exactly once per gesture", () => {
    const { tiles } = mount();
    setOrderAwareRects(tiles);
    let flushes = 0;
    Object.defineProperty(tiles[0], "offsetWidth", {
      get() {
        flushes++;
        return 100;
      },
      configurable: true,
    });

    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });
    expect(flushes).toBe(1);
  });

  it("lets a new drag take over an animation already running", () => {
    const { tiles } = mount();
    setOrderAwareRects(tiles);
    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });
    press(tiles[1], 150);
    move(154, 50);
    expect(latest.draggedId).toBe("b");
  });

  // Retiring between the drop and the layout effect is the one new race this
  // handoff introduces. Both events go in a single act() so the effect has not
  // run yet when the second press retires the first session.
  it("starts no animation for a session retired before the effect runs", () => {
    const { tiles } = mount();
    setOrderAwareRects(tiles);
    let flushes = 0;
    Object.defineProperty(tiles[0], "offsetWidth", {
      get() {
        flushes++;
        return 100;
      },
      configurable: true,
    });

    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
      tiles[1].dispatchEvent(
        pointer("pointerdown", { clientX: 150, clientY: 50 })
      );
    });

    expect(flushes).toBe(0);
    expect(tiles[0].style.translate).toBe("");
    move(154, 50);
    expect(latest.draggedId).toBe("b");
  });
});

describe("usePointerReorder neighbour animation", () => {
  it("leaves the displaced tiles animating when the drop is committed", () => {
    const { tiles } = mount();
    setOrderAwareRects(tiles);
    press(tiles[0]);
    move(60, 50);
    move(150, 50);
    // The tile that got displaced is mid-slide at this point.
    expect(tiles[1].style.transition).toContain("transform");

    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    // Releasing must not cut that slide short while the dropped tile glides.
    expect(tiles[1].style.transition).toContain("transform");
  });
});
