/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {import("./translations").LanguagePair} LanguagePair
 */

/**
 * @typedef {object} Lazy
 * @property {typeof console} console
 * @property {typeof import("resource://services-settings/RemoteSettingsClient.sys.mjs").RemoteSettingsClient} RemoteSettingsClient
 */

/** @type {Lazy} */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.translations.logLevel",
    logModule: "Translations",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettingsClient:
    "resource://services-settings/RemoteSettingsClient.sys.mjs",
});

/**
 * A set of global static utility functions that are useful throughout the
 * Translations ecosystem within the Firefox code base.
 */
export class TranslationsUtils {
  /**
   * The name of the Remote Settings collection that hosts
   * Translations Model Records for the current version of Firefox.
   */
  static get translationsModelsCollectionName() {
    return "translations-models-v2";
  }

  /**
   * The name of the Remote Settings collection that hosts
   * Translations WASM Records for the current version of Firefox.
   */
  static get translationsWasmCollectionName() {
    return "translations-wasm-v2";
  }

  /**
   * Checks if the language tag string parses as a valid BCP-47 language tag.
   *
   * @param {string} langTag - A BCP-47 language tag.
   * @returns {boolean} - True if the given language tag parses or false if it does not.
   */
  static isLangTagValid(langTag) {
    if (!langTag) {
      return false;
    }

    try {
      new Intl.Locale(langTag);
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Normalizes the language tag for comparison within the Translations ecosystem.
   *
   * Prefers to compare languages with a script tag if one is available, only resorting
   * to returning the language tag in isolation if a script tag could not be derived.
   *
   * @param {string} langTag - A BCP-47 language tag.
   * @returns {string} - A BCP-47 language tag normalized for Translations.
   */
  static #normalizeLangTag(langTag) {
    let locale = new Intl.Locale(langTag);

    if (!locale.script) {
      // Attempt to populate a script tag via likely subtags.
      locale = locale.maximize();
    }

    if (locale.script) {
      // If the locale has a script, use it.
      return `${locale.language}-${locale.script}`;
    }

    return locale.language;
  }

  /**
   * Compares two BCP-47 language tags for Translations compatibility.
   *
   * If one language tag belongs to one of our models, and the other
   * language tag is determined to be a match, then it is determined
   * that the model is compatible for translation with that language.
   *
   * @param {string} lhsLangTag - The left-hand-side language tag to compare.
   * @param {string} rhsLangTag - The right-hand-side language tag to compare.
   *
   * @returns {boolean}
   *  `true`  if the language tags are the same string, or if valid tags match after canonicalization or normalization.
   *  `false` if either tag is empty, or if they are invalid and not the same string, or if valid tags do not match.
   *
   * @see https://datatracker.ietf.org/doc/html/rfc5646#appendix-A
   */
  static langTagsMatch(lhsLangTag, rhsLangTag) {
    if (!lhsLangTag || !rhsLangTag) {
      return false;
    }

    if (lhsLangTag === rhsLangTag) {
      // A simple direct match.
      return true;
    }

    try {
      // The language tags did not match, so attempt to canonicalize them and check again.
      const canonicalLhsLangTag = Intl.getCanonicalLocales(lhsLangTag)[0];
      const canonicalRhsLangTag = Intl.getCanonicalLocales(rhsLangTag)[0];

      if (canonicalLhsLangTag === canonicalRhsLangTag) {
        return true;
      }

      if (
        canonicalLhsLangTag.split("-")[0] !== canonicalRhsLangTag.split("-")[0]
      ) {
        // The language components of the canonicalized tags do not match,
        // so there is no need to further normalize them and compare.
        return false;
      }

      return (
        TranslationsUtils.#normalizeLangTag(canonicalLhsLangTag) ===
        TranslationsUtils.#normalizeLangTag(canonicalRhsLangTag)
      );
    } catch {
      // At least one of the locales is not valid, just continue on to return false.
    }

    return false;
  }

  /**
   * Serializes a language pair into a unique key that is human readable. This is useful
   * for caching, deduplicating, and logging.
   *
   * e.g.
   *   "en -> fr"
   *   "en -> fr,base"
   *   "zh-Hans,tiny -> fr,base"
   *
   * @param {LanguagePair} languagePair
   */
  static serializeLanguagePair({
    sourceLanguage,
    targetLanguage,
    sourceVariant,
    targetVariant,
  }) {
    let key = sourceLanguage;
    if (sourceVariant) {
      key += `,${sourceVariant}`;
    }
    key += ` -> ${targetLanguage}`;
    if (targetVariant) {
      key += `,${targetVariant}`;
    }
    return key;
  }

  /**
   * Deletes all translations language model files.
   *
   * @returns {Promise<string[]>} - A list of record IDs.
   */
  static async deleteAllLanguageFiles() {
    const { RemoteSettings } = ChromeUtils.importESModule(
      "resource://services-settings/remote-settings.sys.mjs"
    );
    const client = RemoteSettings(
      TranslationsUtils.translationsModelsCollectionName
    );

    try {
      await client.attachments.deleteAll();
    } catch (error) {
      if (!(error instanceof lazy.RemoteSettingsClient.EmptyDatabaseError)) {
        lazy.console.error(
          "Failed to delete Translations language files.",
          error
        );
      }
    }

    try {
      const records = await client.get({
        syncIfEmpty: false,
        loadDumpIfNewer: false,
      });
      return records.map(record => record.id);
    } catch (error) {
      lazy.console.error("Failed to list Translations model records.", error);
      return [];
    }
  }
}
