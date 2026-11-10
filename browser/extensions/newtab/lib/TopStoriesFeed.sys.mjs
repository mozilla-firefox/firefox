/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {Store} from "resource://newtab/lib/Store.sys.mjs" */

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import { Prefs } from "resource://newtab/lib/ActivityStreamPrefs.sys.mjs";
import { SectionsManager } from "resource://newtab/lib/SectionsManager.sys.mjs";
import { PersistentCache } from "resource://newtab/lib/PersistentCache.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
});

export const STORIES_UPDATE_TIME = 30 * 60 * 1000; // 30 minutes
export const TOPICS_UPDATE_TIME = 3 * 60 * 60 * 1000; // 3 hours
const STORIES_NOW_THRESHOLD = 24 * 60 * 60 * 1000; // 24 hours
export const DEFAULT_RECS_EXPIRE_TIME = 60 * 60 * 1000; // 1 hour
export const SECTION_ID = "topstories";
const IMPRESSION_SOURCE = "TOP_STORIES";

export const SPOC_IMPRESSION_TRACKING_PREF =
  "feeds.section.topstories.spoc.impressions";

const DISCOVERY_STREAM_PREF_ENABLED = "discoverystream.enabled";
const DISCOVERY_STREAM_PREF_ENABLED_PATH =
  "browser.newtabpage.activity-stream.discoverystream.enabled";
export const REC_IMPRESSION_TRACKING_PREF =
  "feeds.section.topstories.rec.impressions";
const PREF_USER_TOPSTORIES = "feeds.section.topstories";
const MAX_LIFETIME_CAP = 500; // Guard against misconfiguration on the server
const DISCOVERY_STREAM_PREF = "discoverystream.config";

export class TopStoriesFeed {
  /** @type {Store} */
  store = null;

  constructor(ds) {
    // Use discoverystream config pref default values for fast path and
    // if needed lazy load activity stream top stories feed based on
    // actual user preference when INIT and PREF_CHANGED is invoked
    this.discoveryStreamEnabled =
      ds &&
      ds.value &&
      JSON.parse(ds.value).enabled &&
      Services.prefs.getBoolPref(DISCOVERY_STREAM_PREF_ENABLED_PATH, false);
    if (!this.discoveryStreamEnabled) {
      this.initializeProperties();
    }
  }

  initializeProperties() {
    this.contentUpdateQueue = [];
    this.spocCampaignMap = new Map();
    this.cache = new PersistentCache(SECTION_ID, true);
    this._prefs = new Prefs();
    this.propertiesInitialized = true;
  }

  async onInit() {
    SectionsManager.enableSection(SECTION_ID, true /* isStartup */);
    if (this.discoveryStreamEnabled) {
      return;
    }

    try {
      const { options } = SectionsManager.sections.get(SECTION_ID);
      const apiKey = this.getApiKeyFromPref(options.api_key_pref);
      this.stories_endpoint = this.produceFinalEndpointUrl(
        options.stories_endpoint,
        apiKey
      );
      this.topics_endpoint = this.produceFinalEndpointUrl(
        options.topics_endpoint,
        apiKey
      );
      this.read_more_endpoint = options.read_more_endpoint;
      this.stories_referrer = options.stories_referrer;
      this.show_spocs = options.show_spocs;
      this.storiesLastUpdated = 0;
      this.topicsLastUpdated = 0;
      this.storiesLoaded = false;
      this.dispatchPocketCta(this._prefs.get("pocketCta"), false);

      // Cache is used for new page loads, which shouldn't have changed data.
      // If we have changed data, cache should be cleared,
      // and last updated should be 0, and we can fetch.
      let { stories, topics } = await this.loadCachedData();
      if (this.storiesLastUpdated === 0) {
        stories = await this.fetchStories();
      }
      if (this.topicsLastUpdated === 0) {
        topics = await this.fetchTopics();
      }
      this.doContentUpdate({ stories, topics }, true);
      this.storiesLoaded = true;

      // This is filtered so an update function can return true to retry on the next run
      this.contentUpdateQueue = this.contentUpdateQueue.filter(update =>
        update()
      );
    } catch (e) {
      console.error(`Problem initializing top stories feed: ${e.message}`);
    }
  }

