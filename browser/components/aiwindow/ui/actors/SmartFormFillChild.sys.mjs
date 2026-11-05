/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({ logModule: "SmartFormFillChild" });
});

ChromeUtils.defineLazyGetter(lazy, "formFillController", function () {
  return Cc["@mozilla.org/satchel/form-fill-controller;1"].getService(
    Ci.nsIFormFillController
  );
});

ChromeUtils.defineESModuleGetters(lazy, {
  FormHistoryAutoCompleteResult:
    "resource://gre/modules/FormHistoryAutoComplete.sys.mjs",
  SmartFormFillDocument:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs",
});

/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillController.sys.mjs").FillFormResult} FillFormResult */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").SmartFormFillDocument} SmartFormFillDocument */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FormData} FormData */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FocusedForm} FocusedForm */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FieldOutcomes} FieldOutcomes */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FillFormOperationResult} FillFormOperationResult */

/**
 * @typedef {{
 *   data: FillFormResult,
 *   name: "SmartFormFill:FillForm",
 * } |
 * {
 *  data?: undefined,
 *  name: "SmartFormFill:StopFilling"
 * } |
 * {
 *  data?: undefined,
 *  name: "SmartFormFill:GetFocusedForm"
 * } |
 * {
 *  data?: undefined,
 *  name: "SmartFormFill:RefreshAutocomplete"
 * } |
 * {
 *  data?: undefined,
 *  name: "SmartFormFill:ShowAutocompletePopup"
 * }} SmartFormFillMessage
 */

/**
 * Child actor for Smart Form Fill.
 */
export class SmartFormFillChild extends JSWindowActorChild {
  /**
   * Document manager for the current page.
   *
   * @type {SmartFormFillDocument | null}
   */
  #smartFormFillDocument = null;

  /**
   * Whether the actor has been destroyed.
   *
   * @type {boolean}
   */
  #destroyed = false;

  /**
   * Cancels a pending attempt to reopen the autocomplete popup after focus
   * returns from a tab-modal dialog.
   *
   * @type {AbortController | null}
   */
  #autocompletePopupFocusAbortController = null;

  /**
   * Current document preparation request.
   *
   * @type {Promise<void> | null}
   */
  #documentPreparationPromise = null;

  /**
   * Prepares the child actor's document.
   */
  actorCreated() {
    if (this.document.readyState !== "loading") {
      this.#prepareDocument();
    }
  }

  /**
   * Prepares Smart Form Fill after the document loads.
   *
   * @param {Event} event
   * @returns {Promise<void>}
   */
  async handleEvent(event) {
    if (event.type !== "DOMContentLoaded") {
      return;
    }

    await this.#prepareDocument();
  }

  /**
   * Ensures document preparation has completed.
   *
   * @returns {Promise<void>}
   */
  #prepareDocument() {
    if (this.#documentPreparationPromise) {
      return this.#documentPreparationPromise;
    }

    if (this.#smartFormFillDocument || this.#destroyed) {
      return Promise.resolve();
    }

    const documentPreparationPromise = this.#setUpDocument()
      .catch(error => {
        this.#smartFormFillDocument?.destroy();
        this.#smartFormFillDocument = null;

        if (!this.#destroyed) {
          lazy.console.error(
            "Smart Form Fill document preparation failed",
            error
          );
        }
      })
      .finally(() => {
        if (this.#documentPreparationPromise === documentPreparationPromise) {
          this.#documentPreparationPromise = null;
        }
      });

