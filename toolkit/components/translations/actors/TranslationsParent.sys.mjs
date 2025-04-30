/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The pivot language is used to pivot between two different language translations
 * when there is not a model available to translate directly between the two. In this
 * case "en" is common between the various supported models.
 *
 * For instance given the following two models:
 *   "fr" -> "en"
 *   "en" -> "it"
 *
 * You can accomplish:
 *   "fr" -> "it"
 *
 * By doing:
 *   "fr" -> "en" -> "it"
 */
const PIVOT_LANGUAGE = "en";

const TRANSLATIONS_PERMISSION = "translations";

const ACCEPT_LANGUAGES_PREF = "intl.accept_languages";
const ALWAYS_TRANSLATE_LANGS_PREF =
  "browser.translations.alwaysTranslateLanguages";
const NEVER_TRANSLATE_LANGS_PREF =
  "browser.translations.neverTranslateLanguages";
const MOST_RECENT_TARGET_LANGS_PREF =
  "browser.translations.mostRecentTargetLanguages";
const TOPIC_NS_PREF_CHANGED = "nsPref:changed";
const TOPIC_TRANSLATIONS_PREF_CHANGED = "translations:pref-changed";
const TOPIC_MAYBE_UPDATE_USER_LANG_TAG =
  "translations:maybe-update-user-lang-tag";
const TOPIC_APP_LOCALES_CHANGED = "intl:app-locales-changed";
const USE_LEXICAL_SHORTLIST_PREF = "browser.translations.useLexicalShortlist";

const lazy = {};

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

if (AppConstants.ENABLE_WEBDRIVER) {
  XPCOMUtils.defineLazyServiceGetter(
    lazy,
    "Marionette",
    "@mozilla.org/remote/marionette;1",
    "nsIMarionette"
  );

  XPCOMUtils.defineLazyServiceGetter(
    lazy,
    "RemoteAgent",
    "@mozilla.org/remote/agent;1",
    "nsIRemoteAgent"
  );
} else {
  lazy.Marionette = { running: false };
  lazy.RemoteAgent = { running: false };
}

XPCOMUtils.defineLazyServiceGetters(lazy, {
  BrowserHandler: ["@mozilla.org/browser/clh;1", "nsIBrowserHandler"],
});

ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  TranslationsTelemetry:
    "chrome://global/content/translations/TranslationsTelemetry.sys.mjs",
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  EngineProcess: "chrome://global/content/ml/EngineProcess.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.translations.logLevel",
    prefix: "Translations",
  });
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "translationsEnabledPref",
  "browser.translations.enable"
);

/**
 * Returns whether Translations should utilize lexical shortlisting.
 */
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "useLexicalShortlist",
  USE_LEXICAL_SHORTLIST_PREF,
  /* aDefaultValue */ false,
  /* aOnUpdate */ () => {
    Services.obs.notifyObservers(
      null,
      TOPIC_TRANSLATIONS_PREF_CHANGED,
      USE_LEXICAL_SHORTLIST_PREF
    );
  }
);

/**
 * @import {DetectionResult} "../LanguageDetector.sys.mjs"
 */

/**
 * Retrieves the most recent target languages that have been requested for translation by the user.
 * Inserting into this pref should be managed by the static TranslationsParent class.
 *
 * @see {TranslationsParent.storeMostRecentTargetLanguage}
 *
 * There is a linear chain of synchronously dependent observers related to this pref.
 *
 * When this pref's value is updated, it sends "translations:most-recent-target-language-changed"
 * which is observed by the static global TranslationsParent object to know when to clear its cache.
 *
 * Once the cache has been cleared, the static global TranslationsParent object then sends
 * "translations:maybe-update-user-lang-tag" which is observed by every instantiated TranslationsParent
 * actor object to consider updating their cached userLangTag.
 *
 * @see {TranslationsParent} for further descriptions and diagrams.
 */
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "mostRecentTargetLanguages",
  MOST_RECENT_TARGET_LANGS_PREF,
  /* aDefaultValue */ "",
  /* aOnUpdate */ () => {
    Services.obs.notifyObservers(
      null,
      TOPIC_TRANSLATIONS_PREF_CHANGED,
      MOST_RECENT_TARGET_LANGS_PREF
    );
  },
  /* aTransform */ rawLangTags =>
    rawLangTags ? new Set(rawLangTags.split(",")) : new Set()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chaosErrorsPref",
  "browser.translations.chaos.errors"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "chaosTimeoutMSPref",
  "browser.translations.chaos.timeoutMS"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "automaticallyPopupPref",
  "browser.translations.automaticallyPopup"
);

/**
 * Returns the always-translate language tags as an array.
 */
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "alwaysTranslateLangTags",
  ALWAYS_TRANSLATE_LANGS_PREF,
  /* aDefaultPrefValue */ "",
  /* onUpdate */ () =>
    Services.obs.notifyObservers(
      null,
      TOPIC_TRANSLATIONS_PREF_CHANGED,
      ALWAYS_TRANSLATE_LANGS_PREF
    ),
  /* aTransform */ rawLangTags =>
    rawLangTags ? new Set(rawLangTags.split(",")) : new Set()
);

/**
 * Returns the never-translate language tags as an array.
 */
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "neverTranslateLangTags",
  NEVER_TRANSLATE_LANGS_PREF,
  /* aDefaultPrefValue */ "",
  /* onUpdate */ () =>
    Services.obs.notifyObservers(
      null,
      TOPIC_TRANSLATIONS_PREF_CHANGED,
      NEVER_TRANSLATE_LANGS_PREF
    ),
  /* aTransform */ rawLangTags =>
    rawLangTags ? new Set(rawLangTags.split(",")) : new Set()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "simulateUnsupportedEnginePref",
  "browser.translations.simulateUnsupportedEngine"
);

// At this time the signatures of the files are not being checked when they are being
// loaded from disk. This signature check involves hitting the network, and translations
// are explicitly an offline-capable feature. See Bug 1827265 for re-enabling this
// check.
const VERIFY_SIGNATURES_FROM_FS = false;

/**
 * @typedef {import("../translations").TranslationModelRecord} TranslationModelRecord
 * @typedef {import("../translations").RemoteSettingsClient} RemoteSettingsClient
 * @typedef {import("../translations").TranslationModelPayload} TranslationModelPayload
 * @typedef {import("../translations").LanguageTranslationModelFiles} LanguageTranslationModelFiles
 * @typedef {import("../translations").WasmRecord} WasmRecord
 * @typedef {import("../translations").LangTags} LangTags
 * @typedef {import("../translations").LanguagePair} LanguagePair
 * @typedef {import("../translations").ModelLanguages} ModelLanguages
 * @typedef {import("../translations").SupportedLanguages} SupportedLanguages
 * @typedef {import("../translations").TranslationErrors} TranslationErrors
 */

/**
 * The state that is stored per a "top" ChromeWindow. This "top" ChromeWindow is the JS
 * global associated with a browser window. Some state is unique to a browser window, and
 * using the top ChromeWindow is a unique key that ensures the state will be unique to
 * that browser window.
 *
 * See BrowsingContext.webidl for information on the "top"
 * See the TranslationsParent JSDoc for more information on the state management.
 */
class StatePerTopChromeWindow {
  /**
   * The storage backing for the states.
   *
   * @type {WeakMap<ChromeWindow, StatePerTopChromeWindow>}
   */
  static #states = new WeakMap();

  /**
   * When reloading the page, store the language pair that needs translating.
   *
   * @type {null | LanguagePair}
   */
  translateOnPageReload = null;

  /**
   * The page may auto-translate due to user settings. On a page restore, always
   * skip the page restore logic.
   *
   * @type {boolean}
   */
  isPageRestored = false;

  /**
   * Remember the detected languages on a page reload. This will keep the translations
   * button from disappearing and reappearing, which causes the button to lose focus.
   *
   * @type {LangTags | null} previousDetectedLanguages
   */
  previousDetectedLanguages = null;

  static #id = 0;
  /**
   * @param {ChromeWindow} topChromeWindow
   */
  constructor(topChromeWindow) {
    this.id = StatePerTopChromeWindow.#id++;
    StatePerTopChromeWindow.#states.set(topChromeWindow, this);
  }

  /**
   * @param {ChromeWindow} topChromeWindow
   * @returns {StatePerTopChromeWindow}
   */
  static getOrCreate(topChromeWindow) {
    let state = StatePerTopChromeWindow.#states.get(topChromeWindow);
    if (state) {
      return state;
    }
    state = new StatePerTopChromeWindow(topChromeWindow);
    StatePerTopChromeWindow.#states.set(topChromeWindow, state);
    return state;
  }
}

/**
 * The TranslationsParent is used to orchestrate translations in Firefox. It can
 * download the Wasm translation engine, and the language models. It manages the life
 * cycle for offering and performing translations.
 *
 * Care must be taken for the life cycle of the state management and data caching. The
 * following examples use a fictitious `myState` property to show how state can be stored.
 *
 * There is only 1 TranslationsParent static class in the parent process. At this
 * layer it is safe to store things like translation models and general browser
 * configuration as these don't change across browser windows. This is accessed like
 * `TranslationsParent.myState`
 *
 * The next layer down are the top ChromeWindows. These map to the UI and user's conception
 * of a browser window, such as what you would get by hitting cmd+n or ctrl+n to get a new
 * browser window. State such as whether a page is reloaded or general navigation events
 * must be unique per ChromeWindow. State here is stored in the `StatePerTopChromeWindow`
 * abstraction, like `this.getWindowState().myState`. This layer also consists of a
 * `FullPageTranslationsPanel` instance per top ChromeWindow (at least on Desktop).
 *
 * The final layer consists of the multiple tabs and navigation history inside of a
 * ChromeWindow. Data for this layer is safe to store on the TranslationsParent instance,
 * like `this.myState`.
 *
 * Below is an ascii diagram of this relationship.
 *
 *   ┌─────────────────────────────────────────────────────────────────────────────┐
 *   │                           static TranslationsParent                         │
 *   └─────────────────────────────────────────────────────────────────────────────┘
 *                  |                                       |
 *                  v                                       v
 * ┌──────────────────────────────────────┐   ┌──────────────────────────────────────┐
 * │         top ChromeWindow             │   │        top ChromeWindow              │
 * │ (FullPageTranslationsPanel instance) │   │ (FullPageTranslationsPanel instance) │
 * └──────────────────────────────────────┘   └──────────────────────────────────────┘
 *             |               |       |                |              |       |
 *             v               v       v                v              v       v
 *   ┌────────────────────┐ ┌─────┐ ┌─────┐  ┌────────────────────┐ ┌─────┐ ┌─────┐
 *   │ TranslationsParent │ │ ... │ │ ... │  │ TranslationsParent │ │ ... │ │ ... │
 *   │  (actor instance)  │ │     │ │     │  │  (actor instance)  │ │     │ │     │
 *   └────────────────────┘ └─────┘ └─────┘  └────────────────────┘ └─────┘ └─────┘
 */
export class TranslationsParent extends JSWindowActorParent {
  /**
   * The following constants control the major version for assets downloaded from
   * Remote Settings. When a breaking change is introduced, Nightly will have these
   * numbers incremented by one, but Beta and Release will still be on the previous
   * version. Remote Settings will ship both versions of the records, and the latest
   * asset released in that version will be used. For instance, with a major version
   * of "1", assets can be downloaded for "1.0", "1.2", "1.3beta", but assets marked
   * as "2.0", "2.1", etc will not be downloaded.
   *
   * Release docs:
   * https://firefox-source-docs.mozilla.org/toolkit/components/translations/resources/03_bergamot.html
   *
   * Release History:
   *
   * 1.x WASM Major Versions
   *
   *   - Compatible with all 1.x Translation models.
   *
   * 2.x WASM Major Versions
   *
   *   - Compatible with all 1.x Translation models.
   *
   *   - Compatible with all 2.x Translation models.
   *
   *     Notes: The 2.x WASM binary introduces segmentation changes that are necessary
   *            to translate CJK languages.
   */
  static BERGAMOT_MAJOR_VERSION = 2;

  /**
   * The BERGAMOT_MAJOR_VERSION defined above has only a single value, because there will
   * only ever be one instance of the WASM binary that is downloaded for all translations.
   *
   * However, the current Bergamot WASM binary may be backward compatible with existing models.
   * As such, the models use a range of major versions that are compatible with the current
   * WASM binary and/or source code changes.
   *
   * By incrementing only the maximum major version, this allows us to introduce new model types
   * that are compatible only with the latest source code or WASM binary while continuing to utilize
   * old model types that are backward compatible with the changes.
   *
   *   - Models with versions less than the new maximum major version:
   *       - Available to past versions of Firefox.
   *       - Available to the current version of Firefox.
   *
   *   - Models with versions equal to the new maximum major version:
   *       - Not available to past versions of Firefox.
   *       - Available to the current version of Firefox.
   *
   * By incrementing both the minimum and maximum major versions to the same value, this allows us to
   * introduce a hard cutoff point at which prior models are no longer compatible with the current version
   * of Firefox.
   *
   *   - Models with versions less than the new minimum and maximum major versions:
   *       - Available to past versions of Firefox.
   *       - Not available to current and future versions of Firefox.
   *
   *   - Models with versions equal to the new minimum and maximum major versions:
   *       - Not available to past versions of Firefox.
   *       - Available to the current version of Firefox.
   *
   * Release History:
   *
   * 1.x Model Major Versions
   *
   *   - Compatible with 1.x Bergamot WASM binaries.
   *   - Compatible with 2.x Bergamot WASM binaries.
   *
   *   Notes: 1.x models are referred to as "tiny" models, and are the models that were shipped with the original
   *          release of Translations in Firefox.
   *
   * 2.x Model Major Versions
   *
   *   - Compatible with 2.x Bergamot WASM binaries.
   *
   *   Notes: 2.x models are defined by any of two characteristics. The first characteristic is any CJK language model.
   *          Only the 2.x WASM binaries support the segmentation concerns needed to interop with CJK language models.
   *          The second characteristic is any "base" language model, which is larger than the "tiny" 1.x models.
   *          Compatibility for base models is dependent on the code changes in Bug 1926100.
   */
  static LANGUAGE_MODEL_MAJOR_VERSION_MIN = 1;
  static LANGUAGE_MODEL_MAJOR_VERSION_MAX = 2;

  /**
   * Contains the state that would affect UI. Anytime this state is changed, a dispatch
   * event is sent so that UI can react to it. The actor is inside of /toolkit and
   * needs a way of notifying /browser code (or other users) of when the state changes.
   *
   * @type {TranslationsLanguageState}
   */
  languageState;

  /**
   * Allows the TranslationsEngineParent to resolve an engine once it is ready.
   *
   * @type {null | () => TranslationsEngineParent}
   */
  resolveEngine = null;

  /**
   * The TranslationsEngineParent instance which requests from this
   * TranslationsParent are being handled by.
   *
   * Used to ensure translations are discarded when the actor dies.
   *
   * @type {null | TranslationsEngineParent}
   */
  engineActor = null;

  /**
   * Do not send queries or do work when the actor is already destroyed. This flag needs
   * to be checked after calls to `await`.
   */
  #isDestroyed = false;

  /**
   * There is only one static TranslationsParent for all of the top ChromeWindows.
   * The top ChromeWindow maps to the user's conception of a window such as when you hit
   * cmd+n or ctrl+n.
   *
   * @returns {StatePerTopChromeWindow}
   */
  getWindowState() {
    const state = StatePerTopChromeWindow.getOrCreate(
      this.browsingContext.top.embedderWindowGlobal
    );
    return state;
  }

  actorCreated() {
    this.innerWindowId = this.browsingContext.top.embedderElement.innerWindowID;
    const windowState = this.getWindowState();
    this.languageState = new TranslationsLanguageState(
      this,
      windowState.previousDetectedLanguages
    );
    windowState.previousDetectedLanguages = null;

    this.#boundObserve = this.#observe.bind(this);
    Services.obs.addObserver(
      this.#boundObserve,
      TOPIC_MAYBE_UPDATE_USER_LANG_TAG
    );

    if (windowState.translateOnPageReload) {
      // The actor was recreated after a page reload, start the translation.
      const languagePair = windowState.translateOnPageReload;
      windowState.translateOnPageReload = null;

      lazy.console.log(
        `Translating on a page reload from "${lazy.TranslationsUtils.serializeLanguagePair(languagePair)}".`
      );

      this.translate(
        languagePair,
        false // reportAsAutoTranslate
      );
    }
  }

  /**
   * A map of the TranslationModelRecord["id"] to the record of the model in Remote Settings.
   * Used to coordinate the downloads.
   *
   * @type {null | Promise<Map<string, TranslationModelRecord>>}
   */
  static #translationModelRecords = null;

