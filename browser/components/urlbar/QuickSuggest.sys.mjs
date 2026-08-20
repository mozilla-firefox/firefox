/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  Preferences: "resource://gre/modules/Preferences.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  TelemetryEnvironment: "resource://gre/modules/TelemetryEnvironment.sys.mjs",
  TelemetryReportingPolicy:
    "resource://gre/modules/TelemetryReportingPolicy.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

// See the `QuickSuggest.SETTINGS_UI` jsdoc below.
const SETTINGS_UI = Object.freeze({
  // Settings relevant to both offline and online will be shown.
  FULL: 0,
  // Settings relevant to both offline and online will be hidden.
  NONE: 1,
  // Only settings relevant to offline will be shown. Settings that pertain to
  // online will be hidden.
  OFFLINE_ONLY: 2,
});

// Timestamp in ms since epoch of the Firefox Terms of Use (ToU) pertaining to
// Suggest. Keep this value in sync with Nimbus targeting in `constants.py` in
// the `experimenter` repo.
//
// Privacy Notification Published date of 12:00 PM UTC on Dec 15, 2025
const SUGGEST_TOU_TIMESTAMP = 1765800000000;

const EN_LOCALES = ["en-CA", "en-GB", "en-US", "en-ZA"];

/**
 * @typedef {[string[], boolean|number|Function]} RegionLocaleDefault
 *   The first element is an array of locales, e.g. `["en-US", "en-CA"]`. The
 *   second element is either the value of the preference or a function that
 *   should return the value of the preference.
 */

/**
 * @typedef {object} SuggestPrefsRecord
 * @property {Record<string, RegionLocaleDefault>} [defaultValues]
 *   This controls the home regions and locales where Suggest and each of its
 *   subfeatures will be enabled. If the pref should be initialized on the
 *   default branch depending on the user's home region and locale, then this
 *   should be set to an object where each entry maps a region name to a tuple
 *   `[locales, prefValue]`. `locales` is an array of strings and `prefValue` is
 *   the value that should be set when the region and locale match the user's
 *   region and locale. If the user's region and locale do not match any of the
 *   entries in `defaultValues`, then the pref will retain its default value as
 *   defined in `firefox.js`.
 * @property {string} [nimbusVariableIfExposedInUi]
 *   If the pref is exposed in the settings UI and it's a fallback for a Nimbus
 *   variable, then this should be set to the variable's name. See point 3 in
 *   the comment in `#initPrefs()` for more.
 */

/**
 * This defines the home regions and locales where Suggest will be enabled.
 * Suggest will remain disabled for regions and locales not defined here. More
 * generally it defines important Suggest prefs that require special handling.
 * Each entry in this object defines a pref name and information about that
 * pref. Pref names are relative to `browser.urlbar.` The value in each entry is
 * an object with the following properties:
 *
 * @type {{[key: string]: SuggestPrefsRecord}}
 * {object} defaultValues
 */
const SUGGEST_PREFS = Object.freeze({
  // Prefs related to Suggest overall
  //
  // Please update `test_quicksuggest_defaultPrefs.js` when you change these.
  "quicksuggest.enabled": {
    defaultValues: {
      DE: [["de", ...EN_LOCALES], true],
      FR: [["fr", ...EN_LOCALES], true],
      GB: [EN_LOCALES, true],
      IT: [["it", ...EN_LOCALES], true],
      US: [EN_LOCALES, true],
    },
  },
  "quicksuggest.online.available": {
    defaultValues: {
      US: [EN_LOCALES, shouldOnlineBeAvailable],
    },
  },
  "quicksuggest.settingsUi": {
    defaultValues: {
      DE: [["de"], SETTINGS_UI.OFFLINE_ONLY],
      FR: [["fr"], SETTINGS_UI.OFFLINE_ONLY],
      GB: [EN_LOCALES, SETTINGS_UI.OFFLINE_ONLY],
      IT: [["it"], SETTINGS_UI.OFFLINE_ONLY],
      US: [
        EN_LOCALES,
        () => {
          return shouldOnlineBeAvailable()
            ? SETTINGS_UI.FULL
            : SETTINGS_UI.OFFLINE_ONLY;
        },
      ],
    },
  },
  "suggest.quicksuggest.all": {
    defaultValues: {
      DE: [["de"], true],
      FR: [["fr"], true],
      GB: [EN_LOCALES, true],
      IT: [["it"], true],
      US: [EN_LOCALES, true],
    },
  },
  "suggest.quicksuggest.sponsored": {
    nimbusVariableIfExposedInUi: "quickSuggestSponsoredEnabled",
    defaultValues: {
      DE: [["de"], true],
      FR: [["fr"], true],
      GB: [EN_LOCALES, true],
      IT: [["it"], true],
      US: [EN_LOCALES, true],
    },
  },

  // Prefs related to individual features
  //
  // Please update `test_quicksuggest_defaultPrefs.js` when you change these.
  "addons.featureGate": {
    defaultValues: {
      US: [EN_LOCALES, true],
    },
  },
  "amp.featureGate": {
    defaultValues: {
      DE: [["de"], true],
      FR: [["fr"], true],
      GB: [EN_LOCALES, true],
      IT: [["it"], true],
      US: [EN_LOCALES, true],
    },
  },
  "flightStatus.featureGate": {
    defaultValues: {
      US: [EN_LOCALES, shouldOnlineBeAvailable],
    },
  },
  "importantDates.featureGate": {
    defaultValues: {
      DE: [["de", ...EN_LOCALES], true],
      FR: [["fr", ...EN_LOCALES], true],
      GB: [EN_LOCALES, true],
      IT: [["it", ...EN_LOCALES], true],
      US: [EN_LOCALES, true],
    },
  },
  "market.featureGate": {
    defaultValues: {
      US: [EN_LOCALES, shouldOnlineBeAvailable],
    },
  },
  "mdn.featureGate": {
    defaultValues: {
      US: [EN_LOCALES, true],
    },
  },
  "sports.featureGate": {
    defaultValues: {
      US: [EN_LOCALES, shouldOnlineBeAvailable],
    },
  },
  "weather.featureGate": {
    defaultValues: {
      DE: [["de"], true],
      FR: [["fr"], true],
      GB: [EN_LOCALES, true],
      IT: [["it"], true],
      US: [EN_LOCALES, true],
    },
  },
  "wikipedia.featureGate": {
    defaultValues: {
      DE: [["de"], true],
      FR: [["fr"], true],
      GB: [EN_LOCALES, true],
      IT: [["it"], true],
      US: [EN_LOCALES, true],
    },
  },
  "yelp.featureGate": {
    defaultValues: {
      US: [EN_LOCALES, true],
    },
  },
});

