/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports the UrlbarUtils singleton, which contains constants and
 * helper functions that are useful to all components of the urlbar.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  KeywordUtils: "resource://gre/modules/KeywordUtils.sys.mjs",
  PlacesUIUtils: "resource:///modules/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchSuggestionController:
    "resource://gre/modules/SearchSuggestionController.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarProviderInterventions:
    "resource:///modules/UrlbarProviderInterventions.sys.mjs",
  UrlbarProviderOpenTabs: "resource:///modules/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarProvidersManager: "resource:///modules/UrlbarProvidersManager.sys.mjs",
  UrlbarProviderSearchTips:
    "resource:///modules/UrlbarProviderSearchTips.sys.mjs",
  UrlbarSearchUtils: "resource:///modules/UrlbarSearchUtils.sys.mjs",
  UrlbarTokenizer: "resource:///modules/UrlbarTokenizer.sys.mjs",
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "parserUtils",
  "@mozilla.org/parserutils;1",
  "nsIParserUtils"
);

export var UrlbarUtils = {
  // Results are categorized into groups to help the muxer compose them.  See
  // UrlbarUtils.getResultGroup.  Since result groups are stored in result
  // groups and result groups are stored in prefs, additions and changes to
  // result groups may require adding UI migrations to BrowserGlue.  Be careful
  // about making trivial changes to existing groups, like renaming them,
  // because we don't want to make downgrades unnecessarily hard.
  RESULT_GROUP: {
    ABOUT_PAGES: "aboutPages",
    GENERAL: "general",
    GENERAL_PARENT: "generalParent",
    FORM_HISTORY: "formHistory",
    HEURISTIC_AUTOFILL: "heuristicAutofill",
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
    SUGGESTED_INDEX: "suggestedIndex",
    TAIL_SUGGESTION: "tailSuggestion",
  },

  // Defines provider types.
  PROVIDER_TYPE: {
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
  },

  // Defines UrlbarResult types.
  RESULT_TYPE: {
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

    // When you add a new type, also add its schema to
    // UrlbarUtils.RESULT_PAYLOAD_SCHEMA below.  Also consider checking if
    // consumers of "urlbar-user-start-navigation" need updating.
  },

  // This defines the source of results returned by a provider. Each provider
  // can return results from more than one source. This is used by the
  // ProvidersManager to decide which providers must be queried and which
  // results can be returned.
  // If you add new source types, consider checking if consumers of
  // "urlbar-user-start-navigation" need update as well.
  RESULT_SOURCE: {
    BOOKMARKS: 1,
    HISTORY: 2,
    SEARCH: 3,
    TABS: 4,
    OTHER_LOCAL: 5,
    OTHER_NETWORK: 6,
    ADDON: 7,
    ACTIONS: 8,
  },

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
    // DEFAULT is defined lazily so it doesn't eagerly initialize PlacesUtils.
    EXTENSION: "chrome://mozapps/skin/extensions/extension.svg",
    HISTORY: "chrome://browser/skin/history.svg",
    SEARCH_GLASS: "chrome://global/skin/icons/search-glass.svg",
    TRENDING: "chrome://global/skin/icons/trending.svg",
    TIP: "chrome://global/skin/icons/lightbulb.svg",
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
  HIGHLIGHT: {
    NONE: 0,
    TYPED: 1,
    SUGGESTED: 2,
  },

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
  // telemetry documentation and Scalars.yaml.
  SEARCH_MODE_ENTRY: new Set([
    "bookmarkmenu",
    "handoff",
    "keywordoffer",
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
    return [
      {
        source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
        restrict: lazy.UrlbarTokenizer.RESTRICT.BOOKMARK,
        icon: "chrome://browser/skin/bookmark.svg",
        pref: "shortcuts.bookmarks",
        telemetryLabel: "bookmarks",
        uiLabel: "urlbar-searchmode-bookmarks",
      },
      {
        source: UrlbarUtils.RESULT_SOURCE.TABS,
        restrict: lazy.UrlbarTokenizer.RESTRICT.OPENPAGE,
        icon: "chrome://browser/skin/tabs.svg",
        pref: "shortcuts.tabs",
        telemetryLabel: "tabs",
        uiLabel: "urlbar-searchmode-tabs",
      },
      {
        source: UrlbarUtils.RESULT_SOURCE.HISTORY,
        restrict: lazy.UrlbarTokenizer.RESTRICT.HISTORY,
        icon: "chrome://browser/skin/history.svg",
        pref: "shortcuts.history",
        telemetryLabel: "history",
        uiLabel: "urlbar-searchmode-history",
      },
      {
        source: UrlbarUtils.RESULT_SOURCE.ACTIONS,
        restrict: lazy.UrlbarTokenizer.RESTRICT.ACTION,
        icon: "chrome://browser/skin/quickactions.svg",
        pref: "shortcuts.actions",
        telemetryLabel: "actions",
        uiLabel: "urlbar-searchmode-actions",
      },
    ];
  },

  /**
   * Returns the payload schema for the given type of result.
   *
   * @param {number} type One of the UrlbarUtils.RESULT_TYPE values.
   * @returns {object} The schema for the given type.
   */
  getPayloadSchema(type) {
    return UrlbarUtils.RESULT_PAYLOAD_SCHEMA[type];
  },

  /**
   * Adds a url to history as long as it isn't in a private browsing window,
   * and it is valid.
   *
   * @param {string} url The url to add to history.
   * @param {nsIDomWindow} window The window from where the url is being added.
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

    let engine = await Services.search.getEngineByAlias(keyword);
    if (engine) {
      let submission = engine.getSubmission(param, null, "keyword");
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

  _compareIgnoringDiacritics: null,

  /**
   * Returns a list of all the token substring matches in a string.  Matching is
   * case insensitive.  Each match in the returned list is a tuple: [matchIndex,
   * matchLength].  matchIndex is the index in the string of the match, and
   * matchLength is the length of the match.
   *
   * @param {Array} tokens The tokens to search for.
   * @param {string} str The string to match against.
   * @param {boolean} highlightType
   *   One of the HIGHLIGHT values:
   *     TYPED: match ranges matching the tokens; or
   *     SUGGESTED: match ranges for words not matching the tokens and the
   *                endings of words that start with a token.
   * @returns {Array} An array: [
   *            [matchIndex_0, matchLength_0],
   *            [matchIndex_1, matchLength_1],
   *            ...
   *            [matchIndex_n, matchLength_n]
   *          ].
   *          The array is sorted by match indexes ascending.
   */
  getTokenMatches(tokens, str, highlightType) {
    // Only search a portion of the string, because not more than a certain
    // amount of characters are visible in the UI, matching over what is visible
    // would be expensive and pointless.
    str = str.substring(0, UrlbarUtils.MAX_TEXT_LENGTH).toLocaleLowerCase();
    // To generate non-overlapping ranges, we start from a 0-filled array with
    // the same length of the string, and use it as a collision marker, setting
    // 1 where the text should be highlighted.
    let hits = new Array(str.length).fill(
      highlightType == this.HIGHLIGHT.SUGGESTED ? 1 : 0
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

        if (highlightType == UrlbarUtils.HIGHLIGHT.SUGGESTED) {
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
          highlightType == this.HIGHLIGHT.SUGGESTED ? 0 : 1,
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
            if (highlightType == UrlbarUtils.HIGHLIGHT.SUGGESTED) {
              let previousSpaceIndex = str.lastIndexOf(" ", index) + 1;
              if (index != previousSpaceIndex) {
                index += needle.length;
                continue;
              }
            }
            hits.fill(
              highlightType == this.HIGHLIGHT.SUGGESTED ? 0 : 1,
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
      if (totalTokensLength > UrlbarUtils.MAX_TEXT_LENGTH) {
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
   * Returns the group for a result.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @returns {UrlbarUtils.RESULT_GROUP}
   *   The reuslt's group.
   */
  getResultGroup(result) {
    if (result.group) {
      return result.group;
    }

    if (result.hasSuggestedIndex && !result.isSuggestedIndexRelativeToGroup) {
      return UrlbarUtils.RESULT_GROUP.SUGGESTED_INDEX;
    }
    if (result.heuristic) {
      switch (result.providerName) {
        case "AliasEngines":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS;
        case "Autofill":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_AUTOFILL;
        case "BookmarkKeywords":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD;
        case "HeuristicFallback":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_FALLBACK;
        case "Omnibox":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_OMNIBOX;
        case "RestrictKeywordsAutofill":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_RESTRICT_KEYWORD_AUTOFILL;
        case "TokenAliasEngines":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE;
        case "UrlbarProviderSearchTips":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_SEARCH_TIP;
        case "HistoryUrlHeuristic":
          return UrlbarUtils.RESULT_GROUP.HEURISTIC_HISTORY_URL;
        default:
          if (result.providerName.startsWith("TestProvider")) {
            return UrlbarUtils.RESULT_GROUP.HEURISTIC_TEST;
          }
          break;
      }
      if (result.providerType == UrlbarUtils.PROVIDER_TYPE.EXTENSION) {
        return UrlbarUtils.RESULT_GROUP.HEURISTIC_EXTENSION;
      }
      console.error(
        "Returning HEURISTIC_FALLBACK for unrecognized heuristic result: ",
        result
      );
      return UrlbarUtils.RESULT_GROUP.HEURISTIC_FALLBACK;
    }

    switch (result.providerName) {
      case "AboutPages":
        return UrlbarUtils.RESULT_GROUP.ABOUT_PAGES;
      case "InputHistory":
        return UrlbarUtils.RESULT_GROUP.INPUT_HISTORY;
      case "UrlbarProviderQuickSuggest":
        return UrlbarUtils.RESULT_GROUP.GENERAL_PARENT;
      default:
        break;
    }

    switch (result.type) {
      case UrlbarUtils.RESULT_TYPE.SEARCH:
        if (result.source == UrlbarUtils.RESULT_SOURCE.HISTORY) {
          return result.providerName == "RecentSearches"
            ? UrlbarUtils.RESULT_GROUP.RECENT_SEARCH
            : UrlbarUtils.RESULT_GROUP.FORM_HISTORY;
        }
        if (result.payload.tail && !result.isRichSuggestion) {
          return UrlbarUtils.RESULT_GROUP.TAIL_SUGGESTION;
        }
        if (result.payload.suggestion) {
          return UrlbarUtils.RESULT_GROUP.REMOTE_SUGGESTION;
        }
        break;
      case UrlbarUtils.RESULT_TYPE.OMNIBOX:
        return UrlbarUtils.RESULT_GROUP.OMNIBOX;
      case UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
        return UrlbarUtils.RESULT_GROUP.REMOTE_TAB;
      case UrlbarUtils.RESULT_TYPE.RESTRICT:
        return UrlbarUtils.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD;
    }
    return UrlbarUtils.RESULT_GROUP.GENERAL;
  },

  /**
   * Extracts the URL from a result.
   *
   * @param {UrlbarResult} result
   *   The result to extract from.
   * @returns {object}
   *   An object: `{ url, postData }`
   *   `url` will be null if the result doesn't have a URL. `postData` will be
   *   null if the result doesn't have post data.
   */
  getUrlFromResult(result) {
    if (
      result.type == UrlbarUtils.RESULT_TYPE.SEARCH &&
      result.payload.engine
    ) {
      const engine = Services.search.getEngineByName(result.payload.engine);
      let [url, postData] = this.getSearchQueryUrl(
        engine,
        result.payload.suggestion || result.payload.query
      );
      return { url, postData };
    }

    return {
      url: result.payload.url ?? null,
      postData: result.payload.postData
        ? this.getPostDataStream(result.payload.postData)
        : null,
    };
  },

  /**
   * Get the url to load for the search query.
   *
   * @param {nsISearchEngine} engine
   *   The engine to generate the query for.
   * @param {string} query
   *   The query string to search for.
   * @returns {Array}
   *   Returns an array containing the query url (string) and the
   *    post data (object).
   */
  getSearchQueryUrl(engine, query) {
    let submission = engine.getSubmission(query, null, "keyword");
    return [submission.uri.spec, submission.postData];
  },

  // Ranks a URL prefix from 3 - 0 with the following preferences:
  // https:// > https://www. > http:// > http://www.
  // Higher is better for the purposes of deduping URLs.
  // Returns -1 if the prefix does not match any of the above.
  getPrefixRank(prefix) {
    return ["http://www.", "http://", "https://www.", "https://"].indexOf(
      prefix
    );
  },

  /**
   * Gets the number of rows a result should span in the view.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {bool} includeHiddenExposures
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
      case UrlbarUtils.RESULT_TYPE.URL:
      case UrlbarUtils.RESULT_TYPE.BOOKMARKS:
      case UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
      case UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
      case UrlbarUtils.RESULT_TYPE.KEYWORD:
      case UrlbarUtils.RESULT_TYPE.SEARCH:
      case UrlbarUtils.RESULT_TYPE.OMNIBOX:
        return 1;
      case UrlbarUtils.RESULT_TYPE.TIP:
        return 3;
    }
    return 1;
  },

  /**
   * Gets a default icon for a URL.
   *
   * @param {string} url
   *   The URL to get the icon for.
   * @returns {string} A URI pointing to an icon for `url`.
   */
  getIconForUrl(url) {
    if (typeof url == "string") {
      return UrlbarUtils.PROTOCOLS_WITH_ICONS.some(p => url.startsWith(p))
        ? "page-icon:" + url
        : UrlbarUtils.ICON.DEFAULT;
    }
    if (
      URL.isInstance(url) &&
      UrlbarUtils.PROTOCOLS_WITH_ICONS.includes(url.protocol)
    ) {
      return "page-icon:" + url.href;
    }
    return UrlbarUtils.ICON.DEFAULT;
  },

  /**
   * Returns a search mode object if a token should enter search mode when
   * typed. This does not handle engine aliases.
   *
   * @param {UrlbarUtils.RESTRICT} token
   *   A restriction token to convert to search mode.
   * @returns {object}
   *   A search mode object. Null if search mode should not be entered. See
   *   setSearchMode documentation for details.
   */
  searchModeForToken(token) {
    if (token == lazy.UrlbarTokenizer.RESTRICT.SEARCH) {
      return {
        engineName: lazy.UrlbarSearchUtils.getDefaultEngine(this.isPrivate)
          ?.name,
      };
    }

    let mode = UrlbarUtils.LOCAL_SEARCH_MODES.find(m => m.restrict == token);
    if (!mode) {
      return null;
    }

    // Return a copy so callers don't modify the object in LOCAL_SEARCH_MODES.
    return { ...mode };
  },

  /**
   * Tries to initiate a speculative connection to a given url.
   *
   * Note: This is not infallible, if a speculative connection cannot be
   *       initialized, it will be a no-op.
   *
   * @param {nsISearchEngine|nsIURI|URL|string} urlOrEngine entity to initiate
   *        a speculative connection for.
   * @param {window} window the window from where the connection is initialized.
   */
  setupSpeculativeConnection(urlOrEngine, window) {
    if (!lazy.UrlbarPrefs.get("speculativeConnect.enabled")) {
      return;
    }
    if (urlOrEngine instanceof Ci.nsISearchEngine) {
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
        null,
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
   * Strips parts of a URL defined in `options`.
   *
   * @param {string} spec
   *        The text to modify.
   * @param {object} [options]
   *        The options object.
   * @param {boolean} options.stripHttp
   *        Whether to strip http.
   * @param {boolean} options.stripHttps
   *        Whether to strip https.
   * @param {boolean} options.stripWww
   *        Whether to strip `www.`.
   * @param {boolean} options.trimSlash
   *        Whether to trim the trailing slash.
   * @param {boolean} options.trimEmptyQuery
   *        Whether to trim a trailing `?`.
   * @param {boolean} options.trimEmptyHash
   *        Whether to trim a trailing `#`.
   * @param {boolean} options.trimTrailingDot
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
   * Used to filter out the javascript protocol from URIs, since we don't
   * support LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL for those.
   *
   * @param {string} pasteData The data to check for javacript protocol.
   * @returns {string} The modified paste data.
   */
  stripUnsafeProtocolOnPaste(pasteData) {
    for (;;) {
      let scheme = "";
      try {
        scheme = Services.io.extractScheme(pasteData);
      } catch (ex) {
        // If it throws, this is not a javascript scheme.
      }
      if (scheme != "javascript") {
        break;
      }

      pasteData = pasteData.substring(pasteData.indexOf(":") + 1);
    }
    return pasteData;
  },

  /**
   * Add a (url, input) tuple to the input history table that drives adaptive
   * results.
   *
   * @param {string} url The url to add input history for
   * @param {string} input The associated search term
   */
  async addToInputHistory(url, input) {
    await lazy.PlacesUtils.withConnectionWrapper("addToInputHistory", db => {
      // use_count will asymptotically approach the max of 10.
      return db.executeCached(
        `
        INSERT OR REPLACE INTO moz_inputhistory
        SELECT h.id, IFNULL(i.input, :input), IFNULL(i.use_count, 0) * .9 + 1
        FROM moz_places h
        LEFT JOIN moz_inputhistory i ON i.place_id = h.id AND i.input = :input
        WHERE url_hash = hash(:url) AND url = :url
      `,
        { url, input: input.toLowerCase() }
      );
    });
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
   * Whether the passed-in input event is paste event.
   *
   * @param {DOMEvent} event an input DOM event.
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
    let match = lazy.UrlbarTokenizer.REGEXP_PREFIX.exec(str);
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
      !UrlbarUtils.PROTOCOLS_WITHOUT_AUTHORITY.includes(prefix.toLowerCase())
    ) {
      // Something that looks like a URI scheme but we won't treat as one:
      // e.g. "localhost:8888"
      return ["", str];
    }
    return [prefix, str.substring(prefix.length)];
  },

  /**
   * Runs a search for the given string, and returns the heuristic result.
   *
   * @param {string} searchString The string to search for.
   * @param {nsIDOMWindow} window The window requesting it.
   * @returns {UrlbarResult} an heuristic result.
   */
  async getHeuristicResultFor(searchString, window) {
    if (!searchString) {
      throw new Error("Must pass a non-null search string");
    }

    let options = {
      allowAutofill: false,
      isPrivate: lazy.PrivateBrowsingUtils.isWindowPrivate(window),
      maxResults: 1,
      searchString,
      userContextId: parseInt(
        window.gBrowser.selectedBrowser.getAttribute("usercontextid") || 0
      ),
      prohibitRemoteResults: true,
      providers: ["AliasEngines", "BookmarkKeywords", "HeuristicFallback"],
    };
    if (window.gURLBar.searchMode) {
      let searchMode = window.gURLBar.searchMode;
      options.searchMode = searchMode;
      if (searchMode.source) {
        options.sources = [searchMode.source];
      }
    }
    let context = new UrlbarQueryContext(options);
    await lazy.UrlbarProvidersManager.startQuery(context);
    if (!context.heuristicResult) {
      throw new Error("There should always be an heuristic result");
    }
    return context.heuristicResult;
  },

  /**
   * Creates a console logger.
   * Logging level can be controlled through the `browser.urlbar.loglevel`
   * preference.
   *
   * @param {object} [options] Options for the logger.
   * @param {string} [options.prefix] Prefix to use for the logged messages.
   * @returns {ConsoleInstance} The console logger.
   */
  getLogger({ prefix = "" } = {}) {
    if (!this._loggers) {
      this._loggers = new Map();
    }
    let logger = this._loggers.get(prefix);
    if (!logger) {
      logger = console.createInstance({
        prefix: `URLBar${prefix ? " - " + prefix : ""}`,
        maxLogLevelPref: "browser.urlbar.loglevel",
      });
      this._loggers.set(prefix, logger);
    }
    return logger;
  },

  /**
   * Returns the name of a result source.  The name is the lowercase name of the
   * corresponding property in the RESULT_SOURCE object.
   *
   * @param {string} source A UrlbarUtils.RESULT_SOURCE value.
   * @returns {string} The token's name, a lowercased name in the RESULT_SOURCE
   *   object.
   */
  getResultSourceName(source) {
    if (!this._resultSourceNamesBySource) {
      this._resultSourceNamesBySource = new Map();
      for (let [name, src] of Object.entries(this.RESULT_SOURCE)) {
        this._resultSourceNamesBySource.set(src, name.toLowerCase());
      }
    }
    return this._resultSourceNamesBySource.get(source);
  },

  /**
   * Add the search to form history.  This also updates any existing form
   * history for the search.
   *
   * @param {UrlbarInput} input The UrlbarInput object requesting the addition.
   * @param {string} value The value to add.
   * @param {string} [source] The source of the addition, usually
   *        the name of the engine the search was made with.
   * @returns {Promise} resolved once the operation is complete
   */
  addToFormHistory(input, value, source) {
    // If the user types a search engine alias without a search string,
    // we have an empty search string and we can't bump it.
    // We also don't want to add history in private browsing mode.
    // Finally we don't want to store extremely long strings that would not be
    // particularly useful to the user.
    if (
      !value ||
      input.isPrivate ||
      value.length >
        lazy.SearchSuggestionController.SEARCH_HISTORY_MAX_VALUE_LENGTH
    ) {
      return Promise.resolve();
    }
    return lazy.FormHistory.update({
      op: "bump",
      fieldname: input.formHistoryName,
      value,
      source,
    });
  },

  /**
   * Returns whether a URL can be autofilled from a candidate string. This
   * function is specifically designed for origin and up-to-the-next-slash URL
   * autofill. It should not be used for other types of autofill.
   *
   * @param {string} url
   *                 The URL to test
   * @param {string} candidate
   *                 The candidate string to test against
   * @param {string} checkFragmentOnly
   *                 If want to check the fragment only, pass true.
   *                 Otherwise, check whole url.
   * @returns {boolean} true: can autofill
   */
  canAutofillURL(url, candidate, checkFragmentOnly = false) {
    // If the URL does not start with the candidate, it can't be autofilled.
    // The length check is an optimization to short-circuit the `startsWith()`.
    if (
      !checkFragmentOnly &&
      (url.length <= candidate.length ||
        !url.toLocaleLowerCase().startsWith(candidate.toLocaleLowerCase()))
    ) {
      return false;
    }

    // Create `URL` objects to make the logic below easier. The strings must
    // include schemes for this to work.
    if (!lazy.UrlbarTokenizer.REGEXP_PREFIX.test(url)) {
      url = "http://" + url;
    }
    if (!lazy.UrlbarTokenizer.REGEXP_PREFIX.test(candidate)) {
      candidate = "http://" + candidate;
    }

    url = URL.parse(url);
    candidate = URL.parse(candidate);
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
   * Extracts a telemetry type from a result, used by scalars and event
   * telemetry.
   *
   * @param {UrlbarResult} result The result to analyze.
   * @returns {string} A string type for telemetry.
   */
  telemetryTypeFromResult(result) {
    if (!result) {
      return "unknown";
    }
    switch (result.type) {
      case UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
        return "switchtab";
      case UrlbarUtils.RESULT_TYPE.SEARCH:
        if (result.providerName == "RecentSearches") {
          return "recent_search";
        }
        if (result.source == UrlbarUtils.RESULT_SOURCE.HISTORY) {
          return "formhistory";
        }
        if (result.providerName == "TabToSearch") {
          return "tabtosearch";
        }
        if (result.payload.suggestion) {
          let type = result.payload.trending ? "trending" : "searchsuggestion";
          if (result.isRichSuggestion) {
            type += "_rich";
          }
          return type;
        }
        return "searchengine";
      case UrlbarUtils.RESULT_TYPE.URL:
        if (result.autofill) {
          let { type } = result.autofill;
          if (!type) {
            type = "other";
            console.error(
              new Error(
                "`result.autofill.type` not set, falling back to 'other'"
              )
            );
          }
          return `autofill_${type}`;
        }
        if (
          result.source == UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL &&
          result.heuristic
        ) {
          return "visiturl";
        }
        if (result.providerName == "UrlbarProviderQuickSuggest") {
          return "quicksuggest";
        }
        if (result.providerName == "UrlbarProviderClipboard") {
          return "clipboard";
        }
        {
          let type =
            result.source == UrlbarUtils.RESULT_SOURCE.BOOKMARKS
              ? "bookmark"
              : "history";
          if (result.providerName == "InputHistory") {
            return type + "adaptive";
          }
          return type;
        }
      case UrlbarUtils.RESULT_TYPE.KEYWORD:
        return "keyword";
      case UrlbarUtils.RESULT_TYPE.OMNIBOX:
        return "extension";
      case UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
        return "remotetab";
      case UrlbarUtils.RESULT_TYPE.TIP:
        return "tip";
      case UrlbarUtils.RESULT_TYPE.DYNAMIC:
        if (result.providerName == "TabToSearch") {
          // This is the onboarding result.
          return "tabtosearch";
        }
        return "dynamic";
      case UrlbarUtils.RESULT_TYPE.RESTRICT:
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.BOOKMARK) {
          return "restrict_keyword_bookmarks";
        }
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.OPENPAGE) {
          return "restrict_keyword_tabs";
        }
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.HISTORY) {
          return "restrict_keyword_history";
        }
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.ACTION) {
          return "restrict_keyword_actions";
        }
    }
    return "unknown";
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
    return uri.length > UrlbarUtils.MAX_TEXT_LENGTH
      ? uri
      : Services.textToSubURI.unEscapeURIForUI(uri);
  },

  /**
   * Checks whether a given text has right-to-left direction or not.
   *
   * @param {string} value The text which should be check for RTL direction.
   * @param {Window} window The window where 'value' is going to be displayed.
   * @returns {boolean} Returns true if text has right-to-left direction and
   *                    false otherwise.
   */
  isTextDirectionRTL(value, window) {
    let directionality = window.windowUtils.getDirectionFromText(value);
    return directionality == window.windowUtils.DIRECTION_RTL;
  },

  /**
   * Unescape, decode punycode, and trim (both protocol and trailing slash)
   * the URL. Use for displaying purposes only!
   *
   * @param {string} url The url that should be prepared for display.
   * @param {object} [options] Preparation options.
   * @param {boolean} [options.trimURL] Whether the displayed URL should be
   *                  trimmed or not.
   * @param {boolean} [options.schemeless] Trim `http(s)://`.
   * @returns {string} Prepared url.
   */
  prepareUrlForDisplay(url, { trimURL = true, schemeless = false } = {}) {
    // Some domains are encoded in punycode. The following ensures we display
    // the url in utf-8.
    try {
      url = new URL(url).URI.displaySpec;
    } catch {} // In some cases url is not a valid url.

    if (url) {
      if (schemeless) {
        url = UrlbarUtils.stripPrefixAndTrim(url, {
          stripHttp: true,
          stripHttps: true,
        })[0];
      } else if (trimURL && lazy.UrlbarPrefs.get("trimURLs")) {
        url = lazy.BrowserUIUtils.removeSingleTrailingSlashFromURL(url);
        if (url.startsWith("https://")) {
          url = url.substring(8);
          if (url.startsWith("www.")) {
            url = url.substring(4);
          }
        }
      }
    }

    return this.unEscapeURIForUI(url);
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
      case UrlbarUtils.RESULT_GROUP.INPUT_HISTORY: {
        return "adaptive_history";
      }
      case UrlbarUtils.RESULT_GROUP.RECENT_SEARCH: {
        return "recent_search";
      }
      case UrlbarUtils.RESULT_GROUP.FORM_HISTORY: {
        return "search_history";
      }
      case UrlbarUtils.RESULT_GROUP.TAIL_SUGGESTION:
      case UrlbarUtils.RESULT_GROUP.REMOTE_SUGGESTION: {
        let group = result.payload.trending
          ? "trending_search"
          : "search_suggest";
        if (result.isRichSuggestion) {
          group += "_rich";
        }
        return group;
      }
      case UrlbarUtils.RESULT_GROUP.REMOTE_TAB: {
        return "remote_tab";
      }
      case UrlbarUtils.RESULT_GROUP.HEURISTIC_EXTENSION:
      case UrlbarUtils.RESULT_GROUP.HEURISTIC_OMNIBOX:
      case UrlbarUtils.RESULT_GROUP.OMNIBOX: {
        return "addon";
      }
      case UrlbarUtils.RESULT_GROUP.GENERAL: {
        return "general";
      }
      // Group of UrlbarProviderQuickSuggest is GENERAL_PARENT.
      case UrlbarUtils.RESULT_GROUP.GENERAL_PARENT: {
        return "suggest";
      }
      case UrlbarUtils.RESULT_GROUP.ABOUT_PAGES: {
        return "about_page";
      }
      case UrlbarUtils.RESULT_GROUP.SUGGESTED_INDEX: {
        return "suggested_index";
      }
      case UrlbarUtils.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD: {
        return "restrict_keyword";
      }
    }

    return result.heuristic ? "heuristic" : "unknown";
  },

  /**
   * Extracts a type for search engagement telemetry from a result.
   *
   * @param {UrlbarResult} result The result to analyze.
   * @param {string} selType An optional parameter for the selected type.
   * @returns {string} Type as string.
   */
  searchEngagementTelemetryType(result, selType = null) {
    if (!result) {
      return selType === "oneoff" ? "search_shortcut_button" : "input_field";
    }

    // While product doesn't use experimental addons anymore, tests may still do
    // for testing purposes.
    if (
      result.providerType === UrlbarUtils.PROVIDER_TYPE.EXTENSION &&
      result.providerName != "Omnibox"
    ) {
      return "experimental_addon";
    }

    switch (result.type) {
      case UrlbarUtils.RESULT_TYPE.DYNAMIC:
        switch (result.providerName) {
          case "calculator":
            return "calc";
          case "TabToSearch":
            return "tab_to_search";
          case "UnitConversion":
            return "unit";
          case "UrlbarProviderQuickSuggest":
            return this._getQuickSuggestTelemetryType(result);
          case "UrlbarProviderQuickSuggestContextualOptIn":
            return "fxsuggest_data_sharing_opt_in";
          case "UrlbarProviderGlobalActions":
          case "UrlbarProviderActionsSearchMode":
            return "action";
        }
        break;
      case UrlbarUtils.RESULT_TYPE.KEYWORD:
        return "keyword";
      case UrlbarUtils.RESULT_TYPE.OMNIBOX:
        return "addon";
      case UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
        return "remote_tab";
      case UrlbarUtils.RESULT_TYPE.SEARCH:
        if (result.providerName === "TabToSearch") {
          return "tab_to_search";
        }
        if (result.source == UrlbarUtils.RESULT_SOURCE.HISTORY) {
          return result.providerName == "RecentSearches"
            ? "recent_search"
            : "search_history";
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
      case UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
        return "tab";
      case UrlbarUtils.RESULT_TYPE.TIP:
        if (result.providerName === "UrlbarProviderInterventions") {
          switch (result.payload.type) {
            case lazy.UrlbarProviderInterventions.TIP_TYPE.CLEAR:
              return "intervention_clear";
            case lazy.UrlbarProviderInterventions.TIP_TYPE.REFRESH:
              return "intervention_refresh";
            case lazy.UrlbarProviderInterventions.TIP_TYPE.UPDATE_ASK:
            case lazy.UrlbarProviderInterventions.TIP_TYPE.UPDATE_CHECKING:
            case lazy.UrlbarProviderInterventions.TIP_TYPE.UPDATE_REFRESH:
            case lazy.UrlbarProviderInterventions.TIP_TYPE.UPDATE_RESTART:
            case lazy.UrlbarProviderInterventions.TIP_TYPE.UPDATE_WEB:
              return "intervention_update";
            default:
              return "intervention_unknown";
          }
        }

        switch (result.payload.type) {
          case lazy.UrlbarProviderSearchTips.TIP_TYPE.ONBOARD:
            return "tip_onboard";
          case lazy.UrlbarProviderSearchTips.TIP_TYPE.REDIRECT:
            return "tip_redirect";
          case "dismissalAcknowledgment":
            return "tip_dismissal_acknowledgment";
          default:
            return "tip_unknown";
        }
      case UrlbarUtils.RESULT_TYPE.URL:
        if (
          result.source === UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL &&
          result.heuristic
        ) {
          return "url";
        }
        if (result.autofill) {
          return `autofill_${result.autofill.type ?? "unknown"}`;
        }
        if (result.providerName === "UrlbarProviderQuickSuggest") {
          return this._getQuickSuggestTelemetryType(result);
        }
        if (result.providerName === "UrlbarProviderTopSites") {
          return "top_site";
        }
        if (result.providerName === "UrlbarProviderClipboard") {
          return "clipboard";
        }
        return result.source === UrlbarUtils.RESULT_SOURCE.BOOKMARKS
          ? "bookmark"
          : "history";
      case UrlbarUtils.RESULT_TYPE.RESTRICT:
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.BOOKMARK) {
          return "restrict_keyword_bookmarks";
        }
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.OPENPAGE) {
          return "restrict_keyword_tabs";
        }
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.HISTORY) {
          return "restrict_keyword_history";
        }
        if (result.payload.keyword === lazy.UrlbarTokenizer.RESTRICT.ACTION) {
          return "restrict_keyword_actions";
        }
    }

    return "unknown";
  },

  searchEngagementTelemetryAction(result) {
    if (result.providerName != "UrlbarProviderGlobalActions") {
      return result.payload.action?.key ?? "none";
    }
    return result.payload.actionsResults.map(({ key }) => key).join(",");
  },

  _getQuickSuggestTelemetryType(result) {
    if (result.payload.telemetryType == "weather") {
      // Return "weather" without the usual source prefix for consistency with
      // past reporting of weather suggestions.
      return "weather";
    }
    return result.payload.source + "_" + result.payload.telemetryType;
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
   * @param {boolean} overwrite
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
};

ChromeUtils.defineLazyGetter(UrlbarUtils.ICON, "DEFAULT", () => {
  return lazy.PlacesUtils.favicons.defaultFavicon.spec;
});

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
    // The remaining properties are related to l10n string caching. See
    // `L10nCache`. All are optional and are false by default.
    parseMarkup: {
      type: "boolean",
    },
    cacheable: {
      type: "boolean",
    },
    excludeArgsFromCacheKey: {
      type: "boolean",
    },
  },
};

