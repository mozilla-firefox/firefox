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

const RESULT_MENU_COMMAND = {
  DISMISS: "dismiss",
  MANAGE: "manage",
  NOT_INTERESTED: "not_interested",
  SHOW_LESS_FREQUENTLY: "show_less_frequently",
};

/**
 * A feature that supports MDN suggestions.
 */
export class MDNSuggestions extends SuggestProvider {
  get enablingPreferences() {
    return ["mdn.featureGate", "suggest.mdn", "suggest.quicksuggest.all"];
  }

  get primaryUserControlledPreferences() {
    return ["suggest.mdn"];
  }

  get merinoProvider() {
    return "mdn";
  }

  get rustSuggestionType() {
    return "Mdn";
  }

  async makeResult(queryContext, suggestion, searchString) {
    if (!this.isEnabled) {
      // The feature is disabled on the client, but Merino may still return
      // mdn suggestions anyway, and we filter them out here.
      return null;
    }

    if (
      this.showLessFrequentlyCount &&
      searchString.length < this.#minKeywordLength
    ) {
      return null;
    }

    const url = new URL(suggestion.url);
    url.searchParams.set("utm_medium", "firefox-desktop");
    url.searchParams.set("utm_source", "firefox-suggest");
    url.searchParams.set(
      "utm_campaign",
      "firefox-mdn-web-docs-suggestion-experiment"
    );
    url.searchParams.set("utm_content", "treatment");

    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.URL,
      source: lazy.UrlbarShared.RESULT_SOURCE.OTHER_NETWORK,
      isBestMatch: true,
      isBottomUrlSuggestion: true,
      payload: {
        icon: "chrome://global/skin/icons/mdn.svg",
        url: url.href,
        originalUrl: suggestion.url,
        title: suggestion.title,
        subtitleL10n: { id: "urlbar-result-mdn-subtitle" },
        description: suggestion.description,
        bottomTextL10n: {
          id: "urlbar-result-suggestion-recommended",
        },
      },
      highlights: {
        title: lazy.UrlbarShared.HIGHLIGHT.TYPED,
      },
    });
  }

  /**
   * Gets the list of commands that should be shown in the result menu for a
   * given result from the provider. All commands returned by this method should
   * be handled by implementing `onEngagement()` with the possible exception of
   * commands automatically handled by the urlbar, like "help".
   */
  getResultCommands() {
    /** @type {UrlbarResultCommand[]} */
    const commands = [];

    if (this.canShowLessFrequently) {
      commands.push({
        name: RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY,
        l10n: {
          id: "urlbar-result-menu-show-less-frequently2",
        },
      });
    }

    commands.push(
      {
        name: RESULT_MENU_COMMAND.DISMISS,
        l10n: {
          id: "urlbar-result-menu-dismiss-suggestion2",
        },
      },
      {
        name: RESULT_MENU_COMMAND.NOT_INTERESTED,
        l10n: {
          id: "firefox-suggest-command-dont-show-mdn2",
        },
      },
      { name: "separator" },
      {
        name: RESULT_MENU_COMMAND.MANAGE,
        l10n: {
          id: "urlbar-result-menu-manage-firefox-suggest2",
        },
      }
    );

    return commands;
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
      case RESULT_MENU_COMMAND.MANAGE:
        // "manage" is handled by UrlbarInput, no need to do anything here.
        break;
      // selType == "dismiss" when the user presses the dismiss key shortcut.
      case RESULT_MENU_COMMAND.DISMISS:
        lazy.QuickSuggest.dismissResult(result);
        controller.removeResult(result, {
          acknowledgeDismissalL10n: {
            id: "firefox-suggest-dismissal-acknowledgment-one-mdn",
          },
        });
        break;
      case RESULT_MENU_COMMAND.NOT_INTERESTED:
        lazy.UrlbarPrefs.set("suggest.mdn", false);
        controller.removeResult(result, {
          acknowledgeDismissalL10n: {
            id: "firefox-suggest-dismissal-acknowledgment-all-mdn",
          },
        });
        break;
      case RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY:
        this.handleShowLessFrequently(controller, result);
        lazy.UrlbarPrefs.set("mdn.minKeywordLength", searchString.length + 1);
        break;
    }
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        "mdn.showLessFrequentlyCount",
        this.showLessFrequentlyCount + 1
      );
    }
  }

  get showLessFrequentlyCount() {
    const count = lazy.UrlbarPrefs.get("mdn.showLessFrequentlyCount") || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    const cap =
      lazy.UrlbarPrefs.get("mdn.showLessFrequentlyCap") ||
      lazy.QuickSuggest.config.showLessFrequentlyCap ||
      0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  get #minKeywordLength() {
    let minLength = lazy.UrlbarPrefs.get("mdn.minKeywordLength");
    return Math.max(minLength, 0);
  }
}
