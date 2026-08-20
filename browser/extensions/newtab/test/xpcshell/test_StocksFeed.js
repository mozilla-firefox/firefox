/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionTypes: "resource://newtab/common/Actions.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  StocksFeed: "resource://newtab/lib/Widgets/StocksFeed.sys.mjs",
});

function stubFeed(sandbox, { fetchResult = [], cache = {} } = {}) {
  sandbox.stub(StocksFeed.prototype, "PersistentCache").returns({
    get: async () => cache,
    set: async () => {},
  });
  const fetchStub = sandbox.stub().resolves(fetchResult);
  sandbox
    .stub(StocksFeed.prototype, "MerinoClient")
    .returns({ name: "TEST", fetch: fetchStub });
  // Don't fire either timer: the refresh timer would recursively call fetch(),
  // and the retry is now a background fetch that the retry tests trigger
  // explicitly.
  sandbox.stub(StocksFeed.prototype, "setTimeout").returns(1);
  sandbox.stub(StocksFeed.prototype, "clearTimeout");
  sandbox.stub(StocksFeed.prototype, "Date").returns({ now: () => 1000 });
  return fetchStub;
}

add_task(async function test_construction() {
  let sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  let feed = new StocksFeed();
  Assert.strictEqual(feed.loaded, false, "not loaded");
  Assert.strictEqual(feed.merino, null, "merino null");
  Assert.strictEqual(feed.tickers.length, 0, "tickers empty");
  Assert.strictEqual(feed.fetchTimer, null, "fetchTimer null");
  sandbox.restore();
});

add_task(async function test_fetch_parses_and_dispatches() {
  let sandbox = sinon.createSandbox();
  const fetchStub = stubFeed(sandbox, {
    fetchResult: [
      {
        custom_details: {
          polygon: {
            values: [
              {
                ticker: "SPY",
                name: "SPDR S&P 500 ETF Trust",
                // Matches what Merino returns: last_price ends with " USD" and no "%".
                last_price: "$559.44 USD",
                todays_change_perc: "+0.20",
              },
            ],
          },
        },
      },
    ],
  });

  let feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };

  await feed.fetch();

  const [args] = fetchStub.firstCall.args;
  Assert.deepEqual(args.providers, ["polygon"], "polygon provider");
  Assert.equal(args.otherParams.source, "newtab", "source newtab");
  Assert.equal(args.query, "", "empty query -> default ETFs");

  const [dispatched] = feed.store.dispatch.getCall(0).args;
  Assert.equal(
    dispatched.type,
    actionTypes.WIDGETS_STOCKS_UPDATE,
    "dispatches WIDGETS_STOCKS_UPDATE"
  );
  Assert.equal(dispatched.data.tickers.length, 1, "one ticker stored");
  Assert.equal(dispatched.data.tickers[0].ticker, "SPY", "ticker parsed");
  sandbox.restore();
});

add_task(async function test_fetch_supports_individual_query() {
  let sandbox = sinon.createSandbox();
  const fetchStub = stubFeed(sandbox, { fetchResult: [] });
  let feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  await feed.fetch("$AAPL");
  Assert.equal(
    fetchStub.firstCall.args[0].query,
    "$AAPL",
    "passes a non-empty query through the same path"
  );
  sandbox.restore();
});

add_task(async function test_stopFetching_clears_timer_without_merino() {
  let sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  let feed = new StocksFeed();
  feed.fetchTimer = 123;
  feed.merino = null;

  feed.stopFetching();

  Assert.ok(
    StocksFeed.prototype.clearTimeout.calledWith(123),
    "clearTimeout called even when merino is null"
  );
  Assert.strictEqual(feed.fetchTimer, null, "fetchTimer reset to null");
  Assert.strictEqual(feed.lastUpdated, null, "stopFetching resets lastUpdated");
  sandbox.restore();
});

add_task(async function test_isEnabled_gating() {
  let sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  let feed = new StocksFeed();
  feed.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": true,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };
  Assert.ok(feed.isEnabled(), "enabled when user + system prefs true");

  feed.store.getState = () => ({
    Prefs: { values: { "widgets.stocks.enabled": false } },
  });
  Assert.ok(!feed.isEnabled(), "disabled when user pref false");
  sandbox.restore();
});

add_task(async function test_onPrefChangedAction_enable_disable_reenable() {
  let sandbox = sinon.createSandbox();
  const fetchStub = stubFeed(sandbox, {
    fetchResult: [
      {
        custom_details: {
          polygon: {
            values: [
              {
                ticker: "SPY",
                name: "SPDR S&P 500 ETF Trust",
                last_price: "$559.44 USD",
                todays_change_perc: "+0.20",
              },
            ],
          },
        },
      },
    ],
  });

  let enabled = true;
  let feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": enabled,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };

  await feed.onPrefChangedAction({ data: { name: "widgets.stocks.enabled" } });
  Assert.ok(feed.loaded, "loaded after enabling");
  Assert.equal(fetchStub.callCount, 1, "fetched once after enabling");
  Assert.equal(
    feed.store.dispatch.callCount,
    2,
    "dispatches the default update and the watchlist update after enabling"
  );
  Assert.equal(
    feed.store.dispatch.getCall(0).args[0].type,
    actionTypes.WIDGETS_STOCKS_UPDATE,
    "the first dispatch is the default ticker update"
  );

  enabled = false;
  await feed.onPrefChangedAction({ data: { name: "widgets.stocks.enabled" } });
  Assert.strictEqual(feed.loaded, false, "disabling resets loaded");
  Assert.strictEqual(feed.tickers.length, 0, "disabling clears tickers");

  enabled = true;
  await feed.onPrefChangedAction({ data: { name: "widgets.stocks.enabled" } });
  Assert.ok(feed.loaded, "loaded again after re-enabling");
  Assert.equal(fetchStub.callCount, 2, "fetches again after re-enabling");
  sandbox.restore();
});