  init() {
    SectionsManager.onceInitialized(this.onInit.bind(this));
  }

  async clearCache() {
    await this.cache.set("stories", {});
    await this.cache.set("topics", {});
    await this.cache.set("spocs", {});
  }

  uninit() {
    this.storiesLoaded = false;
    SectionsManager.disableSection(SECTION_ID);
  }

  dispatchPocketCta(data, shouldBroadcast) {
    const action = { type: at.POCKET_CTA, data: JSON.parse(data) };
    this.store.dispatch(
      shouldBroadcast
        ? ac.BroadcastToContent(action)
        : ac.AlsoToPreloaded(action)
    );
  }

  /**
   * doContentUpdate - Updates topics and stories in the topstories section.
   *
   *                   Sections have one update action for the whole section.
   *                   Redux creates a state race condition if you call the same action,
   *                   twice, concurrently. Because of this, doContentUpdate is
   *                   one place to update both topics and stories in a single action.
   *
   *                   Section updates used old topics if none are available,
   *                   but clear stories if none are available. Because of this, if no
   *                   stories are passed, we instead use the existing stories in state.
   *
   * @param {object} This is an object with potential new stories or topics.
   * @param {boolean} shouldBroadcast If we should update existing tabs or not. For first page
   *                  loads or pref changes, we want to update existing tabs,
   *                  for system tick or other updates we do not.
   */
  doContentUpdate({ stories, topics }, shouldBroadcast) {
    let updateProps = {};
    if (stories) {
      updateProps.rows = stories;
    } else {
      const { Sections } = this.store.getState();
      if (Sections && Sections.find) {
        updateProps.rows = Sections.find(s => s.id === SECTION_ID).rows;
      }
    }
    if (topics) {
      Object.assign(updateProps, {
        topics,
        read_more_endpoint: this.read_more_endpoint,
      });
    }

    // We should only be calling this once per init.
    this.dispatchUpdateEvent(shouldBroadcast, updateProps);
  }

  async fetchStories() {
    if (!this.stories_endpoint) {
      return null;
    }
    try {
      const response = await fetch(this.stories_endpoint, {
        credentials: "omit",
      });
      if (!response.ok) {
        throw new Error(
          `Stories endpoint returned unexpected status: ${response.status}`
        );
      }

      /** @type {object} */
      const body = await response.json();
      this.updateSettings(body.settings);
      this.stories = this.rotate(this.transform(body.recommendations));
      this.cleanUpTopRecImpressionPref();

      if (this.show_spocs && body.spocs) {
        this.spocCampaignMap = new Map(
          body.spocs.map(s => [s.id, `${s.campaign_id}`])
        );
        this.spocs = this.transform(body.spocs);
        this.cleanUpCampaignImpressionPref();
      }
      this.storiesLastUpdated = Date.now();
      body._timestamp = this.storiesLastUpdated;
      this.cache.set("stories", body);
    } catch (error) {
      console.error(`Failed to fetch content: ${error.message}`);
    }
    return this.stories;
  }

  async loadCachedData() {
    const data = await this.cache.get();
    let stories = data.stories && data.stories.recommendations;
    let topics = data.topics && data.topics.topics;

    if (stories && !!stories.length && this.storiesLastUpdated === 0) {
      this.updateSettings(data.stories.settings);
      this.stories = this.rotate(this.transform(stories));
      this.storiesLastUpdated = data.stories._timestamp;
      if (data.stories.spocs && data.stories.spocs.length) {
        this.spocCampaignMap = new Map(
          data.stories.spocs.map(s => [s.id, `${s.campaign_id}`])
        );
        this.spocs = this.transform(data.stories.spocs);
        this.cleanUpCampaignImpressionPref();
      }
    }
    if (topics && !!topics.length && this.topicsLastUpdated === 0) {
      this.topics = topics;
      this.topicsLastUpdated = data.topics._timestamp;
    }

    return { topics: this.topics, stories: this.stories };
  }