// Suggest features classes. On init, `QuickSuggest` creates an instance of each
// class and keeps it in the `#featuresByName` map. See `SuggestFeature`.
const FEATURES = {
  AddonSuggestions:
    "moz-src:///browser/components/urlbar/private/AddonSuggestions.sys.mjs",
  AmpSuggestions:
    "moz-src:///browser/components/urlbar/private/AmpSuggestions.sys.mjs",
  DynamicSuggestions:
    "moz-src:///browser/components/urlbar/private/DynamicSuggestions.sys.mjs",
  FlightStatusSuggestions:
    "moz-src:///browser/components/urlbar/private/FlightStatusSuggestions.sys.mjs",
  ImportantDatesSuggestions:
    "moz-src:///browser/components/urlbar/private/ImportantDatesSuggestions.sys.mjs",
  ImpressionCaps:
    "moz-src:///browser/components/urlbar/private/ImpressionCaps.sys.mjs",
  MarketSuggestions:
    "moz-src:///browser/components/urlbar/private/MarketSuggestions.sys.mjs",
  MDNSuggestions:
    "moz-src:///browser/components/urlbar/private/MDNSuggestions.sys.mjs",
  SportsSuggestions:
    "moz-src:///browser/components/urlbar/private/SportsSuggestions.sys.mjs",
  SuggestBackendMerino:
    "moz-src:///browser/components/urlbar/private/SuggestBackendMerino.sys.mjs",
  SuggestBackendMl:
    "moz-src:///browser/components/urlbar/private/SuggestBackendMl.sys.mjs",
  SuggestBackendRust:
    "moz-src:///browser/components/urlbar/private/SuggestBackendRust.sys.mjs",
  WeatherSuggestions:
    "moz-src:///browser/components/urlbar/private/WeatherSuggestions.sys.mjs",
  WikipediaSuggestions:
    "moz-src:///browser/components/urlbar/private/WikipediaSuggestions.sys.mjs",
  YelpRealtimeSuggestions:
    "moz-src:///browser/components/urlbar/private/YelpRealtimeSuggestions.sys.mjs",
  YelpSuggestions:
    "moz-src:///browser/components/urlbar/private/YelpSuggestions.sys.mjs",
};

/**
 * @import {SuggestBackendRust} from "moz-src:///browser/components/urlbar/private/SuggestBackendRust.sys.mjs"
 * @import {SuggestFeature} from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs"
 * @import {SuggestProvider} from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs"
 * @import {ImpressionCaps} from "moz-src:///browser/components/urlbar/private/ImpressionCaps.sys.mjs"
 */

/**
 * This class manages Firefox Suggest and has related helpers.
 */
class _QuickSuggest {
  /**
   * Test-only variable to skip telemetry environment initialisation.
   */
  _testSkipTelemetryEnvironmentInit = false;

  /**
   * @returns {string}
   *   The help URL for Suggest.
   */
  get HELP_URL() {
    return (
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      this.HELP_TOPIC
    );
  }

  /**
   * @returns {string}
   *   The help URL topic for Suggest.
   */
  get HELP_TOPIC() {
    return "firefox-suggest";
  }

  /**
   * Possible values of the `quickSuggestSettingsUi` Nimbus variable and its
   * fallback pref `browser.urlbar.quicksuggest.settingsUi`. When Suggest is
   * enabled, these values determine the Suggest settings that will be visible
   * in `about:preferences`. When Suggest is disabled, the variable/pref are
   * ignored and Suggest settings are hidden.
   */
  get SETTINGS_UI() {
    return SETTINGS_UI;
  }

  /**
   * @returns {number}
   *   The Firefox Terms of Use (ToU) timestamp in ms since epoch pertaining to
   *   Suggest.
   */
  get SUGGEST_TOU_TIMESTAMP() {
    return SUGGEST_TOU_TIMESTAMP;
  }

  /**
   * @returns {Promise}
   *   Resolved when Suggest initialization finishes.
   */
  get initPromise() {
    return this.#initResolvers.promise;
  }

  /**
   * @returns {Array}
   *   Enabled Suggest backends.
   */
  get enabledBackends() {
    // This getter may be accessed before `init()` is called, so the backends
    // may not be registered yet. Don't assume they're non-null.
    return [
      this.rustBackend,
      this.#featuresByName.get("SuggestBackendMerino"),
      this.#featuresByName.get("SuggestBackendMl"),
    ].filter(b => b?.isEnabled);
  }

  /**
   * @returns {SuggestBackendRust}
   *   The Rust backend, which manages the Rust component.
   */
  get rustBackend() {
    return this.#featuresByName.get("SuggestBackendRust");
  }

