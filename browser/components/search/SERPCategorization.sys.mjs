/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Functionality related to categorizing SERPs.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EnrollmentType: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "gCryptoHash", () => {
  return Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);
});

const CATEGORIZATION_PREF =
  "browser.search.serpEventTelemetryCategorization.enabled";
const CATEGORIZATION_REGION_PREF =
  "browser.search.serpEventTelemetryCategorization.regionEnabled";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "serpEventTelemetryCategorization",
  CATEGORIZATION_PREF,
  false,
  (aPreference, previousValue, newValue) => {
    if (newValue) {
      SERPCategorization.init();
    } else {
      SERPCategorization.uninit({ deleteMap: true });
    }
  }
);

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "SearchTelemetry",
    maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
  });
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "activityLimit",
  "telemetry.fog.test.activity_limit",
  120
);

export const TELEMETRY_CATEGORIZATION_KEY = "search-categorization";
export const TELEMETRY_CATEGORIZATION_DOWNLOAD_SETTINGS = {
  // Units are in milliseconds.
  base: 3600000,
  minAdjust: 60000,
  maxAdjust: 600000,
  maxTriesPerSession: 2,
};

export const CATEGORIZATION_SETTINGS = {
  STORE_SCHEMA: 1,
  STORE_FILE: "domain_to_categories.sqlite",
  STORE_NAME: "domain_to_categories",
  MAX_DOMAINS_TO_CATEGORIZE: 10,
  MINIMUM_SCORE: 0,
  STARTING_RANK: 2,
  IDLE_TIMEOUT_SECONDS: 60 * 60,
  WAKE_TIMEOUT_MS: 60 * 60 * 1000,
  PING_SUBMISSION_THRESHOLD: 10,
  HAS_MATCHING_REGION: "SearchTelemetry:HasMatchingRegion",
  INCONCLUSIVE: 0,
};

/**
 * @typedef {object} CategorizationResult
 * @property {string} organic_category
 *  The category for the organic result.
 * @property {string} organic_num_domains
 *  The number of domains examined to determine the organic category result.
 * @property {string} organic_num_inconclusive
 *  The number of inconclusive domains when determining the organic result.
 * @property {string} organic_num_unknown
 *  The number of unknown domains when determining the organic result.
 * @property {string} sponsored_category
 *  The category for the sponsored result.
 * @property {string} sponsored_num_domains
 *  The number of domains examined to determine the sponsored category.
 * @property {string} sponsored_num_inconclusive
 *  The number of inconclusive domains when determining the sponsored category.
 * @property {string} sponsored_num_unknown
 *  The number of unknown domains when determining the sponsored category.
 * @property {string} mappings_version
 *  The category mapping version used to determine the categories.
 */

/**
 * @typedef {object} CategorizationExtraParams
 * @property {string} num_ads_clicked
 *  The total number of ads clicked on a SERP.
 * @property {string} num_ads_hidden
 *  The total number of ads hidden from the user when categorization occured.
 * @property {string} num_ads_loaded
 *  The total number of ads loaded when categorization occured.
 * @property {string} num_ads_visible
 *  The total number of ads visible to the user when categorization occured.
 */

/* eslint-disable jsdoc/valid-types */
/**
 * @typedef {CategorizationResult & CategorizationExtraParams} RecordCategorizationParameters
 */
/* eslint-enable jsdoc/valid-types */

/**
 * Categorizes SERPs.
 */
class Categorizer {
  async init() {
    if (this.enabled) {
      lazy.logConsole.debug("Initialize SERP categorizer.");
      await SERPDomainToCategoriesMap.init();
      SERPCategorizationEventScheduler.init();
      SERPCategorizationRecorder.init();
    }
  }

  async uninit({ deleteMap = false } = {}) {
    lazy.logConsole.debug("Uninit SERP categorizer.");
    await SERPDomainToCategoriesMap.uninit(deleteMap);
    SERPCategorizationEventScheduler.uninit();
    SERPCategorizationRecorder.uninit();
  }

  get enabled() {
    return lazy.serpEventTelemetryCategorization;
  }

  /**
   * Categorizes domains extracted from SERPs. Note that we don't process
   * domains if the domain-to-categories map is empty (if the client couldn't
   * download Remote Settings attachments, for example).
   *
   * @param {Set} nonAdDomains
   *   Domains from organic results extracted from the page.
   * @param {Set} adDomains
   *   Domains from ad results extracted from the page.
   * @returns {Promise<?CategorizationResult>}
   *   The final categorization result. Returns null if the map was empty.
   */
  async maybeCategorizeSERP(nonAdDomains, adDomains) {
    // Per DS, if the map was empty (e.g. because of a technical issue
    // downloading the data), we shouldn't report telemetry.
    // Thus, there is no point attempting to categorize the SERP.
    if (SERPDomainToCategoriesMap.empty) {
      SERPCategorizationRecorder.recordMissingImpressionTelemetry();
      return null;
    }
    /** @type {CategorizationResult} */
    let resultsToReport = {};

    let results = await this.applyCategorizationLogic(nonAdDomains);
    resultsToReport.organic_category = results.category;
    resultsToReport.organic_num_domains = results.num_domains;
    resultsToReport.organic_num_unknown = results.num_unknown;
    resultsToReport.organic_num_inconclusive = results.num_inconclusive;

    results = await this.applyCategorizationLogic(adDomains);
    resultsToReport.sponsored_category = results.category;
    resultsToReport.sponsored_num_domains = results.num_domains;
    resultsToReport.sponsored_num_unknown = results.num_unknown;
    resultsToReport.sponsored_num_inconclusive = results.num_inconclusive;

    resultsToReport.mappings_version =
      SERPDomainToCategoriesMap.version.toString();

    return resultsToReport;
  }

