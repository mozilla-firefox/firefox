/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Weather } from "content-src/components/Widgets/Weather/Weather";

const weatherSuggestion = {
  current_conditions: {
    icon_id: 3,
    summary: "Partly Cloudy",
    temperature: { c: 20, f: 68 },
  },
  forecast: {
    high: { c: 25, f: 77 },
    low: { c: 15, f: 59 },
    url: "https://example.com",
  },
};

const hourlyForecasts = [
  {
    epoch_date_time: 1000000000,
    temperature: { c: 18, f: 64 },
    icon_id: 5,
    summary: "Partly Cloudy",
    date_time: "2024-01-15T14:00:00",
    url: "https://example.com/forecast",
  },
  {
    epoch_date_time: 1000003600,
    temperature: { c: 17, f: 62 },
    icon_id: 6,
    summary: "Mostly Cloudy",
    date_time: "2024-01-15T15:00:00",
  },
  {
    epoch_date_time: 1000007200,
    temperature: { c: 16, f: 61 },
    icon_id: 7,
    summary: "Cloudy",
    date_time: "2024-01-15T16:00:00",
  },
];

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "weather.temperatureUnits": "f",
      "weather.locationSearchEnabled": true,
      "system.showWeatherOptIn": false,
      "widgets.system.enabled": true,
      "widgets.enabled": true,
      "widgets.system.weather.enabled": true,
      "widgets.weather.enabled": true,
      "widgets.weather.size": "medium",
      "widgets.system.maximized": true,
    },
  },
  Weather: {
    initialized: true,
    searchActive: false,
    locationData: { city: "Testville" },
    suggestions: [weatherSuggestion],
    hourlyForecasts,
  },
};

const optInMockState = {
  ...mockState,
  Prefs: {
    ...mockState.Prefs,
    values: {
      ...mockState.Prefs.values,
      "system.showWeatherOptIn": true,
      "weather.optInDisplayed": true,
      "weather.optInAccepted": false,
    },
  },
};

// Opt-in is enabled (region-based) but user has already accepted, so showOptInState=false
const optInAcceptedState = {
  ...optInMockState,
  Prefs: {
    ...optInMockState.Prefs,
    values: {
      ...optInMockState.Prefs.values,
      "weather.optInAccepted": true,
    },
  },
};

