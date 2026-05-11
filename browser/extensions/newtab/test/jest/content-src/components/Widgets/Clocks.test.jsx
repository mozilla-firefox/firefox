/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Clocks } from "content-src/components/Widgets/Clocks/Clocks";
import { isValidPaletteName } from "content-src/components/Widgets/Clocks/ClocksHelpers";

jest.mock("content-src/components/Widgets/Clocks/ClocksHelpers.mjs", () => ({
  ...jest.requireActual(
    "content-src/components/Widgets/Clocks/ClocksHelpers.mjs"
  ),
  getRandomLabelColor: jest.fn().mockReturnValue("cyan"),
}));

// Stub Intl.DateTimeFormat().resolvedOptions().timeZone so tests are
// deterministic regardless of the CI/developer machine's local zone. We
// pin the local zone to Europe/Berlin to match the default sample.
const originalResolvedOptions = Intl.DateTimeFormat.prototype.resolvedOptions;
beforeAll(() => {
  Intl.DateTimeFormat.prototype.resolvedOptions = function () {
    const opts = originalResolvedOptions.call(this);
    return { ...opts, timeZone: "Europe/Berlin" };
  };
});
afterAll(() => {
  Intl.DateTimeFormat.prototype.resolvedOptions = originalResolvedOptions;
});

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.enabled": true,
      "widgets.system.clocks.enabled": true,
      "widgets.clocks.enabled": true,
      "widgets.clocks.size": "large",
    },
  },
};

