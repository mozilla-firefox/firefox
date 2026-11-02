/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  SectionsLayoutManager: "resource://newtab/lib/SectionsLayoutFeed.sys.mjs",
  maskLayoutAds: "resource://newtab/lib/SectionsLayoutFeed.sys.mjs",
  MozAdsPlacementRequestWithCount:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsIabContent:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsIabContentTaxonomy:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
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
import {
  isSpaceOverridden,
  isSpacesAssigned,
  SPACE_IDS,
} from "resource://newtab/common/PageLayoutVariants.mjs";

import { isPlausibleSurfaceId } from "resource://newtab/common/SurfaceId.mjs";
import { scoreItemInferred } from "resource://newtab/lib/InferredModel/GreedyContentRanker.mjs";

const LOCAL_POPULAR_RERANK = false; // default behavior for local re-ranking
const LOCAL_WEIGHT = 1;
const SERVER_WEIGHT = 1;
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

// Decommissioned, and no longer in the `discoverystream.endpoints` allowlist,
// so fetchFromEndpoint rejects it. It only remains because loadLayout keys the
// spocs placement update off a truthy layout spocs URL.
const SPOCS_URL = "https://spocs.getpocket.com/spocs";
const PREF_CONFIG = "discoverystream.config";
const PREF_ENDPOINTS = "discoverystream.endpoints";
// const PREF_LAYOUT_EXPERIMENT_A = "newtabLayouts.variant-a";
// const PREF_LAYOUT_EXPERIMENT_B = "newtabLayouts.variant-b";
const PREF_CONTEXTUAL_SPOC_PLACEMENTS =
  "discoverystream.placements.contextualSpocs";
const PREF_CONTEXTUAL_SPOC_COUNTS =
  "discoverystream.placements.contextualSpocs.counts";
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
const PREF_UNIFIED_ADS_BLOCKED_LIST = "unifiedAds.blockedAds";
const PREF_UNIFIED_ADS_SPOCS_ENABLED = "unifiedAds.spocs.enabled";
const PREF_UNIFIED_ADS_ADSFEED_ENABLED = "unifiedAds.adsFeed.enabled";
const PREF_UNIFIED_ADS_ENDPOINT = "unifiedAds.endpoint";
const PREF_UNIFIED_ADS_OHTTP = "unifiedAds.ohttp.enabled";
const PREF_SHOW_SPONSORED = "showSponsored";
const PREF_SYSTEM_SHOW_SPONSORED = "system.showSponsored";
const PREF_SPOC_IMPRESSIONS = "discoverystream.spoc.impressions";
const PREF_FLIGHT_BLOCKS = "discoverystream.flight.blocks";
const PREF_SELECTED_TOPICS = "discoverystream.topicSelection.selectedTopics";
const PREF_TOPIC_SELECTION_ENABLED = "discoverystream.topicSelection.enabled";
const PREF_TOPIC_SELECTION_PREVIOUS_SELECTED =
  "discoverystream.topicSelection.hasBeenUpdatedPreviously";
const PREF_SPOCS_CACHE_ONDEMAND = "discoverystream.spocs.onDemand";
const PREF_SPOCS_CACHE_TIMEOUT = "discoverystream.spocs.cacheTimeout";
const PREF_SPOCS_STARTUP_CACHE_ENABLED =
  "discoverystream.spocs.startupCache.enabled";
const PREF_CONTEXTUAL_ADS = "discoverystream.sections.contextualAds.enabled";
const PREF_USER_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.user.enabled";
const PREF_SYSTEM_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.enabled";
const PREF_INFERRED_INTERESTS_OVERRIDE =
  "discoverystream.sections.personalization.inferred.interests.override";

const PREF_MERINO_OHTTP = "discoverystream.merino-provider.ohttp.enabled";
const PREF_BILLBOARD_ENABLED = "newtabAdSize.billboard";
const PREF_LEADERBOARD_ENABLED = "newtabAdSize.leaderboard";
const PREF_LEADERBOARD_POSITION = "newtabAdSize.leaderboard.position";
const PREF_BILLBOARD_POSITION = "newtabAdSize.billboard.position";
const PREF_CONTEXTUAL_BANNER_PLACEMENTS =
  "discoverystream.placements.contextualBanners";
const PREF_CONTEXTUAL_BANNER_COUNTS =
  "discoverystream.placements.contextualBanners.counts";

const PREF_SECTIONS_ENABLED = "discoverystream.sections.enabled";
const PREF_SECTIONS_FOLLOWING = "discoverystream.sections.following";
const PREF_SECTIONS_BLOCKED = "discoverystream.sections.blocked";
const PREF_INTEREST_PICKER_ENABLED =
  "discoverystream.sections.interestPicker.enabled";
const PREF_VISIBLE_SECTIONS =
  "discoverystream.sections.interestPicker.visibleSections";
const PREF_PRIVATE_PING_ENABLED = "telemetry.privatePing.enabled";
const PREF_SURFACE_ID = "telemetry.surfaceId";
const PREF_CLIENT_LAYOUT_ENABLED =
  "discoverystream.sections.clientLayout.enabled";
// Selection key for the RS sections-ordering: "" = off (use Merino), otherwise
// the name of the sections-ordering record to render.
const PREF_SECTIONS_ORDERING = "discoverystream.sections.ordering";
// Comma-separated section ranks that may carry an ad; "" uses the hard-coded set.
const PREF_SECTIONS_AD_ALLOWED_RANKS =
  "discoverystream.sections.adAllowedRanks";

let getHardcodedLayout;

ChromeUtils.defineLazyGetter(lazy, "userAgent", () => {
  return Cc["@mozilla.org/network/protocol;1?name=http"].getService(
    Ci.nsIHttpProtocolHandler
  ).userAgent;
});

export class DiscoveryStreamFeed {
  /** @type {Store} */
  store = null;

  constructor() {
    // Internal state for checking if we've intialized all our data
    this.loaded = false;

    // Persistent cache for remote endpoint data.
    this.cache = new lazy.PersistentCache(CACHE_KEY, true);
    this.locale = Services.locale.appLocaleAsBCP47;
    // Internal in-memory cache for parsing json prefs.
    this._prefCache = {};
    this.adsClient = null;

    this.onPocketExperimentUpdated = this.onPocketExperimentUpdated.bind(this);
  }

  onPocketExperimentUpdated(event, reason) {
    if (
      reason !== "feature-experiment-loaded" &&
      reason !== "feature-rollout-loaded"
    ) {
      this.pocketNewTabExperimentChanged();
    }
  }

