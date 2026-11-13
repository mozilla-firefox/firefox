/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

if (Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT) {
  // This check ensures that the `mockable` API calls can be consisently mocked in tests.
  // If this requirement needs to be eased, please ensure the test logic remains valid.
  throw new Error("This code is assumed to run in the parent process.");
}

/**
 * Attempts to find an appropriate langpack for a given language. The async function
 * is infallible, but may not return a langpack.
 *
 * @returns {Promise<{
 *   langPack: LangPack | null,
 *   langPackDisplayName: string | null
 * }>}
 */
async function negotiateLangPackForLanguageMismatch() {
  const localeInfo = getAppAndSystemLocaleInfo();
  /** @type {Awaited<ReturnType<negotiateLangPackForLanguageMismatch>>} */
  const nullResult = {
    langPack: null,
    langPackDisplayName: null,
  };
  if (!localeInfo.systemLocale) {
    // The system locale info was not valid.
    return nullResult;
  }

  /**
   * Fetch the available langpacks from AMO.
   *
   * @type {Array<LangPack>}
   */
  const availableLangpacks = await mockable.getAvailableLangpacks();
  if (!availableLangpacks) {
    return nullResult;
  }

  /**
   * Figure out a langpack to recommend.
   *
   * @type {LangPack | null}
   */
  const langPack =
    // First look for a langpack that matches the baseName, which may include a script.
    // e.g. system "fr-FR" matches langpack "fr-FR"
    //      system "en-GB" matches langpack "en-GB".
    //      system "zh-Hant-CN" matches langpack "zh-Hant-CN".
    availableLangpacks.find(
      ({ target_locale }) => target_locale === localeInfo.systemLocale.baseName
    ) ||
    // Next try matching language and region while excluding script
    // e.g. system "zh-Hant-TW" matches langpack "zh-TW" but not "zh-CN".
    availableLangpacks.find(
      ({ target_locale }) =>
        target_locale ===
        `${localeInfo.systemLocale.language}-${localeInfo.systemLocale.region}`
    ) ||
    // Next look for langpacks that just match the language.
    // e.g. system "fr-FR" matches langpack "fr".
    //      system "en-AU" matches langpack "en".
    availableLangpacks.find(
      ({ target_locale }) => target_locale === localeInfo.systemLocale.language
    ) ||
    // Next look for a langpack that matches the language, but not the region.
    // e.g. "es-CL" (Chilean Spanish) as a system language matching
    //      "es-ES" (European Spanish)
    availableLangpacks.find(({ target_locale }) =>
      target_locale.startsWith(`${localeInfo.systemLocale.language}-`)
    ) ||
    null;

  if (!langPack) {
    return nullResult;
  }

  return {
    langPack,
    langPackDisplayName: Services.intl.getLocaleDisplayNames(
      undefined,
      [langPack.target_locale],
      { preferNative: true }
    )[0],
  };
}

// If a langpack is being installed, allow blocking on that.
let installingLangpack = new Map();

/**
 * @typedef {object} LangPack
 * @property {string} id
 * @property {string} hash
 * @property {string} target_locale
 * @property {string} url
 * @property {string} version
 */

/**
 * Ensure that a given langpack is installed.
 *
 * @param {LangPack} langPack
 * @param {string} source Where the install is happening for telemetry.
 * @returns {Promise<boolean>} Success or failure.
 */
function ensureLangPackInstalled(langPack, source = "about:welcome") {
  if (!langPack) {
    throw new Error("Expected a LangPack to install.");
  }
  // Make sure any outstanding calls get resolved before attempting another call.
  // This guards against any quick page refreshes attempting to install the langpack
  // twice.
  const inProgress = installingLangpack.get(langPack.hash);
  if (inProgress) {
    return inProgress;
  }
  const promise = _ensureLangPackInstalledImpl(langPack, source);
  installingLangpack.set(langPack.hash, promise);
  promise
    .finally(() => {
      installingLangpack.delete(langPack.hash);
    })
    .catch(() => {});
  return promise;
}

/**
 * @param {LangPack} langPack
 * @returns {boolean} Success or failure.
 */
async function _ensureLangPackInstalledImpl(langPack, source) {
  const availablelocales = await getAvailableLocales();
  if (availablelocales.includes(langPack.target_locale)) {
    // The langpack is already installed.
    return true;
  }

  return mockable.installLangPack(langPack, source);
}

