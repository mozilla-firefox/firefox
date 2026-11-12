/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a tokenizer to be used by the urlbar model.
 * Emitted tokens are objects in the shape { type, value }, where type is one
 * of UrlbarShared.TOKEN_TYPE.
 */

import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import { SearchMode } from "chrome://browser/content/urlbar/UrlbarInputBase.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
  logger: () => UrlbarShared.getLogger({ prefix: "Tokenizer" }),
  gFluentStrings: () => new Localization(["browser/browser.ftl"]),
});

/**
 * @typedef UrlbarSearchStringTokenData
 * @property {Values<typeof UrlbarShared.TOKEN_TYPE>} type
 *   The type of the token.
 * @property {string} value
 *   The value of the token.
 * @property {string} lowerCaseValue
 *   The lower case version of the value.
 */

/**
 * This Map stores key-value pairs where each key is a restrict token
 * and each value is an array containing the localized keyword and the
 * english keyword.
 *
 * For example,
 * "*" maps to "Bookmarks" for english locales
 * "*" maps to "Marcadores, Bookmarks" for es-ES
 *
 * @type {Map<string, string[]>}
 */
let tokenToKeywords = new Map();

export var UrlbarTokenizer = {
  async loadL10nRestrictKeywords() {
    let l10nKeywords = await lazy.gFluentStrings.formatValues(
      UrlbarShared.LOCAL_SEARCH_MODES.map(mode => {
        let name = UrlbarShared.getResultSourceName(mode.source);
        return { id: `urlbar-search-mode-${name}` };
      })
    );

    let englishSearchStrings = new Localization([
      "preview/enUS-searchFeatures.ftl",
    ]);

    let englishKeywords = await englishSearchStrings.formatValues(
      UrlbarShared.LOCAL_SEARCH_MODES.map(mode => {
        let name = UrlbarShared.getResultSourceName(mode.source);
        return { id: `urlbar-search-mode-${name}-en` };
      })
    );

    for (let { restrict } of UrlbarShared.LOCAL_SEARCH_MODES) {
      let uniqueKeywords = [
        ...new Set([l10nKeywords.shift(), englishKeywords.shift()]),
      ];

      tokenToKeywords.set(restrict, uniqueKeywords);
    }
  },

  /**
   * Gets the cached localized restrict keywords. If keywords are not cached
   * fetch the localized keywords first and then return the keywords.
   */
  async getL10nRestrictKeywords() {
    if (tokenToKeywords.size === 0) {
      await this.loadL10nRestrictKeywords();
    }

    return tokenToKeywords;
  },

  /**
   * Tokenizes the searchString from a UrlbarQueryContext.
   *
   * @param {object} context
   * @param {string} context.searchString
   * @param {SearchMode} [context.searchMode]
   * @param {string} context.trimmedSearchString
   * @returns {UrlbarSearchStringTokenData[]}
   *  The tokens associated with the query.
   */
  tokenize(context) {
    lazy.logger.debug("Tokenizing search string", {
      searchString: context.searchString,
    });
    if (!context.trimmedSearchString) {
      return [];
    }
    let unfiltered = splitString(context);
    return filterTokens(unfiltered);
  },

  /**
   * Given a token, tells if it's a restriction token.
   *
   * @param {object} token
   *   The token to check.
   * @returns {boolean} Whether the token is a restriction character.
   */
  isRestrictionToken(token) {
    return (
      token &&
      token.type >= UrlbarShared.TOKEN_TYPE.RESTRICT_HISTORY &&
      token.type <= UrlbarShared.TOKEN_TYPE.RESTRICT_SEARCH
    );
  },

  /**
   * Returns the host and path of a token that looks like an http(s) origin or
   * URL, like "example.com" or "https://example.com/foo".
   *
   * @param {UrlbarSearchStringTokenData} token
   * @returns {{host: string, path: string}|null}
   *   `host` is the lower case hostname, without any leading "www." or trailing
   *   dot, and `path` the unparsed text from the first "/" after the host, or an
   *   empty string. Null if the token is not an http(s) origin or URL, or has
   *   user info.
   */
  getOriginInfo(token) {
    if (
      token.type != UrlbarShared.TOKEN_TYPE.POSSIBLE_ORIGIN &&
      token.type !=
        UrlbarShared.TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED &&
      token.type != UrlbarShared.TOKEN_TYPE.POSSIBLE_URL
    ) {
      return null;
    }
    let prefix = lazy.UrlUtils.REGEXP_PREFIX.exec(token.value)?.[0] ?? "";
    let url = URL.parse(prefix ? token.value : `http://${token.value}`);
    if (
      !url ||
      !["http:", "https:"].includes(url.protocol) ||
      url.username ||
      url.password ||
      !url.hostname
    ) {
      return null;
    }
    let rest = token.value.slice(prefix.length);
    let slashIndex = rest.indexOf("/");
    return {
      host: UrlbarShared.stripPrefixAndTrim(url.hostname, {
        stripWww: true,
        trimTrailingDot: true,
      })[0],
      path: slashIndex == -1 ? "" : rest.slice(slashIndex),
    };
  },
};

