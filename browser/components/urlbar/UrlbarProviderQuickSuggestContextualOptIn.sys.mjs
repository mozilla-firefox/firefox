/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that offers a search engine when the user is
 * typing a search engine domain.
 */

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderTopSites:
    "moz-src:///browser/components/urlbar/UrlbarProviderTopSites.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

const DYNAMIC_RESULT_TYPE = "quickSuggestContextualOptIn";
const VIEW_TEMPLATE = {
  children: [
    {
      name: "no-wrap",
      tag: "span",
      classList: ["urlbarView-no-wrap"],
      children: [
        {
          name: "icon",
          tag: "img",
          classList: ["urlbarView-favicon"],
        },
        {
          name: "text-container",
          tag: "span",
          children: [
            {
              name: "title",
              tag: "strong",
            },
            {
              name: "description",
              tag: "span",
              children: [
                {
                  name: "learn_more",
                  tag: "a",
                  attributes: {
                    "data-command": "learn_more",
                    "data-l10n-name": "learn-more-link",
                    selectable: true,
                  },
                },
              ],
            },
          ],
        },
      ],
    },
  ],
};

/**
 * Class used to create the provider.
 */
export class UrlbarProviderQuickSuggestContextualOptIn extends UrlbarProvider {
  constructor() {
    super();
  }

  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC;
  }

  #shouldDisplayContextualOptIn(queryContext = null) {
    if (
      queryContext &&
      (queryContext.isPrivate ||
        queryContext.restrictSource ||
        queryContext.searchString ||
        queryContext.restrictInSearchMode())
    ) {
      return false;
    }

    // If the feature is disabled, or the user has already opted in, don't show
    // the onboarding.
    if (
      !lazy.UrlbarPrefs.get("quickSuggestEnabled") ||
      !lazy.UrlbarPrefs.get("quicksuggest.contextualOptIn") ||
      lazy.UrlbarPrefs.get("quicksuggest.online.enabled")
    ) {
      return false;
    }

    let lastDismissedTime = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.lastDismissedTime"
    );
    if (!lastDismissedTime) {
      return true;
    }

    let dismissedCount = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.dismissedCount"
    );

    let reshowAfterPeriodDays;
    switch (dismissedCount) {
      case 1: {
        reshowAfterPeriodDays = lazy.UrlbarPrefs.get(
          "quicksuggest.contextualOptIn.firstReshowAfterPeriodDays"
        );
        break;
      }
      case 2: {
        reshowAfterPeriodDays = lazy.UrlbarPrefs.get(
          "quicksuggest.contextualOptIn.secondReshowAfterPeriodDays"
        );
        break;
      }
      case 3: {
        reshowAfterPeriodDays = lazy.UrlbarPrefs.get(
          "quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays"
        );
        break;
      }
      default: {
        return false;
      }
    }

    let time = reshowAfterPeriodDays * 24 * 60 * 60;
    return Date.now() / 1000 - lastDismissedTime > time;
  }

  async isActive(queryContext) {
    if (!this.#shouldDisplayContextualOptIn(queryContext)) {
      return false;
    }

    // Evaluate impressions in order to dismiss.
    let firstImpressionTime = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.firstImpressionTime"
    );
    if (!firstImpressionTime) {
      return true;
    }

    let impressionCount = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.impressionCount"
    );
    let impressionLimit = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.impressionLimit"
    );

    if (impressionCount < impressionLimit) {
      return true;
    }

    let daysLimit = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.impressionDaysLimit"
    );
    let timeLimit = daysLimit * 24 * 60 * 60;
    if (Date.now() / 1000 - firstImpressionTime < timeLimit) {
      return true;
    }

    this.#dismiss();

    return false;
  }

  getPriority() {
    return lazy.UrlbarProviderTopSites.PRIORITY;
  }

  getViewTemplate(_result) {
    return VIEW_TEMPLATE;
  }

  /**
   * @param {UrlbarResult} _result The result whose view will be updated.
   * @returns {object} An object describing the view update.
   */
  getViewUpdate(_result) {
    return {
      icon: {
        attributes: {
          src: "chrome://branding/content/icon32.png",
        },
      },
      title: {
        l10n: {
          id: "urlbar-firefox-suggest-contextual-opt-in-title-1",
        },
      },
      description: {
        l10n: {
          id: "urlbar-firefox-suggest-contextual-opt-in-description-3",
        },
      },
    };
  }

  onBeforeSelection(result, element) {
    if (element.getAttribute("name") == "learn_more") {
      this.#a11yAlertRow(element.closest(".urlbarView-row"));
    }
  }

  #a11yAlertRow(row) {
    let alertText = row.querySelector(
      ".urlbarView-dynamic-quickSuggestContextualOptIn-title"
    ).textContent;
    let decription = row
      .querySelector(
        ".urlbarView-dynamic-quickSuggestContextualOptIn-description"
      )
      .cloneNode(true);
    // Remove the "Learn More" link.
    decription.firstElementChild?.remove();
    alertText += ". " + decription.textContent;
    row.ariaNotify(alertText);
  }

  onImpression(state, _queryContext, _controller, _resultsAndIndexes, details) {
    if (state == "engagement" && details.provider == this.name) {
      return;
    }

    let impressionCount = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.impressionCount"
    );
    lazy.UrlbarPrefs.set(
      "quicksuggest.contextualOptIn.impressionCount",
      impressionCount + 1
    );

    let firstImpressionTime = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.firstImpressionTime"
    );
    if (!firstImpressionTime) {
      lazy.UrlbarPrefs.set(
        "quicksuggest.contextualOptIn.firstImpressionTime",
        Date.now() / 1000
      );
    }
  }

  onEngagement(queryContext, controller, details) {
    // The clicked control's command rides `selType` (set from its data-command),
    // so it crosses the actor boundary; `details.element` is content-only.
    this._handleCommand(details.selType, controller, details.result);
  }

  _handleCommand(command, controller, result, container) {
    switch (command) {
      case "learn_more":
        controller.browserWindow.openHelpLink("firefox-suggest");
        break;
      case "allow":
        lazy.UrlbarPrefs.set("quicksuggest.online.enabled", true);
        break;
      case "dismiss":
        this.#dismiss();
        break;
      default:
        return;
    }

    // Picking one of these controls resolves the opt-in prompt, so close the
    // view. elementPicked marks this as an engagement, so the zero-prefix
    // telemetry and focus border are handled accordingly.
    controller.view.close({ elementPicked: true });

    // Remove the result if it shouldn't be active anymore due to above
    // actions.
    if (!this.#shouldDisplayContextualOptIn()) {
      if (result) {
        controller.removeResult(result);
      } else {
        // This is for when the UI is outside of standard results, after
        // one-off search buttons.
        container.hidden = true;
      }
    }
  }

  #dismiss() {
    lazy.UrlbarPrefs.set("quicksuggest.contextualOptIn.firstImpressionTime", 0);
    lazy.UrlbarPrefs.set("quicksuggest.contextualOptIn.impressionCount", 0);

    lazy.UrlbarPrefs.set(
      "quicksuggest.contextualOptIn.lastDismissedTime",
      Date.now() / 1000
    );
    let dismissedCount = lazy.UrlbarPrefs.get(
      "quicksuggest.contextualOptIn.dismissedCount"
    );
    lazy.UrlbarPrefs.set(
      "quicksuggest.contextualOptIn.dismissedCount",
      dismissedCount + 1
    );
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    let result = new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.DYNAMIC,
      source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
      suggestedIndex: 0,
      payload: {
        buttons: [
          {
            l10n: {
              id: "urlbar-firefox-suggest-contextual-opt-in-allow",
            },
            command: "allow",
            attributes: { primary: true, name: "allow" },
          },
          {
            l10n: {
              id: "urlbar-firefox-suggest-contextual-opt-in-dismiss",
            },
            command: "dismiss",
            attributes: { name: "dismiss" },
          },
        ],
        dynamicType: DYNAMIC_RESULT_TYPE,
      },
    });
    addCallback(this, result);
  }
}
