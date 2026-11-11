/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// AppConstants, and overrides importESModule to be a no-op (which
// can't be done for a static import statement).

import {
  actionCreators as ac,
  actionTypes as at,
} from "resource://newtab/common/Actions.mjs";

import { ImportHelper } from "resource://newtab/lib/ImportHelper.sys.mjs";

/**
 * @backward-compat { version 162 }
 *
 * Switch back to normal imports once 162 hits release.
 */
const { TippyTopProvider } = ImportHelper.import(
  "moz-src:///browser/components/topsites/TippyTopProvider.sys.mjs",
  "resource:///modules/topsites/"
);
const { insertPinned } = ImportHelper.import(
  "moz-src:///browser/components/topsites/TopSites.sys.mjs",
  "resource:///modules/topsites/"
);
const { TOP_SITES_MAX_ROWS, TOP_SITES_MAX_SITES_PER_ROW } = ImportHelper.import(
  "moz-src:///browser/components/topsites/constants.mjs",
  "resource:///modules/topsites/"
);
import { Dedupe } from "resource:///modules/Dedupe.sys.mjs";

import {
  CUSTOM_SEARCH_SHORTCUTS,
  SEARCH_SHORTCUTS_EXPERIMENT,
  SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF,
  SEARCH_SHORTCUTS_HAVE_PINNED_PREF,
  checkHasSearchEngine,
  getSearchProvider,
} from "moz-src:///toolkit/components/search/SearchShortcuts.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  FilterAdult: "resource:///modules/FilterAdult.sys.mjs",
  LinksCache: "resource:///modules/LinksCache.sys.mjs",
  MozAdsPlacementRequest:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  Sampling: "resource://gre/modules/components-utils/Sampling.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  Screenshots: "resource://newtab/lib/Screenshots.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { Logger } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/Logger.sys.mjs"
  );
  return new Logger("TopSitesFeed");
});

const DEFAULT_SITES_PREF = "default.sites";
const SHOWN_ON_NEWTAB_PREF = "feeds.topsites";
export const DEFAULT_TOP_SITES = [];
const MIN_FAVICON_SIZE = 96;
const CACHED_LINK_PROPS_TO_MIGRATE = ["screenshot", "customScreenshot"];
const PINNED_FAVICON_PROPS_TO_MIGRATE = [
  "favicon",
  "faviconRef",
  "faviconSize",
];

const CACHE_KEY = "contile";
const ROWS_PREF = "topSitesRows";
const PREF_MAX_SITES_PER_ROW = "topSitesMaxSitesPerRow";
const SHOW_SPONSORED_PREF = "showSponsoredTopSites";
const GROUPED_PINS_PREF = "topSitesGroupedPins";
// The default total number of sponsored top sites to fetch from Contile
// and Pocket.
const MAX_NUM_SPONSORED = 3;
// Nimbus variable for the total number of sponsored top sites including
// both Contile and Pocket sources.
// The default will be `MAX_NUM_SPONSORED` if this variable is unspecified.
const NIMBUS_VARIABLE_MAX_SPONSORED = "topSitesMaxSponsored";
// Nimbus variable to allow more than two sponsored tiles from Contile to be
//considered for Top Sites.
const NIMBUS_VARIABLE_ADDITIONAL_TILES =
  "topSitesUseAdditionalTilesFromContile";
// Nimbu variable for the total number of sponsor topsite that come from Contile
// The default will be `CONTILE_MAX_NUM_SPONSORED` if variable is unspecified.
const NIMBUS_VARIABLE_CONTILE_MAX_NUM_SPONSORED = "topSitesContileMaxSponsored";

const PREF_UNIFIED_ADS_TILES_ENABLED = "unifiedAds.tiles.enabled";
const PREF_UNIFIED_ADS_ENDPOINT = "unifiedAds.endpoint";
const PREF_UNIFIED_ADS_PLACEMENTS = "discoverystream.placements.tiles";
const PREF_UNIFIED_ADS_COUNTS = "discoverystream.placements.tiles.counts";
const PREF_UNIFIED_ADS_BLOCKED_LIST = "unifiedAds.blockedAds";
const PREF_UNIFIED_ADS_ADSFEED_ENABLED = "unifiedAds.adsFeed.enabled";

const PREF_SOV_ENABLED = "sov.enabled";
const PREF_SOV_FRECENCY_EXPOSURE = "sov.frecency.exposure";
const PREF_SOV_NAME = "sov.name";
const PREF_SOV_AMP_ALLOCATION = "sov.amp.allocation";
const PREF_SOV_FRECENCY_ALLOCATION = "sov.frecency.allocation";
const DEFAULT_SOV_SLOT_COUNT = 3;

// Search experiment stuff
const FILTER_DEFAULT_SEARCH_PREF = "improvesearch.noDefaultSearchTile";
const SEARCH_FILTERS = [
  "google",
  "search.yahoo",
  "yahoo",
  "bing",
  "ask",
  "duckduckgo",
];

const REMOTE_SETTING_DEFAULTS_PREF = "browser.topsites.useRemoteSetting";
const DEFAULT_SITES_OVERRIDE_PREF =
  "browser.newtabpage.activity-stream.default.sites";
const DEFAULT_SITES_EXPERIMENTS_PREF_BRANCH = "browser.topsites.experiment.";

// Mozilla Tiles Service (Contile) prefs
// Nimbus variable for the Contile integration. It falls back to the pref:
// `browser.topsites.contile.enabled`.
const NIMBUS_VARIABLE_CONTILE_ENABLED = "topSitesContileEnabled";
const NIMBUS_VARIABLE_CONTILE_POSITIONS = "contileTopsitesPositions";
const CONTILE_ENDPOINT_PREF = "browser.topsites.contile.endpoint";
const CONTILE_UPDATE_INTERVAL = 15 * 60 * 1000; // 15 minutes
// The maximum number of sponsored top sites to fetch from Contile.
const CONTILE_MAX_NUM_SPONSORED = 3;
const TOP_SITES_BLOCKED_SPONSORS_PREF = "browser.topsites.blockedSponsors";
const CONTILE_CACHE_VALID_FOR_PREF = "browser.topsites.contile.cacheValidFor";
const CONTILE_CACHE_LAST_FETCH_PREF = "browser.topsites.contile.lastFetch";
const CONTILE_CACHE_VALID_FOR_FALLBACK = 3 * 60 * 60; // 3 hours in seconds

// Partners of sponsored tiles.
const SPONSORED_TILE_PARTNER_AMP = "amp";
const SPONSORED_TILE_PARTNER_MOZ_SALES = "moz-sales";
const SPONSORED_TILE_PARTNER_FREC_BOOST = "frec-boost";
const SPONSORED_TILE_PARTNERS = new Set([
  SPONSORED_TILE_PARTNER_AMP,
  SPONSORED_TILE_PARTNER_MOZ_SALES,
  SPONSORED_TILE_PARTNER_FREC_BOOST,
]);

const DISPLAY_FAIL_REASON_OVERSOLD = "oversold";
const DISPLAY_FAIL_REASON_DISMISSED = "dismissed";
const DISPLAY_FAIL_REASON_UNRESOLVED = "unresolved";

ChromeUtils.defineLazyGetter(lazy, "userAgent", () => {
  return Cc["@mozilla.org/network/protocol;1?name=http"].getService(
    Ci.nsIHttpProtocolHandler
  ).userAgent;
});

function getTopSitesCount(prefValues) {
  return (
    prefValues[ROWS_PREF] *
    (prefValues.trainhopConfig?.topSites?.maxSitesPerRow ??
      prefValues[PREF_MAX_SITES_PER_ROW] ??
      TOP_SITES_MAX_SITES_PER_ROW)
  );
}

// Smart shortcuts
import { RankShortcutsProvider } from "resource://newtab/lib/SmartShortcutsRanker/RankShortcuts.mjs";
import { FrecencyBoostProvider } from "resource://newtab/lib/FrecencyBoostProvider/FrecencyBoostProvider.mjs";
import { SponsoredTopSitesScoreProvider } from "resource://newtab/lib/SponsoredTopSitesScoreProvider/SponsoredTopSitesScoreProvider.mjs";

const PREF_SYSTEM_SHORTCUTS_PERSONALIZATION =
  "discoverystream.shortcuts.personalization.enabled";

function smartshortcutsEnabled(values) {
  // if nimbus pref is valid we use it, otherwise fall back to local pref
  const experimentVariable = values.trainhopConfig?.smartShortcuts?.enabled;
  if (typeof experimentVariable === "boolean") {
    return experimentVariable;
  }
  return !!values[PREF_SYSTEM_SHORTCUTS_PERSONALIZATION];
}
const OVERSAMPLE_MULTIPLIER = 2;

/**
 * Number of slots within the first numItems that ranked organic sites can
 * fill, after pinned and sponsored tiles take theirs.
 *
 * @param {number} numItems Total shortcut slots.
 * @param {Array<object|null>} pinned Pinned links by position, possibly sparse.
 * @param {Array<object>} sponsored Sponsored links with a 1-based
 *   sponsored_position.
 * @returns {number}
 */
function countOrganicSlots(numItems, pinned, sponsored) {
  const pinnedSlots = pinned.slice(0, numItems).filter(Boolean).length;
  // A later partner's tile replaces an earlier one at the same position, so
  // each position takes a single slot.
  const sponsoredSlots = new Set(
    sponsored
      .map(link => link?.sponsored_position)
      .filter(position => position >= 1 && position <= numItems)
  ).size;
  return Math.max(0, numItems - pinnedSlots - sponsoredSlots);
}

function getShortHostnameForCurrentSearch() {
  return lazy.NewTabUtils.shortHostname(
    lazy.SearchService.defaultEngine.searchUrlDomain
  );
}

class TopSitesTelemetry {
  constructor() {
    this.allSponsoredTiles = {};
    this.sponsoredTilesConfigured = 0;
  }

  _tileProviderForTiles(tiles) {
    // Assumption: the list of tiles is from a single provider
    return tiles && tiles.length ? this._tileProvider(tiles[0]) : null;
  }

  _tileProvider(tile) {
    return tile.partner || SPONSORED_TILE_PARTNER_AMP;
  }

  _buildPropertyKey(tile) {
    let provider = this._tileProvider(tile);
    return provider + lazy.NewTabUtils.shortURL(tile);
  }

  // Returns an array of strings indicating the property name (based on the
  // provider and brand) of tiles that have been filtered e.g. ["moz-salesbrand1"]
  // currentTiles: The list of tiles remaining and may be displayed in new tab.
  // this.allSponsoredTiles: The original list of tiles set via setTiles prior to any filtering
  // The returned list indicated the difference between these two lists (excluding any previously filtered tiles).
  _getFilteredTiles(currentTiles) {
    let notPreviouslyFilteredTiles = Object.assign(
      {},
      ...Object.entries(this.allSponsoredTiles)
        .filter(
          ([, v]) =>
            v.display_fail_reason === null ||
            v.display_fail_reason === undefined
        )
        .map(([k, v]) => ({ [k]: v }))
    );

    // Get the property names of the newly filtered list.
    let remainingTiles = currentTiles.map(el => {
      return this._buildPropertyKey(el);
    });

    // Get the property names of the tiles that were filtered.
    let tilesToUpdate = Object.keys(notPreviouslyFilteredTiles).filter(
      element => !remainingTiles.includes(element)
    );
    return tilesToUpdate;
  }

  setSponsoredTilesConfigured() {
    const maxSponsored =
      lazy.NimbusFeatures.pocketNewtab.getVariable(
        NIMBUS_VARIABLE_MAX_SPONSORED
      ) ?? MAX_NUM_SPONSORED;

    this.sponsoredTilesConfigured = maxSponsored;
    Glean.topsites.sponsoredTilesConfigured.set(maxSponsored);
  }

  clearTilesForProvider(provider) {
    Object.entries(this.allSponsoredTiles)
      .filter(([k]) => k.startsWith(provider))
      .map(([k]) => delete this.allSponsoredTiles[k]);
  }

  _getAdvertiser(tile) {
    let label = tile.label || null;
    let title = tile.title || null;

    return label ?? title ?? lazy.NewTabUtils.shortURL(tile);
  }

