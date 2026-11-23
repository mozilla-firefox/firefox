/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

const RESULT_MENU_COMMAND = Object.freeze({
  DISMISS: "dismiss",
  NOT_INTERESTED: "not_interested",
});

/**
 * Maps each supported temporal type to the pref that controls whether
 * suggestions of that type are shown.
 */
const PREFS_BY_TYPE = new Map([["seasonal", "suggest.temporal.seasonal"]]);

/**
 * A Suggest feature that manages temporal search suggestions, editorially
 * curated search queries tied to a period of time, shown in the zero prefix
 * view.
 *
 * Each query is a separate dynamic Rust suggestion whose keyword is the empty
 * string and whose dismissal key is the query, so dismissed queries are
 * filtered out by the Rust component. Queries belong to a group, and only one
 * group is shown at a time.
 *
 * Example Remote Settings record:
 *
 *   {
 *     "type": "dynamic-suggestions",
 *     "suggestion_type": "temporal",
 *     "attachment": [
 *       {
 *         "keywords": [""],
 *         "dismissal_key": "turkey recipes",
 *         "data": {
 *           "group": "thanksgiving-2026",
 *           "title": "Thanksgiving",
 *           "type": "seasonal",
 *           "startDate": "2026-11-12",
 *           "endDate": "2026-11-27",
 *           "engines": ["google"],
 *           "query": "turkey recipes"
 *         }
 *       }
 *     ]
 *   }
 */
export class TemporalSuggestions extends SuggestProvider {
  /**
   * Whether temporal search suggestions are enabled: the feature gate,
   * `suggest.temporal` and search suggestions are on and at least one temporal
   * type is enabled.
   *
   * @type {boolean}
   */
  static get isEnabled() {
    return (
      lazy.UrlbarPrefs.get("temporal.featureGate") &&
      lazy.UrlbarPrefs.get("suggest.temporal") &&
      lazy.UrlbarPrefs.get("suggest.searches") &&
      lazy.UrlbarPrefs.get("browser.search.suggest.enabled") &&
      [...PREFS_BY_TYPE.values()].some(p => lazy.UrlbarPrefs.get(p))
    );
  }

  /**
   * The queries picked since the last time every query was picked, shared by
   * all windows.
   *
   * @type {Set<string>}
   */
  #shownQueries = new Set();

  get enablingPreferences() {
    return [
      "temporal.featureGate",
      "suggest.temporal",
      "suggest.quicksuggest.all",
      "suggest.searches",
      "browser.search.suggest.enabled",
    ];
  }

  get primaryUserControlledPreferences() {
    return ["suggest.temporal"];
  }

  get rustSuggestionType() {
    return "Dynamic";
  }

  get dynamicRustSuggestionTypes() {
    return ["temporal"];
  }

  getSuggestionTelemetryType() {
    return "temporal";
  }

