import {
  actionCreators as ac,
  actionTypes as at,
  actionUtils as au,
} from "common/Actions.mjs";
import { combineReducers, createStore } from "redux";
import { GlobalOverrider } from "test/unit/utils";
import { DiscoveryStreamFeed } from "lib/DiscoveryStreamFeed.sys.mjs";
import { RecommendationProvider } from "lib/RecommendationProvider.sys.mjs";
import { reducers } from "common/Reducers.sys.mjs";

import { PersistentCache } from "lib/PersistentCache.sys.mjs";

const CONFIG_PREF_NAME = "discoverystream.config";
const ENDPOINTS_PREF_NAME = "discoverystream.endpoints";
const DUMMY_ENDPOINT = "https://getpocket.cdn.mozilla.net/dummy";
const SPOC_IMPRESSION_TRACKING_PREF = "discoverystream.spoc.impressions";
const THIRTY_MINUTES = 30 * 60 * 1000;
const ONE_WEEK = 7 * 24 * 60 * 60 * 1000; // 1 week

const FAKE_UUID = "{foo-123-foo}";

// eslint-disable-next-line max-statements
describe("DiscoveryStreamFeed", () => {
  let feed;
  let feeds;
  let recommendationProvider;
  let sandbox;
  let fetchStub;
  let clock;
  let fakeNewTabUtils;
  let fakePktApi;
  let globals;

  const setPref = (name, value) => {
    const action = {
      type: at.PREF_CHANGED,
      data: {
        name,
        value: typeof value === "object" ? JSON.stringify(value) : value,
      },
    };
    feed.store.dispatch(action);
    feed.onAction(action);
  };

  beforeEach(() => {
    sandbox = sinon.createSandbox();

    // Fetch
    fetchStub = sandbox.stub(global, "fetch");

    // Time
    clock = sinon.useFakeTimers();

    globals = new GlobalOverrider();
    globals.set({
      gUUIDGenerator: { generateUUID: () => FAKE_UUID },
      PersistentCache,
    });

    sandbox
      .stub(global.Services.prefs, "getBoolPref")
      .withArgs("browser.newtabpage.activity-stream.discoverystream.enabled")
      .returns(true);

    recommendationProvider = new RecommendationProvider();
    recommendationProvider.store = createStore(combineReducers(reducers), {});
    feeds = {
      "feeds.recommendationprovider": recommendationProvider,
    };

    // Feed
    feed = new DiscoveryStreamFeed();
    feed.store = createStore(combineReducers(reducers), {
      Prefs: {
        values: {
          [CONFIG_PREF_NAME]: JSON.stringify({
            enabled: false,
          }),
          [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
          "discoverystream.enabled": true,
          "feeds.section.topstories": true,
          "feeds.system.topstories": true,
          "discoverystream.spocs.personalized": true,
          "discoverystream.recs.personalized": true,
          "system.showSponsored": false,
          "discoverystream.spocs.startupCache.enabled": true,
        },
      },
    });
    feed.store.feeds = {
      get: name => feeds[name],
    };
    global.fetch.resetHistory();

    sandbox.stub(feed, "_maybeUpdateCachedData").resolves();

    globals.set("setTimeout", callback => {
      callback();
    });

    fakeNewTabUtils = {
      blockedLinks: {
        links: [],
        isBlocked: () => false,
      },
    };
    globals.set("NewTabUtils", fakeNewTabUtils);

    fakePktApi = {
      isUserLoggedIn: () => false,
      getRecentSavesCache: () => null,
      getRecentSaves: () => null,
    };
    globals.set("pktApi", fakePktApi);
  });

  afterEach(() => {
    clock.restore();
    sandbox.restore();
    globals.restore();
  });

  describe("#fetchFromEndpoint", () => {
    beforeEach(() => {
      feed._prefCache = {
        config: {
          api_key_pref: "",
        },
      };
      fetchStub.resolves({
        json: () => Promise.resolve("hi"),
        ok: true,
      });
    });
    it("should get a response", async () => {
      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      assert.equal(response, "hi");
    });
    it("should not send cookies", async () => {
      await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      assert.propertyVal(fetchStub.firstCall.args[1], "credentials", "omit");
    });
    it("should allow unexpected response", async () => {
      fetchStub.resolves({ ok: false });

      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      assert.equal(response, null);
    });
    it("should disallow unexpected endpoints", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            [ENDPOINTS_PREF_NAME]: "https://other.site",
          },
        },
      });

      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      assert.equal(response, null);
    });
    it("should allow multiple endpoints", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            [ENDPOINTS_PREF_NAME]: `https://other.site,${DUMMY_ENDPOINT}`,
          },
        },
      });

      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      assert.equal(response, "hi");
    });
    it("should ignore white-space added to multiple endpoints", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            [ENDPOINTS_PREF_NAME]: `https://other.site, ${DUMMY_ENDPOINT}`,
          },
        },
      });

      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      assert.equal(response, "hi");
    });
    it("should replace urls with $apiKey", async () => {
      sandbox.stub(global.Services.prefs, "getCharPref").returns("replaced");

      await feed.fetchFromEndpoint(
        "https://getpocket.cdn.mozilla.net/dummy?consumer_key=$apiKey"
      );

      assert.calledWithMatch(
        fetchStub,
        "https://getpocket.cdn.mozilla.net/dummy?consumer_key=replaced",
        { credentials: "omit" }
      );
    });
    it("should replace locales with $locale", async () => {
      feed.locale = "replaced";
      await feed.fetchFromEndpoint(
        "https://getpocket.cdn.mozilla.net/dummy?locale_lang=$locale"
      );

      assert.calledWithMatch(
        fetchStub,
        "https://getpocket.cdn.mozilla.net/dummy?locale_lang=replaced",
        { credentials: "omit" }
      );
    });
    it("should allow POST and with other options", async () => {
      await feed.fetchFromEndpoint("https://getpocket.cdn.mozilla.net/dummy", {
        method: "POST",
        body: "{}",
      });

      assert.calledWithMatch(
        fetchStub,
        "https://getpocket.cdn.mozilla.net/dummy",
        {
          credentials: "omit",
          method: "POST",
          body: "{}",
        }
      );
    });
  });

  describe("#setupPocketState", () => {
    it("should setup logged in state and recent saves with cache", async () => {
      fakePktApi.isUserLoggedIn = () => true;
      fakePktApi.getRecentSavesCache = () => [1, 2, 3];
      sandbox.spy(feed.store, "dispatch");
      await feed.setupPocketState({});
      assert.calledTwice(feed.store.dispatch);
      assert.calledWith(
        feed.store.dispatch.firstCall,
        ac.OnlyToOneContent(
          {
            type: at.DISCOVERY_STREAM_POCKET_STATE_SET,
            data: { isUserLoggedIn: true },
          },
          {}
        )
      );
      assert.calledWith(
        feed.store.dispatch.secondCall,
        ac.OnlyToOneContent(
          {
            type: at.DISCOVERY_STREAM_RECENT_SAVES,
            data: { recentSaves: [1, 2, 3] },
          },
          {}
        )
      );
    });
    it("should setup logged in state and recent saves without cache", async () => {
      fakePktApi.isUserLoggedIn = () => true;
      fakePktApi.getRecentSaves = ({ success }) => success([1, 2, 3]);
      sandbox.spy(feed.store, "dispatch");
      await feed.setupPocketState({});
      assert.calledTwice(feed.store.dispatch);
      assert.calledWith(
        feed.store.dispatch.firstCall,
        ac.OnlyToOneContent(
          {
            type: at.DISCOVERY_STREAM_POCKET_STATE_SET,
            data: { isUserLoggedIn: true },
          },
          {}
        )
      );
      assert.calledWith(
        feed.store.dispatch.secondCall,
        ac.OnlyToOneContent(
          {
            type: at.DISCOVERY_STREAM_RECENT_SAVES,
            data: { recentSaves: [1, 2, 3] },
          },
          {}
        )
      );
    });
  });

  describe("#getOrCreateImpressionId", () => {
    it("should create impression id in constructor", async () => {
      assert.equal(feed._impressionId, FAKE_UUID);
    });
    it("should create impression id if none exists", async () => {
      sandbox.stub(global.Services.prefs, "getCharPref").returns("");
      sandbox.stub(global.Services.prefs, "setCharPref").returns();

      const result = feed.getOrCreateImpressionId();

      assert.equal(result, FAKE_UUID);
      assert.calledOnce(global.Services.prefs.setCharPref);
    });
    it("should use impression id if exists", async () => {
      sandbox.stub(global.Services.prefs, "getCharPref").returns("from get");

      const result = feed.getOrCreateImpressionId();

      assert.equal(result, "from get");
      assert.calledOnce(global.Services.prefs.getCharPref);
    });
  });

  describe("#parseGridPositions", () => {
    it("should return an equivalent array for an array of non negative integers", async () => {
      assert.deepEqual(feed.parseGridPositions([0, 2, 3]), [0, 2, 3]);
    });
    it("should return undefined for an array containing negative integers", async () => {
      assert.equal(feed.parseGridPositions([-2, 2, 3]), undefined);
    });
    it("should return undefined for an undefined input", async () => {
      assert.equal(feed.parseGridPositions(undefined), undefined);
    });
  });

  describe("#loadLayout", () => {
    it("should use local basic layout with hardcoded_basic_layout being true", async () => {
      feed.config.hardcoded_basic_layout = true;

      await feed.loadLayout(feed.store.dispatch);

      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.spocs_endpoint,
        "https://spocs.getpocket.com/spocs"
      );
      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(layout[0].components[2].properties.items, 3);
    });
    it("should use 1 row layout if specified", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            [CONFIG_PREF_NAME]: JSON.stringify({
              enabled: true,
            }),
            [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
            "discoverystream.enabled": true,
            "discoverystream.region-basic-layout": true,
            "system.showSponsored": false,
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(layout[0].components[2].properties.items, 3);
    });
    it("should use 7 row layout if specified", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            [CONFIG_PREF_NAME]: JSON.stringify({
              enabled: true,
            }),
            [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
            "discoverystream.enabled": true,
            "discoverystream.region-basic-layout": false,
            "system.showSponsored": false,
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(layout[0].components[2].properties.items, 21);
    });
    it("should use new spocs endpoint if in the config", async () => {
      feed.config.spocs_endpoint = "https://spocs.getpocket.com/spocs2";

      await feed.loadLayout(feed.store.dispatch);

      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.spocs_endpoint,
        "https://spocs.getpocket.com/spocs2"
      );
    });
    it("should use local basic layout with FF pref hardcoded_basic_layout", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            [CONFIG_PREF_NAME]: JSON.stringify({
              enabled: false,
            }),
            [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
            "discoverystream.enabled": true,
            "discoverystream.hardcoded-basic-layout": true,
            "system.showSponsored": false,
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.spocs_endpoint,
        "https://spocs.getpocket.com/spocs"
      );
      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(layout[0].components[2].properties.items, 3);
    });
    it("should use new spocs endpoint if in a FF pref", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            [CONFIG_PREF_NAME]: JSON.stringify({
              enabled: false,
            }),
            [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
            "discoverystream.enabled": true,
            "discoverystream.spocs-endpoint":
              "https://spocs.getpocket.com/spocs2",
            "system.showSponsored": false,
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.spocs_endpoint,
        "https://spocs.getpocket.com/spocs2"
      );
    });
    it("should return enough stories to fill a four card layout", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            pocketConfig: { fourCardLayout: true },
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(layout[0].components[2].properties.items, 24);
    });
    it("should create a layout with spoc and widget positions", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            "discoverystream.spoc-positions": "1, 2",
            pocketConfig: {
              widgetPositions: "3, 4",
            },
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      assert.deepEqual(layout[0].components[2].spocs.positions, [
        { index: 1 },
        { index: 2 },
      ]);
      assert.deepEqual(layout[0].components[2].widgets.positions, [
        { index: 3 },
        { index: 4 },
      ]);
    });
    it("should create a layout with spoc position data", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            pocketConfig: {
              spocAdTypes: "1230",
              spocZoneIds: "4560, 7890",
            },
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      assert.deepEqual(layout[0].components[2].placement.ad_types, [1230]);
      assert.deepEqual(
        layout[0].components[2].placement.zone_ids,
        [4560, 7890]
      );
    });
    it("should create a layout with spoc topsite position data", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            pocketConfig: {
              spocTopsitesPlacementEnabled: true,
              spocTopsitesAdTypes: "1230",
              spocTopsitesZoneIds: "4560, 7890",
            },
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      assert.deepEqual(layout[0].components[0].placement.ad_types, [1230]);
      assert.deepEqual(
        layout[0].components[0].placement.zone_ids,
        [4560, 7890]
      );
    });
    it("should create a layout with proper spoc url with a site id", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            pocketConfig: {
              spocSiteId: "1234",
            },
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);
      const { spocs } = feed.store.getState().DiscoveryStream;
      assert.deepEqual(
        spocs.spocs_endpoint,
        "https://spocs.getpocket.com/spocs?site=1234"
      );
    });
  });

  describe("#updatePlacements", () => {
    it("should dispatch DISCOVERY_STREAM_SPOCS_PLACEMENTS", () => {
      sandbox.spy(feed.store, "dispatch");
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: true, "system.showSponsored": true },
        },
      });
      const fakeComponents = {
        components: [
          { placement: { name: "first" }, spocs: {} },
          { placement: { name: "second" }, spocs: {} },
        ],
      };
      const fakeLayout = [fakeComponents];

      feed.updatePlacements(feed.store.dispatch, fakeLayout);

      assert.calledOnce(feed.store.dispatch);
      assert.calledWith(feed.store.dispatch, {
        type: "DISCOVERY_STREAM_SPOCS_PLACEMENTS",
        data: { placements: [{ name: "first" }, { name: "second" }] },
        meta: { isStartup: false },
      });
    });
    it("should dispatch DISCOVERY_STREAM_SPOCS_PLACEMENTS with prefs array", () => {
      sandbox.spy(feed.store, "dispatch");
      feed.store.getState = () => ({
        Prefs: {
          values: {
            showSponsored: true,
            withPref: true,
            "system.showSponsored": true,
          },
        },
      });
      const fakeComponents = {
        components: [
          { placement: { name: "withPref" }, spocs: { prefs: ["withPref"] } },
          { placement: { name: "withoutPref1" }, spocs: {} },
          {
            placement: { name: "withoutPref2" },
            spocs: { prefs: ["whatever"] },
          },
          { placement: { name: "withoutPref3" }, spocs: { prefs: [] } },
        ],
      };
      const fakeLayout = [fakeComponents];

      feed.updatePlacements(feed.store.dispatch, fakeLayout);

      assert.calledOnce(feed.store.dispatch);
      assert.calledWith(feed.store.dispatch, {
        type: "DISCOVERY_STREAM_SPOCS_PLACEMENTS",
        data: { placements: [{ name: "withPref" }, { name: "withoutPref1" }] },
        meta: { isStartup: false },
      });
    });
    it("should fire update placements from loadLayout", async () => {
      sandbox.spy(feed, "updatePlacements");

      await feed.loadLayout(feed.store.dispatch);

      assert.calledOnce(feed.updatePlacements);
    });
  });

  describe("#placementsForEach", () => {
    it("should forEach through placements", () => {
      feed.store.getState = () => ({
        DiscoveryStream: {
          spocs: {
            placements: [{ name: "first" }, { name: "second" }],
          },
        },
      });

      let items = [];

      feed.placementsForEach(item => items.push(item.name));

      assert.deepEqual(items, ["first", "second"]);
    });
  });

  describe("#loadComponentFeeds", () => {
    let fakeCache;
    let fakeDiscoveryStream;
    beforeEach(() => {
      fakeDiscoveryStream = {
        Prefs: {
          values: {
            "discoverystream.spocs.startupCache.enabled": true,
          },
        },
        DiscoveryStream: {
          layout: [
            { components: [{ feed: { url: "foo.com" } }] },
            { components: [{}] },
            {},
          ],
        },
      };
      fakeCache = {};
      sandbox.stub(feed.store, "getState").returns(fakeDiscoveryStream);
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
    });

    afterEach(() => {
      sandbox.restore();
    });

    it("should not dispatch updates when layout is not defined", async () => {
      fakeDiscoveryStream = {
        DiscoveryStream: {},
      };
      feed.store.getState.returns(fakeDiscoveryStream);
      sandbox.spy(feed.store, "dispatch");

      await feed.loadComponentFeeds(feed.store.dispatch);

      assert.notCalled(feed.store.dispatch);
    });

    it("should populate feeds cache", async () => {
      fakeCache = {
        feeds: { "foo.com": { lastUpdated: Date.now(), data: "data" } },
      };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(fakeCache));

      await feed.loadComponentFeeds(feed.store.dispatch);

      assert.calledWith(feed.cache.set, "feeds", {
        "foo.com": { data: "data", lastUpdated: 0 },
      });
    });

    it("should send feed update events with new feed data", async () => {
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(fakeCache));
      sandbox.spy(feed.store, "dispatch");
      feed._prefCache = {
        config: {
          api_key_pref: "",
        },
      };

      await feed.loadComponentFeeds(feed.store.dispatch);

      assert.calledWith(feed.store.dispatch.firstCall, {
        type: at.DISCOVERY_STREAM_FEED_UPDATE,
        data: { feed: { data: { status: "failed" } }, url: "foo.com" },
        meta: { isStartup: false },
      });
      assert.calledWith(feed.store.dispatch.secondCall, {
        type: at.DISCOVERY_STREAM_FEEDS_UPDATE,
        meta: { isStartup: false },
      });
    });

    it("should return number of promises equal to unique urls", async () => {
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(fakeCache));
      sandbox.stub(global.Promise, "all").resolves();
      fakeDiscoveryStream = {
        DiscoveryStream: {
          layout: [
            {
              components: [
                { feed: { url: "foo.com" } },
                { feed: { url: "bar.com" } },
              ],
            },
            { components: [{ feed: { url: "foo.com" } }] },
            {},
            { components: [{ feed: { url: "baz.com" } }] },
          ],
        },
      };
      feed.store.getState.returns(fakeDiscoveryStream);

      await feed.loadComponentFeeds(feed.store.dispatch);

      assert.calledOnce(global.Promise.all);
      const { args } = global.Promise.all.firstCall;
      assert.equal(args[0].length, 3);
    });
  });

  describe("#getComponentFeed", () => {
    it("should fetch fresh feed data if cache is empty", async () => {
      const fakeCache = {};
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(fakeCache));
      sandbox.stub(feed, "rotate").callsFake(val => val);
      sandbox
        .stub(feed, "scoreItems")
        .callsFake(val => ({ data: val, filtered: [], personalized: false }));
      sandbox.stub(feed, "fetchFromEndpoint").resolves({
        recommendations: ["data"],
        settings: {
          recsExpireTime: 1,
        },
      });

      const feedResp = await feed.getComponentFeed("foo.com");

      assert.equal(feedResp.data.recommendations, "data");
    });
    it("should fetch fresh feed data if cache is old", async () => {
      const fakeCache = { feeds: { "foo.com": { lastUpdated: Date.now() } } };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(fakeCache));
      sandbox.stub(feed, "fetchFromEndpoint").resolves({
        recommendations: ["data"],
        settings: {
          recsExpireTime: 1,
        },
      });
      sandbox.stub(feed, "rotate").callsFake(val => val);
      sandbox
        .stub(feed, "scoreItems")
        .callsFake(val => ({ data: val, filtered: [], personalized: false }));
      clock.tick(THIRTY_MINUTES + 1);

      const feedResp = await feed.getComponentFeed("foo.com");

      assert.equal(feedResp.data.recommendations, "data");
    });
    it("should return feed data from cache if it is fresh", async () => {
      const fakeCache = {
        feeds: { "foo.com": { lastUpdated: Date.now(), data: "data" } },
      };
      sandbox.stub(feed.cache, "get").resolves(fakeCache);
      sandbox.stub(feed, "fetchFromEndpoint").resolves("old data");
      clock.tick(THIRTY_MINUTES - 1);

      const feedResp = await feed.getComponentFeed("foo.com");

      assert.equal(feedResp.data, "data");
    });
    it("should return null if no response was received", async () => {
      sandbox.stub(feed, "fetchFromEndpoint").resolves(null);

      const feedResp = await feed.getComponentFeed("foo.com");

      assert.deepEqual(feedResp, { data: { status: "failed" } });
    });
  });

  describe("#loadSpocs", () => {
    beforeEach(() => {
      feed._prefCache = {
        config: {
          api_key_pref: "",
        },
      };

      sandbox.stub(feed, "getPlacements").returns([{ name: "spocs" }]);
      Object.defineProperty(feed, "showSpocs", { get: () => true });
    });
    it("should not fetch or update cache if no spocs endpoint is defined", async () => {
      feed.store.dispatch(
        ac.BroadcastToContent({
          type: at.DISCOVERY_STREAM_SPOCS_ENDPOINT,
          data: "",
        })
      );

      sandbox.spy(feed.cache, "set");

      await feed.loadSpocs(feed.store.dispatch);

      assert.notCalled(global.fetch);
      assert.calledWith(feed.cache.set, "spocs", { lastUpdated: 0, spocs: {} });
    });
    it("should fetch fresh spocs data if cache is empty", async () => {
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed, "fetchFromEndpoint").resolves({ placement: "data" });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());

      await feed.loadSpocs(feed.store.dispatch);

      assert.calledWith(feed.cache.set, "spocs", {
        spocs: { placement: "data" },
        lastUpdated: 0,
      });
      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.data.placement,
        "data"
      );
    });
    it("should fetch fresh data if cache is old", async () => {
      const cachedSpoc = {
        spocs: { placement: "old" },
        lastUpdated: Date.now(),
      };
      const cachedData = { spocs: cachedSpoc };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(cachedData));
      sandbox.stub(feed, "fetchFromEndpoint").resolves({ placement: "new" });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      clock.tick(THIRTY_MINUTES + 1);

      await feed.loadSpocs(feed.store.dispatch);

      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.data.placement,
        "new"
      );
    });
    it("should return spoc data from cache if it is fresh", async () => {
      const cachedSpoc = {
        spocs: { placement: "old" },
        lastUpdated: Date.now(),
      };
      const cachedData = { spocs: cachedSpoc };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(cachedData));
      sandbox.stub(feed, "fetchFromEndpoint").resolves({ placement: "new" });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      clock.tick(THIRTY_MINUTES - 1);

      await feed.loadSpocs(feed.store.dispatch);

      assert.equal(
        feed.store.getState().DiscoveryStream.spocs.data.placement,
        "old"
      );
    });
    it("should properly transform spocs using placements", async () => {
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed, "fetchFromEndpoint").resolves({
        spocs: { items: [{ id: "data" }] },
      });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      const loadTimestamp = 100;
      clock.tick(loadTimestamp);

      await feed.loadSpocs(feed.store.dispatch);

      assert.calledWith(feed.cache.set, "spocs", {
        spocs: {
          spocs: {
            personalized: false,
            context: "",
            title: "",
            sponsor: "",
            sponsored_by_override: undefined,
            items: [{ id: "data", score: 1, fetchTimestamp: loadTimestamp }],
          },
        },
        lastUpdated: loadTimestamp,
      });

      assert.deepEqual(
        feed.store.getState().DiscoveryStream.spocs.data.spocs.items[0],
        { id: "data", score: 1, fetchTimestamp: loadTimestamp }
      );
    });
    it("should normalizeSpocsItems for older spoc data", async () => {
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox
        .stub(feed, "fetchFromEndpoint")
        .resolves({ spocs: [{ id: "data" }] });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());

      await feed.loadSpocs(feed.store.dispatch);

      assert.deepEqual(
        feed.store.getState().DiscoveryStream.spocs.data.spocs.items[0],
        { id: "data", score: 1, fetchTimestamp: 0 }
      );
    });
    it("should dispatch DISCOVERY_STREAM_PERSONALIZATION_OVERRIDE with feature_flags", async () => {
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.spy(feed.store, "dispatch");
      sandbox
        .stub(feed, "fetchFromEndpoint")
        .resolves({ settings: { feature_flags: {} }, spocs: [{ id: "data" }] });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());

      await feed.loadSpocs(feed.store.dispatch);

      assert.calledWith(
        feed.store.dispatch,
        ac.OnlyToMain({
          type: at.DISCOVERY_STREAM_PERSONALIZATION_OVERRIDE,
          data: {
            override: true,
          },
        })
      );
    });
    it("should return expected data if normalizeSpocsItems returns no spoc data", async () => {
      // We don't need this for just this test, we are setting placements
      // manually.
      feed.getPlacements.restore();
      Object.defineProperty(feed, "showSponsoredStories", {
        get: () => true,
      });

      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox
        .stub(feed, "fetchFromEndpoint")
        .resolves({ placement1: [{ id: "data" }], placement2: [] });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());

      const fakeComponents = {
        components: [
          { placement: { name: "placement1" }, spocs: {} },
          { placement: { name: "placement2" }, spocs: {} },
        ],
      };
      feed.updatePlacements(feed.store.dispatch, [fakeComponents]);

      await feed.loadSpocs(feed.store.dispatch);

      assert.deepEqual(feed.store.getState().DiscoveryStream.spocs.data, {
        placement1: {
          personalized: false,
          title: "",
          context: "",
          sponsor: "",
          sponsored_by_override: undefined,
          items: [{ id: "data", score: 1, fetchTimestamp: 0 }],
        },
        placement2: {
          title: "",
          context: "",
          items: [],
        },
      });
    });
    it("should use title and context on spoc data", async () => {
      // We don't need this for just this test, we are setting placements
      // manually.
      feed.getPlacements.restore();
      Object.defineProperty(feed, "showSponsoredStories", {
        get: () => true,
      });
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed, "fetchFromEndpoint").resolves({
        placement1: {
          title: "title",
          context: "context",
          sponsor: "",
          sponsored_by_override: undefined,
          items: [{ id: "data" }],
        },
      });
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());

      const fakeComponents = {
        components: [{ placement: { name: "placement1" }, spocs: {} }],
      };
      feed.updatePlacements(feed.store.dispatch, [fakeComponents]);

      await feed.loadSpocs(feed.store.dispatch);

      assert.deepEqual(feed.store.getState().DiscoveryStream.spocs.data, {
        placement1: {
          personalized: false,
          title: "title",
          context: "context",
          sponsor: "",
          sponsored_by_override: undefined,
          items: [{ id: "data", score: 1, fetchTimestamp: 0 }],
        },
      });
    });
    describe("test SOV behaviour", () => {
      beforeEach(() => {
        globals.set("NimbusFeatures", {
          pocketNewtab: {
            getVariable: sandbox.stub(),
          },
        });
        global.NimbusFeatures.pocketNewtab.getVariable
          .withArgs("topSitesContileSovEnabled")
          .returns(true);
        // We don't need this for just this test, we are setting placements
        // manually.
        feed.getPlacements.restore();
        Object.defineProperty(feed, "showSponsoredStories", {
          get: () => true,
        });
        const fakeComponents = {
          components: [
            { placement: { name: "sponsored-topsites" }, spocs: {} },
            { placement: { name: "spocs" }, spocs: {} },
          ],
        };
        feed.updatePlacements(feed.store.dispatch, [fakeComponents]);
        sandbox.stub(feed.cache, "get").returns(Promise.resolve());
        sandbox.stub(feed, "fetchFromEndpoint").resolves({
          spocs: [{ id: "spoc1" }],
          "sponsored-topsites": [{ id: "topsite1" }],
        });
      });
      it("should use topsites placement by default if there is no SOV", async () => {
        await feed.loadSpocs(feed.store.dispatch);

        assert.equal(
          feed.fetchFromEndpoint.firstCall.args[1].body,
          JSON.stringify({
            pocket_id: "{foo-123-foo}",
            version: 2,
            placements: [
              {
                name: "sponsored-topsites",
              },
              {
                name: "spocs",
              },
            ],
          })
        );
      });
      it("should use cache if cache is available and SOV is not ready", async () => {
        const cache = {
          sov: [{ assignedPartner: "amp" }],
        };
        feed.cache.get.resolves(cache);
        await feed.loadSpocs(feed.store.dispatch);

        assert.equal(
          feed.fetchFromEndpoint.firstCall.args[1].body,
          JSON.stringify({
            pocket_id: "{foo-123-foo}",
            version: 2,
            placements: [
              {
                name: "spocs",
              },
            ],
          })
        );
      });
      it("should properly set placements", async () => {
        sandbox.spy(feed.cache, "set");

        // Testing only 1 placement type.
        feed.store.dispatch(
          ac.OnlyToMain({
            type: at.SOV_UPDATED,
            data: {
              ready: true,
              positions: [
                {
                  position: 1,
                  assignedPartner: "amp",
                },
                {
                  position: 2,
                  assignedPartner: "amp",
                },
              ],
            },
          })
        );

        await feed.loadSpocs(feed.store.dispatch);

        const firstCall = feed.cache.set.getCall(0);
        assert.deepEqual(firstCall.args[0], "sov");
        assert.deepEqual(firstCall.args[1], [
          {
            position: 1,
            assignedPartner: "amp",
          },
          {
            position: 2,
            assignedPartner: "amp",
          },
        ]);
        assert.equal(
          feed.fetchFromEndpoint.firstCall.args[1].body,
          JSON.stringify({
            pocket_id: "{foo-123-foo}",
            version: 2,
            placements: [
              {
                name: "spocs",
              },
            ],
          })
        );

        // Testing 2 placement types.
        feed.store.dispatch(
          ac.OnlyToMain({
            type: at.SOV_UPDATED,
            data: {
              ready: true,
              positions: [
                {
                  position: 1,
                  assignedPartner: "amp",
                },
                {
                  position: 2,
                  assignedPartner: "moz-sales",
                },
              ],
            },
          })
        );

        await feed.loadSpocs(feed.store.dispatch);

        const secondCall = feed.cache.set.getCall(2);
        assert.deepEqual(secondCall.args[0], "sov");
        assert.deepEqual(secondCall.args[1], [
          {
            position: 1,
            assignedPartner: "amp",
          },
          {
            position: 2,
            assignedPartner: "moz-sales",
          },
        ]);
        assert.equal(
          feed.fetchFromEndpoint.secondCall.args[1].body,
          JSON.stringify({
            pocket_id: "{foo-123-foo}",
            version: 2,
            placements: [
              {
                name: "sponsored-topsites",
              },
              {
                name: "spocs",
              },
            ],
          })
        );
      });
    });
  });

  describe("#normalizeSpocsItems", () => {
    it("should return correct data if new data passed in", async () => {
      const spocs = {
        title: "title",
        context: "context",
        sponsor: "sponsor",
        sponsored_by_override: "override",
        items: [{ id: "id" }],
      };
      const result = feed.normalizeSpocsItems(spocs);
      assert.deepEqual(result, spocs);
    });
    it("should return normalized data if new data passed in without title or context", async () => {
      const spocs = {
        items: [{ id: "id" }],
      };
      const result = feed.normalizeSpocsItems(spocs);
      assert.deepEqual(result, {
        title: "",
        context: "",
        sponsor: "",
        sponsored_by_override: undefined,
        items: [{ id: "id" }],
      });
    });
    it("should return normalized data if old data passed in", async () => {
      const spocs = [{ id: "id" }];
      const result = feed.normalizeSpocsItems(spocs);
      assert.deepEqual(result, {
        title: "",
        context: "",
        sponsor: "",
        sponsored_by_override: undefined,
        items: [{ id: "id" }],
      });
    });
  });

  describe("#showSpocs", () => {
    it("should return true from showSpocs if showSponsoredStories is false", async () => {
      Object.defineProperty(feed, "showSponsoredStories", {
        get: () => false,
      });
      Object.defineProperty(feed, "showSponsoredTopsites", {
        get: () => true,
      });
      assert.isTrue(feed.showSpocs);
    });
    it("should return true from showSpocs if showSponsoredTopsites is false", async () => {
      Object.defineProperty(feed, "showSponsoredStories", {
        get: () => true,
      });
      Object.defineProperty(feed, "showSponsoredTopsites", {
        get: () => false,
      });
      assert.isTrue(feed.showSpocs);
    });
    it("should return true from showSpocs if both are true", async () => {
      Object.defineProperty(feed, "showSponsoredStories", {
        get: () => true,
      });
      Object.defineProperty(feed, "showSponsoredTopsites", {
        get: () => true,
      });
      assert.isTrue(feed.showSpocs);
    });
    it("should return false from showSpocs if both are false", async () => {
      Object.defineProperty(feed, "showSponsoredStories", {
        get: () => false,
      });
      Object.defineProperty(feed, "showSponsoredTopsites", {
        get: () => false,
      });
      assert.isFalse(feed.showSpocs);
    });
  });

  describe("#showSponsoredStories", () => {
    it("should return false from showSponsoredStories if user pref showSponsored is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: false, "system.showSponsored": true },
        },
      });

      assert.isFalse(feed.showSponsoredStories);
    });
    it("should return false from showSponsoredStories if DiscoveryStream pref system.showSponsored is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: true, "system.showSponsored": false },
        },
      });

      assert.isFalse(feed.showSponsoredStories);
    });
    it("should return true from showSponsoredStories if both prefs are true", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: true, "system.showSponsored": true },
        },
      });

      assert.isTrue(feed.showSponsoredStories);
    });
  });

  describe("#showSponsoredTopsites", () => {
    it("should return false from showSponsoredTopsites if user pref showSponsoredTopSites is false", async () => {
      feed.store.getState = () => ({
        Prefs: { values: { showSponsoredTopSites: false } },
        DiscoveryStream: {
          spocs: {
            placements: [{ name: "sponsored-topsites" }],
          },
        },
      });
      assert.isFalse(feed.showSponsoredTopsites);
    });
    it("should return true from showSponsoredTopsites if user pref showSponsoredTopSites is true", async () => {
      feed.store.getState = () => ({
        Prefs: { values: { showSponsoredTopSites: true } },
        DiscoveryStream: {
          spocs: {
            placements: [{ name: "sponsored-topsites" }],
          },
        },
      });
      assert.isTrue(feed.showSponsoredTopsites);
    });
  });

  describe("#showStories", () => {
    it("should return false from showStories if user pref is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "feeds.section.topstories": false,
            "feeds.system.topstories": true,
          },
        },
      });
      assert.isFalse(feed.showStories);
    });
    it("should return false from showStories if system pref is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "feeds.section.topstories": true,
            "feeds.system.topstories": false,
          },
        },
      });
      assert.isFalse(feed.showStories);
    });
    it("should return true from showStories if both prefs are true", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "feeds.section.topstories": true,
            "feeds.system.topstories": true,
          },
        },
      });
      assert.isTrue(feed.showStories);
    });
  });

  describe("#showTopsites", () => {
    it("should return false from showTopsites if user pref is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "feeds.topsites": false,
            "feeds.system.topsites": true,
          },
        },
      });
      assert.isFalse(feed.showTopsites);
    });
    it("should return false from showTopsites if system pref is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "feeds.topsites": true,
            "feeds.system.topsites": false,
          },
        },
      });
      assert.isFalse(feed.showTopsites);
    });
    it("should return true from showTopsites if both prefs are true", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "feeds.topsites": true,
            "feeds.system.topsites": true,
          },
        },
      });
      assert.isTrue(feed.showTopsites);
    });
  });

  describe("#clearSpocs", () => {
    let defaultState;
    let DiscoveryStream;
    let Prefs;
    beforeEach(() => {
      DiscoveryStream = {
        layout: [],
        spocs: {
          placements: [{ name: "sponsored-topsites" }],
        },
      };
      Prefs = {
        values: {
          "feeds.section.topstories": true,
          "feeds.system.topstories": true,
          "feeds.topsites": true,
          "feeds.system.topsites": true,
          showSponsoredTopSites: true,
          showSponsored: true,
          "system.showSponsored": true,
        },
      };
      defaultState = {
        DiscoveryStream,
        Prefs,
      };
      feed.store.getState = () => defaultState;
    });
    it("should not fail with no endpoint", async () => {
      sandbox.stub(feed.store, "getState").returns({
        Prefs: {
          values: { PREF_SPOCS_CLEAR_ENDPOINT: null },
        },
      });
      sandbox.stub(feed, "fetchFromEndpoint").resolves(null);

      await feed.clearSpocs();

      assert.notCalled(feed.fetchFromEndpoint);
    });
    it("should call DELETE with endpoint", async () => {
      sandbox.stub(feed.store, "getState").returns({
        Prefs: {
          values: {
            "discoverystream.endpointSpocsClear": "https://spocs/user",
          },
        },
      });
      sandbox.stub(feed, "fetchFromEndpoint").resolves(null);
      feed._impressionId = "1234";

      await feed.clearSpocs();

      assert.equal(
        feed.fetchFromEndpoint.firstCall.args[0],
        "https://spocs/user"
      );
      assert.equal(feed.fetchFromEndpoint.firstCall.args[1].method, "DELETE");
      assert.equal(
        feed.fetchFromEndpoint.firstCall.args[1].body,
        '{"pocket_id":"1234"}'
      );
    });
    it("should properly call clearSpocs when sponsored content is changed", async () => {
      sandbox.stub(feed, "clearSpocs").returns(Promise.resolve());
      sandbox.stub(feed, "loadSpocs").returns();

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      assert.notCalled(feed.clearSpocs);

      Prefs.values.showSponsoredTopSites = false;
      Prefs.values.showSponsored = false;

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      assert.calledOnce(feed.clearSpocs);
    });
    it("should call clearSpocs when top stories and top sites is turned off", async () => {
      sandbox.stub(feed, "clearSpocs").returns(Promise.resolve());
      Prefs.values["feeds.section.topstories"] = false;
      Prefs.values["feeds.topsites"] = false;

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "feeds.section.topstories" },
      });

      assert.calledOnce(feed.clearSpocs);

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "feeds.topsites" },
      });

      assert.calledTwice(feed.clearSpocs);
    });
  });

  describe("#rotate", () => {
    it("should move seen first story to the back of the response", async () => {
      const feedResponse = {
        recommendations: [
          {
            id: "first",
          },
          {
            id: "second",
          },
          {
            id: "third",
          },
          {
            id: "fourth",
          },
        ],
      };
      const fakeImpressions = {
        first: Date.now() - 60 * 60 * 1000, // 1 hour
        third: Date.now(),
      };
      const cache = {
        recsImpressions: fakeImpressions,
      };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      feed.cache.get.resolves(cache);

      const result = await feed.rotate(feedResponse.recommendations);

      assert.equal(result[3].id, "first");
    });
  });

  describe("#reset", () => {
    it("should fire all reset based functions", async () => {
      sandbox.stub(global.Services.obs, "removeObserver").returns();

      sandbox.stub(feed, "resetDataPrefs").returns();
      sandbox.stub(feed, "resetCache").returns(Promise.resolve());
      sandbox.stub(feed, "resetState").returns();

      feed.loaded = true;

      await feed.reset();

      assert.calledOnce(feed.resetDataPrefs);
      assert.calledOnce(feed.resetCache);
      assert.calledOnce(feed.resetState);
    });
  });

  describe("#resetCache", () => {
    it("should set .feeds .spocs and .sov to {}", async () => {
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());

      await feed.resetCache();

      assert.callCount(feed.cache.set, 4);
      const firstCall = feed.cache.set.getCall(0);
      const secondCall = feed.cache.set.getCall(1);
      const thirdCall = feed.cache.set.getCall(2);
      const fourthCall = feed.cache.set.getCall(3);
      assert.deepEqual(firstCall.args, ["feeds", {}]);
      assert.deepEqual(secondCall.args, ["spocs", {}]);
      assert.deepEqual(thirdCall.args, ["sov", {}]);
      assert.deepEqual(fourthCall.args, ["recsImpressions", {}]);
    });
  });

  describe("#scoreItems", () => {
    it("should return initial data from scoreItems if spocs are empty", async () => {
      const { data: result } = await feed.scoreItems([]);

      assert.equal(result.length, 0);
    });

    it("should sort based on item_score", async () => {
      const { data: result } = await feed.scoreItems([
        { id: 2, flight_id: 2, item_score: 0.8 },
        { id: 4, flight_id: 4, item_score: 0.5 },
        { id: 3, flight_id: 3, item_score: 0.7 },
        { id: 1, flight_id: 1, item_score: 0.9 },
      ]);

      assert.deepEqual(result, [
        { id: 1, flight_id: 1, item_score: 0.9, score: 0.9 },
        { id: 2, flight_id: 2, item_score: 0.8, score: 0.8 },
        { id: 3, flight_id: 3, item_score: 0.7, score: 0.7 },
        { id: 4, flight_id: 4, item_score: 0.5, score: 0.5 },
      ]);
    });

    it("should sort based on priority", async () => {
      const { data: result } = await feed.scoreItems([
        { id: 6, flight_id: 6, priority: 2, item_score: 0.7 },
        { id: 2, flight_id: 3, priority: 1, item_score: 0.2 },
        { id: 4, flight_id: 4, item_score: 0.6 },
        { id: 5, flight_id: 5, priority: 2, item_score: 0.8 },
        { id: 3, flight_id: 3, item_score: 0.8 },
        { id: 1, flight_id: 1, priority: 1, item_score: 0.3 },
      ]);

      assert.deepEqual(result, [
        {
          id: 1,
          flight_id: 1,
          priority: 1,
          score: 0.3,
          item_score: 0.3,
        },
        {
          id: 2,
          flight_id: 3,
          priority: 1,
          score: 0.2,
          item_score: 0.2,
        },
        {
          id: 5,
          flight_id: 5,
          priority: 2,
          score: 0.8,
          item_score: 0.8,
        },
        {
          id: 6,
          flight_id: 6,
          priority: 2,
          score: 0.7,
          item_score: 0.7,
        },
        { id: 3, flight_id: 3, item_score: 0.8, score: 0.8 },
        { id: 4, flight_id: 4, item_score: 0.6, score: 0.6 },
      ]);
    });

    it("should add a score prop to spocs", async () => {
      const { data: result } = await feed.scoreItems([
        { flight_id: 1, item_score: 0.9 },
      ]);

      assert.equal(result[0].score, 0.9);
    });
  });

  describe("#filterBlocked", () => {
    it("should return initial data from filterBlocked if spocs are empty", async () => {
      const { data: result } = await feed.filterBlocked([]);

      assert.equal(result.length, 0);
    });
    it("should return initial data if links are not blocked", async () => {
      const { data: result } = await feed.filterBlocked([
        { url: "https://foo.com" },
        { url: "test.com" },
      ]);
      assert.equal(result.length, 2);
    });
    it("should return filtered data if links are blocked", async () => {
      const fakeBlocks = {
        flight_id_3: 1,
      };
      sandbox.stub(feed, "readDataPref").returns(fakeBlocks);
      sandbox
        .stub(fakeNewTabUtils.blockedLinks, "isBlocked")
        .callsFake(({ url }) => url === "https://blocked_url.com");
      const cache = {
        recsBlocks: {
          id_4: 1,
        },
      };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed.cache, "set");
      feed.cache.get.resolves(cache);
      const { data: result } = await feed.filterBlocked([
        {
          url: "https://not_blocked.com",
          flight_id: "flight_id_1",
          id: "id_1",
        },
        {
          url: "https://blocked_url.com",
          flight_id: "flight_id_2",
          id: "id_2",
        },
        {
          url: "https://blocked_flight.com",
          flight_id: "flight_id_3",
          id: "id_3",
        },
        { url: "https://blocked_id.com", flight_id: "flight_id_4", id: "id_4" },
      ]);
      assert.equal(result.length, 1);
      assert.equal(result[0].url, "https://not_blocked.com");
    });
    it("filterRecommendations based on blockedlist by passing feed data", () => {
      fakeNewTabUtils.blockedLinks.links = [{ url: "https://foo.com" }];
      fakeNewTabUtils.blockedLinks.isBlocked = site =>
        fakeNewTabUtils.blockedLinks.links[0].url === site.url;

      const result = feed.filterRecommendations({
        lastUpdated: 4,
        data: {
          recommendations: [{ url: "https://foo.com" }, { url: "test.com" }],
        },
      });

      assert.equal(result.lastUpdated, 4);
      assert.lengthOf(result.data.recommendations, 1);
      assert.equal(result.data.recommendations[0].url, "test.com");
      assert.notInclude(
        result.data.recommendations,
        fakeNewTabUtils.blockedLinks.links[0]
      );
    });
  });

  describe("#frequencyCapSpocs", () => {
    it("should return filtered out spocs based on frequency caps", () => {
      const fakeSpocs = [
        {
          id: 1,
          flight_id: "seen",
          caps: {
            lifetime: 3,
            flight: {
              count: 1,
              period: 1,
            },
          },
        },
        {
          id: 2,
          flight_id: "not-seen",
          caps: {
            lifetime: 3,
            flight: {
              count: 1,
              period: 1,
            },
          },
        },
      ];
      const fakeImpressions = {
        seen: [Date.now() - 1],
      };
      sandbox.stub(feed, "readDataPref").returns(fakeImpressions);

      const { data: result, filtered } = feed.frequencyCapSpocs(fakeSpocs);

      assert.equal(result.length, 1);
      assert.equal(result[0].flight_id, "not-seen");
      assert.deepEqual(filtered, [fakeSpocs[0]]);
    });
    it("should return simple structure and do nothing with no spocs", () => {
      const { data: result, filtered } = feed.frequencyCapSpocs([]);

      assert.equal(result.length, 0);
      assert.equal(filtered.length, 0);
    });
  });

  describe("#migrateFlightId", () => {
    it("should migrate campaign to flight if no flight exists", () => {
      const fakeSpocs = [
        {
          id: 1,
          campaign_id: "campaign",
          caps: {
            lifetime: 3,
            campaign: {
              count: 1,
              period: 1,
            },
          },
        },
      ];
      const { data: result } = feed.migrateFlightId(fakeSpocs);

      assert.deepEqual(result[0], {
        id: 1,
        flight_id: "campaign",
        campaign_id: "campaign",
        caps: {
          lifetime: 3,
          flight: {
            count: 1,
            period: 1,
          },
          campaign: {
            count: 1,
            period: 1,
          },
        },
      });
    });
    it("should not migrate campaign to flight if caps or id don't exist", () => {
      const fakeSpocs = [{ id: 1 }];
      const { data: result } = feed.migrateFlightId(fakeSpocs);

      assert.deepEqual(result[0], { id: 1 });
    });
    it("should return simple structure and do nothing with no spocs", () => {
      const { data: result } = feed.migrateFlightId([]);

      assert.equal(result.length, 0);
    });
  });

  describe("#isBelowFrequencyCap", () => {
    it("should return true if there are no flight impressions", () => {
      const fakeImpressions = {
        seen: [Date.now() - 1],
      };
      const fakeSpoc = {
        flight_id: "not-seen",
        caps: {
          lifetime: 3,
          flight: {
            count: 1,
            period: 1,
          },
        },
      };

      const result = feed.isBelowFrequencyCap(fakeImpressions, fakeSpoc);

      assert.isTrue(result);
    });
    it("should return true if there are no flight caps", () => {
      const fakeImpressions = {
        seen: [Date.now() - 1],
      };
      const fakeSpoc = {
        flight_id: "seen",
        caps: {
          lifetime: 3,
        },
      };

      const result = feed.isBelowFrequencyCap(fakeImpressions, fakeSpoc);

      assert.isTrue(result);
    });

    it("should return false if lifetime cap is hit", () => {
      const fakeImpressions = {
        seen: [Date.now() - 1],
      };
      const fakeSpoc = {
        flight_id: "seen",
        caps: {
          lifetime: 1,
          flight: {
            count: 3,
            period: 1,
          },
        },
      };

      const result = feed.isBelowFrequencyCap(fakeImpressions, fakeSpoc);

      assert.isFalse(result);
    });

    it("should return false if time based cap is hit", () => {
      const fakeImpressions = {
        seen: [Date.now() - 1],
      };
      const fakeSpoc = {
        flight_id: "seen",
        caps: {
          lifetime: 3,
          flight: {
            count: 1,
            period: 1,
          },
        },
      };

      const result = feed.isBelowFrequencyCap(fakeImpressions, fakeSpoc);

      assert.isFalse(result);
    });
  });

  describe("#retryFeed", () => {
    it("should retry a feed fetch", async () => {
      sandbox.stub(feed, "getComponentFeed").returns(Promise.resolve({}));
      sandbox.spy(feed.store, "dispatch");

      await feed.retryFeed({ url: "https://feed.com" });

      assert.calledOnce(feed.getComponentFeed);
      assert.calledOnce(feed.store.dispatch);
      assert.equal(
        feed.store.dispatch.firstCall.args[0].type,
        "DISCOVERY_STREAM_FEED_UPDATE"
      );
      assert.deepEqual(feed.store.dispatch.firstCall.args[0].data, {
        feed: {},
        url: "https://feed.com",
      });
    });
  });

  describe("#recordFlightImpression", () => {
    it("should return false if time based cap is hit", () => {
      sandbox.stub(feed, "readDataPref").returns({});
      sandbox.stub(feed, "writeDataPref").returns();

      feed.recordFlightImpression("seen");

      assert.calledWith(feed.writeDataPref, SPOC_IMPRESSION_TRACKING_PREF, {
        seen: [0],
      });
    });
  });

  describe("#recordBlockFlightId", () => {
    it("should call writeDataPref with new flight id added", () => {
      sandbox.stub(feed, "readDataPref").returns({ 1234: 1 });
      sandbox.stub(feed, "writeDataPref").returns();

      feed.recordBlockFlightId("5678");

      assert.calledOnce(feed.readDataPref);
      assert.calledWith(feed.writeDataPref, "discoverystream.flight.blocks", {
        1234: 1,
        5678: 1,
      });
    });
  });

  describe("#cleanUpFlightImpressionPref", () => {
    it("should remove flight-3 because it is no longer being used", async () => {
      const fakeSpocs = {
        spocs: {
          items: [
            {
              flight_id: "flight-1",
              caps: {
                lifetime: 3,
                flight: {
                  count: 1,
                  period: 1,
                },
              },
            },
            {
              flight_id: "flight-2",
              caps: {
                lifetime: 3,
                flight: {
                  count: 1,
                  period: 1,
                },
              },
            },
          ],
        },
      };
      const fakeImpressions = {
        "flight-2": [Date.now() - 1],
        "flight-3": [Date.now() - 1],
      };
      sandbox.stub(feed, "getPlacements").returns([{ name: "spocs" }]);
      sandbox.stub(feed, "readDataPref").returns(fakeImpressions);
      sandbox.stub(feed, "writeDataPref").returns();

      feed.cleanUpFlightImpressionPref(fakeSpocs);

      assert.calledWith(feed.writeDataPref, SPOC_IMPRESSION_TRACKING_PREF, {
        "flight-2": [-1],
      });
    });
  });

  describe("#recordTopRecImpression", () => {
    it("should add a rec id to the rec impression pref", async () => {
      const cache = {
        recsImpressions: {},
      };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed.cache, "set");
      feed.cache.get.resolves(cache);

      await feed.recordTopRecImpression("rec");

      assert.calledWith(feed.cache.set, "recsImpressions", {
        rec: 0,
      });
    });
    it("should not add an impression if it already exists", async () => {
      const cache = {
        recsImpressions: { rec: 4 },
      };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed.cache, "set");
      feed.cache.get.resolves(cache);

      await feed.recordTopRecImpression("rec");

      assert.notCalled(feed.cache.set);
    });
  });

  describe("#cleanUpTopRecImpressions", () => {
    it("should remove rec impressions older than 7 days", async () => {
      const fakeImpressions = {
        rec2: Date.now(),
        rec3: Date.now(),
        rec5: Date.now() - 7 * 24 * 60 * 60 * 1000, // 7 days
      };

      const cache = {
        recsImpressions: fakeImpressions,
      };
      sandbox.stub(feed.cache, "get").returns(Promise.resolve());
      sandbox.stub(feed.cache, "set");
      feed.cache.get.resolves(cache);

      await feed.cleanUpTopRecImpressions();

      assert.calledWith(feed.cache.set, "recsImpressions", {
        rec2: 0,
        rec3: 0,
      });
    });
  });

  describe("#writeDataPref", () => {
    it("should call Services.prefs.setStringPref", () => {
      sandbox.spy(feed.store, "dispatch");
      const fakeImpressions = {
        foo: [Date.now() - 1],
        bar: [Date.now() - 1],
      };

      feed.writeDataPref(SPOC_IMPRESSION_TRACKING_PREF, fakeImpressions);

      assert.calledWithMatch(feed.store.dispatch, {
        data: {
          name: SPOC_IMPRESSION_TRACKING_PREF,
          value: JSON.stringify(fakeImpressions),
        },
        type: at.SET_PREF,
      });
    });
  });

  describe("#addEndpointQuery", () => {
    const url = "https://spocs.getpocket.com/spocs";

    it("should return same url with no query", () => {
      const result = feed.addEndpointQuery(url, "");
      assert.equal(result, url);
    });

    it("should add multiple query params to standard url", () => {
      const params = "?first=first&second=second";
      const result = feed.addEndpointQuery(url, params);
      assert.equal(result, url + params);
    });

    it("should add multiple query params to url with a query already", () => {
      const params = "first=first&second=second";
      const initialParams = "?zero=zero";
      const result = feed.addEndpointQuery(
        `${url}${initialParams}`,
        `?${params}`
      );
      assert.equal(result, `${url}${initialParams}&${params}`);
    });
  });

  describe("#readDataPref", () => {
    it("should return what's in Services.prefs.getStringPref", () => {
      const fakeImpressions = {
        foo: [Date.now() - 1],
        bar: [Date.now() - 1],
      };
      setPref(SPOC_IMPRESSION_TRACKING_PREF, fakeImpressions);

      const result = feed.readDataPref(SPOC_IMPRESSION_TRACKING_PREF);

      assert.deepEqual(result, fakeImpressions);
    });
  });

  describe("#setupPrefs", () => {
    it("should call setupPrefs", async () => {
      sandbox.spy(feed, "setupPrefs");
      feed.onAction({
        type: at.INIT,
      });
      assert.calledOnce(feed.setupPrefs);
    });
    it("should dispatch to at.DISCOVERY_STREAM_PREFS_SETUP with proper data", async () => {
      sandbox.spy(feed.store, "dispatch");
      globals.set("ExperimentAPI", {
        getExperimentMetaData: () => ({
          slug: "experimentId",
          branch: {
            slug: "branchId",
          },
        }),
        getRolloutMetaData: () => ({}),
      });
      global.Services.prefs.getBoolPref
        .withArgs("extensions.pocket.enabled")
        .returns(true);
      feed.store.getState = () => ({
        Prefs: {
          values: {
            region: "CA",
            pocketConfig: {
              recentSavesEnabled: true,
              hideDescriptions: false,
              hideDescriptionsRegions: "US,CA,GB",
              compactImages: true,
              imageGradient: true,
              newSponsoredLabel: true,
              titleLines: "1",
              descLines: "1",
              readTime: true,
              saveToPocketCard: false,
              saveToPocketCardRegions: "US,CA,GB",
            },
          },
        },
      });
      feed.setupPrefs();
      assert.deepEqual(feed.store.dispatch.firstCall.args[0].data, {
        utmSource: "pocket-newtab",
        utmCampaign: "experimentId",
        utmContent: "branchId",
      });
      assert.deepEqual(feed.store.dispatch.secondCall.args[0].data, {
        recentSavesEnabled: true,
        pocketButtonEnabled: true,
        saveToPocketCard: true,
        hideDescriptions: true,
        compactImages: true,
        imageGradient: true,
        newSponsoredLabel: true,
        titleLines: "1",
        descLines: "1",
        readTime: true,
      });
    });
  });

  describe("#onAction: DISCOVERY_STREAM_IMPRESSION_STATS", () => {
    it("should call recordTopRecImpressions from DISCOVERY_STREAM_IMPRESSION_STATS", async () => {
      sandbox.stub(feed, "recordTopRecImpression").returns();
      await feed.onAction({
        type: at.DISCOVERY_STREAM_IMPRESSION_STATS,
        data: { tiles: [{ id: "seen" }] },
      });

      assert.calledWith(feed.recordTopRecImpression, "seen");
    });
  });

  describe("#onAction: DISCOVERY_STREAM_SPOC_IMPRESSION", () => {
    beforeEach(() => {
      const data = {
        spocs: {
          items: [
            {
              id: 1,
              flight_id: "seen",
              caps: {
                lifetime: 3,
                flight: {
                  count: 1,
                  period: 1,
                },
              },
            },
            {
              id: 2,
              flight_id: "not-seen",
              caps: {
                lifetime: 3,
                flight: {
                  count: 1,
                  period: 1,
                },
              },
            },
          ],
        },
      };
      sandbox.stub(feed.store, "getState").returns({
        DiscoveryStream: {
          spocs: {
            data,
          },
        },
      });
    });

    it("should call dispatch to ac.AlsoToPreloaded with filtered spoc data", async () => {
      sandbox.stub(feed, "getPlacements").returns([{ name: "spocs" }]);
      Object.defineProperty(feed, "showSpocs", { get: () => true });
      const fakeImpressions = {
        seen: [Date.now() - 1],
      };
      const result = {
        spocs: {
          items: [
            {
              id: 2,
              flight_id: "not-seen",
              caps: {
                lifetime: 3,
                flight: {
                  count: 1,
                  period: 1,
                },
              },
            },
          ],
        },
      };
      sandbox.stub(feed, "recordFlightImpression").returns();
      sandbox.stub(feed, "readDataPref").returns(fakeImpressions);
      sandbox.spy(feed.store, "dispatch");

      await feed.onAction({
        type: at.DISCOVERY_STREAM_SPOC_IMPRESSION,
        data: { flightId: "seen" },
      });

      assert.deepEqual(
        feed.store.dispatch.secondCall.args[0].data.spocs,
        result
      );
    });
    it("should not call dispatch to ac.AlsoToPreloaded if spocs were not changed by frequency capping", async () => {
      sandbox.stub(feed, "getPlacements").returns([{ name: "spocs" }]);
      Object.defineProperty(feed, "showSpocs", { get: () => true });
      const fakeImpressions = {};
      sandbox.stub(feed, "recordFlightImpression").returns();
      sandbox.stub(feed, "readDataPref").returns(fakeImpressions);
      sandbox.spy(feed.store, "dispatch");

      await feed.onAction({
        type: at.DISCOVERY_STREAM_SPOC_IMPRESSION,
        data: { flight_id: "seen" },
      });

      assert.notCalled(feed.store.dispatch);
    });
    it("should attempt feq cap on valid spocs with placements on impression", async () => {
      sandbox.restore();
      Object.defineProperty(feed, "showSpocs", { get: () => true });
      const fakeImpressions = {};
      sandbox.stub(feed, "recordFlightImpression").returns();
      sandbox.stub(feed, "readDataPref").returns(fakeImpressions);
      sandbox.spy(feed.store, "dispatch");
      sandbox.spy(feed, "frequencyCapSpocs");

      const data = {
        spocs: {
          items: [
            {
              id: 2,
              flight_id: "seen-2",
              caps: {
                lifetime: 3,
                flight: {
                  count: 1,
                  period: 1,
                },
              },
            },
          ],
        },
      };
      sandbox.stub(feed.store, "getState").returns({
        DiscoveryStream: {
          spocs: {
            data,
            placements: [{ name: "spocs" }, { name: "notSpocs" }],
          },
        },
      });

      await feed.onAction({
        type: at.DISCOVERY_STREAM_SPOC_IMPRESSION,
        data: { flight_id: "doesn't matter" },
      });

      assert.calledOnce(feed.frequencyCapSpocs);
      assert.calledWith(feed.frequencyCapSpocs, data.spocs.items);
    });
  });

  describe("#onAction: PLACES_LINK_BLOCKED", () => {
    beforeEach(() => {
      const spocsData = {
        data: {
          spocs: {
            items: [
              {
                id: 1,
                flight_id: "foo",
                url: "foo.com",
              },
              {
                id: 2,
                flight_id: "bar",
                url: "bar.com",
              },
            ],
          },
        },
        placements: [{ name: "spocs" }],
      };
      const feedsData = {
        data: {},
      };
      sandbox.stub(feed.store, "getState").returns({
        DiscoveryStream: {
          spocs: spocsData,
          feeds: feedsData,
        },
      });
    });
    it("should call dispatch if found a blocked spoc", async () => {
      Object.defineProperty(feed, "showSpocs", { get: () => true });

      sandbox.spy(feed.store, "dispatch");

      await feed.onAction({
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "foo.com" },
      });

      assert.deepEqual(
        feed.store.dispatch.firstCall.args[0].data.url,
        "foo.com"
      );
    });
    it("should dispatch once if the blocked is not a SPOC", async () => {
      Object.defineProperty(feed, "showSpocs", { get: () => true });
      sandbox.spy(feed.store, "dispatch");

      await feed.onAction({
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "not_a_spoc.com" },
      });

      assert.calledOnce(feed.store.dispatch);
      assert.deepEqual(
        feed.store.dispatch.firstCall.args[0].data.url,
        "not_a_spoc.com"
      );
    });
    it("should dispatch a DISCOVERY_STREAM_SPOC_BLOCKED for a blocked spoc", async () => {
      Object.defineProperty(feed, "showSpocs", { get: () => true });
      sandbox.spy(feed.store, "dispatch");

      await feed.onAction({
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "foo.com" },
      });

      assert.equal(
        feed.store.dispatch.secondCall.args[0].type,
        "DISCOVERY_STREAM_SPOC_BLOCKED"
      );
    });
  });

  describe("#onAction: BLOCK_URL", () => {
    it("should call recordBlockFlightId whith BLOCK_URL", async () => {
      sandbox.stub(feed, "recordBlockFlightId").returns();

      await feed.onAction({
        type: at.BLOCK_URL,
        data: [
          {
            flight_id: "1234",
          },
        ],
      });

      assert.calledWith(feed.recordBlockFlightId, "1234");
    });
  });

  describe("#onAction: INIT", () => {
    it("should be .loaded=false before initialization", () => {
      assert.isFalse(feed.loaded);
    });
    it("should load data and set .loaded=true if config.enabled is true", async () => {
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      setPref(CONFIG_PREF_NAME, { enabled: true });
      sandbox.stub(feed, "loadLayout").returns(Promise.resolve());

      await feed.onAction({ type: at.INIT });

      assert.calledOnce(feed.loadLayout);
      assert.isTrue(feed.loaded);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_SET_VALUE", async () => {
    it("should add the new value to the pref without changing the existing values", async () => {
      sandbox.spy(feed.store, "dispatch");
      setPref(CONFIG_PREF_NAME, { enabled: true, other: "value" });

      await feed.onAction({
        type: at.DISCOVERY_STREAM_CONFIG_SET_VALUE,
        data: { name: "api_key_pref", value: "foo" },
      });

      assert.calledWithMatch(feed.store.dispatch, {
        data: {
          name: CONFIG_PREF_NAME,
          value: JSON.stringify({
            enabled: true,
            other: "value",
            api_key_pref: "foo",
          }),
        },
        type: at.SET_PREF,
      });
    });
  });

  describe("#onAction: DISCOVERY_STREAM_POCKET_STATE_INIT", async () => {
    it("should call setupPocketState", async () => {
      sandbox.spy(feed, "setupPocketState");
      feed.onAction({
        type: at.DISCOVERY_STREAM_POCKET_STATE_INIT,
        meta: { fromTarget: {} },
      });
      assert.calledOnce(feed.setupPocketState);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_RESET", async () => {
    it("should call configReset", async () => {
      sandbox.spy(feed, "configReset");
      feed.onAction({
        type: at.DISCOVERY_STREAM_CONFIG_RESET,
      });
      assert.calledOnce(feed.configReset);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS", async () => {
    it("Should dispatch CLEAR_PREF with pref name", async () => {
      sandbox.spy(feed.store, "dispatch");
      await feed.onAction({
        type: at.DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS,
      });

      assert.calledWithMatch(feed.store.dispatch, {
        data: {
          name: CONFIG_PREF_NAME,
        },
        type: at.CLEAR_PREF,
      });
    });
  });

  describe("#onAction: DISCOVERY_STREAM_RETRY_FEED", async () => {
    it("should call retryFeed", async () => {
      sandbox.spy(feed, "retryFeed");
      feed.onAction({
        type: at.DISCOVERY_STREAM_RETRY_FEED,
        data: { feed: { url: "https://feed.com" } },
      });
      assert.calledOnce(feed.retryFeed);
      assert.calledWith(feed.retryFeed, { url: "https://feed.com" });
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_CHANGE", () => {
    it("should call this.loadLayout if config.enabled changes to true ", async () => {
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      // First initialize
      await feed.onAction({ type: at.INIT });
      assert.isFalse(feed.loaded);

      // force clear cached pref value
      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: true });

      sandbox.stub(feed, "resetCache").returns(Promise.resolve());
      sandbox.stub(feed, "loadLayout").returns(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      assert.calledOnce(feed.loadLayout);
      assert.calledOnce(feed.resetCache);
      assert.isTrue(feed.loaded);
    });
    it("should clear the cache if a config change happens and config.enabled is true", async () => {
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      // force clear cached pref value
      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: true });

      sandbox.stub(feed, "resetCache").returns(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      assert.calledOnce(feed.resetCache);
    });
    it("should dispatch DISCOVERY_STREAM_LAYOUT_RESET from DISCOVERY_STREAM_CONFIG_CHANGE", async () => {
      sandbox.stub(feed, "resetDataPrefs");
      sandbox.stub(feed, "resetCache").resolves();
      sandbox.stub(feed, "enable").resolves();
      setPref(CONFIG_PREF_NAME, { enabled: true });
      sandbox.spy(feed.store, "dispatch");

      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      assert.calledWithMatch(feed.store.dispatch, {
        type: at.DISCOVERY_STREAM_LAYOUT_RESET,
      });
    });
    it("should not call this.loadLayout if config.enabled changes to false", async () => {
      sandbox.stub(feed.cache, "set").returns(Promise.resolve());
      // force clear cached pref value
      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });
      assert.isTrue(feed.loaded);

      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: false });
      sandbox.stub(feed, "resetCache").returns(Promise.resolve());
      sandbox.stub(feed, "loadLayout").returns(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      assert.notCalled(feed.loadLayout);
      assert.calledOnce(feed.resetCache);
      assert.isFalse(feed.loaded);
    });
  });

  describe("#onAction: UNINIT", () => {
    it("should reset pref cache", async () => {
      feed._prefCache = { cached: "value" };

      await feed.onAction({ type: at.UNINIT });

      assert.deepEqual(feed._prefCache, {});
    });
  });

  describe("#onAction: PREF_CHANGED", () => {
    it("should update state.DiscoveryStream.config when the pref changes", async () => {
      setPref(CONFIG_PREF_NAME, {
        enabled: true,
        api_key_pref: "foo",
      });

      assert.deepEqual(feed.store.getState().DiscoveryStream.config, {
        enabled: true,
        api_key_pref: "foo",
      });
    });
    it("should fire loadSpocs is showSponsored pref changes", async () => {
      sandbox.stub(feed, "loadSpocs").returns(Promise.resolve());

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      assert.calledOnce(feed.loadSpocs);
    });
    it("should fire onPrefChange when pocketConfig pref changes", async () => {
      sandbox.stub(feed, "onPrefChange").returns(Promise.resolve());

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "pocketConfig", value: false },
      });

      assert.calledOnce(feed.onPrefChange);
    });
    it("should fire onCollectionsChanged when collections pref changes", async () => {
      sandbox.stub(feed, "onCollectionsChanged").returns(Promise.resolve());

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "discoverystream.sponsored-collections.enabled" },
      });

      assert.calledOnce(feed.onCollectionsChanged);
    });
    it("should re enable stories when top stories is turned on", async () => {
      sandbox.stub(feed, "refreshAll").returns(Promise.resolve());
      feed.loaded = true;
      setPref(CONFIG_PREF_NAME, {
        enabled: true,
      });

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "feeds.section.topstories", value: true },
      });

      assert.calledOnce(feed.refreshAll);
    });
    it("shoud update allowlist", async () => {
      assert.equal(
        feed.store.getState().Prefs.values[ENDPOINTS_PREF_NAME],
        DUMMY_ENDPOINT
      );
      setPref(ENDPOINTS_PREF_NAME, "sick-kickflip.mozilla.net");
      assert.equal(
        feed.store.getState().Prefs.values[ENDPOINTS_PREF_NAME],
        "sick-kickflip.mozilla.net"
      );
    });
  });

  describe("#onAction: SYSTEM_TICK", () => {
    it("should not refresh if DiscoveryStream has not been loaded", async () => {
      sandbox.stub(feed, "refreshAll").resolves();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.SYSTEM_TICK });
      assert.notCalled(feed.refreshAll);
    });

    it("should not refresh if no caches are expired", async () => {
      sandbox.stub(feed.cache, "set").resolves();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      sandbox.stub(feed, "checkIfAnyCacheExpired").resolves(false);
      sandbox.stub(feed, "refreshAll").resolves();

      await feed.onAction({ type: at.SYSTEM_TICK });
      assert.notCalled(feed.refreshAll);
    });

    it("should refresh if DiscoveryStream has been loaded at least once and a cache has expired", async () => {
      sandbox.stub(feed.cache, "set").resolves();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      sandbox.stub(feed, "checkIfAnyCacheExpired").resolves(true);
      sandbox.stub(feed, "refreshAll").resolves();

      await feed.onAction({ type: at.SYSTEM_TICK });
      assert.calledOnce(feed.refreshAll);
    });

    it("should refresh and not update open tabs if DiscoveryStream has been loaded at least once", async () => {
      sandbox.stub(feed.cache, "set").resolves();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      sandbox.stub(feed, "checkIfAnyCacheExpired").resolves(true);
      sandbox.stub(feed, "refreshAll").resolves();

      await feed.onAction({ type: at.SYSTEM_TICK });
      assert.calledWith(feed.refreshAll, { updateOpenTabs: false });
    });
  });

  describe("#onCollectionsChanged", () => {
    it("should call loadLayout when Pocket config changes", async () => {
      sandbox.stub(feed, "loadLayout").callsFake(dispatch => dispatch("foo"));
      sandbox.stub(feed.store, "dispatch");
      await feed.onCollectionsChanged();
      assert.calledOnce(feed.loadLayout);
      assert.calledWith(feed.store.dispatch, ac.AlsoToPreloaded("foo"));
    });
  });

  describe("#enable", () => {
    it("should pass along proper options to refreshAll from enable", async () => {
      sandbox.stub(feed, "refreshAll");
      await feed.enable();
      assert.calledWith(feed.refreshAll, {});
      await feed.enable({ updateOpenTabs: true });
      assert.calledWith(feed.refreshAll, { updateOpenTabs: true });
      await feed.enable({ isStartup: true });
      assert.calledWith(feed.refreshAll, { isStartup: true });
      await feed.enable({ updateOpenTabs: true, isStartup: true });
      assert.calledWith(feed.refreshAll, {
        updateOpenTabs: true,
        isStartup: true,
      });
    });
  });

  describe("#onPrefChange", () => {
    it("should call loadLayout when Pocket config changes", async () => {
      sandbox.stub(feed, "loadLayout");
      feed._prefCache.config = {
        enabled: true,
      };
      await feed.onPrefChange();
      assert.calledOnce(feed.loadLayout);
    });
    it("should update open tabs but not startup with onPrefChange", async () => {
      sandbox.stub(feed, "refreshAll");
      feed._prefCache.config = {
        enabled: true,
      };
      await feed.onPrefChange();
      assert.calledWith(feed.refreshAll, { updateOpenTabs: true });
    });
  });

  describe("#onAction: PREF_SHOW_SPONSORED", () => {
    it("should call loadSpocs when preference changes", async () => {
      sandbox.stub(feed, "loadSpocs").resolves();
      sandbox.stub(feed.store, "dispatch");

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      assert.calledOnce(feed.loadSpocs);
      const [dispatchFn] = feed.loadSpocs.firstCall.args;
      dispatchFn({});
      assert.calledWith(feed.store.dispatch, ac.BroadcastToContent({}));
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_SYNC_RS", () => {
    it("should fire remote settings pollChanges", async () => {
      sandbox.stub(global.RemoteSettings, "pollChanges").returns();
      await feed.onAction({
        type: at.DISCOVERY_STREAM_DEV_SYNC_RS,
      });
      assert.calledOnce(global.RemoteSettings.pollChanges);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_SYSTEM_TICK", () => {
    it("should refresh if DiscoveryStream has been loaded at least once and a cache has expired", async () => {
      sandbox.stub(feed.cache, "set").resolves();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      sandbox.stub(feed, "checkIfAnyCacheExpired").resolves(true);
      sandbox.stub(feed, "refreshAll").resolves();

      await feed.onAction({ type: at.DISCOVERY_STREAM_DEV_SYSTEM_TICK });
      assert.calledOnce(feed.refreshAll);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_EXPIRE_CACHE", () => {
    it("should fire resetCache", async () => {
      sandbox.stub(feed, "resetContentCache").returns();
      await feed.onAction({
        type: at.DISCOVERY_STREAM_DEV_EXPIRE_CACHE,
      });
      assert.calledOnce(feed.resetContentCache);
    });
  });

  describe("#spocsCacheUpdateTime", () => {
    it("should call setupSpocsCacheUpdateTime", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      sandbox.spy(feed, "setupSpocsCacheUpdateTime");
      const cacheTime = feed.spocsCacheUpdateTime;
      assert.equal(feed._spocsCacheUpdateTime, defaultCacheTime);
      assert.equal(cacheTime, defaultCacheTime);
      assert.calledOnce(feed.setupSpocsCacheUpdateTime);
    });
    it("should return _spocsCacheUpdateTime", () => {
      sandbox.spy(feed, "setupSpocsCacheUpdateTime");
      const testCacheTime = 123;
      feed._spocsCacheUpdateTime = testCacheTime;
      const cacheTime = feed.spocsCacheUpdateTime;
      // Ensure _spocsCacheUpdateTime was not changed.
      assert.equal(feed._spocsCacheUpdateTime, testCacheTime);
      assert.equal(cacheTime, testCacheTime);
      assert.notCalled(feed.setupSpocsCacheUpdateTime);
    });
  });

  describe("#setupSpocsCacheUpdateTime", () => {
    it("should set _spocsCacheUpdateTime with default value", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      feed.setupSpocsCacheUpdateTime();
      assert.equal(feed._spocsCacheUpdateTime, defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with min", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.cacheTimeout": 1,
          },
        },
      });
      feed.setupSpocsCacheUpdateTime();
      assert.equal(feed._spocsCacheUpdateTime, defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with max", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.cacheTimeout": 31,
          },
        },
      });
      feed.setupSpocsCacheUpdateTime();
      assert.equal(feed._spocsCacheUpdateTime, defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with spocsCacheTimeout", () => {
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.cacheTimeout": 20,
          },
        },
      });
      feed.setupSpocsCacheUpdateTime();
      assert.equal(feed._spocsCacheUpdateTime, 20 * 60 * 1000);
    });
  });

  describe("#isExpired", () => {
    it("should throw if the key is not valid", () => {
      assert.throws(() => {
        feed.isExpired({}, "foo");
      });
    });
    it("should return false for spocs on startup for content under 1 week", () => {
      const spocs = { lastUpdated: Date.now() };
      const result = feed.isExpired({
        cachedData: { spocs },
        key: "spocs",
        isStartup: true,
      });

      assert.isFalse(result);
    });
    it("should return true for spocs for isStartup=false after 30 mins", () => {
      const spocs = { lastUpdated: Date.now() };
      clock.tick(THIRTY_MINUTES + 1);
      const result = feed.isExpired({ cachedData: { spocs }, key: "spocs" });

      assert.isTrue(result);
    });
    it("should return true for spocs on startup for content over 1 week", () => {
      const spocs = { lastUpdated: Date.now() };
      clock.tick(ONE_WEEK + 1);
      const result = feed.isExpired({
        cachedData: { spocs },
        key: "spocs",
        isStartup: true,
      });

      assert.isTrue(result);
    });
  });

  describe("#checkIfAnyCacheExpired", () => {
    let cache;
    beforeEach(() => {
      cache = {
        feeds: { "foo.com": { lastUpdated: Date.now() } },
        spocs: { lastUpdated: Date.now() },
      };
      Object.defineProperty(feed, "showSpocs", { get: () => true });
      sandbox.stub(feed.cache, "get").resolves(cache);
    });

    it("should return false if nothing in the cache is expired", async () => {
      const result = await feed.checkIfAnyCacheExpired();
      assert.isFalse(result);
    });
    it("should return true if .spocs is missing", async () => {
      delete cache.spocs;
      assert.isTrue(await feed.checkIfAnyCacheExpired());
    });
    it("should return true if .spocs is expired", async () => {
      clock.tick(THIRTY_MINUTES + 1);
      // Update other caches we aren't testing
      cache.spocs.lastUpdated = Date.now();
      cache.feeds["foo.com"].lastUpdate = Date.now();

      assert.isTrue(await feed.checkIfAnyCacheExpired());
    });

    it("should return true if .feeds is missing", async () => {
      delete cache.feeds;
      assert.isTrue(await feed.checkIfAnyCacheExpired());
    });
    it("should return true if data for .feeds[url] is missing", async () => {
      cache.feeds["foo.com"] = null;
      assert.isTrue(await feed.checkIfAnyCacheExpired());
    });
    it("should return true if data for .feeds[url] is expired", async () => {
      clock.tick(THIRTY_MINUTES + 1);
      // Update other caches we aren't testing
      cache.spocs.lastUpdate = Date.now();
      assert.isTrue(await feed.checkIfAnyCacheExpired());
    });
  });

  describe("#refreshAll", () => {
    beforeEach(() => {
      sandbox.stub(feed, "loadLayout").resolves();
      sandbox.stub(feed, "loadComponentFeeds").resolves();
      sandbox.stub(feed, "loadSpocs").resolves();
      sandbox.spy(feed.store, "dispatch");
      Object.defineProperty(feed, "showSpocs", { get: () => true });
    });

    it("should call layout, component, spocs update and telemetry reporting functions", async () => {
      await feed.refreshAll();

      assert.calledOnce(feed.loadLayout);
      assert.calledOnce(feed.loadComponentFeeds);
      assert.calledOnce(feed.loadSpocs);
    });
    it("should pass in dispatch wrapped with broadcast if options.updateOpenTabs is true", async () => {
      await feed.refreshAll({ updateOpenTabs: true });
      [feed.loadLayout, feed.loadComponentFeeds, feed.loadSpocs].forEach(fn => {
        assert.calledOnce(fn);
        const result = fn.firstCall.args[0]({ type: "FOO" });
        assert.isTrue(au.isBroadcastToContent(result));
      });
    });
    it("should pass in dispatch with regular actions if options.updateOpenTabs is false", async () => {
      await feed.refreshAll({ updateOpenTabs: false });
      [feed.loadLayout, feed.loadComponentFeeds, feed.loadSpocs].forEach(fn => {
        assert.calledOnce(fn);
        const result = fn.firstCall.args[0]({ type: "FOO" });
        assert.deepEqual(result, { type: "FOO" });
      });
    });
    it("should set loaded to true if loadSpocs and loadComponentFeeds fails", async () => {
      feed.loadComponentFeeds.rejects("loadComponentFeeds error");
      feed.loadSpocs.rejects("loadSpocs error");

      await feed.enable();

      assert.isTrue(feed.loaded);
    });
    it("should call loadComponentFeeds and loadSpocs in Promise.all", async () => {
      sandbox.stub(global.Promise, "all").resolves();

      await feed.refreshAll();

      assert.calledOnce(global.Promise.all);
      const { args } = global.Promise.all.firstCall;
      assert.equal(args[0].length, 2);
    });
    describe("test startup cache behaviour", () => {
      beforeEach(() => {
        feed._maybeUpdateCachedData.restore();
        sandbox.stub(feed.cache, "set").resolves();
      });
      it("should not refresh layout on startup if it is under THIRTY_MINUTES", async () => {
        feed.loadLayout.restore();
        sandbox.stub(feed.cache, "get").resolves({
          layout: { lastUpdated: Date.now(), layout: {} },
        });
        sandbox.stub(feed, "fetchFromEndpoint").resolves({ layout: {} });

        await feed.refreshAll({ isStartup: true });

        assert.notCalled(feed.fetchFromEndpoint);
      });
      it("should refresh spocs on startup if it was served from cache", async () => {
        feed.loadSpocs.restore();
        sandbox.stub(feed, "getPlacements").returns([{ name: "spocs" }]);
        sandbox.stub(feed.cache, "get").resolves({
          spocs: { lastUpdated: Date.now() },
        });
        clock.tick(THIRTY_MINUTES + 1);

        await feed.refreshAll({ isStartup: true });

        // Once from cache, once to update the store
        assert.calledTwice(feed.store.dispatch);
        assert.equal(
          feed.store.dispatch.firstCall.args[0].type,
          at.DISCOVERY_STREAM_SPOCS_UPDATE
        );
      });
      it("should not refresh spocs on startup if it is under THIRTY_MINUTES", async () => {
        feed.loadSpocs.restore();
        sandbox.stub(feed.cache, "get").resolves({
          spocs: { lastUpdated: Date.now() },
        });
        sandbox.stub(feed, "fetchFromEndpoint").resolves("data");

        await feed.refreshAll({ isStartup: true });

        assert.notCalled(feed.fetchFromEndpoint);
      });
      it("should refresh feeds on startup if it was served from cache", async () => {
        feed.loadComponentFeeds.restore();

        const fakeComponents = { components: [{ feed: { url: "foo.com" } }] };
        const fakeLayout = [fakeComponents];
        const fakeDiscoveryStream = {
          DiscoveryStream: {
            layout: fakeLayout,
          },
          Prefs: {
            values: {
              "feeds.section.topstories": true,
              "feeds.system.topstories": true,
            },
          },
        };
        sandbox.stub(feed.store, "getState").returns(fakeDiscoveryStream);
        sandbox.stub(feed, "rotate").callsFake(val => val);
        sandbox
          .stub(feed, "scoreItems")
          .callsFake(val => ({ data: val, filtered: [], personalized: false }));
        sandbox.stub(feed, "filterBlocked").callsFake(val => ({ data: val }));

        const fakeCache = {
          feeds: { "foo.com": { lastUpdated: Date.now(), data: ["data"] } },
        };
        sandbox.stub(feed.cache, "get").resolves(fakeCache);
        clock.tick(THIRTY_MINUTES + 1);
        sandbox.stub(feed, "fetchFromEndpoint").resolves({
          recommendations: ["data"],
          settings: {
            recsExpireTime: 1,
          },
        });

        await feed.refreshAll({ isStartup: true });

        assert.calledOnce(feed.fetchFromEndpoint);
        // Once from cache, once to update the feed, once to update that all
        // feeds are done, and once to update scores.
        assert.callCount(feed.store.dispatch, 4);
        assert.equal(
          feed.store.dispatch.secondCall.args[0].type,
          at.DISCOVERY_STREAM_FEEDS_UPDATE
        );
      });
    });
  });

  describe("#scoreFeeds", () => {
    beforeEach(() => {
      sandbox.stub(feed.cache, "set").resolves();
      sandbox.spy(feed.store, "dispatch");
    });
    it("should score feeds and set cache, and dispatch", async () => {
      const fakeDiscoveryStream = {
        Prefs: {
          values: {
            "discoverystream.spocs.personalized": true,
            "discoverystream.recs.personalized": false,
          },
        },
        Personalization: {
          initialized: true,
        },
        DiscoveryStream: {
          spocs: {
            placements: [
              { name: "placement1" },
              { name: "placement2" },
              { name: "placement3" },
            ],
          },
        },
      };
      sandbox.stub(feed.store, "getState").returns(fakeDiscoveryStream);
      const fakeFeeds = {
        data: {
          "https://foo.com": {
            data: {
              recommendations: [
                {
                  id: "first",
                  item_score: 0.7,
                },
                {
                  id: "second",
                  item_score: 0.6,
                },
              ],
            },
          },
          "https://bar.com": {
            data: {
              recommendations: [
                {
                  id: "third",
                  item_score: 0.4,
                },
                {
                  id: "fourth",
                  item_score: 0.6,
                },
                {
                  id: "fifth",
                  item_score: 0.8,
                },
              ],
            },
          },
        },
      };
      const feedsTestResult = {
        "https://foo.com": {
          personalized: true,
          data: {
            recommendations: [
              {
                id: "first",
                item_score: 0.7,
                score: 0.7,
              },
              {
                id: "second",
                item_score: 0.6,
                score: 0.6,
              },
            ],
          },
        },
        "https://bar.com": {
          personalized: true,
          data: {
            recommendations: [
              {
                id: "fifth",
                item_score: 0.8,
                score: 0.8,
              },
              {
                id: "fourth",
                item_score: 0.6,
                score: 0.6,
              },
              {
                id: "third",
                item_score: 0.4,
                score: 0.4,
              },
            ],
          },
        },
      };

      await feed.scoreFeeds(fakeFeeds);

      assert.calledWith(feed.cache.set, "feeds", feedsTestResult);
      assert.equal(
        feed.store.dispatch.firstCall.args[0].type,
        at.DISCOVERY_STREAM_FEED_UPDATE
      );
      assert.deepEqual(feed.store.dispatch.firstCall.args[0].data, {
        url: "https://foo.com",
        feed: feedsTestResult["https://foo.com"],
      });
      assert.equal(
        feed.store.dispatch.secondCall.args[0].type,
        at.DISCOVERY_STREAM_FEED_UPDATE
      );
      assert.deepEqual(feed.store.dispatch.secondCall.args[0].data, {
        url: "https://bar.com",
        feed: feedsTestResult["https://bar.com"],
      });
    });

    it("should skip already personalized feeds", async () => {
      sandbox.spy(feed, "scoreItems");
      const recsExpireTime = 5600;
      const fakeFeeds = {
        data: {
          "https://foo.com": {
            personalized: true,
            data: {
              recommendations: [
                {
                  id: "first",
                  item_score: 0.7,
                },
                {
                  id: "second",
                  item_score: 0.6,
                },
              ],
              settings: {
                recsExpireTime,
              },
            },
          },
        },
      };

      await feed.scoreFeeds(fakeFeeds);

      assert.notCalled(feed.scoreItems);
    });
  });

  describe("#scoreSpocs", () => {
    beforeEach(() => {
      sandbox.stub(feed.cache, "set").resolves();
      sandbox.spy(feed.store, "dispatch");
    });
    it("should score spocs and set cache, dispatch", async () => {
      const fakeDiscoveryStream = {
        Prefs: {
          values: {
            "discoverystream.spocs.personalized": true,
            "discoverystream.recs.personalized": false,
          },
        },
        Personalization: {
          initialized: true,
        },
        DiscoveryStream: {
          spocs: {
            placements: [
              { name: "placement1" },
              { name: "placement2" },
              { name: "placement3" },
            ],
          },
        },
      };
      sandbox.stub(feed.store, "getState").returns(fakeDiscoveryStream);
      const fakeSpocs = {
        lastUpdated: 1234,
        data: {
          placement1: {
            items: [
              {
                item_score: 0.6,
              },
              {
                item_score: 0.4,
              },
              {
                item_score: 0.8,
              },
            ],
          },
          placement2: {
            items: [
              {
                item_score: 0.6,
              },
              {
                item_score: 0.8,
              },
            ],
          },
          placement3: { items: [] },
        },
      };

      await feed.scoreSpocs(fakeSpocs);

      const spocsTestResult = {
        lastUpdated: 1234,
        spocs: {
          placement1: {
            personalized: true,
            items: [
              {
                score: 0.8,
                item_score: 0.8,
              },
              {
                score: 0.6,
                item_score: 0.6,
              },
              {
                score: 0.4,
                item_score: 0.4,
              },
            ],
          },
          placement2: {
            personalized: true,
            items: [
              {
                score: 0.8,
                item_score: 0.8,
              },
              {
                score: 0.6,
                item_score: 0.6,
              },
            ],
          },
          placement3: { items: [] },
        },
      };
      assert.calledWith(feed.cache.set, "spocs", spocsTestResult);
      assert.equal(
        feed.store.dispatch.firstCall.args[0].type,
        at.DISCOVERY_STREAM_SPOCS_UPDATE
      );
      assert.deepEqual(
        feed.store.dispatch.firstCall.args[0].data,
        spocsTestResult
      );
    });

    it("should skip already personalized spocs", async () => {
      sandbox.spy(feed, "scoreItems");
      const fakeDiscoveryStream = {
        Prefs: {
          values: {
            "discoverystream.spocs.personalized": true,
            "discoverystream.recs.personalized": false,
          },
        },
        Personalization: {
          initialized: true,
        },
        DiscoveryStream: {
          spocs: {
            placements: [{ name: "placement1" }],
          },
        },
      };
      sandbox.stub(feed.store, "getState").returns(fakeDiscoveryStream);
      const fakeSpocs = {
        lastUpdated: 1234,
        data: {
          placement1: {
            personalized: true,
            items: [
              {
                item_score: 0.6,
              },
              {
                item_score: 0.4,
              },
              {
                item_score: 0.8,
              },
            ],
          },
        },
      };

      await feed.scoreSpocs(fakeSpocs);

      assert.notCalled(feed.scoreItems);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_PERSONALIZATION_UPDATED", () => {
    it("should call scoreFeeds and scoreSpocs if loaded", async () => {
      const fakeDiscoveryStream = {
        Prefs: {
          values: {
            pocketConfig: {
              recsPersonalized: true,
              spocsPersonalized: true,
            },
          },
        },
        DiscoveryStream: {
          feeds: { loaded: false },
          spocs: { loaded: false },
        },
      };

      sandbox.stub(feed, "scoreFeeds").resolves();
      sandbox.stub(feed, "scoreSpocs").resolves();
      Object.defineProperty(feed, "personalized", { get: () => true });
      sandbox.stub(feed.store, "getState").returns(fakeDiscoveryStream);

      await feed.onAction({
        type: at.DISCOVERY_STREAM_PERSONALIZATION_UPDATED,
      });

      assert.notCalled(feed.scoreFeeds);
      assert.notCalled(feed.scoreSpocs);

      fakeDiscoveryStream.DiscoveryStream.feeds.loaded = true;
      fakeDiscoveryStream.DiscoveryStream.spocs.loaded = true;

      await feed.onAction({
        type: at.DISCOVERY_STREAM_PERSONALIZATION_UPDATED,
      });

      assert.calledOnce(feed.scoreFeeds);
      assert.calledOnce(feed.scoreSpocs);
    });
  });

  describe("#onAction: TOPIC_SELECTION_MAYBE_LATER", () => {
    it("should call topicSelectionMaybeLaterEvent", async () => {
      sandbox.stub(feed, "topicSelectionMaybeLaterEvent").resolves();
      await feed.onAction({
        type: at.TOPIC_SELECTION_MAYBE_LATER,
      });
      assert.calledOnce(feed.topicSelectionMaybeLaterEvent);
    });
  });

  describe("#observe", () => {
    it("should call configReset on Pocket button pref change", async () => {
      sandbox.stub(feed, "configReset").returns();
      feed.observe(null, "nsPref:changed", "extensions.pocket.enabled");
      assert.calledOnce(feed.configReset);
    });
  });

  describe("#scoreItem", () => {
    it("should call calculateItemRelevanceScore with recommendationProvider with initial score", async () => {
      const item = {
        item_score: 0.6,
      };
      feed.recommendationProvider.store.getState = () => ({
        Prefs: {
          values: {
            pocketConfig: {
              recsPersonalized: true,
              spocsPersonalized: true,
            },
            "discoverystream.personalization.enabled": true,
            "feeds.section.topstories": true,
            "feeds.system.topstories": true,
          },
        },
      });
      feed.recommendationProvider.calculateItemRelevanceScore = sandbox
        .stub()
        .returns();
      const result = await feed.scoreItem(item, true);
      assert.calledOnce(
        feed.recommendationProvider.calculateItemRelevanceScore
      );
      assert.equal(result.score, 0.6);
    });
    it("should fallback to score 1 without an initial score", async () => {
      const item = {};
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.personalized": true,
            "discoverystream.recs.personalized": true,
            "discoverystream.personalization.enabled": true,
          },
        },
      });
      feed.recommendationProvider.calculateItemRelevanceScore = sandbox
        .stub()
        .returns();
      const result = await feed.scoreItem(item, true);
      assert.equal(result.score, 1);
    });
  });
  describe("new proxy feed", () => {
    beforeEach(() => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            pocketConfig: { regionBffConfig: "DE" },
          },
        },
      });
      sandbox.stub(global.Region, "home").get(() => "DE");
      sandbox.stub(global.Services.prefs, "getStringPref");
      global.Services.prefs.getStringPref
        .withArgs("extensions.pocket.bffApi")
        .returns("bffApi");
      global.Services.prefs.getStringPref
        .withArgs("extensions.pocket.oAuthConsumerKeyBff")
        .returns("oAuthConsumerKeyBff");
    });
    it("should return true with isBff", async () => {
      assert.isUndefined(feed._isBff);
      assert.isTrue(feed.isBff);
      assert.isTrue(feed._isBff);
    });
    it("should update to new feed url", async () => {
      await feed.loadLayout(feed.store.dispatch);
      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(
        layout[0].components[2].feed.url,
        "https://bffApi/desktop/v1/recommendations?locale=$locale&region=$region&count=30"
      );
    });
    it("should update the new feed url with pocketFeedParameters", async () => {
      globals.set("NimbusFeatures", {
        pocketNewtab: {
          getVariable: sandbox.stub(),
        },
      });
      global.NimbusFeatures.pocketNewtab.getVariable
        .withArgs("pocketFeedParameters")
        .returns("&enableRankingByRegion=1");
      await feed.loadLayout(feed.store.dispatch);
      const { layout } = feed.store.getState().DiscoveryStream;
      assert.equal(
        layout[0].components[2].feed.url,
        "https://bffApi/desktop/v1/recommendations?locale=$locale&region=$region&count=30&enableRankingByRegion=1"
      );
    });
    it("should fetch proper data from getComponentFeed", async () => {
      const fakeCache = {};
      sandbox.stub(feed.cache, "get").returns(Promise.resolve(fakeCache));
      sandbox.stub(feed, "rotate").callsFake(val => val);
      sandbox
        .stub(feed, "scoreItems")
        .callsFake(val => ({ data: val, filtered: [], personalized: false }));
      sandbox.stub(feed, "fetchFromEndpoint").resolves({
        data: [
          {
            recommendationId: "decaf-c0ff33",
            tileId: 1234,
            url: "url",
            title: "title",
            excerpt: "excerpt",
            publisher: "publisher",
            timeToRead: "timeToRead",
            imageUrl: "imageUrl",
          },
        ],
      });

      const feedData = await feed.getComponentFeed("url");
      assert.deepEqual(feedData, {
        lastUpdated: 0,
        personalized: false,
        data: {
          settings: {},
          sections: [],
          interestPicker: {},
          surfaceId: "",
          recommendations: [
            {
              id: 1234,
              url: "url",
              title: "title",
              excerpt: "excerpt",
              publisher: "publisher",
              time_to_read: "timeToRead",
              raw_image_src: "imageUrl",
              recommendation_id: "decaf-c0ff33",
            },
          ],
          status: "success",
        },
      });
      assert.equal(feed.fetchFromEndpoint.firstCall.args[0], "url");
      assert.equal(feed.fetchFromEndpoint.firstCall.args[1].method, "GET");
      assert.equal(
        feed.fetchFromEndpoint.firstCall.args[1].headers.get("consumer_key"),
        "oAuthConsumerKeyBff"
      );
    });
  });
});