  setTiles(tiles) {
    // Assumption: the list of tiles is from a single provider,
    // should be called once per tile source.
    if (tiles && tiles.length) {
      let tile_provider = this._tileProviderForTiles(tiles);
      this.clearTilesForProvider(tile_provider);

      for (let sponsoredTile of tiles) {
        this.allSponsoredTiles[this._buildPropertyKey(sponsoredTile)] = {
          advertiser: this._getAdvertiser(sponsoredTile).toLowerCase(),
          provider: tile_provider,
          display_position: null,
          display_fail_reason: null,
        };
      }
    }
  }

  _setDisplayFailReason(filteredTiles, reason) {
    for (let tile of filteredTiles) {
      if (tile in this.allSponsoredTiles) {
        let tileToUpdate = this.allSponsoredTiles[tile];
        tileToUpdate.display_position = null;
        tileToUpdate.display_fail_reason = reason;
      }
    }
  }

  determineFilteredTilesAndSetToOversold(nonOversoldTiles) {
    let filteredTiles = this._getFilteredTiles(nonOversoldTiles);
    this._setDisplayFailReason(filteredTiles, DISPLAY_FAIL_REASON_OVERSOLD);
  }

  determineFilteredTilesAndSetToDismissed(nonDismissedTiles) {
    let filteredTiles = this._getFilteredTiles(nonDismissedTiles);
    this._setDisplayFailReason(filteredTiles, DISPLAY_FAIL_REASON_DISMISSED);
  }

  _setTilePositions(currentTiles) {
    // This function performs many loops over a small dataset.  The size of
    // dataset is limited by the number of sponsored tiles displayed on
    // the newtab instance.
    if (this.allSponsoredTiles) {
      let tilePositionsAssigned = [];
      // processing the currentTiles parameter, assigns a position to the
      // corresponding property in this.allSponsoredTiles
      currentTiles.forEach(item => {
        let tile = this.allSponsoredTiles[this._buildPropertyKey(item)];
        if (
          tile &&
          (tile.display_fail_reason === undefined ||
            tile.display_fail_reason === null)
        ) {
          tile.display_position = item.sponsored_position;
          // Track assigned tile slots.
          tilePositionsAssigned.push(item.sponsored_position);
        }
      });

      // Need to check if any objects in this.allSponsoredTiles do not
      // have either a display_fail_reason or a display_position set.
      // This can happen if the tiles list was updated before the
      // metric is written to Glean.
      // See https://bugzilla.mozilla.org/show_bug.cgi?id=1877197
      let tilesMissingPosition = [];
      Object.keys(this.allSponsoredTiles).forEach(property => {
        let tile = this.allSponsoredTiles[property];
        if (!tile.display_fail_reason && !tile.display_position) {
          tilesMissingPosition.push(property);
        }
      });

      if (tilesMissingPosition.length) {
        // Determine if any available slots exist based on max number of tiles
        // and the list of tiles already used and assign to a tile with missing
        // value.
        for (let i = 1; i <= this.sponsoredTilesConfigured; i++) {
          if (!tilePositionsAssigned.includes(i)) {
            let tileProperty = tilesMissingPosition.shift();
            this.allSponsoredTiles[tileProperty].display_position = i;
          }
        }
      }

      // At this point we might still have a few unresolved states.  These
      // rows will be tagged with a display_fail_reason `unresolved`.
      this._detectErrorConditionAndSetUnresolved();
    }
  }

  // Checks the data for inconsistent state and updates the display_fail_reason
  _detectErrorConditionAndSetUnresolved() {
    Object.keys(this.allSponsoredTiles).forEach(property => {
      let tile = this.allSponsoredTiles[property];
      if (
        (!tile.display_fail_reason && !tile.display_position) ||
        (tile.display_fail_reason && tile.display_position)
      ) {
        tile.display_position = null;
        tile.display_fail_reason = DISPLAY_FAIL_REASON_UNRESOLVED;
      }
    });
  }

  finalizeNewtabPingFields(currentTiles) {
    this._setTilePositions(currentTiles);
    Glean.topsites.sponsoredTilesReceived.set(
      JSON.stringify({
        sponsoredTilesReceived: Object.values(this.allSponsoredTiles),
      })
    );
  }
}

export class ContileIntegration {
  constructor(topSitesFeed) {
    this._topSitesFeed = topSitesFeed;
    this._lastPeriodicUpdate = 0;
    this._sites = [];
    // The Share-of-Voice object managed by Shepherd and sent via Contile.
    this._sov = null;
    this.cache = this.PersistentCache(CACHE_KEY, true);
  }

  get sites() {
    return this._sites;
  }

  get sov() {
    return this._sov;
  }

  periodicUpdate() {
    let now = Date.now();
    if (now - this._lastPeriodicUpdate >= CONTILE_UPDATE_INTERVAL) {
      this._lastPeriodicUpdate = now;
      this.refresh();
    }
  }

  async refresh() {
    let updateDefaultSites = await this._fetchSites();
    await this._topSitesFeed.allocatePositions();
    if (updateDefaultSites) {
      // Sponsored tiles changing (including the 15 minute periodic refetch)
      // must not reshuffle organic shortcuts, so the ranker gates this refresh
      // on the rerank interval like SYSTEM_TICK.
      this._topSitesFeed._readDefaults({ reason: "SPONSORED_TILES_UPDATED" });
    }
  }

  /**
   * Clear Contile Cache.
   */
  _resetContileCache() {
    Services.prefs.clearUserPref(CONTILE_CACHE_LAST_FETCH_PREF);
    Services.prefs.clearUserPref(CONTILE_CACHE_VALID_FOR_PREF);

    // This can be async, but in this case we don't need to wait.
    this.cache.set("contile", []);
  }

