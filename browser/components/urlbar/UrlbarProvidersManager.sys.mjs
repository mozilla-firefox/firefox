/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a component used to register search providers and manage
 * the connection between such providers and a UrlbarParentController.
 */

/**
 * @import { UrlbarProvider } from "./UrlbarUtils.sys.mjs"
 * @import { UrlbarMuxer } from "./UrlbarUtils.sys.mjs"
 * @import { UrlbarSearchStringTokenData } from "./UrlbarTokenizer.sys.mjs"
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SkippableTimer: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarMuxer: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProvider: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.UrlbarShared.getLogger({ prefix: "ProvidersManager" })
);

// List of available local providers, each is implemented in its own module and
// will track different queries internally by queryContext.
// When adding new providers please remember to update the list in metrics.yaml.
var localProviderModules = [
  {
    name: "UrlbarProviderAboutPages",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderAboutPages.sys.mjs",
    supportedSAPs: ["smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderActionsSearchMode",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderActionsSearchMode.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderGlobalActions",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderGlobalActions.sys.mjs",
    supportedSAPs: ["searchbar", "urlbar"],
  },
  {
    name: "UrlbarProviderAliasEngines",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderAliasEngines.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "urlbar"],
  },
  {
    name: "UrlbarProviderAutofill",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderAutofill.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderBookmarkKeywords",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderBookmarkKeywords.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderCalculator",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderCalculator.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderAiChat",
    module: "moz-src:///browser/components/urlbar/UrlbarProviderAiChat.sys.mjs",
    supportedSAPs: ["smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderClipboard",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderClipboard.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderHeuristicFallback",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderHeuristicFallback.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderHistoryUrlHeuristic",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderHistoryUrlHeuristic.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderInputHistory",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderInputHistory.sys.mjs",
    supportedSAPs: ["smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderInterventions",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderInterventions.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderOmnibox",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderOmnibox.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderPlaces",
    module: "moz-src:///browser/components/urlbar/UrlbarProviderPlaces.sys.mjs",
    supportedSAPs: ["smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderPrivateSearch",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderPrivateSearch.sys.mjs",
    supportedSAPs: ["searchbar", "urlbar"],
  },
  {
    name: "UrlbarProviderQuickSuggest",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderQuickSuggest.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderQuickSuggestContextualOptIn",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderQuickSuggestContextualOptIn.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderRecentSearches",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderRecentSearches.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderRemoteTabs",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderRemoteTabs.sys.mjs",
    supportedSAPs: ["smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderRestrictKeywords",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderRestrictKeywords.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderRestrictKeywordsAutofill",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderRestrictKeywordsAutofill.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderSearchTips",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderSearchTips.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderSearchSuggestions",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderSearchSuggestions.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderSemanticHistorySearch",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderSemanticHistorySearch.sys.mjs",
    supportedSAPs: ["smartbar", "urlbar"],
  },
  {
    name: "UrlbarProviderTabToSearch",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderTabToSearch.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderTokenAliasEngines",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderTokenAliasEngines.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "urlbar"],
  },
  {
    name: "UrlbarProviderTopSites",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderTopSites.sys.mjs",
    supportedSAPs: ["urlbar"],
  },
  {
    name: "UrlbarProviderUnitConversion",
    module:
      "moz-src:///browser/components/urlbar/UrlbarProviderUnitConversion.sys.mjs",
    supportedSAPs: ["newtab_searchbar", "searchbar", "smartbar", "urlbar"],
  },
];

// List of available local muxers, each is implemented in its own jsm module.
var localMuxerModules = {
  UrlbarMuxerStandard:
    "moz-src:///browser/components/urlbar/UrlbarMuxerStandard.sys.mjs",
};

/**
 * Map of ProvidersManager instances per SAP name.
 *
 * @type {Map<string,ProvidersManager>}
 */
var gProvidersManagerPerSap = new Map();

// Monotonically increasing id stamped on every result as it is finalized, so a
// result can be matched to its context entry and view row across the actor
// boundary without relying on its position. Never reset, so a stale
// notification from a superseded query can't collide with a new query's result.
let gNextResultId = 0;

