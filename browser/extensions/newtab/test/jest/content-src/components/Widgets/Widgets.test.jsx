import { WIDGET_ROW_COMPONENTS } from "content-src/components/Widgets/WidgetsComponentRegistry.jsx";
import { WidgetLocationContext } from "content-src/components/Widgets/WidgetPlacement";
import { useContext } from "react";
import { render, fireEvent, createEvent, act } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { WIDGET_REGISTRY } from "common/WidgetsRegistry.mjs";
import {
  Widgets,
  resetTimerToDefaults,
} from "content-src/components/Widgets/Widgets";
import { BaseContext } from "content-src/lib/BaseContext";

const ENABLED_STATE = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.enabled": true,
      "widgets.lists.enabled": true,
      "widgets.system.lists.enabled": true,
    },
  },
  ListsWidget: {
    selected: "list-1",
    lists: {
      "list-1": {
        label: "My List",
        tasks: [],
        completed: [],
      },
    },
  },
};

const LEGACY_WEATHER_FORECAST_STATE = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "nova.enabled": false,
      "widgets.enabled": true,
      "widgets.system.weatherForecast.enabled": true,
      "weather.display": "detailed",
      showWeather: true,
      "system.showWeather": true,
    },
  },
  Weather: {
    ...INITIAL_STATE.Weather,
    initialized: true,
  },
};

function renderWidgets(state, props = {}) {
  const store = createStore(combineReducers(reducers), state);
  jest.spyOn(store, "dispatch");
  const { container } = render(
    <Provider store={store}>
      <Widgets {...props} />
    </Provider>
  );
  return { container, store };
}

const PREF_WIDGETS_ENABLED = "widgets.enabled";
const PREF_WIDGETS_LISTS_ENABLED = "widgets.lists.enabled";
const PREF_WIDGETS_SYSTEM_LISTS_ENABLED = "widgets.system.lists.enabled";
const PREF_WIDGETS_TIMER_ENABLED = "widgets.focusTimer.enabled";
const PREF_WIDGETS_SYSTEM_TIMER_ENABLED = "widgets.system.focusTimer.enabled";
const PREF_WIDGETS_CLOCKS_ENABLED = "widgets.clocks.enabled";
const PREF_WIDGETS_PRIVACY_ENABLED = "widgets.privacy.enabled";
const PREF_WIDGETS_CROSSWORD_ENABLED = "widgets.crossword.enabled";
const PREF_WIDGETS_STOCKS_ENABLED = "widgets.stocks.enabled";
const PREF_WIDGETS_RECENT_SEARCHES_ENABLED = "widgets.recentSearches.enabled";
const PREF_WIDGETS_HOROSCOPES_ENABLED = "widgets.horoscopes.enabled";
const PREF_WIDGETS_TRAVEL_ENABLED = "widgets.travel.enabled";
const PREF_WIDGETS_SOLITAIRE_ENABLED = "widgets.solitaire.enabled";
const PREF_WIDGETS_PICTURE_OF_THE_DAY_ENABLED =
  "widgets.pictureOfTheDay.enabled";
const PREF_WIDGETS_FEEDBACK_ENABLED = "widgets.feedback.enabled";
const PREF_WIDGETS_HIDE_ALL_TOAST_ENABLED = "widgets.hideAllToast.enabled";

function widgetsState(values, rest = {}) {
  return {
    ...INITIAL_STATE,
    Prefs: {
      ...INITIAL_STATE.Prefs,
      values: { ...INITIAL_STATE.Prefs.values, ...values },
    },
    ...rest,
  };
}

const actionsFrom = store =>
  store.dispatch.mock.calls.map(([action]) => action);

