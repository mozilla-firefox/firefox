/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  logger: () =>
    lazy.UrlbarShared.getLogger({ prefix: "UrlbarSearchTermsPersistence" }),
});

/**
 * @import {RemoteSettingsClient} from "resource://services-settings/RemoteSettingsClient.sys.mjs"
 */

/**
 * @typedef {object} PersistedTermsProviderInfo
 * @property {string} providerId
 *   The search engine provider id associated with the persisted terms.
 * @property {RegExp} [searchPageRegexp]
 *   The regular expression for determining if the search page URL matches.
 * @property {{key: string, values: string[], canBeMissing: boolean}[]} includeParams
 *   The parameters that should be included in determining if the search page URL matches.
 * @property {{key: string, values: string[]}[]} excludeParams
 *   The parameters that should be excluded in determining if the search page URL matches.
 */

const URLBAR_PERSISTENCE_SETTINGS_KEY = "urlbar-persisted-search-terms";

/**
 * Provides utilities to manage and validate search terms persistence in the URL
 * bar. This class is designed to handle the identification of default search
 * engine results pages (SERPs), retrieval of search terms, and validation of
 * conditions for persisting search terms based on predefined provider
 * information.
 */
class _UrlbarSearchTermsPersistence {
  // Whether or not this class is initialised.
  #initialized = false;

  // The original provider information, mainly used for tests.
  #originalProviderInfo = [];

  /**
   * The current search provider info.
   *
   * @type {PersistedTermsProviderInfo[]}
   */
  #searchProviderInfo = [];

  /**
   * An instance of remote settings that is used to access the provider info.
   *
   * @type {RemoteSettingsClient}
   */
  #urlbarSearchTermsPersistenceSettings;

  // Callback used when syncing Urlbar Search Terms Persistence config settings.
  #urlbarSearchTermsPersistenceSettingsSync;

  async init() {
    if (this.#initialized) {
      return;
    }

    this.#urlbarSearchTermsPersistenceSettings = lazy.RemoteSettings(
      URLBAR_PERSISTENCE_SETTINGS_KEY
    );
    let rawProviderInfo = [];
    try {
      rawProviderInfo = await this.#urlbarSearchTermsPersistenceSettings.get();
    } catch (ex) {
      lazy.logger.error("Could not get settings:", ex);
    }

