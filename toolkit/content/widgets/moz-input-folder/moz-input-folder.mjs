/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-input-folder.tagmap.d.ts" />

import { html, classMap, styleMap } from "../vendor/lit.all.mjs";
import { MozInputText } from "chrome://global/content/elements/moz-input-text.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozInputFolder.ftl");

/**
 * An input folder custom element.
 *
 * @tagname moz-input-folder
 *
 * @property {string} label - The text of the label element
 * @property {string} name - The name of the input control
 * @property {string} value - The path to the selected folder
 * @property {boolean} disabled - The disabled state of the component
 * @property {string} iconSrc - The src for an optional icon in the label
 * @property {string} description - The text for the description element that helps describe the input control
 * @property {string} supportPage - Name of the SUMO support page to link to.
 * @property {string} placeholder - Text to display when the input has no value.
 * @property {string} displayValue - The value of the input control if it's different from the component value.
 * @property {string} dialogTitle - Text to display as a file picker dialog title.
 * @property {object} folder - The file object that represents the selected folder.
 * @property {string} title - The title attribute, mapped onto the inner input.
 */

export class MozInputFolder extends MozInputText {
  #folder;

  static properties = {
    displayValue: { type: String },
    dialogTitle: { type: String, fluent: true },
    _inputIconSrc: { type: String, state: true },
  };

  static queries = {
    chooseFolderButtonEl: "#choose-folder-button",
  };

  constructor() {
    super();
    this.readonly = true;
    this.displayValue = "";
    this.dialogTitle = "";
    this._inputIconSrc = "";
    this.#folder = null;
  }

  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);

    if (changedProperties.has("readonly")) {
      this.readonly = true;
    }
    if (changedProperties.has("value")) {
      if (this.value == "") {
        this.#folder = null;
        this._inputIconSrc = "";
      } else if (!this.#folder || this.value != this.#folder.path) {
        let currentValue = this.value;
        this.getFolderFromPath(this.value).then(folder => {
          if (this.value === currentValue) {
            this.#folder = folder;
            this._inputIconSrc = this.getInputIconSrc(this.#folder);
          }
        });
      } else {
        this._inputIconSrc = this.getInputIconSrc(this.#folder);
      }
    }
  }

  get folder() {
    return this.#folder;
  }

  hasServices() {
    // Safely check for Services without throwing a ReferenceError.
    return typeof Services !== "undefined";
  }

  async getFolderFromPath(path) {
    if (
      Cu.isInAutomation &&
      Services.appinfo.OS === "WINNT" &&
      path?.includes("/")
    ) {
      console.error(
        `moz-input-folder: path contains forward slashes: "${path}"`,
        new Error().stack
      );
    }

    let folder = null;
    try {
      // nsIFile.initWithPath() accepts both forward and backward slashes and
      // normalizes to platform-native format (backslashes on Windows).
      const file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
      file.initWithPath(path);
      folder = await IOUtils.getDirectory(file.path);
    } catch (e) {
      //Not a valid path
      console.error(
        "The error occurred while attempting to get directory from the moz-input-folder value"
      );
    }

    return folder;
  }

  getInputIconSrc(folder) {
    if (!folder || !this.hasServices()) {
      let defaultIconSrc = "chrome://global/skin/icons/folder.svg";
      return defaultIconSrc;
    }

    let fph = Services.io
      .getProtocolHandler("file")
      .QueryInterface(Ci.nsIFileProtocolHandler);
    let iconUrlSpec = fph.getURLSpecFromDir(folder);
    let inputIconSrc = "moz-icon://" + iconUrlSpec + "?size=16";
    return inputIconSrc;
  }

  async openFolderPicker() {
    let folderPicker = Cc["@mozilla.org/filepicker;1"].createInstance(
      Ci.nsIFilePicker
    );
    let mode = Ci.nsIFilePicker.modeGetFolder;
    folderPicker.init(window.browsingContext, this.dialogTitle, mode);
    folderPicker.appendFilters(Ci.nsIFilePicker.filterAll);

    if (this.#folder && (await IOUtils.exists(this.#folder.path))) {
      folderPicker.displayDirectory = this.#folder;
    }

    let result = await new Promise(resolve => folderPicker.open(resolve));
    if (
      result != Ci.nsIFilePicker.returnOK ||
      this.value == folderPicker.file.path
    ) {
      if (Cu.isInAutomation) {
        // Dispatch a test-only event so we can tell that the dialog is closing.
        this.dispatchEvent(new CustomEvent("moz-input-folder-picker-close"));
      }

      return;
    }

    this.#folder = folderPicker.file;
    this.value = this.#folder.path;

    this.dispatchEvent(new Event("input", { bubbles: true }));
    this.dispatchEvent(new Event("change", { bubbles: true }));
  }

  inputStylesTemplate() {
    return html`<link
      rel="stylesheet"
      href="chrome://global/content/elements/moz-input-folder.css"
    />`;
  }

  inputTemplate() {
    let inputValue = this.displayValue || this.value;
    let classes, styles;
    if (this._inputIconSrc) {
      classes = classMap({
        "with-icon": true,
      });
      styles = styleMap({
        "--input-background-icon": `url(${this._inputIconSrc})`,
      });
    }

    return html`
      <div class="container">
        ${super.inputTemplate({ classes, styles, inputValue })}
        <moz-button
          id="choose-folder-button"
          data-l10n-id="choose-folder-button"
          data-l10n-attrs="accesskey"
          ?disabled=${this.disabled || this.parentDisabled}
          @click=${this.openFolderPicker}
        ></moz-button>
        <slot name="actions"></slot>
      </div>
    `;
  }
}
customElements.define("moz-input-folder", MozInputFolder);
