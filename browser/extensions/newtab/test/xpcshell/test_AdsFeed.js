/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  AdsFeed: "resource://newtab/lib/AdsFeed.sys.mjs",
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const { ObliviousHTTP } = ChromeUtils.importESModule(
  "resource://gre/modules/ObliviousHTTP.sys.mjs"
);

// Prefs to enable UAPI ad types
const PREF_UNIFIED_ADS_ADSFEED_ENABLED = "unifiedAds.adsFeed.enabled";
const PREF_UNIFIED_ADS_TILES_ENABLED = "unifiedAds.tiles.enabled";
const PREF_UNIFIED_ADS_SPOCS_ENABLED = "unifiedAds.spocs.enabled";
const PREF_UNIFIED_ADS_ENDPOINT = "unifiedAds.endpoint";
const PREF_TILES_PLACEMENTS = "discoverystream.placements.tiles";
const PREF_TILES_COUNTS = "discoverystream.placements.tiles.counts";
const PREF_SPOC_COUNTS = "discoverystream.placements.spocs.counts";
const PREF_SPOC_PLACEMENTS = "discoverystream.placements.spocs";
const PREF_UNIFIED_ADS_BLOCKED_LIST = "unifiedAds.blockedAds";

// Note: Full pref path required by Services.prefs.setBoolPref
const PREF_UNIFIED_ADS_OHTTP_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled";
const PREF_UNIFIED_ADS_OHTTP_RELAY_URL =
  "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL";
const PREF_UNIFIED_ADS_OHTTP_CONFIG_URL =
  "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL";

// Primary pref that is toggled when enabling top site sponsored tiles
const PREF_FEED_TOPSITES = "feeds.topsites";
const PREF_SYSTEM_TOPSITES = "feeds.system.topsites";
const PREF_SHOW_SPONSORED_TOPSITES = "showSponsoredTopSites";

// Primary pref that is toggled when enabling sponsored stories
const PREF_FEED_SECTIONS_TOPSTORIES = "feeds.section.topstories";
const PREF_SYSTEM_TOPSTORIES = "feeds.system.topstories";
const PREF_SHOW_SPONSORED = "showSponsored";
const PREF_SYSTEM_SHOW_SPONSORED = "system.showSponsored";

// Calculated pref from nimbus experiments
const PREF_ADS_BACKEND_CONFIG = "adsBackendConfig";
const PREF_ADS_BACKEND_CONFIG_FLAG_TRUE = "flag_true";
const PREF_ADS_BACKEND_CONFIG_FLAG_FALSE = "flag_false";

