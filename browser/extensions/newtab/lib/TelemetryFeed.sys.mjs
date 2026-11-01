// We're using console.error() to debug, so we'll be keeping this rule handy
/* eslint no-console: ["error", { allow: ["error"] }] */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

// We use importESModule here instead of static import so that the Karma test
// environment won't choke on these module. This is because the Karma test
// environment already stubs out XPCOMUtils and RemoteSettings, and overrides
// importESModule to be a no-op (which can't be done for a static import
// statement).
// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

import {
  actionTypes as at,
  actionUtils as au,
} from "resource://newtab/common/Actions.mjs";
import {
  PREF_HOME_WIDGETS,
  WIDGET_REGISTRY,
  getWidgets,
  isWidgetEnabled,
} from "resource://newtab/common/WidgetsRegistry.mjs";
import {
  isWidgetPlacementActive,
  resolvePageLayoutVariant,
  resolvePinnedWidgetIds,
} from "resource://newtab/common/PageLayoutVariants.mjs";
import { Prefs } from "resource://newtab/lib/ActivityStreamPrefs.sys.mjs";
import { classifySite } from "resource://newtab/lib/SiteClassifier.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AboutNewTabParent: "resource:///actors/AboutNewTabParent.sys.mjs",
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ClientEnvironmentBase:
    "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  NewTabContentPing: "resource://newtab/lib/NewTabContentPing.sys.mjs",
  hasScoringFlag:
    "resource://newtab/lib/SponsoredTopSitesScoreProvider/SponsoredTopSitesScoreProvider.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  MozAdsReportReason:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
});

export const PREF_IMPRESSION_ID = "impressionId";
export const TELEMETRY_PREF = "telemetry";
export const PREF_UNIFIED_ADS_SPOCS_ENABLED = "unifiedAds.spocs.enabled";
export const PREF_UNIFIED_ADS_TILES_ENABLED = "unifiedAds.tiles.enabled";
const PREF_ENDPOINTS = "discoverystream.endpoints";
const PREF_SHOW_SPONSORED_STORIES = "showSponsored";
const PREF_SHOW_SPONSORED_TOPSITES = "showSponsoredTopSites";
const BLANK_HOMEPAGE_URL = "chrome://browser/content/blanktab.html";
const PREF_PRIVATE_PING_ENABLED = "telemetry.privatePing.enabled";
const PREF_MERINO_FEED_EXPERIMENT =
  "browser.newtabpage.activity-stream.discoverystream.merino-feed-experiment";
const PREF_PRIVATE_PING_INFERRED_ENABLED =
  "telemetry.privatePing.inferredInterests.enabled";
const PREF_PRIVATE_PING_DWELL_TIME_ENABLED =
  "telemetry.privatePing.dwellTime.enabled";
const PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE =
  "telemetry.privatePing.dwellTime.noiseScale";
const PREF_NEWTAB_PING_ENABLED = "browser.newtabpage.ping.enabled";
const PREF_USER_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.user.enabled";
const PREF_SYSTEM_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.enabled";
const PREF_SECTIONS_PERSONALIZATION_ENABLED =
  "discoverystream.sections.personalization.enabled";
const PREF_SOV_FRECENCY_EXPOSURE = "sov.frecency.exposure";

const TOP_STORIES_SECTION_NAME = "top_stories_section";

// Activity notifications that ignore synthesized pointer moves, unlike the
// plain user-interaction-active topic. EventStateManager fires "active" when
// interaction starts and at each user_interaction_interval while it continues,
// then "inactive" at the first interval with no input. The notification at the
// start of a run only fires after a full idle period, so interaction that
// resumes within a run can go unnoticed for up to one interval.
const USER_INTERACTION_ACTIVE = "user-interaction-active-non-synthesized";
const USER_INTERACTION_INACTIVE = "user-interaction-inactive-non-synthesized";

// Labels newtab.opened_page_dwell_time accepts, mirroring metrics.yaml. The
// label is chosen at the click site in content, so it is validated here rather
// than trusted.
const DWELL_LABELS = new Set([
  "topsite_organic",
  "topsite_sponsored",
  "story_organic",
  "story_sponsored",
]);

// Upper bound on the pages we track at once, so a session that opens many
// links without going idle can't grow the map forever.
const MAX_TRACKED_OPENED_PAGES = 100;

// Fired by URILoadingHelper when Firefox opens a link from page content in a
// new tab or window, e.g. from its context menu or on middle-click. Newtab
// cannot otherwise see these opens.
const CREATED_NAVIGATION_TARGET_TOPIC = "webNavigation-createdNavigationTarget";
const BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK_SOURCE =
  "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK";

// Upper bound on the dwell time reported to the newtab-content ping, matching
// the longest submission delay the dwell time rollout uses. That delay already
// bounds a story opened in the same tab. A story opened in a new tab leaves
// the newtab page loaded, so nothing schedules the ping and only this bounds
// it.
const MAX_DWELL_TIME_MS = 60000;

// Scale of the Laplace noise each reported dwell time is multiplied by, for
// when the pref is missing or unusable.
const DEFAULT_DWELL_TIME_NOISE_SCALE = 0.05;

/**
 * Draw a sample from a Laplace distribution centered at 0.
 *
 * @param  {number} scale the scale (b) of the distribution
 * @returns {number} the sample
 */
export function laplaceNoise(scale) {
  const randomValues = new Uint32Array(1);
  crypto.getRandomValues(randomValues);
  // Strictly between 0 and 1, so neither logarithm below can be of 0.
  const u = (randomValues[0] + 0.5) / 2 ** 32;
  return u < 0.5 ? scale * Math.log(2 * u) : -scale * Math.log(2 * (1 - u));
}

/**
 * The key that pairs a dwell measurement with the click that started it.
 * SafeAnchor's id only counts up within one newtab document, so the sending
 * port has to be part of the key. Never recorded.
 *
 * @param  {string} portID the newtab that sent both actions
 * @param  {number|string} [dwellId] the id SafeAnchor put on both of them
 * @returns {string|null} the token, or null if the link did not send one
 */
function dwellTokenFor(portID, dwellId) {
  return portID && dwellId ? `${portID}:${dwellId}` : null;
}

/**
 * Glean session types for OHTTP ping optimization.
 * Determines whether events are queued or sent immediately to OHTTP ping.
 */
export const GleanSessionType = {
  NormalGleanSession: "normal",
  PrivateGleanSession: "private",
};

/**
    Additional parameters defined in the newTabTrainHop experimenter method

    trainhopConfig.newtabPrivatePing.randomContentProbabilityEpsilonMicro
    Epsilon for randomizing content impression and click telemetry using the RandomizedReponse method
    in the newtab_content ping , as integer multipled by 1e6

    trainhopConfig.newtabPrivatePing.dailyEventCap
    Maximum newtab_content events that can be sent in 24 hour period.
*/
const TRAINHOP_PREF_RANDOM_CLICK_PROBABILITY_MICRO =
  "randomContentClickProbabilityEpsilonMicro";

/**
 *    Maximum newtab_content events that can be sent in 24 hour period.
 */
const TRAINHOP_PREF_DAILY_EVENT_CAP = "dailyEventCap";

const TRAINHOP_PREF_DAILY_CLICK_EVENT_CAP = "dailyClickEventCap";
const TRAINHOP_PREF_WEEKLY_CLICK_EVENT_CAP = "weeklyClickEventCap";

// This is a mapping table between the user preferences and its encoding code
export const USER_PREFS_ENCODING = {
  showSearch: 1 << 0,
  "feeds.topsites": 1 << 1,
  "feeds.section.topstories": 1 << 2,
  "feeds.section.highlights": 1 << 3,
  [PREF_SHOW_SPONSORED_STORIES]: 1 << 5,
  "asrouter.userprefs.cfr.addons": 1 << 6,
  "asrouter.userprefs.cfr.features": 1 << 7,
  [PREF_SHOW_SPONSORED_TOPSITES]: 1 << 8,
};

const PRIVATE_PING_SURFACE_COUNTRY_MAP = {
  // This will be expanded to other surfaces as we expand the reach of the private content ping
  NEW_TAB_EN_US: ["US", "CA"],
  NEW_TAB_DE_DE: ["DE", "CH", "AT"],
  NEW_TAB_EN_GB: ["GB", "IE"],
  NEW_TAB_FR_FR: ["FR", "BE"],
};

// Used as the missing value for timestamps in the session ping
const TIMESTAMP_MISSING_VALUE = -1;

// The scroll distance in pixels each newtab ping scroll metric reports on,
// in ascending order. Must stay in sync with SCROLL_TELEMETRY_THRESHOLDS in
// content-src/components/Base/Base.jsx.
const SCROLL_THRESHOLD_METRICS = [
  [50, "scroll"],
  [100, "scroll100"],
  [250, "scroll250"],
];

// Page filter for onboarding telemetry, any value other than these will
// be set as "other"
const ONBOARDING_ALLOWED_PAGE_VALUES = [
  "about:welcome",
  "about:home",
  "about:newtab",
];

const PREF_SURFACE_ID = "telemetry.surfaceId";

const CONTENT_PING_VERSION = 2;

const ACTIVITY_STREAM_PREF_BRANCH = "browser.newtabpage.activity-stream.";

const NEWTAB_PING_PREFS = {
  showSearch: Glean.newtabSearch.enabled,
  "feeds.topsites": Glean.topsites.enabled,
  [PREF_SHOW_SPONSORED_TOPSITES]: Glean.topsites.sponsoredEnabled,
  "feeds.section.highlights": Glean.newtab.highlightsEnabled,
  "feeds.section.topstories": Glean.pocket.enabled,
  [PREF_SHOW_SPONSORED_STORIES]: Glean.pocket.sponsoredStoriesEnabled,
  topSitesRows: Glean.topsites.rows,
  showWeather: Glean.newtab.weatherEnabled,
};

const TOP_SITES_BLOCKED_SPONSORS_PREF = "browser.topsites.blockedSponsors";
const TOPIC_SELECTION_SELECTED_TOPICS_PREF =
  "browser.newtabpage.activity-stream.discoverystream.topicSelection.selectedTopics";
const WALLPAPER_USER_EVENTS = new Set([
  at.WALLPAPER_CATEGORY_CLICK,
  at.WALLPAPER_CLICK,
  at.WALLPAPERS_FEATURE_HIGHLIGHT_DISMISSED,
  at.WALLPAPERS_FEATURE_HIGHLIGHT_CTA_CLICKED,
  at.WALLPAPER_SAVED_ADDED,
  at.WALLPAPER_SAVED_APPLIED,
  at.WALLPAPER_SAVED_REMOVED,
]);

export class TelemetryFeed {
  /** @type {Store} */
  store = null;

  /**
   * Lazy getters for the sponsored stories and top sites prefs replace these in
   * the constructor.
   *
   * @type {boolean}
   */
  SHOW_SPONSORED_STORIES_ENABLED;

  /** @type {boolean} */
  SHOW_SPONSORED_TOPSITES_ENABLED;

  /**
   * Queue for telemetry events when in NormalGleanSession mode.
   * Events are stored here and cleared at session end based on session type.
   */
  #eventBuffer = [];
  // portID -> { url, userEvent } for the story Firefox may open next.
  #pendingLinkOpens = new Map();

  // Dwell ids for links Firefox opens itself. Prefixed, so they can never
  // match an id the newtab document counted up for a link it opened.
  #nextBrowserOpenDwellId = 0;

  /** Whether the user is currently interacting. Drives the dwell stopwatches. */
  #userActive = false;

  /**
   * When the last "active" notification arrived, i.e. the most recent moment we
   * know the user was interacting. Null before the first one.
   */
  #lastActiveAt = null;

  /**
   * Pages opened from a newtab, by the permanentKey of the browser showing
   * them. Each entry runs the same dwell stopwatch as a newtab session and is
   * finalized into newtab.opened_page_dwell_time when it stops being
   * attributed.
   */
  #openedPages = new Map();

  /**
   * Dwell tokens of organic story clicks that arrived before the link open
   * they belong to, oldest first. A link opened in a new window is only
   * reported once the window has started, well after the click.
   */
  #clicksAwaitingDwell = new Set();

