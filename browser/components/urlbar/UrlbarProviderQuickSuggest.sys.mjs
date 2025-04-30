/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  UrlbarProvider,
  UrlbarUtils,
} from "resource:///modules/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentRelevancyManager:
    "resource://gre/modules/ContentRelevancyManager.sys.mjs",
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  SearchUtils: "resource://gre/modules/SearchUtils.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarProviderSearchSuggestions:
    "resource:///modules/UrlbarProviderSearchSuggestions.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarSearchUtils: "resource:///modules/UrlbarSearchUtils.sys.mjs",
});

// Used for suggestions that don't otherwise have a score.
const DEFAULT_SUGGESTION_SCORE = 0.2;

/**
 * A provider that returns a suggested url to the user based on what
 * they have currently typed so they can navigate directly.
 */
class ProviderQuickSuggest extends UrlbarProvider {
  /**
   * Returns the name of this provider.
   *
   * @returns {string} the name of this provider.
   */
  get name() {
    return "UrlbarProviderQuickSuggest";
  }

  /**
   * The type of the provider.
   *
   * @returns {UrlbarUtils.PROVIDER_TYPE}
   */
  get type() {
    return UrlbarUtils.PROVIDER_TYPE.NETWORK;
  }

  /**
   * @returns {number}
   *   The default score for suggestions that don't otherwise have one. All
   *   suggestions require scores so they can be ranked. Scores are numeric
   *   values in the range [0, 1].
   */
  get DEFAULT_SUGGESTION_SCORE() {
    return DEFAULT_SUGGESTION_SCORE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   * @returns {boolean} Whether this provider should be invoked for the search.
   */
  isActive(queryContext) {
    // If the sources don't include search or the user used a restriction
    // character other than search, don't allow any suggestions.
    if (
      !queryContext.sources.includes(UrlbarUtils.RESULT_SOURCE.SEARCH) ||
      (queryContext.restrictSource &&
        queryContext.restrictSource != UrlbarUtils.RESULT_SOURCE.SEARCH)
    ) {
      return false;
    }

    if (
      !lazy.UrlbarPrefs.get("quickSuggestEnabled") ||
      queryContext.isPrivate ||
      queryContext.searchMode
    ) {
      return false;
    }

    // Trim only the start of the search string because a trailing space can
    // affect the suggestions.
    let trimmedSearchString = queryContext.searchString.trimStart();

    // Per product requirements, at least two characters must be typed to
    // trigger a Suggest suggestion. Suggestion keywords should always be at
    // least two characters long, but we check here anyway to be safe. Note we
    // called `trimStart()` above, so we only call `trimEnd()` here.
    if (trimmedSearchString.trimEnd().length < 2) {
      return false;
    }
    this._trimmedSearchString = trimmedSearchString;
    return true;
  }

  /**
   * Starts querying. Extended classes should return a Promise resolved when the
   * provider is done searching AND returning results.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   * @param {Function} addCallback Callback invoked by the provider to add a new
   *        result. A UrlbarResult should be passed to it.
   * @returns {Promise}
   */
  async startQuery(queryContext, addCallback) {
    let instance = this.queryInstance;
    let searchString = this._trimmedSearchString;

    // Fetch suggestions from all enabled backends.
    let values = await Promise.all(
      lazy.QuickSuggest.enabledBackends.map(backend =>
        backend.query(searchString, { queryContext })
      )
    );
    if (instance != this.queryInstance) {
      return;
    }

    let suggestions = await this.#filterAndSortSuggestions(values.flat());
    if (instance != this.queryInstance) {
      return;
    }

    // Convert each suggestion into a result and add it. Don't add more than
    // `maxResults` visible results so we don't spam the muxer.
    let remainingCount = queryContext.maxResults ?? 10;
    for (let suggestion of suggestions) {
      if (!remainingCount) {
        break;
      }

      let result = await this.#makeResult(queryContext, suggestion);
      if (instance != this.queryInstance) {
        return;
      }
      if (result) {
        let canAdd = await this.#canAddResult(result);
        if (instance != this.queryInstance) {
          return;
        }
        if (canAdd) {
          addCallback(this, result);
          if (!result.isHiddenExposure) {
            remainingCount--;
          }
        }
      }
    }
  }

  async #filterAndSortSuggestions(suggestions) {
    let requiredKeys = ["source", "provider"];
    let scoreMap = lazy.UrlbarPrefs.get("quickSuggestScoreMap");
    let suggestionsByFeature = new Map();
    let indexesBySuggestion = new Map();

    for (let i = 0; i < suggestions.length; i++) {
      let suggestion = suggestions[i];

      // Discard the suggestion if it doesn't have the properties required to
      // get the feature that manages it. Each backend should set these, so this
      // should never happen.
      if (!requiredKeys.every(key => suggestion[key])) {
        this.logger.error("Suggestion is missing one or more required keys", {
          requiredKeys,
          suggestion,
        });
        continue;
      }

      // Ensure the suggestion has a score.
      //
      // Step 1: Set a default score if the suggestion doesn't have one.
      if (typeof suggestion.score != "number" || isNaN(suggestion.score)) {
        suggestion.score = DEFAULT_SUGGESTION_SCORE;
      }

      // Step 2: Apply relevancy ranking. For now we only do this for Merino
      // suggestions, but we may expand it in the future.
      if (suggestion.source == "merino") {
        await this.#applyRanking(suggestion);
      }

      // Step 3: Apply score overrides defined in `quickSuggestScoreMap`. It
      // maps telemetry types to scores.
      if (scoreMap) {
        let telemetryType = this.#getSuggestionTelemetryType(suggestion);
        if (scoreMap.hasOwnProperty(telemetryType)) {
          let score = parseFloat(scoreMap[telemetryType]);
          if (!isNaN(score)) {
            suggestion.score = score;
          }
        }
      }

      // Save some state used below to build the final list of suggestions.
      // `feature` will be null if the suggestion isn't managed by one.
      let feature = lazy.QuickSuggest.getFeatureBySource(suggestion);
      let featureSuggestions = suggestionsByFeature.get(feature);
      if (!featureSuggestions) {
        featureSuggestions = [];
        suggestionsByFeature.set(feature, featureSuggestions);
      }
      featureSuggestions.push(suggestion);
      indexesBySuggestion.set(suggestion, i);
    }

    // Let each feature filter its suggestions.
    let filteredSuggestions = (
      await Promise.all(
        [...suggestionsByFeature].map(([feature, featureSuggestions]) =>
          feature
            ? feature.filterSuggestions(featureSuggestions)
            : Promise.resolve(featureSuggestions)
        )
      )
    ).flat();

    // Sort the suggestions. When scores are equal, sort by original index to
    // ensure a stable sort.
    filteredSuggestions.sort((a, b) => {
      return (
        b.score - a.score ||
        indexesBySuggestion.get(a) - indexesBySuggestion.get(b)
      );
    });

    return filteredSuggestions;
  }

