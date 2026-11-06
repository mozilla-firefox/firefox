/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Travel } from "content-src/components/Widgets/Travel/Travel";
import { WrapWithProvider } from "test/jest/test-utils";
import { FRAMED_WIDGET_LOAD_TIMEOUT_MS } from "content-src/components/Widgets/useFramedWidgetMessages";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.system.travel.enabled": true,
      "widgets.travel.enabled": true,
      "widgets.travel.size": "medium",
      "widgets.travel.interaction": false,
    },
  },
  FramedWidgets: {
    travel: {
      url: "https://stage-widgets-kayak-travel.merino.nonprod.webservices.mozgcp.net/1.0.202610272219/index.html",
    },
  },
};

const renderWidget = (dispatch = jest.fn(), props = {}, state = mockState) => {
  const handleUserInteraction = props.handleUserInteraction || jest.fn();
  const { container } = render(
    <WrapWithProvider state={state}>
      <Travel
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

describe("Travel widget", () => {
  // jsdom does not honor `source` from the MessageEvent init dict, so it is
  // forced onto the instance.
  function postFrameMessage(container, data) {
    const frame = container.querySelector("iframe.travel-frame");
    const event = new MessageEvent("message", {
      data: { channel: "travel_widget", sentAt: "", ...data },
      origin: new URL(mockState.FramedWidgets.travel.url).origin,
    });
    Object.defineProperty(event, "source", { value: frame.contentWindow });
    act(() => {
      window.dispatchEvent(event);
    });
  }

  const postChangeSize = (container, size) =>
    postFrameMessage(container, {
      type: "action",
      message: "change_size",
      payload: { size },
    });

  it("shows the frame only after widget_ready", () => {
    const { container } = renderWidget();
    const article = container.querySelector("article.travel");
    expect(article.hasAttribute("data-ready")).toBe(false);
    postFrameMessage(container, {
      type: "widget_ready",
      payload: { protocol: 1 },
    });
    expect(article.hasAttribute("data-ready")).toBe(true);
  });

  it("records load_timeout and hides itself without widget_ready", () => {
    jest.useFakeTimers();
    try {
      const { dispatch } = renderWidget();
      act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
      const actions = dispatch.mock.calls.map(([a]) => a);
      expect(
        actions.find(a => a.type === at.WIDGETS_ERROR)?.data.error_type
      ).toBe("load_timeout");
      expect(actions).toContainEqual({
        type: at.FRAMED_WIDGET_UNAVAILABLE,
        data: { id: "travel" },
      });
    } finally {
      jest.useRealTimers();
    }
  });

  it("grows to the size the frame asks for and counts it as an interaction", () => {
    const { container, handleUserInteraction } = renderWidget();
    postChangeSize(container, "large");
    expect(container.querySelector("article.travel").className).toContain(
      "large-widget"
    );
    expect(handleUserInteraction).toHaveBeenCalledWith("travel");
  });

  it("lets the user's Change size choice win over the frame's request", () => {
    const { container } = renderWidget();
    postChangeSize(container, "large");
    expect(
      container
        .querySelector("#travel-size-submenu panel-item[data-size='large']")
        .hasAttribute("checked")
    ).toBe(true);
    fireEvent.click(
      container.querySelector(
        "#travel-size-submenu panel-item[data-size='medium']"
      )
    );
    expect(container.querySelector("article.travel").className).toContain(
      "medium-widget"
    );
  });

  it("reports the size the frame asked for in telemetry", () => {
    const { container, dispatch } = renderWidget();
    postChangeSize(container, "large");
    fireEvent.click(
      container.querySelector(
        "panel-item[data-l10n-id='newtab-travel-menu-learn-more']"
      )
    );
    const userEvent = dispatch.mock.calls.find(
      ([action]) =>
        action?.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "learn_more"
    );
    expect(userEvent[0].data.widget_size).toBe("large");
  });

  it("keeps its size when the layout can't host a larger widget", () => {
    const { container } = renderWidget(jest.fn(), {
      widgetsMayBeMaximized: false,
    });
    postChangeSize(container, "large");
    expect(container.querySelector("article.travel").className).toContain(
      "medium-widget"
    );
  });

  it("loads the manifest url in a sandboxed iframe labelled by the title", () => {
    const { container } = renderWidget();
    const frame = container.querySelector("iframe.travel-frame");
    expect(frame).toHaveAttribute("src", mockState.FramedWidgets.travel.url);
    expect(frame).toHaveAttribute("aria-labelledby", "travel-widget-label");
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
    expect(container.querySelector("article.travel")).toBeNull();
  });

  it("renders the widget at the resolved size", () => {
    const { container } = renderWidget();
    const root = container.querySelector("article.travel");
    expect(root).toBeTruthy();
    expect(root.className).toContain("medium-widget");
  });

  it("renders the localized title", () => {
    const { container } = renderWidget();
    const title = container.querySelector("#travel-widget-label");
    expect(title.classList).toContain("newtab-widget-title");
    expect(title.getAttribute("data-l10n-id")).toBe(
      "newtab-travel-widget-title"
    );
  });

  it("renders a labelled context menu button", () => {
    const { container } = renderWidget();
    expect(
      container.querySelector(
        ".travel-context-menu-button[data-l10n-id='newtab-travel-widget-open-menu-button']"
      )
    ).toBeTruthy();
  });

  it("shows the New badge until the widget has been interacted with", () => {
    const { container } = renderWidget();
    expect(container.querySelector(".travel-new-badge")).toBeTruthy();
  });

  it("hides the New badge once the interaction pref is set", () => {
    const interacted = {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.travel.interaction": true,
        },
      },
    };
    const { container } = renderWidget(jest.fn(), {}, interacted);
    expect(container.querySelector(".travel-new-badge")).toBeNull();
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
      container.querySelector("panel-list[id='travel-size-submenu']")
    ).toBeNull();
  });

  it("puts Powered by Kayak first, above the shared menu footer", () => {
    const { container } = renderWidget();
    const menu = container.querySelector(
      "panel-list[id='travel-context-menu']"
    );
    expect(menu.firstElementChild).toHaveAttribute(
      "data-l10n-id",
      "newtab-travel-menu-powered-by-kayak"
    );
    expect(menu.firstElementChild.nextElementSibling.tagName).toBe("HR");
    expect(
      menu.querySelector("panel-item[data-l10n-id='newtab-widget-menu-hide']")
    ).toBeTruthy();
    expect(
      menu.querySelector(
        "panel-item[data-l10n-id='newtab-travel-menu-learn-more']"
      )
    ).toBeTruthy();
  });

  describe("context menu actions", () => {
    it("picking a size writes the pref and records change_size", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "#travel-size-submenu panel-item[data-size='large']"
        )
      );

      const setPref = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.SET_PREF &&
          action.data?.name === "widgets.travel.size"
      );
      expect(setPref[0].data.value).toBe("large");

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "change_size"
      );
      expect(userEvent[0].data).toMatchObject({
        widget_name: "travel",
        widget_source: "context_menu",
        action_value: "large",
        widget_size: "large",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("travel");
    });

    it("Powered by Kayak opens kayak.com in a new tab and records the click", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-travel-menu-powered-by-kayak']"
        )
      );

      const openLink = dispatch.mock.calls.find(
        ([action]) => action?.type === at.OPEN_LINK
      );
      expect(openLink[0].data).toEqual({
        url: "https://www.kayak.com/",
        where: "tab",
      });

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "powered_by_kayak"
      );
      expect(userEvent[0].data).toMatchObject({
        widget_name: "travel",
        widget_source: "context_menu",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("travel");
    });

    it("Learn more records the event", () => {
      const { container, dispatch, handleUserInteraction } = renderWidget();
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-travel-menu-learn-more']"
        )
      );

      const userEvent = dispatch.mock.calls.find(
        ([action]) =>
          action?.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "learn_more"
      );
      expect(userEvent[0].data).toMatchObject({
        widget_name: "travel",
        widget_source: "context_menu",
      });
      expect(handleUserInteraction).toHaveBeenCalledWith("travel");
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

    it("dispatches WIDGETS_IMPRESSION once, keyed on the telemetry name", () => {
      const dispatch = jest.fn();
      renderWidget(dispatch);
      const [observer] = observerInstances;
      const [target] = observer.observed;

      observer.callback([{ isIntersecting: true, target }], observer);
      observer.callback([{ isIntersecting: true, target }], observer);

      const impressions = dispatch.mock.calls.filter(
        ([action]) => action?.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
      expect(impressions[0][0].data).toMatchObject({
        widget_name: "travel",
        widget_size: "medium",
      });
    });
  });
});