function WrapWithProvider({ children, state = INITIAL_STATE }) {
  const store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

function renderWeather(
  size = "medium",
  state = mockState,
  dispatch = jest.fn()
) {
  const { container } = render(
    <WrapWithProvider state={state}>
      <Weather dispatch={dispatch} size={size} />
    </WrapWithProvider>
  );
  return { container, dispatch };
}

describe("<Weather> (Widgets/Weather)", () => {
  describe("rendering", () => {
    it("renders the weather widget", () => {
      const { container } = renderWeather();
      expect(container.querySelector(".weather-widget")).toBeInTheDocument();
    });

    it("does not render when weather is not initialized", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, initialized: false },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-widget")
      ).not.toBeInTheDocument();
    });

    it("does not render when widgets.weather.enabled is false", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.weather.enabled": false,
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-widget")
      ).not.toBeInTheDocument();
    });

    it("does not render when widgets.system.weather.enabled is false and no experiment", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.system.weather.enabled": false,
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-widget")
      ).not.toBeInTheDocument();
    });

    it("renders correctly when transitioning from uninitialized to initialized", () => {
      const store = createStore(combineReducers(reducers), {
        ...mockState,
        Weather: { ...mockState.Weather, initialized: false },
      });

      const { container } = render(
        <Provider store={store}>
          <Weather dispatch={jest.fn()} size="small" />
        </Provider>
      );

      expect(
        container.querySelector(".weather-widget")
      ).not.toBeInTheDocument();

      act(() => {
        store.dispatch({
          type: at.WEATHER_UPDATE,
          data: {
            suggestions: mockState.Weather.suggestions,
            hourlyForecasts: mockState.Weather.hourlyForecasts,
            lastUpdated: Date.now(),
            locationData: mockState.Weather.locationData,
          },
        });
      });

      expect(container.querySelector(".weather-widget")).toBeInTheDocument();
    });
  });

  describe("size-driven views", () => {
    it("size=small renders conditions view and no hourly grid", () => {
      const { container } = renderWeather("small");
      expect(
        container.querySelector(".weather-conditions-view")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".forecast-row-items")
      ).not.toBeInTheDocument();
    });

    it("size=medium renders hourly forecast grid and conditions view", () => {
      const { container } = renderWeather("medium");
      expect(
        container.querySelector(".weather-conditions-view")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".forecast-row-items")
      ).toBeInTheDocument();
    });

    it("size=large renders both conditions view and hourly forecast grid", () => {
      const { container } = renderWeather("large");
      expect(
        container.querySelector(".weather-conditions-view")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".forecast-row-items")
      ).toBeInTheDocument();
    });
  });

  describe("city name", () => {
    it("displays city name when searchActive is false", () => {
      const { container } = renderWeather();
      expect(container.querySelector(".widget-title h3").textContent).toBe(
        "Testville"
      );
    });

    it("does not show city h3 when searchActive is true", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".widget-title h3")
      ).not.toBeInTheDocument();
    });
  });

  describe("high/low temperature (size=small)", () => {
    it("renders high temperature arrow with correct l10n id", () => {
      const { container } = renderWeather("small");
      expect(
        container.querySelector(
          ".high-temperature .arrow-icon.arrow-up[data-l10n-id='newtab-weather-high']"
        )
      ).toBeInTheDocument();
    });

    it("renders low temperature arrow with correct l10n id", () => {
      const { container } = renderWeather("small");
      expect(
        container.querySelector(
          ".low-temperature .arrow-icon.arrow-down[data-l10n-id='newtab-weather-low']"
        )
      ).toBeInTheDocument();
    });
  });

  describe("context menu", () => {
    it("renders the context menu button", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          ".weather-context-menu-button[data-l10n-id='newtab-menu-section-tooltip']"
        )
      ).toBeInTheDocument();
    });

    it("contains change-location when locationSearchEnabled", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-location']"
        )
      ).toBeInTheDocument();
    });

    it("contains detect-my-location when opt-in is enabled and user has accepted", () => {
      const { container } = renderWeather("medium", optInAcceptedState);
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-detect-my-location']"
        )
      ).toBeInTheDocument();
    });

    it("does not contain detect-my-location when opt-in is disabled", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-detect-my-location']"
        )
      ).not.toBeInTheDocument();
    });

    it("shows change-to-celsius when units are fahrenheit", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-temperature-units-celsius']"
        )
      ).toBeInTheDocument();
    });

    it("shows change-to-fahrenheit when units are celsius", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "weather.temperatureUnits": "c",
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-temperature-units-fahrenheit']"
        )
      ).toBeInTheDocument();
    });

    it("does not contain simple/detailed display toggle items", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-weather-display-simple']"
        )
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-weather-display-detailed']"
        )
      ).not.toBeInTheDocument();
    });

    it("contains size submenu with small, medium, large items", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-size-small']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-size-medium']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-size-large']"
        )
      ).toBeInTheDocument();
    });

    it("checks the current size in the submenu (medium)", () => {
      const { container } = renderWeather();
      expect(
        container
          .querySelector("panel-item[data-l10n-id='newtab-widget-size-medium']")
          .hasAttribute("checked")
      ).toBe(true);
      expect(
        container
          .querySelector("panel-item[data-l10n-id='newtab-widget-size-small']")
          .hasAttribute("checked")
      ).toBe(false);
    });

    it("hides change-size submenu when widgets.system.maximized is false", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.system.maximized": false,
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).not.toBeInTheDocument();
    });

    it("shows change-size submenu when widgets.system.maximized is true", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
    });

    it("trainhopConfig.widgets.enabled overrides widgets.system.enabled=false", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.system.enabled": false,
            trainhopConfig: { widgets: { enabled: true } },
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
    });

    it("trainhopConfig.widgets.enabled overrides widgets.enabled=false", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.enabled": false,
            trainhopConfig: { widgets: { enabled: true } },
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
    });

    it("trainhopConfig.widgets.maximized overrides widgets.system.maximized=false", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.system.maximized": false,
            trainhopConfig: { widgets: { maximized: true } },
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
    });

    it("hides change-size submenu when both widgets.enabled is false and trainhopConfig is absent", () => {
      const state = {
        ...mockState,
        Prefs: {
          ...mockState.Prefs,
          values: {
            ...mockState.Prefs.values,
            "widgets.enabled": false,
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).not.toBeInTheDocument();
    });

    it("contains hide and learn-more items", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-menu-hide']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-learn-more']"
        )
      ).toBeInTheDocument();
    });

    it("hides temp unit items when opt-in is enabled (shortened menu)", () => {
      const { container } = renderWeather("small", optInMockState);
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-temperature-units-celsius']"
        )
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-temperature-units-fahrenheit']"
        )
      ).not.toBeInTheDocument();
    });
  });

  describe("context menu actions & telemetry", () => {
    it("dispatches WEATHER_SEARCH_ACTIVE and WIDGETS_USER_EVENT on change-location click", () => {
      const { container, dispatch } = renderWeather();
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-weather-menu-change-location']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.WEATHER_SEARCH_ACTIVE,
        data: true,
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "weather",
          widget_source: "context_menu",
          user_action: "change_location",
          widget_size: "medium",
        }),
      });
    });

    it("dispatches WEATHER_USER_OPT_IN_LOCATION and WIDGETS_USER_EVENT on detect-location click", () => {
      const { container, dispatch } = renderWeather(
        "medium",
        optInAcceptedState
      );
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-weather-menu-detect-my-location']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.WEATHER_USER_OPT_IN_LOCATION,
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          user_action: "detect_location",
          widget_size: "medium",
        }),
      });
    });

    it("dispatches SET_PREF(weather.temperatureUnits, 'c') on celsius click", () => {
      const { container, dispatch } = renderWeather();
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-weather-menu-change-temperature-units-celsius']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "weather.temperatureUnits", value: "c" },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          user_action: "change_temperature_units",
          action_value: "c",
          widget_size: "medium",
        }),
      });
    });

    it("dispatches SET_PREF(widgets.weather.size) and WIDGETS_USER_EVENT on size submenu click", () => {
      const { container, dispatch } = renderWeather();
      const submenuNode = container.querySelector(
        "panel-list[id='weather-size-submenu']"
      );
      const mockItem = document.createElement("div");
      mockItem.dataset.size = "small";
      const event = new MouseEvent("click", { bubbles: true });
      Object.defineProperty(event, "composedPath", {
        value: () => [mockItem],
      });
      submenuNode.dispatchEvent(event);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "widgets.weather.size", value: "small" },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          user_action: "change_size",
          action_value: "small",
          widget_size: "small",
        }),
      });
    });

    it("dispatches SET_PREF(widgets.weather.size, large) on large size click", () => {
      const { container, dispatch } = renderWeather();
      const submenuNode = container.querySelector(
        "panel-list[id='weather-size-submenu']"
      );
      const mockItem = document.createElement("div");
      mockItem.dataset.size = "large";
      const event = new MouseEvent("click", { bubbles: true });
      Object.defineProperty(event, "composedPath", {
        value: () => [mockItem],
      });
      submenuNode.dispatchEvent(event);

      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "widgets.weather.size", value: "large" },
      });
    });

    it("dispatches SET_PREF(widgets.weather.enabled, false) and WIDGETS_ENABLED on hide click", () => {
      const { container, dispatch } = renderWeather();
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-widget-menu-hide']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.SET_PREF,
        data: { name: "widgets.weather.enabled", value: false },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_ENABLED,
        data: expect.objectContaining({
          widget_name: "weather",
          enabled: false,
          widget_size: "medium",
        }),
      });
    });

    it("dispatches OPEN_LINK and WIDGETS_USER_EVENT on learn-more click", () => {
      const { container, dispatch } = renderWeather();
      const item = container.querySelector(
        "panel-item[data-l10n-id='newtab-weather-menu-learn-more']"
      );
      fireEvent.click(item);

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.OPEN_LINK,
        data: {
          url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
        },
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          user_action: "learn_more",
          widget_size: "medium",
        }),
      });
    });

    it("widget_size in telemetry reflects the size prop", () => {
      const { container, dispatch } = renderWeather("large");
      fireEvent.click(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-location']"
        )
      );
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({ widget_size: "large" }),
      });
    });
  });

  describe("hourly forecast", () => {
    it("renders one li per hourly forecast entry", () => {
      const { container } = renderWeather("medium");
      expect(container.querySelectorAll(".forecast-row-items li")).toHaveLength(
        hourlyForecasts.length
      );
    });

    it("renders correct weather icon class for each forecast item", () => {
      const { container } = renderWeather("medium");
      const items = container.querySelectorAll(".forecast-row-items li");
      items.forEach((item, i) => {
        expect(
          item.querySelector(
            `.weather-icon.iconId${hourlyForecasts[i].icon_id}`
          )
        ).toBeInTheDocument();
      });
    });

    it("renders aria-label with summary for each weather icon", () => {
      const { container } = renderWeather("medium");
      const icons = container.querySelectorAll(
        ".forecast-row-items li .weather-icon"
      );
      icons.forEach((icon, i) => {
        expect(icon.getAttribute("aria-label")).toBe(
          hourlyForecasts[i].summary
        );
      });
    });

    it("renders empty list when hourlyForecasts is empty", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, hourlyForecasts: [] },
      };
      const { container } = renderWeather("medium", state);
      expect(container.querySelectorAll(".forecast-row-items li")).toHaveLength(
        0
      );
    });
  });

  describe("error state", () => {
    it("renders weather-error when current_conditions is missing", () => {
      const state = {
        ...mockState,
        Weather: {
          ...mockState.Weather,
          suggestions: [{ forecast: weatherSuggestion.forecast }],
        },
      };
      const { container } = renderWeather("medium", state);
      expect(container.querySelector(".weather-error")).toBeInTheDocument();
      expect(
        container.querySelector(
          ".weather-error p[data-l10n-id='newtab-weather-error-not-available']"
        )
      ).toBeInTheDocument();
    });

    it("renders weather-error when forecast is missing", () => {
      const state = {
        ...mockState,
        Weather: {
          ...mockState.Weather,
          suggestions: [
            { current_conditions: weatherSuggestion.current_conditions },
          ],
        },
      };
      const { container } = renderWeather("medium", state);
      expect(container.querySelector(".weather-error")).toBeInTheDocument();
    });

    it("adds weather-error-state class to root element on error", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, suggestions: [{}] },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-widget.weather-error-state")
      ).toBeInTheDocument();
    });

    it("does not render weather-anchor on error", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, suggestions: [{}] },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-anchor")
      ).not.toBeInTheDocument();
    });
  });

  describe("provider link / anchor", () => {
    it("renders weather-anchor as <a> with aria-label=city (medium)", () => {
      const { container } = renderWeather("medium");
      const anchor = container.querySelector(".weather-anchor");
      expect(anchor).toBeInTheDocument();
      expect(anchor.tagName).toBe("A");
      expect(anchor.getAttribute("aria-label")).toBe("Testville");
    });

    it("dispatches WIDGETS_USER_EVENT when weather-anchor is clicked", () => {
      const { container, dispatch } = renderWeather("medium");
      fireEvent.click(container.querySelector(".weather-anchor"));

      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "weather",
          widget_source: "widget",
          user_action: "provider_link_click",
        }),
      });
    });

    it("renders a.full-forecast with hourly forecast URL", () => {
      const { container } = renderWeather("medium");
      const link = container.querySelector("a.full-forecast");
      expect(link).toBeInTheDocument();
      expect(link.getAttribute("href")).toBe(hourlyForecasts[0].url);
    });

    it("dispatches WIDGETS_USER_EVENT when full-forecast is clicked", () => {
      const { container, dispatch } = renderWeather("medium");
      fireEvent.click(container.querySelector("a.full-forecast"));

      expect(dispatch).toHaveBeenCalledTimes(1);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({ user_action: "provider_link_click" }),
      });
    });

    it("renders weather-anchor linking to conditions URL for size=small", () => {
      const { container } = renderWeather("small");
      const anchor = container.querySelector(".weather-anchor");
      expect(anchor).toBeInTheDocument();
      expect(anchor.getAttribute("href")).toBe(weatherSuggestion.forecast.url);
    });
  });

  describe("opt-in state", () => {
    it("renders weather-opt-in-container when opt-in is enabled and user has not accepted", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(".weather-opt-in-container")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".weather-container")
      ).not.toBeInTheDocument();
    });

    it("renders weather-container when opt-in is enabled and user has accepted", () => {
      const { container } = renderWeather("medium", optInAcceptedState);
      expect(container.querySelector(".weather-container")).toBeInTheDocument();
      expect(
        container.querySelector(".weather-opt-in-container")
      ).not.toBeInTheDocument();
    });

    it("renders weather-container when opt-in is rejected", () => {
      const state = {
        ...optInMockState,
        Prefs: {
          ...optInMockState.Prefs,
          values: {
            ...optInMockState.Prefs.values,
            "weather.optInDisplayed": false,
            "weather.optInAccepted": false,
          },
        },
      };
      const { container } = renderWeather("medium", state);
      expect(container.querySelector(".weather-container")).toBeInTheDocument();
      expect(
        container.querySelector(".weather-opt-in-container")
      ).not.toBeInTheDocument();
    });

    it("does not render weather-anchor during opt-in", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(".weather-anchor")
      ).not.toBeInTheDocument();
    });

    it("renders opt-in headline", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(
          ".weather-opt-in-container [data-l10n-id='newtab-weather-opt-in-headline']"
        )
      ).toBeInTheDocument();
    });

    it("renders use-location button", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(
          ".weather-opt-in-container [data-l10n-id='newtab-weather-opt-in-use-location']"
        )
      ).toBeInTheDocument();
    });

    it("renders choose-location button", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(
          ".weather-opt-in-container [data-l10n-id='newtab-weather-opt-in-choose-location']"
        )
      ).toBeInTheDocument();
    });

    it("context menu shows only size, hide, and learn-more during opt-in", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(
          "span[data-l10n-id='newtab-widget-menu-change-size']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-widget-menu-hide']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-learn-more']"
        )
      ).toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-location']"
        )
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-detect-my-location']"
        )
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(
          "panel-item[data-l10n-id='newtab-weather-menu-change-temperature-units-celsius']"
        )
      ).not.toBeInTheDocument();
    });

    it("dispatches opt-in accepted actions when use-location is clicked", () => {
      const { container, dispatch } = renderWeather("medium", optInMockState);
      const button = container.querySelector(
        ".weather-opt-in-container [data-l10n-id='newtab-weather-opt-in-use-location']"
      );
      fireEvent.click(button);

      expect(dispatch).toHaveBeenCalledTimes(3);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.WEATHER_USER_OPT_IN_LOCATION,
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WEATHER_OPT_IN_PROMPT_SELECTION,
        data: "use_location",
      });
      expect(dispatch.mock.calls[2][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "weather",
          widget_source: "widget",
          user_action: "opt_in_accepted",
          widget_size: "medium",
          action_value: "use_location",
        }),
      });
    });

    it("adds weather-opt-in class to article when opt-in is showing", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(".weather-widget.weather-opt-in")
      ).toBeInTheDocument();
    });

    it("does not add weather-opt-in class when opt-in is not showing", () => {
      const { container } = renderWeather("medium");
      expect(
        container.querySelector(".weather-widget.weather-opt-in")
      ).not.toBeInTheDocument();
    });

    it("does not render city name during opt-in", () => {
      const { container } = renderWeather("medium", optInMockState);
      expect(
        container.querySelector(".widget-title h3")
      ).not.toBeInTheDocument();
    });

    it("hides opt-in container and weather container when search is active during opt-in", () => {
      const state = {
        ...optInMockState,
        Weather: { ...optInMockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-opt-in-container")
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(".weather-container")
      ).not.toBeInTheDocument();
    });

    it("shows location search input when search is active during opt-in", () => {
      const state = {
        ...optInMockState,
        Weather: { ...optInMockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".location-search input")
      ).toBeInTheDocument();
    });

    it("sets size=small on opt-in buttons in small widget", () => {
      const { container } = renderWeather("small", optInMockState);
      const buttons = container.querySelectorAll(
        ".weather-opt-in-container moz-button"
      );
      buttons.forEach(btn => expect(btn.getAttribute("size")).toBe("small"));
    });

    it("does not set size on opt-in buttons in medium widget", () => {
      const { container } = renderWeather("medium", optInMockState);
      const buttons = container.querySelectorAll(
        ".weather-opt-in-container moz-button"
      );
      buttons.forEach(btn => expect(btn.getAttribute("size")).toBeNull());
    });

    it("dispatches choose-location actions when choose-location is clicked", () => {
      const { container, dispatch } = renderWeather("medium", optInMockState);
      const button = container.querySelector(
        ".weather-opt-in-container [data-l10n-id='newtab-weather-opt-in-choose-location']"
      );
      fireEvent.click(button);

      expect(dispatch).toHaveBeenCalledTimes(3);
      expect(dispatch.mock.calls[0][0]).toMatchObject({
        type: at.WEATHER_OPT_IN_PROMPT_SELECTION,
        data: "choose_location",
      });
      expect(dispatch.mock.calls[1][0]).toMatchObject({
        type: at.WEATHER_SEARCH_ACTIVE,
        data: true,
      });
      expect(dispatch.mock.calls[2][0]).toMatchObject({
        type: at.WIDGETS_USER_EVENT,
        data: expect.objectContaining({
          widget_name: "weather",
          widget_source: "widget",
          user_action: "opt_in_accepted",
          widget_size: "medium",
          action_value: "choose_location",
        }),
      });
    });
  });

  describe("size=small (sidebar view)", () => {
    it("renders widget with conditions view and forecast footer without full-forecast link", () => {
      const { container } = renderWeather("small");
      expect(container.querySelector(".weather-widget")).toBeInTheDocument();
      expect(
        container.querySelector(".weather-conditions-view")
      ).toBeInTheDocument();
      expect(container.querySelector(".forecast-footer")).toBeInTheDocument();
      expect(container.querySelector(".full-forecast")).not.toBeInTheDocument();
    });
  });

  describe("search UI", () => {
    it("adds weather-search-active class when searchActive is true", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-widget.weather-search-active")
      ).toBeInTheDocument();
    });

    it("does not add weather-search-active class when searchActive is false", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(".weather-widget.weather-search-active")
      ).not.toBeInTheDocument();
    });

    it("renders LocationSearch when searchActive is true", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(container.querySelector(".location-search")).toBeInTheDocument();
    });

    it("does not render LocationSearch when searchActive is false", () => {
      const { container } = renderWeather();
      expect(
        container.querySelector(".location-search")
      ).not.toBeInTheDocument();
    });

    it("suppresses weather-opt-in class when searchActive is true during opt-in", () => {
      const state = {
        ...optInMockState,
        Weather: { ...optInMockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-widget.weather-opt-in")
      ).not.toBeInTheDocument();
    });

    it("does not render context menu when searchActive is true", () => {
      const state = {
        ...mockState,
        Weather: { ...mockState.Weather, searchActive: true },
      };
      const { container } = renderWeather("medium", state);
      expect(
        container.querySelector(".weather-context-menu-wrapper")
      ).not.toBeInTheDocument();
    });
  });
});