    this.#documentPreparationPromise = documentPreparationPromise;
    return documentPreparationPromise;
  }

  /**
   * Sets up Smart Form Fill for the current document.
   *
   * @returns {Promise<void>}
   */
  async #setUpDocument() {
    if (this.#destroyed) {
      return;
    }

    const isSmartWindow = await this.sendQuery(
      "SmartFormFill:IsSmartWindow"
    ).catch(error => {
      if (!this.#destroyed) {
        lazy.console.error("Could not determine if in Smart Window", error);
      }

      return false;
    });

    if (!isSmartWindow || this.#destroyed) {
      return;
    }

    this.#smartFormFillDocument = new lazy.SmartFormFillDocument(this.document);
    await this.#smartFormFillDocument.initialize(
      this.#onFormUpdate.bind(this),
      this.#onFieldOutcomes.bind(this),
      this.#onFieldsFilled.bind(this)
    );
    this.#registerAutocompleteFields();
  }

  /**
   * Forwards parent messages to the document manager.
   *
   * @param {SmartFormFillMessage} message
   * @returns {Promise<FocusedForm | FillFormOperationResult | null |
   * undefined>}
   */
  async receiveMessage({ data, name }) {
    if (
      !this.#smartFormFillDocument &&
      this.document.readyState !== "loading"
    ) {
      await this.#prepareDocument();
    }

    if (!this.#smartFormFillDocument) {
      return null;
    }

    switch (name) {
      case "SmartFormFill:FillForm":
        return this.#smartFormFillDocument.fillForm(data);

      case "SmartFormFill:StopFilling":
        this.#smartFormFillDocument.stopFilling();
        return undefined;

      case "SmartFormFill:GetFocusedForm":
        return this.#smartFormFillDocument.getFocusedForm();

      case "SmartFormFill:RefreshAutocomplete":
        this.#refreshAutocomplete();
        return undefined;

      case "SmartFormFill:ShowAutocompletePopup": {
        this.#showAutocompletePopup();
        return undefined;
      }
    }

    return null;
  }

  /**
   * Destroys document state.
   */
  didDestroy() {
    this.#autocompletePopupFocusAbortController?.abort();
    this.#autocompletePopupFocusAbortController = null;
    this.#smartFormFillDocument?.destroy();
    this.#smartFormFillDocument = null;
    this.#documentPreparationPromise = null;
    this.#destroyed = true;
  }

  /**
   * Callback for SmartFormFillDocument to use when it
   * detects a form update
   *
   * @param {Array<FormData>} formDataList
   */
  #onFormUpdate(formDataList) {
    if (!formDataList || this.#destroyed) {
      return;
    }

    this.#registerAutocompleteFields();
    this.sendAsyncMessage("SmartFormFill:FormUpdate", formDataList);
  }

  /**
   * Reopens the autocomplete popup once focus has returned from a dialog.
   */
  #showAutocompletePopup() {
    if (this.#tryShowAutocompletePopup()) {
      return;
    }

    this.#autocompletePopupFocusAbortController?.abort();

    const abortController = new AbortController();
    this.#autocompletePopupFocusAbortController = abortController;

    const clearPendingAttempt = () => {
      abortController.abort();
      if (this.#autocompletePopupFocusAbortController === abortController) {
        this.#autocompletePopupFocusAbortController = null;
      }
    };

    const retryAfterFocus = () => {
      Services.tm.dispatchToMainThread(() => {
        if (abortController.signal.aborted || this.#destroyed) {
          return;
        }

        clearPendingAttempt();
        this.#tryShowAutocompletePopup();
      });
    };

    this.document.addEventListener("focusin", retryAfterFocus, {
      capture: true,
      once: true,
      signal: abortController.signal,
    });

    Services.tm.dispatchToMainThread(() => {
      if (abortController.signal.aborted || this.#destroyed) {
        return;
      }

      if (this.#tryShowAutocompletePopup()) {
        clearPendingAttempt();
      }
    });
  }

  /**
   * Attempts to open the autocomplete popup for the focused field.
   *
   * @returns {boolean} Whether the popup was opened.
   */
  #tryShowAutocompletePopup() {
    if (this.#destroyed || !this.#smartFormFillDocument) {
      return false;
    }

    let autocompleteActor;
    try {
      autocompleteActor = this.manager.getActor("AutoComplete");
    } catch {
      return false;
    }

    if (!autocompleteActor || autocompleteActor.popupOpen) {
      return false;
    }

    const focusedElement = this.document.activeElement;
    if (
      !this.#smartFormFillDocument.isSupportedField(focusedElement) ||
      lazy.formFillController.controlledElement !== focusedElement
    ) {
      return false;
    }

    lazy.formFillController.showPopup();
    return true;
  }

  /**
   * Refreshes autocomplete state after Smart Form Fill changes; reruns
   * the search if the popup is open, otherwise clears cached results
   * for the next open.
   */
  #refreshAutocomplete() {
    const focusedElement = this.document.activeElement;

    if (
      !this.#smartFormFillDocument?.isSupportedField(focusedElement) ||
      lazy.formFillController.controlledElement !== focusedElement
    ) {
      return;
    }

    const autocompleteInput = lazy.formFillController.QueryInterface(
      Ci.nsIAutoCompleteInput
    );
    const autocompleteController = autocompleteInput.controller;
    const autocompleteActor = this.manager.getActor("AutoComplete");

    if (!autocompleteActor?.popupOpen) {
      autocompleteController.resetInternalState();
      return;
    }

    autocompleteController.startSearch(autocompleteController.searchString);
  }

  /*
   * AutoComplete-related functions
   */

  /**
   * Registers each supported Smart Form Fill field with AutoCompleteChild.
   *
   * Registration allows the form-fill controller to start autocomplete searches
   * for the field and associates this actor with the field as a result provider.
   *
   * Called after document preparation and when tracked forms change.
   *
   * @private
   */
  #registerAutocompleteFields() {
    const autocompleteActor = this.manager.getActor("AutoComplete");
    if (!autocompleteActor) {
      return;
    }

    for (const field of this.#smartFormFillDocument.getSupportedFields()) {
      autocompleteActor.markAsAutoCompletableField(field, this);
    }
  }

  /**
   * Determines whether AutoCompleteChild should request Smart Form Fill results
   * for the focused input.
   *
   * AutoCompleteChild calls this before sending an autocomplete search to the
   * parent process.
   *
   * @param {HTMLInputElement | HTMLTextAreaElement} input
   *   The input associated with the autocomplete search.
   * @returns {boolean}
   *   Whether Smart Form Fill has an entry to offer for the input.
   */
  shouldSearchForAutoComplete(input) {
    return this.#smartFormFillDocument?.shouldOfferFill(input) ?? false;
  }

  /**
   * Provides the options object required by the autocomplete provider contract.
   *
   * @returns {object} Empty provider options.
   */
  getAutoCompleteSearchOption() {
    return {};
  }

  /**
   * Converts results returned by SmartFormFillParent into an
   * nsIAutoCompleteResult that AutoCompleteChild can give to the autocomplete
   * controller.
   *
   * AutoCompleteChild calls this after the parent-process search completes.
   *
   * @param {string} searchString
   *   The current value used for the autocomplete search.
   * @param {HTMLInputElement | HTMLTextAreaElement} input
   *   The input associated with the search.
   * @param {{ entries: Array<object> } | null | undefined} records
   *   Results returned by SmartFormFillParent.searchAutoCompleteEntries().
   * @returns {FormHistoryAutoCompleteResult | null}
   *   The autocomplete result, or null when no Smart Form Fill entry is available.
   */
  searchResultToAutoCompleteResult(searchString, input, records) {
    if (!records?.entries?.length) {
      return null;
    }

    const result = new lazy.FormHistoryAutoCompleteResult(
      input,
      [],
      input.name,
      searchString
    );
    result.externalEntries.push(...records.entries);
    return result;
  }

  /**
   * Name used by AutoCompleteChild to identify this autocomplete provider and
   * route searches to the corresponding parent actor.
   *
   * @returns {string}
   */
  get actorName() {
    return "SmartFormFill";
  }

  /**
   * Callback for SmartFormFillDocument to use when the fill of one or more
   * fields ends
   *
   * @param {FieldOutcomes} outcomes
   */
  #onFieldOutcomes(outcomes) {
    if (this.#destroyed) {
      return;
    }

    this.sendAsyncMessage("SmartFormFill:FieldOutcomes", outcomes);
  }

  /**
   * Reports which fields a fill wrote to, so the parent can record what the
   * model decided for each of them. Pushed rather than returned from the fill
   * itself: the page is the only side that knows what it wrote, and filling
   * stays a message the parent does not wait on.
   *
   * @param {{ id: string, fieldIds: Array<string> }} filled
   */
  #onFieldsFilled(filled) {
    if (this.#destroyed) {
      return;
    }

    this.sendAsyncMessage("SmartFormFill:FieldsFilled", filled);
  }
}
