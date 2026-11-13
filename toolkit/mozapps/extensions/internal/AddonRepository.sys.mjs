/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  ServiceRequest: "resource://gre/modules/ServiceRequest.sys.mjs",
});

// The current platform as specified in the AMO API:
// http://addons-server.readthedocs.io/en/latest/topics/api/addons.html#addon-detail-platform
ChromeUtils.defineLazyGetter(lazy, "PLATFORM", () => {
  let platform = Services.appinfo.OS;
  switch (platform) {
    case "Darwin":
      return "mac";

    case "Linux":
      return "linux";

    case "Android":
      return "android";

    case "WINNT":
      return "windows";
  }
  return platform;
});

const PREF_GETADDONS_CACHE_ENABLED = "extensions.getAddons.cache.enabled";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "getAddonsCacheEnabled",
  PREF_GETADDONS_CACHE_ENABLED
);

const PREF_GETADDONS_CACHE_TYPES = "extensions.getAddons.cache.types";
const PREF_GETADDONS_CACHE_ID_ENABLED =
  "extensions.%ID%.getAddons.cache.enabled";
const PREF_GETADDONS_BROWSEADDONS = "extensions.getAddons.browseAddons";
const PREF_GETADDONS_BYIDS = "extensions.getAddons.get.url";
const PREF_GETADDONS_BROWSESEARCHRESULTS =
  "extensions.getAddons.search.browseURL";
const PREF_GETADDONS_DB_SCHEMA = "extensions.getAddons.databaseSchema";
const PREF_GET_LANGPACKS = "extensions.getAddons.langpacks.url";
const PREF_GET_BROWSER_MAPPINGS = "extensions.getAddons.browserMappings.url";

const PREF_METADATA_LASTUPDATE = "extensions.getAddons.cache.lastUpdate";
const PREF_METADATA_UPDATETHRESHOLD_SEC =
  "extensions.getAddons.cache.updateThreshold";
const DEFAULT_METADATA_UPDATETHRESHOLD_SEC = 172800; // two days

const DEFAULT_CACHE_TYPES = "extension,theme,locale,dictionary";

const FILE_DATABASE = "addons.json";
const DB_SCHEMA = 6;
const DB_MIN_JSON_SCHEMA = 5;
const DB_BATCH_TIMEOUT_MS = 50;

const BLANK_DB = function () {
  return {
    addons: new Map(),
    schema: DB_SCHEMA,
  };
};

import { Log } from "resource://gre/modules/Log.sys.mjs";

const LOGGER_ID = "addons.repository";

// Create a new logger for use by the Addons Repository
// (Requires AddonManager.sys.mjs)
var logger = Log.repository.getLogger(LOGGER_ID);

function convertHTMLToPlainText(html) {
  if (!html) {
    return html;
  }
  var converter = Cc[
    "@mozilla.org/widget/htmlformatconverter;1"
  ].createInstance(Ci.nsIFormatConverter);

  var input = Cc["@mozilla.org/supports-string;1"].createInstance(
    Ci.nsISupportsString
  );
  input.data = html.replace(/\n/g, "<br>");

  var output = {};
  converter.convert("text/html", input, "text/plain", output);

  if (output.value instanceof Ci.nsISupportsString) {
    return output.value.data.replace(/\r\n/g, "\n");
  }
  return html;
}

async function getAddonsToCache(aIds) {
  let types = Services.prefs.getStringPref(
    PREF_GETADDONS_CACHE_TYPES,
    DEFAULT_CACHE_TYPES
  );

  types = types.split(",");

  let addons = await lazy.AddonManager.getAddonsByIDs(aIds);
  let enabledIds = [];

  for (let [i, addon] of addons.entries()) {
    var preference = PREF_GETADDONS_CACHE_ID_ENABLED.replace("%ID%", aIds[i]);
    // If the preference doesn't exist caching is enabled by default
    if (!Services.prefs.getBoolPref(preference, true)) {
      continue;
    }

    // The add-ons manager may not know about this ID yet if it is a pending
    // install. In that case we'll just cache it regardless

    // Don't cache add-ons of the wrong types
    if (addon && !types.includes(addon.type)) {
      continue;
    }

    // Don't cache system add-ons
    if (addon && addon.isSystem) {
      continue;
    }

    enabledIds.push(aIds[i]);
  }

  return enabledIds;
}

