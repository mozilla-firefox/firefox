/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./devtools-settings-theme.tagmap.d.ts" />

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

/**
 * @import { PropertyDeclarations } from "chrome://global/content/vendor/lit.all.mjs"
 */

// eslint-disable-next-line no-shadow
const { require } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/Loader.sys.mjs"
);
/** @type {typeof import("../../framework/devtools.js")} */
const {
  gDevTools,
} = require("resource://devtools/client/framework/devtools.js");

/** @type {Record<string, string>} */
const THEME_ICONS = {
  auto: "chrome://global/skin/icons/local-host.svg",
  light: "chrome://global/skin/icons/sun.svg",
  dark: "chrome://global/skin/icons/moon.svg",
};

/** Renders a segmented control listing the themes registered with DevTools. */
export class DevToolsSettingsTheme extends MozLitElement {
  /** @type {AbortController | null} */
  #abortController = null;

  /** @satisfies {PropertyDeclarations} */
  static properties = {
    l10nId: { type: String, attribute: "l10n-id" },
  };

  constructor() {
    super();
    /** @type {string | null} */
    this.l10nId = null;
  }

  connectedCallback() {
    super.connectedCallback();
    this.#abortController = new AbortController();
    const { signal } = this.#abortController;
    gDevTools.on("theme-registered", this.#refresh, { signal });
    gDevTools.on("theme-unregistered", this.#refresh, { signal });
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#abortController?.abort();
  }

  #refresh = () => {
    this.requestUpdate();
  };

  render() {
    return html`
      <devtools-settings-pref pref="devtools.theme">
        <moz-segmented-control data-l10n-id=${this.l10nId ?? nothing}>
          <moz-segmented-control-item
            value="auto"
            iconsrc=${THEME_ICONS.auto}
            data-l10n-id="devtools-settings-theme-auto"
          ></moz-segmented-control-item>
          ${gDevTools
            .getThemeDefinitionArray()
            .map(
              theme =>
                html`<moz-segmented-control-item
                  value=${theme.id}
                  iconsrc=${THEME_ICONS[theme.id] ?? nothing}
                  label=${theme.label}
                ></moz-segmented-control-item>`
            )}
        </moz-segmented-control>
      </devtools-settings-pref>
    `;
  }
}

customElements.define("devtools-settings-theme", DevToolsSettingsTheme);
