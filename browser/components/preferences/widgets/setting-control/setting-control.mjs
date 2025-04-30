/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

export class SettingControl extends MozLitElement {
  #lastSetting;

  static properties = {
    setting: { type: Object },
    config: { type: Object },
    value: {},
  };

  static queries = {
    inputEl: "#input",
  };

  createRenderRoot() {
    return this;
  }

  onSettingChange = () => {
    this.value = this.setting.value;
  };

  willUpdate(changedProperties) {
    if (changedProperties.has("setting")) {
      if (this.#lastSetting) {
        this.#lastSetting.off("change", this.onSettingChange);
      }
      this.#lastSetting = this.setting;
      this.value = this.getValue();
      this.setting.on("change", this.onSettingChange);
    }
  }

  getValue() {
    return this.setting.value;
  }

  onChange(e) {
    this.setting.userChange(e.target.checked);
    this.value = this.getValue();
  }

  render() {
    let { config } = this;
    switch (config.control) {
      case "checkbox":
      default:
        return html`<moz-checkbox
          id="input"
          data-l10n-id=${config.l10nId}
          .iconSrc=${config.iconSrc}
          .checked=${this.value}
          .supportPage=${this.config.supportPage}
          data-subcategory=${ifDefined(this.config.subcategory)}
          @change=${this.onChange}
        ></moz-checkbox>`;
    }
  }
}
customElements.define("setting-control", SettingControl);
