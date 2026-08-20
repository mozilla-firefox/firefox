/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  createRef,
  html,
  ref,
  classMap,
  ifDefined,
  styleMap,
} from "../vendor/lit.all.mjs";
import { MozBaseInputElement, MozLitElement } from "../lit-utils.mjs";

/** @import { TemplateResult } from "chrome://global/content/vendor/lit.all.mjs" */

/**
 * @typedef {object} SelectOption
 * @property {string} value - The value of the option.
 * @property {string} label - The display label of the option.
 * @property {string} [iconSrc] - The icon source URL for the option.
 * @property {boolean} [disabled] - Whether the option is disabled.
 * @property {boolean} [hidden] - Whether the option is hidden.
 */

/**
 * A select dropdown with options provided via custom `moz-option` elements.
 *
 * @tagname moz-select
 * @property {string} label - The text of the label element
 * @property {string} size - The select size.
 *   Options: default, small.
 * @property {string} name - The name of the input control
 * @property {string} value - The value of the selected option
 * @property {boolean} disabled - The disabled state of the input control
 * @property {string} iconSrc - The src for an optional icon
 * @property {string} description - The text for the description element that helps describe the input control
 * @property {string} supportPage - Name of the SUMO support page to link to.
 * @property {string} ariaLabel - The aria-label text when there is no visible label.
 * @property {string} ariaDescription - The aria-description text when there is no visible description.
 * @property {SelectOption[]} options - The array of options, populated by <moz-option> children in the
 *     default slot. Do not set directly, these will be overridden by <moz-option> children.
 * @property {SelectOption} selectedOption - The currently selected option object.
 * @property {number} selectedIndex - The index of the currently selected option.
 * @property {boolean} usePanelList - Whether or not to render a panel. Depends on options using icons.
 */
export default class MozSelect extends MozBaseInputElement {
  static properties = {
    size: { type: String, reflect: true },
    options: { type: Array, state: true },
    selectedOption: { type: Object, state: true },
    selectedIndex: { type: Number, state: true },
    usePanelList: { type: Boolean, state: true },
  };
  static inputLayout = "block";

  static queries = {
    panelList: "panel-list",
    panelTrigger: ".panel-trigger",
  };

