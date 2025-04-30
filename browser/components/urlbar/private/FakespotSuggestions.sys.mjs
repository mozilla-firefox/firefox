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
  UrlbarView: "resource:///modules/UrlbarView.sys.mjs",
});

const RESULT_MENU_COMMAND = {
  HELP: "help",
  MANAGE: "manage",
  NOT_INTERESTED: "not_interested",
  NOT_RELEVANT: "not_relevant",
  SHOW_LESS_FREQUENTLY: "show_less_frequently",
};

const KNOWN_SUGGESTION_PROVIDERS = new Set(["amazon", "bestbuy", "walmart"]);
const UNKNOWN_SUGGESTION_PROVIDER = "other";

const VIEW_TEMPLATE = {
  attributes: {
    selectable: true,
  },
  children: [
    {
      name: "icon",
      tag: "img",
      classList: ["urlbarView-favicon"],
    },
    {
      name: "body",
      tag: "span",
      overflowable: true,
      children: [
        {
          name: "title",
          tag: "span",
          classList: ["urlbarView-title"],
        },
        {
          name: "description",
          tag: "span",
          children: [
            {
              name: "rating-five-stars",
              tag: "moz-five-star",
            },
            {
              name: "rating-and-total-reviews",
              tag: "span",
            },
            {
              name: "badge",
              tag: "span",
            },
          ],
        },
        {
          name: "footer",
          tag: "span",
        },
      ],
    },
  ],
};

const REVIEWS_OVERFLOW = 99999;

/**
 * A feature that supports Fakespot suggestions.
 */
export class FakespotSuggestions extends SuggestProvider {
  constructor() {
    super();
    lazy.UrlbarResult.addDynamicResultType("fakespot");
    lazy.UrlbarView.addDynamicViewTemplate("fakespot", VIEW_TEMPLATE);
  }

  get enablingPreferences() {
    return [
      "fakespotFeatureGate",
      "suggest.fakespot",
      "suggest.quicksuggest.sponsored",
    ];
  }

  get primaryUserControlledPreference() {
    return "suggest.fakespot";
  }

  get rustSuggestionType() {
    return "Fakespot";
  }

  get showLessFrequentlyCount() {
    let count = lazy.UrlbarPrefs.get("fakespot.showLessFrequentlyCount") || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    let cap =
      lazy.UrlbarPrefs.get("fakespotShowLessFrequentlyCap") ||
      lazy.QuickSuggest.config.showLessFrequentlyCap ||
      0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  isSuggestionSponsored(_suggestion) {
    return true;
  }

  getSuggestionTelemetryType(suggestion) {
    return "fakespot_" + this.#parseProvider(suggestion);
  }

  makeResult(queryContext, suggestion, searchString) {
    if (!this.isEnabled || searchString.length < this.#minKeywordLength) {
      return null;
    }

    const payload = {
      url: suggestion.url,
      originalUrl: suggestion.url,
      title: [suggestion.title, lazy.UrlbarUtils.HIGHLIGHT.TYPED],
      rating: Number(suggestion.rating),
      totalReviews: Number(suggestion.totalReviews),
      fakespotGrade: suggestion.fakespotGrade,
      fakespotProvider: this.#parseProvider(suggestion),
      dynamicType: "fakespot",
    };

    return Object.assign(
      new lazy.UrlbarResult(
        lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC,
        lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
        ...lazy.UrlbarResult.payloadAndSimpleHighlights(
          queryContext.tokens,
          payload
        )
      ),
      {
        isSuggestedIndexRelativeToGroup: true,
        suggestedIndex: lazy.UrlbarPrefs.get("fakespotSuggestedIndex"),
      }
    );
  }

  getViewUpdate(result) {
    return {
      icon: {
        attributes: {
          src: result.payload.iconBlob,
        },
      },
      title: {
        textContent: result.payload.title,
        highlights: result.payloadHighlights.title,
      },
      "rating-five-stars": {
        attributes: {
          rating: result.payload.rating,
        },
      },
      "rating-and-total-reviews": {
        l10n:
          result.payload.totalReviews > REVIEWS_OVERFLOW
            ? {
                id: "firefox-suggest-fakespot-rating-and-total-reviews-overflow",
                args: {
                  rating: result.payload.rating,
                  totalReviews: REVIEWS_OVERFLOW,
                },
              }
            : {
                id: "firefox-suggest-fakespot-rating-and-total-reviews",
                args: {
                  rating: result.payload.rating,
                  totalReviews: result.payload.totalReviews,
                },
              },
      },
      badge: {
        l10n: {
          id: "firefox-suggest-fakespot-badge",
        },
        attributes: {
          grade: result.payload.fakespotGrade,
        },
      },
      footer: {
        l10n: {
          id: "firefox-suggest-fakespot-sponsored",
        },
      },
    };
  }

  getResultCommands() {
    let commands = [];

    if (this.canShowLessFrequently) {
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
          id: "firefox-suggest-command-manage-fakespot",
        },
        children: [
          {
            name: RESULT_MENU_COMMAND.NOT_RELEVANT,
            l10n: {
              id: "firefox-suggest-command-dont-show-this-suggestion",
            },
          },
          {
            name: RESULT_MENU_COMMAND.NOT_INTERESTED,
            l10n: {
              id: "firefox-suggest-command-dont-show-any-suggestions",
            },
          },
        ],
      },
      { name: "separator" },
      {
        name: RESULT_MENU_COMMAND.MANAGE,
        l10n: {
          id: "urlbar-result-menu-manage-firefox-suggest",
        },
      },
      {
        name: RESULT_MENU_COMMAND.HELP,
        l10n: {
          id: "urlbar-result-menu-learn-more-about-firefox-suggest",
        },
      }
    );