describe("<Widgets>", () => {
  it("should not render without any enabled widgets", () => {
    const store = createStore(combineReducers(reducers), INITIAL_STATE);
    const { container } = render(
      <Provider store={store}>
        <Widgets />
      </Provider>
    );
    expect(container.querySelector(".widgets-wrapper")).not.toBeInTheDocument();
  });

  it("should render when a widget is enabled", () => {
    const { container } = render(
      <WrapWithProvider state={ENABLED_STATE}>
        <Widgets />
      </WrapWithProvider>
    );
    expect(container.querySelector(".widgets-wrapper")).toBeInTheDocument();
  });

  it("should render and show <Lists> if list prefs are enabled", () => {
    const state = widgetsState({
      [PREF_WIDGETS_ENABLED]: true,
      [PREF_WIDGETS_LISTS_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
    });
    const { container } = render(
      <WrapWithProvider state={state}>
        <Widgets />
      </WrapWithProvider>
    );
    expect(container.querySelector(".widgets-container")).toBeInTheDocument();
    expect(container.querySelector(".lists.widget")).toBeInTheDocument();
  });

  // @experiment(remove) { bug 2078816 }
  it("should leave out the widget the first content slot shows", () => {
    const state = widgetsState({
      "nova.enabled": true,
      "feeds.section.topstories": true,
      "feeds.system.topstories": true,
      "pageLayouts.variant": "widget-first-content-slot",
      "pageLayouts.widgetFirstContentSlot.widget": "crossword",
      [PREF_WIDGETS_ENABLED]: true,
      [PREF_WIDGETS_LISTS_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
      [PREF_WIDGETS_CROSSWORD_ENABLED]: true,
      "widgets.system.crossword.enabled": true,
    });
    const { container } = renderWidgets(state);
    expect(container.querySelector(".lists.widget")).toBeInTheDocument();
    expect(container.querySelector(".crossword")).not.toBeInTheDocument();
  });

  it("should render <Travel> under Nova when its prefs are enabled", () => {
    const state = widgetsState({
      "nova.enabled": true,
      [PREF_WIDGETS_ENABLED]: true,
      [PREF_WIDGETS_TRAVEL_ENABLED]: true,
      "widgets.system.travel.enabled": true,
    });
    const { container } = renderWidgets(state);
    expect(
      container.querySelector("article.travel.widget")
    ).toBeInTheDocument();
  });

  it("should render and show <FocusTimer> if timer prefs are enabled", () => {
    const state = widgetsState({
      [PREF_WIDGETS_ENABLED]: true,
      [PREF_WIDGETS_TIMER_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_TIMER_ENABLED]: true,
    });
    const { container } = render(
      <WrapWithProvider state={state}>
        <Widgets />
      </WrapWithProvider>
    );
    expect(container.querySelector(".widgets-container")).toBeInTheDocument();
    expect(container.querySelector(".focus-timer.widget")).toBeInTheDocument();
  });

  it("should render <Solitaire> if solitaire prefs are enabled", () => {
    const state = widgetsState({
      "nova.enabled": true,
      [PREF_WIDGETS_ENABLED]: true,
      [PREF_WIDGETS_SOLITAIRE_ENABLED]: true,
      "widgets.system.solitaire.enabled": true,
    });
    const { container } = renderWidgets(state);
    expect(
      container.querySelector("article.solitaire.widget")
    ).toBeInTheDocument();
  });

  it("should render nothing when widgetsEnabled is false, even if individual widget prefs are on", () => {
    const state = widgetsState({
      [PREF_WIDGETS_ENABLED]: false,
      [PREF_WIDGETS_LISTS_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
      [PREF_WIDGETS_TIMER_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_TIMER_ENABLED]: true,
    });
    const { container } = render(
      <WrapWithProvider state={state}>
        <Widgets />
      </WrapWithProvider>
    );
    expect(container.querySelector(".widgets-wrapper")).not.toBeInTheDocument();
    expect(container.querySelector(".lists.widget")).not.toBeInTheDocument();
    expect(
      container.querySelector(".focus-timer.widget")
    ).not.toBeInTheDocument();
  });

  it("should not render FocusTimer when timer pref is disabled", () => {
    const state = widgetsState({
      [PREF_WIDGETS_TIMER_ENABLED]: false,
      [PREF_WIDGETS_SYSTEM_TIMER_ENABLED]: true,
    });
    const { container } = render(
      <WrapWithProvider state={state}>
        <Widgets />
      </WrapWithProvider>
    );
    expect(
      container.querySelector(".focus-timer.widget")
    ).not.toBeInTheDocument();
  });
});

describe("<Widgets> resetTimerToDefaults", () => {
  it("should dispatch WIDGETS_TIMER_RESET with focus timer defaults", () => {
    const dispatch = jest.fn();

    resetTimerToDefaults(dispatch, "focus");

    const dispatched = dispatch.mock.calls.map(([action]) => action);
    const resetAction = dispatched.find(
      action => action?.type === at.WIDGETS_TIMER_RESET
    );
    const setTypeAction = dispatched.find(
      action => action?.type === at.WIDGETS_TIMER_SET_TYPE
    );

    expect(resetAction).toBeDefined();
    expect(setTypeAction).toBeDefined();
    expect(resetAction.data.duration).toBe(1500);
    expect(resetAction.data.initialDuration).toBe(1500);
    expect(resetAction.data.timerType).toBe("focus");
    expect(setTypeAction.data.timerType).toBe("focus");
  });

  it("should dispatch WIDGETS_TIMER_RESET with break timer defaults", () => {
    const dispatch = jest.fn();

    resetTimerToDefaults(dispatch, "break");

    const resetAction = dispatch.mock.calls
      .map(([action]) => action)
      .find(action => action?.type === at.WIDGETS_TIMER_RESET);

    expect(resetAction).toBeDefined();
    expect(resetAction.data.duration).toBe(300);
    expect(resetAction.data.initialDuration).toBe(300);
    expect(resetAction.data.timerType).toBe("break");
  });
});

describe("<Widgets> handleHideAllWidgets", () => {
  const HIDE_ALL_PREFS = {
    [PREF_WIDGETS_ENABLED]: true,
    [PREF_WIDGETS_LISTS_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
    [PREF_WIDGETS_TIMER_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_TIMER_ENABLED]: true,
  };

  let container;
  let store;

  beforeEach(() => {
    ({ container, store } = renderWidgets(widgetsState(HIDE_ALL_PREFS)));
  });

  function setPrefActions(targetStore = store) {
    return actionsFrom(targetStore).filter(
      action => action?.type === at.SET_PREF
    );
  }

  function expectAllWidgetPrefsDisabled(calls) {
    expect(calls).toHaveLength(11);
    for (const name of [
      PREF_WIDGETS_LISTS_ENABLED,
      PREF_WIDGETS_TIMER_ENABLED,
      PREF_WIDGETS_CLOCKS_ENABLED,
      PREF_WIDGETS_PRIVACY_ENABLED,
      PREF_WIDGETS_CROSSWORD_ENABLED,
      PREF_WIDGETS_STOCKS_ENABLED,
      PREF_WIDGETS_PICTURE_OF_THE_DAY_ENABLED,
      PREF_WIDGETS_RECENT_SEARCHES_ENABLED,
      PREF_WIDGETS_HOROSCOPES_ENABLED,
      PREF_WIDGETS_TRAVEL_ENABLED,
      PREF_WIDGETS_SOLITAIRE_ENABLED,
    ]) {
      const call = calls.find(action => action.data?.name === name);
      expect(call).toBeDefined();
      expect(call.data.value).toBe(false);
    }
  }

  it("should dispatch SetPref actions when hide button is clicked", () => {
    const hideButton = container.querySelector("#hide-all-widgets-button");
    expect(hideButton).toBeInTheDocument();

    fireEvent.click(hideButton);

    expectAllWidgetPrefsDisabled(setPrefActions());
  });

  it("should dispatch SetPref actions when Enter key is pressed on hide button", () => {
    const hideButton = container.querySelector("#hide-all-widgets-button");

    fireEvent.keyDown(hideButton, { key: "Enter" });

    expectAllWidgetPrefsDisabled(setPrefActions());
  });

  it("should dispatch SetPref actions when Space key is pressed on hide button", () => {
    const hideButton = container.querySelector("#hide-all-widgets-button");

    fireEvent.keyDown(hideButton, { key: " " });

    expectAllWidgetPrefsDisabled(setPrefActions());
  });

  it("should not dispatch SetPref actions when other keys are pressed", () => {
    const hideButton = container.querySelector("#hide-all-widgets-button");

    for (const key of ["Escape", "Tab", "a", "ArrowDown"]) {
      store.dispatch.mockClear();
      fireEvent.keyDown(hideButton, { key });
      expect(setPrefActions()).toHaveLength(0);
    }
  });

  it("should dispatch WIDGETS_HIDE_ALL with correct data when hide button is clicked", () => {
    fireEvent.click(container.querySelector("#hide-all-widgets-button"));

    const hideAllAction = actionsFrom(store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );

    expect(hideAllAction).toBeDefined();
    expect(hideAllAction.data.widget_size).toBe("large");

    const listsTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "lists"
    );
    const timerTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "focus_timer"
    );
    expect(listsTarget).toBeDefined();
    expect(timerTarget).toBeDefined();
    expect(listsTarget.active).toBe(true);
    expect(timerTarget.active).toBe(true);
  });

  it("should dispatch WIDGETS_HIDE_ALL with large size when widgets are maximized", () => {
    const maximized = renderWidgets(
      widgetsState({
        ...HIDE_ALL_PREFS,
        "widgets.maximized": true,
        "widgets.system.maximized": true,
      })
    );

    fireEvent.click(
      maximized.container.querySelector("#hide-all-widgets-button")
    );

    const hideAllAction = actionsFrom(maximized.store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );

    expect(hideAllAction).toBeDefined();
    expect(hideAllAction.data.widget_size).toBe("large");
  });

  it("should dispatch WIDGETS_HIDE_ALL with active=true only for enabled widgets", () => {
    fireEvent.click(container.querySelector("#hide-all-widgets-button"));

    const hideAllAction = actionsFrom(store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );

    expect(hideAllAction).toBeDefined();

    const listsTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "lists"
    );
    const timerTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "focus_timer"
    );

    expect(listsTarget).toBeDefined();
    expect(listsTarget.active).toBe(true);
    expect(listsTarget.enabledPref).toBe(PREF_WIDGETS_LISTS_ENABLED);

    expect(timerTarget).toBeDefined();
    expect(timerTarget.active).toBe(true);
    expect(timerTarget.enabledPref).toBe(PREF_WIDGETS_TIMER_ENABLED);
  });

  it("should dispatch WIDGETS_HIDE_ALL with active=false for disabled widgets", () => {
    const partial = renderWidgets(
      widgetsState({
        ...HIDE_ALL_PREFS,
        [PREF_WIDGETS_TIMER_ENABLED]: false,
      })
    );

    fireEvent.click(
      partial.container.querySelector("#hide-all-widgets-button")
    );

    const hideAllAction = actionsFrom(partial.store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );

    expect(hideAllAction).toBeDefined();

    const listsTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "lists"
    );
    const timerTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "focus_timer"
    );

    expect(listsTarget).toBeDefined();
    expect(listsTarget.active).toBe(true);

    expect(timerTarget).toBeDefined();
    expect(timerTarget.active).toBe(false);
  });

  it("should dispatch WIDGETS_HIDE_ALL with correct widget_size when maximized", () => {
    const maximized = renderWidgets(
      widgetsState({
        ...HIDE_ALL_PREFS,
        "widgets.maximized": true,
        "widgets.system.maximized": true,
      })
    );

    fireEvent.click(
      maximized.container.querySelector("#hide-all-widgets-button")
    );

    const hideAllAction = actionsFrom(maximized.store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );

    expect(hideAllAction).toBeDefined();
    expect(hideAllAction.data.widget_size).toBe("large");
  });

  it("should dispatch WIDGETS_HIDE_ALL when Enter key is pressed", () => {
    fireEvent.keyDown(container.querySelector("#hide-all-widgets-button"), {
      key: "Enter",
    });

    const hideAllAction = actionsFrom(store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );

    expect(hideAllAction).toBeDefined();

    const listsTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "lists"
    );
    const timerTarget = hideAllAction.data.targets.find(
      t => t.telemetryName === "focus_timer"
    );

    expect(listsTarget).toBeDefined();
    expect(listsTarget.active).toBe(true);

    expect(timerTarget).toBeDefined();
    expect(timerTarget.active).toBe(true);
  });
});