const mockedTileData = [
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

const mockedFetchTileData = {
  newtab_tile_1: [
    {
      format: "tile",
      url: "https://www.test1.com",
      callbacks: {
        click: "https://www.test1-click.com",
        impression: "https://www.test1-impression.com",
        report: "https://www.test1-report.com",
      },
      image_url: "images/test1-com.png",
      name: "test1",
      block_key: "test1",
    },
  ],
  newtab_tile_2: [
    {
      format: "tile",
      url: "https://www.test2.com",
      callbacks: {
        click: "https://www.test2-click.com",
        impression: "https://www.test2-impression.com",
        report: "https://www.test2-report.com",
      },
      image_url: "images/test2-com.png",
      name: "test2",
      block_key: "test2",
    },
  ],
  newtab_tile_3: [
    {
      format: "tile",
      url: "https://www.test3.com",
      callbacks: {
        click: "https://www.test3-click.com",
        impression: "https://www.test3-impression.com",
        report: "https://www.test3-report.com",
      },
      image_url: "images/test3-com.png",
      name: "test3",
      block_key: "test3",
    },
  ],
};

const mockedSpocPlacements = [
  {
    placement: "newtab_spocs",
    count: 6,
  },
];

const mockedSpocsData = [
  {
    format: "spoc",
    url: "https://www.test.com",
    callbacks: {
      click: "https://www.test-click.com",
      impression: "https://www.test-impression.com",
      report: "https://www.test-report.com",
    },
    image_url: "images/test-com.png",
    title: "test",
    domain: "test.com",
    excerpt: "Lorem ipsum dolor sit amet.",
    sponsor: "Test",
    block_key: "test",
    caps: {
      cap_key: "123456789",
      day: 10,
    },
    ranking: {
      priority: 9,
      personalization_models: {},
      item_score: 1,
    },
  },
  {
    format: "spoc",
    url: "https://www.test1.com",
    callbacks: {
      click: "https://www.test1-click.com",
      impression: "https://www.test1-impression.com",
      report: "https://www.test1-report.com",
    },
    image_url: "images/test1-com.png",
    title: "test1",
    domain: "test1.com",
    excerpt: "Lorem ipsum dolor sit amet.",
    sponsor: "Test1",
    block_key: "test1",
    caps: {
      cap_key: "234567891",
      day: 10,
    },
    ranking: {
      priority: 9,
      personalization_models: {},
      item_score: 1,
    },
  },
  {
    format: "spoc",
    url: "https://www.test2.com",
    callbacks: {
      click: "https://www.test2-click.com",
      impression: "https://www.test2-impression.com",
      report: "https://www.test2-report.com",
    },
    image_url: "images/test2-com.png",
    title: "test2",
    domain: "test2.com",
    excerpt: "Lorem ipsum dolor sit amet.",
    sponsor: "Test2",
    block_key: "test2",
    caps: {
      cap_key: "345678912",
      day: 10,
    },
    ranking: {
      priority: 9,
      personalization_models: {},
      item_score: 1,
    },
  },
  {
    format: "spoc",
    url: "https://www.test3.com",
    callbacks: {
      click: "https://www.test3-click.com",
      impression: "https://www.test3-impression.com",
      report: "https://www.test3-report.com",
    },
    image_url: "images/test3-com.png",
    title: "test3",
    domain: "test3.com",
    excerpt: "Lorem ipsum dolor sit amet.",
    sponsor: "Test3",
    block_key: "test3",
    caps: {
      cap_key: "456789123",
      day: 10,
    },
    ranking: {
      priority: 9,
      personalization_models: {},
      item_score: 1,
    },
  },
  {
    format: "spoc",
    url: "https://www.test4.com",
    callbacks: {
      click: "https://www.test4-click.com",
      impression: "https://www.test4-impression.com",
      report: "https://www.test4-report.com",
    },
    image_url: "images/test4-com.png",
    title: "test4",
    domain: "test4.com",
    excerpt: "Lorem ipsum dolor sit amet.",
    sponsor: "Test4",
    block_key: "test4",
    caps: {
      cap_key: "567891234",
      day: 10,
    },
    ranking: {
      priority: 9,
      personalization_models: {},
      item_score: 1,
    },
  },
  {
    format: "spoc",
    url: "https://www.test5.com",
    callbacks: {
      click: "https://www.test5-click.com",
      impression: "https://www.test5-impression.com",
      report: "https://www.test5-report.com",
    },
    image_url: "images/test5-com.png",
    title: "test5",
    domain: "test5.com",
    excerpt: "Lorem ipsum dolor sit amet.",
    sponsor: "Test5",
    block_key: "test5",
    caps: {
      cap_key: "678912345",
      day: 10,
    },
    ranking: {
      priority: 9,
      personalization_models: {},
      item_score: 1,
    },
  },
];

function getAdsFeedForTest() {
  let feed = new AdsFeed();
  let tiles = mockedTileData;
  let spocs = mockedSpocsData;
  let spocPlacements = mockedSpocPlacements;

  feed.store = {
    dispatch: sinon.spy(),
    getState() {
      return this.state;
    },
    state: {
      tiles,
      spocs,
      spocPlacements,
      lastUpdated: 1,
      Prefs: {
        values: {
          [PREF_TILES_PLACEMENTS]:
            "newtab_tile_1, newtab_tile_2, newtab_tile_3",
          [PREF_TILES_COUNTS]: "1, 1, 1",
          [PREF_SPOC_PLACEMENTS]: "newtab_spocs",
          [PREF_SPOC_COUNTS]: "6",
          [PREF_UNIFIED_ADS_BLOCKED_LIST]: "",
          [PREF_UNIFIED_ADS_ENDPOINT]: "https://example.com/",
          // AdsFeed/UAPI specific prefs to test
          [PREF_UNIFIED_ADS_TILES_ENABLED]: false,
          [PREF_UNIFIED_ADS_SPOCS_ENABLED]: false,
          [PREF_UNIFIED_ADS_ADSFEED_ENABLED]: false,
          // Default display prefs for tiles/spocs
          [PREF_FEED_TOPSITES]: true,
          [PREF_SYSTEM_TOPSITES]: true,
          [PREF_SHOW_SPONSORED_TOPSITES]: true,
          [PREF_FEED_SECTIONS_TOPSTORIES]: true,
          [PREF_SYSTEM_TOPSTORIES]: true,
          [PREF_SHOW_SPONSORED]: true,
          [PREF_SYSTEM_SHOW_SPONSORED]: true,
          [PREF_ADS_BACKEND_CONFIG]: {
            [PREF_ADS_BACKEND_CONFIG_FLAG_TRUE]: true,
            [PREF_ADS_BACKEND_CONFIG_FLAG_FALSE]: false,
          },
        },
      },
    },
  };

  return feed;
}

add_task(async function test_construction() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = new AdsFeed();

  info("AdsFeed constructor should create initial values");

  Assert.ok(feed, "Could construct a AdsFeed");
  Assert.strictEqual(feed.loaded, false, "AdsFeed is not loaded");
  Assert.strictEqual(feed.enabled, false, "AdsFeed is not enabled");
  Assert.strictEqual(
    feed.lastUpdated,
    null,
    "AdsFeed has no lastUpdated record"
  );
  Assert.strictEqual(
    feed.tiles.length,
    0,
    "tiles is initialized as a array with length of 0"
  );
  sandbox.restore();
});

