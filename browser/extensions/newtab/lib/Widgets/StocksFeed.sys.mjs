/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  MerinoClient: "moz-src:///browser/components/urlbar/MerinoClient.sys.mjs",
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
});

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import {
  normalize,
  parseWatchlist,
} from "resource://newtab/common/StocksWatchlist.mjs";
import { PREF_STOCKS_WATCHLIST } from "resource://newtab/common/WidgetsRegistry.mjs";

const CACHE_KEY = "stocks_feed";
const MERINO_CLIENT_KEY = "HNT_STOCKS_FEED";
const MERINO_SEARCH_CLIENT_KEY = "HNT_STOCKS_SEARCH";
const MERINO_PROVIDER = ["polygon"];
const STOCKS_UPDATE_TIME = 15 * 60 * 1000; // 15 minutes
const MERINO_TIMEOUT_MS = 5000;
const RETRY_DELAY_MS = 60 * 1000; // 1 minute
const MAX_RETRIES = 1;

const PREF_WIDGETS_STOCKS_ENABLED = "widgets.stocks.enabled";
const PREF_WIDGETS_SYSTEM_STOCKS_ENABLED = "widgets.system.stocks.enabled";

/**
 * Periodically fetches the default-ETF stock snapshots from Merino for HNT and
 * mirrors them into the `Stocks` Redux slice.
 */
export class StocksFeed {
  constructor() {
    this.loaded = false;
    this.merino = null;
    this.tickers = [];
    this.lastUpdated = null;
    this.fetchTimer = null;
    this.fetchIntervalMs = STOCKS_UPDATE_TIME;
    this.fetchGeneration = 0;
    this.cache = this.PersistentCache(CACHE_KEY, true);
    this.error = false;
    this.retryTimer = null;
    // Watchlist data (saved symbols that may not be in the default snapshot).
    this.watchlistTickers = [];
    this.watchlistSymbols = []; // saved set the last completed reconcile resolved
    this.watchlistDesiredSaved = []; // saved set the newest request wants
    this.watchlistGeneration = 0;
    this.watchlistRequestedVersion = 0;
    this.watchlistProcessedVersion = 0;
    this.pendingFullRefresh = false;
    this.watchlistWorker = null;
    this.watchlistLastFullRefresh = null;
    this.cacheWriteChain = null;
    this.watchlistExpiring = false;
  }

  isEnabled() {
    const { values } = this.store.getState().Prefs;
    const userValue = values[PREF_WIDGETS_STOCKS_ENABLED];
    const systemValue = values[PREF_WIDGETS_SYSTEM_STOCKS_ENABLED];
    const experimentValue =
      values.trainhopConfig?.widgets?.stocksEnabled || false;
    return userValue && (systemValue || experimentValue);
  }

  async init() {
    const generation = this.fetchGeneration;
    await this.loadStocks();
    if (generation !== this.fetchGeneration || !this.isEnabled()) {
      return; // turned off during init
    }
    await this.loadWatchlist();
  }

  stopFetching() {
    // Change the generation number so that a fetch() still running when we stop
    // will notice it is out of date and skip updating state or starting a new
    // timer.
    this.fetchGeneration++;
    // Always clear the refresh timer. loadStocks() can start it when it finds
    // cached data, without ever creating a Merino client, so only clearing it
    // when a client exists would leave the timer running and keep fetching after
    // the widget is turned off. Also reset lastUpdated so turning the widget back
    // on sends a fresh update.
    this.clearTimeout(this.fetchTimer);
    this.fetchTimer = null;
    this.merino = null;
    this.tickers = [];
    this.lastUpdated = null;
    this.clearTimeout(this.retryTimer);
    this.retryTimer = null;
    this.error = false;
    // Cancel the running watchlist lookup and clear its data.
    this.watchlistGeneration++;
    this.watchlistTickers = [];
    this.watchlistSymbols = [];
    this.watchlistLastFullRefresh = null;
    this.pendingFullRefresh = false;
  }

