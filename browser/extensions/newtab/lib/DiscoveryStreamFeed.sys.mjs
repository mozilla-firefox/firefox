/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ClientEnvironmentBase:
    "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  pktApi: "chrome://pocket/content/pktApi.sys.mjs",
  PersistentCache: "resource://newtab/lib/PersistentCache.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
});

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// setTimeout / clearTimeout, and overrides importESModule
// to be a no-op (which can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { setTimeout, clearTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";

// `contextId` is a unique identifier used by Contextual Services
const CONTEXT_ID_PREF = "browser.contextual-services.contextId";
ChromeUtils.defineLazyGetter(lazy, "contextId", () => {
  let _contextId = Services.prefs.getStringPref(CONTEXT_ID_PREF, null);
  if (!_contextId) {
    _contextId = String(Services.uuid.generateUUID());
    Services.prefs.setStringPref(CONTEXT_ID_PREF, _contextId);
  }
  return _contextId;
});

const CACHE_KEY = "discovery_stream";
const STARTUP_CACHE_EXPIRE_TIME = 7 * 24 * 60 * 60 * 1000; // 1 week
const COMPONENT_FEEDS_UPDATE_TIME = 30 * 60 * 1000; // 30 minutes
const SPOCS_FEEDS_UPDATE_TIME = 30 * 60 * 1000; // 30 minutes
const DEFAULT_RECS_ROTATION_TIME = 60 * 60 * 1000; // 1 hour
const DEFAULT_RECS_IMPRESSION_EXPIRE_TIME = 7 * 24 * 60 * 60 * 1000; // 7 days
const MAX_LIFETIME_CAP = 500; // Guard against misconfiguration on the server
const SPOCS_CAP_DURATION = 24 * 60 * 60; // 1 day in seconds.
const FETCH_TIMEOUT = 45 * 1000;
const TOPIC_LOADING_TIMEOUT = 1 * 1000;
const TOPIC_SELECTION_DISPLAY_COUNT =
  "discoverystream.topicSelection.onboarding.displayCount";
const TOPIC_SELECTION_LAST_DISPLAYED =
  "discoverystream.topicSelection.onboarding.lastDisplayed";
const TOPIC_SELECTION_DISPLAY_TIMEOUT =
  "discoverystream.topicSelection.onboarding.displayTimeout";

const SPOCS_URL = "https://spocs.getpocket.com/spocs";
const FEED_URL =
  "https://getpocket.cdn.mozilla.net/v3/firefox/global-recs?version=3&consumer_key=$apiKey&locale_lang=$locale&region=$region&count=30";
const PREF_CONFIG = "discoverystream.config";
const PREF_ENDPOINTS = "discoverystream.endpoints";
const PREF_IMPRESSION_ID = "browser.newtabpage.activity-stream.impressionId";
const PREF_LAYOUT_EXPERIMENT_A = "newtabLayouts.variant-a";
const PREF_LAYOUT_EXPERIMENT_B = "newtabLayouts.variant-b";
const PREF_SPOC_PLACEMENTS = "discoverystream.placements.spocs";
const PREF_SPOC_COUNTS = "discoverystream.placements.spocs.counts";
const PREF_SPOC_POSITIONS = "discoverystream.spoc-positions";
const PREF_MERINO_FEED_EXPERIMENT =
  "browser.newtabpage.activity-stream.discoverystream.merino-feed-experiment";
const PREF_ENABLED = "discoverystream.enabled";
const PREF_HARDCODED_BASIC_LAYOUT = "discoverystream.hardcoded-basic-layout";
const PREF_SPOCS_ENDPOINT = "discoverystream.spocs-endpoint";
const PREF_SPOCS_ENDPOINT_QUERY = "discoverystream.spocs-endpoint-query";
const PREF_REGION_BASIC_LAYOUT = "discoverystream.region-basic-layout";
const PREF_USER_TOPSTORIES = "feeds.section.topstories";
const PREF_SYSTEM_TOPSTORIES = "feeds.system.topstories";
const PREF_SYSTEM_TOPSITES = "feeds.system.topsites";
const PREF_UNIFIED_ADS_BLOCKED_LIST = "unifiedAds.blockedAds";
const PREF_UNIFIED_ADS_SPOCS_ENABLED = "unifiedAds.spocs.enabled";
const PREF_UNIFIED_ADS_ENDPOINT = "unifiedAds.endpoint";
const PREF_USER_TOPSITES = "feeds.topsites";
const PREF_SPOCS_CLEAR_ENDPOINT = "discoverystream.endpointSpocsClear";
const PREF_SHOW_SPONSORED = "showSponsored";
const PREF_SYSTEM_SHOW_SPONSORED = "system.showSponsored";
const PREF_SHOW_SPONSORED_TOPSITES = "showSponsoredTopSites";
// Nimbus variable to enable the SOV feature for sponsored tiles.
const NIMBUS_VARIABLE_CONTILE_SOV_ENABLED = "topSitesContileSovEnabled";
const PREF_SPOC_IMPRESSIONS = "discoverystream.spoc.impressions";
const PREF_FLIGHT_BLOCKS = "discoverystream.flight.blocks";
const PREF_COLLECTIONS_ENABLED =
  "discoverystream.sponsored-collections.enabled";
const PREF_POCKET_BUTTON = "extensions.pocket.enabled";
const PREF_COLLECTION_DISMISSIBLE = "discoverystream.isCollectionDismissible";
const PREF_SELECTED_TOPICS = "discoverystream.topicSelection.selectedTopics";
const PREF_TOPIC_SELECTION_ENABLED = "discoverystream.topicSelection.enabled";
const PREF_TOPIC_SELECTION_PREVIOUS_SELECTED =
  "discoverystream.topicSelection.hasBeenUpdatedPreviously";
const PREF_SPOCS_CACHE_TIMEOUT = "discoverystream.spocs.cacheTimeout";
const PREF_SPOCS_STARTUP_CACHE_ENABLED =
  "discoverystream.spocs.startupCache.enabled";
const PREF_CONTEXTUAL_CONTENT_ENABLED =
  "discoverystream.contextualContent.enabled";
const PREF_FAKESPOT_ENABLED =
  "discoverystream.contextualContent.fakespot.enabled";
const PREF_CONTEXTUAL_CONTENT_SELECTED_FEED =
  "discoverystream.contextualContent.selectedFeed";
const PREF_CONTEXTUAL_CONTENT_LISTFEED_TITLE =
  "discoverystream.contextualContent.listFeedTitle";
const PREF_CONTEXTUAL_CONTENT_FAKESPOT_FOOTER =
  "discoverystream.contextualContent.fakespot.footerCopy";
const PREF_CONTEXTUAL_CONTENT_FAKESPOT_CATEGORY =
  "discoverystream.contextualContent.fakespot.defaultCategoryTitle";
const PREF_CONTEXTUAL_CONTENT_FAKESPOT_CTA_COPY =
  "discoverystream.contextualContent.fakespot.ctaCopy";
const PREF_CONTEXTUAL_CONTENT_FAKESPOT_CTA_URL =
  "discoverystream.contextualContent.fakespot.ctaUrl";

const PREF_SECTIONS_ENABLED = "discoverystream.sections.enabled";
const PREF_SECTIONS_FOLLOWING = "discoverystream.sections.following";
const PREF_SECTIONS_BLOCKED = "discoverystream.sections.blocked";
const PREF_INTEREST_PICKER_ENABLED =
  "discoverystream.sections.interestPicker.enabled";
const PREF_VISIBLE_SECTIONS =
  "discoverystream.sections.interestPicker.visibleSections";
const PREF_PRIVATE_PING_ENABLED = "telemetry.privatePing.enabled";
const PREF_SURFACE_ID = "telemetry.surfaceId";

let getHardcodedLayout;

export class DiscoveryStreamFeed {
  constructor() {
    // Internal state for checking if we've intialized all our data
    this.loaded = false;

    // Persistent cache for remote endpoint data.
    this.cache = new lazy.PersistentCache(CACHE_KEY, true);
    this.locale = Services.locale.appLocaleAsBCP47;
    this._impressionId = this.getOrCreateImpressionId();
    // Internal in-memory cache for parsing json prefs.
    this._prefCache = {};
  }

  getOrCreateImpressionId() {
    let impressionId = Services.prefs.getCharPref(PREF_IMPRESSION_ID, "");
    if (!impressionId) {
      impressionId = String(Services.uuid.generateUUID());
      Services.prefs.setCharPref(PREF_IMPRESSION_ID, impressionId);
    }
    return impressionId;
  }

  get config() {
    if (this._prefCache.config) {
      return this._prefCache.config;
    }
    try {
      this._prefCache.config = JSON.parse(
        this.store.getState().Prefs.values[PREF_CONFIG]
      );
    } catch (e) {
      // istanbul ignore next
      this._prefCache.config = {};
      // istanbul ignore next
      console.error(
        `Could not parse preference. Try resetting ${PREF_CONFIG} in about:config.`,
        e
      );
    }
    this._prefCache.config.enabled =
      this._prefCache.config.enabled &&
      this.store.getState().Prefs.values[PREF_ENABLED];

    return this._prefCache.config;
  }

  resetConfigDefauts() {
    this.store.dispatch({
      type: at.CLEAR_PREF,
      data: {
        name: PREF_CONFIG,
      },
    });
  }

  get region() {
    return lazy.Region.home;
  }

  get isBff() {
    if (this._isBff === undefined) {
      const pocketConfig =
        this.store.getState().Prefs.values?.pocketConfig || {};

      const preffedRegionBffConfigString = pocketConfig.regionBffConfig || "";
      const preffedRegionBffConfig = preffedRegionBffConfigString
        .split(",")
        .map(s => s.trim());
      const regionBff = preffedRegionBffConfig.includes(this.region);
      this._isBff = regionBff;
    }

    return this._isBff;
  }

  get isMerino() {
    if (this._isMerino === undefined) {
      const pocketConfig =
        this.store.getState().Prefs.values?.pocketConfig || {};

      this._isMerino = pocketConfig.merinoProviderEnabled;
    }

    return this._isMerino;
  }

  get showSpocs() {
    // High level overall sponsored check, if one of these is true,
    // we know we need some sort of spoc control setup.
    return this.showSponsoredStories || this.showSponsoredTopsites;
  }

  get showSponsoredStories() {
    // Combine user-set sponsored opt-out with Mozilla-set config
    return (
      this.store.getState().Prefs.values[PREF_SHOW_SPONSORED] &&
      this.store.getState().Prefs.values[PREF_SYSTEM_SHOW_SPONSORED]
    );
  }

  get showSponsoredTopsites() {
    const placements = this.getPlacements();
    // Combine user-set sponsored opt-out with placement data
    return !!(
      this.store.getState().Prefs.values[PREF_SHOW_SPONSORED_TOPSITES] &&
      placements.find(placement => placement.name === "sponsored-topsites")
    );
  }

  get showStories() {
    // Combine user-set stories opt-out with Mozilla-set config
    return (
      this.store.getState().Prefs.values[PREF_SYSTEM_TOPSTORIES] &&
      this.store.getState().Prefs.values[PREF_USER_TOPSTORIES]
    );
  }

  get showTopsites() {
    // Combine user-set topsites opt-out with Mozilla-set config
    return (
      this.store.getState().Prefs.values[PREF_SYSTEM_TOPSITES] &&
      this.store.getState().Prefs.values[PREF_USER_TOPSITES]
    );
  }

  get personalized() {
    return this.recommendationProvider.personalized;
  }

  get recommendationProvider() {
    if (this._recommendationProvider) {
      return this._recommendationProvider;
    }
    this._recommendationProvider = this.store.feeds.get(
      "feeds.recommendationprovider"
    );
    return this._recommendationProvider;
  }

