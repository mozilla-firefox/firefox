/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import {
  AddonManagerListenerHandler,
  isBrowserNovaEnabled,
} from "../aboutaddons-utils.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { getL10nIdForThemeProp, themeIdPrefix } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/ThemesBundledLocalization.sys.mjs"
);

const PREF_ACTIVE_THEME_ID = "extensions.activeThemeID";
// NOTE: nova themes picker is enabled by default by Firefox Desktop prefs
// and disabled by default by Gecko Toolkit level prefs.
const PREF_NOVA_THEMES_PICKER = "browser.aboutaddons.novaThemesPickerEnabled";

const lazy = XPCOMUtils.declareLazy({
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  getThemesList: "moz-src:///browser/themes/ThemesList.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",

  activeThemeID: {
    pref: PREF_ACTIVE_THEME_ID,
    default: "",
  },

  novaThemesPickerEnabled: {
    pref: PREF_NOVA_THEMES_PICKER,
    default: false,
  },
});

export function isNovaThemesPickerEnabled() {
  return lazy.novaThemesPickerEnabled && isBrowserNovaEnabled();
}

export class AboutaddonsThemesPicker extends MozLitElement {
  static properties = {
    currentThemeId: { type: String },
    visibleRows: { type: Number },
    _expanded: { state: true },
    _hasError: { state: true },
  };

  static queries = {
    pickerCard: "moz-card",
    themeCards: { all: ".theme-card" },
    expandToggle: ".expand-toggle",
    errorBar: "moz-message-bar",
  };

