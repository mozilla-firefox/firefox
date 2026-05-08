/* Copyright 2026 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { AIFeature } from "chrome://global/content/ml/AIFeature.sys.mjs";
import { FEATURES } from "chrome://global/content/ml/EngineProcess.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  MLUninstallService: "chrome://global/content/ml/Utils.sys.mjs",
});

export class PdfJsGuessAltTextFeature extends AIFeature {
  /**
   * Returns the engine identifier for the PDF.js alt-text feature.
   *
   * @returns {string}
   */
  static get engineId() {
    return FEATURES[this.id].engineId;
  }

  /**
   * Returns the feature identifier for PDF.js alt-text generation.
   *
   * @returns {string}
   */
  static get id() {
    return "pdfjs-alt-text";
  }

  /**
   * Returns whether PDF.js alt-text generation exposes a distinct "Enabled"
   * AI Controls state that is meaningfully separate from "Available".
   *
   * @returns {boolean}
   */
  static get hasDistinctEnabledState() {
    // The PDF alt-text feature has a distinct opt-in toggle within the PDF settings.
    // It is not immediately enabled when the feature is "Available" and must still
    // be manually enabled by the user.
    return true;
  }

  /**
   * Returns whether the current device can run PDF.js alt-text generation.
   *
   * @returns {boolean}
   */
  static get canRunOnDevice() {
    // The alt-text generation model has no known restrictions based on device hardware.
    return true;
  }

  /**
   * Enables PDF.js alt-text generation.
   *
   * @returns {Promise<void>}
   */
  static async enable() {
    // Enable alt-text feature (regardless of method)
    Services.prefs.setBoolPref("pdfjs.enableAltText", true);
    // and enable guessing (ML-based) alt-text feature.
    Services.prefs.setBoolPref("pdfjs.enableGuessAltText", true);
    // Also allow model download.
    Services.prefs.setBoolPref("pdfjs.enableAltTextModelDownload", true);
  }

  /**
   * Blocks PDF.js alt-text generation and deletes its model artifacts.
   *
   * @returns {Promise<void>}
   */
  static async block() {
    Services.prefs.setBoolPref("pdfjs.enableGuessAltText", false);
    Services.prefs.setBoolPref("pdfjs.enableAltTextModelDownload", false);
    Services.prefs.setBoolPref("pdfjs.enableAltText", false);
    await this.uninstall();
  }

  /**
   * Makes PDF.js alt-text generation available and deletes its model artifacts.
   *
   * @returns {Promise<void>}
   */
  static async makeAvailable() {
    Services.prefs.setBoolPref("pdfjs.enableGuessAltText", false);
    Services.prefs.setBoolPref("pdfjs.enableAltTextModelDownload", false);
    // Set explicitly rather than clearing, so that a non-locked policy default
    // of "blocked" does not prevent the user from switching back to "available".
    Services.prefs.setBoolPref("pdfjs.enableAltText", true);
    await this.uninstall();
  }

  /**
   * Deletes the PDF.js alt-text model artifacts.
   *
   * @returns {Promise<void>}
   */
  static async uninstall() {
    const { engineId } = this;
    await lazy.MLUninstallService.uninstall({
      engineIds: [engineId],
      actor: engineId,
    });
  }

  /**
   * Returns whether PDF.js alt-text generation is actively enabled.
   *
   * @returns {boolean}
   */
  static get isEnabled() {
    return (
      Services.prefs.getBoolPref("browser.ml.enable", false) &&
      Services.prefs.getBoolPref("pdfjs.enableAltText", false) &&
      Services.prefs.getBoolPref("pdfjs.enableGuessAltText", false) &&
      Services.prefs.getBoolPref("pdfjs.enableAltTextModelDownload", false)
    );
  }

  /**
   * Returns whether PDF.js alt-text generation is allowed and able to participate
   * in the AI Controls settings.
   *
   * @returns {boolean}
   */
  static get isAllowed() {
    return Services.locale.appLocaleAsBCP47.substring(0, 2) === "en";
  }

  /**
   * Returns whether PDF.js alt-text generation is blocked via the AI Controls.
   *
   * @returns {boolean}
   */
  static get isBlocked() {
    return (
      !Services.prefs.getBoolPref("browser.ml.enable", false) ||
      !Services.prefs.getBoolPref("pdfjs.enableAltText", false)
    );
  }

  /**
   * Returns whether enterprise policy manages the PDF.js alt-text feature state
   * in such a way that the user cannot change its enabled state at runtime.
   *
   * @returns {boolean}
   */
  static get isManagedByPolicy() {
    return Services.prefs.prefIsLocked("pdfjs.enableAltText");
  }
}
