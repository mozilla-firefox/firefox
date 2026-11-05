/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  when,
  ifDefined,
} from "chrome://global/content/vendor/lit.all.mjs";

import { SidebarPage } from "./sidebar-page.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const l10nMap = new Map([
  ["viewGenaiChatSidebar", "sidebar-menu-genai-chat-label"],
  ["viewGenaiPageAssistSidebar", "sidebar-menu-genai-page-assist-label"],
  ["viewHistorySidebar", "sidebar-menu-history-label"],
  ["viewTabsSidebar", "sidebar-menu-synced-tabs-label"],
  ["viewBookmarksSidebar", "sidebar-menu-bookmarks-label"],
  ["viewOpenTabsSidebar", "sidebar-menu-open-tabs-label"],
  ["viewCPMSidebar", "sidebar-menu-contextual-password-manager-label"],
  ["viewResourceMonitorSidebar", "sidebar-menu-resource-monitor-label"],
  ["viewWidgetsSidebar", "sidebar-menu-widget-studio-label"],
]);
const VISIBILITY_SETTING_PREF = "sidebar.visibility";
const EXPAND_ON_HOVER_PREF = "sidebar.expandOnHover";
const POSITION_SETTING_PREF = "sidebar.position_start";
const TAB_DIRECTION_SETTING_PREF = "sidebar.verticalTabs";
const HOVER_PREVIEW_PREF = "sidebar.openTabsPanel.hoverPreview.enabled";

export class SidebarCustomize extends SidebarPage {
  constructor() {
    super();
    XPCOMUtils.defineLazyPreferenceGetter(
      this.#prefValues,
      "visibility",
      VISIBILITY_SETTING_PREF,
      "always-show",
      (_aPreference, _previousValue, newValue) => {
        this.visibility = newValue;
      }
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this.#prefValues,
      "isPositionStart",
      POSITION_SETTING_PREF,
      true,
      (_aPreference, _previousValue, newValue) => {
        this.isPositionStart = newValue;
      }
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this.#prefValues,
      "verticalTabsEnabled",
      TAB_DIRECTION_SETTING_PREF,
      false,
      (_aPreference, _previousValue, newValue) => {
        this.verticalTabsEnabled = newValue;
      }
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this.#prefValues,
      "expandOnHoverEnabled",
      EXPAND_ON_HOVER_PREF,
      false,
      (_aPreference, _previousValue, newValue) => {
        this.expandOnHoverEnabled = newValue;
      }
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this.#prefValues,
      "hoverPreviewEnabled",
      HOVER_PREVIEW_PREF,
      true,
      (_aPreference, _previousValue, newValue) => {
        this.hoverPreviewEnabled = newValue;
      }
    );
    this.visibility = this.#prefValues.visibility;
    this.isPositionStart = this.#prefValues.isPositionStart;
    this.verticalTabsEnabled = this.#prefValues.verticalTabsEnabled;
    this.expandOnHoverEnabled = this.#prefValues.expandOnHoverEnabled;
    this.hoverPreviewEnabled = this.#prefValues.hoverPreviewEnabled;
    this.boundObserve = (...args) => this.observe(...args);
  }

  #prefValues = {};

  static properties = {
    visibility: { type: String },
    isPositionStart: { type: Boolean },
    verticalTabsEnabled: { type: Boolean },
    expandOnHoverEnabled: { type: Boolean },
    hoverPreviewEnabled: { type: Boolean },
  };

  static queries = {
    toolInputs: { all: ".tools > .tool" },
    extensionInputs: { all: ".extensions > .tool" },
    extensionLink: ".extension-item a",
    positionInput: "#position",
    visibilityInput: "#hide-sidebar",
    verticalTabsInput: "#vertical-tabs",
    expandOnHoverInput: "#expand-on-hover",
    openToolsFromSidebarInput: "#open-tools-from-sidebar",
    hoverPreviewInput: "#hover-preview",
  };

  connectedCallback() {
    super.connectedCallback();
    this.getWindow().addEventListener("SidebarItemAdded", this);
    this.getWindow().addEventListener("SidebarItemChanged", this);
    this.getWindow().addEventListener("SidebarItemRemoved", this);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.getWindow().removeEventListener("SidebarItemAdded", this);
    this.getWindow().removeEventListener("SidebarItemChanged", this);
    this.getWindow().removeEventListener("SidebarItemRemoved", this);
  }

  get fluentStrings() {
    if (!this._fluentStrings) {
      this._fluentStrings = new Localization(["browser/sidebar.ftl"], true);
    }
    return this._fluentStrings;
  }

  getWindow() {
    return window.browsingContext.embedderWindowGlobal.browsingContext.window;
  }

