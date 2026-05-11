/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import {OpenedConnection} from "resource://gre/modules/Sqlite.sys.mjs"
 * @import {Query} from "./UrlbarProvidersManager.sys.mjs"
 */

/**
 * This module exports a provider that provides an autofill result.
 */

import {
  UrlbarProvider,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutPagesUtils: "resource://gre/modules/AboutPagesUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "moz-src:///browser/components/urlbar/UrlbarResult.sys.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "pageFrecencyThreshold", () => {
  return lazy.PlacesUtils.history.pageFrecencyThreshold(90, 0, true);
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "historyEnabled",
  "places.history.enabled",
  true
);

// Returns which result sources are eligible for autofill. When
// places.history.enabled is false the user has opted out of recording
// browsing history, so we treat HISTORY as unavailable and route the
// query through the bookmarks-only path.
function effectiveSources(queryContext) {
  return {
    historyAllowed:
      queryContext.sources.includes(UrlbarUtils.RESULT_SOURCE.HISTORY) &&
      lazy.historyEnabled,
    bookmarksAllowed: queryContext.sources.includes(
      UrlbarUtils.RESULT_SOURCE.BOOKMARKS
    ),
  };
}

// AutoComplete query type constants.
// Describes the various types of queries that we can process rows for.
const QUERYTYPE = {
  AUTOFILL_ORIGIN: 1,
  AUTOFILL_URL: 2,
  AUTOFILL_ADAPTIVE: 3,
};

const RESULT_MENU_COMMANDS = {
  DISMISS: "dismiss",
  // In telemetry, we'll still report this as dismiss
  DISMISS_AUTOFILL: "dismiss_autofill",
};

// Constants to support an alternative frecency algorithm.
const ORIGIN_USE_ALT_FRECENCY = Services.prefs.getBoolPref(
  "places.frecency.origins.alternative.featureGate",
  false
);
const ORIGIN_FRECENCY_FIELD = ORIGIN_USE_ALT_FRECENCY
  ? "alt_frecency"
  : "frecency";

// `WITH` clause for the autofill queries.
// A NULL frecency is normalized to 1.0, because the table doesn't support NULL.
// Because of that, here we must set a minimum threshold of 2.0, otherwise when
// all the visits are older than the cutoff, we'd check
// 0.0 (frecency) >= 0.0 (threshold) and autofill everything instead of nothing.
const SQL_AUTOFILL_WITH = ORIGIN_USE_ALT_FRECENCY
  ? `
    WITH
    autofill_frecency_threshold(value) AS (
      SELECT IFNULL(
        (SELECT value FROM moz_meta WHERE key = 'origin_alt_frecency_threshold'),
        2.0
      )
    )
    `
  : `
    WITH
    autofill_frecency_threshold(value) AS (
      SELECT IFNULL(
        (SELECT value FROM moz_meta WHERE key = 'origin_frecency_threshold'),
        2.0
      )
    )
  `;

const SQL_AUTOFILL_FRECENCY_THRESHOLD = `total_fixed_host_frecency >= (
    SELECT value FROM autofill_frecency_threshold
  )`;

function originQuery(where, { preferHttps = false } = {}) {
  // `frecency`, `n_bookmarks` and `visited` are partitioned by the fixed host,
  // without `www.`. `host_prefix` instead is partitioned by full host, because
  // we assume a prefix may not work regardless of `www.`.
  let selectAnyRecentTyped = where.includes("any_recent_typed")
    ? `MAX(${ORIGIN_FRECENCY_FIELD} > 1) OVER (PARTITION BY fixup_url(host))`
    : "0";
  let selectTitle;
  let joinBookmarks;
  if (where.includes("n_bookmarks")) {
    selectTitle = "ifnull(b.title, iif(h.frecency <> 0, h.title, NULL))";
    joinBookmarks = "LEFT JOIN moz_bookmarks b ON b.fk = h.id";
  } else {
    selectTitle = "iif(h.frecency <> 0, h.title, NULL)";
    joinBookmarks = "";
  }

  let hostPrefixOrder = preferHttps
    ? `prefix = "https://" DESC, ${ORIGIN_FRECENCY_FIELD} DESC, id DESC`
    : `${ORIGIN_FRECENCY_FIELD} DESC, prefix = "https://" DESC, id DESC`;

  return `/* do not warn (bug no): cannot use an index to sort */
    ${SQL_AUTOFILL_WITH},
    origins(
      id,
      prefix,
      host_prefix,
      host,
      fixed,
      total_fixed_host_frecency,
      frecency,
      n_bookmarks,
      any_recent_typed
    ) AS (
      SELECT
      id,
      prefix,
      first_value(prefix) OVER (
        PARTITION BY host
        ORDER BY ${hostPrefixOrder}
      ),
      host,
      fixup_url(host),
      total(${ORIGIN_FRECENCY_FIELD}) OVER (PARTITION BY fixup_url(host)),
      ${ORIGIN_FRECENCY_FIELD},
      total(
        (SELECT total(foreign_count) FROM moz_places WHERE origin_id = o.id)
      ) OVER (PARTITION BY fixup_url(host)),
      ${selectAnyRecentTyped}
      FROM moz_origins o
      WHERE prefix NOT IN ('about:', 'place:')
        AND ((host BETWEEN :searchString AND :searchString || X'FFFF')
          OR (host BETWEEN 'www.' || :searchString AND 'www.' || :searchString || X'FFFF'))
        AND (:adaptiveAutofillEnabled = 0 OR o.block_until_ms IS NULL OR o.block_until_ms <= :nowMs)
    ),
    matched_origin(host_fixed, url) AS (
      SELECT iif(instr(host, :searchString) = 1, host, fixed) || '/',
             ifnull(:prefix, host_prefix) || host || '/'
      FROM origins
      ${where}
      ORDER BY
        total_fixed_host_frecency DESC,
        frecency DESC,
        n_bookmarks DESC,
        prefix = "https://" DESC,
        id DESC
      LIMIT 1
    ),
    matched_place(host_fixed, url, id, title, frecency) AS (
      SELECT o.host_fixed, o.url, h.id, h.title, h.frecency
      FROM matched_origin o
      LEFT JOIN moz_places h ON h.url_hash IN (
        hash('https://' || o.host_fixed),
        hash('https://www.' || o.host_fixed),
        hash('http://' || o.host_fixed),
        hash('http://www.' || o.host_fixed)
      )
      ORDER BY
        h.title IS NOT NULL DESC,
        h.title || '/' <> o.host_fixed DESC,
        h.url = o.url DESC,
        h.frecency DESC,
        h.id DESC
      LIMIT 1
    )
    SELECT :query_type AS query_type,
           :searchString AS search_string,
           h.host_fixed AS host_fixed,
           h.url AS url,
           ${selectTitle} AS title
    FROM matched_place h
    ${joinBookmarks}
  `;
}

