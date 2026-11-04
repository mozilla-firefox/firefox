/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";

/**
 * A custom element that manages the display and editing of a Device name
 * in Firefox Sync settings section.
 *
 * @tagname sync-device-name
 * @property {string} value - The current value of the device name.
 * @property {string} defaultValue - Default device name shown in the input field when empty.
 * @property {boolean} disabled - The disabled state of the device name component.
 * @property {boolean} _isInEditMode - Whether the component is currently in edit mode.
 */
class SyncDeviceName extends MozLitElement {
  static properties = {
    value: { type: String },
    defaultValue: { type: String },
    disabled: { type: Boolean },
    _isInEditMode: { type: Boolean, state: true },
  };

  static queries = {
    inputTextEl: "#fxaSyncComputerName",
    changeBtnEl: "#fxaChangeDeviceName",
  };

  constructor() {
    super();

    /** @type {string} */
    this.value = "";

    /** @type {string} */
    this.defaultValue = "";

    /** @type {boolean} */
    this.disabled = false;

    /** @type {boolean} */
    this._isInEditMode = false;
  }

  setFocus() {
    this.updateComplete.then(() => {
      const targetEl = this._isInEditMode ? this.inputTextEl : this.changeBtnEl;
      targetEl?.focus();
    });
  }

  onDeviceNameChange() {
    this._isInEditMode = true;
    this.setFocus();
  }

  onDeviceNameCancel() {
    this._isInEditMode = false;
    this.setFocus();
  }

  onDeviceNameSave() {
    const inputVal = this.inputTextEl.value?.trim();
    this.value = inputVal === "" ? this.defaultValue : inputVal;
    this._isInEditMode = false;
    this.setFocus();

    this.dispatchEvent(new Event("change", { bubbles: true }));
  }

  /**
   * Handles key presses in the device name input.
   * Pressing Enter saves the name, pressing Escape cancels editing.
   *
   * @param {KeyboardEvent} event
   */
  onDeviceNameKeyDown(event) {
    switch (event.key) {
      case "Enter":
        event.preventDefault();
        this.onDeviceNameSave();
        break;
      case "Escape":
        event.preventDefault();
        this.onDeviceNameCancel();
        break;
    }
  }

  displayDeviceNameTemplate() {
    return html`<moz-button
      id="fxaChangeDeviceName"
      data-l10n-id="sync-device-name-change-2"
      data-l10n-attrs="accesskey"
      slot="actions"
      @click=${this.onDeviceNameChange}
      ?disabled=${this.disabled}
    ></moz-button>`;
  }

  editDeviceNameTemplate() {
    return html`<moz-input-text
        id="fxaSyncComputerName"
        data-l10n-id="sync-device-name-input"
        data-l10n-args=${JSON.stringify({ placeholder: this.defaultValue })}
        .value=${this.value}
        @keydown=${this.onDeviceNameKeyDown}
      ></moz-input-text>
      <moz-button
        id="fxaCancelChangeDeviceName"
        data-l10n-id="sync-device-name-cancel"
        data-l10n-attrs="accesskey"
        slot="actions"
        @click=${this.onDeviceNameCancel}
      ></moz-button>
      <moz-button
        id="fxaSaveChangeDeviceName"
        data-l10n-id="sync-device-name-save"
        data-l10n-attrs="accesskey"
        slot="actions"
        @click=${this.onDeviceNameSave}
      ></moz-button>`;
  }

  render() {
    let label = "";
    if (!this._isInEditMode) {
      label = this.value == "" ? this.defaultValue : this.value;
    }
    return html`
      <moz-box-item label=${label}>
        ${this._isInEditMode
          ? this.editDeviceNameTemplate()
          : this.displayDeviceNameTemplate()}
      </moz-box-item>
    `;
  }
}
customElements.define("sync-device-name", SyncDeviceName);
