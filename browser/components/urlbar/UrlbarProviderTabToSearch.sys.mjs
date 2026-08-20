/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that offers a search engine when the user is
 * typing a search engine domain.
 */

import {
  UrlbarProvider,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ActionsProviderContextualSearch:
    "moz-src:///browser/components/urlbar/ActionsProviderContextualSearch.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderAutofill:
    "moz-src:///browser/components/urlbar/UrlbarProviderAutofill.sys.mjs",
  UrlbarProviderGlobalActions:
    "moz-src:///browser/components/urlbar/UrlbarProviderGlobalActions.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
});

const DYNAMIC_RESULT_TYPE = "onboardTabToSearch";
const VIEW_TEMPLATE = {
  attributes: {
    selectable: true,
  },
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
              name: "first-row-container",
              tag: "span",
              children: [
                {
                  name: "title",
                  tag: "span",
                  classList: ["urlbarView-title"],
                  children: [
                    {
                      name: "titleStrong",
                      tag: "strong",
                    },
                  ],
                },
                {
                  name: "title-separator",
                  tag: "span",
                  classList: ["urlbarView-title-separator"],
                },
                {
                  name: "action",
                  tag: "span",
                  classList: ["urlbarView-action"],
                  attributes: {
                    "slide-in": true,
                  },
                },
              ],
            },
            {
              name: "description",
              tag: "span",
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
export class UrlbarProviderTabToSearch extends UrlbarProvider {
  static onboardingInteractionAtTime = null;

  constructor() {
    super();
  }

  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    return (
      queryContext.searchString &&
      queryContext.tokens.length == 1 &&
      !queryContext.restrictInSearchMode() &&
      lazy.UrlbarPrefs.get("suggest.engines") &&
      !(
        (await this.queryInstance
          .getProvider(lazy.UrlbarProviderGlobalActions.name)
          ?.isActive(queryContext)) &&
        lazy.ActionsProviderContextualSearch.isActive(queryContext)
      )
    );
  }

  /**
   * Gets the provider's priority.
   *
   * @returns {number} The provider's priority for the given query.
   */
  getPriority() {
    return 0;
  }

  getViewTemplate(_result) {
    return VIEW_TEMPLATE;
  }

  /**
   * This is called only for dynamic result types.
   *
   * @param {UrlbarResult} result The result whose view will be updated.
   * @returns {object} An object describing the view update.
   */
  getViewUpdate(result) {
    return {
      icon: {
        attributes: {
          src: result.payload.icon,
        },
      },
      titleStrong: {
        l10n: {
          id: "urlbar-result-action-search-w-engine",
          args: {
            engine: result.payload.engine,
          },
        },
      },
      action: {
        l10n: {
          id: result.payload.isGeneralPurposeEngine
            ? "urlbar-result-action-tabtosearch-web"
            : "urlbar-result-action-tabtosearch-other-engine",
          args: {
            engine: result.payload.engine,
          },
        },
      },
      description: {
        l10n: {
          id: "urlbar-tabtosearch-onboard",
        },
      },
    };
  }

  /**
   * Called when a result from the provider is selected. "Selected" refers to
   * the user highlighing the result with the arrow keys/Tab, before it is
   * picked. onSelection is also called when a user clicks a result. In the
   * event of a click, onSelection is called just before onEngagement.
   *
   * @param {UrlbarResult} result
   *   The result that was selected.
   */
  onSelection(result) {
    // We keep track of the number of times the user interacts with
    // tab-to-search onboarding results so we stop showing them after
    // `tabToSearch.onboard.interactionsLeft` interactions.
    // Also do not increment the counter if the result was interacted with less
    // than 5 minutes ago. This is a guard against the user running up the
    // counter by interacting with the same result repeatedly.
    if (
      result.payload.dynamicType &&
      (!UrlbarProviderTabToSearch.onboardingInteractionAtTime ||
        UrlbarProviderTabToSearch.onboardingInteractionAtTime <
          Date.now() - 1000 * 60 * 5)
    ) {
      let interactionsLeft = lazy.UrlbarPrefs.get(
        "tabToSearch.onboard.interactionsLeft"
      );

      if (interactionsLeft > 0) {
        lazy.UrlbarPrefs.set(
          "tabToSearch.onboard.interactionsLeft",
          --interactionsLeft
        );
      }

      UrlbarProviderTabToSearch.onboardingInteractionAtTime = Date.now();
    }
  }

  /**
   * Defines whether the view should defer user selection events while waiting
   * for the first result from this provider.
   *
   * @returns {boolean} Whether the provider wants to defer user selection
   *          events.
   */
  get deferUserSelection() {
    return true;
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    // enginesForDomainPrefix only matches against engine domains.
    // Remove trailing slashes and www. from the search string and check if the
    // resulting string is worth matching.
    let [searchStr] = lazy.UrlbarShared.stripPrefixAndTrim(
      queryContext.searchString,
      {
        stripWww: true,
        trimSlash: true,
      }
    );
    // Skip any string that cannot be an origin.
    if (
      !lazy.UrlUtils.looksLikeOrigin(searchStr, {
        ignoreKnownDomains: true,
        noIp: true,
      })
    ) {
      return;
    }

    // Also remove the public suffix, if present, to allow for partial matches.
    if (searchStr.includes(".")) {
      searchStr = UrlbarUtils.stripPublicSuffixFromHost(searchStr);
    }

    // Add all matching engines.
    let engines = await lazy.UrlbarSearchUtils.enginesForDomainPrefix(
      searchStr,
      {
        matchAllDomainLevels: true,
      }
    );
    if (!engines.length) {
      return;
    }

    const onboardingInteractionsLeft = lazy.UrlbarPrefs.get(
      "tabToSearch.onboard.interactionsLeft"
    );

    // If the engine host begins with the search string, autofill may happen
    // for it, and the Muxer will retain the result only if there's a matching
    // autofill heuristic result.
    // Otherwise, we may have a partial match, where the search string is at
    // the boundary of a host part, for example "wiki" in "en.wikipedia.org".
    // We put those engines apart, and later we check if their host satisfies
    // the autofill threshold. If they do, we mark them with the
    // "satisfiesAutofillThreshold" payload property, so the muxer can avoid
    // filtering them out.
    let partialMatchEnginesByHost = new Map();

    for (let engine of engines) {
      // Trim the engine host. This will also be set as the result url, so the
      // Muxer can use it to filter.
      let [host] = lazy.UrlbarShared.stripPrefixAndTrim(
        engine.searchUrlDomain,
        {
          stripWww: true,
        }
      );
      // Check if the host may be autofilled.
      if (host.startsWith(searchStr.toLocaleLowerCase())) {
        if (onboardingInteractionsLeft > 0) {
          addCallback(this, makeOnboardingResult(engine));
        } else {
          addCallback(this, makeResult(queryContext, engine));
        }
        continue;
      }

      // Otherwise it may be a partial match that would not be autofilled.
      if (host.includes("." + searchStr.toLocaleLowerCase())) {
        partialMatchEnginesByHost.set(engine.searchUrlDomain, engine);
        // Don't continue here, we are looking for more partial matches.
      }
      // We also try to match the base domain of the searchUrlDomain,
      // because otherwise for an engine like rakuten, we'd check pt.afl.rakuten.co.jp
      // which redirects and is thus not saved in the history resulting in a low score.

      let baseDomain = Services.eTLD.getBaseDomainFromHost(
        engine.searchUrlDomain
      );
      if (baseDomain.startsWith(searchStr)) {
        partialMatchEnginesByHost.set(baseDomain, engine);
      }
    }
    if (partialMatchEnginesByHost.size) {
      let host = await lazy.UrlbarProviderAutofill.getTopHostOverThreshold(
        queryContext,
        Array.from(partialMatchEnginesByHost.keys())
      );
      if (host) {
        let engine = partialMatchEnginesByHost.get(host);
        if (onboardingInteractionsLeft > 0) {
          addCallback(this, makeOnboardingResult(engine, true));
        } else {
          addCallback(this, makeResult(queryContext, engine, true));
        }
      }
    }
  }
}

