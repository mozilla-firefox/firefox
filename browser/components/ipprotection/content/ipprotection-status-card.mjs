/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { countryName } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-toggle.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/ipprotection/bandwidth-usage.mjs";

/**
 * Custom element that implements a status card for IP protection.
 */
export default class IPProtectionStatusCard extends MozLitElement {
  TOGGLE_ON_EVENT = "ipprotection-status-card:user-toggled-on";
  TOGGLE_OFF_EVENT = "ipprotection-status-card:user-toggled-off";

  static queries = {
    statusBoxEl: "ipprotection-status-box",
    actionButtonEl: 'moz-button[slot="action"]',
    locationButtonEl: 'moz-button[slot="location-action"]',
  };

  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    protectionEnabled: { type: Boolean },
    enabledSince: { type: Object },
    location: { type: Object },
    bandwidthUsage: { type: Object },
    hasExclusion: { type: Boolean },
    isActivating: { type: Boolean },
    showLocationButtonBadge: { type: Boolean },
  };

  handleButtonClick() {
    const type = this.protectionEnabled
      ? this.TOGGLE_OFF_EVENT
      : this.TOGGLE_ON_EVENT;
    this.dispatchEvent(
      new CustomEvent(type, {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleLocationButtonClick(e) {
    this.dispatchEvent(
      new CustomEvent("IPProtection:UserShowLocations", {
        bubbles: true,
        composed: true,
        detail: {
          keyboardActivated: e.detail === 0,
          locationButton: this.locationButtonEl,
        },
      })
    );
  }

  focus() {
    const button = this.shadowRoot.querySelector(`moz-button[slot="action"]`);
    button?.focus();
  }

  bandwidthUsageTemplate() {
    return this.bandwidthUsage
      ? html`<bandwidth-usage
          slot="bandwidth"
          remaining=${this.bandwidthUsage.remaining}
          max=${this.bandwidthUsage.max}
          numeric
        ></bandwidth-usage>`
      : null;
  }

  locationSelectionButtonTemplate() {
    const country =
      this.location && this.location !== "REC"
        ? countryName(this.location)
        : null;

    return html`
      <moz-button
        class="toolbarbutton"
        slot="location-action"
        closemenu="none"
        @click=${this.handleLocationButtonClick}
      >
        <span class="location-btn-content">
          ${this.showLocationButtonBadge
            ? html`<moz-badge type="new"></moz-badge>`
            : null}
          ${country
            ? html`<span
                data-l10n-id="ipprotection-location-country-button"
                data-l10n-args=${JSON.stringify({ country })}
              ></span>`
            : html`<span
                data-l10n-id="ipprotection-recommended-location-button"
              ></span>`}
          <img
            class="arrow-icon"
            src="chrome://global/skin/icons/arrow-right.svg"
            role="presentation"
          />
        </span>
      </moz-button>
    `;
  }

  statusTemplate({
    type,
    headerL10nId,
    buttonL10nId,
    buttonType = "default",
    buttonDisabled = false,
    iconSrc = null,
  }) {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/ipprotection/ipprotection-status-card.css"
      />
      <ipprotection-status-box .headerL10nId=${headerL10nId} .type=${type}>
        ${iconSrc
          ? html`<img
              slot="image"
              role="presentation"
              class="icon"
              src=${iconSrc}
            />`
          : null}
        ${this.bandwidthUsageTemplate()}
        <moz-button
          slot="action"
          type=${buttonType}
          data-l10n-id=${buttonL10nId}
          @click=${this.handleButtonClick}
          ?disabled=${buttonDisabled}
          closemenu="none"
        ></moz-button>

        ${this.locationSelectionButtonTemplate()}
        ${!this.location || this.location === "REC"
          ? html`<div
              slot="content"
              class="location-message"
              data-l10n-id="ipprotection-recommended-location-description"
            ></div>`
          : null}
      </ipprotection-status-box>
    `;
  }

  render() {
    if (this.isActivating) {
      return html`
        ${this.statusTemplate({
          type: "connecting",
          headerL10nId: "ipprotection-connection-status-connecting",
          buttonL10nId: "ipprotection-button-connecting",
          iconSrc:
            "chrome://browser/content/ipprotection/assets/states/ipprotection-loading.svg",
          buttonType: "primary",
          buttonDisabled: true,
        })}
      `;
    }

    if (this.hasExclusion && this.protectionEnabled) {
      return html`
        ${this.statusTemplate({
          type: "excluded",
          headerL10nId: "ipprotection-connection-status-excluded",
          buttonL10nId: "ipprotection-button-turn-vpn-off-excluded-site",
          buttonType: "primary",
          iconSrc:
            "chrome://browser/content/ipprotection/assets/states/ipprotection-excluded.svg",
        })}
      `;
    }

    if (this.protectionEnabled) {
      return html`
        ${this.statusTemplate({
          type: "connected",
          headerL10nId: "ipprotection-connection-status-connected",
          buttonL10nId: "ipprotection-button-turn-vpn-off",
          buttonType: "primary",
          iconSrc:
            "chrome://browser/content/ipprotection/assets/states/ipprotection-on.svg",
        })}
      `;
    }

    return html`
      ${this.statusTemplate({
        type: "disconnected",
        headerL10nId: "ipprotection-connection-status-disconnected",
        buttonL10nId: "ipprotection-button-turn-vpn-on",
        buttonType: "primary",
      })}
    `;
  }
}

customElements.define("ipprotection-status-card", IPProtectionStatusCard);
