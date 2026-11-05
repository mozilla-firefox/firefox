/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

/**
 * @typedef {import("./OpenSearchParser.sys.mjs").OpenSearchProperties} OpenSearchProperties
 */

import {
  EngineURL,
  SearchEngine,
} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  loadAndParseOpenSearchEngine:
    "moz-src:///toolkit/components/search/OpenSearchLoader.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  logConsole: () =>
    console.createInstance({
      logModule: "OpenSearchEngine",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

// The default engine update interval, in days. This is only used if an engine
// specifies an updateURL, but not an updateInterval.
const OPENSEARCH_DEFAULT_UPDATE_INTERVAL = 7;

/**
 * OpenSearchEngine represents an OpenSearch base search engine.
 */
export class OpenSearchEngine extends SearchEngine {
  // The data describing the engine, in the form of an XML document element.
  _data = null;
  // The number of days between update checks for new versions
  _updateInterval = null;
  // The url to check at for a new update
  _updateURL = null;

  /**
   * Creates a OpenSearchEngine.
   *
   * @param {object} [options]
   *   The options object
   * @param {object} [options.json]
   *   An object that represents the saved JSON settings for the engine.
   * @param {OpenSearchProperties} [options.engineData]
   *   The engine data for this search engine that will have been loaded via
   *   `OpenSearchLoader`.
   * @param {string} [options.faviconURL]
   *   The website favicon, to be used if the engine data hasn't specified an
   *   icon.
   * @param {object} [options.originAttributes]
   *   The origin attributes to use to download additional resources.
   */
  constructor(options = {}) {
    super({
      loadPath:
        options.json?._loadPath ??
        OpenSearchEngine.getAnonymizedLoadPath(
          lazy.SearchUtils.sanitizeName(options.engineData.name),
          options.engineData.installURL
        ),
    });

    if (options.faviconURL) {
      this._setIcon(options.faviconURL, {
        override: false,
        originAttributes: options.originAttributes,
      }).catch(e =>
        lazy.logConsole.error(
          `Error while setting icon for search engine ${options.engineData.name}:`,
          e.message
        )
      );
    }

    if (options.engineData) {
      this.#setEngineData(options.engineData, options.originAttributes);

      // As this is a new engine, we must set the verification hash for the load
      // path set in the constructor.
      this.setAttr(
        "loadPathHash",
        lazy.SearchUtils.getVerificationHash(this._loadPath)
      );

      if (this.hasUpdates) {
        this.#setNextUpdateTime();
      }
    } else {
      this._initWithJSON(options.json);
      this._updateInterval = options.json._updateInterval ?? null;
      this._updateURL = options.json._updateURL ?? null;
    }
  }

  /**
   * Creates a JavaScript object that represents this engine.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    let json = super.toJSON();
    json._updateInterval = this._updateInterval;
    json._updateURL = this._updateURL;
    return json;
  }

  /**
   * Determines if this search engine has updates url.
   *
   * @returns {boolean}
   *   Returns true if this search engine may update itself.
   */
  get hasUpdates() {
    // Whether or not the engine has an update URL
    let selfURL = this.getURLOfType(
      lazy.SearchUtils.URL_TYPE.OPENSEARCH,
      "self"
    );
    return !!(this._updateURL || selfURL);
  }

  /**
   * Returns the engine's updateURI if it exists and returns null otherwise
   *
   * @returns {?nsIURI}
   */
  get updateURI() {
    let updateURL = this.getURLOfType(lazy.SearchUtils.URL_TYPE.OPENSEARCH);
    let updateURI =
      updateURL && updateURL._hasRelation("self")
        ? updateURL.getSubmission("", this.queryCharset, "default").uri
        : lazy.SearchUtils.makeURI(this._updateURL);
    return updateURI;
  }

  /**
   * Considers if this engine needs to be updated, and updates it if necessary.
   */
  async maybeUpdate() {
    if (!this.hasUpdates) {
      return;
    }

    let currentTime = Date.now();

    let expireTime = this.getAttr("updateexpir");

    if (!expireTime || !(expireTime <= currentTime)) {
      lazy.logConsole.debug(this.name, "Skipping update, not expired yet.");
      return;
    }

    await this.#update();

    this.#setNextUpdateTime();
  }

  /**
   * Updates the OpenSearch engine details from the server.
   */
  async #update() {
    let updateURI = this.updateURI;
    if (updateURI) {
      let data = await lazy.loadAndParseOpenSearchEngine(
        updateURI,
        this.getAttr("updatelastmodified")
      );

      this.#setEngineData(data);

      lazy.SearchUtils.notifyAction(
        this,
        lazy.SearchUtils.MODIFIED_TYPE.CHANGED
      );

      // Keep track of the last modified date, so that we can make conditional
      // server requests for future updates.
      this.setAttr("updatelastmodified", new Date().toUTCString());
    }
  }

  /**
   * Sets the data for this engine based on the OpenSearch properties.
   *
   * @param {OpenSearchProperties} data
   *   The OpenSearch data.
   * @param {object} originAttributes
   *   The origin attributes for any additional downloads
   */
  #setEngineData(data, originAttributes) {
    let name = data.name.trim();

    this._name = name;
    this._queryCharset = data.queryCharset ?? "UTF-8";
    if (data.searchForm) {
      try {
        let searchFormUrl = new EngineURL({
          type: lazy.SearchUtils.URL_TYPE.SEARCH_FORM,
          template: data.searchForm,
        });
        this._urls.push(searchFormUrl);
      } catch (ex) {
        throw new Error(
          `Failed to add ${data.searchForm} as a searchForm URL`,
          { cause: ex }
        );
      }
    }

    for (let url of data.urls) {
      // Some Mozilla provided opensearch engines used to specify their searchForm
      // through a Url with rel="searchform". We add these as URLs with type searchform.
      if (url.rels.includes("searchform")) {
        let searchFormURL;
        try {
          searchFormURL = new EngineURL({
            type: lazy.SearchUtils.URL_TYPE.SEARCH_FORM,
            template: url.template,
          });
        } catch (ex) {
          throw new Error(`Failed to add ${url.template} as an Engine URL`, {
            cause: ex,
          });
        }
        this.#addParamsToUrl(searchFormURL, url.params);
        this._urls.push(searchFormURL);
      }

      let engineURL;
      try {
        engineURL = new EngineURL(url);
      } catch (ex) {
        throw new Error(`Failed to add ${url.template} as an Engine URL`, {
          cause: ex,
        });
      }

      let nonSearchformRels = url.rels.filter(rel => rel != "searchform");
      if (nonSearchformRels.length) {
        engineURL.rels = nonSearchformRels;
      }

      this.#addParamsToUrl(engineURL, url.params);
      this._urls.push(engineURL);
    }

    for (let image of data.images) {
      this._setIcon(image.url, { size: image.size, originAttributes }).catch(
        e =>
          lazy.logConsole.error(
            `Error while setting icon for search engine ${data.name}:`,
            e.message
          )
      );
    }
  }

  /**
   * Helper method to add all params to the given EngineURL,
   * ignoring those params with missing name or value.
   *
   * @param {EngineURL} engineURL the EngineURL to add the params to.
   * @param {Array} params param objects with name and value properties.
   */
  #addParamsToUrl(engineURL, params) {
    for (let param of params) {
      try {
        engineURL.addParam(param.name, param.value);
      } catch (ex) {
        // Ignore failure
        lazy.logConsole.error("OpenSearch url has an invalid param", param);
      }
    }
  }

  /**
   * Sets the next update time for this engine.
   */
  #setNextUpdateTime() {
    var interval = this._updateInterval || OPENSEARCH_DEFAULT_UPDATE_INTERVAL;
    var milliseconds = interval * 86400000; // |interval| is in days
    this.setAttr("updateexpir", Date.now() + milliseconds);
  }

  /**
   * This indicates where we found the .xml file to load the engine,
   * and attempts to hide user-identifiable data (such as username).
   *
   * @param {string} sanitizedName
   *   The sanitized name of the engine.
   * @param {nsIURI} uri
   *   The uri the engine was loaded from.
   * @returns {string}
   *   A load path with reduced data.
   */
  static getAnonymizedLoadPath(sanitizedName, uri) {
    return `[${uri.scheme}]${uri.host}/${sanitizedName}.xml`;
  }
}