add_task(async function test_system_tick_while_disabled_does_not_fetch() {
  let sandbox = sinon.createSandbox();
  const fetchStub = stubFeed(sandbox);
  let feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: { values: { "widgets.stocks.enabled": false } },
    }),
  };

  await feed.onAction({ type: actionTypes.SYSTEM_TICK });

  Assert.ok(!fetchStub.called, "no Merino fetch while disabled");
  Assert.strictEqual(feed.loaded, false, "not marked loaded");
  sandbox.restore();
});

add_task(
  async function test_loadStocks_cache_hit_hydrates_without_merino_client() {
    let sandbox = sinon.createSandbox();
    const STOCKS_UPDATE_TIME = 15 * 60 * 1000;
    const now = 1_000_000;
    const age = 60 * 1000; // 1 minute old, well inside the 15 minute TTL.
    const cachedTickers = [
      {
        ticker: "SPY",
        name: "SPDR S&P 500 ETF Trust",
        last_price: "$559.44 USD",
        todays_change_perc: "+0.20",
      },
    ];
    sandbox.stub(StocksFeed.prototype, "PersistentCache").returns({
      get: async () => ({
        stocks: { tickers: cachedTickers, lastUpdated: now - age },
      }),
      set: async () => {},
    });
    const fetchStub = sandbox.stub();
    const merinoClientStub = sandbox
      .stub(StocksFeed.prototype, "MerinoClient")
      .returns({ name: "TEST", fetch: fetchStub });
    const setTimeoutStub = sandbox
      .stub(StocksFeed.prototype, "setTimeout")
      .returns(1);
    sandbox.stub(StocksFeed.prototype, "clearTimeout");
    sandbox.stub(StocksFeed.prototype, "Date").returns({ now: () => now });

    let feed = new StocksFeed();
    feed.store = {
      dispatch: sinon.spy(),
      getState: () => ({ Prefs: { values: {} } }),
    };

    await feed.loadStocks();

    Assert.ok(!fetchStub.called, "cache hit does not call Merino.fetch");
    Assert.ok(
      !merinoClientStub.called,
      "cache hit does not construct a Merino client"
    );
    Assert.equal(
      feed.store.dispatch.callCount,
      1,
      "dispatches the cached tickers"
    );
    const [dispatched] = feed.store.dispatch.getCall(0).args;
    Assert.equal(dispatched.data.tickers.length, 1, "hydrated from cache");
    Assert.ok(setTimeoutStub.called, "arms the refresh timer");
    Assert.equal(
      setTimeoutStub.firstCall.args[1],
      STOCKS_UPDATE_TIME - age,
      "arms the timer for the remaining TTL, not a full interval"
    );
    Assert.ok(feed.loaded, "marked loaded");
    sandbox.restore();
  }
);

add_task(async function test_fetch_handles_missing_custom_details() {
  let sandbox = sinon.createSandbox();
  const fetchStub = stubFeed(sandbox, { fetchResult: [{}] });
  let feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };

  await feed.fetch();

  Assert.equal(
    feed.tickers.length,
    0,
    "missing custom_details resolves to no tickers"
  );
  const [dispatched] = feed.store.dispatch.getCall(0).args;
  Assert.deepEqual(
    dispatched.data.tickers,
    [],
    "dispatches an empty ticker list"
  );
  Assert.equal(
    fetchStub.callCount,
    1,
    "missing custom_details is treated as empty; the retry is deferred to a timer"
  );
  Assert.equal(
    dispatched.data.error,
    true,
    "an empty response sets the error flag immediately"
  );
  sandbox.restore();
});

add_task(async function test_fetch_handles_non_array_values() {
  let sandbox = sinon.createSandbox();
  const fetchStub = stubFeed(sandbox, {
    fetchResult: [{ custom_details: { polygon: { values: "bad" } } }],
  });
  let feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };

  await feed.fetch();

  Assert.equal(
    feed.tickers.length,
    0,
    "non-array values resolves to no tickers"
  );
  const [dispatched] = feed.store.dispatch.getCall(0).args;
  Assert.deepEqual(
    dispatched.data.tickers,
    [],
    "dispatches an empty ticker list"
  );
  Assert.equal(
    fetchStub.callCount,
    1,
    "malformed values are treated as empty; the retry is deferred to a timer"
  );
  Assert.equal(
    dispatched.data.error,
    true,
    "malformed values set the error flag immediately"
  );
  sandbox.restore();
});

add_task(async function test_fetch_generation_guard_ignores_stale_response() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(StocksFeed.prototype, "PersistentCache").returns({
    get: async () => ({}),
    set: async () => {},
  });
  let resolveFetch;
  const fetchStub = sandbox.stub().returns(
    new Promise(resolve => {
      resolveFetch = resolve;
    })
  );
  sandbox
    .stub(StocksFeed.prototype, "MerinoClient")
    .returns({ name: "TEST", fetch: fetchStub });
  sandbox.stub(StocksFeed.prototype, "setTimeout").returns(1);
  const clearTimeoutStub = sandbox.stub(StocksFeed.prototype, "clearTimeout");
  sandbox.stub(StocksFeed.prototype, "Date").returns({ now: () => 1000 });

  let feed = new StocksFeed();
  // The Merino client is already set, so fetch() goes straight to the network
  // call without creating one first.
  feed.merino = { fetch: fetchStub };
  feed.fetchTimer = 42;
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };

  const fetchPromise = feed.fetch();
  // The widget is disabled while the Merino request is still running.
  feed.stopFetching();
  resolveFetch([
    {
      custom_details: {
        polygon: {
          values: [
            {
              ticker: "SPY",
              name: "SPDR S&P 500 ETF Trust",
              last_price: "$559.44 USD",
              todays_change_perc: "+0.20",
            },
          ],
        },
      },
    },
  ]);
  await fetchPromise;

  Assert.ok(
    clearTimeoutStub.calledWith(1),
    "stopFetching cleared the refresh timer armed during the in-flight fetch"
  );
  Assert.ok(
    !feed.store.dispatch.called,
    "a stale response after teardown does not dispatch"
  );
  Assert.strictEqual(
    feed.tickers.length,
    0,
    "a stale response after teardown does not repopulate tickers"
  );
  Assert.strictEqual(
    feed.lastUpdated,
    null,
    "a stale response after teardown does not re-arm lastUpdated"
  );
  sandbox.restore();
});

