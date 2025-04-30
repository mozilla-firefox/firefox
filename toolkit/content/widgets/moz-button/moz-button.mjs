/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-label.mjs";

/**
 * A button with multiple types and two sizes.
 *
 * @tagname moz-button
 * @property {string} label - The button's label, will be overridden by slotted content.
 * @property {string} type - The button type.
 *   Options: default, primary, destructive, icon, icon ghost, ghost.
 * @property {string} size - The button size.
 *   Options: default, small.
 * @property {boolean} disabled - The disabled state.
 * @property {string} title - The button's title attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} titleAttribute - Internal, map title attribute to the title JS property.
 * @property {string} tooltipText - Set the title property, the title attribute will be used first.
 * @property {string} ariaLabel - The button's arial-label attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} iconSrc - Path to the icon that should be displayed in the button.
 * @property {string} ariaLabelAttribute - Internal, map aria-label attribute to the ariaLabel JS property.
 * @property {string} hasVisibleLabel - Internal, tracks whether or not the button has a visible label.
 * @property {boolean} attention - Show a dot notification on the button if true.
 * @property {string} iconPosition - The icon's position relative to the button label.
 *   Options: start, end.
 * @property {HTMLButtonElement} buttonEl - The internal button element in the shadow DOM.
 * @property {HTMLButtonElement} slotEl - The internal slot element in the shadow DOM.
 * @cssproperty [--button-outer-padding-inline] - Used to set the outer inline padding of toolbar style buttons
 * @csspropert [--button-outer-padding-block] - Used to set the outer block padding of toolbar style buttons.
 * @cssproperty [--button-outer-padding-inline-start] - Used to set the outer inline-start padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-inline-end] - Used to set the outer inline-end padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block-start] - Used to set the outer block-start padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block-end] - Used to set the outer block-end padding of toolbar style buttons
 * @slot default - The button's content, overrides label property.
 * @fires click - The click event.
 */
export default class MozButton extends MozLitElement {
  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    label: { type: String, reflect: true, fluent: true },
    type: { type: String, reflect: true },
    size: { type: String, reflect: true },
    disabled: { type: Boolean, reflect: true },
    title: { type: String, mapped: true },
    tooltipText: { type: String, fluent: true },
    ariaLabel: { type: String, mapped: true },
    iconSrc: { type: String },
    hasVisibleLabel: { type: Boolean, state: true },
    accessKey: { type: String, mapped: true },
    attention: { type: Boolean },
    iconPosition: { type: String },
  };

  static queries = {
    buttonEl: "button",
    slotEl: "slot",
    backgroundEl: ".button-background",
  };

  constructor() {
    super();
    this.type = "default";
    this.size = "default";
    this.disabled = false;
    this.hasVisibleLabel = !!this.label;
    this.attention = false;
    this.iconPosition = "start";
  }

  // Delegate clicks on host to the button element.
  click() {
    this.buttonEl.click();
  }

  checkForLabelText() {
    this.hasVisibleLabel = this.slotEl
      ?.assignedNodes()
      .some(node => node.textContent.trim());
  }

  labelTemplate() {
    if (this.label) {
      return this.label;
    }
    return html`<slot @slotchange=${this.checkForLabelText}></slot>`;
  }

  iconTemplate(position) {
    if (this.iconSrc && position == this.iconPosition) {
      return html`<img src=${this.iconSrc} role="presentation" />`;
    }
    return null;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-button.css"
      />
      <button
        ?disabled=${this.disabled}
        title=${ifDefined(this.title || this.tooltipText)}
        aria-label=${ifDefined(this.ariaLabel)}
        accesskey=${ifDefined(this.accessKey)}
      >
        <span
          class=${classMap({
            labelled: this.label || this.hasVisibleLabel,
            "button-background": true,
            badged:
              (this.iconSrc || this.type.includes("icon")) && this.attention,
          })}
          part="button"
          type=${this.type}
          size=${this.size}
        >
          ${this.iconTemplate("start")}
          <label
            is="moz-label"
            shownaccesskey=${ifDefined(this.accessKey)}
            part="moz-button-label"
          >
            ${this.labelTemplate()}
          </label>
          ${this.iconTemplate("end")}
        </span>
      </button>
    `;
  }
}
customElements.define("moz-button", MozButton);