  onImpression(state, queryContext, controller, resultsAndIndexes, details) {
    // Build a map from each feature to its results in `resultsAndIndexes`.
    let resultsByFeature = resultsAndIndexes.reduce((memo, { result }) => {
      let feature = lazy.QuickSuggest.getFeatureByResult(result);
      if (feature) {
        let featureResults = memo.get(feature);
        if (!featureResults) {
          featureResults = [];
          memo.set(feature, featureResults);
        }
        featureResults.push(result);
      }
      return memo;
    }, new Map());

    // Notify each feature with its results.
    for (let [feature, featureResults] of resultsByFeature) {
      feature.onImpression(
        state,
        queryContext,
        controller,
        featureResults,
        details
      );
    }
  }

  onEngagement(queryContext, controller, details) {
    let { result } = details;

    // Delegate to the result's feature if there is one.
    let feature = lazy.QuickSuggest.getFeatureByResult(details.result);
    if (feature) {
      feature.onEngagement(
        queryContext,
        controller,
        details,
        this._trimmedSearchString
      );
      return;
    }

    // Otherwise, handle commands. The dismiss, manage, and help commands are
    // supported for results without features. Dismissal is the only one we need
    // to handle here since urlbar handles the others.
    if (details.selType == "dismiss" && result.payload.isBlockable) {
      // `dismissResult()` is async but there's no need to await it here.
      lazy.QuickSuggest.dismissResult(result);
      controller.removeResult(result);
    }
  }

  onSearchSessionEnd(queryContext, controller, details) {
    for (let backend of lazy.QuickSuggest.enabledBackends) {
      backend.onSearchSessionEnd(queryContext, controller, details);
    }
  }

  /**
   * This is called only for dynamic result types, when the urlbar view updates
   * the view of one of the results of the provider.  It should return an object
   * describing the view update.
   *
   * @param {UrlbarResult} result The result whose view will be updated.
   * @returns {object} An object describing the view update.
   */
  getViewUpdate(result) {
    return lazy.QuickSuggest.getFeatureByResult(result)?.getViewUpdate?.(
      result
    );
  }

