/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddressResult: "resource://autofill/ProfileAutoCompleteResult.sys.mjs",
  AutofillFormFactory:
    "resource://gre/modules/shared/AutofillFormFactory.sys.mjs",
  AutofillTelemetry: "resource://gre/modules/shared/AutofillTelemetry.sys.mjs",
  CreditCardResult: "resource://autofill/ProfileAutoCompleteResult.sys.mjs",
  GenericAutocompleteItem: "resource://gre/modules/FillHelpers.sys.mjs",
  InsecurePasswordUtils: "resource://gre/modules/InsecurePasswordUtils.sys.mjs",
  FieldDetail: "resource://gre/modules/shared/FieldScanner.sys.mjs",
  FormAutofill: "resource://autofill/FormAutofill.sys.mjs",
  FormAutofillContent: "resource://autofill/FormAutofillContent.sys.mjs",
  FormAutofillHandler:
    "resource://gre/modules/shared/FormAutofillHandler.sys.mjs",
  FORM_CHANGE_REASON:
    "resource://gre/modules/shared/FormAutofillHandler.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  FormLikeFactory: "resource://gre/modules/FormLikeFactory.sys.mjs",
  FormScenarios: "resource://gre/modules/FormScenarios.sys.mjs",
  FormStateManager: "resource://gre/modules/shared/FormStateManager.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  FORM_SUBMISSION_REASON: "resource://gre/actors/FormHandlerChild.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

/**
 * Handles content's interactions for the frame.
 */
export class FormAutofillChild extends JSWindowActorChild {
  /**
   * Keep track of autofill handlers that are waiting for the parent process
   * to send back the identified result.
   */
  #handlerWaitingForDetectedComplete = new Set();

  /**
   * Keep track of handler that are waiting for the
   * notification to re-fill fields after a form change
   */
  #handlerWaitingForFillOnFormChangeComplete = new Set();

  /**
   * Keep track of handler that are waiting for the parent process
   * to complete the previous form submission action. This is needed
   * to prevent the field update heuristics from changing the detected field
   * details while the form submission heuristics is trying to capture them.
   */
  #handlerWaitingForFormSubmissionComplete = new Set();

  constructor() {
    super();

    this.log = lazy.FormAutofill.defineLogGetter(this, "FormAutofillChild");
    this.debug("init");

    this._hasDOMContentLoadedHandler = false;

    this._hasRegisteredPageHide = new Set();

    /**
     * @type {FormAutofillFieldDetailsManager} handling state management of current forms and handlers.
     */
    this._fieldDetailsManager = new lazy.FormStateManager(
      this.onFilledModified.bind(this)
    );

    /**
     * Tracks whether the last form submission was triggered by a form submit event,
     * if so we'll ignore the page navigation that follows
     */
    this.isFollowingSubmitEvent = false;
  }

  /**
   * After the parent process finishes classifying the fields, the parent process
   * informs all the child process of the classified field result. The child process
   * then sets the updated result to the corresponding AutofillHandler
   *
   * @param {Array<FieldDetail>} fieldDetails
   *        An array of the identified fields.
   * @param {boolean} isUpdate flags whether the field detection process
   *                           is run due to a form change
   */
  onFieldsDetectedComplete(fieldDetails, isUpdate = false) {
    if (!fieldDetails.length) {
      return;
    }

    const handler = this._fieldDetailsManager.getFormHandlerByRootElementId(
      fieldDetails[0].rootElementId
    );
    this.#handlerWaitingForDetectedComplete.delete(handler);

    if (isUpdate) {
      if (this.#handlerWaitingForFormSubmissionComplete.has(handler)) {
        // The form change was detected before the form submission, but was probably initiated
        // by it, so don't touch the fieldDetails in this case.
        return;
      }
      handler.updateFormByElement(fieldDetails[0].element);
      this._fieldDetailsManager.addFormHandlerByElementEntries(handler);
    }

    handler.setIdentifiedFieldDetails(fieldDetails);
    handler.setUpDynamicFormChangeObserver();

    let addressFields = [];
    let creditcardFields = [];

    handler.fieldDetails.forEach(fd => {
      if (lazy.FormAutofillUtils.isAddressField(fd.fieldName)) {
        addressFields.push(fd);
      } else if (lazy.FormAutofillUtils.isCreditCardField(fd.fieldName)) {
        creditcardFields.push(fd);
      }
    });

    // Bug 1905040. This is only a temporarily workaround for now to skip marking address fields
    // autocompletable whenever we detect an address field. We only mark address field when
    // it is a valid address section (This is done in the parent)
    const addressFieldSet = new Set(addressFields.map(fd => fd.fieldName));
    if (
      addressFieldSet.size < lazy.FormAutofillUtils.AUTOFILL_FIELDS_THRESHOLD
    ) {
      addressFields = [];
    }

    // Inform the autocomplete controller these fields are autofillable
    [...addressFields, ...creditcardFields].forEach(fieldDetail => {
      this.#markAsAutofillField(fieldDetail);

      if (
        fieldDetail.element == lazy.FormAutofillContent.focusedElement &&
        !isUpdate
      ) {
        this.showPopupIfEmpty(fieldDetail.element, fieldDetail.fieldName);
      }
    });

    if (isUpdate) {
      // The fields detection was re-run because of a form change, this means
      // FormAutofillChild already registered its interest in form submissions
      // in the initial field detection process
      return;
    }

    // Do not need to listen to form submission event because if the address fields do not contain
    // 'street-address' or `address-linx`, we will not save the address.
    if (
      creditcardFields.length ||
      (addressFields.length &&
        [
          "street-address",
          "address-line1",
          "address-line2",
          "address-line3",
        ].some(fieldName => addressFieldSet.has(fieldName)))
    ) {
      this.manager
        .getActor("FormHandler")
        .registerFormSubmissionInterest(this, {
          includesFormRemoval: lazy.FormAutofill.captureOnFormRemoval,
          includesPageNavigation: lazy.FormAutofill.captureOnPageNavigation,
        });

      // TODO (Bug 1901486): Integrate pagehide to FormHandler.
      if (!this._hasRegisteredPageHide.has(handler)) {
        this.registerPageHide(handler);
        this._hasRegisteredPageHide.add(true);
      }
    }
  }

