/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ContileIntegration: "resource://newtab/lib/TopSitesFeed.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  DEFAULT_TOP_SITES: "resource://newtab/lib/TopSitesFeed.sys.mjs",
  FilterAdult: "resource:///modules/FilterAdult.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  Screenshots: "resource://newtab/lib/Screenshots.sys.mjs",
  Sampling: "resource://gre/modules/components-utils/Sampling.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SponsoredTopSitesScoreProvider:
    "resource://newtab/lib/SponsoredTopSitesScoreProvider/SponsoredTopSitesScoreProvider.mjs",
  TOP_SITES_DEFAULT_ROWS:
    "moz-src:///browser/components/topsites/constants.mjs",
  TOP_SITES_MAX_SITES_PER_ROW:
    "moz-src:///browser/components/topsites/constants.mjs",
  TopSitesFeed: "resource://newtab/lib/TopSitesFeed.sys.mjs",
});

const FAKE_FAVICON = "data987";
const FAKE_FAVICON_SIZE = 128;
const FAKE_FRECENCY = PlacesUtils.history.pageFrecencyThreshold(0, 2, false);
const FAKE_LINKS = new Array(2 * TOP_SITES_MAX_SITES_PER_ROW)
  .fill(null)
  .map((v, i) => ({
    frecency: FAKE_FRECENCY,
    url: `http://www.site${i}.com`,
  }));
const FAKE_SCREENSHOT = "data123";
const SEARCH_SHORTCUTS_EXPERIMENT_PREF = "improvesearch.topSiteSearchShortcuts";
const SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF =
  "improvesearch.topSiteSearchShortcuts.searchEngines";
const SEARCH_SHORTCUTS_HAVE_PINNED_PREF =
  "improvesearch.topSiteSearchShortcuts.havePinned";
const SHOWN_ON_NEWTAB_PREF = "feeds.topsites";
const SHOW_SPONSORED_PREF = "showSponsoredTopSites";
const PREF_SYSTEM_SHORTCUTS_PERSONALIZATION =
  "discoverystream.shortcuts.personalization.enabled";
const TOP_SITES_BLOCKED_SPONSORS_PREF = "browser.topsites.blockedSponsors";

// This pref controls how long the contile cache is valid for in seconds.
const CONTILE_CACHE_VALID_FOR_SECONDS_PREF =
  "browser.topsites.contile.cacheValidFor";
// This pref records when the last contile fetch occurred, as a UNIX timestamp
// in seconds.
const CONTILE_CACHE_LAST_FETCH_PREF = "browser.topsites.contile.lastFetch";

function FakeTippyTopProvider() {}
FakeTippyTopProvider.prototype = {
  async init() {
    this.initialized = true;
  },
  processSite(site) {
    return site;
  },
};

let gSearchServiceInitStub;
let gGetTopSitesStub;

function getTopSitesFeedForTest(sandbox) {
  sandbox.stub(ContileIntegration.prototype, "PersistentCache").returns({
    set: sandbox.stub(),
    get: sandbox.stub(),
  });

  let feed = new TopSitesFeed();

  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return this.state;
    },
    state: {
      Prefs: {
        values: {
          topSitesRows: 2,
        },
      },
      TopSites: { rows: Array(12).fill("site") },
    },
  };

  return feed;
}

add_setup(async () => {
  let sandbox = sinon.createSandbox();
  sandbox.stub(SearchService, "defaultEngine").get(() => {
    return { identifier: "ddg", searchUrlDomain: "duckduckgo.com" };
  });

  gGetTopSitesStub = sandbox
    .stub(NewTabUtils.activityStreamLinks, "getTopSites")
    .resolves(FAKE_LINKS);

  gSearchServiceInitStub = sandbox.stub(SearchService, "init").resolves();

  sandbox.stub(NewTabUtils.activityStreamProvider, "_faviconBytesToDataURI");

  sandbox
    .stub(NewTabUtils.activityStreamProvider, "_addFavicons")
    .callsFake(l => {
      return Promise.resolve(
        l.map(link => {
          link.favicon = FAKE_FAVICON;
          link.faviconSize = FAKE_FAVICON_SIZE;
          return link;
        })
      );
    });

  sandbox.stub(Screenshots, "getScreenshotForURL").resolves(FAKE_SCREENSHOT);
  sandbox.spy(Screenshots, "maybeCacheScreenshot");
  sandbox.stub(Screenshots, "_shouldGetScreenshots").returns(true);

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function test_construction() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(ContileIntegration.prototype, "PersistentCache").returns({
    set: sandbox.stub(),
    get: sandbox.stub(),
  });
  let feed = new TopSitesFeed();
  Assert.ok(feed, "Could construct a TopSitesFeed");
  Assert.ok(feed._currentSearchHostname, "_currentSearchHostname defined");
  sandbox.restore();
});

add_task(async function test_refreshDefaults() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(ContileIntegration.prototype, "PersistentCache").returns({
    set: sandbox.stub(),
    get: sandbox.stub(),
  });
  let feed = new TopSitesFeed();
  Assert.ok(
    !DEFAULT_TOP_SITES.length,
    "Should have 0 DEFAULT_TOP_SITES initially."
  );

  info("refreshDefaults should add defaults on PREFS_INITIAL_VALUES");
  feed.onAction({
    type: actionTypes.PREFS_INITIAL_VALUES,
    data: { "default.sites": "https://foo.com" },
  });

  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have 1 DEFAULT_TOP_SITES now."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should add defaults on default.sites PREF_CHANGED");
  feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "default.sites", value: "https://foo.com" },
  });

  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have 1 DEFAULT_TOP_SITES now."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should refresh on topSiteRows PREF_CHANGED");
  let refreshStub = sandbox.stub(feed, "refresh");
  feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "topSitesRows" },
  });
  Assert.ok(feed.refresh.calledOnce, "refresh called");
  refreshStub.restore();

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should have default sites with .isDefault = true");
  feed.refreshDefaults("https://foo.com");
  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have a DEFAULT_TOP_SITES now."
  );
  Assert.ok(
    DEFAULT_TOP_SITES[0].isDefault,
    "Lone top site should be the default."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should have default sites with appropriate hostname");
  feed.refreshDefaults("https://foo.com");
  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have a DEFAULT_TOP_SITES now."
  );
  let [site] = DEFAULT_TOP_SITES;
  Assert.equal(
    site.hostname,
    NewTabUtils.shortURL(site),
    "Lone top site should have the right hostname."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should add no defaults on empty pref");
  feed.refreshDefaults("");
  Assert.equal(
    DEFAULT_TOP_SITES.length,
    0,
    "Should have 0 DEFAULT_TOP_SITES now."
  );

  info("refreshDefaults should be able to clear defaults");
  feed.refreshDefaults("https://foo.com");
  feed.refreshDefaults("");

  Assert.equal(
    DEFAULT_TOP_SITES.length,
    0,
    "Should have 0 DEFAULT_TOP_SITES now."
  );

  sandbox.restore();
});

add_task(async function test_filterForThumbnailExpiration() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);

  info(
    "filterForThumbnailExpiration should pass rows.urls to the callback provided"
  );
  const rows = [
    { url: "foo.com" },
    { url: "bar.com", customScreenshotURL: "custom" },
  ];
  feed.store.state.TopSites = { rows };
  const stub = sandbox.stub();
  feed.filterForThumbnailExpiration(stub);
  Assert.ok(stub.calledOnce);
  Assert.ok(stub.calledWithExactly(["foo.com", "bar.com", "custom"]));

  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_on_SearchService_init_failure() {
    let sandbox = sinon.createSandbox();
    let feed = getTopSitesFeedForTest(sandbox);

    feed.refreshDefaults("https://foo.com");

    gSearchServiceInitStub.rejects(new Error("Simulating search init failure"));

    const result = await feed.getLinksWithDefaults();
    Assert.ok(result);

    gSearchServiceInitStub.resolves();

    sandbox.restore();
  }
);

add_task(async function test_getLinksWithDefaults() {
  NewTabUtils.activityStreamLinks.getTopSites.resetHistory();

  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);

  feed.refreshDefaults("https://foo.com");

  info("getLinksWithDefaults should get the links from NewTabUtils");
  let result = await feed.getLinksWithDefaults();

  const reference = FAKE_LINKS.map(site =>
    Object.assign({}, site, {
      hostname: NewTabUtils.shortURL(site),
      typedBonus: true,
    })
  );

  Assert.deepEqual(result, reference);
  Assert.ok(NewTabUtils.activityStreamLinks.getTopSites.calledOnce);

  info("getLinksWithDefaults should indicate the links get typed bonus");
  Assert.ok(result[0].typedBonus, "Expected typed bonus property to be true.");

  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_ranks_with_local_pref_fallback() {
    let sandbox = sinon.createSandbox();
    let feed = getTopSitesFeedForTest(sandbox);

    feed.store.state.Prefs.values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION] = true;
    sandbox.stub(feed.ranker, "rankTopSites").callsFake(async sites => sites);

    await feed.getLinksWithDefaults();

    Assert.ok(
      feed.ranker.rankTopSites.calledOnce,
      "local pref triggers ranking without trainhop config"
    );

    sandbox.restore();
  }
);

add_task(async function test_getLinksWithDefaults_remote_false_skips_ranking() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);

  feed.store.state.Prefs.values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION] = true;
  feed.store.state.Prefs.values.trainhopConfig = {
    smartShortcuts: { enabled: false },
  };
  sandbox.stub(feed.ranker, "rankTopSites").callsFake(async sites => sites);

  await feed.getLinksWithDefaults();

  Assert.ok(
    feed.ranker.rankTopSites.notCalled,
    "explicit remote false skips ranking"
  );

  sandbox.restore();
});

add_task(async function test_ranker_receives_refresh_reasons() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;
  feed.store.state.Prefs.values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION] = true;
  sandbox.stub(feed._contile, "periodicUpdate");
  sandbox.stub(feed, "updateFrecencyBoostedSpocs").resolves();
  let rank = sandbox
    .stub(feed.ranker, "rankTopSites")
    .callsFake(async sites => sites);

  try {
    for (let reason of [
      actionTypes.SYSTEM_TICK,
      actionTypes.DISCOVERY_STREAM_DEV_SYSTEM_TICK,
      actionTypes.DISCOVERY_STREAM_DEV_EXPIRE_CACHE,
    ]) {
      rank.resetHistory();
      feed.onAction({ type: reason });
      await feed._latestRefreshPromise;

      Assert.ok(rank.calledOnce, `${reason} refresh reaches the ranker`);
      Assert.equal(
        rank.firstCall.args[2].reason,
        reason,
        "the original action type reaches the rerank gate"
      );
    }
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_sponsored_refresh_reason_reaches_ranker() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;
  feed.store.state.Prefs.values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION] = true;
  sandbox.stub(feed._contile, "_fetchSites").resolves(true);
  sandbox.stub(feed, "allocatePositions").resolves();
  let rank = sandbox
    .stub(feed.ranker, "rankTopSites")
    .callsFake(async sites => sites);
  let remotePref = "browser.topsites.useRemoteSetting";
  let hadRemotePref = Services.prefs.prefHasUserValue(remotePref);
  let originalRemotePref = Services.prefs.getBoolPref(remotePref);
  let originalDefaults = [...DEFAULT_TOP_SITES];

  try {
    for (let useRemoteSettings of [false, true]) {
      Services.prefs.setBoolPref(remotePref, useRemoteSettings);
      rank.resetHistory();
      await feed._contile.refresh();
      await feed._latestRefreshPromise;

      Assert.ok(
        rank.calledOnce,
        `sponsored refresh reaches the ranker with remote settings ${useRemoteSettings}`
      );
      Assert.equal(
        rank.firstCall.args[2].reason,
        "SPONSORED_TILES_UPDATED",
        "the sponsored reason survives default-site loading"
      );
    }
  } finally {
    if (hadRemotePref) {
      Services.prefs.setBoolPref(remotePref, originalRemotePref);
    } else {
      Services.prefs.clearUserPref(remotePref);
    }
    DEFAULT_TOP_SITES.splice(0, DEFAULT_TOP_SITES.length, ...originalDefaults);
    sandbox.restore();
  }
});

add_task(async function test_ranker_visible_slots_exclude_pins_and_sponsored() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed.store.state.Prefs.values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION] = true;
  let rank = sandbox
    .stub(feed.ranker, "rankTopSites")
    .callsFake(async sites => sites);
  let pins = [];
  let sponsored = [];
  sandbox.stub(feed.pinnedCache, "request").callsFake(async () => pins);
  sandbox.stub(feed, "_mergeSponsoredLinks").callsFake(() => sponsored);
  sandbox.stub(feed, "_maybeCapSponsoredLinks");
  let slots = 2 * TOP_SITES_MAX_SITES_PER_ROW;
  let pin = { url: "https://pinned.example.com", favicon: FAKE_FAVICON };
  let sparsePins = Array(slots + 2).fill(null);
  sparsePins[0] = pin;
  sparsePins[slots - 1] = { ...pin, url: "https://last-visible.example.com" };
  sparsePins[slots] = { ...pin, url: "https://offscreen.example.com" };

  try {
    for (let testCase of [
      { pins: [], expected: slots, description: "no pins" },
      { pins: sparsePins, expected: slots - 2, description: "sparse pins" },
      {
        pins: Array.from({ length: slots }, (_, i) => ({
          ...pin,
          url: `https://pinned${i}.example.com`,
        })),
        expected: 0,
        description: "all slots pinned",
      },
    ]) {
      pins = testCase.pins;
      for (let sponsoredCount of [0, 2]) {
        sponsored = Array.from({ length: sponsoredCount }, (_, i) => ({
          url: `https://sponsor${i}.example.com`,
          sponsored_position: i + 1,
        }));
        rank.resetHistory();
        await feed.getLinksWithDefaults();

        Assert.ok(rank.calledOnce, "ranking is requested");
        Assert.equal(
          rank.firstCall.args[2].numItems,
          Math.max(0, testCase.expected - sponsoredCount),
          `${testCase.description}, ${sponsoredCount} sponsored tiles: visible pins and sponsored tiles reduce the frozen prefix`
        );
      }
    }
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_ranker_visible_slots_count_sponsored_positions() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed.store.state.Prefs.values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION] = true;
  let rank = sandbox
    .stub(feed.ranker, "rankTopSites")
    .callsFake(async sites => sites);
  let sponsored = [];
  sandbox.stub(feed.pinnedCache, "request").resolves([]);
  sandbox.stub(feed, "_mergeSponsoredLinks").callsFake(() => sponsored);
  sandbox.stub(feed, "_maybeCapSponsoredLinks");
  let slots = 2 * TOP_SITES_MAX_SITES_PER_ROW;
  let tile = position => ({
    url: `https://sponsor-${position}-${Math.random()}.example.com`,
    sponsored_position: position,
  });

  try {
    for (let testCase of [
      {
        sponsored: [tile(1), tile(1), tile(3)],
        expected: slots - 2,
        description: "tiles sharing a position take one slot",
      },
      {
        sponsored: [tile(2), tile(slots + 1)],
        expected: slots - 1,
        description: "tiles past the last slot take none",
      },
    ]) {
      sponsored = testCase.sponsored;
      rank.resetHistory();
      await feed.getLinksWithDefaults();

      Assert.equal(
        rank.firstCall.args[2].numItems,
        testCase.expected,
        testCase.description
      );
    }
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_refresh_discards_stale_results() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;

  let resolveFirst;
  let resolveSecond;
  let firstPromise = new Promise(resolve => {
    resolveFirst = resolve;
  });
  let secondPromise = new Promise(resolve => {
    resolveSecond = resolve;
  });

  sandbox
    .stub(feed, "getLinksWithDefaults")
    .onFirstCall()
    .returns(firstPromise)
    .onSecondCall()
    .returns(secondPromise);

  let firstRefresh = feed.refresh({ broadcast: true });
  let secondRefresh = feed.refresh({ broadcast: true });

  resolveSecond([{ url: "https://second.example" }]);
  await secondRefresh;

  Assert.equal(feed.store.dispatch.callCount, 1, "newest refresh dispatched");
  Assert.deepEqual(
    feed.store.dispatch.firstCall.args[0].data.links,
    [{ url: "https://second.example" }],
    "the newest links win"
  );

  resolveFirst([{ url: "https://first.example" }]);
  await firstRefresh;

  Assert.equal(feed.store.dispatch.callCount, 1, "stale refresh was dropped");

  sandbox.restore();
});

