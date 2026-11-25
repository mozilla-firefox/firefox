/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-promo.mjs";

const TIP_PREF = "browser.widgets.generated.promptTip.seen";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const lazy = {};
XPCOMUtils.defineLazyPreferenceGetter(lazy, "promptTipSeen", TIP_PREF, false);

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
    showTip: { type: Boolean, state: true },
  };

  static queries = {
    textareaEl: "textarea",
  };

  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  constructor() {
    super();
    this.value = "";
    this.focused = false;
    this.showTip = false;
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
    if (this.value.trim() && !lazy.promptTipSeen) {
      Services.prefs.setBoolPref(TIP_PREF, true);
      this.showTip = true;
    }
  }

  #onTipDismissed(event) {
    event.preventDefault();
    this.showTip = false;
    this.textareaEl.focus();
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
      <div aria-live="polite">
        ${this.showTip
          ? html`<moz-promo
              class="tip"
              type="vibrant"
              dismissable
              data-l10n-id="widget-composer-tip"
              @promo:user-dismissed=${this.#onTipDismissed}
            >
              <img
                slot="image"
                src="chrome://global/skin/icons/lightbulb.svg"
                alt=""
              />
            </moz-promo>`
          : nothing}
      </div>
      <div class="field" ?focused=${this.focused}>
        <div class="background"></div>
        <textarea
          aria-describedby="disclaimer"
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
      <p
        id="disclaimer"
        class="disclaimer"
        data-l10n-id="widget-composer-disclaimer"
      ></p>
    `;
  }
}

customElements.define("widget-composer", WidgetComposer);
