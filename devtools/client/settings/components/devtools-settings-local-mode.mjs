/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./devtools-settings-local-mode.tagmap.d.ts" />

import {
  html,
  nothing,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

/**
 * @import { PropertyDeclarations } from "chrome://global/content/vendor/lit.all.mjs"
 * @import { Toolbox } from "../types/toolbox.d.ts"
 * @import { Mapping, LocalModeMappingsModule } from "../types/local-mode.d.ts"
 */

const l10n = new Localization(["devtools/preview/settings.ftl"]);

const { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs"
);
const { LocalModeMappings } = /** @type {LocalModeMappingsModule} */ (
  ChromeUtils.importESModule(
    "resource://devtools/client/framework/LocalModeMappings.sys.mjs"
  )
);

/** The Local Mode mappings editor. */
export class DevToolsSettingsLocalMode extends MozLitElement {
  /** @type {AbortController | null} */
  #abortController = null;

  /** @satisfies {PropertyDeclarations} */
  static properties = {
    toolbox: { type: Object, attribute: false },
    _mappings: { state: true },
  };

  constructor() {
    super();
    /** @type {Toolbox | null} */
    this.toolbox = null;
    /** @type {Mapping[]} */
    this._mappings = [];
  }

  connectedCallback() {
    super.connectedCallback();
    this.#abortController = new AbortController();
    LocalModeMappings.on("updated", this.#readMappings, {
      signal: this.#abortController.signal,
    });
    this.#readMappings();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#abortController?.abort();
  }

  #readMappings = () => {
    this._mappings = LocalModeMappings.getAllMappings();
  };

  #addMapping = async () => {
    const mappingOrigin = LocalModeMappings.getNextAvailableOrigin();
    const path = await this.#chooseFolder(mappingOrigin);
    if (!path) {
      return;
    }
    LocalModeMappings.createNewMapping(mappingOrigin, path);
    Glean.devtoolsLocalmodeMappings.addPermanentFromOptions.add(1);
    this.#readMappings();
    await this.updateComplete;
    const input = this.shadowRoot?.querySelector("li:last-of-type .origin");
    if (input instanceof MozInputText) {
      await input.updateComplete;
      input.focus();
      input.inputEl.select();
    }
  };

  /**
   * @param {string} mappingOrigin The mapping's hostname.
   * @returns {Promise<string | null>} The chosen path, or null if cancelled.
   */
  async #chooseFolder(mappingOrigin) {
    const title = await l10n.formatValue(
      "devtools-settings-local-mode-folder-picker-title",
      { url: "https://" + mappingOrigin }
    );
    const filePicker = Cc["@mozilla.org/filepicker;1"].createInstance(
      Ci.nsIFilePicker
    );
    filePicker.init(
      window.browsingContext,
      title ?? "",
      Ci.nsIFilePicker.modeGetFolder
    );
    return new Promise(resolve => {
      filePicker.open(result => {
        if (result == Ci.nsIFilePicker.returnOK) {
          resolve(filePicker.file.path);
        } else {
          resolve(null);
        }
      });
    });
  }

  /** @param {Mapping} mapping */
  #renderFolder(mapping) {
    return html`
      <div class="folder-row">
        <span data-l10n-id="devtools-settings-local-mode-folder"></span>
      </div>
      <span class="folder">${mapping.path}</span>
      ${folderExists(mapping.path)
        ? nothing
        : html`<p
            class="folder-error"
            data-l10n-id="devtools-settings-local-mode-folder-invalid"
          ></p>`}
    `;
  }

  /** @param {Mapping} mapping */
  #renderMapping(mapping) {
    return html`
      <li>
        <div class="origin-row">
          <moz-input-text
            class="origin"
            data-l10n-id="devtools-settings-local-mode-origin"
            ?disabled=${mapping.disabled}
            .value=${mapping.origin}
          ></moz-input-text>
        </div>
        ${this.#renderFolder(mapping)}
      </li>
    `;
  }

  render() {
    if (!this.toolbox) {
      return nothing;
    }
    const { descriptorFront } = this.toolbox.commands;
    return html`
      <link
        rel="stylesheet"
        href="resource://devtools/client/settings/components/devtools-settings-local-mode.css"
      />
      ${descriptorFront.isLocalTab
        ? nothing
        : html`<moz-message-bar
            type="info"
            data-l10n-id="devtools-settings-local-mode-local-only"
          ></moz-message-bar>`}
      <ul>
        ${repeat(
          this._mappings,
          mapping => mapping.prefPrefix,
          mapping => this.#renderMapping(mapping)
        )}
      </ul>
      <moz-button
        data-l10n-id="devtools-settings-local-mode-add"
        @click=${this.#addMapping}
      ></moz-button>
    `;
  }
}

/** @param {string} path */
function folderExists(path) {
  try {
    return new FileUtils.File(path).exists();
  } catch (error) {
    return false;
  }
}

customElements.define(
  "devtools-settings-local-mode",
  DevToolsSettingsLocalMode
);
