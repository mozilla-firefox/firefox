/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that returns all the available
 * actions for an actions search mode.
 */

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ActionsProviderQuickActions:
    "moz-src:///browser/components/urlbar/ActionsProviderQuickActions.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

// Default icon shown for actions if no custom one is provided.
const DEFAULT_ICON = "chrome://global/skin/icons/settings.svg";
const DYNAMIC_TYPE_NAME = "actions";

/**
 * A provider that lets the user view all available actions while in searchMode.
 */
export class UrlbarProviderActionsSearchMode extends UrlbarProvider {
  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  async isActive(queryContext) {
    return (
      queryContext.searchMode?.source == lazy.UrlbarShared.RESULT_SOURCE.ACTIONS
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
    let input = queryContext.trimmedLowerCaseSearchString;
    let results = await lazy.ActionsProviderQuickActions.getActions({
      input,
      includesExactMatch: true,
    });
    results.forEach(resultKey => {
      let action = lazy.ActionsProviderQuickActions.getAction(resultKey);
      if (action.isUnsupported?.()) {
        return;
      }
      let result = new lazy.UrlbarResult({
        type: lazy.UrlbarShared.RESULT_TYPE.DYNAMIC,
        source: lazy.UrlbarShared.RESULT_SOURCE.ACTIONS,
        payload: {
          key: resultKey,
          dynamicType: DYNAMIC_TYPE_NAME,
          inputLength: queryContext.trimmedLowerCaseSearchString.length,
        },
      });
      addCallback(this, result);
    });
  }

  /**
   * Whether an action's button is shown disabled. Shared by the view template
   * and the engagement handler so both agree without the latter reading the
   * picked DOM element. Unsupported actions are filtered out in `startQuery`,
   * so only inactive actions reach here.
   *
   * @param {object} action The quick action, from `getAction`.
   * @returns {boolean} Whether the action is inactive.
   */
  #isActionInactive(action) {
    return !!action.isInactive?.();
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   */
  onEngagement(queryContext, controller, details) {
    let { key, inputLength } = details.result.payload;
    let action = lazy.ActionsProviderQuickActions.getAction(key);
    if (this.#isActionInactive(action)) {
      return;
    }
    lazy.ActionsProviderQuickActions.pickAction(
      queryContext,
      controller,
      key,
      inputLength
    );
  }

  getViewTemplate(result) {
    let action = lazy.ActionsProviderQuickActions.getAction(result.payload.key);
    let isInactive = this.#isActionInactive(action);
    return {
      children: [
        {
          tag: "span",
          classList: ["urlbarView-action-btn"],
          attributes: {
            "data-action": result.payload.key,
            "data-input-length": result.payload.inputLength,
            role: "button",
            "aria-disabled": isInactive ? "true" : null,
            disabled: isInactive,
          },
          children: [
            {
              tag: "img",
              attributes: {
                src: action.icon || DEFAULT_ICON,
              },
            },
            {
              name: `label`,
              tag: "span",
            },
          ],
        },
      ],
    };
  }

  getViewUpdate(result) {
    let action = lazy.ActionsProviderQuickActions.getAction(result.payload.key);

    return {
      label: {
        l10n: { id: action.label },
      },
    };
  }
}