/**
 * These are all functions with side effects or configuration options that should be
 * mockable for tests.
 */
const mockable = {
  /**
   * @returns {LangPack[] | null}
   */
  async getAvailableLangpacks() {
    try {
      return lazy.AddonRepository.getAvailableLangpacks();
    } catch (error) {
      console.error(
        `Failed to get the list of available language packs: ${error?.message}`
      );
      return null;
    }
  },

  /**
   * Install locale/dictionary add-on from URL, generally AMO.
   *
   * @param {string} url The URL.
   * @param {object} options
   * @param {string} options.source
   *  Where the download was triggered e.g. about:welcome or about:preferences
   * @param {string} options.id Verify the download against this ID.
   * @param {string} [options.hash] Verify the download against this hash.
   * @param {string} options.type Verify the download against this type.
   * @param {string} options.version Verify the download against this version.
   */
  async _installAddon(url, { source, id, hash, type, version }) {
    if (!(id && type && version)) {
      console.error("Missing addon details, locale/dictionary not installed");
      return false;
    }

    if (!(type === "locale" || type === "dictionary")) {
      console.error("Invalid type provided, locale/dictionary not installed");
      return false;
    }

    let install;
    try {
      install = await lazy.AddonManager.getInstallForURL(url, {
        id,
        hash,
        type,
        version,
        telemetryInfo: { source },
      });
    } catch (error) {
      console.error(error);
      return false;
    }

    try {
      await install.install();
    } catch (error) {
      console.error(error);
      return false;
    }
    return true;
  },

  /**
   * Use the AddonManager to install an addon from the URL.
   *
   * @param {LangPack} langPack
   * @param {string} source Where the install is happening for telemetry.
   */
  async installLangPack(langPack, source = "about:welcome") {
    const langPackInstalled = await this._installAddon(langPack.url, {
      source,
      id: langPack.id,
      hash: langPack.hash,
      type: "locale",
      version: langPack.version,
    });
    if (langPackInstalled) {
      // Install the dictionaries in the background without waiting.
      this.installDictionariesForLocale(langPack.target_locale, source);
    }
    return langPackInstalled;
  },

  async dictionaryIdsForLocale(locale) {
    let entries = await lazy.RemoteSettings("language-dictionaries").get({
      filters: { id: locale },
    });
    if (entries.length) {
      return entries[0].dictionaries;
    }
    return [];
  },

  async installDictionariesForLocale(locale, source) {
    try {
      let ids = await this.dictionaryIdsForLocale(locale);
      let addonInfos = await lazy.AddonRepository.getAddonsByIDs(ids);
      await Promise.all(
        addonInfos.map(info =>
          this._installAddon(info.sourceURI.spec, {
            source,
            id: info.id,
            type: "dictionary",
            version: info.version,
          })
        )
      );
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Returns the available locales, including the fallback locale, which may not include
   * all of the resources, in cases where the defaultLocale is not "en-US".
   *
   * @returns {string[]}
   */
  getAvailableLocalesIncludingFallback() {
    return Services.locale.availableLocales;
  },

  /**
   * @returns {string}
   */
  getDefaultLocale() {
    return Services.locale.defaultLocale;
  },

  /**
   * @returns {string}
   */
  getLastFallbackLocale() {
    return Services.locale.lastFallbackLocale;
  },

  /**
   * @returns {string}
   */
  getAppLocaleAsBCP47() {
    return Services.locale.appLocaleAsBCP47;
  },

  /**
   * @returns {string}
   */
  getSystemLocale() {
    // Allow the system locale to be overridden for manual testing.
    const systemLocaleOverride = Services.prefs.getCharPref(
      "intl.multilingual.aboutWelcome.systemLocaleOverride",
      null
    );
    if (systemLocaleOverride) {
      try {
        // If the locale can't be parsed, ignore the pref.
        new Services.intl.Locale(systemLocaleOverride);
        return systemLocaleOverride;
      } catch (_error) {}
    }

    const osPrefs = Cc["@mozilla.org/intl/ospreferences;1"].getService(
      Ci.mozIOSPreferences
    );
    return osPrefs.systemLocale;
  },

  /**
   * @param {string[]} locales The BCP 47 locale identifiers.
   */
  setRequestedAppLocales(locales) {
    Services.locale.requestedLocales = locales;
  },
};

/**
 * This function is really only setting `Services.locale.requestedLocales`, but it's
 * using the `mockable` object to allow this behavior to be mocked in tests.
 *
 * @param {string[]} locales The BCP 47 locale identifiers.
 */
function setRequestedAppLocales(locales) {
  mockable.setRequestedAppLocales(locales);
}

/**
 * A serializable Intl.Locale.
 *
 * @typedef StructuredLocale
 * @type {object}
 * @property {string} baseName
 * @property {string} language
 * @property {string} region
 */

/**
 * In telemetry data, some of the system locales show up as blank. Guard against this
 * and any other malformed locale information provided by the system by wrapping the call
 * into a catch/try.
 *
 * @param {string} localeString
 * @returns {StructuredLocale | null}
 */
function getStructuredLocaleOrNull(localeString) {
  try {
    const locale = new Services.intl.Locale(localeString);
    return {
      baseName: locale.baseName,
      language: locale.language,
      region: locale.region,
    };
  } catch (_err) {
    return null;
  }
}

/**
 * Determine the system and app locales, and how much the locales match.
 *
 * @returns {{
 *  systemLocale: StructuredLocale,
 *  appLocale: StructuredLocale,
 *  matchType: "unknown" | "language-mismatch" | "region-mismatch" | "match",
 * }}
 */
function getAppAndSystemLocaleInfo() {
  // Convert locale strings into structured locale objects.
  const systemLocaleRaw = mockable.getSystemLocale();
  const appLocaleRaw = mockable.getAppLocaleAsBCP47();

  const systemLocale = getStructuredLocaleOrNull(systemLocaleRaw);
  const appLocale = getStructuredLocaleOrNull(appLocaleRaw);

  let matchType = "unknown";
  if (systemLocale && appLocale) {
    if (systemLocale.language !== appLocale.language) {
      matchType = "language-mismatch";
    } else if (systemLocale.region !== appLocale.region) {
      matchType = "region-mismatch";
    } else {
      matchType = "match";
    }
  }

  // Live reloading with bidi switching may not be supported.
  let canLiveReload = null;
  if (systemLocale && appLocale) {
    const systemDirection = Services.intl.getScriptDirection(
      systemLocale.language
    );
    const appDirection = Services.intl.getScriptDirection(appLocale.language);
    const supportsBidiSwitching = Services.prefs.getBoolPref(
      "intl.multilingual.liveReloadBidirectional",
      false
    );
    canLiveReload = systemDirection === appDirection || supportsBidiSwitching;
  }
  return {
    // Return the Intl.Locale in a serializable form.
    systemLocaleRaw,
    systemLocale,
    appLocaleRaw,
    appLocale,
    matchType,
    canLiveReload,

    // These can be used as Fluent message args.
    displayNames: {
      systemLanguage: systemLocale
        ? Services.intl.getLocaleDisplayNames(
            undefined,
            [systemLocale.baseName],
            { preferNative: true }
          )[0]
        : null,
      appLanguage: appLocale
        ? Services.intl.getLocaleDisplayNames(undefined, [appLocale.baseName], {
            preferNative: true,
          })[0]
        : null,
    },
  };
}

/**
 * Filter the lastFallbackLocale from availableLocales if it doesn't have all
 * of the needed strings.
 *
 * When the lastFallbackLocale isn't the defaultLocale, then by default only
 * fluent strings are included. To fully use that locale you need the langpack
 * to be installed, so if it isn't installed remove it from availableLocales.
 */
async function getAvailableLocales() {
  const availableLocales = mockable.getAvailableLocalesIncludingFallback();
  const defaultLocale = mockable.getDefaultLocale();
  const lastFallbackLocale = mockable.getLastFallbackLocale();
  // If defaultLocale isn't lastFallbackLocale, then we still need the langpack
  // for lastFallbackLocale for it to be useful.
  if (defaultLocale != lastFallbackLocale) {
    let lastFallbackId = `langpack-${lastFallbackLocale}@firefox.mozilla.org`;
    let lastFallbackInstalled =
      await lazy.AddonManager.getAddonByID(lastFallbackId);
    if (!lastFallbackInstalled) {
      return availableLocales.filter(locale => locale != lastFallbackLocale);
    }
  }
  return availableLocales;
}

export var LangPackMatcher = {
  negotiateLangPackForLanguageMismatch,
  ensureLangPackInstalled,
  getAppAndSystemLocaleInfo,
  setRequestedAppLocales,
  getAvailableLocales,
  mockable,
};
