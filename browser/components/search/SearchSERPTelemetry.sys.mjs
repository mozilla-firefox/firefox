/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchUtils: "resource://gre/modules/SearchUtils.sys.mjs",
  SERPCategorization:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  SERPCategorizationRecorder:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  SERPCategorizationEventScheduler:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
});

// Exported for tests.
export const ADLINK_CHECK_TIMEOUT_MS = 1000;
// Unlike the standard adlink check, the timeout for single page apps is not
// based on a content event within the page, like DOMContentLoaded or load.
// Thus, we aim for a longer timeout to account for when the server might be
// slow to update the content on the page.
export const SPA_ADLINK_CHECK_TIMEOUT_MS = 2500;
export const TELEMETRY_SETTINGS_KEY = "search-telemetry-v2";

export const SEARCH_TELEMETRY_SHARED = {
  PROVIDER_INFO: "SearchTelemetry:ProviderInfo",
  LOAD_TIMEOUT: "SearchTelemetry:LoadTimeout",
  SPA_LOAD_TIMEOUT: "SearchTelemetry:SPALoadTimeout",
};

const impressionIdsWithoutEngagementsSet = new Set();

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "SearchTelemetry",
    maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
  });
});

export const SearchSERPTelemetryUtils = {
  ACTIONS: {
    CLICKED: "clicked",
    // specific to cookie banner
    CLICKED_ACCEPT: "clicked_accept",
    CLICKED_REJECT: "clicked_reject",
    CLICKED_MORE_OPTIONS: "clicked_more_options",
    EXPANDED: "expanded",
    SUBMITTED: "submitted",
  },
  COMPONENTS: {
    AD_CAROUSEL: "ad_carousel",
    AD_IMAGE_ROW: "ad_image_row",
    AD_LINK: "ad_link",
    AD_SIDEBAR: "ad_sidebar",
    AD_SITELINK: "ad_sitelink",
    AD_UNCATEGORIZED: "ad_uncategorized",
    COOKIE_BANNER: "cookie_banner",
    INCONTENT_SEARCHBOX: "incontent_searchbox",
    NON_ADS_LINK: "non_ads_link",
    REFINED_SEARCH_BUTTONS: "refined_search_buttons",
    SHOPPING_TAB: "shopping_tab",
  },
  ABANDONMENTS: {
    NAVIGATION: "navigation",
    TAB_CLOSE: "tab_close",
    WINDOW_CLOSE: "window_close",
  },
  INCONTENT_SOURCES: {
    OPENED_IN_NEW_TAB: "opened_in_new_tab",
    REFINE_ON_SERP: "follow_on_from_refine_on_SERP",
    SEARCHBOX: "follow_on_from_refine_on_incontent_search",
  },
};

const AD_COMPONENTS = [
  SearchSERPTelemetryUtils.COMPONENTS.AD_CAROUSEL,
  SearchSERPTelemetryUtils.COMPONENTS.AD_IMAGE_ROW,
  SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
  SearchSERPTelemetryUtils.COMPONENTS.AD_SIDEBAR,
  SearchSERPTelemetryUtils.COMPONENTS.AD_SITELINK,
  SearchSERPTelemetryUtils.COMPONENTS.AD_UNCATEGORIZED,
];

/**
 * TelemetryHandler is the main class handling Search Engine Result Page (SERP)
 * telemetry. It primarily deals with tracking of what pages are loaded into tabs.
 *
 * It handles the *in-content:sap* keys of the SEARCH_COUNTS histogram.
 */
class TelemetryHandler {
  // Whether or not this class is initialised.
  _initialized = false;

  // An instance of ContentHandler.
  _contentHandler;

  // The original provider information, mainly used for tests.
  _originalProviderInfo = null;

  // The current search provider info.
  _searchProviderInfo = null;

  // An instance of remote settings that is used to access the provider info.
  _telemetrySettings;

  // Callback used when syncing telemetry settings.
  #telemetrySettingsSync;

  // _browserInfoByURL is a map of tracked search urls to objects containing:
  // * {object} info
  //   the search provider information associated with the url.
  // * {WeakMap} browserTelemetryStateMap
  //   a weak map of browsers that have the url loaded, their ad report state,
  //   and their impression id.
  // * {integer} count
  //   a manual count of browsers logged.
  // We keep a weak map of browsers, in case we miss something on our counts
  // and cause a memory leak - worst case our map is slightly bigger than it
  // needs to be.
  // The manual count is because WeakMap doesn't give us size/length
  // information, but we want to know when we can clean up our associated
  // entry.
  _browserInfoByURL = new Map();

  // Browser objects mapped to the info in _browserInfoByURL.
  #browserToItemMap = new WeakMap();

  // An array of regular expressions that match urls that could be subframes
  // on SERPs.
  #subframeRegexps = [];

  // _browserSourceMap is a map of the latest search source for a particular
  // browser - one of the KNOWN_SEARCH_SOURCES in BrowserSearchTelemetry.
  _browserSourceMap = new WeakMap();

  /**
   * A WeakMap whose key is a browser with value of a source type found in
   * INCONTENT_SOURCES. Kept separate to avoid overlapping with legacy
   * search sources. These sources are specific to the content of a search
   * provider page rather than something from within the browser itself.
   */
  #browserContentSourceMap = new WeakMap();

  /**
   * Sets the source of a SERP visit from something that occured in content
   * rather than from the browser.
   *
   * @param {browser} browser
   *   The browser object associated with the page that should be a SERP.
   * @param {string} source
   *   The source that started the load. One of
   *   SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
   *   SearchSERPTelemetryUtils.INCONTENT_SOURCES.OPENED_IN_NEW_TAB or
   *   SearchSERPTelemetryUtils.INCONTENT_SOURCES.REFINE_ON_SERP.
   */
  setBrowserContentSource(browser, source) {
    this.#browserContentSourceMap.set(browser, source);
  }

  // _browserNewtabSessionMap is a map of the newtab session id for particular
  // browsers.
  _browserNewtabSessionMap = new WeakMap();

  constructor() {
    this._contentHandler = new ContentHandler({
      browserInfoByURL: this._browserInfoByURL,
      findBrowserItemForURL: (...args) => this._findBrowserItemForURL(...args),
      checkURLForSerpMatch: (...args) => this._checkURLForSerpMatch(...args),
      findItemForBrowser: (...args) => this.findItemForBrowser(...args),
      urlIsKnownSERPSubframe: (...args) => this.urlIsKnownSERPSubframe(...args),
    });
  }

  /**
   * Initializes the TelemetryHandler and its ContentHandler. It will add
   * appropriate listeners to the window so that window opening and closing
   * can be tracked.
   */
  async init() {
    if (this._initialized) {
      return;
    }

    this._telemetrySettings = lazy.RemoteSettings(TELEMETRY_SETTINGS_KEY);
    let rawProviderInfo = [];
    try {
      rawProviderInfo = await this._telemetrySettings.get();
    } catch (ex) {
      lazy.logConsole.error("Could not get settings:", ex);
    }

    this.#telemetrySettingsSync = event => this.#onSettingsSync(event);
    this._telemetrySettings.on("sync", this.#telemetrySettingsSync);

    // Send the provider info to the child handler.
    this._contentHandler.init(rawProviderInfo);
    this._originalProviderInfo = rawProviderInfo;

    // Now convert the regexps into
    this._setSearchProviderInfo(rawProviderInfo);

    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      this._registerWindow(win);
    }
    Services.wm.addListener(this);

    this._initialized = true;
  }

