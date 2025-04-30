/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "resource:///modules/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeolocationUtils:
    "resource:///modules/urlbar/private/GeolocationUtils.sys.mjs",
  MerinoClient: "resource:///modules/MerinoClient.sys.mjs",
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
  UrlbarView: "resource:///modules/UrlbarView.sys.mjs",
});

const MERINO_PROVIDER = "accuweather";
const MERINO_TIMEOUT_MS = 5000; // 5s

// Cache period for Merino's weather response. This is intentionally a small
// amount of time. See the `cachePeriodMs` discussion in `MerinoClient`. In
// addition, caching also helps prevent the weather suggestion from flickering
// out of and into the view as the user matches the same suggestion with each
// keystroke, especially when Merino has high latency.
const MERINO_WEATHER_CACHE_PERIOD_MS = 60000; // 1 minute

const RESULT_MENU_COMMAND = {
  DISMISS: "dismiss",
  HELP: "help",
  INACCURATE_LOCATION: "inaccurate_location",
  MANAGE: "manage",
  SHOW_LESS_FREQUENTLY: "show_less_frequently",
};

const WEATHER_PROVIDER_DISPLAY_NAME = "AccuWeather®";

const WEATHER_DYNAMIC_TYPE = "weather";
const WEATHER_VIEW_TEMPLATE = {
  attributes: {
    selectable: true,
  },
  children: [
    {
      name: "currentConditions",
      tag: "span",
      children: [
        {
          name: "currently",
          tag: "div",
        },
        {
          name: "currentTemperature",
          tag: "div",
          children: [
            {
              name: "temperature",
              tag: "span",
            },
            {
              name: "weatherIcon",
              tag: "img",
            },
          ],
        },
      ],
    },
    {
      name: "summary",
      tag: "span",
      overflowable: true,
      children: [
        {
          name: "top",
          tag: "div",
          children: [
            {
              name: "topNoWrap",
              tag: "span",
              children: [
                { name: "title", tag: "span", classList: ["urlbarView-title"] },
                {
                  name: "titleSeparator",
                  tag: "span",
                  classList: ["urlbarView-title-separator"],
                },
              ],
            },
            {
              name: "url",
              tag: "span",
              classList: ["urlbarView-url"],
            },
          ],
        },
        {
          name: "middle",
          tag: "div",
          children: [
            {
              name: "middleNoWrap",
              tag: "span",
              overflowable: true,
              children: [
                {
                  name: "summaryText",
                  tag: "span",
                },
                {
                  name: "summaryTextSeparator",
                  tag: "span",
                },
                {
                  name: "highLow",
                  tag: "span",
                },
              ],
            },
            {
              name: "highLowWrap",
              tag: "span",
            },
          ],
        },
        {
          name: "bottom",
          tag: "div",
        },
      ],
    },
  ],
};

/**
 * A feature that periodically fetches weather suggestions from Merino.
 */
export class WeatherSuggestions extends SuggestProvider {
  constructor(...args) {
    super(...args);
    lazy.UrlbarResult.addDynamicResultType(WEATHER_DYNAMIC_TYPE);
    lazy.UrlbarView.addDynamicViewTemplate(
      WEATHER_DYNAMIC_TYPE,
      WEATHER_VIEW_TEMPLATE
    );
  }

  get enablingPreferences() {
    return [
      "weatherFeatureGate",
      "suggest.weather",
      "suggest.quicksuggest.sponsored",
    ];
  }

  get primaryUserControlledPreference() {
    return "suggest.weather";
  }

  get rustSuggestionType() {
    return "Weather";
  }

  get showLessFrequentlyCount() {
    const count = lazy.UrlbarPrefs.get("weather.showLessFrequentlyCount") || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    const cap =
      lazy.UrlbarPrefs.get("weatherShowLessFrequentlyCap") ||
      lazy.QuickSuggest.config.showLessFrequentlyCap ||
      0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  isSuggestionSponsored(_suggestion) {
    return true;
  }

  getSuggestionTelemetryType() {
    return "weather";
  }

  enable(enabled) {
    if (!enabled) {
      this.#merino = null;
    }
  }

  async filterSuggestions(suggestions) {
    // If the query didn't include a city, Rust will return at most one
    // suggestion. If the query matched multiple cities, Rust will return one
    // suggestion per city. All suggestions will have the same score, and
    // they'll be ordered by population size from largest to smallest.
    if (suggestions.length <= 1) {
      return suggestions;
    }
    let suggestion = await lazy.GeolocationUtils.best(suggestions);
    return [suggestion];
  }

  async makeResult(queryContext, suggestion, searchString) {
    if (searchString.length < this.#minKeywordLength) {
      return null;
    }

    if (!this.#merino) {
      this.#merino = new lazy.MerinoClient(this.constructor.name, {
        cachePeriodMs: MERINO_WEATHER_CACHE_PERIOD_MS,
      });
    }