function AddonSearchResult(aId) {
  this.id = aId;
  this.icons = {};
  this._unsupportedProperties = {};
}

AddonSearchResult.prototype = {
  /**
   * The ID of the add-on
   */
  id: null,

  /**
   * The add-on type (e.g. "extension" or "theme")
   */
  type: null,

  /**
   * The name of the add-on
   */
  name: null,

  /**
   * The version of the add-on
   */
  version: null,

  /**
   * The creator of the add-on
   */
  creator: null,

  /**
   * The developers of the add-on
   */
  developers: null,

  /**
   * A short description of the add-on
   */
  description: null,

  /**
   * The full description of the add-on
   */
  fullDescription: null,

  /**
   * The end-user licensing agreement (EULA) of the add-on
   */
  eula: null,

  /**
   * The url of the add-on's icon
   */
  get iconURL() {
    return this.icons && this.icons[32];
  },

  /**
   * The URLs of the add-on's icons, as an object with icon size as key
   */
  icons: null,

  /**
   * An array of screenshot urls for the add-on
   */
  screenshots: null,

  /**
   * The homepage for the add-on
   */
  homepageURL: null,

  /**
   * The support URL for the add-on
   */
  supportURL: null,

  /**
   * The contribution url of the add-on
   */
  contributionURL: null,

  /**
   * The rating of the add-on, 0-5
   */
  averageRating: null,

  /**
   * The number of reviews for this add-on
   */
  reviewCount: null,

  /**
   * The URL to the list of reviews for this add-on
   */
  reviewURL: null,

  /**
   * The number of times the add-on was downloaded the current week
   */
  weeklyDownloads: null,

  /**
   * The URL to the AMO detail page of this (listed) add-on
   */
  amoListingURL: null,

  /**
   * AddonInstall object generated from the add-on XPI url
   */
  install: null,

  /**
   * nsIURI storing where this add-on was installed from
   */
  sourceURI: null,

  /**
   * The Date that the add-on was most recently updated
   */
  updateDate: null,

  toJSON() {
    let json = {};

    for (let property of Object.keys(this)) {
      let value = this[property];
      if (property.startsWith("_") || typeof value === "function") {
        continue;
      }

      try {
        switch (property) {
          case "sourceURI":
            json.sourceURI = value ? value.spec : "";
            break;

          case "updateDate":
            json.updateDate = value ? value.getTime() : "";
            break;

          default:
            json[property] = value;
        }
      } catch (ex) {
        logger.warn("Error writing property value for " + property);
      }
    }

    for (let property of Object.keys(this._unsupportedProperties)) {
      let value = this._unsupportedProperties[property];
      if (!property.startsWith("_")) {
        json[property] = value;
      }
    }

    return json;
  },
};

/**
 * The add-on repository is a source of add-ons that can be installed. It can
 * be searched in three ways. The first takes a list of IDs and returns a
 * list of the corresponding add-ons. The second returns a list of add-ons that
 * come highly recommended. This list should change frequently. The third is to
 * search for specific search terms entered by the user. Searches are
 * asynchronous and results should be passed to the provided callback object
 * when complete. The results passed to the callback should only include add-ons
 * that are compatible with the current application and are not already
 * installed.
 */
