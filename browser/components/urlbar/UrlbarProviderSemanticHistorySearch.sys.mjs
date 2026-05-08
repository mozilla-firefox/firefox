/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that offers search history suggestions
 * based on embeddings and semantic search techniques using semantic
 * history
 */

import {
  UrlbarProvider,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EnrollmentType: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarResult: "moz-src:///browser/components/urlbar/UrlbarResult.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", function () {
  return UrlbarUtils.getLogger({ prefix: "SemanticHistorySearch" });
});

/**
 * Lazily creates (on first call) and returns the
 * {@link PlacesSemanticHistoryManager} instance backing this provider.
 */
ChromeUtils.defineLazyGetter(lazy, "semanticManager", function () {
  let { getPlacesSemanticHistoryManager } = ChromeUtils.importESModule(
    "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
  );
  const distanceThreshold = Services.prefs.getFloatPref(
    "places.semanticHistory.distanceThreshold",
    0.6
  );
  return getPlacesSemanticHistoryManager({
    rowLimit: 10000,
    samplingAttrib: "frecency",
    changeThresholdCount: 3,
    distanceThreshold,
  });
});

/**
 * @typedef {ReturnType<import("resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs").getPlacesSemanticHistoryManager>} PlacesSemanticHistoryManager
 */

/**
 * Class representing the Semantic History Search provider for the URL bar.
 *
 * This provider queries a semantic database created using history.
 * It performs semantic search using embeddings generated
 * by an ML model and retrieves results ranked by cosine similarity to the
 * query's embedding.
 *
 * @class
 */
export class UrlbarProviderSemanticHistorySearch extends UrlbarProvider {
  /** @type {boolean} */
  static #exposureRecorded;

  /**
   * Provides a shared instance of the semantic manager, so that other consumers
   * won't wrongly initialize it with different parameters.
   *
   * @returns {PlacesSemanticHistoryManager}
   *   The semantic manager instance used by this provider.
   */
  static get semanticManager() {
    return lazy.semanticManager;
  }

  /**
   * @returns {Values<typeof UrlbarUtils.PROVIDER_TYPE>}
   */
  get type() {
    return UrlbarUtils.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Determines if the provider is active for the given query context.
   *
   * @param {object} queryContext
   *   The context of the query, including the search string.
   */
  async isActive(queryContext) {
    const minSearchStringLength = lazy.UrlbarPrefs.get(
      "suggest.semanticHistory.minLength"
    );
    if (
      lazy.UrlbarPrefs.get("suggest.history") &&
      queryContext.searchString.length >= minSearchStringLength &&
      (!queryContext.searchMode ||
        queryContext.searchMode.source == UrlbarUtils.RESULT_SOURCE.HISTORY)
    ) {
      // The smartbar (SW-only surface) is gated on the SW pref so it can light
      // up independently of the CW feature gate; all other surfaces (urlbar in
      // either window, searchbar, etc.) stay on the CW gate so urlbar behavior
      // is consistent across CW and SW.
      const canUse =
        queryContext.sapName === "smartbar"
          ? lazy.semanticManager.isEnabledForSmartWindow
          : lazy.semanticManager.canUseSemanticSearch;
      if (canUse) {
        // Proceed only if a sufficient number of history entries have
        // embeddings calculated.
        return lazy.semanticManager.hasSufficientEntriesForSearching();
      }
    }
    return false;
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    let instance = this.queryInstance;
    let resultObject = await lazy.semanticManager.infer(queryContext);
    this.#maybeRecordExposure();
    let results = resultObject.results;
    if (!results || instance != this.queryInstance) {
      return;
    }

    let openTabs = lazy.UrlbarProviderOpenTabs.getOpenTabUrls(
      queryContext.isPrivate
    );
    for (let res of results) {
      if (
        !this.#addAsSwitchToTab(
          openTabs.get(res.url),
          queryContext,
          res,
          addCallback
        )
      ) {
        const result = new lazy.UrlbarResult({
          type: UrlbarUtils.RESULT_TYPE.URL,
          source: UrlbarUtils.RESULT_SOURCE.HISTORY,
          payload: {
            title: res.title,
            url: res.url,
            icon: UrlbarUtils.getIconForUrl(res.url),
            isBlockable: true,
            blockL10n: { id: "urlbar-result-menu-remove-from-history" },
            helpUrl:
              Services.urlFormatter.formatURLPref("app.support.baseURL") +
              "awesome-bar-result-menu",
            frecency: res.frecency,
          },
        });
        addCallback(this, result);
      }
    }
  }