add_task(async function test_refresh_keeps_broadcast_when_superseded() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;

  let resolveFirst;
  let firstPromise = new Promise(resolve => {
    resolveFirst = resolve;
  });

  sandbox
    .stub(feed, "getLinksWithDefaults")
    .onFirstCall()
    .returns(firstPromise)
    .onSecondCall()
    .resolves([{ url: "https://second.example" }]);

  let firstRefresh = feed.refresh({ broadcast: true });
  let secondRefresh = feed.refresh({ broadcast: false });

  await secondRefresh;
  resolveFirst([{ url: "https://first.example" }]);
  await firstRefresh;

  Assert.equal(
    feed.store.dispatch.callCount,
    1,
    "only the newest refresh dispatched"
  );
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.BroadcastToContent({
        type: actionTypes.TOP_SITES_UPDATED,
        data: { links: [{ url: "https://second.example" }] },
      })
    ),
    "the superseding refresh broadcasts for the one it discarded"
  );

  feed.getLinksWithDefaults.onThirdCall().resolves([]);
  await feed.refresh({ broadcast: false });

  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.AlsoToPreloaded({
        type: actionTypes.TOP_SITES_UPDATED,
        data: { links: [] },
      })
    ),
    "a later refresh no longer broadcasts"
  );

  sandbox.restore();
});

add_task(async function test_refresh_discards_results_after_uninit() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NimbusFeatures.newtab, "onUpdate");
  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_readDefaults");
  sandbox.stub(feed._contile, "refresh");
  feed.init();
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;

  let resolveLinks;
  let linksPromise = new Promise(resolve => {
    resolveLinks = resolve;
  });
  sandbox.stub(feed, "getLinksWithDefaults").returns(linksPromise);

  let refreshPromise = feed.refresh({ broadcast: true });

  feed.uninit();
  resolveLinks([{ url: "https://stale.example" }]);
  await refreshPromise;

  Assert.ok(
    feed.store.dispatch.notCalled,
    "a refresh that was in flight during uninit() does not dispatch"
  );

  feed.getLinksWithDefaults.resetHistory();
  await feed.refresh({ broadcast: true });

  Assert.ok(
    feed.getLinksWithDefaults.notCalled,
    "a refresh started after uninit() does no work"
  );
  Assert.ok(feed.store.dispatch.notCalled, "and does not dispatch");

  sandbox.restore();
});

/**
 * Stubs getLinksWithDefaults so each call proposes snapshot `{ id: name }`
 * right away and resolves with that call's links only when the test says so.
 *
 * @param {object} sandbox
 * @param {object} feed
 * @param {string[]} names One per expected refresh, in call order.
 * @returns {object} name -> function resolving that refresh's links.
 */
function stubDeferredRankedLinks(sandbox, feed, names) {
  const finish = {};
  const stub = sandbox.stub(feed, "getLinksWithDefaults");
  names.forEach((name, i) => {
    const { promise, resolve } = Promise.withResolvers();
    finish[name] = () => resolve([{ url: `https://${name}.example` }]);
    stub
      .onCall(i)
      .callsFake((isStartup, refreshId, reason, onRankedSnapshot) => {
        onRankedSnapshot({ id: name });
        return promise;
      });
  });
  return finish;
}

add_task(async function test_refresh_commits_only_newest_snapshot() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;
  sandbox.stub(feed.ranker, "commitSnapshot").resolves();
  const finish = stubDeferredRankedLinks(sandbox, feed, ["a", "b"]);

  let refreshA = feed.refresh({ broadcast: false });
  let refreshB = feed.refresh({ broadcast: false });

  finish.b();
  await refreshB;
  finish.a();
  await refreshA;

  Assert.equal(feed.store.dispatch.callCount, 1, "only one refresh dispatched");
  Assert.deepEqual(
    feed.store.dispatch.firstCall.args[0].data.links,
    [{ url: "https://b.example" }],
    "the newest refresh dispatched its links"
  );
  Assert.ok(
    feed.ranker.commitSnapshot.calledOnceWithExactly({ id: "b" }),
    "only the newest refresh's snapshot is committed"
  );

  sandbox.restore();
});

add_task(
  async function test_refresh_superseded_first_finisher_does_not_commit() {
    let sandbox = sinon.createSandbox();
    let feed = getTopSitesFeedForTest(sandbox);
    feed._startedUp = true;
    feed._tippyTopProvider.initialized = true;
    sandbox.stub(feed.ranker, "commitSnapshot").resolves();
    const finish = stubDeferredRankedLinks(sandbox, feed, ["a", "b"]);

    let refreshA = feed.refresh({ broadcast: false });
    let refreshB = feed.refresh({ broadcast: false });

    finish.a();
    await refreshA;

    Assert.ok(
      feed.store.dispatch.notCalled,
      "a refresh superseded before it finished does not dispatch"
    );
    Assert.ok(
      feed.ranker.commitSnapshot.notCalled,
      "and does not commit its snapshot"
    );

    finish.b();
    await refreshB;

    Assert.equal(
      feed.store.dispatch.callCount,
      1,
      "the newest refresh dispatched"
    );
    Assert.ok(
      feed.ranker.commitSnapshot.calledOnceWithExactly({ id: "b" }),
      "the newest refresh's snapshot is committed"
    );

    sandbox.restore();
  }
);

add_task(async function test_refresh_does_not_commit_after_uninit() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NimbusFeatures.newtab, "onUpdate");
  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_readDefaults");
  sandbox.stub(feed._contile, "refresh");
  feed.init();
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;
  sandbox.stub(feed.ranker, "commitSnapshot").resolves();
  const finish = stubDeferredRankedLinks(sandbox, feed, ["stale"]);

  let refreshPromise = feed.refresh({ broadcast: true });

  feed.uninit();
  finish.stale();
  await refreshPromise;

  Assert.ok(
    feed.store.dispatch.notCalled,
    "the pending refresh does not dispatch"
  );
  Assert.ok(
    feed.ranker.commitSnapshot.notCalled,
    "a snapshot proposed before uninit() is not committed"
  );

  sandbox.restore();
});

add_task(async function test_refresh_passes_isStartup_as_boolean() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NimbusFeatures.newtab, "onUpdate");
  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_readDefaults");
  sandbox.stub(feed._contile, "refresh");
  feed.init();
  feed._startedUp = true;
  feed._tippyTopProvider.initialized = true;
  sandbox.stub(feed, "getLinksWithDefaults").resolves([]);

  await feed.refresh({ broadcast: true, isStartup: false });

  Assert.strictEqual(
    feed.getLinksWithDefaults.firstCall.args[0],
    false,
    "getLinksWithDefaults gets the isStartup boolean, not an object that is always truthy"
  );

  feed.uninit();
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_filterAdult() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should filter out non-pinned adult sites");

  sandbox.stub(FilterAdult, "filter").returns([]);
  const TEST_URL = "https://foo.com/";
  sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [{ url: TEST_URL }]);

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  const result = await feed.getLinksWithDefaults();
  Assert.ok(FilterAdult.filter.calledOnce);
  Assert.equal(result.length, 1);
  Assert.equal(result[0].url, TEST_URL);

  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_caching() {
  let sandbox = sinon.createSandbox();

  info(
    "getLinksWithDefaults should filter out the defaults that have been blocked"
  );
  // make sure we only have one top site, and we block the only default site we have to show
  const url = "www.myonlytopsite.com";
  const topsite = {
    frecency: FAKE_FRECENCY,
    hostname: NewTabUtils.shortURL({ url }),
    typedBonus: true,
    url,
  };

  const blockedDefaultSite = { url: "https://foo.com" };
  gGetTopSitesStub.resolves([topsite]);
  sandbox.stub(NewTabUtils.blockedLinks, "isBlocked").callsFake(site => {
    return site.url === blockedDefaultSite.url;
  });

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");
  const result = await feed.getLinksWithDefaults();

  // what we should be left with is just the top site we added, and not the default site we blocked
  Assert.equal(result.length, 1);
  Assert.deepEqual(result[0], topsite);
  let foundBlocked = result.find(site => site.url === blockedDefaultSite.url);
  Assert.ok(!foundBlocked, "Should not have found blocked site.");

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_dedupe() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should call dedupe.group on the links");
  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  let stub = sandbox.stub(feed.dedupe, "group").callsFake((...id) => id);
  await feed.getLinksWithDefaults();

  Assert.ok(stub.calledOnce, "dedupe.group was called once");
  sandbox.restore();
});

add_task(async function test__dedupe_key() {
  let sandbox = sinon.createSandbox();

  info("_dedupeKey should dedupe on hostname instead of url");
  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  let site = { url: "foo", hostname: "bar" };
  let result = feed._dedupeKey(site);

  Assert.equal(result, site.hostname, "deduped on hostname");
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_adds_defaults() {
  let sandbox = sinon.createSandbox();

  info(
    "getLinksWithDefaults should add defaults if there are are not enough links"
  );
  const TEST_LINKS = [{ frecency: FAKE_FRECENCY, url: "foo.com" }];
  gGetTopSitesStub.resolves(TEST_LINKS);
  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  let result = await feed.getLinksWithDefaults();

  let reference = [...TEST_LINKS, ...DEFAULT_TOP_SITES].map(s =>
    Object.assign({}, s, {
      hostname: NewTabUtils.shortURL(s),
      typedBonus: true,
    })
  );

  Assert.deepEqual(result, reference);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_adds_defaults_for_visible_slots() {
    let sandbox = sinon.createSandbox();

    info(
      "getLinksWithDefaults should only add defaults up to the number of visible slots"
    );
    const numVisible = TOP_SITES_DEFAULT_ROWS * TOP_SITES_MAX_SITES_PER_ROW;
    let testLinks = [];
    for (let i = 0; i < numVisible - 1; i++) {
      testLinks.push({ frecency: FAKE_FRECENCY, url: `foo${i}.com` });
    }
    gGetTopSitesStub.resolves(testLinks);

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");

    let result = await feed.getLinksWithDefaults();

    let reference = [...testLinks, DEFAULT_TOP_SITES[0]].map(s =>
      Object.assign({}, s, {
        hostname: NewTabUtils.shortURL(s),
        typedBonus: true,
      })
    );

    Assert.equal(result.length, numVisible);
    Assert.deepEqual(result, reference);

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }
);

add_task(async function test_getLinksWithDefaults_no_throw_on_no_links() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should not throw if NewTabUtils returns null");
  gGetTopSitesStub.resolves(null);

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  feed.getLinksWithDefaults();
  Assert.ok(true, "getLinksWithDefaults did not throw");

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_get_more_on_request() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should get more if the user has asked for more");
  let testLinks = new Array(4 * TOP_SITES_MAX_SITES_PER_ROW)
    .fill(null)
    .map((v, i) => ({
      frecency: FAKE_FRECENCY,
      url: `http://www.site${i}.com`,
    }));
  gGetTopSitesStub.resolves(testLinks);

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  const TEST_ROWS = 3;
  feed.store.state.Prefs.values.topSitesRows = TEST_ROWS;

  let result = await feed.getLinksWithDefaults();
  Assert.equal(result.length, TEST_ROWS * TOP_SITES_MAX_SITES_PER_ROW);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_reuse_cache() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should reuse the cache on subsequent calls");

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  gGetTopSitesStub.resetHistory();

  await feed.getLinksWithDefaults();
  await feed.getLinksWithDefaults();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getTopSites.calledOnce,
    "getTopSites only called once"
  );

  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_ignore_cache_on_requesting_more() {
    let sandbox = sinon.createSandbox();
    info("getLinksWithDefaults should ignore the cache when requesting more");

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");

    gGetTopSitesStub.resetHistory();

    await feed.getLinksWithDefaults();
    feed.store.state.Prefs.values.topSitesRows *= 100;
    await feed.getLinksWithDefaults();

    Assert.ok(
      NewTabUtils.activityStreamLinks.getTopSites.calledTwice,
      "getTopSites called twice"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_getLinksWithDefaults_migrate_frecent_screenshot_data() {
    let sandbox = sinon.createSandbox();
    info(
      "getLinksWithDefaults should migrate frecent screenshot data without getting screenshots again"
    );

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");

    gGetTopSitesStub.resetHistory();

    feed.store.state.Prefs.values[SHOWN_ON_NEWTAB_PREF] = true;
    await feed.getLinksWithDefaults();

    let originalCallCount = Screenshots.getScreenshotForURL.callCount;
    feed.frecentCache.expire();

    let result = await feed.getLinksWithDefaults();

    Assert.ok(
      NewTabUtils.activityStreamLinks.getTopSites.calledTwice,
      "getTopSites called twice"
    );
    Assert.equal(
      Screenshots.getScreenshotForURL.callCount,
      originalCallCount,
      "getScreenshotForURL was not called again."
    );
    Assert.equal(result[0].screenshot, FAKE_SCREENSHOT);

    sandbox.restore();
  }
);

add_task(
  async function test_getLinksWithDefaults_migrate_pinned_favicon_data() {
    let sandbox = sinon.createSandbox();
    info(
      "getLinksWithDefaults should migrate pinned favicon data without getting favicons again"
    );

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");

    gGetTopSitesStub.resetHistory();

    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "https://foo.com/" }]);

    await feed.getLinksWithDefaults();

    let originalCallCount =
      NewTabUtils.activityStreamProvider._addFavicons.callCount;
    feed.pinnedCache.expire();

    let result = await feed.getLinksWithDefaults();

    Assert.equal(
      NewTabUtils.activityStreamProvider._addFavicons.callCount,
      originalCallCount,
      "_addFavicons was not called again."
    );
    Assert.equal(result[0].favicon, FAKE_FAVICON);
    Assert.equal(result[0].faviconSize, FAKE_FAVICON_SIZE);

    sandbox.restore();
  }
);

add_task(async function test_getLinksWithDefaults_no_internal_properties() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should not expose internal link properties");

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  let result = await feed.getLinksWithDefaults();

  let internal = Object.keys(result[0]).filter(key => key.startsWith("__"));
  Assert.equal(internal.join(""), "");

  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_copy_frecent_screenshot() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should copy the screenshot of the frecent site if " +
      "pinned site doesn't have customScreenshotURL"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  const TEST_SCREENSHOT = "screenshot";

  gGetTopSitesStub.resolves([
    { url: "https://foo.com/", screenshot: TEST_SCREENSHOT },
  ]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com/" }]);

  let result = await feed.getLinksWithDefaults();

  Assert.equal(result[0].screenshot, TEST_SCREENSHOT);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_no_copy_frecent_screenshot() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should not copy the frecent screenshot if " +
      "customScreenshotURL is set"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  gGetTopSitesStub.resolves([
    { url: "https://foo.com/", screenshot: "screenshot" },
  ]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com/", customScreenshotURL: "custom" }]);

  let result = await feed.getLinksWithDefaults();

  Assert.equal(result[0].screenshot, undefined);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_persist_screenshot() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should keep the same screenshot if no frecent site is found"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  const CUSTOM_SCREENSHOT = "custom";

  gGetTopSitesStub.resolves([]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com/", screenshot: CUSTOM_SCREENSHOT }]);

  let result = await feed.getLinksWithDefaults();

  Assert.equal(result[0].screenshot, CUSTOM_SCREENSHOT);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_no_overwrite_pinned_screenshot() {
    let sandbox = sinon.createSandbox();
    info("getLinksWithDefaults should not overwrite pinned site screenshot");

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");

    const EXISTING_SCREENSHOT = "some-screenshot";

    gGetTopSitesStub.resolves([{ url: "https://foo.com/", screenshot: "foo" }]);
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [
        { url: "https://foo.com/", screenshot: EXISTING_SCREENSHOT },
      ]);

    let result = await feed.getLinksWithDefaults();

    Assert.equal(result[0].screenshot, EXISTING_SCREENSHOT);

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }
);

add_task(
  async function test_getLinksWithDefaults_no_searchTopSite_from_frecent() {
    let sandbox = sinon.createSandbox();
    info("getLinksWithDefaults should not set searchTopSite from frecent site");

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");

    const EXISTING_SCREENSHOT = "some-screenshot";

    gGetTopSitesStub.resolves([
      {
        url: "https://foo.com/",
        searchTopSite: true,
        screenshot: EXISTING_SCREENSHOT,
      },
    ]);
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "https://foo.com/" }]);

    let result = await feed.getLinksWithDefaults();

    Assert.ok(!result[0].searchTopSite);
    // But it should copy over other properties
    Assert.equal(result[0].screenshot, EXISTING_SCREENSHOT);

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }
);