  /**
   * Disconnect all remaining form change observer that are still set up
   * for the form that was submitted.
   *
   * @param {string} rootElementId
   */
  onFormSubmissionComplete(rootElementId) {
    const handler =
      this._fieldDetailsManager.getFormHandlerByRootElementId(rootElementId);
    handler.clearFormChangeObservers();
    this.#handlerWaitingForFormSubmissionComplete.delete(handler);
  }

  /**
   * Filling the fields again, because a form change was detected by this or
   * another FormAutofillChild immediately after an autocompletion process
   * (see handler.fillOnFormChangeData.isWithinDynamicFormChangeThreshold).
   *
   * @param {string} focusedId  element id of focused element that triggered
   *                           the initial autocompletion process
   * @param {Array<string>} ids element ids of detected fields that will be filled
   * @param {object} profile profile that was used on first autcompletion process
   *
   * @returns {object} filled fields
   */
  fillFieldsOnFormChange(focusedId, ids, profile) {
    const result = this.fillFields(focusedId, ids, profile, true);

    const handler = this.#getHandlerByElementId(ids[0]);
    this.#handlerWaitingForFillOnFormChangeComplete.delete(handler);

    return result;
  }

  /**
   * Identifies elements that are in the associated form of the passed element.
   *
   * @param {Element} element
   *        The element to be identified.
   *
   * @returns {FormAutofillHandler}
   *        The autofill handler instance for the form that is associated with the
   *        passed element.
   */
  identifyFieldsWhenFocused(element) {
    this.debug(
      `identifyFieldsWhenFocused: ${element.ownerDocument.location?.hostname}`
    );

    const handler = this._fieldDetailsManager.getOrCreateFormHandler(element);

    if (
      this.#handlerWaitingForDetectedComplete.has(handler) ||
      this.#handlerWaitingForFillOnFormChangeComplete.has(handler) ||
      this.#handlerWaitingForFormSubmissionComplete.has(handler)
    ) {
      // Bail out if the child process is still waiting for the parent to send a
      // `onFieldsDetectedComplete` or `onFieldsUpdatedComplete` message,
      // or a form submission is currently still getting processed.
      return;
    }

    if (handler.fillOnFormChangeData.isWithinDynamicFormChangeThreshold) {
      // Received the focus event immediately after an autofill action, which was not
      // initiated by a user but by the site due to the form change. Bail out here,
      // because we will receive the form-changed-event anyway and should not process the
      // field detection here, since this would block the second autofill process.
      return;
    }

    // Bail out if there is nothing changed since last time we identified this element
    // or there is no interested fields.
    if (handler.hasIdentifiedFields() && !handler.updateFormIfNeeded(element)) {
      // This is for testing purposes only. It sends a notification to indicate that the
      // form has been identified and is ready to open the popup.
      // If new fields are detected, the message will be sent to the parent
      // once the parent finishes collecting information from sub-frames if they exist.
      this.sendAsyncMessage("FormAutofill:FieldsIdentified");

      const fieldName =
        handler.getFieldDetailByElement(element)?.fieldName ?? "";
      this.showPopupIfEmpty(element, fieldName);
    } else {
      const includeIframe = this.browsingContext == this.browsingContext.top;
      let detectedFields = lazy.FormAutofillHandler.collectFormFieldDetails(
        handler.form,
        includeIframe
      );

      // If none of the detected fields are credit card or address fields,
      // there's no need to notify the parent because nothing will change.
      if (
        !detectedFields.some(
          fd =>
            lazy.FormAutofillUtils.isCreditCardField(fd.fieldName) ||
            lazy.FormAutofillUtils.isAddressField(fd.fieldName)
        )
      ) {
        handler.setIdentifiedFieldDetails(detectedFields);
        return;
      }

      this.sendAsyncMessage(
        "FormAutofill:OnFieldsDetected",
        detectedFields.map(field => field.toVanillaObject())
      );

      // Notify the parent about the newly identified fields because
      // the autofill section information is maintained on the parent side.
      this.#handlerWaitingForDetectedComplete.add(handler);
    }
  }

