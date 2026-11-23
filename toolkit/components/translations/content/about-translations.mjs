/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The following globals are injected via the AboutTranslationsChild actor.
// about-translations.mjs is running in an unprivileged context, and these injected functions
// allow for the page to get access to additional privileged features.

/* global AT_getAppLocaleAsBCP47, AT_getSupportedLanguages, AT_log, AT_getScriptDirection,
   AT_getDisplayName, AT_logError, AT_createTranslationsPort, AT_isHtmlTranslation,
   AT_isTranslationEngineSupported, AT_isInAutomation, AT_identifyLanguage, AT_clearSourceText,
   AT_telemetry, AT_isEnabledStateManagedByPolicy, AT_enableTranslationsFeature,
   AT_getIsTranslationsPageInSplitView, AT_closeTranslationsSplitViewTab */

import { Translator } from "chrome://global/content/translations/Translator.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-support-link.mjs";

/**
 * This is the delay for the set of throttled reactions to input in the source text area.
 * These actions will trigger at most once every 200 milliseconds while the source text area is receiving input.
 *
 * Applying this on the window allows tests to override the delay milliseconds for throttled input handling.
 */
window.THROTTLE_DELAY = 200;

/**
 * This is the delay for the set of debounced reactions to input in the source text area.
 * These actions will trigger 400 milliseconds after the final text-area input.
 * Further input to the text area within the 400ms window will reset this timer.
 *
 * Applying this on the window allows tests to override the delay milliseconds for debounced input handling.
 */
window.DEBOUNCE_DELAY = 400;

/**
 * This is the delay for throttling translation-request telemetry in about:translations.
 *
 * These events will trigger at most once per interval, unless the throttle
 * is manually reset by context changes (e.g. source clear or language change).
 *
 * Applying this on the window allows tests to override the delay milliseconds.
 */
window.TRANSLATION_REQUEST_TELEMETRY_THROTTLE_DELAY = 5_000;

/**
 * This is the default duration, in milliseconds, that the copy button remains
 * in the "copied" state before reverting back to its default state.
 *
 * Based on WCAG standards, a minimum of 5 seconds should be maintained to
 * avoid blinking, and to provide enough time for users to notice the change
 * of the copy button's state.
 */
const COPY_BUTTON_RESET_DELAY_DEFAULT = 5_000;

/**
 * This is the duration, in milliseconds, that the copy button remains in the
 * "copied" state before reverting back to its default state for users who
 * prefer reduced motion.
 *
 * https://www.w3.org/WAI/WCAG22/Understanding/timing-adjustable.html
 */
const COPY_BUTTON_RESET_DELAY_REDUCED_MOTION = 20_000;

window.COPY_BUTTON_RESET_DELAY = COPY_BUTTON_RESET_DELAY_DEFAULT;

/**
 * Limits how long the "text" parameter can be in the URL.
 */
const URL_MAX_TEXT_LENGTH = 5000;

/**
 * @typedef {import("../translations").LanguagePair} LanguagePair
 * @typedef {import("../translations").SupportedLanguages} SupportedLanguages
 */

/**
 * Dispatches a custom event used only by automated tests.
 *
 * @param {string} type
 * @param {object} [detail]
 */
function dispatchTestEvent(type, detail) {
  if (!AT_isInAutomation()) {
    return;
  }

  document.dispatchEvent(new CustomEvent(type, { detail }));
}

class AboutTranslations {
  /**
   * The set of languages that are supported by the Translations feature.
   *
   * @type {SupportedLanguages}
   */
  #supportedLanguages;

  /**
   * A monotonically increasing number that represents a unique ID
   * for each translation request.
   *
   * @type {number}
   */
  #translationId = 0;

  /**
   * Whether the Translations feature is enabled for this page.
   *
   * @type {boolean}
   */
  #isFeatureEnabled = true;

  /**
   * Whether the current system supports the translations engine.
   *
   * @type {boolean}
   */
  #isTranslationsEngineSupported;

  /**
   * The previous source text that was translated.
   *
   * This is used to calculate the difference of lengths in the texts,
   * which informs whether we should display a translating placeholder.
   *
   * @type {string}
   */
  #previousSourceText = "";

  /**
   * The BCP-47 language tag of the currently detected language.
   *
   * Defaults to an empty string if the detect-language option is not selected,
   * or if the detector did not determine a language tag.
   *
   * @type {string}
   */
  #detectedLanguage = "";

  /**
   * The display name of the currently detected language.
   *
   * @type {string}
   */
  #detectedLanguageDisplayName = "";

  /**
   * The translator for the current language pair.
   *
   * @type {null | Translator}
   */
  #translator = null;

  /**
   * The elements that comprise the about:translations UI.
   *
   * Use the {@link AboutTranslations#elements} getter to instantiate and retrieve them.
   *
   * @type {Record<string, Element>}
   */
  #lazyElements;

  /**
   * The localized placeholder text to display when translating.
   *
   * @type {string}
   */
  #translatingPlaceholderText;

  /**
   * A promise that resolves when the about:translations UI has successfully initialized,
   * or rejects if the UI failed to initialize.
   *
   * @type {PromiseWithResolvers}
   */
  #readyPromiseWithResolvers = Promise.withResolvers();

  /**
   * A timeout id for resetting the copy button's "copied" state.
   *
   * @type {number | null}
   */
  #copyButtonResetTimeoutId = null;

  /**
   * A timeout id for throttling translation-request telemetry.
   *
   * @type {number | null}
   */
  #translationRequestTelemetryThrottleTimeoutId = null;

  /**
   * The word-count segmenter for the current source language.
   *
   * @type {Intl.Segmenter | null}
   */
  #wordCountSegmenter = null;

  /**
   * The language tag of the current word-count segmenter.
   *
   * @type {string}
   */
  #wordCountSegmenterLanguageTag = "";

  /**
   * An optional delay override for resetting the copy button.
   *
   * This is intended for automated tests that need deterministic timing.
   *
   * @type {number | null}
   */
  #copyButtonResetDelayOverride = null;

  /**
   * Whether manual copy button resets are enabled for automated tests.
   *
   * @type {boolean}
   */
  #isManualCopyButtonResetEnabled = false;

  /**
   * The orientation of the page's content.
   *
   * When the page orientation is horizontal the source and target sections
   * are displayed side by side with the source section at the inline start
   * and the target section at the inline end.
   *
   * When the page orientation is vertical the source section is displayed
   * above the target section, independent of locale bidirectionality.
   *
   * When the page orientation is vertical, each section spans the full width
   * of the content, and resizing the window width or changing the zoom level
   * must trigger section resizing, whereas those updates are not necessary
   * when the page orientation is horizontal.
   *
   * @type {("vertical"|"horizontal")}
   */
  #pageOrientation = "horizontal";

  /**
   * A timeout id that gets set when a pending callback is scheduled
   * to update the section heights.
   *
   * This helps ensure that we do not make repeated calls to this function
   * that would cause unnecessary and excessive reflow.
   *
   * @type {number | null}
   */
  #updateSectionHeightsTimeoutId = null;

  /**
   * This set contains hash values that to be assigned to the URL from user
   * interaction with the UI. When this occurs, we want to ignore the "hashchange"
   * event, since the URL did not change externally.
   *
   * When "hashchange" fires and the active URL hash is not a member of this set,
   * then it means the user may have modified the URL outside of the page UI, and
   * we will need to update the UI from the URL.
   *
   * @type {Set<string>}
   */
  #urlHashesFromUIState = new Set();

