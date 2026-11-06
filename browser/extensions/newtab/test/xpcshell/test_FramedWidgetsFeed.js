/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionTypes: "resource://newtab/common/Actions.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  FramedWidgetsFeed: "resource://newtab/lib/Widgets/FramedWidgetsFeed.sys.mjs",
});

const PROD_ENDPOINT =
  "https://merino.services.mozilla.com/api/v1/widgets/frames";
const STAGE_ENDPOINT =
  "https://merino.services.mozilla.com/stage/api/v1/widgets/frames";
const CROSSWORD_URL =
  "https://prod-games-particle.merino.prod.webservices.mozgcp.net/index.html";
const MANIFEST = {
  date_utc: "2026-11-03T17:44:42.528272Z",
  locale: "en-US",
  widgets: [
    { id: "crossword", enabled: true, url: CROSSWORD_URL, vendor: "particle" },
    {
      id: "notARegisteredWidget",
      enabled: true,
      url: CROSSWORD_URL,
      vendor: "mozilla",
    },
  ],
};
const FRAMES = { crossword: { url: CROSSWORD_URL } };

const ENABLED_PREFS = {
  "widgets.enabled": true,
  "widgets.crossword.enabled": true,
  "widgets.system.crossword.enabled": true,
  "widgets.framed.endpoint.prod": PROD_ENDPOINT,
  "widgets.framed.endpoint.stage": STAGE_ENDPOINT,
  "widgets.framed.environment": "prod",
  "widgets.framed.allowedHostSuffixes": ".mozgcp.net,.mozilla.com",
  "discoverystream.endpoints": "https://merino.services.mozilla.com/",
};

function stubMerino(sandbox, feed, result) {
  feed.merino = { fetchFramedWidgets: sandbox.stub().resolves(result) };
  return feed.merino.fetchFramedWidgets;
}

function makeFeed(sandbox, { prefs = ENABLED_PREFS, cache = {} } = {}) {
  const proto = FramedWidgetsFeed.prototype;
  if (!proto.PersistentCache.isSinonProxy) {
    sandbox.stub(proto, "PersistentCache");
  }
  const set = sandbox.stub().resolves();
  proto.PersistentCache.returns({ get: async () => cache, set });
  const feed = new FramedWidgetsFeed();
  feed.store = {
    dispatch: sandbox.spy(),
    getState: () => ({ Prefs: { values: prefs } }),
  };
  feed.cacheSet = set;
  stubMerino(sandbox, feed, { data: MANIFEST, error: null });
  return feed;
}

function lastUpdate(feed) {
  return feed.store.dispatch
    .getCalls()
    .filter(c => c.args[0]?.type === actionTypes.FRAMED_WIDGETS_UPDATE)
    .at(-1)?.args[0].data;
}

add_task(async function test_fetch_caches_and_broadcasts() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);

  await feed.onAction({ type: actionTypes.INIT });

  Assert.ok(
    feed.merino.fetchFramedWidgets.calledWithMatch({
      endpointUrl: PROD_ENDPOINT,
      locale: Services.locale.appLocaleAsBCP47,
    }),
    "Fetches the prod endpoint for the app locale"
  );
  Assert.deepEqual(
    lastUpdate(feed),
    FRAMES,
    "Broadcasts the enabled entries of registered framed widgets"
  );
  Assert.equal(
    feed.cacheSet.firstCall.args[1].environment,
    "prod",
    "Caches the manifest with its environment"
  );
  sandbox.restore();
});

add_task(async function test_fresh_cache_skips_fetch() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    cache: {
      frames: {
        environment: "prod",
        manifest: MANIFEST,
        lastUpdated: Date.now(),
      },
    },
  });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.ok(feed.merino.fetchFramedWidgets.notCalled, "Serves a fresh cache");
  Assert.deepEqual(lastUpdate(feed), FRAMES, "Broadcasts the cached manifest");
  sandbox.restore();
});

