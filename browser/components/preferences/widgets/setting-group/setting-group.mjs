/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import {
  SettingElement,
  spread,
} from "chrome://browser/content/preferences/widgets/setting-element.mjs";
import { SettingControl } from "chrome://browser/content/preferences/widgets/setting-control.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

/**
 * @import { SettingElementConfig } from "chrome://browser/content/preferences/widgets/setting-element.mjs"
 * @import { SettingControlConfig, SettingControlEvent } from "../setting-control/setting-control.mjs"
 * @import { Preferences } from "chrome://global/content/preferences/Preferences.mjs"
 * @import { TemplateResult } from "chrome://global/content/vendor/lit.all.mjs";
 */

/**
 * @typedef {object} SettingGroupConfigExtensions
 * @property {SettingControlConfig[]} items Array of SettingControlConfigs to render.
 * @property {boolean} [inProgress]
 * Hide this section unless the browser.settings-redesign.enabled or
 * browser.settings-redesign.<groupid>.enabled prefs are true.
 * @property {"default"|"always"|"never"} [card]
 * Whether to use a card. Default: use a card after SRD or in a sub-pane.
 * @property {boolean} [hiddenFromSearch]
 * Whether this group should be hidden from search.
 * @property {boolean} [hidden] Whether this group should be visible.
 * @property {string} [subcategory] Value for the `data-subcategory` attribute, used as a scroll target.
 */
/** @typedef {SettingElementConfig & SettingGroupConfigExtensions} SettingGroupConfig */

const CLICK_HANDLERS = new Set([
  "moz-box-button",
  "moz-box-item",
  "moz-box-link",
  "moz-button",
  "moz-box-group",
  "moz-message-bar",
  "a",
]);
const DISMISS_HANDLERS = new Set(["moz-message-bar"]);
const REORDER_HANDLERS = new Set(["moz-box-group"]);

/**
 * Enumish of attribute names used for changing setting-group and groupbox
 * visibilities based on the visibility of child setting-controls.
 */
const HiddenAttr = Object.freeze({
  /** Attribute used to hide elements without using the hidden attribute. */
  Self: "data-hidden-by-setting-group",
  /** Attribute used to signal that this element should not be searchable. */
  Search: "data-hidden-from-search",
});

export class SettingGroup extends SettingElement {
  static properties = {
    config: { type: Object },
    groupId: { type: String },
    getSetting: { type: Function },
    srdEnabled: { type: Boolean },
    inSubPane: { type: Boolean },
  };

  static queries = {
    allControlEls: { all: "setting-control" },
    fieldsetEl: "moz-fieldset",
  };

  /**
   * Immediate child control elements. See {@link SettingGroup.allControlEls} to
   * get all ancestors.
   */
  get childControlEls() {
    if (!this.config) {
      return [];
    }
    // @ts-expect-error bug 1997478
    return [...this.fieldsetEl.children].filter(
      child => child instanceof SettingControl
    );
  }

  constructor() {
    super();

    /**
     * @type {Preferences['getSetting'] | undefined}
     */
    this.getSetting = undefined;

    /**
     * @type {SettingGroupConfig | undefined}
     */
    this.config = undefined;

    /**
     * Set by initSettingGroup based on browser.settings-redesign.enabled.
     */
    this.srdEnabled = false;
    /**
     * Set by setting-pane if this is a sub pane so we can render cards even if SRD is off.
     */
    this.inSubPane = false;
  }

  createRenderRoot() {
    return this;
  }

  /**
   * Unsubscribe callback for the late-registration listener. See
   * connectedCallback for why.
   *
   * @type {(() => void) | null}
   */
  #unsubscribeGroupRegister = null;

