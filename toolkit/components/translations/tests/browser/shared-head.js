/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * @type {import("../../../ml/content/EngineProcess.sys.mjs")}
 */
const { EngineProcess } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EngineProcess.sys.mjs"
);
const { TranslationsPanelShared } = ChromeUtils.importESModule(
  "chrome://browser/content/translations/TranslationsPanelShared.sys.mjs"
);
const { TranslationsFeature } = ChromeUtils.importESModule(
  "chrome://global/content/translations/TranslationsFeature.sys.mjs"
);
const { TranslationsUtils } = ChromeUtils.importESModule(
  "chrome://global/content/translations/TranslationsUtils.mjs"
);

// This is a bit silly, but ml/tests/browser/head.js relies on this function:
// https://searchfox.org/mozilla-central/rev/14f68f084d6a3bc438a3f973ed81d3a4dbab9629/toolkit/components/ml/tests/browser/head.js#23-25
//
// And it also pulls in the entirety of this file.
// https://searchfox.org/mozilla-central/rev/14f68f084d6a3bc438a3f973ed81d3a4dbab9629/toolkit/components/ml/tests/browser/head.js#41-46
//
// So we can't have a naming conflict of a variable defined twice like this.
// https://bugzilla.mozilla.org/show_bug.cgi?id=1949530
const { getInferenceProcessInfo: fetchInferenceProcessInfo } =
  ChromeUtils.importESModule("chrome://global/content/ml/Utils.sys.mjs");

// Avoid about:blank's non-standard behavior.
const BLANK_PAGE =
  "data:text/html;charset=utf-8,<!DOCTYPE html><title>Blank</title>Blank page";

const URL_COM_PREFIX = "https://example.com/browser/";
const URL_ORG_PREFIX = "https://example.org/browser/";
const CHROME_URL_PREFIX = "chrome://mochitests/content/browser/";
const DIR_PATH = "toolkit/components/translations/tests/browser/";

/**
 * @template D, T
 * @typedef {(
 *   fn: (selectors: Record<string, string>, data: D) => Promise<void>,
 *   data: T
 * ) => Promise<T>} RunInPageFn
 */

/**
 * @typedef {"click" | "enter" | "space"} AboutTranslationsInvokeAction
 */

/**
 * Use a utility function to make this easier to read.
 *
 * @param {string} path
 * @returns {string}
 */
function _url(path) {
  return URL_COM_PREFIX + DIR_PATH + path;
}

const BLANK_PAGE_URL = _url("translations-tester-blank.html");
const SPANISH_PAGE_URL = _url("translations-tester-es.html");
const SPANISH_PAGE_URL_2 = _url("translations-tester-es-2.html");
const SPANISH_PAGE_SHORT_URL = _url("translations-tester-es-short.html");
const SPANISH_PAGE_MISMATCH_URL = _url("translations-tester-es-mismatch.html");
const SPANISH_PAGE_MISMATCH_SHORT_URL = _url("translations-tester-es-mismatch-short.html"); // prettier-ignore
const SPANISH_PAGE_UNDECLARED_URL = _url("translations-tester-es-undeclared.html"); // prettier-ignore
const SPANISH_PAGE_UNDECLARED_SHORT_URL = _url("translations-tester-es-undeclared-short.html"); // prettier-ignore
const ENGLISH_PAGE_URL = _url("translations-tester-en.html");
const FRENCH_PAGE_URL = _url("translations-tester-fr.html");
const NO_LANGUAGE_URL = _url("translations-tester-no-tag.html");
const PDF_TEST_PAGE_URL = _url("translations-tester-pdf-file.pdf");
const SELECT_TEST_PAGE_URL = _url("translations-tester-select.html");
const TEXT_CLEANING_URL = _url("translations-text-cleaning.html");
const ENGLISH_BENCHMARK_PAGE_URL = _url("translations-bencher-en.html");

const SPANISH_PAGE_URL_DOT_ORG =
  URL_ORG_PREFIX + DIR_PATH + "translations-tester-es.html";

const PIVOT_LANGUAGE = "en";
const LANGUAGE_PAIRS = [
  { fromLang: PIVOT_LANGUAGE, toLang: "es" },
  { fromLang: "es", toLang: PIVOT_LANGUAGE },
  { fromLang: PIVOT_LANGUAGE, toLang: "fr" },
  { fromLang: "fr", toLang: PIVOT_LANGUAGE },
  { fromLang: PIVOT_LANGUAGE, toLang: "uk" },
  { fromLang: "uk", toLang: PIVOT_LANGUAGE },
];

const LANGUAGE_PAIRS_WITHOUT_SPANISH = LANGUAGE_PAIRS.filter(
  // Spanish is intentionally omitted so that "es" may trigger
  // the unsupported-language message when relevant.
  ({ fromLang, toLang }) => fromLang !== "es" && toLang !== "es"
);

const TRANSLATIONS_PERMISSION = "translations";
const ALWAYS_TRANSLATE_LANGS_PREF =
  "browser.translations.alwaysTranslateLanguages";
const NEVER_TRANSLATE_LANGS_PREF =
  "browser.translations.neverTranslateLanguages";
const USE_LEXICAL_SHORTLIST_PREF = "browser.translations.useLexicalShortlist";

/**
 * Provide a uniform way to log actions. This abuses the Error stack to get the callers
 * of the action. This should help in test debugging.
 */
function logAction(...params) {
  const error = new Error();
  const stackLines = error.stack.split("\n");
  const actionName = stackLines[1]?.split("@")[0] ?? "";
  const taskFileLocation = stackLines[2]?.split("@")[1] ?? "";
  if (taskFileLocation.includes("head.js")) {
    // Only log actions that were done at the test level.
    return;
  }

  info(`Action: ${actionName}(${params.join(", ")})`);
  info(
    `Source: ${taskFileLocation.replace(
      "chrome://mochitests/content/browser/",
      ""
    )}`
  );
}

/**
 * Returns the baseline visibility expectations for the main about:translations UI.
 *
 * @param {object} [overrides={}]
 * @returns {object}
 */
function aboutTranslationsVisibilityExpectations(overrides = {}) {
  return {
    pageHeader: true,
    mainUserInterface: true,
    sourceLanguageSelector: true,
    targetLanguageSelector: true,
    clearButton: undefined,
    copyButton: true,
    swapLanguagesButton: true,
    sourceSectionTextArea: true,
    targetSectionTextArea: true,
    detectedLanguageUnsupportedMessage: false,
    translationErrorMessage: false,
    unsupportedInfoMessage: false,
    policyDisabledInfoMessage: false,
    featureBlockedInfoMessage: false,
    languageLoadErrorMessage: false,
    ...overrides,
  };
}

/**
 * Returns the baseline visibility expectations for about:translations when showing
 * a stand-alone message bar.
 *
 * @param {object} [overrides={}]
 * @returns {object}
 */
function aboutTranslationsStandaloneMessageVisibilityExpectations(
  overrides = {}
) {
  return aboutTranslationsVisibilityExpectations({
    mainUserInterface: false,
    sourceLanguageSelector: false,
    targetLanguageSelector: false,
    copyButton: false,
    swapLanguagesButton: false,
    sourceSectionTextArea: false,
    targetSectionTextArea: false,
    ...overrides,
  });
}

/**
 * Generates a sorted list of Translation model file names for the given language pairs.
 *
 * @param {Array<{ fromLang: string, toLang: string }>} languagePairs - An array of language pair objects.
 *
 * @returns {string[]} A sorted array of translation model file names.
 */
function languageModelNames(languagePairs) {
  return languagePairs
    .flatMap(({ fromLang, toLang }) => [
      `model.${fromLang}${toLang}.intgemm.alphas.bin`,
      `vocab.${fromLang}${toLang}.spm`,
      ...(Services.prefs.getBoolPref(USE_LEXICAL_SHORTLIST_PREF)
        ? [`lex.50.50.${fromLang}${toLang}.s2t.bin`]
        : []),
    ])
    .sort();
}

/**
 * Start an HTTP server that serves page.html with the provided HTML.
 * Explicitly encode the text as UTF-8 to correctly handle characters outside Latin-1,
 * which the HttpServer renders incorrectly by default.
 *
 * @param {string} html
 * @param {number} statusCode
 */
function serveOnce(html, statusCode = 200) {
  /** @type {import("../../../../../netwerk/test/httpserver/httpd.sys.mjs")} */
  const { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );
  info("Create server");
  const server = new HttpServer();

  const { promise, resolve } = Promise.withResolvers();
  const encoder = new TextEncoder();
  const htmlUtf8 = encoder.encode(html);

  server.registerPathHandler("/page.html", (request, response) => {
    info("Request received for: " + url);
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.setStatusLine(request.httpVersion, statusCode);

    const binaryOutputStream = Cc[
      "@mozilla.org/binaryoutputstream;1"
    ].createInstance(Ci.nsIBinaryOutputStream);

    binaryOutputStream.setOutputStream(response.bodyOutputStream);
    binaryOutputStream.writeByteArray(htmlUtf8);

    resolve(server.stop());
  });

  server.start(-1);

  let { primaryHost, primaryPort } = server.identity;
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  const url = `http://${primaryHost}:${primaryPort}/page.html`;
  info("Server listening for: " + url);

  return { url, serverClosed: promise };
}

/**
 * Loads a new page in the given browser at the given URL.
 *
 * @param {object} browser
 * @param {string} url
 */
async function loadNewPage(browser, url) {
  BrowserTestUtils.startLoadingURIString(browser, url);
  await BrowserTestUtils.browserLoaded(
    browser,
    /* includeSubFrames */ false,
    url
  );
}

/**
 * The mochitest runs in the parent process. This function opens up a new tab,
 * opens up about:translations, and passes the test requirements into the content process.
 *
 * @param {object} [options={}]
 * @param {boolean} [options.featureEnabled=true]
 *        Whether Translations starts enabled before opening the page.
 * @param {boolean} [options.lockEnabledState=false]
 *        When true, locks the prefs that govern the Translations feature enabled state as
 *        though they were controlled by an enterprise policy before opening the page.
 * @param {Array<{fromLang: string, toLang: string}>} [options.languagePairs=LANGUAGE_PAIRS]
 *        Language pairs that should be available in Remote Settings mocks.
 * @param {Array<[string, any]>} [options.prefs]
 *        Preference tuples to push before the page loads.
 * @param {boolean} [options.autoDownloadFromRemoteSettings=false]
 *        When true, Remote Settings downloads resolve automatically.
 *        When false, resolveDownloads or rejectDownloads must be manually called.
 * @param {number} [options.copyButtonResetDelay]
 *        Overrides the copy button reset timeout ms to be shorter for testing.
 * @param {boolean} [options.requireManualCopyButtonReset]
 *        When true, copy button resets must be triggered manually by tests.
 * @returns {Promise<{
 *   aboutTranslationsTestUtils: AboutTranslationsTestUtils,
 *   cleanup: () => Promise<void>
 * }>}
 */
async function openAboutTranslations({
  featureEnabled = true,
  lockEnabledState = false,
  languagePairs = LANGUAGE_PAIRS,
  prefs,
  autoDownloadFromRemoteSettings = false,
  copyButtonResetDelay,
  requireManualCopyButtonReset,
} = {}) {
  if (
    copyButtonResetDelay !== undefined &&
    requireManualCopyButtonReset !== undefined
  ) {
    throw new Error(
      "copyButtonResetDelay and requireManualCopyButtonReset cannot both be defined."
    );
  }
  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.translations.enable", featureEnabled],
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.translations", "default"],
      ["browser.translations.logLevel", "All"],
      ["browser.translations.mostRecentTargetLanguages", ""],
      ["dom.events.testing.asyncClipboard", true],
      [USE_LEXICAL_SHORTLIST_PREF, false],
      ...(prefs ?? []),
    ],
  });
  const lockedFeaturePrefs = [];

  if (!featureEnabled) {
    await TranslationsFeature.block();
  }

  if (lockEnabledState) {
    for (const pref of ["browser.ai.control.translations"]) {
      Services.prefs.lockPref(pref);
      lockedFeaturePrefs.push(pref);
    }
  }

  /**
   * Collect any relevant selectors for the page here.
   */
  const selectors = {
    pageHeader: "header#about-translations-header",
    learnMoreLink: "a#about-translations-learn-more-link",
    mainUserInterface: "section#about-translations-main-user-interface",
    sourceLanguageSelector: "moz-select#about-translations-source-select",
    targetLanguageSelector: "moz-select#about-translations-target-select",
    detectLanguageOption:
      "moz-option#about-translations-detect-language-label-option",
    detectedLanguageUnsupportedHeading:
      "#about-translations-detected-language-unsupported-heading",
    detectedLanguageUnsupportedLearnMoreLink:
      "a#about-translations-detected-language-unsupported-learn-more-link",
    detectedLanguageUnsupportedMessage:
      "moz-message-bar#about-translations-detected-language-unsupported-message",
    swapLanguagesButton: "moz-button#about-translations-swap-languages-button",
    sourceSection: "div#about-translations-source-section",
    sourceSectionTextArea: "textarea#about-translations-source-textarea",
    targetSection: "div#about-translations-target-section",
    targetSectionTextArea: "textarea#about-translations-target-textarea",
    clearButton: "moz-button#about-translations-clear-button",
    copyButton: "moz-button#about-translations-copy-button",
    translationErrorMessage:
      "moz-message-bar#about-translations-translation-error-message",
    translationErrorButton:
      "moz-button#about-translations-translation-error-button",
    unsupportedInfoMessage:
      "moz-message-bar#about-translations-unsupported-info-message",
    policyDisabledInfoMessage:
      "moz-message-bar#about-translations-policy-disabled-info-message",
    featureBlockedInfoMessage:
      "moz-message-bar#about-translations-feature-blocked-info-message",
    unblockFeatureButton:
      "moz-button#about-translations-feature-blocked-unblock-button",
    languageLoadErrorMessage:
      "moz-message-bar#about-translations-language-load-error-message",
    languageLoadErrorButton:
      "button#about-translations-language-load-error-button",
  };

  // Start the tab at a blank page.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    BLANK_PAGE,
    true // waitForLoad
  );

  const { removeMocks, remoteClients } = await createAndMockRemoteSettings({
    languagePairs,
    autoDownloadFromRemoteSettings,
  });

  // Now load the about:translations page, since the actor could be mocked.
  await loadNewPage(tab.linkedBrowser, "about:translations#src=detect");

  // Ensure the window always opens with a horizontal page layout.
  // Divide everything by sqrt(2) to halve the overall content size.
  await ensureWindowSize(window, 1600 * Math.SQRT1_2, 900 * Math.SQRT1_2);
  FullZoom.setZoom(Math.SQRT1_2, tab.linkedBrowser);

  /**
   * @param {number} count - Count of the language pairs expected.
   */
  const resolveDownloads = async count => {
    await remoteClients.translationsWasm.waitForPendingDownloads(1);
    await remoteClients.translationsWasm.resolvePendingDownloads(1);
    await remoteClients.translationModels.waitForPendingDownloads(
      downloadedFilesPerLanguagePair() * count
    );
    await remoteClients.translationModels.resolvePendingDownloads(
      downloadedFilesPerLanguagePair() * count
    );
  };

  /**
   * @param {number} count - Count of the language pairs expected.
   */
  const rejectDownloads = async count => {
    await remoteClients.translationsWasm.rejectPendingDownloads(1);
    await remoteClients.translationModels.rejectPendingDownloads(
      downloadedFilesPerLanguagePair() * count
    );
  };

  const runInPage = (callback, data = {}) => {
    // TODO: Switch to SpecialPowers.spawn
    // eslint-disable-next-line mozilla/reject-contenttask-spawn
    return ContentTask.spawn(
      tab.linkedBrowser,
      { selectors, contentData: data, callbackSource: callback.toString() }, // Data to inject.
      function ({ selectors, contentData, callbackSource }) {
        // eslint-disable-next-line no-eval
        const contentCallback = eval(`(${callbackSource})`);
        return contentCallback(selectors, contentData);
      }
    );
  };

  const aboutTranslationsTestUtils = new AboutTranslationsTestUtils(
    tab.linkedBrowser,
    runInPage,
    resolveDownloads,
    rejectDownloads,
    autoDownloadFromRemoteSettings
  );

  await aboutTranslationsTestUtils.waitForReady();

  if (featureEnabled) {
    await aboutTranslationsTestUtils.setThrottleDelay(25);

    const isTranslationEngineSupported =
      TranslationsParent.getIsTranslationsEngineSupported();

    if (isTranslationEngineSupported) {
      // Consume any debounce event from startup so that test cases
      // will only receive debounce events from within the test task.
      await aboutTranslationsTestUtils.setDebounceDelay(0);
      await aboutTranslationsTestUtils.assertEvents(
        {
          expected: [
            [
              AboutTranslationsTestUtils.Events.SourceTextInputDebounced,
              { sourceText: "" },
            ],
          ],
        },
        async () => {
          await aboutTranslationsTestUtils.setSourceTextAreaValue("");
        }
      );
    }

    await aboutTranslationsTestUtils.setDebounceDelay(100);

    if (requireManualCopyButtonReset !== undefined) {
      await aboutTranslationsTestUtils.setManualCopyButtonResetEnabled(
        requireManualCopyButtonReset
      );
    } else if (copyButtonResetDelay !== undefined) {
      await aboutTranslationsTestUtils.setCopyButtonResetDelay(
        copyButtonResetDelay
      );
    }
  }

  return {
    aboutTranslationsTestUtils,
    async cleanup() {
      await aboutTranslationsTestUtils.setManualCopyButtonResetEnabled(false);
      await aboutTranslationsTestUtils.clearCopyButtonResetDelayOverride();
      await loadBlankPage();
      BrowserTestUtils.removeTab(tab);

      await removeMocks();
      await EngineProcess.destroyTranslationsEngine();
      for (const pref of lockedFeaturePrefs) {
        if (Services.prefs.prefIsLocked(pref)) {
          Services.prefs.unlockPref(pref);
        }
      }

      await SpecialPowers.popPrefEnv();
      TestTranslationsTelemetry.cleanup();
    },
  };
}

/**
 * Naively prettify's html based on the opening and closing tags. This is not robust
 * for general usage, but should be adequate for these tests.
 *
 * @param {string} html
 * @returns {string}
 */
function naivelyPrettify(html) {
  let result = "";
  let indent = 0;

  function addText(actualEndIndex) {
    const text = html.slice(startIndex, actualEndIndex).trim();
    if (text) {
      for (let i = 0; i < indent; i++) {
        result += "  ";
      }
      result += text + "\n";
    }
    startIndex = actualEndIndex;
  }

  let startIndex = 0;
  let endIndex = 0;
  for (; endIndex < html.length; endIndex++) {
    if (
      html[endIndex] === " " ||
      html[endIndex] === "\t" ||
      html[endIndex] === "n"
    ) {
      // Skip whitespace.
      // "   <div>foobar</div>"
      //  ^^^
      startIndex = endIndex;
      continue;
    }

    // Find all of the text.
    // "<div>foobar</div>"
    //       ^^^^^^
    while (endIndex < html.length && html[endIndex] !== "<") {
      endIndex++;
    }

    addText(endIndex);

    if (html[endIndex] === "<") {
      if (html[endIndex + 1] === "/") {
        // "<div>foobar</div>"
        //             ^
        while (endIndex < html.length && html[endIndex] !== ">") {
          endIndex++;
        }
        indent--;
        addText(endIndex + 1);
      } else {
        // "<div>foobar</div>"
        //  ^
        while (endIndex < html.length && html[endIndex] !== ">") {
          endIndex++;
        }
        // "<div>foobar</div>"
        //      ^
        addText(endIndex + 1);
        indent++;
      }
    }
  }

  return result.trim();
}

/**
 * Recursively transforms all child nodes to have uppercased text.
 *
 * @param {Node} node
 */
function upperCaseNode(node) {
  if (typeof node.nodeValue === "string") {
    node.nodeValue = node.nodeValue.toUpperCase();
  }
  for (const childNode of node.childNodes) {
    upperCaseNode(childNode);
  }
}

/**
 * Test utility class for translations settings UI tests.
 * Provides methods for interacting with and asserting the state of
 * the translations settings page in about:preferences.
 */
class TranslationsSettingsTestUtils {
  /**
   * @param {Document} document - The settings document
   */
  constructor(document) {
    this.document = document;
  }

  async openTranslationsSubpageFromDocument() {
    const manageButton = await waitForCondition(
      () => this.document.getElementById("translationsManageButton"),
      "Waiting for translationsManageButton"
    );
    manageButton.scrollIntoView({ behavior: "instant", block: "center" });

    await this.assertEvents(
      {
        expected: [[TranslationsSettingsTestUtils.Events.Initialized]],
      },
      async () => {
        click(manageButton, "Open translations subpage");
      }
    );
  }

  /**
   * Opens the translations settings subpage and returns helpers.
   *
   * @param {Array} [lexicalShortlistPrefs]
   * @returns {Promise<{cleanup: Function, remoteClients: object, translationsSettingsTestUtils: TranslationsSettingsTestUtils}>}
   */
  static async openTranslationsSettingsSubpage(lexicalShortlistPrefs = []) {
    const { cleanup, remoteClients, translationsSettingsTestUtils } =
      await setupAboutPreferences(LANGUAGE_PAIRS, {
        prefs: [
          ["browser.settings-redesign.enabled", true],
          ...lexicalShortlistPrefs,
        ],
      });

    const document = gBrowser.selectedBrowser.contentDocument;
    const manageButton = await waitForCondition(
      () => document.getElementById("translationsManageButton"),
      "Waiting for translationsManageButton"
    );
    manageButton.scrollIntoView({ behavior: "instant", block: "center" });

    await translationsSettingsTestUtils.assertEvents(
      {
        expected: [[TranslationsSettingsTestUtils.Events.Initialized]],
      },
      async () => {
        click(manageButton, "Open translations subpage");
      }
    );

    return { cleanup, remoteClients, translationsSettingsTestUtils };
  }

  static getLanguageModelNames(langTag) {
    return languageModelNames([
      { fromLang: langTag, toLang: "en" },
      { fromLang: "en", toLang: langTag },
    ]);
  }

