/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

const DEFAULT_FAVICON = "chrome://global/skin/icons/defaultFavicon.svg";

/**
 * Tab shown in a confirmation card row.
 *
 * @typedef {object} TabSelectionData
 * @property {string} url - Tab URL
 * @property {string} title - Display title
 * @property {string} [iconSrc] - Favicon URL
 */

/**
 * A single tab row inside an action confirmation card.
 *
 * @property {TabSelectionData} tab - Tab this row represents
 */
export class AIActionConfirmationTabRow extends MozLitElement {
  static properties = {
    tab: { type: Object },
  };

  constructor() {
    super();
    this.tab = null;
  }

  /**
   * Open URL or switch to tab. URL if it is already open.
   *
   * @param {MouseEvent} event
   */
  #handleClick(event) {
    event.preventDefault();

    const { shiftKey, metaKey, ctrlKey, altKey, button } = event;
    // Modifier clicks forwards to the parent actor for resolving.
    const hasModifier =
      shiftKey || metaKey || ctrlKey || altKey || button !== 0;

    this.dispatchEvent(
      new CustomEvent("AIChatContent:OpenLink", {
        bubbles: true,
        composed: true,
        detail: {
          url: this.tab.url,
          preferSwitchToTab: !hasModifier,
          shiftKey,
          metaKey,
          ctrlKey,
          altKey,
          button,
        },
      })
    );
  }

  render() {
    if (!this.tab) {
      return nothing;
    }
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-action-confirmation-tab-row.css"
      />
      <a
        class="tab-row"
        href=${this.tab.url}
        title=${this.tab.url}
        @click=${this.#handleClick}
      >
        <img
          class="tab-row-icon"
          alt=""
          src=${this.tab.iconSrc || DEFAULT_FAVICON}
        />
        <span class="tab-row-label">${this.tab.title}</span>
      </a>
    `;
  }
}

customElements.define(
  "ai-action-confirmation-tab-row",
  AIActionConfirmationTabRow
);