function urlQuery(where1, where2, isBookmarkContained) {
  // We limit the search to places that are either bookmarked or have a frecency
  // over some small, arbitrary threshold in order to avoid scanning as few
  // rows as possible.  Keep in mind that we run this query every time the user
  // types a key when the urlbar value looks like a URL with a path.
  let selectTitle;
  let joinBookmarks;
  if (isBookmarkContained) {
    selectTitle = "ifnull(b.title, matched_url.title)";
    joinBookmarks = "LEFT JOIN moz_bookmarks b ON b.fk = matched_url.id";
  } else {
    selectTitle = "matched_url.title";
    joinBookmarks = "";
  }
  return `/* do not warn (bug no): cannot use an index to sort */
    WITH matched_url(url, title, frecency, n_bookmarks, visited, stripped_url, is_exact_match, id) AS (
      SELECT url,
             title,
             frecency,
             foreign_count AS n_bookmarks,
             visit_count > 0 AS visited,
             strip_prefix_and_userinfo(url) AS stripped_url,
             strip_prefix_and_userinfo(url) = strip_prefix_and_userinfo(:strippedURL) AS is_exact_match,
             id
      FROM moz_places
      WHERE rev_host = :revHost
            ${where1}
      UNION ALL
      SELECT url,
             title,
             frecency,
             foreign_count AS n_bookmarks,
             visit_count > 0 AS visited,
             strip_prefix_and_userinfo(url) AS stripped_url,
             strip_prefix_and_userinfo(url) = 'www.' || strip_prefix_and_userinfo(:strippedURL) AS is_exact_match,
             id
      FROM moz_places
      WHERE rev_host = :revHost || 'www.'
            ${where2}
      ORDER BY is_exact_match DESC, frecency DESC, id DESC
      LIMIT 1
    )
    SELECT :query_type AS query_type,
           :searchString AS search_string,
           :strippedURL AS stripped_url,
           matched_url.url AS url,
           ${selectTitle} AS title
    FROM matched_url
    ${joinBookmarks}
  `;
}

// Queries
const QUERY_ORIGIN_HISTORY_BOOKMARK = originQuery(
  `WHERE (:adaptiveAutofillEnabled = 0 AND n_bookmarks > 0)
     OR (any_recent_typed AND ${SQL_AUTOFILL_FRECENCY_THRESHOLD})`,
  { preferHttps: true }
);

const QUERY_ORIGIN_PREFIX_HISTORY_BOOKMARK = originQuery(
  `WHERE prefix BETWEEN :prefix AND :prefix || X'FFFF'
     AND ((:adaptiveAutofillEnabled = 0 AND n_bookmarks > 0)
       OR (any_recent_typed AND ${SQL_AUTOFILL_FRECENCY_THRESHOLD}))`,
  { preferHttps: true }
);

const QUERY_ORIGIN_HISTORY = originQuery(
  `WHERE any_recent_typed AND ${SQL_AUTOFILL_FRECENCY_THRESHOLD}`,
  { preferHttps: true }
);

const QUERY_ORIGIN_PREFIX_HISTORY = originQuery(
  `WHERE prefix BETWEEN :prefix AND :prefix || X'FFFF'
     AND any_recent_typed AND ${SQL_AUTOFILL_FRECENCY_THRESHOLD}`,
  { preferHttps: true }
);

const QUERY_ORIGIN_BOOKMARK = originQuery(`WHERE n_bookmarks > 0`);

const QUERY_ORIGIN_PREFIX_BOOKMARK = originQuery(
  `WHERE prefix BETWEEN :prefix AND :prefix || X'FFFF' AND n_bookmarks > 0`
);

const QUERY_URL_HISTORY_BOOKMARK = urlQuery(
  `AND ((:adaptiveAutofillEnabled = 0 AND n_bookmarks > 0)
        OR frecency > :pageFrecencyThreshold)
     AND stripped_url COLLATE NOCASE
       BETWEEN :strippedURL AND :strippedURL || X'FFFF'`,
  `AND ((:adaptiveAutofillEnabled = 0 AND n_bookmarks > 0)
        OR frecency > :pageFrecencyThreshold)
     AND stripped_url COLLATE NOCASE
       BETWEEN 'www.' || :strippedURL AND 'www.' || :strippedURL || X'FFFF'`,
  true
);

