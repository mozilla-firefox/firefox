/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { useSelector } from "react-redux";
import { Lists } from "./Lists/Lists";
import { FocusTimer } from "./FocusTimer/FocusTimer";
import { Weather as WeatherWidget } from "./Weather/Weather";
import { Clocks } from "./Clocks/Clocks";
import { Privacy } from "./Privacy/Privacy";
import { Crossword } from "./Crossword/Crossword";
import { Stocks } from "./Stocks/Stocks";
import { PictureOfTheDay } from "./PictureOfTheDay/PictureOfTheDay";
import { RecentSearches } from "./RecentSearches/RecentSearches";
import { Horoscopes } from "./Horoscopes/Horoscopes";
import { Travel } from "./Travel/Travel";
import { Solitaire } from "./Solitaire/Solitaire";
import { WIDGET_REGISTRY, resolveWidgetSize } from "common/WidgetsRegistry.mjs";

const weatherEntry = WIDGET_REGISTRY.find(w => w.id === "weather");

function WeatherRowWidget({ dispatch, widgetEnabledMap }) {
  const prefs = useSelector(state => state.Prefs.values);
  const weatherSize = resolveWidgetSize(weatherEntry, prefs);
  return (
    <WeatherWidget
      dispatch={dispatch}
      size={weatherSize}
      widgetEnabledMap={widgetEnabledMap}
    />
  );
}

function WeatherSidebarWidget({ dispatch }) {
  const prefs = useSelector(state => state.Prefs.values);
  if (!prefs.showWeather) {
    return null;
  }
  return <WeatherWidget dispatch={dispatch} size="small" />;
}

export const WIDGET_ROW_COMPONENTS = {
  lists: Lists,
  focusTimer: FocusTimer,
  weather: WeatherRowWidget,
  clocks: Clocks,
  privacy: Privacy,
  crossword: Crossword,
  stocks: Stocks,
  pictureOfTheDay: PictureOfTheDay,
  recentSearches: RecentSearches,
  horoscopes: Horoscopes,
  travel: Travel,
  solitaire: Solitaire,
};

export const WIDGET_SIDEBAR_COMPONENTS = {
  weather: WeatherSidebarWidget,
  privacy: Privacy,
};