  /**
   * Applies the logic for reducing extracted domains to a single category for
   * the SERP.
   *
   * @param {Set} domains
   *   The domains extracted from the page.
   * @returns {Promise<object>} resultsToReport
   *   The final categorization results. Keys are: "category", "num_domains",
   *   "num_unknown" and "num_inconclusive".
   */
  async applyCategorizationLogic(domains) {
    let domainInfo = {};
    let domainsCount = 0;
    let unknownsCount = 0;
    let inconclusivesCount = 0;

    for (let domain of domains) {
      domainsCount++;

      let categoryCandidates = await SERPDomainToCategoriesMap.get(domain);

      if (!categoryCandidates.length) {
        unknownsCount++;
        continue;
      }

      // Inconclusive domains do not have more than one category candidate.
      if (
        categoryCandidates[0].category == CATEGORIZATION_SETTINGS.INCONCLUSIVE
      ) {
        inconclusivesCount++;
        continue;
      }

      domainInfo[domain] = categoryCandidates;
    }

    let finalCategory;
    let topCategories = [];
    // Determine if all domains were unknown or inconclusive.
    if (unknownsCount + inconclusivesCount == domainsCount) {
      finalCategory = CATEGORIZATION_SETTINGS.INCONCLUSIVE;
    } else {
      let maxScore = CATEGORIZATION_SETTINGS.MINIMUM_SCORE;
      let rank = CATEGORIZATION_SETTINGS.STARTING_RANK;
      for (let categoryCandidates of Object.values(domainInfo)) {
        for (let { category, score } of categoryCandidates) {
          let adjustedScore = score / Math.log2(rank);
          if (adjustedScore > maxScore) {
            maxScore = adjustedScore;
            topCategories = [category];
          } else if (adjustedScore == maxScore) {
            topCategories.push(Number(category));
          }
          rank++;
        }
      }
      finalCategory =
        topCategories.length > 1
          ? this.#chooseRandomlyFrom(topCategories)
          : topCategories[0];
    }

    return {
      category: finalCategory.toString(),
      num_domains: domainsCount.toString(),
      num_unknown: unknownsCount.toString(),
      num_inconclusive: inconclusivesCount.toString(),
    };
  }

  #chooseRandomlyFrom(categories) {
    let randIdx = Math.floor(Math.random() * categories.length);
    return categories[randIdx];
  }
}

/**
 * Contains outstanding categorizations of browser objects that have yet to be
 * scheduled to be reported into a Glean event.
 * They are kept here until one of the conditions are met:
 * 1. The browser that was tracked is no longer being tracked.
 * 2. A user has been idle for IDLE_TIMEOUT_SECONDS
 * 3. The user has awoken their computer and the time elapsed from the last
 *    categorization event exceeds WAKE_TIMEOUT_MS.
 */
class CategorizationEventScheduler {
  /**
   * A WeakMap containing browser objects mapped to a callback.
   *
   * @type {WeakMap | null}
   */
  #browserToCallbackMap = null;

  /**
   * An instance of user idle service. Cached for testing purposes.
   *
   * @type {nsIUserIdleService | null}
   */
  #idleService = null;

  /**
   * Whether it has been initialized.
   *
   * @type {boolean}
   */
  #init = false;

  /**
   * The last Date.now() of a callback insertion.
   *
   * @type {number | null}
   */
  #mostRecentMs = null;

  init() {
    if (this.#init) {
      return;
    }

    lazy.logConsole.debug("Initializing categorization event scheduler.");

    this.#browserToCallbackMap = new WeakMap();

    // In tests, we simulate idleness as it is more reliable and easier than
    // trying to replicate idleness. The way to do is so it by creating
    // an mock idle service and having the component subscribe to it. If we
    // used a lazy instantiation of idle service, the test could only ever be
    // subscribed to the real one.
    this.#idleService = Cc["@mozilla.org/widget/useridleservice;1"].getService(
      Ci.nsIUserIdleService
    );

    this.#idleService.addIdleObserver(
      this,
      CATEGORIZATION_SETTINGS.IDLE_TIMEOUT_SECONDS
    );

    Services.obs.addObserver(this, "quit-application");
    Services.obs.addObserver(this, "wake_notification");

    this.#init = true;
  }

  uninit() {
    if (!this.#init) {
      return;
    }

    this.#browserToCallbackMap = null;

    lazy.logConsole.debug("Un-initializing categorization event scheduler.");
    this.#idleService.removeIdleObserver(
      this,
      CATEGORIZATION_SETTINGS.IDLE_TIMEOUT_SECONDS
    );

    Services.obs.removeObserver(this, "quit-application");
    Services.obs.removeObserver(this, "wake_notification");

    this.#idleService = null;
    this.#init = false;
  }

  observe(subject, topic) {
    switch (topic) {
      case "idle":
        lazy.logConsole.debug("Triggering all callbacks due to idle.");
        this.#sendAllCallbacks();
        break;
      case "quit-application":
        this.uninit();
        break;
      case "wake_notification":
        if (
          this.#mostRecentMs &&
          Date.now() - this.#mostRecentMs >=
            CATEGORIZATION_SETTINGS.WAKE_TIMEOUT_MS
        ) {
          lazy.logConsole.debug(
            "Triggering all callbacks due to a wake notification."
          );
          this.#sendAllCallbacks();
        }
        break;
    }
  }

  addCallback(browser, callback) {
    lazy.logConsole.debug("Adding callback to queue.");
    this.#mostRecentMs = Date.now();
    this.#browserToCallbackMap?.set(browser, callback);
  }

  sendCallback(browser) {
    let callback = this.#browserToCallbackMap?.get(browser);
    if (callback) {
      lazy.logConsole.debug("Triggering callback.");
      callback();
      Services.obs.notifyObservers(
        null,
        "recorded-single-categorization-event"
      );
      this.#browserToCallbackMap.delete(browser);
    }
  }

  #sendAllCallbacks() {
    let browsers = ChromeUtils.nondeterministicGetWeakMapKeys(
      this.#browserToCallbackMap
    );
    if (browsers) {
      lazy.logConsole.debug("Triggering all callbacks.");
      for (let browser of browsers) {
        this.sendCallback(browser);
      }
    }
    this.#mostRecentMs = null;
    Services.obs.notifyObservers(null, "recorded-all-categorization-events");
  }
}

