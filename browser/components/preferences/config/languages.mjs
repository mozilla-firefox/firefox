/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const {
  LangPackMatcher, // preferences.js
  TAB_SESSION_ID, // preferences.js
  gotoPref, // preferences.js
  canShowAiFeature, // main.js
} = window;

Preferences.addAll([
  { id: "intl.multilingual.enabled", type: "bool" },
  { id: "intl.multilingual.downloadEnabled", type: "bool" },
  { id: "intl.regional_prefs.use_os_locales", type: "bool" },
  { id: "intl.accept_languages", type: "string" },
  { id: "privacy.spoof_english", type: "int" },
  { id: "layout.spellcheckDefault", type: "int" },
  { id: "browser.translations.automaticallyPopup", type: "bool" },
]);

/**
 * @typedef {string} LocaleCode A locale code, like "en" or "fr".
 *
 * @typedef {object} Langpack
 *   Langpack data from the AMO server, this is a partial typedef of what's used
 *   in this file.
 * @property {LocaleCode} target_locale The locale code of the Langpack.
 *
 * @typedef {object} Locale Info about a locale.
 * @property {LocaleCode} code The locale code.
 * @property {string} label The localized name.
 *
 * @typedef {object} RemoteLocaleExtensions
 * @property {Langpack} langpack Langpack info for installing the Locale.
 *
 * @typedef {Locale & RemoteLocaleExtensions} RemoteLocale
 *  Locale info from the API, includes a Langpack
 */

export const Multilingual = {
  TransitionType: Object.freeze({
    LocalesMatch: "LocalesMatch",
    RestartRequired: "RestartRequired",
    LiveReload: "LiveReload",
  }),

  /**
   * @returns {Promise<Locale[]>}
   */
  async installedLocales() {
    return this.localizeArray(
      /** @type {LocaleCode[]} */ (await LangPackMatcher.getAvailableLocales()),
      code => code,
      (code, label) => ({ code, label })
    );
  },

  /**
   * Localize the locale codes to a locale name and transform the data from an
   * arbitrary locale data array. The output of transform is returned sorted by
   * the localized locale name.
   *
   * @template T Input data type
   * @template U Output data type
   * @param {T[]} list Array of your locale data.
   * @param {(data: T) => string} getCode Get a locale code from an entry.
   * @param {(data: T, localizedName: string) => U} transform
   *   Transform the original data with the now localized name.
   * @returns {U[]}
   */
  localizeArray(list, getCode, transform) {
    const localeNames = this.getLocaleDisplayNames(list.map(getCode));
    return list
      .map(
        (data, i) =>
          /**
           * Map into the localeName (for sorting) and output data tuple.
           *
           * @type {[localeName: string, output: U]}
           */ ([localeNames[i], transform(data, localeNames[i])])
      )
      .sort((a, b) => a[0].localeCompare(b[0]))
      .map(d => d[1]);
  },

  /**
   * @param {LocaleCode[]} codes The locale codes to localize.
   * @returns {string[]} The localized names in the same order as codes.
   */
  getLocaleDisplayNames(codes) {
    return Services.intl.getLocaleDisplayNames(undefined, codes, {
      preferNative: true,
    });
  },

  /**
   * Determine the transition strategy for switching the locale based on prefs
   * and the switched locales.
   *
   * @param {LocaleCode[]} newLocales - List of BCP 47 locale identifiers.
   * @returns {keyof typeof Multilingual.TransitionType}
   */
  getTransitionType(newLocales) {
    const { appLocalesAsBCP47 } = Services.locale;
    if (appLocalesAsBCP47.join(",") === newLocales.join(",")) {
      // The selected locales match, the order matters.
      return Multilingual.TransitionType.LocalesMatch;
    }

    if (Services.prefs.getBoolPref("intl.multilingual.liveReload")) {
      if (
        Services.intl.getScriptDirection(newLocales[0]) !==
          Services.intl.getScriptDirection(appLocalesAsBCP47[0]) &&
        !Services.prefs.getBoolPref("intl.multilingual.liveReloadBidirectional")
      ) {
        // Bug 1750852: The directionality of the text changed, which requires a restart
        // until the quality of the switch can be improved.
        return Multilingual.TransitionType.RestartRequired;
      }

      return Multilingual.TransitionType.LiveReload;
    }

    return Multilingual.TransitionType.RestartRequired;
  },

  /**
   * @param {string} method
   * @param {any} value
   */
  recordTelemetry(method, value = null) {
    if (method == "apply") {
      Glean.intlUiBrowserLanguage.applyMain.record();
    } else if (method == "reorder") {
      Glean.intlUiBrowserLanguage.reorderMain.record();
    } else if (method == "add") {
      // This isn't in the dialog, but only one or the other is available.
      Glean.intlUiBrowserLanguage.addDialog.record({
        installId: String(value),
        // value is telemetryId which was a msSinceProcessStart string of when
        // the dialog opened. Using the new UUID for this tab makes sense.
        value: TAB_SESSION_ID,
      });
    } else if (method == "search") {
      Glean.intlUiBrowserLanguage.searchMain.record({ value });
    } else if (method == "manage") {
      Glean.intlUiBrowserLanguage.manageMain.record({ value });
    }
  },

  /**
   * @param {LocaleCode[]} locales
   */
  applyAndRestart(locales) {
    Services.locale.requestedLocales = locales;

    // Record the change in telemetry before we restart.
    this.recordTelemetry("apply");

    // Restart with the new locale.
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );
    if (!cancelQuit.data) {
      Services.startup.quit(
        Services.startup.eAttemptQuit | Services.startup.eRestart
      );
    }
  },

  /**
   * @param {Langpack} langpack
   */
  async ensureLangPackInstalled(langpack) {
    try {
      await LangPackMatcher.ensureLangPackInstalled(
        langpack,
        "about:preferences",
        /** @param {any} installId */
        installId => this.recordTelemetry("add", installId)
      );
    } catch (e) {
      return false;
    }
    return true;
  },
};