function WrapWithProvider({ children, state = INITIAL_STATE }) {
  const store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

function renderClocks(size = "large", state = mockState, dispatch = jest.fn()) {
  const { container, unmount, rerender } = render(
    <WrapWithProvider state={state}>
      <Clocks dispatch={dispatch} size={size} />
    </WrapWithProvider>
  );
  return { container, unmount, rerender, dispatch };
}

const withClockZones = zones => ({
  ...mockState,
  Prefs: {
    ...mockState.Prefs,
    values: {
      ...mockState.Prefs.values,
      "widgets.clocks.zones": JSON.stringify(zones),
    },
  },
});

describe("<Clocks> (Widgets/Clocks)", () => {
  describe("rendering", () => {
    it("renders exactly four clock rows (hard-coded default set)", () => {
      const { container } = renderClocks();
      expect(container.querySelectorAll(".clocks-row")).toHaveLength(4);
    });

    it("shows the add button when fewer than four clocks are saved", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );

      expect(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      ).toBeInTheDocument();
    });

    it("hides the add button when four clocks are already saved", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "Australia/Sydney", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
          { timeZone: "America/Los_Angeles", label: null, labelColor: null },
        ])
      );

      expect(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-menu-button']"
        )
      ).toBeInTheDocument();
    });

    it("renders the default cities in order (Berlin, Sydney, New York, Los Angeles)", () => {
      const { container } = renderClocks();
      const rows = container.querySelectorAll(".clocks-row");
      const cities = Array.from(rows).map(
        r => r.querySelector(".clocks-city").textContent
      );
      expect(cities).toEqual(["Berlin", "Sydney", "New York", "Los Angeles"]);
    });

    it("renders saved clock zones in pref order and preserves duplicate zones", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.clocks.zones": JSON.stringify([
              {
                timeZone: "America/New_York",
                city: "Boston",
                label: "Office",
                labelColor: "cyan",
              },
              {
                timeZone: "America/New_York",
                city: "New York",
                label: "Family",
                labelColor: "green",
              },
            ]),
          },
        },
      };
      const { container } = renderClocks("large", state);
      const cities = Array.from(container.querySelectorAll(".clocks-city")).map(
        el => el.textContent
      );
      const labels = Array.from(
        container.querySelectorAll(".clocks-label-chip")
      ).map(el => el.textContent);

      expect(cities).toEqual(["Boston", "New York"]);
      expect(labels).toEqual(["Office", "Family"]);
    });

    it("backfills missing label colors for saved nickname clocks", () => {
      const savedZones = [
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: null,
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: null,
          labelColor: null,
        },
      ];
      const { dispatch } = renderClocks("large", withClockZones(savedZones));
      const setPrefCall = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.SET_PREF &&
          action.data?.name === "widgets.clocks.zones"
      )?.[0];

      expect(setPrefCall).toEqual(
        expect.objectContaining({
          type: at.SET_PREF,
          data: expect.objectContaining({
            name: "widgets.clocks.zones",
          }),
        })
      );

      const persistedZones = JSON.parse(setPrefCall.data.value);
      expect(persistedZones[0]).toMatchObject({
        timeZone: "America/New_York",
        city: "Boston",
        label: "Office",
      });
      expect(isValidPaletteName(persistedZones[0].labelColor)).toBe(true);
      expect(persistedZones[1]).toEqual(savedZones[1]);
    });

    it("does not dispatch a backfill SET_PREF when every labeled clock already has a color", () => {
      // Guards against a future refactor accidentally re-firing the
      // backfill on every render.
      const savedZones = [
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: null,
          labelColor: null,
        },
      ];
      const { dispatch } = renderClocks("large", withClockZones(savedZones));
      const zonesPrefCall = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.SET_PREF &&
          action.data?.name === "widgets.clocks.zones"
      );
      expect(zonesPrefCall).toBeUndefined();
    });

    it("falls back to default clock zones when the saved pref is invalid", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.clocks.zones": "{",
          },
        },
      };
      const { container } = renderClocks("large", state);
      const cities = Array.from(container.querySelectorAll(".clocks-city")).map(
        el => el.textContent
      );

      expect(cities).toEqual(["Berlin", "Sydney", "New York", "Los Angeles"]);
    });

    it("applies the size-specific class to the article root", () => {
      expect(
        renderClocks("small").container.querySelector(
          ".clocks-widget.small-widget"
        )
      ).toBeInTheDocument();
      expect(
        renderClocks("medium").container.querySelector(
          ".clocks-widget.medium-widget"
        )
      ).toBeInTheDocument();
      expect(
        renderClocks("large").container.querySelector(
          ".clocks-widget.large-widget"
        )
      ).toBeInTheDocument();
    });

    it("defaults to medium size when the size prop is falsy", () => {
      // Pass null rather than undefined — undefined would let the renderClocks
      // default ("large") kick in, which is not what we want to test.
      const { container } = renderClocks(null);
      expect(
        container.querySelector(".clocks-widget.medium-widget")
      ).toBeInTheDocument();
    });

    it("renders IATA abbreviations in small and medium sizes", () => {
      const smallCities = Array.from(
        renderClocks("small").container.querySelectorAll(".clocks-city")
      ).map(el => el.textContent);
      expect(smallCities).toEqual(["BER", "SYD", "NYC", "LAX"]);

      const mediumCities = Array.from(
        renderClocks("medium").container.querySelectorAll(".clocks-city")
      ).map(el => el.textContent);
      expect(mediumCities).toEqual(["BER", "SYD", "NYC", "LAX"]);
    });

    it("renders label chips only in Large size", () => {
      const labeledZones = withClockZones([
        { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
        { timeZone: "Australia/Sydney", label: "Work", labelColor: "green" },
        { timeZone: "America/New_York", label: "NYC", labelColor: "yellow" },
        { timeZone: "America/Los_Angeles", label: "LA", labelColor: "purple" },
      ]);
      const large = renderClocks("large", labeledZones).container;
      expect(large.querySelectorAll(".clocks-label-chip").length).toBe(4);

      expect(
        renderClocks("small", labeledZones).container.querySelectorAll(
          ".clocks-label-chip"
        ).length
      ).toBe(0);
      expect(
        renderClocks("medium", labeledZones).container.querySelectorAll(
          ".clocks-label-chip"
        ).length
      ).toBe(0);
    });

    it("applies saved palette colors to label chips", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
          { timeZone: "Australia/Sydney", label: "Work", labelColor: "green" },
          { timeZone: "America/New_York", label: "NYC", labelColor: "yellow" },
          {
            timeZone: "America/Los_Angeles",
            label: "LA",
            labelColor: "purple",
          },
        ])
      );
      const chips = Array.from(
        container.querySelectorAll(".clocks-label-chip")
      );
      const paletteClasses = [];
      for (const el of chips) {
        const match = Array.from(el.classList).find(c =>
          c.startsWith("clocks-chip-")
        );
        paletteClasses.push(match);
      }
      expect(paletteClasses).toEqual([
        "clocks-chip-cyan",
        "clocks-chip-green",
        "clocks-chip-yellow",
        "clocks-chip-purple",
      ]);
    });

    it("sets an aria-label on each clock row with full city + TZ + time", () => {
      jest.useFakeTimers();
      jest.setSystemTime(new Date("2026-04-20T13:44:00Z"));
      try {
        const { container } = renderClocks("small");
        const rows = container.querySelectorAll(".clocks-row");
        // The UI abbreviates the city in Small size ("BER") but the aria-label
        // always uses the full city name for screen readers.
        expect(rows[0].getAttribute("aria-label")).toMatch(/^Berlin, /);
        expect(rows[3].getAttribute("aria-label")).toMatch(/^Los Angeles, /);
      } finally {
        jest.useRealTimers();
      }
    });

    it("prefixes the label in the aria-label for large-size labeled clocks", () => {
      jest.useFakeTimers();
      jest.setSystemTime(new Date("2026-04-20T13:44:00Z"));
      try {
        const { container } = renderClocks(
          "large",
          withClockZones([
            { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
            {
              timeZone: "America/Los_Angeles",
              label: "Family",
              labelColor: "green",
            },
          ])
        );
        const rows = container.querySelectorAll(".clocks-row");
        expect(rows[0].getAttribute("aria-label")).toMatch(/^Home, Berlin, /);
        expect(rows[1].getAttribute("aria-label")).toMatch(
          /^Family, Los Angeles, /
        );
      } finally {
        jest.useRealTimers();
      }
    });
  });

  describe("live time", () => {
    beforeEach(() => {
      jest.useFakeTimers();
      jest.setSystemTime(new Date("2026-04-20T13:44:00Z"));
    });

    afterEach(() => {
      jest.useRealTimers();
    });

    it("advances the displayed time on each minute boundary (self-rescheduling timeout)", () => {
      const { container } = renderClocks();
      // Extract just the minute portion — the hour differs per zone and per
      // locale's 12/24h default, but the minute value is the same across all
      // four clocks and is what this test actually cares about.
      const minutes = () =>
        Array.from(container.querySelectorAll(".clocks-time")).map(
          el => el.textContent.match(/:(\d{2})/)?.[1]
        );

      // Initial tick fires synchronously inside the useEffect; system time is
      // pinned to 13:44:00Z in beforeEach.
      expect(minutes()).toEqual(["44", "44", "44", "44"]);

      act(() => {
        jest.advanceTimersByTime(60_000);
      });
      expect(minutes()).toEqual(["45", "45", "45", "45"]);

      // The second advance only changes the display if the first tick
      // rescheduled itself. A broken one-shot setTimeout would leave the
      // widget frozen at :45.
      act(() => {
        jest.advanceTimersByTime(60_000);
      });
      expect(minutes()).toEqual(["46", "46", "46", "46"]);
    });

    it("has no pending timers after unmount", () => {
      const { unmount } = renderClocks();
      expect(jest.getTimerCount()).toBeGreaterThan(0);
      unmount();
      expect(jest.getTimerCount()).toBe(0);
    });
  });

  describe("impression telemetry", () => {
    let originalIntersectionObserver;
    let lastCallback;

    beforeEach(() => {
      originalIntersectionObserver = globalThis.IntersectionObserver;
      globalThis.IntersectionObserver = class {
        constructor(cb) {
          lastCallback = cb;
        }
        observe() {}
        unobserve() {}
        disconnect() {}
      };
    });

    afterEach(() => {
      globalThis.IntersectionObserver = originalIntersectionObserver;
      lastCallback = undefined;
    });

    it("fires WIDGETS_IMPRESSION once when the widget intersects", () => {
      const { container, dispatch } = renderClocks("large");
      const widget = container.querySelector(".clocks-widget");
      act(() => {
        lastCallback([{ target: widget, isIntersecting: true }]);
      });
      const impressions = dispatch.mock.calls.filter(
        ([a]) => a.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
      expect(impressions[0][0]).toMatchObject({
        type: at.WIDGETS_IMPRESSION,
        data: { widget_name: "clocks", widget_size: "large" },
      });
    });

    it("does not fire WIDGETS_IMPRESSION on subsequent intersections", () => {
      const { container, dispatch } = renderClocks();
      const widget = container.querySelector(".clocks-widget");
      act(() => {
        lastCallback([{ target: widget, isIntersecting: true }]);
        lastCallback([{ target: widget, isIntersecting: true }]);
      });
      const impressions = dispatch.mock.calls.filter(
        ([a]) => a.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
    });

    it("does not fire when isIntersecting is false", () => {
      const { container, dispatch } = renderClocks();
      const widget = container.querySelector(".clocks-widget");
      act(() => {
        lastCallback([{ target: widget, isIntersecting: false }]);
      });
      const impressions = dispatch.mock.calls.filter(
        ([a]) => a.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(0);
    });
  });

  describe("context menu", () => {
    it("renders the context menu button with the clock-specific a11y label", () => {
      const { container } = renderClocks();
      expect(
        container.querySelector(
          ".clocks-context-menu-button[data-l10n-id='newtab-clock-widget-menu-button']"
        )
      ).toBeInTheDocument();
    });

    it("contains Change size submenu with small, medium, large items", () => {
      const { container } = renderClocks();
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
      ["small", "medium", "large"].forEach(s => {
        expect(
          container.querySelector(
            `panel-item[data-l10n-id='newtab-widget-size-${s}']`
          )
        ).toBeInTheDocument();
      });
    });

    it("checks the current size in the submenu", () => {
      const { container } = renderClocks("large");
      expect(
        container
          .querySelector("panel-item[data-l10n-id='newtab-widget-size-large']")
          .hasAttribute("checked")
      ).toBe(true);
      expect(
        container
          .querySelector("panel-item[data-l10n-id='newtab-widget-size-small']")
          .hasAttribute("checked")
      ).toBe(false);
    });

    it("contains hide (singular 'Hide clock') and learn-more items", () => {
      const { container } = renderClocks();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-hide']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-learn-more']"
        )
      ).toBeInTheDocument();
    });

    it("clears is-dismissed class when the mouse leaves the widget", () => {
      const { container } = renderClocks();
      const widget = container.querySelector(".clocks-widget");
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-clock-widget-menu-hide']"
      );
      fireEvent.click(item);
      expect(widget.classList.contains("is-dismissed")).toBe(true);
      fireEvent.mouseLeave(widget);
      expect(widget.classList.contains("is-dismissed")).toBe(false);
    });
  });

  describe("context menu actions & telemetry", () => {
    it("dispatches SET_PREF(widgets.clocks.size) and WIDGETS_USER_EVENT on submenu size click", () => {
      const { container, dispatch } = renderClocks();
      const submenuNode = container.querySelector(
        "panel-list[id='clocks-size-submenu']"
      );
      const mockItem = document.createElement("div");
      mockItem.dataset.size = "small";
      const event = new MouseEvent("click", { bubbles: true });
      Object.defineProperty(event, "composedPath", {
        value: () => [mockItem],
      });
      act(() => {
        submenuNode.dispatchEvent(event);
      });

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "widgets.clocks.size", value: "small" },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "clocks",
          widget_source: "context_menu",
          user_action: "change_size",
          action_value: "small",
          widget_size: "small",
        }),
      });
    });

    it("dispatches SET_PREF(widgets.clocks.enabled, false) and WIDGETS_ENABLED on hide click", () => {
      const { container, dispatch } = renderClocks();
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-clock-widget-menu-hide']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "widgets.clocks.enabled", value: false },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_ENABLED,
        data: expect.objectContaining({
          widget_name: "clocks",
          widget_source: "context_menu",
          enabled: false,
          widget_size: "large",
        }),
      });
    });

    it("dispatches OPEN_LINK and WIDGETS_USER_EVENT on learn-more click", () => {
      const { container, dispatch } = renderClocks();
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-clock-widget-menu-learn-more']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.OPEN_LINK,
        data: {
          url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
        },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "clocks",
          widget_source: "context_menu",
          user_action: "learn_more",
          widget_size: "large",
        }),
      });
    });
  });

  describe("add clock flow", () => {
    it("hides the '+' button at the default 4-clock max", () => {
      const { container } = renderClocks();
      const addButton = container.querySelector(
        "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
      );
      expect(addButton).not.toBeInTheDocument();
    });

    it("opens the add clock form when fewer than four clocks are saved", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      const addButton = container.querySelector(
        "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
      );

      expect(addButton.hasAttribute("disabled")).toBe(false);
      fireEvent.click(addButton);

      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
      expect(
        container.querySelector(
          "moz-input-search[data-l10n-id='newtab-clock-widget-search-location-input']"
        )
      ).toBeInTheDocument();
    });

    it("shows filtered location results when a search query is entered", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Ber",
      });
      fireEvent.input(searchInput);
      expect(
        container.querySelector(
          ".clocks-search-result .clocks-search-result-city"
        )
      ).toBeInTheDocument();
    });

    it("dispatches SET_PREF when an exact city name is entered and Add is clicked", () => {
      const savedZones = [
        { timeZone: "Australia/Sydney", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: {
            name: "widgets.clocks.zones",
            value: JSON.stringify([
              ...savedZones,
              {
                timeZone: "Europe/Berlin",
                city: "Berlin",
                label: null,
                labelColor: null,
              },
            ]),
          },
        })
      );
    });

    it("dispatches WIDGETS_USER_EVENT with add_clock when saving a new clock", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Australia/Sydney", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "toolbar",
            user_action: "add_clock",
            widget_size: "large",
          }),
        })
      );
    });

    it("filters results and dispatches SET_PREF when a result then Add is clicked", () => {
      const savedZones = [
        { timeZone: "Europe/Berlin", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      // moz-input-search is a custom element without a native value setter;
      // define value as a writable own property so React can also update it.
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Syd",
      });
      fireEvent.input(searchInput);

      const sydneyButton = Array.from(
        container.querySelectorAll(".clocks-search-result")
      ).find(
        el =>
          el.querySelector(".clocks-search-result-city")?.textContent ===
          "Sydney"
      );
      expect(sydneyButton).toBeInTheDocument();

      // Clicking a result selects the location; clicking Add clock saves it.
      fireEvent.click(sydneyButton);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: {
            name: "widgets.clocks.zones",
            value: JSON.stringify([
              ...savedZones,
              {
                timeZone: "Australia/Sydney",
                city: "Sydney",
                label: null,
                labelColor: null,
              },
            ]),
          },
        })
      );
    });

    it("closes the form after adding a clock", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      const addButton = container.querySelector(
        "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
      );
      fireEvent.click(addButton);
      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();

      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);
      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));
      expect(
        container.querySelector(".clocks-add-form")
      ).not.toBeInTheDocument();
    });

    it("Add clock button inside the form is disabled until a location is selected", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      const addClockButton = container.querySelector(
        "moz-button.clocks-form-submit"
      );
      expect(addClockButton.hasAttribute("disabled")).toBe(true);

      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);

      expect(addClockButton.hasAttribute("disabled")).toBe(false);
    });

    it("includes the nickname as label when one is entered", () => {
      const savedZones = [
        { timeZone: "Europe/Berlin", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Syd",
      });
      fireEvent.input(searchInput);
      fireEvent.click(
        Array.from(container.querySelectorAll(".clocks-search-result")).find(
          el =>
            el.querySelector(".clocks-search-result-city")?.textContent ===
            "Sydney"
        )
      );

      const nicknameInput = container.querySelector(".clocks-nickname-input");
      Object.defineProperty(nicknameInput, "value", {
        configurable: true,
        writable: true,
        value: "Work",
      });
      fireEvent.input(nicknameInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: {
            name: "widgets.clocks.zones",
            value: JSON.stringify([
              ...savedZones,
              {
                timeZone: "Australia/Sydney",
                city: "Sydney",
                label: "Work",
                labelColor: "cyan",
              },
            ]),
          },
        })
      );
    });

    it("dispatches WIDGETS_USER_EVENT with add_nickname when saving a clock with a nickname", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Syd",
      });
      fireEvent.input(searchInput);
      fireEvent.click(
        Array.from(container.querySelectorAll(".clocks-search-result")).find(
          el =>
            el.querySelector(".clocks-search-result-city")?.textContent ===
            "Sydney"
        )
      );
      const nicknameInput = container.querySelector(".clocks-nickname-input");
      Object.defineProperty(nicknameInput, "value", {
        configurable: true,
        writable: true,
        value: "Work",
      });
      fireEvent.input(nicknameInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "toolbar",
            user_action: "add_nickname",
            widget_size: "large",
          }),
        })
      );
    });

    it("does not dispatch add_nickname when saving a clock without a nickname", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Australia/Sydney", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      const nicknameEvents = dispatch.mock.calls.filter(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "add_nickname"
      );
      expect(nicknameEvents).toHaveLength(0);
    });

    it("does not dispatch add_nickname when editing a clock that already has a label", () => {
      const savedZones = [
        {
          timeZone: "Europe/Berlin",
          label: "Home",
          labelColor: "cyan",
        },
        {
          timeZone: "America/New_York",
          label: null,
          labelColor: null,
        },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.click(container.querySelector(".clocks-edit-item-edit-button"));
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      const nicknameEvents = dispatch.mock.calls.filter(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "add_nickname"
      );
      expect(nicknameEvents).toHaveLength(0);
    });

    it("dispatches add_nickname when editing an unlabeled clock and adding a label", () => {
      const savedZones = [
        { timeZone: "Europe/Berlin", label: null, labelColor: null },
        { timeZone: "America/New_York", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.click(container.querySelector(".clocks-edit-item-edit-button"));

      const nicknameInput = container.querySelector(".clocks-nickname-input");
      Object.defineProperty(nicknameInput, "value", {
        configurable: true,
        writable: true,
        value: "Work",
      });
      fireEvent.input(nicknameInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      const nicknameEvents = dispatch.mock.calls.filter(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "add_nickname"
      );
      expect(nicknameEvents).toHaveLength(1);
      expect(nicknameEvents[0][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "clocks",
          widget_source: "manage",
          user_action: "add_nickname",
          widget_size: "large",
        }),
      });
    });

    it("limits nickname labels to 11 characters", () => {
      const savedZones = [
        { timeZone: "Europe/Berlin", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Syd",
      });
      fireEvent.input(searchInput);
      fireEvent.click(
        Array.from(container.querySelectorAll(".clocks-search-result")).find(
          el =>
            el.querySelector(".clocks-search-result-city")?.textContent ===
            "Sydney"
        )
      );

      const nicknameInput = container.querySelector(".clocks-nickname-input");
      Object.defineProperty(nicknameInput, "value", {
        configurable: true,
        writable: true,
        value: "Very Long Work Label",
      });
      fireEvent.input(nicknameInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: {
            name: "widgets.clocks.zones",
            value: JSON.stringify([
              ...savedZones,
              {
                timeZone: "Australia/Sydney",
                city: "Sydney",
                label: "Very Long W",
                labelColor: "cyan",
              },
            ]),
          },
        })
      );
    });

    it("Cancel button closes the form without saving", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-cancel']"
        )
      );

      expect(
        container.querySelector(".clocks-add-form")
      ).not.toBeInTheDocument();
      expect(dispatch).not.toHaveBeenCalledWith(
        expect.objectContaining({ type: at.SET_PREF })
      );
    });

    it("pressing Enter on Cancel does not save the form", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);

      dispatch.mockClear();
      fireEvent.keyDown(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-cancel']"
        ),
        { key: "Enter" }
      );

      expect(dispatch).not.toHaveBeenCalledWith(
        expect.objectContaining({ type: at.SET_PREF })
      );
      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
    });

    it("keeps the form open when blur has no next focused element", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      fireEvent.blur(container.querySelector(".clocks-add-form"), {
        relatedTarget: null,
      });

      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
    });

    it("closes the form on Escape", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      fireEvent.keyDown(container.querySelector(".clocks-add-form"), {
        key: "Escape",
      });

      expect(
        container.querySelector(".clocks-add-form")
      ).not.toBeInTheDocument();
    });

    it("pressing Enter in the form saves the clock without clicking Add", () => {
      const savedZones = [
        { timeZone: "Europe/Berlin", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);

      fireEvent.keyDown(container.querySelector(".clocks-add-form"), {
        key: "Enter",
      });

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: expect.objectContaining({ name: "widgets.clocks.zones" }),
        })
      );
      expect(
        container.querySelector(".clocks-add-form")
      ).not.toBeInTheDocument();
    });

    it("expands a small widget to large while the add panel is open", () => {
      const { container } = renderClocks(
        "small",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );

      expect(
        container.querySelector(".clocks-widget.small-widget")
      ).toBeInTheDocument();

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-add']"
        )
      );

      expect(
        container.querySelector(
          ".clocks-widget.large-widget.is-clock-form-open"
        )
      ).toBeInTheDocument();

      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-cancel']"
        )
      );

      expect(
        container.querySelector(".clocks-widget.small-widget")
      ).toBeInTheDocument();
    });

    it("Edit clocks opens the manage view instead of the add form", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          {
            timeZone: "America/New_York",
            city: "Boston",
            label: "Office",
            labelColor: "cyan",
          },
          {
            timeZone: "Europe/Berlin",
            city: "Berlin",
            label: null,
            labelColor: null,
          },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );

      expect(container.querySelector(".clocks-edit-panel")).toBeInTheDocument();
      expect(
        container.querySelector(".clocks-add-form")
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(".clocks-edit-back-button")
      ).toBeInTheDocument();
      expect(container.querySelectorAll(".clocks-edit-item")).toHaveLength(2);
    });

    it("dispatches WIDGETS_USER_EVENT with expand when the edit panel opens", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "context_menu",
            user_action: "expand",
            widget_size: "large",
          }),
        })
      );
    });

    it("dispatches WIDGETS_USER_EVENT with collapse when the back button closes the edit panel", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      dispatch.mockClear();

      fireEvent.click(
        container.querySelector(
          ".clocks-edit-panel moz-button.clocks-edit-back-button"
        )
      );

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "context_menu",
            user_action: "collapse",
            widget_size: "large",
          }),
        })
      );
    });

    it("preserves the panel open source across an in-panel form save when emitting collapse", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Australia/Sydney", label: null, labelColor: null },
        ])
      );

      // Open the edit (manage) panel via the context menu.
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      // Open the add form from inside the manage panel.
      fireEvent.click(container.querySelector(".clocks-edit-add-button"));
      // Save a new clock from the in-panel form.
      const searchInput = container.querySelector(
        ".clocks-search-location-input"
      );
      Object.defineProperty(searchInput, "value", {
        configurable: true,
        writable: true,
        value: "Berlin",
      });
      fireEvent.input(searchInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));
      dispatch.mockClear();

      // Close the manage panel via the back button.
      fireEvent.click(
        container.querySelector(
          ".clocks-edit-panel moz-button.clocks-edit-back-button"
        )
      );

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "context_menu",
            user_action: "collapse",
            widget_size: "large",
          }),
        })
      );
    });

    it("closes the edit panel on Escape", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.keyDown(container.querySelector(".clocks-edit-panel"), {
        key: "Escape",
      });

      expect(
        container.querySelector(".clocks-edit-panel")
      ).not.toBeInTheDocument();
    });

    it("each clock item in the edit panel has tabIndex=0 so keyboard focus reveals its action buttons", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );

      const editItems = container.querySelectorAll(".clocks-edit-item");
      expect(editItems.length).toBeGreaterThan(0);
      editItems.forEach(item => {
        expect(item.getAttribute("tabindex")).toBe("0");
      });
    });

    it("expands a medium widget to large while the edit panel is open", () => {
      const { container } = renderClocks(
        "medium",
        withClockZones([
          {
            timeZone: "America/New_York",
            city: "Boston",
            label: "Office",
            labelColor: "cyan",
          },
        ])
      );

      expect(
        container.querySelector(".clocks-widget.medium-widget")
      ).toBeInTheDocument();

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );

      expect(
        container.querySelector(".clocks-widget.large-widget.is-editing-clocks")
      ).toBeInTheDocument();

      fireEvent.click(
        container.querySelector(
          ".clocks-edit-panel moz-button.clocks-edit-back-button"
        )
      );

      expect(
        container.querySelector(".clocks-widget.medium-widget")
      ).toBeInTheDocument();
    });

    it("shows an add button in the edit view when more clocks can be added", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );

      expect(
        container.querySelector(".clocks-edit-header .clocks-edit-add-button")
      ).toBeInTheDocument();
    });

    it("opens the clock form in Save mode from the manage view and updates the clock", () => {
      const savedZones = [
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: null,
          labelColor: null,
        },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.click(container.querySelector(".clocks-edit-item-edit-button"));

      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
      expect(
        container.querySelector("moz-button.clocks-form-submit")
      ).toBeInTheDocument();

      const nicknameInput = container.querySelector(".clocks-nickname-input");
      Object.defineProperty(nicknameInput, "value", {
        configurable: true,
        writable: true,
        value: "HQ",
      });
      fireEvent.input(nicknameInput);
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: {
            name: "widgets.clocks.zones",
            value: JSON.stringify([
              {
                timeZone: "America/New_York",
                city: "Boston",
                label: "HQ",
                labelColor: "cyan",
              },
              savedZones[1],
            ]),
          },
        })
      );
      expect(container.querySelector(".clocks-edit-panel")).toBeInTheDocument();
    });

    it("dispatches WIDGETS_USER_EVENT with edit_clock when saving an edited clock", () => {
      const savedZones = [
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: null,
          labelColor: null,
        },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.click(container.querySelector(".clocks-edit-item-edit-button"));
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "manage",
            user_action: "edit_clock",
            widget_size: "large",
          }),
        })
      );
    });

    it("removes a clock from the manage view while keeping the panel open", () => {
      const savedZones = [
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: null,
          labelColor: null,
        },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.click(
        container.querySelector(".clocks-edit-item-remove-button")
      );

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: {
            name: "widgets.clocks.zones",
            value: JSON.stringify([savedZones[1]]),
          },
        })
      );
      expect(container.querySelector(".clocks-edit-panel")).toBeInTheDocument();
    });

    it("dispatches WIDGETS_USER_EVENT with remove_clock when removing a clock from the manage view", () => {
      const savedZones = [
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: null,
          labelColor: null,
        },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-edit']"
        )
      );
      fireEvent.click(
        container.querySelector(".clocks-edit-item-remove-button")
      );

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "manage",
            user_action: "remove_clock",
            widget_size: "large",
          }),
        })
      );
    });

    it("dispatches WIDGETS_USER_EVENT with remove_clock and widget_source 'row' when using the inline remove button", () => {
      const savedZones = [
        { timeZone: "Europe/Berlin", label: null, labelColor: null },
        { timeZone: "America/New_York", label: null, labelColor: null },
      ];
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones(savedZones)
      );

      fireEvent.click(container.querySelector(".clocks-row-remove-button"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "row",
            user_action: "remove_clock",
            widget_size: "large",
          }),
        })
      );
    });

    it("shows only the inline edit action when one clock is visible", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          {
            timeZone: "Europe/Berlin",
            city: "Berlin",
            label: null,
            labelColor: null,
          },
        ])
      );

      expect(
        container.querySelector(".clocks-row-edit-button")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".clocks-row-remove-button")
      ).not.toBeInTheDocument();
    });

    it("does not render inline row actions in the small widget", () => {
      const { container } = renderClocks(
        "small",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );

      expect(
        container.querySelector(".clocks-row-edit-button")
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(".clocks-row-remove-button")
      ).not.toBeInTheDocument();
    });

    it("clicking the inline row edit button opens the clock form", () => {
      const { container } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );

      fireEvent.click(container.querySelector(".clocks-row-edit-button"));

      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
      expect(
        container.querySelector("moz-button.clocks-form-submit")
      ).toBeInTheDocument();
    });

    it("dispatches WIDGETS_USER_EVENT with edit_clock and widget_source 'row' when saving after inline row edit", () => {
      const { container, dispatch } = renderClocks(
        "large",
        withClockZones([
          { timeZone: "Europe/Berlin", label: null, labelColor: null },
          { timeZone: "America/New_York", label: null, labelColor: null },
        ])
      );

      fireEvent.click(container.querySelector(".clocks-row-edit-button"));
      fireEvent.click(container.querySelector("moz-button.clocks-form-submit"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_USER_EVENT,
          data: expect.objectContaining({
            widget_name: "clocks",
            widget_source: "row",
            user_action: "edit_clock",
            widget_size: "large",
          }),
        })
      );
    });
  });

  describe("hour format toggle", () => {
    function renderWithHourFormatPref(prefValue) {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.clocks.hourFormat": prefValue,
          },
        },
      };
      return renderClocks("large", state);
    }

    it("shows 'Switch to 24h' when pref is '12'", () => {
      const { container } = renderWithHourFormatPref("12");
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-switch-to-24h']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-switch-to-12h']"
        )
      ).not.toBeInTheDocument();
    });

    it("shows 'Switch to 12h' when pref is '24'", () => {
      const { container } = renderWithHourFormatPref("24");
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-switch-to-12h']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-clock-widget-menu-switch-to-24h']"
        )
      ).not.toBeInTheDocument();
    });

    it("flips the pref and fires WIDGETS_USER_EVENT on toggle click (12 -> 24)", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.clocks.hourFormat": "12",
          },
        },
      };
      const { container, dispatch } = renderClocks("large", state);
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-clock-widget-menu-switch-to-24h']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "widgets.clocks.hourFormat", value: "24" },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "clocks",
          widget_source: "context_menu",
          user_action: "change_hour_format",
          action_value: "24",
          widget_size: "large",
        }),
      });
    });
  });
});