const DEFAULT_MUXER = "UnifiedComplete";
const DEFAULT_CHUNK_RESULTS_DELAY_MS = 16;

/**
 * Class used to create a manager. There always exists one manager instance
 * per input type. It is responsible to keep a list of provider instances,
 * instantiate query objects and pass those to the providers.
 */
export class ProvidersManager {
  /**
   * Interrupt() allows to stop any running SQL query, some provider may be
   * running a query that shouldn't be interrupted, and if so it should
   * bump this through disableInterrupt and enableInterrupt.
   */
  static interruptLevel = 0;

  /**
   * @param {string} sapName
   *  The SAP name this manager is for, e.g. "urlbar", "searchbar", ...
   * @param {object} muxerModules
   *   Object with symbol names as keys and module paths as values.
   *   Symbols should be UrlbarMuxer instances.
   */
  constructor(sapName, muxerModules = localMuxerModules) {
    /**
     * Tracks the available providers. This is a sorted array, with HEURISTIC
     * providers at the front.
     *
     * @type {UrlbarProvider[]}
     */
    this.providers = [];

    /**
     * @type {{onEngagement: Set<UrlbarProvider>, onImpression: Set<UrlbarProvider>, onAbandonment: Set<UrlbarProvider>, onSearchSessionEnd: Set<UrlbarProvider>}}
     */
    this.providersByNotificationType = {
      onEngagement: new Set(),
      onImpression: new Set(),
      onAbandonment: new Set(),
      onSearchSessionEnd: new Set(),
    };

    for (let providerInfo of localProviderModules.filter(info =>
      info.supportedSAPs.includes(sapName)
    )) {
      let { [providerInfo.name]: providerClass } = ChromeUtils.importESModule(
        providerInfo.module
      );
      this.registerProvider(new providerClass());
    }

    /**
     * Tracks ongoing Query instances by queryContext.
     *
     * @type {Map<object, Query>}
     */
    this.queries = new Map();

    /**
     * This maps muxer names to muxers.
     *
     * @type {Map<string, UrlbarMuxer>}
     */
    this.muxers = new Map();

    for (let [symbol, module] of Object.entries(muxerModules)) {
      let { [symbol]: muxer } = ChromeUtils.importESModule(module);
      this.registerMuxer(muxer);
    }
  }

  /**
   * This can be set by tests to increase or reduce the chunk delays.
   * See _notifyResultsFromProvider for additional details.
   * To improve dataflow and reduce UI work, when a result is added we may notify
   * it to the controller after a delay, so that we can chunk results in that
   * timeframe into a single call. See _notifyResultsFromProvider for details.
   */
  static chunkResultsDelayMs = DEFAULT_CHUNK_RESULTS_DELAY_MS;

  /**
   * Gets a cached ProvidersManager for the given SAP name, or creates a new one.
   *
   * @param {string} sapName The SAP name.
   * @returns {ProvidersManager} The ProvidersManager instance.
   */
  static getInstanceForSap(sapName) {
    let manager = gProvidersManagerPerSap.get(sapName);
    if (!manager) {
      manager = new ProvidersManager(sapName);
      gProvidersManagerPerSap.set(sapName, manager);
    }
    return manager;
  }

  /**
   * Registers a provider object with the manager.
   *
   * @param {object} provider
   *   The provider object to register.
   */
  registerProvider(provider) {
    if (!provider || !(provider instanceof lazy.UrlbarProvider)) {
      throw new Error(`Trying to register an invalid provider`);
    }
    if (
      !Object.values(lazy.UrlbarShared.PROVIDER_TYPE).includes(provider.type)
    ) {
      throw new Error(`Unknown provider type ${provider.type}`);
    }
    lazy.logger.info(`Registering provider ${provider.name}`);
    let index = -1;
    if (provider.type == lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC) {
      // Keep heuristic providers in order at the front of the array.  Find the
      // first non-heuristic provider and insert the new provider there.
      index = this.providers.findIndex(
        p => p.type != lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC
      );
    }
    if (index < 0) {
      index = this.providers.length;
    }
    this.providers.splice(index, 0, provider);

    for (const notificationType of Object.keys(
      this.providersByNotificationType
    )) {
      if (typeof provider[notificationType] === "function") {
        this.providersByNotificationType[notificationType].add(provider);
      }
    }
  }

