/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-page-nav.tagmap.d.ts" />

import {
  html,
  when,
  ifDefined,
} from "chrome://global/content/vendor/lit.all.mjs";
import {
  MozLitElement,
  hasModifierKey,
} from "chrome://global/content/lit-utils.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-support-link.mjs";

/**
 * @typedef {("mobile"|"default")} PageNavType
 */

/**
 * A grouping of navigation buttons that is displayed at the page level,
 * intended to change the selected view, provide a heading, and have links
 * to external resources.
 *
 * @tagname moz-page-nav
 * @slot [default] - Used to append moz-page-nav-button elements to the navigation.
 * @slot [subheading] - Used to append page specific search input or notification to the nav.
 */
export class MozPageNav extends MozLitElement {
  static properties = {
    currentView: { type: String },
    heading: { type: String, fluent: true },
    type: { type: String, reflect: true },
    allowNoSelection: { type: Boolean },
    pageNavButtons: { type: Array, state: true },
    secondaryNavButtons: { type: Array, state: true },
  };

  static queries = {
    headingEl: "#page-nav-heading",
    primaryNavGroupSlot: ".primary-nav-group slot",
    secondaryNavGroupSlot: "#secondary-nav-group slot",
  };

  constructor() {
    super();

    /**
     * The currently selected view.
     *
     * @type {string | undefined}
     */
    this.currentView = undefined;

    /**
     * A heading displayed at the top of the navigation.
     *
     * @type {string | undefined}
     */
    this.heading = undefined;

    /**
     * The type of the component.
     *
     * @type {PageNavType}
     */
    this.type = "default";

    /**
     * Allows a currentView that matches no button to persist instead of
     * selecting the first visible button.
     *
     * @type {boolean}
     */
    this.allowNoSelection = false;

    /**
     * The array of all page nav buttons.
     *
     * @type {MozPageNavButton[]}
     */
    this.pageNavButtons = [];

    /**
     * The array of all secondary nav buttons.
     *
     * @type {MozPageNavButton[]}
     */
    this.secondaryNavButtons = [];
  }

  /** @internal */
  get visiblePageNavButtons() {
    return this.pageNavButtons.filter(this.checkElementVisibility);
  }

  /**
   * @param {HTMLSlotElement} el
   */
  getSlottedChildren(el) {
    return el
      ?.assignedElements()
      .filter(element => element instanceof MozPageNavButton);
  }

  /**
   * @param {MozPageNavButton} element
   */
  checkElementVisibility(element) {
    let computedStyles = window.getComputedStyle(element);
    return (
      computedStyles &&
      !element.hidden &&
      computedStyles.getPropertyValue("display") !== "none" &&
      computedStyles.getPropertyValue("visibility") !== "hidden" &&
      computedStyles.getPropertyValue("opacity") != "0"
    );
  }

  onChangeView(e) {
    this.currentView = e.target.view;
  }

  handleFocus(e) {
    if (hasModifierKey(e)) {
      return;
    }
    let isRTL = this.isDocumentRTL;
    let nextKey = isRTL ? "ArrowLeft" : "ArrowRight";
    let previousKey = isRTL ? "ArrowRight" : "ArrowLeft";
    if (e.key == "ArrowDown" || e.key == nextKey) {
      e.preventDefault();
      this.focusNextView();
    } else if (e.key == "ArrowUp" || e.key == previousKey) {
      e.preventDefault();
      this.focusPreviousView();
    }
  }

  focusPreviousView() {
    let pageNavButtons = this.visiblePageNavButtons;
    let currentIndex = pageNavButtons.findIndex(b => b.selected);
    let prev = pageNavButtons[currentIndex - 1];
    if (prev) {
      prev.activate({ focusVisible: true });
    }
  }

  focusNextView() {
    let pageNavButtons = this.visiblePageNavButtons;
    let currentIndex = pageNavButtons.findIndex(b => b.selected);
    let next = pageNavButtons[currentIndex + 1];
    if (next) {
      next.activate({ focusVisible: true });
    }
  }

  /**
   * @param {Event & { target: HTMLSlotElement}} event
   */
  onPrimaryNavChange(event) {
    this.pageNavButtons = this.getSlottedChildren(event.target);
    this.updateNavButtonsState();
  }

  /**
   * @param {Event & { target: HTMLSlotElement}} event
   */
  onSecondaryNavChange(event) {
    this.secondaryNavButtons = this.getSlottedChildren(event.target);
    let secondaryNavElements = event.target.assignedElements();
    this.hasSecondaryNav = !!secondaryNavElements.length;
  }

  updated() {
    this.updateNavButtonsState();
  }

