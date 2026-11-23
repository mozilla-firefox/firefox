/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-radio-group.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import {
  SelectControlBaseElement,
  SelectControlItemMixin,
} from "../lit-select-control.mjs";
import { MozBaseInputElement } from "../lit-utils.mjs";

/**
 * Element used to group and associate moz-radio buttons so that they function
 * as a single form-control element.
 *
 * @tagname moz-radio-group
 * @slot default - The radio group's content, intended for moz-radio elements.
 * @slot support-link - The radio group's support link intended for moz-radio elements.
 */
export class MozRadioGroup extends SelectControlBaseElement {
  static childElementName = "moz-radio";

  static properties = {
    parentDisabled: { type: Boolean, state: true },
  };

  constructor() {
    super();

    /**
     * Whether the parent of the group is disabled.
     *
     * @type {boolean | undefined}
     */
    this.parentDisabled = undefined;

    this.orientation = "vertical";
  }
}
customElements.define("moz-radio-group", MozRadioGroup);

/**
 * Input element that allows a user to select one option from a group of options.
 *
 * @tagname moz-radio
 */
export class MozRadio extends SelectControlItemMixin(MozBaseInputElement) {
  static activatedProperty = "checked";

  get isDisabled() {
    return (
      super.isDisabled || this.parentDisabled || this.controller.parentDisabled
    );
  }

  inputTemplate() {
    return html`<input
      type="radio"
      id="input"
      .value=${this.value}
      name=${this.name}
      .checked=${this.checked}
      aria-checked=${this.checked}
      tabindex=${this.itemTabIndex}
      ?disabled=${this.isDisabled}
      accesskey=${ifDefined(this.accessKey)}
      aria-label=${ifDefined(this.ariaLabel ?? undefined)}
      aria-describedby="description"
      aria-description=${ifDefined(
        this.hasDescription ? undefined : this.ariaDescription
      )}
      title=${ifDefined(this.title)}
      @click=${this.handleClick}
      @change=${this.handleChange}
    />`;
  }
}
customElements.define("moz-radio", MozRadio);
