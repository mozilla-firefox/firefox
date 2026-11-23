/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-page-header.tagmap.d.ts" />

import { html } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozPageHeader.ftl");

/**
 * A header component for providing context about a specific page.
 *
 * @tagname moz-page-header
 * @slot breadcrumbs - Container for a <moz-breadcrumb-group, shown above the heading.
 * @fires navigate-back
 *  Event indicating the backwards navigation should occur.
 */
export class MozPageHeader extends MozLitElement {
  static properties = {
    heading: { type: String, fluent: true },
    description: { type: String, fluent: true },
    iconSrc: { type: String },
    supportPage: { type: String, attribute: "support-page" },
    backButton: { type: Boolean },
    badge: { type: String },
    headingLevel: { type: Number },
  };

  static queries = {
    headingEl: "#heading",
    backButtonEl: "moz-button",
  };

  constructor() {
    super();

    /**
     * The page title text.
     *
     * @type {string}
     */
    this.heading = "";

    /**
     * Secondary text shown under the heading.
     *
     * @type {string}
     */
    this.description = "";

    /**
     * The src for an optional icon.
     *
     * @type {string}
     */
    this.iconSrc = "";

    /**
     * Name of the SUMO support page to link to.
     *
     * @type {string}
     */
    this.supportPage = "";

    /**
     * Whether or not the header includes a back button.
     *
     * @type {boolean}
     */
    this.backButton = false;

    /**
     * Includes a badge of this type with matching text.
     *
     * @type {"beta" | "new" | undefined}
     */
    this.badge = undefined;

    /**
     * Heading level to render (1-6).
     *
     * @type {number}
     */
    this.headingLevel = 2;
  }

  backButtonTemplate() {
    if (!this.backButton) {
      return "";
    }
    return html`<moz-button
      type="ghost"
      data-l10n-id="back-nav-button-title2"
      iconsrc="chrome://global/skin/icons/arrow-left.svg"
      class="back-button"
      @click=${this.handleBack}
    ></moz-button>`;
  }

  iconTemplate() {
    if (!this.iconSrc) {
      return "";
    }
    return html`<img src=${this.iconSrc} role="presentation" class="icon" />`;
  }

  descriptionTemplate() {
    if (!this.description) {
      return "";
    }
    return html`<span class="description" id="description">
        ${this.description}
      </span>
      ${this.supportLinkTemplate()}`;
  }

  supportLinkTemplate() {
    if (!this.supportPage) {
      return "";
    }
    return html`<a
      is="moz-support-link"
      support-page=${this.supportPage}
      part="support-link"
      class="support-link"
      aria-describedby=${this.description ? "description" : "heading"}
    ></a>`;
  }

  badgeTemplate() {
    if (!this.badge) {
      return "";
    }
    return html`<moz-badge type=${this.badge}></moz-badge>`;
  }

  handleBack() {
    this.dispatchEvent(new Event("navigate-back"));
  }

  headingTemplate() {
    switch (this.headingLevel) {
      case 1:
        return html`<h1 id="heading">${this.heading}</h1>`;
      case 3:
        return html`<h3 id="heading">${this.heading}</h3>`;
      case 4:
        return html`<h4 id="heading">${this.heading}</h4>`;
      case 5:
        return html`<h5 id="heading">${this.heading}</h5>`;
      case 6:
        return html`<h6 id="heading">${this.heading}</h6>`;
      case 2:
      default:
        return html`<h2 id="heading">${this.heading}</h2>`;
    }
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-page-header.css"
      />
      <link
        rel="stylesheet"
        href="chrome://global/skin/design-system/text-and-typography.css"
      />
      <div class="page-header-container">
        <slot name="breadcrumbs"></slot>
        <div class="heading">
          ${this.backButtonTemplate()}${this.iconTemplate()}
          ${this.headingTemplate()} ${this.badgeTemplate()}
          ${!this.description ? this.supportLinkTemplate() : ""}
        </div>
        ${this.descriptionTemplate()}
      </div>
    `;
  }
}
customElements.define("moz-page-header", MozPageHeader);
