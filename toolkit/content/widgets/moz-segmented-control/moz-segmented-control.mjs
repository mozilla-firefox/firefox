/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-segmented-control.tagmap.d.ts" />

import { html } from "../vendor/lit.all.mjs";
import {
  SelectControlBaseElement,
  SelectControlItemMixin,
} from "../lit-select-control.mjs";
import { MozButton } from "chrome://global/content/elements/moz-button.mjs";

/**
 * A segmented control component that can function as either a tab switcher or
 * radio button group.
 *
 * @tagname moz-segmented-control
 */
export class MozSegmentedControl extends SelectControlBaseElement {
  #deckElement;

  static childElementName = "moz-segmented-control-item";

  static properties = {
    deck: { type: String },
  };

  constructor() {
    super();

    /**
     * ID of a named-deck to control for tab switching.
     *
     * @type {string | undefined}
     */
    this.deck = undefined;

    this.type = "radio";
  }

  connectedCallback() {
    super.connectedCallback();
    this.setupDeckListener();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeDeckListener();
  }

  getChildRole() {
    return this.deck ? "tab" : super.getChildRole();
  }

  getGroupRole() {
    return this.deck ? "tablist" : super.getGroupRole();
  }

  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);
    if (changedProperties.has("deck")) {
      this.removeDeckListener();
      this.setupDeckListener();
      this.updateChildRoles();
    }
  }

  setupDeckListener() {
    if (!this.deck) {
      return;
    }

    this.#deckElement = this.getRootNode().querySelector(`#${this.deck}`);
    if (this.#deckElement) {
      this.#deckElement.addEventListener("view-changed", this);
    }
  }

  removeDeckListener() {
    if (this.#deckElement) {
      this.#deckElement.removeEventListener("view-changed", this);
      this.#deckElement = null;
    }
  }

  handleEvent(event) {
    if (event.type === "view-changed" && this.#deckElement) {
      this.value = this.#deckElement.selectedViewName;
    }
  }

  handleChange(e) {
    super.handleChange?.(e);

    // If a deck is specified, automatically switch views
    if (this.#deckElement && this.value) {
      this.#deckElement.selectedViewName = this.value;
    }
  }

  render() {
    return html` <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-segmented-control.css"
      />${super.render()}`;
  }
}
customElements.define("moz-segmented-control", MozSegmentedControl);

/**
 * An item in a segmented control. Renders as a button that can be selected.
 *
 * @tagname moz-segmented-control-item
 */
export class MozSegmentedControlItem extends SelectControlItemMixin(MozButton) {
  constructor() {
    super();
    this.size = "default";
    this.type = "ghost";
    this.addEventListener("click", this.handleClick.bind(this));
  }

  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);
    if (changedProperties.has("checked")) {
      this.type = this.checked ? "primary" : "ghost";
      this.ariaChecked = this.checked ? "true" : "false";
    }
    if (changedProperties.has("itemTabIndex")) {
      this.setAttribute("tabindex", this.itemTabIndex);
    }
  }

  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("role") && this.buttonEl) {
      this.buttonEl.setAttribute("role", this.role);
    }
  }

  handleClick(event) {
    event.stopPropagation();
    if (this.isDisabled) {
      return;
    }
    this.focus();
    if (this.checked) {
      return;
    }
    super.handleClick();

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
}
customElements.define("moz-segmented-control-item", MozSegmentedControlItem);