  pocketNewTabExperimentChanged() {
    this.store.dispatch(
      ac.OnlyToMain({
        type: at.INFERRED_PERSONALIZATION_CLEAR_INTEREST_VECTOR,
      })
    );
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

  get isContextualAds() {
    if (this._isContextualAds === undefined) {
      // We care about if the contextual ads pref is on, if contextual is supported,
      // and if inferred is on, but OHTTP is off.
      const state = this.store.getState();
      const marsOhttpEnabled = state.Prefs.values[PREF_UNIFIED_ADS_OHTTP];
      const contextualAds = state.Prefs.values[PREF_CONTEXTUAL_ADS];
      const inferredPersonalization =
        state.Prefs.values[PREF_USER_INFERRED_PERSONALIZATION] &&
        state.Prefs.values[PREF_SYSTEM_INFERRED_PERSONALIZATION];
      const sectionsEnabled = state.Prefs.values[PREF_SECTIONS_ENABLED];
      // We want this if contextual ads are on, and also if inferred personalization is on, we also use OHTTP.
      const useContextualAds =
        contextualAds &&
        ((inferredPersonalization && marsOhttpEnabled) ||
          !inferredPersonalization);
      this._isContextualAds = sectionsEnabled && useContextualAds;
    }

    return this._isContextualAds;
  }

  get doLocalInferredRerank() {
    if (this._doLocalInferredRerank === undefined) {
      const state = this.store.getState();

      const inferredPersonalization =
        state.Prefs.values[PREF_USER_INFERRED_PERSONALIZATION] &&
        state.Prefs.values[PREF_SYSTEM_INFERRED_PERSONALIZATION];
      const sectionsEnabled = state.Prefs.values[PREF_SECTIONS_ENABLED];

      const systemPref = inferredPersonalization && sectionsEnabled;
      const expPref =
        state.Prefs.values.inferredPersonalizationConfig
          ?.local_popular_today_rerank ?? LOCAL_POPULAR_RERANK;

      const overridePref =
        this.store.getState().InferredPersonalization
          ?.inferredTelemetrySettingsOverrides?.local_popular_today_rerank ??
        true; // This can be used to turn off local inferred reranking

      // we do it if inferred is on and the experiment is on
      this._doLocalInferredRerank = systemPref && expPref && overridePref;
    }
    return this._doLocalInferredRerank;
  }

  get showSponsoredStories() {
    // Combine user-set sponsored opt-out with Mozilla-set config
    return (
      this.store.getState().Prefs.values[PREF_SHOW_SPONSORED] &&
      this.store.getState().Prefs.values[PREF_SYSTEM_SHOW_SPONSORED]
    );
  }

  get showStories() {
    // Combine user-set stories opt-out with Mozilla-set config
    const prefs = this.store.getState().Prefs.values;
    return (
      prefs[PREF_SYSTEM_TOPSTORIES] &&
      (prefs[PREF_USER_TOPSTORIES] ||
        isSpaceOverridden(SPACE_IDS.STORIES, prefs))
    );
  }

  // The selected sections-ordering key. "" means the RS ordering is off and
  // Merino supplies the layout.
  get sectionsOrderingKey() {
    const prefs = this.store.getState().Prefs.values;
    return (
      prefs.trainhopConfig?.sections?.ordering ??
      prefs[PREF_SECTIONS_ORDERING] ??
      ""
    );
  }

  // Section ranks that may carry an ad. Precedence: trainhop (an array), then
  // the pref (comma-separated), then the hard-coded default set.
  get sectionsAdAllowedRanks() {
    const prefs = this.store.getState().Prefs.values;
    let ranks = prefs.trainhopConfig?.sections?.adAllowedRanks;
    if (!Array.isArray(ranks)) {
      const pref = prefs[PREF_SECTIONS_AD_ALLOWED_RANKS];
      ranks = pref ? pref.split(",").map(Number) : undefined;
    }
    // Fall back to the hard-coded set for an empty or malformed override.
    if (!ranks?.length || ranks.some(rank => !Number.isInteger(rank))) {
      return lazy.SectionsLayoutManager.AD_ALLOWED_RANKS;
    }
    return new Set(ranks);
  }

  /**
   * Choose the layout to apply across the whole page, or null to fall back to
   * Merino. The clientLayout override is applied first if enabled; else a usable
   * Remote Settings ordering; else the hard-coded default if Merino has a gap.
   *
   * @param {object[]} sections The page's sections.
   * @returns {object[]|null} Layout records to apply across sections, else null.
   */
  _resolveLayoutOverride(sections) {
    const prefs = this.store.getState().Prefs.values;
    const { configs = {}, orderings = {} } =
      this.store.getState().SectionsLayout || {};

    const forceClientLayout =
      prefs.trainhopConfig?.clientLayout?.enabled ||
      prefs[PREF_CLIENT_LAYOUT_ENABLED];
    if (forceClientLayout) {
      return lazy.SectionsLayoutManager.DEFAULT_SECTION_LAYOUT;
    }

    const key = this.sectionsOrderingKey;
    const names = key ? orderings[key] : null;
    if (names?.length && names.every(name => configs[name])) {
      return names.map(name => configs[name]);
    }

    // If even one section from Merino is missing a layout, replace the whole
    // page with the hard-coded default rather than filling only the gaps: mixing
    // default and Merino layouts is an uncontrolled combination that could look
    // broken, so we fall back to a complete layout.
    if (sections.some(section => !section.layout)) {
      return lazy.SectionsLayoutManager.DEFAULT_SECTION_LAYOUT;
    }

    return null;
  }

  /**
   * Assign each section its layout and apply the ad mask. The first section gets
   * the override's first layout; later sections cycle through the rest. Leaves
   * Merino's layouts untouched when no override applies.
   *
   * @param {object[]} sections Sections to lay out; mutated in place.
   */
  _applySectionLayouts(sections) {
    if (!sections.length) {
      return;
    }

    const override = this._resolveLayoutOverride(sections);
    if (!override) {
      return;
    }

    const [topLayout] = override;
    // A single-entry override applies to the top section while the default cycle
    // fills the rest, so a top-section experiment doesn't restate the defaults.
    const cycleLayouts =
      override.length === 1
        ? lazy.SectionsLayoutManager.DEFAULT_SECTION_LAYOUT.slice(1)
        : override.slice(1);

    const allowedRanks = this.sectionsAdAllowedRanks;
    sections.sort((a, b) => a.receivedRank - b.receivedRank);
    // Pin the first layout to the top section and exclude it from the cycle: it's
    // typically a distinct, larger "headline" layout, so repeating it partway
    // down the page would look out of place. The rest cycle through cycleLayouts.
    sections.forEach((section, index) => {
      const layout =
        index === 0
          ? topLayout
          : cycleLayouts[(index - 1) % cycleLayouts.length];
      section.layout = lazy.maskLayoutAds(layout, section, allowedRanks);
    });
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
    const experimentMetadata =
      lazy.NimbusFeatures.pocketNewtab.getEnrollmentMetadata();

    let utmSource = "pocket-newtab";
    let utmCampaign = experimentMetadata?.slug;
    let utmContent = experimentMetadata?.branch;

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

    const nimbusConfig = this.store.getState().Prefs.values?.pocketConfig || {};
    const { region } = this.store.getState().Prefs.values;

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

    // sync redux store with PersistantCache personalization data
    this.configureFollowedSections(isStartup);
  }

  async configureFollowedSections(isStartup = false) {
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
      ac.BroadcastToContent({
        type: at.SECTION_PERSONALIZATION_UPDATE,
        data: sectionPersonalization || {},
        meta: {
          isStartup,
        },
      })
    );
  }

