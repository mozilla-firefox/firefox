/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import { SapLocation, ContextWebsite } from "chrome://browser/content/urlbar/SmartbarInput.mjs"
 * @import { SmartbarAction } from "chrome://browser/content/aiwindow/components/input-cta/input-cta.mjs"
 * @import { ChatSubmitType } from "chrome://browser/content/aiwindow/components/ai-window/ai-window.mjs"
 */

/**
 * @typedef {object} SmartbarCommitDetails
 * @property {string} value - The query string
 * @property {SapLocation} location - The location of the input
 * @property {ContextWebsite[]} [contextMentions] - Context mentions
 * @property {?string} [contextPageUrl] - The current page URL context, as a
 *   string because URL objects can't be sent in actor messages
 * @property {SmartbarAction} [detectedIntent] - The detected intent
 * @property {ChatSubmitType} [submitType] - How the submit was triggered
 */

/**
 * JSWindowActor to pass a query from the Urlbar to the Smartbar.
 */
export class AISmartBarParent extends JSWindowActorParent {
  /**
   * Submit a chat query to the smartbar.
   *
   * @param {SmartbarCommitDetails} details - The details for the `smartbar-commit` event
   */
  async ask(details) {
    this.sendAsyncMessage("AskFromParent", details);
  }
}
