/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

/**
 * @typedef {import("resource://services-settings/RemoteSettingsClient.sys.mjs").RemoteSettingsClient} RemoteSettingsClient
 * @typedef {import("../uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs").SearchEngineDefinition} SearchEngineDefinition
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

import {
  SearchEngine,
  EngineURL,
  QueryParameter,
} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchEngineClassification:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSearch.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  idleService: {
    service: "@mozilla.org/widget/useridleservice;1",
    iid: Ci.nsIUserIdleService,
  },
  logConsole: () =>
    console.createInstance({
      prefix: "SearchEngine",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

// If a user used a search engine at least once, we'll toggle a boolean.
const HAS_BEEN_USED = "hasBeenUsed";

// After the user has been idle for 30s, we'll update icons if we need to.
const ICON_UPDATE_ON_IDLE_DELAY = 30;

/**
 * Handles loading config search engine icons from remote settings.
 */
class IconHandler {
  /**
   * The remote settings client for the search engine icons.
   *
   * @type {?RemoteSettingsClient}
   */
  #iconCollection = null;

  /**
   * The list of icon records from the remote settings collection indexed by
   * the first two characters of their engineIdentifier for fast search by
   * engineID.
   *
   * If a record has multiple engineIdentifiers with different
   * first characters, the record will be available once under every key.
   *
   * @type {?Map<string, object[]>}
   */
  #iconMap = null;

  /**
   * A flag that indicates if we have queued an idle observer to update icons.
   *
   * @type {boolean}
   */
  #queuedIdle = false;

  /**
   * A map of pending updates that need to be applied to the engines. This is
   * keyed via record id, so that if multiple updates are queued for the same
   * record, then we will only update the engine once.
   *
   * @type {Map<string, object>}
   */
  #pendingUpdatesMap = new Map();

  constructor() {
    this.#iconCollection = lazy.RemoteSettings("search-config-icons");
    this.#iconCollection.on("sync", this._onIconListUpdated.bind(this));
  }

  /**
   * Extracts the first two chars of an engineID for use with `this.#iconMap`.
   *
   * @param {string} engineID
   *   ID of the engine.
   * @returns {string}
   *   The key used by `this.#iconMap`.
   */
  getKey(engineID) {
    return engineID.substring(0, 2);
  }

  /**
   * Returns a list of the sizes of the records available for the supplied engine.
   *
   * @param {string} engineIdentifier
   *  The ID of the engine.
   * @returns {Promise<object[]>}
   *  The available records.
   */
  async getAvailableRecords(engineIdentifier) {
    if (!this.#iconMap) {
      await this.#buildIconMap();
    }

    let iconList = this.#iconMap.get(this.getKey(engineIdentifier)) || [];
    return iconList.filter(r =>
      this.#identifierMatches(engineIdentifier, r.engineIdentifiers)
    );
  }

  /**
   * Creates an object URL for the icon of the given record.
   *
   * @param {object} iconRecord
   *   The record of the icon.
   * @returns {Promise<?string>}
   *   An object URL that can be used to reference the contents of the specified
   *   source object or null of there is no icon with the supplied width.
   */
  async createIconURL(iconRecord) {
    let iconData;
    try {
      iconData = await this.#iconCollection.attachments.get(iconRecord);
    } catch (ex) {
      console.error(ex);
    }
    if (!iconData) {
      console.warn("Unable to find the attachment for", iconRecord.id);
      // Queue an update in case we haven't downloaded it yet.
      this.#pendingUpdatesMap.set(iconRecord.id, iconRecord);
      this.#maybeQueueIdle();
      return null;
    }

    if (iconData.record.last_modified != iconRecord.last_modified) {
      // The icon we have stored is out of date, queue an update so that we'll
      // download the new icon.
      this.#pendingUpdatesMap.set(iconRecord.id, iconRecord);
      this.#maybeQueueIdle();
    }
    return URL.createObjectURL(
      new Blob([iconData.buffer], { type: iconRecord.attachment.mimetype })
    );
  }

