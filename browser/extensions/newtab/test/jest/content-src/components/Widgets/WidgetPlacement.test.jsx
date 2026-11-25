/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent } from "@testing-library/react";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import {
  PREF_HOME_WIDGETS,
  PREF_WIDGETS_ORDER,
} from "common/WidgetsRegistry.mjs";
import { WIDGET_PLACEMENTS } from "common/PageLayoutVariants.mjs";
import {
  HOME_USER_ACTIONS,
  WidgetHideMenuItem,
  WidgetPlacementContext,
  setWidgetOnHome,
} from "content-src/components/Widgets/WidgetPlacement";

// A thematic config whose library is "widgets", with the widgets these tests
// pin turned on.
const PLACEMENT_PREFS = {
  "nova.enabled": true,
  "pageLayouts.variant": "spaces-thematic-v1",
  trainhopConfig: {
    spaces: {
      order: ["home", "widgets"],
      default: "home",
      widgetLibrary: "widgets",
      spaces: {
        home: { label: "Home" },
        widgets: { label: "Widgets" },
      },
    },
  },
  "widgets.system.enabled": true,
  "widgets.enabled": true,
  "widgets.system.lists.enabled": true,
  "widgets.lists.enabled": true,
  "widgets.system.clocks.enabled": true,
  "widgets.clocks.enabled": true,
  "widgets.system.focusTimer.enabled": true,
  "widgets.focusTimer.enabled": true,
};

const setPrefs = dispatch =>
  Object.fromEntries(
    dispatch.mock.calls
      .map(([action]) => action)
      .filter(action => action.type === at.SET_PREF)
      .map(action => [action.data.name, action.data.value])
  );

function renderItem({ placement = null, prefs = {}, onHide = jest.fn() }) {
  const store = createStore(combineReducers(reducers), {
    ...INITIAL_STATE,
    Prefs: {
      ...INITIAL_STATE.Prefs,
      values: { ...INITIAL_STATE.Prefs.values, ...PLACEMENT_PREFS, ...prefs },
    },
  });
  jest.spyOn(store, "dispatch");
  const { container } = render(
    <Provider store={store}>
      <WidgetPlacementContext.Provider value={placement}>
        <panel-list>
          <WidgetHideMenuItem widgetId="lists" onHide={onHide} />
        </panel-list>
      </WidgetPlacementContext.Provider>
    </Provider>
  );
  return { item: container.querySelector("panel-item"), onHide, store };
}

describe("<WidgetHideMenuItem>", () => {
  it("turns the widget off outside Home placement", () => {
    const { item, onHide, store } = renderItem({});
    expect(item.getAttribute("data-l10n-id")).toBe("newtab-widget-menu-hide");
    fireEvent.click(item);
    expect(onHide).toHaveBeenCalledTimes(1);
    expect(setPrefs(store.dispatch)).toEqual({});
  });

  it("turns the widget off in the widget library", () => {
    const { item, onHide, store } = renderItem({
      placement: WIDGET_PLACEMENTS.LIBRARY,
    });
    expect(item.getAttribute("data-l10n-id")).toBe("newtab-widget-menu-hide");
    fireEvent.click(item);
    expect(onHide).toHaveBeenCalledTimes(1);
    expect(setPrefs(store.dispatch)).toEqual({});
  });

  it("moves the widget back to the library on Home without turning it off", () => {
    const { item, onHide, store } = renderItem({
      placement: WIDGET_PLACEMENTS.HOME,
      prefs: { [PREF_HOME_WIDGETS]: '["clocks","lists"]' },
    });
    expect(item.getAttribute("data-l10n-id")).toBe("newtab-widget-menu-hide");
    fireEvent.click(item);
    expect(onHide).not.toHaveBeenCalled();
    expect(setPrefs(store.dispatch)).toEqual({
      [PREF_HOME_WIDGETS]: '["clocks"]',
    });
  });
});

describe("setWidgetOnHome", () => {
  const prefsWith = homeList => ({
    ...PLACEMENT_PREFS,
    [PREF_HOME_WIDGETS]: JSON.stringify(homeList),
    [PREF_WIDGETS_ORDER]: "clocks,focusTimer,lists",
  });

  it("pins a widget first in the Home list and the widget order", () => {
    const dispatch = jest.fn();
    setWidgetOnHome(dispatch, prefsWith(["clocks"]), "lists", true);
    const prefs = setPrefs(dispatch);
    expect(prefs[PREF_HOME_WIDGETS]).toBe('["lists","clocks"]');
    expect(prefs[PREF_WIDGETS_ORDER].split(",")[0]).toBe("lists");
  });

  it("does not pin a widget twice", () => {
    const dispatch = jest.fn();
    setWidgetOnHome(dispatch, prefsWith(["lists"]), "lists", true);
    expect(setPrefs(dispatch)[PREF_HOME_WIDGETS]).toBe('["lists"]');
  });

  it("pins a widget however many are already pinned", () => {
    const dispatch = jest.fn();
    setWidgetOnHome(
      dispatch,
      prefsWith(["clocks", "focusTimer"]),
      "lists",
      true
    );
    expect(setPrefs(dispatch)[PREF_HOME_WIDGETS]).toBe(
      '["lists","clocks","focusTimer"]'
    );
  });

  const userEvents = dispatch =>
    dispatch.mock.calls
      .map(([action]) => action)
      .filter(action => action.type === at.WIDGETS_USER_EVENT)
      .map(action => action.data);

  it.each([
    [true, HOME_USER_ACTIONS.PIN, []],
    [false, HOME_USER_ACTIONS.UNPIN, ["lists"]],
  ])(
    "records onHome=%s as %s with its source",
    (onHome, userAction, homeList) => {
      const dispatch = jest.fn();
      setWidgetOnHome(
        dispatch,
        prefsWith(homeList),
        "lists",
        onHome,
        "context_menu"
      );
      expect(userEvents(dispatch)).toEqual([
        {
          widget_name: "lists",
          widget_source: "context_menu",
          widget_size: expect.any(String),
          user_action: userAction,
        },
      ]);
    }
  );

  // Turning a pinned widget off unpins it, but the toggle records itself.
  it("records nothing without a source", () => {
    const dispatch = jest.fn();
    setWidgetOnHome(dispatch, prefsWith(["lists"]), "lists", false);
    expect(userEvents(dispatch)).toEqual([]);
  });
});

describe("<WidgetHideMenuItem> telemetry", () => {
  it("records hiding on Home as remove_from_home, not as disabling", () => {
    const { item, store } = renderItem({
      placement: WIDGET_PLACEMENTS.HOME,
      prefs: { [PREF_HOME_WIDGETS]: '["lists"]' },
    });
    fireEvent.click(item);
    const actions = store.dispatch.mock.calls.map(([action]) => action);
    expect(actions.some(a => a.type === at.WIDGETS_ENABLED)).toBe(false);
    expect(
      actions.find(a => a.type === at.WIDGETS_USER_EVENT).data
    ).toMatchObject({
      widget_name: "lists",
      widget_source: "context_menu",
      user_action: HOME_USER_ACTIONS.UNPIN,
    });
  });
});
