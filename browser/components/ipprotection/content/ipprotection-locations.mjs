/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";
import {
  ADDITIONAL_LOCATIONS_AVAILABLE,
  LINKS,
} from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";

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

  #isViewShown = false;
  #seenPromos = new Set();

  constructor() {
    super();
    this.state = {};
  }

  createRenderRoot() {
    return this;
  }

  connectedCallback() {
    super.connectedCallback();
    this.panelView = this.closest("panelview");
    this.panelView?.addEventListener("ViewShown", this);
    this.panelView?.addEventListener("ViewHiding", this);
    this.dispatchEvent(new CustomEvent("IPProtection:Init", { bubbles: true }));
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.panelView?.removeEventListener("ViewShown", this);
    this.panelView?.removeEventListener("ViewHiding", this);
    this.panelView = null;
  }

  handleEvent(event) {
    if (event.type == "ViewShown") {
      this.#isViewShown = true;
      this.#maybeRecordPromoSeen();
    } else if (event.type == "ViewHiding") {
      this.#isViewShown = false;
      this.#seenPromos.clear();
    }
  }

  updated(changedProperties) {
    super.updated(changedProperties);
    this.#maybeRecordPromoSeen();
  }

  #maybeRecordPromoSeen() {
    let promo = this.#visiblePromo();
    if (!this.#isViewShown || !promo || this.#seenPromos.has(promo)) {
      return;
    }
    this.#seenPromos.add(promo);
    Glean.ipprotection.promoSeen.record({ promo, entrypoint: "panel" });
  }

  /**
   * @returns {"defaultbrowser"|"mozillavpn"|null}
   *   The promo currently rendered in the subview, if any.
   */
  #visiblePromo() {
    if (!this.state.location && !this.state.locationsList) {
      return null;
    }
    // If the user has a Mozilla VPN subscription, show nothing
    if (this.state?.hasUpgraded) {
      return null;
    }
    // If the user does not have Firefox as default and is allowed to set it, show default promo
    if (!this.state?.isDefaultBrowser && this.state?.canSetDefaultBrowser) {
      return "defaultbrowser";
    }
    // If the user is not eligible for Mozilla VPN, show nothing
    if (this.state?.upgradeNotAvailable) {
      return null;
    }
    return "mozillavpn";
  }

  handlePromoButtonClick(event) {
    Glean.ipprotection.promoClicked.record({
      promo: "mozillavpn",
      entrypoint: "panel",
    });
    event.target.documentGlobal.openWebLinkIn(LINKS.LOCATION_PROMO_URL, "tab");
  }

  handleSetDefaultPromoButtonClick() {
    Glean.ipprotection.promoClicked.record({
      promo: "defaultbrowser",
      entrypoint: "panel",
    });
    this.dispatchEvent(
      new CustomEvent("IPProtection:SetDefaultBrowser", { bubbles: true })
    );
  }

  setDefaultPromoTemplate() {
    return html`
      <moz-promo
        id="locations-subview-set-default-promo"
        data-l10n-id="ipprotection-locations-subview-set-default-promo"
        data-l10n-args=${JSON.stringify({
          count: ADDITIONAL_LOCATIONS_AVAILABLE,
        })}
        imagealignment="end"
        imagesrc="chrome://browser/content/ipprotection/assets/location-pin.svg"
        type="vibrant"
      >
        <moz-button
          slot="actions"
          type="primary"
          data-l10n-id="ipprotection-locations-subview-set-default-promo-button"
          @click=${this.handleSetDefaultPromoButtonClick}
        ></moz-button>
      </moz-promo>
    `;
  }

  promoTemplate() {
    let promo = this.#visiblePromo();
    if (promo == "defaultbrowser") {
      return this.setDefaultPromoTemplate();
    }
    if (promo != "mozillavpn") {
      return null;
    }

    return html`
      <moz-promo
        id="locations-subview-promo"
        data-l10n-id="ipprotection-locations-subview-promo"
        imagealignment="end"
        imagesrc="chrome://browser/content/ipprotection/assets/mozilla-vpn-promo.svg"
        type="vibrant"
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