add_task(async function test_getLinksWithDefaults_concurrency_getTopSites() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults concurrent calls should call the backing data once"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  NewTabUtils.activityStreamLinks.getTopSites.resetHistory();

  await Promise.all([feed.getLinksWithDefaults(), feed.getLinksWithDefaults()]);

  Assert.ok(
    NewTabUtils.activityStreamLinks.getTopSites.calledOnce,
    "getTopSites only called once"
  );

  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_concurrency_getScreenshotForURL() {
    let sandbox = sinon.createSandbox();
    info(
      "getLinksWithDefaults concurrent calls should call the backing data once"
    );

    let feed = getTopSitesFeedForTest(sandbox);
    feed.refreshDefaults("https://foo.com");
    feed.store.state.Prefs.values[SHOWN_ON_NEWTAB_PREF] = true;

    NewTabUtils.activityStreamLinks.getTopSites.resetHistory();
    Screenshots.getScreenshotForURL.resetHistory();

    await Promise.all([
      feed.getLinksWithDefaults(),
      feed.getLinksWithDefaults(),
    ]);

    Assert.ok(
      NewTabUtils.activityStreamLinks.getTopSites.calledOnce,
      "getTopSites only called once"
    );

    Assert.equal(
      Screenshots.getScreenshotForURL.callCount,
      FAKE_LINKS.length,
      "getLinksWithDefaults concurrent calls should get screenshots once per link"
    );

    sandbox.restore();
    feed = getTopSitesFeedForTest(sandbox);
    feed.store.state.Prefs.values[SHOWN_ON_NEWTAB_PREF] = true;

    feed.refreshDefaults("https://foo.com");

    sandbox.stub(feed, "_requestRichIcon");
    await Promise.all([
      feed.getLinksWithDefaults(),
      feed.getLinksWithDefaults(),
    ]);

    Assert.equal(
      feed.store.dispatch.callCount,
      FAKE_LINKS.length,
      "getLinksWithDefaults concurrent calls should dispatch once per link screenshot fetched"
    );

    sandbox.restore();
  }
);

add_task(async function test_getLinksWithDefaults_deduping_no_dedupe_pinned() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should not dedupe pinned sites");

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults("https://foo.com");

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      { url: "https://developer.mozilla.org/en-US/docs/Web" },
      { url: "https://developer.mozilla.org/en-US/docs/Learn" },
    ]);

  let sites = await feed.getLinksWithDefaults();
  Assert.equal(sites.length, 2 * TOP_SITES_MAX_SITES_PER_ROW);
  Assert.equal(sites[0].url, NewTabUtils.pinnedLinks.links[0].url);
  Assert.equal(sites[1].url, NewTabUtils.pinnedLinks.links[1].url);
  Assert.equal(sites[0].hostname, sites[1].hostname);

  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_prefer_pinned_sites() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should prefer pinned sites over links");

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults();

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      { url: "https://developer.mozilla.org/en-US/docs/Web" },
      { url: "https://developer.mozilla.org/en-US/docs/Learn" },
    ]);

  const SECOND_TOP_SITE_URL = "https://www.mozilla.org/";

  gGetTopSitesStub.resolves([
    { frecency: FAKE_FRECENCY, url: "https://developer.mozilla.org/" },
    { frecency: FAKE_FRECENCY, url: SECOND_TOP_SITE_URL },
  ]);

  let sites = await feed.getLinksWithDefaults();

  // Expecting 3 links where there's 2 pinned and 1 www.mozilla.org, so
  // the frecent with matching hostname as pinned is removed.
  Assert.equal(sites.length, 3);
  Assert.equal(sites[0].url, NewTabUtils.pinnedLinks.links[0].url);
  Assert.equal(sites[1].url, NewTabUtils.pinnedLinks.links[1].url);
  Assert.equal(sites[2].url, SECOND_TOP_SITE_URL);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_title_and_null() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should return sites that have a title");

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults();

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://github.com/mozilla/activity-stream" }]);

  let sites = await feed.getLinksWithDefaults();
  for (let site of sites) {
    Assert.ok(site.hostname);
  }

  info("getLinksWithDefaults should not throw for null entries");
  sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [null]);
  await feed.getLinksWithDefaults();
  Assert.ok(true, "getLinksWithDefaults didn't throw");

  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_calls__fetchIcon() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should return sites that have a title");

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults();

  sandbox.spy(feed, "_fetchIcon");
  let results = await feed.getLinksWithDefaults();
  Assert.ok(results.length, "Got back some results");
  Assert.equal(feed._fetchIcon.callCount, results.length);
  for (let result of results) {
    Assert.ok(feed._fetchIcon.calledWith(result));
  }

  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_calls__fetchScreenshot() {
  let sandbox = sinon.createSandbox();

  info(
    "getLinksWithDefaults should call _fetchScreenshot when customScreenshotURL is set"
  );

  gGetTopSitesStub.resolves([]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com", customScreenshotURL: "custom" }]);

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults();

  sandbox.stub(feed, "_fetchScreenshot");
  await feed.getLinksWithDefaults();

  Assert.ok(feed._fetchScreenshot.calledWith(sinon.match.object, "custom"));

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_getLinksWithDefaults_with_DiscoveryStream() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should add a sponsored topsite from discoverystream to all the valid indices"
  );

  let makeStreamData = index => ({
    layout: [
      {
        components: [
          {
            placement: {
              name: "sponsored-topsites",
            },
            spocs: {
              positions: [{ index }],
            },
          },
        ],
      },
    ],
    spocs: {
      data: {
        "sponsored-topsites": {
          items: [{ title: "test spoc", url: "https://test-spoc.com" }],
        },
      },
    },
  });

  let feed = getTopSitesFeedForTest(sandbox);
  feed.refreshDefaults();

  for (let i = 0; i < FAKE_LINKS.length; i++) {
    feed.store.state.DiscoveryStream = makeStreamData(i);
    const result = await feed.getLinksWithDefaults();
    const link = result[i];

    Assert.equal(link.type, "SPOC");
    Assert.equal(link.title, "test spoc");
    Assert.equal(link.sponsored_position, i + 1);
    Assert.equal(link.hostname, "test-spoc");
    Assert.equal(link.url, "https://test-spoc.com");
  }

  sandbox.restore();
});

add_task(async function test_init() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(NimbusFeatures.newtab, "onUpdate");

  let feed = getTopSitesFeedForTest(sandbox);

  sandbox.stub(feed, "refresh");
  await feed.init();

  info("TopSitesFeed.init should call refresh (broadcast: true)");
  Assert.ok(feed.refresh.calledOnce, "refresh called once");
  Assert.ok(
    feed.refresh.calledWithExactly({
      broadcast: true,
      isStartup: true,
      reason: undefined,
    })
  );

  info(
    "TopSitesFeed.init should call onUpdate to set up Nimbus update listener"
  );

  Assert.ok(
    NimbusFeatures.newtab.onUpdate.calledOnce,
    "NimbusFeatures.newtab.onUpdate called once"
  );
  sandbox.restore();
});

add_task(async function test_refresh() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(NimbusFeatures.newtab, "onUpdate");

  let feed = getTopSitesFeedForTest(sandbox);

  sandbox.stub(feed, "_fetchIcon");
  feed._startedUp = true;

  info("TopSitesFeed.refresh should wait for tippytop to initialize");
  feed._tippyTopProvider.initialized = false;
  sandbox.stub(feed._tippyTopProvider, "init").resolves();

  await feed.refresh();

  Assert.ok(
    feed._tippyTopProvider.init.calledOnce,
    "feed._tippyTopProvider.init called once"
  );

  info(
    "TopSitesFeed.refresh should not init the tippyTopProvider if already initialized"
  );
  feed._tippyTopProvider.initialized = true;
  feed._tippyTopProvider.init.resetHistory();

  await feed.refresh();

  Assert.ok(
    feed._tippyTopProvider.init.notCalled,
    "tippyTopProvider not initted again"
  );

  info("TopSitesFeed.refresh should broadcast TOP_SITES_UPDATED");
  feed.store.dispatch.resetHistory();
  sandbox.stub(feed, "getLinksWithDefaults").resolves([]);

  await feed.refresh({ broadcast: true });

  Assert.ok(feed.store.dispatch.calledOnce, "dispatch called once");
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.BroadcastToContent({
        type: actionTypes.TOP_SITES_UPDATED,
        data: { links: [] },
      })
    )
  );

  sandbox.restore();
});

add_task(async function test_refresh_dispatch() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.refresh should dispatch an action with the links returned"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_fetchIcon");
  feed._startedUp = true;

  await feed.refresh({ broadcast: true });
  let reference = FAKE_LINKS.map(site =>
    Object.assign({}, site, {
      hostname: NewTabUtils.shortURL(site),
      typedBonus: true,
    })
  );

  Assert.ok(feed.store.dispatch.calledOnce, "Store.dispatch called once");
  Assert.equal(
    feed.store.dispatch.firstCall.args[0].type,
    actionTypes.TOP_SITES_UPDATED
  );
  Assert.deepEqual(feed.store.dispatch.firstCall.args[0].data.links, reference);

  sandbox.restore();
});

add_task(async function test_refresh_empty_slots() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.refresh should handle empty slots in the resulting top sites array"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_fetchIcon");
  feed._startedUp = true;

  gGetTopSitesStub.resolves([FAKE_LINKS[0]]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      null,
      null,
      FAKE_LINKS[1],
      null,
      null,
      null,
      null,
      null,
      FAKE_LINKS[2],
    ]);

  await feed.refresh({ broadcast: true });

  Assert.ok(feed.store.dispatch.calledOnce, "Store.dispatch called once");

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_refresh_to_preloaded() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.refresh should dispatch AlsoToPreloaded when broadcast is false"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_fetchIcon");
  feed._startedUp = true;

  gGetTopSitesStub.resolves([]);
  await feed.refresh({ broadcast: false });

  Assert.ok(feed.store.dispatch.calledOnce, "Store.dispatch called once");
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.AlsoToPreloaded({
        type: actionTypes.TOP_SITES_UPDATED,
        data: { links: [] },
      })
    )
  );
  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(async function test_refresh_handles_indexedDB_errors() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.refresh should dispatch AlsoToPreloaded when broadcast is false"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_fetchIcon");
  feed._startedUp = true;

  try {
    await feed.refresh({ broadcast: false });
    Assert.ok(true, "refresh should have succeeded");
  } catch (e) {
    Assert.ok(false, "Should not have thrown");
  }

  sandbox.restore();
});

add_task(async function test_adEligiblePositions() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);

  let nimbusVariables = { topSitesContileEnabled: true };
  sandbox
    .stub(NimbusFeatures.pocketNewtab, "getVariable")
    .callsFake(name => nimbusVariables[name]);
  sandbox
    .stub(NimbusFeatures.newtab, "getVariable")
    .callsFake(name => nimbusVariables[name]);

  info("No position is ad-eligible while sponsored top sites are off");
  Assert.deepEqual(feed._adEligiblePositions(), []);

  feed.store.state.Prefs.values[SHOW_SPONSORED_PREF] = true;

  info("No position is ad-eligible while Contile is disabled");
  nimbusVariables.topSitesContileEnabled = false;
  Assert.deepEqual(feed._adEligiblePositions(), []);
  nimbusVariables.topSitesContileEnabled = true;

  info("Contile fills the first two positions by default");
  Assert.deepEqual(feed._adEligiblePositions(), [1, 2]);

  info("The Nimbus variable is 0-based and replaces the default");
  nimbusVariables.contileTopsitesPositions = "0,2,3";
  Assert.deepEqual(feed._adEligiblePositions(), [1, 3, 4]);

  info("Positions past the display maximum can never be filled");
  nimbusVariables.topSitesMaxSponsored = 2;
  Assert.deepEqual(feed._adEligiblePositions(), [1, 3]);

  info("SOV allocations replace the Contile positions once SOV is ready");
  sandbox
    .stub(feed._contile, "sov")
    .get(() => ({ name: "SOV-20230518215316" }));
  feed.store.state.TopSites.sov = {
    ready: true,
    positions: [
      { position: 1, assignedPartner: "amp" },
      { position: 2, assignedPartner: "amp" },
      { position: 3, assignedPartner: "frec-boost" },
    ],
  };
  Assert.deepEqual(feed._adEligiblePositions(), [1, 2]);

  info("Contile positions are used until SOV is ready");
  feed.store.state.TopSites.sov.ready = false;
  Assert.deepEqual(feed._adEligiblePositions(), [1, 3]);

  sandbox.restore();
});

add_task(
  async function test_getLinksWithDefaults_flags_ad_eligible_positions() {
    info(
      "getLinksWithDefaults should flag whichever tile lands in an " +
        "ad-eligible position"
    );

    let sandbox = sinon.createSandbox();
    let feed = getTopSitesFeedForTest(sandbox);
    sandbox.stub(feed, "_adEligiblePositions").returns([1, 3]);
    // getLinksWithDefaults writes to the link objects it is handed, so keep
    // the shared FAKE_LINKS clean for the tasks that deep-compare against it.
    gGetTopSitesStub.resolves(FAKE_LINKS.map(link => ({ ...link })));

    let result = await feed.getLinksWithDefaults();

    Assert.ok(
      result[0].is_ad_eligible_position,
      "1-based position 1 should be flagged"
    );
    Assert.ok(
      result[2].is_ad_eligible_position,
      "1-based position 3 should be flagged"
    );
    Assert.equal(
      result[1].is_ad_eligible_position,
      undefined,
      "A position no ad may fill should not be flagged"
    );

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }
);

add_task(
  async function test_getLinksWithDefaults_clears_stale_ad_eligible_flag() {
    info(
      "getLinksWithDefaults should clear the flag from a tile that moved out " +
        "of an ad-eligible position"
    );

    let sandbox = sinon.createSandbox();
    let feed = getTopSitesFeedForTest(sandbox);
    // Default tiles are the reused ones: getLinksWithDefaults puts the
    // DEFAULT_TOP_SITES entry itself into the row, not a copy, so a flag
    // written onto one outlives the refresh.
    feed.refreshDefaults("https://default.com");
    gGetTopSitesStub.resolves([]);
    sandbox.stub(feed, "_adEligiblePositions").returns([1]);

    let result = await feed.getLinksWithDefaults();
    Assert.ok(
      result[0].is_ad_eligible_position,
      "Should be flagged while sitting in position 1"
    );

    feed._adEligiblePositions.returns([2]);
    result = await feed.getLinksWithDefaults();
    Assert.equal(
      result[0].is_ad_eligible_position,
      undefined,
      "Should not still be flagged once position 1 is not eligible"
    );

    gGetTopSitesStub.resolves(FAKE_LINKS);
    feed.refreshDefaults(null);
    sandbox.restore();
  }
);

