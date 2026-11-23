/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-box-button.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozBoxBase } from "../lit-utils.mjs";

/**
 * A button custom element used for navigating between sub-pages or opening
 * dialogs.
 *
 * @tagname moz-box-button
 */
export class MozBoxButton extends MozBoxBase {
  static shadowRootOptions = {
    ...super.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    disabled: { type: Boolean },
    accessKey: { type: String, mapped: true, fluent: true },
    parentDisabled: { type: Boolean, state: true },
  };

  static queries = {
    buttonEl: "button",
    navIcon: ".nav-icon",
  };

  constructor() {
    super();

    /**
     * Whether or not the button is disabled.
     *
     * @type {boolean}
     */
    this.disabled = false;

    /**
     * Key used for keyboard access.
     *
     * @type {string | undefined}
     */
    this.accessKey = undefined;

    /**
     * Whether the parent's state disables the button, see
     * MozBaseInputElement.
     *
     * @type {boolean | undefined}
     */
    this.parentDisabled = undefined;
  }

  click() {
    this.buttonEl.click();
  }

  labelTemplate() {
    if (!this.label) {
      return "";
    }
    return html`<label
      is="moz-label"
      class="label"
      shownaccesskey=${ifDefined(this.accessKey)}
      .textContent=${this.label}
    >
    </label>`;
  }

  render() {
    return html`
      ${super.stylesTemplate()}
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-box-button.css"
      />
      <button
        class="button"
        ?disabled=${this.disabled || this.parentDisabled}
        accesskey=${ifDefined(this.accessKey)}
      >
        ${super.textTemplate()}
        <img
          class="icon nav-icon"
          src="chrome://global/skin/icons/arrow-right.svg"
          alt=""
        />
      </button>
    `;
  }
}
customElements.define("moz-box-button", MozBoxButton);