  QueryInterface = ChromeUtils.generateQI(["nsIObserver"]);

  /**
   * Called when there is an update queued and the user has been observed to be
   * idle for ICON_UPDATE_ON_IDLE_DELAY seconds.
   *
   * This will always download new icons (added or updated), even if there is
   * no current engine that matches the identifiers. This is to ensure that we
   * have pre-populated the cache if the engine is added later for this user.
   *
   * We do not handle deletes, as remote settings will handle the cleanup of
   * removed records. We also do not expect the case where an icon is removed
   * for an active engine.
   *
   * @param {nsISupports} subject
   *   The subject of the observer.
   * @param {string} topic
   *   The topic of the observer.
   */
  async observe(subject, topic) {
    if (topic != "idle") {
      return;
    }

    this.#queuedIdle = false;
    lazy.idleService.removeIdleObserver(this, ICON_UPDATE_ON_IDLE_DELAY);

    // Update the icon list, in case engines will call getIcon() again.
    await this.#buildIconMap();

    let appProvidedEngines = await lazy.SearchService.getAppProvidedEngines();
    for (let record of this.#pendingUpdatesMap.values()) {
      let iconData;
      try {
        iconData = await this.#iconCollection.attachments.download(record);
      } catch (ex) {
        console.error("Could not download new icon", ex);
        continue;
      }

      for (let engine of appProvidedEngines) {
        if (this.#identifierMatches(engine.id, record.engineIdentifiers)) {
          await engine.maybeUpdateIconURL(
            URL.createObjectURL(
              new Blob([iconData.buffer], {
                type: record.attachment.mimetype,
              })
            ),
            record.imageSize
          );
        }
      }
    }

    this.#pendingUpdatesMap.clear();
  }

  /**
   * Checks if the identifier matches any of the engine identifiers.
   *
   * @param {string} identifier
   *   The identifier of the engine.
   * @param {string[]} engineIdentifiers
   *   The list of engine identifiers to match against. This can include
   *   wildcards at the end of strings.
   * @returns {boolean}
   *   Returns true if the identifier matches any of the engine identifiers.
   */
  #identifierMatches(identifier, engineIdentifiers) {
    return engineIdentifiers.some(i => {
      if (i.endsWith("*")) {
        return identifier.startsWith(i.slice(0, -1));
      }
      return identifier == i;
    });
  }

  /**
   * Obtains the icon list from the remote settings collection.
   */
  async #buildIconMap() {
    let iconList = [];
    try {
      iconList = await this.#iconCollection.get();
    } catch (ex) {
      console.error(ex);
    }
    if (!iconList.length) {
      console.error("Failed to obtain search engine icon list records");
    }

    this.#iconMap = new Map();
    for (let record of iconList) {
      let keys = new Set(record.engineIdentifiers.map(this.getKey));
      for (let key of keys) {
        if (this.#iconMap.has(key)) {
          this.#iconMap.get(key).push(record);
        } else {
          this.#iconMap.set(key, [record]);
        }
      }
    }
  }

  /**
   * Called via a callback when remote settings updates the icon list. This
   * stores potential updates and queues an idle observer to apply them.
   *
   * @param {object} payload
   *   The payload from the remote settings collection.
   * @param {object} payload.data
   *   The payload data from the remote settings collection.
   * @param {object[]} payload.data.created
   *    The list of created records.
   * @param {object[]} payload.data.updated
   *    The list of updated records.
   */
  async _onIconListUpdated({ data: { created, updated } }) {
    created.forEach(record => {
      this.#pendingUpdatesMap.set(record.id, record);
    });
    for (let record of updated) {
      if (record.new) {
        this.#pendingUpdatesMap.set(record.new.id, record.new);
      }
    }
    this.#maybeQueueIdle();
  }

  /**
   * Queues an idle observer if there are pending updates.
   */
  #maybeQueueIdle() {
    if (this.#pendingUpdatesMap && !this.#queuedIdle) {
      this.#queuedIdle = true;
      lazy.idleService.addIdleObserver(this, ICON_UPDATE_ON_IDLE_DELAY);
    }
  }
}