/**
 * Handles reporting SERP categorization telemetry to Glean.
 */
class CategorizationRecorder {
  #init = false;

  // The number of SERP categorizations that have been recorded but not yet
  // reported in a Glean ping.
  #serpCategorizationsCount = 0;

  // When the user started interacting with the SERP.
  #userInteractionStartTime = null;

  async init() {
    if (this.#init) {
      return;
    }

    Services.obs.addObserver(this, "user-interaction-active");
    Services.obs.addObserver(this, "user-interaction-inactive");
    this.#init = true;
    this.#serpCategorizationsCount = Services.prefs.getIntPref(
      "browser.search.serpMetricsRecordedCounter",
      0
    );
    Services.prefs.setIntPref("browser.search.serpMetricsRecordedCounter", 0);
    this.submitPing("startup");
    Services.obs.notifyObservers(null, "categorization-recorder-init");
  }

  uninit() {
    if (this.#init) {
      Services.obs.removeObserver(this, "user-interaction-active");
      Services.obs.removeObserver(this, "user-interaction-inactive");
      Services.prefs.setIntPref(
        "browser.search.serpMetricsRecordedCounter",
        this.#serpCategorizationsCount
      );

      this.#resetCategorizationRecorderData();
      this.#init = false;
    }
  }

  observe(subject, topic, _data) {
    switch (topic) {
      case "user-interaction-active": {
        // If the user is already active, we don't want to overwrite the start
        // time.
        if (this.#userInteractionStartTime == null) {
          this.#userInteractionStartTime = Date.now();
        }
        break;
      }
      case "user-interaction-inactive": {
        let currentTime = Date.now();
        let activityLimitInMs = lazy.activityLimit * 1000;
        if (
          this.#userInteractionStartTime &&
          currentTime - this.#userInteractionStartTime >= activityLimitInMs
        ) {
          this.submitPing("inactivity");
        }
        this.#userInteractionStartTime = null;
        break;
      }
    }
  }

  /**
   * Helper function for recording the SERP categorization event.
   *
   * @param {RecordCategorizationParameters} resultToReport
   *  The object containing all the data required to report.
   */
  recordCategorizationTelemetry(resultToReport) {
    lazy.logConsole.debug(
      "Reporting the following categorization result:",
      resultToReport
    );
    Glean.serp.categorization.record(resultToReport);

    this.#incrementCategorizationsCount();
  }

  /**
   * Helper function for recording Glean telemetry when issues with the
   * domain-to-categories map cause the categorization and impression not to be
   * recorded.
   */
  recordMissingImpressionTelemetry() {
    lazy.logConsole.debug(
      "Recording a missing impression due to an issue with the domain-to-categories map."
    );
    Glean.serp.categorizationNoMapFound.add();
    this.#incrementCategorizationsCount();
  }

  /**
   * Adds a Glean object metric to the custom SERP categorization ping if info
   * about a single experiment has been requested via Nimbus config.
   */
  maybeExtractAndRecordExperimentInfo() {
    let targetExperiment =
      lazy.NimbusFeatures.search.getVariable("targetExperiment");
    if (!targetExperiment) {
      lazy.logConsole.debug("No targetExperiment found.");
      return;
    }

    lazy.logConsole.debug("Found targetExperiment:", targetExperiment);

    let metadata = lazy.NimbusFeatures.search.getEnrollmentMetadata(
      lazy.EnrollmentType.EXPERIMENT
    );
    if (metadata?.slug !== targetExperiment) {
      metadata = lazy.NimbusFeatures.search.getEnrollmentMetadata(
        lazy.EnrollmentType.ROLLOUT
      );

      if (metadata?.slug !== targetExperiment) {
        lazy.logConsole.debug(
          "No experiment or rollout found that matches targetExperiment."
        );
        return;
      }
    }

    let experimentToRecord = {
      slug: metadata.slug,
      branch: metadata.branch,
    };
    lazy.logConsole.debug("Experiment data:", experimentToRecord);
    Glean.serp.experimentInfo.set(experimentToRecord);
  }

  submitPing(reason) {
    if (!this.#serpCategorizationsCount) {
      return;
    }

    // If experiment info has been requested via Nimbus config, we want to
    // record it just before submitting the ping.
    this.maybeExtractAndRecordExperimentInfo();
    lazy.logConsole.debug("Submitting SERP categorization ping:", reason);
    GleanPings.serpCategorization.submit(reason);

    this.#serpCategorizationsCount = 0;
  }

  /**
   * Tests are able to clear telemetry on demand. When that happens, we need to
   * ensure we're doing to the same here or else the internal count in tests
   * will be inaccurate.
   */
  testReset() {
    if (Cu.isInAutomation) {
      this.#resetCategorizationRecorderData();
    }
  }

  #incrementCategorizationsCount() {
    this.#serpCategorizationsCount++;

    if (
      this.#serpCategorizationsCount >=
      CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD
    ) {
      this.submitPing("threshold_reached");
    }
  }

  #resetCategorizationRecorderData() {
    this.#serpCategorizationsCount = 0;
    this.#userInteractionStartTime = null;
  }
}

/**
 * @typedef {object} DomainToCategoriesRecord
 * @property {boolean} isDefault
 *  Whether the record is a default if the user's region does not contain a
 *  more specific set of mappings.
 * @property {string[]} includeRegions
 *  The region codes to include. If left blank, it applies to all regions.
 * @property {string[]} excludeRegions
 *  The region codes to exclude.
 * @property {number} version
 *  The version of the record.
 */

/**
 * @typedef {object} DomainCategoryScore
 * @property {number} category
 *  The index of the category.
 * @property {number} score
 *  The score associated with the category.
 */

/**
 * Maps domain to categories. Data is downloaded from Remote Settings and
 * stored inside DomainToCategoriesStore.
 */
class DomainToCategoriesMap {
  /**
   * Latest version number of the attachments.
   *
   * @type {number | null}
   */
  #version = null;

  /**
   * The Remote Settings client.
   *
   * @type {object | null}
   */
  #client = null;