  /**
   * The RemoteSettingsClient that downloads the translation models.
   *
   * @type {RemoteSettingsClient | null}
   */
  static #translationModelsRemoteClient = null;

  /**
   * The RemoteSettingsClient that downloads the wasm binaries.
   *
   * @type {RemoteSettingsClient | null}
   */
  static #translationsWasmRemoteClient = null;

  /**
   * Allows the actor's behavior to be changed when the translations engine is mocked via
   * a dummy RemoteSettingsClient.
   *
   * @type {bool}
   */
  static #isTranslationsEngineMocked = false;

  /**
   * @type {null | Promise<boolean>}
   */
  static #isTranslationsEngineSupported = null;

  /**
   * An ordered list of preferred languages based on:
   *
   *   1. Most recent target languages
   *   2. Web requested languages
   *   3. App languages
   *   4. OS language
   *
   * This is the composition of #mostRecentTargetLanguages and #userSettingsLanguages
   *
   * @type {null | string[]}
   */
  static #preferredLanguages = null;

  /**
   * An ordered list of the most recently translated-into target languages.
   *
   * @type {null | string[]}
   */
  static #mostRecentTargetLanguages = null;

  /**
   * An ordered list of languages specified in the user's settings based on:
   *
   *   1. Web requested languages
   *   2. App languages
   *   3. OS languages
   *
   * @type {null | string[]}
   */
  static #userSettingsLanguages = null;

  /**
   * The value of navigator.languages.
   *
   * @type {null | Set<string>}
   */
  static #webContentLanguages = null;

  /**
   * A guard to ensure that we initialize static pref observers only once.
   *
   * @type {boolean}
   */
  static #observingPrefs = false;

  /**
   * A dedicated handle to this.#observe.bind(this), which we need to register non-static
   * per-instance observers when the actor is created as well as remove when it is destroyed.
   *
   * @type {Function | null}
   *
   * @see {TranslationsParent.actorCreated}
   * @see {TranslationsParent.didDestroy}
   */
  #boundObserve = null;

  // On a fast connection, 10 concurrent downloads were measured to be the fastest when
  // downloading all of the language files.
  static MAX_CONCURRENT_DOWNLOADS = 10;
  static MAX_DOWNLOAD_RETRIES = 3;

  // The set of hosts that have already been offered for translations.
  static #hostsOffered = new Set();

  // Enable the translations popup offer in tests.
  static testAutomaticPopup = false;

  /**
   * Gecko preference for always translating a language.
   *
   * @type {string}
   */
  static ALWAYS_TRANSLATE_LANGS_PREF = ALWAYS_TRANSLATE_LANGS_PREF;

  /**
   * Gecko preference for never translating a language.
   *
   * @type {string}
   */
  static NEVER_TRANSLATE_LANGS_PREF = NEVER_TRANSLATE_LANGS_PREF;

  /**
   * Telemetry functions for Translations
   *
   * @returns {TranslationsTelemetry}
   */
  static telemetry() {
    return lazy.TranslationsTelemetry;
  }

  /**
   * TODO(Bug 1834306) - Cu.isInAutomation doesn't recognize Marionette and RemoteAgent
   * tests.
   */
  static isInAutomation() {
    return (
      Cu.isInAutomation || lazy.Marionette.running || lazy.RemoteAgent.running
    );
  }

  /**
   * Returns whether the Translations Engine is mocked for testing.
   *
   * @returns {boolean}
   */
  static isTranslationsEngineMocked() {
    return TranslationsParent.#isTranslationsEngineMocked;
  }

  /**
   * Offer translations (for instance by automatically opening the popup panel) whenever
   * languages are detected, but only do it once per host per session.
   *
   * Keep this table up to date with:
   * browser/components/translations/tests/browser/browser_translations_full_page_language_id_behavior.js
   *
   * ┌──────────┬───────────┬───────────┬─────────────────────┐
   * │ Has HTML │ Detection │ Detection │ Outcome             │
   * │ Tag      │ Agrees    │ Confident │                     │
   * ├──────────┼───────────┼───────────┼─────────────────────┤
   * │ TRUE     │ TRUE      │ TRUE      │ Offer Matching Tag  │
   * │ TRUE     │ TRUE      │ FALSE     │ Offer Matching Tag  │
   * │ TRUE     │ FALSE     │ TRUE      │ Show Button Only    │
   * │ TRUE     │ FALSE     │ FALSE     │ Show Button Only    │
   * │ FALSE    │ N/A       │ TRUE      │ Offer Detected Tag  │
   * │ FALSE    │ N/A       │ FALSE     │ Show Button Only    │
   * └──────────┴───────────┴───────────┴─────────────────────┘
   *
   * @param {LangTags} detectedLanguages
   */
  async maybeOfferTranslations(detectedLanguages) {
    if (!this.browsingContext.currentWindowGlobal) {
      return;
    }
    if (!lazy.automaticallyPopupPref) {
      return;
    }

    // On Android the BrowserHandler is intermittently not available (for unknown reasons).
    // Check that the component is available before de-lazifying lazy.BrowserHandler.
    if (Cc["@mozilla.org/browser/clh;1"] && lazy.BrowserHandler?.kiosk) {
      // Pop-ups should not be shown in kiosk mode.
      return;
    }
    const { documentURI } = this.browsingContext.currentWindowGlobal;

    if (
      TranslationsParent.isInAutomation() &&
      !TranslationsParent.testAutomaticPopup
    ) {
      // Do not offer translations in automation, as many tests do not expect this
      // behavior.
      lazy.console.log(
        "maybeOfferTranslations - Do not offer translations in automation.",
        documentURI.spec
      );
      return;
    }

    if (
      !detectedLanguages.docLangTag ||
      !detectedLanguages.userLangTag ||
      !detectedLanguages.isDocLangTagSupported
    ) {
      lazy.console.log(
        "maybeOfferTranslations - The detected languages were not supported.",
        detectedLanguages
      );
      return;
    }

    const browser = this.browsingContext.top.embedderElement;
    if (!browser) {
      return;
    }

    if (
      TranslationsParent.shouldNeverTranslateLanguage(
        detectedLanguages.docLangTag
      )
    ) {
      lazy.console.log(
        `maybeOfferTranslations - Should never translate language. "${detectedLanguages.docLangTag}"`,
        documentURI.spec
      );
      return;
    }
    if (this.shouldNeverTranslateSite()) {
      lazy.console.log(
        "maybeOfferTranslations - Should never translate site.",
        documentURI.spec
      );
      return;
    }

    if (
      lazy.TranslationsUtils.langTagsMatch(
        detectedLanguages.docLangTag,
        detectedLanguages.userLangTag
      )
    ) {
      lazy.console.error(
        "maybeOfferTranslations - The document and user lang tag are the same, not offering a translation.",
        documentURI.spec
      );
      return;
    }

    // Before offering this translation, do a final language detection of the page.
    // Frequently pages' lang attributes are mislabeled. If there is a mismatch between
    // the identified and declared language, the translation icon will be shown, but the
    // popup will not be shown.
    if (
      detectedLanguages.htmlLangAttribute &&
      !detectedLanguages.identifiedLangTag
    ) {
      // Compare language langTagsMatch
      const identifyResult = await this.queryIdentifyLanguage();
      detectedLanguages.identifiedLangTag = identifyResult.language;
      detectedLanguages.identifiedLangConfident = identifyResult.confident;

      if (
        !lazy.TranslationsUtils.langTagsMatch(
          detectedLanguages.identifiedLangTag,
          detectedLanguages.docLangTag
        )
      ) {
        detectedLanguages.identifiedLangTag = Intl.getCanonicalLocales(
          detectedLanguages.identifiedLangTag
        )[0];
        if (
          !lazy.TranslationsUtils.langTagsMatch(
            detectedLanguages.identifiedLangTag,
            detectedLanguages.docLangTag
          )
        ) {
          if (!identifyResult.confident) {
            lazy.console.log(
              "The identified language was not confident, and the language tags don't match so don't offer a translation.",
              this.languageState.detectedLanguages
            );
            return;
          }

          // The identified language and the declared document language do not match,
          // but we are confident in the results of the contents of the page.

          const originalDocLangTag = detectedLanguages.docLangTag;
          // We support the identified language, use that as the preferred target
          // language. Duplicate the object so that it will be dispatched to any
          // consumers that are using it.
          detectedLanguages = {
            ...detectedLanguages,
            docLangTag: detectedLanguages.identifiedLangTag,
          };
          this.languageState.detectedLanguages = detectedLanguages;

          if (originalDocLangTag) {
            lazy.console.log(
              "maybeOfferTranslations - The document language tag was changed, but there was an original language, so don't offer.",
              documentURI.spec,
              detectedLanguages
            );
            return;
          }

          if (
            !TranslationsParent.findCompatibleSourceLangTagSync(
              detectedLanguages.identifiedLangTag,
              await TranslationsParent.getNonPivotLanguagePairs()
            )
          ) {
            lazy.console.log(
              "maybeOfferTranslations - There was no original language tag, but the detected language is not supported.",
              documentURI.spec,
              detectedLanguages
            );
            return;
          }
        }
      }
    }

    // Do the host check after the language identify check so that the translations popup
    // will update the language correctly.
    let host;
    try {
      host = documentURI.host;
    } catch {
      // nsIURI.host can throw if the URI scheme doesn't have a host. In this case
      // do not offer a translation.
      return;
    }
    if (TranslationsParent.#hostsOffered.has(host)) {
      // This host was already offered a translation.
      lazy.console.log(
        "maybeOfferTranslations - Host already offered a translation, so skip.",
        documentURI.spec
      );
      return;
    }
    TranslationsParent.#hostsOffered.add(host);

    // Only offer the translation if it's still the current page.
    let isCurrentPage = false;
    if (AppConstants.platform !== "android") {
      isCurrentPage =
        documentURI.spec ===
        this.browsingContext.topChromeWindow.gBrowser.selectedBrowser
          .documentURI.spec;
    } else {
      // In Android, the active window is the active tab.
      isCurrentPage = documentURI.spec === browser.documentURI.spec;
    }
    if (isCurrentPage) {
      lazy.console.log(
        "maybeOfferTranslations - Offering a translation",
        documentURI.spec,
        detectedLanguages
      );

      const { CustomEvent } = browser.ownerGlobal;
      browser.dispatchEvent(
        new CustomEvent("TranslationsParent:OfferTranslation", {
          bubbles: true,
        })
      );
    }
  }

  /**
   * This is for testing purposes.
   */
  static resetHostsOffered() {
    TranslationsParent.#hostsOffered = new Set();
  }

  /**
   * Returns the word count of the text for a given language.
   *
   * @param {string} langTag - A BCP-47 language tag.
   * @param {string} text - The text for which to count words.
   *
   * @returns {number} - The count of words in the text.
   * @throws If a segmenter could not be created for the given language tag.
   */
  static countWords(langTag, text) {
    const segmenter = new Intl.Segmenter(langTag, { granularity: "word" });
    const segments = Array.from(segmenter.segment(text));
    return segments.filter(segment => segment.isWordLike).length;
  }

  /**
   * Retrieves the Translations actor from the current browser context.
   *
   * @param {object} browser - The browser object from which to get the context.
   *
   * @returns {object} The Translations actor for handling translation actions.
   * @throws {Error} Throws an error if the TranslationsParent actor cannot be found.
   */
  static getTranslationsActor(browser) {
    const actor =
      browser.browsingContext.currentWindowGlobal.getActor("Translations");

    if (!actor) {
      throw new Error("Unable to get the TranslationsParent actor.");
    }
    return actor;
  }

  /**
   * Detect if Wasm SIMD is supported, and cache the value. It's better to check
   * for support before downloading large binary blobs to a user who can't even
   * use the feature. This function also respects mocks and simulating unsupported
   * engines.
   *
   * @type {boolean}
   */
  static getIsTranslationsEngineSupported() {
    if (lazy.simulateUnsupportedEnginePref) {
      // Use the non-lazy console.log so that the user is always informed as to why
      // the translations engine is not working.
      console.log(
        "Translations: The translations engine is disabled through the pref " +
          '"browser.translations.simulateUnsupportedEngine".'
      );

      // The user is manually testing unsupported engines.
      return false;
    }

    if (TranslationsParent.#isTranslationsEngineMocked) {
      // A mocked translations engine is always supported.
      return true;
    }

    if (TranslationsParent.#isTranslationsEngineSupported === null) {
      TranslationsParent.#isTranslationsEngineSupported = detectSimdSupport();
    }

    return TranslationsParent.#isTranslationsEngineSupported;
  }

  /**
   * Only translate pages that match certain protocols, that way internal pages like
   * about:* pages will not be translated. Keep this logic up to date with the "matches"
   * array in the `toolkit/modules/ActorManagerParent.sys.mjs` definition.
   *
   * @param {object} gBrowser
   * @returns {boolean}
   */
  static isFullPageTranslationsRestrictedForPage(gBrowser) {
    const contentType = gBrowser.selectedBrowser.documentContentType;
    const scheme = gBrowser.currentURI.scheme;

    if (contentType === "application/pdf") {
      return true;
    }

    // Keep this logic up to date with the "matches" array in the
    // `toolkit/modules/ActorManagerParent.sys.mjs` definition.
    switch (scheme) {
      case "https":
      case "http":
      case "file":
      case "moz-extension":
        return false;
    }
    return true;
  }