  /**
   * @returns {object}
   *   Global Suggest configuration stored in remote settings and ingested by
   *   the Rust component. See remote settings or the Rust component for the
   *   latest schema.
   */
  get config() {
    return this.rustBackend?.config || {};
  }

  /**
   * @returns {ImpressionCaps}
   *   The impression caps feature.
   */
  get impressionCaps() {
    return this.#featuresByName.get("ImpressionCaps");
  }

  /**
   * @returns {Set}
   *   The set of features that manage Rust suggestion types, as determined by
   *   each feature's `rustSuggestionType`.
   */
  get rustFeatures() {
    return new Set([
      ...this.#featuresByRustSuggestionType.values(),
      ...this.#featuresByDynamicRustSuggestionType.values(),
    ]);
  }

  /**
   * @returns {Set}
   *   The set of features that manage ML suggestion types, as determined by
   *   each feature's `mlIntent`.
   */
  get mlFeatures() {
    return new Set(this.#featuresByMlIntent.values());
  }

  get logger() {
    if (!this._logger) {
      this._logger = lazy.UrlbarShared.getLogger({ prefix: "QuickSuggest" });
    }
    return this._logger;
  }

  /**
   * Initializes Suggest. It's safe to call more than once.
   *
   * @param {object} testOverrides
   *   This is intended for tests only. See `#initPrefs()`.
   */
  async init(testOverrides = null) {
    if (this.#initStarted) {
      await this.initPromise;
      return;
    }
    this.#initStarted = true;

    // Wait for dependencies to finish before initializing prefs.
    //
    // (1) Whether Suggest should be enabled depends on the user's region.
    await lazy.Region.init();

    // (2) The default-branch values of Suggest prefs that are both exposed in
    // the UI and configurable by Nimbus depend on Nimbus.
    await lazy.NimbusFeatures.urlbar.ready();

    // (3) `TelemetryEnvironment` records the values of some Suggest prefs.
    if (!this._testSkipTelemetryEnvironmentInit) {
      await lazy.TelemetryEnvironment.onInitialized();
    }

    this.#initPrefs(testOverrides);

    // Create an instance of each feature and keep it in `#featuresByName`.
    for (let [name, uri] of Object.entries(FEATURES)) {
      let { [name]: ctor } = ChromeUtils.importESModule(uri);
      let feature = new ctor();
      this.#featuresByName.set(name, feature);
      if (feature.merinoProvider) {
        this.#featuresByMerinoProvider.set(feature.merinoProvider, feature);
      }
      if (feature.rustSuggestionType) {
        if (feature.dynamicRustSuggestionTypes?.length) {
          for (let t of feature.dynamicRustSuggestionTypes) {
            this.#featuresByDynamicRustSuggestionType.set(t, feature);
          }
        } else {
          this.#featuresByRustSuggestionType.set(
            feature.rustSuggestionType,
            feature
          );
        }
      }
      if (feature.mlIntent) {
        this.#featuresByMlIntent.set(feature.mlIntent, feature);
      }

      // Update the map from enabling preferences to features.
      let prefs = feature.enablingPreferences;
      if (prefs) {
        for (let p of prefs) {
          let features = this.#featuresByEnablingPrefs.get(p);
          if (!features) {
            features = new Set();
            this.#featuresByEnablingPrefs.set(p, features);
          }
          features.add(feature);
        }
      }
    }

    this.#updateAll();
    lazy.UrlbarPrefs.addObserver(this);

    this.#initResolvers.resolve();
  }

  /**
   * Returns a Suggest feature by name.
   *
   * @param {string} name
   *   The name of the feature's JS class.
   * @returns {SuggestFeature}
   *   The feature object, an instance of a subclass of `SuggestFeature`.
   */
  getFeature(name) {
    return this.#featuresByName.get(name);
  }

  /**
   * Returns a Suggest feature by the ML intent name (as defined by
   * `feature.mlIntent` and `MLSuggest`). Not all features support ML.
   *
   * @param {string} intent
   *   The name of an ML intent.
   * @returns {SuggestProvider}
   *   The feature object, an instance of a subclass of `SuggestProvider`, or
   *   null if no feature corresponds to the intent.
   */
  getFeatureByMlIntent(intent) {
    return this.#featuresByMlIntent.get(intent);
  }

  /**
   * Gets the Suggest feature that manages suggestions for urlbar result.
   *
   * @param {UrlbarResult} result
   *   The urlbar result.
   * @returns {SuggestProvider}
   *   The feature instance or null if none was found.
   */
  getFeatureByResult(result) {
    return this.getFeatureBySource(result.payload);
  }

  /**
   * Gets the Suggest feature that manages suggestions for a source and provider
   * name. The source and provider name can be supplied from either a suggestion
   * object or the payload of a `UrlbarResult` object.
   *
   * @param {object} options
   *   Options object.
   * @param {string} options.source
   *   The suggestion source, one of: "merino", "ml", "rust"
   * @param {string} options.provider
   *   This value depends on `source`. The possible values per source are:
   *
   *   merino:
   *     The name of the Merino provider that serves the suggestion type
   *   ml:
   *     The name of the intent as determined by `MLSuggest`
   *   rust:
   *     The name of the suggestion type as defined in Rust
   *
   * @param {string} options.suggestionType
   *   This value is only relevant to dynamic Rust suggestions. It is
   *   `suggestion.suggestionType` value, the dynamic Rust suggestion type.
   * @returns {SuggestProvider}
   *   The feature instance or null if none was found.
   */
  getFeatureBySource({ source, provider, suggestionType }) {
    switch (source) {
      case "merino":
        return this.#featuresByMerinoProvider.get(provider);
      case "rust":
        if (provider == "Dynamic" && suggestionType) {
          let dynamicFeature =
            this.#featuresByDynamicRustSuggestionType.get(suggestionType);
          if (dynamicFeature) {
            return dynamicFeature;
          }
        }
        return this.#featuresByRustSuggestionType.get(provider);
      case "ml":
        return this.getFeatureByMlIntent(provider);
    }
    return null;
  }

