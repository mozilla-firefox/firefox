/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { FramedWidgetCard } from "content-src/components/Widgets/FramedWidgetCard";

const ENTRY = {
  id: "tallyWidget",
  telemetryName: "extension_widget",
  enabledPref: "widgets.tally.enabled",
  sizePref: "widgets.tally.size",
  validSizes: ["medium", "large"],
  card: "FramedWidgetCard",
  name: "Tally",
};

function renderCard(
  entry = {},
  prefs = {},
  src = "moz-extension://uuid/widget.html",
  size = "medium"
) {
  const dispatch = jest.fn();
  const store = createStore(combineReducers(reducers), {
    ...INITIAL_STATE,
    Prefs: { ...INITIAL_STATE.Prefs, values: prefs },
  });
  const cardFor = cardSrc => (
    <Provider store={store}>
      <FramedWidgetCard
        dispatch={dispatch}
        entry={{ ...ENTRY, ...entry }}
        size={size}
        src={cardSrc}
        widgetEnabledMap={{}}
      />
    </Provider>
  );
  const { container, rerender } = render(cardFor(src));
  const card = container.querySelector("moz-widget");
  const menu = card.querySelector(':scope > panel-list[slot="menu"]');
  return {
    card,
    dispatch,
    menu,
    store,
    rerenderWithSrc: newSrc => rerender(cardFor(newSrc)),
  };
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

const MENU = [
  {
    type: "choice",
    id: "view",
    l10nId: "tally-view",
    pref: "widgets.tally.view",
    options: [
      { value: "day", l10nId: "tally-view-day" },
      { value: "week", l10nId: "tally-view-week", size: "large" },
    ],
  },
  {
    type: "action",
    id: "wallpapers",
    l10nId: "tally-wallpapers",
    action: "show-customize",
  },
];

describe("<FramedWidgetCard>", () => {
  it("leaves ready to its web page", () => {
    const { card } = renderCard({}, {}, "https://widget.example/doc.html");
    expect(card).not.toHaveAttribute("ready-on-load");
  });

  describe("widget messages", () => {
    let iframe;
    let frames;
    let originalIntersectionObserver;
    let observerInstances;

    function appendFrame() {
      const frame = document.createElement("iframe");
      document.body.appendChild(frame);
      frames.push(frame);
      return frame;
    }

    beforeEach(() => {
      frames = [];
      iframe = appendFrame();
      observerInstances = [];
      originalIntersectionObserver = global.IntersectionObserver;
      global.IntersectionObserver = class MockIntersectionObserver {
        constructor(callback) {
          this.callback = callback;
          observerInstances.push(this);
        }
        observe() {}
        unobserve() {}
        disconnect() {}
      };
    });

    afterEach(() => {
      frames.forEach(frame => frame.remove());
      global.IntersectionObserver = originalIntersectionObserver;
    });

    function renderWebCard() {
      return renderCard({}, {}, "https://widget.example/doc.html");
    }

    function stubReports(card) {
      card.frame = iframe;
      card.reportError = jest.fn(() => {
        card.error = true;
      });
      card.reportReady = jest.fn();
    }

    function postWidgetMessage({
      data = {
        channel: "extension_widget_widget",
        type: "widget_error",
        payload: { reason: "render_failed", terminal: true },
      },
      origin = "https://widget.example",
      source = iframe.contentWindow,
    } = {}) {
      const event = new MessageEvent("message", { data, origin });
      Object.defineProperty(event, "source", {
        configurable: true,
        value: source,
      });
      act(() => {
        window.dispatchEvent(event);
      });
    }

    it("reports the widget's own error and ready", () => {
      const { card } = renderWebCard();
      stubReports(card);

      postWidgetMessage({
        data: {
          channel: "extension_widget_widget",
          type: "widget_ready",
          payload: { protocol: 1 },
        },
      });
      expect(card.reportReady).toHaveBeenCalledTimes(1);
      postWidgetMessage();
      expect(card.reportError).toHaveBeenCalledTimes(1);
      expect(card.reportError).toHaveBeenCalledWith("render_failed");
    });

    it("keeps a terminal error when the page then reports ready", () => {
      const { card } = renderWebCard();
      stubReports(card);

      postWidgetMessage();
      postWidgetMessage({
        data: {
          channel: "extension_widget_widget",
          type: "widget_ready",
          payload: { protocol: 1 },
        },
      });
      expect(card.reportError).toHaveBeenCalledTimes(1);
      expect(card.reportReady).not.toHaveBeenCalled();
    });

    it("ignores messages from anywhere else", () => {
      const { card } = renderWebCard();
      stubReports(card);

      for (const message of [
        {
          data: {
            channel: "other",
            type: "widget_error",
            payload: { reason: "render_failed", terminal: true },
          },
        },
        {
          data: {
            channel: "moz_widget",
            type: "widget_error",
            payload: { reason: "render_failed", terminal: true },
          },
        },
        { source: window },
        { source: null },
        { origin: "https://example.com" },
        { origin: "null" },
      ]) {
        postWidgetMessage(message);
        expect(card.reportError).not.toHaveBeenCalled();
        expect(card.reportReady).not.toHaveBeenCalled();
      }
      postWidgetMessage();
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });

    it("leaves the widget alone on an error it recovers from", () => {
      const { card } = renderWebCard();
      stubReports(card);

      for (const payload of [
        { reason: "uncaught_error", terminal: false },
        { reason: "uncaught_error" },
        { reason: "render_failed", terminal: "true" },
      ]) {
        postWidgetMessage({
          data: {
            channel: "extension_widget_widget",
            type: "widget_error",
            payload,
          },
        });
        expect(card.reportError).not.toHaveBeenCalled();
      }
    });

    it("ignores an error whose reason breaks the protocol", () => {
      const { card } = renderWebCard();
      stubReports(card);

      for (const reason of [
        undefined,
        5,
        "",
        "Render failed",
        "1st",
        "a".repeat(65),
      ]) {
        postWidgetMessage({
          data: {
            channel: "extension_widget_widget",
            type: "widget_error",
            payload: { reason, terminal: true },
          },
        });
        expect(card.reportError).not.toHaveBeenCalled();
      }
      postWidgetMessage();
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });

    it("ignores a message when the frame has no window", () => {
      const { card } = renderWebCard();
      stubReports(card);
      card.frame = { contentWindow: null };

      postWidgetMessage({ source: null });
      expect(card.reportError).not.toHaveBeenCalled();
      expect(card.reportReady).not.toHaveBeenCalled();
      card.frame = iframe;
      postWidgetMessage();
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });

    it("ignores malformed messages", () => {
      const { card } = renderWebCard();
      stubReports(card);

      for (const data of [
        null,
        "widget_error",
        { type: "widget_error" },
        { channel: "extension_widget_widget", type: "widget_error" },
      ]) {
        postWidgetMessage({ data });
        expect(card.reportError).not.toHaveBeenCalled();
        expect(card.reportReady).not.toHaveBeenCalled();
      }
      postWidgetMessage();
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });

    it("ignores the previous document", () => {
      const { card, rerenderWithSrc } = renderWebCard();
      stubReports(card);
      const oldWindow = iframe.contentWindow;

      rerenderWithSrc("https://widget.example/other.html");
      card.frame = appendFrame();
      postWidgetMessage({ source: oldWindow });
      expect(card.reportError).not.toHaveBeenCalled();
      expect(card.reportReady).not.toHaveBeenCalled();
      postWidgetMessage({ source: card.frame.contentWindow });
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });

    it("ignores messages once the document is gone", () => {
      const { card, rerenderWithSrc } = renderWebCard();
      stubReports(card);

      rerenderWithSrc("");
      postWidgetMessage();
      expect(card.reportError).not.toHaveBeenCalled();
      expect(card.reportReady).not.toHaveBeenCalled();
      rerenderWithSrc("https://other.example/doc.html");
      postWidgetMessage();
      expect(card.reportError).not.toHaveBeenCalled();
      expect(card.reportReady).not.toHaveBeenCalled();
      postWidgetMessage({ origin: "https://other.example" });
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });

    it("ignores a document with an opaque origin", () => {
      const { card } = renderCard({}, {}, "data:text/html,hi");
      stubReports(card);

      postWidgetMessage({ origin: "null" });
      expect(card.reportError).not.toHaveBeenCalled();
      expect(card.reportReady).not.toHaveBeenCalled();
    });

    it.each([159, 160])("reports nothing on %i", version => {
      const { card, dispatch } = renderWebCard();
      if (version === 160) {
        card.frame = iframe;
      }

      postWidgetMessage();
      postWidgetMessage({
        data: {
          channel: "extension_widget_widget",
          type: "widget_ready",
          payload: { protocol: 1 },
        },
      });
      const [observer] = observerInstances;
      act(() => {
        observer.callback([{ isIntersecting: true, target: card }], observer);
      });

      expect(card.querySelector(".widget-error")).toBeNull();
      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({ type: at.WIDGETS_IMPRESSION })
      );
      expect(
        dispatch.mock.calls.filter(
          ([action]) =>
            action.type === at.WIDGETS_ERROR || action.type === at.WIDGETS_READY
        )
      ).toHaveLength(0);

      stubReports(card);
      postWidgetMessage({
        data: {
          channel: "extension_widget_widget",
          type: "widget_ready",
          payload: { protocol: 1 },
        },
      });
      expect(card.reportReady).toHaveBeenCalledTimes(1);
      postWidgetMessage();
      expect(card.reportError).toHaveBeenCalledTimes(1);
    });
  });

  describe("menu items", () => {
    const checkedValues = menu =>
      [...menu.querySelectorAll("panel-item[data-value]")]
        .filter(item => item.hasAttribute("checked"))
        .map(item => item.dataset.value);

    it("draws the entry's items above the shared items", () => {
      const { menu } = renderCard({ menu: MENU });
      expect(menuItems(menu)).toEqual([
        "panel-item",
        "tally-wallpapers",
        "hr",
        "panel-item",
        "newtab-widget-menu-hide",
        "newtab-widget-section-menu-learn-more",
      ]);
    });

    it("checks a choice's first option until one is chosen", () => {
      const { menu } = renderCard({ menu: MENU });
      expect(checkedValues(menu)).toEqual(["day"]);
    });

    it("checks a choice's chosen option", () => {
      const { menu } = renderCard(
        { menu: MENU },
        { "widgets.tally.view": "week" }
      );
      expect(checkedValues(menu)).toEqual(["week"]);
    });

    it("passes the chosen values to the document in its URL fragment", () => {
      const { card } = renderCard(
        { menu: MENU },
        { "widgets.tally.view": "week" }
      );
      expect(card).toHaveAttribute(
        "src",
        "moz-extension://uuid/widget.html#view=week"
      );
    });

    it("checks the first option while the pref holds no option", () => {
      const { card, menu } = renderCard(
        { menu: MENU },
        { "widgets.tally.view": "month" }
      );
      expect(checkedValues(menu)).toEqual(["day"]);
      expect(card).toHaveAttribute(
        "src",
        "moz-extension://uuid/widget.html#view=day"
      );
    });

    it("replaces a URL fragment the document already has", () => {
      const { card } = renderCard(
        { menu: MENU },
        { "widgets.tally.view": "week" },
        "https://example.com/widget.html#theme=dark"
      );
      expect(card).toHaveAttribute(
        "src",
        "https://example.com/widget.html#view=week"
      );
    });

    it("leaves the document's URL alone without choices", () => {
      const { card } = renderCard();
      expect(card).toHaveAttribute("src", "moz-extension://uuid/widget.html");
    });

    it("writes a choice's pref and records change_<id>", () => {
      const { dispatch, menu } = renderCard({ menu: MENU });
      fireEvent.click(menu.querySelector("panel-item[data-value='week']"));

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.SET_PREF,
          data: { name: "widgets.tally.view", value: "week" },
        })
      );
      expect(findUserEvent(dispatch, "change_view").data).toMatchObject({
        widget_name: "extension_widget",
        widget_source: "context_menu",
        action_value: "week",
      });
    });

    const prefWrites = (dispatch, name) =>
      dispatch.mock.calls
        .filter(
          ([action]) => action.type === at.SET_PREF && action.data.name === name
        )
        .map(([action]) => action.data.value);

    it("leaves the size pref alone when an option is chosen", () => {
      const { dispatch, menu } = renderCard({ menu: MENU });
      fireEvent.click(menu.querySelector("panel-item[data-value='week']"));
      expect(prefWrites(dispatch, ENTRY.sizePref)).toEqual([]);
    });

    it("switches a choice to its first option that fits a size too small for it", () => {
      const { dispatch, menu } = renderCard(
        { menu: MENU },
        { "widgets.tally.view": "week" },
        undefined,
        "large"
      );
      fireEvent.click(menu.querySelector("panel-item[data-size='medium']"));
      expect(prefWrites(dispatch, ENTRY.sizePref)).toEqual(["medium"]);
      expect(prefWrites(dispatch, "widgets.tally.view")).toEqual(["day"]);
    });

    it("skips options that do not fit the new size either", () => {
      const view = {
        ...MENU[0],
        options: [
          { value: "week", l10nId: "tally-view-week", size: "large" },
          { value: "day", l10nId: "tally-view-day" },
        ],
      };
      const { dispatch, menu } = renderCard(
        { menu: [view] },
        { "widgets.tally.view": "week" },
        undefined,
        "large"
      );
      fireEvent.click(menu.querySelector("panel-item[data-size='medium']"));
      expect(prefWrites(dispatch, "widgets.tally.view")).toEqual(["day"]);
    });

    it("keeps a choice whose option fits the new size", () => {
      const { dispatch, menu } = renderCard(
        { menu: MENU },
        { "widgets.tally.view": "day" },
        undefined,
        "large"
      );
      fireEvent.click(menu.querySelector("panel-item[data-size='medium']"));
      expect(prefWrites(dispatch, "widgets.tally.view")).toEqual([]);
    });

    it("runs an action's host action and records its id", () => {
      const { dispatch, menu } = renderCard({ menu: MENU });
      fireEvent.click(
        menu.querySelector("panel-item[data-l10n-id='tally-wallpapers']")
      );

      expect(dispatch).toHaveBeenCalledWith({
        type: at.SHOW_PERSONALIZE,
        data: { source: "extension_widget" },
      });
      expect(findUserEvent(dispatch, "wallpapers").data).toMatchObject({
        widget_name: "extension_widget",
        widget_source: "context_menu",
      });
    });
  });

  describe("session history", () => {
    const VIEW_PREF = "widgets.tally.view";
    const setView = (store, value) =>
      act(() => {
        store.dispatch({
          type: at.PREF_CHANGED,
          data: { name: VIEW_PREF, value },
        });
      });

    it("passes a later choice with location.replace() instead of a new URL", () => {
      const { card, store } = renderCard({ menu: MENU });
      const replace = jest.fn();
      card.frame = { contentWindow: { location: { replace } } };

      setView(store, "week");
      expect(card).toHaveAttribute(
        "src",
        "moz-extension://uuid/widget.html#view=day"
      );
      expect(replace).toHaveBeenLastCalledWith(
        "moz-extension://uuid/widget.html#view=week"
      );

      setView(store, "day");
      expect(replace).toHaveBeenLastCalledWith(
        "moz-extension://uuid/widget.html#view=day"
      );
    });

    it("keeps the error when only a choice changes", () => {
      const { card, store } = renderCard({ menu: MENU });
      const replace = jest.fn();
      card.frame = { contentWindow: { location: { replace } } };
      act(() => {
        card.dispatchEvent(new CustomEvent("widget-error"));
      });
      expect(card.querySelector(".widget-error")).not.toBeNull();

      setView(store, "week");

      expect(card.querySelector(".widget-error")).not.toBeNull();
    });

    it("loads a new document normally", () => {
      const { card, rerenderWithSrc } = renderCard({ menu: MENU });
      const replace = jest.fn();
      card.frame = { contentWindow: { location: { replace } } };

      rerenderWithSrc("https://example.com/other.html");
      expect(card).toHaveAttribute(
        "src",
        "https://example.com/other.html#view=day"
      );
      expect(replace).not.toHaveBeenCalled();
    });
  });
});
