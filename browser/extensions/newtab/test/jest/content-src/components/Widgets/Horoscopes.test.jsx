/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Horoscopes } from "content-src/components/Widgets/Horoscopes/Horoscopes";
import { WrapWithProvider } from "test/jest/test-utils";
import { FRAMED_WIDGET_LOAD_TIMEOUT_MS } from "content-src/components/Widgets/useFramedWidgetMessages";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.system.horoscopes.enabled": true,
      "widgets.horoscopes.enabled": true,
      "widgets.horoscopes.size": "medium",
      "widgets.horoscopes.interaction": false,
    },
  },
  FramedWidgets: {
    horoscopes: {
      url: "https://stage-widgets-yahoo-horoscope.merino.nonprod.webservices.mozgcp.net/1.2.20261022081515/index.html",
    },
  },
};

const renderWidget = (dispatch = jest.fn(), props = {}, state = mockState) => {
  const handleUserInteraction = props.handleUserInteraction || jest.fn();
  const tree = treeState => (
    <WrapWithProvider state={treeState}>
      <Horoscopes
        dispatch={dispatch}
        handleUserInteraction={handleUserInteraction}
        widgetsMayBeMaximized={true}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
  const { container, rerender } = render(tree(state));
  const rerenderWithState = newState => rerender(tree(newState));
  return { container, dispatch, handleUserInteraction, rerenderWithState };
};

const FRAME_ORIGIN = new URL(mockState.FramedWidgets.horoscopes.url).origin;

// A new manifest url on the same origin, so the test helpers still match it.
const NEW_URL_STATE = {
  ...mockState,
  FramedWidgets: {
    horoscopes: {
      url: mockState.FramedWidgets.horoscopes.url.replace(
        "1.2.20261022081515",
        "1.13.20261119190859"
      ),
    },
  },
};

// jsdom ignores `source` in the MessageEvent init dict, so it is set on the
// instance.
const postFrameMessage = (container, data) => {
  const frame = container.querySelector("iframe.horoscopes-frame");
  const event = new MessageEvent("message", {
    data: { channel: "horoscopes_widget", ...data },
    origin: FRAME_ORIGIN,
  });
  Object.defineProperty(event, "source", { value: frame.contentWindow });
  act(() => {
    window.dispatchEvent(event);
  });
};

const postReady = (container, variant = "default_sign") =>
  postFrameMessage(container, {
    type: "widget_ready",
    payload: { protocol: 1, variant },
  });

const postTerminalError = container =>
  postFrameMessage(container, {
    type: "widget_error",
    payload: { reason: "render_failed", terminal: true },
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

// Call jest.useFakeTimers() first; the afterEach below restores real ones.
const timeOut = () =>
  act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));

const clickLearnMore = container =>
  fireEvent.click(
    container.querySelector(
      "panel-item[data-l10n-id='newtab-horoscopes-menu-learn-more']"
    )
  );

describe("Horoscopes widget", () => {
  afterEach(() => {
    jest.useRealTimers();
  });

  it("loads the manifest url in a sandboxed iframe labelled by the title", () => {
    const { container } = renderWidget();
    const frame = container.querySelector("iframe.horoscopes-frame");
    expect(frame).toHaveAttribute(
      "src",
      mockState.FramedWidgets.horoscopes.url
    );
    expect(frame).toHaveAttribute("aria-labelledby", "horoscopes-widget-label");
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
    expect(container.querySelector("article.horoscopes")).toBeNull();
  });

  it("renders the widget at the resolved size", () => {
    const { container } = renderWidget();
    const root = container.querySelector("article.horoscopes");
    expect(root).toBeTruthy();
    expect(root.className).toContain("medium-widget");
  });

  it("renders the localized title", () => {
    const { container } = renderWidget();
    const title = container.querySelector("#horoscopes-widget-label");
    expect(title.classList).toContain("newtab-widget-title");
    expect(title.getAttribute("data-l10n-id")).toBe(
      "newtab-horoscopes-widget-title"
    );
  });

  it("renders a labelled context menu button", () => {
    const { container } = renderWidget();
    expect(
      container.querySelector(
        ".horoscopes-context-menu-button[data-l10n-id='newtab-horoscopes-widget-open-menu-button']"
      )
    ).toBeTruthy();
  });

  it("shows the New badge until the widget has been interacted with", () => {
    const { container } = renderWidget();
    expect(container.querySelector(".horoscopes-new-badge")).toBeTruthy();
  });

  it("hides the New badge once the interaction pref is set", () => {
    const interacted = {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.horoscopes.interaction": true,
        },
      },
    };
    const { container } = renderWidget(jest.fn(), {}, interacted);
    expect(container.querySelector(".horoscopes-new-badge")).toBeNull();
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
      container.querySelector("panel-list[id='horoscopes-size-submenu']")
    ).toBeNull();
  });

  it("puts Powered by Yahoo first, above the shared footer divider", () => {
    const { container } = renderWidget();
    const menu = container.querySelector(
      "panel-list[id='horoscopes-context-menu']"
    );
    expect(menu).toBeTruthy();
    const [first, second] = menu.children;
    expect(first.getAttribute("data-l10n-id")).toBe(
      "newtab-horoscopes-menu-powered-by-yahoo"
    );
    expect(second.tagName).toBe("HR");
    expect(
      menu.querySelector("panel-item[data-l10n-id='newtab-widget-menu-hide']")
    ).toBeTruthy();
    expect(
      menu.querySelector(
        "panel-item[data-l10n-id='newtab-horoscopes-menu-learn-more']"
      )
    ).toBeTruthy();
  });

  describe("context menu actions", () => {
    it("picking a size writes the pref and records change_size", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "#horoscopes-size-submenu panel-item[data-size='large']"
        )
      );

      const setPref = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.SET_PREF &&
          action.data?.name === "widgets.horoscopes.size"
      );
      expect(setPref[0].data.value).toBe("large");

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "change_size"
      );
      // widget_size must be the NEW size, not the pre-change one.
      expect(userEvent[0].data).toMatchObject({
        widget_name: "horoscopes",
        widget_source: "context_menu",
        action_value: "large",
        widget_size: "large",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("horoscopes");
    });

    it("Powered by Yahoo opens yahoo.com in a new tab and records the click", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-horoscopes-menu-powered-by-yahoo']"
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
        widget_name: "horoscopes",
        widget_source: "context_menu",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("horoscopes");
    });

    it("Learn more records the event without opening the link itself", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-horoscopes-menu-learn-more']"
        )
      );

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "learn_more"
      );
      expect(userEvent[0].data).toMatchObject({
        widget_name: "horoscopes",
        widget_source: "context_menu",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("horoscopes");
    });

    it("leaves widget_variant off when widget_ready comes after the load timeout", () => {
      jest.useFakeTimers();
      const { container, dispatch } = renderWidget();
      timeOut();
      postReady(container);
      clickLearnMore(container);
      const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(event.data.user_action).toBe("learn_more");
      expect(event.data).not.toHaveProperty("widget_variant");
    });

    it("keeps widget_variant after a terminal widget_error that follows widget_ready", () => {
      const { container, dispatch } = renderWidget();
      postReady(container, "custom_sign");
      postTerminalError(container);
      clickLearnMore(container);
      const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(event.data.widget_variant).toBe("custom_sign");
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
      expect(observer.observed).toHaveLength(0);

      postFrameMessage(container, {
        type: "widget_ready",
        payload: { protocol: 1, variant: "custom_sign" },
      });
      const [target] = observer.observed;
      observer.callback([{ isIntersecting: true, target }], observer);
      observer.callback([{ isIntersecting: true, target }], observer);

      const impressions = dispatch.mock.calls.filter(
        ([action]) => action?.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
      expect(impressions[0][0].data).toMatchObject({
        widget_name: "horoscopes",
        widget_size: "medium",
        widget_variant: "custom_sign",
      });
    });

    it.each([
      ["an unlisted variant", { protocol: 1, variant: "leo" }],
      ["no variant", { protocol: 1 }],
    ])("leaves widget_variant off the impression for %s", (_label, payload) => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch);
      postFrameMessage(container, { type: "widget_ready", payload });
      const [observer] = observerInstances;
      const [target] = observer.observed;
      observer.callback([{ isIntersecting: true, target }], observer);

      const impressions = dispatch.mock.calls.filter(
        ([action]) => action?.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
      expect(impressions[0][0].data).not.toHaveProperty("widget_variant");
    });

    it("still dispatches WIDGETS_IMPRESSION, without a variant, once the frame times out", () => {
      jest.useFakeTimers();
      const dispatch = jest.fn();
      renderWidget(dispatch);
      const [observer] = observerInstances;
      expect(observer.observed).toEqual([]);

      timeOut();
      const [target] = observer.observed;
      observer.callback([{ isIntersecting: true, target }], observer);

      const [impression] = actionsOfType(dispatch, at.WIDGETS_IMPRESSION);
      expect(impression.data.widget_name).toBe("horoscopes");
      expect(impression.data).not.toHaveProperty("widget_variant");
    });

    it("waits for widget_ready again when the manifest url changes after a timeout", () => {
      jest.useFakeTimers();
      const { rerenderWithState } = renderWidget();
      const [observer] = observerInstances;
      timeOut();
      expect(observer.observed).toHaveLength(1);

      rerenderWithState(NEW_URL_STATE);
      expect(observer.observed).toEqual([]);
    });
  });

  describe("frame telemetry", () => {
    it.each([
      [{ user_action: "sign_select" }],
      [{ user_action: "range_change", action_value: "day" }],
      [{ user_action: "range_change", action_value: "week" }],
      [{ user_action: "range_change", action_value: "month" }],
      [{ user_action: "read_more_click" }],
    ])("records %o and clears the New badge", payload => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      postReady(container);
      postUserEvent(container, payload);

      const [event] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT);
      expect(event.data).toMatchObject({
        widget_name: "horoscopes",
        widget_source: "iframe",
        user_action: payload.user_action,
      });
      expect(event.data.action_value).toBe(payload.action_value);
      expect(handleUserInteraction).toHaveBeenCalledWith("horoscopes");
    });

    it.each([
      ["a hover", { user_action: "sign_info_hover" }],
      [
        "a sign as the value",
        { user_action: "sign_select", action_value: "leo" },
      ],
      [
        "an unknown range",
        { user_action: "range_change", action_value: "year" },
      ],
    ])("drops %s and leaves the New badge", (_label, payload) => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      postReady(container);
      postUserEvent(container, payload);
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
      expect(handleUserInteraction).not.toHaveBeenCalled();
    });

    const reportedVariants = dispatch =>
      actionsOfType(dispatch, at.WIDGETS_USER_EVENT).map(
        event => event.data.widget_variant
      );

    it("reports custom_sign on events after sign_select", () => {
      const { container, dispatch } = renderWidget();
      postReady(container, "default_sign");
      postUserEvent(container, { user_action: "sign_select" });
      postUserEvent(container, {
        user_action: "range_change",
        action_value: "week",
      });
      clickLearnMore(container);
      expect(reportedVariants(dispatch)).toEqual([
        "default_sign",
        "custom_sign",
        "custom_sign",
      ]);
    });

    it("goes back to widget_ready's variant when the manifest url changes", () => {
      const { container, dispatch, rerenderWithState } = renderWidget();
      postReady(container, "default_sign");
      postUserEvent(container, { user_action: "sign_select" });
      rerenderWithState(NEW_URL_STATE);
      postReady(container, "default_sign");
      clickLearnMore(container);
      expect(reportedVariants(dispatch)).toEqual([
        "default_sign",
        "default_sign",
      ]);
    });

    it("drops user events before widget_ready", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      postUserEvent(container, { user_action: "sign_select" });
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
      expect(handleUserInteraction).not.toHaveBeenCalled();
    });

    it("drops user events after a terminal widget_error", () => {
      const { container, dispatch } = renderWidget();
      postReady(container);
      postTerminalError(container);
      postUserEvent(container, { user_action: "sign_select" });
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
    });

    it("drops user events when widget_ready arrives after the load timeout", () => {
      jest.useFakeTimers();
      const { container, dispatch } = renderWidget();
      timeOut();
      postReady(container);
      postUserEvent(container, { user_action: "sign_select" });
      expect(actionsOfType(dispatch, at.WIDGETS_USER_EVENT)).toHaveLength(0);
    });

    it("records a terminal widget_error and stays on the page", () => {
      const { container, dispatch } = renderWidget();
      postTerminalError(container);
      const [error] = actionsOfType(dispatch, at.WIDGETS_ERROR);
      expect(error.data).toMatchObject({
        widget_name: "horoscopes",
        error_type: "render_failed",
      });
      expect(
        actionsOfType(dispatch, at.FRAMED_WIDGET_UNAVAILABLE)
      ).toHaveLength(0);
      expect(container.querySelector("article.horoscopes")).toBeTruthy();
    });

    it("records load_timeout without widget_ready and stays on the page", () => {
      jest.useFakeTimers();
      const { container, dispatch } = renderWidget();
      timeOut();
      const [error] = actionsOfType(dispatch, at.WIDGETS_ERROR);
      expect(error.data.error_type).toBe("load_timeout");
      expect(
        actionsOfType(dispatch, at.FRAMED_WIDGET_UNAVAILABLE)
      ).toHaveLength(0);
      expect(container.querySelector("article.horoscopes")).toBeTruthy();
    });
  });
});