add_task(async function test_allocatePositions() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.allocationPositions should allocate positions and dispatch"
  );

  let feed = getTopSitesFeedForTest(sandbox);

  let sov = {
    name: "SOV-20230518215316",
    allocations: [
      {
        position: 1,
        allocation: [
          {
            partner: "amp",
            percentage: 100,
          },
          {
            partner: "moz-sales",
            percentage: 0,
          },
        ],
      },
      {
        position: 2,
        allocation: [
          {
            partner: "amp",
            percentage: 80,
          },
          {
            partner: "moz-sales",
            percentage: 20,
          },
        ],
      },
    ],
  };

  sandbox.stub(feed._contile, "sov").get(() => sov);

  sandbox.stub(Sampling, "ratioSample");
  Sampling.ratioSample.onCall(0).resolves(0);
  Sampling.ratioSample.onCall(1).resolves(1);

  await feed.allocatePositions();

  Assert.ok(feed.store.dispatch.calledOnce, "feed.store.dispatch called once");
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.OnlyToMain({
        type: actionTypes.SOV_UPDATED,
        data: {
          ready: true,
          positions: [
            { position: 1, assignedPartner: "amp" },
            { position: 2, assignedPartner: "moz-sales" },
          ],
        },
      })
    )
  );

  Sampling.ratioSample.onCall(2).resolves(0);
  Sampling.ratioSample.onCall(3).resolves(0);

  await feed.allocatePositions();

  Assert.ok(
    feed.store.dispatch.calledTwice,
    "feed.store.dispatch called twice"
  );
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.OnlyToMain({
        type: actionTypes.SOV_UPDATED,
        data: {
          ready: true,
          positions: [
            { position: 1, assignedPartner: "amp" },
            { position: 2, assignedPartner: "amp" },
          ],
        },
      })
    )
  );

  sandbox.restore();
});

add_task(async function test_getScreenshotPreview() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.getScreenshotPreview should dispatch preview if request is succesful"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  await feed.getScreenshotPreview("custom", 1234);

  Assert.ok(feed.store.dispatch.calledOnce);
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.OnlyToOneContent(
        {
          data: { preview: FAKE_SCREENSHOT, url: "custom" },
          type: actionTypes.PREVIEW_RESPONSE,
        },
        1234
      )
    )
  );

  sandbox.restore();
});

add_task(async function test_getScreenshotPreview() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.getScreenshotPreview should return empty string if request fails"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  Screenshots.getScreenshotForURL.resolves(Promise.resolve(null));
  await feed.getScreenshotPreview("custom", 1234);

  Assert.ok(feed.store.dispatch.calledOnce);
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.OnlyToOneContent(
        {
          data: { preview: "", url: "custom" },
          type: actionTypes.PREVIEW_RESPONSE,
        },
        1234
      )
    )
  );

  Screenshots.getScreenshotForURL.resolves(FAKE_SCREENSHOT);
  sandbox.restore();
});

add_task(async function test_onAction_part_1() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.onAction should call getScreenshotPreview on PREVIEW_REQUEST"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "getScreenshotPreview");

  feed.onAction({
    type: actionTypes.PREVIEW_REQUEST,
    data: { url: "foo" },
    meta: { fromTarget: 1234 },
  });

  Assert.ok(
    feed.getScreenshotPreview.calledOnce,
    "feed.getScreenshotPreview called once"
  );
  Assert.ok(feed.getScreenshotPreview.calledWithExactly("foo", 1234));

  info("TopSitesFeed.onAction should refresh on SYSTEM_TICK");
  sandbox.stub(feed, "refresh");
  feed.onAction({ type: actionTypes.SYSTEM_TICK });

  Assert.ok(feed.refresh.calledOnce, "feed.refresh called once");
  Assert.ok(
    feed.refresh.calledWithExactly({
      broadcast: false,
      reason: actionTypes.SYSTEM_TICK,
    })
  );

  info(
    "TopSitesFeed.onAction should call with correct parameters on TOP_SITES_PIN"
  );
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  sandbox.spy(feed, "pin");

  let pinAction = {
    type: actionTypes.TOP_SITES_PIN,
    data: { site: { url: "foo.com" }, index: 7 },
  };
  feed.onAction(pinAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledOnce,
    "NewTabUtils.pinnedLinks.pin called once"
  );
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledWithExactly(
      pinAction.data.site,
      pinAction.data.index
    )
  );
  Assert.ok(
    feed.pin.calledOnce,
    "TopSitesFeed.onAction should call pin on TOP_SITES_PIN"
  );

  info(
    "TopSitesFeed.onAction should unblock a previously blocked top site if " +
      "we are now adding it manually via 'Add a Top Site' option"
  );
  sandbox.stub(NewTabUtils.blockedLinks, "unblock");
  pinAction = {
    type: actionTypes.TOP_SITES_PIN,
    data: { site: { url: "foo.com" }, index: -1 },
  };
  feed.onAction(pinAction);
  Assert.ok(
    NewTabUtils.blockedLinks.unblock.calledWith({
      url: pinAction.data.site.url,
    })
  );

  info("TopSitesFeed.onAction should call insert on TOP_SITES_INSERT");
  sandbox.stub(feed, "insert");
  let addAction = {
    type: actionTypes.TOP_SITES_INSERT,
    data: { site: { url: "foo.com" } },
  };

  feed.onAction(addAction);
  Assert.ok(feed.insert.calledOnce, "TopSitesFeed.insert called once");

  info(
    "TopSitesFeed.onAction should call unpin with correct parameters " +
      "on TOP_SITES_UNPIN"
  );

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      null,
      null,
      { url: "foo.com" },
      null,
      null,
      null,
      null,
      null,
      FAKE_LINKS[0],
    ]);
  sandbox.stub(NewTabUtils.pinnedLinks, "unpin");

  let unpinAction = {
    type: actionTypes.TOP_SITES_UNPIN,
    data: { site: { url: "foo.com" } },
  };
  feed.onAction(unpinAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.unpin.calledOnce,
    "NewTabUtils.pinnedLinks.unpin called once"
  );
  Assert.ok(NewTabUtils.pinnedLinks.unpin.calledWith(unpinAction.data.site));

  sandbox.restore();
});

add_task(async function test_onAction_part_2() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSitesFeed.onAction should call refresh without a target if we clear " +
      "history with PLACES_HISTORY_CLEARED"
  );

  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "refresh");
  feed.onAction({ type: actionTypes.PLACES_HISTORY_CLEARED });

  Assert.ok(feed.refresh.calledOnce, "TopSitesFeed.refresh called once");
  Assert.ok(feed.refresh.calledWithExactly({ broadcast: true }));

  feed.refresh.resetHistory();

  info(
    "TopSitesFeed.onAction should call refresh without a target " +
      "if we remove a Topsite from history"
  );
  feed.onAction({ type: actionTypes.PLACES_LINKS_DELETED });

  Assert.ok(feed.refresh.calledOnce, "TopSitesFeed.refresh called once");
  Assert.ok(feed.refresh.calledWithExactly({ broadcast: true }));

  info("TopSitesFeed.onAction should call init on INIT action");
  feed.onAction({ type: actionTypes.PLACES_LINKS_DELETED });
  sandbox.stub(feed, "init");
  feed.onAction({ type: actionTypes.INIT });
  Assert.ok(feed.init.calledOnce, "TopSitesFeed.init called once");

  info(
    "TopSitesFeed.onAction should call refresh on PLACES_LINK_BLOCKED action"
  );
  feed.refresh.resetHistory();
  await feed.onAction({ type: actionTypes.PLACES_LINK_BLOCKED });
  Assert.ok(feed.refresh.calledOnce, "TopSitesFeed.refresh called once");
  Assert.ok(feed.refresh.calledWithExactly({ broadcast: true }));

  info(
    "TopSitesFeed.onAction should call refresh on PLACES_LINKS_CHANGED action"
  );
  feed.refresh.resetHistory();
  await feed.onAction({ type: actionTypes.PLACES_LINKS_CHANGED });
  Assert.ok(feed.refresh.calledOnce, "TopSitesFeed.refresh called once");
  Assert.ok(feed.refresh.calledWithExactly({ broadcast: false }));

  info(
    "TopSitesFeed.onAction should call pin with correct args on " +
      "TOP_SITES_INSERT without an index specified"
  );
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");

  let addAction = {
    type: actionTypes.TOP_SITES_INSERT,
    data: { site: { url: "foo.bar", label: "foo" } },
  };
  feed.onAction(addAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledOnce,
    "NewTabUtils.pinnedLinks.pin called once"
  );
  Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(addAction.data.site, 0));

  info(
    "TopSitesFeed.onAction should call pin with correct args on " +
      "TOP_SITES_INSERT"
  );
  NewTabUtils.pinnedLinks.pin.resetHistory();
  let dropAction = {
    type: actionTypes.TOP_SITES_INSERT,
    data: { site: { url: "foo.bar", label: "foo" }, index: 3 },
  };
  feed.onAction(dropAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledOnce,
    "NewTabUtils.pinnedLinks.pin called once"
  );
  Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(dropAction.data.site, 3));

  // feed.init needs to actually run in order to register the observers that'll
  // be removed in the following UNINIT test, otherwise uninit will throw.
  feed.init.restore();
  feed.init();

  info("TopSitesFeed.onAction should remove the expiration filter on UNINIT");
  sandbox.stub(PageThumbs, "removeExpirationFilter");
  feed.onAction({ type: "UNINIT" });
  Assert.ok(
    PageThumbs.removeExpirationFilter.calledOnce,
    "PageThumbs.removeExpirationFilter called once"
  );

  sandbox.restore();
});

add_task(async function test_onAction_part_3() {
  let sandbox = sinon.createSandbox();

  let feed = getTopSitesFeedForTest(sandbox);

  info(
    "TopSitesFeed.onAction should call updatePinnedSearchShortcuts " +
      "on UPDATE_PINNED_SEARCH_SHORTCUTS action"
  );
  sandbox.stub(feed, "updatePinnedSearchShortcuts");
  let addedShortcuts = [
    {
      url: "https://google.com",
      searchVendor: "google",
      label: "google",
      searchTopSite: true,
    },
  ];
  await feed.onAction({
    type: actionTypes.UPDATE_PINNED_SEARCH_SHORTCUTS,
    data: { addedShortcuts },
  });
  Assert.ok(
    feed.updatePinnedSearchShortcuts.calledOnce,
    "TopSitesFeed.updatePinnedSearchShortcuts called once"
  );

  info(
    "TopSitesFeed.onAction should refresh from Contile on " +
      "SHOW_SPONSORED_PREF if Contile is enabled"
  );
  sandbox.spy(feed._contile, "refresh");
  let prefChangeAction = {
    type: actionTypes.PREF_CHANGED,
    data: { name: SHOW_SPONSORED_PREF },
  };
  sandbox.stub(NimbusFeatures.newtab, "getVariable").returns(true);
  feed.onAction(prefChangeAction);

  Assert.ok(
    feed._contile.refresh.calledOnce,
    "TopSitesFeed._contile.refresh called once"
  );

  info(
    "TopSitesFeed.onAction should not refresh from Contile on " +
      "SHOW_SPONSORED_PREF if Contile is disabled"
  );
  NimbusFeatures.newtab.getVariable.returns(false);
  feed._contile.refresh.resetHistory();
  feed.onAction(prefChangeAction);

  Assert.ok(
    !feed._contile.refresh.calledOnce,
    "TopSitesFeed._contile.refresh never called"
  );

  info(
    "TopSitesFeed.onAction should reset Contile cache prefs " +
      "when SHOW_SPONSORED_PREF is false"
  );
  feed._contile.cache.get.returns({ contile: [] });
  Services.prefs.setIntPref(
    CONTILE_CACHE_LAST_FETCH_PREF,
    Math.round(Date.now() / 1000)
  );
  Services.prefs.setIntPref(CONTILE_CACHE_VALID_FOR_SECONDS_PREF, 15 * 60);
  prefChangeAction = {
    type: actionTypes.PREF_CHANGED,
    data: { name: SHOW_SPONSORED_PREF, value: false },
  };
  NimbusFeatures.newtab.getVariable.returns(true);
  feed._contile.refresh.resetHistory();

  feed.onAction(prefChangeAction);
  Assert.ok(feed._contile.cache.set.calledWith("contile", []));
  Assert.ok(!Services.prefs.prefHasUserValue(CONTILE_CACHE_LAST_FETCH_PREF));
  Assert.ok(
    !Services.prefs.prefHasUserValue(CONTILE_CACHE_VALID_FOR_SECONDS_PREF)
  );

  sandbox.restore();
});

add_task(async function test_sponsored_scoring_follows_prefs() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NimbusFeatures.newtab, "onUpdate");
  let feed = getTopSitesFeedForTest(sandbox);
  sandbox.stub(feed, "_readDefaults");
  sandbox.stub(feed, "refresh");
  sandbox.stub(feed._contile, "refresh");
  sandbox.stub(feed._contile, "_resetContileCache");
  sandbox.stub(NimbusFeatures.newtab, "getVariable").returns(false);
  sandbox.stub(SponsoredTopSitesScoreProvider, "init");
  sandbox.stub(SponsoredTopSitesScoreProvider, "uninit");
  sandbox.stub(SponsoredTopSitesScoreProvider, "clearData").resolves();
  const { values } = feed.store.state.Prefs;
  values[SHOWN_ON_NEWTAB_PREF] = true;
  values[SHOW_SPONSORED_PREF] = true;

  // The store updates the pref values before feeds see PREF_CHANGED.
  function assertAfterPrefChange(name, value, running) {
    sandbox.resetHistory();
    values[name] = value;
    feed.onAction({ type: actionTypes.PREF_CHANGED, data: { name, value } });
    Assert.equal(
      SponsoredTopSitesScoreProvider.init.called,
      running,
      `Setting ${name} to ${value} ${running ? "starts" : "does not start"} the scoring`
    );
    Assert.equal(
      SponsoredTopSitesScoreProvider.clearData.called &&
        SponsoredTopSitesScoreProvider.uninit.called,
      !running,
      `Setting ${name} to ${value} ${running ? "keeps" : "clears"} the scoring data`
    );
  }

  feed.init();
  Assert.ok(
    SponsoredTopSitesScoreProvider.init.calledOnce,
    "The scoring starts with shortcuts and sponsored shortcuts on"
  );
  feed.uninit();

  assertAfterPrefChange(SHOW_SPONSORED_PREF, false, false);
  assertAfterPrefChange(SHOW_SPONSORED_PREF, true, true);
  assertAfterPrefChange(SHOWN_ON_NEWTAB_PREF, false, false);
  assertAfterPrefChange(SHOWN_ON_NEWTAB_PREF, true, true);

  sandbox.resetHistory();
  feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "unifiedAds.tiles.enabled", value: false },
  });
  Assert.ok(
    SponsoredTopSitesScoreProvider.clearData.notCalled &&
      SponsoredTopSitesScoreProvider.uninit.notCalled,
    "Switching sponsored tiles away from MARS keeps the scoring"
  );

  sandbox.resetHistory();
  values[SHOW_SPONSORED_PREF] = false;
  feed.init();
  Assert.ok(
    SponsoredTopSitesScoreProvider.init.notCalled,
    "The scoring does not start with sponsored shortcuts off"
  );
  feed.uninit();

  sandbox.restore();
});

add_task(async function test_insert_part_1() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    return feed;
  };

  {
    info(
      "TopSitesFeed.insert should pin site in first slot of empty pinned list"
    );

    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    Screenshots.getScreenshotForURL.resolves(Promise.resolve(null));
    await feed.getScreenshotPreview("custom", 1234);

    Assert.ok(feed.store.dispatch.calledOnce);
    Assert.ok(
      feed.store.dispatch.calledWithExactly(
        actionCreators.OnlyToOneContent(
          {
            data: { preview: "", url: "custom" },
            type: actionTypes.PREVIEW_RESPONSE,
          },
          1234
        )
      )
    );

    Screenshots.getScreenshotForURL.resolves(FAKE_SCREENSHOT);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.insert should pin site in first slot of pinned list with " +
        "empty first slot"
    );

    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);
    let site = { url: "foo.bar", label: "foo" };
    await feed.insert({ data: { site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.insert should move a pinned site in first slot to the " +
        "next slot: part 1"
    );
    let site1 = { url: "example.com" };
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [site1]);
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    let site = { url: "foo.bar", label: "foo" };

    await feed.insert({ data: { site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 1));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.insert should move a pinned site in first slot to the " +
        "next slot: part 2"
    );
    let site1 = { url: "example.com" };
    let site2 = { url: "example.org" };
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [site1, null, site2]);
    let site = { url: "foo.bar", label: "foo" };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    await feed.insert({ data: { site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 1));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }
});

