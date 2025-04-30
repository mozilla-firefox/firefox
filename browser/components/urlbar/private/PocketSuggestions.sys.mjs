/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "resource:///modules/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
});

const RESULT_MENU_COMMAND = {
  HELP: "help",
  NOT_INTERESTED: "not_interested",
  NOT_RELEVANT: "not_relevant",
  SHOW_LESS_FREQUENTLY: "show_less_frequently",
};

/**
 * A feature that manages Pocket suggestions in remote settings.
 */
export class PocketSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return [
      "pocketFeatureGate",
      "suggest.pocket",
      "suggest.quicksuggest.nonsponsored",
    ];
  }

  get primaryUserControlledPreference() {
    return "suggest.pocket";
  }

  get merinoProvider() {
    return "pocket";
  }

  get rustSuggestionType() {
    return "Pocket";
  }

  get showLessFrequentlyCount() {
    let count = lazy.UrlbarPrefs.get("pocket.showLessFrequentlyCount") || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    let cap =
      lazy.UrlbarPrefs.get("pocketShowLessFrequentlyCap") ||
      lazy.QuickSuggest.config.showLessFrequentlyCap ||
      0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  makeResult(queryContext, suggestion, searchString) {
    if (!this.isEnabled) {
      // The feature is disabled on the client, but Merino may still return
      // suggestions anyway, and we filter them out here.
      return null;
    }

    // If the user hasn't clicked the "Show less frequently" command, the
    // suggestion can be shown. Otherwise, the suggestion can be shown if the
    // user typed more than one word with at least `showLessFrequentlyCount`
    // characters after the first word, including spaces.
    if (this.showLessFrequentlyCount) {
      let spaceIndex = searchString.search(/\s/);
      if (
        spaceIndex < 0 ||
        searchString.length - spaceIndex < this.showLessFrequentlyCount
      ) {
        return null;
      }
    }

    // Merino will set `is_top_pick` if the suggestion should be a best match.
    let isBestMatch = !!suggestion.is_top_pick;

    if (suggestion.source == "rust") {
      isBestMatch = suggestion.isTopPick;

      // The Rust component doesn't implement these properties. For now we use
      // dummy values. See issue #5878 in application-services.
      suggestion.description = suggestion.title;
      suggestion.full_keyword = searchString;
    }

    let url = new URL(suggestion.url);
    url.searchParams.set("utm_medium", "firefox-desktop");
    url.searchParams.set("utm_source", "firefox-suggest");
    url.searchParams.set(
      "utm_campaign",
      "pocket-collections-in-the-address-bar"
    );
    url.searchParams.set("utm_content", "treatment");

    let resultProperties = {
      isBestMatch,
      isRichSuggestion: true,
      richSuggestionIconSize: isBestMatch ? 24 : 16,
      showFeedbackMenu: true,
    };

    if (!isBestMatch) {
      let suggestedIndex = lazy.UrlbarPrefs.get("pocketSuggestIndex");
      if (suggestedIndex !== null) {
        resultProperties.isSuggestedIndexRelativeToGroup = true;
        resultProperties.suggestedIndex = suggestedIndex;
      }
    }

    return Object.assign(
      new lazy.UrlbarResult(
        lazy.UrlbarUtils.RESULT_TYPE.URL,
        lazy.UrlbarUtils.RESULT_SOURCE.OTHER_NETWORK,
        ...lazy.UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
          url: url.href,
          originalUrl: suggestion.url,
          title: [suggestion.title, lazy.UrlbarUtils.HIGHLIGHT.TYPED],
          description: isBestMatch ? suggestion.description : "",
          // Use the favicon for non-best matches so the icon exactly matches
          // the Pocket favicon in the user's history and tabs.
          icon: isBestMatch
            ? "chrome://global/skin/icons/pocket.svg"
            : "chrome://global/skin/icons/pocket-favicon.ico",
          shouldShowUrl: true,
          bottomTextL10n: {
            id: "firefox-suggest-pocket-bottom-text",
            args: {
              keywordSubstringTyped: searchString,
              keywordSubstringNotTyped: suggestion.full_keyword.substring(
                searchString.length
              ),
            },
          },
          helpUrl: lazy.QuickSuggest.HELP_URL,
        })
      ),
      resultProperties
    );
  }

  onEngagement(queryContext, controller, details, _searchString) {
    let { result } = details;
    switch (details.selType) {
      case RESULT_MENU_COMMAND.HELP:
        // "help" is handled by UrlbarInput, no need to do anything here.
        break;
      // selType == "dismiss" when the user presses the dismiss key shortcut.
      case "dismiss":
      case RESULT_MENU_COMMAND.NOT_RELEVANT:
        lazy.QuickSuggest.dismissResult(result);
        result.acknowledgeDismissalL10n = {
          id: "firefox-suggest-dismissal-acknowledgment-one",
        };
        controller.removeResult(result);
        break;
      case RESULT_MENU_COMMAND.NOT_INTERESTED:
        lazy.UrlbarPrefs.set("suggest.pocket", false);
        result.acknowledgeDismissalL10n = {
          id: "firefox-suggest-dismissal-acknowledgment-all",
        };
        controller.removeResult(result);
        break;
      case RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY:
        controller.view.acknowledgeFeedback(result);
        this.incrementShowLessFrequentlyCount();
        if (!this.canShowLessFrequently) {
          controller.view.invalidateResultMenuCommands();
        }
        break;
    }
  }

  getResultCommands(result) {
    let commands = [];

    if (!result.isBestMatch && this.canShowLessFrequently) {
      commands.push({
        name: RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY,
        l10n: {
          id: "urlbar-result-menu-show-less-frequently",
        },
      });
    }

    commands.push(
      {
        l10n: {
          id: "firefox-suggest-command-dont-show-this",
        },
        children: [
          {
            name: RESULT_MENU_COMMAND.NOT_RELEVANT,
            l10n: {
              id: "firefox-suggest-command-not-relevant",
            },
          },
          {
            name: RESULT_MENU_COMMAND.NOT_INTERESTED,
            l10n: {
              id: "firefox-suggest-command-not-interested",
            },
          },
        ],
      },
      { name: "separator" },
      {
        name: RESULT_MENU_COMMAND.HELP,
        l10n: {
          id: "urlbar-result-menu-learn-more-about-firefox-suggest",
        },
      }
    );

    return commands;
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        "pocket.showLessFrequentlyCount",
        this.showLessFrequentlyCount + 1
      );
    }
  }
}
