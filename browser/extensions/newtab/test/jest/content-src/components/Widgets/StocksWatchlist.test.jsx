/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  MAX_STOCKS_WATCHLIST,
  parseWatchlist,
  serializeWatchlist,
  addToWatchlist,
  removeFromWatchlist,
} from "common/StocksWatchlist.mjs";

describe("StocksWatchlist helpers", () => {
  it("parses, trimming/upper-casing/deduping and dropping empties", () => {
    expect(parseWatchlist(" voo , qqq ,,VOO")).toEqual(["VOO", "QQQ"]);
    expect(parseWatchlist("")).toEqual([]);
    expect(parseWatchlist(undefined)).toEqual([]);
  });

  it("caps the watchlist at 10", () => {
    expect(MAX_STOCKS_WATCHLIST).toBe(10);
  });

  it("caps parsing at MAX_STOCKS_WATCHLIST", () => {
    const many = Array.from(
      { length: MAX_STOCKS_WATCHLIST + 5 },
      (_, i) => `S${i}`
    ).join(",");
    expect(parseWatchlist(many)).toHaveLength(MAX_STOCKS_WATCHLIST);
  });

  it("adds without duplicates; no-op returns the same array; no-op at the cap", () => {
    expect(addToWatchlist(["VOO"], "qqq")).toEqual(["VOO", "QQQ"]);
    const same = ["VOO"];
    expect(addToWatchlist(same, "VOO")).toBe(same);
    const full = Array.from(
      { length: MAX_STOCKS_WATCHLIST },
      (_, i) => `S${i}`
    );
    expect(addToWatchlist(full, "NEW")).toBe(full);
  });

  it("removes case-insensitively and serializes", () => {
    expect(removeFromWatchlist(["VOO", "QQQ"], "voo")).toEqual(["QQQ"]);
    expect(serializeWatchlist(["VOO", "QQQ"])).toBe("VOO,QQQ");
  });
});