add_task(async function test_isEnabled_tiles() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });
  const dateNowTestValue = 1;
  sandbox.stub(AdsFeed.prototype, "Date").returns({
    now: () => dateNowTestValue,
  });

  let feed = getAdsFeedForTest(sandbox);

  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_TILES_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_FEED_TOPSITES] = true;
  feed.store.state.Prefs.values[PREF_SYSTEM_TOPSITES] = true;
  feed.store.state.Prefs.values[PREF_SHOW_SPONSORED_TOPSITES] = true;
  feed.store.state.Prefs.values[PREF_FEED_SECTIONS_TOPSTORIES] = true;
  feed.store.state.Prefs.values[PREF_SYSTEM_TOPSTORIES] = true;
  feed.store.state.Prefs.values[PREF_SHOW_SPONSORED] = true;
  feed.store.state.Prefs.values[PREF_SYSTEM_SHOW_SPONSORED] = true;

  Assert.ok(feed.isEnabled());

  sandbox.restore();
});

add_task(async function test_onAction_INIT_tiles_and_spocs() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });
  const dateNowTestValue = 1;
  sandbox.stub(AdsFeed.prototype, "Date").returns({
    now: () => dateNowTestValue,
  });

  let feed = getAdsFeedForTest(sandbox);

  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_TILES_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED] = true;

  sandbox.stub(feed, "isEnabled").returns(true);

  sandbox.stub(feed, "fetchData").returns({
    tiles: mockedTileData,
    spocs: mockedSpocsData,
    spocPlacements: mockedSpocPlacements,
    lastUpdated: dateNowTestValue,
  });

  info("AdsFeed.onAction INIT should initialize Ads");

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.ok(feed.store.dispatch.calledTwice);

  Assert.ok(
    feed.store.dispatch.firstCall.calledWithExactly(
      actionCreators.BroadcastToContent({
        type: "ADS_UPDATE_TILES",
        data: {
          tiles: mockedTileData,
        },
        meta: {
          isStartup: true,
        },
      })
    )
  );

  Assert.ok(
    feed.store.dispatch.secondCall.calledWithExactly(
      actionCreators.BroadcastToContent({
        type: "ADS_UPDATE_SPOCS",
        data: {
          spocs: mockedSpocsData,
          spocPlacements: mockedSpocPlacements,
        },
        meta: {
          isStartup: true,
        },
      })
    )
  );

  sandbox.restore();
});

add_task(async function test_onAction_INIT_spocs_only() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });
  const dateNowTestValue = 1;
  sandbox.stub(AdsFeed.prototype, "Date").returns({
    now: () => dateNowTestValue,
  });

  let feed = getAdsFeedForTest(sandbox);

  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED] = true;

  sandbox.stub(feed, "isEnabled").returns(true);

  sandbox.stub(feed, "fetchData").returns({
    tiles: [],
    spocs: mockedSpocsData,
    spocPlacements: mockedSpocPlacements,
    lastUpdated: dateNowTestValue,
  });

  info(
    "AdsFeed.onAction INIT should return only spocs when tiles are disabled"
  );

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.ok(feed.store.dispatch.calledOnce);
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.BroadcastToContent({
        type: "ADS_UPDATE_SPOCS",
        data: {
          spocs: mockedSpocsData,
          spocPlacements: mockedSpocPlacements,
        },
        meta: {
          isStartup: true,
        },
      })
    )
  );

  sandbox.restore();
});