  getResultCommands(result) {
    return lazy.QuickSuggest.getFeatureByResult(result)?.getResultCommands?.(
      result
    );
  }

  /**
   * Returns the telemetry type for a suggestion. A telemetry type uniquely
   * identifies a type of suggestion as well as the kind of `UrlbarResult`
   * instances created from it.
   *
   * @param {object} suggestion
   *   A suggestion from a Suggest backend.
   * @returns {string}
   *   The telemetry type. If the suggestion type is managed by a feature, the
   *   telemetry type is retrieved from it. Otherwise the suggestion type is
   *   assumed to come from Merino, and `suggestion.provider` (the Merino
   *   provider name) is returned.
   */
  #getSuggestionTelemetryType(suggestion) {
    let feature = lazy.QuickSuggest.getFeatureBySource(suggestion);
    if (feature) {
      return feature.getSuggestionTelemetryType(suggestion);
    }
    return suggestion.provider;
  }

  async #makeResult(queryContext, suggestion) {
    let result = null;
    let feature = lazy.QuickSuggest.getFeatureBySource(suggestion);
    if (!feature) {
      result = this.#makeUnmanagedResult(queryContext, suggestion);
    } else if (feature.isEnabled) {
      result = await feature.makeResult(
        queryContext,
        suggestion,
        this._trimmedSearchString
      );
    }

    if (!result) {
      return null;
    }

    // Set important properties that every Suggest result should have. See
    // `QuickSuggest.getFeatureBySource()` for `source` and `provider` values.
    // If the suggestion isn't managed by a feature, then it's from Merino and
    // `is_sponsored` is true if it's sponsored. (Merino uses snake_case.)
    result.payload.source = suggestion.source;
    result.payload.provider = suggestion.provider;
    result.payload.telemetryType = this.#getSuggestionTelemetryType(suggestion);
    result.payload.isSponsored = feature
      ? feature.isSuggestionSponsored(suggestion)
      : !!suggestion.is_sponsored;
    if (suggestion.source == "rust") {
      // `suggestionObject` is passed back into the Rust component on dismissal.
      result.payload.suggestionObject = suggestion;
    }

    // Handle icons here so each feature doesn't have to do it, but use `||=` to
    // let them do it if they need to.
    result.payload.icon ||= suggestion.icon;
    result.payload.iconBlob ||= suggestion.icon_blob;

    // Set the appropriate suggested index and related properties unless the
    // feature did it already.
    if (!result.hasSuggestedIndex) {
      if (result.isBestMatch) {
        result.isRichSuggestion = true;
        result.richSuggestionIconSize ||= 52;
        result.suggestedIndex = 1;
      } else {
        result.isSuggestedIndexRelativeToGroup = true;
        if (!result.payload.isSponsored) {
          result.suggestedIndex = lazy.UrlbarPrefs.get(
            "quickSuggestNonSponsoredIndex"
          );
        } else if (
          lazy.UrlbarPrefs.get("showSearchSuggestionsFirst") &&
          lazy.UrlbarProviderSearchSuggestions.isActive(queryContext) &&
          lazy.UrlbarSearchUtils.getDefaultEngine(
            queryContext.isPrivate
          ).supportsResponseType(lazy.SearchUtils.URL_TYPE.SUGGEST_JSON)
        ) {
          // Allow sponsored suggestions to be shown somewhere other than the
          // bottom of the Suggest section (-1, the `else` branch below) only if
          // search suggestions are shown first, the search suggestions provider
          // is active for the current context (it will not be active if search
          // suggestions are disabled, among other reasons), and the default
          // engine supports suggestions.
          result.suggestedIndex = lazy.UrlbarPrefs.get(
            "quickSuggestSponsoredIndex"
          );
        } else {
          result.suggestedIndex = -1;
        }
      }
    }

    return result;
  }

  /**
   * Returns a new result for an unmanaged suggestion. An "unmanaged" suggestion
   * is a suggestion without a feature.
   *
   * Merino is the only backend allowed to serve unmanaged suggestions, for a
   * couple of reasons: (1) Some suggestion types aren't that complicated and
   * can be handled in a default manner, for example dynamic Wikipedia
   * suggestions. (2) It allows us to experiment with new suggestion types
   * without requiring any changes to Firefox.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query context.
   * @param {object} suggestion
   *   The suggestion.
   * @returns {UrlbarResult|null}
   *   A new result for the suggestion or null if the suggestion is not from
   *   Merino.
   */
  #makeUnmanagedResult(queryContext, suggestion) {
    if (suggestion.source != "merino") {
      return null;
    }

    // Note that Merino uses snake_case keys.
    let payload = {
      url: suggestion.url,
      originalUrl: suggestion.original_url,
      dismissalKey: suggestion.dismissal_key,
      isBlockable: true,
      isManageable: true,
    };

    if (suggestion.full_keyword) {
      payload.title = suggestion.title;
      payload.qsSuggestion = [
        suggestion.full_keyword,
        UrlbarUtils.HIGHLIGHT.SUGGESTED,
      ];
    } else {
      payload.title = [suggestion.title, UrlbarUtils.HIGHLIGHT.TYPED];
      payload.shouldShowUrl = true;
    }

    return Object.assign(
      new lazy.UrlbarResult(
        UrlbarUtils.RESULT_TYPE.URL,
        UrlbarUtils.RESULT_SOURCE.SEARCH,
        ...lazy.UrlbarResult.payloadAndSimpleHighlights(
          queryContext.tokens,
          payload
        )
      ),
      {
        isBestMatch: !!suggestion.is_top_pick,
      }
    );
  }

  /**
   * Cancels the current query.
   */
  cancelQuery() {
    for (let backend of lazy.QuickSuggest.enabledBackends) {
      backend.cancelQuery();
    }
  }

  /**
   * Applies relevancy ranking to a suggestion by updating its score.
   *
   * @param {object} suggestion
   *   The suggestion to be ranked.
   */
  async #applyRanking(suggestion) {
    let oldScore = suggestion.score;

    let mode = lazy.UrlbarPrefs.get("quickSuggestRankingMode");
    switch (mode) {
      case "random":
        suggestion.score = Math.random();
        break;
      case "interest":
        await this.#updateScoreByRelevance(suggestion);
        break;
      case "default":
      default:
        // Do nothing.
        return;
    }

    this.logger.debug("Applied ranking to suggestion score", {
      mode,
      oldScore,
      newScore: suggestion.score.toFixed(3),
    });
  }

  /**
   * Update score by interest-based relevance scoring. The final score is a mean
   * between the interest-based score and the default static score, which means
   * if the former is 0 or less than the latter, the combined score will be less
   * than the static score.
   *
   * @param {object} suggestion
   *   The suggestion to be ranked.
   */
  async #updateScoreByRelevance(suggestion) {
    if (!suggestion.categories?.length) {
      return;
    }

    let score;
    try {
      score = await lazy.ContentRelevancyManager.score(
        suggestion.categories,
        true // adjustment needed b/c Merino uses the original encoding
      );
    } catch (error) {
      Glean.suggestRelevance.status.failure.add(1);
      this.logger.error("Error updating suggestion score", error);
      return;
    }

    Glean.suggestRelevance.status.success.add(1);
    let oldScore = suggestion.score;
    suggestion.score = (oldScore + score) / 2;
    Glean.suggestRelevance.outcome[
      suggestion.score >= oldScore ? "boosted" : "decreased"
    ].add(1);
  }

  /**
   * Returns whether a given result can be added for a query, assuming the
   * provider itself should be active.
   *
   * @param {UrlbarResult} result
   *   The result to check.
   * @returns {boolean}
   *   Whether the result can be added.
   */
  async #canAddResult(result) {
    // Discard the result if it's not managed by a feature and its sponsored
    // state isn't allowed.
    //
    // This isn't necessary when the result is managed because in that case: If
    // its feature is disabled, we didn't create a result in the first place; if
    // its feature is enabled, we delegate responsibility to it for either
    // creating or not creating its results.
    //
    // Also note that it's possible for suggestion types to be considered
    // neither sponsored nor nonsponsored. In other words, the decision to add
    // them or not does not depend on the prefs in this conditional. Such types
    // should always be managed. Exposure suggestions are an example.
    let feature = lazy.QuickSuggest.getFeatureByResult(result);
    if (
      !feature &&
      ((result.payload.isSponsored &&
        !lazy.UrlbarPrefs.get("suggest.quicksuggest.sponsored")) ||
        (!result.payload.isSponsored &&
          !lazy.UrlbarPrefs.get("suggest.quicksuggest.nonsponsored")))
    ) {
      return false;
    }

    // Discard the result if it was dismissed.
    if (await lazy.QuickSuggest.isResultDismissed(result)) {
      this.logger.debug("Suggestion dismissed, not adding it");
      return false;
    }

    return true;
  }

  async _test_applyRanking(suggestion) {
    await this.#applyRanking(suggestion);
  }
}

export var UrlbarProviderQuickSuggest = new ProviderQuickSuggest();