describe("<Widgets> feedback link", () => {
  const BASE_PREFS = {
    [PREF_WIDGETS_ENABLED]: true,
    [PREF_WIDGETS_LISTS_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
  };

  it("should not render the feedback link when feedbackEnabled is not set", () => {
    const { container } = renderWidgets(widgetsState(BASE_PREFS));
    expect(
      container.querySelector(".widgets-feedback-link")
    ).not.toBeInTheDocument();
  });

  it("should not render the feedback link when feedbackEnabled is false", () => {
    const { container } = renderWidgets(
      widgetsState({
        ...BASE_PREFS,
        trainhopConfig: { widgets: { feedbackEnabled: false } },
      })
    );
    expect(
      container.querySelector(".widgets-feedback-link")
    ).not.toBeInTheDocument();
  });

  it("should render the feedback link when trainhopConfig feedbackEnabled is true", () => {
    const { container } = renderWidgets(
      widgetsState({
        ...BASE_PREFS,
        trainhopConfig: { widgets: { feedbackEnabled: true } },
      })
    );
    expect(
      container.querySelector(".widgets-feedback-link")
    ).toBeInTheDocument();
  });

  it("should render the feedback link when the pref is true", () => {
    const { container } = renderWidgets(
      widgetsState({
        ...BASE_PREFS,
        [PREF_WIDGETS_FEEDBACK_ENABLED]: true,
      })
    );
    expect(
      container.querySelector(".widgets-feedback-link")
    ).toBeInTheDocument();
  });

  it("should dispatch OPEN_LINK and WIDGETS_CONTAINER_ACTION when feedback link is clicked", () => {
    const { container, store } = renderWidgets(
      widgetsState({
        ...BASE_PREFS,
        trainhopConfig: { widgets: { feedbackEnabled: true } },
      })
    );

    fireEvent.click(container.querySelector(".widgets-feedback-link"));

    const dispatched = actionsFrom(store);
    const openLink = dispatched.find(a => a.type === at.OPEN_LINK);
    const containerAction = dispatched.find(
      a => a.type === at.WIDGETS_CONTAINER_ACTION
    );

    expect(openLink).toBeDefined();
    expect(containerAction).toBeDefined();
    expect(containerAction.data.action_type).toBe("feedback");
    expect(containerAction.data.widget_size).toBe("large");
  });

  it("should use a custom URL from trainhopConfig when provided", () => {
    const customUrl = "https://example.com/custom-feedback";
    const { container, store } = renderWidgets(
      widgetsState({
        ...BASE_PREFS,
        trainhopConfig: {
          widgets: { feedbackEnabled: true, feedbackUrl: customUrl },
        },
      })
    );

    fireEvent.click(container.querySelector(".widgets-feedback-link"));

    const openLink = actionsFrom(store).find(a => a.type === at.OPEN_LINK);

    expect(openLink).toBeDefined();
    expect(openLink.data.url).toBe(customUrl);
  });
});

describe("<Widgets> hide all widgets toast", () => {
  const BASE_PREFS = {
    [PREF_WIDGETS_ENABLED]: true,
    [PREF_WIDGETS_LISTS_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
  };

  function clickHideButton(prefs) {
    const { container, store } = renderWidgets(widgetsState(prefs));
    fireEvent.click(container.querySelector("#hide-all-widgets-button"));
    return actionsFrom(store);
  }

  it("should not dispatch toast when hideAllToastEnabled is not set", () => {
    const dispatched = clickHideButton(BASE_PREFS);
    expect(
      dispatched.filter(a => a.type === at.SHOW_TOAST_MESSAGE)
    ).toHaveLength(0);
  });

  it("should not dispatch toast when pref is false", () => {
    const dispatched = clickHideButton({
      ...BASE_PREFS,
      [PREF_WIDGETS_HIDE_ALL_TOAST_ENABLED]: false,
    });
    expect(
      dispatched.filter(a => a.type === at.SHOW_TOAST_MESSAGE)
    ).toHaveLength(0);
  });

  it("should not dispatch toast when trainhopConfig hideAllToastEnabled is false", () => {
    const dispatched = clickHideButton({
      ...BASE_PREFS,
      trainhopConfig: { widgets: { hideAllToastEnabled: false } },
    });
    expect(
      dispatched.filter(a => a.type === at.SHOW_TOAST_MESSAGE)
    ).toHaveLength(0);
  });

  it("should dispatch toast when pref is true", () => {
    const dispatched = clickHideButton({
      ...BASE_PREFS,
      [PREF_WIDGETS_HIDE_ALL_TOAST_ENABLED]: true,
    });
    const toastAction = dispatched.find(
      a => a.data && a.data.toastId === "hideWidgetsToast"
    );
    expect(toastAction).toBeDefined();
    expect(toastAction.data.showNotifications).toBe(true);
  });

  it("should dispatch toast when trainhopConfig hideAllToastEnabled is true", () => {
    const dispatched = clickHideButton({
      ...BASE_PREFS,
      trainhopConfig: { widgets: { hideAllToastEnabled: true } },
    });
    const toastAction = dispatched.find(
      a => a.data && a.data.toastId === "hideWidgetsToast"
    );
    expect(toastAction).toBeDefined();
    expect(toastAction.data.showNotifications).toBe(true);
  });
});

describe("<Widgets> handleToggleMaximize", () => {
  const TOGGLE_PREFS = {
    [PREF_WIDGETS_ENABLED]: true,
    [PREF_WIDGETS_LISTS_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
    "widgets.maximized": false,
    "widgets.system.maximized": true,
  };

  it("should dispatch WIDGETS_CONTAINER_ACTION telemetry when toggle button is clicked", () => {
    const { container, store } = renderWidgets(widgetsState(TOGGLE_PREFS));

    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));

    const containerAction = actionsFrom(store).find(
      action => action.type === at.WIDGETS_CONTAINER_ACTION
    );

    expect(containerAction).toBeDefined();
    expect(containerAction.data.action_type).toBe("change_size_all");
    expect(containerAction.data.action_value).toBe("maximize_widgets");
    expect(containerAction.data.widget_size).toBe("large");
  });

  it("should dispatch WIDGETS_CONTAINER_ACTION with correct values when toggling from maximized", () => {
    const { container, store } = renderWidgets(
      widgetsState({ ...TOGGLE_PREFS, "widgets.maximized": true })
    );

    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));

    const containerAction = actionsFrom(store).find(
      action => action.type === at.WIDGETS_CONTAINER_ACTION
    );

    expect(containerAction).toBeDefined();
    expect(containerAction.data.action_type).toBe("change_size_all");
    expect(containerAction.data.action_value).toBe("minimize_widgets");
    expect(containerAction.data.widget_size).toBe("medium");
  });

  describe("with Nova enabled", () => {
    const NOVA_PREFS = {
      "nova.enabled": true,
      [PREF_WIDGETS_ENABLED]: true,
      [PREF_WIDGETS_LISTS_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
      [PREF_WIDGETS_TIMER_ENABLED]: true,
      [PREF_WIDGETS_SYSTEM_TIMER_ENABLED]: true,
      "widgets.system.weather.enabled": true,
      "widgets.system.clocks.enabled": true,
      "widgets.system.weatherForecast.enabled": true,
      "weather.display": "detailed",
      showWeather: true,
      "system.showWeather": true,
      "widgets.maximized": false,
      "widgets.system.maximized": true,
      "widgets.lists.size": "medium",
      "widgets.focusTimer.size": "medium",
      "widgets.weather.size": "medium",
    };

    const novaState = (extraPrefs = {}) =>
      widgetsState(
        { ...NOVA_PREFS, ...extraPrefs },
        { Weather: { ...INITIAL_STATE.Weather, initialized: true } }
      );

    function renderNovaWidgets(state, openWidgetsPanel) {
      const store = createStore(combineReducers(reducers), state);
      jest.spyOn(store, "dispatch");
      const { container } = render(
        <BaseContext.Provider value={{ openWidgetsPanel }}>
          <Provider store={store}>
            <Widgets />
          </Provider>
        </BaseContext.Provider>
      );
      return { container, store };
    }

    it("should render the Nova header menu instead of the footer feedback link", () => {
      const { container } = renderWidgets(
        novaState({ [PREF_WIDGETS_FEEDBACK_ENABLED]: true })
      );

      expect(
        container.querySelector(".widgets-header-context-menu-button")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".widgets-feedback-link")
      ).not.toBeInTheDocument();
    });

    it("should render both Nova header menu items", () => {
      const { container } = renderWidgets(novaState());

      expect(
        container.querySelectorAll(
          '[id^="widgets-header-context-panel"] panel-item'
        )
      ).toHaveLength(3);
    });

    it("should call openWidgetsPanel when the manage widgets menu item is clicked", () => {
      const openWidgetsPanel = jest.fn();
      const { container } = renderNovaWidgets(novaState(), openWidgetsPanel);

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-section-menu-manage']"
        )
      );

      expect(openWidgetsPanel).toHaveBeenCalledTimes(1);
    });

    it("should render the Add widgets button when at least one widget is not enabled", () => {
      const { container } = renderWidgets(novaState());
      expect(
        container.querySelector(".widgets-add-button")
      ).toBeInTheDocument();
    });

    it("should not render the Add widgets button when every widget is enabled", () => {
      const { container } = renderWidgets(
        novaState({
          "widgets.weather.enabled": true,
          "widgets.system.weather.enabled": true,
          "widgets.clocks.enabled": true,
          "widgets.system.clocks.enabled": true,
        })
      );
      expect(
        container.querySelector(".widgets-add-button")
      ).not.toBeInTheDocument();
    });

    it("should not render the Add widgets button when Nova is disabled", () => {
      const { container } = renderWidgets(novaState({ "nova.enabled": false }));
      expect(
        container.querySelector(".widgets-add-button")
      ).not.toBeInTheDocument();
    });

    it("should call openWidgetsPanel when the Add widgets button is clicked", () => {
      const openWidgetsPanel = jest.fn();
      const { container } = renderNovaWidgets(novaState(), openWidgetsPanel);

      fireEvent.click(container.querySelector(".widgets-add-button"));

      expect(openWidgetsPanel).toHaveBeenCalledTimes(1);
    });

    it("should match the largest current widget size on the Add widgets button", () => {
      const { container } = renderWidgets(
        novaState({
          "widgets.maximized": true,
          "widgets.lists.size": "large",
          "widgets.focusTimer.size": "large",
          "widgets.weather.size": "large",
        })
      );
      expect(
        container.querySelector(".widgets-add-button.large-widget")
      ).toBeInTheDocument();
    });

    it("should dispatch hide widget actions from the Nova header menu", () => {
      const { container, store } = renderWidgets(novaState());

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-section-menu-hide-all']"
        )
      );

      const setPrefCalls = actionsFrom(store).filter(
        action => action?.type === at.SET_PREF
      );

      expect(
        setPrefCalls.find(
          action => action.data?.name === PREF_WIDGETS_LISTS_ENABLED
        )
      ).toBeDefined();
      expect(
        setPrefCalls.find(
          action => action.data?.name === PREF_WIDGETS_TIMER_ENABLED
        )
      ).toBeDefined();
    });

    it.each([
      [undefined, "widget_container"],
      ["home", "home_space"],
      ["library", "widget_space"],
    ])(
      "should tell its widgets where they render (placement %s: %s)",
      (placement, location) => {
        const original = WIDGET_ROW_COMPONENTS.lists;
        WIDGET_ROW_COMPONENTS.lists = function Probe() {
          return (
            <div
              className="location-probe"
              data-location={useContext(WidgetLocationContext) ?? ""}
            />
          );
        };
        try {
          const { container } = renderWidgets(novaState(), { placement });
          expect(
            container.querySelector(".location-probe").dataset.location
          ).toBe(location);
        } finally {
          WIDGET_ROW_COMPONENTS.lists = original;
        }
      }
    );

    it("should only empty the Home list when hiding all on Home", () => {
      const { container, store } = renderWidgets(novaState(), {
        placement: "home",
      });

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-section-menu-hide-all']"
        )
      );

      const setPrefCalls = actionsFrom(store).filter(
        action => action?.type === at.SET_PREF
      );
      expect(setPrefCalls.map(action => action.data)).toEqual([
        { name: "widgets.home", value: "[]" },
      ]);
      const actions = actionsFrom(store);
      expect(
        actions.find(action => action.type === at.WIDGETS_CONTAINER_ACTION).data
          .action_type
      ).toBe("remove_all_from_home");
      expect(
        actions.some(
          action =>
            action.type === at.WIDGETS_HIDE_ALL ||
            action.type === at.WIDGETS_ENABLED
        )
      ).toBe(false);
    });

    it("should turn widgets off when hiding all in the widget library", () => {
      const { container, store } = renderWidgets(novaState(), {
        placement: "library",
      });

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-section-menu-hide-all']"
        )
      );

      const names = actionsFrom(store)
        .filter(action => action?.type === at.SET_PREF)
        .map(action => action.data.name);
      expect(names).toContain(PREF_WIDGETS_LISTS_ENABLED);
      expect(names).not.toContain("widgets.home");
    });

    it("should dispatch Learn more actions from the Nova header menu", () => {
      const { container, store } = renderWidgets(novaState());

      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-section-menu-learn-more']"
        )
      );

      const dispatched = actionsFrom(store);
      const openLink = dispatched.find(action => action.type === at.OPEN_LINK);
      const containerAction = dispatched.find(
        action => action.type === at.WIDGETS_CONTAINER_ACTION
      );

      expect(openLink).toBeDefined();
      expect(openLink.data.url).toBe(
        "https://support.mozilla.org/kb/firefox-new-tab-widgets"
      );
      expect(openLink.data.where).toBe("tab");
      expect(containerAction).toBeDefined();
      expect(containerAction.data.action_type).toBe("feedback");
    });
  });
});

