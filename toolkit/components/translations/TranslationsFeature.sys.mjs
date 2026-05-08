/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AIFeature } from "chrome://global/content/ml/AIFeature.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @typedef {object} Lazy
 * @property {typeof console} console
 * @property {typeof import("chrome://global/content/translations/TranslationsUtils.mjs").TranslationsUtils} TranslationsUtils
 * @property {typeof import("chrome://global/content/ml/EngineProcess.sys.mjs").EngineProcess} EngineProcess
 * @property {typeof import("chrome://global/content/translations/TranslationsTelemetry.sys.mjs").TranslationsTelemetry} TranslationsTelemetry
 * @property {typeof import("resource://gre/actors/TranslationsParent.sys.mjs").TranslationsParent} TranslationsParent
 * @property {boolean} translationsEnabledPref
 */

/** @type {Lazy} */
const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  EngineProcess: "chrome://global/content/ml/EngineProcess.sys.mjs",
  TranslationsTelemetry:
    "chrome://global/content/translations/TranslationsTelemetry.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.translations.logLevel",
    prefix: "Translations",
  });
});

const AI_CONTROL_TRANSLATIONS_PREF = "browser.ai.control.translations";
const TRANSLATIONS_ENABLE_PREF = "browser.translations.enable";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "translationsEnabledPref",
  TRANSLATIONS_ENABLE_PREF,
  false
);

/**
 * AIFeature implementation for translations.
 */
export class TranslationsFeature extends AIFeature {
  /**
   * Feature identifier for translations.
   *
   * @returns {string}
   */
  static get id() {
    return "translations";
  }

  /**
   * Returns whether Translations exposes a distinct "Enabled" AI Controls
   * state.
   *
   * @returns {boolean}
   */
  static get hasDistinctEnabledState() {
    // Translations does not have a distinct "Enabled" state. When Translations
    // is "Available," it is immediately usable, and there is no separate
    // experience to enable further functionality.
    return false;
  }

  /**
   * Returns whether the Translations feature is enabled.
   *
   * @returns {boolean}
   */
  static get isEnabled() {
    return TranslationsFeature.isAllowed && lazy.translationsEnabledPref;
  }

  /**
   * Returns whether the Translations feature is blocked.
   *
   * @returns {boolean}
   */
  static get isBlocked() {
    return !lazy.translationsEnabledPref;
  }

  /**
   * Returns whether the Translations feature is allowed.
   *
   * @returns {boolean}
   */
  static get isAllowed() {
    return true;
  }

  /**
   * Returns whether the current device can run Translations.
   *
   * @returns {boolean}
   */
  static get canRunOnDevice() {
    return lazy.TranslationsParent.getIsTranslationsEngineSupported();
  }

  /**
   * Returns whether enterprise policy manages the Translations feature state.
   *
   * @returns {boolean}
   */
  static get isManagedByPolicy() {
    return (
      Services.prefs.prefIsLocked(TRANSLATIONS_ENABLE_PREF) ||
      Services.prefs.prefIsLocked(AI_CONTROL_TRANSLATIONS_PREF)
    );
  }

  /**
   * Makes the Translations feature available and removes artifacts.
   *
   * @returns {Promise<void>}
   */
  static async makeAvailable() {
    if (TranslationsFeature.isManagedByPolicy) {
      throw new Error(
        "Cannot make Translations available: controlled by enterprise policy"
      );
    }

    Services.prefs.setStringPref(AI_CONTROL_TRANSLATIONS_PREF, "available");
    Services.prefs.setBoolPref(TRANSLATIONS_ENABLE_PREF, true);

    await Promise.allSettled([
      lazy.EngineProcess.destroyTranslationsEngine(),
      TranslationsFeature.#deleteAllArtifacts(),
    ]);
    lazy.TranslationsTelemetry.onFeatureReset();
  }

  /**
   * Enables the Translations feature.
   *
   * @returns {Promise<void>}
   */
  static async enable() {
    if (TranslationsFeature.isManagedByPolicy) {
      throw new Error(
        "Cannot enable Translations: controlled by enterprise policy"
      );
    }

    Services.prefs.setStringPref(AI_CONTROL_TRANSLATIONS_PREF, "enabled");
    Services.prefs.setBoolPref(TRANSLATIONS_ENABLE_PREF, true);
    lazy.TranslationsTelemetry.onFeatureEnable();
  }

  /**
   * Blocks the Translations feature and removes artifacts.
   *
   * @returns {Promise<void>}
   */
  static async block() {
    if (TranslationsFeature.isManagedByPolicy) {
      throw new Error(
        "Cannot block Translations: controlled by enterprise policy"
      );
    }

    Services.prefs.setStringPref(AI_CONTROL_TRANSLATIONS_PREF, "blocked");
    Services.prefs.setBoolPref(TRANSLATIONS_ENABLE_PREF, false);

    await Promise.allSettled([
      lazy.EngineProcess.destroyTranslationsEngine(),
      TranslationsFeature.#deleteAllArtifacts(),
    ]);
    lazy.TranslationsTelemetry.onFeatureDisable();
  }

  /**
   * Deletes translations artifacts.
   *
   * @returns {Promise<void>}
   */
  static async #deleteAllArtifacts() {
    try {
      await lazy.TranslationsUtils.deleteAllLanguageFiles();
    } catch (error) {
      lazy.console.error(
        "Failed to delete Translations language files.",
        error
      );
    }
  }
}