  /**
   * Returns the maximum of the given numbers, rounded up.
   *
   * @param  {...number} numbers
   *
   * @returns {number}
   */
  static #maxInteger(...numbers) {
    return Math.ceil(Math.max(...numbers));
  }

  /**
   * Constructs a new {@link AboutTranslations} instance.
   *
   * @param {boolean} isTranslationsEngineSupported
   *        Whether the translations engine is supported by the current system.
   */
  constructor(isTranslationsEngineSupported) {
    this.#isTranslationsEngineSupported = isTranslationsEngineSupported;

    AT_telemetry("onOpen", {
      maintainFlow: false,
    });

    this.elements.splitViewCloseButton.addEventListener(
      "click",
      this.#onSplitViewCloseButton
    );

    if (!isTranslationsEngineSupported) {
      void this.#updateSplitViewCloseButtonVisibility();
      this.#readyPromiseWithResolvers.reject();
      this.#showUnsupportedInfoMessage();
      return;
    }

    this.#setup()
      .then(() => {
        this.#readyPromiseWithResolvers.resolve();
      })
      .catch(error => {
        AT_logError(error);
        this.#readyPromiseWithResolvers.reject(error);
        this.#showLanguageLoadErrorMessage();
      });
  }

  /**
   * Resolves when the about:translations page has successfully initialized.
   *
   * @returns {Promise<void>}
   */
  async ready() {
    await this.#readyPromiseWithResolvers.promise;
  }

  /**
   * Enables the feature and updates the UI to match the enabled state.
   *
   * @returns {Promise<void>}
   */
  async onFeatureEnabled() {
    this.#isFeatureEnabled = true;

    if (!this.#isTranslationsEngineSupported) {
      this.#showUnsupportedInfoMessage();
      document.body.style.visibility = "visible";
      return;
    }

    if (!this.#supportedLanguages) {
      this.#showLanguageLoadErrorMessage();
      document.body.style.visibility = "visible";
      return;
    }

    this.#showMainUserInterface();
    this.#setMainUserInterfaceEnabled(true);
    this.#updateSourceScriptDirection();
    this.#updateTargetScriptDirection();
    this.#updateSourceSectionClearButtonVisibility();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
    this.#updateURLFromUI();
    this.#maybeRequestTranslation();

    document.body.style.visibility = "visible";
  }

  /**
   * Disables the feature and clears any active translations.
   *
   * @returns {Promise<void>}
   */
  async onFeatureDisabled() {
    // Ensure any active translation request becomes stale.
    this.#translationId += 1;
    this.#isFeatureEnabled = false;
    this.#destroyTranslator();

    const isManagedByPolicy = await AT_isEnabledStateManagedByPolicy();
    if (isManagedByPolicy) {
      this.#showPolicyDisabledInfoMessage();
      return;
    }

    this.#showFeatureBlockedInfoMessage();
  }

  /**
   * Instantiates and returns the elements that comprise the UI.
   *
   * @returns {{
   *   copyButton: HTMLElement,
   *   detectLanguageOption: HTMLElement,
   *   detectedLanguageUnsupportedHeading: HTMLElement,
   *   detectedLanguageUnsupportedLearnMoreLink: HTMLAnchorElement,
   *   detectedLanguageUnsupportedMessage: HTMLElement,
   *   featureBlockedInfoMessage: HTMLElement,
   *   unblockFeatureButton: HTMLElement,
   *   languageLoadErrorButton: HTMLElement,
   *   languageLoadErrorMessage: HTMLElement,
   *   learnMoreLink: HTMLAnchorElement,
   *   mainUserInterface: HTMLElement,
   *   policyDisabledInfoMessage: HTMLElement,
   *   sourceLanguageSelector: HTMLElement,
   *   sourceSection: HTMLElement,
   *   sourceSectionClearButton: HTMLElement,
   *   sourceSectionTextArea: HTMLTextAreaElement,
   *   splitViewCloseButton: HTMLElement,
   *   swapLanguagesButton: HTMLElement,
   *   targetLanguageSelector: HTMLElement,
   *   targetSection: HTMLElement,
   *   targetSectionTextArea: HTMLTextAreaElement,
   *   translationErrorButton: HTMLElement,
   *   translationErrorMessage: HTMLElement,
   *   unsupportedInfoMessage: HTMLElement,
   * }}
   */
  get elements() {
    if (this.#lazyElements) {
      return this.#lazyElements;
    }

    this.#lazyElements = {
      copyButton: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-copy-button")
      ),
      detectLanguageOption: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-detect-language-label-option"
        )
      ),
      detectedLanguageUnsupportedHeading: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-detected-language-unsupported-heading"
        )
      ),
      detectedLanguageUnsupportedLearnMoreLink:
        /** @type {HTMLAnchorElement} */ (
          document.getElementById(
            "about-translations-detected-language-unsupported-learn-more-link"
          )
        ),
      detectedLanguageUnsupportedMessage: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-detected-language-unsupported-message"
        )
      ),
      featureBlockedInfoMessage: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-feature-blocked-info-message"
        )
      ),
      unblockFeatureButton: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-feature-blocked-unblock-button"
        )
      ),
      languageLoadErrorButton: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-language-load-error-button")
      ),
      languageLoadErrorMessage: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-language-load-error-message"
        )
      ),
      learnMoreLink: /** @type {HTMLAnchorElement} */ (
        document.getElementById("about-translations-learn-more-link")
      ),
      mainUserInterface: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-main-user-interface")
      ),
      policyDisabledInfoMessage: /** @type {HTMLElement} */ (
        document.getElementById(
          "about-translations-policy-disabled-info-message"
        )
      ),
      sourceLanguageSelector: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-source-select")
      ),
      sourceSection: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-source-section")
      ),
      sourceSectionClearButton: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-clear-button")
      ),
      sourceSectionTextArea: /** @type {HTMLTextAreaElement} */ (
        document.getElementById("about-translations-source-textarea")
      ),
      splitViewCloseButton: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-split-view-close-button")
      ),
      swapLanguagesButton: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-swap-languages-button")
      ),
      targetLanguageSelector: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-target-select")
      ),
      targetSection: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-target-section")
      ),
      targetSectionTextArea: /** @type {HTMLTextAreaElement} */ (
        document.getElementById("about-translations-target-textarea")
      ),
      translationErrorButton: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-translation-error-button")
      ),
      translationErrorMessage: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-translation-error-message")
      ),
      unsupportedInfoMessage: /** @type {HTMLElement} */ (
        document.getElementById("about-translations-unsupported-info-message")
      ),
    };

    return this.#lazyElements;
  }

  /**
   * Sets the internal data member for which orientation the page is in.
   *
   * This information is important for performance regarding in which situations
   * we need to dynamically resize the <textarea> elements on the page.
   *
   * @returns {boolean} True if the page orientation changed, otherwise false.
   */
  #updatePageOrientation() {
    const orientationAtStart = this.#pageOrientation;

    // Keep this value up to date with the media query rule in about-translations.css
    this.#pageOrientation =
      window.innerWidth > 1200 ? "horizontal" : "vertical";

    const orientationChanged = orientationAtStart !== this.#pageOrientation;

    if (orientationChanged) {
      dispatchTestEvent("AboutTranslationsTest:PageOrientationChanged", {
        orientation: this.#pageOrientation,
      });
    }

    return orientationChanged;
  }

  /**
   * An async setup function to initialize the about:translations page.
   *
   * Performs all initialization steps and finally reveals the main UI.
   */
  async #setup() {
    this.#initializeEventListeners();
    this.#updatePageOrientation();
    await this.#updateSplitViewCloseButtonVisibility();

    this.#translatingPlaceholderText = await document.l10n.formatValue(
      "about-translations-translating-message"
    );

    await this.#setupLanguageSelectors();
    this.#updateUIFromURL();

    this.#updateSourceScriptDirection();
    this.#updateTargetScriptDirection();

    this.#showMainUserInterface();
    this.#updateSourceSectionClearButtonVisibility();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
  }

  /**
   * Populates the language selector elements with supported languages.
   *
   * This is a one-time operation performed during initialization.
   */
  async #populateLanguageSelectors() {
    this.#supportedLanguages = await AT_getSupportedLanguages();

    const { sourceLanguageSelector, targetLanguageSelector } = this.elements;

    for (const { langTagKey, displayName } of this.#supportedLanguages
      .sourceLanguages) {
      const option = document.createElement("moz-option");
      option.value = langTagKey;
      option.setAttribute("label", displayName);
      sourceLanguageSelector.append(option);
    }

    for (const { langTagKey, displayName } of this.#supportedLanguages
      .targetLanguages) {
      const option = document.createElement("moz-option");
      option.value = langTagKey;
      option.setAttribute("label", displayName);
      targetLanguageSelector.append(option);
    }
  }

  /**
   * Sets up the language selectors during initialization.
   */
  async #setupLanguageSelectors() {
    await this.#populateLanguageSelectors();

    const { sourceLanguageSelector, targetLanguageSelector } = this.elements;
    this.#resetDetectLanguageOptionText();
    sourceLanguageSelector.value = "detect";
    targetLanguageSelector.value = "";
    this.#setElementEnabled(sourceLanguageSelector, true);
    this.#setElementEnabled(targetLanguageSelector, true);
  }

  /**
   * Updates the close button to reflect this page's current Split View state.
   *
   * @returns {Promise<void>}
   */
  async #updateSplitViewCloseButtonVisibility() {
    try {
      this.applySplitViewState({
        isInSplitView: await AT_getIsTranslationsPageInSplitView(),
      });
    } catch (error) {
      AT_logError(error);
      this.applySplitViewState({ isInSplitView: false });
    }
  }

  /**
   * Updates UI that is only available when the page is in Split View.
   *
   * @param {object} splitViewState
   * @param {boolean} splitViewState.isInSplitView
   */
  applySplitViewState({ isInSplitView }) {
    this.elements.splitViewCloseButton.hidden = !isInSplitView;
  }

  /**
   * Initializes all relevant event listeners for the UI elements.
   */
  #initializeEventListeners() {
    const {
      copyButton,
      detectedLanguageUnsupportedMessage,
      unblockFeatureButton,
      languageLoadErrorButton,
      sourceLanguageSelector,
      sourceSectionClearButton,
      sourceSectionTextArea,
      swapLanguagesButton,
      targetLanguageSelector,
      translationErrorButton,
      translationErrorMessage,
    } = this.elements;

    copyButton.addEventListener("click", this.#onCopyButton);
    unblockFeatureButton.addEventListener(
      "click",
      this.#onUnblockFeatureButton
    );
    languageLoadErrorButton.addEventListener(
      "click",
      this.#onLanguageLoadErrorRetry
    );
    sourceLanguageSelector.addEventListener(
      "change",
      this.#onSourceLanguageChange
    );
    detectedLanguageUnsupportedMessage.addEventListener(
      "pointerdown",
      this.#onSourceMessageBarPointerDown
    );
    sourceSectionClearButton.addEventListener(
      "click",
      this.#onSourceSectionClearButton
    );
    sourceSectionClearButton.addEventListener(
      "mousedown",
      this.#onSourceSectionClearButtonMouseDown
    );
    sourceSectionTextArea.addEventListener("input", this.#onSourceTextInput);
    swapLanguagesButton.addEventListener("click", this.#onSwapLanguagesButton);
    targetLanguageSelector.addEventListener(
      "change",
      this.#onTargetLanguageChange
    );
    translationErrorButton.addEventListener(
      "click",
      this.#onTranslationErrorRetry
    );
    translationErrorMessage.addEventListener(
      "pointerdown",
      this.#onTargetMessageBarPointerDown
    );
    window.addEventListener("hashchange", this.#onHashChange);
    window.addEventListener("resize", this.#onResize);
    window.visualViewport.addEventListener("resize", this.#onResize);
  }

  /**
   * Handles retry clicks from the language-load error message.
   */
  #onLanguageLoadErrorRetry = event => {
    event.preventDefault();
    void this.#retryLanguageLoad();
  };

  /**
   * Handles clicks on the unblock button in the feature-blocked message.
   */
  #onUnblockFeatureButton = async () => {
    const { unblockFeatureButton } = this.elements;
    unblockFeatureButton.setAttribute("disabled", "true");
    AT_telemetry("onUnblockFeature");

    try {
      await AT_enableTranslationsFeature();
    } catch (error) {
      AT_logError(error);
    } finally {
      unblockFeatureButton.removeAttribute("disabled");
    }
  };

  /**
   * Handles mousedown on the source section clear button.
   *
   * Prevents the button from taking focus when clicked.
   */
  #onSourceSectionClearButtonMouseDown = event => {
    event.preventDefault();
  };

  /**
   * Handles clicks on the source section clear button.
   */
  #onSourceSectionClearButton = event => {
    if (!this.#sourceTextAreaHasValue()) {
      return;
    }

    event.preventDefault();
    this.#clearSourceText();

    AT_telemetry("onClearSourceTextButton");
  };

  /**
   * Records activation and requests that this page's Split View tab close.
   */
  #onSplitViewCloseButton = () => {
    AT_telemetry("onCloseCompanionButton");
    AT_closeTranslationsSplitViewTab();
  };

  /**
   * Handles clicks on the swap-languages button.
   */
  #onSwapLanguagesButton = () => {
    AT_telemetry("onSwapButton");
    this.#clearTranslationRequestTelemetryThrottle();
    this.#disableSwapLanguagesButton();
    this.#maybeSwapLanguages();
    this.#updateURLFromUI();
    this.#maybeRequestTranslation({ allowFromErrorState: true });
  };

  /**
   * Handles change events on the source-language selector.
   */
  #onSourceLanguageChange = () => {
    const { sourceLanguageSelector } = this.elements;
    const previouslyDetectedLanguageWasExplicitlySelected =
      sourceLanguageSelector.value === this.#detectedLanguage;

    this.#updateURLFromUI();
    this.#resetDetectLanguageOptionText();
    this.#clearTranslationRequestTelemetryThrottle();

    if (previouslyDetectedLanguageWasExplicitlySelected) {
      // This represents the case where we were previously on the "Detect language"
      // dropdown menu item and, for example, Spanish was detected, but the user has
      // now updated the source-language select to the explicit "Spanish" menu item.
      // In this case, the effectively selected language tag remains the same.
      return;
    }

    this.#disableSwapLanguagesButton();
    this.#updateSourceScriptDirection();
    this.#maybeRequestTranslation({ allowFromErrorState: true });
    this.#updateDetectedLanguageUnsupportedMessage();
  };

  /**
   * Handles change events on the target-language selector.
   */
  #onTargetLanguageChange = () => {
    this.#clearTranslationRequestTelemetryThrottle();
    this.#disableSwapLanguagesButton();
    this.#updateURLFromUI();
    this.#maybeRequestTranslation({ allowFromErrorState: true });
  };

  /**
   * Handles input events on the source textarea.
   */
  #onSourceTextInput = () => {
    this.#updateSourceSectionClearButtonVisibility();
    this.#handleSourceTextInput();
  };

  /**
   * Handles pointerdown events within the source message bar.
   *
   * Focuses the source section when the event occurs on non-interactive content.
   */
  #onSourceMessageBarPointerDown = event => {
    if (event.target?.closest?.("a")) {
      return;
    }

    this.elements.sourceSection.focus();
  };

  /**
   * Handles pointerdown events within the target message bar.
   *
   * Focuses the target section when the event occurs on non-interactive content.
   */
  #onTargetMessageBarPointerDown = event => {
    if (event.target?.closest?.("moz-button")) {
      return;
    }

    this.elements.targetSection.focus();
  };

  /**
   * Handles retry clicks from the translation error message.
   */
  #onTranslationErrorRetry = () => {
    AT_telemetry("onTryAgainButton");
    this.#maybeRequestTranslation({ allowFromErrorState: true });
  };

  /**
   * Handles copying the translated text to the clipboard when the copy button is invoked.
   */
  #onCopyButton = async () => {
    const { copyButton, targetSectionTextArea } = this.elements;
    if (copyButton.disabled) {
      return;
    }

    const targetText = targetSectionTextArea.value;
    if (!targetText) {
      return;
    }

    try {
      if (!navigator.clipboard?.writeText) {
        throw new Error("Clipboard API is unavailable.");
      }
      await navigator.clipboard.writeText(targetText);
    } catch (error) {
      AT_logError(error);
      return;
    }

    AT_telemetry("onCopyButton");
    this.#showCopyButtonCopiedState();
  };

  /**
   * Handles resize events, including resizing the window and zooming.
   */
  #onResize = () => {
    const orientationChanged = this.#updatePageOrientation();

    if (orientationChanged) {
      this.#requestSectionHeightsUpdate({ scheduleCallback: false });
    } else if (this.#pageOrientation === "vertical") {
      this.#requestSectionHeightsUpdate({ scheduleCallback: true });
    }
  };

  /**
   * Handles hash changes that come from URL bar navigation.
   */
  #onHashChange = event => {
    const urlHash = new URL(event.newURL).hash.slice(1);

    if (this.#urlHashesFromUIState.has(urlHash)) {
      // This hash change came from the UI updating the URL.
      // The UI doesn't need to react to this hash change.
      this.#urlHashesFromUIState.delete(urlHash);
      return;
    }

    this.#updateUIFromURL();
  };

  /**
   * Shows the main UI and hides any stand-alone message bars.
   */
  #showMainUserInterface() {
    const { mainUserInterface } = this.elements;

    for (const messageBar of this.#getTopLevelMessageBars()) {
      this.#setTopLevelMessageBarVisible(messageBar, false);
    }

    mainUserInterface.hidden = false;
  }

  /**
   * Enables or disables the controls in the main UI.
   *
   * @param {boolean} enabled
   */
  #setMainUserInterfaceEnabled(enabled) {
    const {
      copyButton,
      sourceLanguageSelector,
      sourceSectionClearButton,
      sourceSectionTextArea,
      swapLanguagesButton,
      targetLanguageSelector,
      targetSectionTextArea,
      translationErrorButton,
    } = this.elements;

    for (const element of [
      sourceLanguageSelector,
      targetLanguageSelector,
      sourceSectionTextArea,
      targetSectionTextArea,
      sourceSectionClearButton,
      translationErrorButton,
    ]) {
      this.#setElementEnabled(element, enabled);
    }

    // These buttons depend on contextual UI state, so they are handled separately.
    // They will always be disabled when we are disabling the UI, but they are only
    // conditionally re-enabled based on the context within the UI itself.
    if (!enabled) {
      swapLanguagesButton.disabled = true;
      copyButton.disabled = true;
    } else {
      this.#updateSwapLanguagesButtonEnabledState();
    }
  }

  /**
   * Sets an element's enabled state.
   *
   * We need to do it this way until the moz-select web component properly
   * reflects the disabled attribute on the top-level custom element.
   *
   * @param {HTMLElement} element
   * @param {boolean} enabled
   */
  #setElementEnabled(element, enabled) {
    if ("disabled" in element) {
      element.disabled = !enabled;
    }

    if (enabled) {
      element.removeAttribute("disabled");
    } else {
      element.setAttribute("disabled", "true");
    }
  }

  /**
   * Shows the message that translations are not supported in the current environment.
   */
  #showUnsupportedInfoMessage() {
    const { unsupportedInfoMessage } = this.elements;
    this.#showTopLevelMessageBar(unsupportedInfoMessage);
  }

  /**
   * Shows the message that translations are unavailable due to enterprise policy.
   */
  #showPolicyDisabledInfoMessage() {
    const { policyDisabledInfoMessage } = this.elements;
    this.#showTopLevelMessageBar(policyDisabledInfoMessage);
  }

  /**
   * Returns all top-level message bars.
   *
   * @returns {HTMLElement[]}
   */
  #getTopLevelMessageBars() {
    const {
      featureBlockedInfoMessage,
      unsupportedInfoMessage,
      policyDisabledInfoMessage,
      languageLoadErrorMessage,
    } = this.elements;

    return [
      unsupportedInfoMessage,
      policyDisabledInfoMessage,
      languageLoadErrorMessage,
      featureBlockedInfoMessage,
    ];
  }

  /**
   * Shows a top-level message bar and updates the main UI visibility.
   *
   * @param {HTMLElement} messageBar
   */
  #showTopLevelMessageBar(messageBar) {
    const { featureBlockedInfoMessage, mainUserInterface } = this.elements;

    for (const topLevelMessageBar of this.#getTopLevelMessageBars()) {
      this.#setTopLevelMessageBarVisible(
        topLevelMessageBar,
        topLevelMessageBar === messageBar
      );
    }

    if (messageBar === featureBlockedInfoMessage) {
      mainUserInterface.hidden = false;
      this.#setMainUserInterfaceEnabled(false);
    } else {
      mainUserInterface.hidden = true;
    }

    document.body.style.visibility = "visible";
  }

  /**
   * Shows the message that the list of languages could not be loaded.
   */
  #showLanguageLoadErrorMessage() {
    const { languageLoadErrorMessage } = this.elements;
    this.#showTopLevelMessageBar(languageLoadErrorMessage);
  }

  /**
   * Shows the message that the feature is blocked but can be unblocked.
   */
  #showFeatureBlockedInfoMessage() {
    const { featureBlockedInfoMessage } = this.elements;
    this.#showTopLevelMessageBar(featureBlockedInfoMessage);
  }

  /**
   * Shows or hides a top-level message bar and notifies tests when the
   * visibility changes.
   *
   * @param {HTMLElement} messageBar
   * @param {boolean} visible
   */
  #setTopLevelMessageBarVisible(messageBar, visible) {
    const isVisible = !messageBar.hidden;

    if (isVisible === visible) {
      return;
    }

    messageBar.hidden = !visible;
    if (visible) {
      this.#recordTopLevelMessageBarTelemetry(messageBar);
    }

    const eventNames =
      this.#getTopLevelMessageBarVisibilityEventNames(messageBar);
    if (!eventNames) {
      return;
    }

    dispatchTestEvent(visible ? eventNames.shown : eventNames.hidden);
  }

  /**
   * Records telemetry when a top-level message bar is shown.
   *
   * @param {HTMLElement} messageBar
   */
  #recordTopLevelMessageBarTelemetry(messageBar) {
    const {
      unsupportedInfoMessage,
      policyDisabledInfoMessage,
      languageLoadErrorMessage,
      featureBlockedInfoMessage,
    } = this.elements;

    switch (messageBar) {
      case unsupportedInfoMessage: {
        AT_telemetry("onUnsupportedInfoMessage");
        return;
      }
      case policyDisabledInfoMessage: {
        AT_telemetry("onPolicyDisabledInfoMessage");
        return;
      }
      case languageLoadErrorMessage: {
        AT_telemetry("onLanguageLoadErrorMessage");
        return;
      }
      case featureBlockedInfoMessage: {
        AT_telemetry("onFeatureBlockedInfoMessage");
        return;
      }
      default: {
        AT_logError(
          `Unexpected top-level message bar for telemetry: ${messageBar.id || "<no-id>"}`
        );
      }
    }
  }

  /**
   * Returns test events for top-level message bar visibility changes.
   *
   * @param {HTMLElement} messageBar
   * @returns {{ shown: string, hidden: string } | null}
   */
  #getTopLevelMessageBarVisibilityEventNames(messageBar) {
    const {
      unsupportedInfoMessage,
      policyDisabledInfoMessage,
      languageLoadErrorMessage,
    } = this.elements;

    if (messageBar === unsupportedInfoMessage) {
      return {
        shown: "AboutTranslationsTest:UnsupportedInfoMessageShown",
        hidden: "AboutTranslationsTest:UnsupportedInfoMessageHidden",
      };
    }

    if (messageBar === policyDisabledInfoMessage) {
      return {
        shown: "AboutTranslationsTest:PolicyDisabledInfoMessageShown",
        hidden: "AboutTranslationsTest:PolicyDisabledInfoMessageHidden",
      };
    }

    if (messageBar === languageLoadErrorMessage) {
      return {
        shown: "AboutTranslationsTest:LanguageLoadErrorMessageShown",
        hidden: "AboutTranslationsTest:LanguageLoadErrorMessageHidden",
      };
    }

    return null;
  }

  /**
   * Resets the source and target language selectors before reloading languages.
   */
  #resetLanguageSelectorsForReload() {
    const {
      detectLanguageOption,
      sourceLanguageSelector,
      targetLanguageSelector,
    } = this.elements;

    for (const option of sourceLanguageSelector.querySelectorAll(
      "moz-option"
    )) {
      if (option !== detectLanguageOption) {
        option.remove();
      }
    }

    for (const option of targetLanguageSelector.querySelectorAll(
      "moz-option"
    )) {
      if (option.getAttribute("value") !== "") {
        option.remove();
      }
    }
  }

  /**
   * Retries loading languages and restores the UI when the data is available.
   */
  async #retryLanguageLoad() {
    const { languageLoadErrorButton } = this.elements;
    languageLoadErrorButton.setAttribute("disabled", "true");
    dispatchTestEvent("AboutTranslationsTest:LanguageLoadRetryStarted");

    try {
      this.#resetLanguageSelectorsForReload();
      await this.#setupLanguageSelectors();
      this.#updateUIFromURL();
      this.#updateSourceScriptDirection();
      this.#updateTargetScriptDirection();
      this.#showMainUserInterface();
      this.#updateSourceSectionClearButtonVisibility();
      this.#requestSectionHeightsUpdate({ scheduleCallback: false });
      dispatchTestEvent("AboutTranslationsTest:LanguageLoadRetrySucceeded");
    } catch (error) {
      AT_logError(error);
      this.#showLanguageLoadErrorMessage();
      dispatchTestEvent("AboutTranslationsTest:LanguageLoadRetryFailed");
    } finally {
      languageLoadErrorButton.removeAttribute("disabled");
    }
  }

  /**
   * Shows the message that the detected language is not yet supported.
   */
  #showDetectedLanguageUnsupportedMessage() {
    const {
      detectedLanguageUnsupportedHeading,
      detectedLanguageUnsupportedMessage,
    } = this.elements;
    const wasHidden = detectedLanguageUnsupportedMessage.hidden;

    const languageLabel =
      this.#detectedLanguageDisplayName || this.#detectedLanguage;
    if (languageLabel) {
      document.l10n.setAttributes(
        detectedLanguageUnsupportedHeading,
        "about-translations-detected-language-unsupported-heading-2",
        { language: languageLabel }
      );
    } else {
      document.l10n.setAttributes(
        detectedLanguageUnsupportedHeading,
        "about-translations-detected-language-unsupported-heading-unknown-2"
      );
    }

    detectedLanguageUnsupportedMessage.hidden = false;
    if (wasHidden) {
      this.#recordUnsupportedLanguageMessageTelemetry();
    }
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
    document.l10n
      .translateFragment(detectedLanguageUnsupportedMessage)
      .then(() => {
        this.#requestSectionHeightsUpdate({ scheduleCallback: false });
      });
  }

  /**
   * Hides the message that the detected language is not yet supported.
   */
  #hideDetectedLanguageUnsupportedMessage() {
    const { detectedLanguageUnsupportedMessage } = this.elements;
    if (detectedLanguageUnsupportedMessage.hidden) {
      return;
    }

    detectedLanguageUnsupportedMessage.hidden = true;
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
  }

  /**
   * Shows the translation error message in the target section.
   */
  #showTranslationErrorMessage() {
    const { targetSection, translationErrorMessage } = this.elements;
    if (!translationErrorMessage.hidden) {
      return;
    }

    this.#clearTranslationRequestTelemetryThrottle();
    targetSection.classList.add("has-translation-error");
    translationErrorMessage.hidden = false;
    this.#disableSwapLanguagesButton();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
  }

  /**
   * Hides the translation error message in the target section.
   */
  #hideTranslationErrorMessage() {
    const { targetSection, translationErrorMessage } = this.elements;
    if (translationErrorMessage.hidden) {
      return;
    }

    targetSection.classList.remove("has-translation-error");
    translationErrorMessage.hidden = true;
    this.#updateSwapLanguagesButtonEnabledState();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
  }

  /**
   * Updates the detected-language unsupported message based on the current state.
   */
  #updateDetectedLanguageUnsupportedMessage() {
    const sourceText = this.#getSourceText();
    if (!sourceText || !this.#isDetectLanguageSelected()) {
      this.#hideDetectedLanguageUnsupportedMessage();
      return;
    }

    if (!this.#detectedLanguage || this.#isDetectedLanguageUnsupported()) {
      this.#showDetectedLanguageUnsupportedMessage();
      return;
    }

    this.#hideDetectedLanguageUnsupportedMessage();
  }

  /**
   * Splits a language-tag key into its language tag and optional variant.
   *
   * @param {string} languageTagKey
   * @returns {{ languageTag: string, variant: string | undefined }}
   */
  #splitLanguageTagKey(languageTagKey) {
    const [languageTag = "", variant] = languageTagKey.split(",");
    return { languageTag, variant };
  }

  /**
   * Returns the currently selected or detected source language tag.
   * Returns an empty string if no language is selected or detected.
   *
   * @returns {string}
   */
  #getSelectedSourceLanguageTag() {
    const selectedSourceLanguage = this.#isDetectLanguageSelected()
      ? this.#detectedLanguage
      : this.elements.sourceLanguageSelector.value;
    return this.#splitLanguageTagKey(selectedSourceLanguage).languageTag;
  }

  /**
   * Returns the currently selected target language tag.
   * Returns an empty string if no language is selected.
   *
   * @returns {string}
   */
  #getSelectedTargetLanguageTag() {
    return this.#splitLanguageTagKey(this.elements.targetLanguageSelector.value)
      .languageTag;
  }

  /**
   * Returns the language pair formed by the values of the language selectors.
   * Returns null if one or more of the selectors has no selected value.
   *
   * @returns {null | LanguagePair}
   */
  #getSelectedLanguagePair() {
    const { sourceLanguageSelector, targetLanguageSelector } = this.elements;

    let selectedSourceLanguage;
    if (this.#isDetectLanguageSelected()) {
      selectedSourceLanguage = this.#detectedLanguage;
    } else {
      selectedSourceLanguage = sourceLanguageSelector.value;
    }

    const { languageTag: sourceLanguage, variant: sourceVariant } =
      this.#splitLanguageTagKey(selectedSourceLanguage);
    const { languageTag: targetLanguage, variant: targetVariant } =
      this.#splitLanguageTagKey(targetLanguageSelector.value);

    if (sourceLanguage === "" || targetLanguage === "") {
      // At least one selector is empty, so we cannot create a language pair.
      return null;
    }

    return {
      sourceLanguage,
      targetLanguage,
      sourceVariant,
      targetVariant,
    };
  }

  /**
   * Returns true if we do not yet support the detected language for Translations,
   * otherwise false.
   *
   * @returns {boolean}
   */
  #isDetectedLanguageUnsupported() {
    if (!this.#isDetectLanguageSelected()) {
      // The detect-language option is not selected so this is not relevant.
      return false;
    }

    return !this.elements.sourceLanguageSelector.querySelector(
      `[value^=${this.#detectedLanguage}]`
    );
  }

  /**
   * Returns true if the source-language selector is set to the detect-language
   * option, otherwise false.
   *
   * @returns {boolean}
   */
  #isDetectLanguageSelected() {
    return this.elements.sourceLanguageSelector.value === "detect";
  }

  /**
   * Updates the display value of the source-language selector with the identified
   * language of the source text, only if the source-language selector is set to
   * the detect-language option.
   */
  async #maybeUpdateDetectedSourceLanguage() {
    if (!this.#isDetectLanguageSelected()) {
      this.#resetDetectLanguageOptionText();
      return;
    }

    const detectedLanguage = await this.#identifySourceLanguage();
    if (!this.#isDetectLanguageSelected()) {
      this.#resetDetectLanguageOptionText();
      return;
    }

    const previousDetectedLanguage = this.#detectedLanguage;
    this.#detectedLanguage = detectedLanguage;

    if (detectedLanguage) {
      const displayName = await AT_getDisplayName(detectedLanguage);
      this.#detectedLanguageDisplayName = displayName;
      this.#populateDetectLanguageOption({ detectedLanguage, displayName });
    } else {
      this.#resetDetectLanguageOptionText();
    }

    this.#updateSourceScriptDirection();

    if (previousDetectedLanguage !== detectedLanguage) {
      this.#clearTranslationRequestTelemetryThrottle();
      dispatchTestEvent("AboutTranslationsTest:DetectedLanguageUpdated", {
        language: detectedLanguage,
      });
    }
  }

  /**
   * Sets the label of the about-translations-detect option in the
   * about-translations-source-select dropdown.
   *
   * Passing an empty display name will reset to the default string.
   * Passing a display name will set the text to the given language.
   *
   * @param {object} params
   * @param {string} params.detectedLanguage - The BCP-47 language tag of the detected language.
   * @param {string} params.displayName - The display name of the detected language.
   */
  #populateDetectLanguageOption({ detectedLanguage, displayName }) {
    const { detectLanguageOption } = this.elements;
    detectLanguageOption.setAttribute("language", detectedLanguage);
    document.l10n.setAttributes(
      detectLanguageOption,
      "about-translations-detect-language-label",
      { language: displayName }
    );
  }

  /**
   * Restores the detect-language option to its default value.
   */
  #resetDetectLanguageOptionText() {
    const { detectLanguageOption } = this.elements;
    this.#detectedLanguage = "";
    this.#detectedLanguageDisplayName = "";
    detectLanguageOption.removeAttribute("language");
    document.l10n.setAttributes(
      detectLanguageOption,
      "about-translations-detect-default-label"
    );
  }

  /**
   * Returns true if the source-language selector contains the given language,
   * otherwise false.
   *
   * @param {string} language - A BCP-47 language tag or prefix.
   * @returns {boolean}
   */
  #sourceSelectorHasLanguage(language) {
    return this.elements.sourceLanguageSelector.querySelector(
      `[value^=${language}]`
    );
  }

  /**
   * Returns true if the target-language selector contains the given language,
   * otherwise false.
   *
   * @param {string} language - A BCP-47 language tag or prefix.
   * @returns {boolean}
   */
  #targetSelectorHasLanguage(language) {
    return this.elements.targetLanguageSelector.querySelector(
      `[value^=${language}]`
    );
  }

  /**
   * Determines whether the currently selected source/target languages can be swapped.
   *
   * @returns {boolean}
   */
  #selectedLanguagePairIsSwappable() {
    const selectedLanguagePair = this.#getSelectedLanguagePair();
    return (
      // There is an actively selected language pair.
      selectedLanguagePair &&
      // The source and target languages are different languages.
      selectedLanguagePair.sourceLanguage !==
        selectedLanguagePair.targetLanguage &&
      // The source language list contains the currently selected target language.
      this.#sourceSelectorHasLanguage(selectedLanguagePair.targetLanguage) &&
      // The target language list contains the currently selected source language.
      this.#targetSelectorHasLanguage(selectedLanguagePair.sourceLanguage)
    );
  }

  /**
   * Disables the swap-languages button.
   */
  #disableSwapLanguagesButton() {
    const { swapLanguagesButton } = this.elements;
    if (swapLanguagesButton.disabled === true) {
      return;
    }

    swapLanguagesButton.disabled = true;
    dispatchTestEvent("AboutTranslationsTest:SwapLanguagesButtonDisabled");
  }

  /**
   * Enables the swap-languages button.
   */
  #enableSwapLanguagesButton() {
    const { swapLanguagesButton } = this.elements;
    if (swapLanguagesButton.disabled === false) {
      return;
    }

    swapLanguagesButton.disabled = false;
    dispatchTestEvent("AboutTranslationsTest:SwapLanguagesButtonEnabled");
  }

  /**
   * Updates the enabled state of the swap-languages button based on the current
   * values of the language selectors.
   */
  #updateSwapLanguagesButtonEnabledState() {
    if (!this.elements.translationErrorMessage.hidden) {
      this.#disableSwapLanguagesButton();
      return;
    }

    if (this.#selectedLanguagePairIsSwappable()) {
      this.#enableSwapLanguagesButton();
    } else {
      this.#disableSwapLanguagesButton();
    }
  }

  /**
   * Updates the enabled state of the copy button.
   *
   * @param {boolean} shouldEnable
   */
  #setCopyButtonEnabled(shouldEnable) {
    const { copyButton } = this.elements;

    if (copyButton.disabled !== shouldEnable) {
      // The state isn't going to change: nothing to do.
      return;
    }

    if (
      this.#copyButtonResetTimeoutId !== null ||
      copyButton.classList.contains("copied")
    ) {
      // When the copy button's enabled state changes while it is in the "copied" state,
      // then we want to reset it immediately, instead of waiting for the timeout.
      this.#resetCopyButton();
    }

    copyButton.disabled = !shouldEnable;

    const eventName = shouldEnable
      ? "AboutTranslationsTest:CopyButtonEnabled"
      : "AboutTranslationsTest:CopyButtonDisabled";
    dispatchTestEvent(eventName);
  }

  /**
   * Applies the "copied" state visuals to the copy button.
   */
  #showCopyButtonCopiedState() {
    const { copyButton } = this.elements;

    copyButton.classList.add("copied");
    copyButton.iconSrc = "chrome://global/skin/icons/check.svg";

    document.l10n.setAttributes(
      copyButton,
      "about-translations-copy-button-copied"
    );
    dispatchTestEvent("AboutTranslationsTest:CopyButtonShowCopied");

    this.#maybeScheduleCopyButtonReset();
  }

  /**
   * Restores the copy button to its default visual state.
   */
  #resetCopyButton() {
    if (this.#copyButtonResetTimeoutId !== null) {
      window.clearTimeout(this.#copyButtonResetTimeoutId);
      this.#copyButtonResetTimeoutId = null;
    }

    const { copyButton } = this.elements;
    if (!copyButton.classList.contains("copied")) {
      return;
    }

    copyButton.classList.remove("copied");
    copyButton.iconSrc = "chrome://global/skin/icons/edit-copy.svg";

    document.l10n.setAttributes(
      copyButton,
      "about-translations-copy-button-default"
    );
    dispatchTestEvent("AboutTranslationsTest:CopyButtonReset");
  }

  /**
   * Returns the copy button reset delay based on user preferences and test overrides.
   *
   * @returns {number}
   */
  #getCopyButtonResetDelay() {
    if (this.#copyButtonResetDelayOverride !== null) {
      window.COPY_BUTTON_RESET_DELAY = this.#copyButtonResetDelayOverride;
      return this.#copyButtonResetDelayOverride;
    }

    const defaultCopyButtonResetDelay = window.matchMedia(
      "(prefers-reduced-motion: reduce)"
    ).matches
      ? COPY_BUTTON_RESET_DELAY_REDUCED_MOTION
      : COPY_BUTTON_RESET_DELAY_DEFAULT;

    window.COPY_BUTTON_RESET_DELAY = defaultCopyButtonResetDelay;
    return defaultCopyButtonResetDelay;
  }

  /**
   * Schedules resetting the copy button back to its default state.
   */
  #maybeScheduleCopyButtonReset() {
    if (this.#isManualCopyButtonResetEnabled) {
      return;
    }

    if (this.#copyButtonResetTimeoutId !== null) {
      window.clearTimeout(this.#copyButtonResetTimeoutId);
      this.#copyButtonResetTimeoutId = null;
    }

    this.#copyButtonResetTimeoutId = window.setTimeout(() => {
      this.#resetCopyButton();
    }, this.#getCopyButtonResetDelay());
  }

  /**
   * Manually resets the state of the copy button.
   * This function is only expected to be called by automated tests.
   */
  testResetCopyButton() {
    if (!AT_isInAutomation()) {
      throw new Error("Test-only function called outside of automation.");
    }

    if (!this.#isManualCopyButtonResetEnabled) {
      throw new Error("Unexpected call to testResetCopyButton.");
    }

    this.#resetCopyButton();
  }

  /**
   * Enables or disables manual copy button resets for automated tests.
   *
   * @param {boolean} enabled
   */
  testSetManualCopyButtonResetEnabled(enabled) {
    if (!AT_isInAutomation()) {
      throw new Error("Test-only function called outside of automation.");
    }

    this.#isManualCopyButtonResetEnabled = enabled;
  }

  /**
   * Sets or clears the copy button reset delay override for automated tests.
   *
   * @param {number | null} delayMs
   */
  testSetCopyButtonResetDelayOverride(delayMs) {
    if (!AT_isInAutomation()) {
      throw new Error("Test-only function called outside of automation.");
    }

    this.#copyButtonResetDelayOverride = delayMs;
    this.#getCopyButtonResetDelay();
  }

  /**
   * Clears translation-request telemetry throttling for automated tests.
   */
  testClearTranslationRequestTelemetryThrottle() {
    if (!AT_isInAutomation()) {
      throw new Error("Test-only function called outside of automation.");
    }

    this.#clearTranslationRequestTelemetryThrottle();
  }

  /**
   * If the currently selected language pair is determined to be swappable,
   * swaps the active source language with the active target language,
   * and moves the translated output to be the new source text.
   */
  #maybeSwapLanguages() {
    if (!this.#selectedLanguagePairIsSwappable()) {
      return;
    }

    const {
      sourceLanguageSelector,
      targetLanguageSelector,
      sourceSectionTextArea,
      targetSectionTextArea,
    } = this.elements;

    const selectedLanguagePair = this.#getSelectedLanguagePair();

    sourceLanguageSelector.value = selectedLanguagePair.targetLanguage;
    targetLanguageSelector.value = selectedLanguagePair.sourceLanguage;
    sourceSectionTextArea.value = targetSectionTextArea.value;

    this.#updateSourceScriptDirection();
    this.#updateTargetScriptDirection();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
    this.#updateSourceSectionClearButtonVisibility();

    if (sourceSectionTextArea.value) {
      this.#displayTranslatingPlaceholder();
    }

    // When swapping languages, the target language will become the source language,
    // therefore the "detect" option is guaranteed to not be selected, and we need
    // to ensure that we reset the option text back to the default in case the user
    // opens the source-text dropdown menu.
    this.#resetDetectLanguageOptionText();
  }

  /**
   * Attempts to identify the BCP-47 language tag of the source text.
   * Returns an empty string if no language could be identified.
   *
   * @returns {string} The BCP-47 language tag of the identified language.
   */
  async #identifySourceLanguage() {
    const sourceText = this.#getSourceText();
    if (!sourceText) {
      return "";
    }

    const { language } = await AT_identifyLanguage(sourceText);
    if (!language) {
      return "";
    }

    if (language === "zh") {
      // CLD2 detects simplified Chinese as "zh" and traditional Chinese as "zh-Hant".
      // But we need to be explicit about the script tag in both cases.
      return "zh-Hans";
    }

    return language;
  }

  /**
   * Returns the trimmed value of the source <textarea>.
   *
   * @returns {string}
   */
  #getSourceText() {
    return this.elements.sourceSectionTextArea.value.trim();
  }

  /**
   * Returns true if the source textarea contains any text, otherwise false.
   *
   * @returns {boolean}
   */
  #sourceTextAreaHasValue() {
    return Boolean(this.elements.sourceSectionTextArea.value);
  }

  /**
   * Returns the configured throttle delay for translation-request telemetry.
   *
   * @returns {number}
   */
  #getTranslationRequestTelemetryThrottleDelay() {
    const delay = Number(window.TRANSLATION_REQUEST_TELEMETRY_THROTTLE_DELAY);
    return Number.isFinite(delay) && delay >= 0 ? delay : 5_000;
  }

  /**
   * Clears the active translation-request telemetry throttle timer.
   */
  #clearTranslationRequestTelemetryThrottle() {
    if (this.#translationRequestTelemetryThrottleTimeoutId !== null) {
      clearTimeout(this.#translationRequestTelemetryThrottleTimeoutId);
      this.#translationRequestTelemetryThrottleTimeoutId = null;
    }
  }

  /**
   * Determines whether translation-request telemetry should be recorded now.
   *
   * @returns {boolean}
   */
  #shouldRecordTranslationRequestTelemetry() {
    if (this.#translationRequestTelemetryThrottleTimeoutId !== null) {
      return false;
    }

    this.#translationRequestTelemetryThrottleTimeoutId = setTimeout(() => {
      this.#translationRequestTelemetryThrottleTimeoutId = null;
    }, this.#getTranslationRequestTelemetryThrottleDelay());

    return true;
  }

  /**
   * Counts the words in the text for the given language tag.
   *
   * @param {string} text - The text for which to count words.
   * @param {string | null} [languageTag=null] - An optional BCP-47 language tag.
   *
   * @returns {number | null} The count of words in the text, or null when no language tag is available.
   */
  #countWords(text, languageTag = null) {
    if (!languageTag) {
      return null;
    }

    let segmenter = this.#wordCountSegmenter;

    if (
      // We have not yet cached a word segmenter.
      !segmenter ||
      // Our cached segmenter no longer matches the source language.
      this.#wordCountSegmenterLanguageTag !== languageTag
    ) {
      segmenter = new Intl.Segmenter(languageTag, { granularity: "word" });
      this.#wordCountSegmenter = segmenter;
      this.#wordCountSegmenterLanguageTag = languageTag;
    }

    const segments = Array.from(segmenter.segment(text));
    return segments.filter(segment => segment.isWordLike).length;
  }

  /**
   * Records a translation-request telemetry event when the throttle allows it.
   *
   * @param {object} data
   * @param {string} data.sourceLanguage
   * @param {string} data.targetLanguage
   * @param {string} data.sourceText
   */
  async #maybeRecordTranslationRequestTelemetry({
    sourceLanguage,
    targetLanguage,
    sourceText,
  }) {
    if (!this.#shouldRecordTranslationRequestTelemetry()) {
      return;
    }

    let sourceTextWordCount;
    try {
      sourceTextWordCount = this.#countWords(sourceText, sourceLanguage);
    } catch (error) {
      AT_logError(error);
    }

    try {
      AT_telemetry("onTranslate", {
        autoTranslate: false,
        sourceLanguage,
        targetLanguage,
        sourceTextCodeUnits: sourceText.length,
        sourceTextWordCount,
      });
    } catch (error) {
      AT_logError(error);
    }
  }

  /**
   * Records an unsupported-language-message telemetry event.
   */
  #recordUnsupportedLanguageMessageTelemetry() {
    const sourceText = this.#getSourceText();
    const detectedLanguage = this.#detectedLanguage;

    let sourceTextWordCount;
    try {
      sourceTextWordCount = this.#countWords(sourceText, detectedLanguage);
    } catch (error) {
      AT_logError(error);
    }

    try {
      AT_telemetry("onUnsupportedLanguageMessage", {
        detectedLanguage,
        sourceTextCodeUnits: sourceText.length,
        sourceTextWordCount,
      });
    } catch (error) {
      AT_logError(error);
    }
  }

  /**
   * Shows or hides the source clear button based on whether the textarea has text.
   */
  #updateSourceSectionClearButtonVisibility() {
    const { sourceSectionClearButton } = this.elements;

    const shouldShow = this.#sourceTextAreaHasValue();
    const isHidden = sourceSectionClearButton.hidden;

    const shouldHide = !shouldShow;
    const isShown = !isHidden;

    if (shouldShow && isHidden) {
      sourceSectionClearButton.hidden = false;
      dispatchTestEvent("AboutTranslationsTest:SourceTextClearButtonShown");
    } else if (shouldHide && isShown) {
      sourceSectionClearButton.hidden = true;
      dispatchTestEvent("AboutTranslationsTest:SourceTextClearButtonHidden");
    }
  }

  /**
   * Clears the content in the source-section text area.
   */
  #clearSourceText() {
    AT_clearSourceText();
    this.elements.sourceSectionTextArea.focus();
    dispatchTestEvent("AboutTranslationsTest:ClearSourceText");
  }

  /**
   * Sets the value of the source <textarea> and dispatches an input event.
   *
   * @param {string} value
   */
  #setSourceText(value) {
    const { sourceSectionTextArea } = this.elements;
    const hadValueBefore = Boolean(sourceSectionTextArea.value);

    sourceSectionTextArea.value = value;

    this.#updateSourceScriptDirection();
    this.#maybeUpdateDetectedSourceLanguage();
    this.#updateSourceSectionClearButtonVisibility();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });

    if (!value && hadValueBefore) {
      this.#clearTranslationRequestTelemetryThrottle();
      dispatchTestEvent("AboutTranslationsTest:ClearSourceText");
    }

    sourceSectionTextArea.dispatchEvent(new Event("input"));
  }

  /**
   * Sets the value of the target <textarea>.
   *
   * @param {string} value
   * @param {object} [options]
   * @param {boolean} [options.isTranslationResult=false]
   * True if the value is the result of a translation request, otherwise false.
   */
  #setTargetText(value, { isTranslationResult = false } = {}) {
    this.elements.targetSectionTextArea.value = value;

    if (!value) {
      dispatchTestEvent("AboutTranslationsTest:ClearTargetText");
    }

    this.#updateTargetScriptDirection();
    this.#requestSectionHeightsUpdate({ scheduleCallback: false });
    this.#setCopyButtonEnabled(Boolean(value) && isTranslationResult);
  }

  /**
   * Displays the translating placeholder text if the conditions are correct to do so.
   * We do this when switching to a new language pair, when rebuilding the translator,
   * or if the text is long enough that translating may incur a significant wait time.
   */
  #maybeDisplayTranslatingPlaceholder() {
    const sourceText = this.#getSourceText();

    if (
      // We will need to request a new translator, which will take time.
      !this.#translatorMatchesSelectedLanguagePair() ||
      // The current translator matches, but we need to request a new port, which takes time.
      this.#translator?.portClosed ||
      // The source text is long enough to take noticeable time to translate,
      // and has changed enough from the previous source text to warrant a placeholder.
      (sourceText.length > 5000 &&
        Math.abs(sourceText.length - this.#previousSourceText.length) > 500)
    ) {
      this.#displayTranslatingPlaceholder();
    }

    this.#previousSourceText = sourceText;
  }

  /**
   * Shows the translating placeholder in the target textarea and disables
   * the swap-languages button while translation is in progress.
   */
  #displayTranslatingPlaceholder() {
    const { targetSectionTextArea } = this.elements;

    this.#setTargetText(this.#translatingPlaceholderText);
    this.#disableSwapLanguagesButton();
    this.#updateTextAreaAttributes(targetSectionTextArea, {
      languageTag: AT_getAppLocaleAsBCP47(),
    });

    dispatchTestEvent("AboutTranslationsTest:ShowTranslatingPlaceholder");
  }

  /**
   * Returns true if the current {@link Translator} instance is sufficient
   * to translate the actively selected language pair, otherwise false.
   *
   * @returns {boolean}
   */
  #translatorMatchesSelectedLanguagePair() {
    if (!this.#translator) {
      // There is no active translator, therefore the language pair cannot match it.
      return false;
    }

    const selectedLanguagePair = this.#getSelectedLanguagePair();
    if (!selectedLanguagePair) {
      // No valid language pair is selected, therefore it cannot match the translator.
      return false;
    }

    return this.#translator.matchesLanguagePair(selectedLanguagePair);
  }

  /**
   * Reads the current URL hash and updates the UI to reflect the parameters.
   * Invalid parameters are ignored.
   */
  #updateUIFromURL() {
    const supportedLanguages = this.#supportedLanguages;
    const urlParams = new URLSearchParams(window.location.hash.slice(1));
    const sourceLanguage = urlParams.get("src");
    const targetLanguage = urlParams.get("trg");
    const sourceText = urlParams.get("text") ?? "";
    const { sourceLanguageSelector, targetLanguageSelector } = this.elements;
    const previousSourceLanguage = sourceLanguageSelector.value;
    const previousTargetLanguage = targetLanguageSelector.value;

    sourceLanguageSelector.value = "detect";
    targetLanguageSelector.value = "";

    if (
      sourceLanguage !== "detect" &&
      supportedLanguages.sourceLanguages.some(
        ({ langTagKey }) => langTagKey === sourceLanguage
      )
    ) {
      sourceLanguageSelector.value = sourceLanguage;
    }

    if (
      supportedLanguages.targetLanguages.some(
        ({ langTagKey }) => langTagKey === targetLanguage
      )
    ) {
      targetLanguageSelector.value = targetLanguage;
    }

    if (
      sourceLanguageSelector.value !== previousSourceLanguage ||
      targetLanguageSelector.value !== previousTargetLanguage
    ) {
      this.#clearTranslationRequestTelemetryThrottle();
    }

    this.#setSourceText(sourceText);
  }

  /**
   * Updates the URL hash to reflect the current state of the UI.
   */
  #updateURLFromUI() {
    const { sourceLanguageSelector, targetLanguageSelector } = this.elements;

    const params = new URLSearchParams();

    const sourceLanguage = sourceLanguageSelector.value;
    if (sourceLanguage) {
      params.append("src", sourceLanguage);
    }

    const targetLanguage = targetLanguageSelector.value;
    if (targetLanguage) {
      params.append("trg", targetLanguage);
    }

    const sourceText = this.#getSourceText().substring(0, URL_MAX_TEXT_LENGTH);
    if (sourceText) {
      params.append("text", sourceText);
    }

    const hashFromUIState = params.toString();
    const currentHash = window.location.hash.slice(1);
    if (hashFromUIState !== currentHash) {
      const url = new URL(window.location.href);
      url.hash = hashFromUIState;
      this.#urlHashesFromUIState.add(hashFromUIState);
      try {
        window.location.replace(url.href);
      } catch (error) {
        this.#urlHashesFromUIState.delete(hashFromUIState);
        throw error;
      }
    }

    dispatchTestEvent("AboutTranslationsTest:URLUpdatedFromUI", {
      sourceLanguage,
      targetLanguage,
      sourceText,
    });
  }

  /**
   * Updates a textarea's `dir` and `lang` attributes according to the provided language tag.
   *
   * The `dir` attribute is always derived and set from the provided language tag,
   * however there are cases in which we may want to remove the `lang` attribute
   * from the textarea, rather than populate it with the tag's value.
   *
   * @param {HTMLTextAreaElement} textArea
   * @param {object} config
   * @param {string} config.languageTag
   * @param {boolean} [config.removeLangAttribute=false]
   */
  #updateTextAreaAttributes(
    textArea,
    { languageTag, removeLangAttribute = false }
  ) {
    textArea.setAttribute("dir", AT_getScriptDirection(languageTag));

    if (removeLangAttribute) {
      textArea.removeAttribute("lang");
    } else {
      textArea.setAttribute("lang", languageTag);
    }
  }

  /**
   * Sets the source text area's `dir` and `lang` attributes based on its
   * content and the currently selected source language.
   */
  #updateSourceScriptDirection() {
    const selectedSourceLanguage = this.#getSelectedSourceLanguageTag();
    const { sourceSectionTextArea } = this.elements;

    if (selectedSourceLanguage && sourceSectionTextArea.value) {
      this.#updateTextAreaAttributes(sourceSectionTextArea, {
        languageTag: selectedSourceLanguage,
      });
    } else {
      this.#updateTextAreaAttributes(sourceSectionTextArea, {
        languageTag: AT_getAppLocaleAsBCP47(),
        removeLangAttribute: true,
      });
    }
  }

  /**
   * Sets the target text area's `dir` and `lang` attributes based on its
   * content and the currently selected target language.
   */
  #updateTargetScriptDirection() {
    const selectedTargetLanguage = this.#getSelectedTargetLanguageTag();
    const { targetSectionTextArea } = this.elements;

    if (selectedTargetLanguage && targetSectionTextArea.value) {
      this.#updateTextAreaAttributes(targetSectionTextArea, {
        languageTag: selectedTargetLanguage,
      });
    } else {
      this.#updateTextAreaAttributes(targetSectionTextArea, {
        languageTag: AT_getAppLocaleAsBCP47(),
        removeLangAttribute: true,
      });
    }
  }

  /**
   * Ensures that the translator matches the selected language pair,
   * destroying the previous non-matching translator if needed, and
   * creating a new translator.
   *
   * @returns {Promise<boolean>} true if the translator was reassigned, otherwise false.
   */
  async #ensureTranslatorMatchesSelectedLanguagePair() {
    if (this.#translatorMatchesSelectedLanguagePair()) {
      return false;
    }

    this.#translator?.destroy();

    const selectedLanguagePair = this.#getSelectedLanguagePair();

    this.#translator = await Translator.create({
      languagePair: selectedLanguagePair,
      requestTranslationsPort,
      allowSameLanguage: true,
      activeRequestCapacity: 1,
    });

    return true;
  }

  /**
   * Destroys the active {@link Translator} instance, if any.
   */
  #destroyTranslator() {
    if (this.#translator) {
      this.#translator.destroy();
      this.#translator = null;
    }
  }

  /**
   * Rebuilds the translator unconditionally and immediately re-evaluates whether
   * a translation request should be made.
   */
  rebuildTranslator() {
    this.#destroyTranslator();
    this.#maybeRequestTranslation();
  }

  /**
   * Schedules reactions to input in the source text area in three categories
   * with different considerations for the timing in which the actions are triggered.
   */
  #handleSourceTextInput = scheduleInputHandling({
    /**
     * These actions happen every time input is received to the source text area.
     *
     * These actions should be cheap to call, and of high importance: things that we
     * likely want to react to immediately, such as clearing the entirety of the text.
     */
    doEveryTime: () => {
      if (!this.#isFeatureEnabled) {
        return;
      }

      const sourceText = this.#getSourceText();
      performance.mark(
        `Translations: input changed to ${sourceText.length} code units.`
      );

      if (!sourceText) {
        this.#clearTranslationRequestTelemetryThrottle();
        this.#setTargetText("");

        if (this.#isDetectLanguageSelected()) {
          this.#resetDetectLanguageOptionText();
        }

        this.#updateURLFromUI();
        this.#updateSwapLanguagesButtonEnabledState();
        this.#hideDetectedLanguageUnsupportedMessage();
      }

      this.#updateSourceScriptDirection();
    },

    /**
     * These actions are throttled, such that they occur frequently while the text area is receiving input,
     * but they will not occur every time the text area receives input.
     *
     * These actions may be more expensive to call than do-every-time actions, but are still necessary for
     * making the UI appear fluid and reactive to input, such as updating the height of the sections, or
     * updating the detected language of the text.
     */
    onThrottle: async () => {
      if (!this.#isFeatureEnabled) {
        return;
      }

      try {
        this.#requestSectionHeightsUpdate({ scheduleCallback: false });
        await this.#maybeUpdateDetectedSourceLanguage();
        this.#updateDetectedLanguageUnsupportedMessage();
      } catch (error) {
        AT_logError(error);
      }
    },

    /**
     * These actions are debounced, such that they occur only when active input to the text area has stopped
     * for a determined amount of time.
     *
     * These are the most expensive actions to call, such as requesting translations, which we should only invoke
     * when we're reasonably certain that the user has completed their source-text input for the moment.
     */
    onDebounce: async () => {
      const sourceText = this.#getSourceText();
      dispatchTestEvent("AboutTranslationsTest:SourceTextInputDebounced", {
        sourceText,
      });
      if (sourceText) {
        this.#updateURLFromUI();
      }
      await this.#maybeRequestTranslation();
    },
  });

  /**
   * Requests translation when the UI conditions are met.
   *
   * @param {object} [options]
   * @param {boolean} [options.allowFromErrorState=false]
   *   Allow a translation request when a translation error message is visible, such as retrying
   *   after a failure or switching languages while an error is showing.
   * @returns {Promise<void>}
   */
  async #maybeRequestTranslation({ allowFromErrorState = false } = {}) {
    if (!this.#isFeatureEnabled) {
      return;
    }

    let translationId = null;

    try {
      await this.#maybeUpdateDetectedSourceLanguage();
      this.#updateDetectedLanguageUnsupportedMessage();

      const sourceText = this.#getSourceText();
      const selectedLanguagePair = this.#getSelectedLanguagePair();

      if (!sourceText || !selectedLanguagePair) {
        // The conditions for translation are not met.
        this.#setTargetText("");
        this.#destroyTranslator();
        this.#updateSwapLanguagesButtonEnabledState();
        this.#hideTranslationErrorMessage();
        return;
      }

      if (this.#isDetectedLanguageUnsupported()) {
        this.#updateSwapLanguagesButtonEnabledState();
        this.#setTargetText("");
        this.#hideTranslationErrorMessage();
        return;
      }

      if (
        !this.elements.translationErrorMessage.hidden &&
        !allowFromErrorState
      ) {
        return;
      }

      translationId = ++this.#translationId;
      this.#hideTranslationErrorMessage();
      this.#maybeDisplayTranslatingPlaceholder();

      await this.#ensureTranslatorMatchesSelectedLanguagePair();
      if (translationId !== this.#translationId) {
        // This translation request is no longer relevant.
        return;
      }

      dispatchTestEvent("AboutTranslationsTest:TranslationRequested", {
        translationId,
      });

      void this.#maybeRecordTranslationRequestTelemetry({
        sourceLanguage: selectedLanguagePair.sourceLanguage,
        targetLanguage: selectedLanguagePair.targetLanguage,
        sourceText,
      });

      const startTime = performance.now();
      const translationRequest = this.#translator.translate(
        sourceText,
        AT_isHtmlTranslation()
      );

      const translatedText = await translationRequest;
      if (translationId !== this.#translationId) {
        // This translation request is no longer relevant.
        return;
      }

      performance.measure(
        `AboutTranslations: Translate ${selectedLanguagePair.sourceLanguage} → ${selectedLanguagePair.targetLanguage} with ${sourceText.length} characters.`,
        {
          start: startTime,
          end: performance.now(),
        }
      );

      this.#setTargetText(translatedText, { isTranslationResult: true });
      this.#hideTranslationErrorMessage();
      this.#updateSwapLanguagesButtonEnabledState();
      dispatchTestEvent("AboutTranslationsTest:TranslationComplete", {
        translationId,
      });

      const duration = performance.now() - startTime;
      AT_log(`Translation done in ${duration / 1000} seconds`);
    } catch (error) {
      AT_logError(error);
      if (translationId === null || translationId !== this.#translationId) {
        return;
      }
      this.#setTargetText("");
      this.#showTranslationErrorMessage();
      this.#updateSwapLanguagesButtonEnabledState();
    }
  }

  /**
   * Requests that the heights of each section is updated to at least match its content.
   *
   * In horizontal orientation the source and target sections are synchronized to
   * the maximum content height between the two. In vertical orientation, each section
   * is sized to its own content height.
   *
   * There are many situations in which this function needs to be called:
   *   - Every time the source text is updated
   *   - Every time a translation occurs
   *   - Every time the window is resized
   *   - Every time the zoom level is changed
   *   - Etc.
   *
   * Some of these events happen infrequently, or are already debounced, such as
   * each time a translation occurs. In these situations it is okay to update
   * the section heights immediately.
   *
   * Some of these events can trigger quite rapidly, such as resizing the window
   * via click-and-drag semantics. In this case, a single callback should be scheduled
   * to update the section heights to prevent unnecessary and excessive reflow.
   *
   * @param {object} params
   * @param {boolean} params.scheduleCallback
   */
  #requestSectionHeightsUpdate({ scheduleCallback }) {
    if (scheduleCallback) {
      if (this.#updateSectionHeightsTimeoutId) {
        // There is already a pending callback: no need to schedule another.
        return;
      }

      this.#updateSectionHeightsTimeoutId = setTimeout(
        this.#updateSectionHeights,
        100
      );

      return;
    }

    this.#updateSectionHeights();
  }

  /**
   * Updates the section heights based on the current page orientation.
   *
   * This function is intentionally written as a lambda so that it can be passed
   * as a callback without the need to explicitly bind `this` to the function object.
   */
  #updateSectionHeights = () => {
    if (this.#updateSectionHeightsTimeoutId) {
      clearTimeout(this.#updateSectionHeightsTimeoutId);
      this.#updateSectionHeightsTimeoutId = null;
    }

    if (this.#pageOrientation === "horizontal") {
      this.#synchronizeSectionsToMaxContentHeight();
    } else {
      this.#resizeSectionsToIndividualContentHeights();
    }
  };

  /**
   * Retrieves the combined border and padding height of an element.
   *
   * Returns 0 when the element is missing or does not use border-box sizing.
   *
   * @param {HTMLElement | undefined} element
   *
   * @returns {number}
   */
  #getBorderAndPaddingHeight(element) {
    if (!element) {
      return 0;
    }

    const style = window.getComputedStyle(element);
    if (style.boxSizing !== "border-box") {
      return 0;
    }

    const borderTop = Number.parseFloat(style.borderTopWidth) || 0;
    const borderBottom = Number.parseFloat(style.borderBottomWidth) || 0;
    const paddingTop = Number.parseFloat(style.paddingTop) || 0;
    const paddingBottom = Number.parseFloat(style.paddingBottom) || 0;

    return Math.ceil(borderTop + borderBottom + paddingTop + paddingBottom);
  }

  /**
   * Retrieves the minimum height of an element.
   *
   * @param {HTMLElement | undefined} element
   *
   * @returns {number}
   */
  #getMinHeight(element) {
    if (!element) {
      return 0;
    }

    const style = window.getComputedStyle(element);
    const minHeight = Number.parseFloat(style.minHeight);

    if (Number.isNaN(minHeight)) {
      return 0;
    }

    return Math.max(minHeight - this.#getBorderAndPaddingHeight(element), 0);
  }

  /**
   * Returns whether a section height increased or decreased.
   *
   * @param {number} beforeHeight
   * @param {number} afterHeight
   * @returns {null | "decreased" | "increased"}
   */
  #getSectionHeightChange(beforeHeight, afterHeight) {
    const changeThreshold = 1;
    if (!Number.isFinite(beforeHeight) || beforeHeight <= changeThreshold) {
      return null;
    }

    const delta = afterHeight - beforeHeight;
    if (Math.abs(delta) <= changeThreshold) {
      return null;
    }

    return delta < 0 ? "decreased" : "increased";
  }

  /**
   * Dispatches a test event when section height changes are detected.
   *
   * @param {object} params
   * @param {null | "decreased" | "increased"} params.sourceSectionHeightChange
   * @param {null | "decreased" | "increased"} params.targetSectionHeightChange
   */
  #dispatchSectionHeightsChangedEvent({
    sourceSectionHeightChange,
    targetSectionHeightChange,
  }) {
    if (!sourceSectionHeightChange && !targetSectionHeightChange) {
      return;
    }

    dispatchTestEvent("AboutTranslationsTest:SectionHeightsChanged", {
      sourceSectionHeightChange: sourceSectionHeightChange ?? "unchanged",
      targetSectionHeightChange: targetSectionHeightChange ?? "unchanged",
    });
  }

  /**
   * Calculates the content heights in both the source and target sections,
   * then syncs them to the maximum calculated content height among the two.
   *
   * Prefer calling #requestSectionHeightsUpdate to make it clear whether this function
   * needs to run immediately, or is okay to be scheduled as a callback.
   *
   * @see {AboutTranslations#requestSectionHeightsUpdate}
   */
  #synchronizeSectionsToMaxContentHeight() {
    const {
      detectedLanguageUnsupportedMessage,
      sourceSection,
      sourceSectionTextArea,
      targetSection,
      targetSectionTextArea,
      translationErrorMessage,
    } = this.elements;

    const sourceSectionHeightBefore = Number.parseFloat(
      sourceSection?.style.height
    );
    const targetSectionHeightBefore = Number.parseFloat(
      targetSection?.style.height
    );

    sourceSection.style.height = "auto";
    targetSection.style.height = "auto";
    sourceSectionTextArea.style.height = "auto";
    targetSectionTextArea.style.height = "auto";

    const detectedLanguageUnsupportedHeight =
      detectedLanguageUnsupportedMessage?.getBoundingClientRect().height ?? 0;
    const translationErrorHeight =
      translationErrorMessage?.getBoundingClientRect().height ?? 0;
    const minSectionHeight = AboutTranslations.#maxInteger(
      this.#getMinHeight(sourceSection),
      this.#getMinHeight(targetSection)
    );
    const sourceSectionContentHeight =
      sourceSectionTextArea.scrollHeight + detectedLanguageUnsupportedHeight;
    const targetSectionContentHeight =
      targetSectionTextArea.scrollHeight + translationErrorHeight;
    const maxContentHeight = AboutTranslations.#maxInteger(
      sourceSectionContentHeight,
      targetSectionContentHeight,
      minSectionHeight
    );
    const sectionBorderHeight = AboutTranslations.#maxInteger(
      this.#getBorderAndPaddingHeight(sourceSection),
      this.#getBorderAndPaddingHeight(targetSection)
    );
    const maxSectionHeight = maxContentHeight + sectionBorderHeight;
    const maxSectionHeightPixels = `${maxSectionHeight}px`;
    const sourceSectionTextAreaHeightPixels = `${Math.max(
      maxContentHeight - detectedLanguageUnsupportedHeight,
      0
    )}px`;
    const targetSectionTextAreaHeightPixels = `${Math.max(
      maxContentHeight - translationErrorHeight,
      0
    )}px`;
    const sourceSectionHeightChange = this.#getSectionHeightChange(
      sourceSectionHeightBefore,
      maxSectionHeight
    );
    const targetSectionHeightChange = this.#getSectionHeightChange(
      targetSectionHeightBefore,
      maxSectionHeight
    );

    sourceSection.style.height = maxSectionHeightPixels;
    targetSection.style.height = maxSectionHeightPixels;

    sourceSectionTextArea.style.height = sourceSectionTextAreaHeightPixels;
    targetSectionTextArea.style.height = targetSectionTextAreaHeightPixels;

    this.#dispatchSectionHeightsChangedEvent({
      sourceSectionHeightChange,
      targetSectionHeightChange,
    });
  }

  /**
   * Calculates the content heights in both the source and target sections,
   * then sizes each section to its own calculated content height.
   *
   * Prefer calling #requestSectionHeightsUpdate to make it clear whether this function
   * needs to run immediately, or is okay to be scheduled as a callback.
   *
   * @see {AboutTranslations#requestSectionHeightsUpdate}
   */
  #resizeSectionsToIndividualContentHeights() {
    const {
      detectedLanguageUnsupportedMessage,
      sourceSection,
      sourceSectionTextArea,
      targetSection,
      targetSectionTextArea,
      translationErrorMessage,
    } = this.elements;

    const sourceSectionHeightBefore = Number.parseFloat(
      sourceSection?.style.height
    );
    const targetSectionHeightBefore = Number.parseFloat(
      targetSection?.style.height
    );

    sourceSection.style.height = "auto";
    targetSection.style.height = "auto";
    sourceSectionTextArea.style.height = "auto";
    targetSectionTextArea.style.height = "auto";

    const detectedLanguageUnsupportedHeight =
      detectedLanguageUnsupportedMessage?.getBoundingClientRect().height ?? 0;
    const translationErrorHeight =
      translationErrorMessage?.getBoundingClientRect().height ?? 0;
    const sourceMinHeight = this.#getMinHeight(sourceSection);
    const targetMinHeight = this.#getMinHeight(targetSection);
    const sourceContentHeight = AboutTranslations.#maxInteger(
      sourceSectionTextArea.scrollHeight + detectedLanguageUnsupportedHeight,
      sourceMinHeight
    );
    const targetContentHeight = AboutTranslations.#maxInteger(
      targetSectionTextArea.scrollHeight + translationErrorHeight,
      targetMinHeight
    );
    const sourceSectionHeight =
      sourceContentHeight + this.#getBorderAndPaddingHeight(sourceSection);
    const targetSectionHeight =
      targetContentHeight + this.#getBorderAndPaddingHeight(targetSection);
    const sourceSectionTextAreaHeightPixels = `${Math.max(
      sourceContentHeight - detectedLanguageUnsupportedHeight,
      0
    )}px`;
    const targetSectionTextAreaHeightPixels = `${Math.max(
      targetContentHeight - translationErrorHeight,
      0
    )}px`;
    const sourceSectionHeightChange = this.#getSectionHeightChange(
      sourceSectionHeightBefore,
      sourceSectionHeight
    );
    const targetSectionHeightChange = this.#getSectionHeightChange(
      targetSectionHeightBefore,
      targetSectionHeight
    );

    sourceSection.style.height = `${sourceSectionHeight}px`;
    targetSection.style.height = `${targetSectionHeight}px`;
    sourceSectionTextArea.style.height = sourceSectionTextAreaHeightPixels;
    targetSectionTextArea.style.height = targetSectionTextAreaHeightPixels;

    this.#dispatchSectionHeightsChangedEvent({
      sourceSectionHeightChange,
      targetSectionHeightChange,
    });
  }
}