describe("<Widgets> widget order", () => {
  const PREF_WIDGETS_ORDER = "widgets.order";
  const ORDER_PREFS = {
    [PREF_WIDGETS_ENABLED]: true,
    [PREF_WIDGETS_LISTS_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_LISTS_ENABLED]: true,
    [PREF_WIDGETS_TIMER_ENABLED]: true,
    [PREF_WIDGETS_SYSTEM_TIMER_ENABLED]: true,
  };

  it("should render Lists before FocusTimer with default order (empty pref)", () => {
    const { container } = renderWidgets(
      widgetsState({ ...ORDER_PREFS, [PREF_WIDGETS_ORDER]: "" })
    );
    const listsNode = container.querySelector(".lists.widget");
    const timerNode = container.querySelector(".focus-timer.widget");
    // DOCUMENT_POSITION_FOLLOWING (4): timerNode comes after listsNode
    expect(
      listsNode.compareDocumentPosition(timerNode) &
        Node.DOCUMENT_POSITION_FOLLOWING
    ).toBeTruthy();
  });

  it("should render FocusTimer before Lists when order pref reverses them", () => {
    const { container } = renderWidgets(
      widgetsState({
        ...ORDER_PREFS,
        [PREF_WIDGETS_ORDER]: "focusTimer,lists,weather",
      })
    );
    const timerNode = container.querySelector(".focus-timer.widget");
    const listsNode = container.querySelector(".lists.widget");
    // DOCUMENT_POSITION_FOLLOWING (4): listsNode comes after timerNode
    expect(
      timerNode.compareDocumentPosition(listsNode) &
        Node.DOCUMENT_POSITION_FOLLOWING
    ).toBeTruthy();
  });

  it("should not dispatch SET_PREF for widgets.order when a widget is disabled", () => {
    const { container, store } = renderWidgets(widgetsState(ORDER_PREFS));

    fireEvent.click(container.querySelector("#hide-all-widgets-button"));

    const orderPrefCalls = actionsFrom(store).filter(
      action =>
        action?.type === at.SET_PREF &&
        action?.data?.name === PREF_WIDGETS_ORDER
    );

    expect(orderPrefCalls).toHaveLength(0);
  });
});

describe("<Widgets> hideAllWidgets legacy weather telemetry", () => {
  it("dispatches WIDGETS_ENABLED for weather when !novaEnabled && weatherForecastEnabled", () => {
    const { container, store } = renderWidgets(LEGACY_WEATHER_FORECAST_STATE);
    const hideAllButton = container.querySelector("#hide-all-widgets-button");
    expect(hideAllButton).toBeInTheDocument();

    fireEvent.click(hideAllButton);

    const dispatched = store.dispatch.mock.calls.map(([action]) => action);
    expect(dispatched).toContainEqual(
      expect.objectContaining({
        type: at.WIDGETS_ENABLED,
        data: expect.objectContaining({
          widget_name: "weather",
          widget_source: "widget",
          enabled: false,
        }),
      })
    );
  });

  it("does not dispatch WIDGETS_ENABLED for weather when weatherForecastEnabled is false", () => {
    const state = {
      ...LEGACY_WEATHER_FORECAST_STATE,
      Prefs: {
        ...LEGACY_WEATHER_FORECAST_STATE.Prefs,
        values: {
          ...LEGACY_WEATHER_FORECAST_STATE.Prefs.values,
          "widgets.system.weatherForecast.enabled": false,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": true,
        },
      },
      ListsWidget: {
        selected: "list-1",
        lists: { "list-1": { label: "My List", tasks: [], completed: [] } },
      },
    };
    const { container, store } = renderWidgets(state);
    const hideAllButton = container.querySelector("#hide-all-widgets-button");
    expect(hideAllButton).toBeInTheDocument();

    fireEvent.click(hideAllButton);

    const dispatched = store.dispatch.mock.calls.map(([action]) => action);
    const weatherEnabledCalls = dispatched.filter(
      action =>
        action.type === at.WIDGETS_ENABLED &&
        action.data?.widget_name === "weather" &&
        action.data?.widget_source === "widget"
    );
    expect(weatherEnabledCalls).toHaveLength(0);
  });
});

// Builds the minimum state needed to enable the listed widgets in nova
// mode at the given sizes. Each entry is [widgetId, size].
function makeNovaWidgetState(widgets, extraPrefs = {}) {
  const prefValues = {
    ...INITIAL_STATE.Prefs.values,
    "nova.enabled": true,
    "widgets.enabled": true,
    region: "US",
    // Permissive defaults that let the weather widget pass its
    // `weatherData.initialized && showWeather && systemShowWeather`
    // gate when it's enabled by the test. Harmless when it isn't.
    showWeather: true,
    "system.showWeather": true,
    ...extraPrefs,
  };
  // Map widget IDs to their enabled/system/size pref names. Mirrors
  // WIDGET_REGISTRY so the test doesn't depend on registry internals.
  const widgetPrefs = {
    lists: {
      enabled: "widgets.lists.enabled",
      system: "widgets.system.lists.enabled",
      size: "widgets.lists.size",
    },
    focusTimer: {
      enabled: "widgets.focusTimer.enabled",
      system: "widgets.system.focusTimer.enabled",
      size: "widgets.focusTimer.size",
    },
    weather: {
      enabled: "widgets.weather.enabled",
      system: "widgets.system.weather.enabled",
      size: "widgets.weather.size",
    },
    stocks: {
      enabled: "widgets.stocks.enabled",
      system: "widgets.system.stocks.enabled",
      size: "widgets.stocks.size",
    },
    clocks: {
      enabled: "widgets.clocks.enabled",
      system: "widgets.system.clocks.enabled",
      size: "widgets.clocks.size",
    },
  };
  for (const [id, size] of widgets) {
    const prefs = widgetPrefs[id];
    prefValues[prefs.enabled] = true;
    prefValues[prefs.system] = true;
    prefValues[prefs.size] = size;
  }
  return {
    ...INITIAL_STATE,
    Prefs: { ...INITIAL_STATE.Prefs, values: prefValues },
    // ListsWidget needs a valid selected list to render
    ListsWidget: {
      selected: "list-1",
      lists: {
        "list-1": { label: "My List", tasks: [], completed: [] },
      },
    },
    // Weather only renders when initialized; mark it so it can be
    // counted toward widget positions when enabled.
    Weather: { ...INITIAL_STATE.Weather, initialized: true },
  };
}

describe("<Widgets> sidebar widget leaves the row", () => {
  const PRIVACY_SIDEBAR = {
    "widgets.privacy.enabled": true,
    "widgets.system.privacy.enabled": true,
    "widgets.privacy.size": "small",
    trainhopConfig: { widgets: { privacySidebar: true } },
  };

  function rowWidgetIds(container) {
    return [
      ...container.querySelectorAll(".widget-wrapper[data-widget-id]"),
    ].map(el => el.getAttribute("data-widget-id"));
  }

  it("keeps small weather out of the row", () => {
    const state = makeNovaWidgetState([
      ["lists", "medium"],
      ["weather", "small"],
    ]);
    const { container } = renderWidgets(state);
    expect(rowWidgetIds(container)).toEqual(["lists"]);
  });

  it("keeps privacy out of the row when trainhop places it in the sidebar", () => {
    const state = makeNovaWidgetState([["lists", "medium"]], PRIVACY_SIDEBAR);
    const { container } = renderWidgets(state);
    expect(rowWidgetIds(container)).toEqual(["lists"]);
  });

  it("puts the widget that loses the single sidebar slot in the row", () => {
    const state = makeNovaWidgetState(
      [
        ["lists", "medium"],
        ["weather", "small"],
      ],
      PRIVACY_SIDEBAR
    );
    const { container } = renderWidgets(state);
    expect(rowWidgetIds(container)).toEqual(["lists", "privacy"]);
  });
});

