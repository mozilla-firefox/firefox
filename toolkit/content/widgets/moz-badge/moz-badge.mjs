/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-badge.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozBadge.ftl");

/**
 * @typedef {"default" | "beta" | "new"} MozBadgeType Types of badges for moz-badge.
 */

/**
 * A simple badge element that can be used to indicate status or convey simple messages
 *
 * @tagname moz-badge
 */
export class MozBadge extends MozLitElement {
  static properties = {
    label: { type: String, fluent: true },
    iconSrc: { type: String },
    title: { type: String, fluent: true, mapped: true },
    type: { type: String, reflect: true },
  };

  constructor() {
    super();

    /**
     * Text to display on the badge, by default inferred from type.
     *
     * @type {string}
     */
    this.label = "";

    /**
     * The src for an optional icon shown next to the label.
     *
     * @type {string | undefined}
     */
    this.iconSrc = undefined;

    /**
     * The title of the badge, appears as a tooltip on hover.
     *
     * @type {string | undefined}
     */
    this.title = undefined;

    /**
     * The type of badge (e.g., "new").
     *
     * @type {MozBadgeType}
     */
    this.type = "default";
  }

  get labelL10nId() {
    if (this.type == "beta") {
      return "moz-badge-beta2";
    }
    if (this.type == "new") {
      return "moz-badge-new2";
    }

    return undefined;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-badge.css"
      />
      <div class="moz-badge" title=${ifDefined(this.title)}>
        ${this.iconSrc
          ? html`<img class="moz-badge-icon" src=${this.iconSrc} role="presentation"></img>`
          : ""}
        <span
          class="moz-badge-label"
          data-l10n-id=${ifDefined(this.label ? null : this.labelL10nId)}
          >${this.label}</span
        >
      </div>
    `;
  }
}
customElements.define("moz-badge", MozBadge);
