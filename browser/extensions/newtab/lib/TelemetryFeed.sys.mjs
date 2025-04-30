// We're using console.error() to debug, so we'll be keeping this rule handy
/* eslint no-console: ["error", { allow: ["error"] }] */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that the Karma test
// environment won't choke on these module. This is because the Karma test
// environment already stubs out XPCOMUtils, AppConstants and RemoteSettings,
// and overrides importESModule to be a no-op (which can't be done for a static
// import statement).

// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

import {
  actionTypes as at,
  actionUtils as au,
} from "resource://newtab/common/Actions.mjs";
import { Prefs } from "resource://newtab/lib/ActivityStreamPrefs.sys.mjs";
import { classifySite } from "resource://newtab/lib/SiteClassifier.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  ClientEnvironmentBase:
    "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ExperimentManager: "resource://nimbus/lib/ExperimentManager.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  TelemetryEnvironment: "resource://gre/modules/TelemetryEnvironment.sys.mjs",
  UTEventReporting: "resource://newtab/lib/UTEventReporting.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  pktApi: "chrome://pocket/content/pktApi.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "handoffToAwesomebarPrefValue",
  "browser.newtabpage.activity-stream.improvesearch.handoffToAwesomebar",
  false,
  (preference, previousValue, new_value) =>
    Glean.newtabHandoffPreference.enabled.set(new_value)
);

export const PREF_IMPRESSION_ID = "impressionId";
export const TELEMETRY_PREF = "telemetry";
export const EVENTS_TELEMETRY_PREF = "telemetry.ut.events";
export const PREF_UNIFIED_ADS_SPOCS_ENABLED = "unifiedAds.spocs.enabled";
export const PREF_UNIFIED_ADS_TILES_ENABLED = "unifiedAds.tiles.enabled";
const PREF_ENDPOINTS = "discoverystream.endpoints";
const PREF_SHOW_SPONSORED_STORIES = "showSponsored";
const PREF_SHOW_SPONSORED_TOPSITES = "showSponsoredTopSites";
const BLANK_HOMEPAGE_URL = "chrome://browser/content/blanktab.html";
const PREF_PRIVATE_PING_ENABLED = "telemetry.privatePing.enabled";
const PREF_FOLLOWED_SECTIONS = "discoverystream.sections.following";

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

// Used as the missing value for timestamps in the session ping
const TIMESTAMP_MISSING_VALUE = -1;

// Page filter for onboarding telemetry, any value other than these will
// be set as "other"
const ONBOARDING_ALLOWED_PAGE_VALUES = [
  "about:welcome",
  "about:home",
  "about:newtab",
];

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

const ACTIVITY_STREAM_PREF_BRANCH = "browser.newtabpage.activity-stream.";
const NEWTAB_PING_PREFS = {
  showSearch: Glean.newtabSearch.enabled,
  "feeds.topsites": Glean.topsites.enabled,
  [PREF_SHOW_SPONSORED_TOPSITES]: Glean.topsites.sponsoredEnabled,
  "feeds.section.topstories": Glean.pocket.enabled,
  [PREF_SHOW_SPONSORED_STORIES]: Glean.pocket.sponsoredStoriesEnabled,
  topSitesRows: Glean.topsites.rows,
  showWeather: Glean.newtab.weatherEnabled,
};
const TOP_SITES_BLOCKED_SPONSORS_PREF = "browser.topsites.blockedSponsors";
const TOPIC_SELECTION_SELECTED_TOPICS_PREF =
  "browser.newtabpage.activity-stream.discoverystream.topicSelection.selectedTopics";
