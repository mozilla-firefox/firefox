/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ActionsProviderQuickActions:
    "moz-src:///browser/components/urlbar/ActionsProviderQuickActions.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  RemoteL10n: "resource:///modules/asrouter/RemoteL10n.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});

// Notified by ASRouter after it reloads messages, e.g. on experiment enrollment.
const TOPIC_MESSAGES_LOADED = "asrouter:messages-loaded";
// Prefixed to message keys so they can't overwrite built-in quick actions.
const KEY_PREFIX = "fxms_";
const ALLOWED_ACTIONS = [
  "SHOW_SPOTLIGHT",
  "OPEN_SUPPORT_PAGE",
  "OPEN_ABOUT_PAGE",
  "OPEN_PREFERENCES_PAGE",
  "SELECT_AND_MARK_MENU_ITEM",
];

/**
 * Loads quick actions from `urlbar_action` messaging system messages.
 */
export class QuickActionsLoaderMessaging {
  static #loadedPromise = null;
  static #registeredKeys = [];
  static #reloadPending = false;

  /**
   * Replaces the registered actions with those of the matching messages.
   */
  static async load() {
    await lazy.ASRouter.waitForInitialized;
    let messages = await lazy.ASRouter.handleMessageRequest(
      /** @type {any} */ ({ template: "urlbar_action", returnAll: true })
    );
    let definitions = new Map();
    for (let message of messages ?? []) {
      try {
        let { key, keywords, label, icon, action } = message.content;
        if (!ALLOWED_ACTIONS.includes(action.type)) {
          continue;
        }
        // Keywords and label are either raw strings, which Nimbus experiments
        // deliver already translated for the user's locale, or Fluent
        // `string_id`s from in-tree messages.
        let formattedKeywords = await Promise.all(
          keywords.map(k => lazy.RemoteL10n.formatLocalizableText(k))
        );
        let labelText = await lazy.RemoteL10n.formatLocalizableText(label);
        definitions.set(KEY_PREFIX + key, {
          commands: formattedKeywords
            .flatMap(k => k?.split(",") ?? [])
            .map(k => k.trim().toLowerCase()),
          labelText,
          icon,
          onPick: (_queryContext, controller) => {
            lazy.SpecialMessageActions.handleAction(
              action,
              controller.browserWindow.gBrowser.selectedBrowser
            ).catch(console.error);
            return null;
          },
        });
      } catch (e) {
        console.error(`Couldn't load urlbar action from ${message.id}`, e);
      }
    }

    for (let key of this.#registeredKeys) {
      if (lazy.ActionsProviderQuickActions.getAction(key)) {
        lazy.ActionsProviderQuickActions.removeAction(key);
      }
    }
    this.#registeredKeys = [];
    for (let [key, definition] of definitions) {
      lazy.ActionsProviderQuickActions.addAction(key, definition);
      this.#registeredKeys.push(key);
    }
  }

  static init() {
    Services.obs.addObserver(() => this.reload(), TOPIC_MESSAGES_LOADED);
    this.#loadedPromise = this.load().catch(console.error);
  }

  static reload() {
    // Not initialized yet, or a reload is already waiting to run.
    if (!this.#loadedPromise || this.#reloadPending) {
      return;
    }
    this.#reloadPending = true;
    this.#loadedPromise = this.#loadedPromise
      .then(() => {
        this.#reloadPending = false;
        return this.load();
      })
      .catch(console.error);
  }
}
