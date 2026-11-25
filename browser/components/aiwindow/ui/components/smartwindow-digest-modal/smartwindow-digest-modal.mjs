/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-digest-form.mjs";

const CLOSE_ICON = "chrome://global/skin/icons/close.svg";

/**
 * A modal dialog holding the digest form. Hosts call open() with the stored
 * digest. The modal closes itself on Escape or click of any form CTA.
 * The form's digest-form:* events bubble out of it, so hosts can react to them
 */
export class SmartWindowDigestModal extends MozLitElement {
  static properties = {
    digest: { state: true },
    mode: { state: true },
    isOpen: { state: true },
  };

  constructor() {
    super();
    this.digest = null;
    this.mode = "default";
    this.isOpen = false;
  }

  get #dialog() {
    return this.shadowRoot.querySelector("dialog");
  }

  /**
   * Opens the modal with a fresh form.
   *
   * @param {object} options
   * @param {object} options.digest - The stored digest, from DigestManager.
   * @param {"default"|"edit"} [options.mode] - "default" turns on a suggested
   *   digest, "edit" saves changes to an enabled one.
   */
  async open({ digest, mode = "default" }) {
    // Drop any form left from the last open before rendering a new one.
    this.isOpen = false;
    await this.updateComplete;
    this.digest = digest;
    this.mode = mode;
    this.isOpen = true;
    await this.updateComplete;
    this.#dialog.showModal();
  }

  close() {
    this.#dialog?.close();
  }

  // The form is only rendered while the modal is open, so every open starts
  // from the digest rather than from the last session's edits. The close event
  // fires a task after close(), so ignore it if the modal was reopened since.
  #onDialogClose() {
    if (!this.#dialog.open) {
      this.isOpen = false;
    }
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-digest-modal.css"
      />
      <dialog
        data-l10n-id=${this.mode === "edit"
          ? "smartwindow-digest-modal-edit"
          : "smartwindow-digest-modal-create"}
        data-l10n-attrs="aria-label"
        @close=${this.#onDialogClose}
        @digest-form:cancel=${this.close}
        @digest-form:save-success=${this.close}
        @digest-form:save-error=${this.close}
      >
        <moz-button
          class="close-button"
          type="icon ghost"
          size="small"
          iconsrc=${CLOSE_ICON}
          data-l10n-id="smartwindow-digest-modal-close"
          data-l10n-attrs="aria-label"
          @click=${this.close}
        ></moz-button>
        ${this.isOpen
          ? html`<smartwindow-digest-form
              mode=${this.mode}
              .digest=${this.digest}
            ></smartwindow-digest-form>`
          : nothing}
      </dialog>
    `;
  }
}

customElements.define("smartwindow-digest-modal", SmartWindowDigestModal);