/** @type {Map<string, Values<typeof UrlbarShared.RESTRICT_TOKENS>>} */
const CHAR_TO_TYPE_MAP = new Map(
  Object.entries(UrlbarShared.RESTRICT_TOKENS).map(([type, char]) => [
    char,
    UrlbarShared.TOKEN_TYPE[`RESTRICT_${type}`],
  ])
);

/**
 * Given a queryContext object, splits its searchString into string tokens.
 *
 * @param {object} context
 * @param {string} context.searchString
 * @param {SearchMode} [context.searchMode]
 * @returns {string[]} An array of string tokens.
 */
function splitString({ searchString, searchMode }) {
  // The first step is splitting on unicode whitespaces. We ignore whitespaces
  // if the search string starts with "data:", to better support Web developers
  // and compatiblity with other browsers.
  let trimmed = searchString.trim();
  let tokens;
  if (trimmed.startsWith("data:")) {
    tokens = [trimmed];
  } else if (trimmed.length < 500) {
    tokens = trimmed.split(lazy.UrlUtils.REGEXP_SPACES);
  } else {
    // If the string is very long, tokenizing all of it would be expensive. So
    // we only tokenize a part of it, then let the last token become a
    // catch-all.
    tokens = trimmed.substring(0, 500).split(lazy.UrlUtils.REGEXP_SPACES);
    tokens[tokens.length - 1] += trimmed.substring(500);
  }

  if (!tokens.length) {
    return tokens;
  }

  // If there is no separate restriction token, it's possible we have to split
  // a token, if it's the first one and it includes a leading restriction char
  // or it's the last one and it includes a trailing restriction char.
  // This allows to not require the user to add artificial whitespaces to
  // enforce restrictions, for example typing questions would restrict to
  // search results.
  const hasRestrictionToken = tokens.some(t => CHAR_TO_TYPE_MAP.has(t));

  const firstToken = tokens[0];
  const isFirstTokenAKeyword =
    !Object.values(UrlbarShared.RESTRICT_TOKENS).includes(
      /** @type {Values<typeof UrlbarShared.RESTRICT_TOKENS>} */ (firstToken)
    ) && lazy.PlacesUtils.keywords.isKeywordFromCache(firstToken);

  if (hasRestrictionToken || isFirstTokenAKeyword) {
    return tokens;
  }

  // Check for an unambiguous restriction char at the beginning of the first
  // token.
  if (
    CHAR_TO_TYPE_MAP.has(firstToken[0]) &&
    !lazy.UrlUtils.REGEXP_PERCENT_ENCODED_START.test(firstToken) &&
    !searchMode
  ) {
    tokens[0] = firstToken.substring(1);
    tokens.splice(0, 0, firstToken[0]);
    return tokens;
  }

  return tokens;
}