  /**
   * This function is called by the parent when a field is detected in another
   * frame. The parent uses this function to collect field information from frames
   * that are part of the same form as the detected field.
   *
   * @param {string} focusedBCId
   *        The browsing context ID of the top-level iframe
   *        that contains the detected field.
   *        Note that this value is set only when the current frame is the top-level.
   *
   * @returns {Array}
   *        Array of FieldDetail objects of identified fields (including iframes).
   */
  identifyFields(focusedBCId) {
    const isTop = this.browsingContext == this.browsingContext.top;

    let element;
    if (isTop) {
      // Find the focused iframe
      element = BrowsingContext.get(focusedBCId).embedderElement;
    } else {
      // Ignore form as long as the frame is not the top-level, which means
      // we can just pick any of the eligible elements to identify.
      element = lazy.FormAutofillUtils.queryEligibleElements(
        this.document,
        true
      )[0];
    }

    if (!element) {
      return [];
    }

    const handler = this._fieldDetailsManager.getOrCreateFormHandler(element);

    // We don't have to call 'updateFormIfNeeded' like we do in
    // 'identifyFieldsWhenFocused' because 'collectFormFieldDetails' doesn't use cached
    // result.
    const includeIframe = isTop;
    const detectedFields = lazy.FormAutofillHandler.collectFormFieldDetails(
      handler.form,
      includeIframe
    );

    if (detectedFields.length) {
      // This actor should receive `onFieldsDetectedComplete`message after
      // `idenitfyFields` is called
      this.#handlerWaitingForDetectedComplete.add(handler);
    }
    return detectedFields;
  }

  showPopupIfEmpty(element, fieldName) {
    if (element?.value?.length !== 0) {
      this.debug(`Not opening popup because field is not empty.`);
      return;
    }

    if (fieldName.startsWith("cc-") || AppConstants.platform === "android") {
      lazy.FormAutofillContent.showPopup();
    }
  }

  /**
   * We received a form-submission-detected event because
   * the page was navigated.
   */
  onPageNavigation() {
    if (!lazy.FormAutofill.captureOnPageNavigation) {
      return;
    }

    if (this.isFollowingSubmitEvent) {
      // The next page navigation should be handled as form submission again
      this.isFollowingSubmitEvent = false;
      return;
    }

    const formSubmissionReason = lazy.FORM_SUBMISSION_REASON.PAGE_NAVIGATION;
    const weakIdentifiedForms =
      this._fieldDetailsManager.getWeakIdentifiedForms();

    for (const form of weakIdentifiedForms) {
      // Disconnected forms are captured by the form removal heuristic
      if (!form.isConnected) {
        continue;
      }
      this.formSubmitted(form, formSubmissionReason);
    }
  }

  /**
   * We received a form-submission-detected event because
   * a form was removed from the DOM after a successful
   * xhr/fetch request
   *
   * @param {Event} form form to be submitted
   */
  onFormRemoval(form) {
    if (!lazy.FormAutofill.captureOnFormRemoval) {
      return;
    }

    const formSubmissionReason =
      lazy.FORM_SUBMISSION_REASON.FORM_REMOVAL_AFTER_FETCH;
    this.formSubmitted(form, formSubmissionReason);
    this.manager.getActor("FormHandler").unregisterFormRemovalInterest(this);
  }

  registerPageHide(handler) {
    // Check whether the section is in an <iframe>; and, if so,
    // watch for the <iframe> to pagehide.
    if (this.browsingContext != this.browsingContext.top) {
      this.debug(
        "Address/Credit card form is in an iframe -- watching for pagehide"
      );
      handler.window.addEventListener(
        "pagehide",
        () => {
          this.debug("Credit card subframe is pagehiding", handler.form);

          const reason = lazy.FORM_SUBMISSION_REASON.IFRAME_PAGEHIDE;
          this.formSubmitted(handler.form.rootElement, reason, handler);
          this._hasRegisteredPageHide.delete(handler);
        },
        { once: true }
      );
    }
  }

  shouldIgnoreFormAutofillEvent(event) {
    if (!event.isTrusted) {
      return true;
    }

    if (
      !lazy.FormAutofill.isAutofillCreditCardsAvailable &&
      !lazy.FormAutofill.isAutofillAddressesAvailable
    ) {
      return true;
    }

    const nodePrincipal = event.target.nodePrincipal;
    return nodePrincipal.isSystemPrincipal || nodePrincipal.schemeIs("about");
  }

