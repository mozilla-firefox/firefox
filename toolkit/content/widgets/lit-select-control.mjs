/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "./vendor/lit.all.mjs";
import { MozLitElement } from "./lit-utils.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-fieldset.mjs";

const NAVIGATION_FORWARD = "forward";
const NAVIGATION_BACKWARD = "backward";

const NAVIGATION_VALUE = {
  [NAVIGATION_FORWARD]: 1,
  [NAVIGATION_BACKWARD]: -1,
};

const DIRECTION_RIGHT = "Right";
const DIRECTION_LEFT = "Left";

const NAVIGATION_DIRECTIONS = {
  LTR: {
    FORWARD: DIRECTION_RIGHT,
    BACKWARD: DIRECTION_LEFT,
  },
  RTL: {
    FORWARD: DIRECTION_LEFT,
    BACKWARD: DIRECTION_RIGHT,
  },
};

/**
 * Class that can be extended to handle managing the selected and focus states
 * of child elements using a roving tabindex. For more information on this focus
 * management pattern, see:
 * https://www.w3.org/WAI/ARIA/apg/practices/keyboard-interface/#kbd_roving_tabindex
 *
 * Child elements must use SelectControlItemMixin for behavior to work as
 * expected.
 */
export class SelectControlBaseElement extends MozLitElement {
  static formAssociated = true;
  #childElements;
  #value;
  #checkedIndex;
  #focusedIndex;
  #internals;

  static properties = {
    type: { type: String },
    disabled: { type: Boolean, reflect: true },
    description: { type: String, fluent: true },
    supportPage: { type: String, attribute: "support-page" },
    label: { type: String, fluent: true },
    // It looks like some interaction between fluent and mapped: true doesn't
    // work well in this context of nested components and doesn't trigger a
    // rerender when fluent sets the property. Therefore we don't use "mapped: true".
    // This means that a specified aria-label attribute won't be removed. This is fine
    // because this component has a "generic" role, where aria-label doesn't apply.
    ariaLabel: { type: String, fluent: true, attribute: "aria-label" },
    name: { type: String },
    value: { type: String },
    headingLevel: { type: Number },
  };

  static queries = {
    fieldset: "moz-fieldset",
  };

  set value(newValue) {
    this.#value = newValue;
    this.#internals.setFormValue(newValue);
    this.childElements.forEach((item, index) => {
      let isChecked = this.value === item.value;
      item.checked = isChecked;
      if (isChecked && !item.disabled) {
        this.#checkedIndex = index;
      }
    });
    this.syncFocusState();
  }

  get value() {
    return this.#value;
  }

  get hasValue() {
    return this.value === 0 || this.value === false || !!this.value;
  }

  set focusedIndex(newIndex) {
    if (this.#focusedIndex !== newIndex) {
      this.#focusedIndex = newIndex;
      this.syncFocusState();
    }
  }

  get checkedIndex() {
    return this.#checkedIndex;
  }

  set checkedIndex(newIndex) {
    if (this.#checkedIndex !== newIndex) {
      this.#checkedIndex = newIndex;
      this.syncFocusState();
    }
  }

  focus() {
    this.childElements[this.focusableIndex]?.focus();
    this.#focusedIndex = undefined;
  }

  get focusableIndex() {
    let activeEl = this.getRootNode().activeElement;
    let childElFocused =
      activeEl?.localName == this.constructor.childElementName;

    if (
      this.#checkedIndex != undefined &&
      this.hasValue &&
      (this.type == "radio" ||
        !childElFocused ||
        this.#focusedIndex == undefined)
    ) {
      return this.#checkedIndex;
    }

    if (
      this.#focusedIndex != undefined &&
      this.type === "listbox" &&
      childElFocused
    ) {
      return this.#focusedIndex;
    }

    return this.childElements.findIndex(item => !item.isDisabled);
  }

  // Query for child elements the first time they are needed + ensure they
  // have been upgraded so we can access properties.
  get childElements() {
    if (!this.#childElements) {
      this.#childElements = (
        this.shadowRoot
          ?.querySelector("slot:not([name])")
          ?.assignedElements() || [...this.children]
      )?.filter(
        el => el.localName === this.constructor.childElementName && !el.slot
      );
      this.#childElements.forEach(item => customElements.upgrade(item));
    }
    return this.#childElements;
  }
  get form() {
    return this.#internals.form;
  }

