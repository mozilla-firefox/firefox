// We're using console.error() to debug, so we'll be keeping this rule handy
/* eslint no-console: ["error", { allow: ["error"] }] */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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
  WIDGET_REGISTRY,
  isWidgetEnabled,
} from "resource://newtab/common/WidgetsRegistry.mjs";
import { resolvePageLayoutVariant } from "resource://newtab/common/PageLayoutVariants.mjs";
import { Prefs } from "resource://newtab/lib/ActivityStreamPrefs.sys.mjs";
import { classifySite } from "resource://newtab/lib/SiteClassifier.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
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
const PREF_NEWTAB_PING_ENABLED = "browser.newtabpage.ping.enabled";
const PREF_USER_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.user.enabled";
const PREF_SYSTEM_INFERRED_PERSONALIZATION =
  "discoverystream.sections.personalization.inferred.enabled";
const PREF_SECTIONS_PERSONALIZATION_ENABLED =
  "discoverystream.sections.personalization.enabled";
const PREF_SOV_FRECENCY_EXPOSURE = "sov.frecency.exposure";

const TOP_STORIES_SECTION_NAME = "top_stories_section";

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
export class TelemetryFeed {
  /**
   * Queue for telemetry events when in NormalGleanSession mode.
   * Events are stored here and cleared at session end based on session type.
   */
  #eventBuffer = [];

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