const QUERY_URL_PREFIX_HISTORY_BOOKMARK = urlQuery(
  `AND ((:adaptiveAutofillEnabled = 0 AND n_bookmarks > 0)
        OR frecency > :pageFrecencyThreshold)
     AND url COLLATE NOCASE
       BETWEEN :prefix || :strippedURL AND :prefix || :strippedURL || X'FFFF'`,
  `AND ((:adaptiveAutofillEnabled = 0 AND n_bookmarks > 0)
        OR frecency > :pageFrecencyThreshold)
     AND url COLLATE NOCASE
       BETWEEN :prefix || 'www.' || :strippedURL AND :prefix || 'www.' || :strippedURL || X'FFFF'`,
  true
);

const QUERY_URL_HISTORY = urlQuery(
  `AND (visited OR n_bookmarks = 0)
     AND frecency > :pageFrecencyThreshold
     AND stripped_url COLLATE NOCASE
       BETWEEN :strippedURL AND :strippedURL || X'FFFF'`,
  `AND (visited OR n_bookmarks = 0)
     AND frecency > :pageFrecencyThreshold
     AND stripped_url COLLATE NOCASE
       BETWEEN 'www.' || :strippedURL AND 'www.' || :strippedURL || X'FFFF'`,
  false
);

const QUERY_URL_PREFIX_HISTORY = urlQuery(
  `AND (visited OR n_bookmarks = 0)
     AND frecency > :pageFrecencyThreshold
     AND url COLLATE NOCASE
       BETWEEN :prefix || :strippedURL AND :prefix || :strippedURL || X'FFFF'`,
  `AND (visited OR n_bookmarks = 0)
     AND frecency > :pageFrecencyThreshold
     AND url COLLATE NOCASE
       BETWEEN :prefix || 'www.' || :strippedURL AND :prefix || 'www.' || :strippedURL || X'FFFF'`,
  false
);

const QUERY_URL_BOOKMARK = urlQuery(
  `AND n_bookmarks > 0
     AND stripped_url COLLATE NOCASE
       BETWEEN :strippedURL AND :strippedURL || X'FFFF'`,
  `AND n_bookmarks > 0
     AND stripped_url COLLATE NOCASE
       BETWEEN 'www.' || :strippedURL AND 'www.' || :strippedURL || X'FFFF'`,
  true
);

const QUERY_URL_PREFIX_BOOKMARK = urlQuery(
  `AND n_bookmarks > 0
     AND url COLLATE NOCASE
       BETWEEN :prefix || :strippedURL AND :prefix || :strippedURL || X'FFFF'`,
  `AND n_bookmarks > 0
     AND url COLLATE NOCASE
       BETWEEN :prefix || 'www.' || :strippedURL AND :prefix || 'www.' || :strippedURL || X'FFFF'`,
  true
);

/**
 * @typedef AutofillData
 *
 * @property {UrlbarResult} result
 *   The result entry.
 * @property {UrlbarResult} [fallbackResult]
 *   An optional origin fallback result when the primary result is a deep URL.
 * @property {Query} instance
 *   The query instance.
 */

/**
 * Class used to create the provider.
 */
export class UrlbarProviderAutofill extends UrlbarProvider {
  /**
   * This is usually reset on canceling or completing the query, but since we
   * query in isActive, it may not have been canceled by the previous call.
   *
   * @type {?AutofillData}
   */
  _autofillData = null;
  constructor() {
    super();
  }

  /**
   * @returns {Values<typeof UrlbarUtils.PROVIDER_TYPE>}
   */
  get type() {
    return UrlbarUtils.PROVIDER_TYPE.HEURISTIC;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    let instance = this.queryInstance;

    // This is usually reset on canceling or completing the query, but since we
    // query in isActive, it may not have been canceled by the previous call.
    this._autofillData = null;

    // First of all, check for the autoFill pref.
    if (!lazy.UrlbarPrefs.get("autoFill")) {
      return false;
    }

    if (!queryContext.allowAutofill) {
      return false;
    }

    if (queryContext.tokens.length != 1) {
      return false;
    }

    // Trying to autofill an extremely long string would be expensive, and
    // not particularly useful since the filled part falls out of screen anyway.
    if (queryContext.searchString.length > UrlbarUtils.MAX_TEXT_LENGTH) {
      return false;
    }

    // autoFill can only cope with history, bookmarks, and about: entries.
    if (
      !queryContext.sources.includes(UrlbarUtils.RESULT_SOURCE.HISTORY) &&
      !queryContext.sources.includes(UrlbarUtils.RESULT_SOURCE.BOOKMARKS)
    ) {
      return false;
    }

    // Autofill doesn't search tags or titles
    if (
      queryContext.tokens.some(
        t =>
          t.type == lazy.UrlbarTokenizer.TYPE.RESTRICT_TAG ||
          t.type == lazy.UrlbarTokenizer.TYPE.RESTRICT_TITLE
      )
    ) {
      return false;
    }

    [this._strippedPrefix, this._searchString] = UrlbarUtils.stripURLPrefix(
      queryContext.searchString
    );
    this._strippedPrefix = this._strippedPrefix.toLowerCase();

    // Don't try to autofill if the search term includes any whitespace.
    // This may confuse completeDefaultIndex cause the AUTOCOMPLETE_MATCH
    // tokenizer ends up trimming the search string and returning a value
    // that doesn't match it, or is even shorter.
    if (lazy.UrlUtils.REGEXP_SPACES.test(queryContext.searchString)) {
      return false;
    }

    // Fetch autofill result now, rather than in startQuery. We do this so the
    // muxer doesn't have to wait on autofill for every query, since startQuery
    // will be guaranteed to return a result very quickly using this approach.
    // Bug 1651101 is filed to improve this behaviour.
    let result = await this._getAutofillResult(queryContext);
    if (!result || instance != this.queryInstance) {
      return false;
    }
    this._autofillData = { ...result, instance };
    return true;
  }

