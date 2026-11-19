/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-grouped-chip-container.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-page-actions.mjs";

/** @typedef {{ favicon?: string, title?: string, href: string }} SourceLink */

/**
 * Hero for a generated AI Tab page, rendered from the page config's `Header`
 * block. Title and subhead are model generated, so they are bound as text and
 * never as markup.
 *
 * The `eyebrow` field is expected to come back blank, so `createdAt` is
 * supplied by the renderer from the stored page's creation date rather than
 * from the page config.
 *
 * @property {string} createdAt - Ready to display run date, already localized
 *   by AITabParent. The component does no date handling of its own.
 * @property {string} title - Page title.
 * @property {string} subhead - One sentence of context below the title.
 * @property {SourceLink[]} references - Pages this report was built from.
 * @property {boolean} refreshing - Whether sources are being re-fetched.
 * @property {boolean} editable - Whether the page can be revised, which
 *   decides the page actions offered.
 */
export class AITabHeader extends MozLitElement {
  static properties = {
    createdAt: { type: String },
    title: { type: String },
    subhead: { type: String },
    references: { type: Array },
    refreshing: { type: Boolean },
    editable: { type: Boolean },
  };

  constructor() {
    super();
    this.createdAt = "";
    this.title = "";
    this.subhead = "";
    this.references = [];
    this.refreshing = false;
    this.editable = true;
  }

  #renderCreatedAt() {
    if (!this.createdAt) {
      return nothing;
    }

    return html`<span class="aitab-eyebrow">${this.createdAt}</span>`;
  }

  #renderReferences() {
    if (!this.references.length) {
      return nothing;
    }
    const chips = this.references.map(source => ({
      url: source.href,
      label: source.title || source.href,
      iconSrc: source.favicon ?? "",
    }));
    return html`<ai-grouped-chip-container
      class="aitab-references"
      .chips=${chips}
      .chipsLabel=${"smart-window-aitab-sources-count"}
      openLinkEvent="AITab:OpenLink"
    ></ai-grouped-chip-container>`;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/aitab-base.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/aitab-header.css"
      />
      <header class="aitab-header">
        <aitab-page-actions
          class="aitab-header-actions"
          ?refreshing=${this.refreshing}
          .editable=${this.editable}
        ></aitab-page-actions>
        ${this.#renderCreatedAt()}
        <h1 class="aitab-title">${this.title}</h1>
        ${this.subhead
          ? html`<p class="aitab-title-subtext">${this.subhead}</p>`
          : nothing}
        ${this.#renderReferences()}
      </header>
    `;
  }
}

customElements.define("aitab-header", AITabHeader);
