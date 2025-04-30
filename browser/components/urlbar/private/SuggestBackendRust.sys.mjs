/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestBackend } from "resource:///modules/urlbar/private/SuggestFeature.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  InterruptKind: "resource://gre/modules/RustSuggest.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettingsConfig2: "resource://gre/modules/RustRemoteSettings.sys.mjs",
  RemoteSettingsContext: "resource://gre/modules/RustRemoteSettings.sys.mjs",
  RemoteSettingsServer: "resource://gre/modules/RustRemoteSettings.sys.mjs",
  RemoteSettingsService: "resource://gre/modules/RustRemoteSettings.sys.mjs",
  SuggestIngestionConstraints: "resource://gre/modules/RustSuggest.sys.mjs",
  SuggestStoreBuilder: "resource://gre/modules/RustSuggest.sys.mjs",
  Suggestion: "resource://gre/modules/RustSuggest.sys.mjs",
  SuggestionProvider: "resource://gre/modules/RustSuggest.sys.mjs",
  SuggestionProviderConstraints: "resource://gre/modules/RustSuggest.sys.mjs",
  SuggestionQuery: "resource://gre/modules/RustSuggest.sys.mjs",
  TaskQueue: "resource:///modules/UrlbarUtils.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "timerManager",
  "@mozilla.org/updates/timer-manager;1",
  "nsIUpdateTimerManager"
);

const SUGGEST_DATA_STORE_BASENAME = "suggest.sqlite";

// This ID is used to register our ingest timer with nsIUpdateTimerManager.
const INGEST_TIMER_ID = "suggest-ingest";
const INGEST_TIMER_LAST_UPDATE_PREF = `app.update.lastUpdateTime.${INGEST_TIMER_ID}`;

// Maps from `suggestion.constructor` to the corresponding name of the
// suggestion type. See `getSuggestionType()` for details.
const gSuggestionTypesByCtor = new WeakMap();

/**
 * The Suggest Rust backend. Not used when the remote settings JS backend is
 * enabled.
 *
 * This class returns suggestions served by the Rust component. These are the
 * primary related architectural pieces (see bug 1851256 for details):
 *
 * (1) The `suggest` Rust component, which lives in the application-services
 *     repo [1] and is periodically vendored into mozilla-central [2] and then
 *     built into the Firefox binary.
 * (2) `suggest.udl`, which is part of the Rust component's source files and
 *     defines the interface exposed to foreign-function callers like JS [3, 4].
 * (3) `RustSuggest.sys.mjs` [5], which contains the JS bindings generated from
 *     `suggest.udl` by UniFFI. The classes defined in `RustSuggest.sys.mjs` are
 *     what we consume here in this file. If you have a question about the JS
 *     interface to the Rust component, try checking `RustSuggest.sys.mjs`, but
 *     as you get accustomed to UniFFI JS conventions you may find it simpler to
 *     refer directly to `suggest.udl`.
 * (4) `config.toml` [6], which defines which functions in the JS bindings are
 *     sync and which are async. Functions default to the "worker" thread, which
 *     means they are async. Some functions are "main", which means they are
 *     sync. Async functions return promises. This information is reflected in
 *     `RustSuggest.sys.mjs` of course: If a function is "worker", its JS
 *     binding will return a promise, and if it's "main" it won't.
 *
 * [1] https://github.com/mozilla/application-services/tree/main/components/suggest
 * [2] https://searchfox.org/mozilla-central/source/third_party/rust/suggest
 * [3] https://github.com/mozilla/application-services/blob/main/components/suggest/src/suggest.udl
 * [4] https://searchfox.org/mozilla-central/source/third_party/rust/suggest/src/suggest.udl
 * [5] https://searchfox.org/mozilla-central/source/toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs
 * [6] https://searchfox.org/mozilla-central/source/toolkit/components/uniffi-bindgen-gecko-js/config.toml
 */
