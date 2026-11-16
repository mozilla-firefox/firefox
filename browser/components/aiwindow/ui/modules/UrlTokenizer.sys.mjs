/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Matches a URL token in model text, e.g. "§url_token: GITHUB_COM_1§". The
 * first capture group is the bare token.
 */
export const URL_TOKEN_REGEX = /§url_token:\s*([A-Z0-9_]+_\d+)§/g;

const EXACT_URL_TOKEN_REGEX = new RegExp(`^${URL_TOKEN_REGEX.source}$`);

/**
 * Matches the candidate URLs tokenizeText replaces. A match starts at any
 * http:// or https:// and runs until whitespace or a delimiter. There is no
 * \b, so "xhttps://..." is also caught. The lookahead ends the match just
 * before the next scheme, so a bad URL can't swallow a valid one that follows
 * it.
 */
export const URL_REGEX = /https?:\/\/(?:(?!https?:\/\/)[^\s<>"'`§])*/gi;

/**
 * Tokenizes URLs to send to an LLM
 */
export class UrlTokenizer {
  /**
   * A mapping of a URL to its unique URL token. URL tokens are used as shortened
   * versions of URLs to help the model deal with very long URLs. Very long URLs are
   * problematic since they are hard for a model to repeat back without making mistakes
   * or hallucinating details about the URL. There is also additional cost for every
   * additional token in the context. Long URLs can also contain prompt injections since
   * they can be of an arbitrary size. URL Tokens help solve all of these issues.
   *
   * URL tokens are only generated while a message is "in flight" to and from the language
   * model. When tool calls are handled, messages rendered, and messages stored they are
   * all done with the URL tokens expanded into full URLs.
   *
   * There are no guarantees that a URL in this list isn't just hallucinated by the model.
   * Any URL the language model invents can be present in this list. The only guarantee
   * is that a token maps to some kind of arbitrary URL.
   *
   * Example mapping:
   * https://github.com/mozilla/ -> GITHUB_COM_MOZILLA_1
   *
   * @type {Map<string, string>}
   */
  urlToToken;

  /**
   * The reverse mapping for a token back to its original URL.
   *
   * e.g. GITHUB_COM_MOZILLA_1 -> https://github.com/mozilla/
   *
   * @type {Map<string, string>}
   */
  tokenToUrl;

  /**
   * A mapping of the base URL token to how many counts there are for it. It's
   * used to generate the final number on URL tokens.
   *
   * e.g.
   *
   * https://github.com/mozilla/                  -> GITHUB_COM_MOZILLA_1
   * https://github.com/mozilla#not-part-of-token -> GITHUB_COM_MOZILLA_2
   *
   * @type {Map<string, number>}
   */
  #baseTokenCounts = new Map();

  constructor() {
    this.urlToToken = new Map();
    this.tokenToUrl = new Map();
  }

  /**
   * Converts a URL into a token. It first computes a base token from
   * the hostname and path parts, then appends a monotonically increasing number on the
   * end to make it unique.
   *
   * URL token analysis:
   * https://docs.google.com/document/d/1kwf2PH1APyUR4wrvv6lJIhA12bkQoNVV5KFwAPtWubw/edit?tab=t.0#heading=h.yhx5pggnwgne
   *
   * @param {string} url - The full URL to register
   *
   * @returns {string} The short token for the URL (e.g. "GITHUB_COM_1")
   */
  encodeToken(url) {
    const seenToken = this.urlToToken.get(url);
    if (seenToken) {
      return seenToken;
    }

    let baseToken = "";

    // Attempt to convert the URL into a base token.
    const parsedUrl = URL.parse(url);
    if (parsedUrl) {
      if (parsedUrl.protocol !== "http:" && parsedUrl.protocol !== "https:") {
        // Go ahead and handle URL tokens for more complicated URLs that
        // aren't probably supported in the chat interface, but would be useful
        // to disambiguate from the HTTP(s) varieties.

        if (
          parsedUrl.protocol == "about:" &&
          parsedUrl.pathname == "smartpage"
        ) {
          // Give smart pages better looking URL to better hint the LLM
          baseToken = "SAVED_PAGE";
        } else {
          baseToken +=
            // e.g. "ftp:" -> "FTP"
            parsedUrl.protocol.toUpperCase().replace(":", "");
        }
      }

      // Convert the hostname into a token.
      const hostToken = parsedUrl.hostname
        .replace(/^www\./, "")
        .toUpperCase()
        .replace(/[.\-]/g, "_")
        .substring(0, 100);

      if (hostToken) {
        baseToken = baseToken ? `${baseToken}_${hostToken}` : hostToken;
      }

      // Add on the parts of the URL to the token.
      for (let part of parsedUrl.pathname.split("/")) {
        if (!part) {
          continue;
        }
        const partToken = part.toUpperCase().replace(/[^A-Z0-9]/g, "_");

        const nextToken = `${baseToken}_${partToken}`;
        if (nextToken.length > 100) {
          break;
        }
        baseToken = nextToken;
      }
    } else {
      baseToken = "INVALID_URL";
    }

    let count = this.#baseTokenCounts.get(baseToken) ?? 0;
    count += 1;
    this.#baseTokenCounts.set(baseToken, count);

    const tokenFinal = `${baseToken}_${count}`;

    this.urlToToken.set(url, tokenFinal);
    this.tokenToUrl.set(tokenFinal, url);

    return tokenFinal;
  }

  /**
   * Encodes a URL and wraps it in the delimiters the model is prompted with.
   *
   * @param {string} url
   * @returns {string} e.g. "§url_token: GITHUB_COM_1§"
   */
  formatToken(url) {
    return `§url_token: ${this.encodeToken(url)}§`;
  }

  /**
   * Replaces every http(s) URL in the text with a token from formatToken, in
   * one pass. Any "http://" or "https://" that doesn't form a valid URL has its
   * scheme removed, so the output never contains a raw URL.
   *
   * @param {string} text
   * @returns {string}
   */
  tokenizeText(text) {
    if (!text) {
      return text;
    }
    return text.replace(URL_REGEX, match => {
      // Keep trailing sentence punctuation like "." or ")" and markdown
      // emphasis like "*" outside the token, except a ")" that closes a "("
      // in the URL, as in "Function_(math)". Markdown-escaped "\(" and "\)"
      // always belong to the URL.
      let url = match.replace(/(?:(?<!\\)[.,;:!?)\]}*])+$/, "");
      let unclosed = countUnescaped(url, "(") - countUnescaped(url, ")");
      while (unclosed-- > 0 && match[url.length] == ")") {
        url += ")";
      }
      const trailing = match.slice(url.length);
      url = url.replace(/\\([()])/g, "$1");
      // If it doesn't parse, drop the scheme and keep the rest as plain text.
      return URL.parse(url)
        ? this.formatToken(url) + trailing
        : match.slice(match.indexOf("//") + 2);
    });
  }

  /**
   * Resolves a value that must consist of exactly one known URL token, such
   * as a link target in structured model output. Anything else, including a
   * raw URL written by the model or a hallucinated token, yields null.
   *
   * @param {string} value
   * @returns {string|null}
   */
  resolveExactToken(value) {
    if (typeof value != "string") {
      return null;
    }
    const match = value.trim().match(EXACT_URL_TOKEN_REGEX);
    return (match && this.tokenToUrl.get(match[1])) || null;
  }
}

