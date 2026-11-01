/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "./aboutaddons-themes-picker.mjs";

/**
 * Groups the Nova and Classic aboutaddons-themes-picker instances shown in
 * the about:addons themes list view. It renders in light DOM and does not
 * participate in the page layout (see `display: contents` in aboutaddons.css).
 */
export class AboutaddonsThemesPickers extends MozLitElement {
  createRenderRoot() {
    return this;
  }

  render() {
    return html`
      <aboutaddons-themes-picker></aboutaddons-themes-picker>
      <aboutaddons-themes-picker
        themes-collection-name="classic"
        heading-l10n-id="aboutaddons-classic-themes-heading"
      ></aboutaddons-themes-picker>
    `;
  }
}
customElements.define("aboutaddons-themes-pickers", AboutaddonsThemesPickers);