/**
 * Payload JSON schemas for each result type.  Payloads are validated against
 * these schemas using JsonSchemaValidator.sys.mjs.
 */
UrlbarUtils.RESULT_PAYLOAD_SCHEMA = {
  [UrlbarUtils.RESULT_TYPE.TAB_SWITCH]: {
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
      displayUrl: {
        type: "string",
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
      title: {
        type: "string",
      },
      url: {
        type: "string",
      },
      userContextId: {
        type: "number",
      },
    },
  },
  [UrlbarUtils.RESULT_TYPE.SEARCH]: {
    type: "object",
    properties: {
      blockL10n: L10N_SCHEMA,
      description: {
        type: "string",
      },
      displayUrl: {
        type: "string",
      },
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
  [UrlbarUtils.RESULT_TYPE.URL]: {
    type: "object",
    required: ["url"],
    properties: {
      blockL10n: L10N_SCHEMA,
      bottomTextL10n: L10N_SCHEMA,
      description: {
        type: "string",
      },
      descriptionL10n: L10N_SCHEMA,
      dismissalKey: {
        type: "string",
      },
      displayUrl: {
        type: "string",
      },
      dupedHeuristic: {
        type: "boolean",
      },
      fallbackTitle: {
        type: "string",
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
      qsSuggestion: {
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
      url: {
        type: "string",
      },
      urlTimestampIndex: {
        type: "number",
      },
    },
  },
  [UrlbarUtils.RESULT_TYPE.KEYWORD]: {
    type: "object",
    required: ["keyword", "url"],
    properties: {
      displayUrl: {
        type: "string",
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
  [UrlbarUtils.RESULT_TYPE.OMNIBOX]: {
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
  [UrlbarUtils.RESULT_TYPE.REMOTE_TAB]: {
    type: "object",
    required: ["device", "url", "lastUsed"],
    properties: {
      device: {
        type: "string",
      },
      displayUrl: {
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
  [UrlbarUtils.RESULT_TYPE.TIP]: {
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
          "searchTip_onboard",
          "searchTip_redirect",
          "test", // for tests only
        ],
      },
    },
  },
  [UrlbarUtils.RESULT_TYPE.DYNAMIC]: {
    type: "object",
    required: ["dynamicType"],
    properties: {
      dynamicType: {
        type: "string",
      },
    },
  },
  [UrlbarUtils.RESULT_TYPE.RESTRICT]: {
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
};

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
   * @param {string} options.searchString
   *   The string the user entered in autocomplete. Could be the empty string
   *   in the case of the user opening the popup via the mouse.
   * @param {boolean} options.isPrivate
   *   Set to true if this query was started from a private browsing window.
   * @param {number} options.maxResults
   *   The maximum number of results that will be displayed for this query.
   * @param {boolean} options.allowAutofill
   *   Whether or not to allow providers to include autofill results.
   * @param {number} options.userContextId
   *   The container id where this context was generated, if any.
   * @param {Array} [options.sources]
   *   A list of acceptable UrlbarUtils.RESULT_SOURCE for the context.
   * @param {object} [options.searchMode]
   *   The input's current search mode.  See UrlbarInput.setSearchMode for a
   *   description.
   * @param {boolean} [options.prohibitRemoteResults]
   *   This provides a short-circuit override for `context.allowRemoteResults`.
   *   If it's false, then `allowRemoteResults` will do its usual checks to
   *   determine whether remote results are allowed. If it's true, then
   *   `allowRemoteResults` will immediately return false. Defaults to false.
   * @param {string} [options.formHistoryName]
   *   The name under which the local form history is registered.
   */
  constructor(options = {}) {
    this._checkRequiredOptions(options, [
      "allowAutofill",
      "isPrivate",
      "maxResults",
      "searchString",
    ]);

    if (isNaN(parseInt(options.maxResults))) {
      throw new Error(
        `Invalid maxResults property provided to UrlbarQueryContext`
      );
    }

    // Manage optional properties of options.
    for (let [prop, checkFn, defaultValue] of [
      ["currentPage", v => typeof v == "string" && !!v.length],
      ["formHistoryName", v => typeof v == "string" && !!v.length],
      ["prohibitRemoteResults", () => true, false],
      ["providers", v => Array.isArray(v) && v.length],
      ["searchMode", v => v && typeof v == "object"],
      ["sources", v => Array.isArray(v) && v.length],
      ["view", () => true],
    ]) {
      if (prop in options) {
        if (!checkFn(options[prop])) {
          throw new Error(`Invalid value for option "${prop}"`);
        }
        this[prop] = options[prop];
      } else if (defaultValue !== undefined) {
        this[prop] = defaultValue;
      }
    }

    this.lastResultCount = 0;
    // Note that Set is not serializable through JSON, so these may not be
    // easily shared with add-ons.
    this.pendingHeuristicProviders = new Set();
    this.deferUserSelectionProviders = new Set();
    this.trimmedSearchString = this.searchString.trim();
    this.lowerCaseSearchString = this.searchString.toLowerCase();
    this.trimmedLowerCaseSearchString = this.trimmedSearchString.toLowerCase();
    this.userContextId =
      lazy.UrlbarProviderOpenTabs.getUserContextIdForOpenPagesTable(
        options.userContextId,
        this.isPrivate
      ) || Ci.nsIScriptSecurityManager.DEFAULT_USER_CONTEXT_ID;

    // Used to store glean timing distribution timer ids.
    this.firstTimerId = 0;
    this.sixthTimerId = 0;
  }

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
   * @returns {{ href: string; isSearch: boolean; }?}
   */
  get fixupInfo() {
    if (!this._fixupError && !this._fixupInfo && this.trimmedSearchString) {
      let flags =
        Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
        Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
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

    // Disallow remote results if only an origin is typed to avoid disclosing
    // sites the user visits. This also catches partially typed origins, like
    // mozilla.o, because the fixup check below can't validate them.
    if (
      this.tokens.length == 1 &&
      this.tokens[0].type == lazy.UrlbarTokenizer.TYPE.POSSIBLE_ORIGIN
    ) {
      return false;
    }

    // Disallow remote results for strings containing tokens that look like URIs
    // to avoid disclosing information about networks and passwords.
    if (this.fixupInfo?.href && !this.fixupInfo?.isSearch) {
      return false;
    }

    // Allow remote results.
    return true;
  }
}

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
   * @abstract
   */
  sort(_queryContext) {
    throw new Error("Trying to access the base class, must be overridden");
  }
}

/**
 * Base class for a provider.
 * The provider scope is to query a datasource and return results from it.
 */
export class UrlbarProvider {
  constructor() {
    ChromeUtils.defineLazyGetter(this, "logger", () =>
      UrlbarUtils.getLogger({ prefix: `Provider.${this.name}` })
    );
  }

  /**
   * Unique name for the provider, used by the context to filter on providers.
   * Not using a unique name will cause the newest registration to win.
   *
   * @abstract
   */
  get name() {
    return "UrlbarProviderBase";
  }

  /**
   * The type of the provider, must be one of UrlbarUtils.PROVIDER_TYPE.
   *
   * @abstract
   */
  get type() {
    throw new Error("Trying to access the base class, must be overridden");
  }

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
   * @param {UrlbarQueryContext} _queryContext The query context object
   * @returns {boolean} Whether this provider should be invoked for the search.
   * @abstract
   */
  isActive(_queryContext) {
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
   * @param {UrlbarQueryContext} _queryContext The query context object
   * @param {Function} _addCallback Callback invoked by the provider to add a new
   *        result. A UrlbarResult should be passed to it.
   * @abstract
   */
  startQuery(_queryContext, _addCallback) {
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
   * @param {UrlbarController} _controller
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
   *       UrlbarController.sys.mjs.
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
   * @param {UrlbarController} _controller
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
   * @param {UrlbarController} _controller
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
   * @param {UrlbarController} _controller
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
   * This is called only for dynamic result types, when the urlbar view updates
   * the view of one of the results of the provider.  It should return an object
   * describing the view update that looks like this:
   *
   *   {
   *     nodeNameFoo: {
   *       attributes: {
   *         someAttribute: someValue,
   *       },
   *       style: {
   *         someStyleProperty: someValue,
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
   * UrlbarView.addDynamicViewTemplate().  The values are similar to the nested
   * objects specified in the view template but not quite the same; see below.
   * For each property, the element in the view subtree with the specified name
   * is updated according to the object in the property's value.  If an
   * element's name is not specified, then it will not be updated and will
   * retain its current state.
   *
   * @param {UrlbarResult} _result
   *   The result whose view will be updated.
   * @param {Map} _idsByName
   *   A Map from an element's name, as defined by the provider; to its ID in
   *   the DOM, as defined by the browser. The browser manages element IDs for
   *   dynamic results to prevent collisions. However, a provider may need to
   *   access the IDs of the elements created for its results. For example, to
   *   set various `aria` attributes.
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
  getViewUpdate(_result, _idsByName) {
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
   * @returns {Array}
   *   If the result doesn't have any commands, this should return null.
   *   Otherwise it should return an array of command objects that look like:
   *   `{ name, l10n, children}`
   *
   *   {string} name
   *     The name of the command. Must be specified unless `children` is
   *     present. When a command is picked, its name will be passed as
   *     `details.selType` to `onEngagement()`. The special name "separator"
   *     will create a menu separator.
   *   {object} l10n
   *     An l10n object for the command's label: `{ id, args }`
   *     Must be specified unless `name` is "separator".
   *   {array} children
   *     If specified, a submenu will be created with the given child commands.
   *     Each object in the array must be a command object.
   */
  getResultCommands(_result) {
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
   * @param {object} options An object that configures the timer
   * @param {string} options.name The name of the timer, logged when necessary
   * @param {Function} options.callback To be invoked when requested
   * @param {number} options.time A delay in milliseconds to wait for
   * @param {boolean} options.reportErrorOnTimeout If true and the timer times
   *                  out, an error will be logged with Cu.reportError
   * @param {logger} options.logger An optional logger
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
 * This class implements a cache for l10n strings. Cached strings can be
 * accessed synchronously, avoiding the asynchronicity of `data-l10n-id` and
 * `document.l10n.setAttributes`, which can lead to text pop-in and flickering
 * as strings are fetched from Fluent. (`document.l10n.formatValueSync` is also
 * sync but should not be used since it may perform sync I/O.)
 *
 * Values stored and returned by the cache are JS objects similar to
 * `L10nMessage` objects, not bare strings. This allows the cache to store not
 * only l10n strings with bare values but also strings that define attributes
 * (e.g., ".label = My label value"). See `get` for details.
 */
export class L10nCache {
  /**
   * @param {Localization} l10n
   *   A `Localization` object like `document.l10n`. This class keeps a weak
   *   reference to this object, so the caller or something else must hold onto
   *   it.
   */
  constructor(l10n) {
    this.l10n = Cu.getWeakReference(l10n);
    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);
    Services.obs.addObserver(this, "intl:app-locales-changed", true);
  }

  /**
   * Gets a cached l10n message.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} options.args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @param {boolean} options.excludeArgsFromCacheKey
   *   Pass true if the string was cached using a key that excluded arguments.
   * @returns {object}
   *   The message object or undefined if it's not cached. The message object is
   *   similar to `L10nMessage` (defined in Localization.webidl) but its
   *   attributes are stored differently for convenience. It looks like this:
   *
   *     { value, attributes }
   *
   *   The properties are:
   *
   *     {string} value
   *       The bare value of the string. If the string does not have a bare
   *       value (i.e., it has only attributes), this will be null.
   *     {object} attributes
   *       A mapping from attribute names to their values. If the string doesn't
   *       have any attributes, this will be null.
   *
   *   For example, if we cache these strings from an ftl file:
   *
   *     foo = Foo's value
   *     bar =
   *       .label = Bar's label value
   *
   *   Then:
   *
   *     cache.get("foo")
   *     // => { value: "Foo's value", attributes: null }
   *     cache.get("bar")
   *     // => { value: null, attributes: { label: "Bar's label value" }}
   */
  get({ id, args = undefined, excludeArgsFromCacheKey = false }) {
    return this._messagesByKey.get(
      this._key({ id, args, excludeArgsFromCacheKey })
    );
  }

  /**
   * Fetches a string from Fluent and caches it.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} options.args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @param {boolean} options.excludeArgsFromCacheKey
   *   Pass true to cache the string using a key that excludes the arguments.
   *   The string will be cached only by its ID. This is useful if the string is
   *   used only once in the UI, its arguments vary, and it's acceptable to
   *   fetch and show a cached value with old arguments until the string is
   *   relocalized with new arguments.
   */
  async add({ id, args = undefined, excludeArgsFromCacheKey = false }) {
    let l10n = this.l10n.get();
    if (!l10n) {
      return;
    }
    let messages = await l10n.formatMessages([{ id, args }]);
    if (!messages?.length) {
      console.error(
        "l10n.formatMessages returned an unexpected value for ID: ",
        id
      );
      return;
    }
    let message = messages[0];
    if (message.attributes) {
      // Convert `attributes` from an array of `{ name, value }` objects to one
      // object mapping names to values.
      message.attributes = message.attributes.reduce(
        (valuesByName, { name, value }) => {
          valuesByName[name] = value;
          return valuesByName;
        },
        {}
      );
    }
    this._messagesByKey.set(
      this._key({ id, args, excludeArgsFromCacheKey }),
      message
    );
  }

  /**
   * Fetches and caches a string if it's not already cached. This is just a
   * slight optimization over `add` that avoids calling into Fluent
   * unnecessarily.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} options.args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @param {boolean} options.excludeArgsFromCacheKey
   *   Pass true to cache the string using a key that excludes the arguments.
   *   The string will be cached only by its ID. See `add()` for more.
   */
  async ensure({ id, args = undefined, excludeArgsFromCacheKey = false }) {
    // Always re-cache if `excludeArgsFromCacheKey` is true. The values in
    // `args` may be different from the values in the cached string.
    if (excludeArgsFromCacheKey || !this.get({ id, args })) {
      await this.add({ id, args, excludeArgsFromCacheKey });
    }
  }

  /**
   * Fetches and caches strings that aren't already cached.
   *
   * @param {Array} objects
   *   An array of objects as passed to `ensure()`.
   */
  async ensureAll(objects) {
    let promises = [];
    for (let obj of objects) {
      promises.push(this.ensure(obj));
    }
    await Promise.all(promises);
  }

  /**
   * Removes a cached string.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} options.args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @param {boolean} options.excludeArgsFromCacheKey
   *   Pass true if the string was cached using a key that excludes the
   *   arguments. If true, `args` is ignored.
   */
  delete({ id, args = undefined, excludeArgsFromCacheKey = false }) {
    this._messagesByKey.delete(
      this._key({ id, args, excludeArgsFromCacheKey })
    );
  }

  /**
   * Removes all cached strings.
   */
  clear() {
    this._messagesByKey.clear();
  }

  /**
   * Returns the number of cached messages.
   *
   * @returns {number}
   */
  size() {
    return this._messagesByKey.size;
  }

  /**
   * Sets an element's content or attribute to a cached l10n string. If the
   * string isn't cached, then this falls back to the usual
   * `document.l10n.setAttributes()` using the given l10n ID and args, which
   * means the string will pop in on a later animation frame.
   *
   * This also optionally caches the string so that it will be ready the next
   * time this is called for it. The function returns a promise that will be
   * resolved when the string has been cached. Typically there's no need to
   * await it unless you want to be sure the string is cached before continuing.
   *
   * @param {Element} element
   *   The l10n string will be applied to this element.
   * @param {object} options
   *   Options object.
   * @param {string} options.id
   *   The l10n string ID.
   * @param {object} options.args
   *   The l10n string arguments.
   * @param {string} options.attribute
   *   If the string applies to an attribute on the element, pass the name of
   *   the attribute. The string in the Fluent file should define a value for
   *   the attribute, like ".foo = My value". If the string applies to the
   *   element's content, leave this undefined.
   * @param {boolean} options.parseMarkup
   *   This controls whether the cached string is applied to the element's
   *   `textContent` or its `innerHTML`. It's not relevant if the string is
   *   applied to an attribute. Typically it should be set to true when the
   *   string is expected to contain markup. When true, the cached string is
   *   essentially assigned to the element's `innerHTML`. When false, it's
   *   assigned to the element's `textContent`.
   * @param {boolean} options.cacheable
   *   Whether the string should be cached in addition to applying it to the
   *   given element.
   * @param {boolean} options.excludeArgsFromCacheKey
   *   This affects how the string is stored in and fetched from the cache and
   *   is only relevant if the string has arguments. When true, all formatted
   *   values of the string share the same cache entry regardless of the
   *   arguments they were formatted with. In other words, only the ID matters.
   *   When false, formatted values with different arguments have separate cache
   *   entries. Typically it should be true when the number of possible argument
   *   values is unbounded and false otherwise. For example, it should be true
   *   if the argument is a user search string since that could be anything. It
   *   should be false if the argument is the name of an installed search engine
   *   since there's a relatively small number of those.
   *
   *   If `cacheable` is false but you previously cached the string using
   *   another function, you should pass the same value you passed for
   *   `excludeArgsFromCacheKey` when you cached it.
   * @returns {Promise|null}
   *   If `cacheable` is true, this returns a promise that's resolved when the
   *   string has been cached. Otherwise it returns null.
   */
  setElementL10n(
    element,
    {
      id,
      args = undefined,
      attribute = undefined,
      parseMarkup = false,
      cacheable = false,
      excludeArgsFromCacheKey = false,
    }
  ) {
    let message = this.get({ id, args, excludeArgsFromCacheKey });
    if (message) {
      element.removeAttribute("data-l10n-id");
      element.removeAttribute("data-l10n-attrs");
      element.removeAttribute("data-l10n-args");
      if (attribute) {
        element.setAttribute(attribute, message.attributes[attribute]);
      } else if (!parseMarkup) {
        element.textContent = message.value;
      } else {
        element.innerHTML = "";
        element.append(
          lazy.parserUtils.parseFragment(
            message.value,
            Ci.nsIParserUtils.SanitizerDropNonCSSPresentation |
              Ci.nsIParserUtils.SanitizerDropForms |
              Ci.nsIParserUtils.SanitizerDropMedia,
            false,
            Services.io.newURI(element.ownerDocument.documentURI),
            element
          )
        );
      }
    }

    // If the message wasn't cached, set the element's l10n attributes and let
    // `DOMLocalization` do its asynchronous translation. The element's content
    // will pop in when translation finishes.
    //
    // Also do this if the message was cached but its args aren't part of the
    // cache key because in that case the cached message may contain outdated
    // arg values. We just set the element's content to the old message above,
    // and when `DOMLocalization` finishes translating the new message, it will
    // set the element's content again. If the old and new args are different,
    // the new content will pop in. If they're the same, nothing will appear to
    // change.
    if (!message || (cacheable && excludeArgsFromCacheKey)) {
      if (attribute) {
        element.setAttribute("data-l10n-attrs", attribute);
      } else {
        element.removeAttribute("data-l10n-attrs");
      }
      element.ownerDocument.l10n.setAttributes(element, id, args);
    }

    if (cacheable) {
      // Cache the string. We specifically do not do this first and await it
      // because the whole point of the l10n cache is to synchronously update
      // the element's content when possible. Here, we return a promise rather
      // than making this function async and awaiting so it's clearer to callers
      // that they probably don't need to wait for caching to finish.
      return this.ensure({ id, args, excludeArgsFromCacheKey });
    }
    return null;
  }

  /**
   * Removes content and attributes set by `setElementL10n()`.
   *
   * @param {Element} element
   *   The content and attributes will be removed from this element.
   * @param {object} options
   *   Options object.
   * @param {string} options.attribute
   *   If you passed an attribute to `setElementL10n()`, pass it here too.
   */
  removeElementL10n(element, { attribute = undefined } = {}) {
    if (attribute) {
      element.removeAttribute(attribute);
      element.removeAttribute("data-l10n-attrs");
    } else {
      element.textContent = "";
    }
    element.removeAttribute("data-l10n-id");
    element.removeAttribute("data-l10n-args");
  }

  /**
   * Observer method from Services.obs.addObserver.
   *
   * @param {nsISupports} subject
   *   The subject of the notification.
   * @param {string} topic
   *   The topic of the notification.
   */
  async observe(subject, topic) {
    switch (topic) {
      case "intl:app-locales-changed": {
        await this.l10n.ready;
        this.clear();
        break;
      }
    }
  }

  /**
   * Cache keys => cached message objects
   */
  _messagesByKey = new Map();

  /**
   * Returns a cache key for a string in `_messagesByKey`.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} options.args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @param {boolean} options.excludeArgsFromCacheKey
   *   Pass true to exclude the arguments from the key and include only the ID.
   * @returns {string}
   *   The cache key.
   */
  _key({ id, args, excludeArgsFromCacheKey }) {
    // Keys are `id` plus JSON'ed `args` values. `JSON.stringify` doesn't
    // guarantee a particular ordering of object properties, so instead of
    // stringifying `args` as is, sort its entries by key and then pull out the
    // values. The final key is a JSON'ed array of `id` concatenated with the
    // sorted-by-key `args` values.
    args = (!excludeArgsFromCacheKey && args) || [];
    let argValues = Object.entries(args)
      .sort(([key1], [key2]) => key1.localeCompare(key2))
      .map(([_, value]) => value);
    let parts = [id].concat(argValues);
    return JSON.stringify(parts);
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