  /**
   * Unregisters a previously registered provider object.
   *
   * @param {object} provider
   *   The provider object to unregister.
   */
  unregisterProvider(provider) {
    lazy.logger.info(`Unregistering provider ${provider.name}`);
    let index = this.providers.findIndex(p => p.name == provider.name);
    if (index != -1) {
      this.providers.splice(index, 1);
    }

    Object.values(this.providersByNotificationType).forEach(providers =>
      providers.delete(provider)
    );
  }

  /**
   * Returns the provider with the given name.
   *
   * @param {string} name
   *   The provider name.
   * @returns {UrlbarProvider | undefined}
   *   The provider.
   */
  getProvider(name) {
    return this.providers.find(p => p.name == name);
  }

  /**
   * Registers a muxer object with the manager.
   *
   * @param {UrlbarMuxer} muxer
   *   a UrlbarMuxer object
   */
  registerMuxer(muxer) {
    if (!muxer || !(muxer instanceof lazy.UrlbarMuxer)) {
      throw new Error(`Trying to register an invalid muxer`);
    }
    lazy.logger.info(`Registering muxer ${muxer.name}`);
    this.muxers.set(muxer.name, muxer);
  }

  /**
   * Unregisters a previously registered muxer object.
   *
   * @param {UrlbarMuxer|string} muxer
   *   a UrlbarMuxer object or name.
   */
  unregisterMuxer(muxer) {
    let muxerName = typeof muxer == "string" ? muxer : muxer.name;
    lazy.logger.info(`Unregistering muxer ${muxerName}`);
    this.muxers.delete(muxerName);
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query context object
   * @param {?UrlbarParentController} [controller]
   *   a UrlbarParentController instance
   */
  async startQuery(queryContext, controller = null) {
    lazy.logger.info(`Query start "${queryContext.searchString}"`);

    // Define the muxer to use.
    let muxerName = queryContext.muxer || DEFAULT_MUXER;
    lazy.logger.debug(`Using muxer ${muxerName}`);
    let muxer = this.muxers.get(muxerName);
    if (!muxer) {
      throw new Error(`Muxer with name ${muxerName} not found`);
    }

    // If the queryContext specifies a list of providers to use, filter on it,
    // otherwise just pass the full list of providers.
    let providers = queryContext.providers
      ? this.providers.filter(p => queryContext.providers.includes(p.name))
      : this.providers;

    queryContext.canceled = false;
    try {
      // The tokenizer needs to synchronously check whether the first token is a
      // keyword, thus here we must ensure the keywords cache is up.
      await lazy.PlacesUtils.keywords.ensureCacheInitialized();
    } catch (ex) {
      lazy.logger.error(
        "Unable to ensure keyword cache is initialization. A keyword may not be \
         detected at the beginning of the search string.",
        ex
      );
    }

    // The query may have been canceled while awaiting for asynchronous work.
    if (queryContext.canceled) {
      return;
    }

    // Apply tokenization.
    let tokens = lazy.UrlbarTokenizer.tokenize(queryContext);
    queryContext.tokens = tokens;

    // If there's a single source, we are in restriction mode.
    if (queryContext.sources && queryContext.sources.length == 1) {
      queryContext.restrictSource = queryContext.sources[0];
    }
    // Providers can use queryContext.sources to decide whether they want to be
    // invoked or not.
    // The sources may be defined in the context, then the whole search string
    // can be used for searching. Otherwise sources are extracted from prefs and
    // restriction tokens, then restriction tokens must be filtered out of the
    // search string.
    let restrictToken = updateSourcesIfEmpty(queryContext);
    if (restrictToken) {
      queryContext.restrictToken = restrictToken;
      // If the restriction token has an equivalent source, then set it as
      // restrictSource.
      if (lazy.UrlbarShared.SEARCH_MODE_RESTRICT.has(restrictToken.value)) {
        queryContext.restrictSource = queryContext.sources[0];
      }
    }
    lazy.logger.debug(`Context sources ${queryContext.sources}`);

    let query = new Query(queryContext, controller, muxer, providers);
    this.queries.set(queryContext, query);

    // The muxer and many providers depend on the search service and our search
    // utils.  Make sure they're initialized now (via UrlbarSearchUtils) so that
    // all query-related urlbar modules don't need to do it.
    try {
      await lazy.UrlbarSearchUtils.init();
    } catch {
      // We continue anyway, because we want the user to be able to search their
      // history and bookmarks even if search engines are not available.
    }

    // Some providers depend on Region/Locale info and must access Region.home
    // synchronously, so we ensure Region is initialized.
    try {
      await lazy.Region.init();
    } catch (ex) {
      // We continue anyway, region will be null and providers should handle
      // that gracefully.
    }

    if (query.canceled) {
      return;
    }

    await query.start();
  }

  /**
   * Cancels a running query.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  cancelQuery(queryContext) {
    lazy.logger.info(`Query cancel "${queryContext.searchString}"`);
    queryContext.canceled = true;

    let query = this.queries.get(queryContext);
    if (!query) {
      // The query object may have not been created yet, if the query was
      // canceled immediately.
      return;
    }
    query.cancel();
    if (!ProvidersManager.interruptLevel) {
      try {
        let db = lazy.PlacesUtils.promiseLargeCacheDBConnection();
        db.interrupt();
      } catch (ex) {}
    }
    this.queries.delete(queryContext);
  }

  /**
   * A provider can use this util when it needs to run a SQL query that can't
   * be interrupted. Otherwise, when a query is canceled any running SQL query
   * is interrupted abruptly.
   *
   * @param {Function} taskFn a Task to execute in the critical section.
   */
  static async runInCriticalSection(taskFn) {
    this.interruptLevel++;
    try {
      await taskFn();
    } finally {
      this.interruptLevel--;
    }
  }

  /**
   * Notifies all providers about changes in user engagement with the urlbar.
   * This function centralizes the dispatch of engagement-related events to the
   * appropriate providers based on the current state of interaction.
   *
   * @param {"engagement"|"abandonment"} state
   *   The state of the engagement, one of: engagement, abandonment
   * @param {UrlbarQueryContext} queryContext
   *   The engagement's query context, if available.
   * @param {object} details
   *   An object that describes the search string and the picked result, if any.
   * @param {UrlbarParentController} controller
   *   The controller associated with the engagement
   * @param {UrlbarResult[]} [visibleResults]
   *   The results shown at engagement. Passed on the message path, where the
   *   parent's view has none; falls back to the view on the direct path.
   */
  notifyEngagementChange(
    state,
    queryContext,
    details = {},
    controller,
    visibleResults
  ) {
    if (!["engagement", "abandonment"].includes(state)) {
      lazy.logger.error(`Unsupported state for engagement change: ${state}`);
      return;
    }

    // On the message path the parent's view has no results; the caller passes
    // the results shown content-side. Fall back to the view for the direct path.
    visibleResults = visibleResults ?? controller.view?.visibleResults ?? [];
    const visibleResultsByProviderName = new Map();

    visibleResults.forEach((result, index) => {
      const providerName = result.providerName;
      let results = visibleResultsByProviderName.get(providerName);
      if (!results) {
        results = [];
        visibleResultsByProviderName.set(providerName, results);
      }
      results.push({ index, result });
    });

    if (!details.isSessionOngoing) {
      this.#notifyImpression(
        this.providersByNotificationType.onImpression,
        state,
        queryContext,
        controller,
        visibleResultsByProviderName,
        state == "engagement" && details.result ? details : null
      );
    }

    if (state === "engagement") {
      if (details.result) {
        this.#notifyEngagement(
          this.providersByNotificationType.onEngagement,
          queryContext,
          controller,
          details
        );
      }
    } else {
      this.#notifyAbandonment(
        this.providersByNotificationType.onAbandonment,
        queryContext,
        controller,
        visibleResultsByProviderName
      );
    }

    if (!details.isSessionOngoing) {
      this.#notifySearchSessionEnd(
        this.providersByNotificationType.onSearchSessionEnd,
        queryContext,
        controller,
        details
      );
    }
  }

  #notifyEngagement(engagementProviders, queryContext, controller, details) {
    for (const provider of engagementProviders) {
      if (details.result.providerName == provider.name) {
        provider.tryMethod("onEngagement", queryContext, controller, details);
        controller.notify(lazy.UrlbarShared.NOTIFICATIONS.PROVIDER_ENGAGEMENT);
        break;
      }
    }
  }

  #notifyImpression(
    impressionProviders,
    state,
    queryContext,
    controller,
    visibleResultsByProviderName,
    details
  ) {
    for (const provider of impressionProviders) {
      const providerVisibleResults =
        visibleResultsByProviderName.get(provider.name) ?? [];

      if (providerVisibleResults.length) {
        provider.tryMethod(
          "onImpression",
          state,
          queryContext,
          controller,
          providerVisibleResults,
          details
        );
      }
    }
  }

  #notifyAbandonment(
    abandomentProviders,
    queryContext,
    controller,
    visibleResultsByProviderName
  ) {
    for (const provider of abandomentProviders) {
      if (visibleResultsByProviderName.has(provider.name)) {
        provider.tryMethod("onAbandonment", queryContext, controller);
      }
    }
  }

  #notifySearchSessionEnd(
    searchSessionEndProviders,
    queryContext,
    controller,
    details
  ) {
    for (const provider of searchSessionEndProviders) {
      provider.tryMethod(
        "onSearchSessionEnd",
        queryContext,
        controller,
        details
      );
    }
  }
}