  handleEvent(evt) {
    if (
      !lazy.FormAutofill.isAutofillEnabled ||
      this.shouldIgnoreFormAutofillEvent(evt)
    ) {
      return;
    }

    if (!this.windowContext) {
      // !this.windowContext must not be null, because we need the
      // windowContext and/or docShell to (un)register form submission listeners
      return;
    }

    switch (evt.type) {
      case "focusin": {
        this.onFocusIn(evt.target);
        break;
      }
      case "form-changed": {
        const { form, changes } = evt.detail;
        this.onFormChange(form, changes);
        break;
      }
      case "form-submission-detected": {
        const { form, reason } = evt.detail;
        this.onFormSubmission(form, reason);
        break;
      }

      default: {
        throw new Error("Unexpected event type");
      }
    }
  }

  onFocusIn(element) {
    const handler = this._fieldDetailsManager.getFormHandler(element);
    // When autofilling and clearing a field, we focus on the element before modifying the value.
    // (See FormAutofillHandler.fillFieldValue and FormAutofillHandler.clearFilledFields).
    // We ignore the focus event for those case to avoid showing popup while autofilling or clearing.
    if (
      !lazy.FormAutofillUtils.isCreditCardOrAddressFieldType(element) ||
      handler?.isAutofillInProgress
    ) {
      return;
    }

    const doc = element.ownerDocument;
    if (doc.readyState === "loading") {
      // For auto-focused input, we might receive focus event before document becomes ready.
      // When this happens, run field identification after receiving `DOMContentLoaded` event
      if (!this._hasDOMContentLoadedHandler) {
        this._hasDOMContentLoadedHandler = true;
        doc.addEventListener(
          "DOMContentLoaded",
          () => this.onFocusIn(lazy.FormAutofillContent.focusedElement),
          { once: true }
        );
      }
      return;
    }

    if (
      AppConstants.MOZ_GECKOVIEW ||
      !lazy.FormAutofillContent.savedFieldNames
    ) {
      this.debug("onFocusIn: savedFieldNames are not known yet");

      // Init can be asynchronous because we don't need anything from the parent
      // at this point.
      this.sendAsyncMessage("FormAutofill:InitStorage");
    }

    this.identifyFieldsWhenFocused(element);
  }