  /**
   * Filter the tiles whose sponsor is on the Top Sites sponsor blocklist.
   *
   * @param {Array} tiles
   *   An array of the tile objects
   */
  _filterBlockedSponsors(tiles) {
    let blocklist;
    try {
      blocklist = JSON.parse(
        Services.prefs.getStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF, "[]")
      );
    } catch (e) {
      blocklist = [];
    }
    const currentSearchHostname = this._topSitesFeed._currentSearchHostname;
    return tiles.filter(tile => {
      const shortURL = lazy.NewTabUtils.shortURL(tile);
      if (blocklist.includes(shortURL)) {
        return false;
      }
      // Drop a sponsored tile that matches the user's current default
      // search engine so it doesn't duplicate the search bar. Maintained
      // tile count relies on the source (MARS via `blocks`, or Contile
      // over-returning) supplying a substitute.
      if (currentSearchHostname && shortURL === currentSearchHostname) {
        return false;
      }
      return true;
    });
  }

  /**
   * Calculate the time Contile response is valid for based on cache-control header
   *
   * @param {string} cacheHeader
   *   string value of the Contile resposne cache-control header
   */
  _extractCacheValidFor(cacheHeader) {
    const unifiedAdsTilesEnabled =
      this._topSitesFeed.store.getState().Prefs.values[
        PREF_UNIFIED_ADS_TILES_ENABLED
      ];

    // Note: Cache-control only applies to direct Contile API calls
    if (!cacheHeader && !unifiedAdsTilesEnabled) {
      lazy.log.warn("Contile response cache control header is empty");
      return 0;
    }
    const [, staleIfError] = cacheHeader.match(/stale-if-error=\s*([0-9]+)/i);
    const [, maxAge] = cacheHeader.match(/max-age=\s*([0-9]+)/i);
    const validFor =
      Number.parseInt(staleIfError, 10) + Number.parseInt(maxAge, 10);
    return isNaN(validFor) ? 0 : validFor;
  }

  /**
   * Load Tiles from Contile Cache Prefs
   */
  async _loadTilesFromCache() {
    lazy.log.info("Contile client is trying to load tiles from local cache.");
    const now = Math.round(Date.now() / 1000);
    const lastFetch = Services.prefs.getIntPref(
      CONTILE_CACHE_LAST_FETCH_PREF,
      0
    );
    const validFor = Services.prefs.getIntPref(
      CONTILE_CACHE_VALID_FOR_PREF,
      CONTILE_CACHE_VALID_FOR_FALLBACK
    );
    this._topSitesFeed._telemetryUtility.setSponsoredTilesConfigured();
    if (now <= lastFetch + validFor) {
      try {
        const cachedData = (await this.cache.get()) || {};
        let cachedTiles = cachedData.contile;
        this._topSitesFeed._telemetryUtility.setTiles(cachedTiles);
        cachedTiles = this._filterBlockedSponsors(cachedTiles);
        this._topSitesFeed._telemetryUtility.determineFilteredTilesAndSetToDismissed(
          cachedTiles
        );
        this._sites = cachedTiles;
        lazy.log.info("Local cache loaded.");
        return true;
      } catch (error) {
        lazy.log.warn(`Failed to load tiles from local cache: ${error}.`);
        return false;
      }
    }

    return false;
  }

  /**
   * Determine number of Tiles to get from Contile
   */
  _getMaxNumFromContile() {
    return (
      lazy.NimbusFeatures.pocketNewtab.getVariable(
        NIMBUS_VARIABLE_CONTILE_MAX_NUM_SPONSORED
      ) ?? CONTILE_MAX_NUM_SPONSORED
    );
  }

  /**
   * Normalize new Unified Ads API response into previous Contile ads response shape.
   *
   * @param {object} data
   *   Tiles keyed by placement ID, as returned by MARS or the ads client.
   *   Each value should be an array of 0 or 1 items.
   * @param {Array<string>} placementIds
   *   Placement IDs in the order tiles should appear.
   *   IDs absent from this list are emitted last, keeping their iteration order from `data`.
   */
  _normalizeTileData(data, placementIds) {
    // Requested placement ids are in iteration order from placementIds
    const requestedPlacementIds = placementIds.filter(id =>
      Object.hasOwn(data, id)
    );
    // Extra placement ids are in iteration order from data
    const extraPlacementIds = Object.keys(data).filter(
      id => !placementIds.includes(id)
    );

    const formattedTileData = [];
    for (const placementId of [
      ...requestedPlacementIds,
      ...extraPlacementIds,
    ]) {
      const [tile] = data[placementId] ?? [];
      if (tile) {
        const formattedData = {
          id: tile.block_key,
          block_key: tile.block_key,
          name: tile.name,
          url: tile.url,
          click_url: tile.callbacks.click,
          image_url: tile.image_url,
          impression_url: tile.callbacks.impression,
          image_size: 200,
          attribution: tile.attributions || null,
        };

        formattedTileData.push(formattedData);
      }
    }

    return { tiles: formattedTileData };
  }

  sovEnabled() {
    const { values } = this._topSitesFeed.store.getState().Prefs;
    const trainhopSovEnabled = values?.trainhopConfig?.sov?.enabled;
    return trainhopSovEnabled || values?.[PREF_SOV_ENABLED];
  }

  csvToInts(val) {
    if (!val) {
      return [];
    }

    return val
      .split(",")
      .map(s => s.trim())
      .filter(item => item)
      .map(item => parseInt(item, 10));
  }

  /**
   * Builds a Share of Voice (SOV) config.
   *
   * @example input data from prefs/trainhopConfig
   * // name: "SOV-20251122215625"
   * // amp:  "100, 100, 100"
   * // frec: "0, 0, 0"
   *
   * @returns {{
   *   name: string,
   *   allocations: Array<{
   *     position: number,
   *     allocation: Array<{
   *       partner: string,
   *       percentage: number,
   *     }>,
   *   }>,
   * }}
   */
  generateSov() {
    const { values } = this._topSitesFeed.store.getState().Prefs;
    const trainhopSovConfig = values?.trainhopConfig?.sov || {};
    const name = trainhopSovConfig.name || values[PREF_SOV_NAME];
    const amp = this.csvToInts(
      trainhopSovConfig.amp || values[PREF_SOV_AMP_ALLOCATION]
    );
    const frec = this.csvToInts(
      trainhopSovConfig.frec || values[PREF_SOV_FRECENCY_ALLOCATION]
    );

    const allocations = Array.from(
      { length: DEFAULT_SOV_SLOT_COUNT },
      (val, i) => ({
        position: i + 1, // 1-based
        allocation: [
          { partner: SPONSORED_TILE_PARTNER_AMP, percentage: amp[i] || 0 },
          {
            partner: SPONSORED_TILE_PARTNER_FREC_BOOST,
            percentage: frec[i] || 0,
          },
        ],
      })
    );

    return { name, allocations };
  }

  // eslint-disable-next-line max-statements
  async _fetchSites() {
    if (
      !lazy.NimbusFeatures.newtab.getVariable(
        NIMBUS_VARIABLE_CONTILE_ENABLED
      ) ||
      !this._topSitesFeed.store.getState().Prefs.values[SHOW_SPONSORED_PREF]
    ) {
      if (this._sites.length) {
        this._sites = [];
        return true;
      }
      return false;
    }

    // Sponsored tile filtering consults the current default search engine
    // (via _currentSearchHostname), so wait for the search service before any
    // fetch/cache path reads it. Continue on failure as getLinksWithDefaults
    // does, so tiles still load when search engines are unavailable.
    try {
      await lazy.SearchService.init();
    } catch {
      // Search engines unavailable; the search-hostname filter is skipped.
    }

    let placementsArray = [];
    let response;
    let body;

    const state = this._topSitesFeed.store.getState();

    const unifiedAdsTilesEnabled =
      state.Prefs.values[PREF_UNIFIED_ADS_TILES_ENABLED];

    const adsFeedEnabled = state.Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED];

    const debugServiceName = unifiedAdsTilesEnabled ? "MARS" : "Contile";

    try {
      // Fetch Data via TopSitesFeed.sys.mjs
      if (!adsFeedEnabled) {
        // Fetch tiles via UAPI service directly from TopSitesFeed.sys.mjs
        if (unifiedAdsTilesEnabled) {
          // Shared set up for manual MARS call and ads-client calls
          placementsArray = state.Prefs.values[
            PREF_UNIFIED_ADS_PLACEMENTS
          ]?.split(`,`)
            .map(s => s.trim())
            .filter(item => item);
          const countsArray = state.Prefs.values[
            PREF_UNIFIED_ADS_COUNTS
          ]?.split(`,`)
            .map(s => s.trim())
            .filter(item => item)
            .map(item => parseInt(item, 10));

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
          const useOhttp = marsOhttpEnabled && ohttpConfigURL && ohttpRelayURL;

          const relevance = useOhttp
            ? await SponsoredTopSitesScoreProvider.getScores()
            : {};

          if (this._topSitesFeed.adsClient) {
            body = await this._fetchSitesWithAdsClient(
              placementsArray,
              relevance
            );
          } else {
            let fetchPromise;
            const headers = new Headers();
            headers.append("content-type", "application/json");

            const endpointBaseUrl =
              state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];

            // We need some basic data that we can pass along to the ohttp request.
            // We purposefully don't use ohttp on this request. We also expect to
            // mostly hit the HTTP cache rather than the network with these requests.
            if (marsOhttpEnabled) {
              const preflightResponse = await this._topSitesFeed.fetch(
                `${endpointBaseUrl}v1/ads-preflight`,
                {
                  method: "GET",
                }
              );
              const preFlight = await preflightResponse.json();

              if (preFlight) {
                // If we don't get a normalized_ua, it means it matched the default userAgent.
                headers.append(
                  "X-User-Agent",
                  preFlight.normalized_ua || lazy.userAgent
                );
                headers.append("X-Geoname-ID", preFlight.geoname_id);
                headers.append("X-Geo-Location", preFlight.geo_location);
              }
            }

            let blockedSponsors =
              this._topSitesFeed.store.getState().Prefs.values[
                PREF_UNIFIED_ADS_BLOCKED_LIST
              ];

            // Also block the user's current default search engine hostname so
            // MARS returns a substitute sponsor instead of leaving us short.
            const blocksList = Array.from(
              new Set(
                blockedSponsors
                  .split(",")
                  .concat(this._topSitesFeed._currentSearchHostname || [])
                  .filter(item => item)
              )
            );

            // Overwrite URL to Unified Ads endpoint
            const fetchUrl = `${endpointBaseUrl}v1/ads`;

            const controller = new AbortController();
            const { signal } = controller;

            const adsBackendConfig = state.Prefs.values?.adsBackendConfig || {};

            const hasRelevance = !!Object.keys(relevance).length;

            const options = {
              method: "POST",
              headers,
              body: JSON.stringify({
                ...(!hasRelevance && {
                  context_id: await lazy.ContextId.request(),
                }),
                flags: adsBackendConfig,
                placements: placementsArray.map((placement, index) => ({
                  placement,
                  count: countsArray[index],
                })),
                blocks: blocksList,
                ...(hasRelevance && { relevance }),
              }),
              credentials: "omit",
              signal,
            };

            if (useOhttp) {
              const config =
                await lazy.ObliviousHTTP.getOHTTPConfig(ohttpConfigURL);
              if (!config) {
                console.error(
                  new Error(
                    `OHTTP was configured for ${fetchUrl} but we couldn't fetch a valid config`
                  )
                );
                return null;
              }

              // ObliviousHTTP.ohttpRequest only accepts a key/value object, and not
              // a Headers instance.
              fetchPromise = lazy.ObliviousHTTP.ohttpRequest(
                ohttpRelayURL,
                config,
                fetchUrl,
                { ...options, headers: Object.fromEntries(headers) }
              );
            } else {
              fetchPromise = this._topSitesFeed.fetch(fetchUrl, options);
            }

            response = await fetchPromise;
          }
        } else {
          // (Default) Fetch tiles via Contile service from TopSitesFeed.sys.mjs
          const fetchUrl = Services.prefs.getStringPref(CONTILE_ENDPOINT_PREF);

          let options = {
            credentials: "omit",
          };

          response = await this._topSitesFeed.fetch(fetchUrl, options);
        }

        // Catch Response Error
        if (response && !response.ok) {
          lazy.log.warn(
            `${debugServiceName} endpoint returned unexpected status: ${response.status}`
          );
          if (response.status === 304 || response.status >= 500) {
            return await this._loadTilesFromCache();
          }
        }

        // Set Cache Prefs
        const lastFetch = Math.round(Date.now() / 1000);
        Services.prefs.setIntPref(CONTILE_CACHE_LAST_FETCH_PREF, lastFetch);
        this._topSitesFeed._telemetryUtility.setSponsoredTilesConfigured();

        // Contile returns 204 indicating there is no content at the moment.
        // If this happens, it will clear `this._sites` reset the cached tiles
        // to an empty array.
        if (response && response.status === 204) {
          this._topSitesFeed._telemetryUtility.clearTilesForProvider(
            SPONSORED_TILE_PARTNER_AMP
          );
          if (this._sites.length) {
            this._sites = [];
            await this.cache.set("contile", this._sites);
            return true;
          }
          return false;
        }
      }

      // Default behavior when ads fetched via TopSitesFeed
      if (response && response.status === 200) {
        body = await response.json();
      }

      // If using UAPI, normalize the data
      if (unifiedAdsTilesEnabled) {
        if (adsFeedEnabled) {
          // IMPORTANT: Ignore all previous fetch logic and get ads data from AdsFeed
          const { tiles } = state.Ads;
          body = { tiles };
        } else {
          // Converts UAPI response into normalized tiles[] array
          body = this._normalizeTileData(body, placementsArray);
        }
      }

      // Logic below runs the same regardless of ad source
      if (body?.sov) {
        this._sov = JSON.parse(atob(body.sov));
      } else if (this.sovEnabled()) {
        this._sov = this.generateSov();
      }

      if (body?.tiles && Array.isArray(body.tiles)) {
        const useAdditionalTiles = lazy.NimbusFeatures.newtab.getVariable(
          NIMBUS_VARIABLE_ADDITIONAL_TILES
        );

        const maxNumFromContile = this._getMaxNumFromContile();

        let { tiles } = body;
        this._topSitesFeed._telemetryUtility.setTiles(tiles);
        if (
          useAdditionalTiles !== undefined &&
          !useAdditionalTiles &&
          tiles.length > maxNumFromContile
        ) {
          tiles.length = maxNumFromContile;
          this._topSitesFeed._telemetryUtility.determineFilteredTilesAndSetToOversold(
            tiles
          );
        }
        tiles = this._filterBlockedSponsors(tiles);
        this._topSitesFeed._telemetryUtility.determineFilteredTilesAndSetToDismissed(
          tiles
        );
        if (tiles.length > maxNumFromContile) {
          lazy.log.info(`Remove unused links from ${debugServiceName}`);
          tiles.length = maxNumFromContile;
          this._topSitesFeed._telemetryUtility.determineFilteredTilesAndSetToOversold(
            tiles
          );
        }
        this._sites = tiles;

        await this.cache.set("contile", this._sites);

        if (!unifiedAdsTilesEnabled) {
          Services.prefs.setIntPref(
            CONTILE_CACHE_VALID_FOR_PREF,
            this._extractCacheValidFor(
              response.headers.get("cache-control") ||
                response.headers.get("Cache-Control")
            )
          );
        } else {
          Services.prefs.setIntPref(
            CONTILE_CACHE_VALID_FOR_PREF,
            CONTILE_CACHE_VALID_FOR_FALLBACK
          );
        }

        return true;
      }
    } catch (error) {
      lazy.log.warn(
        `Failed to fetch data from ${debugServiceName} server: ${error.message}`
      );
      return await this._loadTilesFromCache();
    }
    return false;
  }

  async _fetchSitesWithAdsClient(placements, relevance) {
    const options = lazy.AdsClient.requestOptions(
      this._topSitesFeed.store.getState().Prefs.values,
      // Also block the user's current default search engine hostname so
      // MARS returns a substitute sponsor instead of leaving us short.
      this._topSitesFeed._currentSearchHostname || [],
      relevance
    );

    const requests = placements.map(
      placementId =>
        new lazy.MozAdsPlacementRequest({
          placementId,
          iabContent: null,
        })
    );

    const tiles = await this._topSitesFeed.adsClient.requestTileAds(
      requests,
      options
    );

    return Object.fromEntries(
      tiles.entries().map(([placementId, tile]) => [
        placementId,
        [
          {
            block_key: tile.blockKey,
            name: tile.name,
            url: tile.url,
            image_url: tile.imageUrl,
            callbacks: {
              impression: tile.callbacks.impression,
              click: tile.callbacks.click,
            },
            // Attributions are not returned from MAC
          },
        ],
      ])
    );
  }
}

/**
 * Creating a thin wrapper around PersistentCache.
 * This makes it easier for us to write automated tests that simulate responses.
 */
ContileIntegration.prototype.PersistentCache = (...args) => {
  return new lazy.PersistentCache(...args);
};