  setupConfig(isStartup = false) {
    // Send the initial state of the pref on our reducer
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.DISCOVERY_STREAM_CONFIG_SETUP,
        data: this.config,
        meta: {
          isStartup,
        },
      })
    );
  }

  async setupDevtoolsState(isStartup = false) {
    const cachedData = (await this.cache.get()) || {};
    let impressions = cachedData.recsImpressions || {};
    let blocks = cachedData.recsBlocks || {};

    this.store.dispatch({
      type: at.DISCOVERY_STREAM_DEV_IMPRESSIONS,
      data: impressions,
      meta: {
        isStartup,
      },
    });

    this.store.dispatch({
      type: at.DISCOVERY_STREAM_DEV_BLOCKS,
      data: blocks,
      meta: {
        isStartup,
      },
    });
  }

  setupPrefs(isStartup = false) {
    const pocketNewtabExperiment = lazy.ExperimentAPI.getExperimentMetaData({
      featureId: "pocketNewtab",
    });

    const pocketNewtabRollout = lazy.ExperimentAPI.getRolloutMetaData({
      featureId: "pocketNewtab",
    });

    // We want to know if the user is in an experiment or rollout,
    // but we prioritize experiments over rollouts.
    const experimentMetaData = pocketNewtabExperiment || pocketNewtabRollout;

    let utmSource = "pocket-newtab";
    let utmCampaign = experimentMetaData?.slug;
    let utmContent = experimentMetaData?.branch?.slug;

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.DISCOVERY_STREAM_EXPERIMENT_DATA,
        data: {
          utmSource,
          utmCampaign,
          utmContent,
        },
        meta: {
          isStartup,
        },
      })
    );

    const pocketButtonEnabled = Services.prefs.getBoolPref(PREF_POCKET_BUTTON);

    const nimbusConfig = this.store.getState().Prefs.values?.pocketConfig || {};
    const { region } = this.store.getState().Prefs.values;

    this.setupSpocsCacheUpdateTime();
    const saveToPocketCardRegions = nimbusConfig.saveToPocketCardRegions
      ?.split(",")
      .map(s => s.trim());
    const saveToPocketCard =
      pocketButtonEnabled &&
      (nimbusConfig.saveToPocketCard ||
        saveToPocketCardRegions?.includes(region));

    const hideDescriptionsRegions = nimbusConfig.hideDescriptionsRegions
      ?.split(",")
      .map(s => s.trim());
    const hideDescriptions =
      nimbusConfig.hideDescriptions ||
      hideDescriptionsRegions?.includes(region);

    // We don't BroadcastToContent for this, as the changes may
    // shift around elements on an open newtab the user is currently reading.
    // So instead we AlsoToPreloaded so the next tab is updated.
    // This is because setupPrefs is called by the system and not a user interaction.
    this.store.dispatch(
      ac.AlsoToPreloaded({
        type: at.DISCOVERY_STREAM_PREFS_SETUP,
        data: {
          recentSavesEnabled: nimbusConfig.recentSavesEnabled,
          pocketButtonEnabled,
          saveToPocketCard,
          hideDescriptions,
          compactImages: nimbusConfig.compactImages,
          imageGradient: nimbusConfig.imageGradient,
          newSponsoredLabel: nimbusConfig.newSponsoredLabel,
          titleLines: nimbusConfig.titleLines,
          descLines: nimbusConfig.descLines,
          readTime: nimbusConfig.readTime,
        },
        meta: {
          isStartup,
        },
      })
    );

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.DISCOVERY_STREAM_COLLECTION_DISMISSIBLE_TOGGLE,
        data: {
          value:
            this.store.getState().Prefs.values[PREF_COLLECTION_DISMISSIBLE],
        },
        meta: {
          isStartup,
        },
      })
    );
  }

  async configureFollowedSections() {
    const prefs = this.store.getState().Prefs.values;
    const cachedData = (await this.cache.get()) || {};
    let { sectionPersonalization } = cachedData;

    // if sectionPersonalization is empty, populate it with data from the followed and blocked prefs
    // eventually we could remove this (maybe once more of sections is added to release)
    if (
      sectionPersonalization &&
      Object.keys(sectionPersonalization).length === 0
    ) {
      // Raw string of followed/blocked topics, ex: "entertainment, news"
      const followedSectionsString = prefs[PREF_SECTIONS_FOLLOWING];
      const blockedSectionsString = prefs[PREF_SECTIONS_BLOCKED];
      // Format followed sections
      const followedSections = followedSectionsString
        ? followedSectionsString.split(",").map(s => s.trim())
        : [];

      // Format blocked sections
      const blockedSections = blockedSectionsString
        ? blockedSectionsString.split(",").map(s => s.trim())
        : [];

      const sectionTopics = new Set([...followedSections, ...blockedSections]);
      sectionPersonalization = Array.from(sectionTopics).reduce(
        (acc, section) => {
          acc[section] = {
            isFollowed: followedSections.includes(section),
            isBlocked: blockedSections.includes(section),
          };
          return acc;
        },
        {}
      );
      await this.cache.set(
        "sectionPersonalization",
        sectionPersonalization || {}
      );
    }
    this.store.dispatch(
      ac.AlsoToMain({
        type: at.SECTION_PERSONALIZATION_UPDATE,
        data: sectionPersonalization || {},
      })
    );
  }

  async setupPocketState(target) {
    let dispatch = action =>
      this.store.dispatch(ac.OnlyToOneContent(action, target));
    const isUserLoggedIn = lazy.pktApi.isUserLoggedIn();
    dispatch({
      type: at.DISCOVERY_STREAM_POCKET_STATE_SET,
      data: {
        isUserLoggedIn,
      },
    });

    // If we're not logged in, don't bother fetching recent saves, we're done.
    if (isUserLoggedIn) {
      let recentSaves = await lazy.pktApi.getRecentSavesCache();
      if (recentSaves) {
        // We have cache, so we can use those.
        dispatch({
          type: at.DISCOVERY_STREAM_RECENT_SAVES,
          data: {
            recentSaves,
          },
        });
      } else {
        // We don't have cache, so fetch fresh stories.
        lazy.pktApi.getRecentSaves({
          success(data) {
            dispatch({
              type: at.DISCOVERY_STREAM_RECENT_SAVES,
              data: {
                recentSaves: data,
              },
            });
          },
          error() {},
        });
      }
    }
  }

  uninitPrefs() {
    // Reset in-memory cache
    this._prefCache = {};
  }

  async fetchFromEndpoint(rawEndpoint, options = {}) {
    if (!rawEndpoint) {
      console.error("Tried to fetch endpoint but none was configured.");
      return null;
    }

    const apiKeyPref = this.config.api_key_pref;
    const apiKey = Services.prefs.getCharPref(apiKeyPref, "");

    const endpoint = rawEndpoint
      .replace("$apiKey", apiKey)
      .replace("$locale", this.locale)
      .replace("$region", this.region);

    try {
      // Make sure the requested endpoint is allowed
      const allowed =
        this.store
          .getState()
          .Prefs.values[PREF_ENDPOINTS].split(",")
          .map(item => item.trim())
          .filter(item => item) || [];
      if (!allowed.some(prefix => endpoint.startsWith(prefix))) {
        throw new Error(`Not one of allowed prefixes (${allowed})`);
      }

      const controller = new AbortController();
      const { signal } = controller;

      const fetchPromise = fetch(endpoint, {
        ...options,
        credentials: "omit",
        signal,
      });
      // istanbul ignore next
      const timeoutId = setTimeout(() => {
        controller.abort();
      }, FETCH_TIMEOUT);

      const response = await fetchPromise;
      if (!response.ok) {
        throw new Error(`Unexpected status (${response.status})`);
      }
      clearTimeout(timeoutId);

      return response.json();
    } catch (error) {
      console.error(`Failed to fetch ${endpoint}:`, error.message);
    }
    return null;
  }

  get spocsCacheUpdateTime() {
    if (this._spocsCacheUpdateTime) {
      return this._spocsCacheUpdateTime;
    }
    this.setupSpocsCacheUpdateTime();
    return this._spocsCacheUpdateTime;
  }

  setupSpocsCacheUpdateTime() {
    const spocsCacheTimeout =
      this.store.getState().Prefs.values[PREF_SPOCS_CACHE_TIMEOUT];
    const MAX_TIMEOUT = 30;
    const MIN_TIMEOUT = 5;
    // We do a bit of min max checking the the configured value is between
    // 5 and 30 minutes, to protect against unreasonable values.
    if (
      spocsCacheTimeout &&
      spocsCacheTimeout <= MAX_TIMEOUT &&
      spocsCacheTimeout >= MIN_TIMEOUT
    ) {
      // This value is in minutes, but we want ms.
      this._spocsCacheUpdateTime = spocsCacheTimeout * 60 * 1000;
    } else {
      // The const is already in ms.
      this._spocsCacheUpdateTime = SPOCS_FEEDS_UPDATE_TIME;
    }
  }

  /**
   * Returns true if data in the cache for a particular key has expired or is missing.
   * @param {object} cachedData data returned from cache.get()
   * @param {string} key a cache key
   * @param {string?} url for "feed" only, the URL of the feed.
   * @param {boolean} is this check done at initial browser load
   */
  isExpired({ cachedData, key, url, isStartup }) {
    const { spocs, feeds } = cachedData;
    const updateTimePerComponent = {
      spocs: this.spocsCacheUpdateTime,
      feed: COMPONENT_FEEDS_UPDATE_TIME,
    };
    const EXPIRATION_TIME = isStartup
      ? STARTUP_CACHE_EXPIRE_TIME
      : updateTimePerComponent[key];
    switch (key) {
      case "spocs":
        return !spocs || !(Date.now() - spocs.lastUpdated < EXPIRATION_TIME);
      case "feed":
        return (
          !feeds ||
          !feeds[url] ||
          !(Date.now() - feeds[url].lastUpdated < EXPIRATION_TIME)
        );
      default:
        // istanbul ignore next
        throw new Error(`${key} is not a valid key`);
    }
  }

  async _checkExpirationPerComponent() {
    const cachedData = (await this.cache.get()) || {};
    const { feeds } = cachedData;
    return {
      spocs: this.showSpocs && this.isExpired({ cachedData, key: "spocs" }),
      feeds:
        this.showStories &&
        (!feeds ||
          Object.keys(feeds).some(url =>
            this.isExpired({ cachedData, key: "feed", url })
          )),
    };
  }

  /**
   * Returns true if any data for the cached endpoints has expired or is missing.
   */
  async checkIfAnyCacheExpired() {
    const expirationPerComponent = await this._checkExpirationPerComponent();
    return expirationPerComponent.spocs || expirationPerComponent.feeds;
  }

  updatePlacements(sendUpdate, layout, isStartup = false) {
    const placements = [];
    const placementsMap = {};
    for (const row of layout.filter(r => r.components && r.components.length)) {
      for (const component of row.components.filter(
        c => c.placement && c.spocs
      )) {
        // If we find a valid placement, we set it to this value.
        let placement;

        // We need to check to see if this placement is on or not.
        // If this placement has a prefs array, check against that.
        if (component.spocs.prefs) {
          // Check every pref in the array to see if this placement is turned on.
          if (
            component.spocs.prefs.length &&
            component.spocs.prefs.every(
              p => this.store.getState().Prefs.values[p]
            )
          ) {
            // This placement is on.
            placement = component.placement;
          }
        } else if (this.showSponsoredStories) {
          // If we do not have a prefs array, use old check.
          // This is because Pocket spocs uses an old non pref method.
          placement = component.placement;
        }

        // Validate this placement and check for dupes.
        if (placement?.name && !placementsMap[placement.name]) {
          placementsMap[placement.name] = placement;
          placements.push(placement);
        }
      }
    }

    // Update placements data.
    // Even if we have no placements, we still want to update it to clear it.
    sendUpdate({
      type: at.DISCOVERY_STREAM_SPOCS_PLACEMENTS,
      data: { placements },
      meta: {
        isStartup,
      },
    });
  }

  /**
   * Adds a query string to a URL.
   * A query can be any string literal accepted by https://developer.mozilla.org/docs/Web/API/URLSearchParams
   * Examples: "?foo=1&bar=2", "&foo=1&bar=2", "foo=1&bar=2", "?bar=2" or "bar=2"
   */
  addEndpointQuery(url, query) {
    if (!query) {
      return url;
    }

    const urlObject = new URL(url);
    const params = new URLSearchParams(query);

    for (let [key, val] of params.entries()) {
      urlObject.searchParams.append(key, val);
    }

    return urlObject.toString();
  }

  parseGridPositions(csvPositions) {
    let gridPositions;

    // Only accept parseable non-negative integers
    try {
      gridPositions = csvPositions.map(index => {
        let parsedInt = parseInt(index, 10);

        if (!isNaN(parsedInt) && parsedInt >= 0) {
          return parsedInt;
        }

        throw new Error("Bad input");
      });
    } catch (e) {
      // Catch spoc positions that are not numbers or negative, and do nothing.
      // We have hard coded backup positions.
      gridPositions = undefined;
    }

    return gridPositions;
  }

  generateFeedUrl() {
    // check for experiment parameters
    const hasParameters = lazy.NimbusFeatures.pocketNewtab.getVariable(
      "pocketFeedParameters"
    );

    if (this.isMerino) {
      return `https://${Services.prefs.getStringPref(
        "browser.newtabpage.activity-stream.discoverystream.merino-provider.endpoint"
      )}/api/v1/curated-recommendations`;
    } else if (this.isBff) {
      return `https://${Services.prefs.getStringPref(
        "extensions.pocket.bffApi"
      )}/desktop/v1/recommendations?locale=$locale&region=$region&count=30${
        hasParameters || ""
      }`;
    }
    return FEED_URL;
  }

  loadLayout(sendUpdate, isStartup) {
    let layoutData = {};
    let url = "";

    const isBasicLayout =
      this.config.hardcoded_basic_layout ||
      this.store.getState().Prefs.values[PREF_HARDCODED_BASIC_LAYOUT] ||
      this.store.getState().Prefs.values[PREF_REGION_BASIC_LAYOUT];

    const sponsoredCollectionsEnabled =
      this.store.getState().Prefs.values[PREF_COLLECTIONS_ENABLED];

    const pocketConfig = this.store.getState().Prefs.values?.pocketConfig || {};
    const onboardingExperience =
      this.isBff && pocketConfig.onboardingExperience;

    // The Unified Ads API does not support the spoc topsite placement.
    const unifiedAdsEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];
    const spocTopsitesPlacementEnabled =
      pocketConfig.spocTopsitesPlacementEnabled && !unifiedAdsEnabled;

    const layoutExperiment =
      this.store.getState().Prefs.values[PREF_LAYOUT_EXPERIMENT_A] ||
      this.store.getState().Prefs.values[PREF_LAYOUT_EXPERIMENT_B];

    let items = isBasicLayout ? 3 : 21;
    if (
      pocketConfig.fourCardLayout ||
      pocketConfig.hybridLayout ||
      layoutExperiment
    ) {
      items = isBasicLayout ? 4 : 24;
    }

    const ctaButtonSponsors = pocketConfig.ctaButtonSponsors
      ?.split(",")
      .map(s => s.trim().toLowerCase());
    let ctaButtonVariant = "";
    // We specifically against hard coded values, instead of applying whatever is in the pref.
    // This is to ensure random class names from a user modified pref doesn't make it into the class list.
    if (
      pocketConfig.ctaButtonVariant === "variant-a" ||
      pocketConfig.ctaButtonVariant === "variant-b"
    ) {
      ctaButtonVariant = pocketConfig.ctaButtonVariant;
    }

    const topicSelectionHasBeenUpdatedPreviously =
      this.store.getState().Prefs.values[
        PREF_TOPIC_SELECTION_PREVIOUS_SELECTED
      ];

    const selectedTopics =
      this.store.getState().Prefs.values[PREF_SELECTED_TOPICS];

    // Note: This requires a cache update to react to a pref update
    const pocketStoriesHeadlineId =
      topicSelectionHasBeenUpdatedPreviously || selectedTopics
        ? "newtab-section-header-todays-picks"
        : "newtab-section-header-stories";

    pocketConfig.pocketStoriesHeadlineId = pocketStoriesHeadlineId;

    let spocMessageVariant = "";
    if (
      pocketConfig.spocMessageVariant === "variant-a" ||
      pocketConfig.spocMessageVariant === "variant-b"
    ) {
      spocMessageVariant = pocketConfig.spocMessageVariant;
    }

    const prepConfArr = arr => {
      return arr
        ?.split(",")
        .filter(item => item)
        .map(item => parseInt(item, 10));
    };

    const spocAdTypes = prepConfArr(pocketConfig.spocAdTypes);
    const spocZoneIds = prepConfArr(pocketConfig.spocZoneIds);
    const spocTopsitesAdTypes = prepConfArr(pocketConfig.spocTopsitesAdTypes);
    const spocTopsitesZoneIds = prepConfArr(pocketConfig.spocTopsitesZoneIds);
    const { spocSiteId } = pocketConfig;
    let spocPlacementData;
    let spocTopsitesPlacementData;
    let spocsUrl;

    if (spocAdTypes?.length && spocZoneIds?.length) {
      spocPlacementData = {
        ad_types: spocAdTypes,
        zone_ids: spocZoneIds,
      };
    }

    if (spocTopsitesAdTypes?.length && spocTopsitesZoneIds?.length) {
      spocTopsitesPlacementData = {
        ad_types: spocTopsitesAdTypes,
        zone_ids: spocTopsitesZoneIds,
      };
    }

    if (spocSiteId) {
      const newUrl = new URL(SPOCS_URL);
      newUrl.searchParams.set("site", spocSiteId);
      spocsUrl = newUrl.href;
    }

    let feedUrl = this.generateFeedUrl();

    // Set layout config.
    // Changing values in this layout in memory object is unnecessary.
    layoutData = getHardcodedLayout({
      spocsUrl,
      feedUrl,
      items,
      sponsoredCollectionsEnabled,
      spocPlacementData,
      spocTopsitesPlacementEnabled,
      spocTopsitesPlacementData,
      spocPositions: this.parseGridPositions(
        this.store.getState().Prefs.values[PREF_SPOC_POSITIONS]?.split(`,`)
      ),
      spocTopsitesPositions: this.parseGridPositions(
        pocketConfig.spocTopsitesPositions?.split(`,`)
      ),
      widgetPositions: this.parseGridPositions(
        pocketConfig.widgetPositions?.split(`,`)
      ),
      widgetData: [
        ...(this.locale.startsWith("en-") ? [{ type: "TopicsWidget" }] : []),
      ],
      hybridLayout: pocketConfig.hybridLayout,
      hideCardBackground: pocketConfig.hideCardBackground,
      fourCardLayout: pocketConfig.fourCardLayout,
      newFooterSection: pocketConfig.newFooterSection,
      compactGrid: pocketConfig.compactGrid,
      // For now essentialReadsHeader and editorsPicksHeader are English only.
      essentialReadsHeader:
        this.locale.startsWith("en-") && pocketConfig.essentialReadsHeader,
      editorsPicksHeader:
        this.locale.startsWith("en-") && pocketConfig.editorsPicksHeader,
      onboardingExperience,
      // For now button variants are for experimentation and English only.
      ctaButtonSponsors: this.locale.startsWith("en-") ? ctaButtonSponsors : [],
      ctaButtonVariant: this.locale.startsWith("en-") ? ctaButtonVariant : "",
      spocMessageVariant: this.locale.startsWith("en-")
        ? spocMessageVariant
        : "",
      pocketStoriesHeadlineId: pocketConfig.pocketStoriesHeadlineId,
    });

    sendUpdate({
      type: at.DISCOVERY_STREAM_LAYOUT_UPDATE,
      data: layoutData,
      meta: {
        isStartup,
      },
    });

    if (layoutData.spocs) {
      url =
        this.store.getState().Prefs.values[PREF_SPOCS_ENDPOINT] ||
        this.config.spocs_endpoint ||
        layoutData.spocs.url;

      const spocsEndpointQuery =
        this.store.getState().Prefs.values[PREF_SPOCS_ENDPOINT_QUERY];

      // For QA, testing, or debugging purposes, there may be a query string to add.
      url = this.addEndpointQuery(url, spocsEndpointQuery);

      if (
        url &&
        url !== this.store.getState().DiscoveryStream.spocs.spocs_endpoint
      ) {
        sendUpdate({
          type: at.DISCOVERY_STREAM_SPOCS_ENDPOINT,
          data: {
            url,
          },
          meta: {
            isStartup,
          },
        });
        this.updatePlacements(sendUpdate, layoutData.layout, isStartup);
      }
    }
  }

  /**
   * buildFeedPromise - Adds the promise result to newFeeds and
   *                    pushes a promise to newsFeedsPromises.
   * @param {Object} Has both newFeedsPromises (Array) and newFeeds (Object)
   * @param {Boolean} isStartup We have different cache handling for startup.
   * @returns {Function} We return a function so we can contain
   *                     the scope for isStartup and the promises object.
   *                     Combines feed results and promises for each component with a feed.
   */
  buildFeedPromise(
    { newFeedsPromises, newFeeds },
    isStartup = false,
    sendUpdate
  ) {
    return component => {
      const { url } = component.feed;

      if (!newFeeds[url]) {
        // We initially stub this out so we don't fetch dupes,
        // we then fill in with the proper object inside the promise.
        newFeeds[url] = {};
        const feedPromise = this.getComponentFeed(url, isStartup);

        feedPromise
          .then(feed => {
            // I think we could reduce doing this for cache fetches.
            // Bug https://bugzilla.mozilla.org/show_bug.cgi?id=1606277
            // We can remove filterRecommendations once ESR catches up to bug 1932196
            newFeeds[url] = this.filterRecommendations(feed);
            sendUpdate({
              type: at.DISCOVERY_STREAM_FEED_UPDATE,
              data: {
                feed: newFeeds[url],
                url,
              },
              meta: {
                isStartup,
              },
            });
          })
          .catch(
            /* istanbul ignore next */ error => {
              console.error(
                `Error trying to load component feed ${url}:`,
                error
              );
            }
          );
        newFeedsPromises.push(feedPromise);
      }
    };
  }

  // This filters just recommendations using NewTabUtils.blockedLinks only.
  // This is essentially a sync blocked links filter. filterBlocked is async.
  // See bug 1606277.
  filterRecommendations(feed) {
    if (feed?.data?.recommendations?.length) {
      const recommendations = feed.data.recommendations.filter(item => {
        const blocked = lazy.NewTabUtils.blockedLinks.isBlocked({
          url: item.url,
        });
        return !blocked;
      });

      return {
        ...feed,
        data: {
          ...feed.data,
          recommendations,
        },
      };
    }
    return feed;
  }

  /**
   * reduceFeedComponents - Filters out components with no feeds, and combines
   *                        all feeds on this component with the feeds from other components.
   * @param {Boolean} isStartup We have different cache handling for startup.
   * @returns {Function} We return a function so we can contain the scope for isStartup.
   *                     Reduces feeds into promises and feed data.
   */
  reduceFeedComponents(isStartup, sendUpdate) {
    return (accumulator, row) => {
      row.components
        .filter(component => component && component.feed)
        .forEach(this.buildFeedPromise(accumulator, isStartup, sendUpdate));
      return accumulator;
    };
  }

  /**
   * buildFeedPromises - Filters out rows with no components,
   *                     and gets us a promise for each unique feed.
   * @param {Object} layout This is the Discovery Stream layout object.
   * @param {Boolean} isStartup We have different cache handling for startup.
   * @returns {Object} An object with newFeedsPromises (Array) and newFeeds (Object),
   *                   we can Promise.all newFeedsPromises to get completed data in newFeeds.
   */
  buildFeedPromises(layout, isStartup, sendUpdate) {
    const initialData = {
      newFeedsPromises: [],
      newFeeds: {},
    };
    return layout
      .filter(row => row && row.components)
      .reduce(this.reduceFeedComponents(isStartup, sendUpdate), initialData);
  }

  async loadComponentFeeds(sendUpdate, isStartup = false) {
    const { DiscoveryStream } = this.store.getState();

    if (!DiscoveryStream || !DiscoveryStream.layout) {
      return;
    }

    // Reset the flag that indicates whether or not at least one API request
    // was issued to fetch the component feed in `getComponentFeed()`.
    this.componentFeedFetched = false;
    const { newFeedsPromises, newFeeds } = this.buildFeedPromises(
      DiscoveryStream.layout,
      isStartup,
      sendUpdate
    );

    // Each promise has a catch already built in, so no need to catch here.
    await Promise.all(newFeedsPromises);
    await this.cache.set("feeds", newFeeds);
    sendUpdate({
      type: at.DISCOVERY_STREAM_FEEDS_UPDATE,
      meta: {
        isStartup,
      },
    });
  }

  getPlacements() {
    const { placements } = this.store.getState().DiscoveryStream.spocs;
    return placements;
  }

  // I wonder, can this be better as a reducer?
  // See Bug https://bugzilla.mozilla.org/show_bug.cgi?id=1606717
  placementsForEach(callback) {
    this.getPlacements().forEach(callback);
  }

  // Bug 1567271 introduced meta data on a list of spocs.
  // This involved moving the spocs array into an items prop.
  // However, old data could still be returned, and cached data might also be old.
  // For ths reason, we want to ensure if we don't find an items array,
  // we use the previous array placement, and then stub out title and context to empty strings.
  // We need to do this *after* both fresh fetches and cached data to reduce repetition.

  // Bug 1916488 introduced a new data stricture from the unified ads API.
  // We want to maintain both implementations until we're done rollout out,
  // so for now we are going to normlaize the new data to match the old data props,
  // so we can change as little as possible. Once we commit to one, we can remove all this.
  normalizeSpocsItems(spocs) {
    const unifiedAdsEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];
    if (unifiedAdsEnabled) {
      return {
        items: spocs.map(spoc => ({
          format: spoc.format,
          alt_text: spoc.alt_text,
          id: spoc.caps?.cap_key,
          flight_id: spoc.block_key,
          block_key: spoc.block_key,
          shim: spoc.callbacks,
          caps: {
            flight: {
              count: spoc.caps?.day,
              period: SPOCS_CAP_DURATION,
            },
          },
          domain: spoc.domain,
          excerpt: spoc.excerpt,
          raw_image_src: spoc.image_url,
          priority: spoc.ranking?.priority || 1,
          personalization_models: spoc.ranking?.personalization_models,
          item_score: spoc.ranking?.item_score,
          sponsor: spoc.sponsor,
          title: spoc.title,
          url: spoc.url,
        })),
      };
    }

    const items = spocs.items || spocs;
    const title = spocs.title || "";
    const context = spocs.context || "";
    const sponsor = spocs.sponsor || "";
    // We do not stub sponsored_by_override with an empty string. It is an override, and an empty string
    // explicitly means to override the client to display an empty string.
    // An empty string is not an no op in this case. Undefined is the proper no op here.
    const { sponsored_by_override } = spocs;
    // Undefined is fine here. It's optional and only used by collections.
    // If we leave it out, you get a collection that cannot be dismissed.
    const { flight_id } = spocs;
    return {
      items,
      title,
      context,
      sponsor,
      sponsored_by_override,
      ...(flight_id ? { flight_id } : {}),
    };
  }

  updateSponsoredCollectionsPref(collectionEnabled = false) {
    const currentState =
      this.store.getState().Prefs.values[PREF_COLLECTIONS_ENABLED];

    // If the current state does not match the new state, update the pref.
    if (currentState !== collectionEnabled) {
      this.store.dispatch(
        ac.SetPref(PREF_COLLECTIONS_ENABLED, collectionEnabled)
      );
    }
  }

  async loadSpocs(sendUpdate, isStartup) {
    const cachedData = (await this.cache.get()) || {};
    const unifiedAdsEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];
    let spocsState = cachedData.spocs;
    let placements = this.getPlacements();
    let unifiedAdsPlacements = [];

    if (
      this.showSpocs &&
      placements?.length &&
      this.isExpired({ cachedData, key: "spocs", isStartup })
    ) {
      // We optimistically set this to true, because if SOV is not ready, we fetch them.
      let useTopsitesPlacement = true;

      // If SOV is turned off or not available, we optimistically fetch sponsored topsites.
      if (
        lazy.NimbusFeatures.pocketNewtab.getVariable(
          NIMBUS_VARIABLE_CONTILE_SOV_ENABLED
        ) &&
        !unifiedAdsEnabled
      ) {
        let { positions, ready } = this.store.getState().TopSites.sov;
        if (ready) {
          // We don't need to await here, because we don't need it now.
          this.cache.set("sov", positions);
        } else {
          // If SOV is not available, and there is a SOV cache, use it.
          positions = cachedData.sov;
        }

        if (positions?.length) {
          // If SOV is ready and turned on, we can check if we need moz-sales position.
          useTopsitesPlacement = positions.some(
            allocation => allocation.assignedPartner === "moz-sales"
          );
        }
      }

      // We can filter out the topsite placement from the fetch.
      if (!useTopsitesPlacement || unifiedAdsEnabled) {
        placements = placements.filter(
          placement => placement.name !== "sponsored-topsites"
        );
      }

      if (placements?.length) {
        const apiKeyPref = this.config.api_key_pref;
        const apiKey = Services.prefs.getCharPref(apiKeyPref, "");
        const state = this.store.getState();
        let endpoint = state.DiscoveryStream.spocs.spocs_endpoint;
        let body = {
          pocket_id: this._impressionId,
          version: 2,
          consumer_key: apiKey,
          ...(placements.length ? { placements } : {}),
        };

        if (unifiedAdsEnabled) {
          const endpointBaseUrl = state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];
          endpoint = `${endpointBaseUrl}v1/ads`;
          const placementsArray = state.Prefs.values[
            PREF_SPOC_PLACEMENTS
          ]?.split(`,`)
            .map(s => s.trim())
            .filter(item => item);
          const countsArray = state.Prefs.values[PREF_SPOC_COUNTS]?.split(`,`)
            .map(s => s.trim())
            .filter(item => item)
            .map(item => parseInt(item, 10));

          unifiedAdsPlacements = placementsArray.map((placement, index) => ({
            placement,
            count: countsArray[index],
          }));

          const blockedSponsors =
            this.store.getState().Prefs.values[PREF_UNIFIED_ADS_BLOCKED_LIST];

          body = {
            context_id: lazy.contextId,
            placements: unifiedAdsPlacements,
            blocks: blockedSponsors.split(","),
          };
        }

        const headers = new Headers();
        headers.append("content-type", "application/json");

        let spocsResponse;
        try {
          spocsResponse = await this.fetchFromEndpoint(endpoint, {
            method: "POST",
            headers,
            body: JSON.stringify(body),
          });
        } catch (error) {
          console.error("Error trying to load spocs feeds:", error);
        }

        if (spocsResponse) {
          const fetchTimestamp = Date.now();
          spocsState = {
            lastUpdated: fetchTimestamp,
            spocs: {
              ...spocsResponse,
            },
          };

          if (spocsResponse.settings && spocsResponse.settings.feature_flags) {
            this.store.dispatch(
              ac.OnlyToMain({
                type: at.DISCOVERY_STREAM_PERSONALIZATION_OVERRIDE,
                data: {
                  override: !spocsResponse.settings.feature_flags.spoc_v2,
                },
              })
            );
            this.updateSponsoredCollectionsPref(
              spocsResponse.settings.feature_flags.collections
            );
          }

          const spocsResultPromises = this.getPlacements().map(
            async placement => {
              let freshSpocs = spocsState.spocs[placement.name];

              if (unifiedAdsEnabled) {
                freshSpocs = unifiedAdsPlacements.reduce(
                  (accumulator, currentValue) => {
                    return accumulator.concat(
                      spocsState.spocs[currentValue.placement]
                    );
                  },
                  []
                );
              }

              if (!freshSpocs) {
                return;
              }

              // spocs can be returns as an array, or an object with an items array.
              // We want to normalize this so all our spocs have an items array.
              // There can also be some meta data for title and context.
              // This is mostly because of backwards compat.
              const {
                items: normalizedSpocsItems,
                title,
                context,
                sponsor,
                sponsored_by_override,
              } = this.normalizeSpocsItems(freshSpocs);

              if (!normalizedSpocsItems || !normalizedSpocsItems.length) {
                // In the case of old data, we still want to ensure we normalize the data structure,
                // even if it's empty. We expect the empty data to be an object with items array,
                // and not just an empty array.
                spocsState.spocs = {
                  ...spocsState.spocs,
                  [placement.name]: {
                    title,
                    context,
                    items: [],
                  },
                };
                return;
              }

              // Migrate flight_id
              const { data: migratedSpocs } =
                this.migrateFlightId(normalizedSpocsItems);

              const { data: capResult } = this.frequencyCapSpocs(migratedSpocs);

              const { data: blockedResults } =
                await this.filterBlocked(capResult);

              const { data: spocsWithFetchTimestamp } = this.addFetchTimestamp(
                blockedResults,
                fetchTimestamp
              );

              const { data: scoredResults, personalized } =
                await this.scoreItems(spocsWithFetchTimestamp, "spocs");

              spocsState.spocs = {
                ...spocsState.spocs,
                [placement.name]: {
                  title,
                  context,
                  sponsor,
                  sponsored_by_override,
                  personalized,
                  items: scoredResults,
                },
              };
            }
          );
          await Promise.all(spocsResultPromises);

          this.cleanUpFlightImpressionPref(spocsState.spocs);
        } else {
          console.error("No response for spocs_endpoint prop");
        }
      }
    }

    // Use good data if we have it, otherwise nothing.
    // We can have no data if spocs set to off.
    // We can have no data if request fails and there is no good cache.
    // We want to send an update spocs or not, so client can render something.
    spocsState =
      spocsState && spocsState.spocs
        ? spocsState
        : {
            lastUpdated: Date.now(),
            spocs: {},
          };
    await this.cache.set("spocs", {
      lastUpdated: spocsState.lastUpdated,
      spocs: spocsState.spocs,
    });

    sendUpdate({
      type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
      data: {
        lastUpdated: spocsState.lastUpdated,
        spocs: spocsState.spocs,
      },
      meta: {
        isStartup,
      },
    });
  }

  async clearSpocs() {
    const state = this.store.getState();
    let endpoint = state.Prefs.values[PREF_SPOCS_CLEAR_ENDPOINT];

    const unifiedAdsEnabled =
      state.Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];

    let body = {
      pocket_id: this._impressionId,
    };

    if (unifiedAdsEnabled) {
      const endpointBaseUrl = state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];

      if (!endpointBaseUrl) {
        return;
      }

      endpoint = `${endpointBaseUrl}v1/delete_user`;
      body = {
        context_id: lazy.contextId,
      };
    }

    if (!endpoint) {
      return;
    }
    const headers = new Headers();
    headers.append("content-type", "application/json");

    await this.fetchFromEndpoint(endpoint, {
      method: "DELETE",
      headers,
      body: JSON.stringify(body),
    });
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "nsPref:changed":
        // If the Pocket button was turned on or off, we need to update the cards
        // because cards show menu options for the Pocket button that need to be removed.
        if (data === PREF_POCKET_BUTTON) {
          this.configReset();
        }
        break;
    }
  }

  /*
   * This function is used to sort any type of story, both spocs and recs.
   * This uses hierarchical sorting, first sorting by priority, then by score within a priority.
   * This function could be sorting an array of spocs or an array of recs.
   * A rec would have priority undefined, and a spoc would probably have a priority set.
   * Priority is sorted ascending, so low numbers are the highest priority.
   * Score is sorted descending, so high numbers are the highest score.
   * Undefined priority values are considered the lowest priority.
   * A negative priority is considered the same as undefined, lowest priority.
   * A negative priority is unlikely and not currently supported or expected.
   * A negative score is a possible use case.
   */
  sortItem(a, b) {
    // If the priorities are the same, sort based on score.
    // If both item priorities are undefined,
    // we can safely sort via score.
    if (a.priority === b.priority) {
      return b.score - a.score;
    } else if (!a.priority || a.priority <= 0) {
      // If priority is undefined or an unexpected value,
      // consider it lowest priority.
      return 1;
    } else if (!b.priority || b.priority <= 0) {
      // Also consider this case lowest priority.
      return -1;
    }
    // Our primary sort for items with priority.
    return a.priority - b.priority;
  }

  async scoreItems(items, type) {
    const spocsPersonalized =
      this.store.getState().Prefs.values?.pocketConfig?.spocsPersonalized;
    const recsPersonalized =
      this.store.getState().Prefs.values?.pocketConfig?.recsPersonalized;
    const personalizedByType =
      type === "feed" ? recsPersonalized : spocsPersonalized;
    // If this is initialized, we are ready to go.
    const personalized = this.store.getState().Personalization.initialized;

    const data = (
      await Promise.all(
        items.map(item => this.scoreItem(item, personalizedByType))
      )
    )
      // Sort by highest scores.
      .sort(this.sortItem);

    return { data, personalized };
  }

  async scoreItem(item, personalizedByType) {
    item.score = item.item_score;
    if (item.score !== 0 && !item.score) {
      item.score = 1;
    }
    if (this.personalized && personalizedByType) {
      await this.recommendationProvider.calculateItemRelevanceScore(item);
    }
    return item;
  }

  async filterBlocked(data) {
    if (data?.length) {
      let flights = this.readDataPref(PREF_FLIGHT_BLOCKS);

      const cachedData = (await this.cache.get()) || {};
      let blocks = cachedData.recsBlocks || {};

      const filteredItems = data.filter(item => {
        const blocked =
          lazy.NewTabUtils.blockedLinks.isBlocked({ url: item.url }) ||
          flights[item.flight_id] ||
          blocks[item.id];
        return !blocked;
      });
      return { data: filteredItems };
    }
    return { data };
  }

  // Add the fetch timestamp property to each spoc returned to communicate how
  // old the spoc is in telemetry when it is used by the client
  addFetchTimestamp(spocs, fetchTimestamp) {
    if (spocs && spocs.length) {
      return {
        data: spocs.map(s => {
          return {
            ...s,
            fetchTimestamp,
          };
        }),
      };
    }
    return { data: spocs };
  }

  // For backwards compatibility, older spoc endpoint don't have flight_id,
  // but instead had campaign_id we can use
  //
  // @param {Object} data  An object that might have a SPOCS array.
  // @returns {Object} An object with a property `data` as the result.
  migrateFlightId(spocs) {
    if (spocs && spocs.length) {
      return {
        data: spocs.map(s => {
          return {
            ...s,
            ...(s.flight_id || s.campaign_id
              ? {
                  flight_id: s.flight_id || s.campaign_id,
                }
              : {}),
            ...(s.caps
              ? {
                  caps: {
                    ...s.caps,
                    flight: s.caps.flight || s.caps.campaign,
                  },
                }
              : {}),
          };
        }),
      };
    }
    return { data: spocs };
  }

  // Filter spocs based on frequency caps
  //
  // @param {Object} data  An object that might have a SPOCS array.
  // @returns {Object} An object with a property `data` as the result, and a property
  //                   `filterItems` as the frequency capped items.
  frequencyCapSpocs(spocs) {
    if (spocs?.length) {
      const impressions = this.readDataPref(PREF_SPOC_IMPRESSIONS);
      const caps = [];
      const result = spocs.filter(s => {
        const isBelow = this.isBelowFrequencyCap(impressions, s);
        if (!isBelow) {
          caps.push(s);
        }
        return isBelow;
      });
      // send caps to redux if any.
      if (caps.length) {
        this.store.dispatch({
          type: at.DISCOVERY_STREAM_SPOCS_CAPS,
          data: caps,
        });
      }
      return { data: result, filtered: caps };
    }
    return { data: spocs, filtered: [] };
  }

  // Frequency caps are based on flight, which may include multiple spocs.
  // We currently support two types of frequency caps:
  // - lifetime: Indicates how many times spocs from a flight can be shown in total
  // - period: Indicates how many times spocs from a flight can be shown within a period
  //
  // So, for example, the feed configuration below defines that for flight 1 no more
  // than 5 spocs can be shown in total, and no more than 2 per hour.
  // "flight_id": 1,
  // "caps": {
  //  "lifetime": 5,
  //  "flight": {
  //    "count": 2,
  //    "period": 3600
  //  }
  // }
  isBelowFrequencyCap(impressions, spoc) {
    const flightImpressions = impressions[spoc.flight_id];
    if (!flightImpressions) {
      return true;
    }

    const lifetime = spoc.caps && spoc.caps.lifetime;

    const lifeTimeCap = Math.min(
      lifetime || MAX_LIFETIME_CAP,
      MAX_LIFETIME_CAP
    );
    const lifeTimeCapExceeded = flightImpressions.length >= lifeTimeCap;
    if (lifeTimeCapExceeded) {
      return false;
    }

    const flightCap = spoc.caps && spoc.caps.flight;
    if (flightCap) {
      const flightCapExceeded =
        flightImpressions.filter(i => Date.now() - i < flightCap.period * 1000)
          .length >= flightCap.count;
      return !flightCapExceeded;
    }
    return true;
  }

  async retryFeed(feed) {
    const { url } = feed;
    const newFeed = await this.getComponentFeed(url);
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.DISCOVERY_STREAM_FEED_UPDATE,
        data: {
          feed: newFeed,
          url,
        },
      })
    );
  }

  getExperimentInfo() {
    const pocketNewtabExperiment = lazy.ExperimentAPI.getExperimentMetaData({
      featureId: "pocketNewtab",
    });

    const pocketNewtabRollout = lazy.ExperimentAPI.getRolloutMetaData({
      featureId: "pocketNewtab",
    });

    // We want to know if the user is in an experiment or rollout,
    // but we prioritize experiments over rollouts.
    const experimentMetaData = pocketNewtabExperiment || pocketNewtabRollout;

    let experimentName = experimentMetaData?.slug || "";
    let experimentBranch = experimentMetaData?.branch?.slug || "";

    return {
      experimentName,
      experimentBranch,
    };
  }

  // eslint-disable-next-line max-statements
  async getComponentFeed(feedUrl, isStartup) {
    const cachedData = (await this.cache.get()) || {};
    const prefs = this.store.getState().Prefs.values;
    const sectionsEnabled = prefs[PREF_SECTIONS_ENABLED];
    let isFakespot;
    const selectedFeedPref = prefs[PREF_CONTEXTUAL_CONTENT_SELECTED_FEED];
    let sections = [];
    const { feeds } = cachedData;

    let feed = feeds ? feeds[feedUrl] : null;
    if (this.isExpired({ cachedData, key: "feed", url: feedUrl, isStartup })) {
      const options = this.formatComponentFeedRequest(
        cachedData.sectionPersonalization
      );

      const feedResponse = await this.fetchFromEndpoint(feedUrl, options);

      if (feedResponse) {
        const { settings = {} } = feedResponse;
        let { recommendations } = feedResponse;
        if (this.isMerino) {
          recommendations = feedResponse.data.map(item => ({
            id: item.corpusItemId || item.scheduledCorpusItemId || item.tileId,
            scheduled_corpus_item_id: item.scheduledCorpusItemId,
            corpus_item_id: item.corpusItemId,
            url: item.url,
            title: item.title,
            topic: item.topic,
            excerpt: item.excerpt,
            publisher: item.publisher,
            raw_image_src: item.imageUrl,
            received_rank: item.receivedRank,
            recommended_at: feedResponse.recommendedAt,
            icon_src: item.iconUrl,
            isTimeSensitive: item.isTimeSensitive,
          }));
          if (feedResponse.feeds && selectedFeedPref && !sectionsEnabled) {
            isFakespot = selectedFeedPref === "fakespot";
            const keyName = isFakespot ? "products" : "recommendations";
            const selectedFeedResponse = feedResponse.feeds[selectedFeedPref];
            selectedFeedResponse?.[keyName]?.forEach(item =>
              recommendations.push({
                id: isFakespot
                  ? item.id
                  : item.corpusItemId ||
                    item.scheduledCorpusItemId ||
                    item.tileId,
                scheduled_corpus_item_id: item.scheduledCorpusItemId,
                corpus_item_id: item.corpusItemId,
                url: item.url,
                title: item.title,
                topic: item.topic,
                excerpt: item.excerpt,
                publisher: item.publisher,
                raw_image_src: item.imageUrl,
                received_rank: item.receivedRank,
                recommended_at: feedResponse.recommendedAt,
                // property to determine if rec is used in ListFeed or not
                feedName: selectedFeedPref,
                category: item.category,
                icon_src: item.iconUrl,
                isTimeSensitive: item.isTimeSensitive,
              })
            );

            const prevTitle = prefs[PREF_CONTEXTUAL_CONTENT_LISTFEED_TITLE];

            const feedTitle = isFakespot
              ? selectedFeedResponse.headerCopy
              : selectedFeedResponse.title;

            if (feedTitle && feedTitle !== prevTitle) {
              this.handleListfeedStrings(selectedFeedResponse, isFakespot);
            }
          }

          if (sectionsEnabled) {
            for (const [sectionKey, sectionData] of Object.entries(
              feedResponse.feeds
            )) {
              if (sectionData) {
                for (const item of sectionData.recommendations) {
                  recommendations.push({
                    id:
                      item.corpusItemId ||
                      item.scheduledCorpusItemId ||
                      item.tileId,
                    scheduled_corpus_item_id: item.scheduledCorpusItemId,
                    corpus_item_id: item.corpusItemId,
                    url: item.url,
                    title: item.title,
                    topic: item.topic,
                    excerpt: item.excerpt,
                    publisher: item.publisher,
                    raw_image_src: item.imageUrl,
                    received_rank: item.receivedRank,
                    recommended_at: feedResponse.recommendedAt,
                    section: sectionKey,
                    icon_src: item.iconUrl,
                    isTimeSensitive: item.isTimeSensitive,
                  });
                }
                sections.push({
                  sectionKey,
                  title: sectionData.title,
                  subtitle: sectionData.subtitle || "",
                  receivedRank: sectionData.receivedFeedRank,
                  layout: sectionData.layout,
                  // property if initially shown (with interest picker)
                  visible: sectionData.isInitiallyVisible,
                });
              }
            }
          }
        } else if (this.isBff) {
          recommendations = feedResponse.data.map(item => ({
            id: item.tileId,
            url: item.url,
            title: item.title,
            excerpt: item.excerpt,
            publisher: item.publisher,
            time_to_read: item.timeToRead,
            raw_image_src: item.imageUrl,
            recommendation_id: item.recommendationId,
          }));
        }
        const { data: scoredItems, personalized } = await this.scoreItems(
          recommendations,
          "feed"
        );

        if (sections.length) {
          const visibleSections = sections
            .filter(({ visible }) => visible)
            .sort((a, b) => a.receivedRank - b.receivedRank)
            .map(section => section.sectionKey)
            .join(",");

          // after the request only show the sections that are
          // initially visible and only keep the initial order (determined by the server)
          this.store.dispatch(
            ac.SetPref(PREF_VISIBLE_SECTIONS, visibleSections)
          );
        }

        // This assigns the section title to the interestPicker.sections
        // object to more easily access the title in JSX files
        if (
          feedResponse.interestPicker &&
          feedResponse.interestPicker.sections
        ) {
          feedResponse.interestPicker.sections =
            feedResponse.interestPicker.sections.map(section => {
              const { sectionId } = section;
              const title = sections.find(
                ({ sectionKey }) => sectionKey === sectionId
              )?.title;
              return { sectionId, title };
            });
        }

        // We can cleanup any impressions we have that are old before we rotate.
        // In theory we can do this anywhere, but doing it just before rotate is optimal.
        // Rotate is also the only place that uses these impressions.
        await this.cleanUpTopRecImpressions();
        const rotatedItems = await this.rotate(scoredItems);

        const { data: filteredResults } =
          await this.filterBlocked(rotatedItems);
        this.componentFeedFetched = true;
        feed = {
          lastUpdated: Date.now(),
          personalized,
          data: {
            settings,
            sections,
            interestPicker: feedResponse.interestPicker || {},
            recommendations: filteredResults,
            surfaceId: feedResponse.surfaceId || "",
            status: "success",
          },
        };
      } else {
        console.error("No response for feed");
      }
    }

    // if surfaceID is availible either through the cache or the response set value in Glean
    if (prefs[PREF_PRIVATE_PING_ENABLED] && feed.data.surfaceId) {
      Glean.newtabContent.surfaceId.set(feed.data.surfaceId);
      this.store.dispatch(ac.SetPref(PREF_SURFACE_ID, feed.data.surfaceId));
    }

    // If we have no feed at this point, both fetch and cache failed for some reason.
    return (
      feed || {
        data: {
          status: "failed",
        },
      }
    );
  }

  handleListfeedStrings(feedResponse, isFakespot) {
    if (isFakespot) {
      this.store.dispatch(
        ac.SetPref(
          PREF_CONTEXTUAL_CONTENT_LISTFEED_TITLE,
          feedResponse.headerCopy
        )
      );
      this.store.dispatch(
        ac.SetPref(
          PREF_CONTEXTUAL_CONTENT_FAKESPOT_CATEGORY,
          feedResponse.defaultCategoryName
        )
      );
      this.store.dispatch(
        ac.SetPref(
          PREF_CONTEXTUAL_CONTENT_FAKESPOT_FOOTER,
          feedResponse.footerCopy
        )
      );
      this.store.dispatch(
        ac.SetPref(
          PREF_CONTEXTUAL_CONTENT_FAKESPOT_CTA_COPY,
          feedResponse.cta.ctaCopy
        )
      );
      this.store.dispatch(
        ac.SetPref(
          PREF_CONTEXTUAL_CONTENT_FAKESPOT_CTA_URL,
          feedResponse.cta.url
        )
      );
    } else {
      this.store.dispatch(
        ac.SetPref(PREF_CONTEXTUAL_CONTENT_LISTFEED_TITLE, feedResponse.title)
      );
    }
  }

  formatComponentFeedRequest(sectionPersonalization = {}) {
    const prefs = this.store.getState().Prefs.values;
    const headers = new Headers();
    if (this.isMerino) {
      const topicSelectionEnabled = prefs[PREF_TOPIC_SELECTION_ENABLED];
      const topicsString = prefs[PREF_SELECTED_TOPICS];
      const topics = topicSelectionEnabled
        ? topicsString
            .split(",")
            .map(s => s.trim())
            .filter(item => item)
        : [];

      // Should we pass the experiment branch and slug to the Merino feed request.
      const prefMerinoFeedExperiment = Services.prefs.getBoolPref(
        PREF_MERINO_FEED_EXPERIMENT
      );

      // convert section to array to match what merino is expecting
      const sections = Object.entries(sectionPersonalization).map(
        ([sectionId, data]) => ({
          sectionId,
          isFollowed: data.isFollowed,
          isBlocked: data.isBlocked,
          ...(data.followedAt && { followedAt: data.followedAt }),
        })
      );

      // To display the inline interest picker pass `enableInterestPicker` into the request
      const interestPickerEnabled = prefs[PREF_INTEREST_PICKER_ENABLED];

      const requestMetadata = {
        utc_offset: lazy.NewTabUtils.getUtcOffset(),
        coarse_os: lazy.NewTabUtils.normalizeOs(),
        coarse_os_version: lazy.ClientEnvironmentBase.os.version,
        surface_id: prefs[PREF_SURFACE_ID] || "",
      };

      headers.append("content-type", "application/json");
      let body = {
        ...(prefMerinoFeedExperiment ? this.getExperimentInfo() : {}),
        ...requestMetadata,
        locale: this.locale,
        region: this.region,
        topics,
        sections,
        enableInterestPicker: !!interestPickerEnabled,
      };

      const sectionsEnabled = prefs[PREF_SECTIONS_ENABLED];

      // Should we pass the feed param to the merino request
      const contextualContentEnabled = prefs[PREF_CONTEXTUAL_CONTENT_ENABLED];
      const selectedFeed = prefs[PREF_CONTEXTUAL_CONTENT_SELECTED_FEED];
      const isFakespot = selectedFeed === "fakespot";
      const fakespotEnabled = prefs[PREF_FAKESPOT_ENABLED];

      const shouldFetchTBRFeed =
        (contextualContentEnabled && !isFakespot) ||
        (contextualContentEnabled && isFakespot && fakespotEnabled);

      if (shouldFetchTBRFeed) {
        body.feeds = [selectedFeed];
      }
      if (sectionsEnabled) {
        // if sections is enabled, it should override the TBR feed
        body.feeds = ["sections"];
      }

      return {
        method: "POST",
        headers,
        body: JSON.stringify(body),
      };
    } else if (this.isBff) {
      const oAuthConsumerKey = Services.prefs.getStringPref(
        "extensions.pocket.oAuthConsumerKeyBff"
      );
      headers.append("consumer_key", oAuthConsumerKey);
      return {
        method: "GET",
        headers,
      };
    }
    return {};
  }

  /**
   * Called at startup to update cached data in the background.
   */
  async _maybeUpdateCachedData() {
    const expirationPerComponent = await this._checkExpirationPerComponent();
    // Pass in `store.dispatch` to send the updates only to main
    if (expirationPerComponent.spocs) {
      await this.loadSpocs(this.store.dispatch);
    }
    if (expirationPerComponent.feeds) {
      await this.loadComponentFeeds(this.store.dispatch);
    }
  }

  async scoreFeeds(feedsState) {
    if (feedsState.data) {
      const feeds = {};
      const feedsPromises = Object.keys(feedsState.data).map(url => {
        let feed = feedsState.data[url];
        if (feed.personalized) {
          // Feed was previously personalized then cached, we don't need to do this again.
          return Promise.resolve();
        }
        const feedPromise = this.scoreItems(feed.data.recommendations, "feed");
        feedPromise.then(({ data: scoredItems, personalized }) => {
          feed = {
            ...feed,
            personalized,
            data: {
              ...feed.data,
              recommendations: scoredItems,
            },
          };

          feeds[url] = feed;

          this.store.dispatch(
            ac.AlsoToPreloaded({
              type: at.DISCOVERY_STREAM_FEED_UPDATE,
              data: {
                feed,
                url,
              },
            })
          );
        });
        return feedPromise;
      });
      await Promise.all(feedsPromises);
      await this.cache.set("feeds", feeds);
    }
  }

  async scoreSpocs(spocsState) {
    const spocsResultPromises = this.getPlacements().map(async placement => {
      const nextSpocs = spocsState.data[placement.name] || {};
      const { items } = nextSpocs;

      if (nextSpocs.personalized || !items || !items.length) {
        return;
      }

      const { data: scoreResult, personalized } = await this.scoreItems(
        items,
        "spocs"
      );

      spocsState.data = {
        ...spocsState.data,
        [placement.name]: {
          ...nextSpocs,
          personalized,
          items: scoreResult,
        },
      };
    });
    await Promise.all(spocsResultPromises);

    // Update cache here so we don't need to re calculate scores on loads from cache.
    // Related Bug 1606276
    await this.cache.set("spocs", {
      lastUpdated: spocsState.lastUpdated,
      spocs: spocsState.data,
    });
    this.store.dispatch(
      ac.AlsoToPreloaded({
        type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
        data: {
          lastUpdated: spocsState.lastUpdated,
          spocs: spocsState.data,
        },
      })
    );
  }

  /**
   * @typedef {Object} RefreshAll
   * @property {boolean} updateOpenTabs - Sends updates to open tabs immediately if true,
   *                                      updates in background if false
   * @property {boolean} isStartup - When the function is called at browser startup
   *
   * Refreshes component feeds, and spocs in order if caches have expired.
   * @param {RefreshAll} options
   */
  async refreshAll(options = {}) {
    const { updateOpenTabs, isStartup } = options;

    const dispatch = updateOpenTabs
      ? action => this.store.dispatch(ac.BroadcastToContent(action))
      : this.store.dispatch;

    this.loadLayout(dispatch, isStartup);
    if (this.showStories || this.showTopsites) {
      const spocsStartupCacheEnabled =
        this.store.getState().Prefs.values[PREF_SPOCS_STARTUP_CACHE_ENABLED];
      const promises = [];

      // We could potentially have either or both sponsored topsites or stories.
      // We only make one fetch, and control which to request when we fetch.
      // So for now we only care if we need to make this request at all.
      const spocsPromise = this.loadSpocs(
        dispatch,
        isStartup && spocsStartupCacheEnabled
      ).catch(error =>
        console.error("Error trying to load spocs feeds:", error)
      );
      promises.push(spocsPromise);
      if (this.showStories) {
        const storiesPromise = this.loadComponentFeeds(
          dispatch,
          isStartup
        ).catch(error =>
          console.error("Error trying to load component feeds:", error)
        );
        promises.push(storiesPromise);
      }
      await Promise.all(promises);
      if (isStartup) {
        // We don't pass isStartup in _maybeUpdateCachedData on purpose,
        // because startup loads have a longer cache timer,
        // and we want this to update in the background sooner.
        await this._maybeUpdateCachedData();
      }
    }
  }

  // We have to rotate stories on the client so that
  // active stories are at the front of the list, followed by stories that have expired
  // impressions i.e. have been displayed for longer than DEFAULT_RECS_ROTATION_TIME.
  async rotate(recommendations) {
    const cachedData = (await this.cache.get()) || {};
    const impressions = cachedData.recsImpressions;

    // If we have no impressions, don't bother checking.
    if (!impressions) {
      return recommendations;
    }

    const expired = [];
    const active = [];
    for (const item of recommendations) {
      if (
        impressions[item.id] &&
        Date.now() - impressions[item.id] >= DEFAULT_RECS_ROTATION_TIME
      ) {
        expired.push(item);
      } else {
        active.push(item);
      }
    }
    return active.concat(expired);
  }

  enableStories() {
    if (this.config.enabled) {
      // If stories are being re enabled, ensure we have stories.
      this.refreshAll({ updateOpenTabs: true });
    }
  }

  async enable(options = {}) {
    await this.refreshAll(options);
    this.loaded = true;
  }

  async reset() {
    this.resetDataPrefs();
    await this.resetCache();
    this.resetState();
  }

  async resetCache() {
    await this.resetAllCache();
  }

  async resetContentCache() {
    await this.cache.set("feeds", {});
    await this.cache.set("spocs", {});
    await this.cache.set("sov", {});
    await this.cache.set("recsImpressions", {});
  }

  async resetBlocks() {
    await this.cache.set("recsBlocks", {});
    const cachedData = (await this.cache.get()) || {};
    let blocks = cachedData.recsBlocks || {};

    this.store.dispatch({
      type: at.DISCOVERY_STREAM_DEV_BLOCKS,
      data: blocks,
    });
    // Update newtab after clearing blocks.
    await this.refreshAll({ updateOpenTabs: true });
  }

  async resetContentFeed() {
    await this.cache.set("feeds", {});
  }

  async resetAllCache() {
    await this.resetContentCache();
    // Reset in-memory caches.
    this._isBff = undefined;
    this._isMerino = undefined;
    this._spocsCacheUpdateTime = undefined;
  }

  resetDataPrefs() {
    this.writeDataPref(PREF_SPOC_IMPRESSIONS, {});
    this.writeDataPref(PREF_FLIGHT_BLOCKS, {});
  }

  resetState() {
    // Reset reducer
    this.store.dispatch(
      ac.BroadcastToContent({ type: at.DISCOVERY_STREAM_LAYOUT_RESET })
    );
    this.setupPrefs(false /* isStartup */);
    this.loaded = false;
  }

  async onPrefChange() {
    // We always want to clear the cache/state if the pref has changed
    await this.reset();
    if (this.config.enabled) {
      // Load data from all endpoints
      await this.enable({ updateOpenTabs: true });
    }
  }

  // This is a request to change the config from somewhere.
  // Can be from a specific pref related to Discovery Stream,
  // or can be a generic request from an external feed that
  // something changed.
  configReset() {
    this._prefCache.config = null;
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.DISCOVERY_STREAM_CONFIG_CHANGE,
        data: this.config,
      })
    );
  }

  recordFlightImpression(flightId) {
    let impressions = this.readDataPref(PREF_SPOC_IMPRESSIONS);

    const timeStamps = impressions[flightId] || [];
    timeStamps.push(Date.now());
    impressions = { ...impressions, [flightId]: timeStamps };

    this.writeDataPref(PREF_SPOC_IMPRESSIONS, impressions);
  }

  async recordTopRecImpression(recId) {
    const cachedData = (await this.cache.get()) || {};
    let impressions = cachedData.recsImpressions || {};

    if (!impressions[recId]) {
      impressions = { ...impressions, [recId]: Date.now() };
      await this.cache.set("recsImpressions", impressions);

      this.store.dispatch({
        type: at.DISCOVERY_STREAM_DEV_IMPRESSIONS,
        data: impressions,
      });
    }
  }

  async recordBlockRecId(recId) {
    const cachedData = (await this.cache.get()) || {};
    let blocks = cachedData.recsBlocks || {};

    if (!blocks[recId]) {
      blocks[recId] = 1;
      await this.cache.set("recsBlocks", blocks);

      this.store.dispatch({
        type: at.DISCOVERY_STREAM_DEV_BLOCKS,
        data: blocks,
      });
    }
  }

  recordBlockFlightId(flightId) {
    const unifiedAdsEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];

    const flights = this.readDataPref(PREF_FLIGHT_BLOCKS);
    if (!flights[flightId]) {
      flights[flightId] = 1;
      this.writeDataPref(PREF_FLIGHT_BLOCKS, flights);

      if (unifiedAdsEnabled) {
        let blockList =
          this.store.getState().Prefs.values[PREF_UNIFIED_ADS_BLOCKED_LIST];

        let blockedAdsArray = [];

        // If prev ads have been blocked, convert CSV string to array
        if (blockList !== "") {
          blockedAdsArray = blockList
            .split(",")
            .map(s => s.trim())
            .filter(item => item);
        }

        blockedAdsArray.push(flightId);

        this.store.dispatch(
          ac.SetPref(PREF_UNIFIED_ADS_BLOCKED_LIST, blockedAdsArray.join(","))
        );
      }
    }
  }

  cleanUpFlightImpressionPref(data) {
    let flightIds = [];
    this.placementsForEach(placement => {
      const newSpocs = data[placement.name];
      if (!newSpocs) {
        return;
      }

      const items = newSpocs.items || [];
      flightIds = [...flightIds, ...items.map(s => `${s.flight_id}`)];
    });
    if (flightIds && flightIds.length) {
      this.cleanUpImpressionPref(
        id => !flightIds.includes(id),
        PREF_SPOC_IMPRESSIONS
      );
    }
  }

  // Clean up rec impressions that are old.
  async cleanUpTopRecImpressions() {
    await this.cleanUpImpressionCache(
      impression =>
        Date.now() - impression >= DEFAULT_RECS_IMPRESSION_EXPIRE_TIME,
      "recsImpressions"
    );
  }

  writeDataPref(pref, impressions) {
    this.store.dispatch(ac.SetPref(pref, JSON.stringify(impressions)));
  }

  readDataPref(pref) {
    const prefVal = this.store.getState().Prefs.values[pref];
    return prefVal ? JSON.parse(prefVal) : {};
  }

  async cleanUpImpressionCache(isExpired, cacheKey) {
    const cachedData = (await this.cache.get()) || {};
    let impressions = cachedData[cacheKey];
    let changed = false;

    if (impressions) {
      Object.keys(impressions).forEach(id => {
        if (isExpired(impressions[id])) {
          changed = true;
          delete impressions[id];
        }
      });

      if (changed) {
        await this.cache.set(cacheKey, impressions);

        this.store.dispatch({
          type: at.DISCOVERY_STREAM_DEV_IMPRESSIONS,
          data: impressions,
        });
      }
    }
  }

  cleanUpImpressionPref(isExpired, pref) {
    const impressions = this.readDataPref(pref);
    let changed = false;

    Object.keys(impressions).forEach(id => {
      if (isExpired(id)) {
        changed = true;
        delete impressions[id];
      }
    });

    if (changed) {
      this.writeDataPref(pref, impressions);
    }
  }

  onCollectionsChanged() {
    // Update layout, and reload any off screen tabs.
    // This does not change any existing open tabs.
    // It also doesn't update any spoc or rec data, just the layout.
    const dispatch = action => this.store.dispatch(ac.AlsoToPreloaded(action));
    this.loadLayout(dispatch, false);
  }

  async retreiveProfileAge() {
    let profileAccessor = await lazy.ProfileAge();
    let profileCreateTime = await profileAccessor.created;
    let timeNow = new Date().getTime();
    let profileAge = timeNow - profileCreateTime;
    // Convert milliseconds to days
    return profileAge / 1000 / 60 / 60 / 24;
  }

  topicSelectionImpressionEvent() {
    let counter =
      this.store.getState().Prefs.values[TOPIC_SELECTION_DISPLAY_COUNT];

    const newCount = counter + 1;
    this.store.dispatch(ac.SetPref(TOPIC_SELECTION_DISPLAY_COUNT, newCount));
    this.store.dispatch(
      ac.SetPref(TOPIC_SELECTION_LAST_DISPLAYED, `${new Date().getTime()}`)
    );
  }

  topicSelectionMaybeLaterEvent() {
    const age = this.retreiveProfileAge();
    const newProfile = age <= 1;
    const day = 24 * 60 * 60 * 1000;
    this.store.dispatch(
      ac.SetPref(
        TOPIC_SELECTION_DISPLAY_TIMEOUT,
        newProfile ? 3 * day : 7 * day
      )
    );
  }

  async onPrefChangedAction(action) {
    switch (action.data.name) {
      case PREF_CONFIG:
      case PREF_ENABLED:
      case PREF_HARDCODED_BASIC_LAYOUT:
      case PREF_SPOCS_ENDPOINT:
      case PREF_SPOCS_ENDPOINT_QUERY:
      case PREF_SPOCS_CLEAR_ENDPOINT:
      case PREF_ENDPOINTS:
      case PREF_LAYOUT_EXPERIMENT_A:
      case PREF_LAYOUT_EXPERIMENT_B:
      case PREF_SPOC_POSITIONS:
      case PREF_UNIFIED_ADS_SPOCS_ENABLED:
      case PREF_CONTEXTUAL_CONTENT_ENABLED:
      case PREF_CONTEXTUAL_CONTENT_SELECTED_FEED:
      case PREF_SECTIONS_ENABLED:
      case PREF_INTEREST_PICKER_ENABLED:
        // This is a config reset directly related to Discovery Stream pref.
        this.configReset();
        break;
      case PREF_COLLECTIONS_ENABLED:
        this.onCollectionsChanged();
        break;
      case PREF_SELECTED_TOPICS:
        this.store.dispatch(
          ac.BroadcastToContent({ type: at.DISCOVERY_STREAM_LAYOUT_RESET })
        );
        // Ensure at least a little bit of loading is seen, if this is too fast,
        // it's not clear to the user what just happened.
        this.store.dispatch(
          ac.BroadcastToContent({
            type: at.DISCOVERY_STREAM_TOPICS_LOADING,
            data: true,
          })
        );
        setTimeout(() => {
          this.store.dispatch(
            ac.BroadcastToContent({
              type: at.DISCOVERY_STREAM_TOPICS_LOADING,
              data: false,
            })
          );
        }, TOPIC_LOADING_TIMEOUT);
        this.loadLayout(
          a => this.store.dispatch(ac.BroadcastToContent(a)),
          false
        );

        // when topics have been updated, make a new request from merino and clear impression cap
        await this.cache.set("recsImpressions", {});
        await this.resetContentFeed();
        this.refreshAll({ updateOpenTabs: true });
        break;
      case PREF_USER_TOPSITES:
      case PREF_SYSTEM_TOPSITES:
        if (
          !(
            this.showTopsites ||
            (this.showStories && this.showSponsoredStories)
          )
        ) {
          // Ensure we delete any remote data potentially related to spocs.
          this.clearSpocs();
        }
        break;
      case PREF_USER_TOPSTORIES:
      case PREF_SYSTEM_TOPSTORIES:
        if (
          !(
            this.showStories ||
            (this.showTopsites && this.showSponsoredTopsites)
          )
        ) {
          // Ensure we delete any remote data potentially related to spocs.
          this.clearSpocs();
        }
        if (action.data.value) {
          this.enableStories();
        }
        break;
      // Check if spocs was disabled. Remove them if they were.
      case PREF_SHOW_SPONSORED:
      case PREF_SHOW_SPONSORED_TOPSITES: {
        const dispatch = update =>
          this.store.dispatch(ac.BroadcastToContent(update));
        // We refresh placements data because one of the spocs were turned off.
        this.updatePlacements(
          dispatch,
          this.store.getState().DiscoveryStream.layout
        );
        // Currently the order of this is important.
        // We need to check this after updatePlacements is called,
        // because some of the spoc logic depends on the result of placement updates.
        if (
          !(
            (this.showSponsoredStories ||
              (this.showTopsites && this.showSponsoredTopsites)) &&
            (this.showSponsoredTopsites ||
              (this.showStories && this.showSponsoredStories))
          )
        ) {
          // Ensure we delete any remote data potentially related to spocs.
          this.clearSpocs();
        }
        // Placements have changed so consider spocs expired, and reload them.
        await this.cache.set("spocs", {});
        await this.loadSpocs(dispatch);
        break;
      }
    }
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        // During the initialization of Firefox:
        // 1. Set-up listeners and initialize the redux state for config;
        this.setupConfig(true /* isStartup */);
        this.setupPrefs(true /* isStartup */);
        // 2. If config.enabled is true, start loading data.
        if (this.config.enabled) {
          await this.enable({ updateOpenTabs: true, isStartup: true });
        }
        await this.configureFollowedSections();
        Services.prefs.addObserver(PREF_POCKET_BUTTON, this);
        // This function is async but just for devtools,
        // so we don't need to wait for it.
        this.setupDevtoolsState(true /* isStartup */);
        break;
      case at.TOPIC_SELECTION_MAYBE_LATER:
        this.topicSelectionMaybeLaterEvent();
        break;
      case at.DISCOVERY_STREAM_DEV_BLOCKS_RESET:
        await this.resetBlocks();
        break;
      case at.DISCOVERY_STREAM_DEV_SYSTEM_TICK:
      case at.SYSTEM_TICK:
        // Only refresh if we loaded once in .enable()
        if (
          this.config.enabled &&
          this.loaded &&
          (await this.checkIfAnyCacheExpired())
        ) {
          await this.refreshAll({ updateOpenTabs: false });
        }
        break;
      case at.DISCOVERY_STREAM_DEV_SYNC_RS:
        lazy.RemoteSettings.pollChanges();
        break;
      case at.DISCOVERY_STREAM_DEV_EXPIRE_CACHE:
        // Personalization scores update at a slower interval than content, so in order to debug,
        // we want to be able to expire just content to trigger the earlier expire times.
        await this.resetContentCache();
        break;
      case at.DISCOVERY_STREAM_DEV_SHOW_PLACEHOLDER: {
        // We want to display the loading state permanently, for dev purposes.
        // We do this by resetting everything, loading the layout, and nothing else.
        // This essentially hangs because we never triggered the content load.
        await this.reset();
        this.loadLayout(
          a => this.store.dispatch(ac.BroadcastToContent(a)),
          false
        );
        break;
      }
      case at.DISCOVERY_STREAM_CONFIG_SET_VALUE:
        // Use the original string pref to then set a value instead of
        // this.config which has some modifications
        this.store.dispatch(
          ac.SetPref(
            PREF_CONFIG,
            JSON.stringify({
              ...JSON.parse(this.store.getState().Prefs.values[PREF_CONFIG]),
              [action.data.name]: action.data.value,
            })
          )
        );
        break;
      case at.DISCOVERY_STREAM_POCKET_STATE_INIT:
        this.setupPocketState(action.meta.fromTarget);
        break;
      case at.DISCOVERY_STREAM_PERSONALIZATION_UPDATED:
        if (this.personalized) {
          const { feeds, spocs } = this.store.getState().DiscoveryStream;
          const spocsPersonalized =
            this.store.getState().Prefs.values?.pocketConfig?.spocsPersonalized;
          const recsPersonalized =
            this.store.getState().Prefs.values?.pocketConfig?.recsPersonalized;
          if (recsPersonalized && feeds.loaded) {
            this.scoreFeeds(feeds);
          }
          if (spocsPersonalized && spocs.loaded) {
            this.scoreSpocs(spocs);
          }
        }
        break;
      case at.DISCOVERY_STREAM_CONFIG_RESET:
        // This is a generic config reset likely related to an external feed pref.
        this.configReset();
        break;
      case at.DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS:
        this.resetConfigDefauts();
        break;
      case at.DISCOVERY_STREAM_RETRY_FEED:
        this.retryFeed(action.data.feed);
        break;
      case at.DISCOVERY_STREAM_CONFIG_CHANGE:
        // When the config pref changes, load or unload data as needed.
        await this.onPrefChange();
        break;
      case at.DISCOVERY_STREAM_IMPRESSION_STATS:
        if (
          action.data.tiles &&
          action.data.tiles[0] &&
          action.data.tiles[0].id
        ) {
          this.recordTopRecImpression(action.data.tiles[0].id);
        }
        break;
      case at.DISCOVERY_STREAM_SPOC_IMPRESSION:
        if (this.showSpocs) {
          this.recordFlightImpression(action.data.flightId);

          // Apply frequency capping to SPOCs in the redux store, only update the
          // store if the SPOCs are changed.
          const spocsState = this.store.getState().DiscoveryStream.spocs;

          let frequencyCapped = [];
          this.placementsForEach(placement => {
            const spocs = spocsState.data[placement.name];
            if (!spocs || !spocs.items) {
              return;
            }

            const { data: capResult, filtered } = this.frequencyCapSpocs(
              spocs.items
            );
            frequencyCapped = [...frequencyCapped, ...filtered];

            spocsState.data = {
              ...spocsState.data,
              [placement.name]: {
                ...spocs,
                items: capResult,
              },
            };
          });

          if (frequencyCapped.length) {
            // Update cache here so we don't need to re calculate frequency caps on loads from cache.
            await this.cache.set("spocs", {
              lastUpdated: spocsState.lastUpdated,
              spocs: spocsState.data,
            });

            this.store.dispatch(
              ac.AlsoToPreloaded({
                type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
                data: {
                  lastUpdated: spocsState.lastUpdated,
                  spocs: spocsState.data,
                },
              })
            );
          }
        }
        break;

      // This is fired from the browser, it has no concept of spocs, flights or pocket.
      // We match the blocked url with our available story urls to see if there is a match.
      // I suspect we *could* instead do this in BLOCK_URL but I'm not sure.
      case at.PLACES_LINK_BLOCKED: {
        const feedsState = this.store.getState().DiscoveryStream.feeds;
        const feeds = {};

        for (const url of Object.keys(feedsState.data)) {
          let feed = feedsState.data[url];

          const { data: filteredResults } = await this.filterBlocked(
            feed.data.recommendations
          );

          feed = {
            ...feed,
            data: {
              ...feed.data,
              recommendations: filteredResults,
            },
          };

          feeds[url] = feed;
        }

        await this.cache.set("feeds", feeds);

        if (this.showSpocs) {
          let blockedItems = [];
          const spocsState = this.store.getState().DiscoveryStream.spocs;

          this.placementsForEach(placement => {
            const spocs = spocsState.data[placement.name];
            if (spocs && spocs.items && spocs.items.length) {
              const blockedResults = [];
              const blocks = spocs.items.filter(s => {
                const blocked = s.url === action.data.url;
                if (!blocked) {
                  blockedResults.push(s);
                }
                return blocked;
              });

              blockedItems = [...blockedItems, ...blocks];

              spocsState.data = {
                ...spocsState.data,
                [placement.name]: {
                  ...spocs,
                  items: blockedResults,
                },
              };
            }
          });

          if (blockedItems.length) {
            // Update cache here so we don't need to re calculate blocks on loads from cache.
            await this.cache.set("spocs", {
              lastUpdated: spocsState.lastUpdated,
              spocs: spocsState.data,
            });

            // If we're blocking a spoc, we want open tabs to have
            // a slightly different treatment from future tabs.
            // AlsoToPreloaded updates the source data and preloaded tabs with a new spoc.
            // BroadcastToContent updates open tabs with a non spoc instead of a new spoc.
            this.store.dispatch(
              ac.AlsoToPreloaded({
                type: at.DISCOVERY_STREAM_LINK_BLOCKED,
                data: action.data,
              })
            );
            this.store.dispatch(
              ac.BroadcastToContent({
                type: at.DISCOVERY_STREAM_SPOC_BLOCKED,
                data: action.data,
              })
            );
            break;
          }
        }

        this.store.dispatch(
          ac.BroadcastToContent({
            type: at.DISCOVERY_STREAM_LINK_BLOCKED,
            data: action.data,
          })
        );
        break;
      }
      case at.UNINIT:
        // When this feed is shutting down:
        this.uninitPrefs();
        this._recommendationProvider = null;
        Services.prefs.removeObserver(PREF_POCKET_BUTTON, this);
        break;
      case at.BLOCK_URL: {
        // If we block a story that also has a flight_id
        // we want to record that as blocked too.
        // This is because a single flight might have slightly different urls.
        for (const site of action.data) {
          const { flight_id, tile_id } = site;
          if (flight_id) {
            this.recordBlockFlightId(flight_id);
          }
          if (tile_id) {
            await this.recordBlockRecId(tile_id);
          }
        }
        break;
      }
      case at.PREF_CHANGED:
        await this.onPrefChangedAction(action);
        if (action.data.name === "pocketConfig") {
          await this.onPrefChange();
          this.setupPrefs(false /* isStartup */);
        }
        break;
      case at.TOPIC_SELECTION_IMPRESSION:
        this.topicSelectionImpressionEvent();
        break;
      case at.SECTION_PERSONALIZATION_UPDATE:
        await this.cache.set("sectionPersonalization", action.data);
    }
  }
}