add_task(async function test_insert_part_2() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    return feed;
  };

  {
    info(
      "TopSitesFeed.insert should unpin the last site if all slots are " +
        "already pinned"
    );
    let site1 = { url: "example.com" };
    let site2 = { url: "example.org" };
    let site3 = { url: "example.net" };
    let site4 = { url: "example.biz" };
    let site5 = { url: "example.info" };
    let site6 = { url: "example.news" };
    let site7 = { url: "example.lol" };
    let site8 = { url: "example.golf" };
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [site1, site2, site3, site4, site5, site6, site7, site8]);
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values.topSitesRows = 1;
    let site = { url: "foo.bar", label: "foo" };
    await feed.insert({ data: { site } });
    Assert.equal(
      NewTabUtils.pinnedLinks.pin.callCount,
      8,
      "NewTabUtils.pinnedLinks.pin called 8 times"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 1));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site2, 2));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site3, 3));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site4, 4));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site5, 5));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site6, 6));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site7, 7));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info("TopSitesFeed.insert should trigger refresh on TOP_SITES_INSERT");
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.stub(feed, "refresh");
    let addAction = {
      type: actionTypes.TOP_SITES_INSERT,
      data: { site: { url: "foo.com" } },
    };

    await feed.insert(addAction);

    Assert.ok(feed.refresh.calledOnce, "feed.refresh called once");
    sandbox.restore();
  }

  {
    info("TopSitesFeed.insert should correctly handle different index values");
    let index = -1;
    let site = { url: "foo.bar", label: "foo" };
    let action = { data: { index, site } };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    await feed.insert(action);
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));

    index = undefined;
    await feed.insert(action);
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));

    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }
});

add_task(async function test_insert_part_3() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    return feed;
  };

  {
    info("TopSitesFeed.insert should pin site in specified slot that is free");
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    await feed.insert({ data: { index: 2, site, draggedFromIndex: 0 } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));

    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.insert should move a pinned site in specified slot " +
        "to the next slot"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    await feed.insert({ data: { index: 2, site, draggedFromIndex: 3 } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledWith({ url: "example.com" }, 3)
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.insert should move pinned sites in the direction " +
        "of the dragged site"
    );

    let site1 = { url: "foo.bar", label: "foo" };
    let site2 = { url: "example.com", label: "example" };
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, null, site2]);

    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    await feed.insert({ data: { index: 2, site: site1, draggedFromIndex: 0 } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 2));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site2, 1));
    NewTabUtils.pinnedLinks.pin.resetHistory();

    await feed.insert({ data: { index: 2, site: site1, draggedFromIndex: 5 } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 2));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site2, 3));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info("TopSitesFeed.insert should not insert past the visible top sites");

    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    let site1 = { url: "foo.bar", label: "foo" };
    await feed.insert({
      data: { index: 42, site: site1, draggedFromIndex: 0 },
    });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.notCalled,
      "NewTabUtils.pinnedLinks.pin wasn't called"
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }
});

add_task(async function test_pin_part_1() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    return feed;
  };

  {
    info(
      "TopSitesFeed.pin should pin site in specified slot empty pinned " +
        "list"
    );
    let site = {
      url: "foo.bar",
      label: "foo",
      customScreenshotURL: "screenshot",
    };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    await feed.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.pin should lookup the link object to update the custom " +
        "screenshot"
    );
    let site = {
      url: "foo.bar",
      label: "foo",
      customScreenshotURL: "screenshot",
    };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.spy(feed.pinnedCache, "request");
    await feed.pin({ data: { index: 2, site } });

    Assert.ok(
      feed.pinnedCache.request.calledOnce,
      "feed.pinnedCache.request called once"
    );
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.pin should lookup the link object to update the custom " +
        "screenshot when the custom screenshot is initially null"
    );
    let site = {
      url: "foo.bar",
      label: "foo",
      customScreenshotURL: null,
    };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.spy(feed.pinnedCache, "request");
    await feed.pin({ data: { index: 2, site } });

    Assert.ok(
      feed.pinnedCache.request.calledOnce,
      "feed.pinnedCache.request called once"
    );
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.pin should not do a link object lookup if custom " +
        "screenshot field is not set"
    );
    let site = { url: "foo.bar", label: "foo" };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.spy(feed.pinnedCache, "request");
    await feed.pin({ data: { index: 2, site } });

    Assert.ok(
      !feed.pinnedCache.request.called,
      "feed.pinnedCache.request never called"
    );
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.pin should pin site in specified slot of pinned " +
        "list that is free"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    await feed.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }
});

add_task(async function test_pin_part_2() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    return feed;
  };

  {
    info("TopSitesFeed.pin should save the searchTopSite attribute if set");
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo", searchTopSite: true };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    await feed.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.firstCall.args[0].searchTopSite);
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.pin should NOT move a pinned site in specified " +
        "slot to the next slot"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    await feed.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.pin should properly update LinksCache object " +
        "properties between migrations"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "https://foo.com/" }]);

    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    let pinnedLinks = await feed.pinnedCache.request();
    Assert.equal(pinnedLinks.length, 1);
    feed.pinnedCache.expire();

    pinnedLinks[0].__sharedCache.updateLink("screenshot", "foo");

    pinnedLinks = await feed.pinnedCache.request();
    Assert.equal(pinnedLinks[0].screenshot, "foo");

    // Force cache expiration in order to trigger a migration of objects
    feed.pinnedCache.expire();
    pinnedLinks[0].__sharedCache.updateLink("screenshot", "bar");

    pinnedLinks = await feed.pinnedCache.request();
    Assert.equal(pinnedLinks[0].screenshot, "bar");
    sandbox.restore();
  }
});

add_task(async function test_pin_part_3() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    return feed;
  };

  {
    info("TopSitesFeed.pin should call insert if index < 0");
    let site = { url: "foo.bar", label: "foo" };
    let action = { data: { index: -1, site } };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.spy(feed, "insert");
    await feed.pin(action);

    Assert.ok(feed.insert.calledOnce, "feed.insert called once");
    Assert.ok(feed.insert.calledWithExactly(action));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info("TopSitesFeed.pin should not call insert if index == 0");
    let site = { url: "foo.bar", label: "foo" };
    let action = { data: { index: 0, site } };
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.spy(feed, "insert");
    await feed.pin(action);

    Assert.ok(!feed.insert.called, "feed.insert not called");
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }

  {
    info("TopSitesFeed.pin should trigger refresh on TOP_SITES_PIN");
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.stub(feed, "refresh");
    let pinExistingAction = {
      type: actionTypes.TOP_SITES_PIN,
      data: { site: FAKE_LINKS[4], index: 4 },
    };

    await feed.pin(pinExistingAction);

    Assert.ok(feed.refresh.calledOnce, "feed.refresh called once");
    NewTabUtils.pinnedLinks.pin.resetHistory();
    sandbox.restore();
  }
});

add_task(async function test_integration() {
  let sandbox = sinon.createSandbox();

  info("Test adding a pinned site and removing it with actions");
  let feed = getTopSitesFeedForTest(sandbox);

  let resolvers = [];
  feed.store.dispatch = sandbox.stub().callsFake(() => {
    resolvers.shift()();
  });
  feed._startedUp = true;
  sandbox.stub(feed, "_fetchScreenshot");

  let forDispatch = action =>
    new Promise(resolve => {
      resolvers.push(resolve);
      feed.onAction(action);
    });

  feed._requestRichIcon = sandbox.stub();
  let url = "https://pin.me";
  sandbox.stub(NewTabUtils.pinnedLinks, "pin").callsFake(link => {
    NewTabUtils.pinnedLinks.links.push(link);
  });

  await forDispatch({
    type: actionTypes.TOP_SITES_INSERT,
    data: { site: { url } },
  });
  NewTabUtils.pinnedLinks.links.pop();
  await forDispatch({ type: actionTypes.PLACES_LINK_BLOCKED });

  Assert.ok(
    feed.store.dispatch.calledTwice,
    "feed.store.dispatch called twice"
  );
  Assert.equal(feed.store.dispatch.firstCall.args[0].data.links[0].url, url);
  Assert.equal(
    feed.store.dispatch.secondCall.args[0].data.links[0].url,
    FAKE_LINKS[0].url
  );

  sandbox.restore();
});

add_task(async function test_improvesearch_noDefaultSearchTile_experiment() {
  let sandbox = sinon.createSandbox();
  const NO_DEFAULT_SEARCH_TILE_PREF = "improvesearch.noDefaultSearchTile";

  let prepFeed = feed => {
    sandbox.stub(SearchService, "getDefault").resolves({
      identifier: "google",
    });
    return feed;
  };

  {
    info(
      "TopSitesFeed.getLinksWithDefaults should filter out alexa top 5 " +
        "search from the default sites"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = true;
    let top5Test = [
      "https://google.com",
      "https://search.yahoo.com",
      "https://yahoo.com",
      "https://bing.com",
      "https://ask.com",
      "https://duckduckgo.com",
    ];

    gGetTopSitesStub.resolves([
      { url: "https://amazon.com" },
      ...top5Test.map(url => ({ url })),
    ]);

    const urlsReturned = (await feed.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(
      urlsReturned.includes("https://amazon.com"),
      "amazon included in default links"
    );
    top5Test.forEach(url =>
      Assert.ok(!urlsReturned.includes(url), `Should not include ${url}`)
    );

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.getLinksWithDefaults should not filter out alexa, default " +
        "search from the query results if the experiment pref is off"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = false;

    gGetTopSitesStub.resolves([
      { url: "https://google.com" },
      { url: "https://foo.com" },
      { url: "https://duckduckgo" },
    ]);
    let urlsReturned = (await feed.getLinksWithDefaults()).map(
      link => link.url
    );

    Assert.ok(urlsReturned.includes("https://google.com"));
    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.getLinksWithDefaults should filter out the current " +
        "default search from the default sites"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = true;

    sandbox.stub(feed, "_currentSearchHostname").get(() => "amazon");
    feed.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
      data: { "default.sites": "google.com,amazon.com" },
    });
    gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);

    let urlsReturned = (await feed.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(!urlsReturned.includes("https://amazon.com"));

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.getLinksWithDefaults should not filter out current " +
        "default search from pinned sites even if it matches the current " +
        "default search"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = true;

    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "google.com" }]);
    gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);

    let urlsReturned = (await feed.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(urlsReturned.includes("google.com"));

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }
});

add_task(async function test_sponsored_tile_retained_when_default_search() {
  // Bug 1957165: when the user's default search engine matches a sponsored
  // tile's hostname (e.g. setting Amazon as default search), the sponsored
  // tile must still be shown so the sponsored slate stays full.
  let sandbox = sinon.createSandbox();
  const NO_DEFAULT_SEARCH_TILE_PREF = "improvesearch.noDefaultSearchTile";

  sandbox.stub(SearchService, "getDefault").resolves({ identifier: "google" });
  let feed = getTopSitesFeedForTest(sandbox);
  feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = true;
  feed.store.state.Prefs.values.showSponsoredTopSites = true;

  sandbox.stub(feed, "_currentSearchHostname").get(() => "amazon");

  DEFAULT_TOP_SITES.length = 0;
  DEFAULT_TOP_SITES.push({
    url: "https://amazon.com",
    hostname: "amazon",
    sponsored_position: 1,
    label: "Amazon",
  });
  gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);

  let urlsReturned = (await feed.getLinksWithDefaults()).map(link => link?.url);
  Assert.ok(
    urlsReturned.includes("https://amazon.com"),
    "sponsored Amazon tile is retained even though Amazon is the default " +
      "search engine"
  );

  DEFAULT_TOP_SITES.length = 0;
  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
});

add_task(
  async function test_improvesearch_noDefaultSearchTile_experiment_part_2() {
    let sandbox = sinon.createSandbox();
    const NO_DEFAULT_SEARCH_TILE_PREF = "improvesearch.noDefaultSearchTile";

    let prepFeed = feed => {
      sandbox.stub(SearchService, "getDefault").resolves({
        identifier: "google",
      });
      return feed;
    };

    {
      info(
        "TopSitesFeed.getLinksWithDefaults should call refresh and set " +
          "._currentSearchHostname to the new engine hostname when the " +
          "default search engine has been set"
      );
      let feed = prepFeed(getTopSitesFeedForTest(sandbox));
      feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = true;
      sandbox.stub(feed, "refresh");
      sandbox.stub(feed._contile, "refresh");

      feed.observe(null, "browser-search-engine-modified", "engine-default");
      Assert.equal(feed._currentSearchHostname, "duckduckgo");
      Assert.ok(feed.refresh.calledOnce, "feed.refresh called once");
      Assert.ok(
        feed._contile.refresh.calledOnce,
        "feed._contile.refresh called once so sponsored tiles are rebuilt " +
          "for the new default search engine"
      );

      gGetTopSitesStub.resolves(FAKE_LINKS);
      sandbox.restore();
    }

    {
      info(
        "TopSitesFeed.getLinksWithDefaults should call refresh when the " +
          "experiment pref has changed"
      );
      let feed = prepFeed(getTopSitesFeedForTest(sandbox));
      feed.store.state.Prefs.values[NO_DEFAULT_SEARCH_TILE_PREF] = true;
      sandbox.stub(feed, "refresh");

      feed.onAction({
        type: actionTypes.PREF_CHANGED,
        data: { name: NO_DEFAULT_SEARCH_TILE_PREF, value: true },
      });
      Assert.ok(feed.refresh.calledOnce, "feed.refresh was called once");

      feed.onAction({
        type: actionTypes.PREF_CHANGED,
        data: { name: NO_DEFAULT_SEARCH_TILE_PREF, value: false },
      });
      Assert.ok(feed.refresh.calledTwice, "feed.refresh was called twice");

      gGetTopSitesStub.resolves(FAKE_LINKS);
      sandbox.restore();
    }
  }
);