  /**
   * Invalidates the #mostRecentTargetLanguages portion of #preferredLanguages.
   *
   * This means that the next time getPreferredLanguages() is called, it will
   * need to re-fetch the mostRecentTargetLanguages, but it may still use a
   * cached version of userSettingsLanguages.
   *
   * @see {getPreferredLanguages}
   */
  static #invalidateMostRecentTargetLanguages() {
    TranslationsParent.#mostRecentTargetLanguages = null;
    TranslationsParent.#preferredLanguages = null;
    Services.obs.notifyObservers(null, TOPIC_MAYBE_UPDATE_USER_LANG_TAG);
  }

  /**
   * Invalidates the #userSettingsLanguages portion of #preferredLanguages.
   *
   * This means that the next time getPreferredLanguages() is called, it will
   * need to re-fetch the userSettingsLanguages, but it may still use a
   * cached version of mostRecentTargetLanguages.
   *
   * @see {getPreferredLanguages}
   */
  static #invalidateUserSettingsLanguages() {
    TranslationsParent.#webContentLanguages = null;
    TranslationsParent.#userSettingsLanguages = null;
    TranslationsParent.#preferredLanguages = null;
  }

  /**
   * Provide a way for tests to override the system locales.
   *
   * @type {null | string[]}
   */
  static mockedSystemLocales = null;

  /**
   * The "Accept-Language" values that the localizer or user has indicated for
   * the preferences for the web. https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Accept-Language
   *
   * Note that this preference always has English in the fallback chain, even if the
   * user doesn't actually speak English, and to other languages they potentially do
   * not speak. However, this preference will be used as an indication that a user may
   * prefer this language.
   *
   * https://transvision.flod.org/string/?entity=toolkit/chrome/global/intl.properties:intl.accept_languages&repo=gecko_strings
   */
  static getWebContentLanguages() {
    if (!TranslationsParent.#webContentLanguages) {
      const values = Services.prefs
        .getComplexValue(ACCEPT_LANGUAGES_PREF, Ci.nsIPrefLocalizedString)
        .data.split(/\s*,\s*/g);

      TranslationsParent.#webContentLanguages = new Set();

      for (const locale of values) {
        try {
          // Wrap this in a try statement since users can manually edit this pref.
          TranslationsParent.#webContentLanguages.add(
            new Intl.Locale(locale).baseName
          );
        } catch {
          // The locale was invalid, discard it.
        }
      }

      if (
        !Services.prefs.prefHasUserValue(ACCEPT_LANGUAGES_PREF) &&
        Services.locale.appLocaleAsBCP47 !== "en" &&
        !Services.locale.appLocaleAsBCP47.startsWith("en-")
      ) {
        // The user hasn't customized their accept languages, this means that English
        // is always provided as a fallback language, even if it is not available.
        TranslationsParent.#webContentLanguages.delete("en");
        TranslationsParent.#webContentLanguages.delete("en-US");
      }

      if (TranslationsParent.#webContentLanguages.size === 0) {
        // The user has removed all of their web content languages, default to the
        // app locale.
        TranslationsParent.#webContentLanguages.add(
          new Intl.Locale(Services.locale.appLocaleAsBCP47).baseName
        );
      }
    }

    return TranslationsParent.#webContentLanguages;
  }

  /**
   * Retrieves the most recently translated-into target languages.
   *
   * This will return a cached value unless #invalidateMostRecentTargetLanguages
   * has been called.
   *
   * @see {#invalidateMostRecentTargetLanguages}
   *
   * @returns {string[]} - An ordered list of the most recent target languages.
   */
  static #getMostRecentTargetLanguages() {
    if (TranslationsParent.#mostRecentTargetLanguages) {
      return TranslationsParent.#mostRecentTargetLanguages;
    }

    // Store the mostRecentTargetLanguage values in reverse order
    // so that the most recently used language is first in the array.
    TranslationsParent.#mostRecentTargetLanguages = [
      ...lazy.mostRecentTargetLanguages,
    ].reverse();

    return TranslationsParent.#mostRecentTargetLanguages;
  }

  /**
   * Returns true if the active user has ever triggered a translation request, otherwise false.
   *
   * @returns {boolean}
   */
  static hasUserEverTranslated() {
    return !!TranslationsParent.#getMostRecentTargetLanguages().length;
  }

  /**
   * Retrieves the user's preferred languages from the settings based on:
   *
   *   1. Web requested languages
   *   2. App languages
   *   3. OS language
   *
   * This will return a cached value unless #invalidateUserSettingsLanguages
   * has been called.
   *
   * @see {#invalidateUserSettingsLanguages}
   *
   * @returns {string[]} - An ordered list of the user's settings languages.
   */
  static #getUserSettingsLanguages() {
    if (TranslationsParent.#userSettingsLanguages) {
      return TranslationsParent.#userSettingsLanguages;
    }

    // The system language could also be a good option for a language to offer the user.
    const osPrefs = Cc["@mozilla.org/intl/ospreferences;1"].getService(
      Ci.mozIOSPreferences
    );
    const systemLocales =
      TranslationsParent.mockedSystemLocales ?? osPrefs.systemLocales;

    // Combine the locales together.
    const userSettingsLocales = new Set([
      ...TranslationsParent.getWebContentLanguages(),
      ...Services.locale.appLocalesAsBCP47,
      ...systemLocales,
    ]);

    // Attempt to convert the locales to lang tags. Do not completely trust the
    // values coming from preferences and the OS to have been validated as correct
    // BCP 47 locale identifiers.
    const userSettingsLangTags = new Set();
    for (const locale of userSettingsLocales) {
      try {
        userSettingsLangTags.add(new Intl.Locale(locale).baseName);
      } catch (_) {
        // The locale was invalid, discard it.
      }
    }

    // Convert the Set to an array to indicate that it is an ordered listing of languages.
    TranslationsParent.#userSettingsLanguages = [...userSettingsLangTags];
    return TranslationsParent.#userSettingsLanguages;
  }

  /**
   * Initializes static pref observers exactly once the first time this is called.
   * Does nothing on subsequent calls.
   */
  static #maybeStartObservingPrefs() {
    if (TranslationsParent.#observingPrefs) {
      // We have already initialized the observers.
      return;
    }

    /**
     * This one pref is special and requires its own observer.
     * through Services.prefs.
     *
     * We cannot make a lazy pref getter for this pref, because
     * it needs to be retrieved using Ci.nsIPrefLocalizedString
     * which defineLazyPreferenceGetter does not currently support.
     *
     * Retrieving the pref with Ci.nsIPrefLocalizedString allows
     * its default value to be pulled from a properties file.
     *
     * @see {TranslationsParent.getWebContentLanguages}
     */
    Services.prefs.addObserver(
      ACCEPT_LANGUAGES_PREF,
      TranslationsParent.#observeStatic
    );

    /**
     * An observer for all other Translations-relevant pref changes.
     */
    Services.obs.addObserver(
      TranslationsParent.#observeStatic,
      TOPIC_TRANSLATIONS_PREF_CHANGED
    );

    /**
     * An observer for if the application locales change.
     */
    Services.obs.addObserver(
      TranslationsParent.#observeStatic,
      TOPIC_APP_LOCALES_CHANGED
    );

    TranslationsParent.#observingPrefs = true;
  }

  /**
   * Observes notifications from a given subject, handling them according to the topic.
   *
   * @param {nsISupports} subject
   * @param {string} topic
   * @param {string} data
   *
   * @see {nsIObserver}
   */
  #observe(subject, topic, data) {
    lazy.console.debug(this.#observe.name, { subject, topic, data });

    switch (topic) {
      case TOPIC_MAYBE_UPDATE_USER_LANG_TAG: {
        this.#maybeUpdateUserLangTag();
        break;
      }
      default: {
        lazy.console.error(
          `Unexpected topic observed by TranslationsParent actor: '${topic}'`
        );
      }
    }
  }

  /**
   * A static observer method that listens for changes to preferences and other
   * Translations-relevant settings, invalidating caches or reacting to changes
   * as needed.
   *
   * @param {nsISupports} subject
   * @param {string} topic
   * @param {string} data
   *
   * @see {nsIObserver}
   */
  static #observeStatic(subject, topic, data) {
    lazy.console.debug(TranslationsParent.#observeStatic.name, {
      subject,
      topic,
      data,
    });
    switch (topic) {
      case TOPIC_APP_LOCALES_CHANGED: {
        TranslationsParent.#invalidateUserSettingsLanguages();
        break;
      }
      case TOPIC_NS_PREF_CHANGED: {
        switch (data) {
          case ACCEPT_LANGUAGES_PREF: {
            TranslationsParent.#invalidateUserSettingsLanguages();
            break;
          }
        }
        break;
      }
      case TOPIC_TRANSLATIONS_PREF_CHANGED: {
        switch (data) {
          case USE_LEXICAL_SHORTLIST_PREF: {
            // This is an extreme edge case where someone would flip the useLexicalShortlist
            // pref during an active translation. Most people will not be flipping this pref
            // at all, much less during a translation. But if it does happen, we should destroy
            // the current engine to be rebuilt with the new configuration.
            lazy.EngineProcess.destroyTranslationsEngine()
              .catch(error => lazy.console.error(error))
              .finally(TranslationsParent.#invalidateTranslationModelRecords);

            break;
          }
          case MOST_RECENT_TARGET_LANGS_PREF: {
            TranslationsParent.#invalidateMostRecentTargetLanguages();
          }
        }
        break;
      }
      default: {
        lazy.console.error(
          `Unexpected topic observed by TranslationsParent: '${topic}'`
        );
      }
    }
  }

  /**
   * Updates the user's language tag if it has changed from the current.
   */
  #maybeUpdateUserLangTag() {
    const langTag = TranslationsParent.getPreferredLanguages({
      excludeLangTags: [this.languageState.detectedLanguages?.docLangTag],
    })[0];
    this.languageState.maybeUpdateUserLangTag(langTag);
  }

  /**
   * An ordered list of preferred languages based on:
   *
   *   1. Most recent target languages
   *   2. Web requested languages
   *   3. App languages
   *   4. OS language
   *
   * @param {object} options
   * @param {string[]} [options.excludeLangTags] - BCP-47 language tags to intentionally exclude.
   *
   * @returns {string[]}
   */
  static getPreferredLanguages({ excludeLangTags } = {}) {
    if (TranslationsParent.#preferredLanguages) {
      return TranslationsParent.#preferredLanguages.filter(
        langTag =>
          !excludeLangTags?.some(langTagToExclude =>
            lazy.TranslationsUtils.langTagsMatch(langTagToExclude, langTag)
          )
      );
    }

    TranslationsParent.#maybeStartObservingPrefs();

    const preferredLanguages = new Set([
      ...TranslationsParent.#getMostRecentTargetLanguages(),
      ...TranslationsParent.#getUserSettingsLanguages(),
    ]);

    // Convert the Set to an array to indicate that it is an ordered listing of languages.
    TranslationsParent.#preferredLanguages = [...preferredLanguages];

    return TranslationsParent.#preferredLanguages.filter(
      langTag =>
        !excludeLangTags?.some(langTagToExclude =>
          lazy.TranslationsUtils.langTagsMatch(langTagToExclude, langTag)
        )
    );
  }

  /**
   * Requests a new translations port.
   *
   * @param {LanguagePair} languagePair
   * @param {TranslationsParent} [translationsParent] - A TranslationsParent actor instance.
   *   NOTE: This value should be provided only if your port is associated with Full Page Translations.
   *   This will associate this translations port with the TranslationsParent actor instance, which will mean that changes
   *   in the translation state will affect the state of the Full-Page Translations UI, e.g. the URL-bar Translations button.
   *
   * @returns {Promise<MessagePort | undefined>} The port for communication with the translation engine, or undefined on failure.
   */
  static async requestTranslationsPort(languagePair, translationsParent) {
    let translationsEngineParent;
    try {
      translationsEngineParent =
        await lazy.EngineProcess.getTranslationsEngineParent();
    } catch (error) {
      lazy.console.error("Failed to get the translation engine process", error);
      return undefined;
    }

    if (translationsParent) {
      // NOTE: It's OK if this overrides an existing engine actor reference, as
      // only one TranslationsEngineParent instance may be active at a time.
      translationsParent.engineActor = translationsEngineParent;
    }

    // The MessageChannel will be used for communicating directly between the content
    // process and the engine's process.
    const { port1, port2 } = new MessageChannel();
    translationsEngineParent.startTranslation(
      languagePair,
      port1,
      translationsParent
    );

    return port2;
  }

  async receiveMessage({ name, data }) {
    switch (name) {
      case "Translations:ReportLangTags": {
        const { htmlLangAttribute, href } = data;
        const detectedLanguages = await this.getDetectedLanguages(
          htmlLangAttribute,
          href
        ).catch(error => {
          // Detecting the languages can fail if the page gets destroyed before it
          // can be completed. This runs on every page that doesn't have a lang tag,
          // so only report the error if you have Translations logging turned on to
          // avoid console spam.
          lazy.console.log("Failed to get the detected languages.", error);
        });

        if (!detectedLanguages) {
          // The actor was already destroyed, and the detectedLanguages weren't reported
          // in time.
          return undefined;
        }

        this.languageState.detectedLanguages = detectedLanguages;

        if (await this.shouldAutoTranslate(detectedLanguages)) {
          this.translate(
            {
              sourceLanguage: detectedLanguages.docLangTag,
              targetLanguage: detectedLanguages.userLangTag,
            },
            true // reportAsAutoTranslate
          );
        } else {
          this.maybeOfferTranslations(detectedLanguages).catch(error =>
            lazy.console.error(error)
          );
        }
        return undefined;
      }
      case "Translations:RequestPort": {
        const { requestedLanguagePair } = this.languageState;
        if (!requestedLanguagePair) {
          lazy.console.error(
            "A port was requested but no language pair was previously requested"
          );
          return undefined;
        }

        if (this.#isDestroyed) {
          // This actor was already destroyed.
          return undefined;
        }

        if (!this.innerWindowId) {
          throw new Error(
            "The innerWindowId for the TranslationsParent was not available."
          );
        }

        const port = await TranslationsParent.requestTranslationsPort(
          requestedLanguagePair,
          this
        );

        if (!port) {
          lazy.console.error(
            `Failed to create a translations port for language pair: ${lazy.TranslationsUtils.serializeLanguagePair(requestedLanguagePair)}`
          );
          return undefined;
        }

        this.sendAsyncMessage(
          "Translations:AcquirePort",
          { port },
          [port] // Mark the port as transferable.
        );

        return undefined;
      }
      case "Translations:ReportFirstVisibleChange": {
        this.languageState.hasVisibleChange = true;
      }
    }
    return undefined;
  }

  /**
   * @param {LanguagePair} languagePair
   */
  static async getTranslationsEnginePayload(languagePair) {
    const wasmStartTime = Cu.now();
    const bergamotWasmArrayBufferPromise =
      TranslationsParent.#getBergamotWasmArrayBuffer();
    bergamotWasmArrayBufferPromise
      .then(() => {
        ChromeUtils.addProfilerMarker(
          "TranslationsParent",
          { innerWindowId: this.innerWindowId, startTime: wasmStartTime },
          "Loading bergamot wasm array buffer"
        );
      })
      .catch(() => {
        // Do nothing.
      });

    const modelStartTime = Cu.now();

    /** @type {TranslationModelPayload[]} */
    const translationModelPayloads = [];
    const { sourceLanguage, targetLanguage, sourceVariant, targetVariant } =
      languagePair;
    if (sourceLanguage === PIVOT_LANGUAGE) {
      translationModelPayloads.push(
        await TranslationsParent.getTranslationModelPayload(
          sourceLanguage,
          targetLanguage,
          targetVariant
        )
      );
    } else if (targetLanguage === PIVOT_LANGUAGE) {
      translationModelPayloads.push(
        await TranslationsParent.getTranslationModelPayload(
          sourceLanguage,
          targetLanguage,
          sourceVariant
        )
      );
    } else {
      // No matching model was found, try to pivot between English.
      translationModelPayloads.push(
        ...(await Promise.all([
          TranslationsParent.getTranslationModelPayload(
            sourceLanguage,
            PIVOT_LANGUAGE,
            sourceVariant
          ),
          TranslationsParent.getTranslationModelPayload(
            PIVOT_LANGUAGE,
            targetLanguage,
            targetVariant
          ),
        ]))
      );
    }

    ChromeUtils.addProfilerMarker(
      "TranslationsParent",
      { innerWindowId: this.innerWindowId, startTime: modelStartTime },
      "Loading translation model files"
    );

    const bergamotWasmArrayBuffer = await bergamotWasmArrayBufferPromise;

    return {
      bergamotWasmArrayBuffer,
      translationModelPayloads,
      isMocked: TranslationsParent.#isTranslationsEngineMocked,
    };
  }

  /**
   * Returns true if translations should auto-translate from the given
   * language, otherwise returns false.
   *
   * @param {LangTags} langTags
   * @returns {boolean}
   */
  #maybeAutoTranslate(langTags) {
    const windowState = this.getWindowState();
    if (windowState.isPageRestored) {
      // The user clicked the restore button. Respect it for one page load.
      windowState.isPageRestored = false;

      // Skip this auto-translation.
      return false;
    }

    return TranslationsParent.shouldAlwaysTranslateLanguage(langTags);
  }

  /**
   * Creates a lookup key that is unique to each sourceLanguage-targetLanguage pair.
   *
   * @param {string} sourceLanguage
   * @param {string} targetLanguage
   * @param {string} [variant]
   * @returns {string}
   */
  static nonPivotKey(sourceLanguage, targetLanguage, variant) {
    return variant
      ? `${sourceLanguage},${targetLanguage},${variant}`
      : `${sourceLanguage},${targetLanguage}`;
  }

  /**
   * The cached language pairs.
   *
   * @type {Promise<Array<LanguagePair>> | null}
   */
  static #languagePairs = null;

  /**
   * Clears the cached list of language pairs, notifying observers that the
   * available language pairs have changed.
   */
  static #invalidateLanguagePairs() {
    TranslationsParent.#languagePairs = null;
    Services.obs.notifyObservers(null, "translations:language-pairs-changed");
  }

  /**
   * Clears the cached promise to the translation model records. These will
   * have to be re-fetched the next time they are queried.
   */
  static #invalidateTranslationModelRecords() {
    TranslationsParent.#translationModelRecords = null;
    Services.obs.notifyObservers(null, "translations:model-records-changed");
  }

  /**
   * Get the list of language pairs supported by the translations engine.
   *
   * @returns {Promise<Array<NonPivotLanguagePair>>}
   */
  static getNonPivotLanguagePairs() {
    if (!TranslationsParent.#languagePairs) {
      TranslationsParent.#languagePairs =
        TranslationsParent.#getTranslationModelRecords().then(records => {
          const languagePairMap = new Map();

          for (const {
            fromLang: sourceLanguage,
            toLang: targetLanguage,
            variant,
          } of records.values()) {
            const key = TranslationsParent.nonPivotKey(
              sourceLanguage,
              targetLanguage,
              variant
            );
            if (!languagePairMap.has(key)) {
              languagePairMap.set(key, {
                sourceLanguage,
                targetLanguage,
                variant,
              });
            }
          }
          return Array.from(languagePairMap.values());
        });
      TranslationsParent.#languagePairs.catch(() => {
        TranslationsParent.#invalidateLanguagePairs();
      });
    }
    return TranslationsParent.#languagePairs;
  }

  /**
   * Get the list of languages and their display names, sorted by their display names.
   * This is more expensive of a call than getNonPivotLanguagePairs since the display
   * names are looked up.
   *
   * This is all of the information needed to render dropdowns for translation
   * language selection.
   *
   * @returns {Promise<SupportedLanguages>}
   */
  static async getSupportedLanguages() {
    await chaosMode(1 / 4);
    const languagePairs = await TranslationsParent.getNonPivotLanguagePairs();

    /** @type {Set<string>} */
    const sourceLanguageKeys = new Set();
    /** @type {Set<string>} */
    const targetLanguageKeys = new Set();

    for (const { sourceLanguage, targetLanguage, variant } of languagePairs) {
      if (sourceLanguage === PIVOT_LANGUAGE) {
        // Ignore variants for the pivot language, as every variant targets English.
        sourceLanguageKeys.add(PIVOT_LANGUAGE);
      } else {
        sourceLanguageKeys.add(
          variant ? `${sourceLanguage},${variant}` : sourceLanguage
        );
      }
      targetLanguageKeys.add(
        variant ? `${targetLanguage},${variant}` : targetLanguage
      );
    }

    // Build a map of the langTag to the display name.
    /** @type {Map<string, string>} */
    const displayNames = new Map();
    {
      const languageDisplayNames =
        TranslationsParent.createLanguageDisplayNames();

      for (const langTagSet of [sourceLanguageKeys, targetLanguageKeys]) {
        for (const langTagKey of langTagSet) {
          const [langTag] = langTagKey.split(",");
          if (displayNames.has(langTag)) {
            continue;
          }
          displayNames.set(langTag, languageDisplayNames.of(langTag));
        }
      }
    }

    const addDisplayName = langTagKey => {
      const [langTag, variant] = langTagKey.split(",");
      let displayName = displayNames.get(langTag);
      if (variant) {
        // Right now if there is a variant always append the variant name, but in the
        // future it might be a good idea to not show the variant name if there is only
        // 1 variant for a language. For now this is only developer facing. This is also
        // why Fluent isn't used here, as it's not exposed to end users.
        //
        // The display needs to work with languages that use script tags,
        // e.g. "Chinese (Traditional) - base".
        //      "Spanish - decoder-bigger-embeddings".
        displayName = `${displayName} - ${variant}`;
      }
      return { langTag, variant, langTagKey, displayName };
    };

    const sort = (a, b) => a.displayName.localeCompare(b.displayName);

    return {
      languagePairs,
      sourceLanguages: Array.from(sourceLanguageKeys.keys())
        .map(addDisplayName)
        .sort(sort),
      targetLanguages: Array.from(targetLanguageKeys.keys())
        .map(addDisplayName)
        .sort(sort),
    };
  }

  /**
   * Create a unique list of languages, sorted by the display name.
   *
   * @param {object} supportedLanguages
   * @returns {Array<{ langTag: string, displayName: string}>}
   */
  static getLanguageList(supportedLanguages) {
    const displayNames = new Map();
    for (const languages of [
      supportedLanguages.sourceLanguages,
      supportedLanguages.targetLanguages,
    ]) {
      for (const { langTag, displayName } of languages) {
        displayNames.set(langTag, displayName);
      }
    }

    const appLangTag = Services.locale.appLocaleAsBCP47;
    for (const langTag of displayNames.keys()) {
      if (lazy.TranslationsUtils.langTagsMatch(langTag, appLangTag)) {
        displayNames.delete(langTag);
        break;
      }
    }

    // Sort the list of languages by the display names.
    return [...displayNames.entries()]
      .map(([langTag, displayName]) => ({
        langTag,
        displayName,
      }))
      .sort((a, b) => a.displayName.localeCompare(b.displayName));
  }

  /**
   * Creates and retrieves an `Intl.DisplayNames` object for displaying languages
   * in translation-related user interfaces across the browser.
   *
   * @param {Record<string, string>} [options={}]
   *  - Optional parameters to customize the display of language names.
   * @param {string} [options.fallback="code"]
   *  - Determines the behavior when a language display name is unavailable:
   *    "code": Fallback to the language code.
   *    "none": No fallback; return `undefined`.
   * @param {string} [options.languageDisplay="standard"]
   *  - Specifies how to display the language names:
   *    "standard": Display the standard form of the language name e.g. "Chinese (Simplified)"
   *    "dialect": Display the dialect form if available e.g. "Simplified Chinese"
   *
   * @returns {Intl.DisplayNames}
   *   An `Intl.DisplayNames` object configured to format language names according to the given options.
   *
   * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/DisplayNames
   */
  static createLanguageDisplayNames({
    fallback = "code",
    languageDisplay = "standard",
  } = {}) {
    return new Services.intl.DisplayNames(Services.locale.appLocaleAsBCP47, {
      type: "language",
      languageDisplay,
      fallback,
    });
  }

  /**
   * Handles records that were deleted in a Remote Settings "sync" event by
   * attempting to delete any previously downloaded attachments that are
   * associated with the deleted records.
   *
   * @param {RemoteSettingsClient} client
   *  - The Remote Settings client for which to handle deleted records.
   * @param {TranslationModelRecord[]} deletedRecords
   *  - The list of records that were deleted from the client's database.
   */
  static async #handleDeletedRecords(client, deletedRecords) {
    // Attempt to delete any downloaded attachments that are associated with deleted records.
    const failedDeletions = [];
    await Promise.all(
      deletedRecords.map(async record => {
        try {
          if (await client.attachments.isDownloaded(record)) {
            await client.attachments.deleteDownloaded(record);
          }
        } catch (error) {
          failedDeletions.push({ record, error });
        }
      })
    );

    // Report deletion failures if any occurred.
    if (failedDeletions.length) {
      lazy.console.warn(
        'Remote Settings "sync" event failed to delete attachments for deleted records.'
      );
      for (const { record, error } of failedDeletions) {
        lazy.console.error(
          `Failed to delete attachment for deleted record ${record.name}: ${error}`
        );
      }
    }
  }

  /**
   * Handles records that were updated in a Remote Settings "sync" event by
   * attempting to delete any previously downloaded attachments that are
   * associated with the old record versions, then downloading attachments
   * that are associated with the new record versions.
   *
   * @param {RemoteSettingsClient} client
   *  - The Remote Settings client for which to handle updated records.
   * @param {{old: TranslationModelRecord, new: TranslationModelRecord}[]} updatedRecords
   *  - The list of records that were updated in the client's database.
   */
  static async #handleUpdatedRecords(client, updatedRecords) {
    // Gather any updated records whose attachments were previously downloaded.
    const recordsWithAttachmentsToReplace = [];
    for (const {
      old: recordBeforeUpdate,
      new: recordAfterUpdate,
    } of updatedRecords) {
      if (await client.attachments.isDownloaded(recordBeforeUpdate)) {
        recordsWithAttachmentsToReplace.push({
          recordBeforeUpdate,
          recordAfterUpdate,
        });
      }
    }

    // Attempt to delete all of the attachments for the old versions of the updated records.
    const failedDeletions = [];
    await Promise.all(
      recordsWithAttachmentsToReplace.map(async ({ recordBeforeUpdate }) => {
        try {
          await client.attachments.deleteDownloaded(recordBeforeUpdate);
        } catch (error) {
          failedDeletions.push({ record: recordBeforeUpdate, error });
        }
      })
    );

    // Report deletion failures if any occurred.
    if (failedDeletions.length) {
      lazy.console.warn(
        'Remote Settings "sync" event failed to delete old record attachments for updated records.'
      );
      for (const { record, error } of failedDeletions) {
        lazy.console.error(
          `Failed to delete old attachment for updated record ${record.name}: ${error.reason}`
        );
      }
    }

    // Attempt to download all of the attachments for the new versions of the updated records.
    const failedDownloads = [];
    await Promise.all(
      recordsWithAttachmentsToReplace.map(async ({ recordAfterUpdate }) => {
        try {
          await client.attachments.download(recordAfterUpdate);
        } catch (error) {
          failedDownloads.push({ record: recordAfterUpdate, error });
        }
      })
    );

    // Report deletion failures if any occurred.
    if (failedDownloads.length) {
      lazy.console.warn(
        'Remote Settings "sync" event failed to download new record attachments for updated records.'
      );
      for (const { record, error } of failedDeletions) {
        lazy.console.error(
          `Failed to download new attachment for updated record ${record.name}: ${error.reason}`
        );
      }
    }
  }

  /**
   * Handles the "sync" event for the Translations Models Remote Settings collection.
   * This is called whenever models are created, updated, or deleted from the Remote Settings database.
   *
   * @param {object} event - The sync event.
   * @param {object} event.data - The data associated with the sync event.
   * @param {TranslationModelRecord[]} event.data.created
   *  - The list of Remote Settings records that were created in the sync event.
   * @param {{old: TranslationModelRecord, new: TranslationModelRecord}[]} event.data.updated
   *  - The list of Remote Settings records that were updated in the sync event.
   * @param {TranslationModelRecord[]} event.data.deleted
   *  - The list of Remote Settings records that were deleted in the sync event.
   */
  static async #handleTranslationsModelsSync({
    data: { created, updated, deleted },
  }) {
    const client = TranslationsParent.#translationModelsRemoteClient;
    if (!client) {
      lazy.console.error(
        "Translations models client was not present when receiving a sync event."
      );
      return;
    }

    // Invalidate cached data.
    TranslationsParent.#invalidateLanguagePairs();
    TranslationsParent.#invalidateTranslationModelRecords();

    // Language model attachments will only be downloaded when they are used.
    lazy.console.log(
      `Remote Settings "sync" event for language-model records`,
      {
        created,
        updated,
        deleted,
      }
    );

    if (deleted.length) {
      await TranslationsParent.#handleDeletedRecords(client, deleted);
    }

    if (updated.length) {
      await TranslationsParent.#handleUpdatedRecords(client, updated);
    }

    // There is nothing to do for created records, since they will not have any previously downloaded attachments.
  }

  /**
   * Handles the "sync" event for the Translations WASM Remote Settings collection.
   * This is called whenever models are created, updated, or deleted from the Remote Settings database.
   *
   * @param {object} event - The sync event.
   * @param {object} event.data - The data associated with the sync event.
   * @param {TranslationModelRecord[]} event.data.created
   *  - The list of Remote Settings records that were created in the sync event.
   * @param {{old: TranslationModelRecord, new: TranslationModelRecord}[]} event.data.updated
   *  - The list of Remote Settings records that were updated in the sync event.
   * @param {TranslationModelRecord[]} event.data.deleted
   *  - The list of Remote Settings records that were deleted in the sync event.
   */
  static async #handleTranslationsWasmSync({
    data: { created, updated, deleted },
  }) {
    const client = TranslationsParent.#translationsWasmRemoteClient;
    if (!client) {
      lazy.console.error(
        "Translations WASM client was not present when receiving a sync event."
      );
      return;
    }

    lazy.console.log(`Remote Settings "sync" event for WASM records`, {
      created,
      updated,
      deleted,
    });

    // Invalidate cached data.
    TranslationsParent.#bergamotWasmRecord = null;

    if (deleted.length) {
      await TranslationsParent.#handleDeletedRecords(client, deleted);
    }

    if (updated.length) {
      await TranslationsParent.#handleUpdatedRecords(client, updated);
    }

    // There is nothing to do for created records, since they will not have any previously downloaded attachments.
  }

  /**
   * Lazily initializes the RemoteSettingsClient for the language models.
   *
   * @returns {RemoteSettingsClient}
   */
  static #getTranslationModelsRemoteClient() {
    if (TranslationsParent.#translationModelsRemoteClient) {
      return TranslationsParent.#translationModelsRemoteClient;
    }

    /** @type {RemoteSettingsClient} */
    const client = lazy.RemoteSettings("translations-models");
    TranslationsParent.#translationModelsRemoteClient = client;
    client.on("sync", TranslationsParent.#handleTranslationsModelsSync);

    return client;
  }

  /**
   * Retrieves the maximum compatible major version of each record in the RemoteSettingsClient.
   *
   * If the client contains two different-version copies of the same record (e.g. 1.0 and 1.1)
   * then only the 1.1-version record will be returned in the resulting collection.
   *
   * @param {RemoteSettingsClient} remoteSettingsClient
   * @param {object} [options]
   *   @param {object} [options.filters={}]
   *     The filters to apply when retrieving the records from RemoteSettings.
   *     Filters should correspond to properties on the RemoteSettings records themselves.
   *     For example, A filter to retrieve only records with a `fromLang` value of "en" and a `toLang` value of "es":
   *     { filters: { fromLang: "en", toLang: "es" } }
   *   @param {number} options.minSupportedMajorVersion
   *     The minimum major record version that is supported in this build of Firefox.
   *   @param {number} options.maxSupportedMajorVersion
   *     The maximum major record version that is supported in this build of Firefox.
   *   @param {Function} [options.lookupKey=(record => record.name)]
   *     The function to use to extract a lookup key from each record.
   *     This function should take a record as input and return a string that represents the lookup key for the record.
   *     For most record types, the name (default) is sufficient, however if a collection contains records with
   *     non-unique name values, it may be necessary to provide an alternative function here.
   * @returns {Array<TranslationModelRecord | WasmRecord>}
   */
  static async getMaxSupportedVersionRecords(
    remoteSettingsClient,
    {
      filters = {},
      minSupportedMajorVersion,
      maxSupportedMajorVersion,
      lookupKey = record => record.name,
    } = {}
  ) {
    if (!minSupportedMajorVersion || !maxSupportedMajorVersion) {
      throw new Error(
        "A minimum and maximum major version must be specified to retrieve records."
      );
    }
    try {
      await chaosMode(1 / 4);
    } catch (_error) {
      // Simulate an error by providing empty records.
      return [];
    }
    const retrievedRecords = await remoteSettingsClient.get({
      // Pull the records from the network if empty.
      syncIfEmpty: true,
      // Do not load the JSON dump if it is newer.
      //
      // The JSON dump comes from the Prod RemoteSettings channel
      // so we shouldn't ever have an issue with the Prod server
      // being older than the JSON dump itself (this is good).
      //
      // However, setting this to true will prevent us from
      // testing RemoteSettings on the Dev and Stage
      // environments if they happen to be older than the
      // most recent JSON dump from Prod.
      loadDumpIfNewer: false,
      // Don't verify the signature if the client is mocked.
      verifySignature: VERIFY_SIGNATURES_FROM_FS,
      // Apply any filters for retrieving the records.
      filters,
    });

    // Create a mapping to only the max version of each record discriminated by
    // the result of the lookupKey() function.
    const keyToRecord = new Map();

    for (const record of retrievedRecords) {
      const key = lookupKey(record);
      const existing = keyToRecord.get(key);

      if (!record.version) {
        lazy.console.error(record);
        throw new Error("Expected the record to have a version.");
      }
      if (
        TranslationsParent.isBetterRecordVersion(
          minSupportedMajorVersion,
          maxSupportedMajorVersion,
          record.version,
          existing?.version
        )
      ) {
        keyToRecord.set(key, record);
      }
    }

    return Array.from(keyToRecord.values());
  }

  /**
   * Determines if the contending record version is a better record version than the current best record version.
   *
   * For the contending version to be considered better, it must fall within the supported-version range and be
   * a larger version than the current best version (if a current best version is provided).
   *
   * @param {number} minSupportedMajorVersion - The minimum major record version that is supported in this build of Firefox.
   * @param {number} maxSupportedMajorVersion - The maximum major record version that is supported in this build of Firefox.
   * @param {string} contendingVersion - The version of the contending record that is actively being evaluated.
   * @param {string} [currentBestVersion] - The version of a previously encountered record that is currently best.
   */
  static isBetterRecordVersion(
    minSupportedMajorVersion,
    maxSupportedMajorVersion,
    contendingVersion,
    currentBestVersion
  ) {
    return (
      // Check that the contending version is within range of the minimum major version.
      Services.vc.compare(
        `${minSupportedMajorVersion}.0a`,
        contendingVersion
      ) <= 0 &&
      // Check that the contending version is within range of the maximum major version.
      Services.vc.compare(
        `${maxSupportedMajorVersion + 1}.0a`,
        contendingVersion
      ) > 0 &&
      // Check that the new record greater than the current best version.
      (!currentBestVersion ||
        Services.vc.compare(currentBestVersion, contendingVersion) < 0)
    );
  }

  /**
   * Lazily initializes the model records, and returns the cached ones if they
   * were already retrieved. The key of the returned `Map` is the record id.
   *
   * @returns {Promise<Map<string, TranslationModelRecord>>}
   */
  static async #getTranslationModelRecords() {
    if (TranslationsParent.#translationModelRecords) {
      return TranslationsParent.#translationModelRecords;
    }

    TranslationsParent.#maybeStartObservingPrefs();

    // Load the models. If no data is present, then there will be an initial sync.
    // Rely on Remote Settings for the syncing strategy for receiving updates.
    lazy.console.log(`Getting remote language models.`);
    const now = Date.now();

    const { promise, resolve } = Promise.withResolvers();
    const client = TranslationsParent.#getTranslationModelsRemoteClient();

    /** @type {TranslationModelRecord[]} */
    const maxSupportedVersionRecords =
      await TranslationsParent.getMaxSupportedVersionRecords(client, {
        minSupportedMajorVersion:
          TranslationsParent.LANGUAGE_MODEL_MAJOR_VERSION_MIN,
        maxSupportedMajorVersion:
          TranslationsParent.LANGUAGE_MODEL_MAJOR_VERSION_MAX,
        // Names in this collection are not unique, so we are appending the languagePairKey
        // to guarantee uniqueness.
        lookupKey: record =>
          `${record.name}${TranslationsParent.nonPivotKey(
            record.fromLang,
            record.toLang,
            record.variant
          )}`,
      });

    if (maxSupportedVersionRecords.length === 0) {
      throw new Error("Unable to retrieve the translation models.");
    }

    // Filter out language pairs that do not have pivot coverage.
    const pivotFilteredRecords =
      TranslationsParent.#ensureLanguagePairsHavePivots(
        maxSupportedVersionRecords
      );

    // Exclude the lexical shortlist records based on the pref configuration.
    const lexFilteredRecords = lazy.useLexicalShortlist
      ? pivotFilteredRecords
      : pivotFilteredRecords.filter(r => r.fileType !== "lex");

    // For each language-pair key, find the version of the "model" file-type record
    // and discard records that do not match that version exactly.
    const versionFilteredRecords =
      TranslationsParent.#filterByModelVersion(lexFilteredRecords);

    // Build a final mapping of id to record.
    const records = new Map();
    for (const record of versionFilteredRecords) {
      records.set(record.id, record);
    }

    const duration = (Date.now() - now) / 1000;
    lazy.console.log(
      `Remote language models loaded in ${duration} seconds.`,
      records
    );

    resolve(records);

    TranslationsParent.#translationModelRecords = promise.catch(() => {
      TranslationsParent.#invalidateTranslationModelRecords();
    });

    return TranslationsParent.#translationModelRecords;
  }

  /**
   * This implementation assumes that every language pair has access to the
   * pivot language. If any languages are added without a pivot language, or the
   * pivot language is changed, then this implementation will need a more complicated
   * language solver. This means that any UI pickers would need to be updated, and
   * the pivot language selection would need a solver.
   *
   * @param {TranslationModelRecord[] | LanguagePair[]} records
   */
  static #ensureLanguagePairsHavePivots(records) {
    if (!AppConstants.DEBUG) {
      // Only run this check on debug builds as it's in the performance critical first
      // page load path.
      return records;
    }
    // lang -> pivot
    const hasToPivot = new Set();
    // pivot -> en
    const hasFromPivot = new Set();

    const fromLangs = new Set();
    const toLangs = new Set();

    for (const { fromLang, toLang } of records) {
      fromLangs.add(fromLang);
      toLangs.add(toLang);

      if (toLang === PIVOT_LANGUAGE) {
        // lang -> pivot
        hasToPivot.add(fromLang);
      }
      if (fromLang === PIVOT_LANGUAGE) {
        // pivot -> en
        hasFromPivot.add(toLang);
      }
    }

    const fromLangsToRemove = new Set();
    const toLangsToRemove = new Set();

    for (const lang of fromLangs) {
      if (lang === PIVOT_LANGUAGE) {
        continue;
      }
      // Check for "lang -> pivot"
      if (!hasToPivot.has(lang)) {
        TranslationsParent.reportError(
          new Error(
            `The "from" language model "${lang}" is being discarded as it doesn't have a pivot language.`
          )
        );
        fromLangsToRemove.add(lang);
      }
    }

    for (const lang of toLangs) {
      if (lang === PIVOT_LANGUAGE) {
        continue;
      }
      // Check for "pivot -> lang"
      if (!hasFromPivot.has(lang)) {
        TranslationsParent.reportError(
          new Error(
            `The "to" language model "${lang}" is being discarded as it doesn't have a pivot language.`
          )
        );
        toLangsToRemove.add(lang);
      }
    }

    const after = records.filter(record => {
      if (fromLangsToRemove.has(record.fromLang)) {
        return false;
      }
      if (toLangsToRemove.has(record.toLang)) {
        return false;
      }
      return true;
    });
    return after;
  }

  /**
   * Finds the version of the "model" file-type record for each language-pair key
   * and retains only records that match that version exactly.
   *
   * Even though we retrieve our records via getMaxSupportedVersionRecords(), it is
   * possible that the maximum version for each record type is not the same. For example,
   * if we upgraded a model from a shared-vocab configuration to a split-vocab configuration,
   * then we might have a leftover shared "vocab" file of version `N.M`, while the rest of the
   * newly updated files for that language pair are all at version `N.M+1`.
   *
   * In such a case, we want to ignore the file from the older version, since it is not
   * intended to be utilized in the current config. The version of the "model" file-type
   * record is guaranteed to be the exact intended version for the current configuration.
   *
   * @param {TranslationModelRecord[]} records
   * @returns {TranslationModelRecord[]} The records after filtering.
   */
  static #filterByModelVersion(records) {
    const recordGroups = new Map();
    for (const record of records) {
      const key = TranslationsParent.nonPivotKey(
        record.fromLang,
        record.toLang,
        record.variant
      );

      let recordGroup = recordGroups.get(key);
      if (!recordGroup) {
        recordGroup = [];
        recordGroups.set(key, recordGroup);
      }

      recordGroup.push(record);
    }

    const filteredRecords = [];
    for (const [key, groupedRecords] of recordGroups) {
      const modelRecordVersion = groupedRecords.find(
        ({ fileType }) => fileType === "model"
      )?.version;

      if (!modelRecordVersion) {
        throw new Error(`No model file found for "${key}".`);
      }

      for (const record of groupedRecords) {
        if (record.version === modelRecordVersion) {
          filteredRecords.push(record);
        }
      }
    }

    return filteredRecords;
  }

  /**
   * Lazily initializes the RemoteSettingsClient for the downloaded wasm binary data.
   *
   * @returns {RemoteSettingsClient}
   */
  static #getTranslationsWasmRemoteClient() {
    if (TranslationsParent.#translationsWasmRemoteClient) {
      return TranslationsParent.#translationsWasmRemoteClient;
    }

    /** @type {RemoteSettingsClient} */
    const client = lazy.RemoteSettings("translations-wasm");
    TranslationsParent.#translationsWasmRemoteClient = client;
    client.on("sync", TranslationsParent.#handleTranslationsWasmSync);

    return client;
  }

  /** @type {Promise<WasmRecord> | null} */
  static #bergamotWasmRecord = null;

  /** @type {boolean} */
  static #lookForLocalWasmBuild = true;

  /**
   * This is used to load a local copy of the Bergamot translations engine, if it exists.
   * From a local build of Firefox:
   *
   * 1. Run the python script:
   *   ./toolkit/components/translations/bergamot-translator/build-bergamot.py --debug
   *
   * 2. Uncomment the .wasm file in: toolkit/components/translations/jar.mn
   * 3. Run: ./mach build
   * 4. Run: ./mach run
   */
  static async #maybeFetchLocalBergamotWasmArrayBuffer() {
    if (TranslationsParent.#lookForLocalWasmBuild) {
      // Attempt to get a local copy of the translator. Most likely this will be a 404.
      try {
        const response = await fetch(
          "chrome://global/content/translations/bergamot-translator.wasm"
        );
        const arrayBuffer = response.arrayBuffer();
        lazy.console.log(`Using a local copy of Bergamot.`);
        return arrayBuffer;
      } catch {
        // Only attempt to fetch once, if it fails don't try again.
        TranslationsParent.#lookForLocalWasmBuild = false;
      }
    }
    return null;
  }

  /**
   * Bergamot is the translation engine that has been compiled to wasm. It is shipped
   * to the user via Remote Settings.
   *
   * https://github.com/mozilla/bergamot-translator/
   */
  /**
   * @returns {Promise<ArrayBuffer>}
   */
  static async #getBergamotWasmArrayBuffer() {
    const start = Date.now();
    const client = TranslationsParent.#getTranslationsWasmRemoteClient();

    const localCopy =
      await TranslationsParent.#maybeFetchLocalBergamotWasmArrayBuffer();
    if (localCopy) {
      return localCopy;
    }

    if (!TranslationsParent.#bergamotWasmRecord) {
      // Place the records into a promise to prevent any races.
      TranslationsParent.#bergamotWasmRecord = (async () => {
        // Load the wasm binary from remote settings, if it hasn't been already.
        lazy.console.log(`Getting remote bergamot-translator wasm records.`);

        /** @type {WasmRecord[]} */
        const wasmRecords =
          await TranslationsParent.getMaxSupportedVersionRecords(client, {
            filters: { name: "bergamot-translator" },
            minSupportedMajorVersion: TranslationsParent.BERGAMOT_MAJOR_VERSION,
            maxSupportedMajorVersion: TranslationsParent.BERGAMOT_MAJOR_VERSION,
          });

        if (wasmRecords.length === 0) {
          // The remote settings client provides an empty list of records when there is
          // an error.
          throw new Error(
            "Unable to get the bergamot translator from Remote Settings."
          );
        }

        if (wasmRecords.length > 1) {
          TranslationsParent.reportError(
            new Error(
              "Expected the bergamot-translator to only have 1 record."
            ),
            wasmRecords
          );
        }
        const [record] = wasmRecords;
        lazy.console.log(
          `Using ${record.name}@${record.release} release version ${record.version} first released on Fx${record.fx_release}`,
          record
        );
        return record;
      })();
    }
    // Unlike the models, greedily download the wasm. It will pull it from a locale
    // cache on disk if it's already been downloaded. Do not retain a copy, as
    // this will be running in the parent process. It's not worth holding onto
    // this much memory, so reload it every time it is needed.

    try {
      await chaosModeError(1 / 3);

      /** @type {{buffer: ArrayBuffer}} */
      const { buffer } = await client.attachments.download(
        await TranslationsParent.#bergamotWasmRecord
      );

      const duration = Date.now() - start;
      lazy.console.log(
        `"bergamot-translator" wasm binary loaded in ${duration / 1000} seconds`
      );

      return buffer;
    } catch (error) {
      TranslationsParent.#bergamotWasmRecord = null;
      throw error;
    }
  }

  /**
   * Deletes language files that match a language.
   * Note, this call doesn't have directionality because it is checking and deleting files
   * for both sides of the pair that are not involved in a pivot.
   *
   * @param {string} languageA The BCP 47 language tag.
   * @param {string} languageB The BCP 47 language tag.
   * @param {boolean} deletePivots When true, the request may delete files that could be used for another language's pivot to complete a translation.
   *                               When false, the request will not delete files that could be used in another language's pivot.
   */
  static async deleteLanguageFilesToAndFromPair(
    languageA,
    languageB,
    deletePivots
  ) {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();
    return Promise.all(
      Array.from(
        await TranslationsParent.getRecordsForTranslatingToAndFromPair(
          languageA,
          languageB,
          deletePivots
        )
      ).map(record => {
        lazy.console.log("Deleting record", record);
        return client.attachments.deleteDownloaded(record);
      })
    );
  }

  /**
   * Deletes language files that match a language.
   * This function operates based on the current app language.
   *
   * @param {string} language The BCP 47 language tag.
   */
  static async deleteLanguageFiles(language) {
    return TranslationsParent.deleteLanguageFilesToAndFromPair(
      language,
      Services.locale.appLocaleAsBCP47,
      /* deletePivots */ false
    );
  }

  /**
   * Download language files that match a language.
   *
   * @param {string} language The BCP 47 language tag.
   */
  static async downloadLanguageFiles(language) {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();

    const queue = [];

    for (const record of await TranslationsParent.getRecordsForTranslatingToAndFromAppLanguage(
      language,
      /* includePivotRecords */ true
    )) {
      const download = () => {
        lazy.console.log("Downloading record", record.name, record.id);
        return client.attachments.download(record);
      };
      queue.push({ download });
    }

    return downloadManager(queue);
  }

  /**
   * Download all files used for translations.
   */
  static async downloadAllFiles() {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();

    const queue = [];

    for (const record of (
      await TranslationsParent.#getTranslationModelRecords()
    ).values()) {
      queue.push({
        // The download may be attempted multiple times.
        onFailure: () => {
          console.error("Failed to download", record.name);
        },
        download: () => client.attachments.download(record),
      });
    }

    queue.push({
      download: () => TranslationsParent.#getBergamotWasmArrayBuffer(),
    });

    return downloadManager(queue);
  }

  /**
   * Delete all language model files.
   *
   * @returns {Promise<string[]>} A list of record IDs.
   */
  static async deleteAllLanguageFiles() {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();
    await chaosMode();
    await client.attachments.deleteAll();
    return [...(await TranslationsParent.#getTranslationModelRecords()).keys()];
  }

  /**
   * Delete all language model files not a part of a complete language package. Also known as
   * the language model "cache" in the UI.
   *
   * Usage is to clean up language models that may be lingering in the file system and are not
   * a part of a downloaded language model package.
   *
   * For example, this deletes models that were acquired via a translation on-the-fly, not
   * the complete package of language models for a language that has both directions.
   *
   * A complete language package for this function is considered both directions, when available,
   * for example, en->es (downloaded) and es->en (downloaded) is complete and nothing will be deleted.
   *
   * When the language is not symmetric, for example nn->en (downloaded), then this is also considered a
   * complete package and not subject to deletion. (Note, in this example, en->nn is not available.)
   *
   * This will delete a downloaded model set when it is incomplete, for example en->es (downloaded) and es->en
   * (not-downloaded) will delete en->es to clear the lingering one-sided package.
   *
   * @returns {Set<string>}  Directional language pairs in the form of "sourceLanguage,targetLanguage" that indicates language pairs that were deleted.
   */
  static async deleteCachedLanguageFiles() {
    const languagePairs = await TranslationsParent.getNonPivotLanguagePairs();

    const deletionRequest = [];
    let deletedPairs = new Set();

    for (const { sourceLanguage, targetLanguage } of languagePairs) {
      const { downloadedPairs, nonDownloadedPairs } =
        await TranslationsParent.getDownloadedFileStatusToAndFromPair(
          sourceLanguage,
          targetLanguage
        );

      if (downloadedPairs.size && nonDownloadedPairs.size) {
        // It is possible that additional pairs are listed, but in general,
        // this should be parallel with deletion requests.
        downloadedPairs.forEach(langPair => deletedPairs.add(langPair));
        deletionRequest.push(
          TranslationsParent.deleteLanguageFilesToAndFromPair(
            sourceLanguage,
            targetLanguage,
            /* deletePivots */ false
          )
        );
      }
    }
    await Promise.all(deletionRequest);

    return deletedPairs;
  }

  /**
   * Contains information about what files are downloaded between a language pair.
   * Note, this call doesn't have directionality because it is checking both sides of the pair.
   *
   * @param {string} languageA The BCP 47 language tag.
   * @param {string} languageB The BCP 47 language tag.
   *
   * @returns {object} status The status between the pairs.
   * @returns {Set<string>} status.downloadedPairs A set of strings that has directionality about what side
   *                                                is downloaded, in the format "sourceLanguage,targetLanguage".
   * @returns {Set<string>} status.nonDownloadedPairs A set of strings that has directionality about what side
   *                                                   is not downloaded, in the format "sourceLanguage,targetLanguage". It is possible to have files both in nonDownloadedFiles
   *                                                   and downloadedFiles in the case of incomplete downloads.
   */

  static async getDownloadedFileStatusToAndFromPair(languageA, languageB) {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();
    let downloadedPairs = new Set();
    let nonDownloadedPairs = new Set();

    for (const record of await TranslationsParent.getRecordsForTranslatingToAndFromPair(
      languageA,
      languageB,
      /* includePivotRecords */ true
    )) {
      let isDownloaded = false;
      if (TranslationsParent.isInAutomation()) {
        isDownloaded = record.attachment.isDownloaded;
      } else {
        isDownloaded = await client.attachments.isDownloaded(record);
      }

      if (isDownloaded) {
        downloadedPairs.add(
          TranslationsParent.nonPivotKey(
            record.fromLang,
            record.toLang,
            record.variant
          )
        );
      } else {
        nonDownloadedPairs.add(
          TranslationsParent.nonPivotKey(
            record.fromLang,
            record.toLang,
            record.variant
          )
        );
      }
    }

    return { downloadedPairs, nonDownloadedPairs };
  }

  /**
   * Only returns true if all language files are present for a requested language.
   * It's possible only half the files exist for a pivot translation into another
   * language, or there was a download error, and we're still missing some files.
   *
   * @param {string} requestedLanguage The BCP 47 language tag.
   */
  static async hasAllFilesForLanguage(requestedLanguage) {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();
    for (const record of await TranslationsParent.getRecordsForTranslatingToAndFromAppLanguage(
      requestedLanguage,
      /* includePivotRecords */ true
    )) {
      if (!(await client.attachments.isDownloaded(record))) {
        return false;
      }
    }

    return true;
  }

  /**
   * Get the necessary files for translating between two given languages.
   * This may require the files for a pivot language translation
   * if there is no language model for a direct translation.
   * Note, this call doesn't have directionality because it is checking both sides of the pair.
   *
   * @param {string} languageA The BCP 47 language tag.
   * @param {string} languageB The BCP 47 language tag.
   * @param {boolean} includePivotRecords - When true, this will include a list of records with any required pivots.
   *                                        An example using true would be to determine which files to download to complete a translation.
   *                                        When false, this will not include the list of pivot records to achieve a translations.
   *                                        An example using false would be to determine which  records to delete, but wanting to be
   *                                        cautions to avoid deleting model files used by another language.
   * @returns {Set<TranslationModelRecord>}
   */
  static async getRecordsForTranslatingToAndFromPair(
    languageA,
    languageB,
    includePivotRecords
  ) {
    const records = await TranslationsParent.#getTranslationModelRecords();

    let matchedRecords = new Set();

    if (lazy.TranslationsUtils.langTagsMatch(languageA, languageB)) {
      // There are no records if the requested language and app language are the same.
      return matchedRecords;
    }

    const addLanguagePair = (sourceLanguage, targetLanguage) => {
      let matchFound = false;
      for (const record of records.values()) {
        if (
          lazy.TranslationsUtils.langTagsMatch(
            record.fromLang,
            sourceLanguage
          ) &&
          lazy.TranslationsUtils.langTagsMatch(record.toLang, targetLanguage)
        ) {
          matchedRecords.add(record);
          matchFound = true;
        }
      }
      return matchFound;
    };

    if (
      // Is there a direct translation?
      !addLanguagePair(languageA, languageB)
    ) {
      // This is no direct translation, get the pivot files.
      addLanguagePair(languageA, PIVOT_LANGUAGE);
      // These files may be required for other pivot translations, so don't list
      // them if we are deleting records.
      if (includePivotRecords) {
        addLanguagePair(PIVOT_LANGUAGE, languageB);
      }
    }

    if (
      // Is there a direct translation?
      !addLanguagePair(languageB, languageA)
    ) {
      // This is no direct translation, get the pivot files.
      addLanguagePair(PIVOT_LANGUAGE, languageA);
      // These files may be required for other pivot translations, so don't list
      // them if we are deleting records.
      if (includePivotRecords) {
        addLanguagePair(languageB, PIVOT_LANGUAGE);
      }
    }

    return matchedRecords;
  }

  /**
   * Get the necessary files for translating to and from the app language and a
   * requested language. This may require the files for a pivot language translation
   * if there is no language model for a direct translation.
   *
   * @param {string} requestedLanguage The BCP 47 language tag.
   * @param {boolean} includePivotRecords - When true, this will include a list of records with any required pivots.
   *                                        An example using true would be to determine which files to download to complete a translation.
   *                                        When false, this will not include the list of pivot records to achieve a translations.
   *                                        An example using false would be to determine which  records to delete, but wanting to be
   *                                        cautions to avoid deleting model files used by another language.
   * @returns {Set<TranslationModelRecord>}
   */
  static async getRecordsForTranslatingToAndFromAppLanguage(
    requestedLanguage,
    includePivotRecords
  ) {
    return TranslationsParent.getRecordsForTranslatingToAndFromPair(
      requestedLanguage,
      Services.locale.appLocaleAsBCP47,
      includePivotRecords
    );
  }

  /**
   * Gets the language model files in an array buffer by downloading attachments from
   * Remote Settings, or retrieving them from the local cache. Each translation
   * requires multiple files.
   *
   * Results are only returned if the model is found.
   *
   * @param {string} sourceLanguage
   * @param {string} targetLanguage
   * @param {string} [variant]
   * @returns {TranslationModelPayload}
   */
  static async getTranslationModelPayload(
    sourceLanguage,
    targetLanguage,
    variant
  ) {
    if (!sourceLanguage || !targetLanguage) {
      console.error({ sourceLanguage, targetLanguage });
      throw new Error("A source or target language was not provided.");
    }
    const client = TranslationsParent.#getTranslationModelsRemoteClient();

    lazy.console.log(
      `Beginning model downloads: "${sourceLanguage}" to "${targetLanguage}"`
    );

    const records = [
      ...(await TranslationsParent.#getTranslationModelRecords()).values(),
    ];

    /** @type {LanguageTranslationModelFiles} */
    const languageModelFiles = {};

    // Use Promise.all to download (or retrieve from cache) the model files in parallel.
    await Promise.all(
      records.map(async record => {
        if (record.fileType === "qualityModel") {
          // Do not include the quality models. We do not use them.
          return;
        }

        if (
          !lazy.TranslationsUtils.langTagsMatch(
            record.fromLang,
            sourceLanguage
          ) ||
          !lazy.TranslationsUtils.langTagsMatch(
            record.toLang,
            targetLanguage
          ) ||
          record.variant !== variant
        ) {
          // Only use models that match.
          return;
        }

        const start = Date.now();

        // Download or retrieve from the local cache:

        await chaosMode(1 / 3);

        /** @type {{buffer: ArrayBuffer }} */
        const { buffer } = await client.attachments.download(record);

        languageModelFiles[record.fileType] = {
          buffer,
          record,
        };

        const duration = Date.now() - start;
        lazy.console.log(
          `Translation model fetched in ${duration / 1000} seconds:`,
          record.fromLang,
          record.toLang,
          record.variant,
          record.fileType,
          record.version
        );
      })
    );

    // Validate that all of the files we expected were actually available and
    // downloaded.

    if (!languageModelFiles.model) {
      throw new Error(
        `No model file was found for "${sourceLanguage}" to "${targetLanguage}."`
      );
    }

    if (!languageModelFiles.lex && lazy.useLexicalShortlist) {
      throw new Error(
        `No lex file was found for "${sourceLanguage}" to "${targetLanguage}."`
      );
    }

    if (languageModelFiles.vocab) {
      if (languageModelFiles.srcvocab) {
        throw new Error(
          `A srcvocab and vocab file were both included for "${sourceLanguage}" to "${targetLanguage}." Only one is needed.`
        );
      }
      if (languageModelFiles.trgvocab) {
        throw new Error(
          `A trgvocab and vocab file were both included for "${sourceLanguage}" to "${targetLanguage}." Only one is needed.`
        );
      }
    } else if (!languageModelFiles.srcvocab || !languageModelFiles.trgvocab) {
      throw new Error(
        `No vocab files were provided for "${sourceLanguage}" to "${targetLanguage}."`
      );
    }

    /** @type {TranslationModelPayload} */
    return {
      sourceLanguage,
      targetLanguage,
      variant,
      languageModelFiles,
    };
  }

  static async getLanguageSize(language) {
    const records = [
      ...(await TranslationsParent.#getTranslationModelRecords()).values(),
    ];

    let downloadSize = 0;
    await Promise.all(
      records.map(async record => {
        if (
          !lazy.TranslationsUtils.langTagsMatch(record.fromLang, language) &&
          !lazy.TranslationsUtils.langTagsMatch(record.toLang, language)
        ) {
          return;
        }
        downloadSize += parseInt(record.attachment.size);
      })
    );
    return downloadSize;
  }

  /**
   * Gets the expected download size that will occur (if any) if translate is called on two given languages for display purposes.
   *
   * @param {string} sourceLanguage
   * @param {string} targetLanguage
   * @returns {Promise<long>} Size in bytes of the expected download. A result of 0 indicates no download is expected for the request.
   */
  static async getExpectedTranslationDownloadSize(
    sourceLanguage,
    targetLanguage
  ) {
    const directSize = await this.#getModelDownloadSize(
      sourceLanguage,
      targetLanguage
    );

    // If a direct model is not found, then check pivots.
    if (directSize.downloadSize == 0 && !directSize.modelFound) {
      const indirectFrom = await TranslationsParent.#getModelDownloadSize(
        sourceLanguage,
        PIVOT_LANGUAGE
      );

      const indirectTo = await TranslationsParent.#getModelDownloadSize(
        PIVOT_LANGUAGE,
        targetLanguage
      );

      // Note, will also return 0 due to the models not being available as well.
      return (
        parseInt(indirectFrom.downloadSize) + parseInt(indirectTo.downloadSize)
      );
    }
    return directSize.downloadSize;
  }

  /**
   * Determines the language model download size for a specified translation for display purposes.
   *
   * @param {string} sourceLanguage
   * @param {string} targetLanguage
   * @returns {Promise<{downloadSize: long, modelFound: boolean}>} Download size is the
   *   size in bytes of the estimated download for display purposes. Model found indicates
   *   a model was found. e.g., a result of {size: 0, modelFound: false} indicates no
   *   bytes to download, because a model wasn't located.
   */
  static async #getModelDownloadSize(sourceLanguage, targetLanguage) {
    const client = TranslationsParent.#getTranslationModelsRemoteClient();
    const records = [
      ...(await TranslationsParent.#getTranslationModelRecords()).values(),
    ];

    let downloadSize = 0;
    let modelFound = false;

    await Promise.all(
      records.map(async record => {
        if (record.fileType === "qualityModel") {
          // Do not include the quality models. We do not use them.
          return;
        }

        if (record.fileType === "lex" && !lazy.useLexicalShortlist) {
          // The current configuration does not use lexical shortlists.
          return;
        }

        if (
          !lazy.TranslationsUtils.langTagsMatch(
            record.fromLang,
            sourceLanguage
          ) ||
          !lazy.TranslationsUtils.langTagsMatch(record.toLang, targetLanguage)
        ) {
          return;
        }

        modelFound = true;
        const isDownloaded = await client.attachments.isDownloaded(record);
        if (!isDownloaded) {
          downloadSize += parseInt(record.attachment.size);
        }
      })
    );
    return { downloadSize, modelFound };
  }

  /**
   * Applies testing mocks to the TranslationsParent class.
   *
   * @param {object} options
   * @param {boolean} [options.useMockedTranslator=true] - Whether to use a mocked translator.
   * @param {RemoteSettingsClient} options.translationModelsRemoteClient - The remote client for translation models.
   * @param {RemoteSettingsClient} options.translationsWasmRemoteClient - The remote client for translations WASM.
   */
  static applyTestingMocks({
    useMockedTranslator = true,
    translationModelsRemoteClient,
    translationsWasmRemoteClient,
  }) {
    lazy.console.log("Mocking RemoteSettings for the translations engine.");
    TranslationsParent.#translationModelsRemoteClient =
      translationModelsRemoteClient;
    TranslationsParent.#translationsWasmRemoteClient =
      translationsWasmRemoteClient;
    TranslationsParent.#isTranslationsEngineMocked = useMockedTranslator;

    translationModelsRemoteClient.on(
      "sync",
      TranslationsParent.#handleTranslationsModelsSync
    );
    translationsWasmRemoteClient.on(
      "sync",
      TranslationsParent.#handleTranslationsWasmSync
    );
  }

  /**
   * Most values are cached for performance, in tests we want to be able to clear them.
   */
  static clearCache() {
    // Records.
    TranslationsParent.#bergamotWasmRecord = null;
    TranslationsParent.#invalidateTranslationModelRecords();

    // Clients.
    TranslationsParent.#translationModelsRemoteClient = null;
    TranslationsParent.#translationsWasmRemoteClient = null;

    // Derived data.
    TranslationsParent.#invalidateLanguagePairs();
    TranslationsParent.#mostRecentTargetLanguages = null;
    TranslationsParent.#userSettingsLanguages = null;
    TranslationsParent.#preferredLanguages = null;
    TranslationsParent.#isTranslationsEngineSupported = null;
  }

  /**
   * Remove the mocks for the translations engine, make sure and call clearCache after
   * to remove the cached values.
   */
  static removeTestingMocks() {
    lazy.console.log(
      "Removing RemoteSettings mock for the translations engine."
    );
    TranslationsParent.#translationModelsRemoteClient.off(
      "sync",
      TranslationsParent.#handleTranslationsModelsSync
    );
    TranslationsParent.#translationsWasmRemoteClient.off(
      "sync",
      TranslationsParent.#handleTranslationsWasmSync
    );

    TranslationsParent.#isTranslationsEngineMocked = false;
  }

  /**
   * Report an error. Having this as a method allows tests to check that an error
   * was properly reported.
   *
   * @param {Error} error - Providing an Error object makes sure the stack is properly
   *                        reported.
   * @param {any[]} args - Any args to pass on to console.error.
   */
  static reportError(error, ...args) {
    lazy.console.log(error, ...args);
  }

  /**
   * @param {LanguagePair} languagePair
   * @param {boolean} reportAsAutoTranslate - In telemetry, report this as
   *   an auto-translate.
   */
  async translate(languagePair, reportAsAutoTranslate) {
    const { sourceLanguage, targetLanguage } = languagePair;
    if (!sourceLanguage || !targetLanguage) {
      lazy.console.error(
        new Error(
          "A translation was requested but the sourceLanguage or targetLanguage was not set."
        ),
        { sourceLanguage, targetLanguage, reportAsAutoTranslate }
      );
      return;
    }
    if (lazy.TranslationsUtils.langTagsMatch(sourceLanguage, targetLanguage)) {
      lazy.console.error(
        new Error(
          "A translation was requested where the source and target languages match."
        ),
        { sourceLanguage, targetLanguage, reportAsAutoTranslate }
      );
      return;
    }
    if (this.languageState.requestedLanguagePair) {
      // This page has already been translated, restore it and translate it
      // again once the actor has been recreated.
      const windowState = this.getWindowState();
      windowState.translateOnPageReload = languagePair;
      this.restorePage(sourceLanguage);
    } else {
      const { docLangTag } = this.languageState.detectedLanguages;

      if (!this.innerWindowId) {
        throw new Error(
          "The innerWindowId for the TranslationsParent was not available."
        );
      }

      // The MessageChannel will be used for communicating directly between the content
      // process and the engine's process.
      const port = await TranslationsParent.requestTranslationsPort(
        languagePair,
        this
      );

      if (!port) {
        lazy.console.error(
          `Failed to create a translations port for language pair: (${lazy.TranslationsUtils.serializeLanguagePair(languagePair)})`
        );
        return;
      }

      this.languageState.requestedLanguagePair = languagePair;

      const preferredLanguages = TranslationsParent.getPreferredLanguages();
      const topPreferredLanguage =
        preferredLanguages && preferredLanguages.length
          ? preferredLanguages[0]
          : null;

      TranslationsParent.telemetry().onTranslate({
        docLangTag,
        sourceLanguage,
        targetLanguage,
        topPreferredLanguage,
        autoTranslate: reportAsAutoTranslate,
        requestTarget: "full_page",
      });

      TranslationsParent.storeMostRecentTargetLanguage(targetLanguage);

      this.sendAsyncMessage(
        "Translations:TranslatePage",
        {
          languagePair,
          port,
        },
        // https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Transferable_objects
        // Mark the MessageChannel port as transferable.
        [port]
      );
    }
  }

  /**
   * Restore the page to the original language by doing a hard reload.
   */
  restorePage() {
    TranslationsParent.telemetry().onRestorePage();
    // Skip auto-translate for one page load.
    const windowState = this.getWindowState();
    windowState.isPageRestored = true;
    this.languageState.hasVisibleChange = false;
    this.languageState.requestedLanguagePair = null;
    windowState.previousDetectedLanguages =
      this.languageState.detectedLanguages;

    const browser = this.browsingContext.embedderElement;
    browser.reload();
  }

  static onLocationChange(browser) {
    if (!lazy.translationsEnabledPref) {
      // The pref isn't enabled, so don't attempt to get the actor.
      return;
    }
    let actor;
    try {
      actor =
        browser.browsingContext.currentWindowGlobal.getActor("Translations");
    } catch {
      // The actor may not be supported on this page, which throws an error.
    }
    actor?.languageState.locationChanged();
  }

  /**
   * @returns {Promise<DetectionResult>}
   */
  async queryIdentifyLanguage() {
    if (
      TranslationsParent.isInAutomation() &&
      !TranslationsParent.#isTranslationsEngineMocked
    ) {
      // In automation assume English is the language, but don't be confident.
      return { confident: false, language: "en", languages: [] };
    }
    return this.sendQuery("Translations:IdentifyLanguage").catch(error => {
      if (this.#isDestroyed) {
        // The actor was destroyed while this message was still being resolved.
        return null;
      }
      return Promise.reject(error);
    });
  }

  /**
   * Returns the language from the document element.
   *
   * @returns {Promise<string>}
   */
  queryDocumentElementLang() {
    return this.sendQuery("Translations:GetDocumentElementLang");
  }

  /**
   *
   * Keep this table up to date with:
   * browser/components/translations/tests/browser/browser_translations_full_page_language_id_behavior.js
   *
   * ┌──────────┬───────────┬───────────┬─────────────────────────────┐
   * │ Has HTML │ Detection │ Detection │ Outcome                     │
   * │ Tag      │ Agrees    │ Confident │                             │
   * ├──────────┼───────────┼───────────┼─────────────────────────────┤
   * │ TRUE     │ TRUE      │ TRUE      │ Auto Translate Matching Tag │
   * │ TRUE     │ TRUE      │ FALSE     │ Auto Translate Matching Tag │
   * │ TRUE     │ FALSE     │ TRUE      │ Show Button Only            │
   * │ TRUE     │ FALSE     │ FALSE     │ Show Button Only            │
   * │ FALSE    │ N/A       │ TRUE      │ Auto Translate Detected Tag │
   * │ FALSE    │ N/A       │ FALSE     │ Show Button Only            │
   * └──────────┴───────────┴───────────┴─────────────────────────────┘
   *
   * @param {LangTags} langTags
   */
  async shouldAutoTranslate(langTags) {
    if (
      langTags.docLangTag &&
      langTags.userLangTag &&
      langTags.isDocLangTagSupported &&
      this.#maybeAutoTranslate(langTags) &&
      !TranslationsParent.shouldNeverTranslateLanguage(langTags.docLangTag) &&
      !this.shouldNeverTranslateSite()
    ) {
      // Do a final check that the identified language matches the reported language
      // tag to ensure that the page isn't reporting the incorrect languages. This
      // check is deferred to now for performance considerations.
      const detectionResult = await this.queryIdentifyLanguage();
      langTags.docLangTag = detectionResult.language;
      langTags.identifiedLangTag = detectionResult.language;
      langTags.identifiedLangConfident = detectionResult.confident;

      if (langTags.identifiedLangTag === langTags.htmlLangAttribute) {
        return true;
      }

      // Since there is a mismatch of the html lang attribute and the identified language,
      // perform another check with the updated language.
      return (
        TranslationsParent.shouldAlwaysTranslateLanguage(langTags) &&
        !TranslationsParent.shouldNeverTranslateLanguage(langTags.docLangTag)
      );
    }

    return false;
  }

  /**
   * Finds a compatible source language tag for translation synchronously.
   * Searches the provided language pairs for a match based on the given language tag.
   *
   * @param {string} langTag - A BCP-47 language tag to match against source languages.
   * @param {Array<{ sourceLanguage: string, targetLanguage: string }>} languagePairs - An array of language pair objects,
   *   where each object contains `sourceLanguage` and `targetLanguage` properties.
   * @returns {string | null} - The compatible source language tag, or `null` if no match is found.
   */
  static findCompatibleSourceLangTagSync(langTag, languagePairs) {
    if (!langTag) {
      return null;
    }

    const langPair = languagePairs.find(({ sourceLanguage }) =>
      lazy.TranslationsUtils.langTagsMatch(sourceLanguage, langTag)
    );

    return langPair?.sourceLanguage;
  }

  /**
   * Finds a compatible source language tag for translation.
   * Fetches language pairs and then determines a match for the given language tag.
   *
   * @param {string} langTag - A BCP-47 language tag to match against source languages.
   * @returns {Promise<string | null>} - A promise resolving to the compatible source language tag,
   *   or `null` if no match is found.
   */
  static async findCompatibleSourceLangTag(langTag) {
    const languagePairs = await TranslationsParent.getNonPivotLanguagePairs();
    return TranslationsParent.findCompatibleSourceLangTagSync(
      langTag,
      languagePairs
    );
  }

  /**
   * Finds a compatible target language tag for translation synchronously.
   * Searches the provided language pairs for a match based on the given language tag.
   *
   * @param {string} langTag - A BCP-47 language tag to match against target languages.
   * @param {Array<{ sourceLanguage: string, targetLanguage: string }>} languagePairs - An array of language pair objects,
   *   where each object contains `sourceLanguage` and `targetLanguage` properties.
   * @returns {string | null} - The compatible target language tag, or `null` if no match is found.
   */
  static findCompatibleTargetLangTagSync(langTag, languagePairs) {
    if (!langTag) {
      return null;
    }

    const langPair = languagePairs.find(({ targetLanguage }) =>
      lazy.TranslationsUtils.langTagsMatch(targetLanguage, langTag)
    );

    return langPair?.targetLanguage;
  }

  /**
   * Finds a compatible target language tag for translation.
   * Fetches language pairs and then determines a match for the given language tag.
   *
   * @param {string} langTag - A BCP-47 language tag to match against target languages.
   * @returns {Promise<string | null>} - A promise resolving to the compatible target language tag,
   *   or `null` if no match is found.
   */
  static async findCompatibleTargetLangTag(langTag) {
    const languagePairs = await TranslationsParent.getNonPivotLanguagePairs();
    return TranslationsParent.findCompatibleTargetLangTagSync(
      langTag,
      languagePairs
    );
  }

  /**
   * Retrieves the top preferred user language for which translation
   * is supported when translating to that language.
   *
   * @param {object} options
   * @param {string[]} [options.excludeLangTags] - BCP-47 language tags to intentionally exclude.
   */
  static async getTopPreferredSupportedToLang({ excludeLangTags } = {}) {
    const preferredLanguages = TranslationsParent.getPreferredLanguages({
      excludeLangTags,
    });

    const languagePairs = await TranslationsParent.getNonPivotLanguagePairs();
    for (const langTag of preferredLanguages) {
      const compatibleLangTag =
        TranslationsParent.findCompatibleTargetLangTagSync(
          langTag,
          languagePairs
        );
      if (compatibleLangTag) {
        return compatibleLangTag;
      }
    }

    return PIVOT_LANGUAGE;
  }

  /**
   * Attempts to make the language tag more specific if it is a supported macro language tag.
   * If no special cases apply, the provided language tag is returned as-is.
   *
   * @param {string} langTag - A BCP-47 language tag to evaluate and possibly refine.
   * @returns {Promise<string>} - The refined language tag, or null if processing was interrupted.
   */
  maybeRefineMacroLanguageTag(langTag) {
    if (langTag === "no") {
      // Choose "Norwegian Bokmål" over "Norwegian Nynorsk" as it is more widely used.
      //
      // https://en.wikipedia.org/wiki/Norwegian_language#Bokm%C3%A5l_and_Nynorsk
      //
      //   > A 2005 poll indicates that 86.3% use primarily Bokmål as their daily
      //   > written language, 5.5% use both Bokmål and Nynorsk, and 7.5% use
      //   > primarily Nynorsk.
      return "nb";
    }

    // No special cases were handled above, so pass the langTag through.
    return langTag;
  }

  /**
   * Returns the lang tags that should be offered for translation. This is in the parent
   * rather than the child to remove the per-content process memory allocation amount.
   *
   * @param {string} [htmlLangAttribute]
   * @param {string} [href]
   * @returns {Promise<LangTags | null>} - Returns null if the actor was destroyed before
   *   the result could be resolved.
   */
  async getDetectedLanguages(htmlLangAttribute, href) {
    if (this.languageState.detectedLanguages) {
      return this.languageState.detectedLanguages;
    }

    if (!TranslationsParent.getIsTranslationsEngineSupported()) {
      return null;
    }

    if (htmlLangAttribute === undefined) {
      htmlLangAttribute = await this.queryDocumentElementLang();
      if (this.#isDestroyed) {
        return null;
      }
    }

    htmlLangAttribute = this.maybeRefineMacroLanguageTag(htmlLangAttribute);

    let languagePairs = await TranslationsParent.getNonPivotLanguagePairs();
    if (this.#isDestroyed) {
      return null;
    }

    const langTags = {
      docLangTag: null,
      userLangTag: null,
      isDocLangTagSupported: false,
      htmlLangAttribute,
      identifiedLangTag: null,
    };

    /**
     * Attempts to find a compatible source language tag that matches
     * langTags.docLangTag. If a match is found, sets langTags.docLangTag
     * to the normalized value and sets langTags.isDocLangTagSupported to true.
     */
    function findCompatibleDocLangTag() {
      const compatibleLangTag =
        TranslationsParent.findCompatibleSourceLangTagSync(
          langTags.docLangTag,
          languagePairs
        );

      if (compatibleLangTag) {
        langTags.docLangTag = compatibleLangTag;
        langTags.isDocLangTagSupported = true;
      }
    }

    /**
     * Attempts to normalize the langTags.docLangTag value to a language tag that is
     * compatible as a source language for one of the translation models. If a language
     * tag is found, sets langTags.isDocLangTagSupported to `true`.
     */
    function maybeNormalizeDocLangTag() {
      if (!langTags.isDocLangTagSupported) {
        findCompatibleDocLangTag();
      }

      if (langTags.docLangTag && !langTags.isDocLangTagSupported) {
        // We have found a docLangTag, but it is still not supported.
        // Try it again with a canonicalized version.
        langTags.docLangTag = Intl.getCanonicalLocales(langTags.docLangTag)[0];
        findCompatibleDocLangTag();
      }
    }

    // First try to get the langTag from the document's markup.
    // Attempt to find a supported locale from highest specificity to lowest specificity.
    try {
      langTags.docLangTag = new Intl.Locale(htmlLangAttribute).baseName;
      maybeNormalizeDocLangTag();
    } catch (error) {
      // Failed to create a locale from htmlLangAttribute, continue on.
    }

    if (!langTags.docLangTag) {
      // If the document's markup had no specified langTag, attempt to identify the page's language.
      const identifyResult = await this.queryIdentifyLanguage();
      if (identifyResult.confident) {
        // Only set this as document language if we are confident.
        langTags.docLangTag = identifyResult.language;
      }
      langTags.identifiedLangTag = identifyResult.language;
      langTags.identifiedLangConfident = identifyResult.confident;
      if (this.#isDestroyed) {
        return null;
      }
      maybeNormalizeDocLangTag();
      langTags.identifiedLangTag = langTags.docLangTag;
    }

    if (!langTags.docLangTag) {
      const message = "No valid language detected.";
      ChromeUtils.addProfilerMarker(
        "TranslationsParent",
        { innerWindowId: this.innerWindowId },
        message
      );
      lazy.console.log(message, href);

      const langTag = await TranslationsParent.getTopPreferredSupportedToLang();
      if (this.#isDestroyed) {
        return null;
      }

      if (langTag) {
        langTags.userLangTag = langTag;
      }

      return langTags;
    }

    if (
      TranslationsParent.getWebContentLanguages()
        .keys()
        .some(langTag =>
          lazy.TranslationsUtils.langTagsMatch(langTag, langTags.docLangTag)
        )
    ) {
      // The doc language has been marked as a known language by the user, do not
      // offer a translation.
      const message =
        "The app and document languages match, so not translating.";
      ChromeUtils.addProfilerMarker(
        "TranslationsParent",
        { innerWindowId: this.innerWindowId },
        message
      );
      lazy.console.log(message, href);
      // The docLangTag will be set, while the userLangTag will be null.
      return langTags;
    }

    const langTag = await TranslationsParent.getTopPreferredSupportedToLang({
      excludeLangTags: [langTags.docLangTag],
    });
    if (this.#isDestroyed) {
      return null;
    }

    if (langTag) {
      langTags.userLangTag = langTag;
    }

    if (!langTags.userLangTag) {
      // No language pairs match.
      const message = `No matching language pairs were found for translating from "${langTags.docLangTag}".`;
      ChromeUtils.addProfilerMarker(
        "TranslationsParent",
        { innerWindowId: this.innerWindowId },
        message
      );
      lazy.console.log(message, languagePairs);
    }

    return langTags;
  }

  /**
   * The pref for if we can always offer a translation when it's available.
   */
  static shouldAlwaysOfferTranslations() {
    return lazy.automaticallyPopupPref;
  }

  /**
   * Returns true if the given language tag is present in the always-translate
   * languages preference, otherwise false.
   *
   * @param {LangTags} langTags
   * @returns {boolean}
   */
  static shouldAlwaysTranslateLanguage(langTags) {
    const { docLangTag, userLangTag } = langTags;
    if (
      !userLangTag ||
      lazy.TranslationsUtils.langTagsMatch(docLangTag, userLangTag)
    ) {
      // Do not auto-translate when the docLangTag matches the userLangTag, or when
      // the userLangTag is not set. The "always translate" is exposed via about:confg.
      // In case of users putting in non-sensical things here, we don't want to break
      // the experience. This behavior can lead to a "language degradation machine"
      // where we go from a source language -> pivot language -> source language.
      return false;
    }
    return (
      lazy.alwaysTranslateLangTags.has(docLangTag) ||
      [...lazy.alwaysTranslateLangTags.values()].some(alwaysTranslateLangTag =>
        lazy.TranslationsUtils.langTagsMatch(alwaysTranslateLangTag, docLangTag)
      )
    );
  }

  /**
   * Returns true if the given language tag is present in the never-translate
   * languages preference, otherwise false.
   *
   * @param {string} langTag - A BCP-47 language tag
   * @returns {boolean}
   */
  static shouldNeverTranslateLanguage(langTag) {
    return (
      lazy.neverTranslateLangTags.has(langTag) ||
      [...lazy.neverTranslateLangTags.values()].some(neverTranslateLangTag =>
        lazy.TranslationsUtils.langTagsMatch(neverTranslateLangTag, langTag)
      )
    );
  }

  /**
   * Returns true if the current site is denied permissions to translate,
   * otherwise returns false.
   *
   * @returns {Promise<boolean>}
   */
  shouldNeverTranslateSite() {
    const perms = Services.perms;
    const permission = perms.getPermissionObject(
      this.browsingContext.currentWindowGlobal.documentPrincipal,
      TRANSLATIONS_PERMISSION,
      /* exactHost */ false
    );
    return permission?.capability === perms.DENY_ACTION;
  }

  /**
   * Removes the given language tag from the given preference.
   *
   * @param {string} langTag - A BCP-47 language tag
   * @param {string} prefName - The pref name
   */
  static removeLangTagFromPref(langTag, prefName) {
    const langTags =
      prefName === ALWAYS_TRANSLATE_LANGS_PREF
        ? lazy.alwaysTranslateLangTags
        : lazy.neverTranslateLangTags;
    const newLangTags = [...langTags].filter(tag => tag !== langTag);
    Services.prefs.setCharPref(prefName, [...newLangTags].join(","));
  }

  /**
   * Adds the given language tag to the given preference.
   *
   * @param {string} langTag - A BCP-47 language tag
   * @param {string} prefName - The pref name
   */
  static addLangTagToPref(langTag, prefName) {
    const langTags =
      prefName === ALWAYS_TRANSLATE_LANGS_PREF
        ? lazy.alwaysTranslateLangTags
        : lazy.neverTranslateLangTags;
    if (!langTags.has(langTag)) {
      langTags.add(langTag);
    }
    Services.prefs.setCharPref(prefName, [...langTags].join(","));
  }

  /**
   * Stores the given langTag as the most recent target language in the
   * browser.translations.mostRecentTargetLanguage pref.
   *
   * @param {string} langTag - A BCP-47 language tag.
   */
  static storeMostRecentTargetLanguage(langTag) {
    // The pref's language tags are managed by this function as a unique-item
    // sliding window with a max size.
    //
    // Examples with MAX_SIZE = 3:
    //
    //  Add a new item to an empty window:
    //  [ ] + a => [a]
    //
    //  Add a new item to a non-full window:
    //  [a] + b => [a, b]
    //
    //  [a, b] + c => [a, b, c]
    //
    //  Add a new item to a full window:
    //  [a, b, c] + z => [b, c, z]
    //
    //  Add an item that is already within a window:
    //  [b, c, z] + z => [b, c, z]
    //
    //  [b, c, z] + c => [b, z, c]
    //
    //  [b, z, c] + b => [z, c, b]
    const MAX_SIZE = 3;
    const mostRecentTargetLanguages = lazy.mostRecentTargetLanguages;

    if (
      mostRecentTargetLanguages.has(langTag) ||
      [...mostRecentTargetLanguages.values()].some(recentLangTag =>
        lazy.TranslationsUtils.langTagsMatch(recentLangTag, langTag)
      )
    ) {
      // The language tag is already present, so delete it to ensure that its order is updated when it gets re-added.
      mostRecentTargetLanguages.delete(langTag);
    } else if (mostRecentTargetLanguages.size === MAX_SIZE) {
      // We only store MAX_SIZE lang tags, so remove the oldest language tag to make room for the new language tag.
      const oldestLangTag = mostRecentTargetLanguages.keys().next().value;
      mostRecentTargetLanguages.delete(oldestLangTag);
    }

    mostRecentTargetLanguages.add(langTag);

    Services.prefs.setCharPref(
      "browser.translations.mostRecentTargetLanguages",
      [...mostRecentTargetLanguages].join(",")
    );
  }

  /**
   * Toggles the always-translate language preference by adding the language
   * to the pref list if it is not present, or removing it if it is present.
   *
   * @param {LangTags} langTags
   * @returns {boolean}
   *  True if always-translate was enabled for this language.
   *  False if always-translate was disabled for this language.
   */
  static toggleAlwaysTranslateLanguagePref(langTags) {
    const { appLangTag, docLangTag } = langTags;

    if (lazy.TranslationsUtils.langTagsMatch(appLangTag, docLangTag)) {
      // In case somehow the user attempts to toggle this when the app and doc language
      // are the same, just remove the lang tag.
      this.removeLangTagFromPref(appLangTag, ALWAYS_TRANSLATE_LANGS_PREF);
      return false;
    }

    if (TranslationsParent.shouldAlwaysTranslateLanguage(langTags)) {
      // The pref was toggled off for this langTag
      this.removeLangTagFromPref(docLangTag, ALWAYS_TRANSLATE_LANGS_PREF);
      return false;
    }

    // The pref was toggled on for this langTag
    this.addLangTagToPref(docLangTag, ALWAYS_TRANSLATE_LANGS_PREF);
    this.removeLangTagFromPref(docLangTag, NEVER_TRANSLATE_LANGS_PREF);
    return true;
  }

  static getAlwaysTranslateLanguages() {
    return lazy.alwaysTranslateLangTags;
  }

  static getNeverTranslateLanguages() {
    return lazy.neverTranslateLangTags;
  }

  /**
   * Toggle the automatically popup pref, which will either
   * enable or disable translations being offered to the user.
   *
   * @returns {boolean}
   *  True if offering translations was enabled by this call.
   *  False if offering translations was disabled by this call.
   */
  static toggleAutomaticallyPopupPref() {
    const prefValueBeforeToggle = lazy.automaticallyPopupPref;
    Services.prefs.setBoolPref(
      "browser.translations.automaticallyPopup",
      !prefValueBeforeToggle
    );
    return !prefValueBeforeToggle;
  }

  /**
   * Toggles the never-translate language preference by adding the language
   * to the pref list if it is not present, or removing it if it is present.
   *
   * @param {string} langTag - A BCP-47 language tag
   * @returns {boolean} Whether the pref was toggled on or off for this langTag.
   *  True if never-translate was enabled for this language.
   *  False if never-translate was disabled for this language.
   */
  static toggleNeverTranslateLanguagePref(langTag) {
    if (TranslationsParent.shouldNeverTranslateLanguage(langTag)) {
      // The pref was toggled off for this langTag
      this.removeLangTagFromPref(langTag, NEVER_TRANSLATE_LANGS_PREF);
      return false;
    }

    // The pref was toggled on for this langTag
    this.addLangTagToPref(langTag, NEVER_TRANSLATE_LANGS_PREF);
    this.removeLangTagFromPref(langTag, ALWAYS_TRANSLATE_LANGS_PREF);
    return true;
  }

  /**
   * Toggles the never-translate site permissions by adding DENY_ACTION to
   * the site principal if it is not present, or removing it if it is present.
   *
   * @returns {boolean}
   *  True if never-translate was enabled for this site.
   *  False if never-translate was disabled for this site.
   */
  toggleNeverTranslateSitePermissions() {
    if (this.shouldNeverTranslateSite()) {
      return this.setNeverTranslateSitePermissions(false);
    }

    return this.setNeverTranslateSitePermissions(true);
  }

  /**
   * Sets the never-translate site permissions by adding DENY_ACTION to
   * the site principal.
   *
   * @param {string} neverTranslate - The never translate setting.
   * @returns {boolean}
   *  True if never-translate was enabled for this site.
   *  False if never-translate was disabled for this site.
   */
  setNeverTranslateSitePermissions(neverTranslate) {
    const { documentPrincipal } = this.browsingContext.currentWindowGlobal;
    return TranslationsParent.#setNeverTranslateSiteByPrincipal(
      neverTranslate,
      documentPrincipal
    );
  }

  /**
   * Sets the never-translate site permissions by creating a principal from the URL origin
   * and setting or unsetting the DENY_ACTION on the permission.
   *
   * @param {string} neverTranslate - The never translate setting to use.
   * @param {string} urlOrigin - The url origin to set the permission for.
   * @returns {boolean}
   *  True if never-translate was enabled for this origin.
   *  False if never-translate was disabled for this origin.
   */
  static setNeverTranslateSiteByOrigin(neverTranslate, urlOrigin) {
    const principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        urlOrigin
      );
    return TranslationsParent.#setNeverTranslateSiteByPrincipal(
      neverTranslate,
      principal
    );
  }

  /**
   * Sets the never-translate site permissions by adding DENY_ACTION to
   * the specified site principal.
   *
   * @param {string} neverTranslate - The never translate setting.
   * @param {string} principal - The principal that should have the permission attached.
   * @returns {boolean}
   *  True if never-translate was enabled for this principal.
   *  False if never-translate was disabled for this principal.
   */
  static #setNeverTranslateSiteByPrincipal(neverTranslate, principal) {
    const perms = Services.perms;

    if (!neverTranslate) {
      perms.removeFromPrincipal(principal, TRANSLATIONS_PERMISSION);
      return false;
    }

    perms.addFromPrincipal(
      principal,
      TRANSLATIONS_PERMISSION,
      perms.DENY_ACTION
    );
    return true;
  }

  /**
   * Creates a list of URLs that have a translations permission set on the resource.
   * These are the sites to never translate.
   *
   * @returns {Array<string>} String array with the URL of the sites that have the never translate permission.
   */
  static listNeverTranslateSites() {
    const neverTranslateSites = [];
    for (const perm of Services.perms.getAllByTypes([
      TRANSLATIONS_PERMISSION,
    ])) {
      if (perm.capability === Services.perms.DENY_ACTION) {
        neverTranslateSites.push(perm.principal.origin);
      }
    }
    let stripProtocol = s => s?.replace(/^\w+:/, "") || "";
    return neverTranslateSites.sort((a, b) => {
      return stripProtocol(a).localeCompare(stripProtocol(b));
    });
  }

  /**
   * Ensure that the translations are always destroyed, even if the content translations
   * are misbehaving.
   */
  #ensureTranslationsDiscarded() {
    if (this.engineActor && this.languageState.requestedLanguagePair) {
      this.engineActor.discardTranslations(this.innerWindowId);
    }
  }

  didDestroy() {
    if (!this.innerWindowId) {
      throw new Error(
        "The innerWindowId for the TranslationsParent was not available."
      );
    }

    if (this.#boundObserve) {
      Services.obs.removeObserver(
        this.#boundObserve,
        TOPIC_MAYBE_UPDATE_USER_LANG_TAG
      );
      this.#boundObserve = null;
    }

    this.#ensureTranslationsDiscarded();

    this.#isDestroyed = true;
  }
}