  /**
   * Check if the url is open in tabs, and adds one or multiple switch to tab
   * results if so.
   *
   * @param {Set<[number, string]>|undefined} openTabs
   *  Tabs open for the result URL, may be undefined.
   * @param {object} queryContext
   *  The query context, including the search string.
   * @param {object} res
   * The result object containing the URL.
   * @param {Function} addCallback
   *  Callback to add results to the URL bar.
   * @returns {boolean} True if a switch to tab result was added.
   */
  #addAsSwitchToTab(openTabs, queryContext, res, addCallback) {
    if (!openTabs?.size) {
      return false;
    }

    let userContextId =
      lazy.UrlbarProviderOpenTabs.getUserContextIdForOpenPagesTable(
        queryContext.userContextId,
        queryContext.isPrivate
      );

    let added = false;
    for (let [tabUserContextId, tabGroupId] of openTabs) {
      // Don't return a switch to tab result for the current page.
      if (
        res.url == queryContext.currentPage &&
        userContextId == tabUserContextId &&
        queryContext.tabGroup === tabGroupId
      ) {
        continue;
      }
      let result = new lazy.UrlbarResult({
        type: UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
        source: UrlbarUtils.RESULT_SOURCE.TABS,
        payload: {
          url: res.url,
          title: res.title,
          icon: UrlbarUtils.getIconForUrl(res.url),
          userContextId: tabUserContextId,
          tabGroup: tabGroupId,
          lastVisit: res.lastVisit,
          action: lazy.UrlbarPrefs.get("secondaryActions.switchToTab")
            ? UrlbarUtils.createTabSwitchSecondaryAction(tabUserContextId)
            : undefined,
        },
      });
      addCallback(this, result);
      added = true;
    }
    return added;
  }

  /**
   * Records an exposure event for the semantic-history feature-gate, but
   * **only once per profile**.  Subsequent calls are ignored.
   */
  #maybeRecordExposure() {
    // Skip if we already recorded or if the gate is manually turned off.
    if (UrlbarProviderSemanticHistorySearch.#exposureRecorded) {
      return;
    }

    // Look up our enrollment (experiment or rollout). If no slug, we’re not enrolled.
    let metadata =
      lazy.NimbusFeatures.urlbar.getEnrollmentMetadata(
        lazy.EnrollmentType.EXPERIMENT
      ) ||
      lazy.NimbusFeatures.urlbar.getEnrollmentMetadata(
        lazy.EnrollmentType.ROLLOUT
      );
    if (!metadata?.slug) {
      // Not part of any semantic-history experiment/rollout → nothing to record
      return;
    }

    try {
      // Actually send it once with the slug.
      lazy.NimbusFeatures.urlbar.recordExposureEvent({
        once: true,
        slug: metadata.slug,
      });
      UrlbarProviderSemanticHistorySearch.#exposureRecorded = true;
      lazy.logger.debug(
        `Nimbus exposure event sent (semanticHistory: ${metadata.slug}).`
      );
    } catch (ex) {
      lazy.logger.warn("Unable to record semantic-history exposure event:", ex);
    }
  }

  /**
   * Gets the priority of this provider relative to other providers.
   *
   * @returns {number} The priority of this provider.
   */
  getPriority() {
    return 0;
  }

  onEngagement(queryContext, controller, details) {
    let { result } = details;
    if (details.selType == "dismiss") {
      // Remove browsing history entries from Places.
      lazy.PlacesUtils.history.remove(result.payload.url).catch(console.error);
      controller.removeResult(result);
    }
  }
}