  /**
   * Returns origins sorted alphabetically while ignoring schemes.
   *
   * @param {string[]} origins
   * @returns {string[]}
   */
  static sortOrigins(origins) {
    const stripScheme = origin =>
      origin.replace(/^[a-z][a-z0-9+.-]*:\/\//i, "");
    return [...origins].sort((originA, originB) =>
      stripScheme(originA).localeCompare(stripScheme(originB))
    );
  }

  /**
   * Static Events class for event name constants.
   */
  static Events = class Events {
    static AlwaysTranslateLanguagesRendered =
      "TranslationsSettingsTest:AlwaysTranslateLanguagesRendered";
    static NeverTranslateLanguagesRendered =
      "TranslationsSettingsTest:NeverTranslateLanguagesRendered";
    static NeverTranslateSitesRendered =
      "TranslationsSettingsTest:NeverTranslateSitesRendered";
    static DownloadedLanguagesRendered =
      "TranslationsSettingsTest:DownloadedLanguagesRendered";

    static AlwaysTranslateLanguagesEmptyStateShown =
      "TranslationsSettingsTest:AlwaysTranslateLanguagesEmptyStateShown";
    static AlwaysTranslateLanguagesEmptyStateHidden =
      "TranslationsSettingsTest:AlwaysTranslateLanguagesEmptyStateHidden";
    static NeverTranslateLanguagesEmptyStateShown =
      "TranslationsSettingsTest:NeverTranslateLanguagesEmptyStateShown";
    static NeverTranslateLanguagesEmptyStateHidden =
      "TranslationsSettingsTest:NeverTranslateLanguagesEmptyStateHidden";
    static NeverTranslateSitesEmptyStateShown =
      "TranslationsSettingsTest:NeverTranslateSitesEmptyStateShown";
    static NeverTranslateSitesEmptyStateHidden =
      "TranslationsSettingsTest:NeverTranslateSitesEmptyStateHidden";
    static DownloadedLanguagesEmptyStateShown =
      "TranslationsSettingsTest:DownloadedLanguagesEmptyStateShown";
    static DownloadedLanguagesEmptyStateHidden =
      "TranslationsSettingsTest:DownloadedLanguagesEmptyStateHidden";
    static AlwaysTranslateLanguagesAddButtonEnabled =
      "TranslationsSettingsTest:AlwaysTranslateLanguagesAddButtonEnabled";
    static AlwaysTranslateLanguagesAddButtonDisabled =
      "TranslationsSettingsTest:AlwaysTranslateLanguagesAddButtonDisabled";

    static AlwaysTranslateLanguagesSelectOptionsUpdated =
      "TranslationsSettingsTest:AlwaysTranslateLanguagesSelectOptionsUpdated";
    static NeverTranslateLanguagesSelectOptionsUpdated =
      "TranslationsSettingsTest:NeverTranslateLanguagesSelectOptionsUpdated";
    static DownloadedLanguagesSelectOptionsUpdated =
      "TranslationsSettingsTest:DownloadedLanguagesSelectOptionsUpdated";
    static NeverTranslateLanguagesAddButtonEnabled =
      "TranslationsSettingsTest:NeverTranslateLanguagesAddButtonEnabled";
    static NeverTranslateLanguagesAddButtonDisabled =
      "TranslationsSettingsTest:NeverTranslateLanguagesAddButtonDisabled";

    static DownloadStarted = "TranslationsSettingsTest:DownloadStarted";
    static DownloadProgress = "TranslationsSettingsTest:DownloadProgress";
    static DownloadCompleted = "TranslationsSettingsTest:DownloadCompleted";
    static DownloadFailed = "TranslationsSettingsTest:DownloadFailed";
    static DownloadDeleted = "TranslationsSettingsTest:DownloadDeleted";

    static Initialized = "TranslationsSettingsTest:Initialized";
    static InitializationFailed =
      "TranslationsSettingsTest:InitializationFailed";

    static DownloadLanguageButtonEnabled =
      "TranslationsSettingsTest:DownloadLanguageButtonEnabled";
    static DownloadLanguageButtonDisabled =
      "TranslationsSettingsTest:DownloadLanguageButtonDisabled";
  };

  /**
   * Waits for a translations settings event to be dispatched.
   *
   * @param {string} eventName - The event name to wait for
   * @param {object} options
   * @param {object} [options.expectedDetail] - Expected detail properties
   * @returns {Promise<CustomEvent>}
   */
  async waitForEvent(eventName, options = {}) {
    const { expectedDetail } = options;

    return BrowserTestUtils.waitForEvent(
      this.document,
      eventName,
      false,
      event => {
        if (expectedDetail) {
          for (const key of Object.keys(expectedDetail)) {
            const actual = event.detail?.[key];
            const expected = expectedDetail[key];
            if (JSON.stringify(actual) !== JSON.stringify(expected)) {
              return false;
            }
          }
        }
        return true;
      }
    );
  }

  /**
   * Asserts that specific events occur (or don't occur) during an action.
   *
   * @param {object} assertions
   * @param {Array<[string, object?]>} assertions.expected - Events that must occur
   * @param {Array<string>} [assertions.unexpected] - Events that must not occur
   * @param {number} [assertions.timeout=10000] - Timeout in milliseconds
   * @param {Function} callback - The action to perform
   * @returns {Promise<void>}
   */
  async assertEvents(
    { expected = [], unexpected = [], timeout = 10000 },
    callback
  ) {
    const firedEvents = [];
    const unexpectedEventsFired = [];

    const handlers = new Map();

    const isInitializedFlagSet =
      this.document?.defaultView?.wrappedJSObject?.TranslationsSettings
        ?.initialized;

    const preseedEventsIfAlreadySatisfied = () => {
      for (const [eventName] of expected) {
        if (
          eventName === TranslationsSettingsTestUtils.Events.Initialized &&
          isInitializedFlagSet &&
          !firedEvents.some(([name]) => name === eventName)
        ) {
          firedEvents.push([eventName, null]);
        }
      }
    };

    preseedEventsIfAlreadySatisfied();

    const maybeAddSyntheticInitializationEvent = () => {
      if (
        expected.some(
          ([name]) => name === TranslationsSettingsTestUtils.Events.Initialized
        ) &&
        !firedEvents.some(
          ([name]) => name === TranslationsSettingsTestUtils.Events.Initialized
        ) &&
        this.document?.defaultView?.wrappedJSObject?.TranslationsSettings
          ?.initialized
      ) {
        firedEvents.push([
          TranslationsSettingsTestUtils.Events.Initialized,
          null,
        ]);
      }
    };

    for (const [eventName] of expected) {
      const handler = event => {
        firedEvents.push([eventName, event.detail]);
      };
      handlers.set(eventName, handler);
      this.document.addEventListener(eventName, handler);
    }

    for (const eventName of unexpected) {
      const handler = event => {
        unexpectedEventsFired.push([eventName, event.detail]);
      };
      handlers.set(eventName, handler);
      this.document.addEventListener(eventName, handler);
    }

    try {
      await callback();

      maybeAddSyntheticInitializationEvent();
      preseedEventsIfAlreadySatisfied();

      const interval = 100;
      const maxTries = Math.ceil(timeout / interval);
      const expectedEventNames = expected.map(([name]) => name).join(", ");
      try {
        await TestUtils.waitForCondition(
          () => {
            maybeAddSyntheticInitializationEvent();
            return firedEvents.length >= expected.length;
          },
          `Waiting for ${expected.length} expected event(s): ${expectedEventNames}`,
          interval,
          maxTries
        );
      } catch (error) {
        throw new Error(
          error?.message ??
            error ??
            `Timed out waiting for expected event(s): ${expectedEventNames}`
        );
      }

      for (let i = 0; i < expected.length; i++) {
        const [expectedEventName, expectedDetail] = expected[i];
        const [firedEventName, firedDetail] = firedEvents[i] || [];

        is(
          firedEventName,
          expectedEventName,
          `Expected event ${i}: ${expectedEventName}`
        );

        if (expectedDetail) {
          for (const key of Object.keys(expectedDetail)) {
            Assert.deepEqual(
              firedDetail?.[key],
              expectedDetail[key],
              `Event ${expectedEventName} detail.${key} matches`
            );
          }
        }
      }

      const unexpectedNames = unexpectedEventsFired
        .map(([name]) => name)
        .join(", ");
      is(
        unexpectedEventsFired.length,
        0,
        `No unexpected events should fire. Fired: ${unexpectedNames}`
      );
    } finally {
      for (const [eventName, handler] of handlers.entries()) {
        this.document.removeEventListener(eventName, handler);
      }
    }
  }

  /**
   * Gets the translations setting pane element.
   *
   * @returns {HTMLElement|null}
   */
  getTranslationsPane() {
    return this.document.querySelector(
      'setting-pane[data-category="paneTranslations"]'
    );
  }

  /**
   * Gets the translations subpage back button element.
   *
   * @returns {HTMLElement|null}
   */
  getBackButton() {
    return this.getTranslationsPane()?.pageHeaderEl?.backButtonEl ?? null;
  }

  /**
   * Clicks the translations subpage back button and waits for the main pane.
   *
   * @returns {Promise<void>}
   */
  async clickBackButton() {
    const pane = this.getTranslationsPane();
    if (!pane) {
      throw new Error("Translations pane not found");
    }

    if (pane.getUpdateComplete) {
      await pane.getUpdateComplete();
    }

    const backButton = pane.pageHeaderEl?.backButtonEl;
    if (!backButton) {
      throw new Error("Translations back button not found");
    }

    const paneShown = BrowserTestUtils.waitForEvent(
      this.document,
      "paneshown",
      event => event.detail?.category === "paneGeneral"
    );

    await click(backButton, "Navigate back to main settings");
    await paneShown;

    await TestUtils.waitForCondition(
      () => pane.hidden,
      "Waiting for translations pane to hide"
    );
  }

  /**
   * Gets the always-translate languages select element.
   *
   * @returns {HTMLSelectElement|null}
   */
  getAlwaysTranslateLanguagesSelect() {
    return this.document.getElementById(
      "translationsAlwaysTranslateLanguagesSelect"
    );
  }

  /**
   * Gets the always-translate languages add button.
   *
   * @returns {HTMLButtonElement|null}
   */
  getAlwaysTranslateLanguagesAddButton() {
    return this.document.getElementById(
      "translationsAlwaysTranslateLanguagesButton"
    );
  }

  /**
   * Gets the never-translate languages select element.
   *
   * @returns {HTMLSelectElement|null}
   */
  getNeverTranslateLanguagesSelect() {
    return this.document.getElementById(
      "translationsNeverTranslateLanguagesSelect"
    );
  }

  /**
   * Gets the never-translate languages add button.
   *
   * @returns {HTMLButtonElement|null}
   */
  getNeverTranslateLanguagesAddButton() {
    return this.document.getElementById(
      "translationsNeverTranslateLanguagesButton"
    );
  }

  /**
   * Gets the download languages select element.
   *
   * @returns {HTMLSelectElement|null}
   */
  getDownloadedLanguagesSelect() {
    return this.document.getElementById("translationsDownloadLanguagesSelect");
  }

  getSelectedDownloadLanguage() {
    return this.getDownloadedLanguagesSelect()?.value ?? "";
  }

  /**
   * Gets the download button element.
   *
   * @returns {HTMLButtonElement|null}
   */
  getDownloadLanguageButton() {
    return this.document.getElementById("translationsDownloadLanguagesButton");
  }

  /**
   * Gets the download languages group element.
   *
   * @returns {HTMLElement|null}
   */
  getDownloadedLanguagesGroup() {
    return this.document.getElementById("translationsDownloadLanguagesGroup");
  }

  /**
   * Selects a language in the download dropdown.
   *
   * @param {string} langTag
   */
  async selectDownloadLanguage(langTag) {
    const dropdown = this.getDownloadedLanguagesSelect();
    dropdown.value = langTag;
    dropdown.dispatchEvent(new Event("change", { bubbles: true }));
  }

  async downloadLanguage({
    langTag,
    remoteClients,
    inProgressLanguages,
    finalLanguages,
  }) {
    await this.selectDownloadLanguage(langTag);

    const started = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadStarted,
      { expectedDetail: { langTag } }
    );
    const renderInProgress = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadedLanguagesRendered,
      {
        expectedDetail: {
          languages: inProgressLanguages,
          count: inProgressLanguages.length,
          downloading: [langTag],
        },
      }
    );
    const optionsUpdated = this.waitForEvent(
      TranslationsSettingsTestUtils.Events
        .DownloadedLanguagesSelectOptionsUpdated
    );

    await click(this.getDownloadLanguageButton(), `Start ${langTag} download`);
    await Promise.all([started, renderInProgress, optionsUpdated]);

    const completed = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadCompleted,
      { expectedDetail: { langTag } }
    );
    const renderComplete = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadedLanguagesRendered,
      {
        expectedDetail: {
          languages: finalLanguages,
          count: finalLanguages.length,
          downloading: [],
        },
      }
    );
    const optionsAfter = this.waitForEvent(
      TranslationsSettingsTestUtils.Events
        .DownloadedLanguagesSelectOptionsUpdated
    );

    await remoteClients.translationModels.resolvePendingDownloads(
      TranslationsSettingsTestUtils.getLanguageModelNames(langTag).length
    );
    await Promise.all([completed, renderComplete, optionsAfter]);
  }

  /**
   * Starts a download expected to fail and waits for the failure state.
   *
   * @param {object} options
   * @param {string} options.langTag
   * @param {object} options.remoteClients
   * @param {string[]} [options.inProgressLanguages]
   * @param {string[]} [options.failedLanguages]
   */
  async startDownloadFailure({
    langTag,
    remoteClients,
    inProgressLanguages = [langTag],
    failedLanguages = [langTag],
  }) {
    await this.selectDownloadLanguage(langTag);

    const started = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadStarted,
      { expectedDetail: { langTag } }
    );
    const renderInProgress = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadedLanguagesRendered,
      {
        expectedDetail: {
          languages: inProgressLanguages,
          count: inProgressLanguages.length,
          downloading: [langTag],
        },
      }
    );
    const optionsUpdated = this.waitForEvent(
      TranslationsSettingsTestUtils.Events
        .DownloadedLanguagesSelectOptionsUpdated
    );

    await click(
      this.getDownloadLanguageButton(),
      `Start ${langTag} download (expect failure)`
    );
    await Promise.all([started, renderInProgress, optionsUpdated]);

    const spinnerButton = this.getDownloadRemoveButton(langTag);
    ok(spinnerButton, "Spinner button should be visible while downloading");
    is(
      spinnerButton.getAttribute("type"),
      "icon ghost",
      "Spinner button should use ghost styling while downloading"
    );

    const failed = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadFailed,
      { expectedDetail: { langTag } }
    );
    const renderFailed = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadedLanguagesRendered,
      {
        expectedDetail: {
          languages: failedLanguages,
          count: failedLanguages.length,
          downloading: [],
        },
      }
    );
    const optionsAfterFail = this.waitForEvent(
      TranslationsSettingsTestUtils.Events
        .DownloadedLanguagesSelectOptionsUpdated
    );

    const modelNames =
      TranslationsSettingsTestUtils.getLanguageModelNames(langTag);
    await remoteClients.translationModels.waitForPendingDownloads(
      modelNames.length
    );
    await remoteClients.translationModels.rejectPendingDownloads(
      modelNames.length
    );
    await Promise.all([failed, renderFailed, optionsAfterFail]);
  }

  /**
   * Waits for a language to appear in the download languages list.
   *
   * @param {string} langTag
   * @returns {Promise<Element>}
   */
  async waitForDownloadedLanguageItem(langTag) {
    return waitForCondition(
      () =>
        this.document.querySelector(
          `.translations-download-language-item[data-lang-tag="${langTag}"]`
        ),
      `Waiting for downloaded language item: ${langTag}`
    );
  }

  /**
   * Asserts the current state of the downloaded languages list.
   *
   * @param {object} expected
   * @param {string[]} [expected.languages] - Expected language tags
   * @param {string[]} [expected.downloading] - Expected language tags that are downloading
   * @param {number} [expected.count] - Expected count of languages
   * @returns {Promise<void>}
   */
  async assertDownloadedLanguages({ languages, downloading, count }) {
    const items = this.document.querySelectorAll(
      ".translations-download-language-item"
    );

    if (count !== undefined) {
      is(items.length, count, `Should have ${count} downloaded language(s)`);
    }

    const langTags = Array.from(items).map(item => item.dataset.langTag);

    if (languages) {
      Assert.deepEqual(
        langTags.sort(),
        [...languages].sort(),
        "Downloaded languages match"
      );
    }

    if (downloading) {
      const downloadingLangs = Array.from(items)
        .filter(item =>
          item
            .querySelector(".translations-download-remove-button")
            ?.hasAttribute("disabled")
        )
        .map(item => item.dataset.langTag);
      Assert.deepEqual(
        downloadingLangs.sort(),
        [...downloading].sort(),
        "Downloading languages match"
      );
    }
  }

  /**
   * Asserts the current order of the downloaded languages list.
   *
   * @param {object} expected
   * @param {string[]} expected.languages - Expected language tags in order
   * @returns {Promise<void>}
   */
  async assertDownloadedLanguagesOrder({ languages }) {
    const items = this.document.querySelectorAll(
      ".translations-download-language-item"
    );
    const actualLanguages = Array.from(items).map(item => item.dataset.langTag);
    Assert.deepEqual(
      actualLanguages,
      languages,
      "Downloaded languages order matches"
    );
  }

  /**
   * Asserts the visibility state of the downloaded languages empty state.
   *
   * @param {object} expected
   * @param {boolean} expected.visible - Whether empty state should be visible
   * @returns {Promise<void>}
   */
  async assertDownloadedLanguagesEmptyState({ visible }) {
    const emptyRow = this.document.getElementById(
      "translationsDownloadLanguagesNoneRow"
    );
    if (visible) {
      ok(
        emptyRow && !emptyRow.hidden,
        "Downloaded languages empty state should be visible"
      );
    } else {
      ok(
        !emptyRow || emptyRow.hidden,
        "Downloaded languages empty state should be hidden"
      );
    }
  }

  /**
   * Removes a language from the downloaded languages list.
   *
   * @param {string} langTag
   * @returns {Promise<void>}
   */
  async removeDownloadedLanguage(langTag) {
    const removeButton = await waitForCondition(
      () => this.getDownloadDeleteIconButton(langTag),
      `Waiting for download delete icon button for ${langTag}`
    );
    removeButton.click();
    await waitForCondition(
      () => this.getDownloadDeleteConfirmButton(langTag),
      `Waiting for delete confirmation for ${langTag}`
    );
  }

  getDownloadRemoveButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-remove-button`
    );
  }

  getDownloadDeleteConfirmButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-delete-confirm-button`
    );
  }

  getDownloadDeleteCancelButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-delete-cancel-button`
    );
  }

  getDownloadRetryButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-retry-button`
    );
  }

  getDownloadErrorButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-remove-button[iconsrc*="error"]`
    );
  }

  getDownloadWarningButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-remove-button[iconsrc*="warning"]`
    );
  }

  getDownloadDeleteIconButton(langTag) {
    return this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-download-remove-button[iconsrc*="delete"]`
    );
  }

  async openDownloadDeleteConfirmation(langTag) {
    const removeButton = await waitForCondition(
      () => this.getDownloadDeleteIconButton(langTag),
      `Waiting for download delete icon button for ${langTag}`
    );
    removeButton.click();
    await waitForCondition(
      () => this.getDownloadDeleteConfirmButton(langTag),
      `Waiting for delete confirmation for ${langTag}`
    );
  }

  async cancelDownloadDelete(langTag) {
    const cancelButton = await waitForCondition(
      () => this.getDownloadDeleteCancelButton(langTag),
      `Waiting for delete cancel button for ${langTag}`
    );
    cancelButton.click();
    await waitForCondition(
      () => this.getDownloadDeleteIconButton(langTag),
      `Waiting for delete icon button to return for ${langTag}`
    );
  }

  async confirmDownloadDelete(langTag) {
    const confirmButton = await waitForCondition(
      () => this.getDownloadDeleteConfirmButton(langTag),
      `Waiting for delete confirm button for ${langTag}`
    );
    const deleted = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadDeleted,
      { expectedDetail: { langTag } }
    );
    confirmButton.click();
    await deleted;
  }

  async clickDownloadRetry(langTag) {
    const retryButton = await waitForCondition(
      () => this.getDownloadRetryButton(langTag),
      `Waiting for retry button for ${langTag}`
    );
    const started = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.DownloadStarted,
      { expectedDetail: { langTag } }
    );
    retryButton.click();
    await started;
  }

  /**
   * Adds a language to the always-translate list.
   *
   * @param {string} langTag
   * @returns {Promise<void>}
   */
  async addAlwaysTranslateLanguage(langTag) {
    const dropdown = this.getAlwaysTranslateLanguagesSelect();
    dropdown.value = langTag;
    dropdown.dispatchEvent(new Event("change", { bubbles: true }));

    const addButton = await waitForCondition(
      () => this.getAlwaysTranslateLanguagesAddButton(),
      "Waiting for always-translate add button"
    );
    if (addButton.disabled) {
      const addButtonEnabled = this.waitForEvent(
        TranslationsSettingsTestUtils.Events
          .AlwaysTranslateLanguagesAddButtonEnabled
      );
      await addButtonEnabled;
    }
    addButton.click();

    const addedLanguage = this.waitForAlwaysTranslateLanguageItem(langTag);
    const addButtonDisabledPromise = addButton.disabled
      ? Promise.resolve()
      : this.waitForEvent(
          TranslationsSettingsTestUtils.Events
            .AlwaysTranslateLanguagesAddButtonDisabled
        );
    await Promise.all([addedLanguage, addButtonDisabledPromise]);
  }

  /**
   * Removes a language from the always-translate list.
   *
   * @param {string} langTag
   * @returns {Promise<void>}
   */
  async removeAlwaysTranslateLanguage(langTag) {
    const removeButton = this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-always-translate-remove-button`
    );
    if (!removeButton) {
      throw new Error(`Remove button not found for language: ${langTag}`);
    }
    const rendered = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.AlwaysTranslateLanguagesRendered
    );
    removeButton.click();
    await rendered;
  }

  /**
   * Waits for a language to appear in the always-translate languages list.
   *
   * @param {string} langTag
   * @returns {Promise<Element>}
   */
  async waitForAlwaysTranslateLanguageItem(langTag) {
    return TestUtils.waitForCondition(
      () =>
        this.document.querySelector(
          `[data-lang-tag="${langTag}"].translations-always-translate-language-item`
        ),
      `Waiting for always-translate language item: ${langTag}`
    );
  }

  /**
   * Asserts the current state of the always-translate languages list.
   *
   * @param {object} expected
   * @param {string[]} [expected.languages] - Expected language tags
   * @param {number} [expected.count] - Expected count of languages
   * @returns {Promise<void>}
   */
  async assertAlwaysTranslateLanguages({ languages, count }) {
    const items = this.document.querySelectorAll(
      ".translations-always-translate-language-item"
    );

    if (count !== undefined) {
      is(
        items.length,
        count,
        `Should have ${count} always-translate language(s)`
      );
    }

    if (languages) {
      const actualLanguages = Array.from(items).map(
        item => item.dataset.langTag
      );
      Assert.deepEqual(
        actualLanguages.sort(),
        [...languages].sort(),
        "Always-translate languages match"
      );
    }
  }

  /**
   * Asserts the current order of the always-translate languages list.
   *
   * @param {object} expected
   * @param {string[]} expected.languages - Expected language tags in order
   * @returns {Promise<void>}
   */
  async assertAlwaysTranslateLanguagesOrder({ languages }) {
    const items = this.document.querySelectorAll(
      ".translations-always-translate-language-item"
    );
    const actualLanguages = Array.from(items).map(item => item.dataset.langTag);
    Assert.deepEqual(
      actualLanguages,
      languages,
      "Always-translate languages order matches"
    );
  }

  /**
   * Asserts the visibility state of the always-translate languages empty state.
   *
   * @param {object} expected
   * @param {boolean} expected.visible - Whether empty state should be visible
   * @returns {Promise<void>}
   */
  async assertAlwaysTranslateLanguagesEmptyState({ visible }) {
    const emptyRow = this.document.getElementById(
      "translationsAlwaysTranslateLanguagesNoneRow"
    );
    if (visible) {
      ok(
        emptyRow && !emptyRow.hidden,
        "Always-translate languages empty state should be visible"
      );
    } else {
      ok(
        !emptyRow || emptyRow.hidden,
        "Always-translate languages empty state should be hidden"
      );
    }
  }

  /**
   * Adds a language to the never-translate list.
   *
   * @param {string} langTag
   * @returns {Promise<void>}
   */
  async addNeverTranslateLanguage(langTag) {
    const dropdown = this.getNeverTranslateLanguagesSelect();
    dropdown.value = langTag;
    dropdown.dispatchEvent(new Event("change", { bubbles: true }));

    const addButton = await waitForCondition(
      () => this.getNeverTranslateLanguagesAddButton(),
      "Waiting for never-translate add button"
    );
    if (addButton.disabled) {
      const addButtonEnabled = this.waitForEvent(
        TranslationsSettingsTestUtils.Events
          .NeverTranslateLanguagesAddButtonEnabled
      );
      await addButtonEnabled;
    }
    addButton.click();

    const addedLanguage = this.waitForNeverTranslateLanguageItem(langTag);
    const addButtonDisabledPromise = addButton.disabled
      ? Promise.resolve()
      : this.waitForEvent(
          TranslationsSettingsTestUtils.Events
            .NeverTranslateLanguagesAddButtonDisabled
        );
    await Promise.all([addedLanguage, addButtonDisabledPromise]);
  }

  /**
   * Removes a language from the never-translate list.
   *
   * @param {string} langTag
   * @returns {Promise<void>}
   */
  async removeNeverTranslateLanguage(langTag) {
    const removeButton = this.document.querySelector(
      `[data-lang-tag="${langTag}"].translations-never-translate-remove-button`
    );
    if (!removeButton) {
      throw new Error(`Remove button not found for language: ${langTag}`);
    }
    const rendered = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.NeverTranslateLanguagesRendered
    );
    removeButton.click();
    await rendered;
  }

  /**
   * Waits for a language to appear in the never-translate languages list.
   *
   * @param {string} langTag
   * @returns {Promise<Element>}
   */
  async waitForNeverTranslateLanguageItem(langTag) {
    return TestUtils.waitForCondition(
      () =>
        this.document.querySelector(
          `[data-lang-tag="${langTag}"].translations-never-translate-language-item`
        ),
      `Waiting for never-translate language item: ${langTag}`
    );
  }

  /**
   * Asserts the current state of the never-translate languages list.
   *
   * @param {object} expected
   * @param {string[]} [expected.languages] - Expected language tags
   * @param {number} [expected.count] - Expected count of languages
   * @returns {Promise<void>}
   */
  async assertNeverTranslateLanguages({ languages, count }) {
    const items = this.document.querySelectorAll(
      ".translations-never-translate-language-item"
    );

    if (count !== undefined) {
      is(
        items.length,
        count,
        `Should have ${count} never-translate language(s)`
      );
    }

    if (languages) {
      const actualLanguages = Array.from(items).map(
        item => item.dataset.langTag
      );
      Assert.deepEqual(
        actualLanguages.sort(),
        [...languages].sort(),
        "Never-translate languages match"
      );
    }
  }

  /**
   * Asserts the current order of the never-translate languages list.
   *
   * @param {object} expected
   * @param {string[]} expected.languages - Expected language tags in order
   * @returns {Promise<void>}
   */
  async assertNeverTranslateLanguagesOrder({ languages }) {
    const items = this.document.querySelectorAll(
      ".translations-never-translate-language-item"
    );
    const actualLanguages = Array.from(items).map(item => item.dataset.langTag);
    Assert.deepEqual(
      actualLanguages,
      languages,
      "Never-translate languages order matches"
    );
  }

  /**
   * Asserts the visibility state of the never-translate languages empty state.
   *
   * @param {object} expected
   * @param {boolean} expected.visible - Whether empty state should be visible
   * @returns {Promise<void>}
   */
  async assertNeverTranslateLanguagesEmptyState({ visible }) {
    const emptyRow = this.document.getElementById(
      "translationsNeverTranslateLanguagesNoneRow"
    );
    if (visible) {
      ok(
        emptyRow && !emptyRow.hidden,
        "Never-translate languages empty state should be visible"
      );
    } else {
      ok(
        !emptyRow || emptyRow.hidden,
        "Never-translate languages empty state should be hidden"
      );
    }
  }

  /**
   * Gets the never-translate sites list element.
   *
   * @returns {HTMLElement|null}
   */
  getNeverTranslateSitesGroup() {
    return this.document.getElementById("translationsNeverTranslateSitesGroup");
  }

  /**
   * Waits for a site to appear in the never-translate sites list.
   *
   * @param {string} origin
   * @returns {Promise<Element>}
   */
  async waitForNeverTranslateSiteItem(origin) {
    return waitForCondition(
      () =>
        this.document.querySelector(
          `[data-origin="${origin}"].translations-never-translate-site-item`
        ),
      `Waiting for never-translate site item: ${origin}`
    );
  }

  /**
   * Removes a site from the never-translate list.
   *
   * @param {string} origin
   * @returns {Promise<void>}
   */
  async removeNeverTranslateSite(origin) {
    const removeButton = await waitForCondition(
      () =>
        this.document.querySelector(
          `[data-origin="${origin}"].translations-never-translate-site-remove-button`
        ),
      `Waiting for remove button for ${origin}`
    );
    const rendered = this.waitForEvent(
      TranslationsSettingsTestUtils.Events.NeverTranslateSitesRendered
    );
    removeButton.click();
    await rendered;
  }

  /**
   * Asserts the current state of the never-translate sites list.
   *
   * @param {object} expected
   * @param {string[]} [expected.sites] - Expected site origins
   * @param {number} [expected.count] - Expected count of sites
   * @returns {Promise<void>}
   */
  async assertNeverTranslateSites({ sites, count }) {
    const items = this.document.querySelectorAll(
      ".translations-never-translate-site-item"
    );

    if (count !== undefined) {
      is(items.length, count, `Should have ${count} never-translate site(s)`);
    }

    if (sites) {
      const actualSites = Array.from(items).map(item => item.dataset.origin);
      Assert.deepEqual(
        actualSites.sort(),
        [...sites].sort(),
        "Never-translate sites match"
      );
    }
  }

  /**
   * Asserts the current order of the never-translate sites list.
   *
   * @param {object} expected
   * @param {string[]} expected.sites - Expected site origins in order
   * @returns {Promise<void>}
   */
  async assertNeverTranslateSitesOrder({ sites }) {
    const items = this.document.querySelectorAll(
      ".translations-never-translate-site-item"
    );
    const actualSites = Array.from(items).map(item => item.dataset.origin);
    Assert.deepEqual(actualSites, sites, "Never-translate sites order matches");
  }

  /**
   * Asserts the visibility state of the never-translate sites empty state.
   *
   * @param {object} expected
   * @param {boolean} expected.visible - Whether empty state should be visible
   * @returns {Promise<void>}
   */
  async assertNeverTranslateSitesEmptyState({ visible }) {
    const emptyRow = this.document.getElementById(
      "translationsNeverTranslateSitesNoneRow"
    );
    if (visible) {
      ok(
        emptyRow && !emptyRow.hidden,
        "Never-translate sites empty state should be visible"
      );
    } else {
      ok(
        !emptyRow || emptyRow.hidden,
        "Never-translate sites empty state should be hidden"
      );
    }
  }
}