export class SuggestBackendRust extends SuggestBackend {
  constructor(...args) {
    super(...args);
    this.#ingestQueue = new lazy.TaskQueue();

    // The remote settings server URL returned by `Utils.SERVER_URL` comes from
    // the `services.settings.server` pref. The xpcshell and browser test
    // harnesses set this pref to `"data:,#remote-settings-dummy/v1"` so that
    // browser features that use RS and remain enabled during tests don't hit
    // the real server. Suggest tests use a mock RS server and set this pref to
    // that server's URL, but during other tests the pref remains the dummy URL.
    // During those other tests, Suggest remains enabled, which means if we
    // initialize the Suggest store with the dummy URL, the Rust Suggest and RS
    // components will attempt to use it (when the store is initialized and on
    // initial ingest). Unfortunately the Rust RS component logs an error each
    // time it tries to manipulate the dummy URL because it's a `data` URI,
    // which is a "cannot-be-a-base" URL. The error is harmless, but it can be
    // logged many times during a test suite.
    //
    // To prevent Suggest from using the dummy URL, we skip setting the initial
    // RS config here during tests, which prevents the Suggest store from being
    // created, effectively disabling Rust suggestions. Suggest tests manually
    // set the RS config when they set up the mock RS server, so they'll work
    // fine. Alternatively the test harnesses could disable Suggest by default
    // just like they set the server pref to the dummy URL, but Suggest is more
    // than Rust suggestions.
    if (!lazy.Utils.shouldSkipRemoteActivityDueToTests) {
      this.#setRemoteSettingsConfig({
        serverUrl: lazy.Utils.SERVER_URL,
        bucketName: lazy.Utils.actualBucketName("main"),
      });
    }
  }

  get enablingPreferences() {
    return ["quicksuggest.rustEnabled"];
  }

  /**
   * @returns {object}
   *   The global Suggest config from the Rust component as returned from
   *   `SuggestStore.fetchGlobalConfig()`.
   */
  get config() {
    return this.#config || {};
  }

  /**
   * @returns {Promise}
   *   Resolved when all pending ingests are done.
   */
  get ingestPromise() {
    return this.#ingestQueue.emptyPromise;
  }

  enable(enabled) {
    if (enabled) {
      this.#init();
    } else {
      this.#uninit();
    }
  }

  /**
   * Queries the Rust component and returns all matching suggestions.
   *
   * @param {string} searchString
   *   The search string.
   * @param {object} options
   *   Options object.
   * @param {UrlbarQueryContext} options._queryContext
   *   The query context.
   * @param {Array} options.types
   *   This is only intended to be used in special circumstances and normally
   *   should not be specified. Array of suggestion types to query. By default
   *   all enabled suggestion types are queried.
   * @returns {Array}
   *   Matching Rust suggestions.
   */
  async query(searchString, { _queryContext, types = null } = {}) {
    if (!this.#store) {
      return [];
    }

    this.logger.debug("Handling query", { searchString });

    if (!types) {
      types = this.#enabledSuggestionTypes;
    } else {
      types = types.map(type => {
        let provider = this.#providerFromSuggestionType(type);
        if (!provider) {
          throw new Error("Unknown Rust suggestion type: " + type);
        }
        return { type, provider };
      });
    }

    let providers = [];
    let allProviderConstraints = {};
    for (let { type, provider } of types) {
      this.logger.debug("Adding type to query", { type, provider });
      providers.push(provider);

      let providerConstraints =
        lazy.QuickSuggest.getFeatureByRustSuggestionType(
          type
        ).rustProviderConstraints;
      if (providerConstraints) {
        allProviderConstraints = {
          ...allProviderConstraints,
          ...providerConstraints,
        };
      }
    }

    const { suggestions, queryTimes } = await this.#store.queryWithMetrics(
      new lazy.SuggestionQuery({
        providers,
        keyword: searchString,
        providerConstraints: new lazy.SuggestionProviderConstraints(
          allProviderConstraints
        ),
      })
    );

    for (let { label, value } of queryTimes) {
      Glean.suggest.queryTime[label].accumulateSingleSample(value);
    }

    for (let suggestion of suggestions) {
      let type = getSuggestionType(suggestion);
      if (!type) {
        continue;
      }

      suggestion.source = "rust";
      suggestion.provider = type;
      if (suggestion.icon) {
        suggestion.icon_blob = new Blob([suggestion.icon], {
          type: suggestion.iconMimetype ?? "",
        });

        delete suggestion.icon;
        delete suggestion.iconMimetype;
      }
    }

    this.logger.debug("Got suggestions", suggestions);

    return suggestions;
  }

  cancelQuery() {
    this.#store?.interrupt(lazy.InterruptKind.READ);
  }

  /**
   * Returns suggestion-type-specific configuration data set by the Rust
   * backend.
   *
   * @param {string} type
   *   A Rust suggestion type name as defined in `suggest.udl`, e.g., "Amp",
   *   "Wikipedia", "Mdn", etc. See also `SuggestProvider.rustSuggestionType`.
   * @returns {object} config
   *   The config data for the type.
   */
  getConfigForSuggestionType(type) {
    return this.#configsBySuggestionType.get(type);
  }

  /**
   * Ingests a feature's enabled suggestion types and updates staleness
   * bookkeeping. By default only stale suggestion types are ingested. A
   * suggestion type is stale if (a) it hasn't been ingested during this app
   * session or (b) the last time this method was called the suggestion type or
   * its feature was disabled.
   *
   * @param {SuggestProvider} feature
   *   A feature that manages Rust suggestion types.
   * @param {object} options
   *   Options object.
   * @param {bool} options.evenIfFresh
   *   Set to true to force ingest for all the feature's suggestion types, even
   *   ones that aren't stale.
   */
  ingestEnabledSuggestions(feature, { evenIfFresh = false } = {}) {
    let type = feature.rustSuggestionType;
    if (!type) {
      return;
    }

    if (!this.isEnabled || !feature.isEnabled) {
      // Mark this type as stale so we'll ingest next time this method is
      // called.
      this.#providerConstraintsByIngestedSuggestionType.delete(type);
    } else {
      let providerConstraints = feature.rustProviderConstraints;
      if (
        evenIfFresh ||
        !this.#providerConstraintsByIngestedSuggestionType.has(type) ||
        !lazy.ObjectUtils.deepEqual(
          providerConstraints,
          this.#providerConstraintsByIngestedSuggestionType.get(type)
        )
      ) {
        this.#providerConstraintsByIngestedSuggestionType.set(
          type,
          providerConstraints
        );
        this.#ingestSuggestionType({ type, providerConstraints });
      }
    }
  }

  /**
   * Registers a dismissal for a Rust suggestion.
   *
   * @param {Suggestion} suggestion
   *   The suggestion to dismiss, an instance of one of the `Suggestion`
   *   subclasses exposed over FFI, e.g., `Suggestion.Wikipedia`. Typically the
   *   suggestion will have been returned from the Rust component, but tests may
   *   find it useful to make a `Suggestion` object directly.
   */
  async dismissRustSuggestion(suggestion) {
    try {
      await this.#store?.dismissBySuggestion(suggestion);
    } catch (error) {
      this.logger.error("Error: dismissRustSuggestion", { error, suggestion });
    }
  }

  /**
   * Registers a dismissal using a dismissal key. If you have a suggestion
   * object returned from the Rust component, use `dismissRustSuggestion()`
   * instead. This method can be used to record dismissals for suggestions from
   * other backends, like Merino.
   *
   * @param {string} dismissalKey
   *   The dismissal key.
   */
  async dismissByKey(dismissalKey) {
    try {
      await this.#store?.dismissByKey(dismissalKey);
    } catch (error) {
      this.logger.error("Error: dismissByKey", { error, dismissalKey });
    }
  }

  /**
   * Returns whether a dismissal is recorded for a Rust suggestion.
   *
   * @param {Suggestion} suggestion
   *   The suggestion to dismiss, an instance of one of the `Suggestion`
   *   subclasses exposed over FFI, e.g., `Suggestion.Wikipedia`. Typically the
   *   suggestion will have been returned from the Rust component, but tests may
   *   find it useful to make a `Suggestion` object directly.
   * @returns {boolean}
   *   Whether the suggestion has been dismissed.
   */
  async isRustSuggestionDismissed(suggestion) {
    try {
      return await this.#store?.isDismissedBySuggestion(suggestion);
    } catch (error) {
      this.logger.error("Error: isDismissedBySuggestion", {
        error,
        suggestion,
      });
    }
    return false;
  }

  /**
   * Returns whether a dismissal is recorded for a dismissal key. If you have a
   * suggestion object returned from the Rust component, use
   * `isRustSuggestionDismissed()` instead. This method can be used to determine
   * whether suggestions from other backends, like Merino, have been dismissed.
   *
   * @param {string} dismissalKey
   *   The dismissal key.
   * @returns {boolean}
   *   Whether a dismissal is recorded for the key.
   */
  async isDismissedByKey(dismissalKey) {
    try {
      return await this.#store?.isDismissedByKey(dismissalKey);
    } catch (error) {
      this.logger.error("Error: isDismissedByKey", { error, dismissalKey });
    }
    return false;
  }

  /**
   * Returns whether any dismissals are recorded.
   *
   * @returns {boolean}
   *   Whether any suggestions have been dismissed.
   */
  async anyDismissedSuggestions() {
    try {
      return await this.#store?.anyDismissedSuggestions();
    } catch (error) {
      this.logger.error("Error: anyDismissedSuggestions", error);
    }
    // Return true because there may be dismissed suggestions, we don't know.
    return true;
  }

  /**
   * Removes all registered dismissals.
   */
  async clearDismissedSuggestions() {
    try {
      await this.#store?.clearDismissedSuggestions();
    } catch (error) {
      this.logger.error("Error clearing dismissed suggestions", error);
    }
  }

  /**
   * Fetches geonames stored in the Suggest database. A geoname represents a
   * geographic place.
   *
   * See `SuggestStore::fetch_geonames()` in the Rust component for full
   * documentation.
   *
   * @param {string} searchString
   *   The string to match against geonames.
   * @param {bool} matchNamePrefix
   *   Whether prefix matching is performed on names excluding abbreviations and
   *   airport codes.
   * @param {GeonameType} geonameType
   *   Restricts returned geonames to a type.
   * @param {Array} filter
   *   Restricts returned geonames to certain cities or regions. Optional.
   * @returns {Array}
   *   Array of `GeonameMatch` objects. An empty array if there are no matches.
   */
  fetchGeonames(searchString, matchNamePrefix, geonameType, filter) {
    if (!this.#store) {
      return [];
    }
    return this.#store.fetchGeonames(
      searchString,
      matchNamePrefix,
      geonameType,
      filter
    );
  }

  /**
   * nsITimerCallback
   */
  notify() {
    this.logger.info("Ingest timer fired");
    this.#ingestAll();
  }

  /**
   * @returns {string}
   *   The path of `suggest.sqlite`, where the Rust component stores ingested
   *   suggestions. It also stores dismissed suggestions, which is why we keep
   *   this file in the profile directory, but desktop doesn't currently use the
   *   Rust component for that.
   */
  get #storeDataPath() {
    return PathUtils.join(
      Services.dirsvc.get("ProfD", Ci.nsIFile).path,
      SUGGEST_DATA_STORE_BASENAME
    );
  }

  /**
   * @returns {string}
   *   The path of the directory that should contain the remote settings cache
   *   used internally by the Rust component.
   */
  get #remoteSettingsStoragePath() {
    return Services.dirsvc.get("ProfLD", Ci.nsIFile).path;
  }

  /**
   * @returns {Array}
   *   Each item in this array identifies an enabled Rust suggestion type and
   *   related data. Items have the following properties:
   *
   *   {string} type
   *     A Rust suggestion type name as defined in Rust, e.g., "Amp",
   *     "Wikipedia", "Mdn", etc.
   *   {number} provider
   *     An integer that identifies the provider of the suggestion type to Rust.
   */
  get #enabledSuggestionTypes() {
    let items = [];
    for (let feature of lazy.QuickSuggest.rustFeatures) {
      if (feature.isEnabled) {
        let type = feature.rustSuggestionType;
        let provider = this.#providerFromSuggestionType(type);
        if (provider) {
          items.push({ type, provider });
        }
      }
    }
    return items;
  }

  #init() {
    this.#store = this.#makeStore();
    if (!this.#store) {
      return;
    }

    // Log the last ingest time for debugging.
    let lastIngestSecs = Services.prefs.getIntPref(
      INGEST_TIMER_LAST_UPDATE_PREF,
      0
    );
    this.logger.debug("Last ingest time (seconds)", lastIngestSecs);

    // Add our shutdown blocker.
    this.#shutdownBlocker = () => {
      // Interrupt any ongoing ingests (WRITE) and queries (READ).
      // `interrupt()` runs on the main thread and is not async; see
      // toolkit/components/uniffi-bindgen-gecko-js/config.toml
      this.#store?.interrupt(lazy.InterruptKind.READ_WRITE);

      // Null the store so it's destroyed now instead of later when `this` is
      // collected. The store's Sqlite DBs are synced when dropped (its DB and
      // its RS client's DB), which causes a `LateWriteObserver` test failure if
      // it happens too late during shutdown.
      this.#store = null;
      this.#shutdownBlocker = null;
    };
    lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
      "QuickSuggest: Interrupt the Rust component",
      this.#shutdownBlocker
    );

    // Register the ingest timer.
    lazy.timerManager.registerTimer(
      INGEST_TIMER_ID,
      this,
      lazy.UrlbarPrefs.get("quicksuggest.rustIngestIntervalSeconds"),
      true // skipFirst
    );

    // Do an initial ingest for all enabled suggestion types. When a type
    // becomes enabled after this point, its `SuggestProvider` will update and
    // call `ingestEnabledSuggestions()`, which will be its initial ingest.
    this.#ingestAll();

    this.#migrateBlockedDigests().then(() => {
      // Now that the backend has finished initializing, send
      // `quicksuggest-dismissals-changed` to let consumers know that the
      // dismissal API is available. about:preferences relies on this to update
      // its "Restore" button if it's open at this time.
      Services.obs.notifyObservers(null, "quicksuggest-dismissals-changed");
    });
  }

  #makeStore() {
    this.logger.info("Creating SuggestStore", {
      server: this.#remoteSettingsServer,
      bucketName: this.#remoteSettingsBucketName,
      dataPath: this.#storeDataPath,
      storagePath: this.#remoteSettingsStoragePath,
    });

    if (!this.#remoteSettingsServer) {
      return null;
    }

    let rsContext = {
      formFactor: "desktop",
      appId: Services.appinfo.ID || "",
      channel: AppConstants.IS_ESR ? "esr" : AppConstants.MOZ_UPDATE_CHANNEL,
      appVersion: Services.appinfo.version,
      locale: Services.locale.appLocaleAsBCP47,
      os: AppConstants.platform,
      osVersion: Services.sysinfo.get("version"),
    };

    // We assume `QuickSuggest` init already awaited `Region.init()`.
    if (lazy.Region.home) {
      rsContext.country = lazy.Region.home;
    }

    let rsService;
    try {
      rsService = lazy.RemoteSettingsService.init(
        this.#remoteSettingsStoragePath,
        new lazy.RemoteSettingsConfig2({
          server: this.#remoteSettingsServer,
          bucketName: this.#remoteSettingsBucketName,
          appContext: new lazy.RemoteSettingsContext(rsContext),
        })
      );
    } catch (error) {
      this.logger.error("Error creating RemoteSettingsService", error);
      return null;
    }

    let builder;
    try {
      builder = lazy.SuggestStoreBuilder.init()
        .dataPath(this.#storeDataPath)
        .remoteSettingsService(rsService)
        .loadExtension(
          AppConstants.SQLITE_LIBRARY_FILENAME,
          "sqlite3_fts5_init"
        );
    } catch (error) {
      this.logger.error("Error creating SuggestStoreBuilder", error);
      return null;
    }

    let store;
    try {
      store = builder.build();
    } catch (error) {
      this.logger.error("Error creating SuggestStore", error);
      return null;
    }

    return store;
  }

  #uninit() {
    this.#store = null;
    this.#providerConstraintsByIngestedSuggestionType.clear();
    this.#configsBySuggestionType.clear();
    lazy.timerManager.unregisterTimer(INGEST_TIMER_ID);

    lazy.AsyncShutdown.profileChangeTeardown.removeBlocker(
      this.#shutdownBlocker
    );
    this.#shutdownBlocker = null;
  }

  /**
   * Ingests the given suggestion type.
   *
   * @param {object} options
   *   Options object.
   * @param {string} options.type
   *   A Rust suggestion type name as defined in `suggest.udl`, e.g., "Amp",
   *   "Wikipedia", "Mdn", etc.
   * @param {object|null} options.providerConstraints
   *   A plain JS object version of the type's provider constraints, if any.
   */
  #ingestSuggestionType({ type, providerConstraints }) {
    this.#ingestQueue.queueIdleCallback(async () => {
      if (!this.#store) {
        return;
      }

      let provider = this.#providerFromSuggestionType(type);
      if (!provider) {
        return;
      }

      this.logger.debug("Starting ingest", { type });
      try {
        const metrics = await this.#store.ingest(
          new lazy.SuggestIngestionConstraints({
            providers: [provider],
            providerConstraints: providerConstraints
              ? new lazy.SuggestionProviderConstraints(providerConstraints)
              : null,
          })
        );
        for (let { label, value } of metrics.downloadTimes) {
          Glean.suggest.ingestDownloadTime[label].accumulateSingleSample(value);
        }
        for (let { label, value } of metrics.ingestionTimes) {
          Glean.suggest.ingestTime[label].accumulateSingleSample(value);
        }
      } catch (error) {
        // Ingest can throw a `SuggestApiError` subclass called `Other` with a
        // `reason` message, which is very helpful for diagnosing problems with
        // remote settings data in tests in particular.
        this.logger.error("Ingest error", {
          type,
          error,
          reason: error.reason,
        });
      }
      this.logger.debug("Finished ingest", { type });

      if (!this.#store) {
        return;
      }

      // Fetch the provider config.
      this.logger.debug("Fetching provider config", { type });
      let config = await this.#store.fetchProviderConfig(provider);
      this.logger.debug("Got provider config", { type, config });
      this.#configsBySuggestionType.set(type, config);
      this.logger.debug("Finished fetching provider config", { type });
    });
  }

  #ingestAll() {
    // Ingest all enabled suggestion types.
    for (let feature of lazy.QuickSuggest.rustFeatures) {
      this.ingestEnabledSuggestions(feature, { evenIfFresh: true });
    }

    // Fetch the global config.
    this.#ingestQueue.queueIdleCallback(async () => {
      if (!this.#store) {
        return;
      }
      this.logger.debug("Fetching global config");
      this.#config = await this.#store.fetchGlobalConfig();
      this.logger.debug("Got global config", this.#config);
    });
  }

  /**
   * Given a Rust suggestion type, gets the integer value that identifies the
   * corresponding suggestion provider to Rust.
   *
   * @param {string} type
   *   A Rust suggestion type name as defined in `suggest.udl`, e.g., "Amp",
   *   "Wikipedia", "Mdn", etc.
   * @returns {number}
   *   An integer that identifies the provider of the suggestion type to Rust.
   */
  #providerFromSuggestionType(type) {
    let key = type.toUpperCase();
    if (!lazy.SuggestionProvider.hasOwnProperty(key)) {
      // Normally this shouldn't happen but it can during development when the
      // Rust component and desktop integration are out of sync.
      this.logger.error("SuggestionProvider[key] not defined!", { key });
      return null;
    }
    return lazy.SuggestionProvider[key];
  }

  #setRemoteSettingsConfig(options) {
    let { serverUrl, bucketName } = options || {};
    this.#remoteSettingsServer = serverUrl
      ? new lazy.RemoteSettingsServer.Custom(serverUrl)
      : null;
    this.#remoteSettingsBucketName = bucketName;
  }

  /**
   * Dismissals are stored in the Rust component but were previously stored as
   * URL digests in a pref. This method migrates the pref to the Rust component
   * by registering each digest as a dismissal key in the Rust component. The
   * pref is cleared when the migration successfully finishes.
   */
  async #migrateBlockedDigests() {
    if (!this.#store) {
      return;
    }

    let pref = "browser.urlbar.quicksuggest.blockedDigests";
    this.logger.debug("Checking blockedDigests migration", { pref });

    let json;
    // eslint-disable-next-line mozilla/use-default-preference-values
    try {
      json = Services.prefs.getCharPref(pref);
    } catch (error) {
      if (error.result != Cr.NS_ERROR_UNEXPECTED) {
        throw error;
      }
      this.logger.debug(
        "blockedDigests pref does not exist, migration not necessary"
      );
      return;
    }

    await this.#migrateBlockedDigestsJson(json);

    // Don't clear the pref until migration finishes successfully, in case
    // there's some uncaught error. We don't want to lose the user's data.
    Services.prefs.clearUserPref(pref);
  }

  // This assumes `this.#store` is non-null!
  async #migrateBlockedDigestsJson(json) {
    let digests;
    try {
      digests = JSON.parse(json);
    } catch (error) {
      this.logger.debug("blockedDigests is not valid JSON, discarding it");
      return;
    }

    if (!digests) {
      this.logger.debug("blockedDigests is falsey, discarding it");
      return;
    }

    if (!Array.isArray(digests)) {
      this.logger.debug("blockedDigests is not an array, discarding it");
      return;
    }

    let promises = [];
    for (let digest of digests) {
      if (typeof digest != "string") {
        continue;
      }
      promises.push(this.#store.dismissByKey(digest));
    }
    await Promise.all(promises);
  }

  get _test_store() {
    return this.#store;
  }

  get _test_enabledSuggestionTypes() {
    return this.#enabledSuggestionTypes;
  }

  async _test_setRemoteSettingsConfig(options) {
    this.#setRemoteSettingsConfig(options);
    if (this.isEnabled) {
      // Recreate the store and re-ingest.
      Services.prefs.clearUserPref(INGEST_TIMER_LAST_UPDATE_PREF);
      this.#uninit();
      this.#init();
      await this.ingestPromise;
    }
  }

  async _test_ingest() {
    this.#ingestAll();
    await this.ingestPromise;
  }

  // The `SuggestStore` instance.
  #store;

  // Global Suggest config as returned from `SuggestStore.fetchGlobalConfig()`.
  #config = {};

  // Maps from suggestion type to provider config as returned from
  // `SuggestStore.fetchProviderConfig()`.
  #configsBySuggestionType = new Map();

  // Keeps track of suggestion types with fresh (non-stale) ingests. Maps
  // ingested suggestion types to `feature.rustProviderConstraints`.
  #providerConstraintsByIngestedSuggestionType = new Map();

  #ingestQueue;
  #shutdownBlocker;
  #remoteSettingsServer;
  #remoteSettingsBucketName;
}