add_task(
  async function test_fetch_shows_error_immediately_and_schedules_retry() {
    const sandbox = sinon.createSandbox();
    const feed = new StocksFeed();
    feed.store = {
      dispatch: sinon.spy(),
      getState: () => ({ Prefs: { values: {} } }),
    };
    feed.merino = { fetch: sandbox.stub().rejects(new Error("network")) };
    sandbox.stub(feed, "restartFetchTimer");
    // Capture the scheduled retry instead of firing it.
    let retryDelay;
    sandbox.stub(feed, "setTimeout").callsFake((fn, ms) => {
      retryDelay = ms;
      return 1;
    });

    await feed.fetch();

    const [update] = feed.store.dispatch.getCalls().at(-1).args;
    Assert.equal(
      update.data.error,
      true,
      "the error flag is set on the first failure, without waiting for a retry"
    );
    Assert.equal(
      feed.merino.fetch.callCount,
      1,
      "only the initial attempt runs; the retry is deferred to a timer"
    );
    Assert.equal(retryDelay, 60 * 1000, "a retry is scheduled 60s later");
    sandbox.restore();
  }
);

add_task(async function test_fetch_clears_error_on_success() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  feed.error = true;
  feed.merino = {
    fetch: sandbox.stub().resolves([
      {
        custom_details: {
          polygon: {
            values: [
              {
                ticker: "SPY",
                name: "n",
                last_price: "$1 USD",
                todays_change_perc: "+0.1",
              },
            ],
          },
        },
      },
    ]),
  };
  sandbox.stub(feed, "restartFetchTimer");
  sandbox.stub(feed.cache, "set").resolves();

  await feed.fetch();

  const [update] = feed.store.dispatch.getCalls().at(-1).args;
  Assert.equal(update.data.error, false, "error flag is cleared on success");
  sandbox.restore();
});

add_task(function test_stopFetching_clears_retry_timer_and_error() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  const clearStub = sandbox.stub(feed, "clearTimeout");
  feed.retryTimer = 7;
  feed.error = true;
  feed.stopFetching();
  Assert.ok(clearStub.calledWith(7), "retry timer is cleared");
  Assert.equal(feed.error, false, "error is reset");
  sandbox.restore();
});

add_task(async function test_fetch_retry_succeeds_recovers() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  const fetchStub = sandbox.stub();
  fetchStub.onCall(0).rejects(new Error("network"));
  fetchStub.onCall(1).resolves([
    {
      custom_details: {
        polygon: {
          values: [
            {
              ticker: "SPY",
              name: "SPDR S&P 500 ETF Trust",
              last_price: "$559.44 USD",
              todays_change_perc: "+0.20",
            },
          ],
        },
      },
    },
  ]);
  feed.merino = { fetch: fetchStub };
  sandbox.stub(feed, "restartFetchTimer");
  sandbox.stub(feed.cache, "set").resolves();
  // Capture the scheduled retry callback so the test runs the real timer path.
  let retryCb;
  sandbox.stub(feed, "setTimeout").callsFake(fn => {
    retryCb = fn;
    return 1;
  });

  // First attempt fails: the error surfaces immediately.
  await feed.fetch();
  Assert.equal(
    feed.store.dispatch.getCall(0).args[0].data.error,
    true,
    "the first failure sets the error flag"
  );

  // Run the scheduled retry; it succeeds and clears the error.
  await retryCb();

  Assert.equal(fetchStub.callCount, 2, "one failed call plus one retry");
  const [update] = feed.store.dispatch.getCalls().at(-1).args;
  Assert.equal(
    update.data.error,
    false,
    "error flag is cleared once the retry succeeds"
  );
  Assert.equal(
    feed.tickers.length,
    1,
    "tickers are populated from the retry's response"
  );
  Assert.equal(
    feed.tickers[0].ticker,
    "SPY",
    "ticker parsed from the retry's response"
  );
  sandbox.restore();
});

add_task(async function test_stopFetching_cancels_pending_retry() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  const fetchStub = sandbox.stub().rejects(new Error("network"));
  feed.merino = { fetch: fetchStub };
  sandbox.stub(feed, "restartFetchTimer");
  const clearStub = sandbox.stub(feed, "clearTimeout");
  // Capture the scheduled retry (return a handle) without firing it.
  sandbox.stub(feed, "setTimeout").returns(42);

  await feed.fetch();
  Assert.equal(
    feed.retryTimer,
    42,
    "a retry timer is scheduled after a failure"
  );

  feed.stopFetching();

  Assert.ok(clearStub.calledWith(42), "stopFetching clears the pending retry");
  Assert.equal(feed.error, false, "teardown resets the error flag");
  Assert.equal(fetchStub.callCount, 1, "the cancelled retry never runs");
  sandbox.restore();
});

add_task(async function test_scheduled_retry_bails_after_teardown() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  const fetchStub = sandbox.stub().rejects(new Error("network"));
  feed.merino = { fetch: fetchStub };
  sandbox.stub(feed, "MerinoClient").returns({ fetch: fetchStub });
  const restartStub = sandbox.stub(feed, "restartFetchTimer");
  sandbox.stub(feed, "clearTimeout");
  // Capture the scheduled retry callback so the test can run it after teardown.
  let retryCb;
  sandbox.stub(feed, "setTimeout").callsFake(fn => {
    retryCb = fn;
    return 1;
  });

  await feed.fetch();
  Assert.equal(
    restartStub.callCount,
    1,
    "the first attempt armed the refresh timer once"
  );

  // The widget is stopped, then the already-scheduled retry callback runs.
  feed.stopFetching();
  await retryCb();

  Assert.equal(
    restartStub.callCount,
    1,
    "a retry that fires after teardown does not restart fetching"
  );
  Assert.equal(fetchStub.callCount, 1, "the stale retry does not fetch again");
  sandbox.restore();
});

