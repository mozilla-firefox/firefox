/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { SmartwindowOverflowRowMixin } from "chrome://browser/content/aiwindow/components/SmartwindowOverflowRow.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-chip.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-panel-list.mjs";

/**
 * A citation source.
 *
 * @typedef {object} Citation
 * @property {string} url - Source URL
 * @property {string} [title] - Display title
 * @property {string} [faviconUrl] - Favicon URL
 * @property {boolean} [hasFavicon] - Whether we already have a favicon stored for this URL
 */

/**
 * Renders citation pills for an assistant chat message.
 *
 * @property {Citation[]} citations - The sources to display.
 */
export class ChatAssistantCitations extends SmartwindowOverflowRowMixin(
  MozLitElement
) {
  static properties = {
    citations: { type: Array, attribute: false },
    isPanelOpen: { type: Boolean, state: true },
  };

  constructor() {
    super();
    this.citations = [];
    this.isPanelOpen = false;
  }

  /**
   * @returns {string} Selector for the citations row.
   */
  get overflowContainerSelector() {
    return ".citations";
  }

  /**
   * @returns {string} Selector for the “+n more” button.
   */
  get overflowTriggerSelector() {
    return ".citations-more";
  }

  /**
   * @returns {Citation[]} The items the row measures against.
   */
  get overflowItems() {
    return this.citations ?? [];
  }

  #panel() {
    return this.shadowRoot.querySelector("smartwindow-panel-list");
  }

  #onToggleClick(event) {
    const panel = this.#panel();
    if (panel) {
      panel.anchor = event.currentTarget;
      panel.toggle(event);
    }
  }

  #onPanelOpenLink() {
    this.#panel()?.hide();
  }

  #label(citation) {
    return (
      citation.title || (URL.parse(citation.url)?.hostname ?? citation.url)
    );
  }

  #titleText(citation) {
    return citation.title || citation.url;
  }

  #icon(citation) {
    if (citation.faviconUrl) {
      return citation.faviconUrl;
    }
    return citation.hasFavicon
      ? `page-icon:${citation.url}`
      : "chrome://global/skin/icons/defaultFavicon.svg";
  }

  #renderPill(citation, { itemRole, showTitle } = {}) {
    return html`<smartwindow-chip
      type="source"
      role=${itemRole ? "presentation" : nothing}
      .itemRole=${itemRole ?? ""}
      title=${showTitle ? this.#titleText(citation) : nothing}
      .label=${this.#label(citation)}
      .href=${citation.url}
      .iconSrc=${this.#icon(citation)}
    ></smartwindow-chip>`;
  }

  render() {
    if (!this.citations?.length) {
      return nothing;
    }

    const visibleCount = Math.min(this.visibleCount, this.citations.length);
    const citationsOverflow = this.citations.slice(visibleCount);
    const hasOverflow = !!citationsOverflow.length;

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-overflow-row.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/chat-assistant-citations.css"
      />
      <div class="citations smartwindow-overflow-row" role="list">
        ${this.citations.map(
          (citation, index) =>
            html`<span role="listitem" ?data-overflow=${index >= visibleCount}
              >${this.#renderPill(citation, { showTitle: true })}</span
            >`
        )}
        <moz-button
          class="citations-more"
          type="ghost"
          ?data-overflow=${!hasOverflow}
          .ariaHasPopup=${"menu"}
          .ariaExpanded=${String(this.isPanelOpen)}
          data-l10n-id="smartwindow-assistant-citations-more-label"
          data-l10n-args=${JSON.stringify({ count: citationsOverflow.length })}
          @click=${this.#onToggleClick}
        ></moz-button>
        ${hasOverflow
          ? html`<smartwindow-panel-list
              @shown=${() => (this.isPanelOpen = true)}
              @hidden=${() => (this.isPanelOpen = false)}
              @AIChatContent:OpenLink=${this.#onPanelOpenLink}
            >
              ${citationsOverflow.map(citation =>
                this.#renderPill(citation, { itemRole: "menuitem" })
              )}
            </smartwindow-panel-list>`
          : nothing}
      </div>
    `;
  }
}

customElements.define("chat-assistant-citations", ChatAssistantCitations);