Preferences.addSetting({
  id: "multilingualEnabled",
  pref: "intl.multilingual.enabled",
});
Preferences.addSetting({
  id: "multilingualDownloadEnabled",
  pref: "intl.multilingual.downloadEnabled",
});

/**
 * @param {Locale} locale
 * @returns {SettingOptionConfig}
 */
function makeBrowserLanguageOption({ code, label }) {
  return {
    value: code,
    controlAttrs: {
      label,
    },
  };
}
class BrowserLanguagesSetting extends Preferences.AsyncSetting {
  static id = "browserLanguages";

  multilingualEnabled = Preferences.getSetting("multilingualEnabled");

  /** @type {Promise<Locale[]>} */
  installedLocales = Promise.resolve([]);

  /** @type {LocaleCode[] | null} */
  #pendingLocales = null;
  #installing = false;
  #installError = false;

  get currentLocale() {
    return Services.locale.appLocaleAsBCP47;
  }

  get pendingLocale() {
    return this.#pendingLocales?.[0] ?? null;
  }

  get restartRequired() {
    return Boolean(this.#pendingLocales?.length);
  }

  get installing() {
    return this.#installing;
  }

  get installError() {
    return this.#installError;
  }

  /**
   * @param {LocaleCode[]} newLocales
   */
  #updateLocales(newLocales) {
    switch (Multilingual.getTransitionType(newLocales)) {
      case Multilingual.TransitionType.RestartRequired:
        this.#pendingLocales = newLocales;
        break;
      case Multilingual.TransitionType.LiveReload:
        this.#pendingLocales = null;
        Services.locale.requestedLocales = newLocales;
        break;
      case Multilingual.TransitionType.LocalesMatch:
        this.#pendingLocales = null;
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
    this.emitChange();
  }

  /**
   * @param {LocaleCode} code
   * @param {RemoteLocale[]} remoteLocales
   * @returns {Promise<Locale | null>}
   */
  async #ensureLocaleInstalled(code, remoteLocales) {
    const installed = (await this.installedLocales).find(
      locale => locale.code == code
    );
    if (installed) {
      return installed;
    }
    const remote = remoteLocales.find(locale => locale.code == code);
    if (remote) {
      this.#installing = true;
      this.emitChange();
      try {
        if (await Multilingual.ensureLangPackInstalled(remote.langpack)) {
          return remote;
        }
      } finally {
        this.#installing = false;
        this.emitChange();
      }
    }
    return null;
  }

