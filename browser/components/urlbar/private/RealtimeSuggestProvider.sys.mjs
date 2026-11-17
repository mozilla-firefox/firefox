/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SuggestProvider } from "moz-src:///browser/components/urlbar/private/SuggestFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * A Suggest feature that manages realtime suggestions (a.k.a. "carrots"),
 * including both opt-in and online suggestions for a given realtime type. It is
 * intended to be subclassed rather than used as is.
 *
 * Each subclass should manage one realtime type. If the user has not opted in
 * to online suggestions, this class will serve the realtime type's opt-in
 * suggestion. Once the user has opted in, it will switch to serving online
 * Merino suggestions for the realtime type.
 */
export class RealtimeSuggestProvider extends SuggestProvider {
  // The following methods must be overridden.

  /**
   * The type of the realtime suggestion provider.
   *
   * @type {string}
   */
  get realtimeType() {
    throw new Error("Trying to access the base class, must be overridden");
  }

  getViewTemplateForDescriptionTop(_item, _index) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  getViewTemplateForDescriptionBottom(_item, _index) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  getViewUpdateForPayloadItem(_item, _index, _controller) {
    throw new Error("Trying to access the base class, must be overridden");
  }

  // The following getters depend on `realtimeType` and should be overridden as
  // necessary.

  /**
   * @returns {string[]}
   *   The opt-in suggestion is a dynamic Rust suggestion. `suggestion_type` in
   *   the RS record is `${this.realtimeType}_opt_in` by default.
   */
  get dynamicRustSuggestionTypes() {
    return [this.realtimeType + "_opt_in"];
  }

  /**
   * @returns {string}
   *   The online suggestions are served by Merino. The Merino provider is
   *   `this.realtimeType` by default.
   */
  get merinoProvider() {
    return this.realtimeType;
  }

  get baseTelemetryType() {
    return this.realtimeType;
  }

  get realtimeTypeForFtl() {
    return this.realtimeType.replace(/([A-Z])/g, "-$1").toLowerCase();
  }

  get featureGatePref() {
    return this.realtimeType + "FeatureGate";
  }

  get suggestPref() {
    return "suggest." + this.realtimeType;
  }

  get minKeywordLengthPref() {
    return this.realtimeType + ".minKeywordLength";
  }

  get showLessFrequentlyCountPref() {
    return this.realtimeType + ".showLessFrequentlyCount";
  }

  get optInIcon() {
    return `chrome://browser/skin/illustrations/${this.realtimeType}-opt-in.svg`;
  }

  get optInTitleL10n() {
    return {
      id: `urlbar-result-${this.realtimeTypeForFtl}-opt-in-title`,
    };
  }

  get optInDescriptionL10n() {
    return {
      id: `urlbar-result-${this.realtimeTypeForFtl}-opt-in-description`,
      parseMarkup: true,
    };
  }

  get notInterestedCommandL10n() {
    return {
      id: `urlbar-result-menu-dont-show-${this.realtimeTypeForFtl}2`,
    };
  }

  get acknowledgeDismissalL10n() {
    return {
      id: "urlbar-result-dismissal-acknowledgment-" + this.realtimeTypeForFtl,
    };
  }

  get ariaGroupL10n() {
    return {
      id: "urlbar-result-aria-group-" + this.realtimeTypeForFtl,
      attribute: "aria-label",
    };
  }

  get isSponsored() {
    return false;
  }

  /**
   * @returns {string}
   *   The dynamic result type that will be set in the Merino result's payload
   *   as `result.payload.dynamicType`. Note that "dynamic" here refers to the
   *   concept of dynamic result types as used in the view and
   *   `UrlbarShared.RESULT_TYPE.DYNAMIC`, not Rust dynamic suggestions.
   *
   *   If you override this, make sure the value starts with "realtime-" because
   *   there are CSS rules that depend on that.
   */
  get dynamicResultType() {
    return "realtime-" + this.realtimeType;
  }

