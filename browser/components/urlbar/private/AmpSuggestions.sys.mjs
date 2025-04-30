/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "resource:///modules/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AmpMatchingStrategy: "resource://gre/modules/RustSuggest.sys.mjs",
  CONTEXTUAL_SERVICES_PING_TYPES:
    "resource:///modules/PartnerLinkAttribution.sys.mjs",
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  rawSuggestionUrlMatches: "resource://gre/modules/RustSuggest.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
});

// `contextId` is a unique identifier used by Contextual Services
const CONTEXT_ID_PREF = "browser.contextual-services.contextId";
ChromeUtils.defineLazyGetter(lazy, "contextId", () => {
  let _contextId = Services.prefs.getStringPref(CONTEXT_ID_PREF, null);
  if (!_contextId) {
    _contextId = String(Services.uuid.generateUUID());
    Services.prefs.setStringPref(CONTEXT_ID_PREF, _contextId);
  }
  return _contextId;
});

const TIMESTAMP_TEMPLATE = "%YYYYMMDDHH%";
const TIMESTAMP_LENGTH = 10;
const TIMESTAMP_REGEXP = /^\d{10}$/;

/**
 * A feature that manages AMP suggestions.
 */
export class AmpSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return ["suggest.quicksuggest.sponsored"];
  }

  get primaryUserControlledPreference() {
    // AMP suggestions can't be toggled separately from sponsored suggestions.
    return null;
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
    if (enabled) {
      GleanPings.quickSuggest.setEnabled(true);
      GleanPings.quickSuggestDeletionRequest.setEnabled(true);
    } else {
      // Submit the `deletion-request` ping. Both it and the `quick-suggest`
      // ping must remain enabled in order for it to be successfully submitted
      // and uploaded. That's fine: It's harmless for both pings to remain
      // enabled until shutdown, and they won't be submitted again since AMP
      // suggestions are now disabled. On restart they won't be enabled again.
      this.#submitQuickSuggestDeletionRequestPing();
    }
  }

  makeResult(queryContext, suggestion) {
    let originalUrl;
    if (suggestion.source == "rust") {
      // The Rust backend replaces URL timestamp templates for us, and it
      // includes the original URL as `rawUrl`.
      originalUrl = suggestion.rawUrl;
    } else {
      // Replace URL timestamp templates, but first save the original URL.
      originalUrl = suggestion.url;
      this.#replaceSuggestionTemplates(suggestion);

      // Normalize the Merino suggestion so it has camelCased properties like
      // Rust suggestions.
      suggestion = {
        title: suggestion.title,
        url: suggestion.url,
        fullKeyword: suggestion.full_keyword,
        impressionUrl: suggestion.impression_url,
        clickUrl: suggestion.click_url,
        blockId: suggestion.block_id,
        advertiser: suggestion.advertiser,
        iabCategory: suggestion.iab_category,
        requestId: suggestion.request_id,
      };
    }

    let payload = {
      originalUrl,
      url: suggestion.url,
      title: suggestion.title,
      requestId: suggestion.requestId,
      urlTimestampIndex: suggestion.urlTimestampIndex,
      sponsoredImpressionUrl: suggestion.impressionUrl,
      sponsoredClickUrl: suggestion.clickUrl,
      sponsoredBlockId: suggestion.blockId,
      sponsoredAdvertiser: suggestion.advertiser,
      sponsoredIabCategory: suggestion.iabCategory,
      isBlockable: true,
      isManageable: true,
    };

    let isTopPick =
      lazy.UrlbarPrefs.get("quickSuggestAmpTopPickCharThreshold") &&
      lazy.UrlbarPrefs.get("quickSuggestAmpTopPickCharThreshold") <=
        queryContext.trimmedLowerCaseSearchString.length;

    payload.qsSuggestion = [
      suggestion.fullKeyword,
      isTopPick
        ? lazy.UrlbarUtils.HIGHLIGHT.TYPED
        : lazy.UrlbarUtils.HIGHLIGHT.SUGGESTED,
    ];

    let result = new lazy.UrlbarResult(
      lazy.UrlbarUtils.RESULT_TYPE.URL,
      lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      ...lazy.UrlbarResult.payloadAndSimpleHighlights(
        queryContext.tokens,
        payload
      )
    );

    result.isRichSuggestion = true;
    if (isTopPick) {
      result.isBestMatch = true;
      result.suggestedIndex = 1;
    } else {
      if (lazy.UrlbarPrefs.get("quickSuggestSponsoredPriority")) {
        result.isBestMatch = true;
        result.suggestedIndex = 1;
      } else {
        result.richSuggestionIconSize = 16;
      }
      result.payload.descriptionL10n = {
        id: "urlbar-result-action-sponsored",
      };
    }

    return result;
  }

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

  onEngagement(queryContext, controller, details, _searchString) {
    let { result } = details;

    // Handle commands. These suggestions support the Dismissal and Manage
    // commands. Dismissal is the only one we need to handle here. `UrlbarInput`
    // handles Manage.
    if (details.selType == "dismiss") {
      lazy.QuickSuggest.dismissResult(result);
      controller.removeResult(result);
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
          pingType: lazy.CONTEXTUAL_SERVICES_PING_TYPES.QS_SELECTION,
          reportingUrl: result.payload.sponsoredClickUrl,
        };
        break;
      case "dismiss":
        pingData = {
          pingType: lazy.CONTEXTUAL_SERVICES_PING_TYPES.QS_BLOCK,
          iabCategory: result.payload.sponsoredIabCategory,
        };
        break;
    }
    if (pingData) {
      this.#submitQuickSuggestPing({ queryContext, result, ...pingData });
    }
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
      return;
    }

    let allPingData = {
      pingType,
      ...pingData,
      matchType: result.isBestMatch ? "best-match" : "firefox-suggest",
      // Always use lowercase to make the reporting consistent.
      advertiser: result.payload.sponsoredAdvertiser.toLocaleLowerCase(),
      blockId: result.payload.sponsoredBlockId,
      improveSuggestExperience: lazy.UrlbarPrefs.get(
        "quicksuggest.dataCollection.enabled"
      ),
      // `position` is 1-based, unlike `rowIndex`, which is zero-based.
      position: result.rowIndex + 1,
      suggestedIndex: result.suggestedIndex.toString(),
      suggestedIndexRelativeToGroup: !!result.isSuggestedIndexRelativeToGroup,
      requestId: result.payload.requestId,
      source: result.payload.source,
      contextId: lazy.contextId,
    };

    for (let [gleanKey, value] of Object.entries(allPingData)) {
      let glean = Glean.quickSuggest[gleanKey];
      if (value !== undefined && value !== "") {
        glean.set(value);
      }
    }
    GleanPings.quickSuggest.submit();
  }

  #submitQuickSuggestImpressionPing({ queryContext, result, details }) {
    this.#submitQuickSuggestPing({
      result,
      queryContext,
      pingType: lazy.CONTEXTUAL_SERVICES_PING_TYPES.QS_IMPRESSION,
      isClicked:
        // `selType` == "quicksuggest" if the result itself was clicked. It will
        // be a command name if a command was clicked, e.g., "dismiss".
        result == details.result && details.selType == "quicksuggest",
      reportingUrl: result.payload.sponsoredImpressionUrl,
    });
  }

  #submitQuickSuggestDeletionRequestPing() {
    Glean.quickSuggest.contextId.set(lazy.contextId);
    GleanPings.quickSuggestDeletionRequest.submit();
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
    for (let key of ["url", "click_url"]) {
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