// eslint-disable-next-line max-statements
add_task(async function test_improvesearch_topSitesSearchShortcuts() {
  let sandbox = sinon.createSandbox();
  let searchEngines = [{ aliases: ["@google"] }, { aliases: ["@amazon"] }];

  let prepFeed = feed => {
    sandbox
      .stub(SearchService, "getAppProvidedEngines")
      .resolves(searchEngines);
    sandbox.stub(NewTabUtils.pinnedLinks, "pin").callsFake((site, index) => {
      NewTabUtils.pinnedLinks.links[index] = site;
    });
    feed.store.state.Prefs.values[SEARCH_SHORTCUTS_EXPERIMENT_PREF] = true;
    feed.store.state.Prefs.values[SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF] =
      "google,amazon";
    feed.store.state.Prefs.values[SEARCH_SHORTCUTS_HAVE_PINNED_PREF] = "";
    return feed;
  };

  {
    info(
      "TopSitesFeed should updateCustomSearchShortcuts when experiment " +
        "pref is turned on"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values[SEARCH_SHORTCUTS_EXPERIMENT_PREF] = false;
    feed.updateCustomSearchShortcuts = sandbox.spy();

    // turn the experiment on
    feed.onAction({
      type: actionTypes.PREF_CHANGED,
      data: { name: SEARCH_SHORTCUTS_EXPERIMENT_PREF, value: true },
    });

    Assert.ok(
      feed.updateCustomSearchShortcuts.calledOnce,
      "feed.updateCustomSearchShortcuts called once"
    );
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed should filter out default top sites that match a " +
        "hostname of a search shortcut if previously blocked"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.refreshDefaults("https://amazon.ca");
    sandbox
      .stub(NewTabUtils.blockedLinks, "links")
      .value([{ url: "https://amazon.com" }]);
    sandbox.stub(NewTabUtils.blockedLinks, "isBlocked").callsFake(site => {
      return NewTabUtils.blockedLinks.links[0].url === site.url;
    });

    let urlsReturned = (await feed.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(!urlsReturned.includes("https://amazon.ca"));
    sandbox.restore();
  }

  {
    info("TopSitesFeed should update frecent search topsite icon");
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed._tippyTopProvider.processSite = site => {
      site.tippyTopIcon = "icon.png";
      site.backgroundColor = "#fff";
      return site;
    };
    gGetTopSitesStub.resolves([{ url: "https://google.com" }]);

    let urlsReturned = await feed.getLinksWithDefaults();

    let defaultSearchTopsite = urlsReturned.find(
      s => s.url === "https://google.com"
    );
    Assert.ok(defaultSearchTopsite.searchTopSite);
    Assert.equal(defaultSearchTopsite.tippyTopIcon, "icon.png");
    Assert.equal(defaultSearchTopsite.backgroundColor, "#fff");
    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }

  {
    info("TopSitesFeed should update default search topsite icon");
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed._tippyTopProvider.processSite = site => {
      site.tippyTopIcon = "icon.png";
      site.backgroundColor = "#fff";
      return site;
    };
    gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);
    feed.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
      data: { "default.sites": "google.com,amazon.com" },
    });

    let urlsReturned = await feed.getLinksWithDefaults();

    let defaultSearchTopsite = urlsReturned.find(
      s => s.url === "https://amazon.com"
    );
    Assert.ok(defaultSearchTopsite.searchTopSite);
    Assert.equal(defaultSearchTopsite.tippyTopIcon, "icon.png");
    Assert.equal(defaultSearchTopsite.backgroundColor, "#fff");
    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed should dispatch UPDATE_SEARCH_SHORTCUTS on " +
        "updateCustomSearchShortcuts"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));
    feed.store.state.Prefs.values["improvesearch.noDefaultSearchTile"] = true;
    await feed.updateCustomSearchShortcuts();
    Assert.ok(
      feed.store.dispatch.calledOnce,
      "feed.store.dispatch called once"
    );
    Assert.ok(
      feed.store.dispatch.calledWith({
        data: {
          searchShortcuts: [
            {
              keyword: "@google",
              shortURL: "google",
              url: "https://google.com",
              backgroundColor: undefined,
              smallFavicon:
                "chrome://activity-stream/content/data/content/tippytop/favicons/google-com.ico",
              tippyTopIcon:
                "chrome://activity-stream/content/data/content/tippytop/images/google-com@2x.png",
            },
            {
              keyword: "@amazon",
              shortURL: "amazon",
              url: "https://amazon.com",
              backgroundColor: undefined,
              smallFavicon:
                "chrome://activity-stream/content/data/content/tippytop/favicons/amazon.ico",
              tippyTopIcon:
                "chrome://activity-stream/content/data/content/tippytop/images/amazon@2x.png",
            },
          ],
        },
        meta: {
          from: "ActivityStream:Main",
          to: "ActivityStream:Content",
          isStartup: false,
        },
        type: "UPDATE_SEARCH_SHORTCUTS",
      })
    );
  }

  sandbox.restore();
});

// eslint-disable-next-line max-statements
add_task(async function test_updatePinnedSearchShortcuts() {
  let sandbox = sinon.createSandbox();

  let prepFeed = feed => {
    sandbox.stub(NewTabUtils.pinnedLinks, "pin");
    sandbox.stub(NewTabUtils.pinnedLinks, "unpin");
    return feed;
  };

  {
    info(
      "TopSitesFeed.updatePinnedSearchShortcuts should unpin a " +
        "shortcut in deletedShortcuts"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    let deletedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
    ];
    let addedShortcuts = [];
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [
      null,
      null,
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ]);

    feed.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.notCalled,
      "NewTabUtils.pinnedLinks.pin not called"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.calledOnce,
      "NewTabUtils.pinnedLinks.unpin called once"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.calledWith({
        url: "https://google.com",
      })
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.updatePinnedSearchShortcuts should pin a shortcut " +
        "in addedShortcuts"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    let addedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
    ];
    let deletedShortcuts = [];
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [
      null,
      null,
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ]);
    feed.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });

    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.notCalled,
      "NewTabUtils.pinnedLinks.unpin not called"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledWith(
        {
          label: "google",
          searchTopSite: true,
          searchVendor: "google",
          url: "https://google.com",
        },
        0
      )
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.updatePinnedSearchShortcuts should pin and unpin " +
        "in the same action"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    let addedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
      {
        url: "https://ebay.com",
        searchVendor: "ebay",
        label: "ebay",
        searchTopSite: true,
      },
    ];
    let deletedShortcuts = [
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ];

    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [
      { url: "https://foo.com" },
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ]);

    feed.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });

    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.calledOnce,
      "NewTabUtils.pinnedLinks.unpin called once"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed.updatePinnedSearchShortcuts should pin a shortcut in " +
        "addedShortcuts even if pinnedLinks is full"
    );
    let feed = prepFeed(getTopSitesFeedForTest(sandbox));

    let addedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
    ];
    let deletedShortcuts = [];
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => FAKE_LINKS);
    feed.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });

    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.notCalled,
      "NewTabUtils.pinnedLinks.unpin not called"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledWith(
        { label: "google", searchTopSite: true, url: "https://google.com" },
        0
      ),
      "NewTabUtils.pinnedLinks.unpin not called"
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    sandbox.restore();
  }
});