  // The following methods can be overridden but hopefully it's not necessary.

  get rustSuggestionType() {
    return "Dynamic";
  }

  get enablingPreferences() {
    return [
      "suggest.quicksuggest.all",
      "suggest.realtimeOptIn",
      "quicksuggest.realtimeOptIn.dismissTypes",
      "quicksuggest.realtimeOptIn.notNowTimeSeconds",
      "quicksuggest.realtimeOptIn.notNowReshowAfterPeriodDays",
      "quickSuggestOnlineAvailable",
      "quicksuggest.online.enabled",
      this.featureGatePref,
      this.suggestPref,

      // We could include the sponsored pref only if `this.isSponsored` is true,
      // but for maximum flexibility `this.isSponsored` is only a fallback for
      // when individual suggestions do not have an `isSponsored` property.
      // Since individual suggestions may be sponsored or not, we include the
      // pref here.
      "suggest.quicksuggest.sponsored",
    ];
  }

  get primaryUserControlledPreferences() {
    return [
      "suggest.realtimeOptIn",
      "quicksuggest.realtimeOptIn.dismissTypes",
      "quicksuggest.realtimeOptIn.notNowTimeSeconds",
      "quicksuggest.realtimeOptIn.notNowReshowAfterPeriodDays",
      this.suggestPref,
    ];
  }

  get shouldEnable() {
    if (
      !lazy.UrlbarPrefs.get(this.featureGatePref) ||
      !lazy.UrlbarPrefs.get("quickSuggestOnlineAvailable") ||
      !lazy.UrlbarPrefs.get("suggest.quicksuggest.all")
    ) {
      // The feature gate is disabled, online suggestions aren't available, or
      // all Suggest suggestions are disabled. Don't show opt-in or online
      // suggestions for this realtime type.
      return false;
    }

    if (lazy.UrlbarPrefs.get("quicksuggest.online.enabled")) {
      // Online suggestions are enabled. Show this realtime type if the user
      // didn't disable it.
      return lazy.UrlbarPrefs.get(this.suggestPref);
    }

    if (!lazy.UrlbarPrefs.get("suggest.realtimeOptIn")) {
      // The user dismissed opt-in suggestions for all realtime types.
      return false;
    }

    let dismissTypes = lazy.UrlbarPrefs.get(
      "quicksuggest.realtimeOptIn.dismissTypes"
    );
    if (dismissTypes.has(this.realtimeType)) {
      // The user dismissed opt-in suggestions for this realtime type.
      return false;
    }

    let notNowTimeSeconds = lazy.UrlbarPrefs.get(
      "quicksuggest.realtimeOptIn.notNowTimeSeconds"
    );
    if (!notNowTimeSeconds) {
      return true;
    }

    let notNowReshowAfterPeriodDays = lazy.UrlbarPrefs.get(
      "quicksuggest.realtimeOptIn.notNowReshowAfterPeriodDays"
    );

    let timeSecs = notNowReshowAfterPeriodDays * 24 * 60 * 60;
    return Date.now() / 1000 - notNowTimeSeconds > timeSecs;
  }

  isSuggestionSponsored(suggestion) {
    switch (suggestion.source) {
      case "merino":
        if (suggestion.hasOwnProperty("is_sponsored")) {
          return !!suggestion.is_sponsored;
        }
        break;
      case "rust":
        if (suggestion.data?.result?.payload?.hasOwnProperty("isSponsored")) {
          return suggestion.data.result.payload.isSponsored;
        }
        break;
    }
    return this.isSponsored;
  }

