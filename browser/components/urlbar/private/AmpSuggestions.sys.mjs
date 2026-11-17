/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AmpMatchingStrategy:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  rawSuggestionUrlMatches:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

const TIMESTAMP_TEMPLATE = "%YYYYMMDDHH%";
const TIMESTAMP_LENGTH = 10;
const TIMESTAMP_REGEXP = /^\d{10}$/;

/**
 * Possible values for `ping_type` in the `quick-suggest` ping.
 */
export const QUICK_SUGGEST_PING_TYPE = {
  BLOCK: "quicksuggest-block",
  CLICK: "quicksuggest-click",
  IMPRESSION: "quicksuggest-impression",
};

/**
 * A feature that manages AMP suggestions.
 */
export class AmpSuggestions extends SuggestProvider {
  // Serializes `quick-suggest` ping submissions so that pings are submitted in
  // the order they were initiated, despite the async ContextId.request() call
  // inside each submission.
  #lastPingSubmission = Promise.resolve();

  get enablingPreferences() {
    return [
      "ampFeatureGate",
      "suggest.amp",
      "suggest.quicksuggest.all",
      "suggest.quicksuggest.sponsored",
    ];
  }

  get primaryUserControlledPreferences() {
    return ["suggest.amp"];
  }

  get merinoProvider() {
    return "adm";
  }

  get rustSuggestionType() {
    return "Amp";
  }

  get rustProviderConstraints() {
    let intValue = lazy.UrlbarPrefs.get("ampMatchingStrategy");
    if (!intValue) {
      // If the value is zero or otherwise falsey, use the usual default
      // exact-keyword strategy by returning null here.
      return null;
    }
    if (!Object.values(lazy.AmpMatchingStrategy).includes(intValue)) {
      this.logger.error(
        "Unknown AmpMatchingStrategy value, using default strategy",
        { intValue }
      );
      return null;
    }
    return {
      ampAlternativeMatching: intValue,
    };
  }

  isSuggestionSponsored() {
    return true;
  }

  getSuggestionTelemetryType() {
    return "adm_sponsored";
  }

  enable(enabled) {
    // NOTE: The lines below related to the deletion-request ping are
    // intentionally commented out so that the ping is never enabled. The ping
    // should be removed at the same time the `context_id` metric is removed.
    // See bug 2056936, bug 2056943, and bug 2059566.

    if (enabled) {
      GleanPings.quickSuggest.setEnabled(true);
      // GleanPings.quickSuggestDeletionRequest.setEnabled(true);
    } else {
      // Submit the `deletion-request` ping. Both it and the `quick-suggest`
      // ping must remain enabled in order for it to be successfully submitted
      // and uploaded. That's fine: It's harmless for both pings to remain
      // enabled until shutdown, and they won't be submitted again since AMP
      // suggestions are now disabled. On restart they won't be enabled again.
      //
      // this.#submitQuickSuggestDeletionRequestPing();
    }
  }

