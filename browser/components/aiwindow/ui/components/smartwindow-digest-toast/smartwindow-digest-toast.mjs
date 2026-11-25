/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-message-bar.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

const CREATED_L10N_IDS = Object.freeze({
  daily: "smartwindow-digest-toast-created-daily",
  weekly: "smartwindow-digest-toast-created-weekly",
});
const DEFAULT_CREATED_L10N_ID = "smartwindow-digest-toast-created";

const ERROR_L10N_IDS = Object.freeze({
  default: "smartwindow-digest-toast-error-create",
  edit: "smartwindow-digest-toast-error-edit",
});

/**
 * A toast pinned to the top corner of the page that notifies the result of
 * saving a digest. Each toast opens on top of any toast already showing.
 *
 * The toast opens once its text is translated because screen readers
 * announce it as it appears.
 * The toast stays on screen until the user closes it or clicks the toast CTA,
 * then removes itself.
 * The toast never takes focus.
 * The toast removes itself when the window switches to classic.
 *
 * "View digest" fires smartwindow-digest-view, detail: { digest }.
 *
 * @property {"success"|"error"} type - Whether the save worked.
 * @property {?object} digest - The saved digest, for a success toast.
 * @property {"default"|"edit"} mode - The digest form's mode, for an error
 *   toast: "default" for creating a digest, "edit" for saving changes to one.
 */
export class SmartWindowDigestToast extends MozLitElement {
  static properties = {
    type: { type: String },
    digest: { type: Object },
    mode: { type: String },
    closing: { type: Boolean, reflect: true },
  };

  constructor() {
    super();
    this.type = "success";
    this.digest = null;
    this.mode = "default";
    this.closing = false;
  }

  connectedCallback() {
    super.connectedCallback();
    this.popover = "manual";
    if (!window.IS_STORYBOOK) {
      Services.obs.addObserver(
        this.#onWindowModeChange,
        "ai-window-state-changed"
      );
      // Remove the toast on unload because unloading doesn't call
      // disconnectedCallback, which removes the observer.
      this.documentGlobal.addEventListener("unload", () => this.remove(), {
        once: true,
      });
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (!window.IS_STORYBOOK) {
      Services.obs.removeObserver(
        this.#onWindowModeChange,
        "ai-window-state-changed"
      );
    }
  }

  #onWindowModeChange = (subject, _topic, mode) => {
    if (
      mode == "classic" &&
      subject == window.browsingContext?.topChromeWindow
    ) {
      this.remove();
    }
  };

  async firstUpdated() {
    await this.ownerDocument.l10n?.translateFragment(this.shadowRoot);
    await this.shadowRoot.querySelector("moz-message-bar")?.updateComplete;
    if (this.isConnected) {
      this.showPopover();
    }
  }

  /**
   * Fades the toast out, then removes it.
   */
  async close() {
    if (this.closing) {
      return;
    }
    this.closing = true;
    await this.updateComplete;
    await Promise.allSettled(
      this.getAnimations().map(animation => animation.finished)
    );
    this.remove();
  }

  #onDismiss(event) {
    event.preventDefault();
    this.close();
  }

  #onView() {
    this.close();
    this.dispatchEvent(
      new CustomEvent("smartwindow-digest-view", {
        detail: { digest: this.digest },
        bubbles: true,
        composed: true,
      })
    );
  }

  #renderSuccess() {
    const l10nId =
      CREATED_L10N_IDS[this.digest?.schedule?.type] ?? DEFAULT_CREATED_L10N_ID;
    return html`
      <moz-message-bar
        dismissable
        data-l10n-id=${l10nId}
        data-l10n-args=${JSON.stringify({ title: this.digest?.title })}
        @message-bar:user-dismissed=${this.#onDismiss}
      >
        <moz-button
          slot="actions"
          class="view-button"
          type="muted"
          data-l10n-id="smartwindow-digest-toast-view"
          @click=${this.#onView}
        ></moz-button>
      </moz-message-bar>
    `;
  }

  #renderError() {
    return html`
      <moz-message-bar
        type="error"
        dismissable
        data-l10n-id=${ERROR_L10N_IDS[this.mode]}
        @message-bar:user-dismissed=${this.#onDismiss}
      ></moz-message-bar>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-digest-toast.css"
      />
      ${this.type === "error" ? this.#renderError() : this.#renderSuccess()}
    `;
  }
}

customElements.define("smartwindow-digest-toast", SmartWindowDigestToast);
