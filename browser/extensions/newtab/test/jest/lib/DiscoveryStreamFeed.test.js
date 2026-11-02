/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  actionCreators as ac,
  actionTypes as at,
  actionUtils as au,
} from "common/Actions.mjs";
import { combineReducers, createStore } from "redux";
import { mockServices, stubGlobals } from "test/jest/test-utils";
import { DiscoveryStreamFeed } from "lib/DiscoveryStreamFeed.sys.mjs";
import { reducers } from "common/Reducers.sys.mjs";

import { PersistentCache } from "lib/PersistentCache.sys.mjs";
import {
  SectionsLayoutManager,
  maskLayoutAds,
} from "lib/SectionsLayoutFeed.sys.mjs";

const CONFIG_PREF_NAME = "discoverystream.config";
const ENDPOINTS_PREF_NAME = "discoverystream.endpoints";
const DUMMY_ENDPOINT = "https://getpocket.cdn.mozilla.net/dummy";
const SPOC_IMPRESSION_TRACKING_PREF = "discoverystream.spoc.impressions";
const THIRTY_MINUTES = 30 * 60 * 1000;
const ONE_WEEK = 7 * 24 * 60 * 60 * 1000; // 1 week

const FAKE_UUID = "{foo-123-foo}";

const DEFAULT_COLUMN_COUNT = 4;
const DEFAULT_ROW_COUNT = 6;