  /**
   * Whether this is synced with Remote Settings.
   *
   * @type {boolean}
   */
  #init = false;

  /**
   * Callback when Remote Settings syncs.
   *
   * @type {Function | null}
   */
  #onSettingsSync = null;

  /**
   * When downloading an attachment from Remote Settings fails, this will
   * contain a timer which will eventually attempt to retry downloading
   * attachments.
   */
  #downloadTimer = null;

  /**
   * Number of times this has attempted to try another download. Will reset
   * if the categorization preference has been toggled, or a sync event has
   * been detected.
   *
   * @type {number}
   */
  #downloadRetries = 0;

  /**
   * A reference to the data store.
   *
   * @type {DomainToCategoriesStore | null}
   */
  #store = null;

  /**
   * Runs at application startup with startup idle tasks. If the SERP
   * categorization preference is enabled, it creates a Remote Settings
   * client to listen to updates, and populates the store.
   */
  async init() {
    if (this.#init) {
      return;
    }
    lazy.logConsole.debug("Initializing domain-to-categories map.");

    // Set early to allow un-init from an initialization.
    this.#init = true;

    try {
      await this.#setupClientAndStore();
    } catch (ex) {
      lazy.logConsole.error(ex);
      await this.uninit();
      return;
    }

    // If we don't have a client and store, it likely means an un-init process
    // started during the initialization process.
    if (this.#client && this.#store) {
      lazy.logConsole.debug("Initialized domain-to-categories map.");
      Services.obs.notifyObservers(null, "domain-to-categories-map-init");
    }
  }

  async uninit(shouldDeleteStore) {
    if (this.#init) {
      lazy.logConsole.debug("Un-initializing domain-to-categories map.");
      this.#clearClient();
      this.#cancelAndNullifyTimer();

      if (this.#store) {
        if (shouldDeleteStore) {
          try {
            await this.#store.dropData();
          } catch (ex) {
            lazy.logConsole.error(ex);
          }
        }
        await this.#store.uninit();
        this.#store = null;
      }

      lazy.logConsole.debug("Un-initialized domain-to-categories map.");
      this.#init = false;
      Services.obs.notifyObservers(null, "domain-to-categories-map-uninit");
    }
  }

  /**
   * Given a domain, find categories and relevant scores.
   *
   * @param {string} domain Domain to lookup.
   * @returns {Promise<DomainCategoryScore[]>}
   *  An array containing categories and their respective score. If no record
   *  for the domain is available, return an empty array.
   */
  async get(domain) {
    if (!this.#store || this.#store.empty || !this.#store.ready) {
      return [];
    }
    lazy.gCryptoHash.init(lazy.gCryptoHash.SHA256);
    let bytes = new TextEncoder().encode(domain);
    lazy.gCryptoHash.update(bytes, bytes.length);
    let hash = lazy.gCryptoHash.finish(true);
    let rawValues = await this.#store.getCategories(hash);
    if (rawValues?.length) {
      let output = [];
      // Transform data into a more readable format.
      // [x, y] => { category: x, score: y }
      for (let i = 0; i < rawValues.length; i += 2) {
        output.push({ category: rawValues[i], score: rawValues[i + 1] });
      }
      return output;
    }
    return [];
  }

  /**
   * If the map was initialized, returns the version number for the data.
   * The version number is determined by the record with the highest version
   * number. Even if the records have different versions, only records from the
   * latest version should be available. Returns null if the map was not
   * initialized.
   *
   * @returns {null | number} The version number.
   */
  get version() {
    return this.#version;
  }

  /**
   * Whether the store is empty of data.
   *
   * @returns {boolean}
   */
  get empty() {
    if (!this.#store) {
      return true;
    }
    return this.#store.empty;
  }

  /**
   * Unit test-only function, used to override the domainToCategoriesMap so
   * that tests can set it to easy to test values.
   *
   * @param {object} domainToCategoriesMap
   *   An object where the key is a hashed domain and the value is an array
   *   containing an arbitrary number of DomainCategoryScores.
   * @param {number} version
   *   The version number for the store.
   * @param {boolean} isDefault
   *   Whether the records should be considered default.
   */
  async overrideMapForTests(
    domainToCategoriesMap,
    version = 1,
    isDefault = false
  ) {
    if (Cu.isInAutomation || Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
      await this.#store.init();
      await this.#store.dropData();
      await this.#store.insertObject(domainToCategoriesMap, version, isDefault);
    }
  }

  /**
   * Given a list of records from Remote Settings, determine which ones should
   * be matched based on the region.
   *
   * - If a set of records match the region, they should be derived from one
   *   source JSON file. The reason why it is split up is to make it less
   *   onerous to download and parse, though testing might find a single
   *   file to be sufficient.
   * - If more than one set of records match the region, it would be from one
   *   set of records belonging to default mappings that apply to many regions.
   *   The more specific collection should override the default set.
   *
   * @param {DomainToCategoriesRecord[]} records
   *   The records from Remote Settings.
   * @param {string|null} region
   *   The region to match.
   * @returns {object|null}
   */
  findRecordsForRegion(records, region) {
    if (!region || !records?.length) {
      return null;
    }

    let regionSpecificRecords = [];
    let defaultRecords = [];
    for (let record of records) {
      if (this.recordMatchesRegion(record, region)) {
        if (record.isDefault) {
          defaultRecords.push(record);
        } else {
          regionSpecificRecords.push(record);
        }
      }
    }

    if (regionSpecificRecords.length) {
      return { records: regionSpecificRecords, isDefault: false };
    }

    if (defaultRecords.length) {
      return { records: defaultRecords, isDefault: true };
    }

    return null;
  }

  /**
   * Checks the record matches the region.
   *
   * @param {DomainToCategoriesRecord} record
   *   The record to check.
   * @param {string|null} region
   *   The region the record to be matched against.
   * @returns {boolean}
   */
  recordMatchesRegion(record, region) {
    if (!region || !record) {
      return false;
    }

    if (record.excludeRegions?.includes(region)) {
      return false;
    }

    if (record.isDefault) {
      return true;
    }

    if (!record.includeRegions?.includes(region)) {
      return false;
    }

    return true;
  }

