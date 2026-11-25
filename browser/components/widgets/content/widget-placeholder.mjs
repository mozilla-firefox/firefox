/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

const STOP_ICON =
  "chrome://browser/content/aiwindow/assets/stop-generation.svg";
const EDIT_ICON = "chrome://global/skin/icons/reload.svg";
const SPARKLES_IMAGE = "chrome://browser/content/widgets/assets/sparkles.svg";

const { GENERATION_ERRORS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/WidgetGenerator.sys.mjs"
);
const { CREATION_ERRORS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs"
);

const ERROR_L10N_IDS = new Map([
  [GENERATION_ERRORS.BLOCKED, "widget-generation-error-blocked"],
  [GENERATION_ERRORS.AUTH, "widget-generation-error-auth"],
  [GENERATION_ERRORS.SERVICE, "widget-generation-error-service"],
  [GENERATION_ERRORS.BUDGET, "widget-generation-error-budget"],
  [GENERATION_ERRORS.RATE_LIMIT, "widget-generation-error-rate-limit"],
  [GENERATION_ERRORS.CAPACITY, "widget-generation-error-service"],
  [GENERATION_ERRORS.NETWORK, "widget-generation-error-network"],
  [GENERATION_ERRORS.GENERATION, "widget-generation-error-generation"],
  [GENERATION_ERRORS.UNSUPPORTED, "widget-generation-error-unsupported"],
  [GENERATION_ERRORS.UNCLEAR, "widget-generation-error-unsupported"],
  [GENERATION_ERRORS.POLICY, "widget-generation-error-policy"],
  [CREATION_ERRORS.INSTALL, "widget-generation-error-service"],
]);

CSS.registerProperty({
  name: "--widget-placeholder-angle",
  syntax: "<angle>",
  inherits: false,
  initialValue: "0deg",
});

/**
 * Shows the prompt during widget creation and an error message on failure.
 * Button sends `WidgetPlaceholder:Edit` to reopen the composer with the prompt.
 *
 * @property {string} prompt - The widget creation prompt.
 * @property {?string} error - An error code or `null` while creating the widget.
 * @property {string} heading - What went wrong, set by Fluent from the error's message.
 * @property {string} message - What to do next, set by Fluent from the error's message.
 */
export class WidgetPlaceholder extends MozLitElement {
  static properties = {
    prompt: { type: String },
    error: { type: String, reflect: true },
    heading: { type: String, fluent: true },
    message: { type: String, fluent: true },
  };

  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  #hadFocus = false;
  #announcePending = false;

  constructor() {
    super();
    this.prompt = "";
    this.error = null;
    this.heading = "";
    this.message = "";
  }

  willUpdate(changes) {
    super.willUpdate(changes);
    this.#hadFocus = !!this.renderRoot.activeElement;
    this.#announcePending =
      changes.has("error") && this.hasUpdated && !!this.error;
    if (changes.has("error")) {
      if (this.error) {
        this.dataset.l10nId =
          ERROR_L10N_IDS.get(this.error) ?? "widget-generation-error-service";
      } else {
        delete this.dataset.l10nId;
      }
    }
  }

  async updated() {
    if (this.#announcePending) {
      this.#announceError();
    }
    if (this.#hadFocus && !this.renderRoot.activeElement) {
      await this.updateComplete;
      this.focus();
    }
  }

  async #announceError() {
    await document.l10n.translateElements([this]);
    let announcement = [this.heading, this.message].filter(Boolean).join(" ");
    if (announcement) {
      this.ariaNotify(announcement, { priority: "high" });
    }
  }

  #dispatch(type) {
    this.dispatchEvent(new CustomEvent(type, { bubbles: true }));
  }

  #onEdit() {
    this.#dispatch("WidgetPlaceholder:Edit");
  }

  creatingTemplate() {
    return html`
      <img class="sparkles" src=${SPARKLES_IMAGE} alt="" />
      <p class="prompt"><q>${this.prompt}</q></p>
      <moz-button
        class="cancel"
        type="icon muted"
        iconsrc=${STOP_ICON}
        data-l10n-id="widget-placeholder-cancel-button"
        @click=${this.#onEdit}
      ></moz-button>
    `;
  }

  errorTemplate() {
    return html`
      <div class="error">
        <p class="error-heading">${this.heading}</p>
        <p class="error-message">${this.message}</p>
      </div>
      <moz-button
        class="edit"
        type="primary"
        iconsrc=${EDIT_ICON}
        data-l10n-id="widget-placeholder-edit-button"
        @click=${this.#onEdit}
      ></moz-button>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/widgets/widget-placeholder.css"
      />
      <div
        class="content"
        role="group"
        tabindex="0"
        data-l10n-id=${this.error
          ? "widget-placeholder-failed"
          : "widget-placeholder-building"}
      >
        ${this.error ? this.errorTemplate() : this.creatingTemplate()}
      </div>
    `;
  }
}

customElements.define("widget-placeholder", WidgetPlaceholder);
