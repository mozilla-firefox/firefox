/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, classMap } from "../vendor/lit.all.mjs";
import { MozBoxBase } from "../lit-utils.mjs";
import { GROUP_TYPES } from "chrome://global/content/elements/moz-box-group.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozBoxBase.ftl");

const DIRECTION_RIGHT = "Right";
const DIRECTION_LEFT = "Left";
const NAVIGATION_DIRECTIONS = {
  LTR: {
    FORWARD: DIRECTION_RIGHT,
    BACKWARD: DIRECTION_LEFT,
  },
  RTL: {
    FORWARD: DIRECTION_LEFT,
    BACKWARD: DIRECTION_RIGHT,
  },
};

const NAVIGATION_FORWARD = "forward";
const NAVIGATION_BACKWARD = "backward";

const NAVIGATION_VALUE = {
  [NAVIGATION_FORWARD]: 1,
  [NAVIGATION_BACKWARD]: -1,
};

/**
 * A custom element used for highlighting important information and/or providing
 * context for specific settings.
 *
 * @tagname moz-box-item
 * @property {string} label - Label for the button.
 * @property {string} description - Descriptive text for the button.
 * @property {string} iconSrc - The src for an optional icon shown next to the label.
 * @property {string} supportPage - The name of the SUMO support page to link to.
 * @property {"default"|"medium-icon"|"large-icon"} layout - Layout style for the box content.
 * @slot default - Slot for the box item's content, which overrides label and description.
 * @slot support-link - Slot for custom support link element.
 * @slot description - Slot for custom description content.
 * @slot actions - Slot for the actions positioned at the end of the component container.
 * @slot actions-start - Slot for the actions positioned at the start of the component container.
 */
export default class MozBoxItem extends MozBoxBase {
  #actionEls = [];

  static properties = {
    layout: { type: String, reflect: true },
    supportPage: { type: String, attribute: "support-page" },
    _hasSlottedSupportLink: { type: Boolean, state: true },
    _hasSlottedDescription: { type: Boolean, state: true },
  };

  static queries = {
    defaultSlotEl: "slot:not([name])",
    actionsStartSlotEl: "slot[name=actions-start]",
    actionsSlotEl: "slot[name=actions]",
    handleEl: ".handle",
  };

  constructor() {
    super();
    this.layout = "default";
    this._hasSlottedDescription = false;
    this.addEventListener("keydown", e => this.handleKeydown(e));
  }

  get hasSupportPage() {
    return this.supportPage || this._hasSlottedSupportLink;
  }

  get hasDescription() {
    return this.description || this._hasSlottedDescription;
  }

  /** @param {Event} e */
  checkSlottedSupportLink(e) {
    this._hasSlottedSupportLink = !!e.target?.assignedNodes()?.length;
  }

  /** @param {Event} e */
  checkSlottedDescription(e) {
    this._hasSlottedDescription = !!e.target?.assignedNodes()?.length;
  }

  firstUpdated() {
    this.getActionEls();
  }

  handleKeydown(event) {
    let isHandleEvent = event.originalTarget === this.handleEl;

    // Find which action element the event came from
    let target = isHandleEvent
      ? this.handleEl
      : this.#actionEls.find(el => el.contains(event.target));
    if (!target) {
      return;
    }

    let directions = this.getNavigationDirections();
    switch (event.key) {
      case directions.FORWARD:
      case `Arrow${directions.FORWARD}`: {
        this.navigate(target, NAVIGATION_FORWARD);
        break;
      }
      case directions.BACKWARD:
      case `Arrow${directions.BACKWARD}`: {
        this.navigate(target, NAVIGATION_BACKWARD);
        break;
      }
    }
  }

  /**
   * Navigate between action elements, skipping disabled elements.
   *
   * @param {HTMLElement} target - The currently focused action element
   * @param {NAVIGATION_FORWARD | NAVIGATION_BACKWARD} direction - The navigation direction
   */
  navigate(target, direction) {
    let actionEls = this.#actionEls;
    let currentIndex = actionEls.indexOf(target);
    let step = NAVIGATION_VALUE[direction];
    for (
      let nextIndex = currentIndex + step;
      nextIndex >= 0 && nextIndex < actionEls.length;
      nextIndex += step
    ) {
      let nextItem = actionEls[nextIndex];
      nextItem.focus();
      if (nextItem.contains(this.getRootNode().activeElement)) {
        // If the next item became focused then we've navigated. This skips
        // disabled elements or elements that can never receive focus.
        return;
      }
    }
  }