describe("<Widgets> overflow detection", () => {
  function getSectionContainer(container) {
    return container.querySelector(".widgets-section-container");
  }

  it("sets no overflow attributes when only one widget is enabled", () => {
    const state = makeNovaWidgetState([["lists", "medium"]]);
    const { container } = renderWidgets(state);
    const section = getSectionContainer(container);
    expect(section.hasAttribute("data-overflow-1")).toBe(false);
    expect(section.hasAttribute("data-overflow-2")).toBe(false);
    expect(section.hasAttribute("data-overflow-3")).toBe(false);
    expect(section.hasAttribute("data-overflow-4")).toBe(false);
  });

  it("flags overflow-1 (but not -2) for two mediums", () => {
    const state = makeNovaWidgetState([
      ["lists", "medium"],
      ["focusTimer", "medium"],
    ]);
    const { container } = renderWidgets(state);
    const section = getSectionContainer(container);
    // One widget per card slot regardless of size, so 2 widgets overflow
    // a 1-card viewport but fit a 2-card viewport.
    expect(section.hasAttribute("data-overflow-1")).toBe(true);
    expect(section.hasAttribute("data-overflow-2")).toBe(false);
  });

  it("flags overflow-1 and -2 (but not -3) when three mediums are enabled", () => {
    const state = makeNovaWidgetState([
      ["lists", "medium"],
      ["focusTimer", "medium"],
      ["stocks", "medium"],
    ]);
    const { container } = renderWidgets(state);
    const section = getSectionContainer(container);
    // 3 widgets need 3 card slots; overflows 1- and 2-card viewports.
    expect(section.hasAttribute("data-overflow-1")).toBe(true);
    expect(section.hasAttribute("data-overflow-2")).toBe(true);
    expect(section.hasAttribute("data-overflow-3")).toBe(false);
  });

  it("overflow detection is size-agnostic", () => {
    const state = makeNovaWidgetState([
      ["stocks", "medium"],
      ["clocks", "medium"],
      ["lists", "medium"],
      ["focusTimer", "medium"],
      ["weather", "large"],
    ]);
    const { container } = renderWidgets(state);
    const section = getSectionContainer(container);
    // 5 widgets, one per slot, overflow every viewport up to 4 cards.
    expect(section.hasAttribute("data-overflow-4")).toBe(true);
  });

  it("flags overflow-3 (but not -4) for four widgets of mixed sizes", () => {
    const state = makeNovaWidgetState([
      ["lists", "large"],
      ["focusTimer", "medium"],
      ["weather", "medium"],
      ["stocks", "medium"],
    ]);
    const { container } = renderWidgets(state);
    const section = getSectionContainer(container);
    // 4 widgets = 4 slots; overflows a 3-card viewport, fits a 4-card one.
    // Sizes don't affect the count.
    expect(section.hasAttribute("data-overflow-3")).toBe(true);
    expect(section.hasAttribute("data-overflow-4")).toBe(false);
  });
});

describe("<Widgets> extension widget", () => {
  const EXTENSION_ID = "fixture-widget@mozilla.org";
  const EXTENSION_SRC = "moz-extension://uuid/widget.html";
  const EXTENSION_WIDGET = {
    id: "extensionWidget",
    enabledPref: "widgets.extensionWidget.enabled",
    sizePref: "widgets.extensionWidget.size",
    systemEnabledPref: "widgets.system.extensionWidget.enabled",
    defaultSize: "large",
    extensionID: EXTENSION_ID,
  };
  const EXTENSION_WIDGETS = {
    [EXTENSION_ID]: {
      src: EXTENSION_SRC,
      name: "Tally",
      telemetryName: EXTENSION_ID,
      validSizes: ["medium", "large"],
    },
  };

  beforeAll(() => {
    WIDGET_REGISTRY.push(EXTENSION_WIDGET);
  });

  afterAll(() => {
    WIDGET_REGISTRY.splice(WIDGET_REGISTRY.indexOf(EXTENSION_WIDGET), 1);
  });

  function extensionWidgetState(extensionWidgets) {
    return {
      ...makeNovaWidgetState([["lists", "medium"]], {
        "widgets.extensionWidget.enabled": true,
        "widgets.system.extensionWidget.enabled": true,
      }),
      ExtensionWidgets: extensionWidgets,
    };
  }

  it("renders a padded moz-widget card carrying its extension's widget src, size and name", () => {
    const { container } = renderWidgets(
      extensionWidgetState(EXTENSION_WIDGETS)
    );
    const card = container.querySelector(
      'moz-widget[widget-id="extensionWidget"]'
    );
    expect(card.getAttribute("src")).toBe(EXTENSION_SRC);
    expect(card.getAttribute("size")).toBe("large");
    expect(card.hasAttribute("padded")).toBe(true);
    const header = card.querySelector('moz-widget-header[slot="header"]');
    expect(header.getAttribute("title")).toBe("Tally");
  });

  it("hides the widget under its extension's telemetry name", () => {
    const { container, store } = renderWidgets(
      extensionWidgetState(EXTENSION_WIDGETS)
    );
    fireEvent.click(
      container.querySelector(
        'panel-item[data-l10n-id="newtab-widget-section-menu-hide-all"]'
      )
    );
    const hideAll = actionsFrom(store).find(
      action => action.type === at.WIDGETS_HIDE_ALL
    );
    expect(hideAll.data.targets).toContainEqual({
      enabledPref: "widgets.extensionWidget.enabled",
      telemetryName: EXTENSION_ID,
      active: true,
    });
  });

  it("renders nothing while ExtensionWidgets has no entry for its extension", () => {
    const { container } = renderWidgets(extensionWidgetState({}));
    expect(
      container.querySelector('[data-widget-id="extensionWidget"]')
    ).not.toBeInTheDocument();
  });
});

describe("<Widgets> on a profile with no resolved region", () => {
  const REVEALED_BY_ROLLOUT = {
    region: "",
    "widgets.lists.enabled": true,
    "widgets.focusTimer.enabled": true,
    "widgets.clocks.enabled": true,
    trainhopConfig: {
      widgets: {
        enabled: true,
        listsEnabled: true,
        timerEnabled: true,
        clocksEnabled: true,
      },
    },
  };

  it("renders none of the widgets the rollout reveals", () => {
    const { container } = renderWidgets(
      makeNovaWidgetState([], REVEALED_BY_ROLLOUT)
    );
    expect(
      container.querySelector('[data-widget-id="lists"]')
    ).not.toBeInTheDocument();
    expect(
      container.querySelector('[data-widget-id="focusTimer"]')
    ).not.toBeInTheDocument();
    expect(
      container.querySelector('[data-widget-id="clocks"]')
    ).not.toBeInTheDocument();
  });

  it("renders them once a region resolves", () => {
    const { container } = renderWidgets(
      makeNovaWidgetState([], { ...REVEALED_BY_ROLLOUT, region: "FR" })
    );
    expect(
      container.querySelector('[data-widget-id="lists"]')
    ).toBeInTheDocument();
    expect(
      container.querySelector('[data-widget-id="focusTimer"]')
    ).toBeInTheDocument();
    expect(
      container.querySelector('[data-widget-id="clocks"]')
    ).toBeInTheDocument();
  });
});

describe("<Widgets> row toggle", () => {
  function novaStateWith(rowExpanded) {
    return makeNovaWidgetState([["lists", "medium"]], {
      "widgets.row.expanded": rowExpanded,
    });
  }

  function startRowHeightAnimation() {
    const { container, store } = renderWidgets(novaStateWith(false));
    const widgetsContainer = container.querySelector("#widgets-container");
    jest
      .spyOn(widgetsContainer, "getBoundingClientRect")
      .mockReturnValueOnce({ height: 40 })
      .mockReturnValueOnce({ height: 80 });

    // The click captures the pre-toggle height; the pref change round-trips
    // from the main process as PREF_CHANGED, flipping rowExpanded and running
    // the height animation effect.
    fireEvent.click(container.querySelector(".widgets-row-toggle"));
    act(() => {
      store.dispatch({
        type: at.PREF_CHANGED,
        data: { name: "widgets.row.expanded", value: true },
      });
    });

    return widgetsContainer;
  }

  it("renders the toggle button when nova is enabled", () => {
    const { container } = renderWidgets(novaStateWith(false));
    expect(container.querySelector(".widgets-row-toggle")).toBeInTheDocument();
  });

  it("labels the toggle 'show more' when the row is collapsed", () => {
    const { container } = renderWidgets(novaStateWith(false));
    const toggle = container.querySelector(".widgets-row-toggle");
    expect(toggle.getAttribute("data-l10n-id")).toBe(
      "newtab-widget-section-show-more"
    );
  });

  it("labels the toggle 'show less' when the row is expanded", () => {
    const { container } = renderWidgets(novaStateWith(true));
    const toggle = container.querySelector(".widgets-row-toggle");
    expect(toggle.getAttribute("data-l10n-id")).toBe(
      "newtab-widget-section-show-less"
    );
  });

  it("click flips the pref and dispatches WIDGETS_CONTAINER_ACTION", () => {
    const { container, store } = renderWidgets(novaStateWith(false));
    fireEvent.click(container.querySelector(".widgets-row-toggle"));
    const dispatched = store.dispatch.mock.calls.map(([action]) => action);
    expect(dispatched).toContainEqual(
      expect.objectContaining({
        type: at.SET_PREF,
        data: expect.objectContaining({
          name: "widgets.row.expanded",
          value: true,
        }),
      })
    );
    expect(dispatched).toContainEqual(
      expect.objectContaining({
        type: at.WIDGETS_CONTAINER_ACTION,
        data: expect.objectContaining({ action_value: "expand_row" }),
      })
    );
  });

  it("clears row height animation if transitionend does not fire", () => {
    jest.useFakeTimers();
    try {
      const widgetsContainer = startRowHeightAnimation();

      expect(widgetsContainer).toHaveClass("is-animating-height");
      expect(widgetsContainer.style.height).toBe("80px");

      act(() => {
        jest.runOnlyPendingTimers();
      });

      expect(widgetsContainer).not.toHaveClass("is-animating-height");
      expect(widgetsContainer.style.height).toBe("");
    } finally {
      jest.useRealTimers();
    }
  });

  it("clears row height animation on transitionend", () => {
    const widgetsContainer = startRowHeightAnimation();

    expect(widgetsContainer).toHaveClass("is-animating-height");
    expect(widgetsContainer.style.height).toBe("80px");

    fireEvent.transitionEnd(widgetsContainer, { propertyName: "height" });

    expect(widgetsContainer).not.toHaveClass("is-animating-height");
    expect(widgetsContainer.style.height).toBe("");
  });

  it("skips the row height animation under prefers-reduced-motion", () => {
    const originalMatchMedia = globalThis.matchMedia;
    globalThis.matchMedia = () => ({ matches: true });
    try {
      const { container, store } = renderWidgets(novaStateWith(false));
      const widgetsContainer = container.querySelector("#widgets-container");
      const rectSpy = jest.spyOn(widgetsContainer, "getBoundingClientRect");

      fireEvent.click(container.querySelector(".widgets-row-toggle"));
      act(() => {
        store.dispatch({
          type: at.PREF_CHANGED,
          data: { name: "widgets.row.expanded", value: true },
        });
      });

      // The container is never measured, so the FLIP animation never starts.
      expect(rectSpy).not.toHaveBeenCalled();
      expect(widgetsContainer).not.toHaveClass("is-animating-height");
      expect(widgetsContainer.style.height).toBe("");
    } finally {
      globalThis.matchMedia = originalMatchMedia;
    }
  });
});

