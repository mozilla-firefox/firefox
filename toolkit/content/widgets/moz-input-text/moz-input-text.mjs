/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-text.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozBaseInputElement } from "../lit-utils.mjs";

/**
 * A text input custom element.
 *
 * @tagname moz-input-text
 */
export class MozInputText extends MozBaseInputElement {
  static properties = {
    placeholder: { type: String, fluent: true },
    readonly: { type: Boolean, reflect: true },
    required: { type: Boolean, reflect: true },
    maxLength: { type: Number },
  };
  static inputLayout = "block";

  constructor() {
    super();

    /**
     * Text to display when the input has no value.
     *
     * @type {string | undefined}
     */
    this.placeholder = undefined;

    /**
     * The readonly state of the input control.
     *
     * @type {boolean}
     */
    this.readonly = false;

    /**
     * The required state of the input control.
     *
     * @type {boolean}
     */
    this.required = false;

    this.value = "";
  }

  inputStylesTemplate() {
    return html`<link
      rel="stylesheet"
      href="chrome://global/content/elements/moz-input-text.css"
    />`;
  }

  handleInput(e) {
    this.value = e.target.value;
  }

  inputTemplate(options = {}) {
    let { type = "text", classes, styles, inputValue } = options;

    return html`
      <input
        id="input"
        type=${type}
        class=${ifDefined(classes)}
        style=${ifDefined(styles)}
        name=${this.name}
        .value=${inputValue || this.value}
        ?disabled=${this.disabled || this.parentDisabled}
        ?readonly=${this.readonly}
        ?required=${this.required}
        accesskey=${ifDefined(this.accessKey)}
        placeholder=${ifDefined(this.placeholder)}
        maxlength=${ifDefined(this.maxLength)}
        aria-label=${ifDefined(this.ariaLabel ?? undefined)}
        aria-describedby="description"
        aria-description=${ifDefined(
          this.hasDescription ? undefined : this.ariaDescription
        )}
        title=${ifDefined(this.title)}
        @input=${this.handleInput}
        @change=${this.redispatchEvent}
      />
    `;
  }

  requiredIndicatorTemplate() {
    return this.required
      ? html`<span class="required-indicator" aria-hidden="true">*</span>`
      : "";
  }
}
customElements.define("moz-input-text", MozInputText);
