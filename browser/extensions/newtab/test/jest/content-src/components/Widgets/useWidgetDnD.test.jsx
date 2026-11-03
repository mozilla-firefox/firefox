/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, render } from "@testing-library/react";
import { useWidgetDnD } from "content-src/components/Widgets/useWidgetDnD.jsx";

const DEFAULT_ORDER = ["lists", "focusTimer", "weather"];

let latest;

function Row({
  widgetOrder = DEFAULT_ORDER,
  prefs = {},
  dispatch = jest.fn(),
}) {
  const api = useWidgetDnD({ widgetOrder, prefs, dispatch });
  latest = api;
  return (
    <div ref={api.containerRef}>
      {api.effectiveOrder.map(id => (
        <div key={id} data-widget-id={id} {...api.getItemProps(id)}>
          <moz-button>menu</moz-button>
          <moz-widget-header></moz-widget-header>
          <panel-list>
            <hr />
          </panel-list>
          <a href="https://example.com">link</a>
          <div className="task" draggable="true">
            task
          </div>
        </div>
      ))}
    </div>
  );
}

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

function mount(props) {
  const utils = render(<Row {...props} />);
  const slots = [...utils.container.querySelectorAll("[data-widget-id]")];
  slots.forEach(setRect);
  return { ...utils, slots };
}

function pointer(type, opts = {}) {
  return new PointerEvent(type, {
    bubbles: true,
    pointerId: 1,
    pointerType: "mouse",
    button: 0,
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

beforeEach(() => {
  window.matchMedia = jest.fn(() => ({ matches: false }));
});

describe("useWidgetDnD", () => {
  it("requests a widgets.order preference update after a completed drag", () => {
    const dispatch = jest.fn();
    const { slots } = mount({ dispatch });
    act(() => {
      slots[0].dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });

    expect(dispatch).toHaveBeenCalledTimes(1);
    const [[action]] = dispatch.mock.calls;
    expect(action.data.name).toBe("widgets.order");
    expect(action.data.value).toBe("focusTimer,lists,weather");
  });

  it("renders the committed order optimistically before the pref lands", () => {
    const { slots } = mount();
    act(() => {
      slots[0].dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(pointer("pointerup", { clientX: 150, clientY: 50 }));
    });
    expect(latest.effectiveOrder).toEqual(["focusTimer", "lists", "weather"]);
  });

  it("does not start a drag from a moz-button inside a widget", () => {
    const dispatch = jest.fn();
    const { slots } = mount({ dispatch });
    const button = slots[0].querySelector("moz-button");
    act(() => {
      button.dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(150, 50);
    expect(latest.draggedId).toBe(null);
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("does not start a drag from inside a widget's menu", () => {
    const dispatch = jest.fn();
    const { slots } = mount({ dispatch });
    const separator = slots[0].querySelector("panel-list hr");
    act(() => {
      separator.dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(150, 50);
    expect(latest.draggedId).toBe(null);
    expect(dispatch).not.toHaveBeenCalled();
  });

  // moz-widget-header draws its menu button in its shadow root, so the press
  // is retargeted to the header.
  it("does not start a drag from a button in a shadow root", () => {
    const dispatch = jest.fn();
    const { slots } = mount({ dispatch });
    const header = slots[0].querySelector("moz-widget-header");
    const button = document.createElement("moz-button");
    header.attachShadow({ mode: "open" }).append(button);
    act(() => {
      button.dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50, composed: true })
      );
    });
    move(150, 50);
    expect(latest.draggedId).toBe(null);
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("starts a widget drag from a press on an anchor", () => {
    const { slots } = mount();
    const anchor = slots[0].querySelector("a");
    act(() => {
      anchor.dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(150, 50);
    expect(latest.draggedId).toBe("lists");
  });

  it("does not request a preference update when Escape cancels the drag", () => {
    const dispatch = jest.fn();
    const { slots } = mount({ dispatch });
    act(() => {
      slots[0].dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    move(60, 50);
    move(150, 50);
    act(() => {
      window.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
    });
    expect(dispatch).not.toHaveBeenCalled();
    expect(latest.effectiveOrder).toEqual(DEFAULT_ORDER);
  });

  // The Lists widget embeds moz-reorderable-list, which puts draggable="true"
  // on each task's drag handle. Those handles own their own drag.
  it("leaves a nested draggable to handle its own drag", () => {
    const dispatch = jest.fn();
    const { slots } = mount({ dispatch });
    const task = slots[0].querySelector(".task");
    act(() => {
      task.dispatchEvent(pointer("pointerdown", { clientX: 50, clientY: 50 }));
    });
    move(150, 50);
    expect(latest.draggedId).toBe(null);
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("suppresses the native drag once the widget owns the gesture", () => {
    const { slots } = mount();
    const anchor = slots[0].querySelector("a");
    act(() => {
      anchor.dispatchEvent(
        pointer("pointerdown", { clientX: 50, clientY: 50 })
      );
    });
    const dragStart = new Event("dragstart", {
      bubbles: true,
      cancelable: true,
    });
    act(() => {
      anchor.dispatchEvent(dragStart);
    });
    expect(dragStart.defaultPrevented).toBe(true);
  });

  it("leaves the native drag alone when no widget gesture is active", () => {
    const { slots } = mount();
    const anchor = slots[0].querySelector("a");
    const dragStart = new Event("dragstart", {
      bubbles: true,
      cancelable: true,
    });
    act(() => {
      anchor.dispatchEvent(dragStart);
    });
    expect(dragStart.defaultPrevented).toBe(false);
  });
});