/**
 * Validate some simple Wasm that uses a SIMD operation.
 */
function detectSimdSupport() {
  try {
    return WebAssembly.validate(
      new Uint8Array(
        // ```
        // ;; Detect SIMD support.
        // ;; Compile by running: wat2wasm --enable-all simd-detect.wat
        //
        // (module
        //   (func (result v128)
        //     i32.const 0
        //     i8x16.splat
        //     i8x16.popcnt
        //   )
        // )
        // ```

        // prettier-ignore
        [
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00,
        0x01, 0x7b, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x0a, 0x01, 0x08, 0x00, 0x41, 0x00,
        0xfd, 0x0f, 0xfd, 0x62, 0x0b
      ]
      )
    );
  } catch {
    return false;
  }
}

/**
 * State that affects the UI. Any of the state that gets set triggers a dispatch to update
 * the UI.
 */
class TranslationsLanguageState {
  /**
   * @param {TranslationsParent} actor
   * @param {LangTags | null} previousDetectedLanguages
   */
  constructor(actor, previousDetectedLanguages = null) {
    this.#actor = actor;
    this.#detectedLanguages = previousDetectedLanguages;
  }

  /**
   * The data members for TranslationsLanguageState, see the getters for their
   * documentation.
   */

  /** @type {TranslationsParent} */
  #actor;