  transform(items) {
    if (!items) {
      return [];
    }

    const calcResult = items
      .filter(s => !lazy.NewTabUtils.blockedLinks.isBlocked({ url: s.url }))
      .map(s => {
        let mapped = {
          guid: s.id,
          hostname:
            s.domain ||
            lazy.NewTabUtils.shortURL(Object.assign({}, s, { url: s.url })),
          type:
            Date.now() - s.published_timestamp * 1000 <= STORIES_NOW_THRESHOLD
              ? "now"
              : "trending",
          context: s.context,
          icon: s.icon,
          title: s.title,
          description: s.excerpt,
          image: this.normalizeUrl(s.image_src),
          referrer: this.stories_referrer,
          url: s.url,
          score: s.item_score || 1,
          spoc_meta: this.show_spocs
            ? { campaign_id: s.campaign_id, caps: s.caps }
            : {},
        };

        // Very old cached spocs may not contain an `expiration_timestamp` property
        if (s.expiration_timestamp) {
          mapped.expiration_timestamp = s.expiration_timestamp;
        }

        return mapped;
      })
      .sort(this.compareScore);

    return calcResult;
  }

  async fetchTopics() {
    if (!this.topics_endpoint) {
      return null;
    }
    try {
      const response = await fetch(this.topics_endpoint, {
        credentials: "omit",
      });
      if (!response.ok) {
        throw new Error(
          `Topics endpoint returned unexpected status: ${response.status}`
        );
      }
      /** @type {object} */
      const body = await response.json();
      const { topics } = body;
      if (topics) {
        this.topics = topics;
        this.topicsLastUpdated = Date.now();
        body._timestamp = this.topicsLastUpdated;
        this.cache.set("topics", body);
      }
    } catch (error) {
      console.error(`Failed to fetch topics: ${error.message}`);
    }
    return this.topics;
  }

  dispatchUpdateEvent(shouldBroadcast, data) {
    SectionsManager.updateSection(SECTION_ID, data, shouldBroadcast);
  }

  compareScore(a, b) {
    return b.score - a.score;
  }

  updateSettings(settings = {}) {
    this.spocsPerNewTabs = settings.spocsPerNewTabs || 1; // Probability of a new tab getting a spoc [0,1]
    this.recsExpireTime = settings.recsExpireTime;
  }

  // We rotate stories on the client so that
  // active stories are at the front of the list, followed by stories that have expired
  // impressions i.e. have been displayed for longer than recsExpireTime.
  rotate(items) {
    if (items.length <= 3) {
      return items;
    }

    const maxImpressionAge = Math.max(
      this.recsExpireTime * 1000 || DEFAULT_RECS_EXPIRE_TIME,
      DEFAULT_RECS_EXPIRE_TIME
    );
    const impressions = this.readImpressionsPref(REC_IMPRESSION_TRACKING_PREF);
    const expired = [];
    const active = [];
    for (const item of items) {
      if (
        impressions[item.guid] &&
        Date.now() - impressions[item.guid] >= maxImpressionAge
      ) {
        expired.push(item);
      } else {
        active.push(item);
      }
    }
    return active.concat(expired);
  }

  getApiKeyFromPref(apiKeyPref) {
    if (!apiKeyPref) {
      return apiKeyPref;
    }

    return (
      this._prefs.get(apiKeyPref) || Services.prefs.getCharPref(apiKeyPref)
    );
  }

  produceFinalEndpointUrl(url, apiKey) {
    if (!url) {
      return url;
    }
    if (url.includes("$apiKey") && !apiKey) {
      throw new Error(`An API key was specified but none configured: ${url}`);
    }
    return url.replace("$apiKey", apiKey);
  }