add_task(async function test_expire_cache_clears_snapshot_and_refetches() {
  const sandbox = sinon.createSandbox();
  // An empty Merino result drives the error path on the forced refetch.
  const fetchStub = stubFeed(sandbox, { fetchResult: [] });
  const feed = new StocksFeed();
  const setSpy = sandbox.spy(feed.cache, "set");
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": true,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };
  // Pretend an earlier successful fetch left tickers in memory.
  feed.tickers = [{ ticker: "SPY" }];
  feed.lastUpdated = 500;

  await feed.onAction({
    type: actionTypes.DISCOVERY_STREAM_DEV_EXPIRE_CACHE,
  });

  Assert.ok(setSpy.calledWith("stocks", {}), "clears the saved snapshot");
  Assert.ok(
    setSpy.calledWith("stocksWatchlist", {}),
    "clears the watchlist snapshot too"
  );
  Assert.equal(fetchStub.callCount, 1, "refetches after expiring the cache");
  const update = feed.store.dispatch
    .getCalls()
    .map(c => c.args[0])
    .reverse()
    .find(a => a.type === actionTypes.WIDGETS_STOCKS_UPDATE);
  Assert.equal(update.data.error, true, "a failing refetch shows the error");
  Assert.deepEqual(update.data.tickers, [], "old tickers are cleared");
  sandbox.restore();
});

add_task(
  async function test_expire_cache_clears_snapshot_while_disabled_without_fetch() {
    const sandbox = sinon.createSandbox();
    const fetchStub = stubFeed(sandbox, { fetchResult: [] });
    const feed = new StocksFeed();
    const setSpy = sandbox.spy(feed.cache, "set");
    feed.store = {
      dispatch: sinon.spy(),
      getState: () => ({
        Prefs: { values: { "widgets.stocks.enabled": false } },
      }),
    };
    feed.tickers = [{ ticker: "SPY" }];

    await feed.onAction({
      type: actionTypes.DISCOVERY_STREAM_DEV_EXPIRE_CACHE,
    });

    Assert.ok(setSpy.calledWith("stocks", {}), "still clears the snapshot");
    Assert.equal(feed.tickers.length, 0, "still clears in-memory tickers");
    Assert.ok(!fetchStub.called, "does not fetch while disabled");
    sandbox.restore();
  }
);

add_task(async function test_success_cancels_pending_retry() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  const fetchStub = sandbox.stub();
  fetchStub.onCall(0).rejects(new Error("network"));
  fetchStub.onCall(1).resolves([
    {
      custom_details: {
        polygon: {
          values: [
            {
              ticker: "SPY",
              name: "SPDR S&P 500 ETF Trust",
              last_price: "$559.44 USD",
              todays_change_perc: "+0.20",
            },
          ],
        },
      },
    },
  ]);
  feed.merino = { fetch: fetchStub };
  sandbox.stub(feed, "restartFetchTimer");
  sandbox.stub(feed.cache, "set").resolves();
  const clearStub = sandbox.stub(feed, "clearTimeout");
  sandbox.stub(feed, "setTimeout").returns(99);

  // A failure schedules a retry.
  await feed.fetch();
  Assert.equal(feed.retryTimer, 99, "a retry is pending after the failure");

  // An independent successful fetch cancels that pending retry.
  await feed.fetch();
  Assert.ok(
    clearStub.calledWith(99),
    "the later success cancels the pending retry"
  );
  Assert.equal(feed.retryTimer, null, "the retry timer is reset after success");
  sandbox.restore();
});

add_task(async function test_ensureMerinoClient_reuses_client() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  const client = { name: "TEST", fetch: sinon.stub() };
  const clientStub = sandbox.stub(feed, "MerinoClient").returns(client);
  Assert.equal(
    feed.ensureMerinoClient(),
    client,
    "creates and returns a client"
  );
  Assert.equal(feed.ensureMerinoClient(), client, "reuses the existing client");
  Assert.equal(clientStub.callCount, 1, "does not create a second client");
  sandbox.restore();
});

add_task(async function test_fetchWatchlistSymbol_dollar_then_bare() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.merino = { name: "TEST" };
  const helper = sandbox.stub(feed, "_fetchHelper");
  helper.withArgs("$AAPL").resolves([{ ticker: "AAPL", name: "Apple" }]);
  Assert.deepEqual(
    await feed._fetchWatchlistSymbol("AAPL"),
    { ticker: "AAPL", name: "Apple" },
    "resolves via the dollar form"
  );
  helper.withArgs("$BRK.B").resolves([]);
  helper.withArgs("BRK.B").resolves([{ ticker: "BRK.B", name: "Berkshire" }]);
  Assert.deepEqual(
    await feed._fetchWatchlistSymbol("BRK.B"),
    { ticker: "BRK.B", name: "Berkshire" },
    "falls back to the bare form for dotted symbols"
  );
  helper.withArgs("$ZZZZ").resolves([]);
  helper.withArgs("ZZZZ").resolves([]);
  Assert.strictEqual(
    await feed._fetchWatchlistSymbol("ZZZZ"),
    null,
    "returns null when nothing resolves"
  );
  sandbox.restore();
});

function makeWatchlistFeed(sandbox, { saved = [], tickers = [] } = {}) {
  const feed = new StocksFeed();
  feed.merino = { name: "TEST" };
  feed.tickers = tickers;
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  sandbox.stub(feed, "getSavedWatchlistSymbols").returns(saved);
  sandbox.stub(feed, "_writeWatchlistCache").resolves();
  sandbox.stub(feed, "ensureMerinoClient").returns({ name: "TEST" });
  return feed;
}

