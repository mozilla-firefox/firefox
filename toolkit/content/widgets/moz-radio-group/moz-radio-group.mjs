/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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
 * @property {boolean} disabled - Whether or not the fieldset is disabled.
 * @property {string} label - Label for the group of moz-radio elements.
 * @property {string} ariaLabel - Accessible name for the group when no visible label is provided.
 * @property {string} description - Description for the group of moz-radio elements.
 * @property {string} supportPage - Support page for the group of moz-radio elements.
 * @property {number} headingLevel - Render the label in a heading of this level.
 * @property {string} name
 *  Input name of the radio group. Propagates to moz-radio children.
 * @property {string} value
 *  Selected value for the group. Changing the value updates the checked
 *  state of moz-radio children and vice versa.
 * @slot default - The radio group's content, intended for moz-radio elements.
 * @slot support-link - The radio group's support link intended for moz-radio elements.
 */
export class MozRadioGroup extends SelectControlBaseElement {
  static childElementName = "moz-radio";
  static orientation = "vertical";

  static properties = {
    parentDisabled: { type: Boolean, state: true },
  };
}
customElements.define("moz-radio-group", MozRadioGroup);

/**
 * Input element that allows a user to select one option from a group of options.
 *
 * @tagname moz-radio
 * @property {boolean} checked - Whether or not the input is selected.
 * @property {string} description - Description for the input.
 * @property {boolean} disabled - Whether or not the input is disabled.
 * @property {string} iconSrc - Path to an icon displayed next to the input.
 * @property {number} itemTabIndex - Tabindex of the input element.
 * @property {string} label - Label for the radio input.
 * @property {string} name
 *  Name of the input control, set by the associated moz-radio-group element.
 * @property {string} supportPage - Name of the SUMO support page to link to.
 * @property {string} value - Value of the radio input.
 * @property {string} ariaLabel - The aria-label text when there is no visible label.
 * @property {string} ariaDescription - The aria-description text when there is no visible description.
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
      @click=${this.handleClick}
      @change=${this.handleChange}
    />`;
  }
}
customElements.define("moz-radio", MozRadio);