  /**
   * Registers a dismissal with the Rust backend. A
   * `quicksuggest-dismissals-changed` notification topic is sent when done.
   *
   * @param {UrlbarResult} result
   *   The result to dismiss.
   */
  async dismissResult(result) {
    if (result.payload.source == "rust") {
      await this.rustBackend?.dismissRustSuggestion(
        result.payload.suggestionObject
      );
    } else {
      let key = getDismissalKey(result);
      if (key) {
        await this.rustBackend?.dismissByKey(key);
      }
    }

    Services.obs.notifyObservers(null, "quicksuggest-dismissals-changed");
  }

  /**
   * Returns whether a dismissal is recorded for a result.
   *
   * @param {UrlbarResult} result
   *   The result to check.
   * @returns {Promise<boolean>}
   *   Whether the result has been dismissed.
   */
  async isResultDismissed(result) {
    let promises = [
      // Check whether the result was dismissed using the old API, where
      // dismissals were recorded as URL digests.
      getDigest(result.payload.originalUrl || result.payload.url).then(digest =>
        this.rustBackend?.isDismissedByKey(digest)
      ),
    ];

    if (result.payload.source == "rust") {
      promises.push(
        this.rustBackend?.isRustSuggestionDismissed(
          result.payload.suggestionObject
        )
      );
    } else {
      let key = getDismissalKey(result);
      if (key) {
        promises.push(this.rustBackend?.isDismissedByKey(key));
      }
    }

    let values = await Promise.all(promises);
    return values.some(v => !!v);
  }

  /**
   * Clears all dismissed suggestions, including individually dismissed
   * suggestions and dismissed suggestion types. The following notification
   * topics are sent when done, in this order:
   *
   * ```
   * quicksuggest-dismissals-changed
   * quicksuggest-dismissals-cleared
   * ```
   */
  async clearDismissedSuggestions() {
    // Clear the user value of each feature's primary user-controlled pref if
    // its value is `false`.
    for (let [name, feature] of this.#featuresByName) {
      for (let pref of feature.primaryUserControlledPreferences) {
        // This should never throw, but try-catch to avoid breaking the entire
        // loop if `UrlbarPrefs` doesn't recognize a pref in one iteration.
        try {
          if (pref && !lazy.UrlbarPrefs.get(pref)) {
            lazy.UrlbarPrefs.clear(pref);
          }
        } catch (error) {
          this.logger.error("Error clearing primaryEnablingPreference", {
            "feature.name": name,
            pref,
            error,
          });
        }
      }
    }

    // Clear individually dismissed suggestions, which are stored in the Rust
    // component regardless of their source.
    await this.rustBackend?.clearDismissedSuggestions();

    Services.obs.notifyObservers(null, "quicksuggest-dismissals-changed");
    Services.obs.notifyObservers(null, "quicksuggest-dismissals-cleared");
  }

  /**
   * Whether there are any dismissed suggestions that can be cleared, including
   * individually dismissed suggestions and dismissed suggestion types.
   *
   * @returns {Promise<boolean>}
   *   Whether dismissals can be cleared.
   */
  async canClearDismissedSuggestions() {
    // Return true if any feature's primary user-controlled pref is `false` on
    // the user branch.
    for (let [name, feature] of this.#featuresByName) {
      for (let pref of feature.primaryUserControlledPreferences) {
        // This should never throw, but try-catch to avoid breaking the entire
        // loop if `UrlbarPrefs` doesn't recognize a pref in one iteration.
        try {
          if (
            pref &&
            !lazy.UrlbarPrefs.get(pref) &&
            lazy.UrlbarPrefs.hasUserValue(pref)
          ) {
            return true;
          }
        } catch (error) {
          this.logger.error(
            "Error accessing primaryUserControlledPreferences",
            {
              "feature.name": name,
              pref,
              error,
            }
          );
        }
      }
    }

    // Return true if there are any individually dismissed suggestions.
    if (await this.rustBackend?.anyDismissedSuggestions()) {
      return true;
    }

    return false;
  }

  /**
   * Gets the intended default Suggest prefs for a home region and locale.
   *
   * @param {string} region
   *   A home region, typically from `Region.home`.
   * @param {string} locale
   *   A locale.
   * @returns {object}
   *   An object that maps pref names to their intended default values. Pref
   *   names are relative to `browser.urlbar.`.
   */
  intendedDefaultPrefs(region, locale) {
    let regionLocalePrefs = Object.fromEntries(
      Object.entries(SUGGEST_PREFS)
        .map(([prefName, { defaultValues }]) => {
          if (defaultValues?.hasOwnProperty(region)) {
            let [enablingLocales, prefValue] = defaultValues[region];
            if (enablingLocales.includes(locale)) {
              if (typeof prefValue == "function") {
                prefValue = prefValue();
              }
              return [prefName, prefValue];
            }
          }
          return null;
        })
        .filter(entry => !!entry)
    );
    return {
      ...this.#unmodifiedDefaultPrefs,
      ...regionLocalePrefs,
    };
  }

