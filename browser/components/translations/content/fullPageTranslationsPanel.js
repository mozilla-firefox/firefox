/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable jsdoc/valid-types */
/**
 * @typedef {import("../../../../toolkit/components/translations/translations").LangTags} LangTags
 */
/* eslint-enable jsdoc/valid-types */

ChromeUtils.defineESModuleGetters(this, {
  PageActions: "resource:///modules/PageActions.sys.mjs",
  TranslationsFeature:
    "chrome://global/content/translations/TranslationsFeature.sys.mjs",
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  TranslationsPanelShared:
    "chrome://browser/content/translations/TranslationsPanelShared.sys.mjs",
});

/**
 * The set of actions that can occur from interaction with the
 * translations panel.
 */
const PageAction = Object.freeze({
  NO_CHANGE: "NO_CHANGE",
  RESTORE_PAGE: "RESTORE_PAGE",
  TRANSLATE_PAGE: "TRANSLATE_PAGE",
  CLOSE_PANEL: "CLOSE_PANEL",
});

/**
 * A mechanism for determining the next relevant page action
 * based on the current translated state of the page and the state
 * of the persistent options in the translations panel settings.
 */
class CheckboxPageAction {
  /**
   * Whether or not translations is active on the page.
   *
   * @type {boolean}
   */
  #translationsActive = false;

  /**
   * Whether the always-translate-language menuitem is checked
   * in the translations panel settings menu.
   *
   * @type {boolean}
   */
  #alwaysTranslateLanguage = false;

  /**
   * Whether the never-translate-language menuitem is checked
   * in the translations panel settings menu.
   *
   * @type {boolean}
   */
  #neverTranslateLanguage = false;

  /**
   * Whether the never-translate-site menuitem is checked
   * in the translations panel settings menu.
   *
   * @type {boolean}
   */
  #neverTranslateSite = false;

  /**
   * @param {boolean} translationsActive
   * @param {boolean} alwaysTranslateLanguage
   * @param {boolean} neverTranslateLanguage
   * @param {boolean} neverTranslateSite
   */
  constructor(
    translationsActive,
    alwaysTranslateLanguage,
    neverTranslateLanguage,
    neverTranslateSite
  ) {
    this.#translationsActive = translationsActive;
    this.#alwaysTranslateLanguage = alwaysTranslateLanguage;
    this.#neverTranslateLanguage = neverTranslateLanguage;
    this.#neverTranslateSite = neverTranslateSite;
  }

