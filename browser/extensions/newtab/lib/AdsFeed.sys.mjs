/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
const VERSION = "7";

const lazy = {
  Utils: "resource://services-settings/Utils.sys.mjs",
};

ChromeUtils.defineESModuleGetters(lazy, {
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
});

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";

// Prefs for AdsFeeds to run
const PREF_UNIFIED_ADS_ADSFEED_ENABLED = "unifiedAds.adsFeed.enabled";
const PREF_UNIFIED_ADS_SPOCS_ENABLED = "unifiedAds.spocs.enabled";
const PREF_UNIFIED_ADS_TILES_ENABLED = "unifiedAds.tiles.enabled";

// Prefs for UAPI
const PREF_UNIFIED_ADS_BLOCKED_LIST = "unifiedAds.blockedAds";
const PREF_UNIFIED_ADS_ENDPOINT = "unifiedAds.endpoint";

// Prefs for Tiles
const PREF_TILES_COUNTS = "discoverystream.placements.tiles.counts";
const PREF_TILES_PLACEMENTS = "discoverystream.placements.tiles";

// Prefs for Sponsored Content
const PREF_SPOC_COUNTS = "discoverystream.placements.spocs.counts";
const PREF_SPOC_PLACEMENTS = "discoverystream.placements.spocs";

// Primary pref that is toggled when enabling top site sponsored tiles
const PREF_FEED_TOPSITES = "feeds.topsites";
const PREF_SYSTEM_TOPSITES = "feeds.system.topsites";
const PREF_SHOW_SPONSORED_TOPSITES = "showSponsoredTopSites";

// Primary pref that is toggled when enabling sponsored stories
const PREF_FEED_SECTION_TOPSTORIES = "feeds.section.topstories";
const PREF_SYSTEM_TOPSTORIES = "feeds.system.topstories";
const PREF_SHOW_SPONSORED = "showSponsored";
const PREF_SYSTEM_SHOW_SPONSORED = "system.showSponsored";

const CACHE_KEY = "ads_feed";
const ADS_UPDATE_TIME = 1; // 30 minutes
const USE_ADS_CLIENT = true;

import {
  MozAdsClient,
  MozAdsClientConfig,
  MozAdsEnvironment,
  MozAdsPlacementRequest,
  MozAdsPlacementRequestWithCount,
  MozAdsCacheConfig,
  MozAdsTelemetryImpl,
} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs"

class LoggerTelemetry extends MozAdsTelemetryImpl {
  constructor(opts) {
    console.error("LoggerTelemetry - simple constructor");
    super(opts);
  }
  recordBuildCacheError(label, value) {
    console.error("AdsClient - recordBuildCacheError", label, value);
  }
  recordClientError(label, value) {
    console.error("AdsClient - recordClientError", label, value);
  }
  recordClientOperationTotal(label, value) {
    console.error("AdsClient - recordClientOperationTotal", label, value);
  }
  recordDeserializationError(label, value) {
    console.error("AdsClient - recordDeserializationError", label, value);
  }
  recordHttpCacheOutcome(label, value) {
    console.error("AdsClient - recordHttpCacheOutcome", label, value);
  }
}

const telemetry = new LoggerTelemetry();

const AdsClient = MozAdsClient.init(new MozAdsClientConfig({
  cacheConfig: new MozAdsCacheConfig({
    dbPath: "ads_cache.db",
  }),
  environment: MozAdsEnvironment.PROD,
  telemetry,
}));

export class AdsFeed {
  constructor() {
    this.enabled = false;
    this.loaded = false;
    this.lastUpdated = null;
    this.tiles = [];
    this.spocs = [];
    this.spocPlacements = [];
    this.cache = this.PersistentCache(CACHE_KEY, true);
  }

  async _resetCache() {
    if (this.cache) {
      await this.cache.set("ads", {});
    }
  }