  setup() {
    Services.obs.addObserver(this.emitChange, "intl:app-locales-changed");
    this.multilingualEnabled.on("change", this.emitChange);
    return () => {
      Services.obs.removeObserver(this.emitChange, "intl:app-locales-changed");
      this.multilingualEnabled.off("change", this.emitChange);
    };
  }

  beforeRefresh() {
    this.installedLocales = Multilingual.installedLocales();
  }

  // @ts-expect-error Arrays aren't typed currently.
  async get() {
    return this.#pendingLocales
      ? [...this.#pendingLocales]
      : [...Services.locale.appLocalesAsBCP47];
  }

  async visible() {
    return Boolean(this.multilingualEnabled.value);
  }

  async getPreferred() {
    return this.pendingLocale ?? this.currentLocale;
  }

  /**
   * @param {LocaleCode} code
   * @param {Locale[]} remoteLocales Available remote locales for installation.
   */
  async setPreferred(code, remoteLocales = []) {
    this.#installError = false;
    if (code == this.currentLocale) {
      this.#pendingLocales = null;
      this.emitChange();
      return;
    }
    let locale = await this.#ensureLocaleInstalled(code, remoteLocales);
    if (!locale) {
      this.#installError = true;
      this.emitChange();
      return;
    }
    if (!locale.langpack) {
      // Previously installed locales don't have a langpack, this is a reorder.
      // Telemetry for the add case is recorded during install.
      Multilingual.recordTelemetry("reorder");
    }
    let newLocales = Array.from(
      new Set([code, ...Services.locale.requestedLocales]).values()
    );
    this.#updateLocales(newLocales);
  }

  async getFallback() {
    let locales = await this.get();
    if (locales[1]) {
      return locales[1];
    }
    return null;
  }

  /**
   * @param {LocaleCode} code
   */
  async setFallback(code) {
    let locales = await this.get();
    Multilingual.recordTelemetry("reorder");
    this.#updateLocales([locales[0], code]);
  }

  applyAndRestart() {
    if (this.restartRequired) {
      Multilingual.applyAndRestart(this.#pendingLocales);
    }
  }
}
Preferences.addSetting(BrowserLanguagesSetting);

class BrowserLanguageRemoteLocalesSetting extends Preferences.AsyncSetting {
  static id = "browserLanguageRemoteLocales";

  #multilingualDownloadEnabled = /** @type {Setting} */ (
    Preferences.getSetting("multilingualDownloadEnabled")
  );

  /** @type {Promise<RemoteLocale[]> | null} */
  #cache = null;

  #languagesLoaded = false;

  setup() {
    this.#multilingualDownloadEnabled.on("change", this.emitChange);
    /** @param {CustomEvent} e */
    const onPaneshown = e => {
      if (e.detail.category == "paneLanguages") {
        this.#languagesLoaded = true;
        this.emitChange();
        window.removeEventListener("paneshown", onPaneshown);
      }
    };
    window.addEventListener("paneshown", onPaneshown);
    return () =>
      this.#multilingualDownloadEnabled.off("change", this.emitChange);
  }

  /** @type {RemoteLocale[]} */
  // @ts-expect-error Arrays aren't typed currently.
  defaultValue = [];

  /** @returns {Promise<RemoteLocale[]>} */
  // @ts-expect-error Arrays aren't typed currently.
  async get() {
    if (!this.#multilingualDownloadEnabled.value || !this.#languagesLoaded) {
      return [];
    }
    if (!this.#cache) {
      this.#cache = this.#fetch();
    }
    return this.#cache;
  }

  async #fetch() {
    try {
      let langpacks = /** @type {Langpack[]} */ (
        await LangPackMatcher.mockable.getAvailableLangpacks()
      );
      return Multilingual.localizeArray(
        langpacks,
        langpack => langpack.target_locale,
        (langpack, label) =>
          /** @type {RemoteLocale} */ ({
            code: langpack.target_locale,
            label,
            langpack,
          })
      );
    } catch (e) {
      console.error("Failed to fetch remote language packs:", e);
      return [];
    }
  }
}
Preferences.addSetting(BrowserLanguageRemoteLocalesSetting);