/**
 * Tracks a query status.
 * Multiple queries can potentially be executed at the same time by different
 * controllers. Each query has to track its own status and delays separately,
 * to avoid conflicting with other ones.
 */
export class Query {
  /**
   * Initializes the query object.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query context.
   * @param {?UrlbarParentController} controller
   *   The controller to be notified. May be null.
   * @param {UrlbarMuxer} muxer
   *   The muxer to sort results.
   * @param {UrlbarProvider[]} providers
   *   Array of all the providers.
   */
  constructor(queryContext, controller, muxer, providers) {
    this.context = queryContext;
    this.context.results = [];
    // Clear any state in the context object, since it could be reused by the
    // caller and we don't want to port previous query state over.
    this.context.pendingHeuristicProviders.clear();
    this.context.deferUserSelectionProviders.clear();
    this.unsortedResults = [];
    this.muxer = muxer;
    this.controller = controller;
    this.providers = providers;
    this.started = false;
    this.canceled = false;

    // This is used as a last safety filter in add(), thus we keep an unmodified
    // copy of it.
    this.acceptableSources = queryContext.sources.slice();
  }

  /**
   * Starts querying.
   */
  async start() {
    if (this.started) {
      throw new Error("This Query has been started already");
    }
    this.started = true;

    // Check which providers should be queried by calling isActive on them.
    let activeProviders = [];
    let activePromises = [];
    let maxPriority = -1;
    for (let provider of this.providers) {
      // This can be used by the provider to check the query is still running
      // after executing async tasks:
      //   let instance = this.queryInstance;
      //   await ...
      //   if (instance != this.queryInstance) {
      //     // Query was canceled or a new one started.
      //     return;
      //   }
      provider.queryInstance = this;
      activePromises.push(
        provider
          .isActive(this.context, this.controller)
          .then(isActive => {
            if (isActive && !this.canceled) {
              let priority = provider.tryMethod("getPriority", this.context);
              if (priority >= maxPriority) {
                // The provider's priority is at least as high as the max.
                if (priority > maxPriority) {
                  // The provider's priority is higher than the max.  Remove all
                  // previously added providers, since their priority is
                  // necessarily lower, by setting length to zero.
                  activeProviders.length = 0;
                  maxPriority = priority;
                }
                activeProviders.push(provider);
                if (provider.deferUserSelection) {
                  this.context.deferUserSelectionProviders.add(provider.name);
                }
              }
            }
          })
          .catch(ex => lazy.logger.error(ex))
      );
    }

    // We have to wait for all isActive calls to finish because we want to query
    // only the highest priority active providers as determined by the priority
    // logic above.
    await Promise.all(activePromises);

    if (this.canceled) {
      this.controller = null;
      return;
    }

    // Start querying active providers.
    /**
     * @type {(provider: UrlbarProvider) => Promise<void>}
     */
    let startQuery = async provider => {
      provider.logger.debug(
        `Starting query for "${this.context.searchString}"`
      );
      let addedResult = false;
      await provider.tryMethod(
        "startQuery",
        this.context,
        /** @type {Parameters<UrlbarProvider['startQuery']>[1]} */
        (innerProvider, result) => {
          addedResult = true;
          this.add(innerProvider, result);
        },
        this.controller
      );
      if (!addedResult) {
        this.context.deferUserSelectionProviders.delete(provider.name);
      }
    };

    let queryPromises = [];
    for (let provider of activeProviders) {
      // Track heuristic providers. later we'll use this Set to wait for them
      // before returning results to the user.
      if (provider.type == lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC) {
        this.context.pendingHeuristicProviders.add(provider.name);
        queryPromises.push(
          startQuery(provider).finally(() => {
            this.context.pendingHeuristicProviders.delete(provider.name);
          })
        );
        continue;
      }
      if (!this._sleepTimer) {
        // Tracks the delay timer. We will fire (in this specific case, cancel
        // would do the same, since the callback is empty) the timer when the
        // search is canceled, unblocking start().
        this._sleepTimer = new lazy.SkippableTimer({
          name: "Query provider timer",
          time: lazy.UrlbarPrefs.get("delay"),
          logger: provider.logger,
        });
      }
      queryPromises.push(
        this._sleepTimer.promise.then(() =>
          this.canceled ? undefined : startQuery(provider)
        )
      );
    }

    lazy.logger.info(
      `Queried ${queryPromises.length} providers: ${activeProviders.map(
        p => p.name
      )}`
    );

    // Normally we wait for all the queries, but in case this is canceled we can
    // return earlier.
    let cancelPromise = new Promise(resolve => {
      this._cancelQueries = resolve;
    });
    await Promise.race([Promise.all(queryPromises), cancelPromise]);

    // All the providers are done returning results, so we can stop chunking.
    if (!this.canceled) {
      await this._chunkTimer?.fire();
    }

    // Break cycles with the controller to avoid leaks.
    this.controller = null;
  }

