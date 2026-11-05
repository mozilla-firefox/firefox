/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-check

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/**
 * @typedef {object} Lazy
 * @property {typeof console} console
 */

/** @type {Lazy} */
const lazy = /** @type {any} */ ({});

/**
 * @typedef {{ flowId: string, randomRoll: number }} FlowContext
 * @typedef {"default" | "nightly" | "beta" | "aurora" | "esr" | "release" | "unofficial"} UpdateChannel
 * @typedef {Partial<Record<UpdateChannel, number>> & { applyInAutomation?: boolean }} SampleRates
 */

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "toolkit.telemetry.translations.logLevel",
    logModule: "TranslationsTelemetry",
  });
});

/**
 * Telemetry functions for Translations actors
 */
export class TranslationsTelemetry {
  /**
   * The context of the current UI flow of events.
   *
   * This contains the flowId, which is a randomly generated UUID to
   * link telemetry UI events to one another.
   *
   * This also contains a random number in the range [0, 1) that will
   * be associated with the flowId. This allows us to skip all events
   * in the same flow based on our rate limits.
   *
   * @type {FlowContext}
   */
  static #flowContext;

  /**
   * Page loads happen quite frequently, so we want to avoid firing a telemetry
   * event for every page load that occurs. These rate limits will help keep our
   * page-load telemetry to a reasonable size, without affecting significance.
   *
   * @type {SampleRates}
   */
  // prettier-ignore
  static #PAGE_LOAD_RATE_LIMITS = {
    nightly: 1 /   1_000,
       beta: 1 /  10_000,
     aurora: 1 /  10_000,
        esr: 1 /  10_000,
    release: 1 / 100_000,
  };

  /**
   * TranslationsEngine performance metrics occur every time an active engine is
   * destroyed, but this is aggregate performance data, and we don't need to have
   * a sample for every single engine that is created.
   *
   * @type {SampleRates}
   */
  // prettier-ignore
  static #ENGINE_PERFORMANCE_RATE_LIMITS = {
       beta: 1 /   100,
     aurora: 1 /   100,
        esr: 1 /   100,
    release: 1 / 1_000,
  };

  /**
   * An array of a single unsigned 32-bit integer that we will use to generate random values.
   *
   * @type {Uint32Array}
   */
  static #RANDOM_VALUE = new Uint32Array(1);

  /**
   * Generates a random number in the range [0, 1).
   *
   * @returns {number}
   */
  static randomRoll() {
    // Generate a uniformly-distributed, random u32 in #RANDOM_VALUE.
    crypto.getRandomValues(TranslationsTelemetry.#RANDOM_VALUE);

    // Scale the random u32 to the range [0, 1).
    return TranslationsTelemetry.#RANDOM_VALUE[0] / Math.pow(2, 32);
  }

  /**
   * Logs the telemetry event to the console if enabled by toolkit.telemetry.translations.logLevel
   *
   * @param {Function} caller - The calling function.
   * @param {object} [data] - Optional data passed to telemetry.
   */
  static logEventToConsole(caller, data) {
    const flowContext = TranslationsTelemetry.getOrCreateFlowContext();
    const logId = flowContext.flowId.substring(0, 5);
    lazy.console?.log(
      `flowId[${logId}]: ${caller.name}`,
      ...(data ? [data] : [])
    );
  }

  /**
   * Determines whether the caller should skip telemetry based on the current update channel.
   *
   * Passing the optional FlowContext will use the randomRoll associated with that flowId,
   * ensuring that each event for the entire flow adheres to the same rate-limit threshold.
   *
   * @param {SampleRates} sampleRates
   *   - A mapping of update-channel names to sampling probabilities in the range [0, 1).
   *     Channels omitted from the mapping are never skipped.
   * @param {FlowContext} [flowContext]
   *   - The context that contains the flow id and a random roll for the entire flow.
   * @param {UpdateChannel} [channel]
   *   - The update channel whose sampling policy should be applied. Defaults to the
   *     application-wide update channel, but may be overridden for testing.
   *
   * @returns {boolean} True if the event should be skipped for this channel, otherwise false.
   */
  static shouldSkipSample(
    sampleRates,
    flowContext,
    channel = AppConstants.MOZ_UPDATE_CHANNEL
  ) {
    if (Cu.isInAutomation && !sampleRates.applyInAutomation) {
      // Do no skip any samples in automation, unless it is explicitly requested.
      return false;
    }

    if (channel !== AppConstants.MOZ_UPDATE_CHANNEL && !Cu.isInAutomation) {
      throw new Error(
        `Channel "${AppConstants.MOZ_UPDATE_CHANNEL}" was overridden as "${channel}" outside of testing.`
      );
    }

    const sampleRate = sampleRates[channel];
    let randomRoll;

    if (lazy.console?.shouldLog("Debug")) {
      randomRoll =
        flowContext?.randomRoll ?? TranslationsTelemetry.randomRoll();

      lazy.console.debug({
        randomRoll: randomRoll.toFixed(8),
        default: {
          skip: randomRoll >= (sampleRates.default ?? 1),
          threshold: sampleRates.default,
        },
        nightly: {
          skip: randomRoll >= (sampleRates.nightly ?? 1),
          threshold: sampleRates.nightly,
        },
        beta: {
          skip: randomRoll >= (sampleRates.beta ?? 1),
          threshold: sampleRates.beta,
        },
        esr: {
          skip: randomRoll >= (sampleRates.esr ?? 1),
          threshold: sampleRates.esr,
        },
        release: {
          skip: randomRoll >= (sampleRates.release ?? 1),
          threshold: sampleRates.release,
        },
      });
    }

    if (sampleRate === undefined) {
      // Never skip when a rate limit is unspecified.
      return false;
    }

    if (randomRoll === undefined) {
      randomRoll =
        flowContext?.randomRoll ?? TranslationsTelemetry.randomRoll();
    }

    return randomRoll >= sampleRate;
  }

  /**
   * Invokes or skips the given callback function according to the provided rate limits.
   *
   * @param {SampleRates} sampleRates
   *   - A mapping of update-channel names to sampling probabilities in the range [0, 1).
   * @param {function(FlowContext): void} callback
   *   - The function invoked when the event should be recorded.
   */
  static withRateLimits(sampleRates, callback) {
    const flowContext = TranslationsTelemetry.getOrCreateFlowContext();

    if (TranslationsTelemetry.shouldSkipSample(sampleRates, flowContext)) {
      return;
    }

    callback(flowContext);
  }

  /**
   * Telemetry functions for the Full Page Translations panel.
   *
   * @returns {FullPageTranslationsPanelTelemetry}
   */
  static fullPagePanel() {
    return FullPageTranslationsPanelTelemetry;
  }

  /**
   * Telemetry functions for the SelectTranslationsPanel.
   *
   * @returns {SelectTranslationsPanelTelemetry}
   */
  static selectTranslationsPanel() {
    return SelectTranslationsPanelTelemetry;
  }

  /**
   * Telemetry functions for the AboutTranslations page.
   *
   * @returns {AboutTranslationsPageTelemetry}
   */
  static aboutTranslationsPage() {
    return AboutTranslationsPageTelemetry;
  }

  /**
   * Forces the creation of a new Translations telemetry flowId and returns it.
   *
   * @returns {FlowContext}
   */
  static createFlowContext() {
    const flowContext = {
      flowId: crypto.randomUUID(),
      randomRoll: TranslationsTelemetry.randomRoll(),
    };

    TranslationsTelemetry.#flowContext = flowContext;

    return flowContext;
  }

  /**
   * Returns a Translations telemetry flowId by retrieving the cached value
   * if available, or creating a new one otherwise.
   *
   * @returns {FlowContext}
   */
  static getOrCreateFlowContext() {
    // If we have the flowId cached, return it.
    if (TranslationsTelemetry.#flowContext) {
      return TranslationsTelemetry.#flowContext;
    }

    // If no flowId exists, create one.
    return TranslationsTelemetry.createFlowContext();
  }

  /**
   * Records a telemetry event when the language of a page is identified for Translations.
   *
   * @param {object} data
   * @param {string | null} [data.htmlLangAttribute]
   * @param {string} data.identifiedLanguage
   * @param {boolean | null} [data.langTagsMatch]
   * @param {boolean | null} [data.isLangAttributeValid]
   * @param {number} data.extractedCodeUnits
   * @param {number} data.identificationTime
   * @param {boolean} data.confident
   */
  static onIdentifyPageLanguage(data) {
    TranslationsTelemetry.withRateLimits(
      TranslationsTelemetry.#PAGE_LOAD_RATE_LIMITS,
      () => {
        const {
          htmlLangAttribute,
          identifiedLanguage,
          langTagsMatch,
          isLangAttributeValid,
          extractedCodeUnits,
          identificationTime,
          confident,
        } = data;

        Glean.translations.identifyPageLanguage.record({
          html_lang_attribute: htmlLangAttribute,
          identified_language: identifiedLanguage,
          lang_tags_match: langTagsMatch,
          is_lang_attribute_valid: isLangAttributeValid,
          extracted_code_units: extractedCodeUnits,
          identification_time: identificationTime,
          confident,
        });
        TranslationsTelemetry.logEventToConsole(
          TranslationsTelemetry.onIdentifyPageLanguage,
          data
        );
      }
    );
  }

  /**
   * Records a telemetry event when full page translation fails.
   *
   * @param {string} errorMessage
   */
  static onError(errorMessage) {
    Glean.translations.error.record({
      flow_id: TranslationsTelemetry.getOrCreateFlowContext().flowId,
      reason: errorMessage,
    });
    TranslationsTelemetry.logEventToConsole(TranslationsTelemetry.onError, {
      errorMessage,
    });
  }

  /**
   * Records a telemetry event when a translation request is sent.
   *
   * @param {object} data
   * @param {boolean} data.autoTranslate
   * @param {string} [data.docLangTag]
   * @param {string} data.sourceLanguage
   * @param {string} data.targetLanguage
   * @param {string} data.requestTarget
   * @param {number} data.sourceTextCodeUnits
   * @param {number} [data.sourceTextWordCount]
   */
  static onTranslate(data) {
    const {
      autoTranslate,
      docLangTag,
      sourceLanguage,
      requestTarget,
      targetLanguage,
      sourceTextCodeUnits,
      sourceTextWordCount,
    } = data;
    Glean.translations.requestCount[requestTarget ?? "full_page"].add(1);
    Glean.translations.translationRequest.record({
      flow_id: TranslationsTelemetry.getOrCreateFlowContext().flowId,
      from_language: sourceLanguage,
      to_language: targetLanguage,
      auto_translate: autoTranslate,
      document_language: docLangTag,
      request_target: requestTarget ?? "full_page",
      source_text_code_units: sourceTextCodeUnits,
      source_text_word_count: sourceTextWordCount,
    });
    TranslationsTelemetry.logEventToConsole(
      TranslationsTelemetry.onTranslate,
      data
    );
  }

  static onRestorePage() {
    Glean.translations.restorePage.record({
      flow_id: TranslationsTelemetry.getOrCreateFlowContext().flowId,
    });
    TranslationsTelemetry.logEventToConsole(
      TranslationsTelemetry.onRestorePage
    );
  }

  /**
   * Records a telemetry event for measuring translation engine performance.
   *
   * @param {object} data
   * @param {string} data.sourceLanguage
   * @param {string} data.targetLanguage
   * @param {number} data.totalCompletedRequests
   * @param {number} data.totalInferenceSeconds
   * @param {number} data.totalTranslatedWords
   */
  static onReportEnginePerformance(data) {
    TranslationsTelemetry.withRateLimits(
      TranslationsTelemetry.#ENGINE_PERFORMANCE_RATE_LIMITS,
      () => {
        const {
          sourceLanguage,
          targetLanguage,
          totalCompletedRequests,
          totalInferenceSeconds,
          totalTranslatedWords,
        } = data;

        const averageWordsPerRequest =
          totalTranslatedWords / totalCompletedRequests;
        const averageWordsPerSecond =
          totalTranslatedWords / totalInferenceSeconds;

        Glean.translations.enginePerformance.record({
          from_language: sourceLanguage,
          to_language: targetLanguage,
          average_words_per_request: averageWordsPerRequest,
          average_words_per_second: averageWordsPerSecond,
          total_completed_requests: totalCompletedRequests,
          total_inference_seconds: totalInferenceSeconds,
          total_translated_words: totalTranslatedWords,
        });
        TranslationsTelemetry.logEventToConsole(
          TranslationsTelemetry.onReportEnginePerformance,
          {
            sourceLanguage,
            targetLanguage,
            averageWordsPerSecond,
            averageWordsPerRequest,
            totalCompletedRequests,
            totalInferenceSeconds,
            totalTranslatedWords,
          }
        );
      }
    );
  }

  static onFeatureEnable() {
    Glean.translationsFeature.enable.record();
    TranslationsTelemetry.logEventToConsole(
      TranslationsTelemetry.onFeatureEnable
    );
  }

  static onFeatureDisable() {
    Glean.translationsFeature.disable.record();
    TranslationsTelemetry.logEventToConsole(
      TranslationsTelemetry.onFeatureDisable
    );
  }

  static onFeatureReset() {
    Glean.translationsFeature.reset.record();
    TranslationsTelemetry.logEventToConsole(
      TranslationsTelemetry.onFeatureReset
    );
  }
}