  /**
   * The telemetry type for a suggestion from this provider. (This string does
   * not include the `${source}_` prefix, e.g., "rust_".)
   *
   * Since realtime providers serve two types of suggestions, the opt-in and the
   * online suggestion, this will return two possible telemetry types depending
   * on the passed-in suggestion. Telemetry types for each are:
   *
   *   Opt-in suggestion: `${this.baseTelemetryType}_opt_in`
   *   Online suggestion: this.baseTelemetryType
   *
   * Individual suggestions can override these telemetry types, but that's
   * expected to be uncommon.
   *
   * @param {object} suggestion
   *   A suggestion from this provider.
   * @returns {string}
   *   The suggestion's telemetry type.
   */
  getSuggestionTelemetryType(suggestion) {
    switch (suggestion.source) {
      case "merino":
        if (suggestion.hasOwnProperty("telemetry_type")) {
          return suggestion.telemetry_type;
        }
        break;
      case "rust":
        if (suggestion.data?.result?.payload?.hasOwnProperty("telemetryType")) {
          return suggestion.data.result.payload.telemetryType;
        }
        return this.baseTelemetryType + "_opt_in";
    }
    return this.baseTelemetryType;
  }

  filterSuggestions(suggestions) {
    // The Rust opt-in suggestion can always be matched regardless of whether
    // online is enabled, so return only Merino suggestions when it is enabled.
    if (lazy.UrlbarPrefs.get("quicksuggest.online.enabled")) {
      return suggestions.filter(s => s.source == "merino");
    }
    return suggestions;
  }

  makeResult(queryContext, suggestion, searchString) {
    // For maximum flexibility individual suggestions can indicate whether they
    // are sponsored or not, despite `this.isSponsored`, which is a fallback.
    if (
      !lazy.UrlbarPrefs.get("suggest.quicksuggest.all") ||
      (this.isSuggestionSponsored(suggestion) &&
        !lazy.UrlbarPrefs.get("suggest.quicksuggest.sponsored"))
    ) {
      return null;
    }

    switch (suggestion.source) {
      case "merino":
        return this.makeMerinoResult(queryContext, suggestion, searchString);
      case "rust":
        return this.makeOptInResult(queryContext, suggestion);
    }
    return null;
  }