add_task(async function test_onAction_INIT_tiles_only() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });
  const dateNowTestValue = 1;
  sandbox.stub(AdsFeed.prototype, "Date").returns({
    now: () => dateNowTestValue,
  });

  let feed = getAdsFeedForTest(sandbox);

  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED] = true;
  feed.store.state.Prefs.values[PREF_UNIFIED_ADS_TILES_ENABLED] = true;

  sandbox.stub(feed, "isEnabled").returns(true);

  sandbox.stub(feed, "fetchData").returns({
    tiles: mockedTileData,
    spocs: [],
    spocPlacements: [],
    lastUpdated: dateNowTestValue,
  });

  info(
    "AdsFeed.onAction INIT should return only tiles when spocs are disabled"
  );

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.ok(feed.store.dispatch.calledOnce);
  Assert.ok(
    feed.store.dispatch.calledWithExactly(
      actionCreators.BroadcastToContent({
        type: "ADS_UPDATE_TILES",
        data: {
          tiles: mockedTileData,
        },
        meta: {
          isStartup: true,
        },
      })
    )
  );

  sandbox.restore();
});

add_task(async function test_fetchData_noOHTTP() {
  const sandbox = sinon.createSandbox();
  const feed = getAdsFeedForTest();

  sandbox
    .stub(AdsFeed.prototype, "PersistentCache")
    .returns({ get: () => {}, set: () => {} });
  sandbox.stub(feed, "fetch").resolves(
    new Response(
      JSON.stringify({
        tile1: [
          {
            block_key: "foo",
            name: "bar",
            url: "https://test.com",
            image_url: "image.png",
            callbacks: { click: "click", impression: "impression" },
          },
        ],
      })
    )
  );

  sandbox.stub(feed, "Date").returns({ now: () => 123 });

  // Simulate OHTTP being disabled
  Services.prefs.setBoolPref(PREF_UNIFIED_ADS_OHTTP_ENABLED, false);

  const supportedAdTypes = { tiles: true, spocs: false };
  await feed.fetchData(supportedAdTypes);

  info("AdsFeed: fetchData() should fetch normally via HTTP");

  Assert.ok(feed.fetch.calledOnce, "Fallback fetch called");
  sandbox.restore();
});

add_task(async function test_fetchData_OHTTP() {
  const sandbox = sinon.createSandbox();
  const CONTEXT_ID = "ContextId";
  sandbox.stub(ContextId, "request").resolves(CONTEXT_ID);
  const feed = getAdsFeedForTest();

  Services.prefs.setBoolPref(PREF_UNIFIED_ADS_OHTTP_ENABLED, true);
  Services.prefs.setStringPref(
    PREF_UNIFIED_ADS_OHTTP_RELAY_URL,
    "https://relay.test"
  );
  Services.prefs.setStringPref(
    PREF_UNIFIED_ADS_OHTTP_CONFIG_URL,
    "https://config.test"
  );

  const mockConfig = { config: "mocked" };

  sandbox
    .stub(AdsFeed.prototype, "PersistentCache")
    .returns({ get: () => {}, set: () => {} });
  sandbox.stub(feed, "Date").returns({ now: () => 123 });

  sandbox.stub(ObliviousHTTP, "getOHTTPConfig").resolves(mockConfig);
  sandbox.stub(ObliviousHTTP, "ohttpRequest").resolves({
    status: 200,
    json: () => {
      return Promise.resolve(mockedFetchTileData);
    },
  });

  const result = await feed.fetchData({ tiles: true, spocs: false });

  info("AdsFeed: fetchData() should fetch via OHTTP when enabled");

  Assert.ok(ObliviousHTTP.getOHTTPConfig.calledOnce);
  Assert.ok(ObliviousHTTP.ohttpRequest.calledOnce);
  Assert.deepEqual(result.tiles[0].id, "test1");

  info("AdsFeed: fetchData() should not send cookies");
  Assert.equal(
    ObliviousHTTP.ohttpRequest.firstCall.args[3].credentials,
    "omit",
    "should not send cookies"
  );

  Assert.equal(
    Object.keys(ObliviousHTTP.ohttpRequest.firstCall.args[3].headers).join(),
    "content-type",
    "should send headers that ohttpRequest can enumerate"
  );

  info("AdsFeed: fetchData() should construct request body");
  Assert.equal(
    ObliviousHTTP.ohttpRequest.firstCall.args[3].body,
    JSON.stringify({
      context_id: CONTEXT_ID,
      flags: {
        [PREF_ADS_BACKEND_CONFIG_FLAG_TRUE]: true,
        [PREF_ADS_BACKEND_CONFIG_FLAG_FALSE]: false,
      },
      placements: [
        {
          placement: "newtab_tile_1",
          count: 1,
        },
        {
          placement: "newtab_tile_2",
          count: 1,
        },
        {
          placement: "newtab_tile_3",
          count: 1,
        },
      ],
      blocks: [""],
    })
  );

  sandbox.restore();
});

