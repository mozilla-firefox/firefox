/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  Ci.nsIIDNService
);

const QRCodeDialog = {
  _url: null,
  _qrCodeDataURI: null,
  _hideActions: false,
  _titleL10nId: null,

  /** Initializes the dialog from window arguments. */
  init() {
    if (window.arguments && window.arguments[0]) {
      const params = window.arguments[0];
      this._url = params.url;
      this._qrCodeDataURI = params.qrCodeDataURI;
      this._hideActions = params.hideActions;
      this._titleL10nId = params.titleL10nId;
    }

    document.mozSubdialogReady = this.setupDialog();

    const copyButton = document.getElementById("copy-button");
    document.getElementById("actions").hidden = this._hideActions;

    document.subDialogSetDefaultFocus = () =>
      this._hideActions
        ? document.getElementById("close-button").focus()
        : copyButton.focus();

    copyButton.addEventListener("click", () => this.copyImage());
    document
      .getElementById("save-button")
      .addEventListener("click", () => this.saveImage());
    document
      .getElementById("close-button")
      .addEventListener("click", () => window.close());

    if (!this._hideActions) {
      document.addEventListener("keydown", event => {
        if (
          event.key === "Enter" &&
          !event.defaultPrevented &&
          !event.target.closest("moz-button")
        ) {
          event.preventDefault();
          this.copyImage();
        }
      });
    }
  },

  /**
   * Populates the dialog with the QR code image and URL, or shows an error
   * if the QR code data URI is missing.
   */
  async setupDialog() {
    const successContainer = document.getElementById("success-container");

    if (this._titleL10nId) {
      const dialogTitle = document.getElementById("dialog-title");
      const windowTitle = document.getElementById("window-title");
      document.l10n.setAttributes(dialogTitle, this._titleL10nId);
      document.l10n.setAttributes(windowTitle, this._titleL10nId);
      await document.l10n.translateElements([dialogTitle, windowTitle]);
    }

    if (!this._qrCodeDataURI) {
      this.showFeedback("error", "qrcode-panel-error");
      return;
    }

    const imageElement = document.getElementById("qrcode-image");
    imageElement.src = this._qrCodeDataURI;

    successContainer.hidden = false;

    const urlElement = document.getElementById("qrcode-url");
    urlElement.textContent = this._url;
    urlElement.title = this._url;
  },

  /**
   * Shows a feedback message bar in the dialog and resizes the dialog to fit.
   *
   * @param {string} type - The message bar type ("success" or "error").
   * @param {string} l10nId - The Fluent localization ID for the message.
   */
  async showFeedback(type, l10nId) {
    let bar = document.getElementById("feedback-bar");
    if (!bar) {
      bar = document.createElement("moz-message-bar");
      bar.id = "feedback-bar";
      bar.setAttribute("role", "alert");
      bar.setAttribute("data-l10n-attrs", "message");
      bar.setAttribute("dismissable", "");
      bar.addEventListener("message-bar:user-dismissed", () => {
        requestAnimationFrame(() => window.resizeDialog?.());
      });
      let content = document.getElementById("qrcode-dialog-content");
      content.appendChild(bar);
    }
    bar.type = type;
    document.l10n.setAttributes(bar, l10nId);
    await bar.updateComplete;
    window.resizeDialog?.();
  },

  /**
   * Strips the data URI prefix from the stored QR code data URI, base64-decodes
   * the payload, and returns the raw PNG bytes as a Uint8Array.
   *
   * @returns {Uint8Array} The decoded PNG image bytes.
   * @throws {Error} If the data URI is missing or not a base64-encoded PNG.
   */
  decodeDataURI() {
    const dataPrefix = "data:image/png;base64,";
    if (!this._qrCodeDataURI?.startsWith(dataPrefix)) {
      throw new Error("Invalid QR code image data");
    }

    return Uint8Array.fromBase64(this._qrCodeDataURI.slice(dataPrefix.length));
  },

  /** Copies the QR code image to the clipboard as a PNG. */
  async copyImage() {
    try {
      const qrCodeBytes = this.decodeDataURI();
      const item = new ClipboardItem({
        "image/png": new Blob([qrCodeBytes], { type: "image/png" }),
      });
      await navigator.clipboard.write([item]);
      this.showFeedback("success", "qrcode-copy-success");
    } catch (error) {
      console.error("Failed to copy QR code:", error);
      this.showFeedback("error", "qrcode-copy-error");
    }
  },

  /** Saves the QR code image as a PNG file via the standard download flow. */
  async saveImage() {
    let domain = "";
    let uri;
    try {
      uri = Services.io.newURI(this._url);
      domain = lazy.IDNService.domainToDisplay(
        Services.eTLD.getSchemelessSite(uri)
      );
    } catch (e) {
      if (uri) {
        domain = uri.host;
      }
    }

    const filenameMessage = domain
      ? { id: "qrcode-save-filename-with-domain-base", args: { domain } }
      : "qrcode-save-filename-base";
    const [defaultFilename] = await document.l10n.formatValues([
      filenameMessage,
    ]);
    // Append .png so the QR code for "firefox.com" defaults to
    // "qrcode-firefox.com.png".
    const filename = `${defaultFilename}.png`;

    const chromeWindow = window.browsingContext.topChromeWindow;
    chromeWindow.internalSave(
      this._qrCodeDataURI,
      null, // originalURL
      null, // document
      filename,
      null, // content disposition
      "image/png",
      true, // bypass cache, since it's a data: URI
      "SaveImageTitle",
      null, // chosen data
      null, // referrer info
      null, // cookie jar settings
      null, // initiating document
      false, // don't skip the prompt for where to save
      null, // cache key
      lazy.PrivateBrowsingUtils.isWindowPrivate(chromeWindow),
      Services.scriptSecurityManager.getSystemPrincipal()
    );
  },
};

// Exposed on `window` for browser-chrome tests.
window.QRCodeDialog = QRCodeDialog;

window.addEventListener("DOMContentLoaded", () => {
  QRCodeDialog.init();
});
