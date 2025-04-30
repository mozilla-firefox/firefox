/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  getFrecentRecentCombinedUrls:
    "resource://gre/modules/contentrelevancy/private/InputUtils.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  Interest: "resource://gre/modules/RustRelevancy.sys.mjs",
  InterestVector: "resource://gre/modules/RustRelevancy.sys.mjs",
  RelevancyStore: "resource://gre/modules/RustRelevancy.sys.mjs",
  RemoteSettingsConfig2: "resource://gre/modules/RustRemoteSettings.sys.mjs",
  RemoteSettingsService: "resource://gre/modules/RustRemoteSettings.sys.mjs",
  score: "resource://gre/modules/RustRelevancy.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "timerManager",
  "@mozilla.org/updates/timer-manager;1",
  "nsIUpdateTimerManager"
);

// Constants used by `nsIUpdateTimerManager` for a cross-session timer.
const TIMER_ID = "content-relevancy-timer";
const PREF_TIMER_LAST_UPDATE = `app.update.lastUpdateTime.${TIMER_ID}`;
const PREF_TIMER_INTERVAL = "toolkit.contentRelevancy.timerInterval";
const PREF_LOG_ENABLED = "toolkit.contentRelevancy.log";
// Set the timer interval to 1 day for validation.
const DEFAULT_TIMER_INTERVAL_SECONDS = 1 * 24 * 60 * 60;

// Default maximum input URLs to fetch from Places.
const DEFAULT_MAX_URLS = 100;
// Default minimal input URLs for clasification.
const DEFAULT_MIN_URLS = 0;

// File name of the relevancy database
const RELEVANCY_STORE_FILENAME = "content-relevancy.sqlite";

// Nimbus variables
const NIMBUS_VARIABLE_ENABLED = "enabled";
const NIMBUS_VARIABLE_MAX_INPUT_URLS = "maxInputUrls";
const NIMBUS_VARIABLE_MIN_INPUT_URLS = "minInputUrls";
const NIMBUS_VARIABLE_TIMER_INTERVAL = "timerInterval";
const NIMBUS_VARIABLE_INGEST_ENABLED = "ingestEnabled";

// A map used to align the UniFFI Interest enum with SERP categories.
ChromeUtils.defineLazyGetter(lazy, "SerpCategoriesToInterestEnum", () => {
  return new Map([
    [0, lazy.Interest.INCONCLUSIVE],
    [1, lazy.Interest.ANIMALS],
    [2, lazy.Interest.ARTS],
    [3, lazy.Interest.AUTOS],
    [4, lazy.Interest.BUSINESS],
    [5, lazy.Interest.CAREER],
    [6, lazy.Interest.EDUCATION],
    [7, lazy.Interest.FASHION],
    [8, lazy.Interest.FINANCE],
    [9, lazy.Interest.FOOD],
    [10, lazy.Interest.GOVERNMENT],
    [12, lazy.Interest.HOBBIES],
    [13, lazy.Interest.HOME],
    [14, lazy.Interest.NEWS],
    [15, lazy.Interest.REALESTATE],
    [16, lazy.Interest.SOCIETY],
    [17, lazy.Interest.SPORTS],
    [18, lazy.Interest.TECH],
    [19, lazy.Interest.TRAVEL],
  ]);
});

// Setup the `lazy.log` object.  This is called on startup and also whenever `PREF_LOG_ENABLED`
// changes.
function setupLogging() {
  ChromeUtils.defineLazyGetter(lazy, "log", () => {
    return console.createInstance({
      prefix: "ContentRelevancyManager",
      maxLogLevel: Services.prefs.getBoolPref(PREF_LOG_ENABLED, false)
        ? "Debug"
        : "Error",
    });
  });
}

setupLogging();

class RelevancyManager {
  get TIMER_ID() {
    return TIMER_ID;
  }

  get initialized() {
    return this.#initialized;
  }

  /**
   * Init the manager. An update timer is registered if the feature is enabled.
   * The pref observer is always set so we can toggle the feature without restarting
   * the browser.
   *
   * Note that this should be called once only. `#enable` and `#disable` can be
   * used to toggle the feature once the manager is initialized.
   */
  init(rustRelevancyStore = lazy.RelevancyStore) {
    if (this.initialized) {
      return;
    }

    lazy.log.info("Initializing the manager");

    this.#storeManager = new RustRelevancyStoreManager(
      this.#storePath,
      rustRelevancyStore
    );
    if (this.shouldEnable) {
      this.#enable();
    }