  handleEvent(e) {
    switch (e.type) {
      case "SidebarItemAdded":
      case "SidebarItemChanged":
      case "SidebarItemRemoved":
        this.requestUpdate();
        break;
    }
  }

  async onToggleToolInput(e, commandID) {
    e.preventDefault();
    this.getWindow().SidebarController.toggleTool(commandID);
    switch (commandID) {
      case "viewGenaiChatSidebar":
        Glean.sidebarCustomize.chatbotEnabled.record({
          checked: e.target.checked,
        });
        break;
      case "viewTabsSidebar":
        Glean.sidebarCustomize.syncedTabsEnabled.record({
          checked: e.target.checked,
        });
        break;
      case "viewHistorySidebar":
        Glean.sidebarCustomize.historyEnabled.record({
          checked: e.target.checked,
        });
        break;
      case "viewBookmarksSidebar":
        Glean.sidebarCustomize.bookmarksEnabled.record({
          checked: e.target.checked,
        });
        break;
      case "viewCPMSidebar":
        Glean.contextualManager.passwordsEnabled.record({
          checked: e.target.checked,
        });
        break;
    }
  }

  getInputL10nId(view) {
    return l10nMap.get(view);
  }

  openFirefoxSettings(e) {
    if (e.type == "click" || (e.type == "keydown" && e.code == "Enter")) {
      e.preventDefault();
      this.getWindow().openPreferences();
      Glean.sidebarCustomize.firefoxSettingsClicked.record();
    }
  }