class BrowserLanguagePreferredSetting extends Preferences.AsyncSetting {
  static id = "browserLanguagePreferred";

  #browserLanguagesSetting = /** @type {Setting} */ (
    Preferences.getSetting("browserLanguages")
  );
  #remoteLocalesSetting = /** @type {Setting} */ (
    Preferences.getSetting("browserLanguageRemoteLocales")
  );

  /** @type {BrowserLanguagesSetting} */
  get #browserLanguages() {
    return /** @type {BrowserLanguagesSetting} */ (
      /** @type {unknown} */ (
        /** @type {AsyncSettingHandler} */ (
          this.#browserLanguagesSetting.config
        ).asyncSetting
      )
    );
  }

  setup() {
    this.#browserLanguagesSetting.on("change", this.emitChange);
    this.#remoteLocalesSetting.on("change", this.emitChange);
    return () => {
      this.#browserLanguagesSetting.off("change", this.emitChange);
      this.#remoteLocalesSetting.off("change", this.emitChange);
    };
  }

  get #remoteLocales() {
    return (
      /** @type {RemoteLocale[]} */ (
        /** @type {unknown} */ (this.#remoteLocalesSetting.value)
      ) || []
    );
  }

  async get() {
    return this.#browserLanguages.getPreferred();
  }

  /**
   * @param {LocaleCode} code
   */
  async set(code) {
    return this.#browserLanguages.setPreferred(code, this.#remoteLocales);
  }

  async disabled() {
    return this.#browserLanguages.installing;
  }

  async visible() {
    return this.#browserLanguagesSetting.visible;
  }

  async getControlConfig() {
    let installed = await this.#browserLanguages.installedLocales;
    let remote = this.#remoteLocales.filter(
      r => !installed.some(i => i.code == r.code)
    );
    return {
      options: [
        ...installed.map(makeBrowserLanguageOption),
        ...(remote.length ? [{ control: "hr" }] : []),
        ...remote.map(makeBrowserLanguageOption),
      ],
    };
  }
}
Preferences.addSetting(BrowserLanguagePreferredSetting);

class BrowserLanguageFallbackSetting extends Preferences.AsyncSetting {
  static id = "browserLanguageFallback";

  #browserLanguages = Preferences.getSetting("browserLanguages");

  /** @type {BrowserLanguagesSetting} */
  get #languages() {
    return /** @type {BrowserLanguagesSetting} */ (
      /** @type {unknown} */ (
        /** @type {AsyncSettingHandler} */ (this.#browserLanguages.config)
          .asyncSetting
      )
    );
  }

  setup() {
    this.#browserLanguages.on("change", this.emitChange);
    return () => this.#browserLanguages.off("change", this.emitChange);
  }

  async get() {
    return this.#languages.getFallback();
  }

  /**
   * @param {LocaleCode} code
   */
  async set(code) {
    return this.#languages.setFallback(code);
  }

  async disabled() {
    return this.#languages.installing;
  }