    Services.prefs.addObserver(PREF_LOG_ENABLED, this);
    this._nimbusUpdateCallback = this.#onNimbusUpdate.bind(this);
    // This will handle both Nimbus updates and pref changes.
    lazy.NimbusFeatures.contentRelevancy.onUpdate(this._nimbusUpdateCallback);
    this.#initialized = true;
  }

  uninit() {
    if (!this.initialized) {
      return;
    }

    lazy.log.info("Uninitializing the manager");

    lazy.NimbusFeatures.contentRelevancy.offUpdate(this._nimbusUpdateCallback);
    Services.prefs.removeObserver(PREF_LOG_ENABLED, this);
    this.#disable();
    this.#storeManager = null;

    this.#initialized = false;
    this.#interestVector = null;
  }

  /**
   * Determine whether the feature should be enabled based on prefs and Nimbus.
   */
  get shouldEnable() {
    return (
      lazy.NimbusFeatures.contentRelevancy.getVariable(
        NIMBUS_VARIABLE_ENABLED
      ) ?? false
    );
  }

  /**
   * Whether or not the manager is initialized and enabled.
   */
  get enabled() {
    return this.initialized && this.#storeManager.enabled;
  }

  #startUpTimer() {
    // Log the last timer tick for debugging.
    const lastTick = Services.prefs.getIntPref(PREF_TIMER_LAST_UPDATE, 0);
    if (lastTick) {
      lazy.log.debug(
        `Last timer tick: ${lastTick}s (${
          Math.round(Date.now() / 1000) - lastTick
        })s ago`
      );
    } else {
      lazy.log.debug("Last timer tick: none");
    }

    const interval =
      lazy.NimbusFeatures.contentRelevancy.getVariable(
        NIMBUS_VARIABLE_TIMER_INTERVAL
      ) ??
      Services.prefs.getIntPref(
        PREF_TIMER_INTERVAL,
        DEFAULT_TIMER_INTERVAL_SECONDS
      );
    lazy.timerManager.registerTimer(
      TIMER_ID,
      this,
      interval,
      interval != 0 // Do not skip the first timer tick for a zero interval for testing
    );
  }

  get #storePath() {
    return PathUtils.join(
      Services.dirsvc.get("ProfLD", Ci.nsIFile).path,
      RELEVANCY_STORE_FILENAME
    );
  }

  #enable() {
    this.#storeManager.enable();
    this._shutdownBlocker = () => this.interrupt();
    // Interrupt sooner prior to the `profile-before-change` phase to allow
    // all the in-progress IOs to exit.
    lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
      "ContentRelevancyManager: Interrupt IO operations on relevancy store",
      this._shutdownBlocker
    );
    this.#startUpTimer();
  }

  /**
   * The reciprocal of `#enable()`, ensure this is safe to call when you add
   * new disabling code here. It should be so even if `#enable()` hasn't been
   * called.
   */
  #disable() {
    if (this._shutdownBlocker) {
      lazy.AsyncShutdown.profileChangeTeardown.removeBlocker(
        this._shutdownBlocker
      );
      this._shutdownBlocker = null;
    }
    lazy.timerManager.unregisterTimer(TIMER_ID);
    this.#storeManager.disable();
  }

  #toggleFeature() {
    if (this.shouldEnable) {
      this.#enable();
    } else {
      this.#disable();
    }
  }

  /**
   * nsITimerCallback
   */
  notify() {
    lazy.log.info("Background job timer fired");
    this.#doClassification();
  }

  get isInProgress() {
    return this.#isInProgress;
  }

  /**
   * Perform classification based on browsing history.
   *
   * It will fetch up to `DEFAULT_MAX_URLS` (or the corresponding Nimbus value)
   * URLs from top frecent URLs and use most recent URLs as a fallback if the
   * former is insufficient. The returned URLs might be fewer than requested.
   *
   * The classification will not be performed if the total number of input URLs
   * is less than `DEFAULT_MIN_URLS` (or the corresponding Nimbus value).
   *
   * @param {object} options
   *   options.minUrlsForTest {number} A minimal URL count used only for testing.
   */
  async #doClassification(options = {}) {
    if (this.isInProgress) {
      lazy.log.info(
        "Another classification is in progress, aborting interest classification"
      );
      return;
    }

    // Set a flag indicating this classification. Ensure it's cleared upon early
    // exit points & success.
    this.#isInProgress = true;

    let timerId;

    try {
      lazy.log.info("Fetching input data for interest classification");

      const maxUrls =
        lazy.NimbusFeatures.contentRelevancy.getVariable(
          NIMBUS_VARIABLE_MAX_INPUT_URLS
        ) ?? DEFAULT_MAX_URLS;
      const minUrls =
        lazy.NimbusFeatures.contentRelevancy.getVariable(
          NIMBUS_VARIABLE_MIN_INPUT_URLS
        ) ??
        options.minUrlsForTest ??
        DEFAULT_MIN_URLS;
      const urls = await lazy.getFrecentRecentCombinedUrls(maxUrls);
      if (urls.length < minUrls) {
        lazy.log.info("Aborting interest classification: insufficient input");
        Glean.relevancyClassify.fail.record({ reason: "insufficient-input" });
        return;
      }

      lazy.log.info("Starting interest classification");
      timerId = Glean.relevancyClassify.duration.start();

      const interestVector = await this.#classifyUrls(urls);
      this.#interestVector = interestVector;
      const sortedVector = Object.entries(interestVector).sort(
        ([, a], [, b]) => b - a // descending
      );
      lazy.log.info(`Classification results: ${JSON.stringify(sortedVector)}`);

      Glean.relevancyClassify.duration.stopAndAccumulate(timerId);
      Glean.relevancyClassify.succeed.record({
        input_size: urls.length,
        input_classified_size: sortedVector.reduce((acc, [, v]) => acc + v, 0),
        input_inconclusive_size: interestVector.inconclusive,
        output_interest_size: sortedVector.filter(([, v]) => v != 0).length,
        interest_top_1_hits: sortedVector[0][1],
        interest_top_2_hits: sortedVector[1][1],
        interest_top_3_hits: sortedVector[2][1],
      });
    } catch (error) {
      let reason;

      if (error instanceof StoreDisabledError) {
        lazy.log.error(
          "RustRelevancyStoreManager not enabled, aborting interest classification"
        );
        reason = "store-not-ready";
      } else {
        lazy.log.error("Classification error: " + (error.reason ?? error));
        reason = "component-errors";
      }
      Glean.relevancyClassify.fail.record({ reason });
      Glean.relevancyClassify.duration.cancel(timerId); // No error is recorded if `start` was not called.
    } finally {
      this.#isInProgress = false;
    }

    lazy.log.info("Finished interest classification");
  }

  /**
   * Interrupt all the IO operations on the relevancy store.
   */
  interrupt() {
    if (this.#storeManager.enabled) {
      try {
        lazy.log.debug(
          "Interrupting all the IO operations on the relevancy store"
        );
        this.#storeManager.store.interrupt();
      } catch (error) {
        lazy.log.error("Interrupt error: " + (error.reason ?? error));
      }
    }
  }

  /**
   * Exposed for testing.
   */
  async _test_doClassification(options = {}) {
    await this.#doClassification(options);
  }

  /**
   * Classify a list of URLs.
   *
   * If the nimbus feature is disabled, then this will succeed but return an empty InterestVector.
   *
   * @param {Array} urls
   *   An array of URLs.
   * @returns {InterestVector}
   *   An interest vector.
   * @throws {StoreDisabledError}
   *   Thrown when storeManager is disabled.
   * @throws {RelevancyAPIError}
   *   Thrown for other API errors on the store.
   */
  async #classifyUrls(urls) {
    lazy.log.info("Classification input: " + urls);
    let interestVector = new lazy.InterestVector({
      animals: 0,
      arts: 0,
      autos: 0,
      business: 0,
      career: 0,
      education: 0,
      fashion: 0,
      finance: 0,
      food: 0,
      government: 0,
      hobbies: 0,
      home: 0,
      news: 0,
      realEstate: 0,
      society: 0,
      sports: 0,
      tech: 0,
      travel: 0,
      inconclusive: 0,
    });

    if (
      lazy.NimbusFeatures.contentRelevancy.getVariable(
        NIMBUS_VARIABLE_INGEST_ENABLED
      ) ??
      false
    ) {
      interestVector = await this.#storeManager.store.ingest(urls);
    }

    return interestVector;
  }

  /**
   * Get the user interest vector from the relevancy store.
   *
   * Note:
   *   - It will return "null" if the vector couldn't be fetched from
   *     the relevancy store or the store is not enabled.
   *   - The fetched interest vector will be cached for future acesses.
   *     the cache will be automatically updated upon the next interest
   *     classification.
   *
   * @returns {InterestVector}
   *   An interest vector.
   */
  async getUserInterestVector() {
    if (!this.enabled) {
      return null;
    }

    if (this.#interestVector !== null) {
      return this.#interestVector;
    }

    try {
      this.#interestVector =
        await this.#storeManager.store.userInterestVector();
    } catch (error) {
      lazy.log.error(
        "Failed to fetch the interest vector: " + (error.reason ?? error)
      );
    }

    return this.#interestVector;
  }

  /**
   * Generate a score for a given interest array based on the user interest vector.
   *
   * @param {Array<Interest>} interests
   *   An array of interests with each item of type `RustRelevancy.Interest`.
   *   This is usually specified by the content provider representing the
   *   interest type(s) of the content. Note that `Interest.INCONCLUSIVE`
   *   will be ignored for scoring.
   * @param {boolean}  adjustInterest
   *   Whether or not to adjust `interests` for off-by-1 encoding difference
   *   between the UniFFI binding and the true source. This flag will be
   *   ignored if `Interest.INCONCLUSIVE == 0` meaning the encoding behavior
   *   gets unified in UniFFI.
   * @returns {number}
   *   A relevance score ranges from 0 to 1. A higher score indicating the content
   *   is more relevant to the user.
   * @throws {Error}
   *   Thrown for any store errors or invalid interest parameters.
   */
  async score(interests, adjustInterest = false) {
    const userInterestVector = await this.getUserInterestVector();
    if (userInterestVector === null) {
      throw new Error("User interest vector not ready");
    }

    // Copy it for mutation below.
    let newInterests = [...interests];

    // UniFFI currently uses 1-based encoding for enums regardless of how
    // it's defined upstream. Manually adjust that until that off-by-1
    // handling gets fixed in the future.
    //
    // `INCONCLUSIVE` should be 0 as defined by upstream. If not, convert the
    // interest values via the conversion lookup table.
    if (lazy.Interest.INCONCLUSIVE !== 0 && adjustInterest) {
      newInterests = newInterests.map(
        item => lazy.SerpCategoriesToInterestEnum.get(item) || item
      );
    }
    // `INCONCLUSIVE` is excluded from scoring.
    newInterests = newInterests.filter(
      item => item !== lazy.Interest.INCONCLUSIVE
    );

    let relevanceScore;
    try {
      relevanceScore = lazy.score(userInterestVector, newInterests);
    } catch (error) {
      throw new Error("Invalid interest value");
    }

    return relevanceScore;
  }

  /**
   * Nimbus update listener.
   */
  #onNimbusUpdate(_event, _reason) {
    this.#toggleFeature();
  }

  /**
   * Preference observer
   */
  observe(_subj, topic, data) {
    switch (topic) {
      case "nsPref:changed":
        if (data === PREF_LOG_ENABLED) {
          // Call setupLogging again so that the logger will be re-created with the updated pref
          setupLogging();
        }
        break;
    }
  }

  // The `RustRelevancyStoreManager`
  #storeManager;

  // Whether or not the module is initialized.
  #initialized = false;

  // Whether or not there is an in-progress classification. Used to prevent
  // duplicate classification tasks.
  #isInProgress = false;

  // The inferred user interest vector
  #interestVector = null;
}

