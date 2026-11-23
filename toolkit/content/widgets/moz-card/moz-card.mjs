/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-card.tagmap.d.ts" />

import {
  html,
  staticHtml,
  literal,
  ifDefined,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

/**
 * Cards contain content and actions about a single subject.
 * There are two card types:
 * The default type where no type attribute is required and the card
 * will have no extra functionality.
 *
 * The "accordion" type will initially not show any content. The card
 * will contain an arrow to expand the card so that all of the content
 * is visible. You can use the "expanded" attribute to force the accordion
 * card to show its content on initial render.
 *
 * @slot content - The content to show inside of the card.
 */
export class MozCard extends MozLitElement {
  static queries = {
    detailsEl: "#moz-card-details",
    headingEl: "#heading",
    contentEl: "#content",
    summaryEl: "summary",
    contentSlotEl: "#content-slot",
  };

  static properties = {
    heading: { type: String, fluent: true },
    headingLevel: { type: Number },
    iconSrc: { type: String },
    type: { type: String, reflect: true },
    spacing: { type: String, reflect: true },
    expanded: { type: Boolean },
    role: { type: String, mapped: true },
    summaryTabIndex: { type: Number },
    treeItem: { type: Boolean },
    ariaLevel: { type: String, mapped: true },
    ariaPosInSet: { type: String, mapped: true },
    ariaSetSize: { type: String, mapped: true },
  };

  constructor() {
    super();

    /**
     * The heading text of the card.
     *
     * @type {string | undefined}
     */
    this.heading = undefined;

    /**
     * Heading level 1-6 used around the heading. 0 renders a regular span.
     *
     * @type {number}
     */
    this.headingLevel = 0;

    /**
     * Path to the icon displayed in the card.
     *
     * @type {string | undefined}
     */
    this.iconSrc = undefined;

    /**
     * The type of card.
     *
     * @type {"default" | "accordion"}
     */
    this.type = "default";

    /**
     * The level of spacing applied to the card.
     *
     * @type {"default" | "compact"}
     */
    this.spacing = "default";

    /**
     * Whether the accordion card shows its content.
     *
     * @type {boolean}
     */
    this.expanded = false;

    /**
     * Role of the article element in the card.
     *
     * @type {string | undefined}
     */
    this.role = undefined;

    /**
     * Tabindex for the accordion summary.
     *
     * @type {number | undefined}
     */
    this.summaryTabIndex = undefined;

    /**
     * Whether the accordion summary is an item of a tree. When set, the
     * summary gets the treeitem role, the article is presentational, and the
     * heading always renders as a span. Ignored unless the type is
     * "accordion".
     *
     * @type {boolean}
     */
    this.treeItem = false;

    /**
     * The aria-level of the accordion summary.
     *
     * @type {string | undefined}
     */
    this.ariaLevel = undefined;

    /**
     * The aria-posinset of the accordion summary.
     *
     * @type {string | undefined}
     */
    this.ariaPosInSet = undefined;

    /**
     * The aria-setsize of the accordion summary. With ariaLevel and
     * ariaPosInSet, this lets a tree item say where it is when its siblings
     * are in other cards. Ignored unless treeItem is set.
     *
     * @type {string | undefined}
     */
    this.ariaSetSize = undefined;
  }

  headingTextTemplate() {
    const headingLevels = [
      literal`span`,
      literal`h1`,
      literal`h2`,
      literal`h3`,
      literal`h4`,
      literal`h5`,
      literal`h6`,
    ];
    const tagName =
      (!this.#isTreeItem && headingLevels[this.headingLevel]) ||
      headingLevels[0];
    return staticHtml`<${tagName} id="heading" title=${ifDefined(this.heading)} part="heading">${this.heading}</${tagName}>`;
  }

  get #isTreeItem() {
    return this.type === "accordion" && this.treeItem;
  }

  headingTemplate() {
    if (!this.heading) {
      return "";
    }
    return html`
      <div id="heading-wrapper" part="moz-card-heading-wrapper">
        ${when(
          this.type == "accordion",
          () => html`<div class="chevron-icon"></div>`
        )}
        ${when(
          !!this.iconSrc,
          () =>
            html`<img
              id="heading-icon"
              src=${this.iconSrc}
              role="presentation"
            />`
        )}
        ${this.headingTextTemplate()}
      </div>
    `;
  }

  cardTemplate() {
    if (this.type === "accordion") {
      return html`
        <div class="moz-card-wrapper">
          <div id="cover-image"><slot name="cover-image"></slot></div>
          <details
            id="moz-card-details"
            @toggle=${this.onToggle}
            ?open=${this.expanded}
          >
            <summary
              part="summary"
              role=${ifDefined(this.#isTreeItem ? "treeitem" : undefined)}
              aria-level=${ifDefined(
                this.#isTreeItem ? this.ariaLevel : undefined
              )}
              aria-posinset=${ifDefined(
                this.#isTreeItem ? this.ariaPosInSet : undefined
              )}
              aria-setsize=${ifDefined(
                this.#isTreeItem ? this.ariaSetSize : undefined
              )}
              tabindex=${ifDefined(this.summaryTabIndex)}
            >
              ${this.headingTemplate()}
            </summary>
            <div id="content"><slot id="content-slot"></slot></div>
          </details>
        </div>
      `;
    }

    return html`
      <div id="moz-card-details">
        <div id="cover-image"><slot name="cover-image"></slot></div>
        ${this.headingTemplate()}
        <div id="content" aria-describedby="content">
          <slot></slot>
        </div>
      </div>
    `;
  }

  onToggle() {
    this.expanded = this.detailsEl.open;
    this.dispatchEvent(
      new ToggleEvent("toggle", {
        newState: this.detailsEl.open ? "open" : "closed",
        oldState: this.detailsEl.open ? "closed" : "open",
      })
    );
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/skin/design-system/text-and-typography.css"
      />
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-card.css"
      />
      <article
        role=${ifDefined(this.#isTreeItem ? "none" : this.role)}
        class="moz-card"
        aria-labelledby=${ifDefined(
          this.heading && !this.#isTreeItem ? "heading" : undefined
        )}
      >
        ${this.cardTemplate()}
      </article>
    `;
  }
}
customElements.define("moz-card", MozCard);