  async syncMayModifyStore(syncData, region) {
    if (!syncData || !region) {
      return false;
    }

    let currentResult = this.findRecordsForRegion(syncData?.current, region);
    if (this.#store.empty && !currentResult) {
      lazy.logConsole.debug("Store was empty and there were no results.");
      return false;
    }

    if (!this.#store.empty && !currentResult) {
      return true;
    }

    let storeHasDefault = await this.#store.isDefault();
    if (storeHasDefault != currentResult.isDefault) {
      return true;
    }

    const recordsDifferFromStore = records => {
      let result = this.findRecordsForRegion(records, region);
      return result?.records.length && storeHasDefault == result.isDefault;
    };

    if (
      recordsDifferFromStore(syncData.created) ||
      recordsDifferFromStore(syncData.deleted) ||
      recordsDifferFromStore(syncData.updated.map(obj => obj.new))
    ) {
      return true;
    }

    return false;
  }

  /**
   * Connect with Remote Settings and retrieve the records associated with
   * categorization. Then, check if the records match the store version. If
   * no records exist, return early. If records exist but the version stored
   * on the records differ from the store version, then attempt to
   * empty the store and fill it with data from downloaded attachments. Only
   * reuse the store if the version in each record matches the store.
   */
  async #setupClientAndStore() {
    if (this.#client && !this.empty) {
      return;
    }
    lazy.logConsole.debug("Setting up domain-to-categories map.");
    this.#client = lazy.RemoteSettings(TELEMETRY_CATEGORIZATION_KEY);

    this.#onSettingsSync = event => this.#sync(event.data);
    this.#client.on("sync", this.#onSettingsSync);

    this.#store = new DomainToCategoriesStore();
    await this.#store.init();

    let records = await this.#client.get();
    // Even though records don't exist, we still consider the store initialized
    // since a sync event from Remote Settings could populate the store with
    // records eligible for the client to download.
    if (!records.length) {
      lazy.logConsole.debug("No records found for domain-to-categories map.");
      return;
    }

    // At least one of the records must be eligible for the region.
    let result = this.findRecordsForRegion(records, lazy.Region.home);
    let matchingRecords = result?.records;
    let matchingRecordsAreDefault = result?.isDefault;
    let hasMatchingRecords = !!matchingRecords?.length;
    Services.prefs.setBoolPref(CATEGORIZATION_REGION_PREF, hasMatchingRecords);

    if (!hasMatchingRecords) {
      lazy.logConsole.debug(
        "No domain-to-category records match the current region:",
        lazy.Region.home
      );
      // If no matching record was found but the store is not empty,
      // the user changed their home region.
      if (!this.#store.empty) {
        lazy.logConsole.debug(
          "Drop store because it no longer matches the home region."
        );
        await this.#store.dropData();
      }
      return;
    }

    this.#version = this.#retrieveLatestVersion(matchingRecords);
    let storeVersion = await this.#store.getVersion();
    let storeIsDefault = await this.#store.isDefault();
    if (
      storeVersion == this.#version &&
      !this.#store.empty &&
      storeIsDefault == matchingRecordsAreDefault
    ) {
      lazy.logConsole.debug("Reuse existing domain-to-categories map.");
      Services.obs.notifyObservers(
        null,
        "domain-to-categories-map-update-complete"
      );
      return;
    }

