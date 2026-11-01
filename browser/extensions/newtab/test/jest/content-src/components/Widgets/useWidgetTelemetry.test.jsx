/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render } from "@testing-library/react";
import { actionTypes as at } from "common/Actions.mjs";
import { useWidgetTelemetry } from "content-src/components/Widgets/useWidgetTelemetry";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";
import { WIDGET_PLACEMENTS } from "common/PageLayoutVariants.mjs";
import {
  WIDGET_LOCATIONS,
  WidgetLocationContext,
  WidgetPlacementProvider,
} from "content-src/components/Widgets/WidgetPlacement";

const WEATHER_WIDGET = { id: "weather", telemetryName: "weather" };
const FOCUS_TIMER_WIDGET = { id: "focusTimer", telemetryName: "focus_timer" };
const LISTS_WIDGET = { id: "lists", telemetryName: "lists" };

function TestComponent({
  dispatch,
  widget,
  widgetSize,
  widgetVariant,
  legacyImpressionTypes,
  legacyUserEventType,
  showEl = true,
  onRender,
}) {
  const telemetry = useWidgetTelemetry({
    dispatch,
    widget,
    widgetSize,
    widgetVariant,
    legacyImpressionTypes,
    legacyUserEventType,
  });
  onRender(telemetry);
  return showEl ? <div ref={telemetry.impressionRef} /> : null;
}