  updateNavButtonsState() {
    let isViewSelected = false;
    for (let button of this.pageNavButtons) {
      button.selected = button.view == this.currentView;
    }
    let visibleButtons = this.visiblePageNavButtons;
    for (let button of visibleButtons) {
      isViewSelected = isViewSelected || button.selected;
    }
    if (
      !isViewSelected &&
      visibleButtons.length &&
      (!this.currentView || !this.allowNoSelection)
    ) {
      visibleButtons[0].activate();
    }
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-page-nav.css"
      />
      <div class="page-nav-heading-wrapper">
        ${this.type === "mobile"
          ? html`<moz-button
              type="icon ghost"
              aria-label="Open Menu"
              aria-expanded="false"
              iconsrc="chrome://browser/skin/menu.svg"
            >
            </moz-button>`
          : ""}
        <div class="logo"></div>
        <h1 class="page-nav-heading" id="page-nav-heading">${this.heading}</h1>
      </div>
      <slot name="subheading"></slot>
      <nav>
        <div
          class="primary-nav-group"
          role="tablist"
          aria-orientation="vertical"
          aria-labelledby="page-nav-heading"
        >
          <slot
            @change-view=${this.onChangeView}
            @keydown=${this.handleFocus}
            @slotchange=${this.onPrimaryNavChange}
          ></slot>
        </div>
        ${when(this.hasSecondaryNav, () => html`<hr />`)}
        <div id="secondary-nav-group" role="group">
          <slot
            name="secondary-nav"
            @slotchange=${this.onSecondaryNavChange}
          ></slot>
        </div>
      </nav>
    `;
  }
}
customElements.define("moz-page-nav", MozPageNav);

/**
 * A navigation button intended to change the selected view within a page.
 *
 * @tagname moz-page-nav-button
 * @slot [default] - Used to append the l10n string to the button.
 */
export class MozPageNavButton extends MozLitElement {
  static properties = {
    iconSrc: { type: String, reflect: true },
    href: { type: String },
    selected: { type: Boolean },
    supportPage: { type: String, attribute: "support-page" },
    title: { type: String, mapped: true },
  };

  constructor() {
    super();

    /**
     * The chrome:// url for the icon used for the button.
     *
     * @type {string | undefined}
     */
    this.iconSrc = undefined;

    /**
     * The url for an external link if not a support page URL.
     *
     * @type {string | undefined}
     */
    this.href = undefined;

    /**
     * Whether or not the button is currently selected.
     *
     * @type {boolean}
     */
    this.selected = false;

    /**
     * The short name for the support page a secondary link launches.
     *
     * @type {string | undefined}
     */
    this.supportPage = undefined;

    /**
     * The title used for the button or link, mapped onto the inner element.
     *
     * @type {string | undefined}
     */
    this.title = undefined;
  }

  connectedCallback() {
    super.connectedCallback();
    this.setAttribute("role", "none");
  }

  static queries = {
    buttonEl: "button",
    linkEl: "a",
  };

  get view() {
    return this.getAttribute("view");
  }

  activate(options = {}) {
    let focusVisible = options.focusVisible ?? false;

    this.dispatchEvent(
      new CustomEvent("change-view", {
        bubbles: true,
        composed: true,
      })
    );
    this.buttonEl?.focus({ focusVisible });
  }

  itemTemplate() {
    if (this.href || this.supportPage) {
      return this.linkTemplate();
    }
    return this.buttonTemplate();
  }

  buttonTemplate() {
    return html`
      <button
        aria-selected=${this.selected}
        tabindex=${this.selected ? 0 : -1}
        role="tab"
        ?selected=${this.selected}
        title=${ifDefined(this.title)}
        @click=${this.activate}
      >
        ${this.innerContentTemplate()}
      </button>
    `;
  }

  linkTemplate() {
    if (this.supportPage) {
      return html`
        <a
          is="moz-support-link"
          class="moz-page-nav-link"
          support-page=${this.supportPage}
          title=${ifDefined(this.title)}
        >
          ${this.innerContentTemplate()}
        </a>
      `;
    }
    return html`
      <a
        href=${this.href}
        class="moz-page-nav-link"
        target="_blank"
        title=${ifDefined(this.title)}
      >
        ${this.innerContentTemplate()}
      </a>
    `;
  }

  innerContentTemplate() {
    return html`
      ${this.iconSrc
        ? html`<img
            class="page-nav-icon"
            src=${this.iconSrc}
            role="presentation"
          />`
        : ""}
      <slot></slot>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-page-nav-button.css"
      />
      ${this.itemTemplate()}
    `;
  }
}
customElements.define("moz-page-nav-button", MozPageNavButton);