/**
 * Telemetry functions for the FullPageTranslationsPanel UI
 */
class FullPageTranslationsPanelTelemetry {
  /**
   * The Full-Page panel events help us determine how users interact with the panel UI.
   * The panel can be opened quite frequently, and we don't need to collect data every
   * single time the panel opens in order to get a good idea of interaction behavior.
   *
   * @type {SampleRates}
   */
  // prettier-ignore
  static #FULL_PAGE_PANEL_RATE_LIMITS = {
    nightly: 1 /    10,
       beta: 1 /    50,
     aurora: 1 /    50,
        esr: 1 /   100,
    release: 1 / 1_000,
  };

  /**
   * Invokes a callback function with #FULL_PAGE_PANEL_RATE_LIMITS.
   *
   * @param {function(FlowContext): void} callback
   */
  static #withRateLimits(callback) {
    TranslationsTelemetry.withRateLimits(
      FullPageTranslationsPanelTelemetry.#FULL_PAGE_PANEL_RATE_LIMITS,
      callback
    );
  }

  /**
   * Records a telemetry event when the FullPageTranslationsPanel is opened.
   *
   * @param {object} data
   * @param {string} data.viewName
   * @param {string} data.docLangTag
   * @param {boolean} data.autoShow
   * @param {boolean} data.maintainFlow
   * @param {boolean} data.openedFromAppMenu
   */
  static onOpen(data) {
    if (!data.maintainFlow) {
      // Create a new flow context when the panel opens,
      // unless explicitly instructed to maintain the current flow.
      TranslationsTelemetry.createFlowContext();
    }

    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.open.record({
        flow_id: flowId,
        auto_show: data.autoShow,
        view_name: data.viewName,
        document_language: data.docLangTag,
        opened_from: data.openedFromAppMenu ? "appMenu" : "translationsButton",
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onOpen,
        data
      );
    });
  }

  static onClose() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.close.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onClose
      );
    });
  }

  static onOpenFromLanguageMenu() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.openFromLanguageMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onOpenFromLanguageMenu
      );
    });
  }

  static onChangeFromLanguage(langTag) {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.changeFromLanguage.record({
        flow_id: flowId,
        language: langTag,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onChangeFromLanguage,
        {
          langTag,
        }
      );
    });
  }

  static onCloseFromLanguageMenu() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.closeFromLanguageMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onCloseFromLanguageMenu
      );
    });
  }

  static onOpenToLanguageMenu() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.openToLanguageMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onOpenToLanguageMenu
      );
    });
  }

  static onChangeToLanguage(langTag) {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.changeToLanguage.record({
        flow_id: flowId,
        language: langTag,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onChangeToLanguage,
        {
          langTag,
        }
      );
    });
  }

  static onCloseToLanguageMenu() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.closeToLanguageMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onCloseToLanguageMenu
      );
    });
  }

  static onOpenSettingsMenu() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.openSettingsMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onOpenSettingsMenu
      );
    });
  }

  static onCloseSettingsMenu() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.closeSettingsMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onCloseSettingsMenu
      );
    });
  }

  static onCancelButton() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.cancelButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onCancelButton
      );
    });
  }

  static onChangeSourceLanguageButton() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.changeSourceLanguageButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onChangeSourceLanguageButton
      );
    });
  }

  static onDismissErrorButton() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.dismissErrorButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onDismissErrorButton
      );
    });
  }

  static onRestorePageButton() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.restorePageButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onRestorePageButton
      );
    });
  }

  static onTranslateButton() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.translateButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onTranslateButton
      );
    });
  }

  static onAlwaysOfferTranslations(toggledOn) {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.alwaysOfferTranslations.record({
        flow_id: flowId,
        toggled_on: toggledOn,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onAlwaysOfferTranslations,
        {
          toggledOn,
        }
      );
    });
  }

  static onAlwaysTranslateLanguage(langTag, toggledOn) {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.alwaysTranslateLanguage.record({
        flow_id: flowId,
        language: langTag,
        toggled_on: toggledOn,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onAlwaysTranslateLanguage,
        {
          langTag,
          toggledOn,
        }
      );
    });
  }

  static onNeverTranslateLanguage(langTag, toggledOn) {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.neverTranslateLanguage.record({
        flow_id: flowId,
        language: langTag,
        toggled_on: toggledOn,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onNeverTranslateLanguage,
        {
          langTag,
          toggledOn,
        }
      );
    });
  }

  static onNeverTranslateSite(toggledOn) {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.neverTranslateSite.record({
        flow_id: flowId,
        toggled_on: toggledOn,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onNeverTranslateSite,
        {
          toggledOn,
        }
      );
    });
  }

  static onManageLanguages() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.manageLanguages.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onManageLanguages
      );
    });
  }

  static onAboutTranslations() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.aboutTranslations.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onAboutTranslations
      );
    });
  }

  static onLearnMoreLink() {
    FullPageTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsPanel.learnMore.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        FullPageTranslationsPanelTelemetry.onLearnMoreLink
      );
    });
  }
}