    this.#urlbarSearchTermsPersistenceSettingsSync = event =>
      this.#onSettingsSync(event);
    this.#urlbarSearchTermsPersistenceSettings.on(
      "sync",
      this.#urlbarSearchTermsPersistenceSettingsSync
    );

    this.#originalProviderInfo = rawProviderInfo;
    this.#setSearchProviderInfo(rawProviderInfo);

    this.#initialized = true;
  }

  uninit() {
    if (!this.#initialized) {
      return;
    }

    try {
      this.#urlbarSearchTermsPersistenceSettings.off(
        "sync",
        this.#urlbarSearchTermsPersistenceSettingsSync
      );
    } catch (ex) {
      lazy.logger.error(
        "Failed to shutdown UrlbarSearchTermsPersistence Remote Settings.",
        ex
      );
    }
    this.#urlbarSearchTermsPersistenceSettings = null;
    this.#urlbarSearchTermsPersistenceSettingsSync = null;

    this.#initialized = false;
  }

  getSearchProviderInfo() {
    return this.#searchProviderInfo;
  }

  /**
   * Test-only function, used to override the provider information, so that
   * unit tests can set it to easy to test values.
   *
   * @param {Array} providerInfo
   *   An array of provider information to set.
   */
  overrideSearchTermsPersistenceForTests(providerInfo) {
    let info = providerInfo ? providerInfo : this.#originalProviderInfo;
    this.#setSearchProviderInfo(info);
  }

  /**
   * Determines if the URIs represent an config search engine
   * results page (SERP) and retrieves the search terms used.
   *
   * @param {nsIURI} uri
   *   The primary URI that is checked to determine if it matches the expected
   *   structure of a default SERP.
   * @returns {string}
   *   The search terms used.
   *   Will return an empty string if it's not a default SERP, the search term
   *   looks too similar to a URL, the string exceeds the maximum characters,
   *   or the default engine hasn't been initialized.
   */
  getSearchTerm(uri) {
    if (!lazy.SearchService.hasSuccessfullyInitialized || !uri?.spec) {
      return "";
    }

    // Avoid inspecting URIs if they are non-http(s).
    if (!/^https?:\/\//.test(uri.spec)) {
      return "";
    }

    let searchTerm = "";

    // If we have a provider, we have specific rules for dealing and can
    // understand changes to params.
    let provider = this.#getProviderInfoForURL(uri.spec);
    if (provider) {
      let result = lazy.SearchService.parseSubmissionURL(uri.spec);
      if (
        !(result.engine instanceof lazy.ConfigSearchEngine) ||
        !this.isDefaultPage(uri, provider)
      ) {
        return "";
      }
      searchTerm = result.terms;
    } else {
      let result = lazy.SearchService.parseSubmissionURL(uri.spec);
      if (!(result.engine instanceof lazy.ConfigSearchEngine)) {
        return "";
      }
      searchTerm = result.engine.searchTermFromResult(uri);
    }

    if (!searchTerm || searchTerm.length > lazy.UrlbarShared.MAX_TEXT_LENGTH) {
      return "";
    }

    let searchTermWithSpacesRemoved = searchTerm.replaceAll(/\s/g, "");

    // Check if the search string uses a commonly used URL protocol. This
    // avoids doing a fixup if we already know it matches a URL. Additionally,
    // it ensures neither http:// nor https:// will appear by themselves in
    // UrlbarInput. This is important because http:// can be trimmed, which in
    // the Persisted Search Terms case, will cause the UrlbarInput to appear
    // blank.
    if (
      searchTermWithSpacesRemoved.startsWith("https://") ||
      searchTermWithSpacesRemoved.startsWith("http://")
    ) {
      return "";
    }

    // We pass the search term to URIFixup to determine if it could be
    // interpreted as a URL, including typos in the scheme and/or the domain
    // suffix. This is to prevent search terms from persisting in the Urlbar if
    // they look too similar to a URL, but still allow phrases with periods
    // that are unlikely to be a URL.
    try {
      let info = Services.uriFixup.getFixupURIInfo(
        searchTermWithSpacesRemoved,
        Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
          Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP
      );
      if (info.keywordAsSent) {
        return searchTerm;
      }
    } catch (e) {}

    return "";
  }

  shouldPersist(state, { uri, isSameDocument, userTypedValue, firstView }) {
    let persist = state.persist;
    if (!persist) {
      return false;
    }

    // Don't persist if there are no search terms to show.
    if (!persist.searchTerms) {
      return false;
    }

    // If there is a userTypedValue and it differs from the search terms, the
    // user must've modified the text.
    if (userTypedValue && userTypedValue !== persist.searchTerms) {
      return false;
    }

    // For some search engines, particularly single page applications, check
    // if the URL matches a default search results page as page changes will
    // occur within the same document.
    if (
      isSameDocument &&
      state.persist.provider &&
      !this.isDefaultPage(uri, state.persist.provider)
    ) {
      return false;
    }

    // The first page view will set the search mode but after that, the search
    // mode could differ. Since persisting the search guarantees the correct
    // search mode is shown, we don't want to undo changes the user could've
    // done, like removing/adding the search mode.
    if (
      !firstView &&
      !this.searchModeMatchesState(state.searchModes?.confirmed, state)
    ) {
      return false;
    }

    let origin, pathname;
    try {
      let url = URL.fromURI(uri);
      origin = url.origin;
      pathname = url.pathname;
    } catch (ex) {
      return false;
    }

    // Bug 1972464: Prevent search terms from persisting across different origin
    // or pathnames. This should be refactored later to be simplified.
    if (
      origin !== state.persist.origin ||
      pathname !== state.persist.pathname
    ) {
      return false;
    }

    return true;
  }

  // Resets and assigns initial values for Search Terms Persistence state.
  setPersistenceState(state, uri) {
    state.persist = {
      // Whether the engine that loaded the URI is the default search engine.
      isDefaultEngine: null,

      // Temporary until we resolve Bug 1972464 - refactor the architecture.
      origin: null,

      // The name of the engine that was used to load the URI.
      originalEngineName: null,

      // Temporary until we resolve Bug 1972464 - refactor the architecture.
      originalURI: null,

      // Temporary until we resolve Bug 1972464 - refactor the architecture.
      path: null,

      // The search provider associated with the URI. If one exists, it means
      // we have custom rules for this search provider to determine whether or
      // not the URI corresponds to a default search engine results page.
      provider: null,

      // The search string within the URI.
      searchTerms: "",

      // Whether the search terms should persist.
      shouldPersist: null,
    };

    let origin, pathname;
    try {
      let url = URL.fromURI(uri);
      origin = url.origin;
      pathname = url.pathname;
    } catch (ex) {
      return;
    }

    let searchTerms = this.getSearchTerm(uri);
    // Avoid setting state if either are missing.
    if (!searchTerms || !origin || !pathname) {
      return;
    }

    state.persist.origin = origin;
    state.persist.searchTerms = searchTerms;
    state.persist.pathname = pathname;
    state.persist.originalURI = uri;

    let provider = this.#getProviderInfoForURL(uri?.spec);
    // If we have specific Remote Settings defined providers for the URL,
    // it's because changing the page won't clear the search terms unless we
    // observe changes of the params in the URL.
    if (provider) {
      state.persist.provider = provider;
    }

    let result = this.#searchModeForUrl(uri.spec);
    state.persist.originalEngineName = result.engineName;
    state.persist.isDefaultEngine = result.isDefaultEngine;
  }

  /**
   * Determines if search mode is in alignment with the persisted
   * search state. Returns true in either of these cases:
   *
   * - The search mode engine is the same as the persisted engine.
   * - There's no search mode, but the persisted engine is a default engine.
   *
   * @param {object} searchMode
   *   The search mode for the address bar.
   * @param {object} state
   *   The address bar state associated with the browser.
   * @returns {boolean}
   */
  searchModeMatchesState(searchMode, state) {
    if (searchMode?.engineName === state.persist?.originalEngineName) {
      return true;
    }
    if (!searchMode && state.persist?.isDefaultEngine) {
      return true;
    }
    return false;
  }

  onSearchModeChanged(window) {
    let state = window.gURLBar.getBrowserState(window.gBrowser.selectedBrowser);
    if (!state?.persist) {
      return;
    }

    // Exit search terms persistence when search mode changes and it's not
    // consistent with the persisted engine.
    if (
      state.persist.shouldPersist &&
      !this.searchModeMatchesState(state.searchModes?.confirmed, state)
    ) {
      state.persist.shouldPersist = false;
      window.gURLBar.removeAttribute("persistsearchterms");
    }
  }

  async #onSettingsSync(event) {
    let current = event.data?.current;
    if (current) {
      lazy.logger.debug("Update provider info due to Remote Settings sync.");
      this.#originalProviderInfo = current;
      this.#setSearchProviderInfo(current);
    } else {
      lazy.logger.debug(
        "Ignoring Remote Settings sync data due to missing records."
      );
    }
    Services.obs.notifyObservers(null, "urlbar-persisted-search-terms-synced");
  }

  /**
   * Gets the search mode for a URL, if it matches an engine.
   *
   * @param {string} url
   */
  #searchModeForUrl(url) {
    // If there's no default engine, no engines are available.
    if (!lazy.SearchService.defaultEngine) {
      return null;
    }
    let result = lazy.SearchService.parseSubmissionURL(url);
    if (!(result.engine instanceof lazy.ConfigSearchEngine)) {
      return null;
    }
    return {
      engineName: result.engine.name,
      isDefaultEngine: result.engine === lazy.SearchService.defaultEngine,
    };
  }

  /**
   * Used to set the local version of the search provider information.
   * This automatically maps the regexps to RegExp objects so that
   * we don't have to create a new instance each time.
   *
   * @param {Array} providerInfo
   *   A raw array of provider information to set.
   */
  #setSearchProviderInfo(providerInfo) {
    this.#searchProviderInfo = providerInfo.map(provider => {
      let newProvider = {
        ...provider,
        searchPageRegexp: new RegExp(provider.searchPageRegexp),
      };
      return newProvider;
    });
  }

  /**
   * Searches for provider information for a given url.
   *
   * @param {string} url
   *   The url to match for a provider.
   * @returns {PersistedTermsProviderInfo|null}
   *   Returns the provider information.
   */
  #getProviderInfoForURL(url) {
    return this.#searchProviderInfo.find(info =>
      info.searchPageRegexp.test(url)
    );
  }

  /**
   * Determines whether the search terms in the provided URL should be persisted
   * based on whether we find it's a default web SERP.
   *
   * @param {nsIURI} currentURI
   *   The current URI
   * @param {PersistedTermsProviderInfo} provider
   *   An array of provider information
   * @returns {boolean}
   *   Returns true if the parameteres match, null otherwise.
   */
  isDefaultPage(currentURI, provider) {
    let { searchParams } = URL.fromURI(currentURI);
    if (!searchParams.size) {
      return false;
    }

    if (provider.includeParams?.length) {
      let foundMatch = false;
      for (let param of provider.includeParams) {
        // The param might not be present on page load.
        if (param.canBeMissing && !searchParams.has(param.key)) {
          foundMatch = true;
          break;
        }

        // If we didn't provide a specific param value,
        // the presence of the name is sufficient.
        if (searchParams.has(param.key) && !param.values?.length) {
          foundMatch = true;
          break;
        }

        let value = searchParams.get(param.key);
        // The param name and value must be present.
        if (value && param?.values.includes(value)) {
          foundMatch = true;
          break;
        }
      }
      if (!foundMatch) {
        return false;
      }
    }

    if (provider.excludeParams) {
      for (let param of provider.excludeParams) {
        let value = searchParams.get(param.key);
        // If we found a value for a key but didn't
        // provide a specific value to match.
        if (!param.values?.length && value) {
          return false;
        }
        // If we provided a value and it was present.
        if (param.values?.includes(value)) {
          return false;
        }
      }
    }
    return true;
  }
}

export var UrlbarSearchTermsPersistence = new _UrlbarSearchTermsPersistence();