  async visible() {
    if (!this.#browserLanguages.visible) {
      return false;
    }
    if (
      (await this.#languages.getPreferred()) == Services.locale.defaultLocale
    ) {
      return false;
    }
    let installed = await this.#languages.installedLocales;
    return installed.length >= 2;
  }

  async getControlConfig() {
    let installed = await this.#languages.installedLocales;
    let locales = await this.#languages.get();
    let options = installed.map(locale => ({
      ...makeBrowserLanguageOption(locale),
      hidden: locale.code === locales[0],
    }));
    return { options };
  }
}
Preferences.addSetting(BrowserLanguageFallbackSetting);

Preferences.addSetting({
  id: "browserLanguageMessage",
  deps: ["browserLanguages"],
  visible(deps) {
    let handler = /** @type {AsyncSettingHandler} */ (
      deps.browserLanguages.config
    );
    let setting = /** @type {BrowserLanguagesSetting} */ (
      /** @type {unknown} */ (handler.asyncSetting)
    );
    return setting.restartRequired || setting.installError;
  },
});

Preferences.addSetting({
  id: "useSystemLocale",
  pref: "intl.regional_prefs.use_os_locales",
  visible() {
    let appLocale = Services.locale.appLocaleAsBCP47;
    let regionalPrefsLocales = Services.locale.regionalPrefsLocales;
    if (!regionalPrefsLocales.length) {
      return false;
    }
    let systemLocale = regionalPrefsLocales[0];
    return appLocale.split("-u-")[0] != systemLocale.split("-u-")[0];
  },
});

Preferences.addSetting({
  id: "acceptLanguages",
  pref: "intl.accept_languages",
  get(prefVal, _, setting) {
    return setting.pref.defaultValue != prefVal
      ? prefVal.toLowerCase()
      : Services.locale.acceptLanguages.toLowerCase();
  },
});
Preferences.addSetting({
  id: "availableLanguages",
  deps: ["acceptLanguages"],
  get(_, { acceptLanguages }) {
    let re = /\s*(?:,|$)\s*/;
    let _acceptLanguages = acceptLanguages.value.split(re);
    let availableLanguages = [];
    let localeCodes = [];
    let localeValues = [];
    let bundle = Services.strings.createBundle(
      "resource://gre/res/language.properties"
    );

    for (let currString of bundle.getSimpleEnumeration()) {
      let property = currString.key.split(".");
      if (property[1] == "accept") {
        localeCodes.push(property[0]);
        localeValues.push(currString.value);
      }
    }

    let localeNames = Services.intl.getLocaleDisplayNames(
      undefined,
      localeCodes
    );

    for (let i in localeCodes) {
      let isVisible =
        localeValues[i] == "true" &&
        (!_acceptLanguages.includes(localeCodes[i]) ||
          !_acceptLanguages[localeCodes[i]]);
      let locale = {
        code: localeCodes[i],
        displayName: localeNames[i],
        isVisible,
      };
      availableLanguages.push(locale);
    }

    return availableLanguages;
  },
});

Preferences.addSetting({
  id: "websiteLanguageWrapper",
  deps: ["acceptLanguages"],
  onUserReorder(event, deps) {
    const { draggedIndex, targetIndex } = event.detail;

    let re = /\s*(?:,|$)\s*/;
    let languages = deps.acceptLanguages.value.split(re).filter(lang => lang);

    const [draggedLang] = languages.splice(draggedIndex, 1);

    languages.splice(targetIndex, 0, draggedLang);

    deps.acceptLanguages.value = languages.join(",");
  },
  getControlConfig(config, deps) {
    let languagePref = deps.acceptLanguages.value;
    let localeCodes = languagePref
      .toLowerCase()
      .split(/\s*,\s*/)
      .filter(code => code.length);
    let localeDisplayNames = Services.intl.getLocaleDisplayNames(
      undefined,
      localeCodes
    );
    /** @type {SettingOptionConfig[]} */
    let availableLanguages = [];
    for (let i = 0; i < localeCodes.length; i++) {
      let displayName = localeDisplayNames[i];
      let localeCode = localeCodes[i];
      availableLanguages.push({
        l10nId: "languages-code-format",
        l10nArgs: {
          locale: displayName,
          code: localeCode,
        },
        control: "moz-box-item",
        key: localeCode,
        options: [
          {
            control: "moz-button",
            slot: "actions-start",
            iconSrc: "chrome://global/skin/icons/delete.svg",
            l10nId: "website-remove-language-button",
            l10nArgs: {
              locale: displayName,
              code: localeCode,
            },
            controlAttrs: {
              locale: localeCode,
              action: "remove",
            },
          },
        ],
      });
    }
    config.options = [config.options[0], ...availableLanguages];
    return config;
  },
  onUserClick(e, deps) {
    let code = e.target.getAttribute("locale");
    let action = e.target.getAttribute("action");
    if (code && action) {
      if (action === "remove") {
        let re = /\s*(?:,|$)\s*/;
        let acceptedLanguages = deps.acceptLanguages.value.split(re);
        let filteredLanguages = acceptedLanguages.filter(
          acceptedCode => acceptedCode !== code
        );
        deps.acceptLanguages.value = filteredLanguages.join(",");
        let closestBoxItem = e.target.closest("moz-box-item");
        closestBoxItem.nextElementSibling
          ? closestBoxItem.nextElementSibling.focus()
          : closestBoxItem.previousElementSibling.focus();
      }
    }
  },
});

Preferences.addSetting({
  id: "websiteLanguageAddLanguage",
  deps: ["websiteLanguagePicker", "acceptLanguages"],
  onUserClick(e, deps) {
    let selectedLanguage = deps.websiteLanguagePicker.value;
    if (selectedLanguage == "-1") {
      return;
    }

    let re = /\s*(?:,|$)\s*/;
    let currentLanguages = deps.acceptLanguages.value.split(re);
    let isAlreadyAccepted = currentLanguages.includes(selectedLanguage);

    if (isAlreadyAccepted) {
      return;
    }

    currentLanguages.unshift(selectedLanguage);
    deps.acceptLanguages.value = currentLanguages.join(",");
  },
});

Preferences.addSetting(
  /** @type {{inputValue: string} & SettingConfig } */ ({
    id: "websiteLanguagePicker",
    deps: ["availableLanguages", "acceptLanguages"],
    inputValue: "-1",
    getControlConfig(config, deps) {
      let re = /\s*(?:,|$)\s*/;
      let availableLanguages =
        /** @type {{ locale: string, code: string, displayName: string, isVisible: boolean }[]} */
        deps.availableLanguages.value;

      let acceptLanguages = new Set(
        /** @type {string} */ (deps.acceptLanguages.value).split(re)
      );

      let sortedOptions = availableLanguages.map(locale => ({
        l10nId: "languages-code-format",
        l10nArgs: {
          locale: locale.displayName,
          code: locale.code,
        },
        hidden: locale.isVisible && acceptLanguages.has(locale.code),
        value: locale.code,
      }));
      // Sort the list of languages by name
      let comp = new Services.intl.Collator(undefined, {
        usage: "sort",
      });

      sortedOptions.sort((a, b) => {
        return comp.compare(a.l10nArgs.locale, b.l10nArgs.locale);
      });

      // Take the existing "Add Language" option and prepend it.
      config.options = [config.options[0], ...sortedOptions];
      return config;
    },
    get(_, deps) {
      if (
        !this.inputValue ||
        deps.acceptLanguages.value.split(",").includes(this.inputValue)
      ) {
        this.inputValue = "-1";
      }
      return this.inputValue;
    },
    set(inputVal) {
      this.inputValue = String(inputVal);
    },
  })
);

Preferences.addSetting({
  id: "offerTranslations",
  pref: "browser.translations.automaticallyPopup",
  deps: ["aiControlDefault", "aiControlTranslations"],
  visible: ({ aiControlDefault, aiControlTranslations }) =>
    canShowAiFeature(aiControlTranslations, aiControlDefault),
});

Preferences.addSetting({
  id: "checkSpelling",
  pref: "layout.spellcheckDefault",
  get: prefVal => prefVal != 0,
  set: val => (val ? 1 : 0),
});

Preferences.addSetting({
  id: "downloadDictionaries",
});

Preferences.addSetting({
  id: "spellCheckPromo",
});

Preferences.addSetting({
  id: "translationsManageButton",
  deps: ["aiControlDefault", "aiControlTranslations"],
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneTranslations");
  },
  visible: ({ aiControlDefault, aiControlTranslations }) =>
    canShowAiFeature(aiControlTranslations, aiControlDefault),
});