  /**
   * Gets the provider's priority.
   *
   * @returns {number} The provider's priority for the given query.
   */
  getPriority() {
    return 0;
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    // Check if the query was cancelled while the autofill result was being
    // fetched. We don't expect this to be true since we also check the instance
    // in isActive and clear _autofillData in cancelQuery, but we sanity check it.
    if (
      !this._autofillData ||
      this._autofillData.instance != this.queryInstance
    ) {
      this.logger.error("startQuery invoked with an invalid _autofillData");
      return;
    }

    addCallback(this, this._autofillData.result);
    if (this._autofillData.fallbackResult) {
      addCallback(this, this._autofillData.fallbackResult);
    }
    this._autofillData = null;
  }

  /**
   * Cancels a running query.
   */
  cancelQuery() {
    if (this._autofillData?.instance == this.queryInstance) {
      this._autofillData = null;
    }
  }

  async onEngagement(queryContext, controller, details) {
    let { result } = details;
    let didRemove = false;

    switch (details.selType) {
      case RESULT_MENU_COMMANDS.DISMISS: {
        await lazy.PlacesUtils.history
          .remove(result.payload.url)
          .catch(console.error);
        didRemove = true;
        break;
      }
      case RESULT_MENU_COMMANDS.DISMISS_AUTOFILL: {
        let blockUntilMs =
          Date.now() +
          lazy.UrlbarPrefs.get("autoFill.dismissalBlockDurationMs");
        await UrlbarUtils.blockAutofill(result.payload.url, blockUntilMs).catch(
          console.error
        );
        didRemove = true;
        break;
      }
    }

    if (didRemove) {
      // Upon removing the autofill, we should do another search.
      controller.input._setValue(queryContext.searchString);
      controller.input.startQuery({
        searchString: queryContext.searchString,
        allowAutofill: false,
        resetSearchState: false,
      });
    }
  }

  getResultCommands(result, isPrivate) {
    if (
      !result.autofill ||
      !lazy.UrlbarPrefs.get("autoFill.adaptiveHistory.enabled")
    ) {
      return undefined;
    }
    if (
      result.autofill.type === "adaptive_url" ||
      result.autofill.type === "adaptive_origin" ||
      result.autofill.type === "origin"
    ) {
      let isOrigin = UrlbarUtils.isOriginUrl(result.payload.url);
      let resultArray = [];

      if (!isPrivate) {
        resultArray.push({
          name: RESULT_MENU_COMMANDS.DISMISS_AUTOFILL,
          l10n: {
            id: "urlbar-result-menu-dismiss-suggestion",
          },
        });
      }

      // For non-origin URLs, include the ability to remove it from history.
      if (!isOrigin) {
        resultArray.push({
          name: RESULT_MENU_COMMANDS.DISMISS,
          l10n: {
            id: "urlbar-result-menu-remove-from-history",
          },
        });
      }

      return resultArray.length ? resultArray : undefined;
    }
    return undefined;
  }

  /**
   * Filters hosts by retaining only the ones over the autofill threshold, then
   * sorts them by their frecency, and extracts the one with the highest value.
   *
   * @param {UrlbarQueryContext} queryContext The current queryContext.
   * @param {Array} hosts Array of host names to examine.
   * @returns {Promise<string?>}
   *   Resolved when the filtering is complete. Resolves with the top matching
   *   host, or null if not found.
   */
  static async getTopHostOverThreshold(queryContext, hosts) {
    let db = await lazy.PlacesUtils.promiseLargeCacheDBConnection();
    let conditions = [];
    // Pay attention to the order of params, since they are not named.
    let params = [...hosts];
    let sources = queryContext.sources;
    if (
      sources.includes(UrlbarUtils.RESULT_SOURCE.HISTORY) &&
      sources.includes(UrlbarUtils.RESULT_SOURCE.BOOKMARKS)
    ) {
      conditions.push(
        `(n_bookmarks > 0 OR ${SQL_AUTOFILL_FRECENCY_THRESHOLD})`
      );
    } else if (sources.includes(UrlbarUtils.RESULT_SOURCE.HISTORY)) {
      conditions.push(`visited AND ${SQL_AUTOFILL_FRECENCY_THRESHOLD}`);
    } else if (sources.includes(UrlbarUtils.RESULT_SOURCE.BOOKMARKS)) {
      conditions.push("n_bookmarks > 0");
    }

    let rows = await db.executeCached(
      `
        ${SQL_AUTOFILL_WITH},
        origins(id, prefix, host_prefix, host, fixed, total_fixed_host_frecency, frecency, n_bookmarks, visited) AS (
          SELECT
          id,
          prefix,
          first_value(prefix) OVER (
            PARTITION BY host ORDER BY ${ORIGIN_FRECENCY_FIELD} DESC, prefix = "https://" DESC, id DESC
          ),
          host,
          fixup_url(host),
          total(${ORIGIN_FRECENCY_FIELD}) OVER (PARTITION BY fixup_url(host)),
          ${ORIGIN_FRECENCY_FIELD},
          total(
            (SELECT total(foreign_count) FROM moz_places WHERE origin_id = o.id)
          ) OVER (PARTITION BY fixup_url(host)),
          MAX(EXISTS(
            SELECT 1 FROM moz_places WHERE origin_id = o.id AND visit_count > 0
          )) OVER (PARTITION BY fixup_url(host))
          FROM moz_origins o
          WHERE o.host IN (${new Array(hosts.length).fill("?").join(",")})
        )
        SELECT host
        FROM origins
        ${conditions.length ? "WHERE " + conditions.join(" AND ") : ""}
        ORDER BY frecency DESC, prefix = "https://" DESC, id DESC
        LIMIT 1
      `,
      params
    );
    if (!rows.length) {
      return null;
    }
    return rows[0].getResultByName("host");
  }

