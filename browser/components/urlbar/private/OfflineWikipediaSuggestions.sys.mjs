/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "resource:///modules/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
});

/**
 * A feature that manages offline (non-Merino) Wikipedia suggestions. Online
 * (Merino) Wikipedia suggestions don't have their own `SuggestProvider`.
 * Instead they're handled directly by `UrlbarProviderQuickSuggest`.
 */
export class OfflineWikipediaSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return ["suggest.quicksuggest.nonsponsored"];
  }

  get primaryUserControlledPreference() {
    // Wikipedia suggestions can't be toggled separately from nonsponsored
    // suggestions.
    return null;
  }

  get rustSuggestionType() {
    return "Wikipedia";
  }

  isSuggestionSponsored() {
    return false;
  }

  getSuggestionTelemetryType() {
    return "adm_nonsponsored";
  }

  makeResult(queryContext, suggestion) {
    return new lazy.UrlbarResult(
      lazy.UrlbarUtils.RESULT_TYPE.URL,
      lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      ...lazy.UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
        url: suggestion.url,
        title: suggestion.title,
        qsSuggestion: [
          suggestion.fullKeyword,
          lazy.UrlbarUtils.HIGHLIGHT.SUGGESTED,
        ],
        sponsoredAdvertiser: "Wikipedia",
        sponsoredIabCategory: "5 - Education",
        isBlockable: true,
        isManageable: true,
      })
    );
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
  }
}
