/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SkippableTimer: "resource:///modules/UrlbarUtils.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
});

const SEARCH_PARAMS = {
  CLIENT_VARIANTS: "client_variants",
  PROVIDERS: "providers",
  QUERY: "q",
  SEQUENCE_NUMBER: "seq",
  SESSION_ID: "sid",
};

const SESSION_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

/**
 * Client class for querying the Merino server. Each instance maintains its own
 * session state including a session ID and sequence number that is included in
 * its requests to Merino.
 */
export class MerinoClient {
  /**
   * @returns {object}
   *   The names of URL search params.
   */
  static get SEARCH_PARAMS() {
    return { ...SEARCH_PARAMS };
  }

  /**
   * @param {string} name
   *   An optional name for the client. It will be included in log messages.
   * @param {object} options
   *   Options object
   * @param {string} options.cachePeriodMs
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
  constructor(name = "anonymous", { cachePeriodMs = 0 } = {}) {
    this.#name = name;
    this.#cachePeriodMs = cachePeriodMs;
    ChromeUtils.defineLazyGetter(this, "logger", () =>
      lazy.UrlbarUtils.getLogger({ prefix: `MerinoClient [${name}]` })
    );
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

  /**
   * @returns {number}
   *   The current session ID. Null when there is no active session.
   */
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

  /**
   * @returns {string}
   *   A string that indicates the status of the last fetch. Possible values:
   *   success, timeout, network_error, http_error
   */
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
   * @param {Array} options.providers
   *   Array of provider names to request from Merino. If this is given it will
   *   override the `merinoProviders` Nimbus variable and its fallback pref
   *   `browser.urlbar.merino.providers`.
   * @param {number} options.timeoutMs
   *   Timeout in milliseconds. This method will return once the timeout
   *   elapses, a response is received, or an error occurs, whichever happens
   *   first.
   * @param {object} options.otherParams
   *   If specified, the otherParams will be added as a query params. Currently
   *   used for accuweather's location autocomplete endpoint
   * @returns {Array}
   *   The Merino suggestions or null if there's an error or unexpected
   *   response.
   */
  async fetch({
    query,
    providers = null,
    timeoutMs = lazy.UrlbarPrefs.get("merinoTimeoutMs"),
    otherParams = {},
  }) {
    this.logger.debug("Fetch start", { query });

    // Get the endpoint URL. It's empty by default when running tests so they
    // don't hit the network.
    let endpointString = lazy.UrlbarPrefs.get("merinoEndpointURL");
    if (!endpointString) {
      return [];
    }
    let url = URL.parse(endpointString);
    if (!url) {
      let error = new Error(`${endpointString} is not a valid URL`);
      this.logger.error("Error creating endpoint URL", error);
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
    this.logger.debug("Fetch details", details);

    // If caching is enabled, generate the cache key for this request URL.
    let cacheKey;
    if (this.#cachePeriodMs && !MerinoClient._test_disableCache) {
      url.searchParams.sort();
      cacheKey = url.toString();

      // If we have cached suggestions and they're still valid, return them.
      if (
        this.#cache.suggestions &&
        Date.now() < this.#cache.dateMs + this.#cachePeriodMs &&
        this.#cache.key == cacheKey
      ) {
        this.logger.debug("Fetch served from cache");
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
        logger: this.logger,
        callback: () => this.resetSession(),
      });
    }
    url.searchParams.set(SEARCH_PARAMS.SESSION_ID, this.#sessionID);
    url.searchParams.set(SEARCH_PARAMS.SEQUENCE_NUMBER, this.#sequenceNumber);
    this.#sequenceNumber++;

    let recordResponse = category => {
      this.logger.debug("Fetch done", { status: category });
      this.#lastFetchStatus = category;
      recordResponse = null;
    };

    // Set up the timeout timer.
    let timer = (this.#timeoutTimer = new lazy.SkippableTimer({
      name: "Merino timeout",
      time: timeoutMs,
      logger: this.logger,
      callback: () => {
        // The fetch timed out.
        this.logger.debug("Fetch timed out", { timeoutMs });
        recordResponse?.("timeout");
      },
    }));

    // If there's an ongoing fetch, abort it so there's only one at a time. By
    // design we do not abort fetches on timeout or when the query is canceled
    // so we can record their latency.
    try {
      this.#fetchController?.abort();
    } catch (error) {
      this.logger.error("Error aborting previous fetch", error);
    }

    // Do the fetch.
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
          response = await fetch(url, { signal: controller.signal });
          this.logger.debug("Got response", {
            status: response.status,
            ...details,
          });
          if (!response.ok) {
            recordResponse?.("http_error");
          }
        } catch (error) {
          if (error.name != "AbortError") {
            this.logger.error("Fetch error", error);
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
    let body;
    try {
      body = await response.json();
    } catch (error) {
      this.logger.error("Error getting response as JSON", error);
    }

    if (body) {
      this.logger.debug("Response body", body);
    }

    if (!body?.suggestions?.length) {
      recordResponse?.("no_suggestion");
      return [];
    }

    let { suggestions, request_id } = body;
    if (!Array.isArray(suggestions)) {
      this.logger.error("Unexpected response", body);
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
   * @returns {Promise}
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
   * @returns {Promise}
   */
  waitForNextSessionReset() {
    if (!this.#nextSessionResetDeferred) {
      this.#nextSessionResetDeferred = Promise.withResolvers();
    }
    return this.#nextSessionResetDeferred.promise;
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
  #sessionID = null;
  #sequenceNumber = 0;
  #sessionTimer = null;
  #sessionTimeoutMs = SESSION_TIMEOUT_MS;

  #name;
  #timeoutTimer = null;
  #fetchController = null;
  #lastFetchStatus = null;
  #nextResponseDeferred = null;
  #nextSessionResetDeferred = null;
  #cachePeriodMs = 0;

  // When caching is enabled, we cache response suggestions from the most recent
  // successful request.
  #cache = {
    // The cached suggestions array.
    suggestions: null,
    // The cache key: the stringified request URL without session-related params
    // (session ID and sequence number).
    key: null,
    // The date the suggestions were cached as returned by `Date.now()`.
    dateMs: 0,
  };
}