/**
 * Recursively transforms all child nodes to have diacriticized text. This is useful
 * to spot multiple translations.
 *
 * @param {Node} node
 */
function diacriticizeNode(node) {
  if (typeof node.nodeValue === "string") {
    let result = "";
    for (let i = 0; i < node.nodeValue.length; i++) {
      const ch = node.nodeValue[i];
      result += ch;
      if ("abcdefghijklmnopqrstuvwxyz".includes(ch.toLowerCase())) {
        result += "\u0305";
      }
    }
    node.nodeValue = result;
  }
  for (const childNode of node.childNodes) {
    diacriticizeNode(childNode);
  }
}

/**
 * Creates a mocked message port for translations.
 *
 * @returns {MessagePort} This is mocked
 */
function createMockedTranslatorPort(transformNode = upperCaseNode, delay = 0) {
  const parser = new DOMParser();
  const mockedPort = {
    async postMessage(message) {
      // Make this response async.
      await TestUtils.waitForTick();

      switch (message.type) {
        case "TranslationsPort:GetEngineStatusRequest": {
          mockedPort.onmessage({
            data: {
              type: "TranslationsPort:GetEngineStatusResponse",
              status: "ready",
            },
          });
          break;
        }
        case "TranslationsPort:Passthrough": {
          const { translationId } = message;

          mockedPort.onmessage({
            data: {
              type: "TranslationsPort:TranslationResponse",
              translationId,
              targetText: null,
            },
          });

          break;
        }
        case "TranslationsPort:CachedTranslation": {
          const { cachedTranslation, translationId } = message;

          mockedPort.onmessage({
            data: {
              type: "TranslationsPort:TranslationResponse",
              translationId,
              targetText: cachedTranslation,
            },
          });

          break;
        }
        case "TranslationsPort:TranslationRequest": {
          const { translationId, sourceText } = message;

          const translatedDoc = parser.parseFromString(sourceText, "text/html");
          transformNode(translatedDoc.body);
          if (delay) {
            await new Promise(resolve => setTimeout(resolve, delay));
          }
          mockedPort.onmessage({
            data: {
              type: "TranslationsPort:TranslationResponse",
              targetText: translatedDoc.body.innerHTML,
              translationId,
            },
          });
          break;
        }
        default: {
          throw new Error("Unexpected mock translator message:", message.type);
        }
      }
    },
  };
  return mockedPort;
}
class TranslationResolver {
  resolvers = Promise.withResolvers();
  resolveCount = 0;
  getPromise() {
    return this.resolvers.promise;
  }
}

/**
 * Creates a mocked message port for translations.
 *
 * @returns {MessagePort} This is mocked
 */
function createControlledTranslatorPort() {
  const parser = new DOMParser();

  const canceledTranslations = new Set();

  let resolvers = [];

  let engineStatusCount = 0;
  let cancelCount = 0;
  let passthroughCount = 0;
  let cachedCount = 0;
  let requestCount = 0;

  function resolveRequests() {
    const resolvedCount = resolvers.length;

    let resolver = resolvers.pop();
    while (resolver) {
      let { translationId, resolve, debugText } = resolver;
      info(`Resolving promise for request (id:${translationId}): ${debugText}`);
      resolve();

      resolver = resolvers.pop();
    }

    return resolvedCount;
  }

  function resetPortData() {
    if (resolveRequests() > 0) {
      throw new Error(
        "Attempt to collect port data with pending translation requests."
      );
    }

    engineStatusCount = 0;
    cancelCount = 0;
    passthroughCount = 0;
    cachedCount = 0;
    requestCount = 0;
  }

  function collectPortData(resetCounters = true) {
    info("Collecting data from port messages");
    const portData = {
      engineStatusCount,
      cancelCount,
      passthroughCount,
      cachedCount,
      requestCount,
    };

    if (resetCounters) {
      resetPortData();
    }

    return portData;
  }

  const mockedTranslatorPort = {
    async postMessage(message) {
      switch (message.type) {
        case "TranslationsPort:GetEngineStatusRequest": {
          engineStatusCount++;

          mockedTranslatorPort.onmessage({
            data: {
              type: "TranslationsPort:GetEngineStatusResponse",
              status: "ready",
            },
          });
          break;
        }
        case "TranslationsPort:CancelSingleTranslation": {
          cancelCount++;

          info("Canceling translation id:" + message.translationId);
          canceledTranslations.add(message.translationId);
          break;
        }
        case "TranslationsPort:Passthrough": {
          passthroughCount++;

          const { translationId } = message;

          // Create a short debug version of the text.
          let debugText = null;

          info(
            `Translation requested for (id:${translationId}): "${debugText}"`
          );

          const { promise, resolve } = Promise.withResolvers();

          resolvers.push({ translationId, resolve, debugText });

          info(
            `Waiting for promise for (id:${translationId}) to resolve: "${debugText}`
          );

          await promise;

          info(`Promise for (id:${translationId}) resolved: "${debugText}`);

          mockedTranslatorPort.onmessage({
            data: {
              type: "TranslationsPort:TranslationResponse",
              translationId,
              targetText: null,
            },
          });

          break;
        }
        case "TranslationsPort:CachedTranslation": {
          cachedCount++;

          const { cachedTranslation, translationId } = message;

          // Create a short debug version of the text.
          let debugText = cachedTranslation.trim().replaceAll("\n", "");
          if (debugText.length > 50) {
            debugText = debugText.slice(0, 50) + "...";
          }

          info(
            `Translation requested for (id:${translationId}): "${debugText}"`
          );

          const { promise, resolve } = Promise.withResolvers();

          resolvers.push({ translationId, resolve, debugText });

          info(
            `Waiting for promise for (id:${translationId}) to resolve: "${debugText}`
          );

          await promise;

          info(`Promise for (id:${translationId}) resolved: "${debugText}`);

          mockedTranslatorPort.onmessage({
            data: {
              type: "TranslationsPort:TranslationResponse",
              translationId,
              targetText: cachedTranslation,
            },
          });

          break;
        }
        case "TranslationsPort:TranslationRequest": {
          requestCount++;

          const { translationId, sourceText } = message;

          // Create a short debug version of the text.
          let debugText = sourceText.trim().replaceAll("\n", "");
          if (debugText.length > 50) {
            debugText = debugText.slice(0, 50) + "...";
          }

          info(
            `Translation requested for (id:${translationId}): "${debugText}"`
          );

          const { promise, resolve } = Promise.withResolvers();

          resolvers.push({ translationId, resolve, debugText });

          info(
            `Waiting for promise for (id:${translationId}) to resolve: "${debugText}`
          );

          await promise;

          info(`Promise for (id:${translationId}) resolved: "${debugText}`);

          if (canceledTranslations.has(translationId)) {
            info(`Cancelled translation for request (id:${translationId})`);
          } else {
            info(`Translation completed for request (id:${translationId})`);

            const translatedDoc = parser.parseFromString(
              sourceText,
              "text/html"
            );

            diacriticizeNode(translatedDoc.body);
            const targetText =
              translatedDoc.body.innerHTML.trim() + ` (id:${translationId})`;

            info("Translation response: " + targetText.replaceAll("\n", ""));
            mockedTranslatorPort.onmessage({
              data: {
                type: "TranslationsPort:TranslationResponse",
                targetText,
                translationId,
              },
            });
          }
        }
      }
    },
  };

  return { mockedTranslatorPort, resolveRequests, collectPortData };
}

/**
 * @type {typeof import("../../content/translations-document.sys.mjs")}
 */
const { TranslationsDocument, LRUCache } = ChromeUtils.importESModule(
  "chrome://global/content/translations/translations-document.sys.mjs"
);

/**
 * Creates a translated document from the provided HTML string.
 *
 * @param {string} html - The HTML source to translate.
 * @param {object} [options] - Optional configuration.
 * @param {string} [options.sourceLanguage="en"] - Source language code (default: "en").
 * @param {string} [options.targetLanguage="en"] - Target language code (default: "en").
 * @param {DOMParserSupportedType} [options.parserType="text/html"] - Parser type for the source content.
 * @param {(message: string) => Promise<string>} [options.mockedTranslatorPort] - Optional mock translation function.
 * @param {() => void} [options.mockedReportVisibleChange] - Optional callback for visibility reporting.
 * @returns {Promise<void>} Resolves when the document translation is complete.
 */
async function createTranslationsDoc(
  html,
  {
    sourceLanguage = "en",
    targetLanguage = "es",
    parserType = "text/html",
    mockedTranslatorPort,
    mockedReportVisibleChange,
  } = {}
) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.translations.enable", true],
      ["browser.translations.logLevel", "All"],
      [USE_LEXICAL_SHORTLIST_PREF, false],
    ],
  });

  const parser = new DOMParser();
  const document = parser.parseFromString(html, parserType);

  // For some reason, the document <body> here from the DOMParser is "display: flex" by
  // default. Ensure that it is "display: block" instead, otherwise the children of the
  // <body> will not be "display: inline".
  if (document.body) {
    document.body.style.display = "block";
  }

  let translationsDoc = null;

  const translate = () => {
    info("Creating the TranslationsDocument.");
    translationsDoc = new TranslationsDocument(
      document,
      sourceLanguage,
      targetLanguage,
      0, // This is a fake innerWindowID
      mockedTranslatorPort ?? createMockedTranslatorPort(),
      () => {
        throw new Error("Cannot request a new port");
      },
      mockedReportVisibleChange ?? (() => {}),
      new LRUCache(),
      false
    );

    translationsDoc.simulateIntersectionObservationForNonPendingNodes();
    return translationsDoc;
  };

  /**
   * Converts a string of expected HTML output into a regex that we can
   * use to match the actual HTML output.
   *
   * The expected HTML string may use double curly braces to escape a
   * {{ regex literal }} within the HTML itself, which will be preserved
   * in the final expression.
   *
   * For example, converts the HTML string:
   * `
   * <div>
   *   M̅u̅t̅a̅t̅i̅o̅n̅ 5 o̅n̅ e̅l̅e̅m̅e̅n̅t̅ (id:{{ [1-5] }})
   * </div>
   * `
   *
   * Into the following regex:
   *
   * /^\s*<div>\s*M̅u̅t̅a̅t̅i̅o̅n̅ 5 o̅n̅ e̅l̅e̅m̅e̅n̅t̅ \(id:[1-5]\)\s*<\/div>\s*$/su
   *
   * Which allows us to match the actual HTML to the expected HTML
   * regardless of whether the translation id was 1, 2, 3, 4, or 5.
   *
   * @param {string} html
   * @returns {RegExp}
   */
  function expectedHtmlToRegex(html) {
    // All characters that will need to be escaped with a backslash in the
    // final regex if they are contained within the HTML string.
    const ESCAPABLE_CHARACTERS = /[.*+?^${}()|[\]\\]/g;

    // Our own escape syntax to signify a {{ regex literal }} within the
    // HTML string that should be preserved in its original form.
    const REGEX_LITERAL = /\{\{(.*?)\}\}/gsu;

    // The same matcher as above, after escaping the curly braces with backslash.
    const ESCAPED_REGEX_LITERAL = /\\\{\\\{.*?\\\}\\\}/su;

    // Collect all regex literals that were escaped by using {{ literal }}
    // syntax into a single array. We will place them back in at the end.
    const regexLiterals = [...html.matchAll(REGEX_LITERAL)].map(
      match => match[1]
    );

    let pattern = html
      // Escape each character that needs it with a backslash.
      .replaceAll(ESCAPABLE_CHARACTERS, "\\$&")
      // Add a 0+ blank space matcher \s* before each opening angle bracket <
      .replaceAll(/\s*</g, "\\s*<")
      // Add a 0+ blank space matcher \s* after each closing angle bracket >
      .replaceAll(/>\s*/g, ">\\s*")
      // Collapse more than one blank space into a 1+ matcher
      .replaceAll(/\s\s+/g, "\\s+")
      // Replace a 1+ blank space matcher at the beginning with a 0+ matcher.
      .replace(/^\\s\+/, "\\s*")
      // Replace a 1+ blank space matcher at the end with a 0+ matcher.
      .replace(/\\s\+$/, "\\s*");

    // Go back through and replace each {{ regex literal }} that we preserved
    // at the start with its captured content.
    for (const regexLiteral of regexLiterals) {
      pattern = pattern.replace(ESCAPED_REGEX_LITERAL, regexLiteral.trim());
    }

    return new RegExp(`^${pattern}$`, "su");
  }

  /**
   * Test utility to check that the document matches the expected markup.
   * If `html` is a string, the prettified innerHTML must match exactly.
   * If `html` is a RegExp, the prettified innerHTML must satisfy the
   * regular expression.
   *
   * @param {string} message
   * @param {string} expectedHtml
   * @param {Document} [sourceDoc]
   * @param {() => void} [resolveRequests]
   */
  async function htmlMatches(
    message,
    expectedHtml,
    sourceDoc = document,
    resolveRequests
  ) {
    const prettyHtml = naivelyPrettify(expectedHtml);
    const expected = expectedHtmlToRegex(expectedHtml);

    let didSimulateIntersectionObservation = false;

    const getHTMLSource = () => {
      return (
        sourceDoc.body?.innerHTML ?? sourceDoc.documentElement?.outerHTML ?? ""
      );
    };

    try {
      await waitForCondition(async () => {
        await waitForCondition(
          () => !translationsDoc.hasPendingCallbackOnEventLoop()
        );

        while (
          translationsDoc.hasPendingCallbackOnEventLoop() ||
          translationsDoc.hasPendingTranslationRequests()
        ) {
          if (resolveRequests) {
            // Since resolveRequests is defined, we must manually resolve
            // them as the scheduler sends them until all are fulfilled.
            await waitForCondition(
              () =>
                resolveRequests() ||
                (!translationsDoc.hasPendingCallbackOnEventLoop() &&
                  !translationsDoc.hasPendingTranslationRequests()),
              "Manually resolving requests as they come in..."
            );
          } else {
            // Since resolveRequests is not defined, requests will resolve
            // automatically when the scheduler sends them. We simply have
            // to wait until they are all fulfilled.
            await waitForCondition(
              () =>
                !translationsDoc.hasPendingCallbackOnEventLoop() &&
                !translationsDoc.hasPendingTranslationRequests(),
              "Waiting for all requests to come in..."
            );
          }
        }

        await waitForCondition(
          () => !translationsDoc.hasPendingCallbackOnEventLoop()
        );

        const actualHtml = naivelyPrettify(getHTMLSource());
        const htmlMatches = expected.test(actualHtml);

        if (!htmlMatches && !didSimulateIntersectionObservation) {
          // If all of the requests have been resolved, and the HTML doesn't match,
          // then it may be because the request was never sent to the scheduler,
          // so we need to manually simulate intersection observation.
          //
          // This is a valid case, and not a bug. For example, if an attribute is mutated,
          // then it will not be scheduled for translation until it is observed.
          // However, we should never have to do this more than one time.
          didSimulateIntersectionObservation = true;
          translationsDoc.simulateIntersectionObservationForNonPendingNodes();
        }

        if (htmlMatches) {
          await waitForCondition(
            () =>
              !translationsDoc.hasPendingCallbackOnEventLoop() &&
              !translationsDoc.hasPendingTranslationRequests() &&
              !translationsDoc.isObservingAnyElementForContentIntersection() &&
              !translationsDoc.isObservingAnyElementForAttributeIntersection(),
            "Ensuring that the entire document is translated."
          );
        }

        return htmlMatches;
      }, "Waiting for HTML to match.");
      ok(true, message);
    } catch (error) {
      console.error(error);

      // Provide a nice error message.
      const actual = naivelyPrettify(getHTMLSource());
      ok(
        false,
        `${message}\n\nExpected HTML:\n\n${
          prettyHtml
        }\n\nActual HTML:\n\n${actual}\n\n${String(error)}`
      );
    }
  }

  function cleanup() {
    SpecialPowers.popPrefEnv();
  }

  return { htmlMatches, cleanup, translate, document };
}

/**
 * Perform a double requestAnimationFrame, which is used by the TranslationsDocument
 * to handle mutations.
 *
 * @param {Document} doc
 */
function doubleRaf(doc) {
  return new Promise(resolve => {
    doc.documentGlobal.requestAnimationFrame(() => {
      doc.documentGlobal.requestAnimationFrame(() => {
        resolve(
          // Wait for a tick to be after anything that resolves with a double rAF.
          TestUtils.waitForTick()
        );
      });
    });
  });
}

