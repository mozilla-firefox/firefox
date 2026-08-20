/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports urlbar related constants and helpers. It can be imported
 * into system and content realms, so it should not use content-only globals
 * like `window`. Any state it holds is per-realm (each importing realm gets
 * its own copy of the module).
 */

import * as UrlbarContentUtils from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";
import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";

/**
 * @typedef {object} LocalSearchMode
 *   Represents a local search mode, e.g. bookmarks.
 *
 * @property {Values<typeof UrlbarShared.RESULT_SOURCE>} source
 *   The source which the search mode will search.
 * @property {Values<typeof UrlbarShared.RESTRICT_TOKENS>} restrict
 *   The restrict token that is associated with the search (*, %, $ etc).
 * @property {string} icon
 *   The URL of the icon associated with the search mode in preferences.
 * @property {string} pref
 *   The suffix of the preference associated with if the mode is displayed
 *   in the lists or not (prefix with `browser.urlbar.`).
 * @property {string} telemetryLabel
 *   The telemetry label for recording searches in this mode.
 * @property {string} uiLabel
 *   The L10n ID to use for the UI label.
 *   Has a value and an accesskey attribute.
 */

/**
 * @typedef {object} UrlLoad
 *   A direct URL load.
 * @property {string} url
 *   The url to load.
 * @property {?string} postData
 *   The post data, or null for a GET.
 */

/**
 * @typedef {object} EngineSearchLoad
 *   A search to submit to an engine.
 * @property {string} engineName
 *   The name of the search engine.
 * @property {string} query
 *   The query.
 */

/**
 * @typedef {{urlLoad: UrlLoad, engineSearch?: never} |
 *           {engineSearch: EngineSearchLoad, urlLoad?: never}} UrlbarLoadRequest
 *  Either a URL or an engine search.
 */

/**
 * @typedef {object} URIFixupPrimitives
 *   The parts of an `nsIURIFixupInfo` that survive the actor boundary, so a
 *   content-realm consumer never holds an XPCOM object. Produced by
 *   `UrlbarUtils.getFixupPrimitives()`.
 *
 * @property {string} keywordAsSent
 *   The keyword the string was turned into, empty if it wasn't a keyword
 *   search.
 * @property {?string} preferredURIDisplaySpec
 *   The display spec of the preferred URI, if there is one.
 */

// The userContextId used in the moz_openpages_temp table for tabs in private
// windows. Real container ids are non-negative, so -1 is a safe sentinel.
const PRIVATE_USER_CONTEXT_ID = -1;

// `nsIScriptSecurityManager.DEFAULT_USER_CONTEXT_ID`, which a content realm has
// no `Ci` to read it from.
const DEFAULT_USER_CONTEXT_ID = 0;

