/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { fireEvent, render } from "@testing-library/react";
import { EditClocksPanel } from "content-src/components/Widgets/Clocks/EditClocksPanel";

const DEFAULT_CLOCKS = [
  {
    timeZone: "Europe/Berlin",
    city: "Berlin",
    label: "Home",
    labelColor: "cyan",
  },
  {
    timeZone: "America/New_York",
    city: "New York",
    label: null,
    labelColor: null,
  },
];

function renderPanel(overrides = {}) {
  const props = {
    clockZones: DEFAULT_CLOCKS,
    canAddClock: true,
    onShowAddClock: jest.fn(),
    onEditClock: jest.fn(),
    onRemoveClock: jest.fn(),
    onClose: jest.fn(),
    ...overrides,
  };
  const result = render(<EditClocksPanel {...props} />);
  return { ...result, props };
}

describe("<EditClocksPanel>", () => {
  describe("rendering", () => {
    it("renders the panel with the back button and title", () => {
      const { container } = renderPanel();
      expect(container.querySelector(".clocks-edit-panel")).toBeInTheDocument();
      expect(
        container.querySelector(".clocks-edit-back-button")
      ).toBeInTheDocument();
      expect(
        container
          .querySelector(".clocks-edit-title")
          .getAttribute("data-l10n-id")
      ).toBe("newtab-clock-widget-label-your-clocks");
    });

    it("renders one item per clock with the city name", () => {
      const { container } = renderPanel();
      const items = container.querySelectorAll(".clocks-edit-item");
      expect(items.length).toBe(2);
      expect(items[0].querySelector(".clocks-edit-city").textContent).toBe(
        "Berlin"
      );
      expect(items[1].querySelector(".clocks-edit-city").textContent).toBe(
        "New York"
      );
    });

    it("falls back to deriving the city from the timezone when clock.city is missing", () => {
      const { container } = renderPanel({
        clockZones: [
          {
            timeZone: "America/Los_Angeles",
            label: null,
            labelColor: null,
          },
        ],
      });
      expect(container.querySelector(".clocks-edit-city").textContent).toBe(
        "Los Angeles"
      );
    });

    it("renders the nickname subtitle when a label is set", () => {
      const { container } = renderPanel();
      const subtitle = container
        .querySelectorAll(".clocks-edit-item")[0]
        .querySelector(".clocks-edit-subtitle");
      expect(subtitle.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-label-nickname-with-value"
      );
      expect(JSON.parse(subtitle.getAttribute("data-l10n-args"))).toEqual({
        nickname: "Home",
      });
    });

    it("hides the nickname subtitle from AT when no label is set", () => {
      const { container } = renderPanel();
      const subtitle = container
        .querySelectorAll(".clocks-edit-item")[1]
        .querySelector(".clocks-edit-subtitle");
      expect(subtitle.getAttribute("aria-hidden")).toBe("true");
      expect(subtitle.hasAttribute("data-l10n-id")).toBe(false);
    });

    it("makes each clock item focusable for keyboard hover-reveal", () => {
      const { container } = renderPanel();
      const items = container.querySelectorAll(".clocks-edit-item");
      items.forEach(item => {
        expect(item.getAttribute("tabIndex")).toBe("0");
      });
    });
  });

  describe("add affordance", () => {
    it("renders the add button when canAddClock is true", () => {
      const { container } = renderPanel({ canAddClock: true });
      expect(
        container.querySelector(".clocks-edit-add-button")
      ).toBeInTheDocument();
    });

    it("hides the add button when canAddClock is false", () => {
      const { container } = renderPanel({ canAddClock: false });
      expect(container.querySelector(".clocks-edit-add-button")).toBeNull();
    });

    it("calls onShowAddClock when the add button is clicked", () => {
      const { container, props } = renderPanel();
      fireEvent.click(container.querySelector(".clocks-edit-add-button"));
      expect(props.onShowAddClock).toHaveBeenCalledTimes(1);
    });
  });

  describe("row actions", () => {
    it("calls onEditClock with the row index when the edit button is clicked", () => {
      const { container, props } = renderPanel();
      const editButtons = container.querySelectorAll(
        ".clocks-edit-item-edit-button"
      );
      fireEvent.click(editButtons[1]);
      expect(props.onEditClock).toHaveBeenCalledWith(1);
    });

    it("calls onRemoveClock with the row index when the remove button is clicked", () => {
      const { container, props } = renderPanel();
      const removeButtons = container.querySelectorAll(
        ".clocks-edit-item-remove-button"
      );
      fireEvent.click(removeButtons[0]);
      expect(props.onRemoveClock).toHaveBeenCalledWith(0);
    });

    it("hides the remove button when only one clock remains", () => {
      const { container } = renderPanel({
        clockZones: [DEFAULT_CLOCKS[0]],
      });
      expect(
        container.querySelector(".clocks-edit-item-remove-button")
      ).toBeNull();
      // The edit button is still rendered.
      expect(
        container.querySelector(".clocks-edit-item-edit-button")
      ).toBeInTheDocument();
    });
  });

  describe("close", () => {
    it("calls onClose when the back button is clicked", () => {
      const { container, props } = renderPanel();
      fireEvent.click(container.querySelector(".clocks-edit-back-button"));
      expect(props.onClose).toHaveBeenCalled();
    });

    it("calls onClose on Escape inside the panel", () => {
      const { container, props } = renderPanel();
      fireEvent.keyDown(container.querySelector(".clocks-edit-panel"), {
        key: "Escape",
      });
      expect(props.onClose).toHaveBeenCalled();
    });

    it("does not call onClose for other keys", () => {
      const { container, props } = renderPanel();
      fireEvent.keyDown(container.querySelector(".clocks-edit-panel"), {
        key: "Enter",
      });
      expect(props.onClose).not.toHaveBeenCalled();
    });
  });

  describe("focus management", () => {
    it("focuses the back button (and only the back button) after a double rAF on mount", () => {
      jest.useFakeTimers();
      const { container } = renderPanel();
      const backButton = container.querySelector(".clocks-edit-back-button");
      const backFocusSpy = jest.spyOn(backButton, "focus");
      // Spy on the rest of the focusable elements in the panel so this
      // test fails if focus accidentally lands somewhere else.
      const otherFocusables = container.querySelectorAll(
        ".clocks-edit-add-button, .clocks-edit-item, .clocks-edit-item-button"
      );
      const otherSpies = Array.from(otherFocusables).map(el =>
        jest.spyOn(el, "focus")
      );
      try {
        // Two rAFs are scheduled; advance both.
        jest.runOnlyPendingTimers();
        jest.runOnlyPendingTimers();
        expect(backFocusSpy).toHaveBeenCalled();
        otherSpies.forEach(spy => expect(spy).not.toHaveBeenCalled());
      } finally {
        backFocusSpy.mockRestore();
        otherSpies.forEach(spy => spy.mockRestore());
        jest.useRealTimers();
      }
    });
  });
});
