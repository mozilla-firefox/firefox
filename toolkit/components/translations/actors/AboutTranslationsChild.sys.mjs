/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.translations.logLevel",
    logModule: "Translations",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  LanguageDetector:
    "resource://gre/modules/translations/LanguageDetector.sys.mjs",
});

/**
 * @typedef {import("./TranslationsChild.sys.mjs").TranslationsEngine} TranslationsEngine
 * @typedef {import("./TranslationsChild.sys.mjs").SupportedLanguages} SupportedLanguages
 * @typedef {import("../translations").LanguagePair} LanguagePair
 */

/**
 * The AboutTranslationsChild is responsible for coordinating what privileged APIs
 * are exposed to the un-privileged scope of the about:translations page.
 */
export class AboutTranslationsChild extends JSWindowActorChild {
  /**
   * The translations engine uses text translations by default in about:translations,
   * but it can be changed to translate HTML by setting this pref to true. This is
   * useful for manually testing HTML translation behavior, but is not useful to surface
   * as a user-facing feature.
   *
   * @type {bool}
   */
  #isHtmlTranslation = Services.prefs.getBoolPref(
    "browser.translations.useHTML"
  );

  async handleEvent(event) {
    if (event.type === "DOMDocElementInserted") {
      this.#exportFunctions();
    }

    if (event.type === "DOMContentLoaded") {
      const enabled = await this.sendQuery("AboutTranslations:GetEnabledState");
      this.#sendEventToContent({
        type: "enabled-state-changed",
        enabled,
      });
    }
  }

  receiveMessage({ name, data }) {
    switch (name) {
      case "AboutTranslations:EnabledStateChanged": {
        const { enabled } = data;
        this.#sendEventToContent({
          type: "enabled-state-changed",
          enabled,
        });
        break;
      }
      case "AboutTranslations:SendTranslationsPort": {
        const { languagePair, port } = data;
        const transferables = [port];
        this.contentWindow.postMessage(
          {
            type: "GetTranslationsPort",
            languagePair,
            port,
          },
          "*",
          transferables
        );
        break;
      }
      case "AboutTranslations:RebuildTranslator": {
        this.#sendEventToContent({ type: "rebuild-translator" });
        break;
      }
      default:
        throw new Error("Unknown AboutTranslations message: " + name);
    }
  }

  RPMGetFormatURLPref(formatURL) {
    return Services.urlFormatter.formatURLPref(formatURL);
  }

  /**
   * @param {object} detail
   */
  #sendEventToContent(detail) {
    this.contentWindow.dispatchEvent(
      new this.contentWindow.CustomEvent("AboutTranslationsChromeToContent", {
        detail: Cu.cloneInto(detail, this.contentWindow),
      })
    );
  }

  /**
   * A privileged promise can't be used in the content page, so convert a privileged
   * promise into a content one.
   *
   * @param {Promise<any>} promise
   * @returns {Promise<any>}
   */
  #convertToContentPromise(promise) {
    return new this.contentWindow.Promise((resolve, reject) =>
      promise.then(resolve, error => {
        let contentWindow;
        try {
          contentWindow = this.contentWindow;
        } catch {
          // The content window is no longer available.
          reject();
          return;
        }
        // Create an error in the content window, if the content window is still around.
        let message = "An error occured in the AboutTranslations actor.";
        if (typeof error === "string") {
          message = error;
        }
        if (typeof error?.message === "string") {
          message = error.message;
        }
        if (typeof error?.stack === "string") {
          message += `\n\nOriginal stack:\n\n${error.stack}\n`;
        }

        reject(new contentWindow.Error(message));
      })
    );
  }

  /**
   * Export any of the child functions that start with "AT_" to the unprivileged content
   * page. This restricts the security capabilities of the the content page.
   */
  #exportFunctions() {
    const window = this.contentWindow;

    const fns = [
      "AT_log",
      "AT_logError",
      "AT_getAppLocaleAsBCP47",
      "AT_getSupportedLanguages",
      "AT_clearSourceText",
      "AT_enableTranslationsFeature",
      "AT_isEnabledStateManagedByPolicy",
      "AT_isTranslationEngineSupported",
      "AT_isHtmlTranslation",
      "AT_isInAutomation",
      "AT_createTranslationsPort",
      "AT_identifyLanguage",
      "AT_getDisplayName",
      "AT_getScriptDirection",
      "AT_telemetry",
      "RPMGetFormatURLPref",
    ];
    for (const name of fns) {
      Cu.exportFunction(this[name].bind(this), window, { defineAs: name });
    }
  }

  /**
   * Log messages if "browser.translations.logLevel" is set to "All".
   *
   * @param {...any} args
   */
  AT_log(...args) {
    lazy.console.log(...args);
  }

  /**
   * Report an error to the console.
   *
   * @param {...any} args
   */
  AT_logError(...args) {
    lazy.console.error(...args);
  }

  /**
   * Returns the app's locale.
   *
   * @returns {Intl.Locale}
   */
  AT_getAppLocaleAsBCP47() {
    return Services.locale.appLocaleAsBCP47;
  }

  /**
   * Wire this function to the TranslationsChild.
   *
   * @returns {Promise<SupportedLanguages>}
   */
  AT_getSupportedLanguages() {
    return this.#convertToContentPromise(
      this.sendQuery("AboutTranslations:GetSupportedLanguages").then(data =>
        Cu.cloneInto(data, this.contentWindow)
      )
    );
  }

  /**
   * Clears the about:translations source textarea with privileged user-input
   * semantics, rather than setting the value directly to an empty string.
   *
   * Clearing the text this way allows the text to be restored via `Ctrl/Cmd + Z`.
   */
  AT_clearSourceText() {
    const sourceTextArea = this.contentWindow.document.getElementById(
      "about-translations-source-textarea"
    );

    sourceTextArea.focus();
    sourceTextArea.setUserInput("");
  }

  /**
   * Returns the display name of the given BCP-47 language tag.
   *
   * @param {string} language
   */
  AT_getDisplayName(language) {
    return this.#convertToContentPromise(
      this.sendQuery("AboutTranslations:GetDisplayName", { language }).then(
        data => Cu.cloneInto(data, this.contentWindow)
      )
    );
  }

  /**
   * Does this device support the translation engine?
   *
   * @returns {Promise<boolean>}
   */
  AT_isTranslationEngineSupported() {
    return this.#convertToContentPromise(
      this.sendQuery("AboutTranslations:IsTranslationsEngineSupported")
    );
  }

  /**
   * Returns true if the enabled state is managed by an enterprise policy, otherwise false.
   *
   * When false, the user may freely enable or disable the Translations feature.
   * When true, the enabled state cannot be changed by the user at run time.
   *
   * Note that it is possible for a policy to enforce that the feature is "disabled and immutable,"
   * such that the user cannot turn the feature on, as well as "enabled and immutable," such that
   * the user cannot turn the feature off.
   *
   * @returns {Promise<boolean>}
   */
  AT_isEnabledStateManagedByPolicy() {
    return this.#convertToContentPromise(
      this.sendQuery("AboutTranslations:IsEnabledStateManagedByPolicy")
    );
  }

  /**
   * Enables the Translations feature.
   *
   * @returns {Promise<void>}
   */
  AT_enableTranslationsFeature() {
    return this.#convertToContentPromise(
      this.sendQuery("AboutTranslations:EnableTranslationsFeature")
    );
  }

  /**
   * Expose the #isHtmlTranslation property.
   *
   * @returns {bool}
   */
  AT_isHtmlTranslation() {
    return this.#isHtmlTranslation;
  }

  /**
   * Returns true if we are running tests in automation, otherwise false.
   *
   * @returns {boolean}
   */
  AT_isInAutomation() {
    return Cu.isInAutomation;
  }

  /**
   * Requests a port to the TranslationsEngine process. An engine will be created on
   * the fly for translation requests through this port. This port is unique to its
   * language pair. In order to translate a different language pair, a new port must be
   * created for that pair. The lifecycle of the engine is managed by the
   * TranslationsEngine.
   *
   * @param {LanguagePair} languagePair
   * @returns {void}
   */
  AT_createTranslationsPort(languagePair) {
    this.sendAsyncMessage("AboutTranslations:GetTranslationsPort", {
      languagePair,
    });
  }

  /**
   * Attempts to identify the human language in which the message is written.
   *
   * @param {string} message
   * @returns {Promise<{ language: string, confident: boolean }>}
   */
  AT_identifyLanguage(message) {
    return this.#convertToContentPromise(
      lazy.LanguageDetector.detectLanguage(message).then(data =>
        Cu.cloneInto(data, this.contentWindow)
      )
    );
  }

  /**
   * TODO - Remove this when Intl.Locale.prototype.textInfo is available to
   * content scripts.
   *
   * https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/Locale/textInfo
   * https://bugzilla.mozilla.org/show_bug.cgi?id=1693576
   *
   * @param {string} locale
   * @returns {string}
   */
  AT_getScriptDirection(locale) {
    return Services.intl.getScriptDirection(locale);
  }

  /**
   * Sends telemetry data to the TranslationsEngine.
   *
   * @param {string} telemetryFunctionName - The name of the telemetry function.
   * @param {object} telemetryData - The data associated with the telemetry event.
   */
  AT_telemetry(telemetryFunctionName, telemetryData) {
    this.sendAsyncMessage("AboutTranslations:Telemetry", {
      telemetryFunctionName,
      telemetryData,
    });
  }
}
