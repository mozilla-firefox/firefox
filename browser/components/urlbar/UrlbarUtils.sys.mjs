/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports the UrlbarUtils singleton, which contains constants and
 * helper functions that are useful to all components of the urlbar.
 */

/**
 * @import {UrlbarLoadRequest, URIFixupPrimitives} from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 * @import {Query} from "./UrlbarProvidersManager.sys.mjs"
 * @import {SearchEngine} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 * @import {SmartbarInput} from "chrome://browser/content/urlbar/SmartbarInput.mjs"
 */

import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  DEFAULT_FORM_HISTORY_PARAM:
    "moz-src:///toolkit/components/search/SearchSuggestionController.sys.mjs",
  FaviconUtils: "moz-src:///toolkit/modules/FaviconUtils.sys.mjs",
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  KeywordUtils: "resource://gre/modules/KeywordUtils.sys.mjs",
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchEngine: "moz-src:///toolkit/components/search/SearchEngine.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchSuggestionController:
    "moz-src:///toolkit/components/search/SearchSuggestionController.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",

  historyEnabled: {
    pref: "places.history.enabled",
    default: true,
  },
});

/**
 * Parses a URL and returns the origin parts needed for moz_origins lookups.
 * Returns null if the URL is unparseable.
 *
 * @param {string} url
 *   The URL to parse.
 * @returns {{ prefix: string, host: string } | null}
 *   The prefix (scheme + "//") and host, or null if parsing failed.
 */
function parseOriginParts(url) {
  let parsed = URL.parse(url);
  if (!parsed) {
    return null;
  }
  return { prefix: parsed.protocol + "//", host: parsed.host };
}