/**
 * This mocked translator reports on the batching of calls by replacing the text
 * with a letter. Each call of the function moves the letter forward alphabetically.
 *
 * So consecutive calls would transform things like:
 *   "First translation" -> "aaaa aaaaaaaaa"
 *   "Second translation" -> "bbbbb bbbbbbbbb"
 *   "Third translation" -> "cccc ccccccccc"
 *
 * This can visually show what the translation batching behavior looks like.
 *
 * @returns {MessagePort} A mocked port.
 */
function createBatchedMockedTranslatorPort() {
  let letter = "a";

  /**
   * @param {Node} node
   */
  function transformNode(node) {
    if (typeof node.nodeValue === "string") {
      node.nodeValue = node.nodeValue.replace(/\w/g, letter);
    }
    for (const childNode of node.childNodes) {
      transformNode(childNode);
    }
  }

  return createMockedTranslatorPort(node => {
    transformNode(node);
    letter = String.fromCodePoint(letter.codePointAt(0) + 1);
  });
}

/**
 * This mocked translator reorders Nodes to be in alphabetical order, and then
 * uppercases the text. This allows for testing the reordering behavior of the
 * translation engine.
 *
 * @returns {MessagePort} A mocked port.
 */
function createdReorderingMockedTranslatorPort() {
  /**
   * @param {Node} node
   */
  function transformNode(node) {
    if (typeof node.nodeValue === "string") {
      node.nodeValue = node.nodeValue.toUpperCase();
    }
    const nodes = [...node.childNodes];
    nodes.sort((a, b) =>
      (a.textContent?.trim() ?? "").localeCompare(b.textContent?.trim() ?? "")
    );
    for (const childNode of nodes) {
      childNode.remove();
    }
    for (const childNode of nodes) {
      // Re-append in sorted order.
      node.appendChild(childNode);
      transformNode(childNode);
    }
  }

  return createMockedTranslatorPort(transformNode);
}

/**
 * @returns {import("../../actors/TranslationsParent.sys.mjs").TranslationsParent}
 */
function getTranslationsParent(win = window) {
  return TranslationsParent.getTranslationsActor(win.gBrowser.selectedBrowser);
}

/**
 * Closes all open panels and menu popups related to Translations.
 *
 * @param {ChromeWindow} [win]
 */
async function closeAllOpenPanelsAndMenus(win) {
  await closeFullPagePanelSettingsMenuIfOpen(win);
  await closeFullPageTranslationsPanelIfOpen(win);
  await closeSelectPanelSettingsMenuIfOpen(win);
  await closeSelectTranslationsPanelIfOpen(win);
  await closeContextMenuIfOpen(win);
}

/**
 * Closes the popup element with the given Id if it is open.
 *
 * @param {string} popupElementId
 * @param {ChromeWindow} [win]
 */
async function closePopupIfOpen(popupElementId, win = window) {
  await waitForCondition(async () => {
    const popupElement = win.document.getElementById(popupElementId);
    if (!popupElement) {
      return true;
    }
    if (popupElement.state === "closed") {
      return true;
    }
    let popuphiddenPromise = BrowserTestUtils.waitForEvent(
      popupElement,
      "popuphidden"
    );
    popupElement.hidePopup();
    PanelMultiView.hidePopup(popupElement);
    await popuphiddenPromise;
    return false;
  });
}

/**
 * Closes the context menu if it is open.
 *
 * @param {ChromeWindow} [win]
 */
async function closeContextMenuIfOpen(win) {
  await closePopupIfOpen("contentAreaContextMenu", win);
}

/**
 * Closes the full-page translations panel settings menu if it is open.
 *
 * @param {ChromeWindow} [win]
 */
async function closeFullPagePanelSettingsMenuIfOpen(win) {
  await closePopupIfOpen(
    "full-page-translations-panel-settings-menupopup",
    win
  );
}

/**
 * Closes the select translations panel settings menu if it is open.
 *
 * @param {ChromeWindow} [win]
 */
async function closeSelectPanelSettingsMenuIfOpen(win) {
  await closePopupIfOpen("select-translations-panel-settings-menupopup", win);
}

/**
 * Closes the translations panel if it is open.
 *
 * @param {ChromeWindow} [win]
 */
async function closeFullPageTranslationsPanelIfOpen(win) {
  await closePopupIfOpen("full-page-translations-panel", win);
}

/**
 * Closes the translations panel if it is open.
 *
 * @param {ChromeWindow} [win]
 */
async function closeSelectTranslationsPanelIfOpen(win) {
  await closePopupIfOpen("select-translations-panel", win);
}

/**
 * This is for tests that don't need a browser page to run.
 */
async function setupActorTest({
  languagePairs,
  prefs,
  autoDownloadFromRemoteSettings = false,
}) {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.translations.enable", true],
      ["browser.translations.logLevel", "All"],
      [USE_LEXICAL_SHORTLIST_PREF, false],
      ...(prefs ?? []),
    ],
  });

  const { remoteClients, removeMocks } = await createAndMockRemoteSettings({
    languagePairs,
    autoDownloadFromRemoteSettings,
  });

  // Create a new tab so each test gets a new actor, and doesn't re-use the old one.
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    ENGLISH_PAGE_URL,
    true // waitForLoad
  );

  const actor = getTranslationsParent();
  return {
    actor,
    remoteClients,
    async cleanup() {
      await closeAllOpenPanelsAndMenus();
      await loadBlankPage();
      await EngineProcess.destroyTranslationsEngine();
      BrowserTestUtils.removeTab(tab);
      await removeMocks();
      TestTranslationsTelemetry.cleanup();
      return SpecialPowers.popPrefEnv();
    },
  };
}

/**
 * Creates and mocks remote settings for translations.
 *
 * @param {object} options - The options for creating and mocking remote settings.
 * @param {Array<{fromLang: string, toLang: string}>} [options.languagePairs=LANGUAGE_PAIRS]
 *  - The language pairs to be used.
 * @param {boolean} [options.useMockedTranslator=true]
 *  - Whether to use a mocked translator.
 * @param {boolean} [options.autoDownloadFromRemoteSettings=false]
 *  - Whether to automatically download from remote settings.
 *
 * @returns {Promise<object>} - An object containing the removeMocks function and remoteClients.
 */
async function createAndMockRemoteSettings({
  languagePairs = LANGUAGE_PAIRS,
  useMockedTranslator = true,
  autoDownloadFromRemoteSettings = false,
}) {
  if (TranslationsParent.isTranslationsEngineMocked()) {
    info("Attempt to mock the Translations Engine when it is already mocked.");
  }

  const remoteClients = {
    translationModels: await createTranslationModelsRemoteClient(
      autoDownloadFromRemoteSettings,
      languagePairs
    ),
    translationsWasm: await createTranslationsWasmRemoteClient(
      autoDownloadFromRemoteSettings
    ),
  };

  // The TranslationsParent will pull the language pair values from the JSON dump
  // of Remote Settings. Clear these before mocking the translations engine.
  TranslationsParent.clearCache();
  TranslationsPanelShared.clearLanguageListsCache();

  TranslationsParent.applyTestingMocks({
    useMockedTranslator,
    translationModelsRemoteClient: remoteClients.translationModels.client,
    translationsWasmRemoteClient: remoteClients.translationsWasm.client,
  });

  return {
    async removeMocks() {
      await remoteClients.translationModels.client.attachments.deleteAll();
      await remoteClients.translationModels.client.db.clear();
      await remoteClients.translationsWasm.client.db.clear();

      TranslationsParent.removeTestingMocks();
      TranslationsParent.clearCache();
      TranslationsPanelShared.clearLanguageListsCache();
    },
    remoteClients,
  };
}

/**
 * Normalizes the backslashes or forward slashes in the given path
 * to be correct for the current operating system.
 *
 * @param {string} path - The path to normalize.
 *
 * @returns {string} - The normalized path.
 */
function normalizePathForOS(path) {
  if (Services.appinfo.OS === "WINNT") {
    // On Windows, replace forward slashes with backslashes
    return path.replace(/\//g, "\\");
  }

  // On Unix-like systems, replace backslashes with forward slashes
  return path.replace(/\\/g, "/");
}

/**
 * Returns true if the given path exists, otherwise false.
 *
 * @param {string} path - The path to check.
 *
 * @returns {Promise<boolean>}
 */
async function pathExists(path) {
  try {
    return await IOUtils.exists(path);
  } catch (e) {
    return false;
  }
}

/**
 * Creates remote settings for the file system.
 *
 * @param {Array<{fromLang: string, toLang: string}>} languagePairs - The language pairs to be used.
 *
 * @returns {Promise<object>} - An object containing the removeMocks function and remoteClients.
 */
async function createFileSystemRemoteSettings(languagePairs, architecture) {
  const { removeMocks, remoteClients } = await createAndMockRemoteSettings({
    languagePairs,
    useMockedTranslator: false,
    autoDownloadFromRemoteSettings: true,
  });

  const artifactDirectory = normalizePathForOS(
    `${Services.env.get("MOZ_FETCHES_DIR")}`
  );

  if (!artifactDirectory) {
    await removeMocks();
    throw new Error(`

      🚨 The MOZ_FETCHES_DIR environment variable is not set 🚨

      If you are running a Translations end-to-end test locally, you will need to download the required artifacts to MOZ_FETCHES_DIR.
      To configure MOZ_FETCHES_DIR to run Translations end-to-end tests locally, please run the following script:

      ❯ python3 toolkit/components/translations/tests/scripts/download-translations-artifacts.py

    `);
  }

  if (!PathUtils.isAbsolute(artifactDirectory)) {
    await removeMocks();
    throw new Error(`
      The path exported to MOZ_FETCHES_DIR environment variable is a relative path.
      Please export an absolute path to MOZ_FETCHES_DIR.
    `);
  }

  const download = async record => {
    const recordPath = normalizePathForOS(
      record.name === "bergamot-translator"
        ? `${artifactDirectory}/${record.name}.zst`
        : `${artifactDirectory}/${architecture}.${record.name}.zst`
    );

    if (!(await pathExists(recordPath))) {
      throw new Error(`
        The record ${record.name} was not found in ${artifactDirectory} specified by MOZ_FETCHES_DIR at the expected path: ${recordPath}
        If you are running a Translations end-to-end test locally, you will need to download the required artifacts to MOZ_FETCHES_DIR.
        To configure MOZ_FETCHES_DIR to run Translations end-to-end tests locally, please run toolkit/components/translations/tests/scripts/download-translations-artifacts.py
      `);
    }

    const file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    file.initWithPath(recordPath);

    return {
      blob: await File.createFromNsIFile(file),
    };
  };

  remoteClients.translationsWasm.client.attachments.download = download;
  remoteClients.translationModels.client.attachments.download = download;

  return {
    removeMocks,
    remoteClients,
  };
}

/**
 * This class mocks the window's A11yUtils to count/capture arguments.
 *
 * This helps us ensure that the right calls are being made without
 * needing to handle whether the accessibility service is enabled in CI,
 * and also without needing to worry about if the call itself is broken
 * in the accessibility engine, since this is sometimes OS dependent.
 */
class MockedA11yUtils {
  /**
   * Holds the parameters passed to any calls to announce.
   *
   * @type {Array<{ raw: string, id: string}>}
   */
  static announceCalls = [];

  /**
   * Mocks the A11yUtils object for the given window, replacing the real A11yUtils with the mock
   * and returning a function that will restore the original A11yUtils when called.
   *
   * @param {object} window - The window for which to mock A11yUtils.
   * @returns {Function} - A function to restore A11yUtils to the window.
   */
  static mockForWindow(window) {
    const realA11yUtils = window.A11yUtils;
    window.A11yUtils = MockedA11yUtils;

    return () => {
      // Restore everything back to normal for this window.
      MockedA11yUtils.announceCalls = [];
      window.A11yUtils = realA11yUtils;
    };
  }

  /**
   * A mocked call to A11yUtils.announce that captures the parameters.
   *
   * @param {{ raw: string, id: string }}
   */
  static announce({ id, raw }) {
    MockedA11yUtils.announceCalls.push({ id, raw });
  }

  /**
   * Asserts that the most recent A11yUtils announce call matches the expectations.
   *
   * @param {object} expectations
   * @param {string} expectations.expectedCallNumber - The expected position in the announceCalls array.
   * @param {object} expectations.expectedArgs - The expected arguments passed to the most recent announce call.
   */
  static assertMostRecentAnnounceCall({ expectedCallNumber, expectedArgs }) {
    is(
      MockedA11yUtils.announceCalls.length,
      expectedCallNumber,
      "The most recent A11yUtils announce should match the expected call number."
    );
    const { id, raw } = MockedA11yUtils.announceCalls.at(-1);
    const { id: expectedId, raw: expectedRaw } = expectedArgs;

    is(
      id,
      expectedId,
      "A11yUtils announce arg id should match the expected arg id."
    );
    is(
      raw,
      expectedRaw,
      "A11yUtils announce arg raw should match the expected arg raw."
    );
  }
}

/**
 * Ensures that the window size is within 50px of the given dimensions.
 *
 * @param {WindowProxy} win
 * @param {number} width
 * @param {number} height
 *
 * @returns {Promise<void>}
 */
async function ensureWindowSize(win, width, height) {
  if (
    Math.abs(win.outerWidth - width) <= 1 &&
    Math.abs(win.outerHeight - height) <= 1
  ) {
    return;
  }

  info(
    `Resizing to ${width}x${height} (currently ${win.outerWidth}x${win.outerHeight})`
  );

  const resizePromise = BrowserTestUtils.waitForEvent(win, "resize");

  win.resizeTo(width, height);

  await resizePromise;
}

/**
 * Load a translations test page in a new tab and wire up utilities used by the browser tests.
 *
 * Exactly one of `page` or `html` must be provided. Supplying `page` will navigate to a
 * pre-defined test-file URL, while providing `html` serves the markup from a local server.
 *
 * @param {object} [options]
 * @param {Array<{fromLang: string, toLang: string}>} [options.languagePairs]
 * @param {boolean} [options.endToEndTest=false]
 * @param {boolean} [options.autoDownloadFromRemoteSettings=false]
 * @param {string} [options.page] - Fixture URL to load. Mutually exclusive with `html`.
 * @param {string} [options.html] - Raw HTML markup to serve once. Mutually exclusive with `page`.
 * @param {Array<[string, any]>} [options.prefs]
 * @param {boolean} [options.autoOffer]
 * @param {string[]} [options.permissionsUrls]
 * @param {string[]} [options.systemLocales=["en"]]
 * @param {string[]} [options.appLocales]
 * @param {string[]} [options.webLanguages]
 * @param {string} [options.architecture]
 * @param {boolean} [options.contentEagerMode=false]
 * @param {WindowProxy} [options.win=window]
 * @returns {Promise<{
 *   tab: object,
 *   remoteClients: (Record<string, any> | null),
 *   cleanup: () => Promise<void>,
 *   resolveDownloads: (count: number) => Promise<void>,
 *   rejectDownloads: (count: number) => Promise<void>,
 *   resolveBulkDownloads: (expectations: { expectedWasmDownloads: number, expectedLanguagePairDownloads: number }) => Promise<void>,
 *   rejectBulkDownloads: (expectations: { expectedWasmDownloads: number, expectedLanguagePairDownloads: number }) => Promise<void>,
 *   runInPage: RunInPageFn
 * }>}
 */
async function loadTestPage({
  languagePairs,
  endToEndTest = false,
  autoDownloadFromRemoteSettings = false,
  page,
  html,
  prefs,
  autoOffer,
  permissionsUrls,
  systemLocales = ["en"],
  appLocales,
  webLanguages,
  architecture,
  contentEagerMode = false,
  win = window,
}) {
  // Just one argument should be set
  const hasPage = page !== undefined;
  const hasHtml = html !== undefined;

  if (hasPage === hasHtml) {
    throw new Error(
      "Provide either the `page` or the `html` option when loading a test page."
    );
  }

  const { url, serverClosed } = hasHtml ? serveOnce(html) : {};

  // If there are multiple windows, only do the first time setup on the main window.
  const isFirstTimeSetup = win === window;

  let remoteClients = null;
  let removeMocks = () => {};

  const restoreA11yUtils = MockedA11yUtils.mockForWindow(win);

  if (isFirstTimeSetup) {
    await ensureWindowSize(win, 1000, 600);

    // Ensure no engine is being carried over from a previous test.
    await EngineProcess.destroyTranslationsEngine();

    Services.fog.testResetFOG();
    await SpecialPowers.pushPrefEnv({
      set: [
        // Enabled by default.
        ["browser.translations.enable", true],
        ["browser.translations.logLevel", "All"],
        ["browser.translations.automaticallyPopup", true],
        ["browser.translations.alwaysTranslateLanguages", ""],
        ["browser.translations.neverTranslateLanguages", ""],
        ["browser.translations.mostRecentTargetLanguages", ""],
        [USE_LEXICAL_SHORTLIST_PREF, false],
        // Bug 1893100 - This is needed to ensure that switching focus
        // with tab works in tests independent of macOS settings that
        // would otherwise disable keyboard navigation at the OS level.
        ["accessibility.tabfocus_applies_to_xul", false],
        ...(prefs ?? []),
      ],
    });
    await SpecialPowers.pushPermissions(
      [
        ENGLISH_PAGE_URL,
        FRENCH_PAGE_URL,
        NO_LANGUAGE_URL,
        SPANISH_PAGE_URL,
        SPANISH_PAGE_URL_2,
        SPANISH_PAGE_URL_DOT_ORG,
        ...(permissionsUrls || []),
      ].map(url => ({
        type: TRANSLATIONS_PERMISSION,
        allow: true,
        context: url,
      }))
    );

    const result = endToEndTest
      ? await createFileSystemRemoteSettings(languagePairs, architecture)
      : await createAndMockRemoteSettings({
          languagePairs,
          autoDownloadFromRemoteSettings,
        });
    remoteClients = result.remoteClients;
    removeMocks = result.removeMocks;
  }

  if (autoOffer) {
    TranslationsParent.testAutomaticPopup = true;
  }

  let cleanupLocales;
  if (systemLocales || appLocales || webLanguages) {
    cleanupLocales = await mockLocales({
      systemLocales,
      appLocales,
      webLanguages,
    });
  }

  // Start the tab at a blank page.
  const tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    BLANK_PAGE,
    true // waitForLoad
  );

  if (contentEagerMode) {
    info("Triggering content-eager translations mode by opening the find bar.");
    await openFindBar(tab);

    // We cannot access the TranslationsParent actor on BLANK_PAGE because the
    // data scheme is disallowed for the TranslationsParent actor, so we will load
    // our blank https:// page to ensure that the actor has registered its findBar.
    await loadNewPage(tab.linkedBrowser, BLANK_PAGE_URL);

    const actor = getTranslationsParent(win);
    await waitForCondition(
      () => actor.findBar,
      "Waiting for the TranslationsParent actor to register its findBar"
    );
  }

  if (page) {
    info(`Loading test page starting at url: ${page}`);
    await loadNewPage(tab.linkedBrowser, page);
  } else {
    info(`Loading test html at: ${url}`);
    await loadNewPage(tab.linkedBrowser, url);
  }

  if (autoOffer && TranslationsParent.shouldAlwaysOfferTranslations()) {
    info("Waiting for the popup to be automatically shown.");
    await waitForCondition(() => {
      const panel = document.getElementById("full-page-translations-panel");
      return panel && panel.state === "open";
    });
  }

  return {
    tab,
    remoteClients,

    /**
     * Resolves the downloads for the pending count of requested language pairs.
     * This should be used when resolving downloads immediately after requesting them.
     *
     * @see {resolveBulkDownloads} for requesting multiple translations prior to resolving.
     *
     * @param {number} count - Count of the language pairs expected.
     */
    async resolveDownloads(count) {
      await remoteClients.translationsWasm.resolvePendingDownloads(1);
      await remoteClients.translationModels.resolvePendingDownloads(
        downloadedFilesPerLanguagePair() * count
      );
    },

    /**
     * Rejects the downloads for the pending count of requested language pairs.
     * This should be used when rejecting downloads immediately after requesting them.
     *
     * @see {resolveBulkDownloads} for requesting multiple translations prior to rejecting.
     *
     * @param {number} count - Count of the language pairs expected.
     */
    async rejectDownloads(count) {
      await remoteClients.translationsWasm.rejectPendingDownloads(1);
      await remoteClients.translationModels.rejectPendingDownloads(
        downloadedFilesPerLanguagePair() * count
      );
    },

    /**
     * Resolves downloads for multiple pending translation requests.
     *
     * @see {resolveDownloads} for resolving downloads for just a single request.
     *
     * @param {object} expectations
     * @param {number} expectations.expectedWasmDownloads
     *  - The expected count of pending WASM binary download requests.
     * @param {number} expectations.expectedLanguagePairDownloads
     *  - The expected count of language-pair model-download requests.
     */
    async resolveBulkDownloads({
      expectedWasmDownloads,
      expectedLanguagePairDownloads,
    }) {
      await remoteClients.translationsWasm.resolvePendingDownloads(
        expectedWasmDownloads
      );
      await remoteClients.translationModels.resolvePendingDownloads(
        downloadedFilesPerLanguagePair() * expectedLanguagePairDownloads
      );
    },

    /**
     * Rejects downloads for multiple pending translation requests.
     *
     * @see {rejectDownloads} for rejecting downloads for just a single request.
     *
     * @param {object} expectations
     * @param {number} expectations.expectedWasmDownloads
     *  - The expected count of pending WASM binary download requests.
     * @param {number} expectations.expectedLanguagePairDownloads
     *  - The expected count of language-pair model-download requests.
     */
    async rejectBulkDownloads({
      expectedWasmDownloads,
      expectedLanguagePairDownloads,
    }) {
      await remoteClients.translationsWasm.rejectPendingDownloads(
        expectedWasmDownloads
      );
      await remoteClients.translationModels.rejectPendingDownloads(
        downloadedFilesPerLanguagePair() * expectedLanguagePairDownloads
      );
    },

    /**
     * @returns {Promise<void>}
     */
    async cleanup() {
      await closeAllOpenPanelsAndMenus();
      await loadBlankPage();
      await EngineProcess.destroyTranslationsEngine();
      await removeMocks();
      if (cleanupLocales) {
        await cleanupLocales();
      }
      if (serverClosed) {
        await serverClosed;
      }
      restoreA11yUtils();
      TranslationsParent.testAutomaticPopup = false;
      TranslationsParent.resetHostsOffered();
      BrowserTestUtils.removeTab(tab);
      TestTranslationsTelemetry.cleanup();
      return Promise.all([
        SpecialPowers.popPrefEnv(),
        SpecialPowers.popPermissions(),
      ]);
    },

    /**
     * Runs a callback in the content page. The function's contents are serialized as
     * a string, and run in the page. The `translations-test.mjs` module is made
     * available to the page.
     *
     * @type {RunInPageFn}
     */
    runInPage(callback, data = {}) {
      // TODO: Switch to SpecialPowers.spawn
      // eslint-disable-next-line mozilla/reject-contenttask-spawn
      return ContentTask.spawn(
        tab.linkedBrowser,
        { contentData: data, callbackSource: callback.toString() }, // Data to inject.
        function ({ contentData, callbackSource }) {
          const TranslationsTest = ChromeUtils.importESModule(
            "chrome://mochitests/content/browser/toolkit/components/translations/tests/browser/translations-test.mjs"
          );

          // Pass in the values that get injected by the task runner.
          TranslationsTest.setup({ Assert, ContentTaskUtils, content });

          // eslint-disable-next-line no-eval
          let contentCallback = eval(`(${callbackSource})`);
          return contentCallback(TranslationsTest, contentData);
        }
      );
    },
  };
}