/**
 * Telemetry functions for the SelectTranslationsPanel UI
 */
class SelectTranslationsPanelTelemetry {
  /**
   * These events do not yet occur frequently enough to benefit from rate limiting.
   *
   * @type {SampleRates}
   */
  static #SELECT_PANEL_RATE_LIMITS = {};

  /**
   * Invokes a callback function with #SELECT_PANEL_RATE_LIMITS.
   *
   * @param {function(FlowContext): void} callback
   */
  static #withRateLimits(callback) {
    TranslationsTelemetry.withRateLimits(
      SelectTranslationsPanelTelemetry.#SELECT_PANEL_RATE_LIMITS,
      callback
    );
  }

  /**
   * Records a telemetry event when the SelectTranslationsPanel is opened.
   *
   * @param {object} data
   * @param {string} data.docLangTag
   * @param {boolean} data.maintainFlow
   * @param {string} data.sourceLanguage
   * @param {string} data.targetLanguage
   * @param {string} data.textSource
   */
  static onOpen(data) {
    if (!data.maintainFlow) {
      // Create a new flow context when the panel opens,
      // unless explicitly instructed to maintain the current flow.
      TranslationsTelemetry.createFlowContext();
    }

    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.open.record({
        flow_id: flowId,
        document_language: data.docLangTag,
        from_language: data.sourceLanguage,
        to_language: data.targetLanguage,
        text_source: data.textSource,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onOpen,
        data
      );
    });
  }

  static onClose() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.close.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onClose
      );
    });
  }

  static onCancelButton() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.cancelButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onCancelButton
      );
    });
  }

  static onCopyButton() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.copyButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onCopyButton
      );
    });
  }

  static onDoneButton() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.doneButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onDoneButton
      );
    });
  }

  static onTranslateButton({
    detectedLanguage,
    sourceLanguage,
    targetLanguage,
  }) {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.translateButton.record({
        flow_id: flowId,
        detected_language: detectedLanguage,
        from_language: sourceLanguage,
        to_language: targetLanguage,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onTranslateButton,
        {
          detectedLanguage,
          sourceLanguage,
          targetLanguage,
        }
      );
    });
  }

  static onTranslateFullPageButton() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.translateFullPageButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onTranslateFullPageButton
      );
    });
  }

  static onTryAgainButton() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.tryAgainButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onTryAgainButton
      );
    });
  }

  static onChangeFromLanguage({ previousLangTag, currentLangTag, docLangTag }) {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.changeFromLanguage.record({
        flow_id: flowId,
        document_language: docLangTag,
        previous_language: previousLangTag,
        language: currentLangTag,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onChangeFromLanguage,
        { previousLangTag, currentLangTag, docLangTag }
      );
    });
  }

  static onChangeToLanguage(langTag) {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.changeToLanguage.record({
        flow_id: flowId,
        language: langTag,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onChangeToLanguage,
        { langTag }
      );
    });
  }

  static onOpenSettingsMenu() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.openSettingsMenu.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onOpenSettingsMenu
      );
    });
  }

  static onTranslationSettings() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.translationSettings.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onTranslationSettings
      );
    });
  }

  static onAboutTranslations() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.aboutTranslations.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onAboutTranslations
      );
    });
  }

  static onInitializationFailureMessage() {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.initializationFailureMessage.record(
        {
          flow_id: flowId,
        }
      );
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onInitializationFailureMessage
      );
    });
  }

  /**
   * Records a telemetry event when the translation-failure message is displayed.
   *
   * @param {object} data
   * @param {string} data.sourceLanguage
   * @param {string} data.targetLanguage
   */
  static onTranslationFailureMessage(data) {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.translationFailureMessage.record(
        {
          flow_id: flowId,
          from_language: data.sourceLanguage,
          to_language: data.targetLanguage,
        }
      );
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onTranslationFailureMessage,
        data
      );
    });
  }

  /**
   * Records a telemetry event when the unsupported-language message is displayed.
   *
   * @param {object} data
   * @param {string} data.docLangTag
   * @param {string} data.detectedLanguage
   */
  static onUnsupportedLanguageMessage(data) {
    SelectTranslationsPanelTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsSelectTranslationsPanel.unsupportedLanguageMessage.record(
        {
          flow_id: flowId,
          document_language: data.docLangTag,
          detected_language: data.detectedLanguage,
        }
      );
      TranslationsTelemetry.logEventToConsole(
        SelectTranslationsPanelTelemetry.onUnsupportedLanguageMessage,
        data
      );
    });
  }
}

