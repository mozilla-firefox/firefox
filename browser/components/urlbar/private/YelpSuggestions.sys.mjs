/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeolocationUtils:
    "moz-src:///browser/components/urlbar/private/GeolocationUtils.sys.mjs",
  GeonameMatchType:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  YelpSubjectType:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs",
});

/**
 * @import {GeonameMatch} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs"
 */

const RESULT_MENU_COMMAND = {
  DISMISS: "dismiss",
  HELP: "help",
  INACCURATE_LOCATION: "inaccurate_location",
  MANAGE: "manage",
  NOT_INTERESTED: "not_interested",
  SHOW_LESS_FREQUENTLY: "show_less_frequently",
};

/**
 * A feature for Yelp suggestions.
 */
export class YelpSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return [
      "yelpFeatureGate",
      "suggest.yelp",
      "suggest.quicksuggest.all",
      "suggest.quicksuggest.sponsored",
    ];
  }

  get primaryUserControlledPreferences() {
    return ["suggest.yelp"];
  }

  get rustSuggestionType() {
    return "Yelp";
  }

  get mlIntent() {
    return "yelp_intent";
  }

  get isMlIntentEnabled() {
    // Note that even when ML is enabled, we still leave Yelp Rust suggestions
    // enabled because we need to fetch the Yelp icon, URL, etc. from Rust, as
    // well as geonames, and Rust still needs to ingest all of that.
    return lazy.UrlbarPrefs.get("yelpMlEnabled");
  }

  get showLessFrequentlyCount() {
    const count = lazy.UrlbarPrefs.get("yelp.showLessFrequentlyCount") || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    const cap =
      lazy.UrlbarPrefs.get("yelpShowLessFrequentlyCap") ||
      lazy.QuickSuggest.config.showLessFrequentlyCap ||
      0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  isSuggestionSponsored(_suggestion) {
    return true;
  }

  getSuggestionTelemetryType() {
    return "yelp";
  }

  enable(enabled) {
    if (!enabled) {
      this.#metadataCache = null;
    }
  }

  async filterSuggestions(suggestions) {
    // Important notes:
    //
    // Both Rust and ML return at most one Yelp suggestion each.
    //
    // We leave Rust Yelp suggestions enabled even when ML Yelp is enabled
    // because we need to fetch the Yelp icon, URL, etc. from Rust, as well as
    // geonames, and Rust still needs to ingest all of that. Since we don't have
    // a way to tell the Rust backend to leave a suggestion type enabled without
    // querying it, `suggestions` can contain both kinds of suggestions. If ML
    // is enabled, return the ML suggestion; if it's disabled, return Rust.
    //
    // After this method returns, the Suggest provider will sort suggestions by
    // score and check whether they've been previously dismissed based on their
    // URLs. So we need to make sure suggestions have scores and URLs now. For
    // both Rust and ML suggestions, we'll make sure URLs at this point do *not*
    // contain a location param because we'll likely end up setting a new param
    // in `makeResult()`. That means for the purpose of dismissal, Yelp URLs
    // will exclude location.
    //
    // Since we're doing all the above in this method anyway, we'll also
    // normalize the suggestion so that `makeResult()` can easily handle either
    // kind of suggestion.

    let suggestion;
    if (!lazy.UrlbarPrefs.get("yelpMlEnabled")) {
      suggestion = suggestions.find(s => s.source != "ml");
      if (suggestion) {
        suggestion = this.#normalizeRustSuggestion(suggestion);
      }
    } else {
      suggestion = suggestions.find(s => s.source == "ml");
      if (suggestion) {
        if (!this.#metadataCache) {
          this.#metadataCache = await this.#makeMetadataCache();
        }
        suggestion = this.#normalizeMlSuggestion(suggestion);
      }
    }

    return suggestion ? [suggestion] : [];
  }

  async makeResult(queryContext, suggestion, searchString) {
    // If the user clicked "Show less frequently" at least once or if the
    // subject wasn't typed in full, then apply the min length threshold and
    // return null if the entire search string is too short.
    if (
      (this.showLessFrequentlyCount || !suggestion.subjectExactMatch) &&
      searchString.length < this.#minKeywordLength
    ) {
      return null;
    }

    let { city, region } = suggestion;
    if (!city && !region) {
      // The user didn't specify any location at all, so use geolocation. If we
      // can't get the geolocation for some reason, that's fine, the suggestion
      // just won't have a location.
      let geo = await lazy.GeolocationUtils.geolocation();
      if (geo) {
        city = geo.city;
        region = geo.region_code;
      }
    } else {
      // The user specified a city and/or region -- at least we think they did.
      // If we can't find a matching location, assume they're typing something
      // unrelated to Yelp and discard the suggestion by returning null.
      let match = await this.#bestCityRegion(city, region);
      if (!match) {
        return null;
      }
      city = match.city;
      region = match.region;
    }

    let url = new URL(suggestion.url);

    let title = suggestion.title;
    let locationStr = [city, region].filter(s => !!s).join(", ");
    if (locationStr) {
      url.searchParams.set(suggestion.locationParam, locationStr);
      if (!suggestion.hasLocationSign) {
        title += " in";
      }
      title += " " + locationStr;
    }

    url.searchParams.set("utm_medium", "partner");
    url.searchParams.set("utm_source", "mozilla");

    let resultProperties = {
      type: lazy.UrlbarShared.RESULT_TYPE.URL,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      isBottomUrlSuggestion: true,
      isBestMatch: lazy.UrlbarPrefs.get("yelpSuggestPriority"),
    };

    if (!resultProperties.isBestMatch) {
      let suggestedIndex = lazy.UrlbarPrefs.get("yelpSuggestNonPriorityIndex");
      if (suggestedIndex !== null) {
        resultProperties.isSuggestedIndexRelativeToGroup = true;
        resultProperties.suggestedIndex = suggestedIndex;
      }
    }

    let payload = {
      url: url.toString(),
      originalUrl: suggestion.url,
      subtitleL10n: { id: "urlbar-result-yelp-subtitle" },
      bottomTextL10n: {
        id: "urlbar-result-action-sponsored",
      },
      iconBlob: suggestion.icon_blob,
    };

    if (
      lazy.UrlbarPrefs.get("yelpServiceResultDistinction") &&
      suggestion.subjectType === lazy.YelpSubjectType.SERVICE
    ) {
      payload.titleL10n = {
        id: "firefox-suggest-yelp-service-title",
        args: {
          service: title,
        },
      };
    } else {
      payload.title = title;
    }

    return new lazy.UrlbarResult({
      ...resultProperties,
      payload,
    });
  }

  /**
   * @typedef {object} L10nItem
   * @property {Values<RESULT_MENU_COMMAND>} [name]
   *   The name of the command.
   * @property {{id: string}} [l10n]
   *   The id of the l10n string to use for the translation.
   */

  /**
   * Gets the list of commands that should be shown in the result menu for a
   * given result from the provider. All commands returned by this method should
   * be handled by implementing `onEngagement()` with the possible exception of
   * commands automatically handled by the urlbar, like "help".
   */
  getResultCommands() {
    /** @type {UrlbarResultCommand[]} */
    let commands = [
      {
        name: RESULT_MENU_COMMAND.INACCURATE_LOCATION,
        l10n: {
          id: "urlbar-result-menu-report-inaccurate-location2",
        },
      },
    ];

    if (this.canShowLessFrequently) {
      commands.push({
        name: RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY,
        l10n: {
          id: "urlbar-result-menu-show-less-frequently2",
        },
      });
    }

    commands.push(
      {
        name: RESULT_MENU_COMMAND.DISMISS,
        l10n: {
          id: "urlbar-result-menu-dismiss-suggestion2",
        },
      },
      { name: "separator" },
      {
        name: RESULT_MENU_COMMAND.MANAGE,
        l10n: {
          id: "urlbar-result-menu-manage-firefox-suggest2",
        },
      },
      {
        name: RESULT_MENU_COMMAND.HELP,
        l10n: {
          id: "urlbar-result-menu-learn-more2",
        },
      }
    );

    return commands;
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   * @param {string} searchString
   */
  onEngagement(queryContext, controller, details, searchString) {
    let { result } = details;
    switch (details.selType) {
      case RESULT_MENU_COMMAND.HELP:
      case RESULT_MENU_COMMAND.MANAGE:
        // "manage" and "help" are handled by UrlbarInput, no need to do
        // anything here.
        break;
      case RESULT_MENU_COMMAND.INACCURATE_LOCATION:
        // Currently the only way we record this feedback is in the Glean
        // engagement event. As with all commands, it will be recorded with an
        // `engagement_type` value that is the command's name, in this case
        // `inaccurate_location`.
        controller.view.acknowledgeFeedback(result.id);
        break;
      // selType == "dismiss" when the user presses the dismiss key shortcut.
      case RESULT_MENU_COMMAND.DISMISS:
        lazy.QuickSuggest.dismissResult(result);
        controller.removeResult(result, {
          acknowledgeDismissalL10n: {
            id: "firefox-suggest-dismissal-acknowledgment-one-yelp",
          },
        });
        break;
      case RESULT_MENU_COMMAND.NOT_INTERESTED:
        lazy.UrlbarPrefs.set("suggest.yelp", false);
        controller.removeResult(result, {
          acknowledgeDismissalL10n: {
            id: "firefox-suggest-dismissal-acknowledgment-all-yelp",
          },
        });
        break;
      case RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY:
        this.handleShowLessFrequently(controller, result);
        lazy.UrlbarPrefs.set("yelp.minKeywordLength", searchString.length + 1);
        break;
    }
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        "yelp.showLessFrequentlyCount",
        this.showLessFrequentlyCount + 1
      );
    }
  }

  get #minKeywordLength() {
    // Use the pref value if it has a user value (which means the user clicked
    // "Show less frequently") or if there's no Nimbus value. Otherwise use the
    // Nimbus value. This lets us override the pref's default value using Nimbus
    // if necessary.
    let hasUserValue = Services.prefs.prefHasUserValue(
      "browser.urlbar.yelp.minKeywordLength"
    );
    let nimbusValue = lazy.UrlbarPrefs.get("yelpMinKeywordLength");
    let minLength =
      hasUserValue || nimbusValue === null
        ? lazy.UrlbarPrefs.get("yelp.minKeywordLength")
        : nimbusValue;
    return Math.max(minLength, 0);
  }

  #normalizeRustSuggestion(suggestion) {
    // TODO: The Rust component should be updated to return Yelp suggestions
    // that don't require us to make these modifications.

    // Rust Yelp suggestions don't currently specify the city and region
    // separately. Instead the location param in the URL contains whatever was
    // left over at the end of the search string. We'll assume it's a city. If
    // it's actually a region, then unfortunately we'll discard the suggestion
    // because it won't match any cities in our DB, but it's much more likely
    // for it to be a city.
    let url = new URL(suggestion.url);
    let loc = url.searchParams.get(suggestion.locationParam);
    if (loc) {
      // Normalized suggestion URLs should not include the location. See
      // `filterSuggestions()`.
      url.searchParams.delete(suggestion.locationParam);
      suggestion.url = url.toString();
      suggestion.city = loc;

      // Rust includes the location in the title, but we'll want to replace it
      // with the location we compute in `makeResult()`, so remove it.
      if (suggestion.title.endsWith(loc)) {
        suggestion.title = suggestion.title
          .substring(0, suggestion.title.length - loc.length)
          .trimEnd();
      }
    }

    return suggestion;
  }

  #normalizeMlSuggestion(ml) {
    // The ML model can return false positives, including Yelp-intent
    // suggestions with nothing but a city or region, no subject. Discard them.
    if (!ml.subject) {
      return null;
    }

    let url = new URL(this.#metadataCache.urlOrigin);
    url.pathname = this.#metadataCache.urlPathname;
    url.searchParams.set(this.#metadataCache.findDesc, ml.subject);

    return {
      ...ml,
      title: ml.subject,
      url: url.toString(),
      subjectExactMatch: false,
      hasLocationSign: false,
      locationParam: this.#metadataCache.findLoc,
      icon_blob: this.#metadataCache.iconBlob,
      score: this.#metadataCache.score,
      city: ml.location?.city,
      region: ml.location?.state,
    };
  }

  /**
   * TODO Bug 1926782: ML suggestions don't include an icon, score, or URL, so
   * for now we directly query the Rust backend with a known Yelp keyword and
   * location to get all of that information and then cache it in
   * `#metadataCache`. If the known Yelp suggestion is absent for some reason,
   * we fall back to hardcoded values. This is a tad hacky and we should come up
   * with something better.
   */
  async #makeMetadataCache() {
    let cache;

    this.logger.debug("Querying Rust backend to populate metadata cache");
    let rs = await lazy.QuickSuggest.rustBackend.query("coffee in atlanta", {
      types: ["Yelp"],
    });
    if (!rs.length) {
      this.logger.debug("Rust didn't return any Yelp suggestions!");
      cache = {};
    } else {
      let suggestion = rs[0];
      let url = new URL(suggestion.url);
      let findParamWithValue = value => {
        let tuple = [...url.searchParams.entries()].find(
          ([_, v]) => v == value
        );
        return tuple?.[0];
      };
      cache = {
        iconBlob: suggestion.icon_blob,
        score: suggestion.score,
        urlOrigin: url.origin,
        urlPathname: url.pathname,
        findDesc: findParamWithValue("coffee"),
        findLoc: findParamWithValue("atlanta"),
      };
    }

    let defaults = {
      urlOrigin: "https://www.yelp.com",
      urlPathname: "/search",
      findDesc: "find_desc",
      findLoc: "find_loc",
      score: 0.25,
    };
    for (let [key, value] of Object.entries(defaults)) {
      if (cache[key] === undefined) {
        cache[key] = value;
      }
    }

    return cache;
  }

  /**
   * Looks up a city-region in the Suggest database and returns the one that
   * best matches the client's geolocation.
   *
   * @param {string|null} city
   *   The candidate city name or null if you're only matching regions.
   * @param {string|null} region
   *   The candidate region name or abbreviation, or null if you're only
   *   matching cities.
   * @returns {Promise<{city: string|null, region: string|null}|null>}
   *   If a city was passed in and it didn't match a city in the DB, or if a
   *   region was passed in and it didn't match a region in the DB, null is
   *   returned. Null is also returned if both were passed but they aren't a
   *   valid city-region combination. Otherwise, an object `{ city, region }` is
   *   returned:
   *
   *   city
   *     The best matching city's name, or if the passed-in city was null and a
   *     region was matched, this will be null.
   *   region
   *     The best matching region. If a city was matched, it will be the ISO
   *     code of the city's region (e.g., the usual two-letter abbreviation for
   *     U.S. states). If a city wasn't passed in, this will be the best
   *     matching region's name.
   */
  async #bestCityRegion(city, region) {
    // Match the region first since we'll use region matches to filter city
    // matches. We'll do prefix matching on cities below, so to avoid even more
    // time and work that's probably unnecessary, don't do it for regions.
    let regionMatches;
    if (region) {
      regionMatches = await lazy.QuickSuggest.rustBackend.fetchGeonames(
        region,
        false, // prefix matching
        null // geonames filter array
      );
      if (!regionMatches.length) {
        // The user typed something we thought was a region but isn't, so assume
        // the query is not Yelp-related after all.
        return null;
      }
    }

    if (city) {
      let cityMatches = await lazy.QuickSuggest.rustBackend.fetchGeonames(
        city,
        true, // prefix matching
        regionMatches?.map(m => m.geoname)
      );
      // Discard prefix matches on any names that aren't full names, i.e., on
      // abbreviations and airport codes. Airport codes especially can sometimes
      // be surprising (e.g., "act" for Waco, TX), and we don't want to return
      // too many false positives.
      cityMatches = cityMatches.filter(
        match => match.matchType == lazy.GeonameMatchType.NAME || !match.prefix
      );
      if (!cityMatches.length) {
        // The user typed something we thought was a city but isn't, so assume
        // the query is not Yelp-related after all.
        return null;
      }

      // Return the best city for the user's geolocation.
      let best = await lazy.GeolocationUtils.best(
        cityMatches,
        locationFromGeonameMatch
      );
      return {
        city: best.geoname.name,
        region: best.geoname.adminDivisionCodes.get(1),
      };
    }

    // We didn't detect a city in the query but we detected a region, so try to
    // return at least that, but only if a full name was matched, not an
    // abbreviation. Abbreviations are too short and make it too easy to return
    // false positives. For example, after the user types "ramen in", we
    // probably shouldn't match "in" to Indiana.
    regionMatches = regionMatches?.filter(
      match => match.matchType == lazy.GeonameMatchType.NAME
    );
    if (regionMatches?.length) {
      let best = await lazy.GeolocationUtils.best(
        regionMatches,
        locationFromGeonameMatch
      );
      return { city: null, region: best.geoname.name };
    }

    return null;
  }

  _test_invalidateMetadataCache() {
    this.#metadataCache = null;
  }

  #metadataCache = null;
}

/**
 * A function that can be passed to `GeolocationUtils.best()` as
 * `locationFromItem`. It maps `GeonameMatch` objects to the location objects
 * required by that function.
 *
 * @param {GeonameMatch} match
 *   A match object.
 * @returns {object}
 *   A location object suitable for `GeolocationUtils`.
 */
function locationFromGeonameMatch(match) {
  return {
    latitude: match.geoname.latitude,
    longitude: match.geoname.longitude,
    country: match.geoname.countryCode,
    region: match.geoname.adminDivisionCodes.get(1),
    population: match.geoname.population,
  };
}