  /** @type {LanguagePair | null} */
  #requestedLanguagePair = null;

  /** @type {LangTags | null} */
  #detectedLanguages = null;

  /** @type {boolean} */
  #hasVisibleChange = false;

  /** @type {null | TranslationErrors} */
  #error = null;

  #isEngineReady = false;

  /**
   * Dispatch anytime the language details change, so that any UI can react to it.
   */
  dispatch({ reason } = {}) {
    const browser = this.#actor.browsingContext.top.embedderElement;
    if (!browser) {
      return;
    }
    const { CustomEvent } = browser.ownerGlobal;
    browser.dispatchEvent(
      new CustomEvent("TranslationsParent:LanguageState", {
        bubbles: true,
        detail: {
          actor: this.#actor,
          reason,
        },
      })
    );
  }

  /**
   * When a translation is requested, this contains the language pair. This means
   * that the TranslationsChild should be creating a TranslationsDocument and keep
   * the page updated with the target language.
   *
   * @returns {LanguagePair | null}
   */
  get requestedLanguagePair() {
    return this.#requestedLanguagePair;
  }

  set requestedLanguagePair(requestedLanguagePair) {
    if (this.#requestedLanguagePair === requestedLanguagePair) {
      return;
    }

    this.#error = null;
    this.#isEngineReady = false;
    this.#requestedLanguagePair = requestedLanguagePair;
    this.dispatch({ reason: "requestedLanguagePair" });
  }

