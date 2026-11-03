/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-textarea.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

/**
 * A textarea custom element.
 *
 * @tagname moz-textarea
 * @property {string} label - The text of the label element.
 * @property {string} name - The name of the textarea control.
 * @property {string} value - The value of the textarea control.
 * @property {boolean} disabled - The disabled state of the textarea control.
 * @property {boolean} readonly - The readonly state of the textarea control.
 * @property {boolean} required - The required state of the textarea control
 * @property {string} description - The text for the description element that helps describe the textarea control.
 * @property {string} supportPage - Name of the SUMO support page to link to.
 * @property {string} placeholder - Text to display when the textarea has no value.
 * @property {string} iconSrc - The src for an optional icon.
 * @property {string} ariaLabel - The aria-label text when there is no visible label.
 * @property {string} ariaDescription - The aria-description text when there is no visible description.
 * @property {string} title - The title attribute, mapped onto the inner textarea.
 * @property {number} rows - The number of visible text rows.
 */
export class MozTextarea extends MozInputText {
  static properties = {
    ...MozInputText.properties,
    rows: { type: Number, reflect: true },
  };

  constructor() {
    super();
    this.rows = 2;
  }

  inputStylesTemplate() {
    return html`
      ${super.inputStylesTemplate()}
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-textarea.css"
      />
    `;
  }

  inputTemplate() {
    return html`
      <textarea
        id="input"
        name=${this.name}
        rows=${this.rows}
        .value=${this.value}
        ?disabled=${this.disabled || this.parentDisabled}
        ?readonly=${this.readonly}
        ?required=${this.required}
        accesskey=${ifDefined(this.accessKey)}
        placeholder=${ifDefined(this.placeholder)}
        aria-label=${ifDefined(this.ariaLabel ?? undefined)}
        aria-describedby="description"
        aria-description=${ifDefined(
          this.hasDescription ? undefined : this.ariaDescription
        )}
        title=${ifDefined(this.title)}
        @input=${this.handleInput}
        @change=${this.redispatchEvent}
      ></textarea>
    `;
  }
}
customElements.define("moz-textarea", MozTextarea);
