/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./devtools-settings-pref.tagmap.d.ts" />

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { MozCheckbox } from "chrome://global/content/elements/moz-checkbox.mjs";
import { MozSelect } from "chrome://global/content/elements/moz-select.mjs";
import { MozSegmentedControl } from "chrome://global/content/elements/moz-segmented-control.mjs";

import { SettingChangedEvent } from "../events.mjs";

/** @import { PropertyDeclarations, PropertyValues } from "chrome://global/content/vendor/lit.all.mjs" */

/** Binds the controls it wraps to a pref. */
export class DevToolsSettingsPref extends MozLitElement {
  /** @satisfies {PropertyDeclarations} */
  static properties = {
    pref: { type: String },
    changeAction: { type: String, attribute: "change-action" },
    _errorMessage: { state: true },
  };

  constructor() {
    super();
    this.pref = "";
    /** @type {string | null} */
    this.changeAction = null;
    this._errorMessage = "";
  }

  /** @param {PropertyValues<this>} changedProperties */
  willUpdate(changedProperties) {
    if (!changedProperties.has("pref")) {
      return;
    }
    try {
      const control = this.#getControl();
      if (control instanceof MozCheckbox) {
        control.checked = Services.prefs.getBoolPref(this.pref);
      } else if (
        control instanceof MozSelect ||
        control instanceof MozSegmentedControl
      ) {
        if (this.#isIntPref()) {
          control.value = String(Services.prefs.getIntPref(this.pref));
        } else {
          control.value = Services.prefs.getStringPref(this.pref);
        }
      } else {
        return;
      }
      this._errorMessage = "";
    } catch (error) {
      this._errorMessage = `Unable to render setting for "${this.pref}".`;
      console.error(this._errorMessage, error);
    }
  }

  #getControl() {
    return this.querySelector(
      "moz-checkbox, moz-select, moz-segmented-control"
    );
  }

  #controlChange = () => {
    const control = this.#getControl();
    if (control instanceof MozCheckbox) {
      Services.prefs.setBoolPref(this.pref, control.checked);
    } else if (
      control instanceof MozSelect ||
      control instanceof MozSegmentedControl
    ) {
      if (this.#isIntPref()) {
        Services.prefs.setIntPref(this.pref, Number(control.value));
      } else {
        Services.prefs.setStringPref(this.pref, control.value);
      }
    } else {
      return;
    }
    if (this.changeAction) {
      this.dispatchEvent(
        new SettingChangedEvent({ action: this.changeAction, pref: this.pref })
      );
    }
  };

  #isIntPref() {
    return Services.prefs.getPrefType(this.pref) === Services.prefs.PREF_INT;
  }

  render() {
    if (this._errorMessage) {
      return html`<p role="alert">${this._errorMessage}</p>`;
    }
    return html`<slot @change=${this.#controlChange}></slot>`;
  }
}

customElements.define("devtools-settings-pref", DevToolsSettingsPref);
