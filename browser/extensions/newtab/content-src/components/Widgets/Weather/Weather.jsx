/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useCallback, useContext, useEffect, useRef } from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  PREF_WEATHER_SIZE,
  WIDGET_REGISTRY,
  WIDGET_USER_ACTIONS,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import { LocationSearch } from "content-src/components/Weather/LocationSearch";
import { SizeSubmenu } from "../SizeSubmenu";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";

const USER_ACTION_TYPES = {
  ...WIDGET_USER_ACTIONS,
  CHANGE_LOCATION: "change_location",
  DETECT_LOCATION: "detect_location",
  CHANGE_TEMP_UNIT: "change_temperature_units",
  OPT_IN_ACCEPTED: "opt_in_accepted",
  PROVIDER_LINK_CLICK: "provider_link_click",
};

const WEATHER_PROVIDER = "AccuWeather®";

const WEATHER_ENTRY = WIDGET_REGISTRY.find(w => w.id === "weather");

function SponsoredText({ size }) {
  if (size === "small") {
    return (
      <span className="sponsored-text" aria-hidden="true">
        {WEATHER_PROVIDER}
      </span>
    );
  }
  return (
    <span
      className="sponsored-text"
      aria-hidden="true"
      data-l10n-id="newtab-weather-sponsored"
      data-l10n-args={JSON.stringify({ provider: WEATHER_PROVIDER })}
    />
  );
}