  /**
   * A "form-changed" event was dispatched, because the observed document/form
   * added or removed child nodes or an observed element changed its visibility state.
   * A new field detection process will be initiated in the parent, if the collected fieldDetails
   * from the current form/document differ from the previous state.
   *
   * @param {HTMLFormElement|HTMLDocument} form or document (if form-less) that contains the
   *                                            elements that were added/removed/became (in-)visible
   * @param {object} changes Change details keyed by lazy.FORM_CHANGE_REASON:
   *                          - NODES_ADDED: HTMLElement[] - nodes added
   *                          - NODES_REMOVED: HTMLElement[] - nodes removed
   *                          - ELEMENT_VISIBLE: HTMLElement[] - elements that became visible
   *                          - ELEMENT_INVISIBLE: HTMLElement[] - elements that became invisible
   *                          - SELECT_OPTIONS_CHANGED: HTMLSelectElement[] - select elements with modified options
   *                          A form-change event is single-reasoned for visibility changes and can be multi-reasoned for mutations.
   */
  async onFormChange(form, changes) {
    if (!lazy.FormAutofill.detectDynamicFormChanges) {
      return;
    }

    this.debug(
      `Handling form change - infered by reason(s): ${Object.keys(changes)}`
    );

    // Ignore "form-changed" events with reason "visibile-element-became-invisible" if
    // the affected element is disconnected. This element change is already handled by a
    // "form-changed" event with reason "nodes-removed".
    const invisibleElement =
      changes[lazy.FORM_CHANGE_REASON.ELEMENT_INVISIBLE]?.[0];
    if (invisibleElement && !invisibleElement.isConnected) {
      return;
    }

    const formRootElementId = lazy.FormAutofillUtils.getElementIdentifier(form);
    const handler =
      this._fieldDetailsManager.getFormHandlerByRootElementId(
        formRootElementId
      );

    if (
      this.#handlerWaitingForFormSubmissionComplete.has(handler) ||
      !form.isConnected ||
      (HTMLFormElement.isInstance(form) &&
        !Array.from(form.elements).find(e => e.checkVisibility()))
    ) {
      // Bail out if a form submission is happening, or the whole form is disconnected or invisible,
      // because then we're suspecting a form submission of reason "form-removal-after-fetch" next.
      // In these cases avoid updating the fieldDetails since the form submission heuristics rely on them
      return;
    }

    // Not resetting the field state for elements that became invisible because the handler
    // keeps tracking them if they were previously autocompleted. Their field state
    // will be updated on a clearing action
    const removedElements = changes[lazy.FORM_CHANGE_REASON.NODES_REMOVED];
    removedElements?.forEach(element => {
      handler.resetFieldStateWhenRemoved(element);
    });

    if (
      this.#handlerWaitingForDetectedComplete.has(handler) ||
      this.#handlerWaitingForFillOnFormChangeComplete.has(handler)
    ) {
      // The child is still waiting for the parent to complete
      // a previous fields detection or a previous re-filling on form change.
      return;
    }

    let currentForm;
    if (HTMLFormElement.isInstance(form)) {
      currentForm = lazy.AutofillFormFactory.createFromForm(form);
    }

    if (!currentForm) {
      const findAnchorElement = elements => {
        return elements.find(
          element =>
            element.isConnected &&
            // createFromField needs an input, select or iframe element
            (lazy.FormAutofillUtils.isCreditCardOrAddressFieldType(element) ||
              HTMLIFrameElement.isInstance(element))
        );
      };

      let anchorElement = findAnchorElement(handler.form.elements);

      if (!anchorElement) {
        // Unable to find an anchor element under the previously tracked fields.
        // So going over the elements that were added/became visible on form change
        const addedElements = changes[lazy.FORM_CHANGE_REASON.ADDED_NODES];
        const visibleElements =
          changes[lazy.FORM_CHANGE_REASON.ELEMENT_VISIBLE];
        anchorElement = findAnchorElement([addedElements, visibleElements]);
      }
      if (anchorElement) {
        currentForm = lazy.AutofillFormFactory.createFromField(anchorElement);
      } else {
        currentForm = lazy.AutofillFormFactory.createFromDocumentRoot(form);
      }
    }

    const currentFields =
      lazy.FormAutofillHandler.collectFormFieldDetails(currentForm);

    const changedSelectElements =
      changes[lazy.FORM_CHANGE_REASON.SELECT_OPTIONS_CHANGED];
    if (
      currentFields.length == handler.fieldDetails.length &&
      currentFields.every(
        (field, idx) => field.element === handler.fieldDetails[idx].element
      ) &&
      !changedSelectElements.length
    ) {
      // All detected fields (including options from detected <select> fields)
      // remain unchanged, so don't notify the parent and the subtree children
      return;
    }

    // Merging previous fields with current fields to preserve the previous element ids
    // which are needed for the parent to not capture duplicates in filledResult.
    const mergedFields = currentFields.map(currentField => {
      const prevField = handler.getFieldDetailByElement(currentField.element);
      return prevField ?? currentField;
    });

    this._fieldDetailsManager.removeFormHandlerByElementEntries(handler);

    this.sendAsyncMessage(
      "FormAutofill:OnFieldsUpdated",
      mergedFields.map(field => field.toVanillaObject())
    );

    this.#handlerWaitingForDetectedComplete.add(handler);

    if (
      lazy.FormAutofill.fillOnDynamicFormChanges &&
      handler.fillOnFormChangeData.isWithinDynamicFormChangeThreshold &&
      !this.#handlerWaitingForFillOnFormChangeComplete.has(handler)
    ) {
      const previouslyFocusedId =
        handler.fillOnFormChangeData.previouslyFocusedId;
      const prevFocusedElement =
        lazy.FormAutofillUtils.getElementByIdentifier(previouslyFocusedId);

      // Determine the element that will be re-focused after the autofill action (ideally that
      // should be the previously focused one). It is also used by the parent to retrieve the section that should be filled.
      // If the previous one was removed in the form change, then just take the first element of the newly detected fields.
      const elementToFocus = prevFocusedElement.isConnected
        ? previouslyFocusedId
        : mergedFields[0].elementId;

      this.#handlerWaitingForFillOnFormChangeComplete.add(handler);
      this.sendAsyncMessage("FormAutofill:FieldsUpdatedDuringAutofill", {
        elementId: elementToFocus,
        profile: handler.fillOnFormChangeData.previouslyUsedProfile,
      });
    }
  }

  /**
   * Handle form-submission-detected event (dispatched by FormHandlerChild)
   *
   * Depending on the heuristic that detected the form submission,
   * the form that is submitted is retrieved differently
   *
   * @param {HTMLFormElement} form that is being submitted
   * @param {string} reason heuristic that detected the form submission
   *                        (see FormHandlerChild.FORM_SUBMISSION_REASON)
   */
  onFormSubmission(form, reason) {
    switch (reason) {
      case lazy.FORM_SUBMISSION_REASON.PAGE_NAVIGATION:
        this.onPageNavigation();
        break;
      case lazy.FORM_SUBMISSION_REASON.FORM_SUBMIT_EVENT:
        this.formSubmitted(form, reason);
        break;
      case lazy.FORM_SUBMISSION_REASON.FORM_REMOVAL_AFTER_FETCH:
        this.onFormRemoval(form);
        break;
    }
  }