/**
 * A simple class to handle caching of preferences that may be read from
 * parameters.
 */
const ParamPreferenceCache = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  initCache() {
    // Preference params are normally only on the default branch to avoid these being easily changed.
    // We allow them on the normal branch in nightly builds to make testing easier.
    let branchFetcher = AppConstants.NIGHTLY_BUILD
      ? "getBranch"
      : "getDefaultBranch";
    this.branch = Services.prefs[branchFetcher](
      lazy.SearchUtils.BROWSER_SEARCH_PREF + "param."
    );
    this.cache = new Map();
    this.nimbusCache = new Map();
    for (let prefName of this.branch.getChildList("")) {
      this.cache.set(prefName, this.branch.getCharPref(prefName, null));
    }
    this.branch.addObserver("", this, true);

    this.onNimbusUpdate = this.onNimbusUpdate.bind(this);
    this.onNimbusUpdate();
    lazy.NimbusFeatures.searchConfiguration.onUpdate(this.onNimbusUpdate);
    lazy.NimbusFeatures.searchConfiguration.ready().then(this.onNimbusUpdate);
  },

  observe(subject, topic, data) {
    this.cache.set(data, this.branch.getCharPref(data, null));
  },

  onNimbusUpdate() {
    let extraParams =
      lazy.NimbusFeatures.searchConfiguration.getVariable("extraParams") || [];
    this.nimbusCache.clear();
    // The try catch ensures that if the params were incorrect for some reason,
    // the search service can still startup properly.
    try {
      for (const { key, value } of extraParams) {
        this.nimbusCache.set(key, value);
      }
    } catch (ex) {
      console.error("Failed to load nimbus variables for extraParams:", ex);
    }
  },

  getPref(prefName) {
    if (!this.cache) {
      this.initCache();
    }
    return this.nimbusCache.has(prefName)
      ? this.nimbusCache.get(prefName)
      : this.cache.get(prefName);
  },
};

/**
 * Represents a special paramater that can be set by preferences. The
 * value is read from the 'browser.search.param.*' default preference
 * branch.
 */
class QueryPreferenceParameter extends QueryParameter {
  /**
   * @param {string} name
   *   The name of the parameter as injected into the query string.
   * @param {string} prefName
   *   The name of the preference to read from the branch.
   */
  constructor(name, prefName) {
    super(name, prefName);
  }

  get value() {
    const prefValue = ParamPreferenceCache.getPref(this._value);
    return prefValue ? encodeURIComponent(prefValue) : null;
  }

  /**
   * Creates a JavaScript object that represents this parameter.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    lazy.logConsole.warn(
      "QueryPreferenceParameter should only exist for config engines which are never saved as JSON"
    );
    return {
      condition: "pref",
      name: this.name,
      pref: this._value,
    };
  }
}

/**
 * ConfigSearchEngine represents a search engine defined by the search-config-v2.
 * This class is intended as an abstract class.
 */
export class ConfigSearchEngine extends SearchEngine {
  static URL_TYPE_MAP = new Map([
    ["search", lazy.SearchUtils.URL_TYPE.SEARCH],
    ["suggestions", lazy.SearchUtils.URL_TYPE.SUGGEST_JSON],
    ["trending", lazy.SearchUtils.URL_TYPE.TRENDING_JSON],
    ["searchForm", lazy.SearchUtils.URL_TYPE.SEARCH_FORM],
    ["visualSearch", lazy.SearchUtils.URL_TYPE.VISUAL_SEARCH],
  ]);
  static iconHandler = new IconHandler();

  /**
   * Promises for the blob URL of the icon by icon width.
   * We save the promises to avoid reentrancy issues.
   *
   * @type {Map<number, Promise<?string>>}
   */
  #blobURLPromises = new Map();