/**
 * Counts the occurrences of `char` in `text` not escaped with a backslash.
 *
 * @param {string} text
 * @param {string} char
 * @returns {number}
 */
function countUnescaped(text, char) {
  let count = 0;
  for (let i = 0; i < text.length; i++) {
    if (text[i] == "\\") {
      i++;
    } else if (text[i] == char) {
      count++;
    }
  }
  return count;
}

/**
 * Whether every ")" in `url` closes an earlier "(" and every "(" is closed.
 *
 * @param {string} url
 * @returns {boolean}
 */
function hasBalancedParens(url) {
  let depth = 0;
  for (const char of url) {
    if (char == "(") {
      depth++;
    } else if (char == ")" && --depth < 0) {
      return false;
    }
  }
  return depth == 0;
}

/**
 * Expands URL tokens (e.g. §url_token: GITHUB_COM_1§) in text using the provided
 * mapping. Any token not found in the mapping is left unchanged. In a Markdown
 * link destination, a URL with unbalanced parens has them escaped so the link
 * isn't cut short.
 *
 * @param {string} text
 * @param {Map<string, string>} tokenToUrl - A tokenizer's `tokenToUrl` map.
 * @returns {string}
 */
export function expandUrlTokens(text, tokenToUrl) {
  return text.replace(URL_TOKEN_REGEX, (match, token, offset) => {
    const url = tokenToUrl.get(token);
    if (url === undefined) {
      return match;
    }
    return text.slice(0, offset).endsWith("](") && !hasBalancedParens(url)
      ? url.replace(/[()]/g, "\\$&")
      : url;
  });
}

/**
 * Strips URL tokens that remain after expansion.
 * Any remaining tokens at this point were hallucinated by the model.
 *
 * @param {string} text
 * @returns {string}
 */
export function stripUnresolvedUrlTokens(text) {
  return text.replace(URL_TOKEN_REGEX, "");
}
