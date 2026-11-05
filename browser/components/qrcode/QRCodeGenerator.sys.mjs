/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * QR Code Generator with Firefox logo overlay
 * This module generates QR codes with the Firefox logo in the center
 * Uses a worker thread for QR generation to avoid blocking the main thread
 */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "QRCodeGenerator",
    maxLogLevel: Services.prefs.getBoolPref("browser.qrcode.log", false)
      ? "Debug"
      : "Warn",
  });
});

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

ChromeUtils.defineESModuleGetters(lazy, {
  QRCodeWorker: "moz-src:///browser/components/qrcode/QRCodeWorker.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "embedLogo",
  "browser.shareqrcode.embed_logo",
  true
);

export const QRCodeGenerator = {
  /**
   * Generate a QR code for the given URL, optionally with a Firefox logo
   * overlay. The logo is embedded only when the
   * browser.shareqrcode.embed_logo pref is true.
   *
   * @param {string} url - The URL to encode
   * @returns {Promise<string>} - Data URI of the QR code PNG
   */
  async generateQRCode(url) {
    const worker = new lazy.QRCodeWorker();
    try {
      const dataURI = await worker.generateFullQRCode(url, lazy.embedLogo);
      lazy.logConsole.debug("QRCode worker generated full QR code");
      return dataURI;
    } finally {
      try {
        await worker.terminate();
        lazy.logConsole.debug("QRCode worker terminated successfully");
      } catch (e) {
        lazy.logConsole.warn("Failed to terminate QRCode worker:", e);
      }
    }
  },
};