add_task(async function test_reconcile_fetches_and_broadcasts_full_snapshot() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, { saved: ["AAPL"], tickers: [] });
  sandbox
    .stub(feed, "_fetchWatchlistSymbol")
    .withArgs("AAPL")
    .resolves({ ticker: "AAPL", name: "Apple" });
  await feed.reconcileWatchlist({ full: true });
  Assert.deepEqual(
    feed.watchlistTickers.map(t => t.ticker),
    ["AAPL"],
    "the desired symbol is fetched into watchlistTickers"
  );
  const [update] = feed.store.dispatch.getCalls().at(-1).args;
  Assert.equal(update.type, actionTypes.WIDGETS_STOCKS_WATCHLIST_UPDATE);
  Assert.deepEqual(
    update.data.reconciledSymbols,
    ["AAPL"],
    "broadcasts the full saved set as reconciled"
  );
  sandbox.restore();
});

add_task(async function test_reconcile_saved_default_reports_without_fetch() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, {
    saved: ["SPY"],
    tickers: [{ ticker: "SPY" }],
  });
  const fetchSym = sandbox.stub(feed, "_fetchWatchlistSymbol");
  await feed.reconcileWatchlist({ full: true });
  Assert.ok(!fetchSym.called, "a saved default symbol is not network-fetched");
  const [update] = feed.store.dispatch.getCalls().at(-1).args;
  Assert.deepEqual(
    update.data.reconciledSymbols,
    ["SPY"],
    "the default symbol is still reported reconciled"
  );
  Assert.deepEqual(
    feed.watchlistTickers,
    [],
    "no separate watchlist ticker for a default symbol"
  );
  sandbox.restore();
});

add_task(async function test_reconcile_rapid_add_never_reports_pending_ready() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, { saved: ["A"], tickers: [] });
  let aCalls = 0;
  let resolveA;
  sandbox.stub(feed, "_fetchWatchlistSymbol").callsFake(sym => {
    if (sym === "A") {
      aCalls++;
      if (aCalls === 1) {
        return new Promise(r => (resolveA = r));
      }
      return Promise.resolve({ ticker: "A", name: "A" });
    }
    return Promise.resolve({ ticker: sym, name: sym });
  });
  const p = feed.reconcileWatchlist({ full: true });
  // While A is still fetching, the pref adds B.
  feed.getSavedWatchlistSymbols.returns(["A", "B"]);
  feed.reconcileWatchlist({ full: false });
  resolveA({ ticker: "A", name: "A" });
  await p;
  await feed.watchlistWorker;
  for (const call of feed.store.dispatch.getCalls()) {
    const d = call.args[0].data;
    if (d.reconciledSymbols.includes("B")) {
      Assert.ok(
        d.watchlistTickers.some(t => t.ticker === "B"),
        "B is only reported reconciled once its data is present"
      );
    }
  }
  sandbox.restore();
});

add_task(async function test_reconcile_bails_after_teardown() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, { saved: ["AAPL"], tickers: [] });
  let resolveA;
  sandbox
    .stub(feed, "_fetchWatchlistSymbol")
    .returns(new Promise(r => (resolveA = r)));
  const p = feed.reconcileWatchlist({ full: true });
  feed.stopFetching(); // stop while the symbol fetch is still running
  resolveA({ ticker: "AAPL", name: "Apple" });
  await p;
  Assert.ok(
    !feed.store.dispatch.called,
    "a reconcile interrupted by teardown does not broadcast"
  );
  sandbox.restore();
});

add_task(async function test_stop_then_reenable_does_not_strand_worker() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, { saved: ["A"], tickers: [] });
  sandbox.stub(feed, "isEnabled").returns(true);
  let resolveFirst;
  const fetchSym = sandbox.stub(feed, "_fetchWatchlistSymbol");
  fetchSym.onFirstCall().returns(new Promise(r => (resolveFirst = r)));
  fetchSym.onSecondCall().resolves({ ticker: "A", name: "A" });
  // Queue a second request after stopFetching() bumps the generation, while the
  // first (now-stale) worker is still running, to check the queued request runs
  // once the stale worker stops.
  const p = feed.reconcileWatchlist({ full: true });
  feed.stopFetching();
  feed.reconcileWatchlist({ full: true });
  resolveFirst({ ticker: "A", name: "A" });
  await p;
  await feed.watchlistWorker;
  Assert.deepEqual(
    feed.watchlistSymbols,
    ["A"],
    "the reconciliation requested after re-enable eventually runs"
  );
  sandbox.restore();
});

add_task(async function test_init_loads_watchlist() {
  const sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": true,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };
  const reconcile = sandbox.stub(feed, "reconcileWatchlist").resolves();
  await feed.init();
  Assert.ok(reconcile.called, "init reconciles the watchlist");
  sandbox.restore();
});

add_task(async function test_watchlist_pref_change_incremental_reconcile() {
  const sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  const feed = new StocksFeed();
  feed.loaded = true;
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": true,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };
  const reconcile = sandbox.stub(feed, "reconcileWatchlist").resolves();
  await feed.onPrefChangedAction({
    data: { name: "widgets.stocks.watchlist" },
  });
  Assert.ok(
    reconcile.calledWithMatch({ full: false }),
    "a watchlist pref change does an incremental reconcile"
  );
  sandbox.restore();
});

add_task(async function test_disable_during_init_does_not_load_watchlist() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(StocksFeed.prototype, "PersistentCache").returns({
    get: async () => ({}),
    set: async () => {},
  });
  let resolveFetch;
  const fetchStub = sandbox
    .stub()
    .returns(new Promise(r => (resolveFetch = r)));
  sandbox
    .stub(StocksFeed.prototype, "MerinoClient")
    .returns({ name: "TEST", fetch: fetchStub });
  sandbox.stub(StocksFeed.prototype, "setTimeout").returns(1);
  sandbox.stub(StocksFeed.prototype, "clearTimeout");
  sandbox.stub(StocksFeed.prototype, "Date").returns({ now: () => 1000 });
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": true,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };
  const loadWatchlist = sandbox.stub(feed, "loadWatchlist").resolves();
  const initPromise = feed.init();
  feed.stopFetching(); // disabled mid-init, before the fetch resolves
  resolveFetch([]);
  await initPromise;
  Assert.ok(!feed.loaded, "init invalidated by teardown does not mark loaded");
  Assert.ok(
    !loadWatchlist.called,
    "does not load the watchlist after a mid-init teardown"
  );
  sandbox.restore();
});

