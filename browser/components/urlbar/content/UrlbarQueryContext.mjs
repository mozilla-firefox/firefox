/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports UrlbarQueryContext, the object describing a single urlbar
 * query. It can be imported into system and content realms, so it should not
 * use content-only globals like `window`.
 */

import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";
import { UrlbarResult } from "chrome://browser/content/urlbar/UrlbarResult.mjs";

const { XPCOMUtils } =
  typeof ChromeUtils == "undefined"
    ? { XPCOMUtils: null }
    : ChromeUtils.importESModule("resource://gre/modules/XPCOMUtils.sys.mjs");

const lazy = XPCOMUtils?.declareLazy({
  TemporalSuggestions:
    "moz-src:///browser/components/urlbar/private/TemporalSuggestions.sys.mjs",
});

/**
 * @import {UrlbarSearchStringTokenData} from "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs"
 * @import {SearchMode} from "moz-src:///browser/components/urlbar/content/UrlbarInputBase.mjs"
 */

// Source of the per-context `id`. Ids only have to be unique among the queries
// of a given input, which all originate in the same realm as this counter.
let gNextId = 1;

/**
 * UrlbarQueryContext defines a user's autocomplete input from within the urlbar.
 * It supplements it with details of how the search results should be obtained
 * and what they consist of.
 */
export class UrlbarQueryContext {
  /**
   * Constructs the UrlbarQueryContext instance.
   *
   * @param {object} options
   *   The initial options for UrlbarQueryContext.
   * @param {UrlbarSapName} options.sapName
   *   The search access point name of the UrlbarInput.
   * @param {string} options.searchString
   *   The string the user entered in autocomplete. Could be the empty string
   *   in the case of the user opening the popup via the mouse.
   * @param {boolean} options.isPrivate
   *   Set to true if this query was started from a private browsing window.
   * @param {number} options.maxResults
   *   The maximum number of results that will be displayed for this query.
   * @param {boolean} options.allowAutofill
   *   Whether or not to allow providers to include autofill results.
   * @param {number} [options.userContextId]
   *   The container id where this context was generated, if any.
   * @param {string | null} [options.tabGroup]
   *   The tab group where this context was generated, if any.
   * @param {Array} [options.sources]
   *   A list of acceptable UrlbarShared.RESULT_SOURCE for the context.
   * @param {object} [options.searchMode]
   *   The input's current search mode.  See UrlbarInput.setSearchMode for a
   *   description.
   * @param {boolean} [options.prohibitRemoteResults]
   *   This provides a short-circuit override for `context.allowRemoteResults`.
   *   If it's false, then `allowRemoteResults` will do its usual checks to
   *   determine whether remote results are allowed. If it's true, then
   *   `allowRemoteResults` will immediately return false. Defaults to false.
   */
  constructor(options) {
    // Clone to make sure all properties belong to the system realm.
    // This is required because this method is called from a window.
    // Not doing this causes a window leak if providers don't properly
    // clean up after a query and keep references to UrlbarQueryContext
    // properties (e.g. ProviderPlaces).
    options = structuredClone(options);

    this._checkRequiredOptions(options, [
      "allowAutofill",
      "isPrivate",
      "maxResults",
      "sapName",
      "searchString",
    ]);

    if (isNaN(options.maxResults)) {
      throw new Error(
        `Invalid maxResults property provided to UrlbarQueryContext`
      );
    }

    /**
     * @type {[string, (v: any) => boolean, any?][]}
     */
    const optionalProperties = [
      ["currentPage", v => typeof v == "string" && !!v.length],
      ["excludeSponsoredResults", v => typeof v == "boolean", false],
      ["prohibitRemoteResults", v => typeof v == "boolean", false],
      ["providers", v => Array.isArray(v) && !!v.length],
      ["searchMode", v => v && typeof v == "object"],
      ["sources", v => Array.isArray(v) && !!v.length],
    ];

    // Manage optional properties of options.
    for (let [prop, checkFn, defaultValue] of optionalProperties) {
      if (prop in options) {
        if (!checkFn(options[prop])) {
          throw new Error(`Invalid value for option "${prop}"`);
        }
        this[prop] = options[prop];
      } else if (defaultValue !== undefined) {
        this[prop] = defaultValue;
      }
    }

    this.id = gNextId++;
    this.lastResultCount = 0;
    // Note that Set is not serializable through JSON, so these may not be
    // easily shared with add-ons.
    this.pendingHeuristicProviders = new Set();
    this.deferUserSelectionProviders = new Set();
    this.trimmedSearchString = this.searchString.trim();
    this.lowerCaseSearchString = this.searchString.toLowerCase();
    this.trimmedLowerCaseSearchString = this.trimmedSearchString.toLowerCase();
    this.userContextId = UrlbarShared.normalizedUserContextId(
      options.userContextId,
      this.isPrivate
    );
    this.tabGroup = options.tabGroup || null;

    // Used to store glean timing distribution timer ids.
    this.firstTimerId = 0;
    this.sixthTimerId = 0;
  }

