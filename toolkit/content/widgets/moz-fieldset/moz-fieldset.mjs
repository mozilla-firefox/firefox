/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-fieldset.tagmap.d.ts" />

import { classMap, html, ifDefined } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

/**
 * Functions to wrap a string in a heading.
 *
 * @type {Record<number, (label: string) => ReturnType<typeof html>>}
 */
const HEADING_LEVEL_TEMPLATES = {
  1: label => html`<h1 class="text-box-trim-start">${label}</h1>`,
  2: label => html`<h2 class="text-box-trim-start">${label}</h2>`,
  3: label => html`<h3 class="text-box-trim-start">${label}</h3>`,
  4: label => html`<h4>${label}</h4>`,
  5: label => html`<h5>${label}</h5>`,
  6: label => html`<h6>${label}</h6>`,
};

/**
 * Fieldset wrapper to lay out form inputs consistently.
 *
 * @tagname moz-fieldset
 */
export class MozFieldset extends MozLitElement {
  static properties = {
    label: { type: String, fluent: true },
    description: { type: String, fluent: true },
    supportPage: { type: String, attribute: "support-page" },
    ariaLabel: { type: String, fluent: true, mapped: true },
    ariaOrientation: { type: String, mapped: true },
    role: { type: String, mapped: true },
    inputsRole: { type: String, state: true },
    headingLevel: { type: Number },
    disabled: { type: Boolean, reflect: true },
    iconSrc: { type: String },
    badge: { type: String },
  };

  constructor() {
    super();

    /**
     * The label for the fieldset's legend.
     *
     * @type {string | undefined}
     */
    this.label = undefined;

    /**
     * The description for the fieldset.
     *
     * @type {string | undefined}
     */
    this.description = undefined;

    /**
     * Name of the SUMO support page to link to.
     *
     * @type {string | undefined}
     */
    this.supportPage = undefined;

    /**
     * Accessible name for the fieldset when there is no visible label.
     *
     * @type {string | undefined}
     */
    this.ariaLabel = undefined;

    /**
     * Orientation of the fieldset's inputs, exposed through aria-orientation.
     *
     * @type {string | undefined}
     */
    this.ariaOrientation = undefined;

    /**
     * Role of the inner fieldset element.
     *
     * @type {ARIAMixin['role']}
     */
    this.role = null;

    /**
     * When set, the #inputs container receives this role plus the accessible
     * name, description, orientation, and disabled state, instead of the inner
     * fieldset.
     *
     * @type {string | undefined}
     */
    this.inputsRole = undefined;

    /**
     * Renders the legend in a heading of this level.
     *
     * @type {number}
     */
    this.headingLevel = -1;

    /**
     * Whether the fieldset and its children are disabled.
     *
     * @type {boolean}
     */
    this.disabled = false;

    /**
     * The src for an optional icon.
     *
     * @type {string}
     */
    this.iconSrc = "";

    /**
     * Includes a badge of this type with matching text.
     *
     * @type {"beta" | "new" | undefined}
     */
    this.badge = undefined;
  }

  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("disabled")) {
      this.#updateChildDisabledState();
    }
    if (
      changedProperties.has("headingLevel") ||
      changedProperties.has("label")
    ) {
      this.toggleAttribute("hasheading", this.hasHeading);
    }
  }

  /**
   * Returns true when the fieldset should render its label as a heading element.
   *
   * @returns {boolean}
   */
  get hasHeading() {
    return !!this.label && !!HEADING_LEVEL_TEMPLATES[this.headingLevel];
  }

  #updateChildDisabledState() {
    const formControls = [...this.querySelectorAll("*")].filter(
      element => "disabled" in element || "parentDisabled" in element
    );

    formControls.forEach(control => {
      if ("parentDisabled" in control) {
        control.parentDisabled = this.disabled;
      }

      if (this.disabled) {
        control.setAttribute("disabled", "");
      } else {
        control.removeAttribute("disabled");
      }
    });
  }

  descriptionTemplate() {
    if (this.description) {
      return html`<div class="description">
        <span id="description">${this.description}</span>
        ${this.supportPageTemplate()}
      </div>`;
    }
    return "";
  }

  supportPageTemplate() {
    if (this.supportPage) {
      return html`<a
        is="moz-support-link"
        support-page=${this.supportPage}
        part="support-link"
      ></a>`;
    }
    return html`<slot name="support-link"></slot>`;
  }

  legendTemplate() {
    let label =
      HEADING_LEVEL_TEMPLATES[this.headingLevel]?.(this.label) || this.label;
    return html`<legend id="label" part="label">
      ${this.iconTemplate()}${label}${this.badgeTemplate()}
    </legend>`;
  }

  iconTemplate() {
    if (!this.iconSrc) {
      return "";
    }
    return html`<img
      src=${this.iconSrc}
      role="presentation"
      class=${classMap({
        icon: true,
        "heading-xlarge": this.headingLevel == 1,
        "heading-large": this.headingLevel == 2,
        "heading-medium": this.headingLevel == 3,
        "text-box-trim-start": this.headingLevel >= 1 && this.headingLevel <= 3,
      })}
    />`;
  }

  badgeTemplate() {
    if (!this.badge) {
      return "";
    }
    return html`<moz-badge type=${this.badge}></moz-badge>`;
  }

  render() {
    const hasInputsRole = Boolean(this.inputsRole);
    const descriptionId = this.description ? "description" : undefined;

    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-fieldset.css"
      />
      <fieldset
        ?disabled=${this.disabled}
        role=${ifDefined(hasInputsRole ? "presentation" : this.role)}
        aria-label=${ifDefined(!hasInputsRole ? this.ariaLabel : undefined)}
        aria-describedby=${ifDefined(
          !hasInputsRole ? descriptionId : undefined
        )}
        aria-orientation=${ifDefined(
          !hasInputsRole ? this.ariaOrientation : undefined
        )}
      >
        ${this.label ? this.legendTemplate() : ""}
        ${!this.description ? this.supportPageTemplate() : ""}
        ${this.descriptionTemplate()}
        <div
          id="inputs"
          part="inputs"
          role=${ifDefined(this.inputsRole)}
          aria-labelledby=${ifDefined(
            hasInputsRole && this.label ? "label" : undefined
          )}
          aria-label=${ifDefined(
            hasInputsRole && !this.label ? this.ariaLabel : undefined
          )}
          aria-describedby=${ifDefined(
            hasInputsRole ? descriptionId : undefined
          )}
          aria-orientation=${ifDefined(
            hasInputsRole ? this.ariaOrientation : undefined
          )}
          aria-disabled=${ifDefined(
            hasInputsRole && this.disabled ? "true" : undefined
          )}
        >
          <slot></slot>
        </div>
      </fieldset>
    `;
  }
}
customElements.define("moz-fieldset", MozFieldset);