  restartFetchTimer(ms = this.fetchIntervalMs) {
    this.clearTimeout(this.fetchTimer);
    this.fetchTimer = this.setTimeout(() => {
      this.fetch();
    }, ms);
  }

  async loadStocks() {
    const generation = this.fetchGeneration;
    const cached = (await this.cache.get()) || {};
    if (generation !== this.fetchGeneration) {
      return; // turned off while reading the cache
    }
    const { stocks } = cached;
    if (
      !stocks?.lastUpdated ||
      this.Date().now() - stocks.lastUpdated >= STOCKS_UPDATE_TIME
    ) {
      await this.fetch();
    } else if (!this.lastUpdated) {
      this.tickers = stocks.tickers || [];
      this.lastUpdated = stocks.lastUpdated;
      this.update();
      const age = this.Date().now() - stocks.lastUpdated;
      this.restartFetchTimer(Math.max(0, STOCKS_UPDATE_TIME - age));
    }
    if (generation !== this.fetchGeneration) {
      return; // turned off during the fetch/hydrate above
    }
    this.loaded = true;
  }

  // Create the Merino client once and reuse it.
  ensureMerinoClient() {
    if (!this.merino) {
      const generation = this.fetchGeneration;
      const client = this.MerinoClient(MERINO_CLIENT_KEY);
      if (generation !== this.fetchGeneration) {
        return null;
      }
      this.merino = client;
    }
    return this.merino;
  }

  // Read lastFetchStatus because fetch() returns [] for both errors and no
  // matches, so the values alone cannot tell the two apart.
  async _searchFetch(client, query) {
    const result = await client.fetch({
      query,
      providers: MERINO_PROVIDER,
      timeoutMs: MERINO_TIMEOUT_MS,
      otherParams: { source: "newtab" },
    });
    const status = client.lastFetchStatus;
    if (
      status === "timeout" ||
      status === "network_error" ||
      status === "http_error"
    ) {
      return null;
    }
    const values = result?.[0]?.custom_details?.polygon?.values;
    return Array.isArray(values) && values.length ? values : [];
  }

  // Look up a search query and reply only to the tab that asked. Each search gets
  // its own Merino client so overlapping searches don't cancel each other (one
  // client serves one request at a time).
  async search(query, requestId, target) {
    if (!target) {
      return; // no port to reply to
    }
    const reply = (status, values = []) =>
      this.store.dispatch(
        ac.OnlyToOneContent(
          {
            type: at.WIDGETS_STOCKS_SEARCH_RESPONSE,
            data: { query, requestId, status, values },
          },
          target
        )
      );
    try {
      if (!this.isEnabled() || typeof query !== "string") {
        reply("error");
        return;
      }
      const normalized = query.trim().replace(/^\$+/, "").trim();
      if (!normalized) {
        reply("empty");
        return;
      }
      const client = this.MerinoClient(MERINO_SEARCH_CLIENT_KEY);
      try {
        const bare = await this._searchFetch(client, normalized);
        if (bare === null) {
          reply("error");
        } else if (bare.length) {
          reply("success", bare);
        } else {
          // A company name (e.g. amazon) or a symbol Merino rejects bare (e.g.
          // SPY) resolves through the "<query> stock" phrase Merino maps to a
          // ticker. Lower-cased because Merino matches those phrases in lower case.
          const named = await this._searchFetch(
            client,
            `${normalized.toLowerCase()} stock`
          );
          if (named === null) {
            reply("error");
          } else if (named.length) {
            reply("success", named);
          } else {
            reply("empty");
          }
        }
      } finally {
        // End the client's session so this per-search client is released
        // instead of lingering until the session timer fires.
        client.resetSession();
      }
    } catch (e) {
      console.error("StocksFeed search failed", e);
      reply("error");
    }
  }