  /**
   * Whether or not this is a general purpose search engine.
   *
   * @type {boolean}
   */
  #isGeneralPurposeSearchEngine = false;

  #partnerCode = "";

  /**
   * The telemetry id to use for this engine for legacy telemetry. This is
   * deprecated and should not be used for new telemetry.
   *
   * @type {string}
   */
  #telemetryId;

  /**
   * The order hint for this engine, as determined by the search configuration.
   *
   * @type {?number}
   */
  #orderHint;

  /**
   * @param {object} options
   *   The options for this search engine.
   * @param {SearchEngineDefinition} options.config
   *   The engine config from Remote Settings.
   * @param {object} [options.settings]
   *   The saved settings for the user.
   */
  constructor({ config, settings }) {
    if (new.target === ConfigSearchEngine) {
      throw new Error("Cannot instanciate abstract class ConfigSearchEngine");
    }
    // The load path for both all types of ConfigSearchEngine is prefixed
    // with [app] for historical reasons.
    super({
      loadPath: "[app]" + config.identifier,
      id: config.identifier,
    });

    this.#init(config);
    this._loadSettings(settings);
  }

  /**
   * Used to clean up the engine when it is removed. This will revoke the blob
   * URL for the icon.
   */
  async cleanup() {
    for (let [size, blobURLPromise] of this.#blobURLPromises) {
      URL.revokeObjectURL(await blobURLPromise);
      this.#blobURLPromises.delete(size);
    }
  }

  /**
   * Update this engine based on new config, used during config upgrades.
   *
   * @param {object} options
   *   The options object.
   * @param {SearchEngineDefinition} options.configuration
   *    The updated engine definition for this engine from the search config.
   */
  update({ configuration }) {
    this._urls = [];
    this.#init(configuration);

    lazy.SearchUtils.notifyAction(this, lazy.SearchUtils.MODIFIED_TYPE.CHANGED);
  }

  /**
   * Whether or not this engine is an in-memory only search engine.
   * These engines are typically config or policy engines,
   * where they are loaded every time on SearchService initialization
   * using the search config or the policy JSON. Minimal details of the
   * in-memory engines are saved to disk, but they are never loaded
   * from the user's saved settings file.
   *
   * @returns {boolean}
   */
  get inMemory() {
    return true;
  }

  /**
   * Whether or not this engine is a "general" search engine, e.g. is it for
   * generally searching the web, or does it have a specific purpose like
   * shopping.
   *
   * @returns {boolean}
   */
  get isGeneralPurposeEngine() {
    return this.#isGeneralPurposeSearchEngine;
  }

  /**
   * @type {string}
   *   The partner code being used by this search engine in the Search URL.
   */
  get partnerCode() {
    return this.#partnerCode;
  }

  /**
   * Returns the appropriate identifier to use for telemetry. It is based on
   * the following order:
   *
   * - telemetryId: The telemetry id from the configuration, or derived from
   *                the WebExtension name.
   * - other-<name>: The engine name prefixed by `other-` for non-config-engines.
   *
   * @returns {string}
   * @deprecated This should not be used for new telemetry. It is a combined
   * field that contains multiple values. Report separate
   * id/partner_code/other fields instead.
   */
  get telemetryId() {
    if (this.getAttr("overriddenBy")) {
      return this.#telemetryId + "-addon";
    }
    return this.#telemetryId;
  }

  /**
   * Returns whether the engine is new. An overridden engine is never new because the
   * user had already installed the third-party engine now overriding the
   * app-provided engine, so it is not new to them.
   *
   * @returns {boolean}
   */
  isNew() {
    if (this.overriddenById) {
      return false;
    }

    return super.isNew();
  }

  /**
   * Gets the order hint for this engine. This is determined from the search
   * configuration when the engine is initialized.
   *
   * @type {?number}
   */
  get orderHint() {
    return this.#orderHint;
  }

