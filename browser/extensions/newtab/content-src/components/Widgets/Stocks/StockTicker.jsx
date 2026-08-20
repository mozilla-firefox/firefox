/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef, useState } from "react";

function getDirection(changePercent) {
  const value = parseFloat(
    String(changePercent ?? "").replace(/[^0-9.-]/g, "")
  );
  if (!Number.isFinite(value) || value === 0) {
    return "flat";
  }
  return value > 0 ? "up" : "down";
}

// The feed hands us Merino's preformatted values: last_price like "$559.44 USD"
// and todays_change_perc like "+0.2" (US formatting, no percent sign). Parse the
// number back out and reformat it for the viewer's locale, so the decimal
// separator, currency symbol, and percent sign follow local conventions.
function parseAmount(raw) {
  const value = parseFloat(String(raw ?? "").replace(/[^0-9.-]/g, ""));
  return Number.isFinite(value) ? value : null;
}

// How many decimal places the source value carries, so the formatted output
// keeps the same precision Merino sent (e.g. "0.2" stays 1 place, "0.00" stays 2).
function decimalPlaces(raw) {
  const decimals = String(raw ?? "").match(/\.(\d+)/);
  return decimals ? decimals[1].length : 0;
}

function formatPrice(price, locale) {
  const value = parseAmount(price);
  if (value === null) {
    return String(price ?? "");
  }
  return new Intl.NumberFormat(locale, {
    style: "currency",
    // For now the feed only returns US stocks and ETFs, which are all priced
    // in USD, so the currency is fixed here.
    currency: "USD",
    currencyDisplay: "narrowSymbol",
  }).format(value);
}

function formatChange(change, locale) {
  const value = parseAmount(change);
  if (value === null) {
    return String(change ?? "");
  }
  const places = decimalPlaces(change);
  // Merino sends the value already in percent units ("0.2" means 0.2%), but the
  // percent style expects a ratio, so divide by 100.
  return new Intl.NumberFormat(locale, {
    style: "percent",
    signDisplay: "exceptZero",
    minimumFractionDigits: places,
    maximumFractionDigits: places,
  }).format(value / 100);
}

const TICKER_STATUS_L10N_ID = {
  up: "newtab-stocks-ticker-status-up",
  down: "newtab-stocks-ticker-status-down",
  flat: "newtab-stocks-ticker-status-flat",
};

// A single read-only ticker card. The visible rows are hidden from screen
// readers; the spoken label comes from the localized `.stock-ticker-sr` span
// instead (one of the newtab-stocks-ticker-status-* messages, by direction).
function StockTicker({
  loading,
  size = "medium",
  name: stockName,
  ticker,
  price,
  changePercent,
  watchlistState,
  onWatchlistToggle,
  disabled,
  variant,
}) {
  const direction = getDirection(changePercent);
  const locale =
    typeof navigator !== "undefined" ? navigator.language : undefined;
  const displayPrice = formatPrice(price, locale);
  const displayChange = formatChange(changePercent, locale);
  // Flat is a sideways arrow, so it mirrors for right-to-left locales. The URL
  // is built in JS like the other newtab arrows, not as a CSS url(): a static
  // reference to shaft-arrow-right.svg makes browser_all_files_referenced.js
  // flag the icon.
  const isRTL = typeof document !== "undefined" && document.dir === "rtl";
  const indicatorStyle =
    direction === "flat"
      ? {
          backgroundImage: `url("chrome://global/skin/icons/shaft-arrow-${
            isRTL ? "left" : "right"
          }.svg")`,
        }
      : undefined;
  const changeText = (
    <span className={`stock-ticker-change stock-ticker-change--${direction}`}>
      {displayChange}
    </span>
  );
  // Large-only watchlist control, driven by watchlistState: an add/remove button, or a
  // non-interactive "added" checkmark on Markets rows.
  const isActionable = watchlistState === "add" || watchlistState === "remove";
  const showActionButton =
    size === "large" && isActionable && typeof onWatchlistToggle === "function";
  const showAddedIndicator = size === "large" && watchlistState === "added";
  const actionIcon =
    watchlistState === "remove"
      ? "chrome://global/skin/icons/minus.svg"
      : "chrome://global/skin/icons/plus.svg";

  // Show the checkmark briefly after an add so the user sees the confirmation; afterwards
  // it appears on hover like the other rows.
  const [confirming, setConfirming] = useState(false);
  const confirmTimer = useRef(null);
  useEffect(() => () => clearTimeout(confirmTimer.current), []);
  const handleToggle = () => {
    onWatchlistToggle(ticker, stockName || ticker);
    if (watchlistState === "add") {
      setConfirming(true);
      clearTimeout(confirmTimer.current);
      confirmTimer.current = setTimeout(() => setConfirming(false), 2000);
    }
  };
  return (
    <li
      className={`stock-ticker stock-ticker--${size}${
        variant === "search" ? " stock-ticker--result" : ""
      }`}
      aria-hidden={loading ? "true" : undefined}
    >
      {!loading && (
        <span
          className="stock-ticker-sr"
          data-l10n-id={TICKER_STATUS_L10N_ID[direction]}
          data-l10n-args={JSON.stringify({
            name: stockName || ticker,
            change: displayChange,
            price: displayPrice,
          })}
        />
      )}
      <span
        className={`stock-indicator stock-indicator--${direction}`}
        style={indicatorStyle}
        aria-hidden="true"
      />
      <span className="stock-ticker-label" aria-hidden="true">
        {size === "large" && (
          <>
            <span className="stock-ticker-line">
              <span className="stock-ticker-name">{stockName}</span>
              <span className="stock-ticker-dot" />
              <span className="stock-ticker-symbol">{ticker}</span>
            </span>
            <span className="stock-ticker-line">
              {changeText}
              <span className="stock-ticker-dot" />
              <span className="stock-ticker-price">{displayPrice}</span>
            </span>
          </>
        )}
        {size === "medium" && (
          <>
            <span className="stock-ticker-line">
              <span className="stock-ticker-symbol">{ticker}</span>
              <span className="stock-ticker-dot" />
              <span className="stock-ticker-price">{displayPrice}</span>
            </span>
            {changeText}
          </>
        )}
        {size === "small" && (
          <>
            {changeText}
            <span className="stock-ticker-price">{displayPrice}</span>
          </>
        )}
      </span>
      {showActionButton && (
        <moz-button
          className="stock-ticker-action"
          type="icon"
          size="small"
          iconSrc={actionIcon}
          disabled={disabled || undefined}
          data-l10n-id={
            watchlistState === "remove"
              ? "newtab-stocks-remove-from-watchlist"
              : "newtab-stocks-add-to-watchlist"
          }
          data-l10n-args={JSON.stringify({ name: stockName || ticker })}
          onClick={handleToggle}
        />
      )}
      {showAddedIndicator && (
        <>
          <span
            className="stock-ticker-sr stock-ticker-in-watchlist"
            data-l10n-id="newtab-stocks-in-watchlist"
            data-l10n-args={JSON.stringify({ name: stockName || ticker })}
          />
          <span
            className={`stock-ticker-added${
              confirming ? " stock-ticker-added--confirming" : ""
            }`}
            aria-hidden="true"
          />
        </>
      )}
    </li>
  );
}

export { StockTicker, getDirection, formatPrice, formatChange };
