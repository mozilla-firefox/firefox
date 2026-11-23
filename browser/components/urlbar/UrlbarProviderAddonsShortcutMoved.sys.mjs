/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that tells someone who used to open the
 * Add-ons Manager with the shortcut that Search Tabs now uses that the Add-ons
 * Manager shortcut moved.
 */

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

// Set when the former Add-ons Manager shortcut was used.
const LAST_USED_PREF = "browser.keys.openAddons.lastUsed";
const SHOWN_COUNT_PREF = "browser.keys.openAddons.movedShownCount";
const SHOWN_LIMIT = 3;

const DYNAMIC_RESULT_TYPE = "addonsShortcutMoved";
const VIEW_TEMPLATE = {
  children: [
    {
      name: "no-wrap",
      tag: "span",
      classList: ["urlbarView-no-wrap"],
      children: [
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
export class UrlbarProviderAddonsShortcutMoved extends UrlbarProvider {
  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  async isActive(queryContext) {
    return (
      !queryContext.searchString &&
      queryContext.searchMode?.source == lazy.UrlbarShared.RESULT_SOURCE.TABS &&
      queryContext.searchMode.entry == "shortcut" &&
      Services.prefs.prefHasUserValue(LAST_USED_PREF)
    );
  }

  getViewTemplate(_result) {
    return VIEW_TEMPLATE;
  }

  /**
   * @param {UrlbarResult} _result The result whose view will be updated.
   * @param {UrlbarParentController} controller
   * @returns {object} An object describing the view update.
   */
  getViewUpdate(_result, controller) {
    let document = controller.browserWindow.document;
    let args = {
      searchTabsShortcut: lazy.ShortcutUtils.prettifyShortcut(
        document.getElementById("key_searchTabs")
      ),
      addonsShortcut: lazy.ShortcutUtils.prettifyShortcut(
        document.getElementById("key_openAddons")
      ),
    };
    return {
      title: {
        l10n: {
          id: "urlbar-addons-shortcut-moved-title",
          args,
        },
      },
      description: {
        l10n: {
          id: "urlbar-addons-shortcut-moved-description",
          args,
        },
      },
    };
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   */
  onEngagement(queryContext, controller, details) {
    switch (details.selType) {
      case "change_shortcuts":
        lazy.URILoadingHelper.switchToTabHavingURI(
          controller.browserWindow,
          "about:keyboard",
          true
        );
        break;
      case "dismiss":
        break;
      default:
        return;
    }
    this.#stop();
    controller.view.close({ elementPicked: true });
  }

  onImpression() {
    // Picking "Got it" stops this but leaves the search session open, so the
    // session can still end with this result among the visible ones.
    if (!Services.prefs.prefHasUserValue(LAST_USED_PREF)) {
      return;
    }
    let shownCount = Services.prefs.getIntPref(SHOWN_COUNT_PREF, 0) + 1;
    if (shownCount < SHOWN_LIMIT) {
      Services.prefs.setIntPref(SHOWN_COUNT_PREF, shownCount);
    } else {
      this.#stop();
    }
  }

  #stop() {
    Services.prefs.clearUserPref(LAST_USED_PREF);
    Services.prefs.clearUserPref(SHOWN_COUNT_PREF);
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
      source: lazy.UrlbarShared.RESULT_SOURCE.TABS,
      suggestedIndex: 0,
      payload: {
        buttons: [
          {
            l10n: { id: "urlbar-addons-shortcut-moved-change-shortcuts" },
            command: "change_shortcuts",
            attributes: { name: "change_shortcuts" },
          },
          {
            l10n: { id: "urlbar-addons-shortcut-moved-dismiss" },
            command: "dismiss",
            attributes: { primary: true, name: "dismiss" },
          },
        ],
        dynamicType: DYNAMIC_RESULT_TYPE,
      },
    });
    addCallback(this, result);
  }
}
