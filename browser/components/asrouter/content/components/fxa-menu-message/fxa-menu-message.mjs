/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

/**
 * This widget is for a message that can be displayed in panelview menus when
 * the user is signed out to encourage them to sign in.
 */
export default class FxAMenuMessage extends MozLitElement {
  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };
  static properties = {
    imageURL: { type: String },
    buttonText: { type: String },
    primaryText: { type: String },
    secondaryText: { type: String },
    layout: { type: String, reflect: true },
  };
  static queries = {
    signUpButton: "#sign-up-button",
    closeButton: "#close-button",
  };

  constructor() {
    super();
    this.layout = "column"; // Default layout
    this.addEventListener(
      "keydown",
      event => {
        let keyCode = event.code;
        switch (keyCode) {
          case "ArrowLeft":
          // Intentional fall-through
          case "ArrowRight":
          // Intentional fall-through
          case "ArrowUp":
          // Intentional fall-through
          case "ArrowDown": {
            if (this.shadowRoot.activeElement === this.signUpButton) {
              this.closeButton.focus();
            } else {
              this.signUpButton.focus();
            }
            break;
          }
        }
      },
      { capture: true }
    );
  }

  handleClose(event) {
    // Keep the menu open by stopping the click event from
    // propagating up.
    event.stopPropagation();
    this.dispatchEvent(new CustomEvent("FxAMenuMessage:Close"), {
      bubbles: true,
    });
  }

  handleSignUp() {
    this.dispatchEvent(new CustomEvent("FxAMenuMessage:SignUp"), {
      bubbles: true,
    });
  }

  get isRowLayout() {
    return this.layout === "row";
  }

  render() {
    return html`<link
        rel="stylesheet"
        href="chrome://browser/content/asrouter/components/fxa-menu-message.css"
      />
      <div id="container" layout=${this.layout} ?has-image=${this.imageURL}>
        ${this.isRowLayout
          ? html`
              <div id="top-row">
                <moz-button
                  id="close-button"
                  @click=${this.handleClose}
                  type="ghost"
                  iconsrc="chrome://global/skin/icons/close-12.svg"
                  tabindex="2"
                  data-l10n-id="fxa-menu-message-close-button"
                >
                </moz-button>
                <div id="primary">${this.primaryText}</div>
              </div>
              <div id="bottom-row">
                <div id="body-container">
                  <div id="secondary">${this.secondaryText}</div>
                  <moz-button
                    id="sign-up-button"
                    @click=${this.handleSignUp}
                    type="primary"
                    tabindex="1"
                    autofocus
                    title=${this.buttonText}
                    aria-label=${this.buttonText}
                  >
                    ${this.buttonText}
                  </moz-button>
                </div>
                <div id="illustration-container">
                  <img
                    id="illustration"
                    role="presentation"
                    src=${this.imageURL}
                  />
                </div>
              </div>
            `
          : html`
              <moz-button
                id="close-button"
                @click=${this.handleClose}
                type="ghost"
                iconsrc="chrome://global/skin/icons/close-12.svg"
                tabindex="2"
                data-l10n-id="fxa-menu-message-close-button"
              >
              </moz-button>
              <div id="illustration-container">
                <img
                  id="illustration"
                  role="presentation"
                  src=${this.imageURL}
                />
              </div>
              <div id="primary">${this.primaryText}</div>
              <div id="secondary">${this.secondaryText}</div>
              <moz-button
                id="sign-up-button"
                @click=${this.handleSignUp}
                type="primary"
                tabindex="1"
                autofocus
                title=${this.buttonText}
                aria-label=${this.buttonText}
              >
                ${this.buttonText}
              </moz-button>
            `}
      </div>`;
  }
}

customElements.define("fxa-menu-message", FxAMenuMessage);
