/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

/**
 * The widget for showing available options when users want to turn off
 * scheduled backups.
 */
export default class TurnOffScheduledBackups extends MozLitElement {
  static properties = {
    // Identifier of the message, surface, or code path that hosted this
    // widget. Forwarded to BackupService.setScheduledBackups so that
    // browser.backup.scheduler_toggle_source can attribute the disable.
    source: {
      type: String,
      reflect: true,
      attribute: "source",
    },
  };

  static get queries() {
    return {
      cancelButtonEl: "#backup-turn-off-scheduled-cancel-button",
      confirmButtonEl: "#backup-turn-off-scheduled-confirm-button",
    };
  }

  close() {
    this.dispatchEvent(
      new CustomEvent("dialogCancel", {
        bubbles: true,
        composed: true,
      })
    );
  }

  handleConfirm() {
    this.dispatchEvent(
      new CustomEvent("BackupUI:DisableScheduledBackups", {
        bubbles: true,
        detail: {
          source: this.source,
        },
      })
    );
  }

  contentTemplate() {
    return html`
      <div
        id="backup-turn-off-scheduled-wrapper"
        aria-labelledby="backup-turn-off-scheduled-header"
        aria-describedby="backup-turn-off-scheduled-description"
      >
        <h2
          id="backup-turn-off-scheduled-header"
          class="heading-medium"
          data-l10n-id="turn-off-scheduled-backups-header"
        ></h2>
        <main id="backup-turn-off-scheduled-content">
          <div id="backup-turn-off-scheduled-description">
            <span
              id="backup-turn-off-scheduled-description-span"
              data-l10n-id="turn-off-scheduled-backups-description"
            ></span>
            <a
              id="backup-turn-off-scheduled-learn-more-link"
              is="moz-support-link"
              support-page="firefox-backup"
              data-l10n-id="turn-off-scheduled-backups-support-link"
              utm-content="turn-off-backup"
            ></a>
          </div>
        </main>

        <moz-button-group id="backup-turn-off-scheduled-button-group">
          <moz-button
            id="backup-turn-off-scheduled-cancel-button"
            @click=${this.close}
            data-l10n-id="turn-off-scheduled-backups-cancel-button"
          ></moz-button>
          <moz-button
            id="backup-turn-off-scheduled-confirm-button"
            @click=${this.handleConfirm}
            type="primary"
            data-l10n-id="turn-off-scheduled-backups-confirm-button"
          ></moz-button>
        </moz-button-group>
      </div>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/backup/turn-off-scheduled-backups.css"
      />
      ${this.contentTemplate()}
    `;
  }
}

customElements.define("turn-off-scheduled-backups", TurnOffScheduledBackups);
