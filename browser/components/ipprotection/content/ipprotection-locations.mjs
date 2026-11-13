/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { LINKS } from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-promo.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/ipprotection/locations-list.mjs";

/**
 * A custom element that wraps the locations content.
 */
export default class IPProtectionLocationsElement extends MozLitElement {
  static properties = {
    state: { type: Object, attribute: false },
  };

  constructor() {
    super();
    this.state = {};
  }

  createRenderRoot() {
    return this;
  }

  connectedCallback() {
    super.connectedCallback();
    this.dispatchEvent(new CustomEvent("IPProtection:Init", { bubbles: true }));
  }

  handlePromoButtonClick(event) {
    Glean.ipprotection.locationUpgradePromoClicked.record();
    event.target.documentGlobal.openWebLinkIn(LINKS.LOCATION_PROMO_URL, "tab");
  }

  promoTemplate() {
    if (this.state?.hasUpgraded || this.state?.upgradeNotAvailable) {
      return null;
    }
    return html`
      <moz-promo
        id="locations-subview-promo"
        data-l10n-id="ipprotection-locations-subview-promo"
        imagealignment="end"
        imagesrc="chrome://browser/content/ipprotection/assets/mozilla-vpn-promo.svg"
      >
        <moz-button
          slot="actions"
          type="primary"
          data-l10n-id="ipprotection-locations-subview-promo-button"
          @click=${this.handlePromoButtonClick}
        ></moz-button>
      </moz-promo>
    `;
  }

  render() {
    if (!this.state.location && !this.state.locationsList) {
      return null;
    }
    return html`<link
        rel="stylesheet"
        href="chrome://browser/content/ipprotection/locations-subview.css"
      />
      <locations-list
        .selectedLocation=${this.state.location}
        .locations=${this.state.locationsList}
        .premium=${this.state.isPremium}
      ></locations-list
      >${this.promoTemplate()}`;
  }
}

customElements.define("ipprotection-locations", IPProtectionLocationsElement);