export var UrlbarUtils = {
  /**
   * Returns the payload schema for the given type of result.
   *
   * @param {Values<typeof UrlbarShared.RESULT_TYPE>} type
   * @returns {object} The schema for the given type.
   */
  getPayloadSchema(type) {
    return this.RESULT_PAYLOAD_SCHEMA[type];
  },

  /**
   * Adds a url to history as long as it isn't in a private browsing window,
   * and it is valid.
   *
   * @param {string} url The url to add to history.
   * @param {nsIDOMWindow} window The window from where the url is being added.
   */
  addToUrlbarHistory(url, window) {
    if (
      !lazy.PrivateBrowsingUtils.isWindowPrivate(window) &&
      url &&
      !url.includes(" ") &&
      // eslint-disable-next-line no-control-regex
      !/[\x00-\x1F]/.test(url)
    ) {
      lazy.PlacesUIUtils.markPageAsTyped(url);
    }
  },

  /**
   * Given a string, will generate a more appropriate urlbar value if a Places
   * keyword or a search alias is found at the beginning of it.
   *
   * @param {string} url
   *        A string that may begin with a keyword or an alias.
   *
   * @returns {Promise<{ url, postData, mayInheritPrincipal }>}
   *        If it's not possible to discern a keyword or an alias, url will be
   *        the input string.
   */
  async getShortcutOrURIAndPostData(url) {
    let mayInheritPrincipal = false;
    let postData = null;
    // Split on the first whitespace.
    let [keyword, param = ""] = url.trim().split(/\s(.+)/, 2);

    if (!keyword) {
      return { url, postData, mayInheritPrincipal };
    }

    /** @type {SearchEngine} */
    let engine = await lazy.SearchService.getEngineByAlias(keyword);
    if (engine) {
      let submission = engine.getSubmission(param, null);
      return {
        url: submission.uri.spec,
        postData: submission.postData,
        mayInheritPrincipal,
      };
    }

    // A corrupt Places database could make this throw, breaking navigation
    // from the location bar.
    let entry = null;
    try {
      entry = await lazy.PlacesUtils.keywords.fetch(keyword);
    } catch (ex) {
      console.error(`Unable to fetch Places keyword "${keyword}":`, ex);
    }
    if (!entry || !entry.url) {
      // This is not a Places keyword.
      return { url, postData, mayInheritPrincipal };
    }

    try {
      [url, postData] = await lazy.KeywordUtils.parseUrlAndPostData(
        entry.url.href,
        entry.postData,
        param
      );
      if (postData) {
        postData = this.getPostDataStream(postData);
      }

      // Since this URL came from a bookmark, it's safe to let it inherit the
      // current document's principal.
      mayInheritPrincipal = true;
    } catch (ex) {
      // It was not possible to bind the param, just use the original url value.
    }

    return { url, postData, mayInheritPrincipal };
  },

  /**
   * Returns an input stream wrapper for the given post data.
   *
   * @param {string} postDataString The string to wrap.
   * @param {string} [type] The encoding type.
   * @returns {nsIInputStream} An input stream of the wrapped post data.
   */
  getPostDataStream(
    postDataString,
    type = "application/x-www-form-urlencoded"
  ) {
    let dataStream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
      Ci.nsIStringInputStream
    );
    dataStream.setByteStringData(postDataString);

    let mimeStream = Cc[
      "@mozilla.org/network/mime-input-stream;1"
    ].createInstance(Ci.nsIMIMEInputStream);
    mimeStream.addHeader("Content-Type", type);
    mimeStream.setData(dataStream);
    return mimeStream.QueryInterface(Ci.nsIInputStream);
  },

  /**
   * Converts nsIInputStream to string. Throws unless the stream is a MIME
   * stream wrapping a string stream, as built by `getPostDataStream` and by
   * search engine submissions.
   *
   * @param {nsIInputStream} postData
   *   The stream to unwrap.
   * @returns {string}
   *  The wrapped post data.
   */
  getPostDataString(postData) {
    return postData
      .QueryInterface(Ci.nsIMIMEInputStream)
      .data.QueryInterface(Ci.nsISupportsCString).data;
  },

  /**
   * Extracts the URL from a result.
   *
   * @param {UrlbarResult} result
   *   The result to extract from.
   * @param {object} options
   *   Options object.
   * @param {HTMLElement} [options.element]
   *   The element associated with the result that was selected or picked, if
   *   available. For results that have multiple selectable children, the URL
   *   may be taken from a child element rather than the result.
   * @returns {{url: ?string, postData: ?nsIInputStream}}
   *   `url` will be null if the result doesn't have a URL. `postData` will be
   *   null if the result doesn't have post data.
   */
  getUrlFromResult(result, { element = null } = {}) {
    let loadRequest = UrlbarShared.getLoadRequestFromResult(result, {
      element,
    });
    if (!loadRequest) {
      return { url: null, postData: null };
    }

    return this.loadRequestToUrl(loadRequest);
  },

  /**
   * Resolves a load request to the url and post data to load.
   *
   * @param {UrlbarLoadRequest} loadRequest
   *   What to load.
   * @returns {{url: ?string, postData: ?nsIInputStream}}
   *   `url` will be null when the search engine wasn't found.
   */
  loadRequestToUrl(loadRequest) {
    if (loadRequest.engineSearch) {
      let { engineName, query } = loadRequest.engineSearch;
      let engine = lazy.SearchService.getEngineByName(engineName);
      if (!engine) {
        return { url: null, postData: null };
      }
      let [url, postData] = this.getSearchQueryUrl(engine, query);
      return { url, postData };
    }

    return {
      url: loadRequest.urlLoad.url,
      postData: loadRequest.urlLoad.postData
        ? this.getPostDataStream(loadRequest.urlLoad.postData)
        : null,
    };
  },

  /**
   * Get the url to load for the search query.
   *
   * @param {SearchEngine} engine
   *   The engine to generate the query for.
   * @param {string} query
   *   The query string to search for.
   * @returns {Array}
   *   Returns an array containing the query url (string) and the
   *    post data (object).
   */
  getSearchQueryUrl(engine, query) {
    let submission = engine.getSubmission(query);
    return [submission.uri.spec, submission.postData];
  },

  /**
   * Ranks a URL prefix from 3 - 0 with the following preferences:
   * https:// > https://www. > http:// > http://www.
   * Higher is better for the purposes of deduping URLs.
   * Returns -1 if the prefix does not match any of the above.
   *
   * @param {string} prefix
   */
  getPrefixRank(prefix) {
    return ["http://www.", "http://", "https://www.", "https://"].indexOf(
      prefix
    );
  },

  /**
   * Converts a given icon URL to a remote icon URL if it's not a trusted
   * protocol.
   *
   * @param {string} iconUrl The URL of the icon.
   * @param {number} size The desired size of the icon (currently ignored).
   * @param {Window} win The window context.
   * @returns {string|null} The URL of the remote icon or null if not available.
   */
  getRemoteIconUrl(iconUrl, size, win) {
    let url = URL.parse(iconUrl);
    if (!url) {
      return null;
    }
    if (!lazy.FaviconUtils.TRUSTED_FAVICON_SCHEMES.includes(url.protocol)) {
      if (Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
        // XPCShell tests don't have a real window, just use fallback values.
        return lazy.FaviconUtils.getMozRemoteImageURL(iconUrl, {
          size,
          colorScheme: "light",
        });
      }
      return lazy.FaviconUtils.getMozRemoteImageURL(iconUrl, {
        // TODO Bug 2035971: Restore the size property once `FaviconUtils` and
        // `moz-remote-image` handle the image aspect ratio correctly.
        //
        // size: Math.floor(size * win.devicePixelRatio),
        colorScheme: win.matchMedia("(prefers-color-scheme: dark)").matches
          ? "dark"
          : "light",
      });
    }
    return iconUrl;
  },

  /**
   * Tries to initiate a speculative connection to a given url.
   *
   * Note: This is not infallible, if a speculative connection cannot be
   *       initialized, it will be a no-op.
   *
   * @param {SearchEngine|nsIURI|URL|string} urlOrEngine
   *   The entity to initiate a speculative connection for.
   * @param {window} window
   *   The window from where the connection is initialized.
   */
  setupSpeculativeConnection(urlOrEngine, window) {
    if (!lazy.UrlbarPrefs.get("speculativeConnect.enabled")) {
      return;
    }
    if (urlOrEngine instanceof lazy.SearchEngine) {
      try {
        urlOrEngine.speculativeConnect({
          window,
          originAttributes: window.gBrowser.contentPrincipal.originAttributes,
        });
      } catch (ex) {
        // Can't setup speculative connection for this url, just ignore it.
      }
      return;
    }

    if (URL.isInstance(urlOrEngine)) {
      urlOrEngine = urlOrEngine.href;
    }

    try {
      let uri =
        urlOrEngine instanceof Ci.nsIURI
          ? urlOrEngine
          : Services.io.newURI(urlOrEngine);
      Services.io.speculativeConnect(
        uri,
        window.gBrowser.contentPrincipal,
        window.docShell.QueryInterface(Ci.nsIInterfaceRequestor),
        false
      );
    } catch (ex) {
      // Can't setup speculative connection for this url, just ignore it.
    }
  },

  /**
   * Splits a url into base and ref strings, according to nsIURI.idl.
   * Base refers to the part of the url before the ref, excluding the #.
   *
   * @param {string} url
   *   The url to split.
   * @returns {object} { base, ref }
   *   Base and ref parts of the given url. Ref is an empty string
   *   if there is no ref and undefined if url is not well-formed.
   */
  extractRefFromUrl(url) {
    let uri = URL.parse(url)?.URI;
    if (uri) {
      return { base: uri.specIgnoringRef, ref: uri.ref };
    }
    return { base: url };
  },

  /**
   * Strips a PSL verified public suffix from an hostname.
   *
   * Note: Because stripping the full suffix requires to verify it against the
   *   Public Suffix List, this call is not the cheapest, and thus it should
   *   not be used in hot paths.
   *
   * @param {string} host A host name.
   * @returns {string} Host name without the public suffix.
   */
  stripPublicSuffixFromHost(host) {
    try {
      return host.substring(
        0,
        host.length - Services.eTLD.getKnownPublicSuffixFromHost(host).length
      );
    } catch (ex) {
      if (ex.result != Cr.NS_ERROR_HOST_IS_IP_ADDRESS) {
        throw ex;
      }
    }
    return host;
  },

  /**
   * Returns URI fixup info for a string, allowing keyword lookup.
   *
   * @param {string} searchString
   *   The string to fix up.
   * @param {boolean} isPrivate
   *   Whether the fixup runs for a private context.
   * @returns {nsIURIFixupInfo|null}
   *   The fixup info, or null if fixup threw.
   */
  getURIFixupInfo(searchString, isPrivate) {
    let flags =
      Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
      Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
    if (isPrivate) {
      flags |= Ci.nsIURIFixup.FIXUP_FLAG_PRIVATE_CONTEXT;
    }
    try {
      return Services.uriFixup.getFixupURIInfo(searchString, flags);
    } catch (ex) {
      console.error(
        `An error occurred while trying to fixup "${searchString}"`,
        ex
      );
    }
    return null;
  },

  /**
   * Returns the parts of a string's URI fixup info that a consumer which can't
   * hold an XPCOM object can use, notably the content realm, which is served
   * across the `UrlbarChild` actor boundary.
   *
   * @param {string} searchString
   *   The string to fix up.
   * @param {boolean} isPrivate
   *   Whether the fixup runs for a private context.
   * @returns {?URIFixupPrimitives}
   *   The fixup primitives, or null if fixup threw.
   */
  getFixupPrimitives(searchString, isPrivate) {
    let info = this.getURIFixupInfo(searchString, isPrivate);
    return info
      ? {
          keywordAsSent: info.keywordAsSent,
          preferredURIDisplaySpec: info.preferredURI?.displaySpec ?? null,
        }
      : null;
  },

  /**
   * Add a (url, input) tuple to the input history table that drives adaptive
   * results.
   *
   * @param {string} url The url to add input history for
   * @param {string} input The associated search term
   * @returns {Promise<boolean>}
   *   Whether the row was written. False if the URL is not yet in moz_places
   *   or history is disabled.
   */
  async addToInputHistory(url, input) {
    if (!lazy.historyEnabled) {
      return false;
    }
    // use_count will asymptotically approach the max of 10.
    let rows = await lazy.PlacesUtils.withConnectionWrapper(
      "addToInputHistory",
      db => {
        return db.executeCached(
          `
          INSERT OR REPLACE INTO moz_inputhistory
          SELECT h.id, IFNULL(i.input, :input), IFNULL(i.use_count, 0) * .9 + 1
          FROM moz_places h
          LEFT JOIN moz_inputhistory i ON i.place_id = h.id AND i.input = :input
          WHERE url_hash = hash(:url) AND url = :url
          RETURNING place_id
          `,
          { url, input: input.toLowerCase() }
        );
      }
    );
    return !!rows.length;
  },

  /**
   * Like addToInputHistory, but if the URL is not yet in moz_places
   * (e.g. an origin derived from a deep-link visit), waits for the
   * visit to land before writing.
   *
   * @param {string} url The url to add input history for
   * @param {string} input The associated search term
   */
  async addToInputHistoryWhenReady(url, input) {
    if (!lazy.historyEnabled) {
      return;
    }
    // Register the observer before the initial attempt so we can't miss
    // a visit that lands between the check and the registration.
    let { promise: visitedPromise, resolve: visitedResolve } =
      Promise.withResolvers();
    let listener = events => {
      for (let event of events) {
        if (event.type == "page-visited" && event.url == url) {
          PlacesObservers.removeListener(["page-visited"], listener);
          visitedResolve(true);
          return;
        }
      }
    };
    PlacesObservers.addListener(["page-visited"], listener);

    // Safety timeout so we don't leak the listener forever.
    let timeoutId = lazy.setTimeout(() => {
      PlacesObservers.removeListener(["page-visited"], listener);
      visitedResolve(false);
    }, 1000);

    // Try immediately, succeeds if the URL is already in moz_places.
    if (await this.addToInputHistory(url, input)) {
      PlacesObservers.removeListener(["page-visited"], listener);
      lazy.clearTimeout(timeoutId);
      return;
    }

    // Page not yet in moz_places, wait for the visit to be recorded.
    let visited = await visitedPromise;
    lazy.clearTimeout(timeoutId);
    if (visited) {
      await this.addToInputHistory(url, input);
    }
  },

  /**
   * Remove a (url, input*) tuple from the input history table that drives
   * adaptive results.
   * Note the input argument is used as a wildcard so any match starting with
   * it will also be removed.
   *
   * @param {string} url The url to add input history for
   * @param {string} input The associated search term
   */
  async removeInputHistory(url, input) {
    await lazy.PlacesUtils.withConnectionWrapper("removeInputHistory", db => {
      return db.executeCached(
        `
        DELETE FROM moz_inputhistory
        WHERE input BETWEEN :input AND :input || X'FFFF'
          AND place_id =
            (SELECT id FROM moz_places WHERE url_hash = hash(:url) AND url = :url)
        `,
        { url, input: input.toLowerCase() }
      );
    });
  },

  /**
   * Temporarily blocks autofill for the given URL. If the URL is an origin,
   * blocks origin autofill via blockOriginAutofill. Otherwise, blocks
   * page-level autofill via blockOriginPageAutofill.
   *
   * @param {string} url
   *   The URL to block from autofill.
   * @param {number} blockUntilMs
   *   Epoch timestamp in ms after which the block expires.
   */
  async blockAutofill(url, blockUntilMs) {
    if (UrlbarShared.isOriginUrl(url)) {
      await this.blockOriginAutofill(url, blockUntilMs);
    } else {
      await this.blockOriginPageAutofill(url, blockUntilMs);
    }
  },

  /**
   * Temporarily blocks origin autofill for the given URL's origin and all its
   * scheme/www variations. For example, blocking https://www.example.com also
   * blocks http://www.example.com, http://example.com, and
   * https://example.com. The lookup matches against moz_origins directly, so
   * the URL need not have a corresponding entry in moz_places.
   *
   * This is a no-op if the URL is unparseable.
   *
   * @param {string} url
   *   A URL belonging to the origin to block.
   * @param {number} blockUntilMs
   *   Epoch timestamp in ms after which the block expires.
   */
  async blockOriginAutofill(url, blockUntilMs) {
    let origin = parseOriginParts(url);
    if (!origin) {
      return;
    }

    let baseHost = origin.host.replace(/^www\./, "");
    let wwwHost = "www." + baseHost;

    await lazy.PlacesUtils.withConnectionWrapper("blockOriginAutofill", db => {
      return db.executeCached(
        `
      UPDATE moz_origins SET block_until_ms = :blockUntilMs
      WHERE host IN (:baseHost, :wwwHost)
        AND prefix IN ('http://', 'https://')
      `,
        { blockUntilMs, baseHost, wwwHost }
      );
    });
  },

  /**
   * Temporarily blocks page-level autofill for the given URL's origin and all
   * its scheme/www variations. For example, blocking https://www.example.com
   * also blocks http://www.example.com, http://example.com, and
   * https://example.com.
   *
   * This is a no-op if the URL is unparseable.
   *
   * @param {string} url
   *   A URL belonging to the origin to block.
   * @param {number} blockPagesUntilMs
   *   Epoch timestamp in ms after which the block expires.
   */
  async blockOriginPageAutofill(url, blockPagesUntilMs) {
    let origin = parseOriginParts(url);
    if (!origin) {
      return;
    }

    let baseHost = origin.host.replace(/^www\./, "");
    let wwwHost = "www." + baseHost;

    await lazy.PlacesUtils.withConnectionWrapper(
      "blockOriginPageAutofill",
      db => {
        return db.executeCached(
          `
        UPDATE moz_origins SET block_pages_until_ms = :blockPagesUntilMs
        WHERE host IN (:baseHost, :wwwHost)
          AND prefix IN ('http://', 'https://')
        `,
          { blockPagesUntilMs, baseHost, wwwHost }
        );
      }
    );
  },

  /**
   * Clears an origin-level autofill block for the given URL's origin and all
   * its scheme/www variations. For example, clearing a block on
   * http://example.com also clears blocks on https://example.com,
   * http://www.example.com, and https://www.example.com. The lookup matches
   * against moz_origins directly, so the URL need not have a corresponding
   * entry in moz_places.
   *
   * This is a no-op if the URL is unparseable or the origin is not
   * currently blocked.
   *
   * @param {string} url
   *   A URL belonging to the origin to unblock.
   * @returns {Promise<boolean>}
   *   True if a block was actually cleared, false otherwise.
   */
  async clearOriginAutofillBlock(url) {
    let origin = parseOriginParts(url);
    if (!origin) {
      return false;
    }

    let baseHost = origin.host.replace(/^www\./, "");
    let wwwHost = "www." + baseHost;

    let rows = await lazy.PlacesUtils.withConnectionWrapper(
      "clearOriginAutofillBlock",
      db => {
        return db.executeCached(
          `
        UPDATE moz_origins SET block_until_ms = NULL
        WHERE host IN (:baseHost, :wwwHost)
          AND prefix IN ('http://', 'https://')
          AND block_until_ms IS NOT NULL
        RETURNING id
        `,
          { baseHost, wwwHost }
        );
      }
    );
    return !!rows.length;
  },

  /**
   * Clears a page-level autofill block for the given URL's origin and all its
   * scheme/www variations. For example, clearing a block on
   * http://example.com also clears blocks on https://example.com,
   * http://www.example.com, and https://www.example.com.
   *
   * This is a no-op if the URL is unparseable or the origin's pages are
   * not currently blocked.
   *
   * @param {string} url
   *   A URL belonging to the origin to unblock.
   * @returns {Promise<boolean>}
   *   True if a block was actually cleared, false otherwise.
   */
  async clearOriginPageAutofillBlock(url) {
    let origin = parseOriginParts(url);
    if (!origin) {
      return false;
    }

    let baseHost = origin.host.replace(/^www\./, "");
    let wwwHost = "www." + baseHost;

    let rows = await lazy.PlacesUtils.withConnectionWrapper(
      "clearOriginPageAutofillBlock",
      db => {
        return db.executeCached(
          `
        UPDATE moz_origins SET block_pages_until_ms = NULL
        WHERE host IN (:baseHost, :wwwHost)
          AND prefix IN ('http://', 'https://')
          AND block_pages_until_ms IS NOT NULL
        RETURNING id
        `,
          { baseHost, wwwHost }
        );
      }
    );
    return !!rows.length;
  },

  /**
   * @typedef {object} BackspaceInfo
   * @property {number?} count
   *   How many times the origin or URL had its autofill cleared.
   * @property {number?} blockedAt
   *   How long ago the origin or URL was blocked from backspacing.
   */

  /**
   * LRU map tracking adaptive autofill backspace dismissals through their
   * lifecycle: pre-block (counting backspaces toward the threshold) and
   * post-block (timestamp used by re-integration telemetry to measure how
   * long until the user returned to the same destination).
   *
   * Keyed by "<scope>:<host>" where scope is "origin" or "page" and host
   * has any leading "www." stripped, matching how blockOriginAutofill
   * applies the underlying SQL block. The two scopes for the same host
   * live as independent entries.
   *
   * Insertion order is maintained as an LRU: entries past
   * _BACKSPACE_BLOCKS_MAX are evicted. State does not survive
   * restart.
   *
   * Re-integration after restart, or more than _BACKSPACE_BLOCK_MAX_AGE_HOURS
   * after the block, does not record a sample.
   *
   * @type {Map<string, BackspaceInfo>}
   */
  _backspaceBlocks: new Map(),

  // Resolves with the most recent recordAutofillBackspace() call's DB write
  // (if any). Tests can await this to sequence on the block before reading
  // from the database.
  _lastRecordAutofillBackspacePromise: Promise.resolve(),

  // Maximum age of a tracked block, in hours.
  _BACKSPACE_BLOCK_MAX_AGE_HOURS: 24,

  // Maximum number of blocked origins that can be stored at one time.
  _BACKSPACE_BLOCKS_MAX: 512,

  /**
   * Computes the map key used to record a backspace block for the given URL.
   * The key is in the form of the scope (origin or page), followed by colon,
   * and the URL's host with a leading "www." stripped, mirroring how
   * blockOriginAutofill applies the underlying block in SQL.
   *
   * @param {string} url
   *   The URL whose key is being computed.
   * @returns {?string}
   *   The scope and normalized host key, or null if the URL is unparseable.
   *
   * @example
   * // Returns "origin:example.com"
   * _backspaceBlockKey("https://www.example.com/");
   *
   * @example
   * // Returns "page:example.com"
   * _backspaceBlockKey("https://example.com/some/path");
   */
  _backspaceBlockKey(url) {
    let origin = parseOriginParts(url);
    if (!origin) {
      return null;
    }
    let basehost = origin.host.replace(/^www\./, "");
    let scope = /** @type {"origin" | "page"} */ (
      UrlbarShared.isOriginUrl(url) ? "origin" : "page"
    );
    return `${scope}:${basehost}`;
  },

  /**
   * Records a backspace in the LRU map for the autofill URL. Increments
   * (or creates) the entry's count and when the count reaches the
   * `autoFill.backspaceThreshold` pref calls blockAutofill and records the
   * entry's blockedAt so re-integration telemetry can sample the unblock
   * delay later.
   *
   * @param {string} url
   *   The autofill result URL whose backspace is being recorded.
   * @returns {Promise<void>}
   *   Resolves after the threshold-triggered blockAutofill DB write completes.
   *   Resolves immediately when the URL is unparseable or the count is still
   *   below threshold (no DB write in either case). Tests can await this to
   *   sequence on the block, or await `_lastRecordAutofillBackspacePromise`
   *   when they can't reach the return value.
   */
  recordAutofillBackspace(url) {
    this._lastRecordAutofillBackspacePromise =
      this._doRecordAutofillBackspace(url);
    return this._lastRecordAutofillBackspacePromise;
  },

  async _doRecordAutofillBackspace(url) {
    let key = this._backspaceBlockKey(url);
    if (!key) {
      return;
    }

    let entry = this._backspaceBlocks.get(key) ?? {
      count: 0,
      blockedAt: null,
    };
    if (entry.blockedAt) {
      delete entry.blockedAt;
    }
    let newCount = (entry.count ?? 0) + 1;

    if (newCount >= lazy.UrlbarPrefs.get("autoFill.backspaceThreshold")) {
      delete entry.count;
      entry.blockedAt = Date.now();
      await this.blockAutofill(
        url,
        Date.now() + lazy.UrlbarPrefs.get("autoFill.backspaceBlockDurationMs")
      ).catch(console.error);
    } else {
      entry.count = newCount;
    }

    // Remove and reinsert so this entry is treated as most-recent under the
    // Map's insertion-order semantics, which we rely on below to expire the
    // least-recently-used entry.
    this._backspaceBlocks.delete(key);
    this._backspaceBlocks.set(key, entry);

    // Least recently used are expired.
    if (this._backspaceBlocks.size > this._BACKSPACE_BLOCKS_MAX) {
      let oldestKey = this._backspaceBlocks.keys().next().value;
      this._backspaceBlocks.delete(oldestKey);
    }
  },

  /**
   * Retrieves and deletes the recorded backspace block for the given URL, if
   * any. Only the timestamp matching the URL's level (origin vs. url) is
   * returned and removed; any other-level timestamp for the same host is
   * preserved. If the matching timestamp is older than
   * BACKSPACE_BLOCK_MAX_AGE_HOURS, this returns null. (The timestamp is
   * removed either way.)
   *
   * @param {string} url
   *   The URL whose block is being cleared.
   * @returns {?{blockedAt: number, level: "origin" | "url"}}
   *   The matching timestamp and level if a fresh block existed,
   *   null otherwise.
   */
  getBackspaceBlock(url) {
    let key = this._backspaceBlockKey(url);
    if (!key) {
      return null;
    }

    let entry = this._backspaceBlocks.get(key);
    if (!entry?.blockedAt) {
      return null;
    }

    // Consume the timestamp so a later call returns null for this URL.
    this._backspaceBlocks.delete(key);

    // If the timestamp is too old, don't report it.
    let ageHours = (Date.now() - entry.blockedAt) / (60 * 60 * 1000);
    if (ageHours > this._BACKSPACE_BLOCK_MAX_AGE_HOURS) {
      return null;
    }
    /** @type {"origin" | "url"} */
    let level = UrlbarShared.isOriginUrl(url) ? "origin" : "url";
    return { blockedAt: entry.blockedAt, level };
  },

  /**
   * Clears the in-progress backspace count for the URL's scope.
   *
   * @param {string} url
   *   A URL belonging to the scope being cleared.
   */
  clearAutofillBackspaceEntryForUrl(url) {
    let key = this._backspaceBlockKey(url);
    if (key) {
      this._backspaceBlocks.delete(key);
    }
  },

  /**
   * Dismisses an autofill result, either by blocking the autofill pairing for
   * `autoFill.dismissalBlockDurationMs` or by removing the URL from history
   * entirely, and clears the URL's backspace bookkeeping either way. Failures
   * are reported and swallowed so a caller can still re-run its query.
   *
   * @param {string} url
   *   The dismissed autofill result's URL.
   * @param {object} [options]
   *   Options object.
   * @param {boolean} [options.removeFromHistory]
   *   Whether to remove the URL from history instead of blocking autofill
   *   for it.
   */
  async dismissAutofill(url, { removeFromHistory = false } = {}) {
    if (removeFromHistory) {
      await lazy.PlacesUtils.history.remove(url).catch(console.error);
    } else {
      await this.blockAutofill(
        url,
        Date.now() + lazy.UrlbarPrefs.get("autoFill.dismissalBlockDurationMs")
      ).catch(console.error);
    }

    this.clearAutofillBackspaceEntryForUrl(url);
  },

  /**
   * Re-integrates an autofill URL the user navigated to anyway: clears the
   * URL's autofill block and its backspace bookkeeping, and reports what
   * happened so the caller can record re-integration telemetry.
   *
   * @param {string} url
   *   The URL being re-integrated.
   * @returns {Promise<{wasBlocked: boolean, level: "origin" | "url", backspaceBlock: ?{blockedAt: number, level: "origin" | "url"}}>}
   *   `wasBlocked` is whether a database block was actually cleared, `level`
   *   the scope it was cleared at, and `backspaceBlock` the consumed backspace
   *   block, if the URL had one.
   */
  async reintegrateAutofill(url) {
    let isOrigin = UrlbarShared.isOriginUrl(url);
    let wasBlocked = isOrigin
      ? await this.clearOriginAutofillBlock(url)
      : await this.clearOriginPageAutofillBlock(url);

    // getBackspaceBlock reads and removes the {blockedAt} entry for telemetry.
    // clearAutofillBackspaceEntryForUrl then removes any remaining
    // sub-threshold {count} entry. Together they always clear the in-memory
    // counter — visiting the url is a positive signal regardless of whether a
    // database block existed.
    let backspaceBlock = this.getBackspaceBlock(url);
    this.clearAutofillBackspaceEntryForUrl(url);

    return { wasBlocked, level: isOrigin ? "origin" : "url", backspaceBlock };
  },

  /**
   * Return whether or not persisted search terms is enabled.
   *
   * @returns {boolean} true: if enabled.
   */
  isPersistedSearchTermsEnabled() {
    return (
      lazy.UrlbarPrefs.getScotchBonnetPref("showSearchTerms.featureGate") &&
      lazy.UrlbarPrefs.get("showSearchTerms.enabled") &&
      !lazy.CustomizableUI.getPlacementOfWidget("search-container")
    );
  },

  /**
   * Returns the portion of a string starting at the index where another string
   * begins.
   *
   * @param   {string} sourceStr
   *          The string to search within.
   * @param   {string} targetStr
   *          The string to search for.
   * @returns {string} The substring within sourceStr starting at targetStr, or
   *          the empty string if targetStr does not occur in sourceStr.
   */
  substringAt(sourceStr, targetStr) {
    let index = sourceStr.indexOf(targetStr);
    return index < 0 ? "" : sourceStr.substr(index);
  },

  /**
   * Returns the portion of a string starting at the index where another string
   * ends.
   *
   * @param   {string} sourceStr
   *          The string to search within.
   * @param   {string} targetStr
   *          The string to search for.
   * @returns {string} The substring within sourceStr where targetStr ends, or
   *          the empty string if targetStr does not occur in sourceStr.
   */
  substringAfter(sourceStr, targetStr) {
    let index = sourceStr.indexOf(targetStr);
    return index < 0 ? "" : sourceStr.substr(index + targetStr.length);
  },

  /**
   * Strips the prefix from a URL and returns the prefix and the remainder of
   * the URL. "Prefix" is defined to be the scheme and colon plus zero to two
   * slashes (see `UrlbarTokenizer.REGEXP_PREFIX`). If the given string is not
   * actually a URL or it has a prefix we don't recognize, then an empty prefix
   * and the string itself is returned.
   *
   * @param   {string} str The possible URL to strip.
   * @returns {Array} If `str` is a URL with a prefix we recognize,
   *          then [prefix, remainder].  Otherwise, ["", str].
   */
  stripURLPrefix(str) {
    let match = lazy.UrlUtils.REGEXP_PREFIX.exec(str);
    if (!match) {
      return ["", str];
    }
    let prefix = match[0];
    if (prefix.length < str.length && str[prefix.length] == " ") {
      // A space following a prefix:
      // e.g. "http:// some search string", "about: some search string"
      return ["", str];
    }
    if (
      prefix.endsWith(":") &&
      !UrlbarShared.PROTOCOLS_WITHOUT_AUTHORITY.includes(prefix.toLowerCase())
    ) {
      // Something that looks like a URI scheme but we won't treat as one:
      // e.g. "localhost:8888"
      return ["", str];
    }
    return [prefix, str.substring(prefix.length)];
  },

  /**
   * Add the search to form history.  This also updates any existing form
   * history for the search.
   *
   * @param {boolean} isPrivate
   *   Whether the search is private is in a private window.
   * @param {string} value The value to add.
   * @param {string} [source] The source of the addition, usually
   *        the name of the engine the search was made with.
   * @returns {Promise<void>} resolved once the operation is complete
   */
  async addToFormHistory(isPrivate, value, source) {
    // If the user types a search engine alias without a search string,
    // we have an empty search string and we can't bump it.
    // We also don't want to add history in private browsing mode.
    // Finally we don't want to store extremely long strings that would not be
    // particularly useful to the user.
    if (
      !value ||
      isPrivate ||
      value.length >
        lazy.SearchSuggestionController.SEARCH_HISTORY_MAX_VALUE_LENGTH
    ) {
      return;
    }
    await lazy.FormHistory.update({
      op: "bump",
      fieldname: lazy.DEFAULT_FORM_HISTORY_PARAM,
      value,
      source,
    });
  },

  /**
   * Clears the form history for all search engines.
   *
   * @returns {Promise<void>}
   */
  clearFormHistory() {
    return lazy.FormHistory.update({
      op: "remove",
      fieldname: lazy.DEFAULT_FORM_HISTORY_PARAM,
    });
  },

  /**
   * For use when we want to hash a pair of items in a dictionary
   *
   * @param {string[]} tokens
   *   list of tokens to join into a string eg "a" "b" "c"
   * @returns {string}
   *   the tokens joined in a string "a|b|c"
   */
  tupleString(...tokens) {
    return tokens.filter(t => t).join("|");
  },

  /**
   * Creates camelCase versions of snake_case keys in the given object and
   * recursively all nested objects. All objects are modified in place and the
   * original snake_case keys are preserved.
   *
   * @param {object} obj
   *   The object to modify.
   * @param {boolean} [overwrite]
   *   Controls what happens when a camelCase key is already defined for a
   *   snake_case key (excluding keys that don't have underscores). If true the
   *   existing key will be overwritten. If false an error will be thrown.
   * @returns {object} The passed-in modified-in-place object.
   */
  copySnakeKeysToCamel(obj, overwrite = true) {
    for (let [key, value] of Object.entries(obj)) {
      // Trim off leading underscores since they'll interfere with the replace.
      // We'll tack them back on after.
      let match = key.match(/^_+/);
      if (match) {
        key = key.substring(match[0].length);
      }
      let camelKey = key.replace(/_([^_])/g, (m, p1) => p1.toUpperCase());
      if (match) {
        camelKey = match[0] + camelKey;
      }
      if (!overwrite && camelKey != key && obj.hasOwnProperty(camelKey)) {
        throw new Error(
          `Can't copy snake_case key '${key}' to camelCase key ` +
            `'${camelKey}' because '${camelKey}' is already defined`
        );
      }
      obj[camelKey] = value;
      if (value && typeof value == "object") {
        this.copySnakeKeysToCamel(value);
      }
    }
    return obj;
  },

  /**
   * Create secondary action button data for tab switch.
   *
   * @param {number} userContextId
   *   The container id for the tab.
   * @returns {object} data to create secondary action button.
   */
  createTabSwitchSecondaryAction(userContextId) {
    let action = { key: "tabswitch" };
    let identity =
      lazy.ContextualIdentityService.getPublicIdentityFromId(userContextId);

    if (identity) {
      let label =
        lazy.ContextualIdentityService.getUserContextLabel(
          userContextId
        ).toLowerCase();
      action.l10nId = "urlbar-result-action-switch-tab-with-container";
      action.l10nArgs = {
        container: label,
      };
      action.classList = [
        "urlbarView-userContext",
        `identity-color-${identity.color}`,
      ];
    } else {
      action.l10nId = "urlbar-result-action-switch-tab";
    }

    return action;
  },

  /**
   * Builds the `userContext` payload of a tab-switch result: the container id
   * the tab lives in, plus the container's display data, resolved here because
   * the view can't reach ContextualIdentityService.
   *
   * @param {number} userContextId
   *   The container id for the tab.
   * @returns {{id: number, label?: string, color?: string, iconUrl?: string}}
   *   The display data is absent when the id has no public identity. The label
   *   is trimmed.
   */
  getUserContextData(userContextId) {
    let identity =
      lazy.ContextualIdentityService.getPublicIdentityFromId(userContextId);
    if (!identity) {
      return { id: userContextId };
    }

    return {
      id: userContextId,
      label:
        lazy.ContextualIdentityService.getUserContextLabel(
          userContextId
        ).trim(),
      color: identity.color,
      iconUrl: lazy.ContextualIdentityService.getContainerIconURL(
        identity.icon
      ),
    };
  },

  /**
   * Gets the URL bar element that should be focused for the given window.
   * Returns window.gURLBar for regular browser windows, or the smartbar
   * for AI windows in immersive view.
   *
   * @param {Window} window
   *   The window to get the URL bar for.
   * @returns {UrlbarInput | SmartbarInput }
   *   The URL bar element that should be focused.
   */
  getURLBarForFocus(window) {
    /** @type {UrlbarInput | SmartbarInput} */
    let urlbar = window.gURLBar;
    // Check if we're in an AI window with immersive view (no address bar visible)
    if (
      lazy.AIWindow.isAIWindowActive(window) &&
      lazy.AIWindow.shouldUseImmersiveView(window.gBrowser.currentURI)
    ) {
      let smartbar = lazy.AIWindow.getSmartbarForWindow(window);
      if (smartbar) {
        urlbar = smartbar;
      }
    }
    return urlbar;
  },

  /**
   * Formats the numerical portion of unit conversion results.
   *
   * @param {number} result
   *  The raw unformatted unit conversion result.
   */
  formatUnitConversionResult(result) {
    const DECIMAL_PRECISION = 10;
    const MAX_SIG_FIGURES = 10;
    const FULL_NUMBER_MAX_THRESHOLD = 1 * 10 ** 10;
    const FULL_NUMBER_MIN_THRESHOLD = 10 ** -5;
    const MAX_FLOAT_PRECISION = 15;

    let locale = Services.locale.appLocaleAsBCP47;

    if (
      Math.abs(result) >= FULL_NUMBER_MAX_THRESHOLD ||
      (Math.abs(result) <= FULL_NUMBER_MIN_THRESHOLD && result !== 0)
    ) {
      return new Intl.NumberFormat(locale, {
        style: "decimal",
        notation: "scientific",
        minimumFractionDigits: 1,
        maximumFractionDigits: DECIMAL_PRECISION,
        numberingSystem: "latn",
      })
        .format(result)
        .toLowerCase();
    } else if (Math.abs(result) >= 1) {
      return new Intl.NumberFormat(locale, {
        style: "decimal",
        maximumFractionDigits: DECIMAL_PRECISION,
        maximumSignificantDigits: MAX_FLOAT_PRECISION,
        roundingPriority: "lessPrecision",
        numberingSystem: "latn",
      }).format(result);
    }
    return new Intl.NumberFormat(locale, {
      style: "decimal",
      maximumSignificantDigits: MAX_SIG_FIGURES,
      numberingSystem: "latn",
    }).format(result);
  },
};