  // Need to remove parenthesis from image URLs as React will otherwise
  // fail to render them properly as part of the card template.
  normalizeUrl(url) {
    if (url) {
      return url.replace(/\(/g, "%28").replace(/\)/g, "%29");
    }
    return url;
  }

  shouldShowSpocs() {
    return this.show_spocs && this.store.getState().Prefs.values.showSponsored;
  }

  dispatchSpocDone(target) {
    const action = { type: at.POCKET_WAITING_FOR_SPOC, data: false };
    this.store.dispatch(ac.OnlyToOneContent(action, target));
  }

  filterSpocs() {
    if (!this.shouldShowSpocs()) {
      return [];
    }

    if (Math.random() > this.spocsPerNewTabs) {
      return [];
    }

    if (!this.spocs || !this.spocs.length) {
      // We have stories but no spocs so there's nothing to do and this update can be
      // removed from the queue.
      return [];
    }

    // Filter spocs based on frequency caps
    const impressions = this.readImpressionsPref(SPOC_IMPRESSION_TRACKING_PREF);
    let spocs = this.spocs.filter(s =>
      this.isBelowFrequencyCap(impressions, s)
    );

    // Filter out expired spocs based on `expiration_timestamp`
    spocs = spocs.filter(spoc => {
      // If cached data is so old it doesn't contain this property, assume the spoc is ok to show
      if (!(`expiration_timestamp` in spoc)) {
        return true;
      }
      // `expiration_timestamp` is the number of seconds elapsed since January 1, 1970 00:00:00 UTC
      return spoc.expiration_timestamp * 1000 > Date.now();
    });

    return spocs;
  }

  maybeAddSpoc(target) {
    const updateContent = () => {
      let spocs = this.filterSpocs();

      if (!spocs.length) {
        this.dispatchSpocDone(target);
        return false;
      }

      // Create a new array with a spoc inserted at index 2
      const section = this.store
        .getState()
        .Sections.find(s => s.id === SECTION_ID);
      let rows = section.rows.slice(0, this.stories.length);
      rows.splice(2, 0, Object.assign(spocs[0], { pinned: true }));

      // Send a content update to the target tab
      const action = {
        type: at.SECTION_UPDATE,
        data: Object.assign({ rows }, { id: SECTION_ID }),
      };
      this.store.dispatch(ac.OnlyToOneContent(action, target));
      this.dispatchSpocDone(target);
      return false;
    };

    if (this.storiesLoaded) {
      updateContent();
    } else {
      // Delay updating tab content until initial data has been fetched
      this.contentUpdateQueue.push(updateContent);
    }
  }

  // Frequency caps are based on campaigns, which may include multiple spocs.
  // We currently support two types of frequency caps:
  // - lifetime: Indicates how many times spocs from a campaign can be shown in total
  // - period: Indicates how many times spocs from a campaign can be shown within a period
  //
  // So, for example, the feed configuration below defines that for campaign 1 no more
  // than 5 spocs can be show in total, and no more than 2 per hour.
  // "campaign_id": 1,
  // "caps": {
  //  "lifetime": 5,
  //  "campaign": {
  //    "count": 2,
  //    "period": 3600
  //  }
  // }
  isBelowFrequencyCap(impressions, spoc) {
    const campaignImpressions = impressions[spoc.spoc_meta.campaign_id];
    if (!campaignImpressions) {
      return true;
    }

    const lifeTimeCap = Math.min(
      spoc.spoc_meta.caps && spoc.spoc_meta.caps.lifetime,
      MAX_LIFETIME_CAP
    );
    const lifeTimeCapExceeded = campaignImpressions.length >= lifeTimeCap;
    if (lifeTimeCapExceeded) {
      return false;
    }

    const campaignCap =
      (spoc.spoc_meta.caps && spoc.spoc_meta.caps.campaign) || {};
    const campaignCapExceeded =
      campaignImpressions.filter(
        i => Date.now() - i < campaignCap.period * 1000
      ).length >= campaignCap.count;
    return !campaignCapExceeded;
  }