add_task(async function test_refresh_timer_fetches_defaults_only() {
  const sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  let timerCb;
  StocksFeed.prototype.setTimeout.restore();
  sandbox.stub(StocksFeed.prototype, "setTimeout").callsFake(fn => {
    timerCb = fn;
    return 1;
  });
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({ Prefs: { values: {} } }),
  };
  const fetchStocks = sandbox.stub(feed, "fetch").resolves();
  const reconcile = sandbox.stub(feed, "reconcileWatchlist").resolves();
  feed.restartFetchTimer();
  await timerCb();
  Assert.ok(fetchStocks.called, "the timer fetches the default tickers");
  Assert.ok(!reconcile.called, "the timer does not reconcile the watchlist");
  sandbox.restore();
});

add_task(async function test_system_tick_refreshes_stale_watchlist() {
  const sandbox = sinon.createSandbox();
  stubFeed(sandbox);
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": true,
          "widgets.system.stocks.enabled": true,
        },
      },
    }),
  };
  sandbox.stub(feed, "loadStocks").resolves();
  const reconcile = sandbox.stub(feed, "reconcileWatchlist").resolves();
  feed.watchlistLastFullRefresh = null; // never refreshed, so stale
  await feed.onAction({ type: actionTypes.SYSTEM_TICK });
  Assert.ok(
    reconcile.calledWithMatch({ full: true }),
    "a stale watchlist is refreshed on the tick"
  );

  reconcile.resetHistory();
  feed.watchlistLastFullRefresh = 1000; // now() is 1000, so fresh
  await feed.onAction({ type: actionTypes.SYSTEM_TICK });
  Assert.ok(
    !reconcile.called,
    "a fresh watchlist is not refreshed on the tick"
  );
  sandbox.restore();
});

add_task(async function test_fetchWatchlistSymbols_stops_when_superseded() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.watchlistGeneration = 0;
  feed.watchlistRequestedVersion = 1;
  const calls = [];
  sandbox.stub(feed, "_fetchWatchlistSymbol").callsFake(async sym => {
    calls.push(sym);
    if (calls.length === 5) {
      // A newer request arrives while earlier symbols are resolving.
      feed.watchlistRequestedVersion = 2;
    }
    return { ticker: sym, name: sym };
  });
  const symbols = ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J"];
  await feed._fetchWatchlistSymbols(symbols, 0, 1);
  Assert.equal(
    calls.length,
    5,
    "stops fetching once the request is superseded"
  );
  sandbox.restore();
});

add_task(async function test_fetchWatchlistSymbols_serializes_requests() {
  const sandbox = sinon.createSandbox();
  const feed = new StocksFeed();
  feed.watchlistGeneration = 0;
  feed.watchlistRequestedVersion = 1;
  let inFlight = 0;
  let maxInFlight = 0;
  // Each lookup counts itself as in flight until its promise settles a microtask
  // later. The Merino client serves one request at a time, so the loop must wait
  // for each symbol before starting the next; if it fetched in parallel, several
  // would be counted in flight at once.
  sandbox.stub(feed, "_fetchWatchlistSymbol").callsFake(sym => {
    inFlight++;
    maxInFlight = Math.max(maxInFlight, inFlight);
    return Promise.resolve().then(() => {
      inFlight--;
      return { ticker: sym, name: sym };
    });
  });
  const symbols = ["A", "B", "C", "D", "E", "F"];
  const results = await feed._fetchWatchlistSymbols(symbols, 0, 1);
  Assert.equal(maxInFlight, 1, "only one symbol fetch is in flight at a time");
  Assert.equal(results.size, symbols.length, "every symbol resolved");
  sandbox.restore();
});

add_task(
  async function test_fetchWatchlistSymbol_aborts_on_generation_change() {
    const sandbox = sinon.createSandbox();
    const feed = new StocksFeed();
    feed.watchlistGeneration = 0;
    const helper = sandbox.stub(feed, "_fetchHelper");
    helper.withArgs("$AAPL").callsFake(async () => {
      feed.watchlistGeneration = 1; // widget turned off between the two lookups
      return [];
    });
    helper.withArgs("AAPL").resolves([{ ticker: "AAPL", name: "Apple" }]);
    const result = await feed._fetchWatchlistSymbol("AAPL", 0);
    Assert.strictEqual(
      result,
      null,
      "stops before the bare lookup after a generation change"
    );
    Assert.ok(!helper.calledWith("AAPL"), "the bare fallback never runs");
    sandbox.restore();
  }
);

add_task(async function test_worker_does_not_restart_without_pending_request() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, { saved: ["AAPL"], tickers: [] });
  sandbox.stub(feed, "isEnabled").returns(true);
  let resolveFetch;
  sandbox
    .stub(feed, "_fetchWatchlistSymbol")
    .returns(new Promise(r => (resolveFetch = r)));
  const startSpy = sandbox.spy(feed, "_startWatchlistWorker");
  const p = feed.reconcileWatchlist({ full: true }); // worker running
  // Expire Cache neutralizes pending work before the worker settles.
  feed.watchlistGeneration++;
  feed.watchlistRequestedVersion = feed.watchlistProcessedVersion;
  resolveFetch({ ticker: "AAPL", name: "Apple" });
  await p;
  Assert.equal(
    startSpy.callCount,
    1,
    "no restart when the pending request was cleared"
  );
  Assert.ok(
    !feed.store.dispatch.called,
    "the invalidated worker does not broadcast"
  );
  sandbox.restore();
});

add_task(async function test_reconcile_is_ignored_during_cache_expiry() {
  const sandbox = sinon.createSandbox();
  const feed = makeWatchlistFeed(sandbox, { saved: ["AAPL"], tickers: [] });
  const startSpy = sandbox.spy(feed, "_startWatchlistWorker");
  feed.watchlistExpiring = true;
  await feed.reconcileWatchlist({ full: true });
  Assert.ok(
    !startSpy.called,
    "a reconcile during cache expiry does not start a worker"
  );
  sandbox.restore();
});