  getNavigationDirections() {
    if (this.isDocumentRTL) {
      return NAVIGATION_DIRECTIONS.RTL;
    }
    return NAVIGATION_DIRECTIONS.LTR;
  }

  get isDocumentRTL() {
    if (typeof Services !== "undefined") {
      return Services.locale.isAppLocaleRTL;
    }
    return document.dir === "rtl";
  }

  get isDraggable() {
    const reorderableParent = this.closest("moz-box-group");
    return (
      reorderableParent?.type == GROUP_TYPES.reorderable &&
      this.slot != "header" &&
      this.slot != "footer" &&
      !this.slot.includes("static")
    );
  }

  focus(event) {
    if (event?.key == "Up" || event?.key == "ArrowUp") {
      let actionEls = this.actionsSlotEl.assignedElements();
      let lastActions = actionEls.length
        ? actionEls
        : this.actionsStartSlotEl?.assignedElements();
      let lastAction = lastActions?.[lastActions.length - 1] ?? this.handleEl;
      lastAction?.focus();
    } else {
      let firstAction =
        this.handleEl ??
        this.actionsStartSlotEl?.assignedElements()?.[0] ??
        this.actionsSlotEl.assignedElements()?.[0];
      firstAction?.focus();
    }
  }

  getActionEls() {
    let handleEl = this.handleEl ? [this.handleEl] : [];
    let startActions = this.actionsStartSlotEl?.assignedElements() ?? [];
    let endActions = this.actionsSlotEl.assignedElements();
    this.#actionEls = [...handleEl, ...startActions, ...endActions];
  }

  stylesTemplate() {
    return html`${super.stylesTemplate()}
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-box-item.css"
      />`;
  }

  slotTemplate(name) {
    return html`
      <span
        role="group"
        aria-labelledby="label"
        aria-describedby="description"
        class="actions"
        @slotchange=${this.getActionEls}
      >
        <slot name=${name}></slot>
      </span>
    `;
  }

  descriptionTemplate() {
    if (!this.description) {
      return html`<slot
        class="description text-deemphasized"
        id="description"
        name="description"
        @slotchange=${this.checkSlottedDescription}
      ></slot>`;
    }
    return html`<span class="description text-deemphasized" id="description"
      >${this.description}</span
    >`;
  }

  textTemplate() {
    return html`<div
      class=${classMap({
        "text-content": true,
        "has-icon": this.iconSrc,
        "has-description": this.hasDescription,
        "has-support-page": this.hasSupportPage,
      })}
    >
      ${this.iconTemplate()}
      <span class="label-wrapper">
        ${this.labelTemplate()}${!this.hasDescription
          ? this.supportPageTemplate()
          : ""}
      </span>
      <span class="description-wrapper">
        ${this.descriptionTemplate()}${this.hasDescription
          ? this.supportPageTemplate()
          : ""}
      </span>
    </div>`;
  }

  supportPageTemplate() {
    if (this.supportPage) {
      return html`<a
        class="support-page"
        is="moz-support-link"
        support-page=${this.supportPage}
        part="support-link"
        aria-describedby=${this.description ? "description" : "label"}
      ></a>`;
    }
    return html`<slot
      name="support-link"
      class="support-page"
      @slotchange=${this.checkSlottedSupportLink}
    ></slot>`;
  }

  handleTemplate() {
    if (!this.isDraggable) {
      return "";
    }
    return html`<span
      tabindex="0"
      class="handle"
      data-l10n-id=${this.label
        ? "moz-box-item-reorder-handle-named"
        : "moz-box-item-reorder-handle"}
      data-l10n-args=${this.label
        ? JSON.stringify({ item: this.label })
        : undefined}
    ></span>`;
  }

  render() {
    return html`
      ${this.stylesTemplate()}
      <div class="box-container">
        ${this.handleTemplate()} ${this.slotTemplate("actions-start")}
        <div class="box-content">
          ${this.label ? this.textTemplate() : html`<slot></slot>`}
        </div>
        ${this.slotTemplate("actions")}
      </div>
    `;
  }
}
customElements.define("moz-box-item", MozBoxItem);