  makeResult(queryContext, suggestion, searchString) {
    // Per product requirements, at least two characters must be typed to
    // trigger an AMP suggestion.
    if (searchString.trimEnd().length < 2) {
      return null;
    }

    if (
      this.showLessFrequentlyCount &&
      searchString.length < this.#minKeywordLength
    ) {
      return null;
    }

    let normalized = Object.assign({}, suggestion);
    if (suggestion.source == "merino") {
      // Normalize the Merino suggestion so it has the same properties as Rust
      // AMP suggestions: camelCased properties plus a `rawUrl` property whose
      // value is `url` without replacing the timestamp template.
      normalized.rawUrl = suggestion.url;
      normalized.fullKeyword = suggestion.full_keyword;
      normalized.impressionUrl = suggestion.impression_url;
      normalized.clickUrl = suggestion.click_url;
      normalized.blockId = suggestion.block_id;
      normalized.iabCategory = suggestion.iab_category;
      normalized.requestId = suggestion.request_id;

      if (suggestion.custom_details?.amp) {
        let { amp } = suggestion.custom_details;
        normalized.suggestionId = amp.suggestion_id;
        if (typeof amp.header_text == "string") {
          normalized.fullKeyword = amp.header_text;
        }
      }

      // Replace URL timestamp templates inline. This isn't necessary for Rust
      // AMP suggestions because the Rust component handles it.
      this.#replaceSuggestionTemplates(normalized);
    }

    let isTopPick;
    if (
      suggestion.source == "merino" &&
      typeof suggestion.is_top_pick == "boolean"
    ) {
      // Defer entirely to Merino.
      isTopPick = suggestion.is_top_pick;
    } else {
      isTopPick =
        (lazy.UrlbarPrefs.get("quickSuggestAmpTopPickCharThreshold") &&
          lazy.UrlbarPrefs.get("quickSuggestAmpTopPickCharThreshold") <=
            queryContext.trimmedLowerCaseSearchString.length) ||
        lazy.UrlbarPrefs.get("quickSuggestSponsoredPriority");
    }

    // TODO: `richSuggestionIconSize` is unused in Nova and should be removed.
    let bestMatchType;
    let richSuggestionIconSize;
    if (!isTopPick) {
      richSuggestionIconSize = 16;
      bestMatchType = lazy.UrlbarShared.BEST_MATCH_TYPE.NONE;
    } else {
      let minScore = parseFloat(
        lazy.UrlbarPrefs.get("quicksuggest.ampTopPickScoreForLargeIcon")
      );
      if (minScore && minScore <= suggestion.score) {
        richSuggestionIconSize = 52;
        bestMatchType = lazy.UrlbarShared.BEST_MATCH_TYPE.LARGE;
      } else {
        richSuggestionIconSize = 28;
        bestMatchType = lazy.UrlbarShared.BEST_MATCH_TYPE.SMALL;
      }
    }

    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.URL,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      isBottomUrlSuggestion: true,
      bestMatchType,
      richSuggestionIconSize,
      payload: {
        url: normalized.url,
        originalUrl: normalized.rawUrl,
        title: normalized.fullKeyword,
        subtitle: normalized.title,
        bottomTextL10n: {
          id: "urlbar-result-action-sponsored",
        },
        requestId: normalized.requestId,
        suggestionId: normalized.suggestionId,
        urlTimestampIndex: normalized.urlTimestampIndex,
        sponsoredImpressionUrl: normalized.impressionUrl,
        sponsoredClickUrl: normalized.clickUrl,
        sponsoredBlockId: normalized.blockId,
        sponsoredAdvertiser: normalized.advertiser,
        sponsoredIabCategory: normalized.iabCategory,
      },
    });
  }

  getResultCommands() {
    /** @type {UrlbarResultCommand[]} */
    const commands = [];

    if (this.canShowLessFrequently) {
      commands.push({
        name: "show_less_frequently",
        l10n: {
          id: "urlbar-result-menu-show-less-frequently2",
        },
      });
    }

    commands.push(
      {
        name: "dismiss",
        l10n: {
          id: "urlbar-result-menu-dismiss-suggestion2",
        },
      },
      { name: "separator" },
      {
        name: "manage",
        l10n: {
          id: "urlbar-result-menu-manage-firefox-suggest2",
        },
      },
      {
        name: "help",
        l10n: {
          id: "urlbar-result-menu-learn-more2",
        },
      }
    );

    return commands;
  }

  /**
   * @param {string} state
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {{index: number, result: UrlbarResult}[]} featureResults
   * @param {object|null} details
   */
  onImpression(state, queryContext, controller, featureResults, details) {
    // For the purpose of the `quick-suggest` impression ping, "impression"
    // means that one of these suggestions was visible at the time of an
    // engagement regardless of the engagement type or engagement result, so
    // submit the ping if `state` is "engagement".
    if (state == "engagement") {
      for (let result of featureResults) {
        this.#submitQuickSuggestImpressionPing({
          result,
          queryContext,
          details,
        });
      }
    }
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   * @param {string} searchString
   */
  onEngagement(queryContext, controller, details, searchString) {
    let { result } = details;

    switch (details.selType) {
      case "help":
      case "manage": {
        // "manage" and "help" are handled by UrlbarInput, no need to do
        // anything here.
        return;
      }
      case "dismiss": {
        lazy.QuickSuggest.dismissResult(result);
        controller.removeResult(result);
        break;
      }
      case "show_less_frequently": {
        this.handleShowLessFrequently(controller, result);
        lazy.UrlbarPrefs.set("amp.minKeywordLength", searchString.length + 1);
        break;
      }
    }

    // A `quick-suggest` impression ping must always be submitted on engagement
    // regardless of engagement type. Normally we do that in `onImpression()`,
    // but that's not called when the session remains ongoing, so in that case,
    // submit the impression ping now.
    if (details.isSessionOngoing) {
      this.#submitQuickSuggestImpressionPing({ queryContext, result, details });
    }

    // Submit the `quick-suggest` engagement ping.
    let pingData;
    switch (details.selType) {
      case "quicksuggest":
        pingData = {
          pingType: QUICK_SUGGEST_PING_TYPE.CLICK,
          reportingUrl: result.payload.sponsoredClickUrl,
        };
        break;
      case "dismiss":
        pingData = {
          pingType: QUICK_SUGGEST_PING_TYPE.BLOCK,
          iabCategory: result.payload.sponsoredIabCategory,
        };
        break;
    }
    if (pingData) {
      this.#submitQuickSuggestPing({ queryContext, result, ...pingData });
    }
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        "amp.showLessFrequentlyCount",
        this.showLessFrequentlyCount + 1
      );
    }
  }

  get showLessFrequentlyCount() {
    const count = lazy.UrlbarPrefs.get("amp.showLessFrequentlyCount") || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    const cap = lazy.QuickSuggest.config.showLessFrequentlyCap || 0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  get #minKeywordLength() {
    let minLength = lazy.UrlbarPrefs.get("amp.minKeywordLength");
    return Math.max(minLength, 0);
  }

  isUrlEquivalentToResultUrl(url, result) {
    // If the URLs aren't the same length, they can't be equivalent.
    let resultURL = result.payload.url;
    if (resultURL.length != url.length) {
      return false;
    }

    if (result.payload.source == "rust") {
      // Rust has its own equivalence function.
      return lazy.rawSuggestionUrlMatches(result.payload.originalUrl, url);
    }

    // If the result URL doesn't have a timestamp, then do a straight string
    // comparison.
    let { urlTimestampIndex } = result.payload;
    if (typeof urlTimestampIndex != "number" || urlTimestampIndex < 0) {
      return resultURL == url;
    }

    // Compare the first parts of the strings before the timestamps.
    if (
      resultURL.substring(0, urlTimestampIndex) !=
      url.substring(0, urlTimestampIndex)
    ) {
      return false;
    }

    // Compare the second parts of the strings after the timestamps.
    let remainderIndex = urlTimestampIndex + TIMESTAMP_LENGTH;
    if (resultURL.substring(remainderIndex) != url.substring(remainderIndex)) {
      return false;
    }

    // Test the timestamp against the regexp.
    let maybeTimestamp = url.substring(
      urlTimestampIndex,
      urlTimestampIndex + TIMESTAMP_LENGTH
    );
    return TIMESTAMP_REGEXP.test(maybeTimestamp);
  }

  #submitQuickSuggestPing({ queryContext, result, pingType, ...pingData }) {
    if (queryContext.isPrivate) {
      return Promise.resolve();
    }

    // Chain this submission to the previous one so that pings are always
    // submitted in the order they were initiated. Without this, multiple
    // concurrent calls can race at the async ContextId.request() and submit
    // their pings out of order.
    let submission = this.#lastPingSubmission.then(async () => {
      let nimbusEnrollment = lazy.NimbusFeatures.urlbar.getEnrollmentMetadata();
      let allPingData = {
        pingType,
        // Suggest initialization awaits `Region.init()`, so safe to assume
        // it's already been initialized here.
        country: lazy.Region.home,
        ...pingData,
        matchType: result.isBestMatch ? "best-match" : "firefox-suggest",
        // Always use lowercase to make the reporting consistent.
        advertiser: result.payload.sponsoredAdvertiser.toLocaleLowerCase(),
        blockId: result.payload.sponsoredBlockId,
        improveSuggestExperience:
          lazy.UrlbarPrefs.get("quickSuggestOnlineAvailable") &&
          lazy.UrlbarPrefs.get("quicksuggest.online.enabled"),
        // `position` is 1-based, unlike `rowIndex`, which is zero-based.
        position: result.rowIndex + 1,
        suggestedIndex: result.suggestedIndex.toString(),
        suggestedIndexRelativeToGroup: !!result.isSuggestedIndexRelativeToGroup,
        requestId: result.payload.requestId,
        suggestionId: result.payload.suggestionId,
        source: result.payload.source,
        experimentName: nimbusEnrollment?.slug,
        experimentBranch: nimbusEnrollment?.branch,
        contextId: await lazy.ContextId.request(),
      };

      for (let [gleanKey, value] of Object.entries(allPingData)) {
        let glean = Glean.quickSuggest[gleanKey];
        if (value !== undefined && value !== "") {
          glean.set(value);
        }
      }
      GleanPings.quickSuggest.submit();
    });

    // Ensure the queue keeps working even if this submission fails.
    this.#lastPingSubmission = submission.catch(console.error);
    return submission;
  }

  #submitQuickSuggestImpressionPing({ queryContext, result, details }) {
    return this.#submitQuickSuggestPing({
      result,
      queryContext,
      pingType: QUICK_SUGGEST_PING_TYPE.IMPRESSION,
      isClicked:
        // `selType` == "quicksuggest" if the result itself was clicked. It will
        // be a command name if a command was clicked, e.g., "dismiss". Match by
        // id: the visible result and the picked result aren't necessarily the
        // same object.
        result.id == details.result?.id && details.selType == "quicksuggest",
      reportingUrl: result.payload.sponsoredImpressionUrl,
    });
  }

  // eslint-disable-next-line no-unused-private-class-members
  async #submitQuickSuggestDeletionRequestPing() {
    if (lazy.ContextId.rotationEnabled) {
      // The ContextId module will take care of sending the appropriate
      // deletion requests if rotation is enabled.
      lazy.ContextId.forceRotation();
    } else {
      Glean.quickSuggest.contextId.set(await lazy.ContextId.request());
      GleanPings.quickSuggestDeletionRequest.submit();
    }
  }

  /**
   * Some AMP suggestion URL properties include timestamp templates that must be
   * replaced with timestamps at query time. This method replaces them in place.
   *
   * Example URL with template:
   *
   *   http://example.com/foo?bar=%YYYYMMDDHH%
   *
   * It will be replaced with a timestamp like this:
   *
   *   http://example.com/foo?bar=2021111610
   *
   * @param {object} suggestion
   *   An AMP suggestion.
   */
  #replaceSuggestionTemplates(suggestion) {
    let now = new Date();
    let timestampParts = [
      now.getFullYear(),
      now.getMonth() + 1,
      now.getDate(),
      now.getHours(),
    ];
    let timestamp = timestampParts
      .map(n => n.toString().padStart(2, "0"))
      .join("");
    for (let key of ["url", "clickUrl"]) {
      let value = suggestion[key];
      if (!value) {
        continue;
      }

      let timestampIndex = value.indexOf(TIMESTAMP_TEMPLATE);
      if (timestampIndex >= 0) {
        if (key == "url") {
          suggestion.urlTimestampIndex = timestampIndex;
        }
        // We could use replace() here but we need the timestamp index for
        // `suggestion.urlTimestampIndex`, and since we already have that, avoid
        // another O(n) substring search and manually replace the template with
        // the timestamp.
        suggestion[key] =
          value.substring(0, timestampIndex) +
          timestamp +
          value.substring(timestampIndex + TIMESTAMP_TEMPLATE.length);
      }
    }
  }

  static get TIMESTAMP_TEMPLATE() {
    return TIMESTAMP_TEMPLATE;
  }

  static get TIMESTAMP_LENGTH() {
    return TIMESTAMP_LENGTH;
  }
}