  constructor() {
    this.sessions = new Map();
    this._prefs = new Prefs();
    this._impressionId = this.getOrCreateImpressionId();
    this._aboutHomeSeen = false;
    this._adsClient = null;
    this._classifySite = classifySite;
    this._browserOpenNewtabStart = null;
    this._privateRandomContentTelemetryProbablityValues = {};

    this.newtabContentPing = new lazy.NewTabContentPing();
    this._initialized = false;
    this._gleanSessionInitialized = false;

    this.gleanSessionType = GleanSessionType.PrivateGleanSession;

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "SHOW_SPONSORED_STORIES_ENABLED",
      `${ACTIVITY_STREAM_PREF_BRANCH}${PREF_SHOW_SPONSORED_STORIES}`,
      false
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "SHOW_SPONSORED_TOPSITES_ENABLED",
      `${ACTIVITY_STREAM_PREF_BRANCH}${PREF_SHOW_SPONSORED_TOPSITES}`,
      false
    );
  }

  get telemetryEnabled() {
    return this._prefs.get(TELEMETRY_PREF);
  }

  get privatePingEnabled() {
    return this._prefs.get(PREF_PRIVATE_PING_ENABLED);
  }

  get privatePingDwellTimeEnabled() {
    return this._prefs.get(PREF_PRIVATE_PING_DWELL_TIME_ENABLED);
  }

  /**
   * The scale of the Laplace noise dwell times are multiplied by. Falls back
   * to the default for anything that is not a positive number, so no
   * configuration can turn the noise off.
   *
   * @returns {number}
   */
  get privatePingDwellTimeNoiseScale() {
    const value = this._prefs.get(
      PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE,
      String(DEFAULT_DWELL_TIME_NOISE_SCALE)
    );
    const scale = parseFloat(value);
    if (!(scale > 0) || !Number.isFinite(scale)) {
      console.error(
        `Invalid newtab-content dwell time noise scale "${value}", using` +
          ` ${DEFAULT_DWELL_TIME_NOISE_SCALE}`
      );
      return DEFAULT_DWELL_TIME_NOISE_SCALE;
    }
    return scale;
  }

  /**
   * One draw of the noise a reported dwell time is multiplied by, as
   * 1 + dwellTimeNoise().
   *
   * @returns {number}
   */
  dwellTimeNoise() {
    return laplaceNoise(this.privatePingDwellTimeNoiseScale);
  }

  /**
   * The dwell time to report for a story. A story that was never in front of
   * the user, opened in a background tab and closed or still waiting when the
   * ping goes out, is not a bounce, so it reports nothing. A story still open
   * reports the time accrued so far.
   *
   * @param  {boolean} seen whether the story's tab was ever the selected tab of
   *  the frontmost window
   * @param  {number} dwellMs active milliseconds accrued
   * @returns {number|null} the noised dwell time in milliseconds, between 0 and
   *  MAX_DWELL_TIME_MS, or null to send the click without a dwell time
   */
  #dwellTimeMsIfSeen(seen, dwellMs) {
    if (!seen) {
      return null;
    }
    const noisedMs = Math.round(dwellMs * (1 + this.dwellTimeNoise()));
    return Math.min(MAX_DWELL_TIME_MS, Math.max(0, noisedMs));
  }

  get privatePingInferredInterestsEnabled() {
    return (
      this._prefs.get(PREF_PRIVATE_PING_INFERRED_ENABLED) &&
      this._prefs.get(PREF_USER_INFERRED_PERSONALIZATION) &&
      this._prefs.get(PREF_SYSTEM_INFERRED_PERSONALIZATION)
    );
  }

  /**
   * Gets the clickOnly feature flag from Nimbus trainhop configuration.
   * When enabled, events are queued and OHTTP is only used when clicks occur.
   *
   * @returns {boolean} true if clickOnly is enabled
   */
  get trainhopClickOnlyEnabled() {
    return (
      this.store?.getState()?.Prefs.values?.trainhopConfig?.newtabPrivatePing
        ?.clickOnly || false
    );
  }

  /**
   * Gets the optimizeInferred feature flag from Nimbus trainhop configuration.
   * When enabled with inferred personalization, waits for CIV to have clicks
   * before enabling private ping with inferred interests.
   *
   * @returns {boolean} true if optimizeInferred is enabled
   */
  get trainhopOptimizeInferredEnabled() {
    return (
      this.store?.getState()?.Prefs.values?.trainhopConfig?.newtabPrivatePing
        ?.optimizeInferred || false
    );
  }

  get sectionsPersonalizationEnabled() {
    return this._prefs.get(PREF_SECTIONS_PERSONALIZATION_ENABLED);
  }

  get inferredInterests() {
    return this.store?.getState()?.InferredPersonalization
      ?.coarsePrivateInferredInterests;
  }

  get inferredTelemetrySettingsOverrides() {
    return this.store?.getState()?.InferredPersonalization
      ?.inferredTelemetrySettingsOverrides;
  }

  /**
   * Checks if the Coarse Interest Vector (CIV) has recorded any clicks.
   * Used for the optimizeInferred feature to determine if private ping
   * is necessary based on historical click activity.
   *
   * @returns {boolean} true if CIV has recorded clicks, otherwise false
   */
  hasRecordedClicksInCIV() {
    const inferredPersonalization =
      this.store?.getState()?.InferredPersonalization;

    if (!inferredPersonalization?.initialized) {
      return false;
    }

    const clickCount = inferredPersonalization.inferredInterests?.clicks;
    if (clickCount) {
      return true;
    }

    const coarsePrivate =
      inferredPersonalization.coarsePrivateInferredInterests;
    if (
      !coarsePrivate ||
      !coarsePrivate.values ||
      coarsePrivate.values.length === 0
    ) {
      return false;
    }

    return coarsePrivate.values.some(bitstring => bitstring !== "000");
  }

  /**
   * Initializes the Glean session type based on configuration and CIV state.
   * Determines whether to use NormalGleanSession (queue events) or
   * PrivateGleanSession (send to both pings immediately).
   */
  initializeGleanSession() {
    if (this._gleanSessionInitialized) {
      return;
    }

    this._gleanSessionInitialized = true;

    if (
      !this.privatePingEnabled ||
      !this.trainhopClickOnlyEnabled ||
      this.isPlacesInfluenced()
    ) {
      this.gleanSessionType = GleanSessionType.PrivateGleanSession;
      return;
    }

    if (
      this.trainhopOptimizeInferredEnabled &&
      this.privatePingInferredInterestsEnabled &&
      !this.hasRecordedClicksInCIV()
    ) {
      this.gleanSessionType = GleanSessionType.NormalGleanSession;
      return;
    }

    this.gleanSessionType = GleanSessionType.NormalGleanSession;
  }

  /**
   * Initializes MAC
   */
  initializeMac() {
    if (lazy.AdsClient.isEnabled(this.store?.getState()?.Prefs.values)) {
      this._adsClient = lazy.AdsClient.getClient();
    }
  }

  /**
   * Clears the event queue, executing callbacks and optionally
   * recording to newtab-content ping.
   *
   * @param {boolean} recordToContentPing - Whether to record events to newtab-content ping
   * @param {string} [sessionId] - Only drain events queued by this session. Omit
   *  to drain every session's events, which is only correct when no session is
   *  going to get the chance to drain its own (private-session transition and
   *  shutdown).
   */
  #clearEventBuffer(recordToContentPing, sessionId) {
    if (!this.#eventBuffer.length) {
      return;
    }

    let events;
    if (sessionId === undefined) {
      events = this.#eventBuffer;
      this.#eventBuffer = [];
    } else {
      events = this.#eventBuffer.filter(event => event.sessionId === sessionId);
      this.#eventBuffer = this.#eventBuffer.filter(
        event => event.sessionId !== sessionId
      );
    }

    for (const { eventName, eventData, callback } of events) {
      callback?.();
      if (recordToContentPing && this.privatePingEnabled) {
        this.newtabContentPing.recordEvent(eventName, eventData);
      }
    }
  }

  /**
   * Drops a session's queued events without recording them, for a session whose
   * events must not be reported at all.
   *
   * @param {string} sessionId - The session whose events are discarded
   */
  #discardEventBuffer(sessionId) {
    this.#eventBuffer = this.#eventBuffer.filter(
      event => event.sessionId !== sessionId
    );
  }

  /**
   * Flushes any events still queued at shutdown into the newtab ping.
   *
   * Sessions that are still open when the feed goes away never receive a
   * NEW_TAB_UNLOAD, so they never call endSession and never drain their own
   * events. Without this, everything they queued is dropped.
   */
  #flushBufferedEventsOnUninit() {
    if (
      !this.#eventBuffer.length ||
      !this.telemetryEnabled ||
      !Services.prefs.getBoolPref(PREF_NEWTAB_PING_ENABLED, true)
    ) {
      return;
    }

    const recordToContentPing =
      this.gleanSessionType === GleanSessionType.PrivateGleanSession;
    this.#clearEventBuffer(recordToContentPing);
    GleanPings.newtab.submit("newtab_session_end");
  }

  /**
   * Records or queues an event based on the current Glean session type.
   * For queueable events (impression, click, section_impression).
   *
   * @param {string} eventName - Name of the event for newtab-content ping
   * @param {object} eventData - Event data for newtab-content ping
   * @param {string} sessionId - The session this event belongs to. Used only to
   *  decide which events a given session drains; it is never recorded, and in
   *  particular is kept out of eventData, which is the newtab-content payload
   *  and deliberately carries no visit id.
   * @param {Function} [callback] - Function to record to non-private newtab ping
   *  Called immediately if in PrivateGleanSession, otherwise its added to eventBuffer
   *  and called when the eventBuffer is cleared. The return value is ignored.
   * @param {string} [dwellToken] - Identifies this row for a later dwell time
   *  write. Never recorded, and deliberately kept out of eventData. Dropped for
   *  a queued event: no click is queued, because the click handler leaves
   *  NormalGleanSession first.
   */
  recordOrQueueEvent(eventName, eventData, sessionId, callback, dwellToken) {
    if (this.gleanSessionType === GleanSessionType.NormalGleanSession) {
      this.#eventBuffer.push({ eventName, eventData, callback, sessionId });
    } else {
      callback?.();
      if (this.privatePingEnabled) {
        this.newtabContentPing.recordEvent(eventName, eventData, dwellToken);
      }
    }
  }

  /**
   * Transitions from NormalGleanSession to PrivateGleanSession.
   * clears the event buffer, sending events to both pings with redaction.
   */
  transitionToPrivateSession() {
    if (this.gleanSessionType === GleanSessionType.PrivateGleanSession) {
      return;
    }

    this.gleanSessionType = GleanSessionType.PrivateGleanSession;
    this.#clearEventBuffer(true);
  }

  get clientInfo() {
    return lazy.ClientEnvironmentBase;
  }

  get canSendUnifiedAdsSpocCallbacks() {
    const unifiedAdsSpocsEnabled = this._prefs.get(
      PREF_UNIFIED_ADS_SPOCS_ENABLED
    );

    return unifiedAdsSpocsEnabled && this.SHOW_SPONSORED_STORIES_ENABLED;
  }

  get canSendUnifiedAdsTilesCallbacks() {
    const unifiedAdsTilesEnabled = this._prefs.get(
      PREF_UNIFIED_ADS_TILES_ENABLED
    );

    return unifiedAdsTilesEnabled && this.SHOW_SPONSORED_TOPSITES_ENABLED;
  }

  get telemetryClientId() {
    Object.defineProperty(this, "telemetryClientId", {
      value: lazy.ClientID.getClientID(),
    });
    return this.telemetryClientId;
  }

  get processStartTs() {
    let startupInfo = Services.startup.getStartupInfo();
    let processStartTs = startupInfo.process.getTime();

    Object.defineProperty(this, "processStartTs", {
      value: processStartTs,
    });
    return this.processStartTs;
  }

  init() {
    // TODO: It looks like (at least) browser_newtab_glean.js and
    // browser_newtab_ping.js depend on most of the following to be executed
    // even if init() is called more than once. That feels fragile.

    this._beginObservingNewtabPingPrefs();

    if (!this._initialized) {
      this._initialized = true;
      Services.obs.addObserver(
        this.browserOpenNewtabStart,
        "browser-open-newtab-start"
      );
      Services.obs.addObserver(this, USER_INTERACTION_ACTIVE);
      Services.obs.addObserver(this, USER_INTERACTION_INACTIVE);
      Services.obs.addObserver(this, CREATED_NAVIGATION_TARGET_TOPIC);
    }

    // Set two scalars for the "deletion-request" ping (See bug 1602064 and 1729474)
    Glean.deletionRequest.impressionId.set(this._impressionId);
    if (!lazy.ContextId.rotationEnabled) {
      Glean.deletionRequest.contextId.set(
        lazy.ContextId.requestSynchronously()
      );
    }
    Glean.newtab.locale.set(Services.locale.appLocaleAsBCP47);
  }

  getOrCreateImpressionId() {
    let impressionId = this._prefs.get(PREF_IMPRESSION_ID);
    if (!impressionId) {
      impressionId = String(Services.uuid.generateUUID());
      this._prefs.set(PREF_IMPRESSION_ID, impressionId);
    }
    return impressionId;
  }

  browserOpenNewtabStart() {
    let now = ChromeUtils.now();
    this._browserOpenNewtabStart = Math.round(this.processStartTs + now);

    ChromeUtils.addProfilerMarker(
      "UserTiming",
      now,
      "browser-open-newtab-start"
    );
  }

  /**
   * Retrieves most recently followed sections (maximum 2 sections)
   *
   * @returns {string[]} comma separated string of section UUID's
   */
  getFollowedSections() {
    const sections =
      this.store?.getState()?.DiscoveryStream.sectionPersonalization;
    if (sections) {
      // filter to only include followedTopics
      const followed = Object.entries(sections).filter(
        ([, info]) => info.isFollowed
      );
      // sort from most recently followed to oldest. If followedAt is falsey, treat it as the oldest
      followed.sort((a, b) => {
        const aDate = a[1].followedAt ? new Date(a[1].followedAt).getTime() : 0;
        const bDate = b[1].followedAt ? new Date(b[1].followedAt).getTime() : 0;
        return bDate - aDate;
      });

      return followed.slice(0, 2).map(([sectionId]) => sectionId);
    }
    return [];
  }

  setLoadTriggerInfo(port) {
    // XXX note that there is a race condition here; we're assuming that no
    // other tab will be interleaving calls to browserOpenNewtabStart and
    // when at.NEW_TAB_INIT gets triggered by RemotePages and calls this
    // method.  For manually created windows, it's hard to imagine us hitting
    // this race condition.
    //
    // However, for session restore, where multiple windows with multiple tabs
    // might be restored much closer together in time, it's somewhat less hard,
    // though it should still be pretty rare.
    //
    // The fix to this would be making all of the load-trigger notifications
    // return some data with their notifications, and somehow propagate that
    // data through closures into the tab itself so that we could match them
    //
    // As of this writing (very early days of system add-on perf telemetry),
    // the hypothesis is that hitting this race should be so rare that makes
    // more sense to live with the slight data inaccuracy that it would
    // introduce, rather than doing the correct but complicated thing.  It may
    // well be worth reexamining this hypothesis after we have more experience
    // with the data.

    let data_to_save;
    try {
      if (!this._browserOpenNewtabStart) {
        throw new Error("No browser-open-newtab-start recorded.");
      }
      data_to_save = {
        load_trigger_ts: this._browserOpenNewtabStart,
        load_trigger_type: "menu_plus_or_keyboard",
      };
    } catch (e) {
      // if no mark was returned, we have nothing to save
      return;
    }
    this.saveSessionPerfData(port, data_to_save);
  }

  /**
   * Get encoded user preferences, multiple prefs will be combined via bitwise OR operator
   */
  get userPreferences() {
    let prefs = 0;

    for (const pref of Object.keys(USER_PREFS_ENCODING)) {
      if (this._prefs.get(pref)) {
        prefs |= USER_PREFS_ENCODING[pref];
      }
    }
    return prefs;
  }

  /**
   * Removes fields that link to any user content preference.
   *
   * @param {*} pingDict Input dictionary
   * @param {boolean} isSponsored Whether this is a sponsored (ad) item.
   *   Sponsored items retain the corpus/recommendation identifiers that
   *   organic items drop, but tile_id is redacted in both cases.
   * @returns {*} Redacted dictionary
   */
  redactNewTabPing(pingDict, isSponsored = false) {
    if (!isSponsored) {
      const {
        // eslint-disable-next-line no-unused-vars
        corpus_item_id,
        // eslint-disable-next-line no-unused-vars
        scheduled_corpus_item_id,
        // eslint-disable-next-line no-unused-vars
        section,
        // eslint-disable-next-line no-unused-vars
        selected_topics,
        // @backward-compat { version 159 }
        // We can remove tile_id from this list once 159 hits release, since at that point,
        // tile_id will have been removed from the newtab ping within metrics.yaml, and there
        // will no longer be a chance of it accidentally slipping through extra_keys.
        // eslint-disable-next-line no-unused-vars
        tile_id,
        // eslint-disable-next-line no-unused-vars
        topic,
        // eslint-disable-next-line no-unused-vars
        variant_id,
        // eslint-disable-next-line no-unused-vars
        source_section_id,
        ...result
      } = pingDict;
      result.content_redacted = true;
      return result;
    }

    const {
      // eslint-disable-next-line no-unused-vars
      section,
      // eslint-disable-next-line no-unused-vars
      selected_topics,
      // @backward-compat { version 159 }
      // We can remove tile_id from this list once 159 hits release, since at that point,
      // tile_id will have been removed from the newtab ping within metrics.yaml, and there
      // will no longer be a chance of it accidentally slipping through extra_keys.
      // eslint-disable-next-line no-unused-vars
      tile_id,
      // eslint-disable-next-line no-unused-vars
      topic,
      // eslint-disable-next-line no-unused-vars
      variant_id,
      // eslint-disable-next-line no-unused-vars
      source_section_id,
      ...result
    } = pingDict;

    result.content_redacted = true;
    return result;
  }

  /**
   * addSession - Start tracking a new session
   *
   * @param  {string} id the portID of the open session
   * @param  {string} url The URL being loaded for this session (optional)
   * @return {object}    Session object
   */
  addSession(id, url) {
    // XXX refactor to use setLoadTriggerInfo or saveSessionPerfData

    // "unexpected" will be overwritten when appropriate
    let load_trigger_type = "unexpected";
    let load_trigger_ts;

    if (!this._aboutHomeSeen && url === "about:home") {
      this._aboutHomeSeen = true;

      // XXX note that this will be incorrectly set in the following cases:
      // session_restore following by clicking on the toolbar button,
      // or someone who has changed their default home page preference to
      // something else and later clicks the toolbar.  It will also be
      // incorrectly unset if someone changes their "Home Page" preference to
      // about:newtab.
      //
      // That said, the ratio of these mistakes to correct cases should
      // be very small, and these issues should follow away as we implement
      // the remaining load_trigger_type values for about:home in issue 3556.
      //
      // XXX file a bug to implement remaining about:home cases so this
      // problem will go away and link to it here.
      load_trigger_type = "first_window_opened";

      // The real perceived trigger of first_window_opened is the OS-level
      // clicking of the icon. We express this by using the process start
      // absolute timestamp.
      load_trigger_ts = this.processStartTs;
    }

    const session = {
      session_id: String(Services.uuid.generateUUID()),
      // "unknown" will be overwritten when appropriate
      page: url ? url : "unknown",
      max_scroll_threshold: 0,
      perf: {
        load_trigger_type,
        is_preloaded: false,
      },
      // Dwell stopwatch: time accrued so far, and when the current run started
      // (null when stopped).
      dwellTimeMs: 0,
      dwellStartedAt: null,
    };

    if (load_trigger_ts) {
      session.perf.load_trigger_ts = load_trigger_ts;
    }

    this.sessions.set(id, session);
    return session;
  }

  /**
   * endSession - Stop tracking a session
   *
   * @param  {string} portID the portID of the session that just closed
   */
  async endSession(portID) {
    const session = this.sessions.get(portID);
    if (!session) {
      // It's possible the tab was never visible – in which case, there was no user session.
      return;
    }

    if (!session.perf.visibility_event_rcvd_ts) {
      // This session was never shown (i.e. the hidden preloaded newtab), there was no user session either.
      // Bail out before recording anything: a newtab the user never saw must
      // not contribute a newtab.closed event, and must not submit a ping that
      // describes it. Anything it managed to queue goes with it.
      this.#discardEventBuffer(session.session_id);
      this.sessions.delete(portID);
      this.#pendingLinkOpens.delete(portID);
      return;
    }

    this.#stopDwellClock(session);

    Glean.newtab.closed.record({ newtab_visit_id: session.session_id });
    if (
      this.telemetryEnabled &&
      Services.prefs.getBoolPref(PREF_NEWTAB_PING_ENABLED, true)
    ) {
      // Record before the submit below: submitting clears ping-lifetime
      // metrics, so a later value would never be sent. Recording here rather
      // than unconditionally also keeps a session from leaving a sample behind
      // for a later ping to pick up when no ping is submitted for this one.
      const dwellMs = Math.round(session.dwellTimeMs);
      if (dwellMs > 0) {
        Glean.newtab.dwellTime.accumulateSingleSample(dwellMs);
      }

      // clear event buffer based on session type
      const recordToContentPing =
        this.gleanSessionType === GleanSessionType.PrivateGleanSession;
      this.#clearEventBuffer(recordToContentPing, session.session_id);
      for (const [threshold, metric] of SCROLL_THRESHOLD_METRICS) {
        // The metric is undefined rather than registered if this add-on has
        // train-hopped to a Firefox that predates it, in which case the rest
        // of the ping should still be submitted.
        Glean.newtab[metric]?.set(session.max_scroll_threshold >= threshold);
      }
      GleanPings.newtab.submit("newtab_session_end");
      if (this.privatePingEnabled) {
        this.configureContentPing();
      }
    }

    let absNow = this.processStartTs + ChromeUtils.now();
    session.session_duration = Math.round(
      absNow - session.perf.visibility_event_rcvd_ts
    );

    // Rounding all timestamps in perf to ease the data processing on the backend.
    // NB: use `TIMESTAMP_MISSING_VALUE` if the value is missing.
    session.perf.visibility_event_rcvd_ts = Math.round(
      session.perf.visibility_event_rcvd_ts
    );
    session.perf.load_trigger_ts = Math.round(
      session.perf.load_trigger_ts || TIMESTAMP_MISSING_VALUE
    );
    session.perf.topsites_first_painted_ts = Math.round(
      session.perf.topsites_first_painted_ts || TIMESTAMP_MISSING_VALUE
    );

    this.sessions.delete(portID);
    this.#pendingLinkOpens.delete(portID);
  }

  /**
   * The frontmost window, or null if Firefox is not the active application.
   * Separate from isDwellTargetInForeground so tests can stub it.
   *
   * @returns {mozIDOMWindowProxy|null}
   */
  getActiveChromeWindow() {
    return Services.focus.activeWindow;
  }

  /**
   * Monotonic time in milliseconds. A method so tests can stub the clock.
   *
   * @returns {number}
   */
  now() {
    return ChromeUtils.now();
  }

  /**
   * Whether a dwell target's <browser> is the selected tab of the frontmost
   * window. A preloaded newtab doesn't qualify because its browser isn't in a
   * tab yet.
   *
   * Known gap: dragging the tab to another window gives it a new <browser>, so
   * a newtab session stops qualifying.
   *
   * @param  {object} target a newtab session or an entry of this.#openedPages
   * @param  {mozIDOMWindowProxy|null} [activeWindow] the frontmost window, read if omitted
   * @returns {boolean}
   */
  isDwellTargetInForeground(
    target,
    activeWindow = this.getActiveChromeWindow()
  ) {
    const browser = target.browserRef?.deref();
    const win = browser?.documentGlobal;
    return (
      !!win &&
      !win.closed &&
      win === activeWindow &&
      win.gBrowser?.selectedBrowser === browser
    );
  }

  /**
   * Everything running a dwell stopwatch: the newtab sessions and the pages
   * they opened.
   *
   * @yields {object} a dwell target
   */
  *#dwellTargets() {
    yield* this.sessions.values();
    yield* this.#openedPages.values();
  }

  /**
   * Start the stopwatch for whatever is in front of the user, and stop it for
   * everything else.
   *
   * Foreground is rechecked on every notification, not just on
   * active/inactive changes. No foreground signal reaches this feed, so these
   * notifications are also how often we sample it.
   */
  #onUserInteractionActive() {
    this.#userActive = true;
    const now = this.now();
    this.#lastActiveAt = now;

    // Before the stopwatches, so a destination that finished loading since the
    // last notification starts accruing in this sweep rather than the next.
    this.#syncOpenedPages(now);
    this.#refreshDwellClocks(now);
  }

  /**
   * Run the stopwatch for whatever is in front of the user, if the user is
   * interacting, and stop it for everything else. Called on each activity
   * notification, and whenever an opened page's tab changes what it shows,
   * so a story replacing the newtab in its tab hands the time over at once.
   *
   * @param  {number} now a this.now() timestamp
   */
  #refreshDwellClocks(now) {
    // Read once for the whole sweep, and only when a target might ask for it.
    const activeWindow =
      this.sessions.size || this.#openedPages.size
        ? this.getActiveChromeWindow()
        : null;

    for (const target of this.#dwellTargets()) {
      const inFront =
        // Newtab sessions have no committed flag. Only opened pages wait.
        (target.committed ?? true) &&
        this.isDwellTargetInForeground(target, activeWindow);
      // Only opened pages keep track of having been seen. Checked whether or
      // not the user is interacting, so a story in front of an idle user still
      // counts as looked at.
      if (inFront && target.seen === false) {
        target.seen = true;
      }
      if (this.#userActive && inFront) {
        target.dwellStartedAt ??= now;
      } else {
        this.#stopDwellClock(target, now);
      }
    }
  }

  /**
   * The last interval saw no interaction, so stop every stopwatch. Credit only
   * up to the last notification that said the user was there. Using that
   * timestamp rather than subtracting the interval pref keeps the cutoff exact
   * when the timer is late or the clock jumps.
   */
  #onUserInteractionInactive() {
    this.#userActive = false;
    const cutoff = this.#lastActiveAt ?? this.now();
    for (const target of this.#dwellTargets()) {
      this.#stopDwellClock(target, cutoff);
    }
    this.#syncOpenedPages(cutoff);
  }

  /**
   * Start a target's stopwatch if the user is already interacting when it
   * comes into view. Without this, a visit shorter than one interval would see
   * no notification and record nothing.
   *
   * @param  {object} target a newtab session or an entry of this.#openedPages
   */
  #startDwellClockIfActive(target) {
    if (
      target.dwellStartedAt === null &&
      this.#userActive &&
      this.isDwellTargetInForeground(target)
    ) {
      target.dwellStartedAt = this.now();
    }
  }

  /**
   * Stop a target's stopwatch, crediting time up to `cutoff`.
   *
   * @param  {object} target a newtab session or an entry of this.#openedPages
   * @param  {number} [cutoff] a this.now() timestamp, defaulting to now
   */
  #stopDwellClock(target, cutoff = this.now()) {
    if (target.dwellStartedAt === null) {
      return;
    }
    target.dwellTimeMs += Math.max(0, cutoff - target.dwellStartedAt);
    target.dwellStartedAt = null;
  }

  /**
   * Handle DWELL_LINK_OPENED, which PlacesFeed dispatches once it knows the
   * <browser> that will receive a link opened from the newtab, and which
   * #trackBrowserOpen sends for a link Firefox opens itself. Starts measuring
   * the active time the user spends on that page.
   *
   * @param {object} action the Action object
   * @param {object} action.data
   * @param {MozBrowser} action.data.browser the <browser> the link loads into
   * @param {string} action.data.dwell_label one of DWELL_LABELS
   * @param {number|string} [action.data.dwell_id] pairs the page with its
   *  click: SafeAnchor's counter, or "browser-N" for a link Firefox opened
   * @param {string} [action.data.port_id] the newtab the link was opened from
   */
  handleDwellLinkOpened(action) {
    const { browser, dwell_label, dwell_id, port_id } = action.data;
    if (!browser || !DWELL_LABELS.has(dwell_label)) {
      return;
    }

    // This <browser> may already be showing a page we are measuring, if the
    // user came back to a newtab in it and clicked again. Finalize that one
    // rather than lose what it accrued.
    const key = browser.permanentKey;
    const previous = this.#openedPages.get(key);
    if (previous) {
      this.#finalizeOpenedPage(previous);
    }
    if (this.#openedPages.size >= MAX_TRACKED_OPENED_PAGES) {
      this.#finalizeOpenedPage(this.#openedPages.values().next().value);
    }

    const page = {
      key,
      label: dwell_label,
      browserRef: new WeakRef(browser),
      dwellTimeMs: 0,
      dwellStartedAt: null,
      // Set once the destination has loaded. Until then the newtab is still
      // what the user is looking at, so nothing accrues.
      committed: false,
      // The document the page is showing, and its address without the
      // fragment.
      windowId: null,
      address: null,
      // Set once the destination has been the selected tab of the frontmost
      // window. A story opened in a background tab and never switched to was
      // not looked at, so it reports no dwell time at all.
      seen: false,
      // Matches the click event that follows, which is the only thing that
      // knows which story this was. Null for a link that sent no token.
      dwellToken: dwellTokenFor(port_id, dwell_id),
      // Set once that click has been found, so the dwell time knows which
      // buffered row to land on.
      dwellPaired: false,
      // Held here because the browser holds its progress listeners weakly.
      progressListener: null,
      // Weak, so a page still showing when its window closes cannot keep the
      // window alive.
      tabRef: null,
      windowRef: null,
      onClosed: null,
    };
    this.#openedPages.set(key, page);
    if (this.#clicksAwaitingDwell.delete(page.dwellToken)) {
      this.#pairPage(page);
    }
    this.#watchOpenedPage(page, browser);
    // A link opened in a new window is only reported once the window has
    // started, by which time the destination may already have loaded.
    const now = this.now();
    this.#syncOpenedPage(page, now);
    if (page.committed) {
      this.#refreshDwellClocks(now);
    }
  }

  /**
   * Mark an opened page as the source of the dwell time for its click, if it
   * is an organic story and the feature is on.
   *
   * @param  {object} page an entry of this.#openedPages
   * @returns {boolean} whether the page was paired
   */
  #pairPage(page) {
    // A topsite or a spoc must never carry a dwell time even if something
    // upstream starts sending a token for one.
    if (
      page.label !== "story_organic" ||
      !this.privatePingEnabled ||
      !this.privatePingDwellTimeEnabled
    ) {
      return false;
    }
    page.dwellPaired = true;
    // Normally a no-op: the click arrives long before the destination loads.
    // It is the other way round for a load that was already complete, such as
    // a cached page.
    this.#trackOpenDwell(page);
    return true;
  }

  /**
   * Pair a dwell measurement with the queued click that started it, so the
   * dwell time can be written onto that row rather than onto a second one.
   *
   * Nothing about the story is copied anywhere. The dwell time lands on the
   * click row, which has already disclosed the identity, and on no other row:
   * the whole privacy argument for the dwell time is that it discloses no
   * content identity the click has not.
   *
   * @param  {string} dwellToken the token both actions carried
   * @param  {object} released the payload handed to the content ping
   */
  #pairDwellWithClick(dwellToken, released) {
    // is_sponsored is what the click itself says was clicked, while the label
    // below comes from the link open that started the measurement. Both are
    // derived from the same prop today, but in two different expressions, so
    // both have to say organic before a dwell time is written. A spoc is
    // passed through randomizeOrganicContentEvent untouched, and so would
    // arrive here carrying its true corpus_item_id.
    if (
      !dwellToken ||
      !released?.corpus_item_id ||
      released.is_sponsored ||
      !this.privatePingEnabled ||
      !this.privatePingDwellTimeEnabled
    ) {
      return;
    }
    for (const page of this.#openedPages.values()) {
      if (page.dwellToken === dwellToken) {
        this.#pairPage(page);
        return;
      }
    }

    this.#clicksAwaitingDwell.add(dwellToken);
    if (this.#clicksAwaitingDwell.size > MAX_TRACKED_OPENED_PAGES) {
      this.#clicksAwaitingDwell.delete(
        this.#clicksAwaitingDwell.values().next().value
      );
    }
  }

  /**
   * Let the queued click for a page that has loaded read the page's active
   * time when the ping goes out, so a user who is still reading reports what
   * they have read so far. #finalizeOpenedPage replaces it with the final
   * measurement if the user leaves first.
   *
   * @param  {object} page an entry of this.#openedPages
   */
  #trackOpenDwell(page) {
    if (
      !page.committed ||
      !page.dwellPaired ||
      !this.privatePingEnabled ||
      !this.privatePingDwellTimeEnabled
    ) {
      return;
    }
    this.newtabContentPing.trackDwellTime(page.dwellToken, () => {
      // Re-checked here to support opt-outs made mid-read.
      if (!this.privatePingEnabled || !this.privatePingDwellTimeEnabled) {
        return null;
      }
      const running =
        page.dwellStartedAt === null
          ? 0
          : Math.max(0, this.now() - page.dwellStartedAt);
      return this.#dwellTimeMsIfSeen(
        page.seen || this.isDwellTargetInForeground(page),
        page.dwellTimeMs + running
      );
    });
  }

  /**
   * Listen for the destination loading, the tab moving on and the tab closing,
   * so each is noticed when it happens rather than at the next activity
   * notification, which can be up to an interval later.
   *
   * @param  {object} page an entry of this.#openedPages
   * @param  {MozBrowser} browser the <browser> showing it
   */
  #watchOpenedPage(page, browser) {
    page.progressListener = {
      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),
      onLocationChange: (webProgress, request, location, flags) => {
        if (
          !webProgress?.isTopLevel ||
          this.#openedPages.get(page.key) !== page
        ) {
          return;
        }
        const now = this.now();
        this.#syncOpenedPage(page, now, {
          uri: location,
          sameDocument: !!(
            flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT
          ),
          errorPage: !!(
            flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE
          ),
        });
        this.#refreshDwellClocks(now);
      },
    };
    try {
      browser.addProgressListener(
        page.progressListener,
        Ci.nsIWebProgress.NOTIFY_LOCATION
      );
    } catch (e) {
      // The activity notifications still keep the page up to date, only later.
      page.progressListener = null;
    }

    page.onClosed = () => {
      if (this.#openedPages.get(page.key) === page) {
        this.#finalizeOpenedPage(page);
      }
    };
    const win = browser.documentGlobal;
    const tab = win?.gBrowser?.getTabForBrowser(browser);
    if (tab) {
      page.tabRef = new WeakRef(tab);
      tab.addEventListener("TabClose", page.onClosed);
    }
    // Closing a window does not close its tabs one by one, so TabClose does
    // not fire for them.
    if (win) {
      page.windowRef = new WeakRef(win);
      win.addEventListener("unload", page.onClosed);
    }
  }

  /**
   * Undo #watchOpenedPage.
   *
   * @param  {object} page an entry of this.#openedPages
   */
  #unwatchOpenedPage(page) {
    if (page.progressListener) {
      try {
        page.browserRef.deref()?.removeProgressListener(page.progressListener);
      } catch (e) {
        // The browser is already gone, and its listeners with it.
      }
      page.progressListener = null;
    }
    page.tabRef?.deref()?.removeEventListener("TabClose", page.onClosed);
    page.windowRef?.deref()?.removeEventListener("unload", page.onClosed);
    page.tabRef = null;
    page.windowRef = null;
  }

  /**
   * Bring the opened pages up to date with what their tabs are showing, on
   * each activity notification. The progress and tab-close listeners
   * normally get there first; this catches anything they missed.
   *
   * @param  {number} cutoff a this.now() timestamp
   */
  #syncOpenedPages(cutoff) {
    for (const page of [...this.#openedPages.values()]) {
      this.#syncOpenedPage(page, cutoff);
    }
  }

  /**
   * Bring one opened page up to date with what its tab is showing. Notice the
   * destination once it has loaded, notice when the tab has moved on to
   * another page, and notice when the tab is gone.
   *
   * A visit moves on when a new document arrives at a different address.
   * Same-document navigation, a fragment link or a pushState as the user
   * scrolls, keeps the visit going. So does a new document at the same
   * address, a reload or a page that replaces itself as it starts up.
   *
   * @param  {object} page an entry of this.#openedPages
   * @param  {number} cutoff a this.now() timestamp
   * @param  {object} [change] what a location change reported, when called from
   *  the progress listener: the new uri, and whether it is a same-document
   *  change or an error page
   */
  #syncOpenedPage(page, cutoff, change = null) {
    const browser = page.browserRef.deref();
    if (!browser?.isConnected) {
      // The tab was closed, or dragged to another window, which swaps in a
      // new <browser>.
      this.#finalizeOpenedPage(page, cutoff);
      return;
    }

    const windowGlobal = browser.browsingContext?.currentWindowGlobal;
    const uri = change ? change.uri : windowGlobal?.documentURI;
    // Only a real web page can be the page the newtab sent the user to. Rules
    // out the about:blank a new tab starts on, an error page if the load
    // fails, and about:newtab if the user goes back. An error page keeps the
    // address that failed, so a location change has to say it is one.
    const isWebPage =
      !change?.errorPage && (uri?.scheme === "http" || uri?.scheme === "https");
    const address = isWebPage ? uri.specIgnoringRef : null;
    const windowId = windowGlobal?.innerWindowId;
    const sameDocument = change
      ? change.sameDocument
      : windowId === page.windowId;

    if (!page.committed) {
      if (isWebPage) {
        page.committed = true;
        page.windowId = windowId;
        page.address = address;
        this.#trackOpenDwell(page);
      }
    } else if (!isWebPage || (!sameDocument && address !== page.address)) {
      this.#finalizeOpenedPage(page, cutoff);
    } else {
      page.windowId = windowId;
      page.address = address;
    }
  }

  /**
   * Stop measuring an opened page and record what it accrued.
   *
   * The sample accumulates on the client until the next metrics ping, so it
   * belongs to no newtab visit.
   *
   * @param  {object} page an entry of this.#openedPages
   * @param  {number} [cutoff] a this.now() timestamp, defaulting to now
   */
  #finalizeOpenedPage(page, cutoff = this.now()) {
    // A tab switched to and closed between two activity notifications was
    // never seen by a sweep, but it is in front of the user as it closes.
    page.seen ||= page.committed && this.isDwellTargetInForeground(page);
    this.#stopDwellClock(page, cutoff);
    this.#openedPages.delete(page.key);
    this.#unwatchOpenedPage(page);

    const dwellMs = Math.round(page.dwellTimeMs);
    if (dwellMs > 0 && this.telemetryEnabled) {
      // Optional chaining: metrics.yaml lives outside this add-on, so a
      // train-hopped build can be running on a Firefox that predates the
      // metric.
      Glean.newtab.openedPageDwellTime?.[page.label]?.accumulateSingleSample(
        dwellMs
      );
    }

    if (!page.dwellPaired) {
      return;
    }
    page.dwellPaired = false;
    // Unlike the distribution above, a story the user bounced off in under one
    // sampling interval still reports, as a dwell time of 0. A fast bounce is
    // the strongest signal that a recommendation was not worth the click. A
    // story that was never in front of the user reports nothing.
    // Re-checking both prefs here to support opt-outs made mid-read.
    if (
      page.committed &&
      this.privatePingEnabled &&
      this.privatePingDwellTimeEnabled
    ) {
      this.newtabContentPing.recordDwellTime(
        page.dwellToken,
        this.#dwellTimeMsIfSeen(page.seen, dwellMs)
      );
    } else {
      // The destination never loaded, or the user opted out while reading.
      // Either way the click must not report a dwell time.
      this.newtabContentPing.recordDwellTime(page.dwellToken, null);
    }
  }

  /**
   * handleNewTabInit - Handle NEW_TAB_INIT, which creates a new session and sets the a flag
   *                    for session.perf based on whether or not this new tab is preloaded
   *
   * @param  {object} action the Action object
   */
  handleNewTabInit(action) {
    const session = this.addSession(
      au.getPortIdOfSender(action),
      action.data.url
    );
    session.perf.is_preloaded =
      action.data.browser.getAttribute("preloadedState") === "preloaded";
    // Weak, so a session that never gets NEW_TAB_UNLOAD cannot keep a <browser>
    // alive past its tab. The preloaded-browser swap reuses this element, so
    // the reference survives it.
    session.browserRef = new WeakRef(action.data.browser);

    // A newtab showing here means whatever page this <browser> was opened to
    // is over. The location change usually gets there first. This covers any
    // route to a newtab that does not, so the two metrics can never both be
    // counting the same tab.
    const opened = this.#openedPages.get(action.data.browser.permanentKey);
    if (opened) {
      this.#finalizeOpenedPage(opened);
    }
  }

  /**
   * Handle NEW_TAB_SCROLL, which records the deepest scroll threshold passed
   * so far in a session. The scroll metrics are set from it in endSession.
   *
   * @param  {object} action the Action object
   */
  handleNewTabScroll(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }
    session.max_scroll_threshold = Math.max(
      session.max_scroll_threshold,
      action.data.threshold
    );
  }

  isPlacesInfluenced() {
    const { values } = this.store?.getState()?.Prefs || {};
    return (
      !!values?.trainhopConfig?.sov?.enabled ||
      lazy.hasScoringFlag(values?.adsBackendConfig)
    );
  }

  frecencyBoostedHasExposure() {
    const { values } = this.store?.getState()?.Prefs || {};
    return values?.[PREF_SOV_FRECENCY_EXPOSURE];
  }

  async handleTopSitesSponsoredImpressionStats(action) {
    const { data } = action;
    const {
      type,
      position,
      source,
      advertiser_name,
      visible_topsites,
      frecency_boosted = false,
      is_ad_eligible_position,
    } = data;
    // Legacy telemetry expects 1-based tile positions.
    const legacyTelemetryPosition = position + 1;

    const session = this.sessions.get(au.getPortIdOfSender(action));

    if (type === "impression") {
      Glean.contextualServicesTopsites.impression[
        `${source}_${legacyTelemetryPosition}`
      ].add(1);
      if (session) {
        if (this.isPlacesInfluenced()) {
          const eventData = {
            advertiser_name,
            is_sponsored: true,
            position,
            visible_topsites,
            frecency_boosted,
            frecency_boosted_has_exposure: this.frecencyBoostedHasExposure(),
            ...(is_ad_eligible_position
              ? { is_ad_eligible_position: true }
              : {}),
          };
          this.recordOrQueueEvent(
            "topSitesImpression",
            eventData,
            session.session_id
          );
        } else {
          const gleanData = {
            advertiser_name,
            newtab_visit_id: session.session_id,
            is_sponsored: true,
            position,
            visible_topsites,
            ...(is_ad_eligible_position
              ? { is_ad_eligible_position: true }
              : {}),
          };
          Glean.topsites.impression.record(gleanData);
        }
      }
    } else if (type === "click") {
      Glean.contextualServicesTopsites.click[
        `${source}_${legacyTelemetryPosition}`
      ].add(1);
      if (session) {
        if (this.isPlacesInfluenced()) {
          const eventData = {
            advertiser_name,
            is_sponsored: true,
            position,
            visible_topsites,
            frecency_boosted,
            frecency_boosted_has_exposure: this.frecencyBoostedHasExposure(),
          };
          this.recordOrQueueEvent(
            "topSitesClick",
            eventData,
            session.session_id
          );
        } else {
          const gleanData = {
            advertiser_name,
            newtab_visit_id: session.session_id,
            is_sponsored: true,
            position,
            visible_topsites,
            ...(data.event_source
              ? { event_source: data.event_source, background: data.background }
              : {}),
          };
          Glean.topsites.click.record(gleanData);
        }
      }
    } else {
      console.error("Unknown ping type for sponsored TopSites impression");
      return;
    }

    if (data.reporting_url && this.canSendUnifiedAdsTilesCallbacks) {
      if (this._adsClient) {
        this.sendMacCallbackEvent(data.reporting_url, type);
      } else {
        // Send callback events to MARS unified ads api
        this.sendUnifiedAdsCallbackEvent({
          url: data.reporting_url,
          position,
        });
      }
    }
  }

  handleTopSitesOrganicImpressionStats(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }
    const visible_topsites = action.data?.visible_topsites;

    switch (action.data?.type) {
      case "impression":
        Glean.topsites.impression.record({
          newtab_visit_id: session.session_id,
          is_sponsored: false,
          position: action.data.position,
          is_pinned: !!action.data.isPinned,
          visible_topsites,
          smart_scores: JSON.stringify(action.data.smart_scores),
          smart_weights: JSON.stringify(action.data.smart_weights),
          ...(action.data.is_ad_eligible_position
            ? { is_ad_eligible_position: true }
            : {}),
        });
        break;

      case "click":
        Glean.topsites.click.record({
          newtab_visit_id: session.session_id,
          is_sponsored: false,
          position: action.data.position,
          is_pinned: !!action.data.isPinned,
          visible_topsites,
          smart_scores: JSON.stringify(action.data.smart_scores),
          smart_weights: JSON.stringify(action.data.smart_weights),
          ...(action.data.event_source
            ? {
                event_source: action.data.event_source,
                background: action.data.background,
              }
            : {}),
        });
        break;

      default:
        break;
    }
  }

  /**
   * Records the duration that spoc (ads) placeholders were visible to the user.
   * This tracks how long placeholder content is shown before being replaced
   * with actual sponsored content when using onDemand mode.
   *
   * @param {object} action
   * @param {object} action.data
   * @param {number} action.data.duration - Duration in milliseconds
   */
  handleSpocPlaceholderDuration(action) {
    const { duration } = action.data;
    if (duration !== undefined && duration >= 0) {
      Glean.pocket.spocPlaceholderDuration.accumulateSingleSample(duration);
    }
  }

  handleUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }

    switch (action.data?.event) {
      case "PIN": {
        Glean.topsites.pin.record({
          newtab_visit_id: session.session_id,
          position: action.data.action_position,
        });
        break;
      }
      case "UNPIN": {
        Glean.topsites.unpin.record({
          newtab_visit_id: session.session_id,
          position: action.data.action_position,
        });
        break;
      }
      case "TOP_SITES_ADD": {
        Glean.topsites.add.record({
          newtab_visit_id: session.session_id,
          position: action.data.action_position,
        });
        break;
      }
      case "TOP_SITES_EDIT": {
        Glean.topsites.edit.record({
          newtab_visit_id: session.session_id,
          position: action.data.action_position,
          has_title_changed: action.data.has_title_changed,
          has_url_changed: action.data.has_url_changed,
        });
        break;
      }
      case "WEATHER_DETECT_LOCATION": {
        Glean.newtab.weatherDetectLocation.record({
          newtab_visit_id: session.session_id,
        });
        break;
      }
      case "SHOW_PERSONALIZE": {
        Glean.newtab.customizePanelOpen.record({
          newtab_visit_id: session.session_id,
        });
        break;
      }
      case "SHOW_PERSONALIZE_SUBPANEL": {
        Glean.newtab.customizePanelSubpanelOpen.record({
          newtab_visit_id: session.session_id,
          panel: action.data.source,
        });
        break;
      }
      case "EXPLORE_MORE_THEMES_CLICK": {
        Glean.newtab.appearanceExploreMoreThemesClick.record({
          newtab_visit_id: session.session_id,
        });
        break;
      }
      case "OPEN_NEW_WINDOW":
      case "OPEN_PRIVATE_WINDOW": {
        const { action_position, source, value } = action.data;
        if (source !== "TOP_SITES" || value?.card_type === "search") {
          break;
        }
        const row = this.store?.getState()?.TopSites?.rows?.[action_position];
        if (value?.card_type === "spoc") {
          this.recordSponsoredTopSiteClick(row, {
            position: action_position,
            event_source: "CONTEXT_MENU",
            meta: action.meta,
          });
          break;
        }
        this.handleTopSitesOrganicImpressionStats({
          data: {
            type: "click",
            position: action_position,
            isPinned: row?.isPinned,
            smart_scores: row?.scores,
            smart_weights: row?.weights,
            event_source: "CONTEXT_MENU",
          },
          meta: action.meta,
        });
        break;
      }
    }
  }

  /**
   * @returns Flat list of all articles for the New Tab. Does not include spocs (ads)
   */
  getAllRecommendations() {
    const merinoData = this.store?.getState()?.DiscoveryStream?.feeds.data;
    return Object.values(merinoData ?? {}).flatMap(
      feed => feed?.data?.recommendations ?? []
    );
  }

  /**
   * @returns Flat list of all sections for the New Tab, each with its assigned layout.
   */
  getAllSections() {
    const merinoData = this.store?.getState()?.DiscoveryStream?.feeds.data;
    return Object.values(merinoData ?? {}).flatMap(
      feed => feed?.data?.sections ?? []
    );
  }

  /**
   * @returns Number of articles for the New Tab. Does not include spocs (ads)
   */
  getRecommendationCount() {
    const merinoData = this.store?.getState()?.DiscoveryStream?.feeds.data;
    return Object.values(merinoData ?? {}).reduce(
      (count, feed) => count + (feed.data?.recommendations?.length || 0),
      0
    );
  }

  /**
   * Occasionally replaces a content item with another that is in the feed.
   *
   * @param {*} item
   * @param {object} session The session the event belongs to. When it has
   *   sectionPositions, only items from rendered sections can be swapped in.
   * @returns Same item, but another item occasionally based on probablility setting.
   * Sponsored items are unchanged
   */
  randomizeOrganicContentEvent(item, session) {
    if (item.is_sponsored) {
      return item; // Don't alter spocs
    }

    const epsilon =
      this._privateRandomContentTelemetryProbablityValues?.epsilon ?? 0;
    if (!epsilon) {
      return item;
    }
    if (!("n" in this._privateRandomContentTelemetryProbablityValues)) {
      // We cache the number of items in the feed because it's computationally expensive to compute.
      // This may not be ideal, but the number of content items typically is very similar over reloads
      this._privateRandomContentTelemetryProbablityValues.n =
        this.getRecommendationCount();
    }
    const { n } = this._privateRandomContentTelemetryProbablityValues;
    if (!n || n < 10) {
      // None or very view articles. We're in an intermediate or error state.
      return item;
    }
    const cache_key = `probability_${epsilon}_${n}`; // Lookup of probability for a item size
    if (!(cache_key in this._privateRandomContentTelemetryProbablityValues)) {
      this._privateRandomContentTelemetryProbablityValues[cache_key] = {
        p: Math.exp(epsilon) / (Math.exp(epsilon) + n - 1),
      };
    }

    const { p } =
      this._privateRandomContentTelemetryProbablityValues[cache_key];
    if (lazy.NewTabContentPing.decideWithProbability(p)) {
      return item;
    }
    const sectionPositions = session?.sectionPositions;
    let allRecs = this.getAllRecommendations(); // Number of recommendations has changed
    if (sectionPositions) {
      allRecs = allRecs.filter(rec => sectionPositions.has(rec.section));
    }
    if (!allRecs.length) {
      return item;
    }

    // Update number of recs for next round of checks for next round
    this._privateRandomContentTelemetryProbablityValues.n = allRecs.length;

    const randomIndex = lazy.NewTabContentPing.secureRandIntInRange(
      allRecs.length
    );
    let randomItem = allRecs[randomIndex];
    const resultItem = {
      ...item,
      topic: randomItem.topic,
      corpus_item_id: randomItem.corpus_item_id,
      source_section_id: randomItem.source_section_id ?? randomItem.section,
    };
    // If we're replacing a non top stories item, then assign the appropriate
    // section and layout to the item
    if (
      resultItem.section &&
      resultItem.section !== TOP_STORIES_SECTION_NAME &&
      randomItem.section
    ) {
      resultItem.section = randomItem.section;
      resultItem.section_position = sectionPositions?.get(randomItem.section);
      resultItem.layout_name = this.getAllSections().find(
        section => section.sectionKey === randomItem.section
      )?.layout?.name;
      // variant_id is section-level, so only adopt the swapped item's when we adopt its section.
      resultItem.variant_id = randomItem.variant_id;
    }
    return resultItem;
  }

  handleDiscoveryStreamUserEvent(action) {
    this.handleUserEvent({
      ...action,
      data: {
        ...(action.data || {}),
        value: {
          ...(action.data?.value || {}),
        },
      },
    });
    const session = this.sessions.get(au.getPortIdOfSender(action));

    switch (action.data?.event) {
      case "OPEN_PRIVATE_WINDOW":
      case "OPEN_NEW_WINDOW":
      case "CLICK": {
        const {
          background,
          card_column,
          card_type,
          corpus_item_id,
          event_source,
          feature,
          format,
          is_section_followed,
          layout_name,
          matches_selected_topic,
          received_rank,
          recommended_at,
          scheduled_corpus_item_id,
          section_position,
          section,
          selected_topics,
          shim,
          source_section_id,
          topic,
          variant_id,
        } = action.data.value ?? {};

        if (
          action.data.source === "POPULAR_TOPICS" ||
          card_type === "topics_widget"
        ) {
          Glean.pocket.topicClick.record({
            newtab_visit_id: session.session_id,
            topic,
          });
        } else if (action.data.source === "FEATURE_HIGHLIGHT") {
          Glean.newtab.tooltipClick.record({
            newtab_visit_id: session.session_id,
            feature,
          });
        } else if (["spoc", "organic"].includes(card_type)) {
          const is_sponsored = card_type === "spoc";
          const gleanData = {
            newtab_visit_id: session.session_id,
            is_sponsored,
            ...(format ? { format } : {}),
            ...(card_column ? { card_column } : {}),
            ...(section
              ? {
                  section,
                  section_position,
                  ...(this.sectionsPersonalizationEnabled
                    ? { is_section_followed: !!is_section_followed }
                    : {}),
                  layout_name,
                }
              : {}),
            matches_selected_topic,
            selected_topics,
            topic,
            variant_id,
            source_section_id: source_section_id ?? section,
            position: action.data.action_position,
            event_source,
            // We conditionally add in a few props.
            ...(corpus_item_id ? { corpus_item_id } : {}),
            ...(scheduled_corpus_item_id ? { scheduled_corpus_item_id } : {}),
            ...(corpus_item_id || scheduled_corpus_item_id
              ? {
                  received_rank,
                  recommended_at,
                }
              : {}),
          };
          if (this.trainhopClickOnlyEnabled) {
            this.transitionToPrivateSession();
          }
          const releasedContentData = this.randomizeOrganicContentEvent(
            gleanData,
            session
          );
          const dwellToken = dwellTokenFor(
            au.getPortIdOfSender(action),
            action.data.value?.dwell_id
          );
          this.recordOrQueueEvent(
            "click",
            releasedContentData,
            session.session_id,
            () => {
              Glean.pocket.click.record({
                ...this.redactNewTabPing(gleanData, is_sponsored),
                newtab_visit_id: session.session_id,
                ...(background !== undefined ? { background } : {}),
              });
            },
            dwellToken
          );
          this.#pairDwellWithClick(dwellToken, releasedContentData);
          if (shim) {
            if (this.canSendUnifiedAdsSpocCallbacks) {
              if (this._adsClient) {
                // Send callback event via MAC
                this.sendMacCallbackEvent(shim, "click");
              } else {
                // Send unified ads callback event
                this.sendUnifiedAdsCallbackEvent({
                  url: shim,
                  position: action.data.action_position,
                });
              }
            }
          }
        }

        break;
      }
      // Bug 1969452 - Feature Highlight Telemetry Events
      case "FEATURE_HIGHLIGHT_DISMISS":
      case "FEATURE_HIGHLIGHT_IMPRESSION":
      case "FEATURE_HIGHLIGHT_OPEN": {
        // Note that Feature Highlight CLICK events are covered via newtab.tooltipClick Glean event
        const { feature } = action.data.value ?? {};

        if (!feature) {
          throw new Error(
            `Feature ID parameter is missing from ${action.data?.event}`
          );
        }

        if (action.data.event === "FEATURE_HIGHLIGHT_DISMISS") {
          Glean.newtab.featureHighlightDismiss.record({
            newtab_visit_id: session.session_id,
            feature,
          });
        } else if (action.data.event === "FEATURE_HIGHLIGHT_IMPRESSION") {
          Glean.newtab.featureHighlightImpression.record({
            newtab_visit_id: session.session_id,
            feature,
          });
        } else if (action.data.event === "FEATURE_HIGHLIGHT_OPEN") {
          Glean.newtab.featureHighlightOpen.record({
            newtab_visit_id: session.session_id,
            feature,
          });
        }

        break;
      }
    }
  }

  /**
   * This function submits callback events to MARS via MAC.
   */

  async sendMacCallbackEvent(url, event) {
    if (!url) {
      throw new Error(
        `[Unified ads callback] Missing argument (No url). Cannot send telemetry event.`
      );
    }

    // Make sure the callback endpoint is allowed
    const allowed = this.allowedEndpoints;
    if (!allowed.some(prefix => url.startsWith(prefix))) {
      throw new Error(
        `[Unified ads callback] Not one of allowed prefixes (${allowed})`
      );
    }

    const options = lazy.AdsClient.callbackOptions();
    if (event === "impression") {
      await this._adsClient.recordImpression(url, options);
    } else if (event === "click") {
      await this._adsClient.recordClick(url, options);
    } else {
      throw new Error(
        `[Unified ads callback] Unknown callback event type. Cannot send telemetry event.`
      );
    }
  }

  /**
   * This function submits callback events to the MARS unified ads service.
   */

  async sendUnifiedAdsCallbackEvent(data = { url: null, position: null }) {
    if (!data.url) {
      throw new Error(
        `[Unified ads callback] Missing argument (No url). Cannot send telemetry event.`
      );
    }

    // data.position can be 0 (0)
    if (!data.position && data.position !== 0) {
      throw new Error(
        `[Unified ads callback] Missing argument (No position). Cannot send telemetry event.`
      );
    }

    // Make sure the callback endpoint is allowed
    const allowed = this.allowedEndpoints;
    if (!allowed.some(prefix => data.url.startsWith(prefix))) {
      throw new Error(
        `[Unified ads callback] Not one of allowed prefixes (${allowed})`
      );
    }

    const url = new URL(data.url);
    url.searchParams.append("position", data.position);

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

    let fetchPromise;
    const fetchUrl = url.toString();

    if (marsOhttpEnabled) {
      if (!ohttpRelayURL) {
        console.error(
          new Error(
            `OHTTP was configured for ${fetchUrl} but we didn't have a valid ohttpRelayURL`
          )
        );
      }
      if (!ohttpConfigURL) {
        console.error(
          new Error(
            `OHTTP was configured for ${fetchUrl} but we didn't have a valid ohttpConfigURL`
          )
        );
      }

      const headers = new Headers();
      const controller = new AbortController();
      const { signal } = controller;

      const options = {
        method: "GET",
        headers,
        signal,
      };

      let config = await lazy.ObliviousHTTP.getOHTTPConfig(ohttpConfigURL);
      if (!config) {
        console.error(
          new Error(
            `OHTTP was configured for ${fetchUrl} but we couldn't fetch a valid config`
          )
        );
      }

      fetchPromise = lazy.ObliviousHTTP.ohttpRequest(
        ohttpRelayURL,
        config,
        fetchUrl,
        options
      );
    } else {
      fetchPromise = fetch(fetchUrl);
    }

    try {
      await fetchPromise;
    } catch (error) {
      console.error("Error:", error);
    }
  }

  async sendPageTakeoverData() {
    if (this.telemetryEnabled) {
      const value = {};
      let homeAffected = false;
      let newtabCategory = "disabled";
      let homePageCategory = "disabled";

      // Check whether or not about:home and about:newtab are set to a custom URL.
      // If so, classify them.
      if (Services.prefs.getBoolPref("browser.newtabpage.enabled")) {
        newtabCategory = "enabled";
        if (
          lazy.AboutNewTab.newTabURLOverridden &&
          !lazy.ExtensionUtils.isExtensionUrl(lazy.AboutNewTab.newTabURL)
        ) {
          value.newtab_url_category = await this._classifySite(
            lazy.AboutNewTab.newTabURL
          );
          newtabCategory = value.newtab_url_category;
        }
      }
      // Check if the newtab page setting is controlled by an extension.
      await lazy.ExtensionSettingsStore.initialize();
      const newtabExtensionInfo = lazy.ExtensionSettingsStore.getSetting(
        "url_overrides",
        "newTabURL"
      );
      if (newtabExtensionInfo && newtabExtensionInfo.id) {
        value.newtab_extension_id = newtabExtensionInfo.id;
        newtabCategory = "extension";
      }

      const homePageURL = lazy.HomePage.get();
      if (
        !["about:home", "about:blank", BLANK_HOMEPAGE_URL].includes(
          homePageURL
        ) &&
        !lazy.ExtensionUtils.isExtensionUrl(homePageURL)
      ) {
        value.home_url_category = await this._classifySite(homePageURL);
        homeAffected = true;
        homePageCategory = value.home_url_category;
      }
      const homeExtensionInfo = lazy.ExtensionSettingsStore.getSetting(
        "prefs",
        "homepage_override"
      );
      if (homeExtensionInfo && homeExtensionInfo.id) {
        value.home_extension_id = homeExtensionInfo.id;
        homeAffected = true;
        homePageCategory = "extension";
      }
      if (!homeAffected && !lazy.HomePage.overridden) {
        homePageCategory = "enabled";
      }

      Glean.newtab.newtabCategory.set(newtabCategory);
      Glean.newtab.homepageCategory.set(homePageCategory);

      if (Services.prefs.getBoolPref(PREF_NEWTAB_PING_ENABLED, true)) {
        if (this.privatePingEnabled) {
          this.configureContentPing();
        }
        GleanPings.newtab.submit("component_init");
      }
    }
  }

  /**
   * Populates the newtab-content ping with metrics, and the schedules
   * submission of the ping via NewTabContentPing.
   */
  async configureContentPing() {
    let privateMetrics = {};
    const prefs = this.store?.getState()?.Prefs.values; // Needed for experimenter configs
    // An override from model response may turn the inclusion of inferred interests
    // in telemetry off but not on.
    const includeInferredInterestsInTelemetry =
      this.privatePingInferredInterestsEnabled &&
      (this.inferredTelemetrySettingsOverrides?.iv_in_telemetry ?? true);
    const inferredInterests =
      includeInferredInterestsInTelemetry && this.inferredInterests;
    if (inferredInterests) {
      privateMetrics.inferredInterests = inferredInterests;
    }
    let epsilonMicroRaw =
      prefs?.trainhopConfig?.newtabPrivatePing?.[
        TRAINHOP_PREF_RANDOM_CLICK_PROBABILITY_MICRO
      ] || 0;
    if (
      this.inferredTelemetrySettingsOverrides
        ?.random_content_click_probability_epsilon_micro !== undefined
    ) {
      epsilonMicroRaw =
        this.inferredTelemetrySettingsOverrides
          .random_content_click_probability_epsilon_micro;
    }

    this._privateRandomContentTelemetryProbablityValues = {
      epsilon: epsilonMicroRaw / 1e6,
    };
    const privatePingConfig = prefs?.trainhopConfig?.newtabPrivatePing || {};
    // Set the daily cap for content pings
    const impressionCap = privatePingConfig[TRAINHOP_PREF_DAILY_EVENT_CAP] || 0;
    this.newtabContentPing.setMaxEventsPerDay(impressionCap);
    let clickDailyCap =
      privatePingConfig[TRAINHOP_PREF_DAILY_CLICK_EVENT_CAP] || 0;
    if (
      this.inferredTelemetrySettingsOverrides?.daily_click_event_cap !==
      undefined
    ) {
      clickDailyCap =
        this.inferredTelemetrySettingsOverrides.daily_click_event_cap;
    }
    this.newtabContentPing.setMaxClickEventsPerDay(clickDailyCap);
    const weeklyClickCap =
      privatePingConfig[TRAINHOP_PREF_WEEKLY_CLICK_EVENT_CAP] || 0;
    this.newtabContentPing.setMaxClickEventsPerWeek(weeklyClickCap);

    // When we have a coarse interest vector we want to make sure there isn't
    // anything additionaly identifable as a unique identifier. Therefore,
    // when interest vectors are used we reduce our context profile somewhat.
    const reduceTrackingInformation = !!inferredInterests;

    if (!reduceTrackingInformation) {
      const followed = this.getFollowedSections();
      privateMetrics.followedSections = followed;
    }
    const surfaceId = this._prefs.get(PREF_SURFACE_ID);
    if (surfaceId) {
      privateMetrics.surfaceId = surfaceId;
    }

    const curCountry = lazy.Region.home;
    if (PRIVATE_PING_SURFACE_COUNTRY_MAP[surfaceId]) {
      // This is a market that supports inferred
      // Only include supported current countries for the surface to reduce identifiability.
      // Default to first country on the list
      privateMetrics.country = PRIVATE_PING_SURFACE_COUNTRY_MAP[
        surfaceId
      ].includes(curCountry)
        ? curCountry
        : PRIVATE_PING_SURFACE_COUNTRY_MAP[surfaceId][0];
    }

    if (
      prefs?.inferredPersonalizationConfig?.normalized_time_zone_offset &&
      surfaceId
    ) {
      privateMetrics.utcOffset = lazy.NewTabUtils.getUtcOffset(surfaceId);
    }
    // To prevent fingerprinting we only send one current experiment / branch
    const experimentMetadata =
      lazy.NimbusFeatures.pocketNewtab.getEnrollmentMetadata();
    const isMerinoFeedExperiment = Services.prefs.getBoolPref(
      PREF_MERINO_FEED_EXPERIMENT
    );

    privateMetrics.experimentName =
      (isMerinoFeedExperiment && experimentMetadata?.slug) || "";
    privateMetrics.experimentBranch =
      (isMerinoFeedExperiment && experimentMetadata?.branch) || "";
    privateMetrics.pingVersion = CONTENT_PING_VERSION;
    this.newtabContentPing.scheduleSubmission(privateMetrics);
  }

  async onAction(action) {
    // These all go to one place, so they are matched as a set rather than as
    // a branch each in the switch below.
    if (WALLPAPER_USER_EVENTS.has(action.type)) {
      this.handleWallpaperUserEvent(action);
      return;
    }

    switch (action.type) {
      case at.INIT:
        this.init();
        await this.sendPageTakeoverData();
        break;
      case at.NEW_TAB_INIT:
        this.handleNewTabInit(action);
        break;
      case at.NEW_TAB_UNLOAD:
        this.endSession(au.getPortIdOfSender(action));
        break;
      case at.NEW_TAB_SCROLL:
        this.handleNewTabScroll(action);
        break;
      case at.DWELL_LINK_OPENED:
        this.handleDwellLinkOpened(action);
        break;
      case at.SAVE_SESSION_PERF_DATA:
        this.saveSessionPerfData(au.getPortIdOfSender(action), action.data);
        break;
      case at.DISCOVERY_STREAM_IMPRESSION_STATS:
        this.handleDiscoveryStreamImpressionStats(
          au.getPortIdOfSender(action),
          action.data
        );
        break;
      case at.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION:
        this.handleSpocPlaceholderDuration(action);
        break;
      case at.DISCOVERY_STREAM_USER_EVENT:
        this.handleDiscoveryStreamUserEvent(action);
        break;
      case at.BROWSER_LINK_OPEN_INTENT:
        this.handleBrowserLinkOpenIntent(action);
        break;
      case at.TELEMETRY_USER_EVENT:
        this.handleUserEvent(action);
        break;
      case at.TOP_SITES_SPONSORED_IMPRESSION_STATS:
        this.handleTopSitesSponsoredImpressionStats(action);
        break;
      case at.TOP_SITES_ORGANIC_IMPRESSION_STATS:
        this.handleTopSitesOrganicImpressionStats(action);
        break;
      case at.UNINIT:
        this.uninit();
        break;
      case at.ABOUT_SPONSORED_TOP_SITES:
        this.handleAboutSponsoredTopSites(action);
        break;
      case at.BLOCK_URL:
        this.handleBlockUrl(action);
        break;
      case at.SET_PREF:
        this.handleSetPref(action);
        break;
      case at.WEATHER_IMPRESSION:
      case at.WEATHER_LOAD_ERROR:
      case at.WEATHER_OPEN_PROVIDER_URL:
      case at.WEATHER_LOCATION_DATA_UPDATE:
      case at.WEATHER_OPT_IN_PROMPT_SELECTION:
        this.handleWeatherUserEvent(action);
        break;
      case at.TOPIC_SELECTION_USER_OPEN:
      case at.TOPIC_SELECTION_USER_DISMISS:
      case at.TOPIC_SELECTION_USER_SAVE:
        this.handleTopicSelectionUserEvent(action);
        break;
      case at.CARD_SECTIONS_ORDER: {
        const session = this.sessions.get(au.getPortIdOfSender(action));
        if (session) {
          session.sectionPositions = new Map(
            action.data.sections.map((sectionKey, i) => [sectionKey, i])
          );
        }
        break;
      }
      case at.BLOCK_SECTION:
      // Intentional fall-through
      case at.CARD_SECTION_IMPRESSION:
      // Intentional fall-through
      case at.CLICK_SECTION_LEARN_MORE:
      // Intentional fall-through
      case at.FOLLOW_SECTION:
      // Intentional fall-through
      case at.UNBLOCK_SECTION:
      // Intentional fall-through
      case at.UNFOLLOW_SECTION: {
        this.handleCardSectionUserEvent(action);
        break;
      }
      case at.CAROUSEL_NAVIGATE:
      // Intentional fall-through
      case at.CAROUSEL_TOGGLE_AUTOPLAY: {
        this.handleCarouselUserEvent(action);
        break;
      }
      case at.INLINE_SELECTION_CLICK:
      // Intentional fall-through
      case at.INLINE_SELECTION_IMPRESSION:
        this.handleInlineSelectionUserEvent(action);
        break;
      case at.TOPIC_NAVIGATION_CLICK:
        this.handleTopicNavigationUserEvent(action);
        break;
      case at.REPORT_AD_SUBMIT:
        this.handleReportAdUserEvent(action);
        break;
      case at.REPORT_CONTENT_OPEN:
      case at.REPORT_CONTENT_SUBMIT:
        this.handleReportContentUserEvent(action);
        break;
      case at.WIDGETS_LISTS_USER_EVENT:
      case at.WIDGETS_LISTS_USER_IMPRESSION:
      case at.WIDGETS_TIMER_USER_EVENT:
      case at.WIDGETS_TIMER_USER_IMPRESSION:
        this.handleWidgetsUserEvent(action);
        break;
      case at.SPACES_USER_EVENT:
        this.handleSpacesUserEvent(action);
        break;
      case at.WIDGETS_USER_EVENT:
        this.handleUnifiedWidgetUserEvent(action);
        break;
      case at.WIDGETS_IMPRESSION:
        this.handleUnifiedWidgetImpression(action);
        break;
      case at.WIDGETS_CONTAINER_ACTION:
        this.handleUnifiedWidgetContainerAction(action);
        break;
      case at.WIDGETS_ENABLED:
        this.handleUnifiedWidgetEnabled(action);
        break;
      case at.WIDGETS_HIDE_ALL:
        this.handleWidgetsHideAll(action);
        break;
      case at.WIDGETS_ERROR:
        this.handleUnifiedWidgetError(action);
        break;
      case at.PROMO_CARD_CLICK:
      case at.PROMO_CARD_DISMISS:
      case at.PROMO_CARD_IMPRESSION:
        this.handlePromoCardUserEvent(action);
        break;
      case at.PREFS_INITIAL_VALUES:
        this.initializeGleanSession();
        this.recordEnabledWidgets();
        this.initializeMac();
        this.recordPageLayoutVariant();
        break;
      case at.PREF_CHANGED:
        // Turning history off hides history-dependent widgets, and resolving
        // the region reveals trainhop-enabled ones, so the enabled list has to
        // be re-read: unlike a widget's own pref, these flip enablement without
        // any widget toggle to hang the update off.
        if (["recordsHistory", "region"].includes(action.data?.name)) {
          this.recordEnabledWidgets();
        }
        if (action.data?.name === PREF_HOME_WIDGETS) {
          this.recordHomeWidgets();
        }
        break;
      case at.WIDGETS_EXTENSIONS_UPDATE:
        this.recordEnabledWidgets();
        break;
    }
  }

  handlePromoCardUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
      };

      switch (action.type) {
        case at.PROMO_CARD_CLICK:
          Glean.newtab.promoCardClick.record(payload);
          break;
        case at.PROMO_CARD_DISMISS:
          Glean.newtab.promoCardDismiss.record(payload);
          break;
        case at.PROMO_CARD_IMPRESSION:
          Glean.newtab.promoCardImpression.record(payload);
          break;
      }
    }
  }

  // TODO Bug 2012779 - Remove this method and legacy widget-specific telemetry
  // events once migration to unified telemetry is complete.
  handleWidgetsUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
      };
      switch (action.type) {
        case "WIDGETS_LISTS_USER_EVENT":
          Glean.newtab.widgetsListsUserEvent.record({
            ...payload,
            user_action: action.data.userAction,
          });
          break;
        case "WIDGETS_LISTS_USER_IMPRESSION":
          Glean.newtab.widgetsListsImpression.record(payload);
          break;
        case "WIDGETS_TIMER_USER_EVENT":
          Glean.newtab.widgetsTimerUserEvent.record({
            ...payload,
            user_action: action.data.userAction,
          });
          break;
        case "WIDGETS_TIMER_USER_IMPRESSION":
          Glean.newtab.widgetsTimerImpression.record(payload);
          break;
      }
    }
  }

  handleSpacesUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      Glean.newtab.spacesSwitch.record({
        newtab_visit_id: session.session_id,
        space: action.data.space,
        previous_space: action.data.previous_space,
        method: action.data.method,
      });
    }
  }

  handleUnifiedWidgetUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
        widget_name: action.data.widget_name,
        widget_source: action.data.widget_source || "widget",
        user_action: action.data.user_action,
      };

      if (action.data.widget_size) {
        payload.widget_size = action.data.widget_size;
      }

      if (action.data.widget_variant) {
        payload.widget_variant = action.data.widget_variant;
      }

      if (action.data.action_value !== undefined) {
        payload.action_value = String(action.data.action_value);
      }

      if (typeof action.data.widget_location === "string") {
        payload.widget_location = action.data.widget_location;
      }

      Glean.newtab.widgetsUserEvent.record(payload);
    }
  }

  handleUnifiedWidgetImpression(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
        widget_name: action.data.widget_name,
      };

      if (action.data.widget_size) {
        payload.widget_size = action.data.widget_size;
      }

      if (typeof action.data.widget_location === "string") {
        payload.widget_location = action.data.widget_location;
      }

      if (action.data.widget_variant) {
        payload.widget_variant = action.data.widget_variant;
      }

      Glean.newtab.widgetsImpression.record(payload);
    }
  }

  handleUnifiedWidgetContainerAction(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
        action_type: action.data.action_type,
      };

      if (action.data.widget_size) {
        payload.widget_size = action.data.widget_size;
      }

      if (action.data.action_value) {
        payload.action_value = action.data.action_value;
      }

      Glean.newtab.widgetsContainerAction.record(payload);
    }
  }

  handleUnifiedWidgetEnabled(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
        widget_name: action.data.widget_name,
        widget_source: action.data.widget_source || "widget",
        enabled: action.data.enabled,
      };

      if (action.data.widget_size) {
        payload.widget_size = action.data.widget_size;
      }

      if (action.data.widget_variant) {
        payload.widget_variant = action.data.widget_variant;
      }

      Glean.newtab.widgetsEnabled.record(payload);
      this.recordEnabledWidgets();
    }
  }

  recordEnabledWidgets() {
    const state = this.store?.getState();
    const prefs = state?.Prefs.values;
    if (!prefs) {
      return;
    }
    const widgetsEnabled = prefs["widgets.enabled"];
    Glean.newtab.widgetsEnabledList.set(
      getWidgets(WIDGET_REGISTRY, state.ExtensionWidgets)
        .filter(w => isWidgetEnabled(w, prefs, widgetsEnabled))
        .map(w => w.telemetryName)
    );
    // Enablement decides which pinned widgets Home actually shows.
    this.recordHomeWidgets();
  }

  // Left unset where users don't pin widgets to Home, so an empty list always
  // means nothing is pinned.
  recordHomeWidgets() {
    const prefs = this.store?.getState()?.Prefs.values;
    if (!prefs || !isWidgetPlacementActive(prefs)) {
      return;
    }
    const pinned = resolvePinnedWidgetIds(prefs);
    Glean.newtab.widgetsHomeList.set(
      WIDGET_REGISTRY.filter(w => pinned.includes(w.id)).map(
        w => w.telemetryName
      )
    );
  }

  // Read from the store rather than NEWTAB_PING_PREFS, since the variant can
  // also come from a train-hop config, which is not a pref.
  recordPageLayoutVariant() {
    const prefs = this.store?.getState()?.Prefs.values;
    if (!prefs) {
      return;
    }
    Glean.newtab.pageLayoutVariant.set(resolvePageLayoutVariant(prefs));
  }

  handleWidgetsHideAll(action) {
    const { targets, widget_size } = action.data;
    this.handleUnifiedWidgetContainerAction({
      ...action,
      data: { action_type: "hide_all", widget_size },
    });
    for (const target of targets) {
      if (target.active) {
        this.handleUnifiedWidgetEnabled({
          ...action,
          data: {
            widget_name: target.telemetryName,
            widget_source: "widget",
            enabled: false,
            widget_size,
          },
        });
      }
    }
  }

  handleUnifiedWidgetError(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const payload = {
        newtab_visit_id: session.session_id,
        widget_name: action.data.widget_name,
        error_type: action.data.error_type,
      };

      if (action.data.widget_size) {
        payload.widget_size = action.data.widget_size;
      }

      if (action.data.widget_variant) {
        payload.widget_variant = action.data.widget_variant;
      }

      Glean.newtab.widgetsError.record(payload);
    }
  }

  get allowedEndpoints() {
    return (
      this._prefs
        .get(PREF_ENDPOINTS)
        .split(",")
        .map(item => item.trim())
        .filter(item => item) || []
    );
  }

  async handleReportAdUserEvent(action) {
    const { placement_id, position, report_reason, reporting_url } =
      action.data || {};

    if (!reporting_url) {
      throw new Error(
        `[Unified ads callback] Missing argument (No url). Cannot send telemetry event.`
      );
    }

    if (!report_reason) {
      throw new Error(
        `[Unified ads callback] Missing argument (No report reason). Cannot send telemetry event.`
      );
    }

    const allowed = this.allowedEndpoints;
    if (!allowed.some(prefix => reporting_url.startsWith(prefix))) {
      throw new Error(
        `[Unified ads callback] Not one of allowed prefixes (${allowed})`
      );
    }

    try {
      if (this._adsClient) {
        // js-style enum string (eg. "seen_too_many_times") must be converted to
        // rust-style enum name (eg. "SEEN_TOO_MANY_TIMES") and then converted to
        // enum value (eg. 2) by lookup in uniffi bindings
        const reportReasonValue =
          lazy.MozAdsReportReason[report_reason.toUpperCase()];
        if (reportReasonValue === undefined) {
          throw new Error(
            `[Unified ads callback] Invalid argument (Invalid report reason). Cannot send telemetry event.`
          );
        }

        const options = lazy.AdsClient.callbackOptions();
        await this._adsClient.reportAd(
          reporting_url,
          reportReasonValue,
          options
        );
      } else {
        const url = new URL(reporting_url);
        url.searchParams.append("placement_id", placement_id);
        url.searchParams.append("position", position);
        url.searchParams.append("reason", report_reason);
        const adResponse = url.toString();

        await fetch(adResponse);
      }
    } catch (error) {
      console.error("Error:", error);
    }
  }

  handleReportContentUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    const {
      card_type,
      corpus_item_id,
      report_reason,
      scheduled_corpus_item_id,
      section_position,
      section,
      title,
      topic,
      url,
    } = action.data || {};

    if (session) {
      switch (action.type) {
        case "REPORT_CONTENT_OPEN": {
          if (!this.privatePingEnabled) {
            return;
          }

          const gleanData = {
            corpus_item_id,
            scheduled_corpus_item_id,
          };

          Glean.newtabContent.reportContentOpen.record(gleanData);

          break;
        }
        case "REPORT_CONTENT_SUBMIT": {
          const gleanData = {
            card_type,
            corpus_item_id,
            report_reason,
            scheduled_corpus_item_id,
            section_position,
            section,
            title,
            topic,
            url,
          };

          if (this.privatePingEnabled) {
            Glean.newtabContent.reportContentSubmit.record(gleanData);
          }
          break;
        }
      }
    }
  }

  handleCarouselUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }

    const { section, section_position, direction, slide_index, paused } =
      action.data;
    const gleanData = {
      newtab_visit_id: session.session_id,
      section,
      section_position,
    };

    switch (action.type) {
      case "CAROUSEL_NAVIGATE":
        Glean.newtab.carouselNavigate.record({
          ...gleanData,
          direction,
          slide_index,
        });
        break;
      case "CAROUSEL_TOGGLE_AUTOPLAY":
        Glean.newtab.carouselToggleAutoplay.record({ ...gleanData, paused });
        break;
    }
  }

  handleCardSectionUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const {
        section,
        section_position,
        event_source,
        is_section_followed,
        layout_name,
      } = action.data;
      const gleanDataForPrivatePing = {
        section,
        section_position,
        event_source,
      };

      const gleanDataForNewtabPing = {
        ...gleanDataForPrivatePing,
        newtab_visit_id: session.session_id,
      };

      switch (action.type) {
        case "BLOCK_SECTION":
          Glean.newtab.sectionsBlockSection.record(
            this.redactNewTabPing(gleanDataForNewtabPing)
          );
          if (this.privatePingEnabled) {
            this.newtabContentPing.recordEvent(
              "sectionsBlockSection",
              gleanDataForPrivatePing
            );
          }
          break;
        case "UNBLOCK_SECTION":
          Glean.newtab.sectionsUnblockSection.record(
            this.redactNewTabPing(gleanDataForNewtabPing)
          );
          if (this.privatePingEnabled) {
            this.newtabContentPing.recordEvent(
              "sectionsUnblockSection",
              gleanDataForPrivatePing
            );
          }
          break;
        case "CARD_SECTION_IMPRESSION":
          {
            const gleanData = {
              newtab_visit_id: session.session_id,
              section,
              section_position,
              ...(this.sectionsPersonalizationEnabled
                ? { is_section_followed: !!is_section_followed }
                : {}),
              layout_name,
            };
            const eventData = {
              section,
              section_position,
              ...(this.sectionsPersonalizationEnabled
                ? { is_section_followed: !!is_section_followed }
                : {}),
            };
            this.recordOrQueueEvent(
              "sectionsImpression",
              eventData,
              session.session_id,
              () => {
                Glean.newtab.sectionsImpression.record(
                  this.redactNewTabPing(gleanData)
                );
              }
            );
          }
          break;
        case "FOLLOW_SECTION": {
          Glean.newtab.sectionsFollowSection.record(
            this.redactNewTabPing(gleanDataForNewtabPing)
          );
          if (this.privatePingEnabled) {
            this.newtabContentPing.recordEvent(
              "sectionsFollowSection",
              gleanDataForPrivatePing
            );
          }
          break;
        }
        case "UNFOLLOW_SECTION":
          Glean.newtab.sectionsUnfollowSection.record(
            this.redactNewTabPing(gleanDataForNewtabPing)
          );
          if (this.privatePingEnabled) {
            this.newtabContentPing.recordEvent(
              "sectionsUnfollowSection",
              gleanDataForPrivatePing
            );
          }
          break;
        case "CLICK_SECTION_LEARN_MORE":
          Glean.newtab.sectionsLearnMore.record({
            newtab_visit_id: session.session_id,
          });
          break;
        default:
          break;
      }
    }
  }

  handleInlineSelectionUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      switch (action.type) {
        case "INLINE_SELECTION_CLICK": {
          const { topic, section_position, topic_position, is_followed } =
            action.data;
          Glean.newtab.inlineSelectionClick.record({
            newtab_visit_id: session.session_id,
            topic,
            section_position,
            topic_position,
            is_followed,
          });
          break;
        }
        case "INLINE_SELECTION_IMPRESSION":
          Glean.newtab.inlineSelectionImpression.record({
            newtab_visit_id: session.session_id,
            section_position: action.data.section_position,
          });
          break;
      }
    }
  }

  handleTopicNavigationUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }

    const { topic, event_source } = action.data;
    Glean.newtab.topicNavigationClick.record({
      newtab_visit_id: session.session_id,
      topic,
      event_source,
    });
  }

  handleTopicSelectionUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      switch (action.type) {
        case "TOPIC_SELECTION_USER_OPEN":
          Glean.newtab.topicSelectionOpen.record({
            newtab_visit_id: session.session_id,
          });
          break;
        case "TOPIC_SELECTION_USER_DISMISS":
          Glean.newtab.topicSelectionDismiss.record({
            newtab_visit_id: session.session_id,
          });
          break;
        case "TOPIC_SELECTION_USER_SAVE":
          Glean.newtab.topicSelectionTopicsSaved.record({
            newtab_visit_id: session.session_id,
            topics: action.data.topics,
            previous_topics: action.data.previous_topics,
            first_save: action.data.first_save,
          });
          break;
        default:
          break;
      }
    }
  }

  handleSetPref(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }
    switch (action.data.name) {
      case "topSitesRows":
        Glean.topsites.changeDisplay.record({
          newtab_visit_id: session.session_id,
          rows: action.data.value,
        });
        break;
      case "weather.display":
        Glean.newtab.weatherChangeDisplay.record({
          newtab_visit_id: session.session_id,
          weather_display_mode: action.data.value,
        });
        break;
      case "widgets.lists.enabled":
        Glean.newtab.widgetsListsChangeDisplay.record({
          newtab_visit_id: session.session_id,
          display_status: action.data.value,
        });
        this.recordEnabledWidgets();
        break;
      case "widgets.focusTimer.enabled":
        Glean.newtab.widgetsTimerChangeDisplay.record({
          newtab_visit_id: session.session_id,
          display_status: action.data.value,
        });
        this.recordEnabledWidgets();
        break;
    }
  }

  handleWeatherUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));

    if (!session) {
      return;
    }

    // Weather specific telemtry events can be added and parsed here.
    switch (action.type) {
      case "WEATHER_IMPRESSION":
        Glean.newtab.weatherImpression.record({
          newtab_visit_id: session.session_id,
        });
        break;
      case "WEATHER_LOAD_ERROR":
        Glean.newtab.weatherLoadError.record({
          newtab_visit_id: session.session_id,
        });
        break;
      case "WEATHER_OPEN_PROVIDER_URL":
        Glean.newtab.weatherOpenProviderUrl.record({
          newtab_visit_id: session.session_id,
        });
        break;
      case "WEATHER_LOCATION_DATA_UPDATE":
        Glean.newtab.weatherLocationSelected.record({
          newtab_visit_id: session.session_id,
        });
        break;
      case "WEATHER_OPT_IN_PROMPT_SELECTION":
        Glean.newtab.weatherOptInSelection.record({
          newtab_visit_id: session.session_id,
          user_selection: action.data,
        });
        break;
      default:
        break;
    }
  }

  handleWallpaperUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));

    if (!session) {
      return;
    }

    const { data = {} } = action;

    // Wallpaper specific telemetry events can be added and parsed here.
    switch (action.type) {
      // Both of these come from the parent once the work actually happened,
      // so neither is recorded for an operation the parent refused.
      case "WALLPAPER_SAVED_REMOVED":
        Glean.newtab.wallpaperSavedRemove.record({
          newtab_visit_id: session.session_id,
          saved_wallpaper_count: data.saved_wallpaper_count,
          was_applied: data.was_applied,
          wallpaper_source: data.wallpaper_source,
        });
        break;
      case "WALLPAPER_SAVED_ADDED":
        Glean.newtab.wallpaperSavedAdd.record({
          newtab_visit_id: session.session_id,
          wallpaper_source: data.wallpaper_source,
          saved_wallpaper_count: data.saved_wallpaper_count,
        });
        break;
      case "WALLPAPER_SAVED_APPLIED":
        // wallpaperClick reports every saved image as "custom", so this is
        // what tells them apart without recording the image's name. The picker
        // still fires wallpaperClick for the same pick, so the two describe one
        // selection and must not be added together.
        Glean.newtab.wallpaperSavedClick.record({
          newtab_visit_id: session.session_id,
          saved_wallpaper_count: data.saved_wallpaper_count,
          wallpaper_source: data.wallpaper_source,
        });
        break;
      case "WALLPAPER_CATEGORY_CLICK":
        Glean.newtab.wallpaperCategoryClick.record({
          newtab_visit_id: session.session_id,
          selected_category: action.data,
        });
        break;
      case "WALLPAPER_CLICK":
        {
          const {
            selected_wallpaper,
            had_previous_wallpaper,
            had_uploaded_previously,
          } = data;

          // if either of the wallpaper prefs are truthy, they had a previous wallpaper
          Glean.newtab.wallpaperClick.record({
            newtab_visit_id: session.session_id,
            selected_wallpaper,
            had_previous_wallpaper,
            had_uploaded_previously,
          });
        }
        break;
      case "WALLPAPERS_FEATURE_HIGHLIGHT_CTA_CLICKED":
        Glean.newtab.wallpaperHighlightCtaClick.record({
          newtab_visit_id: session.session_id,
        });
        break;
      case "WALLPAPERS_FEATURE_HIGHLIGHT_DISMISSED":
        Glean.newtab.wallpaperHighlightDismissed.record({
          newtab_visit_id: session.session_id,
        });
        break;
      default:
        break;
    }
  }

  handleBlockUrl(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    // TODO: Do we want to not send this unless there's a newtab_visit_id?
    if (!session) {
      return;
    }

    // Despite the action name, this is actually a bulk dismiss action:
    // it can be applied to multiple topsites simultaneously.
    const { data } = action;
    for (const datum of data) {
      const { corpus_item_id, scheduled_corpus_item_id } = datum;

      if (datum.is_pocket_card) {
        const gleanData = {
          is_sponsored: datum.card_type === "spoc",
          ...(datum.format ? { format: datum.format } : {}),
          position: datum.position,
          ...(datum.section
            ? {
                section: datum.section,
                section_position: datum.section_position,
                ...(this.sectionsPersonalizationEnabled
                  ? { is_section_followed: !!datum.is_section_followed }
                  : {}),
              }
            : {}),
          // We conditionally add in a few props.
          ...(corpus_item_id ? { corpus_item_id } : {}),
          ...(scheduled_corpus_item_id ? { scheduled_corpus_item_id } : {}),
          ...(corpus_item_id || scheduled_corpus_item_id
            ? {
                received_rank: datum.received_rank,
                recommended_at: datum.recommended_at,
              }
            : {}),
        };

        if (this.trainhopClickOnlyEnabled) {
          this.transitionToPrivateSession();
        }
        this.recordOrQueueEvent(
          "dismiss",
          gleanData,
          session.session_id,
          () => {
            Glean.pocket.dismiss.record({
              ...this.redactNewTabPing(gleanData, gleanData.is_sponsored),
              newtab_visit_id: session.session_id,
            });
          }
        );
        continue;
      }
      // Only log a topsites.dismiss telemetry event if the action came from TopSites section
      if (action.source === "TOP_SITES") {
        const { position, advertiser_name, isSponsoredTopSite } = datum;
        if (this.isPlacesInfluenced() && isSponsoredTopSite) {
          this.recordOrQueueEvent(
            "topSitesDismiss",
            {
              advertiser_name,
              is_sponsored: !!isSponsoredTopSite,
              position,
            },
            session.session_id
          );
        } else {
          const gleanData = {
            advertiser_name,
            newtab_visit_id: session.session_id,
            is_sponsored: !!isSponsoredTopSite,
            position,
          };
          Glean.topsites.dismiss.record(gleanData);
        }
      }
    }
  }

  handleAboutSponsoredTopSites(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    const { data } = action;
    const { position, advertiser_name } = data;

    if (session) {
      if (this.isPlacesInfluenced()) {
        if (this.privatePingEnabled) {
          this.newtabContentPing.recordEvent("topSitesShowPrivacyClick", {
            advertiser_name,
            position,
          });
        }
      } else {
        const gleanData = {
          advertiser_name,
          newtab_visit_id: session.session_id,
          position,
        };
        Glean.topsites.showPrivacyClick.record(gleanData);
      }
    }
  }

  /**
   * Handle impression stats actions from Discovery Stream.
   *
   * @param {string} port  The session port with which this is associated
   * @param {object} data  The impression data structured as {source: "SOURCE", tiles: [{id: 123}]}
   */
  handleDiscoveryStreamImpressionStats(port, data) {
    let session = this.sessions.get(port);

    if (!session) {
      throw new Error("Session does not exist.");
    }

    const { tiles } = data;

    tiles.forEach(tile => {
      const { corpus_item_id, scheduled_corpus_item_id } = tile;
      const is_sponsored = tile.type === "spoc";
      const gleanData = {
        is_sponsored,
        ...(tile.format ? { format: tile.format } : {}),
        ...(tile.card_column ? { card_column: tile.card_column } : {}),
        ...(tile.is_ad_eligible_position
          ? { is_ad_eligible_position: true }
          : {}),
        ...(tile.section
          ? {
              section: tile.section,
              section_position: tile.section_position,
              ...(this.sectionsPersonalizationEnabled
                ? { is_section_followed: !!tile.is_section_followed }
                : {}),
              layout_name: tile.layout_name,
            }
          : {}),
        position: tile.pos,
        topic: tile.topic,
        variant_id: tile.variant_id,
        source_section_id: tile.source_section_id ?? tile.section,
        selected_topics: tile.selectedTopics,
        is_list_card: tile.is_list_card,
        // We conditionally add in a few props.
        ...(corpus_item_id ? { corpus_item_id } : {}),
        ...(scheduled_corpus_item_id ? { scheduled_corpus_item_id } : {}),
        ...(corpus_item_id || scheduled_corpus_item_id
          ? {
              received_rank: tile.received_rank,
              recommended_at: tile.recommended_at,
            }
          : {}),
      };
      this.recordOrQueueEvent(
        "impression",
        gleanData,
        session.session_id,
        () => {
          Glean.pocket.impression.record({
            ...this.redactNewTabPing(gleanData, is_sponsored),
            newtab_visit_id: session.session_id,
          });
        }
      );

      if (tile.shim) {
        if (this.canSendUnifiedAdsSpocCallbacks) {
          if (this._adsClient) {
            // Send callback event via MAC
            this.sendMacCallbackEvent(tile.shim, "impression");
          } else {
            // Send unified ads callback event
            this.sendUnifiedAdsCallbackEvent({
              url: tile.shim,
              position: tile.pos,
            });
          }
        }
      }
    });
  }

  /**
   * Take all enumerable members of the data object and merge them into
   * the session.perf object for the given port, so that it is sent to the
   * server when the session ends.  All members of the data object should
   * be valid values of the perf object, as defined in pings.js and the
   * data*.md documentation.
   *
   * Note: Any existing keys with the same names already in the
   * session perf object will be overwritten by values passed in here.
   *
   * @param {string} port  The session with which this is associated
   * @param {object} data  The perf data to be
   */
  saveSessionPerfData(port, data) {
    // XXX should use try/catch and send a bad state indicator if this
    // get blows up.
    let session = this.sessions.get(port);

    // XXX Partial workaround for #3118; avoids the worst incorrect associations
    // of times with browsers, by associating the load trigger with the
    // visibility event as the user is most likely associating the trigger to
    // the tab just shown. This helps avoid associating with a preloaded
    // browser as those don't get the event until shown. Better fix for more
    // cases forthcoming.
    //
    // XXX the about:home check (and the corresponding test) should go away
    // once the load_trigger stuff in addSession is refactored into
    // setLoadTriggerInfo.
    //
    if (data.visibility_event_rcvd_ts && session.page !== "about:home") {
      this.setLoadTriggerInfo(port);
    }

    let timestamp = data.topsites_first_painted_ts;

    if (
      timestamp &&
      session.page === "about:home" &&
      !lazy.HomePage.overridden &&
      Services.prefs.getIntPref("browser.startup.page") === 1
    ) {
      lazy.AboutNewTab.maybeRecordTopsitesPainted(timestamp);
    }

    Object.assign(session.perf, data);

    if (data.visibility_event_rcvd_ts && !session.newtabOpened) {
      session.newtabOpened = true;
      this.#startDwellClockIfActive(session);
      const source = ONBOARDING_ALLOWED_PAGE_VALUES.includes(session.page)
        ? session.page
        : "other";
      Glean.newtab.opened.record({
        newtab_visit_id: session.session_id,
        source,
        window_inner_height: data.window_inner_height,
        window_inner_width: data.window_inner_width,
      });
    }
  }

  /**
   * Records a click for a tile that Firefox opened itself in a new tab or
   * window, e.g. from its context menu. The tile is resolved from state by its
   * url and routed through the ordinary click handlers.
   *
   * @param {object} subject Subject of the navigation target notification.
   * @param {MozBrowser} subject.sourceTabBrowser Browser the link was opened from.
   * @param {MozBrowser} subject.createdTabBrowser Browser the link opened in.
   * @param {string} subject.url Url of the opened link.
   */
  handleCreatedNavigationTarget({ sourceTabBrowser, createdTabBrowser, url }) {
    const { loadedTabs } = lazy.AboutNewTabParent;
    const gBrowser = createdTabBrowser?.documentGlobal?.gBrowser;
    // For a tab opened in the foreground, sourceTabBrowser is read after the
    // new tab is selected, so it is the new tab rather than newtab.
    const source = loadedTabs.has(sourceTabBrowser)
      ? sourceTabBrowser
      : gBrowser?.getTabForBrowser(createdTabBrowser)?.openerTab?.linkedBrowser;
    const portID = source && loadedTabs.get(source)?.portID;
    if (!portID || !this.sessions.has(portID)) {
      return;
    }
    const meta = { fromTarget: portID };
    const background = gBrowser?.selectedBrowser !== createdTabBrowser;

    const state = this.store.getState();
    const rows = state?.TopSites?.rows ?? [];
    const position = rows.findIndex(row => row?.url === url);
    if (position !== -1) {
      const row = rows[position];
      const sponsored = !!(
        row.sponsored_position ||
        row.sponsored_tile_id ||
        row.type === "SPOC"
      );
      this.#trackBrowserOpen(
        createdTabBrowser,
        sponsored ? "topsite_sponsored" : "topsite_organic",
        portID
      );
      if (sponsored) {
        this.recordSponsoredTopSiteClick(row, {
          position,
          event_source: BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK_SOURCE,
          background,
          meta,
        });
        return;
      }
      this.handleTopSitesOrganicImpressionStats({
        data: {
          type: "click",
          position,
          isPinned: row.isPinned,
          smart_scores: row.scores,
          smart_weights: row.weights,
          event_source: BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK_SOURCE,
          background,
        },
        meta,
      });
      return;
    }

    const intent = this.#pendingLinkOpens.get(portID);
    if (intent?.url !== url) {
      return;
    }
    this.#pendingLinkOpens.delete(portID);

    // The shim is dropped so that no ad callback is sent for sponsored stories.
    // eslint-disable-next-line no-unused-vars
    const { shim, ...value } = intent.userEvent.value ?? {};
    const dwellId = this.#trackBrowserOpen(
      createdTabBrowser,
      value.card_type === "spoc" ? "story_sponsored" : "story_organic",
      portID
    );
    this.handleDiscoveryStreamUserEvent({
      data: {
        ...intent.userEvent,
        value: {
          ...value,
          event_source: BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK_SOURCE,
          background,
          ...(dwellId ? { dwell_id: dwellId } : {}),
        },
      },
      meta,
    });
  }

  /**
   * Start measuring the active time on a page Firefox opened from the newtab
   * itself, the way PlacesFeed does for the links newtab opens.
   *
   * @param {MozBrowser} browser Browser the link opened in.
   * @param {string} dwellLabel One of DWELL_LABELS.
   * @param {string} portID The newtab the link was opened from.
   * @returns {string|undefined} The dwell id that pairs the page with its click
   *   event, or undefined if the page is not measured.
   */
  #trackBrowserOpen(browser, dwellLabel, portID) {
    if (!browser || browser.browsingContext?.usePrivateBrowsing) {
      return undefined;
    }
    const dwellId = dwellLabel.startsWith("story_")
      ? `browser-${++this.#nextBrowserOpenDwellId}`
      : undefined;
    this.handleDwellLinkOpened({
      data: {
        browser,
        dwell_label: dwellLabel,
        dwell_id: dwellId,
        port_id: portID,
      },
    });
    return dwellId;
  }

  /**
   * Holds the click payload a story sends when it is right-clicked or
   * middle-clicked, so that it can be recorded if Firefox then opens the link.
   *
   * @param {object} action BROWSER_LINK_OPEN_INTENT action from newtab.
   */
  handleBrowserLinkOpenIntent(action) {
    const portID = au.getPortIdOfSender(action);
    const { url, userEvent } = action.data ?? {};
    if (!portID || !url || !userEvent) {
      return;
    }
    this.#pendingLinkOpens.set(portID, { url, userEvent });
  }

  /**
   * Records a click on a sponsored top site on newtab whose link was opened by
   * Firefox's right-click menu or a middle-click, or newtab's private window
   * option.
   * No reporting url is passed, so no ad callback is sent.
   *
   * @param {object} row Top site row from state.
   * @param {object} options Position, event_source, background and action meta.
   */
  recordSponsoredTopSiteClick(
    row,
    { position, event_source, background, meta }
  ) {
    this.handleTopSitesSponsoredImpressionStats({
      data: {
        type: "click",
        position,
        source: "newtab",
        advertiser_name: (
          row?.label ||
          row?.hostname ||
          ""
        ).toLocaleLowerCase(),
        frecency_boosted: row?.type === "frecency-boost",
        event_source,
        background,
      },
      meta,
    });
  }

  _beginObservingNewtabPingPrefs() {
    Services.prefs.addObserver(ACTIVITY_STREAM_PREF_BRANCH, this);

    for (const pref of Object.keys(NEWTAB_PING_PREFS)) {
      const fullPrefName = ACTIVITY_STREAM_PREF_BRANCH + pref;
      this._setNewtabPrefMetrics(fullPrefName, false);
    }

    Services.prefs.addObserver(TOP_SITES_BLOCKED_SPONSORS_PREF, this);
    this._setBlockedSponsorsMetrics();

    Services.prefs.addObserver(TOPIC_SELECTION_SELECTED_TOPICS_PREF, this);
    this._setTopicSelectionSelectedTopicsMetrics();
  }

  _stopObservingNewtabPingPrefs() {
    Services.prefs.removeObserver(ACTIVITY_STREAM_PREF_BRANCH, this);
    Services.prefs.removeObserver(TOP_SITES_BLOCKED_SPONSORS_PREF, this);
    Services.prefs.removeObserver(TOPIC_SELECTION_SELECTED_TOPICS_PREF, this);
  }

  observe(subject, topic, data) {
    // Before the pref branch below, which switches on `data` and would read
    // these topics' null data as a pref name.
    switch (topic) {
      case USER_INTERACTION_ACTIVE:
        this.#onUserInteractionActive();
        return;
      case USER_INTERACTION_INACTIVE:
        this.#onUserInteractionInactive();
        return;
      case CREATED_NAVIGATION_TARGET_TOPIC:
        this.handleCreatedNavigationTarget(subject.wrappedJSObject ?? {});
        return;
    }

    if (data === TOP_SITES_BLOCKED_SPONSORS_PREF) {
      this._setBlockedSponsorsMetrics();
    } else if (data === TOPIC_SELECTION_SELECTED_TOPICS_PREF) {
      this._setTopicSelectionSelectedTopicsMetrics();
    } else {
      this._setNewtabPrefMetrics(data, true);
    }
  }

  async _setNewtabPrefMetrics(fullPrefName, isChanged) {
    const pref = fullPrefName.slice(ACTIVITY_STREAM_PREF_BRANCH.length);
    if (!Object.hasOwn(NEWTAB_PING_PREFS, pref)) {
      return;
    }
    const metric = NEWTAB_PING_PREFS[pref];
    switch (Services.prefs.getPrefType(fullPrefName)) {
      case Services.prefs.PREF_BOOL:
        metric.set(Services.prefs.getBoolPref(fullPrefName));
        break;

      case Services.prefs.PREF_INT:
        metric.set(Services.prefs.getIntPref(fullPrefName));
        break;
    }
    if (isChanged) {
      switch (fullPrefName) {
        case `${ACTIVITY_STREAM_PREF_BRANCH}feeds.topsites`:
        case `${ACTIVITY_STREAM_PREF_BRANCH}${PREF_SHOW_SPONSORED_TOPSITES}`:
          Glean.topsites.prefChanged.record({
            pref_name: fullPrefName,
            new_value: Services.prefs.getBoolPref(fullPrefName),
          });
          break;
      }
    }
  }

  _setBlockedSponsorsMetrics() {
    let blocklist;
    try {
      blocklist = JSON.parse(
        Services.prefs.getStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF, "[]")
      );
    } catch (e) {}
    if (blocklist) {
      Glean.newtab.blockedSponsors.set(blocklist);
    }
  }

  _setTopicSelectionSelectedTopicsMetrics() {
    let topiclist;
    try {
      topiclist = Services.prefs.getStringPref(
        TOPIC_SELECTION_SELECTED_TOPICS_PREF,
        ""
      );
    } catch (e) {}
    if (topiclist) {
      // Note: Beacuse Glean is expecting a string list, the
      // value of the pref needs to be converted to an array
      topiclist = topiclist.split(",").map(s => s.trim());
      Glean.newtab.selectedTopics.set(topiclist);
    }
  }

  uninit() {
    this._stopObservingNewtabPingPrefs();
    // Record what the user is reading at shutdown. The sample waits,
    // ping-lifetime, for the next metrics ping.
    for (const page of [...this.#openedPages.values()]) {
      this.#finalizeOpenedPage(page);
    }
    // Must run before newtabContentPing.uninit(), which discards its own buffer.
    this.#flushBufferedEventsOnUninit();
    this.newtabContentPing.uninit();
    if (this._initialized) {
      Services.obs.removeObserver(
        this.browserOpenNewtabStart,
        "browser-open-newtab-start"
      );
      Services.obs.removeObserver(this, USER_INTERACTION_ACTIVE);
      Services.obs.removeObserver(this, USER_INTERACTION_INACTIVE);
      Services.obs.removeObserver(this, CREATED_NAVIGATION_TARGET_TOPIC);
      this.#userActive = false;
      this.#lastActiveAt = null;
      this._initialized = false;
    }

    // TODO: The sessions still in this.sessions are not reported as ended;
    // only their buffered events are flushed above. They also drop whatever
    // dwell time they accrued, which biases newtab.dwell_time low. The newtab
    // in front of the user at shutdown is the one most likely to have some.
  }
}