  #prefObserver = {
    observe: (_subject, _topic, prefName) => {
      if (prefName === PREF_NOVA_THEMES_PICKER) {
        this.requestUpdate();
      } else {
        this.#updateCurrentThemeFromPref();
      }
    },
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
  };

  #addonListener = {
    onInstalled: addon => {
      if (addon.type === "theme" && this.#manager?.hasThemeId(addon.id)) {
        this.#installedAddonIds.add(addon.id);
        this.requestUpdate();
      }
    },
    onUninstalled: addon => {
      if (addon.type === "theme" && this.#manager?.hasThemeId(addon.id)) {
        this.#installedAddonIds.delete(addon.id);
        this.requestUpdate();
      }
    },
  };

  #manager = null;
  #themes = [];
  #installedAddonIds = new Set();

  constructor() {
    super();
    this.currentThemeId = "";
    this.visibleRows = 3;
    this._expanded = false;
    this._hasError = false;
  }

  connectedCallback() {
    super.connectedCallback();
    Services.prefs.addObserver(PREF_ACTIVE_THEME_ID, this.#prefObserver);
    Services.prefs.addObserver(PREF_NOVA_THEMES_PICKER, this.#prefObserver);
    AddonManagerListenerHandler.addListener(this.#addonListener);
    this.#loadThemes();
    this.#recordShownTelemetry();
  }

  #recordShownTelemetry() {
    // Do not record the theme-picker.shown Glean event if this component
    // is not going to be rendering its content.
    if (!isNovaThemesPickerEnabled()) {
      return;
    }
    Glean.themePicker.shown.record({ source: "about:addons", layout: "full" });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    Services.prefs.removeObserver(PREF_ACTIVE_THEME_ID, this.#prefObserver);
    Services.prefs.removeObserver(PREF_NOVA_THEMES_PICKER, this.#prefObserver);
    AddonManagerListenerHandler.removeListener(this.#addonListener);
  }

  async #loadThemes() {
    if (!isNovaThemesPickerEnabled()) {
      // Do not attempt to load the Nova themes on builds that are opt-ing out
      // of this component explicitly (and it would render as `nothing` anyway).
      return;
    }
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    window.MozXULElement.insertFTLIfNeeded("browser/appExtensionFields.ftl");
    const [manager, installedThemes] = await Promise.all([
      lazy.getThemesList({ installSource: "about:addons" }),
      lazy.AddonManager.getAddonsByTypes(["theme"]),
    ]);
    this.#manager = manager;
    this.#themes = manager.getThemesInfo();
    this.#installedAddonIds = new Set(installedThemes.map(a => a.id));
    this.#updateCurrentThemeFromPref();
    this.requestUpdate();
  }

  #updateCurrentThemeFromPref() {
    const addonId = lazy.activeThemeID;
    this.currentThemeId = this.#manager?.hasThemeId(addonId) ? addonId : "";
  }

  get #visibleThemes() {
    if (this._expanded) {
      return this.#themes;
    }
    return this.#themes.slice(0, this.visibleRows * 2);
  }

  get #needsExpandToggle() {
    return this.#themes.length > this.visibleRows * 2;
  }

  #isInstalled(themeId) {
    return (
      this.#manager.isBuiltIn(themeId) || this.#installedAddonIds.has(themeId)
    );
  }

  #isActive(themeId) {
    return themeId === this.currentThemeId;
  }

  #isButtonHidden(themeId) {
    return themeId === "default-theme@mozilla.org" && this.#isActive(themeId);
  }

  #buttonL10nId(themeId) {
    if (this.#isActive(themeId)) {
      return "aboutaddons-themes-picker-disable-button";
    }
    if (this.#isInstalled(themeId)) {
      return "aboutaddons-themes-picker-enable-button";
    }
    return "aboutaddons-themes-picker-install-button";
  }

  async #onButtonClick(themeId) {
    this._hasError = false;
    const success = await this.#manager.updateThemeState(
      themeId,
      !this.#isActive(themeId),
      { layout: "full" }
    );
    this._hasError = !success;
    await this.#loadThemes();
  }

  #toggleExpanded() {
    this._expanded = !this._expanded;
  }

  render() {
    if (!isNovaThemesPickerEnabled()) {
      return nothing;
    }
    return html`
      <link
        rel="stylesheet"
        href="chrome://mozapps/content/extensions/components/aboutaddons-themes-picker.css"
      />
      ${this._hasError
        ? html`
            <moz-message-bar
              class="picker-error-bar"
              type="error"
              dismissable
              data-l10n-id="aboutaddons-themes-picker-error-message"
              @message-bar:user-dismissed=${() => (this._hasError = false)}
            ></moz-message-bar>
          `
        : nothing}
      <moz-card
        id="aboutaddons-themes-picker-card"
        data-l10n-id="aboutaddons-themes-picker-heading"
        headingLevel="2"
        role="list"
      >
        <div class="theme-grid">
          ${this.#visibleThemes.map(({ id: themeId }) => {
            return html`
              <moz-box-item
                class="theme-card"
                role="listitem"
                data-theme=${themeIdPrefix(themeId)}
              >
                <theme-preview
                  .addon=${{ id: themeId, type: "theme" }}
                ></theme-preview>
                <div class="theme-card-footer">
                  <span
                    class="theme-name"
                    data-l10n-id=${getL10nIdForThemeProp(themeId, "name")}
                  ></span>
                  <moz-button
                    size="small"
                    data-l10n-id=${this.#buttonL10nId(themeId)}
                    ?hidden=${this.#isButtonHidden(themeId)}
                    @click=${() => this.#onButtonClick(themeId)}
                  ></moz-button>
                </div>
              </moz-box-item>
            `;
          })}
        </div>
        ${this.#needsExpandToggle
          ? html`
              <moz-button
                type="ghost"
                class="expand-toggle"
                data-l10n-id=${this._expanded
                  ? "aboutaddons-themes-picker-see-less"
                  : "aboutaddons-themes-picker-see-more"}
                @click=${this.#toggleExpanded}
              ></moz-button>
            `
          : nothing}
      </moz-card>
    `;
  }
}
customElements.define("aboutaddons-themes-picker", AboutaddonsThemesPicker);
