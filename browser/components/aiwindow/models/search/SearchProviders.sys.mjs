/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Search providers for the web search flow.
 */

import { openAIEngine } from "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs";
import {
  SEARCH_QUERY_APIKEY_PREF,
  SEARCH_QUERY_ENDPOINT_PREF,
} from "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const DEFAULT_SEARCH_QUERY_ENDPOINT =
  "https://funsearchapp.netlify.app/v1/search";
const REQUEST_TIMEOUT_MS = 15000;

/**
 * A normalized search result.
 *
 * @typedef {object} SearchResult
 * @property {string} title - Result title.
 * @property {string} url - Result URL.
 * @property {string} snippet - Short text excerpt describing the result.
 * @property {string} [publishedDate] - ISO-8601 publish date when provided.
 */

/**
 * Base interface for search providers.
 *
 * Subclasses must override `search`. Each provider is responsible for its
 * own authentication, request shape, response parsing, and error mapping.
 */
export class SearchProvider {
  /**
   * Executes a search.
   *
   * @param {string} _query - User-facing search query.
   * @param {object} [_options] - Provider-specific options.
   * @returns {Promise<{results: SearchResult[], raw: object}>}
   *   Normalized results and the unmodified provider response.
   */
  async search(_query, _options) {
    throw new Error("SearchProvider.search must be implemented by subclass");
  }
}

/**
 * Search provider backed by the Exa search API, proxied through MLPA's
 * /v1/search endpoint.
 *
 * The base URL is read from a dedicated pref so that users with a custom
 * chat endpoint (BYOM) still hit Mozilla's MLPA for search. Auth is the
 * FxA OAuth token used elsewhere for MLPA requests.
 */
export class ExaSearchProvider extends SearchProvider {
  static MAX_RESULTS = 10;
  // a static property to allow mocking the fetch function in tests
  static _fetch = (url, options) => fetch(url, options);

  /**
   * Executes a search against MLPA /v1/search.
   *
   * @param {string} query - Non-empty search query.
   * @param {object} [options]
   * @param {number} [options.maxResults] - Number of results to request,
   *   clamped to [1, ExaSearchProvider.MAX_RESULTS]. Defaults to
   *   ExaSearchProvider.MAX_RESULTS.
   * @returns {Promise<{results: SearchResult[], raw: object}>}
   *   Normalized result list and the raw MLPA JSON response.
   * @throws {Error} On non-2xx response, network failure, or timeout.
   */
  async search(query, options = {}) {
    if (typeof query !== "string" || !query.trim()) {
      throw new Error(
        "ExaSearchProvider.search: query must be a non-empty string"
      );
    }

    const endpoint = Services.prefs.getStringPref(
      SEARCH_QUERY_ENDPOINT_PREF,
      DEFAULT_SEARCH_QUERY_ENDPOINT
    );
    if (!endpoint) {
      throw new Error("ExaSearchProvider.search: endpoint pref is empty");
    }

    const requestedMax = Number.isInteger(options.maxResults)
      ? options.maxResults
      : ExaSearchProvider.MAX_RESULTS;
    const maxResults = Math.min(
      ExaSearchProvider.MAX_RESULTS,
      Math.max(1, requestedMax)
    );

    const token =
      Services.prefs.getStringPref(SEARCH_QUERY_APIKEY_PREF, "") ||
      (await openAIEngine.getFxAccountToken());
    if (!token) {
      throw new Error("ExaSearchProvider.search: auth token unavailable");
    }

    const controller = new AbortController();
    const timeoutId = lazy.setTimeout(
      () => controller.abort(),
      REQUEST_TIMEOUT_MS
    );

    let response;
    try {
      response = await ExaSearchProvider._fetch(endpoint, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Accept: "application/json",
          "service-type": "search",
          Authorization: `Bearer ${token}`,
        },
        body: JSON.stringify({ query, max_results: maxResults }),
        signal: controller.signal,
      });
    } catch (err) {
      if (err?.name === "AbortError") {
        throw new Error(
          `ExaSearchProvider.search: request timed out after ${REQUEST_TIMEOUT_MS}ms`
        );
      }
      throw err;
    } finally {
      lazy.clearTimeout(timeoutId);
    }

    if (!response.ok) {
      let body = "";
      try {
        body = await response.text();
      } catch (_e) {}
      throw new Error(
        `ExaSearchProvider.search: ${response.status} ${response.statusText}${
          body ? ` — ${body.slice(0, 500)}` : ""
        }`
      );
    }

    const raw = await response.json();
    return {
      results: ExaSearchProvider._normalizeResults(raw),
      raw,
    };
  }

  /**
   * Maps the raw MLPA/Exa response into the normalized SearchResult shape.
   *
   * The /v1/search OpenAPI spec leaves the 200 response schema open, so
   * results are extracted defensively: snippet text falls back across
   * common Exa field names (`text`, `snippet`, `summary`).
   *
   * @param {object} raw - Parsed JSON body of the MLPA response.
   * @returns {SearchResult[]} Normalized list, possibly empty.
   */
  static _normalizeResults(raw) {
    const list = Array.isArray(raw?.results) ? raw.results : [];
    const normalized = [];
    for (const item of list) {
      if (!item || typeof item !== "object") {
        continue;
      }
      const url = typeof item.url === "string" ? item.url : "";
      if (!url) {
        continue;
      }
      const title = typeof item.title === "string" ? item.title : "";
      const snippet = ExaSearchProvider._extractSnippet(item);
      const result = { title, url, snippet };
      if (typeof item.publishedDate === "string" && item.publishedDate) {
        result.publishedDate = item.publishedDate;
      }
      normalized.push(result);
    }
    return normalized;
  }

  /**
   * Extracts a snippet string from a single result item, falling back
   * across the common Exa field names since the MLPA OpenAPI spec does
   * not pin the response shape.
   *
   * @param {object} item - One entry from the raw `results` array.
   * @returns {string} Snippet text, or "" when no usable field exists.
   */
  static _extractSnippet(item) {
    for (const key of ["text", "snippet", "summary"]) {
      if (typeof item[key] === "string" && item[key]) {
        return item[key];
      }
    }
    return "";
  }
}