  uninitPrefs() {
    // Reset in-memory cache
    this._prefCache = {};
  }

  async fetchFromEndpoint(endpoint, options = {}, useOhttp = false) {
    let fetchPromise;
    if (!endpoint) {
      console.error("Tried to fetch endpoint but none was configured.");
      return null;
    }

    const ohttpRelayURL = Services.prefs.getStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
      ""
    );
    const ohttpConfigURL = Services.prefs.getStringPref(
      "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
      ""
    );

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

      if (useOhttp && ohttpConfigURL && ohttpRelayURL) {
        let config = await lazy.ObliviousHTTP.getOHTTPConfig(ohttpConfigURL);

        if (!config) {
          console.error(
            new Error(
              `OHTTP was configured for ${endpoint} but we couldn't fetch a valid config`
            )
          );
          return null;
        }

        // ObliviousHTTP.ohttpRequest only accepts a key/value object, and not
        // a Headers instance. We normalize any headers to a key/value object.
        //
        // We use instanceof here since isInstance isn't available for node
        // tests like DiscoveryStreamFeed.test.js.
        // eslint-disable-next-line mozilla/use-isInstance
        if (options.headers && options.headers instanceof Headers) {
          options.headers = Object.fromEntries(options.headers);
        }

        fetchPromise = lazy.ObliviousHTTP.ohttpRequest(
          ohttpRelayURL,
          config,
          endpoint,
          {
            ...options,
            credentials: "omit",
            signal,
          }
        );
      } else {
        fetchPromise = fetch(endpoint, {
          ...options,
          credentials: "omit",
          signal,
        });
      }

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

  async _fetchSpocsWithAdsClient(placements) {
    const options = lazy.AdsClient.requestOptions(
      this.store.getState().Prefs.values
    );

    const requests = [];
    for (let { placement: placementId, count, content } of placements) {
      let iabContent = null;
      if (content) {
        iabContent = new lazy.MozAdsIabContent({
          categoryIds: content.categories,
          taxonomy:
            lazy.MozAdsIabContentTaxonomy[
              // js-style enum strings (eg. "IAB-3.0") must be converted to
              // rust-style enum names (eg. "IAB3_0") before looking up
              // the actual enum value (eg. 4)
              content.taxonomy.replace("-", "").replace(".", "_")
            ],
        });
      }

      requests.push(
        new lazy.MozAdsPlacementRequestWithCount({
          placementId,
          count,
          iabContent,
        })
      );
    }

    const spocs = await this.adsClient.requestSpocAds(requests, options);

    // The ads-client omits placements it has no ads for; MARS returns them as
    // empty arrays, and loadSpocs concats by placement, so a missing key folds
    // an undefined into the spocs list.
    return {
      ...Object.fromEntries(placements.map(p => [p.placement, []])),
      ...Object.fromEntries(
        spocs.entries().map(([placementId, placementSpocs]) => [
          placementId,
          placementSpocs.map(spoc => ({
            format: spoc.format,
            url: spoc.url,
            callbacks: spoc.callbacks,
            image_url: spoc.imageUrl,
            title: spoc.title,
            domain: spoc.domain,
            excerpt: spoc.excerpt,
            sponsor: spoc.sponsor,
            sponsored_by_override: spoc.sponsoredByOverride,
            block_key: spoc.blockKey,
            caps: spoc.caps
              ? { cap_key: spoc.caps.capKey, day: spoc.caps.day }
              : undefined,
            ranking: spoc.ranking
              ? {
                  item_score: spoc.ranking.itemScore,
                  personalization_models: Object.fromEntries(
                    spoc.ranking.personalizationModels ?? []
                  ),
                  priority: spoc.ranking.priority,
                }
              : undefined,
          })),
        ])
      ),
    };
  }

  get spocsOnDemand() {
    if (this._spocsOnDemand === undefined) {
      const { values } = this.store.getState().Prefs;
      const spocsOnDemandConfig = values.trainhopConfig?.spocsOnDemand || {};
      const spocsOnDemand =
        spocsOnDemandConfig.enabled || values[PREF_SPOCS_CACHE_ONDEMAND];
      this._spocsOnDemand = this.showSponsoredStories && spocsOnDemand;
    }

    return this._spocsOnDemand;
  }

  get spocsCacheUpdateTime() {
    if (this._spocsCacheUpdateTime === undefined) {
      const { values } = this.store.getState().Prefs;
      const spocsOnDemandConfig = values.trainhopConfig?.spocsOnDemand || {};
      const spocsCacheTimeout =
        spocsOnDemandConfig.timeout || values[PREF_SPOCS_CACHE_TIMEOUT];
      const MAX_TIMEOUT = 30;
      const MIN_TIMEOUT = 5;

      // We have some guard rails against misconfigured values.
      // Ignore 0: a zero-minute timeout would cause constant fetches.
      // Check min max times, or ensure we don't make requests on a timer.
      const guardRailed =
        spocsCacheTimeout &&
        (this.spocsOnDemand ||
          (spocsCacheTimeout <= MAX_TIMEOUT &&
            spocsCacheTimeout >= MIN_TIMEOUT));

      if (guardRailed) {
        // This value is in minutes, but we want ms.
        this._spocsCacheUpdateTime = spocsCacheTimeout * 60 * 1000;
      } else {
        // The const is already in ms.
        this._spocsCacheUpdateTime = SPOCS_FEEDS_UPDATE_TIME;
      }
    }

    return this._spocsCacheUpdateTime;
  }