  async receiveMessage(message) {
    switch (message.name) {
      case "FormAutofill:FillFields": {
        const { focusedId, ids, profile } = message.data;

        // Retrieving the handler before filling,
        // since the filling could trigger a form change and fields removal
        const handler = this.#getHandlerByElementId(focusedId);
        const result = this.fillFields(focusedId, ids, profile);
        this.prepareFillingFieldsOnFormChange(handler, focusedId, ids, profile);

        // Return the autofilled result to the parent. The result
        // is used by both tests and telemetry.
        return result;
      }
      case "FormAutofill:FillFieldsOnFormChange": {
        const { focusedId, ids, profile } = message.data;
        // Site might clear some fields on form change, so let any other task complete first.
        return new Promise(resolve => {
          lazy.setTimeout(() => {
            const result = this.fillFieldsOnFormChange(focusedId, ids, profile);
            // Not preparing for another filling on form change to avoid infinite loops
            resolve(result);
          }, 0);
        });
      }
      case "FormAutofill:ClearFilledFields": {
        const { focusedId, ids } = message.data;
        this.clearFields(focusedId, ids);
        break;
      }
      case "FormAutofill:PreviewFields": {
        const { ids, profile } = message.data;
        const handler = this.#getHandlerByElementId(ids[0]);

        if (profile) {
          handler?.previewFields(ids, profile);
        } else {
          handler?.clearPreviewedFields(ids);
        }
        break;
      }
      case "FormAutofill:IdentifyFields": {
        const { focusedBCId } = message.data ?? {};
        return this.identifyFields(focusedBCId).map(fieldDetail =>
          fieldDetail.toVanillaObject()
        );
      }
      case "FormAutofill:GetFilledInfo": {
        const { rootElementId } = message.data;
        const handler =
          this._fieldDetailsManager.getFormHandlerByRootElementId(
            rootElementId
          );
        return handler?.collectFormFilledData();
      }
      case "FormAutofill:InspectFields": {
        const fieldDetails = this.inspectFields();
        return fieldDetails.map(field => field.toVanillaObject());
      }
      case "FormAutofill:onFieldsDetectedComplete": {
        const { fds } = message.data;
        const fieldDetails = fds.map(fd =>
          lazy.FieldDetail.fromVanillaObject(fd)
        );
        this.onFieldsDetectedComplete(fieldDetails);
        break;
      }
      case "FormAutofill:onFieldsUpdatedComplete": {
        const { fds } = message.data;
        const fieldDetails = fds.map(fd =>
          lazy.FieldDetail.fromVanillaObject(fd)
        );
        const isUpdate = true;
        this.onFieldsDetectedComplete(fieldDetails, isUpdate);
        break;
      }
      case "FormAutofill:onFormSubmissionComplete": {
        const { rootElementId } = message.data;
        this.onFormSubmissionComplete(rootElementId);
        break;
      }
    }
    return true;
  }

  /**
   * Handle a form submission and early return when:
   * 1. In private browsing mode.
   * 2. Could not map any autofill handler by form element.
   * 3. Number of filled fields is less than autofill threshold
   *
   * @param {HTMLElement} formElement Root element which receives submit event.
   * @param {string} formSubmissionReason Reason for invoking the form submission
   *                 (see options for FORM_SUBMISSION_REASON in FormAutofillUtils))
   * @param {object} handler FormAutofillHander, if known by caller
   */
  formSubmitted(formElement, formSubmissionReason, handler = undefined) {
    this.debug(`Handling form submission - infered by ${formSubmissionReason}`);

    lazy.AutofillTelemetry.recordFormSubmissionHeuristicCount(
      formSubmissionReason
    );

    if (!lazy.FormAutofill.isAutofillEnabled) {
      this.debug("Form Autofill is disabled");
      return;
    }

    // The `domWin` truthiness test is used by unit tests to bypass this check.
    const domWin = formElement.ownerGlobal;
    if (!domWin) {
      return;
    }

    if (lazy.PrivateBrowsingUtils.isContentWindowPrivate(domWin)) {
      this.debug("Ignoring submission in a private window");
      return;
    }

    handler = handler || this._fieldDetailsManager.getFormHandler(formElement);
    if (!handler) {
      this.debug("Form element could not map to an existing handler");
      return;
    }

    if (this.#handlerWaitingForFormSubmissionComplete.has(handler)) {
      return;
    }

    const formFilledData = handler.collectFormFilledData();
    if (!formFilledData) {
      this.debug("Form handler could not obtain filled data");
      return;
    }

    // After a form submit event follows (most likely) a page navigation, so we set this flag
    // to not handle the following one as form submission in order to avoid re-submitting the same form.
    // Ideally, we should keep a record of the last submitted form details and based on that we
    // should decide if we want to submit a form (bug 1895437)
    this.isFollowingSubmitEvent = true;

    this.sendAsyncMessage("FormAutofill:OnFormSubmit", {
      rootElementId: handler.rootElementId,
      formFilledData,
    });

    this.#handlerWaitingForFormSubmissionComplete.add(handler);
  }

  /**
   * This is called by FormAutofillHandler
   */
  onFilledModified(fieldDetail, previousState, newState) {
    const element = fieldDetail.element;
    if (lazy.FormAutofillUtils.isTextControl(element)) {
      // If the user manually blanks a credit card field, then
      // we want the popup to be activated.
      if (
        lazy.FormAutofillUtils.isCreditCardField(fieldDetail.fieldName) &&
        element.value === ""
      ) {
        lazy.FormAutofillContent.showPopup();
      }
    }

    if (
      previousState == lazy.FormAutofillUtils.FIELD_STATES.AUTO_FILLED &&
      newState == lazy.FormAutofillUtils.FIELD_STATES.NORMAL
    ) {
      this.sendAsyncMessage(
        "FormAutofill:FieldFilledModified",
        fieldDetail.elementId
      );
    }
  }