  // `query` defaults to "" (the default ETF set). A non-empty query (e.g.
  // "$AAPL") performs an individual lookup through the same path; unused for now.
  async fetch(query = "", retryCount = 0) {
    const generation = this.fetchGeneration;
    try {
      if (!this.ensureMerinoClient()) {
        return; // the widget was turned off during client creation
      }
      this.restartFetchTimer();
      const tickers = await this._fetchHelper(query);
      if (generation !== this.fetchGeneration) {
        return; // the widget was turned off during the fetch
      }
      if (tickers.length) {
        this.tickers = tickers;
        this.lastUpdated = this.Date().now();
        this.error = false;
        // A success cancels any retry still pending from an earlier failure.
        this.clearTimeout(this.retryTimer);
        this.retryTimer = null;
        await this._cacheSet("stocks", {
          tickers: this.tickers,
          lastUpdated: this.lastUpdated,
        });
      } else {
        this.error = true;
      }
      if (generation !== this.fetchGeneration) {
        return; // the widget was turned off during the cache write
      }
      // Show the result immediately so a failed request surfaces the error
      // state instead of leaving placeholders on screen.
      this.update();
      // Retry a failure once in the background; if it succeeds the tickers
      // replace the error state on their own.
      if (!tickers.length && retryCount < MAX_RETRIES) {
        this.clearTimeout(this.retryTimer);
        this.retryTimer = this.setTimeout(() => {
          this.retryTimer = null;
          // Skip a retry scheduled before the widget was turned off.
          if (generation !== this.fetchGeneration) {
            return undefined;
          }
          return this.fetch(query, retryCount + 1);
        }, RETRY_DELAY_MS);
      }
    } catch (e) {
      console.error("StocksFeed fetch failed", e);
    }
  }

  /**
   * Thin wrapper around the Merino call so tests can simulate responses.
   * Returns the ticker values, or an empty array when the request fails or
   * comes back empty; fetch() handles the retry and the error state.
   */
  async _fetchHelper(query = "") {
    try {
      if (!this.merino) {
        return [];
      }
      const result = await this.merino.fetch({
        query,
        providers: MERINO_PROVIDER,
        timeoutMs: MERINO_TIMEOUT_MS,
        otherParams: { source: "newtab" },
      });
      const values = result?.[0]?.custom_details?.polygon?.values;
      return Array.isArray(values) && values.length ? values : [];
    } catch (e) {
      console.error("StocksFeed Merino request failed", e);
      return [];
    }
  }

  // Look up one saved watchlist symbol. The "$" form resolves symbols that
  // Merino's eager-match blocklist rejects as a bare query (e.g. SPY); the bare
  // form resolves dotted symbols (e.g. BRK.B) that the "$" grammar rejects.
  // Returns the row whose ticker matches, or null when nothing resolves.
  async _fetchWatchlistSymbol(symbol, generation = this.watchlistGeneration) {
    for (const query of [`$${symbol}`, symbol]) {
      if (generation !== this.watchlistGeneration) {
        return null; // stopped between the two lookups
      }
      const values = await this._fetchHelper(query);
      const match = values.find(v => normalize(v.ticker) === symbol);
      if (match) {
        return match;
      }
    }
    return null;
  }

  getSavedWatchlistSymbols() {
    const pref =
      this.store.getState().Prefs.values[PREF_STOCKS_WATCHLIST] || "";
    return parseWatchlist(pref);
  }

  // Saved symbols that need their own fetch: those not already in the default
  // snapshot (default symbols get their data from the default fetch instead).
  watchlistFetchSet(saved) {
    const defaults = new Set(this.tickers.map(t => normalize(t.ticker)));
    return saved.filter(s => !defaults.has(s));
  }

  reconcileWatchlist({ full = false } = {}) {
    if (this.watchlistExpiring) {
      // Expire Cache is clearing state and will run the one reconcile itself.
      return this.watchlistWorker || Promise.resolve();
    }
    this.watchlistDesiredSaved = this.getSavedWatchlistSymbols();
    this.watchlistRequestedVersion++;
    this.pendingFullRefresh = this.pendingFullRefresh || full;
    return this._startWatchlistWorker();
  }

