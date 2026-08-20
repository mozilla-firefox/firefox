/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a component used to sort results in a UrlbarQueryContext.
 */

import {
  UrlbarMuxer,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderQuickSuggest:
    "moz-src:///browser/components/urlbar/UrlbarProviderQuickSuggest.sys.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.UrlbarShared.getLogger({ prefix: "MuxerUnifiedComplete" })
);

const SEMANTIC_HISTORY_PROVIDER_NAME = "UrlbarProviderSemanticHistorySearch";

/**
 * Constructs the map key by joining the url with the userContextId.
 *
 * @param   {UrlbarResult} result The result object.
 * @returns {string} map key
 */
function makeMapKeyForTabResult(result) {
  let userContextId =
    result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH
      ? result.payload.userContext?.id
      : undefined;
  return UrlbarUtils.tupleString(
    result.payload.url,
    lazy.UrlbarShared.isNonPrivateUserContextId(userContextId)
      ? userContextId
      : undefined
  );
}

/**
 * Returns the scheme- and www-insensitive key used to dedupe a URL across
 * prefix variants (http vs https, www vs non-www).
 *
 * @param {string} url The URL to strip.
 * @returns {string} The stripped URL.
 */
function stripUrlForDedupe(url) {
  return lazy.UrlbarShared.stripPrefixAndTrim(url, {
    stripHttp: true,
    stripHttps: true,
    stripWww: true,
    trimEmptyQuery: true,
  })[0];
}

/**
 * Class used to create a muxer.
 * The muxer receives and sorts results in a UrlbarQueryContext.
 */
class MuxerUnifiedComplete extends UrlbarMuxer {
  constructor() {
    super();
  }

  get name() {
    return "UnifiedComplete";
  }

  /**
   * Sorts results in the given UrlbarQueryContext.
   *
   * @param {UrlbarQueryContext} context
   *   The query context.
   * @param {Array} unsortedResults
   *   The array of UrlbarResult that is not sorted yet.
   */
  sort(context, unsortedResults) {
    // This method is called multiple times per keystroke, so it should be as
    // fast and efficient as possible.  We do two passes through the results:
    // one to collect state for the second pass, and then a second to build the
    // sorted list of results.  If you find yourself writing something like
    // context.results.find(), filter(), sort(), etc., modify one or both passes
    // instead.

    // Global state we'll use to make decisions during this sort.
    let state = {
      context,
      // RESULT_GROUP => array of results belonging to the group, excluding
      // group-relative suggestedIndex results
      resultsByGroup: new Map(),
      // RESULT_GROUP => array of group-relative suggestedIndex results
      // belonging to the group
      suggestedIndexResultsByGroup: new Map(),
      // This is analogous to `maxResults` except it's the total available
      // result span instead of the total available result count. We'll add
      // results until `usedResultSpan` would exceed `availableResultSpan`.
      availableResultSpan: context.maxResults,
      // The total result span taken up by all global (non-group-relative)
      // suggestedIndex results.
      globalSuggestedIndexResultSpan: 0,
      // The total span of results that have been added so far.
      usedResultSpan: 0,
      strippedUrlToTopPrefixAndTitle: new Map(),
      // Dedupe keys for every page a non-semantic provider returned, so that
      // semantic-history results that dupe them can be suppressed. URL results
      // are keyed by stripped URL (prefix-insensitive); switch-to-tab results
      // by their exact URL, matching how open tabs are keyed.
      nonSemanticDupeKeys: new Set(),
      baseAndTitleToTopRef: new Map(),
      urlToTabResultType: new Map(),
      addedRemoteTabUrls: new Set(),
      addedSwitchTabUrls: new Set(),
      addedResultUrls: new Set(),
      canShowPrivateSearch: unsortedResults.length > 1,
      canShowTailSuggestions: true,
      // Form history and remote suggestions added so far.  Used for deduping
      // suggestions.  Also includes the heuristic query string if the heuristic
      // is a search result.  All strings in the set are lowercased.
      suggestions: new Set(),
      canAddTabToSearch: true,
      hasUnitConversionResult: false,
      maxHeuristicResultSpan: 0,
      maxTabToSearchResultSpan: 0,
      // When you add state, update _copyState() as necessary.
    };

    // Determine the result groups to use for this sort.
    let rootGroup = lazy.UrlbarPrefs.getResultGroups({ context });
    lazy.logger.debug("Root groups", rootGroup);

    // We must do a first pass over the result to reorder some groups.
    // TODO (Bug 1970591): It would be better to sort while we insert results,
    // but we cannot because the order matters to `_updateStatePreAdd`.
    // This maps groups to { sortingField, start, length } objects.
    let indicesToSort = new Map();
    for (let i = 0; i < unsortedResults.length; ++i) {
      let group = lazy.UrlbarShared.getResultGroup(unsortedResults[i]);
      let groupObj = this.#getGroupAsObject(rootGroup, group);
      let sortingField = groupObj?.orderBy;
      if (sortingField) {
        let indices = indicesToSort.get(group);
        if (!indices) {
          indices = { sortingField, start: i, length: 1 };
          indicesToSort.set(group, indices);
        } else {
          indices.length++;
        }
      }
    }
    for (let { sortingField, start, length } of indicesToSort.values()) {
      let toSort = unsortedResults.slice(start, start + length);
      // We assume sorting is always descending.
      toSort.sort((a, b) => b.payload[sortingField] - a.payload[sortingField]);
      unsortedResults.splice(start, length, ...toSort);
    }

    // Pre-scan: collect keys for every page a non-semantic provider returned,
    // keyed by stripped URL (prefix-insensitive) for URL results and by exact
    // URL for switch-to-tab results. Semantic-history results that dupe these
    // are suppressed in `_updateStatePreAdd` and `_canAddResult`. This must run
    // before the first pass so the keys are complete when those checks run.
    for (let result of unsortedResults) {
      if (
        result.providerName == SEMANTIC_HISTORY_PROVIDER_NAME ||
        !result.payload.url
      ) {
        continue;
      }
      if (result.type == lazy.UrlbarShared.RESULT_TYPE.URL) {
        state.nonSemanticDupeKeys.add(stripUrlForDedupe(result.payload.url));
      } else if (result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH) {
        state.nonSemanticDupeKeys.add(result.payload.url);
      }
    }

    // Do the first pass over all results to build some state.
    for (let result of unsortedResults) {
      // Add each result to the appropriate `resultsByGroup` map.
      let group = lazy.UrlbarShared.getResultGroup(result);
      let resultsByGroup =
        result.hasSuggestedIndex && result.isSuggestedIndexRelativeToGroup
          ? state.suggestedIndexResultsByGroup
          : state.resultsByGroup;
      let results = resultsByGroup.get(group);
      if (!results) {
        results = [];
        resultsByGroup.set(group, results);
      }
      results.push(result);

      // Update pre-add state.
      this._updateStatePreAdd(result, state);
    }

    // Now that the first pass is done, adjust the available result span. More
    // than one tab-to-search result may be present but only one will be shown;
    // add the max TTS span to the total span of global suggestedIndex results.
    state.globalSuggestedIndexResultSpan += state.maxTabToSearchResultSpan;

    // Leave room for global suggestedIndex results at the end of the sort, by
    // subtracting their total span from the total available span. For very
    // small values of `maxRichResults`, their total span may be larger than
    // `state.availableResultSpan`.
    let globalSuggestedIndexAvailableSpan = Math.min(
      state.availableResultSpan,
      state.globalSuggestedIndexResultSpan
    );
    state.availableResultSpan -= globalSuggestedIndexAvailableSpan;

    if (state.maxHeuristicResultSpan) {
      if (lazy.UrlbarPrefs.get("experimental.hideHeuristic")) {
        // The heuristic is hidden. The muxer will include it but the view will
        // hide it. Increase the available span to compensate so that the total
        // visible span accurately reflects `context.maxResults`.
        state.availableResultSpan += state.maxHeuristicResultSpan;
      } else if (context.maxResults > 0) {
        // `context.maxResults` is positive. Make sure there's room for the
        // heuristic even if it means exceeding `context.maxResults`.
        state.availableResultSpan = Math.max(
          state.availableResultSpan,
          state.maxHeuristicResultSpan
        );
      }
    }

    // Fill the root group.
    let [sortedResults] = this._fillGroup(
      rootGroup,
      { availableSpan: state.availableResultSpan, maxResultCount: Infinity },
      state
    );

    // Add global suggestedIndex results.
    let globalSuggestedIndexResults = state.resultsByGroup.get(
      lazy.UrlbarShared.RESULT_GROUP.SUGGESTED_INDEX
    );
    if (globalSuggestedIndexResults) {
      this._addSuggestedIndexResults(
        globalSuggestedIndexResults,
        sortedResults,
        {
          availableSpan: globalSuggestedIndexAvailableSpan,
          maxResultCount: Infinity,
        },
        state
      );
    }

    context.results = sortedResults;
  }

