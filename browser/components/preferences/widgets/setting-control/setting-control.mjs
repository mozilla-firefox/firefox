/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  createRef,
  html,
  ifDefined,
  literal,
  ref,
  repeat,
  staticHtml,
  unsafeStatic,
} from "chrome://global/content/vendor/lit.all.mjs";
import {
  SettingElement,
  spread,
} from "chrome://browser/content/preferences/widgets/setting-element.mjs";
import MozInputFolder from "chrome://global/content/elements/moz-input-folder.mjs";

/** @import { LitElement, Ref, TemplateResult } from "chrome://global/content/vendor/lit.all.mjs" */
/** @import { SettingElementConfig } from "chrome://browser/content/preferences/widgets/setting-element.mjs" */
/** @import { Setting } from "chrome://global/content/preferences/Setting.mjs" */

/**
 * @typedef {object} SettingNestedConfig
 * @property {SettingControlConfig[]} [items] Additional nested SettingControls to render.
 * @property {SettingOptionConfig[]} [options]
 * Additional nested plain elements to render (may have SettingControls nested within them, though).
 */

/**
 * @typedef {object} SettingOptionConfigExtensions
 * @property {string} [control]
 * The element tag to render, default assumed based on parent control.
 * @property {any} [value] A value to set on the option.
 * @property {boolean} [disabled] If the option should be disabled.
 * @property {boolean} [hidden] If the option should be hidden.
 * @property {any} [key] A key to identify this option.
 */

/**
 * @typedef {object} SettingControlConfigExtensions
 * @property {string} id
 * The ID for the Setting, also set in the DOM unless overridden with controlAttrs.id
 * @property {string} [control] The element to render, default to "moz-checkbox".
 * @property {string} [controllingExtensionInfo]
 * ExtensionSettingStore id for checking if a setting is controlled by an extension.
 */

/**
 * @typedef {SettingOptionConfigExtensions & SettingElementConfig & SettingNestedConfig} SettingOptionConfig
 * @typedef {SettingControlConfigExtensions & SettingElementConfig & SettingNestedConfig} SettingControlConfig
 * @typedef {{ control: SettingControl } & HTMLElement} SettingControlChild
 */

/**
 * @template T=Event
 * @typedef {T & { target: SettingControlChild }} SettingControlEvent
 * SettingControlEvent simplifies the types in this file, but causes issues when
 * doing more involved work when used in Setting.mjs. When casting the
 * `event.target` to a more specific type like MozButton (or even
 * HTMLButtonElement) it gets flagged as being too different from SettingControlChild.
 */

/**
 * Mapping of parent control tag names to the literal tag name for their
 * expected children. eg. "moz-radio-group"->literal`moz-radio`.
 */
const KNOWN_OPTIONS = new Map([
  ["moz-radio-group", literal`moz-radio`],
  ["moz-select", literal`moz-option`],
  ["moz-visual-picker", literal`moz-visual-picker-item`],
]);

/**
 * Mapping of parent control tag names to the expected slot for their children.
 * If there's no entry here for a control then it's expected that its children
 * should go in the default slot.
 *
 * @type Map<string, string>
 */
const ITEM_SLOT_BY_PARENT = new Map([
  ["moz-checkbox", "nested"],
  ["moz-input-email", "nested"],
  ["moz-input-folder", "nested"],
  ["moz-input-number", "nested"],
  ["moz-input-password", "nested"],
  ["moz-input-search", "nested"],
  ["moz-input-tel", "nested"],
  ["moz-input-text", "nested"],
  ["moz-input-url", "nested"],
  ["moz-radio", "nested"],
  ["moz-radio-group", "nested"],
  // NOTE: moz-select does not support the nested slot.
  ["moz-toggle", "nested"],
]);

export class SettingNotDefinedError extends Error {
  /** @param {string} settingId */
  constructor(settingId) {
    super(
      `No Setting with id "${settingId}". Did you register it with Preferences.addSetting()?`
    );
    this.name = "SettingNotDefinedError";
    this.settingId = settingId;
  }
}