  /**
   * Returns true if data in the cache for a particular key has expired or is missing.
   *
   * @param {object} options
   * @param {object} options.cachedData data returned from cache.get()
   * @param {string} options.key a cache key
   * @param {string} [options.url] for "feed" only, the URL of the feed.
   * @param {boolean} [options.isStartup] is this check done at initial browser load
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
      case "feed": {
        if (!feeds || !feeds[url]) {
          return true;
        }
        const feed = feeds[url];
        const isTimeExpired = Date.now() - feed.lastUpdated >= EXPIRATION_TIME;
        const sectionsEnabled =
          this.store.getState().Prefs.values[PREF_SECTIONS_ENABLED];
        const sectionsEnabledChanged = feed.sectionsEnabled !== sectionsEnabled;
        return isTimeExpired || sectionsEnabledChanged;
      }
      default:
        // istanbul ignore next
        throw new Error(`${key} is not a valid key`);
    }
  }

  async _checkExpirationPerComponent() {
    const cachedData = (await this.cache.get()) || {};
    const { feeds } = cachedData;

    return {
      spocs:
        this.showSponsoredStories &&
        this.isExpired({ cachedData, key: "spocs" }),
      feeds:
        this.showStories &&
        (!feeds ||
          Object.keys(feeds).some(url =>
            this.isExpired({ cachedData, key: "feed", url })
          )),
    };
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

        if (this.showSponsoredStories) {
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
    return `https://${Services.prefs.getStringPref(
      "browser.newtabpage.activity-stream.discoverystream.merino-provider.endpoint"
    )}/api/v1/curated-recommendations`;
  }

  loadLayout(sendUpdate, isStartup) {
    let layoutData = {};
    let url = "";

    const isBasicLayout =
      this.config.hardcoded_basic_layout ||
      this.store.getState().Prefs.values[PREF_HARDCODED_BASIC_LAYOUT] ||
      this.store.getState().Prefs.values[PREF_REGION_BASIC_LAYOUT];

    const pocketConfig = this.store.getState().Prefs.values?.pocketConfig || {};
    const items = isBasicLayout ? 4 : 24;
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

    const prepConfArr = arr => {
      return arr
        ?.split(",")
        .filter(item => item)
        .map(item => parseInt(item, 10));
    };

    const spocAdTypes = prepConfArr(pocketConfig.spocAdTypes);
    const spocZoneIds = prepConfArr(pocketConfig.spocZoneIds);
    const { spocSiteId } = pocketConfig;
    let spocPlacementData;
    let spocsUrl;

    if (spocAdTypes?.length && spocZoneIds?.length) {
      spocPlacementData = {
        ad_types: spocAdTypes,
        zone_ids: spocZoneIds,
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
      spocPlacementData,
      spocPositions: this.parseGridPositions(
        this.store.getState().Prefs.values[PREF_SPOC_POSITIONS]?.split(`,`)
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
      // For now button variants are for experimentation and English only.
      ctaButtonSponsors: this.locale.startsWith("en-") ? ctaButtonSponsors : [],
      ctaButtonVariant: this.locale.startsWith("en-") ? ctaButtonVariant : "",
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
   * Adds the promise result to newFeeds and pushes a promise to newsFeedsPromises.
   *
   * @param {object} Has both newFeedsPromises (Array) and newFeeds (Object)
   * @param {boolean} isStartup We have different cache handling for startup.
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
   * Filters out components with no feeds, and combines all feeds on this component
   * with the feeds from other components.
   *
   * @param {boolean} isStartup We have different cache handling for startup.
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
   * Filters out rows with no components, and gets us a promise for each unique feed.
   *
   * @param {object} layout This is the Discovery Stream layout object.
   * @param {boolean} isStartup We have different cache handling for startup.
   * @returns {object} An object with newFeedsPromises (Array) and newFeeds (Object),
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
  /**
   * @param {object} spocs
   * @returns {{items: Array, title?: string, context?: string, sponsor?: string, sponsored_by_override?: string, flight_id?: string}}
   */
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
          attribution: spoc.attributions || null,
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

  // This returns ad placements that contain IAB content.
  // The results are ads that are contextual, and match an IAB category.
  getContextualAdsPlacements() {
    const state = this.store.getState();

    const billboardEnabled = state.Prefs.values[PREF_BILLBOARD_ENABLED];
    const billboardPosition = state.Prefs.values[PREF_BILLBOARD_POSITION];
    const leaderboardEnabled = state.Prefs.values[PREF_LEADERBOARD_ENABLED];
    const leaderboardPosition = state.Prefs.values[PREF_LEADERBOARD_POSITION];

    function getContextualStringPref(prefName) {
      return state.Prefs.values[prefName]
        ?.split(",")
        .map(s => s.trim())
        .filter(item => item);
    }

    function getContextualCountPref(prefName) {
      return state.Prefs.values[prefName]
        ?.split(`,`)
        .map(s => s.trim())
        .filter(item => item)
        .map(item => parseInt(item, 10));
    }

    const placementSpocsArray = getContextualStringPref(
      PREF_CONTEXTUAL_SPOC_PLACEMENTS
    );
    const countsSpocsArray = getContextualCountPref(
      PREF_CONTEXTUAL_SPOC_COUNTS
    );
    const bannerPlacementsArray = getContextualStringPref(
      PREF_CONTEXTUAL_BANNER_PLACEMENTS
    );
    const bannerCountsArray = getContextualCountPref(
      PREF_CONTEXTUAL_BANNER_COUNTS
    );

    const feeds = state.DiscoveryStream.feeds.data;

    const recsFeed = Object.values(feeds).find(
      feed => feed?.data?.sections?.length
    );

    let iabSections = [];
    let iabPlacements = [];
    let bannerPlacements = [];

    // If we don't have recsFeed, it means we are loading for the first time,
    // and don't have any cached data.
    // In this situation, we don't fill iabPlacements,
    // and go with the non IAB default contextual placement prefs.
    if (recsFeed) {
      iabSections = recsFeed.data.sections.sort(
        (a, b) => a.receivedRank - b.receivedRank
      );

      // Array of IAB placements, sorted by receivedRank.
      // Placements may be undefined for sections without IAB data.
      iabPlacements = iabSections.reduce((acc, section) => {
        const iabArray = section.layout.responsiveLayouts[0].tiles
          .filter(tile => tile.hasAd)
          .map(() => {
            return section.iab;
          });
        return [...acc, ...iabArray];
      }, []);
    }

    const spocPlacements = placementSpocsArray.map((placement, index) => ({
      placement,
      count: countsSpocsArray[index],
      ...(iabPlacements[index] ? { content: iabPlacements[index] } : {}),
    }));

    if (billboardEnabled) {
      bannerPlacements = bannerPlacementsArray.map((placement, index) => ({
        placement,
        count: bannerCountsArray[index],
        ...(iabSections[billboardPosition - 2]?.iab
          ? { content: iabSections[billboardPosition - 2].iab }
          : {}),
      }));
    } else if (leaderboardEnabled) {
      bannerPlacements = bannerPlacementsArray.map((placement, index) => ({
        placement,
        count: bannerCountsArray[index],
        ...(iabSections[leaderboardPosition - 2]?.iab
          ? { content: iabSections[leaderboardPosition - 2].iab }
          : {}),
      }));
    }

    return [...spocPlacements, ...bannerPlacements];
  }