  constructor() {
    super();
    this.size = "default";
    this.value = "";
    this.options = [];
    this.usePanelList = false;
    this.selectedOption = null;
    this.selectedIndex = 0;
    this.slotRef = createRef();
    this.optionsMutationObserver = new MutationObserver(
      this.populateOptions.bind(this)
    );
  }

  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.optionsMutationObserver.observe(this, {
      attributeFilter: ["label", "value", "iconsrc", "disabled", "hidden"],
      childList: true,
      subtree: true,
    });
  }

  update(changedProperties) {
    super.update(changedProperties);
    if (this.hasUpdated && changedProperties.has("options")) {
      // Match the select's value on initial render or options change.
      this.value = this.inputEl.value;
    }
  }

  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);
    if (changedProperties.has("value") || changedProperties.has("options")) {
      this.selectedIndex = this.options.findIndex(
        opt => opt.value === this.value
      );
      this.selectedOption = this.options[this.selectedIndex] ?? this.options[0];
    }
  }

  updated() {
    if (
      this.panelTrigger &&
      this.panelList &&
      this.panelTrigger.popoverTargetElement !== this.panelList
    ) {
      this.panelTrigger.popoverTargetElement = this.panelList;
    }
  }

  /**
   * Gets the icon source for the currently selected option.
   *
   * @returns {string} The icon source URL or empty string.
   */
  get _selectedOptionIconSrc() {
    return this.selectedOption?.iconSrc ?? "";
  }

  /**
   * Internal - populates the select element with options from the light DOM slot.
   */
  populateOptions() {
    if (!this.slotRef.value) {
      this.options = [];
      this.usePanelList = false;
      return;
    }

    let options = [];

    for (const node of this.slotRef.value.assignedNodes()) {
      if (node.localName === "moz-option") {
        options.push({
          value: node.getAttribute("value"),
          label: node.getAttribute("label"),
          iconSrc: node.getAttribute("iconsrc"),
          disabled: node.getAttribute("disabled") !== null,
          hidden: node.getAttribute("hidden") !== null,
        });
      } else if (node.localName === "hr") {
        options.push({
          separator: true,
        });
      }
    }

    this.options = options;
    this.usePanelList = options.some(opt => opt.iconSrc);

    // Default to first option if no value set to match native select behavior.
    if (this.usePanelList && !this.value && this.options.length) {
      this.value = this.options[0].value;
    }
  }

  /**
   * Handles change events and updates the selected value.
   *
   * @param {Event} event
   * @memberof MozSelect
   */
  handleStateChange(event) {
    this.value = event.target.value;
  }

  /**
   * Handles change events from the panel-list and dispatches a change event.
   *
   * @param {Event} event - The click event from panel-item selection.
   */
  handlePanelChange(event) {
    this.handleStateChange(event);
    this.redispatchEvent(new Event("change", { bubbles: true }));
  }

  /**
   * Handles the panel being hidden and returns focus to the trigger button.
   */
  handlePanelHidden() {
    let active = document.activeElement;
    if (!active || active === document.body || active === this) {
      this.panelTrigger?.focus();
    }
  }

  /**
   * Handles mousedown events to open the panel for mouse clicks.
   * Note: panel-list's handleEvent() ignores the triggeringEvent by
   * reference, preventing the panel from immediately closing.
   *
   * @param {MouseEvent} event - The mousedown event.
   */
  handlePanelMousedown(event) {
    if (event.button !== 0) {
      return;
    }
    /**
     * Bug 2017668 - This is required for the "Default search engine"
     * and private search engine moz-selects. Otherwise, clicking on one
     * of the select elements, using arrow keys to navigate, and then clicking
     * on the other select element will cause focus to jump between the two
     * moz-select elements while toggling their respective panels.
     */
    if (navigator.platform.includes("Mac")) {
      this.panelTrigger?.focus();
    }
    this.panelList?.toggle(event, this.panelTrigger);
  }

  /**
   * Handles click events from keyboard activation (Space/Enter from button).
   * Mouse clicks are handled by mousedown, so we filter those out here.
   *
   * @param {MouseEvent} event - The click event.
   */
  handlePanelClick(event) {
    // Only handle keyboard-initiated clicks. Mouse clicks are handled
    // by mousedown. event.detail is 0 for keyboard clicks, >0 for
    // mouse clicks.
    event.preventDefault();
    if (event.detail === 0) {
      this.panelList?.toggle(event);
    }
  }

  /**
   * Toggles the panel-list open/closed state.
   * Called by keyboard handlers and potentially other event handlers.
   *
   * @param {Event} event - The triggering event.
   */
  togglePanel(event) {
    this.panelList?.toggle(event);
  }

  /**
   * Handles keyboard events on the panel trigger button.
   * Arrow keys change selection (Windows/Linux) or open the panel (Mac).
   * Space and Enter open the panel.
   *
   * @param {KeyboardEvent} event - The keyboard event.
   */
  handlePanelKeydown(event) {
    if (this.panelList?.open) {
      return;
    }

    switch (event.key) {
      case "ArrowDown":
      case "ArrowUp":
        event.preventDefault();
        if (navigator.platform.includes("Mac")) {
          this.togglePanel(event);
        } else {
          this.selectNextOption(event.key === "ArrowDown" ? 1 : -1);
        }
        break;
      case "Enter":
        event.preventDefault();
        this.togglePanel(event);
        break;
    }
  }

  /**
   * Selects the next enabled option in the given direction. Skips disabled and
   * hidden options.
   *
   * @param {number} direction - The direction to move (1 for next, -1 for
   * previous).
   */
  selectNextOption(direction) {
    let currentIndex = this.selectedIndex;
    let options = this.options;

    for (let i = 1; i < options.length; i++) {
      let nextIndex = currentIndex + direction * i;
      let nextOption = options[nextIndex];
      if (
        nextOption &&
        !nextOption.disabled &&
        !nextOption.hidden &&
        !nextOption.separator
      ) {
        this.value = nextOption.value;
        this.redispatchEvent(new Event("change", { bubbles: true }));
        return;
      }
    }
  }

  /**
   * @type {MozBaseInputElement['inputStylesTemplate']}
   */
  inputStylesTemplate() {
    return html` <link
      rel="stylesheet"
      href="chrome://global/content/elements/moz-select.css"
    />`;
  }

  /**
   * Renders the icon for the currently selected option.
   *
   * @returns {TemplateResult | null}
   */
  selectedOptionIconTemplate() {
    if (this._selectedOptionIconSrc) {
      return html`<img
        src=${this._selectedOptionIconSrc}
        role="presentation"
        class="select-option-icon"
      />`;
    }
    return null;
  }

  /**
   * Renders the native select element (used when options don't have icons).
   *
   * @returns {TemplateResult}
   */
  selectTemplate() {
    return html`<select
      id="input"
      name=${this.name}
      .value=${this.value}
      accesskey=${this.accessKey}
      @input=${this.handleStateChange}
      @change=${this.redispatchEvent}
      ?disabled=${this.disabled || this.parentDisabled}
      size=${this.size}
      aria-label=${ifDefined(this.ariaLabel ?? undefined)}
      aria-describedby="description"
      aria-description=${ifDefined(
        this.hasDescription ? undefined : this.ariaDescription
      )}
    >
      ${this.options.map(option =>
        option.separator
          ? html`<hr />`
          : html`
              <option
                value=${option.value}
                .selected=${option.value == this.value}
                ?disabled=${option.disabled}
                ?hidden=${option.hidden}
              >
                ${option.label}
              </option>
            `
      )}
    </select>`;
  }

  /**
   * Renders the button trigger for the panel-list (used when options have
   * icons).
   *
   * @returns {TemplateResult}
   */
  panelTargetTemplate() {
    return html`<button
      id="input"
      name=${this.name}
      .value=${this.value}
      class="panel-trigger"
      type="button"
      role="combobox"
      aria-label=${ifDefined(this.ariaLabel)}
      aria-description=${ifDefined(
        this.hasDescription ? undefined : this.ariaDescription
      )}
      aria-describedby="description"
      aria-haspopup="menu"
      aria-expanded=${this.panelList?.open ? "true" : "false"}
      accesskey=${ifDefined(this.accessKey)}
      @mousedown=${this.handlePanelMousedown}
      @click=${this.handlePanelClick}
      @keydown=${this.handlePanelKeydown}
      ?disabled=${this.disabled || this.parentDisabled}
      size=${this.size}
    >
      <span class="panel-trigger-text">${this.selectedOption?.label}</span>
    </button>`;
  }

  /**
   * Renders the panel-list dropdown menu (used when options have icons).
   *
   * @returns {TemplateResult}
   */
  panelListTemplate() {
    return html`<panel-list
      .value=${this.value}
      min-width-from-anchor
      @click=${this.handlePanelChange}
      @hidden=${this.handlePanelHidden}
    >
      ${this.options.map(option =>
        option.separator
          ? html`<hr />`
          : html`<panel-item
              .value=${option.value}
              ?selected=${option.value == this.value}
              ?disabled=${option.disabled}
              ?hidden=${option.hidden}
              icon=${ifDefined(option.iconSrc)}
              style=${styleMap(
                option.iconSrc
                  ? { "--select-item-icon-url": `url(${option.iconSrc})` }
                  : {}
              )}
            >
              ${option.label}
            </panel-item>`
      )}
    </panel-list>`;
  }

  /**
   * Renders the main input template with either a native select or panel-list.
   *
   * @returns {TemplateResult}
   */
  inputTemplate() {
    return html`
      <div
        class=${classMap({
          "select-wrapper": true,
          "with-icon": !!this._selectedOptionIconSrc,
        })}
      >
        ${this.selectedOptionIconTemplate()}
        ${!this.usePanelList
          ? this.selectTemplate()
          : this.panelTargetTemplate()}
        <img
          src="chrome://global/skin/icons/arrow-down.svg"
          role="presentation"
          class="select-chevron-icon"
        />
      </div>
      ${this.usePanelList ? this.panelListTemplate() : ""}
      <slot
        @slotchange=${this.populateOptions}
        hidden
        ${ref(this.slotRef)}
      ></slot>
    `;
  }
}
customElements.define("moz-select", MozSelect);

/**
 * A custom option element for use in moz-select.
 *
 * @tagname moz-option
 * @property {string} value - The value of the option
 * @property {string} label - The label of the option
 * @property {string} iconSrc - The path to the icon of the the option
 * @property {boolean} disabled - Whether the option is disabled
 * @property {boolean} hidden - Whether the option is hidden
 */
export class MozOption extends MozLitElement {
  static properties = {
    // Reflect the attribute so that moz-select can detect changes with a MutationObserver
    value: { type: String, reflect: true },
    // Reflect the attribute so that moz-select can detect changes with a MutationObserver
    label: { type: String, reflect: true, fluent: true },
    iconSrc: { type: String, reflect: true },
    disabled: { type: Boolean, reflect: true },
    hidden: { type: Boolean, reflect: true },
  };

  constructor() {
    super();
    this.value = "";
    this.label = "";
    this.iconSrc = "";
    this.disabled = false;
    this.hidden = false;
  }

  render() {
    // This is just a placeholder to pass values into moz-select.
    return "";
  }
}
customElements.define("moz-option", MozOption);