  formResetCallback() {
    this.value = this.getAttribute("value");
  }

  constructor() {
    super();
    this.type = "radio";
    this.disabled = false;
    this.#internals = this.attachInternals();
    this.addEventListener("blur", e => this.handleBlur(e), true);
    this.addEventListener("keydown", e => this.handleKeydown(e));
  }

  firstUpdated() {
    this.syncStateToChildElements();
  }

  async getUpdateComplete() {
    await super.getUpdateComplete();
    await Promise.all(this.childElements.map(item => item.updateComplete));
  }

  syncStateToChildElements() {
    this.childElements.forEach((item, index) => {
      item.position = index;

      if (item.checked && this.value == undefined) {
        this.value = item.value;
      }

      if (this.value == item.value && !item.disabled) {
        this.#checkedIndex = item.position;
      }

      item.name = this.name;
    });
    this.syncFocusState();
  }

  syncFocusState() {
    let focusableIndex = this.focusableIndex;
    this.childElements.forEach((item, index) => {
      item.itemTabIndex = focusableIndex === index ? 0 : -1;
    });
  }

  handleBlur(event) {
    if (this.contains(event.relatedTarget)) {
      return;
    }
    this.focusedIndex = undefined;
  }

  /**
   * @param {KeyboardEvent & { target: HTMLElement }} event
   */
  handleKeydown(event) {
    if (event.target.parentElement != this) {
      // Ignore events from nested controls.
      return;
    }
    let directions = this.getNavigationDirections();
    switch (event.key) {
      case "Down":
      case "ArrowDown":
      case directions.FORWARD:
      case `Arrow${directions.FORWARD}`: {
        event.preventDefault();
        this.navigate(NAVIGATION_FORWARD);
        break;
      }
      case "Up":
      case "ArrowUp":
      case directions.BACKWARD:
      case `Arrow${directions.BACKWARD}`: {
        event.preventDefault();
        this.navigate(NAVIGATION_BACKWARD);
        break;
      }
    }
  }

  getNavigationDirections() {
    if (this.isDocumentRTL) {
      return NAVIGATION_DIRECTIONS.RTL;
    }
    return NAVIGATION_DIRECTIONS.LTR;
  }

  get isDocumentRTL() {
    if (typeof Services !== "undefined") {
      return Services.locale.isAppLocaleRTL;
    }
    return document.dir === "rtl";
  }

  navigate(direction) {
    let currentIndex = this.focusableIndex;
    let children = this.childElements;
    let step = NAVIGATION_VALUE[direction];
    let isRadio = this.type == "radio";

    for (let i = 1; i < children.length; i++) {
      // Support focus wrapping for type="radio" only.
      let nextIndex = isRadio
        ? (currentIndex + children.length + step * i) % children.length
        : currentIndex + step * i;

      let nextItem = children[nextIndex];

      if (nextItem && !nextItem.isDisabled) {
        nextItem.focus();
        if (isRadio) {
          this.value = nextItem.value;
          nextItem.click();
        }
        return;
      }
    }
  }

  willUpdate(changedProperties) {
    if (changedProperties.has("name")) {
      this.handleSetName();
    }
    if (changedProperties.has("disabled")) {
      this.childElements.forEach(item => {
        item.parentDisabled = this.disabled;
        item.requestUpdate();
      });
    }
    if (changedProperties.has("type")) {
      let childRole = this.type == "radio" ? "radio" : "option";
      this.childElements.forEach(item => {
        item.role = childRole;
      });
    }
    if (changedProperties.has("value")) {
      this.#internals.setFormValue(this.value);
    }
  }

  handleSetName() {
    this.childElements.forEach(item => {
      item.name = this.name;
    });
  }