  // This returns ad placements that don't contain IAB content.
  // The results are ads that are not contextual, and can be of any IAB category.
  getSimpleAdsPlacements() {
    const state = this.store.getState();
    const placementsArray = state.Prefs.values[PREF_SPOC_PLACEMENTS]?.split(`,`)
      .map(s => s.trim())
      .filter(item => item);
    const countsArray = state.Prefs.values[PREF_SPOC_COUNTS]?.split(`,`)
      .map(s => s.trim())
      .filter(item => item)
      .map(item => parseInt(item, 10));

    return placementsArray.map((placement, index) => ({
      placement,
      count: countsArray[index],
    }));
  }

  getAdsPlacements() {
    // We can replace unifiedAdsPlacements if we have and can use contextual ads.
    // No longer relying on pref based placements and counts.
    if (this.isContextualAds) {
      return this.getContextualAdsPlacements();
    }
    return this.getSimpleAdsPlacements();
  }

  async updateOrRemoveSpocs() {
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
    if (!this.showSponsoredStories) {
      // Ensure we delete any remote data potentially related to spocs.
      this.clearSpocs();
    }
    // Placements have changed so consider spocs expired, and reload them.
    await this.cache.set("spocs", {});
    await this.loadSpocs(dispatch);
  }

  // eslint-disable-next-line max-statements
  async loadSpocs(sendUpdate, isStartup) {
    const cachedData = this.adsClient ? {} : (await this.cache.get()) || {};
    const unifiedAdsEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED];