  /**
   * The stored results for the detected languages.
   *
   * @returns {LangTags | null}
   */
  get detectedLanguages() {
    return this.#detectedLanguages;
  }

  set detectedLanguages(detectedLanguages) {
    if (this.#detectedLanguages === detectedLanguages) {
      return;
    }

    this.#detectedLanguages = detectedLanguages;
    this.dispatch({ reason: "detectedLanguages" });
  }

  /**
   * A visual translation change occurred on the DOM.
   *
   * @returns {boolean}
   */
  get hasVisibleChange() {
    return this.#hasVisibleChange;
  }

  set hasVisibleChange(hasVisibleChange) {
    if (this.#hasVisibleChange === hasVisibleChange) {
      return;
    }

    this.#hasVisibleChange = hasVisibleChange;
    this.dispatch({ reason: "hasVisibleChange" });
  }

  /**
   * When the location changes remove the previous error and dispatch a change event
   * so that any browser chrome UI that needs to be updated can get the latest state.
   */
  locationChanged() {
    this.#error = null;
    this.dispatch({ reason: "locationChanged" });
  }

  /**
   * Makes a determination about whether to update the cached userLangTag with the given langTag.
   */
  maybeUpdateUserLangTag(langTag) {
    const currentUserLangTag = this.#detectedLanguages?.userLangTag;

    if (!currentUserLangTag) {
      // The userLangTag is not present in the detectedLanguages cache.
      // This is intentional and we should not update it in this case,
      // otherwise we may end up showing the Translations URL-bar button
      // on a page where it is currently hidden.
      return;
    }

    this.#detectedLanguages.userLangTag = langTag;
    // There is no need to call this.dispatch() in this function.
    //
    // Updating the userLangTag will affect which language is offered the next time
    // a panel is opened, or which language is auto-translated into when a page loads,
    // but this information should not eagerly affect the visual states of Translations
    // content across the browser. Relevant consumers will fetch the updated langTag from
    // the cache when they need it.
    //
    // In theory, calling this.dispatch() should be fine to do since the LanguageState event
    // guards itself against irrelevant changes, but that would ultimately cause unneeded noise.
  }

