/* eslint-disable no-eq-null */
/* eslint-disable no-param-reassign */
/* eslint-disable eqeqeq */
/* eslint-disable no-multi-assign */
/* eslint-disable max-statements */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  GeolocationUtils:
    "moz-src:///browser/components/urlbar/private/GeolocationUtils.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  SkippableTimer: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * @import {SkippableTimer} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs"
 * @import {OHTTPResponse} from "resource://gre/modules/ObliviousHTTP.sys.mjs"
 */

/**
 * @typedef {object} MerinoClientBaseSuggestion
 * @property {string} request_id
 *   The request id associated with the suggestion.
 * @property {string} source
 *   The source of the suggestion.
 *
 * @typedef {{[key: string]:any} & MerinoClientBaseSuggestion} MerinoClientSuggestion
 *   Details of a suggestion received from Merino. Whilst the base properties are
 *   consistent the suggestion properties may vary depending on the provider.
 */

const SEARCH_PARAMS = Object.freeze({
  CLIENT_VARIANTS: "client_variants",
  PROVIDERS: "providers",
  QUERY: "q",
  SEQUENCE_NUMBER: "seq",
  SESSION_ID: "sid",
});

const SESSION_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

/**
 * Client class for querying the Merino server. Each instance maintains its own
 * session state including a session ID and sequence number that is included in
 * its requests to Merino.
 */