/**
 * A promise used to ensure the about:translations page initializes once.
 *
 * @type {Promise<void> | null}
 */
let aboutTranslationsInitPromise = null;

/**
 * Initializes the about:translations page if it has not been initialized yet.
 *
 * @returns {Promise<void>}
 */
async function ensureAboutTranslationsInitialized() {
  if (aboutTranslationsInitPromise) {
    return aboutTranslationsInitPromise;
  }

  aboutTranslationsInitPromise = AT_isTranslationEngineSupported()
    .then(async isTranslationsEngineSupported => {
      window.aboutTranslations = new AboutTranslations(
        isTranslationsEngineSupported
      );
      await window.aboutTranslations.ready();
    })
    .catch(error => {
      AT_logError(error);
    });

  return aboutTranslationsInitPromise;
}

/**
 * Marks that the document is ready for automated testing to being making assertions.
 *
 * @returns {void}
 */
function markReadyForTesting() {
  document.body.setAttribute("ready-for-testing", "");
}

/**
 * Whether the initial enabled state has been applied.
 *
 * @type {boolean}
 */
let initialEnabledStateApplied = false;

/**
 * Applies the enabled state to the about:translations UI.
 *
 * @param {boolean} enabled
 * @returns {Promise<void>}
 */
async function setFeatureEnabledState(enabled) {
  await ensureAboutTranslationsInitialized();

  if (!window.aboutTranslations) {
    return;
  }

  if (enabled) {
    await window.aboutTranslations.onFeatureEnabled();
  } else {
    await window.aboutTranslations.onFeatureDisabled();
  }

  if (initialEnabledStateApplied) {
    dispatchTestEvent("AboutTranslationsTest:EnabledStateChanged", { enabled });
  }

  initialEnabledStateApplied = true;
  markReadyForTesting();
}