  clearFields(focusedId, elementIds) {
    const handler = this.#getHandlerByElementId(elementIds[0]);
    handler?.clearFilledFields(focusedId, elementIds);

    // Explicitly calling showPopupIfEmpty here, because FormAutofillChild is ignoring
    // all focus events during the autofilling/clearing process.
    const focusedElement =
      lazy.FormAutofillUtils.getElementByIdentifier(focusedId);
    const fieldName =
      handler.getFieldDetailByElement(focusedElement)?.fieldName ?? "";
    this.showPopupIfEmpty(focusedElement, fieldName);
  }

  async fillFields(focusedId, elementIds, profile) {
    let result = new Map();
    let handler;
    try {
      Services.obs.notifyObservers(null, "autofill-fill-starting");
      handler = this.#getHandlerByElementId(elementIds[0]);
      handler.fillFields(focusedId, elementIds, profile);

      // Return the autofilled result to the parent. The result
      // is used by both tests and telemetry.
      result = handler.collectFormFilledData();

      Services.obs.notifyObservers(null, "autofill-fill-complete");
    } catch {}

    return result;
  }

  /**
   * Caches necessary data in handler.fillOnFormChangeData in order to fill any fields that
   * are additonally detected after a form changed dynamically. This data is cleared after
   * a predefined timeout threshold (see lazy.FormAutofill.fillOnDynamicFormChangeTimeout).
   * The timeout gets cancelled early and the data cleared if a "click" or "keydown" event
   * is dispatched on the form.
   */
  prepareFillingFieldsOnFormChange(handler, focusedId, elementIds, profile) {
    if (!lazy.FormAutofill.fillOnDynamicFormChanges) {
      return;
    }

    // TODO bug 1953231:
    // FormAutofillParent should keep of which profile is used for which section, e.g. by introducing profile
    // ids. It's not ideal that we are cachine the whole used profile data in the child and then send it back to
    // the parent when filling after a form change. The parent should let the child know what profile to use.
    handler.fillOnFormChangeData.previouslyUsedProfile = profile;
    handler.fillOnFormChangeData.previouslyFocusedId = focusedId;
    handler.fillOnFormChangeData.isWithinDynamicFormChangeThreshold = true;

    const clearFillOnFormChangeTimeoutID = lazy.setTimeout(
      () => {
        handler.clearFillOnFormChangeData();
        try {
          userActedEvents.forEach(event => {
            handler.form.rootElement.removeEventListener(
              event,
              onUserInteractionListener
            );
          });
        } catch (e) {
          // handler.form.rootElement might already be a dead object by now
        }
      },
      // Note: The longer the timeout, the higher the possibility that all dynamic form
      //       changes have occured. Default timeout is 1000ms and should not be increased
      //       to avoid accidentially filling on non-script/user actions.
      lazy.FormAutofill.fillOnDynamicFormChangeTimeout
    );

    const onUserInteractionListener = () => {
      // User interacted with the form after it was filled
      lazy.clearTimeout(clearFillOnFormChangeTimeoutID);
      handler.clearFillOnFormChangeData();
    };
    const userActedEvents = ["click", "keydown"];
    userActedEvents.forEach(event => {
      handler.form.rootElement.addEventListener(
        event,
        onUserInteractionListener,
        { once: true }
      );
    });
  }

  /**
   * Returns all the identified fields for this document.
   * This function is only used by the autofill developer tool extension.
   */
  inspectFields() {
    const isTop = this.browsingContext == this.browsingContext.top;
    const elements = lazy.FormAutofillUtils.queryEligibleElements(
      this.document,
      isTop
    );

    // Unlike the case when users click on a field and we only run our heuristic
    // on fields within the same form as the focused field, for inspection,
    // we want to inspect all the forms in this page.
    const roots = new Set();
    let fieldDetails = [];
    for (const element of elements) {
      const formLike = lazy.FormLikeFactory.createFromField(element);
      if (roots.has(formLike.rootElement)) {
        continue;
      }
      roots.add(formLike.rootElement);
      const handler = new lazy.FormAutofillHandler(formLike);

      // Fields that cannot be recognized will still be reported with this API.
      const includeIframe = isTop;
      const fields = lazy.FormAutofillHandler.collectFormFieldDetails(
        handler.form,
        includeIframe,
        false
      );
      fieldDetails.push(...fields);
    }

    // The 'fieldDetails' array are grouped by form so might not follow their
    // order in the DOM tree. We rebuild the array based on their order in
    // the document.
    fieldDetails = elements
      .map(element => fieldDetails.find(field => field.element == element))
      .filter(field => !!field && field.element);

    // Add a data attribute with a unique identifier to allow the inspector
    // to link the element with its associated 'FieldDetail' information.
    for (const fd of fieldDetails) {
      const INSPECT_ATTRIBUTE = "data-moz-autofill-inspect-id";
      fd.inspectId = fd.element.getAttribute(INSPECT_ATTRIBUTE);
    }

    return fieldDetails;
  }