export var AddonRepository = {
  /**
   * The homepage for visiting this repository. If the corresponding preference
   * is not defined, defaults to about:blank.
   */
  get homepageURL() {
    let url = this._formatURLPref(PREF_GETADDONS_BROWSEADDONS, {});
    return url != null ? url : "about:blank";
  },

  get appIsShuttingDown() {
    return Services.startup.shuttingDown;
  },

  /**
   * Retrieves the url that can be visited to see search results for the given
   * terms. If the corresponding preference is not defined, defaults to
   * about:blank.
   *
   * @param  aSearchTerms
   *         Search terms used to search the repository
   */
  getSearchURL(aSearchTerms) {
    let url = this._formatURLPref(PREF_GETADDONS_BROWSESEARCHRESULTS, {
      TERMS: aSearchTerms,
    });
    return url != null ? url : "about:blank";
  },

  /**
   * Whether caching is currently enabled
   */
  get cacheEnabled() {
    return lazy.getAddonsCacheEnabled;
  },

  /**
   * Shut down AddonRepository
   * return: promise{integer} resolves with the result of flushing
   *         the AddonRepository database
   */
  shutdown() {
    return AddonDatabase.shutdown(false);
  },

  metadataAge() {
    let now = Math.round(Date.now() / 1000);
    let lastUpdate = Services.prefs.getIntPref(PREF_METADATA_LASTUPDATE, 0);
    return Math.max(0, now - lastUpdate);
  },

  isMetadataStale() {
    let threshold = Services.prefs.getIntPref(
      PREF_METADATA_UPDATETHRESHOLD_SEC,
      DEFAULT_METADATA_UPDATETHRESHOLD_SEC
    );
    return this.metadataAge() > threshold;
  },

  /**
   * Asynchronously get a cached add-on by id. The add-on (or null if the
   * add-on is not found) is passed to the specified callback. If caching is
   * disabled, null is passed to the specified callback.
   *
   * The callback variant exists only for existing code in XPIProvider.sys.mjs
   * and XPIDatabase.sys.mjs that requires a synchronous callback, yuck.
   *
   * @param  aId
   *         The id of the add-on to get
   */
  async getCachedAddonByID(aId, aCallback) {
    if (!aId || !this.cacheEnabled) {
      if (aCallback) {
        aCallback(null);
      }
      return null;
    }

    if (aCallback && AddonDatabase._loaded) {
      let addon = AddonDatabase.getAddon(aId);
      aCallback(addon);
      return addon;
    }

    await AddonDatabase.openConnection();

    let addon = AddonDatabase.getAddon(aId);
    if (aCallback) {
      aCallback(addon);
    }
    return addon;
  },

  /**
   * Clear and delete the AddonRepository database
   *
   * @return Promise{null} resolves when the database is deleted
   */
  _clearCache() {
    return AddonDatabase.delete().then(() =>
      lazy.AddonManagerPrivate.updateAddonRepositoryData()
    );
  },

  /**
   * Create a ServiceRequest instance.
   *
   * @return ServiceRequest returns a ServiceRequest instance.
   */
  _createServiceRequest() {
    return new lazy.ServiceRequest({ mozAnon: true });
  },

  /**
   * Fetch data from an API where the results may span multiple "pages".
   * This function will take care of issuing multiple requests until all
   * the results have been fetched, and will coalesce them all into a
   * single return value.  The handling here is specific to the way AMO
   * implements paging (ie a JSON result with a "next" property).
   *
   * @param {string} pref
   *                 The pref name that contains the API URL to call.
   * @param {object} params
   *                 A key-value object that contains the parameters to replace
   *                 in the API URL.
   * @param {function} handler
   *                   This function will be called once per page of results,
   *                   it should return an array of objects (the type depends
   *                   on the particular API being called of course).
   *
   * @returns Promise{array} An array of all the individual results from
   *                         the API call(s).
   */
  _fetchPaged(pref, params, handler) {
    const startURL = this._formatURLPref(pref, params);

    let results = [];
    const fetchNextPage = url => {
      return new Promise((resolve, reject) => {
        if (this.appIsShuttingDown) {
          logger.debug(
            "Rejecting AddonRepository._fetchPaged call, shutdown already in progress"
          );
          reject(
            new Error(
              `Reject ServiceRequest for "${url}", shutdown already in progress`
            )
          );
          return;
        }
        let request = this._createServiceRequest();
        request.mozBackgroundRequest = true;
        request.open("GET", url, true);
        request.responseType = "json";

        request.addEventListener("error", () => {
          reject(new Error(`GET ${url} failed`));
        });
        request.addEventListener("timeout", () => {
          reject(new Error(`GET ${url} timed out`));
        });
        request.addEventListener("load", () => {
          let response = request.response;
          if (!response || (request.status != 200 && request.status != 0)) {
            reject(new Error(`GET ${url} failed (status ${request.status})`));
            return;
          }

          try {
            results.push(...handler(response.results));
          } catch (err) {
            reject(err);
          }

          if (response.next) {
            resolve(fetchNextPage(response.next));
          }

          resolve(results);
        });

        request.send(null);
      });
    };

    return fetchNextPage(startURL);
  },

  /**
   * Fetch metadata for a given set of addons from AMO.
   *
   * @param  aIDs
   *         The array of ids to retrieve metadata for.
   * @returns {Array<AddonSearchResult>}
   */
  async getAddonsByIDs(aIDs) {
    const idCheck = aIDs.map(id => {
      if (id.startsWith("rta:")) {
        return atob(id.split(":")[1]);
      }
      return id;
    });

    const addons = await this._fetchPaged(
      PREF_GETADDONS_BYIDS,
      { IDS: aIDs.join(",") },
      results =>
        results
          .map(entry => this._parseAddon(entry))
          // Only return the add-ons corresponding the IDs passed to this method.
          .filter(addon => idCheck.includes(addon.id))
    );

    return addons;
  },

  /**
   * Fetch the Firefox add-ons mapped to the list of extension IDs for the
   * browser ID passed to this method.
   *
   * See: https://addons-server.readthedocs.io/en/latest/topics/api/addons.html#browser-mappings
   *
   * @param browserID
   *        The browser ID used to retrieve the mapping of IDs.
   * @param extensionIDs
   *        The array of browser (non-Firefox) extension IDs to retrieve
   *        metadata for.
   * @returns {object} result
   *        The result of the mapping.
   * @returns {Array<AddonSearchResult>} result.addons
   *        The AddonSearchResults for the addons that were successfully mapped.
   * @returns {Array<string>} result.matchedIDs
   *        The IDs of the extensions that were successfully matched to
   *        equivalents that can be installed in this browser. These are
   *        the IDs before matching to equivalents.
   * @returns {Array<string>} result.unmatchedIDs
   *        The IDs of the extensions that were not matched to equivalents.
   */
  async getMappedAddons(browserID, extensionIDs) {
    let matchedExtensionIDs = new Set();
    let unmatchedExtensionIDs = new Set(extensionIDs);

    const addonIds = await this._fetchPaged(
      PREF_GET_BROWSER_MAPPINGS,
      { BROWSER: browserID },
      results =>
        results
          // Filter out all the entries with an extension ID not in the list
          // passed to the method.
          .filter(entry => {
            if (unmatchedExtensionIDs.has(entry.extension_id)) {
              unmatchedExtensionIDs.delete(entry.extension_id);
              matchedExtensionIDs.add(entry.extension_id);
              return true;
            }
            return false;
          })
          // Return the add-on ID (stored as `guid` on AMO).
          .map(entry => entry.addon_guid)
    );

    if (!addonIds.length) {
      return {
        addons: [],
        matchedIDs: [],
        unmatchedIDs: [...unmatchedExtensionIDs],
      };
    }

    return {
      addons: await this.getAddonsByIDs(addonIds),
      matchedIDs: [...matchedExtensionIDs],
      unmatchedIDs: [...unmatchedExtensionIDs],
    };
  },

  /**
   * Asynchronously add add-ons to the cache corresponding to the specified
   * ids. If caching is disabled, the cache is unchanged.
   *
   * @param  aIds
   *         The array of add-on ids to add to the cache
   * @returns {Array<AddonSearchResult>} Add-ons to add to the cache.
   */
  async cacheAddons(aIds) {
    logger.debug(
      "cacheAddons: enabled " + this.cacheEnabled + " IDs " + aIds.toSource()
    );
    if (!this.cacheEnabled) {
      return [];
    }

    let ids = await getAddonsToCache(aIds);

    // If there are no add-ons to cache, act as if caching is disabled
    if (!ids.length) {
      return [];
    }

    let addons = [];
    try {
      addons = await this.getAddonsByIDs(ids);
    } catch (err) {
      logger.error(`Error in addon metadata check: ${err.message}`);
    }
    if (addons.length) {
      await AddonDatabase.update(addons);
    }
    return addons;
  },

  /**
   * Get all installed addons from the AddonManager singleton.
   *
   * @return Promise{array<AddonWrapper>} Resolves to an array of AddonWrapper instances.
   */
  _getAllInstalledAddons() {
    return lazy.AddonManager.getAllAddons();
  },

  /**
   * Performs the periodic background update check.
   *
   * In Firefox Desktop builds, the background update check is triggered on a
   * daily basis as part of the AOM background update check and registered
   * from: `toolkit/mozapps/extensions/extensions.manifest`
   *
   * In GeckoView builds, add-ons are checked for updates individually. The
   * `AddonRepository.backgroundUpdateCheck()` method is called by the
   * `updateWebExtension()` method defined in `GeckoViewWebExtensions.sys.mjs`
   * but only when `AddonRepository.isMetadataStale()` returns true.
   *
   * @return Promise{null} Resolves when the metadata update is complete.
   */
  async backgroundUpdateCheck() {
    let shutter = (async () => {
      if (this.appIsShuttingDown) {
        logger.debug(
          "Returning earlier from backgroundUpdateCheck, shutdown already in progress"
        );
        return;
      }

      let allAddons = await this._getAllInstalledAddons();

      // Completely remove cache if caching is not enabled
      if (!this.cacheEnabled) {
        logger.debug("Clearing cache because it is disabled");
        await this._clearCache();
        return;
      }

      let ids = allAddons.map(a => a.id);
      logger.debug("Repopulate add-on cache with " + ids.toSource());

      let addonsToCache = await getAddonsToCache(ids);

      // Completely remove cache if there are no add-ons to cache
      if (!addonsToCache.length) {
        logger.debug("Clearing cache because 0 add-ons were requested");
        await this._clearCache();
        return;
      }

      let addons;
      try {
        addons = await this.getAddonsByIDs(addonsToCache);
      } catch (err) {
        // This is likely to happen if the server is unreachable, e.g. when
        // there is no network connectivity.
        logger.error(`Error in addon metadata lookup: ${err.message}`);
        // Return now to avoid calling repopulate with an empty array;
        // doing so would clear the cache.
        return;
      }

      AddonDatabase.repopulate(addons);

      // Always call AddonManager updateAddonRepositoryData after we refill the cache
      await lazy.AddonManagerPrivate.updateAddonRepositoryData();
    })();
    lazy.AddonManager.beforeShutdown.addBlocker(
      "AddonRepository Background Updater",
      shutter
    );
    await shutter;
    lazy.AddonManager.beforeShutdown.removeBlocker(shutter);
  },

  /**
   * Creates an AddonSearchResult by parsing an entry from the AMO API.
   *
   * @param  aEntry
   *         An entry from the AMO search API to parse.
   * @return Result object containing the parsed AddonSearchResult
   */
  _parseAddon(aEntry) {
    let addon = new AddonSearchResult(aEntry.guid);

    addon.name = aEntry.name;
    if (typeof aEntry.current_version == "object") {
      addon.version = String(aEntry.current_version.version);
      if (Array.isArray(aEntry.current_version.files)) {
        for (let file of aEntry.current_version.files) {
          if (file.platform == "all" || file.platform == lazy.PLATFORM) {
            if (file.url) {
              addon.sourceURI = lazy.NetUtil.newURI(file.url);
            }
            break;
          }
        }
      }
    }
    addon.homepageURL = aEntry.homepage;
    addon.supportURL = aEntry.support_url;
    addon.amoListingURL = aEntry.url;

    addon.description = convertHTMLToPlainText(aEntry.summary);
    addon.fullDescription = convertHTMLToPlainText(aEntry.description);

    addon.weeklyDownloads = aEntry.weekly_downloads;

    switch (aEntry.type) {
      case "persona":
      case "statictheme":
        addon.type = "theme";
        break;

      case "language":
        addon.type = "locale";
        break;

      default:
        addon.type = aEntry.type;
        break;
    }

    if (Array.isArray(aEntry.authors)) {
      let authors = aEntry.authors.map(
        author =>
          new lazy.AddonManagerPrivate.AddonAuthor(author.name, author.url)
      );
      if (authors.length) {
        addon.creator = authors[0];
        addon.developers = authors.slice(1);
      }
    }

    if (typeof aEntry.previews == "object") {
      addon.screenshots = aEntry.previews.map(shot => {
        let safeSize = orig =>
          Array.isArray(orig) && orig.length >= 2 ? orig : [null, null];
        let imageSize = safeSize(shot.image_size);
        let thumbSize = safeSize(shot.thumbnail_size);
        return new lazy.AddonManagerPrivate.AddonScreenshot(
          shot.image_url,
          imageSize[0],
          imageSize[1],
          shot.thumbnail_url,
          thumbSize[0],
          thumbSize[1],
          shot.caption
        );
      });
    }

    addon.contributionURL = aEntry.contributions_url;

    if (typeof aEntry.ratings == "object") {
      addon.averageRating = Math.min(5, aEntry.ratings.average);
      addon.reviewCount = aEntry.ratings.text_count;
    }

    addon.reviewURL = aEntry.ratings_url;
    if (aEntry.last_updated) {
      addon.updateDate = new Date(aEntry.last_updated);
    }

    addon.icons = aEntry.icons || {};

    return addon;
  },

  // Create url from preference, returning null if preference does not exist
  _formatURLPref(aPreference, aSubstitutions = {}) {
    let url = Services.prefs.getCharPref(aPreference, "");
    if (!url) {
      logger.warn("_formatURLPref: Couldn't get pref: " + aPreference);
      return null;
    }

    url = url.replace(/%([A-Z_]+)%/g, function (aMatch, aKey) {
      return aKey in aSubstitutions
        ? encodeURIComponent(aSubstitutions[aKey])
        : aMatch;
    });

    return Services.urlFormatter.formatURL(url);
  },

  flush() {
    return AddonDatabase.flush();
  },

  async getAvailableLangpacks() {
    // This should be the API endpoint documented at:
    // http://addons-server.readthedocs.io/en/latest/topics/api/addons.html#language-tools
    let url = this._formatURLPref(PREF_GET_LANGPACKS);

    let response = await fetch(url, { credentials: "omit" });
    if (!response.ok) {
      throw new Error("fetching available language packs failed");
    }

    let data = await response.json();

    let result = [];
    for (let entry of data.results) {
      if (
        !entry.current_compatible_version ||
        !entry.current_compatible_version.files
      ) {
        continue;
      }

      for (let file of entry.current_compatible_version.files) {
        if (
          file.platform == "all" ||
          file.platform == Services.appinfo.OS.toLowerCase()
        ) {
          result.push({
            id: entry.guid,
            hash: file.hash,
            target_locale: entry.target_locale,
            url: file.url,
            version: entry.current_compatible_version.version,
          });
        }
      }
    }

    return result;
  },
};

