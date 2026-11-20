/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Solitaire } from "content-src/components/Widgets/Solitaire/Solitaire";
import { WrapWithProvider } from "test/jest/test-utils";
import { FRAMED_WIDGET_LOAD_TIMEOUT_MS } from "content-src/components/Widgets/useFramedWidgetMessages";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.system.solitaire.enabled": true,
      "widgets.solitaire.enabled": true,
      "widgets.solitaire.size": "medium",
      "widgets.solitaire.interaction": false,
    },
  },
  FramedWidgets: {
    solitaire: {
      url: "https://prod-widgets-yahoo-solitaire.merino.prod.webservices.mozgcp.net/1.0.20261030214548/index.html",
    },
  },
};

const renderWidget = (dispatch = jest.fn(), props = {}, state = mockState) => {
  const handleUserInteraction = props.handleUserInteraction || jest.fn();
  const { container } = render(
    <WrapWithProvider state={state}>
      <Solitaire
        dispatch={dispatch}
        handleUserInteraction={handleUserInteraction}
        widgetsMayBeMaximized={true}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
  return { container, dispatch, handleUserInteraction };
};

// jsdom ignores `source` in the MessageEvent init dict, so it is set on the
// instance.
const postFrameMessage = (container, data) => {
  const frame = container.querySelector("iframe.solitaire-frame");
  const event = new MessageEvent("message", {
    data: { channel: "solitaire_widget", ...data },
    origin: new URL(mockState.FramedWidgets.solitaire.url).origin,
  });
  Object.defineProperty(event, "source", { value: frame.contentWindow });
  act(() => {
    window.dispatchEvent(event);
  });
};

const postReady = (container, variant = "no_game") =>
  postFrameMessage(container, {
    type: "widget_ready",
    payload: { protocol: 1, variant },
  });

const postUserEvent = (container, payload) =>
  postFrameMessage(container, {
    type: "telemetry",
    message: "user_event",
    payload,
  });

const actionsOfType = (dispatch, type) =>
  dispatch.mock.calls
    .map(([action]) => action)
    .filter(action => action?.type === type);

describe("Solitaire widget", () => {
  let showModal;

  beforeEach(() => {
    showModal = jest.fn();
    HTMLDialogElement.prototype.showModal = showModal;
    HTMLDialogElement.prototype.close = jest.fn();
  });

  afterEach(() => {
    delete HTMLDialogElement.prototype.showModal;
    delete HTMLDialogElement.prototype.close;
  });

  it("loads the manifest url in a sandboxed iframe labelled by the title", () => {
    const { container } = renderWidget();
    const frame = container.querySelector("iframe.solitaire-frame");
    expect(frame).toHaveAttribute("src", mockState.FramedWidgets.solitaire.url);
    expect(frame).toHaveAttribute("aria-labelledby", "solitaire-widget-label");
    expect(frame).toHaveAttribute(
      "sandbox",
      "allow-scripts allow-same-origin allow-forms allow-popups allow-popups-to-escape-sandbox"
    );
  });

  it("renders nothing without a manifest entry", () => {
    const { container } = renderWidget(
      jest.fn(),
      {},
      {
        ...mockState,
        FramedWidgets: {},
      }
    );
    expect(container.querySelector("article.solitaire")).toBeNull();
  });

  it("renders the widget at the resolved size", () => {
    const { container } = renderWidget();
    const root = container.querySelector("article.solitaire");
    expect(root).toBeTruthy();
    expect(root.className).toContain("medium-widget");
  });

  it("renders the localized title", () => {
    const { container } = renderWidget();
    const title = container.querySelector("#solitaire-widget-label");
    expect(title.classList).toContain("newtab-widget-title");
    expect(title.getAttribute("data-l10n-id")).toBe(
      "newtab-solitaire-widget-title"
    );
  });

  it("renders a labelled context menu button", () => {
    const { container } = renderWidget();
    expect(
      container.querySelector(
        ".solitaire-context-menu-button[data-l10n-id='newtab-solitaire-widget-open-menu-button']"
      )
    ).toBeTruthy();
  });

  it("shows the New badge until the widget has been interacted with", () => {
    const { container } = renderWidget();
    expect(container.querySelector(".solitaire-new-badge")).toBeTruthy();
  });

  it("hides the New badge once the interaction pref is set", () => {
    const interacted = {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.solitaire.interaction": true,
        },
      },
    };
    const { container } = renderWidget(jest.fn(), {}, interacted);
    expect(container.querySelector(".solitaire-new-badge")).toBeNull();
  });

  it("offers only medium and large in the size submenu", () => {
    const { container } = renderWidget();
    const sizes = Array.from(
      container.querySelectorAll("panel-item[data-size]")
    ).map(el => el.getAttribute("data-size"));
    expect(sizes).toEqual(["medium", "large"]);
  });

  it("omits the size submenu when the layout cannot maximize", () => {
    const { container } = renderWidget(jest.fn(), {
      widgetsMayBeMaximized: false,
    });
    expect(
      container.querySelector("panel-list[id='solitaire-size-submenu']")
    ).toBeNull();
  });

  it("puts Powered by Yahoo first, above the shared footer divider", () => {
    const { container } = renderWidget();
    const menu = container.querySelector(
      "panel-list[id='solitaire-context-menu']"
    );
    expect(menu).toBeTruthy();
    const [first, second] = menu.children;
    expect(first.getAttribute("data-l10n-id")).toBe(
      "newtab-solitaire-menu-powered-by-yahoo"
    );
    expect(second.tagName).toBe("HR");
    expect(
      menu.querySelector("panel-item[data-l10n-id='newtab-widget-menu-hide']")
    ).toBeTruthy();
    expect(
      menu.querySelector(
        "panel-item[data-l10n-id='newtab-solitaire-menu-learn-more']"
      )
    ).toBeTruthy();
  });

  describe("context menu actions", () => {
    it("picking a size writes the pref and records change_size", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "#solitaire-size-submenu panel-item[data-size='large']"
        )
      );

      const setPref = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.SET_PREF &&
          action.data?.name === "widgets.solitaire.size"
      );
      expect(setPref[0].data.value).toBe("large");

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "change_size"
      );
      // widget_size must be the NEW size, not the pre-change one.
      expect(userEvent[0].data).toMatchObject({
        widget_name: "solitaire",
        widget_source: "context_menu",
        action_value: "large",
        widget_size: "large",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("solitaire");
    });

    it("Powered by Yahoo opens yahoo.com in a new tab and records the click", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-solitaire-menu-powered-by-yahoo']"
        )
      );

      const openLink = dispatch.mock.calls.find(
        ([action]) => action?.type === at.OPEN_LINK
      );
      expect(openLink[0].data).toEqual({
        url: "https://www.yahoo.com/",
        where: "tab",
      });

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "powered_by_yahoo"
      );
      expect(userEvent[0].data).toMatchObject({
        widget_name: "solitaire",
        widget_source: "context_menu",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("solitaire");
    });

    it("Learn more records the event without opening the link itself", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-solitaire-menu-learn-more']"
        )
      );

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "learn_more"
      );
      expect(userEvent[0].data).toMatchObject({
        widget_name: "solitaire",
        widget_source: "context_menu",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("solitaire");
    });

    const clickLearnMore = container =>
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-solitaire-menu-learn-more']"
        )
      );

    it("leaves widget_variant off when widget_ready comes after the load timeout", () => {
      jest.useFakeTimers();
      try {
        const { container, dispatch } = renderWidget();
        act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
        postReady(container);
        clickLearnMore(container);
        const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
        expect(event.data.user_action).toBe("learn_more");
        expect(event.data).not.toHaveProperty("widget_variant");
      } finally {
        jest.useRealTimers();
      }
    });

    it("keeps widget_variant after a terminal widget_error that follows widget_ready", () => {
      const { container, dispatch } = renderWidget();
      postReady(container, "game_in_progress");
      postFrameMessage(container, {
        type: "widget_error",
        payload: { reason: "render_failed", terminal: true },
      });
      clickLearnMore(container);
      const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(event.data.widget_variant).toBe("game_in_progress");
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
        unobserve(el) {
          this.observed = this.observed.filter(o => o !== el);
        }
        disconnect() {}
      };
    });

    afterEach(() => {
      global.IntersectionObserver = originalIntersectionObserver;
    });

    it("waits for widget_ready, then dispatches WIDGETS_IMPRESSION once with the variant", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch);
      const [observer] = observerInstances;
      expect(observer.observed).toEqual([]);

      postReady(container, "game_in_progress");
      const [target] = observer.observed;
      observer.callback([{ isIntersecting: true, target }], observer);
      observer.callback([{ isIntersecting: true, target }], observer);

      const impressions = actionsOfType(dispatch, at.WIDGETS_IMPRESSION);
      expect(impressions).toHaveLength(1);
      expect(impressions[0].data).toMatchObject({
        widget_name: "solitaire",
        widget_size: "medium",
        widget_variant: "game_in_progress",
      });
    });

    it("still dispatches WIDGETS_IMPRESSION, without a variant, once the frame times out", () => {
      jest.useFakeTimers();
      try {
        const dispatch = jest.fn();
        renderWidget(dispatch);
        const [observer] = observerInstances;
        expect(observer.observed).toEqual([]);

        act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
        const [target] = observer.observed;
        observer.callback([{ isIntersecting: true, target }], observer);

        const [impression] = actionsOfType(dispatch, at.WIDGETS_IMPRESSION);
        expect(impression.data.widget_name).toBe("solitaire");
        expect(impression.data).not.toHaveProperty("widget_variant");
      } finally {
        jest.useRealTimers();
      }
    });

    it("waits for widget_ready again when the manifest url changes after a timeout", () => {
      jest.useFakeTimers();
      try {
        const dispatch = jest.fn();
        const handleUserInteraction = jest.fn();
        const tree = state => (
          <WrapWithProvider state={state}>
            <Solitaire
              dispatch={dispatch}
              handleUserInteraction={handleUserInteraction}
              widgetsMayBeMaximized={true}
              widgetEnabledMap={{}}
            />
          </WrapWithProvider>
        );
        const { rerender } = render(tree(mockState));
        const [observer] = observerInstances;
        act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
        expect(observer.observed).toHaveLength(1);

        rerender(
          tree({
            ...mockState,
            FramedWidgets: {
              solitaire: {
                url: "https://prod-widgets-yahoo-solitaire.merino.prod.webservices.mozgcp.net/1.1.0/index.html",
              },
            },
          })
        );
        expect(observer.observed).toEqual([]);
      } finally {
        jest.useRealTimers();
      }
    });
  });

  describe("frame telemetry", () => {
    it.each([
      [{ user_action: "game_start", action_value: "new" }],
      [{ user_action: "game_start", action_value: "resumed" }],
      [{ user_action: "game_undo" }],
      [{ user_action: "game_hint" }],
      [{ user_action: "settings_click" }],
      [{ user_action: "cardback_change" }],
      [{ user_action: "wallpaper_change" }],
      [{ user_action: "game_close" }],
      [{ user_action: "game_complete" }],
    ])("records %o and clears the New badge", payload => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      postReady(container);
      postUserEvent(container, payload);

      const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(event.data).toMatchObject({
        widget_name: "solitaire",
        widget_source: "iframe",
        user_action: payload.user_action,
      });
      expect(event.data.action_value).toBe(payload.action_value);
      expect(handleUserInteraction).toHaveBeenCalledWith("solitaire");
    });

    it.each([
      ["an unlisted event", { user_action: "ad_click" }],
      ["the Yahoo Games link click", { user_action: "yahoo_games_link" }],
      [
        "an unknown value",
        { user_action: "game_start", action_value: "restart" },
      ],
      [
        "a value on an event without one",
        { user_action: "game_undo", action_value: "three" },
      ],
    ])("drops %s and leaves the New badge", (_label, payload) => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      postReady(container);
      postUserEvent(container, payload);
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
      expect(handleUserInteraction).not.toHaveBeenCalled();
    });

    it("drops user events before widget_ready", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      postUserEvent(container, { user_action: "game_undo" });
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
      expect(handleUserInteraction).not.toHaveBeenCalled();
    });

    it("drops user events after a terminal widget_error", () => {
      const { container, dispatch } = renderWidget();
      postReady(container);
      postFrameMessage(container, {
        type: "widget_error",
        payload: { reason: "render_failed", terminal: true },
      });
      postUserEvent(container, { user_action: "game_undo" });
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
    });

    it("drops user events when widget_ready arrives after the load timeout", () => {
      jest.useFakeTimers();
      try {
        const { container, dispatch } = renderWidget();
        act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
        postReady(container);
        postUserEvent(container, { user_action: "game_undo" });
        expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
      } finally {
        jest.useRealTimers();
      }
    });

    it("records render_failed and stays on the page", () => {
      const { container, dispatch } = renderWidget();
      postFrameMessage(container, {
        type: "widget_error",
        payload: { reason: "render_failed", terminal: true },
      });
      const [error] = actionsOfType(dispatch, at.WIDGETS_ERROR);
      expect(error.data).toMatchObject({
        widget_name: "solitaire",
        error_type: "render_failed",
      });
      expect(
        actionsOfType(dispatch, at.FRAMED_WIDGET_UNAVAILABLE)
      ).toHaveLength(0);
      expect(container.querySelector("article.solitaire")).toBeTruthy();
    });

    it("records load_timeout without widget_ready and stays on the page", () => {
      jest.useFakeTimers();
      try {
        const { container, dispatch } = renderWidget();
        act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
        const [error] = actionsOfType(dispatch, at.WIDGETS_ERROR);
        expect(error.data.error_type).toBe("load_timeout");
        expect(
          actionsOfType(dispatch, at.FRAMED_WIDGET_UNAVAILABLE)
        ).toHaveLength(0);
        expect(container.querySelector("article.solitaire")).toBeTruthy();
      } finally {
        jest.useRealTimers();
      }
    });
  });
  describe("expanded view", () => {
    function postAction(container, message) {
      postFrameMessage(container, {
        type: "action",
        message,
        payload: { game: "solitaire" },
      });
    }

    it("exposes dialog semantics only while playing", () => {
      const { container } = renderWidget();
      const dialog = container.querySelector("dialog.solitaire-dialog");
      expect(dialog).toHaveAttribute("role", "none");
      expect(dialog).not.toHaveAttribute("aria-labelledby");
      postAction(container, "start_game");
      expect(dialog).not.toHaveAttribute("role");
      expect(dialog).toHaveAttribute(
        "aria-labelledby",
        "solitaire-widget-label"
      );
    });

    it("opens the modal on start_game and hides the header", () => {
      const { container, handleUserInteraction } = renderWidget();
      postAction(container, "start_game");
      expect(showModal).toHaveBeenCalled();
      expect(container.querySelector(".solitaire-dialog-close")).toBeTruthy();
      expect(container.querySelector(".solitaire-title-wrapper").hidden).toBe(
        true
      );
      expect(handleUserInteraction).toHaveBeenCalledWith("solitaire");
    });

    it("returns to the card on close_game without reloading the frame", () => {
      const { container, dispatch } = renderWidget();
      const frame = container.querySelector("iframe.solitaire-frame");
      postAction(container, "start_game");
      postAction(container, "close_game");
      const dialog = container.querySelector("dialog.solitaire-dialog");
      act(() => {
        fireEvent(dialog, new Event("close"));
      });
      expect(container.querySelector(".solitaire-title-wrapper").hidden).toBe(
        false
      );
      expect(container.querySelector("iframe.solitaire-frame")).toBe(frame);
      // The frame records its own game_close for this one.
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
    });

    const expectHostClose = dispatch => {
      const events = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(events).toHaveLength(1);
      expect(events[0].data).toMatchObject({
        widget_name: "solitaire",
        widget_source: "widget",
        user_action: "game_close",
      });
    };

    it("reloads the frame and records game_close when the host closes the modal", () => {
      const { container, dispatch } = renderWidget();
      const frame = container.querySelector("iframe.solitaire-frame");
      postReady(container);
      postAction(container, "start_game");
      fireEvent.click(container.querySelector(".solitaire-dialog-close"));
      expect(container.querySelector(".solitaire-title-wrapper").hidden).toBe(
        false
      );
      expect(container.querySelector("iframe.solitaire-frame")).not.toBe(frame);
      expectHostClose(dispatch);
    });

    it("reloads the frame and records game_close when Esc or a backdrop click closes the modal", () => {
      const { container, dispatch } = renderWidget();
      const frame = container.querySelector("iframe.solitaire-frame");
      postReady(container);
      postAction(container, "start_game");
      const dialog = container.querySelector("dialog.solitaire-dialog");
      expect(dialog).toHaveAttribute("closedby", "any");
      act(() => {
        fireEvent(dialog, new Event("close"));
      });
      expect(container.querySelector("iframe.solitaire-frame")).not.toBe(frame);
      expectHostClose(dispatch);
    });

    it.each([
      ["before widget_ready", () => {}],
      [
        "after a terminal widget_error",
        container => {
          postReady(container);
          postFrameMessage(container, {
            type: "widget_error",
            payload: { reason: "render_failed", terminal: true },
          });
        },
      ],
    ])("reloads the frame without recording game_close %s", (_label, setup) => {
      const { container, dispatch } = renderWidget();
      const frame = container.querySelector("iframe.solitaire-frame");
      setup(container);
      postAction(container, "start_game");
      fireEvent.click(container.querySelector(".solitaire-dialog-close"));
      expect(container.querySelector("iframe.solitaire-frame")).not.toBe(frame);
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
    });

    it("records events from the reloaded frame after a terminal widget_error", () => {
      const { container, dispatch } = renderWidget();
      postReady(container);
      postAction(container, "start_game");
      postFrameMessage(container, {
        type: "widget_error",
        payload: { reason: "render_failed", terminal: true },
      });
      fireEvent.click(container.querySelector(".solitaire-dialog-close"));
      postReady(container, "game_in_progress");
      postUserEvent(container, { user_action: "game_undo" });
      const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(event.data).toMatchObject({
        user_action: "game_undo",
        widget_variant: "game_in_progress",
      });
    });

    it("drops events from the reloaded frame until it sends widget_ready", () => {
      const { container, dispatch } = renderWidget();
      postReady(container);
      postAction(container, "start_game");
      fireEvent.click(container.querySelector(".solitaire-dialog-close"));
      dispatch.mockClear();
      postUserEvent(container, { user_action: "game_undo" });
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
    });

    it("ignores actions from other windows", () => {
      renderWidget();
      const event = new MessageEvent("message", {
        data: {
          channel: "solitaire_widget",
          type: "action",
          message: "start_game",
          payload: {},
        },
        origin: new URL(mockState.FramedWidgets.solitaire.url).origin,
      });
      act(() => {
        window.dispatchEvent(event);
      });
      expect(showModal).not.toHaveBeenCalled();
    });
  });
});
