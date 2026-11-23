/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

const SHOW_COUNTDOWN_THRESHOLD_DAYS = 30;
const MS_PER_DAY = 1000 * 60 * 60 * 24;

/**
 * A Suggest feature that manages important-dates suggestions.
 */
export class ImportantDatesSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return ["importantDatesFeatureGate", "suggest.importantDates"];
  }

  get primaryUserControlledPreferences() {
    return ["suggest.importantDates"];
  }

  get rustSuggestionType() {
    return "Dynamic";
  }

  get dynamicRustSuggestionTypes() {
    return ["important_dates"];
  }

  isSuggestionSponsored(suggestion) {
    if (suggestion.data?.result?.payload?.hasOwnProperty("isSponsored")) {
      return suggestion.data.result.payload.isSponsored;
    }
    return false;
  }

  getSuggestionTelemetryType(suggestion) {
    if (suggestion.data?.result?.payload?.hasOwnProperty("telemetryType")) {
      return suggestion.data.result.payload.telemetryType;
    }
    return this.dynamicRustSuggestionTypes[0];
  }

  async makeResult(queryContext, suggestion, _searchString) {
    if (
      !suggestion.data?.result?.payload ||
      typeof suggestion.data.result.payload != "object"
    ) {
      this.logger.warn("Unexpected remote settings suggestion");
      return null;
    }

    return this.#makeDateResult(queryContext, suggestion.data.result.payload);
  }

  /**
   * @typedef DatePayload
   *   The shape of an important dates suggestion payload.
   * @property {(string | [string, string])[]} dates
   *   An array of dates or date tuples in the format "YYYY-MM-DD".
   *   For a date tuple, the first value represents the start and the second
   *   the end date. This is assumed to be ordered oldest to newest.
   * @property {string} name
   *   The name of the event.
   */

  /**
   * Returns a string about the date of an event that can be displayed.
   *
   * @param {string | [string, string]} dateStr
   *   A string in the format "YYYY-MM-DD" representing a single
   *   day or a tuple of strings representing start and end days.
   * @returns {string}
   *   A localized string about the date of the event.
   */
  #formatDateOrRange(dateStr) {
    if (Array.isArray(dateStr)) {
      let format = new Intl.DateTimeFormat(Services.locale.appLocaleAsBCP47, {
        year: "numeric",
        month: "long",
        day: "numeric",
      });
      let startDate = new Date(dateStr[0] + "T00:00");
      let endDate = new Date(dateStr[1] + "T00:00");
      return format.formatRange(startDate, endDate);
    }

    let format = new Intl.DateTimeFormat(Services.locale.appLocaleAsBCP47, {
      weekday: "long",
      year: "numeric",
      month: "long",
      day: "numeric",
    });
    let date = new Date(dateStr + "T00:00");
    return format.format(date);
  }

  /**
   * Returns a l10n object about the name of and time until an event
   * that can be used as the description of a urlbar result.
   *
   * @param {string | [string, string]} dateStr
   *   A string in the format "YYYY-MM-DD" representing a single
   *   day or a tuple of strings representing start and end days.
   * @param {string} name
   *   The name of the event.
   * @returns {?object}
   *   A l10n object or null if the event is in the past.
   */
  #formatDateCountdown(dateStr, name) {
    if (Array.isArray(dateStr)) {
      let daysUntilStart = this.#getDaysUntil(dateStr[0]);
      let daysUntilEnd = this.#getDaysUntil(dateStr[1]);
      if (daysUntilEnd < 0) {
        throw new Error("Date lies in the past.");
      }
      if (daysUntilStart > 0) {
        return {
          id: "urlbar-result-dates-countdown-range",
          args: { daysUntilStart, name },
        };
      }
      if (daysUntilEnd > 0) {
        return {
          id: "urlbar-result-dates-ongoing",
          args: { daysUntilEnd, name },
        };
      }
      return {
        id: "urlbar-result-dates-ends-today",
        args: { name },
      };
    }

    let daysUntil = this.#getDaysUntil(dateStr);
    if (daysUntil < 0) {
      throw new Error("Date lies in the past.");
    }
    if (daysUntil > 0) {
      return {
        id: "urlbar-result-dates-countdown",
        args: { daysUntilStart: daysUntil, name },
      };
    }
    return {
      id: "urlbar-result-dates-today",
      args: { name },
    };
  }

  /**
   * Returns the number of days until the specified date.
   *
   * @param {string} dateStr
   *   A string in the format "YYYY-MM-DD".
   * @returns {number}
   *   The time until the input date in days.
   */
  #getDaysUntil(dateStr) {
    let now = new Date();
    now.setHours(0, 0, 0, 0);
    let date = new Date(dateStr + "T00:00");

    let msUntil = date.getTime() - now.getTime();
    // Round to account for potential DST.
    return Math.round(msUntil / MS_PER_DAY);
  }

  /**
   * Creates a urlbar result from an important dates payload.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query context.
   * @param {DatePayload} payload
   *   The important dates payload.
   * @returns {?UrlbarResult}
   *   A urlbar result or null if all instances are in the past.
   */
  #makeDateResult(queryContext, payload) {
    let eventDateOrRange = payload.dates.find(
      // Find first entry where the end date is in the future.
      // This is always the upcoming occurrence since dates is ordered by time.
      d => this.#getDaysUntil(Array.isArray(d) ? d[1] : d) >= 0
    );
    if (!eventDateOrRange) {
      // All instances of the event are in the past.
      return null;
    }

    let daysUntilStart = this.#getDaysUntil(
      Array.isArray(eventDateOrRange) ? eventDateOrRange[0] : eventDateOrRange
    );

    let description, descriptionL10n;
    if (daysUntilStart > SHOW_COUNTDOWN_THRESHOLD_DAYS) {
      description = payload.name;
    } else {
      descriptionL10n = {
        ...this.#formatDateCountdown(eventDateOrRange, payload.name),
      };
    }

    let dateString = this.#formatDateOrRange(eventDateOrRange);
    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.SEARCH,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      isBestMatch: true,
      richSuggestionIconSize: 24,
      payload: {
        title: dateString,
        description,
        engine: lazy.UrlbarSearchUtils.getDefaultEngine(queryContext.isPrivate)
          .name,
        descriptionL10n,
        query: payload.name,
        lowerCaseSuggestion: payload.name.toLowerCase(),
        icon: "chrome://browser/skin/calendar-24.svg",
        helpUrl: lazy.QuickSuggest.HELP_URL,
        isManageable: true,
        isBlockable: true,
      },
      highlights: {
        title: lazy.UrlbarShared.HIGHLIGHT.ALL,
      },
    });
  }

  /**
   * @param {UrlbarQueryContext} _queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   * @param {string} _searchString
   */
  onEngagement(_queryContext, controller, details, _searchString) {
    switch (details.selType) {
      case "manage":
        // "manage" is handled by UrlbarInput, no need to do anything here.
        break;
      case "dismiss": {
        let { result } = details;
        lazy.QuickSuggest.dismissResult(result);
        controller.removeResult(result);
        break;
      }
    }
  }
}