export const UrlbarShared = {
  /**
   * Measures an element without flushing layout where that is possible.
   * `windowUtils` is chrome-only, so a content realm takes the flushing path.
   *
   * @param {Element} element
   *   The element to measure.
   * @returns {DOMRect}
   */
  getBoundsWithoutFlushing:
    typeof ChromeUtils != "undefined"
      ? element =>
          element.documentGlobal.windowUtils.getBoundsWithoutFlushing(element)
      : element => element.getBoundingClientRect(),

  /**
   * Checks whether a value implements a DOM interface. `isInstance` is
   * chrome-only, and unlike `instanceof` it holds for a value from another
   * global; a content realm compares against its own interface object.
   *
   * @param {any} value
   *   The value to check.
   * @param {object} iface
   *   The interface, e.g. `KeyboardEvent`.
   * @returns {boolean}
   */
  isInstance:
    typeof ChromeUtils != "undefined"
      ? (value, iface) => iface.isInstance(value)
      : (value, iface) => value instanceof iface,

  // REGEXP_ constants are duplicated from UrlUtils.sys.mjs
  // Regex matching on whitespaces.
  REGEXP_SPACES: /\s+/,
  // Regex matching scheme and colon, plus, if present, two slashes.
  REGEXP_PREFIX: /^[a-z-]+:(?:\/){0,2}/i,

  // The notifications the controller dispatches to its listeners. Defined here
  // so both the parent controller and the content-side child controller name
  // them identically without the child reaching into the parent module.
  NOTIFICATIONS: Object.freeze({
    QUERY_STARTED: "onQueryStarted",
    // Fires when the first result changed, before QUERY_RESULTS, so the input
    // can react to it (enter search mode, apply autofill) before the results
    // are shown.
    QUERY_FIRST_RESULT: "onFirstResult",
    QUERY_RESULTS: "onQueryResults",
    QUERY_RESULT_REMOVED: "onQueryResultRemoved",
    // Fires when no further results will reach the listeners. The parent
    // cancels a query when a new one supersedes it and when the view closes or
    // freezes its rows; the child controller cancels one whose results it
    // withholds.
    QUERY_CANCELLED: "onQueryCancelled",
    QUERY_FINISHED: "onQueryFinished",
    VIEW_OPEN: "onViewOpen",
    VIEW_CLOSE: "onViewClose",
    // Fires after the picked result's provider handled the engagement (its
    // `onEngagement` hook ran), letting content-side listeners await a
    // provider's parent-side engagement side effect, which lands after an actor
    // round-trip on the message path.
    PROVIDER_ENGAGEMENT: "onProviderEngagement",
  }),

  TOKEN_TYPE: Object.freeze({
    TEXT: 1,
    // `looksLikeOrigin()` returned a value for this token that was neither
    // `LOOKS_LIKE_ORIGIN.NONE` nor `LOOKS_LIKE_ORIGIN.OTHER`. It sure looks
    // like an origin.
    POSSIBLE_ORIGIN: 2,
    POSSIBLE_URL: 3, // Consumers should still check this with a fixup.
    RESTRICT_HISTORY: 4,
    RESTRICT_BOOKMARK: 5,
    RESTRICT_TAG: 6,
    RESTRICT_OPENPAGE: 7,
    RESTRICT_SEARCH: 8,
    RESTRICT_TITLE: 9,
    RESTRICT_URL: 10,
    RESTRICT_ACTION: 11,
    // `looksLikeOrigin()` returned `LOOKS_LIKE_ORIGIN.OTHER` for this token.
    // It may or may not be an origin.
    POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED: 12,
  }),

  /**
   * Special characters that can be typed to restrict the search to a certain
   * category, like history, bookmarks or open pages; or to force a match on
   * just the title or url.
   *
   * These restriction characters can be typed alone, or at word boundaries,
   * provided their meaning cannot be confused, for example # could be present
   * in a valid url, and thus it should not be interpreted as a restriction.
   */
  RESTRICT_TOKENS: Object.freeze({
    HISTORY: "^",
    BOOKMARK: "*",
    TAG: "+",
    OPENPAGE: "%",
    SEARCH: "?",
    TITLE: "#",
    URL: "$",
    ACTION: ">",
  }),

  // Defines UrlbarResult types.
  RESULT_TYPE: Object.freeze({
    // An open tab.
    TAB_SWITCH: 1,
    // A search suggestion or engine.
    SEARCH: 2,
    // A common url/title tuple, may be a bookmark with tags.
    URL: 3,
    // A bookmark keyword.
    KEYWORD: 4,
    // A WebExtension Omnibox result.
    OMNIBOX: 5,
    // A tab from another synced device.
    REMOTE_TAB: 6,
    // An actionable message to help the user with their query.
    TIP: 7,
    // A type of result which layout is defined at runtime.
    DYNAMIC: 8,
    // A restrict keyword result, could be @bookmarks, @history, or @tabs.
    RESTRICT: 9,
    // An AI chat result.
    AI_CHAT: 10,

    // When you add a new type, also add its schema to
    // UrlbarUtils.RESULT_PAYLOAD_SCHEMA. Also consider checking if
    // consumers of "urlbar-user-start-navigation" need updating.
  }),

  // This defines the source of results returned by a provider. Each provider
  // can return results from more than one source. This is used by the
  // ProvidersManager to decide which providers must be queried and which
  // results can be returned.
  // If you add new source types, consider checking if consumers of
  // "urlbar-user-start-navigation" need update as well.
  RESULT_SOURCE: Object.freeze({
    BOOKMARKS: 1,
    HISTORY: 2,
    SEARCH: 3,
    TABS: 4,
    OTHER_LOCAL: 5,
    OTHER_NETWORK: 6,
    ADDON: 7,
    ACTIONS: 8,
  }),

  // Results are categorized into groups to help the muxer compose them.  See
  // getResultGroup.  Since result groups are stored in result
  // groups and result groups are stored in prefs, additions and changes to
  // result groups may require adding UI migrations to BrowserGlue.  Be careful
  // about making trivial changes to existing groups, like renaming them,
  // because we don't want to make downgrades unnecessarily hard.
  RESULT_GROUP: Object.freeze({
    ABOUT_PAGES: "aboutPages",
    AI: "ai",
    GENERAL: "general",
    GENERAL_PARENT: "generalParent",
    FORM_HISTORY: "formHistory",
    HEURISTIC_AUTOFILL: "heuristicAutofill",
    HEURISTIC_AI_CHAT: "heuristicAiChat",
    HEURISTIC_ENGINE_ALIAS: "heuristicEngineAlias",
    HEURISTIC_EXTENSION: "heuristicExtension",
    HEURISTIC_FALLBACK: "heuristicFallback",
    HEURISTIC_BOOKMARK_KEYWORD: "heuristicBookmarkKeyword",
    HEURISTIC_HISTORY_URL: "heuristicHistoryUrl",
    HEURISTIC_OMNIBOX: "heuristicOmnibox",
    HEURISTIC_RESTRICT_KEYWORD_AUTOFILL: "heuristicRestrictKeywordAutofill",
    HEURISTIC_SEARCH_TIP: "heuristicSearchTip",
    HEURISTIC_TEST: "heuristicTest",
    HEURISTIC_TOKEN_ALIAS_ENGINE: "heuristicTokenAliasEngine",
    INPUT_HISTORY: "inputHistory",
    OMNIBOX: "extension",
    RECENT_SEARCH: "recentSearch",
    REMOTE_SUGGESTION: "remoteSuggestion",
    REMOTE_TAB: "remoteTab",
    RESTRICT_SEARCH_KEYWORD: "restrictSearchKeyword",
    SEMANTIC_HISTORY: "semanticHistory",
    SUGGESTED_INDEX: "suggestedIndex",
    TAIL_SUGGESTION: "tailSuggestion",
  }),

  // Defines provider types.
  PROVIDER_TYPE: Object.freeze({
    // Should be executed immediately, because it returns heuristic results
    // that must be handed to the user asap.
    // WARNING: these providers must be extremely fast, because the urlbar will
    // await for them before returning results to the user. In particular it is
    // critical to reply quickly to isActive and startQuery.
    HEURISTIC: 1,
    // Can be delayed, contains results coming from the session or the profile.
    PROFILE: 2,
    // Can be delayed, contains results coming from the network.
    NETWORK: 3,
    // Can be delayed, contains results coming from unknown sources.
    EXTENSION: 4,
  }),

  // The tip types UrlbarProviderInterventions can show, i.e. the `type` in the
  // payload of its results.
  INTERVENTION_TIP_TYPE: Object.freeze({
    NONE: "",
    CLEAR: "intervention_clear",
    REFRESH: "intervention_refresh",

    // There's an update available, but the user's pref says we should ask them
    // to download and apply it.
    UPDATE_ASK: "intervention_update_ask",

    // The updater is currently checking.  We don't actually show a tip for this,
    // but we use it to tell whether we should wait for the check to complete in
    // startQuery.  See startQuery for details.
    UPDATE_CHECKING: "intervention_update_checking",

    // The user's browser is up to date, but they triggered the update
    // intervention. We show this special refresh intervention instead.
    UPDATE_REFRESH: "intervention_update_refresh",

    // There's an update and it's been downloaded and applied. The user needs to
    // restart to finish.
    UPDATE_RESTART: "intervention_update_restart",

    // We can't update the browser or possibly even check for updates for some
    // reason, so the user should download the latest version from the web.
    UPDATE_WEB: "intervention_update_web",
  }),

  // The tip types UrlbarProviderSearchTips can show, i.e. the `type` in the
  // payload of its results.
  SEARCH_TIP_TYPE: Object.freeze({
    NONE: "",
    ONBOARD: "searchTip_onboard",
    REDIRECT: "searchTip_redirect",
  }),

  // Per-result exposure telemetry.
  EXPOSURE_TELEMETRY: {
    // Exposure telemetry will not be recorded for the result.
    NONE: 0,
    // Exposure telemetry will be recorded for the result and the result will be
    // visible in the view as usual.
    SHOWN: 1,
    // Exposure telemetry will be recorded for the result but the result will
    // not be present in the view.
    HIDDEN: 2,
  },

  // This defines icon locations that are commonly used in the UI.
  ICON: {
    EXTENSION: "chrome://mozapps/skin/extensions/extension.svg",
    HISTORY: "chrome://browser/skin/history.svg",
    SEARCH_GLASS: "chrome://global/skin/icons/search-glass.svg",
    TRENDING: "chrome://global/skin/icons/trending.svg",
    TIP: "chrome://global/skin/icons/lightbulb.svg",
    GLOBE: "chrome://global/skin/icons/defaultFavicon.svg",
    DEFAULT: "chrome://global/skin/icons/defaultFavicon.svg",
  },

  // The number of results by which Page Up/Down move the selection.
  PAGE_UP_DOWN_DELTA: 5,

  // IME composition states.
  COMPOSITION: {
    NONE: 1,
    COMPOSING: 2,
    COMMIT: 3,
    CANCELED: 4,
  },

  // Limit the length of titles and URLs we display so layout doesn't spend too
  // much time building text runs.
  MAX_TEXT_LENGTH: 255,

  // Whether a result should be highlighted up to the point the user has typed
  // or after that point.
  HIGHLIGHT: Object.freeze({
    TYPED: 1,
    SUGGESTED: 2,
    ALL: 3,
  }),

  // UrlbarProviderPlaces's autocomplete results store their titles and tags
  // together in their comments.  This separator is used to separate them.
  // After bug 1717511, we should stop using this old hack and store titles and
  // tags separately.  It's important that this be a character that no title
  // would ever have.  We use \x1F, the non-printable unit separator.
  TITLE_TAGS_SEPARATOR: "\x1F",

  // Regex matching single word hosts with an optional port; no spaces, auth or
  // path-like chars are admitted.
  REGEXP_SINGLE_WORD: /^[^\s@:/?#]+(:\d+)?$/,

  // Valid entry points for search mode. If adding a value here, please update
  // telemetry documentation and metrics.yaml.
  SEARCH_MODE_ENTRY: new Set([
    "bookmarkmenu",
    "handoff",
    "keywordoffer",
    "messagingSystem",
    "oneoff",
    "historymenu",
    "other",
    "searchbutton",
    "shortcut",
    "tabmenu",
    "tabtosearch",
    "tabtosearch_onboard",
    "topsites_newtab",
    "topsites_urlbar",
    "touchbar",
    "typed",
  ]),

  // The favicon service stores icons for URLs with the following protocols.
  PROTOCOLS_WITH_ICONS: ["about:", "http:", "https:", "file:"],

  // Valid URI schemes that are considered safe but don't contain
  // an authority component (e.g host:port). There are many URI schemes
  // that do not contain an authority, but these in particular have
  // some likelihood of being entered or bookmarked by a user.
  // `file:` is an exceptional case because an authority is optional
  PROTOCOLS_WITHOUT_AUTHORITY: [
    "about:",
    "data:",
    "file:",
    "javascript:",
    "view-source:",
  ],

  // Search mode objects corresponding to the local shortcuts in the view, in
  // order they appear.  Pref names are relative to the `browser.urlbar` branch.
  get LOCAL_SEARCH_MODES() {
    return /** @type {LocalSearchMode[]} */ ([
      {
        source: this.RESULT_SOURCE.BOOKMARKS,
        restrict: this.RESTRICT_TOKENS.BOOKMARK,
        icon: "chrome://browser/skin/bookmark.svg",
        pref: "shortcuts.bookmarks",
        telemetryLabel: "bookmarks",
        uiLabel: "urlbar-searchmode-bookmarks3",
      },
      {
        source: this.RESULT_SOURCE.TABS,
        restrict: this.RESTRICT_TOKENS.OPENPAGE,
        icon: "chrome://browser/skin/open-tabs.svg",
        pref: "shortcuts.tabs",
        telemetryLabel: "tabs",
        uiLabel: "urlbar-searchmode-tabs3",
      },
      {
        source: this.RESULT_SOURCE.HISTORY,
        restrict: this.RESTRICT_TOKENS.HISTORY,
        icon: "chrome://browser/skin/history.svg",
        pref: "shortcuts.history",
        telemetryLabel: "history",
        uiLabel: "urlbar-searchmode-history3",
      },
      {
        source: this.RESULT_SOURCE.ACTIONS,
        restrict: this.RESTRICT_TOKENS.ACTION,
        icon: "chrome://browser/skin/lightning-bolt.svg",
        pref: "shortcuts.actions",
        telemetryLabel: "actions",
        uiLabel: "urlbar-searchmode-actions3",
      },
    ]);
  },

  /**
   * Set of characters in RESTRICT_TOKENS that will enter search mode.
   */
  get SEARCH_MODE_RESTRICT() {
    /** @type {Values<typeof UrlbarShared.RESTRICT_TOKENS>[]} */
    const keys = [
      this.RESTRICT_TOKENS.HISTORY,
      this.RESTRICT_TOKENS.BOOKMARK,
      this.RESTRICT_TOKENS.OPENPAGE,
      this.RESTRICT_TOKENS.SEARCH,
    ];
    if (UrlbarPrefs.get("scotchBonnet.enableOverride")) {
      keys.push(this.RESTRICT_TOKENS.ACTION);
    }
    return new Set(keys);
  },

  PRIVATE_USER_CONTEXT_ID,

  /**
   * Return userContextId that is used in the moz_openpages_temp table and
   * returned as part of the payload. It differs only for private windows.
   *
   * @param {number} userContextId Containers user context id
   * @param {boolean} isInPrivateWindow In private browsing window or not
   * @returns {number} userContextId
   */
  getUserContextIdForOpenPagesTable(userContextId, isInPrivateWindow) {
    return isInPrivateWindow ? PRIVATE_USER_CONTEXT_ID : userContextId;
  },

  /**
   * Normalizes a raw container id to the id the open-pages table is keyed by,
   * for the given private state, falling back to the default container id.
   * Callers assign the result to a query context's userContextId, which some
   * providers read directly.
   *
   * @param {number} userContextId The raw container id.
   * @param {boolean} isInPrivateWindow Whether the query is private.
   * @returns {number} The normalized container id.
   */
  normalizedUserContextId(userContextId, isInPrivateWindow) {
    return (
      this.getUserContextIdForOpenPagesTable(
        userContextId,
        isInPrivateWindow
      ) || DEFAULT_USER_CONTEXT_ID
    );
  },

  /**
   * Return whether the provided userContextId is for a non-private tab.
   *
   * @param {number} userContextId the userContextId to evaluate
   * @returns {boolean}
   */
  isNonPrivateUserContextId(userContextId) {
    return userContextId != PRIVATE_USER_CONTEXT_ID;
  },

  /**
   * Return whether the provided userContextId is for a container.
   *
   * @param {number} userContextId the userContextId to evaluate
   * @returns {boolean}
   */
  isContainerUserContextId(userContextId) {
    return userContextId > 0;
  },

  /**
   * Creates a console logger.
   *
   * In non-chrome contexts, `console.createInstance` is unavailable, so this
   * falls back to the global `console`.
   *
   * @param {object} [options]
   * @param {string} [options.prefix]
   *   Prefix to use for the logged messages.
   * @param {string} [options.maxLogLevelPref]
   *   The pref holding the maximum log level. It has to be known to
   *   `UrlbarPrefs`, which is how the logger reads it outside chrome.
   * @returns {Console}
   *   The console logger.
   */
  getLogger({ prefix = "", maxLogLevelPref = "browser.urlbar.loglevel" } = {}) {
    let logger = loggers.get(prefix);
    if (logger) {
      return logger;
    }

    let fullPrefix = `URLBar${prefix ? " - " + prefix : ""}`;
    if (console.createInstance) {
      logger = createLoggerChrome(fullPrefix, maxLogLevelPref);
    } else {
      logger = createLoggerContent(fullPrefix, maxLogLevelPref);
    }
    loggers.set(prefix, logger);
    return logger;
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
   * @returns {?UrlbarLoadRequest}
   *   Null if the result has nothing to load.
   */
  getLoadRequestFromResult(result, { element = null } = {}) {
    if (
      result.payload.engine &&
      (result.type == UrlbarShared.RESULT_TYPE.SEARCH ||
        result.type == UrlbarShared.RESULT_TYPE.DYNAMIC)
    ) {
      let query =
        element?.dataset.query ||
        result.payload.suggestion ||
        result.payload.query;
      if (query) {
        return { engineSearch: { query, engineName: result.payload.engine } };
      }
    }

    if (!result.payload.url) {
      return null;
    }

    return {
      urlLoad: {
        url: result.payload.url,
        postData: result.payload.postData ?? null,
      },
    };
  },

  /**
   * Deep-equality check for plain JSON-like data (arrays, objects, primitives),
   * so content-realm modules needn't import ObjectUtils (a system module). Not a
   * general-purpose deepEqual: it treats Map/Set/Date/RegExp/typed arrays as
   * plain objects, so only use it on structured-clone-plain data.
   *
   * @param {any} a
   * @param {any} b
   * @returns {boolean}
   */
  deepEqual(a, b) {
    if (a === b) {
      return true;
    }
    if (typeof a != "object" || typeof b != "object" || !a || !b) {
      return false;
    }
    let aKeys = Object.keys(a);
    let bKeys = Object.keys(b);
    return (
      aKeys.length == bKeys.length &&
      aKeys.every(
        k => Object.hasOwn(b, k) && UrlbarShared.deepEqual(a[k], b[k])
      )
    );
  },

  /**
   * Given a string, checks if it looks like a single word host, not containing
   * spaces nor dots (apart from a possible trailing one).
   *
   * Note: This matching should stay in sync with the related code in
   * URIFixup::KeywordURIFixup
   *
   * @param {string} value
   *   The string to check.
   * @returns {boolean}
   *   Whether the value looks like a single word host.
   */
  looksLikeSingleWordHost(value) {
    let str = value.trim();
    return this.REGEXP_SINGLE_WORD.test(str);
  },

  /**
   * Returns whether a URL is an origin URL, i.e. it has no path beyond "/",
   * no query string, and no hash.
   *
   * @param {string} url
   *   The URL to check.
   * @returns {boolean}
   *   True if the URL is an origin URL, false if it has a path, query, hash,
   *   or is unparseable.
   */
  isOriginUrl(url) {
    let parsed = URL.parse(url);
    return (
      !!parsed && parsed.pathname === "/" && !parsed.search && !parsed.hash
    );
  },

  /**
   * Gets a default icon for a URL.
   *
   * @param {string|URL} url
   *   The URL to get the icon for.
   * @returns {string} A URI pointing to an icon for `url`.
   */
  getIconForUrl(url) {
    if (typeof url == "string") {
      return this.PROTOCOLS_WITH_ICONS.some(p => url.startsWith(p))
        ? "page-icon:" + url
        : this.ICON.DEFAULT;
    }
    if (
      this.isInstance(url, URL) &&
      this.PROTOCOLS_WITH_ICONS.includes(url.protocol)
    ) {
      return "page-icon:" + url.href;
    }
    return this.ICON.DEFAULT;
  },

  /**
   * Returns the name of a result source.  The name is the lowercase name of the
   * corresponding property in the RESULT_SOURCE object.
   *
   * @param {Values<typeof this.RESULT_SOURCE>} source
   *   A UrlbarShared.RESULT_SOURCE value.
   * @returns {string}
   *   The token's name, a lowercased name in the RESULT_SOURCE object.
   */
  getResultSourceName(source) {
    if (!this._resultSourceNamesBySource) {
      this._resultSourceNamesBySource = new Map();
      for (let [sourceName, src] of Object.entries(
        UrlbarShared.RESULT_SOURCE
      )) {
        this._resultSourceNamesBySource.set(src, sourceName.toLowerCase());
      }
    }
    return this._resultSourceNamesBySource.get(source);
  },

  /**
   * Strips parts of a URL defined in `options`.
   *
   * @param {string} spec
   *        The text to modify.
   * @param {object} [options]
   *        The options object.
   * @param {boolean} [options.stripHttp]
   *        Whether to strip http.
   * @param {boolean} [options.stripHttps]
   *        Whether to strip https.
   * @param {boolean} [options.stripWww]
   *        Whether to strip `www.`.
   * @param {boolean} [options.trimSlash]
   *        Whether to trim the trailing slash.
   * @param {boolean} [options.trimEmptyQuery]
   *        Whether to trim a trailing `?`.
   * @param {boolean} [options.trimEmptyHash]
   *        Whether to trim a trailing `#`.
   * @param {boolean} [options.trimTrailingDot]
   *        Whether to trim a trailing '.'.
   * @returns {string[]} [modified, prefix, suffix]
   *          modified: {string} The modified spec.
   *          prefix: {string} The parts stripped from the prefix, if any.
   *          suffix: {string} The parts trimmed from the suffix, if any.
   */
  stripPrefixAndTrim(spec, options = {}) {
    let prefix = "";
    let suffix = "";
    if (options.stripHttp && spec.startsWith("http://")) {
      spec = spec.slice(7);
      prefix = "http://";
    } else if (options.stripHttps && spec.startsWith("https://")) {
      spec = spec.slice(8);
      prefix = "https://";
    }
    if (options.stripWww && spec.startsWith("www.")) {
      spec = spec.slice(4);
      prefix += "www.";
    }
    if (options.trimEmptyHash && spec.endsWith("#")) {
      spec = spec.slice(0, -1);
      suffix = "#" + suffix;
    }
    if (options.trimEmptyQuery && spec.endsWith("?")) {
      spec = spec.slice(0, -1);
      suffix = "?" + suffix;
    }
    if (options.trimSlash && spec.endsWith("/")) {
      spec = spec.slice(0, -1);
      suffix = "/" + suffix;
    }
    if (options.trimTrailingDot && spec.endsWith(".")) {
      spec = spec.slice(0, -1);
      suffix = "." + suffix;
    }
    return [spec, prefix, suffix];
  },

  /**
   * Unescape the given uri to use as UI.
   * NOTE: If the length of uri is over MAX_TEXT_LENGTH,
   *       return the given uri as it is.
   *
   * @param {string} uri will be unescaped.
   * @returns {string} Unescaped uri.
   */
  unEscapeURIForUI(uri) {
    return uri.length > this.MAX_TEXT_LENGTH
      ? uri
      : UrlbarContentUtils.unEscapeURIForUI(uri);
  },

  /**
   * Unescape, decode punycode, and trim (both protocol and trailing slash)
   * the URL. Use for displaying purposes only!
   *
   * @param {string|URL} url The url that should be prepared for display.
   * @param {object} [options] Preparation options.
   * @param {boolean} [options.trimURL] Whether the displayed URL should be
   *                  trimmed or not.
   * @param {boolean} [options.schemeless] Trim `http(s)://`.
   * @returns {string} Prepared url.
   */
  prepareUrlForDisplay(url, { trimURL = true, schemeless = false } = {}) {
    // Some domains are encoded in punycode. The following ensures we display
    // the url in utf-8. If the url can't be parsed we fall back to using the
    // string as-is.
    let spec = typeof url == "string" ? url : url.href;
    let displayString = UrlbarContentUtils.getDisplaySpec(spec) ?? spec;

    if (displayString) {
      if (schemeless) {
        displayString = this.stripPrefixAndTrim(displayString, {
          stripHttp: true,
          stripHttps: true,
        })[0];
      } else if (trimURL && UrlbarPrefs.get("trimURLs")) {
        // Remove a single trailing slash for http/https/ftp URLs.
        displayString = displayString.replace(
          /^((?:http|https|ftp):\/\/[^/]+)\/$/,
          "$1"
        );
        if (displayString.startsWith("https://")) {
          displayString = displayString.substring(8);
          if (displayString.startsWith("www.")) {
            displayString = displayString.substring(4);
          }
        }
      }
    }

    return this.unEscapeURIForUI(displayString);
  },

  /**
   * Returns whether a URL can be autofilled from a candidate string. This
   * function is specifically designed for origin and up-to-the-next-slash URL
   * autofill. It should not be used for other types of autofill.
   *
   * @param {string} urlString
   *                 The URL to test
   * @param {string} candidateString
   *                 The candidate string to test against
   * @param {boolean} [checkFragmentOnly]
   *                 If want to check the fragment only, pass true.
   *                 Otherwise, check whole url.
   * @returns {boolean} true: can autofill
   */
  canAutofillURL(urlString, candidateString, checkFragmentOnly = false) {
    // If the URL does not start with the candidate, it can't be autofilled.
    // The length check is an optimization to short-circuit the `startsWith()`.
    if (
      !checkFragmentOnly &&
      (urlString.length <= candidateString.length ||
        !urlString
          .toLocaleLowerCase()
          .startsWith(candidateString.toLocaleLowerCase()))
    ) {
      return false;
    }

    // Create `URL` objects to make the logic below easier. The strings must
    // include schemes for this to work.
    if (!this.REGEXP_PREFIX.test(urlString)) {
      urlString = "http://" + urlString;
    }
    if (!this.REGEXP_PREFIX.test(candidateString)) {
      candidateString = "http://" + candidateString;
    }

    let url = URL.parse(urlString);
    let candidate = URL.parse(candidateString);
    if (!url || !candidate) {
      return false;
    }

    if (checkFragmentOnly) {
      return url.hash.startsWith(candidate.hash);
    }

    // For both origin and URL autofill, autofill should stop when the user
    // types a trailing slash. This is a fundamental part of autofill's
    // up-to-the-next-slash behavior. We handle that here in the else-if branch.
    // The length and hash checks in the else-if condition aren't strictly
    // necessary -- the else-if branch could simply be an else-branch that
    // returns false -- but they mean this function will return true when the
    // URL and candidate have the same case-insenstive path and no hash. In
    // other words, we allow a URL to autofill itself.
    if (!candidate.href.endsWith("/")) {
      // The candidate doesn't end in a slash. The URL can't be autofilled if
      // its next slash is not at the end.
      let nextSlashIndex = url.pathname.indexOf("/", candidate.pathname.length);
      if (nextSlashIndex >= 0 && nextSlashIndex != url.pathname.length - 1) {
        return false;
      }
    } else if (url.pathname.length > candidate.pathname.length || url.hash) {
      return false;
    }

    return url.hash.startsWith(candidate.hash);
  },

  /**
   * Whether the passed-in input event is paste event.
   *
   * @param {InputEvent} event an input DOM event.
   * @returns {boolean} Whether the event is a paste event.
   */
  isPasteEvent(event) {
    return (
      event.inputType &&
      (event.inputType.startsWith("insertFromPaste") ||
        event.inputType == "insertFromYank")
    );
  },

  /**
   * Sanitize and process data retrieved from the clipboard
   *
   * @param {string} clipboardData
   *   The original data retrieved from the clipboard.
   * @param {?URIFixupPrimitives} fixupInfo
   *   Fixup info for `clipboardData`, or null if it couldn't be fixed up. URI
   *   fixup is parent-only, so the caller supplies it, either from
   *   `UrlbarUtils.getFixupPrimitives()` or, from an input, from
   *   `UrlbarContentUtils.getFixupPrimitives()`.
   * @returns {string}
   *   The sanitized paste data, ready to use.
   */
  sanitizeTextFromClipboard(clipboardData, fixupInfo) {
    let url = URL.parse(clipboardData);
    let pasteData;
    if (fixupInfo?.keywordAsSent) {
      // For performance reasons, we don't want to beautify a long string.
      if (clipboardData.length < 500) {
        // For only keywords, replace any white spaces including line break
        // with white space.
        pasteData = clipboardData.replace(/\s/g, " ");
      } else {
        pasteData = clipboardData;
      }
    } else if (
      url?.protocol == "data:" &&
      !url.href.match(/^data:.+;base64,/)
    ) {
      // For data url without base64, replace line break with white space.
      pasteData = clipboardData.replace(/[\r\n]/g, " ");
    } else {
      // For normal url or data url having basic64, or if fixup failed, just
      // remove line breaks.
      pasteData = clipboardData.replace(/[\r\n]/g, "");
    }

    return this.stripUnsafeProtocolOnPaste(pasteData);
  },

  /**
   * Used to filter out the javascript protocol from URIs, since we don't
   * support LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL for those.
   *
   * @param {string} pasteData The data to check for javacript protocol.
   * @returns {string} The modified paste data.
   */
  stripUnsafeProtocolOnPaste(pasteData) {
    while (URL.parse(pasteData)?.protocol == "javascript:") {
      pasteData = pasteData.substring(pasteData.indexOf(":") + 1);
    }
    return pasteData;
  },

  /**
   * Gets the number of rows a result should span in the view.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {object} [options]
   * @param {boolean} [options.includeHiddenExposures]
   *   Whether a span should be returned if the result is a hidden exposure. If
   *   false and `result.isHiddenExposure` is true, zero will be returned since
   *   the result should be hidden and not take up any rows at all. Otherwise
   *   the result's true span is returned.
   * @returns {number}
   *   The number of rows the result should span in the view.
   */
  getSpanForResult(result, { includeHiddenExposures = false } = {}) {
    if (!includeHiddenExposures && result.isHiddenExposure) {
      return 0;
    }

    if (result.resultSpan) {
      return result.resultSpan;
    }

    switch (result.type) {
      case UrlbarShared.RESULT_TYPE.TIP:
        return 3;
    }
    return 1;
  },

  /**
   * Returns the group for a result.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @returns {Values<typeof UrlbarShared.RESULT_GROUP>}
   *   The result's group.
   */
  getResultGroup(result) {
    // Used for test_suggestedIndexRelativeToGroup.js to make it simpler
    if (result.group) {
      return result.group;
    }

    if (result.hasSuggestedIndex && !result.isSuggestedIndexRelativeToGroup) {
      return UrlbarShared.RESULT_GROUP.SUGGESTED_INDEX;
    }
    if (result.heuristic) {
      switch (result.providerName) {
        case "UrlbarProviderAiChat":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_AI_CHAT;
        case "UrlbarProviderAliasEngines":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS;
        case "UrlbarProviderAutofill":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL;
        case "UrlbarProviderBookmarkKeywords":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD;
        case "UrlbarProviderHeuristicFallback":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK;
        case "UrlbarProviderHistoryUrlHeuristic":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL;
        case "UrlbarProviderOmnibox":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX;
        case "UrlbarProviderRestrictKeywordsAutofill":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_RESTRICT_KEYWORD_AUTOFILL;
        case "UrlbarProviderTokenAliasEngines":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE;
        case "UrlbarProviderSearchTips":
          return UrlbarShared.RESULT_GROUP.HEURISTIC_SEARCH_TIP;
        default:
          if (result.providerName.startsWith("TestProvider")) {
            return UrlbarShared.RESULT_GROUP.HEURISTIC_TEST;
          }
          break;
      }
      if (result.providerType == UrlbarShared.PROVIDER_TYPE.EXTENSION) {
        return UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION;
      }
      console.error(
        "Returning HEURISTIC_FALLBACK for unrecognized heuristic result: ",
        result
      );
      return UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK;
    }

    switch (result.providerName) {
      case "UrlbarProviderAboutPages":
        return UrlbarShared.RESULT_GROUP.ABOUT_PAGES;
      case "UrlbarProviderInputHistory":
        return UrlbarShared.RESULT_GROUP.INPUT_HISTORY;
      case "UrlbarProviderQuickSuggest":
        return UrlbarShared.RESULT_GROUP.GENERAL_PARENT;
      default:
        break;
    }

    switch (result.type) {
      case UrlbarShared.RESULT_TYPE.SEARCH:
        if (result.source == UrlbarShared.RESULT_SOURCE.HISTORY) {
          return result.providerName == "UrlbarProviderRecentSearches"
            ? UrlbarShared.RESULT_GROUP.RECENT_SEARCH
            : UrlbarShared.RESULT_GROUP.FORM_HISTORY;
        }
        if (result.payload.tail && !result.isRichSuggestion) {
          return UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION;
        }
        if (result.payload.suggestion) {
          return UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION;
        }
        break;
      case UrlbarShared.RESULT_TYPE.OMNIBOX:
        return UrlbarShared.RESULT_GROUP.OMNIBOX;
      case UrlbarShared.RESULT_TYPE.REMOTE_TAB:
        return UrlbarShared.RESULT_GROUP.REMOTE_TAB;
      case UrlbarShared.RESULT_TYPE.RESTRICT:
        return UrlbarShared.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD;
      case UrlbarShared.RESULT_TYPE.AI_CHAT:
        return UrlbarShared.RESULT_GROUP.AI;
    }
    // When enabled, semantic history results (both history URLs and
    // switch-to-tab results) get their own group so they fill only the space
    // left after, and never evict, the plain (non-semantic) results that would
    // otherwise share the general group.
    if (
      result.providerName == "UrlbarProviderSemanticHistorySearch" &&
      UrlbarPrefs.get("suggest.semanticHistory.separateGroup")
    ) {
      return UrlbarShared.RESULT_GROUP.SEMANTIC_HISTORY;
    }
    return UrlbarShared.RESULT_GROUP.GENERAL;
  },

  /**
   * Extracts a group for search engagement telemetry from a result.
   *
   * @param {UrlbarResult} result The result to analyze.
   * @returns {string} Group name as string.
   */
  searchEngagementTelemetryGroup(result) {
    if (!result) {
      return "unknown";
    }
    if (result.isBestMatch) {
      return "top_pick";
    }
    if (result.providerName === "UrlbarProviderTopSites") {
      return "top_site";
    }

    switch (this.getResultGroup(result)) {
      case UrlbarShared.RESULT_GROUP.INPUT_HISTORY: {
        return "adaptive_history";
      }
      case UrlbarShared.RESULT_GROUP.RECENT_SEARCH: {
        return "recent_search";
      }
      case UrlbarShared.RESULT_GROUP.FORM_HISTORY: {
        return "search_history";
      }
      case UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION:
      case UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION: {
        let group = result.payload.trending
          ? "trending_search"
          : "search_suggest";
        if (result.isRichSuggestion) {
          group += "_rich";
        }
        return group;
      }
      case UrlbarShared.RESULT_GROUP.REMOTE_TAB: {
        return "remote_tab";
      }
      case UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION:
      case UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX:
      case UrlbarShared.RESULT_GROUP.OMNIBOX: {
        return "addon";
      }
      // Semantic history results have their own group for sorting purposes but
      // are reported as "general" results, as they were before the group split.
      case UrlbarShared.RESULT_GROUP.GENERAL:
      case UrlbarShared.RESULT_GROUP.SEMANTIC_HISTORY: {
        return "general";
      }
      // Group of UrlbarProviderQuickSuggest is GENERAL_PARENT.
      case UrlbarShared.RESULT_GROUP.GENERAL_PARENT: {
        return "suggest";
      }
      case UrlbarShared.RESULT_GROUP.ABOUT_PAGES: {
        return "about_page";
      }
      case UrlbarShared.RESULT_GROUP.SUGGESTED_INDEX: {
        return "suggested_index";
      }
      case UrlbarShared.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD: {
        return "restrict_keyword";
      }
      case UrlbarShared.RESULT_GROUP.AI: {
        return "ai";
      }
    }

    return result.heuristic ? "heuristic" : "unknown";
  },

  /**
   * Extracts an action for search engagement telemetry from a result.
   *
   * @param {UrlbarResult} result The result to analyze.
   * @param {string} [pickedActionKey] The key of the action the user picked.
   * @returns {string} Action key, or a comma-separated list of the keys offered.
   */
  searchEngagementTelemetryAction(result, pickedActionKey = null) {
    if (result.providerName != "UrlbarProviderGlobalActions") {
      return result.payload.action?.key ?? "none";
    }
    if (pickedActionKey) {
      return pickedActionKey;
    }
    return result.payload.actionsResults.map(({ key }) => key).join(",");
  },

  /**
   * Extracts a type for search engagement telemetry from a result.
   *
   * @param {UrlbarResult} result The result to analyze.
   * @param {string} [selType] An optional parameter for the selected type.
   * @returns {string} Type as string.
   */
  searchEngagementTelemetryType(result, selType = null) {
    if (!result) {
      return selType === "oneoff" ? "search_shortcut_button" : "input_field";
    }

    // While product doesn't use experimental addons anymore, tests may still do
    // for testing purposes.
    if (
      result.providerType === UrlbarShared.PROVIDER_TYPE.EXTENSION &&
      result.providerName != "UrlbarProviderOmnibox"
    ) {
      return "experimental_addon";
    }

    if (result.providerName == "UrlbarProviderQuickSuggest") {
      return this._getQuickSuggestTelemetryType(result);
    }

    // Appends subtype to certain result types.
    function checkForSubType(type, res) {
      if (res.providerName == "UrlbarProviderInputHistory") {
        type += "_adaptive";
      } else if (res.providerName == "UrlbarProviderSemanticHistorySearch") {
        type += "_semantic";
      }
      if (
        res.isSERP &&
        [
          UrlbarShared.RESULT_SOURCE.BOOKMARKS,
          UrlbarShared.RESULT_SOURCE.HISTORY,
          UrlbarShared.RESULT_SOURCE.TABS,
        ].includes(res.source)
      ) {
        type += "_serp";
      }
      return type;
    }

    switch (result.type) {
      case UrlbarShared.RESULT_TYPE.DYNAMIC:
        switch (result.providerName) {
          case "UrlbarProviderCalculator":
            return "calc";
          case "UrlbarProviderTabToSearch":
            return "tab_to_search";
          case "UrlbarProviderUnitConversion":
            return "unit";
          case "UrlbarProviderQuickSuggestContextualOptIn":
            return "fxsuggest_data_sharing_opt_in";
          case "UrlbarProviderGlobalActions":
          case "UrlbarProviderActionsSearchMode":
            return "action";
        }
        break;
      case UrlbarShared.RESULT_TYPE.KEYWORD:
        return "keyword";
      case UrlbarShared.RESULT_TYPE.OMNIBOX:
        return "addon";
      case UrlbarShared.RESULT_TYPE.REMOTE_TAB:
        return "remote_tab";
      case UrlbarShared.RESULT_TYPE.SEARCH:
        if (result.providerName === "UrlbarProviderTabToSearch") {
          return "tab_to_search";
        }
        if (result.source == UrlbarShared.RESULT_SOURCE.HISTORY) {
          return result.providerName == "UrlbarProviderRecentSearches"
            ? "recent_search"
            : "search_history";
        }
        if (result.providerName === "UrlbarProviderAiChat") {
          return "ai_search_fallback";
        }
        if (result.payload.suggestion) {
          let type = result.payload.trending
            ? "trending_search"
            : "search_suggest";
          if (result.isRichSuggestion) {
            type += "_rich";
          }
          return type;
        }
        return "search_engine";
      case UrlbarShared.RESULT_TYPE.TAB_SWITCH:
        return checkForSubType("tab", result);
      case UrlbarShared.RESULT_TYPE.TIP:
        if (result.providerName === "UrlbarProviderInterventions") {
          switch (result.payload.type) {
            case UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR:
              return "intervention_clear";
            case UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH:
              return "intervention_refresh";
            case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_ASK:
            case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING:
            case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_REFRESH:
            case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_RESTART:
            case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_WEB:
              return "intervention_update";
            default:
              return "intervention_unknown";
          }
        }
        switch (result.payload.type) {
          case UrlbarShared.SEARCH_TIP_TYPE.ONBOARD:
            return "tip_onboard";
          case UrlbarShared.SEARCH_TIP_TYPE.REDIRECT:
            return "tip_redirect";
          case "dismissalAcknowledgment":
            return "tip_dismissal_acknowledgment";
          default:
            return "tip_unknown";
        }
      case UrlbarShared.RESULT_TYPE.URL:
        if (
          result.source === UrlbarShared.RESULT_SOURCE.OTHER_LOCAL &&
          result.heuristic
        ) {
          return "url";
        }
        if (result.autofill) {
          return `autofill_${result.autofill.type ?? "unknown"}`;
        }
        if (result.providerName === "UrlbarProviderTopSites") {
          return "top_site";
        }
        if (result.providerName === "UrlbarProviderClipboard") {
          return "clipboard";
        }
        if (result.payload.isAutofillFallback) {
          return "history_autofill_fallback_origin";
        }
        if (result.source === UrlbarShared.RESULT_SOURCE.BOOKMARKS) {
          return checkForSubType("bookmark", result);
        }
        return checkForSubType("history", result);
      case UrlbarShared.RESULT_TYPE.RESTRICT:
        if (result.payload.keyword === UrlbarShared.RESTRICT_TOKENS.BOOKMARK) {
          return "restrict_keyword_bookmarks";
        }
        if (result.payload.keyword === UrlbarShared.RESTRICT_TOKENS.OPENPAGE) {
          return "restrict_keyword_tabs";
        }
        if (result.payload.keyword === UrlbarShared.RESTRICT_TOKENS.HISTORY) {
          return "restrict_keyword_history";
        }
        if (result.payload.keyword === UrlbarShared.RESTRICT_TOKENS.ACTION) {
          return "restrict_keyword_actions";
        }
        break;
      case UrlbarShared.RESULT_TYPE.AI_CHAT:
        return "ai_chat";
    }

    return "unknown";
  },

  _getQuickSuggestTelemetryType(result) {
    if (result.payload.telemetryType == "weather") {
      // Return "weather" without the usual source prefix for consistency with
      // past reporting of weather suggestions.
      return "weather";
    }
    return result.payload.source + "_" + result.payload.telemetryType;
  },

  _compareIgnoringDiacritics: null,

  /**
   * Returns a list of all the token substring matches in a string.  Matching is
   * case insensitive.  Each match in the returned list is a tuple: [matchIndex,
   * matchLength].  matchIndex is the index in the string of the match, and
   * matchLength is the length of the match.
   *
   * @param {Array} tokens The tokens to search for.
   * @param {string} str The string to match against.
   * @param {Values<typeof this.HIGHLIGHT>} highlightType
   *   One of the HIGHLIGHT values:
   *     TYPED: match ranges matching the tokens; or
   *     SUGGESTED: match ranges for words not matching the tokens and the
   *                endings of words that start with a token.
   *     ALL: match all ranges of str.
   * @returns {Array} An array: [
   *            [matchIndex_0, matchLength_0],
   *            [matchIndex_1, matchLength_1],
   *            ...
   *            [matchIndex_n, matchLength_n]
   *          ].
   *          The array is sorted by match indexes ascending.
   */
  getTokenMatches(tokens, str, highlightType) {
    if (highlightType == UrlbarShared.HIGHLIGHT.ALL) {
      return [[0, str.length]];
    }

    if (!tokens?.length) {
      return [];
    }

    // Only search a portion of the string, because not more than a certain
    // amount of characters are visible in the UI, matching over what is visible
    // would be expensive and pointless.
    str = str.substring(0, UrlbarShared.MAX_TEXT_LENGTH).toLocaleLowerCase();
    // To generate non-overlapping ranges, we start from a 0-filled array with
    // the same length of the string, and use it as a collision marker, setting
    // 1 where the text should be highlighted.
    let hits = new Array(str.length).fill(
      highlightType == UrlbarShared.HIGHLIGHT.SUGGESTED ? 1 : 0
    );
    let compareIgnoringDiacritics;
    for (let i = 0, totalTokensLength = 0; i < tokens.length; i++) {
      const { lowerCaseValue: needle } = tokens[i];

      // Ideally we should never hit the empty token case, but just in case
      // the `needle` check protects us from an infinite loop.
      if (!needle) {
        continue;
      }
      let index = 0;
      let found = false;
      // First try a diacritic-sensitive search.
      for (;;) {
        index = str.indexOf(needle, index);
        if (index < 0) {
          break;
        }

        if (highlightType == UrlbarShared.HIGHLIGHT.SUGGESTED) {
          // We de-emphasize the match only if it's preceded by a space, thus
          // it's a perfect match or the beginning of a longer word.
          let previousSpaceIndex = str.lastIndexOf(" ", index) + 1;
          if (index != previousSpaceIndex) {
            index += needle.length;
            // We found the token but we won't de-emphasize it, because it's not
            // after a word boundary.
            found = true;
            continue;
          }
        }

        hits.fill(
          highlightType == UrlbarShared.HIGHLIGHT.SUGGESTED ? 0 : 1,
          index,
          index + needle.length
        );
        index += needle.length;
        found = true;
      }
      // If that fails to match anything, try a (computationally intensive)
      // diacritic-insensitive search.
      if (!found) {
        if (!compareIgnoringDiacritics) {
          if (!this._compareIgnoringDiacritics) {
            // Diacritic insensitivity in the search engine follows a set of
            // general rules that are not locale-dependent, so use a generic
            // English collator for highlighting matching words instead of a
            // collator for the user's particular locale.
            this._compareIgnoringDiacritics = new Intl.Collator("en", {
              sensitivity: "base",
            }).compare;
          }
          compareIgnoringDiacritics = this._compareIgnoringDiacritics;
        }
        index = 0;
        while (index < str.length) {
          let hay = str.substr(index, needle.length);
          if (compareIgnoringDiacritics(needle, hay) === 0) {
            if (highlightType == UrlbarShared.HIGHLIGHT.SUGGESTED) {
              let previousSpaceIndex = str.lastIndexOf(" ", index) + 1;
              if (index != previousSpaceIndex) {
                index += needle.length;
                continue;
              }
            }
            hits.fill(
              highlightType == UrlbarShared.HIGHLIGHT.SUGGESTED ? 0 : 1,
              index,
              index + needle.length
            );
            index += needle.length;
          } else {
            index++;
          }
        }
      }

      totalTokensLength += needle.length;
      if (totalTokensLength > UrlbarShared.MAX_TEXT_LENGTH) {
        // Limit the number of tokens to reduce calculate time.
        break;
      }
    }
    // Starting from the collision array, generate [start, len] tuples
    // representing the ranges to be highlighted.
    let ranges = [];
    for (let index = hits.indexOf(1); index >= 0 && index < hits.length; ) {
      let len = 0;
      // eslint-disable-next-line no-empty
      for (let j = index; j < hits.length && hits[j]; ++j, ++len) {}
      ranges.push([index, len]);
      // Move to the next 1.
      index = hits.indexOf(1, index + len);
    }
    return ranges;
  },

  /**
   * Adds text content to a node, placing substrings that should be highlighted
   * inside <strong> nodes.
   *
   * @param {Element} parentNode
   *   The text content will be added to this node.
   * @param {string} textContent
   *   The text content to give the node.
   * @param {Array} highlights
   *   Array of highlights as returned by `UrlbarShared.getTokenMatches()` or
   *   `UrlbarResult.getDisplayableValueAndHighlights()`.
   */
  addTextContentWithHighlights(parentNode, textContent, highlights) {
    parentNode.textContent = "";
    if (!textContent) {
      return;
    }

    highlights = (highlights || []).concat([[textContent.length, 0]]);
    let index = 0;
    for (let [highlightIndex, highlightLength] of highlights) {
      if (highlightIndex - index > 0) {
        parentNode.appendChild(
          parentNode.ownerDocument.createTextNode(
            textContent.substring(index, highlightIndex)
          )
        );
      }
      if (highlightLength > 0) {
        let strong = parentNode.ownerDocument.createElement("strong");
        strong.textContent = textContent.substring(
          highlightIndex,
          highlightIndex + highlightLength
        );
        parentNode.appendChild(strong);
      }
      index = highlightIndex + highlightLength;
    }
  },

  /**
   * Formats a date and time for display. This is not a general formatting
   * function. It uses some heuristics to generate formatted strings as used in
   * urlbar UI. The format chosen will depend on the date.
   *
   * @param {Date} date
   *   A JS `Date` object.
   * @param {object} options
   *   Options object
   * @param {boolean} [options.forceAbsoluteDate]
   *   Pass true to force the formatted date to be absolute ("May 11") when it
   *   otherwise would be formatted as relative ("tomorrow").
   * @param {boolean} [options.forceMonthAndDayWhenAbsolute]
   *   When the date format is absolute, normally the formatted date will simply
   *   be the weekday name when the date is in the near future, e.g., "Mon"
   *   instead of "May 11". Pass true to format it as a month and day instead.
   * @param {boolean} [options.capitalizeRelativeDate]
   *   Whether relative dates should be capitalized ("Tomorrow" instead of
   *   "tomorrow").
   * @param {boolean} [options.includeTimeZone]
   *   When a formatted time is generated, it will include the time zone only if
   *   this param is true.
   * @returns {FormatDateResult}
   *   The result.
   *
   * @typedef {object} FormatDateResult
   * @property {string} formattedDate
   *   The formatted date.
   * @property {?string} formattedTime
   *   The formatted time. Depending on the passed-in date, a formatted time
   *   might not be generated, and in that case this will be undefined.
   * @property {string} dateFormatType
   *   The type of the formatted date. See `DATE_FORMAT_TYPE` values.
   * @property {boolean} isRelative
   *   Whether the formatted date is relative rather than absolute. Relative
   *   date examples: "tomorrow", "5 days ago", "1 week ago", "2 months ago".
   *   Absolute date examples: "Mon" (this coming Monday), "May 11" (this year),
   *   "May 11, 2013"
   * @property {ParseDateResult} parseDateResult
   *   This function calls `parseDate()` as part of its operation, and the
   *   result is included here in case it's useful.
   */
  formatDate(
    date,
    {
      forceAbsoluteDate = false,
      forceMonthAndDayWhenAbsolute = false,
      capitalizeRelativeDate = false,
      includeTimeZone = false,
    } = {}
  ) {
    let parseDateResult = this.parseDate(date);
    let { zonedNow, zonedDate, daysAgo, weeksAgo, monthsAgo, isFuture } =
      parseDateResult;

    // First, format the date.
    let formattedDate;
    let dateFormatType;
    let isRelative = true;
    if (!forceAbsoluteDate) {
      if (Math.abs(daysAgo) <= 1) {
        // "yesterday", "today", or "tomorrow"
        dateFormatType = this.DATE_FORMAT_TYPE.YESTERDAY_TODAY_TOMORROW;
        formattedDate = new Intl.RelativeTimeFormat(undefined, {
          numeric: "auto",
        }).format(-daysAgo, "day");
      } else if (0 < daysAgo && daysAgo <= 6) {
        // <= 6 days ago: "{n} days ago"
        dateFormatType = this.DATE_FORMAT_TYPE.DAYS_WEEKS_MONTHS_AGO;
        formattedDate = new Intl.RelativeTimeFormat(undefined, {
          numeric: "always",
        }).format(-daysAgo, "day");
      } else if (0 < weeksAgo && (weeksAgo <= 4 || monthsAgo == 0)) {
        // <= 4 weeks ago or same month: "{n} weeks ago"
        dateFormatType = this.DATE_FORMAT_TYPE.DAYS_WEEKS_MONTHS_AGO;
        formattedDate = new Intl.RelativeTimeFormat(undefined, {
          numeric: "always",
        }).format(-weeksAgo, "week");
      } else if (0 < monthsAgo && monthsAgo <= 11) {
        // <= 11 months ago: "{n} months ago"
        dateFormatType = this.DATE_FORMAT_TYPE.DAYS_WEEKS_MONTHS_AGO;
        formattedDate = new Intl.RelativeTimeFormat(undefined, {
          numeric: "always",
        }).format(-monthsAgo, "month");
      }

      if (capitalizeRelativeDate && formattedDate) {
        formattedDate =
          formattedDate[0].toLocaleUpperCase() + formattedDate.substring(1);
      }
    }

    if (!formattedDate) {
      // Format the date as an absolute date with some combination of year,
      // month, day, and weekday, e.g.: "May 11", "May 11, 2026", "Mon"
      let opts = {
        timeZone: zonedNow.timeZoneId,
      };
      if (!forceMonthAndDayWhenAbsolute && 0 < -daysAgo && -daysAgo < 7) {
        // Include only the weekday.
        opts.weekday = "short";
      } else {
        // Include the month and day and the year if it's not this year.
        opts.month = "short";
        opts.day = "numeric";
        if (zonedDate.year != zonedNow.year) {
          opts.year = "numeric";
        }
      }
      formattedDate = new Intl.DateTimeFormat(undefined, opts).format(date);
      dateFormatType = this.DATE_FORMAT_TYPE.ABSOLUTE;
      isRelative = false;
    }

    // Now format the time.
    let formattedTime;
    if (isFuture) {
      formattedTime = new Intl.DateTimeFormat(undefined, {
        hour: "numeric",
        minute: "numeric",
        timeZoneName: includeTimeZone ? "short" : undefined,
        timeZone: zonedNow.timeZoneId,
      }).format(date);
    }

    return {
      isRelative,
      dateFormatType,
      formattedDate,
      formattedTime,
      parseDateResult,
    };
  },

  // Date format types returned by `formatDate()`.
  DATE_FORMAT_TYPE: Object.freeze({
    // "yesterday", "today", or "tomorrow"
    YESTERDAY_TODAY_TOMORROW: "yesterday_today_tomorrow",
    // "{n} days ago", "{n} weeks ago", or "{n} months ago"
    DAYS_WEEKS_MONTHS_AGO: "days_weeks_months_ago",
    // "Mon" (this coming Monday), "May 11" (this year), or "May 11, 2013"
    ABSOLUTE: "absolute",
  }),

  /**
   * Parses a `Date` and returns some info about it relative to today.
   *
   * For the "ago" values, a value will be positive if the date is in the past
   * and negative if the date is in the future.
   *
   * @param {Date} date
   *   A JS `Date` object.
   * @returns {ParseDateResult}
   *   The result.
   *
   * @typedef {object} ParseDateResult
   * @property {Temporal.ZonedDateTime} zonedNow
   *   The "now" date as a `ZonedDateTime`.
   * @property {Temporal.ZonedDateTime} zonedDate
   *   The passed-in date as a `ZonedDateTime`.
   * @property {boolean} isFuture
   *   Whether the date is in the future.
   * @property {number} daysAgo
   *   The number of calendar days from the date to today. If the date is also
   *   today, this value will be zero.
   * @property {number} weeksAgo
   *   The number of calendar weeks from the date to today. If the date is in
   *   the same calendar week as today, this value will be zero. If the date is
   *   in the previous calendar week, this value will be 1, and so on. The start
   *   of the calendar week is determined by `UrlbarShared._firstDayOfWeek()`.
   * @property {number} monthsAgo
   *   The number of calendar months from the date to today. If the date is in
   *   the same calendar month as today, this value will be zero. If the date is
   *   in the previous calendar month, this value will be 1, and so on.
   */
  parseDate(date) {
    let zonedNow = this._zonedDateTimeISO();
    let zonedDate = date.toTemporalInstant().toZonedDateTimeISO(zonedNow);
    let isFuture = Temporal.ZonedDateTime.compare(zonedNow, zonedDate) < 0;

    let today = zonedNow.startOfDay();
    let dateDay = zonedDate.startOfDay();

    let daysAgo = today.since(dateDay).round("days").days;
    let firstDayOfWeek = this._firstDayOfWeek();
    let thisWeek = today.subtract({
      days: (today.dayOfWeek - firstDayOfWeek + 7) % 7,
    });
    let dateWeek = dateDay.subtract({
      days: (dateDay.dayOfWeek - firstDayOfWeek + 7) % 7,
    });
    let weeksAgo = thisWeek.since(dateWeek).round({
      smallestUnit: "weeks",
      relativeTo: thisWeek,
    }).weeks;

    let thisMonth = today.with({ day: 1 });
    let dateMonth = dateDay.with({ day: 1 });
    let monthsAgo = thisMonth.since(dateMonth).round({
      smallestUnit: "months",
      relativeTo: thisMonth,
    }).months;

    return {
      zonedNow,
      zonedDate,
      isFuture,
      daysAgo,
      weeksAgo,
      monthsAgo,
    };
  },

  // Thin wrapper around `zonedDateTimeISO` so that tests can easily set a mock
  // "now" date and time. See `UrlbarTestUtils.stubNowZonedDateTime()`.
  _zonedDateTimeISO() {
    return Temporal.Now.zonedDateTimeISO();
  },

  // Thin wrapper around `getWeekInfo` so that tests can easily set a mock first
  // day of the week. See `UrlbarTestUtils.stubFirstDayOfWeek()`.
  _firstDayOfWeek() {
    // Getting locale info can be expensive, so cache this value. We don't re-
    // cache it on locale change. That should be OK because we use it only to
    // generate "{n} weeks ago" UI strings, which aren't very precise anyway.
    if (this.__firstDayOfWeek === undefined) {
      this.__firstDayOfWeek = new Intl.Locale(
        Intl.DateTimeFormat().resolvedOptions().locale
      ).getWeekInfo().firstDay;

      // Make sure we always have a valid value in case the locale doesn't
      // define one for whatever reason. 7 = Sunday.
      this.__firstDayOfWeek ??= 7;
    }
    return this.__firstDayOfWeek;
  },
};

/**
 * Create a logger that uses `console.createInstance`.
 *
 * @param {string} prefix
 * @param {string} maxLogLevelPref
 * @returns {Console}
 */
function createLoggerChrome(prefix, maxLogLevelPref) {
  let logger = console.createInstance({ prefix, maxLogLevelPref });
  // Casting from ConsoleInstance to Console. Note that it is technically not a
  // `Console` because it is missing the chrome-only property `createInstance`.
  return /** @type {Console} */ (/** @type {unknown} */ (logger));
}

/**
 * Create a logger that uses the global `console`.
 *
 * @param {string} prefix
 * @param {string} maxLogLevelPref
 * @returns {Console}
 */
function createLoggerContent(prefix, maxLogLevelPref) {
  let tag = `[${prefix}]`;
  const LEVEL_NUMBERS = {
    all: 0,
    trace: 1,
    debug: 2,
    log: 3,
    info: 3,
    warn: 4,
    error: 5,
    off: Infinity,
  };
  const LEVELS = ["debug", "log", "info", "trace", "warn", "error"];

  // UrlbarPrefs names prefs in the `browser.urlbar.` branch relative to it.
  let levelPref = maxLogLevelPref.replace(/^browser\.urlbar\./, "");

  let shouldLog = level => {
    let maxLevel =
      LEVEL_NUMBERS[UrlbarPrefs.get(levelPref).toLowerCase()] ??
      LEVEL_NUMBERS.warn;
    return maxLevel <= LEVEL_NUMBERS[level];
  };

  return new Proxy(console, {
    get(target, prop) {
      if (typeof prop == "string" && LEVELS.includes(prop)) {
        return (...args) => shouldLog(prop) && target[prop](tag, ...args);
      }

      let value = target[prop];
      return typeof value == "function" ? value.bind(target) : value;
    },
  });
}

/** @type {Map<string, Console>} */
const loggers = new Map();
