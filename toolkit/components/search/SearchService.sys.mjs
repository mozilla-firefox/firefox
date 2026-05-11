/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AppProvidedConfigEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  AddonSearchEngine:
    "moz-src:///toolkit/components/search/AddonSearchEngine.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  IgnoreLists: "resource://gre/modules/IgnoreLists.sys.mjs",
  loadAndParseOpenSearchEngine:
    "moz-src:///toolkit/components/search/OpenSearchLoader.sys.mjs",
  OpenSearchEngine:
    "moz-src:///toolkit/components/search/OpenSearchEngine.sys.mjs",
  PolicySearchEngine:
    "moz-src:///toolkit/components/search/PolicySearchEngine.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchEngine: "moz-src:///toolkit/components/search/SearchEngine.sys.mjs",
  SearchEngineInstallError:
    "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  SearchEngineSelector:
    "moz-src:///toolkit/components/search/SearchEngineSelector.sys.mjs",
  SearchSettings: "moz-src:///toolkit/components/search/SearchSettings.sys.mjs",
  SearchStaticData:
    "moz-src:///toolkit/components/search/SearchStaticData.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  UserInstalledConfigEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  UserSearchEngine:
    "moz-src:///toolkit/components/search/UserSearchEngine.sys.mjs",
  logConsole: () =>
    console.createInstance({
      prefix: "SearchService",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
  timerManager: {
    service: "@mozilla.org/updates/timer-manager;1",
    iid: Ci.nsIUpdateTimerManager,
  },
  idleService: {
    service: "@mozilla.org/widget/useridleservice;1",
    iid: Ci.nsIUserIdleService,
  },
  defaultOverrideAllowlist: () => {
    return new SearchDefaultOverrideAllowlistHandler();
  },
});

/**
 * @import {AddonSearchEngine} from "./AddonSearchEngine.sys.mjs"
 * @import {OpenSearchEngine} from "./OpenSearchEngine.sys.mjs"
 * @import {SearchEngine} from "./SearchEngine.sys.mjs"
 * @import {SearchEngineSelector} from "./SearchEngineSelector.sys.mjs"
 * @import {
 *   RefinedSearchConfig,
 *   SearchEngineDefinition,
 * } from "../uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs";
 * @import {UserSearchEngine, FormInfo} from "./UserSearchEngine.sys.mjs"
 */

const TOPIC_LOCALES_CHANGE = "intl:app-locales-changed";
const QUIT_APPLICATION_TOPIC = "quit-application";

// The update timer for OpenSearch engines checks in once a day.
const OPENSEARCH_UPDATE_TIMER_TOPIC = "search-engine-update-timer";
const OPENSEARCH_UPDATE_TIMER_INTERVAL = 60 * 60 * 24;

// This is the amount of time we'll be idle for before applying any configuration
// changes.
const RECONFIG_IDLE_TIME_SEC = 5 * 60;

// The key for the metadata we store about whether to prompt users to
// install engines they are using.
const ENGINES_SEEN_KEY = "contextual-engines-seen";

// Value we store to indicate prompt should not be shown.
const DONT_SHOW_PROMPT = -1;

// Amount of times the engine has to be used before prompting.
const ENGINES_SEEN_FOR_PROMPT = 1;

/**
 * The search service handles loading and maintaining of search engines. It will
 * also work out the default lists for each locale/region.
 */