  /**
   * Cancels this query. Note: Invoking cancel multiple times is a no-op.
   */
  cancel() {
    if (this.canceled) {
      return;
    }
    this.canceled = true;
    this.context.deferUserSelectionProviders.clear();
    for (let provider of this.providers) {
      provider.logger.debug(
        `Canceling query for "${this.context.searchString}"`
      );
      // Mark the instance as no more valid, see start() for details.
      provider.queryInstance = null;
      provider.tryMethod("cancelQuery", this.context);
    }
    this._chunkTimer?.cancel().catch(ex => lazy.logger.error(ex));
    this._sleepTimer?.fire().catch(ex => lazy.logger.error(ex));
    this._cancelQueries?.();
  }

  /**
   * Adds a result returned from a provider to the results set.
   *
   * @param {UrlbarProvider} provider The provider that returned the result.
   * @param {UrlbarResult} result The result object.
   */
  add(provider, result) {
    if (!(provider instanceof lazy.UrlbarProvider)) {
      throw new Error("Invalid provider passed to the add callback");
    }

    // When this set is empty, we can display heuristic results early. We remove
    // the provider from the list without checking result.heuristic since
    // heuristic providers don't necessarily have to return heuristic results.
    // We expect a provider with type HEURISTIC will return its heuristic
    // result(s) first.
    this.context.pendingHeuristicProviders.delete(provider.name);

    // Stop returning results as soon as we've been canceled.
    if (this.canceled) {
      return;
    }

    // In search mode, don't allow heuristic results in the following cases
    // since they don't make sense:
    //   * When the search string is empty, or
    //   * In local search mode, except for autofill results
    if (
      result.heuristic &&
      this.context.searchMode &&
      (!this.context.trimmedSearchString ||
        (!this.context.searchMode.engineName && !result.autofill))
    ) {
      return;
    }

    // Check if the result source should be filtered out. Pay attention to the
    // heuristic result though, that is supposed to be added regardless.
    if (
      !this.acceptableSources.includes(result.source) &&
      !result.heuristic &&
      // Treat form history as searches for the purpose of acceptableSources.
      (result.type != lazy.UrlbarShared.RESULT_TYPE.SEARCH ||
        result.source != lazy.UrlbarShared.RESULT_SOURCE.HISTORY ||
        !this.acceptableSources.includes(
          lazy.UrlbarShared.RESULT_SOURCE.SEARCH
        )) &&
      // To enable tab group search in tabs mode, allow actions to bypass
      // acceptableSources.
      !(
        result.source == lazy.UrlbarShared.RESULT_SOURCE.ACTIONS &&
        this.acceptableSources.includes(lazy.UrlbarShared.RESULT_SOURCE.TABS)
      )
    ) {
      return;
    }

    // Filter out javascript results for safety. The provider is supposed to do
    // it, but we don't want to risk leaking these out.
    if (
      result.type != lazy.UrlbarShared.RESULT_TYPE.KEYWORD &&
      result.payload.url &&
      result.payload.url.startsWith("javascript:") &&
      !this.context.searchString.startsWith("javascript:") &&
      lazy.UrlbarPrefs.get("filter.javascript")
    ) {
      return;
    }

    result.id = gNextResultId++;
    result.providerName = provider.name;
    result.providerType = provider.type;

    // Pre-compute the view's per-result data now so the view can read it
    // synchronously later without calling back into the provider, which may
    // live in another process.
    if (result.type == lazy.UrlbarShared.RESULT_TYPE.DYNAMIC) {
      result.payload.viewTemplate = provider.getViewTemplate(result);
      result.payload.viewUpdate = provider.getViewUpdate(result);
    }
    let commands = provider.tryMethod(
      "getResultCommands",
      result,
      this.context.isPrivate
    );
    if (commands) {
      result.commands = commands;
    }

    if (result.payload.url) {
      result.isSERP = lazy.UrlbarSearchUtils.resultIsSERP(result);
    }

    this.unsortedResults.push(result);

    this._notifyResultsFromProvider(provider);
  }