/**
 * Captures any reported errors in the TranslationsParent.
 *
 * @param {Function} callback
 * @returns {Array<{ error: Error, args: any[] }>}
 */
async function captureTranslationsError(callback) {
  const { reportError } = TranslationsParent;

  let errors = [];
  TranslationsParent.reportError = (error, ...args) => {
    errors.push({ error, args });
  };

  await callback();

  // Restore the original function.
  TranslationsParent.reportError = reportError;
  return errors;
}

/**
 * Opens the FindBar in the given tab for the current window.
 */
async function openFindBar(tab, win = window) {
  info("Opening the find bar in the current tab.");
  const findBar = await win.gBrowser.getFindBar(tab);
  const { promise, resolve } = Promise.withResolvers();

  findBar.addEventListener(
    "findbaropen",
    () => {
      resolve();
    },
    { once: true }
  );

  findBar.open();
  await promise;
}

/**
 * Opens the FindBar in the given tab for the current window.
 */
async function closeFindBar(tab, win = window) {
  info("Closing the find bar in the current tab.");
  const findBar = await win.gBrowser.getFindBar(tab);
  const { promise, resolve } = Promise.withResolvers();

  findBar.addEventListener(
    "findbarclose",
    () => {
      resolve();
    },
    { once: true }
  );

  findBar.close();
  await promise;
}

/**
 * Load a test page and run
 *
 * @param {object} options - The options for `loadTestPage` plus a `runInPage` function.
 */
async function autoTranslatePage(options) {
  const { prefs, languagePairs, ...otherOptions } = options;
  const fromLangs = languagePairs.map(language => language.fromLang).join(",");
  const { cleanup, runInPage } = await loadTestPage({
    autoDownloadFromRemoteSettings: true,
    prefs: [
      ["browser.translations.alwaysTranslateLanguages", fromLangs],
      ...(prefs ?? []),
    ],
    ...otherOptions,
  });

  await runInPage(options.runInPage);
  await cleanup();
}

/**
 * @typedef {ReturnType<createAttachmentMock>} AttachmentMock
 */

/**
 * @param {RemoteSettingsClient} client
 * @param {string} mockedCollectionName - The name of the mocked collection without
 *  the incrementing "id" part. This is provided so that attachments can be asserted
 *  as being of a certain version.
 * @param {boolean} autoDownloadFromRemoteSettings - Skip the manual download process,
 *  and automatically download the files. Normally it's preferrable to manually trigger
 *  the downloads to trigger the download behavior, but this flag lets you bypass this
 *  and automatically download the files.
 */
function createAttachmentMock(
  client,
  mockedCollectionName,
  autoDownloadFromRemoteSettings
) {
  const pendingDownloads = [];

  client.attachments.download = record =>
    new Promise((resolve, reject) => {
      console.log("Download requested:", client.collectionName, record.name);
      if (autoDownloadFromRemoteSettings) {
        const encoder = new TextEncoder();
        const { buffer } = encoder.encode(
          `Mocked download: ${mockedCollectionName} ${record.name} ${record.version}`
        );

        resolve({ buffer });
      } else {
        pendingDownloads.push({ record, resolve, reject });
      }
    });

  function resolvePendingDownloads(expectedDownloadCount) {
    info(
      `Resolving ${expectedDownloadCount} mocked downloads for "${client.collectionName}"`
    );
    return downloadHandler(expectedDownloadCount, download =>
      download.resolve({ buffer: new ArrayBuffer() })
    );
  }

  async function rejectPendingDownloads(expectedDownloadCount) {
    info(
      `Intentionally rejecting ${expectedDownloadCount} mocked downloads for "${client.collectionName}"`
    );

    const names = [];
    const waitTick = () => new Promise(resolve => setTimeout(resolve, 0));

    const rejectNext = () => {
      const download = pendingDownloads.shift();
      if (!download) {
        return false;
      }
      console.log(`Handling download:`, client.collectionName);
      download.reject(new Error("Intentionally rejecting downloads."));
      names.push(download.record.name);
      return true;
    };

    // Wait for the expected downloads to start arriving and reject them as they do.
    while (names.length < expectedDownloadCount) {
      try {
        await waitForPendingDownloads(names.length + 1);
        while (names.length < expectedDownloadCount && rejectNext()) {
          // Keep rejecting until we reach the expected count.
        }
      } catch (error) {
        // Timeout waiting for downloads - this can happen if downloads aren't
        // requested or if they complete through a different path. Log and continue.
        info(
          `Timeout or error waiting for download ${names.length + 1}: ${error.message}`
        );
        break;
      }
    }

    // Drain any retries until the queue stays empty for a short idle window.
    let idleTicks = 0;
    const idleWindow = 20;
    while (idleTicks < idleWindow) {
      await waitTick();
      if (rejectNext()) {
        idleTicks = 0;
      } else {
        idleTicks++;
      }
    }

    if (pendingDownloads.length) {
      throw new Error(
        `An unexpected download was found, only expected ${expectedDownloadCount} downloads`
      );
    }

    return names.sort((a, b) => a.localeCompare(b));
  }

  async function downloadHandler(expectedDownloadCount, action) {
    const names = [];
    let maxTries = 100;
    while (names.length < expectedDownloadCount && maxTries-- > 0) {
      await new Promise(resolve => setTimeout(resolve, 0));
      let download = pendingDownloads.shift();
      if (!download) {
        // Uncomment the following to debug download issues:
        // console.log(`No pending download:`, client.collectionName, names.length);
        continue;
      }
      console.log(`Handling download:`, client.collectionName);
      action(download);
      names.push(download.record.name);
    }

    // This next check is not guaranteed to catch an unexpected download, but wait
    // at least one event loop tick to see if any more downloads were added.
    await new Promise(resolve => setTimeout(resolve, 0));

    if (pendingDownloads.length) {
      throw new Error(
        `An unexpected download was found, only expected ${expectedDownloadCount} downloads`
      );
    }

    return names.sort((a, b) => a.localeCompare(b));
  }

  async function assertNoNewDownloads() {
    await new Promise(resolve => setTimeout(resolve, 0));
    is(
      pendingDownloads.length,
      0,
      `No downloads happened for "${client.collectionName}"`
    );
  }

  function waitForPendingDownloads(expectedCount) {
    return waitForCondition(
      () => pendingDownloads.length >= expectedCount,
      `Waiting for ${expectedCount} pending downloads for "${client.collectionName}"`,
      100,
      10
    );
  }

  return {
    client,
    pendingDownloads,
    resolvePendingDownloads,
    rejectPendingDownloads,
    assertNoNewDownloads,
    waitForPendingDownloads,
  };
}

/**
 * The count of records per mocked language pair in Remote Settings utilizing a shared-vocab config.
 */
const RECORDS_PER_LANGUAGE_PAIR_SHARED_VOCAB = 3;

/**
 * The count of records per mocked language pair in Remote Settings utilizing a split-vocab config.
 */
const RECORDS_PER_LANGUAGE_PAIR_SPLIT_VOCAB = 4;

/**
 * The count of files that are downloaded for a mocked language pair in Remote Settings.
 */
function downloadedFilesPerLanguagePair(splitVocab = false) {
  const expectedRecords = splitVocab
    ? RECORDS_PER_LANGUAGE_PAIR_SPLIT_VOCAB
    : RECORDS_PER_LANGUAGE_PAIR_SHARED_VOCAB;

  return Services.prefs.getBoolPref(USE_LEXICAL_SHORTLIST_PREF)
    ? expectedRecords
    : expectedRecords - 1;
}

function createRecordsForLanguagePair(fromLang, toLang, splitVocab = false) {
  const records = [];
  const lang = fromLang + toLang;
  const models = [
    { fileType: "model", name: `model.${lang}.intgemm.alphas.bin` },
    { fileType: "lex", name: `lex.50.50.${lang}.s2t.bin` },
    ...(splitVocab
      ? [
          { fileType: "srcvocab", name: `srcvocab.${lang}.spm` },
          { fileType: "trgvocab", name: `trgvocab.${lang}.spm` },
        ]
      : [{ fileType: "vocab", name: `vocab.${lang}.spm` }]),
  ];

  const expectedLength = splitVocab
    ? RECORDS_PER_LANGUAGE_PAIR_SPLIT_VOCAB
    : RECORDS_PER_LANGUAGE_PAIR_SHARED_VOCAB;

  is(
    models.length,
    expectedLength,
    "The number of records per language pair should match the expected length."
  );

  for (const { fileType, name } of models) {
    const attachment = {
      hash: `${crypto.randomUUID()}`,
      size: "123",
      filename: name,
      location: `main-workspace/translations-models/${crypto.randomUUID()}.bin`,
      mimetype: "application/octet-stream",
      isDownloaded: false,
    };

    records.push({
      id: crypto.randomUUID(),
      name,
      sourceLanguage: fromLang,
      targetLanguage: toLang,
      fileType,
      version: TranslationsParent.LANGUAGE_MODEL_MAJOR_VERSION_MAX + ".0",
      last_modified: Date.now(),
      schema: Date.now(),
      attachment: JSON.parse(JSON.stringify(attachment)), // Making a deep copy
    });
  }
  return records;
}

/**
 * Creates a new WASM record for the Bergamot Translator to store in Remote Settings.
 *
 * @returns {WasmRecord}
 */
function createWasmRecord() {
  return {
    id: crypto.randomUUID(),
    name: "bergamot-translator",
    version: TranslationsParent.BERGAMOT_MAJOR_VERSION + ".0",
    last_modified: Date.now(),
    schema: Date.now(),
  };
}

/**
 * Increments each time a remote settings client is created to ensure a unique client
 * name for each test run.
 */
let _remoteSettingsMockId = 0;

/**
 * Creates a local RemoteSettingsClient for use within tests.
 *
 * @param {boolean} autoDownloadFromRemoteSettings
 * @param {object[]} langPairs
 * @returns {RemoteSettingsClient}
 */
async function createTranslationModelsRemoteClient(
  autoDownloadFromRemoteSettings,
  langPairs
) {
  const records = [];
  for (const { fromLang, toLang } of langPairs) {
    records.push(...createRecordsForLanguagePair(fromLang, toLang));
  }

  const { RemoteSettings } = ChromeUtils.importESModule(
    "resource://services-settings/remote-settings.sys.mjs"
  );
  const mockedCollectionName = "test-translation-models";
  const client = RemoteSettings(
    `${mockedCollectionName}-${_remoteSettingsMockId++}`
  );
  const metadata = {};
  await client.db.clear();
  await client.db.importChanges(metadata, Date.now(), records);

  return createAttachmentMock(
    client,
    mockedCollectionName,
    autoDownloadFromRemoteSettings
  );
}

/**
 * Creates a local RemoteSettingsClient for use within tests.
 *
 * @param {boolean} autoDownloadFromRemoteSettings
 * @returns {RemoteSettingsClient}
 */
async function createTranslationsWasmRemoteClient(
  autoDownloadFromRemoteSettings
) {
  const records = [createWasmRecord()];
  const { RemoteSettings } = ChromeUtils.importESModule(
    "resource://services-settings/remote-settings.sys.mjs"
  );
  const mockedCollectionName = "test-translation-wasm";
  const client = RemoteSettings(
    `${mockedCollectionName}-${_remoteSettingsMockId++}`
  );
  const metadata = {};
  await client.db.clear();
  await client.db.importChanges(metadata, Date.now(), records);

  return createAttachmentMock(
    client,
    mockedCollectionName,
    autoDownloadFromRemoteSettings
  );
}

/**
 * Modifies the client's Remote Settings database to create, update, and delete records, then emits
 * a "sync" event with the relevant changes for the Remote Settings client.
 *
 * Asserts that the list of records to create is disjoint from the list of records to delete.
 * If your test case needs to create a record and then delete it, do it in separate transactions.
 *
 * @param {RemoteSettingsClient} remoteSettingsClient - The Remote Settings client whose database will be modified.
 * @param {object} options
 * @param {TranslationModelRecord[]} [options.recordsToCreate]
 *  - A list of records to newly create or update. These records are automatically partitioned into
 *    either the created array or the updated array based on whether they exist in the database yet.
 * @param {TranslationModelRecord[]} [options.recordsToDelete]
 *  - A list of records to delete from the database. Asserts that all of these records exist in the
 *    database before deleting them.
 * @param {number} [options.expectedCreatedRecordsCount]
 *  - The expected count of records within the recordsToCreate parameter that are new to the database.
 * @param {number} [options.expectedUpdatedRecordsCount]
 *  - The expected count of records within the recordsToCreate parameter that are already in the database.
 * @param {number} [options.expectedDeletedRecordsCount]
 *  - The expected count of records within the recordsToDelete parameter that are already in the database.
 */
async function modifyRemoteSettingsRecords(
  remoteSettingsClient,
  {
    recordsToCreate = [],
    recordsToDelete = [],
    expectedCreatedRecordsCount = 0,
    expectedUpdatedRecordsCount = 0,
    expectedDeletedRecordsCount = 0,
  }
) {
  for (const recordToCreate of recordsToCreate) {
    for (const recordToDelete of recordsToDelete) {
      isnot(
        recordToCreate.id,
        recordToDelete.id,
        `Attempt to both create and delete the same record from Remote Settings database: '${recordToCreate.name}'`
      );
    }
  }

  let created = [];
  let updated = [];
  let deleted = [];

  const existingRecords = await remoteSettingsClient.get();

  for (const newRecord of recordsToCreate) {
    const existingRecord = existingRecords.find(
      existingRecord => existingRecord.id === newRecord.id
    );
    if (existingRecord) {
      updated.push({
        old: existingRecord,
        new: newRecord,
      });
    } else {
      created.push(newRecord);
    }
  }

  if (recordsToCreate.length) {
    info("Storing new and updated records in mocked Remote Settings database");
    await remoteSettingsClient.db.importChanges(
      /* metadata */ {},
      Date.now(),
      recordsToCreate
    );
  }

  if (recordsToDelete.length) {
    info("Storing new and updated records in mocked Remote Settings database");
    for (const recordToDelete of recordsToDelete) {
      ok(
        existingRecords.find(
          existingRecord => existingRecord.id === recordToDelete.id
        ),
        `The record to delete '${recordToDelete.name}' should be found in the database.`
      );
      await remoteSettingsClient.db.delete(recordToDelete.id);
      deleted.push(recordToDelete);
    }
  }

  is(
    created.length,
    expectedCreatedRecordsCount,
    "Expected the correct number of created records"
  );
  is(
    updated.length,
    expectedUpdatedRecordsCount,
    "Expected the correct number of updated records"
  );
  is(
    deleted.length,
    expectedDeletedRecordsCount,
    "Expected the correct number of deleted records"
  );

  info('Emitting a remote client "sync" event.');
  await remoteSettingsClient.emit("sync", {
    data: {
      created,
      updated,
      deleted,
    },
  });
}

async function selectAboutPreferencesElements() {
  const document = gBrowser.selectedBrowser.contentDocument;

  const settingsButton = document.getElementById(
    "translations-manage-settings-button"
  );

  const rows = await waitForCondition(() => {
    const elements = document.querySelectorAll(".translations-manage-language");
    if (elements.length !== 4) {
      return false;
    }
    return elements;
  }, "Waiting for manage language rows.");

  const [downloadAllRow, frenchRow, spanishRow, ukrainianRow] = rows;

  const downloadAllLabel = downloadAllRow.querySelector("label");
  const downloadAll = downloadAllRow.querySelector(
    "#translations-manage-install-all"
  );
  const deleteAll = downloadAllRow.querySelector(
    "#translations-manage-delete-all"
  );
  const frenchLabel = frenchRow.querySelector("label");
  const frenchDownload = frenchRow.querySelector(
    `[data-l10n-id="translations-manage-language-download-button"]`
  );
  const frenchDelete = frenchRow.querySelector(
    `[data-l10n-id="translations-manage-language-remove-button"]`
  );
  const spanishLabel = spanishRow.querySelector("label");
  const spanishDownload = spanishRow.querySelector(
    `[data-l10n-id="translations-manage-language-download-button"]`
  );
  const spanishDelete = spanishRow.querySelector(
    `[data-l10n-id="translations-manage-language-remove-button"]`
  );
  const ukrainianLabel = ukrainianRow.querySelector("label");
  const ukrainianDownload = ukrainianRow.querySelector(
    `[data-l10n-id="translations-manage-language-download-button"]`
  );
  const ukrainianDelete = ukrainianRow.querySelector(
    `[data-l10n-id="translations-manage-language-remove-button"]`
  );

  return {
    document,
    downloadAllLabel,
    downloadAll,
    deleteAll,
    frenchLabel,
    frenchDownload,
    frenchDelete,
    ukrainianLabel,
    ukrainianDownload,
    ukrainianDelete,
    settingsButton,
    spanishLabel,
    spanishDownload,
    spanishDelete,
  };
}

function click(button, message) {
  info(message);
  if (button.hidden) {
    throw new Error("The button was hidden when trying to click it.");
  }
  button.click();
}

function hitEnterKey(button, message) {
  info(message);
  button.dispatchEvent(
    new KeyboardEvent("keypress", {
      key: "Enter",
      keyCode: KeyboardEvent.DOM_VK_RETURN,
    })
  );
}

/**
 * Similar to assertVisibility, but is asynchronous and attempts
 * to wait for the elements to match the expected states if they
 * do not already.
 *
 * @see assertVisibility
 *
 * @param {object} options
 * @param {string} options.message
 * @param {Record<string, Element[]>} options.visible
 * @param {Record<string, Element[]>} options.hidden
 */
async function ensureVisibility({ message = null, visible = {}, hidden = {} }) {
  try {
    // First wait for the condition to be met.
    await waitForCondition(() => {
      for (const element of Object.values(visible)) {
        if (BrowserTestUtils.isHidden(element)) {
          return false;
        }
      }
      for (const element of Object.values(hidden)) {
        if (BrowserTestUtils.isVisible(element)) {
          return false;
        }
      }
      return true;
    });
  } catch (error) {
    // Ignore, this will get caught below.
  }
  // Now report the conditions.
  assertVisibility({ message, visible, hidden });
}

/**
 * Asserts that the provided elements are either visible or hidden.
 *
 * @param {object} options
 * @param {string} options.message
 * @param {Record<string, Element[]>} options.visible
 * @param {Record<string, Element[]>} options.hidden
 */
function assertVisibility({ message = null, visible = {}, hidden = {} }) {
  if (message) {
    info(message);
  }
  for (const [name, element] of Object.entries(visible)) {
    ok(BrowserTestUtils.isVisible(element), `${name} is visible.`);
  }
  for (const [name, element] of Object.entries(hidden)) {
    ok(BrowserTestUtils.isHidden(element), `${name} is hidden.`);
  }
}

async function setupAboutPreferences(
  languagePairs,
  { prefs = [], permissionsUrls = [] } = {}
) {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.translations.enable", true],
      ["browser.translations.logLevel", "All"],
      [USE_LEXICAL_SHORTLIST_PREF, false],
      ...prefs,
    ],
  });
  await SpecialPowers.pushPermissions(
    permissionsUrls.map(url => ({
      type: TRANSLATIONS_PERMISSION,
      allow: true,
      context: url,
    }))
  );
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    BLANK_PAGE,
    true // waitForLoad
  );

  const { remoteClients, removeMocks } = await createAndMockRemoteSettings({
    languagePairs,
  });

  await loadNewPage(tab.linkedBrowser, "about:preferences");

  // Load the Languages pane if SRD is enabled (moved from General)
  const document = gBrowser.selectedBrowser.contentDocument;
  let redesigned = Services.prefs.getBoolPref(
    "browser.settings-redesign.enabled",
    false
  );
  if (redesigned) {
    let loaded = BrowserTestUtils.waitForEvent(document, "paneshown");
    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("category-languages"),
      {},
      document.documentGlobal
    );
    let event = await loaded;
    is(event.detail.category, "paneLanguages", "Loaded the correct pane");
  }

  const elements = await selectAboutPreferencesElements();

  const translationsSettingsTestUtils = new TranslationsSettingsTestUtils(
    document
  );

  async function cleanup() {
    Services.prefs.setCharPref(NEVER_TRANSLATE_LANGS_PREF, "");
    Services.prefs.setCharPref(ALWAYS_TRANSLATE_LANGS_PREF, "");
    Services.perms.removeAll();
    await closeAllOpenPanelsAndMenus();
    await loadBlankPage();
    await EngineProcess.destroyTranslationsEngine();
    BrowserTestUtils.removeTab(tab);
    await removeMocks();
    await SpecialPowers.popPrefEnv();
    TestTranslationsTelemetry.cleanup();
  }

  return {
    cleanup,
    remoteClients,
    elements,
    translationsSettingsTestUtils,
  };
}

/**
 * Tests a callback function with the lexical shortlist preference enabled and disabled.
 *
 * @param {Function} callback - An async function to execute, receiving the preference settings as an argument.
 */
async function testWithAndWithoutLexicalShortlist(callback) {
  for (const prefs of [
    [[USE_LEXICAL_SHORTLIST_PREF, true]],
    [[USE_LEXICAL_SHORTLIST_PREF, false]],
  ]) {
    await callback(prefs);
  }
}

/**
 * Waits for the "translations:model-records-changed" observer event to occur.
 *
 * @param {Function} [callback]
 *   - An optional function to execute before waiting for the "translations:pref-changed" observer event.
 * @returns {Promise<void>}
 *   - A promise that resolves when the "translations:model-records-changed" event is observed.
 */
async function waitForTranslationModelRecordsChanged(callback) {
  const { promise, resolve } = Promise.withResolvers();

  function onChange() {
    Services.obs.removeObserver(onChange, "translations:model-records-changed");
    resolve();
  }
  Services.obs.addObserver(onChange, "translations:model-records-changed");

  if (callback) {
    await callback();
  }

  await promise;
}

function waitForAppLocaleChanged() {
  new Promise(resolve => {
    function onChange() {
      Services.obs.removeObserver(onChange, "intl:app-locales-changed");
      resolve();
    }
    Services.obs.addObserver(onChange, "intl:app-locales-changed");
  });
}

async function mockLocales({ systemLocales, appLocales, webLanguages }) {
  if (systemLocales) {
    TranslationsParent.mockedSystemLocales = systemLocales;
  }

  const { availableLocales, requestedLocales } = Services.locale;

  if (appLocales) {
    await SpecialPowers.pushPrefEnv({
      set: [["intl.locale.requested", "en"]],
    });

    const appLocaleChanged = waitForAppLocaleChanged();

    info("Mocking locales, so expect potential .ftl resource errors.");
    Services.locale.availableLocales = appLocales;
    Services.locale.requestedLocales = appLocales;

    await appLocaleChanged;
  }

  if (webLanguages) {
    await SpecialPowers.pushPrefEnv({
      set: [["intl.accept_languages", webLanguages.join(",")]],
    });
  }

  return async () => {
    // Reset back to the originals.
    if (webLanguages) {
      await SpecialPowers.popPrefEnv();
    }

    if (appLocales) {
      const appLocaleChanged = waitForAppLocaleChanged();

      Services.locale.availableLocales = availableLocales;
      Services.locale.requestedLocales = requestedLocales;

      await appLocaleChanged;

      await SpecialPowers.popPrefEnv();
    }

    if (systemLocales) {
      TranslationsParent.mockedSystemLocales = null;
    }
  };
}

