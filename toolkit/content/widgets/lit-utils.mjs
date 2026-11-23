/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  LitElement,
  html,
  ifDefined,
  nothing,
  classMap,
} from "chrome://global/content/vendor/lit.all.mjs";

/** @import { TemplateResult } from "chrome://global/content/vendor/lit.all.mjs" */

/**
 * Helper for our replacement of @query. Used with `static queries` property.
 *
 * https://github.com/lit/lit/blob/main/packages/reactive-element/src/decorators/query.ts
 */
function query(el, selector) {
  return () => el.renderRoot.querySelector(selector);
}

/**
 * Helper for our replacement of @queryAll. Used with `static queries` property.
 *
 * https://github.com/lit/lit/blob/main/packages/reactive-element/src/decorators/query-all.ts
 */
function queryAll(el, selector) {
  return () => el.renderRoot.querySelectorAll(selector);
}

/**
 * Checks whether a modifier key is held during a keyboard event. Arrow key
 * navigation ignores these, since modified arrows belong to browser, OS, and
 * assistive technology shortcuts, e.g. Alt+Left or Cmd+Option+Left.
 *
 * @param {KeyboardEvent} event
 * @returns {boolean}
 */
export function hasModifierKey(event) {
  return event.altKey || event.ctrlKey || event.metaKey || event.shiftKey;
}

/**
 * MozLitElement provides extensions to the lit-provided LitElement class.
 *
 * ---------
 *
 * `@query` support (define a getter for a querySelector):
 *
 * static get queries() {
 *   return {
 *     propertyName: ".aNormal .cssSelector",
 *     anotherName: { all: ".selectorFor .querySelectorAll" },
 *   };
 * }
 *
 * This example would add properties that would be written like this without
 * using `queries`:
 *
 * get propertyName() {
 *   return this.renderRoot?.querySelector(".aNormal .cssSelector");
 * }
 *
 * get anotherName() {
 *   return this.renderRoot?.querySelectorAll(".selectorFor .querySelectorAll");
 * }
 * ---------
 *
 * Automatic Fluent support for shadow DOM.
 *
 * Fluent requires that a shadowRoot be connected before it can use Fluent.
 * Shadow roots will get connected automatically.
 *
 * ---------
 *
 * Automatic Fluent support for localized Reactive Properties
 *
 * When a Reactive Property can be set by fluent, set `fluent: true` in its
 * property definition and it will automatically be added to the data-l10n-attrs
 * attribute so that fluent will allow setting the attribute.
 *
 * ---------
 *
 * Mapped properties support (moving a standard attribute to rendered content)
 *
 * When you want to accept a standard attribute such as accesskey, title or
 * aria-label at the component level but it should really be set on a child
 * element then you can set the `mapped: true` option in your property
 * definition and the attribute will be removed from the host when it is set.
 * Note that the attribute can not be unset once it is set.
 *
 * ---------
 *
 * Test helper for sending events after a change: `dispatchOnUpdateComplete`
 *
 * When some async stuff is going on and you want to wait for it in a test, you
 * can use `this.dispatchOnUpdateComplete(myEvent)` and have the test wait on
 * your event.
 *
 * The component will then wait for your reactive property change to take effect
 * and dispatch the desired event.
 *
 * Example:
 *
 * async onClick() {
 *   let response = await this.getServerResponse(this.data);
 *   // Show the response status to the user.
 *   this.responseStatus = response.status;
 *   this.dispatchOnUpdateComplete(
 *     new CustomEvent("status-shown")
 *   );
 * }
 *
 * add_task(async testButton() {
 *   let button = this.setupAndGetButton();
 *   button.click();
 *   await BrowserTestUtils.waitForEvent(button, "status-shown");
 * });
 */
export class MozLitElement extends LitElement {
  #l10nObj;
  #l10nRootConnected = false;

  static createProperty(attrName, options) {
    if (options.mapped) {
      let domAttrPropertyName = `${attrName}Attribute`;
      let domAttrName = options.attribute ?? attrName.toLowerCase();
      if (attrName.startsWith("aria")) {
        domAttrName = domAttrName.replace("aria", "aria-");
      }
      // Give this class its own array so we don't mutate the one we inherit
      // from an ancestor, which would leak our attributes into every sibling.
      if (!Object.hasOwn(this, "mappedAttributes")) {
        this.mappedAttributes = [...(this.mappedAttributes ?? [])];
      }
      this.mappedAttributes.push([attrName, domAttrPropertyName]);
      options.state = true;
      super.createProperty(domAttrPropertyName, {
        type: String,
        attribute: domAttrName,
        reflect: true,
      });
    }
    if (options.fluent) {
      // Same as above: give this class its own array.
      if (!Object.hasOwn(this, "fluentProperties")) {
        this.fluentProperties = [...(this.fluentProperties ?? [])];
      }
      this.fluentProperties.push(options.attribute || attrName.toLowerCase());
    }
    return super.createProperty(attrName, options);
  }

