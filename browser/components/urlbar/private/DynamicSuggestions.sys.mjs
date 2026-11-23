/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * A feature that manages the dynamic Rust suggestion types defined in the
 * `quickSuggestDynamicSuggestionTypes` Nimbus variable or its related pref
 * `quicksuggest.dynamicSuggestionTypes`. Dynamic Rust suggestions are not
 * statically typed except for a few core properties, so they can be used to
 * serve many different types of suggestions without any Rust changes. They are
 * also used for hidden-exposure suggestions (potential exposures).
 *
 * This feature only manages the types defined in the variable or pref. Other
 * features can manage dynamic suggestion types that are *not* defined in the
 * variable or pref by extending `SuggestProvider` as usual and overriding the
 * `dynamicRustSuggestionTypes` getter. That makes it possible for a feature to
 * use dynamic suggestions as an implementation detail.
 */
export class DynamicSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return ["quicksuggest.dynamicSuggestionTypes"];
  }

  get shouldEnable() {
    return !!this.dynamicRustSuggestionTypes.length;
  }

  get rustSuggestionType() {
    return "Dynamic";
  }

  get dynamicRustSuggestionTypes() {
    // UrlbarPrefs converts this pref to a `Set` of type strings.
    return [...lazy.UrlbarPrefs.get("quicksuggest.dynamicSuggestionTypes")];
  }

  isSuggestionSponsored(suggestion) {
    return !!suggestion.data?.result?.payload?.isSponsored;
  }

  getSuggestionTelemetryType(suggestion) {
    if (suggestion.data?.result?.payload?.hasOwnProperty("telemetryType")) {
      return suggestion.data.result.payload.telemetryType;
    }
    if (suggestion.data?.result?.isHiddenExposure) {
      return "exposure";
    }
    return suggestion.suggestionType;
  }

  makeResult(queryContext, suggestion, _searchString) {
    let { data } = suggestion;
    if (!data || typeof data != "object") {
      this.logger.warn(
        "suggestion.data is falsey or not an object, ignoring suggestion"
      );
      return null;
    }

    let { result } = data;
    if (!result || typeof result != "object") {
      this.logger.warn(
        "suggestion.data.result is falsey or not an object, ignoring suggestion"
      );
      return null;
    }

    let payload = {};
    if (result.hasOwnProperty("payload")) {
      if (typeof result.payload != "object") {
        this.logger.warn(
          "suggestion.data.result.payload is not an object, ignoring suggestion"
        );
        return null;
      }
      payload = result.payload;
    }

    if (result.isHiddenExposure) {
      return this.#makeExposureResult(suggestion, payload);
    }

    // Dynamic results can set `result.bypassSuggestAll` to be shown even when
    // `suggest.quicksuggest.all` is false. Typically results should not do this
    // unless they aren't considered part of the Suggest brand.
    if (
      !result.bypassSuggestAll &&
      (!lazy.UrlbarPrefs.get("suggest.quicksuggest.all") ||
        (payload.isSponsored &&
          !lazy.UrlbarPrefs.get("suggest.quicksuggest.sponsored")))
    ) {
      return null;
    }

    payload.isManageable = true;
    payload.helpUrl = lazy.QuickSuggest.HELP_URL;

    let resultProperties = { ...result };
    delete resultProperties.payload;
    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.URL,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      ...resultProperties,
      payload,
    });
  }

  /**
   * @param {UrlbarQueryContext} _queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   * @param {string} _searchString
   */
  onEngagement(_queryContext, controller, details, _searchString) {
    switch (details.selType) {
      case "manage":
        // "manage" is handled by UrlbarInput, no need to do anything here.
        break;
      case "dismiss": {
        let { result } = details;
        lazy.QuickSuggest.dismissResult(result);
        controller.removeResult(result);
        break;
      }
    }
  }

  #makeExposureResult(suggestion, payload) {
    // It doesn't really matter what kind of result we return since it won't be
    // shown. Use a dynamic result since that kind of makes sense and there are
    // no requirements for its payload other than `dynamicType`.
    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.DYNAMIC,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      // Exposure suggestions should always be hidden, and it's assumed that
      // exposure telemetry should be recorded for them, so as a convenience
      // set `exposureTelemetry` here. Otherwise experiments would need to set
      // the corresponding Nimbus variables properly. (They can still do that,
      // it's just not required.)
      exposureTelemetry: lazy.UrlbarShared.EXPOSURE_TELEMETRY.HIDDEN,
      payload: {
        ...payload,
        dynamicType: "exposure",
      },
    });
  }
}