  _notifyResultsFromProvider(provider) {
    // We use a timer to reduce UI flicker, by adding results in chunks.
    if (!this._chunkTimer || this._chunkTimer.done) {
      // Either there's no heuristic provider pending at all, or the previous
      // timer is done, but we're still getting results. Start a short timer
      // to chunk remaining results.
      this._chunkTimer = new lazy.SkippableTimer({
        name: "chunking",
        callback: () => this._notifyResults(),
        time: ProvidersManager.chunkResultsDelayMs,
        logger: provider.logger,
      });
    } else if (
      !this.context.pendingHeuristicProviders.size &&
      provider.type == lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC
    ) {
      // All the active heuristic providers have returned results, we can skip
      // the heuristic chunk timer and start showing results immediately.
      this._chunkTimer.fire().catch(ex => lazy.logger.error(ex));
    }

    // Otherwise some timer is still ongoing and we'll wait for it.
  }

  _notifyResults() {
    this.muxer.sort(this.context, this.unsortedResults);
    // We don't want to notify consumers if there are no results since they
    // generally expect at least one result when notified, so bail, but only
    // after nulling out the chunk timer above so that it will be restarted
    // the next time results are added.
    if (!this.context.results.length) {
      return;
    }

    this.context.firstResultChanged = !lazy.ObjectUtils.deepEqual(
      this.context.firstResult,
      this.context.results[0]
    );
    this.context.firstResult = this.context.results[0];

    if (this.controller) {
      this.controller.receiveResults(this.context);
    }
  }