export class TopSitesFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    this._telemetryUtility = new TopSitesTelemetry();
    this._contile = new ContileIntegration(this);
    this._tippyTopProvider = new TippyTopProvider();
    this._refreshGeneration = 0;
    this._broadcastPending = false;
    this._latestRefreshPromise = Promise.resolve();
    this._uninitialized = false;
    ChromeUtils.defineLazyGetter(
      this,
      "_currentSearchHostname",
      getShortHostnameForCurrentSearch
    );
    this.ranker = new RankShortcutsProvider();
    this.adsClient = null;

    this.dedupe = new Dedupe(this._dedupeKey);
    this.frecentCache = new lazy.LinksCache(
      lazy.NewTabUtils.activityStreamLinks,
      "getTopSites",
      CACHED_LINK_PROPS_TO_MIGRATE,
      (oldOptions, newOptions) =>
        // Refresh if no old options or requesting more items
        !(oldOptions.numItems >= newOptions.numItems)
    );
    this.frecencyBoostProvider = new FrecencyBoostProvider(this.frecentCache);
    this.pinnedCache = new lazy.LinksCache(
      lazy.NewTabUtils.pinnedLinks,
      "links",
      [...CACHED_LINK_PROPS_TO_MIGRATE, ...PINNED_FAVICON_PROPS_TO_MIGRATE]
    );
    lazy.PageThumbs.addExpirationFilter(this);
    this._nimbusChangeListener = this._nimbusChangeListener.bind(this);
  }

  _nimbusChangeListener(event, reason) {
    // The Nimbus API current doesn't specify the changed variable(s) in the
    // listener callback, so we have to refresh unconditionally on every change
    // of the `newtab` feature. It should be a manageable overhead given the
    // current update cadence (6 hours) of Nimbus.
    //
    // Skip the experiment and rollout loading reasons since this feature has
    // `isEarlyStartup` enabled, the feature variables are already available
    // before the experiment or rollout loads.
    if (
      !["feature-experiment-loaded", "feature-rollout-loaded"].includes(reason)
    ) {
      this._contile.refresh();
    }
  }

  init() {
    // If the feed was previously disabled PREFS_INITIAL_VALUES was never received
    this._readDefaults({ isStartup: true });
    this._contile.refresh();
    Services.obs.addObserver(this, "browser-search-engine-modified");
    Services.obs.addObserver(this, "browser-region-updated");
    Services.prefs.addObserver(REMOTE_SETTING_DEFAULTS_PREF, this);
    Services.prefs.addObserver(DEFAULT_SITES_OVERRIDE_PREF, this);
    Services.prefs.addObserver(DEFAULT_SITES_EXPERIMENTS_PREF_BRANCH, this);
    lazy.NimbusFeatures.newtab.onUpdate(this._nimbusChangeListener);
    this.frecencyBoostProvider.init();
    this._updateSponsoredScoring();
  }

  /**
   * Starts the sponsored top sites scoring when shortcuts and sponsored
   * shortcuts are both shown, and otherwise stops it and clears its data.
   */
  _updateSponsoredScoring() {
    const { values } = this.store.getState().Prefs;
    if (values[SHOWN_ON_NEWTAB_PREF] && values[SHOW_SPONSORED_PREF]) {
      // init() only initializes if initialization has not already occurred,
      // so it is safe to call while the scoring is running.
      SponsoredTopSitesScoreProvider.init(
        () => this.store.getState().Prefs.values.adsBackendConfig
      );
    } else {
      SponsoredTopSitesScoreProvider.clearData().catch(e =>
        lazy.log.error(`Failed to clear sponsored scoring data: ${e}`)
      );
      SponsoredTopSitesScoreProvider.uninit();
    }
  }

  uninit() {
    this._uninitialized = true;
    lazy.PageThumbs.removeExpirationFilter(this);
    Services.obs.removeObserver(this, "browser-search-engine-modified");
    Services.obs.removeObserver(this, "browser-region-updated");
    Services.prefs.removeObserver(REMOTE_SETTING_DEFAULTS_PREF, this);
    Services.prefs.removeObserver(DEFAULT_SITES_OVERRIDE_PREF, this);
    Services.prefs.removeObserver(DEFAULT_SITES_EXPERIMENTS_PREF_BRANCH, this);
    lazy.NimbusFeatures.newtab.offUpdate(this._nimbusChangeListener);
    this.frecencyBoostProvider.uninit();
    SponsoredTopSitesScoreProvider.uninit();
  }

  observe(subj, topic, data) {
    switch (topic) {
      case "browser-search-engine-modified":
        // We should update the current top sites if the search engine has been changed since
        // the search engine that gets filtered out of top sites has changed.
        // We also need to drop search shortcuts when their engine gets removed / hidden.
        if (
          data === "engine-default" &&
          this.store.getState().Prefs.values[FILTER_DEFAULT_SEARCH_PREF]
        ) {
          delete this._currentSearchHostname;
          this._currentSearchHostname = getShortHostnameForCurrentSearch();
          // Re-fetch sponsored tiles so the new default search engine is
          // sent to MARS as a block (and a substitute sponsor is returned),
          // and the cached Contile slate is rebuilt.
          this._contile.refresh();
        }
        this.refresh({ broadcast: true });
        break;
      case "browser-region-updated":
        this._readDefaults();
        break;
      case "nsPref:changed":
        if (
          data === REMOTE_SETTING_DEFAULTS_PREF ||
          data === DEFAULT_SITES_OVERRIDE_PREF ||
          data.startsWith(DEFAULT_SITES_EXPERIMENTS_PREF_BRANCH)
        ) {
          this._readDefaults();
        }
        break;
    }
  }

  _dedupeKey(site) {
    return site && site.hostname;
  }

  /**
   * _readContile - sets DEFAULT_TOP_SITES with contile
   */
  _readContile() {
    const contilePositions = this._contilePositions;

    let hasContileTiles = false;

    let contilePositionIndex = 0;
    // We need to loop through potential spocs and set their positions.
    // If we run out of spocs or positions, we stop.
    // First, we need to know which array is shortest. This is our exit condition.
    const minLength = Math.min(
      contilePositions.length,
      this._contile.sites.length
    );
    // Loop until we run out of spocs or positions.
    for (let i = 0; i < minLength; i++) {
      let site = this._contile.sites[i];
      let hostname = lazy.NewTabUtils.shortURL(site);
      let link = {
        isDefault: true,
        url: site.url,
        hostname,
        label: site.name,
        show_sponsored_label: hostname !== "yandex",
        sponsored_position: contilePositions[contilePositionIndex++],
        sponsored_click_url: site.click_url,
        sponsored_impression_url: site.impression_url,
        sponsored_tile_id: site.id,
        partner: SPONSORED_TILE_PARTNER_AMP,
        block_key: site.id,
        attribution: site.attribution,
      };
      if (site.image_url && site.image_size >= MIN_FAVICON_SIZE) {
        // Only use the image from Contile if it's hi-res, otherwise, fallback
        // to the built-in favicons.
        link.favicon = site.image_url;
        link.faviconSize = site.image_size;
      }
      DEFAULT_TOP_SITES.push(link);
    }
    hasContileTiles = contilePositionIndex > 0;
    // This is to catch where we receive 3 tiles but reduce to 2 early in the filtering, before blocked list applied.
    this._telemetryUtility.determineFilteredTilesAndSetToOversold(
      DEFAULT_TOP_SITES
    );

    return hasContileTiles;
  }

  /**
   * _readDefaults - sets DEFAULT_TOP_SITES
   *
   * @param {object} [options]
   * @param {boolean} [options.isStartup] Being called while TopSitesFeed is initting.
   * @param {string} [options.reason] Passed through to refresh().
   */
  async _readDefaults({ isStartup = false, reason } = {}) {
    this._useRemoteSetting = false;

    if (!Services.prefs.getBoolPref(REMOTE_SETTING_DEFAULTS_PREF)) {
      this.refreshDefaults(
        this.store.getState().Prefs.values[DEFAULT_SITES_PREF],
        { isStartup, reason }
      );
      return;
    }

    // Try using default top sites from enterprise policies or tests. The pref
    // is locked when set via enterprise policy. Tests have no default sites
    // unless they set them via this pref.
    if (
      Services.prefs.prefIsLocked(DEFAULT_SITES_OVERRIDE_PREF) ||
      Cu.isInAutomation
    ) {
      let sites = Services.prefs.getStringPref(DEFAULT_SITES_OVERRIDE_PREF, "");
      this.refreshDefaults(sites, { isStartup, reason });
      return;
    }

    // Clear out the array of any previous defaults.
    DEFAULT_TOP_SITES.length = 0;

    // Read defaults from contile.
    const contileEnabled = lazy.NimbusFeatures.newtab.getVariable(
      NIMBUS_VARIABLE_CONTILE_ENABLED
    );

    let hasContileTiles = false;

    if (contileEnabled) {
      hasContileTiles = this._readContile();
    }

    // Read defaults from remote settings.
    this._useRemoteSetting = true;
    let remoteSettingData = await this._getRemoteConfig();

    let sponsoredBlocklist;
    try {
      sponsoredBlocklist = JSON.parse(
        Services.prefs.getStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF, "[]")
      );
    } catch (e) {
      sponsoredBlocklist = [];
    }

    for (let siteData of remoteSettingData) {
      let hostname = lazy.NewTabUtils.shortURL(siteData);
      // Drop default sites when Contile already provided a sponsored one with
      // the same host name.
      if (
        contileEnabled &&
        DEFAULT_TOP_SITES.findIndex(site => site.hostname === hostname) > -1
      ) {
        continue;
      }
      // Also drop those sponsored sites that were blocked by the user before
      // with the same hostname.
      if (
        siteData.sponsored_position &&
        sponsoredBlocklist.includes(hostname)
      ) {
        continue;
      }
      let link = {
        isDefault: true,
        url: siteData.url,
        hostname,
      };
      if (siteData.url_urlbar_override) {
        link.url_urlbar = siteData.url_urlbar_override;
      }
      if (siteData.title) {
        link.label = siteData.title;
      }
      if (siteData.search_shortcut) {
        link = await this.topSiteToSearchTopSite(link);
      } else if (siteData.sponsored_position) {
        if (contileEnabled && hasContileTiles) {
          continue;
        }
        const {
          sponsored_position,
          sponsored_tile_id,
          sponsored_impression_url,
          sponsored_click_url,
          block_key,
        } = siteData;
        link = {
          sponsored_position,
          sponsored_tile_id,
          sponsored_impression_url,
          sponsored_click_url,
          block_key,
          show_sponsored_label: link.hostname !== "yandex",
          ...link,
        };
      }
      DEFAULT_TOP_SITES.push(link);
    }

    this.refresh({ broadcast: true, isStartup, reason });
  }

  /**
   * The 1-based tile positions Contile ads are configured to fill.
   *
   * Keep the number of positions in the array in sync with
   * CONTILE_MAX_NUM_SPONSORED. The Nimbus variable is 0-based, so we need to
   * add 1 to each of these. Also currently this does not work with SOV.
   */
  get _contilePositions() {
    const configured = lazy.NimbusFeatures.pocketNewtab
      .getVariable(NIMBUS_VARIABLE_CONTILE_POSITIONS)
      ?.split(",")
      .map(item => parseInt(item, 10) + 1)
      .filter(item => !Number.isNaN(item));
    return configured?.length ? configured : [1, 2];
  }

  /**
   * The maximum number of sponsored top sites that can be displayed.
   */
  get _maxSponsored() {
    return (
      lazy.NimbusFeatures.pocketNewtab.getVariable(
        NIMBUS_VARIABLE_MAX_SPONSORED
      ) ?? MAX_NUM_SPONSORED
    );
  }

  /**
   * The 1-based tile positions an ad is allowed to fill, whether or not an ad
   * was available for them. Positions past the display maximum can never be
   * filled, so they are not eligible.
   *
   * @returns {number[]} Ascending 1-based positions.
   */
  _adEligiblePositions() {
    const state = this.store.getState();
    if (
      !lazy.NimbusFeatures.newtab.getVariable(
        NIMBUS_VARIABLE_CONTILE_ENABLED
      ) ||
      !state.Prefs.values[SHOW_SPONSORED_PREF]
    ) {
      return [];
    }
    const { positions, ready } = state.TopSites.sov || {};
    const eligible =
      this._contile.sov && ready
        ? positions.map(allocation => allocation.position)
        : this._contilePositions;
    return eligible.slice(0, this._maxSponsored);
  }

  /**
   * @param {string} sites Comma separated default site URLs.
   * @param {object} [options]
   * @param {boolean} [options.isStartup] Being called while TopSitesFeed is initting.
   * @param {string} [options.reason] Passed through to refresh().
   */
  refreshDefaults(sites, { isStartup = false, reason } = {}) {
    // Clear out the array of any previous defaults
    DEFAULT_TOP_SITES.length = 0;

    // Add default sites if any based on the pref
    if (sites) {
      for (const url of sites.split(",")) {
        const site = {
          isDefault: true,
          url,
        };
        site.hostname = lazy.NewTabUtils.shortURL(site);
        DEFAULT_TOP_SITES.push(site);
      }
    }

    this.refresh({ broadcast: true, isStartup, reason });
  }

  async _getRemoteConfig(firstTime = true) {
    if (!this._remoteConfig) {
      this._remoteConfig = await lazy.RemoteSettings("top-sites");
      this._remoteConfig.on("sync", () => {
        this._readDefaults();
      });
    }

    let result = [];
    let failed = false;
    try {
      result = await this._remoteConfig.get();
    } catch (ex) {
      console.error(ex);
      failed = true;
    }
    if (!result.length) {
      console.error("Received empty top sites configuration!");
      failed = true;
    }
    // If we failed, or the result is empty, try loading from the local dump.
    if (firstTime && failed) {
      await this._remoteConfig.db.clear();
      // Now call this again.
      return this._getRemoteConfig(false);
    }

    // Sort sites based on the "order" attribute.
    result.sort((a, b) => a.order - b.order);

    result = result.filter(topsite => {
      // Filter by region.
      if (topsite.exclude_regions?.includes(lazy.Region.home)) {
        return false;
      }
      if (
        topsite.include_regions?.length &&
        !topsite.include_regions.includes(lazy.Region.home)
      ) {
        return false;
      }

      // Filter by locale.
      if (topsite.exclude_locales?.includes(Services.locale.appLocaleAsBCP47)) {
        return false;
      }
      if (
        topsite.include_locales?.length &&
        !topsite.include_locales.includes(Services.locale.appLocaleAsBCP47)
      ) {
        return false;
      }

      // Filter by experiment.
      // Exclude this top site if any of the specified experiments are running.
      if (
        topsite.exclude_experiments?.some(experimentID =>
          Services.prefs.getBoolPref(
            DEFAULT_SITES_EXPERIMENTS_PREF_BRANCH + experimentID,
            false
          )
        )
      ) {
        return false;
      }
      // Exclude this top site if none of the specified experiments are running.
      if (
        topsite.include_experiments?.length &&
        topsite.include_experiments.every(
          experimentID =>
            !Services.prefs.getBoolPref(
              DEFAULT_SITES_EXPERIMENTS_PREF_BRANCH + experimentID,
              false
            )
        )
      ) {
        return false;
      }

      return true;
    });

    return result;
  }

  filterForThumbnailExpiration(callback) {
    const { rows } = this.store.getState().TopSites;
    callback(
      rows.reduce((acc, site) => {
        acc.push(site.url);
        if (site.customScreenshotURL) {
          acc.push(site.customScreenshotURL);
        }
        return acc;
      }, [])
    );
  }

  /**
   * shouldFilterSearchTile - is default filtering enabled and does a given hostname match the user's default search engine?
   *
   * @param {string} hostname a top site hostname, such as "amazon" or "foo"
   * @returns {boolean}
   */
  shouldFilterSearchTile(hostname) {
    if (
      this.store.getState().Prefs.values[FILTER_DEFAULT_SEARCH_PREF] &&
      (SEARCH_FILTERS.includes(hostname) ||
        hostname === this._currentSearchHostname)
    ) {
      return true;
    }
    return false;
  }

  /**
   * If the search shortcuts experiment is running, insert search shortcuts if
   * needed.
   *
   * @param {Array} plainPinnedSites (from the pinnedSitesCache)
   * @returns {Promise<boolean>} Did we insert any search shortcuts?
   */
  async _maybeInsertSearchShortcuts(plainPinnedSites) {
    // Only insert shortcuts if the experiment is running
    if (this.store.getState().Prefs.values[SEARCH_SHORTCUTS_EXPERIMENT]) {
      // We don't want to insert shortcuts we've previously inserted
      const prevInsertedShortcuts = this.store
        .getState()
        .Prefs.values[SEARCH_SHORTCUTS_HAVE_PINNED_PREF].split(",")
        .filter(s => s); // Filter out empty strings
      const newInsertedShortcuts = [];

      let shouldPin = this._useRemoteSetting
        ? DEFAULT_TOP_SITES.filter(s => s.searchTopSite).map(s => s.hostname)
        : this.store
            .getState()
            .Prefs.values[SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF].split(",");
      shouldPin = shouldPin
        .map(getSearchProvider)
        .filter(s => s && s.shortURL !== this._currentSearchHostname);

      // If we've previously inserted all search shortcuts return early
      if (
        shouldPin.every(shortcut =>
          prevInsertedShortcuts.includes(shortcut.shortURL)
        )
      ) {
        return false;
      }

      const numberOfSlots = getTopSitesCount(
        this.store.getState().Prefs.values
      );

      // The plainPinnedSites array is populated with pinned sites at their
      // respective indices, and null everywhere else, but is not always the
      // right length
      const emptySlots = Math.max(numberOfSlots - plainPinnedSites.length, 0);
      const pinnedSites = [...plainPinnedSites].concat(
        Array(emptySlots).fill(null)
      );

      const tryToInsertSearchShortcut = async shortcut => {
        const nextAvailable = pinnedSites.indexOf(null);
        // Only add a search shortcut if the site isn't already pinned, we
        // haven't previously inserted it, there's space to pin it, and the
        // search engine is available in Firefox
        if (
          !pinnedSites.find(
            s => s && lazy.NewTabUtils.shortURL(s) === shortcut.shortURL
          ) &&
          !prevInsertedShortcuts.includes(shortcut.shortURL) &&
          nextAvailable > -1 &&
          (await checkHasSearchEngine(shortcut.keyword))
        ) {
          const site = await this.topSiteToSearchTopSite({ url: shortcut.url });
          this._pinSiteAt(site, nextAvailable);
          pinnedSites[nextAvailable] = site;
          newInsertedShortcuts.push(shortcut.shortURL);
        }
      };

      for (let shortcut of shouldPin) {
        await tryToInsertSearchShortcut(shortcut);
      }

      if (newInsertedShortcuts.length) {
        this.store.dispatch(
          ac.SetPref(
            SEARCH_SHORTCUTS_HAVE_PINNED_PREF,
            prevInsertedShortcuts.concat(newInsertedShortcuts).join(",")
          )
        );
        return true;
      }
    }

    return false;
  }

  /**
   * This thin wrapper around global.fetch makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   *
   * @param {Parameters<typeof fetch>} args
   */
  fetch(...args) {
    return fetch(...args);
  }

  /**
   * Fetch topsites spocs that are frecency boosted.
   *
   * @returns {Promise<Array>} An array of sponsored tile objects.
   */
  async fetchFrecencyBoostedSpocs() {
    let candidates = [];
    if (
      this._contile.sovEnabled() &&
      this.store.getState().Prefs.values[SHOW_SPONSORED_PREF]
    ) {
      const { values } = this.store.getState().Prefs;
      const numItems = values?.trainhopConfig?.sov?.numItems;
      const randomSponsorEnabled = values?.trainhopConfig?.sov?.random_sponsor;

      if (!randomSponsorEnabled) {
        candidates = await this.frecencyBoostProvider.fetch(numItems);
        // If we have a matched set of candidates,
        // we can check if it's an exposure event.
        if (candidates.length) {
          this.frecencyBoostedSpocsExposureEvent();
        }
      }

      if (!candidates.length) {
        const randomTile =
          await this.frecencyBoostProvider.retrieveRandomFrecencyTile();
        if (randomTile) {
          candidates = [randomTile];
        }
      }
    }
    return candidates;
  }

  /**
   * Updates frecency boosted topsites spocs cache.
   */
  async updateFrecencyBoostedSpocs() {
    const { values } = this.store.getState().Prefs;
    const numItems = values?.trainhopConfig?.sov?.numItems;
    await this.frecencyBoostProvider.update(numItems);
  }

  /**
   * Flip exposure event pref,
   * if the user is in a SOV experiment,
   * for both control and treatment,
   * and had frecency boosted spocs because of it.
   */
  frecencyBoostedSpocsExposureEvent() {
    const { values } = this.store.getState().Prefs;
    const trainhopSovEnabled = values?.trainhopConfig?.sov?.enabled;

    if (trainhopSovEnabled) {
      this.store.dispatch(ac.SetPref(PREF_SOV_FRECENCY_EXPOSURE, true));
    }
  }

  /**
   * Fetch topsites spocs from the DiscoveryStream feed.
   *
   * @returns {Array} An array of sponsored tile objects.
   */
  fetchDiscoveryStreamSpocs() {
    let sponsored = [];
    const { DiscoveryStream } = this.store.getState();
    if (DiscoveryStream) {
      const discoveryStreamSpocs =
        DiscoveryStream.spocs.data["sponsored-topsites"]?.items || [];
      // Find the first component of a type and remove it from layout
      const findSponsoredTopsitesPositions = name => {
        for (const row of DiscoveryStream.layout) {
          for (const component of row.components) {
            if (component.placement?.name === name) {
              return component.spocs.positions;
            }
          }
        }
        return null;
      };

      // Get positions from layout for now. This could be improved if we store position data in state.
      const discoveryStreamSpocPositions =
        findSponsoredTopsitesPositions("sponsored-topsites");

      if (discoveryStreamSpocPositions?.length) {
        function reformatImageURL(url, width, height) {
          // Change the image URL to request a size tailored for the parent container width
          // Also: force WebP, quality 75, no upscaling, no EXIF data
          // Uses Thumbor: https://thumbor.readthedocs.io/en/latest/usage.html
          // For now we wrap this in single quotes because this is being used in a url() css rule, and otherwise would cause a parsing error.
          return `'https://img-getpocket.cdn.mozilla.net/${width}x${height}/filters:format(webp):quality(75):no_upscale():strip_exif()/${encodeURIComponent(
            url
          )}'`;
        }

        // We need to loop through potential spocs and set their positions.
        // If we run out of spocs or positions, we stop.
        // First, we need to know which array is shortest. This is our exit condition.
        const minLength = Math.min(
          discoveryStreamSpocPositions.length,
          discoveryStreamSpocs.length
        );
        // Loop until we run out of spocs or positions.
        for (let i = 0; i < minLength; i++) {
          const positionIndex = discoveryStreamSpocPositions[i].index;
          const spoc = discoveryStreamSpocs[i];
          const link = {
            favicon: reformatImageURL(spoc.raw_image_src, 96, 96),
            faviconSize: 96,
            type: "SPOC",
            label: spoc.title || spoc.sponsor,
            title: spoc.title || spoc.sponsor,
            url: spoc.url,
            flightId: spoc.flight_id,
            id: spoc.id,
            guid: spoc.id,
            shim: spoc.shim,
            // For now we are assuming position based on intended position.
            // Actual position can shift based on other content.
            // We send the intended position in the ping.
            pos: positionIndex,
            // Set this so that SPOC topsites won't be shown in the URL bar.
            // See Bug 1822027. Note that `sponsored_position` is 1-based.
            sponsored_position: positionIndex + 1,
            // This is used for topsites deduping.
            hostname: lazy.NewTabUtils.shortURL({ url: spoc.url }),
            partner: SPONSORED_TILE_PARTNER_MOZ_SALES,
          };
          sponsored.push(link);
        }
      }
    }
    return sponsored;
  }

  // eslint-disable-next-line max-statements
  async getLinksWithDefaults(
    isStartup = false,
    refreshId = null,
    reason,
    onRankedSnapshot
  ) {
    const prefValues = this.store.getState().Prefs.values;
    // switch on top_sites thompson sampling experiment
    const overSampleMultiplier =
      prefValues?.trainhopConfig?.smartShortcuts?.over_sample_multiplier ??
      OVERSAMPLE_MULTIPLIER;
    const numFetch =
      (smartshortcutsEnabled(this.store.getState().Prefs.values)
        ? overSampleMultiplier
        : 1) * getTopSitesCount(prefValues);
    const numItems = getTopSitesCount(prefValues);
    const searchShortcutsExperiment = prefValues[SEARCH_SHORTCUTS_EXPERIMENT];
    // We must wait for search services to initialize in order to access default
    // search engine properties without triggering a synchronous initialization
    try {
      await lazy.SearchService.init();
    } catch {
      // We continue anyway because we want the user to see their sponsored,
      // saved, or visited shortcut tiles even if search engines are not
      // available.
    }

    // Get all frecent sites from history.
    let frecent = [];
    const cache = await this.frecentCache.request({
      // We need to overquery due to the top 5 alexa search + default search possibly being removed
      numItems: numFetch + SEARCH_FILTERS.length + 1,
      // 30 days ago, 5 visits. The threshold avoids one non-typed visit from
      // immediately being included in recent history to mimic the original
      // threshold which aimed to prevent first-run visits from being included in
      // Top Sites.
      topsiteFrecency: lazy.PlacesUtils.history.pageFrecencyThreshold(
        30,
        5,
        false
      ),
    });
    for (let link of cache) {
      const hostname = lazy.NewTabUtils.shortURL(link);
      if (!this.shouldFilterSearchTile(hostname)) {
        frecent.push({
          ...(searchShortcutsExperiment
            ? await this.topSiteToSearchTopSite(link)
            : link),
          hostname,
        });
        // LinksCache can return the previous cached result
        // if it's equal to or greater than the requested amount.
        // In this case we can just take what we need.
        if (frecent.length >= numFetch) {
          break;
        }
      }
    }

    // Get defaults.
    let contileSponsored = [];
    let notBlockedDefaultSites = [];

    for (let link of DEFAULT_TOP_SITES) {
      // For sponsored Yandex links, default filtering is reversed: we only
      // show them if Yandex is the default search engine.
      if (link.sponsored_position && link.hostname === "yandex") {
        if (link.hostname !== this._currentSearchHostname) {
          continue;
        }
      } else if (
        !link.sponsored_position &&
        this.shouldFilterSearchTile(link.hostname)
      ) {
        continue;
      }
      // Drop blocked default sites.
      if (
        lazy.NewTabUtils.blockedLinks.isBlocked({
          url: link.url,
        })
      ) {
        continue;
      }
      // If we've previously blocked a search shortcut, remove the default top site
      // that matches the hostname
      const searchProvider = getSearchProvider(lazy.NewTabUtils.shortURL(link));
      if (
        searchProvider &&
        lazy.NewTabUtils.blockedLinks.isBlocked({ url: searchProvider.url })
      ) {
        continue;
      }
      if (link.sponsored_position) {
        if (!prefValues[SHOW_SPONSORED_PREF]) {
          continue;
        }
        contileSponsored[link.sponsored_position - 1] = link;

        // Unpin search shortcut if present for the sponsored link to be shown
        // instead.
        this._unpinSearchShortcut(link.hostname);
      } else {
        notBlockedDefaultSites.push(
          searchShortcutsExperiment
            ? await this.topSiteToSearchTopSite(link)
            : link
        );
      }
    }
    this._telemetryUtility.determineFilteredTilesAndSetToDismissed(
      contileSponsored
    );

    const discoverySponsored = this.fetchDiscoveryStreamSpocs();
    const frecencyBoostedSponsored = await this.fetchFrecencyBoostedSpocs();
    this._telemetryUtility.setTiles(discoverySponsored);

    // Get pinned links augmented with desired properties
    let plainPinned = await this.pinnedCache.request();

    // Insert search shortcuts if we need to.
    // _maybeInsertSearchShortcuts returns true if any search shortcuts are
    // inserted, meaning we need to expire and refresh the pinnedCache
    if (await this._maybeInsertSearchShortcuts(plainPinned)) {
      this.pinnedCache.expire();
      plainPinned = await this.pinnedCache.request();
    }

    const pinned = await Promise.all(
      plainPinned.map(async link => {
        if (!link) {
          return link;
        }

        // Drop pinned search shortcuts when their engine has been removed / hidden.
        if (link.searchTopSite) {
          const searchProvider = getSearchProvider(
            lazy.NewTabUtils.shortURL(link)
          );
          if (
            !searchProvider ||
            !(await checkHasSearchEngine(searchProvider.keyword))
          ) {
            return null;
          }
        }

        // Copy all properties from a frecent link and add more
        const finder = other => other.url === link.url;

        // Remove frecent link's screenshot if pinned link has a custom one
        const frecentSite = frecent.find(finder);
        if (frecentSite && link.customScreenshotURL) {
          delete frecentSite.screenshot;
        }
        // If the link is a frecent site, do not copy over 'isDefault', else check
        // if the site is a default site
        const copy = Object.assign(
          {},
          frecentSite || { isDefault: !!notBlockedDefaultSites.find(finder) },
          link,
          { hostname: lazy.NewTabUtils.shortURL(link) },
          { searchTopSite: !!link.searchTopSite }
        );

        // Add in favicons if we don't already have it
        if (!copy.favicon) {
          try {
            lazy.NewTabUtils.activityStreamProvider._faviconBytesToDataURI(
              await lazy.NewTabUtils.activityStreamProvider._addFavicons([copy])
            );

            for (const prop of PINNED_FAVICON_PROPS_TO_MIGRATE) {
              copy.__sharedCache.updateLink(prop, copy[prop]);
            }
          } catch (e) {
            // Some issue with favicon, so just continue without one
          }
        }

        return copy;
      })
    );

    Glean.topsites.pinnedCount.set(pinned.filter(Boolean).length);

    // Remove any duplicates from frecent and default sites
    const [
      ,
      dedupedContileSponsored,
      dedupedDiscoverySponsored,
      dedupedFrecent,
      dedupedFrecencyBoostedSponsored,
      dedupedDefaults,
    ] = this.dedupe.group(
      pinned,
      contileSponsored,
      discoverySponsored,
      frecent,
      frecencyBoostedSponsored,
      notBlockedDefaultSites
    );

    const dedupedUnpinned = [...dedupedFrecent, ...dedupedDefaults];

    const dedupedSponsored = this._mergeSponsoredLinks({
      [SPONSORED_TILE_PARTNER_AMP]: dedupedContileSponsored,
      [SPONSORED_TILE_PARTNER_MOZ_SALES]: dedupedDiscoverySponsored,
      [SPONSORED_TILE_PARTNER_FREC_BOOST]: dedupedFrecencyBoostedSponsored,
    });

    this._maybeCapSponsoredLinks(dedupedSponsored);

    // This will set all extra tiles to oversold, including moz-sales.
    this._telemetryUtility.determineFilteredTilesAndSetToOversold(
      dedupedSponsored
    );

    // Remove adult sites if we need to
    const checkedAdult = lazy.FilterAdult.filter(dedupedUnpinned);

    // Sample topsites via thompson sampling, if in experiment
    let sampledSites;
    if (smartshortcutsEnabled(this.store.getState().Prefs.values)) {
      // The ranker only orders unpinned, unsponsored sites, so the frozen part
      // of its order should match the slots those sites can actually fill.
      // This is an upper bound on what's visible: narrow windows show fewer
      // tiles per row, which the feed can't see.
      const organicSlots = countOrganicSlots(
        numItems,
        pinned,
        dedupedSponsored
      );
      try {
        sampledSites = await this.ranker.rankTopSites(
          checkedAdult,
          prefValues,
          { isStartup, reason, numItems: organicSlots, onRankedSnapshot }
        );
      } catch (error) {
        lazy.log.warn(`Smart shortcuts ranking failed: ${error.message}`);
        sampledSites = checkedAdult;
      }
    } else {
      sampledSites = checkedAdult;
    }

    // Insert the original pinned sites into the deduped frecent and defaults.
    let withPinned = insertPinned(sampledSites, pinned);
    if (this._groupedPinsEnabled) {
      // Grouped: pinned tiles form a contiguous block at the front, ahead of
      // the sampled frecent/default sites.
      withPinned = [
        ...pinned.filter(Boolean).map(link => ({ ...link, isPinned: true })),
        ...sampledSites,
      ];
    }

    // Insert sponsored sites at their desired position.
    dedupedSponsored.forEach(link => {
      if (!link) {
        return;
      }
      let index = link.sponsored_position - 1;
      if (index >= withPinned.length) {
        withPinned[index] = link;
      } else if (withPinned[index]?.sponsored_position) {
        // We currently want DiscoveryStream spocs to replace existing spocs.
        withPinned[index] = link;
      } else {
        withPinned.splice(index, 0, link);
      }
    });
    // Remove excess items after we inserted sponsored ones.
    withPinned = withPinned.slice(0, numItems);

    // These positions are ad-eligible even when no ad was available to fill
    // them, so flag whichever tile ended up in each one for telemetry.
    // Clear any stale references as well.
    const adEligible = new Set(this._adEligiblePositions());
    withPinned.forEach((link, index) => {
      if (!link) {
        return;
      }
      if (adEligible.has(index + 1)) {
        link.is_ad_eligible_position = true;
      } else {
        delete link.is_ad_eligible_position;
      }
    });

    // Now, get a tippy top icon, a rich icon, or screenshot for every item
    for (const link of withPinned) {
      if (link) {
        // If there is a custom screenshot this is the only image we display
        if (link.customScreenshotURL) {
          this._fetchScreenshot(link, link.customScreenshotURL, isStartup);
        } else if (link.searchTopSite && !link.isDefault) {
          this._tippyTopProvider.processSite(link);
        } else {
          this._fetchIcon(link, isStartup);
        }

        // Remove internal properties that might be updated after dispatch
        delete link.__sharedCache;

        // Indicate that these links should get a frecency bonus when clicked
        link.typedBonus = true;
      }
    }

    if (refreshId === null || refreshId === this._refreshGeneration) {
      this._linksWithDefaults = withPinned;
      this._telemetryUtility.finalizeNewtabPingFields(dedupedSponsored);
    }
    return withPinned;
  }

  /**
   * Cap sponsored links if they're more than the specified maximum.
   *
   * @param {Array} links An array of sponsored links. Capping will be performed in-place.
   */
  _maybeCapSponsoredLinks(links) {
    // Set maximum sponsored top sites
    const maxSponsored = this._maxSponsored;
    if (links.length > maxSponsored) {
      links.length = maxSponsored;
    }
  }

  /**
   * Merge sponsored links from all the partners using SOV if present.
   * For each tile position, the user is assigned to one partner via stable sampling.
   * If the chosen partner doesn't have a tile to serve, another tile from a different
   * partner is used as the replacement.
   *
   * @param {object} sponsoredLinks An object with sponsored links from all the partners.
   * @returns {Array} An array of merged sponsored links.
   */
  _mergeSponsoredLinks(sponsoredLinks) {
    const { positions: allocatedPositions, ready: sovReady } =
      this.store.getState().TopSites.sov || {};

    // AMP links might have empty slots, remove them as SOV doesn't need those.
    if (sponsoredLinks[SPONSORED_TILE_PARTNER_AMP]) {
      sponsoredLinks[SPONSORED_TILE_PARTNER_AMP] =
        sponsoredLinks[SPONSORED_TILE_PARTNER_AMP].filter(Boolean);
    }

    if (!this._contile.sov || !sovReady) {
      return Object.values(sponsoredLinks).flat();
    }

    let sponsored = [];

    for (const allocation of allocatedPositions) {
      let link = null;
      const { assignedPartner } = allocation;
      if (assignedPartner) {
        const candidates = sponsoredLinks[assignedPartner] || [];
        while (candidates.length) {
          // Unknown partners are allowed so that new partners can be added to Shepherd
          // sooner without waiting for client changes.
          const candidate = candidates?.shift();
          if (!candidate) {
            continue;
          }
          const candLabel = candidate.label?.trim().toLowerCase();
          // Deduplicate against sponsored links that have already been added.
          if (candLabel) {
            const duplicateSponsor = sponsored.some(
              s => s.label?.trim().toLowerCase() === candLabel
            );
            if (duplicateSponsor) {
              continue; // skip this candidate, try next
            }
          }
          link = candidate;
          break;
        }
      }

      if (!link) {
        // If the chosen partner doesn't have a tile for this position, choose any
        // one from another group. For simplicity, we do _not_ do resampling here
        // against the remaining partners.
        for (const partner of SPONSORED_TILE_PARTNERS) {
          if (
            partner === assignedPartner ||
            sponsoredLinks[partner].length === 0
          ) {
            continue;
          }
          link = sponsoredLinks[partner].shift();
          break;
        }

        if (!link) {
          // No more links to be added across all the partners, just return.
          return sponsored;
        }
      }

      // Update the position fields. Note that postion is also 1-based in SOV.
      link.sponsored_position = allocation.position;
      if (link.pos !== undefined) {
        // Pocket `pos` is 0-based.
        link.pos = allocation.position - 1;
      }
      sponsored.push(link);
    }

    // add the remaining contile sponsoredLinks when nimbus variable present
    if (
      lazy.NimbusFeatures.pocketNewtab.getVariable(
        NIMBUS_VARIABLE_CONTILE_MAX_NUM_SPONSORED
      )
    ) {
      return sponsored.concat(sponsoredLinks[SPONSORED_TILE_PARTNER_AMP]);
    }

    return sponsored;
  }

  /**
   * Refresh the top sites data for content.
   *
   * @param {object} [options]
   * @param {boolean} [options.broadcast] Should the update be broadcasted.
   * @param {boolean} [options.isStartup] Being called while TopSitesFeed is initting.
   * @param {string} [options.reason] What triggered the refresh. Smart
   *   shortcuts only skip an organic rerank for "SYSTEM_TICK" and
   *   "SPONSORED_TILES_UPDATED"; any other or missing reason reranks.
   */
  async refresh(options = {}) {
    if (this._uninitialized) {
      // The store has already dropped this feed, and may hold a newer instance.
      // An in-flight Contile fetch can still get here, through _readDefaults().
      return;
    }
    if (!this._startedUp && !options.isStartup) {
      // Initial refresh still pending.
      return;
    }
    this._startedUp = true;

    const refreshId = ++this._refreshGeneration;
    // Only the newest refresh dispatches, so a refresh started while a
    // broadcasting one is in flight has to broadcast on its behalf.
    this._broadcastPending ||= !!options.broadcast;
    const refreshPromise = (async () => {
      if (!this._tippyTopProvider.initialized) {
        await this._tippyTopProvider.init();
      }

      let rankedSnapshot;
      const links = await this.getLinksWithDefaults(
        options.isStartup,
        refreshId,
        options.reason,
        snapshot => {
          rankedSnapshot = snapshot;
        }
      );
      if (this._uninitialized || refreshId !== this._refreshGeneration) {
        return;
      }

      const newAction = { type: at.TOP_SITES_UPDATED, data: { links } };

      if (options.isStartup) {
        newAction.meta = {
          isStartup: true,
        };
      }

      if (this._broadcastPending) {
        this._broadcastPending = false;
        // Broadcast an update to all open content pages
        this.store.dispatch(ac.BroadcastToContent(newAction));
      } else {
        // Don't broadcast only update the state and update the preloaded tab.
        this.store.dispatch(ac.AlsoToPreloaded(newAction));
      }
      if (rankedSnapshot) {
        void this.ranker.commitSnapshot(rankedSnapshot).catch(error => {
          lazy.log.warn(
            `Smart shortcuts snapshot persistence failed: ${error.message}`
          );
        });
      }
    })();

    this._latestRefreshPromise = refreshPromise.catch(() => {});
    await refreshPromise;
  }

  // Allocate ad positions to partners based on SOV via stable randomization.
  async allocatePositions() {
    // If the fetch to get sov fails for whatever reason, we can just return here.
    // Code that uses this falls back to flattening allocations instead if this has failed.
    if (!this._contile.sov) {
      return;
    }

    // This sample input should ensure we return the same result for this allocation,
    // even if called from other parts of the code.
    let contextId = await lazy.ContextId.request();
    const sampleInput = `${contextId}-${this._contile.sov.name}`;
    const allocatedPositions = [];
    for (const allocation of this._contile.sov.allocations) {
      const allocatedPosition = {
        position: allocation.position,
      };
      allocatedPositions.push(allocatedPosition);
      const ratios = allocation.allocation.map(alloc => alloc.percentage);
      if (ratios.length) {
        const index = await lazy.Sampling.ratioSample(sampleInput, ratios);
        allocatedPosition.assignedPartner =
          allocation.allocation[index].partner;
      }
    }

    this.store.dispatch(
      ac.OnlyToMain({
        type: at.SOV_UPDATED,
        data: {
          ready: !!allocatedPositions.length,
          positions: allocatedPositions,
        },
      })
    );
  }

  async updateCustomSearchShortcuts(isStartup = false) {
    if (!this.store.getState().Prefs.values[SEARCH_SHORTCUTS_EXPERIMENT]) {
      return;
    }

    if (!this._tippyTopProvider.initialized) {
      await this._tippyTopProvider.init();
    }

    // Populate the state with available search shortcuts
    let searchShortcuts = [];
    for (const engine of await lazy.SearchService.getAppProvidedEngines()) {
      const shortcut = CUSTOM_SEARCH_SHORTCUTS.find(s =>
        engine.aliases.includes(s.keyword)
      );
      if (shortcut) {
        let clone = { ...shortcut };
        this._tippyTopProvider.processSite(clone);
        searchShortcuts.push(clone);
      }
    }

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.UPDATE_SEARCH_SHORTCUTS,
        data: { searchShortcuts },
        meta: {
          isStartup,
        },
      })
    );
  }

  async topSiteToSearchTopSite(site) {
    const searchProvider = getSearchProvider(lazy.NewTabUtils.shortURL(site));
    if (
      !searchProvider ||
      !(await checkHasSearchEngine(searchProvider.keyword))
    ) {
      return site;
    }
    return {
      ...site,
      searchTopSite: true,
      label: searchProvider.keyword,
    };
  }

  /**
   * Get an image for the link preferring tippy top, rich favicon, screenshots.
   */
  async _fetchIcon(link, isStartup = false) {
    // Nothing to do if we already have a rich icon from the page
    if (link.favicon && link.faviconSize >= MIN_FAVICON_SIZE) {
      return;
    }

    // Nothing more to do if we can use a default tippy top icon
    this._tippyTopProvider.processSite(link);
    if (link.tippyTopIcon) {
      return;
    }

    // Make a request for a better icon
    this._requestRichIcon(link.url);

    // Also request a screenshot if we don't have one yet
    await this._fetchScreenshot(link, link.url, isStartup);
  }

  /**
   * Fetch, cache and broadcast a screenshot for a specific topsite.
   *
   * @param link cached topsite object
   * @param url where to fetch the image from
   * @param isStartup Whether the screenshot is fetched while initting TopSitesFeed.
   */
  async _fetchScreenshot(link, url, isStartup = false) {
    // We shouldn't bother caching screenshots if they won't be shown.
    if (
      link.screenshot ||
      !this.store.getState().Prefs.values[SHOWN_ON_NEWTAB_PREF]
    ) {
      return;
    }
    await lazy.Screenshots.maybeCacheScreenshot(
      link,
      url,
      "screenshot",
      screenshot =>
        this.store.dispatch(
          ac.BroadcastToContent({
            data: { screenshot, url: link.url },
            type: at.SCREENSHOT_UPDATED,
            meta: {
              isStartup,
            },
          })
        )
    );
  }

  /**
   * Dispatch screenshot preview to target or notify if request failed.
   *
   * @param customScreenshotURL {string} The URL used to capture the screenshot
   * @param target {string} Id of content process where to dispatch the result
   */
  async getScreenshotPreview(url, target) {
    const preview = (await lazy.Screenshots.getScreenshotForURL(url)) || "";
    this.store.dispatch(
      ac.OnlyToOneContent(
        {
          data: { url, preview },
          type: at.PREVIEW_RESPONSE,
        },
        target
      )
    );
  }

  _requestRichIcon(url) {
    this.store.dispatch({
      type: at.RICH_ICON_MISSING,
      data: { url },
    });
  }

  /**
   * Inform others that top sites data has been updated due to pinned changes.
   */
  _broadcastPinnedSitesUpdated() {
    // Pinned data changed, so make sure we get latest
    this.pinnedCache.expire();

    // Refresh to update pinned sites with screenshots, trigger deduping, etc.
    this.refresh({ broadcast: true });
  }

  get _groupedPins() {
    // Drop the gaps so the group is dense.
    return lazy.NewTabUtils.pinnedLinks.links.filter(Boolean);
  }

  /**
   * Whether grouped-pin display + restricted drag-and-drop is on. Read live so a
   * pref flip takes effect immediately, and so this and the content process
   * (which reads the same pref from Redux) agree on what a drop index means.
   */
  get _groupedPinsEnabled() {
    return !!this.store.getState().Prefs.values[GROUPED_PINS_PREF];
  }

  _saveGroupedPins(pins) {
    const { pinnedLinks } = lazy.NewTabUtils;
    // Lay the group order back onto the existing hole pattern: the k-th pin takes
    // the k-th occupied slot, so the sparse layout's gaps stay put for the
    // classic renderer and other pinnedLinks consumers. Extra pins append
    // densely; trailing holes are trimmed.
    // PinnedLinks has no public "set whole array" method (only pin/unpin/replace),
    // so we write _links directly to lay down this exact sparse layout in one shot
    // rather than thrashing through per-item pin/unpin calls.
    const occupied = pinnedLinks.links.flatMap((v, i) => (v ? [i] : []));
    const next = [];
    pins.forEach((pin, k) => {
      next[k < occupied.length ? occupied[k] : next.length] = pin;
    });
    while (next.length && !next[next.length - 1]) {
      next.length -= 1;
    }
    pinnedLinks._links = next;
    pinnedLinks.save();
  }

  /**
   * Pin a site at a specific position saving only the desired keys.
   *
   * @param {object} site
   * @param {string} [site.customScreenshotURL] User set URL of preview image for site
   * @param {string} [site.label] User set string of custom site name
   * @param {string} site.url
   * @param {boolean} [site.searchTopSite]
   * @param {number} index
   */
  async _pinSiteAt({ customScreenshotURL, label, url, searchTopSite }, index) {
    if (this._groupedPinsEnabled) {
      // A context-menu / search pin appends to the group; the slot index only
      // applies to the classic scattered layout.
      await this._pinSiteAtGrouped({
        customScreenshotURL,
        label,
        url,
        searchTopSite,
      });
      return;
    }
    const toPin = { url };
    if (label) {
      toPin.label = label;
    }
    if (customScreenshotURL) {
      toPin.customScreenshotURL = customScreenshotURL;
    }
    if (searchTopSite) {
      toPin.searchTopSite = searchTopSite;
    }
    lazy.NewTabUtils.pinnedLinks.pin(toPin, index);

    await this._clearLinkCustomScreenshot({ customScreenshotURL, url });
  }

  /**
   * Grouped variant of `_pinSiteAt`: `index` is a position within the pinned
   * group (the k-th pin), not an absolute slot. Removes any existing entry for
   * the site and splices it back in at the group position. Omitting `index`
   * keeps an already-pinned site's position (e.g. an edit) and appends an
   * otherwise-new pin.
   */
  async _pinSiteAtGrouped(
    { customScreenshotURL, label, url, searchTopSite },
    index
  ) {
    const toPin = { url };
    if (label) {
      toPin.label = label;
    }
    if (customScreenshotURL) {
      toPin.customScreenshotURL = customScreenshotURL;
    }
    if (searchTopSite) {
      toPin.searchTopSite = searchTopSite;
    }

    const existingIndex = this._groupedPins.findIndex(p => p.url === url);
    const pins = this._groupedPins.filter(p => p.url !== url);
    if (index !== undefined) {
      // `index` is the group position to drop at. The dragged tile is already
      // removed above, and the frontend sends the slot in that post-removal
      // space, so it splices straight in.
      pins.splice(Math.min(index, pins.length), 0, toPin);
    } else if (existingIndex !== -1) {
      // Re-pinning a site already in the group (e.g. an edit) keeps its
      // position instead of jumping to the end.
      pins.splice(existingIndex, 0, toPin);
    } else {
      // A fresh pin (context-menu / search / add button) appends to the group.
      pins.push(toPin);
      // Grouped-pins growth (classic mode grows in pin() instead). Adding to the
      // group pushes the last frecency tile off the visible grid. If the grid is
      // already full of pins there's nothing to push off, so grow by a row instead.
      const prefs = this.store.getState().Prefs.values;
      const { rows } = this.store.getState().TopSites;
      if (
        rows[getTopSitesCount(prefs) - 1]?.isPinned &&
        prefs[ROWS_PREF] < TOP_SITES_MAX_ROWS
      ) {
        this.store.dispatch(ac.SetPref(ROWS_PREF, prefs[ROWS_PREF] + 1));
      }
    }
    this._saveGroupedPins(pins);

    await this._clearLinkCustomScreenshot({ customScreenshotURL, url });
  }

  async _clearLinkCustomScreenshot(site) {
    // If screenshot url changed or was removed we need to update the cached link obj
    if (site.customScreenshotURL !== undefined) {
      const pinned = await this.pinnedCache.request();
      const link = pinned.find(pin => pin && pin.url === site.url);
      if (link && link.customScreenshotURL !== site.customScreenshotURL) {
        link.__sharedCache.updateLink("screenshot", undefined);
      }
    }
  }

  /**
   * Handle a pin action of a site to a position.
   */
  async pin(action) {
    let { site, index } = action.data;
    // Grow keys on the requested slot, not the sponsor-adjusted pin position.
    const requestedIndex = index;
    index = this._adjustPinIndexForSponsoredLinks(site, index);
    // If valid index provided, pin at that position
    if (index >= 0) {
      const currentSite = this._linksWithDefaults?.[requestedIndex];
      if (
        this._groupedPinsEnabled &&
        currentSite?.isPinned &&
        currentSite.url !== site.url
      ) {
        // Bug 2073040: an edit changed a pin's URL, so swap it in place.
        lazy.NewTabUtils.pinnedLinks.replace(currentSite.url, {
          url: site.url,
        });
      }
      const prefs = this.store.getState().Prefs.values;
      // Classic add button on a full row: replace the last visible frecency tile,
      // matching how a grouped pin pushes the trailing frecency tile off the grid.
      // Fall back to growing a row only when there's no organic tile to replace.
      const count = getTopSitesCount(prefs);
      const pinsPastGrid = !this._groupedPinsEnabled && requestedIndex >= count;
      let evictIndex = -1;
      if (pinsPastGrid) {
        const { rows } = this.store.getState().TopSites;
        for (let i = count - 1; i >= 0; i--) {
          const link = rows[i];
          if (link && !link.isPinned && !link.sponsored_position) {
            evictIndex = i;
            break;
          }
        }
      }
      if (evictIndex >= 0) {
        await this._pinSiteAt(
          site,
          this._adjustPinIndexForSponsoredLinks(site, evictIndex)
        );
      } else {
        await this._pinSiteAt(site, index);
        if (pinsPastGrid && prefs[ROWS_PREF] < TOP_SITES_MAX_ROWS) {
          this.store.dispatch(ac.SetPref(ROWS_PREF, prefs[ROWS_PREF] + 1));
        }
      }
      this._broadcastPinnedSitesUpdated();
    } else {
      // Bug 1458658. If the top site is being pinned from an 'Add a Top Site' option,
      // then we want to make sure to unblock that link if it has previously been
      // blocked. We know if the site has been added because the index will be -1.
      if (index === -1) {
        lazy.NewTabUtils.blockedLinks.unblock({ url: site.url });
        this.frecentCache.expire();
      }
      this.insert(action);
    }
  }

  /**
   * Handle an unpin action of a site.
   */
  unpin(action) {
    const { site } = action.data;
    lazy.NewTabUtils.pinnedLinks.unpin(site);
    this._broadcastPinnedSitesUpdated();
  }

  unpinAllSearchShortcuts() {
    Services.prefs.clearUserPref(
      `browser.newtabpage.activity-stream.${SEARCH_SHORTCUTS_HAVE_PINNED_PREF}`
    );
    for (let pinnedLink of lazy.NewTabUtils.pinnedLinks.links) {
      if (pinnedLink && pinnedLink.searchTopSite) {
        lazy.NewTabUtils.pinnedLinks.unpin(pinnedLink);
      }
    }
    this.pinnedCache.expire();
  }

  _unpinSearchShortcut(vendor) {
    for (let pinnedLink of lazy.NewTabUtils.pinnedLinks.links) {
      if (
        pinnedLink &&
        pinnedLink.searchTopSite &&
        lazy.NewTabUtils.shortURL(pinnedLink) === vendor
      ) {
        lazy.NewTabUtils.pinnedLinks.unpin(pinnedLink);
        this.pinnedCache.expire();

        const prevInsertedShortcuts = this.store
          .getState()
          .Prefs.values[SEARCH_SHORTCUTS_HAVE_PINNED_PREF].split(",");
        this.store.dispatch(
          ac.SetPref(
            SEARCH_SHORTCUTS_HAVE_PINNED_PREF,
            prevInsertedShortcuts.filter(s => s !== vendor).join(",")
          )
        );
        break;
      }
    }
  }

  /**
   * Reduces the given pinning index by the number of preceding sponsored
   * sites, to accomodate for sponsored sites pushing pinned ones to the side,
   * effectively increasing their index again.
   */
  _adjustPinIndexForSponsoredLinks(site, index) {
    if (!this._linksWithDefaults) {
      return index;
    }
    // Adjust insertion index for sponsored sites since their position is
    // fixed.
    let adjustedIndex = index;
    for (let i = 0; i < index; i++) {
      const link = this._linksWithDefaults[i];
      if (
        link &&
        link.sponsored_position &&
        this._linksWithDefaults[i]?.url !== site.url
      ) {
        adjustedIndex--;
      }
    }
    return adjustedIndex;
  }

  /**
   * Insert a site to pin at a position shifting over any other pinned sites.
   */
  _insertPin(site, originalIndex, draggedFromIndex) {
    if (this._groupedPinsEnabled) {
      this._insertPinGrouped(site, originalIndex);
      return;
    }
    let index = this._adjustPinIndexForSponsoredLinks(site, originalIndex);
    let adjustedDraggedFromIndex = this._adjustPinIndexForSponsoredLinks(
      site,
      draggedFromIndex
    );

    // Don't insert any pins past the end of the visible top sites. Otherwise,
    // we can end up with a bunch of pinned sites that can never be unpinned again
    // from the UI.
    const topSitesCount = getTopSitesCount(this.store.getState().Prefs.values);
    if (index >= topSitesCount) {
      return;
    }

    let pinned = lazy.NewTabUtils.pinnedLinks.links;
    if (!pinned[index]) {
      this._pinSiteAt(site, index);
    } else {
      pinned[adjustedDraggedFromIndex] = null;
      // Find the hole to shift the pinned site(s) towards. We shift towards the
      // hole left by the site being dragged.
      let holeIndex = index;
      const indexStep = index > adjustedDraggedFromIndex ? -1 : 1;
      while (pinned[holeIndex]) {
        holeIndex += indexStep;
      }
      if (holeIndex >= topSitesCount || holeIndex < 0) {
        // There are no holes, so we will effectively unpin the last slot and shifting
        // towards it. This only happens when adding a new top site to an already
        // fully pinned grid.
        holeIndex = topSitesCount - 1;
      }

      // Shift towards the hole.
      const shiftingStep = holeIndex > index ? -1 : 1;
      while (holeIndex !== index) {
        const nextIndex = holeIndex + shiftingStep;
        this._pinSiteAt(pinned[nextIndex], holeIndex);
        holeIndex = nextIndex;
      }
      this._pinSiteAt(site, index);
    }
  }

  /**
   * Grouped variant of `_insertPin`: `originalIndex` is a group position, and
   * `_pinSiteAtGrouped` removes any existing entry for the site, so there's no
   * hole-shifting (`draggedFromIndex` is unused).
   */
  _insertPinGrouped(site, originalIndex) {
    const index = this._adjustPinIndexForSponsoredLinks(site, originalIndex);
    const topSitesCount = getTopSitesCount(this.store.getState().Prefs.values);
    if (index >= topSitesCount) {
      return;
    }
    this._pinSiteAtGrouped(site, index);
  }

  /**
   * Handle an insert (drop/add) action of a site.
   */
  async insert(action) {
    let { index } = action.data;
    // Treat invalid pin index values (e.g., -1, undefined) as insert in the first position
    if (!(index > 0)) {
      index = 0;
    }

    if (this._groupedPinsEnabled) {
      await this._insertGrouped(action, index);
      return;
    }

    // Inserting a top site pins it in the specified slot, pushing over any link already
    // pinned in the slot (unless it's the last slot, then it replaces).
    this._insertPin(
      action.data.site,
      index,
      action.data.draggedFromIndex !== undefined
        ? action.data.draggedFromIndex
        : getTopSitesCount(this.store.getState().Prefs.values)
    );

    await this._clearLinkCustomScreenshot(action.data.site);
    this._broadcastPinnedSitesUpdated();
  }

  /**
   * Grouped variant of `insert`: a dragged tile reorders within / joins the
   * group at the drop's group position; a brand-new add (no drag source)
   * appends to the group.
   */
  async _insertGrouped(action, index) {
    const { site, draggedFromIndex } = action.data;
    if (draggedFromIndex !== undefined) {
      this._insertPinGrouped(site, index);
    } else {
      await this._pinSiteAtGrouped(site);
    }
    await this._clearLinkCustomScreenshot(site);
    this._broadcastPinnedSitesUpdated();
  }

  updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts }) {
    if (this._groupedPinsEnabled) {
      this._updatePinnedSearchShortcutsGrouped({
        addedShortcuts,
        deletedShortcuts,
      });
      return;
    }
    // Unpin the deletedShortcuts.
    deletedShortcuts.forEach(({ url }) => {
      lazy.NewTabUtils.pinnedLinks.unpin({ url });
    });

    // Pin the addedShortcuts.
    const numberOfSlots = getTopSitesCount(this.store.getState().Prefs.values);
    addedShortcuts.forEach(shortcut => {
      // Find first hole in pinnedLinks.
      let index = lazy.NewTabUtils.pinnedLinks.links.findIndex(link => !link);
      if (
        index < 0 &&
        lazy.NewTabUtils.pinnedLinks.links.length + 1 < numberOfSlots
      ) {
        // pinnedLinks can have less slots than the total available.
        index = lazy.NewTabUtils.pinnedLinks.links.length;
      }
      if (index >= 0) {
        lazy.NewTabUtils.pinnedLinks.pin(shortcut, index);
      } else {
        // No slots available, we need to do an insert in first slot and push over other pinned links.
        this._insertPin(shortcut, 0, numberOfSlots);
      }
    });

    this._broadcastPinnedSitesUpdated();
  }

  /**
   * Grouped variant of `updatePinnedSearchShortcuts`: removed shortcuts drop out
   * of the group and added ones append to it, up to the slot budget.
   */
  _updatePinnedSearchShortcutsGrouped({ addedShortcuts, deletedShortcuts }) {
    const numberOfSlots = getTopSitesCount(this.store.getState().Prefs.values);
    const deletedUrls = new Set(deletedShortcuts.map(({ url }) => url));
    let pins = this._groupedPins.filter(p => !deletedUrls.has(p.url));
    for (const shortcut of addedShortcuts) {
      if (pins.length < numberOfSlots) {
        pins.push(shortcut);
      }
    }
    this._saveGroupedPins(pins);
    this._broadcastPinnedSitesUpdated();
  }

  onAction(action) {
    switch (action.type) {
      case at.INIT:
        this.init();
        this.updateCustomSearchShortcuts(true /* isStartup */);
        if (lazy.AdsClient.isEnabled(this.store.getState().Prefs.values)) {
          this.adsClient = lazy.AdsClient.getClient();
        }
        break;
      case at.DISCOVERY_STREAM_DEV_SYSTEM_TICK:
      case at.DISCOVERY_STREAM_DEV_EXPIRE_CACHE:
      case at.SYSTEM_TICK:
        // Only SYSTEM_TICK is gated by the smart shortcuts rerank interval;
        // the dev tick and cache expiry pass their own type and always rerank.
        this.refresh({ broadcast: false, reason: action.type });
        this._contile.periodicUpdate();
        // We don't need to await on this,
        // we can let this update in the background.
        void this.updateFrecencyBoostedSpocs();
        break;
      // All these actions mean we need new top sites
      case at.PLACES_HISTORY_CLEARED:
      case at.PLACES_LINKS_DELETED:
        this.frecentCache.expire();
        this.refresh({ broadcast: true });
        break;
      case at.PLACES_LINKS_CHANGED:
        this.frecentCache.expire();
        this.refresh({ broadcast: false });
        break;
      case at.PLACES_LINK_BLOCKED:
        this.frecentCache.expire();
        this.pinnedCache.expire();
        this.refresh({ broadcast: true });
        break;
      case at.PREF_CHANGED:
        switch (action.data.name) {
          case DEFAULT_SITES_PREF:
            if (!this._useRemoteSetting) {
              this.refreshDefaults(action.data.value);
            }
            break;
          case ROWS_PREF:
          case FILTER_DEFAULT_SEARCH_PREF:
          case SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF:
          case GROUPED_PINS_PREF:
            this.refresh({ broadcast: true });
            break;
          case SHOW_SPONSORED_PREF:
          case PREF_UNIFIED_ADS_TILES_ENABLED:
            if (
              lazy.NimbusFeatures.newtab.getVariable(
                NIMBUS_VARIABLE_CONTILE_ENABLED
              )
            ) {
              this._contile.refresh();
            } else {
              this.refresh({ broadcast: true });
            }
            if (!action.data.value) {
              this._contile._resetContileCache();
            }
            if (action.data.name === SHOW_SPONSORED_PREF) {
              this._updateSponsoredScoring();
            }

            break;
          case SHOWN_ON_NEWTAB_PREF:
            this._updateSponsoredScoring();
            break;
          case SEARCH_SHORTCUTS_EXPERIMENT:
            if (action.data.value) {
              this.updateCustomSearchShortcuts();
            } else {
              this.unpinAllSearchShortcuts();
            }
            this.refresh({ broadcast: true });
            break;
          case PREF_UNIFIED_ADS_ADSFEED_ENABLED:
            this._contile.refresh();
            break;
        }
        break;
      case at.PREFS_INITIAL_VALUES:
        if (!this._useRemoteSetting) {
          this.refreshDefaults(action.data[DEFAULT_SITES_PREF]);
        }
        break;
      case at.TOP_SITES_PIN:
        this.pin(action);
        break;
      case at.TOP_SITES_UNPIN:
        this.unpin(action);
        break;
      case at.TOP_SITES_INSERT:
        this.insert(action);
        break;
      case at.PREVIEW_REQUEST:
        this.getScreenshotPreview(action.data.url, action.meta.fromTarget);
        break;
      case at.UPDATE_PINNED_SEARCH_SHORTCUTS:
        this.updatePinnedSearchShortcuts(action.data);
        break;
      case at.ADS_UPDATE_TILES:
        this._contile.refresh();
        break;
      case at.UNINIT:
        this.uninit();
        break;
    }
  }
}