  // Clean up campaign impression pref by removing all campaigns that are no
  // longer part of the response, and are therefore considered inactive.
  cleanUpCampaignImpressionPref() {
    const campaignIds = new Set(this.spocCampaignMap.values());
    this.cleanUpImpressionPref(
      id => !campaignIds.has(id),
      SPOC_IMPRESSION_TRACKING_PREF
    );
  }

  // Clean up rec impression pref by removing all stories that are no
  // longer part of the response.
  cleanUpTopRecImpressionPref() {
    const activeStories = new Set(this.stories.map(s => `${s.guid}`));
    this.cleanUpImpressionPref(
      id => !activeStories.has(id),
      REC_IMPRESSION_TRACKING_PREF
    );
  }

  /**
   * Cleans up the provided impression pref (spocs or recs).
   *
   * @param isExpired predicate (boolean-valued function) that returns whether or not
   * the impression for the given key is expired.
   * @param pref the impression pref to clean up.
   */
  cleanUpImpressionPref(isExpired, pref) {
    const impressions = this.readImpressionsPref(pref);
    let changed = false;

    Object.keys(impressions).forEach(id => {
      if (isExpired(id)) {
        changed = true;
        delete impressions[id];
      }
    });

    if (changed) {
      this.writeImpressionsPref(pref, impressions);
    }
  }

  // Sets a pref mapping campaign IDs to timestamp arrays.
  // The timestamps represent impressions which are used to calculate frequency caps.
  recordCampaignImpression(campaignId) {
    let impressions = this.readImpressionsPref(SPOC_IMPRESSION_TRACKING_PREF);

    const timeStamps = impressions[campaignId] || [];
    timeStamps.push(Date.now());
    impressions = Object.assign(impressions, { [campaignId]: timeStamps });

    this.writeImpressionsPref(SPOC_IMPRESSION_TRACKING_PREF, impressions);
  }

  // Sets a pref mapping story (rec) IDs to a single timestamp (time of first impression).
  // We use these timestamps to guarantee a story doesn't stay on top for longer than
  // configured in the feed settings (settings.recsExpireTime).
  recordTopRecImpressions(topItems) {
    let impressions = this.readImpressionsPref(REC_IMPRESSION_TRACKING_PREF);
    let changed = false;

    topItems.forEach(t => {
      if (!impressions[t]) {
        changed = true;
        impressions = Object.assign(impressions, { [t]: Date.now() });
      }
    });

    if (changed) {
      this.writeImpressionsPref(REC_IMPRESSION_TRACKING_PREF, impressions);
    }
  }

  readImpressionsPref(pref) {
    const prefVal = this._prefs.get(pref);
    return prefVal ? JSON.parse(prefVal) : {};
  }

  writeImpressionsPref(pref, impressions) {
    this._prefs.set(pref, JSON.stringify(impressions));
  }

  async removeSpocs() {
    // Quick hack so that SPOCS are removed from all open and preloaded tabs when
    // they are disabled. The longer term fix should probably be to remove them
    // in the Reducer.
    await this.clearCache();
    this.uninit();
    this.init();
  }

  lazyLoadTopStories(options = {}) {
    let { dsPref, userPref } = options;
    if (!dsPref) {
      dsPref = this.store.getState().Prefs.values[DISCOVERY_STREAM_PREF];
    }
    if (!userPref) {
      userPref = this.store.getState().Prefs.values[PREF_USER_TOPSTORIES];
    }

    try {
      this.discoveryStreamEnabled =
        JSON.parse(dsPref).enabled &&
        this.store.getState().Prefs.values[DISCOVERY_STREAM_PREF_ENABLED];
    } catch (e) {
      // Load activity stream top stories if fail to determine discovery stream state
      this.discoveryStreamEnabled = false;
    }

    // Return without invoking initialization if top stories are loaded, or preffed off.
    if (this.storiesLoaded || !userPref) {
      return;
    }

    if (!this.discoveryStreamEnabled && !this.propertiesInitialized) {
      this.initializeProperties();
    }
    this.init();
  }