// eslint-disable-next-line max-statements
add_task(async function test_ContileIntegration() {
  let sandbox = sinon.createSandbox();
  Services.prefs.setStringPref(
    TOP_SITES_BLOCKED_SPONSORS_PREF,
    `["foo","bar"]`
  );

  let prepFeed = feed => {
    sandbox.stub(NimbusFeatures.newtab, "getVariable").returns(true);
    feed.store.state.Prefs.values[SHOW_SPONSORED_PREF] = true;
    let fetchStub = sandbox.stub(feed, "fetch");
    return { feed, fetchStub };
  };

  {
    info("TopSitesFeed._fetchSites should fetch sites from Contile");
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles: [
            {
              url: "https://www.test.com",
              image_url: "images/test-com.png",
              click_url: "https://www.test-click.com",
              impression_url: "https://www.test-impression.com",
              name: "test",
            },
            {
              url: "https://www.test1.com",
              image_url: "images/test1-com.png",
              click_url: "https://www.test1-click.com",
              impression_url: "https://www.test1-impression.com",
              name: "test1",
            },
          ],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    Assert.equal(feed._contile.sites.length, 2);

    info("TopSitesFeed._fetchSites should not send cookies");
    Assert.ok(fetchStub.calledOnce, "fetch should be called once");
    Assert.equal(
      fetchStub.firstCall.args[1].credentials,
      "omit",
      "should not send cookies"
    );
    sandbox.restore();
  }

  {
    info("TopSitesFeed._fetchSites should call allocatePositions");
    let { feed } = prepFeed(getTopSitesFeedForTest(sandbox));
    sandbox.stub(feed, "allocatePositions").resolves();
    await feed._contile.refresh();

    Assert.ok(
      feed.allocatePositions.calledOnce,
      "feed.allocatePositions called once"
    );
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should fetch SOV (Share-of-Voice) " +
        "settings from Contile"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));

    let sov = {
      name: "SOV-20230518215316",
      allocations: [
        {
          position: 1,
          allocation: [
            {
              partner: "foo",
              percentage: 100,
            },
            {
              partner: "bar",
              percentage: 0,
            },
          ],
        },
        {
          position: 2,
          allocation: [
            {
              partner: "foo",
              percentage: 80,
            },
            {
              partner: "bar",
              percentage: 20,
            },
          ],
        },
      ],
    };
    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          sov: btoa(JSON.stringify(sov)),
          tiles: [
            {
              url: "https://www.test.com",
              image_url: "images/test-com.png",
              click_url: "https://www.test-click.com",
              impression_url: "https://www.test-impression.com",
              name: "test",
            },
            {
              url: "https://www.test1.com",
              image_url: "images/test1-com.png",
              click_url: "https://www.test1-click.com",
              impression_url: "https://www.test1-impression.com",
              name: "test1",
            },
          ],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    Assert.deepEqual(feed._contile.sov, sov);
    Assert.equal(feed._contile.sites.length, 2);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should not fetch from Contile if " +
        "it's not enabled"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));

    NimbusFeatures.newtab.getVariable.reset();
    NimbusFeatures.newtab.getVariable.returns(false);
    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetchStub.notCalled, "TopSitesFeed.fetch was not called");
    Assert.ok(!fetched);
    Assert.equal(feed._contile.sites.length, 0);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should still return two tiles when Contile " +
        "provides more than 2 tiles and filtering results in more than 2 tiles"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));

    NimbusFeatures.newtab.getVariable.reset();
    NimbusFeatures.newtab.getVariable.onCall(0).returns(true);
    NimbusFeatures.newtab.getVariable.onCall(1).returns(true);

    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles: [
            {
              url: "https://www.test.com",
              image_url: "images/test-com.png",
              click_url: "https://www.test-click.com",
              impression_url: "https://www.test-impression.com",
              name: "test",
            },
            {
              url: "https://foo.com",
              image_url: "images/foo-com.png",
              click_url: "https://www.foo-click.com",
              impression_url: "https://www.foo-impression.com",
              name: "foo",
            },
            {
              url: "https://bar.com",
              image_url: "images/bar-com.png",
              click_url: "https://www.bar-click.com",
              impression_url: "https://www.bar-impression.com",
              name: "bar",
            },
            {
              url: "https://test1.com",
              image_url: "images/test1-com.png",
              click_url: "https://www.test1-click.com",
              impression_url: "https://www.test1-impression.com",
              name: "test1",
            },
            {
              url: "https://test2.com",
              image_url: "images/test2-com.png",
              click_url: "https://www.test2-click.com",
              impression_url: "https://www.test2-impression.com",
              name: "test2",
            },
          ],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    // Both "foo" and "bar" should be filtered
    Assert.equal(feed._contile.sites.length, 3);
    Assert.equal(feed._contile.sites[0].url, "https://www.test.com");
    Assert.equal(feed._contile.sites[1].url, "https://test1.com");
    Assert.equal(feed._contile.sites[2].url, "https://test2.com");
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should still return two tiles with " +
        "replacement if the Nimbus variable was unset"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));

    NimbusFeatures.newtab.getVariable.reset();
    NimbusFeatures.newtab.getVariable.onCall(0).returns(true);
    NimbusFeatures.newtab.getVariable.onCall(1).returns(undefined);

    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles: [
            {
              url: "https://www.test.com",
              image_url: "images/test-com.png",
              click_url: "https://www.test-click.com",
              impression_url: "https://www.test-impression.com",
              name: "test",
            },
            {
              url: "https://foo.com",
              image_url: "images/foo-com.png",
              click_url: "https://www.foo-click.com",
              impression_url: "https://www.foo-impression.com",
              name: "foo",
            },
            {
              url: "https://test1.com",
              image_url: "images/test1-com.png",
              click_url: "https://www.test1-click.com",
              impression_url: "https://www.test1-impression.com",
              name: "test1",
            },
          ],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    Assert.equal(feed._contile.sites.length, 2);
    Assert.equal(feed._contile.sites[0].url, "https://www.test.com");
    Assert.equal(feed._contile.sites[1].url, "https://test1.com");
    sandbox.restore();
  }

  {
    info("TopSitesFeed._fetchSites should filter the blocked sponsors");

    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles: [
            {
              url: "https://www.test.com",
              image_url: "images/test-com.png",
              click_url: "https://www.test-click.com",
              impression_url: "https://www.test-impression.com",
              name: "test",
            },
            {
              url: "https://foo.com",
              image_url: "images/foo-com.png",
              click_url: "https://www.foo-click.com",
              impression_url: "https://www.foo-impression.com",
              name: "foo",
            },
            {
              url: "https://bar.com",
              image_url: "images/bar-com.png",
              click_url: "https://www.bar-click.com",
              impression_url: "https://www.bar-impression.com",
              name: "bar",
            },
          ],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    // Both "foo" and "bar" should be filtered
    Assert.equal(feed._contile.sites.length, 1);
    Assert.equal(feed._contile.sites[0].url, "https://www.test.com");
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should return false when Contile returns " +
        "with error status and no values are stored in cache prefs"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);
    feed._contile.cache.get.returns({ contile: [] });
    Services.prefs.setIntPref(CONTILE_CACHE_LAST_FETCH_PREF, 0);

    fetchStub.resolves({
      ok: false,
      status: 500,
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(!fetched);
    Assert.ok(!feed._contile.sites.length);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should return false when Contile " +
        "returns with error status and cached tiles are expried"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);
    feed._contile.cache.get.returns({ contile: [] });
    const THIRTY_MINUTES_AGO_IN_SECONDS =
      Math.round(Date.now() / 1000) - 60 * 30;
    Services.prefs.setIntPref(
      CONTILE_CACHE_LAST_FETCH_PREF,
      THIRTY_MINUTES_AGO_IN_SECONDS
    );
    Services.prefs.setIntPref(CONTILE_CACHE_VALID_FOR_SECONDS_PREF, 60 * 15);

    fetchStub.resolves({
      ok: false,
      status: 500,
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(!fetched);
    Assert.ok(!feed._contile.sites.length);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should handle invalid payload " +
        "properly from Contile"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));

    NimbusFeatures.newtab.getVariable.returns(true);
    fetchStub.resolves({
      ok: true,
      status: 200,
      json: () =>
        Promise.resolve({
          unknown: [],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(!fetched);
    Assert.ok(!feed._contile.sites.length);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should handle empty payload properly " +
        "from Contile"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles: [],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    Assert.ok(!feed._contile.sites.length);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should handle no content properly " +
        "from Contile"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    fetchStub.resolves({ ok: true, status: 204 });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(!fetched);
    Assert.ok(!feed._contile.sites.length);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should set Caching Prefs after " +
        "a successful request"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    let tiles = [
      {
        url: "https://www.test.com",
        image_url: "images/test-com.png",
        click_url: "https://www.test-click.com",
        impression_url: "https://www.test-impression.com",
        name: "test",
      },
      {
        url: "https://www.test1.com",
        image_url: "images/test1-com.png",
        click_url: "https://www.test1-click.com",
        impression_url: "https://www.test1-impression.com",
        name: "test1",
      },
    ];
    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles,
        }),
    });

    let fetched = await feed._contile._fetchSites();
    Assert.ok(fetched);
    Assert.ok(feed._contile.cache.set.calledWith("contile", tiles));
    Assert.equal(
      Services.prefs.getIntPref(CONTILE_CACHE_VALID_FOR_SECONDS_PREF),
      11322
    );
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should return cached valid tiles " +
        "when Contile returns error status"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    let tiles = [
      {
        url: "https://www.test-cached.com",
        image_url: "images/test-com.png",
        click_url: "https://www.test-click.com",
        impression_url: "https://www.test-impression.com",
        name: "test",
      },
      {
        url: "https://www.test1-cached.com",
        image_url: "images/test1-com.png",
        click_url: "https://www.test1-click.com",
        impression_url: "https://www.test1-impression.com",
        name: "test1",
      },
    ];

    feed._contile.cache.get.returns({ contile: tiles });
    Services.prefs.setIntPref(CONTILE_CACHE_VALID_FOR_SECONDS_PREF, 60 * 15);
    Services.prefs.setIntPref(
      CONTILE_CACHE_LAST_FETCH_PREF,
      Math.round(Date.now() / 1000)
    );

    fetchStub.resolves({
      status: 304,
    });

    let fetched = await feed._contile._fetchSites();
    Assert.ok(fetched);
    Assert.equal(feed._contile.sites.length, 2);
    Assert.equal(feed._contile.sites[0].url, "https://www.test-cached.com");
    Assert.equal(feed._contile.sites[1].url, "https://www.test1-cached.com");
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should not be successful when contile " +
        "returns an error and no valid tiles are cached"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    feed._contile.cache.get.returns({ contile: [] });
    Services.prefs.setIntPref(CONTILE_CACHE_VALID_FOR_SECONDS_PREF, 0);
    Services.prefs.setIntPref(CONTILE_CACHE_LAST_FETCH_PREF, 0);

    fetchStub.resolves({
      status: 500,
    });

    let fetched = await feed._contile._fetchSites();
    Assert.ok(!fetched);
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should return cached valid tiles " +
        "filtering blocked tiles when Contile returns error status"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    let tiles = [
      {
        url: "https://foo.com",
        image_url: "images/foo-com.png",
        click_url: "https://www.foo-click.com",
        impression_url: "https://www.foo-impression.com",
        name: "foo",
      },
      {
        url: "https://www.test1-cached.com",
        image_url: "images/test1-com.png",
        click_url: "https://www.test1-click.com",
        impression_url: "https://www.test1-impression.com",
        name: "test1",
      },
    ];
    feed._contile.cache.get.returns({ contile: tiles });
    Services.prefs.setIntPref(CONTILE_CACHE_VALID_FOR_SECONDS_PREF, 60 * 15);
    Services.prefs.setIntPref(
      CONTILE_CACHE_LAST_FETCH_PREF,
      Math.round(Date.now() / 1000)
    );

    fetchStub.resolves({
      status: 304,
    });

    let fetched = await feed._contile._fetchSites();
    Assert.ok(fetched);
    Assert.equal(feed._contile.sites.length, 1);
    Assert.equal(feed._contile.sites[0].url, "https://www.test1-cached.com");
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should still return 3 tiles when nimbus " +
        "variable overrides max num of sponsored contile tiles"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    NimbusFeatures.newtab.getVariable.returns(true);

    sandbox.stub(NimbusFeatures.pocketNewtab, "getVariable").returns(3);
    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map([
        ["cache-control", "private, max-age=859, stale-if-error=10463"],
      ]),
      json: () =>
        Promise.resolve({
          tiles: [
            {
              url: "https://www.test.com",
              image_url: "images/test-com.png",
              click_url: "https://www.test-click.com",
              impression_url: "https://www.test-impression.com",
              name: "test",
            },
            {
              url: "https://test1.com",
              image_url: "images/test1-com.png",
              click_url: "https://www.test1-click.com",
              impression_url: "https://www.test1-impression.com",
              name: "test1",
            },
            {
              url: "https://test2.com",
              image_url: "images/test2-com.png",
              click_url: "https://www.test2-click.com",
              impression_url: "https://www.test2-impression.com",
              name: "test2",
            },
          ],
        }),
    });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetched);
    Assert.equal(feed._contile.sites.length, 3);
    Assert.equal(feed._contile.sites[0].url, "https://www.test.com");
    Assert.equal(feed._contile.sites[1].url, "https://test1.com");
    Assert.equal(feed._contile.sites[2].url, "https://test2.com");
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should cast headers from a Headers object to JS object when using OHTTP"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    const CONTEXT_ID = "ContextId";
    sandbox.stub(ContextId, "request").returns(CONTEXT_ID);

    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
      "https://relay.url"
    );
    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
      "https://config.url"
    );
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled",
      true
    );
    feed.store.state.Prefs.values["unifiedAds.tiles.enabled"] = true;
    feed.store.state.Prefs.values["unifiedAds.adsFeed.enabled"] = false;
    feed.store.state.Prefs.values["unifiedAds.endpoint"] =
      "https://test.endpoint/";
    feed.store.state.Prefs.values["discoverystream.placements.tiles"] = "1";
    feed.store.state.Prefs.values["discoverystream.placements.tiles.counts"] =
      "1";
    feed.store.state.Prefs.values["unifiedAds.blockedAds"] = "";

    const TEST_PREFLIGHT_UA_STRING = "Some test UA";
    const TEST_PREFLIGHT_GEONAME_ID = "Some geo name";
    const TEST_PREFLIGHT_GEO_LOCATION = "Some geo location";

    fetchStub.resolves({
      ok: true,
      status: 200,
      json: () =>
        Promise.resolve({
          normalized_ua: TEST_PREFLIGHT_UA_STRING,
          geoname_id: TEST_PREFLIGHT_GEONAME_ID,
          geo_location: TEST_PREFLIGHT_GEO_LOCATION,
        }),
    });

    const fakeOhttpConfig = { config: "config" };
    sandbox.stub(ObliviousHTTP, "getOHTTPConfig").resolves(fakeOhttpConfig);

    const ohttpRequestStub = sandbox
      .stub(ObliviousHTTP, "ohttpRequest")
      .resolves({
        ok: true,
        status: 200,
        headers: new Map([
          ["cache-control", "private, max-age=859, stale-if-error=10463"],
        ]),
        json: () =>
          Promise.resolve({
            1: [
              {
                block_key: 12345,
                name: "test",
                url: "https://www.test.com",
                image_url: "images/test-com.png",
                callbacks: {
                  click: "https://www.test-click.com",
                  impression: "https://www.test-impression.com",
                },
              },
            ],
          }),
      });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetchStub.calledOnce, "The preflight request was made.");

    Assert.ok(fetched);
    Assert.ok(
      ohttpRequestStub.calledOnce,
      "ohttpRequest should be called once"
    );
    const callArgs = ohttpRequestStub.getCall(0).args;
    Assert.equal(callArgs[0], "https://relay.url", "relay URL should match");
    Assert.deepEqual(
      callArgs[1],
      fakeOhttpConfig,
      "config should be passed through"
    );

    const sentHeaders = callArgs[3].headers;
    Assert.equal(
      typeof sentHeaders,
      "object",
      "headers should be a plain object"
    );
    Assert.ok(
      // We use instanceof here since isInstance isn't available for
      // Headers, it seems.
      // eslint-disable-next-line mozilla/use-isInstance
      !(sentHeaders instanceof Headers),
      "headers should not be a Headers instance"
    );

    Assert.equal(
      sentHeaders["x-user-agent"],
      TEST_PREFLIGHT_UA_STRING,
      "Sent the x-user-agent header from preflight"
    );
    Assert.equal(
      sentHeaders["x-geoname-id"],
      TEST_PREFLIGHT_GEONAME_ID,
      "Sent the x-geoname-id header from preflight"
    );
    Assert.equal(
      sentHeaders["x-geo-location"],
      TEST_PREFLIGHT_GEO_LOCATION,
      "Sent the x-geo-location header from preflight"
    );

    info("TopSitesFeed._fetchSites should not send cookies via OHTTP");
    Assert.equal(callArgs[3].credentials, "omit", "should not send cookies");

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled"
    );

    info("TopSitesFeed._fetchSites should include correct body");
    Assert.equal(
      callArgs[3].body,
      JSON.stringify({
        context_id: CONTEXT_ID,
        flags: {},
        placements: [
          {
            placement: "1",
            count: 1,
          },
        ],
        blocks: ["duckduckgo"],
      })
    );
    sandbox.restore();
  }

  {
    info("TopSitesFeed._fetchSites inclued adsBackend flags");
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    const CONTEXT_ID = "ContextId";
    sandbox.stub(ContextId, "request").returns(CONTEXT_ID);

    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
      "https://relay.url"
    );
    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
      "https://config.url"
    );
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled",
      true
    );
    feed.store.state.Prefs.values["unifiedAds.tiles.enabled"] = true;
    feed.store.state.Prefs.values["unifiedAds.adsFeed.enabled"] = false;
    feed.store.state.Prefs.values["unifiedAds.endpoint"] =
      "https://test.endpoint/";
    feed.store.state.Prefs.values["discoverystream.placements.tiles"] = "1";
    feed.store.state.Prefs.values["discoverystream.placements.tiles.counts"] =
      "1";
    feed.store.state.Prefs.values["unifiedAds.blockedAds"] = "";
    feed.store.state.Prefs.values.adsBackendConfig = {
      feature1: true,
      feature2: false,
    };

    const TEST_PREFLIGHT_UA_STRING = "Some test UA";
    const TEST_PREFLIGHT_GEONAME_ID = "Some geo name";
    const TEST_PREFLIGHT_GEO_LOCATION = "Some geo location";

    fetchStub.resolves({
      ok: true,
      status: 200,
      json: () =>
        Promise.resolve({
          normalized_ua: TEST_PREFLIGHT_UA_STRING,
          geoname_id: TEST_PREFLIGHT_GEONAME_ID,
          geo_location: TEST_PREFLIGHT_GEO_LOCATION,
        }),
    });

    const fakeOhttpConfig = { config: "config" };
    sandbox.stub(ObliviousHTTP, "getOHTTPConfig").resolves(fakeOhttpConfig);

    const ohttpRequestStub = sandbox
      .stub(ObliviousHTTP, "ohttpRequest")
      .resolves({
        ok: true,
        status: 200,
        headers: new Map([
          ["cache-control", "private, max-age=859, stale-if-error=10463"],
        ]),
        json: () =>
          Promise.resolve({
            1: [
              {
                block_key: 12345,
                name: "test",
                url: "https://www.test.com",
                image_url: "images/test-com.png",
                callbacks: {
                  click: "https://www.test-click.com",
                  impression: "https://www.test-impression.com",
                },
              },
            ],
          }),
      });

    let fetched = await feed._contile._fetchSites();

    Assert.ok(fetchStub.calledOnce, "The preflight request was made.");

    Assert.ok(fetched);
    Assert.ok(
      ohttpRequestStub.calledOnce,
      "ohttpRequest should be called once"
    );
    const callArgs = ohttpRequestStub.getCall(0).args;
    Assert.equal(callArgs[0], "https://relay.url", "relay URL should match");
    Assert.deepEqual(
      callArgs[1],
      fakeOhttpConfig,
      "config should be passed through"
    );

    const sentHeaders = callArgs[3].headers;
    Assert.equal(
      typeof sentHeaders,
      "object",
      "headers should be a plain object"
    );
    Assert.ok(
      // We use instanceof here since isInstance isn't available for
      // Headers, it seems.
      // eslint-disable-next-line mozilla/use-isInstance
      !(sentHeaders instanceof Headers),
      "headers should not be a Headers instance"
    );

    Assert.equal(
      sentHeaders["x-user-agent"],
      TEST_PREFLIGHT_UA_STRING,
      "Sent the x-user-agent header from preflight"
    );
    Assert.equal(
      sentHeaders["x-geoname-id"],
      TEST_PREFLIGHT_GEONAME_ID,
      "Sent the x-geoname-id header from preflight"
    );
    Assert.equal(
      sentHeaders["x-geo-location"],
      TEST_PREFLIGHT_GEO_LOCATION,
      "Sent the x-geo-location header from preflight"
    );

    info("TopSitesFeed._fetchSites should not send cookies via OHTTP");
    Assert.equal(callArgs[3].credentials, "omit", "should not send cookies");

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled"
    );

    info("TopSitesFeed._fetchSites should include correct body");
    Assert.equal(
      callArgs[3].body,
      JSON.stringify({
        context_id: CONTEXT_ID,
        flags: {
          feature1: true,
          feature2: false,
        },
        placements: [
          {
            placement: "1",
            count: 1,
          },
        ],
        blocks: ["duckduckgo"],
      })
    );
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should send relevance scores without the context_id"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    const contextIdStub = sandbox
      .stub(ContextId, "request")
      .returns("ContextId");
    const RELEVANCE = { t1: 0.3, t2: 0.1 };
    sandbox
      .stub(SponsoredTopSitesScoreProvider, "getScores")
      .resolves(RELEVANCE);

    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
      "https://relay.url"
    );
    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
      "https://config.url"
    );
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled",
      true
    );
    feed.store.state.Prefs.values["unifiedAds.tiles.enabled"] = true;
    feed.store.state.Prefs.values["unifiedAds.adsFeed.enabled"] = false;
    feed.store.state.Prefs.values["unifiedAds.endpoint"] =
      "https://test.endpoint/";
    feed.store.state.Prefs.values["discoverystream.placements.tiles"] = "1";
    feed.store.state.Prefs.values["discoverystream.placements.tiles.counts"] =
      "1";
    feed.store.state.Prefs.values["unifiedAds.blockedAds"] = "";

    fetchStub.resolves({
      ok: true,
      status: 200,
      json: () => Promise.resolve({}),
    });
    sandbox
      .stub(ObliviousHTTP, "getOHTTPConfig")
      .resolves({ config: "config" });
    const ohttpRequestStub = sandbox
      .stub(ObliviousHTTP, "ohttpRequest")
      .resolves({
        ok: true,
        status: 200,
        headers: new Map(),
        json: () => Promise.resolve({ 1: [] }),
      });

    await feed._contile._fetchSites();

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled"
    );

    Assert.ok(
      ohttpRequestStub.calledOnce,
      "ohttpRequest should be called once"
    );
    Assert.ok(contextIdStub.notCalled, "The context_id was not requested");
    Assert.equal(
      ohttpRequestStub.getCall(0).args[3].body,
      JSON.stringify({
        flags: {},
        placements: [
          {
            placement: "1",
            count: 1,
          },
        ],
        blocks: ["duckduckgo"],
        relevance: RELEVANCE,
      }),
      "The body should include the relevance scores and no context_id"
    );
    sandbox.restore();
  }

  {
    info(
      "TopSitesFeed._fetchSites should not send relevance scores without OHTTP"
    );
    let { feed, fetchStub } = prepFeed(getTopSitesFeedForTest(sandbox));
    const CONTEXT_ID = "ContextId";
    sandbox.stub(ContextId, "request").returns(CONTEXT_ID);
    const getScoresStub = sandbox
      .stub(SponsoredTopSitesScoreProvider, "getScores")
      .resolves({ t1: 0.3 });

    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled",
      false
    );
    feed.store.state.Prefs.values["unifiedAds.tiles.enabled"] = true;
    feed.store.state.Prefs.values["unifiedAds.adsFeed.enabled"] = false;
    feed.store.state.Prefs.values["unifiedAds.endpoint"] =
      "https://test.endpoint/";
    feed.store.state.Prefs.values["discoverystream.placements.tiles"] = "1";
    feed.store.state.Prefs.values["discoverystream.placements.tiles.counts"] =
      "1";
    feed.store.state.Prefs.values["unifiedAds.blockedAds"] = "";

    fetchStub.resolves({
      ok: true,
      status: 200,
      headers: new Map(),
      json: () => Promise.resolve({ 1: [] }),
    });

    await feed._contile._fetchSites();

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled"
    );

    Assert.ok(fetchStub.calledOnce, "The tiles were requested without OHTTP");
    Assert.ok(getScoresStub.notCalled, "The scores were not read");
    Assert.equal(
      fetchStub.getCall(0).args[1].body,
      JSON.stringify({
        context_id: CONTEXT_ID,
        flags: {},
        placements: [
          {
            placement: "1",
            count: 1,
          },
        ],
        blocks: ["duckduckgo"],
      }),
      "The body should include the context_id and no relevance scores"
    );
    sandbox.restore();
  }

  Services.prefs.clearUserPref(TOP_SITES_BLOCKED_SPONSORS_PREF);
});

add_task(async function test_filterBlockedSponsors_invalid_pref() {
  info(
    "TopSitesFeed._filterBlockedSponsors should treat an invalid pref value " +
      "as an empty blocklist and return all tiles"
  );
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  Services.prefs.setStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF, "");

  let tiles = [{ url: "https://foo.com" }, { url: "https://bar.com" }];
  let result = feed._contile._filterBlockedSponsors(tiles);
  Assert.deepEqual(result, tiles);

  Services.prefs.clearUserPref(TOP_SITES_BLOCKED_SPONSORS_PREF);
  sandbox.restore();
});