  /**
   * @type {string}
   *   The search string with the prefix stripped.
   */
  _searchString;

  /**
   * Obtains the query to search for autofill origin results.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The current queryContext.
   * @returns {Array} consisting of the correctly optimized query to search the
   *         database with and an object containing the params to bound.
   */
  _getOriginQuery(queryContext) {
    // At this point, searchString is not a URL with a path; it does not
    // contain a slash, except for possibly at the very end.  If there is
    // trailing slash, remove it when searching here to match the rest of the
    // string because it may be an origin.
    let searchStr = this._searchString.endsWith("/")
      ? this._searchString.slice(0, -1)
      : this._searchString;

    let opts = {
      query_type: QUERYTYPE.AUTOFILL_ORIGIN,
      searchString: searchStr.toLowerCase(),
      nowMs: Date.now(),
      adaptiveAutofillEnabled: lazy.UrlbarPrefs.get(
        "autoFillAdaptiveHistoryEnabled"
      )
        ? 1
        : 0,
    };
    if (this._strippedPrefix) {
      opts.prefix = this._strippedPrefix;
    }

    let { historyAllowed, bookmarksAllowed } = effectiveSources(queryContext);

    if (historyAllowed && bookmarksAllowed) {
      return [
        this._strippedPrefix
          ? QUERY_ORIGIN_PREFIX_HISTORY_BOOKMARK
          : QUERY_ORIGIN_HISTORY_BOOKMARK,
        opts,
      ];
    }
    if (historyAllowed) {
      return [
        this._strippedPrefix
          ? QUERY_ORIGIN_PREFIX_HISTORY
          : QUERY_ORIGIN_HISTORY,
        opts,
      ];
    }
    if (bookmarksAllowed) {
      return [
        this._strippedPrefix
          ? QUERY_ORIGIN_PREFIX_BOOKMARK
          : QUERY_ORIGIN_BOOKMARK,
        opts,
      ];
    }
    throw new Error("Either history or bookmark behavior expected");
  }

  /**
   * Obtains the query to search for autoFill url results.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The current queryContext.
   * @returns {Array} consisting of the correctly optimized query to search the
   *         database with and an object containing the params to bound.
   */
  _getUrlQuery(queryContext) {
    // Try to get the host from the search string.  The host is the part of the
    // URL up to either the path slash, port colon, or query "?".  If the search
    // string doesn't look like it begins with a host, then return; it doesn't
    // make sense to do a URL query with it.
    const urlQueryHostRegexp = /^[^/:?]+/;
    let hostMatch = urlQueryHostRegexp.exec(this._searchString);
    if (!hostMatch) {
      return [null, null];
    }

    let host = hostMatch[0].toLowerCase();
    let revHost = host.split("").reverse().join("") + ".";

    // Build a string that's the URL stripped of its prefix, i.e., the host plus
    // everything after.  Use queryContext.trimmedSearchString instead of
    // this._searchString because this._searchString has had unEscapeURIForUI()
    // called on it.  It's therefore not necessarily the literal URL.
    let strippedURL = queryContext.trimmedSearchString;
    if (this._strippedPrefix) {
      strippedURL = strippedURL.substr(this._strippedPrefix.length);
    }
    strippedURL = host + strippedURL.substr(host.length);

    let opts = {
      query_type: QUERYTYPE.AUTOFILL_URL,
      searchString: this._searchString,
      revHost,
      strippedURL,
    };
    if (this._strippedPrefix) {
      opts.prefix = this._strippedPrefix;
    }

    let { historyAllowed, bookmarksAllowed } = effectiveSources(queryContext);

    if (historyAllowed && bookmarksAllowed) {
      opts.pageFrecencyThreshold = lazy.pageFrecencyThreshold;
      opts.adaptiveAutofillEnabled = lazy.UrlbarPrefs.get(
        "autoFillAdaptiveHistoryEnabled"
      )
        ? 1
        : 0;
      return [
        this._strippedPrefix
          ? QUERY_URL_PREFIX_HISTORY_BOOKMARK
          : QUERY_URL_HISTORY_BOOKMARK,
        opts,
      ];
    }
    if (historyAllowed) {
      opts.pageFrecencyThreshold = lazy.pageFrecencyThreshold;
      return [
        this._strippedPrefix ? QUERY_URL_PREFIX_HISTORY : QUERY_URL_HISTORY,
        opts,
      ];
    }
    if (bookmarksAllowed) {
      return [
        this._strippedPrefix ? QUERY_URL_PREFIX_BOOKMARK : QUERY_URL_BOOKMARK,
        opts,
      ];
    }
    throw new Error("Either history or bookmark behavior expected");
  }