  constructor() {
    super();
    let { queries } = this.constructor;
    if (queries) {
      for (let [selectorName, selector] of Object.entries(queries)) {
        if (selector.all) {
          Object.defineProperty(this, selectorName, {
            get: queryAll(this, selector.all),
          });
        } else {
          Object.defineProperty(this, selectorName, {
            get: query(this, selector),
          });
        }
      }
    }
  }

  connectedCallback() {
    super.connectedCallback();
    if (
      this.renderRoot == this.shadowRoot &&
      !this.#l10nRootConnected &&
      this.#l10n
    ) {
      this.#l10n.connectRoot(this.renderRoot);
      this.#l10nRootConnected = true;

      if (this.constructor.fluentProperties?.length) {
        let { fluentProperties } = this.constructor;
        if (this.dataset.l10nAttrs) {
          // Not worrying about duplication since this may happen a lot and we
          // could avoid it by not providing the duplicates manually.
          // Copy the fluentProperties since they're stored on our class.
          fluentProperties = fluentProperties.concat(this.dataset.l10nAttrs);
        }
        this.dataset.l10nAttrs = fluentProperties.join(",");
        if (this.dataset.l10nId) {
          this.#l10n.translateElements([this]);
        }
      }
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (
      this.renderRoot == this.shadowRoot &&
      this.#l10nRootConnected &&
      this.#l10n
    ) {
      this.#l10n.disconnectRoot(this.renderRoot);
      this.#l10nRootConnected = false;
    }
  }

  willUpdate(changes) {
    this.#handleMappedAttributeChange(changes);
  }

  #handleMappedAttributeChange(changes) {
    if (!this.constructor.mappedAttributes) {
      return;
    }
    for (let [attrName, domAttrName] of this.constructor.mappedAttributes) {
      if (changes.has(domAttrName)) {
        this[attrName] = this[domAttrName];
        this[domAttrName] = null;
      }
    }
  }

  get #l10n() {
    if (!this.#l10nObj) {
      this.#l10nObj =
        (window.Cu?.isInAutomation && window.mockL10n) || document.l10n;
    }
    return this.#l10nObj;
  }

  /**
   * Whether the app locale is RTL. Falls back to the document direction where
   * Services is unavailable.
   *
   * @returns {boolean}
   */
  get isDocumentRTL() {
    if (typeof Services !== "undefined") {
      return Services.locale.isAppLocaleRTL;
    }
    return document.dir === "rtl";
  }

  async dispatchOnUpdateComplete(event) {
    await this.updateComplete;
    this.dispatchEvent(event);
  }

  update() {
    super.update();
    if (this.#l10n) {
      this.#l10n.translateFragment(this.renderRoot);
    }
  }
}

/**
 * A base input element. Provides common layout and properties for our design
 * system input elements.
 *
 * Subclasses must implement the inputTemplate() method which returns the input
 * template for this specific input element with its id set to "input".
 */
export class MozBaseInputElement extends MozLitElement {
  static formAssociated = true;
  #internals;
  #hasSlottedContent = new Map();

  static properties = {
    label: { type: String, fluent: true },
    name: { type: String },
    value: { type: String },
    iconSrc: { type: String },
    disabled: { type: Boolean },
    description: { type: String, fluent: true },
    supportPage: { type: String, attribute: "support-page" },
    accessKey: { type: String, mapped: true, fluent: true },
    parentDisabled: { type: Boolean, state: true },
    ariaLabel: { type: String, mapped: true },
    ariaDescription: { type: String, mapped: true },
    title: { type: String, mapped: true },
    inputLayout: { type: String, reflect: true, attribute: "inputlayout" },
  };
  /** @type {"inline" | "block" | "inline-end"} */
  static inputLayout = "inline";
  /** @type {keyof MozBaseInputElement} */
  static activatedProperty = null;

