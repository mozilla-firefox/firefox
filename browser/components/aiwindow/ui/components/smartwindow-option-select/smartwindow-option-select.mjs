/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  ifDefined,
  nothing,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/panel-list.mjs";

/**
 * @typedef {object} OptionItem
 * @property {string} value - Value reported when the option is selected
 * @property {string} l10nId - Fluent ID for the option label
 * @property {object} [l10nArgs] - Fluent args for the option label
 * @property {string} [descriptionL10nId] - Fluent ID for a secondary line
 * @property {object} [descriptionL10nArgs] - Fluent args for the secondary line
 * @property {string} [iconSrc] - URL of an icon shown in a circle before the
 *   option text
 * @property {string} [secondaryLabelL10nId] - Fluent ID for a badge after the
 *   label, e.g. "Recommended"
 * @property {object} [secondaryLabelL10nArgs] - Fluent args for the secondary
 *   label
 * @property {string} [buttonL10nId] - Fluent ID shown on the closed button
 *   when this option is selected; defaults to l10nId
 * @property {object} [buttonL10nArgs] - Fluent args for buttonL10nId
 */

/**
 * A dropdown button that opens a panel-list of single-select options, each
 * with an optional description line and a check on the selected option.
 *
 * @property {OptionItem[]} options - Options to show in the menu
 * @property {string} value - The selected option value
 */
export class SmartwindowOptionSelect extends MozLitElement {
  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    options: { type: Array },
    value: { type: String, reflect: true },
    panelOpen: { type: Boolean, state: true },
  };

  static queries = {
    button: "moz-button",
    panelList: "panel-list",
  };

  constructor() {
    super();
    this.options = [];
    this.value = "";
    this.panelOpen = false;
    this._menuId = `option-select-menu-${crypto.randomUUID()}`;
  }

  get #selectedOption() {
    return this.options.find(option => option.value === this.value);
  }

  get #caretIcon() {
    return this.panelOpen
      ? "chrome://global/skin/icons/arrow-up-12.svg"
      : "chrome://global/skin/icons/arrow-down-12.svg";
  }

  get #buttonL10n() {
    let selected = this.#selectedOption;
    if (!selected) {
      return null;
    }
    return selected.buttonL10nId
      ? { id: selected.buttonL10nId, args: selected.buttonL10nArgs }
      : { id: selected.l10nId, args: selected.l10nArgs };
  }

  #onPanelShown = () => {
    this.panelOpen = true;
  };

  #onPanelHidden = () => {
    this.panelOpen = false;
  };

  #select(value) {
    this.panelList?.hide();
    if (value === this.value) {
      return;
    }
    this.value = value;
    this.dispatchEvent(
      new CustomEvent("smartwindow-option-select:change", {
        detail: { value },
        bubbles: true,
        composed: true,
      })
    );
  }

  #itemId(index, part) {
    return `${this._menuId}-${index}-${part}`;
  }

  #labelledBy(option, index) {
    let ids = [this.#itemId(index, "label")];
    if (option.secondaryLabelL10nId) {
      ids.push(this.#itemId(index, "secondary"));
    }
    return ids.join(" ");
  }

  #l10nArgs(args) {
    return args ? JSON.stringify(args) : nothing;
  }

  render() {
    const buttonL10n = this.#buttonL10n;
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-panel-list.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-option-select.css"
      />
      <moz-button
        type="default"
        class="option-select-button"
        .menuId=${this._menuId}
        data-l10n-id=${ifDefined(buttonL10n?.id)}
        data-l10n-args=${this.#l10nArgs(buttonL10n?.args)}
        .iconSrc=${this.#caretIcon}
        iconPosition="end"
      ></moz-button>
      <panel-list
        id=${this._menuId}
        @shown=${this.#onPanelShown}
        @hidden=${this.#onPanelHidden}
      >
        ${repeat(
          this.options,
          option => option.value,
          (option, index) => html`
            <button
              class="option-item"
              role="menuitemradio"
              aria-checked=${option.value === this.value}
              aria-labelledby=${this.#labelledBy(option, index)}
              aria-describedby=${ifDefined(
                option.descriptionL10nId
                  ? this.#itemId(index, "description")
                  : undefined
              )}
              data-value=${option.value}
              @click=${() => this.#select(option.value)}
            >
              ${option.iconSrc
                ? html`<span class="option-item-avatar"
                    ><img class="option-item-icon" src=${option.iconSrc} alt=""
                  /></span>`
                : nothing}
              <span class="option-item-content">
                <span class="option-item-header">
                  <span
                    id=${this.#itemId(index, "label")}
                    class="option-item-label"
                    data-l10n-id=${option.l10nId}
                    data-l10n-args=${this.#l10nArgs(option.l10nArgs)}
                  ></span>
                  ${option.secondaryLabelL10nId
                    ? html`<span
                        id=${this.#itemId(index, "secondary")}
                        class="option-item-badge"
                        data-l10n-id=${option.secondaryLabelL10nId}
                        data-l10n-args=${this.#l10nArgs(
                          option.secondaryLabelL10nArgs
                        )}
                      ></span>`
                    : nothing}
                </span>
                ${option.descriptionL10nId
                  ? html`<span
                      id=${this.#itemId(index, "description")}
                      class="option-item-description"
                      data-l10n-id=${option.descriptionL10nId}
                      data-l10n-args=${this.#l10nArgs(
                        option.descriptionL10nArgs
                      )}
                    ></span>`
                  : nothing}
              </span>
              ${option.value === this.value
                ? html`<img
                    class="option-item-check"
                    src="chrome://global/skin/icons/check.svg"
                    alt=""
                  />`
                : nothing}
            </button>
          `
        )}
      </panel-list>
    `;
  }
}

customElements.define("smartwindow-option-select", SmartwindowOptionSelect);