/**
 * Holds the RustRelevancyStore and handles enabling/disabling it.
 */
class RustRelevancyStoreManager {
  constructor(path, rustRelevancyStore = undefined) {
    lazy.log.info(`Initializing RelevancyStore: ${path}`);
    if (rustRelevancyStore === undefined) {
      rustRelevancyStore = lazy.RelevancyStore;
    }
    // Initialize a RemoteSettingsService for the relevancy store
    // TODO (1956519): consolidate this with the Suggest code and only create a single app-wide remote settings
    // service.  For now this duplication is okay though because we're not really shipping Relevancy -- it's only enabled via a
    // pref.
    const rsService = lazy.RemoteSettingsService.init(
      PathUtils.join(
        Services.dirsvc.get("ProfLD", Ci.nsIFile).path,
        "remote-settings"
      ),
      new lazy.RemoteSettingsConfig2({})
    );
    this.#store = rustRelevancyStore.init(path, rsService);
  }

  get store() {
    if (!this.enabled) {
      throw new StoreDisabledError();
    }
    return this.#store;
  }

  enable() {
    this.enabled = true;
  }

  disable() {
    // Calling `close()` makes the store release all resources.  In particular, it closes all
    // database connections until a read/write method is called.  The `store` method ensures that
    // this won't happen before `enable` is called.
    this.#store.close();
    this.enabled = false;
  }

  // The `RustRelevancy` store.
  #store;

  // Is the store enabled?
  enabled = false;
}

/**
 * Error raised when attempting to access a null store.
 */
class StoreDisabledError extends Error {
  constructor() {
    super("RustRelevancyStoreManager is disabled");
    this.name = "StoreDisabledError";
  }
}

export var ContentRelevancyManager = new RelevancyManager();

/**
 * Exposed to provide an easy way for users to run a single classification
 *
 * This allows users to test out the relevancy component without too much hassle:
 *
 *  - Enable the `toolkit.contentRelevancy.log` pref
 *  - Enable the `devtools.chrome.enabled` pref if it wasn't already
 *  - Open the browser console and enter:
 *    `ChromeUtils.importESModule("resource://gre/modules/ContentRelevancyManager.sys.mjs").classifyOnce()`
 */
export function classifyOnce() {
  ContentRelevancyManager._test_doClassification();
}

export var exposedForTesting = {
  RustRelevancyStoreManager,
};
