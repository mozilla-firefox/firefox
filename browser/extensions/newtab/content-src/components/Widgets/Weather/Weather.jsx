/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useCallback, useEffect, useRef } from "react";
import { useSelector, batch } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useIntersectionObserver } from "../../../lib/utils";
import { LocationSearch } from "content-src/components/Weather/LocationSearch";

const USER_ACTION_TYPES = {
  CHANGE_LOCATION: "change_location",
  DETECT_LOCATION: "detect_location",
  CHANGE_TEMP_UNIT: "change_temperature_units",
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
  OPT_IN_ACCEPTED: "opt_in_accepted",
  PROVIDER_LINK_CLICK: "provider_link_click",
};

const PREF_WEATHER_SIZE = "widgets.weather.size";

function Weather({ dispatch, size }) {
  const prefs = useSelector(state => state.Prefs.values);
  const weatherData = useSelector(state => state.Weather);
  const impressionFired = useRef(false);
  const errorTelemetrySent = useRef(false);
  const errorRef = useRef(null);
  const sizeSubmenuRef = useRef(null);
  const currentWeatherSize = prefs[PREF_WEATHER_SIZE] || "medium";
  const trainhopWidgetsEnabled = prefs.trainhopConfig?.widgets?.enabled;
  const widgetsSystemEnabled =
    trainhopWidgetsEnabled || prefs["widgets.system.enabled"];
  const widgetsEnabled = trainhopWidgetsEnabled || prefs["widgets.enabled"];
  const widgetsMayBeMaximized =
    prefs.trainhopConfig?.widgets?.maximized ||
    prefs["widgets.system.maximized"];

  const handleChangeSize = useCallback(
    newSize => {
      batch(() => {
        dispatch(
          ac.OnlyToMain({
            type: at.SET_PREF,
            data: {
              name: PREF_WEATHER_SIZE,
              value: newSize,
            },
          })
        );
        dispatch(
          ac.OnlyToMain({
            type: at.WIDGETS_USER_EVENT,
            data: {
              widget_name: "weather",
              widget_source: "context_menu",
              user_action: USER_ACTION_TYPES.CHANGE_SIZE,
              action_value: newSize,
              widget_size: newSize,
            },
          })
        );
      });
    },
    [dispatch]
  );

  useEffect(() => {
    const el = sizeSubmenuRef.current;
    if (!el) {
      return undefined;
    }
    const listener = e => {
      const item = e.composedPath().find(node => node.dataset?.size);
      if (item) {
        handleChangeSize(item.dataset.size);
      }
    };
    el.addEventListener("click", listener);
    return () => el.removeEventListener("click", listener);
  }, [handleChangeSize]);

  const handleIntersection = useCallback(() => {
    if (impressionFired.current) {
      return;
    }
    impressionFired.current = true;
    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_IMPRESSION,
        data: {
          widget_name: "weather",
          widget_size: size,
        },
      })
    );
  }, [dispatch, size]);

  const weatherRef = useIntersectionObserver(handleIntersection);

  const weatherExperimentEnabled = prefs.trainhopConfig?.weather?.enabled;
  const isWeatherEnabled =
    prefs["widgets.weather.enabled"] &&
    (prefs["widgets.system.weather.enabled"] || weatherExperimentEnabled);

  const WEATHER_SUGGESTION = weatherData?.suggestions?.[0];
  const HOURLY_FORECASTS = weatherData?.hourlyForecasts ?? [];

  const showForecast = size === "medium" || size === "large";
  const hasError =
    !WEATHER_SUGGESTION?.current_conditions ||
    !WEATHER_SUGGESTION?.forecast ||
    (showForecast && !HOURLY_FORECASTS[0]);

  const handleErrorIntersection = useCallback(
    entries => {
      const entry = entries.find(e => e.isIntersecting);
      if (entry && !errorTelemetrySent.current) {
        dispatch(
          ac.AlsoToMain({
            type: at.WIDGETS_ERROR,
            data: {
              widget_name: "weather",
              widget_size: size,
              error_type: "load_error",
            },
          })
        );
        errorTelemetrySent.current = true;
      }
    },
    [dispatch, size]
  );

  useEffect(() => {
    if (errorRef.current && !errorTelemetrySent.current) {
      const observer = new IntersectionObserver(handleErrorIntersection);
      observer.observe(errorRef.current);
      return () => {
        observer.disconnect();
      };
    }
    return undefined;
  }, [handleErrorIntersection, hasError]);

  // Must be declared before the early return to satisfy React's Rules of Hooks.
  const handleOptInLocationSelected = useCallback(() => {
    dispatch(ac.SetPref("weather.optInAccepted", true));
  }, [dispatch]);

  if (!weatherData?.initialized || !isWeatherEnabled) {
    return null;
  }

  const weatherOptIn = prefs["system.showWeatherOptIn"];
  const nimbusWeatherOptInEnabled =
    prefs.trainhopConfig?.weather?.weatherOptInEnabled;
  const isOptInEnabled = weatherOptIn || nimbusWeatherOptInEnabled;
  const optInDisplayed = prefs["weather.optInDisplayed"];
  const optInUserChoice = prefs["weather.optInAccepted"];
  const showOptInState = isOptInEnabled && optInDisplayed && !optInUserChoice;

  const { searchActive } = weatherData;

  function handleChangeLocation() {
    batch(() => {
      dispatch(
        ac.BroadcastToContent({
          type: at.WEATHER_SEARCH_ACTIVE,
          data: true,
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "weather",
            widget_source: "context_menu",
            user_action: USER_ACTION_TYPES.CHANGE_LOCATION,
            widget_size: size,
          },
        })
      );
    });
  }

  function handleDetectLocation() {
    batch(() => {
      dispatch(
        ac.AlsoToMain({
          type: at.WEATHER_USER_OPT_IN_LOCATION,
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "weather",
            widget_source: "context_menu",
            user_action: USER_ACTION_TYPES.DETECT_LOCATION,
            widget_size: size,
          },
        })
      );
    });
  }

  function handleChangeTempUnit(unit) {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: {
            name: "weather.temperatureUnits",
            value: unit,
          },
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "weather",
            widget_source: "context_menu",
            user_action: USER_ACTION_TYPES.CHANGE_TEMP_UNIT,
            widget_size: size,
            action_value: unit,
          },
        })
      );
    });
  }

  function handleHideWeather() {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: {
            name: "widgets.weather.enabled",
            value: false,
          },
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_ENABLED,
          data: {
            widget_name: "weather",
            widget_source: "context_menu",
            enabled: false,
            widget_size: size,
          },
        })
      );
    });
  }

  function handleLearnMore() {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.OPEN_LINK,
          data: {
            url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
          },
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "weather",
            widget_source: "context_menu",
            user_action: USER_ACTION_TYPES.LEARN_MORE,
            widget_size: size,
          },
        })
      );
    });
  }

  function handleProviderLinkClick() {
    dispatch(
      ac.OnlyToMain({
        type: at.WIDGETS_USER_EVENT,
        data: {
          widget_name: "weather",
          widget_source: "widget",
          user_action: USER_ACTION_TYPES.PROVIDER_LINK_CLICK,
          widget_size: size,
        },
      })
    );
  }

  function handleOptInChooseLocation() {
    batch(() => {
      dispatch(
        ac.AlsoToMain({
          type: at.WEATHER_OPT_IN_PROMPT_SELECTION,
          data: "choose_location",
        })
      );
      dispatch(
        ac.BroadcastToContent({
          type: at.WEATHER_SEARCH_ACTIVE,
          data: true,
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "weather",
            widget_source: "widget",
            user_action: USER_ACTION_TYPES.OPT_IN_ACCEPTED,
            widget_size: size,
            action_value: "choose_location",
          },
        })
      );
    });
  }

  function handleAcceptOptIn() {
    batch(() => {
      dispatch(
        ac.AlsoToMain({
          type: at.WEATHER_USER_OPT_IN_LOCATION,
        })
      );
      dispatch(
        ac.AlsoToMain({
          type: at.WEATHER_OPT_IN_PROMPT_SELECTION,
          data: "use_location",
        })
      );
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_USER_EVENT,
          data: {
            widget_name: "weather",
            widget_source: "widget",
            user_action: USER_ACTION_TYPES.OPT_IN_ACCEPTED,
            widget_size: size,
            action_value: "use_location",
          },
        })
      );
    });
  }

  function renderContextMenu() {
    return (
      <div className="weather-context-menu-wrapper">
        <moz-button
          className="weather-context-menu-button"
          data-l10n-id="newtab-menu-section-tooltip"
          iconSrc="chrome://global/skin/icons/more.svg"
          menuId="weather-widget-context-menu"
          type="ghost"
          size="small"
        />
        <panel-list id="weather-widget-context-menu">
          {!showOptInState &&
            !isOptInEnabled &&
            (prefs["weather.temperatureUnits"] === "f" ? (
              <panel-item
                data-l10n-id="newtab-weather-menu-change-temperature-units-celsius"
                onClick={() => handleChangeTempUnit("c")}
              />
            ) : (
              <panel-item
                data-l10n-id="newtab-weather-menu-change-temperature-units-fahrenheit"
                onClick={() => handleChangeTempUnit("f")}
              />
            ))}
          {!showOptInState && prefs["weather.locationSearchEnabled"] && (
            <panel-item
              data-l10n-id="newtab-weather-menu-change-location"
              onClick={handleChangeLocation}
            />
          )}
          {!showOptInState && isOptInEnabled && (
            <panel-item
              data-l10n-id="newtab-weather-menu-detect-my-location"
              onClick={handleDetectLocation}
            />
          )}
          {/* Only show size options when both system and user prefs are enabled;
              medium/large sizes require the widgets row, which only renders when both are true.
              trainhopConfig.widgets.enabled overrides either system or user pref so
              an experiment payload can drive the submenu without flipping local prefs. */}
          {widgetsSystemEnabled && widgetsEnabled && widgetsMayBeMaximized && (
            <panel-item submenu="weather-size-submenu">
              <span data-l10n-id="newtab-widget-menu-change-size"></span>
              <panel-list
                ref={sizeSubmenuRef}
                slot="submenu"
                id="weather-size-submenu"
              >
                {["small", "medium", "large"].map(s => (
                  <panel-item
                    key={s}
                    type="checkbox"
                    checked={currentWeatherSize === s || undefined}
                    data-size={s}
                    data-l10n-id={`newtab-widget-size-${s}`}
                  />
                ))}
              </panel-list>
            </panel-item>
          )}
          <panel-item
            data-l10n-id="newtab-widget-menu-hide"
            onClick={handleHideWeather}
          />
          <panel-item
            data-l10n-id="newtab-weather-menu-learn-more"
            onClick={handleLearnMore}
          />
        </panel-list>
      </div>
    );
  }

  function getArticleClassNames() {
    return [
      "weather-widget",
      "col-4",
      `${size}-widget`,
      hasError && "weather-error-state",
      // weather-opt-in is suppressed while search is active so the opt-in
      // layout styles don't conflict with the search UI layout.
      showOptInState && !searchActive && "weather-opt-in",
      // weather-search-active hides weather content and expands small widgets to 4-col.
      searchActive && "weather-search-active",
    ]
      .filter(Boolean)
      .join(" ");
  }

  return (
    <article
      className={getArticleClassNames()}
      ref={el => {
        weatherRef.current = [el];
      }}
    >
      {!hasError && !showOptInState && (
        <a
          className="weather-anchor"
          href={
            showForecast
              ? HOURLY_FORECASTS[0].url || "#"
              : WEATHER_SUGGESTION.forecast.url || "#"
          }
          aria-label={weatherData.locationData.city}
          onClick={handleProviderLinkClick}
        />
      )}
      <div className="widget-title-bar">
        <div className="widget-title">
          {!showOptInState && !searchActive && (
            <h3>{weatherData.locationData.city}</h3>
          )}
        </div>
        {!searchActive && renderContextMenu()}
      </div>
      {hasError && (
        <div className="weather-error" ref={errorRef}>
          <span className="icon icon-info-warning" />{" "}
          <p data-l10n-id="newtab-weather-error-not-available"></p>
        </div>
      )}
      {/* Search  */}
      {searchActive && (
        <div className="weather-search-container">
          <LocationSearch
            outerClassName=""
            onLocationSelected={
              showOptInState ? handleOptInLocationSelected : undefined
            }
          />
        </div>
      )}

      {showOptInState ? (
        !searchActive && (
          <div className="weather-opt-in-container">
            <div className="weather-opt-in-container-title-bar">
              <div className="weather-icon-column">
                <span className="weather-icon iconId3" />
              </div>
              <h3
                className="weather-opt-in-container-title"
                data-l10n-id="newtab-weather-opt-in-headline"
              />
            </div>
            <div className="weather-opt-in-container-buttons">
              <moz-button
                data-l10n-id="newtab-weather-opt-in-use-location"
                onClick={handleAcceptOptIn}
                type="primary"
                size={size === "small" ? "small" : undefined}
              />
              <button
                className="weather-text-link"
                onClick={handleOptInChooseLocation}
                data-l10n-id="newtab-weather-opt-in-choose-location"
              />
            </div>
          </div>
        )
      ) : (
        <>
          <div className="weather-container">
            {!hasError && (
              <div className="weather-conditions-view">
                <a
                  data-l10n-id="newtab-weather-see-forecast-description"
                  data-l10n-args='{"provider": "AccuWeather®"}'
                  data-l10n-attrs="aria-description"
                  href={WEATHER_SUGGESTION.forecast.url}
                  className="weather-info-link"
                  onClick={handleProviderLinkClick}
                >
                  <div className="weather-icon-column">
                    <span
                      className={`weather-icon iconId${WEATHER_SUGGESTION.current_conditions.icon_id}`}
                    />
                  </div>
                  <div className="weather-info-column">
                    <div className="weather-info-row">
                      <div className="temperature-unit">
                        {
                          WEATHER_SUGGESTION.current_conditions.temperature[
                            prefs["weather.temperatureUnits"]
                          ]
                        }
                        &deg;{prefs["weather.temperatureUnits"]}
                      </div>
                      <div className="high-low-row">
                        <span className="high-temperature">
                          <span
                            className="arrow-icon arrow-up"
                            data-l10n-id="newtab-weather-high"
                          />
                          {
                            WEATHER_SUGGESTION.forecast.high[
                              prefs["weather.temperatureUnits"]
                            ]
                          }
                          &deg;
                        </span>
                        <span className="low-temperature">
                          <span
                            className="arrow-icon arrow-down"
                            data-l10n-id="newtab-weather-low"
                          />
                          {
                            WEATHER_SUGGESTION.forecast.low[
                              prefs["weather.temperatureUnits"]
                            ]
                          }
                          &deg;
                        </span>
                      </div>
                    </div>

                    <div className="weather-info-description">
                      {WEATHER_SUGGESTION.current_conditions.summary}
                    </div>
                  </div>
                </a>
              </div>
            )}
            {!hasError && showForecast && (
              <div className="forecast-row">
                <p
                  className="today-forecast"
                  data-l10n-id="newtab-weather-todays-forecast"
                ></p>
                <ul className="forecast-row-items">
                  {HOURLY_FORECASTS.map(slot => (
                    <li key={slot.epoch_date_time}>
                      <span>
                        {slot.temperature[prefs["weather.temperatureUnits"]]}
                        &deg;
                      </span>
                      <span
                        className={`weather-icon iconId${slot.icon_id}`}
                        aria-label={slot.summary}
                        role="img"
                      ></span>
                      <span>
                        {(() => {
                          const date = new Date(slot.date_time);
                          const hours = date.getHours() % 12 || 12;
                          return `${hours}:${String(date.getMinutes()).padStart(2, "0")}`;
                        })()}
                      </span>
                    </li>
                  ))}
                </ul>
              </div>
            )}
          </div>
          {!hasError && (
            <div className="forecast-footer">
              <span
                className="sponsored-text"
                aria-hidden="true"
                data-l10n-id="newtab-weather-sponsored"
                data-l10n-args='{"provider": "AccuWeather®"}'
              ></span>
              {showForecast && (
                <a
                  className="full-forecast"
                  href={HOURLY_FORECASTS[0]?.url || "#"}
                  onClick={handleProviderLinkClick}
                  data-l10n-id="newtab-weather-see-full-forecast"
                ></a>
              )}
            </div>
          )}
        </>
      )}
    </article>
  );
}

export { Weather };