  /**
   * Returns the icon URL for the search engine closest to the preferred width.
   *
   * @param {number} preferredWidth
   *   The preferred width of the image. Defaults to 16.
   * @returns {Promise<?string>}
   *   A promise that resolves to the URL of the icon.
   */
  async getIconURL(preferredWidth) {
    // XPCOM interfaces pass optional number parameters as 0.
    preferredWidth ||= 16;

    if (this.id == "funsearch") {
      if (preferredWidth >= 64) {
        return "chrome://branding/content/icon64.png";
      }
      if (preferredWidth >= 32) {
        return "chrome://branding/content/icon32.png";
      }
      return "chrome://branding/content/icon16.png";
    }

    let availableRecords =
      await ConfigSearchEngine.iconHandler.getAvailableRecords(this.id);
    if (!availableRecords.length) {
      console.warn("No icon found for", this.id);
      return null;
    }

    let availableSizes = availableRecords.map(r => r.imageSize);
    let width = lazy.SearchUtils.chooseIconSize(preferredWidth, availableSizes);

    if (this.#blobURLPromises.has(width)) {
      return this.#blobURLPromises.get(width);
    }

    let record = availableRecords.find(r => r.imageSize == width);
    let promise = ConfigSearchEngine.iconHandler.createIconURL(record);
    this.#blobURLPromises.set(width, promise);
    return promise;
  }

  /**
   * Updates the icon URL for the given size.
   *
   * @param {string} blobURL
   *   The new icon URL for the search engine.
   * @param {number} size
   *   The size of the icon in blobURL.
   */
  async maybeUpdateIconURL(blobURL, size) {
    if (this.#blobURLPromises.has(size)) {
      URL.revokeObjectURL(await this.#blobURLPromises.get(size));
    }
    this.#blobURLPromises.set(size, Promise.resolve(blobURL));

    lazy.SearchUtils.notifyAction(
      this,
      lazy.SearchUtils.MODIFIED_TYPE.ICON_CHANGED
    );
  }

  /**
   * Marks the search engine has having been used.
   */
  markAsUsed() {
    this.setAttr(HAS_BEEN_USED, true, true);
  }

  /**
   * Whether this search engine has ever been used. This returns true if
   * `markAsUsed()` has been called at least once for this engine.
   *
   * @returns {boolean}
   */
  get hasBeenUsed() {
    return this.getAttr(HAS_BEEN_USED) ?? false;
  }

  /**
   * Clears the usage record for this search engine. The property hasBeenUsed
   * will return false unless `markAsUsed()` is called again.
   */
  clearUsage() {
    this.clearAttr(HAS_BEEN_USED);
  }

  /**
   * Creates a JavaScript object that represents this engine.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    // For config engines we don't want to store all their data in
    // the settings file so just store the relevant metadata.
    return {
      id: this.id,
      _name: this.name,
      _isConfigEngine: true,
      _metaData: this._metaData,
    };
  }

  /**
   * Initializes the engine.
   *
   * @param {SearchEngineDefinition} engineConfig
   *   The engine definition from the search config for this engine.
   */
  #init(engineConfig) {
    this.#orderHint = engineConfig.orderHint;
    this.#telemetryId = engineConfig.identifier;
    this.#isGeneralPurposeSearchEngine =
      engineConfig.classification == lazy.SearchEngineClassification.GENERAL;

    if (engineConfig.charset) {
      this._queryCharset = engineConfig.charset;
    }

    if (engineConfig.telemetrySuffix) {
      this.#telemetryId += `-${engineConfig.telemetrySuffix}`;
    }

    if (engineConfig.clickUrl) {
      this.clickUrl = engineConfig.clickUrl;
    }

    this._name = engineConfig.name.trim();
    this._definedAliases =
      engineConfig.aliases?.map(alias => `@${alias}`) ?? [];
    this.#partnerCode = engineConfig.partnerCode ?? "";
    /** @type {string} */
    this.isNewUntil = engineConfig.isNewUntil ?? "";

    for (const [type, urlData] of Object.entries(engineConfig.urls)) {
      if (urlData) {
        this.#setUrl(type, urlData, engineConfig.partnerCode);
      }
    }
  }

