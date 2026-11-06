/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

/*
 * Puts the UI in choose state where the user can
 * choose to open the AITab in either the currently
 * opened tab or in a new tab.
 */
export const STATE_CHOOSE = "choose";
/**
 * Puts the UI in complete state after the user has
 * already chosen what tab to open the AITab in,
 * renders a link to the AITab to either re-focus
 * the AITab or re-open it if it's closed
 */
export const STATE_COMPLETE = "complete";
/*
 * List of available states for this component.
 */
export const STATES = [STATE_CHOOSE, STATE_COMPLETE];

/**
 * Renders options for opening a generated AITab and its completed state.
 */
export class AITabToolUI extends MozLitElement {
  static properties = {
    state: { type: String },
    title: { type: String },
    viewerURL: { type: String },
    actionsStateOpen: { type: Boolean },
  };

  constructor() {
    super();
    this.state = STATE_CHOOSE;
    this.title = "";
    this.viewerURL = "";
    this.actionsStateOpen = true;
  }

  #handleTitleClick(event) {
    event.preventDefault();

    this.#requestOpen("new");
  }

  #handleKeyboardActivation(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      this.#toggleActionsState();
    }
  }

  #toggleActionsState() {
    this.actionsStateOpen = !this.actionsStateOpen;
  }

  #requestOpen(openTarget) {
    this.dispatchEvent(
      new CustomEvent("aitab-open-request", {
        bubbles: true,
        composed: true,
        detail: { openTarget },
      })
    );
  }

  #renderCardContents() {
    const arrow = this.actionsStateOpen
      ? "chrome://global/skin/icons/arrow-up-12.svg"
      : "chrome://global/skin/icons/arrow-down-12.svg";

    return html`<div class="card">
      <div
        class="card-title"
        role="button"
        tabindex="0"
        aria-expanded=${this.actionsStateOpen ? "true" : "false"}
        aria-controls="aitab-card-actions"
        @click=${() => this.#toggleActionsState()}
        @keydown=${e => this.#handleKeyboardActivation(e)}
      >
        <img src=${arrow} alt="" />
        <span data-l10n-id="smartwindow-aitab-response-card-title"></span>
      </div>

      ${this.actionsStateOpen ? this.#renderTitle() : nothing}
      ${this.actionsStateOpen && this.state === STATE_CHOOSE
        ? this.#renderChoices()
        : nothing}
    </div>`;
  }

  #renderChoices() {
    return html`
      <div id="aitab-card-actions" class="actions">
        <moz-button
          type="default"
          data-l10n-id="smartwindow-aitab-open-current"
          data-l10n-attrs="label"
          @click=${() => this.#requestOpen("current")}
        ></moz-button>
        <moz-button
          type="primary"
          data-l10n-id="smartwindow-aitab-open-new"
          data-l10n-attrs="label"
          @click=${() => this.#requestOpen("new")}
        ></moz-button>
      </div>
    `;
  }

  #renderTitle() {
    const title = html`
      <img
        src="chrome://browser/skin/smart-window-mono-32.svg"
        alt=""
        width="16"
        height="16"
      />
      ${this.title}
    `;

    return this.viewerURL
      ? html`<a
          href=${this.viewerURL}
          class="page-title"
          @click=${this.#handleTitleClick}
          >${title}</a
        >`
      : html`<p class="page-title">${title}</p>`;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-action-confirmation.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/aitab-tool-ui.css"
      />

      ${this.#renderCardContents()}
    `;
  }
}

customElements.define("aitab-tool-ui", AITabToolUI);