  /**
   * Called when a urlbar pref changes.
   *
   * @param {string} pref
   *   The name of the pref relative to `browser.urlbar`.
   */
  onPrefChanged(pref) {
    // If the Terms of Use (ToU) pref changed, recalculate pref defaults since
    // some prefs depend on it.
    if (pref == lazy.TelemetryReportingPolicy.TOU_ACCEPTED_DATE_PREF) {
      this.#initPrefs();
    }

    // If any feature's enabling preferences changed, update it now.
    let features = this.#featuresByEnablingPrefs.get(pref);
    if (!features) {
      return;
    }

    let isPrimaryUserControlledPref = false;

    for (let f of features) {
      f.update();
      if (f.primaryUserControlledPreferences.includes(pref)) {
        isPrimaryUserControlledPref = true;
      }
    }

    if (isPrimaryUserControlledPref) {
      Services.obs.notifyObservers(null, "quicksuggest-dismissals-changed");
    }
  }

  /**
   * Called when a urlbar Nimbus variable changes.
   *
   * @param {string} variable
   *   The name of the variable.
   */
  onNimbusChanged(variable) {
    // If a change occurred to a variable that corresponds to a pref exposed in
    // the UI, sync the variable to the pref on the default branch.
    this.#syncNimbusVariablesToUiPrefs(variable);

    // Update features.
    this.#updateAll();
  }

  /**
   * Returns whether a given URL and result URL map back to the same original
   * suggestion URL.
   *
   * Some features may create result URLs that are potentially unique per query.
   * Typically this is done by modifying an original suggestion URL at query
   * time, for example by adding timestamps or query-specific search params. In
   * that case, a single original suggestion URL will map to many result URLs.
   * This function returns whether the given URL and result URL are equal
   * excluding any such modifications.
   *
   * @param {string} url
   *   The URL to check, typically from the user's history.
   * @param {UrlbarResult} result
   *   The Suggest result.
   * @returns {boolean}
   *   Whether `url` is equivalent to the result's URL.
   */
  isUrlEquivalentToResultUrl(url, result) {
    let feature = this.getFeatureByResult(result);
    return feature
      ? feature.isUrlEquivalentToResultUrl(url, result)
      : url == result.payload.url;
  }

  /**
   * Returns the title and highlights for suggestions that should display their
   * full keywords.
   *
   * When `fullKeyword` is defined, highlighting will be applied only to it, not
   * to the title as a whole; otherwise highlighting will not be applied at all.
   * It's unclear if that's the intended UI spec, but historically it's how
   * highlighting has been implemented for suggestions that should display their
   * full keywords.
   *
   * @param {object} options
   * @param {Array} options.tokens
   *   It is compatible to UrlbarQueryContext.tokens.
   * @param {Values<typeof lazy.lazy.UrlbarShared.HIGHLIGHT>} [options.highlightType]
   * @param {string} [options.fullKeyword]
   *   Full keyword if there is.
   * @param {string} options.title
   *   Suggestion title.
   * @returns {object} { value, highlights }
   *   The value will be used for title.
   *   The highlights will be created by UrlbarShared.getTokenMatches().
   */
  getFullKeywordTitleAndHighlights({
    tokens,
    highlightType,
    fullKeyword,
    title,
  }) {
    return {
      value: fullKeyword ? `${fullKeyword} — ${title}` : title,
      highlights: fullKeyword
        ? lazy.UrlbarShared.getTokenMatches(tokens, fullKeyword, highlightType)
        : [],
    };
  }

