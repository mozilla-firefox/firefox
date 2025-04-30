/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

export class SettingGroup extends MozLitElement {
  static properties = {
    config: { type: Object },
    groupId: { type: String },
    // getSetting should be Preferences.getSetting from preferencesBindings.js
    getSetting: { type: Function },
  };

  createRenderRoot() {
    return this;
  }

  itemTemplate(item) {
    let setting = this.getSetting(item.id);
    if (!setting.visible) {
      return "";
    }
    return html`<setting-control
      .setting=${setting}
      .config=${item}
    ></setting-control>`;
  }

  xulCheckboxTemplate(item, setting) {
    let result;
    let checkbox = document.createXULElement("checkbox");
    checkbox.id = item.id;
    document.l10n.setAttributes(checkbox, item.l10nId);
    checkbox.addEventListener("command", e =>
      setting.userChange(e.target.checked)
    );
    setting.on("change", () => (checkbox.checked = setting.value));
    checkbox.checked = setting.value;
    if (item.supportPage) {
      let container = document.createXULElement("hbox");
      container.setAttribute("align", "center");
      let supportLink = document.createElement("a", { is: "moz-support-link" });
      supportLink.supportPage = item.supportPage;
      checkbox.classList.add("tail-with-learn-more");
      container.append(checkbox, supportLink);
      result = container;
    } else {
      result = checkbox;
    }
    if (item.subcategory) {
      result.dataset.subcategory = item.subcategory;
    }
    return result;
  }

  xulItemTemplate(item) {
    let setting = this.getSetting(item.id);
    if (!setting.visible) {
      return "";
    }
    switch (item.control) {
      case "checkbox":
      default:
        return this.xulCheckboxTemplate(item, setting);
    }
  }

  render() {
    if (!this.config) {
      return "";
    }
    if (
      window.IS_STORYBOOK ||
      Services.prefs.getBoolPref("settings.revamp.design", false)
    ) {
      return html`<moz-fieldset data-l10n-id=${ifDefined(this.config.l10nId)}
        >${this.config.items.map(item => this.itemTemplate(item))}</moz-fieldset
      >`;
    }
    return this.config.items.map(item => this.xulItemTemplate(item));
  }
}
customElements.define("setting-group", SettingGroup);