export const SearchService = new (class SearchService {
  constructor() {
    this._settings = new lazy.SearchSettings(this);
  }

  /**
   * A reason that is used in the change of default search engine event telemetry.
   * These are mutally exclusive.
   */
  CHANGE_REASON = Object.freeze({
    // The cause of the change is unknown.
    UNKNOWN: "unknown",
    // The user changed the default search engine via the options in the
    // preferences UI.
    USER: "user",
    // The change resulted from the user toggling the "Use this search engine in
    // Private Windows" option in the preferences UI.
    USER_PRIVATE_SPLIT: "user_private_split",
    // The user changed the default via keys (cmd/ctrl-up/down) in the separate
    // search bar.
    USER_SEARCHBAR: "user_searchbar",
    // The user changed the default via context menu on the one-off buttons in the
    // separate search bar.
    USER_SEARCHBAR_CONTEXT: "user_searchbar_context",
    // An add-on requested the change of default on install, which was either
    // accepted automatically or by the user.
    ADDON_INSTALL: "addon-install",
    // An add-on was uninstalled, which caused the engine to be uninstalled.
    ADDON_UNINSTALL: "addon-uninstall",
    // A configuration update caused a change of default.
    CONFIG: "config",
    // A locale update caused a change of default.
    LOCALE: "locale",
    // A region update caused a change of default.
    REGION: "region",
    // Turning on/off an experiment caused a change of default.
    EXPERIMENT: "experiment",
    // An enterprise policy caused a change of default.
    ENTERPRISE: "enterprise",
    // The UI Tour caused a change of default.
    UITOUR: "uitour",
    // The engine updated.
    ENGINE_UPDATE: "engine-update",
    // When the private default UI is enabled (e.g. via toggling the preference
    // when an experiment is run).
    USER_PRIVATE_PREF_ENABLED: "user_private_pref_enabled",
    // An update to the search engine ignore list caused a change of default.
    ENGINE_IGNORE_LIST_UPDATED: "ignore-list",
    // There was no default engine in the settings or it was hidden, so we found
    // a new default engine.
    NO_EXISTING_DEFAULT_ENGINE: "no-existing-default",
  });

  /**
   * The currently active search engine.
   * Unless the application doesn't ship any search engine, this should never
   * be null. If the currently active engine is removed, this attribute will
   * fallback first to the application default engine if it's not hidden, then to
   * the first visible engine, and as a last resort it will unhide the app
   * default engine.
   */
  get defaultEngine() {
    this.#ensureInitialized();
    return this._getEngineDefault(false);
  }

  /**
   * The currently active search engine for private browsing mode.
   *
   * @see defaultEngine
   */
  get defaultPrivateEngine() {
    this.#ensureInitialized();
    return this._getEngineDefault(this.#separatePrivateDefault);
  }

  /**
   * The currently active search engine.
   * Unless the application doesn't ship any search engine, this should never
   * be null. If the currently active engine is removed, this attribute will
   * fallback first to the application default engine if it's not hidden, then to
   * the first visible engine, and as a last resort it will unhide the app
   * default engine.
   */
  async getDefault() {
    await this.init();
    return this.defaultEngine;
  }

  /**
   * Sets the currently active search engine.
   *
   * @param {SearchEngine} engine
   *   The engine to set the default to.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason the default engine is being changed, used for recording to
   *   telemetry.
   */
  async setDefault(engine, changeReason) {
    await this.init();
    this.#setEngineDefault(false, engine, changeReason);
  }

  /**
   * The currently active search engine for private browsing mode.
   *
   * @see defaultPrivateEngine
   */
  async getDefaultPrivate() {
    await this.init();
    return this.defaultPrivateEngine;
  }

  /**
   * Sets the currently active default private search engine.
   *
   * @param {SearchEngine} engine
   *   The engine to set the default to.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason the default engine is being changed, used for recording to
   *   telemetry.
   */
  async setDefaultPrivate(engine, changeReason) {
    await this.init();
    if (!this.#lazyPrefs.separatePrivateDefaultPrefValue) {
      Services.prefs.setBoolPref(
        lazy.SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
        true
      );
    }
    this.#setEngineDefault(this.#separatePrivateDefault, engine, changeReason);
  }

  /**
   * @returns {SearchEngine}
   *   The engine that is the default for this locale/region, ignoring any
   *   user changes to the default engine.
   */
  get appDefaultEngine() {
    return this.#appDefaultEngine();
  }

  /**
   * @returns {SearchEngine}
   *   The engine that is the default for this locale/region in private browsing
   *   mode, ignoring any user changes to the default engine.
   *   Note: if there is no default for this locale/region, then the non-private
   *   browsing engine will be returned.
   */
  get appPrivateDefaultEngine() {
    return this.#appDefaultEngine(this.#separatePrivateDefault);
  }

  /**
   * Determine whether initialization has been completed.
   *
   * Clients of the service can use this attribute to quickly determine whether
   * initialization is complete, and decide to trigger some immediate treatment,
   * to launch asynchronous initialization or to bailout.
   *
   * Note that this attribute does not indicate that initialization has
   * succeeded, use hasSuccessfullyInitialized() for that.
   *
   * @returns {boolean}
   *  |true | if the search service has finished its attempt to initialize and
   *          we have an outcome. It could have failed or succeeded during this
   *          process.
   *  |false| if initialization has not been triggered yet or initialization is
   *          still ongoing.
   */
  get isInitialized() {
    return (
      this.#initializationStatus == "success" ||
      this.#initializationStatus == "failed"
    );
  }

  /**
   * Determine whether initialization has been successfully completed.
   *
   * @returns {boolean}
   *  |true | if the search service has succesfully initialized.
   *  |false| if initialization has not been started yet, initialization is
   *          still ongoing or initializaiton has failed.
   */
  get hasSuccessfullyInitialized() {
    return this.#initializationStatus == "success";
  }

  /**
   * A promise that is resolved when initialization has finished. This does not
   * trigger initialization to begin.
   *
   * @returns {Promise<void>}
   *   Resolved when initalization has successfully finished, and rejected if it
   *   has failed.
   */
  get promiseInitialized() {
    return this.#initDeferredPromise.promise;
  }

  /**
   * Gets a representation of the default engine in an anonymized JSON
   * string suitable for recording in the Telemetry environment.
   *
   * @typedef {object} SearchEngineTelemetryInfo
   * @property {string} name
   *   The user given name of the search engine.
   * @property {string} loadPath
   *   The load path for the search engine.
   * @property {string} [submissionURL]
   *   The submission URL for the search engine, only reported for a select
   *   list of domains. See `#getEngineInfo()` for more info.
   *
   * @typedef {object} EnginesTelemetryInfo
   *   Contains anonymized info about the default engine(s).
   * @property {string} defaultSearchEngine
   *   The telemetry id of the default engine.
   * @property {SearchEngineTelemetryInfo} defaultSearchEngineData
   *   Information about the default engine.
   * @property {string} [defaultPrivateSearchEngine]
   *   Only returned if the preference for having a separate engine in private
   *   mode is turned on.
   *   The telemetry id of the default engine for private browsing mode.
   * @property {SearchEngineTelemetryInfo} [defaultPrivateSearchEngineData]
   *   Only returned if the preference for having a separate engine in private
   *   mode is turned on.
   *   Information about the default engine for private browsing mode.
   *
   * @returns {EnginesTelemetryInfo}
   */
  getDefaultEngineInfo() {
    let engineInfo = this.#getEngineInfo(this.defaultEngine);
    /** @type {EnginesTelemetryInfo} */
    const result = {
      defaultSearchEngine: engineInfo.telemetryId,
      defaultSearchEngineData: {
        loadPath: engineInfo.loadPath,
        name: engineInfo.name,
      },
    };
    if (engineInfo.submissionURL) {
      result.defaultSearchEngineData.submissionURL = engineInfo.submissionURL;
    }

    if (this.#separatePrivateDefault) {
      let privateEngineInfo = this.#getEngineInfo(this.defaultPrivateEngine);
      result.defaultPrivateSearchEngine = privateEngineInfo.telemetryId;
      result.defaultPrivateSearchEngineData = {
        loadPath: privateEngineInfo.loadPath,
        name: privateEngineInfo.name,
      };
      if (privateEngineInfo.submissionURL) {
        result.defaultPrivateSearchEngineData.submissionURL =
          privateEngineInfo.submissionURL;
      }
    }

    return result;
  }

  /**
   * If possible, please call getEngineById() rather than getEngineByName()
   * because engines are stored as { id: object } in this._engine Map.
   *
   * Returns the engine associated with the name.
   *
   * @param {string} engineName
   *   The name of the engine.
   * @returns {?SearchEngine}
   *   The associated engine if found, null otherwise.
   */
  getEngineByName(engineName) {
    this.#ensureInitialized();
    return this.#getEngineByName(engineName);
  }

  /**
   * Returns the engine associated with the name without initialization checks.
   *
   * @param {string} engineName
   *   The name of the engine.
   * @returns {?SearchEngine}
   *   The associated engine if found, null otherwise.
   */
  #getEngineByName(engineName) {
    for (let engine of this._engines.values()) {
      if (engine.name == engineName) {
        return engine;
      }
    }

    return null;
  }

  /**
   * Returns the engine associated with the id.
   *
   * @param {string} engineId
   *   The id of the engine.
   * @returns {?SearchEngine}
   *   The associated engine if found, null otherwise.
   */
  getEngineById(engineId) {
    this.#ensureInitialized();
    return this._engines.get(engineId) || null;
  }

  /**
   * Returns the first search engine matching the provided alias
   * (case-insensitive).
   *
   * @param {string} alias
   *  The alias to look for.
   * @returns {Promise<?SearchEngine>}
   *  The first search engine matching the alias or null.
   */
  async getEngineByAlias(alias) {
    await this.init();
    alias = alias.toLocaleLowerCase();

    for (let engine of this._engines.values()) {
      for (let engineAlias of engine.aliases) {
        if (engineAlias.toLocaleLowerCase() == alias) {
          return engine;
        }
      }
    }
    return null;
  }

  /**
   * Returns an array of all installed search engines.
   * The array is sorted either to the user requirements or the default order.
   *
   * @returns {Promise<SearchEngine[]>}
   */
  async getEngines() {
    await this.init();
    lazy.logConsole.debug("getEngines: getting all engines");
    return this.#sortedEngines;
  }

  /**
   * Returns an array of all installed search engines whose hidden attribute is
   * false.
   * The array is sorted either to the user requirements or the default order.
   *
   * @returns {Promise<SearchEngine[]>}
   */
  async getVisibleEngines() {
    await this.init();
    lazy.logConsole.debug("getVisibleEngines: getting all visible engines");
    return this.#sortedVisibleEngines;
  }

  /**
   * Returns the current list of application provided engines.
   */
  async getAppProvidedEngines() {
    await this.init();

    return lazy.SearchUtils.sortEnginesByDefaults({
      engines: this.#sortedEngines.filter(
        e => e instanceof lazy.AppProvidedConfigEngine
      ),
      appDefaultEngine: this.appDefaultEngine,
      appPrivateDefaultEngine: this.appPrivateDefaultEngine,
    });
  }

  /**
   * Returns an engine definition if it's search url matches the host provided.
   *
   * @param {string} host
   *   The host to search for.
   */
  async findContextualSearchEngineByHost(host) {
    await this.init();
    let settings = await this._settings.get();
    let config =
      await this.#engineSelector.findContextualSearchEngineByHost(host);
    if (config) {
      return new lazy.UserInstalledConfigEngine({ config, settings });
    }
    return null;
  }

  /**
   * Returns whether the user should be given a prompt to install the
   * engine they are currently using. A prompt is shown after the
   * second time a user picks a contextual engine to search with. After
   * the second time the prompt should not be shown again.
   *
   * @param {SearchEngine} engine
   *   The engine to check.
   * @returns {Promise<boolean>}
   *   Whether or not to show the prompt.
   */
  async shouldShowInstallPrompt(engine) {
    let identifer = engine._loadPath;
    let seenEngines =
      this._settings.getMetaDataAttribute(ENGINES_SEEN_KEY) ?? {};

    if (!(identifer in seenEngines)) {
      seenEngines[identifer] = 1;
      this._settings.setMetaDataAttribute(ENGINES_SEEN_KEY, seenEngines);
      return false;
    }

    let value = seenEngines[identifer];
    if (value == DONT_SHOW_PROMPT) {
      return false;
    }

    if (value == ENGINES_SEEN_FOR_PROMPT) {
      seenEngines[identifer] = DONT_SHOW_PROMPT;
      this._settings.setMetaDataAttribute(ENGINES_SEEN_KEY, seenEngines);
      return true;
    }

    console.error(`Unexpected value ${value} in seenEngines`);
    return false;
  }

  /**
   * Starts initialisation if necessary, otherwise returns a promise which indicates
   * the state of initialisation.
   *
   * @returns {Promise<void>}
   *   Returns the pending Promise when #init has started but not yet finished.
   *   | Resolved | when initialization has successfully finished.
   *   | Rejected | when initialization has failed.
   */
  async init() {
    if (["started", "success", "failed"].includes(this.#initializationStatus)) {
      return this.promiseInitialized;
    }
    this.#initializationStatus = "started";
    return this.#init();
  }

  /**
   * Runs background checks for the search service. This is called from
   * BrowserGlue and may be run once per session if the user is idle for
   * long enough.
   */
  async runBackgroundChecks() {
    await this.init();
    await this.#migrateLegacyEngines();
    await this.#checkWebExtensionEngines();
    await this.#addOpenSearchTelemetry();
    await this.#removeAppProvidedExtensions();
  }

  /**
   * Test only - reset SearchService data. Ideally this should be replaced
   */
  reset() {
    lazy.logConsole.debug("Resetting search service.");
    if (this.#earlyObserversAdded) {
      Services.obs.removeObserver(this, lazy.Region.REGION_TOPIC);
      this.#earlyObserversAdded = false;
    }
    this.#initializationStatus = "not initialized";
    this.#initDeferredPromise = Promise.withResolvers();
    this.#startupExtensions = new Set();
    this._engines.clear();
    this._cachedSortedEngines = null;
    this.#currentEngine = null;
    this.#currentPrivateEngine = null;
    this.#searchDefault = null;
    this.#searchPrivateDefault = null;
    this.#maybeReloadDebounce = false;
    this._settings._batchTask?.disarm();
    if (this.#engineSelector) {
      this.#engineSelector.reset();
      this.#engineSelector = null;
    }
  }

  /**
   * Test-only function to set SearchService initialization status.
   *
   * @param {string} status
   */
  forceInitializationStatusForTests(status) {
    this.#initializationStatus = status;
  }

  /**
   * Test-only function
   */
  forceCurrentEngineToBeNull() {
    this.#currentEngine = null;
  }

  /**
   * Test only variable to indicate an error should occur during
   * search service initialization.
   *
   * @type {{type : string, message: string}}
   */
  errorToThrowInTest = { type: null, message: null };

  // Test-only function to reset just the engine selector so that it can
  // load a different configuration.
  resetEngineSelector() {
    this.#engineSelector = new lazy.SearchEngineSelector(
      this.#handleConfigurationUpdated.bind(this)
    );
  }

  /**
   * Resets the default engine to its app default engine value.
   */
  resetToAppDefaultEngine() {
    let appDefaultEngine = this.appDefaultEngine;
    appDefaultEngine.hidden = false;
    this.#setEngineDefault(false, appDefaultEngine, this.CHANGE_REASON.USER);

    let appPrivateDefaultEngine = this.appPrivateDefaultEngine;
    appPrivateDefaultEngine.hidden = false;
    this.#setEngineDefault(
      true,
      appPrivateDefaultEngine,
      this.CHANGE_REASON.USER
    );
  }

  /**
   * Allows the add-on manager to discover if a WebExtension based search engine
   * may change the default to a config search engine.
   * If that WebExtension is on the allow list, then it will override the
   * built-in engine's urls and parameters.
   *
   *  @param {object} extension
   *   The extension to load from.
   *  @returns {Promise<{canChangeToConfigEngine: boolean, canInstallEngine: boolean}>}
   *   canChangeToConfigEngine: indicates if the WebExtension engine may set the
   *     already installed config search engine as default.
   *   canInstallEngine: indicates if the WebExtension engine may be installed.
   */
  async maybeSetAndOverrideDefault(extension) {
    let searchProvider =
      extension.manifest.chrome_settings_overrides.search_provider;
    let engine = this.getEngineByName(searchProvider.name);
    if (!engine || !engine.isConfigEngine || engine.hidden) {
      // We should only default to config engines.
      // If the engine is a hidden config engine, then we don't
      // switch to it, nor do we try to install it.
      return {
        canChangeToConfigEngine: false,
        canInstallEngine: !engine?.hidden,
      };
    }

    if (
      extension.startupReason === "ADDON_INSTALL" ||
      extension.startupReason === "ADDON_ENABLE"
    ) {
      // Don't allow an extension to set the default if it is already the default.
      if (this.defaultEngine.name == searchProvider.name) {
        return {
          canChangeToConfigEngine: false,
          canInstallEngine: false,
        };
      }
      if (
        !(await lazy.defaultOverrideAllowlist.canOverride(extension, engine.id))
      ) {
        lazy.logConsole.debug(
          "Allowing default engine to be set to config engine.",
          extension.id
        );
        // We don't allow overriding the engine in this case, but we can allow
        // the extension to change the default engine.
        return {
          canChangeToConfigEngine: true,
          canInstallEngine: false,
        };
      }
      // We're ok to override.
      engine.overrideWithEngine({ extension });
      lazy.logConsole.debug(
        "Allowing default engine to be set to config engine and overridden.",
        extension.id
      );
      return {
        canChangeToConfigEngine: true,
        canInstallEngine: false,
      };
    }

    if (
      engine.getAttr("overriddenBy") == extension.id &&
      (await lazy.defaultOverrideAllowlist.canOverride(extension, engine.id))
    ) {
      engine.overrideWithEngine({ extension });
      lazy.logConsole.debug(
        "Re-enabling overriding of core extension by",
        extension.id
      );
      return {
        canChangeToConfigEngine: true,
        canInstallEngine: false,
      };
    }

    return {
      canChangeToConfigEngine: false,
      canInstallEngine: false,
    };
  }

  /**
   * Adds a search engine that is specified from enterprise policies.
   *
   * @param {object} details
   *   An object that matches the `SearchEngines` policy schema.
   * @param {object} [settings]
   *   The saved settings for the user.
   * @see browser/components/enterprisepolicies/schemas/policies-schema.json
   */
  async #addPolicyEngine(details, settings) {
    let newEngine = new lazy.PolicySearchEngine({ details, settings });
    lazy.logConsole.debug("Adding Policy Engine:", newEngine.name);
    this.#addEngineToStore(newEngine);
  }

  /**
   * Adds a search engine that is specified by the user.
   *
   * @param {FormInfo} formInfo
   *   General information about the search engine.
   * @returns {Promise<UserSearchEngine>}
   *   The generated search engine object.
   */
  async addUserEngine(formInfo) {
    await this.init();

    let newEngine = new lazy.UserSearchEngine({ formInfo });
    lazy.logConsole.debug(`Adding ${formInfo.name}`);
    this.#addEngineToStore(newEngine);
    return newEngine;
  }

  /**
   * Installs an engine into the user's engine list.
   *
   * @param {SearchEngine} engine
   *   An engine configuration definition.
   */
  async addSearchEngine(engine) {
    await this.init();
    this.#addEngineToStore(engine);
  }

  /**
   * Called from the AddonManager when it either installs a new
   * extension containing a search engine definition or an upgrade
   * to an existing one.
   *
   * @param {object} extension
   *   An Extension object containing data about the extension.
   */
  async addEnginesFromExtension(extension) {
    // Treat add-on upgrade and downgrades the same - either way, the search
    // engine gets updated, not added. Generally, we don't expect a downgrade,
    // but just in case...
    if (
      extension.startupReason == "ADDON_UPGRADE" ||
      extension.startupReason == "ADDON_DOWNGRADE"
    ) {
      // Bug 1679861 An a upgrade or downgrade could be adding a search engine
      // that was not in a prior version, or the addon may have been blocklisted.
      // In either case, there will not be an existing engine.
      let existing = await this.#upgradeExtensionEngine(extension);
      if (existing?.length) {
        return;
      }
    }

    if (extension.isAppProvided) {
      this.#extensionsToRemove.add(extension.id);
      lazy.logConsole.debug(
        "addEnginesFromExtension: Queuing old app provided WebExtension for uninstall",
        extension.id
      );
      return;
    }
    lazy.logConsole.debug("addEnginesFromExtension:", extension.id);

    // If we haven't started the SearchService yet, store this extension
    // to install in SearchService.init().
    if (!this.isInitialized) {
      this.#startupExtensions.add(extension);
      return;
    }

    await this.#createAndAddAddonEngine({
      extension,
    });
  }

  /**
   * Adds a new Open Search engine from the xml file at the supplied URI.
   *
   * @param {string} engineURL
   *   The URL to the search engine's description file.
   * @param {string} iconURL
   *   A URL string to an icon file to be used as the search engine's icon. This
   *   value may be overridden by an icon specified in the engine description
   *   file.
   * @param {OriginAttributesDictionary} [originAttributes]
   *   The origin attributes to use to load this manifest.
   * @throws {lazy.SearchEngineInstallError|TypeError|Error}
   *   If the description file cannot be successfully loaded.
   */
  async addOpenSearchEngine(engineURL, iconURL, originAttributes) {
    lazy.logConsole.debug("addOpenSearchEngine: Adding", engineURL);
    await this.init();
    let engineData = await lazy.loadAndParseOpenSearchEngine(
      Services.io.newURI(engineURL),
      null,
      originAttributes
    );
    let engine = new lazy.OpenSearchEngine({
      engineData,
      faviconURL: iconURL,
      originAttributes,
    });
    this.#addEngineToStore(engine);
    this.#maybeStartOpenSearchUpdateTimer();
    return engine;
  }

  /**
   * This should be called when an extension is removed. It will remove any
   * search engines that are associated with the extension.
   *
   * @param {string} id
   *   The id of the extension.
   */
  async removeWebExtensionEngine(id) {
    if (!this.isInitialized) {
      lazy.logConsole.debug(
        "Delaying removing extension engine on startup:",
        id
      );
      this.#startupRemovedExtensions.add(id);
      return;
    }

    lazy.logConsole.debug("removeWebExtensionEngine:", id);
    for (let engine of this.#getEnginesByExtensionID(id)) {
      await this.removeEngine(engine, this.CHANGE_REASON.ADDON_UNINSTALL);
    }
  }

  /**
   * Removes the search engine. If the search engine is installed in a global
   * location, this will just hide the engine. If the engine is in the user's
   * profile directory, it will be removed from disk.
   *
   * @param {SearchEngine} engine
   *   The engine to remove.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason for the engine being removed, used for telemetry if the engine
   *   is currently a default engine.
   */
  async removeEngine(engine, changeReason) {
    await this.init();
    if (!engine) {
      throw new TypeError("no engine passed");
    }

    var engineToRemove = null;
    for (var e of this._engines.values()) {
      if (engine == e) {
        engineToRemove = e;
      }
    }

    if (!engineToRemove) {
      throw new Error("Unable to find engine to remove");
    }

    this.#enginesPendingRemoval.add(engineToRemove);

    if (engineToRemove == this.defaultEngine) {
      this.#findAndSetNewDefaultEngine(
        {
          privateMode: false,
        },
        changeReason
      );
    }

    // Bug 1575649 - We can't just check the default private engine here when
    // we're not using separate, as that re-checks the normal default, and
    // triggers update of the default search engine, which messes up various
    // tests. Really, removeEngine should always commit to updating any
    // changed defaults.
    if (
      this.#separatePrivateDefault &&
      engineToRemove == this.defaultPrivateEngine
    ) {
      this.#findAndSetNewDefaultEngine(
        {
          privateMode: true,
        },
        changeReason
      );
    }

    let userInstalled =
      engineToRemove instanceof lazy.UserInstalledConfigEngine;
    if (engineToRemove.inMemory && !userInstalled) {
      // Just hide it (the "hidden" setter will notify) and remove its alias to
      // avoid future conflicts with other engines.
      engineToRemove.hidden = true;
      engineToRemove.alias = null;
      this.#enginesPendingRemoval.delete(engineToRemove);
    } else {
      // Remove the engine file from disk if we had a legacy file in the profile.
      if (engineToRemove._filePath) {
        let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
        file.persistentDescriptor = engineToRemove._filePath;
        if (file.exists()) {
          file.remove(false);
        }
        engineToRemove._filePath = null;
      }

      if (userInstalled) {
        // If the engine is a user installed config engine,
        // reset its seen counter so it can be added again.
        let seenEngines =
          this._settings.getMetaDataAttribute(ENGINES_SEEN_KEY) ?? {};
        delete seenEngines[engineToRemove._loadPath];
        this._settings.setMetaDataAttribute(ENGINES_SEEN_KEY, seenEngines);
      }

      this.#internalRemoveEngine(engineToRemove);
      // Since we removed an engine, we may need to update the preferences.
      if (!this.#dontSetUseSavedOrder) {
        this.#saveSortedEngineList();
      }
    }
    lazy.SearchUtils.notifyAction(
      engineToRemove,
      lazy.SearchUtils.MODIFIED_TYPE.REMOVED
    );
  }

  /**
   * Moves a search engine in the list. This method can account for both visible
   * and hidden engines.
   *
   * @param {SearchEngine} engine
   *   The engine to move.
   * @param {number} newIndex
   *   The engine's new index in the set of visible engines.
   * @param {boolean} [skipHidden]
   *   If set, this skips moving hidden engines. This is for the case of the old
   *   preferences UI which hides engines from the user's view, and so we need
   *   to take them into account when adjusting indexes.
   *
   * @throws {RangeError}
   *   If newIndex is out of bounds.
   * @throws {TypeError}
   *   If the engine is not a search engine.
   * @throws {Error}
   *   If the engine is hidden or can't be found.
   */
  async moveEngine(engine, newIndex, skipHidden = false) {
    await this.init();
    if (newIndex >= this.#sortedEngines.length || newIndex < 0) {
      throw new RangeError("newIndex out of bounds");
    }
    if (!(engine instanceof lazy.SearchEngine)) {
      throw new TypeError("engine is not a SearchEngine instance");
    }
    if (skipHidden && engine.hidden) {
      throw new Error("Unable to move a hidden engine");
    }

    var currentIndex = this.#sortedEngines.indexOf(engine);
    if (currentIndex == -1) {
      throw new Error("Unable to find engine to move");
    }

    if (skipHidden) {
      // These callers only take into account non-hidden engines when calculating
      // newIndex, but we need to move it in the array of all engines, so we
      // need to adjust newIndex accordingly. To do this, we count the number
      // of hidden engines in the list before the engine that we're taking the
      // place of. We do this by first finding newIndexEngine (the engine that
      // we were supposed to replace) and then iterating through the complete
      // engine list until we reach it, increasing newIndex for each hidden
      // engine we find on our way there.
      //
      // This could be further simplified by having our caller pass in
      // newIndexEngine directly instead of newIndex.
      var newIndexEngine = this.#sortedVisibleEngines[newIndex];
      if (!newIndexEngine) {
        throw new Error("Unable to find engine to replace");
      }

      for (var i = 0; i < this.#sortedEngines.length; ++i) {
        if (newIndexEngine == this.#sortedEngines[i]) {
          break;
        }
        if (this.#sortedEngines[i].hidden) {
          newIndex++;
        }
      }
    }

    if (currentIndex == newIndex) {
      return;
    } // nothing to do!

    // Move the engine
    var movedEngine = this._cachedSortedEngines.splice(currentIndex, 1)[0];
    this._cachedSortedEngines.splice(newIndex, 0, movedEngine);

    lazy.SearchUtils.notifyAction(
      engine,
      lazy.SearchUtils.MODIFIED_TYPE.CHANGED
    );

    // Since we moved an engine, we need to update the preferences.
    this.#saveSortedEngineList();
  }

  /**
   * Un-hides all application provided engines.
   */
  restoreDefaultEngines() {
    this.#ensureInitialized();
    for (let e of this._engines.values()) {
      // Unhide all default engines
      if (e.hidden && e.isAppProvided) {
        e.hidden = false;
      }
    }
  }

  /**
   * @typedef {object} ParseSubmissionResult
   * @property {?SearchEngine} engine
   *   The search engine associated with the URL passed in to
   *   ``parseSubmissionURL``, or null if the URL does not represent a search
   *   submission.
   * @property {string} terms
   *   String containing the sought terms. This can be an empty string in case
   *   no terms were specified or the URL does not represent a search submission.
   * @property {string} termsParameterName
   *   The name of the query parameter used by `engine` for queries. E.g. "q".
   */

  /**
   * Determines if the provided URL represents results from a search engine, and
   * provides details about the match.
   *
   * The lookup mechanism checks whether the domain name and path of the
   * provided HTTP or HTTPS URL matches one of the known values for the visible
   * search engines. The match does not depend on which of the schemes is used.
   * The expected URI parameter for the search terms must exist in the query
   * string, but other parameters are ignored.
   *
   * @param {string} url
   *   String containing the URL to parse, for example
   *   `https://www.google.com/search?q=terms`.
   * @returns {ParseSubmissionResult}
   */
  parseSubmissionURL(url) {
    if (!this.hasSuccessfullyInitialized) {
      // If search is not initialized or failed initializing, do nothing.
      // This allows us to use this function early in telemetry.
      // The only other consumer of this (places) uses it much later.
      return { engine: null, terms: "", termsParameterName: "" };
    }

    if (!this.#parseSubmissionMap) {
      this.#buildParseSubmissionMap();
    }

    // Extract the elements of the provided URL first.
    let soughtKey, soughtQuery;
    try {
      let soughtUrl = Services.io.newURI(url);

      // Exclude any URL that is not HTTP or HTTPS from the beginning.
      if (!soughtUrl.schemeIs("http") && !soughtUrl.schemeIs("https")) {
        return { engine: null, terms: "", termsParameterName: "" };
      }

      // Reading these URL properties may fail and raise an exception.
      soughtKey = soughtUrl.host + soughtUrl.filePath.toLowerCase();
      soughtQuery = soughtUrl.query;
    } catch (ex) {
      // Errors while parsing the URL or accessing the properties are not fatal.
      return { engine: null, terms: "", termsParameterName: "" };
    }

    // Look up the domain and path in the map to identify the search engine.
    let mapEntry = this.#parseSubmissionMap.get(soughtKey);
    if (!mapEntry) {
      return { engine: null, terms: "", termsParameterName: "" };
    }

    // Extract the search terms from the parameter, for example "caff%C3%A8"
    // from the URL "https://www.google.com/search?q=caff%C3%A8&client=firefox".
    // We cannot use `URLSearchParams` here as the terms might not be
    // encoded in UTF-8.
    let encodedTerms = null;
    for (let param of soughtQuery.split("&")) {
      let equalPos = param.indexOf("=");
      if (
        equalPos != -1 &&
        param.substr(0, equalPos) == mapEntry.termsParameterName
      ) {
        // This is the parameter we are looking for.
        encodedTerms = param.substr(equalPos + 1);
        break;
      }
    }
    if (encodedTerms === null) {
      return { engine: null, terms: "", termsParameterName: "" };
    }

    // Decode the terms using the charset defined in the search engine.
    let terms;
    try {
      terms = Services.textToSubURI.UnEscapeAndConvert(
        mapEntry.engine.queryCharset,
        encodedTerms.replace(/\+/g, " ")
      );
    } catch (ex) {
      // Decoding errors will cause this match to be ignored.
      return { engine: null, terms: "", termsParameterName: "" };
    }

    return {
      engine: mapEntry.engine,
      terms,
      termsParameterName: mapEntry.termsParameterName,
    };
  }

  /**
   * Whether to display the "Search in Private Window" result in the urlbar.
   */
  get separatePrivateDefaultUrlbarResultEnabled() {
    return this.#lazyPrefs.separatePrivateDefaultUrlbarResultEnabled;
  }

  /**
   * This is a nsITimerCallback for the timerManager notification that is
   * registered for handling updates to search engines. Only OpenSearch engines
   * have these updates and hence, only those are handled here.
   */
  async notify() {
    lazy.logConsole.debug("notify: checking for updates");

    // Walk the engine list, looking for engines whose update time has expired.
    for (let engine of this._engines.values()) {
      if (!(engine instanceof lazy.OpenSearchEngine)) {
        continue;
      }
      await engine.maybeUpdate();
    }
  }

  /** @type {?SearchEngine} */
  #currentEngine = null;
  /** @type {?SearchEngine} */
  #currentPrivateEngine = null;
  /** @type {boolean} */
  #queuedIdle = false;

  /**
   * A deferred promise that is resolved when initialization has finished.
   *
   * Resolved when initalization has successfully finished, and rejected if it
   * has failed.
   *
   * @type {PromiseWithResolvers<void>}
   */
  #initDeferredPromise = Promise.withResolvers();

  /**
   * Indicates if initialization has started, failed, succeeded or has not
   * started yet.
   *
   * These are the statuses:
   *   "not initialized" - The SearchService has not started initialization.
   *   "started" - The SearchService has started initializaiton.
   *   "success" - The SearchService successfully completed initialization.
   *   "failed" - The SearchService failed during initialization.
   *
   * @type {string}
   */
  #initializationStatus = "not initialized";

  /**
   * Indicates if we're already waiting for maybeReloadEngines to be called.
   *
   * @type {boolean}
   */
  #maybeReloadDebounce = false;

  /**
   * Indicates if we're currently in maybeReloadEngines.
   *
   * This is prefixed with _ rather than # because it is
   * called in a test.
   *
   * @type {boolean}
   */
  _reloadingEngines = false;

  /**
   * The engine selector singleton that is managing the engine configuration.
   *
   * @type {SearchEngineSelector|null}
   */
  #engineSelector = null;

  /**
   * Various search engines may be ignored if their submission urls contain a
   * string that is in the list. The list is controlled via remote settings.
   *
   * @type {string[]}
   */
  #submissionURLIgnoreList = [];

  /**
   * Various search engines may be ignored if their load path is contained
   * in this list. The list is controlled via remote settings.
   *
   * @type {string[]}
   */
  #loadPathIgnoreList = [];

  /**
   * A map of engine identifiers to `SearchEngine`.
   * It is prefixed with _ rather than # because it is accessed in tests.
   *
   * @type {Map<string, SearchEngine>}
   */
  _engines = new Map();

  /**
   * Cache for this.#sortedEngines.
   *
   * @type {SearchEngine[]}
   */
  _cachedSortedEngines = null;

  /**
   * A flag to prevent setting of useSavedOrder when there's non-user
   * activity happening.
   *
   * @type {boolean}
   */
  #dontSetUseSavedOrder = false;

  /**
   * An object containing the id of the AppProvidedConfigEngine for the default
   * engine, as suggested by the configuration.
   *
   * @type {object}
   */
  #searchDefault = null;

  /**
   * An object containing the id of the AppProvidedConfigEngine for the default
   * engine for private browsing mode, as suggested by the configuration.
   *
   * @type {object}
   */
  #searchPrivateDefault = null;

  /**
   * A Set of installed search extensions reported by AddonManager
   * startup before SearchSevice has started. Will be installed
   * during init(). Does not contain application provided engines.
   *
   * @type {Set<object>}
   */
  #startupExtensions = new Set();

  /**
   * A Set of installed app provided search Web Extensions to be uninstalled by
   * the AddonManager on idle. We no longer have app provided engines as
   * web extensions after search-config-v2 enabled in Firefox version 128.
   *
   * @type {Set<object>}
   */
  #extensionsToRemove = new Set();

  /**
   * A Set of removed search extensions reported by AddonManager
   * startup before SearchSevice has started. Will be removed
   * during init().
   *
   * @type {Set<object>}
   */
  #startupRemovedExtensions = new Set();

  /**
   * Used in #parseSubmissionMap
   *
   * @typedef {object} submissionMapEntry
   * @property {SearchEngine} engine
   *   The search engine.
   * @property {string} termsParameterName
   *   The search term parameter name.
   */

  /**
   * This map is built lazily after the available search engines change.  It
   * allows quick parsing of an URL representing a search submission into the
   * search engine name and original terms.
   *
   * The keys are strings containing the domain name and lowercase path of the
   * engine submission, for example "www.google.com/search".
   *
   * @type {Map<string, submissionMapEntry>|null}
   */
  #parseSubmissionMap = null;

  /**
   * Keep track of pre-init observers have been added.
   *
   * @type {boolean}
   */
  #earlyObserversAdded = false;

  /**
   * Keep track of observers have been added.
   *
   * @type {boolean}
   */
  #observersAdded = false;

  /**
   * Keeps track to see if the OpenSearch update timer has been started or not.
   *
   * @type {boolean}
   */
  #openSearchUpdateTimerStarted = false;

  /**
   * Keeps track of engines that are about to be removed.
   *
   * @type {WeakSet<SearchEngine>}
   */
  #enginesPendingRemoval = new WeakSet();

  /**
   * An array of search engines sorted into display order.
   *
   * @type {SearchEngine[]}
   */
  get #sortedEngines() {
    if (!this._cachedSortedEngines) {
      return this.#buildSortedEngineList();
    }
    return this._cachedSortedEngines;
  }
  /**
   * This reflects the combined values of the prefs for enabling the separate
   * private default UI, and for the user choosing a separate private engine.
   * If either one is disabled, then we don't enable the separate private default.
   *
   * @returns {boolean}
   */
  get #separatePrivateDefault() {
    return (
      this.#lazyPrefs.separatePrivateDefaultPrefValue &&
      this.#lazyPrefs.separatePrivateDefaultEnabledPrefValue
    );
  }

  /**
   * Returns an array of addon search engines with a particular
   * extension ID.
   *
   * @param {string} extensionID
   * @returns {AddonSearchEngine[]}
   */
  #getEnginesByExtensionID(extensionID) {
    lazy.logConsole.debug(
      "getEnginesByExtensionID: getting all engines for",
      extensionID
    );

    /**
     * @param {SearchEngine} engine
     * @returns {engine is AddonSearchEngine}
     */
    function isAddonEngine(engine) {
      return (
        engine instanceof lazy.AddonSearchEngine &&
        engine.extensionID == extensionID
      );
    }

    return this.#sortedEngines.filter(isAddonEngine);
  }

  /**
   * Returns the engine associated with the WebExtension details,
   * or null if no engine matched.
   *
   * @param {object} details
   *   Details of the WebExtension.
   * @param {string} details.id
   *   The WebExtension ID
   */
  #getEngineByWebExtensionDetails(details) {
    for (const engine of this._engines.values()) {
      if (
        engine instanceof lazy.AddonSearchEngine &&
        engine.extensionID == details.id
      ) {
        return engine;
      }
    }
    return null;
  }

  /**
   * Helper function to get the current default engine.
   *
   * This is prefixed with _ rather than # because it is
   * called in test_remove_engine_notification_box.js
   *
   * @param {boolean} privateMode
   *   If true, returns the default engine for private browsing mode, otherwise
   *   the default engine for the normal mode. Note, this function does not
   *   check the "separatePrivateDefault" preference - that is up to the caller.
   * @returns {?SearchEngine}
   *   The appropriate search engine, or null if one could not be determined.
   */
  _getEngineDefault(privateMode) {
    let currentEngine = privateMode
      ? this.#currentPrivateEngine
      : this.#currentEngine;

    if (currentEngine && !currentEngine.hidden) {
      return currentEngine;
    }

    // No default loaded, so find it from settings.
    const attributeName = privateMode
      ? "privateDefaultEngineId"
      : "defaultEngineId";

    let engineId = this._settings.getMetaDataAttribute(attributeName);
    let engine = this._engines.get(engineId) || null;
    if (
      engine &&
      this._settings.getVerifiedMetaDataAttribute(
        attributeName,
        engine.isConfigEngine
      )
    ) {
      if (privateMode) {
        this.#currentPrivateEngine = engine;
      } else {
        this.#currentEngine = engine;
      }
    }
    if (!engineId) {
      if (privateMode) {
        this.#currentPrivateEngine = this.appPrivateDefaultEngine;
      } else {
        this.#currentEngine = this.appDefaultEngine;
      }
    }

    currentEngine = privateMode
      ? this.#currentPrivateEngine
      : this.#currentEngine;
    if (currentEngine && !currentEngine.hidden) {
      return currentEngine;
    }
    // No default in settings or it is hidden, so find the new default.
    return this.#findAndSetNewDefaultEngine(
      { privateMode },
      this.CHANGE_REASON.NO_EXISTING_DEFAULT_ENGINE
    );
  }

  /**
   * If initialization has not been completed yet, perform synchronous
   * initialization.
   * Throws in case of initialization error.
   */
  #ensureInitialized() {
    if (this.#initializationStatus === "success") {
      return;
    }

    if (this.#initializationStatus === "failed") {
      throw new Error("SearchService failed while it was initializing.");
    }

    let err = new Error(
      "Something tried to use the search service before it finished " +
        "initializing. Please examine the stack trace to figure out what and " +
        "where to fix it:\n"
    );
    err.message += err.stack;
    throw err;
  }

  #lazyPrefs = XPCOMUtils.declareLazy({
    separatePrivateDefaultPrefValue: {
      pref: "browser.search.separatePrivateDefault",
      default: false,
      onUpdate: this.#onSeparateDefaultPrefChanged.bind(this),
    },
    separatePrivateDefaultEnabledPrefValue: {
      pref: "browser.search.separatePrivateDefault.ui.enabled",
      default: false,
      onUpdate: this.#onSeparateDefaultPrefChanged.bind(this),
    },
    separatePrivateDefaultUrlbarResultEnabled: {
      pref: "browser.search.separatePrivateDefault.urlbarResult.enabled",
      default: false,
      // No need to reload engines, as this only affects the Urlbar result list.
    },
    experimentPrefValue: {
      pref: "browser.search.experiment",
      default: "",
      onUpdate: () => this.#maybeReloadEngines(this.CHANGE_REASON.EXPERIMENT),
    },
  });

  /**
   * This function adds observers, retrieves the search engine ignore list, and
   * initializes the Search Engine Selector prior to doing the core tasks of
   * search service initialization.
   *
   */
  #doPreInitWork() {
    // We need to catch the region being updated during initialization so we
    // start listening straight away.
    if (!this.#earlyObserversAdded) {
      Services.obs.addObserver(this, lazy.Region.REGION_TOPIC);
      this.#earlyObserversAdded = true;
    }

    this.#getIgnoreListAndSubscribe().catch(ex =>
      console.error(ex, "Search Service could not get the ignore list.")
    );

    this.#engineSelector = new lazy.SearchEngineSelector(
      this.#handleConfigurationUpdated.bind(this)
    );
  }

  /**
   * This function fetches information to load search engines and ensures the
   * search service is in the correct state for external callers to interact
   * with it.
   *
   * This function sets #initDeferredPromise to resolve or reject.
   *   | Resolved | when initalization has successfully finished.
   *   | Rejected | when initialization has failed.
   */
  async #init() {
    lazy.logConsole.debug("init");

    const timerId = Glean.searchService.startupTime.start();

    this.#doPreInitWork();

    let initSection;
    try {
      initSection = "Settings";
      this.#maybeThrowErrorInTest(initSection);
      this.#maybeThrowErrorInTest("LoadSettingsAddonManager");
      const settings = await this._settings.get();

      initSection = "FetchEngines";
      this.#maybeThrowErrorInTest(initSection);
      const refinedConfig = await this._fetchEngineSelectorEngines();

      initSection = "LoadEngines";
      this.#maybeThrowErrorInTest(initSection);
      await this.#loadEngines(settings, refinedConfig);
    } catch (ex) {
      if (ex.message.startsWith("Addon manager")) {
        if (
          !Services.startup.shuttingDown &&
          ex.message != "Addon manager shutting down"
        ) {
          Glean.searchService.initializationStatus.failedLoadSettingsAddonManager.add();
        }
      } else {
        Glean.searchService.initializationStatus[`failed${initSection}`].add();
      }
      Glean.searchService.startupTime.cancel(timerId);

      lazy.logConsole.error("#init: failure initializing search:", ex);
      this.#initializationStatus = "failed";
      this.#initDeferredPromise.reject(ex);

      throw ex;
    }

    // If we've got this far, but the application is now shutting down,
    // then we need to abandon any further work, especially not writing
    // the settings. We do this, because the add-on manager has also
    // started shutting down and as a result, we might have an incomplete
    // picture of the installed search engines. Writing the settings at
    // this stage would potentially mean the user would loose their engine
    // data.
    // We will however, rebuild the settings on next start up if we detect
    // it is necessary.
    if (Services.startup.shuttingDown) {
      Glean.searchService.startupTime.cancel(timerId);

      let ex = new Error("Abandoning search service init due to shutting down");
      this.#initializationStatus = "failed";
      this.#initDeferredPromise.reject(ex);
      throw ex;
    }

    this.#initializationStatus = "success";
    if (!this._settings.lastGetCorrupt) {
      Glean.searchService.initializationStatus.success.add();
    } else {
      Glean.searchService.initializationStatus.settingsCorrupt.add();
      this._showSearchSettingsResetNotificationBox(this.defaultEngine.name);
    }
    this.#initDeferredPromise.resolve();
    this.#addObservers();

    Glean.searchService.startupTime.stopAndAccumulate(timerId);

    this.#recordDefaultEngineTelemetryData();

    Services.obs.notifyObservers(
      null,
      lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
      "init-complete"
    );

    lazy.logConsole.debug("Completed #init");
    this.#doPostInitWork();
  }

  /**
   * This function records telemetry, checks experiment updates, sets up a timer
   * for opensearch, removes any necessary Add-on engines immediately after the
   * search service has successfully initialized.
   *
   */
  #doPostInitWork() {
    this.#maybeStartOpenSearchUpdateTimer();

    if (this.#startupRemovedExtensions.size) {
      Services.tm.dispatchToMainThread(async () => {
        // Now that init() has successfully finished, we remove any engines
        // that have had their add-ons removed by the add-on manager.
        // We do this after init() has complete, as that allows us to use
        // removeEngine to look after any default engine changes as well.
        // This could cause a slight flicker on startup, but it should be
        // a rare action.
        lazy.logConsole.debug("Removing delayed extension engines");
        for (let id of this.#startupRemovedExtensions) {
          for (let engine of this.#getEnginesByExtensionID(id)) {
            await this.removeEngine(engine, this.CHANGE_REASON.ADDON_UNINSTALL);
          }
        }
        this.#startupRemovedExtensions.clear();
      });
    }
  }

  /**
   * Obtains the ignore list from remote settings. This should only be
   * called from init(). Any subsequent updates to the remote settings are
   * handled via a sync listener.
   *
   */
  async #getIgnoreListAndSubscribe() {
    let listener = this.#handleIgnoreListUpdated.bind(this);
    const current = await lazy.IgnoreLists.getAndSubscribe(listener);

    // Only save the listener after the subscribe, otherwise for tests it might
    // not be fully set up by the time we remove it again.
    this.ignoreListListener = listener;

    await this.#handleIgnoreListUpdated({ data: { current } });
    Services.obs.notifyObservers(
      null,
      lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
      "settings-update-complete"
    );
  }

  /**
   * This handles updating of the ignore list settings, and removing any ignored
   * engines.
   *
   * @param {object} eventData
   *   The event in the format received from RemoteSettings.
   */
  async #handleIgnoreListUpdated(eventData) {
    lazy.logConsole.debug("#handleIgnoreListUpdated");
    const {
      data: { current },
    } = eventData;

    for (const entry of current) {
      if (entry.id == "load-paths") {
        this.#loadPathIgnoreList = [...entry.matches];
      } else if (entry.id == "submission-urls") {
        this.#submissionURLIgnoreList = [...entry.matches];
      }
    }

    try {
      await this.promiseInitialized;
    } catch (ex) {
      // If there's a problem with initialization return early to allow
      // search service to continue in a limited mode without engines.
      return;
    }

    // We try to remove engines manually, as this should be more efficient and
    // we don't really want to cause a re-init as this upsets unit tests.
    let engineRemoved = false;
    for (let engine of this._engines.values()) {
      if (this.#engineMatchesIgnoreLists(engine)) {
        await this.removeEngine(
          engine,
          this.CHANGE_REASON.ENGINE_IGNORE_LIST_UPDATED
        );
        engineRemoved = true;
      }
    }
    // If we've removed an engine, and we don't have any left, we need to
    // reload the engines - it is possible the settings just had one engine in it,
    // and that is now empty, so we need to load from our main list.
    if (engineRemoved && !this._engines.size) {
      this.#maybeReloadEngines(
        this.CHANGE_REASON.ENGINE_IGNORE_LIST_UPDATED
      ).catch(console.error);
    }
  }

  /**
   * Determines if a given engine matches the ignorelists or not.
   *
   * @param {SearchEngine} engine
   *   The engine to check against the ignorelists.
   * @returns {boolean}
   *   Returns true if the engine matches a ignorelists entry.
   */
  #engineMatchesIgnoreLists(engine) {
    /** @type {(name: string, url: string, type: string) => void} */
    let logIgnored = (name, url, type) => {
      lazy.logConsole.warn("Search engine", name, `matches ${type}`, url);
      Services.prefs.setCharPref(
        lazy.SearchUtils.BROWSER_SEARCH_PREF + "lastEngineIgnored",
        // Limit length of url to avoid storing too much in prefs.
        `${Math.trunc(Date.now() / 1000)} Search engine '${name}' matches ${type} ignore list ${url.substring(0, 200)}`
      );
    };

    if (this.#loadPathIgnoreList.includes(engine._loadPath)) {
      logIgnored(engine.name, engine._loadPath, "load path");
      return true;
    }
    let url = engine.searchURLWithNoTerms.spec.toLowerCase();
    if (
      this.#submissionURLIgnoreList.some(code =>
        url.includes(code.toLowerCase())
      )
    ) {
      logIgnored(engine.name, url, "submission url");
      return true;
    }
    return false;
  }

  /**
   * Handles the search configuration being - adds a wait on the user
   * being idle, before the search engine update gets handled.
   */
  #handleConfigurationUpdated() {
    if (this.#queuedIdle) {
      return;
    }

    this.#queuedIdle = true;

    lazy.idleService.addIdleObserver(this, RECONFIG_IDLE_TIME_SEC);
  }

  /**
   * Returns the engine that is the default for this locale/region, ignoring any
   * user changes to the default engine.
   *
   * @param {boolean} privateMode
   *   Set to true to return the default engine in private mode,
   *   false for normal mode.
   * @returns {SearchEngine}
   *   The engine that is default.
   */
  #appDefaultEngine(privateMode = false) {
    let defaultEngine = this._engines.get(
      privateMode && this.#searchPrivateDefault
        ? this.#searchPrivateDefault
        : this.#searchDefault
    );

    if (Services.policies?.status == Ci.nsIEnterprisePolicies.ACTIVE) {
      let activePolicies = Services.policies.getActivePolicies();
      if (activePolicies.SearchEngines) {
        let policyDefault =
          privateMode &&
          this.#separatePrivateDefault &&
          activePolicies.SearchEngines.DefaultPrivate
            ? activePolicies.SearchEngines.DefaultPrivate
            : activePolicies.SearchEngines.Default;
        if (policyDefault) {
          let policyEngine = this.#getEngineByName(policyDefault);
          if (policyEngine) {
            return policyEngine;
          }
        }
        if (activePolicies.SearchEngines.Remove?.includes(defaultEngine.name)) {
          defaultEngine = null;
        }
      }
    }

    if (defaultEngine) {
      return defaultEngine;
    }

    if (privateMode) {
      // If for some reason we can't find the private mode engine, fall back
      // to the non-private one.
      return this.#appDefaultEngine(false);
    }

    // Something unexpected has happened. In order to recover the app default
    // engine, use the first visible engine that is also a general purpose engine.
    // Worst case, we just use the first visible engine.
    defaultEngine = this.#sortedVisibleEngines.find(
      e => e.isGeneralPurposeEngine
    );
    return defaultEngine ? defaultEngine : this.#sortedVisibleEngines[0];
  }

  /**
   * Loads engines asynchronously.
   *
   * @param {object} settings
   *   An object representing the search engine settings.
   * @param {RefinedSearchConfig} refinedConfig
   *   The refined search configuration for this user.
   */
  async #loadEngines(settings, refinedConfig) {
    // Get user's current settings and search engine before we load engines from
    // config. These values will be compared after engines are loaded.
    let prevMetaData = { ...settings?.metaData };
    let prevCurrentEngineId = prevMetaData.defaultEngineId;
    let prevAppDefaultEngineId = prevMetaData?.appDefaultEngineId;

    lazy.logConsole.debug("#loadEngines: start");
    this.#setDefaultFromSelector(refinedConfig);

    this.#loadEnginesFromConfig(refinedConfig.engines, settings);

    await this.#loadStartupEngines(settings);

    this.#loadEnginesFromPolicies(settings);

    // `loadEnginesFromSettings` loads the engines and their settings together.
    // If loading the settings caused the default engine to change because of an
    // override, then we don't want to show the notification box.
    let skipDefaultChangedNotification = await this.#loadEnginesFromSettings(
      settings,
      refinedConfig.engines
    );

    // If #loadEnginesFromSettings changed the default engine, then we don't
    // need to call #checkOpenSearchOverrides as we know that the overrides have
    // only just been applied.
    skipDefaultChangedNotification ||=
      await this.#checkOpenSearchOverrides(settings);

    // Settings file version 6 and below will need a migration to store the
    // engine ids rather than engine names.
    this._settings.migrateEngineIds(settings);

    lazy.logConsole.debug("#loadEngines: done");

    let newCurrentEngine = this._getEngineDefault(false);
    let newCurrentEngineId = newCurrentEngine?.id;

    this._settings.setMetaDataAttribute(
      "appDefaultEngineId",
      this.appDefaultEngine?.id
    );

    if (
      !skipDefaultChangedNotification &&
      this.#shouldDisplayRemovalOfEngineNotificationBox(
        settings,
        prevMetaData,
        newCurrentEngineId,
        prevCurrentEngineId,
        prevAppDefaultEngineId
      )
    ) {
      let newCurrentEngineName = newCurrentEngine?.name;

      let [prevCurrentEngineName, prevAppDefaultEngineName] = [
        settings.engines.find(e => e.id == prevCurrentEngineId)?._name,
        settings.engines.find(e => e.id == prevAppDefaultEngineId)?._name,
      ];

      this._showRemovalOfSearchEngineNotificationBox(
        prevCurrentEngineName || prevAppDefaultEngineName,
        newCurrentEngineName
      );
    }
  }

  /**
   * Helper function to determine if the removal of search engine notification
   * box should be displayed.
   *
   * @param { object } settings
   *   The user's search engine settings.
   * @param { object } prevMetaData
   *   The user's previous search settings metadata.
   * @param { object } newCurrentEngineId
   *   The user's new current default engine.
   * @param { object } prevCurrentEngineId
   *   The user's previous default engine.
   * @param { object } prevAppDefaultEngineId
   *   The user's previous app default engine.
   * @returns { boolean }
   *   Return true if the previous default engine has been removed and
   *   notification box should be displayed.
   */
  #shouldDisplayRemovalOfEngineNotificationBox(
    settings,
    prevMetaData,
    newCurrentEngineId,
    prevCurrentEngineId,
    prevAppDefaultEngineId
  ) {
    if (
      !Services.prefs.getBoolPref("browser.search.removeEngineInfobar.enabled")
    ) {
      return false;
    }

    // If for some reason we were unable to install any engines and hence no
    // default engine, do not display the notification box
    if (!newCurrentEngineId) {
      return false;
    }

    // If the previous engine is still available, don't show the notification
    // box.
    if (prevCurrentEngineId && this._engines.has(prevCurrentEngineId)) {
      return false;
    }
    if (!prevCurrentEngineId && this._engines.has(prevAppDefaultEngineId)) {
      return false;
    }

    // Don't show the notification if the previous engine was an enterprise engine -
    // the text doesn't quite make sense.
    let checkPolicyEngineId = prevCurrentEngineId || prevAppDefaultEngineId;
    if (checkPolicyEngineId) {
      let engineSettings = settings.engines.find(
        e => e.id == checkPolicyEngineId
      );
      if (engineSettings?._loadPath?.startsWith("[policy]")) {
        return false;
      }
    }

    // If the user's previous engine id is different than the new current
    // engine id, or if the user was using the app default engine and the
    // app default engine id is different than the new current engine id,
    // we check if the user's settings metadata has been upddated.
    if (
      (prevCurrentEngineId && prevCurrentEngineId !== newCurrentEngineId) ||
      (!prevCurrentEngineId &&
        prevAppDefaultEngineId &&
        prevAppDefaultEngineId !== newCurrentEngineId)
    ) {
      // Check settings metadata to detect an update to locale. Sometimes when
      // the user changes their locale it causes a change in engines.
      // If there is no update to settings metadata then the engine change was
      // caused by an update to config rather than a user changing their locale.
      if (!this.#didSettingsMetaDataUpdate(prevMetaData)) {
        return true;
      }
    }

    return false;
  }

  /**
   * Loads engines as specified by the configuration. We only expect
   * configured engines here, user engines should not be listed.
   *
   * @param {SearchEngineDefinition[]} engineConfigs
   *   An array of engines configurations based on the schema.
   * @param {object} [settings]
   *   The saved settings for the user.
   */
  #loadEnginesFromConfig(engineConfigs, settings) {
    lazy.logConsole.debug("#loadEnginesFromConfig");
    for (let config of engineConfigs) {
      try {
        let engine = new lazy.AppProvidedConfigEngine({ config, settings });
        this.#addEngineToStore(engine);
      } catch (ex) {
        console.error(
          "Could not load app provided search engine id:",
          config.identifier,
          ex
        );
      }
    }
  }

  /**
   * Loads any engines that have been received from the AddonManager during
   * startup and before we have finished initialising.
   *
   * @param {object} [settings]
   *   The saved settings for the user.
   */
  async #loadStartupEngines(settings) {
    if (this.#startupExtensions.size) {
      await lazy.AddonManager.readyPromise;
    }

    lazy.logConsole.debug(
      "#loadStartupEngines: loading",
      this.#startupExtensions.size,
      "engines reported by AddonManager startup"
    );
    for (let extension of this.#startupExtensions) {
      try {
        await this.#createAndAddAddonEngine({
          extension,
          settings,
        });
      } catch (ex) {
        lazy.logConsole.error(
          "#loadStartupEngines failed for",
          extension.id,
          ex
        );
      }
    }
    this.#startupExtensions.clear();
  }

  /**
   * When starting up, check if any of the saved application provided engines
   * are no longer required, previously were default and were overridden by
   * an OpenSearch engine.
   *
   * Also check if any OpenSearch overrides need to be re-applied.
   *
   * Add-on search engines are handled separately.
   *
   * @param {object} settings
   *   The loaded settings for the user.
   * @returns {Promise<boolean>}
   *   Returns true if the default engine was changed.
   */
  async #checkOpenSearchOverrides(settings) {
    let defaultEngineChanged = false;
    let savedDefaultEngineId =
      settings.metaData.defaultEngineId || settings.metaData.appDefaultEngineId;
    if (!savedDefaultEngineId) {
      return false;
    }
    // First handle the case where the application provided engine was removed,
    // and we need to restore the OpenSearch engine.
    for (let engineSettings of settings.engines) {
      if (
        !this._engines.get(engineSettings.id) &&
        engineSettings._isConfigEngine &&
        engineSettings.id == savedDefaultEngineId &&
        engineSettings._metaData.overriddenByOpenSearch
      ) {
        let restoringEngine = new lazy.OpenSearchEngine({
          json: engineSettings._metaData.overriddenByOpenSearch,
        });
        restoringEngine.copyUserSettingsFrom(engineSettings);
        this.#addEngineToStore(restoringEngine, true);

        // We assume that the app provided engine was removed due to a
        // configuration change, and therefore we have re-added the OpenSearch
        // search engine. It is possible that it was actually due to a
        // locale/region change, but that is harder to detect here.
        this.#setEngineDefault(
          false,
          restoringEngine,
          this.CHANGE_REASON.CONFIG
        );
        delete engineSettings._metaData.overriddenByOpenSearch;
      }
    }
    // Now handle the case where the an application provided engine has been
    // overridden by an OpenSearch engine, and we need to re-apply the override.
    for (let engine of this._engines.values()) {
      if (
        engine.isConfigEngine &&
        engine.getAttr("overriddenByOpenSearch") &&
        engine.id == savedDefaultEngineId
      ) {
        let restoringEngine = new lazy.OpenSearchEngine({
          json: engine.getAttr("overriddenByOpenSearch"),
        });
        if (
          await lazy.defaultOverrideAllowlist.canEngineOverride(
            restoringEngine,
            engine.id
          )
        ) {
          engine.overrideWithEngine({ engine: restoringEngine });
        }
      }
    }

    return defaultEngineChanged;
  }

  /**
   * Reloads engines asynchronously, but only when
   * the service has already been initialized.
   *
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason reload engines is being called.
   */
  async #maybeReloadEngines(changeReason) {
    if (this.#maybeReloadDebounce) {
      lazy.logConsole.debug("We're already waiting to reload engines.");
      return;
    }

    if (!this.isInitialized || this._reloadingEngines) {
      this.#maybeReloadDebounce = true;
      // Schedule a reload to happen at most 10 seconds after the current run.
      Services.tm.idleDispatchToMainThread(() => {
        if (!this.#maybeReloadDebounce) {
          return;
        }
        this.#maybeReloadDebounce = false;
        this.#maybeReloadEngines(changeReason).catch(console.error);
      }, 10000);
      lazy.logConsole.debug(
        "Post-poning maybeReloadEngines() as we're currently initializing."
      );
      return;
    }

    // Before entering `_reloadingEngines` get the settings which we'll need.
    // This also ensures that any pending settings have finished being written,
    // which could otherwise cause data loss.
    let settings = await this._settings.get();

    lazy.logConsole.debug("Running maybeReloadEngines");
    this._reloadingEngines = true;

    try {
      await this._reloadEngines(settings, changeReason);
    } catch (ex) {
      lazy.logConsole.error("maybeReloadEngines failed", ex);
    }
    this._reloadingEngines = false;
    lazy.logConsole.debug("maybeReloadEngines complete");
  }

  /**
   * Manages reloading of the search engines when something in the user's
   * environment or the configuration has changed.
   *
   * The order of work here is designed to avoid potential issues when updating
   * the default engines, so that we're not removing active defaults or trying
   * to set a default to something that hasn't been added yet. The order is:
   *
   * 1) Update exising engines that are in both the old and new configuration.
   * 2) Add any new engines from the new configuration.
   * 3) Check for changes needed to the default engines due to environment changes
   *    and potentially overriding engines as per the override allowlist.
   * 4) Update the default engines.
   * 5) Remove any old engines.
   *
   * This is prefixed with _ rather than # because it is called in
   * test_remove_engine_notification_box.js
   *
   * @param {object} settings
   *   The user's current saved settings.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason reload engines is being called.
   */
  async _reloadEngines(settings, changeReason) {
    // Capture the current engine state, in case we need to notify below.
    let prevCurrentEngine = this.#currentEngine;
    let prevPrivateEngine = this.#currentPrivateEngine;
    let prevMetaData = { ...settings?.metaData };

    // Ensure that we don't set the useSavedOrder flag whilst we're doing this.
    // This isn't a user action, so we shouldn't be switching it.
    this.#dontSetUseSavedOrder = true;

    let refinedConfig = await this._fetchEngineSelectorEngines();

    let availableConfigEngines = [...refinedConfig.engines];
    let oldEngineList = [...this._engines.values()];

    for (let engine of oldEngineList) {
      if (!(engine instanceof lazy.ConfigSearchEngine)) {
        if (engine instanceof lazy.AddonSearchEngine) {
          // If this is an add-on search engine, check to see if it needs
          // an update.
          await engine
            .update()
            .catch(ex =>
              lazy.logConsole.error(
                `Failed to update add-on search engine ${engine.id}`,
                ex
              )
            );
        }
        continue;
      }

      let index = availableConfigEngines.findIndex(
        e => e.identifier == engine.id
      );
      let configuration = availableConfigEngines[index];

      if (!configuration && engine.getAttr("user-installed")) {
        configuration =
          await this.#engineSelector.findContextualSearchEngineById(engine.id);
      }

      if (!configuration) {
        this.#enginesPendingRemoval.add(engine);
        continue;
      } else {
        // This is an existing engine that we should update. (However
        // notification will happen only if the configuration for this engine
        // has changed).

        // Check if a config engine should get converted.
        let willBeAppProvided = index != -1;
        if (
          willBeAppProvided &&
          engine instanceof lazy.UserInstalledConfigEngine
        ) {
          engine.upgrade();
        } else if (
          !willBeAppProvided &&
          engine instanceof lazy.AppProvidedConfigEngine
        ) {
          engine.downgrade();
        }

        engine.update({ configuration });
      }

      availableConfigEngines.splice(index, 1);
    }

    let existingDuplicateEngines = [];

    // Any remaining configuration engines are ones that we need to add.
    for (let engine of availableConfigEngines) {
      try {
        let newAppEngine = new lazy.AppProvidedConfigEngine({
          config: engine,
          settings,
        });

        // If this is a duplicate name, keep track of the old engine as we need
        // to handle it later.
        let duplicateEngine = this.#getEngineByName(newAppEngine.name);
        if (duplicateEngine) {
          existingDuplicateEngines.push({
            duplicateEngine,
            newAppEngine,
          });
        }
        // We add our new engine to the store anyway, as we know it is an
        // application provided engine which will take priority over the
        // duplicate.
        this.#addEngineToStore(newAppEngine, true);
      } catch (ex) {
        lazy.logConsole.warn(
          "Could not load app provided search engine id:",
          engine.identifier,
          ex
        );
      }
    }

    // Now set the sort out the default engines and notify as appropriate.

    // Clear the current values, so that we'll completely reset.
    this.#currentEngine = null;
    this.#currentPrivateEngine = null;

    // If the user's default is one of the private engines that is being removed,
    // reset the stored setting, so that we correctly detect the change in
    // in default.
    if (this.#enginesPendingRemoval.has(prevCurrentEngine)) {
      this._settings.setMetaDataAttribute("defaultEngineId", "");
    }
    if (this.#enginesPendingRemoval.has(prevPrivateEngine)) {
      this._settings.setMetaDataAttribute("privateDefaultEngineId", "");
    }

    this.#setDefaultFromSelector(refinedConfig);

    let skipDefaultChangedNotification = false;

    for (let { duplicateEngine, newAppEngine } of existingDuplicateEngines) {
      if (prevCurrentEngine && prevCurrentEngine == duplicateEngine) {
        if (
          (duplicateEngine instanceof lazy.AddonSearchEngine ||
            duplicateEngine instanceof lazy.OpenSearchEngine) &&
          (await lazy.defaultOverrideAllowlist.canEngineOverride(
            duplicateEngine,
            newAppEngine?.id
          ))
        ) {
          lazy.logConsole.log(
            "Applying override from",
            duplicateEngine.id,
            "to application engine",
            newAppEngine.id,
            "and setting app engine default"
          );
          // This engine was default, and is allowed to override our application
          // provided engines, so update the application engine and set it as
          // default.
          newAppEngine.overrideWithEngine({
            engine: duplicateEngine,
          });

          this.#setEngineDefault(
            false,
            newAppEngine,
            this.CHANGE_REASON.CONFIG
          );
          // We're removing the old engine and we've changed the default, but this
          // is intentional and effectively everything is the same for the user, so
          // don't notify.
          skipDefaultChangedNotification = true;
        }
      }
      this.#enginesPendingRemoval.add(duplicateEngine);
    }

    if (this.#enginesPendingRemoval.has(prevCurrentEngine)) {
      skipDefaultChangedNotification ||=
        await this.#maybeRestoreEngineFromOverride(prevCurrentEngine);
    }

    // If the defaultEngine has changed between the previous load and this one,
    // dispatch the appropriate notifications.
    if (prevCurrentEngine && this.defaultEngine !== prevCurrentEngine) {
      this.#updateTelemetryDueToDefaultEngineChange(
        false,
        prevCurrentEngine,
        this.defaultEngine,
        changeReason
      );
      lazy.SearchUtils.notifyAction(
        this.#currentEngine,
        lazy.SearchUtils.MODIFIED_TYPE.DEFAULT
      );
      // If we've not got a separate private active, notify update of the
      // private so that the UI updates correctly.
      if (!this.#separatePrivateDefault) {
        lazy.SearchUtils.notifyAction(
          this.#currentEngine,
          lazy.SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE
        );
      }

      if (
        !skipDefaultChangedNotification &&
        prevMetaData &&
        settings.metaData &&
        !this.#didSettingsMetaDataUpdate(prevMetaData) &&
        this.#enginesPendingRemoval.has(prevCurrentEngine) &&
        Services.prefs.getBoolPref("browser.search.removeEngineInfobar.enabled")
      ) {
        this._showRemovalOfSearchEngineNotificationBox(
          prevCurrentEngine.name,
          this.defaultEngine.name
        );
      }
    }

    if (
      this.#separatePrivateDefault &&
      prevPrivateEngine &&
      this.defaultPrivateEngine !== prevPrivateEngine
    ) {
      this.#updateTelemetryDueToDefaultEngineChange(
        true,
        prevPrivateEngine,
        this.defaultPrivateEngine,
        changeReason
      );
      lazy.SearchUtils.notifyAction(
        this.#currentPrivateEngine,
        lazy.SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE
      );
    }

    // Finally, remove any engines that need removing. We do this after sorting
    // out the new default, as otherwise this could cause multiple notifications
    // and the wrong engine to be selected as default.
    await this.#maybeRemoveEnginesAfterReload(this._engines);

    // Save app default engine to the user's settings metaData incase it has
    // been updated
    this._settings.setMetaDataAttribute(
      "appDefaultEngineId",
      this.appDefaultEngine?.id
    );

    // If we are leaving an experiment, and the default is the same as the
    // application default, we reset the user's setting to blank, so that
    // future changes of the application default engine may take effect.
    if (
      prevMetaData.experiment &&
      !this._settings.getMetaDataAttribute("experiment")
    ) {
      if (this.defaultEngine == this.appDefaultEngine) {
        this._settings.setVerifiedMetaDataAttribute("defaultEngineId", "");
      }
      if (
        this.#separatePrivateDefault &&
        this.defaultPrivateEngine == this.appPrivateDefaultEngine
      ) {
        this._settings.setVerifiedMetaDataAttribute(
          "privateDefaultEngineId",
          ""
        );
      }
    }

    this.#dontSetUseSavedOrder = false;
    // Clear out the sorted engines settings, so that we re-sort it if necessary.
    this._cachedSortedEngines = null;
    Services.obs.notifyObservers(
      null,
      lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
      "engines-reloaded"
    );
  }

  /**
   * Potentially restores an engine if it was previously overriding the app
   * provided engine.
   *
   * @param {SearchEngine} prevCurrentEngine
   *   The previous current engine to check for override.
   * @returns {Promise<boolean>}
   *   True if an engine was restored.
   */
  async #maybeRestoreEngineFromOverride(prevCurrentEngine) {
    let overriddenBy = prevCurrentEngine.getAttr("overriddenBy");
    if (!overriddenBy) {
      return false;
    }
    let overriddenByOpenSearch = prevCurrentEngine.getAttr(
      "overriddenByOpenSearch"
    );
    let engine;
    if (overriddenByOpenSearch) {
      engine = new lazy.OpenSearchEngine({
        json: overriddenByOpenSearch,
      });
    } else {
      // The previous application default engine is being removed, and it was
      // overridden by another engine. We want to put the previous engine back,
      // so that the user retains that engine as default.
      engine = new lazy.AddonSearchEngine({
        details: {
          extensionID: overriddenBy,
        },
      });
      try {
        await engine.init();
      } catch (ex) {
        // If there is an error, the add-on may no longer be available, or
        // there was some other issue with the settings.
        lazy.logConsole.error(
          "Error restoring overridden engine",
          overriddenBy,
          ex
        );
        return false;
      }
    }
    engine.copyUserSettingsFrom(prevCurrentEngine);
    this.#addEngineToStore(engine, true);

    // Now set it back to default.
    this.#setEngineDefault(false, engine, this.CHANGE_REASON.CONFIG);
    return true;
  }

  /**
   * Remove any engines that have been flagged for removal during reloadEngines.
   *
   * @param {Map<string, SearchEngine>} engines
   *   The list of engines to check.
   */
  async #maybeRemoveEnginesAfterReload(engines) {
    for (let engine of engines.values()) {
      if (!this.#enginesPendingRemoval.has(engine)) {
        continue;
      }

      // Use the internal remove - _reloadEngines already deals with default
      // engines etc, and we want to avoid adjusting the sort order unnecessarily.
      this.#internalRemoveEngine(engine);

      if (engine instanceof lazy.ConfigSearchEngine) {
        await engine.cleanup();
      }

      lazy.SearchUtils.notifyAction(
        engine,
        lazy.SearchUtils.MODIFIED_TYPE.REMOVED
      );
    }
  }

  /**
   * Adds an engine to the store and notifies observers.
   *
   * @param {SearchEngine} engine
   *   The search engine to add.
   * @param {boolean} [skipDuplicateCheck]
   *   True to override engines with the same name.
   */
  #addEngineToStore(engine, skipDuplicateCheck = false) {
    if (this.#engineMatchesIgnoreLists(engine)) {
      return;
    }

    lazy.logConsole.debug("#addEngineToStore: Adding engine:", engine.name);

    // See if there is an existing engine with the same name.
    if (!skipDuplicateCheck && this.#getEngineByName(engine.name)) {
      throw new lazy.SearchEngineInstallError(
        "duplicate-title",
        `An engine called ${engine.name} already exists!`
      );
    }

    // Not an update, just add the new engine.
    this._engines.set(engine.id, engine);
    // Only add the engine to the list of sorted engines if the initial list
    // has already been built (i.e. if this._cachedSortedEngines is non-null). If
    // it hasn't, we're loading engines from disk and the sorted engine list
    // will be built once we need it.
    if (this._cachedSortedEngines && !this.#dontSetUseSavedOrder) {
      this._cachedSortedEngines.push(engine);
      this.#saveSortedEngineList();
    }
    lazy.SearchUtils.notifyAction(engine, lazy.SearchUtils.MODIFIED_TYPE.ADDED);

    // Let the engine know it can start notifying new updates.
    engine._engineAddedToStore = true;
  }

  /**
   * Loads any search engines specified by enterprise policies.
   *
   * @param {object} [settings]
   *   The saved settings for the user.
   */
  #loadEnginesFromPolicies(settings) {
    if (Services.policies?.status != Ci.nsIEnterprisePolicies.ACTIVE) {
      return;
    }

    let activePolicies = Services.policies.getActivePolicies();
    if (!activePolicies.SearchEngines) {
      return;
    }
    for (let engineDetails of activePolicies.SearchEngines.Add ?? []) {
      this.#addPolicyEngine(engineDetails, settings);
    }
  }

  /**
   * Loads remaining user search engines from settings.
   *
   * @param {object} [settings]
   *   The saved settings for the user.
   * @param {SearchEngineDefinition[]} [engines]
   *   The saved settings for the user.
   * @returns {Promise<boolean>}
   *   Returns true if the default engine was changed.
   */
  async #loadEnginesFromSettings(settings, engines) {
    if (!settings.engines) {
      return false;
    }

    lazy.logConsole.debug(
      "#loadEnginesFromSettings: Loading",
      settings.engines.length,
      "engines from settings"
    );

    let defaultEngineChanged = false;
    let skippedEngines = 0;
    let appProvidedEngineIds = new Set(engines.map(e => e.identifier));
    for (let engineJSON of settings.engines) {
      let willBeUserInstalled =
        !!engineJSON._metaData?.["user-installed"] &&
        // A config engine with user-installed attribute still counts as
        // app-provided if it's also app-provided.
        // In that case, it's installed by #loadEnginesFromConfig.
        !appProvidedEngineIds.has(engineJSON.id);
      if (engineJSON._isConfigEngine && !willBeUserInstalled) {
        ++skippedEngines;
        continue;
      }

      // Some OpenSearch type engines are now obsolete and no longer supported.
      // These were application provided engines that used to use the OpenSearch
      // format before gecko transitioned to WebExtensions.
      // These will sometimes have been missed in migration due to various
      // reasons, and due to how the settings saves everything. We therefore
      // explicitly ignore them here to drop them, and let the rest of the code
      // fallback to the application/distribution default if necessary.
      let loadPath = engineJSON._loadPath?.toLowerCase();
      if (
        loadPath &&
        // Replaced by application provided in Firefox 79.
        (loadPath.startsWith("[distribution]") ||
          // Langpack engines moved in-app in Firefox 62.
          // Note: these may be prefixed by jar:,
          loadPath.includes("[app]/extensions/langpack") ||
          loadPath.includes("[other]/langpack") ||
          loadPath.includes("[profile]/extensions/langpack") ||
          // Old omni.ja engines also moved to in-app in Firefox 62.
          loadPath.startsWith("jar:[app]/omni.ja"))
      ) {
        continue;
      }

      try {
        let engine;
        if (loadPath?.startsWith("[policy]")) {
          skippedEngines++;
          continue;
        } else if (loadPath?.startsWith("[user]")) {
          engine = new lazy.UserSearchEngine({ json: engineJSON });
        } else if (engineJSON.extensionID ?? engineJSON._extensionID) {
          let existingEngine = this.#getEngineByName(engineJSON._name);
          let extensionId = engineJSON.extensionID ?? engineJSON._extensionID;

          if (
            existingEngine instanceof lazy.AddonSearchEngine &&
            existingEngine.extensionID == extensionId
          ) {
            // We assume that this WebExtension was already loaded as part of
            // #loadStartupEngines, and therefore do not try to add it again.
            lazy.logConsole.log(
              "Ignoring already added WebExtension",
              extensionId
            );
            continue;
          }

          engine = new lazy.AddonSearchEngine({
            json: engineJSON,
          });
        } else if (engineJSON._isConfigEngine && willBeUserInstalled) {
          let config =
            await this.#engineSelector.findContextualSearchEngineById(
              engineJSON.id
            );
          engine = new lazy.UserInstalledConfigEngine({ config, settings });
        } else {
          engine = new lazy.OpenSearchEngine({
            json: engineJSON,
          });
        }
        // Only check the override for Add-on or OpenSearch engines, and only
        // if they are the default engine.
        if (
          (engine instanceof lazy.OpenSearchEngine ||
            engine instanceof lazy.AddonSearchEngine) &&
          settings.metaData?.defaultEngineId == engine.id
        ) {
          defaultEngineChanged = await this.#maybeApplyOverride(engine);
          if (defaultEngineChanged) {
            continue;
          }
        }
        this.#addEngineToStore(engine);
      } catch (ex) {
        lazy.logConsole.error(
          "Failed to load",
          engineJSON._name,
          "from settings:",
          ex,
          engineJSON
        );
      }
    }

    if (skippedEngines) {
      lazy.logConsole.debug(
        "#loadEnginesFromSettings: skipped",
        skippedEngines,
        "built-in/policy engines."
      );
    }
    return defaultEngineChanged;
  }

  /**
   * Looks to see if an override may be applied to an application engine
   * if the supplied engine is a duplicate of it. This should only be called
   * in the case where the engine would become the default engine.
   *
   * @param {AddonSearchEngine|OpenSearchEngine} engine
   *   The search engine to check to see if it should override an existing engine.
   * @returns {Promise<boolean>}
   *  True if the default engine was changed.
   */
  async #maybeApplyOverride(engine) {
    // If an engine with the same name already exists, we're not going to
    // be allowed to add it - however, if it is default, and it
    // matches an existing engine, then we might be allowed to
    // override the application provided engine.
    let existingEngine = this.#getEngineByName(engine.name);
    if (
      existingEngine?.isConfigEngine &&
      (await lazy.defaultOverrideAllowlist.canEngineOverride(
        engine,
        existingEngine?.id
      ))
    ) {
      existingEngine.overrideWithEngine({
        engine,
      });
      this.#setEngineDefault(
        false,
        existingEngine,
        // We assume that the application provided engine was added due
        // to a configuration change. It is possible that it was actually
        // due to a locale/region change, but that is harder to detect
        // here.
        this.CHANGE_REASON.CONFIG
      );
      return true;
    }
    return false;
  }

  // This is prefixed with _ rather than # because it is
  // called in test_remove_engine_notification_box.js
  async _fetchEngineSelectorEngines() {
    let searchEngineSelectorProperties = {
      locale: Services.locale.appLocaleAsBCP47,
      region: lazy.Region.home || "unknown",
      channel: lazy.SearchUtils.MODIFIED_APP_CHANNEL,
      experiment: this.#lazyPrefs.experimentPrefValue,
      distroID: lazy.SearchUtils.distroID ?? "",
    };

    for (let [key, value] of Object.entries(searchEngineSelectorProperties)) {
      this._settings.setMetaDataAttribute(key, value);
    }

    return this.#engineSelector.fetchEngineConfiguration(
      searchEngineSelectorProperties
    );
  }

  #setDefaultFromSelector(refinedConfig) {
    this.#searchDefault = refinedConfig.appDefaultEngineId;
    this.#searchPrivateDefault = refinedConfig.appPrivateDefaultEngineId;
  }

  #saveSortedEngineList() {
    lazy.logConsole.debug("#saveSortedEngineList");

    // Set the useSavedOrder attribute to indicate that from now on we should
    // use the user's order information stored in settings.
    this._settings.setMetaDataAttribute("useSavedOrder", true);

    var engines = this.#sortedEngines;

    for (var i = 0; i < engines.length; ++i) {
      engines[i].setAttr("order", i + 1);
    }
  }

  #buildSortedEngineList() {
    // We must initialise _cachedSortedEngines here to avoid infinite recursion
    // in the case of tests which don't define a default search engine.
    // If there's no default defined, then we revert to the first item in the
    // sorted list, but we can't do that if we don't have a list.
    this._cachedSortedEngines = [];

    // If the user has specified a custom engine order, read the order
    // information from the metadata instead of the default prefs.
    if (this._settings.getMetaDataAttribute("useSavedOrder")) {
      lazy.logConsole.debug("#buildSortedEngineList: using saved order");
      let addedEngines = {};

      // Flag to keep track of whether or not we need to call #saveSortedEngineList.
      let needToSaveEngineList = false;

      for (let engine of this._engines.values()) {
        var orderNumber = engine.getAttr("order");

        // Since the DB isn't regularly cleared, and engine files may disappear
        // without us knowing, we may already have an engine in this slot. If
        // that happens, we just skip it - it will be added later on as an
        // unsorted engine.
        if (orderNumber && !this._cachedSortedEngines[orderNumber - 1]) {
          this._cachedSortedEngines[orderNumber - 1] = engine;
          addedEngines[engine.name] = engine;
        } else {
          // We need to call #saveSortedEngineList so this gets sorted out.
          needToSaveEngineList = true;
        }
      }

      // Filter out any nulls for engines that may have been removed
      var refinedConfig = this._cachedSortedEngines.filter(function (a) {
        return !!a;
      });
      if (this._cachedSortedEngines.length != refinedConfig.length) {
        needToSaveEngineList = true;
      }
      this._cachedSortedEngines = refinedConfig;

      if (needToSaveEngineList) {
        this.#saveSortedEngineList();
      }

      // Array for the remaining engines, alphabetically sorted.
      let alphaEngines = [];

      for (let engine of this._engines.values()) {
        if (!(engine.name in addedEngines)) {
          alphaEngines.push(engine);
        }
      }

      const collator = new Intl.Collator();
      alphaEngines.sort((a, b) => {
        return collator.compare(a.name, b.name);
      });
      return (this._cachedSortedEngines =
        this._cachedSortedEngines.concat(alphaEngines));
    }
    lazy.logConsole.debug("#buildSortedEngineList: using default orders");

    return (this._cachedSortedEngines = lazy.SearchUtils.sortEnginesByDefaults({
      engines: Array.from(this._engines.values()),
      appDefaultEngine: this.appDefaultEngine,
      appPrivateDefaultEngine: this.appPrivateDefaultEngine,
    }));
  }

  /**
   * Get a sorted array of the visible engines.
   *
   * @returns {SearchEngine[]}
   */
  get #sortedVisibleEngines() {
    return this.#sortedEngines.filter(engine => !engine.hidden);
  }

  /**
   * Migrates legacy add-ons which used the OpenSearch definitions to
   * WebExtensions, if an equivalent WebExtension is installed.
   *
   * Run during the background checks.
   */
  async #migrateLegacyEngines() {
    lazy.logConsole.debug("Running migrate legacy engines");

    const matchRegExp = /extensions\/(.*?)\.xpi!/i;
    for (let engine of this._engines.values()) {
      if (
        engine instanceof lazy.OpenSearchEngine &&
        engine._loadPath.includes("[profile]/extensions/")
      ) {
        let match = engine._loadPath.match(matchRegExp);
        if (match?.[1]) {
          // There's a chance here that the WebExtension might not be
          // installed any longer, even though the engine is. We'll deal
          // with that in `checkWebExtensionEngines`.
          let engines = this.#getEnginesByExtensionID(match[1]);
          if (engines.length) {
            lazy.logConsole.debug(
              `Migrating ${engine.name} to WebExtension install`
            );

            if (this.defaultEngine == engine) {
              this.#setEngineDefault(
                false,
                engines[0],
                this.CHANGE_REASON.CONFIG
              );
            }
            await this.removeEngine(engine, this.CHANGE_REASON.ADDON_INSTALL);
          }
        }
      }
    }

    lazy.logConsole.debug("Migrate legacy engines complete");
  }

  /**
   * Checks if Search Engines associated with WebExtensions are valid and
   * up-to-date, and reports them via telemetry if not.
   *
   * Run during the background checks.
   */
  async #checkWebExtensionEngines() {
    lazy.logConsole.debug("Running check on WebExtension engines");

    for (let engine of this._engines.values()) {
      if (engine instanceof lazy.AddonSearchEngine) {
        await engine.checkAndReportIfSettingsValid();
      }
    }
    lazy.logConsole.debug("WebExtension engine check complete");
  }

  /**
   * Counts the number of secure, insecure, securely updated and insecurely
   * updated OpenSearch engines the user has installed and reports those
   * counts via telemetry.
   *
   * Run during the background checks.
   */
  async #addOpenSearchTelemetry() {
    let totalSecure = 0;
    let totalInsecure = 0;
    let totalWithSecureUpdates = 0;
    let totalWithInsecureUpdates = 0;

    let engine;
    let searchURI;
    let updateURI;
    for (let elem of this._engines) {
      engine = elem[1];
      if (engine instanceof lazy.OpenSearchEngine) {
        searchURI = engine.searchURLWithNoTerms;
        updateURI = engine.updateURI;

        if (lazy.SearchUtils.isSecureURIForOpenSearch(searchURI)) {
          totalSecure++;
        } else {
          totalInsecure++;
        }

        if (updateURI && lazy.SearchUtils.isSecureURIForOpenSearch(updateURI)) {
          totalWithSecureUpdates++;
        } else if (updateURI) {
          totalWithInsecureUpdates++;
        }
      }
    }

    Glean.browserSearchinit.secureOpensearchEngineCount.set(totalSecure);
    Glean.browserSearchinit.insecureOpensearchEngineCount.set(totalInsecure);
    Glean.browserSearchinit.secureOpensearchUpdateCount.set(
      totalWithSecureUpdates
    );
    Glean.browserSearchinit.insecureOpensearchUpdateCount.set(
      totalWithInsecureUpdates
    );
  }

  /**
   * Removes application-provided extensions with a specific identifier.
   *
   * After search-config-v2 (enabled in Firefox version 128), app-provided
   * engines are no longer web extensions. This method iterates over the IDs
   * in `#extensionsToRemove` and uninstalls extensions ending with
   * `@search.mozilla.org`. Although the list should contain only app-provided
   * engines (as per addEnginesFromExtension), the `@search.mozilla.org` is an
   * additional safety check to ensure only the expected add-ons are removed.
   */
  async #removeAppProvidedExtensions() {
    for (let id of this.#extensionsToRemove.values()) {
      if (id.endsWith("@search.mozilla.org")) {
        let addOn = await lazy.AddonManager.getAddonByID(id);
        if (addOn) {
          await addOn.uninstall();
        }
      }
    }
    this.#extensionsToRemove.clear();
  }

  /**
   * Creates and adds a WebExtension based engine. It is expected that this
   * function is only called after initialisation has completed, or at a stage
   * where we are ready to load the engines we've been told about during startup.
   *
   * @param {object} options
   *   Options for the engine.
   * @param {Extension} options.extension
   *   An Extension object containing data about the extension.
   * @param {object} [options.settings]
   *   The saved settings for the user.
   */
  async #createAndAddAddonEngine({ extension, settings }) {
    // If we're in the startup cycle, and we've already loaded this engine,
    // then we use the existing one rather than trying to start from scratch.
    // This also avoids console errors.
    if (extension.startupReason == "APP_STARTUP") {
      let engine = this.#getEngineByWebExtensionDetails({
        id: extension.id,
      });
      if (engine) {
        lazy.logConsole.debug(
          "Engine already loaded via settings, skipping due to APP_STARTUP:",
          extension.id
        );
        return;
      }
    }

    lazy.logConsole.debug(
      "#createAndAddAddonEngine: installing:",
      extension.id
    );

    let shouldSetAsDefault = false;
    /** @type {Values<typeof this.CHANGE_REASON>} */
    let changeReason = this.CHANGE_REASON.UNKNOWN;

    for (let engine of this._engines.values()) {
      if (
        engine instanceof lazy.OpenSearchEngine &&
        engine._loadPath.startsWith(`jar:[profile]/extensions/${extension.id}`)
      ) {
        // This is a legacy extension engine that needs to be migrated to WebExtensions.
        lazy.logConsole.debug("Migrating existing engine");
        shouldSetAsDefault = shouldSetAsDefault || this.defaultEngine == engine;
        await this.removeEngine(engine, this.CHANGE_REASON.ADDON_INSTALL);
      }
    }

    let newEngine = new lazy.AddonSearchEngine({
      details: {
        extensionID: extension.id,
      },
    });
    await newEngine.init({
      settings,
      extension,
    });

    // If this extension is starting up, check to see if it previously overrode
    // an application provided engine that has now been removed from the user's
    // set-up. If the application provided engine has been removed and was
    // default, then we should set this engine back to default and copy
    // the settings across.
    if (extension.startupReason == "APP_STARTUP") {
      if (!settings) {
        settings = await this._settings.get();
      }
      // We check the saved settings for the overridden flag, because if the engine
      // has been removed, we won't have that in _engines.
      let previouslyOverridden = settings.engines?.find(
        e => !!e._metaData.overriddenBy
      );
      if (previouslyOverridden) {
        // Only allow override if we were previously overriding and the
        // engine is no longer installed, and the new engine still matches the
        // override allow list.
        if (
          previouslyOverridden._metaData.overriddenBy == extension.id &&
          !this._engines.get(previouslyOverridden.id) &&
          (await lazy.defaultOverrideAllowlist.canEngineOverride(
            newEngine,
            previouslyOverridden.id
          ))
        ) {
          shouldSetAsDefault = true;
          // We assume that the app provided engine was removed due to a
          // configuration change, and therefore we have re-added the add-on
          // search engine. It is possible that it was actually due to a
          // locale/region change, but that is harder to detect here.
          changeReason = this.CHANGE_REASON.CONFIG;
          newEngine.copyUserSettingsFrom(previouslyOverridden);
        }
      }
    }

    this.#addEngineToStore(newEngine);
    if (shouldSetAsDefault) {
      this.#setEngineDefault(false, newEngine, changeReason);
    }
  }

  /**
   * Called when we see an upgrade to an existing search extension.
   *
   * @param {object} extension
   *   An Extension object containing data about the extension.
   */
  async #upgradeExtensionEngine(extension) {
    let extensionEngines = this.#getEnginesByExtensionID(extension.id);

    for (let engine of extensionEngines) {
      let isDefault = engine == this.defaultEngine;
      let isDefaultPrivate = engine == this.defaultPrivateEngine;

      let originalName = engine.name;

      await engine.update({
        extension,
      });

      if (engine.name != originalName) {
        if (isDefault) {
          this._settings.setVerifiedMetaDataAttribute(
            "defaultEngineId",
            engine.id
          );
        }
        if (isDefaultPrivate) {
          this._settings.setVerifiedMetaDataAttribute(
            "privateDefaultEngineId",
            engine.id
          );
        }
        this._cachedSortedEngines = null;
      }
    }
    return extensionEngines;
  }

  /**
   * Removes a search engine from _sortedEngines and _engines.
   *
   * @param {SearchEngine} engine
   *   The search engine to remove.
   */
  #internalRemoveEngine(engine) {
    // Remove the engine from _sortedEngines
    if (this._cachedSortedEngines) {
      var index = this._cachedSortedEngines.indexOf(engine);
      if (index == -1) {
        throw new Error("Unable to find engine to remove in the cache");
      }
      this._cachedSortedEngines.splice(index, 1);
    }

    // Remove the engine from the internal store
    this._engines.delete(engine.id);
  }

  /**
   * Helper function to find a new default engine and set it. This could
   * be used if there is not default set yet, or if the current default is
   * being removed.
   *
   * This function will not consider engines in #enginesPendingRemoval.
   *
   * The new default will be chosen from (in order):
   *
   * - Existing default from configuration, if it is not hidden.
   * - The first non-hidden engine that is a general search engine.
   * - If all other engines are hidden, unhide the default from the configuration.
   * - If the default from the configuration is the one being removed, unhide
   *   the first general search engine, or first visible engine.
   *
   * @param {object} options
   *   The options object.
   * @param {boolean} options.privateMode
   *   If true, returns the default engine for private browsing mode, otherwise
   *   the default engine for the normal mode. Note, this function does not
   *   check the "separatePrivateDefault" preference - that is up to the caller.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason for the change of default engine.
   * @returns {?SearchEngine}
   *   The appropriate search engine, or null if one could not be determined.
   */
  #findAndSetNewDefaultEngine({ privateMode }, changeReason) {
    // First to the app default engine...
    let newDefault = privateMode
      ? this.appPrivateDefaultEngine
      : this.appDefaultEngine;

    if (
      !newDefault ||
      newDefault.hidden ||
      this.#enginesPendingRemoval.has(newDefault)
    ) {
      let sortedEngines = this.#sortedVisibleEngines;
      let generalSearchEngines = sortedEngines.filter(
        e => e.isGeneralPurposeEngine
      );

      // then to the first visible general search engine that isn't excluded...
      let firstVisible = generalSearchEngines.find(
        e => !this.#enginesPendingRemoval.has(e)
      );
      if (firstVisible) {
        newDefault = firstVisible;
      } else if (newDefault) {
        // then to the app default if it is not the one that is excluded...
        if (!this.#enginesPendingRemoval.has(newDefault)) {
          newDefault.hidden = false;
        } else {
          newDefault = null;
        }
      }

      // and finally as a last resort we unhide the first engine
      // even if the name is the same as the excluded one (should never happen).
      if (!newDefault) {
        if (!firstVisible) {
          sortedEngines = this.#sortedEngines;
          firstVisible = sortedEngines.find(e => e.isGeneralPurposeEngine);
          if (!firstVisible) {
            firstVisible = sortedEngines[0];
          }
        }
        if (firstVisible) {
          firstVisible.hidden = false;
          newDefault = firstVisible;
        }
      }
    }
    // We tried out best but something went very wrong.
    if (!newDefault) {
      lazy.logConsole.error("Could not find a replacement default engine.");
      return null;
    }

    // If the current engine wasn't set or was hidden, we used a fallback
    // to pick a new current engine. As soon as we return it, this new
    // current engine will become user-visible, so we should persist it.
    // by calling the setter.
    this.#setEngineDefault(privateMode, newDefault, changeReason);

    return privateMode ? this.#currentPrivateEngine : this.#currentEngine;
  }

  /**
   * Helper function to set the current default engine.
   *
   * @param {boolean} privateMode
   *   If true, sets the default engine for private browsing mode, otherwise
   *   sets the default engine for the normal mode. Note, this function does not
   *   check the "separatePrivateDefault" preference - that is up to the caller.
   * @param {SearchEngine} newEngine
   *   The search engine to select.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason for the default search engine change.
   */
  #setEngineDefault(privateMode, newEngine, changeReason) {
    if (!(newEngine instanceof lazy.SearchEngine)) {
      throw new TypeError("newEngine is not a SearchEngine instance");
    }

    const newCurrentEngine = this._engines.get(newEngine.id);
    if (!newCurrentEngine) {
      throw new Error("Unable to find the new engine in the engine store");
    }

    if (!newCurrentEngine.isConfigEngine) {
      // If a non config engine is being set as the current engine,
      // ensure its loadPath has a verification hash.
      if (!newCurrentEngine._loadPath) {
        newCurrentEngine._loadPath = "[other]unknown";
      }
      let loadPathHash = lazy.SearchUtils.getVerificationHash(
        newCurrentEngine._loadPath
      );
      let currentHash = newCurrentEngine.getAttr("loadPathHash");
      if (!currentHash || currentHash != loadPathHash) {
        newCurrentEngine.setAttr("loadPathHash", loadPathHash);
        lazy.SearchUtils.notifyAction(
          newCurrentEngine,
          lazy.SearchUtils.MODIFIED_TYPE.CHANGED
        );
      }
    }

    let currentEngine = privateMode
      ? this.#currentPrivateEngine
      : this.#currentEngine;

    if (newCurrentEngine == currentEngine) {
      return;
    }

    // Ensure that we reset an engine override if it was previously overridden.
    currentEngine?.removeExtensionOverride();

    if (privateMode) {
      this.#currentPrivateEngine = newCurrentEngine;
    } else {
      this.#currentEngine = newCurrentEngine;
    }

    // If we change the default engine in the future, that change should impact
    // users who have switched away from and then back to the build's
    // "app default" engine. So clear the user pref when the currentEngine is
    // set to the build's app default engine, so that the currentEngine getter
    // falls back to whatever the default is.
    // However, we do not do this whilst we are running an experiment - an
    // experiment must preseve the user's choice of default engine during it's
    // runtime and when it ends. Once the experiment ends, we will reset the
    // attribute elsewhere.
    let newId = newCurrentEngine.id;
    const appDefaultEngine = privateMode
      ? this.appPrivateDefaultEngine
      : this.appDefaultEngine;
    if (
      newCurrentEngine == appDefaultEngine &&
      !this.#lazyPrefs.experimentPrefValue
    ) {
      newId = "";
    }

    this._settings.setVerifiedMetaDataAttribute(
      privateMode ? "privateDefaultEngineId" : "defaultEngineId",
      newId
    );

    // Only do this if we're initialized though - this function can get called
    // during initalization.
    if (this.isInitialized) {
      this.#updateTelemetryDueToDefaultEngineChange(
        privateMode,
        currentEngine,
        newCurrentEngine,
        changeReason
      );
    }

    lazy.SearchUtils.notifyAction(
      newCurrentEngine,
      lazy.SearchUtils.MODIFIED_TYPE[
        privateMode ? "DEFAULT_PRIVATE" : "DEFAULT"
      ]
    );
    // If we've not got a separate private active, notify update of the
    // private so that the UI updates correctly.
    if (!privateMode && !this.#separatePrivateDefault) {
      lazy.SearchUtils.notifyAction(
        newCurrentEngine,
        lazy.SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE
      );
    }
  }

  #onSeparateDefaultPrefChanged(prefName, previousValue, currentValue) {
    // Clear out the sorted engines settings, so that we re-sort it if necessary.
    this._cachedSortedEngines = null;
    // We should notify if the normal default, and the currently saved private
    // default are different. Otherwise, save the energy.
    if (this.defaultEngine != this._getEngineDefault(true)) {
      lazy.SearchUtils.notifyAction(
        // Always notify with the new private engine, the function checks
        // the preference value for us.
        this.defaultPrivateEngine,
        lazy.SearchUtils.MODIFIED_TYPE.DEFAULT_PRIVATE
      );
    }

    let eventReason = prefName.endsWith("separatePrivateDefault.ui.enabled")
      ? this.CHANGE_REASON.USER_PRIVATE_PREF_ENABLED
      : this.CHANGE_REASON.USER_PRIVATE_SPLIT;
    if (!previousValue && currentValue) {
      this.#updateTelemetryDueToDefaultEngineChange(
        true,
        null,
        this._getEngineDefault(true),
        eventReason
      );
    } else {
      this.#updateTelemetryDueToDefaultEngineChange(
        true,
        this._getEngineDefault(true),
        null,
        eventReason
      );
    }
  }

  /**
   * Gets summary information for an engine to report to telemetry.
   *
   * @param {SearchEngine} engine
   */
  #getEngineInfo(engine) {
    if (!engine) {
      // The defaultEngine getter will throw if there's no engine at all,
      // which shouldn't happen unless an add-on or a test deleted all of them.
      // Our preferences UI doesn't let users do that.
      lazy.logConsole.error("getEngineInfo: No default engine");
      return {
        providerId: "NONE",
        partnerCode: "NONE",
        overriddenByThirdParty: false,
        telemetryId: "NONE",
        loadPath: "NONE",
        name: "NONE",
        submissionURL: "NONE",
      };
    }

    // When an engine is overridden by a third party, then we report the
    // override and skip reporting the partner code, since we don't have
    // a requirement to report the partner code in that case.
    let isOverridden = !!engine.overriddenById;

    let engineInfo = {
      providerId: engine.isConfigEngine ? engine.id : "other",
      partnerCode: isOverridden ? "" : engine.partnerCode,
      overriddenByThirdParty: isOverridden,
      telemetryId: engine.telemetryId,
      loadPath: engine.loadPath,
      name: engine.name ? engine.name : "",
      /** @type {string?} */
      submissionURL: undefined,
    };

    // For privacy, we only collect the submission URL for config engines...
    let sendSubmissionURL = engine.isConfigEngine;

    if (!sendSubmissionURL) {
      // ... or engines that are the same domain as a config engine.
      let engineHost = engine.searchUrlDomain;
      for (let innerEngine of this._engines.values()) {
        if (!innerEngine.isConfigEngine) {
          continue;
        }

        if (innerEngine.searchUrlDomain == engineHost) {
          sendSubmissionURL = true;
          break;
        }
      }

      if (!sendSubmissionURL) {
        // ... or well known search domains.
        //
        // Starts with: www.google., search.aol., yandex.
        // or
        // Ends with: search.yahoo.com, .ask.com, .bing.com, .startpage.com, baidu.com, duckduckgo.com
        const urlTest =
          /^(?:www\.google\.|search\.aol\.|yandex\.)|(?:search\.yahoo|\.ask|\.bing|\.startpage|\.baidu|duckduckgo)\.com$/;
        sendSubmissionURL = urlTest.test(engineHost);
      }
    }

    if (sendSubmissionURL) {
      let uri = engine.searchURLWithNoTerms;
      uri = uri
        .mutate()
        .setUserPass("") // Avoid reporting a username or password.
        .finalize();
      engineInfo.submissionURL = uri.spec;
    }

    return engineInfo;
  }

  /**
   * Records the telemetry event when the default engine has changed, and
   * also updates the related non-event probes.
   *
   * @param {boolean} isPrivate
   *   True if this is a event about a private engine.
   * @param {SearchEngine} [previousEngine]
   *   The previously default search engine.
   * @param {SearchEngine} [newEngine]
   *   The new default search engine.
   * @param {Values<typeof this.CHANGE_REASON>} changeReason
   *   The reason for the default search engine change
   */
  #updateTelemetryDueToDefaultEngineChange(
    isPrivate,
    previousEngine,
    newEngine,
    changeReason = this.CHANGE_REASON.UNKNOWN
  ) {
    let engineInfo;
    // If we are toggling the separate private browsing settings, we might not
    // have an engine to record.
    if (newEngine) {
      engineInfo = this.#getEngineInfo(newEngine);
    }

    let submissionURL = engineInfo?.submissionURL ?? "";
    /** @type {Parameters<typeof Glean.searchEngineDefault.changed.record>[0]} */
    let extraArgs = {
      // In docshell tests, the previous engine does not exist, so we allow
      // for the previousEngine to be undefined.
      previous_engine_id: previousEngine?.telemetryId ?? "",
      new_engine_id: engineInfo?.telemetryId ?? "",
      new_display_name: engineInfo?.name ?? "",
      new_load_path: engineInfo?.loadPath ?? "",
      // Glean has a limit of 100 characters.
      new_submission_url: submissionURL.slice(0, 100),
      change_reason: changeReason,
    };
    if (isPrivate) {
      Glean.searchEnginePrivate.changed.record(extraArgs);
    } else {
      Glean.searchEngineDefault.changed.record(extraArgs);
    }
    this.#recordDefaultEngineTelemetryData();
  }

  /**
   * Records the user's current default engine (normal and private) data to
   * telemetry.
   */
  #recordDefaultEngineTelemetryData() {
    let engineInfo = this.#getEngineInfo(this.defaultEngine);

    Glean.searchEngineDefault.providerId.set(engineInfo.providerId);
    Glean.searchEngineDefault.partnerCode.set(engineInfo.partnerCode);
    Glean.searchEngineDefault.overriddenByThirdParty.set(
      engineInfo.overriddenByThirdParty
    );
    Glean.searchEngineDefault.engineId.set(engineInfo.telemetryId);
    Glean.searchEngineDefault.displayName.set(engineInfo.name);
    Glean.searchEngineDefault.loadPath.set(engineInfo.loadPath);
    Glean.searchEngineDefault.submissionUrl.set(
      engineInfo.submissionURL ?? "blank:"
    );

    if (this.#separatePrivateDefault) {
      let privateEngineInfo = this.#getEngineInfo(this.defaultPrivateEngine);

      Glean.searchEnginePrivate.providerId.set(privateEngineInfo.providerId);
      Glean.searchEnginePrivate.partnerCode.set(privateEngineInfo.partnerCode);
      Glean.searchEnginePrivate.overriddenByThirdParty.set(
        privateEngineInfo.overriddenByThirdParty
      );
      Glean.searchEnginePrivate.engineId.set(privateEngineInfo.telemetryId);
      Glean.searchEnginePrivate.displayName.set(privateEngineInfo.name);
      Glean.searchEnginePrivate.loadPath.set(privateEngineInfo.loadPath);
      Glean.searchEnginePrivate.submissionUrl.set(
        privateEngineInfo.submissionURL ?? "blank:"
      );
    } else {
      Glean.searchEnginePrivate.providerId.set("");
      Glean.searchEnginePrivate.partnerCode.set("");
      Glean.searchEnginePrivate.overriddenByThirdParty.set(false);
      Glean.searchEnginePrivate.engineId.set("");
      Glean.searchEnginePrivate.displayName.set("");
      Glean.searchEnginePrivate.loadPath.set("");
      Glean.searchEnginePrivate.submissionUrl.set("blank:");
    }
  }

  /**
   * This function is called at the beginning of search service init.
   * If the error type set in a test environment matches errorType
   * passed to this function, we throw an error.
   *
   * @param {string} errorType
   *   The error that can occur during search service init.
   */
  #maybeThrowErrorInTest(errorType) {
    if (
      Services.env.exists("XPCSHELL_TEST_PROFILE_DIR") &&
      this.errorToThrowInTest.type === errorType
    ) {
      throw new Error(
        this.errorToThrowInTest.message ??
          `Fake ${errorType} error during search service initialization.`
      );
    }
  }

  #buildParseSubmissionMap() {
    this.#parseSubmissionMap = new Map();

    // Used only while building the map, indicates which entries do not refer to
    // the main domain of the engine but to an alternate domain, for example
    // "www.google.fr" for the "www.google.com" search engine.
    let keysOfAlternates = new Set();

    for (let engine of this.#sortedEngines) {
      if (engine.hidden) {
        continue;
      }

      let urlParsingInfo = engine.getURLParsingInfo();
      if (!urlParsingInfo) {
        continue;
      }

      // Store the same object on each matching map key, as an optimization.
      let mapValueForEngine = {
        engine,
        termsParameterName: urlParsingInfo.termsParameterName,
      };

      let processDomain = (domain, isAlternate) => {
        let key = domain + urlParsingInfo.path;

        // Apply the logic for which main domains take priority over alternate
        // domains, even if they are found later in the ordered engine list.
        let existingEntry = this.#parseSubmissionMap.get(key);
        if (!existingEntry) {
          if (isAlternate) {
            keysOfAlternates.add(key);
          }
        } else if (!isAlternate && keysOfAlternates.has(key)) {
          keysOfAlternates.delete(key);
        } else {
          return;
        }

        this.#parseSubmissionMap.set(key, mapValueForEngine);
      };

      processDomain(urlParsingInfo.mainDomain, false);
      lazy.SearchStaticData.getAlternateDomains(
        urlParsingInfo.mainDomain
      ).forEach(d => processDomain(d, true));
    }
  }

  #addObservers() {
    if (this.#observersAdded) {
      // There might be a race between synchronous and asynchronous
      // initialization for which we try to register the observers twice.
      return;
    }
    this.#observersAdded = true;

    Services.obs.addObserver(this, lazy.SearchUtils.TOPIC_ENGINE_MODIFIED);
    Services.obs.addObserver(this, QUIT_APPLICATION_TOPIC);
    Services.obs.addObserver(this, TOPIC_LOCALES_CHANGE);

    this._settings.addObservers();

    // The current stage of shutdown. Used to help analyze crash
    // signatures in case of shutdown timeout.
    let shutdownState = {
      step: "Not started",
      latestError: {
        message: undefined,
        stack: undefined,
      },
    };
    IOUtils.profileBeforeChange.addBlocker(
      "Search service: shutting down",
      () =>
        (async () => {
          // If we are in initialization, then don't attempt to save the settings.
          // It is likely that shutdown will have caused the add-on manager to
          // stop, which can cause initialization to fail.
          // Hence at that stage, we could have broken settings which we don't
          // want to write.
          // The good news is, that if we don't write the settings here, we'll
          // detect the out-of-date settings on next state, and automatically
          // rebuild it.
          if (!this.isInitialized) {
            lazy.logConsole.warn(
              "not saving settings on shutdown due to initializing."
            );
            return;
          }

          try {
            await this._settings.shutdown(shutdownState);
          } catch (ex) {
            // Ensure that error is reported and that it causes tests
            // to fail, otherwise ignore it.
            Promise.reject(ex);
          }
        })(),

      () => shutdownState
    );
  }

  #removeObservers() {
    if (this.ignoreListListener) {
      lazy.IgnoreLists.unsubscribe(this.ignoreListListener);
      delete this.ignoreListListener;
    }
    if (this.#queuedIdle) {
      lazy.idleService.removeIdleObserver(this, RECONFIG_IDLE_TIME_SEC);
      this.#queuedIdle = false;
    }

    this._settings.removeObservers();

    Services.obs.removeObserver(this, lazy.Region.REGION_TOPIC);
    Services.obs.removeObserver(this, lazy.SearchUtils.TOPIC_ENGINE_MODIFIED);
    Services.obs.removeObserver(this, QUIT_APPLICATION_TOPIC);
    Services.obs.removeObserver(this, TOPIC_LOCALES_CHANGE);
    this.#observersAdded = false;
    this.#earlyObserversAdded = false;
  }

  QueryInterface = ChromeUtils.generateQI(["nsIObserver", "nsITimerCallback"]);

  // nsIObserver
  observe(subject, topic, verb) {
    switch (topic) {
      case lazy.SearchUtils.TOPIC_ENGINE_MODIFIED: {
        switch (verb) {
          case lazy.SearchUtils.MODIFIED_TYPE.ADDED:
            this.#parseSubmissionMap = null;
            break;
          case lazy.SearchUtils.MODIFIED_TYPE.CHANGED: {
            let engine = /** @type {SearchEngine} */ (subject.wrappedJSObject);
            if (
              engine == this.defaultEngine ||
              engine == this.defaultPrivateEngine
            ) {
              this.#updateTelemetryDueToDefaultEngineChange(
                engine != this.defaultEngine,
                engine,
                engine,
                this.CHANGE_REASON.ENGINE_UPDATE
              );
            }
            this.#parseSubmissionMap = null;
            break;
          }
          case lazy.SearchUtils.MODIFIED_TYPE.REMOVED:
            // Invalidate the map used to parse URLs to search engines.
            this.#parseSubmissionMap = null;
            break;
        }
        break;
      }
      case "idle": {
        lazy.idleService.removeIdleObserver(this, RECONFIG_IDLE_TIME_SEC);
        this.#queuedIdle = false;
        lazy.logConsole.debug(
          "Reloading engines after idle due to configuration change"
        );
        this.#maybeReloadEngines(this.CHANGE_REASON.CONFIG).catch(
          console.error
        );
        break;
      }

      case QUIT_APPLICATION_TOPIC:
        this.#removeObservers();
        break;

      case TOPIC_LOCALES_CHANGE:
        // Locale changed. Re-init. We rely on observers, because we can't
        // return this promise to anyone.

        // At the time of writing, when the user does a "Apply and Restart" for
        // a new language the preferences code triggers the locales change and
        // restart straight after, so we delay the check, which means we should
        // be able to avoid the reload on shutdown, and we'll sort it out
        // on next startup.
        // This also helps to avoid issues with the add-on manager shutting
        // down at the same time (see _reInit for more info).
        Services.tm.dispatchToMainThread(() => {
          if (!Services.startup.shuttingDown) {
            this.#maybeReloadEngines(this.CHANGE_REASON.LOCALE).catch(
              console.error
            );
          }
        });
        break;
      case lazy.Region.REGION_TOPIC:
        lazy.logConsole.debug("Region updated:", lazy.Region.home);
        this.#maybeReloadEngines(this.CHANGE_REASON.REGION).catch(
          console.error
        );
        break;
    }
  }

  /**
   * @param {object} metaData
   *    The metadata object that defines the details of the engine.
   * @returns {boolean}
   *    Returns true if metaData has different property values than
   *    the cached _metaData.
   */
  #didSettingsMetaDataUpdate(metaData) {
    let metaDataProperties = [
      "locale",
      "region",
      "channel",
      "experiment",
      "distroID",
    ];

    return metaDataProperties.some(p => {
      return metaData?.[p] !== this._settings.getMetaDataAttribute(p);
    });
  }

  /**
   * Shows an infobar to notify the user their default search engine has been
   * removed and replaced by a new default search engine.
   *
   * This indirection exists to simplify tests.
   *
   * @param {string} prevCurrentEngineName
   *   The name of the previous default engine that will be replaced.
   * @param {string} newCurrentEngineName
   *   The name of the engine that will be the new default engine.
   */
  _showRemovalOfSearchEngineNotificationBox(
    prevCurrentEngineName,
    newCurrentEngineName
  ) {
    lazy.BrowserUtils.callModulesFromCategory(
      { categoryName: "search-service-notification" },
      "search-engine-removal",
      prevCurrentEngineName,
      newCurrentEngineName
    );
  }

  /**
   * Infobar informing the user that the search settings had to be reset
   * and what their new default engine is.
   *
   * @param {string} newEngine
   *   The name of the new default search engine.
   */
  _showSearchSettingsResetNotificationBox(newEngine) {
    lazy.BrowserUtils.callModulesFromCategory(
      { categoryName: "search-service-notification" },
      "search-settings-reset",
      newEngine
    );
  }

  /**
   * Maybe starts the timer for OpenSearch engine updates. This will be set
   * only if updates are enabled and there are OpenSearch engines installed
   * which have updates.
   */
  #maybeStartOpenSearchUpdateTimer() {
    if (
      this.#openSearchUpdateTimerStarted ||
      !Services.prefs.getBoolPref(
        lazy.SearchUtils.BROWSER_SEARCH_PREF + "update",
        true
      )
    ) {
      return;
    }

    let engineWithUpdates = [...this._engines.values()].some(
      engine => engine instanceof lazy.OpenSearchEngine && engine.hasUpdates
    );

    if (engineWithUpdates) {
      lazy.logConsole.debug("Engine with updates found, setting update timer");
      lazy.timerManager.registerTimer(
        OPENSEARCH_UPDATE_TIMER_TOPIC,
        this,
        OPENSEARCH_UPDATE_TIMER_INTERVAL,
        true
      );
      this.#openSearchUpdateTimerStarted = true;
    }
  }
})(); // end SearchService class