    await this.#clearAndPopulateStore(records);
  }

  #clearClient() {
    if (this.#client) {
      lazy.logConsole.debug("Removing Remote Settings client.");
      this.#client.off("sync", this.#onSettingsSync);
      this.#client = null;
      this.#onSettingsSync = null;
      this.#downloadRetries = 0;
    }
  }

  /**
   * Inspects a list of records from the categorization domain bucket and finds
   * the maximum version score from the set of records. Each record should have
   * the same version number but if for any reason one entry has a lower
   * version number, the latest version can be used to filter it out.
   *
   * @param {DomainToCategoriesRecord[]} records
   *   An array containing the records from a Remote Settings collection.
   * @returns {number}
   */
  #retrieveLatestVersion(records) {
    return records.reduce((version, record) => {
      if (record.version > version) {
        return record.version;
      }
      return version;
    }, 0);
  }

  /**
   * Callback when Remote Settings has indicated the collection has been
   * synced. Determine if the records changed should result in updating the map,
   * as some of the records changed might not affect the user's region.
   * Additionally, delete any attachment for records that no longer exist.
   *
   * @param {object} data
   *  Object containing records that are current, deleted, created, or updated.
   */
  async #sync(data) {
    lazy.logConsole.debug("Syncing domain-to-categories with Remote Settings.");

    // Remove local files of deleted records.
    let toDelete = data?.deleted.filter(d => d.attachment);
    await Promise.all(
      toDelete.map(record => this.#client.attachments.deleteDownloaded(record))
    );

    let couldModify = await this.syncMayModifyStore(data, lazy.Region.home);
    if (!couldModify) {
      lazy.logConsole.debug(
        "Domain-to-category records had no changes that matched the region."
      );
      return;
    }

    this.#downloadRetries = 0;

    try {
      await this.#clearAndPopulateStore(data?.current);
    } catch (ex) {
      lazy.logConsole.error("Error populating map: ", ex);
      await this.uninit();
    }
  }

  /**
   * Clear the existing store and populate it with attachments found in the
   * records. If no attachments are found, or no record containing an
   * attachment contained the latest version, then nothing will change.
   *
   * @param {DomainToCategoriesRecord[]} records
   *  The records containing attachments.
   * @throws {Error}
   *  Will throw if it was not able to drop the store data, or it was unable
   *  to insert data into the store.
   */
  async #clearAndPopulateStore(records) {
    // If we don't have a handle to a store, it would mean that it was removed
    // during an uninitialization process.
    if (!this.#store) {
      lazy.logConsole.debug(
        "Could not populate store because no store was available."
      );
      return;
    }

    if (!this.#store.ready) {
      lazy.logConsole.debug(
        "Could not populate store because it was not ready."
      );
      return;
    }

    // Empty table so that if there are errors in the download process, callers
    // querying the map won't use information we know is probably outdated.
    await this.#store.dropData();

    this.#version = null;
    this.#cancelAndNullifyTimer();

    let result = this.findRecordsForRegion(records, lazy.Region.home);
    let recordsMatchingRegion = result?.records;
    let isDefault = result?.isDefault;
    let hasMatchingRecords = !!recordsMatchingRegion?.length;
    Services.prefs.setBoolPref(CATEGORIZATION_REGION_PREF, hasMatchingRecords);

    // A collection with no records is still a valid init state.
    if (!records?.length) {
      lazy.logConsole.debug("No records found for domain-to-categories map.");
      return;
    }

    if (!hasMatchingRecords) {
      lazy.logConsole.debug(
        "No domain-to-category records match the current region:",
        lazy.Region.home
      );
      return;
    }

    let fileContents = [];
    let start = ChromeUtils.now();
    for (let record of recordsMatchingRegion) {
      let fetchedAttachment;
      // Downloading attachments can fail.
      try {
        fetchedAttachment = await this.#client.attachments.download(record);
      } catch (ex) {
        lazy.logConsole.error("Could not download file:", ex);
        this.#createTimerToPopulateMap();
        return;
      }
      fileContents.push(fetchedAttachment.buffer);
    }
    ChromeUtils.addProfilerMarker(
      "SERPCategorization.#clearAndPopulateStore",
      start,
      "Download attachments."
    );

    this.#version = this.#retrieveLatestVersion(recordsMatchingRegion);
    if (!this.#version) {
      lazy.logConsole.debug("Could not find a version number for any record.");
      return;
    }

    await this.#store.insertFileContents(
      fileContents,
      this.#version,
      isDefault
    );

    lazy.logConsole.debug("Finished updating domain-to-categories store.");
    Services.obs.notifyObservers(
      null,
      "domain-to-categories-map-update-complete"
    );
  }

  #cancelAndNullifyTimer() {
    if (this.#downloadTimer) {
      lazy.logConsole.debug("Cancel and nullify download timer.");
      this.#downloadTimer.cancel();
      this.#downloadTimer = null;
    }
  }

  #createTimerToPopulateMap() {
    if (
      this.#downloadRetries >=
        TELEMETRY_CATEGORIZATION_DOWNLOAD_SETTINGS.maxTriesPerSession ||
      !this.#client
    ) {
      return;
    }
    if (!this.#downloadTimer) {
      this.#downloadTimer = Cc["@mozilla.org/timer;1"].createInstance(
        Ci.nsITimer
      );
    }
    lazy.logConsole.debug("Create timer to retry downloading attachments.");
    let delay =
      TELEMETRY_CATEGORIZATION_DOWNLOAD_SETTINGS.base +
      randomInteger(
        TELEMETRY_CATEGORIZATION_DOWNLOAD_SETTINGS.minAdjust,
        TELEMETRY_CATEGORIZATION_DOWNLOAD_SETTINGS.maxAdjust
      );
    this.#downloadTimer.initWithCallback(
      async () => {
        this.#downloadRetries += 1;
        let records = await this.#client.get();
        try {
          await this.#clearAndPopulateStore(records);
        } catch (ex) {
          lazy.logConsole.error("Error populating store: ", ex);
          await this.uninit();
        }
      },
      delay,
      Ci.nsITimer.TYPE_ONE_SHOT
    );
  }
}

/**
 * Handles the storage of data containing domains to categories.
 */
export class DomainToCategoriesStore {
  #init = false;

  /**
   * The connection to the store.
   *
   * @type {object | null}
   */
  #connection = null;

  /**
   * Reference for the shutdown blocker in case we need to remove it before
   * shutdown.
   *
   * @type {Function | null}
   */
  #asyncShutdownBlocker = null;

  /**
   * Whether the store is empty of data.
   *
   * @type {boolean}
   */
  #empty = true;

  /**
   * For a particular subset of errors, we'll attempt to rebuild the database
   * from scratch.
   */
  #rebuildableErrors = ["NS_ERROR_FILE_CORRUPTED"];

  /**
   * Initializes the store. If the store is initialized it should have cached
   * a connection to the store and ensured the store exists.
   */
  async init() {
    if (this.#init) {
      return;
    }
    lazy.logConsole.debug("Initializing domain-to-categories store.");

    // Attempts to cache a connection to the store.
    // If a failure occured, try to re-build the store.
    let rebuiltStore = false;
    try {
      await this.#initConnection();
    } catch (ex1) {
      lazy.logConsole.error(`Error initializing a connection: ${ex1}`);
      if (this.#rebuildableErrors.includes(ex1.name)) {
        try {
          await this.#rebuildStore();
        } catch (ex2) {
          await this.#closeConnection();
          lazy.logConsole.error(`Could not rebuild store: ${ex2}`);
          return;
        }
        rebuiltStore = true;
      }
    }

    // If we don't have a connection, bail because the browser could be
    // shutting down ASAP, or re-creating the store is impossible.
    if (!this.#connection) {
      lazy.logConsole.debug(
        "Bailing from DomainToCategoriesStore.init because connection doesn't exist."
      );
      return;
    }

    // If we weren't forced to re-build the store, we only have the connection.
    // We want to ensure the store exists so calls to public methods can pass
    // without throwing errors due to the absence of the store.
    if (!rebuiltStore) {
      try {
        await this.#initSchema();
      } catch (ex) {
        lazy.logConsole.error(`Error trying to create store: ${ex}`);
        await this.#closeConnection();
        return;
      }
    }

    lazy.logConsole.debug("Initialized domain-to-categories store.");
    this.#init = true;
  }

  async uninit() {
    if (this.#init) {
      lazy.logConsole.debug("Un-initializing domain-to-categories store.");
      await this.#closeConnection();
      lazy.logConsole.debug("Un-initialized domain-to-categories store.");
    }
  }