    // Set up location params to pass to Merino. We need to null-check each
    // suggestion property because `MerinoClient` will stringify null values.
    let otherParams = {};
    for (let key of ["city", "region", "country"]) {
      if (suggestion[key]) {
        otherParams[key] = suggestion[key];
      }
    }

    let merino = this.#merino;
    let fetchInstance = (this.#fetchInstance = {});
    let suggestions = await merino.fetch({
      query: "",
      otherParams,
      providers: [MERINO_PROVIDER],
      timeoutMs: this.#timeoutMs,
    });
    if (fetchInstance != this.#fetchInstance || merino != this.#merino) {
      return null;
    }

    if (!suggestions.length) {
      return null;
    }
    suggestion = suggestions[0];

    let unit = Services.locale.regionalPrefsLocales[0] == "en-US" ? "f" : "c";

    let treatment = lazy.UrlbarPrefs.get("weatherUiTreatment");
    if (treatment == 1 || treatment == 2) {
      return this.#makeDynamicResult(suggestion, unit);
    }

    return Object.assign(
      new lazy.UrlbarResult(
        lazy.UrlbarUtils.RESULT_TYPE.URL,
        lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
        {
          url: suggestion.url,
          titleL10n: {
            id: "firefox-suggest-weather-title-simplest",
            args: {
              temperature: suggestion.current_conditions.temperature[unit],
              unit: unit.toUpperCase(),
              city: suggestion.city_name,
              region: suggestion.region_code,
            },
            parseMarkup: true,
            cacheable: true,
            excludeArgsFromCacheKey: true,
          },
          bottomTextL10n: {
            id: "firefox-suggest-weather-sponsored",
            args: { provider: WEATHER_PROVIDER_DISPLAY_NAME },
            cacheable: true,
          },
          helpUrl: lazy.QuickSuggest.HELP_URL,
        }
      ),
      {
        suggestedIndex: 1,
        isRichSuggestion: true,
        richSuggestionIconVariation: String(
          suggestion.current_conditions.icon_id
        ),
      }
    );
  }

