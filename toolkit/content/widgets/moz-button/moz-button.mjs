/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-button.tagmap.d.ts" />

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozButton.ftl");

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-label.mjs";

/**
 * Controller for moz-buttons with popovers.
 */
class PopoverController {
  /** @type {MozButton} */
  host;

  /** @type {HTMLElement | null} */
  triggerEl = null;

  /** @type {HTMLElement | null} */
  listenerTargetEl = null;

  /** @type {HTMLElement | null} */
  popoverEl = null;

  /**
   * @type {string}
   *
   * The ID of the popover element.
   */
  get popoverTarget() {
    return this.host.popoverTarget;
  }

  /**
   * @type {string}
   *
   * The default value of the aria-haspopup attribute that should be set on the
   * host. Consumers can override this.
   */
  get ariaHasPopupDefault() {
    return "dialog";
  }

  /**
   * @type {string[]}
   *
   * The events that are expected to be fired when the popover element is shown
   * and hidden.
   */
  get popoverToggledEvents() {
    return ["toggle"];
  }

  /**
   * @param {MozButton} host
   */
  constructor(host) {
    this.host = host;
    host.addController(this);
  }

  hostConnected() {
    if (!this.popoverTarget) {
      return;
    }
    this.popoverEl = this.getPopover();
    if (this.popoverEl) {
      this.popoverSetUp();
    }
  }

  hostDisconnected() {
    this.removeListeners();
    this.popoverEl = null;
  }

  /**
   * Gets the element in the host that opens the popover. This should never be
   * null.
   *
   * @returns {HTMLElement}
   */
  getTriggerEl() {
    return this.host.buttonEl;
  }

  /**
   * Gets the element that receives the click and mousedown listeners that
   * toggle the popover.
   *
   * @returns {HTMLElement}
   */
  getListenerTargetEl() {
    return this.host;
  }

  /**
   * Retrieves the element matching the host's popoverTarget.
   *
   * @returns {HTMLElement | null}
   */
  getPopover() {
    let root = this.host.getRootNode();
    let popoverEl = null;

    while (root) {
      popoverEl = root.querySelector?.(`#${this.popoverTarget}`);
      if (popoverEl) {
        break;
      }

      if (root instanceof ShadowRoot) {
        root = root.host?.getRootNode();
      } else {
        break;
      }
    }

    return popoverEl;
  }