    const adsFeedEnabled =
      this.store.getState().Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED];

    let spocsState = cachedData.spocs;
    let placements = this.getPlacements();
    let unifiedAdsPlacements = [];

    if (
      this.showSponsoredStories &&
      placements?.length &&
      this.isExpired({ cachedData, key: "spocs", isStartup })
    ) {
      if (placements?.length) {
        const headers = new Headers();
        headers.append("content-type", "application/json");
        const state = this.store.getState();
        let endpoint = state.DiscoveryStream.spocs.spocs_endpoint;
        /** @type {object} */
        let body = {
          version: 2,
          ...(placements.length ? { placements } : {}),
        };

        const marsOhttpEnabled = state.Prefs.values[PREF_UNIFIED_ADS_OHTTP];

        // Bug 1964715: Remove this logic when AdsFeed is 100% enabled
        if (unifiedAdsEnabled && !adsFeedEnabled) {
          const endpointBaseUrl = state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];
          endpoint = `${endpointBaseUrl}v1/ads`;
          unifiedAdsPlacements = this.getAdsPlacements();
          const blockedSponsors =
            state.Prefs.values[PREF_UNIFIED_ADS_BLOCKED_LIST];

          // We need some basic data that we can pass along to the ohttp request.
          // We purposefully don't use ohttp on this request. We also expect to
          // mostly hit the HTTP cache rather than the network with these requests.
          if (marsOhttpEnabled) {
            const preFlight = await this.fetchFromEndpoint(
              `${endpointBaseUrl}v1/ads-preflight`,
              {
                method: "GET",
              }
            );

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

          const adsBackendConfig =
            this.store.getState().Prefs.values?.adsBackendConfig || {};

          body = {
            context_id: await lazy.ContextId.request(),
            flags: adsBackendConfig,
            placements: unifiedAdsPlacements,
            blocks: blockedSponsors.split(","),
          };
        }

        let spocsResponse;
        // Logic decision point: Query ads servers in this file or utilize AdsFeed method
        if (adsFeedEnabled) {
          const { spocs, spocPlacements } = state.Ads;

          if (spocs) {
            spocsResponse = { newtab_spocs: spocs };
            unifiedAdsPlacements = spocPlacements;
          } else {
            throw new Error("DSFeed cannot read AdsFeed spocs");
          }
        } else {
          try {
            if (this.adsClient) {
              spocsResponse =
                await this._fetchSpocsWithAdsClient(unifiedAdsPlacements);
            } else {
              spocsResponse = await this.fetchFromEndpoint(
                endpoint,
                {
                  method: "POST",
                  headers,
                  body: JSON.stringify(body),
                },
                marsOhttpEnabled
              );
            }
          } catch (error) {
            console.error("Error trying to load spocs feeds:", error);
          }
        }

        if (spocsResponse) {
          const fetchTimestamp = Date.now();
          spocsState = {
            lastUpdated: fetchTimestamp,
            spocs: {
              ...spocsResponse,
            },
          };

          const spocsResultPromises = this.getPlacements().map(
            async placement => {
              let freshSpocs = spocsState.spocs[placement.name];

              if (unifiedAdsEnabled) {
                if (!unifiedAdsPlacements) {
                  throw new Error("unifiedAdsPlacements has no value");
                }

                // No placements to reduce upon
                if (!unifiedAdsPlacements.length) {
                  return;
                }

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

              let items = blockedResults;

              // We only need to rank if we don't have contextual ads.
              if (!this.isContextualAds) {
                items = (
                  await Promise.all(
                    items.map(item => this.normalizeScore(item))
                  )
                )
                  // Sort by highest scores.
                  .sort(this.sortItem);
              }

              spocsState.spocs = {
                ...spocsState.spocs,
                [placement.name]: {
                  title,
                  context,
                  sponsor,
                  sponsored_by_override,
                  items,
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
    // The ads-client has its own HTTP response cache, so it is the only cache
    // on that path.
    if (!this.adsClient) {
      await this.cache.set("spocs", {
        lastUpdated: spocsState.lastUpdated,
        spocs: spocsState.spocs,
        spocsOnDemand: this.spocsOnDemand,
        spocsCacheUpdateTime: this.spocsCacheUpdateTime,
      });
    }

    sendUpdate({
      type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
      data: {
        lastUpdated: spocsState.lastUpdated,
        spocs: spocsState.spocs,
        spocsOnDemand: this.spocsOnDemand,
        spocsCacheUpdateTime: this.spocsCacheUpdateTime,
      },
      meta: {
        isStartup,
      },
    });
  }

  async clearSpocs() {
    const state = this.store.getState();

    // The legacy Pocket ad server this used to delete from is decommissioned,
    // so there is nothing to clear unless we are talking to MARS.
    if (!state.Prefs.values[PREF_UNIFIED_ADS_SPOCS_ENABLED]) {
      return;
    }

    const adsFeedEnabled = state.Prefs.values[PREF_UNIFIED_ADS_ADSFEED_ENABLED];
    const endpointBaseUrl = state.Prefs.values[PREF_UNIFIED_ADS_ENDPOINT];

    // Exit if there no DELETE endpoint or AdsFeed is enabled (which will handle the DELETE request)
    if (!endpointBaseUrl || adsFeedEnabled) {
      return;
    }

    // If rotation is enabled, then the module is going to take care of
    // sending the request to MARS to delete the context_id. Otherwise,
    // we do it manually here.
    if (lazy.ContextId.rotationEnabled) {
      await lazy.ContextId.forceRotation();
      return;
    }

    const headers = new Headers();
    headers.append("content-type", "application/json");
    const marsOhttpEnabled = state.Prefs.values[PREF_UNIFIED_ADS_OHTTP];

    await this.fetchFromEndpoint(
      `${endpointBaseUrl}v1/delete_user`,
      {
        method: "DELETE",
        headers,
        body: JSON.stringify({
          context_id: await lazy.ContextId.request(),
        }),
      },
      marsOhttpEnabled
    );
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

  async scoreItemsInferred(items) {
    // If this is initialized, we are ready to go.
    let personalized = false;
    let data = null;
    if (this.doLocalInferredRerank) {
      // make a flag for this
      const { inferredInterests = {} } =
        this.store.getState().InferredPersonalization ?? {};
      const weights = {
        inferred_norm: Object.entries(inferredInterests).reduce(
          (acc, [, v]) =>
            Number.isFinite(v) && !Number.isInteger(v) ? acc + v : acc,
          0
        ),
        local:
          (this.store.getState().Prefs.values?.inferredPersonalizationConfig
            ?.local_inferred_weight ?? LOCAL_WEIGHT) / 100,
        server:
          (this.store.getState().Prefs.values?.inferredPersonalizationConfig
            ?.server_inferred_weight ?? SERVER_WEIGHT) / 100,
      };
      data = (
        await Promise.all(
          items.map(item => scoreItemInferred(item, inferredInterests, weights))
        )
      )
        // Sort by highest scores.
        .sort(this.sortItem);
      personalized = true;
    } else {
      data = (await Promise.all(items.map(item => this.normalizeScore(item))))
        // Sort by highest scores.
        .sort(this.sortItem);
    }

    return { data, personalized };
  }

  async normalizeScore(item) {
    item.score = item.item_score;
    if (item.score !== 0 && !item.score) {
      item.score = 1;
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
    // We want to know if the user is in an experiment or rollout,
    // but we prioritize experiments over rollouts.
    const experimentMetadata =
      lazy.NimbusFeatures.pocketNewtab.getEnrollmentMetadata();

    let experimentName = experimentMetadata?.slug ?? "";
    let experimentBranch = experimentMetadata?.branch ?? "";

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
    // Should we fetch /curated-recommendations over OHTTP
    const merinoOhttpEnabled = prefs[PREF_MERINO_OHTTP];
    let sections = [];
    const { feeds } = cachedData;

    let feed = feeds ? feeds[feedUrl] : null;
    if (this.isExpired({ cachedData, key: "feed", url: feedUrl, isStartup })) {
      // Read before the awaits below, alongside the request body, so the
      // surfaceId is checked against what Merino was sent even if the region
      // changes while the fetch is in flight.
      const { locale, region } = this;
      const options = this.formatComponentFeedRequest(
        cachedData.sectionPersonalization
      );

      const feedResponse = await this.fetchFromEndpoint(
        feedUrl,
        options,
        merinoOhttpEnabled
      );

      if (feedResponse) {
        const { settings = {} } = feedResponse;
        let { recommendations } = feedResponse;

        recommendations = feedResponse.data.map(item => ({
          id: item.corpusItemId || item.scheduledCorpusItemId || item.tileId,
          scheduled_corpus_item_id: item.scheduledCorpusItemId,
          corpus_item_id: item.corpusItemId,
          features: item.features,
          excerpt: item.excerpt,
          icon_src: item.iconUrl,
          isTimeSensitive: item.isTimeSensitive,
          publisher: item.publisher,
          raw_image_src: item.imageUrl,
          received_rank: item.receivedRank,
          recommended_at: feedResponse.recommendedAt,
          title: item.title,
          topic: item.topic,
          url: item.url,
        }));

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
                  features: item.features,
                  excerpt: item.excerpt,
                  publisher: item.publisher,
                  raw_image_src: item.imageUrl,
                  received_rank: item.receivedRank,
                  server_score: item.serverScore,
                  recommended_at: feedResponse.recommendedAt,
                  section: sectionKey,
                  variant_id: item.variantId === undefined ? 0 : item.variantId,
                  source_section_id: item.sourceSectionId,
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
                iab: sectionData.iab,
                allowAds: sectionData.allowAds ?? true,
                followable: sectionData.followable ?? true,
                // property if initially shown (with interest picker)
                visible: sectionData.isInitiallyVisible,
              });
            }
          }

          this._applySectionLayouts(sections);
        }

        const { data: scoredItems, personalized } =
          await this.scoreItemsInferred(recommendations);

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
              const found = sections.find(
                ({ sectionKey }) => sectionKey === sectionId
              );
              return {
                sectionId,
                title: found?.title,
                followable: found?.followable,
              };
            });
        }
        if (feedResponse.inferredLocalModel) {
          this.store.dispatch(
            ac.AlsoToMain({
              type: at.INFERRED_PERSONALIZATION_MODEL_UPDATE,
              data: feedResponse.inferredLocalModel || {},
            })
          );
        }
        // We can cleanup any impressions we have that are old before we rotate.
        // In theory we can do this anywhere, but doing it just before rotate is optimal.
        // Rotate is also the only place that uses these impressions.
        await this.cleanUpTopRecImpressions();
        const rotatedItems = await this.rotate(scoredItems);
        const { data: filteredResults } =
          await this.filterBlocked(rotatedItems);
        let { surfaceId = "" } = feedResponse;
        if (surfaceId && !isPlausibleSurfaceId(surfaceId, locale, region)) {
          console.warn(`Ignoring implausible surfaceId: ${surfaceId}`);
          // @ts-expect-error tools/@types/generated/lib.gecko.glean.d.ts predates this metric.
          Glean.newtabContent.surfaceIdRejected.record();
          surfaceId = "";
        }
        feed = {
          lastUpdated: Date.now(),
          personalized,
          sectionsEnabled,
          data: {
            settings,
            sections,
            interestPicker: feedResponse.interestPicker || {},
            recommendations: filteredResults,
            surfaceId,
            status: "success",
          },
        };
      } else {
        console.error("No response for feed");
      }
    }

    // if surfaceID is availible either through the cache or the response set value in Glean
    if (feed?.data?.surfaceId) {
      Glean.newtabContent.surfaceId.set(feed.data.surfaceId);
    }

    // Still gated: this pref feeds utc_offset on the Merino request
    if (prefs[PREF_PRIVATE_PING_ENABLED] && feed?.data) {
      this.store.dispatch(
        feed.data.surfaceId
          ? ac.SetPref(PREF_SURFACE_ID, feed.data.surfaceId)
          : { type: at.CLEAR_PREF, data: { name: PREF_SURFACE_ID } }
      );
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

  formatComponentFeedRequest(sectionPersonalization = {}) {
    const prefs = this.store.getState().Prefs.values;
    const inferredPersonalization =
      prefs[PREF_USER_INFERRED_PERSONALIZATION] &&
      prefs[PREF_SYSTEM_INFERRED_PERSONALIZATION];
    const merinoOhttpEnabled = prefs[PREF_MERINO_OHTTP];
    const headers = new Headers();
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

    let inferredInterests = null;
    if (inferredPersonalization && merinoOhttpEnabled) {
      const personalization = this.store.getState().InferredPersonalization;
      const useLocationVector =
        prefs?.trainhopConfig?.locationInitInferred?.enabled &&
        personalization?.shouldUseLocationVector &&
        personalization.locationPrivateInferredInterests;
      inferredInterests = useLocationVector
        ? personalization.locationPrivateInferredInterests
        : personalization?.coarsePrivateInferredInterests || {};
      if (prefs[PREF_INFERRED_INTERESTS_OVERRIDE]) {
        try {
          inferredInterests = JSON.parse(
            prefs[PREF_INFERRED_INTERESTS_OVERRIDE]
          );
        } catch (ex) {
          console.error("Invalid format json for inferred interest override.");
        }
      }
    }

    const requestMetadata = {
      // Without a surface, getUtcOffset would return the unclamped offset.
      utc_offset:
        prefs.inferredPersonalizationConfig?.normalized_time_zone_offset &&
        prefs[PREF_SURFACE_ID]
          ? lazy.NewTabUtils.getUtcOffset(prefs[PREF_SURFACE_ID])
          : undefined,
      inferredInterests,
    };
    headers.append("content-type", "application/json");
    /** @type {{[key: string]: any}} */
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

    if (sectionsEnabled) {
      body.feeds = ["sections"];
    }

    return {
      method: "POST",
      headers,
      body: JSON.stringify(body),
    };
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
        const feedPromise = this.scoreItemsInferred(feed.data.recommendations);
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

  /**
   * @typedef {object} RefreshAll
   * @property {boolean} [updateOpenTabs] - Sends updates to open tabs immediately if true,
   *                                      updates in background if false
   * @property {boolean} [isStartup] - When the function is called at browser startup
   * @property {boolean} [isSystemTick] - When the function is called by the system tick,
   *                                      so that it can check whether spocs are on demand
   */

  /**
   * Refreshes component feeds, and spocs in order if caches have expired.
   *
   * @param {RefreshAll} [options]
   */
  async refreshAll(options = {}) {
    const { updateOpenTabs, isStartup, isSystemTick } = options;

    const dispatch = updateOpenTabs
      ? action => this.store.dispatch(ac.BroadcastToContent(action))
      : this.store.dispatch;

    this.loadLayout(dispatch, isStartup);
    if (this.showStories) {
      const spocsStartupCacheEnabled =
        this.store.getState().Prefs.values[PREF_SPOCS_STARTUP_CACHE_ENABLED];
      const promises = [];

      // We don't want to make spoc requests during system tick if on demand is on.
      if (!(this.spocsOnDemand && isSystemTick)) {
        const spocsPromise = this.loadSpocs(
          dispatch,
          isStartup && spocsStartupCacheEnabled
        ).catch(error =>
          console.error("Error trying to load spocs feeds:", error)
        );
        promises.push(spocsPromise);
      }
      const storiesPromise = this.loadComponentFeeds(dispatch, isStartup).catch(
        error => console.error("Error trying to load component feeds:", error)
      );
      promises.push(storiesPromise);
      await Promise.all(promises);
      // We don't need to check onDemand here,
      // even though _maybeUpdateCachedData fetches spocs.
      // This is because isStartup and isSystemTick can never both be true.
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

  async resetSpocs() {
    await this.cache.set("spocs", {});
  }

  async resetAllCache() {
    await this.resetContentCache();
    // Reset in-memory caches.
    this._isContextualAds = undefined;
    this._doLocalInferredRerank = undefined;
    this._spocsCacheUpdateTime = undefined;
    this._spocsOnDemand = undefined;
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

  async topicSelectionMaybeLaterEvent() {
    const age = await this.retreiveProfileAge();
    const newProfile = age <= 1;
    const day = 24 * 60 * 60 * 1000;
    this.store.dispatch(
      ac.SetPref(
        TOPIC_SELECTION_DISPLAY_TIMEOUT,
        newProfile ? 3 * day : 7 * day
      )
    );
  }

  async onSpocsOnDemandUpdate() {
    if (this.spocsOnDemand) {
      const expirationPerComponent = await this._checkExpirationPerComponent();
      if (expirationPerComponent.spocs) {
        await this.loadSpocs(action =>
          this.store.dispatch(ac.BroadcastToContent(action))
        );
      }
    }
  }

  async onSystemTick() {
    // Only refresh when enabled and after initial load has completed.
    if (!this.config.enabled || !this.loaded) {
      return;
    }

    const expirationPerComponent = await this._checkExpirationPerComponent();
    let expired = false;

    if (this.spocsOnDemand) {
      // With on-demand only feeds can trigger a refresh.
      expired = expirationPerComponent.feeds;
    } else {
      // Without on-demand both feeds or spocs can trigger a refresh.
      expired = expirationPerComponent.feeds || expirationPerComponent.spocs;
    }

    if (expired) {
      // We use isSystemTick so refreshAll can know to check onDemand
      await this.refreshAll({ updateOpenTabs: false, isSystemTick: true });
    }
  }

  async onTrainhopConfigChanged() {
    this.resetSpocsOnDemand();
    // The spaces experiment turns stories on without the user pref changing, so
    // the config arriving is what starts the feed. Scoped to the experiment, so
    // an unrelated train-hop config does not trigger a refresh.
    const prefs = this.store.getState().Prefs.values;
    if (isSpacesAssigned(prefs) && this.showStories) {
      this.enableStories();
    }
  }

  async onPrefChangedAction(action) {
    switch (action.data.name) {
      case PREF_CONFIG:
      case PREF_ENABLED:
      case PREF_HARDCODED_BASIC_LAYOUT:
      case PREF_SPOCS_ENDPOINT:
      case PREF_SPOCS_ENDPOINT_QUERY:
      case PREF_ENDPOINTS:
      case PREF_SPOC_POSITIONS:
      case PREF_UNIFIED_ADS_SPOCS_ENABLED:
      case PREF_SECTIONS_ENABLED:
      case PREF_INTEREST_PICKER_ENABLED:
        // This is a config reset directly related to Discovery Stream pref.
        this.configReset();
        break;
      case PREF_USER_INFERRED_PERSONALIZATION:
        this.configReset();
        this._isContextualAds = undefined;
        this._doLocalInferredRerank = undefined;
        await this.resetContentCache();
        break;
      case PREF_CONTEXTUAL_ADS:
      case PREF_SYSTEM_INFERRED_PERSONALIZATION:
        this._isContextualAds = undefined;
        this._doLocalInferredRerank = undefined;
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
      case PREF_USER_TOPSTORIES:
      case PREF_SYSTEM_TOPSTORIES:
        if (!this.showStories) {
          // Ensure we delete any remote data potentially related to spocs.
          this.clearSpocs();
        }
        if (action.data.value) {
          this.enableStories();
        }
        break;
      // Remove spocs if turned off.
      case PREF_SHOW_SPONSORED: {
        await this.updateOrRemoveSpocs();
        break;
      }
      case PREF_SPOCS_CACHE_ONDEMAND:
      case PREF_SPOCS_CACHE_TIMEOUT: {
        this.resetSpocsOnDemand();
        break;
      }
    }

    if (action.data.name === "pocketConfig") {
      await this.onPrefChange();
      this.setupPrefs(false /* isStartup */);
    }
    if (action.data.name === "trainhopConfig") {
      await this.onTrainhopConfigChanged();
    }
  }

  resetSpocsOnDemand() {
    // This is all we have to do, because we're just changing how often caches update.
    // No need to reset what is already fetched, we just care about the next check.
    this._spocsCacheUpdateTime = undefined;
    this._spocsOnDemand = undefined;
    this.store.dispatch({
      type: at.DISCOVERY_STREAM_SPOCS_ONDEMAND_RESET,
      data: {
        spocsOnDemand: this.spocsOnDemand,
        spocsCacheUpdateTime: this.spocsCacheUpdateTime,
      },
    });
  }

  async onAction(action) {
    switch (action.type) {
      case at.INIT:
        // During the initialization of Firefox:
        // 1. Set-up listeners and initialize the redux state for config;
        this.setupConfig(true /* isStartup */);
        this.setupPrefs(true /* isStartup */);
        lazy.NimbusFeatures.pocketNewtab.onUpdate(
          this.onPocketExperimentUpdated
        );
        if (lazy.AdsClient.isEnabled(this.store.getState().Prefs.values)) {
          this.adsClient = lazy.AdsClient.getClient();
        }
        // 2. If config.enabled is true, start loading data.
        if (this.config.enabled) {
          await this.enable({ updateOpenTabs: true, isStartup: true });
        }
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
        await this.onSystemTick();
        break;
      case at.DISCOVERY_STREAM_SPOCS_ONDEMAND_UPDATE: {
        await this.onSpocsOnDemandUpdate();
        break;
      }
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
      case at.DISCOVERY_STREAM_DEV_REFRESH_CACHE:
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
        if (this.showSponsoredStories) {
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
              spocsOnDemand: this.spocsOnDemand,
              spocsCacheUpdateTime: this.spocsCacheUpdateTime,
            });

            this.store.dispatch(
              ac.AlsoToPreloaded({
                type: at.DISCOVERY_STREAM_SPOCS_UPDATE,
                data: {
                  lastUpdated: spocsState.lastUpdated,
                  spocs: spocsState.data,
                  spocsOnDemand: this.spocsOnDemand,
                  spocsCacheUpdateTime: this.spocsCacheUpdateTime,
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

        if (this.showSponsoredStories) {
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
              spocsOnDemand: this.spocsOnDemand,
              spocsCacheUpdateTime: this.spocsCacheUpdateTime,
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
        lazy.NimbusFeatures.pocketNewtab.offUpdate(
          this.onPocketExperimentUpdated
        );
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
        break;
      case at.TOPIC_SELECTION_IMPRESSION:
        this.topicSelectionImpressionEvent();
        break;
      case at.SECTION_PERSONALIZATION_SET:
        await this.cache.set("sectionPersonalization", action.data);
        this.store.dispatch(
          ac.BroadcastToContent({
            type: at.SECTION_PERSONALIZATION_UPDATE,
            data: action.data,
          })
        );
        break;
      case at.INFERRED_PERSONALIZATION_MODEL_UPDATE:
        await this.cache.set("inferredModel", action.data);
        break;
      case at.ADS_UPDATE_SPOCS:
        await this.updateOrRemoveSpocs();
        break;
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
     `spocPlacementData` Used to set the spoc content.
     `hybridLayout` Changes cards to smaller more compact cards only for specific breakpoints.
     `hideCardBackground` Removes Pocket card background and borders.
     `fourCardLayout` Enable four Pocket cards per row.
     `newFooterSection` Changes the layout of the topics section.
     `compactGrid` Reduce the number of pixels between the Pocket cards.
     `ctaButtonSponsors` An array of sponsors we want to show a cta button on the card for.
     `ctaButtonVariant` Sets the variant for the cta sponsor button.
*/
getHardcodedLayout = ({
  spocsUrl = SPOCS_URL,
  feedUrl,
  items = 21,
  spocPositions = [1, 5, 7, 11, 18, 20],
  spocPlacementData = { ad_types: [3617], zone_ids: [217758, 217995] },
  widgetPositions = [],
  widgetData = [],
  hybridLayout = false,
  hideCardBackground = false,
  fourCardLayout = false,
  newFooterSection = false,
  compactGrid = false,
  ctaButtonSponsors = [],
  ctaButtonVariant = "",
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
          properties: {},
        },
        {
          type: "Message",
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
          styles: {
            ".ds-message": "margin-block-end: -20px",
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
            ctaButtonSponsors,
            ctaButtonVariant,
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
              url: "https://www.mozilla.org/privacy/firefox/#to-serve-relevant-content-and-advertising-on-firefox-new-tab",
              title: {
                id: "newtab-section-menu-privacy-notice",
              },
            },
          },
          styles: {
            ".ds-navigation": "margin-block-start: -10px;",
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