export class SettingControl extends SettingElement {
  static SettingNotDefinedError = SettingNotDefinedError;
  static properties = {
    setting: { type: Object },
    config: { type: Object },
    value: {},
    parentDisabled: { type: Boolean },
    tabIndex: { type: Number, reflect: true },
    showEnableExtensionMessage: { type: Boolean, state: true },
    isDisablingExtension: { type: Boolean, state: true },
  };

  /**
   * @type {Setting | undefined}
   */
  #lastSetting;

  constructor() {
    super();
    /** @type {Ref<LitElement>} */
    this.controlRef = createRef();

    /** @type {Ref<LitElement>} */
    this.controlledMessageBarRef = createRef();

    /** @type {Ref<LitElement>} */
    this.enableMessageBarRef = createRef();

    /**
     * @type {Preferences['getSetting'] | undefined}
     */
    this.getSetting = undefined;

    /**
     * @type {Setting | undefined}
     */
    this.setting = undefined;

    /**
     * @type {SettingControlConfig | undefined}
     */
    this.config = undefined;

    /**
     * @type {boolean | undefined}
     */
    this.parentDisabled = undefined;

    /**
     * @type {boolean}
     */
    this.showEnableExtensionMessage = false;

    /**
     * @type {boolean}
     */
    this.isDisablingExtension = false;
  }

  createRenderRoot() {
    return this;
  }

  focus() {
    this.controlEl.focus();
  }

  get controlEl() {
    return this.controlRef.value;
  }

  get disabled() {
    return (
      this.setting?.disabled ||
      this.setting?.locked ||
      this.isDisabledByExtension()
    );
  }

  async getUpdateComplete() {
    let result = await super.getUpdateComplete();
    await this.controlEl?.updateComplete;
    return result;
  }

  onSettingChange = () => {
    this.setValue();
    this.requestUpdate();
  };

  /**
   * @type {SettingElement['willUpdate']}
   */
  willUpdate(changedProperties) {
    if (changedProperties.has("setting")) {
      if (this.#lastSetting) {
        this.#lastSetting.off("change", this.onSettingChange);
      }
      this.#lastSetting = this.setting;
      this.setValue();
      this.setting.on("change", this.onSettingChange);
    }
    if (!this.setting) {
      throw new SettingNotDefinedError(this.config.id);
    }
    this.id = `setting-control-${this.config.id}`;
    let prevHidden = this.hidden;
    this.hidden = !this.setting.visible;
    if (prevHidden != this.hidden) {
      this.dispatchEvent(new Event("visibility-change", { bubbles: true }));
    }
  }

  updated() {
    const control = this.controlRef?.value;
    if (!control) {
      return;
    }

    // Set the value based on the control's API.
    if ("checked" in control) {
      control.checked = this.value;
    } else if ("pressed" in control) {
      control.pressed = this.value;
    } else if ("value" in control) {
      control.value = this.value;
    }

    control.requestUpdate?.();
  }

  /**
   * The default properties that controls and options accept.
   * Note: for the disabled property, a setting can either be locked,
   * or controlled by an extension but not both.
   *
   * @override
   * @param {SettingElementConfig} config
   */
  getCommonPropertyMapping(config) {
    return {
      ...super.getCommonPropertyMapping(config),
      "data-subcategory": config.subcategory,
      ".setting": this.setting,
      ".control": this,
    };
  }

  /**
   * The default properties for an option.
   *
   * @param {SettingOptionConfig} config
   */
  getOptionPropertyMapping(config) {
    return {
      ...this.getCommonPropertyMapping(config),
      ".value": config.value,
      ".disabled": config.disabled,
      ".hidden": config.hidden,
    };
  }