  get tileIdRedactedForSponsored() {
    return (
      this.store?.getState()?.Prefs.values?.trainhopConfig?.newtabPing
        ?.redactTileIdForSponsored || false
    );
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
      this.sovEnabled()
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
   * @param {Function} callback - Function to record to non-private newtab ping
   *  Called immediately if in PrivateGleanSession, otherwise its added to eventBuffer
   *  and called when the eventBuffer is cleared. The return value is ignored.
   */
  recordOrQueueEvent(eventName, eventData, sessionId, callback) {
    if (this.gleanSessionType === GleanSessionType.NormalGleanSession) {
      this.#eventBuffer.push({ eventName, eventData, callback, sessionId });
    } else {
      callback?.();
      if (this.privatePingEnabled) {
        this.newtabContentPing.recordEvent(eventName, eventData);
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
        const aDate = a[1].followedAt ? new Date(a[1].followedAt) : 0;
        const bDate = b[1].followedAt ? new Date(b[1].followedAt) : 0;
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
   * @param {boolean} isSponsored Is this in ad, in which case there is nothing we can redact currently
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
        // eslint-disable-next-line no-unused-vars
        tile_id,
        // eslint-disable-next-line no-unused-vars
        topic,
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
      // eslint-disable-next-line no-unused-vars
      topic,
      ...result
    } = pingDict;

    // For spocs we need to retain the tile id, unless we're configured to
    // redact it.
    if (this.tileIdRedactedForSponsored) {
      delete result.tile_id;
    }

    result.content_redacted = true;
    return result;
  }

  /**
   * addSession - Start tracking a new session
   *
   * @param  {string} id the portID of the open session
   * @param  {string} the URL being loaded for this session (optional)
   * @return {obj}    Session object
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
      perf: {
        load_trigger_type,
        is_preloaded: false,
      },
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
      return;
    }

    Glean.newtab.closed.record({ newtab_visit_id: session.session_id });
    if (
      this.telemetryEnabled &&
      Services.prefs.getBoolPref(PREF_NEWTAB_PING_ENABLED, true)
    ) {
      // clear event buffer based on session type
      const recordToContentPing =
        this.gleanSessionType === GleanSessionType.PrivateGleanSession;
      this.#clearEventBuffer(recordToContentPing, session.session_id);
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
  }

  /**
   * handleNewTabInit - Handle NEW_TAB_INIT, which creates a new session and sets the a flag
   *                    for session.perf based on whether or not this new tab is preloaded
   *
   * @param  {obj} action the Action object
   */
  handleNewTabInit(action) {
    const session = this.addSession(
      au.getPortIdOfSender(action),
      action.data.url
    );
    session.perf.is_preloaded =
      action.data.browser.getAttribute("preloadedState") === "preloaded";
  }

  sovEnabled() {
    const { values } = this.store?.getState()?.Prefs || {};
    const trainhopSovEnabled = values?.trainhopConfig?.sov?.enabled;
    return trainhopSovEnabled;
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
      advertiser: advertiser_name,
      tile_id,
      visible_topsites,
      frecency_boosted = false,
    } = data;
    // Legacy telemetry expects 1-based tile positions.
    const legacyTelemetryPosition = position + 1;

    const session = this.sessions.get(au.getPortIdOfSender(action));

    if (type === "impression") {
      Glean.contextualServicesTopsites.impression[
        `${source}_${legacyTelemetryPosition}`
      ].add(1);
      if (session) {
        if (this.sovEnabled()) {
          const eventData = {
            advertiser_name,
            tile_id,
            is_sponsored: true,
            position,
            visible_topsites,
            frecency_boosted,
            frecency_boosted_has_exposure: this.frecencyBoostedHasExposure(),
          };
          this.recordOrQueueEvent(
            "topSitesImpression",
            eventData,
            session.session_id
          );
        } else {
          Glean.topsites.impression.record({
            advertiser_name,
            tile_id,
            newtab_visit_id: session.session_id,
            is_sponsored: true,
            position,
            visible_topsites,
          });
        }
      }
    } else if (type === "click") {
      Glean.contextualServicesTopsites.click[
        `${source}_${legacyTelemetryPosition}`
      ].add(1);
      if (session) {
        if (this.sovEnabled()) {
          const eventData = {
            advertiser_name,
            tile_id,
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
          Glean.topsites.click.record({
            advertiser_name,
            tile_id,
            newtab_visit_id: session.session_id,
            is_sponsored: true,
            position,
            visible_topsites,
          });
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
          smart_scores: JSON.stringify(action.data.smartScores),
          smart_weights: JSON.stringify(action.data.smartWeights),
        });
        break;

      case "click":
        Glean.topsites.click.record({
          newtab_visit_id: session.session_id,
          is_sponsored: false,
          position: action.data.position,
          is_pinned: !!action.data.isPinned,
          visible_topsites,
          smart_scores: JSON.stringify(action.data.smartScores),
          smart_weights: JSON.stringify(action.data.smartWeights),
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
          has_title_changed: action.data.hasTitleChanged,
          has_url_changed: action.data.hasURLChanged,
        });
        break;
      }
      case "WEATHER_DETECT_LOCATION": {
        Glean.newtab.weatherDetectLocation.record({
          newtab_visit_id: session.session_id,
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
   * @returns Same item, but another item occasionally based on probablility setting.
   * Sponsored items are unchanged
   */
  randomizeOrganicContentEvent(item) {
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
    const allRecs = this.getAllRecommendations(); // Number of recommendations has changed
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
    };
    // If we're replacing a non top stories item, then assign the appropriate
    // section to the item
    if (
      resultItem.section &&
      resultItem.section !== TOP_STORIES_SECTION_NAME &&
      randomItem.section
    ) {
      resultItem.section = randomItem.section;
      resultItem.section_position = randomItem.section_position;
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
      // TODO: Determine if private window should be tracked?
      // case "OPEN_PRIVATE_WINDOW":
      case "OPEN_NEW_WINDOW":
      case "CLICK": {
        const {
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
          tile_id,
          topic,
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
            position: action.data.action_position,
            tile_id,
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
          this.recordOrQueueEvent(
            "click",
            this.randomizeOrganicContentEvent(gleanData),
            session.session_id,
            () => {
              Glean.pocket.click.record({
                ...this.redactNewTabPing(gleanData, is_sponsored),
                newtab_visit_id: session.session_id,
              });
            }
          );
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
    privateMetrics.surfaceId = surfaceId;

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

    if (prefs?.inferredPersonalizationConfig?.normalized_time_zone_offset) {
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
        Glean.newtab.scroll.set(true);
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
      case at.WALLPAPER_CATEGORY_CLICK:
      case at.WALLPAPER_CLICK:
      case at.WALLPAPERS_FEATURE_HIGHLIGHT_DISMISSED:
      case at.WALLPAPERS_FEATURE_HIGHLIGHT_CTA_CLICKED:
      case at.WALLPAPER_UPLOAD:
        this.handleWallpaperUserEvent(action);
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
      case at.INLINE_SELECTION_CLICK:
      // Intentional fall-through
      case at.INLINE_SELECTION_IMPRESSION:
        this.handleInlineSelectionUserEvent(action);
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
        // Turning history off hides history-dependent widgets, so the enabled
        // list has to be re-read: unlike a widget's own pref, this flips
        // enablement without any widget toggle to hang the update off.
        if (action.data?.name === "recordsHistory") {
          this.recordEnabledWidgets();
        }
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

      if (action.data.action_value !== undefined) {
        payload.action_value = String(action.data.action_value);
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

      Glean.newtab.widgetsEnabled.record(payload);
      this.recordEnabledWidgets();
    }
  }

  recordEnabledWidgets() {
    const prefs = this.store?.getState()?.Prefs.values;
    if (!prefs) {
      return;
    }
    const widgetsEnabled = prefs["widgets.enabled"];
    Glean.newtab.widgetsEnabledList.set(
      WIDGET_REGISTRY.filter(w =>
        isWidgetEnabled(w, prefs, widgetsEnabled)
      ).map(w => w.telemetryName)
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
          const { topic, section_position, position, is_followed } =
            action.data;
          Glean.newtab.inlineSelectionClick.record({
            newtab_visit_id: session.session_id,
            topic,
            section_position,
            position,
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

    const { data } = action;

    // Wallpaper specific telemtry events can be added and parsed here.
    switch (action.type) {
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
          tile_id: datum.id || datum.tile_id,
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
        const { position, advertiser_name, tile_id, isSponsoredTopSite } =
          datum;
        if (this.sovEnabled() && isSponsoredTopSite) {
          this.recordOrQueueEvent(
            "topSitesDismiss",
            {
              advertiser_name,
              tile_id,
              is_sponsored: !!isSponsoredTopSite,
              position,
            },
            session.session_id
          );
        } else {
          Glean.topsites.dismiss.record({
            advertiser_name,
            tile_id,
            newtab_visit_id: session.session_id,
            is_sponsored: !!isSponsoredTopSite,
            position,
          });
        }
      }
    }
  }

  handleAboutSponsoredTopSites(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    const { data } = action;
    const { position, advertiser_name, tile_id } = data;

    if (session) {
      if (this.sovEnabled()) {
        if (this.privatePingEnabled) {
          this.newtabContentPing.recordEvent("topSitesShowPrivacyClick", {
            advertiser_name,
            tile_id,
            position,
          });
        }
      } else {
        Glean.topsites.showPrivacyClick.record({
          advertiser_name,
          tile_id,
          newtab_visit_id: session.session_id,
          position,
        });
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
        tile_id: tile.id,
        topic: tile.topic,
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
    // Must run before newtabContentPing.uninit(), which discards its own buffer.
    this.#flushBufferedEventsOnUninit();
    this.newtabContentPing.uninit();
    if (this._initialized) {
      Services.obs.removeObserver(
        this.browserOpenNewtabStart,
        "browser-open-newtab-start"
      );
      this._initialized = false;
    }

    // TODO: The sessions still in this.sessions are not reported as ended;
    // only their buffered events are flushed above.
  }
}