/**
 * Helpful test functions for translations telemetry
 */
class TestTranslationsTelemetry {
  static #previousFlowId = null;

  static cleanup() {
    TestTranslationsTelemetry.#previousFlowId = null;
    Services.fog.testResetFOG();
  }

  /**
   * Asserts qualities about a counter telemetry metric.
   *
   * @param {string} name - The name of the metric.
   * @param {object} counter - The Glean counter object.
   * @param {object} expectedCount - The expected value of the counter.
   */
  static async assertCounter(name, counter, expectedCount) {
    // Ensures that glean metrics are collected from all child processes
    // so that calls to testGetValue() are up to date.
    await Services.fog.testFlushAllChildren();
    const count = counter.testGetValue() ?? 0;
    is(
      count,
      expectedCount,
      `Telemetry counter ${name} should have expected count`
    );
  }

  /**
   * Asserts that a counter with the given label matches the expected count for that label.
   *
   * @param {object} counter - The Glean counter object.
   * @param {Array<Array<string | number>>} expectations - An array of string/number pairs for the label and expected count.
   */
  static async assertLabeledCounter(counter, expectations) {
    for (const [label, expectedCount] of expectations) {
      await Services.fog.testFlushAllChildren();
      const count = counter[label].testGetValue() ?? 0;
      is(
        count,
        expectedCount,
        `Telemetry counter with label ${label} should have expected count.`
      );
    }
  }

  /**
   * Asserts qualities about an event telemetry metric.
   *
   * @param {object} event - The Glean event object.
   * @param {object} expectations - The test expectations.
   * @param {number} expectations.expectedEventCount - The expected count of events.
   * @param {boolean} expectations.expectNewFlowId
   * @param {Record<string, string | boolean | number | Function>} [expectations.assertForAllEvents]
   * - A record of key-value pairs to assert against all events in this category.
   * @param {Record<string, string | boolean | number | Function>} [expectations.assertForMostRecentEvent]
   * - A record of key-value pairs to assert against the most recently recorded event in this category.
   */
  static async assertEvent(
    event,
    {
      expectedEventCount,
      expectNewFlowId = null,
      assertForAllEvents = {},
      assertForMostRecentEvent = {},
    }
  ) {
    // Ensures that glean metrics are collected from all child processes
    // so that calls to testGetValue() are up to date.
    await Services.fog.testFlushAllChildren();
    const events = event.testGetValue() ?? [];
    const eventCount = events.length;
    const name =
      eventCount > 0 ? `${events[0].category}.${events[0].name}` : null;

    if (eventCount > 0 && expectNewFlowId !== null) {
      const flowId = events[eventCount - 1].extra.flow_id;
      if (expectNewFlowId) {
        is(
          events[eventCount - 1].extra.flow_id !==
            TestTranslationsTelemetry.#previousFlowId,
          true,
          `The newest flowId ${flowId} should be different than the previous flowId ${
            TestTranslationsTelemetry.#previousFlowId
          }`
        );
      } else {
        is(
          events[eventCount - 1].extra.flow_id ===
            TestTranslationsTelemetry.#previousFlowId,
          true,
          `The newest flowId ${flowId} should be equal to the previous flowId ${
            TestTranslationsTelemetry.#previousFlowId
          }`
        );
      }
      TestTranslationsTelemetry.#previousFlowId = flowId;
    }

    if (eventCount !== expectedEventCount) {
      console.error("Actual events:", events);
    }

    is(
      eventCount,
      expectedEventCount,
      `There should be ${expectedEventCount} telemetry events of type ${name}`
    );

    if (Object.keys(assertForAllEvents).length !== 0) {
      is(
        eventCount > 0,
        true,
        `Telemetry event ${name} should contain values if assertForMostRecentEvent are specified`
      );
      if (eventCount === 0) {
        return;
      }
      for (const [key, expected] of Object.entries(assertForAllEvents)) {
        for (const event of events) {
          if (typeof expected === "function") {
            ok(
              expected(event.extra[key]),
              `Telemetry event ${name} value for ${key} should match the expected predicate: got ${event.extra[key]}`
            );
          } else {
            is(
              event.extra[key],
              String(expected),
              `Telemetry event ${name} value for ${key} should match the expected entry`
            );
          }
        }
      }
    }

    if (Object.keys(assertForMostRecentEvent).length !== 0) {
      is(
        eventCount > 0,
        true,
        `Telemetry event ${name} should contain values if assertForMostRecentEvent are specified`
      );
      if (eventCount === 0) {
        return;
      }
      for (const [key, expected] of Object.entries(assertForMostRecentEvent)) {
        if (typeof expected === "function") {
          ok(
            expected(events[eventCount - 1].extra[key]),
            `Telemetry event ${name} value for ${key} should match the expected predicate: got ${events[eventCount - 1].extra[key]}`
          );
        } else {
          is(
            events[eventCount - 1].extra[key],
            String(expected),
            `Telemetry event ${name} value for ${key} should match the expected entry`
          );
        }
      }
    }
  }

  /**
   * Asserts qualities about a rate telemetry metric.
   *
   * @param {string} name - The name of the metric.
   * @param {object} rate - The Glean rate object.
   * @param {object} expectations - The test expectations.
   * @param {number} expectations.expectedNumerator - The expected value of the numerator.
   * @param {number} expectations.expectedDenominator - The expected value of the denominator.
   */
  static async assertRate(
    name,
    rate,
    { expectedNumerator, expectedDenominator }
  ) {
    // Ensures that glean metrics are collected from all child processes
    // so that calls to testGetValue() are up to date.
    await Services.fog.testFlushAllChildren();
    const { numerator = 0, denominator = 0 } = rate.testGetValue() ?? {};
    is(
      numerator,
      expectedNumerator,
      `Telemetry rate ${name} should have expected numerator`
    );
    is(
      denominator,
      expectedDenominator,
      `Telemetry rate ${name} should have expected denominator`
    );
  }

  /**
   * Asserts that all TranslationsEngine performance events are expected and have valid data.
   *
   * @param {object} expectations - The test expectations.
   * @param {number} expectations.expectedEventCount - The expected count of engine performance events.
   */
  static async assertTranslationsEnginePerformance({ expectedEventCount }) {
    info("Destroying the TranslationsEngine.");
    await EngineProcess.destroyTranslationsEngine();

    const isNotEmptyString = entry => typeof entry === "string" && entry !== "";
    const isGreaterThanZero = entry => parseFloat(entry) > 0;

    const assertForAllEvents =
      expectedEventCount === 0
        ? {}
        : {
            from_language: isNotEmptyString,
            to_language: isNotEmptyString,
            average_words_per_request: isGreaterThanZero,
            average_words_per_second: isGreaterThanZero,
            total_completed_requests: isGreaterThanZero,
            total_inference_seconds: isGreaterThanZero,
            total_translated_words: isGreaterThanZero,
          };

    await TestTranslationsTelemetry.assertEvent(
      Glean.translations.enginePerformance,
      {
        expectedEventCount,
        assertForAllEvents,
      }
    );
  }
}

/**
 * Provide longer defaults for the waitForCondition.
 *
 * @param {Function} callback
 * @param {string} message
 * @param {number} [interval=100] - Interval in milliseconds between condition checks
 * @param {number} [maxTries=null] - Maximum number of tries
 */
function waitForCondition(callback, message, interval = 100, maxTries = null) {
  // Use 4 times the defaults to guard against intermittents. Many of the tests rely on
  // communication between the parent and child process, which is inherently async.
  if (maxTries === null) {
    maxTries = 50 * 4;
  }
  return TestUtils.waitForCondition(callback, message, interval, maxTries);
}

/**
 * Retrieves the always-translate language list as an array.
 *
 * @returns {Array<string>}
 */
function getAlwaysTranslateLanguagesFromPref() {
  let langs = Services.prefs.getCharPref(ALWAYS_TRANSLATE_LANGS_PREF);
  return langs ? langs.split(",") : [];
}

/**
 * Retrieves the never-translate language list as an array.
 *
 * @returns {Array<string>}
 */
function getNeverTranslateLanguagesFromPref() {
  let langs = Services.prefs.getCharPref(NEVER_TRANSLATE_LANGS_PREF);
  return langs ? langs.split(",") : [];
}

/**
 * Retrieves the never-translate site list as an array.
 *
 * @returns {Array<string>}
 */
function getNeverTranslateSitesFromPerms() {
  let results = [];
  for (let perm of Services.perms.all) {
    if (
      perm.type == TRANSLATIONS_PERMISSION &&
      perm.capability == Services.perms.DENY_ACTION
    ) {
      results.push(perm.principal);
    }
  }

  return results;
}

/**
 * Opens a dialog window for about:preferences
 *
 * @param {string} dialogUrl - The URL of the dialog window
 * @param {Function} callback - The function to open the dialog via UI
 * @returns {object} The dialog window object
 */
async function waitForOpenDialogWindow(dialogUrl, callback) {
  const dialogLoaded = promiseLoadSubDialog(dialogUrl);
  await callback();
  const dialogWindow = await dialogLoaded;
  return dialogWindow;
}

/**
 * Closes an open dialog window and waits for it to close.
 *
 * @param {object} dialogWindow
 */
async function waitForCloseDialogWindow(dialogWindow) {
  const closePromise = BrowserTestUtils.waitForEvent(
    content.gSubDialog._dialogStack,
    "dialogclose"
  );
  dialogWindow.close();
  await closePromise;
}

// Extracted from https://searchfox.org/mozilla-central/rev/40ef22080910c2e2c27d9e2120642376b1d8b8b2/browser/components/preferences/in-content/tests/head.js#41
function promiseLoadSubDialog(aURL) {
  return new Promise(resolve => {
    content.gSubDialog._dialogStack.addEventListener(
      "dialogopen",
      function dialogopen(aEvent) {
        if (
          aEvent.detail.dialog._frame.contentWindow.location == "about:blank"
        ) {
          return;
        }
        content.gSubDialog._dialogStack.removeEventListener(
          "dialogopen",
          dialogopen
        );

        Assert.equal(
          aEvent.detail.dialog._frame.contentWindow.location.toString(),
          aURL,
          "Check the proper URL is loaded"
        );

        // Check visibility
        isnot(
          aEvent.detail.dialog._overlay,
          null,
          "Element should not be null, when checking visibility"
        );
        Assert.ok(
          !BrowserTestUtils.isHidden(aEvent.detail.dialog._overlay),
          "The element is visible"
        );

        // Check that stylesheets were injected
        let expectedStyleSheetURLs =
          aEvent.detail.dialog._injectedStyleSheets.slice(0);
        for (let styleSheet of aEvent.detail.dialog._frame.contentDocument
          .styleSheets) {
          let i = expectedStyleSheetURLs.indexOf(styleSheet.href);
          if (i >= 0) {
            info("found " + styleSheet.href);
            expectedStyleSheetURLs.splice(i, 1);
          }
        }
        Assert.equal(
          expectedStyleSheetURLs.length,
          0,
          "All expectedStyleSheetURLs should have been found"
        );

        // Wait for the next event tick to make sure the remaining part of the
        // testcase runs after the dialog gets ready for input.
        executeSoon(() => resolve(aEvent.detail.dialog._frame.contentWindow));
      }
    );
  });
}

/**
 * Loads the blank-page URL.
 *
 * This is useful for resetting the state during cleanup, and also
 * before starting a test, to further help ensure that there is no
 * unintentional state left over from test case.
 */
async function loadBlankPage() {
  await loadNewPage(gBrowser.selectedBrowser, BLANK_PAGE);
}

/**
 * Destroys the Translations Engine process.
 */
async function destroyTranslationsEngine() {
  await EngineProcess.destroyTranslationsEngine();
}

class AboutTranslationsTestUtils {
  /**
   * A collection of custom events that the about:translations document may dispatch.
   */
  static Events = class Events {
    /**
     * Event fired when the enabled state of the Translations feature changes.
     */
    static EnabledStateChanged = "AboutTranslationsTest:EnabledStateChanged";

    /**
     * Event fired when the detected language updates.
     *
     * @type {string}
     */
    static DetectedLanguageUpdated =
      "AboutTranslationsTest:DetectedLanguageUpdated";

    /**
     * Event fired when the swap-languages button becomes disabled.
     *
     * @type {string}
     */
    static SwapLanguagesButtonDisabled =
      "AboutTranslationsTest:SwapLanguagesButtonDisabled";

    /**
     * Event fired when the swap-languages button becomes enabled.
     *
     * @type {string}
     */
    static SwapLanguagesButtonEnabled =
      "AboutTranslationsTest:SwapLanguagesButtonEnabled";

    /**
     * Event fired when the translating placeholder message is shown.
     *
     * @type {string}
     */
    static ShowTranslatingPlaceholder =
      "AboutTranslationsTest:ShowTranslatingPlaceholder";

    /**
     * Event fired when the debounce-delay action triggers after input to the source text.
     */
    static SourceTextInputDebounced =
      "AboutTranslationsTest:SourceTextInputDebounced";

    /**
     * Event fired after the URL has been updated from UI interactions.
     *
     * @type {string}
     */
    static URLUpdatedFromUI = "AboutTranslationsTest:URLUpdatedFromUI";

    /**
     * Event fired when a translation is requested.
     *
     * @type {string}
     */
    static TranslationRequested = "AboutTranslationsTest:TranslationRequested";

    /**
     * Event fired when a translation completes.
     *
     * @type {string}
     */
    static TranslationComplete = "AboutTranslationsTest:TranslationComplete";

    /**
     * Event fired when the copy button becomes enabled.
     *
     * @type {string}
     */
    static CopyButtonEnabled = "AboutTranslationsTest:CopyButtonEnabled";

    /**
     * Event fired when the copy button becomes disabled.
     *
     * @type {string}
     */
    static CopyButtonDisabled = "AboutTranslationsTest:CopyButtonDisabled";

    /**
     * Event fired when the copy button shows the "copied" feedback state.
     *
     * @type {string}
     */
    static CopyButtonShowCopied = "AboutTranslationsTest:CopyButtonShowCopied";

    /**
     * Event fired when the copy button exits the "copied" feedback state.
     *
     * @type {string}
     */
    static CopyButtonReset = "AboutTranslationsTest:CopyButtonReset";

    /**
     * Event fired when the clear button becomes visible.
     *
     * @type {string}
     */
    static SourceTextClearButtonShown =
      "AboutTranslationsTest:SourceTextClearButtonShown";

    /**
     * Event fired when the clear button becomes hidden.
     *
     * @type {string}
     */
    static SourceTextClearButtonHidden =
      "AboutTranslationsTest:SourceTextClearButtonHidden";

    /**
     * Event fired when the page layout changes.
     *
     * @type {string}
     */
    static PageOrientationChanged =
      "AboutTranslationsTest:PageOrientationChanged";

    /**
     * Event fired when the source/target section heights change.
     *
     * @type {string}
     */
    static SectionHeightsChanged =
      "AboutTranslationsTest:SectionHeightsChanged";

    /**
     * Event fired when the source text is cleared programmatically.
     *
     * @type {string}
     */
    static ClearSourceText = "AboutTranslationsTest:ClearSourceText";

    /**
     * Event fired when the target text is cleared programmatically.
     *
     * @type {string}
     */
    static ClearTargetText = "AboutTranslationsTest:ClearTargetText";

    /**
     * Event fired when the unsupported info message is shown.
     *
     * @type {string}
     */
    static UnsupportedInfoMessageShown =
      "AboutTranslationsTest:UnsupportedInfoMessageShown";

    /**
     * Event fired when the unsupported info message is hidden.
     *
     * @type {string}
     */
    static UnsupportedInfoMessageHidden =
      "AboutTranslationsTest:UnsupportedInfoMessageHidden";

    /**
     * Event fired when the policy-disabled info message is shown.
     *
     * @type {string}
     */
    static PolicyDisabledInfoMessageShown =
      "AboutTranslationsTest:PolicyDisabledInfoMessageShown";

    /**
     * Event fired when the policy-disabled info message is hidden.
     *
     * @type {string}
     */
    static PolicyDisabledInfoMessageHidden =
      "AboutTranslationsTest:PolicyDisabledInfoMessageHidden";

    /**
     * Event fired when the language-load error message is shown.
     *
     * @type {string}
     */
    static LanguageLoadErrorMessageShown =
      "AboutTranslationsTest:LanguageLoadErrorMessageShown";

    /**
     * Event fired when the language-load error message is hidden.
     *
     * @type {string}
     */
    static LanguageLoadErrorMessageHidden =
      "AboutTranslationsTest:LanguageLoadErrorMessageHidden";

    /**
     * Event fired when the language-load retry flow starts.
     *
     * @type {string}
     */
    static LanguageLoadRetryStarted =
      "AboutTranslationsTest:LanguageLoadRetryStarted";

    /**
     * Event fired when the language-load retry flow succeeds.
     *
     * @type {string}
     */
    static LanguageLoadRetrySucceeded =
      "AboutTranslationsTest:LanguageLoadRetrySucceeded";

    /**
     * Event fired when the language-load retry flow fails.
     *
     * @type {string}
     */
    static LanguageLoadRetryFailed =
      "AboutTranslationsTest:LanguageLoadRetryFailed";
  };

  /** @type {object} */
  #browser;

  /**
   * A function that runs a closure in the content page.
   *
   * @type {RunInPageFn}
   */
  #runInPage;

  /**
   * A function that resolves download requests for tests.
   *
   * @type {(number) => Promise<void>}
   */
  #resolveDownloads;

  /**
   * A function that rejects download requests for tests.
   *
   * @type {(number) => Promise<void>}
   */
  #rejectDownloads;

  /**
   * Whether or not download requests should be resolved automatically,
   * or manually resolved/rejected by the test code.
   *
   * @type {boolean}
   */
  #autoDownloadFromRemoteSettings;

  /**
   * @param {object} browser
   *   The browser used for parent-process page navigation in tests.
   * @param {RunInPageFn} runInPage
   *   A function that runs a closure in the content page.
   * @param {(number) => Promise<void>} resolveDownloads
   *   A function that resolves download requests for tests.
   * @param {(number) => Promise<void>} rejectDownloads
   *   A function that rejects download requests for tests.
   * @param {boolean} autoDownloadFromRemoteSettings
   *   Whether download requests should be resolved automatically
   *   or manually resolved by the test code.
   */
  constructor(
    browser,
    runInPage,
    resolveDownloads,
    rejectDownloads,
    autoDownloadFromRemoteSettings
  ) {
    this.#browser = browser;
    this.#runInPage = runInPage;
    this.#resolveDownloads = resolveDownloads;
    this.#rejectDownloads = rejectDownloads;
    this.#autoDownloadFromRemoteSettings = autoDownloadFromRemoteSettings;
  }