function makeOnboardingResult(engine, satisfiesAutofillThreshold = false) {
  return new lazy.UrlbarResult({
    type: lazy.UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
    resultSpan: 2,
    suggestedIndex: 1,
    payload: {
      engine: engine.name,
      searchUrlDomainWithoutSuffix: searchUrlDomainWithoutSuffix(engine),
      providesSearchMode: true,
      icon: lazy.UrlbarShared.ICON.SEARCH_GLASS,
      dynamicType: DYNAMIC_RESULT_TYPE,
      satisfiesAutofillThreshold,
    },
  });
}

function makeResult(context, engine, satisfiesAutofillThreshold = false) {
  return new lazy.UrlbarResult({
    type: lazy.UrlbarShared.RESULT_TYPE.SEARCH,
    source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
    suggestedIndex: 1,
    payload: {
      engine: engine.name,
      isGeneralPurposeEngine: engine.isGeneralPurposeEngine,
      searchUrlDomainWithoutSuffix: searchUrlDomainWithoutSuffix(engine),
      providesSearchMode: true,
      icon: lazy.UrlbarShared.ICON.SEARCH_GLASS,
      query: "",
      satisfiesAutofillThreshold,
    },
  });
}

function searchUrlDomainWithoutSuffix(engine) {
  let [value] = lazy.UrlbarShared.stripPrefixAndTrim(engine.searchUrlDomain, {
    stripWww: true,
  });
  return value.substr(0, value.length - engine.searchUrlPublicSuffix.length);
}