  _getAdaptiveHistoryQuery(queryContext) {
    let { historyAllowed, bookmarksAllowed } = effectiveSources(queryContext);

    let sourceCondition;
    let params = {};
    if (historyAllowed && bookmarksAllowed) {
      sourceCondition =
        "((:adaptiveAutofillEnabled = 0 AND h.foreign_count > 0) OR h.frecency > :pageFrecencyThreshold)";
      params.pageFrecencyThreshold = lazy.pageFrecencyThreshold;
    } else if (historyAllowed) {
      sourceCondition =
        "((h.visit_count > 0 OR h.foreign_count = 0) AND h.frecency > :pageFrecencyThreshold)";
      params.pageFrecencyThreshold = lazy.pageFrecencyThreshold;
    } else if (bookmarksAllowed) {
      sourceCondition = "h.foreign_count > 0";
    } else {
      return [];
    }

    let selectTitle;
    let joinBookmarks;
    if (UrlbarUtils.RESULT_SOURCE.BOOKMARKS) {
      selectTitle = "ifnull(b.title, matched.title)";
      joinBookmarks = "LEFT JOIN moz_bookmarks b ON b.fk = matched.id";
    } else {
      selectTitle = "matched.title";
      joinBookmarks = "";
    }

    params = Object.assign(params, {
      queryType: QUERYTYPE.AUTOFILL_ADAPTIVE,
      // `fullSearchString` is the value the user typed including a prefix if
      // they typed one. `searchString` has been stripped of the prefix.
      fullSearchString: queryContext.lowerCaseSearchString,
      searchString: this._searchString,
      strippedPrefix: this._strippedPrefix,
      useCountThreshold: lazy.UrlbarPrefs.get(
        "autoFillAdaptiveHistoryUseCountThreshold"
      ),
      nowMs: Date.now(),
      adaptiveAutofillEnabled: lazy.UrlbarPrefs.get(
        "autoFillAdaptiveHistoryEnabled"
      )
        ? 1
        : 0,
    });

    const query = `
      WITH matched(input, url, title, stripped_url, is_exact_match, starts_with, id) AS (
        SELECT
          i.input AS input,
          h.url AS url,
          h.title AS title,
          strip_prefix_and_userinfo(h.url) AS stripped_url,
          strip_prefix_and_userinfo(h.url) = :searchString AS is_exact_match,
          (strip_prefix_and_userinfo(h.url) COLLATE NOCASE BETWEEN :searchString AND :searchString || X'FFFF') AS starts_with,
          h.id AS id
        FROM moz_places h
        JOIN moz_inputhistory i ON i.place_id = h.id
        JOIN moz_origins o ON o.id = h.origin_id
        WHERE LENGTH(i.input) != 0
          AND :fullSearchString BETWEEN i.input AND i.input || X'FFFF'
          AND ${sourceCondition}
          AND i.use_count >= :useCountThreshold
          AND (:strippedPrefix = '' OR get_prefix(h.url) = :strippedPrefix)
          AND (
            starts_with OR
            (stripped_url COLLATE NOCASE BETWEEN 'www.' || :searchString AND 'www.' || :searchString || X'FFFF')
          )
          AND (:adaptiveAutofillEnabled = 0 OR o.block_until_ms IS NULL OR o.block_until_ms <= :nowMs
            OR fixup_url(h.url) != fixup_url(o.host) || '/')
          AND (:adaptiveAutofillEnabled = 0 OR o.block_pages_until_ms IS NULL OR o.block_pages_until_ms <= :nowMs
            OR fixup_url(h.url) = fixup_url(o.host) || '/')
        ORDER BY is_exact_match DESC, i.use_count DESC, h.frecency DESC, h.id DESC
        LIMIT 1
      )
      SELECT
        :queryType AS query_type,
        :searchString AS search_string,
        input,
        url,
        iif(starts_with, stripped_url, fixup_url(stripped_url)) AS url_fixed,
        ${selectTitle} AS title,
        stripped_url
      FROM matched
      ${joinBookmarks}
    `;

    return [query, params];
  }