  /**
   * Reports any error as a test failure.
   * This will show up more nicely in the test logs.
   *
   * @param {Error} error
   */
  static #reportTestFailure(error) {
    ok(false, String(error));
  }

  /**
   * Retrieves the count of words in the given text for the given language.
   *
   * @param {string} language
   * @param {string} text
   * @returns {number}
   */
  static getWordCount(language, text) {
    const segmenter = new Intl.Segmenter(language, { granularity: "word" });
    return Array.from(segmenter.segment(text)).filter(
      segment => segment.isWordLike
    ).length;
  }

  /**
   * Waits for the about:translations page to fully initialize.
   *
   * @returns {Promise<void>}
   */
  async waitForReady() {
    try {
      await this.#runInPage(async () => {
        const { document } = content;
        await ContentTaskUtils.waitForCondition(
          () => document.body.hasAttribute("ready-for-testing"),
          "Waiting for the about:translations document to be ready for tests."
        );
      });
      ok(true, "about:translations is ready.");
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Loads a fresh about:translations document with optional URL-hash parameters.
   *
   * @param {object}  [options={}]
   * @param {string}  [options.sourceLanguage] - Value for the "src" hash parameter.
   * @param {string}  [options.targetLanguage] - Value for the "trg" hash parameter.
   * @param {string}  [options.sourceText]     - Value for the "text" hash parameter.
   * @returns {Promise<void>}
   */
  async loadNewPage({ sourceLanguage, targetLanguage, sourceText } = {}) {
    const url = new URL("about:translations");
    const searchParams = new URLSearchParams();

    if (sourceLanguage) {
      searchParams.set("src", sourceLanguage);
    }

    if (targetLanguage) {
      searchParams.set("trg", targetLanguage);
    }

    if (sourceText) {
      searchParams.set("text", sourceText);
    }

    const hashString = searchParams.toString();
    url.hash = hashString ? hashString : "src=detect";

    logAction(url);
    await loadNewPage(this.#browser, BLANK_PAGE);
    await loadNewPage(this.#browser, url.href);

    await this.waitForReady();
  }

  /**
   * Navigates to a new hash URL in the current about:translations document.
   *
   * @param {object}  [options={}]
   * @param {string}  [options.sourceLanguage] - Value for the "src" hash parameter.
   * @param {string}  [options.targetLanguage] - Value for the "trg" hash parameter.
   * @param {string}  [options.sourceText]     - Value for the "text" hash parameter.
   * @returns {Promise<void>}
   */
  async updateCurrentPageHash({
    sourceLanguage,
    targetLanguage,
    sourceText,
  } = {}) {
    const searchParams = new URLSearchParams();

    if (sourceLanguage) {
      searchParams.set("src", sourceLanguage);
    }

    if (targetLanguage) {
      searchParams.set("trg", targetLanguage);
    }

    if (sourceText) {
      searchParams.set("text", sourceText);
    }

    const hashString = searchParams.toString();
    const hash = hashString || "src=detect";
    const url = new URL("about:translations");
    url.hash = hash;
    logAction(url);

    await this.#setCurrentPageHash(hash);
  }

  /**
   * Clears the hash in the current about:translations document.
   *
   * @returns {Promise<void>}
   */
  async clearCurrentPageHash() {
    const url = new URL("about:translations");
    logAction(url);
    await this.#setCurrentPageHash("");
  }

  /**
   * Updates the hash in the current about:translations document and waits for a hashchange.
   *
   * @param {string} hash
   * @returns {Promise<void>}
   */
  async #setCurrentPageHash(hash) {
    await this.#runInPage(
      async (_, { hash }) => {
        const { window } = content;
        const nextHash = hash ? `#${hash}` : "";

        if (window.location.hash === nextHash) {
          return;
        }

        const hashChange = new Promise(resolve => {
          window.addEventListener("hashchange", resolve, { once: true });
        });

        window.location.hash = hash;
        await hashChange;
      },
      { hash }
    );
  }

  /**
   * Sets a new delay timer for the throttle on reacting to input.
   *
   * @param {number} ms - The delay milliseconds.
   * @returns {Promise<void>}
   */
  async setThrottleDelay(ms) {
    logAction(ms);
    try {
      await this.#runInPage(
        (_, { ms }) => {
          const { window } = content;
          Cu.waiveXrays(window).THROTTLE_DELAY = ms;
        },
        { ms }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Sets a new delay timer for the debounce on reacting to input.
   *
   * @param {number} ms - The delay milliseconds.
   * @returns {Promise<void>}
   */
  async setDebounceDelay(ms) {
    logAction(ms);
    try {
      await this.#runInPage(
        (_, { ms }) => {
          const { window } = content;
          Cu.waiveXrays(window).DEBOUNCE_DELAY = ms;
        },
        { ms }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Sets a new delay timer for translation-request telemetry throttling.
   *
   * @param {number} ms - The delay milliseconds.
   * @returns {Promise<void>}
   */
  async setTranslationRequestTelemetryThrottleDelay(ms) {
    logAction(ms);
    try {
      await this.#runInPage(
        (_, { ms }) => {
          const { window } = content;
          Cu.waiveXrays(window).TRANSLATION_REQUEST_TELEMETRY_THROTTLE_DELAY =
            ms;
        },
        { ms }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Clears translation-request telemetry throttling in the about:translations UI.
   *
   * @returns {Promise<void>}
   */
  async clearTranslationRequestTelemetryThrottle() {
    logAction();
    try {
      await this.#runInPage(() => {
        const { window } = content;
        const aboutTranslations = Cu.waiveXrays(window).aboutTranslations;
        if (!aboutTranslations) {
          throw new Error("aboutTranslations instance is unavailable.");
        }
        aboutTranslations.testClearTranslationRequestTelemetryThrottle();
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Manually resolves pending RemoteSettings download requests during tests.
   *
   * @param {number} count
   */
  async resolveDownloads(count) {
    if (this.#autoDownloadFromRemoteSettings) {
      throw new Error(
        "Cannot manually resolve downloads when autoDownloadFromRemoteSettings is enabled."
      );
    }
    try {
      await this.#resolveDownloads(count);
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Manually rejects pending RemoteSettings download requests during tests.
   *
   * @param {number} requestCount
   */
  async rejectDownloads(requestCount) {
    if (this.#autoDownloadFromRemoteSettings) {
      throw new Error(
        "Cannot manually reject downloads when autoDownloadFromRemoteSettings is enabled."
      );
    }
    try {
      await this.#rejectDownloads(requestCount);
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Sets the source-language selector to the given value in the about:translations UI.
   *
   * @param {string} language
   */
  async setSourceLanguageSelectorValue(language) {
    logAction(language);
    try {
      await this.#runInPage(
        (selectors, { language }) => {
          const selector = Cu.waiveXrays(
            content.document.querySelector(selectors.sourceLanguageSelector)
          );
          selector.value = language;
          selector.dispatchEvent(
            new content.Event("change", { bubbles: true })
          );
        },
        { language }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Sets the target-language selector to the given value in the about:translations UI.
   *
   * @param {string} language
   */
  async setTargetLanguageSelectorValue(language) {
    logAction(language);
    try {
      await this.#runInPage(
        (selectors, { language }) => {
          const selector = Cu.waiveXrays(
            content.document.querySelector(selectors.targetLanguageSelector)
          );
          selector.value = language;
          selector.dispatchEvent(
            new content.Event("change", { bubbles: true })
          );
        },
        { language }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Sets the source textarea value in the about:translations UI.
   *
   * @param {string} value
   */
  async setSourceTextAreaValue(value) {
    logAction(value);
    try {
      await this.#runInPage(
        (selectors, { value }) => {
          const textArea = content.document.querySelector(
            selectors.sourceSectionTextArea
          );
          textArea.value = value;
          textArea.dispatchEvent(new content.Event("input"));
        },
        { value }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Overrides the duration that the copy button remains in its copied state.
   *
   * @param {number} ms
   */
  async setCopyButtonResetDelay(ms) {
    try {
      await this.#runInPage(
        (_, { delayMs }) => {
          const { window } = content;
          const aboutTranslations = Cu.waiveXrays(window).aboutTranslations;
          if (!aboutTranslations) {
            throw new Error("aboutTranslations instance is unavailable.");
          }
          aboutTranslations.testSetCopyButtonResetDelayOverride(delayMs);
        },
        { delayMs: ms }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Clears any copy button reset delay override.
   */
  async clearCopyButtonResetDelayOverride() {
    try {
      await this.#runInPage(() => {
        const { window } = content;
        const aboutTranslations = Cu.waiveXrays(window).aboutTranslations;
        if (!aboutTranslations) {
          throw new Error("aboutTranslations instance is unavailable.");
        }
        aboutTranslations.testSetCopyButtonResetDelayOverride(null);
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Enables or disables manual copy button resets for testing.
   *
   * When enabled, tests are expected to reset the copy button manually.
   * When disabled (default), the copy button resets based on its reset timeout.
   *
   * @param {boolean} enabled
   */
  async setManualCopyButtonResetEnabled(enabled) {
    logAction(enabled);
    try {
      await this.#runInPage(
        (_, { enabled }) => {
          const { window } = content;
          const aboutTranslations = Cu.waiveXrays(window).aboutTranslations;
          if (!aboutTranslations) {
            throw new Error("aboutTranslations instance is unavailable.");
          }
          aboutTranslations.testSetManualCopyButtonResetEnabled(enabled);
        },
        { enabled }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Manually resets the copy button.
   */
  async resetCopyButton() {
    logAction();
    try {
      await this.#runInPage(() => {
        const { window } = content;
        const aboutTranslations = Cu.waiveXrays(window).aboutTranslations;
        if (!aboutTranslations) {
          throw new Error("aboutTranslations instance is unavailable.");
        }
        aboutTranslations.testResetCopyButton();
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Invokes the swap-languages button in the about:translations UI.
   *
   * @param {object} options
   * @param {string} options.selectorKey
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   * @returns {Promise<void>}
   */
  async #invokeElement({ selectorKey, action = "click" }) {
    await this.#runInPage(
      async (selectors, options) => {
        const { selectorKey, action } = options;
        const selector = selectors[selectorKey];
        if (!selector) {
          throw new Error(`Unsupported selector key: ${selectorKey}`);
        }

        const element = content.document.querySelector(selector);
        if (!element) {
          throw new Error(
            `Could not find element for selector key: ${selectorKey}`
          );
        }

        const EventUtils = ContentTaskUtils.getEventUtils(content);

        if (action === "click") {
          EventUtils.synthesizeMouseAtCenter(element, {}, content);
          return;
        }

        const focusTarget = element.buttonEl ?? element;
        focusTarget.focus({ focusVisible: true });
        await new Promise(resolve =>
          content.requestAnimationFrame(() =>
            content.requestAnimationFrame(resolve)
          )
        );
        if (action === "enter") {
          EventUtils.synthesizeKey("KEY_Enter", {}, content);
          return;
        }

        if (action === "space") {
          EventUtils.synthesizeKey(" ", {}, content);
          return;
        }

        throw new Error(`Unsupported action: ${action}`);
      },
      {
        selectorKey,
        action,
      }
    );
  }

  /**
   * Invokes the swap-languages button in the about:translations UI.
   *
   * @param {object} [options={}]
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   */
  async invokeSwapLanguagesButton({ action = "click" } = {}) {
    logAction(action);
    try {
      await this.#invokeElement({
        selectorKey: "swapLanguagesButton",
        action,
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Invokes the copy button in the about:translations UI.
   *
   * @param {object} [options={}]
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   */
  async invokeCopyButton({ action = "click" } = {}) {
    logAction(action);
    try {
      await this.#invokeElement({
        selectorKey: "copyButton",
        action,
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Invokes the translation error retry button in the about:translations UI.
   *
   * @param {object} [options={}]
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   */
  async invokeTranslationErrorButton({ action = "click" } = {}) {
    logAction(action);
    try {
      await this.#invokeElement({
        selectorKey: "translationErrorButton",
        action,
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Invokes the language-load error retry button in the about:translations UI.
   *
   * @param {object} [options={}]
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   */
  async invokeLanguageLoadErrorButton({ action = "click" } = {}) {
    logAction(action);
    try {
      await this.#invokeElement({
        selectorKey: "languageLoadErrorButton",
        action,
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Invokes the feature-blocked "unblock" button in the about:translations UI.
   *
   * @param {object} [options={}]
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   */
  async invokeUnblockFeatureButton({ action = "click" } = {}) {
    logAction(action);
    try {
      await this.#invokeElement({
        selectorKey: "unblockFeatureButton",
        action,
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Waits for the specified AboutTranslations event to fire, then returns its detail payload.
   * Rejects if the event doesn’t fire within the given time limit.
   *
   * @param {string} eventName
   * @returns {Promise<any>}
   */
  async waitForEvent(eventName) {
    const detail = await this.#runInPage(
      (_, { eventName }) => {
        const { document } = content;
        const eventPromise = new Promise(resolve => {
          document.addEventListener(
            eventName,
            event => resolve({ ...(event.detail ?? {}) }),
            { once: true }
          );
        });

        const timeoutMS = 10_000;
        const timeoutPromise = new Promise((_, reject) => {
          setTimeout(
            () =>
              reject(
                new Error(
                  `Event "${eventName}" did not fire within ${timeoutMS / 1000} seconds.`
                )
              ),
            timeoutMS
          );
        });

        return Promise.race([eventPromise, timeoutPromise]);
      },
      { eventName }
    );

    return detail;
  }

  /**
   * Waits for the translation error message to match the requested visibility.
   *
   * @param {object} options
   * @param {boolean} [options.visible=true]
   * @returns {Promise<void>}
   */
  async waitForTranslationErrorMessage({ visible = true } = {}) {
    try {
      await this.#runInPage(
        (selectors, { visible }) => {
          const { document } = content;
          const message = document.querySelector(
            selectors.translationErrorMessage
          );
          if (!message) {
            throw new Error("Could not find the translation error message.");
          }
          return ContentTaskUtils.waitForMutationCondition(
            message,
            { attributes: true, attributeFilter: ["hidden"] },
            () => message.hidden === !visible
          );
        },
        { visible }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Waits for the detected-language unsupported message to match the requested visibility.
   *
   * @param {object} options
   * @param {boolean} [options.visible=true]
   * @returns {Promise<void>}
   */
  async waitForDetectedLanguageUnsupportedMessage({ visible = true } = {}) {
    try {
      await this.#runInPage(
        (selectors, { visible }) => {
          const { document } = content;
          const message = document.querySelector(
            selectors.detectedLanguageUnsupportedMessage
          );
          if (!message) {
            throw new Error(
              "Could not find the detected-language unsupported message."
            );
          }
          return ContentTaskUtils.waitForMutationCondition(
            message,
            { attributes: true, attributeFilter: ["hidden"] },
            () => message.hidden === !visible
          );
        },
        { visible }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Asserts that expected AboutTranslations events fire (with optional details)
   * and that unexpected events do not fire during as a result of the given callback.
   *
   * @param {object} [options={}]
   * @param {Array.<[string, any]>} [options.expected=[]] — An array of
   *        `[eventName, expectedDetail?]` pairs. `expectedDetail` may be:
   *          - a predicate function `(detail) => boolean`,
   *          - an exact detail object for equality comparison.
   * @param {Array.<string>} [options.unexpected=[]] — An array of event names
   *        that should *not* fire during the execution of `callback`.
   * @param {() => Promise<void>} callback — Async function to execute while
   *        listening for events.
   * @returns {Promise<void>}
   */
  async assertEvents({ expected = [], unexpected = [] } = {}, callback) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    try {
      const expectedEventWaiters = Object.fromEntries(
        expected.map(([eventName]) => [eventName, this.waitForEvent(eventName)])
      );

      const unexpectedEventMap = {};
      for (const eventName of unexpected) {
        unexpectedEventMap[eventName] = false;
        this.waitForEvent(eventName)
          .then(() => {
            unexpectedEventMap[eventName] = true;
          })
          .catch(() => {
            // The waitForEvent() timeout race triggered, which is okay
            // since we didn't expect this event to fire anyway.
          });
      }

      // Wait for one content-task spawn to guarantee that event waiters are fully registered.
      await this.#runInPage(() => {});

      await callback();

      for (const [eventName, expectedDetail] of expected) {
        const actualDetail = await expectedEventWaiters[eventName];
        const actualDetailString = JSON.stringify(actualDetail ?? {});

        if (typeof expectedDetail === "function") {
          ok(
            expectedDetail(actualDetail ?? {}),
            `Expected detail for "${eventName}" predicate to pass. Got ${actualDetailString}.`
          );
        } else {
          is(
            actualDetailString,
            JSON.stringify(expectedDetail ?? {}),
            `Expected detail for "${eventName}" to match.`
          );
        }
      }

      await TestUtils.waitForTick();
      await TestUtils.waitForTick();

      for (const eventName of unexpected) {
        if (unexpectedEventMap[eventName]) {
          throw new Error(
            `Unexpected event ${eventName} fired during callback.`
          );
        }
      }
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);
  }

  /**
   * Asserts properties of the source textarea.
   *
   * @param {object} options
   * @param {string}  [options.value]
   * @param {boolean} [options.showsPlaceholder]
   * @param {string}  [options.scriptDirection]
   * @param {string|null} [options.languageTag]
   * @returns {Promise<void>}
   */
  async assertSourceTextArea(options = {}) {
    const { value, showsPlaceholder, scriptDirection, languageTag } = options;
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(
        selectors => {
          const textArea = content.document.querySelector(
            selectors.sourceSectionTextArea
          );
          return {
            hasPlaceholder: textArea.hasAttribute("placeholder"),
            actualValue: textArea.value,
            actualScriptDirection: textArea.getAttribute("dir"),
            actualLanguageTag: textArea.getAttribute("lang"),
          };
        },
        { value, showsPlaceholder, scriptDirection, languageTag }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const {
      hasPlaceholder,
      actualValue,
      actualScriptDirection,
      actualLanguageTag,
    } = pageResult;

    if (showsPlaceholder !== undefined) {
      if (showsPlaceholder) {
        ok(hasPlaceholder, "Expected placeholder on source textarea.");
        is(
          actualValue,
          "",
          "Expected source textarea to have no value when showing placeholder."
        );
      } else {
        ok(actualValue, "Expected source textarea to have a value.");
      }
    }

    if (value !== undefined) {
      is(
        actualValue,
        value,
        `Expected source textarea value to be "${value}", but got "${actualValue}".`
      );
    }

    if (scriptDirection !== undefined) {
      is(
        actualScriptDirection,
        scriptDirection,
        `Expected source textarea "dir" attribute to be "${scriptDirection}", but got "${actualScriptDirection}".`
      );
    }

    if (languageTag !== undefined) {
      is(
        actualLanguageTag,
        languageTag,
        `Expected source textarea "lang" attribute to be "${languageTag}", but got "${actualLanguageTag}".`
      );
    }
  }

  /**
   * Asserts properties of the target textarea.
   *
   * @param {object} options
   * @param {string}  [options.value]
   * @param {boolean} [options.showsPlaceholder]
   * @param {string}  [options.scriptDirection]
   * @param {string|null} [options.languageTag]
   * @returns {Promise<void>}
   */
  async assertTargetTextArea(options = {}) {
    const { value, showsPlaceholder, scriptDirection, languageTag } = options;
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(
        selectors => {
          const textArea = content.document.querySelector(
            selectors.targetSectionTextArea
          );
          return {
            hasPlaceholder: textArea.hasAttribute("placeholder"),
            actualValue: textArea.value,
            actualScriptDirection: textArea.getAttribute("dir"),
            actualLanguageTag: textArea.getAttribute("lang"),
          };
        },
        { value, showsPlaceholder, scriptDirection, languageTag }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const {
      hasPlaceholder,
      actualValue,
      actualScriptDirection,
      actualLanguageTag,
    } = pageResult;

    if (showsPlaceholder !== undefined) {
      if (showsPlaceholder) {
        ok(hasPlaceholder, "Expected placeholder on target textarea.");
        is(
          actualValue,
          "",
          "Expected target textarea to have no value when showing placeholder."
        );
      } else {
        ok(actualValue, "Expected target textarea to have a value.");
      }
    }

    if (value !== undefined) {
      is(
        actualValue,
        value,
        `Expected target textarea value to be "${value}", but got "${actualValue}".`
      );
    }

    if (scriptDirection !== undefined) {
      is(
        actualScriptDirection,
        scriptDirection,
        `Expected target textarea "dir" attribute to be "${scriptDirection}", but got "${actualScriptDirection}".`
      );
    }

    if (languageTag !== undefined) {
      is(
        actualLanguageTag,
        languageTag,
        `Expected target textarea "lang" attribute to be "${languageTag}", but got "${actualLanguageTag}".`
      );
    }
  }

  /**
   * Asserts properties of the translation error message.
   *
   * @param {object} options
   * @param {boolean} [options.visible]
   * @param {boolean} [options.retryButtonEnabled]
   * @param {boolean} [options.targetTextAreaVisible]
   * @param {boolean} [options.hasErrorClass]
   * @returns {Promise<void>}
   */
  async assertTranslationErrorMessage({
    visible,
    retryButtonEnabled,
    targetTextAreaVisible,
    hasErrorClass,
  } = {}) {
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(selectors => {
        const { document, window } = content;
        const isElementVisible = selector => {
          const element = document.querySelector(selector);
          if (element?.offsetParent === null) {
            return false;
          }

          const computedStyle = window.getComputedStyle(element);
          if (!computedStyle) {
            return false;
          }

          const { display, visibility } = computedStyle;
          return !(display === "none" || visibility === "hidden");
        };

        const message = document.querySelector(
          selectors.translationErrorMessage
        );
        const retryButton = document.querySelector(
          selectors.translationErrorButton
        );
        const targetSection = document.querySelector(selectors.targetSection);

        return {
          messageExists: Boolean(message),
          retryButtonExists: Boolean(retryButton),
          targetSectionExists: Boolean(targetSection),
          messageVisible: isElementVisible(selectors.translationErrorMessage),
          targetTextAreaVisible: isElementVisible(
            selectors.targetSectionTextArea
          ),
          retryButtonDisabled: retryButton?.hasAttribute("disabled") ?? true,
          hasErrorClass:
            targetSection?.classList.contains("has-translation-error") ?? false,
        };
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const {
      messageExists,
      retryButtonExists,
      targetSectionExists,
      messageVisible,
      retryButtonDisabled,
      targetTextAreaVisible: actualTargetTextAreaVisible,
      hasErrorClass: actualHasErrorClass,
    } = pageResult;

    ok(messageExists, "Expected translation error message to be present.");
    ok(retryButtonExists, "Expected translation error button to be present.");
    ok(targetSectionExists, "Expected target section to be present.");

    if (visible !== undefined) {
      visible
        ? ok(
            messageVisible,
            "Expected translation error message to be visible."
          )
        : ok(
            !messageVisible,
            "Expected translation error message to be hidden."
          );
    }

    if (retryButtonEnabled !== undefined) {
      retryButtonEnabled
        ? ok(
            !retryButtonDisabled,
            "Expected translation error button to be enabled."
          )
        : ok(
            retryButtonDisabled,
            "Expected translation error button to be disabled."
          );
    }

    if (targetTextAreaVisible !== undefined) {
      targetTextAreaVisible
        ? ok(
            actualTargetTextAreaVisible,
            "Expected target textarea to be visible."
          )
        : ok(
            !actualTargetTextAreaVisible,
            "Expected target textarea to be hidden."
          );
    }

    if (hasErrorClass !== undefined) {
      hasErrorClass
        ? ok(actualHasErrorClass, "Expected target section error styling.")
        : ok(
            !actualHasErrorClass,
            "Expected target section error styling to be removed."
          );
    }
  }

  /**
   * Returns the current pixel heights for the source and target sections.
   *
   * @returns {Promise<{ sourceSectionHeight: number, targetSectionHeight: number }>}
   */
  async getSectionHeights() {
    await doubleRaf(document);

    let pageResult = {
      sourceSectionHeight: NaN,
      targetSectionHeight: NaN,
    };

    try {
      pageResult = await this.#runInPage(selectors => {
        const { document } = content;
        const sourceSection = document.querySelector(selectors.sourceSection);
        const targetSection = document.querySelector(selectors.targetSection);

        return {
          sourceSectionHeight: Math.round(
            sourceSection.getBoundingClientRect().height
          ),
          targetSectionHeight: Math.round(
            targetSection.getBoundingClientRect().height
          ),
        };
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    return pageResult;
  }

  /**
   * Asserts properties of the source-language selector.
   *
   * @param {object}   options
   * @param {string}   [options.value]
   * @param {string[]} [options.options]
   * @param {string}   [options.detectedLanguage]
   * @returns {Promise<void>}
   */
  async assertSourceLanguageSelector({
    value,
    options,
    detectedLanguage,
  } = {}) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(selectors => {
        const selector = Cu.waiveXrays(
          content.document.querySelector(selectors.sourceLanguageSelector)
        );
        const detectOptionElement = content.document.querySelector(
          selectors.detectLanguageOption
        );
        return {
          actualValue: selector.value,
          optionValues: Array.from(selector.querySelectorAll("moz-option")).map(
            option => option.getAttribute("value")
          ),
          detectLanguageAttribute:
            detectOptionElement?.getAttribute("language") ?? null,
        };
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const { actualValue, optionValues, detectLanguageAttribute } = pageResult;

    if (value !== undefined) {
      is(
        actualValue,
        value,
        `Expected source-language selector value to be "${value}", but got "${actualValue}".`
      );
    }

    if (Array.isArray(options)) {
      is(
        optionValues.length,
        options.length,
        `Expected source-language selector to have ${options.length} options, but got ${optionValues.length}.`
      );
      for (let index = 0; index < options.length; index++) {
        is(
          optionValues[index],
          options[index],
          `Expected source-language selector option at index ${index} to be "${options[index]}", but got "${optionValues[index]}".`
        );
      }
    }

    if (detectedLanguage !== undefined) {
      is(
        actualValue,
        "detect",
        `With detectedLanguage set, expected selector value to be "detect", but got "${actualValue}".`
      );
      is(
        detectLanguageAttribute,
        detectedLanguage,
        `Expected detect-language option "language" attribute to be "${detectedLanguage}", but got "${detectLanguageAttribute}".`
      );
    }
  }

  /**
   * Asserts properties of the detected-language unsupported message.
   *
   * @param {object} options
   * @param {boolean} [options.visible]
   * @param {boolean} [options.sourceTextAreaVisible]
   * @param {boolean} [options.targetTextAreaVisible]
   * @param {string} [options.learnMoreSupportPage]
   * @returns {Promise<void>}
   */
  async assertDetectedLanguageUnsupportedMessage({
    visible,
    sourceTextAreaVisible,
    targetTextAreaVisible,
    learnMoreSupportPage,
  } = {}) {
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(selectors => {
        const { document, window } = content;
        const isElementVisible = selector => {
          const element = document.querySelector(selector);
          if (element?.offsetParent === null) {
            return false;
          }

          const computedStyle = window.getComputedStyle(element);
          if (!computedStyle) {
            return false;
          }

          const { display, visibility } = computedStyle;
          return !(display === "none" || visibility === "hidden");
        };

        const message = document.querySelector(
          selectors.detectedLanguageUnsupportedMessage
        );
        const heading = document.querySelector(
          selectors.detectedLanguageUnsupportedHeading
        );
        const link = document.querySelector(
          selectors.detectedLanguageUnsupportedLearnMoreLink
        );

        return {
          messageExists: Boolean(message),
          headingExists: Boolean(heading),
          linkExists: Boolean(link),
          messageVisible: isElementVisible(
            selectors.detectedLanguageUnsupportedMessage
          ),
          sourceTextAreaVisible: isElementVisible(
            selectors.sourceSectionTextArea
          ),
          targetTextAreaVisible: isElementVisible(
            selectors.targetSectionTextArea
          ),
          learnMoreSupportPage: link?.getAttribute("support-page") ?? "",
        };
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const {
      messageExists,
      headingExists,
      linkExists,
      messageVisible,
      sourceTextAreaVisible: actualSourceTextAreaVisible,
      targetTextAreaVisible: actualTargetTextAreaVisible,
      learnMoreSupportPage: actualLearnMoreSupportPage,
    } = pageResult;

    ok(
      messageExists,
      "Expected detected-language unsupported message to be present."
    );
    ok(headingExists, "Expected unsupported message heading to be present.");
    ok(
      linkExists,
      "Expected unsupported message learn-more link to be present."
    );

    if (visible !== undefined) {
      visible
        ? ok(
            messageVisible,
            "Expected detected-language unsupported message to be visible."
          )
        : ok(
            !messageVisible,
            "Expected detected-language unsupported message to be hidden."
          );
    }

    if (sourceTextAreaVisible !== undefined) {
      sourceTextAreaVisible
        ? ok(
            actualSourceTextAreaVisible,
            "Expected source textarea to be visible."
          )
        : ok(
            !actualSourceTextAreaVisible,
            "Expected source textarea to be hidden."
          );
    }

    if (targetTextAreaVisible !== undefined) {
      targetTextAreaVisible
        ? ok(
            actualTargetTextAreaVisible,
            "Expected target textarea to be visible."
          )
        : ok(
            !actualTargetTextAreaVisible,
            "Expected target textarea to be hidden."
          );
    }

    if (learnMoreSupportPage !== undefined) {
      is(
        actualLearnMoreSupportPage,
        learnMoreSupportPage,
        `Expected unsupported message learn-more support-page to be "${learnMoreSupportPage}", but got "${actualLearnMoreSupportPage}".`
      );
    }
  }

  /**
   * Asserts properties of the target-language selector.
   *
   * @param {object}   options
   * @param {string}   [options.value]
   * @param {string[]} [options.options]
   * @returns {Promise<void>}
   */
  async assertTargetLanguageSelector({ value, options } = {}) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(
        selectors => {
          const selector = Cu.waiveXrays(
            content.document.querySelector(selectors.targetLanguageSelector)
          );
          const optionValues = Array.from(
            selector.querySelectorAll("moz-option")
          ).map(option => option.getAttribute("value"));
          return {
            actualValue: selector.value,
            optionValues,
          };
        },
        { value, options }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const { actualValue, optionValues } = pageResult;

    if (value !== undefined) {
      is(
        actualValue,
        value,
        `Expected target-language selector value to be "${value}", but got "${actualValue}".`
      );
    }

    if (Array.isArray(options)) {
      is(
        optionValues.length,
        options.length,
        `Expected target-language selector to have ${options.length} options, but got ${optionValues.length}.`
      );
      for (let index = 0; index < options.length; index++) {
        is(
          optionValues[index],
          options[index],
          `Expected target-language selector option at index ${index} to be "${options[index]}", but got "${optionValues[index]}".`
        );
      }
    }
  }

  /**
   * Asserts properties of the detect-language option in the source-language selector.
   *
   * @param {object}  options
   * @param {boolean} [options.isSelected]
   * @param {boolean} [options.defaultValue]
   * @param {string}  [options.language]
   * @returns {Promise<void>}
   */
  async assertDetectLanguageOption({
    isSelected,
    defaultValue,
    language,
  } = {}) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    if (language !== undefined && defaultValue) {
      throw new Error(
        "assertDetectLanguageOption: `language` and `defaultValue: true` are mutually exclusive."
      );
    }

    if (isSelected !== undefined) {
      if (isSelected) {
        await this.assertSourceLanguageSelector({ value: "detect" });
      } else {
        let pageResult = {};
        try {
          pageResult = await this.#runInPage(selectors => {
            const selector = Cu.waiveXrays(
              content.document.querySelector(selectors.sourceLanguageSelector)
            );
            return { actualValue: selector.value };
          });
        } catch (error) {
          AboutTranslationsTestUtils.#reportTestFailure(error);
        }

        const { actualValue } = pageResult;
        Assert.notStrictEqual(
          actualValue,
          "detect",
          `Expected source-language selector value not to be "detect", but got "${actualValue}".`
        );
      }
    }

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(
        selectors => {
          const detectOptionElement = content.document.querySelector(
            selectors.detectLanguageOption
          );
          return {
            localizationId: detectOptionElement?.getAttribute("data-l10n-id"),
            languageAttributeValue:
              detectOptionElement?.getAttribute("language"),
          };
        },
        { defaultValue, language }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const { localizationId, languageAttributeValue } = pageResult;

    if (defaultValue !== undefined) {
      const expectedIdentifier = defaultValue
        ? "about-translations-detect-default-label"
        : "about-translations-detect-language-label";
      is(
        localizationId,
        expectedIdentifier,
        `Expected detect-language option "data-l10n-id" to be "${expectedIdentifier}", but got "${localizationId}".`
      );
    }

    if (language !== undefined) {
      is(
        languageAttributeValue,
        language,
        `Expected detect-language option "language" attribute to be "${language}", but got "${languageAttributeValue}".`
      );
    }
  }

  /**
   * Asserts properties of the the swap-languages button.
   *
   * @param {object} options
   * @param {boolean} [options.enabled]
   * @returns {Promise<void>}
   */
  async assertSwapLanguagesButton({ enabled } = {}) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    let pageResult = {};
    try {
      pageResult = await this.#runInPage(
        selectors => {
          const button = content.document.querySelector(
            selectors.swapLanguagesButton
          );
          return {
            isDisabled: button.hasAttribute("disabled"),
          };
        },
        { enabled }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    const { isDisabled } = pageResult;

    if (enabled !== undefined) {
      if (enabled) {
        ok(!isDisabled, "Expected swap-languages button to be enabled.");
      } else {
        ok(isDisabled, "Expected swap-languages button to be disabled.");
      }
    }
  }

  /**
   * Asserts the tab order across the provided selectors, starting from the
   * first selector focused directly.
   *
   * @param {string[]} selectorKeys
   * @returns {Promise<void>}
   */
  async assertTabFocusOrder(selectorKeys) {
    await doubleRaf(document);
    logAction(selectorKeys.join(", "));

    try {
      await this.#runInPage(
        async (selectors, { selectorKeys }) => {
          const { document } = content;
          const EventUtils = ContentTaskUtils.getEventUtils(content);
          const doubleRaf = () =>
            new Promise(resolve => {
              content.requestAnimationFrame(() => {
                content.requestAnimationFrame(resolve);
              });
            });

          const elements = selectorKeys.map(selectorKey => {
            const selector = selectors[selectorKey];
            const element = document.querySelector(selector);
            if (!element) {
              throw new Error(`Could not find element for "${selectorKey}".`);
            }
            return element;
          });

          const activeElementAtStart = document.activeElement;

          elements[0].focus();
          await doubleRaf();

          for (let index = 0; index < elements.length; index++) {
            const element = elements[index];
            if (document.activeElement !== element) {
              throw new Error(
                `Expected "${selectorKeys[index]}" (#${element.id}) to have focus.`
              );
            }

            if (index + 1 < elements.length) {
              EventUtils.synthesizeKey("KEY_Tab", {}, content);
              await doubleRaf();
            }
          }

          activeElementAtStart?.focus?.();
        },
        { selectorKeys }
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Retrieves the current state of the copy button.
   *
   * @returns {Promise<{exists: boolean, isDisabled: boolean, isCopied: boolean, l10nId: string}>}
   */
  async getCopyButtonState() {
    await doubleRaf(document);

    try {
      return await this.#runInPage(selectors => {
        const { document } = content;
        const button = document.querySelector(selectors.copyButton);
        return {
          exists: !!button,
          isDisabled: button?.hasAttribute("disabled") ?? true,
          isCopied: button?.classList.contains("copied") ?? false,
          l10nId: button?.getAttribute("data-l10n-id") ?? "",
        };
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    return {
      exists: false,
      isDisabled: true,
      isCopied: false,
      l10nId: "",
    };
  }

  /**
   * Asserts properties of the copy button.
   *
   * @param {object} options
   * @param {boolean} [options.visible=true]
   * @param {boolean} [options.enabled=false]
   * @param {boolean} [options.copied]
   * @param {string} [options.l10nId]
   * @returns {Promise<void>}
   */
  async assertCopyButton({
    visible = true,
    enabled = false,
    copied,
    l10nId,
  } = {}) {
    const {
      exists,
      isDisabled,
      isCopied,
      l10nId: actualL10nId,
    } = await this.getCopyButtonState();

    ok(exists, "Expected copy button to be present.");

    await this.assertIsVisible({
      pageHeader: true,
      mainUserInterface: true,
      sourceLanguageSelector: true,
      targetLanguageSelector: true,
      copyButton: visible,
      swapLanguagesButton: true,
      sourceSectionTextArea: true,
      targetSectionTextArea: true,
    });

    if (enabled !== undefined) {
      if (enabled) {
        ok(!isDisabled, "Expected copy button to be enabled.");
      } else {
        ok(isDisabled, "Expected copy button to be disabled.");
      }
    }

    if (copied !== undefined) {
      if (copied) {
        ok(isCopied, "Expected copy button to show the copied state.");
      } else {
        ok(!isCopied, "Expected copy button to show the default state.");
      }
    }

    if (l10nId !== undefined) {
      is(
        actualL10nId,
        l10nId,
        `Expected copy button to use the "${l10nId}" localization id.`
      );
    }
  }

  /**
   * Retrieves the state of the clear button.
   *
   * @returns {Promise<{exists: boolean, hidden: boolean, tabIndex: string | null}>}
   */
  async getSourceClearButtonState() {
    await doubleRaf(document);

    try {
      return await this.#runInPage(selectors => {
        const button = content.document.querySelector(selectors.clearButton);
        return {
          exists: !!button,
          hidden: button?.hasAttribute("hidden") ?? true,
          tabIndex: button?.getAttribute("tabindex") ?? null,
        };
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    return {
      exists: false,
      hidden: true,
      tabIndex: null,
    };
  }

  /**
   * Asserts properties of the clear button.
   *
   * @param {object} options
   * @param {boolean} [options.visible=false]
   * @param {string}  [options.tabIndex="-1"]
   * @returns {Promise<void>}
   */
  async assertSourceClearButton({ visible = false, tabIndex = "-1" } = {}) {
    const {
      exists,
      hidden,
      tabIndex: actualTabIndex,
    } = await this.getSourceClearButtonState();

    ok(exists, "Expected clear button to be present.");

    if (visible) {
      ok(!hidden, "Expected clear button to be visible.");
    } else {
      ok(hidden, "Expected clear button to be hidden.");
    }

    if (tabIndex !== undefined) {
      is(
        actualTabIndex,
        tabIndex,
        `Expected clear button tabindex to be "${tabIndex}".`
      );
    }
  }

  /**
   * Invokes the clear button.
   *
   * @param {object} [options={}]
   * @param {AboutTranslationsInvokeAction} [options.action="click"]
   *
   * @returns {Promise<void>}
   */
  async invokeClearButton({ action = "click" } = {}) {
    await doubleRaf(document);
    logAction(action);
    try {
      await this.#invokeElement({
        selectorKey: "clearButton",
        action,
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Undoes the most recent user-input edit to the source-section
   * text area, simulating the behavior of invoking `Ctrl/Cmd + Z`.
   *
   * @returns {Promise<void>}
   */
  async invokeSourceTextAreaUndoAction() {
    logAction();
    await doubleRaf(document);
    try {
      await this.#runInPage(selectors => {
        const sourceTextArea = content.document.querySelector(
          selectors.sourceSectionTextArea
        );
        sourceTextArea.focus();
      });
      await BrowserTestUtils.synthesizeKey(
        "z",
        { accelKey: true },
        this.#browser
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Retrieves the current value of the target textarea.
   *
   * @returns {Promise<string>}
   */
  async getTargetTextAreaValue() {
    await doubleRaf(document);
    try {
      return await this.#runInPage(selectors => {
        const textarea = content.document.querySelector(
          selectors.targetSectionTextArea
        );
        return textarea?.value ?? "";
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
    return "";
  }

  /**
   * Asserts that the target textarea shows the translating placeholder.
   *
   * @returns {Promise<void>}
   */
  async assertTranslatingPlaceholder() {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    let actualValue;
    try {
      actualValue = await this.#runInPage(selectors => {
        const textarea = content.document.querySelector(
          selectors.targetSectionTextArea
        );
        return textarea.value;
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    is(
      actualValue,
      "Translating…",
      `Expected target textarea to show "Translating…", but got "${actualValue}".`
    );
  }

  /**
   * Asserts that a translation completes with expected text.
   *
   * @param {object} options
   * @param {string} [options.sourceLanguage]   - Explicit source language.
   * @param {string} [options.detectedLanguage] - Language detected when the selector is set to "detect".
   * @param {string} options.targetLanguage
   * @param {string} options.sourceText
   * @returns {Promise<void>}
   */
  async assertTranslatedText({
    sourceLanguage,
    detectedLanguage,
    targetLanguage,
    sourceText,
  }) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    if (sourceLanguage !== undefined && detectedLanguage !== undefined) {
      throw new Error(
        "assertTranslatedText: sourceLanguage and detectedLanguage are mutually exclusive assertion options."
      );
    }

    if (detectedLanguage !== undefined) {
      await this.assertSourceLanguageSelector({ detectedLanguage });
    } else {
      await this.assertSourceLanguageSelector({ value: sourceLanguage });
    }

    await this.assertTargetLanguageSelector({ value: targetLanguage });
    await this.assertSourceTextArea({ value: sourceText });

    const actualSourceLanguage = detectedLanguage ?? sourceLanguage;
    const expectedValue =
      actualSourceLanguage === targetLanguage
        ? // Expect a passthrough translation if the source and target are the same.
          sourceText
        : // Otherwise it will have a full translation with the mock translator.
          `${sourceText.toUpperCase()} [${actualSourceLanguage} to ${targetLanguage}]`;

    let actualValue;
    try {
      actualValue = await this.#runInPage(selectors => {
        const textarea = content.document.querySelector(
          selectors.targetSectionTextArea
        );
        return textarea.value;
      });
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    is(
      actualValue,
      expectedValue,
      `Expected translated text to be "${expectedValue}", but got "${actualValue}".`
    );
  }

  /**
   * Asserts that the UI values and URL parameters all match
   * the provided arguments.
   *
   * @param {object}  options
   * @param {string} [options.sourceLanguage="detect"] - Expected value for the source-language selector and “src” URL parameter.
   * @param {string} [options.targetLanguage=""]       - Expected value for the target-language selector and “trg” URL parameter.
   * @param {string} [options.sourceText=""]           - Expected value for the source textarea and “text” URL parameter.
   * @returns {Promise<void>}
   */
  async assertURLMatchesUI({
    sourceLanguage = "detect",
    targetLanguage = "",
    sourceText = "",
  } = {}) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    try {
      // First verify that the UI controls contain the expected values.
      await this.assertSourceLanguageSelector({ value: sourceLanguage });
      await this.assertTargetLanguageSelector({ value: targetLanguage });
      await this.assertSourceTextArea({ value: sourceText });

      // Then inspect the URL from within the content page.
      const { href, sourceParam, targetParam, textParam } =
        await this.#runInPage(() => {
          const { location } = content.window;
          const currentURL = new URL(location.href);
          const hashSubstring = currentURL.hash.startsWith("#")
            ? currentURL.hash.slice(1)
            : currentURL.hash;
          const urlSearchParams = new URLSearchParams(hashSubstring);
          return {
            href: currentURL.href,
            sourceParam: urlSearchParams.get("src") ?? "detect",
            targetParam: urlSearchParams.get("trg") ?? "",
            textParam: urlSearchParams.get("text") ?? "",
          };
        });

      // Assert individual hash parameters.
      is(
        sourceParam,
        sourceLanguage,
        `Expected URL parameter "src" to be "${sourceLanguage}", but got "${sourceParam}".`
      );
      is(
        targetParam,
        targetLanguage,
        `Expected URL parameter "trg" to be "${targetLanguage}", but got "${targetParam}".`
      );
      is(
        textParam,
        sourceText,
        `Expected URL parameter "text" to be "${sourceText}", but got "${textParam}".`
      );

      const expectedURL = new URL("about:translations");
      const expectedParams = new URLSearchParams();

      if (sourceLanguage) {
        expectedParams.set("src", sourceLanguage);
      }

      if (targetLanguage) {
        expectedParams.set("trg", targetLanguage);
      }

      if (sourceText) {
        expectedParams.set("text", sourceText);
      }

      expectedURL.hash = expectedParams.toString();

      is(
        href,
        expectedURL.href,
        `Expected full URL to be "${expectedURL.href}", but got "${href}".`
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }

  /**
   * Returns the current selected-browser session history entry count.
   *
   * @returns {number}
   */
  getHistoryLength() {
    try {
      return this.#browser.browsingContext?.sessionHistory?.count ?? NaN;
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }

    return NaN;
  }

  /**
   * Asserts that the content-page history length matches the expected value.
   *
   * @param {object} options
   * @param {number} options.expectedLength
   */
  assertHistoryLength({ expectedLength }) {
    const actualLength = this.getHistoryLength();
    is(
      actualLength,
      expectedLength,
      "Expected history length to match the expected length."
    );
  }

  /**
   * Asserts visibility of each element based on the provided options.
   *
   * @param {object}  options
   * @param {boolean} [options.pageHeader=false]
   * @param {boolean} [options.mainUserInterface=false]
   * @param {boolean} [options.sourceLanguageSelector=false]
   * @param {boolean} [options.targetLanguageSelector=false]
   * @param {boolean} [options.clearButton=undefined]
   * The clear button visibility is automatically determined from source text if undefined.
   * @param {boolean} [options.copyButton=false]
   * @param {boolean} [options.swapLanguagesButton=false]
   * @param {boolean} [options.sourceSectionTextArea=false]
   * @param {boolean} [options.targetSectionTextArea=false]
   * @param {boolean} [options.detectedLanguageUnsupportedMessage=false]
   * @param {boolean} [options.translationErrorMessage=false]
   * @param {boolean} [options.unsupportedInfoMessage=false]
   * @param {boolean} [options.policyDisabledInfoMessage=false]
   * @param {boolean} [options.featureBlockedInfoMessage=false]
   * @param {boolean} [options.languageLoadErrorMessage=false]
   * @returns {Promise<void>}
   */
  async assertIsVisible({
    pageHeader = false,
    mainUserInterface = false,
    sourceLanguageSelector = false,
    targetLanguageSelector = false,
    clearButton = undefined,
    copyButton = false,
    swapLanguagesButton = false,
    sourceSectionTextArea = false,
    targetSectionTextArea = false,
    detectedLanguageUnsupportedMessage = false,
    translationErrorMessage = false,
    unsupportedInfoMessage = false,
    policyDisabledInfoMessage = false,
    featureBlockedInfoMessage = false,
    languageLoadErrorMessage = false,
  } = {}) {
    // This helps the test visually render at each step without significantly slowing test speed.
    await doubleRaf(document);

    try {
      if (clearButton === undefined) {
        const sourceTextAreaValue = await this.#runInPage(selectors => {
          const sourceTextArea = content.document.querySelector(
            selectors.sourceSectionTextArea
          );
          return sourceTextArea?.value ?? "";
        });
        clearButton = Boolean(sourceTextAreaValue.trim());
      }

      const visibilityMap = await this.#runInPage(selectors => {
        const { document, window } = content;
        const isElementVisible = selector => {
          const element = document.querySelector(selector);
          if (element.offsetParent === null) {
            return false;
          }

          const computedStyle = window.getComputedStyle(element);
          if (!computedStyle) {
            return false;
          }

          const { display, visibility } = computedStyle;
          return !(display === "none" || visibility === "hidden");
        };

        return {
          pageHeader: isElementVisible(selectors.pageHeader),
          mainUserInterface: isElementVisible(selectors.mainUserInterface),
          sourceLanguageSelector: isElementVisible(
            selectors.sourceLanguageSelector
          ),
          targetLanguageSelector: isElementVisible(
            selectors.targetLanguageSelector
          ),
          clearButton: isElementVisible(selectors.clearButton),
          copyButton: isElementVisible(selectors.copyButton),
          swapLanguagesButton: isElementVisible(selectors.swapLanguagesButton),
          sourceSectionTextArea: isElementVisible(
            selectors.sourceSectionTextArea
          ),
          targetSectionTextArea: isElementVisible(
            selectors.targetSectionTextArea
          ),
          detectedLanguageUnsupportedMessage: isElementVisible(
            selectors.detectedLanguageUnsupportedMessage
          ),
          translationErrorMessage: isElementVisible(
            selectors.translationErrorMessage
          ),
          unsupportedInfoMessage: isElementVisible(
            selectors.unsupportedInfoMessage
          ),
          policyDisabledInfoMessage: isElementVisible(
            selectors.policyDisabledInfoMessage
          ),
          featureBlockedInfoMessage: isElementVisible(
            selectors.featureBlockedInfoMessage
          ),
          languageLoadErrorMessage: isElementVisible(
            selectors.languageLoadErrorMessage
          ),
        };
      });

      const assertVisibility = (
        expectedVisibility,
        actualVisibility,
        label
      ) => {
        expectedVisibility
          ? ok(actualVisibility, `Expected ${label} to be visible.`)
          : ok(!actualVisibility, `Expected ${label} to be hidden.`);
      };

      assertVisibility(pageHeader, visibilityMap.pageHeader, "page header");
      assertVisibility(
        mainUserInterface,
        visibilityMap.mainUserInterface,
        "main user interface"
      );
      assertVisibility(
        sourceLanguageSelector,
        visibilityMap.sourceLanguageSelector,
        "source-language selector"
      );
      assertVisibility(
        targetLanguageSelector,
        visibilityMap.targetLanguageSelector,
        "target-language selector"
      );
      assertVisibility(copyButton, visibilityMap.copyButton, "copy button");
      assertVisibility(
        swapLanguagesButton,
        visibilityMap.swapLanguagesButton,
        "swap-languages button"
      );
      assertVisibility(
        sourceSectionTextArea,
        visibilityMap.sourceSectionTextArea,
        "source textarea"
      );
      assertVisibility(clearButton, visibilityMap.clearButton, "clear button");
      assertVisibility(
        targetSectionTextArea,
        visibilityMap.targetSectionTextArea,
        "target textarea"
      );
      assertVisibility(
        detectedLanguageUnsupportedMessage,
        visibilityMap.detectedLanguageUnsupportedMessage,
        "detected-language unsupported message"
      );
      assertVisibility(
        translationErrorMessage,
        visibilityMap.translationErrorMessage,
        "translation error message"
      );
      assertVisibility(
        unsupportedInfoMessage,
        visibilityMap.unsupportedInfoMessage,
        "unsupported info message"
      );
      assertVisibility(
        policyDisabledInfoMessage,
        visibilityMap.policyDisabledInfoMessage,
        "policy-disabled info message"
      );
      assertVisibility(
        featureBlockedInfoMessage,
        visibilityMap.featureBlockedInfoMessage,
        "feature-blocked info message"
      );
      assertVisibility(
        languageLoadErrorMessage,
        visibilityMap.languageLoadErrorMessage,
        "language-load error message"
      );
    } catch (error) {
      AboutTranslationsTestUtils.#reportTestFailure(error);
    }
  }
}