add_task(async function test_readDefaults_invalid_blocked_sponsors_pref() {
  info(
    "TopSitesFeed._readDefaults should not throw when the blocked sponsors " +
      "pref contains invalid JSON"
  );
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  Services.prefs.setBoolPref("browser.topsites.useRemoteSetting", true);
  Services.prefs.setStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF, "");
  sandbox.stub(feed, "_getRemoteConfig").resolves([]);

  await feed._readDefaults();
  Assert.ok(true, "No exception thrown with invalid blocked sponsors pref");

  Services.prefs.clearUserPref("browser.topsites.useRemoteSetting");
  Services.prefs.clearUserPref(TOP_SITES_BLOCKED_SPONSORS_PREF);
  sandbox.restore();
});

// Guards the fragile pinnedLinks._links write in _saveGroupedPins: the k-th pin
// must land in the k-th occupied slot so the sparse hole pattern is preserved for
// the classic renderer, extras append densely, and removing a pin remaps cleanly.
add_task(async function test_saveGroupedPins_preserves_hole_pattern() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed.pinnedCache = { expire: sandbox.stub() };

  const A = { url: "https://a.com" };
  const B = { url: "https://b.com" };
  const C = { url: "https://c.com" };
  const D = { url: "https://d.com" };

  // Holes serialize to null on persistence, so normalize the sparse _links the
  // same way before comparing.
  const persisted = () =>
    JSON.parse(JSON.stringify(NewTabUtils.pinnedLinks._links));

  const origLinks = NewTabUtils.pinnedLinks._links;
  // Sparse layout: pins occupy slots 0, 2, 4 with holes at 1 and 3.
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [A, null, B, null, C]);
  let saveStub = sandbox.stub(NewTabUtils.pinnedLinks, "save");

  // Reorder within the group: each pin maps to the k-th occupied slot, gaps stay.
  feed._saveGroupedPins([B, A, C]);
  Assert.deepEqual(
    persisted(),
    [B, null, A, null, C],
    "reorder maps k-th pin to k-th occupied slot, holes preserved"
  );
  Assert.ok(saveStub.called, "persisted via save()");

  // A pin beyond the occupied slots appends densely after the last one.
  feed._saveGroupedPins([A, B, C, D]);
  Assert.deepEqual(
    persisted(),
    [A, null, B, null, C, D],
    "extra pin appends densely after the existing hole pattern"
  );

  // Toggling a pin off remaps the remaining group onto the leading slots.
  feed._saveGroupedPins([A, C]);
  Assert.deepEqual(
    persisted(),
    [A, null, C],
    "removing a pin remaps remaining pins to the first occupied slots"
  );

  NewTabUtils.pinnedLinks._links = origLinks;
  sandbox.restore();
});

// True if the feed dispatched a topSitesRows grow (optionally to a given value).
function dispatchedRowGrow(feed, value) {
  return feed.store.dispatch.calledWithMatch({
    data:
      value === undefined
        ? { name: "topSitesRows" }
        : { name: "topSitesRows", value },
  });
}

add_task(async function test_pinSiteAtGrouped_growsRowWhenFullOfPins() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  // _groupedPins derives from pinnedLinks.links; an empty group is enough here
  // since the appended url isn't already pinned.
  sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => []);
  sandbox.stub(feed, "_saveGroupedPins");
  sandbox.stub(feed, "_clearLinkCustomScreenshot").resolves();

  // A pinned slot at getTopSitesCount - 1 means the visible grid is full of pins.
  let setGrid = (rows, lastPinned) => {
    feed.store.dispatch.resetHistory();
    feed.store.state.Prefs.values = {
      topSitesRows: rows,
      topSitesMaxSitesPerRow: 8,
    };
    feed.store.state.TopSites.rows = Array(rows * 8).fill("site");
    if (lastPinned) {
      feed.store.state.TopSites.rows[rows * 8 - 1] = { isPinned: true };
    }
  };

  info("grows a row when the visible grid is full of pins, below max rows");
  setGrid(1, true);
  await feed._pinSiteAtGrouped({ url: "https://new1.com" });
  Assert.ok(dispatchedRowGrow(feed, 2), "grew to keep the new pin visible");

  info("does not grow when the last visible slot is a frecency tile");
  setGrid(1, false);
  await feed._pinSiteAtGrouped({ url: "https://new2.com" });
  Assert.ok(
    !dispatchedRowGrow(feed),
    "no grow: a frecency tile can be pushed off instead"
  );

  info("does not grow once already at the max rows");
  setGrid(4, true);
  await feed._pinSiteAtGrouped({ url: "https://new3.com" });
  Assert.ok(!dispatchedRowGrow(feed), "no grow at the max rows");

  sandbox.restore();
});

add_task(async function test_pin_replacesLastFrecencyTileOnFullRow() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  // Classic mode (grouped pref unset). Stub side effects so only pin()'s
  // eviction/grow decision is under test. Identity index adjustment keeps the
  // asserted pin slot readable (no preceding sponsored tiles to shift over).
  sandbox.stub(feed, "_adjustPinIndexForSponsoredLinks").callsFake((s, i) => i);
  let pinStub = sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  sandbox.stub(feed, "_clearLinkCustomScreenshot").resolves();
  sandbox.stub(feed, "_broadcastPinnedSitesUpdated");

  // The add button on a full row sends the slot just past the visible grid.
  let pinPastGrid = (rows, gridRows) => {
    feed.store.dispatch.resetHistory();
    pinStub.resetHistory();
    feed.store.state.Prefs.values = {
      topSitesRows: gridRows,
      topSitesMaxSitesPerRow: 8,
    };
    feed.store.state.TopSites.rows = rows;
    return feed.pin({
      data: { site: { url: "https://new.com" }, index: gridRows * 8 },
    });
  };

  info("replaces the last frecency tile instead of growing a row");
  await pinPastGrid(Array(8).fill("site"), 1);
  Assert.ok(!dispatchedRowGrow(feed), "no grow: a frecency tile was replaced");
  Assert.equal(
    pinStub.getCall(0).args[1],
    7,
    "pinned at the last frecency slot"
  );

  info("replaces the last frecency tile even when a later slot is pinned");
  let scattered = Array(8).fill("site");
  scattered[7] = { isPinned: true };
  await pinPastGrid(scattered, 1);
  Assert.ok(
    !dispatchedRowGrow(feed),
    "no grow: an earlier frecency tile was replaced"
  );
  Assert.equal(
    pinStub.getCall(0).args[1],
    6,
    "pinned at the last frecency slot, not the last position"
  );

  info("skips sponsored tiles when choosing the tile to replace");
  let withSponsored = Array(8).fill("site");
  withSponsored[7] = { sponsored_position: 1 };
  await pinPastGrid(withSponsored, 1);
  Assert.equal(
    pinStub.getCall(0).args[1],
    6,
    "did not replace the sponsored tile"
  );

  info("grows a row when every visible slot is pinned or sponsored");
  await pinPastGrid(Array(8).fill({ isPinned: true }), 1);
  Assert.ok(dispatchedRowGrow(feed, 2), "grew: no frecency tile to replace");

  info("does not grow once already at the max rows");
  await pinPastGrid(Array(32).fill({ isPinned: true }), 4);
  Assert.ok(!dispatchedRowGrow(feed), "no grow at the max rows");

  sandbox.restore();
});

// Re-pinning a grouped site with a changed custom screenshot URL must clear the
// stale cached screenshot. Regression test for the redundant pinnedCache.expire()
// in _saveGroupedPins, which made _clearLinkCustomScreenshot compare new===new.
add_task(async function test_pinSiteAtGrouped_clears_stale_custom_screenshot() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);

  const url = "https://example.com/";
  const origLinks = NewTabUtils.pinnedLinks._links;
  // Live backing array so a (buggy) expire would resurface the freshly-saved pin.
  NewTabUtils.pinnedLinks._links = [{ url, customScreenshotURL: "old-shot" }];
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => NewTabUtils.pinnedLinks._links);
  sandbox.stub(NewTabUtils.pinnedLinks, "save");

  // Prime the cache and stamp a screenshot on the pinned link.
  let pinned = await feed.pinnedCache.request();
  pinned[0].__sharedCache.updateLink("screenshot", "cached-screenshot");

  // Edit: re-pin the same site with a different custom screenshot URL.
  await feed._pinSiteAtGrouped({ url, customScreenshotURL: "new-shot" });

  pinned = await feed.pinnedCache.request();
  Assert.equal(
    pinned.find(p => p && p.url === url).screenshot,
    undefined,
    "stale custom screenshot cleared when re-pinning with a new custom URL"
  );

  NewTabUtils.pinnedLinks._links = origLinks;
  sandbox.restore();
});

// Editing a grouped pin's URL must replace that pin in place rather than
// appending a second tile and leaving the original behind (bug 2073040).
add_task(async function test_pin_grouped_edit_changed_url_replaces_pin() {
  let sandbox = sinon.createSandbox();
  let feed = getTopSitesFeedForTest(sandbox);
  feed.store.state.Prefs.values = {
    topSitesRows: 1,
    topSitesMaxSitesPerRow: 8,
    topSitesGroupedPins: true,
  };
  sandbox.stub(feed, "_broadcastPinnedSitesUpdated");
  let pinnedLinks = [];
  sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => pinnedLinks);
  sandbox.stub(NewTabUtils.pinnedLinks, "save");
  let saved;
  sandbox.stub(feed, "_saveGroupedPins").callsFake(pins => (saved = pins));

  const A = { url: "https://a.com" };
  const B = { url: "https://b.com" };
  const C = { url: "https://c.com" };
  const B2 = { url: "https://b2.com" };
  const S = { url: "https://search.com", searchTopSite: true };
  const setPins = (links, rows) => {
    pinnedLinks = links;
    feed._linksWithDefaults = rows.map(link => ({ ...link, isPinned: true }));
  };

  info("changing a pin's URL replaces it at the same group position");
  setPins([A, B, C], [A, B, C]);
  await feed.pin({ data: { site: B2, index: 1 } });
  Assert.deepEqual(saved, [A, B2, C]);

  info("a hidden pin ahead of the edited one doesn't shift the target");
  setPins([S, A, B], [A, B]);
  await feed.pin({ data: { site: B2, index: 1 } });
  Assert.deepEqual(saved, [S, A, B2]);

  info("editing to a URL that's already pinned leaves one copy");
  setPins([A, B, C], [A, B, C]);
  await feed.pin({ data: { site: C, index: 1 } });
  Assert.deepEqual(saved, [A, C]);

  sandbox.restore();
});

add_task(async function test_fetchSites_callsAdsClientWhenEnabled() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(AdsClient, "isEnabled").returns(true);

  const ADS_CLIENT = {
    requestTileAds: sinon.fake.resolves(
      new Map([
        [
          "newtab_tile_1",
          {
            blockKey: "block1",
            name: "Tile 1",
            url: "https://tile.example/",
            imageUrl: "https://tile.example/img.png",
            callbacks: {
              click: "https://tile.example/click",
              impression: "https://tile.example/impression",
            },
          },
        ],
      ])
    ),
  };
  sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);

  const REQUEST_OPTIONS = {
    flags: new Map([
      ["feature_1", true],
      ["feature_2", false],
    ]),
    ohttp: true,
  };
  sandbox.stub(AdsClient, "requestOptions").returns(REQUEST_OPTIONS);
  const RELEVANCE = { t1: 0.3 };
  const getScoresStub = sandbox
    .stub(SponsoredTopSitesScoreProvider, "getScores")
    .resolves(RELEVANCE);
  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled",
    true
  );
  Services.prefs.setStringPref(
    "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
    "https://relay.url"
  );
  Services.prefs.setStringPref(
    "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
    "https://config.url"
  );

  sandbox.stub(TopSitesFeed.prototype, "_readDefaults").returns();
  sandbox.stub(NimbusFeatures.newtab, "getVariable").returns(true);

  const feed = getTopSitesFeedForTest(sandbox);
  feed.store.state.Prefs.values.showSponsoredTopSites = true;
  feed.store.state.Prefs.values["unifiedAds.tiles.enabled"] = true;
  feed.store.state.Prefs.values["discoverystream.placements.tiles"] =
    "newtab_tile_1";
  feed.store.state.Prefs.values["discoverystream.placements.tiles.counts"] =
    "1";

  await feed.onAction({
    type: actionTypes.INIT,
  });
  Assert.ok(AdsClient.isEnabled.calledOnce);
  Assert.ok(AdsClient.getClient.calledOnce);

  await feed.onAction({
    type: actionTypes.TOP_SITES_UPDATED,
  });

  // The flags in adsBackendConfig only reach MARS if the prefs are passed.
  Assert.ok(
    AdsClient.requestOptions.calledOnceWithExactly(
      feed.store.getState().Prefs.values,
      "duckduckgo",
      RELEVANCE
    ),
    "The prefs, blocks, and relevance scores were passed to requestOptions"
  );
  Assert.ok(getScoresStub.calledOnce, "The scores were read once");
  Assert.ok(
    ADS_CLIENT.requestTileAds.calledOnceWithExactly(
      sinon.match.any,
      REQUEST_OPTIONS
    )
  );

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL"
  );
  sandbox.restore();
});

add_task(async function test_normalizeTileData_sorts() {
  let sandbox = sinon.createSandbox();

  const placements = [0, 1, 2, 3].map(i => ({
    id: `placement_${i}`,
    raw: [
      {
        block_key: `key_${i}`,
        name: `name_${i}`,
        url: `url_${i}`,
        image_url: `image_${i}`,
        callbacks: {
          click: `click_${i}`,
          impression: `impression_${i}`,
        },
      },
    ],
    normalized: {
      id: `key_${i}`,
      block_key: `key_${i}`,
      name: `name_${i}`,
      url: `url_${i}`,
      click_url: `click_${i}`,
      image_url: `image_${i}`,
      impression_url: `impression_${i}`,
      image_size: 200,
      attribution: null,
    },
  }));

  const feed = getTopSitesFeedForTest(sandbox);

  Assert.deepEqual(
    feed._contile._normalizeTileData({}, []),
    { tiles: [] },
    "_normalizeTileData should return empty tiles with no placements."
  );

  Assert.deepEqual(
    feed._contile._normalizeTileData(
      { [placements[0].id]: placements[0].raw },
      []
    ),
    { tiles: [placements[0].normalized] },
    "_normalizeTileData should return single tiles with single placement empty array."
  );

  Assert.deepEqual(
    feed._contile._normalizeTileData(
      { [placements[0].id]: placements[0].raw },
      [placements[0].id]
    ),
    { tiles: [placements[0].normalized] },
    "_normalizeTileData should return single tiles with single placement in array."
  );

  Assert.deepEqual(
    feed._contile._normalizeTileData(
      { [placements[0].id]: placements[0].raw },
      [placements[1].id]
    ),
    { tiles: [placements[0].normalized] },
    "_normalizeTileData should return single tiles with single placement not in array."
  );

  Assert.deepEqual(
    feed._contile._normalizeTileData(
      {
        [placements[0].id]: placements[0].raw,
        [placements[3].id]: placements[3].raw,
        [placements[1].id]: placements[1].raw,
        [placements[2].id]: placements[2].raw,
      },
      [placements[0].id, placements[1].id, placements[2].id, placements[3].id]
    ),
    {
      tiles: [
        placements[0].normalized,
        placements[1].normalized,
        placements[2].normalized,
        placements[3].normalized,
      ],
    },
    "_normalizeTileData should return sort placements by order in array."
  );

  Assert.deepEqual(
    feed._contile._normalizeTileData(
      {
        [placements[0].id]: placements[0].raw,
        [placements[3].id]: placements[3].raw,
        [placements[1].id]: placements[1].raw,
        [placements[2].id]: placements[2].raw,
      },
      [placements[0].id, placements[1].id]
    ),
    {
      tiles: [
        placements[0].normalized,
        placements[1].normalized,
        placements[3].normalized,
        placements[2].normalized,
      ],
    },
    "_normalizeTileData should return sort placements by order in array, leave others in place."
  );

  sandbox.restore();
});