  async #onSettingsSync(event) {
    let current = event.data?.current;
    if (current) {
      lazy.logConsole.debug(
        "Update provider info due to Remote Settings sync."
      );
      this._originalProviderInfo = current;
      this._setSearchProviderInfo(current);
      Services.ppmm.sharedData.set(
        SEARCH_TELEMETRY_SHARED.PROVIDER_INFO,
        current
      );
      Services.ppmm.sharedData.flush();
    } else {
      lazy.logConsole.debug(
        "Ignoring Remote Settings sync data due to missing records."
      );
    }
    Services.obs.notifyObservers(null, "search-telemetry-v2-synced");
  }

  /**
   * Uninitializes the TelemetryHandler and its ContentHandler.
   */
  uninit() {
    if (!this._initialized) {
      return;
    }

    this._contentHandler.uninit();

    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      this._unregisterWindow(win);
    }
    Services.wm.removeListener(this);

    try {
      this._telemetrySettings.off("sync", this.#telemetrySettingsSync);
    } catch (ex) {
      lazy.logConsole.error(
        "Failed to shutdown SearchSERPTelemetry Remote Settings.",
        ex
      );
    }
    this._telemetrySettings = null;
    this.#telemetrySettingsSync = null;

    this._initialized = false;
  }

  /**
   * Records the search source for particular browsers, in case it needs
   * to be associated with a SERP.
   *
   * @param {browser} browser
   *   The browser where the search originated.
   * @param {string} source
   *    Where the search originated from.
   */
  recordBrowserSource(browser, source) {
    this._browserSourceMap.set(browser, source);
  }

  /**
   * Records the newtab source for particular browsers, in case it needs
   * to be associated with a SERP.
   *
   * @param {browser} browser
   *   The browser where the search originated.
   * @param {string} newtabSessionId
   *    The sessionId of the newtab session the search originated from.
   */
  recordBrowserNewtabSession(browser, newtabSessionId) {
    this._browserNewtabSessionMap.set(browser, newtabSessionId);
  }

  /**
   * Helper function for recording the reason for a Glean abandonment event.
   *
   * @param {string} impressionId
   *    The impression id for the abandonment event about to be recorded.
   * @param {string} reason
   *    The reason the SERP is deemed abandoned.
   *    One of SearchSERPTelemetryUtils.ABANDONMENTS.
   */
  recordAbandonmentTelemetry(impressionId, reason) {
    impressionIdsWithoutEngagementsSet.delete(impressionId);

    lazy.logConsole.debug(
      `Recording an abandonment event for impression id ${impressionId} with reason: ${reason}`
    );

    Glean.serp.abandonment.record({
      impression_id: impressionId,
      reason,
    });
  }

  /**
   * Handles the TabClose event received from the listeners.
   *
   * @param {object} event
   *   The event object provided by the listener.
   */
  handleEvent(event) {
    if (event.type != "TabClose") {
      console.error("Received unexpected event type", event.type);
      return;
    }

    this._browserNewtabSessionMap.delete(event.target.linkedBrowser);
    this.stopTrackingBrowser(
      event.target.linkedBrowser,
      SearchSERPTelemetryUtils.ABANDONMENTS.TAB_CLOSE
    );
  }

  /**
   * Test-only function, used to override the provider information, so that
   * unit tests can set it to easy to test values.
   *
   * @param {Array} providerInfo
   *   See {@link https://searchfox.org/mozilla-central/search?q=search-telemetry-v2-schema.json}
   *   for type information.
   */
  overrideSearchTelemetryForTests(providerInfo) {
    let info = providerInfo ? providerInfo : this._originalProviderInfo;
    this._contentHandler.overrideSearchTelemetryForTests(info);
    this._setSearchProviderInfo(info);
  }

  /**
   * Used to set the local version of the search provider information.
   * This automatically maps the regexps to RegExp objects so that
   * we don't have to create a new instance each time.
   *
   * @param {Array} providerInfo
   *   A raw array of provider information to set.
   */
  _setSearchProviderInfo(providerInfo) {
    this.#subframeRegexps = [];
    this._searchProviderInfo = providerInfo.map(provider => {
      let newProvider = {
        ...provider,
        searchPageRegexp: new RegExp(provider.searchPageRegexp),
      };
      if (provider.extraAdServersRegexps) {
        newProvider.extraAdServersRegexps = provider.extraAdServersRegexps.map(
          r => new RegExp(r)
        );
      }

      newProvider.ignoreLinkRegexps = provider.ignoreLinkRegexps?.length
        ? provider.ignoreLinkRegexps.map(r => new RegExp(r))
        : [];

      newProvider.nonAdsLinkRegexps = provider.nonAdsLinkRegexps?.length
        ? provider.nonAdsLinkRegexps.map(r => new RegExp(r))
        : [];
      if (provider.shoppingTab?.regexp) {
        newProvider.shoppingTab = {
          selector: provider.shoppingTab.selector,
          regexp: new RegExp(provider.shoppingTab.regexp),
        };
      }

      newProvider.nonAdsLinkQueryParamNames =
        provider.nonAdsLinkQueryParamNames ?? [];

      newProvider.subframes =
        provider.subframes?.map(obj => {
          let regexp = new RegExp(obj.regexp);
          // Also add the Regexp to the list of urls to observe.
          this.#subframeRegexps.push(regexp);
          return { ...obj, regexp };
        }) ?? [];

      return newProvider;
    });
    this._contentHandler._searchProviderInfo = this._searchProviderInfo;
  }

  reportPageAction(info, browser) {
    this._contentHandler._reportPageAction(info, browser);
  }

  reportPageWithAds(info, browser) {
    this._contentHandler._reportPageWithAds(info, browser);
  }

  reportPageWithAdImpressions(info, browser) {
    this._contentHandler._reportPageWithAdImpressions(info, browser);
  }

  async reportPageDomains(info, browser) {
    await this._contentHandler._reportPageDomains(info, browser);
  }

  reportPageImpression(info, browser) {
    this._contentHandler._reportPageImpression(info, browser);
  }

  /**
   * This may start tracking a tab based on the URL. If the URL matches a search
   * partner, and it has a code, then we'll start tracking it. This will aid
   * determining if it is a page we should be tracking for adverts.
   *
   * @param {object} browser
   *   The browser associated with the page.
   * @param {string} url
   *   The url that was loaded in the browser.
   * @param {nsIDocShell.LoadCommand} loadType
   *   The load type associated with the page load.
   */
  updateTrackingStatus(browser, url, loadType) {
    if (
      !lazy.BrowserSearchTelemetry.shouldRecordSearchCount(
        browser.getTabBrowser()
      )
    ) {
      return;
    }
    let info = this._checkURLForSerpMatch(url);
    if (!info) {
      this._browserNewtabSessionMap.delete(browser);
      this.stopTrackingBrowser(browser);
      return;
    }

    let source = "unknown";
    if (loadType & Ci.nsIDocShell.LOAD_CMD_RELOAD) {
      source = "reload";
    } else if (loadType & Ci.nsIDocShell.LOAD_CMD_HISTORY) {
      source = "tabhistory";
    } else if (this._browserSourceMap.has(browser)) {
      source = this._browserSourceMap.get(browser);
      this._browserSourceMap.delete(browser);
    }

    let newtabSessionId;
    if (this._browserNewtabSessionMap.has(browser)) {
      newtabSessionId = this._browserNewtabSessionMap.get(browser);
      // We leave the newtabSessionId in the map for this browser
      // until we stop loading SERP pages or the tab is closed.
    }

    // Generate metadata for the SERP impression.
    let { impressionId, impressionInfo } = this._generateImpressionInfo(
      browser,
      url,
      info,
      source
    );

    this._reportSerpPage(info, source, url);

    // For single page apps, we store the page by its original URI so the
    // network observers can recover the browser in a context when they only
    // have access to the originURL.
    let urlKey =
      info.isSPA && browser.originalURI?.spec ? browser.originalURI.spec : url;
    let item = this._browserInfoByURL.get(urlKey);

    if (item) {
      item.browserTelemetryStateMap.set(browser, {
        adsReported: false,
        adImpressionsReported: false,
        impressionId,
        urlToComponentMap: null,
        impressionInfo,
        searchBoxSubmitted: false,
        categorizationInfo: null,
        adsClicked: 0,
        adsHidden: 0,
        adsLoaded: 0,
        adsVisible: 0,
        searchQuery: info.searchQuery,
      });
      item.count++;
      item.source = source;
      item.newtabSessionId = newtabSessionId;
    } else {
      item = {
        browserTelemetryStateMap: new WeakMap().set(browser, {
          adsReported: false,
          adImpressionsReported: false,
          impressionId,
          urlToComponentMap: null,
          impressionInfo,
          searchBoxSubmitted: false,
          categorizationInfo: null,
          adsClicked: 0,
          adsHidden: 0,
          adsLoaded: 0,
          adsVisible: 0,
          searchQuery: info.searchQuery,
        }),
        info,
        count: 1,
        source,
        newtabSessionId,
        majorVersion: parseInt(Services.appinfo.version),
        channel: lazy.SearchUtils.MODIFIED_APP_CHANNEL,
        region: lazy.Region.home,
        isSPA: info.isSPA,
      };
      // For single page apps, we store the page by its original URI so that
      // network observers can recover the browser in a context when they only
      // have the originURL to work with.
      this._browserInfoByURL.set(urlKey, item);
    }
    this.#browserToItemMap.set(browser, item);
  }

  /**
   * Determines whether or not a browser should be untracked or tracked for
   * SERPs who have single page app behaviour.
   *
   * The over-arching logic:
   * 1. Only inspect the browser if the url matches a SERP that is a SPA.
   * 2. Recording an engagement if we're tracking the browser and we're going
   *    to another page.
   * 3. Untrack the browser if we're tracking it and switching pages.
   * 4. Track the browser if we're now on a default search page.
   *
   * @param {BrowserElement} browser
   *   The browser element related to the request.
   * @param {string} url
   *   The url of the request.
   * @param {number} loadType
   *   The loadtype of a the request.
   */
  async updateTrackingSinglePageApp(browser, url, loadType) {
    let providerInfo = this._getProviderInfoForURL(url);
    if (!providerInfo?.isSPA) {
      return;
    }

    let item = this.findItemForBrowser(browser);
    let telemetryState = item?.browserTelemetryStateMap.get(browser);

    let previousSearchTerm = telemetryState?.searchQuery ?? "";
    let searchTerm = this.urlSearchTerms(url, providerInfo);
    let searchTermChanged = previousSearchTerm !== searchTerm;

    let isSerp = !!this._checkURLForSerpMatch(url, providerInfo);
    let browserIsTracked = !!telemetryState;
    let isTabHistory = loadType & Ci.nsIDocShell.LOAD_CMD_HISTORY;

    // Step 2: Maybe record engagement.
    if (browserIsTracked && !isTabHistory && (searchTermChanged || !isSerp)) {
      // If we've established we've changed to another SERP, the cause could be
      // from a submission event inside the content process. The event is
      // sent to the parent and stored as `telemetryState.searchBoxSubmitted`
      // but if we check now, it may be too early. Instead, we check with the
      // content process directly to see if it recorded a submit event.
      let actor = browser.browsingContext.currentWindowGlobal.getActor(
        "SearchSERPTelemetry"
      );
      let didSubmit = await actor.sendQuery("SearchSERPTelemetry:DidSubmit");

      if (telemetryState && !telemetryState.searchBoxSubmitted && !didSubmit) {
        impressionIdsWithoutEngagementsSet.delete(telemetryState.impressionId);
        Glean.serp.engagement.record({
          impression_id: telemetryState.impressionId,
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        });
        lazy.logConsole.debug("Counting click:", {
          impressionId: telemetryState.impressionId,
          type: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
          URL: url,
        });
      }
    }

    // Step 3: Maybe untrack the browser.
    if (browserIsTracked && (searchTermChanged || !isSerp)) {
      let reason = "";
      // If we have to untrack it, it might be due to the user using the
      // back/forward button.
      if (isTabHistory) {
        reason = SearchSERPTelemetryUtils.ABANDONMENTS.NAVIGATION;
      }
      let actor = browser.browsingContext.currentWindowGlobal.getActor(
        "SearchSERPTelemetry"
      );
      actor.sendAsyncMessage("SearchSERPTelemetry:StopTrackingDocument");
      this.stopTrackingBrowser(browser, reason);
      browserIsTracked = false;
    }

    // Step 4: Maybe track the browser.
    if (isSerp && !browserIsTracked) {
      this.updateTrackingStatus(browser, url, loadType);
      let actor = browser.browsingContext.currentWindowGlobal.getActor(
        "SearchSERPTelemetry"
      );
      actor.sendAsyncMessage("SearchSERPTelemetry:WaitForSPAPageLoad");
    }
  }

  /**
   * Stops tracking of a tab, for example the tab has loaded a different URL.
   * Also records a Glean abandonment event if appropriate.
   *
   * @param {object} browser The browser associated with the tab to stop being
   *   tracked.
   * @param {string} abandonmentReason
   *   An optional parameter that specifies why the browser is deemed abandoned.
   *   The reason will be recorded as part of Glean abandonment telemetry.
   *   One of SearchSERPTelemetryUtils.ABANDONMENTS.
   */
  stopTrackingBrowser(browser, abandonmentReason) {
    for (let [url, item] of this._browserInfoByURL) {
      if (item.browserTelemetryStateMap.has(browser)) {
        let telemetryState = item.browserTelemetryStateMap.get(browser);
        let impressionId = telemetryState.impressionId;
        if (impressionIdsWithoutEngagementsSet.has(impressionId)) {
          this.recordAbandonmentTelemetry(impressionId, abandonmentReason);
        }

        if (
          lazy.SERPCategorization.enabled &&
          telemetryState.categorizationInfo
        ) {
          lazy.SERPCategorizationEventScheduler.sendCallback(browser);
        }

        item.browserTelemetryStateMap.delete(browser);
        item.count--;
      }

      if (!item.count) {
        this._browserInfoByURL.delete(url);
      }
    }
    this.#browserToItemMap.delete(browser);
  }

  /**
   * Calculate how close two urls are in equality.
   *
   * The scoring system:
   * - If the URLs look exactly the same, including the ordering of query
   *   parameters, the score is Infinity.
   * - If the origin is the same, the score is increased by 1. Otherwise the
   *   score is 0.
   * - If the path is the same, the score is increased by 1.
   * - For each query parameter, if the key exists the score is increased by 1.
   *   Likewise if the query parameter values match.
   * - If the hash is the same, the score is increased by 1. This includes if
   *   the hash is missing in both URLs.
   *
   * @param {URL} url1
   *   Url to compare.
   * @param {URL} url2
   *   Other url to compare. Ordering shouldn't matter.
   * @param {object} [matchOptions]
   *   Options for checking equality.
   * @param {boolean} [matchOptions.path]
   *   Whether the path must match. Default to false.
   * @param {boolean} [matchOptions.paramValues]
   *   Whether the values of the query parameters must match if the query
   *   parameter key exists in the other. Defaults to false.
   * @returns {number}
   *   A score of how closely the two URLs match. Returns 0 if there is no
   *   match or the equality check failed for an enabled match option.
   */
  compareUrls(url1, url2, matchOptions = {}) {
    // In case of an exact match, well, that's an obvious winner.
    if (url1.href == url2.href) {
      return Infinity;
    }

    // Each step we get closer to the two URLs being the same, we increase the
    // score. The consumer of this method will use these scores to see which
    // of the URLs is the best match.
    let score = 0;
    if (url1.origin == url2.origin) {
      ++score;
      if (url1.pathname == url2.pathname) {
        ++score;
        for (let [key1, value1] of url1.searchParams) {
          // Let's not fuss about the ordering of search params, since the
          // score effect will solve that.
          if (url2.searchParams.has(key1)) {
            ++score;
            if (url2.searchParams.get(key1) == value1) {
              ++score;
            } else if (matchOptions.paramValues) {
              return 0;
            }
          }
        }
        if (url1.hash == url2.hash) {
          ++score;
        }
      } else if (matchOptions.path) {
        return 0;
      }
    }
    return score;
  }

  /**
   * Extracts the search terms from the URL based on the provider info.
   *
   * @param {string} url
   *  The URL to inspect.
   * @param {object} providerInfo
   *  The providerInfo associated with the URL.
   * @returns {string}
   *   The search term or if none is found, a blank string.
   */
  urlSearchTerms(url, providerInfo) {
    if (providerInfo?.queryParamNames?.length) {
      let { searchParams } = new URL(url);
      for (let queryParamName of providerInfo.queryParamNames) {
        let value = searchParams.get(queryParamName);
        if (value) {
          return value;
        }
      }
    }
    return "";
  }

  findItemForBrowser(browser) {
    return this.#browserToItemMap.get(browser);
  }

  /**
   * Parts of the URL, like search params and hashes, may be mutated by scripts
   * on a page we're tracking. Since we don't want to keep track of that
   * ourselves in order to keep the list of browser objects a weak-referenced
   * set, we do optional fuzzy matching of URLs to fetch the most relevant item
   * that contains tracking information.
   *
   * @param {string} url URL to fetch the tracking data for.
   * @returns {object} Map containing the following members:
   *   - {WeakMap} browsers
   *     Map of browser elements that belong to `url` and their ad report state.
   *   - {object} info
   *     Info dictionary as returned by `_checkURLForSerpMatch`.
   *   - {number} count
   *     The number of browser element we can most accurately tell we're
   *     tracking, since they're inside a WeakMap.
   */
  _findBrowserItemForURL(url) {
    url = URL.parse(url);
    if (!url) {
      return null;
    }

    let item;
    let currentBestMatch = 0;
    for (let [trackingURL, candidateItem] of this._browserInfoByURL) {
      if (currentBestMatch === Infinity) {
        break;
      }
      // Make sure to cache the parsed URL object, since there's no reason to
      // do it twice.
      trackingURL =
        candidateItem._trackingURL ||
        (candidateItem._trackingURL = URL.parse(trackingURL));
      if (!trackingURL) {
        continue;
      }
      let score = this.compareUrls(url, trackingURL);
      if (score > currentBestMatch) {
        item = candidateItem;
        currentBestMatch = score;
      }
    }

    return item;
  }

  // nsIWindowMediatorListener

  /**
   * This is called when a new window is opened, and handles registration of
   * that window if it is a browser window.
   *
   * @param {nsIAppWindow} appWin The xul window that was opened.
   */
  onOpenWindow(appWin) {
    let win = appWin.docShell.domWindow;
    win.addEventListener(
      "load",
      () => {
        if (
          win.document.documentElement.getAttribute("windowtype") !=
          "navigator:browser"
        ) {
          return;
        }

        this._registerWindow(win);
      },
      { once: true }
    );
  }

  /**
   * Listener that is called when a window is closed, and handles deregistration of
   * that window if it is a browser window.
   *
   * @param {nsIAppWindow} appWin The xul window that was closed.
   */
  onCloseWindow(appWin) {
    let win = appWin.docShell.domWindow;

    if (
      win.document.documentElement.getAttribute("windowtype") !=
      "navigator:browser"
    ) {
      return;
    }

    this._unregisterWindow(win);
  }

  urlIsKnownSERPSubframe(url) {
    if (url) {
      for (let regexp of this.#subframeRegexps) {
        if (regexp.test(url)) {
          return true;
        }
      }
    }
    return false;
  }

  /**
   * Adds event listeners for the window and registers it with the content handler.
   *
   * @param {object} win The window to register.
   */
  _registerWindow(win) {
    win.gBrowser.tabContainer.addEventListener("TabClose", this);
  }

  /**
   * Removes event listeners for the window and unregisters it with the content
   * handler.
   *
   * @param {object} win The window to unregister.
   */
  _unregisterWindow(win) {
    for (let tab of win.gBrowser.tabs) {
      this.stopTrackingBrowser(
        tab.linkedBrowser,
        SearchSERPTelemetryUtils.ABANDONMENTS.WINDOW_CLOSE
      );
    }

    win.gBrowser.tabContainer.removeEventListener("TabClose", this);
  }

  /**
   * Searches for provider information for a given url.
   *
   * @param {string} url The url to match for a provider.
   * @returns {Array | null} Returns an array of provider name and the provider information.
   */
  _getProviderInfoForURL(url) {
    return this._searchProviderInfo.find(info =>
      info.searchPageRegexp.test(url)
    );
  }

  /**
   * Checks to see if a url is a search partner location, and determines the
   * provider and codes used.
   *
   * @param {string} url The url to match.
   * @returns {null|object} Returns null if there is no match found. Otherwise,
   *   returns an object of strings for provider, code, type, whether it's a
   *   single page app, and the search query used.
   */
  _checkURLForSerpMatch(url) {
    let searchProviderInfo = this._getProviderInfoForURL(url);
    if (!searchProviderInfo) {
      return null;
    }

    let queries = new URL(url).searchParams;
    queries.forEach((v, k) => {
      queries.set(k.toLowerCase(), v);
    });

    let isSPA = !!searchProviderInfo.isSPA;
    if (isSPA) {
      // A URL may have a specific query parameter denoting a search page.
      // If the key was expected but doesn't currently exist, it could be due to
      // the initial url containing it until after a page load.
      // In that case, ignore this check since most SERPs missing the query
      // param will go to the default search page.
      let { key, value } = searchProviderInfo.defaultPageQueryParam;
      if (key && queries.has(key) && queries.get(key) != value) {
        return null;
      }
    }

    // Some URLs can match provider info but also be the provider's homepage
    // instead of a SERP.
    // e.g. https://example.com/ vs. https://example.com/?foo=bar
    // Look for the presence of the query parameter that contains a search term.
    let hasQuery = false;
    let searchQuery = "";
    for (let queryParamName of searchProviderInfo.queryParamNames) {
      searchQuery = queries.get(queryParamName);
      if (searchQuery) {
        hasQuery = true;
        break;
      }
    }
    if (!hasQuery) {
      return null;
    }
    // Default to organic to simplify things.
    // We override type in the sap cases.
    let type = "organic";
    let code;
    if (searchProviderInfo.codeParamName) {
      code = queries.get(searchProviderInfo.codeParamName.toLowerCase());
      if (code) {
        // The code is only included if it matches one of the specific ones.
        if (searchProviderInfo.taggedCodes.includes(code)) {
          type = "tagged";
          if (
            searchProviderInfo.followOnParamNames &&
            searchProviderInfo.followOnParamNames.some(p => queries.has(p))
          ) {
            type += "-follow-on";
          }
        } else if (searchProviderInfo.organicCodes.includes(code)) {
          type = "organic";
        } else if (searchProviderInfo.expectedOrganicCodes?.includes(code)) {
          code = "none";
        } else {
          code = "other";
        }
      } else if (searchProviderInfo.followOnCookies) {
        // Especially Bing requires lots of extra work related to cookies.
        for (let followOnCookie of searchProviderInfo.followOnCookies) {
          if (followOnCookie.extraCodeParamName) {
            let eCode = queries.get(
              followOnCookie.extraCodeParamName.toLowerCase()
            );
            if (
              !eCode ||
              !followOnCookie.extraCodePrefixes.some(p => eCode.startsWith(p))
            ) {
              continue;
            }
          }

          // If this cookie is present, it's probably an SAP follow-on.
          // This might be an organic follow-on in the same session, but there
          // is no way to tell the difference.
          for (let cookie of Services.cookies.getCookiesFromHost(
            followOnCookie.host,
            {}
          )) {
            if (cookie.name != followOnCookie.name) {
              continue;
            }

            // Cookie values may take the form of "foo=bar&baz=1".
            let [cookieParam, cookieValue] = cookie.value
              .split("&")[0]
              .split("=")
              .map(p => p.trim());
            if (
              cookieParam == followOnCookie.codeParamName &&
              searchProviderInfo.taggedCodes.includes(cookieValue)
            ) {
              type = "tagged-follow-on";
              code = cookieValue;
              break;
            }
          }
        }
      }
    }

    return {
      provider: searchProviderInfo.telemetryId,
      type,
      code,
      searchQuery,
      isSPA,
    };
  }

  /**
   * Logs telemetry for a search provider visit.
   *
   * @param {object} info The search provider information.
   * @param {string} info.provider The name of the provider.
   * @param {string} info.type The type of search.
   * @param {string} [info.code] The code for the provider.
   * @param {string} source Where the search originated from.
   * @param {string} url The url that was matched (for debug logging only).
   */
  _reportSerpPage(info, source, url) {
    let payload = `${info.provider}:${info.type}:${info.code || "none"}`;
    let name = source.replace(/_([a-z])/g, (m, p) => p.toUpperCase());
    Glean.browserSearchContent[name][payload].add(1);
    lazy.logConsole.debug("Impression:", payload, url);
  }

  /**
   * @typedef {object} ImpressionInfo
   * @property {string} provider The name of the provider for the impression.
   * @property {boolean} tagged Whether the search has partner tags.
   * @property {string} source The search access point.
   * @property {boolean} isShoppingPage Whether the page is shopping.
   * @property {boolean} isPrivate Whether the SERP is in a private tab.
   * @property {boolean} isSignedIn Whether the user is signed on to the SERP.
   */

  /**
   * @typedef {object} ImpressionInfoResult
   * @property {string | null} impressionId The unique id of the impression.
   * @property {ImpressionInfo | null} impressionInfo General impresison info.
   */

  /**
   * If applicable for a tracked SERP provider, generates a unique id and
   * caches information that shouldn't be changed during the lifetime of the
   * impression.
   *
   * @param {browser} browser
   *   The browser associated with the SERP.
   * @param {string} url
   *   The URL of the SERP.
   * @param {object} info
   *   General information about the tracked SERP.
   * @param {string} source
   *   The originator of the SERP load.
   * @returns {ImpressionInfoResult} The result when attempting to generate
   *   impression info.
   */
  _generateImpressionInfo(browser, url, info, source) {
    let searchProviderInfo = this._getProviderInfoForURL(url);
    let data = {
      impressionId: null,
      impressionInfo: null,
    };

    if (!searchProviderInfo?.components?.length) {
      return data;
    }

    // The UUID generated by Services.uuid contains leading and trailing braces.
    // Need to trim them first.
    data.impressionId = Services.uuid.generateUUID().toString().slice(1, -1);
    impressionIdsWithoutEngagementsSet.add(data.impressionId);

    // If it's a SERP but doesn't have a browser source, the source might be
    // from something that happened in content.
    if (this.#browserContentSourceMap.has(browser)) {
      source = this.#browserContentSourceMap.get(browser);
      this.#browserContentSourceMap.delete(browser);
    }

    let partnerCode = "";
    if (info.code != "none" && info.code != null) {
      partnerCode = info.code;
    }

    let isShoppingPage = false;
    if (searchProviderInfo.shoppingTab?.regexp) {
      isShoppingPage = searchProviderInfo.shoppingTab.regexp.test(url);
    }

    let isPrivate =
      browser.contentPrincipal.originAttributes.privateBrowsingId > 0;

    let isSignedIn = false;
    // Signed-in status should not be recorded when the client is in a private
    // window.
    if (!isPrivate && searchProviderInfo.signedInCookies) {
      isSignedIn = searchProviderInfo.signedInCookies.some(cookieObj => {
        return Services.cookies
          .getCookiesFromHost(
            cookieObj.host,
            browser.contentPrincipal.originAttributes
          )
          .some(c => c.name == cookieObj.name);
      });
    }

    data.impressionInfo = {
      provider: info.provider,
      tagged: info.type.startsWith("tagged"),
      partnerCode,
      source,
      isShoppingPage,
      isPrivate,
      isSignedIn,
    };

    return data;
  }
}