  /**
   * Processes a matched row in the Places database.
   *
   * @param {object} row
   *   The matched row.
   * @param {UrlbarQueryContext} queryContext
   *   The query context.
   * @returns {UrlbarResult} a result generated from the matches row.
   */
  _processRow(row, queryContext) {
    let queryType = row.getResultByName("query_type");
    let title = row.getResultByName("title");

    // `searchString` is `this._searchString` or derived from it. It is
    // stripped, meaning the prefix (the URL protocol) has been removed.
    let searchString = row.getResultByName("search_string");

    // `fixedURL` is the part of the matching stripped URL that starts with the
    // stripped search string. The important point here is "www" handling. If a
    // stripped URL starts with "www", we allow the user to omit the "www" and
    // still match it. So if the matching stripped URL starts with "www" but the
    // stripped search string does not, `fixedURL` will also omit the "www".
    // Otherwise `fixedURL` will be equivalent to the matching stripped URL.
    //
    // Example 1:
    //   stripped URL: www.example.com/
    //   searchString: exam
    //   fixedURL: example.com/
    // Example 2:
    //   stripped URL: www.example.com/
    //   searchString: www.exam
    //   fixedURL: www.example.com/
    // Example 3:
    //   stripped URL: example.com/
    //   searchString: exam
    //   fixedURL: example.com/
    let fixedURL;

    // `finalCompleteValue` will be the UrlbarResult's URL. If the matching
    // stripped URL starts with "www" but the user omitted it,
    // `finalCompleteValue` will include it to properly reflect the real URL.
    let finalCompleteValue;

    let autofilledType;
    let adaptiveHistoryInput;

    switch (queryType) {
      case QUERYTYPE.AUTOFILL_ORIGIN: {
        fixedURL = row.getResultByName("host_fixed");
        finalCompleteValue = row.getResultByName("url");
        autofilledType = "origin";
        break;
      }
      case QUERYTYPE.AUTOFILL_URL: {
        let url = row.getResultByName("url");
        let strippedURL = row.getResultByName("stripped_url");

        if (!UrlbarUtils.canAutofillURL(url, strippedURL, true)) {
          return null;
        }

        // We autofill urls to-the-next-slash.
        // http://mozilla.org/foo/bar/baz will be autofilled to:
        //  - http://mozilla.org/f[oo/]
        //  - http://mozilla.org/foo/b[ar/]
        //  - http://mozilla.org/foo/bar/b[az]
        // And, toLowerCase() is preferred over toLocaleLowerCase() here
        // because "COLLATE NOCASE" in the SQL only handles ASCII characters.
        let strippedURLIndex = url
          .toLowerCase()
          .indexOf(strippedURL.toLowerCase());
        let strippedPrefix = url.substr(0, strippedURLIndex);
        let nextSlashIndex = url.indexOf(
          "/",
          strippedURLIndex + strippedURL.length - 1
        );
        fixedURL =
          nextSlashIndex < 0
            ? url.substr(strippedURLIndex)
            : url.substring(strippedURLIndex, nextSlashIndex + 1);
        finalCompleteValue = strippedPrefix + fixedURL;
        if (finalCompleteValue !== url) {
          title = null;
        }
        autofilledType = "url";
        break;
      }
      case QUERYTYPE.AUTOFILL_ADAPTIVE: {
        adaptiveHistoryInput = row.getResultByName("input");
        fixedURL = row.getResultByName("url_fixed");
        finalCompleteValue = row.getResultByName("url");
        autofilledType = UrlbarUtils.isOriginUrl(finalCompleteValue)
          ? "adaptive_origin"
          : "adaptive_url";
        break;
      }
    }

    // Compute `autofilledValue`, the full value that will be placed in the
    // input. It includes two parts: the part the user already typed in the
    // character case they typed it (`queryContext.searchString`), and the
    // autofilled part, which is the portion of the fixed URL starting after the
    // stripped search string.
    let autofilledValue =
      queryContext.searchString + fixedURL.substring(searchString.length);

    // If more than an origin was autofilled and the user typed the full
    // autofilled value, override the final URL by using the exact value the
    // user typed. This allows the user to visit a URL that differs from the
    // autofilled URL only in character case (for example "wikipedia.org/RAID"
    // vs. "wikipedia.org/Raid") by typing the full desired URL.
    if (
      queryType != QUERYTYPE.AUTOFILL_ORIGIN &&
      queryContext.searchString.length == autofilledValue.length
    ) {
      // Use `new URL().href` to lowercase the domain in the final completed
      // URL. This isn't necessary since domains are case insensitive, but it
      // looks nicer because it means the domain will remain lowercased in the
      // input, and it also reflects the fact that Firefox will visit the
      // lowercased name.
      const originalCompleteValue = new URL(finalCompleteValue).href;
      let strippedAutofilledValue = autofilledValue.substring(
        this._strippedPrefix.length
      );
      finalCompleteValue = new URL(
        finalCompleteValue.substring(
          0,
          finalCompleteValue.length - strippedAutofilledValue.length
        ) + strippedAutofilledValue
      ).href;

      // If the character case of except origin part of the original
      // finalCompleteValue differs from finalCompleteValue that includes user's
      // input, we set title null because it expresses different web page.
      if (finalCompleteValue !== originalCompleteValue) {
        title = null;
      }
    }

    let payload = {
      url: finalCompleteValue,
      icon: UrlbarUtils.getIconForUrl(finalCompleteValue),
    };

    let noVisitAction = !!title;
    if (title) {
      payload.title = title;
    } else {
      let trimHttps = lazy.UrlbarPrefs.getScotchBonnetPref("trimHttps");
      let displaySpec = UrlbarUtils.prepareUrlForDisplay(finalCompleteValue, {
        trimURL: false,
      });
      let [fallbackTitle] = UrlbarUtils.stripPrefixAndTrim(displaySpec, {
        stripHttp: !trimHttps,
        stripHttps: trimHttps,
        trimEmptyQuery: true,
        trimSlash: !this._searchString.includes("/"),
      });
      payload.title = fallbackTitle;
    }

    return new lazy.UrlbarResult({
      type: UrlbarUtils.RESULT_TYPE.URL,
      source: UrlbarUtils.RESULT_SOURCE.HISTORY,
      heuristic: true,
      autofill: {
        adaptiveHistoryInput,
        value: autofilledValue,
        selectionStart: queryContext.searchString.length,
        selectionEnd: autofilledValue.length,
        type: autofilledType,
        noVisitAction,
      },
      payload,
      highlights: {
        url: UrlbarUtils.HIGHLIGHT.TYPED,
        title: UrlbarUtils.HIGHLIGHT.TYPED,
        fallbackTitle: UrlbarUtils.HIGHLIGHT.TYPED,
      },
    });
  }

  async _getAutofillResult(queryContext) {
    // We may be autofilling an about: link.
    let result = this._matchAboutPageForAutofill(queryContext);
    if (result) {
      return { result, fallbackResult: null };
    }

    // It may also look like a URL we know from the database.
    return this._matchKnownUrl(queryContext);
  }