  /**
   * Returns the provider with the given name.
   *
   * @param {string} name
   *   The provider name.
   * @returns {UrlbarProvider | undefined}
   *   The provider.
   */
  getProvider(name) {
    return this.providers.find(p => p.name == name);
  }
}

/**
 * Updates in place the sources for a given UrlbarQueryContext.
 *
 * @param {UrlbarQueryContext} context The query context to examine
 * @returns {UrlbarSearchStringTokenData|undefined} The restriction token that
 *   was used to set sources, or undefined if there's no restriction token.
 */
function updateSourcesIfEmpty(context) {
  if (context.sources && context.sources.length) {
    return undefined;
  }
  let acceptedSources = [];
  // There can be only one restrict token per query.
  let restrictToken =
    context.sapName != "urlbar"
      ? undefined
      : context.tokens.find(t =>
          [
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_HISTORY,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_BOOKMARK,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_TAG,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_OPENPAGE,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_SEARCH,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_TITLE,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_URL,
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_ACTION,
          ].includes(t.type)
        );

  // RESTRICT_TITLE and RESTRICT_URL do not affect query sources.
  let restrictTokenType =
    restrictToken &&
    restrictToken.type != lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_TITLE &&
    restrictToken.type != lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_URL
      ? restrictToken.type
      : undefined;

  for (let source of Object.values(lazy.UrlbarShared.RESULT_SOURCE)) {
    // Check prefs and restriction tokens.
    switch (source) {
      case lazy.UrlbarShared.RESULT_SOURCE.BOOKMARKS:
        if (
          restrictTokenType ===
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_BOOKMARK ||
          restrictTokenType === lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_TAG ||
          (!restrictTokenType && lazy.UrlbarPrefs.get("suggest.bookmark"))
        ) {
          acceptedSources.push(source);
        }
        break;
      case lazy.UrlbarShared.RESULT_SOURCE.HISTORY:
        if (
          restrictTokenType === lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_HISTORY ||
          (!restrictTokenType && lazy.UrlbarPrefs.get("suggest.history"))
        ) {
          acceptedSources.push(source);
        }
        break;
      case lazy.UrlbarShared.RESULT_SOURCE.SEARCH:
        if (
          restrictTokenType === lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_SEARCH ||
          !restrictTokenType
        ) {
          // We didn't check browser.urlbar.suggest.searches here, because it
          // just controls search suggestions. If a search suggestion arrives
          // here, we lost already, because we broke user's privacy by hitting
          // the network. Thus, it's better to leave things go through and
          // notice the bug, rather than hiding it with a filter.
          acceptedSources.push(source);
        }
        break;
      case lazy.UrlbarShared.RESULT_SOURCE.TABS:
        if (
          restrictTokenType ===
            lazy.UrlbarShared.TOKEN_TYPE.RESTRICT_OPENPAGE ||
          (!restrictTokenType && lazy.UrlbarPrefs.get("suggest.openpage"))
        ) {
          acceptedSources.push(source);
        }
        break;
      case lazy.UrlbarShared.RESULT_SOURCE.OTHER_NETWORK:
        if (!context.isPrivate && !restrictTokenType) {
          acceptedSources.push(source);
        }
        break;
      case lazy.UrlbarShared.RESULT_SOURCE.OTHER_LOCAL:
      case lazy.UrlbarShared.RESULT_SOURCE.ADDON:
      default:
        if (!restrictTokenType) {
          acceptedSources.push(source);
        }
        break;
    }
  }
  context.sources = acceptedSources;
  return restrictToken;
}