add_task(async function test_cache_expires_after_30_minutes() {
  const sandbox = sinon.createSandbox();
  const NOW = Date.UTC(2026, 10, 6, 12, 0, 0);
  sandbox.stub(FramedWidgetsFeed.prototype, "Date").returns({ now: () => NOW });
  const feed = makeFeed(sandbox);
  const MINUTE = 60 * 1000;

  Assert.ok(feed.isStale(undefined), "No timestamp is stale");
  Assert.ok(!feed.isStale(NOW), "A just-fetched manifest is fresh");
  Assert.ok(!feed.isStale(NOW - 29 * MINUTE), "Fresh before 30 minutes");
  Assert.ok(feed.isStale(NOW - 30 * MINUTE), "Stale after 30 minutes");
  Assert.ok(feed.isStale(NOW + MINUTE), "A future timestamp is stale");
  Assert.ok(feed.isStale(NOW - 2 * 60 * MINUTE), "Stale after two hours");

  sandbox.restore();
});

add_task(async function test_expired_cache_refetches() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    cache: {
      frames: {
        environment: "prod",
        manifest: { widgets: [] },
        lastUpdated: Date.now() - 31 * 60 * 1000,
      },
    },
  });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.ok(
    feed.merino.fetchFramedWidgets.calledOnce,
    "Refetches a cache older than 30 minutes"
  );
  Assert.deepEqual(lastUpdate(feed), FRAMES, "Broadcasts the new manifest");
  sandbox.restore();
});

add_task(async function test_system_tick_skips_fresh_cache() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    cache: {
      frames: {
        environment: "prod",
        manifest: MANIFEST,
        lastUpdated: Date.now(),
      },
    },
  });

  await feed.onAction({ type: actionTypes.SYSTEM_TICK });

  Assert.ok(feed.merino.fetchFramedWidgets.notCalled, "Serves a fresh cache");
  Assert.deepEqual(lastUpdate(feed), FRAMES, "Broadcasts the cached manifest");
  sandbox.restore();
});

add_task(async function test_system_tick_retries_failed_fetch() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);
  stubMerino(sandbox, feed, { data: null, error: "load_error" });

  await feed.onAction({ type: actionTypes.INIT });
  Assert.deepEqual(lastUpdate(feed), {}, "No frames after the failed fetch");

  stubMerino(sandbox, feed, { data: MANIFEST, error: null });
  await feed.onAction({ type: actionTypes.SYSTEM_TICK });

  Assert.ok(
    feed.merino.fetchFramedWidgets.calledOnce,
    "The next tick fetches again"
  );
  Assert.deepEqual(lastUpdate(feed), FRAMES, "Broadcasts the new manifest");
  sandbox.restore();
});

add_task(async function test_fetch_error_uses_stale_cache() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    cache: {
      frames: { environment: "prod", manifest: MANIFEST, lastUpdated: 1 },
    },
  });
  stubMerino(sandbox, feed, { data: null, error: "load_error" });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.deepEqual(lastUpdate(feed), FRAMES, "Falls back to the stale cache");
  sandbox.restore();
});

add_task(async function test_no_manifest_broadcasts_empty() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);
  stubMerino(sandbox, feed, { data: null, error: "load_error" });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.deepEqual(lastUpdate(feed), {}, "No framed widget has a frame");
  sandbox.restore();
});

add_task(async function test_endpoint_must_be_allowlisted() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    prefs: {
      ...ENABLED_PREFS,
      "widgets.framed.endpoint.prod": "https://evil.example.com/frames",
    },
  });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.ok(feed.merino.fetchFramedWidgets.notCalled, "Does not fetch");
  Assert.deepEqual(lastUpdate(feed), {}, "No framed widget has a frame");
  sandbox.restore();
});

add_task(async function test_unchanged_manifest_is_not_rebroadcast() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);
  await feed.onAction({ type: actionTypes.INIT });
  const broadcasts = () =>
    feed.store.dispatch
      .getCalls()
      .filter(c => c.args[0]?.type === actionTypes.FRAMED_WIDGETS_UPDATE)
      .length;
  Assert.equal(broadcasts(), 1, "Broadcasts the first manifest");

  feed.update();
  Assert.equal(broadcasts(), 1, "Skips an identical rebroadcast");

  feed.manifest = { widgets: [] };
  feed.update();
  Assert.equal(broadcasts(), 2, "Broadcasts a changed manifest");
  sandbox.restore();
});

