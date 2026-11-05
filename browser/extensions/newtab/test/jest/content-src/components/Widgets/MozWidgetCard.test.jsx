/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { WIDGET_REGISTRY } from "common/WidgetsRegistry.mjs";
import { MozWidgetCard } from "content-src/components/Widgets/MozWidgetCard";
import { WrapWithProvider } from "test/jest/test-utils";

const ENTRY = {
  id: "extensionWidget",
  telemetryName: "extension_widget",
  enabledPref: "widgets.extensionWidget.enabled",
  sizePref: "widgets.extensionWidget.size",
  validSizes: ["medium", "large"],
  extensionID: "fixture-widget@mozilla.org",
  name: "Tally",
};

function cardElement({
  entry = {},
  dispatch,
  prefs = {},
  src = "moz-extension://uuid/widget.html",
  size = "medium",
  displaySize = size,
  props = {},
}) {
  const state = {
    ...INITIAL_STATE,
    Prefs: { ...INITIAL_STATE.Prefs, values: prefs },
  };
  return (
    <WrapWithProvider state={state}>
      <MozWidgetCard
        dispatch={dispatch}
        entry={{ ...ENTRY, ...entry }}
        size={size}
        displaySize={displaySize}
        src={src}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
}

function renderCard(entry, prefs, src, size, displaySize, props) {
  const dispatch = jest.fn();
  const { container, rerender } = render(
    cardElement({ entry, dispatch, prefs, src, size, displaySize, props })
  );
  const card = container.querySelector("moz-widget");
  const header = card.querySelector("moz-widget-header");
  const menu = card.querySelector(':scope > panel-list[slot="menu"]');
  return { card, dispatch, header, menu, rerender };
}

function findUserEvent(dispatch, userAction) {
  return dispatch.mock.calls.find(
    ([action]) =>
      action.type === at.WIDGETS_USER_EVENT &&
      action.data.user_action === userAction
  )?.[0];
}

function menuItems(menu) {
  return [...menu.children].map(el => el.dataset.l10nId ?? el.localName);
}

describe("<MozWidgetCard>", () => {
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

  it("draws the entry's icon before its name in the header", () => {
    const { header } = renderCard({ icon: "chrome://test/content/icon.png" });
    const icon = header.querySelector(':scope > img[slot="leading"]');
    expect(icon.getAttribute("src")).toBe("chrome://test/content/icon.png");
    expect(icon.getAttribute("alt")).toBe("");
  });

  it("shows a placeholder without a document", () => {
    const { card } = renderCard({}, {}, "");
    expect(
      card.querySelector(':scope > p[data-l10n-id="newtab-widget-placeholder"]')
    ).not.toBeNull();
  });

  it("shows no placeholder with a document", () => {
    const { card } = renderCard();
    expect(card.querySelector(":scope > p")).toBeNull();
  });

  it("lets the page load count as ready", () => {
    const { card } = renderCard();
    expect(card).toHaveAttribute("ready-on-load", "");
  });

  it("leaves ready to a card that says so", () => {
    const { card } = renderCard({}, {}, undefined, "medium", "medium", {
      readyOnLoad: false,
    });
    expect(card).not.toHaveAttribute("ready-on-load");
  });

  it("draws no icon for an entry without one", () => {
    const { header } = renderCard();
    expect(header.querySelector("img")).toBeNull();
  });

  it("renders the shared widget items", () => {
    const { menu } = renderCard();
    expect(menuItems(menu)).toEqual([
      "panel-item",
      "newtab-widget-menu-hide",
      "newtab-widget-section-menu-learn-more",
    ]);
  });

  it("localizes the card and header with the entry's l10nId", () => {
    const { card, header } = renderCard({ l10nId: "tally-name" });
    expect(card).toHaveAttribute("data-l10n-id", "tally-name");
    expect(header).toHaveAttribute("data-l10n-id", "tally-name");
  });

  it("titles the card with the widget's name", () => {
    const { card } = renderCard();
    expect(card).toHaveAttribute("title", "Tally");
  });

  it("renders only Hide and Learn more without sizes", () => {
    const { menu } = renderCard({ validSizes: ["large"] });
    expect(menuItems(menu)).toEqual([
      "newtab-widget-menu-hide",
      "newtab-widget-section-menu-learn-more",
    ]);
  });

  it("writes the size pref and records change_size", () => {
    const { dispatch, menu } = renderCard();
    fireEvent.click(menu.querySelector("panel-item[data-size='large']"));

    expect(dispatch).toHaveBeenCalledWith(
      expect.objectContaining({
        type: at.SET_PREF,
        data: { name: ENTRY.sizePref, value: "large" },
      })
    );
    expect(findUserEvent(dispatch, "change_size").data).toMatchObject({
      widget_name: "extension_widget",
      widget_source: "context_menu",
      action_value: "large",
      widget_size: "large",
    });
  });

  it("records learn_more", () => {
    const { dispatch, menu } = renderCard();
    fireEvent.click(
      menu.querySelector(
        "panel-item[data-l10n-id='newtab-widget-section-menu-learn-more']"
      )
    );

    expect(findUserEvent(dispatch, "learn_more").data).toMatchObject({
      widget_name: "extension_widget",
      widget_source: "context_menu",
    });
  });

  it("draws the card at its display size", () => {
    const { card } = renderCard({}, {}, undefined, "medium", "large");
    expect(card).toHaveAttribute("size", "large");
  });

  it("draws menu items above the shared items", () => {
    const { menu } = renderCard({}, {}, undefined, "medium", "medium", {
      menuItems: <panel-item data-l10n-id="tally-extra" />,
    });
    expect(menuItems(menu)).toEqual([
      "tally-extra",
      "hr",
      "panel-item",
      "newtab-widget-menu-hide",
      "newtab-widget-section-menu-learn-more",
    ]);
  });

  it("tells onChangeSize the size the user chooses", () => {
    const onChangeSize = jest.fn();
    const { menu } = renderCard({}, {}, undefined, "medium", "medium", {
      onChangeSize,
    });
    fireEvent.click(menu.querySelector("panel-item[data-size='large']"));
    expect(onChangeSize).toHaveBeenCalledWith("large");
  });

  it("sets cardRef to the moz-widget", () => {
    const cardRef = { current: null };
    const { card } = renderCard({}, {}, undefined, "medium", "medium", {
      cardRef,
    });
    expect(cardRef.current).toBe(card);
  });

  describe("impression telemetry", () => {
    it("records an impression of the card", () => {
      const { card, dispatch } = renderCard();
      const [observer] = observerInstances;
      expect(observer.observed).toEqual([card]);

      act(() => {
        observer.callback([{ isIntersecting: true, target: card }], observer);
      });

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_IMPRESSION,
          data: { widget_name: "extension_widget", widget_size: "medium" },
        })
      );
    });
  });

  describe("ready telemetry", () => {
    function see(card) {
      const [observer] = observerInstances;
      act(() => {
        observer.callback([{ isIntersecting: true, target: card }], observer);
      });
    }

    function reportReady(card) {
      act(() => {
        card.dispatchEvent(new CustomEvent("widget-ready"));
      });
    }

    function readys(dispatch) {
      return dispatch.mock.calls.filter(
        ([action]) => action.type === at.WIDGETS_READY
      );
    }

    it("records the widget once it reports ready and the card is seen", () => {
      const { card, dispatch } = renderCard();
      reportReady(card);
      see(card);

      expect(readys(dispatch)).toHaveLength(1);
      expect(readys(dispatch)[0][0].data).toMatchObject({
        widget_name: "extension_widget",
        widget_size: "medium",
      });
    });

    it("records the widget seen before it reports ready", () => {
      const { card, dispatch } = renderCard();
      see(card);
      reportReady(card);

      expect(readys(dispatch)).toHaveLength(1);
    });

    it("records nothing for a widget the user never saw", () => {
      const { card, dispatch } = renderCard();
      reportReady(card);

      expect(readys(dispatch)).toHaveLength(0);
    });

    it("records nothing until the widget reports itself ready", () => {
      const { card, dispatch } = renderCard();
      see(card);

      expect(readys(dispatch)).toHaveLength(0);
    });

    it("does not record ready while the widget has failed", () => {
      const { card, dispatch } = renderCard();
      reportReady(card);
      act(() => {
        card.dispatchEvent(new CustomEvent("widget-error"));
      });
      see(card);

      expect(readys(dispatch)).toHaveLength(0);

      reportReady(card);
      expect(readys(dispatch)).toHaveLength(1);
    });

    it("does not record ready from the previous document", () => {
      const { card, dispatch, rerender } = renderCard();
      reportReady(card);
      rerender(
        cardElement({ dispatch, src: "moz-extension://uuid/other.html" })
      );
      see(card);

      expect(readys(dispatch)).toHaveLength(0);

      reportReady(card);
      expect(readys(dispatch)).toHaveLength(1);
    });

    it("does not record a second time when the widget recovers", () => {
      const { card, dispatch } = renderCard();
      reportReady(card);
      see(card);
      act(() => {
        card.dispatchEvent(new CustomEvent("widget-error"));
      });
      reportReady(card);

      expect(readys(dispatch)).toHaveLength(1);
    });

    it("does not record a second time when the effect re-runs", () => {
      const { card, dispatch, rerender } = renderCard();
      reportReady(card);
      see(card);

      // A different dispatch function rebuilds recordReady, which the effect
      // depends on, so the effect runs again with both conditions still true.
      // Only readyRecorded stops a second event.
      rerender(cardElement({ dispatch: (...args) => dispatch(...args) }));

      expect(readys(dispatch)).toHaveLength(1);
    });

    it("records the size the impression used, not a later one", () => {
      const { card, dispatch, rerender } = renderCard();
      see(card);
      rerender(cardElement({ dispatch, size: "large" }));
      reportReady(card);

      expect(readys(dispatch)[0][0].data.widget_size).toBe("medium");
    });
  });

  // moz-widget decides the widget failed and signals it with widget-error and
  // widget-ready. The card only mounts and unmounts the message.
  describe("the error message", () => {
    function seeError(card) {
      const error = card.querySelector(".widget-error");
      const observer = observerInstances.at(-1);
      observer.callback([{ isIntersecting: true, target: error }], observer);
    }

    function errors(dispatch) {
      return dispatch.mock.calls.filter(
        ([action]) => action.type === at.WIDGETS_ERROR
      );
    }

    function reportError(card) {
      act(() => {
        card.dispatchEvent(new CustomEvent("widget-error"));
      });
    }

    function reportReady(card) {
      act(() => {
        card.dispatchEvent(new CustomEvent("widget-ready"));
      });
    }

    it("shows nothing until the widget reports a failure", () => {
      const { card } = renderCard();
      expect(card.querySelector(".widget-error")).toBeNull();
    });

    it("shows the generic message on widget-error", () => {
      const { card } = renderCard();
      reportError(card);

      expect(card.querySelector(".widget-error")).toHaveAttribute(
        "slot",
        "error"
      );
      expect(card.querySelector(".widget-error-text")).toHaveAttribute(
        "data-l10n-id",
        "newtab-widget-error-not-available"
      );
    });

    it("gives the message the widget's name, for the announcement", () => {
      const { card } = renderCard();
      reportError(card);

      expect(card.querySelector(".widget-error-title")).toHaveTextContent(
        "Tally"
      );
    });

    it("uses the entry's own message when it has one", () => {
      const { card } = renderCard({
        errorL10nId: "newtab-stocks-error-not-available",
      });
      reportError(card);

      expect(card.querySelector(".widget-error-text")).toHaveAttribute(
        "data-l10n-id",
        "newtab-stocks-error-not-available"
      );
    });

    it("takes the message away again on widget-ready", () => {
      const { card } = renderCard();
      reportError(card);
      reportReady(card);

      expect(card.querySelector(".widget-error")).toBeNull();
    });

    it("drops the error message for a new document", () => {
      const { card, dispatch, rerender } = renderCard();
      reportError(card);
      expect(card.querySelector(".widget-error")).not.toBeNull();
      seeError(card);
      expect(errors(dispatch)).toHaveLength(1);

      rerender(
        cardElement({ dispatch, src: "moz-extension://uuid/other.html" })
      );

      expect(card.querySelector(".widget-error")).toBeNull();
      expect(errors(dispatch)).toHaveLength(1);

      reportError(card);
      seeError(card);
      expect(errors(dispatch)).toHaveLength(2);
    });

    it("drops the error message when the document goes", () => {
      const { card, dispatch, rerender } = renderCard();
      reportError(card);
      expect(card.querySelector(".widget-error")).not.toBeNull();
      seeError(card);
      expect(errors(dispatch)).toHaveLength(1);

      rerender(cardElement({ dispatch, src: "" }));

      expect(card.querySelector(".widget-error")).toBeNull();
      expect(errors(dispatch)).toHaveLength(1);
      expect(
        card.querySelector(
          ':scope > p[data-l10n-id="newtab-widget-placeholder"]'
        )
      ).not.toBeNull();
      expect(
        dispatch.mock.calls.filter(
          ([action]) => action.type === at.WIDGETS_READY
        )
      ).toHaveLength(0);
    });

    it("names the error after a widget without a name", () => {
      const entry = {
        ...WIDGET_REGISTRY.find(w => w.id === "wrexhamAFC"),
        name: undefined,
      };
      const { card } = renderCard(entry);
      card.title = "Wrexham AFC";
      reportError(card);

      expect(card.querySelector(".widget-error h2")).toHaveTextContent(
        "Wrexham AFC"
      );
    });

    it("names the error after the widget's name first", () => {
      const { card } = renderCard();
      card.title = "Something else";
      reportError(card);

      expect(card.querySelector(".widget-error h2")).toHaveTextContent("Tally");
    });

    it("leaves src alone, since moz-widget covers its own frame", () => {
      const { card } = renderCard();
      reportError(card);

      expect(card).toHaveAttribute("src", "moz-extension://uuid/widget.html");
    });

    it("records load_error once the message is on screen", () => {
      const { card, dispatch } = renderCard();
      reportError(card);
      const error = card.querySelector(".widget-error");
      const observer = observerInstances.at(-1);
      expect(observer.observed).toEqual([error]);

      observer.callback([{ isIntersecting: true, target: error }], observer);
      observer.callback([{ isIntersecting: true, target: error }], observer);

      const errors = dispatch.mock.calls.filter(
        ([action]) => action.type === at.WIDGETS_ERROR
      );
      expect(errors).toHaveLength(1);
      expect(errors[0][0].data).toMatchObject({
        widget_name: "extension_widget",
        widget_size: "medium",
        error_type: "load_error",
      });
    });

    it("records the widget's own reason as the error type", () => {
      const { card, dispatch } = renderCard();
      act(() => {
        card.dispatchEvent(
          new CustomEvent("widget-error", {
            detail: { reason: "render_failed" },
          })
        );
      });
      seeError(card);

      expect(errors(dispatch)).toHaveLength(1);
      expect(errors(dispatch)[0][0].data).toMatchObject({
        error_type: "render_failed",
      });
    });

    it("records a widget that fails, recovers and fails again twice", () => {
      const { card, dispatch } = renderCard();
      const see = () => {
        const error = card.querySelector(".widget-error");
        const observer = observerInstances.at(-1);
        observer.callback([{ isIntersecting: true, target: error }], observer);
      };

      reportError(card);
      see();
      reportReady(card);
      reportError(card);
      see();

      const errors = dispatch.mock.calls.filter(
        ([action]) => action.type === at.WIDGETS_ERROR
      );
      expect(errors).toHaveLength(2);
    });

    it("records nothing for a failure the user never saw", () => {
      const { card, dispatch } = renderCard();
      reportError(card);
      reportReady(card);

      const errors = dispatch.mock.calls.filter(
        ([action]) => action.type === at.WIDGETS_ERROR
      );
      expect(errors).toHaveLength(0);
    });
  });
});