    return commands;
  }

  onEngagement(queryContext, controller, details, searchString) {
    let { result } = details;
    switch (details.selType) {
      case RESULT_MENU_COMMAND.HELP:
      case RESULT_MENU_COMMAND.MANAGE:
        // "help" and "manage" are handled by UrlbarInput, no need to do
        // anything here.
        break;
      // selType == "dismiss" when the user presses the dismiss key shortcut.
      case "dismiss":
      case RESULT_MENU_COMMAND.NOT_RELEVANT:
        lazy.QuickSuggest.dismissResult(result);
        result.acknowledgeDismissalL10n = {
          id: "firefox-suggest-dismissal-acknowledgment-one-fakespot",
        };
        controller.removeResult(result);
        break;
      case RESULT_MENU_COMMAND.NOT_INTERESTED:
        lazy.UrlbarPrefs.set("suggest.fakespot", false);
        result.acknowledgeDismissalL10n = {
          id: "firefox-suggest-dismissal-acknowledgment-all-fakespot",
        };
        controller.removeResult(result);
        break;
      case RESULT_MENU_COMMAND.SHOW_LESS_FREQUENTLY:
        controller.view.acknowledgeFeedback(result);
        this.incrementShowLessFrequentlyCount();
        if (!this.canShowLessFrequently) {
          controller.view.invalidateResultMenuCommands();
        }
        lazy.UrlbarPrefs.set(
          "fakespot.minKeywordLength",
          searchString.length + 1
        );
        break;
    }

    Glean.urlbar.fakespotEngagement.record({
      grade: result.payload.fakespotGrade,
      rating: String(result.payload.rating),
      provider: result.payload.fakespotProvider,
    });
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        "fakespot.showLessFrequentlyCount",
        this.showLessFrequentlyCount + 1
      );
    }
  }

  get #minKeywordLength() {
    // Use the pref value if it has a user value (which means the user clicked
    // "Show less frequently") or if there's no Nimbus value. Otherwise use the
    // Nimbus value. This lets us override the pref's default value using Nimbus
    // if necessary.
    let hasUserValue = Services.prefs.prefHasUserValue(
      "browser.urlbar.fakespot.minKeywordLength"
    );
    let nimbusValue = lazy.UrlbarPrefs.get("fakespotMinKeywordLength");
    let minLength =
      hasUserValue || nimbusValue === null
        ? lazy.UrlbarPrefs.get("fakespot.minKeywordLength")
        : nimbusValue;
    return Math.max(minLength, 0);
  }

  #parseProvider({ productId }) {
    // The Fakespot provider is encoded in the `productId` like this:
    // `{provider}-{id}`. To avoid recording unexpected values in telemetry that
    // might be dangerous or impact the user's privacy, we look up the parsed
    // provider in the `KNOWN_SUGGESTION_PROVIDERS` safe list and record it as
    // `UNKNOWN_SUGGESTION_PROVIDER` if it's absent.
    let provider = productId?.split("-")[0];
    return KNOWN_SUGGESTION_PROVIDERS.has(provider)
      ? provider
      : UNKNOWN_SUGGESTION_PROVIDER;
  }

  get _test_minKeywordLength() {
    return this.#minKeywordLength;
  }
}