/* This function generates a hardcoded layout each call.
   This is because modifying the original object would
   persist across pref changes and system_tick updates.

   NOTE: There is some branching logic in the template.
     `spocsUrl` Changing the url for spocs is used for adding a siteId query param.
     `feedUrl` Where to fetch stories from.
     `items` How many items to include in the primary card grid.
     `spocPositions` Changes the position of spoc cards.
     `spocTopsitesPositions` Changes the position of spoc topsites.
     `spocPlacementData` Used to set the spoc content.
     `spocTopsitesPlacementEnabled` Tuns on and off the sponsored topsites placement.
     `spocTopsitesPlacementData` Used to set spoc content for topsites.
     `sponsoredCollectionsEnabled` Tuns on and off the sponsored collection section.
     `hybridLayout` Changes cards to smaller more compact cards only for specific breakpoints.
     `hideCardBackground` Removes Pocket card background and borders.
     `fourCardLayout` Enable four Pocket cards per row.
     `newFooterSection` Changes the layout of the topics section.
     `compactGrid` Reduce the number of pixels between the Pocket cards.
     `essentialReadsHeader` Updates the Pocket section header and title to say "Today’s Essential Reads", moves the "Recommended by Pocket" header to the right side.
     `editorsPicksHeader` Updates the Pocket section header and title to say "Editor’s Picks", if used with essentialReadsHeader, creates a second section 2 rows down for editorsPicks.
     `onboardingExperience` Show new users some UI explaining Pocket above the Pocket section.
     `ctaButtonSponsors` An array of sponsors we want to show a cta button on the card for.
     `ctaButtonVariant` Sets the variant for the cta sponsor button.
     `spocMessageVariant` Sets the variant for the sponsor message dialog.
*/
getHardcodedLayout = ({
  spocsUrl = SPOCS_URL,
  feedUrl = FEED_URL,
  items = 21,
  spocPositions = [1, 5, 7, 11, 18, 20],
  spocTopsitesPositions = [1],
  spocPlacementData = { ad_types: [3617], zone_ids: [217758, 217995] },
  spocTopsitesPlacementEnabled = false,
  spocTopsitesPlacementData = { ad_types: [3120], zone_ids: [280143] },
  widgetPositions = [],
  widgetData = [],
  sponsoredCollectionsEnabled = false,
  hybridLayout = false,
  hideCardBackground = false,
  fourCardLayout = false,
  newFooterSection = false,
  compactGrid = false,
  essentialReadsHeader = false,
  editorsPicksHeader = false,
  onboardingExperience = false,
  ctaButtonSponsors = [],
  ctaButtonVariant = "",
  spocMessageVariant = "",
  pocketStoriesHeadlineId = "newtab-section-header-stories",
}) => ({
  lastUpdate: Date.now(),
  spocs: {
    url: spocsUrl,
  },
  layout: [
    {
      width: 12,
      components: [
        {
          type: "TopSites",
          header: {
            title: {
              id: "newtab-section-header-topsites",
            },
          },
          ...(spocTopsitesPlacementEnabled && spocTopsitesPlacementData
            ? {
                placement: {
                  name: "sponsored-topsites",
                  ad_types: spocTopsitesPlacementData.ad_types,
                  zone_ids: spocTopsitesPlacementData.zone_ids,
                },
                spocs: {
                  probability: 1,
                  prefs: [PREF_SHOW_SPONSORED_TOPSITES],
                  positions: spocTopsitesPositions.map(position => {
                    return { index: position };
                  }),
                },
              }
            : {}),
          properties: {},
        },
        ...(sponsoredCollectionsEnabled
          ? [
              {
                type: "CollectionCardGrid",
                properties: {
                  items: 3,
                },
                header: {
                  title: "",
                },
                placement: {
                  name: "sponsored-collection",
                  ad_types: [3617],
                  zone_ids: [217759, 218031],
                },
                spocs: {
                  probability: 1,
                  positions: [
                    {
                      index: 0,
                    },
                    {
                      index: 1,
                    },
                    {
                      index: 2,
                    },
                  ],
                },
              },
            ]
          : []),
        {
          type: "Message",
          essentialReadsHeader,
          editorsPicksHeader,
          header: {
            title: {
              id: pocketStoriesHeadlineId,
            },
            subtitle: "",
            link_text: {
              id: "newtab-pocket-learn-more",
            },
            link_url: "",
            icon: "chrome://global/skin/icons/pocket.svg",
          },
          properties: {
            spocMessageVariant,
          },
          styles: {
            ".ds-message": "margin-bottom: -20px",
          },
        },
        {
          type: "CardGrid",
          properties: {
            items,
            hybridLayout,
            hideCardBackground,
            fourCardLayout,
            compactGrid,
            essentialReadsHeader,
            editorsPicksHeader,
            onboardingExperience,
            ctaButtonSponsors,
            ctaButtonVariant,
            spocMessageVariant,
          },
          widgets: {
            positions: widgetPositions.map(position => {
              return { index: position };
            }),
            data: widgetData,
          },
          cta_variant: "link",
          header: {
            title: "",
          },
          placement: {
            name: "newtab_spocs",
            ad_types: spocPlacementData.ad_types,
            zone_ids: spocPlacementData.zone_ids,
          },
          feed: {
            embed_reference: null,
            url: feedUrl,
          },
          spocs: {
            probability: 1,
            positions: spocPositions.map(position => {
              return { index: position };
            }),
          },
        },
        {
          type: "Navigation",
          newFooterSection,
          properties: {
            alignment: "left-align",
            extraLinks: [
              {
                name: "Career",
                url: "https://getpocket.com/explore/career?utm_source=pocket-newtab",
              },
              {
                name: "Technology",
                url: "https://getpocket.com/explore/technology?utm_source=pocket-newtab",
              },
            ],
            privacyNoticeURL: {
              url: "https://www.mozilla.org/privacy/firefox/#recommend-relevant-content",
              title: {
                id: "newtab-section-menu-privacy-notice",
              },
            },
          },
          styles: {
            ".ds-navigation": "margin-top: -10px;",
          },
        },
        ...(newFooterSection
          ? [
              {
                type: "PrivacyLink",
                properties: {
                  url: "https://www.mozilla.org/privacy/firefox/",
                  title: {
                    id: "newtab-section-menu-privacy-notice",
                  },
                },
              },
            ]
          : []),
      ],
    },
  ],
});