  _startWatchlistWorker() {
    if (this.watchlistWorker) {
      return this.watchlistWorker;
    }
    this.watchlistWorker = this._runWatchlistWorker(
      this.watchlistGeneration
    ).finally(() => {
      this.watchlistWorker = null;
      // A worker that stopped early can leave a request unprocessed; start a
      // fresh one at the current generation if the widget is still enabled.
      if (
        this.watchlistProcessedVersion !== this.watchlistRequestedVersion &&
        this.isEnabled()
      ) {
        this._startWatchlistWorker();
      }
    });
    return this.watchlistWorker;
  }

  async _runWatchlistWorker(generation) {
    while (this.watchlistProcessedVersion !== this.watchlistRequestedVersion) {
      const version = this.watchlistRequestedVersion;
      const saved = this.watchlistDesiredSaved; // fixed saved set for this pass
      const full = this.pendingFullRefresh;
      this.pendingFullRefresh = false;

      if (
        generation !== this.watchlistGeneration ||
        !this.ensureMerinoClient()
      ) {
        return;
      }
      const toFetch = this.watchlistFetchSet(saved);
      const have = new Map(
        this.watchlistTickers.map(t => [normalize(t.ticker), t])
      );
      const needed = full ? toFetch : toFetch.filter(s => !have.has(s));
      const fetched = await this._fetchWatchlistSymbols(
        needed,
        generation,
        version
      );

      if (generation !== this.watchlistGeneration) {
        return; // the widget was turned off during the fetch
      }
      if (version !== this.watchlistRequestedVersion) {
        // A newer request arrived; keep any full-refresh intent and reprocess.
        this.pendingFullRefresh ||= full;
        continue;
      }
      const merged = new Map(have);
      for (const [sym, summary] of fetched) {
        if (summary) {
          merged.set(sym, summary); // null keeps prior (stale) data or stays absent
        }
      }
      const keep = new Set(toFetch);
      this.watchlistTickers = [...merged.entries()]
        .filter(([sym]) => keep.has(sym))
        .map(([, summary]) => summary);
      this.watchlistSymbols = saved; // the saved set this pass resolved
      if (full) {
        this.watchlistLastFullRefresh = this.Date().now();
      }
      try {
        await this._writeWatchlistCache();
      } catch (e) {
        console.error("StocksFeed watchlist cache write failed", e);
      }
      if (generation !== this.watchlistGeneration) {
        return; // the widget was turned off during the cache write
      }
      this.watchlistProcessedVersion = version;
      this.broadcastWatchlist();
    }
  }

  // Look up saved symbols one at a time. The Merino client aborts any in-flight
  // request when a new one starts, so overlapping fetches would cancel each
  // other; going one by one keeps every request intact.
  async _fetchWatchlistSymbols(symbols, generation, version) {
    const results = new Map();
    for (const symbol of symbols) {
      if (
        generation !== this.watchlistGeneration ||
        version !== this.watchlistRequestedVersion
      ) {
        break; // stopped, or a newer request supersedes this pass
      }
      results.set(symbol, await this._fetchWatchlistSymbol(symbol, generation));
    }
    return results;
  }