  /**
   * @returns {object}
   *   An object that maps from Nimbus variable names to their corresponding
   *   prefs, for prefs in `SUGGEST_PREFS` with `nimbusVariableIfExposedInUi`
   *   set.
   */
  get #uiPrefsByNimbusVariable() {
    return Object.fromEntries(
      Object.entries(SUGGEST_PREFS)
        .map(([prefName, { nimbusVariableIfExposedInUi }]) =>
          nimbusVariableIfExposedInUi
            ? [nimbusVariableIfExposedInUi, prefName]
            : null
        )
        .filter(entry => !!entry)
    );
  }

  /**
   * Sets appropriate default-branch values of Suggest prefs depending on
   * whether Suggest should be enabled by default.
   *
   * @param {object} testOverrides
   *   This is intended for tests only. Pass to force the following:
   *   `{ region, locale, migrationVersion, defaultPrefs }`
   */
  #initPrefs(testOverrides = null) {
    // Updating prefs is tricky and it's important to preserve the user's
    // choices, so we describe the process in detail below. tl;dr:
    //
    // * Prefs exposed in the settings UI should be sticky.
    // * Prefs that are both exposed in the settings UI and configurable via
    //   Nimbus should be added to `SUGGEST_PREFS` with
    //   `nimbusVariableIfExposedInUi` set appropriately.
    // * Prefs with `nimbusVariableIfExposedInUi` set should not be specified as
    //   `fallbackPref` for their Nimbus variables. Access these prefs directly
    //   instead of through their variables.
    //
    // The pref-update process is described next.
    //
    // 1. Determine the appropriate values for Suggest prefs according to the
    //    user's home region and locale.
    //
    // 2. Set the prefs on the default branch. We use the default branch and not
    //    the user branch because we want to distinguish default prefs from the
    //    user's choices.
    //
    //    In particular it's important to consider prefs that are exposed in the
    //    UI, like whether sponsored suggestions are enabled. Once the user
    //    makes a choice to change a default, we want to preserve that choice
    //    indefinitely regardless of whether Suggest is currently enabled or
    //    will be enabled in the future. User choices are of course recorded on
    //    the user branch, so if we set defaults on the user branch too, we
    //    wouldn't be able to distinguish user choices from default values. This
    //    is also why prefs that are exposed in the UI should be sticky. Unlike
    //    non-sticky prefs, sticky prefs retain their user-branch values even
    //    when those values are the same as the ones on the default branch.
    //
    //    It's important to note that the defaults we set here do not persist
    //    across app restarts. (This is a feature of the pref service; prefs set
    //    programmatically on the default branch are not stored anywhere
    //    permanent like firefox.js or user.js.) That's why BrowserGlue calls
    //    `init()` on every startup.
    //
    // 3. Some prefs are both exposed in the UI and configurable via Nimbus,
    //    like whether data collection is enabled. We absolutely want to
    //    preserve the user's past choices for these prefs. But if the user
    //    hasn't yet made a choice for a particular pref, then it should be
    //    configurable.
    //
    //    For any such prefs that have values defined in Nimbus, we set their
    //    default-branch values to their Nimbus values. (These defaults
    //    therefore override any set in the previous step.) If a pref has a user
    //    value, accessing the pref will return the user value; if it does not
    //    have a user value, accessing it will return the value that was
    //    specified in Nimbus.
    //
    //    This isn't strictly necessary. Since prefs exposed in the UI are
    //    sticky, they will always preserve their user-branch values regardless
    //    of their default-branch values, and as long as a pref is listed as a
    //    `fallbackPref` for its corresponding Nimbus variable, Nimbus will use
    //    the user-branch value. So we could instead specify fallback prefs in
    //    Nimbus and always access values through Nimbus instead of through
    //    prefs. But that would make preferences UI code a little harder to
    //    write since the checked state of a checkbox would depend on something
    //    other than its pref. Since we're already setting default-branch values
    //    here as part of the previous step, it's not much more work to set
    //    defaults for these prefs too, and it makes the UI code a little nicer.
    //
    // 4. Migrate prefs as necessary. This refers to any pref changes that are
    //    neccesary across app versions: introducing and initializing new prefs,
    //    removing prefs, or changing the meaning of existing prefs.

    // We use `Preferences` because it lets us access prefs without worrying
    // about their types and can do so on the default branch. Most of our prefs
    // are bools but not all.
    let defaults = new lazy.Preferences({
      branch: "browser.urlbar.",
      defaultBranch: true,
    });

    // Before setting defaults, save their original unmodifed values as defined
    // in `firefox.js` so we can restore them if Suggest becomes disabled.
    if (!this.#unmodifiedDefaultPrefs) {
      this.#unmodifiedDefaultPrefs = Object.fromEntries(
        Object.keys(SUGGEST_PREFS).map(name => [name, defaults.get(name)])
      );
    }

    // 1. Determine the appropriate values for Suggest prefs according to the
    //    user's home region and locale.
    if (testOverrides?.defaultPrefs) {
      this.#intendedDefaultPrefs = testOverrides.defaultPrefs;
    } else {
      let region = testOverrides?.region ?? lazy.Region.home;
      let locale = testOverrides?.locale ?? Services.locale.appLocaleAsBCP47;
      this.#intendedDefaultPrefs = this.intendedDefaultPrefs(region, locale);
    }

    // 2. Set the prefs on the default branch.
    for (let [name, value] of Object.entries(this.#intendedDefaultPrefs)) {
      defaults.set(name, value);
    }

    // 3. Set default-branch values for prefs that are both exposed in the
    //    settings UI and configurable via Nimbus.
    this.#syncNimbusVariablesToUiPrefs();

    // 4. Migrate user-branch prefs across app versions.
    let shouldEnableSuggest =
      !!this.#intendedDefaultPrefs["quicksuggest.enabled"];
    this.#ensureUserPrefsMigrated(shouldEnableSuggest, testOverrides);
  }

  /**
   * Sets default-branch values for prefs in `#uiPrefsByNimbusVariable`, i.e.,
   * prefs that are both exposed in the settings UI and configurable via Nimbus.
   *
   * @param {string} variable
   *   If defined, only the pref corresponding to this variable will be set. If
   *   there is no UI pref for this variable, this function is a no-op.
   */
  #syncNimbusVariablesToUiPrefs(variable = null) {
    let prefsByVariable = this.#uiPrefsByNimbusVariable;

    if (variable) {
      if (!prefsByVariable.hasOwnProperty(variable)) {
        // `variable` does not correspond to a pref exposed in the UI.
        return;
      }
      // Restrict `prefsByVariable` only to `variable`.
      prefsByVariable = { [variable]: prefsByVariable[variable] };
    }

    let defaults = new lazy.Preferences({
      branch: "browser.urlbar.",
      defaultBranch: true,
    });

    for (let [v, pref] of Object.entries(prefsByVariable)) {
      let value = lazy.NimbusFeatures.urlbar.getVariable(v);
      if (value === undefined) {
        value = this.#intendedDefaultPrefs[pref];
      }
      defaults.set(pref, value);
    }
  }

  /**
   * Updates all features.
   */
  #updateAll() {
    // IMPORTANT: This method is a `NimbusFeatures.urlbar.onUpdate()` callback,
    // which means it's called on every change to any pref that is a fallback
    // for a urlbar Nimbus variable.

    // Update features.
    for (let feature of this.#featuresByName.values()) {
      feature.update();
    }
  }

  /**
   * The current version of the Firefox Suggest prefs.
   *
   * @returns {number}
   */
  get MIGRATION_VERSION() {
    return 7;
  }

  /**
   * Migrates user-branch Suggest prefs to the current version if they haven't
   * been migrated already.
   *
   * @param {boolean} shouldEnableSuggest
   *   Whether Suggest should be enabled right now.
   * @param {object} testOverrides
   *   This is intended for tests only. Pass to force a migration version:
   *   `{ migrationVersion }`
   */
  #ensureUserPrefsMigrated(shouldEnableSuggest, testOverrides) {
    let currentVersion =
      testOverrides?.migrationVersion !== undefined
        ? testOverrides.migrationVersion
        : this.MIGRATION_VERSION;
    let lastSeenVersion = Math.max(
      0,
      lazy.UrlbarPrefs.get("quicksuggest.migrationVersion")
    );
    if (currentVersion <= lastSeenVersion) {
      // Migration up to date.
      return;
    }

    // Migrate from the last-seen version up to the current version.
    let userBranch = Services.prefs.getBranch("browser.urlbar.");
    let version = lastSeenVersion;
    for (; version < currentVersion; version++) {
      let nextVersion = version + 1;
      let methodName = "_migrateUserPrefsTo_" + nextVersion;
      try {
        this[methodName](userBranch, shouldEnableSuggest);
      } catch (error) {
        console.error(
          `Error migrating Firefox Suggest prefs to version ${nextVersion}:`,
          error
        );
        break;
      }
    }

    // Record the new last-seen migration version.
    lazy.UrlbarPrefs.set("quicksuggest.migrationVersion", version);
  }

  _migrateUserPrefsTo_1(userBranch, shouldEnableSuggest) {
    // Previously prefs were unversioned and worked like this: When
    // `suggest.quicksuggest` is false, all quick suggest results are disabled
    // and `suggest.quicksuggest.sponsored` is ignored. To show sponsored
    // suggestions, both prefs must be true.
    //
    // Version 1 makes the following changes:
    //
    // `suggest.quicksuggest` is removed, `suggest.quicksuggest.nonsponsored` is
    // introduced. `suggest.quicksuggest.nonsponsored` and
    // `suggest.quicksuggest.sponsored` are independent:
    // `suggest.quicksuggest.nonsponsored` controls non-sponsored results and
    // `suggest.quicksuggest.sponsored` controls sponsored results.
    // `quicksuggest.dataCollection.enabled` is introduced.

    // Copy `suggest.quicksuggest` to `suggest.quicksuggest.nonsponsored` and
    // clear the first.
    if (userBranch.prefHasUserValue("suggest.quicksuggest")) {
      userBranch.setBoolPref(
        "suggest.quicksuggest.nonsponsored",
        userBranch.getBoolPref("suggest.quicksuggest")
      );
      userBranch.clearUserPref("suggest.quicksuggest");
    }

    // In the unversioned prefs, sponsored suggestions were shown only if the
    // main suggestions pref `suggest.quicksuggest` was true, but now there are
    // two independent prefs, so disable sponsored if the main pref was false.
    if (
      shouldEnableSuggest &&
      userBranch.prefHasUserValue("suggest.quicksuggest.nonsponsored") &&
      !userBranch.getBoolPref("suggest.quicksuggest.nonsponsored")
    ) {
      // Set the pref on the user branch. Suggestions are enabled by default
      // for offline; we want to preserve the user's choice of opting out,
      // and we want to preserve the default-branch true value.
      userBranch.setBoolPref("suggest.quicksuggest.sponsored", false);
    }
  }

  _migrateUserPrefsTo_2(userBranch) {
    // For online, the defaults for `suggest.quicksuggest.nonsponsored` and
    // `suggest.quicksuggest.sponsored` are now true. Previously they were
    // false.

    // In previous versions of the prefs for online, suggestions were disabled
    // by default; in version 2, they're enabled by default. For users who were
    // already in online and did not enable suggestions (because they did not
    // opt in, they did opt in but later disabled suggestions, or they were not
    // shown the modal) we don't want to suddenly enable them, so if the prefs
    // do not have user-branch values, set them to false.
    let scenario = userBranch.getCharPref("quicksuggest.scenario", "");
    if (scenario == "online") {
      if (!userBranch.prefHasUserValue("suggest.quicksuggest.nonsponsored")) {
        userBranch.setBoolPref("suggest.quicksuggest.nonsponsored", false);
      }
      if (!userBranch.prefHasUserValue("suggest.quicksuggest.sponsored")) {
        userBranch.setBoolPref("suggest.quicksuggest.sponsored", false);
      }
    }
  }

  _migrateUserPrefsTo_3() {
    // This used to check the `quicksuggest.dataCollection.enabled` preference
    // and set `quicksuggest.settingsUi` to `SETTINGS_UI.FULL` if data collection
    // was enabled. However, this is now cleared for everyone in the v4 migration,
    // hence there is nothing to do here.
  }

  _migrateUserPrefsTo_4(userBranch) {
    // This will reset the pref to the default value, i.e. SETTINGS_UI.OFFLINE_ONLY
    // for users where suggest is enabled, or SETTINGS_UI.NONE where it is not
    // enabled.
    userBranch.clearUserPref("quicksuggest.settingsUi");
  }

  _migrateUserPrefsTo_5(userBranch) {
    // This migration clears the sponsored pref for region-locales where, at the
    // time of this migration, the Suggest technical platform is enabled
    // (`quicksuggest.enabled` is true) but features that are part of the
    // Suggest brand are not. It was incorrectly set to false on the user branch
    // due to the combination of two things:
    //
    // 1. In 146, bug 1992811 enabled the Suggest platform for `en` locales in
    //    DE, FR, and IT in order to ship important-dates suggestions, which
    //    aren't considered part of the Suggest brand. For these region-locales,
    //    `quicksuggest.enabled` was defaulted to true and the sponsored and
    //    nonsponsored prefs retained their false values from `firefox.js`.
    // 2. A previous implementation of the version 1 migration incorrectly set
    //    the sponsored pref to false on the user branch if
    //    `quicksuggest.enabled` is true and the nonsponsored pref is false on
    //    either the user or default branch. The migration should have only
    //    checked the user branch and has since been fixed.
    if (
      ["DE", "FR", "IT"].includes(lazy.Region.home) &&
      EN_LOCALES.includes(Services.locale.appLocaleAsBCP47)
    ) {
      userBranch.clearUserPref("suggest.quicksuggest.sponsored");
    }
  }

  _migrateUserPrefsTo_6(userBranch) {
    // Firefox 146 no longer uses `suggest.quicksuggest.nonsponsored` and stops
    // setting it on the default branch. It introduces
    // `suggest.quicksuggest.all`, which now controls all suggestions that are
    // part of the Suggest brand, both sponsored and nonsponsored. To show
    // nonsponsored suggestions, `all` must be true. To show sponsored
    // suggestions, both `all` and `suggest.quicksuggest.sponsored` must be
    // true.
    //
    // This migration copies the user-branch value of `nonsponsored` to the new
    // `all` pref. We keep the user-branch value in case we need it later.
    if (userBranch.prefHasUserValue("suggest.quicksuggest.nonsponsored")) {
      userBranch.setBoolPref(
        "suggest.quicksuggest.all",
        userBranch.getBoolPref("suggest.quicksuggest.nonsponsored")
      );
    }
  }

  _migrateUserPrefsTo_7(userBranch) {
    // Firefox 149: Make the "Show less frequently" behavior of addon
    // suggestions consistent with other suggestion types. This reverts the fix
    // to bug 1836582 and goes back to using `addons.minKeywordLength`.
    if (
      userBranch.prefHasUserValue("addons.minKeywordLength") &&
      !userBranch.prefHasUserValue("addons.showLessFrequentlyCount")
    ) {
      // The user clicked "Show less frequently" before bug 1836582 was fixed
      // since `minKeywordLength` has a user value, but they haven't clicked it
      // again since `showLessFrequentlyCount` does not have a user value. Set
      // `showLessFrequentlyCount` to 1 and keep `minKeywordLength` the same.
      userBranch.setIntPref("addons.showLessFrequentlyCount", 1);
    } else if (
      !userBranch.prefHasUserValue("addons.minKeywordLength") &&
      userBranch.prefHasUserValue("addons.showLessFrequentlyCount")
    ) {
      // The user clicked "Show less frequently" after bug 1836582 was fixed but
      // not before. We need to set `minKeywordLength` to something but we can't
      // know what. Err on the side of not bothering the user by using the max
      // keyword length as of 149. This will effectively disable addon
      // suggestions unless/until longer keywords are added.
      userBranch.setIntPref("addons.minKeywordLength", 20);
    }
  }

  async _test_reset(testOverrides = null) {
    if (this.#initStarted) {
      await this.initPromise;
    }

    if (this.rustBackend) {
      await this.rustBackend.ingestPromise;
    }

    this.#initPrefs(testOverrides);
    this.#updateAll();
    if (this.rustBackend) {
      // `#updateAll()` triggers ingest, so wait for it to finish.
      await this.rustBackend.ingestPromise;
    }
  }

  #initStarted = false;
  #initResolvers = Promise.withResolvers();

  // Maps from Suggest feature class names to feature instances.
  #featuresByName = new Map();

  // Maps from Merino provider names to Suggest feature instances.
  #featuresByMerinoProvider = new Map();

  // Maps from Rust suggestion types to Suggest feature instances.
  #featuresByRustSuggestionType = new Map();

  // Maps from dynamic Rust suggestion types to Suggest feature instances.
  // Features that manage a dynamic Rust suggestion type will be in this map
  // instead of `#featuresByRustSuggestionType`.
  #featuresByDynamicRustSuggestionType = new Map();

  // Maps from ML intent strings to Suggest feature instances.
  #featuresByMlIntent = new Map();

  // Maps from preference names to the `Set` of feature instances they enable.
  #featuresByEnablingPrefs = new Map();

  // A plain JS object that maps pref names relative to `browser.urlbar.` to
  // their intended defaults depending on whether Suggest should be enabled.
  #intendedDefaultPrefs;

  // A plain JS object that maps pref names relative to `browser.urlbar.` to
  // their original unmodified values as defined in `firefox.js`.
  #unmodifiedDefaultPrefs;
}

/**
 * Returns whether the user has accepted the Firefox Terms of Use (ToU)
 * pertaining to Suggest.
 *
 * @returns {boolean}
 *   Whether the user accepted the ToU.
 */
function userAcceptedSuggestToU() {
  let date = lazy.TelemetryReportingPolicy.termsOfUseAcceptedDate;
  return !!date && SUGGEST_TOU_TIMESTAMP <= date.getTime();
}

/**
 * Returns whether online Suggest should be available to the user *excluding
 * consideration of the user's region and locale*.
 *
 * @returns {boolean}
 *   Whether online Suggest should be available, excluding region and locale
 *   checks.
 */
function shouldOnlineBeAvailable() {
  return userAcceptedSuggestToU();
}

function getDismissalKey(result) {
  return (
    result.payload.dismissalKey ||
    result.payload.originalUrl ||
    result.payload.url
  );
}

async function getDigest(string) {
  let stringArray = new TextEncoder().encode(string);
  let hashBuffer = await crypto.subtle.digest("SHA-1", stringArray);
  let hashArray = new Uint8Array(hashBuffer);
  return Array.from(hashArray, b => b.toString(16).padStart(2, "0")).join("");
}

export const QuickSuggest = new _QuickSuggest();
