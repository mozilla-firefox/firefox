/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-message-bar.mjs";

const ERROR_L10N_ID = "backup-error-retry";

/**
 * The widget for disabling password protection if the backup is already
 * encrypted.
 */
export default class DisableBackupEncryption extends MozLitElement {
  static properties = {
    // managed by BackupUIChild
    disableEncryptionErrorCode: { type: Number },
  };

  static get queries() {
    return {
      cancelButtonEl: "#backup-disable-encryption-cancel-button",
      confirmButtonEl: "#backup-disable-encryption-confirm-button",
      errorEl: "#disable-backup-encryption-error",
    };
  }

  constructor() {
    super();
    this.disableEncryptionErrorCode = 0;
  }

  close() {
    this.dispatchEvent(
      new CustomEvent("dialogCancel", {
        bubbles: true,
        composed: true,
      })
    );
    this.reset();
  }

  reset() {
    this.disableEncryptionErrorCode = 0;
  }

  handleConfirm() {
    this.dispatchEvent(
      new CustomEvent("BackupUI:DisableEncryption", {
        bubbles: true,
      })
    );
  }

  errorTemplate() {
    return html`
      <moz-message-bar
        id="disable-backup-encryption-error"
        type="error"
        .messageL10nId=${ERROR_L10N_ID}
      ></moz-message-bar>
    `;
  }

  contentTemplate() {
    return html`
      <div
        id="backup-disable-encryption-wrapper"
        aria-labelledby="backup-disable-encryption-header"
        aria-describedby="backup-disable-encryption-description"
      >
        <h2
          id="backup-disable-encryption-header"
          class="heading-medium"
          data-l10n-id="disable-backup-encryption-header"
        ></h2>
        <main id="backup-disable-encryption-content">
          <div id="backup-disable-encryption-description">
            <span
              id="backup-disable-encryption-description-span"
              data-l10n-id="disable-backup-encryption-description2"
            >
            </span>
            <a
              id="backup-disable-encryption-learn-more-link"
              is="moz-support-link"
              support-page="firefox-backup"
              data-l10n-id="disable-backup-encryption-support-link"
              utm-content="remove-password"
            ></a>
          </div>
          ${this.disableEncryptionErrorCode ? this.errorTemplate() : null}
        </main>

        <moz-button-group id="backup-disable-encryption-button-group">
          <moz-button
            id="backup-disable-encryption-cancel-button"
            @click=${this.close}
            data-l10n-id="disable-backup-encryption-cancel-button"
          ></moz-button>
          <moz-button
            id="backup-disable-encryption-confirm-button"
            @click=${this.handleConfirm}
            type="primary"
            data-l10n-id="disable-backup-encryption-confirm-button"
          ></moz-button>
        </moz-button-group>
      </div>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/backup/disable-backup-encryption.css"
      />
      ${this.contentTemplate()}
    `;
  }
}

customElements.define("disable-backup-encryption", DisableBackupEncryption);