  connectedCallback() {
    super.connectedCallback();
    // This element can render before its config is available. Initialize once
    // the group registers, which can happen later (bug 2051119).
    this.#unsubscribeGroupRegister = SettingGroupManager.onRegister(id => {
      if (id === this.groupId && !this.config) {
        window.initSettingGroup(id);
      }
    });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#unsubscribeGroupRegister?.();
    this.#unsubscribeGroupRegister = null;
  }

  willUpdate() {
    if (!this.srdEnabled) {
      this.classList.toggle("subcategory", this.config?.headingLevel == 1);
    }
    // Only set/remove attributes when explicitly defined in config
    // This allows handleVisibilityChange to manage visibility independently
    if (this.config?.hiddenFromSearch !== undefined) {
      if (this.config.hiddenFromSearch) {
        this.setAttribute(HiddenAttr.Search, "true");
      } else {
        this.removeAttribute(HiddenAttr.Search);
      }
    }
    if (this.config?.hidden !== undefined) {
      this.toggleAttribute(HiddenAttr.Self, this.config.hidden);
    }
    if (this.config?.subcategory) {
      this.setAttribute("data-subcategory", this.config.subcategory);
    }
  }

  async handleVisibilityChange() {
    await this.updateComplete;

    // Don't change visibility if explicitly hidden by config
    if (this.config?.hidden) {
      return;
    }

    let hasVisibleControls =
      !!this.childControlEls?.length &&
      this.childControlEls?.some(el => !el.hidden);
    let groupbox = /** @type {XULElement} */ (this.closest("groupbox"));

    if (hasVisibleControls) {
      if (this.hasAttribute(HiddenAttr.Self)) {
        this.removeAttribute(HiddenAttr.Self);
        this.removeAttribute(HiddenAttr.Search);
      }
      if (groupbox && groupbox.hasAttribute(HiddenAttr.Self)) {
        groupbox.removeAttribute(HiddenAttr.Search);
        groupbox.removeAttribute(HiddenAttr.Self);
      }
    } else {
      this.setAttribute(HiddenAttr.Self, "");
      this.setAttribute(HiddenAttr.Search, "true");
      if (groupbox && !groupbox.hasAttribute(HiddenAttr.Search)) {
        groupbox.setAttribute(HiddenAttr.Search, "true");
        groupbox.setAttribute(HiddenAttr.Self, "");
      }
    }
  }

  async getUpdateComplete() {
    let result = await super.getUpdateComplete();
    // @ts-expect-error bug 1997478
    await Promise.all([...this.allControlEls].map(el => el.updateComplete));
    return result;
  }

  /**
   * Notify child controls when their input has fired an event. When controls
   * are nested the parent receives events for the nested controls, so this is
   * actually easier to manage here; it also registers fewer listeners.
   *
   * @param {SettingControlEvent<InputEvent>} e
   */
  onChange(e) {
    let inputEl = e.target;
    inputEl.control?.onChange(inputEl);
  }

  /**
   * Notify child controls when their input has been clicked. When controls
   * are nested the parent receives events for the nested controls, so this is
   * actually easier to manage here; it also registers fewer listeners.
   *
   * @param {SettingControlEvent<MouseEvent>} e
   */
  onClick(e) {
    let inputEl = e.target;
    if (!CLICK_HANDLERS.has(inputEl.localName)) {
      return;
    }
    inputEl.control?.onClick(e);
  }

  /**
   * Notify child controls when message bar has been dismissed. When controls
   * are nested the parent receives events for the nested controls, so this is
   * actually easier to manage here; it also registers fewer listeners.
   *
   * @param {SettingControlEvent<CustomEvent>} e
   */
  onMessageBarDismiss(e) {
    let inputEl = e.target;
    if (!DISMISS_HANDLERS.has(inputEl.localName)) {
      return;
    }
    inputEl.control?.onMessageBarDismiss(e);
  }

  /**
   * Notify child controls when items have been reordered. The reorder event is
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
   * @param {SettingControlEvent<CustomEvent>} e
   */
  onReorder(e) {
    let inputEl = e.target;
    if (!REORDER_HANDLERS.has(inputEl.localName)) {
      return;
    }
    inputEl.control?.onReorder(e);
  }

  /**
   * @param {SettingControlConfig} item
   */
  itemTemplate(item) {
    let setting = this.getSetting(item.id);
    return html`<setting-control
      .setting=${setting}
      .config=${item}
      .getSetting=${this.getSetting}
    ></setting-control>`;
  }

  /**
   * @param {TemplateResult} content The content to render in a container.
   */
  containerTemplate(content) {
    if (
      (this.srdEnabled || this.inSubPane || this.config.card == "always") &&
      this.config.card != "never"
    ) {
      return html`<moz-card role="presentation">${content}</moz-card>`;
    }
    return content;
  }

  render() {
    if (!this.config) {
      return "";
    }
    /**
     * setting-groups always render a heading, so fall back to h2 (which gets
     * bumped to h3) when the config doesn't set one.
     */
    let config = { ...this.config };
    if (this.srdEnabled) {
      config.headingLevel ??= 2;
    }
    return this.containerTemplate(
      html`<moz-fieldset
        @change=${this.onChange}
        @toggle=${this.onChange}
        @click=${this.onClick}
        @message-bar:user-dismissed=${this.onMessageBarDismiss}
        @reorder=${this.onReorder}
        @visibility-change=${this.handleVisibilityChange}
        ${spread(this.getCommonPropertyMapping(config))}
        >${this.config.items.map(item => this.itemTemplate(item))}</moz-fieldset
      >`
    );
  }
}
customElements.define("setting-group", SettingGroup);
