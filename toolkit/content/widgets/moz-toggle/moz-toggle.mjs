/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at htp://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-toggle.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozBaseInputElement } from "../lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-label.mjs";

/**
 * A simple toggle element that can be used to switch between two states.
 *
 * @tagname moz-toggle
 * @slot support-link - Used to append a moz-support-link to the description.
 * @fires toggle
 *  Custom event indicating that the toggle's pressed state has changed.
 */
export class MozToggle extends MozBaseInputElement {
  static properties = {
    pressed: { type: Boolean, reflect: true },
  };

  static activatedProperty = "pressed";

  /** @internal */
  get buttonEl() {
    return this.inputEl;
  }

  constructor() {
    super();

    /**
     * Whether or not the element is pressed.
     *
     * @type {boolean}
     */
    this.pressed = false;
  }

  handleClick() {
    this.pressed = !this.pressed;
    this.dispatchOnUpdateComplete(
      new CustomEvent("toggle", {
        bubbles: true,
        composed: true,
      })
    );
  }

  inputTemplate() {
    const { pressed, disabled, ariaLabel, handleClick } = this;
    return html`<button
      id="input"
      part="button"
      type="button"
      class="toggle-button"
      name=${this.name}
      value=${this.value}
      ?disabled=${disabled}
      aria-pressed=${pressed}
      aria-label=${ifDefined(ariaLabel ?? undefined)}
      aria-describedby="description"
      aria-description=${ifDefined(
        this.hasDescription ? undefined : this.ariaDescription
      )}
      accesskey=${ifDefined(this.accessKey)}
      title=${ifDefined(this.title)}
      @click=${handleClick}
    ></button>`;
  }

  inputStylesTemplate() {
    return html`<link
      rel="stylesheet"
      href="chrome://global/content/elements/moz-toggle.css"
    />`;
  }
}
customElements.define("moz-toggle", MozToggle);