  handleSlotChange() {
    this.#childElements = null;
    this.#focusedIndex = undefined;
    this.#checkedIndex = undefined;
    this.syncStateToChildElements();
  }

  render() {
    return html`
      <moz-fieldset
        part="fieldset"
        description=${ifDefined(this.description)}
        support-page=${ifDefined(this.supportPage)}
        ?disabled=${this.disabled}
        label=${ifDefined(this.label)}
        aria-label=${ifDefined(this.ariaLabel)}
        headinglevel=${this.headingLevel}
        exportparts="inputs, support-link"
        aria-orientation=${ifDefined(this.constructor.orientation)}
      >
        ${!this.supportPage
          ? html`<slot slot="support-link" name="support-link"></slot>`
          : ""}
        <slot
          @slotchange=${this.handleSlotChange}
          @change=${this.handleChange}
        ></slot>
      </moz-fieldset>
    `;
  }
}

/**
 * Class that can be extended by items nested in a subclass of
 * SelectControlBaseElement to handle selection, focus management, and keyboard
 * navigation. Implemented as a mixin to enable use with elements that inherit
 * from something other than MozLitElement.
 *
 * @param {LitElement} superClass
 * @returns LitElement
 */
export const SelectControlItemMixin = superClass =>
  class extends superClass {
    #controller;

    static properties = {
      name: { type: String },
      value: { type: String },
      disabled: { type: Boolean, reflect: true },
      parentDisabled: { type: Boolean, reflect: true },
      checked: { type: Boolean, reflect: true },
      itemTabIndex: { type: Number, state: true },
      role: { type: String, state: true },
      position: { type: Number, state: true },
    };

    get controller() {
      return this.#controller;
    }

    get isDisabled() {
      return this.disabled || this.parentDisabled;
    }

    constructor() {
      super();
      this.checked = false;
      this.parentDisabled = false;
      this.addEventListener("focus", () => {
        if (!this.isDisabled) {
          this.controller.focusedIndex = this.position;
        }
      });
    }

    connectedCallback() {
      super.connectedCallback();

      let hostElement = this.parentElement || this.getRootNode().host;
      if (!(hostElement instanceof SelectControlBaseElement)) {
        console.error(
          `${this.localName} should only be used in an element that extends SelectControlBaseElement.`
        );
      }

      this.#controller = hostElement;
      this.parentDisabled = this.#controller.disabled;
      this.role = this.#controller.type == "radio" ? "radio" : "option";
      if (this.#controller.hasValue) {
        this.checked = this.value === this.#controller.value;
      }
    }

    willUpdate(changedProperties) {
      super.willUpdate(changedProperties);
      // Handle setting checked directly via JS.
      if (
        changedProperties.has("checked") &&
        this.checked &&
        this.#controller.hasValue &&
        this.value !== this.#controller.value
      ) {
        this.#controller.value = this.value;
      }
      // Handle un-checking directly via JS. If the checked item is un-checked,
      // the value of the associated focus manager parent needs to be un-set.
      if (
        changedProperties.has("checked") &&
        !this.checked &&
        this.#controller.hasValue &&
        this.value === this.#controller.value
      ) {
        this.#controller.value = "";
      }

      if (
        changedProperties.has("disabled") ||
        changedProperties.has("parentDisabled")
      ) {
        // Update items via focus manager parent for proper keyboard nav behavior.
        if (this.checked || !this.#controller.hasValue) {
          if (this.controller.checkedIndex != this.position) {
            this.#controller.syncFocusState();
          } else if (this.isDisabled) {
            // If the newly disabled element was checked unset the checkedIndex
            // to recompute which element should be focusable.
            this.controller.checkedIndex = undefined;
          }
        }
      }
    }

    handleClick() {
      if (this.isDisabled || this.checked) {
        return;
      }

      this.#controller.value = this.value;
      if (this.getRootNode().activeElement?.localName == this.localName) {
        this.focus();
      }
    }

    // Re-dispatch change event so it propagates out of the element.
    handleChange(e) {
      this.dispatchEvent(new Event(e.type, e));
    }
  };