  #markAsAutofillField(fieldDetail) {
    const element = fieldDetail.element;

    // Since Form Autofill popup is only for input element, any non-Input
    // element should be excluded here.
    if (!lazy.FormAutofillUtils.isTextControl(element)) {
      return;
    }

    this.manager
      .getActor("AutoComplete")
      ?.markAsAutoCompletableField(element, this);
  }

  get actorName() {
    return "FormAutofill";
  }

  /**
   * Get the search options when searching for autocomplete entries in the parent
   *
   * @param {HTMLElement} input - The input or textarea element to search for autocomplete entries
   * @returns {object} the search options for the input
   */
  getAutoCompleteSearchOption(input) {
    const fieldDetail = this._fieldDetailsManager
      .getFormHandler(input)
      ?.getFieldDetailByElement(input);

    const scenarioName = lazy.FormScenarios.detect({ input }).signUpForm
      ? "SignUpFormScenario"
      : "";
    return {
      fieldName: fieldDetail?.fieldName,
      elementId: fieldDetail?.elementId,
      scenarioName,
    };
  }

  /**
   * Ask the provider whether it might have autocomplete entry to show
   * for the given input.
   *
   * @param {HTMLElement} input - The input or textarea element to search for autocomplete entries
   * @returns {boolean} true if we shold search for autocomplete entries
   */
  shouldSearchForAutoComplete(input) {
    const fieldDetail = this._fieldDetailsManager
      .getFormHandler(input)
      ?.getFieldDetailByElement(input);
    if (!fieldDetail) {
      return false;
    }
    const fieldName = fieldDetail.fieldName;
    const isAddressField = lazy.FormAutofillUtils.isAddressField(fieldName);
    const searchPermitted = isAddressField
      ? lazy.FormAutofill.isAutofillAddressesEnabled
      : lazy.FormAutofill.isAutofillCreditCardsEnabled;
    // If the specified autofill feature is pref off, do not search
    if (!searchPermitted) {
      return false;
    }

    // No profile can fill the currently-focused input.
    if (!lazy.FormAutofillContent.savedFieldNames.has(fieldName)) {
      return false;
    }

    return true;
  }

  /**
   * Convert the search result to autocomplete results
   *
   * @param {string} searchString - The string to search for
   * @param {HTMLElement} input - The input or textarea element to search for autocomplete entries
   * @param {Array<object>} records - autocomplete records
   * @returns {AutocompleteResult}
   */
  searchResultToAutoCompleteResult(searchString, input, records) {
    if (!records) {
      return null;
    }

    const handler = this._fieldDetailsManager.getFormHandler(input);
    const fieldDetail = handler?.getFieldDetailByElement(input);
    if (!fieldDetail) {
      return null;
    }

    const adaptedRecords = handler.getAdaptedProfiles(records.records);
    const isSecure = lazy.InsecurePasswordUtils.isFormSecure(handler.form);
    const isInputAutofilled =
      input.autofillState == lazy.FormAutofillUtils.FIELD_STATES.AUTO_FILLED;

    let AutocompleteResult;

    // TODO: This should be calculated in the parent
    // The field categories will be filled if the corresponding profile is
    // used for autofill. We don't display this information for credit
    // cards, so this is only calculated for address fields.
    let fillCategories;
    if (lazy.FormAutofillUtils.isAddressField(fieldDetail.fieldName)) {
      AutocompleteResult = lazy.AddressResult;
      fillCategories = adaptedRecords.map(profile => {
        const fields = Object.keys(profile).filter(fieldName => {
          const detail = handler.getFieldDetailByName(fieldName);
          return detail ? handler.isFieldAutofillable(detail, profile) : false;
        });
        return lazy.FormAutofillUtils.getCategoriesFromFieldNames(fields);
      });
    } else {
      AutocompleteResult = lazy.CreditCardResult;
    }

    const acResult = new AutocompleteResult(
      searchString,
      fieldDetail,
      records.allFieldNames,
      adaptedRecords,
      fillCategories,
      { isSecure, isInputAutofilled }
    );

    const externalEntries = records.externalEntries;

    acResult.externalEntries.push(
      ...externalEntries.map(
        entry =>
          new lazy.GenericAutocompleteItem(
            entry.image,
            entry.label,
            entry.secondary,
            entry.fillMessageName,
            entry.fillMessageData
          )
      )
    );

    return acResult;
  }

  #getHandlerByElementId(elementId) {
    const element = lazy.FormAutofillUtils.getElementByIdentifier(elementId);
    return this._fieldDetailsManager.getFormHandler(element);
  }
}