SettingGroupManager.registerGroups({
  browserLanguage: {
    inProgress: true,
    l10nId: "browser-language-heading",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/sidebar/firefox.svg",
    items: [
      {
        id: "browserLanguagePreferred",
        l10nId: "browser-language-preferred-label",
        control: "moz-select",
      },
      {
        id: "browserLanguageFallback",
        l10nId: "browser-language-fallback-label",
        control: "moz-select",
      },
      {
        id: "useSystemLocale",
        l10nId: "use-system-locale",
        get l10nArgs() {
          let regionalPrefsLocales = Services.locale.regionalPrefsLocales;
          if (!regionalPrefsLocales.length) {
            return { localeName: "und" };
          }
          let [systemLocale] = regionalPrefsLocales;
          let [displayName] = Services.intl.getLocaleDisplayNames(
            undefined,
            [systemLocale],
            { preferNative: true }
          );
          return { localeName: displayName || systemLocale };
        },
      },
      {
        id: "browserLanguageMessage",
        control: "browser-language-restart-message",
      },
    ],
  },
  websiteLanguage: {
    inProgress: true,
    l10nId: "website-language-heading",
    headingLevel: 2,
    iconSrc: "chrome://global/skin/icons/defaultFavicon.svg",
    items: [
      {
        id: "websiteLanguageWrapper",
        control: "moz-box-group",
        controlAttrs: {
          type: "reorderable-list",
        },
        options: [
          {
            id: "websiteLanguagePickerWrapper",
            l10nId: "website-preferred-language",
            key: "addlanguage",
            control: "moz-box-item",
            slot: "header",
            items: [
              {
                id: "websiteLanguagePicker",
                slot: "actions",
                control: "moz-select",
                options: [
                  {
                    control: "moz-option",
                    l10nId: "website-add-language",
                    controlAttrs: {
                      value: "-1",
                    },
                  },
                ],
              },
              {
                id: "websiteLanguageAddLanguage",
                slot: "actions",
                control: "moz-button",
                iconSrc: "chrome://global/skin/icons/plus.svg",
                l10nId: "website-add-language-button",
              },
            ],
          },
        ],
      },
    ],
  },
  translations: {
    inProgress: true,
    l10nId: "settings-translations-header",
    iconSrc: "chrome://browser/skin/translations.svg",
    supportPage: "website-translation",
    headingLevel: 2,
    items: [
      {
        id: "offerTranslations",
        l10nId: "settings-translations-offer-to-translate-label",
      },
      {
        id: "translationsManageButton",
        l10nId: "settings-translations-more-settings-button",
        control: "moz-box-button",
      },
    ],
  },
  spellCheck: {
    l10nId: "settings-spellcheck-header",
    iconSrc: "chrome://global/skin/icons/check.svg",
    headingLevel: 2,
    items: [
      {
        id: "checkSpelling",
        l10nId: "check-user-spelling",
        supportPage: "how-do-i-use-firefox-spell-checker",
      },
      {
        id: "downloadDictionaries",
        l10nId: "spellcheck-download-dictionaries",
        control: "moz-box-link",
        controlAttrs: {
          href: Services.urlFormatter.formatURLPref(
            "browser.dictionaries.download.url"
          ),
        },
      },
      {
        id: "spellCheckPromo",
        l10nId: "spellcheck-promo",
        control: "moz-promo",
        controlAttrs: {
          imagesrc:
            "chrome://browser/content/preferences/spell-check-promo.svg",
          imagewidth: "large",
          imagedisplay: "cover",
        },
      },
    ],
  },
});
