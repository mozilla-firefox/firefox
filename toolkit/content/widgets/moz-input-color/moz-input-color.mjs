/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-color.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

/**
 * @tagname moz-input-color
 */
export class MozInputColor extends MozLitElement {
  static properties = {
    value: { type: String },
    name: { type: String },
    label: { type: String, fluent: true },
  };

  static queries = {
    inputEl: ".swatch",
  };

  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  constructor() {
    super();

    /**
     * A CSS hex value of the color shown in the swatch area.
     *
     * @type {string}
     */
    this.value = "";

    /**
     * The name associated with the component's nested input element.
     *
     * @type {string}
     */
    this.name = "";

    /**
     * The text of the label.
     *
     * @type {string}
     */
    this.label = "";
  }

  /**
   * @param {Event} e
   */
  updateInputFromEvent(e) {
    /**
     * @type {HTMLInputElement}
     */
    const input = /** @type {object} */ (e.target);
    this.value = input.value;
  }

  /**
   * Dispatches an event from the host element so that outside
   * listeners can react to these events
   *
   * @param {Event} e
   * @memberof MozBaseInputElement
   */
  redispatchEvent(e) {
    this.updateInputFromEvent(e);

    let { bubbles, cancelable, composed, type } = e;
    let newEvent = new Event(type, {
      bubbles,
      cancelable,
      composed,
    });
    this.dispatchEvent(newEvent);
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-input-color.css"
      />

      <label title=${this.value}>
        <input
          type="color"
          name=${ifDefined(this.name)}
          .value=${this.value}
          class="swatch"
          @input=${this.updateInputFromEvent}
          @change=${this.redispatchEvent}
        />
        <span>${this.label}</span>
        <img
          class="icon"
          alt=""
          src="chrome://global/skin/icons/edit-outline.svg"
        />
      </label>
    `;
  }
}
customElements.define("moz-input-color", MozInputColor);