  /**
   * The last error that occurred during translation.
   */
  get error() {
    return this.#error;
  }

  set error(error) {
    if (this.#error === error) {
      return;
    }
    this.#error = error;
    // Setting an error invalidates the requested language pair.
    this.#requestedLanguagePair = null;
    this.#isEngineReady = false;
    this.dispatch({ reason: "error" });
  }

  /**
   * Stores when the translations engine is ready. The wasm and language files must
   * be downloaded, which can take some time.
   */
  get isEngineReady() {
    return this.#isEngineReady;
  }

  set isEngineReady(isEngineReady) {
    if (this.#isEngineReady === isEngineReady) {
      return;
    }
    this.#isEngineReady = isEngineReady;
    this.dispatch({ reason: "isEngineReady" });
  }
}

/**
 * @typedef {object} QueueItem
 * @property {Function} download
 * @property {Function} [onSuccess]
 * @property {Function} [onFailure]
 * @property {number} [retriesLeft]
 */

/**
 * Manage the download of the files by providing a maximum number of concurrent files
 * and the ability to retry a file download in case of an error.
 *
 * @param {QueueItem[]} queue
 */
async function downloadManager(queue) {
  const NOOP = () => {};

  const pendingDownloadAttempts = new Set();
  let failCount = 0;
  let index = 0;
  const start = Date.now();
  const originalQueueLength = queue.length;

  while (index < queue.length || pendingDownloadAttempts.size > 0) {
    // Start new downloads up to the maximum limit
    while (
      index < queue.length &&
      pendingDownloadAttempts.size < TranslationsParent.MAX_CONCURRENT_DOWNLOADS
    ) {
      lazy.console.log(`Starting download ${index + 1} of ${queue.length}`);

      const {
        download,
        onSuccess = NOOP,
        onFailure = NOOP,
        retriesLeft = TranslationsParent.MAX_DOWNLOAD_RETRIES,
      } = queue[index];

      const handleFailedDownload = error => {
        // The download failed. Either retry it, or report the failure.
        TranslationsParent.reportError(
          new Error("Failed to download file."),
          error
        );

        const newRetriesLeft = retriesLeft - 1;

        if (retriesLeft > 0) {
          lazy.console.log(
            `Queueing another attempt. ${newRetriesLeft} attempts left.`
          );
          queue.push({
            download,
            retriesLeft: newRetriesLeft,
            onSuccess,
            onFailure,
          });
        } else {
          // Give up on this download.
          failCount++;
          onFailure();
        }
      };

      const afterDownloadAttempt = () => {
        pendingDownloadAttempts.delete(downloadAttempt);
      };

      // Kick off the download. If it fails, retry it a certain number of attempts.
      // This is done asynchronously from the rest of the for loop.
      const downloadAttempt = download()
        .then(onSuccess, handleFailedDownload)
        .then(afterDownloadAttempt);

      pendingDownloadAttempts.add(downloadAttempt);
      index++;
    }

    // Wait for any active downloads to complete.
    await Promise.race(pendingDownloadAttempts);
  }

  const duration = ((Date.now() - start) / 1000).toFixed(3);

  if (failCount > 0) {
    const message = `Finished downloads in ${duration} seconds, but ${failCount} download(s) failed.`;
    lazy.console.log(
      `Finished downloads in ${duration} seconds, but ${failCount} download(s) failed.`
    );
    throw new Error(message);
  }

  lazy.console.log(
    `Finished ${originalQueueLength} downloads in ${duration} seconds.`
  );
}

