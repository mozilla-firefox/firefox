/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";

export const SUBMIT_ICON =
  "chrome://browser/content/aiwindow/assets/ask-icon.svg";

/**
 * A prompt for creating widgets.
 *
 * @property {string} value - The text of the prompt.
 */
export class WidgetComposer extends MozLitElement {
  static properties = {
    value: { type: String },
    focused: { type: Boolean, state: true },
  };

  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  constructor() {
    super();
    this.value = "";
    this.focused = false;
    this.addEventListener("keydown", event => this.#onHostKeyDown(event));
  }

  #onFocus() {
    this.focused = true;
  }

  #onBlur() {
    this.focused = false;
  }

  #onInput(event) {
    this.value = event.target.value;
  }

  #onHostKeyDown(event) {
    if (event.key === "Escape" && !event.isComposing) {
      event.preventDefault();
      this.dispatchEvent(
        new CustomEvent("WidgetComposer:Dismiss", { bubbles: true })
      );
    }
  }

  #onKeyDown(event) {
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      this.#submit();
    }
  }

  #submit() {
    let text = this.value.trim();
    if (!text) {
      return;
    }
    this.dispatchEvent(
      new CustomEvent("WidgetComposer:Submit", {
        bubbles: true,
        detail: { prompt: text },
      })
    );
  }

  render() {
    let hasText = !!this.value.trim();
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/widgets/widget-composer.css"
      />
      <div class="field" ?focused=${this.focused}>
        <div class="background"></div>
        <textarea
          data-l10n-id="widget-composer-input"
          .value=${this.value}
          @focus=${this.#onFocus}
          @blur=${this.#onBlur}
          @input=${this.#onInput}
          @keydown=${this.#onKeyDown}
        ></textarea>
        <div class="actions">
          <moz-button
            class="submit"
            type=${hasText ? "primary" : "ghost icon"}
            iconsrc=${SUBMIT_ICON}
            data-l10n-id="widget-composer-submit-button"
            ?disabled=${!hasText}
            @click=${this.#submit}
          ></moz-button>
        </div>
      </div>
    `;
  }
}

customElements.define("widget-composer", WidgetComposer);