  async resetAdsFeed() {
    await this._resetCache();
    this.tiles = [];
    this.spocs = [];
    this.spocPlacements = [];
    this.lastUpdated = null;
    this.loaded = false;
    this.enabled = false;

    this.store.dispatch(
      ac.OnlyToMain({
        type: at.ADS_RESET,
      })
    );
  }

  async deleteUserAdsData() {
    const state = this.store.getState();
    const headers = new Headers();
    const endpointBaseUrl = state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];

    if (!endpointBaseUrl) {
      return;
    }

    const endpoint = `${endpointBaseUrl}v1/delete_user`;
    const body = {
      context_id: await lazy.ContextId.request(),
    };

    headers.append("content-type", "application/json");

    await this.fetch(endpoint, {
      method: "DELETE",
      headers,
      body: JSON.stringify(body),
    });
  }

  isAdsFeedEnabled() {
    // Check if AdsFeed is enabled
    return this.store.getState().Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED];
  }

  isEnabled() {
    this.loaded = true;

    // Check if AdsFeed is enabled
    const adsFeedEnabled = this.isAdsFeedEnabled();

    if (!adsFeedEnabled) {
      // Exit early as AdsFeed is turned off and shouldn't do anything
      return false;
    }

    // Check all known prefs that top site tiles are enabled
    const tilesEnabled =
      this.store.getState().Prefs.values[PREF_FEED_TOPSITES] &&
      this.store.getState().Prefs.values[PREF_SYSTEM_TOPSITES];

    const sponsoredTilesEnabled =
      this.store.getState().Prefs.values[PREF_SHOW_SPONSORED_TOPSITES] &&
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_TILES_ENABLED];

    // Check all known prefs that spocs are enabled
    const sponsoredStoriesEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED] &&
      this.store.getState().Prefs.values[PREF_SYSTEM_SHOW_SPONSORED] &&
      this.store.getState().Prefs.values[PREF_SHOW_SPONSORED];

    const storiesEnabled =
      this.store.getState().Prefs.values[PREF_FEED_SECTION_TOPSTORIES] &&
      this.store.getState().Prefs.values[PREF_SYSTEM_TOPSTORIES];

    // Confirm at least one ads section (tiles, spocs) are enabled to enable the AdsFeed
    if (
      (tilesEnabled && sponsoredTilesEnabled) ||
      (storiesEnabled && sponsoredStoriesEnabled)
    ) {
      if (adsFeedEnabled) {
        this.enabled = true;
      }

      return adsFeedEnabled;
    }

    // If the AdsFeed is enabled but no placements are enabled, delete user ads data
    this.deleteUserAdsData();

    return false;
  }

  /**
   * This thin wrapper around global.fetch makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   */
  fetch(...args) {
    return fetch(...args);
  }

  /**
   * Normalize new Unified Ads API response into
   * previous Contile ads response
   *
   * @param {Array} - Array of UAPI placement objects ("newtab_tile_1", etc.)
   * @returns {object} - Object containing array of formatted UAPI objects to match legacy Contile system
   */
  _normalizeTileData(data) {
    const formattedTileDataArray = [];
    const responseTilesData = Object.values(data);

    // Bug 1930653: Confirm response has data before iterating
    if (responseTilesData?.length) {
      for (const tileData of responseTilesData) {
        const [tile] = tileData;

        const formattedData = {
          id: tile.block_key,
          block_key: tile.block_key,
          name: tile.name,
          url: tile.url,
          click_url: tile.callbacks.click,
          image_url: tile.image_url,
          impression_url: tile.callbacks.impression,
          image_size: 200,
        };

        formattedTileDataArray.push(formattedData);
      }
    }

    return { tiles: formattedTileDataArray };
  }

  /**
   * Return object of supported ad types to query from MARS API from the AdsFeed file
   *
   * @returns {object}
   */
  getSupportedAdTypes() {
    const supportsAdsFeedTiles =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_TILES_ENABLED];

    const supportsAdsFeedSpocs =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];

    return {
      tiles: supportsAdsFeedTiles,
      spocs: supportsAdsFeedSpocs,
    };
  }

  /**
   * Get ads data either from cache or from API and
   * broadcast the data via at.ADS_UPDATE_{DATA_TYPE} event
   *
   * @param {boolean} isStartup=false - This is only used for reporting
   * and is passed to the update functions meta attribute
   * @returns {void}
   */
  async getAdsData(isStartup = false) {
    const supportedAdTypes = this.getSupportedAdTypes();
    const cachedData = (await this.cache.get()) || {};

    const { ads } = cachedData;
    const adsCacheValid = ads
      ? this.Date().now() - ads.lastUpdated < ADS_UPDATE_TIME
      : false;

    let data = null;

    // Get new data if necessary or default to cache
    if (!ads?.lastUpdated || !adsCacheValid) {
      // Fetch new data
      data = await this.fetchData(supportedAdTypes);
      data.lastUpdated = this.Date().now();
    } else {
      // Use cached data
      data = ads;
    }

    if (!data) {
      throw new Error(`No data available`);
    }

    // Update tile information if tile data is supported
    if (supportedAdTypes.tiles) {
      this.tiles = data.tiles;
    }

    // Update tile information if spoc data is supported
    if (supportedAdTypes.spocs) {
      this.spocs = data.spocs;
      // DSFeed uses unifiedAdsPlacements to determine which spocs to fetch/place into the feed.
      this.spocPlacements = data.spocPlacements;
    }

    this.lastUpdated = data.lastUpdated;
    await this.update(isStartup);
  }

  /**
   * Fetch data from the Mozilla Ad Routing Service (MARS) unified ads API
   * This function is designed to get whichever ads types are needed (tiles, spocs)
   *
   * @param {Array} supportedAdTypes
   * @returns {object} Response object containing ad information from MARS
   */
  async fetchData(supportedAdTypes) {
    const state = this.store.getState();
    const headers = new Headers();
    headers.append("content-type", "application/json");

    const endpointBaseUrl = state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];
    const marsOhttpEnabled = Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled",
      false
    );
    const ohttpRelayURL = Services.prefs.getStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
      ""
    );
    const ohttpConfigURL = Services.prefs.getStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
      ""
    );

    let blockedSponsors =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_BLOCKED_LIST];

    // Overwrite URL to Unified Ads endpoint
    const fetchUrl = `${endpointBaseUrl}v1/ads`;

    // Can include both tile and spoc placement data
    let placements = [];
    let responseData;
    let returnData = {};

    // Determine which data needs to be fetched
    if (supportedAdTypes.tiles) {
      const tilesPlacementsArray = state.Prefs.values[
        PREF_TILES_PLACEMENTS
      ]?.split(`,`)
        .map(s => s.trim())
        .filter(item => item);
      const tilesCountsArray = state.Prefs.values[PREF_TILES_COUNTS]?.split(`,`)
        .map(s => s.trim())
        .filter(item => item)
        .map(item => parseInt(item, 10));

      const tilesPlacements = tilesPlacementsArray.map((placement, index) => ({
        placement,
        count: tilesCountsArray[index],
      }));

      placements.push(...tilesPlacements);
    }

    // Determine which data needs to be fetched
    if (supportedAdTypes.spocs) {
      const spocPlacementsArray = state.Prefs.values[
        PREF_SPOC_PLACEMENTS
      ]?.split(`,`)
        .map(s => s.trim())
        .filter(item => item);

      const spocCountsArray = state.Prefs.values[PREF_SPOC_COUNTS]?.split(`,`)
        .map(s => s.trim())
        .filter(item => item)
        .map(item => parseInt(item, 10));

      const spocPlacements = spocPlacementsArray.map((placement, index) => ({
        placement,
        count: spocCountsArray[index],
      }));

      returnData.spocPlacements = spocPlacements;

      placements.push(...spocPlacements);
    }

    let fetchPromise;

    const controller = new AbortController();
    const { signal } = controller;

    const options = {
      method: "POST",
      headers,
      body: JSON.stringify({
        context_id: await lazy.ContextId.request(),
        placements,
        blocks: blockedSponsors.split(","),
      }),
      credentials: "omit",
      signal,
    };

    if(USE_ADS_CLIENT) {
      responseData = await this.fetchWithAdsClient(placements);
    } else {
    // Make Oblivious Fetch Request
    if (marsOhttpEnabled && ohttpConfigURL && ohttpRelayURL) {
      const config = await lazy.ObliviousHTTP.getOHTTPConfig(ohttpConfigURL);
      if (!config) {
        console.error(
          new Error(
            `OHTTP was configured for ${fetchUrl} but we couldn't fetch a valid config`
          )
        );
        return null;
      }
      fetchPromise = lazy.ObliviousHTTP.ohttpRequest(
        ohttpRelayURL,
        config,
        fetchUrl,
        options
      );
    } else {
      fetchPromise = this.fetch(fetchUrl, options);
    }

    const response = await fetchPromise;

    if (response && response.status === 200) {
      responseData = await response.json();
      console.error("AdsClient - responseData", responseData);
    } else {
      throw new Error(
        `Error fetching data: ${response.status} - ${response.statusText}`
      );
    }
  }

    if (supportedAdTypes.tiles) {
      const filteredRespDataTiles = Object.keys(responseData)
        .filter(key => key.startsWith("newtab_tile_"))
        .reduce((acc, key) => {
          acc[key] = responseData[key];
          return acc;
        }, {});

      const normalizedTileData = this._normalizeTileData(filteredRespDataTiles);
      returnData.tiles = normalizedTileData.tiles;
    }

    if (supportedAdTypes.spocs) {
      const filteredRespDataNonTiles = Object.keys(responseData)
        .filter(key => !key.startsWith("newtab_tile_"))
        .reduce((acc, key) => {
          acc[key] = responseData[key];
          return acc;
        }, {});

      returnData.spocs = filteredRespDataNonTiles.newtab_spocs;
    }

    return returnData;
  }

  async fetchWithAdsClient(placements) {
    console.error("AdsClient - fetchWithAdsClient calling with", placements);
    const formattedResponse = {};
    try {
      const tilesRequests = placements.filter(
        p => (p.placementId || p.placement)?.startsWith("newtab_tile_")
      ).map(p => new MozAdsPlacementRequest({
        placementId: p.placementId || p.placement,
        iabContent: p.iabContent ?? null,
      }));
      if (tilesRequests.length) {
        console.error("AdsClient - requestTileAds calling with", tilesRequests);
        const tiles = await AdsClient.requestTileAds(tilesRequests, null);
        console.error("AdsClient - requestTileAds SUCCESS - tiles:", tiles);

        for (const [placementId, tile] of tiles) {
          tile.name = `${VERSION}: ${tile.name}`;
          formattedResponse[placementId] = [tile];
        }
      }

      const storiesRequests = placements.filter(
        p => (p.placementId || p.placement)?.startsWith("newtab_stories_")
      ).map(p => new MozAdsPlacementRequestWithCount({
        placementId: p.placementId || p.placement,
        iabContent: p.iabContent ?? null,
        count: p.count,
      }));
      if (storiesRequests.length) {
        console.error("AdsClient - requestSpocAds calling with", storiesRequests);
        const spocs = await AdsClient.requestSpocAds(storiesRequests, null);
        console.error("AdsClient - requestSpocAds SUCCESS - spocs:", spocs);

        for (const [placementId, spoc] of spocs) {
          spoc[0].name = `${VERSION}: ${spoc[0].name}`;
          formattedResponse[placementId] = spoc;
        }
      }
    } catch (error) {
      console.error("AdsClient - requestTileAds ERROR:", error);
    }
    console.error("AdsClient - formattedResponse:", formattedResponse);
    return formattedResponse;
  }
  /**
   * Init function that runs only from onAction at.INIT call.
   *
   * @param {boolean} isStartup=false
   * @returns {void}
   */
  async init(isStartup = false) {
    if (this.isEnabled()) {
      await this.getAdsData(isStartup);
    }
  }

  /**
   * Sets cached data and dispatches at.ADS_UPDATE_{DATA_TYPE} event to update store with new ads data
   *
   * @param {boolean} isStartup
   * @returns {void}
   */
  async update(isStartup) {
    await this.cache.set("ads", {
      ...(this.tiles ? { tiles: this.tiles } : {}),
      ...(this.spocs ? { spocs: this.spocs } : {}),
      ...(this.spocPlacements ? { spocPlacements: this.spocPlacements } : {}),
      lastUpdated: this.lastUpdated,
    });

    if (this.tiles && this.tiles.length) {
      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.ADS_UPDATE_TILES,
          data: {
            tiles: this.tiles,
          },
          meta: {
            isStartup,
          },
        })
      );
    }

    if (this.spocs && this.spocs.length) {
      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.ADS_UPDATE_SPOCS,
          data: {
            spocs: this.spocs,
            spocPlacements: this.spocPlacements,
          },
          meta: {
            isStartup,
          },
        })
      );
    }
  }

  async onPrefChangedAction(action) {
    switch (action.data.name) {
      // AdsFeed Feature Prefs
      // Shortcuts or Stories Enabled/Disabled
      case PREF_UNIFIED_ADS_TILES_ENABLED:
      case PREF_UNIFIED_ADS_SPOCS_ENABLED:
      case PREF_UNIFIED_ADS_ADSFEED_ENABLED:
      case PREF_FEED_TOPSITES:
      case PREF_SYSTEM_TOPSITES:
      case PREF_SYSTEM_TOPSTORIES:
      case PREF_FEED_SECTION_TOPSTORIES:
        if (!this.isAdsFeedEnabled()) {
          // Only act on these prefs if AdsFeed is enabled
          break;
        }

        // TODO: Should we use the value of these prefs to determine what to do?
        if (this.isEnabled()) {
          await this.getAdsData(false);
        } else {
          await this.deleteUserAdsData();
          await this.resetAdsFeed();
        }
        break;
      case PREF_SHOW_SPONSORED_TOPSITES:
      case PREF_SHOW_SPONSORED:
      case PREF_SYSTEM_SHOW_SPONSORED:
        if (!this.isEnabled()) {
          // Only act on these prefs if AdsFeed is enabled
          break;
        }

        if (action.data.value) {
          await this.getAdsData(false);
        } else {
          await this.deleteUserAdsData();
          await this.resetAdsFeed();
        }

        break;
    }
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        await this.init(true /* isStartup */);
        break;
      case at.UNINIT:
        break;
      case at.DISCOVERY_STREAM_DEV_SYSTEM_TICK:
      case at.SYSTEM_TICK:
        if (this.isEnabled()) {
          await this.getAdsData(false);
        }
        break;

      case at.PREF_CHANGED:
        await this.onPrefChangedAction(action);
        break;
      case at.DISCOVERY_STREAM_CONFIG_CHANGE: // Event emitted from ASDevTools "Reset Cache" button
      case at.DISCOVERY_STREAM_DEV_EXPIRE_CACHE: // Event emitted from ASDevTools "Expire Cache" button
        // Clear cache
        await this.resetAdsFeed();

        // Get new ads
        if (this.isEnabled()) {
          await this.getAdsData(false);
        }
        break;
    }
  }
}

/**
 * Creating a thin wrapper around PersistentCache, ObliviousHTTP and Date.
 * This makes it easier for us to write automated tests that simulate responses.
 */

AdsFeed.prototype.PersistentCache = (...args) => {
  return new lazy.PersistentCache(...args);
};

AdsFeed.prototype.Date = () => {
  return Date;
};

AdsFeed.prototype.ObliviousHTTP = (...args) => {
  return lazy.ObliviousHTTP(...args);
};
