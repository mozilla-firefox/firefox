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

      expect(dispatch).toHaveBeenCalledWith({ type: at.SHOW_PERSONALIZE });
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