  toolInputTemplate(tool) {
    if (tool.hidden) {
      return null;
    }

    return html`
      <moz-checkbox
        class="tool"
        type="checkbox"
        id=${tool.view}
        name=${tool.name}
        iconsrc=${tool.iconUrl}
        data-l10n-id=${ifDefined(this.getInputL10nId(tool.view))}
        label=${ifDefined(tool.tooltiptext)}
        @change=${e => this.onToggleToolInput(e, tool.commandID)}
        ?checked=${!tool.disabled}
      >
        ${when(
          tool.view === "viewOpenTabsSidebar" && !tool.disabled,
          () => html`
            <moz-checkbox
              slot="nested"
              type="checkbox"
              id="hover-preview"
              name="hover-preview"
              data-l10n-id="sidebar-show-preview-on-hover"
              @change=${this.#toggleHoverPreview}
              ?checked=${this.hoverPreviewEnabled}
            ></moz-checkbox>
          `
        )}
      </moz-checkbox>
    `;
  }

  manageAddons(e) {
    if (e.type == "click" || (e.type == "keydown" && e.code == "Enter")) {
      e.preventDefault();
      this.getWindow().BrowserAddonUI.openAddonsMgr("addons://list/extension");
      Glean.sidebarCustomize.extensionsClicked.record();
    }
  }

  reversePosition() {
    const { SidebarController } = this.getWindow();
    SidebarController.reversePosition();
    Glean.sidebarCustomize.sidebarPosition.record({
      position:
        this.isPositionStart !== this.getWindow().RTL_UI ? "left" : "right",
    });
  }

  render() {
    let extensions = this.getWindow().SidebarController.getExtensions();
    return html`
      ${this.stylesheet()}
      <link rel="stylesheet" href="chrome://browser/content/sidebar/sidebar-customize.css"></link>
      <div class="sidebar-panel">
        <div class="sidebar-panel-scrollable-content">
          <moz-fieldset class="customize-group no-end-margin" data-l10n-id="sidebar-settings2">
            <moz-checkbox
              type="checkbox"
              id="vertical-tabs"
              name="verticalTabs"
              data-l10n-id="sidebar-vertical-tabs"
              @change=${this.#handleTabDirectionChange}
              ?checked=${this.verticalTabsEnabled}
            >
            ${when(
              this.verticalTabsEnabled,
              () => html`
                ${when(
                  this.expandOnHoverEnabled,
                  () => html`
                    <moz-checkbox
                      slot="nested"
                      type="checkbox"
                      id="expand-on-hover"
                      name="expand-on-hover"
                      data-l10n-id="expand-sidebar-on-hover"
                      @change=${this.#toggleExpandOnHover}
                      ?checked=${this.getWindow().SidebarController._state
                        .revampVisibility === "expand-on-hover"}
                      ?disabled=${this.visibility == "hide-sidebar"}
                    ></moz-checkbox>
                  `
                )}
                <moz-checkbox
                  slot="nested"
                  type="checkbox"
                  id="hide-sidebar"
                  name="hideSidebar"
                  data-l10n-id="sidebar-hide-tabs-and-sidebar"
                  @change=${this.#handleVisibilityChange}
                  ?checked=${this.visibility == "hide-sidebar"}
                  ?disabled=${this.getWindow().SidebarController._state
                    .revampVisibility === "expand-on-hover"}
                ></moz-checkbox>
              `
            )}
            </moz-checkbox>
          </moz-fieldset>
          <moz-fieldset
            class="customize-group medium-top-margin no-end-margin no-label"
            ?disabled=${this.verticalTabsEnabled}
          >
            <moz-checkbox
              type="checkbox"
              id="open-tools-from-sidebar"
              name="openToolsFromSidebar"
              data-l10n-id="sidebar-open-tools-from-sidebar"
              @change=${this.#handleOpenToolsFromSidebarChange}
              ?checked=${
                this.verticalTabsEnabled || this.visibility !== "hide-launcher"
              }
            ></moz-checkbox>
          </moz-fieldset>
          <moz-fieldset class="customize-group medium-top-margin no-label">
            <moz-checkbox
              type="checkbox"
              id="position"
              name="position"
              data-l10n-id=${document.dir == "rtl" ? "sidebar-show-on-the-left" : "sidebar-show-on-the-right"}
              @change=${this.reversePosition}
              ?checked=${!this.isPositionStart}
          ></moz-checkbox>
          </moz-fieldset>
          <moz-fieldset class="customize-group tools" data-l10n-id="sidebar-customize-firefox-tools-header2">
            ${this.getWindow()
              .SidebarController.getTools()
              .map(tool => this.toolInputTemplate(tool))}
          </moz-fieldset>
          ${when(
            extensions.length,
            () =>
              html`<div class="customize-group">
                <h4
                  class="customize-extensions-heading"
                  data-l10n-id="sidebar-customize-extensions-header2"
                ></h4>
                <div role="list" class="extensions">
                  ${extensions.map(extension =>
                    this.toolInputTemplate(extension)
                  )}
                  <div class="extension-item">
                    <img
                      src="chrome://mozapps/skin/extensions/category-extensions.svg"
                      class="icon"
                      role="presentation"
                    />
                    <a
                      href="about:addons"
                      @click=${this.manageAddons}
                      @keydown=${this.manageAddons}
                      data-l10n-id="sidebar-manage-extensions2"
                    >
                    </a>
                  </div>
                </div>
              </div>`
          )}
        </div>
        <div id="manage-settings">
          <img src="chrome://browser/skin/preferences/category-general.svg" class="icon" role="presentation" />
          <a
            href="about:preferences"
            @click=${this.openFirefoxSettings}
            @keydown=${this.openFirefoxSettings}
            data-l10n-id="sidebar-customize-firefox-settings"
          >
          </a>
        </div>
      </div>
    `;
  }

  #handleVisibilityChange(e) {
    e.stopPropagation();
    this.visibility = e.target.checked ? "hide-sidebar" : "always-show";
    Services.prefs.setStringPref(
      VISIBILITY_SETTING_PREF,
      e.target.checked ? "hide-sidebar" : "always-show"
    );
    Glean.sidebarCustomize.sidebarDisplay.record({
      preference: e.target.checked ? "hide" : "always",
    });
  }

  #handleOpenToolsFromSidebarChange(e) {
    e.stopPropagation();
    // Checked: tools open from the launcher (the horizontal-tabs default).
    // Unchecked: the launcher is replaced by the panel switcher dropdown.
    this.visibility = e.target.checked ? "hide-on-close" : "hide-launcher";
    Services.prefs.setStringPref(VISIBILITY_SETTING_PREF, this.visibility);
    Glean.sidebarCustomize.sidebarDisplay.record({
      preference: e.target.checked ? "always" : "hide",
    });
  }

  #toggleExpandOnHover(e) {
    e.stopPropagation();
    if (e.target.checked) {
      Services.prefs.setStringPref("sidebar.visibility", "expand-on-hover");
      Glean.sidebarCustomize.expandOnHoverEnabled.record({
        checked: true,
      });
    } else {
      Services.prefs.setStringPref("sidebar.visibility", "always-show");
    }
  }

  #toggleHoverPreview(e) {
    e.stopPropagation();
    Services.prefs.setBoolPref(HOVER_PREVIEW_PREF, e.target.checked);
  }

  #handleTabDirectionChange({ target: { checked } }) {
    const verticalTabsEnabled = checked;
    Services.prefs.setBoolPref(TAB_DIRECTION_SETTING_PREF, verticalTabsEnabled);
    Glean.sidebarCustomize.tabsLayout.record({
      orientation: verticalTabsEnabled ? "vertical" : "horizontal",
    });
  }
}

customElements.define("sidebar-customize", SidebarCustomize);