  /**
   * Whether the store has an open connection to the physical store.
   *
   * @returns {boolean}
   */
  get ready() {
    return this.#init;
  }

  /**
   * Whether the store is devoid of data.
   *
   * @returns {boolean}
   */
  get empty() {
    return this.#empty;
  }

  /**
   * Clears information in the store. If dropping data encountered a failure,
   * try to delete the file containing the store and re-create it.
   *
   * @throws {Error} Will throw if it was unable to clear information from the
   * store.
   */
  async dropData() {
    if (!this.#connection) {
      return;
    }
    let tableExists = await this.#connection.tableExists(
      CATEGORIZATION_SETTINGS.STORE_NAME
    );
    if (tableExists) {
      lazy.logConsole.debug("Drop domain_to_categories.");
      // This can fail if the permissions of the store are read-only.
      await this.#connection.executeTransaction(async () => {
        await this.#connection.execute(`DROP TABLE domain_to_categories`);
        const createDomainToCategoriesTable = `
            CREATE TABLE IF NOT EXISTS
              domain_to_categories (
                string_id
                  TEXT PRIMARY KEY NOT NULL,
                categories
                  TEXT
              ) WITHOUT ROWID;
            `;
        await this.#connection.execute(createDomainToCategoriesTable);
        await this.#connection.execute(`DELETE FROM moz_meta`);
        await this.#connection.executeCached(
          `
              INSERT INTO
                moz_meta (key, value)
              VALUES
                (:key, :value)
              ON CONFLICT DO UPDATE SET
                value = :value
            `,
          { key: "version", value: 0 }
        );
      });

      this.#empty = true;
    }
  }

  /**
   * Given file contents, try moving them into the store. If a failure occurs,
   * it will attempt to drop existing data to ensure callers aren't accessing
   * a partially filled store.
   *
   * @param {ArrayBufferLike[]} fileContents
   *   Contents to convert.
   * @param {number} version
   *   The version for the store.
   * @param {boolean} isDefault
   *   Whether the file contents are from a default collection.
   * @throws {Error}
   *   Will throw if the insertion failed and dropData was unable to run
   *   successfully.
   */
  async insertFileContents(fileContents, version, isDefault = false) {
    if (!this.#init || !fileContents?.length || !version) {
      return;
    }

    try {
      await this.#insert(fileContents, version, isDefault);
    } catch (ex) {
      lazy.logConsole.error(`Could not insert file contents: ${ex}`);
      await this.dropData();
    }
  }

  /**
   * Convenience function to make it trivial to insert Javascript objects into
   * the store. This avoids having to set up the collection in Remote Settings.
   *
   * @param {object} domainToCategoriesMap
   *   An object whose keys should be hashed domains with values containing
   *   an array of integers.
   * @param {number} version
   *   The version for the store.
   * @param {boolean} isDefault
   *   Whether the mappings are from a default record.
   * @returns {Promise<boolean>}
   *   Whether the operation was successful.
   */
  async insertObject(domainToCategoriesMap, version, isDefault) {
    if (!Cu.isInAutomation || !this.#init) {
      return false;
    }
    let buffer = new TextEncoder().encode(
      JSON.stringify(domainToCategoriesMap)
    ).buffer;
    await this.insertFileContents([buffer], version, isDefault);
    return true;
  }

  /**
   * Retrieves domains mapped to the key.
   *
   * @param {string} key
   *   The value to lookup in the store.
   * @returns {Promise<number[]>}
   *   An array of numbers corresponding to the category and score. If the key
   *   does not exist in the store or the store is having issues retrieving the
   *   value, returns an empty array.
   */
  async getCategories(key) {
    if (!this.#init) {
      return [];
    }

    let rows;
    try {
      rows = await this.#connection.executeCached(
        `
        SELECT
          categories
        FROM
          domain_to_categories
        WHERE
          string_id = :key
      `,
        {
          key,
        }
      );
    } catch (ex) {
      lazy.logConsole.error(`Could not retrieve from the store: ${ex}`);
      return [];
    }

    if (!rows.length) {
      return [];
    }
    return JSON.parse(rows[0].getResultByName("categories")) ?? [];
  }

  /**
   * Retrieves the version number of the store.
   *
   * @returns {Promise<number>}
   *   The version number. Returns 0 if the version was never set or if there
   *   was an issue accessing the version number.
   */
  async getVersion() {
    if (this.#connection) {
      let rows;
      try {
        rows = await this.#connection.executeCached(
          `
          SELECT
            value
          FROM
            moz_meta
          WHERE
            key = "version"
          `
        );
      } catch (ex) {
        lazy.logConsole.error(`Could not retrieve version of the store: ${ex}`);
        return 0;
      }
      if (rows.length) {
        return parseInt(rows[0].getResultByName("value")) ?? 0;
      }
    }
    return 0;
  }

  /**
   * Whether the data inside the store was derived from a default set of
   * records.
   *
   * @returns {Promise<boolean>}
   */
  async isDefault() {
    if (this.#connection) {
      let rows;
      try {
        rows = await this.#connection.executeCached(
          `
          SELECT
            value
          FROM
            moz_meta
          WHERE
            key = "is_default"
          `
        );
      } catch (ex) {
        lazy.logConsole.error(
          `Could not retrieve if the store is using default records: ${ex}`
        );
        return false;
      }
      if (rows.length && parseInt(rows[0].getResultByName("value")) == 1) {
        return true;
      }
    }
    return false;
  }

  /**
   * Test only function allowing tests to delete the store.
   */
  async testDelete() {
    if (Cu.isInAutomation) {
      await this.#closeConnection();
      await this.#delete();
    }
  }

  /**
   * If a connection is available, close it and remove shutdown blockers.
   */
  async #closeConnection() {
    this.#init = false;
    this.#empty = true;

    if (this.#connection) {
      lazy.logConsole.debug("Closing connection.");
      // An error could occur while closing the connection. We suppress the
      // error since it is not a critical part of the browser.
      try {
        await this.#connection.close();
      } catch (ex) {
        lazy.logConsole.error(ex);
      }
      this.#connection = null;
    }

    if (this.#asyncShutdownBlocker) {
      lazy.Sqlite.shutdown.removeBlocker(this.#asyncShutdownBlocker);
      this.#asyncShutdownBlocker = null;
    }
  }

  /**
   * Initialize the schema for the store.
   *
   * @throws {Error}
   *   Will throw if a permissions error prevents creating the store.
   */
  async #initSchema() {
    if (!this.#connection) {
      return;
    }
    lazy.logConsole.debug("Create store.");
    // Creation can fail if the store is read only.
    await this.#connection.executeTransaction(async () => {
      // Let outer try block handle the exception.
      const createDomainToCategoriesTable = `
          CREATE TABLE IF NOT EXISTS
            domain_to_categories (
              string_id
                TEXT PRIMARY KEY NOT NULL,
              categories
                TEXT
            ) WITHOUT ROWID;
        `;
      await this.#connection.execute(createDomainToCategoriesTable);
      const createMetaTable = `
          CREATE TABLE IF NOT EXISTS
            moz_meta (
              key
                TEXT PRIMARY KEY NOT NULL,
              value
                INTEGER
            ) WITHOUT ROWID;
          `;
      await this.#connection.execute(createMetaTable);
      await this.#connection.setSchemaVersion(
        CATEGORIZATION_SETTINGS.STORE_SCHEMA
      );
    });

    let rows = await this.#connection.executeCached(
      "SELECT count(*) = 0 FROM domain_to_categories"
    );
    this.#empty = !!rows[0].getResultByIndex(0);
  }

  /**
   * Attempt to delete the store.
   *
   * @throws {Error}
   *   Will throw if the permissions for the file prevent its deletion.
   */
  async #delete() {
    lazy.logConsole.debug("Attempt to delete the store.");
    try {
      await IOUtils.remove(
        PathUtils.join(
          PathUtils.profileDir,
          CATEGORIZATION_SETTINGS.STORE_FILE
        ),
        { ignoreAbsent: true }
      );
    } catch (ex) {
      lazy.logConsole.error(ex);
    }
    this.#empty = true;
    lazy.logConsole.debug("Store was deleted.");
  }

  /**
   * Tries to establish a connection to the store.
   *
   * @throws {Error}
   *   Will throw if there was an issue establishing a connection or adding
   *   adding a shutdown blocker.
   */
  async #initConnection() {
    if (this.#connection) {
      return;
    }

    // This could fail if the store is corrupted.
    this.#connection = await lazy.Sqlite.openConnection({
      path: PathUtils.join(
        PathUtils.profileDir,
        CATEGORIZATION_SETTINGS.STORE_FILE
      ),
    });

    await this.#connection.execute("PRAGMA journal_mode = TRUNCATE");

    this.#asyncShutdownBlocker = async () => {
      await this.#connection.close();
      this.#connection = null;
    };

    // This could fail if we're adding it during shutdown. In this case,
    // don't throw but close the connection.
    try {
      lazy.Sqlite.shutdown.addBlocker(
        "SERPCategorization:DomainToCategoriesSqlite closing",
        this.#asyncShutdownBlocker
      );
    } catch (ex) {
      lazy.logConsole.error(ex);
      await this.#closeConnection();
    }
  }

  /**
   * Inserts into the store.
   *
   * @param {ArrayBufferLike[]} fileContents
   *   The data that should be converted and inserted into the store.
   * @param {number} version
   *   The version number that should be inserted into the store.
   * @param {boolean} isDefault
   *   Whether the file contents are a default set of records.
   * @throws {Error}
   *   Will throw if a connection is not present, if the store is not
   *   able to be updated (permissions error, corrupted file), or there is
   *   something wrong with the file contents.
   */
  async #insert(fileContents, version, isDefault) {
    let start = ChromeUtils.now();
    await this.#connection.executeTransaction(async () => {
      lazy.logConsole.debug("Insert into domain_to_categories table.");
      for (let fileContent of fileContents) {
        await this.#connection.executeCached(
          `
            INSERT INTO
              domain_to_categories (string_id, categories)
            SELECT
              json_each.key AS string_id,
              json_each.value AS categories
            FROM
              json_each(json(:obj))
          `,
          {
            obj: new TextDecoder().decode(fileContent),
          }
        );
      }
      // Once the insertions have successfully completed, update the version.
      await this.#connection.executeCached(
        `
          INSERT INTO
            moz_meta (key, value)
          VALUES
            (:key, :value)
          ON CONFLICT DO UPDATE SET
            value = :value
        `,
        { key: "version", value: version }
      );
      if (isDefault) {
        await this.#connection.executeCached(
          `
          INSERT INTO
            moz_meta (key, value)
          VALUES
            (:key, :value)
          ON CONFLICT DO UPDATE SET
            value = :value
        `,
          { key: "is_default", value: 1 }
        );
      }
    });
    ChromeUtils.addProfilerMarker(
      "DomainToCategoriesSqlite.#insert",
      start,
      "Move file contents into table."
    );

    if (fileContents?.length) {
      this.#empty = false;
    }
  }

  /**
   * Deletes and re-build's the store. Used in cases where we encounter a
   * failure and we want to try fixing the error by starting with an
   * entirely fresh store.
   *
   * @throws {Error}
   *   Will throw if a connection could not be established, if it was
   *   unable to delete the store, or it was unable to build a new store.
   */
  async #rebuildStore() {
    lazy.logConsole.debug("Try rebuilding store.");
    // Step 1. Close all connections.
    await this.#closeConnection();

    // Step 2. Delete the existing store.
    await this.#delete();

    // Step 3. Re-establish the connection.
    await this.#initConnection();

    // Step 4. If a connection exists, try creating the store.
    await this.#initSchema();
  }
}

function randomInteger(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

export var SERPDomainToCategoriesMap = new DomainToCategoriesMap();
export var SERPCategorization = new Categorizer();
export var SERPCategorizationRecorder = new CategorizationRecorder();
export var SERPCategorizationEventScheduler =
  new CategorizationEventScheduler();