/**
 * ContentHandler deals with handling telemetry of the content within a tab -
 * when ads detected and when they are selected.
 */
class ContentHandler {
  /**
   * Constructor.
   *
   * @param {object} options
   *   The options for the handler.
   * @param {Map} options.browserInfoByURL
   *   The map of urls from TelemetryHandler.
   * @param {Function} options.getProviderInfoForURL
   *   A function that obtains the provider information for a url.
   */
  constructor(options) {
    this._browserInfoByURL = options.browserInfoByURL;
    this._findBrowserItemForURL = options.findBrowserItemForURL;
    this._checkURLForSerpMatch = options.checkURLForSerpMatch;
    this._findItemForBrowser = options.findItemForBrowser;
    this._urlIsKnownSERPSubframe = options.urlIsKnownSERPSubframe;
  }

  /**
   * Initializes the content handler. This will also set up the shared data that is
   * shared with the SearchTelemetryChild actor.
   *
   * @param {Array} providerInfo
   *  The provider information for the search telemetry to record.
   */
  init(providerInfo) {
    Services.ppmm.sharedData.set(
      SEARCH_TELEMETRY_SHARED.PROVIDER_INFO,
      providerInfo
    );
    Services.ppmm.sharedData.set(
      SEARCH_TELEMETRY_SHARED.LOAD_TIMEOUT,
      ADLINK_CHECK_TIMEOUT_MS
    );
    Services.ppmm.sharedData.set(
      SEARCH_TELEMETRY_SHARED.SPA_LOAD_TIMEOUT,
      SPA_ADLINK_CHECK_TIMEOUT_MS
    );

    Services.obs.addObserver(this, "http-on-examine-response");
    Services.obs.addObserver(this, "http-on-examine-cached-response");
  }