/**
 * Telemetry functions for the AboutTranslations Page
 */
class AboutTranslationsPageTelemetry {
  /**
   * These events do not yet occur frequently enough to benefit from rate limiting.
   *
   * @type {SampleRates}
   */
  static #ABOUT_TRANSLATIONS_RATE_LIMITS = {};

  /**
   * Invokes a callback function with #ABOUT_TRANSLATIONS_RATE_LIMITS.
   *
   * @param {function(FlowContext): void} callback
   */
  static #withRateLimits(callback) {
    TranslationsTelemetry.withRateLimits(
      AboutTranslationsPageTelemetry.#ABOUT_TRANSLATIONS_RATE_LIMITS,
      callback
    );
  }

  /**
   * Records when a translation request is sent from about:translations.
   *
   * @param {object} data
   * @param {boolean} data.autoTranslate
   * @param {string} data.sourceLanguage
   * @param {string} data.targetLanguage
   * @param {number} data.sourceTextCodeUnits
   * @param {number} [data.sourceTextWordCount]
   */
  static onTranslate(data) {
    // Translation requests are explicitly excluded from rate limiting.
    TranslationsTelemetry.onTranslate({
      ...data,
      requestTarget: "about_translations",
    });
  }

  /**
   * Records when the about:translations page is opened.
   *
   * @param {object} data
   * @param {boolean} data.maintainFlow
   */
  static onOpen(data) {
    if (!data.maintainFlow) {
      // Create a new flow context when the panel opens, unless we
      // are explicitly instructed to maintain the current flow.
      TranslationsTelemetry.createFlowContext();
    }

    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.open.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onOpen,
        data
      );
    });
  }

  /**
   * Records when the try-again button is invoked in about:translations.
   */
  static onTryAgainButton() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.tryAgainButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onTryAgainButton
      );
    });
  }

  /**
   * Records when the copy button is invoked in about:translations.
   */
  static onCopyButton() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.copyButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onCopyButton
      );
    });
  }

  /**
   * Records when the clear source text button is invoked in about:translations.
   */
  static onClearSourceTextButton() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.clearSourceTextButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onClearSourceTextButton
      );
    });
  }

  /**
   * Records when the swap button is invoked in about:translations.
   */
  static onSwapButton() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.swapButton.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onSwapButton
      );
    });
  }

  /**
   * Records when the language-load-error message is shown in about:translations.
   */
  static onLanguageLoadErrorMessage() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.languageLoadErrorMessage.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onLanguageLoadErrorMessage
      );
    });
  }

  /**
   * Records when the unsupported-info message is shown in about:translations.
   */
  static onUnsupportedInfoMessage() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.unsupportedInfoMessage.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onUnsupportedInfoMessage
      );
    });
  }

  /**
   * Records when the policy-disabled-info message is shown in about:translations.
   */
  static onPolicyDisabledInfoMessage() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.policyDisabledInfoMessage.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onPolicyDisabledInfoMessage
      );
    });
  }

  /**
   * Records when the feature-blocked-info message is shown in about:translations.
   */
  static onFeatureBlockedInfoMessage() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.featureBlockedInfoMessage.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onFeatureBlockedInfoMessage
      );
    });
  }

  /**
   * Records when unblock-feature is invoked in about:translations.
   */
  static onUnblockFeature() {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.unblockFeature.record({
        flow_id: flowId,
      });
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onUnblockFeature
      );
    });
  }

  /**
   * Records when the unsupported-language message is shown in about:translations.
   *
   * @param {object} data
   * @param {string} data.detectedLanguage
   * @param {number} data.sourceTextCodeUnits
   * @param {number | null} [data.sourceTextWordCount]
   */
  static onUnsupportedLanguageMessage(data) {
    AboutTranslationsPageTelemetry.#withRateLimits(({ flowId }) => {
      Glean.translationsAboutTranslationsPage.unsupportedLanguageMessage.record(
        {
          flow_id: flowId,
          detected_language: data.detectedLanguage,
          source_text_code_units: data.sourceTextCodeUnits,
          source_text_word_count: data.sourceTextWordCount,
        }
      );
      TranslationsTelemetry.logEventToConsole(
        AboutTranslationsPageTelemetry.onUnsupportedLanguageMessage,
        data
      );
    });
  }
}