var AddonDatabase = {
  connectionPromise: null,
  _loaded: false,
  _saveTask: null,
  _blockerAdded: false,

  // the in-memory database
  DB: BLANK_DB(),

  /**
   * A getter to retrieve the path to the DB
   */
  get jsonFile() {
    return PathUtils.join(
      Services.dirsvc.get("ProfD", Ci.nsIFile).path,
      FILE_DATABASE
    );
  },

  /**
   * Asynchronously opens a new connection to the database file.
   *
   * @return {Promise} a promise that resolves to the database.
   */
  openConnection() {
    if (!this.connectionPromise) {
      this.connectionPromise = (async () => {
        let inputDB, schema;

        try {
          let data = await IOUtils.readUTF8(this.jsonFile);
          inputDB = JSON.parse(data);

          if (
            !inputDB.hasOwnProperty("addons") ||
            !Array.isArray(inputDB.addons)
          ) {
            throw new Error("No addons array.");
          }

          if (!inputDB.hasOwnProperty("schema")) {
            throw new Error("No schema specified.");
          }

          schema = parseInt(inputDB.schema, 10);

          if (!Number.isInteger(schema) || schema < DB_MIN_JSON_SCHEMA) {
            throw new Error("Invalid schema value.");
          }
        } catch (e) {
          if (e.name == "NotFoundError") {
            logger.debug("No " + FILE_DATABASE + " found.");
          } else {
            logger.error(
              `Malformed ${FILE_DATABASE}: ${e} - resetting to empty`
            );
          }

          // Create a blank addons.json file
          this.save();

          Services.prefs.setIntPref(PREF_GETADDONS_DB_SCHEMA, DB_SCHEMA);
          this._loaded = true;
          return this.DB;
        }

        Services.prefs.setIntPref(PREF_GETADDONS_DB_SCHEMA, DB_SCHEMA);

        // Convert the addon objects as necessary
        // and store them in our in-memory copy of the database.
        for (let addon of inputDB.addons) {
          let id = addon.id;

          let entry = this._parseAddon(addon);
          this.DB.addons.set(id, entry);
        }

        this._loaded = true;
        return this.DB;
      })();
    }

    return this.connectionPromise;
  },

  /**
   * Asynchronously shuts down the database connection and releases all
   * cached objects
   *
   * @param  aCallback
   *         An optional callback to call once complete
   * @param  aSkipFlush
   *         An optional boolean to skip flushing data to disk. Useful
   *         when the database is going to be deleted afterwards.
   */
  shutdown(aSkipFlush) {
    if (!this.connectionPromise) {
      return Promise.resolve();
    }

    this.connectionPromise = null;
    this._loaded = false;

    if (aSkipFlush) {
      return Promise.resolve();
    }

    return this.flush();
  },

  /**
   * Asynchronously deletes the database, shutting down the connection
   * first if initialized
   *
   * @param  aCallback
   *         An optional callback to call once complete
   * @return Promise{null} resolves when the database has been deleted
   */
  delete(aCallback) {
    this.DB = BLANK_DB();

    if (this._saveTask) {
      this._saveTask.disarm();
      this._saveTask = null;
    }

    // shutdown(true) never rejects
    this._deleting = this.shutdown(true)
      .then(() => IOUtils.remove(this.jsonFile))
      .catch(error =>
        logger.error(
          "Unable to delete Addon Repository file " + this.jsonFile,
          error
        )
      )
      .then(() => (this._deleting = null))
      .then(aCallback);

    return this._deleting;
  },

  async _saveNow() {
    let json = {
      schema: this.DB.schema,
      addons: Array.from(this.DB.addons.values()),
    };

    await IOUtils.writeUTF8(this.jsonFile, JSON.stringify(json), {
      tmpPath: `${this.jsonFile}.tmp`,
    });
  },

  save() {
    if (!this._saveTask) {
      this._saveTask = new lazy.DeferredTask(
        () => this._saveNow(),
        DB_BATCH_TIMEOUT_MS
      );

      if (!this._blockerAdded) {
        lazy.AsyncShutdown.profileBeforeChange.addBlocker(
          "Flush AddonRepository",
          () => this.flush()
        );
        this._blockerAdded = true;
      }
    }
    this._saveTask.arm();
  },

  /**
   * Flush any pending I/O on the addons.json file
   *
   * @returns {Promise<null>}
   *          Resolves when the pending I/O (writing out or deleting
   *          addons.json) completes
   */
  flush() {
    if (this._deleting) {
      return this._deleting;
    }

    if (this._saveTask) {
      let promise = this._saveTask.finalize();
      this._saveTask = null;
      return promise;
    }

    return Promise.resolve();
  },

  /**
   * Get an individual addon entry from the in-memory cache.
   * Note: calling this function before the database is read will
   * return undefined.
   *
   * @param {string} aId The id of the addon to retrieve.
   */
  getAddon(aId) {
    return this.DB.addons.get(aId);
  },

  /**
   * Asynchronously repopulates the database so it only contains the
   * specified add-ons
   *
   * @param {Array<AddonSearchResult>} aAddons
   *              Add-ons to repopulate the database with.
   */
  repopulate(aAddons) {
    this.DB = BLANK_DB();
    this._update(aAddons);

    let now = Math.round(Date.now() / 1000);
    logger.debug(
      "Cache repopulated, setting " + PREF_METADATA_LASTUPDATE + " to " + now
    );
    Services.prefs.setIntPref(PREF_METADATA_LASTUPDATE, now);
  },

  /**
   * Asynchronously insert new addons into the database.
   *
   * @param {Array<AddonSearchResult>} aAddons
   *              Add-ons to insert/update in the database
   */
  async update(aAddons) {
    await this.openConnection();

    this._update(aAddons);
  },

  /**
   * Merge the given addons into the database.
   *
   * @param {Array<AddonSearchResult>} aAddons
   *              Add-ons to insert/update in the database
   */
  _update(aAddons) {
    for (let addon of aAddons) {
      this.DB.addons.set(addon.id, this._parseAddon(addon));
    }

    this.save();
  },

  /**
   * Creates an AddonSearchResult by parsing an object structure
   * retrieved from the DB JSON representation.
   *
   * @param  aObj
   *         The object to parse
   * @return Returns an AddonSearchResult object.
   */
  _parseAddon(aObj) {
    if (aObj instanceof AddonSearchResult) {
      return aObj;
    }

    let id = aObj.id;
    if (!aObj.id) {
      return null;
    }

    let addon = new AddonSearchResult(id);

    for (let expectedProperty of Object.keys(AddonSearchResult.prototype)) {
      if (
        !(expectedProperty in aObj) ||
        typeof aObj[expectedProperty] === "function"
      ) {
        continue;
      }

      let value = aObj[expectedProperty];

      try {
        switch (expectedProperty) {
          case "sourceURI":
            addon.sourceURI = value ? lazy.NetUtil.newURI(value) : null;
            break;

          case "creator":
            addon.creator = value ? this._makeDeveloper(value) : null;
            break;

          case "updateDate":
            addon.updateDate = value ? new Date(value) : null;
            break;

          case "developers":
            if (!addon.developers) {
              addon.developers = [];
            }
            for (let developer of value) {
              addon.developers.push(this._makeDeveloper(developer));
            }
            break;

          case "screenshots":
            if (!addon.screenshots) {
              addon.screenshots = [];
            }
            for (let screenshot of value) {
              addon.screenshots.push(this._makeScreenshot(screenshot));
            }
            break;

          case "icons":
            if (!addon.icons) {
              addon.icons = {};
            }
            for (let size of Object.keys(aObj.icons)) {
              addon.icons[size] = aObj.icons[size];
            }
            break;

          case "iconURL":
            break;

          default:
            addon[expectedProperty] = value;
        }
      } catch (ex) {
        logger.warn(
          "Error in parsing property value for " + expectedProperty + " | " + ex
        );
      }

      // delete property from obj to indicate we've already
      // handled it. The remaining public properties will
      // be stored separately and just passed through to
      // be written back to the DB.
      delete aObj[expectedProperty];
    }

    // Copy remaining properties to a separate object
    // to prevent accidental access on downgraded versions.
    // The properties will be merged in the same object
    // prior to being written back through toJSON.
    for (let remainingProperty of Object.keys(aObj)) {
      switch (typeof aObj[remainingProperty]) {
        case "boolean":
        case "number":
        case "string":
        case "object":
          // these types are accepted
          break;
        default:
          continue;
      }

      if (!remainingProperty.startsWith("_")) {
        addon._unsupportedProperties[remainingProperty] =
          aObj[remainingProperty];
      }
    }

    return addon;
  },

  /**
   * Make a developer object from a vanilla
   * JS object from the JSON database
   *
   * @param  aObj
   *         The JS object to use
   * @return The created developer
   */
  _makeDeveloper(aObj) {
    let name = aObj.name;
    let url = aObj.url;
    return new lazy.AddonManagerPrivate.AddonAuthor(name, url);
  },

  /**
   * Make a screenshot object from a vanilla
   * JS object from the JSON database
   *
   * @param  aObj
   *         The JS object to use
   * @return The created screenshot
   */
  _makeScreenshot(aObj) {
    let url = aObj.url;
    let width = aObj.width;
    let height = aObj.height;
    let thumbnailURL = aObj.thumbnailURL;
    let thumbnailWidth = aObj.thumbnailWidth;
    let thumbnailHeight = aObj.thumbnailHeight;
    let caption = aObj.caption;
    return new lazy.AddonManagerPrivate.AddonScreenshot(
      url,
      width,
      height,
      thumbnailURL,
      thumbnailWidth,
      thumbnailHeight,
      caption
    );
  },
};