export class TelemetryFeed {
  constructor() {
    this.sessions = new Map();
    this._prefs = new Prefs();
    this._impressionId = this.getOrCreateImpressionId();
    this._aboutHomeSeen = false;
    this._classifySite = classifySite;
    this._browserOpenNewtabStart = null;

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

  get eventTelemetryEnabled() {
    return this._prefs.get(EVENTS_TELEMETRY_PREF);
  }

  get privatePingEnabled() {
    return this._prefs.get(PREF_PRIVATE_PING_ENABLED);
  }

  get experimentManager() {
    return lazy.ExperimentManager;
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
    this._beginObservingNewtabPingPrefs();
    Services.obs.addObserver(
      this.browserOpenNewtabStart,
      "browser-open-newtab-start"
    );
    // Set two scalars for the "deletion-request" ping (See bug 1602064 and 1729474)
    Glean.deletionRequest.impressionId.set(this._impressionId);
    Glean.deletionRequest.contextId.set(lazy.contextId);
    Glean.newtab.locale.set(Services.locale.appLocaleAsBCP47);
    Glean.newtabHandoffPreference.enabled.set(
      lazy.handoffToAwesomebarPrefValue
    );
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
    let now = Cu.now();
    this._browserOpenNewtabStart = Math.round(this.processStartTs + now);

    ChromeUtils.addProfilerMarker(
      "UserTiming",
      now,
      "browser-open-newtab-start"
    );
  }

  /**
   * Retrieves most recently followed sections, ordered alphabetically. (maximum 2 sections)
   * @returns {String[]} comma separated string of section UUID's
   */
  getFollowedSections() {
    const followedString = this._prefs.get(PREF_FOLLOWED_SECTIONS);

    if (followedString.length) {
      const items = followedString
        .split(",")
        .map(item => item.trim())
        .filter(_item => _item);
      return items.slice(-2).sort();
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
   * Lazily initialize UTEventReporting to send pings
   */
  get utEvents() {
    Object.defineProperty(this, "utEvents", {
      value: new lazy.UTEventReporting(),
    });
    return this.utEvents;
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

    Glean.newtab.closed.record({ newtab_visit_id: session.session_id });
    if (
      this.telemetryEnabled &&
      (lazy.NimbusFeatures.glean.getVariable("newtabPingEnabled") ?? true)
    ) {
      GleanPings.newtab.submit("newtab_session_end");
      if (this.privatePingEnabled) {
        this.configureContentPing("newtab_session_end");
      }
    }

    if (session.perf.visibility_event_rcvd_ts) {
      let absNow = this.processStartTs + Cu.now();
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
    } else {
      // This session was never shown (i.e. the hidden preloaded newtab), there was no user session either.
      this.sessions.delete(portID);
      return;
    }

    let sessionEndEvent = this.createSessionEndEvent(session);
    this.sendUTEvent(sessionEndEvent, this.utEvents.sendSessionEndEvent);
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

  /**
   * createPing - Create a ping with common properties
   *
   * @param  {string} id The portID of the session, if a session is relevant (optional)
   * @return {obj}    A telemetry ping
   */
  createPing(portID) {
    const ping = {
      addon_version: Services.appinfo.appBuildID,
      locale: Services.locale.appLocaleAsBCP47,
      user_prefs: this.userPreferences,
    };

    // If the ping is part of a user session, add session-related info
    if (portID) {
      const session = this.sessions.get(portID) || this.addSession(portID);
      Object.assign(ping, { session_id: session.session_id });

      if (session.page) {
        Object.assign(ping, { page: session.page });
      }
    }
    return ping;
  }

  createUserEvent(action) {
    return Object.assign(
      this.createPing(au.getPortIdOfSender(action)),
      action.data,
      { action: "activity_stream_user_event" }
    );
  }

  createSessionEndEvent(session) {
    return Object.assign(this.createPing(), {
      session_id: session.session_id,
      page: session.page,
      session_duration: session.session_duration,
      action: "activity_stream_session",
      perf: session.perf,
      profile_creation_date:
        lazy.TelemetryEnvironment.currentEnvironment.profile.resetDate ||
        lazy.TelemetryEnvironment.currentEnvironment.profile.creationDate,
    });
  }

  sendUTEvent(event_object, eventFunction) {
    if (this.telemetryEnabled && this.eventTelemetryEnabled) {
      eventFunction(event_object);
    }
  }

  handleTopSitesSponsoredImpressionStats(action) {
    const { data } = action;
    const {
      type,
      position,
      source,
      advertiser: advertiser_name,
      tile_id,
    } = data;
    // Legacy telemetry expects 1-based tile positions.
    const legacyTelemetryPosition = position + 1;

    const unifiedAdsTilesEnabled = this._prefs.get(
      PREF_UNIFIED_ADS_TILES_ENABLED
    );

    let pingType;

    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (type === "impression") {
      pingType = "topsites-impression";
      Glean.contextualServicesTopsites.impression[
        `${source}_${legacyTelemetryPosition}`
      ].add(1);
      if (session) {
        Glean.topsites.impression.record({
          advertiser_name,
          tile_id,
          newtab_visit_id: session.session_id,
          is_sponsored: true,
          position,
        });
      }
    } else if (type === "click") {
      pingType = "topsites-click";
      Glean.contextualServicesTopsites.click[
        `${source}_${legacyTelemetryPosition}`
      ].add(1);
      if (session) {
        Glean.topsites.click.record({
          advertiser_name,
          tile_id,
          newtab_visit_id: session.session_id,
          is_sponsored: true,
          position,
        });
      }
    } else {
      console.error("Unknown ping type for sponsored TopSites impression");
      return;
    }

    Glean.topSites.pingType.set(pingType);
    Glean.topSites.position.set(legacyTelemetryPosition);
    Glean.topSites.source.set(source);
    Glean.topSites.tileId.set(tile_id);
    if (data.reporting_url && !unifiedAdsTilesEnabled) {
      Glean.topSites.reportingUrl.set(data.reporting_url);
    }
    Glean.topSites.advertiser.set(advertiser_name);
    Glean.topSites.contextId.set(lazy.contextId);
    GleanPings.topSites.submit();

    if (data.reporting_url && this.canSendUnifiedAdsTilesCallbacks) {
      // Send callback events to MARS unified ads api
      this.sendUnifiedAdsCallbackEvent({
        url: data.reporting_url,
        position,
      });
    }
  }

  handleTopSitesOrganicImpressionStats(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (!session) {
      return;
    }

    switch (action.data?.type) {
      case "impression":
        Glean.topsites.impression.record({
          newtab_visit_id: session.session_id,
          is_sponsored: false,
          position: action.data.position,
        });
        break;

      case "click":
        Glean.topsites.click.record({
          newtab_visit_id: session.session_id,
          is_sponsored: false,
          position: action.data.position,
        });
        break;

      default:
        break;
    }
  }

  handleUserEvent(action) {
    let userEvent = this.createUserEvent(action);
    this.sendUTEvent(userEvent, this.utEvents.sendUserEvent);
  }

  handleDiscoveryStreamUserEvent(action) {
    const pocket_logged_in_status = lazy.pktApi.isUserLoggedIn();
    Glean.pocket.isSignedIn.set(pocket_logged_in_status);
    this.handleUserEvent({
      ...action,
      data: {
        ...(action.data || {}),
        value: {
          ...(action.data?.value || {}),
          pocket_logged_in_status,
        },
      },
    });
    const session = this.sessions.get(au.getPortIdOfSender(action));
    switch (action.data?.event) {
      case "CLICK": {
        const {
          card_type,
          corpus_item_id,
          feature,
          fetchTimestamp,
          firstVisibleTimestamp,
          format,
          is_list_card,
          is_section_followed,
          matches_selected_topic,
          received_rank,
          recommendation_id,
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
          const metricNameSpace = this.privatePingEnabled
            ? Glean.newtabContent
            : Glean.pocket;
          metricNameSpace.click.record({
            newtab_visit_id: session.session_id,
            is_sponsored: card_type === "spoc",
            ...(format ? { format } : {}),
            ...(section
              ? {
                  section,
                  section_position,
                  is_section_followed,
                }
              : {}),
            matches_selected_topic,
            selected_topics,
            topic,
            is_list_card,
            position: action.data.action_position,
            tile_id,
            // We conditionally add in a few props.
            ...(corpus_item_id ? { corpus_item_id } : {}),
            ...(scheduled_corpus_item_id ? { scheduled_corpus_item_id } : {}),
            ...(corpus_item_id || scheduled_corpus_item_id
              ? {
                  received_rank,
                  recommended_at,
                }
              : {
                  recommendation_id,
                }),
          });
          if (shim) {
            if (this.canSendUnifiedAdsSpocCallbacks) {
              // Send unified ads callback event
              this.sendUnifiedAdsCallbackEvent({
                url: shim,
                position: action.data.action_position,
              });
            } else {
              Glean.pocket.shim.set(shim);
              if (fetchTimestamp) {
                Glean.pocket.fetchTimestamp.set(fetchTimestamp * 1000);
              }
              if (firstVisibleTimestamp) {
                Glean.pocket.newtabCreationTimestamp.set(
                  firstVisibleTimestamp * 1000
                );
              }
              GleanPings.spoc.submit("click");
            }
          }
        }
        break;
      }
      case "POCKET_THUMBS_DOWN":
      case "POCKET_THUMBS_UP": {
        const {
          corpus_item_id,
          format,
          is_section_followed,
          received_rank,
          recommendation_id,
          recommended_at,
          scheduled_corpus_item_id,
          section_position,
          section,
          thumbs_down,
          thumbs_up,
          tile_id,
          topic,
        } = action.data.value ?? {};
        const metricNameSpace = this.privatePingEnabled
          ? Glean.newtabContent
          : Glean.pocket;
        metricNameSpace.thumbVotingInteraction.record({
          newtab_visit_id: session.session_id,
          tile_id,
          // We conditionally add in a few props.
          ...(corpus_item_id ? { corpus_item_id } : {}),
          ...(scheduled_corpus_item_id ? { scheduled_corpus_item_id } : {}),
          ...(corpus_item_id || scheduled_corpus_item_id
            ? {
                received_rank,
                recommended_at,
              }
            : {
                recommendation_id,
              }),
          thumbs_up,
          thumbs_down,
          topic,
          ...(format ? { format } : {}),
          ...(section
            ? {
                section,
                section_position,
                is_section_followed,
              }
            : {}),
        });
        break;
      }
      case "SAVE_TO_POCKET": {
        const {
          card_type,
          corpus_item_id,
          fetchTimestamp,
          format,
          is_list_card,
          is_section_followed,
          matches_selected_topic,
          newtabCreationTimestamp,
          received_rank,
          recommendation_id,
          recommended_at,
          scheduled_corpus_item_id,
          section_position,
          section,
          selected_topics,
          shim,
          tile_id,
          topic,
        } = action.data.value ?? {};
        Glean.pocket.save.record({
          newtab_visit_id: session.session_id,
          is_sponsored: card_type === "spoc",
          ...(format ? { format } : {}),
          ...(section
            ? {
                section,
                section_position,
                is_section_followed,
              }
            : {}),
          topic,
          matches_selected_topic,
          selected_topics,
          position: action.data.action_position,
          tile_id,
          is_list_card,
          // We conditionally add in a few props.
          ...(corpus_item_id ? { corpus_item_id } : {}),
          ...(scheduled_corpus_item_id ? { scheduled_corpus_item_id } : {}),
          ...(corpus_item_id || scheduled_corpus_item_id
            ? {
                received_rank,
                recommended_at,
              }
            : {
                recommendation_id,
              }),
        });
        if (shim) {
          Glean.pocket.shim.set(shim);
          if (fetchTimestamp) {
            Glean.pocket.fetchTimestamp.set(fetchTimestamp * 1000);
          }
          if (newtabCreationTimestamp) {
            Glean.pocket.newtabCreationTimestamp.set(
              newtabCreationTimestamp * 1000
            );
          }
          GleanPings.spoc.submit("save");
        }
        break;
      }
      case "FAKESPOT_CLICK": {
        const { product_id, category } = action.data.value ?? {};
        Glean.newtab.fakespotClick.record({
          newtab_visit_id: session.session_id,
          product_id,
          category,
        });
        break;
      }
      case "FAKESPOT_CATEGORY": {
        const { category } = action.data.value ?? {};
        Glean.newtab.fakespotCategory.record({
          newtab_visit_id: session.session_id,
          category,
        });
        break;
      }
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
    const allowed =
      this._prefs
        .get(PREF_ENDPOINTS)
        .split(",")
        .map(item => item.trim())
        .filter(item => item) || [];
    if (!allowed.some(prefix => data.url.startsWith(prefix))) {
      throw new Error(
        `[Unified ads callback] Not one of allowed prefixes (${allowed})`
      );
    }

    const url = new URL(data.url);
    url.searchParams.append("position", data.position);

    try {
      await fetch(url.toString());
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
          !lazy.AboutNewTab.newTabURL.startsWith("moz-extension://")
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
        !homePageURL.startsWith("moz-extension://")
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

      if (lazy.NimbusFeatures.glean.getVariable("newtabPingEnabled") ?? true) {
        if (this.privatePingEnabled) {
          this.configureContentPing("component_init");
        }
        GleanPings.newtab.submit("component_init");
      }
    }
  }

  /**
   *
   * @param {String} submitReason reason why the ping is being submitted.
   * "component_init" | "newtab_session_end"
   */
  async configureContentPing(submitReason) {
    const expContext = this.experimentManager.createTargetingContext();
    const followed = this.getFollowedSections();
    if (followed.length) {
      Glean.newtabContent.followedSections.set(followed);
    }
    Glean.newtabContent.coarseOs.set(lazy.NewTabUtils.normalizeOs());
    // if os.version is undefined pass "0"
    Glean.newtabContent.coarseOsVersion.set(this.clientInfo.os.version || "0");
    Glean.newtabContent.utcOffset.set(lazy.NewTabUtils.getUtcOffset());
    Glean.newtabContent.activeExperiments.set(
      await expContext.activeExperiments
    );
    Glean.newtabContent.activeRollouts.set(await expContext.activeRollouts);
    Glean.newtabContent.enrollmentsMap.set(
      Object.entries(await expContext.enrollmentsMap).map(
        ([experimentSlug, branchSlug]) => ({
          experimentSlug,
          branchSlug,
        })
      )
    );
    GleanPings.newtabContent.submit(submitReason);
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
      case at.SAVE_SESSION_PERF_DATA:
        this.saveSessionPerfData(au.getPortIdOfSender(action), action.data);
        break;
      case at.DISCOVERY_STREAM_IMPRESSION_STATS:
        this.handleDiscoveryStreamImpressionStats(
          au.getPortIdOfSender(action),
          action.data
        );
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
        this.handleWeatherUserEvent(action);
        break;
      case at.TOPIC_SELECTION_USER_OPEN:
      case at.TOPIC_SELECTION_USER_DISMISS:
      case at.TOPIC_SELECTION_USER_SAVE:
        this.handleTopicSelectionUserEvent(action);
        break;
      case at.FAKESPOT_DISMISS: {
        const session = this.sessions.get(au.getPortIdOfSender(action));
        if (session) {
          Glean.newtab.fakespotDismiss.record({
            newtab_visit_id: session.session_id,
          });
        }
        break;
      }
      case at.FAKESPOT_CTA_CLICK: {
        const session = this.sessions.get(au.getPortIdOfSender(action));
        if (session) {
          Glean.newtab.fakespotCtaClick.record({
            newtab_visit_id: session.session_id,
          });
        }
        break;
      }
      case at.OPEN_ABOUT_FAKESPOT: {
        const session = this.sessions.get(au.getPortIdOfSender(action));
        if (session) {
          Glean.newtab.fakespotAboutClick.record({
            newtab_visit_id: session.session_id,
          });
        }
        break;
      }
      case at.BLOCK_SECTION:
      // Intentional fall-through
      case at.CARD_SECTION_IMPRESSION:
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
      case at.REPORT_AD_OPEN:
      case at.REPORT_AD_SUBMIT:
        this.handleReportAdUserEvent(action);
        break;
      case at.REPORT_CONTENT_OPEN:
      case at.REPORT_CONTENT_SUBMIT:
        this.handleReportContentUserEvent(action);
        break;
    }
  }

  async handleReportAdUserEvent(action) {
    const { placement_id, position, report_reason, reporting_url } =
      action.data || {};

    const url = new URL(reporting_url);
    url.searchParams.append("placement_id", placement_id);
    url.searchParams.append("reason", report_reason);
    url.searchParams.append("position", position);
    const adResponse = url.toString();

    const allowed =
      this._prefs
        .get(PREF_ENDPOINTS)
        .split(",")
        .map(item => item.trim())
        .filter(item => item) || [];

    if (!allowed.some(prefix => adResponse.startsWith(prefix))) {
      throw new Error(
        `[Unified ads callback] Not one of allowed prefixes (${allowed})`
      );
    }

    try {
      await fetch(adResponse);
    } catch (error) {
      console.error("Error:", error);
    }
  }

  handleReportContentUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    const {
      card_type,
      corpus_item_id,
      is_section_followed,
      received_rank,
      recommended_at,
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
        case "REPORT_CONTENT_OPEN":
          Glean.newtab.reportContentOpen.record({
            newtab_visit_id: session.session_id,
          });
          break;
        case "REPORT_CONTENT_SUBMIT":
          Glean.newtab.reportContentSubmit.record({
            card_type,
            corpus_item_id,
            is_section_followed,
            newtab_visit_id: session.session_id,
            received_rank,
            recommended_at,
            report_reason,
            scheduled_corpus_item_id,
            section_position,
            section,
            title,
            topic,
            url,
          });
          break;
      }
    }
  }

  handleCardSectionUserEvent(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    if (session) {
      const { section, section_position, event_source, is_section_followed } =
        action.data;
      const metricNameSpace = this.privatePingEnabled
        ? Glean.newtabContent
        : Glean.newtab;
      switch (action.type) {
        case "BLOCK_SECTION":
          metricNameSpace.sectionsBlockSection.record({
            newtab_visit_id: session.session_id,
            section,
            section_position,
            event_source,
          });
          break;
        case "UNBLOCK_SECTION":
          metricNameSpace.sectionsUnblockSection.record({
            newtab_visit_id: session.session_id,
            section,
            section_position,
            event_source,
          });
          break;
        case "CARD_SECTION_IMPRESSION":
          metricNameSpace.sectionsImpression.record({
            newtab_visit_id: session.session_id,
            section,
            section_position,
            is_section_followed,
          });
          break;
        case "FOLLOW_SECTION": {
          metricNameSpace.sectionsFollowSection.record({
            newtab_visit_id: session.session_id,
            section,
            section_position,
            event_source,
          });
          break;
        }
        case "UNFOLLOW_SECTION":
          metricNameSpace.sectionsUnfollowSection.record({
            newtab_visit_id: session.session_id,
            section,
            section_position,
            event_source,
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
    if (action.data.name === "weather.display") {
      if (!session) {
        return;
      }
      Glean.newtab.weatherChangeDisplay.record({
        newtab_visit_id: session.session_id,
        weather_display_mode: action.data.value,
      });
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
        const metricNameSpace = this.privatePingEnabled
          ? Glean.newtabContent
          : Glean.pocket;
        metricNameSpace.dismiss.record({
          newtab_visit_id: session.session_id,
          is_sponsored: datum.card_type === "spoc",
          ...(datum.format ? { format: datum.format } : {}),
          position: datum.pos,
          tile_id: datum.id || datum.tile_id,
          is_list_card: datum.is_list_card,
          ...(datum.section
            ? {
                section: datum.section,
                section_position: datum.section_position,
                is_section_followed: datum.is_section_followed,
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
            : {
                recommendation_id: datum.recommendation_id,
              }),
        });
        continue;
      }
      const { position, advertiser_name, tile_id, isSponsoredTopSite } = datum;
      Glean.topsites.dismiss.record({
        advertiser_name,
        tile_id,
        newtab_visit_id: session.session_id,
        is_sponsored: !!isSponsoredTopSite,
        position,
      });
    }
  }

  handleAboutSponsoredTopSites(action) {
    const session = this.sessions.get(au.getPortIdOfSender(action));
    const { data } = action;
    const { position, advertiser_name, tile_id } = data;

    if (session) {
      Glean.topsites.showPrivacyClick.record({
        advertiser_name,
        tile_id,
        newtab_visit_id: session.session_id,
        position,
      });
    }
  }

  /**
   * Handle impression stats actions from Discovery Stream.
   *
   * @param {String} port  The session port with which this is associated
   * @param {Object} data  The impression data structured as {source: "SOURCE", tiles: [{id: 123}]}
   *
   */
  handleDiscoveryStreamImpressionStats(port, data) {
    let session = this.sessions.get(port);

    if (!session) {
      throw new Error("Session does not exist.");
    }

    const { tiles } = data;

    tiles.forEach(tile => {
      // if the tile has a category it is a product tile from fakespot
      if (tile.type === "fakespot") {
        Glean.newtab.fakespotProductImpression.record({
          newtab_visit_id: session.session_id,
          product_id: tile.id,
          category: tile.category,
        });
      } else {
        const { corpus_item_id, scheduled_corpus_item_id } = tile;
        const metricNameSpace = this.privatePingEnabled
          ? Glean.newtabContent
          : Glean.pocket;
        metricNameSpace.impression.record({
          newtab_visit_id: session.session_id,
          is_sponsored: tile.type === "spoc",
          ...(tile.format ? { format: tile.format } : {}),
          ...(tile.section
            ? {
                section: tile.section,
                section_position: tile.section_position,
                is_section_followed: tile.is_section_followed,
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
            : {
                recommendation_id: tile.recommendation_id,
              }),
        });
      }
      if (tile.shim) {
        if (this.canSendUnifiedAdsSpocCallbacks) {
          // Send unified ads callback event
          this.sendUnifiedAdsCallbackEvent({
            url: tile.shim,
            position: tile.pos,
          });
        } else {
          Glean.pocket.shim.set(tile.shim);
          if (tile.fetchTimestamp) {
            Glean.pocket.fetchTimestamp.set(tile.fetchTimestamp * 1000);
          }
          if (data.firstVisibleTimestamp) {
            Glean.pocket.newtabCreationTimestamp.set(
              data.firstVisibleTimestamp * 1000
            );
          }
          GleanPings.spoc.submit("impression");
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
   * @note Any existing keys with the same names already in the
   * session perf object will be overwritten by values passed in here.
   *
   * @param {String} port  The session with which this is associated
   * @param {Object} data  The perf data to be
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
    Glean.pocket.isSignedIn.set(lazy.pktApi.isUserLoggedIn());

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

  _setNewtabPrefMetrics(fullPrefName, isChanged) {
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

    try {
      Services.obs.removeObserver(
        this.browserOpenNewtabStart,
        "browser-open-newtab-start"
      );
    } catch (e) {
      // Operation can fail when uninit is called before
      // init has finished setting up the observer
    }

    // TODO: Send any unfinished sessions
  }
}
