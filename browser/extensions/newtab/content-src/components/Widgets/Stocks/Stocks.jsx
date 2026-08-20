/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, {
  useCallback,
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { useSelector, batch } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import {
  WIDGET_REGISTRY,
  resolveWidgetSize,
  PREF_STOCKS_WATCHLIST,
} from "common/WidgetsRegistry.mjs";
import {
  parseWatchlist,
  serializeWatchlist,
  addToWatchlist,
  removeFromWatchlist,
  normalize,
  MAX_STOCKS_WATCHLIST,
} from "common/StocksWatchlist.mjs";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { SizeSubmenu } from "../SizeSubmenu";
import { StockTicker } from "./StockTicker";
import { StockSearch } from "./StockSearch";
import { useStockSearch } from "./useStockSearch";
import { StocksError } from "./StocksError";

const STOCKS_ENTRY = WIDGET_REGISTRY.find(w => w.id === "stocks");
const STOCKS_PLACEHOLDER_COUNT = 4;
// One shared empty array. When the Stocks state lacks these fields (an older
// build that predates them), the fallback below reuses this instead of a new []
// each render, so the derived lists don't recompute needlessly.
const EMPTY_ARRAY = [];

// The two lists the dropdown switches between: "markets" (the default ETFs)
// and "watchlist" (the user's picks). Each id maps to its menu/button label.
const STOCKS_LISTS = [
  { id: "markets", l10nId: "newtab-stocks-list-markets" },
  { id: "watchlist", l10nId: "newtab-stocks-list-watchlist" },
];

function Stocks({
  dispatch,
  handleUserInteraction,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const {
    tickers,
    error,
    lastUpdated,
    watchlistTickers = EMPTY_ARRAY,
    watchlistReconciledSymbols = EMPTY_ARRAY,
    searchStatus = "idle",
    searchResults = EMPTY_ARRAY,
  } = useSelector(state => state.Stocks);
  const watchlistPref = useSelector(
    state => state.Prefs.values[PREF_STOCKS_WATCHLIST]
  );

  // Resolve size through the registry helper, not the pref, so trainhop and the
  // default can apply.
  const widgetSize = resolveWidgetSize(STOCKS_ENTRY, prefs);
  const showError = error && !tickers.length;

  // Show the "New" badge until the user first interacts with the widget;
  // handleInteraction flips widgets.stocks.interaction, which removes it.
  const hasInteracted = prefs["widgets.stocks.interaction"];

  const { impressionRef, recordUserAction, recordError } = useWidgetTelemetry({
    dispatch,
    widget: STOCKS_ENTRY,
    widgetSize,
  });

  // Any user action flips widgets.stocks.interaction (idempotent, one-way),
  // matching the other widgets. Hiding the widget is not an interaction.
  const handleInteraction = useCallback(
    () => handleUserInteraction("stocks"),
    [handleUserInteraction]
  );

  // SET_PREF is OnlyToMain, so the pref only updates after a round-trip. Keep the list
  // locally so the UI updates immediately and rapid adds build on each other, then
  // reconcile when the pref broadcast arrives.
  const [savedSymbols, setSavedSymbols] = useState(() =>
    parseWatchlist(watchlistPref)
  );
  const pendingWriteRef = useRef(null);
  const [announcement, setAnnouncement] = useState(null);
  // Index of the removed Watchlist row whose neighbour should receive focus next.
  const focusRemoveIndexRef = useRef(null);
  // Index of the added Markets row whose next add button should receive focus.
  const focusAddIndexRef = useRef(null);
  const watchlistRef = useRef(null);
  const marketsRef = useRef(null);
  const menuButtonRef = useRef(null);
  const {
    active: searchActive,
    open: openSearch,
    close: closeSearch,
    submit: submitSearch,
  } = useStockSearch({ dispatch, recordUserAction, menuButtonRef });

  useEffect(() => {
    const parsed = parseWatchlist(watchlistPref ?? "");
    const serialized = serializeWatchlist(parsed);
    if (pendingWriteRef.current === null) {
      // Adopt an external change or the first-load value. Compare canonical forms so a
      // non-canonical pref (padded or lower-case) doesn't set state on every render.
      if (serializeWatchlist(savedSymbols) !== serialized) {
        setSavedSymbols(parsed);
      }
    } else if (serialized === pendingWriteRef.current) {
      pendingWriteRef.current = null;
    }
    // A broadcast that is neither our pending write nor an external change is an
    // out-of-date acknowledgement; leave the local list as-is.
  }, [savedSymbols, watchlistPref]);

  const writeWatchlist = useCallback(
    nextSymbols => {
      const serialized = serializeWatchlist(nextSymbols);
      setSavedSymbols(nextSymbols);
      pendingWriteRef.current = serialized;
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PREF_STOCKS_WATCHLIST, value: serialized },
        })
      );
    },
    [dispatch]
  );

  // Merge default and watchlist rows by normalized ticker so a saved symbol
  // resolves from either set. Defaults go last so their fresher data wins a
  // symbol that appears in both.
  const bySymbol = useMemo(
    () =>
      new Map(
        [...watchlistTickers, ...tickers].map(t => [normalize(t.ticker), t])
      ),
    [tickers, watchlistTickers]
  );
  const matchedRows = useMemo(
    () => savedSymbols.map(s => bySymbol.get(s)).filter(Boolean),
    [savedSymbols, bySymbol]
  );
  // The watchlist is ready for the current pref only once the feed has
  // reconciled exactly the saved symbols; before that a newly added symbol is
  // still pending, not missing.
  const watchlistReady = useMemo(() => {
    if (savedSymbols.length !== watchlistReconciledSymbols.length) {
      return false;
    }
    const reconciled = new Set(watchlistReconciledSymbols);
    return savedSymbols.every(s => reconciled.has(s));
  }, [savedSymbols, watchlistReconciledSymbols]);
  // While the watchlist is loading, show a loading row for each saved symbol
  // that hasn't resolved yet, or a few loading rows if none have resolved.
  const watchlistPendingCount = useMemo(() => {
    if (watchlistReady) {
      return 0;
    }
    return matchedRows.length
      ? Math.max(savedSymbols.length - matchedRows.length, 0)
      : Math.min(savedSymbols.length, STOCKS_PLACEHOLDER_COUNT);
  }, [watchlistReady, matchedRows.length, savedSymbols.length]);

  const mediumWatchlistRows = matchedRows.slice(0, STOCKS_PLACEHOLDER_COUNT);
  const mediumWatchlistPendingCount = watchlistReady
    ? 0
    : Math.max(
        Math.min(STOCKS_PLACEHOLDER_COUNT, savedSymbols.length) -
          mediumWatchlistRows.length,
        0
      );

  const handleToggleWatchlist = useCallback(
    (symbol, tickerName) => {
      const normalized = normalize(symbol);
      const isSaved = savedSymbols.includes(normalized);
      const next = isSaved
        ? removeFromWatchlist(savedSymbols, normalized)
        : addToWatchlist(savedSymbols, normalized);
      if (next === savedSymbols) {
        return;
      }
      if (isSaved) {
        focusRemoveIndexRef.current = matchedRows.findIndex(
          t => normalize(t.ticker) === normalized
        );
      } else {
        // The added row flips to "added" and its add button unmounts, so record its
        // position among the addable rows to move focus to the next add button.
        focusAddIndexRef.current = tickers
          .filter(t => !savedSymbols.includes(normalize(t.ticker)))
          .findIndex(t => normalize(t.ticker) === normalized);
      }
      writeWatchlist(next);
      recordUserAction(isSaved ? "remove_ticker" : "add_ticker", {
        source: "row",
      });
      setAnnouncement({
        id: isSaved
          ? "newtab-stocks-removed-from-watchlist"
          : "newtab-stocks-added-to-watchlist",
        args: { name: tickerName },
      });
      handleInteraction();
    },
    [
      savedSymbols,
      matchedRows,
      tickers,
      writeWatchlist,
      recordUserAction,
      handleInteraction,
    ]
  );

  // Switching lists counts as an interaction; reselecting the current list does not.
  const [selectedList, setSelectedList] = useState(() =>
    savedSymbols.length ? "watchlist" : "markets"
  );
  const handleSelectList = useCallback(
    list => {
      if (list === selectedList) {
        return;
      }
      setSelectedList(list);
      recordUserAction("change_list", { source: "widget", value: list });
      handleInteraction();
    },
    [selectedList, recordUserAction, handleInteraction]
  );
  const selectedListL10nId = STOCKS_LISTS.find(
    l => l.id === selectedList
  ).l10nId;
  // How many saved symbols the dropdown counts: all of them while the watchlist
  // is loading, then only the ones that resolved once it has loaded.
  const savedInFeed = watchlistReady ? matchedRows.length : savedSymbols.length;
  const atWatchlistLimit = savedSymbols.length >= MAX_STOCKS_WATCHLIST;
  const showDropdown = widgetSize !== "small" && savedInFeed > 0;
  const activeList =
    showDropdown && selectedList === "watchlist" ? "watchlist" : "markets";

  // Small size shows a single ticker.
  const chosenSymbol = savedSymbols.length
    ? savedSymbols[0]
    : (tickers[0]?.ticker ?? null);
  const chosenRow = chosenSymbol ? bySymbol.get(normalize(chosenSymbol)) : null;
  // Keep the saved symbol visible while data is loading or unavailable.
  const headerSymbol = chosenRow?.ticker ?? chosenSymbol;
  const hasLoadedSnapshot = lastUpdated !== null;
  // Small size shows one ticker. If it is a saved symbol, treat it as an error
  // only after the watchlist finished loading with no data for it (before that it
  // is still loading); if it is a default ticker, use the default feed's check.
  const chosenIsSaved = !!savedSymbols.length;
  const smallError =
    widgetSize === "small" &&
    (chosenIsSaved
      ? watchlistReady && !chosenRow
      : (!tickers.length && hasLoadedSnapshot) ||
        (!!chosenSymbol && !!tickers.length && !chosenRow));
  // Suppress the default error only when a resolved watchlist row is on screen,
  // so the watchlist still shows even if the default feed failed.
  const watchlistRowShown =
    (widgetSize === "small" && chosenIsSaved && !!chosenRow) ||
    (widgetSize !== "small" &&
      activeList === "watchlist" &&
      !!matchedRows.length);
  // Show the error box from one place so switching error states doesn't report it
  // to telemetry twice.
  const showAnyError = (showError && !watchlistRowShown) || smallError;

  // Return to Markets when there's nothing to show (the watchlist emptied, or
  // none of its symbols resolved) so the next add starts there with the
  // confirmation animation. A size change with a non-empty watchlist keeps the
  // selection.
  useEffect(() => {
    if (!savedInFeed && selectedList !== "markets") {
      setSelectedList("markets");
    }
  }, [savedInFeed, selectedList]);

  // After a removal re-renders the Watchlist, move focus to a neighbouring remove button,
  // or to the widget menu button if the list collapsed back to Markets.
  useLayoutEffect(() => {
    const index = focusRemoveIndexRef.current;
    if (index === null) {
      return;
    }
    focusRemoveIndexRef.current = null;
    const buttons =
      watchlistRef.current?.querySelectorAll(".stock-ticker-action") ?? [];
    const target =
      index >= 0 && buttons.length
        ? buttons[Math.min(index, buttons.length - 1)]
        : null;
    if (target) {
      target.focus();
    } else {
      menuButtonRef.current?.focus();
    }
  }, [matchedRows]);

  // After an add re-renders Markets, move focus to the next add button, or to the widget
  // menu button if none remain (e.g., the watchlist just reached its limit).
  useLayoutEffect(() => {
    const index = focusAddIndexRef.current;
    if (index === null) {
      return;
    }
    focusAddIndexRef.current = null;
    const buttons =
      marketsRef.current?.querySelectorAll(
        ".stock-ticker-action:not([disabled])"
      ) ?? [];
    const target =
      index >= 0 && buttons.length
        ? buttons[Math.min(index, buttons.length - 1)]
        : null;
    if (target) {
      target.focus();
    } else {
      menuButtonRef.current?.focus();
    }
  }, [savedSymbols]);

  const handleChangeSize = useCallback(
    size => {
      batch(() => {
        dispatch(
          ac.OnlyToMain({
            type: at.SET_PREF,
            data: { name: STOCKS_ENTRY.sizePref, value: size },
          })
        );
        recordUserAction("change_size", {
          source: "context_menu",
          value: size,
          size,
        });
        handleInteraction();
      });
    },
    [dispatch, recordUserAction, handleInteraction]
  );

  function handleSearchTickers() {
    recordUserAction("search_tickers", { source: "context_menu" });
    openSearch();
    handleInteraction();
  }

  const handleAddFromSearch = useCallback(
    (symbol, tickerName) => {
      const next = addToWatchlist(savedSymbols, symbol);
      if (next !== savedSymbols) {
        writeWatchlist(next);
        recordUserAction("add_ticker", { source: "search" });
        setAnnouncement({
          id: "newtab-stocks-added-to-watchlist",
          args: { name: tickerName },
        });
        // Show the Watchlist so the added ticker appears in the list. Closing
        // search drops the widget back to its original size.
        setSelectedList("watchlist");
        handleInteraction();
      }
      closeSearch();
    },
    [
      savedSymbols,
      writeWatchlist,
      recordUserAction,
      handleInteraction,
      closeSearch,
    ]
  );

  // The shared footer opens the support link; here we only record the click.
  function handleLearnMore() {
    recordUserAction("learn_more", { source: "context_menu" });
    handleInteraction();
  }

  function renderWatchlist() {
    if (widgetSize === "medium") {
      return (
        <ul
          aria-busy={!watchlistReady}
          className={`stocks-grid${
            !mediumWatchlistRows.length && !watchlistReady
              ? " stocks-grid--loading"
              : ""
          }`}
        >
          {mediumWatchlistRows.map(t => (
            <StockTicker
              key={t.ticker}
              name={t.name}
              ticker={t.ticker}
              price={t.last_price}
              changePercent={t.todays_change_perc}
            />
          ))}
          {Array.from({ length: mediumWatchlistPendingCount }).map((_, i) => (
            <StockTicker key={`pending-${i}`} loading={true} />
          ))}
        </ul>
      );
    }
    return (
      <ul
        ref={watchlistRef}
        aria-busy={!watchlistReady}
        className={`stocks-list stocks-list--watchlist${
          !matchedRows.length && !watchlistReady ? " stocks-list--loading" : ""
        }`}
      >
        {matchedRows.map(t => (
          <StockTicker
            key={t.ticker}
            size="large"
            name={t.name}
            ticker={t.ticker}
            price={t.last_price}
            changePercent={t.todays_change_perc}
            watchlistState="remove"
            onWatchlistToggle={handleToggleWatchlist}
          />
        ))}
        {Array.from({ length: watchlistPendingCount }).map((_, i) => (
          <StockTicker key={`pending-${i}`} size="large" loading={true} />
        ))}
      </ul>
    );
  }

  return (
    <article
      className={`stocks widget col-4 ${searchActive ? "large" : widgetSize}-widget`}
      ref={impressionRef}
      aria-labelledby="stocks-widget-label"
    >
      {searchActive ? (
        <>
          {/* Keep the region name while the search panel replaces the body. */}
          <h2
            id="stocks-widget-label"
            className="stocks-heading sr-only"
            data-l10n-id="newtab-stocks-widget-title"
          />
          <StockSearch
            searchStatus={searchStatus}
            searchResults={searchResults}
            savedSymbols={savedSymbols}
            atWatchlistLimit={atWatchlistLimit}
            onSubmit={submitSearch}
            onAdd={handleAddFromSearch}
            onClose={closeSearch}
          />
        </>
      ) : (
        <>
          <div className="stocks-title-wrapper">
            <div className="stocks-badge-title-wrapper">
              {widgetSize !== "small" && !hasInteracted && !!tickers.length && (
                <moz-badge
                  className="stocks-new-badge"
                  data-l10n-id="newtab-widget-lists-label-new"
                ></moz-badge>
              )}
              {/* Keep the heading mounted so aria-labelledby always resolves. */}
              <h2
                id="stocks-widget-label"
                className={`stocks-heading${
                  showDropdown || (widgetSize === "small" && chosenSymbol)
                    ? " sr-only"
                    : ""
                }`}
                data-l10n-id="newtab-stocks-widget-title"
              />
              {widgetSize === "small" && chosenSymbol && (
                <span className="stocks-small-symbol">{headerSymbol}</span>
              )}
              {showDropdown && (
                <>
                  <moz-button
                    className="stocks-list-button"
                    type="ghost"
                    size="small"
                    iconSrc="chrome://global/skin/icons/arrow-down-12.svg"
                    iconPosition="end"
                    menuId="stocks-list-menu"
                    data-l10n-id={selectedListL10nId}
                  ></moz-button>
                  <panel-list id="stocks-list-menu">
                    {STOCKS_LISTS.map(({ id, l10nId }) => (
                      <panel-item
                        key={id}
                        type="checkbox"
                        checked={selectedList === id || undefined}
                        onClick={() => handleSelectList(id)}
                        data-list={id}
                        data-l10n-id={l10nId}
                      />
                    ))}
                  </panel-list>
                </>
              )}
            </div>
            <div className="stocks-context-menu-wrapper">
              <moz-button
                ref={menuButtonRef}
                className="stocks-context-menu-button"
                iconSrc="chrome://global/skin/icons/more.svg"
                menuId="stocks-context-menu"
                type="icon ghost"
                size="small"
                data-l10n-id="newtab-stocks-widget-menu-button"
              />
              <panel-list id="stocks-context-menu">
                <panel-item
                  data-l10n-id="newtab-stocks-menu-search-stocks"
                  onClick={handleSearchTickers}
                />
                <WidgetMenuFooter
                  dispatch={dispatch}
                  widgetId="stocks"
                  widgetEnabledMap={widgetEnabledMap}
                  widgetName="stocks"
                  enabledPref={STOCKS_ENTRY.enabledPref}
                  widgetSize={widgetSize}
                  learnMoreL10nId="newtab-stocks-menu-learn-more"
                  onLearnMore={handleLearnMore}
                  sizeSubmenu={
                    widgetsMayBeMaximized ? (
                      <SizeSubmenu
                        submenuId="stocks-size-submenu"
                        sizes={STOCKS_ENTRY.validSizes}
                        checkedSize={widgetSize}
                        onChangeSize={handleChangeSize}
                      />
                    ) : null
                  }
                />
              </panel-list>
            </div>
          </div>

          <div className="stocks-body">
            {showAnyError && <StocksError recordError={recordError} />}
            {!showAnyError && widgetSize === "small" && (
              <ul
                className={`stocks-list stocks-list--small${
                  chosenRow ? "" : " stocks-list--loading"
                }`}
              >
                {chosenRow ? (
                  <StockTicker
                    size="small"
                    name={chosenRow.name}
                    ticker={chosenRow.ticker}
                    price={chosenRow.last_price}
                    changePercent={chosenRow.todays_change_perc}
                  />
                ) : (
                  <StockTicker size="small" loading={true} />
                )}
              </ul>
            )}
            {!showAnyError && widgetSize !== "small" && (
              <>
                {activeList === "markets" && (
                  <>
                    {widgetSize === "medium" && (
                      <ul
                        className={`stocks-grid${tickers.length ? "" : " stocks-grid--loading"}`}
                      >
                        {tickers.length
                          ? tickers.map(t => (
                              <StockTicker
                                key={t.ticker}
                                name={t.name}
                                ticker={t.ticker}
                                price={t.last_price}
                                changePercent={t.todays_change_perc}
                              />
                            ))
                          : Array.from({
                              length: STOCKS_PLACEHOLDER_COUNT,
                            }).map((_, i) => (
                              <StockTicker key={i} loading={true} />
                            ))}
                      </ul>
                    )}
                    {widgetSize === "large" && (
                      <ul
                        ref={marketsRef}
                        className={`stocks-list${tickers.length ? "" : " stocks-list--loading"}`}
                      >
                        {tickers.length
                          ? tickers.map(t => (
                              <StockTicker
                                key={t.ticker}
                                size="large"
                                name={t.name}
                                ticker={t.ticker}
                                price={t.last_price}
                                changePercent={t.todays_change_perc}
                                watchlistState={
                                  savedSymbols.includes(normalize(t.ticker))
                                    ? "added"
                                    : "add"
                                }
                                disabled={atWatchlistLimit}
                                onWatchlistToggle={handleToggleWatchlist}
                              />
                            ))
                          : Array.from({
                              length: STOCKS_PLACEHOLDER_COUNT,
                            }).map((_, i) => (
                              <StockTicker
                                key={i}
                                size="large"
                                loading={true}
                              />
                            ))}
                      </ul>
                    )}
                  </>
                )}
                {activeList === "watchlist" && renderWatchlist()}
              </>
            )}
          </div>
        </>
      )}
      <span
        className="stocks-sr-status sr-only"
        role="status"
        data-l10n-id={announcement?.id}
        data-l10n-args={
          announcement ? JSON.stringify(announcement.args) : undefined
        }
      />
    </article>
  );
}

export { Stocks };
