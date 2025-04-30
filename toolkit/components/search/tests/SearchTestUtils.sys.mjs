import { MockRegistrar } from "resource://testing-common/MockRegistrar.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonTestUtils: "resource://testing-common/AddonTestUtils.sys.mjs",
  AppProvidedSearchEngine:
    "resource://gre/modules/AppProvidedSearchEngine.sys.mjs",
  ExtensionTestUtils:
    "resource://testing-common/ExtensionXPCShellUtils.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchUtils: "resource://gre/modules/SearchUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

/**
 * A class containing useful testing functions for Search based tests.
 */
class _SearchTestUtils {
  /**
   * The test scope that the test is running in.
   *
   * @type {object}
   */
  #testScope = null;

  /**
   * True if we are in a mochitest scope, false for xpcshell-tests.
   *
   * @type {boolean?}
   */
  #isMochitest = null;

  /**
   * Whether the fake idle service is registered and needs to be cleaned up.
   *
   * @type {boolean}
   */
  #idleServiceCID = null;

  /**
   * All stubs of remote settings that will be cleaned up by their key.
   *
   * @type {object}
   */
  #stubs = new Map();

  /**
   * Initialises the test utils, setting up the scope and working out if these
   * are mochitest or xpcshell-test.
   *
   * @param {object} testScope
   *   The global scope for the test.
   */
  init(testScope) {
    this.#testScope = testScope;
    this.#isMochitest = !Services.env.exists("XPCSHELL_TEST_PROFILE_DIR");
    if (this.#isMochitest) {
      lazy.AddonTestUtils.initMochitest(testScope);

      testScope.registerCleanupFunction(async () => {
        this.#stubs.forEach(stub => stub.restore());

        if (this.#stubs.size) {
          this.#stubs = new Map();

          let settingsWritten = SearchTestUtils.promiseSearchNotification(
            "write-settings-to-disk-complete"
          );
          await this.updateRemoteSettingsConfig();
          await settingsWritten;
        }

        if (this.#idleServiceCID) {
          MockRegistrar.unregister(this.#idleServiceCID);
          this.#idleServiceCID = null;
        }
      });
    }
  }

  /**
   * Adds an OpenSearch based engine to the search service. It will remove
   * the engine at the end of the test.
   *
   * @param {object} options
   *   The options for the new search engine.
   * @param {string} options.url
   *   The URL of the engine to add.
   * @param {string} [options.faviconURL]
   *   The icon to be used for the open search engine, if not specified in the
   *   OpenSearch engine data.
   * @param {boolean} [options.setAsDefault]
   *   Whether or not to set the engine as default automatically. If this is
   *   true, the engine will be set as default, and the previous default engine
   *   will be restored when the test exits.
   * @param {boolean} [options.setAsDefaultPrivate]
   *   Whether or not to set the engine as default automatically for private mode.
   *   If this is true, the engine will be set as default, and the previous default
   *   engine will be restored when the test exits.
   * @param {boolean} [options.skipReset]
   *   Skips resetting the default engine at the end of the test.
   * @returns {Promise} Returns a promise that is resolved with the new engine
   *                    or rejected if it fails.
   */
  async installOpenSearchEngine({
    url,
    faviconURL,
    setAsDefault = false,
    setAsDefaultPrivate = false,
    skipReset = false,
  }) {
    // OpenSearch engines can only be added via http protocols.
    url = url.replace("chrome://mochitests/content", "https://example.com");
    let engine = await Services.search.addOpenSearchEngine(url, faviconURL);
    let previousEngine = Services.search.defaultEngine;
    let previousPrivateEngine = Services.search.defaultPrivateEngine;
    if (setAsDefault) {
      await Services.search.setDefault(
        engine,
        Ci.nsISearchService.CHANGE_REASON_UNKNOWN
      );
    }
    if (setAsDefaultPrivate) {
      await Services.search.setDefaultPrivate(
        engine,
        Ci.nsISearchService.CHANGE_REASON_UNKNOWN
      );
    }
    this.#testScope.registerCleanupFunction(async () => {
      if (setAsDefault && !skipReset) {
        await Services.search.setDefault(
          previousEngine,
          Ci.nsISearchService.CHANGE_REASON_UNKNOWN
        );
      }
      if (setAsDefaultPrivate && !skipReset) {
        await Services.search.setDefaultPrivate(
          previousPrivateEngine,
          Ci.nsISearchService.CHANGE_REASON_UNKNOWN
        );
      }
      try {
        await Services.search.removeEngine(engine);
      } catch (ex) {
        // Don't throw if the test has already removed it.
      }
    });
    return engine;
  }

  /**
   * Returns a promise that is resolved when an observer notification from the
   * search service fires with the specified data.
   *
   * @param {*} expectedData
   *        The value the observer notification sends that causes us to resolve
   *        the promise.
   * @param {string} topic
   *        The notification topic to observe. Defaults to 'browser-search-service'.
   * @param {number} times
   *        The number of notifications required to resolve the promise. Defaults to 1.
   * @returns {Promise}
   *        Returns a promise that is resolved with the subject of the
   *        topic once the topic with the data has been observed.
   */
  promiseSearchNotification(
    expectedData,
    topic = "browser-search-service",
    times = 1
  ) {
    return new Promise(resolve => {
      let i = 0;
      Services.obs.addObserver(function observer(aSubject, aTopic, aData) {
        if (aData == expectedData) {
          i += 1;
          if (i == times) {
            Services.obs.removeObserver(observer, topic);
            // Let the stack unwind.
            Services.tm.dispatchToMainThread(() => resolve(aSubject));
          }
        }
      }, topic);
    });
  }

  /**
   * Stubs the get property of the remote settings client with a
   * given Key. Configuration does not get expanded.
   *
   * @param {string} [key]
   *   The remote settings key of the configuration to be stubbed.
   * @param {object[]} [config]
   *   The configuration that will be returned by the stub.
   */
  #stubConfig(key, config) {
    if (!config) {
      if (this.#stubs.has(key)) {
        this.#stubs.get(key).restore();
        this.#stubs.delete(key);
      }
      return;
    }

    if (!this.#stubs.has(key)) {
      let settings = lazy.RemoteSettings(key);
      this.#stubs.set(key, lazy.sinon.stub(settings, "get").returns(config));
    } else {
      this.#stubs.get(key).returns(config);
    }
  }

  /**
   * Expands a partial search config by the minumum number of properties
   * to be a valid config and loads it to make test cases less verbose.
   * Does not modify the input.
   *
   * defaultEngines and engineOrders are not required. The first
   * engine will be set to default if defaultEngines is not specified.
   *
   * Engine objects only require an identifier and there needs to be at least
   * one engine. The name defaults to the identifier, the classification
   * to general, the search url to https://www.example.com/search?q=query
   * and the environment to allRegionsAndLocales.
   *
   * The recordType is detected automatically and thus optional.
   *
   * @param {object[]} [partialConfig]
   *  The partial search config that will be expanded.
   * @returns {object[]}
   *  The expanded search config.
   */
  expandPartialConfig(partialConfig) {
    if (!partialConfig) {
      return partialConfig;
    }
    let fullConfig = structuredClone(partialConfig);
    let numEngines = 0;
    let defaultEngines;
    let engineOrders;
    let availableLocales;

    for (let obj of fullConfig) {
      obj.recordType = this.#detectRecordType(obj);

      switch (obj.recordType) {
        case "engine":
          if (!obj.base) {
            obj.base = {};
          }
          if (!obj.base.name) {
            obj.base.name = obj.identifier;
          }
          if (!obj.base.classification) {
            obj.base.classification = "general";
          }
          if (!obj.base.urls) {
            obj.base.urls = {};
          }
          if (!obj.base.urls.search) {
            obj.base.urls.search = {
              base: "https://www.example.com/search",
              searchTermParamName: "q",
            };
          }

          if (!obj.variants) {
            obj.variants = [{ environment: { allRegionsAndLocales: true } }];
          }
          numEngines++;
          break;
        case "defaultEngines":
          defaultEngines = obj;
          break;
        case "engineOrders":
          engineOrders = obj;
          break;
        case "availableLocales":
          availableLocales = obj;
      }
    }

    if (!numEngines) {
      throw new Error("One engine is required.");
    }

    if (!engineOrders) {
      engineOrders = {
        recordType: "engineOrders",
        orders: [],
      };
      fullConfig.push(engineOrders);
    }

    if (!defaultEngines) {
      defaultEngines = { recordType: "defaultEngines" };
      fullConfig.push(defaultEngines);
    }

    if (!defaultEngines.globalDefault) {
      let firstEngine = fullConfig.find(r => r.recordType == "engine");
      defaultEngines.globalDefault = firstEngine.identifier;
    }
    if (!defaultEngines.specificDefaults) {
      defaultEngines.specificDefaults = [];
    }

    if (!availableLocales) {
      availableLocales = {
        recordType: "availableLocales",
        locales: Array.from(this.extractAvailableLocales(fullConfig)),
      };
      fullConfig.push(availableLocales);
    }

    return fullConfig;
  }

  /**
   * Extracts the list of available locales from a search configuration.
   *
   * @param {object[]} config
   */
  extractAvailableLocales(config) {
    let result = new Set();

    function addLocalesFromEnvironment(environment) {
      environment.locales?.forEach(locale => result.add(locale));
      environment.excludedLocales?.forEach(locale => result.add(locale));
    }

    for (let entry of config) {
      switch (entry.recordType) {
        case "engine": {
          for (let variant of entry.variants) {
            addLocalesFromEnvironment(variant.environment);
            for (let subVariant of variant.subVariants ?? []) {
              addLocalesFromEnvironment(subVariant.environment);
            }
          }
          break;
        }
        case "defaultEngines": {
          for (let specificDefault of entry.specificDefaults) {
            addLocalesFromEnvironment(specificDefault.environment);
          }
          break;
        }
        case "engineOrders": {
          for (let order of entry.orders) {
            addLocalesFromEnvironment(order.environment);
          }
          break;
        }
      }
    }
    return result;
  }

  /**
   * Detects the recordType of a partial search config object based
   * on its properties.
   *
   * @param {object} [partialObject]
   *  The partial search config object whose recordType will be detected.
   * @returns {string}
   *   The detected recordType.
   */
  #detectRecordType(partialObject) {
    const identifyingProperties = {
      engine: ["identifier"],
      defaultEngines: ["specificDefaults", "globalDefault"],
      engineOrders: ["orders"],
    };

    let detectedType = partialObject.recordType;
    for (let recordType in identifyingProperties) {
      if (identifyingProperties[recordType].some(p => p in partialObject)) {
        if (detectedType && detectedType != recordType) {
          throw new Error("Ambiguous recordType");
        }
        detectedType = recordType;
      }
    }

    if (!detectedType) {
      throw new Error(
        "Could not detect recordType. Identifier is mandatory for engine recordTypes."
      );
    }

    return detectedType;
  }

  /**
   * Convert a list of engine configurations into engine objects.
   *
   * @param {Array} engineConfigurations
   *   An array of engine configurations.
   * @returns {AppProvidedSearchEngine[]}
   *   An array of app provided search engine objects.
   */
  async searchConfigToEngines(engineConfigurations) {
    return engineConfigurations.map(
      config => new lazy.AppProvidedSearchEngine({ config })
    );
  }

  /**
   * Sets up the add-on manager so that it is ready for loading WebExtension
   * in xpcshell-tests.
   */
  async initXPCShellAddonManager() {
    this.#testScope.ExtensionTestUtils = lazy.ExtensionTestUtils;

    if (
      lazy.ExtensionTestUtils.addonManagerStarted ||
      lazy.AddonTestUtils.addonIntegrationService
    ) {
      // We have already started the add-on manager, and the following functions
      // may throw if they are called twice.
      return;
    }

    lazy.ExtensionTestUtils.init(this.#testScope);
    lazy.AddonTestUtils.overrideCertDB();
    lazy.AddonTestUtils.init(this.#testScope, false);

    await lazy.ExtensionTestUtils.startAddonManager();
  }

  /**
   * Add a search engine as a WebExtension.
   *
   * Note: for tests, the extension must generally be unloaded before
   * `registerCleanupFunction`s are triggered. See bug 1694409.
   *
   * This function automatically registers an unload for the extension, this
   * may be skipped with the skipUnload argument.
   *
   * @param {object} [manifest]
   *   See {@link createEngineManifest}
   * @param {object} [options]
   *   Options for how the engine is installed and uninstalled.
   * @param {boolean} [options.setAsDefault]
   *   Whether or not to set the engine as default automatically. If this is
   *   true, the engine will be set as default, and the previous default engine
   *   will be restored when the test exits.
   * @param {boolean} [options.setAsDefaultPrivate]
   *   Whether or not to set the engine as default automatically for private mode.
   *   If this is true, the engine will be set as default, and the previous default
   *   engine will be restored when the test exits.
   * @param {boolean} [options.skipUnload]
   *   If true, this will skip the automatic unloading of the extension.
   * @param {object} [files]
   *   A key value object where the keys are the filenames and their contents are
   *   the values. Used for simulating locales and other files in the WebExtension.
   * @returns {object}
   *   The loaded extension.
   */
  async installSearchExtension(
    manifest = {},
    {
      setAsDefault = false,
      setAsDefaultPrivate = false,
      skipUnload = false,
    } = {},
    files = {}
  ) {
    if (!this.#isMochitest) {
      await this.initXPCShellAddonManager();
    }

    await Services.search.init();

    let extensionInfo = {
      useAddonManager: "permanent",
      files,
      manifest: this.createEngineManifest(manifest),
    };

    let extension;

    let previousEngine = Services.search.defaultEngine;
    let previousPrivateEngine = Services.search.defaultPrivateEngine;

    async function cleanup() {
      if (setAsDefault) {
        await Services.search.setDefault(
          previousEngine,
          Ci.nsISearchService.CHANGE_REASON_UNKNOWN
        );
      }
      if (setAsDefaultPrivate) {
        await Services.search.setDefaultPrivate(
          previousPrivateEngine,
          Ci.nsISearchService.CHANGE_REASON_UNKNOWN
        );
      }
      await extension.unload();
    }

    // Cleanup must be registered before loading the extension to avoid
    // failures for mochitests.
    if (!skipUnload && this.#isMochitest) {
      this.#testScope.registerCleanupFunction(cleanup);
    }

    extension = this.#testScope.ExtensionTestUtils.loadExtension(extensionInfo);
    await extension.startup();
    await lazy.AddonTestUtils.waitForSearchProviderStartup(extension);
    let engine = Services.search.getEngineByName(manifest.name);

    if (setAsDefault) {
      await Services.search.setDefault(
        engine,
        Ci.nsISearchService.CHANGE_REASON_UNKNOWN
      );
    }
    if (setAsDefaultPrivate) {
      await Services.search.setDefaultPrivate(
        engine,
        Ci.nsISearchService.CHANGE_REASON_UNKNOWN
      );
    }

    // For xpcshell-tests we must register the unload after adding the extension.
    // See bug 1694409 for why this is.
    if (!skipUnload && !this.#isMochitest) {
      this.#testScope.registerCleanupFunction(cleanup);
    }

    return extension;
  }

  /**
   * Create a search engine extension manifest.
   *
   * @param {object} [options]
   *   The options for the manifest.
   * @param {object} [options.icons]
   *   The icons to use for the WebExtension.
   * @param {string} [options.id]
   *   The id to use for the WebExtension.
   * @param {string} [options.name]
   *   The display name to use for the WebExtension.
   * @param {string} [options.version]
   *   The version to use for the WebExtension.
   * @param {boolean} [options.is_default]
   *   Whether or not to ask for the search engine in the WebExtension to be
   *   attempted to set as default.
   * @param {string} [options.favicon_url]
   *   The favicon to use for the search engine in the WebExtension.
   * @param {string} [options.keyword]
   *   The keyword to use for the search engine.
   * @param {string} [options.encoding]
   *   The encoding to use for the search engine.
   * @param {string} [options.search_url]
   *   The search URL to use for the search engine.
   * @param {string} [options.search_url_get_params]
   *   The GET search URL parameters to use for the search engine
   * @param {string} [options.search_url_post_params]
   *   The POST search URL parameters to use for the search engine
   * @param {string} [options.suggest_url]
   *   The suggestion URL to use for the search engine.
   * @param {string} [options.suggest_url_get_params]
   *   The suggestion URL parameters to use for the search engine.
   * @returns {object}
   *   The generated manifest.
   */
  createEngineManifest(options = {}) {
    options.name = options.name ?? "Example";
    options.id = options.id ?? options.name.toLowerCase().replaceAll(" ", "");
    if (!options.id.includes("@")) {
      options.id += "@tests.mozilla.org";
    }
    options.version = options.version ?? "1.0";
    let manifest = {
      version: options.version,
      browser_specific_settings: {
        gecko: {
          id: options.id,
        },
      },
      chrome_settings_overrides: {
        search_provider: {
          name: options.name,
          is_default: !!options.is_default,
          search_url: options.search_url ?? "https://example.com/",
        },
      },
    };

    if (options.icons) {
      manifest.icons = options.icons;
    }

    if (options.default_locale) {
      manifest.default_locale = options.default_locale;
    }

    if (options.search_url_post_params) {
      manifest.chrome_settings_overrides.search_provider.search_url_post_params =
        options.search_url_post_params;
    } else {
      manifest.chrome_settings_overrides.search_provider.search_url_get_params =
        options.search_url_get_params ?? "?q={searchTerms}";
    }

    if (options.favicon_url) {
      manifest.chrome_settings_overrides.search_provider.favicon_url =
        options.favicon_url;
    }
    if (options.encoding) {
      manifest.chrome_settings_overrides.search_provider.encoding =
        options.encoding;
    }
    if (options.keyword) {
      manifest.chrome_settings_overrides.search_provider.keyword =
        options.keyword;
    }
    if (options.suggest_url) {
      manifest.chrome_settings_overrides.search_provider.suggest_url =
        options.suggest_url;
    }
    if (options.suggest_url) {
      manifest.chrome_settings_overrides.search_provider.suggest_url_get_params =
        options.suggest_url_get_params;
    }
    if (options.favicon_url) {
      manifest.chrome_settings_overrides.search_provider.favicon_url =
        options.favicon_url;
    }
    return manifest;
  }

  /**
   * A mock idleService that allows us to simulate RemoteSettings
   * configuration updates.
   */
  idleService = {
    _observers: new Set(),

    _reset() {
      this._observers.clear();
    },

    _fireObservers(state) {
      for (let observer of this._observers.values()) {
        observer.observe(observer, state, null);
      }
    },

    QueryInterface: ChromeUtils.generateQI(["nsIUserIdleService"]),
    idleTime: 19999,

    addIdleObserver(observer) {
      this._observers.add(observer);
    },

    removeIdleObserver(observer) {
      this._observers.delete(observer);
    },
  };

  /**
   * Register the mock idleSerice.
   */
  useMockIdleService() {
    let fakeIdleService = MockRegistrar.register(
      "@mozilla.org/widget/useridleservice;1",
      this.idleService
    );
    this.#idleServiceCID = fakeIdleService;
  }

  /**
   * Sets the search configuration and search overrides configuration without
   * reloading the engines.
   * If parameters are not specified, the appropriate configuration is
   * reset to the data stored in remote settings.
   *
   * This is useful for example in xpcshell-tests before the search service
   * is initialized.
   *
   * @param {object[]} [partialConfig]
   *   The replacement configuration. Will be expanded via `expandPartialConfig`.
   * @param {object[]} [overridesConfig]
   *   The replacement overrides configuration.
   */
  setRemoteSettingsConfig(partialConfig, overridesConfig) {
    let config = this.expandPartialConfig(partialConfig);
    this.#stubConfig(lazy.SearchUtils.SETTINGS_KEY, config);
    this.#stubConfig(lazy.SearchUtils.SETTINGS_OVERRIDES_KEY, overridesConfig);
  }

  /**
   * Simulates an update to the RemoteSettings configuration.
   * If parameters are not specified, the appropriate configuration is
   * reset to the data stored in remote settings.
   *
   * This is useful if the search service has already been initialized.
   * Note that the search service is always initialized in mochitests.
   *
   * @param {object[]} [partialConfig]
   *   The replacement configuration. Will be expanded via `expandPartialConfig`.
   * @param {object[]} [overridesConfig]
   *   The replacement overrides configuration.
   */
  async updateRemoteSettingsConfig(partialConfig, overridesConfig) {
    if (!this.#idleServiceCID) {
      this.useMockIdleService();
    }
    let config = this.expandPartialConfig(partialConfig);
    this.#stubConfig(lazy.SearchUtils.SETTINGS_KEY, config);
    this.#stubConfig(lazy.SearchUtils.SETTINGS_OVERRIDES_KEY, overridesConfig);

    if (!config) {
      let settings = lazy.RemoteSettings(lazy.SearchUtils.SETTINGS_KEY);
      config = await settings.get();
    }
    if (!overridesConfig) {
      let settings = lazy.RemoteSettings(
        lazy.SearchUtils.SETTINGS_OVERRIDES_KEY
      );
      overridesConfig = await settings.get();
    }
    const reloadObserved = this.promiseSearchNotification("engines-reloaded");
    await lazy.RemoteSettings(lazy.SearchUtils.SETTINGS_KEY).emit("sync", {
      data: { current: config },
    });
    await lazy
      .RemoteSettings(lazy.SearchUtils.SETTINGS_OVERRIDES_KEY)
      .emit("sync", {
        data: { current: overridesConfig },
      });

    this.idleService._fireObservers("idle");
    await reloadObserved;
  }

  /**
   * Fetches a URL and converts the content to a data URL.
   *
   * @param {string} url
   *   The URL of the file that should be fetched.
   * @returns {Promise<string>}
   *   The content of the file as a data URL.
   */
  async fetchAsDataUrl(url) {
    let res = await fetch(url);
    let blob = await res.blob();
    let reader = new FileReader();
    return new Promise((resolve, reject) => {
      reader.onload = () => {
        resolve(reader.result);
      };
      reader.onerror = () => {
        reject(new Error("Failed to read blob."));
      };
      reader.readAsDataURL(blob);
    });
  }
}

export const SearchTestUtils = new _SearchTestUtils();
