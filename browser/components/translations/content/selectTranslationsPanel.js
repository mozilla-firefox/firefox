/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {import("../../../../toolkit/components/translations/translations").SelectTranslationsPanelState} SelectTranslationsPanelState
 * @typedef {import("../../../../toolkit/components/translations/translations").LanguagePair} LanguagePair
 */

ChromeUtils.defineESModuleGetters(this, {
  LanguageDetector:
    "resource://gre/modules/translations/LanguageDetector.sys.mjs",
  TranslationsPanelShared:
    "chrome://browser/content/translations/TranslationsPanelShared.sys.mjs",
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  Translator: "chrome://global/content/translations/Translator.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "ClipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  Ci.nsIClipboardHelper
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "GfxInfo",
  "@mozilla.org/gfx/info;1",
  Ci.nsIGfxInfo
);

/**
 * This singleton class controls the SelectTranslations panel.
 *
 * A global instance of this class is created once per top ChromeWindow and is initialized
 * when the context menu is opened in that window.
 *
 * See the comment above TranslationsParent for more details.
 *
 * @see TranslationsParent
 */
var SelectTranslationsPanel = new (class {
  /** @type {Console?} */
  #console;

  /**
   * Lazily get a console instance. Note that this script is loaded in very early to
   * the browser loading process, and may run before the console is available. In
   * this case the console will return as `undefined`.
   *
   * @returns {Console | void}
   */
  get console() {
    if (!this.#console) {
      try {
        this.#console = console.createInstance({
          maxLogLevelPref: "browser.translations.logLevel",
          prefix: "Translations",
        });
      } catch {
        // The console may not be initialized yet.
      }
    }
    return this.#console;
  }

  /**
   * The textarea height for shorter text.
   *
   * @type {string}
   */
  #shortTextHeight = "8em";

  /**
   * Retrieves the read-only textarea height for shorter text.
   *
   * @see #shortTextHeight
   */
  get shortTextHeight() {
    return this.#shortTextHeight;
  }

  /**
   * The textarea height for shorter text.
   *
   * @type {string}
   */
  #longTextHeight = "16em";

  /**
   * The alignment position value of the panel when it opened.
   *
   * We want to cache this value because some alignments, such as "before_start"
   * and "before_end" will cause the panel to expand upward from the top edge
   * when the user is trying to resize the text-area by dragging the resizer downward.
   *
   * Knowing this value helps us determine if we should disable the textarea resizer
   * based on how and where the panel was opened.
   *
   * @see #maybeEnableTextAreaResizer
   */
  #alignmentPosition = "";

  /**
   * A value to cache the most recent state that caused the panel's UI to update.
   *
   * The event-driven nature of this code can sometimes make redundant calls to
   * idempotent UI updates, however the telemetry data is not idempotent and will
   * be double counted.
   *
   * This value allows us to avoid double-counting telemetry if we're making a
   * redundant call to a UI update.
   *
   * @type {string}
   */
  #mostRecentUIPhase = "closed";

  /**
   * A cached value for the count of words in the source text as determined by the Intl.Segmenter
   * for the currently selected from-language, which is reported to telemetry. This prevents us
   * from having to allocate resource for the segmenter multiple times if the user changes the target
   * language.
   *
   * This value should be invalidated when the panel opens and when the from-language is changed.
   *
   * @type {number}
   */
  #sourceTextWordCount = undefined;

  /**
   * Cached information about the document's detected language and the user's
   * current language settings, useful for populating telemetry events.
   *
   * @type {object}
   */
  #languageInfo = {
    docLangTag: undefined,
    isDocLangTagSupported: undefined,
  };

  /**
   * Retrieves the read-only textarea height for longer text.
   *
   * @see #longTextHeight
   */
  get longTextHeight() {
    return this.#longTextHeight;
  }

  /**
   * The threshold used to determine when the panel should
   * use the short text-height vs. the long-text height.
   *
   * @type {number}
   */
  #textLengthThreshold = 800;

  /**
   * Retrieves the read-only text-length threshold.
   *
   * @see #textLengthThreshold
   */
  get textLengthThreshold() {
    return this.#textLengthThreshold;
  }

  /**
   * The localized placeholder text to display when idle.
   *
   * @type {string}
   */
  #idlePlaceholderText;

  /**
   * The localized placeholder text to display when translating.
   *
   * @type {string}
   */
  #translatingPlaceholderText;

  /**
   * Where the lazy elements are stored.
   *
   * @type {Record<string, Element>?}
   */
  #lazyElements;

  /**
   * Set to true the first time event listeners are initialized.
   *
   * @type {boolean}
   */
  #eventListenersInitialized = false;

  /**
   * This value is true if this page does not allow Full Page Translations,
   * e.g. PDFs, reader mode, internal Firefox pages.
   *
   * Many of these are cases where the SelectTranslationsPanel is available
   * even though the FullPageTranslationsPanel is not, so this helps inform
   * whether the translate-full-page button should be allowed in this context.
   *
   * @type {boolean}
   */
  #isFullPageTranslationsRestrictedForPage = true;

  /**
   * The BCP-47 language tag of the active target language for Full-Page Translations,
   * if available. This may not be available if Full-Page Translations is not currently
   * active in the current tab of the current window, or if Full-Page Translations is
   * restricted on the current page.
   *
   * @type { string | undefined }
   */
  #activeFullPageTranslationsTargetLanguage = undefined;

  /**
   * The internal state of the SelectTranslationsPanel.
   *
   * @type {SelectTranslationsPanelState}
   */
  #translationState = { phase: "closed" };

  /**
   * An Id that increments with each translation, used to help keep track
   * of whether an active translation request continue its progression or
   * stop due to the existence of a newer translation request.
   *
   * @type {number}
   */
  #translationId = 0;

  /**
   * Lazily creates the dom elements, and lazily selects them.
   *
   * @returns {Record<string, Element>}
   */
  get elements() {
    if (!this.#lazyElements) {
      // Lazily turn the template into a DOM element.
      /** @type {HTMLTemplateElement} */
      const wrapper = document.getElementById(
        "template-select-translations-panel"
      );

      const panel = wrapper.content.firstElementChild;
      wrapper.replaceWith(wrapper.content);

      // Lazily select the elements.
      this.#lazyElements = {
        panel,
      };

      TranslationsPanelShared.defineLazyElements(document, this.#lazyElements, {
        cancelButton: "select-translations-panel-cancel-button",
        copyButton: "select-translations-panel-copy-button",
        doneButtonPrimary: "select-translations-panel-done-button-primary",
        doneButtonSecondary: "select-translations-panel-done-button-secondary",
        fromLabel: "select-translations-panel-from-label",
        fromMenuList: "select-translations-panel-from",
        fromMenuPopup: "select-translations-panel-from-menupopup",
        header: "select-translations-panel-header",
        initFailureContent: "select-translations-panel-init-failure-content",
        initFailureMessageBar:
          "select-translations-panel-init-failure-message-bar",
        mainContent: "select-translations-panel-main-content",
        settingsButton: "select-translations-panel-settings-button",
        textArea: "select-translations-panel-text-area",
        toLabel: "select-translations-panel-to-label",
        toMenuList: "select-translations-panel-to",
        toMenuPopup: "select-translations-panel-to-menupopup",
        translateButton: "select-translations-panel-translate-button",
        translateFullPageButton:
          "select-translations-panel-translate-full-page-button",
        translationFailureMessageBar:
          "select-translations-panel-translation-failure-message-bar",
        tryAgainButton: "select-translations-panel-try-again-button",
        tryAnotherSourceMenuList:
          "select-translations-panel-try-another-language",
        tryAnotherSourceMenuPopup:
          "select-translations-panel-try-another-language-menupopup",
        unsupportedLanguageContent:
          "select-translations-panel-unsupported-language-content",
        unsupportedLanguageMessageBar:
          "select-translations-panel-unsupported-language-message-bar",
      });
    }

    return this.#lazyElements;
  }

  /**
   * Attempts to determine the best language tag to use as the source language for translation.
   * If the detected language is not supported, attempts to fallback to the document's language tag.
   *
   * @param {string} textToTranslate - The text for which the language detection and target language retrieval are performed.
   *
   * @returns {Promise<string>} - The code of a supported language, a supported document language, or the top detected language.
   */
  async getTopSupportedDetectedLanguage(textToTranslate) {
    // We want to refresh our cache every time we make a determination about the detected source language,
    // even if we never make it to the section of the logic below where we consider the document language,
    // otherwise the incorrect, cached document language may be reported to telemetry.
    const { docLangTag, isDocLangTagSupported } = this.#getLanguageInfo(
      /* forceFetch */ true
    );

    // First see if any of the detected languages are supported and return it if so.
    const { language, languages } =
      await LanguageDetector.detectLanguage(textToTranslate);
    const languagePairs = await TranslationsParent.getNonPivotLanguagePairs();
    for (const { languageCode } of languages) {
      const compatibleLangTag =
        TranslationsParent.findCompatibleSourceLangTagSync(
          languageCode,
          languagePairs
        );
      if (compatibleLangTag) {
        return compatibleLangTag;
      }
    }

    // Since none of the detected languages were supported, check to see if the
    // document has a specified language tag that is supported.
    if (isDocLangTagSupported) {
      return docLangTag;
    }

    // No supported language was found, so return the top detected language
    // to inform the panel's unsupported language state.
    return language;
  }

  /**
   * Attempts to cache the languageInformation for this page and the user's current settings.
   * This data is helpful for telemetry. Leaves the cache unpopulated if the info failed to be
   * retrieved.
   *
   * @param {boolean} forceFetch - Clears the cache and attempts to refetch data if true.
   *
   * @returns {object} - The cached language-info object.
   */
  #getLanguageInfo(forceFetch = false) {
    if (!forceFetch && this.#languageInfo.docLangTag !== undefined) {
      return this.#languageInfo;
    }

    this.#isFullPageTranslationsRestrictedForPage =
      TranslationsParent.isFullPageTranslationsRestrictedForPage(
        gBrowser.selectedBrowser
      );
    this.#activeFullPageTranslationsTargetLanguage = this
      .#isFullPageTranslationsRestrictedForPage
      ? undefined
      : this.#maybeGetActiveFullPageTranslationsTargetLanguage();

    this.#languageInfo = {
      docLangTag: undefined,
      isDocLangTagSupported: undefined,
    };

    try {
      const actor = TranslationsParent.getTranslationsActor(
        gBrowser.selectedBrowser
      );
      const {
        detectedLanguages: { docLangTag, isDocLangTagSupported },
      } = actor.languageState;
      this.#languageInfo = {
        docLangTag:
          // If Full-Page Translations (FPT) is active, we need to assume that the effective
          // document language tag matches the language of the FPT target language, otherwise,
          // if FPT is not active, we can take the real docLangTag value.
          this.#activeFullPageTranslationsTargetLanguage ?? docLangTag,
        isDocLangTagSupported,
      };
    } catch (error) {
      // Failed to retrieve the Translations actor to detect the document language.
      // This is most likely due to attempting to retrieve the actor in a page that
      // is restricted for Full Page Translations, such as a PDF or reader mode, but
      // Select Translations is often still available, so we can safely continue to
      // the final return fallback.
      if (
        !TranslationsParent.isFullPageTranslationsRestrictedForPage(
          gBrowser.selectedBrowser
        )
      ) {
        // If we failed to retrieve the TranslationsParent actor on a non-restricted page,
        // we should warn about this, because it is unexpected. The SelectTranslationsPanel
        // itself will display an error state if this causes a failure, and this will help
        // diagnose the issue if this scenario should ever occur.
        this.console?.warn(
          "Failed to retrieve the TranslationsParent actor on a page where Full Page Translations is not restricted."
        );
        this.console?.error(error);
      }
    }

    return this.#languageInfo;
  }

  /**
   * Detects the language of the provided text and retrieves a language pair for translation
   * based on user settings.
   *
   * @param {string} textToTranslate - The text for which the language detection and target language retrieval are performed.
   * @returns {Promise<{sourceLanguage?: string, targetLanguage?: string}>} - An object containing the language pair for the translation.
   *   The `sourceLanguage` property is omitted if it is a language that is not currently supported by Firefox Translations.
   */
  async getLangPairPromise(textToTranslate) {
    if (
      TranslationsParent.isInAutomation() &&
      !TranslationsParent.isTranslationsEngineMocked()
    ) {
      // If we are in automation, and the Translations Engine is NOT mocked, then that means
      // we are in a test case in which we are not explicitly testing Select Translations,
      // and the code to get the supported languages below will not be available. However,
      // we still need to ensure that the translate-selection menuitem in the context menu
      // is compatible with all code in other tests, so we will return "en" for the purpose
      // of being able to localize and display the context-menu item in other test cases.
      return { targetLanguage: "en" };
    }

    const sourceLanguage =
      await SelectTranslationsPanel.getTopSupportedDetectedLanguage(
        textToTranslate
      );
    const targetLanguage =
      await TranslationsParent.getTopPreferredSupportedToLang({
        // Avoid offering a same-language to same-language translation if we can.
        excludeLangTags: [sourceLanguage],
      });

    return { sourceLanguage, targetLanguage };
  }

  /**
   * Close the Select Translations Panel.
   */
  close() {
    PanelMultiView.hidePopup(this.elements.panel);
    this.#mostRecentUIPhase = "closed";
  }

  /**
   * Ensures that the from-language and to-language dropdowns are built.
   *
   * This can be called every time the popup is shown, since it will retry
   * when there is an error (such as a network error) or be a no-op if the
   * dropdowns have already been initialized.
   */
  async #ensureLangListsBuilt() {
    await TranslationsPanelShared.ensureLangListsBuilt(document, this);
  }

  /**
   * Initializes the selected value of the given language dropdown based on the language tag.
   *
   * @param {string} langTag - A BCP-47 language tag.
   * @param {Element} menuList - The menu list element to update.
   *
   * @returns {Promise<void>}
   */
  async #initializeLanguageMenuList(langTag, menuList) {
    const compatibleLangTag =
      menuList.id === this.elements.fromMenuList.id
        ? await TranslationsParent.findCompatibleSourceLangTag(langTag)
        : await TranslationsParent.findCompatibleTargetLangTag(langTag);

    if (compatibleLangTag) {
      // Remove the data-l10n-id because the menulist label will
      // be populated from the supported language's display name.
      menuList.removeAttribute("data-l10n-id");
      menuList.value = compatibleLangTag;
    } else {
      await this.#deselectLanguage(menuList);
    }
  }

  /**
   * Initializes the selected values of the from-language and to-language menu
   * lists based on the result of the given language pair promise.
   *
   * @param {Promise<{sourceLanguage?: string, targetLanguage?: string}>} langPairPromise
   *
   * @returns {Promise<void>}
   */
  async #initializeLanguageMenuLists(langPairPromise) {
    const { sourceLanguage, targetLanguage } = await langPairPromise;
    const {
      fromMenuList,
      fromMenuPopup,
      toMenuList,
      toMenuPopup,
      tryAnotherSourceMenuList,
    } = this.elements;

    await Promise.all([
      this.#initializeLanguageMenuList(sourceLanguage, fromMenuList),
      this.#initializeLanguageMenuList(targetLanguage, toMenuList),
      this.#initializeLanguageMenuList(null, tryAnotherSourceMenuList),
    ]);

    this.#maybeTranslateOnEvents(["keypress"], fromMenuList);
    this.#maybeTranslateOnEvents(["keypress"], toMenuList);

    this.#maybeTranslateOnEvents(["popuphidden"], fromMenuPopup);
    this.#maybeTranslateOnEvents(["popuphidden"], toMenuPopup);
  }

  /**
   * Initializes event listeners on the panel class the first time
   * this function is called, and is a no-op on subsequent calls.
   */
  #initializeEventListeners() {
    if (this.#eventListenersInitialized) {
      // Event listeners have already been initialized, do nothing.
      return;
    }

    const { panel, fromMenuList, toMenuList, tryAnotherSourceMenuList } =
      this.elements;

    // XUL buttons on macOS do not handle the Enter key by default for
    // the focused element, so we must listen for the Enter key manually:
    // https://searchfox.org/mozilla-central/rev/4c8627a76e2e0a9b49c2b673424da478e08715ad/dom/xul/XULButtonElement.cpp#563-579
    if (AppConstants.platform === "macosx") {
      panel.addEventListener("keypress", this);
    }
    panel.addEventListener("popupshown", this);
    panel.addEventListener("popuphidden", this);

    panel.addEventListener("command", this);
    fromMenuList.addEventListener("command", this);
    toMenuList.addEventListener("command", this);
    tryAnotherSourceMenuList.addEventListener("command", this);

    this.#eventListenersInitialized = true;
  }

  /**
   * Opens the panel, ensuring the panel's UI and state are initialized correctly.
   *
   * @param {Event} event - The triggering event for opening the panel.
   * @param {number} screenX - The x-axis location of the screen at which to open the popup.
   * @param {number} screenY - The y-axis location of the screen at which to open the popup.
   * @param {string} sourceText - The text to translate.
   * @param {boolean} isTextSelected - True if the text comes from a selection, false if it comes from a hyperlink.
   * @param {Promise} langPairPromise - Promise resolving to language pair data for initializing dropdowns.
   * @param {boolean} maintainFlow - Whether the telemetry flow-id should be persisted or assigned a new id.
   *
   * @returns {Promise<void>}
   */
  async open(
    event,
    screenX,
    screenY,
    sourceText,
    isTextSelected,
    langPairPromise,
    maintainFlow = false
  ) {
    if (this.#isOpen()) {
      await this.#forceReopen(
        event,
        screenX,
        screenY,
        sourceText,
        isTextSelected,
        langPairPromise
      );
      return;
    }

    const { sourceLanguage, targetLanguage } = await langPairPromise;
    const { docLangTag } = this.#getLanguageInfo();

    TranslationsParent.telemetry()
      .selectTranslationsPanel()
      .onOpen({
        maintainFlow,
        docLangTag,
        sourceLanguage,
        targetLanguage,
        textSource: isTextSelected ? "selection" : "hyperlink",
      });

    try {
      this.#sourceTextWordCount = undefined;
      this.#initializeEventListeners();
      await this.#ensureLangListsBuilt();
      await Promise.all([
        this.#cachePlaceholderText(),
        this.#initializeLanguageMenuLists(langPairPromise),
        this.#registerSourceText(sourceText, langPairPromise),
      ]);
      this.#maybeRequestTranslation();
    } catch (error) {
      this.console?.error(error);
      this.#changeStateToInitFailure(
        event,
        screenX,
        screenY,
        sourceText,
        isTextSelected,
        langPairPromise
      );
    }

    this.#openPopup(event, screenX, screenY);
  }

  /**
   * Attempts to retrieve the language tag of the requested target language
   * for Full Page Translations, if Full Page Translations is active on the page
   * within the active tab of the active window.
   *
   * @returns {string | undefined} - The BCP-47 language tag.
   */
  #maybeGetActiveFullPageTranslationsTargetLanguage() {
    try {
      const { requestedLanguagePair } = TranslationsParent.getTranslationsActor(
        gBrowser.selectedBrowser
      ).languageState;
      return requestedLanguagePair?.targetLanguage;
    } catch {
      this.console.warn("Failed to retrieve the TranslationsParent actor.");
    }
    return undefined;
  }

  /**
   * Forces the panel to close and reopen at the same location.
   *
   * This should never be called in the regular flow of events, but is good to have in case
   * the panel somehow gets into an invalid state.
   *
   * @param {Event} event - The triggering event for opening the panel.
   * @param {number} screenX - The x-axis location of the screen at which to open the popup.
   * @param {number} screenY - The y-axis location of the screen at which to open the popup.
   * @param {string} sourceText - The text to translate.
   * @param {boolean} isTextSelected - True if the text comes from a selection, false if it comes from a hyperlink.
   * @param {Promise} langPairPromise - Promise resolving to language pair data for initializing dropdowns.
   *
   * @returns {Promise<void>}
   */
  async #forceReopen(
    event,
    screenX,
    screenY,
    sourceText,
    isTextSelected,
    langPairPromise
  ) {
    this.console?.warn("The SelectTranslationsPanel was forced to reopen.");
    this.close();
    this.#changeStateToClosed();
    await this.open(
      event,
      screenX,
      screenY,
      sourceText,
      isTextSelected,
      langPairPromise
    );
  }

  /**
   * Opens the panel popup at a location on the screen.
   *
   * @param {Event} event - The event that triggers the popup opening.
   * @param {number} screenX - The x-axis location of the screen at which to open the popup.
   * @param {number} screenY - The y-axis location of the screen at which to open the popup.
   */
  #openPopup(event, screenX, screenY) {
    this.console?.log("Showing SelectTranslationsPanel");
    const { panel } = this.elements;
    this.#cacheAlignmentPositionOnOpen();
    panel.openPopupAtScreenRect(
      "after_start",
      screenX,
      screenY,
      /* width */ 0,
      /* height */ 0,
      /* isContextMenu */ false,
      /* attributesOverride */ false,
      event
    );
  }

  /**
   * Resets the cached alignment-position value and adds an event listener
   * to set the value again when the panel is positioned before opening.
   * See the comment on the data member for more details.
   *
   * @see #alignmentPosition
   */
  #cacheAlignmentPositionOnOpen() {
    const { panel } = this.elements;
    this.#alignmentPosition = "";
    panel.addEventListener(
      "popuppositioned",
      popupPositionedEvent => {
        // Cache the alignment position when the popup is opened.
        this.#alignmentPosition = popupPositionedEvent.alignmentPosition;
      },
      { once: true }
    );
  }

  /**
   * Adds the source text to the translation state and adapts the size of the text area based
   * on the length of the text.
   *
   * @param {string} sourceText - The text to translate.
   * @param {Promise<{sourceLanguage?: string, targetLanguage?: string}>} langPairPromise
   *
   * @returns {Promise<void>}
   */
  async #registerSourceText(sourceText, langPairPromise) {
    const { textArea } = this.elements;
    const { sourceLanguage, targetLanguage } = await langPairPromise;
    const compatibleFromLang =
      await TranslationsParent.findCompatibleSourceLangTag(sourceLanguage);

    if (compatibleFromLang) {
      this.#changeStateTo("idle", /* retainEntries */ false, {
        sourceText,
        sourceLanguage: compatibleFromLang,
        targetLanguage,
      });
    } else {
      this.#changeStateTo("unsupported", /* retainEntries */ false, {
        sourceText,
        detectedLanguage: sourceLanguage,
        targetLanguage,
      });
    }

    textArea.value = "";
    textArea.style.resize = "none";
    textArea.style.maxHeight = null;
    if (sourceText.length < SelectTranslationsPanel.textLengthThreshold) {
      textArea.style.height = SelectTranslationsPanel.shortTextHeight;
    } else {
      textArea.style.height = SelectTranslationsPanel.longTextHeight;
    }

    this.#maybeTranslateOnEvents(["focus"], textArea);
  }

  /**
   * Caches the localized text to use as placeholders.
   */
  async #cachePlaceholderText() {
    const [idleText, translatingText] = await document.l10n.formatValues([
      { id: "select-translations-panel-idle-placeholder-text" },
      { id: "select-translations-panel-translating-placeholder-text" },
    ]);
    this.#idlePlaceholderText = idleText;
    this.#translatingPlaceholderText = translatingText;
  }

  /**
   * Opens the settings menu popup at the settings button gear-icon.
   */
  #openSettingsPopup() {
    TranslationsParent.telemetry()
      .selectTranslationsPanel()
      .onOpenSettingsMenu();

    const { settingsButton } = this.elements;
    const popup = settingsButton.ownerDocument.getElementById(
      "select-translations-panel-settings-menupopup"
    );

    popup.openPopup(settingsButton, "after_start");
  }

  /**
   * Opens the "About translation in Firefox" Mozilla support page in a new tab.
   */
  onAboutTranslations() {
    TranslationsParent.telemetry()
      .selectTranslationsPanel()
      .onAboutTranslations();

    this.close();
    const window =
      gBrowser.selectedBrowser.browsingContext.top.embedderElement
        .documentGlobal;
    window.openTrustedLinkIn(
      "https://support.mozilla.org/kb/website-translation",
      "tab",
      {
        forceForeground: true,
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      }
    );
  }

  /**
   * Opens the Translations section of about:preferences in a new tab.
   */
  openTranslationsSettingsPage() {
    TranslationsParent.telemetry()
      .selectTranslationsPanel()
      .onTranslationSettings();

    this.close();
    const window =
      gBrowser.selectedBrowser.browsingContext.top.embedderElement
        .documentGlobal;
    window.openTrustedLinkIn("about:preferences#general-translations", "tab");
  }

  /**
   * Handles events when a command event is triggered within the panel.
   *
   * @param {Element} target - The event target
   */
  #handleCommandEvent(target) {
    const {
      cancelButton,
      copyButton,
      doneButtonPrimary,
      doneButtonSecondary,
      fromMenuList,
      fromMenuPopup,
      settingsButton,
      toMenuList,
      toMenuPopup,
      translateButton,
      translateFullPageButton,
      tryAgainButton,
      tryAnotherSourceMenuList,
      tryAnotherSourceMenuPopup,
    } = this.elements;
    switch (target.id) {
      case cancelButton.id: {
        this.onClickCancelButton();
        break;
      }
      case copyButton.id: {
        this.onClickCopyButton();
        break;
      }
      case doneButtonPrimary.id:
      case doneButtonSecondary.id: {
        this.onClickDoneButton();
        break;
      }
      case fromMenuList.id:
      case fromMenuPopup.id: {
        this.onChangeFromLanguage();
        break;
      }
      case settingsButton.id: {
        this.#openSettingsPopup();
        break;
      }
      case toMenuList.id:
      case toMenuPopup.id: {
        this.onChangeToLanguage();
        break;
      }
      case translateButton.id: {
        this.onClickTranslateButton();
        break;
      }
      case translateFullPageButton.id: {
        this.onClickTranslateFullPageButton();
        break;
      }
      case tryAgainButton.id: {
        this.onClickTryAgainButton();
        break;
      }
      case tryAnotherSourceMenuList.id:
      case tryAnotherSourceMenuPopup.id: {
        this.onChangeTryAnotherSourceLanguage();
        break;
      }
    }
  }

  /**
   * Handles events when the Enter key is pressed within the panel.
   *
   * @param {Element} target - The event target
   */
  #handleEnterKeyPressed(target) {
    const {
      cancelButton,
      copyButton,
      doneButtonPrimary,
      doneButtonSecondary,
      settingsButton,
      translateButton,
      translateFullPageButton,
      tryAgainButton,
    } = this.elements;

    switch (target.id) {
      case cancelButton.id: {
        this.onClickCancelButton();
        break;
      }
      case copyButton.id: {
        this.onClickCopyButton();
        break;
      }
      case doneButtonPrimary.id:
      case doneButtonSecondary.id: {
        this.onClickDoneButton();
        break;
      }
      case settingsButton.id: {
        this.#openSettingsPopup();
        break;
      }
      case translateButton.id: {
        this.onClickTranslateButton();
        break;
      }
      case translateFullPageButton.id: {
        this.onClickTranslateFullPageButton();
        break;
      }
      case tryAgainButton.id: {
        this.onClickTryAgainButton();
        break;
      }
    }
  }

  /**
   * Conditionally enables the resizer component at the bottom corner of the text area,
   * and limits the maximum height that the textarea can be resized.
   *
   * For systems using Wayland, this function ensures that the panel cannot be resized past
   * the border of the current Firefox window.
   *
   * For all other systems, this function ensures that the panel cannot be resized past the
   * bottom edge of the available screen space.
   */
  #maybeEnableTextAreaResizer() {
    // The alignment position of the panel is determined during the "popuppositioned" event
    // when the panel opens. The alignment positions help us determine in which orientation
    // the panel is anchored to the screen space.
    //
    // *  "after_start": The panel is anchored at the top-left     corner in LTR locales, top-right    in RTL locales.
    // *    "after_end": The panel is anchored at the top-right    corner in LTR locales, top-left     in RTL locales.
    // * "before_start": The panel is anchored at the bottom-left  corner in LTR locales, bottom-right in RTL locales.
    // *   "before_end": The panel is anchored at the bottom-right corner in LTR locales, bottom-left  in RTL locales.
    //
    //   ┌─Anchor(LTR)          ┌─Anchor(RTL)
    //   │       Anchor(RTL)─┐  │       Anchor(LTR)─┐
    //   │                   │  │                   │
    //   x───────────────────x  x───────────────────x
    //   │                   │  │                   │
    //   │       Panel       │  │       Panel       │
    //   │   "after_start"   │  │    "after_end"    │
    //   │                   │  │                   │
    //   └───────────────────┘  └───────────────────┘
    //
    //   ┌───────────────────┐  ┌───────────────────┐
    //   │                   │  │                   │
    //   │       Panel       │  │       Panel       │
    //   │   "before_start"  │  │    "before_end"   │
    //   │                   │  │                   │
    //   x───────────────────x  x───────────────────x
    //   │                   │  │                   │
    //   │       Anchor(RTL)─┘  │       Anchor(LTR)─┘
    //   └─Anchor(LTR)          └─Anchor(RTL)
    //
    // The default choice for the panel is "after_start", to match the content context menu's alignment. However, it is
    // possible to end up with any of the four combinations. Before the panel is opened, the XUL popup manager needs to
    // make a determination about the size of the panel and whether or not it will fit within the visible screen area with
    // the intended alignment. The manager may change the panel's alignment before opening to ensure the panel is fully visible.
    //
    // For example, if the panel is opened such that the bottom edge would be rendered off screen, then the XUL popup manager
    // will change the alignment from "after_start" to "before_start", anchoring the panel's bottom corner to the target screen
    // location instead of its top corner. This transformation ensures that the whole of the panel is visible on the screen.
    //
    // When the panel is anchored by one of its bottom corners (the "before_..." options), then it causes unintentionally odd
    // behavior where dragging the text-area resizer downward with the mouse actually grows the panel's top edge upward, since
    // the bottom of the panel is anchored in place. We want to disable the resizer if the panel was positioned to be anchored
    // from one of its bottom corners.
    switch (this.#alignmentPosition) {
      case "after_start":
      case "after_end": {
        // The text-area resizer will act normally.
        break;
      }
      case "before_start":
      case "before_end": {
        // The text-area resizer increase the size of the panel from the top edge even
        // though the user is dragging the resizer downward with the mouse.
        this.console?.debug(
          `Disabling text-area resizer due to panel alignment position: "${
            this.#alignmentPosition
          }"`
        );
        return;
      }
      default: {
        this.console?.debug(
          `Disabling text-area resizer due to unexpected panel alignment position: "${
            this.#alignmentPosition
          }"`
        );
        return;
      }
    }

    const { panel, textArea } = this.elements;

    if (textArea.style.maxHeight) {
      this.console?.debug(
        "The text-area resizer has already been enabled at the current panel location."
      );
      return;
    }

    // The visible height of the text area on the screen.
    const textAreaClientHeight = textArea.clientHeight;

    // The height of the text in the text area, including text that has overflowed beyond the client height.
    const textAreaScrollHeight = textArea.scrollHeight;

    if (textAreaScrollHeight <= textAreaClientHeight) {
      this.console?.debug(
        "Disabling text-area resizer because the text content fits within the text area."
      );
      return;
    }

    // Wayland has no concept of "screen coordinates" which causes getOuterScreenRect to always
    // return { x: 0, y: 0 } for the location. As such, we cannot tell on Wayland where the panel
    // is positioned relative to the screen, so we must restrict the panel's resizing limits to be
    // within the Firefox window itself.
    let isWayland = false;
    try {
      isWayland = GfxInfo.windowProtocol === "wayland";
    } catch (error) {
      if (AppConstants.platform === "linux") {
        this.console?.warn(error);
        this.console?.debug(
          "Disabling text-area resizer because we were unable to retrieve the window protocol on Linux."
        );
        return;
      }
      // Since we're not on Linux, we can safely continue with isWayland = false.
    }

    const {
      top: panelTop,
      left: panelLeft,
      bottom: panelBottom,
      right: panelRight,
    } = isWayland
      ? // The panel's location relative to the Firefox window.
        panel.getBoundingClientRect()
      : // The panel's location relative to the screen.
        panel.getOuterScreenRect();

    const window =
      gBrowser.selectedBrowser.browsingContext.top.embedderElement
        .documentGlobal;

    if (isWayland) {
      if (panelTop < 0) {
        this.console?.debug(
          "Disabling text-area resizer because the panel outside the top edge of the window on Wayland."
        );
        return;
      }
      if (panelBottom > window.innerHeight) {
        this.console?.debug(
          "Disabling text-area resizer because the panel is outside the bottom edge of the window on Wayland."
        );
        return;
      }
      if (panelLeft < 0) {
        this.console?.debug(
          "Disabling text-area resizer because the panel outside the left edge of the window on Wayland."
        );
        return;
      }
      if (panelRight > window.innerWidth) {
        this.console?.debug(
          "Disabling text-area resizer because the panel is outside the right edge of the window on Wayland."
        );
        return;
      }
    } else if (!panelBottom) {
      // The location of the panel was unable to be retrieved by getOuterScreenRect() so we should not enable
      // resizing the text area because we cannot accurately guard against the user resizing the panel off of
      // the bottom edge of the screen. The worst case for the user here is that they have to utilize the scroll
      // bar instead of resizing. This happens intermittently, but infrequently.
      this.console?.debug(
        "Disabling text-area resizer because the location of the bottom edge of the panel was unavailable."
      );
      return;
    }

    const availableHeight = isWayland
      ? // The available height of the Firefox window.
        window.innerHeight
      : // The available height of the screen.
        screen.availHeight;

    // The distance in pixels between the bottom edge of the panel to the bottom
    // edge of our available height, which will either be the bottom of the Firefox
    // window on Wayland, otherwise the bottom of the available screen space.
    const panelBottomToBottomEdge = availableHeight - panelBottom;

    // We want to maintain some buffer of pixels between the panel's bottom edge
    // and the bottom edge of our available space, because if they touch, it can
    // cause visual glitching to occur.
    const BOTTOM_EDGE_PIXEL_BUFFER = Math.abs(panelBottom - panelTop) / 5;

    if (panelBottomToBottomEdge < BOTTOM_EDGE_PIXEL_BUFFER) {
      this.console?.debug(
        "Disabling text-area resizer because the bottom of the panel is already close to the bottom edge."
      );
      return;
    }

    // The height that the textarea could grow to before hitting the threshold of the buffer that we
    // intend to keep between the bottom edge of the panel and the bottom edge of available space.
    const textAreaHeightLimitForEdge =
      textAreaClientHeight + panelBottomToBottomEdge - BOTTOM_EDGE_PIXEL_BUFFER;

    // This is an arbitrary ratio, but allowing the panel's text area to span 1/2 of the available
    // vertical real estate, even if it could expand farther, seems like a reasonable constraint.
    const textAreaHeightLimitUpperBound = Math.trunc(availableHeight / 2);

    // The final maximum height that the text area will be allowed to resize to at its current location.
    const textAreaMaxHeight = Math.min(
      textAreaScrollHeight,
      textAreaHeightLimitForEdge,
      textAreaHeightLimitUpperBound
    );

    textArea.style.resize = "vertical";
    textArea.style.maxHeight = `${textAreaMaxHeight}px`;
    this.console?.debug(
      `Enabling text-area resizer with a maximum height of ${textAreaMaxHeight} pixels`
    );
  }

  /**
   * Handles events when a popup is shown within the panel, including showing
   * the panel itself.
   *
   * @param {Element} target - The event target
   */
  #handlePopupShownEvent(target) {
    const { panel } = this.elements;
    switch (target.id) {
      case panel.id: {
        this.#updatePanelUIFromState();
        break;
      }
    }
  }

  /**
   * Handles events when a popup is closed within the panel, including closing
   * the panel itself.
   *
   * @param {Element} target - The event target
   */
  #handlePopupHiddenEvent(target) {
    const { panel } = this.elements;
    switch (target.id) {
      case panel.id: {
        TranslationsParent.telemetry().selectTranslationsPanel().onClose();
        this.#changeStateToClosed();
        this.#removeActiveTranslationListeners();
        break;
      }
    }
  }

  /**
   * Handles events in the SelectTranslationsPanel.
   *
   * @param {Event} event - The event to handle.
   */
  handleEvent(event) {
    let target = event.target;

    // If a menuitem within a menulist is the target, those don't have ids,
    // so we want to traverse until we get to a parent element with an id.
    while (!target.id && target.parentElement) {
      target = target.parentElement;
    }

    switch (event.type) {
      case "command": {
        this.#handleCommandEvent(target);
        break;
      }
      case "keypress": {
        if (event.key === "Enter") {
          this.#handleEnterKeyPressed(target);
        }
        break;
      }
      case "popupshown": {
        this.#handlePopupShownEvent(target);
        break;
      }
      case "popuphidden": {
        this.#handlePopupHiddenEvent(target);
        break;
      }
    }
  }

  /**
   * Handles events when the panels select from-language is changed.
   */
  onChangeFromLanguage() {
    this.#sourceTextWordCount = undefined;
    this.#updateConditionalUIEnabledState();
  }

  /**
   * Handles events when the panels select to-language is changed.
   */
  onChangeToLanguage() {
    this.#updateConditionalUIEnabledState();
  }

  /**
   * Handles events when the panel's try-another-source language is changed.
   */
  onChangeTryAnotherSourceLanguage() {
    const { tryAnotherSourceMenuList, translateButton } = this.elements;
    if (tryAnotherSourceMenuList.value) {
      translateButton.disabled = false;
    }
  }

  /**
   * Handles events when the panel's cancel button is clicked.
   */
  onClickCancelButton() {
    TranslationsParent.telemetry().selectTranslationsPanel().onCancelButton();
    this.close();
  }

  /**
   * Handles events when the panel's copy button is clicked.
   */
  onClickCopyButton() {
    TranslationsParent.telemetry().selectTranslationsPanel().onCopyButton();

    try {
      ClipboardHelper.copyString(this.getTranslatedText());
    } catch (error) {
      this.console?.error(error);
      return;
    }

    this.#checkCopyButton();
  }

  /**
   * Handles events when the panel's done button is clicked.
   */
  onClickDoneButton() {
    TranslationsParent.telemetry().selectTranslationsPanel().onDoneButton();
    this.close();
  }

  /**
   * Handles events when the panel's translate button is clicked.
   */
  onClickTranslateButton() {
    const { fromMenuList, tryAnotherSourceMenuList } = this.elements;
    const { detectedLanguage, targetLanguage } = this.#translationState;

    fromMenuList.value = tryAnotherSourceMenuList.value;

    TranslationsParent.telemetry().selectTranslationsPanel().onTranslateButton({
      detectedLanguage,
      sourceLanguage: fromMenuList.value,
      targetLanguage,
    });

    this.#maybeRequestTranslation();
  }

  /**
   * Handles events when the panel's translate-full-page button is clicked.
   */
  onClickTranslateFullPageButton() {
    TranslationsParent.telemetry()
      .selectTranslationsPanel()
      .onTranslateFullPageButton();

    const { panel } = this.elements;
    const languagePair = this.#getSelectedLanguagePair();

    try {
      const actor = TranslationsParent.getTranslationsActor(
        gBrowser.selectedBrowser
      );
      panel.addEventListener(
        "popuphidden",
        () =>
          actor.translate(
            languagePair,
            false // reportAsAutoTranslate
          ),
        { once: true }
      );
    } catch (error) {
      // This situation would only occur if the translate-full-page button as invoked
      // while Translations actor is not available. the logic within this class explicitly
      // hides the button in this case, and this should not be possible under normal conditions,
      // but if this button were to somehow still be invoked, the best thing we can do here is log
      // an error to the console because the FullPageTranslationsPanel assumes that the actor is available.
      this.console?.error(error);
    }

    this.close();
  }

  /**
   * Handles events when the panel's try-again button is clicked.
   */
  onClickTryAgainButton() {
    TranslationsParent.telemetry().selectTranslationsPanel().onTryAgainButton();

    switch (this.phase()) {
      case "translation-failure": {
        // If the translation failed, we just need to try translating again.
        this.#maybeRequestTranslation();
        break;
      }
      case "init-failure": {
        // If the initialization failed, we need to close the panel and try reopening it
        // which will attempt to initialize everything again after failure.
        const { panel } = this.elements;
        const {
          event,
          screenX,
          screenY,
          sourceText,
          isTextSelected,
          langPairPromise,
        } = this.#translationState;

        panel.addEventListener(
          "popuphidden",
          () =>
            this.open(
              event,
              screenX,
              screenY,
              sourceText,
              isTextSelected,
              langPairPromise,
              /* maintainFlow */ true
            ),
          { once: true }
        );

        this.close();
        break;
      }
      default: {
        this.console?.error(
          `Unexpected state "${this.phase()}" on try-again button click.`
        );
      }
    }
  }

  /**
   * Changes the copy button's visual icon to checked, and its localized text to "Copied".
   */
  #checkCopyButton() {
    const { copyButton } = this.elements;
    copyButton.classList.add("copied");
    document.l10n.setAttributes(
      copyButton,
      "select-translations-panel-copy-button-copied"
    );
  }

  /**
   * Changes the copy button's visual icon to unchecked, and its localized text to "Copy".
   */
  #uncheckCopyButton() {
    const { copyButton } = this.elements;
    copyButton.classList.remove("copied");
    document.l10n.setAttributes(
      copyButton,
      "select-translations-panel-copy-button"
    );
  }

  /**
   * Clears the selected language and ensures that the menu list displays
   * the proper placeholder text.
   *
   * @param {Element} menuList - The target menu list element to update.
   */
  async #deselectLanguage(menuList) {
    menuList.value = "";
    document.l10n.setAttributes(menuList, "translations-panel-choose-language");
    await document.l10n.translateElements([menuList]);
  }

  /**
   * Focuses on the given menu list if provided and empty, or defaults to focusing one
   * of the from-menu or to-menu lists if either is empty.
   *
   * @param {Element} [menuList] - The menu list to focus if specified.
   */
  #maybeFocusMenuList(menuList) {
    if (menuList && !menuList.value) {
      menuList.focus({ focusVisible: false });
      return;
    }

    const { fromMenuList, toMenuList } = this.elements;
    if (!fromMenuList.value) {
      fromMenuList.focus({ focusVisible: false });
    } else if (!toMenuList.value) {
      toMenuList.focus({ focusVisible: false });
    }
  }

  /**
   * Focuses the translated-text area and sets its overflow to auto post-animation.
   */
  #indicateTranslatedTextArea({ overflow }) {
    const { textArea } = this.elements;
    textArea.focus({ focusVisible: true });
    requestAnimationFrame(() => {
      // We want to set overflow to auto as the final animation, because if it is
      // set before the translated text is displayed, then the scrollTop will
      // move to the bottom as the text is populated.
      //
      // Setting scrollTop = 0 on its own works, but it sometimes causes an animation
      // of the text jumping from the bottom to the top. It looks a lot cleaner to
      // disable overflow before rendering the text, then re-enable it after it renders.
      requestAnimationFrame(() => {
        textArea.style.overflow = overflow;
        textArea.setSelectionRange(0, 0);
        textArea.scrollTop = 0;
      });
    });
  }

  /**
   * Checks if the given language pair matches the panel's currently selected language pair.
   *
   * @param {string} sourceLanguage - The from-language to compare.
   * @param {string} targetLanguage - The to-language to compare.
   *
   * @returns {boolean} - True if the given language pair matches the selected languages in the panel UI, otherwise false.
   */
  #isSelectedLangPair(sourceLanguage, targetLanguage) {
    const selected = this.#getSelectedLanguagePair();
    return (
      TranslationsUtils.langTagsMatch(
        sourceLanguage,
        selected.sourceLanguage
      ) &&
      TranslationsUtils.langTagsMatch(targetLanguage, selected.targetLanguage)
    );
  }

  /**
   * Retrieves the currently selected language pair from the menu lists.
   *
   * @returns {LanguagePair}
   */
  #getSelectedLanguagePair() {
    const { fromMenuList, toMenuList } = this.elements;
    const [sourceLanguage, sourceVariant] = fromMenuList.value.split(",");
    const [targetLanguage, targetVariant] = toMenuList.value.split(",");
    return {
      sourceLanguage,
      targetLanguage,
      sourceVariant,
      targetVariant,
    };
  }

  /**
   * Retrieves the source text from the translation state.
   * This value is not available when the panel is closed.
   *
   * @returns {string | undefined} The source text.
   */
  getSourceText() {
    return this.#translationState?.sourceText;
  }

  /**
   * Retrieves the source text from the translation state.
   * This value is only available in the translated phase.
   *
   * @returns {string | undefined} The translated text.
   */
  getTranslatedText() {
    return this.#translationState?.translatedText;
  }

  /**
   * Retrieves the current phase of the translation state.
   *
   * @returns {string}
   */
  phase() {
    return this.#translationState.phase;
  }

  /**
   * @returns {boolean} True if the panel is open, otherwise false.
   */
  #isOpen() {
    return this.phase() !== "closed";
  }

  /**
   * @returns {boolean} True if the panel is closed, otherwise false.
   */
  #isClosed() {
    return this.phase() === "closed";
  }

  /**
   * Changes the translation state to a new phase with options to retain or overwrite existing entries.
   *
   * @param {SelectTranslationsPanelState} phase - The new phase to transition to.
   * @param {boolean} [retainEntries] - Whether to retain existing state entries that are not overwritten.
   * @param {object | null} [data=null] - Additional data to merge into the state.
   * @throws {Error} If an invalid phase is specified.
   */
  #changeStateTo(phase, retainEntries, data = null) {
    switch (phase) {
      case "closed":
      case "idle":
      case "init-failure":
      case "translation-failure":
      case "translatable":
      case "translating":
      case "translated":
      case "unsupported": {
        // Phase is valid, continue on.
        break;
      }
      default: {
        throw new Error(`Invalid state change to '${phase}'`);
      }
    }

    const previousPhase = this.phase();
    if (data && retainEntries) {
      // Change the phase and apply new entries from data, but retain non-overwritten entries from previous state.
      this.#translationState = { ...this.#translationState, phase, ...data };
    } else if (data) {
      // Change the phase and apply new entries from data, but drop any entries that are not overwritten by data.
      this.#translationState = { phase, ...data };
    } else if (retainEntries) {
      // Change only the phase and retain all entries from previous data.
      this.#translationState.phase = phase;
    } else {
      // Change the phase and delete all entries from previous data.
      this.#translationState = { phase };
    }

    if (previousPhase === this.phase()) {
      // Do not continue on to update the UI because the phase didn't change.
      return;
    }

    const { sourceLanguage, targetLanguage, detectedLanguage } =
      this.#translationState;
    this.console?.debug(
      `SelectTranslationsPanel (${sourceLanguage ?? detectedLanguage ?? "??"}-${
        targetLanguage ? targetLanguage : "??"
      }) state change (${previousPhase} => ${phase})`
    );

    this.#updatePanelUIFromState();
    document.dispatchEvent(
      new CustomEvent("SelectTranslationsPanelStateChanged", {
        detail: { phase },
      })
    );
  }

  /**
   * Changes the phase to closed, discarding any entries in the translation state.
   */
  #changeStateToClosed() {
    this.#changeStateTo("closed", /* retainEntries */ false);
  }

  /**
   * Changes the phase from "translatable" to "translating".
   *
   * @throws {Error} If the current state is not "translatable".
   */
  #changeStateToTranslating() {
    const phase = this.phase();
    if (phase !== "translatable") {
      throw new Error(`Invalid state change (${phase} => translating)`);
    }
    this.#changeStateTo("translating", /* retainEntries */ true);
  }

  /**
   * Changes the phase from "translating" to "translated".
   *
   * @throws {Error} If the current state is not "translating".
   */
  #changeStateToTranslated(translatedText) {
    const phase = this.phase();
    if (phase !== "translating") {
      throw new Error(`Invalid state change (${phase} => translated)`);
    }
    this.#changeStateTo("translated", /* retainEntries */ true, {
      translatedText,
    });
  }

  /**
   * Changes the phase to "init-failure".
   *
   * @param {Event} event - The triggering event for opening the panel.
   * @param {number} screenX - The x-axis location of the screen at which to open the popup.
   * @param {number} screenY - The y-axis location of the screen at which to open the popup.
   * @param {string} sourceText - The text to translate.
   * @param {boolean} isTextSelected - True if the text comes from a hyperlink, false if it is from a selection.
   * @param {Promise} langPairPromise - Promise resolving to language pair data for initializing dropdowns.
   */
  #changeStateToInitFailure(
    event,
    screenX,
    screenY,
    sourceText,
    isTextSelected,
    langPairPromise
  ) {
    this.#changeStateTo("init-failure", /* retainEntries */ true, {
      event,
      screenX,
      screenY,
      sourceText,
      isTextSelected,
      langPairPromise,
    });
  }

  /**
   * Changes the phase from "translating" to "translation-failure".
   */
  #changeStateToTranslationFailure() {
    const phase = this.phase();
    if (phase !== "translating") {
      this.console?.error(
        `Invalid state change (${phase} => translation-failure)`
      );
    }
    this.#changeStateTo("translation-failure", /* retainEntries */ true);
  }

  /**
   * Transitions the phase to "translatable" if the proper conditions are met,
   * otherwise retains the same phase as before.
   *
   * @param {string} sourceLanguage - The BCP-47 from-language tag.
   * @param {string} targetLanguage - The BCP-47 to-language tag.
   */
  #maybeChangeStateToTranslatable(sourceLanguage, targetLanguage) {
    const previous = this.#translationState;

    const langSelectionChanged = () =>
      !TranslationsUtils.langTagsMatch(
        previous.sourceLanguage,
        sourceLanguage
      ) ||
      !TranslationsUtils.langTagsMatch(previous.targetLanguage, targetLanguage);

    const shouldTranslateEvenIfLangSelectionHasNotChanged = () => {
      const phase = this.phase();
      return (
        // The panel has just opened, and this is the initial translation.
        phase === "idle" ||
        // The previous translation failed and we are about to try again.
        phase === "translation-failure"
      );
    };

    if (
      // A valid source language is actively selected.
      sourceLanguage &&
      // A valid target language is actively selected.
      targetLanguage &&
      // The language selection has changed, requiring a new translation.
      (langSelectionChanged() ||
        // We should try to translate even if the language selection has not changed.
        shouldTranslateEvenIfLangSelectionHasNotChanged())
    ) {
      this.#changeStateTo("translatable", /* retainEntries */ true, {
        sourceLanguage,
        targetLanguage,
      });
    }
  }

  /**
   * Handles changes to the copy button based on the current translation state.
   *
   * @param {string} phase - The current phase of the translation state.
   */
  #handleCopyButtonChanges(phase) {
    switch (phase) {
      case "closed":
      case "translation-failure":
      case "translated": {
        this.#uncheckCopyButton();
        break;
      }
      case "idle":
      case "init-failure":
      case "translatable":
      case "translating":
      case "unsupported": {
        // Do nothing.
        break;
      }
      default: {
        throw new Error(`Invalid state change to '${phase}'`);
      }
    }
  }

  /**
   * Handles changes to the text area's background image based on the current translation state.
   *
   * @param {string} phase - The current phase of the translation state.
   */
  #handleTextAreaBackgroundChanges(phase) {
    const { textArea } = this.elements;
    switch (phase) {
      case "translating": {
        textArea.classList.add("translating");
        break;
      }
      case "closed":
      case "idle":
      case "init-failure":
      case "translation-failure":
      case "translatable":
      case "translated":
      case "unsupported": {
        textArea.classList.remove("translating");
        break;
      }
      default: {
        throw new Error(`Invalid state change to '${phase}'`);
      }
    }
  }

  /**
   * Handles changes to the primary UI components based on the current translation state.
   *
   * @param {string} phase  - The current phase of the translation state.
   */
  #handlePrimaryUIChanges(phase) {
    switch (phase) {
      case "closed":
      case "idle": {
        this.#displayIdlePlaceholder();
        break;
      }
      case "init-failure": {
        this.#displayInitFailureMessage();
        break;
      }
      case "translation-failure": {
        this.#displayTranslationFailureMessage();
        break;
      }
      case "translatable": {
        // Do nothing.
        break;
      }
      case "translating": {
        this.#displayTranslatingPlaceholder();
        break;
      }
      case "translated": {
        this.#displayTranslatedText();
        break;
      }
      case "unsupported": {
        this.#displayUnsupportedLanguageMessage();
        break;
      }
      default: {
        throw new Error(`Invalid state change to '${phase}'`);
      }
    }
  }

  /**
   * Returns true if the translate-full-page button should be hidden in the current panel view.
   *
   * @returns {boolean}
   */
  #shouldHideTranslateFullPageButton() {
    return (
      // Do not offer to translate the full page if it is restricted on this page.
      this.#isFullPageTranslationsRestrictedForPage ||
      // Do not offer to translate the full page if Full-Page Translations is already active.
      this.#activeFullPageTranslationsTargetLanguage
    );
  }

  /**
   * Determines whether translation should continue based on panel state and language pair.
   *
   * @param {number} translationId - The id of the translation request to match.
   * @param {string} sourceLanguage - The source language to analyze.
   * @param {string} targetLanguage - The target language to analyze.
   *
   * @returns {boolean} True if translation should continue with the given pair, otherwise false.
   */
  #shouldContinueTranslation(translationId, sourceLanguage, targetLanguage) {
    return (
      // Continue only if the panel is still open.
      this.#isOpen() &&
      // Continue only if the current translationId matches.
      translationId === this.#translationId &&
      // Continue only if the given language pair is still the actively selected pair.
      this.#isSelectedLangPair(sourceLanguage, targetLanguage)
    );
  }

  /**
   * Displays the placeholder text for the translation state's "idle" phase.
   */
  #displayIdlePlaceholder() {
    this.#showMainContent();

    const { textArea } = SelectTranslationsPanel.elements;
    textArea.value = this.#idlePlaceholderText;
    this.#updateTextDirection();
    this.#updateConditionalUIEnabledState();
    this.#maybeFocusMenuList();
  }

  /**
   * Displays the placeholder text for the translation state's "translating" phase.
   */
  #displayTranslatingPlaceholder() {
    this.#showMainContent();

    const { textArea } = SelectTranslationsPanel.elements;
    textArea.value = this.#translatingPlaceholderText;
    this.#updateTextDirection();
    this.#updateConditionalUIEnabledState();
    this.#indicateTranslatedTextArea({ overflow: "hidden" });
  }

  /**
   * Displays the translated text for the translation state's "translated" phase.
   */
  #displayTranslatedText() {
    this.#showMainContent();

    const { targetLanguage } = this.#getSelectedLanguagePair();
    const { textArea } = SelectTranslationsPanel.elements;
    textArea.value = this.getTranslatedText();
    this.#updateTextDirection(targetLanguage);
    this.#updateConditionalUIEnabledState();
    this.#indicateTranslatedTextArea({ overflow: "auto" });
    this.#maybeEnableTextAreaResizer();

    const window =
      gBrowser.selectedBrowser.browsingContext.top.embedderElement
        .documentGlobal;
    window.A11yUtils.announce({
      id: "select-translations-panel-translation-complete-announcement",
    });
  }

  /**
   * Sets attributes on panel elements that are specifically relevant
   * to the SelectTranslationsPanel's state.
   *
   * @param {object} options - Options of which attributes to set.
   * @param {Record<string, Element[]>} options.makeHidden - Make these elements hidden.
   * @param {Record<string, Element[]>} options.makeVisible - Make these elements visible.
   */
  #setPanelElementAttributes({ makeHidden = [], makeVisible = [] }) {
    for (const element of makeHidden) {
      element.hidden = true;
    }
    for (const element of makeVisible) {
      element.hidden = false;
    }
  }

  /**
   * Enables or disables UI components that are conditional on a valid language pair being selected.
   */
  #updateConditionalUIEnabledState() {
    const { sourceLanguage, targetLanguage } = this.#getSelectedLanguagePair();
    const {
      copyButton,
      textArea,
      translateButton,
      translateFullPageButton,
      tryAnotherSourceMenuList,
    } = this.elements;

    const invalidLangPairSelected = !sourceLanguage || !targetLanguage;
    const isTranslating = this.phase() === "translating";

    textArea.disabled = invalidLangPairSelected;
    copyButton.disabled = invalidLangPairSelected || isTranslating;
    translateButton.disabled = !tryAnotherSourceMenuList.value;
    translateFullPageButton.disabled =
      invalidLangPairSelected ||
      TranslationsUtils.langTagsMatch(sourceLanguage, targetLanguage) ||
      this.#shouldHideTranslateFullPageButton();
  }

  /**
   * Updates the panel UI based on the current phase of the translation state.
   */
  #updatePanelUIFromState() {
    const phase = this.phase();

    this.#handlePrimaryUIChanges(phase);
    this.#handleCopyButtonChanges(phase);
    this.#handleTextAreaBackgroundChanges(phase);

    this.#mostRecentUIPhase = phase;
  }

  /**
   * Shows the panel's main-content group of elements.
   */
  #showMainContent() {
    const {
      cancelButton,
      copyButton,
      doneButtonPrimary,
      doneButtonSecondary,
      initFailureContent,
      mainContent,
      unsupportedLanguageContent,
      textArea,
      translateButton,
      translateFullPageButton,
      translationFailureMessageBar,
      tryAgainButton,
    } = this.elements;
    this.#setPanelElementAttributes({
      makeHidden: [
        cancelButton,
        doneButtonSecondary,
        initFailureContent,
        translateButton,
        translationFailureMessageBar,
        tryAgainButton,
        unsupportedLanguageContent,
        ...(this.#shouldHideTranslateFullPageButton()
          ? [translateFullPageButton]
          : []),
      ],
      makeVisible: [
        mainContent,
        copyButton,
        doneButtonPrimary,
        textArea,
        ...(this.#shouldHideTranslateFullPageButton()
          ? []
          : [translateFullPageButton]),
      ],
    });
  }

  /**
   * Shows the panel's unsupported-language group of elements.
   */
  #showUnsupportedLanguageContent() {
    const {
      cancelButton,
      copyButton,
      doneButtonPrimary,
      doneButtonSecondary,
      initFailureContent,
      mainContent,
      unsupportedLanguageContent,
      translateButton,
      translateFullPageButton,
      tryAgainButton,
    } = this.elements;
    this.#setPanelElementAttributes({
      makeHidden: [
        cancelButton,
        doneButtonPrimary,
        copyButton,
        initFailureContent,
        mainContent,
        translateFullPageButton,
        tryAgainButton,
      ],
      makeVisible: [
        doneButtonSecondary,
        translateButton,
        unsupportedLanguageContent,
      ],
    });
  }

  /**
   * Displays the panel content for when the language dropdowns fail to populate.
   */
  #displayInitFailureMessage() {
    if (this.#mostRecentUIPhase !== "init-failure") {
      TranslationsParent.telemetry()
        .selectTranslationsPanel()
        .onInitializationFailureMessage();
    }

    const {
      cancelButton,
      copyButton,
      doneButtonPrimary,
      doneButtonSecondary,
      initFailureContent,
      mainContent,
      unsupportedLanguageContent,
      translateButton,
      translateFullPageButton,
      tryAgainButton,
    } = this.elements;
    this.#setPanelElementAttributes({
      makeHidden: [
        doneButtonPrimary,
        doneButtonSecondary,
        copyButton,
        mainContent,
        translateButton,
        translateFullPageButton,
        unsupportedLanguageContent,
      ],
      makeVisible: [initFailureContent, cancelButton, tryAgainButton],
    });
    tryAgainButton.setAttribute(
      "aria-describedby",
      "select-translations-panel-init-failure-message-bar"
    );
    tryAgainButton.focus({ focusVisible: false });
  }

  /**
   * Displays the panel content for when a translation fails to complete.
   */
  #displayTranslationFailureMessage() {
    if (this.#mostRecentUIPhase !== "translation-failure") {
      const { sourceLanguage, targetLanguage } =
        this.#getSelectedLanguagePair();
      TranslationsParent.telemetry()
        .selectTranslationsPanel()
        .onTranslationFailureMessage({ sourceLanguage, targetLanguage });
    }

    const {
      cancelButton,
      copyButton,
      doneButtonPrimary,
      doneButtonSecondary,
      initFailureContent,
      mainContent,
      textArea,
      translateButton,
      translateFullPageButton,
      translationFailureMessageBar,
      tryAgainButton,
      unsupportedLanguageContent,
    } = this.elements;
    this.#setPanelElementAttributes({
      makeHidden: [
        doneButtonPrimary,
        doneButtonSecondary,
        copyButton,
        initFailureContent,
        translateButton,
        translateFullPageButton,
        textArea,
        unsupportedLanguageContent,
      ],
      makeVisible: [
        cancelButton,
        mainContent,
        translationFailureMessageBar,
        tryAgainButton,
      ],
    });
    tryAgainButton.setAttribute(
      "aria-describedby",
      "select-translations-panel-translation-failure-message-bar"
    );
    tryAgainButton.focus({ focusVisible: false });
  }

  /**
   * Displays the panel's unsupported language message bar, showing
   * the panel's unsupported-language elements.
   */
  #displayUnsupportedLanguageMessage() {
    const { detectedLanguage } = this.#translationState;

    if (this.#mostRecentUIPhase !== "unsupported") {
      const { docLangTag } = this.#getLanguageInfo();
      TranslationsParent.telemetry()
        .selectTranslationsPanel()
        .onUnsupportedLanguageMessage({ docLangTag, detectedLanguage });
    }

    const { unsupportedLanguageMessageBar, tryAnotherSourceMenuList } =
      this.elements;
    const languageDisplayNames =
      TranslationsParent.createLanguageDisplayNames();
    try {
      const language = languageDisplayNames.of(detectedLanguage);
      if (language) {
        document.l10n.setAttributes(
          unsupportedLanguageMessageBar,
          "select-translations-panel-unsupported-language-message-known-2",
          { language }
        );
      } else {
        // Will be immediately caught.
        throw new Error();
      }
    } catch {
      // Either displayNames.of() threw, or we threw due to no display name found.
      // In either case, localize the message for an unknown language.
      document.l10n.setAttributes(
        unsupportedLanguageMessageBar,
        "select-translations-panel-unsupported-language-message-unknown-2"
      );
    }
    this.#updateConditionalUIEnabledState();
    this.#showUnsupportedLanguageContent();
    this.#maybeFocusMenuList(tryAnotherSourceMenuList);
  }

  /**
   * Sets the text direction attribute in the text areas based on the specified language.
   * Uses the given language tag if provided, otherwise uses the current app locale.
   *
   * @param {string} [langTag] - The language tag to determine text direction.
   */
  #updateTextDirection(langTag) {
    const { textArea } = this.elements;
    if (langTag) {
      const scriptDirection = Services.intl.getScriptDirection(langTag);
      textArea.setAttribute("dir", scriptDirection);
    } else {
      textArea.removeAttribute("dir");
    }
  }

  /**
   * Requests a translations port for a given language pair.
   *
   * @param {LanguagePair} languagePair
   * @returns {Promise<MessagePort | undefined>} The message port promise.
   */
  async #requestTranslationsPort(languagePair) {
    return TranslationsParent.requestTranslationsPort(languagePair);
  }

  /**
   * Creates a translator for a single Select Translations request.
   *
   * @param {LanguagePair} languagePair
   *
   * @returns {Promise<Translator>} A promise that resolves to a `Translator` instance for the given language pair.
   */
  async #createTranslator(languagePair) {
    this.console?.log(
      `Creating new Translator (${TranslationsUtils.serializeLanguagePair(languagePair)})`
    );

    const translator = await Translator.create({
      languagePair,
      requestTranslationsPort: this.#requestTranslationsPort,
      allowSameLanguage: true,
      activeRequestCapacity: 1,
    });

    return translator;
  }

  /**
   * Initiates the translation process if the panel state and selected languages
   * meet the conditions for translation.
   */
  #maybeRequestTranslation() {
    if (this.#isClosed()) {
      return;
    }

    const languagePair = this.#getSelectedLanguagePair();
    const { sourceLanguage, targetLanguage } = languagePair;
    this.#maybeChangeStateToTranslatable(sourceLanguage, targetLanguage);

    if (this.phase() !== "translatable") {
      return;
    }

    const { docLangTag } = this.#getLanguageInfo();
    const sourceText = this.getSourceText();
    const translationId = ++this.#translationId;

    TranslationsParent.storeMostRecentTargetLanguage(targetLanguage);

    this.#createTranslator(languagePair)
      .then(async translator => {
        try {
          if (
            this.#shouldContinueTranslation(
              translationId,
              sourceLanguage,
              targetLanguage
            )
          ) {
            this.#changeStateToTranslating();
            return await translator.translate(this.getSourceText());
          }

          return null;
        } finally {
          translator.destroy();
        }
      })
      .then(translatedText => {
        if (
          translatedText &&
          this.#shouldContinueTranslation(
            translationId,
            sourceLanguage,
            targetLanguage
          )
        ) {
          this.#changeStateToTranslated(translatedText);
        }
      })
      .catch(error => {
        this.console?.error(error);
        this.#changeStateToTranslationFailure();
      });

    try {
      if (!this.#sourceTextWordCount) {
        this.#sourceTextWordCount = TranslationsParent.countWords(
          sourceLanguage,
          sourceText
        );
      }
    } catch (error) {
      // Failed to create an Intl.Segmenter for the sourceLanguage.
      // Continue on to report undefined to telemetry.
      this.console?.warn(error);
    }

    TranslationsParent.telemetry().onTranslate({
      docLangTag,
      sourceLanguage,
      targetLanguage,
      autoTranslate: false,
      requestTarget: "select",
      sourceTextCodeUnits: sourceText.length,
      sourceTextWordCount: this.#sourceTextWordCount,
    });
  }

  /**
   * Reports to telemetry whether the source language or the target language has
   * changed based on whether the currently selected language is different
   * than the corresponding language that is stored in the panel's state.
   */
  #maybeReportLanguageChangeToTelemetry() {
    const previous = this.#translationState;
    const selected = this.#getSelectedLanguagePair();

    if (
      !TranslationsUtils.langTagsMatch(
        selected.sourceLanguage,
        previous.sourceLanguage
      )
    ) {
      const { docLangTag } = this.#getLanguageInfo();
      TranslationsParent.telemetry()
        .selectTranslationsPanel()
        .onChangeFromLanguage({
          previousLangTag: previous.sourceLanguage,
          currentLangTag: selected.sourceLanguage,
          docLangTag,
        });
    }
    if (
      !TranslationsUtils.langTagsMatch(
        selected.targetLanguage,
        previous.targetLanguage
      )
    ) {
      TranslationsParent.telemetry()
        .selectTranslationsPanel()
        .onChangeToLanguage(selected.targetLanguage);
    }
  }

  /**
   * Attaches event listeners to the target element for initiating translation on specified event types.
   *
   * @param {string[]} eventTypes - An array of event types to listen for.
   * @param {object} target - The target element to attach event listeners to.
   * @throws {Error} If an unrecognized event type is provided.
   */
  #maybeTranslateOnEvents(eventTypes, target) {
    if (!target.translationListenerCallbacks) {
      target.translationListenerCallbacks = [];
    }
    if (target.translationListenerCallbacks.length === 0) {
      for (const eventType of eventTypes) {
        let callback;
        switch (eventType) {
          case "focus":
          case "popuphidden": {
            callback = () => {
              this.#maybeReportLanguageChangeToTelemetry();
              this.#maybeRequestTranslation();
            };
            break;
          }
          case "keypress": {
            callback = event => {
              if (event.key === "Enter") {
                this.#maybeReportLanguageChangeToTelemetry();
                this.#maybeRequestTranslation();
              }
            };
            break;
          }
          default: {
            throw new Error(
              `Invalid translation event type given: '${eventType}`
            );
          }
        }
        target.addEventListener(eventType, callback);
        target.translationListenerCallbacks.push({ eventType, callback });
      }
    }
  }

  /**
   * Removes all translation event listeners from any panel elements that would have one.
   */
  #removeActiveTranslationListeners() {
    const { fromMenuList, fromMenuPopup, textArea, toMenuList, toMenuPopup } =
      SelectTranslationsPanel.elements;
    this.#removeTranslationListenersFrom(fromMenuList);
    this.#removeTranslationListenersFrom(fromMenuPopup);
    this.#removeTranslationListenersFrom(textArea);
    this.#removeTranslationListenersFrom(toMenuList);
    this.#removeTranslationListenersFrom(toMenuPopup);
  }

  /**
   * Removes all translation event listeners from the target element.
   *
   * @param {Element} target - The element from which event listeners are to be removed.
   */
  #removeTranslationListenersFrom(target) {
    if (!target.translationListenerCallbacks) {
      return;
    }

    for (const { eventType, callback } of target.translationListenerCallbacks) {
      target.removeEventListener(eventType, callback);
    }

    target.translationListenerCallbacks = [];
  }
})();