/**
 * The translations code has lots of async code and fallible network requests. To test
 * this manually while using the feature, enable chaos mode by setting "errors" to true
 * and "timeoutMS" to a positive number of milliseconds.
 * prefs to true:
 *
 *  - browser.translations.chaos.timeoutMS
 *  - browser.translations.chaos.errors
 */
async function chaosMode(probability = 0.5) {
  await chaosModeTimer();
  await chaosModeError(probability);
}

/**
 * The translations code has lots of async code that relies on the network. To test
 * this manually while using the feature, enable chaos mode by setting the following pref
 * to a positive number of milliseconds.
 *
 *  - browser.translations.chaos.timeoutMS
 */
async function chaosModeTimer() {
  if (lazy.chaosTimeoutMSPref) {
    const timeout = Math.random() * lazy.chaosTimeoutMSPref;
    lazy.console.log(
      `Chaos mode timer started for ${(timeout / 1000).toFixed(1)} seconds.`
    );
    await new Promise(resolve => lazy.setTimeout(resolve, timeout));
  }
}

/**
 * The translations code has lots of async code that is fallible. To test this manually
 * while using the feature, enable chaos mode by setting the following pref to true.
 *
 *  - browser.translations.chaos.errors
 */
async function chaosModeError(probability = 0.5) {
  if (lazy.chaosErrorsPref && Math.random() < probability) {
    lazy.console.trace(`Chaos mode error generated.`);
    throw new Error(
      `Chaos Mode error from the pref "browser.translations.chaos.errors".`
    );
  }
}