/**
 * Listen for events coming from the AboutTranslations actor.
 */
window.addEventListener("AboutTranslationsChromeToContent", ({ detail }) => {
  switch (detail.type) {
    case "enabled-state-changed": {
      void setFeatureEnabledState(detail.enabled);
      break;
    }
    case "rebuild-translator": {
      window.aboutTranslations?.rebuildTranslator();
      break;
    }
    case "split-view-state-changed": {
      void ensureAboutTranslationsInitialized().then(() => {
        window.aboutTranslations?.applySplitViewState(detail);
      });
      break;
    }
    default: {
      throw new Error("Unknown AboutTranslationsChromeToContent event.");
    }
  }
});

/**
 * Creates a new message port to handle translation requests for the given
 * language pair.
 *
 * @param {LanguagePair} languagePair
 * @returns {Promise<MessagePort>}
 */
function requestTranslationsPort(languagePair) {
  const { promise, resolve } = Promise.withResolvers();

  /** @param {{data: {type:string, languagePair: LanguagePair, port: MessagePort}}} */
  const getResponse = ({ data }) => {
    if (
      data.type == "GetTranslationsPort" &&
      data.languagePair.sourceLanguage === languagePair.sourceLanguage &&
      data.languagePair.targetLanguage === languagePair.targetLanguage &&
      data.languagePair.sourceVariant == languagePair.sourceVariant &&
      data.languagePair.targetVariant == languagePair.targetVariant
    ) {
      window.removeEventListener("message", getResponse);
      resolve(data.port);
    }
  };

  window.addEventListener("message", getResponse);
  AT_createTranslationsPort(languagePair);

  return promise;
}

