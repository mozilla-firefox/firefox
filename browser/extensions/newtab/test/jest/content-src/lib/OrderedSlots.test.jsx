/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { createRef, StrictMode, useLayoutEffect, useRef } from "react";
import { render } from "@testing-library/react";
import { OrderedSlots } from "content-src/lib/OrderedSlots.jsx";

let mountChecks;

function Item({ id }) {
  const ref = useRef(null);
  useLayoutEffect(() => {
    mountChecks.push([
      id,
      ref.current.isConnected,
      !!ref.current.closest(".grid"),
    ]);
  }, [id]);
  return <div ref={ref} data-id={id} />;
}

function Harness({ keys, containerRef, onParentLayout, ...containerProps }) {
  useLayoutEffect(() => {
    onParentLayout?.();
  });
  return (
    <OrderedSlots
      containerRef={containerRef}
      className="grid"
      {...containerProps}
      items={keys.map(key => (
        <Item key={key} id={key} />
      ))}
    />
  );
}

const slotKeys = grid => [...grid.children].map(child => child.dataset.slotKey);
const itemIds = grid =>
  [...grid.querySelectorAll("[data-id]")].map(item => item.dataset.id);

describe("OrderedSlots", () => {
  let containerRef;

  beforeEach(() => {
    mountChecks = [];
    containerRef = createRef();
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("attaches one slot per item, in item order", () => {
    render(<Harness keys={["a", "b", "c"]} containerRef={containerRef} />);
    const grid = containerRef.current;

    expect(slotKeys(grid)).toEqual(["a", "b", "c"]);
    expect(itemIds(grid)).toEqual(["a", "b", "c"]);
    for (const slot of grid.children) {
      expect(slot.style.display).toBe("contents");
      expect(slot.childNodes).toHaveLength(1);
      expect(slot.firstChild.dataset.id).toBe(slot.dataset.slotKey);
    }
  });

  it("moves existing slots with moveBefore and keeps every item node", () => {
    const { rerender } = render(
      <Harness keys={["a", "b", "c"]} containerRef={containerRef} />
    );
    const grid = containerRef.current;
    const items = [...grid.querySelectorAll("[data-id]")];
    const moveBefore = jest.spyOn(Element.prototype, "moveBefore");
    const insertBefore = jest.spyOn(Node.prototype, "insertBefore");

    rerender(<Harness keys={["c", "a", "b"]} containerRef={containerRef} />);

    expect(slotKeys(grid)).toEqual(["c", "a", "b"]);
    for (const item of items) {
      expect(grid.querySelector(`[data-id="${item.dataset.id}"]`)).toBe(item);
    }
    expect(moveBefore).toHaveBeenCalled();
    expect(insertBefore).not.toHaveBeenCalled();
  });

  it("removes a dropped key's slot and inserts a new key in place", () => {
    const { rerender } = render(
      <Harness keys={["a", "b", "c"]} containerRef={containerRef} />
    );
    const grid = containerRef.current;

    rerender(<Harness keys={["a", "d", "c"]} containerRef={containerRef} />);

    expect(slotKeys(grid)).toEqual(["a", "d", "c"]);
    expect(itemIds(grid)).toEqual(["a", "d", "c"]);
    expect(grid.querySelector('[data-slot-key="b"]')).toBeNull();
  });

  it("a child's mount layout effect finds itself connected inside the container", () => {
    const { rerender } = render(
      <Harness keys={["a", "b"]} containerRef={containerRef} />
    );

    rerender(<Harness keys={["a", "b", "c"]} containerRef={containerRef} />);

    expect(mountChecks).toEqual([
      ["a", true, true],
      ["b", true, true],
      ["c", true, true],
    ]);
  });

  it("the sync has run before a parent's layout effect", () => {
    const parentChecks = [];
    const onParentLayout = () => {
      parentChecks.push(slotKeys(containerRef.current));
    };
    const { rerender } = render(
      <Harness
        keys={["a", "b", "c"]}
        containerRef={containerRef}
        onParentLayout={onParentLayout}
      />
    );

    rerender(
      <Harness
        keys={["c", "b", "a"]}
        containerRef={containerRef}
        onParentLayout={onParentLayout}
      />
    );

    expect(parentChecks).toEqual([
      ["a", "b", "c"],
      ["c", "b", "a"],
    ]);
  });

  it("survives StrictMode's extra ref and effect runs, and an unchanged key list moves nothing", () => {
    const moveBefore = jest.spyOn(Element.prototype, "moveBefore");
    const insertBefore = jest.spyOn(Node.prototype, "insertBefore");
    const { rerender } = render(
      <StrictMode>
        <Harness keys={["a", "b"]} containerRef={containerRef} />
      </StrictMode>
    );
    const grid = containerRef.current;

    expect(moveBefore).not.toHaveBeenCalled();
    expect(insertBefore).toHaveBeenCalledTimes(2);

    rerender(
      <StrictMode>
        <Harness keys={["b", "a"]} containerRef={containerRef} />
      </StrictMode>
    );

    expect(grid.children).toHaveLength(2);
    expect(slotKeys(grid)).toEqual(["b", "a"]);

    moveBefore.mockClear();
    insertBefore.mockClear();

    rerender(
      <StrictMode>
        <Harness keys={["b", "a"]} containerRef={containerRef} />
      </StrictMode>
    );

    expect(moveBefore).not.toHaveBeenCalled();
    expect(insertBefore).not.toHaveBeenCalled();
  });

  it("points containerRef at the container and clears it on unmount", () => {
    const { container, unmount } = render(
      <Harness keys={["a"]} containerRef={containerRef} id="grid-id" />
    );

    expect(containerRef.current).toBe(container.querySelector(".grid#grid-id"));

    unmount();

    expect(containerRef.current).toBeNull();
  });
});