/**
 * Given an array of unfiltered tokens, this function filters them and converts
 * to token objects with a type.
 *
 * @param {Array} tokens
 *        An array of strings, representing search tokens.
 * @returns {Array} An array of token objects.
 * Note: restriction characters are only considered if they appear at the start
 *       or at the end of the tokens list. In case of restriction characters
 *       conflict, the most external ones win. Leading ones win over trailing
 *       ones. Discarded restriction characters are considered text.
 */
function filterTokens(tokens) {
  let filtered = [];
  let restrictions = [];
  const isFirstTokenAKeyword =
    !Object.values(UrlbarShared.RESTRICT_TOKENS).includes(tokens[0]) &&
    lazy.PlacesUtils.keywords.isKeywordFromCache(tokens[0]);

  for (let i = 0; i < tokens.length; ++i) {
    let token = tokens[i];
    let tokenObj = {
      value: token,
      lowerCaseValue: token.toLocaleLowerCase(),
      /** @type {Values<typeof UrlbarShared.TOKEN_TYPE>} */
      type: UrlbarShared.TOKEN_TYPE.TEXT,
    };
    // For privacy reasons, we don't want to send a data (or other kind of) URI
    // to a search engine. So we want to parse any single long token below.
    if (tokens.length > 1 && token.length > 500) {
      filtered.push(tokenObj);
      break;
    }

    if (isFirstTokenAKeyword) {
      filtered.push(tokenObj);
      continue;
    }

    let restrictionType = CHAR_TO_TYPE_MAP.get(token);
    if (restrictionType) {
      restrictions.push({ index: i, type: restrictionType });
    } else {
      let looksLikeOrigin = lazy.UrlUtils.looksLikeOrigin(token);
      if (
        looksLikeOrigin == lazy.UrlUtils.LOOKS_LIKE_ORIGIN.OTHER &&
        lazy.UrlbarPrefs.get("allowSearchSuggestionsForSimpleOrigins")
      ) {
        tokenObj.type =
          UrlbarShared.TOKEN_TYPE.POSSIBLE_ORIGIN_BUT_SEARCH_ALLOWED;
      } else if (looksLikeOrigin != lazy.UrlUtils.LOOKS_LIKE_ORIGIN.NONE) {
        tokenObj.type = UrlbarShared.TOKEN_TYPE.POSSIBLE_ORIGIN;
      } else if (lazy.UrlUtils.looksLikeUrl(token, { requirePath: true })) {
        tokenObj.type = UrlbarShared.TOKEN_TYPE.POSSIBLE_URL;
      }
    }
    filtered.push(tokenObj);
  }

  // Handle restriction characters.
  if (restrictions.length) {
    // We can apply two kind of restrictions: type (bookmark, search, ...) and
    // matching (url, title). These kind of restrictions can be combined, but we
    // can only have one restriction per kind.
    let matchingRestrictionFound = false;
    let typeRestrictionFound = false;
    function assignRestriction(r) {
      if (
        r &&
        !(matchingRestrictionFound && typeRestrictionFound) &&
        !typeRestrictionFound
      ) {
        typeRestrictionFound = true;
        filtered[r.index].type = r.type;
        return true;
      }
      return false;
    }

    // Look at the first token.
    let found = assignRestriction(restrictions.find(r => r.index == 0));
    if (found) {
      // If the first token was assigned, look at the next one.
      assignRestriction(restrictions.find(r => r.index == 1));
    }
    // Then look at the last token.
    let lastIndex = tokens.length - 1;
    found = assignRestriction(restrictions.find(r => r.index == lastIndex));
    if (found) {
      // If the last token was assigned, look at the previous one.
      assignRestriction(restrictions.find(r => r.index == lastIndex - 1));
    }
  }

  lazy.logger.info("Filtered Tokens", filtered);
  return filtered;
}