  /**
   * Search for group in rootGroup and return it.
   *
   * @param {object} rootGroup Root group definition.
   * @param {Values<typeof lazy.UrlbarShared.RESULT_GROUP>} group The group to search for.
   * @returns {object|null} Group object from the root group. The
   *   SUGGESTED_INDEX group is not included in the rootGroup, so this
   *   will return null for it.
   */
  #getGroupAsObject(rootGroup, group) {
    if ("children" in rootGroup) {
      for (let child of rootGroup.children) {
        if ("children" in child) {
          let groupObj = this.#getGroupAsObject(child, group);
          if (groupObj) {
            return groupObj;
          }
        } else if (child.group == group) {
          return child;
        }
      }
    }
    return null;
  }

  /**
   * Returns a *deep* copy of state (except for `state.context`, which is
   * shallow copied).  i.e., any Maps, Sets, and arrays in the state should be
   * recursively copied so that the original state is not modified when the copy
   * is modified.
   *
   * @param {object} state
   *   The muxer state to copy.
   * @returns {object}
   *   A deep copy of the state.
   */
  _copyState(state) {
    let copy = Object.assign({}, state, {
      resultsByGroup: new Map(),
      suggestedIndexResultsByGroup: new Map(),
      strippedUrlToTopPrefixAndTitle: new Map(
        state.strippedUrlToTopPrefixAndTitle
      ),
      nonSemanticDupeKeys: new Set(state.nonSemanticDupeKeys),
      baseAndTitleToTopRef: new Map(state.baseAndTitleToTopRef),
      urlToTabResultType: new Map(state.urlToTabResultType),
      addedRemoteTabUrls: new Set(state.addedRemoteTabUrls),
      addedSwitchTabUrls: new Set(state.addedSwitchTabUrls),
      suggestions: new Set(state.suggestions),
      addedResultUrls: new Set(state.addedResultUrls),
    });

    // Deep copy the `resultsByGroup` maps.
    for (let key of ["resultsByGroup", "suggestedIndexResultsByGroup"]) {
      for (let [group, results] of state[key]) {
        copy[key].set(group, [...results]);
      }
    }

    return copy;
  }

  /**
   * Recursively fills a result group and its children.
   *
   * There are two ways to limit the number of results in a group:
   *
   * (1) By max total result span using the `availableSpan` property. The group
   * will be filled so that the total span of its results does not exceed this
   * value.
   *
   * (2) By max total result count using the `maxResultCount` property. The
   * group will be filled so that the total number of its results does not
   * exceed this value.
   *
   * Both `availableSpan` and `maxResultCount` may be defined, and the group's
   * results will be capped to whichever limit is reached first. If either is
   * not defined, then the group inherits that limit from its parent group.
   *
   * In addition to limiting their total number of results, groups can also
   * control the composition of their child groups by using flex ratios. A group
   * can define a `flexChildren: true` property, and in that case each of its
   * children should have a `flex` property. Each child will be filled according
   * to the ratio of its flex value and the sum of the flex values of all the
   * children, similar to HTML flexbox. If some children do not fill up but
   * others do, the filled-up children will be allowed to grow to use up the
   * unfilled space.
   *
   * @param {object} group
   *   The result group to fill.
   * @param {object} limits
   *   An object with optional `availableSpan` and `maxResultCount` properties
   *   as described above. They will be used as the limits for the group.
   * @param {object} state
   *   The muxer state.
   * @returns {Array}
   *   `[results, usedLimits, hasMoreResults]` -- see `_addResults`.
   */
  _fillGroup(group, limits, state) {
    // Get the group's suggestedIndex results. Reminder: `group.group` is a
    // `RESULT_GROUP` constant.
    let suggestedIndexResults;
    let suggestedIndexAvailableSpan = 0;
    let suggestedIndexAvailableCount = 0;
    if ("group" in group) {
      let results = state.suggestedIndexResultsByGroup.get(group.group);
      if (results) {
        // Subtract them from the group's limits so there will be room for them
        // later. Discard results that can't be added.
        let span = 0;
        let resultCount = 0;
        for (let result of results) {
          if (this._canAddResult(result, state)) {
            suggestedIndexResults ??= [];
            suggestedIndexResults.push(result);
            const spanSize = lazy.UrlbarShared.getSpanForResult(result);
            span += spanSize;
            if (spanSize) {
              resultCount++;
            }
          }
        }

        suggestedIndexAvailableSpan = Math.min(limits.availableSpan, span);
        suggestedIndexAvailableCount = Math.min(
          limits.maxResultCount,
          resultCount
        );

        // Create a new `limits` object so we don't modify the caller's.
        limits = { ...limits };
        limits.availableSpan -= suggestedIndexAvailableSpan;
        limits.maxResultCount -= suggestedIndexAvailableCount;
      }
    }

    // Fill the group. If it has children, fill them recursively. Otherwise fill
    // the group directly.
    let [results, usedLimits, hasMoreResults] = group.children
      ? this._fillGroupChildren(group, limits, state)
      : this._addResults(group.group, limits, state);

    // Add the group's suggestedIndex results.
    if (suggestedIndexResults) {
      let suggestedIndexUsedLimits = this._addSuggestedIndexResults(
        suggestedIndexResults,
        results,
        {
          availableSpan: suggestedIndexAvailableSpan,
          maxResultCount: suggestedIndexAvailableCount,
        },
        state
      );
      for (let [key, value] of Object.entries(suggestedIndexUsedLimits)) {
        usedLimits[key] += value;
      }
    }

    return [results, usedLimits, hasMoreResults];
  }

  /**
   * Helper for `_fillGroup` that fills a group's children.
   *
   * @param {object} group
   *   The result group to fill. It's assumed to have a `children` property.
   * @param {object} limits
   *   An object with optional `availableSpan` and `maxResultCount` properties
   *   as described in `_fillGroup`.
   * @param {object} state
   *   The muxer state.
   * @param {Array} flexDataArray
   *   See `_updateFlexData`.
   * @returns {Array}
   *   `[results, usedLimits, hasMoreResults]` -- see `_addResults`.
   */
  _fillGroupChildren(group, limits, state, flexDataArray = null) {
    // If the group has flexed children, update the data we use during flex
    // calculations.
    //
    // Handling flex is complicated so we discuss it briefly. We may do multiple
    // passes for a group with flexed children in order to try to optimally fill
    // them. If after one pass some children do not fill up but others do, we'll
    // do another pass that tries to overfill the filled-up children while still
    // respecting their flex ratios. We'll continue to do passes until all
    // children stop filling up or we reach the parent's limits. The way we
    // overfill children is by increasing their individual limits to make up for
    // the unused space in their underfilled siblings. Before starting a new
    // pass, we discard the results from the current pass so the new pass starts
    // with a clean slate. That means we need to copy the global sort state
    // (`state`) before modifying it in the current pass so we can use its
    // original value in the next pass [1].
    //
    // [1] Instead of starting each pass with a clean slate in this way, we
    // could accumulate results with each pass since we only ever add results to
    // flexed children and never remove them. However, that would subvert muxer
    // logic related to the global state (deduping, `_canAddResult`) since we
    // generally assume the muxer adds results in the order they appear.
    let stateCopy;
    if (group.flexChildren) {
      stateCopy = this._copyState(state);
      flexDataArray = this._updateFlexData(group, limits, flexDataArray);
    }

    // Fill each child group, collecting all results in the `results` array.
    let results = [];
    let usedLimits = {};
    for (let key of Object.keys(limits)) {
      usedLimits[key] = 0;
    }
    let anyChildUnderfilled = false;
    let anyChildHasMoreResults = false;
    for (let i = 0; i < group.children.length; i++) {
      let child = group.children[i];
      let flexData = flexDataArray?.[i];

      // Compute the child's limits.
      let childLimits = {};
      for (let key of Object.keys(limits)) {
        childLimits[key] = flexData
          ? flexData.limits[key]
          : Math.min(
              typeof child[key] == "number" ? child[key] : Infinity,
              limits[key] - usedLimits[key]
            );
      }

      // Recurse and fill the child.
      let [childResults, childUsedLimits, childHasMoreResults] =
        this._fillGroup(child, childLimits, state);
      results = results.concat(childResults);
      for (let key of Object.keys(usedLimits)) {
        usedLimits[key] += childUsedLimits[key];
      }
      anyChildHasMoreResults = anyChildHasMoreResults || childHasMoreResults;

      if (flexData?.hasMoreResults) {
        // The child is flexed and we possibly added more results to it.
        flexData.usedLimits = childUsedLimits;
        flexData.hasMoreResults = childHasMoreResults;
        anyChildUnderfilled =
          anyChildUnderfilled ||
          (!childHasMoreResults &&
            [...Object.entries(childLimits)].every(
              ([key, limit]) => flexData.usedLimits[key] < limit
            ));
      }
    }

    // If the children are flexed and some underfilled but others still have
    // more results, do another pass.
    if (anyChildUnderfilled && anyChildHasMoreResults) {
      [results, usedLimits, anyChildHasMoreResults] = this._fillGroupChildren(
        group,
        limits,
        stateCopy,
        flexDataArray
      );

      // Update `state` in place so that it's also updated in the caller.
      for (let [key, value] of Object.entries(stateCopy)) {
        state[key] = value;
      }
    }

    return [results, usedLimits, anyChildHasMoreResults];
  }

  /**
   * Updates flex-related state used while filling a group.
   *
   * @param {object} group
   *   The result group being filled.
   * @param {object} limits
   *   An object defining the group's limits as described in `_fillGroup`.
   * @param {Array} flexDataArray
   *   An array parallel to `group.children`. The object at index i corresponds
   *   to the child in `group.children` at index i. Each object maintains some
   *   flex-related state for its child and is updated during each pass in
   *   `_fillGroup` for `group`. When this method is called in the first pass,
   *   this argument should be null, and the method will create and return a new
   *   `flexDataArray` array that should be used in the remainder of the first
   *   pass and all subsequent passes.
   * @returns {Array}
   *   A new `flexDataArray` when called in the first pass, and `flexDataArray`
   *   itself when called in subsequent passes.
   */
  _updateFlexData(group, limits, flexDataArray) {
    flexDataArray =
      flexDataArray ||
      group.children.map((child, index) => {
        let data = {
          // The index of the corresponding child in `group.children`.
          index,
          // The child's limits.
          limits: {},
          // The fractional parts of the child's unrounded limits; see below.
          limitFractions: {},
          // The used-up portions of the child's limits.
          usedLimits: {},
          // True if `state.resultsByGroup` has more results of the child's
          // `RESULT_GROUP`. This is not related to the child's limits.
          hasMoreResults: true,
          // The child's flex value.
          flex: typeof child.flex == "number" ? child.flex : 0,
        };
        for (let key of Object.keys(limits)) {
          data.limits[key] = 0;
          data.limitFractions[key] = 0;
          data.usedLimits[key] = 0;
        }
        return data;
      });

    // The data objects for children with more results (i.e., that are still
    // fillable).
    let fillableDataArray = [];

    // The sum of the flex values of children with more results.
    let fillableFlexSum = 0;

    for (let data of flexDataArray) {
      if (data.hasMoreResults) {
        fillableFlexSum += data.flex;
        fillableDataArray.push(data);
      }
    }

    // Update each limit.
    for (let [key, limit] of Object.entries(limits)) {
      // Calculate the group's limit only including children with more results.
      let fillableLimit = limit;
      for (let data of flexDataArray) {
        if (!data.hasMoreResults) {
          fillableLimit -= data.usedLimits[key];
        }
      }

      // Allow for the possibility that some children may have gone over limit.
      // `fillableLimit` will be negative in that case.
      fillableLimit = Math.max(fillableLimit, 0);

      // Next we'll compute the limits of children with more results. This value
      // is the sum of those limits. It may differ from `fillableLimit` due to
      // the fact that each individual child limit must be an integer.
      let summedFillableLimit = 0;

      // Compute the limits of children with more results. If there are also
      // children that don't have more results, then these new limits will be
      // larger than they were in the previous pass.
      for (let data of fillableDataArray) {
        let unroundedLimit = fillableLimit * (data.flex / fillableFlexSum);
        // `limitFraction` is the fractional part of the unrounded ideal limit.
        // e.g., for 5.234 it will be 0.234. We use this to minimize the
        // mathematical error when tweaking limits below.
        data.limitFractions[key] = unroundedLimit - Math.floor(unroundedLimit);
        data.limits[key] = Math.round(unroundedLimit);
        summedFillableLimit += data.limits[key];
      }

      // As mentioned above, the sum of the individual child limits may not
      // equal the group's fillable limit. If the sum is smaller, the group will
      // end up with too few results. If it's larger, the group will have the
      // correct number of results (since we stop adding results once limits are
      // reached) but it may end up with a composition that does not reflect the
      // child flex ratios as accurately as possible.
      //
      // In either case, tweak the individual limits so that (1) their sum
      // equals the group's fillable limit, and (2) the composition respects the
      // flex ratios with as little mathematical error as possible.
      if (summedFillableLimit != fillableLimit) {
        // Collect the flex datas with a non-zero limit fractions. We'll round
        // them up or down depending on whether the sum is larger or smaller
        // than the group's fillable limit.
        let fractionalDataArray = fillableDataArray.filter(
          data => data.limitFractions[key]
        );

        let diff;
        if (summedFillableLimit < fillableLimit) {
          // The sum is smaller. We'll increment individual limits until the sum
          // is equal, starting with the child whose limit fraction is closest
          // to 1 in order to minimize error.
          diff = 1;
          fractionalDataArray.sort((a, b) => {
            // Sort by fraction descending so larger fractions are first.
            let cmp = b.limitFractions[key] - a.limitFractions[key];
            // Secondarily sort by index ascending so that children with the
            // same fraction are incremented in the order they appear, allowing
            // earlier children to have larger spans.
            return cmp || a.index - b.index;
          });
        } else if (fillableLimit < summedFillableLimit) {
          // The sum is larger. We'll decrement individual limits until the sum
          // is equal, starting with the child whose limit fraction is closest
          // to 0 in order to minimize error.
          diff = -1;
          fractionalDataArray.sort((a, b) => {
            // Sort by fraction ascending so smaller fractions are first.
            let cmp = a.limitFractions[key] - b.limitFractions[key];
            // Secondarily sort by index descending so that children with the
            // same fraction are decremented in reverse order, allowing earlier
            // children to retain larger spans.
            return cmp || b.index - a.index;
          });
        }

        // Now increment or decrement individual limits until their sum is equal
        // to the group's fillable limit.
        while (summedFillableLimit != fillableLimit) {
          if (!fractionalDataArray.length) {
            // This shouldn't happen, but don't let it break us.
            lazy.logger.error("fractionalDataArray is empty!");
            break;
          }
          let data = flexDataArray[fractionalDataArray.shift().index];
          data.limits[key] += diff;
          summedFillableLimit += diff;
        }
      }
    }

    return flexDataArray;
  }

  /**
   * Adds results to a group using the results from its `RESULT_GROUP` in
   * `state.resultsByGroup`.
   *
   * @param {Values<typeof lazy.UrlbarShared.RESULT_GROUP>} groupConst
   *   The group's `RESULT_GROUP`.
   * @param {object} limits
   *   An object defining the group's limits as described in `_fillGroup`.
   * @param {object} state
   *   Global state that we use to make decisions during this sort.
   * @returns {Array}
   *   `[results, usedLimits, hasMoreResults]` where:
   *     results: A flat array of results in the group, empty if no results
   *       were added.
   *     usedLimits: An object defining the amount of each limit that the
   *       results use. For each possible limit property (see `_fillGroup`),
   *       there will be a corresponding property in this object. For example,
   *       if 3 results are added with a total span of 4, then this object will
   *       be: { maxResultCount: 3, availableSpan: 4 }
   *     hasMoreResults: True if `state.resultsByGroup` has more results of
   *       the same `RESULT_GROUP`. This is not related to the group's limits.
   */
  _addResults(groupConst, limits, state) {
    let usedLimits = {};
    for (let key of Object.keys(limits)) {
      usedLimits[key] = 0;
    }

    // For form history, maxHistoricalSearchSuggestions == 0 implies the user
    // has opted out of form history completely, so we override the max result
    // count here in that case. Other values of maxHistoricalSearchSuggestions
    // are ignored and we use the flex defined on the form history group.
    if (
      groupConst == lazy.UrlbarShared.RESULT_GROUP.FORM_HISTORY &&
      !lazy.UrlbarPrefs.get("maxHistoricalSearchSuggestions")
    ) {
      // Create a new `limits` object so we don't modify the caller's.
      limits = { ...limits };
      limits.maxResultCount = 0;
    }

    let addedResults = [];
    let groupResults = state.resultsByGroup.get(groupConst);
    while (
      groupResults?.length &&
      state.usedResultSpan < state.availableResultSpan &&
      [...Object.entries(limits)].every(([k, limit]) => usedLimits[k] < limit)
    ) {
      let result = groupResults[0];
      if (this._canAddResult(result, state)) {
        if (!this.#updateUsedLimits(result, limits, usedLimits, state)) {
          // Adding the result would exceed the group's available span, so stop
          // adding results to it. Skip the shift() below so the result can be
          // added to later groups.
          break;
        }
        addedResults.push(result);
      }

      // We either add or discard results in the order they appear in
      // `groupResults`, so shift() them off. That way later groups with the
      // same `RESULT_GROUP` won't include results that earlier groups have
      // added or discarded.
      groupResults.shift();
    }

    return [addedResults, usedLimits, !!groupResults?.length];
  }

  /**
   * Returns whether a result is a semantic-history result that dupes a page
   * already contributed by a non-semantic provider, and so should be
   * suppressed. URL results are matched prefix-insensitively (stripped);
   * switch-to-tab results are matched on their exact URL, the same way open
   * tabs are keyed.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {object} state
   *   Global state that we use to make decisions during this sort.
   * @returns {boolean}
   *   True if the result is a suppressed semantic-history dupe.
   */
  #isSuppressedSemanticDupe(result, state) {
    if (
      result.providerName != SEMANTIC_HISTORY_PROVIDER_NAME ||
      !result.payload.url
    ) {
      return false;
    }
    if (result.type == lazy.UrlbarShared.RESULT_TYPE.URL) {
      return state.nonSemanticDupeKeys.has(
        stripUrlForDedupe(result.payload.url)
      );
    }
    if (result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH) {
      return state.nonSemanticDupeKeys.has(result.payload.url);
    }
    return false;
  }

  /**
   * Returns whether a result can be added to its group given the current sort
   * state.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {object} state
   *   Global state that we use to make decisions during this sort.
   * @returns {boolean}
   *   True if the result can be added and false if it should be discarded.
   */
  // TODO (Bug 1741273): Refactor this method to avoid an eslint complexity
  // error or increase the complexity threshold.
  // eslint-disable-next-line complexity
  _canAddResult(result, state) {
    if (state.context.excludeSponsoredResults && result.payload.isSponsored) {
      return false;
    }

    // Typically the first visible Suggest result is always added.
    if (result.providerName == lazy.UrlbarProviderQuickSuggest.name) {
      if (result.isHiddenExposure) {
        // Always allow hidden exposure Suggest results.
        return true;
      }
      if (
        result.payload.suggestionObject?.suggestionType == "important_dates"
      ) {
        // Always allow important date results since they are considered
        // utility suggestions rather than typical suggestions.
        // We assume that there will be at most one.
        return true;
      }

      if (state.quickSuggestResult && state.quickSuggestResult != result) {
        // A Suggest result was already added.
        return false;
      }

      // Don't add navigational suggestions that dupe the heuristic.
      let heuristicUrl = state.context.heuristicResult?.payload.url;
      if (
        heuristicUrl &&
        result.payload.telemetryType == "top_picks" &&
        !lazy.UrlbarPrefs.get("experimental.hideHeuristic")
      ) {
        let opts = {
          stripHttp: true,
          stripHttps: true,
          stripWww: true,
          trimSlash: true,
        };
        result.payload.dupedHeuristic =
          lazy.UrlbarShared.stripPrefixAndTrim(heuristicUrl, opts)[0] ==
          lazy.UrlbarShared.stripPrefixAndTrim(result.payload.url, opts)[0];
        return !result.payload.dupedHeuristic;
      }

      return true;
    }

    // Discard semantic-history results that dupe a non-semantic result for the
    // same page. The non-semantic result is preferred, so the surviving result
    // is never mislabeled as semantic. Semantic results with no non-semantic
    // counterpart are left untouched.
    if (this.#isSuppressedSemanticDupe(result, state)) {
      return false;
    }

    // We expect UrlbarProviderPlaces sent us the highest-ranked www. and non-www
    // origins, if any. Now, compare them to each other and to the heuristic
    // result.
    //
    // 1. If the heuristic result is lower ranked than both, discard the www
    //    origin, unless it has a different page title than the non-www
    //    origin. This is a guard against deduping when www.site.com and
    //    site.com have different content.
    // 2. If the heuristic result is higher than either the www origin or
    //    non-www origin:
    //    2a. If the heuristic is a www origin, discard the non-www origin.
    //    2b. If the heuristic is a non-www origin, discard the www origin.
    if (
      !result.heuristic &&
      result.type == lazy.UrlbarShared.RESULT_TYPE.URL &&
      result.payload.url
    ) {
      let [strippedUrl, prefix] = lazy.UrlbarShared.stripPrefixAndTrim(
        result.payload.url,
        {
          stripHttp: true,
          stripHttps: true,
          stripWww: true,
          trimEmptyQuery: true,
        }
      );
      let topPrefixData = state.strippedUrlToTopPrefixAndTitle.get(strippedUrl);
      // If the condition below is not met, we are deduping a result against
      // itself.
      if (
        topPrefixData &&
        (prefix != topPrefixData.prefix ||
          result.providerName != topPrefixData.providerName)
      ) {
        let prefixRank = UrlbarUtils.getPrefixRank(prefix);
        if (
          (prefixRank < topPrefixData.rank &&
            (prefix.endsWith("www.") == topPrefixData.prefix.endsWith("www.") ||
              result.payload?.title == topPrefixData.title)) ||
          (prefix == topPrefixData.prefix &&
            result.providerName != topPrefixData.providerName)
        ) {
          return false;
        }
      }
    }

    // Discard results that dupe autofill.
    if (
      state.context.heuristicResult &&
      state.context.heuristicResult.autofill &&
      !result.autofill &&
      state.context.heuristicResult.payload?.url == result.payload.url &&
      state.context.heuristicResult.type == result.type &&
      !lazy.UrlbarPrefs.get("experimental.hideHeuristic")
    ) {
      return false;
    }

    // UrlbarProviderHeuristicFallback may add non-heuristic results in some cases,
    // but those should be retained only if the heuristic result comes from it.
    if (
      !result.heuristic &&
      result.providerName == "UrlbarProviderHeuristicFallback" &&
      state.context.heuristicResult?.providerName !=
        "UrlbarProviderHeuristicFallback"
    ) {
      return false;
    }

    if (result.providerName == "UrlbarProviderTabToSearch") {
      // Discard the result if a tab-to-search result was added already.
      if (!state.canAddTabToSearch) {
        return false;
      }

      // In cases where the heuristic result is not a URL and we have a
      // tab-to-search result, the tab-to-search provider determined that the
      // typed string is similar to an engine domain. We can let the
      // tab-to-search result through.
      if (
        state.context.heuristicResult?.type == lazy.UrlbarShared.RESULT_TYPE.URL
      ) {
        // Discard the result if the heuristic result is not autofill and we are
        // not making an exception for a fuzzy match.
        if (
          !state.context.heuristicResult.autofill &&
          !result.payload.satisfiesAutofillThreshold
        ) {
          return false;
        }

        let autofillHostname = new URL(
          state.context.heuristicResult.payload.url
        ).hostname;
        let [autofillDomain] = lazy.UrlbarShared.stripPrefixAndTrim(
          autofillHostname,
          {
            stripWww: true,
          }
        );
        // Strip the public suffix because we want to allow matching "domain.it"
        // with "domain.com".
        autofillDomain = UrlbarUtils.stripPublicSuffixFromHost(autofillDomain);
        if (!autofillDomain) {
          return false;
        }

        // `searchUrlDomainWithoutSuffix` is the engine's domain with the public
        // suffix already stripped, for example "www.mozilla.".
        let [engineDomain] = lazy.UrlbarShared.stripPrefixAndTrim(
          result.payload.searchUrlDomainWithoutSuffix,
          {
            stripWww: true,
          }
        );
        // Discard if the engine domain does not end with the autofilled one.
        if (!engineDomain.endsWith(autofillDomain)) {
          return false;
        }
      }
    }

    // Discard "Search in a Private Window" if appropriate.
    if (
      result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH &&
      result.payload.inPrivateWindow &&
      !state.canShowPrivateSearch
    ) {
      return false;
    }

    // Discard form history and remote suggestions that dupe previously added
    // suggestions or the heuristic. We do not deduplicate rich suggestions so
    // they do not visually disapear as the suggestion is completed and
    // becomes the same url as the heuristic result.
    if (
      result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH &&
      result.payload.lowerCaseSuggestion &&
      !result.isRichSuggestion
    ) {
      let suggestion = result.payload.lowerCaseSuggestion.trim();
      if (!suggestion || state.suggestions.has(suggestion)) {
        return false;
      }
    }

    // Discard tail suggestions if appropriate.
    if (
      result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH &&
      result.payload.tail &&
      !result.isRichSuggestion &&
      !state.canShowTailSuggestions
    ) {
      return false;
    }

    // Discard remote tab results that dupes another remote tab or a
    // switch-to-tab result.
    if (result.type == lazy.UrlbarShared.RESULT_TYPE.REMOTE_TAB) {
      if (state.addedRemoteTabUrls.has(result.payload.url)) {
        return false;
      }
      let maybeDupeType = state.urlToTabResultType.get(result.payload.url);
      if (maybeDupeType == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH) {
        return false;
      }
    }

    // Discard switch-to-tab results that dupes another switch-to-tab result.
    if (
      result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH &&
      state.addedSwitchTabUrls.has(makeMapKeyForTabResult(result))
    ) {
      return false;
    }

    // Discard history results that dupe either remote or switch-to-tab results.
    if (
      !result.heuristic &&
      result.type == lazy.UrlbarShared.RESULT_TYPE.URL &&
      result.payload.url &&
      state.urlToTabResultType.has(result.payload.url)
    ) {
      return false;
    }

    // Discard SERPs from browser history that dupe either the heuristic or
    // previously added suggestions.
    if (
      result.source == lazy.UrlbarShared.RESULT_SOURCE.HISTORY &&
      result.type == lazy.UrlbarShared.RESULT_TYPE.URL &&
      // If there's no suggestions, we're not going to have anything to match
      // against, so avoid processing the url.
      state.suggestions.size
    ) {
      let submission = lazy.SearchService.parseSubmissionURL(
        result.payload.url
      );
      if (submission) {
        let resultQuery = submission.terms.trim().toLocaleLowerCase();
        if (state.suggestions.has(resultQuery)) {
          // If the result's URL is the same as a brand new SERP URL created
          // from the query string modulo certain URL params, then treat the
          // result as a dupe and discard it.
          let [newSerpURL] = UrlbarUtils.getSearchQueryUrl(
            submission.engine,
            submission.terms
          );
          if (
            lazy.UrlbarSearchUtils.serpsAreEquivalent(
              result.payload.url,
              newSerpURL
            )
          ) {
            return false;
          }
        }
      }
    }

    // When in an engine search mode, discard URL results whose hostnames don't
    // include the root domain of the search mode engine.
    if (
      state.context.searchMode?.engineName &&
      result.payload.url &&
      state.context.restrictInSearchMode()
    ) {
      let engine = lazy.SearchService.getEngineByName(
        state.context.searchMode.engineName
      );
      if (engine) {
        let searchModeRootDomain =
          lazy.UrlbarSearchUtils.getRootDomainFromEngine(engine);
        let resultUrl = new URL(result.payload.url);
        // Add a trailing "." to increase the stringency of the check. This
        // check covers most general cases. Some edge cases are not covered,
        // like `resultUrl` being ebay.mydomain.com, which would escape this
        // check if `searchModeRootDomain` was "ebay".
        if (!resultUrl.hostname.includes(`${searchModeRootDomain}.`)) {
          return false;
        }
      }
    }

    // Discard history results that dupe the quick suggest result.
    if (
      state.quickSuggestResult &&
      !result.heuristic &&
      result.type == lazy.UrlbarShared.RESULT_TYPE.URL &&
      lazy.QuickSuggest.isUrlEquivalentToResultUrl(
        result.payload.url,
        state.quickSuggestResult
      )
    ) {
      return false;
    }

    // Discard history results whose URLs were originally sponsored. We use the
    // presence of a partner's URL search param to detect these. The param is
    // defined in the pref below, which is also used for the newtab page.
    if (
      result.source == lazy.UrlbarShared.RESULT_SOURCE.HISTORY &&
      result.type == lazy.UrlbarShared.RESULT_TYPE.URL
    ) {
      let param = Services.prefs.getCharPref(
        "browser.newtabpage.activity-stream.hideTopSitesWithSearchParam"
      );
      if (param) {
        let [key, value] = param.split("=");
        let searchParams = URL.parse(result.payload.url)?.searchParams;
        if (
          (value === undefined && searchParams?.has(key)) ||
          (value !== undefined && searchParams?.getAll(key).includes(value))
        ) {
          return false;
        }
      }
    }

    // Heuristic results must always be the first result.  If this result is a
    // heuristic but we've already added results, discard it.  Normally this
    // should never happen because the standard result groups are set up so
    // that there's always at most one heuristic and it's always first, but
    // since result groups are stored in a pref and can therefore be modified
    // by the user, we perform this check.
    if (result.heuristic && state.usedResultSpan) {
      return false;
    }

    // Google search engine might suggest a result for unit conversion with
    // format that starts with "= ". If our UnitConversion can provide the
    // result, we discard the suggestion of Google in order to deduplicate.
    if (
      result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH &&
      result.payload.engine == "Google" &&
      result.payload.suggestion?.startsWith("= ") &&
      state.hasUnitConversionResult
    ) {
      return false;
    }

    // Discard results that have an embedded "url" param with the same value
    // as another result's url
    if (result.payload.url) {
      let urlParams = result.payload.url.split("?").pop();
      let embeddedUrl = new URLSearchParams(urlParams).get("url");

      if (state.addedResultUrls.has(embeddedUrl)) {
        return false;
      }
    }

    // Dedupe history results with different ref.
    if (
      lazy.UrlbarPrefs.get("deduplication.enabled") &&
      result.source == lazy.UrlbarShared.RESULT_SOURCE.HISTORY &&
      result.type == lazy.UrlbarShared.RESULT_TYPE.URL &&
      !result.heuristic
    ) {
      let { base, ref } = UrlbarUtils.extractRefFromUrl(result.payload.url);
      let baseAndTitle = `${base} ${result.payload.title}`;
      let topRef = state.baseAndTitleToTopRef.get(baseAndTitle);

      if (topRef !== undefined && ref != topRef) {
        return false;
      }
    }

    // Include the result.
    return true;
  }

  /**
   * Updates the global state that we use to make decisions during sort.  This
   * should be called for results before we've decided whether to add or discard
   * them.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {object} state
   *   Global state that we use to make decisions during this sort.
   */
  _updateStatePreAdd(result, state) {
    // Check whether the result should trigger exposure telemetry. If it will
    // not be visible because it's a hidden exposure, it should not affect the
    // muxer's state, so bail now.
    this.#setExposureTelemetryProperty(result);
    if (result.isHiddenExposure) {
      return;
    }

    // Keep track of the largest heuristic result span.
    if (result.heuristic && this._canAddResult(result, state)) {
      state.maxHeuristicResultSpan = Math.max(
        state.maxHeuristicResultSpan,
        lazy.UrlbarShared.getSpanForResult(result)
      );
    }

    // Keep track of the total span of global suggestedIndex results so we can
    // make room for them at the end of the sort. Tab-to-search results are an
    // exception: There can be multiple TTS results but only one will be shown,
    // so we track the max TTS span separately.
    if (
      result.hasSuggestedIndex &&
      !result.isSuggestedIndexRelativeToGroup &&
      this._canAddResult(result, state)
    ) {
      let span = lazy.UrlbarShared.getSpanForResult(result);
      if (result.providerName == "UrlbarProviderTabToSearch") {
        state.maxTabToSearchResultSpan = Math.max(
          state.maxTabToSearchResultSpan,
          span
        );
      } else {
        state.globalSuggestedIndexResultSpan += span;
      }
    }

    // A semantic-history result that dupes a non-semantic result is suppressed
    // in `_canAddResult`, so it must not participate in the dedup-state blocks
    // below, or it could become the recorded winner and evict the non-semantic
    // result we want to keep.
    let isSemanticDupe = this.#isSuppressedSemanticDupe(result, state);

    // Save some state we'll use later to dedupe URL results.
    if (
      !isSemanticDupe &&
      (result.type == lazy.UrlbarShared.RESULT_TYPE.URL ||
        result.type == lazy.UrlbarShared.RESULT_TYPE.KEYWORD) &&
      result.payload.url &&
      (!result.heuristic || !lazy.UrlbarPrefs.get("experimental.hideHeuristic"))
    ) {
      let [strippedUrl, prefix] = lazy.UrlbarShared.stripPrefixAndTrim(
        result.payload.url,
        {
          stripHttp: true,
          stripHttps: true,
          stripWww: true,
          trimEmptyQuery: true,
        }
      );
      let prefixRank = UrlbarUtils.getPrefixRank(prefix);
      let topPrefixData = state.strippedUrlToTopPrefixAndTitle.get(strippedUrl);
      let topPrefixRank = topPrefixData ? topPrefixData.rank : -1;
      if (
        topPrefixRank < prefixRank ||
        // If a quick suggest result has the same stripped URL and prefix rank
        // as another result, store the quick suggest as the top rank so we
        // discard the other during deduping. That happens after the user picks
        // the quick suggest: The URL is added to history and later both a
        // history result and the quick suggest may match a query.
        (topPrefixRank == prefixRank &&
          result.providerName == lazy.UrlbarProviderQuickSuggest.name)
      ) {
        // strippedUrl => { prefix, title, rank, providerName }
        state.strippedUrlToTopPrefixAndTitle.set(strippedUrl, {
          prefix,
          title: result.payload.title,
          rank: prefixRank,
          providerName: result.providerName,
        });
      }
    }

    // Save some state to dedupe results that only differ by the ref of their URL.
    // Even though we are considering tab results and URL results of all sources
    // here to find the top ref, we will only dedupe URL results with history source.
    if (
      !isSemanticDupe &&
      (result.type == lazy.UrlbarShared.RESULT_TYPE.URL ||
        result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH)
    ) {
      let { base, ref } = UrlbarUtils.extractRefFromUrl(result.payload.url);

      // This is unique because all spaces in base will be url-encoded
      // so the part before the space is always the base url.
      let baseAndTitle = `${base} ${result.payload.title}`;

      // The first result should have the highest frecency so we set it as the top
      // ref for its base url. If a result is heuristic, always override an existing
      // top ref for its base url in case the heuristic provider was slow and a non
      // heuristic result was added first for the same base url.
      if (!state.baseAndTitleToTopRef.has(baseAndTitle) || result.heuristic) {
        state.baseAndTitleToTopRef.set(baseAndTitle, ref);
      }
    }

    // Save some state we'll use later to dedupe results from open/remote tabs.
    if (
      result.payload.url &&
      (result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH ||
        (result.type == lazy.UrlbarShared.RESULT_TYPE.REMOTE_TAB &&
          !state.urlToTabResultType.has(makeMapKeyForTabResult(result))))
    ) {
      // url => result type
      state.urlToTabResultType.set(makeMapKeyForTabResult(result), result.type);
    }

    // If we find results other than the heuristic, "Search in Private
    // Window," tail suggestions, or AI chat results, then we should hide
    // tail suggestions since they're a last resort.
    if (
      state.canShowTailSuggestions &&
      !result.heuristic &&
      result.type != lazy.UrlbarShared.RESULT_TYPE.AI_CHAT &&
      (result.type != lazy.UrlbarShared.RESULT_TYPE.SEARCH ||
        (!result.payload.inPrivateWindow && !result.payload.tail))
    ) {
      state.canShowTailSuggestions = false;
    }

    if (
      result.providerName == lazy.UrlbarProviderQuickSuggest.name &&
      result.payload.suggestionObject?.suggestionType != "important_dates"
    ) {
      state.quickSuggestResult ??= result;
    }

    state.hasUnitConversionResult ||=
      result.providerName == "UrlbarProviderUnitConversion";

    // Keep track of result urls to dedupe results with the same url embedded
    // in its query string
    if (result.payload.url) {
      state.addedResultUrls.add(result.payload.url);
    }
  }

  /**
   * Updates the global state that we use to make decisions during sort.  This
   * should be called for results after they've been added.  It should not be
   * called for discarded results.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {object} state
   *   Global state that we use to make decisions during this sort.
   */
  _updateStatePostAdd(result, state) {
    // bail early if the result will be hidden from the final view.
    if (result.isHiddenExposure) {
      return;
    }

    // Update heuristic state.
    if (result.heuristic) {
      state.context.heuristicResult = result;
      if (
        result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH &&
        result.payload.query &&
        !lazy.UrlbarPrefs.get("experimental.hideHeuristic")
      ) {
        let query = result.payload.query.trim().toLocaleLowerCase();
        if (query) {
          state.suggestions.add(query);
        }
      }
    }

    // The "Search in a Private Window" result should only be shown when there
    // are other results and all of them are searches.  It should not be shown
    // if the user typed an alias because that's an explicit engine choice.
    if (
      !lazy.SearchService.separatePrivateDefaultUrlbarResultEnabled ||
      (state.canShowPrivateSearch &&
        (result.type != lazy.UrlbarShared.RESULT_TYPE.SEARCH ||
          result.payload.providesSearchMode ||
          (result.heuristic && result.payload.keyword)))
    ) {
      state.canShowPrivateSearch = false;
    }

    // Update suggestions.
    if (
      result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH &&
      result.payload.lowerCaseSuggestion
    ) {
      let suggestion = result.payload.lowerCaseSuggestion.trim();
      if (suggestion) {
        state.suggestions.add(suggestion);
      }
    }

    // Avoid multiple tab-to-search results.
    // TODO (Bug 1670185): figure out better strategies to manage this case.
    if (result.providerName == "UrlbarProviderTabToSearch") {
      state.canAddTabToSearch = false;
    }

    // Sync will send us duplicate remote tabs if multiple copies of a tab are
    // open on a synced client. Keep track of which remote tabs we've added to
    // dedupe these.
    if (result.type == lazy.UrlbarShared.RESULT_TYPE.REMOTE_TAB) {
      state.addedRemoteTabUrls.add(result.payload.url);
    }

    // Keep track of which switch tabs we've added to dedupe switch tabs.
    if (result.type == lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH) {
      state.addedSwitchTabUrls.add(makeMapKeyForTabResult(result));
    }
  }

  /**
   * Inserts results with suggested indexes. This can be called for either
   * global or group-relative suggestedIndex results. It should be called after
   * `sortedResults` has been filled in.
   *
   * @param {Array} suggestedIndexResults
   *   Results with a `suggestedIndex` property.
   * @param {Array} sortedResults
   *   The sorted results. For global suggestedIndex results, this should be the
   *   final list of all results before suggestedIndex results are inserted. For
   *   group-relative suggestedIndex results, this should be the final list of
   *   results in the group before group-relative suggestedIndex results are
   *   inserted.
   * @param {object} limits
   *   An object defining span and count limits. See `_fillGroup()`.
   * @param {object} state
   *   Global state that we use to make decisions during this sort.
   * @returns {object}
   *   A `usedLimits` object that describes the total span and count of all the
   *   added results. See `_addResults`.
   */
  _addSuggestedIndexResults(
    suggestedIndexResults,
    sortedResults,
    limits,
    state
  ) {
    let usedLimits = {
      availableSpan: 0,
      maxResultCount: 0,
    };

    if (!suggestedIndexResults?.length) {
      // This is just a slight optimization; no need to continue.
      return usedLimits;
    }

    // Partition the results into positive- and negative-index arrays. Positive
    // indexes are relative to the start of the list and negative indexes are
    // relative to the end.
    let positive = [];
    let negative = [];
    for (let result of suggestedIndexResults) {
      let results = result.suggestedIndex < 0 ? negative : positive;
      results.push(result);
    }

    // Sort the positive results ascending so that results at the end of the
    // array don't end up offset by later insertions at the front.
    positive.sort((a, b) => {
      if (a.suggestedIndex !== b.suggestedIndex) {
        return a.suggestedIndex - b.suggestedIndex;
      }

      if (a.providerName === b.providerName) {
        if (a.providerName === lazy.UrlbarProviderQuickSuggest.name) {
          // The important dates suggestion should be before the other suggestion.
          let aIsDate =
            a.payload.suggestionObject?.suggestionType === "important_dates";
          let bIsDate =
            b.payload.suggestionObject?.suggestionType === "important_dates";
          return Number(aIsDate) - Number(bIsDate);
        }

        return 0;
      }

      // If same suggestedIndex, change the displaying order along to following
      // provider priority.
      // GlobalActions == TabToSearch (legacy) > QuickSuggest > Other providers
      if (
        a.providerName === "UrlbarProviderTabToSearch" ||
        a.providerName === "UrlbarProviderGlobalActions"
      ) {
        return 1;
      }
      if (
        b.providerName === "UrlbarProviderTabToSearch" ||
        b.providerName === "UrlbarProviderGlobalActions"
      ) {
        return -1;
      }
      if (a.providerName === lazy.UrlbarProviderQuickSuggest.name) {
        return 1;
      }
      if (b.providerName === lazy.UrlbarProviderQuickSuggest.name) {
        return -1;
      }

      return 0;
    });

    // Conversely, sort the negative results descending so that results at the
    // front of the array don't end up offset by later insertions at the end.
    negative.sort((a, b) => b.suggestedIndex - a.suggestedIndex);

    // Insert the results. We start with the positive results because we have
    // tests that assume they're inserted first. In practice it shouldn't matter
    // because there's no good reason we would ever have a negative result come
    // before a positive result in the same query. Even if we did, we have to
    // insert one before the other, and there's no right or wrong order.
    for (let results of [positive, negative]) {
      let prevResult;
      let prevIndex;
      for (let result of results) {
        if (this._canAddResult(result, state)) {
          if (!this.#updateUsedLimits(result, limits, usedLimits, state)) {
            return usedLimits;
          }

          let index;
          if (
            prevResult &&
            prevResult.suggestedIndex == result.suggestedIndex
          ) {
            index = prevIndex;
          } else {
            index =
              result.suggestedIndex >= 0
                ? Math.min(result.suggestedIndex, sortedResults.length)
                : Math.max(result.suggestedIndex + sortedResults.length + 1, 0);
          }
          prevResult = result;
          prevIndex = index;
          sortedResults.splice(index, 0, result);
        }
      }
    }

    return usedLimits;
  }

  /**
   * Checks whether adding a result would exceed the given limits. If the limits
   * would be exceeded, this returns false and does nothing else. If the limits
   * would not be exceeded, the given used limits and state are updated to
   * account for the result, true is returned, and the caller should then add
   * the result to its list of sorted results.
   *
   * @param {UrlbarResult} result
   *   The result.
   * @param {object} limits
   *   An object defining span and count limits. See `_fillGroup()`.
   * @param {object} usedLimits
   *   An object with parallel properties to `limits` that describes how much of
   *   the limits have been used. See `_addResults()`.
   * @param {object} state
   *   The muxer state.
   * @returns {boolean}
   *   True if the limits were updated and the result can be added and false
   *   otherwise.
   */
  #updateUsedLimits(result, limits, usedLimits, state) {
    let span = lazy.UrlbarShared.getSpanForResult(result);
    let newUsedSpan = usedLimits.availableSpan + span;
    if (limits.availableSpan < newUsedSpan) {
      // Adding the result would exceed the available span.
      return false;
    }

    usedLimits.availableSpan = newUsedSpan;
    if (span) {
      usedLimits.maxResultCount++;
    }

    state.usedResultSpan += span;
    this._updateStatePostAdd(result, state);

    return true;
  }

  /**
   * Checks exposure eligibility and visibility for the given result.
   * If the result passes the exposure check, we set `result.exposureTelemetry`
   * to the appropriate `UrlbarShared.EXPOSURE_TELEMETRY` value.
   *
   * @param {UrlbarResult} result
   *   The result.
   */
  #setExposureTelemetryProperty(result) {
    const exposureResults = lazy.UrlbarPrefs.get("exposureResults");
    if (exposureResults.size) {
      const telemetryType =
        lazy.UrlbarShared.searchEngagementTelemetryType(result);
      if (exposureResults.has(telemetryType)) {
        result.exposureTelemetry = lazy.UrlbarPrefs.get("showExposureResults")
          ? lazy.UrlbarShared.EXPOSURE_TELEMETRY.SHOWN
          : lazy.UrlbarShared.EXPOSURE_TELEMETRY.HIDDEN;
      }
    }
  }
}

export var UrlbarMuxerStandard = new MuxerUnifiedComplete();
