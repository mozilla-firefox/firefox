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
} from "resource://gre/modules/SearchEngine.sys.mjs";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchEngineClassification: "resource://gre/modules/RustSearch.sys.mjs",
  SearchUtils: "resource://gre/modules/SearchUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "idleService",
  "@mozilla.org/widget/useridleservice;1",
  "nsIUserIdleService"
);

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "SearchEngine",
    maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
  });
});

// After the user has been idle for 30s, we'll update icons if we need to.
const ICON_UPDATE_ON_IDLE_DELAY = 30;

/**
 * Handles loading application provided search engine icons from remote settings.
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

    let appProvidedEngines = await Services.search.getAppProvidedEngines();
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
      "QueryPreferenceParameter should only exist for app provided engines which are never saved as JSON"
    );
    return {
      condition: "pref",
      name: this.name,
      pref: this._value,
    };
  }
}

/**
 * AppProvidedSearchEngine represents a search engine defined by the
 * search configuration.
 */
export class AppProvidedSearchEngine extends SearchEngine {
  static URL_TYPE_MAP = new Map([
    ["search", lazy.SearchUtils.URL_TYPE.SEARCH],
    ["suggestions", lazy.SearchUtils.URL_TYPE.SUGGEST_JSON],
    ["trending", lazy.SearchUtils.URL_TYPE.TRENDING_JSON],
    ["searchForm", lazy.SearchUtils.URL_TYPE.SEARCH_FORM],
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

  /**
   * Stores certain initial info about an engine. Used to verify whether we've
   * actually changed the engine, so that we don't record default engine
   * changed telemetry unnecessarily.
   *
   * @type {Map|null}
   */
  #prevEngineInfo = null;

  #partnerCode = "";

  /**
   * @param {object} options
   *   The options for this search engine.
   * @param {object} options.config
   *   The engine config from Remote Settings.
   * @param {object} [options.settings]
   *   The saved settings for the user.
   */
  constructor({ config, settings }) {
    super({
      loadPath: "[app]" + config.identifier,
      id: config.identifier,
    });

    this.#init(config);
    this._loadSettings(settings);

    this.#prevEngineInfo = new Map([
      ["name", this.name],
      ["_loadPath", this._loadPath],
      ["submissionURL", this.getSubmission("foo").uri.spec],
      ["aliases", this._definedAliases],
    ]);
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
   * Update this engine based on new config, used during
   * config upgrades.

   * @param {object} options
   *   The options object.
   *
   * @param {object} options.configuration
   *   The search engine configuration for application provided engines.
   */
  update({ configuration }) {
    this._urls = [];
    this.#init(configuration);

    let needToSendUpdate = this.#hasBeenModified(this, this.#prevEngineInfo, [
      "name",
      "_loadPath",
      "aliases",
    ]);

