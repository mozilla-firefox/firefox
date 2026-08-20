/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef, useState } from "react";
import { normalize, MAX_STOCKS_WATCHLIST } from "common/StocksWatchlist.mjs";
import { StockTicker } from "./StockTicker";

const RESULTS_ID = "stocks-search-results";

// The message for each non-success state.
const STATUS_MESSAGE_L10N_ID = {
  loading: "newtab-stocks-search-loading",
  empty: "newtab-stocks-search-no-results",
  error: "newtab-stocks-search-error",
};

/**
 * The ticker search panel: search for a symbol and add a result to the
 * watchlist. The parent owns the search status and results.
 *
 * @param {object} props
 * @param {string} props.searchStatus Current state: "idle", "loading", "success", "empty", or "error".
 * @param {object[]} props.searchResults Tickers returned by a successful search.
 * @param {string[]} props.savedSymbols The user's watchlist, to mark added rows.
 * @param {boolean} props.atWatchlistLimit Whether the watchlist is full.
 * @param {function(string): void} props.onSubmit Called with a non-empty search query.
 * @param {function(string, string): void} props.onAdd Called with a result's ticker symbol and display name.
 * @param {function(): void} props.onClose Called when the user closes the panel.
 */
export function StockSearch({
  searchStatus,
  searchResults,
  savedSymbols,
  atWatchlistLimit,
  onSubmit,
  onAdd,
  onClose,
}) {
  const [value, setValue] = useState("");
  const [submittedQuery, setSubmittedQuery] = useState("");
  const inputRef = useRef(null);

  // moz-input-search creates its inner input asynchronously, so wait for it over
  // a few frames before focusing.
  useEffect(() => {
    let frameId = 0;
    let remainingFrames = 5;
    const focusWhenReady = () => {
      const input = inputRef.current?.inputEl;
      if (input) {
        input.focus();
        return;
      }
      if (remainingFrames > 0) {
        remainingFrames -= 1;
        frameId = requestAnimationFrame(focusWhenReady);
      }
    };
    frameId = requestAnimationFrame(focusWhenReady);
    return () => cancelAnimationFrame(frameId);
  }, []);

  const savedSet = new Set(savedSymbols);
  // When the watchlist is full, explain why results cannot be added rather than
  // showing nothing.
  const statusMessageId =
    atWatchlistLimit && searchStatus === "success"
      ? "newtab-stocks-watchlist-full"
      : STATUS_MESSAGE_L10N_ID[searchStatus];
  // Only the watchlist-full and no-results messages take a variable.
  let statusMessageArgs = null;
  if (statusMessageId === "newtab-stocks-watchlist-full") {
    statusMessageArgs = { limit: MAX_STOCKS_WATCHLIST };
  } else if (statusMessageId === "newtab-stocks-search-no-results") {
    statusMessageArgs = { query: submittedQuery };
  }

  return (
    <div
      className="stocks-search"
      onKeyDown={e => {
        if (e.key === "Escape") {
          onClose();
        }
      }}
    >
      <div className="stocks-search-header">
        <moz-button
          className="stocks-search-back"
          type="icon ghost"
          size="small"
          iconSrc="chrome://global/skin/icons/arrow-left.svg"
          data-l10n-id="newtab-stocks-search-back-button"
          onClick={onClose}
        />
        <form
          className="stocks-search-form"
          onSubmit={e => e.preventDefault()}
          onKeyDown={e => {
            if (e.key === "Enter") {
              e.preventDefault();
              const query = value.trim();
              if (query) {
                setSubmittedQuery(query);
                onSubmit(query);
              }
            }
          }}
        >
          <moz-input-search
            ref={inputRef}
            className="stocks-search-input"
            data-l10n-id="newtab-stocks-search-input"
            aria-controls={RESULTS_ID}
            value={value}
            onInput={e => setValue(e.target.value)}
          />
        </form>
      </div>

      <ul
        id={RESULTS_ID}
        className="stocks-search-results"
        data-l10n-id="newtab-stocks-search-results"
        aria-busy={searchStatus === "loading"}
      >
        {searchStatus === "success" &&
          searchResults.map(t => {
            const saved = savedSet.has(normalize(t.ticker));
            return (
              <StockTicker
                key={t.ticker}
                size="large"
                variant="search"
                name={t.name}
                ticker={t.ticker}
                price={t.last_price}
                changePercent={t.todays_change_perc}
                watchlistState={saved ? "added" : "add"}
                disabled={!saved && atWatchlistLimit}
                onWatchlistToggle={onAdd}
              />
            );
          })}
      </ul>

      <p className="stocks-search-message" role="status" aria-live="polite">
        {statusMessageId && (
          <span
            data-l10n-id={statusMessageId}
            data-l10n-args={
              statusMessageArgs ? JSON.stringify(statusMessageArgs) : undefined
            }
          />
        )}
      </p>
    </div>
  );
}
