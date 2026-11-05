/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

import { SearchEngine } from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  logConsole: () =>
    console.createInstance({
      logModule: "AddonSearchEngine",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

/**
 * AddonSearchEngine represents a search engine defined by an add-on.
 */
export class AddonSearchEngine extends SearchEngine {
  /**
   * Creates a AddonSearchEngine.
   *
   * @param {object} options
   *   The options object
   * @param {object} [options.details]
   *   An object that simulates the manifest object from a WebExtension.
   * @param {object} [options.json]
   *   An object that represents the saved JSON settings for the engine.
   */
  constructor({ details, json } = {}) {
    let extensionId =
      details?.extensionID ?? json.extensionID ?? json._extensionID;
    // Historically, configuration based engines used to be backed by add-ons
    // and they would sometimes have a specific locale enforced. For cases where
    // it wasn't enforced, the locale was recorded as ``default``. We hard-code
    // that here, as we have kept the same identifier format. We could potentially
    // add a migration step in future and drop the ``default``.
    let id = extensionId + "default";

    super({
      loadPath: "[addon]" + extensionId,
      id,
    });

    this.#extensionID = extensionId;

    if (json) {
      this._initWithJSON(json);
    }
  }

  _initWithJSON(json) {
    super._initWithJSON(json);
    this.#extensionID = json.extensionID || json._extensionID || null;
  }

  /**
   * Call to initalise an instance with extension details. Does not need to be
   * called if json has been passed to the constructor.
   *
   * @param {object} options
   *   The options object.
   * @param {Extension} [options.extension]
   *   The extension object representing the add-on.
   * @param {object} [options.settings]
   *   The saved settings for the user.
   */
  async init({ extension, settings } = {}) {
    let { baseURI, manifest } =
      await this.#getExtensionDetailsForLocale(extension);

    this.#initFromManifest(baseURI, manifest);
    this._loadSettings(settings);
  }

  /**
   * Manages updates to this engine.
   *
   * @param {object} options
   *   The options object.
   * @param {object} [options.extension]
   *   The extension associated with this search engine, if known.
   */
  async update({ extension } = {}) {
    let { baseURI, manifest } =
      await this.#getExtensionDetailsForLocale(extension);

    let originalName = this.name;
    let name = manifest.chrome_settings_overrides.search_provider.name.trim();
    if (originalName != name && lazy.SearchService.getEngineByName(name)) {
      throw new Error("Can't upgrade to the same name as an existing engine");
    }

    this.#updateFromManifest(baseURI, manifest);
  }

  /**
   * Creates a JavaScript object that represents this engine.
   *
   * @returns {object}
   *   An object suitable for serialization as JSON.
   */
  toJSON() {
    let json = super.toJSON();
    json._extensionID = this.#extensionID;
    return json;
  }

  /**
   * The extension ID that this search engine is associated with.
   */
  get extensionID() {
    return this.#extensionID;
  }

  /**
   * Checks to see if this engine's settings are in sync with what the add-on
   * manager has, and reports the results to telemetry.
   */
  async checkAndReportIfSettingsValid() {
    let addon = await lazy.AddonManager.getAddonByID(this.#extensionID);

    if (!addon) {
      lazy.logConsole.debug(
        `Add-on ${this.#extensionID} for search engine ${this.name} is not installed!`
      );
      Glean.browserSearchinit.engineInvalidWebextension[this.#extensionID].set(
        1
      );
    } else if (!addon.isActive) {
      lazy.logConsole.debug(
        `Add-on ${this.#extensionID} for search engine ${this.name} is not active!`
      );
      Glean.browserSearchinit.engineInvalidWebextension[this.#extensionID].set(
        2
      );
    } else {
      let policy = await AddonSearchEngine.getWebExtensionPolicy(
        this.#extensionID
      );
      let providerSettings =
        policy.extension.manifest?.chrome_settings_overrides?.search_provider;

      if (!providerSettings) {
        lazy.logConsole.debug(
          `Add-on ${this.#extensionID} for search engine ${this.name} no longer has an engine defined`
        );
        Glean.browserSearchinit.engineInvalidWebextension[
          this.#extensionID
        ].set(4);
      } else if (this.name != providerSettings.name) {
        lazy.logConsole.debug(
          `Add-on ${this.#extensionID} for search engine ${this.name} has a different name!`
        );
        Glean.browserSearchinit.engineInvalidWebextension[
          this.#extensionID
        ].set(5);
      } else if (!this.checkSearchUrlMatchesManifest(providerSettings)) {
        lazy.logConsole.debug(
          `Add-on ${this.#extensionID} for search engine ${this.name} has out-of-date manifest!`
        );
        Glean.browserSearchinit.engineInvalidWebextension[
          this.#extensionID
        ].set(6);
      }
    }
  }

  /**
   * Initializes the engine based on the manifest and other values.
   *
   * @param {nsIURI} extensionBaseURI
   *   The Base URI of the WebExtension.
   * @param {object} manifest
   *   An object representing the WebExtensions' manifest.
   */
  #initFromManifest(extensionBaseURI, manifest) {
    let searchProvider = manifest.chrome_settings_overrides.search_provider;

    // Set the main icon URL for the engine.
    let iconURL = searchProvider.favicon_url;

    if (!iconURL) {
      iconURL =
        manifest.icons &&
        extensionBaseURI.resolve(
          lazy.ExtensionParent.IconDetails.getPreferredIcon(manifest.icons).icon
        );
    }

    // Record other icons that the WebExtension has.
    if (manifest.icons) {
      for (let [size, icon] of Object.entries(manifest.icons)) {
        this._addIconToMap(
          extensionBaseURI.resolve(icon),
          parseInt(size),
          false
        );
      }
    }

    this._initWithDetails({
      ...searchProvider,
      iconURL,
    });
  }

  /**
   * Update this engine based on new manifest, used during
   * webextension upgrades.
   *
   * @param {nsIURI} extensionBaseURI
   *   The Base URI of the WebExtension.
   * @param {object} manifest
   *   An object representing the WebExtensions' manifest.
   */
  #updateFromManifest(extensionBaseURI, manifest) {
    this._urls = [];
    this._iconMapObj = null;
    this.#initFromManifest(extensionBaseURI, manifest);
    lazy.SearchUtils.notifyAction(this, lazy.SearchUtils.MODIFIED_TYPE.CHANGED);
  }

  /**
   * Get the localized manifest from the WebExtension for the given locale or
   * manifest default locale.
   *
   * @param {object} [extension]
   *   The extension to get the manifest from.
   * @returns {Promise<object>}
   *   The loaded manifest.
   */
  async #getExtensionDetailsForLocale(extension) {
    // If we haven't been passed an extension object, then go and find it.
    if (!extension) {
      extension = (
        await AddonSearchEngine.getWebExtensionPolicy(this.#extensionID)
      ).extension;
    }

    let manifest = extension.manifest;

    // We have to simulate the add-on manager code for loading the correct locale.
    // We do this, as in the case of a live language switch, the add-on manager
    // may not have yet reloaded the extension, and there's no way for us to
    // listen for that reload to complete.
    // See also https://bugzilla.mozilla.org/show_bug.cgi?id=1781768#c3 for
    // more background.
    let localeToLoad = Services.locale.negotiateLanguages(
      Services.locale.appLocalesAsBCP47,
      [...extension.localeData.locales.keys()],
      manifest.default_locale
    )[0];

    if (localeToLoad) {
      manifest = await extension.getLocalizedManifest(localeToLoad);
    }
    return { baseURI: extension.baseURI, manifest };
  }

  /**
   * Gets the WebExtensionPolicy for an add-on.
   *
   * @param {string} id
   *   The WebExtension id.
   * @returns {Promise<WebExtensionPolicy>}
   */
  static async getWebExtensionPolicy(id) {
    let policy = WebExtensionPolicy.getByID(id);
    if (!policy) {
      throw new Error("Could not find add-on information");
    }
    // On startup the extension may have not finished parsing the
    // manifest, wait for that here.
    await policy.readyPromise;
    return policy;
  }

  /**
   * @type {string}
   *   The extension ID that this search engine is associated with.
   */
  #extensionID;
}
