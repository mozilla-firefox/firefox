/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentRelevancyManager:
    "resource://gre/modules/ContentRelevancyManager.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
});

// Used for suggestions that don't otherwise have a score.
const DEFAULT_SUGGESTION_SCORE = 0.2;

/**
 * A provider that returns a suggested url to the user based on what
 * they have currently typed so they can navigate directly.
 */
export class UrlbarProviderQuickSuggest extends UrlbarProvider {
  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.NETWORK;
  }

  /**
   * @returns {number}
   *   The default score for suggestions that don't otherwise have one. All
   *   suggestions require scores so they can be ranked. Scores are numeric
   *   values in the range [0, 1].
   */
  static get DEFAULT_SUGGESTION_SCORE() {
    return DEFAULT_SUGGESTION_SCORE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    // If the sources don't include search or the user used a restriction
    // character other than search, don't allow any suggestions.
    if (
      !queryContext.sources.includes(lazy.UrlbarShared.RESULT_SOURCE.SEARCH) ||
      (queryContext.restrictSource &&
        queryContext.restrictSource != lazy.UrlbarShared.RESULT_SOURCE.SEARCH)
    ) {
      return false;
    }

    if (
      !lazy.UrlbarPrefs.get("quickSuggestEnabled") ||
      queryContext.isPrivate ||
      queryContext.restrictInSearchMode()
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
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
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

      // Step 2: Apply relevancy ranking.
      await this.#applyRanking(suggestion);

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
    let feature = lazy.QuickSuggest.getFeatureByResult(result);
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
   * This is called only for dynamic result types.
   *
   * @param {UrlbarResult} result The result whose view will be updated.
   * @returns {object} An object of view template.
   */
  getViewTemplate(result) {
    return lazy.QuickSuggest.getFeatureByResult(result)?.getViewTemplate?.(
      result
    );
  }

  /**
   * This is called only for dynamic result types.
   *
   * @param {UrlbarResult} result The result whose view will be updated.
   * @returns {object} An object describing the view update.
   */
  getViewUpdate(result) {
    return lazy.QuickSuggest.getFeatureByResult(result)?.getViewUpdate?.(
      result
    );
  }

  /**
   * Gets the list of commands that should be shown in the result menu for a
   * given result from the provider. All commands returned by this method should
   * be handled by implementing `onEngagement()` with the possible exception of
   * commands automatically handled by the urlbar, like "help".
   *
   * @param {UrlbarResult} result
   *   The menu will be shown for this result.
   */
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

    // Set important properties that every Suggest result should have.

    // `source` indicates the Suggest backend the suggestion came from.
    result.payload.source = suggestion.source;

    // `provider` depends on `source` and generally indicates the type of
    // Suggest suggestion. See `QuickSuggest.getFeatureBySource()`.
    result.payload.provider = suggestion.provider;

    // Set `isSponsored` unless the feature already did.
    if (!result.payload.hasOwnProperty("isSponsored")) {
      result.payload.isSponsored = !!feature?.isSuggestionSponsored(suggestion);
    }

    // For most Suggest results, the result type recorded in urlbar telemetry is
    // `${source}_${telemetryType}` (the payload values).
    result.payload.telemetryType = this.#getSuggestionTelemetryType(suggestion);

    // Handle icons here unless the feature already did.
    result.payload.icon ||= suggestion.icon;
    result.payload.iconBlob ||= suggestion.icon_blob;

    switch (suggestion.source) {
      case "merino":
        // Dismissals of Merino suggestions are recorded in the Rust component's
        // database. Each dismissal is recorded as a string value called a key.
        // If Merino includes `dismissal_key` in the suggestion, use that as the
        // key. Otherwise we'll use its URL. See `QuickSuggest.dismissResult()`.
        if (
          suggestion.dismissal_key &&
          !result.payload.hasOwnProperty("dismissalKey")
        ) {
          result.payload.dismissalKey = suggestion.dismissal_key;
        }
        break;
      case "rust":
        // `suggestionObject` is passed back to the Rust component on dismissal.
        // See `QuickSuggest.dismissResult()`.
        result.payload.suggestionObject = suggestion;
        // `suggestionType` is defined only for dynamic Rust suggestions and is
        // the dynamic type. Don't add an undefined property to other payloads.
        if (suggestion.suggestionType) {
          result.payload.suggestionType = suggestion.suggestionType;
        }
        break;
    }

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
          (await this.queryInstance
            .getProvider("UrlbarProviderSearchSuggestions")
            ?.isActive(queryContext, this.queryInstance.controller)) &&
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
   * Merino is the only backend allowed to serve unmanaged suggestions, and its
   * "top_picks" provider is the only Merino provider recognized by this method.
   * For everything else, the method returns null.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query context.
   * @param {object} suggestion
   *   The suggestion.
   * @returns {UrlbarResult|null}
   *   A new result for the suggestion or null if the suggestion is not from
   *   the Merino "top_picks" provider.
   */
  #makeUnmanagedResult(queryContext, suggestion) {
    if (suggestion.source != "merino" || suggestion.provider != "top_picks") {
      return null;
    }

    // Note that Merino uses snake_case keys.
    let payload = {
      url: suggestion.url,
      originalUrl: suggestion.original_url,
      isSponsored: !!suggestion.is_sponsored,
      isBlockable: true,
      isManageable: true,
    };

    let titleHighlights;
    if (suggestion.full_keyword) {
      let { value, highlights } =
        lazy.QuickSuggest.getFullKeywordTitleAndHighlights({
          tokens: queryContext.tokens,
          highlightType: lazy.UrlbarShared.HIGHLIGHT.SUGGESTED,
          fullKeyword: suggestion.full_keyword,
          title: suggestion.title,
        });
      payload.title = value;
      titleHighlights = highlights;
    } else {
      payload.title = suggestion.title;
      titleHighlights = lazy.UrlbarShared.HIGHLIGHT.TYPED;
      payload.shouldShowUrl = true;
    }

    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.URL,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      isBestMatch: !!suggestion.is_top_pick,
      payload,
      highlights: {
        title: titleHighlights,
      },
    });
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
      score = await lazy.ContentRelevancyManager.score(suggestion.categories);
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
   * @returns {Promise<boolean>}
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
      (!lazy.UrlbarPrefs.get("suggest.quicksuggest.all") ||
        (result.payload.isSponsored &&
          !lazy.UrlbarPrefs.get("suggest.quicksuggest.sponsored")))
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