  #makeDynamicResult(suggestion, unit) {
    return Object.assign(
      new lazy.UrlbarResult(
        lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC,
        lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
        {
          url: suggestion.url,
          input: suggestion.url,
          iconId: suggestion.current_conditions.icon_id,
          dynamicType: WEATHER_DYNAMIC_TYPE,
          city: suggestion.city_name,
          region: suggestion.region_code,
          temperatureUnit: unit,
          temperature: suggestion.current_conditions.temperature[unit],
          currentConditions: suggestion.current_conditions.summary,
          forecast: suggestion.forecast.summary,
          high: suggestion.forecast.high[unit],
          low: suggestion.forecast.low[unit],
          showRowLabel: true,
          helpUrl: lazy.QuickSuggest.HELP_URL,
        }
      ),
      {
        showFeedbackMenu: true,
        suggestedIndex: 1,
      }
    );
  }

  getViewUpdate(result) {
    let useSimplerUi = lazy.UrlbarPrefs.get("weatherUiTreatment") == 1;
    let uppercaseUnit = result.payload.temperatureUnit.toUpperCase();
    return {
      currently: {
        l10n: {
          id: "firefox-suggest-weather-currently",
          cacheable: true,
        },
      },
      temperature: {
        l10n: {
          id: "firefox-suggest-weather-temperature",
          args: {
            value: result.payload.temperature,
            unit: uppercaseUnit,
          },
          cacheable: true,
          excludeArgsFromCacheKey: true,
        },
      },
      weatherIcon: {
        attributes: { "icon-variation": result.payload.iconId },
      },
      title: {
        l10n: {
          id: "firefox-suggest-weather-title",
          args: { city: result.payload.city, region: result.payload.region },
          cacheable: true,
          excludeArgsFromCacheKey: true,
        },
      },
      url: {
        textContent: result.payload.url,
      },
      summaryText: useSimplerUi
        ? { textContent: result.payload.currentConditions }
        : {
            l10n: {
              id: "firefox-suggest-weather-summary-text",
              args: {
                currentConditions: result.payload.currentConditions,
                forecast: result.payload.forecast,
              },
              cacheable: true,
              excludeArgsFromCacheKey: true,
            },
          },
      highLow: {
        l10n: {
          id: "firefox-suggest-weather-high-low",
          args: {
            high: result.payload.high,
            low: result.payload.low,
            unit: uppercaseUnit,
          },
          cacheable: true,
          excludeArgsFromCacheKey: true,
        },
      },
      highLowWrap: {
        l10n: {
          id: "firefox-suggest-weather-high-low",
          args: {
            high: result.payload.high,
            low: result.payload.low,
            unit: uppercaseUnit,
          },
        },
      },
      bottom: {
        l10n: {
          id: "firefox-suggest-weather-sponsored",
          args: { provider: WEATHER_PROVIDER_DISPLAY_NAME },
          cacheable: true,
        },
      },
    };
  }

  getResultCommands() {
    let commands = [
      {
        name: RESULT_MENU_COMMAND.INACCURATE_LOCATION,
        l10n: {
          id: "urlbar-result-menu-report-inaccurate-location",
        },
      },
    ];

    if (this.canShowLessFrequently) {
      commands.push({
        name: RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY,
        l10n: {
          id: "urlbar-result-menu-show-less-frequently",
        },
      });
    }

    commands.push(
      {
        name: RESULT_MENU_COMMAND.DISMISS,
        l10n: {
          id: "urlbar-result-menu-dont-show-weather-suggestions",
        },
      },
      { name: "separator" },
      {
        name: RESULT_MENU_COMMAND.MANAGE,
        l10n: {
          id: "urlbar-result-menu-manage-firefox-suggest",
        },
      },
      {
        name: RESULT_MENU_COMMAND.HELP,
        l10n: {
          id: "urlbar-result-menu-learn-more-about-firefox-suggest",
        },
      }
    );

    return commands;
  }

  onEngagement(queryContext, controller, details, searchString) {
    let { result } = details;
    switch (details.selType) {
      case RESULT_MENU_COMMAND.HELP:
      case RESULT_MENU_COMMAND.MANAGE:
        // "help" and "manage" are handled by UrlbarInput, no need to do
        // anything here.
        break;
      // Note that selType == "dismiss" when the user presses the dismiss key
      // shortcut, in addition to the result menu command.
      case RESULT_MENU_COMMAND.DISMISS:
        this.logger.info("Dismissing weather result");
        lazy.UrlbarPrefs.set("suggest.weather", false);
        result.acknowledgeDismissalL10n = {
          id: "urlbar-dismissal-acknowledgment-weather",
        };
        controller.removeResult(result);
        break;
      case RESULT_MENU_COMMAND.INACCURATE_LOCATION:
        // Currently the only way we record this feedback is in the Glean
        // engagement event. As with all commands, it will be recorded with an
        // `engagement_type` value that is the command's name, in this case
        // `inaccurate_location`.
        controller.view.acknowledgeFeedback(result);
        break;
      case RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY:
        controller.view.acknowledgeFeedback(result);
        this.incrementShowLessFrequentlyCount();
        if (!this.canShowLessFrequently) {
          controller.view.invalidateResultMenuCommands();
        }
        lazy.UrlbarPrefs.set(
          "weather.minKeywordLength",
          searchString.length + 1
        );
        break;
    }
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        "weather.showLessFrequentlyCount",
        this.showLessFrequentlyCount + 1
      );
    }
  }

  get #config() {
    let { rustBackend } = lazy.QuickSuggest;
    let config = rustBackend.isEnabled
      ? rustBackend.getConfigForSuggestionType(this.rustSuggestionType)
      : null;
    return config || {};
  }

  get #minKeywordLength() {
    // Use the pref value if it has a user value, which means the user clicked
    // "Show less frequently" at least once. Otherwise, fall back to the Nimbus
    // value and then the config value. That lets us override the pref's default
    // value using Nimbus or the config, if necessary.
    let minLength = lazy.UrlbarPrefs.get("weather.minKeywordLength");
    if (
      !Services.prefs.prefHasUserValue(
        "browser.urlbar.weather.minKeywordLength"
      )
    ) {
      let nimbusValue = lazy.UrlbarPrefs.get("weatherKeywordsMinimumLength");
      if (nimbusValue !== null) {
        minLength = nimbusValue;
      } else if (!isNaN(this.#config.minKeywordLength)) {
        minLength = this.#config.minKeywordLength;
      }
    }
    return Math.max(minLength, 0);
  }

  get _test_merino() {
    return this.#merino;
  }

  _test_setTimeoutMs(ms) {
    this.#timeoutMs = ms < 0 ? MERINO_TIMEOUT_MS : ms;
  }

  #fetchInstance = null;
  #merino = null;
  #timeoutMs = MERINO_TIMEOUT_MS;
}
