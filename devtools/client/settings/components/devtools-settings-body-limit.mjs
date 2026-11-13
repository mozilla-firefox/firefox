/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./devtools-settings-body-limit.tagmap.d.ts" />

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { MozButton } from "chrome://global/content/elements/moz-button.mjs";

/**
 * @import { PropertyDeclarations } from "chrome://global/content/vendor/lit.all.mjs"
 */

// eslint-disable-next-line no-shadow
const { require } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/Loader.sys.mjs"
);
/** @type {typeof import("../../netmonitor/src/utils/format-utils.js")} */
const {
  getFormattedSize,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");

const BODY_LIMIT_PREF = "devtools.netmonitor.bodyLimit";

// Integer prefs are capped at PR_INT32_MAX.
const MAX_INT_PREF_VALUE = 2147483647;

/** Shows the network body size limit, with a form to edit it. */
export class DevToolsSettingsBodyLimit extends MozLitElement {
  /** @satisfies {PropertyDeclarations} */
  static properties = {
    _editing: { type: Boolean, state: true },
    _value: { type: Number, state: true },
    _hasUserValue: { type: Boolean, state: true },
  };

  constructor() {
    super();
    this._editing = false;
    this._value = 0;
    this._hasUserValue = false;
  }

  connectedCallback() {
    super.connectedCallback();
    Services.prefs.addObserver(BODY_LIMIT_PREF, this.#readPref);
    this.#readPref();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    Services.prefs.removeObserver(BODY_LIMIT_PREF, this.#readPref);
  }

  #readPref = () => {
    this._value = Services.prefs.getIntPref(BODY_LIMIT_PREF);
    this._hasUserValue = Services.prefs.prefHasUserValue(BODY_LIMIT_PREF);
  };

  #edit = async () => {
    this._editing = true;
    await this.updateComplete;
    const input = this.renderRoot.querySelector("input");
    if (!input) {
      return;
    }
    input.value = String(this._value);
    input.select();
  };

  async #focusEdit() {
    await this.updateComplete;
    const edit = this.renderRoot.querySelector("#edit");
    if (edit instanceof MozButton) {
      edit.focus();
    }
  }

  /** @param {Event} event */
  #inputInput = event => {
    const input = event.currentTarget;
    if (!HTMLInputElement.isInstance(input)) {
      return;
    }
    const tooLarge = Number(input.value) > MAX_INT_PREF_VALUE;
    input.setCustomValidity(tooLarge ? "invalid" : "");
  };

  /** @param {KeyboardEvent} event */
  #inputKeydown = event => {
    if (event.key == "Escape") {
      // Stop the toolbox from toggling the split console.
      event.preventDefault();
      event.stopPropagation();
      this._editing = false;
      this.#focusEdit();
    } else if (event.key == "Enter") {
      this.#save();
    }
  };

  #save = () => {
    const input = this.renderRoot.querySelector("input");
    if (
      input &&
      !input.validity.patternMismatch &&
      !input.validity.valueMissing
    ) {
      const value = Math.min(Number(input.value), MAX_INT_PREF_VALUE);
      Services.prefs.setIntPref(BODY_LIMIT_PREF, value);
    }
    this._editing = false;
    this.#focusEdit();
  };

  #reset = () => {
    Services.prefs.clearUserPref(BODY_LIMIT_PREF);
    this.#focusEdit();
  };

  render() {
    return html`
      <link
        rel="stylesheet"
        href="resource://devtools/client/settings/components/devtools-settings-body-limit.css"
      />
      <label
        for="input"
        data-l10n-id="devtools-settings-netmonitor-body-limit-tooltip"
      >
        <span
          id="text"
          data-l10n-id="devtools-settings-netmonitor-body-limit-label"
        ></span>
        ${this._editing ? nothing : this.#renderValue()}
      </label>
      ${this._editing ? this.#renderEdit() : nothing}
    `;
  }

  #renderValue() {
    return html`
      ${this._value === 0
        ? html`<span
            id="value"
            data-l10n-id="devtools-settings-netmonitor-body-limit-unlimited"
          ></span>`
        : html`<span id="value">${getFormattedSize(this._value)}</span>`}
      <moz-button
        id="edit"
        type="icon"
        iconsrc="chrome://global/skin/icons/edit.svg"
        data-l10n-id="devtools-settings-netmonitor-body-limit-edit"
        @click=${this.#edit}
      ></moz-button>
      ${this._hasUserValue
        ? html`<moz-button
            id="reset"
            type="icon"
            iconsrc="chrome://global/skin/icons/undo.svg"
            data-l10n-id="devtools-settings-netmonitor-body-limit-reset"
            @click=${this.#reset}
          ></moz-button>`
        : nothing}
    `;
  }

  #renderEdit() {
    return html`
      <input
        id="input"
        type="text"
        pattern="\\d+"
        required
        @input=${this.#inputInput}
        @keydown=${this.#inputKeydown}
      />
      <moz-button
        id="save"
        type="icon"
        iconsrc="chrome://global/skin/icons/check.svg"
        data-l10n-id="devtools-settings-netmonitor-body-limit-save"
        @click=${this.#save}
      ></moz-button>
    `;
  }
}

customElements.define(
  "devtools-settings-body-limit",
  DevToolsSettingsBodyLimit
);