  /**
   * Whether or not to allow providers to include autofill results.
   *
   * @type {boolean}
   */
  allowAutofill;

  /**
   * Whether or not the query has been cancelled.
   *
   * @type {boolean}
   */
  canceled = false;

  /**
   * URL of the page that was loaded when the search began. Only set in a
   * browser window; a content urlbar leaves it undefined.
   *
   * @type {string}
   */
  currentPage;

  /**
   * The container id the query runs in, normalized to the id the open-pages
   * table is keyed by (some providers read it directly). Only set in a browser
   * window; a content urlbar keeps the default 0. Assign it from
   * UrlbarShared.normalizedUserContextId().
   *
   * @type {number}
   */
  userContextId = 0;

  /**
   * The tab group the query runs in. Only set in a browser window; a content
   * urlbar keeps the default null.
   *
   * @type {string | null}
   */
  tabGroup = null;

  /**
   * The current firstResult.
   *
   * @type {UrlbarResult}
   */
  firstResult;

  /**
   * Indicates if the first result has been changed changed.
   *
   * @type {boolean}
   */
  firstResultChanged = false;

  /**
   * The heuristic result associated with the context.
   *
   * @type {UrlbarResult}
   */
  heuristicResult;

  /**
   * Identifies this query among the ones its input started. It survives the
   * trip across the actor boundary, so a notification can be matched with the
   * query it belongs to, and results of a superseded query discarded.
   *
   * @type {number}
   */
  id;

  /**
   * True if this query was started from a private browsing window.
   *
   * @type {boolean}
   */
  isPrivate;

  /**
   * The `maxResults` value passed to the constructor or last assigned.
   *
   * @type {number}
   */
  _maxResults;

  /**
   * The maximum number of results that should be displayed for this query.
   *
   * @type {number}
   */
  get maxResults() {
    // When temporal suggestions are active and the SAP is urlbar, override the
    // usual `maxRichResults` unless the user modified it.
    if (
      this.isTemporalZeroPrefix &&
      this._maxResults == UrlbarPrefs.get("maxRichResults") &&
      !UrlbarPrefs.hasUserValue("maxRichResults")
    ) {
      return UrlbarPrefs.get("temporal.zeroPrefixMaxRichResults");
    }
    return this._maxResults;
  }

  set maxResults(value) {
    this._maxResults = value;
  }

  /**
   * Whether the zero prefix layout and result count for temporal search
   * suggestions apply to this query. They only apply to the urlbar SAP, and
   * only when `TemporalSuggestions.isEnabled` is true.
   *
   * @type {boolean}
   */
  get isTemporalZeroPrefix() {
    return (
      this.sapName == "urlbar" &&
      !!lazy?.TemporalSuggestions.isEnabled &&
      !this.searchString &&
      !this.searchMode
    );
  }

  /**
   * Whether search suggestions should be shown before general results.
   *
   * @type {boolean}
   */
  get showSearchSuggestionsFirst() {
    switch (this.sapName) {
      case "urlbar":
        return this._getShowSearchSuggestionsFirst(
          "showSearchSuggestionsFirst"
        );
      case "smartbar":
        return this._getShowSearchSuggestionsFirst(
          "smartbar.showSearchSuggestionsFirst"
        );
      default:
        return true;
    }
  }

  /**
   * Returns whether search suggestions should be shown before general results.
   *
   * @param {string} pref
   *   The SAP's `showSearchSuggestionsFirst` pref.
   * @returns {boolean}
   */
  _getShowSearchSuggestionsFirst(pref) {
    if (this.isTemporalZeroPrefix) {
      return false;
    }

    let showSearchSuggestionsFirst =
      !!this.searchString ||
      (!UrlbarPrefs.get("suggest.trending") &&
        !UrlbarPrefs.get("suggest.recentsearches"));

    let inSearchEngineMode = !!this.searchMode?.engineName;

    // If we're in a case were search suggestions would be shown first, but not
    // in search engine mode, then just use the preference.
    if (!inSearchEngineMode && showSearchSuggestionsFirst) {
      showSearchSuggestionsFirst = UrlbarPrefs.get(pref);
    }
    return showSearchSuggestionsFirst;
  }