function makeSearchFeed(sandbox, { enabled = true } = {}) {
  const feed = new StocksFeed();
  feed.store = {
    dispatch: sinon.spy(),
    getState: () => ({
      Prefs: {
        values: {
          "widgets.stocks.enabled": enabled,
          "widgets.system.stocks.enabled": enabled,
        },
      },
    }),
  };
  return feed;
}

// A fake search Merino client. Each fetch() call takes the next outcome in
// order: { values } for results, or { status } for an error.
function searchClient(sandbox, outcomes) {
  const client = {
    lastFetchStatus: "success",
    fetch: sandbox.stub(),
    resetSession: sandbox.stub(),
  };
  outcomes.forEach((outcome, i) => {
    client.fetch.onCall(i).callsFake(async () => {
      client.lastFetchStatus = outcome.status ?? "success";
      const values = outcome.values ?? [];
      return values.length ? [{ custom_details: { polygon: { values } } }] : [];
    });
  });
  return client;
}

// search() creates one client per call, so a single search reuses one client for
// its bare and dollar lookups. Hand it the fake by stubbing MerinoClient.
function stubSearchClient(sandbox, feed, outcomes) {
  const client = searchClient(sandbox, outcomes);
  sandbox.stub(feed, "MerinoClient").returns(client);
  return client;
}

function lastSearchResponse(feed) {
  return feed.store.dispatch.getCalls().at(-1)?.args[0];
}

add_task(async function test_search_success_replies_to_target() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [{ ticker: "AAPL", name: "Apple" }] },
  ]);
  await feed.search("AAPL", "r1", "port-1");
  const res = lastSearchResponse(feed);
  Assert.equal(res.type, actionTypes.WIDGETS_STOCKS_SEARCH_RESPONSE);
  Assert.equal(res.data.status, "success");
  Assert.deepEqual(res.data.values, [{ ticker: "AAPL", name: "Apple" }]);
  Assert.equal(res.data.requestId, "r1", "echoes the requestId");
  Assert.equal(res.data.query, "AAPL", "echoes the query");
  Assert.equal(res.meta.toTarget, "port-1", "replies only to the asking tab");
  Assert.equal(client.fetch.callCount, 1, "one call for a bare hit");
  Assert.ok(
    client.resetSession.calledOnce,
    "ends the per-search client's session so it can be released"
  );
  sandbox.restore();
});

add_task(async function test_search_empty_after_both_forms() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { status: "no_suggestion", values: [] },
    { status: "no_suggestion", values: [] },
  ]);
  await feed.search("ZZZZ", "r1", "port-1");
  const res = lastSearchResponse(feed);
  Assert.equal(res.data.status, "empty");
  Assert.deepEqual(res.data.values, []);
  Assert.equal(
    client.fetch.callCount,
    2,
    'tries the "<query> stock" form when the bare query is empty'
  );
  sandbox.restore();
});

add_task(async function test_search_stock_fallback_resolves_blocklisted() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [] },
    { values: [{ ticker: "SPY", name: "SPDR" }] },
  ]);
  await feed.search("SPY", "r1", "port-1");
  const res = lastSearchResponse(feed);
  Assert.equal(res.data.status, "success");
  Assert.deepEqual(res.data.values, [{ ticker: "SPY", name: "SPDR" }]);
  Assert.equal(
    client.fetch.secondCall.args[0].query,
    "spy stock",
    'the fallback uses the "<query> stock" form'
  );
  sandbox.restore();
});

add_task(async function test_search_stock_fallback_resolves_company_name() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [] },
    { values: [{ ticker: "AMZN", name: "Amazon.com Inc" }] },
  ]);
  await feed.search("Amazon", "r1", "port-1");
  const res = lastSearchResponse(feed);
  Assert.equal(res.data.status, "success");
  Assert.deepEqual(res.data.values, [
    { ticker: "AMZN", name: "Amazon.com Inc" },
  ]);
  Assert.equal(
    client.fetch.secondCall.args[0].query,
    "amazon stock",
    'a company name is looked up lower-cased with a "stock" suffix'
  );
  sandbox.restore();
});

add_task(async function test_search_no_fallback_when_bare_hits() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [{ ticker: "AAPL" }] },
  ]);
  await feed.search("AAPL", "r1", "port-1");
  Assert.equal(
    client.fetch.callCount,
    1,
    "no dollar fallback after a bare hit"
  );
  sandbox.restore();
});

add_task(async function test_search_transport_error_no_fallback() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { status: "timeout", values: [] },
  ]);
  await feed.search("AAPL", "r1", "port-1");
  Assert.equal(lastSearchResponse(feed).data.status, "error");
  Assert.equal(
    client.fetch.callCount,
    1,
    "a transport error is not retried with the fallback"
  );
  sandbox.restore();
});

add_task(async function test_search_fallback_transport_error_replies_error() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [] },
    { status: "timeout", values: [] },
  ]);
  await feed.search("ZZZZ", "r1", "port-1");
  Assert.equal(lastSearchResponse(feed).data.status, "error");
  Assert.equal(
    client.fetch.callCount,
    2,
    "a transport error on the fallback replies error"
  );
  sandbox.restore();
});

add_task(async function test_search_strips_leading_dollars() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [] },
    { values: [{ ticker: "BRK.B" }] },
  ]);
  await feed.search("$$BRK.B", "r1", "port-1");
  Assert.equal(
    client.fetch.firstCall.args[0].query,
    "BRK.B",
    "all leading dollars are stripped before the bare lookup"
  );
  Assert.equal(
    client.fetch.secondCall.args[0].query,
    "brk.b stock",
    'the fallback appends " stock" to the stripped, lower-cased query'
  );
  sandbox.restore();
});