  broadcastWatchlist() {
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WIDGETS_STOCKS_WATCHLIST_UPDATE,
        data: {
          watchlistTickers: this.watchlistTickers,
          reconciledSymbols: this.watchlistSymbols,
        },
      })
    );
  }

  // Serialize cache writes: PersistentCache.set() rewrites the whole file, so
  // writes to different keys must not overlap or one can overwrite the other.
  // The stored promise never rejects, so a failed write can't block later ones
  // or a caller that waits on the chain.
  _cacheSet(key, value) {
    const op = (this.cacheWriteChain || Promise.resolve()).then(() =>
      this.cache.set(key, value)
    );
    this.cacheWriteChain = op.catch(() => {});
    return op;
  }

  async _writeWatchlistCache() {
    await this._cacheSet("stocksWatchlist", {
      watchlistTickers: this.watchlistTickers,
      watchlistSymbols: this.watchlistSymbols,
      lastFullRefresh: this.watchlistLastFullRefresh,
    });
  }

  async loadWatchlist() {
    const generation = this.watchlistGeneration;
    const cached = (await this.cache.get()) || {};
    if (generation !== this.watchlistGeneration) {
      return; // turned off while reading the cache
    }
    const wl = cached.stocksWatchlist;
    if (wl?.watchlistTickers?.length) {
      this.watchlistTickers = wl.watchlistTickers;
      this.watchlistSymbols = wl.watchlistSymbols || [];
      this.watchlistLastFullRefresh = wl.lastFullRefresh ?? null;
      // Show cached rows immediately; the reconcile below refreshes them.
      this.broadcastWatchlist();
    }
    // Refetch everything when the cache is stale, otherwise just fetch any
    // saved symbols still missing data.
    await this.reconcileWatchlist({ full: this._watchlistIsStale() });
  }

  _watchlistIsStale() {
    return (
      this.watchlistLastFullRefresh === null ||
      this.Date().now() - this.watchlistLastFullRefresh >= STOCKS_UPDATE_TIME
    );
  }

  // Refresh the watchlist when its cached data is older than the update interval.
  async refreshWatchlistIfStale() {
    if (this._watchlistIsStale()) {
      await this.reconcileWatchlist({ full: true });
    }
  }

  update() {
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WIDGETS_STOCKS_UPDATE,
        data: {
          tickers: this.tickers,
          lastUpdated: this.lastUpdated,
          error: this.error,
        },
      })
    );
  }

  async onPrefChangedAction(action) {
    switch (action.data.name) {
      case PREF_WIDGETS_STOCKS_ENABLED:
      case PREF_WIDGETS_SYSTEM_STOCKS_ENABLED:
      case "trainhopConfig": {
        const enabled = this.isEnabled();
        if (enabled && !this.loaded) {
          await this.init();
        } else if (!enabled) {
          // Stop even mid-init, when this.loaded may still be false.
          this.stopFetching();
          this.loaded = false;
        }
        break;
      }
      case PREF_STOCKS_WATCHLIST:
        if (this.isEnabled()) {
          await this.reconcileWatchlist({ full: false });
        }
        break;
    }
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        if (this.isEnabled() && !this.loaded) {
          await this.init();
        }
        break;
      case at.UNINIT:
        this.stopFetching();
        this.loaded = false;
        break;
      case at.SYSTEM_TICK:
        if (this.isEnabled()) {
          await this.loadStocks();
          await this.refreshWatchlistIfStale();
        }
        break;
      case at.PREF_CHANGED:
        await this.onPrefChangedAction(action);
        break;
      case at.WIDGETS_STOCKS_SEARCH_REQUEST:
        await this.search(
          action.data?.query,
          action.data?.requestId,
          action.meta?.fromTarget
        );
        break;
      case at.DISCOVERY_STREAM_DEV_EXPIRE_CACHE:
        // Dev-tools "Expire Cache": clear both snapshots and refetch.
        this.watchlistExpiring = true;
        this.fetchGeneration++;
        this.watchlistGeneration++;
        this.watchlistRequestedVersion = this.watchlistProcessedVersion;
        this.pendingFullRefresh = false;
        await this.watchlistWorker;
        await this.cacheWriteChain;
        await this._cacheSet("stocks", {});
        await this._cacheSet("stocksWatchlist", {});
        this.tickers = [];
        this.watchlistTickers = [];
        this.watchlistSymbols = [];
        this.watchlistLastFullRefresh = null;
        this.lastUpdated = null;
        this.watchlistExpiring = false;
        if (this.isEnabled()) {
          await this.fetch();
          await this.reconcileWatchlist({ full: true });
        }
        break;
    }
  }
}

/**
 * Thin wrappers around external tools so tests can stub them.
 */
StocksFeed.prototype.MerinoClient = name => {
  return new lazy.MerinoClient(name, { allowOhttp: false });
};
StocksFeed.prototype.PersistentCache = (...args) => {
  return new lazy.PersistentCache(...args);
};
StocksFeed.prototype.Date = () => {
  return Date;
};
StocksFeed.prototype.setTimeout = (...args) => {
  return lazy.setTimeout(...args);
};
StocksFeed.prototype.clearTimeout = (...args) => {
  return lazy.clearTimeout(...args);
};