describe("<Widgets> maximize toggle", () => {
  // Nova is disabled, but the size toggle is still offered and only flips
  // widgets.maximized (no per-widget size prefs).
  const nonNovaState = (extra = {}) => ({
    ...ENABLED_STATE,
    Prefs: {
      ...ENABLED_STATE.Prefs,
      values: {
        ...ENABLED_STATE.Prefs.values,
        "nova.enabled": false,
        "widgets.system.maximized": true,
        "widgets.maximized": false,
        ...extra,
      },
    },
  });

  const dispatchedActions = store =>
    store.dispatch.mock.calls.map(([action]) => action);

  it("dispatches a single SET_MULTIPLE_PREFS instead of one SetPref per widget", () => {
    const state = makeNovaWidgetState(
      [
        ["lists", "medium"],
        ["focusTimer", "medium"],
      ],
      {
        "widgets.system.maximized": true,
        "widgets.maximized": false,
        "widgets.weather.size": "small",
      }
    );
    const { container, store } = renderWidgets(state);

    const sizeToggle = container.querySelector("#toggle-widgets-size-button");
    expect(sizeToggle).toBeInTheDocument();
    fireEvent.click(sizeToggle);

    const dispatched = dispatchedActions(store);

    const setPrefsCalls = dispatched.filter(
      a => a.type === at.SET_MULTIPLE_PREFS
    );
    expect(setPrefsCalls).toHaveLength(1);

    const { values } = setPrefsCalls[0].data;
    expect(values["widgets.maximized"]).toBe(true);
    expect(values["widgets.lists.size"]).toBe("large");
    expect(values["widgets.focusTimer.size"]).toBe("large");
    // A sidebar widget at "small" (weather) stays in the sidebar.
    expect(values["widgets.weather.size"]).toBeUndefined();

    // The resize no longer fans out into per-widget SetPref actions.
    const sizeSetPrefCalls = dispatched.filter(
      a => a.type === at.SET_PREF && /^widgets\..*\.size$/.test(a.data?.name)
    );
    expect(sizeSetPrefCalls).toHaveLength(0);

    expect(dispatched).toContainEqual(
      expect.objectContaining({
        type: at.WIDGETS_CONTAINER_ACTION,
        data: expect.objectContaining({ action_value: "maximize_widgets" }),
      })
    );
  });

  it("sends non-small widgets to medium in one SET_MULTIPLE_PREFS when minimizing", () => {
    const state = makeNovaWidgetState(
      [
        ["lists", "large"],
        ["focusTimer", "large"],
      ],
      { "widgets.system.maximized": true, "widgets.maximized": true }
    );
    const { container, store } = renderWidgets(state);

    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));

    const dispatched = dispatchedActions(store);
    const setPrefs = dispatched.find(a => a.type === at.SET_MULTIPLE_PREFS);
    expect(setPrefs.data.values["widgets.maximized"]).toBe(false);
    expect(setPrefs.data.values["widgets.lists.size"]).toBe("medium");
    expect(setPrefs.data.values["widgets.focusTimer.size"]).toBe("medium");
    expect(dispatched).toContainEqual(
      expect.objectContaining({
        type: at.WIDGETS_CONTAINER_ACTION,
        data: expect.objectContaining({ action_value: "minimize_widgets" }),
      })
    );
  });

  it("does not include any size prefs when Nova is disabled", () => {
    const { container, store } = renderWidgets(nonNovaState());

    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));

    const setPrefs = dispatchedActions(store).find(
      a => a.type === at.SET_MULTIPLE_PREFS
    );
    expect(setPrefs.data.values["widgets.maximized"]).toBe(true);
    const sizeKeys = Object.keys(setPrefs.data.values).filter(k =>
      k.endsWith(".size")
    );
    expect(sizeKeys).toHaveLength(0);
  });

  it("toggles via Enter and Space but ignores other keys", () => {
    const state = makeNovaWidgetState([["lists", "medium"]], {
      "widgets.system.maximized": true,
      "widgets.maximized": false,
    });
    const { container, store } = renderWidgets(state);
    const sizeToggle = container.querySelector("#toggle-widgets-size-button");

    fireEvent.keyDown(sizeToggle, { key: "a" });
    expect(
      dispatchedActions(store).some(a => a.type === at.SET_MULTIPLE_PREFS)
    ).toBe(false);

    fireEvent.keyDown(sizeToggle, { key: "Enter" });
    fireEvent.keyDown(sizeToggle, { key: " " });
    const setPrefsCalls = dispatchedActions(store).filter(
      a => a.type === at.SET_MULTIPLE_PREFS
    );
    expect(setPrefsCalls).toHaveLength(2);
    expect(setPrefsCalls[0].data.values["widgets.maximized"]).toBe(true);
  });
});

describe("<Widgets> row-collapsed attribute", () => {
  it("is set on the widgets container when nova is enabled and the row is not expanded", () => {
    const state = makeNovaWidgetState([["lists", "medium"]], {
      "widgets.row.expanded": false,
    });
    const { container } = renderWidgets(state);
    const widgetsContainer = container.querySelector("#widgets-container");
    expect(widgetsContainer.hasAttribute("data-row-collapsed")).toBe(true);
  });

  it("is not set when the row is expanded", () => {
    const state = makeNovaWidgetState([["lists", "medium"]], {
      "widgets.row.expanded": true,
    });
    const { container } = renderWidgets(state);
    const widgetsContainer = container.querySelector("#widgets-container");
    expect(widgetsContainer.hasAttribute("data-row-collapsed")).toBe(false);
  });
});

