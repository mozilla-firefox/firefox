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
 * @import { Mapping, MappingState, LocalModeMappingsModule } from "../types/local-mode.d.ts"
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
    /** @type {MappingState[]} */
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
    this._mappings = LocalModeMappings.getAllMappings().map(mapping => ({
      ...mapping,
      folderExists: folderExists(mapping.path),
    }));
  };

  /**
   * Scrolls to a mapping and highlights it.
   *
   * @param {string} prefPrefix The mapping's pref prefix, e.g. "devtools.local-mode.mappings.0."
   */
  async highlightMapping(prefPrefix) {
    await this.updateComplete;
    const item = this.shadowRoot?.querySelector(
      `li[data-pref-prefix="${prefPrefix}"]`
    );
    if (!item) {
      return;
    }
    item.scrollIntoView({ block: "center" });
    item.classList.add("highlighted");
  }

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
   * @param {string} [currentPath] The folder to open the picker in.
   * @returns {Promise<string | null>} The chosen path, or null if cancelled.
   */
  async #chooseFolder(mappingOrigin, currentPath) {
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
    if (currentPath && folderExists(currentPath)) {
      filePicker.displayDirectory = new FileUtils.File(currentPath);
    }
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

  /**
   * @param {Mapping} mapping
   * @param {string} value
   * @returns {string | null} The ID of the error message, if the origin is invalid.
   */
  #computeOriginError(mapping, value) {
    if (!value || value == mapping.origin) {
      return null;
    }
    if (this._mappings.some(other => other.origin == value)) {
      return "devtools-settings-local-mode-origin-conflict";
    }
    if (!URL.canParse(`https://${value}`)) {
      return "devtools-settings-local-mode-origin-invalid";
    }
    return null;
  }

  /**
   * @param {EventTarget | null} target
   * @returns {MappingState | undefined}
   */
  #getMapping(target) {
    if (!Element.isInstance(target)) {
      return undefined;
    }
    const row = target.closest("li");
    if (!HTMLLIElement.isInstance(row)) {
      return undefined;
    }
    return this._mappings.find(
      mapping => mapping.prefPrefix == row.dataset.prefPrefix
    );
  }

  /** @param {Event} event */
  #originInput(event) {
    const input = event.currentTarget;
    if (!(input instanceof MozInputText)) {
      return;
    }
    const mapping = this.#getMapping(input);
    if (!mapping) {
      return;
    }
    mapping.draftOrigin = input.value;
    this.requestUpdate();
  }

  /** @param {Event} event */
  #originChange(event) {
    const input = event.currentTarget;
    if (!(input instanceof MozInputText)) {
      return;
    }
    const mapping = this.#getMapping(input);
    if (!mapping) {
      return;
    }
    if (input.value && !this.#computeOriginError(mapping, input.value)) {
      Services.prefs.setStringPref(mapping.prefPrefix + "origin", input.value);
    }
    this.#readMappings();
  }

  /** @param {Event} event */
  async #changeFolder(event) {
    const mapping = this.#getMapping(event.currentTarget);
    if (!mapping) {
      return;
    }
    const path = await this.#chooseFolder(mapping.origin, mapping.path);
    if (path) {
      Services.prefs.setStringPref(mapping.prefPrefix + "path", path);
    }
  }

  /** @param {Event} event */
  #revealFolder(event) {
    event.preventDefault();
    const mapping = this.#getMapping(event.currentTarget);
    if (!mapping) {
      return;
    }
    new FileUtils.File(mapping.path).reveal();
  }

  /** @param {Event} event */
  #navigateTo(event) {
    const mapping = this.#getMapping(event.currentTarget);
    if (!mapping || !this.toolbox) {
      return;
    }
    const { targetCommand } = this.toolbox.commands;
    targetCommand.navigateTo("https://" + mapping.origin);
  }

  /** @param {Event} event */
  #toggleMapping(event) {
    const mapping = this.#getMapping(event.currentTarget);
    if (!mapping) {
      return;
    }
    Services.prefs.setBoolPref(
      mapping.prefPrefix + "disabled",
      !mapping.disabled
    );
  }

  /** @param {Event} event */
  async #removeMapping(event) {
    const mapping = this.#getMapping(event.currentTarget);
    if (!mapping) {
      return;
    }
    const message = await l10n.formatValue(
      "devtools-settings-local-mode-confirm-removal",
      { mappingOrigin: mapping.origin }
    );
    if (!window.confirm(message ?? "")) {
      return;
    }
    Services.prefs.clearUserPref(mapping.prefPrefix + "origin");
    Services.prefs.clearUserPref(mapping.prefPrefix + "path");
    Services.prefs.clearUserPref(mapping.prefPrefix + "disabled");
  }

  /** @param {MappingState} mapping */
  #renderFolder(mapping) {
    return html`
      <div class="folder-row">
        <span data-l10n-id="devtools-settings-local-mode-folder"></span>
        <moz-button
          class="choose-folder"
          size="small"
          data-l10n-id="devtools-settings-local-mode-choose-folder"
          ?disabled=${mapping.disabled}
          @click=${this.#changeFolder}
        ></moz-button>
      </div>
      <a
        class="folder"
        href=${"file://" + mapping.path}
        @click=${this.#revealFolder}
        >${mapping.path}</a
      >
      ${mapping.folderExists
        ? nothing
        : html`<p
            class="folder-error"
            data-l10n-id="devtools-settings-local-mode-folder-invalid"
          ></p>`}
    `;
  }

  /** @param {MappingState} mapping */
  #renderMapping(mapping) {
    const displayedOrigin = mapping.draftOrigin ?? mapping.origin;
    const originError = this.#computeOriginError(mapping, displayedOrigin);
    return html`
      <li data-pref-prefix=${mapping.prefPrefix}>
        <div class="origin-row">
          <moz-input-text
            class="origin"
            data-l10n-id="devtools-settings-local-mode-origin"
            ?disabled=${mapping.disabled}
            .value=${displayedOrigin}
            @input=${this.#originInput}
            @change=${this.#originChange}
          ></moz-input-text>
          <moz-button
            class="navigate"
            type="icon"
            iconsrc="chrome://global/skin/icons/arrow-right.svg"
            data-l10n-id="devtools-settings-local-mode-navigate"
            ?disabled=${mapping.disabled}
            @click=${this.#navigateTo}
          ></moz-button>
        </div>
        ${originError
          ? html`<p class="origin-error" data-l10n-id=${originError}></p>`
          : nothing}
        ${this.#renderFolder(mapping)}
        <div class="actions">
          <moz-button
            class="toggle"
            size="small"
            data-l10n-id=${mapping.disabled
              ? "devtools-settings-local-mode-enable"
              : "devtools-settings-local-mode-disable"}
            @click=${this.#toggleMapping}
          ></moz-button>
          <moz-button
            class="remove"
            size="small"
            data-l10n-id="devtools-settings-local-mode-remove"
            @click=${this.#removeMapping}
          ></moz-button>
        </div>
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
