/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

export const MAX_STOCKS_WATCHLIST = 10;

export function normalize(symbol) {
  return String(symbol ?? "")
    .trim()
    .toUpperCase();
}

/**
 * Parse the widgets.stocks.watchlist pref (a comma-separated list of ticker
 * symbols) into a clean array: trimmed, upper-cased, empties dropped, duplicates
 * removed keeping first occurrence, capped at MAX_STOCKS_WATCHLIST.
 *
 * @param {string} pref
 * @returns {string[]}
 */
export function parseWatchlist(pref) {
  const out = [];
  const seen = new Set();
  for (const raw of String(pref ?? "").split(",")) {
    const symbol = normalize(raw);
    if (!symbol || seen.has(symbol)) {
      continue;
    }
    seen.add(symbol);
    out.push(symbol);
    if (out.length >= MAX_STOCKS_WATCHLIST) {
      break;
    }
  }
  return out;
}

export function serializeWatchlist(symbols) {
  return symbols.join(",");
}

/**
 * Return a new array with the symbol appended, or the same array unchanged when
 * the symbol is empty, already present, or the list is at the cap.
 *
 * @param {string[]} symbols
 * @param {string} symbol
 * @returns {string[]}
 */
export function addToWatchlist(symbols, symbol) {
  const next = normalize(symbol);
  if (
    !next ||
    symbols.includes(next) ||
    symbols.length >= MAX_STOCKS_WATCHLIST
  ) {
    return symbols;
  }
  return [...symbols, next];
}

export function removeFromWatchlist(symbols, symbol) {
  const target = normalize(symbol);
  return symbols.filter(s => s !== target);
}