describe("<Widgets> manage widgets menu item", () => {
  it("calls openWidgetsPanel when clicked", () => {
    const novaState = {
      ...ENABLED_STATE,
      Prefs: {
        ...ENABLED_STATE.Prefs,
        values: {
          ...ENABLED_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
    };
    const store = createStore(combineReducers(reducers), novaState);
    const openWidgetsPanel = jest.fn();
    const { container } = render(
      <Provider store={store}>
        <BaseContext.Provider value={{ openWidgetsPanel }}>
          <Widgets />
        </BaseContext.Provider>
      </Provider>
    );
    const manageItem = container.querySelector(
      "[data-l10n-id='newtab-widget-section-menu-manage']"
    );
    expect(manageItem).toBeInTheDocument();
    fireEvent.click(manageItem);
    expect(openWidgetsPanel).toHaveBeenCalledTimes(1);
  });
});

describe("<Widgets> maximize toggle size sync", () => {
  // The size toggle button only renders when widgets may be maximized.
  const MAXIMIZABLE = { "widgets.system.maximized": true };

  // Collect the size prefs the maximize toggle batches into its single
  // SET_MULTIPLE_PREFS, shaped like the old per-widget SetPref actions for assertions.
  function sizePrefSets(store) {
    const setPrefs = store.dispatch.mock.calls
      .map(([action]) => action)
      .find(action => action.type === at.SET_MULTIPLE_PREFS);
    const values = setPrefs?.data?.values ?? {};
    return Object.entries(values)
      .filter(([name]) => name.endsWith(".size"))
      .map(([name, value]) => ({ data: { name, value } }));
  }

  function clickMaximize(container) {
    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));
  }

  it("resizes a small widget sitting in the row to large", () => {
    const state = makeNovaWidgetState(
      [
        ["lists", "medium"],
        ["clocks", "small"],
      ],
      MAXIMIZABLE
    );
    const { container, store } = renderWidgets(state);
    expect(
      container.querySelector("#toggle-widgets-size-button")
    ).toBeInTheDocument();

    clickMaximize(container);

    expect(sizePrefSets(store)).toContainEqual(
      expect.objectContaining({
        data: expect.objectContaining({
          name: "widgets.clocks.size",
          value: "large",
        }),
      })
    );
  });

  it("sends a small in-row widget to medium when minimizing", () => {
    const state = makeNovaWidgetState([["clocks", "small"]], {
      ...MAXIMIZABLE,
      "widgets.maximized": true,
    });
    const { container, store } = renderWidgets(state);

    clickMaximize(container);

    expect(sizePrefSets(store)).toContainEqual(
      expect.objectContaining({
        data: expect.objectContaining({
          name: "widgets.clocks.size",
          value: "medium",
        }),
      })
    );
  });

  it("skips a widget rendered in the sidebar (small + hasSidebar)", () => {
    const state = makeNovaWidgetState(
      [
        ["clocks", "medium"],
        ["weather", "small"],
      ],
      MAXIMIZABLE
    );
    const { container, store } = renderWidgets(state);

    clickMaximize(container);

    const weatherSets = sizePrefSets(store).filter(
      action => action.data.name === "widgets.weather.size"
    );
    expect(weatherSets).toHaveLength(0);
  });

  it("resizes a small widget that lost the sidebar slot", () => {
    const state = makeNovaWidgetState(
      [
        ["clocks", "medium"],
        ["weather", "small"],
      ],
      {
        ...MAXIMIZABLE,
        "widgets.privacy.enabled": true,
        "widgets.system.privacy.enabled": true,
        "widgets.privacy.size": "small",
        trainhopConfig: { widgets: { privacySidebar: true } },
      }
    );
    const { container, store } = renderWidgets(state);

    clickMaximize(container);

    const sets = sizePrefSets(store);
    expect(sets).toContainEqual(
      expect.objectContaining({
        data: expect.objectContaining({
          name: "widgets.privacy.size",
          value: "large",
        }),
      })
    );
    expect(
      sets.filter(action => action.data.name === "widgets.weather.size")
    ).toHaveLength(0);
  });

  it("resizes weather when it is in the row (not small)", () => {
    const state = makeNovaWidgetState(
      [
        ["clocks", "medium"],
        ["weather", "medium"],
      ],
      MAXIMIZABLE
    );
    const { container, store } = renderWidgets(state);

    clickMaximize(container);

    expect(sizePrefSets(store)).toContainEqual(
      expect.objectContaining({
        data: expect.objectContaining({
          name: "widgets.weather.size",
          value: "large",
        }),
      })
    );
  });

  it("resizes a disabled small in-row widget", () => {
    const state = makeNovaWidgetState([["lists", "medium"]], {
      ...MAXIMIZABLE,
      "widgets.system.clocks.enabled": true,
      "widgets.clocks.enabled": false,
      "widgets.clocks.size": "small",
    });
    const { container, store } = renderWidgets(state);

    clickMaximize(container);

    expect(sizePrefSets(store)).toContainEqual(
      expect.objectContaining({
        data: expect.objectContaining({
          name: "widgets.clocks.size",
          value: "large",
        }),
      })
    );
  });
});

describe("<Widgets> header controls", () => {
  // Everything isSideBySideActive gates on beyond the widget prefs
  // makeNovaWidgetState already sets.
  const SIDE_BY_SIDE = {
    "pageLayouts.variant": "side-by-side-content-lead",
    "feeds.section.topstories": true,
    "feeds.system.topstories": true,
    "widgets.system.enabled": true,
  };
  // An addable widget left off, so allWidgetsAdded is false and the tile is not
  // suppressed for that unrelated reason.
  const ONE_WIDGET_UNADDED = {
    "widgets.system.clocks.enabled": true,
    "widgets.clocks.enabled": false,
  };

  it("renders only the placeholder tile in the default full-width layout", () => {
    const { container } = renderWidgets(
      makeNovaWidgetState([["lists", "medium"]], ONE_WIDGET_UNADDED)
    );

    expect(container.querySelector(".widgets-add-button")).toBeInTheDocument();
    expect(container.querySelector("#add-widgets-button")).toBeNull();
  });

  // Two controls sharing the "Add widget" accessible name in one region.
  it("renders only the header button when side-by-side is active", () => {
    const { container } = renderWidgets(
      makeNovaWidgetState([["lists", "medium"]], {
        ...ONE_WIDGET_UNADDED,
        ...SIDE_BY_SIDE,
      })
    );

    expect(container.querySelector("#add-widgets-button")).toBeInTheDocument();
    expect(container.querySelector(".widgets-add-button")).toBeNull();
  });

  // Bug 2063604: the row toggle and the per-widget submenu are separate gates.
  it("keeps per-widget size options while hiding the row toggle in side-by-side", () => {
    const { container } = renderWidgets(
      makeNovaWidgetState([["lists", "medium"]], {
        ...SIDE_BY_SIDE,
        "widgets.system.maximized": true,
      })
    );

    expect(container.querySelector("#toggle-widgets-size-button")).toBeNull();
    expect(
      container.querySelector('[data-l10n-id="newtab-widget-menu-change-size"]')
    ).toBeInTheDocument();
  });
});

// experiment / bug 2066527 (remove)
describe("<Widgets> auto-minimize layout", () => {
  const PREF_OVERRIDE = "widgets.autoMinimize.userOverride";

  function makeState(values = {}) {
    return {
      ...ENABLED_STATE,
      Prefs: {
        ...ENABLED_STATE.Prefs,
        values: {
          ...ENABLED_STATE.Prefs.values,
          "nova.enabled": true,
          "widgets.system.maximized": true,
          "widgets.maximized": false,
          ...values,
        },
      },
    };
  }

  function setReducedMotion(matches) {
    globalThis.matchMedia = () => ({
      matches,
      addListener: () => {},
      removeListener: () => {},
      addEventListener: () => {},
      removeEventListener: () => {},
    });
  }

  function containerActions(store) {
    return store.dispatch.mock.calls
      .map(([action]) => action)
      .filter(action => action?.type === at.WIDGETS_CONTAINER_ACTION)
      .map(action => action.data);
  }

  const widgetsContainer = container =>
    container.querySelector("#widgets-container");

  let realMatchMedia;
  beforeEach(() => {
    realMatchMedia = globalThis.matchMedia;
    jest.useFakeTimers();
  });
  afterEach(() => {
    jest.useRealTimers();
    globalThis.matchMedia = realMatchMedia;
  });

  it("collapses the section after the configured delay", () => {
    const { container } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 3000,
      })
    );

    act(() => {
      jest.advanceTimersByTime(2999);
    });
    expect(widgetsContainer(container)).not.toHaveAttribute(
      "data-section-collapsed"
    );

    act(() => {
      jest.advanceTimersByTime(1);
    });
    expect(widgetsContainer(container)).toHaveAttribute(
      "data-section-collapsed"
    );
  });

  it("prefers the trainhopConfig delay over the pref", () => {
    const { container } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 3000,
        trainhopConfig: { pageLayouts: { autoMinimizeDelayMs: 500 } },
      })
    );

    act(() => {
      jest.advanceTimersByTime(500);
    });
    expect(widgetsContainer(container)).toHaveAttribute(
      "data-section-collapsed"
    );
  });

  it("does not collapse outside the variant", () => {
    const { container, store } = renderWidgets(makeState());

    act(() => {
      jest.advanceTimersByTime(60000);
    });
    expect(widgetsContainer(container)).not.toHaveAttribute(
      "data-section-collapsed"
    );
    expect(containerActions(store)).toEqual([]);
  });

  it("does not collapse once the user has overridden", () => {
    const { container } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        [PREF_OVERRIDE]: true,
      })
    );

    act(() => {
      jest.advanceTimersByTime(60000);
    });
    expect(widgetsContainer(container)).not.toHaveAttribute(
      "data-section-collapsed"
    );
  });

  it("does not collapse under prefers-reduced-motion", () => {
    setReducedMotion(true);
    const { container, store } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 100,
      })
    );

    act(() => {
      jest.advanceTimersByTime(60000);
    });
    expect(widgetsContainer(container)).not.toHaveAttribute(
      "data-section-collapsed"
    );
    expect(containerActions(store)).toEqual([]);
  });

  it("makes the collapsed container inert so it leaves the tab order", () => {
    const { container } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 100,
      })
    );
    expect(widgetsContainer(container)).not.toHaveAttribute("inert");

    act(() => {
      jest.advanceTimersByTime(100);
    });
    expect(widgetsContainer(container)).toHaveAttribute("inert");
  });

  it("leaves the section open if focus is inside the row", () => {
    const { container, store } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 100,
      })
    );
    const focusable = widgetsContainer(container).querySelector(
      "button, input, [tabindex]"
    );
    expect(focusable).toBeTruthy();
    focusable.focus();

    act(() => {
      jest.advanceTimersByTime(60000);
    });
    expect(widgetsContainer(container)).not.toHaveAttribute(
      "data-section-collapsed"
    );
    expect(containerActions(store)).toEqual([]);
  });

  it("does not re-arm the timer when an unrelated pref changes", () => {
    const { container, store } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 100,
      })
    );
    act(() => {
      jest.advanceTimersByTime(100);
    });

    // Any PREF_CHANGED hands the component a fresh prefs object.
    act(() => {
      store.dispatch({
        type: at.PREF_CHANGED,
        data: { name: "widgets.hideAllToast.enabled", value: true },
      });
    });
    act(() => {
      jest.advanceTimersByTime(60000);
    });

    expect(widgetsContainer(container)).toHaveAttribute(
      "data-section-collapsed"
    );
  });

  it("uncollapses if the variant is turned off after auto-collapsing", () => {
    const { container, store } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 100,
      })
    );
    act(() => {
      jest.advanceTimersByTime(100);
    });
    expect(widgetsContainer(container)).toHaveAttribute(
      "data-section-collapsed"
    );

    act(() => {
      store.dispatch({
        type: at.PREF_CHANGED,
        data: { name: "pageLayouts.variant", value: "nova-full-width" },
      });
    });

    expect(widgetsContainer(container)).not.toHaveAttribute(
      "data-section-collapsed"
    );
    expect(widgetsContainer(container)).not.toHaveAttribute("inert");
  });

  it("expands and records the override when the header button is used", () => {
    const { container, store } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        "pageLayouts.autoMinimizeDelayMs": 100,
      })
    );
    act(() => {
      jest.advanceTimersByTime(100);
    });

    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));

    const setPref = store.dispatch.mock.calls
      .map(([action]) => action)
      .find(
        action =>
          action?.type === at.SET_PREF && action.data?.name === PREF_OVERRIDE
      );
    expect(setPref).toBeTruthy();
    expect(setPref.data.value).toBe(true);
    expect(
      containerActions(store).some(
        data =>
          data.action_type === "auto_minimize" &&
          data.action_value === "expand_section"
      )
    ).toBe(true);
  });

  it("returns the header button to the size toggle once overridden", () => {
    const { container, store } = renderWidgets(
      makeState({
        "pageLayouts.variant": "auto-minimize-widgets",
        [PREF_OVERRIDE]: true,
      })
    );

    fireEvent.click(container.querySelector("#toggle-widgets-size-button"));

    expect(
      containerActions(store).some(
        data => data.action_type === "change_size_all"
      )
    ).toBe(true);
  });
});