add_task(async function test_fetchData_OHTTP_with_adsBackendConfig() {
  const sandbox = sinon.createSandbox();
  const CONTEXT_ID = "ContextId";
  sandbox.stub(ContextId, "request").resolves(CONTEXT_ID);
  const feed = getAdsFeedForTest();

  Services.prefs.setBoolPref(PREF_UNIFIED_ADS_OHTTP_ENABLED, true);
  Services.prefs.setStringPref(
    PREF_UNIFIED_ADS_OHTTP_RELAY_URL,
    "https://relay.test"
  );
  Services.prefs.setStringPref(
    PREF_UNIFIED_ADS_OHTTP_CONFIG_URL,
    "https://config.test"
  );

  const mockConfig = { config: "mocked" };

  sandbox
    .stub(AdsFeed.prototype, "PersistentCache")
    .returns({ get: () => {}, set: () => {} });
  sandbox.stub(feed, "Date").returns({ now: () => 123 });

  sandbox.stub(ObliviousHTTP, "getOHTTPConfig").resolves(mockConfig);
  sandbox.stub(ObliviousHTTP, "ohttpRequest").resolves({
    status: 200,
    json: () => {
      return Promise.resolve(mockedFetchTileData);
    },
  });

  const result = await feed.fetchData({ tiles: true, spocs: false });

  info("AdsFeed: fetchData() should fetch via OHTTP when enabled");

  Assert.ok(ObliviousHTTP.getOHTTPConfig.calledOnce);
  Assert.ok(ObliviousHTTP.ohttpRequest.calledOnce);
  Assert.deepEqual(result.tiles[0].id, "test1");

  info("AdsFeed: fetchData() should not send cookies");
  Assert.equal(
    ObliviousHTTP.ohttpRequest.firstCall.args[3].credentials,
    "omit",
    "should not send cookies"
  );

  info("AdsFeed: fetchData() should construct request body");
  Assert.equal(
    ObliviousHTTP.ohttpRequest.firstCall.args[3].body,
    JSON.stringify({
      context_id: CONTEXT_ID,
      flags: {
        [PREF_ADS_BACKEND_CONFIG_FLAG_TRUE]: true,
        [PREF_ADS_BACKEND_CONFIG_FLAG_FALSE]: false,
      },
      placements: [
        {
          placement: "newtab_tile_1",
          count: 1,
        },
        {
          placement: "newtab_tile_2",
          count: 1,
        },
        {
          placement: "newtab_tile_3",
          count: 1,
        },
      ],
      blocks: [""],
    })
  );

  sandbox.restore();
});

add_task(async function test_normalizeTileData_keepsReportUrl() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  const feed = getAdsFeedForTest();
  const { tiles } = feed._normalizeTileData(mockedFetchTileData);

  Assert.deepEqual(
    tiles.map(tile => tile.report_url),
    [
      "https://www.test1-report.com",
      "https://www.test2-report.com",
      "https://www.test3-report.com",
    ],
    "Each tile keeps its report callback"
  );

  sandbox.restore();
});