/**
 * Returns the type of a suggestion.
 *
 * @param {Suggestion} suggestion
 *   A suggestion object, an instance of one of the `Suggestion` subclasses.
 * @returns {string}
 *   The suggestion's type, e.g., "Amp", "Wikipedia", etc.
 */
function getSuggestionType(suggestion) {
  // Suggestion objects served by the Rust component don't have any inherent
  // type information other than the classes they are instances of. There's no
  // `type` property, for example. There's a base `Suggestion` class and many
  // `Suggestion` subclasses, one per type of suggestion. Each suggestion object
  // is an instance of one of these subclasses. We derive a suggestion's type
  // from the subclass it's an instance of.
  //
  // Unfortunately the subclasses are all anonymous, which means
  // `suggestion.constructor.name` is always an empty string. (This is due to
  // how UniFFI generates JS bindings.) Instead, the subclasses are defined as
  // properties on the base `Suggestion` class. For example,
  // `Suggestion.Wikipedia` is the (anonymous) Wikipedia suggestion class. To
  // find a suggestion's subclass, we loop through the keys on `Suggestion`
  // until we find the value the suggestion is an instance of. To avoid doing
  // this every time, we cache the mapping from suggestion constructor to key
  // the first time we encounter a new suggestion subclass.
  let type = gSuggestionTypesByCtor.get(suggestion.constructor);
  if (!type) {
    type = Object.keys(lazy.Suggestion).find(
      key => suggestion instanceof lazy.Suggestion[key]
    );
    if (type) {
      gSuggestionTypesByCtor.set(suggestion.constructor, type);
    } else {
      console.error(
        "Unexpected error: Suggestion class not found on `Suggestion`. " +
          "Did the Rust component or its JS bindings change? ",
        { suggestion }
      );
    }
  }
  return type;
}