add_task(async function test_search_whitespace_replies_empty_without_fetch() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [
    { values: [{ ticker: "SPY" }] },
  ]);
  await feed.search("   ", "r1", "port-1");
  Assert.equal(lastSearchResponse(feed).data.status, "empty");
  Assert.ok(
    !client.fetch.called,
    "a blank query never reaches Merino, which would return the default set"
  );
  sandbox.restore();
});

add_task(async function test_search_non_string_replies_error_without_fetch() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [{ values: [] }]);
  await feed.search(undefined, "r1", "port-1");
  Assert.equal(lastSearchResponse(feed).data.status, "error");
  Assert.ok(!client.fetch.called, "a non-string query does not fetch");
  sandbox.restore();
});

add_task(async function test_search_unexpected_throw_replies_error() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = {
    lastFetchStatus: "success",
    fetch: sandbox.stub().rejects(new Error("boom")),
    resetSession: sandbox.stub(),
  };
  sandbox.stub(feed, "MerinoClient").returns(client);
  await feed.search("AAPL", "r1", "port-1");
  Assert.equal(
    lastSearchResponse(feed).data.status,
    "error",
    "an unexpected throw still resolves the tab's loading state"
  );
  Assert.ok(
    client.resetSession.called,
    "the per-search client's session is ended even on a throw"
  );
  sandbox.restore();
});

add_task(async function test_search_disabled_replies_error_without_fetch() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox, { enabled: false });
  const client = stubSearchClient(sandbox, feed, [
    { values: [{ ticker: "AAPL" }] },
  ]);
  await feed.search("AAPL", "r1", "port-1");
  Assert.equal(lastSearchResponse(feed).data.status, "error");
  Assert.ok(!client.fetch.called, "a disabled widget does not fetch");
  sandbox.restore();
});

add_task(async function test_search_no_target_does_not_reply() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const client = stubSearchClient(sandbox, feed, [{ values: [] }]);
  await feed.search("AAPL", "r1", undefined);
  Assert.ok(
    !feed.store.dispatch.called,
    "a request with no reply port dispatches nothing"
  );
  Assert.ok(!client.fetch.called, "and does not fetch");
  sandbox.restore();
});

add_task(async function test_search_uses_a_separate_client_per_call() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const clientA = searchClient(sandbox, [{ values: [{ ticker: "BRK.B" }] }]);
  const clientB = searchClient(sandbox, [{ values: [{ ticker: "AAPL" }] }]);
  const queue = [clientA, clientB];
  sandbox.stub(feed, "MerinoClient").callsFake(() => queue.shift());
  // Two searches interleave; each runs on its own client so they cannot abort
  // each other, and each replies to its own tab.
  await Promise.all([
    feed.search("BRK.B", "rA", "port-A"),
    feed.search("AAPL", "rB", "port-B"),
  ]);
  Assert.equal(feed.MerinoClient.callCount, 2, "one client per search");
  Assert.ok(clientA.fetch.called, "the first search used its own client");
  Assert.ok(clientB.fetch.called, "the second search used its own client");
  const byTarget = {};
  for (const call of feed.store.dispatch.getCalls()) {
    const [action] = call.args;
    byTarget[action.meta.toTarget] = action.data;
  }
  Assert.equal(byTarget["port-A"].status, "success");
  Assert.deepEqual(byTarget["port-A"].values, [{ ticker: "BRK.B" }]);
  Assert.equal(byTarget["port-B"].status, "success");
  Assert.deepEqual(byTarget["port-B"].values, [{ ticker: "AAPL" }]);
  sandbox.restore();
});

add_task(async function test_does_not_touch_feed_state() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  feed.tickers = [{ ticker: "SPY" }];
  feed.watchlistTickers = [{ ticker: "AAPL" }];
  feed.merino = {
    fetch: sandbox.stub().rejects(new Error("default client must not be used")),
  };
  const cacheSet = sandbox.stub(feed, "_cacheSet").resolves();
  stubSearchClient(sandbox, feed, [{ values: [{ ticker: "MSFT" }] }]);
  await feed.search("MSFT", "r1", "port-1");
  Assert.deepEqual(
    feed.tickers,
    [{ ticker: "SPY" }],
    "default tickers untouched"
  );
  Assert.deepEqual(
    feed.watchlistTickers,
    [{ ticker: "AAPL" }],
    "watchlist tickers untouched"
  );
  Assert.ok(
    !feed.merino.fetch.called,
    "search does not use the default client"
  );
  Assert.ok(!cacheSet.called, "search does not write the cache");
  const types = feed.store.dispatch.getCalls().map(c => c.args[0].type);
  Assert.ok(
    !types.includes(actionTypes.WIDGETS_STOCKS_UPDATE),
    "search does not broadcast a default update"
  );
  Assert.ok(
    !types.includes(actionTypes.WIDGETS_STOCKS_WATCHLIST_UPDATE),
    "search does not broadcast a watchlist update"
  );
  sandbox.restore();
});

add_task(async function test_onAction_search_request_calls_search() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  const searchStub = sandbox.stub(feed, "search").resolves();
  await feed.onAction({
    type: actionTypes.WIDGETS_STOCKS_SEARCH_REQUEST,
    data: { query: "AAPL", requestId: "r1" },
    meta: { fromTarget: "port-7" },
  });
  Assert.ok(
    searchStub.calledOnceWith("AAPL", "r1", "port-7"),
    "onAction routes the request to search()"
  );
  sandbox.restore();
});

add_task(async function test_onAction_search_request_missing_meta_is_safe() {
  const sandbox = sinon.createSandbox();
  const feed = makeSearchFeed(sandbox);
  stubSearchClient(sandbox, feed, [{ values: [] }]);
  await feed.onAction({
    type: actionTypes.WIDGETS_STOCKS_SEARCH_REQUEST,
    data: { query: "AAPL", requestId: "r1" },
  });
  Assert.ok(
    !feed.store.dispatch.called,
    "a request with no reply port dispatches nothing and does not throw"
  );
  sandbox.restore();
});