export class TemporaryMerinoClientShim {
  #lazy = XPCOMUtils.declareLazy({
    logger: () =>
      lazy.UrlbarShared.getLogger({
        prefix: `MerinoClient [${this.#name}]`,
      }),
  });

  /**
   * The names of URL search params.
   */
  static get SEARCH_PARAMS() {
    return { ...SEARCH_PARAMS };
  }

  /**
   * @param {string} [name]
   *   An optional name for the client. It will be included in log messages.
   * @param {object} [options]
   *   Options object
   * @param {boolean} [options.allowOhttp]
   *   Whether the client is allowed to make its requests using OHTTP. When true
   *   and the following prefs are defined, all requests made by the client will
   *   use OHTTP:
   *
   *   browser.urlbar.merino.ohttpConfigURL (Nimbus: merinoOhttpConfigURL)
   *   browser.urlbar.merino.ohttpRelayURL (Nimbus: merinoOhttpRelayURL)
   *
   * @param {number} [options.cachePeriodMs]
   *   Enables caching when nonzero. The client will cache the response
   *   suggestions from its most recent successful request for the specified
   *   period. The client will serve the cached suggestions for all fetches for
   *   the same URL until either the cache period elapses or a successful fetch
   *   for a different URL is made (ignoring session-related URL params like
   *   session ID and sequence number). Caching is per `MerinoClient` instance
   *   and is not shared across instances.
   *
   *   WARNING: Cached suggestions are only ever evicted when new suggestions
   *   are cached. They are not evicted on a timer. If the client has cached
   *   some suggestions and no further fetches are made, they'll stay cached
   *   indefinitely. If your request URLs contain senstive data that should not
   *   stick around in the object graph indefinitely, you should either not use
   *   caching or you should implement an eviction mechanism.
   *
   *   This cache strategy is intentionally simplistic and designed to be used
   *   by the urlbar with very short cache periods to make sure Firefox doesn't
   *   repeatedly call the same Merino URL on each keystroke in a urlbar
   *   session, which is wasteful and can cause a suggestion to flicker out of
   *   and into the urlbar panel as the user matches it again and again,
   *   especially when Merino latency is high. It is not designed to be a
   *   general caching mechanism. If you need more complex or long-lived
   *   caching, try working with the Merino team to add cache headers to the
   *   relevant responses so you can leverage Firefox's HTTP cache.
   */
  constructor(
    name = "anonymous",
    { allowOhttp = false, cachePeriodMs = 0 } = {}
  ) {
    this.#name = name;
    this.#allowOhttp = allowOhttp;
    this.#cachePeriodMs = cachePeriodMs;
  }

  /**
   * @returns {string}
   *   The name of the client.
   */
  get name() {
    return this.#name;
  }

  /**
   * @returns {number}
   *   If `resetSession()` is not called within this timeout period after a
   *   session starts, the session will time out and the next fetch will begin a
   *   new session.
   */
  get sessionTimeoutMs() {
    return this.#sessionTimeoutMs;
  }
  set sessionTimeoutMs(value) {
    this.#sessionTimeoutMs = value;
  }

  // Note: Cannot be JSDoc due to https://github.com/pyodide/sphinx-js/issues/242
  // The current session ID. Null when there is no active session.
  get sessionID() {
    return this.#sessionID;
  }

  /**
   * @returns {number}
   *   The current sequence number in the current session. Zero when there is no
   *   active session.
   */
  get sequenceNumber() {
    return this.#sequenceNumber;
  }

  // Note: Cannot be JSDoc due to https://github.com/pyodide/sphinx-js/issues/242
  // A string that indicates the status of the last fetch. Possible values:
  // success, timeout, network_error, http_error
  get lastFetchStatus() {
    return this.#lastFetchStatus;
  }

  /**
   * Fetches Merino suggestions.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.query
   *   The search string.
   * @param {string[]} options.providers
   *   Array of provider names to request from Merino. If this is given it will
   *   override the `merinoProviders` Nimbus variable and its fallback pref
   *   `browser.urlbar.merino.providers`.
   * @param {number} options.timeoutMs
   *   Timeout in milliseconds. This method will return once the timeout
   *   elapses, a response is received, or an error occurs, whichever happens
   *   first.
   * @param {{[key: string]: string}} options.otherParams
   *   If specified, the otherParams will be added as a query params. Currently
   *   used for accuweather's location autocomplete endpoint
   * @param {string} [options.endpointUrl]
   *   If specified, overrides the default `merinoEndpointURL` pref value.
   * @param {string} [options.acceptLanguage]
   *   If specified, sent as the `Accept-Language` request header so Merino
   *   (and any upstream provider it delegates to) can return localized
   *   results.
   * @returns {Promise<MerinoClientSuggestion[]>}
   *   The Merino suggestions or null if there's an error or unexpected
   *   response.
   */
  async fetch({
    query,
    providers = null,
    timeoutMs = lazy.UrlbarPrefs.get("merinoTimeoutMs"),
    otherParams = {},
    endpointUrl = null,
    acceptLanguage = null,
  }) {
    this.#lazy.logger.debug("Fetch start", { query });

    // If endpointUrl is explicitly provided, use it as-is
    // otherwise fall back to the general Merino endpoint pref
    let endpointString =
      endpointUrl || lazy.UrlbarPrefs.get("merinoEndpointURL");
    if (!endpointString) {
      return [];
    }
    let url = URL.parse(endpointString);
    if (!url) {
      let error = new Error(`${endpointString} is not a valid URL`);
      this.#lazy.logger.error("Error creating endpoint URL", error);
      return [];
    }

    // Start setting search params. Leave session-related params for last.
    url.searchParams.set(SEARCH_PARAMS.QUERY, query);

    let clientVariants = lazy.UrlbarPrefs.get("merinoClientVariants");
    if (clientVariants) {
      url.searchParams.set(SEARCH_PARAMS.CLIENT_VARIANTS, clientVariants);
    }

    let providersString;
    if (providers != null) {
      if (!Array.isArray(providers)) {
        throw new Error("providers must be an array if given");
      }
      providersString = providers.join(",");
    } else {
      let value = lazy.UrlbarPrefs.get("merinoProviders");
      if (value) {
        // The Nimbus variable/pref is used only if it's a non-empty string.
        providersString = value;
      }
    }

    // An empty providers string is a valid value and means Merino should
    // receive the request but not return any suggestions, so do not do a simple
    // `if (providersString)` here.
    if (typeof providersString == "string") {
      url.searchParams.set(SEARCH_PARAMS.PROVIDERS, providersString);
    }

    // if otherParams are present add them to the url
    for (const [param, value] of Object.entries(otherParams)) {
      url.searchParams.set(param, value);
    }

    // At this point, all search params should be set except for session-related
    // params.

    let details = { query, providers, timeoutMs, url: url.toString() };

    // If caching is enabled, generate the cache key for this request URL.
    let cacheKey;
    if (this.#cachePeriodMs && !TemporaryMerinoClientShim._test_disableCache) {
      url.searchParams.sort();
      cacheKey = url.toString();

      // If we have cached suggestions and they're still valid, return them.
      if (
        this.#cache.suggestions &&
        Date.now() < this.#cache.dateMs + this.#cachePeriodMs &&
        this.#cache.key == cacheKey
      ) {
        this.#lazy.logger.debug("Fetch served from cache", details);
        return this.#cache.suggestions;
      }
    }

    // At this point, we're calling Merino.

    // Set up the Merino session ID and related state. The session ID is a UUID
    // without leading and trailing braces.
    if (!this.#sessionID) {
      let uuid = Services.uuid.generateUUID().toString();
      this.#sessionID = uuid.substring(1, uuid.length - 1);
      this.#sequenceNumber = 0;
      this.#sessionTimer?.cancel();

      // Per spec, for the user's privacy, the session should time out and a new
      // session ID should be used if the engagement does not end soon.
      this.#sessionTimer = new lazy.SkippableTimer({
        name: "Merino session timeout",
        time: this.#sessionTimeoutMs,
        logger: this.#lazy.logger,
        callback: () => this.resetSession(),
      });
    }
    url.searchParams.set(SEARCH_PARAMS.SESSION_ID, this.#sessionID);
    url.searchParams.set(
      SEARCH_PARAMS.SEQUENCE_NUMBER,
      this.#sequenceNumber.toString()
    );
    this.#sequenceNumber++;

    this.#lazy.logger.debug("Fetch details", {
      ...details,
      url: url.toString(),
    });

    /** @type {(category: string) => void} */
    let recordResponse = category => {
      this.#lazy.logger.debug("Fetch done", { status: category });
      this.#lastFetchStatus = category;
      recordResponse = null;
    };

    // Set up the timeout timer.
    let timer = (this.#timeoutTimer = new lazy.SkippableTimer({
      name: "Merino timeout",
      time: timeoutMs,
      logger: this.#lazy.logger,
      callback: () => {
        // The fetch timed out.
        this.#lazy.logger.debug("Fetch timed out", { timeoutMs });
        recordResponse?.("timeout");
      },
    }));

    // If there's an ongoing fetch, abort it so there's only one at a time. By
    // design we do not abort fetches on timeout or when the query is canceled
    // so we can record their latency.
    try {
      this.#fetchController?.abort();
    } catch (error) {
      this.#lazy.logger.error("Error aborting previous fetch", error);
    }

    // Do the fetch.
    /** @type {?OHTTPResponse|?Response} */
    let response;
    let controller = (this.#fetchController = new AbortController());
    await Promise.race([
      timer.promise,
      (async () => {
        try {
          // Canceling the timer below resolves its promise, which can resolve
          // the outer promise created by `Promise.race`. This inner async
          // function happens not to await anything after canceling the timer,
          // but if it did, `timer.promise` could win the race and resolve the
          // outer promise without a value. For that reason, we declare
          // `response` in the outer scope and set it here instead of returning
          // the response from this inner function and assuming it will also be
          // returned by `Promise.race`.
          let result = await this.#fetch(url, {
            signal: controller.signal,
            acceptLanguage,
          });
          response = result?.response;
          this.#lazy.logger.debug("Got response", {
            status: response?.status,
            elapsedMs: result ? result.elapsedMs : "n/a",
            ...details,
          });
          if (!response?.ok) {
            recordResponse?.("http_error");
          }
        } catch (error) {
          if (error.name != "AbortError") {
            this.#lazy.logger.error("Fetch error", error);
            recordResponse?.("network_error");
          }
        } finally {
          // Now that the fetch is done, cancel the timeout timer so it doesn't
          // fire and record a timeout. If it already fired, which it would have
          // on timeout, or was already canceled, this is a no-op.
          timer.cancel();
          if (controller == this.#fetchController) {
            this.#fetchController = null;
          }
          this.#nextResponseDeferred?.resolve(response);
          this.#nextResponseDeferred = null;
        }
      })(),
    ]);
    if (timer == this.#timeoutTimer) {
      this.#timeoutTimer = null;
    }

    if (!response?.ok) {
      // `recordResponse()` was already called above, no need to call it here.
      return [];
    }

    if (response.status == 204) {
      // No content. We check for this because `response.json()` (below) throws
      // in this case, and since we log the error it can spam the console.
      recordResponse?.("no_suggestion");
      return [];
    }

    // Get the response body as an object.
    /** @type {{suggestions: MerinoClientSuggestion[], request_id: string }} */
    let body;
    try {
      body = /** @type {any} */ (await response.json());
    } catch (error) {
      this.#lazy.logger.error("Error getting response as JSON", error);
    }

    if (body) {
      this.#lazy.logger.debug("Response body", body);
    }

    if (!body?.suggestions?.length) {
      recordResponse?.("no_suggestion");
      return [];
    }

    let { suggestions, request_id } = body;
    if (!Array.isArray(suggestions)) {
      this.#lazy.logger.error("Unexpected response", body);
      recordResponse?.("no_suggestion");
      return [];
    }

    recordResponse?.("success");
    suggestions = suggestions.map(suggestion => ({
      ...suggestion,
      request_id,
      source: "merino",
    }));

    if (cacheKey) {
      this.#cache = {
        suggestions,
        key: cacheKey,
        dateMs: Date.now(),
      };
    }

    return suggestions;
  }

  /**
   * Auto complete the weather location name. This is intended to be used only
   * in conjunction with `fetchWeatherReport()` and causes Merino to call the location
   * autocomplete endpoint of a third-party weather API.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string=} options.query
   *   The string that will be autocompleted.
   * @param {number=} options.timeoutMs
   *   Timeout in milliseconds. This method will return once the timeout
   *   elapses, a response is received, or an error occurs, whichever happens
   *   first.
   * @returns {Promise<MerinoClientSuggestion>}
   *   The Merino suggestions or null if there's an error or unexpected
   *   response.
   */
  async autoCompleteWeatherLocation({ source, query, timeoutMs = undefined }) {
    let response = await this.fetch({
      providers: ["accuweather"],
      query: query || "",
      otherParams: {
        request_type: "location",
        source,
      },
      timeoutMs,
    });
    return response?.[0] ?? null;
  }

  /**
   * Fetch weather report information (current conditions + daily forecast) for
   * a given location. There are three separate options for specifying the
   * location, choose only one:
   *
   * (1) Pass `locationName`
   * (2) Pass `city`, `region`, and `country`
   * (3) Don't pass any of the above, in which case geolocation will be used
   *
   * If the `locationName` is specified, it will be chosen over the other.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string=} options.locationName
   *   The location name that should come from autoCompleteWeatherLocation().
   * @param {string=} options.country
   *   The country that should be a country code.
   * @param {string=} options.region
   *   The region that should be a comma-separated string of administrative
   *   region codes.
   * @param {string=} options.city
   *   The city that should be a city name.
   * @param {number=} options.timeoutMs
   *   Timeout in milliseconds. This method will return once the timeout
   *   elapses, a response is received, or an error occurs, whichever happens
   *   first.
   * @returns {Promise<MerinoClientSuggestion>}
   *   The Merino suggestions or null if there's an error or unexpected
   *   response.
   */
  async fetchWeatherReport({
    source,
    city = undefined,
    country = undefined,
    region = undefined,
    locationName = undefined,
    timeoutMs = undefined,
    endpointUrl = undefined,
  }) {
    if (locationName) {
      city = undefined;
      country = undefined;
      region = undefined;
    } else if (!city && !country && !region) {
      let geolocation = await lazy.GeolocationUtils.geolocation();
      if (!geolocation) {
        return null;
      }

      country = geolocation.country_code;
      region =
        geolocation.region_code || geolocation.region || geolocation.city;
      city = geolocation.city || geolocation.region;
    }

    let otherParams = {
      request_type: "weather",
      source,
    };

    if (country) {
      otherParams.country = country;
    }
    if (region) {
      otherParams.region = region;
    }
    if (city) {
      otherParams.city = city;
    }

    let response = await this.fetch({
      providers: ["accuweather"],
      query: locationName ?? "",
      otherParams,
      timeoutMs,
      endpointUrl,
      acceptLanguage: Services.locale.appLocaleAsBCP47,
    });
    return response?.[0] ?? null;
  }

  /**
   * Fetch hourly weather forecasts for a given location. There are three
   * separate options for specifying the location, choose only one:
   *
   * (1) Pass `locationName`
   * (2) Pass `city`, `region`, and `country`
   * (3) Don't pass any of the above, in which case geolocation will be used
   *
   * If the `locationName` is specified, it will be chosen over the others.
   *
   * Unlike fetchWeatherReport() which returns a single Merino suggestion
   * object, fetchHourlyForecasts() returns an array of
   * hourly forecast slots, so the structure is set to be different. Each
   * slot has: date_time, epoch_date_time, icon_id, temperature ({ c, f }),
   * and url.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string=} options.locationName
   *   The location name that should come from autoCompleteWeatherLocation().
   * @param {string=} options.country
   *   The country that should be a country code.
   * @param {string=} options.region
   *   The region that should be a comma-separated string of administrative
   *   region codes.
   * @param {string=} options.city
   *   The city that should be a city name.
   * @returns {Promise<Array<{date_time: string, epoch_date_time: number, icon_id: number, temperature: {c: number, f: number}, url: string}>|null>}
   *   Array of hourly forecast slots, or null if the endpoint is not
   *   configured or an error occurs.
   */
  async fetchHourlyForecasts({
    source,
    locationName = undefined,
    city = undefined,
    region = undefined,
    country = undefined,
    endpointUrl = undefined,
  }) {
    const hourlyEndpointURL = endpointUrl;

    if (!hourlyEndpointURL) {
      return null;
    }

    let url = URL.parse(hourlyEndpointURL);
    if (!url) {
      this.#lazy.logger.error(
        "Invalid hourly forecast endpoint URL",
        hourlyEndpointURL
      );
      return null;
    }

    if (locationName) {
      url.searchParams.set("q", locationName);
    }

    // Unlike the suggest endpoint used by fetchWeatherReport(), the hourly
    // endpoint doesn't resolve AccuWeather location keys from q alone.
    // Always include city/region/country so the endpoint can identify the
    // location, falling back to geolocation when they aren't provided.
    if (!city && !country && !region) {
      let geolocation = await lazy.GeolocationUtils.geolocation();
      if (!geolocation) {
        return null;
      }
      country = geolocation.country_code;
      region =
        geolocation.region_code || geolocation.region || geolocation.city;
      city = geolocation.city || geolocation.region;
    }
    if (city) {
      url.searchParams.set("city", city);
    }
    if (region) {
      url.searchParams.set("region", region);
    }
    if (country) {
      url.searchParams.set("country", country);
    }

    if (source) {
      url.searchParams.set("source", source);
    }

    try {
      const response = await fetch(url, {
        headers: { "Accept-Language": Services.locale.appLocaleAsBCP47 },
      });
      /**
       * @type {Array<{
       *   date_time: string,
       *   epoch_date_time: number,
       *   icon_id: number,
       *   temperature: {c: number, f: number},
       *   url: string
       * }>}
       */
      const data = /** @type {any} */ (await response.json());

      this.#lazy.logger.debug("fetchHourlyForecasts response", data);
      return data;
    } catch (e) {
      this.#lazy.logger.error("Hourly forecast fetch error", e);
      return null;
    }
  }

  /**
   * Fetch the list of available sports teams from the Merino WCS endpoint.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string} options.endpointUrl
   *   The teams endpoint URL.
   * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
   *   `error: null` with `data: null` means the endpoint is not configured
   *   (deliberate skip).
   */
  async fetchSportsTeams({ source, endpointUrl }) {
    if (!endpointUrl) {
      return { data: null, error: null };
    }
    let url = URL.parse(endpointUrl);
    if (!url) {
      this.#lazy.logger.error("Invalid sports teams endpoint URL", endpointUrl);
      return { data: null, error: "invalid_url" };
    }
    if (source) {
      url.searchParams.set("source", source);
    }
    try {
      const response = await fetch(url);
      if (!response.ok) {
        this.#lazy.logger.error(
          `Sports teams fetch failed with status ${response.status}`
        );
        return { data: null, error: "load_error" };
      }
      const data = /** @type {any} */ (await response.json());
      this.#lazy.logger.debug("fetchSportsTeams response", data);
      return { data, error: null };
    } catch (e) {
      this.#lazy.logger.error("Sports teams fetch error", e);
      return { data: null, error: "load_error" };
    }
  }

  /**
   * Fetch upcoming matches from the Merino WCS endpoint.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string} options.endpointUrl
   *   The matches endpoint URL.
   * @param {string} [options.date]
   *   Optional YYYY-MM-DD date around which the backend should return its
   *   ±21 day window of matches. Used by the load-more scroll on both the
   *   Upcoming and Results lists (stepping the date forward to load future
   *   matches or backward to load older ones); omit for the initial fetch
   *   (which defaults to "today" backend-side).
   * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
   *   `error: null` with `data: null` means the endpoint is not configured
   *   (deliberate skip).
   */
  async fetchSportsMatches({ source, endpointUrl, date }) {
    if (!endpointUrl) {
      return { data: null, error: null };
    }
    let url = URL.parse(endpointUrl);
    if (!url) {
      this.#lazy.logger.error(
        "Invalid sports matches endpoint URL",
        endpointUrl
      );
      return { data: null, error: "invalid_url" };
    }
    if (source) {
      url.searchParams.set("source", source);
    }
    if (date) {
      url.searchParams.set("date", date);
    }
    try {
      const response = await fetch(url);
      if (!response.ok) {
        this.#lazy.logger.error(
          `Sports matches fetch failed with status ${response.status}`
        );
        return { data: null, error: "load_error" };
      }
      const data = /** @type {any} */ (await response.json());
      this.#lazy.logger.debug("fetchSportsMatches response", data);
      return { data, error: null };
    } catch (e) {
      this.#lazy.logger.error("Sports matches fetch error", e);
      return { data: null, error: "load_error" };
    }
  }

  /**
   * Fetch currently-live matches from the Merino WCS endpoint.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string} options.endpointUrl
   *   The live matches endpoint URL.
   * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
   *   On success, `data` is `{ matches: [...] }`. `error: null` with
   *   `data: null` means the endpoint is not configured (deliberate skip).
   */
  async fetchSportsLive({ source, endpointUrl }) {
    if (!endpointUrl) {
      return { data: null, error: null };
    }
    let url = URL.parse(endpointUrl);
    if (!url) {
      this.#lazy.logger.error("Invalid sports live endpoint URL", endpointUrl);
      return { data: null, error: "invalid_url" };
    }
    if (source) {
      url.searchParams.set("source", source);
    }
    try {
      const response = await fetch(url);
      if (!response.ok) {
        this.#lazy.logger.error(
          `Sports live fetch failed with status ${response.status}`
        );
        return { data: null, error: "load_error" };
      }
      const data = /** @type {any} */ (await response.json());
      this.#lazy.logger.debug("fetchSportsLive response", data);
      return { data, error: null };
    } catch (e) {
      this.#lazy.logger.error("Sports live fetch error", e);
      return { data: null, error: "load_error" };
    }
  }

  /**
   * Fetch watch-live broadcaster listings for the active sports event from
   * the Merino WCS endpoint.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string} options.endpointUrl
   *   The watch-live endpoint URL.
   * @param {string} [options.acceptLanguage]
   *   Optional BCP-47 locale string sent as the Accept-Language header so
   *   the backend can localize broadcaster/region copy.
   * @returns {Promise<object|null>}
   *   The watch-live payload, or null if the endpoint is not configured or
   *   an error occurs. The caller is responsible for shape-validating the
   *   payload before consuming it.
   */
  async fetchWatchLive({ source, endpointUrl, acceptLanguage }) {
    if (!endpointUrl) {
      return null;
    }
    let url = URL.parse(endpointUrl);
    if (!url) {
      this.#lazy.logger.error(
        "Invalid sports watch-live endpoint URL",
        endpointUrl
      );
      return null;
    }
    if (source) {
      url.searchParams.set("source", source);
    }
    const fetchOptions = acceptLanguage
      ? { headers: { "Accept-Language": acceptLanguage } }
      : undefined;
    try {
      const response = await fetch(url, fetchOptions);
      if (!response.ok) {
        this.#lazy.logger.error(
          `Sports watch-live fetch failed with status ${response.status}`
        );
        return null;
      }
      const data = /** @type {any} */ (await response.json());
      this.#lazy.logger.debug("fetchWatchLive response", data);
      return data;
    } catch (e) {
      this.#lazy.logger.error("Sports watch-live fetch error", e);
      return null;
    }
  }

  /**
   * Fetch the daily "Picture of the day" from the Merino endpoint.
   *
   * @param {object} options
   *   Options object
   * @param {string} options.source
   *   The source that is requesting this fetch.
   * @param {string} options.endpointUrl
   *   The picture-of-the-day endpoint URL.
   * @param {string} [options.acceptLanguage]
   *   Optional BCP-47 locale string sent as the Accept-Language header so the
   *   backend can return a localized description.
   * @returns {Promise<{data: object|null, error: null | "invalid_url" | "load_error"}>}
   *   `error: null` with `data: null` means the endpoint is not configured
   *   (deliberate skip).
   */
  async fetchPictureOfTheDay({ source, endpointUrl, acceptLanguage }) {
    if (!endpointUrl) {
      return { data: null, error: null };
    }
    let url = URL.parse(endpointUrl);
    if (!url) {
      this.#lazy.logger.error(
        "Invalid picture-of-the-day endpoint URL",
        endpointUrl
      );
      return { data: null, error: "invalid_url" };
    }
    if (source) {
      url.searchParams.set("source", source);
    }
    const fetchOptions = acceptLanguage
      ? { headers: { "Accept-Language": acceptLanguage } }
      : undefined;
    try {
      const response = await fetch(url, fetchOptions);
      if (!response.ok) {
        this.#lazy.logger.error(
          `Picture of the day fetch failed with status ${response.status}`
        );
        return { data: null, error: "load_error" };
      }
      const data = /** @type {any} */ (await response.json());
      this.#lazy.logger.debug("fetchPictureOfTheDay response", data);
      return { data, error: null };
    } catch (e) {
      this.#lazy.logger.error("Picture of the day fetch error", e);
      return { data: null, error: "load_error" };
    }
  }

  /**
   * Resets the Merino session ID and related state.
   */
  resetSession() {
    this.#sessionID = null;
    this.#sequenceNumber = 0;
    this.#sessionTimer?.cancel();
    this.#sessionTimer = null;
    this.#nextSessionResetDeferred?.resolve();
    this.#nextSessionResetDeferred = null;
  }

  /**
   * Cancels the timeout timer.
   */
  cancelTimeoutTimer() {
    this.#timeoutTimer?.cancel();
  }

  /**
   * Returns a promise that's resolved when the next response is received or a
   * network error occurs.
   *
   * @returns {Promise<?Response|?OHTTPResponse>}
   *   The promise is resolved with the `Response` object or undefined if a
   *   network error occurred.
   */
  waitForNextResponse() {
    if (!this.#nextResponseDeferred) {
      this.#nextResponseDeferred = Promise.withResolvers();
    }
    return this.#nextResponseDeferred.promise;
  }

  /**
   * Returns a promise that's resolved when the session is next reset, including
   * on session timeout.
   *
   * @returns {Promise<void>}
   */
  waitForNextSessionReset() {
    if (!this.#nextSessionResetDeferred) {
      this.#nextSessionResetDeferred = Promise.withResolvers();
    }
    return this.#nextSessionResetDeferred.promise;
  }

  /**
   * Sends the Merino request. Uses OHTTP if `allowOhttp` is true and the Merino
   * OHTTP prefs are defined.
   *
   * @param {URL} url
   *   The request URL.
   * @param {object} options
   *   Options object.
   * @param {AbortSignal} options.signal
   *   An `AbortController.signal` for the fetch.
   * @param {string} [options.acceptLanguage]
   *   If set, sent as the `Accept-Language` request header.
   * @returns {Promise<?FetchResult>}
   *   The fetch result, or null if the fetch couldn't be started.
   *
   * @typedef {object} FetchResult
   * @property {OHTTPResponse|Response} response
   *   The response object.
   * @property {number} elapsedMs
   *   The duration of the fetch in ms.
   */
  async #fetch(url, { signal, acceptLanguage }) {
    let configUrl;
    let relayUrl;
    if (this.#allowOhttp) {
      configUrl = lazy.UrlbarPrefs.get("merinoOhttpConfigURL");
      relayUrl = lazy.UrlbarPrefs.get("merinoOhttpRelayURL");
    }

    let useOhttp = configUrl && relayUrl;

    let headers = acceptLanguage ? { "Accept-Language": acceptLanguage } : {};

    let response;
    let startMs = ChromeUtils.now();
    if (!useOhttp) {
      response = await fetch(url, { signal, headers });
    } else {
      let config = await lazy.ObliviousHTTP.getOHTTPConfig(configUrl);
      if (!config) {
        this.#lazy.logger.error("Couldn't get OHTTP config");
        return null;
      }

      this.#lazy.logger.debug("Sending request using OHTTP", { url });
      response = await lazy.ObliviousHTTP.ohttpRequest(relayUrl, config, url, {
        signal,
        headers,
      });
    }

    let elapsedMs = ChromeUtils.now() - startMs;
    let label = response.status.toString();
    if (useOhttp) {
      label += "_ohttp";
    }
    Glean.urlbarMerino.latencyByResponseStatus[label].accumulateSamples([
      elapsedMs,
    ]);

    return { response, elapsedMs };
  }

  static _test_disableCache = false;

  get _test_sessionTimer() {
    return this.#sessionTimer;
  }

  get _test_timeoutTimer() {
    return this.#timeoutTimer;
  }

  get _test_fetchController() {
    return this.#fetchController;
  }

  // State related to the current session.
  /** @type {string} */
  #sessionID = null;
  #sequenceNumber = 0;
  /** @type {SkippableTimer} */
  #sessionTimer = null;
  #sessionTimeoutMs = SESSION_TIMEOUT_MS;

  #name;
  /** @type {SkippableTimer} */
  #timeoutTimer = null;
  /** @type {AbortController} */
  #fetchController = null;
  /** @type {string} */
  #lastFetchStatus = null;
  /** @type {PromiseWithResolvers<?Response|?OHTTPResponse>} */
  #nextResponseDeferred = null;
  /** @type {PromiseWithResolvers<void>} */
  #nextSessionResetDeferred = null;
  #cachePeriodMs = 0;
  #allowOhttp = false;

  // When caching is enabled, we cache response suggestions from the most recent
  // successful request.
  #cache = {
    /**
     * @type {MerinoClientSuggestion[]}
     *   The cached suggestions array.
     */
    suggestions: null,
    /**
     * @type {string}
     *   The cache key: the stringified request URL without session-related
     *   params (session ID and sequence number).
     */
    key: null,
    /**
     * The date the suggestions were cached as returned by `Date.now()`.
     */
    dateMs: 0,
  };
}
