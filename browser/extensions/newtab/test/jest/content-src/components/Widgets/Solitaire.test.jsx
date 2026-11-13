/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Solitaire } from "content-src/components/Widgets/Solitaire/Solitaire";
import { WrapWithProvider } from "test/jest/test-utils";

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
        widget_name: "solitaire",
        widget_size: "medium",
      });
    });
  });
  describe("expanded view", () => {
    function postAction(container, message) {
      const frame = container.querySelector("iframe.solitaire-frame");
      const event = new MessageEvent("message", {
        data: {
          channel: "solitaire_widget",
          type: "action",
          message,
          payload: { game: "solitaire" },
          sentAt: "",
        },
        origin: new URL(mockState.FramedWidgets.solitaire.url).origin,
      });
      Object.defineProperty(event, "source", { value: frame.contentWindow });
      act(() => {
        window.dispatchEvent(event);
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
      const { container } = renderWidget();
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
    });

    it("reloads the frame when the host closes the modal", () => {
      const { container } = renderWidget();
      const frame = container.querySelector("iframe.solitaire-frame");
      postAction(container, "start_game");
      fireEvent.click(container.querySelector(".solitaire-dialog-close"));
      expect(container.querySelector(".solitaire-title-wrapper").hidden).toBe(
        false
      );
      expect(container.querySelector("iframe.solitaire-frame")).not.toBe(frame);
    });

    it("reloads the frame when Esc or a backdrop click closes the modal", () => {
      const { container } = renderWidget();
      const frame = container.querySelector("iframe.solitaire-frame");
      postAction(container, "start_game");
      const dialog = container.querySelector("dialog.solitaire-dialog");
      expect(dialog).toHaveAttribute("closedby", "any");
      act(() => {
        fireEvent(dialog, new Event("close"));
      });
      expect(container.querySelector("iframe.solitaire-frame")).not.toBe(frame);
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