  /**
   * The default properties for this control.
   *
   * @param {SettingControlConfig} config
   */
  getControlPropertyMapping(config) {
    return {
      ...this.getCommonPropertyMapping(config),
      ".parentDisabled": this.parentDisabled,
      "?disabled": this.disabled,
      // Hide moz-message-bar directly to maintain the role=alert functionality.
      // This setting-control will be visually hidden in CSS.
      ".hidden": config.control == "moz-message-bar" && this.hidden,
    };
  }

  getValue() {
    return this.setting.value;
  }

  setValue = () => {
    this.value = this.setting.value;
  };

  /**
   * @param {HTMLElement} el
   * @returns {any}
   */
  controlValue(el) {
    let Cls = el.constructor;
    if (
      "activatedProperty" in Cls &&
      Cls.activatedProperty &&
      el.localName != "moz-radio"
    ) {
      return el[/** @type {keyof typeof el} */ (Cls.activatedProperty)];
    }
    if (el instanceof MozInputFolder) {
      return el.folder;
    }
    return "value" in el ? el.value : null;
  }

  /**
   * Called by our parent when our input changed.
   *
   * @param {SettingControlChild} el
   */
  onChange(el) {
    this.setting.userChange(this.controlValue(el));
  }

  /**
   * Called by our parent when our input is clicked.
   *
   * @param {MouseEvent} event
   */
  onClick(event) {
    this.setting.userClick(event);
  }

  /**
   * Called by our parent when moz-message-bar is dismissed.
   *
   * @param {CustomEvent} event
   */
  onMessageBarDismiss(event) {
    this.setting.messageBarDismiss(event);
  }

  /**
   * Called by our parent when items are reordered. The reorder event is
   * a CustomEvent that bubbles from reorderable moz-box-group elements when
   * items are reordered via drag-and-drop or keyboard shortcuts.
   *
   * The detail object of the reorder event contains the following properties:
   *
   * - `draggedElement`: The element that was reordered.
   * - `targetElement`: The element that the dragged element was reordered relative to.
   * - `position`: The position of the drop relative to the target element. -1
   *   means before, 0 means after.
   * - `draggedIndex`: The original index of the element being reordered.
   * - `targetIndex`: The new index of the draggedElement after reordering.
   *
   * @param {CustomEvent} event
   */
  onReorder(event) {
    this.setting.userReorder(event);
  }

  async disableExtension() {
    this.isDisablingExtension = true;
    this.showEnableExtensionMessage = true;
    await this.setting.disableControllingExtension();
    this.isDisablingExtension = false;
  }

  isControlledByExtension() {
    return (
      this.setting.controllingExtensionInfo?.id &&
      this.setting.controllingExtensionInfo?.name
    );
  }

  isDisabledByExtension() {
    return (
      this.isControlledByExtension() &&
      !this.setting.controllingExtensionInfo.allowControl
    );
  }

  handleEnableExtensionDismiss() {
    this.showEnableExtensionMessage = false;
  }

  /**
   * @param {MouseEvent} event
   */
  navigateToAddons(event) {
    let link = /** @type {HTMLAnchorElement} */ (event.target);
    if (link.matches("a[data-l10n-name='addons-link']")) {
      event.preventDefault();
      // @ts-ignore
      let mainWindow = window.browsingContext.topChromeWindow;
      mainWindow.BrowserAddonUI.openAddonsMgr("addons://list/extension");
    }
  }

  get extensionName() {
    return this.setting.controllingExtensionInfo.name;
  }

  get extensionMessageId() {
    return this.setting.controllingExtensionInfo.l10nId;
  }

  /**
   * Prepare nested item config and settings.
   *
   * @param {SettingControlConfig | SettingOptionConfig} config
   * @returns {TemplateResult[]}
   */
  itemsTemplate(config) {
    if (!config.items) {
      return [];
    }

    const itemArgs = config.items.map(i => ({
      config: i,
      setting: this.getSetting(i.id),
    }));
    let control = config.control || "moz-checkbox";
    return repeat(
      itemArgs,
      item => item.config.key || item.config.id,
      item =>
        html`<setting-control
          .config=${item.config}
          .setting=${item.setting}
          .getSetting=${this.getSetting}
          slot=${ifDefined(
            item.config.slot || ITEM_SLOT_BY_PARENT.get(control)
          )}
        ></setting-control>`
    );
  }