  handleDisabled(action) {
    switch (action.type) {
      case at.INIT:
        this.lazyLoadTopStories();
        break;
      case at.PREF_CHANGED:
        if (action.data.name === DISCOVERY_STREAM_PREF) {
          this.lazyLoadTopStories({ dsPref: action.data.value });
        }
        if (action.data.name === DISCOVERY_STREAM_PREF_ENABLED) {
          this.lazyLoadTopStories();
        }
        if (action.data.name === PREF_USER_TOPSTORIES) {
          if (action.data.value) {
            // init topstories if value if true.
            this.lazyLoadTopStories({ userPref: action.data.value });
          } else {
            this.uninit();
          }
        }
        break;
      case at.UNINIT:
        this.uninit();
        break;
    }
  }

  async onAction(action) {
    if (this.discoveryStreamEnabled) {
      this.handleDisabled(action);
      return;
    }
    switch (action.type) {
      // Check discoverystream pref and load activity stream top stories only if needed
      case at.INIT:
        this.lazyLoadTopStories();
        break;
      case at.SYSTEM_TICK: {
        let stories;
        let topics;
        if (Date.now() - this.storiesLastUpdated >= STORIES_UPDATE_TIME) {
          stories = await this.fetchStories();
        }
        if (Date.now() - this.topicsLastUpdated >= TOPICS_UPDATE_TIME) {
          topics = await this.fetchTopics();
        }
        this.doContentUpdate({ stories, topics }, false);
        break;
      }
      case at.UNINIT:
        this.uninit();
        break;
      case at.NEW_TAB_REHYDRATED:
        this.maybeAddSpoc(action.meta.fromTarget);
        break;
      case at.SECTION_OPTIONS_CHANGED:
        if (action.data === SECTION_ID) {
          await this.clearCache();
          this.uninit();
          this.init();
        }
        break;
      case at.PLACES_LINK_BLOCKED:
        if (this.spocs) {
          this.spocs = this.spocs.filter(s => s.url !== action.data.url);
        }
        break;
      case at.TELEMETRY_IMPRESSION_STATS: {
        // We want to make sure we only track impressions from Top Stories,
        // otherwise unexpected things that are not properly handled can happen.
        // Example: Impressions from spocs on Discovery Stream can cause the
        // Top Stories impressions pref to continuously grow, see bug #1523408
        if (action.data.source === IMPRESSION_SOURCE) {
          const payload = action.data;
          const viewImpression = !(
            "click" in payload ||
            "block" in payload ||
            "pocket" in payload
          );
          if (payload.tiles && viewImpression) {
            if (this.shouldShowSpocs()) {
              payload.tiles.forEach(t => {
                if (this.spocCampaignMap.has(t.id)) {
                  this.recordCampaignImpression(this.spocCampaignMap.get(t.id));
                }
              });
            }
            const topRecs = payload.tiles
              .filter(t => !this.spocCampaignMap.has(t.id))
              .map(t => t.id);
            this.recordTopRecImpressions(topRecs);
          }
        }
        break;
      }
      case at.PREF_CHANGED:
        if (action.data.name === DISCOVERY_STREAM_PREF) {
          this.lazyLoadTopStories({ dsPref: action.data.value });
        }
        if (action.data.name === PREF_USER_TOPSTORIES) {
          if (action.data.value) {
            // init topstories if value if true.
            this.lazyLoadTopStories({ userPref: action.data.value });
          } else {
            this.uninit();
          }
        }
        // Check if spocs was disabled. Remove them if they were.
        if (action.data.name === "showSponsored" && !action.data.value) {
          await this.removeSpocs();
        }
        if (action.data.name === "pocketCta") {
          this.dispatchPocketCta(action.data.value, true);
        }
        break;
    }
  }
}