  /**
   * The name of the muxer to use for this query.
   *
   * @type {string}
   */
  muxer;

  /**
   * Whether or not to exclude sponsored results.
   *
   * @type {boolean}
   */
  excludeSponsoredResults;

  /**
   * Whether or not to prohibit remote results.
   *
   * @type {boolean}
   */
  prohibitRemoteResults;

  /**
   * List of registered provider names. Providers can be registered through
   * the ProvidersManager.
   *
   * @type {string[]}
   */
  providers;

  /**
   * Set if this context is restricted to a single source.
   *
   * @type {?Values<typeof UrlbarShared.RESULT_SOURCE>}
   */
  restrictSource;

  /**
   * The restriction token used to restrict the sources for this search.
   *
   * @type {UrlbarSearchStringTokenData}
   */
  restrictToken;

  /**
   * The results associated with this context.
   *
   * @type {UrlbarResult[]}
   */
  results;

  /**
   * The search access point name of the UrlbarInput.
   *
   * @type {UrlbarSapName}
   */
  sapName;

  /**
   * Whether the query runs in a bar dedicated to search.
   *
   * See `UrlbarShared.isSearchbarSAP`.
   *
   * @type {boolean}
   */
  get isSearchbarSAP() {
    return UrlbarShared.isSearchbarSAP(this.sapName);
  }

  /**
   * Whether a string that isn't a URL may be searched for.
   *
   * See `UrlbarShared.keywordEnabled`.
   *
   * @type {boolean}
   */
  get keywordEnabled() {
    return UrlbarShared.keywordEnabled(this.sapName);
  }

  /**
   * Whether a string that is a URL may be navigated to.
   *
   * See `UrlbarShared.navigationEnabled`.
   *
   * @type {boolean}
   */
  get navigationEnabled() {
    return UrlbarShared.navigationEnabled(this.sapName);
  }

  /**
   * Whether a string that is a URL may be navigated to in an engine search
   * mode.
   *
   * See `UrlbarShared.navigationInSearchModeEnabled`.
   *
   * @type {boolean}
   */
  get navigationInSearchModeEnabled() {
    return UrlbarShared.navigationInSearchModeEnabled(this.sapName);
  }

  /**
   * Details about the search mode associated with this context.
   *
   * @type {SearchMode}
   */
  searchMode;

  /**
   * Utility function to determine whether we should use the existence of
   * searchMode to restrict the type of results to only search suggestions
   * or the new behaviour behind `historyInSearchMode` pref that shows all
   * types of results in searchMode, treating engine searchMode as
   * "temporarily changed default search engine".
   *
   * @returns {boolean}
   */
  restrictInSearchMode() {
    if (UrlbarPrefs.get("unifiedSearchButton.historyInSearchMode")) {
      // We still want to restrict local searchModes even if the pref is on.
      return !!this.searchMode && !this.searchMode.engineName;
    }
    return !!this.searchMode;
  }

  /**
   * The string the user entered in autocomplete.
   *
   * @type {string}
   */
  searchString;

  /**
   * The possible sources of results for this context.
   *
   * @type {Values<typeof UrlbarShared.RESULT_SOURCE>[]}
   */
  sources;

  /**
   * A list of tokens extracted from the search string.
   *
   * @type {UrlbarSearchStringTokenData[]}
   */
  tokens;

  /**
   * Checks the required options, saving them as it goes.
   *
   * @param {object} options The options object to check.
   * @param {Array} optionNames The names of the options to check for.
   * @throws {Error} Throws if there is a missing option.
   */
  _checkRequiredOptions(options, optionNames) {
    for (let optionName of optionNames) {
      if (!(optionName in options)) {
        throw new Error(
          `Missing or empty ${optionName} provided to UrlbarQueryContext`
        );
      }
      this[optionName] = options[optionName];
    }
  }