  /**
   * This sets the urls for the search engine based on the supplied parameters.
   *
   * @param {string} type
   *   The type of url. This could be a url for search, suggestions, or trending.
   * @param {object} urlData
   *   The url data contains the template/base url and url params.
   * @param {string} partnerCode
   *   The partner code associated with the search engine.
   */
  #setUrl(type, urlData, partnerCode) {
    let urlType = ConfigSearchEngine.URL_TYPE_MAP.get(type);
    if (!urlType) {
      console.warn("unexpected engine url type.", type);
      return;
    }

    let engineURL = new EngineURL({
      ...urlData,
      type: urlType,
      template: urlData.base,
    });

    if (urlData.params) {
      let isEnterprise = Services.policies.isEnterprise;
      let enterpriseParams = urlData.params
        .filter(p => p.enterpriseValue != undefined)
        .map(p => p.name);

      for (const param of urlData.params) {
        switch (true) {
          case param.value != undefined:
            if (!isEnterprise || !enterpriseParams.includes(param.name)) {
              engineURL.addParam(
                param.name,
                param.value == "{partnerCode}" ? partnerCode : param.value
              );
            }
            break;
          case param.experimentConfig != undefined:
            if (!isEnterprise || !enterpriseParams.includes(param.name)) {
              engineURL.addQueryParameter(
                new QueryPreferenceParameter(param.name, param.experimentConfig)
              );
            }
            break;
          case param.enterpriseValue != undefined:
            if (isEnterprise) {
              engineURL.addParam(
                param.name,
                param.enterpriseValue == "{partnerCode}"
                  ? partnerCode
                  : param.enterpriseValue
              );
            }
            break;
        }
      }
    }

    if (urlData.searchTermParamName) {
      // The search term parameter is always added last, which will add it to the
      // end of the URL. This is because in the past we have seen users trying to
      // modify their searches by altering the end of the URL.
      engineURL.setSearchTermParamName(urlData.searchTermParamName);
    } else if (
      !urlData.base.includes("{searchTerms}") &&
      (urlType == lazy.SearchUtils.URL_TYPE.SEARCH ||
        urlType == lazy.SearchUtils.URL_TYPE.SUGGEST_JSON)
    ) {
      throw new Error("Search terms missing from engine URL.");
    }

    this._urls.push(engineURL);
  }
}

/**
 * This class represents ConfigSearchEngines that were pre-configured by the
 * application based on the user's environment, rather than user-installed.
 */
export class AppProvidedConfigEngine extends ConfigSearchEngine {
  /**
   * Converts this engine into a UserInstalledConfigEngine.
   *
   * This can be called when the search service reloads and this engine is no
   * longer available in the user's region but it has a "user-installed" attribute.
   */
  downgrade() {
    if (!this.getAttr("user-installed")) {
      throw new Error("Cannot downgrade without user-installed attribute.");
    }
    Object.setPrototypeOf(this, UserInstalledConfigEngine.prototype);
  }
}

/**
 * This class represents ConfigSearchEngines that were manually installed
 * by the user, e.g. using the contextual search feature.
 */
export class UserInstalledConfigEngine extends ConfigSearchEngine {
  /**
   * @param {object} options
   *   The options for this search engine.
   * @param {SearchEngineDefinition} options.config
   *   The engine config from Remote Settings.
   * @param {object} [options.settings]
   *   The saved settings for the user.
   */
  constructor(options) {
    super(options);
    this.setAttr("user-installed", true);
  }

  /**
   * Converts this engine into a AppProvidedSearchEngine.
   *
   * This can be called when the search service reloads and this engine
   * is now available in the user's region.
   */
  upgrade() {
    Object.setPrototypeOf(this, AppProvidedConfigEngine.prototype);
  }
}