add_task(async function test_endpoint_follows_environment() {
  const sandbox = sinon.createSandbox();
  const prefs = { ...ENABLED_PREFS };
  const feed = makeFeed(sandbox, { prefs });
  Assert.equal(feed.getEndpoint(), PROD_ENDPOINT, "prod by default");

  prefs["widgets.framed.environment"] = "stage";
  Assert.equal(feed.getEndpoint(), STAGE_ENDPOINT, "stage when selected");

  prefs["widgets.framed.environment"] = "bogus";
  Assert.equal(feed.getEndpoint(), PROD_ENDPOINT, "prod for unknown values");
  sandbox.restore();
});

add_task(async function test_environment_change_refetches() {
  const sandbox = sinon.createSandbox();
  const prefs = { ...ENABLED_PREFS };
  const feed = makeFeed(sandbox, { prefs });
  await feed.onAction({ type: actionTypes.INIT });

  prefs["widgets.framed.environment"] = "stage";
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "widgets.framed.environment", value: "stage" },
  });

  const { fetchFramedWidgets } = feed.merino;
  Assert.equal(fetchFramedWidgets.callCount, 2, "Fetches again");
  Assert.equal(
    fetchFramedWidgets.secondCall.args[0].endpointUrl,
    STAGE_ENDPOINT,
    "The second fetch is for the stage endpoint"
  );
  sandbox.restore();
});

add_task(async function test_ignores_cache_from_other_environment() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    cache: {
      frames: {
        environment: "stage",
        manifest: { widgets: [] },
        lastUpdated: Date.now(),
      },
    },
  });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.ok(
    feed.merino.fetchFramedWidgets.calledOnce,
    "Fetches instead of serving the stage cache"
  );
  Assert.deepEqual(
    lastUpdate(feed),
    FRAMES,
    "Broadcasts the freshly fetched prod manifest"
  );
  sandbox.restore();
});

add_task(async function test_keeps_only_enabled_entries() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);
  Assert.deepEqual(
    feed.normalize({
      widgets: [{ id: "crossword", enabled: true, url: CROSSWORD_URL }],
    }),
    FRAMES,
    "Keeps an enabled entry"
  );
  for (const enabled of [false, undefined, "true", 1]) {
    Assert.deepEqual(
      feed.normalize({
        widgets: [{ id: "crossword", enabled, url: CROSSWORD_URL }],
      }),
      {},
      `Drops enabled: ${JSON.stringify(enabled)}`
    );
  }
  sandbox.restore();
});

add_task(async function test_rejects_non_array_widgets() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);
  Assert.deepEqual(
    feed.normalize({
      widgets: { crossword: { enabled: true, url: CROSSWORD_URL } },
    }),
    {},
    "Ignores the old keyed-object shape"
  );
  stubMerino(sandbox, feed, {
    data: { widgets: { crossword: { enabled: true, url: CROSSWORD_URL } } },
    error: null,
  });
  await feed.onAction({ type: actionTypes.INIT });
  Assert.ok(feed.cacheSet.notCalled, "Does not cache a malformed response");
  sandbox.restore();
});

add_task(async function test_drops_untrusted_urls() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox);
  for (const url of [
    "http://prod-games.example.mozgcp.net/index.html",
    "https://evil.example.com/index.html",
    "https://mozgcp.net.evil.example/index.html",
    "not a url",
  ]) {
    Assert.deepEqual(
      feed.normalize({ widgets: [{ id: "crossword", enabled: true, url }] }),
      {},
      `Drops ${url}`
    );
  }
  sandbox.restore();
});

add_task(async function test_skips_when_no_framed_widget_enabled() {
  const sandbox = sinon.createSandbox();
  const feed = makeFeed(sandbox, {
    prefs: { ...ENABLED_PREFS, "widgets.crossword.enabled": false },
  });

  await feed.onAction({ type: actionTypes.INIT });

  Assert.ok(feed.merino.fetchFramedWidgets.notCalled, "Does not fetch");
  Assert.ok(!lastUpdate(feed), "Does not broadcast");
  Assert.ok(!feed.loaded, "Stays unloaded");
  sandbox.restore();
});