  constructor() {
    super();

    /**
     * The text of the label element.
     *
     * @type {string | undefined}
     */
    this.label = undefined;

    /**
     * The name of the input control.
     *
     * @type {string | undefined}
     */
    this.name = undefined;

    /**
     * The value of the input control.
     *
     * @type {string | undefined}
     */
    this.value = undefined;

    /**
     * The src for an optional icon.
     *
     * @type {string | undefined}
     */
    this.iconSrc = undefined;

    /**
     * The disabled state of the input control.
     *
     * @type {boolean}
     */
    this.disabled = false;

    /**
     * The text for the description element that helps describe the input
     * control.
     *
     * @type {string | undefined}
     */
    this.description = undefined;

    /**
     * Name of the SUMO support page to link to.
     *
     * @type {string | undefined}
     */
    this.supportPage = undefined;

    /**
     * Access key for the inner focusable control.
     *
     * @type {string | undefined}
     */
    this.accessKey = undefined;

    /**
     * True when a parent input is disabled or unchecked, so this nested
     * element can be disabled along with it.
     *
     * @type {boolean | undefined}
     */
    this.parentDisabled = undefined;

    /**
     * The aria-label text when there is no visible label.
     *
     * @type {string | undefined}
     */
    this.ariaLabel = undefined;

    /**
     * The aria-description text when there is no visible description.
     *
     * @type {string | undefined}
     */
    this.ariaDescription = undefined;

    /**
     * The title attribute, mapped onto the inner focusable control.
     *
     * @type {string | undefined}
     */
    this.title = undefined;

    /**
     * Placement of the input relative to its label.
     *
     * @type {"inline" | "block" | "inline-end"}
     */
    this.inputLayout = /** @type {typeof MozBaseInputElement} */ (
      this.constructor
    ).inputLayout;

    this.#internals = this.attachInternals();
  }

  get form() {
    return this.#internals.form;
  }

  /**
   * @param {string} value The current value of the element.
   */
  setFormValue(value) {
    this.#internals.setFormValue(value);
  }

  formResetCallback() {
    this.value = this.defaultValue;
  }