  /**
   * Accepts four integers that are either 0 or 1 and returns
   * a single, unique number for each possible combination of
   * values.
   *
   * @param {number} translationsActive
   * @param {number} alwaysTranslateLanguage
   * @param {number} neverTranslateLanguage
   * @param {number} neverTranslateSite
   *
   * @returns {number} - An integer representation of the state
   */
  static #computeState(
    translationsActive,
    alwaysTranslateLanguage,
    neverTranslateLanguage,
    neverTranslateSite
  ) {
    return (
      (translationsActive << 3) |
      (alwaysTranslateLanguage << 2) |
      (neverTranslateLanguage << 1) |
      neverTranslateSite
    );
  }

  /**
   * Returns the current state of the data members as a single number.
   *
   * @returns {number} - An integer representation of the state
   */
  #state() {
    return CheckboxPageAction.#computeState(
      Number(this.#translationsActive),
      Number(this.#alwaysTranslateLanguage),
      Number(this.#neverTranslateLanguage),
      Number(this.#neverTranslateSite)
    );
  }

  /**
   * Returns the next page action to take when the always-translate-language
   * menuitem is toggled in the translations panel settings menu.
   *
   * @returns {PageAction}
   */
  alwaysTranslateLanguage() {
    switch (this.#state()) {
      case CheckboxPageAction.#computeState(1, 1, 0, 1):
      case CheckboxPageAction.#computeState(1, 1, 0, 0):
        return PageAction.RESTORE_PAGE;
      case CheckboxPageAction.#computeState(0, 0, 1, 0):
      case CheckboxPageAction.#computeState(0, 0, 0, 0):
        return PageAction.TRANSLATE_PAGE;
    }
    return PageAction.NO_CHANGE;
  }

  /**
   * Returns the next page action to take when the never-translate-language
   * menuitem is toggled in the translations panel settings menu.
   *
   * @returns {PageAction}
   */
  neverTranslateLanguage() {
    switch (this.#state()) {
      case CheckboxPageAction.#computeState(1, 1, 0, 1):
      case CheckboxPageAction.#computeState(1, 1, 0, 0):
      case CheckboxPageAction.#computeState(1, 0, 0, 1):
      case CheckboxPageAction.#computeState(1, 0, 0, 0):
        return PageAction.RESTORE_PAGE;
      case CheckboxPageAction.#computeState(0, 1, 0, 0):
      case CheckboxPageAction.#computeState(0, 0, 0, 1):
      case CheckboxPageAction.#computeState(0, 1, 0, 1):
      case CheckboxPageAction.#computeState(0, 0, 0, 0):
        return PageAction.CLOSE_PANEL;
    }
    return PageAction.NO_CHANGE;
  }

  /**
   * Returns the next page action to take when the never-translate-site
   * menuitem is toggled in the translations panel settings menu.
   *
   * @returns {PageAction}
   */
  neverTranslateSite() {
    switch (this.#state()) {
      case CheckboxPageAction.#computeState(1, 1, 0, 0):
      case CheckboxPageAction.#computeState(1, 0, 1, 0):
      case CheckboxPageAction.#computeState(1, 0, 0, 0):
        return PageAction.RESTORE_PAGE;
      case CheckboxPageAction.#computeState(0, 1, 0, 1):
        return PageAction.TRANSLATE_PAGE;
      case CheckboxPageAction.#computeState(0, 0, 1, 0):
      case CheckboxPageAction.#computeState(0, 1, 0, 0):
      case CheckboxPageAction.#computeState(0, 0, 0, 0):
        return PageAction.CLOSE_PANEL;
    }
    return PageAction.NO_CHANGE;
  }
}

/**
 * This singleton class controls the FullPageTranslations panel.
 *
 * This component is a `/browser` component, and the actor is a `/toolkit` actor, so care
 * must be taken to keep the presentation (this component) from the state management
 * (the Translations actor). This class reacts to state changes coming from the
 * Translations actor.
 *
 * A global instance of this class is created once per top ChromeWindow and is initialized
 * when the new window is created.
 *
 * See the comment above TranslationsParent for more details.
 *
 * @see TranslationsParent
 */
var FullPageTranslationsPanel = new (class {
  /** @type {Console?} */
  #console;

  /**
   * The cached detected languages for both the document and the user.
   *
   * @type {null | LangTags}
   */
  detectedLanguages = null;

  /**
   * Lazily get a console instance. Note that this script is loaded in very early to
   * the browser loading process, and may run before the console is avialable. In
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
   * Tracks if the popup is open, or scheduled to be open.
   *
   * @type {boolean}
   */
  #isPopupOpen = false;

  /**
   * Where the lazy elements are stored.
   *
   * @type {Record<string, Element>?}
   */
  #lazyElements;

  /**
   * Lazily creates the dom elements, and lazily selects them.
   *
   * @returns {Record<string, Element>}
   */
  get elements() {
    if (!this.#lazyElements) {
      // Lazily turn the template into a DOM element.
      /** @type {HTMLTemplateElement} */
      const wrapper = document.getElementById("template-translations-panel");
      const panel = wrapper.content.firstElementChild;
      wrapper.replaceWith(wrapper.content);

      panel.addEventListener("command", this);
      panel.addEventListener("click", this);
      panel.addEventListener("popupshown", this);
      panel.addEventListener("popuphidden", this);

      const settingsButton = document.getElementById(
        "translations-panel-settings"
      );
      // Clone the settings toolbarbutton across all the views.
      for (const header of panel.querySelectorAll(".panel-header")) {
        if (header.contains(settingsButton)) {
          continue;
        }
        const settingsButtonClone = settingsButton.cloneNode(true);
        settingsButtonClone.removeAttribute("id");
        header.appendChild(settingsButtonClone);
      }

      // Lazily select the elements.
      this.#lazyElements = {
        panel,
        settingsButton,
        // The rest of the elements are set by the getter below.
      };

      TranslationsPanelShared.defineLazyElements(document, this.#lazyElements, {
        alwaysTranslateLanguageMenuItem: ".always-translate-language-menuitem",
        appMenuButton: "PanelUI-menu-button",
        cancelButton: "full-page-translations-panel-cancel",
        changeSourceLanguageButton:
          "full-page-translations-panel-change-source-language",
        dismissErrorButton: "full-page-translations-panel-dismiss-error",
        error: "full-page-translations-panel-error",
        errorMessage: "full-page-translations-panel-error-message",
        errorMessageHint: "full-page-translations-panel-error-message-hint",
        errorHintAction: "full-page-translations-panel-translate-hint-action",
        fromLabel: "full-page-translations-panel-from-label",
        fromMenuList: "full-page-translations-panel-from",
        fromMenuPopup: "full-page-translations-panel-from-menupopup",
        header: "full-page-translations-panel-header",
        intro: "full-page-translations-panel-intro",
        introLearnMoreLink:
          "full-page-translations-panel-intro-learn-more-link",
        langSelection: "full-page-translations-panel-lang-selection",
        manageLanguagesMenuItem: ".manage-languages-menuitem",
        multiview: "full-page-translations-panel-multiview",
        neverTranslateLanguageMenuItem: ".never-translate-language-menuitem",
        neverTranslateSiteMenuItem: ".never-translate-site-menuitem",
        restoreButton: "full-page-translations-panel-restore-button",
        toLabel: "full-page-translations-panel-to-label",
        toMenuList: "full-page-translations-panel-to",
        toMenuPopup: "full-page-translations-panel-to-menupopup",
        translateButton: "full-page-translations-panel-translate",
        unsupportedHeader:
          "full-page-translations-panel-unsupported-language-header",
        unsupportedHint: "full-page-translations-panel-error-unsupported-hint",
        unsupportedLearnMoreLink:
          "full-page-translations-panel-unsupported-learn-more-link",
      });
    }

    return this.#lazyElements;
  }

  #lazyButtonElements = null;

  /**
   * When accessing `this.elements` the first time, it de-lazifies the custom components
   * that are needed for the popup. Avoid that by having a second element lookup
   * just for modifying the button.
   */
  get buttonElements() {
    if (!this.#lazyButtonElements) {
      this.#lazyButtonElements = {
        button: document.getElementById("translations-button"),
        buttonLocale: document.getElementById("translations-button-locale"),
        buttonCircleArrows: document.getElementById(
          "translations-button-circle-arrows"
        ),
      };
    }
    return this.#lazyButtonElements;
  }

  /**
   * Cache the last command used for error hints so that it can be later removed.
   */
  #lastHintCommand = null;

  /**
   * @param {object} options
   * @param {string} options.message - l10n id
   * @param {string} options.hint - l10n id
   * @param {string} options.actionText - l10n id
   * @param {Function} options.actionCommand - The action to perform.
   */
  #showError({
    message,
    hint,
    actionText: hintCommandText,
    actionCommand: hintCommand,
  }) {
    const { error, errorMessage, errorMessageHint, errorHintAction, intro } =
      this.elements;
    error.hidden = false;
    intro.hidden = true;
    document.l10n.setAttributes(errorMessage, message);

    if (hint) {
      errorMessageHint.hidden = false;
      document.l10n.setAttributes(errorMessageHint, hint);
    } else {
      errorMessageHint.hidden = true;
    }

    if (hintCommand && hintCommandText) {
      errorHintAction.removeEventListener("command", this.#lastHintCommand);
      this.#lastHintCommand = hintCommand;
      errorHintAction.addEventListener("command", hintCommand);
      errorHintAction.hidden = false;
      document.l10n.setAttributes(errorHintAction, hintCommandText);
    } else {
      errorHintAction.hidden = true;
    }
  }

  /**
   * Fetches the language tags for the document and the user and caches the results
   * Use `#getCachedDetectedLanguages` when the lang tags do not need to be re-fetched.
   * This requires a bit of work to do, so prefer the cached version when possible.
   *
   * @returns {Promise<LangTags>}
   */
  async #fetchDetectedLanguages() {
    this.detectedLanguages = await TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).getDetectedLanguages();
    return this.detectedLanguages;
  }

  /**
   * If the detected language tags have been retrieved previously, return the cached
   * version. Otherwise do a fresh lookup of the document's language tag.
   *
   * @returns {Promise<LangTags>}
   */
  async #getCachedDetectedLanguages() {
    if (!this.detectedLanguages) {
      return this.#fetchDetectedLanguages();
    }
    return this.detectedLanguages;
  }

  /**
   * Builds the <menulist> of languages for both the "from" and "to". This can be
   * called every time the popup is shown, as it will retry when there is an error
   * (such as a network error) or be a noop if it's already initialized.
   */
  async #ensureLangListsBuilt() {
    try {
      await TranslationsPanelShared.ensureLangListsBuilt(document, this);
    } catch (error) {
      this.console?.error(error);
    }
  }

  /**
   * Reactively sets the views based on the async state changes of the engine, and
   * other component state changes.
   *
   * @param {TranslationsLanguageState} languageState
   */
  #updateViewFromTranslationStatus(
    languageState = TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).languageState
  ) {
    const { translateButton, toMenuList, fromMenuList, header, cancelButton } =
      this.elements;
    const { requestedLanguagePair, isEngineReady } = languageState;

    // Remove the model variant. e.g. "ru,base" -> "ru"
    const selectedFrom = fromMenuList.value.split(",")[0];
    const selectedTo = toMenuList.value.split(",")[0];

    if (
      requestedLanguagePair &&
      !isEngineReady &&
      TranslationsUtils.langTagsMatch(
        selectedFrom,
        requestedLanguagePair.sourceLanguage
      ) &&
      TranslationsUtils.langTagsMatch(
        selectedTo,
        requestedLanguagePair.targetLanguage
      )
    ) {
      // A translation has been requested, but is not ready yet.
      document.l10n.setAttributes(
        translateButton,
        "translations-panel-translate-button-loading"
      );
      translateButton.disabled = true;
      cancelButton.hidden = false;
      this.updateUIForReTranslation(false /* isReTranslation */);
    } else {
      document.l10n.setAttributes(
        translateButton,
        "translations-panel-translate-button"
      );
      translateButton.disabled =
        // No "to" language was provided.
        !toMenuList.value ||
        // No "from" language was provided.
        !fromMenuList.value ||
        // The translation languages are the same, don't allow this translation.
        TranslationsUtils.langTagsMatch(selectedFrom, selectedTo) ||
        // This is the requested language pair.
        (requestedLanguagePair &&
          TranslationsUtils.langTagsMatch(
            requestedLanguagePair.sourceLanguage,
            selectedFrom
          ) &&
          TranslationsUtils.langTagsMatch(
            requestedLanguagePair.targetLanguage,
            selectedTo
          ));
    }

    if (requestedLanguagePair && isEngineReady) {
      const { sourceLanguage, targetLanguage } = requestedLanguagePair;
      const languageDisplayNames =
        TranslationsParent.createLanguageDisplayNames();
      cancelButton.hidden = true;
      this.updateUIForReTranslation(true /* isReTranslation */);

      document.l10n.setAttributes(header, "translations-panel-revisit-header", {
        fromLanguage: languageDisplayNames.of(sourceLanguage),
        toLanguage: languageDisplayNames.of(targetLanguage),
      });
    } else {
      document.l10n.setAttributes(header, "translations-panel-header");
    }
  }

  /**
   * @param {boolean} isReTranslation
   */
  updateUIForReTranslation(isReTranslation) {
    const { restoreButton, fromLabel, fromMenuList, toLabel } = this.elements;
    restoreButton.hidden = !isReTranslation;
    // When offering to re-translate a page, hide the "from" language so users don't
    // get confused.
    fromLabel.hidden = isReTranslation;
    fromMenuList.hidden = isReTranslation;
    if (isReTranslation) {
      fromLabel.style.marginBlockStart = "";
      toLabel.style.marginBlockStart = 0;
    } else {
      fromLabel.style.marginBlockStart = 0;
      toLabel.style.marginBlockStart = "";
    }
  }

  /**
   * Returns true if the panel is currently showing the default view, otherwise false.
   *
   * @returns {boolean}
   */
  #isShowingDefaultView() {
    if (!this.#lazyElements) {
      // Nothing has been initialized.
      return false;
    }
    const { multiview } = this.elements;
    return (
      multiview.getAttribute("mainViewId") ===
      "full-page-translations-panel-view-default"
    );
  }

  /**
   * Show the default view of choosing a source and target language.
   *
   * @param {TranslationsParent} actor
   * @param {boolean} force - Force the page to show translation options.
   */
  async #showDefaultView(actor, force = false) {
    const {
      fromMenuList,
      multiview,
      panel,
      error,
      toMenuList,
      translateButton,
      langSelection,
      intro,
      header,
    } = this.elements;

    this.#updateViewFromTranslationStatus();

    // Unconditionally hide the intro text in case the panel is re-shown.
    intro.hidden = true;

    if (TranslationsPanelShared.getLangListsInitState(this) === "error") {
      // There was an error, display it in the view rather than the language
      // dropdowns.
      const { cancelButton, errorHintAction } = this.elements;

      this.#showError({
        message: "translations-panel-error-load-languages",
        hint: "translations-panel-error-load-languages-hint",
        actionText: "translations-panel-error-load-languages-hint-button",
        actionCommand: () => this.#reloadLangList(actor),
      });

      translateButton.disabled = true;
      this.updateUIForReTranslation(false /* isReTranslation */);
      cancelButton.hidden = false;
      langSelection.hidden = true;
      errorHintAction.disabled = false;
      return;
    }

    // Remove any old selected values synchronously before asking for new ones.
    fromMenuList.value = "";
    error.hidden = true;
    langSelection.hidden = false;
    // Remove the model variant. e.g. "ru,base" -> "ru"
    const selectedSource = fromMenuList.value.split(",")[0];

    const { userLangTag, docLangTag, isDocLangTagSupported } =
      await this.#fetchDetectedLanguages().then(langTags => langTags ?? {});

    if (isDocLangTagSupported || force) {
      // Show the default view with the language selection
      const { cancelButton } = this.elements;

      if (isDocLangTagSupported) {
        fromMenuList.value = docLangTag ?? "";
      } else {
        fromMenuList.value = "";
      }

      if (
        this.#manuallySelectedToLanguage &&
        !TranslationsUtils.langTagsMatch(
          docLangTag,
          this.#manuallySelectedToLanguage
        )
      ) {
        // Use the manually selected language if available
        toMenuList.value = this.#manuallySelectedToLanguage;
      } else if (
        userLangTag &&
        !TranslationsUtils.langTagsMatch(userLangTag, docLangTag)
      ) {
        // The userLangTag is specified and does not match the doc lang tag, so we should use it.
        toMenuList.value = userLangTag;
      } else {
        // No userLangTag is specified in the cache and no #manuallySelectedToLanguage is available,
        // so we will attempt to find a suitable one.
        toMenuList.value =
          await TranslationsParent.getTopPreferredSupportedToLang({
            excludeLangTags: [
              // Avoid offering to translate into the original source language.
              docLangTag,
              // Avoid same-language to same-language translations if possible.
              selectedSource,
            ],
          });
      }

      const resolvedSource = fromMenuList.value.split(",")[0];
      const resolvedTarget = toMenuList.value.split(",")[0];

      if (TranslationsUtils.langTagsMatch(resolvedSource, resolvedTarget)) {
        // The best possible user-preferred language tag that we were able to find for the
        // toMenuList is the same as the fromMenuList, but same-language to same-language
        // translations are not allowed in Full Page Translations, so we will just show the
        // "Choose a language" option in this case.
        toMenuList.value = "";
      }

      this.onChangeLanguages();

      this.updateUIForReTranslation(false /* isReTranslation */);
      cancelButton.hidden = false;
      multiview.setAttribute(
        "mainViewId",
        "full-page-translations-panel-view-default"
      );

      if (TranslationsParent.hasUserEverTranslated()) {
        intro.hidden = true;
        document.l10n.setAttributes(header, "translations-panel-header");
      } else {
        intro.hidden = false;
        document.l10n.setAttributes(header, "translations-panel-intro-header");
      }
    } else {
      // Show the "unsupported language" view.
      const { unsupportedHint } = this.elements;
      multiview.setAttribute(
        "mainViewId",
        "full-page-translations-panel-view-unsupported-language"
      );
      let language;
      if (docLangTag) {
        const languageDisplayNames =
          TranslationsParent.createLanguageDisplayNames({
            fallback: "none",
          });
        language = languageDisplayNames.of(docLangTag);
      }
      if (language) {
        document.l10n.setAttributes(
          unsupportedHint,
          "translations-panel-error-unsupported-hint-known-2",
          { language }
        );
      } else {
        document.l10n.setAttributes(
          unsupportedHint,
          "translations-panel-error-unsupported-hint-unknown-2"
        );
      }
    }

    // Focus the "from" language, as it is the only field not set.
    panel.addEventListener(
      "ViewShown",
      () => {
        if (!fromMenuList.value) {
          fromMenuList.focus();
        }
        if (!toMenuList.value) {
          toMenuList.focus();
        }
      },
      { once: true }
    );
  }

  /**
   * Updates the checked states of the settings menu checkboxes that
   * pertain to languages.
   */
  async #updateSettingsMenuLanguageCheckboxStates() {
    const langTags = await this.#getCachedDetectedLanguages();
    const { docLangTag, isDocLangTagSupported } = langTags;

    const { panel } = this.elements;
    const alwaysTranslateMenuItems = panel.ownerDocument.querySelectorAll(
      ".always-translate-language-menuitem"
    );
    const neverTranslateMenuItems = panel.ownerDocument.querySelectorAll(
      ".never-translate-language-menuitem"
    );
    const alwaysOfferTranslationsMenuItems =
      panel.ownerDocument.querySelectorAll(
        ".always-offer-translations-menuitem"
      );

    const alwaysOfferTranslations =
      TranslationsParent.shouldAlwaysOfferTranslations();
    const alwaysTranslateLanguage =
      TranslationsParent.shouldAlwaysTranslateLanguage(langTags);
    const neverTranslateLanguage =
      TranslationsParent.shouldNeverTranslateLanguage(docLangTag);
    const shouldDisable =
      !docLangTag ||
      !isDocLangTagSupported ||
      docLangTag ===
        (await TranslationsParent.getTopPreferredSupportedToLang());

    for (const menuitem of alwaysOfferTranslationsMenuItems) {
      menuitem.toggleAttribute("checked", alwaysOfferTranslations);
    }
    for (const menuitem of alwaysTranslateMenuItems) {
      menuitem.toggleAttribute("checked", alwaysTranslateLanguage);
      menuitem.disabled = shouldDisable;
    }
    for (const menuitem of neverTranslateMenuItems) {
      menuitem.toggleAttribute("checked", neverTranslateLanguage);
      menuitem.disabled = shouldDisable;
    }
  }

  /**
   * Updates the checked states of the settings menu checkboxes that
   * pertain to site permissions.
   */
  async #updateSettingsMenuSiteCheckboxStates() {
    const { panel } = this.elements;
    const neverTranslateSiteMenuItems = panel.ownerDocument.querySelectorAll(
      ".never-translate-site-menuitem"
    );
    const neverTranslateSite = await TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).shouldNeverTranslateSite();

    for (const menuitem of neverTranslateSiteMenuItems) {
      menuitem.toggleAttribute("checked", neverTranslateSite);
    }
  }

  /**
   * Populates the language-related settings menuitems by adding the
   * localized display name of the document's detected language tag.
   */
  async #populateSettingsMenuItems() {
    const { docLangTag } = await this.#getCachedDetectedLanguages();

    const { panel } = this.elements;

    const alwaysTranslateMenuItems = panel.ownerDocument.querySelectorAll(
      ".always-translate-language-menuitem"
    );
    const neverTranslateMenuItems = panel.ownerDocument.querySelectorAll(
      ".never-translate-language-menuitem"
    );

    /** @type {string | undefined} */
    let docLangDisplayName;
    if (docLangTag) {
      const languageDisplayNames =
        TranslationsParent.createLanguageDisplayNames({
          fallback: "none",
        });
      // The display name will still be empty if the docLangTag is not known.
      docLangDisplayName = languageDisplayNames.of(docLangTag);
    }

    for (const menuitem of alwaysTranslateMenuItems) {
      if (docLangDisplayName) {
        document.l10n.setAttributes(
          menuitem,
          "translations-panel-settings-always-translate-language",
          { language: docLangDisplayName }
        );
      } else {
        document.l10n.setAttributes(
          menuitem,
          "translations-panel-settings-always-translate-unknown-language"
        );
      }
    }

    for (const menuitem of neverTranslateMenuItems) {
      if (docLangDisplayName) {
        document.l10n.setAttributes(
          menuitem,
          "translations-panel-settings-never-translate-language",
          { language: docLangDisplayName }
        );
      } else {
        document.l10n.setAttributes(
          menuitem,
          "translations-panel-settings-never-translate-unknown-language"
        );
      }
    }

    await Promise.all([
      this.#updateSettingsMenuLanguageCheckboxStates(),
      this.#updateSettingsMenuSiteCheckboxStates(),
    ]);
  }

  /**
   * Configures the panel for the user to reset the page after it has been translated.
   *
   * @param {LanguagePair} languagePair
   */
  async #showRevisitView({ sourceLanguage, targetLanguage, sourceVariant }) {
    const { fromMenuList, toMenuList, intro } = this.elements;
    if (!this.#isShowingDefaultView()) {
      await this.#showDefaultView(
        TranslationsParent.getTranslationsActor(gBrowser.selectedBrowser)
      );
    }
    intro.hidden = true;
    if (sourceVariant) {
      fromMenuList.value = `${sourceLanguage},${sourceVariant}`;
    } else {
      fromMenuList.value = sourceLanguage;
    }
    toMenuList.value = await TranslationsParent.getTopPreferredSupportedToLang({
      excludeLangTags: [
        // Avoid offering to translate into the original source language.
        sourceLanguage,
        // Avoid offering to translate into current active target language.
        targetLanguage,
      ],
    });
    this.onChangeLanguages();
  }

  /**
   * Handle the disable logic for when the menulist is changed for the "Translate to"
   * on the "revisit" subview.
   */
  onChangeRevisitTo() {
    const { revisitTranslate, revisitMenuList } = this.elements;
    revisitTranslate.disabled = !revisitMenuList.value;
  }

  /**
   * Handle logic and telemetry for changing the selected from-language option.
   *
   * @param {Event} event
   */
  async onChangeFromLanguage(event) {
    try {
      const { target } = event;

      if (!target?.value) {
        return;
      }

      TranslationsParent.telemetry()
        .fullPagePanel()
        .onChangeFromLanguage(target.value);

      const selectedFrom = target.value;

      // Compute only if "to" Language is not set
      if (this.elements.toMenuList.value) {
        return;
      }

      let toValue = await TranslationsParent.getTopPreferredSupportedToLang({
        excludeLangTags: [selectedFrom], // Avoid same-to-same language translation
      });

      // Re-check in case the user selected a "to" language while awaiting top preferred to language computation.
      if (this.elements.toMenuList.value) {
        return;
      }

      if (TranslationsUtils.langTagsMatch(selectedFrom, toValue)) {
        toValue = "";
      }

      this.elements.toMenuList.value = toValue;
    } catch (error) {
      this.console?.error(error);
    } finally {
      this.onChangeLanguages();
    }
  }

  /**
   * Handle logic and telemetry for changing the selected to-language option.
   *
   * @param {Event} event
   */
  onChangeToLanguage(event) {
    const { target } = event;
    if (target?.value) {
      TranslationsParent.telemetry()
        .fullPagePanel()
        .onChangeToLanguage(target.value);
    }
    this.onChangeLanguages();
    // Update the manually selected language when the user changes the target language.
    this.#manuallySelectedToLanguage = target.value;
  }

  /**
   * When changing the language selection, the translate button will need updating.
   */
  onChangeLanguages() {
    this.#updateViewFromTranslationStatus();
  }

  /**
   * Hide the pop up (for event handlers).
   */
  close() {
    PanelMultiView.hidePopup(this.elements.panel);
  }

  /*
   * Handler for clicking the learn more link from linked text
   * within the translations panel.
   */
  onLearnMoreLink() {
    TranslationsParent.telemetry().fullPagePanel().onLearnMoreLink();
    FullPageTranslationsPanel.close();
  }

  /*
   * Handler for clicking the learn more link from the gear menu.
   */
  onAboutTranslations() {
    TranslationsParent.telemetry().fullPagePanel().onAboutTranslations();
    PanelMultiView.hidePopup(this.elements.panel);
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
   * When a language is not supported and the menu is manually invoked, an error message
   * is shown. This method switches the panel back to the language selection view.
   * Note that this bypasses the showSubView method since the main view doesn't support
   * a subview.
   */
  async onChangeSourceLanguage(event) {
    const { panel } = this.elements;
    PanelMultiView.hidePopup(panel);

    await this.#showDefaultView(
      TranslationsParent.getTranslationsActor(gBrowser.selectedBrowser),
      true /* force this view to be shown */
    );

    await this.#openPanelPopup(this.elements.appMenuButton, {
      event,
      viewName: "defaultView",
      maintainFlow: true,
    });
  }

  /**
   * @param {TranslationsActor} actor
   */
  async #reloadLangList(actor) {
    try {
      await this.#ensureLangListsBuilt();
      await this.#showDefaultView(actor);
    } catch (error) {
      this.elements.errorHintAction.disabled = false;
    }
  }

  /**
   * Handle telemetry events when buttons are invoked in the panel.
   *
   * @param {Event} event
   */
  handlePanelButtonEvent(event) {
    const {
      cancelButton,
      changeSourceLanguageButton,
      dismissErrorButton,
      restoreButton,
      translateButton,
    } = this.elements;
    switch (event.target.id) {
      case cancelButton.id: {
        TranslationsParent.telemetry().fullPagePanel().onCancelButton();
        break;
      }
      case changeSourceLanguageButton.id: {
        TranslationsParent.telemetry()
          .fullPagePanel()
          .onChangeSourceLanguageButton();
        break;
      }
      case dismissErrorButton.id: {
        TranslationsParent.telemetry().fullPagePanel().onDismissErrorButton();
        break;
      }
      case restoreButton.id: {
        TranslationsParent.telemetry().fullPagePanel().onRestorePageButton();
        break;
      }
      case translateButton.id: {
        TranslationsParent.telemetry().fullPagePanel().onTranslateButton();
        break;
      }
    }
  }

  /**
   * Handle telemetry events when popups are shown in the panel.
   *
   * @param {Event} event
   */
  handlePanelPopupShownEvent(event) {
    const { panel, fromMenuList, toMenuList } = this.elements;
    switch (event.target.id) {
      case panel.id: {
        // This telemetry event is invoked externally because it requires
        // extra logic about from where the panel was opened and whether
        // or not the flow should be maintained or started anew.
        break;
      }
      case fromMenuList.firstChild.id: {
        TranslationsParent.telemetry().fullPagePanel().onOpenFromLanguageMenu();
        break;
      }
      case toMenuList.firstChild.id: {
        TranslationsParent.telemetry().fullPagePanel().onOpenToLanguageMenu();
        break;
      }
    }
  }

  /**
   * Handle telemetry events when popups are hidden in the panel.
   *
   * @param {Event} event
   */
  handlePanelPopupHiddenEvent(event) {
    const { panel, fromMenuList, toMenuList } = this.elements;
    switch (event.target.id) {
      case panel.id: {
        TranslationsParent.telemetry().fullPagePanel().onClose();
        this.#isPopupOpen = false;
        this.elements.error.hidden = true;
        break;
      }
      case fromMenuList.firstChild.id: {
        TranslationsParent.telemetry()
          .fullPagePanel()
          .onCloseFromLanguageMenu();
        break;
      }
      case toMenuList.firstChild.id: {
        TranslationsParent.telemetry().fullPagePanel().onCloseToLanguageMenu();
        break;
      }
    }
  }

  /**
   * Handle telemetry events when the settings menu is shown.
   */
  handleSettingsPopupShownEvent() {
    TranslationsParent.telemetry().fullPagePanel().onOpenSettingsMenu();
  }

  /**
   * Handle telemetry events when the settings menu is hidden.
   */
  handleSettingsPopupHiddenEvent() {
    TranslationsParent.telemetry().fullPagePanel().onCloseSettingsMenu();
  }

  /**
   * Opens the Translations panel popup at the given target.
   *
   * @param {object} target - The target element at which to open the popup.
   * @param {object} telemetryData
   * @param {string} telemetryData.event
   *   The trigger event for opening the popup.
   * @param {string} telemetryData.viewName
   *   The name of the view shown by the panel.
   * @param {boolean} telemetryData.autoShow
   *   True if the panel was automatically opened, otherwise false.
   * @param {boolean} telemetryData.maintainFlow
   *   Whether or not to maintain the flow of telemetry.
   */
  async #openPanelPopup(
    target,
    { event = null, viewName = null, autoShow = false, maintainFlow = false }
  ) {
    const { panel, appMenuButton } = this.elements;
    const openedFromAppMenu = target.id === appMenuButton.id;
    const { docLangTag } = await this.#getCachedDetectedLanguages();

    TranslationsParent.telemetry().fullPagePanel().onOpen({
      viewName,
      autoShow,
      docLangTag,
      maintainFlow,
      openedFromAppMenu,
    });

    this.#isPopupOpen = true;

    PanelMultiView.openPopup(panel, target, {
      position: "bottomright topright",
      triggerEvent: event,
    }).catch(error => this.console?.error(error));
  }

  /**
   * Keeps track of open requests to guard against race conditions.
   *
   * @type {Promise<void> | null}
   */
  #openPromise = null;

  /**
   * Opens the FullPageTranslationsPanel.
   *
   * @param {Event} event
   * @param {boolean} reportAsAutoShow
   *   True to report to telemetry that the panel was opened automatically, otherwise false.
   */
  async open(event, reportAsAutoShow = false) {
    if (this.#openPromise) {
      // There is already an open event happening, do not open.
      return;
    }

    this.#openPromise = this.#openImpl(event, reportAsAutoShow);
    this.#openPromise.finally(() => {
      this.#openPromise = null;
    });
  }

  /**
   * The language tag that was manually selected by the user.
   * This is used to remember the selection if the user happens to close and then reopen the panel prior to translating.
   *
   * @type {string | null}
   */
  #manuallySelectedToLanguage = null;

  /**
   * Implementation function for opening the panel. Prefer FullPageTranslationsPanel.open.
   *
   * @param {Event} event
   */
  async #openImpl(event, reportAsAutoShow) {
    event.stopPropagation();
    if (
      (event.type == "click" && event.button != 0) ||
      (event.type == "keypress" &&
        event.charCode != KeyEvent.DOM_VK_SPACE &&
        event.keyCode != KeyEvent.DOM_VK_RETURN)
    ) {
      // Allow only left click, space, or enter.
      return;
    }

    const { button } = this.buttonElements;

    const { requestedLanguagePair } = TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).languageState;

    await this.#ensureLangListsBuilt();

    if (requestedLanguagePair) {
      await this.#showRevisitView(requestedLanguagePair).catch(error => {
        this.console?.error(error);
      });
    } else {
      await this.#showDefaultView(
        TranslationsParent.getTranslationsActor(gBrowser.selectedBrowser)
      ).catch(error => {
        this.console?.error(error);
      });
    }

    this.#populateSettingsMenuItems();

    const targetButton =
      button.contains(event.target) ||
      event.type === "TranslationsParent:OfferTranslation"
        ? button
        : this.elements.appMenuButton;

    this.console?.log(`Showing a translation panel`, gBrowser.currentURI.spec);

    await this.#openPanelPopup(targetButton, {
      event,
      autoShow: reportAsAutoShow,
      viewName: requestedLanguagePair ? "revisitView" : "defaultView",
      maintainFlow: false,
    });
  }

  /**
   * Returns true if translations is currently active, otherwise false.
   *
   * @returns {boolean}
   */
  #isTranslationsActive() {
    const { requestedLanguagePair } = TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).languageState;
    return requestedLanguagePair !== null;
  }

  /**
   * Handle the translation button being clicked when there are two language options.
   */
  async onTranslate() {
    this.#manuallySelectedToLanguage = null;
    PanelMultiView.hidePopup(this.elements.panel);

    const actor = TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    );
    const [sourceLanguage, sourceVariant] =
      this.elements.fromMenuList.value.split(",");
    const [targetLanguage, targetVariant] =
      this.elements.toMenuList.value.split(",");

    actor.translate(
      { sourceLanguage, targetLanguage, sourceVariant, targetVariant },
      false // reportAsAutoTranslate
    );
  }

  /**
   * Handle the cancel button being clicked.
   */
  onCancel() {
    PanelMultiView.hidePopup(this.elements.panel);
  }

  /**
   * A handler for opening the settings context menu.
   */
  async openSettingsPopup(button) {
    await Promise.all([
      this.#updateSettingsMenuLanguageCheckboxStates(),
      this.#updateSettingsMenuSiteCheckboxStates(),
    ]);
    const popup = button.ownerDocument.getElementById(
      "full-page-translations-panel-settings-menupopup"
    );
    popup.openPopup(button, "after_end");
  }

  /**
   * Creates a new CheckboxPageAction based on the current translated
   * state of the page and the state of the persistent options in the
   * translations panel settings.
   *
   * @returns {CheckboxPageAction}
   */
  getCheckboxPageActionFor() {
    const {
      alwaysTranslateLanguageMenuItem,
      neverTranslateLanguageMenuItem,
      neverTranslateSiteMenuItem,
    } = this.elements;

    const alwaysTranslateLanguage =
      alwaysTranslateLanguageMenuItem.hasAttribute("checked");
    const neverTranslateLanguage =
      neverTranslateLanguageMenuItem.hasAttribute("checked");
    const neverTranslateSite =
      neverTranslateSiteMenuItem.hasAttribute("checked");

    return new CheckboxPageAction(
      this.#isTranslationsActive(),
      alwaysTranslateLanguage,
      neverTranslateLanguage,
      neverTranslateSite
    );
  }

  /**
   * Redirect the user to about:preferences
   */
  openManageLanguages() {
    TranslationsParent.telemetry().fullPagePanel().onManageLanguages();
    const window =
      gBrowser.selectedBrowser.browsingContext.top.embedderElement
        .documentGlobal;
    window.openTrustedLinkIn("about:preferences#general-translations", "tab");
  }

  /**
   * Performs the given page action.
   *
   * @param {PageAction} pageAction
   */
  async #doPageAction(pageAction) {
    switch (pageAction) {
      case PageAction.NO_CHANGE: {
        break;
      }
      case PageAction.RESTORE_PAGE: {
        await this.onRestore();
        break;
      }
      case PageAction.TRANSLATE_PAGE: {
        await this.onTranslate();
        break;
      }
      case PageAction.CLOSE_PANEL: {
        PanelMultiView.hidePopup(this.elements.panel);
        break;
      }
    }
  }

  /**
   * Updates the always-translate-language menuitem prefs and checked state.
   * If auto-translate is currently active for the doc language, deactivates it.
   * If auto-translate is currently inactive for the doc language, activates it.
   */
  async onAlwaysTranslateLanguage() {
    const langTags = await this.#getCachedDetectedLanguages();
    const { docLangTag } = langTags;
    if (!docLangTag) {
      throw new Error("Expected to have a document language tag.");
    }
    const pageAction =
      this.getCheckboxPageActionFor().alwaysTranslateLanguage();
    const toggledOn =
      TranslationsParent.toggleAlwaysTranslateLanguagePref(langTags);
    TranslationsParent.telemetry()
      .fullPagePanel()
      .onAlwaysTranslateLanguage(docLangTag, toggledOn);
    this.#updateSettingsMenuLanguageCheckboxStates();
    await this.#doPageAction(pageAction);
  }

  /**
   * Toggle offering translations.
   */
  async onAlwaysOfferTranslations() {
    const toggledOn = TranslationsParent.toggleAutomaticallyPopupPref();
    TranslationsParent.telemetry()
      .fullPagePanel()
      .onAlwaysOfferTranslations(toggledOn);
  }

  /**
   * Updates the never-translate-language menuitem prefs and checked state.
   * If never-translate is currently active for the doc language, deactivates it.
   * If never-translate is currently inactive for the doc language, activates it.
   */
  async onNeverTranslateLanguage() {
    const { docLangTag } = await this.#getCachedDetectedLanguages();
    if (!docLangTag) {
      throw new Error("Expected to have a document language tag.");
    }
    const pageAction = this.getCheckboxPageActionFor().neverTranslateLanguage();
    const toggledOn =
      TranslationsParent.toggleNeverTranslateLanguagePref(docLangTag);
    TranslationsParent.telemetry()
      .fullPagePanel()
      .onNeverTranslateLanguage(docLangTag, toggledOn);
    this.#updateSettingsMenuLanguageCheckboxStates();
    await this.#doPageAction(pageAction);
  }

  /**
   * Updates the never-translate-site menuitem permissions and checked state.
   * If never-translate is currently active for the site, deactivates it.
   * If never-translate is currently inactive for the site, activates it.
   */
  async onNeverTranslateSite() {
    const pageAction = this.getCheckboxPageActionFor().neverTranslateSite();
    const toggledOn = await TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).toggleNeverTranslateSitePermissions();
    TranslationsParent.telemetry()
      .fullPagePanel()
      .onNeverTranslateSite(toggledOn);
    this.#updateSettingsMenuSiteCheckboxStates();
    await this.#doPageAction(pageAction);
  }

  /**
   * Handle the restore button being clicked.
   */
  async onRestore() {
    const { panel } = this.elements;
    PanelMultiView.hidePopup(panel);
    const { docLangTag } = await this.#getCachedDetectedLanguages();
    if (!docLangTag) {
      throw new Error("Expected to have a document language tag.");
    }

    TranslationsParent.getTranslationsActor(
      gBrowser.selectedBrowser
    ).restorePage(docLangTag);
  }

  /**
   * An event handler that allows the FullPageTranslationsPanel object
   * to be compatible with the addTabsProgressListener function.
   *
   * @param {tabbrowser} browser
   */
  onLocationChange(browser) {
    if (browser !== gBrowser.selectedBrowser) {
      // If the given browser is not for the active tab, we should not process it right now.
      // Its state will be processed accordingly whenever that tab becomes active.
      return;
    }

    if (TranslationsParent.isFullPageTranslationsRestrictedForPage(gBrowser)) {
      this.buttonElements.button.hidden = true;
    } else if (!TranslationsFeature.isEnabled) {
      // When the Translations feature is disabled, no actor instance is created, therefore no
      // event will be dispatched to update button visibility. We need to handle it here instead.
      this.buttonElements.button.hidden = true;
    }
  }

  /**
   * Update the view to show an error.
   *
   * @param {TranslationParent} actor
   */
  async #showEngineError(actor) {
    const { button } = this.buttonElements;
    await this.#ensureLangListsBuilt();
    await this.#showDefaultView(actor).catch(e => {
      this.console?.error(e);
    });
    this.elements.error.hidden = false;
    this.#showError({
      message: "translations-panel-error-translating",
    });
    const targetButton = button.hidden ? this.elements.appMenuButton : button;

    // Re-open the menu on an error.
    await this.#openPanelPopup(targetButton, {
      autoShow: true,
      viewName: "errorView",
      maintainFlow: true,
    });
  }

  /**
   * Set the state of the translations button in the URL bar.
   *
   * @param {CustomEvent} event
   */
  handleEvent = event => {
    const target = event.target;
    let { id } = target;

    // If a menuitem within a menulist is the target, it will not have an id,
    // so we want to grab the closest relevant id.
    if (!id) {
      id = target.closest("[id]")?.id;
    }

    switch (event.type) {
      case "command": {
        switch (id) {
          case "translations-panel-settings":
            this.openSettingsPopup(target);
            break;
          case "full-page-translations-panel-from-menupopup":
            this.onChangeFromLanguage(event);
            break;
          case "full-page-translations-panel-to-menupopup":
            this.onChangeToLanguage(event);
            break;
          case "full-page-translations-panel-restore-button":
            this.onRestore(event);
            break;
          case "full-page-translations-panel-cancel":
          case "full-page-translations-panel-dismiss-error":
            this.onCancel(event);
            break;
          case "full-page-translations-panel-translate":
            this.onTranslate(event);
            break;
          case "full-page-translations-panel-change-source-language":
            this.onChangeSourceLanguage(event);
            break;
        }
        break;
      }
      case "click": {
        switch (id) {
          case "full-page-translations-panel-intro-learn-more-link":
          case "full-page-translations-panel-unsupported-learn-more-link":
            this.onLearnMoreLink();
            break;
          default:
            this.handlePanelButtonEvent(event);
        }
        break;
      }
      case "popupshown":
        this.handlePanelPopupShownEvent(event);
        break;
      case "popuphidden":
        this.handlePanelPopupHiddenEvent(event);
        break;
      case "TranslationsParent:OfferTranslation": {
        if (Services.wm.getMostRecentBrowserWindow()?.gBrowser === gBrowser) {
          this.open(event, /* reportAsAutoShow */ true);
        }
        break;
      }
      case "TranslationsParent:LanguageState": {
        const { actor, reason } = event.detail;

        const innerWindowId =
          gBrowser.selectedBrowser.browsingContext.top.embedderElement
            .innerWindowID;

        this.console?.debug("TranslationsParent:LanguageState", {
          reason,
          currentId: innerWindowId,
          originatorId: actor.innerWindowId,
        });

        if (innerWindowId !== actor.innerWindowId) {
          // The id of the currently active tab does not match the id of the tab that was active when the event was dispatched.
          // This likely means that the tab was changed after the event was dispatched, but before it was received by this class.
          //
          // Keep in mind that there is only one instance of this class (FullPageTranslationsPanel) for each open Firefox window,
          // but there is one instance of the TranslationsParent actor for each open tab within a Firefox window. As such, it is
          // possible for a tab-specific actor to fire an event that is received by the window-global panel after switching tabs.
          //
          // Since the dispatched event did not originate in the currently active tab, we should not react to it any further.
          return;
        }

        const {
          detectedLanguages,
          requestedLanguagePair,
          error,
          isEngineReady,
        } = actor.languageState;

        const { button, buttonLocale, buttonCircleArrows } =
          this.buttonElements;

        const hasSupportedLanguage =
          detectedLanguages?.docLangTag &&
          detectedLanguages?.userLangTag &&
          detectedLanguages?.isDocLangTagSupported;

        if (detectedLanguages) {
          // Ensure the cached detected languages are up to date, for instance whenever
          // the user switches tabs.
          FullPageTranslationsPanel.detectedLanguages = detectedLanguages;
          // Reset the manually selected language when the user switches tabs.
          this.#manuallySelectedToLanguage = null;
        }

        if (this.#isPopupOpen) {
          // Make sure to use the language state that is passed by the event.detail, and
          // don't read it from the actor here, as it's possible the actor isn't available
          // via the gBrowser.selectedBrowser.
          this.#updateViewFromTranslationStatus(actor.languageState);
        }

        if (
          // Only show the button if the Translations feature is enabled.
          TranslationsFeature.isEnabled &&
          // We've already requested to translate this page, so always show the icon.
          (requestedLanguagePair ||
            // There was an error translating, so always show the icon. This can happen
            // when a user manually invokes the translation and we wouldn't normally show
            // the icon.
            error ||
            // Finally check that we can translate this language.
            (hasSupportedLanguage &&
              TranslationsParent.getIsTranslationsEngineSupported()))
        ) {
          // Keep track if the button was originally hidden, because it will be shown now.
          const wasButtonHidden = button.hidden;

          button.hidden = false;
          if (requestedLanguagePair) {
            // The translation is active, update the urlbar button.
            button.setAttribute("translationsactive", true);
            if (isEngineReady) {
              const languageDisplayNames =
                TranslationsParent.createLanguageDisplayNames();

              document.l10n.setAttributes(
                button,
                "urlbar-translations-button-translated",
                {
                  fromLanguage: languageDisplayNames.of(
                    requestedLanguagePair.sourceLanguage
                  ),
                  toLanguage: languageDisplayNames.of(
                    requestedLanguagePair.targetLanguage
                  ),
                }
              );
              // Show the language tag of translated the page in the button.
              buttonLocale.hidden = false;
              buttonCircleArrows.hidden = true;
              buttonLocale.innerText =
                requestedLanguagePair.targetLanguage.split("-")[0];
            } else {
              document.l10n.setAttributes(
                button,
                "urlbar-translations-button-loading"
              );
              // Show the spinning circle arrows to indicate that the engine is
              // still loading.
              buttonCircleArrows.hidden = false;
              buttonLocale.hidden = true;
            }
          } else {
            // The translation is not active, update the urlbar button.
            button.removeAttribute("translationsactive");
            buttonLocale.hidden = true;
            buttonCircleArrows.hidden = true;

            // Follow the same rules for displaying the first-run intro text for the
            // button's accessible tooltip label.
            if (TranslationsParent.hasUserEverTranslated()) {
              document.l10n.setAttributes(
                button,
                "urlbar-translations-button2"
              );
            } else {
              document.l10n.setAttributes(
                button,
                "urlbar-translations-button-intro"
              );
            }
          }

          // The button was hidden, but now it is shown.
          if (wasButtonHidden) {
            PageActions.sendPlacedInUrlbarTrigger(button);
          }
        } else if (!button.hidden) {
          // There are no translations visible, hide the button.
          button.hidden = true;
        }

        switch (error) {
          case null:
            break;
          case "engine-load-failure":
            this.#showEngineError(actor).catch(viewError =>
              this.console?.error(viewError)
            );
            break;
          default:
            console.error("Unknown translation error", error);
        }
        break;
      }
    }
  };
})();