/**
 * Handles getting and checking extensions against the allow list.
 */
class SearchDefaultOverrideAllowlistHandler {
  constructor() {
    this.#remoteConfig = lazy.RemoteSettings(
      lazy.SearchUtils.SETTINGS_ALLOWLIST_KEY
    );
  }

  /**
   * Determines if a search engine extension can override a default one
   * according to the allow list.
   *
   * @param {object} extension
   *   The extension object (from add-on manager) that will override the
   *   app provided search engine.
   * @param {string} appProvidedEngineId
   *   The id of the search engine that will be overriden.
   * @returns {Promise<boolean>}
   *   Returns true if the search engine extension may override the app provided
   *   instance.
   */
  async canOverride(extension, appProvidedEngineId) {
    const overrideTable = await this.#getAllowlist();

    let entry = overrideTable.find(e => e.thirdPartyId == extension.id);
    if (!entry) {
      return false;
    }

    if (appProvidedEngineId != entry.overridesAppIdv2) {
      return false;
    }

    let searchProvider =
      extension.manifest.chrome_settings_overrides.search_provider;

    return entry.urls.some(
      e =>
        searchProvider.search_url == e.search_url &&
        searchProvider.search_url_get_params == e.search_url_get_params &&
        searchProvider.search_url_post_params == e.search_url_post_params
    );
  }

  /**
   * Determines if an existing search engine is allowed to override a default one
   * according to the allow list.
   *
   * @param {SearchEngine} engine
   *   The existing search engine.
   * @param {string} appProvidedEngineId
   *   The id of the search engine that will be overriden.
   * @returns {Promise<boolean>}
   *   Returns true if the existing search engine is allowed to override the
   *   app provided instance.
   */
  async canEngineOverride(engine, appProvidedEngineId) {
    const overrideEntries = await this.#getAllowlist();

    let entry;

    if (engine instanceof lazy.AddonSearchEngine) {
      entry = overrideEntries.find(e => e.thirdPartyId == engine.extensionID);
    } else if (engine instanceof lazy.OpenSearchEngine) {
      entry = overrideEntries.find(
        e =>
          e.thirdPartyId == "opensearch@search.mozilla.org" &&
          e.engineName == engine.name
      );
    }
    if (!entry) {
      return false;
    }

    if (appProvidedEngineId != entry.overridesAppIdv2) {
      return false;
    }

    return entry.urls.some(urlSet =>
      engine.checkSearchUrlMatchesManifest(urlSet)
    );
  }

  /**
   * Obtains the configuration from remote settings. This includes
   * verifying the signature of the record within the database.
   *
   * If the signature in the database is invalid, the database will be wiped
   * and the stored dump will be used, until the settings next update.
   *
   * Note that this may cause a network check of the certificate, but that
   * should generally be quick.
   *
   * @returns {Promise<object[]>}
   *   An array of objects in the database, or an empty array if none
   *   could be obtained.
   */
  async #getAllowlist() {
    let result = [];
    try {
      result = await this.#remoteConfig.get();
    } catch (ex) {
      // Don't throw an error just log it, just continue with no data, and hopefully
      // a sync will fix things later on.
      console.error(ex);
    }
    lazy.logConsole.debug("Allow list is:", result);
    return result;
  }

  #remoteConfig;
}