  makeMerinoResult(
    queryContext,
    suggestion,
    searchString,
    additionalOptions = {}
  ) {
    if (!this.isEnabled) {
      return null;
    }

    if (
      this.showLessFrequentlyCount &&
      searchString.length < this.#minKeywordLength
    ) {
      return null;
    }

    let values = suggestion.custom_details?.[this.merinoProvider]?.values;
    if (!values?.length) {
      return null;
    }

    let engine;
    if (values.some(v => v.query)) {
      engine = lazy.UrlbarSearchUtils.getDefaultEngine(queryContext.isPrivate);
      if (!engine) {
        return null;
      }
    }

    let result = new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.DYNAMIC,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      isBestMatch: true,
      ...additionalOptions,
      payload: {
        items: values.map((v, i) => this.makePayloadItem(v, i)),
        dynamicType: this.dynamicResultType,
        engine: engine?.name,
      },
    });

    return result;
  }

  /**
   * Returns the object that should be stored as `result.payload.items[i]` for
   * the Merino result. The default implementation here returns the
   * corresponding value in the suggestion.
   *
   * It's useful to override this if there's a significant amount of logic
   * that's used by the different code paths of the view update. In that case,
   * you can override this method, perform the logic, store the results in the
   * item, and then your different view update paths can all use it.
   *
   * @param {object} value
   *   The value in the suggestion's `values` array.
   * @param {number} _index
   *   The index of the value in the array.
   * @returns {object}
   *   The object that should be stored in `result.payload.items[_index]`.
   */
  makePayloadItem(value, _index) {
    return value;
  }

  makeOptInResult(queryContext, _suggestion) {
    let notNowTypes = lazy.UrlbarPrefs.get(
      "quicksuggest.realtimeOptIn.notNowTypes"
    );
    let splitButtonMain = notNowTypes.has(this.realtimeType)
      ? {
          command: "dismiss",
          l10n: {
            id: "urlbar-result-realtime-opt-in-dismiss",
          },
        }
      : {
          command: "not_now",
          l10n: {
            id: "urlbar-result-realtime-opt-in-not-now",
          },
        };

    return new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.TIP,
      source: lazy.UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
      isBestMatch: true,
      payload: {
        // This `type` is the tip type, required for `TIP` results.
        type: "realtime_opt_in",
        icon: this.optInIcon,
        titleL10n: this.optInTitleL10n,
        descriptionL10n: this.optInDescriptionL10n,
        descriptionLearnMoreTopic: lazy.QuickSuggest.HELP_TOPIC,
        buttons: [
          {
            command: "opt_in",
            l10n: {
              id: "urlbar-result-realtime-opt-in-allow",
            },
            input: queryContext.searchString,
            attributes: {
              primary: "",
            },
          },
          {
            ...splitButtonMain,
            menu: [
              {
                name: "not_interested",
                l10n: {
                  id: "urlbar-result-realtime-opt-in-dismiss-all2",
                },
              },
            ],
          },
        ],
      },
    });
  }

  getViewTemplate(result) {
    let { items } = result.payload;
    let hasMultipleItems = items.length > 1;
    return {
      name: "root",
      overflowable: true,
      attributes: {
        selectable: hasMultipleItems ? null : "",
        role: hasMultipleItems ? "group" : "option",
      },
      classList: ["urlbarView-realtime-root"],
      dataset: {
        // This `url` or `query` is used when there's only one item.
        url: items[0].url,
        query: items[0].query,
      },
      children: items.map((item, i) => ({
        name: `item_${i}`,
        tag: "span",
        classList: ["urlbarView-realtime-item"],
        attributes: {
          selectable: !hasMultipleItems ? null : "",
          role: hasMultipleItems ? "option" : "presentation",
        },
        dataset: {
          // These `url`s or `query`s are used when there are multiple items.
          url: item.url,
          query: item.query,
        },
        children: [
          ...this.getViewTemplateForImageContainer(item, i),
          {
            tag: "span",
            classList: ["urlbarView-realtime-description"],
            children: [
              {
                tag: "div",
                classList: ["urlbarView-realtime-description-top"],
                children: this.getViewTemplateForDescriptionTop(item, i),
              },
              {
                tag: "div",
                classList: ["urlbarView-realtime-description-bottom"],
                children: this.getViewTemplateForDescriptionBottom(item, i),
              },
            ],
          },
        ],
      })),
    };
  }

  /**
   * Returns the view template for the image container. This default
   * implementation creates a `span` with an `img` inside. Override it if you
   * need something else.
   *
   * @param {object} _item
   *   An item from the `result.payload.items` array.
   * @param {number} index
   *   The index of the item in the array.
   * @returns {Array}
   *   View template for the image container, an array of objects.
   */
  getViewTemplateForImageContainer(_item, index) {
    // Create an image inside a container so that the image appears inset into a
    // square. This is atypical because we normally use only an image and give
    // it padding and a background color to achieve that effect, but that only
    // works when the image size is fixed. Unfortunately Merino serves market
    // icons of different sizes due to its reliance on a third-party API.
    return [
      {
        name: `image_container_${index}`,
        tag: "span",
        classList: ["urlbarView-realtime-image-container"],
        children: [
          {
            name: `image_${index}`,
            tag: "img",
            classList: ["urlbarView-realtime-image"],
          },
        ],
      },
    ];
  }

  getViewUpdate(result, controller) {
    let { items } = result.payload;
    let hasMultipleItems = items.length > 1;

    let update = {
      root: {
        l10n: hasMultipleItems ? this.ariaGroupL10n : null,
      },
    };

    for (let i = 0; i < items.length; i++) {
      let item = items[i];
      Object.assign(
        update,
        this.getViewUpdateForPayloadItem(item, i, controller)
      );
    }

    return update;
  }

  getResultCommands(result) {
    if (result.payload.source == "rust") {
      // The opt-in result should not have a result menu.
      return null;
    }

    /** @type {UrlbarResultCommand[]} */
    let commands = [];

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
        name: "not_interested",
        l10n: this.notInterestedCommandL10n,
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
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   * @param {string} searchString
   */
  onEngagement(queryContext, controller, details, searchString) {
    switch (details.result.payload.source) {
      case "merino":
        this.onMerinoEngagement(
          queryContext,
          controller,
          details,
          searchString
        );
        break;
      case "rust":
        this.onOptInEngagement(queryContext, controller, details, searchString);
        break;
    }
  }

  onMerinoEngagement(queryContext, controller, details, searchString) {
    let { result } = details;
    switch (details.selType) {
      case "help":
      case "manage": {
        // "help" and "manage" are handled by UrlbarInput, no need to do
        // anything here.
        break;
      }
      case "not_interested": {
        lazy.UrlbarPrefs.set(this.suggestPref, false);
        controller.removeResult(result, {
          acknowledgeDismissalL10n: this.acknowledgeDismissalL10n,
        });
        break;
      }
      case "show_less_frequently": {
        this.handleShowLessFrequently(controller, result);
        lazy.UrlbarPrefs.set(
          this.minKeywordLengthPref,
          searchString.length + 1
        );
        break;
      }
    }
  }

  onOptInEngagement(queryContext, controller, details, _searchString) {
    switch (details.selType) {
      case "opt_in":
        lazy.UrlbarPrefs.set("quicksuggest.online.enabled", true);
        controller.input.startQuery({ allowAutofill: false });
        break;
      case "not_now": {
        lazy.UrlbarPrefs.set(
          "quicksuggest.realtimeOptIn.notNowTimeSeconds",
          Date.now() / 1000
        );
        lazy.UrlbarPrefs.add(
          "quicksuggest.realtimeOptIn.notNowTypes",
          this.realtimeType
        );
        controller.removeResult(details.result);
        break;
      }
      case "dismiss": {
        lazy.UrlbarPrefs.add(
          "quicksuggest.realtimeOptIn.dismissTypes",
          this.realtimeType
        );
        controller.removeResult(details.result, {
          acknowledgeDismissalL10n: this.acknowledgeDismissalL10n,
        });
        break;
      }
      case "not_interested": {
        lazy.UrlbarPrefs.set("suggest.realtimeOptIn", false);
        controller.removeResult(details.result, {
          acknowledgeDismissalL10n: {
            id: "urlbar-result-dismissal-acknowledgment-all",
          },
        });
        break;
      }
    }
  }

  incrementShowLessFrequentlyCount() {
    if (this.canShowLessFrequently) {
      lazy.UrlbarPrefs.set(
        this.showLessFrequentlyCountPref,
        this.showLessFrequentlyCount + 1
      );
    }
  }

  get showLessFrequentlyCount() {
    const pref = this.showLessFrequentlyCountPref;
    const count = lazy.UrlbarPrefs.get(pref) || 0;
    return Math.max(count, 0);
  }

  get canShowLessFrequently() {
    const cap =
      lazy.UrlbarPrefs.get("realtimeShowLessFrequentlyCap") ||
      lazy.QuickSuggest.config.showLessFrequentlyCap ||
      0;
    return !cap || this.showLessFrequentlyCount < cap;
  }

  get #minKeywordLength() {
    let hasUserValue = Services.prefs.prefHasUserValue(
      "browser.urlbar." + this.minKeywordLengthPref
    );
    let nimbusValue = lazy.UrlbarPrefs.get("realtimeMinKeywordLength");
    let minLength =
      hasUserValue || nimbusValue === null
        ? lazy.UrlbarPrefs.get(this.minKeywordLengthPref)
        : nimbusValue;
    return Math.max(minLength, 0);
  }
}
