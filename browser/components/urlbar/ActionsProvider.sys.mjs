/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A provider that matches the urlbar input to built in actions.
 */
export class ActionsProvider {
  /**
   * Unique name for the provider.
   *
   * @abstract
   * @returns {string}
   */
  get name() {
    return "ActionsProviderBase";
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} _queryContext The query context object.
   * @param {UrlbarParentController} _controller The current controller.
   * @returns {boolean} Whether this provider should be invoked for the search.
   * @abstract
   */
  isActive(_queryContext, _controller) {
    throw new Error("Not implemented.");
  }

  /**
   * Query for actions based on the current users input.
   *
   * @param {UrlbarQueryContext} _queryContext The query context object.
   * @param {UrlbarParentController} _controller The current controller.
   * @returns {Promise<ActionsResult[]>}
   * @abstract
   */
  async queryActions(_queryContext, _controller) {
    throw new Error("Not implemented.");
  }

  /**
   * @param {UrlbarQueryContext} _queryContext
   * @param {UrlbarParentController} _controller
   * @param {ActionsResult} _action
   * @param {object} _details
   */
  onPick(_queryContext, _controller, _action, _details) {}
}

/**
 * A result produced by an `ActionsProvider`, describing one action button in
 * the actions row. It's a plain object (no methods, never deserialized) so it
 * stays structured-cloneable across the urlbar actor boundary.
 *
 * @typedef {object} ActionsResult
 * @property {string} providerName
 *   The name of the `ActionsProvider` that provided this actions result.
 * @property {string} key
 *   A string key used to distinguish between different actions.
 * @property {string} [l10nId]
 *   The id of the l10n string displayed in the action button, if any.
 * @property {{[arg: string]: any}} [l10nArgs]
 *   Arguments passed to construct the above string.
 * @property {string} [icon]
 *   The icon displayed in the button; the view falls back to a default when
 *   it's absent.
 * @property {{[key: string]: any}} [dataset]
 *   An object of properties we set on the action button that can be used to
 *   pass data when it is selected.
 * @param {{[property: string]: string}} [options.style]
 * @property {string} [engine]
 *   The name of an installed engine if the action prompts search mode.
 */