ChromeUtils.defineLazyGetter(UrlbarUtils, "strings", () => {
  return Services.strings.createBundle(
    "chrome://global/locale/autocomplete.properties"
  );
});

const L10N_SCHEMA = {
  type: "object",
  required: ["id"],
  properties: {
    id: {
      type: "string",
    },
    args: {
      type: "object",
      additionalProperties: true,
    },
    // This object is parallel to args and should include an entry for each arg
    // to which highlights should be applied. See L10nCache.setElementL10n().
    argsHighlights: {
      type: "object",
      additionalProperties: true,
    },
    // The remaining properties are related to l10n string caching. See
    // `L10nCache`. All are optional and are false by default.
    parseMarkup: {
      type: "boolean",
    },
  },
};

/**
 * Payload JSON schemas for each result type.  Payloads are validated against
 * these schemas using JsonSchemaValidator.sys.mjs.
 */
UrlbarUtils.RESULT_PAYLOAD_SCHEMA = {
  [UrlbarShared.RESULT_TYPE.TAB_SWITCH]: {
    type: "object",
    required: ["url"],
    properties: {
      action: {
        type: "object",
        properties: {
          classList: {
            type: "array",
            items: {
              type: "string",
            },
          },
          l10nArgs: {
            type: "object",
            additionalProperties: true,
          },
          l10nId: {
            type: "string",
          },
          key: {
            type: "string",
          },
        },
      },
      bookmarkDateMs: {
        type: "number",
      },
      frecency: {
        type: "number",
      },
      icon: {
        type: "string",
      },
      isPinned: {
        type: "boolean",
      },
      isSponsored: {
        type: "boolean",
      },
      lastVisit: {
        type: "number",
      },
      tabGroup: {
        type: "string",
      },
      title: {
        type: "string",
      },
      url: {
        type: "string",
      },
      userContext: {
        type: "object",
        required: ["id"],
        properties: {
          color: {
            type: "string",
          },
          iconUrl: {
            type: "string",
          },
          id: {
            type: "number",
          },
          label: {
            type: "string",
          },
        },
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.SEARCH]: {
    type: "object",
    properties: {
      blockL10n: L10N_SCHEMA,
      description: {
        type: "string",
      },
      descriptionL10n: L10N_SCHEMA,
      engine: {
        type: "string",
      },
      helpUrl: {
        type: "string",
      },
      icon: {
        type: "string",
      },
      inPrivateWindow: {
        type: "boolean",
      },
      isBlockable: {
        type: "boolean",
      },
      isManageable: {
        type: "boolean",
      },
      isPinned: {
        type: "boolean",
      },
      isPrivateEngine: {
        type: "boolean",
      },
      isGeneralPurposeEngine: {
        type: "boolean",
      },
      keyword: {
        type: "string",
      },
      keywords: {
        type: "string",
      },
      lowerCaseSuggestion: {
        type: "string",
      },
      providesSearchMode: {
        type: "boolean",
      },
      query: {
        type: "string",
      },
      satisfiesAutofillThreshold: {
        type: "boolean",
      },
      searchUrlDomainWithoutSuffix: {
        type: "string",
      },
      suggestion: {
        type: "string",
      },
      tail: {
        type: "string",
      },
      tailPrefix: {
        type: "string",
      },
      tailOffsetIndex: {
        type: "number",
      },
      title: {
        type: "string",
      },
      trending: {
        type: "boolean",
      },
      url: {
        type: "string",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.URL]: {
    type: "object",
    required: ["url"],
    properties: {
      blockL10n: L10N_SCHEMA,
      bookmarkDateMs: {
        type: "number",
      },
      bottomTextL10n: L10N_SCHEMA,
      description: {
        type: "string",
      },
      descriptionL10n: L10N_SCHEMA,
      dismissalKey: {
        type: "string",
      },
      dupedHeuristic: {
        type: "boolean",
      },
      frecency: {
        type: "number",
      },
      helpL10n: L10N_SCHEMA,
      helpUrl: {
        type: "string",
      },
      icon: {
        type: "string",
      },
      iconBlob: {
        type: "object",
      },
      isAutofillFallback: {
        type: "boolean",
      },
      isBlockable: {
        type: "boolean",
      },
      isManageable: {
        type: "boolean",
      },
      isPinned: {
        type: "boolean",
      },
      isSponsored: {
        type: "boolean",
      },
      lastVisit: {
        type: "number",
      },
      originalUrl: {
        type: "string",
      },
      provider: {
        type: "string",
      },
      requestId: {
        type: "string",
      },
      sendAttributionRequest: {
        type: "boolean",
      },
      shouldShowUrl: {
        type: "boolean",
      },
      source: {
        type: "string",
      },
      sponsoredAdvertiser: {
        type: "string",
      },
      sponsoredBlockId: {
        type: "number",
      },
      sponsoredClickUrl: {
        type: "string",
      },
      sponsoredIabCategory: {
        type: "string",
      },
      sponsoredImpressionUrl: {
        type: "string",
      },
      sponsoredTileId: {
        type: "number",
      },
      subtype: {
        type: "string",
      },
      suggestionId: {
        type: "string",
      },
      suggestionObject: {
        type: "object",
      },
      tags: {
        type: "array",
        items: {
          type: "string",
        },
      },
      telemetryType: {
        type: "string",
      },
      title: {
        type: "string",
      },
      titleL10n: L10N_SCHEMA,
      subtitle: {
        type: "string",
      },
      subtitleL10n: L10N_SCHEMA,
      url: {
        type: "string",
      },
      urlTimestampIndex: {
        type: "number",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.KEYWORD]: {
    type: "object",
    required: ["keyword", "url"],
    properties: {
      bookmarkDateMs: {
        type: "number",
      },
      icon: {
        type: "string",
      },
      input: {
        type: "string",
      },
      keyword: {
        type: "string",
      },
      postData: {
        type: "string",
      },
      title: {
        type: "string",
      },
      url: {
        type: "string",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.OMNIBOX]: {
    type: "object",
    required: ["keyword"],
    properties: {
      blockL10n: L10N_SCHEMA,
      content: {
        type: "string",
      },
      icon: {
        type: "string",
      },
      isBlockable: {
        type: "boolean",
      },
      keyword: {
        type: "string",
      },
      title: {
        type: "string",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.REMOTE_TAB]: {
    type: "object",
    required: ["device", "url", "lastUsed"],
    properties: {
      device: {
        type: "string",
      },
      icon: {
        type: "string",
      },
      lastUsed: {
        type: "number",
      },
      title: {
        type: "string",
      },
      url: {
        type: "string",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.TIP]: {
    type: "object",
    required: ["type"],
    properties: {
      buttons: {
        type: "array",
        items: {
          type: "object",
          required: ["l10n"],
          properties: {
            l10n: L10N_SCHEMA,
            url: {
              type: "string",
            },
            command: {
              type: "string",
            },
            input: {
              type: "string",
            },
            attributes: {
              type: "object",
              properties: {
                primary: {
                  type: "string",
                },
              },
            },
            menu: {
              type: "array",
              items: {
                type: "object",
                properties: {
                  l10n: L10N_SCHEMA,
                  name: {
                    type: "string",
                  },
                },
              },
            },
          },
        },
      },
      // TODO: This is intended only for WebExtensions. We should remove it and
      // the WebExtensions urlbar API since we're no longer using it.
      buttonText: {
        type: "string",
      },
      // TODO: This is intended only for WebExtensions. We should remove it and
      // the WebExtensions urlbar API since we're no longer using it.
      buttonUrl: {
        type: "string",
      },
      helpL10n: L10N_SCHEMA,
      helpUrl: {
        type: "string",
      },
      icon: {
        type: "string",
      },
      // TODO: This is intended only for WebExtensions. We should remove it and
      // the WebExtensions urlbar API since we're no longer using it.
      text: {
        type: "string",
      },
      titleL10n: L10N_SCHEMA,
      descriptionL10n: L10N_SCHEMA,
      // If the `descriptionL10n` string includes a "Learn more" link, the
      // link anchor must have the attribute `data-l10n-name="learn-more-link"`
      // and the value of `descriptionLearnMoreTopic` must be the SUMO help
      // topic (the string appended to `app.support.baseURL`, e.g.,
      // "firefox-suggest").
      descriptionLearnMoreTopic: {
        type: "string",
      },
      type: {
        type: "string",
        enum: [
          "dismissalAcknowledgment",
          "extension",
          "intervention_clear",
          "intervention_refresh",
          "intervention_update_ask",
          "intervention_update_refresh",
          "intervention_update_restart",
          "intervention_update_web",
          "realtime_opt_in",
          "searchTip_onboard",
          "searchTip_redirect",
          "test", // for tests only
        ],
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.DYNAMIC]: {
    type: "object",
    required: ["dynamicType"],
    properties: {
      dynamicType: {
        type: "string",
      },
      // Set by UrlbarProvidersManager when the result is finalized,
      // so they're not present initially.
      viewTemplate: {
        type: "object",
      },
      viewUpdate: {
        type: "object",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.RESTRICT]: {
    type: "object",
    properties: {
      icon: {
        type: "string",
      },
      keyword: {
        type: "string",
      },
      l10nRestrictKeywords: {
        type: "array",
        items: {
          type: "string",
        },
      },
      autofillKeyword: {
        type: "string",
      },
      providesSearchMode: {
        type: "boolean",
      },
    },
  },
  [UrlbarShared.RESULT_TYPE.AI_CHAT]: {
    type: "object",
    required: ["icon", "query", "title"],
    properties: {
      icon: {
        type: "string",
      },
      query: {
        type: "string",
      },
      title: {
        type: "string",
      },
    },
  },
};

/**
 * Base class for a muxer.
 * The muxer scope is to sort a given list of results.
 */
export class UrlbarMuxer {
  /**
   * Unique name for the muxer, used by the context to sort results.
   * Not using a unique name will cause the newest registration to win.
   *
   * @abstract
   */
  get name() {
    return "UrlbarMuxerBase";
  }

  /**
   * Sorts queryContext results in-place.
   *
   * @param {UrlbarQueryContext} _queryContext the context to sort results for.
   * @param {Array} _unsortedResults
   *   The array of UrlbarResult that is not sorted yet.
   * @abstract
   */
  sort(_queryContext, _unsortedResults) {
    throw new Error("Trying to access the base class, must be overridden");
  }
}

/**
 * Base class for a provider.
 * The provider scope is to query a datasource and return results from it.
 */
export class UrlbarProvider {
  #lazy = XPCOMUtils.declareLazy({
    logger: () => UrlbarShared.getLogger({ prefix: `Provider.${this.name}` }),
  });

  get logger() {
    return this.#lazy.logger;
  }

  /**
   * Unique name for the provider, used by the context to filter on providers.
   * By default, it will use the class name but it can also be overridden to
   * use a different name.
   * Not using a unique name will cause the newest registration to win.
   */
  get name() {
    return this.constructor.name;
  }

  /**
   * The type of the provider, must be one of UrlbarShared.PROVIDER_TYPE.
   *
   * @returns {Values<typeof UrlbarShared.PROVIDER_TYPE>}
   * @abstract
   */
  get type() {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * @type {Query}
   *   This can be used by the provider to check the query is still running
   *   after executing async tasks:
   *
   * ```
   *   let instance = this.queryInstance;
   *   await ...
   *   if (instance != this.queryInstance) {
   *     // Query was canceled or a new one started.
   *     return;
   *   }
   * ```
   */
  queryInstance;

  /**
   * Calls a method on the provider in a try-catch block and reports any error.
   * Unlike most other provider methods, `tryMethod` is not intended to be
   * overridden.
   *
   * @param {string} methodName The name of the method to call.
   * @param {*} args The method arguments.
   * @returns {*} The return value of the method, or undefined if the method
   *          throws an error.
   * @abstract
   */
  tryMethod(methodName, ...args) {
    try {
      return this[methodName](...args);
    } catch (ex) {
      console.error(ex);
    }
    return undefined;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} [_queryContext]
   *   The query context object
   * @param {UrlbarParentController} [_controller]
   *   The current controller.
   * @returns {Promise<boolean>}
   *   Whether this provider should be invoked for the search.
   * @abstract
   */
  async isActive(_queryContext, _controller) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * Gets the provider's priority.  Priorities are numeric values starting at
   * zero and increasing in value.  Smaller values are lower priorities, and
   * larger values are higher priorities.  For a given query, `startQuery` is
   * called on only the active and highest-priority providers.
   *
   * @param {UrlbarQueryContext} _queryContext The query context object
   * @returns {number} The provider's priority for the given query.
   * @abstract
   */
  getPriority(_queryContext) {
    // By default, all providers share the lowest priority.
    return 0;
  }

  /**
   * Starts querying.
   *
   * Note: Extended classes should return a Promise resolved when the provider
   *       is done searching AND returning results.
   *
   * @param {UrlbarQueryContext} _queryContext
   *   The query context object
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} _addCallback
   *   Callback invoked by the provider to add a new result.
   * @param {UrlbarParentController} _controller
   *   The current controller.
   * @returns {void|Promise<void>}
   * @abstract
   */
  startQuery(_queryContext, _addCallback, _controller) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  /**
   * Cancels a running query,
   *
   * @param {UrlbarQueryContext} _queryContext the query context object to cancel
   *        query for.
   * @abstract
   */
  cancelQuery(_queryContext) {
    // Override this with your clean-up on cancel code.
  }

  // The following `on{Event}` notification methods are invoked only when
  // defined, thus there is no base class implementation for them
  /**
   * Called when a user engages with a result in the urlbar. This is called for
   * all providers who have implemented this method.
   *
   * @param {UrlbarQueryContext} _queryContext
   *   The engagement's query context. It will always be defined for
   *   "engagement" and "abandonment".
   * @param {UrlbarParentController} _controller
   *  The associated controller.
   * @param {object} _details
   *   This object is non-empty only when `state` is "engagement" or
   *   "abandonment", and it describes the search string and engaged result.
   *
   *   For "engagement", it has the following properties:
   *
   *   {UrlbarResult} result
   *       The engaged result. If a result itself was picked, this will be it.
   *       If an element related to a result was picked (like a button or menu
   *       command), this will be that result. This property will be present if
   *       and only if `state` == "engagement", so it can be used to quickly
   *       tell when the user engaged with a result.
   *   {Element} element
   *       The picked DOM element.
   *   {boolean} isSessionOngoing
   *       True if the search session remains ongoing or false if the engagement
   *       ended it. Typically picking a result ends the session but not always.
   *       Picking a button or menu command may not end the session; dismissals
   *       do not, for example.
   *   {string} searchString
   *       The search string for the engagement's query.
   *   {number} selIndex
   *       The index of the picked result.
   *   {string} selType
   *       The type of the selected result.  See TelemetryEvent.record() in
   *       UrlbarParentController.sys.mjs.
   *   {string} provider
   *       The name of the provider that produced the picked result.
   *
   *   For "abandonment", only `searchString` is defined.
   *
   * onEngagement(_queryContext, _controller, _details) {}
   */

  /**
   * Called when the user abandons a search session without selecting a result.
   * This could be due to losing focus on the urlbar, switching tabs, or other
   * actions that imply the user is no longer actively engaging with the search
   * suggestions. The method is called for all providers who have implemented
   * this method and whose results were visible at the time of the abandonment.
   *
   * @param {UrlbarQueryContext} _queryContext
   *    The query context at the time of abandonment.
   * @param {UrlbarParentController} _controller
   * The associated controller.
   *
   * onAbandonment(_queryContext, _controller) {}
   */

  /**
   * Called for providers whose results are visible at the time of either
   * engagement or abandonment. The method is called when a user actively
   * interacts with a search result. This interaction could be clicking on a
   * suggestion, using a keyboard to select a suggestion, or any other form of
   * direct engagement with the results displayed. It is also called
   * when a user decides to abandon the search session without engaging with any
   * of the presented results. This is called for all providers who have
   * implemented this method.
   *
   * @param {string} _state
   *    The state of the user interaction, either "engagement" or "abandonment".
   * @param {UrlbarQueryContext} _queryContext
   *    The current query context.
   * @param {UrlbarParentController} _controller
   *    The associated controller.
   * @param {Array} _providerVisibleResults
   *    Array of visible results at the time of either an engagement or
   *    abandonment event relevant to the provider. Each object in the array
   *    contains:
   *    - `index`: The position of the visible result within the original list
   *               visible results.
   *    - `result`: The visible result itself
   * @param {object|null} _details
   *    If the impression is due to an engagement, this will be the `details`
   *    object that's also passed to `onEngagement()`. Otherwise it will be
   *    null. See `onEngagement()` documentation for info.
   *
   * onImpression(_state, _queryContext, _controller, _providerVisibleResults, _details)
   * {}
   */

  /**
   * Called when a search session concludes regardless of how it ends -
   * whether through engagement or abandonment or otherwise. This is
   * called for all providers who have implemented this method.
   *
   * @param {UrlbarQueryContext} _queryContext
   *    The current query context.
   * @param {UrlbarParentController} _controller
   *    The associated controller.
   *
   * onSearchSessionEnd(_queryContext, _controller) {}
   */

  /**
   * Called before a result from the provider is selected. See `onSelection`
   * for details on what that means.
   *
   * @param {UrlbarResult} _result
   *   The result that was selected.
   * @param {Element} _element
   *   The element in the result's view that was selected.
   * @abstract
   */
  onBeforeSelection(_result, _element) {}

  /**
   * Called when a result from the provider is selected. "Selected" refers to
   * the user highlighing the result with the arrow keys/Tab, before it is
   * picked. onSelection is also called when a user clicks a result. In the
   * event of a click, onSelection is called just before onEngagement. Note that
   * this is called when heuristic results are pre-selected.
   *
   * @param {UrlbarResult} _result
   *   The result that was selected.
   * @param {Element} _element
   *   The element in the result's view that was selected.
   * @abstract
   */
  onSelection(_result, _element) {}

  /**
   * @typedef {object} ViewTemplate
   *   A plain object that describes the DOM subtree for a dynamic result type.
   *   When a dynamic result is shown in the urlbar view, its type's view template
   *   is used to construct the part of the view that represents the result.
   *
   * @property {ViewTemplateElement[]} children
   *   The elements that make up the subtree for the dynamic result type.
   */

  /**
   * @typedef {object} ViewTemplateElement
   *   Describes the DOM subtree for the given dynamic result type.
   *   It should be a tree-like nested structure with each object in the nesting
   *   representing a DOM element to be created.  This tree-like structure is
   *   achieved using the `children` property described below.  Each object in
   *   the structure may include the following properties:
   *
   * @property {string} tag
   *   The tag name of the object.  It is required for all objects in the
   *   structure except the root object and declares the kind of element that
   *   will be created for the object: span, div, img, etc.
   *
   * @property {string} [name]
   *   The name of the object. This value is required if you need to update
   *   the object's DOM element at query time. It's also helpful but not
   *   required if you need to style the element. When defined, it serves two
   *   important functions:
   *   (1) The element created for the object will automatically have a class
   *       named `urlbarView-dynamic-${dynamicType}-${name}`, where
   *       `dynamicType` is the name of the dynamic result type.  The element
   *       will also automatically have an attribute "name" whose value is
   *       this name.  The class and attribute allow the element to be styled
   *       in CSS.
   *   (2) The name is used when updating the view.  See
   *       UrlbarProvider.getViewUpdate().
   *   Names must be unique within a view template, but they don't need to be
   *   globally unique.  i.e., two different view templates can use the same
   *   names, and other DOM elements can use the same names in their IDs and
   *   classes.  The name also suffixes the dynamic element's ID: an element
   *   with name `data` will get the ID `urlbarView-row-{unique number}-data`.
   *   If there is no name provided for the root element, the root element
   *   will not get an ID.
   *
   * @property {object} [attributes]
   *   An optional mapping from attribute names to values.  For each
   *   name-value pair, an attribute is added to the element created for the
   *   object. The `id` attribute is reserved and cannot be set by the
   *   provider.
   *
   * @property {ViewTemplateElement[]} [children]
   *   An optional list of children.  Each item in the array must be an object
   *   as described here.  For each item, a child element as described by the
   *   item is created and added to the element created for the parent object.
   *
   * @property {string[]} [classList]
   *   An optional list of classes.  Each class will be added to the element
   *   created for the object by calling element.classList.add().
   *
   * @property {boolean} [overflowable]
   *   If true, the element's overflow status will be tracked in order to
   *   fade it out when needed.
   */

  /**
   * This is called only for dynamic result types.
   *
   * @param {UrlbarResult} _result
   *   The result whose view will be created.
   * @returns {?ViewTemplate}
   *   The view template describing the DOM to build for the result,
   *   or null if the provider doesn't define one.
   */
  getViewTemplate(_result) {
    return null;
  }

  /**
   * This is called only for dynamic result types by the providers manager. It
   * should return an object describing the view update that looks like this:
   *
   *   {
   *     nodeNameFoo: {
   *       attributes: {
   *         someAttribute: someValue,
   *       },
   *       style: {
   *         someStyleProperty: someValue,
   *         "another-style-property": someValue,
   *       },
   *       l10n: {
   *         id: someL10nId,
   *         args: someL10nArgs,
   *       },
   *       textContent: "some text content",
   *     },
   *     nodeNameBar: {
   *       ...
   *     },
   *     nodeNameBaz: {
   *       ...
   *     },
   *   }
   *
   * The object should contain a property for each element to update in the
   * dynamic result type view.  The names of these properties are the names
   * declared in the view template of the dynamic result type; see
   * UrlbarProvider.getViewTemplate().  The values are similar to the nested
   * objects specified in the view template but not quite the same; see below.
   * For each property, the element in the view subtree with the specified name
   * is updated according to the object in the property's value.  If an
   * element's name is not specified, then it will not be updated and will
   * retain its current state.
   *
   * @param {UrlbarResult} _result
   *   The result whose view will be updated.
   * @returns {object}
   *   A view update object as described above.  The names of properties are the
   *   the names of elements declared in the view template.  The values of
   *   properties are objects that describe how to update each element, and
   *   these objects may include the following properties, all of which are
   *   optional:
   *
   *   {object} [attributes]
   *     A mapping from attribute names to values.  Each name-value pair results
   *     in an attribute being added to the element.  The `id` attribute is
   *     reserved and cannot be set by the provider.
   *   {Array} [classList]
   *     An array of CSS classes to set on the element. If this is defined, the
   *     element's previous classes will be cleared first!
   *   {object} [dataset]
   *     Maps element dataset keys to values. Values should be strings with the
   *     following exceptions: `undefined` is ignored, and `null` causes the key
   *     to be removed from the dataset.
   *   {object} [style]
   *     A plain object that can be used to add inline styles to the element,
   *     like `display: none`.   `element.style` is updated for each name-value
   *     pair in this object.
   *   {object} [l10n]
   *     An { id, args } object that will be passed to
   *     document.l10n.setAttributes().
   *   {string} [textContent]
   *     A string that will be set as `element.textContent`.
   */
  getViewUpdate(_result) {
    return null;
  }

  /**
   * Gets the list of commands that should be shown in the result menu for a
   * given result from the provider. All commands returned by this method should
   * be handled by implementing `onEngagement()` with the possible exception of
   * commands automatically handled by the urlbar, like "help".
   *
   * @param {UrlbarResult} _result
   *   The menu will be shown for this result.
   * @param {boolean} _isPrivate
   *   Whether the query was made in a private browsing context.
   * @returns {?UrlbarResultCommand[]}
   */
  getResultCommands(_result, _isPrivate) {
    return null;
  }

  /**
   * Defines whether the view should defer user selection events while waiting
   * for the first result from this provider.
   *
   * Note: UrlbarEventBufferer has a timeout after which user events will be
   *       processed regardless.
   *
   * @returns {boolean} Whether the provider wants to defer user selection
   *          events.
   * @see {@link UrlbarEventBufferer}
   */
  get deferUserSelection() {
    return false;
  }
}

/**
 * Class used to create a timer that can be manually fired, to immediately
 * invoke the callback, or canceled, as necessary.
 * Examples:
 *   let timer = new SkippableTimer();
 *   // Invokes the callback immediately without waiting for the delay.
 *   await timer.fire();
 *   // Cancel the timer, the callback won't be invoked.
 *   await timer.cancel();
 *   // Wait for the timer to have elapsed.
 *   await timer.promise;
 */
export class SkippableTimer {
  /**
   * This can be used to track whether the timer completed.
   */
  done = false;

  /**
   * Creates a skippable timer for the given callback and time.
   *
   * @param {object} [options] An object that configures the timer
   * @param {string} [options.name] The name of the timer, logged when necessary
   * @param {Function} [options.callback] To be invoked when requested
   * @param {number} [options.time] A delay in milliseconds to wait for
   * @param {boolean} [options.reportErrorOnTimeout] If true and the timer times
   *                  out, an error will be logged with Cu.reportError
   * @param {Console} [options.logger] An optional logger
   */
  constructor({
    name = "<anonymous timer>",
    callback = null,
    time = 0,
    reportErrorOnTimeout = false,
    logger = null,
  } = {}) {
    this.name = name;
    this.logger = logger;

    let timerPromise = new Promise(resolve => {
      this._timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      this._timer.initWithCallback(
        () => {
          this._log(`Timed out!`, reportErrorOnTimeout);
          this.done = true;
          this._timer = null;
          resolve();
        },
        time,
        Ci.nsITimer.TYPE_ONE_SHOT
      );
      this._log(`Started`);
    });

    let firePromise = new Promise(resolve => {
      this.fire = async () => {
        this.done = true;
        if (this._timer) {
          if (!this._canceled) {
            this._log(`Skipped`);
          }
          this._timer.cancel();
          this._timer = null;
          resolve();
        }
        await this.promise;
      };
    });

    this.promise = Promise.race([timerPromise, firePromise]).then(() => {
      // If we've been canceled, don't call back.
      if (callback && !this._canceled) {
        callback();
      }
    });
  }

  /**
   * Allows to cancel the timer and the callback won't be invoked.
   * It is not strictly necessary to await for this, the promise can just be
   * used to ensure all the internal work is complete.
   */
  async cancel() {
    if (this._timer) {
      this._log(`Canceling`);
      this._canceled = true;
    }
    await this.fire();
  }

  _log(msg, isError = false) {
    let line = `SkippableTimer :: ${this.name} :: ${msg}`;
    if (this.logger) {
      this.logger.debug(line);
    }
    if (isError) {
      console.error(line);
    }
  }
}

/**
 * This class provides a way of serializing access to a resource. It's a queue
 * of callbacks (or "tasks") where each callback is called and awaited in order,
 * one at a time.
 */
export class TaskQueue {
  /**
   * @returns {Promise}
   *   Resolves when the queue becomes empty. If the queue is already empty,
   *   then a resolved promise is returned.
   */
  get emptyPromise() {
    return this.#emptyPromise;
  }

  /**
   * Adds a callback function to the task queue. The callback will be called
   * after all other callbacks before it in the queue. This method returns a
   * promise that will be resolved after awaiting the callback. The promise will
   * be resolved with the value returned by the callback.
   *
   * @param {Function} callback
   *   The function to queue.
   * @returns {Promise}
   *   Resolved after the task queue calls and awaits `callback`. It will be
   *   resolved with the value returned by `callback`. If `callback` throws an
   *   error, then it will be rejected with the error.
   */
  queue(callback) {
    return new Promise((resolve, reject) => {
      this.#queue.push({ callback, resolve, reject });
      if (this.#queue.length == 1) {
        this.#emptyDeferred = Promise.withResolvers();
        this.#emptyPromise = this.#emptyDeferred.promise;
        this.#doNextTask();
      }
    });
  }

  /**
   * Adds a callback function to the task queue that will be called on idle.
   *
   * @param {Function} callback
   *   The function to queue.
   * @returns {Promise}
   *   Resolved after the task queue calls and awaits `callback`. It will be
   *   resolved with the value returned by `callback`. If `callback` throws an
   *   error, then it will be rejected with the error.
   */
  queueIdleCallback(callback) {
    return this.queue(async () => {
      await new Promise((resolve, reject) => {
        ChromeUtils.idleDispatch(async () => {
          try {
            let value = await callback();
            resolve(value);
          } catch (error) {
            console.error(error);
            reject(error);
          }
        });
      });
    });
  }

  /**
   * Calls the next function in the task queue and recurses until the queue is
   * empty. Once empty, all empty callback functions are called.
   */
  async #doNextTask() {
    if (!this.#queue.length) {
      this.#emptyDeferred.resolve();
      this.#emptyDeferred = null;
      return;
    }

    // Leave the callback in the queue while awaiting it. If we remove it now
    // the queue could become empty, and if `queue()` were called while we're
    // awaiting the callback, `#doNextTask()` would be re-entered.
    let { callback, resolve, reject } = this.#queue[0];
    try {
      let value = await callback();
      resolve(value);
    } catch (error) {
      console.error(error);
      reject(error);
    }
    this.#queue.shift();
    this.#doNextTask();
  }

  #queue = [];
  #emptyDeferred = null;
  #emptyPromise = Promise.resolve();
}