  /**
   * @param {object[]} suggestions
   *   The feature's suggestions, one per query.
   * @returns {Promise<object[]>}
   *   The suggestions of the picked queries of the eligible group, in pick
   *   order.
   */
  async filterSuggestions(suggestions) {
    let engine = lazy.UrlbarSearchUtils.getDefaultEngine(false);
    if (!engine) {
      return [];
    }

    let today = lazy.UrlbarShared.zonedDateTimeISO().toPlainDate();
    let eligible = suggestions.filter(s =>
      this.isEligible(s.data, { today, engineId: engine.id })
    );
    if (!eligible.length) {
      return [];
    }

    // TODO (Bug 2083151): Support showing more than one eligible group at a
    // time.
    let groupId = eligible[0].data.group;
    let suggestionsByQuery = new Map();
    for (let suggestion of eligible) {
      if (suggestion.data.group == groupId) {
        suggestionsByQuery.set(suggestion.data.query, suggestion);
      }
    }

    let queries = this.selectQueries(
      [...suggestionsByQuery.keys()],
      lazy.UrlbarPrefs.get("temporal.rowCount")
    );
    return queries.map(query => ({
      ...suggestionsByQuery.get(query),
      engine: engine.name,
    }));
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {object} suggestion
   *   A suggestion returned by `filterSuggestions()`.
   * @returns {Promise<?UrlbarResult>}
   */
  async makeResult(queryContext, suggestion) {
    // The Rust component is queried with the search string trimmed at the
    // start, so a whitespace-only search string matches the empty keyword.
    if (
      queryContext.searchString ||
      queryContext.sapName != "urlbar" ||
      queryContext.searchMode
    ) {
      return null;
    }

    let { query } = suggestion.data;
    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.SEARCH,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      group: lazy.UrlbarShared.RESULT_GROUP.TEMPORAL_SUGGESTION,
      rowLabel: { id: "urlbar-group-temporal-seasonal" },
      payload: {
        engine: suggestion.engine,
        suggestion: query,
        lowerCaseSuggestion: query.toLocaleLowerCase(),
        trending: false,
        title: query,
        icon: lazy.UrlbarShared.ICON.SEARCH_GLASS,
      },
    });
  }

  /**
   * @param {UrlbarResult} _result
   * @returns {UrlbarResultCommand[]}
   */
  getResultCommands(_result) {
    return [
      {
        name: RESULT_MENU_COMMAND.DISMISS,
        l10n: { id: "urlbar-result-menu-dismiss-suggestion2" },
      },
      {
        name: RESULT_MENU_COMMAND.NOT_INTERESTED,
        l10n: { id: "urlbar-result-menu-dont-show-temporal-seasonal" },
      },
    ];
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   */
  onEngagement(queryContext, controller, details) {
    if (details.selType == RESULT_MENU_COMMAND.DISMISS) {
      lazy.QuickSuggest.dismissResult(details.result);
      controller.removeResult(details.result);
    } else if (details.selType == RESULT_MENU_COMMAND.NOT_INTERESTED) {
      let type = getTemporalType(details.result);
      lazy.UrlbarPrefs.set(PREFS_BY_TYPE.get(type), false);
      // Remove the engaged result last so its acknowledgment isn't removed.
      for (let result of [...queryContext.results]) {
        if (
          lazy.QuickSuggest.getFeatureByResult(result) == this &&
          getTemporalType(result) == type &&
          result.id != details.result.id
        ) {
          controller.removeResult(result);
        }
      }
      controller.removeResult(details.result, {
        acknowledgeDismissal: true,
      });
    }
  }

  /**
   * Returns whether a suggestion's data is eligible on a given day for a given
   * engine.
   *
   * The data is eligible when it's valid, the pref for its type is enabled,
   * `today` falls within its inclusive date range and `engineId` is one of its
   * engines.
   *
   * @param {object} data
   *   The `data` of a suggestion.
   * @param {object} options
   * @param {Temporal.PlainDate} options.today
   * @param {string} options.engineId
   *   The `id` of the default engine.
   * @returns {boolean}
   */
  isEligible(data, { today, engineId }) {
    if (
      !this.#isValid(data) ||
      !lazy.UrlbarPrefs.get(PREFS_BY_TYPE.get(data.type))
    ) {
      return false;
    }
    return (
      data.engines.includes(engineId) &&
      Temporal.PlainDate.compare(parseDate(data.startDate), today) <= 0 &&
      Temporal.PlainDate.compare(today, parseDate(data.endDate)) <= 0
    );
  }

  /**
   * Picks up to `count` queries at random, skipping queries that were already
   * picked. Once every query has been picked, the picked queries are
   * forgotten, except those picked by this call, and picking continues.
   *
   * @param {string[]} queries
   * @param {number} count
   * @returns {string[]}
   *   The picked queries in pick order.
   */
  selectQueries(queries, count) {
    let unshown = () => queries.filter(query => !this.#shownQueries.has(query));
    let pool = unshown();
    let picked = [];
    while (picked.length < count) {
      if (!pool.length) {
        this.#shownQueries = new Set(picked);
        pool = unshown();
        if (!pool.length) {
          break;
        }
      }
      let query = pool.splice(this.#randomIndex(pool.length), 1)[0];
      picked.push(query);
      this.#shownQueries.add(query);
    }
    return picked;
  }

  /**
   * Returns a random number in [0, 1). Tests can stub this.
   *
   * @returns {number}
   */
  _random() {
    return Math.random();
  }

  /**
   * Clears the picked queries. Intended for tests.
   */
  _reset() {
    this.#shownQueries = new Set();
  }

  /**
   * Returns whether a suggestion's data is valid. It's valid when it has a
   * supported type, a non-empty group ID, title and query, a start date on or
   * before its end date, and at least one engine.
   *
   * @param {object} data
   * @returns {boolean}
   */
  #isValid(data) {
    if (!PREFS_BY_TYPE.has(data?.type)) {
      this.logger.debug(`Ignoring suggestion with unknown type: ${data?.type}`);
      return false;
    }

    let startDate = parseDate(data.startDate);
    let endDate = parseDate(data.endDate);
    let isValid =
      isNonEmptyString(data.group) &&
      isNonEmptyString(data.title) &&
      isNonEmptyString(data.query) &&
      !!startDate &&
      !!endDate &&
      Temporal.PlainDate.compare(startDate, endDate) <= 0 &&
      Array.isArray(data.engines) &&
      !!data.engines.length &&
      data.engines.every(isNonEmptyString);

    if (!isValid) {
      this.logger.warn(`Ignoring invalid suggestion: ${data.query}`);
    }
    return isValid;
  }

  #randomIndex(length) {
    return Math.floor(this._random() * length);
  }
}

/**
 * @param {UrlbarResult} result
 *   A temporal result.
 * @returns {string}
 *   The temporal type of the result's suggestion, e.g. "seasonal".
 */
function getTemporalType(result) {
  return result.payload.suggestionObject.data.type;
}

function isNonEmptyString(value) {
  return typeof value == "string" && !!value.trim();
}

/**
 * Parses a date, e.g. `2026-11-12`, using `Temporal.PlainDate.from()`.
 *
 * @param {any} value
 * @returns {?Temporal.PlainDate}
 *   The date, or null if `value` isn't a valid date.
 */
function parseDate(value) {
  try {
    return Temporal.PlainDate.from(value, { overflow: "reject" });
  } catch {
    return null;
  }
}