  /**
   * Caches and returns fixup info from URIFixup for the current search string.
   * Only returns a subset of the properties from URIFixup. This is both to
   * reduce the memory footprint of UrlbarQueryContexts and to keep them
   * serializable so they can be sent to extensions.
   *
   * IMPORTANT: This uses `Services`, so it only works in privileged code.
   */
  get fixupInfo() {
    if (!this._fixupError && !this._fixupInfo && this.trimmedSearchString) {
      let flags =
        Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
        Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
      if (this.isSearchbarSAP) {
        flags |= Ci.nsIURIFixup.FIXUP_FLAG_FORCE_KEYWORD_LOOKUP;
      }
      if (this.isPrivate) {
        flags |= Ci.nsIURIFixup.FIXUP_FLAG_PRIVATE_CONTEXT;
      }

      try {
        let info = Services.uriFixup.getFixupURIInfo(this.searchString, flags);

        this._fixupInfo = {
          href: info.fixedURI.spec,
          isSearch: !!info.keywordAsSent,
          scheme: info.fixedURI.scheme,
        };
      } catch (ex) {
        this._fixupError = ex.result;
      }
    }

    return this._fixupInfo || null;
  }

  /**
   * Returns the error that was thrown when fixupInfo was fetched, if any. If
   * fixupInfo has not yet been fetched for this queryContext, it is fetched
   * here.
   *
   * @returns {any?}
   */
  get fixupError() {
    if (!this.fixupInfo) {
      return this._fixupError;
    }

    return null;
  }

  /**
   * Returns whether results from remote services are generally allowed for the
   * context. Callers can impose further restrictions as appropriate, but
   * typically they should not fetch remote results if this returns false.
   *
   * @param {string} [searchString]
   *   Usually this is just the context's search string, but if you need to
   *   fetch remote results based on a modified version, you can pass it here.
   * @param {boolean} [allowEmptySearchString]
   *   Whether to check for the minimum length of the search string.
   * @returns {boolean}
   *   Whether remote results are allowed.
   */
  allowRemoteResults(
    searchString = this.searchString,
    allowEmptySearchString = false
  ) {
    if (this.prohibitRemoteResults) {
      return false;
    }

    // We're unlikely to get useful remote results for a single character.
    if (
      searchString.length < 2 &&
      !(!searchString.length && allowEmptySearchString)
    ) {
      return false;
    }

    // Prohibit remote results if the search string is likely an origin to avoid
    // disclosing sites the user visits. If the search string may or may not be
    // an origin but we've determined a search is allowed, then allow it.
    if (this.tokens.length == 1) {
      switch (this.tokens[0].type) {
        case UrlbarShared.TOKEN_TYPE.POSSIBLE_ORIGIN:
          return false;
        case UrlbarShared.TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED:
          return true;
      }
    }

    // Disallow remote results for strings containing tokens that look like URIs
    // to avoid disclosing information about networks and passwords.
    // A SAP that can't navigate has nothing to disclose: a URL typed there is
    // only ever a search string.
    if (
      this.navigationEnabled &&
      this.fixupInfo?.href &&
      !this.fixupInfo?.isSearch
    ) {
      return false;
    }

    // Allow remote results.
    return true;
  }

  /**
   * Serializes this context to a plain, structured-cloneable object for sending
   * across the Urlbar actor boundary. The context's own fields survive
   * structured-clone on their own, but its nested UrlbarResults keep their data
   * in private fields, so they're replaced with their wire forms.
   *
   * @returns {object} The wire representation; reconstruct with fromWire().
   */
  toWire() {
    return {
      ...this,
      results: this.results?.map(result => result.toWire()),
      heuristicResult: this.heuristicResult?.toWire(),
    };
  }

  /**
   * Reconstructs a UrlbarQueryContext from the plain object produced by
   * toWire(), e.g. after it has crossed the Urlbar actor boundary: structured-
   * clone preserved the context's own fields but dropped its class identity and
   * its results' private-field data, so restore the prototype and rebuild the
   * results.
   *
   * @param {object} wire The wire representation from toWire().
   * @returns {UrlbarQueryContext} The reconstructed context.
   */
  static fromWire(wire) {
    Object.setPrototypeOf(wire, UrlbarQueryContext.prototype);
    wire.results = wire.results?.map(r => UrlbarResult.fromWire(r)) ?? [];
    if (wire.heuristicResult) {
      wire.heuristicResult = UrlbarResult.fromWire(wire.heuristicResult);
    }
    return wire;
  }
}

// In chrome window globals, we re-export the UrlbarQueryContext from the system
// global. Otherwise, UrlbarQueryContexts created in a window global but cached
// in the system global would leak the window.
if (typeof ChromeUtils != "undefined" && typeof window != "undefined") {
  // @ts-ignore
  // eslint-disable-next-line no-class-assign
  ({ UrlbarQueryContext } = ChromeUtils.importESModule(
    "chrome://browser/content/urlbar/UrlbarQueryContext.mjs"
  ));
}