// @experiment(remove) { bug 2069496 }
describe("<Widgets> row ad slot", () => {
  const SPOC = {
    id: 1,
    flight_id: 2,
    url: "https://example.com/ad",
    title: "Ad title",
    raw_image_src: "https://example.com/ad.jpg",
    domain: "example.com",
    sponsor: "Example",
    shim: {},
  };

  // Three enabled widgets, so a reserved slot has to push one out.
  function adState(values = {}, spocs = [SPOC]) {
    return {
      ...ENABLED_STATE,
      Prefs: {
        ...ENABLED_STATE.Prefs,
        values: {
          ...ENABLED_STATE.Prefs.values,
          "nova.enabled": true,
          "pageLayouts.variant": "widgets-ad-large",
          showSponsored: true,
          "system.showSponsored": true,
          "widgets.system.enabled": true,
          // Ships as true, which is what makes medium the default row size.
          "widgets.system.maximized": true,
          "widgets.system.clocks.enabled": true,
          "widgets.clocks.enabled": true,
          "widgets.system.stocks.enabled": true,
          "widgets.stocks.enabled": true,
          ...values,
        },
      },
      DiscoveryStream: {
        ...ENABLED_STATE.DiscoveryStream,
        spocs: {
          loaded: true,
          blocked: [],
          data: { newtab_spocs: { items: spocs } },
        },
      },
    };
  }

  const adCell = container => container.querySelector(".widgets-row-ad-cell");
  const widgetCells = container =>
    container.querySelectorAll(".widget-wrapper[data-widget-id]");

  it("renders the ad cell only inside the variant", () => {
    expect(adCell(renderWidgets(adState()).container)).toBeTruthy();
    expect(
      adCell(
        renderWidgets(adState({ "pageLayouts.variant": "nova-full-width" }))
          .container
      )
    ).toBeFalsy();
  });

  it("does not render a cell when no spoc is eligible", () => {
    expect(adCell(renderWidgets(adState({}, [])).container)).toBeFalsy();
  });

  // The ad takes the last slot, so widgets get one fewer than the column
  // count. Hiding starts one index earlier than it would without an ad.
  it("gives widgets one slot fewer, so a widget is what the row hides", () => {
    const { container } = renderWidgets(adState());
    const cells = widgetCells(container);
    expect(cells).toHaveLength(3);

    // At three card columns the ad holds slot three, so widget index two goes.
    expect(cells[0]).not.toHaveAttribute("data-hidden-3");
    expect(cells[1]).not.toHaveAttribute("data-hidden-3");
    expect(cells[2]).toHaveAttribute("data-hidden-3");

    // At four columns all three widgets still fit alongside the ad.
    expect(cells[2]).not.toHaveAttribute("data-hidden-4");
  });

  it("hides widgets at the column count when there is no ad", () => {
    const { container } = renderWidgets(adState({}, []));
    const cells = widgetCells(container);
    expect(cells).toHaveLength(3);
    expect(cells[2]).not.toHaveAttribute("data-hidden-3");
  });

  // One card column is the exception: reserving the only slot would leave no
  // widgets, so the ad steps aside and widgets keep every slot.
  it("does not take a slot from widgets at one card column", () => {
    const { container } = renderWidgets(adState());
    expect(widgetCells(container)[0]).not.toHaveAttribute("data-hidden-1");
  });

  it("never marks the ad cell itself as hidden", () => {
    const cell = adCell(renderWidgets(adState()).container);
    for (const cols of [1, 2, 3, 4, 5]) {
      expect(cell).not.toHaveAttribute(`data-hidden-${cols}`);
    }
  });

  // At one card column the slot is markup order, not CSS, because a drag
  // preview puts an inline `order` on every widget that would beat a
  // stylesheet one on the ad.
  it("renders right after the first widget", () => {
    const { container } = renderWidgets(adState());
    const cells = [
      ...container.querySelectorAll("#widgets-container > .widget-wrapper"),
    ];

    expect(cells[1]).toHaveClass("widgets-row-ad-cell");
    expect(cells[0]).not.toHaveClass("widgets-row-ad-cell");
    expect(cells).toHaveLength(4);
  });

  // jsdom measures everything as a zero rect, so the drag has to be told
  // where the stacked slots are before it can map the cursor to one.
  const stackSlots = slots =>
    slots.forEach((el, i) => {
      el.getBoundingClientRect = () => ({
        left: 0,
        right: 100,
        top: i * 100,
        bottom: i * 100 + 100,
        width: 100,
        height: 100,
        x: 0,
        y: i * 100,
        toJSON() {},
      });
    });

  const pointer = (type, clientY) =>
    new PointerEvent(type, {
      bubbles: true,
      pointerId: 1,
      pointerType: "mouse",
      button: 0,
      clientX: 50,
      clientY,
    });

  // Holds the last widget over the first slot without dropping it.
  const dragLastToFront = container => {
    const slots = [...widgetCells(container)];
    stackSlots(slots);
    act(() => {
      slots.at(-1).dispatchEvent(pointer("pointerdown", 250));
    });
    act(() => {
      window.dispatchEvent(pointer("pointermove", 240));
    });
    act(() => {
      window.dispatchEvent(pointer("pointermove", 50));
    });
  };

  // The preview puts an inline `order` on every widget and the ad keeps the
  // default 0, so it ties with whichever widget is shown first and only stays
  // behind it by sitting later in the markup.
  it.each([
    ["large", "medium"],
    ["medium", "large"],
  ])(
    "follows the widget dragged to the front, %s over %s",
    (held, resident) => {
      const { container } = renderWidgets(
        adState({
          "widgets.order": "clocks,lists,stocks",
          "widgets.clocks.size": resident,
          "widgets.stocks.size": held,
        })
      );

      dragLastToFront(container);

      const dragged = container.querySelector(".widget-wrapper[data-dragging]");
      expect(dragged).toHaveAttribute("data-widget-id", "stocks");
      expect(dragged.nextElementSibling).toHaveClass("widgets-row-ad-cell");
    }
  );

  // CSS puts the card on the slot after the last widget, so it needs the
  // count. Capped at four, the most any width shows ahead of it.
  it("reports how many widgets sit ahead of it", () => {
    const { container } = renderWidgets(adState());
    expect(adCell(container)).toHaveAttribute("data-widgets-before", "3");

    const one = renderWidgets(
      adState({
        "widgets.clocks.enabled": false,
        "widgets.stocks.enabled": false,
      })
    ).container;
    expect(adCell(one)).toHaveAttribute("data-widgets-before", "1");
  });

  it("is not draggable, so reordering widgets leaves it alone", () => {
    const cell = adCell(renderWidgets(adState()).container);
    expect(cell).not.toHaveClass("widget-draggable");
    expect(cell).not.toHaveAttribute("data-widget-id");
  });

  // The card is a link, which Firefox makes draggable on its own. Fired on
  // the link so the handler has to catch it on the way up, as it does live.
  it("cancels the link drag the card would otherwise start", () => {
    const cell = adCell(renderWidgets(adState()).container);
    const link = cell.querySelector("a[href]");
    expect(link).toBeTruthy();

    const dragStart = createEvent.dragStart(link);
    fireEvent(link, dragStart);
    expect(dragStart.defaultPrevented).toBe(true);
  });

  // There is no half-height sponsored card yet, so the row's size toggle
  // leaves the ad alone. Bug 2069496 follow-up.
  it("stays large when the row is minimized", () => {
    for (const maximized of [false, true]) {
      const cell = adCell(
        renderWidgets(adState({ "widgets.maximized": maximized })).container
      );
      expect(cell).toHaveClass("large-widget");
      expect(cell).not.toHaveClass("medium-widget");
    }
  });
});
