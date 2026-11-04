/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ActionsProvider } from "moz-src:///browser/components/urlbar/ActionsProvider.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import {ActionsResult} from "moz-src:///browser/components/urlbar/ActionsProvider.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  QuickActionsLoaderDefault:
    "moz-src:///browser/components/urlbar/QuickActionsLoaderDefault.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
});

// These prefs are relative to the `browser.urlbar` branch.
const ENABLED_PREF = "suggest.quickactions";
const MATCH_IN_PHRASE_PREF = "quickactions.matchInPhrase";
const MIN_SEARCH_PREF = "quickactions.minimumSearchString";

/**
 * @typedef QuickActionsDefinition
 * @property {string[]} commands
 *   The possible typed entries that this command will be displayed for.
 * @property {string} icon
 *   The URI of the icon associated with this command.
 * @property {string} label
 *   The id of the label for the result element.
 * @property {() => boolean} [isUnsupported]
 *   Called to check whether this action's underlying feature or capability is
 *   unavailable, e.g. disabled by a pref or policy, or missing on this build.
 *   When it returns true the action is hidden everywhere, including the full
 *   actions list shown in search mode. Defaults to supported.
 * @property {() => boolean} [isInactive]
 *   Called to check whether this action, though supported, can't be used right
 *   now, e.g. mute when no tab is playing audio. When it returns true the
 *   action is hidden from the urlbar but still shown, as a disabled entry, in
 *   the full actions list used for feature discoverability. Defaults to active.
 * @property {(queryContext, controller, window) => null|{focusContent: boolean}} onPick
 *   The function to call when the quick action is picked. It may return an object
 *   with property focusContent to indicate if the content area should be focussed
 *   after the pick.
 */

/**
 * A provider that matches the urlbar input to built in actions.
 */
class ProviderQuickActions extends ActionsProvider {
  get name() {
    return "ActionsProviderQuickActions";
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} _controller
   * @returns {boolean}
   */
  isActive(queryContext, _controller) {
    return (
      queryContext.sapName == "urlbar" &&
      lazy.UrlbarPrefs.get(ENABLED_PREF) &&
      !queryContext.restrictInSearchMode() &&
      queryContext.trimmedSearchString.length < 50 &&
      queryContext.trimmedSearchString.length >=
        lazy.UrlbarPrefs.get(MIN_SEARCH_PREF)
    );
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} _controller
   * @returns {Promise<ActionsResult[]>}
   */
  async queryActions(queryContext, _controller) {
    let input = queryContext.trimmedLowerCaseSearchString;
    let results = await this.getActions({ input });

    if (lazy.UrlbarPrefs.get(MATCH_IN_PHRASE_PREF)) {
      for (let [keyword, keys] of this.#keywords) {
        if (input.includes(keyword) && keys.length) {
          keys.forEach(key => results.add(key));
        }
      }
    }

    // Remove actions that are unsupported or currently inactive.
    results.forEach(key => {
      const action = this.#actions.get(key);
      if (action.isUnsupported?.() || action.isInactive?.()) {
        results.delete(key);
      }
    });

    if (!results.size) {
      return null;
    }

    return [...results].map(key => {
      let action = this.#actions.get(key);
      return /** @type {ActionsResult} */ ({
        providerName: this.name,
        key,
        l10nId: action.label,
        icon: action.icon,
        dataset: {
          action: key,
          inputLength: queryContext.trimmedSearchString.length,
        },
      });
    });
  }

  async getActions({ input, includesExactMatch = false }) {
    await lazy.QuickActionsLoaderDefault.ensureLoaded();

    let results = new Set(this.#prefixes.get(input));

    if (includesExactMatch) {
      let actions = this.#keywords.get(input);
      actions?.forEach(action => results.add(action));
    }

    return results;
  }

  getAction(key) {
    return this.#actions.get(key);
  }

  onPick(queryContext, controller, actionResult, _details) {
    this.pickAction(
      queryContext,
      controller,
      actionResult.key,
      actionResult.dataset.inputLength
    );
  }

  pickAction(queryContext, controller, key, inputLength) {
    inputLength = Math.min(inputLength, 10);
    Glean.urlbarQuickaction.picked[`${key}-${inputLength}`].add(1);
    let options = this.#actions.get(key).onPick(queryContext, controller);
    if (options?.focusContent) {
      controller.browserWindow.gBrowser.selectedBrowser.focus();
    }
  }

  /**
   * Adds a new QuickAction.
   *
   * @param {string} key A key to identify this action.
   * @param {QuickActionsDefinition} definition An object that describes the action.
   */
  addAction(key, definition) {
    this.#actions.set(key, definition);
    definition.commands.forEach(cmd => {
      let keys = this.#keywords.get(cmd) ?? [];
      keys.push(key);
      this.#keywords.set(cmd, keys);
    });
    this.#loopOverPrefixes(definition.commands, prefix => {
      let result = this.#prefixes.get(prefix);
      if (result) {
        result.add(key);
      } else {
        result = new Set([key]);
      }
      this.#prefixes.set(prefix, result);
    });
  }

  /**
   * Removes an action.
   *
   * @param {string} key A key to identify this action.
   */
  removeAction(key) {
    let definition = this.#actions.get(key);
    this.#actions.delete(key);
    definition.commands.forEach(cmd => {
      let keys = this.#keywords.get(cmd) ?? [];
      this.#keywords.set(
        cmd,
        keys.filter(k => k != key)
      );
    });
    this.#loopOverPrefixes(definition.commands, prefix => {
      let result = this.#prefixes.get(prefix);
      if (result) {
        result.delete(key);
      }
      this.#prefixes.set(prefix, result);
    });
  }

  /**
   * A map from keywords to an action.
   *
   * @type {Map<string, Array>}
   */
  #keywords = new Map();

  /**
   * A map of all prefixes to an array of actions.
   *
   * @type {Map<string, Set>}
   */
  #prefixes = new Map();

  /**
   * The actions that have been added.
   *
   * @type {Map<string, QuickActionsDefinition>}
   */
  #actions = new Map();

  #loopOverPrefixes(commands, fun) {
    for (const command of commands) {
      // Loop over all the prefixes of the word, ie
      // "", "w", "wo", "wor", stopping just before the full
      // word itself which will be matched by the whole
      // phrase matching.
      for (let i = 1; i <= command.length; i++) {
        let prefix = command.substring(0, command.length - i);
        fun(prefix);
      }
    }
  }
}

export var ActionsProviderQuickActions = new ProviderQuickActions();
