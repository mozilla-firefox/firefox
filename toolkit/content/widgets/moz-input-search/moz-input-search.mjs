/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-search.tagmap.d.ts" />

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

/**
 * A search input custom element.
 *
 * @tagname moz-input-search
 */
export class MozInputSearch extends MozInputText {
  // The amount of milliseconds that we wait before firing the "search" event.
  static #searchDebounceDelayMs = 500;

  #searchTimer = null;

  #clearSearchTimer() {
    if (this.#searchTimer) {
      clearTimeout(this.#searchTimer);
    }
    this.#searchTimer = null;
  }

  #dispatchSearch() {
    this.dispatchEvent(
      new CustomEvent("MozInputSearch:search", {
        bubbles: true,
        composed: true,
        detail: { query: this.value },
      })
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#clearSearchTimer();
  }

  inputStylesTemplate() {
    return html`<link
      rel="stylesheet"
      href="chrome://global/content/elements/moz-input-search.css"
    />`;
  }

  handleInput(e) {
    super.handleInput(e);
    this.#clearSearchTimer();
    this.#searchTimer = setTimeout(() => {
      this.#dispatchSearch();
    }, MozInputSearch.#searchDebounceDelayMs);
  }

  // Clears the value and synchronously dispatches a search event if needed.
  clear() {
    this.#clearSearchTimer();
    if (this.value) {
      this.value = this.inputEl.value = "";
      this.#dispatchSearch();
    }
  }

  #hasIcon() {
    // If unspecified, search inputs still have a default search icon.
    return this.iconSrc === undefined || !!this.iconSrc;
  }

  inputTemplate() {
    return html`
      <input
        id="input"
        class=${this.#hasIcon() ? "with-icon" : ""}
        type="search"
        name=${this.name}
        .value=${this.value}
        ?disabled=${this.disabled || this.parentDisabled}
        ?required=${this.required}
        accesskey=${ifDefined(this.accessKey)}
        placeholder=${ifDefined(this.placeholder)}
        aria-label=${ifDefined(this.ariaLabel ?? undefined)}
        aria-describedby="description"
        aria-description=${ifDefined(
          this.hasDescription ? undefined : this.ariaDescription
        )}
        title=${ifDefined(this.title)}
        @input=${this.handleInput}
        @change=${this.redispatchEvent}
      />
    `;
  }
}
customElements.define("moz-input-search", MozInputSearch);