// eslint-disable-next-line max-statements
describe("DiscoveryStreamFeed", () => {
  let feed;
  let feeds;
  let fetchStub;
  let fakeNewTabUtils;
  let services;
  let restoreGlobals;
  let surfaceIdSet;
  let surfaceIdRejectedRecord;

  // The feed logs the failures these cases drive it into. Swap out the spy the
  // jest-setup console.error guard installed for the duration of the case, so
  // that expected logging is not reported against it. jest-setup puts the real
  // console.error back in its afterEach.
  const expectConsoleError = () => {
    console.error = jest.fn();
    return console.error;
  };

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

  const stubOutFetchFromEndpointWithRealisticData = (overrides = {}) => {
    jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
      recommendedAt: 1755834072383,
      surfaceId: "NEW_TAB_EN_US",
      data: [
        {
          corpusItemId: "decaf-c0ff33",
          scheduledCorpusItemId: "matcha-latte-ff33c1",
          excerpt: "excerpt",
          iconUrl: "iconUrl",
          imageUrl: "imageUrl",
          isTimeSensitive: true,
          publisher: "publisher",
          receivedRank: 0,
          tileId: 12345,
          title: "title",
          topic: "topic",
          url: "url",
          features: {},
        },
        {
          corpusItemId: "decaf-c0ff34",
          scheduledCorpusItemId: "matcha-latte-ff33c2",
          excerpt: "excerpt",
          iconUrl: "iconUrl",
          imageUrl: "imageUrl",
          isTimeSensitive: true,
          publisher: "publisher",
          receivedRank: 0,
          tileId: 12346,
          title: "title",
          topic: "topic",
          url: "url",
          features: {},
        },
      ],
      settings: {
        recsExpireTime: 1,
      },
      ...overrides,
    });
  };

  beforeEach(() => {
    // Fetch
    fetchStub = jest.fn();

    // Time
    jest.useFakeTimers({ now: 0 });

    fakeNewTabUtils = {
      blockedLinks: {
        links: [],
        isBlocked: () => false,
      },
      getUtcOffset: () => 0,
    };

    services = mockServices(["locale", "obs", "prefs", "uuid"]);
    services.uuid.generateUUID.mockReturnValue(FAKE_UUID);
    services.prefs.getBoolPref.mockImplementation(name =>
      name === "browser.newtabpage.activity-stream.discoverystream.enabled"
        ? true
        : undefined
    );

    surfaceIdSet = jest.fn();
    surfaceIdRejectedRecord = jest.fn();
    restoreGlobals = stubGlobals({
      fetch: fetchStub,
      Glean: {
        newtabContent: {
          surfaceId: { set: surfaceIdSet },
          surfaceIdRejected: { record: surfaceIdRejectedRecord },
        },
      },
      Services: services,
      PersistentCache,
      PathUtils: {
        join: (...parts) => parts[parts.length - 1],
        localProfileDir: "localProfileDir",
      },
      IOUtils: {
        readJSON: () => Promise.resolve({}),
        writeJSON: () => Promise.resolve(0),
      },
      NewTabUtils: fakeNewTabUtils,
      ContextId: {
        request: () => "ContextId",
      },
      AdsClient: {
        isEnabled: () => false,
        getClient: () => null,
      },
      MozAdsPlacementRequestWithCount: class {
        constructor(obj) {
          this.obj = obj;
        }
      },
      MozAdsIabContent: class {
        constructor(obj) {
          this.obj = obj;
        }
      },
      MozAdsIabContentTaxonomy: {},
      NimbusFeatures: {
        pocketNewtab: {
          getEnrollmentMetadata: jest.fn(),
          onUpdate: jest.fn(),
          offUpdate: jest.fn(),
        },
      },
      ObliviousHTTP: {
        getOHTTPConfig: () => {},
        ohttpRequest: () => {},
      },
      Region: { home: "US" },
      RemoteSettings: { pollChanges: jest.fn() },
      // Only read through the lazy.userAgent getter.
      Cc: {
        "@mozilla.org/network/protocol;1?name=http": {
          getService() {
            return this;
          },
        },
      },
      Ci: { nsIHttpProtocolHandler: {} },
      // The layout globals are only installed by the tests that need them.
      SectionsLayoutManager: undefined,
      maskLayoutAds: undefined,
    });

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
          "system.showSponsored": false,
          "discoverystream.spocs.startupCache.enabled": true,
          "unifiedAds.adsFeed.enabled": false,
        },
      },
    });
    feed.store.feeds = {
      get: name => feeds[name],
    };

    jest.spyOn(feed, "_maybeUpdateCachedData").mockResolvedValue();
  });

  afterEach(() => {
    jest.useRealTimers();
    jest.restoreAllMocks();
    restoreGlobals();
  });

  describe("#fetchFromEndpoint", () => {
    beforeEach(() => {
      fetchStub.mockResolvedValue({
        json: () => Promise.resolve("hi"),
        ok: true,
      });
    });
    it("should get a response", async () => {
      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      expect(response).toBe("hi");
    });
    it("should not send cookies", async () => {
      await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      expect(fetchStub.mock.calls[0][1]).toHaveProperty("credentials", "omit");
    });
    it("should allow unexpected response", async () => {
      expectConsoleError();
      fetchStub.mockResolvedValue({ ok: false });

      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      expect(response).toBe(null);
    });
    it("should disallow unexpected endpoints", async () => {
      expectConsoleError();
      feed.store.getState = () => ({
        Prefs: {
          values: {
            [ENDPOINTS_PREF_NAME]: "https://other.site",
          },
        },
      });

      const response = await feed.fetchFromEndpoint(DUMMY_ENDPOINT);

      expect(response).toBe(null);
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

      expect(response).toBe("hi");
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

      expect(response).toBe("hi");
    });
    it("should allow POST and with other options", async () => {
      await feed.fetchFromEndpoint("https://getpocket.cdn.mozilla.net/dummy", {
        method: "POST",
        body: "{}",
      });

      expect(fetchStub).toHaveBeenCalledWith(
        "https://getpocket.cdn.mozilla.net/dummy",
        expect.objectContaining({
          credentials: "omit",
          method: "POST",
          body: "{}",
        })
      );
    });

    it("should use OHTTP when configured and enabled", async () => {
      services.prefs.getStringPref.mockImplementation(name => {
        switch (name) {
          case "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL":
            return "https://relay.url";
          case "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL":
            return "https://config.url";
          default:
            return undefined;
        }
      });

      const fakeOhttpConfig = { config: "config" };
      jest
        .spyOn(globalThis.ObliviousHTTP, "getOHTTPConfig")
        .mockResolvedValue(fakeOhttpConfig);

      const ohttpResponse = {
        json: () => Promise.resolve("ohttp response"),
        ok: true,
      };
      const ohttpRequestStub = jest
        .spyOn(globalThis.ObliviousHTTP, "ohttpRequest")
        .mockResolvedValue(ohttpResponse);

      // Allow the endpoint
      feed.store.getState = () => ({
        Prefs: {
          values: {
            [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
          },
        },
      });

      const result = await feed.fetchFromEndpoint(DUMMY_ENDPOINT, {}, true);

      expect(result).toBe("ohttp response");
      expect(ohttpRequestStub).toHaveBeenCalledTimes(1);
      expect(ohttpRequestStub).toHaveBeenCalledWith(
        "https://relay.url",
        fakeOhttpConfig,
        DUMMY_ENDPOINT,
        expect.anything()
      );
    });

    it("should cast headers from a Headers object to JS object when using OHTTP", async () => {
      services.prefs.getStringPref.mockImplementation(name => {
        switch (name) {
          case "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL":
            return "https://relay.url";
          case "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL":
            return "https://config.url";
          default:
            return undefined;
        }
      });

      const fakeOhttpConfig = { config: "config" };
      jest
        .spyOn(globalThis.ObliviousHTTP, "getOHTTPConfig")
        .mockResolvedValue(fakeOhttpConfig);

      const ohttpResponse = {
        json: () => Promise.resolve("ohttp response"),
        ok: true,
      };
      const ohttpRequestStub = jest
        .spyOn(globalThis.ObliviousHTTP, "ohttpRequest")
        .mockResolvedValue(ohttpResponse);

      // Allow the endpoint
      feed.store.getState = () => ({
        Prefs: {
          values: {
            [ENDPOINTS_PREF_NAME]: DUMMY_ENDPOINT,
          },
        },
      });

      const headers = new Headers();
      headers.set("headername", "headervalue");

      const result = await feed.fetchFromEndpoint(
        DUMMY_ENDPOINT,
        { headers },
        true
      );

      expect(result).toBe("ohttp response");
      expect(ohttpRequestStub).toHaveBeenCalledTimes(1);
      expect(ohttpRequestStub).toHaveBeenCalledWith(
        "https://relay.url",
        fakeOhttpConfig,
        DUMMY_ENDPOINT,
        expect.objectContaining({
          headers: Object.fromEntries(headers),
          credentials: "omit",
        })
      );
    });
  });

  describe("#parseGridPositions", () => {
    it("should return an equivalent array for an array of non negative integers", async () => {
      expect(feed.parseGridPositions([0, 2, 3])).toEqual([0, 2, 3]);
    });
    it("should return undefined for an array containing negative integers", async () => {
      expect(feed.parseGridPositions([-2, 2, 3])).toBe(undefined);
    });
    it("should return undefined for an undefined input", async () => {
      expect(feed.parseGridPositions(undefined)).toBe(undefined);
    });
  });

  describe("#loadLayout", () => {
    it("should use local basic layout with hardcoded_basic_layout being true", async () => {
      feed.config.hardcoded_basic_layout = true;

      await feed.loadLayout(feed.store.dispatch);

      expect(feed.store.getState().DiscoveryStream.spocs.spocs_endpoint).toBe(
        "https://spocs.getpocket.com/spocs"
      );
      const { layout } = feed.store.getState().DiscoveryStream;
      expect(layout[0].components[2].properties.items).toBe(
        DEFAULT_COLUMN_COUNT
      );
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
      expect(layout[0].components[2].properties.items).toBe(
        DEFAULT_COLUMN_COUNT
      );
    });
    it("should use 6 row layout if specified", async () => {
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
      expect(layout[0].components[2].properties.items).toBe(
        DEFAULT_ROW_COUNT * DEFAULT_COLUMN_COUNT
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

      expect(feed.store.getState().DiscoveryStream.spocs.spocs_endpoint).toBe(
        "https://spocs.getpocket.com/spocs"
      );
      const { layout } = feed.store.getState().DiscoveryStream;
      expect(layout[0].components[2].properties.items).toBe(
        DEFAULT_COLUMN_COUNT
      );
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

      expect(feed.store.getState().DiscoveryStream.spocs.spocs_endpoint).toBe(
        "https://spocs.getpocket.com/spocs2"
      );
    });
    it("should return enough stories to fill a four card layout", async () => {
      expectConsoleError();
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            pocketConfig: { fourCardLayout: true },
          },
        },
      });

      await feed.loadLayout(feed.store.dispatch);

      const { layout } = feed.store.getState().DiscoveryStream;
      expect(layout[0].components[2].properties.items).toBe(
        DEFAULT_ROW_COUNT * DEFAULT_COLUMN_COUNT
      );
    });
    it("should create a layout with spoc and widget positions", async () => {
      expectConsoleError();
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
      expect(layout[0].components[2].spocs.positions).toEqual([
        { index: 1 },
        { index: 2 },
      ]);
      expect(layout[0].components[2].widgets.positions).toEqual([
        { index: 3 },
        { index: 4 },
      ]);
    });
    it("should create a layout with spoc position data", async () => {
      expectConsoleError();
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
      expect(layout[0].components[2].placement.ad_types).toEqual([1230]);
      expect(layout[0].components[2].placement.zone_ids).toEqual([4560, 7890]);
    });
    it("should create a layout with proper spoc url with a site id", async () => {
      expectConsoleError();
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
      expect(spocs.spocs_endpoint).toEqual(
        "https://spocs.getpocket.com/spocs?site=1234"
      );
    });
  });

  describe("#updatePlacements", () => {
    it("should dispatch DISCOVERY_STREAM_SPOCS_PLACEMENTS", () => {
      jest.spyOn(feed.store, "dispatch");
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

      expect(feed.store.dispatch).toHaveBeenCalledTimes(1);
      expect(feed.store.dispatch).toHaveBeenCalledWith({
        type: "DISCOVERY_STREAM_SPOCS_PLACEMENTS",
        data: { placements: [{ name: "first" }, { name: "second" }] },
        meta: { isStartup: false },
      });
    });
    it("should fire update placements from loadLayout", async () => {
      jest.spyOn(feed, "updatePlacements");

      await feed.loadLayout(feed.store.dispatch);

      expect(feed.updatePlacements).toHaveBeenCalledTimes(1);
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

      expect(items).toEqual(["first", "second"]);
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
      jest.spyOn(feed.store, "getState").mockReturnValue(fakeDiscoveryStream);
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
    });

    afterEach(() => {
      jest.restoreAllMocks();
    });

    it("should not dispatch updates when layout is not defined", async () => {
      fakeDiscoveryStream = {
        DiscoveryStream: {},
      };
      feed.store.getState.mockReturnValue(fakeDiscoveryStream);
      jest.spyOn(feed.store, "dispatch");

      await feed.loadComponentFeeds(feed.store.dispatch);

      expect(feed.store.dispatch).not.toHaveBeenCalled();
    });

    it("should populate feeds cache", async () => {
      fakeCache = {
        feeds: { "foo.com": { lastUpdated: Date.now(), data: "data" } },
      };
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));

      await feed.loadComponentFeeds(feed.store.dispatch);

      expect(feed.cache.set).toHaveBeenCalledWith("feeds", {
        "foo.com": { data: "data", lastUpdated: 0 },
      });
    });

    it("should send feed update events with new feed data", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed.store, "dispatch");

      await feed.loadComponentFeeds(feed.store.dispatch);

      expect(feed.store.dispatch).toHaveBeenNthCalledWith(1, {
        type: at.DISCOVERY_STREAM_FEED_UPDATE,
        data: { feed: { data: { status: "failed" } }, url: "foo.com" },
        meta: { isStartup: false },
      });
      expect(feed.store.dispatch).toHaveBeenNthCalledWith(2, {
        type: at.DISCOVERY_STREAM_FEEDS_UPDATE,
        meta: { isStartup: false },
      });
    });

    it("should return number of promises equal to unique urls", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(Promise, "all").mockResolvedValue();
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
      feed.store.getState.mockReturnValue(fakeDiscoveryStream);

      await feed.loadComponentFeeds(feed.store.dispatch);

      expect(Promise.all).toHaveBeenCalledTimes(1);
      const [args] = Promise.all.mock.lastCall;
      expect(args.length).toBe(3);
    });
  });

  describe("#formatComponentFeedRequest location interests", () => {
    const engagementVector = { values: ["100"], model_id: "engagement" };
    const locationVector = { values: ["010"], model_id: "location" };

    it.each([
      [true, true, locationVector, locationVector],
      [true, false, locationVector, engagementVector],
      [false, true, locationVector, engagementVector],
      [true, true, null, engagementVector],
    ])(
      "selects interests with enabled=%s, fallback=%s, location=%s",
      (enabled, shouldUseLocationVector, location, expected) => {
        for (const [name, value] of Object.entries({
          "discoverystream.sections.personalization.inferred.user.enabled": true,
          "discoverystream.sections.personalization.inferred.enabled": true,
          "discoverystream.merino-provider.ohttp.enabled": true,
          trainhopConfig: { locationInitInferred: { enabled } },
        })) {
          feed.store.dispatch({ type: at.PREF_CHANGED, data: { name, value } });
        }
        feed.store.dispatch({
          type: at.INFERRED_PERSONALIZATION_UPDATE,
          data: {
            coarsePrivateInferredInterests: engagementVector,
            locationPrivateInferredInterests: location,
            shouldUseLocationVector,
          },
        });

        const body = JSON.parse(feed.formatComponentFeedRequest().body);
        expect(body.inferredInterests).toEqual(expected);
        expect(
          feed.store.getState().InferredPersonalization
            .coarsePrivateInferredInterests
        ).toEqual(engagementVector);
      }
    );
  });

  describe("#getComponentFeed", () => {
    it("should fetch fresh feed data if cache is empty", async () => {
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest
        .spyOn(feed, "scoreItemsInferred")
        .mockImplementation(val => ({ data: val, filtered: [] }));
      stubOutFetchFromEndpointWithRealisticData();

      const feedResp = await feed.getComponentFeed("foo.com");
      expect(feedResp.data.recommendations.length).toBe(2);
    });
    it("should fetch fresh feed data if cache is old", async () => {
      const fakeCache = { feeds: { "foo.com": { lastUpdated: Date.now() } } };
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      stubOutFetchFromEndpointWithRealisticData();
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest
        .spyOn(feed, "scoreItemsInferred")
        .mockImplementation(val => ({ data: val, filtered: [] }));
      jest.advanceTimersByTime(THIRTY_MINUTES + 1);

      const feedResp = await feed.getComponentFeed("foo.com");

      expect(feedResp.data.recommendations.length).toBe(2);
    });
    it("should return feed data from cache if it is fresh", async () => {
      const fakeCache = {
        feeds: { "foo.com": { lastUpdated: Date.now(), data: "data" } },
      };
      jest.spyOn(feed.cache, "get").mockResolvedValue(fakeCache);
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue("old data");
      jest.advanceTimersByTime(THIRTY_MINUTES - 1);

      const feedResp = await feed.getComponentFeed("foo.com");

      expect(feedResp.data).toBe("data");
    });
    it("should return null if no response was received", async () => {
      expectConsoleError();
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue(null);

      const feedResp = await feed.getComponentFeed("foo.com");

      expect(feedResp).toEqual({ data: { status: "failed" } });
    });
    function stubOutComponentFeedDeps(responseOverrides) {
      jest.spyOn(feed.cache, "get").mockResolvedValue({});
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest
        .spyOn(feed, "scoreItemsInferred")
        .mockImplementation(val => ({ data: val, filtered: [] }));
      stubOutFetchFromEndpointWithRealisticData(responseOverrides);
    }

    function setPrefCalledFor(dispatchSpy, name) {
      return dispatchSpy.mock.calls.some(
        ([action]) =>
          action?.type === at.SET_PREF && action?.data?.name === name
      );
    }

    it("should record surfaceId in Glean when the private ping is enabled", async () => {
      stubOutComponentFeedDeps();
      setPref("telemetry.privatePing.enabled", true);

      await feed.getComponentFeed("foo.com");

      expect(surfaceIdSet).toHaveBeenCalledWith("NEW_TAB_EN_US");
      expect(surfaceIdRejectedRecord).not.toHaveBeenCalled();
    });
    it("should record surfaceId in Glean when the private ping is disabled", async () => {
      stubOutComponentFeedDeps();
      setPref("telemetry.privatePing.enabled", false);

      await feed.getComponentFeed("foo.com");

      expect(surfaceIdSet).toHaveBeenCalledWith("NEW_TAB_EN_US");
    });
    it("should not update the surfaceId pref when the private ping is disabled", async () => {
      stubOutComponentFeedDeps();
      setPref("telemetry.privatePing.enabled", false);
      const dispatchSpy = jest.spyOn(feed.store, "dispatch");

      await feed.getComponentFeed("foo.com");

      expect(setPrefCalledFor(dispatchSpy, "telemetry.surfaceId")).toBe(false);
    });
    it("should update the surfaceId pref when the private ping is enabled", async () => {
      stubOutComponentFeedDeps();
      setPref("telemetry.privatePing.enabled", true);
      const dispatchSpy = jest.spyOn(feed.store, "dispatch");

      await feed.getComponentFeed("foo.com");

      expect(setPrefCalledFor(dispatchSpy, "telemetry.surfaceId")).toBe(true);
    });
    it("should drop a surfaceId that doesn't match the request", async () => {
      stubOutComponentFeedDeps({ surfaceId: "NEW_TAB_DE_AT" });
      jest.spyOn(console, "warn").mockImplementation(() => {});
      setPref("telemetry.privatePing.enabled", true);
      const dispatchSpy = jest.spyOn(feed.store, "dispatch");

      const { data } = await feed.getComponentFeed("foo.com");

      expect(data.surfaceId).toBe("");
      expect(surfaceIdSet).not.toHaveBeenCalled();
      expect(surfaceIdRejectedRecord).toHaveBeenCalledWith();
      expect(setPrefCalledFor(dispatchSpy, "telemetry.surfaceId")).toBe(false);
      expect(dispatchSpy).toHaveBeenCalledWith({
        type: at.CLEAR_PREF,
        data: { name: "telemetry.surfaceId" },
      });
    });
  });

  describe("#formatComponentFeedRequest utc_offset", () => {
    beforeEach(() => {
      feed.store.dispatch({
        type: at.PREF_CHANGED,
        data: {
          name: "inferredPersonalizationConfig",
          value: { normalized_time_zone_offset: true },
        },
      });
    });
    it("should send utc_offset when a surfaceId is known", () => {
      setPref("telemetry.surfaceId", "NEW_TAB_EN_US");

      const body = JSON.parse(feed.formatComponentFeedRequest().body);

      expect(body).toHaveProperty("utc_offset", 0);
    });
    it("should omit utc_offset when no surfaceId is known", () => {
      const body = JSON.parse(feed.formatComponentFeedRequest().body);

      expect(body).not.toHaveProperty("utc_offset");
    });
  });

  describe("#loadSpocs", () => {
    beforeEach(() => {
      jest.spyOn(feed, "getPlacements").mockReturnValue([{ name: "spocs" }]);
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
    });
    it("should not fetch or update cache if no spocs endpoint is defined", async () => {
      expectConsoleError();
      feed.store.dispatch(
        ac.BroadcastToContent({
          type: at.DISCOVERY_STREAM_SPOCS_ENDPOINT,
          data: "",
        })
      );

      jest.spyOn(feed.cache, "set");

      await feed.loadSpocs(feed.store.dispatch);

      expect(globalThis.fetch).not.toHaveBeenCalled();
      expect(feed.cache.set).toHaveBeenCalledWith("spocs", {
        lastUpdated: 0,
        spocs: {},
        spocsOnDemand: undefined,
        spocsCacheUpdateTime: 30 * 60 * 1000,
      });
    });
    it("should fetch fresh spocs data if cache is empty", async () => {
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockResolvedValue({ placement: "data" });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.cache.set).toHaveBeenCalledWith("spocs", {
        spocs: { placement: "data" },
        lastUpdated: 0,
        spocsOnDemand: undefined,
        spocsCacheUpdateTime: 30 * 60 * 1000,
      });
      expect(feed.store.getState().DiscoveryStream.spocs.data.placement).toBe(
        "data"
      );
    });
    it("should not send an impression id in the request body", async () => {
      jest.spyOn(feed.cache, "get").mockResolvedValue();
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockResolvedValue({ placement: "data" });
      jest.spyOn(feed.cache, "set").mockResolvedValue();

      await feed.loadSpocs(feed.store.dispatch);

      const [[, options]] = feed.fetchFromEndpoint.mock.calls;
      expect(JSON.parse(options.body)).not.toHaveProperty("pocket_id");
    });
    it("should fetch fresh data if cache is old", async () => {
      const cachedSpoc = {
        spocs: { placement: "old" },
        lastUpdated: Date.now(),
      };
      const cachedData = { spocs: cachedSpoc };
      jest
        .spyOn(feed.cache, "get")
        .mockReturnValue(Promise.resolve(cachedData));
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockResolvedValue({ placement: "new" });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      jest.advanceTimersByTime(THIRTY_MINUTES + 1);

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.store.getState().DiscoveryStream.spocs.data.placement).toBe(
        "new"
      );
    });
    it("should return spoc data from cache if it is fresh", async () => {
      const cachedSpoc = {
        spocs: { placement: "old" },
        lastUpdated: Date.now(),
      };
      const cachedData = { spocs: cachedSpoc };
      jest
        .spyOn(feed.cache, "get")
        .mockReturnValue(Promise.resolve(cachedData));
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockResolvedValue({ placement: "new" });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      jest.advanceTimersByTime(THIRTY_MINUTES - 1);

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.store.getState().DiscoveryStream.spocs.data.placement).toBe(
        "old"
      );
    });
    it("should properly transform spocs using placements", async () => {
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        spocs: { items: [{ id: "data" }] },
      });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      const loadTimestamp = 100;
      jest.advanceTimersByTime(loadTimestamp);

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.cache.set).toHaveBeenCalledWith("spocs", {
        spocs: {
          spocs: {
            context: "",
            title: "",
            sponsor: "",
            sponsored_by_override: undefined,
            items: [{ id: "data", score: 1 }],
          },
        },
        lastUpdated: loadTimestamp,
        spocsOnDemand: undefined,
        spocsCacheUpdateTime: 30 * 60 * 1000,
      });

      expect(
        feed.store.getState().DiscoveryStream.spocs.data.spocs.items[0]
      ).toEqual({ id: "data", score: 1 });
    });
    it("should normalizeSpocsItems for older spoc data", async () => {
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockResolvedValue({ spocs: [{ id: "data" }] });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      await feed.loadSpocs(feed.store.dispatch);

      expect(
        feed.store.getState().DiscoveryStream.spocs.data.spocs.items[0]
      ).toEqual({ id: "data", score: 1 });
    });
    it("should return expected data if normalizeSpocsItems returns no spoc data", async () => {
      // We don't need this for just this test, we are setting placements
      // manually.
      feed.getPlacements.mockRestore();

      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockResolvedValue({ placement1: [{ id: "data" }], placement2: [] });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      const fakeComponents = {
        components: [
          { placement: { name: "placement1" }, spocs: {} },
          { placement: { name: "placement2" }, spocs: {} },
        ],
      };
      feed.updatePlacements(feed.store.dispatch, [fakeComponents]);

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.store.getState().DiscoveryStream.spocs.data).toEqual({
        placement1: {
          title: "",
          context: "",
          sponsor: "",
          sponsored_by_override: undefined,
          items: [{ id: "data", score: 1 }],
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
      feed.getPlacements.mockRestore();
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        placement1: {
          title: "title",
          context: "context",
          sponsor: "",
          sponsored_by_override: undefined,
          items: [{ id: "data" }],
        },
      });
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      const fakeComponents = {
        components: [{ placement: { name: "placement1" }, spocs: {} }],
      };
      feed.updatePlacements(feed.store.dispatch, [fakeComponents]);

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.store.getState().DiscoveryStream.spocs.data).toEqual({
        placement1: {
          title: "title",
          context: "context",
          sponsor: "",
          sponsored_by_override: undefined,
          items: [{ id: "data", score: 1 }],
        },
      });
    });
    it("should fetch MARS pre flight info", async () => {
      expectConsoleError();
      jest
        .spyOn(feed, "fetchFromEndpoint")
        .mockImplementation((endpoint, options) =>
          endpoint === "unifiedAdEndpoint/v1/ads-preflight" &&
          options?.method === "GET"
            ? Promise.resolve({
                normalized_ua: "normalized_ua",
                geoname_id: "geoname_id",
                geo_location: "geo_location",
              })
            : undefined
        );

      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            "unifiedAds.endpoint": "unifiedAdEndpoint/",
            "unifiedAds.blockedAds": "",
            "unifiedAds.spocs.enabled": true,
            "discoverystream.placements.spocs": "newtab_stories_1",
            "discoverystream.placements.spocs.counts": "1",
            "unifiedAds.ohttp.enabled": true,
          },
        },
      });

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.fetchFromEndpoint.mock.calls[0][0]).toBe(
        "unifiedAdEndpoint/v1/ads-preflight"
      );
      expect(feed.fetchFromEndpoint.mock.calls[0][1].method).toBe("GET");
      expect(feed.fetchFromEndpoint.mock.calls[1][0]).toBe(
        "unifiedAdEndpoint/v1/ads"
      );
      expect(
        feed.fetchFromEndpoint.mock.calls[1][1].headers.get("X-User-Agent")
      ).toBe("normalized_ua");
      expect(
        feed.fetchFromEndpoint.mock.calls[1][1].headers.get("X-Geoname-ID")
      ).toBe("geoname_id");
      expect(
        feed.fetchFromEndpoint.mock.calls[1][1].headers.get("X-Geo-Location")
      ).toBe("geo_location");
    });
    it("should fetch ads with empty flags if adsBackend flags are empty", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            "unifiedAds.endpoint": "unifiedAdEndpoint/",
            "unifiedAds.blockedAds": "",
            "unifiedAds.spocs.enabled": true,
            "discoverystream.placements.spocs": "newtab_stories_1",
            "discoverystream.placements.spocs.counts": "1",
            "unifiedAds.ohttp.enabled": true,
          },
        },
      });

      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        newtab_stories_1: [
          {
            format: "spoc",
            title: "ad title",
          },
        ],
      });

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.fetchFromEndpoint.mock.calls[1][0]).toBe(
        "unifiedAdEndpoint/v1/ads"
      );
      expect(feed.fetchFromEndpoint.mock.calls[1][1].body).toBe(
        JSON.stringify({
          context_id: "ContextId",
          flags: {},
          placements: [
            {
              placement: "newtab_stories_1",
              count: 1,
            },
          ],
          blocks: [""],
        })
      );
    });
    it("should fetch ads with adsBackend flags", async () => {
      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            "unifiedAds.endpoint": "unifiedAdEndpoint/",
            "unifiedAds.blockedAds": "",
            "unifiedAds.spocs.enabled": true,
            "discoverystream.placements.spocs": "newtab_stories_1",
            "discoverystream.placements.spocs.counts": "1",
            "unifiedAds.ohttp.enabled": true,
            adsBackendConfig: {
              feature1: true,
              feature2: false,
            },
          },
        },
      });

      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        newtab_stories_1: [
          {
            format: "spoc",
            title: "ad title",
          },
        ],
      });

      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.fetchFromEndpoint.mock.calls[1][0]).toBe(
        "unifiedAdEndpoint/v1/ads"
      );
      expect(feed.fetchFromEndpoint.mock.calls[1][1].body).toBe(
        JSON.stringify({
          context_id: "ContextId",
          flags: {
            feature1: true,
            feature2: false,
          },
          placements: [
            {
              placement: "newtab_stories_1",
              count: 1,
            },
          ],
          blocks: [""],
        })
      );
    });
    it("should use adsClient when enabled", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            "unifiedAds.blockedAds": "",
            "unifiedAds.spocs.enabled": true,
            "discoverystream.placements.spocs": "newtab_stories_1",
            "discoverystream.placements.spocs.counts": "1",
          },
        },
      });

      const ADS_CLIENT = {
        requestSpocAds: jest.fn().mockResolvedValue(
          new Map([
            [
              "newtab_stories_1",
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

      const REQUEST_OPTIONS = {
        flags: new Map(),
        ohttp: true,
      };

      const AdsClient = {
        isEnabled: jest.fn().mockReturnValue(true),
        getClient: jest.fn().mockReturnValue(ADS_CLIENT),
        requestOptions: jest.fn().mockReturnValue(REQUEST_OPTIONS),
      };

      globalThis.AdsClient = AdsClient;

      await feed.onAction({
        type: at.INIT,
      });
      expect(AdsClient.isEnabled).toHaveBeenCalledTimes(1);
      expect(AdsClient.getClient).toHaveBeenCalledTimes(1);

      await feed.loadSpocs(feed.store.dispatch);

      // The flags in adsBackendConfig only reach MARS if the prefs are passed.
      expect(AdsClient.requestOptions).toHaveBeenCalledWith(
        feed.store.getState().Prefs.values
      );
      expect(ADS_CLIENT.requestSpocAds).toHaveBeenCalledTimes(1);
      expect(ADS_CLIENT.requestSpocAds).toHaveBeenCalledWith(
        [expect.anything()],
        REQUEST_OPTIONS
      );
    });

    it("should not read the spocs cache when adsClient is set", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "get").mockResolvedValue({
        spocs: { lastUpdated: Date.now(), spocs: {} },
      });
      jest.spyOn(feed.cache, "set").mockResolvedValue();

      feed.store = createStore(combineReducers(reducers), {
        Prefs: {
          values: {
            "unifiedAds.blockedAds": "",
            "unifiedAds.spocs.enabled": true,
            "discoverystream.placements.spocs": "newtab_stories_1",
            "discoverystream.placements.spocs.counts": "1",
          },
        },
      });

      const ADS_CLIENT = {
        requestSpocAds: jest
          .fn()
          .mockResolvedValue(
            new Map([["newtab_stories_1", [{ blockKey: "b1" }]]])
          ),
      };
      globalThis.AdsClient = {
        isEnabled: jest.fn().mockReturnValue(true),
        getClient: jest.fn().mockReturnValue(ADS_CLIENT),
        requestOptions: jest.fn().mockReturnValue({}),
      };

      await feed.onAction({ type: at.INIT });

      feed.cache.get.mockClear();
      await feed.loadSpocs(feed.store.dispatch);

      // A later, unrelated read happens while processing the results, so it is
      // the ordering that shows the guarded read was skipped.
      expect(ADS_CLIENT.requestSpocAds).toHaveBeenCalledTimes(1);
      expect(
        ADS_CLIENT.requestSpocAds.mock.invocationCallOrder[0]
      ).toBeLessThan(feed.cache.get.mock.invocationCallOrder[0]);
      expect(feed.cache.set.mock.calls.some(([key]) => key === "spocs")).toBe(
        false
      );

      // The same entry does suppress the fetch on the direct MARS path, which
      // is what made it suppress the ads-client one too.
      feed.adsClient = null;
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({});
      await feed.loadSpocs(feed.store.dispatch);

      expect(feed.fetchFromEndpoint).not.toHaveBeenCalled();
    });

    it("should seed placements the ads client omitted with empty arrays", async () => {
      feed.adsClient = {
        requestSpocAds: jest
          .fn()
          .mockResolvedValue(new Map([["newtab_stories_1", []]])),
      };
      globalThis.AdsClient = {
        isEnabled: jest.fn().mockReturnValue(true),
        getClient: jest.fn().mockReturnValue(feed.adsClient),
        requestOptions: jest.fn().mockReturnValue({}),
      };

      const result = await feed._fetchSpocsWithAdsClient([
        { placement: "newtab_stories_1", count: 1 },
        { placement: "newtab_stories_2", count: 1 },
      ]);

      // loadSpocs concats by placement, so a missing key folds an undefined
      // into the spocs list.
      expect(result.newtab_stories_1).toEqual([]);
      expect(result.newtab_stories_2).toEqual([]);
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
      expect(result).toEqual(spocs);
    });
    it("should return normalized data if new data passed in without title or context", async () => {
      const spocs = {
        items: [{ id: "id" }],
      };
      const result = feed.normalizeSpocsItems(spocs);
      expect(result).toEqual({
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
      expect(result).toEqual({
        title: "",
        context: "",
        sponsor: "",
        sponsored_by_override: undefined,
        items: [{ id: "id" }],
      });
    });
  });

  describe("#showSponsoredStories", () => {
    it("should return false from showSponsoredStories if user pref showSponsored is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: false, "system.showSponsored": true },
        },
      });

      expect(feed.showSponsoredStories).toBe(false);
    });
    it("should return false from showSponsoredStories if DiscoveryStream pref system.showSponsored is false", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: true, "system.showSponsored": false },
        },
      });

      expect(feed.showSponsoredStories).toBe(false);
    });
    it("should return true from showSponsoredStories if both prefs are true", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: { showSponsored: true, "system.showSponsored": true },
        },
      });

      expect(feed.showSponsoredStories).toBe(true);
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
      expect(feed.showStories).toBe(false);
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
      expect(feed.showStories).toBe(false);
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
      expect(feed.showStories).toBe(true);
    });
  });

  describe("#clearSpocs", () => {
    let defaultState;
    let DiscoveryStream;
    let Prefs;
    beforeEach(() => {
      DiscoveryStream = {
        layout: [],
      };
      Prefs = {
        values: {
          "feeds.section.topstories": true,
          "feeds.system.topstories": true,
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

    const setUnifiedAdsState = values => {
      jest.spyOn(feed.store, "getState").mockReturnValue({
        Prefs: {
          values: {
            "unifiedAds.spocs.enabled": true,
            "unifiedAds.adsFeed.enabled": false,
            "unifiedAds.endpoint": "https://ads.example/",
            "unifiedAds.ohttp.enabled": true,
            // Bug 2068990: this pref is gone, but keep it in the fixture so
            // these tests fail if a legacy Pocket fall-through comes back.
            "discoverystream.endpointSpocsClear":
              "https://spocs.getpocket.com/user",
            ...values,
          },
        },
      });
    };

    it("should not send anything when unified ads spocs are disabled", async () => {
      setUnifiedAdsState({ "unifiedAds.spocs.enabled": false });
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue(null);

      await feed.clearSpocs();

      expect(feed.fetchFromEndpoint).not.toHaveBeenCalled();
    });
    it("should not send anything with no MARS endpoint", async () => {
      setUnifiedAdsState({ "unifiedAds.endpoint": "" });
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue(null);

      await feed.clearSpocs();

      expect(feed.fetchFromEndpoint).not.toHaveBeenCalled();
    });
    it("should not send anything when AdsFeed handles the DELETE", async () => {
      setUnifiedAdsState({ "unifiedAds.adsFeed.enabled": true });
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue(null);

      await feed.clearSpocs();

      expect(feed.fetchFromEndpoint).not.toHaveBeenCalled();
    });

    it("should properly call clearSpocs when sponsored content is changed", async () => {
      jest.spyOn(feed, "clearSpocs").mockReturnValue(Promise.resolve());
      jest.spyOn(feed, "loadSpocs").mockImplementation(() => {});

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      expect(feed.clearSpocs).not.toHaveBeenCalled();

      Prefs.values.showSponsored = false;

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      expect(feed.clearSpocs).toHaveBeenCalledTimes(1);
    });
    it("should call clearSpocs when top stories are turned off", async () => {
      jest.spyOn(feed, "clearSpocs").mockReturnValue(Promise.resolve());
      Prefs.values["feeds.section.topstories"] = false;

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "feeds.section.topstories" },
      });

      expect(feed.clearSpocs).toHaveBeenCalledTimes(1);
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
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      feed.cache.get.mockResolvedValue(cache);

      const result = await feed.rotate(feedResponse.recommendations);

      expect(result[3].id).toBe("first");
    });
  });

  describe("#reset", () => {
    it("should fire all reset based functions", async () => {
      jest
        .spyOn(globalThis.Services.obs, "removeObserver")
        .mockImplementation(() => {});

      jest.spyOn(feed, "resetDataPrefs").mockImplementation(() => {});
      jest.spyOn(feed, "resetCache").mockReturnValue(Promise.resolve());
      jest.spyOn(feed, "resetState").mockImplementation(() => {});

      feed.loaded = true;

      await feed.reset();

      expect(feed.resetDataPrefs).toHaveBeenCalledTimes(1);
      expect(feed.resetCache).toHaveBeenCalledTimes(1);
      expect(feed.resetState).toHaveBeenCalledTimes(1);
    });
  });

  describe("#resetCache", () => {
    it("should set .feeds and .spocs and to {}", async () => {
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      await feed.resetCache();

      expect(feed.cache.set).toHaveBeenCalledTimes(3);
      const [firstCall, secondCall, thirdCall] = feed.cache.set.mock.calls;
      expect(firstCall).toEqual(["feeds", {}]);
      expect(secondCall).toEqual(["spocs", {}]);
      expect(thirdCall).toEqual(["recsImpressions", {}]);
    });
  });

  describe("#filterBlocked", () => {
    it("should return initial data from filterBlocked if spocs are empty", async () => {
      const { data: result } = await feed.filterBlocked([]);

      expect(result.length).toBe(0);
    });
    it("should return initial data if links are not blocked", async () => {
      const { data: result } = await feed.filterBlocked([
        { url: "https://foo.com" },
        { url: "test.com" },
      ]);
      expect(result.length).toBe(2);
    });
    it("should return filtered data if links are blocked", async () => {
      const fakeBlocks = {
        flight_id_3: 1,
      };
      jest.spyOn(feed, "readDataPref").mockReturnValue(fakeBlocks);
      jest
        .spyOn(fakeNewTabUtils.blockedLinks, "isBlocked")
        .mockImplementation(({ url }) => url === "https://blocked_url.com");
      const cache = {
        recsBlocks: {
          id_4: 1,
        },
      };
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed.cache, "set").mockImplementation(() => {});
      feed.cache.get.mockResolvedValue(cache);
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
      expect(result.length).toBe(1);
      expect(result[0].url).toBe("https://not_blocked.com");
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

      expect(result.lastUpdated).toBe(4);
      expect(result.data.recommendations).toHaveLength(1);
      expect(result.data.recommendations[0].url).toBe("test.com");
      expect(result.data.recommendations).not.toContain(
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
      jest.spyOn(feed, "readDataPref").mockReturnValue(fakeImpressions);

      const { data: result, filtered } = feed.frequencyCapSpocs(fakeSpocs);

      expect(result.length).toBe(1);
      expect(result[0].flight_id).toBe("not-seen");
      expect(filtered).toEqual([fakeSpocs[0]]);
    });
    it("should return simple structure and do nothing with no spocs", () => {
      const { data: result, filtered } = feed.frequencyCapSpocs([]);

      expect(result.length).toBe(0);
      expect(filtered.length).toBe(0);
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

      expect(result[0]).toEqual({
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

      expect(result[0]).toEqual({ id: 1 });
    });
    it("should return simple structure and do nothing with no spocs", () => {
      const { data: result } = feed.migrateFlightId([]);

      expect(result.length).toBe(0);
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

      expect(result).toBe(true);
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

      expect(result).toBe(true);
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

      expect(result).toBe(false);
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

      expect(result).toBe(false);
    });
  });

  describe("#retryFeed", () => {
    it("should retry a feed fetch", async () => {
      jest.spyOn(feed, "getComponentFeed").mockReturnValue(Promise.resolve({}));
      jest.spyOn(feed.store, "dispatch");

      await feed.retryFeed({ url: "https://feed.com" });

      expect(feed.getComponentFeed).toHaveBeenCalledTimes(1);
      expect(feed.store.dispatch).toHaveBeenCalledTimes(1);
      expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
        "DISCOVERY_STREAM_FEED_UPDATE"
      );
      expect(feed.store.dispatch.mock.calls[0][0].data).toEqual({
        feed: {},
        url: "https://feed.com",
      });
    });
  });

  describe("#recordFlightImpression", () => {
    it("should return false if time based cap is hit", () => {
      jest.spyOn(feed, "readDataPref").mockReturnValue({});
      jest.spyOn(feed, "writeDataPref").mockImplementation(() => {});

      feed.recordFlightImpression("seen");

      expect(feed.writeDataPref).toHaveBeenCalledWith(
        SPOC_IMPRESSION_TRACKING_PREF,
        {
          seen: [0],
        }
      );
    });
  });

  describe("#recordBlockFlightId", () => {
    it("should call writeDataPref with new flight id added", () => {
      jest.spyOn(feed, "readDataPref").mockReturnValue({ 1234: 1 });
      jest.spyOn(feed, "writeDataPref").mockImplementation(() => {});

      feed.recordBlockFlightId("5678");

      expect(feed.readDataPref).toHaveBeenCalledTimes(1);
      expect(feed.writeDataPref).toHaveBeenCalledWith(
        "discoverystream.flight.blocks",
        {
          1234: 1,
          5678: 1,
        }
      );
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
      jest.spyOn(feed, "getPlacements").mockReturnValue([{ name: "spocs" }]);
      jest.spyOn(feed, "readDataPref").mockReturnValue(fakeImpressions);
      jest.spyOn(feed, "writeDataPref").mockImplementation(() => {});

      feed.cleanUpFlightImpressionPref(fakeSpocs);

      expect(feed.writeDataPref).toHaveBeenCalledWith(
        SPOC_IMPRESSION_TRACKING_PREF,
        {
          "flight-2": [-1],
        }
      );
    });
  });

  describe("#recordTopRecImpression", () => {
    it("should add a rec id to the rec impression pref", async () => {
      const cache = {
        recsImpressions: {},
      };
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed.cache, "set").mockImplementation(() => {});
      feed.cache.get.mockResolvedValue(cache);

      await feed.recordTopRecImpression("rec");

      expect(feed.cache.set).toHaveBeenCalledWith("recsImpressions", {
        rec: 0,
      });
    });
    it("should not add an impression if it already exists", async () => {
      const cache = {
        recsImpressions: { rec: 4 },
      };
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed.cache, "set").mockImplementation(() => {});
      feed.cache.get.mockResolvedValue(cache);

      await feed.recordTopRecImpression("rec");

      expect(feed.cache.set).not.toHaveBeenCalled();
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
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve());
      jest.spyOn(feed.cache, "set").mockImplementation(() => {});
      feed.cache.get.mockResolvedValue(cache);

      await feed.cleanUpTopRecImpressions();

      expect(feed.cache.set).toHaveBeenCalledWith("recsImpressions", {
        rec2: 0,
        rec3: 0,
      });
    });
  });

  describe("#writeDataPref", () => {
    it("should call Services.prefs.setStringPref", () => {
      jest.spyOn(feed.store, "dispatch");
      const fakeImpressions = {
        foo: [Date.now() - 1],
        bar: [Date.now() - 1],
      };

      feed.writeDataPref(SPOC_IMPRESSION_TRACKING_PREF, fakeImpressions);

      expect(feed.store.dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          data: {
            name: SPOC_IMPRESSION_TRACKING_PREF,
            value: JSON.stringify(fakeImpressions),
          },
          type: at.SET_PREF,
        })
      );
    });
  });

  describe("#addEndpointQuery", () => {
    const url = "https://spocs.getpocket.com/spocs";

    it("should return same url with no query", () => {
      const result = feed.addEndpointQuery(url, "");
      expect(result).toBe(url);
    });

    it("should add multiple query params to standard url", () => {
      const params = "?first=first&second=second";
      const result = feed.addEndpointQuery(url, params);
      expect(result).toBe(url + params);
    });

    it("should add multiple query params to url with a query already", () => {
      const params = "first=first&second=second";
      const initialParams = "?zero=zero";
      const result = feed.addEndpointQuery(
        `${url}${initialParams}`,
        `?${params}`
      );
      expect(result).toBe(`${url}${initialParams}&${params}`);
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

      expect(result).toEqual(fakeImpressions);
    });
  });

  describe("#setupPrefs", () => {
    it("should call setupPrefs", async () => {
      jest.spyOn(feed, "setupPrefs");
      feed.onAction({
        type: at.INIT,
      });
      expect(feed.setupPrefs).toHaveBeenCalledTimes(1);
    });
    it("should dispatch to at.DISCOVERY_STREAM_PREFS_SETUP with proper data", async () => {
      jest.spyOn(feed.store, "dispatch");
      jest
        .spyOn(globalThis.NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
        .mockReturnValue({
          slug: "experimentId",
          branch: "branchId",
          isRollout: false,
        });
      feed.store.getState = () => ({
        Prefs: {
          values: {
            region: "CA",
            pocketConfig: {
              hideDescriptions: false,
              hideDescriptionsRegions: "US,CA,GB",
              compactImages: true,
              imageGradient: true,
              newSponsoredLabel: true,
              titleLines: "1",
              descLines: "1",
              readTime: true,
            },
          },
        },
      });
      feed.setupPrefs();
      expect(feed.store.dispatch.mock.calls[0][0].data).toEqual({
        utmSource: "pocket-newtab",
        utmCampaign: "experimentId",
        utmContent: "branchId",
      });
      expect(feed.store.dispatch.mock.calls[1][0].data).toEqual({
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
      jest.spyOn(feed, "recordTopRecImpression").mockImplementation(() => {});
      await feed.onAction({
        type: at.DISCOVERY_STREAM_IMPRESSION_STATS,
        data: { tiles: [{ id: "seen" }] },
      });

      expect(feed.recordTopRecImpression).toHaveBeenCalledWith("seen");
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
      jest.spyOn(feed.store, "getState").mockReturnValue({
        DiscoveryStream: {
          spocs: {
            data,
          },
        },
        Prefs: {
          values: {
            trainhopConfig: {},
          },
        },
      });
    });

    it("should call dispatch to ac.AlsoToPreloaded with filtered spoc data", async () => {
      jest.spyOn(feed, "getPlacements").mockReturnValue([{ name: "spocs" }]);
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
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
      jest.spyOn(feed, "recordFlightImpression").mockImplementation(() => {});
      jest.spyOn(feed, "readDataPref").mockReturnValue(fakeImpressions);
      jest.spyOn(feed.store, "dispatch");

      await feed.onAction({
        type: at.DISCOVERY_STREAM_SPOC_IMPRESSION,
        data: { flightId: "seen" },
      });

      expect(feed.store.dispatch.mock.calls[1][0].data.spocs).toEqual(result);
    });
    it("should not call dispatch to ac.AlsoToPreloaded if spocs were not changed by frequency capping", async () => {
      jest.spyOn(feed, "getPlacements").mockReturnValue([{ name: "spocs" }]);
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
      const fakeImpressions = {};
      jest.spyOn(feed, "recordFlightImpression").mockImplementation(() => {});
      jest.spyOn(feed, "readDataPref").mockReturnValue(fakeImpressions);
      jest.spyOn(feed.store, "dispatch");

      await feed.onAction({
        type: at.DISCOVERY_STREAM_SPOC_IMPRESSION,
        data: { flight_id: "seen" },
      });

      expect(feed.store.dispatch).not.toHaveBeenCalled();
    });
    it("should attempt feq cap on valid spocs with placements on impression", async () => {
      jest.restoreAllMocks();
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
      const fakeImpressions = {};
      jest.spyOn(feed, "recordFlightImpression").mockImplementation(() => {});
      jest.spyOn(feed, "readDataPref").mockReturnValue(fakeImpressions);
      jest.spyOn(feed.store, "dispatch");
      jest.spyOn(feed, "frequencyCapSpocs");

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
      jest.spyOn(feed.store, "getState").mockReturnValue({
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

      expect(feed.frequencyCapSpocs).toHaveBeenCalledTimes(1);
      expect(feed.frequencyCapSpocs).toHaveBeenCalledWith(data.spocs.items);
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
      jest.spyOn(feed.store, "getState").mockReturnValue({
        DiscoveryStream: {
          spocs: spocsData,
          feeds: feedsData,
        },
      });
    });
    it("should call dispatch if found a blocked spoc", async () => {
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
      Object.defineProperty(feed, "spocsOnDemand", { get: () => false });
      Object.defineProperty(feed, "spocsCacheUpdateTime", {
        get: () => 30 * 60 * 1000,
      });

      jest.spyOn(feed.store, "dispatch");

      await feed.onAction({
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "foo.com" },
      });

      expect(feed.store.dispatch.mock.calls[0][0].data.url).toEqual("foo.com");
    });
    it("should dispatch once if the blocked is not a SPOC", async () => {
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
      jest.spyOn(feed.store, "dispatch");

      await feed.onAction({
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "not_a_spoc.com" },
      });

      expect(feed.store.dispatch).toHaveBeenCalledTimes(1);
      expect(feed.store.dispatch.mock.calls[0][0].data.url).toEqual(
        "not_a_spoc.com"
      );
    });
    it("should dispatch a DISCOVERY_STREAM_SPOC_BLOCKED for a blocked spoc", async () => {
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
      Object.defineProperty(feed, "spocsOnDemand", { get: () => false });
      Object.defineProperty(feed, "spocsCacheUpdateTime", {
        get: () => 30 * 60 * 1000,
      });
      jest.spyOn(feed.store, "dispatch");

      await feed.onAction({
        type: at.PLACES_LINK_BLOCKED,
        data: { url: "foo.com" },
      });

      expect(feed.store.dispatch.mock.calls[1][0].type).toBe(
        "DISCOVERY_STREAM_SPOC_BLOCKED"
      );
    });
  });

  describe("#onAction: BLOCK_URL", () => {
    it("should call recordBlockFlightId whith BLOCK_URL", async () => {
      jest.spyOn(feed, "recordBlockFlightId").mockImplementation(() => {});

      await feed.onAction({
        type: at.BLOCK_URL,
        data: [
          {
            flight_id: "1234",
          },
        ],
      });

      expect(feed.recordBlockFlightId).toHaveBeenCalledWith("1234");
    });
  });

  describe("#onAction: INIT", () => {
    it("should be .loaded=false before initialization", () => {
      expect(feed.loaded).toBe(false);
    });
    it("should load data and set .loaded=true if config.enabled is true", async () => {
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      setPref(CONFIG_PREF_NAME, { enabled: true });
      jest.spyOn(feed, "loadLayout").mockReturnValue(Promise.resolve());

      await feed.onAction({ type: at.INIT });

      expect(feed.loadLayout).toHaveBeenCalledTimes(1);
      expect(feed.loaded).toBe(true);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_SET_VALUE", () => {
    it("should add the new value to the pref without changing the existing values", async () => {
      jest.spyOn(feed.store, "dispatch");
      setPref(CONFIG_PREF_NAME, { enabled: true, other: "value" });

      await feed.onAction({
        type: at.DISCOVERY_STREAM_CONFIG_SET_VALUE,
        data: { name: "api_key_pref", value: "foo" },
      });

      expect(feed.store.dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          data: {
            name: CONFIG_PREF_NAME,
            value: JSON.stringify({
              enabled: true,
              other: "value",
              api_key_pref: "foo",
            }),
          },
          type: at.SET_PREF,
        })
      );
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_RESET", () => {
    it("should call configReset", async () => {
      jest.spyOn(feed, "configReset");
      feed.onAction({
        type: at.DISCOVERY_STREAM_CONFIG_RESET,
      });
      expect(feed.configReset).toHaveBeenCalledTimes(1);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS", () => {
    it("Should dispatch CLEAR_PREF with pref name", async () => {
      jest.spyOn(feed.store, "dispatch");
      await feed.onAction({
        type: at.DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS,
      });

      expect(feed.store.dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          data: expect.objectContaining({
            name: CONFIG_PREF_NAME,
          }),
          type: at.CLEAR_PREF,
        })
      );
    });
  });

  describe("#onAction: DISCOVERY_STREAM_RETRY_FEED", () => {
    it("should call retryFeed", async () => {
      expectConsoleError();
      jest.spyOn(feed, "retryFeed");
      feed.onAction({
        type: at.DISCOVERY_STREAM_RETRY_FEED,
        data: { feed: { url: "https://feed.com" } },
      });
      expect(feed.retryFeed).toHaveBeenCalledTimes(1);
      expect(feed.retryFeed).toHaveBeenCalledWith({ url: "https://feed.com" });
    });
  });

  describe("#onAction: DISCOVERY_STREAM_CONFIG_CHANGE", () => {
    it("should call this.loadLayout if config.enabled changes to true ", async () => {
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      // First initialize
      await feed.onAction({ type: at.INIT });
      expect(feed.loaded).toBe(false);

      // force clear cached pref value
      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: true });

      jest.spyOn(feed, "resetCache").mockReturnValue(Promise.resolve());
      jest.spyOn(feed, "loadLayout").mockReturnValue(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      expect(feed.loadLayout).toHaveBeenCalledTimes(1);
      expect(feed.resetCache).toHaveBeenCalledTimes(1);
      expect(feed.loaded).toBe(true);
    });
    it("should clear the cache if a config change happens and config.enabled is true", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      // force clear cached pref value
      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: true });

      jest.spyOn(feed, "resetCache").mockReturnValue(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      expect(feed.resetCache).toHaveBeenCalledTimes(1);
    });
    it("should dispatch DISCOVERY_STREAM_LAYOUT_RESET from DISCOVERY_STREAM_CONFIG_CHANGE", async () => {
      jest.spyOn(feed, "resetDataPrefs").mockImplementation(() => {});
      jest.spyOn(feed, "resetCache").mockResolvedValue();
      jest.spyOn(feed, "enable").mockResolvedValue();
      setPref(CONFIG_PREF_NAME, { enabled: true });
      jest.spyOn(feed.store, "dispatch");

      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      expect(feed.store.dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.DISCOVERY_STREAM_LAYOUT_RESET,
        })
      );
    });
    it("should not call this.loadLayout if config.enabled changes to false", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());
      // force clear cached pref value
      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });
      expect(feed.loaded).toBe(true);

      feed._prefCache = {};
      setPref(CONFIG_PREF_NAME, { enabled: false });
      jest.spyOn(feed, "resetCache").mockReturnValue(Promise.resolve());
      jest.spyOn(feed, "loadLayout").mockReturnValue(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_CONFIG_CHANGE });

      expect(feed.loadLayout).not.toHaveBeenCalled();
      expect(feed.resetCache).toHaveBeenCalledTimes(1);
      expect(feed.loaded).toBe(false);
    });
  });

  describe("#onAction: UNINIT", () => {
    it("should reset pref cache", async () => {
      feed._prefCache = { cached: "value" };

      await feed.onAction({ type: at.UNINIT });

      expect(feed._prefCache).toEqual({});
    });
  });

  describe("#onAction: PREF_CHANGED", () => {
    it("should update state.DiscoveryStream.config when the pref changes", async () => {
      setPref(CONFIG_PREF_NAME, {
        enabled: true,
        api_key_pref: "foo",
      });

      expect(feed.store.getState().DiscoveryStream.config).toEqual({
        enabled: true,
        api_key_pref: "foo",
      });
    });
    it("should fire loadSpocs is showSponsored pref changes", async () => {
      jest.spyOn(feed, "loadSpocs").mockReturnValue(Promise.resolve());

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      expect(feed.loadSpocs).toHaveBeenCalledTimes(1);
    });
    it("should fire onPrefChange when pocketConfig pref changes", async () => {
      jest.spyOn(feed, "onPrefChange").mockReturnValue(Promise.resolve());

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "pocketConfig", value: false },
      });

      expect(feed.onPrefChange).toHaveBeenCalledTimes(1);
    });
    it("should re enable stories when top stories is turned on", async () => {
      jest.spyOn(feed, "refreshAll").mockReturnValue(Promise.resolve());
      feed.loaded = true;
      setPref(CONFIG_PREF_NAME, {
        enabled: true,
      });

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "feeds.section.topstories", value: true },
      });

      expect(feed.refreshAll).toHaveBeenCalledTimes(1);
    });
    it("shoud update allowlist", async () => {
      expect(feed.store.getState().Prefs.values[ENDPOINTS_PREF_NAME]).toBe(
        DUMMY_ENDPOINT
      );
      setPref(ENDPOINTS_PREF_NAME, "sick-kickflip.mozilla.net");
      expect(feed.store.getState().Prefs.values[ENDPOINTS_PREF_NAME]).toBe(
        "sick-kickflip.mozilla.net"
      );
    });
  });

  describe("#onAction: SYSTEM_TICK", () => {
    it("should not refresh if DiscoveryStream has not been loaded", async () => {
      jest.spyOn(feed, "refreshAll").mockResolvedValue();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.SYSTEM_TICK });
      expect(feed.refreshAll).not.toHaveBeenCalled();
    });

    it("should not refresh if no caches are expired", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "set").mockResolvedValue();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      jest.spyOn(feed, "onSystemTick").mockResolvedValue();
      jest.spyOn(feed, "refreshAll").mockResolvedValue();

      await feed.onAction({ type: at.SYSTEM_TICK });
      expect(feed.refreshAll).not.toHaveBeenCalled();
    });

    it("should refresh if DiscoveryStream has been loaded at least once and a cache has expired", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "set").mockResolvedValue();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      jest.spyOn(feed, "refreshAll").mockResolvedValue();

      await feed.onAction({ type: at.SYSTEM_TICK });
      expect(feed.refreshAll).toHaveBeenCalledTimes(1);
    });

    it("should refresh and not update open tabs if DiscoveryStream has been loaded at least once", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "set").mockResolvedValue();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      jest.spyOn(feed, "refreshAll").mockResolvedValue();

      await feed.onAction({ type: at.SYSTEM_TICK });
      expect(feed.refreshAll).toHaveBeenCalledWith({
        updateOpenTabs: false,
        isSystemTick: true,
      });
    });
  });

  describe("#enable", () => {
    it("should pass along proper options to refreshAll from enable", async () => {
      jest.spyOn(feed, "refreshAll").mockImplementation(() => {});
      await feed.enable();
      expect(feed.refreshAll).toHaveBeenCalledWith({});
      await feed.enable({ updateOpenTabs: true });
      expect(feed.refreshAll).toHaveBeenCalledWith({ updateOpenTabs: true });
      await feed.enable({ isStartup: true });
      expect(feed.refreshAll).toHaveBeenCalledWith({ isStartup: true });
      await feed.enable({ updateOpenTabs: true, isStartup: true });
      expect(feed.refreshAll).toHaveBeenCalledWith({
        updateOpenTabs: true,
        isStartup: true,
      });
    });
  });

  describe("#onPrefChange", () => {
    it("should call loadLayout when Pocket config changes", async () => {
      jest.spyOn(feed, "loadLayout").mockImplementation(() => {});
      feed._prefCache.config = {
        enabled: true,
      };
      await feed.onPrefChange();
      expect(feed.loadLayout).toHaveBeenCalledTimes(1);
    });
    it("should update open tabs but not startup with onPrefChange", async () => {
      jest.spyOn(feed, "refreshAll").mockImplementation(() => {});
      feed._prefCache.config = {
        enabled: true,
      };
      await feed.onPrefChange();
      expect(feed.refreshAll).toHaveBeenCalledWith({ updateOpenTabs: true });
    });
  });

  describe("#onAction: PREF_SHOW_SPONSORED", () => {
    it("should call loadSpocs when preference changes", async () => {
      jest.spyOn(feed, "loadSpocs").mockResolvedValue();
      jest.spyOn(feed.store, "dispatch").mockImplementation(() => {});

      await feed.onAction({
        type: at.PREF_CHANGED,
        data: { name: "showSponsored" },
      });

      expect(feed.loadSpocs).toHaveBeenCalledTimes(1);
      const [dispatchFn] = feed.loadSpocs.mock.lastCall;
      dispatchFn({});
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({})
      );
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_SYNC_RS", () => {
    it("should fire remote settings pollChanges", async () => {
      jest
        .spyOn(globalThis.RemoteSettings, "pollChanges")
        .mockImplementation(() => {});
      await feed.onAction({
        type: at.DISCOVERY_STREAM_DEV_SYNC_RS,
      });
      expect(globalThis.RemoteSettings.pollChanges).toHaveBeenCalledTimes(1);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_REFRESH_CACHE", () => {
    it("should clear the cache with DISCOVERY_STREAM_DEV_REFRESH_CACHE", async () => {
      jest.spyOn(feed.cache, "set").mockReturnValue(Promise.resolve());

      jest.spyOn(feed, "resetCache").mockReturnValue(Promise.resolve());
      await feed.onAction({ type: at.DISCOVERY_STREAM_DEV_REFRESH_CACHE });

      expect(feed.resetCache).toHaveBeenCalledTimes(1);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_SYSTEM_TICK", () => {
    it("should refresh if DiscoveryStream has been loaded at least once and a cache has expired", async () => {
      expectConsoleError();
      jest.spyOn(feed.cache, "set").mockResolvedValue();
      setPref(CONFIG_PREF_NAME, { enabled: true });

      await feed.onAction({ type: at.INIT });

      jest.spyOn(feed, "refreshAll").mockResolvedValue();

      await feed.onAction({ type: at.DISCOVERY_STREAM_DEV_SYSTEM_TICK });
      expect(feed.refreshAll).toHaveBeenCalledTimes(1);
    });
  });

  describe("#onAction: DISCOVERY_STREAM_DEV_EXPIRE_CACHE", () => {
    it("should fire resetCache", async () => {
      jest.spyOn(feed, "resetContentCache").mockImplementation(() => {});
      await feed.onAction({
        type: at.DISCOVERY_STREAM_DEV_EXPIRE_CACHE,
      });
      expect(feed.resetContentCache).toHaveBeenCalledTimes(1);
    });
  });

  describe("#spocsCacheUpdateTime", () => {
    it("should return default cache time", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
    it("should return _spocsCacheUpdateTime", () => {
      const testCacheTime = 123;
      feed._spocsCacheUpdateTime = testCacheTime;
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(testCacheTime);
      expect(cacheTime).toBe(testCacheTime);
    });
    it("should set _spocsCacheUpdateTime with min", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.cacheTimeout": 1,
            showSponsored: true,
            "system.showSponsored": true,
          },
        },
      });
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with max", () => {
      const defaultCacheTime = 30 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.cacheTimeout": 31,
            showSponsored: true,
            "system.showSponsored": true,
          },
        },
      });
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with spocsCacheTimeout", () => {
      const defaultCacheTime = 20 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.cacheTimeout": 20,
            showSponsored: true,
            "system.showSponsored": true,
          },
        },
      });
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with spocsCacheTimeout and onDemand", () => {
      const defaultCacheTime = 4 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.onDemand": true,
            "discoverystream.spocs.cacheTimeout": 4,
            showSponsored: true,
            "system.showSponsored": true,
          },
        },
      });
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with spocsCacheTimeout without max", () => {
      const defaultCacheTime = 31 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.onDemand": true,
            "discoverystream.spocs.cacheTimeout": 31,
            showSponsored: true,
            "system.showSponsored": true,
          },
        },
      });
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
    it("should set _spocsCacheUpdateTime with spocsCacheTimeout without min", () => {
      const defaultCacheTime = 1 * 60 * 1000;
      feed.store.getState = () => ({
        Prefs: {
          values: {
            "discoverystream.spocs.onDemand": true,
            "discoverystream.spocs.cacheTimeout": 1,
            showSponsored: true,
            "system.showSponsored": true,
          },
        },
      });
      const cacheTime = feed.spocsCacheUpdateTime;
      expect(feed._spocsCacheUpdateTime).toBe(defaultCacheTime);
      expect(cacheTime).toBe(defaultCacheTime);
    });
  });

  describe("#isExpired", () => {
    it("should throw if the key is not valid", () => {
      expect(() => {
        feed.isExpired({}, "foo");
      }).toThrow();
    });
    it("should return false for spocs on startup for content under 1 week", () => {
      const spocs = { lastUpdated: Date.now() };
      const result = feed.isExpired({
        cachedData: { spocs },
        key: "spocs",
        isStartup: true,
      });

      expect(result).toBe(false);
    });
    it("should return true for spocs for isStartup=false after 30 mins", () => {
      const spocs = { lastUpdated: Date.now() };
      jest.advanceTimersByTime(THIRTY_MINUTES + 1);
      const result = feed.isExpired({ cachedData: { spocs }, key: "spocs" });

      expect(result).toBe(true);
    });
    it("should return true for spocs on startup for content over 1 week", () => {
      const spocs = { lastUpdated: Date.now() };
      jest.advanceTimersByTime(ONE_WEEK + 1);
      const result = feed.isExpired({
        cachedData: { spocs },
        key: "spocs",
        isStartup: true,
      });

      expect(result).toBe(true);
    });
  });

  describe("#_checkExpirationPerComponent", () => {
    let cache;
    beforeEach(() => {
      cache = {
        feeds: { "foo.com": { lastUpdated: Date.now() } },
        spocs: { lastUpdated: Date.now() },
      };
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
      jest.spyOn(feed.cache, "get").mockResolvedValue(cache);
    });

    it("should return false if nothing in the cache is expired", async () => {
      const results = await feed._checkExpirationPerComponent();
      expect(results.spocs).toBe(false);
      expect(results.feeds).toBe(false);
    });
    it("should return true if .spocs is missing", async () => {
      delete cache.spocs;

      const results = await feed._checkExpirationPerComponent();
      expect(results.spocs).toBe(true);
      expect(results.feeds).toBe(false);
    });
    it("should return true if .feeds is missing", async () => {
      delete cache.feeds;

      const results = await feed._checkExpirationPerComponent();
      expect(results.spocs).toBe(false);
      expect(results.feeds).toBe(true);
    });
    it("should return true if spocs are expired", async () => {
      jest.advanceTimersByTime(THIRTY_MINUTES + 1);
      // Update other caches we aren't testing
      cache.feeds["foo.com"].lastUpdated = Date.now();

      const results = await feed._checkExpirationPerComponent();
      expect(results.spocs).toBe(true);
      expect(results.feeds).toBe(false);
    });
    it("should return true if data for .feeds[url] is missing", async () => {
      cache.feeds["foo.com"] = null;

      const results = await feed._checkExpirationPerComponent();
      expect(results.spocs).toBe(false);
      expect(results.feeds).toBe(true);
    });
    it("should return true if data for .feeds[url] is expired", async () => {
      jest.advanceTimersByTime(THIRTY_MINUTES + 1);
      // Update other caches we aren't testing
      cache.spocs.lastUpdated = Date.now();

      const results = await feed._checkExpirationPerComponent();
      expect(results.spocs).toBe(false);
      expect(results.feeds).toBe(true);
    });
  });

  describe("#refreshAll", () => {
    beforeEach(() => {
      jest.spyOn(feed, "loadLayout").mockResolvedValue();
      jest.spyOn(feed, "loadComponentFeeds").mockResolvedValue();
      jest.spyOn(feed, "loadSpocs").mockResolvedValue();
      jest.spyOn(feed.store, "dispatch");
      Object.defineProperty(feed, "showSponsoredStories", { get: () => true });
    });

    it("should call layout, component, spocs update and telemetry reporting functions", async () => {
      await feed.refreshAll();

      expect(feed.loadLayout).toHaveBeenCalledTimes(1);
      expect(feed.loadComponentFeeds).toHaveBeenCalledTimes(1);
      expect(feed.loadSpocs).toHaveBeenCalledTimes(1);
    });
    it("should pass in dispatch wrapped with broadcast if options.updateOpenTabs is true", async () => {
      await feed.refreshAll({ updateOpenTabs: true });
      [feed.loadLayout, feed.loadComponentFeeds, feed.loadSpocs].forEach(fn => {
        expect(fn).toHaveBeenCalledTimes(1);
        const result = fn.mock.calls[0][0]({ type: "FOO" });
        expect(au.isBroadcastToContent(result)).toBe(true);
      });
    });
    it("should pass in dispatch with regular actions if options.updateOpenTabs is false", async () => {
      await feed.refreshAll({ updateOpenTabs: false });
      [feed.loadLayout, feed.loadComponentFeeds, feed.loadSpocs].forEach(fn => {
        expect(fn).toHaveBeenCalledTimes(1);
        const result = fn.mock.calls[0][0]({ type: "FOO" });
        expect(result).toEqual({ type: "FOO" });
      });
    });
    it("should set loaded to true if loadSpocs and loadComponentFeeds fails", async () => {
      expectConsoleError();
      feed.loadComponentFeeds.mockRejectedValue("loadComponentFeeds error");
      feed.loadSpocs.mockRejectedValue("loadSpocs error");

      await feed.enable();

      expect(feed.loaded).toBe(true);
    });
    it("should call loadComponentFeeds and loadSpocs in Promise.all", async () => {
      jest.spyOn(Promise, "all").mockResolvedValue();

      await feed.refreshAll();

      expect(Promise.all).toHaveBeenCalledTimes(1);
      const [args] = Promise.all.mock.lastCall;
      expect(args.length).toBe(2);
    });
    describe("test startup cache behaviour", () => {
      beforeEach(() => {
        feed._maybeUpdateCachedData.mockRestore();
        jest.spyOn(feed.cache, "set").mockResolvedValue();
      });
      it("should not refresh layout on startup if it is under THIRTY_MINUTES", async () => {
        feed.loadLayout.mockRestore();
        jest.spyOn(feed.cache, "get").mockResolvedValue({
          layout: { lastUpdated: Date.now(), layout: {} },
        });
        jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({ layout: {} });

        await feed.refreshAll({ isStartup: true });

        expect(feed.fetchFromEndpoint).not.toHaveBeenCalled();
      });
      it("should refresh spocs on startup if it was served from cache", async () => {
        expectConsoleError();
        feed.loadSpocs.mockRestore();
        jest.spyOn(feed, "getPlacements").mockReturnValue([{ name: "spocs" }]);
        jest.spyOn(feed.cache, "get").mockResolvedValue({
          spocs: { lastUpdated: Date.now() },
        });
        jest.advanceTimersByTime(THIRTY_MINUTES + 1);

        await feed.refreshAll({ isStartup: true });

        // Once from cache, once to update the store
        expect(feed.store.dispatch).toHaveBeenCalledTimes(2);
        expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
          at.DISCOVERY_STREAM_SPOCS_UPDATE
        );
      });
      it("should not refresh spocs on startup if it is under THIRTY_MINUTES", async () => {
        feed.loadSpocs.mockRestore();
        jest.spyOn(feed.cache, "get").mockResolvedValue({
          spocs: { lastUpdated: Date.now() },
        });
        jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue("data");

        await feed.refreshAll({ isStartup: true });

        expect(feed.fetchFromEndpoint).not.toHaveBeenCalled();
      });
      it("should refresh feeds on startup if it was served from cache", async () => {
        feed.loadComponentFeeds.mockRestore();

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
        jest.spyOn(feed.store, "getState").mockReturnValue(fakeDiscoveryStream);
        jest.spyOn(feed, "rotate").mockImplementation(val => val);
        jest
          .spyOn(feed, "filterBlocked")
          .mockImplementation(val => ({ data: val }));

        const fakeCache = {
          feeds: { "foo.com": { lastUpdated: Date.now(), data: ["data"] } },
        };
        jest.spyOn(feed.cache, "get").mockResolvedValue(fakeCache);
        jest.advanceTimersByTime(THIRTY_MINUTES + 1);
        stubOutFetchFromEndpointWithRealisticData();

        await feed.refreshAll({ isStartup: true });

        expect(feed.fetchFromEndpoint).toHaveBeenCalledTimes(1);
        // Once from cache, once to update the feed, once to update that all
        // feeds are done, and once to update scores.
        expect(feed.store.dispatch).toHaveBeenCalledTimes(4);
        expect(feed.store.dispatch.mock.calls[1][0].type).toBe(
          at.DISCOVERY_STREAM_FEEDS_UPDATE
        );
      });
    });
  });

  describe("#onAction: TOPIC_SELECTION_MAYBE_LATER", () => {
    it("should call topicSelectionMaybeLaterEvent", async () => {
      jest.spyOn(feed, "topicSelectionMaybeLaterEvent").mockResolvedValue();
      await feed.onAction({
        type: at.TOPIC_SELECTION_MAYBE_LATER,
      });
      expect(feed.topicSelectionMaybeLaterEvent).toHaveBeenCalledTimes(1);
    });
  });

  describe("#topicSelectionMaybeLaterEvent", () => {
    it("should use 3-day timeout for new profiles (age <= 1 day)", async () => {
      jest.spyOn(feed, "retreiveProfileAge").mockResolvedValue(0.5);
      jest.spyOn(feed.store, "dispatch");
      await feed.topicSelectionMaybeLaterEvent();
      const day = 24 * 60 * 60 * 1000;
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.SetPref(
          "discoverystream.topicSelection.onboarding.displayTimeout",
          3 * day
        )
      );
    });

    it("should use 7-day timeout for older profiles (age > 1 day)", async () => {
      jest.spyOn(feed, "retreiveProfileAge").mockResolvedValue(5);
      jest.spyOn(feed.store, "dispatch");
      await feed.topicSelectionMaybeLaterEvent();
      const day = 24 * 60 * 60 * 1000;
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.SetPref(
          "discoverystream.topicSelection.onboarding.displayTimeout",
          7 * day
        )
      );
    });
  });

  describe("new proxy feed", () => {
    beforeEach(() => {
      globalThis.Region.home = "DE";

      services.prefs.getStringPref.mockImplementation(name =>
        name ===
        "browser.newtabpage.activity-stream.discoverystream.merino-provider.endpoint"
          ? "merinoEndpoint"
          : undefined
      );
    });

    it("should update to new feed url", async () => {
      await feed.loadLayout(feed.store.dispatch);
      const { layout } = feed.store.getState().DiscoveryStream;
      expect(layout[0].components[2].feed.url).toBe(
        "https://merinoEndpoint/api/v1/curated-recommendations"
      );
    });

    it("should fetch proper data from getComponentFeed", async () => {
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest.spyOn(feed, "scoreItemsInferred").mockImplementation(val => ({
        data: val,
        filtered: [],
        personalized: false,
      }));
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        recommendedAt: 1755834072383,
        surfaceId: "NEW_TAB_EN_US",
        data: [
          {
            corpusItemId: "decaf-c0ff33",
            scheduledCorpusItemId: "matcha-latte-ff33c1",
            excerpt: "excerpt",
            iconUrl: "iconUrl",
            imageUrl: "imageUrl",
            isTimeSensitive: true,
            publisher: "publisher",
            receivedRank: 0,
            tileId: 12345,
            title: "title",
            topic: "topic",
            url: "url",
            features: {},
          },
        ],
      });

      const feedData = await feed.getComponentFeed("url");
      const expectedData = {
        lastUpdated: 0,
        personalized: false,
        sectionsEnabled: undefined,
        data: {
          settings: {},
          sections: [],
          interestPicker: {},
          recommendations: [
            {
              id: "decaf-c0ff33",
              corpus_item_id: "decaf-c0ff33",
              scheduled_corpus_item_id: "matcha-latte-ff33c1",
              excerpt: "excerpt",
              icon_src: "iconUrl",
              isTimeSensitive: true,
              publisher: "publisher",
              raw_image_src: "imageUrl",
              received_rank: 0,
              recommended_at: 1755834072383,
              title: "title",
              topic: "topic",
              url: "url",
              features: {},
            },
          ],
          surfaceId: "NEW_TAB_EN_US",
          status: "success",
        },
      };

      expect(feedData).toEqual(expectedData);
    });
    it("should fetch proper data from getComponentFeed with sections enabled", async () => {
      setPref("discoverystream.sections.enabled", true);
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest.spyOn(feed, "scoreItemsInferred").mockImplementation(val => ({
        data: val,
        filtered: [],
        personalized: false,
      }));
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        recommendedAt: 1755834072383,
        surfaceId: "NEW_TAB_EN_US",
        data: [
          {
            corpusItemId: "decaf-c0ff33",
            scheduledCorpusItemId: "matcha-latte-ff33c1",
            excerpt: "excerpt",
            iconUrl: "iconUrl",
            imageUrl: "imageUrl",
            isTimeSensitive: true,
            publisher: "publisher",
            receivedRank: 0,
            tileId: 12345,
            title: "title",
            topic: "topic",
            url: "url",
            features: {},
          },
        ],
        feeds: {
          "section-1": {
            title: "Section 1",
            subtitle: "Subtitle 1",
            receivedFeedRank: 1,
            layout: "cards",
            iab: "iab-category",
            isInitiallyVisible: true,
            recommendations: [
              {
                corpusItemId: "decaf-c0ff34",
                scheduledCorpusItemId: "matcha-latte-ff33c2",
                excerpt: "section excerpt",
                iconUrl: "sectionIconUrl",
                imageUrl: "sectionImageUrl",
                isTimeSensitive: false,
                publisher: "section publisher",
                serverScore: 0.9,
                receivedRank: 1,
                title: "section title",
                topic: "section topic",
                url: "section url",
                features: {},
              },
            ],
          },
        },
      });

      const feedData = await feed.getComponentFeed("url");
      const expectedData = {
        lastUpdated: 0,
        personalized: false,
        sectionsEnabled: true,
        data: {
          settings: {},
          sections: [
            {
              sectionKey: "section-1",
              title: "Section 1",
              subtitle: "Subtitle 1",
              receivedRank: 1,
              layout: "cards",
              iab: "iab-category",
              allowAds: true,
              followable: true,
              visible: true,
            },
          ],
          interestPicker: {},
          recommendations: [
            {
              id: "decaf-c0ff33",
              scheduled_corpus_item_id: "matcha-latte-ff33c1",
              corpus_item_id: "decaf-c0ff33",
              features: {},
              excerpt: "excerpt",
              icon_src: "iconUrl",
              isTimeSensitive: true,
              publisher: "publisher",
              raw_image_src: "imageUrl",
              received_rank: 0,
              recommended_at: 1755834072383,
              title: "title",
              topic: "topic",
              url: "url",
            },
            {
              id: "decaf-c0ff34",
              scheduled_corpus_item_id: "matcha-latte-ff33c2",
              corpus_item_id: "decaf-c0ff34",
              url: "section url",
              title: "section title",
              topic: "section topic",
              features: {},
              excerpt: "section excerpt",
              publisher: "section publisher",
              raw_image_src: "sectionImageUrl",
              received_rank: 1,
              server_score: 0.9,
              recommended_at: 1755834072383,
              section: "section-1",
              variant_id: 0,
              icon_src: "sectionIconUrl",
              isTimeSensitive: false,
            },
          ],
          surfaceId: "NEW_TAB_EN_US",
          status: "success",
        },
      };

      // Use JSON comparison because deepEqual will error with incorrect property order message
      expect(JSON.stringify(feedData)).toBe(JSON.stringify(expectedData));
    });
    it("should include allowAds and followable in section objects", async () => {
      setPref("discoverystream.sections.enabled", true);
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest.spyOn(feed, "scoreItemsInferred").mockImplementation(val => ({
        data: val,
        filtered: [],
        personalized: false,
      }));
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        recommendedAt: 0,
        surfaceId: "NEW_TAB_EN_US",
        data: [],
        feeds: {
          "section-1": {
            title: "Section 1",
            subtitle: "",
            receivedFeedRank: 1,
            layout: "cards",
            iab: null,
            isInitiallyVisible: true,
            allowAds: false,
            followable: true,
            recommendations: [],
          },
        },
      });

      const feedData = await feed.getComponentFeed("url");
      const [section] = feedData.data.sections;
      expect(section.allowAds).toBe(false);
      expect(section.followable).toBe(true);
    });
    it("should default allowAds and followable to true when absent", async () => {
      setPref("discoverystream.sections.enabled", true);
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest.spyOn(feed, "scoreItemsInferred").mockImplementation(val => ({
        data: val,
        filtered: [],
        personalized: false,
      }));
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        recommendedAt: 0,
        surfaceId: "NEW_TAB_EN_US",
        data: [],
        feeds: {
          "section-1": {
            title: "Section 1",
            subtitle: "",
            receivedFeedRank: 1,
            layout: "cards",
            iab: null,
            isInitiallyVisible: true,
            recommendations: [],
          },
        },
      });

      const feedData = await feed.getComponentFeed("url");
      const [section] = feedData.data.sections;
      expect(section.allowAds).toBe(true);
      expect(section.followable).toBe(true);
    });
    it("should default allowAds and followable to true when null", async () => {
      setPref("discoverystream.sections.enabled", true);
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest.spyOn(feed, "scoreItemsInferred").mockImplementation(val => ({
        data: val,
        filtered: [],
        personalized: false,
      }));
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        recommendedAt: 0,
        surfaceId: "NEW_TAB_EN_US",
        data: [],
        feeds: {
          "section-1": {
            title: "Section 1",
            subtitle: "",
            receivedFeedRank: 1,
            layout: "cards",
            iab: null,
            isInitiallyVisible: true,
            allowAds: null,
            followable: null,
            recommendations: [],
          },
        },
      });

      const feedData = await feed.getComponentFeed("url");
      const [section] = feedData.data.sections;
      expect(section.allowAds).toBe(true);
      expect(section.followable).toBe(true);
    });
    it("should include followable in interestPicker.sections", async () => {
      setPref("discoverystream.sections.enabled", true);
      const fakeCache = {};
      jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve(fakeCache));
      jest.spyOn(feed, "rotate").mockImplementation(val => val);
      jest.spyOn(feed, "scoreItemsInferred").mockImplementation(val => ({
        data: val,
        filtered: [],
        personalized: false,
      }));
      jest.spyOn(feed, "fetchFromEndpoint").mockResolvedValue({
        recommendedAt: 0,
        surfaceId: "NEW_TAB_EN_US",
        data: [],
        feeds: {
          "section-1": {
            title: "Section 1",
            subtitle: "",
            receivedFeedRank: 1,
            layout: "cards",
            iab: null,
            isInitiallyVisible: true,
            followable: false,
            recommendations: [],
          },
        },
        interestPicker: {
          sections: [{ sectionId: "section-1" }],
        },
      });

      const feedData = await feed.getComponentFeed("url");
      const [pickerSection] = feedData.data.interestPicker.sections;
      expect(pickerSection.sectionId).toBe("section-1");
      expect(pickerSection.title).toBe("Section 1");
      expect(pickerSection.followable).toBe(false);
    });

    describe("section layouts", () => {
      const CONFIGS = {
        "custom-a": { name: "custom-a" },
        "custom-b": { name: "custom-b" },
        "custom-c": { name: "custom-c" },
      };

      const buildFeeds = defs =>
        Object.fromEntries(
          defs.map((def, i) => [
            `section-${i}`,
            {
              title: `Section ${i}`,
              subtitle: `Subtitle ${i}`,
              receivedFeedRank: def.rank,
              layout: def.layout,
              allowAds: def.allowAds,
              iab: "iab-category",
              isInitiallyVisible: true,
              recommendations: [],
            },
          ])
        );

      const setFeeds = (defs, extra = {}) =>
        feed.fetchFromEndpoint.mockResolvedValue({
          recommendedAt: 1755834072383,
          surfaceId: "NEW_TAB_EN_US",
          data: [],
          feeds: buildFeeds(defs),
          ...extra,
        });

      const dispatchLayouts = (configs, orderings) =>
        feed.store.dispatch({
          type: at.SECTIONS_LAYOUT_UPDATE,
          data: { configs, orderings },
        });

      const layoutNames = async () => {
        const feedData = await feed.getComponentFeed("url");
        return feedData.data.sections.map(section => section.layout?.name);
      };

      let restoreLayoutGlobals;

      beforeEach(() => {
        setPref("discoverystream.sections.enabled", true);
        restoreLayoutGlobals = stubGlobals({
          SectionsLayoutManager,
          maskLayoutAds,
        });
        dispatchLayouts(CONFIGS, {});
        jest.spyOn(feed.cache, "get").mockReturnValue(Promise.resolve({}));
        jest.spyOn(feed, "rotate").mockImplementation(val => val);
        jest.spyOn(feed, "fetchFromEndpoint").mockImplementation(() => {});
        setFeeds([
          { rank: 1, layout: { name: "original-layout" } },
          { rank: 2, layout: { name: "another-layout" } },
        ]);
      });

      afterEach(() => {
        restoreLayoutGlobals();
      });

      it("keeps Merino layouts when nothing overrides them", async () => {
        expect(await layoutNames()).toEqual([
          "original-layout",
          "another-layout",
        ]);
      });

      it("forces the default layout when clientLayout.enabled is true", async () => {
        setPref("discoverystream.sections.clientLayout.enabled", true);
        expect(await layoutNames()).toEqual([
          "7-double-row-2-ad",
          "6-small-medium-1-ad",
        ]);
      });

      it("forces the default layout for the whole page when a section is missing its layout", async () => {
        setFeeds([
          { rank: 1, layout: undefined },
          { rank: 2, layout: { name: "another-layout" } },
        ]);
        expect(await layoutNames()).toEqual([
          "7-double-row-2-ad",
          "6-small-medium-1-ad",
        ]);
      });

      it("does not throw when there are no sections", async () => {
        setFeeds([]);
        const feedData = await feed.getComponentFeed("url");
        expect(feedData.data.sections).toEqual([]);
      });

      describe("RS sections ordering", () => {
        const withServerLayout = defs =>
          defs.map(def => ({ layout: { name: "server" }, ...def }));

        const selectOrdering = (key, names) => {
          dispatchLayouts(CONFIGS, { [key]: names });
          setPref("discoverystream.sections.ordering", key);
        };

        it("ignores the ordering when the key pref is unset", async () => {
          dispatchLayouts(CONFIGS, { "my-order": ["custom-a"] });
          expect(await layoutNames()).toEqual([
            "original-layout",
            "another-layout",
          ]);
        });

        it("falls back to Merino when an ordering names an unknown layout", async () => {
          selectOrdering("my-order", ["does-not-exist"]);
          expect(await layoutNames()).toEqual([
            "original-layout",
            "another-layout",
          ]);
        });

        it("falls back to Merino when the ordering key has no matching record", async () => {
          dispatchLayouts(CONFIGS, {});
          setPref("discoverystream.sections.ordering", "not-published-yet");
          expect(await layoutNames()).toEqual([
            "original-layout",
            "another-layout",
          ]);
        });

        it("ignores the Remote Settings ordering when clientLayout.enabled is set", async () => {
          selectOrdering("my-order", ["custom-a", "custom-b"]);
          setPref("discoverystream.sections.clientLayout.enabled", true);
          setFeeds(withServerLayout([{ rank: 0 }, { rank: 1 }]));
          expect(await layoutNames()).toEqual([
            "7-double-row-2-ad",
            "6-small-medium-1-ad",
          ]);
        });

        it("cycles a multi-entry ordering with position 0 pinned", async () => {
          selectOrdering("my-order", ["custom-a", "custom-b", "custom-c"]);
          setFeeds(
            withServerLayout([
              { rank: 0 },
              { rank: 1 },
              { rank: 2 },
              { rank: 4 },
              { rank: 6 },
            ])
          );
          expect(await layoutNames()).toEqual([
            "custom-a",
            "custom-b",
            "custom-c",
            "custom-b",
            "custom-c",
          ]);
        });

        it("pins position 0 and fills the rest from the default cycle for a single-entry ordering", async () => {
          selectOrdering("my-order", ["custom-a"]);
          setFeeds(
            withServerLayout([
              { rank: 0 },
              { rank: 1 },
              { rank: 2 },
              { rank: 4 },
            ])
          );
          expect(await layoutNames()).toEqual([
            "custom-a",
            "6-small-medium-1-ad",
            "4-large-small-medium-1-ad",
            "4-medium-small-1-ad",
          ]);
        });

        it("masks ads based on the section's rank and allowAds", async () => {
          const adLayout = name => ({
            name,
            responsiveLayouts: [
              { columnCount: 1, tiles: [{ position: 0, hasAd: true }] },
            ],
          });
          dispatchLayouts(
            { "ad-a": adLayout("ad-a"), "ad-b": adLayout("ad-b") },
            { "my-order": ["ad-a", "ad-b"] }
          );
          setPref("discoverystream.sections.ordering", "my-order");
          setFeeds([
            { rank: 0, layout: { name: "server" }, allowAds: true },
            { rank: 1, layout: { name: "server" }, allowAds: false },
            { rank: 3, layout: { name: "server" }, allowAds: true },
            { rank: 20, layout: { name: "server" }, allowAds: true },
          ]);
          const feedData = await feed.getComponentFeed("url");
          const hasAd = section =>
            section.layout.responsiveLayouts[0].tiles[0].hasAd;
          const [rank0, rank1, rank3, rank20] = feedData.data.sections;
          expect(hasAd(rank0)).toBe(true); // "ad kept for an allowed rank when allowAds is true";
          expect(hasAd(rank1)).toBe(false); // "ad cleared when allowAds is false";
          expect(hasAd(rank3)).toBe(false); // "ad cleared when the rank is not in the allowed set even though allowAds is true";
          expect(hasAd(rank20)).toBe(false); // "ad cleared when the rank is past the allowed set";
        });

        it("lets a trainhopConfig override replace the default ad-rank set", async () => {
          const adLayout = name => ({
            name,
            responsiveLayouts: [
              { columnCount: 1, tiles: [{ position: 0, hasAd: true }] },
            ],
          });
          dispatchLayouts(
            { "ad-a": adLayout("ad-a"), "ad-b": adLayout("ad-b") },
            { "my-order": ["ad-a", "ad-b"] }
          );
          setPref("discoverystream.sections.ordering", "my-order");
          feed.store.dispatch({
            type: at.PREF_CHANGED,
            data: {
              name: "trainhopConfig",
              value: { sections: { adAllowedRanks: [3] } },
            },
          });
          setFeeds([
            { rank: 0, layout: { name: "server" }, allowAds: true },
            { rank: 3, layout: { name: "server" }, allowAds: true },
          ]);
          const feedData = await feed.getComponentFeed("url");
          const hasAd = section =>
            section.layout.responsiveLayouts[0].tiles[0].hasAd;
          const [rank0, rank3] = feedData.data.sections;
          expect(hasAd(rank0)).toBe(false); // "rank 0 masked — default-allowed but excluded by the override";
          expect(hasAd(rank3)).toBe(true); // "rank 3 kept — default-disallowed but included by the override";
        });
      });

      it("resolves the ad-allowed ranks pref as comma-separated ranks", () => {
        setPref("discoverystream.sections.adAllowedRanks", "0, 2, 4");
        expect([...feed.sectionsAdAllowedRanks]).toEqual([0, 2, 4]);
      });

      it("falls back to the default set when the pref is malformed", () => {
        setPref("discoverystream.sections.adAllowedRanks", "0, x, 2");
        expect([...feed.sectionsAdAllowedRanks]).toEqual([
          ...SectionsLayoutManager.AD_ALLOWED_RANKS,
        ]);
      });
    });
  });

  describe("#getContextualAdsPlacements", () => {
    let prefs;
    let feedsData;
    let expected;

    beforeEach(() => {
      prefs = {
        "discoverystream.placements.contextualSpocs":
          "newtab_stories_1, newtab_stories_2, newtab_stories_3, newtab_stories_4, newtab_stories_5, newtab_stories_6",
        "discoverystream.placements.contextualSpocs.counts": "1, 1, 1, 1, 1, 1",
        "discoverystream.placements.contextualBanners": "",
        "discoverystream.placements.contextualBanners.counts": "",
        "newtabAdSize.leaderboard": false,
        "newtabAdSize.billboard": false,
        "newtabAdSize.leaderboard.position": 3,
        "newtabAdSize.billboard.position": 3,
      };

      feedsData = {
        "https://merino.services.mozilla.com/api/v1/curated-recommendations": {
          data: {
            sections: [
              {
                receivedRank: 0,
                layout: {
                  responsiveLayouts: [{ tiles: [{ hasAd: true }] }],
                },
              },
              {
                iab: { taxonomy: "IAB-3.0", categories: ["386"] },
                receivedRank: 1,
                layout: {
                  responsiveLayouts: [{ tiles: [{ hasAd: true }] }],
                },
              },
              {
                iab: { taxonomy: "IAB-3.0", categories: ["52"] },
                receivedRank: 2,
                layout: {
                  responsiveLayouts: [{ tiles: [{ hasAd: true }] }],
                },
              },
              {
                receivedRank: 3,
                layout: {
                  responsiveLayouts: [{ tiles: [{ hasAd: true }] }],
                },
              },
              {
                iab: { taxonomy: "IAB-3.0", categories: ["464"] },
                receivedRank: 4,
                layout: {
                  responsiveLayouts: [{ tiles: [{ hasAd: true }] }],
                },
              },
              {
                receivedRank: 5,
                layout: {
                  responsiveLayouts: [{ tiles: [{ hasAd: true }] }],
                },
              },
            ],
          },
        },
      };

      expected = [
        {
          placement: "newtab_stories_1",
          count: 1,
        },
        {
          placement: "newtab_stories_2",
          count: 1,
          content: {
            taxonomy: "IAB-3.0",
            categories: ["386"],
          },
        },
        {
          placement: "newtab_stories_3",
          count: 1,
          content: {
            taxonomy: "IAB-3.0",
            categories: ["52"],
          },
        },
        {
          placement: "newtab_stories_4",
          count: 1,
        },
        {
          placement: "newtab_stories_5",
          count: 1,
          content: {
            taxonomy: "IAB-3.0",
            categories: ["464"],
          },
        },
        {
          placement: "newtab_stories_6",
          count: 1,
        },
      ];
    });

    it("should only return SPOC placements", async () => {
      feed.store.getState = () => ({
        Prefs: {
          values: prefs,
        },
        DiscoveryStream: {
          feeds: {
            data: feedsData,
          },
        },
      });

      const placements = feed.getContextualAdsPlacements();

      expect(placements).toEqual(expected);
    });

    it("should return SPOC placements AND banner placements when leaderboard is enabled", async () => {
      // Updating the prefs object keys to have the banner values ready for the test
      prefs["discoverystream.placements.contextualBanners"] =
        "newtab_leaderboard";
      prefs["discoverystream.placements.contextualBanners.counts"] = "1";
      prefs["newtabAdSize.leaderboard"] = true;
      prefs["newtabAdSize.leaderboard.position"] = 2;

      feed.store.getState = () => ({
        Prefs: {
          values: prefs,
        },
        DiscoveryStream: {
          feeds: {
            data: feedsData,
          },
        },
      });

      let placements = feed.getContextualAdsPlacements();

      expect(placements).toEqual([
        ...expected,
        ...[
          {
            placement: "newtab_leaderboard",
            count: 1,
          },
        ],
      ]);

      prefs["newtabAdSize.leaderboard.position"] = 3;

      feed.store.getState = () => ({
        Prefs: {
          values: prefs,
        },
        DiscoveryStream: {
          feeds: {
            data: feedsData,
          },
        },
      });

      placements = feed.getContextualAdsPlacements();

      expect(placements).toEqual([
        ...expected,
        ...[
          {
            placement: "newtab_leaderboard",
            count: 1,
            content: {
              taxonomy: "IAB-3.0",
              categories: ["386"],
            },
          },
        ],
      ]);
    });

    it("should return SPOC placements AND banner placements when billboard is enabled", async () => {
      // Updating the prefs object keys to have the banner values ready for the test
      prefs["discoverystream.placements.contextualBanners"] =
        "newtab_billboard";
      prefs["discoverystream.placements.contextualBanners.counts"] = "1";
      prefs["newtabAdSize.billboard"] = true;
      prefs["newtabAdSize.billboard.position"] = 2;

      feed.store.getState = () => ({
        Prefs: {
          values: prefs,
        },
        DiscoveryStream: {
          feeds: {
            data: feedsData,
          },
        },
      });

      let placements = feed.getContextualAdsPlacements();

      expect(placements).toEqual([
        ...expected,
        ...[
          {
            placement: "newtab_billboard",
            count: 1,
          },
        ],
      ]);
      prefs["newtabAdSize.billboard.position"] = 3;

      feed.store.getState = () => ({
        Prefs: {
          values: prefs,
        },
        DiscoveryStream: {
          feeds: {
            data: feedsData,
          },
        },
      });

      placements = feed.getContextualAdsPlacements();

      expect(placements).toEqual([
        ...expected,
        ...[
          {
            placement: "newtab_billboard",
            count: 1,
            content: {
              taxonomy: "IAB-3.0",
              categories: ["386"],
            },
          },
        ],
      ]);
    });
  });
});
