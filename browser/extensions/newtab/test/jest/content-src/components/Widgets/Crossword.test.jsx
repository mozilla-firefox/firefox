/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Crossword } from "content-src/components/Widgets/Crossword/Crossword";

// Origin of the endpoint in baseState; the component only accepts inbound
// messages from, and targets outbound commands at, this exact origin.
const MERINO_ORIGIN =
  "https://prod-games-particle.merino.prod.webservices.mozgcp.net";

const baseState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.enabled": true,
      "widgets.system.crossword.enabled": true,
      "widgets.crossword.enabled": true,
      "widgets.crossword.size": "",
      "widgets.framed.allowedHostSuffixes": ".mozgcp.net",
    },
  },
  FramedWidgets: {
    crossword: {
      url: "https://prod-games-particle.merino.prod.webservices.mozgcp.net/index.html",
    },
  },
};

function WrapWithProvider({ children, state = baseState }) {
  const store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

function renderCrossword({
  state = baseState,
  dispatch = jest.fn(),
  handleUserInteraction = jest.fn(),
  widgetsMayBeMaximized = true,
} = {}) {
  const widgetEnabledMap = { crossword: true };
  const utils = render(
    <WrapWithProvider state={state}>
      <Crossword
        dispatch={dispatch}
        handleUserInteraction={handleUserInteraction}
        widgetsMayBeMaximized={widgetsMayBeMaximized}
        widgetEnabledMap={widgetEnabledMap}
      />
    </WrapWithProvider>
  );
  return { ...utils, dispatch, handleUserInteraction };
}

// Dispatch a postMessage-style event at the window as if it came from the
// widget iframe. jsdom does not honor `source` from the MessageEvent init
// dict, so it is forced onto the instance. `origin` and `source` default to
// the trusted iframe values but can be overridden to exercise the filters.
function postWidgetMessage(
  container,
  { data, origin = MERINO_ORIGIN, source }
) {
  const frame = container.querySelector("iframe.crossword-frame");
  const event = new MessageEvent("message", { data, origin });
  Object.defineProperty(event, "source", {
    configurable: true,
    value: source === undefined ? frame.contentWindow : source,
  });
  act(() => {
    window.dispatchEvent(event);
  });
}

function validMessage(type, payload) {
  return { channel: "crossword_widget", type, payload };
}

describe("<Crossword>", () => {
  describe("rendering", () => {
    it("renders the article root with the default medium size class", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");
      expect(root).toBeInTheDocument();
      expect(root).toHaveClass("widget");
      expect(root).toHaveClass("col-4");
      expect(root).toHaveClass("medium-widget");
    });

    it("applies the size from the size pref when set", () => {
      const state = {
        ...baseState,
        Prefs: {
          ...baseState.Prefs,
          values: {
            ...baseState.Prefs.values,
            "widgets.crossword.size": "large",
          },
        },
      };
      const { container } = renderCrossword({ state });
      expect(container.querySelector("article.crossword")).toHaveClass(
        "large-widget"
      );
    });

    it("renders the size submenu only when widgetsMayBeMaximized", () => {
      const { container, rerender } = renderCrossword({
        widgetsMayBeMaximized: true,
      });
      expect(
        container.querySelector("#crossword-size-submenu")
      ).toBeInTheDocument();

      rerender(
        <WrapWithProvider state={baseState}>
          <Crossword
            dispatch={jest.fn()}
            widgetsMayBeMaximized={false}
            widgetEnabledMap={{ crossword: true }}
          />
        </WrapWithProvider>
      );
      expect(
        container.querySelector("#crossword-size-submenu")
      ).not.toBeInTheDocument();
    });
  });

  describe("hide action", () => {
    it("dispatches SET_PREF and WIDGETS_ENABLED when hide is clicked", () => {
      const { container, dispatch } = renderCrossword();
      const hideItem = container.querySelector(
        "panel-item[data-l10n-id='newtab-widget-menu-hide']"
      );
      fireEvent.click(hideItem);

      const setPrefCall = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.SET_PREF &&
          action.data?.name === "widgets.crossword.enabled"
      );
      expect(setPrefCall?.[0].data.value).toBe(false);

      const enabledCall = dispatch.mock.calls.find(
        ([action]) => action?.type === at.WIDGETS_ENABLED
      );
      expect(enabledCall?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "context_menu",
        enabled: false,
      });
    });
  });

  describe("learn more action", () => {
    it("dispatches OPEN_LINK with the shared SUMO URL", () => {
      const { container, dispatch } = renderCrossword();
      const learnMoreItem = container.querySelector("panel-item.learn-more");
      fireEvent.click(learnMoreItem);

      const openLinkCall = dispatch.mock.calls.find(
        ([action]) => action?.type === at.OPEN_LINK
      );
      expect(openLinkCall?.[0].data.url).toBe(
        "https://support.mozilla.org/kb/firefox-new-tab-widgets"
      );
    });

    it("dispatches a learn_more WIDGETS_USER_EVENT", () => {
      const { container, dispatch } = renderCrossword();
      fireEvent.click(container.querySelector("panel-item.learn-more"));

      const userEventCall = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "learn_more"
      );
      expect(userEventCall?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "context_menu",
        user_action: "learn_more",
        widget_size: "medium",
      });
    });
  });

  describe("impression telemetry", () => {
    let originalIntersectionObserver;
    let observerInstances;

    beforeEach(() => {
      observerInstances = [];
      originalIntersectionObserver = global.IntersectionObserver;
      global.IntersectionObserver = class MockIntersectionObserver {
        constructor(callback) {
          this.callback = callback;
          this.observed = [];
          observerInstances.push(this);
        }
        observe(el) {
          this.observed.push(el);
        }
        unobserve() {}
        disconnect() {}
      };
    });

    afterEach(() => {
      global.IntersectionObserver = originalIntersectionObserver;
    });

    it("dispatches WIDGETS_IMPRESSION only once even when intersection fires repeatedly", () => {
      const { dispatch } = renderCrossword();
      const [observer] = observerInstances;
      const [target] = observer.observed;

      observer.callback([{ isIntersecting: true, target }], observer);
      observer.callback([{ isIntersecting: true, target }], observer);

      const impressions = dispatch.mock.calls.filter(
        ([action]) => action?.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
      expect(impressions[0][0].data).toMatchObject({
        widget_name: "crossword",
        widget_size: "medium",
      });
    });
  });

  describe("crossword iframe", () => {
    it("renders a sandboxed iframe pointing at the resolved endpoint", () => {
      const { container } = renderCrossword();
      const frame = container.querySelector("iframe.crossword-frame");
      expect(frame).toBeInTheDocument();
      expect(frame).toHaveAttribute(
        "sandbox",
        "allow-scripts allow-same-origin allow-forms allow-popups allow-popups-to-escape-sandbox"
      );
      expect(frame).toHaveAttribute(
        "src",
        "https://prod-games-particle.merino.prod.webservices.mozgcp.net/index.html"
      );
    });

    it("prefers the trainhop namespace iframeSrcOverride over the manifest url", () => {
      const trainhopSrc =
        "https://hotfix.merino.prod.webservices.mozgcp.net/index.html";
      const state = {
        ...baseState,
        Prefs: {
          ...baseState.Prefs,
          values: {
            ...baseState.Prefs.values,
            trainhopConfig: {
              widgetCrossword: { iframeSrcOverride: trainhopSrc },
            },
          },
        },
      };
      const { container } = renderCrossword({ state });
      expect(container.querySelector("iframe.crossword-frame")).toHaveAttribute(
        "src",
        trainhopSrc
      );
    });

    it("renders nothing without a manifest entry", () => {
      const { container } = renderCrossword({
        state: { ...baseState, FramedWidgets: {} },
      });
      expect(container.querySelector("article.crossword")).toBeNull();
    });
  });

  describe("change size action", () => {
    it("dispatches SET_PREF and a change_size WIDGETS_USER_EVENT when a size is picked", () => {
      const { container, dispatch } = renderCrossword();
      const largeSizeItem = container.querySelector(
        "#crossword-size-submenu panel-item[data-size='large']"
      );
      fireEvent.click(largeSizeItem);

      const setPrefCall = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.SET_PREF &&
          action.data?.name === "widgets.crossword.size"
      );
      expect(setPrefCall?.[0].data.value).toBe("large");

      const userEventCall = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "change_size"
      );
      expect(userEventCall?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "context_menu",
        user_action: "change_size",
        action_value: "large",
        widget_size: "large",
      });
    });
  });

  describe("menu actions — postMessage send", () => {
    let originalCrypto;

    beforeEach(() => {
      originalCrypto = global.crypto;
      Object.defineProperty(global, "crypto", {
        configurable: true,
        value: { randomUUID: () => "test-request-id" },
      });
    });

    afterEach(() => {
      Object.defineProperty(global, "crypto", {
        configurable: true,
        value: originalCrypto,
      });
    });

    it("posts show_all_clues to the iframe on the crossword channel with the merino targetOrigin", () => {
      const { container } = renderCrossword();
      const frame = container.querySelector("iframe.crossword-frame");
      const postSpy = jest
        .spyOn(frame.contentWindow, "postMessage")
        .mockImplementation(() => {});

      fireEvent.click(container.querySelector("panel-item.show-all-clues"));

      expect(postSpy).toHaveBeenCalledTimes(1);
      const [[message, targetOrigin]] = postSpy.mock.calls;
      expect(message).toMatchObject({
        channel: "crossword_widget",
        type: "menu_action",
        action: "show_all_clues",
      });
      expect(message.requestId).toMatch(/^firefox-menu-/);
      expect(targetOrigin).toBe(MERINO_ORIGIN);
      expect(targetOrigin).not.toBe("*");
    });

    it("posts reveal_grid when Solve puzzle is clicked", () => {
      const { container } = renderCrossword();
      const frame = container.querySelector("iframe.crossword-frame");
      const postSpy = jest
        .spyOn(frame.contentWindow, "postMessage")
        .mockImplementation(() => {});

      fireEvent.click(container.querySelector("panel-item.solve-puzzle"));

      expect(postSpy).toHaveBeenCalledTimes(1);
      expect(postSpy.mock.calls[0][0]).toMatchObject({
        type: "menu_action",
        action: "reveal_grid",
      });
    });

    it("dispatches a WIDGETS_USER_EVENT with the real action in user_action (no menu_action bucket)", () => {
      const { container, dispatch } = renderCrossword();
      const frame = container.querySelector("iframe.crossword-frame");
      jest
        .spyOn(frame.contentWindow, "postMessage")
        .mockImplementation(() => {});

      fireEvent.click(container.querySelector("panel-item.show-all-clues"));

      const userEventCall = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "show_all_clues"
      );
      expect(userEventCall?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "context_menu",
        user_action: "show_all_clues",
        widget_size: "medium",
      });
      // The opaque "menu_action" bucket is gone; crossword now matches the
      // convention every other widget uses.
      expect(
        dispatch.mock.calls.some(
          ([action]) =>
            action?.type === at.WIDGETS_USER_EVENT &&
            action.data?.user_action === "menu_action"
        )
      ).toBe(false);
    });

    it("does not post when the endpoint is malformed (no resolvable origin)", () => {
      const state = {
        ...baseState,
        FramedWidgets: {
          crossword: { url: "not-a-valid-url" },
        },
      };
      const { container } = renderCrossword({ state });
      const frame = container.querySelector("iframe.crossword-frame");
      const postSpy = jest
        .spyOn(frame.contentWindow, "postMessage")
        .mockImplementation(() => {});

      fireEvent.click(container.querySelector("panel-item.show-all-clues"));

      expect(postSpy).not.toHaveBeenCalled();
    });
  });

  describe("inbound widget events + security filtering", () => {
    it("dispatches a puzzle_completed WIDGETS_USER_EVENT for a valid message", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("puzzle_completed", {
          elapsedTimeSeconds: 42,
          hintsTaken: 3,
        }),
      });

      const call = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "puzzle_completed"
      );
      expect(call?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "iframe",
        user_action: "puzzle_completed",
        action_value: 3,
        widget_size: "medium",
      });
    });

    it("puts the real action in user_action for an allowlisted interaction", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("interaction", { action: "hint_revealed" }),
      });

      const call = dispatch.mock.calls.find(
        ([action]) => action?.type === at.WIDGETS_USER_EVENT
      );
      expect(call?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "iframe",
        user_action: "hint_revealed",
      });
    });

    it("does not forward a non-allowlisted interaction (keystroke noise) to Glean, but still flips the interaction pref", () => {
      const { container, dispatch, handleUserInteraction } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("interaction", {
          action: "input_letter",
          letter: "a",
        }),
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
      // Side effect preserved: the "New" badge pref still flips.
      expect(handleUserInteraction).toHaveBeenCalledWith("crossword");
    });

    it("does not forward a menu-echo interaction to Glean (Firefox records the menu action itself)", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("interaction", {
          action: "all_clues_opened",
          entry: "context_menu",
        }),
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
    });

    it("ignores messages from an unexpected origin", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("interaction", { action: "hint_revealed" }),
        origin: "https://evil.example.com",
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
    });

    it("ignores messages whose source is not the widget iframe", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("interaction", { action: "hint_revealed" }),
        source: window,
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
    });

    it("ignores messages missing the crossword channel", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: {
          type: "interaction",
          payload: { action: "hint_revealed" },
        },
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
    });

    it("ignores events whose payload fails validation", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("puzzle_completed", { hintsTaken: "three" }),
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
    });

    it("ignores puzzle_completed with a non-whole hintsTaken", () => {
      const { container, dispatch } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("puzzle_completed", {
          elapsedTimeSeconds: 42,
          hintsTaken: -1,
        }),
      });

      expect(
        dispatch.mock.calls.some(
          ([action]) => action?.type === at.WIDGETS_USER_EVENT
        )
      ).toBe(false);
    });
  });

  describe("completion state", () => {
    it("hides Solve puzzle but keeps Show clues after a valid puzzle_completed", () => {
      const { container } = renderCrossword();
      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).toBeInTheDocument();
      expect(
        container.querySelector("panel-item.show-all-clues")
      ).toBeInTheDocument();

      postWidgetMessage(container, {
        data: validMessage("puzzle_completed", {
          elapsedTimeSeconds: 42,
          hintsTaken: 3,
        }),
      });

      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).not.toBeInTheDocument();
      expect(
        container.querySelector("panel-item.show-all-clues")
      ).toBeInTheDocument();
    });

    it("hides Solve puzzle when returning to an already-solved puzzle (puzzle_state completed only)", () => {
      const { container } = renderCrossword();
      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).toBeInTheDocument();

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "completed" }),
      });

      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).not.toBeInTheDocument();
    });

    it("restores Solve puzzle when a new puzzle returns to the intro state", () => {
      const { container } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "completed" }),
      });
      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).not.toBeInTheDocument();

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "intro" }),
      });
      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).toBeInTheDocument();
    });

    it("keeps Solve puzzle when a completion message fails validation", () => {
      const { container } = renderCrossword();

      postWidgetMessage(container, {
        data: validMessage("puzzle_completed", { hintsTaken: "three" }),
      });

      expect(
        container.querySelector("panel-item.solve-puzzle")
      ).toBeInTheDocument();
    });
  });

  describe("puzzle_state sizing", () => {
    function largeState() {
      return {
        ...baseState,
        Prefs: {
          ...baseState.Prefs,
          values: {
            ...baseState.Prefs.values,
            "widgets.crossword.size": "large",
          },
        },
      };
    }

    it("forces the large layout while the puzzle is in_progress", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");
      expect(root).toHaveClass("medium-widget");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", {
          crosswordId: "gid",
          state: "in_progress",
          sourceLabel: "live",
        }),
      });

      expect(root).toHaveClass("large-widget");
      expect(root).not.toHaveClass("medium-widget");
    });

    it("stays large through the completed state after solving", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "in_progress" }),
      });
      expect(root).toHaveClass("large-widget");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "completed" }),
      });
      expect(root).toHaveClass("large-widget");
      expect(root).not.toHaveClass("medium-widget");
    });

    it("stays large on puzzle_completed after solving", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "in_progress" }),
      });
      expect(root).toHaveClass("large-widget");

      postWidgetMessage(container, {
        data: validMessage("puzzle_completed", {
          elapsedTimeSeconds: 42,
          hintsTaken: 1,
        }),
      });
      expect(root).toHaveClass("large-widget");
    });

    it("stays medium when loading directly into completed (returning card)", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "completed" }),
      });

      expect(root).toHaveClass("medium-widget");
      expect(root).not.toHaveClass("large-widget");
    });

    it("returns to medium when a new puzzle goes back to the intro state", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "in_progress" }),
      });
      expect(root).toHaveClass("large-widget");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "intro" }),
      });
      expect(root).toHaveClass("medium-widget");
    });

    it("does not force large when widgetsMayBeMaximized is false", () => {
      const { container } = renderCrossword({ widgetsMayBeMaximized: false });
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "in_progress" }),
      });

      expect(root).toHaveClass("medium-widget");
      expect(root).not.toHaveClass("large-widget");
    });

    it("keeps a user-chosen large size on the intro state", () => {
      const { container } = renderCrossword({ state: largeState() });
      const root = container.querySelector("article.crossword");
      expect(root).toHaveClass("large-widget");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "intro" }),
      });

      expect(root).toHaveClass("large-widget");
    });

    it("ignores a puzzle_state with an unknown state value", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "bogus" }),
      });

      expect(root).toHaveClass("medium-widget");
      expect(root).not.toHaveClass("large-widget");
    });

    it("grows to large when the completed grid is viewed from the returning card", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "completed" }),
      });
      expect(root).toHaveClass("medium-widget");

      postWidgetMessage(container, {
        data: validMessage("interaction", {
          action: "admire_crossword_clicked",
        }),
      });

      expect(root).toHaveClass("large-widget");
      expect(root).not.toHaveClass("medium-widget");
    });

    it("grows to large when the all-clues panel opens", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("puzzle_state", { state: "completed" }),
      });
      expect(root).toHaveClass("medium-widget");

      postWidgetMessage(container, {
        data: validMessage("interaction", {
          action: "all_clues_opened",
          entry: "context_menu",
        }),
      });

      expect(root).toHaveClass("large-widget");
      expect(root).not.toHaveClass("medium-widget");
    });

    it("grows to large when the grid is revealed via Solve puzzle", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");
      expect(root).toHaveClass("medium-widget");

      postWidgetMessage(container, {
        data: validMessage("interaction", {
          action: "reveal_grid_requested",
          mode: "irreversible_solve",
        }),
      });

      expect(root).toHaveClass("large-widget");
      expect(root).not.toHaveClass("medium-widget");
    });

    it("does not grow to large for an unrelated interaction", () => {
      const { container } = renderCrossword();
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("interaction", { action: "backspace" }),
      });

      expect(root).toHaveClass("medium-widget");
      expect(root).not.toHaveClass("large-widget");
    });

    it("does not grow to large on those interactions when widgetsMayBeMaximized is false", () => {
      const { container } = renderCrossword({ widgetsMayBeMaximized: false });
      const root = container.querySelector("article.crossword");

      postWidgetMessage(container, {
        data: validMessage("interaction", {
          action: "all_clues_opened",
        }),
      });

      expect(root).toHaveClass("medium-widget");
      expect(root).not.toHaveClass("large-widget");
    });
  });

  describe("powered by particle action", () => {
    it("opens particle.news in a new tab", () => {
      const { container, dispatch, handleUserInteraction } = renderCrossword();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-crossword-menu-powered-by-particle']"
        )
      );

      const openLinkCall = dispatch.mock.calls.find(
        ([action]) => action?.type === at.OPEN_LINK
      );
      expect(openLinkCall?.[0].data).toEqual({
        url: "https://particle.news",
        where: "tab",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("crossword");
    });

    it("dispatches a powered_by_particle WIDGETS_USER_EVENT", () => {
      const { container, dispatch } = renderCrossword();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-crossword-menu-powered-by-particle']"
        )
      );

      const userEventCall = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "powered_by_particle"
      );
      expect(userEventCall?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "context_menu",
        user_action: "powered_by_particle",
        widget_size: "medium",
      });
    });
  });

  describe("menu strings", () => {
    it("hardcodes the crossword-specific items and keeps shared chrome ids", () => {
      const { container } = renderCrossword();
      const menu = container.querySelector("#crossword-context-menu");

      expect(
        container.querySelector(".crossword-context-menu-button")
      ).toHaveAttribute(
        "data-l10n-id",
        "newtab-crossword-widget-open-menu-button"
      );
      // Crossword-specific menu items are hardcoded inline (English-only).
      expect(menu.querySelector("panel-item.show-all-clues")).toHaveTextContent(
        "Show clues"
      );
      expect(menu.querySelector("panel-item.solve-puzzle")).toHaveTextContent(
        "Solve puzzle"
      );
      expect(
        menu.querySelector(
          "panel-item[data-l10n-id='newtab-crossword-menu-powered-by-particle']"
        )
      ).toBeInTheDocument();

      // Generic chrome keeps the shared, translated Fluent ids.
      expect(
        menu.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "#crossword-size-submenu panel-item[data-l10n-id='newtab-widget-size-medium']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "#crossword-size-submenu panel-item[data-l10n-id='newtab-widget-size-large']"
        )
      ).toBeInTheDocument();
      expect(
        menu.querySelector("panel-item[data-l10n-id='newtab-widget-menu-hide']")
      ).toBeInTheDocument();

      expect(menu.querySelector("hr")).toBeInTheDocument();
    });
  });

  describe("new badge", () => {
    it("renders the New badge before the title when not interacted with", () => {
      const { container } = renderCrossword();
      const badge = container.querySelector("moz-badge.crossword-new-badge");
      expect(badge).toBeInTheDocument();
      // No type="new": that looks up the unregistered moz-badge-new2 string.
      expect(badge).not.toHaveAttribute("type");
      expect(badge).toHaveAttribute(
        "data-l10n-id",
        "newtab-widget-lists-label-new"
      );
      // The badge renders immediately before the title.
      expect(badge.nextElementSibling).toBe(
        container.querySelector("h3.newtab-crossword-title")
      );
    });

    it("hides the New badge once the interaction pref is set", () => {
      const state = {
        ...baseState,
        Prefs: {
          ...baseState.Prefs,
          values: {
            ...baseState.Prefs.values,
            "widgets.crossword.interaction": true,
          },
        },
      };
      const { container } = renderCrossword({ state });
      expect(
        container.querySelector("moz-badge.crossword-new-badge")
      ).not.toBeInTheDocument();
      expect(
        container.querySelector("h3.newtab-crossword-title")
      ).toBeInTheDocument();
    });
  });

  describe("interaction pref", () => {
    it("flips the interaction pref via handleUserInteraction on a menu action", () => {
      const handleUserInteraction = jest.fn();
      const { container } = renderCrossword({ handleUserInteraction });
      fireEvent.click(container.querySelector("panel-item.learn-more"));
      expect(handleUserInteraction).toHaveBeenCalledWith("crossword");
    });

    it("does NOT flip the interaction pref when hiding the widget", () => {
      const handleUserInteraction = jest.fn();
      const { container } = renderCrossword({ handleUserInteraction });
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-menu-hide']"
        )
      );
      expect(handleUserInteraction).not.toHaveBeenCalled();
    });
  });
});