    // We only send a notification if critical fields have changed, e.g., ones
    // that may affect the UI or telemetry. If we want to add more fields here
    // in the future, we need to ensure we don't send unnecessary
    // `engine-update` telemetry. Therefore we may need additional notification
    // types or to implement an alternative.
    if (needToSendUpdate) {
      lazy.SearchUtils.notifyAction(
        this,
        lazy.SearchUtils.MODIFIED_TYPE.CHANGED
      );

      this._resetPrevEngineInfo();
    }
  }

  /**
   * Whether or not this engine is provided by the application, e.g. it is
   * in the list of configured search engines. Overrides the definition in
   * `SearchEngine`.
   *
   * @returns {boolean}
   */
  get isAppProvided() {
    return true;
  }

  /**
   * Whether or not this engine is an in-memory only search engine.
   * These engines are typically application provided or policy engines,
   * where they are loaded every time on SearchService initialization
   * using the policy JSON or the extension manifest. Minimal details of the
   * in-memory engines are saved to disk, but they are never loaded
   * from the user's saved settings file.
   *
   * @returns {boolean}
   *   Only returns true for application provided engines.
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

    let availableRecords =
      await AppProvidedSearchEngine.iconHandler.getAvailableRecords(this.id);
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
    let promise = AppProvidedSearchEngine.iconHandler.createIconURL(record);
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
   * Creates a JavaScript object that represents this engine.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    // For applicaiton provided engines we don't want to store all their data in
    // the settings file so just store the relevant metadata.
    return {
      id: this.id,
      _name: this.name,
      _isAppProvided: true,
      _metaData: this._metaData,
    };
  }

  /**
   * Initializes the engine.
   *
   * @param {SearchEngineDefinition} engineConfig
   *   The search engine configuration for application provided engines.
   */
  #init(engineConfig) {
    this._orderHint = engineConfig.orderHint;
    this._telemetryId = engineConfig.identifier;
    this.#isGeneralPurposeSearchEngine = lazy.SearchUtils
      .rustSelectorFeatureGate
      ? engineConfig.classification == lazy.SearchEngineClassification.GENERAL
      : // @ts-ignore This is supporting the non-Rust search engine selector.
        engineConfig.classification == "general";

    if (engineConfig.charset) {
      this._queryCharset = engineConfig.charset;
    }

    if (engineConfig.telemetrySuffix) {
      this._telemetryId += `-${engineConfig.telemetrySuffix}`;
    }

    if (engineConfig.clickUrl) {
      this.clickUrl = engineConfig.clickUrl;
    }

    this._name = engineConfig.name.trim();
    this._definedAliases =
      engineConfig.aliases?.map(alias => `@${alias}`) ?? [];
    this.#partnerCode = engineConfig.partnerCode ?? "";

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
    let urlType = AppProvidedSearchEngine.URL_TYPE_MAP.get(type);

    if (!urlType) {
      console.warn("unexpected engine url type.", type);
      return;
    }

    let engineURL = new EngineURL(
      urlType,
      urlData.method || "GET",
      urlData.base
    );

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

  /**
   * Determines whether the specified engine properties differ between their
   * current and initial values.
   *
   * @param {AppProvidedSearchEngine} currentEngine
   *   The current engine.
   * @param {Map} initialValues
   *   The initial values stored for the currentEngine.
   * @param {Array<string>} targetKeys
   *   The relevant keys to compare (current value for the engine vs. initial
   *   value).
   * @returns {boolean}
   *   Returns true if any of the properties relevant to default engine changed
   *   telemetry was changed.
   */
  #hasBeenModified(currentEngine, initialValues, targetKeys) {
    for (let i = 0; i < targetKeys.length; i++) {
      let key = targetKeys[i];

      if (
        !lazy.ObjectUtils.deepEqual(currentEngine[key], initialValues.get(key))
      ) {
        return true;
      }

      let currentEngineSubmissionURL =
        currentEngine.getSubmission("foo").uri.spec;
      if (currentEngineSubmissionURL != initialValues.get("submissionURL")) {
        return true;
      }
    }

    return false;
  }

  // Not #-prefixed private because it's spied upon in a test.
  _resetPrevEngineInfo() {
    this.#prevEngineInfo.forEach((_value, key) => {
      let newValue;
      if (key == "submissionURL") {
        newValue = this.getSubmission("foo").uri.spec;
      } else {
        newValue = this[key];
      }

      this.#prevEngineInfo.set(key, newValue);
    });
  }
}

/**
 * This class defines Search Engines that are built in to to the
 * application but whose installation was triggered by the user
 * and not the region configuration.
 */
export class UserInstalledAppEngine extends AppProvidedSearchEngine {
  /**
   * @param {object} options
   *   Options object passed to the constructor.
   * @param {object} options.config
   *   The configuration object for this engine defined in the Search Configuration.
   * @param {object} options.settings
   *   The settings object that the engine details will be stored in.
   */
  constructor({ config, settings }) {
    super({ config, settings });
    this.setAttr("user-installed", true);
  }
}
