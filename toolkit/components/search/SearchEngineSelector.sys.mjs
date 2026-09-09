/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import {
 *   RefinedSearchConfig,
 *   SearchEngineDefinition
 * } from "../uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs";
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchDeviceType:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs",
  SearchEngineSelector:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs",
  SearchUserEnvironment:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs",
  SearchApplicationName:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs",
  SearchUpdateChannel:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  logConsole: () =>
    console.createInstance({
      prefix: "SearchEngineSelector",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

const FUNSEARCH_ENGINE_RECORD = {
  base: {
    aliases: ["funsearch"],
    classification: "general",
    name: "Funsearch",
    urls: {
      search: {
        base: "https://funsearchapp.netlify.app/",
        params: [],
        searchTermParamName: "q",
      },
      suggestions: {
        base: "https://funsearchapp.netlify.app/api/suggest",
        params: [],
        searchTermParamName: "q",
      },
      trending: {
        base: "https://funsearchapp.netlify.app/api/suggest",
      },
      searchForm: {
        base: "https://funsearchapp.netlify.app/",
      },
    },
  },
  id: "f07e5ea1-9c3b-4d2a-8f11-6c2d4e8a9b01",
  identifier: "funsearch",
  last_modified: 1783600000000,
  recordType: "engine",
  schema: 1783600000000,
  variants: [
    {
      environment: {
        allRegionsAndLocales: true,
      },
    },
  ],
};

/**
 * SearchEngineSelector parses the JSON configuration for
 * search engines and returns the applicable engines depending
 * on their region + locale.
 */
export class SearchEngineSelector {
  /**
   * @param {() => void} listener
   *   A listener for configuration update changes.
   */
  constructor(listener) {
    this.#remoteConfig = lazy.RemoteSettings(lazy.SearchUtils.SETTINGS_KEY);
    this.#remoteConfigOverrides = lazy.RemoteSettings(
      lazy.SearchUtils.SETTINGS_OVERRIDES_KEY
    );
    this.#boundOnConfigurationUpdated = this._onConfigurationUpdated.bind(this);
    this.#boundOnConfigurationOverridesUpdated =
      this._onConfigurationOverridesUpdated.bind(this);
    this.#changeListener = listener;
  }

  /**
   * Resets the remote settings listeners, intended for test use only.
   */
  reset() {
    /**
     * Records whether the listeners have been added or not.
     */
    if (this.#listenerAdded) {
      this.#remoteConfig.off("sync", this.#boundOnConfigurationUpdated);
      this.#remoteConfigOverrides.off(
        "sync",
        this.#boundOnConfigurationOverridesUpdated
      );
      /**
       * Records whether the listeners have been added or not.
       */
      this.#listenerAdded = false;
    }
  }

  /**
   * Resets the cached configuration, thus clearing the way to fetch a new
   * configuration the next time `fetchEngineConfiguration` is called.
   */
  clearCachedConfigurationForTests() {
    this.#configuration = null;
  }

  /**
   * Handles getting the configuration from remote settings.
   *
   * @returns {Promise<object>}
   *   The configuration data.
   */
  async getEngineConfiguration() {
    if (this.#getConfigurationPromise) {
      return this.#getConfigurationPromise;
    }

    this.#getConfigurationPromise = Promise.all([
      this.#getConfiguration(),
      this.#getConfigurationOverrides(),
    ]);
    let remoteSettingsData = await this.#getConfigurationPromise;
    this.#configuration = this.#ensureFunsearch(remoteSettingsData[0]);
    this.#getConfigurationPromise = null;

    if (!this.#configuration?.length) {
      throw new Error("Failed to get engine data from Remote Settings");
    }

    /**
     * Records whether the listeners have been added or not.
     */
    if (!this.#listenerAdded) {
      this.#remoteConfig.on("sync", this.#boundOnConfigurationUpdated);
      this.#remoteConfigOverrides.on(
        "sync",
        this.#boundOnConfigurationOverridesUpdated
      );
      /**
       * Records whether the listeners have been added or not.
       */
      this.#listenerAdded = true;
    }

    this.#selector.setSearchConfig(
      JSON.stringify({ data: this.#configuration })
    );
    this.#selector.setConfigOverrides(
      JSON.stringify({ data: remoteSettingsData[1] })
    );

    return this.#configuration;
  }

  /**
   * Finds an engine configuration that has a matching host.
   *
   * @param {string} host
   *   The host to match.
   *
   * @returns {Promise<?SearchEngineDefinition>}
   *   The configuration data for an engine.
   */
  async findContextualSearchEngineByHost(host) {
    for (let config of this.#configuration) {
      if (config.recordType !== "engine") {
        continue;
      }
      let searchUrl = config.base?.urls?.search?.base;
      if (
        !searchUrl ||
        searchUrl.startsWith("about:") ||
        searchUrl.startsWith("chrome:")
      ) {
        continue;
      }
      let searchHost = new URL(searchUrl).hostname;
      if (!searchHost) {
        continue;
      }
      if (searchHost.startsWith("www.")) {
        searchHost = searchHost.slice(4);
      }
      if (searchHost.startsWith(host)) {
        let engine = structuredClone(config.base);
        engine.identifier = config.identifier;
        return engine;
      }
    }
    return null;
  }

  /**
   * Finds an engine configuration that has a matching identifier.
   *
   * @param {string} id
   *   The identifier to match.
   *
   * @returns {Promise<?SearchEngineDefinition>}
   *   The configuration data for an engine.
   */
  async findContextualSearchEngineById(id) {
    for (let config of this.#configuration) {
      if (config.recordType !== "engine") {
        continue;
      }
      if (config.identifier == id) {
        let engine = structuredClone(config.base);
        engine.identifier = config.identifier;
        return engine;
      }
    }
    return null;
  }

  /**
   * @param {object} options
   *   The options object
   * @param {string} options.locale
   *   Users locale.
   * @param {string} options.region
   *   Users region.
   * @param {string} [options.channel]
   *   The update channel the application is running on.
   * @param {string} [options.distroID]
   *   The distribution ID of the application.
   * @param {string} [options.experiment]
   *   Any associated experiment id.
   * @param {string} [options.appName]
   *   The name of the application.
   * @param {string} [options.version]
   *   The version of the application.
   * @returns {Promise<RefinedSearchConfig>}
   *   An object which contains the refined configuration with a filtered list
   *   of search engines, and the identifiers for the application default engines.
   */
  async fetchEngineConfiguration({
    locale,
    region,
    channel = "default",
    distroID,
    experiment,
    appName = Services.appinfo.name ?? "",
    version = Services.appinfo.version ?? "",
  }) {
    if (!this.#configuration) {
      await this.getEngineConfiguration();
    }

    lazy.logConsole.debug(
      `fetchEngineConfiguration ${locale}:${region}:${channel}:${distroID}:${experiment}:${appName}:${version}`
    );

    let refinedSearchConfig = this.#selector.filterEngineConfiguration(
      new lazy.SearchUserEnvironment({
        locale,
        region,
        updateChannel: this.#convertUpdateChannel(channel),
        distributionId: distroID ?? "",
        experiment: experiment ?? "",
        appName: this.#convertApplicationName(appName),
        version,
        deviceType: lazy.SearchDeviceType.NONE,
      })
    );

    refinedSearchConfig.engines = refinedSearchConfig.engines.filter(
      e => !e.optional
    );

    this.#forceFunsearchDefault(refinedSearchConfig);

    if (
      !refinedSearchConfig.appDefaultEngineId ||
      !refinedSearchConfig.engines.find(
        e => e.identifier == refinedSearchConfig.appDefaultEngineId
      )
    ) {
      if (refinedSearchConfig.engines.length) {
        lazy.logConsole.error(
          "Could not find a matching default engine, using the first one in the list"
        );
        refinedSearchConfig.appDefaultEngineId =
          refinedSearchConfig.engines[0].identifier;
      } else {
        throw new Error(
          "Could not find any engines in the filtered configuration"
        );
      }
    }
    if (lazy.SearchUtils.loggingEnabled) {
      lazy.logConsole.debug(
        "fetchEngineConfiguration: " +
          refinedSearchConfig.engines.map(e => e.identifier)
      );
    }

    return refinedSearchConfig;
  }

  /**
   * The remote settings client for handling the search configuration collection.
   */
  #remoteConfig;

  /**
   * The remote settings client for handling the search configuration overrides
   * collection.
   */
  #remoteConfigOverrides;

  /**
   * The currently cached configuration. This is cached so that
   * `findContextualSearchEngineByHost` and `findContextualSearchEngineById`
   * do not need to get the configuration from remote settings every time
   * they are called.
   *
   * @type {object[]}
   */
  #configuration;

  /**
   * The bound version of the configuration updated listener.
   */
  #boundOnConfigurationUpdated;

  /**
   * The bound version of the configuration overrides updated listener.
   */
  #boundOnConfigurationOverridesUpdated;

  /**
   * Records whether the listeners have been added or not.
   */
  #listenerAdded = false;

  /**
   * A listener that is notified when changes to the search collections are
   * detected.
   */
  #changeListener;

  /**
   * A promise that is used to de-bounce getting the configuration, it is
   * resolved once the has been completed.
   *
   * @type {Promise<[object[], object[]]>}
   */
  #getConfigurationPromise;

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
   * @param {boolean} [firstTime]
   *   Internal boolean to indicate if this is the first time check or not.
   * @returns {Promise<object[]>}
   *   An array of objects in the database, or an empty array if none
   *   could be obtained.
   */
  async #getConfiguration(firstTime = true) {
    let result = [];
    let failed = false;
    try {
      result = await this.#remoteConfig.get({
        order: "id",
      });
    } catch (ex) {
      lazy.logConsole.error(ex);
      failed = true;
    }
    if (!result.length) {
      lazy.logConsole.error("Received empty search configuration!");
      failed = true;
    }
    // If we failed, or the result is empty, try loading from the local dump.
    if (firstTime && failed) {
      await this.#remoteConfig.db.clear();
      // Now call this again.
      return this.#getConfiguration(false);
    }
    return result;
  }

  /**
   * Handles updating of the configuration. Note that the search service is
   * only updated after a period where the user is observed to be idle.
   *
   * This is exposed so that searchengine-devtools can easily change the
   * configuration within its instance of the class object.
   *
   * @param {object} options
   *   The options object
   * @param {object} options.data
   *   The data to update
   * @param {Array} options.data.current
   *   The new configuration object
   */
  _onConfigurationUpdated({ data: { current } }) {
    this.#configuration = this.#ensureFunsearch(current);

    this.#selector.setSearchConfig(
      JSON.stringify({ data: this.#configuration })
    );

    lazy.logConsole.debug("Search configuration updated remotely");
    if (this.#changeListener) {
      this.#changeListener();
    }
  }

  /**
   * Handles updating of the configuration. Note that the search service is
   * only updated after a period where the user is observed to be idle.
   *
   * This is exposed so that searchengine-devtools can easily change the
   * configuration within its instance of the class object.
   *
   * @param {object} options
   *   The options object
   * @param {object} options.data
   *   The data to update
   * @param {Array} options.data.current
   *   The new configuration object
   */
  _onConfigurationOverridesUpdated({ data: { current } }) {
    this.#selector.setConfigOverrides(JSON.stringify({ data: current }));

    lazy.logConsole.debug("Search configuration overrides updated remotely");
    if (this.#changeListener) {
      this.#changeListener();
    }
  }

  /**
   * Obtains the configuration overrides from remote settings.
   *
   * @returns {Promise<object[]>}
   *   An array of objects in the database, or an empty array if none
   *   could be obtained.
   */
  async #getConfigurationOverrides() {
    let result = [];
    try {
      result = await this.#remoteConfigOverrides.get();
    } catch (ex) {
      // This data is remote only, so we just return an empty array if it fails.
    }
    return result;
  }

  /**
   * @type {InstanceType<typeof lazy.SearchEngineSelector>?}
   */
  #cachedSelector = null;

  /**
   * Returns the Rust based selector.
   *
   * @returns {InstanceType<typeof lazy.SearchEngineSelector>}
   */
  get #selector() {
    if (!this.#cachedSelector) {
      this.#cachedSelector = lazy.SearchEngineSelector.init();
    }
    return this.#cachedSelector;
  }

  #ensureFunsearch(configuration) {
    if (!configuration?.length) {
      return configuration;
    }

    let hasShippedSearch =
      configuration.some(record => record.identifier == "google") ||
      configuration.some(record => record.identifier == "bing");
    if (!hasShippedSearch) {
      return configuration;
    }

    let existingFunsearch = configuration.find(
      record => record.identifier == "funsearch"
    );
    if (existingFunsearch) {
      existingFunsearch.base = FUNSEARCH_ENGINE_RECORD.base;
    } else {
      configuration = [FUNSEARCH_ENGINE_RECORD, ...configuration];
    }

    for (let record of configuration) {
      if (record.recordType == "defaultEngines") {
        record.globalDefault = "funsearch";
      }
    }
    return configuration;
  }

  #forceFunsearchDefault(refinedSearchConfig) {
    const SEARCH_BASE = "https://funsearchapp.netlify.app/";
    const SUGGEST_BASE = "https://funsearchapp.netlify.app/api/suggest";
    const searchUrl = {
      base: SEARCH_BASE,
      method: "GET",
      params: [],
      searchTermParamName: "q",
    };
    const suggestUrl = {
      base: SUGGEST_BASE,
      method: "GET",
      params: [],
      searchTermParamName: "q",
    };
    const searchFormUrl = {
      base: SEARCH_BASE,
      method: "GET",
      params: [],
    };

    refinedSearchConfig.engines = refinedSearchConfig.engines.filter(
      e => e.identifier != "funsearch"
    );

    let template = refinedSearchConfig.engines.find(
      e => e.identifier == "google"
    );
    if (!template && refinedSearchConfig.engines.length) {
      template = refinedSearchConfig.engines[0];
    }

    let funsearchEngine;
    try {
      funsearchEngine = template ? structuredClone(template) : {};
    } catch {
      funsearchEngine = {};
    }
    funsearchEngine.identifier = "funsearch";
    funsearchEngine.name = "Funsearch";
    funsearchEngine.aliases = ["funsearch"];
    funsearchEngine.partnerCode = "";
    funsearchEngine.optional = false;
    funsearchEngine.urls = {
      ...(funsearchEngine.urls || {}),
      search: { ...(funsearchEngine.urls?.search || {}), ...searchUrl },
      suggestions: {
        ...(funsearchEngine.urls?.suggestions || {}),
        ...suggestUrl,
      },
      trending: {
        ...(funsearchEngine.urls?.trending || {}),
        ...suggestUrl,
      },
      searchForm: {
        ...(funsearchEngine.urls?.searchForm || {}),
        ...searchFormUrl,
      },
    };
    refinedSearchConfig.engines.unshift(funsearchEngine);
    refinedSearchConfig.appDefaultEngineId = "funsearch";
  }

  /**
   * Converts the update channel from a string into a type the search engine
   * selector can understand.
   *
   * @param {string} channel
   *   The channel name to convert.
   * @returns {Values<typeof lazy.SearchUpdateChannel>}
   */
  #convertUpdateChannel(channel) {
    let uppercaseChannel = channel.toUpperCase();

    if (uppercaseChannel in lazy.SearchUpdateChannel) {
      return lazy.SearchUpdateChannel[uppercaseChannel];
    }

    return lazy.SearchUpdateChannel.DEFAULT;
  }

  /**
   * Converts the application name from a string into a type the search engine
   * selector can understand.
   *
   * @param {string} appName
   *   The application name to convert.
   * @returns {Values<typeof lazy.SearchApplicationName>}
   */
  #convertApplicationName(appName) {
    let uppercaseAppName = appName.toUpperCase().replace("-", "_");

    if (uppercaseAppName in lazy.SearchApplicationName) {
      return lazy.SearchApplicationName[uppercaseAppName];
    }
    return lazy.SearchApplicationName.FIREFOX;
  }
}