  _matchAboutPageForAutofill(queryContext) {
    // Check that the typed query is at least one character longer than the
    // about: prefix.
    if (this._strippedPrefix != "about:" || !this._searchString) {
      return null;
    }

    for (const aboutUrl of lazy.AboutPagesUtils.visibleAboutUrls) {
      if (aboutUrl.startsWith(`about:${this._searchString.toLowerCase()}`)) {
        let [trimmedUrl] = UrlbarUtils.stripPrefixAndTrim(aboutUrl, {
          stripHttp: true,
          trimEmptyQuery: true,
          trimSlash: !this._searchString.includes("/"),
        });
        let autofilledValue =
          queryContext.searchString +
          aboutUrl.substring(queryContext.searchString.length);
        return new lazy.UrlbarResult({
          type: UrlbarUtils.RESULT_TYPE.URL,
          source: UrlbarUtils.RESULT_SOURCE.HISTORY,
          heuristic: true,
          autofill: {
            type: "about",
            value: autofilledValue,
            selectionStart: queryContext.searchString.length,
            selectionEnd: autofilledValue.length,
          },
          payload: {
            title: trimmedUrl,
            url: aboutUrl,
            icon: UrlbarUtils.getIconForUrl(aboutUrl),
          },
          highlights: {
            title: UrlbarUtils.HIGHLIGHT.TYPED,
            url: UrlbarUtils.HIGHLIGHT.TYPED,
          },
        });
      }
    }
    return null;
  }

  async _matchKnownUrl(queryContext) {
    let conn = await lazy.PlacesUtils.promiseLargeCacheDBConnection();
    if (!conn) {
      return null;
    }

    // We try to autofill with adaptive history first.
    if (
      lazy.UrlbarPrefs.get("autoFillAdaptiveHistoryEnabled") &&
      lazy.UrlbarPrefs.get("autoFillAdaptiveHistoryMinCharsThreshold") <=
        queryContext.searchString.length
    ) {
      const [query, params] = this._getAdaptiveHistoryQuery(queryContext);
      if (query) {
        const resultSet = await conn.executeCached(query, params);
        if (resultSet.length) {
          let result = this._processRow(resultSet[0], queryContext);
          if (result) {
            let fallbackResult = await this._getFallbackOriginResult(
              conn,
              result.payload.url
            );
            return { result, fallbackResult };
          }
        }
      }
    }

    // The adaptive history query is passed queryContext.searchString (the full
    // search string), but the origin and URL queries are passed the prefix
    // (this._strippedPrefix) and the rest of the search string
    // (this._searchString) separately. The user must specify a non-prefix part
    // to trigger origin and URL autofill.
    if (!this._searchString.length) {
      return null;
    }

    // If search string looks like an origin, try to autofill against origins.
    // Otherwise treat it as a possible URL.  When the string has only one slash
    // at the end, we still treat it as an URL.
    let query, params;
    if (
      lazy.UrlUtils.looksLikeOrigin(this._searchString, {
        ignoreKnownDomains: true,
        allowPartialNumericalTLDs: true,
      })
    ) {
      [query, params] = this._getOriginQuery(queryContext);
    } else {
      [query, params] = this._getUrlQuery(queryContext);
    }

    // _getUrlQuery doesn't always return a query.
    if (query) {
      let rows = await conn.executeCached(query, params);
      if (rows.length) {
        let result = this._processRow(rows[0], queryContext);
        if (result) {
          return { result, fallbackResult: null };
        }
      }
    }
    return null;
  }

  /**
   * Returns a fallback origin result to accompany a non-origin autofill result.
   *
   * When adaptive autofills to a URL with a path, this method provides an
   * additional origin-only result so the use can navigate directly to the root
   * without modifying the autofilled value.
   *
   * Returns null if the autofill URL is already an origin, if the origin has
   * no Places entry with positive frecency, or if the origin is currently
   * blocked via `moz_origins.block_until_ms`.
   *
   * @param {OpenedConnection} conn
   *   A connection to the Places database.
   * @param {string} autofillUrl
   *   The full URL of the autofill result.
   * @returns {Promise<UrlbarResult?>}
   *   The fallback origin result, or null if no fallback is appropriate.
   */
  async _getFallbackOriginResult(conn, autofillUrl) {
    if (UrlbarUtils.isOriginUrl(autofillUrl)) {
      return null;
    }

    let parsedUrl = URL.parse(autofillUrl);
    if (!parsedUrl) {
      return null;
    }
    let originUrl = parsedUrl.origin + "/";
    let rows = await conn.executeCached(
      `
      SELECT h.title
      FROM moz_places h
      JOIN moz_origins o ON o.id = h.origin_id
      WHERE h.url_hash = hash(:url) AND h.url = :url AND h.frecency > 0
        AND (o.block_until_ms IS NULL OR o.block_until_ms <= :nowMs)
    `,
      { url: originUrl, nowMs: Date.now() }
    );
    if (!rows.length) {
      return null;
    }

    let title = rows[0].getResultByName("title");
    let result = new lazy.UrlbarResult({
      type: UrlbarUtils.RESULT_TYPE.URL,
      source: UrlbarUtils.RESULT_SOURCE.HISTORY,
      payload: {
        url: originUrl,
        title: title ?? originUrl,
        icon: UrlbarUtils.getIconForUrl(originUrl),
        isBlockable: true,
        blockL10n: { id: "urlbar-result-menu-remove-from-history" },
        helpUrl:
          Services.urlFormatter.formatURLPref("app.support.baseURL") +
          "awesome-bar-result-menu",
        isAutofillFallback: true,
      },
    });
    return result;
  }
}