describe("useWidgetTelemetry", () => {
  let dispatch;
  let observerStub;

  beforeEach(() => {
    dispatch = jest.fn();
    observerStub = jest
      .spyOn(window, "IntersectionObserver")
      .mockImplementation(function (cb) {
        this.observe = jest.fn();
        this.unobserve = jest.fn();
        this.disconnect = jest.fn();
        this.callback = cb;
      });
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  describe("impression observer", () => {
    it("dispatches WIDGETS_IMPRESSION once when element intersects", () => {
      const { container } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={() => {}}
        />
      );
      const [observerInstance] = observerStub.mock.instances;
      const el = container.querySelector("div");

      observerInstance.callback([{ isIntersecting: true, target: el }]);

      expect(dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.WIDGETS_IMPRESSION);
      expect(action.data).toEqual({
        widget_name: "weather",
        widget_size: "medium",
      });
    });

    it("does not observe until the enclosing space is active", () => {
      const tree = spaceActive => (
        <SpaceActiveContext.Provider value={spaceActive}>
          <TestComponent
            dispatch={dispatch}
            widget={WEATHER_WIDGET}
            widgetSize="medium"
            onRender={() => {}}
          />
        </SpaceActiveContext.Provider>
      );
      const { container, rerender } = render(tree(false));
      expect(observerStub).not.toHaveBeenCalled();

      rerender(tree(true));
      const [observerInstance] = observerStub.mock.instances;
      expect(observerInstance.observe).toHaveBeenCalledWith(
        container.querySelector("div")
      );
    });

    it("does not dispatch a second impression after the first", () => {
      const { container } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={() => {}}
        />
      );
      const [observerInstance] = observerStub.mock.instances;
      const el = container.querySelector("div");

      observerInstance.callback([{ isIntersecting: true, target: el }]);
      observerInstance.callback([{ isIntersecting: true, target: el }]);

      expect(dispatch).toHaveBeenCalledTimes(1);
    });

    it("observes an element that mounts after the initial render", () => {
      const { container, rerender } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={false}
          onRender={() => {}}
        />
      );
      const [observerInstance] = observerStub.mock.instances;
      // Initial render: no element, observer didn't observe anything yet.
      expect(observerInstance.observe).not.toHaveBeenCalled();

      // Later render reveals the element; the callback ref should observe it.
      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={true}
          onRender={() => {}}
        />
      );
      expect(observerInstance.observe).toHaveBeenCalledTimes(1);

      const el = container.querySelector("div");
      observerInstance.callback([{ isIntersecting: true, target: el }]);
      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch.mock.calls[0][0].type).toBe(at.WIDGETS_IMPRESSION);
    });

    it("ignores a queued intersection entry for a previously-unobserved target", () => {
      const { container, rerender } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={() => {}}
        />
      );
      const [observerInstance] = observerStub.mock.instances;
      const firstEl = container.querySelector("div");

      // Reassign the ref to a different node; firstEl is no longer the
      // observed target.
      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={false}
          onRender={() => {}}
        />
      );
      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={true}
          onRender={() => {}}
        />
      );

      // A queued callback for the old target fires; hook must not dispatch.
      observerInstance.callback([{ isIntersecting: true, target: firstEl }]);

      expect(dispatch).not.toHaveBeenCalled();
    });

    it("unobserves the previous element when the ref is reassigned", () => {
      const { container, rerender } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={() => {}}
        />
      );
      const [observerInstance] = observerStub.mock.instances;
      const firstEl = container.querySelector("div");
      expect(observerInstance.observe).toHaveBeenCalledTimes(1);
      expect(observerInstance.observe).toHaveBeenCalledWith(firstEl);

      // Force the element to remount with a different node.
      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={false}
          onRender={() => {}}
        />
      );
      expect(observerInstance.unobserve).toHaveBeenCalledTimes(1);
      expect(observerInstance.unobserve).toHaveBeenCalledWith(firstEl);

      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={true}
          onRender={() => {}}
        />
      );
      const secondEl = container.querySelector("div");
      expect(observerInstance.observe).toHaveBeenCalledTimes(2);
      expect(observerInstance.observe.mock.lastCall[0]).toBe(secondEl);
    });
  });

  describe("recordImpression", () => {
    it("dispatches WIDGETS_IMPRESSION manually with the current size", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={false}
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordImpression();

      expect(dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.WIDGETS_IMPRESSION);
      expect(action.data).toEqual({
        widget_name: "weather",
        widget_size: "medium",
      });
    });

    it("shares the impressionFired guard with the observer", () => {
      let telemetry;
      const { container } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );
      const [observerInstance] = observerStub.mock.instances;
      const el = container.querySelector("div");

      observerInstance.callback([{ isIntersecting: true, target: el }]);
      telemetry.recordImpression();

      expect(dispatch).toHaveBeenCalledTimes(1);
    });

    it("honors per-call size override", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          showEl={false}
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordImpression({ size: "large" });

      expect(dispatch.mock.calls[0][0].data.widget_size).toBe("large");
    });
  });

  describe("recordUserAction", () => {
    it("dispatches WIDGETS_USER_EVENT via OnlyToMain by default", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="small"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordUserAction("learn_more", { source: "context_menu" });

      expect(dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.WIDGETS_USER_EVENT);
      expect(action.meta.to).toBe("ActivityStream:Main");
      expect(action.meta.skipLocal).toBe(true);
      expect(action.data).toEqual({
        widget_name: "weather",
        widget_size: "small",
        widget_source: "context_menu",
        user_action: "learn_more",
      });
    });

    it("dispatches via AlsoToMain when alsoToMain: true", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={LISTS_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordUserAction("task_complete", {
        source: "widget",
        alsoToMain: true,
      });

      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.WIDGETS_USER_EVENT);
      expect(action.meta.to).toBe("ActivityStream:Main");
      expect(action.meta.skipLocal).not.toBe(true);
    });

    it("includes action_value when value is provided", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordUserAction("change_temperature_units", {
        source: "context_menu",
        value: "c",
      });

      expect(dispatch.mock.calls[0][0].data.action_value).toBe("c");
    });

    it("omits action_value when value is not provided", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordUserAction("learn_more", { source: "context_menu" });

      expect(dispatch.mock.calls[0][0].data).not.toHaveProperty("action_value");
    });

    it("reads the latest widget_size after a prop change", () => {
      let telemetry;
      const { rerender } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="small"
          onRender={t => (telemetry = t)}
        />
      );

      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="large"
          onRender={t => (telemetry = t)}
        />
      );
      telemetry.recordUserAction("provider_link_click", { source: "widget" });

      expect(dispatch.mock.calls[0][0].data.widget_size).toBe("large");
    });

    it("honors an explicit size override", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordUserAction("change_size", {
        source: "context_menu",
        value: "small",
        size: "small",
      });

      expect(dispatch.mock.calls[0][0].data.widget_size).toBe("small");
    });
  });

  describe("recordEnabled", () => {
    it("dispatches WIDGETS_ENABLED with enabled flag and widget_size", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordEnabled(false, { source: "context_menu" });

      expect(dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.WIDGETS_ENABLED);
      expect(action.data).toEqual({
        widget_name: "weather",
        widget_size: "medium",
        widget_source: "context_menu",
        enabled: false,
      });
    });

    it("honors size override", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordEnabled(true, { source: "widget", size: "large" });

      expect(dispatch.mock.calls[0][0].data.widget_size).toBe("large");
      expect(dispatch.mock.calls[0][0].data.enabled).toBe(true);
    });
  });

  describe("recordError", () => {
    it("dispatches WIDGETS_ERROR with error_type and no widget_source", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordError("load_error");

      expect(dispatch).toHaveBeenCalledTimes(1);
      const [[action]] = dispatch.mock.calls;
      expect(action.type).toBe(at.WIDGETS_ERROR);
      expect(action.data).toEqual({
        widget_name: "weather",
        widget_size: "medium",
        error_type: "load_error",
      });
      expect(action.data).not.toHaveProperty("widget_source");
    });

    it("honors size override", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordError("load_error", { size: "small" });

      expect(dispatch.mock.calls[0][0].data.widget_size).toBe("small");
    });
  });

  describe("widgetVariant", () => {
    it("adds widget_variant to all four events", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          widgetVariant="variant-a"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordImpression();
      telemetry.recordUserAction("learn_more", { source: "context_menu" });
      telemetry.recordEnabled(false, { source: "context_menu" });
      telemetry.recordError("load_error");

      expect(dispatch.mock.calls.map(([action]) => action.type)).toEqual([
        at.WIDGETS_IMPRESSION,
        at.WIDGETS_USER_EVENT,
        at.WIDGETS_ENABLED,
        at.WIDGETS_ERROR,
      ]);
      for (const [action] of dispatch.mock.calls) {
        expect(action.data.widget_variant).toBe("variant-a");
      }
    });

    it("omits widget_variant when it isn't set", () => {
      let telemetry;
      render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      telemetry.recordImpression();
      telemetry.recordUserAction("learn_more", { source: "context_menu" });
      telemetry.recordEnabled(false, { source: "context_menu" });
      telemetry.recordError("load_error");

      expect(dispatch).toHaveBeenCalledTimes(4);
      for (const [action] of dispatch.mock.calls) {
        expect(action.data).not.toHaveProperty("widget_variant");
      }
    });

    it("reads the latest widget_variant after a prop change", () => {
      let telemetry;
      const { rerender } = render(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          onRender={t => (telemetry = t)}
        />
      );

      rerender(
        <TestComponent
          dispatch={dispatch}
          widget={WEATHER_WIDGET}
          widgetSize="medium"
          widgetVariant="variant-b"
          onRender={t => (telemetry = t)}
        />
      );
      telemetry.recordUserAction("learn_more", { source: "context_menu" });

      expect(dispatch.mock.calls[0][0].data.widget_variant).toBe("variant-b");
    });
  });

  // Bug 2012779 transition: WIDGETS_TIMER_* / WIDGETS_LISTS_* legacy events
  // co-dispatch alongside the unified events. Delete this block once they go.
  describe("legacy co-dispatch", () => {
    describe("impression", () => {
      it("co-dispatches legacy impression types BEFORE WIDGETS_IMPRESSION", () => {
        const { container } = render(
          <TestComponent
            dispatch={dispatch}
            widget={FOCUS_TIMER_WIDGET}
            widgetSize="large"
            legacyImpressionTypes={[at.WIDGETS_TIMER_USER_IMPRESSION]}
            onRender={() => {}}
          />
        );
        const [observerInstance] = observerStub.mock.instances;
        const el = container.querySelector("div");

        observerInstance.callback([{ isIntersecting: true, target: el }]);

        expect(dispatch).toHaveBeenCalledTimes(2);
        expect(dispatch.mock.calls[0][0].type).toBe(
          at.WIDGETS_TIMER_USER_IMPRESSION
        );
        expect(dispatch.mock.calls[1][0].type).toBe(at.WIDGETS_IMPRESSION);
      });
    });

    describe("user event", () => {
      it("co-dispatches the legacy user-event type BEFORE the unified event when legacy: true", () => {
        let telemetry;
        render(
          <TestComponent
            dispatch={dispatch}
            widget={FOCUS_TIMER_WIDGET}
            widgetSize="large"
            legacyUserEventType={at.WIDGETS_TIMER_USER_EVENT}
            onRender={t => (telemetry = t)}
          />
        );

        telemetry.recordUserAction("timer_play", {
          source: "widget",
          legacy: true,
        });

        expect(dispatch).toHaveBeenCalledTimes(2);
        const [[legacy], [modern]] = dispatch.mock.calls;
        expect(legacy.type).toBe(at.WIDGETS_TIMER_USER_EVENT);
        expect(legacy.data).toEqual({ userAction: "timer_play" });
        expect(modern.type).toBe(at.WIDGETS_USER_EVENT);
      });

      it("does NOT co-dispatch legacy when legacy flag is omitted", () => {
        let telemetry;
        render(
          <TestComponent
            dispatch={dispatch}
            widget={FOCUS_TIMER_WIDGET}
            widgetSize="large"
            legacyUserEventType={at.WIDGETS_TIMER_USER_EVENT}
            onRender={t => (telemetry = t)}
          />
        );

        telemetry.recordUserAction("change_size", {
          source: "context_menu",
          value: "small",
          size: "small",
        });

        expect(dispatch).toHaveBeenCalledTimes(1);
        expect(dispatch.mock.calls[0][0].type).toBe(at.WIDGETS_USER_EVENT);
      });

      it("legacy co-dispatch follows the alsoToMain routing flag", () => {
        let telemetry;
        render(
          <TestComponent
            dispatch={dispatch}
            widget={LISTS_WIDGET}
            widgetSize="medium"
            legacyUserEventType={at.WIDGETS_LISTS_USER_EVENT}
            onRender={t => (telemetry = t)}
          />
        );

        telemetry.recordUserAction("task_complete", {
          source: "widget",
          alsoToMain: true,
          legacy: true,
        });

        expect(dispatch).toHaveBeenCalledTimes(2);
        expect(dispatch.mock.calls[0][0].meta.skipLocal).not.toBe(true);
        expect(dispatch.mock.calls[1][0].meta.skipLocal).not.toBe(true);
      });
    });
  });

  describe("widget_location", () => {
    function eventsWith(wrap) {
      let telemetry;
      const { container } = render(
        wrap(
          <TestComponent
            dispatch={dispatch}
            widget={LISTS_WIDGET}
            widgetSize="medium"
            onRender={t => (telemetry = t)}
          />
        )
      );
      const [observerInstance] = observerStub.mock.instances;
      observerInstance.callback([
        { isIntersecting: true, target: container.querySelector("div") },
      ]);
      telemetry.recordUserAction("list_create", { source: "widget" });
      telemetry.recordEnabled(false, { source: "context_menu" });
      return Object.fromEntries(
        dispatch.mock.calls.map(([action]) => [action.type, action.data])
      );
    }

    it.each([
      [WIDGET_PLACEMENTS.HOME, WIDGET_LOCATIONS.HOME_SPACE],
      [WIDGET_PLACEMENTS.LIBRARY, WIDGET_LOCATIONS.WIDGET_SPACE],
      [null, WIDGET_LOCATIONS.WIDGET_CONTAINER],
    ])(
      "reports the widgets section with placement %s as %s",
      (placement, location) => {
        const events = eventsWith(child => (
          <WidgetPlacementProvider placement={placement}>
            {child}
          </WidgetPlacementProvider>
        ));
        expect(events[at.WIDGETS_IMPRESSION].widget_location).toBe(location);
        expect(events[at.WIDGETS_USER_EVENT].widget_location).toBe(location);
        expect("widget_location" in events[at.WIDGETS_ENABLED]).toBe(false);
      }
    );

    it.each([WIDGET_LOCATIONS.SIDEBAR, WIDGET_LOCATIONS.STORY_CARD])(
      "reports %s from its location provider",
      location => {
        const events = eventsWith(child => (
          <WidgetLocationContext.Provider value={location}>
            {child}
          </WidgetLocationContext.Provider>
        ));
        expect(events[at.WIDGETS_IMPRESSION].widget_location).toBe(location);
        expect(events[at.WIDGETS_USER_EVENT].widget_location).toBe(location);
      }
    );

    it("leaves widget_location out without a location provider", () => {
      const events = eventsWith(child => child);
      expect("widget_location" in events[at.WIDGETS_IMPRESSION]).toBe(false);
      expect("widget_location" in events[at.WIDGETS_USER_EVENT]).toBe(false);
    });
  });
});