/**
 * Creates an input handler that runs callbacks immediately, on a throttle interval,
 * and after input has been idle for a debounce delay.
 *
 * @param {object} settings
 * @param {Function} settings.doEveryTime
 *   Runs for every input event.
 * @param {Function} settings.onThrottle
 *   Runs at most once per throttle interval while input continues.
 * @param {Function} settings.onDebounce
 *   Runs once after input has been idle for the debounce delay.
 * @returns {Function}
 */
function scheduleInputHandling({ doEveryTime, onThrottle, onDebounce }) {
  /** @type {number | null} */
  let debounceTimeoutId = null;

  /** @type {Promise<void> | null} */
  let throttlePromise = null;

  /** @type {object[]} */
  let latestArgs = [];

  return (...args) => {
    latestArgs = args;
    doEveryTime(...args);

    if (!throttlePromise) {
      const { promise, resolve } = Promise.withResolvers();
      throttlePromise = promise
        .then(() => onThrottle(...latestArgs))
        .finally(() => {
          throttlePromise = null;
        });
      setTimeout(resolve, window.THROTTLE_DELAY);
    }

    clearTimeout(debounceTimeoutId);
    debounceTimeoutId = setTimeout(() => {
      debounceTimeoutId = null;
      onDebounce(...latestArgs);
    }, window.DEBOUNCE_DELAY);
  };
}