  /**
   * Handles opening/closing the popover when the trigger element is clicked or
   * activated via the keyboard.
   *
   * @param {MouseEvent|KeyboardEvent} event
   */
  #trigger = event => {
    if (event.type == "click") {
      event.preventDefault();
    }
    if (
      (event.type == "mousedown" && event.button == 0) ||
      event.inputSource == MouseEvent.MOZ_SOURCE_KEYBOARD ||
      !event.detail
    ) {
      this.togglePopover(event);
    }
  };

  /**
   * Toggles the popover.
   *
   * @param {MouseEvent|KeyboardEvent} _event
   */
  togglePopover(_event) {
    this.popoverEl?.togglePopover();
  }

  /**
   * Listener for the popover events defined in popoverToggledEvents.
   *
   * @param {Event} _event
   */
  onPopoverToggled(_event) {
    let isOpen = !!this.popoverEl?.matches(":popover-open");
    this.updateOpenAttr(isOpen);
  }

  /**
   * Updates host's open attribute and ariaExpanded.
   *
   * @param {boolean} isOpen
   */
  updateOpenAttr(isOpen) {
    this.host.toggleAttribute("open", isOpen);
    this.host.ariaExpanded = isOpen ? "true" : "false";
  }

  /**
   * Removes event listeners.
   */
  removeListeners() {
    this.listenerTargetEl?.removeEventListener("click", this.#trigger);
    this.listenerTargetEl?.removeEventListener("mousedown", this.#trigger);
    for (let e of this.popoverToggledEvents) {
      this.popoverEl?.removeEventListener(e, this.#onPopoverToggled);
    }
    this.#onPopoverToggled = null;
  }

  /**
   * Sets up the host for integration with the popover,
   * adding necessary event listeners and ARIA attributes.
   */
  popoverSetUp() {
    this.triggerEl = this.getTriggerEl();
    this.listenerTargetEl = this.getListenerTargetEl();
    this.listenerTargetEl.addEventListener("click", this.#trigger);
    this.listenerTargetEl.addEventListener("mousedown", this.#trigger);
    this.#onPopoverToggled = event => this.onPopoverToggled(event);
    for (let e of this.popoverToggledEvents) {
      this.popoverEl?.addEventListener(e, this.#onPopoverToggled);
    }
    this.updateOpenAttr(false);
    this.triggerEl.popoverTargetElement = this.popoverEl;

    // Update the host's ariaHasPopup.
    this.host.requestUpdate();
  }

  /**
   * Cleans up popover integration,
   * removing event listeners and clearing ARIA attributes.
   */
  popoverCleanUp() {
    this.removeListeners();
    this.host.ariaExpanded = null;
    this.host.removeAttribute("open");
    if (this.triggerEl) {
      this.triggerEl.popoverTargetElement = null;
      this.triggerEl = null;
    }
    this.listenerTargetEl = null;

    // Update the host's ariaHasPopup.
    this.host.requestUpdate();
  }

  /**
   * Cleans up the popover integration and detaches from the host.
   */
  destroy() {
    this.popoverCleanUp();
    this.popoverEl = null;
    this.host.removeController(this);
  }

  /** @type {Function} */
  #onPopoverToggled;
}

/**
 * Controls moz-button behavior when menuId property is set.
 * Helps to integrate moz-button with panel-list.
 */
class MenuController extends PopoverController {
  get popoverTarget() {
    return this.host.menuId;
  }

  get ariaHasPopupDefault() {
    return "menu";
  }

  get popoverToggledEvents() {
    return ["shown", "hidden"];
  }

  getTriggerEl() {
    return this.host.isSplitButton
      ? this.host.chevronButtonEl
      : this.host.buttonEl;
  }

  getListenerTargetEl() {
    return this.host.isSplitButton ? this.host.chevronButtonEl : this.host;
  }

  onPopoverToggled(event) {
    let isOpen = event?.type == "shown";
    this.updateOpenAttr(isOpen);
  }

  popoverCleanUp() {
    if (this.popoverEl?.localName == "panel-list") {
      super.popoverCleanUp();
    }
  }

  popoverSetUp() {
    if (this.popoverEl?.localName == "panel-list") {
      super.popoverSetUp();
    }
  }

  togglePopover(event) {
    this.popoverEl?.toggle(
      event,
      this.host.isSplitButton ? this.host.chevronButtonEl : this.host
    );
  }
}

/**
 * A button with multiple types and three sizes.
 *
 * @tagname moz-button
 * @property {string} label - The button's label, will be overridden by slotted content.
 * @property {string} type - The button type.
 *   Options: default, primary, destructive, icon, icon ghost, ghost.
 * @property {string} size - The button size.
 *   Options: default, small, large.
 * @property {boolean} disabled - The disabled state.
 * @property {string} title - The button's title attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} titleAttribute - Internal, map title attribute to the title JS property.
 * @property {string} tooltipText - Set the title property, the title attribute will be used first.
 * @property {string} ariaLabel - The button's aria-label attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} ariaHasPopup - The button's aria-haspopup attribute, that indicates that a popup element can be triggered by the button.
 * @property {string} ariaExpanded - The button's aria-expanded attribute, that indicates whether or not the controlled elements are displayed or hidden.
 * @property {string} ariaPressed - The button's aria-pressed attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} ariaChecked - The button's aria-checked attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} ariaSelected - The button's aria-selected attribute, used in shadow DOM and therefore not as an attribute on moz-button.
 * @property {string} buttonRole - The button's role attribute in shadow DOM.
 * @property {string} iconSrc - Path to the icon that should be displayed in the button.
 * @property {string} hasVisibleLabel - Internal, tracks whether or not the button has a visible label.
 * @property {boolean} attention - Show a dot notification on the button if true.
 * @property {boolean} parentDisabled - When the parent of this component is disabled.
 * @property {string} iconPosition - The icon's position relative to the button label.
 *   Options: start, end.
 * @property {string} menuId - The ID of the menu element controlled by the button.
 * @property {string} popoverTarget - The ID of the popover element controlled by the button. Either a menuId or popoverTarget may be specified but not both.
 * @property {HTMLButtonElement} buttonEl - The internal button element in the shadow DOM.
 * @property {HTMLButtonElement} slotEl - The internal slot element in the shadow DOM.
 * @cssproperty [--button-outer-padding-inline] - Used to set the outer inline padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block] - Used to set the outer block padding of toolbar style buttons.
 * @cssproperty [--button-outer-padding-inline-start] - Used to set the outer inline-start padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-inline-end] - Used to set the outer inline-end padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block-start] - Used to set the outer block-start padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block-end] - Used to set the outer block-end padding of toolbar style buttons
 * @slot default - The button's content, overrides label property.
 * @fires click - The click event.
 */
export class MozButton extends MozLitElement {
  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    label: { type: String, reflect: true, fluent: true },
    type: { type: String, reflect: true },
    size: { type: String, reflect: true },
    disabled: { type: Boolean, reflect: true },
    title: { type: String, mapped: true },
    tooltipText: { type: String, fluent: true },
    ariaLabel: { type: String, mapped: true },
    ariaHasPopup: { type: String, mapped: true },
    ariaExpanded: { type: String, mapped: true },
    ariaPressed: { type: String, mapped: true },
    ariaChecked: { type: String, mapped: true },
    ariaSelected: { type: String, mapped: true },
    buttonRole: { type: String },
    iconSrc: { type: String },
    hasVisibleLabel: { type: Boolean, state: true },
    accessKey: { type: String, mapped: true },
    attention: { type: Boolean },
    iconPosition: { type: String, reflect: true },
    menuId: { type: String, reflect: true },
    parentDisabled: { type: Boolean },
    popoverTarget: { type: String, reflect: true },
  };

  static queries = {
    buttonEl: "#main-button",
    chevronButtonEl: "#chevron-button",
    slotEl: "slot",
    backgroundEl: "#main-button .button-background",
  };

  constructor() {
    super();
    this.type = "default";
    this.size = "default";
    this.disabled = false;
    this.hasVisibleLabel = !!this.label;
    this.attention = false;
    this.iconPosition = "start";
    /** @type {string | undefined} */
    this.iconSrc = undefined;
    this.menuId = "";
    this.parentDisabled = undefined;
    this.popoverTarget = "";
  }

  updated(changedProperties) {
    super.updated(changedProperties);

    if (
      changedProperties.has("menuId") ||
      changedProperties.has("popoverTarget") ||
      changedProperties.has("type")
    ) {
      // If the button has a menuId, hook up a MenuController. If it has a
      // popoverTarget, hook up a PopoverController. If it has both, prefer the
      // menuId.
      this._popoverController?.destroy();
      this._popoverController = null;
      if (this.menuId) {
        this._popoverController = new MenuController(this);
      } else if (this.popoverTarget) {
        this._popoverController = new PopoverController(this);
      }
    }
  }

  get isSplitButton() {
    return this.type === "split";
  }

  get ariaHasPopup() {
    if (this.#ariaHasPopupConsumerValue) {
      return this.#ariaHasPopupConsumerValue;
    }
    return this._popoverController?.ariaHasPopupDefault;
  }

  set ariaHasPopup(value) {
    this.#ariaHasPopupConsumerValue = value;
  }

  // Delegate clicks on host to the button element.
  click() {
    this.performUpdate();
    this.buttonEl?.click();
  }

  checkForLabelText() {
    this.hasVisibleLabel = this.slotEl
      ?.assignedNodes()
      .some(node => node.textContent.trim());
  }

  labelTemplate() {
    if (this.label) {
      return html`<span class="text" .textContent=${this.label}></span>`;
    }
    return html`<slot @slotchange=${this.checkForLabelText}></slot>`;
  }

  iconTemplate(position) {
    if (this.iconSrc && position == this.iconPosition) {
      return html`<img src=${this.iconSrc} role="presentation" />`;
    }
    return null;
  }

  chevronButtonTemplate() {
    if (this.isSplitButton) {
      return html`<button
        id="chevron-button"
        size=${this.size}
        ?disabled=${this.disabled || this.parentDisabled}
        data-l10n-id="moz-button-more-options"
        aria-labelledby="main-button chevron-button"
        aria-expanded=${ifDefined(this.ariaExpanded)}
        aria-haspopup=${ifDefined(this.ariaHasPopup)}
        @click=${e => e.stopPropagation()}
        @mousedown=${e => e.stopPropagation()}
      >
        <span
          class="button-background"
          part="chevron-button"
          type=${this.type}
          size=${this.size}
        >
          <img
            src="chrome://global/skin/icons/arrow-down${this.size === "small"
              ? "-12"
              : ""}.svg"
            role="presentation"
          />
        </span>
      </button>`;
    }
    return null;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-button.css"
      />
      <button
        id="main-button"
        ?disabled=${this.disabled || this.parentDisabled}
        title=${ifDefined(this.title || this.tooltipText)}
        role=${ifDefined(this.buttonRole)}
        aria-label=${ifDefined(this.ariaLabel)}
        aria-expanded=${ifDefined(
          this.isSplitButton ? undefined : this.ariaExpanded
        )}
        aria-haspopup=${ifDefined(
          this.isSplitButton ? undefined : this.ariaHasPopup
        )}
        aria-pressed=${ifDefined(this.ariaPressed)}
        aria-checked=${ifDefined(this.ariaChecked)}
        aria-selected=${ifDefined(this.ariaSelected)}
        accesskey=${ifDefined(this.accessKey)}
      >
        <span
          class=${classMap({
            labelled: this.label || this.hasVisibleLabel,
            "button-background": true,
            badged:
              (this.iconSrc || this.type.includes("icon")) && this.attention,
          })}
          part="button"
          type=${this.type}
          size=${this.size}
        >
          ${this.iconTemplate("start")}
          <label
            is="moz-label"
            shownaccesskey=${ifDefined(this.accessKey)}
            part="moz-button-label"
          >
            ${this.labelTemplate()}
          </label>
          ${this.iconTemplate("end")}
        </span>
      </button>
      ${this.chevronButtonTemplate()}
    `;
  }

  /** @type {string} */
  #ariaHasPopupConsumerValue;
}
customElements.define("moz-button", MozButton);