  /**
   * Prepares any children (and any of its children's children) that this element may need.
   *
   * @param {SettingOptionConfig} config
   * @returns {TemplateResult[]}
   */
  optionsTemplate(config) {
    if (!config.options) {
      return [];
    }
    let control = config.control || "moz-checkbox";
    return repeat(
      config.options,
      opt => opt.key,
      opt => {
        let optionTag = opt.control
          ? unsafeStatic(opt.control)
          : KNOWN_OPTIONS.get(control);
        let spreadValues = spread(this.getOptionPropertyMapping(opt));
        let children =
          "items" in opt ? this.itemsTemplate(opt) : this.optionsTemplate(opt);
        if (opt.control == "a" && opt.controlAttrs?.is == "moz-support-link") {
          // The `is` attribute must be set when the element is first added to the
          // DOM. We need to mark that up manually, since `spread()` uses
          // `el.setAttribute()` to set attributes it receives.
          return html`<a is="moz-support-link" ${spreadValues}>${children}</a>`;
        }
        return staticHtml`<${optionTag} ${spreadValues}>${children}</${optionTag}>`;
      }
    );
  }

  get extensionSupportPage() {
    return this.setting.controllingExtensionInfo.supportPage;
  }

  render() {
    // Allow the Setting to override the static config if necessary.
    this.config = this.setting.getControlConfig(this.config);
    let { config } = this;
    let control = config.control || "moz-checkbox";

    let nestedSettings =
      "items" in config
        ? this.itemsTemplate(config)
        : this.optionsTemplate(config);

    // Get the properties for this element: id, fluent, disabled, etc.
    // These will be applied to the control using the spread directive.
    let controlProps = this.getControlPropertyMapping(config);

    let tag = unsafeStatic(control);
    let messageBar;

    // NOTE: the showEnableMessage message bar should ONLY appear when
    // there are no extensions controlling the setting.
    if (this.isControlledByExtension()) {
      let args = { name: this.extensionName };
      let supportPage = this.extensionSupportPage;
      messageBar = html`<moz-message-bar
        class="extension-controlled-message-bar"
        ${ref(this.controlledMessageBarRef)}
        .messageL10nId=${this.extensionMessageId}
        .messageL10nArgs=${args}
      >
        ${supportPage
          ? html`<a
              is="moz-support-link"
              slot="support-link"
              support-page=${supportPage}
            ></a>`
          : ""}
        ${this.setting.controllingExtensionInfo.mayDisable
          ? html`<moz-button
              slot="actions"
              @click=${this.disableExtension}
              ?disabled=${this.isDisablingExtension}
              data-l10n-id="disable-extension"
            ></moz-button>`
          : ""}
      </moz-message-bar>`;
    } else if (this.showEnableExtensionMessage) {
      messageBar = html`<moz-message-bar
        class="reenable-extensions-message-bar"
        dismissable=""
        ${ref(this.enableMessageBarRef)}
        @message-bar:user-dismissed=${this.handleEnableExtensionDismiss}
      >
        <span
          @click=${this.navigateToAddons}
          slot="message"
          data-l10n-id="extension-controlled-enable-2"
        >
          <a data-l10n-name="addons-link" href="#"></a>
        </span>
      </moz-message-bar>`;
    }
    return staticHtml`
    ${messageBar}
    <${tag}
      ${spread(controlProps)}
      ${ref(this.controlRef)}
      tabindex=${ifDefined(this.tabIndex)}
    >${nestedSettings}</${tag}>`;
  }
}
customElements.define("setting-control", SettingControl);