add_task(async function test_fetchWithAdsClient_mapsToLegacyShape() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

  let feed = getAdsFeedForTest();

  // Fake MozAdsClient returning canonical MozAdsTile/MozAdsSpoc objects.
  feed.adsClient = {
    requestTileAds: async () =>
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
              report: "https://tile.example/report",
            },
          },
        ],
      ]),
    requestSpocAds: async () =>
      new Map([
        [
          "newtab_spocs",
          [
            {
              format: "spoc",
              url: "https://spoc.example/",
              imageUrl: "https://spoc.example/img.png",
              callbacks: { click: "https://spoc.example/click" },
              title: "Spoc 1",
              domain: "spoc.example",
              excerpt: "Excerpt",
              sponsor: "Sponsor",
              sponsoredByOverride: null,
              blockKey: "spocblock1",
              caps: { capKey: "cap1", day: 5 },
              ranking: {
                itemScore: 0.5,
                personalizationModels: new Map([
                  ["arts_and_entertainment", 1],
                  ["travel", 1],
                ]),
                priority: 2,
              },
            },
          ],
        ],
      ]),
  };

  const returnData = await feed._fetchWithAdsClient(
    { tiles: true, spocs: true },
    [
      { placement: "newtab_tile_1", count: 1 },
      { placement: "newtab_spocs", count: 6 },
    ],
    {},
    {}
  );

  Assert.deepEqual(
    returnData.tiles,
    [
      {
        id: "block1",
        block_key: "block1",
        name: "Tile 1",
        url: "https://tile.example/",
        click_url: "https://tile.example/click",
        image_url: "https://tile.example/img.png",
        impression_url: "https://tile.example/impression",
        report_url: "https://tile.example/report",
        image_size: 200,
      },
    ],
    "Tiles are mapped from MozAdsTile to the legacy tile shape"
  );

  Assert.strictEqual(returnData.spocs.length, 1, "One spoc is returned");
  const [spoc] = returnData.spocs;
  Assert.strictEqual(spoc.block_key, "spocblock1", "spoc block_key mapped");
  Assert.strictEqual(
    spoc.image_url,
    "https://spoc.example/img.png",
    "spoc image_url mapped"
  );
  Assert.strictEqual(spoc.sponsored_by_override, null, "override mapped");
  Assert.deepEqual(spoc.caps, { cap_key: "cap1", day: 5 }, "caps mapped");
  Assert.deepEqual(
    spoc.ranking,
    {
      item_score: 0.5,
      personalization_models: {
        arts_and_entertainment: 1,
        travel: 1,
      },
      priority: 2,
    },
    "ranking mapped"
  );

  sandbox.restore();
});

add_task(async function test_fetchData_callsAdsClientWhenEnabled() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(AdsFeed.prototype, "PersistentCache").returns({
    set: () => {},
    get: () => {},
  });

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
    requestSpocAds: sinon.fake.resolves(
      new Map([
        [
          "newtab_spocs",
          [
            {
              format: "spoc",
              url: "https://spoc.example/",
              imageUrl: "https://spoc.example/img.png",
              callbacks: { click: "https://spoc.example/click" },
              title: "Spoc 1",
              domain: "spoc.example",
              excerpt: "Excerpt",
              sponsor: "Sponsor",
              sponsoredByOverride: null,
              blockKey: "spocblock1",
              caps: { capKey: "cap1", day: 5 },
              ranking: {
                itemScore: 0.5,
                personalizationModels: new Map([
                  ["arts_and_entertainment", 1],
                  ["travel", 1],
                ]),
                priority: 2,
              },
            },
          ],
        ],
      ])
    ),
  };
  sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
  const REQUEST_OPTIONS = {
    flags: new Map([
      [PREF_ADS_BACKEND_CONFIG_FLAG_TRUE, true],
      [PREF_ADS_BACKEND_CONFIG_FLAG_FALSE, false],
    ]),
    ohttp: true,
  };
  sandbox.stub(AdsClient, "requestOptions").returns(REQUEST_OPTIONS);

  const feed = getAdsFeedForTest();

  await feed.onAction({
    type: actionTypes.INIT,
  });
  Assert.ok(AdsClient.isEnabled.calledOnce);
  Assert.ok(AdsClient.getClient.calledOnce);

  await feed.fetchData({
    tiles: true,
    spocs: true,
  });

  Assert.ok(AdsClient.requestOptions.calledOnce);
  Assert.ok(
    ADS_CLIENT.requestTileAds.calledOnceWithExactly(
      sinon.match.any,
      REQUEST_OPTIONS
    )
  );
  Assert.ok(
    ADS_CLIENT.requestSpocAds.calledOnceWithExactly(
      sinon.match.any,
      REQUEST_OPTIONS
    )
  );

  sandbox.restore();
});