function Weather({ dispatch, size, widgetEnabledMap }) {
  const prefs = useSelector(state => state.Prefs.values);
  const weatherData = useSelector(state => state.Weather);
  const { impressionRef, recordUserAction, recordError } = useWidgetTelemetry({
    dispatch,
    widget: WEATHER_ENTRY,
    widgetSize: size,
  });
  const errorTelemetrySent = useRef(false);
  const errorRef = useRef(null);
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
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: {
            name: PREF_WEATHER_SIZE,
            value: newSize,
          },
        })
      );
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: WIDGET_SOURCES.CONTEXT_MENU,
        value: newSize,
        size: newSize,
      });
    },
    [dispatch, recordUserAction]
  );

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
        recordError("load_error");
        errorTelemetrySent.current = true;
      }
    },
    [recordError]
  );

  // Bug 2083119: in the spaces-peek layout a peeked space is visible without
  // being active, so the error waits until its space is.
  const spaceActive = useContext(SpaceActiveContext);
  useEffect(() => {
    if (spaceActive && errorRef.current && !errorTelemetrySent.current) {
      const observer = new IntersectionObserver(handleErrorIntersection);
      observer.observe(errorRef.current);
      return () => {
        observer.disconnect();
      };
    }
    return undefined;
  }, [handleErrorIntersection, hasError, spaceActive]);

  // Must be declared before the early return to satisfy React's Rules of Hooks.
  const handleOptInLocationSelected = useCallback(() => {
    dispatch(ac.SetPref("weather.optInAccepted", true));
  }, [dispatch]);

  if (!weatherData?.initialized || !isWeatherEnabled) {
    return null;
  }

  const weatherOptIn = prefs["system.showWeatherOptIn"];
  const nimbusWeatherOptInEnabled =
    prefs.trainhopConfig?.widgetWeather?.weatherOptInEnabled ??
    prefs.trainhopConfig?.weather?.weatherOptInEnabled;
  const isOptInEnabled = weatherOptIn || nimbusWeatherOptInEnabled;
  const optInUserChoice = prefs["weather.optInAccepted"];
  // Show the opt-in prompt whenever opt-in is required and the user has not yet
  // accepted, independent of weather.optInDisplayed. The Nova widget has no
  // reject button, so the only path to optInDisplayed=false is acceptance (which
  // sets optInAccepted=true); gating on optInDisplayed previously let real
  // location weather render for users migrated from a legacy reject (Bug 2046143).
  const showOptInState = isOptInEnabled && !optInUserChoice;

  const { searchActive } = weatherData;

  function handleChangeLocation() {
    dispatch(
      ac.BroadcastToContent({
        type: at.WEATHER_SEARCH_ACTIVE,
        data: true,
      })
    );
    recordUserAction(USER_ACTION_TYPES.CHANGE_LOCATION, {
      source: WIDGET_SOURCES.CONTEXT_MENU,
    });
  }

  function handleDetectLocation() {
    dispatch(
      ac.AlsoToMain({
        type: at.WEATHER_USER_OPT_IN_LOCATION,
      })
    );
    recordUserAction(USER_ACTION_TYPES.DETECT_LOCATION, {
      source: WIDGET_SOURCES.CONTEXT_MENU,
    });
  }

  function handleChangeTempUnit(unit) {
    dispatch(
      ac.OnlyToMain({
        type: at.SET_PREF,
        data: {
          name: "weather.temperatureUnits",
          value: unit,
        },
      })
    );
    recordUserAction(USER_ACTION_TYPES.CHANGE_TEMP_UNIT, {
      source: WIDGET_SOURCES.CONTEXT_MENU,
      value: unit,
    });
  }

  function handleLearnMore() {
    recordUserAction(USER_ACTION_TYPES.LEARN_MORE, {
      source: WIDGET_SOURCES.CONTEXT_MENU,
    });
  }

  function handleProviderLinkClick() {
    recordUserAction(USER_ACTION_TYPES.PROVIDER_LINK_CLICK, {
      source: WIDGET_SOURCES.WIDGET,
    });
  }

  function handleOptInChooseLocation() {
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
    recordUserAction(USER_ACTION_TYPES.OPT_IN_ACCEPTED, {
      source: WIDGET_SOURCES.WIDGET,
      value: "choose_location",
    });
  }

  function handleAcceptOptIn() {
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
    recordUserAction(USER_ACTION_TYPES.OPT_IN_ACCEPTED, {
      source: WIDGET_SOURCES.WIDGET,
      value: "use_location",
    });
  }

  function renderContextMenu() {
    return (
      <div className="weather-context-menu-wrapper">
        <WidgetMenuButton
          className="weather-context-menu-button"
          menuId="weather-widget-context-menu"
          l10nId="newtab-weather-widget-open-menu-button"
        />
        <panel-list
          className="panel-list-no-icons"
          id="weather-widget-context-menu"
        >
          {!showOptInState &&
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
          <WidgetMenuFooter
            dispatch={dispatch}
            widgetId="weather"
            widgetEnabledMap={widgetEnabledMap}
            widgetName="weather"
            enabledPref="widgets.weather.enabled"
            widgetSize={size}
            learnMoreL10nId="newtab-weather-menu-learn-more"
            onLearnMore={handleLearnMore}
            showDivider={!showOptInState}
            sizeSubmenu={
              /* Only show size options when both system and user prefs are enabled;
                 medium/large sizes require the widgets row, which only renders when both are true.
                 trainhopConfig.widgets.enabled overrides either system or user pref so
                 an experiment payload can drive the submenu without flipping local prefs. */
              widgetsSystemEnabled &&
              widgetsEnabled &&
              widgetsMayBeMaximized && (
                <SizeSubmenu
                  submenuId="weather-size-submenu"
                  sizes={["small", "medium", "large"]}
                  checkedSize={currentWeatherSize}
                  onChangeSize={handleChangeSize}
                />
              )
            }
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
      // weather-error-state is suppressed during opt-in so the error UI does
      // not overlap or push the opt-in layout out of its container.
      hasError && !showOptInState && "weather-error-state",
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
    <article className={getArticleClassNames()} ref={impressionRef}>
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
            <h3 className="newtab-widget-title">
              {weatherData.locationData.city}
            </h3>
          )}
        </div>
        {!searchActive && renderContextMenu()}
      </div>
      {hasError && !showOptInState && (
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
                  data-l10n-args={JSON.stringify({
                    provider: WEATHER_PROVIDER,
                  })}
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
                {size === "medium" && <SponsoredText size={size} />}
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
          {!hasError && size !== "medium" && (
            <div className="forecast-footer">
              <SponsoredText size={size} />
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
