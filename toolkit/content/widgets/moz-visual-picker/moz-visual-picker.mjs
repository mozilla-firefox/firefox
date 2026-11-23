/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-visual-picker.tagmap.d.ts" />

import {
  html,
  nothing,
  classMap,
} from "chrome://global/content/vendor/lit.all.mjs";
import {
  SelectControlItemMixin,
  SelectControlBaseElement,
} from "../lit-select-control.mjs";
import { MozLitElement } from "../lit-utils.mjs";
import { ifDefined } from "../vendor/lit.all.mjs";

/**
 * An element that groups related items and allows a user to navigate between
 * them to select an item. The appearance of the items of the group is
 * determined by the consumer.
 *
 * @tagname moz-visual-picker
 * @slot default - The picker's content, intended for moz-visual-picker-items.
 */
export class MozVisualPicker extends SelectControlBaseElement {
  static childElementName = "moz-visual-picker-item";
}
customElements.define("moz-visual-picker", MozVisualPicker);

/**
 * Element that allows a user to select one option from a group of options.
 * Visual appearance is determined by the slotted content.
 *
 * @tagname moz-visual-picker-item
 * @slot default - The item's content, used for what gets displayed.
 */
export class MozVisualPickerItem extends SelectControlItemMixin(MozLitElement) {
  static properties = {
    label: { type: String, fluent: true },
    description: { type: String, fluent: true },
    ariaLabel: { type: String, fluent: true, mapped: true },
    title: { type: String, mapped: true },
    imageSrc: { type: String },
    labelPosition: { type: String, reflect: true },
  };

  static queries = {
    itemEl: ".picker-item",
    labelEl: ".label",
    descriptionEl: ".description",
  };

  constructor() {
    super();

    /**
     * Visible label for the picker item.
     *
     * @type {string | undefined}
     */
    this.label = undefined;

    /**
     * Additional text shown beneath the label.
     *
     * @type {string | undefined}
     */
    this.description = undefined;

    /**
     * Value for the aria-label attribute of the picker item.
     *
     * @type {string | undefined}
     */
    this.ariaLabel = undefined;

    /**
     * Native title tooltip, mapped onto the inner shadow DOM item.
     *
     * @type {string | undefined}
     */
    this.title = undefined;

    /**
     * Path to an image to display in the picker item.
     *
     * @type {string | undefined}
     */
    this.imageSrc = undefined;

    /**
     * Position of the label, inside the picker item button or outside it as a
     * clickable label element.
     *
     * @type {"inside" | "outside"}
     */
    this.labelPosition = "inside";

    /**
     * Whether the item is disabled.
     *
     * @type {boolean | undefined}
     */
    this.disabled = undefined;
  }

  click() {
    this.itemEl.click();
  }

  focus() {
    this.itemEl.focus();
  }

  blur() {
    this.itemEl.blur();
  }

  handleKeydown(event) {
    if (event.code == "Space" || event.code == "Enter") {
      event.preventDefault();
      this.handleClick(event);
    }
  }

  handleClick(event) {
    // re-target click events from the slot to the item and handle clicks from
    // space bar keydown.
    event.stopPropagation();
    this.dispatchEvent(
      new Event("click", {
        bubbles: true,
        composed: true,
      })
    );

    super.handleClick();

    if (event.type === "click") {
      this.focus();
    }

    // Manually dispatch events since we're not using an input.
    this.dispatchEvent(
      new Event("input", {
        bubbles: true,
        composed: true,
      })
    );
    this.dispatchEvent(
      new Event("change", {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleSlotchange(event) {
    // If the user hasn't provide a visual or accessible label fallback to
    // labelling the picker item based on slotted content.
    if (!this.label && !this.ariaLabel) {
      let elements = event.target.assignedElements();
      this.itemEl.ariaLabelledByElements = elements;
    }
  }

  contentTemplate() {
    if (
      !this.imageSrc &&
      (this.labelPosition == "outside" || (!this.label && !this.description))
    ) {
      return html`<slot></slot>`;
    }

    return html`
      ${this.imageSrc
        ? html`<img src=${this.imageSrc} role="presentation" part="image" />`
        : nothing}
      ${this.labelPosition != "outside" ? this.textTemplate() : nothing}
    `;
  }

  textTemplate() {
    if (!this.label) {
      return "";
    }

    const labelElement =
      this.labelPosition == "outside"
        ? html`<label class="label" id="label" for="picker-item"
            >${this.label}</label
          >`
        : html`<p class="label" id="label">${this.label}</p>`;
    return html`<div class="text-content">
      ${this.label ? labelElement : nothing}
      ${this.description
        ? html`<p class="description" id="description">${this.description}</p>`
        : nothing}
    </div>`;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-visual-picker-item.css"
      />
      <button
        id="picker-item"
        class=${classMap({
          "picker-item": true,
          "image-item": this.imageSrc && this.label,
        })}
        role=${this.role}
        value=${this.value}
        aria-label=${ifDefined(this.ariaLabel)}
        aria-labelledby=${ifDefined(
          this.label && !this.ariaLabel ? "label" : undefined
        )}
        aria-describedby=${ifDefined(
          this.description && this.label ? "description" : undefined
        )}
        title=${ifDefined(this.title)}
        aria-checked=${this.role == "radio" ? this.checked : nothing}
        aria-selected=${this.role == "option" ? this.checked : nothing}
        tabindex=${this.itemTabIndex}
        ?checked=${this.checked}
        ?disabled=${this.isDisabled}
        @click=${this.handleClick}
        @keydown=${this.handleKeydown}
        @slotchange=${this.handleSlotchange}
      >
        ${this.contentTemplate()}
      </button>
      ${this.labelPosition == "outside" ? this.textTemplate() : nothing}
    `;
  }
}
customElements.define("moz-visual-picker-item", MozVisualPickerItem);