  /**
   * Uninitializes the content handler.
   */
  uninit() {
    Services.obs.removeObserver(this, "http-on-examine-response");
    Services.obs.removeObserver(this, "http-on-examine-cached-response");
  }

  /**
   * Test-only function to override the search provider information for use
   * with tests. Passes it to the SearchTelemetryChild actor.
   *
   * @param {object} providerInfo @see SEARCH_PROVIDER_INFO for type information.
   */
  overrideSearchTelemetryForTests(providerInfo) {
    Services.ppmm.sharedData.set("SearchTelemetry:ProviderInfo", providerInfo);
  }

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "http-on-examine-response":
      case "http-on-examine-cached-response":
        this.observeActivity(aSubject);
        break;
    }
  }

  /**
   * Listener that observes network activity, so that we can determine if a link
   * from a search provider page was followed, and if then if that link was an
   * ad click or not.
   *
   * @param {nsIChannel} channel   The channel that generated the activity.
   */
  observeActivity(channel) {
    if (!(channel instanceof Ci.nsIChannel)) {
      return;
    }

    let wrappedChannel = ChannelWrapper.get(channel);
    // The channel we're observing might be a redirect of a channel we've
    // observed before.
    if (wrappedChannel._adClickRecorded) {
      lazy.logConsole.debug("Ad click already recorded");
      return;
    }

    Services.tm.dispatchToMainThread(() => {
      // We suspect that No Content (204) responses are used to transfer or
      // update beacons. They used to lead to double-counting ad-clicks, so let's
      // ignore them.
      if (wrappedChannel.statusCode == 204) {
        lazy.logConsole.debug("Ignoring activity from ambiguous responses");
        return;
      }

      // The wrapper is consistent across redirects, so we can use it to track state.
      let originURL = wrappedChannel.originURI && wrappedChannel.originURI.spec;
      if (!originURL) {
        return;
      }

      let eligibleSubframeUrl = this.#getSerpUrlFromPossibleSubframeUrl(
        originURL,
        wrappedChannel
      );
      let item = this._findBrowserItemForURL(eligibleSubframeUrl || originURL);
      if (!item) {
        return;
      }

      let url = wrappedChannel.finalURL;

      let providerInfo = item.info.provider;
      let info = this._searchProviderInfo.find(provider => {
        return provider.telemetryId == providerInfo;
      });

      // If an error occurs with Glean SERP telemetry logic, avoid
      // disrupting legacy telemetry.
      try {
        this.#maybeRecordSERPTelemetry(wrappedChannel, item, info);
      } catch (ex) {
        lazy.logConsole.error(ex);
      }

      if (!info.extraAdServersRegexps?.some(regex => regex.test(url))) {
        return;
      }

      try {
        let name = item.source.replace(/_([a-z])/g, (m, p) => p.toUpperCase());
        Glean.browserSearchAdclicks[name][
          `${info.telemetryId}:${item.info.type}`
        ].add(1);
        wrappedChannel._adClickRecorded = true;
        if (item.newtabSessionId) {
          Glean.newtabSearchAd.click.record({
            newtab_visit_id: item.newtabSessionId,
            search_access_point: item.source,
            is_follow_on: item.info.type.endsWith("follow-on"),
            is_tagged: item.info.type.startsWith("tagged"),
            telemetry_id: item.info.provider,
          });
        }

        lazy.logConsole.debug("Counting ad click in page for:", {
          source: item.source,
          originURL,
          URL: url,
        });
      } catch (e) {
        console.error(e);
      }
    });
  }

  /**
   * Checks if a request should record an ad click if it can be traced to a
   * browser containing an observed SERP.
   *
   * @param {ChannelWrapper} wrappedChannel
   *   The wrapped channel.
   * @param {object} item
   *   The browser item associated with the origin URL of the request.
   * @param {object} info
   *   The search provider info associated with the item.
   */
  #maybeRecordSERPTelemetry(wrappedChannel, item, info) {
    if (wrappedChannel._recordedClick) {
      lazy.logConsole.debug("Click already recorded.");
      return;
    }

    let originURL = wrappedChannel.originURI?.spec;
    let url = wrappedChannel.finalURL;

    if (info.ignoreLinkRegexps.some(r => r.test(url))) {
      lazy.logConsole.debug("Ignore url.");
      return;
    }

    // Some channels re-direct by loading pages that return 200. The result
    // is the channel will have an originURL that changes from the SERP to
    // either a nonAdsRegexp or an extraAdServersRegexps. This is typical
    // for loading a page in a new tab. The channel will have changed so any
    // properties attached to them to record state (e.g. _recordedClick)
    // won't be present.
    if (
      info.nonAdsLinkRegexps.some(r => r.test(originURL)) ||
      info.extraAdServersRegexps.some(r => r.test(originURL))
    ) {
      lazy.logConsole.debug("Expecting redirect.");
      return;
    }

    // A click event is recorded if a user loads a resource from an
    // originURL that is a SERP.
    //
    // Typically, we only want top level loads containing documents to avoid
    // recording any event on an in-page resource a SERP might load
    // (e.g. CSS files).
    //
    // The exception to this is if a subframe loads a resource that matches
    // a non ad link. Some SERPs encode non ad search results with a URL
    // that gets loaded into an iframe, which then tells the container of
    // the iframe to change the location of the page.
    if (
      wrappedChannel.channel.isDocument &&
      (wrappedChannel.channel.loadInfo.isTopLevelLoad ||
        info.nonAdsLinkRegexps.some(r => r.test(url)))
    ) {
      let browser = wrappedChannel.browserElement;

      // If the load is from history, don't record an event.
      if (
        browser?.browsingContext.webProgress?.loadType &
        Ci.nsIDocShell.LOAD_CMD_HISTORY
      ) {
        lazy.logConsole.debug("Ignoring load from history");
        return;
      }

      // Step 1: Check if the browser associated with the request was a
      // tracked SERP.
      let start = Cu.now();
      let telemetryState;
      let isFromNewtab = false;
      if (item.browserTelemetryStateMap.has(browser)) {
        // If the map contains the browser, then it means that the request is
        // the SERP is going from one page to another. We know this because
        // previous conditions prevent non-top level loads from occuring here.
        telemetryState = item.browserTelemetryStateMap.get(browser);
      } else if (browser) {
        // Alternatively, it could be the case that the request is occuring in
        // a new tab but was triggered by one of the browsers in the state map.
        // If only one browser exists in the state map, it must be that one.
        if (item.count === 1) {
          let sourceBrowsers = ChromeUtils.nondeterministicGetWeakMapKeys(
            item.browserTelemetryStateMap
          );
          if (sourceBrowsers?.length) {
            telemetryState = item.browserTelemetryStateMap.get(
              sourceBrowsers[0]
            );
          }
        } else if (item.count > 1) {
          // If the count is more than 1, then multiple open SERPs contain the
          // same search term, so try to find the specific browser that opened
          // the request.
          let tabBrowser = browser.getTabBrowser();
          let tab = tabBrowser.getTabForBrowser(browser).openerTab;
          // A tab will not always have an openerTab, as first tabs in new
          // windows don't have an openerTab.
          // Bug 1867582: We should also handle the case where multiple tabs
          // contain the same search term.
          if (tab) {
            telemetryState = item.browserTelemetryStateMap.get(
              tab.linkedBrowser
            );
          }
        }
        if (telemetryState) {
          isFromNewtab = true;
        }
      }

      lazy.logConsole.debug("Telemetry state:", telemetryState);

      // Step 2: If we have telemetryState, the browser object must be
      // associated with another browser that is tracked. Try to find the
      // component type on the SERP responsible for the request.
      // Exceptions:
      // - If a searchbox was used to initiate the load, don't record another
      //   engagement because the event was logged elsewhere.
      // - If the ad impression hasn't been recorded yet, we have no way of
      //   knowing precisely what kind of component was selected.
      let isSerp = false;
      if (
        telemetryState &&
        telemetryState.adImpressionsReported &&
        !telemetryState.searchBoxSubmitted
      ) {
        if (info.searchPageRegexp?.test(originURL)) {
          isSerp = true;
        }

        let startFindComponent = Cu.now();
        let parsedUrl = new URL(url);

        // Organic links may contain query param values mapped to links shown
        // on the SERP at page load. If a stored component depends on that
        // value, we need to be able to recover it or else we'll always consider
        // it a non_ads_link.
        if (
          info.nonAdsLinkQueryParamNames.length &&
          info.nonAdsLinkRegexps.some(r => r.test(url))
        ) {
          for (let key of info.nonAdsLinkQueryParamNames) {
            let paramValue = parsedUrl.searchParams.get(key);
            if (paramValue) {
              let newParsedUrl = /^https?:\/\//.test(paramValue)
                ? URL.parse(paramValue)
                : URL.parse(paramValue, parsedUrl.origin);
              if (newParsedUrl) {
                parsedUrl = newParsedUrl;
                break;
              }
            }
          }
        }

        // Determine the component type of the link.
        let type;
        for (let [
          storedUrl,
          componentType,
        ] of telemetryState.urlToComponentMap.entries()) {
          // The URL we're navigating to may have more query parameters if
          // the provider adds query parameters when the user clicks on a link.
          // On the other hand, the URL we are navigating to may have have
          // fewer query parameters because of query param stripping.
          // Thus, if a query parameter is missing, a match can still be made
          // provided keys that exist in both URLs contain equal values.
          let score = SearchSERPTelemetry.compareUrls(storedUrl, parsedUrl, {
            paramValues: true,
            path: true,
          });
          if (score) {
            type = componentType;
            break;
          }
        }
        ChromeUtils.addProfilerMarker(
          "SearchSERPTelemetry._observeActivity",
          startFindComponent,
          "Find component for URL"
        );

        // If no component was found, it's possible the link was added after
        // components were categorized.
        if (!type) {
          let isAd = info.extraAdServersRegexps?.some(regex => regex.test(url));
          type = isAd
            ? SearchSERPTelemetryUtils.COMPONENTS.AD_UNCATEGORIZED
            : SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK;
        }

        if (
          type == SearchSERPTelemetryUtils.COMPONENTS.REFINED_SEARCH_BUTTONS
        ) {
          SearchSERPTelemetry.setBrowserContentSource(
            browser,
            SearchSERPTelemetryUtils.INCONTENT_SOURCES.REFINE_ON_SERP
          );
        } else if (isSerp && isFromNewtab) {
          SearchSERPTelemetry.setBrowserContentSource(
            browser,
            SearchSERPTelemetryUtils.INCONTENT_SOURCES.OPENED_IN_NEW_TAB
          );
        }

        // Step 3: Record the engagement.
        impressionIdsWithoutEngagementsSet.delete(telemetryState.impressionId);
        if (AD_COMPONENTS.includes(type)) {
          telemetryState.adsClicked += 1;
        }
        Glean.serp.engagement.record({
          impression_id: telemetryState.impressionId,
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: type,
        });
        lazy.logConsole.debug("Counting click:", {
          impressionId: telemetryState.impressionId,
          type,
          URL: url,
        });
        // Prevent re-directed channels from being examined more than once.
        wrappedChannel._recordedClick = true;
      }
      ChromeUtils.addProfilerMarker(
        "SearchSERPTelemetry._observeActivity",
        start,
        "Maybe record user engagement."
      );
    }
  }

  /**
   * Checks if the url associated with a request is actually coming from a
   * subframe within a SERP. If so, try to find the best url associated with
   * the frame.
   *
   * @param {string} originURL
   *   The url associated with the request.
   * @param {ChannelWrapper} wrappedChannel
   *   The wrapped channel.
   * @returns {string?}
   *   The url associated with the subframe.
   */
  #getSerpUrlFromPossibleSubframeUrl(originURL, wrappedChannel) {
    if (!this._urlIsKnownSERPSubframe(originURL)) {
      return null;
    }

    // The sponsored link could be opened in a new tab, in which case the
    // browser URI may not match a SERP. Thus, try to find a tab that contains
    // a URI matching a SERP.
    let browser = wrappedChannel.browserElement;
    if (browser?.currentURI.spec == "about:blank") {
      let tabBrowser = browser.getTabBrowser();
      let tab = tabBrowser.getTabForBrowser(browser).openerTab;
      if (tab) {
        return tab.linkedBrowser.currentURI.spec;
      }
      // If no opener tab was found, we're likely looking at the first tab of
      // a new window. As a last resort, check if the window below the newly
      // opened window contains a tab with a matching SERP.
      let windows = lazy.BrowserWindowTracker.orderedWindows;
      let win = windows.at(1);
      if (win) {
        let url = win.gBrowser.selectedBrowser.originalURI?.spec;
        if (url) {
          return url;
        }
      }
      // If we couldn't find a matching tab or window, then return null to
      // indicate to the caller we weren't able to find an appropriate SERP.
      return null;
    }

    return browser?.currentURI.spec;
  }

  /**
   * Logs telemetry for a page with adverts, if it is one of the partner search
   * provider pages that we're tracking.
   *
   * @param {object} info
   *     The search provider information for the page.
   * @param {boolean} info.hasAds
   *     Whether or not the page has adverts.
   * @param {string} info.url
   *     The url of the page.
   * @param {object} browser
   *     The browser associated with the page.
   */
  _reportPageWithAds(info, browser) {
    let item = this._findItemForBrowser(browser);
    if (!item) {
      lazy.logConsole.warn(
        "Expected to report URI for",
        info.url,
        "with ads but couldn't find the information"
      );
      return;
    }

    let telemetryState = item.browserTelemetryStateMap.get(browser);
    if (telemetryState.adsReported) {
      lazy.logConsole.debug(
        "Ad was previously reported for browser with URI",
        info.url
      );
      return;
    }

    lazy.logConsole.debug(
      "Counting ads in page for",
      item.info.provider,
      item.info.type,
      item.source,
      info.url
    );
    let name = item.source.replace(/_([a-z])/g, (m, p) => p.toUpperCase());
    Glean.browserSearchWithads[name][
      `${item.info.provider}:${item.info.type}`
    ].add(1);
    Services.obs.notifyObservers(null, "reported-page-with-ads");

    telemetryState.adsReported = true;

    if (item.newtabSessionId) {
      Glean.newtabSearchAd.impression.record({
        newtab_visit_id: item.newtabSessionId,
        search_access_point: item.source,
        is_follow_on: item.info.type.endsWith("follow-on"),
        is_tagged: item.info.type.startsWith("tagged"),
        telemetry_id: item.info.provider,
      });
    }
  }

  /**
   * Logs ad impression telemetry for a page with adverts, if it is
   * one of the partner search provider pages that we're tracking.
   *
   * @param {object} info
   *     The search provider information for the page.
   * @param {string} info.url
   *     The url of the page.
   * @param {Map<string, object>} info.adImpressions
   *     A map of ad impressions found for the page, where the key
   *     is the type of ad component and the value is an object
   *     containing the number of ads that were loaded, visible,
   *     and hidden.
   * @param {Map<string, string>} info.hrefToComponentMap
   *     A map of hrefs to their component type. Contains both ads
   *     and non-ads.
   * @param {object} browser
   *     The browser associated with the page.
   */
  _reportPageWithAdImpressions(info, browser) {
    let item = this._findItemForBrowser(browser);
    if (!item) {
      return;
    }
    let telemetryState = item.browserTelemetryStateMap.get(browser);
    if (
      info.adImpressions &&
      telemetryState &&
      !telemetryState.adImpressionsReported
    ) {
      for (let [componentType, data] of info.adImpressions.entries()) {
        // Not all ad impressions are sponsored.
        if (AD_COMPONENTS.includes(componentType)) {
          telemetryState.adsHidden += data.adsHidden;
          telemetryState.adsLoaded += data.adsLoaded;
          telemetryState.adsVisible += data.adsVisible;
        }

        lazy.logConsole.debug("Counting ad:", { type: componentType, ...data });
        Glean.serp.adImpression.record({
          impression_id: telemetryState.impressionId,
          component: componentType,
          ads_loaded: data.adsLoaded,
          ads_visible: data.adsVisible,
          ads_hidden: data.adsHidden,
        });
      }
      // Convert hrefToComponentMap to a urlToComponentMap in order to cache
      // the query parameters of the href.
      let urlToComponentMap = new Map();
      for (let [href, adType] of info.hrefToComponentMap) {
        urlToComponentMap.set(new URL(href), adType);
      }
      telemetryState.urlToComponentMap = urlToComponentMap;
      telemetryState.adImpressionsReported = true;
      Services.obs.notifyObservers(null, "reported-page-with-ad-impressions");
    }
  }

  /**
   * Records a page action from a SERP page. Normally, actions are tracked in
   * parent process by observing network events but some actions are not
   * possible to detect outside of subscribing to the child process.
   *
   * @param {object} info
   *   The search provider infomation for the page.
   * @param {string} info.target
   *   The target component that was interacted with.
   * @param {string} info.action
   *   The action taken on the page.
   * @param {object} browser
   *   The browser associated with the page.
   */
  _reportPageAction(info, browser) {
    let item = this._findItemForBrowser(browser);
    if (!item) {
      return;
    }
    let telemetryState = item.browserTelemetryStateMap.get(browser);
    let impressionId = telemetryState?.impressionId;
    if (info.target && impressionId) {
      lazy.logConsole.debug(`Recorded page action:`, {
        impressionId: telemetryState.impressionId,
        target: info.target,
        action: info.action,
      });
      Glean.serp.engagement.record({
        impression_id: impressionId,
        action: info.action,
        target: info.target,
      });
      impressionIdsWithoutEngagementsSet.delete(impressionId);
      // In-content searches are not be categorized with a type, so they will
      // not be picked up in the network processes.
      if (
        info.target ==
          SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX &&
        info.action == SearchSERPTelemetryUtils.ACTIONS.SUBMITTED
      ) {
        telemetryState.searchBoxSubmitted = true;
        SearchSERPTelemetry.setBrowserContentSource(
          browser,
          SearchSERPTelemetryUtils.INCONTENT_SOURCES.SEARCHBOX
        );
      }
      Services.obs.notifyObservers(null, "reported-page-with-action");
    } else {
      lazy.logConsole.warn(
        "Expected to report a",
        info.action,
        "engagement for",
        info.url,
        "but couldn't find an impression id."
      );
    }
  }

  _reportPageImpression(info, browser) {
    let item = this._findItemForBrowser(browser);
    let telemetryState = item.browserTelemetryStateMap.get(browser);
    if (!telemetryState?.impressionInfo) {
      lazy.logConsole.debug(
        "Could not find telemetry state or impression info."
      );
      return;
    }
    let impressionId = telemetryState.impressionId;
    if (impressionId) {
      let impressionInfo = telemetryState.impressionInfo;
      Glean.serp.impression.record({
        impression_id: impressionId,
        provider: impressionInfo.provider,
        tagged: impressionInfo.tagged,
        partner_code: impressionInfo.partnerCode,
        source: impressionInfo.source,
        shopping_tab_displayed: info.shoppingTabDisplayed,
        is_shopping_page: impressionInfo.isShoppingPage,
        is_private: impressionInfo.isPrivate,
        is_signed_in: impressionInfo.isSignedIn,
      });
      lazy.logConsole.debug(`Reported Impression:`, {
        impressionId,
        ...impressionInfo,
        shoppingTabDisplayed: info.shoppingTabDisplayed,
      });
      Services.obs.notifyObservers(null, "reported-page-with-impression");
    } else {
      lazy.logConsole.debug("Could not find an impression id.");
    }
  }

  /**
  * Initiates the categorization and reporting of domains extracted from
  * SERPs.
  *
  * @param {object} info
  *   The search provider infomation for the page.
  * @param {Set} info.nonAdDomains
      The non-ad domains extracted from the page.
  * @param {Set} info.adDomains
      The ad domains extracted from the page.
  * @param {object} browser
  *   The browser associated with the page.
  */
  async _reportPageDomains(info, browser) {
    let item = this._findItemForBrowser(browser);
    let telemetryState = item?.browserTelemetryStateMap.get(browser);
    if (lazy.SERPCategorization.enabled && telemetryState) {
      lazy.logConsole.debug("Ad domains:", Array.from(info.adDomains));
      lazy.logConsole.debug("Non ad domains:", Array.from(info.nonAdDomains));
      let result = await lazy.SERPCategorization.maybeCategorizeSERP(
        info.nonAdDomains,
        info.adDomains,
        item.info.provider
      );
      if (result) {
        telemetryState.categorizationInfo = result;
        let callback = () => {
          let impressionInfo = telemetryState.impressionInfo;
          lazy.SERPCategorizationRecorder.recordCategorizationTelemetry({
            ...telemetryState.categorizationInfo,
            app_version: item.majorVersion,
            channel: item.channel,
            region: item.region,
            partner_code: impressionInfo.partnerCode,
            provider: impressionInfo.provider,
            tagged: impressionInfo.tagged,
            is_shopping_page: impressionInfo.isShoppingPage,
            num_ads_clicked: telemetryState.adsClicked,
            num_ads_hidden: telemetryState.adsHidden,
            num_ads_loaded: telemetryState.adsLoaded,
            num_ads_visible: telemetryState.adsVisible,
          });
        };
        lazy.SERPCategorizationEventScheduler.addCallback(browser, callback);
      }
    }
    Services.obs.notifyObservers(
      null,
      "reported-page-with-categorized-domains"
    );
  }
}

export var SearchSERPTelemetry = new TelemetryHandler();