  connectedCallback() {
    super.connectedCallback();
    /** @type {string} val */
    let val = this.getAttribute("value") || this.value;
    this.defaultValue = val;
    this.value = val;
    this.#internals.setFormValue(this.value || null);
  }

  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);
    this.#updateInternalState(this.description, "description");
    this.#updateInternalState(this.supportPage, "support-link");
    this.#updateInternalState(this.label, "label");

    if (changedProperties.has("value")) {
      this.setFormValue(this.value);
    }
    let activatedProperty = /** @type {typeof MozBaseInputElement} */ (
      this.constructor
    ).activatedProperty;
    if (
      (activatedProperty && changedProperties.has(activatedProperty)) ||
      changedProperties.has("disabled") ||
      changedProperties.has("parentDisabled")
    ) {
      this.updateNestedElements();
    }
  }

  #updateInternalState(propVal, stateKey) {
    let internalStateKey = `has-${stateKey}`;
    let hasValue = !!(propVal || this.#hasSlottedContent.get(stateKey));

    if (this.#internals.states?.has(internalStateKey) == hasValue) {
      return;
    }

    if (hasValue) {
      this.#internals.states.add(internalStateKey);
    } else {
      this.#internals.states.delete(internalStateKey);
    }
  }

  updateNestedElements() {
    if (this.isDisabled) {
      this.#internals.states.add("disabled");
    } else {
      this.#internals.states.delete("disabled");
    }
    for (let el of this.nestedEls) {
      if ("parentDisabled" in el) {
        el.parentDisabled =
          this.parentDisabled ||
          !this[this.constructor.activatedProperty] ||
          this.disabled;
      }
    }
  }

  get inputEl() {
    return this.renderRoot.getElementById("input");
  }

  get labelEl() {
    return this.renderRoot.querySelector("label");
  }

  get icon() {
    return this.renderRoot.querySelector(".icon");
  }

  get descriptionEl() {
    return this.renderRoot.getElementById("description");
  }

  get nestedEls() {
    return this.renderRoot.querySelector(".nested")?.assignedElements() ?? [];
  }

  get hasDescription() {
    return this.#internals.states.has("has-description");
  }

  get hasSupportLink() {
    return this.#internals.states.has("has-support-link");
  }

  get hasLabel() {
    return this.#internals.states.has("has-label");
  }

  get isDisabled() {
    return !!(this.disabled || this.parentDisabled);
  }

  click() {
    this.inputEl.click();
  }

  focus() {
    this.inputEl.focus();
  }

  select() {
    this.inputEl.select();
  }

  blur() {
    this.inputEl.blur();
  }

  /**
   * Dispatches an event from the host element so that outside
   * listeners can react to these events
   *
   * @param {Event} event
   * @memberof MozBaseInputElement
   */
  redispatchEvent(event) {
    let { bubbles, cancelable, composed, type } = event;
    let newEvent = new Event(type, {
      bubbles,
      cancelable,
      composed,
    });
    this.dispatchEvent(newEvent);
  }

  inputTemplate() {
    throw new Error(
      "inputTemplate() must be implemented and provide the input element"
    );
  }

  inputStylesTemplate() {
    return nothing;
  }

  /**
   * Template for optional required indicator, rendered next to label.
   * Subclasses that support required state should override this.
   *
   * @returns {TemplateResult | string}
   */
  requiredIndicatorTemplate() {
    return "";
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-input-common.css"
      />
      ${this.inputStylesTemplate()}
      <div class="content-wrapper">
        <span class="label-wrapper">
          <label
            is="moz-label"
            id="label"
            part="label"
            for="input"
            shownaccesskey=${ifDefined(this.accessKey)}
            >${this.inputLayout === "inline"
              ? this.inputTemplate()
              : ""}${this.labelTemplate()}</label
          >${this.hasDescription ? "" : this.supportLinkTemplate()}
          ${this.descriptionTemplate()}
        </span>
        ${this.inputLayout !== "inline" ? this.inputTemplate() : ""}
      </div>
      ${this.nestedFieldsTemplate()}
    `;
  }

  labelTemplate() {
    if (!this.label) {
      return "";
    }
    let labelEl;
    let headingLevel = this.getAttribute("headinglevel");
    if (headingLevel == "3" || headingLevel == "4") {
      // Undocumented hack for AI controls, do not use, it WILL be removed. (bug 2012250)
      // Configs set headinglevel: 3; the SettingElement SRD bump can promote
      // that to 4 in SRD mode, so both values render h3 here.
      labelEl = html`<h3
        class="text text-box-trim-start"
        .textContent=${this.label}
      ></h3>`;
    } else {
      labelEl = html`<span class="text" .textContent=${this.label}></span>`;
    }
    return html`<span class="text-container"
      >${this.iconTemplate()}${labelEl}${this.requiredIndicatorTemplate()}</span
    >`;
  }

  descriptionTemplate() {
    return html`
      <div class="description text-deemphasized">
        <span id="description" class="description-text">
          ${this.description ??
          html`<slot
            name="description"
            @slotchange=${this.onSlotchange}
          ></slot>`}</span
        >${this.hasDescription ? this.supportLinkTemplate() : ""}
      </div>
    `;
  }

  iconTemplate() {
    if (this.iconSrc) {
      return html`<img src=${this.iconSrc} role="presentation" class="icon" />`;
    }
    return "";
  }

  supportLinkTemplate() {
    if (this.supportPage) {
      return html`<a
        is="moz-support-link"
        support-page=${this.supportPage}
        part="support-link"
        aria-describedby="label description"
      ></a>`;
    }
    return html`<slot
      name="support-link"
      @slotchange=${this.onSlotchange}
    ></slot>`;
  }

  nestedFieldsTemplate() {
    if (this.constructor.activatedProperty) {
      return html`<slot
        name="nested"
        class="nested"
        @slotchange=${this.updateNestedElements}
      ></slot>`;
    }
    return "";
  }

  onSlotchange(e) {
    let propName = e.target.name;
    let hasSlottedContent = e.target
      .assignedNodes()
      .some(
        node => node.textContent.trim() || node.getAttribute("data-l10n-id")
      );

    if (hasSlottedContent == this.#hasSlottedContent.get(propName)) {
      return;
    }

    this.#hasSlottedContent.set(propName, hasSlottedContent);
    this.requestUpdate();
  }
}

/**
 * Base class for moz-box-* elements providing common properties and templates.
 */
export class MozBoxBase extends MozLitElement {
  static properties = {
    label: { type: String, fluent: true },
    description: { type: String, fluent: true },
    iconSrc: { type: String },
  };

  constructor() {
    super();

    /**
     * The text for the label element.
     *
     * @type {string}
     */
    this.label = "";

    /**
     * The text for the description element.
     *
     * @type {string}
     */
    this.description = "";

    /**
     * The src for an optional icon.
     *
     * @type {string}
     */
    this.iconSrc = "";
  }

  get labelEl() {
    return this.renderRoot.querySelector(".label");
  }

  get descriptionEl() {
    return this.renderRoot.querySelector(".description");
  }

  get iconEl() {
    return this.renderRoot.querySelector(".icon");
  }

  stylesTemplate() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-box-common.css"
      />
      <link
        rel="stylesheet"
        href="chrome://global/skin/design-system/text-and-typography.css"
      />
    `;
  }

  textTemplate() {
    return html`<div
      class=${classMap({
        "text-content": true,
        "has-icon": this.iconSrc,
        "has-description": this.description,
      })}
    >
      ${this.iconTemplate()}${this.labelTemplate()}${this.descriptionTemplate()}
    </div>`;
  }

  labelTemplate() {
    if (!this.label) {
      return "";
    }
    return html`<span class="label" id="label">${this.label}</span>`;
  }

  iconTemplate() {
    if (!this.iconSrc) {
      return "";
    }
    return html`<img src=${this.iconSrc} role="presentation" class="icon" />`;
  }

  descriptionTemplate() {
    if (!this.description) {
      return "";
    }
    return html`<span class="description text-deemphasized" id="description">
      ${this.description}
    </span>`;
  }
}
