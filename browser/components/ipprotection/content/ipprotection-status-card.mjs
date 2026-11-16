/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html } from "chrome://global/content/vendor/lit.all.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-toggle.mjs";

/**
 * Custom element that implements a status card for IP protection.
 */
export default class IPProtectionStatusCard extends MozLitElement {
  TOGGLE_ON_EVENT = "ipprotection-status-card:user-toggled-on";
  TOGGLE_OFF_EVENT = "ipprotection-status-card:user-toggled-off";

  static queries = {
    statusBoxEl: "ipprotection-status-box",
    actionButtonEl: 'moz-button[slot="action"]',
  };

  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    protectionEnabled: { type: Boolean },
    enabledSince: { type: Object },
    hasExclusion: { type: Boolean },
    isActivating: { type: Boolean },
  };

  #actionButtonFocused = false;

  handleButtonClick() {
    this.#actionButtonFocused = true;
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

  updated(changedProperties) {
    super.updated(changedProperties);

    let stateChanged =
      changedProperties.has("protectionEnabled") ||
      changedProperties.has("isActivating") ||
      changedProperties.has("hasExclusion");
    if (!this.#actionButtonFocused || !stateChanged || this.isActivating) {
      return;
    }

    this.#actionButtonFocused = false;

    this.actionButtonEl.updateComplete.then(() => {
      this.focus();
    });
  }

  focus() {
    const button = this.shadowRoot.querySelector(`moz-button[slot="action"]`);
    if (!button?.disabled) {
      button?.focus();
    }
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
      <ipprotection-status-box .headerL10nId=${headerL10nId} .type=${type}>
        ${iconSrc
          ? html`<img
              slot="image"
              role="presentation"
              class="icon"
              src=${iconSrc}
            />`
          : null}
        <moz-button
          slot="action"
          type=${buttonType}
          data-l10n-id=${buttonL10nId}
          @click=${this.handleButtonClick}
          ?disabled=${buttonDisabled}
          closemenu="none"
        ></moz-button>
      </ipprotection-status-box>
    `;
  }

  render() {
    let type = "disconnected";
    let headerL10nId = "ipprotection-connection-status-disconnected-1";
    let buttonL10nId = "ipprotection-button-turn-vpn-on-1";
    let buttonDisabled = false;
    let iconSrc = null;

    if (this.isActivating) {
      type = "connecting";
      headerL10nId = "ipprotection-connection-status-connecting-1";
      buttonL10nId = "ipprotection-button-connecting";
      iconSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-loading.svg";
      buttonDisabled = true;
    } else if (this.hasExclusion && this.protectionEnabled) {
      type = "excluded";
      headerL10nId = "ipprotection-connection-status-excluded-1";
      buttonL10nId = "ipprotection-button-turn-vpn-off-excluded-site";
      iconSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-excluded.svg";
    } else if (this.protectionEnabled) {
      type = "connected";
      headerL10nId = "ipprotection-connection-status-connected-1";
      buttonL10nId = "ipprotection-button-turn-vpn-off-1";
      iconSrc =
        "chrome://browser/content/ipprotection/assets/states/ipprotection-on.svg";
    }

    return this.statusTemplate({
      type,
      headerL10nId,
      buttonL10nId,
      buttonType: "primary",
      buttonDisabled,
      iconSrc,
    });
  }
}

customElements.define("ipprotection-status-card", IPProtectionStatusCard);
