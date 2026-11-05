/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  FORM_REVIEW_ERRORS:
    "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs",
  GetPageContent: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  MAX_SELECTED_TABS:
    "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  NonPrivateTabs: "resource:///modules/OpenTabs.sys.mjs",
  SmartFormFillController:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillController.sys.mjs",
  SmartFormFillReviewSession:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillReviewSession.sys.mjs",
  SmartFormFillAutocomplete:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillAutocomplete.sys.mjs",
  SmartFormFillTelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillTelemetry.sys.mjs",
  SOURCE_EDITOR_RESULT:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillTelemetry.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({ logModule: "SmartFormFillParent" });
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "disallowedRegions",
  "browser.smartwindow.smartformfill.disallowedRegions",
  ""
);

/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillController.sys.mjs").SmartFormFillController} Controller */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillController.sys.mjs").FillFormResult} FillFormResult */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillController.sys.mjs").SelectedTab} SelectedTab */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FormData} FormData */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillDocument.sys.mjs").FocusedForm} FocusedForm  */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").PageInfo} PageInfo */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillAutocomplete.sys.mjs").SmartFormFillAutocompleteSource} SmartFormFillAutocompleteSource */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewField} FormReviewField */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewFillResult} FormReviewFillResult */
/** @typedef {import("chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs").FormReviewGenerationResult} FormReviewGenerationResult */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillReviewSession.sys.mjs").SmartFormFillReviewSession} SmartFormFillReviewSession */

/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").ClassificationResponse} ClassificationResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").ClassifyFieldsRequestBody} ClassifyFieldsRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").GenerateFormValuesRequestBody} GenerateFormValuesRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").GenerateFormValuesResponse} GenerateFormValuesResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTabRequestBody} RelevantTabRequestBody */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").RelevantTabsResponse} RelevantTabsResponse */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldData} FieldData */
/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldClassification} FieldClassification */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillTelemetry.sys.mjs").FieldDecision} FieldDecision */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillTelemetry.sys.mjs").ModelInfo} ModelInfo */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillTelemetry.sys.mjs").RequestFlow} RequestFlow */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillTelemetry.sys.mjs").SourceEditorState} SourceEditorState */

/**
 * What a generation round produced, beyond the values the page is asked to
 * fill.
 *
 * @typedef {{
 *   formId: string,
 *   fieldsFilled: number,
 *   response: GenerateFormValuesResponse,
 *   fields: Array<FieldData>,
 *   formFields: Array<FieldData>,
 *   classifications: Map<string, FieldClassification>,
 *   tokensByFieldId: Map<string, string>,
 *   similarityByMemory: Map<string, number>,
 * }} GenerationResult
 */

/**
 * How the controller reports the requests it makes. A dispatch callback answers
 * with the flow the request belongs to, which is handed back on the matching
 * answer or failure so the two can be paired. The controller treats it as
 * opaque, so resolving flow ids stays here.
 *
 * @typedef {{
 *   onRelevantTabsDispatched: (formId: string, request: RelevantTabRequestBody, modelInfo: ModelInfo) => RequestFlow,
 *   onRelevantTabsAnswered: (flow: RequestFlow, response: RelevantTabsResponse, tabsUsed: number) => void,
 *   onRelevantTabsFailed: (flow: RequestFlow, error: Error) => void,
 *   onClassifyDispatched: (formId: string, request: ClassifyFieldsRequestBody, modelInfo: ModelInfo) => RequestFlow,
 *   onClassifyAnswered: (flow: RequestFlow, response: ClassificationResponse) => void,
 *   onClassifyFailed: (flow: RequestFlow, error: Error) => void,
 *   onGenerateDispatched: (formId: string, request: GenerateFormValuesRequestBody, dispatch: object) => RequestFlow,
 *   onGenerateAnswered: (flow: RequestFlow, result: GenerationResult) => void,
 *   onGenerateFailed: (flow: RequestFlow, error: Error) => void,
 * }} RequestObserver
 */

/**
 * @typedef {"idle" | "loading" | "ready" | "failed"} MetadataStatus
 */

/**
 * @typedef {object} FormMetadataState
 * @property {FormData} formData Latest serialized form data.
 * @property {MetadataStatus} relevantTabsStatus Relevant-tab request state.
 * @property {MetadataStatus} classificationStatus Classification request state.
 * @property {Promise<void> | null} relevantTabsPromise Active relevant-tab
 * request.
 * @property {Promise<void> | null} classificationPromise Active classification
 * request.
 * @property {number} relevantTabsRevision Revision used to reject stale
 * relevant-tab responses.
 * @property {number} classificationRevision Revision used to reject stale
 * classification responses.
 */

/**
 * How much page content text to extract
 */
const MAX_PAGE_CONTENT_LENGTH = 10000;

const METADATA_STATUS = Object.freeze({
  IDLE: "idle",
  LOADING: "loading",
  READY: "ready",
  FAILED: "failed",
});

const TAB_SELECTOR_URL =
  "chrome://browser/content/aiwindow/smartformfill-tab-select.html";

/**
 * Parent actor for SmartFormFill
 */
export class SmartFormFillParent extends JSWindowActorParent {
  /**
   * Controller for the managed document.
   *
   * @type {Controller | null}
   */
  #controller;

  /**
   * Form associated with the current Smart Form Fill autocomplete entry.
   *
   * @type {string | null}
   */
  #autocompleteFormId;

  /**
   * Whether tab metadata changed while form values were being generated.
   *
   * @type {boolean}
   */
  #tabsChangedDuringValueGeneration;

  /**
   * Current tab-selector dialog.
   *
   * @type {object | null}
   */
  #tabSelectorDialog;

  /**
   * Whether the open tab-selector dialog was taken away rather than closed by
   * the user, which the two look the same as from the dialog's result alone.
   *
   * @type {boolean}
   */
  #tabSelectorAborted;

  /**
   * Current generated-value review session.
   *
   * @type {SmartFormFillReviewSession | null}
   */
  #formReviewSession;

  /**
   * User-selected tabs by form ID.
   *
   * @type {Map<string, Array<SelectedTab>>}
   */
  #userSelectedTabsByFormId;

  /**
   * IDs of the Smart Windows known during the previous tab event.
   *
   * @type {Set<number>}
   */
  #smartWindowIds;

  /**
   * Generation of the latest autofill request
   *
   * @type {number}
   */
  #autofillGeneration;

  /**
   * Metadata request state by stable form ID.
   *
   * @type {Map<string, FormMetadataState>}
   */
  #formMetadataById;

  /**
   * Whether the actor has been destroyed.
   *
   * @type {boolean}
   */
  #destroyed;

  /**
   * Sends the Smart Form Fill glean events for the managed document.
   *
   * @type {SmartFormFillTelemetry}
   */
  #telemetry;

  /**
   * Flow id correlating every event of a form's current round. Minted with the
   * form's metadata and replaced whenever that metadata is invalidated, so one
   * flow is one attempt at filling one form.
   *
   * @type {Map<string, string>}
   */
  #flowIdByFormId;

  /**
   * The model's decision for each field of the latest generation round, by form
   * ID. Kept after the round ends because the field events are only complete
   * once the page reports what it filled, and the outcome of each field is only
   * known when the fill is torn down.
   *
   * @type {Map<string, {
   *   flowId: string,
   *   decisions: Array<FieldDecision>,
   *   reviewValues: Map<string, string> | null,
   * }>}
   */
  #fieldDecisionsByFormId;

  /**
   * How many times the source editor was opened in a form's current flow, and
   * how the last of those opens ended.
   *
   * @type {Map<string, SourceEditorState>}
   */
  #sourceEditorByFormId;

  /**
   * Creates the parent actor.
   */
  constructor() {
    super();

    this.#controller = null;
    this.#autocompleteFormId = null;
    this.#destroyed = false;
    this.#tabsChangedDuringValueGeneration = false;
    this.#tabSelectorDialog = null;
    this.#tabSelectorAborted = false;
    this.#formReviewSession = null;
    this.#userSelectedTabsByFormId = new Map();
    this.#smartWindowIds = new Set();
    this.#autofillGeneration = 0;
    this.#formMetadataById = new Map();
    this.#telemetry = new lazy.SmartFormFillTelemetry();
    this.#flowIdByFormId = new Map();
    this.#fieldDecisionsByFormId = new Map();
    this.#sourceEditorByFormId = new Map();
  }

  /**
   * Starts listening for browser tab lifecycle changes.
   */
  actorCreated() {
    this.#smartWindowIds = this.#getSmartWindowIds();
    lazy.NonPrivateTabs.addEventListener("TabChange", this);
  }

  /**
   * Handles browser tab lifecycle changes.
   *
   * @param {CustomEvent} event
   */
  handleEvent(event) {
    if (
      event.type != "TabChange" ||
      !event.detail.sourceEvents.some(type =>
        ["TabOpen", "TabClose", "TabAttrModified"].includes(type)
      )
    ) {
      return;
    }

    const currentSmartWindowIds = this.#getSmartWindowIds();
    const smartWindowTabChanged = event.detail.windowIds.some(
      id => currentSmartWindowIds.has(id) || this.#smartWindowIds.has(id)
    );
    this.#smartWindowIds = currentSmartWindowIds;

    if (!smartWindowTabChanged) {
      return;
    }

    this.#abortTabSelector();
    this.#userSelectedTabsByFormId.clear();

    if (this.#formReviewSession?.generationPending) {
      this.#tabsChangedDuringValueGeneration = true;
      return;
    }

    this.#invalidateTabMetadata();
  }

  /**
   * Gets the effective tab selection for a form.
   *
   * @param {string} formId
   * @returns {Array<SelectedTab>}
   */
  #getSelectedTabsFor(formId) {
    const selectedTabs =
      this.#userSelectedTabsByFormId.get(formId) ??
      this.#controller.getRelevantTabsFor(formId);

    return selectedTabs.map(({ id }) => ({ id }));
  }

  /**
   * Generates and applies values for the focused form.
   *
   * @returns {Promise<void>}
   */
  async triggerAutofill() {
    await lazy.Region.init().catch(error =>
      lazy.console.error("Could not initialize Region", error)
    );
    if (!this.#onIsSmartWindow()) {
      return;
    }

    const focusedForm = await this.#getFocusedForm();
    if (this.#destroyed || !focusedForm) {
      return;
    }

    const metadata = this.#getFormMetadataState(focusedForm);
    if (metadata.classificationStatus === METADATA_STATUS.FAILED) {
      return;
    }

    this.#startFormMetadataRequests(metadata);
    await Promise.all([
      metadata.relevantTabsPromise,
      metadata.classificationPromise,
    ]);

    if (
      metadata.relevantTabsStatus !== METADATA_STATUS.READY ||
      metadata.classificationStatus !== METADATA_STATUS.READY ||
      this.#cannotAutofill()
    ) {
      return;
    }

    const selectedTabs = this.#getSelectedTabsFor(focusedForm.id);
    await this.#performAutofill(focusedForm, selectedTabs);
  }

  /**
   * Opens the source editor for the focused form.
   *
   * @returns {Promise<void>}
   */
  async #editSources() {
    const focusedForm = await this.#getFocusedForm();
    if (this.#destroyed || !focusedForm) {
      return;
    }

    const metadata = this.#getFormMetadataState(focusedForm);
    this.#startFormMetadataRequests(metadata);
    await metadata.relevantTabsPromise;

    if (
      metadata.relevantTabsStatus !== METADATA_STATUS.READY ||
      this.#cannotAutofill()
    ) {
      return;
    }

    try {
      const selectedTabs = await this.#selectTabs(focusedForm.id);
      if (this.#destroyed) {
        return;
      }

      if (selectedTabs) {
        this.#userSelectedTabsByFormId.set(focusedForm.id, selectedTabs);
      }
    } finally {
      if (!this.#destroyed) {
        const browser = this.browsingContext?.embedderElement;
        if (browser?.isConnected) {
          browser.focus();
        }

        this.sendAsyncMessage("SmartFormFill:ShowAutocompletePopup");
      }
    }
  }

  /**
   * Presents the tab selector and returns the user's selected tabs.
   *
   * @param {string} formId
   *
   * @returns {Promise<Array<SelectedTab> | null>}
   */
  async #selectTabs(formId) {
    if (this.#tabSelectorDialog) {
      return null;
    }

    const relevantTabs = this.#controller.getRelevantTabsFor(formId);
    const initiallySelectedTabIds = new Set(
      this.#getSelectedTabsFor(formId).map(({ id }) => id)
    );
    const tabsById = new Map(
      this.#controller.getTabs().map(tab => [tab.id, tab])
    );
    const toDialogTab = (tab, pressed) => ({
      ...tab,
      favicon: `page-icon:${tab.url}`,
      pressed,
    });

    const suggestedTabs = relevantTabs
      .map(({ id }) => tabsById.get(id))
      .filter(Boolean)
      .map(tab => toDialogTab(tab, initiallySelectedTabIds.has(tab.id)));
    const suggestedTabIds = new Set(suggestedTabs.map(({ id }) => id));
    const otherTabs = [...tabsById.values()]
      .filter(
        tab =>
          !suggestedTabIds.has(tab.id) &&
          tab.url !== this.manager.documentURI.spec
      )
      .map(tab => toDialogTab(tab, initiallySelectedTabIds.has(tab.id)));
    const selectableTabIds = new Set(
      [...suggestedTabs, ...otherTabs].map(({ id }) => id)
    );

    const browser = this.browsingContext.embedderElement;
    const chromeWindow = this.browsingContext.topChromeWindow;
    if (!browser || !chromeWindow?.gBrowser) {
      return null;
    }

    const dialogArguments = {
      suggestedTabs,
      otherTabs,
      result: null,
    };
    const { dialog, closedPromise } = chromeWindow.gBrowser
      .getTabDialogBox(browser)
      .open(
        TAB_SELECTOR_URL,
        {
          features: "resizable=no",
          allowDuplicateDialogs: false,
        },
        dialogArguments
      );

    if (!dialog) {
      return null;
    }

    this.#tabSelectorDialog = dialog;
    this.#tabSelectorAborted = false;

    const editorState = this.#getSourceEditorState(formId);
    editorState.opens++;

    try {
      await closedPromise;
    } finally {
      if (this.#tabSelectorDialog === dialog) {
        this.#tabSelectorDialog = null;
      }
    }

    const selectedTabIds = dialogArguments.result?.selectedTabIds;
    if (
      !Array.isArray(selectedTabIds) ||
      !selectedTabIds.length ||
      selectedTabIds.length > lazy.MAX_SELECTED_TABS ||
      new Set(selectedTabIds).size !== selectedTabIds.length ||
      selectedTabIds.some(id => !selectableTabIds.has(id))
    ) {
      editorState.result = this.#tabSelectorAborted
        ? lazy.SOURCE_EDITOR_RESULT.ABORTED
        : lazy.SOURCE_EDITOR_RESULT.CANCEL;
      return null;
    }

    editorState.result = lazy.SOURCE_EDITOR_RESULT.DONE;
    return selectedTabIds.map(id => ({ id }));
  }

  /**
   * Closes the tab selector on the user's behalf.
   */
  #abortTabSelector() {
    if (!this.#tabSelectorDialog) {
      return;
    }

    this.#tabSelectorAborted = true;
    this.#tabSelectorDialog.abort();
  }

  /**
   * Receives messages from the child actor.
   *
   * @param {object} param
   * @param {Array<FormData> | undefined} param.data Message data.
   * @param {string} param.name The message type
   *
   * @returns {Promise<boolean> | null | undefined}
   */
  receiveMessage({ data, name }) {
    if (!this.#onIsSmartWindow()) {
      return null;
    }

    switch (name) {
      case "SmartFormFill:IsSmartWindow":
        return lazy.Region.init()
          .catch(error =>
            lazy.console.error("Could not initialize Region", error)
          )
          .then(() => this.#onIsSmartWindow());

      case "SmartFormFill:FormUpdate":
        return this.#onFormUpdate(data);

      case "SmartFormFill:FieldsFilled":
        return this.#onFieldsFilled(data.id, data.fieldIds);

      case "SmartFormFill:FieldOutcomes":
        return this.#onFieldOutcomes(data.id, data.fields);
    }

    return null;
  }

  /**
   * Destroys controller state.
   */
  didDestroy() {
    lazy.NonPrivateTabs.removeEventListener("TabChange", this);
    this.#abortTabSelector();
    this.#tabSelectorDialog = null;
    this.#formReviewSession?.abort();
    this.#formReviewSession = null;
    this.#tabsChangedDuringValueGeneration = false;
    this.#userSelectedTabsByFormId.clear();
    this.#smartWindowIds.clear();
    this.#destroyed = true;
    this.#formMetadataById.clear();
    this.#flowIdByFormId.clear();
    this.#fieldDecisionsByFormId.clear();
    this.#sourceEditorByFormId.clear();
    this.#controller?.destroy();
    this.#controller = null;
  }

  /**
   * Gets the controller used for demand-driven form metadata.
   *
   * @returns {Controller}
   */
  #getController() {
    if (!this.#controller) {
      this.#controller = new lazy.SmartFormFillController(
        this.#getPageInfo(),
        this.#getRequestObserver()
      );
    }

    return this.#controller;
  }

  /**
   * Gets the currently focused form.
   *
   * @returns {Promise<FocusedForm | null>}
   */
  #getFocusedForm() {
    return this.sendQuery("SmartFormFill:GetFocusedForm").catch(error => {
      if (!this.#destroyed) {
        lazy.console.error("Could not get focused Smart Form Fill form", error);
      }

      return null;
    });
  }

  /**
   * Gets cached metadata state for a form.
   *
   * @param {FocusedForm} focusedForm Focused form data.
   * @returns {FormMetadataState}
   */
  #getFormMetadataState(focusedForm) {
    const formData = {
      id: focusedForm.id,
      fields: focusedForm.fields,
    };
    let metadata = this.#formMetadataById.get(focusedForm.id);

    if (!metadata) {
      metadata = {
        formData,
        relevantTabsStatus: METADATA_STATUS.IDLE,
        classificationStatus: METADATA_STATUS.IDLE,
        relevantTabsPromise: null,
        classificationPromise: null,
        relevantTabsRevision: 0,
        classificationRevision: 0,
      };
      this.#formMetadataById.set(focusedForm.id, metadata);
      this.#flowIdByFormId.set(focusedForm.id, crypto.randomUUID());
    } else if (this.#hasFormStructureChanged(metadata.formData, formData)) {
      this.#invalidateFormMetadata(metadata, formData);
    }

    return metadata;
  }

  /**
   * Checks whether fields were added to or removed from a form.
   *
   * @param {FormData} previousFormData Previously cached form data.
   * @param {FormData} formData Current form data.
   * @returns {boolean} Whether the form field collection changed.
   */
  #hasFormStructureChanged(previousFormData, formData) {
    if (previousFormData.fields.length !== formData.fields.length) {
      return true;
    }

    const previousFieldIds = new Set(
      previousFormData.fields.map(({ id }) => id)
    );
    return formData.fields.some(({ id }) => !previousFieldIds.has(id));
  }

  /**
   * Invalidates cached model metadata after a form structure change.
   *
   * @param {FormMetadataState} metadata Metadata to invalidate.
   * @param {FormData} formData Updated form data.
   */
  #invalidateFormMetadata(metadata, formData) {
    ++metadata.relevantTabsRevision;
    ++metadata.classificationRevision;
    metadata.formData = formData;
    metadata.relevantTabsStatus = METADATA_STATUS.IDLE;
    metadata.classificationStatus = METADATA_STATUS.IDLE;
    metadata.relevantTabsPromise = null;
    metadata.classificationPromise = null;
    this.#controller.invalidateForm(formData.id);

    // The round this form was on is superseded, so its events stop sharing a
    // flow with the ones the next round will record.
    this.#flowIdByFormId.set(formData.id, crypto.randomUUID());
    this.#fieldDecisionsByFormId.delete(formData.id);
    this.#sourceEditorByFormId.delete(formData.id);
  }

  /**
   * Starts missing metadata requests for a form.
   *
   * @param {FormMetadataState} metadata Form metadata state.
   */
  #startFormMetadataRequests(metadata) {
    if (metadata.relevantTabsStatus === METADATA_STATUS.IDLE) {
      this.#loadRelevantTabs(metadata);
    }

    if (metadata.classificationStatus === METADATA_STATUS.IDLE) {
      this.#loadFieldClassifications(metadata);
    }
  }

  /**
   * Requests relevant tabs for a form.
   *
   * @param {FormMetadataState} metadata Form metadata state.
   * @returns {Promise<void>}
   */
  #loadRelevantTabs(metadata) {
    const revision = metadata.relevantTabsRevision;
    metadata.relevantTabsStatus = METADATA_STATUS.LOADING;

    const request = this.#getController()
      .findRelevantTabs(metadata.formData)
      .catch(error => {
        if (revision === metadata.relevantTabsRevision && !this.#destroyed) {
          lazy.console.error("Could not find relevant tabs", error);
        }
      })
      .then(() => {
        if (revision !== metadata.relevantTabsRevision || this.#destroyed) {
          return;
        }

        metadata.relevantTabsStatus = METADATA_STATUS.READY;
        this.sendAsyncMessage("SmartFormFill:RefreshAutocomplete");
      })
      .finally(() => {
        if (revision === metadata.relevantTabsRevision) {
          metadata.relevantTabsPromise = null;
        }
      });

    metadata.relevantTabsPromise = request;
    return request;
  }

  /**
   * Requests field classifications for a form.
   *
   * @param {FormMetadataState} metadata Form metadata state.
   * @returns {Promise<void>}
   */
  #loadFieldClassifications(metadata) {
    const revision = metadata.classificationRevision;
    metadata.classificationStatus = METADATA_STATUS.LOADING;

    const request = this.#getController()
      .classifyFields(metadata.formData)
      .then(() => {
        if (revision === metadata.classificationRevision && !this.#destroyed) {
          metadata.classificationStatus = METADATA_STATUS.READY;
        }
      })
      .catch(error => {
        if (revision !== metadata.classificationRevision || this.#destroyed) {
          return;
        }

        metadata.classificationStatus = METADATA_STATUS.FAILED;
        lazy.console.error("Could not classify Smart Form Fill fields", error);
        this.sendAsyncMessage("SmartFormFill:RefreshAutocomplete");
      })
      .finally(() => {
        if (revision === metadata.classificationRevision) {
          metadata.classificationPromise = null;
        }
      });

    metadata.classificationPromise = request;
    return request;
  }

  /**
   * Triggers the autofill for the focused form with chosen tabs
   *
   * @param {FocusedForm} focusedForm
   * @param {Array<SelectedTab>} selectedTabs
   *
   * @returns {Promise<void>}
   */
  async #performAutofill(focusedForm, selectedTabs) {
    if (this.#formReviewSession) {
      return;
    }

    const generation = ++this.#autofillGeneration;

    if (!focusedForm || this.#cannotApplyAutofill(generation)) {
      return;
    }

    const reviewSessionPromise = this.#openFormReview(
      focusedForm.id,
      generation
    );

    const numberOfTabs = selectedTabs.length + 1;
    const perTabTextCharBudget = Math.ceil(
      MAX_PAGE_CONTENT_LENGTH / numberOfTabs
    );

    const [pageText, tabContentById] = await Promise.all([
      this.#getPageText(this.manager.documentURI.spec, perTabTextCharBudget),
      this.#getTabsContent(selectedTabs, perTabTextCharBudget),
    ]);

    if (this.#cannotApplyAutofill(generation)) {
      return;
    }

    this.#recordTabSelectionOutcome(focusedForm.id, selectedTabs);

    // Handle value generation errors here in case it errors before the dialog is
    // ready.
    const generationPromise = this.#controller
      .autofill(
        focusedForm.id,
        focusedForm.emptyFieldIds,
        selectedTabs,
        tabContentById,
        pageText
      )
      .then(result => ({ result, error: null }))
      .catch(error => ({ result: null, error }));

    let reviewSession;
    try {
      reviewSession = await reviewSessionPromise;
    } catch (error) {
      this.#cancelFormReviewGeneration(focusedForm.id, generation);
      throw error;
    }

    if (!reviewSession) {
      this.#cancelFormReviewGeneration(focusedForm.id, generation);
      return;
    }

    if (this.#cannotApplyAutofill(generation)) {
      return;
    }

    const generationResult = await generationPromise;
    if (generationResult.error) {
      if (!this.#cannotApplyAutofill(generation)) {
        lazy.console.error(
          "Could not generate Smart Form Fill values",
          generationResult.error
        );

        this.#finishFormReviewGeneration(reviewSession, generation, {
          errorType: lazy.FORM_REVIEW_ERRORS.GENERATION_FAILED,
        });
      }
      return;
    }

    if (this.#cannotApplyAutofill(generation)) {
      return;
    }

    const fields = generationResult.result
      ? this.#getFormReviewFields(generationResult.result)
      : [];
    this.#setReviewValues(focusedForm.id, fields);
    this.#finishFormReviewGeneration(
      reviewSession,
      generation,
      fields.length
        ? { fields }
        : {
            errorType: lazy.FORM_REVIEW_ERRORS.NO_SUGGESTIONS,
          }
    );
  }

  /**
   * Opens the form review dialog while suggestion generation begins.
   *
   * @param {string} formId The stable form ID.
   * @param {number} generation The current autofill generation.
   *
   * @returns {Promise<SmartFormFillReviewSession | null>}
   *   The initialized review session, or null if it could not be opened.
   */
  async #openFormReview(formId, generation) {
    if (this.#formReviewSession || this.#destroyed) {
      return null;
    }

    const browser = this.browsingContext.embedderElement;
    const chromeWindow = this.browsingContext.topChromeWindow;
    if (!browser || !chromeWindow?.gBrowser) {
      return null;
    }

    const session = new lazy.SmartFormFillReviewSession({
      browser,
      chromeWindow,
      onCancelGeneration: () =>
        this.#cancelFormReviewGeneration(formId, generation),
      onClose: closedSession => this.#onFormReviewClosed(closedSession),
      onFill: fields => this.#fillReviewedFields(formId, fields),
    });
    this.#formReviewSession = session;

    let ready = false;
    try {
      ready = await session.open();
    } finally {
      if (!ready && this.#formReviewSession === session) {
        this.#formReviewSession = null;
      }
    }

    return ready ? session : null;
  }

  /**
   * Sends a completed generation result to the form review session.
   *
   * @param {SmartFormFillReviewSession} session
   *   The session associated with the generation request.
   * @param {number} generation The completed autofill generation.
   * @param {FormReviewGenerationResult} result The generation outcome.
   * @returns {void}
   */
  #finishFormReviewGeneration(session, generation, result) {
    if (
      this.#formReviewSession !== session ||
      this.#cannotApplyAutofill(generation)
    ) {
      return;
    }

    if (session.completeGeneration(result)) {
      this.#refreshDeferredTabData();
    }
  }

  /**
   * Clears parent state after a form-review session closes.
   *
   * @param {SmartFormFillReviewSession} session The closed session.
   * @returns {void}
   */
  #onFormReviewClosed(session) {
    if (this.#formReviewSession !== session) {
      return;
    }

    this.#formReviewSession = null;
    this.#refreshDeferredTabData();
  }

  /**
   * Invalidates tab metadata that changed during value generation.
   *
   * @returns {void}
   */
  #refreshDeferredTabData() {
    if (!this.#tabsChangedDuringValueGeneration) {
      return;
    }

    this.#tabsChangedDuringValueGeneration = false;
    this.#invalidateTabMetadata();
  }

  /**
   * Cancels suggestion generation associated with the open review dialog.
   *
   * @param {string} formId The form whose generation should be cancelled.
   * @param {number} generation The generation associated with the session.
   * @returns {void}
   */
  #cancelFormReviewGeneration(formId, generation) {
    if (this.#destroyed || generation !== this.#autofillGeneration) {
      return;
    }

    ++this.#autofillGeneration;
    this.#controller.cancelAutofill(formId);
  }

  /**
   * Enriches generated values with current serialized field metadata for the
   * review UI. Values for fields that no longer exist are omitted.
   *
   * @param {FillFormResult} result The generated form values.
   *
   * @returns {Array<FormReviewField>}
   *   Current fields and their generated values to present for review.
   */
  #getFormReviewFields(result) {
    const formData = this.#formMetadataById.get(result.id)?.formData;
    const fieldDataById = new Map(
      formData?.fields.map(field => [field.id, field]) ?? []
    );
    const reviewFields = [];

    for (const { id, value } of result.fields) {
      const fieldData = fieldDataById.get(id);
      if (!fieldData) {
        continue;
      }

      reviewFields.push({
        id,
        label: fieldData.label ?? "",
        placeholder: fieldData.placeholder ?? "",
        name: fieldData.name ?? "",
        value,
      });
    }

    return reviewFields;
  }

  /**
   * Sends reviewed values to the content actor for filling.
   *
   * @param {string} formId The stable form ID.
   * @param {Array<{id: string, value: string}>} fields
   *   The reviewed values approved by the session.
   * @returns {Promise<FormReviewFillResult>} The form-filling result.
   */
  async #fillReviewedFields(formId, fields) {
    if (this.#destroyed) {
      return {
        hasErrors: true,
        cancelled: false,
        filledFieldCount: 0,
      };
    }

    this.#recordFieldReviewOutcomes(formId, fields);

    try {
      const result = await this.sendQuery("SmartFormFill:FillForm", {
        id: formId,
        fields: fields.filter(({ value }) => value.trim() !== ""),
      });

      if (!result) {
        return {
          hasErrors: true,
          cancelled: false,
          filledFieldCount: 0,
        };
      }

      return {
        hasErrors: !!result.hasErrors,
        cancelled: !!result.cancelled,
        filledFieldCount: result.filledFieldCount,
      };
    } catch (error) {
      if (!this.#destroyed) {
        lazy.console.error("Could not fill Smart Form Fill values", error);
      }
      return {
        hasErrors: true,
        cancelled: false,
        filledFieldCount: 0,
      };
    }
  }

  /**
   * Gets the page content for each tab
   *
   * @param {Array<SelectedTab>} selectedTabs
   * @param {number} textCharLimitPerTab
   *
   * @returns {Promise<Map<string, string>>}
   */
  async #getTabsContent(selectedTabs, textCharLimitPerTab) {
    const tabContentById = new Map();

    const promises = selectedTabs.map(selectedTab => {
      const tabData = this.#controller.getTabData(selectedTab.id);

      if (tabData) {
        return this.#getPageText(tabData.url, textCharLimitPerTab).then(
          tabContent => {
            tabContentById.set(selectedTab.id, tabContent);
          }
        );
      }

      return Promise.resolve();
    });

    await Promise.allSettled(promises);

    return tabContentById;
  }

  /**
   * Attempts to extract page content from a url if the tab is found
   *
   * @param {string} sourceUrl
   * @param {number} textCharLimit
   *
   * @returns {Promise<string>}
   */
  async #getPageText(sourceUrl, textCharLimit) {
    let windowGlobal = this.manager;

    if (windowGlobal.documentURI.spec !== sourceUrl) {
      const tab = lazy.GetPageContent.getTabWithURL(sourceUrl);
      windowGlobal =
        tab?.linkedBrowser.browsingContext?.currentWindowGlobal ?? null;
    }

    if (!windowGlobal) {
      return "";
    }

    try {
      const pageExtractor = windowGlobal.getActor("PageExtractor");
      const extraction = await pageExtractor.getText({
        sufficientLength: textCharLimit,
        removeBoilerplate: true,
        sourceUrl,
      });

      return extraction?.text ?? "";
    } catch (error) {
      if (!this.#destroyed) {
        lazy.console.error("Could not extract page text", error);
      }

      return "";
    }
  }

  /**
   * Gets information about the current page.
   *
   * @returns {PageInfo}
   */
  #getPageInfo() {
    const windowGlobal = this.manager;

    return {
      url: windowGlobal.documentURI.spec,
      title: windowGlobal.documentTitle,
    };
  }

  /**
   * Gets the IDs of active Smart Windows.
   *
   * @returns {Set<number>}
   */
  #getSmartWindowIds() {
    return new Set(
      lazy.BrowserWindowTracker.orderedWindows
        .filter(window => lazy.AIWindow.isAIWindowActive(window))
        .map(window => window.windowGlobalChild.innerWindowId)
    );
  }

  /**
   * Invalidates tab-dependent metadata for all tracked forms.
   */
  #invalidateTabMetadata() {
    if (this.#destroyed || !this.#controller) {
      return;
    }

    for (const metadata of this.#formMetadataById.values()) {
      ++metadata.relevantTabsRevision;
      metadata.relevantTabsPromise = null;
      metadata.relevantTabsStatus = METADATA_STATUS.IDLE;
    }

    this.#controller.invalidateTabs();
    this.sendAsyncMessage("SmartFormFill:RefreshAutocomplete");
  }

  /**
   * Checks whether autofill can run.
   *
   * @returns {boolean}
   */
  #cannotAutofill() {
    if (!this.#onIsSmartWindow()) {
      return true;
    }

    return !this.#controller;
  }

  /**
   * Checks whether autofill results may still be applied.
   *
   * @param {number} generation
   *
   * @returns {boolean}
   */
  #cannotApplyAutofill(generation) {
    return this.#cannotAutofill() || generation !== this.#autofillGeneration;
  }

  /**
   * Checks whether this is an active Smart Window in a supported region.
   *
   * @returns {boolean}
   */
  #onIsSmartWindow() {
    if (
      this.#destroyed ||
      !lazy.AIWindow.isAIWindowActive(this.browsingContext.topChromeWindow)
    ) {
      return false;
    }

    return !this.#destroyed && !this.#isDisallowedRegion();
  }

  /**
   * Checks whether the user's home region is on the Smart Form Fill denylist.
   * An unknown home region is treated as allowed.
   *
   * @returns {boolean}
   */
  #isDisallowedRegion() {
    const homeRegion = lazy.Region.home?.toUpperCase();

    return lazy.disallowedRegions
      .split(",")
      .map(region => region.trim().toUpperCase())
      .filter(Boolean)
      .includes(homeRegion);
  }

  /**
   * Message handler for "SmartFormFill:FormUpdate"
   *
   * @param {Array<FormData>} formDataList
   */
  #onFormUpdate(formDataList) {
    if (this.#destroyed || !Array.isArray(formDataList)) {
      return;
    }

    const formsById = new Map(
      formDataList.map(formData => [formData.id, formData])
    );

    for (const [formId, metadata] of this.#formMetadataById) {
      const formData = formsById.get(formId);

      if (!formData) {
        ++metadata.relevantTabsRevision;
        ++metadata.classificationRevision;
        this.#controller.invalidateForm(formId);
        this.#formMetadataById.delete(formId);
        this.#flowIdByFormId.delete(formId);
        this.#fieldDecisionsByFormId.delete(formId);
        this.#sourceEditorByFormId.delete(formId);
        this.#userSelectedTabsByFormId.delete(formId);
        continue;
      }

      if (this.#hasFormStructureChanged(metadata.formData, formData)) {
        this.#invalidateFormMetadata(metadata, formData);
      }
    }
  }

  /*
   * AutoComplete-related functions
   */

  /**
   * Gets the Smart Form Fill autocomplete entry for the focused field.
   *
   * SmartFormFillAutocomplete calls this when an autocomplete provider
   * requests Smart Form Fill entries.
   *
   * @param {string} _searchString
   *   The current autocomplete search string. Smart Form Fill does not
   *   currently filter its entry using this value.
   * @param {{ focusElementId?: string }} options
   *   Options supplied by SmartFormFillChild.
   * @returns {Promise<{ entries: Array<object> } | null>}
   *   An object containing the Smart Form Fill entry, or null when the entry
   *   should not be shown.
   */
  async searchAutoCompleteEntries(_searchString, options) {
    if (this.#destroyed) {
      return null;
    }

    const focusedForm = await this.#getFocusedForm();

    // Every provider that injects the Smart Form Fill entry funnels through
    // here, so this is where the entry is kept out of a field the user has
    // already put a value in, or is typing in.
    if (
      this.#destroyed ||
      !focusedForm?.emptyFieldIds.has(focusedForm.focusedFieldId)
    ) {
      return null;
    }

    const metadata = this.#getFormMetadataState(focusedForm);

    if (metadata.classificationStatus === METADATA_STATUS.FAILED) {
      return null;
    }

    this.#autocompleteFormId = focusedForm.id;
    this.#startFormMetadataRequests(metadata);

    const availableTabs = this.#controller.getTabs().length;
    const entries = await lazy.SmartFormFillAutocomplete.createItemsAsync({
      sffActor: this,
      formId: focusedForm.id,
      focusElementId: options.focusElementId,
      availableTabs,
    });

    return entries.length ? { entries } : null;
  }

  /**
   * Adds parent-only source metadata after the autocomplete popup opens.
   */
  onAutoCompletePopupOpened() {
    this.#updateAutoCompletePopupSources();
  }

  /**
   * Adds parent-only source metadata after an open autocomplete popup updates.
   */
  onAutoCompletePopupUpdated() {
    this.#updateAutoCompletePopupSources();
  }

  /**
   * Updates the current Smart Form Fill row with its relevant tab sources.
   */
  #updateAutoCompletePopupSources() {
    if (this.#destroyed) {
      return;
    }

    const formId = this.#autocompleteFormId;
    if (!formId || !this.areRelevantTabsReady(formId)) {
      return;
    }

    lazy.SmartFormFillAutocomplete.updatePopupSources({
      browser: this.browsingContext.top.embedderElement,
      sources: this.getSelectedTabSources(formId),
    });
  }

  /**
   * Handles selection of a Smart Form Fill autocomplete action.
   *
   * AutoCompleteParent calls this using the action name stored in the selected
   * entry's comment metadata.
   *
   * @param {string} message
   *   The selected autocomplete action name.
   * @returns {Promise<void> | undefined}
   *   The autofill operation for the primary action, or undefined for an
   *   unsupported action.
   */
  onAutoCompleteEntrySelected(message) {
    if (this.#destroyed) {
      return undefined;
    }

    switch (message) {
      case "SmartFormFill:Start":
        return this.triggerAutofill();

      case "SmartFormFill:EditSources":
        return this.#editSources();
    }

    return undefined;
  }

  /**
   * Gets display data for tabs selected for a form.
   *
   * @param {string} formId Stable form identifier.
   * @returns {Array<SmartFormFillAutocompleteSource>}
   *   Display data for selected tabs, or an empty array when unavailable.
   */
  getSelectedTabSources(formId) {
    if (!this.#controller) {
      return [];
    }

    return this.#getSelectedTabsFor(formId)
      .map(({ id }) => this.#controller.getTabData(id))
      .filter(Boolean)
      .map(tab => ({
        label: tab.title || tab.url,
        favicon: `page-icon:${tab.url}`,
      }));
  }

  /**
   * Checks whether relevant-tab selection has completed for a form.
   *
   * @param {string} formId Stable form identifier.
   * @returns {boolean} Whether the relevant-tab result can be displayed.
   */
  areRelevantTabsReady(formId) {
    if (this.#destroyed) {
      return false;
    }

    return (
      this.#formMetadataById.get(formId)?.relevantTabsStatus ===
      METADATA_STATUS.READY
    );
  }

  /**
   * Whether Smart Form Fill has tabs available as context sources.
   *
   * @type {boolean}
   */
  get hasSourceTabs() {
    return Boolean(this.#controller?.hasSourceTabs);
  }

  /**
   * Gets the flow id correlating the events of a form's current round. Empty
   * when the form is gone, which a request in flight can outlive.
   *
   * @param {string} formId
   *
   * @returns {string}
   */
  #getFlowId(formId) {
    return this.#flowIdByFormId.get(formId) ?? "";
  }

  /**
   * Gets how the source editor has been used in a form's current flow,
   * starting a count for a flow that has not opened it yet.
   *
   * @param {string} formId
   *
   * @returns {SourceEditorState}
   */
  #getSourceEditorState(formId) {
    let state = this.#sourceEditorByFormId.get(formId);

    if (!state) {
      state = { opens: 0, result: null };
      this.#sourceEditorByFormId.set(formId, state);
    }

    return state;
  }

  /**
   * Records what the tab selection ended up being for a fill that is starting.
   *
   * @param {string} formId
   * @param {Array<SelectedTab>} selectedTabs The tabs the fill will run with
   */
  #recordTabSelectionOutcome(formId, selectedTabs) {
    this.#telemetry.sendRelevantTabsOutcomeTelemetry(this.#getFlowId(formId), {
      suggestedTabs: this.#controller.getRelevantTabsFor(formId),
      finalTabs: selectedTabs,
      editor: this.#sourceEditorByFormId.get(formId),
    });
  }

  /**
   * Keeps the values that are presented for review, so what the user
   * submits can be differentiated from what the model generated.
   *
   * @param {string} formId
   * @param {Array<FormReviewField>} fields The fields the review will show
   */
  #setReviewValues(formId, fields) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    if (!round) {
      return;
    }

    round.reviewValues = new Map(fields.map(({ id, value }) => [id, value]));
  }

  /**
   * Records what the user did with each generated value, after they
   * submitted the review dialog's fill.
   *
   * @param {string} formId
   * @param {Array<{id: string, value: string}>} fields The reviewed values the
   * dialog was submitted with
   */
  #recordFieldReviewOutcomes(formId, fields) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    const reviewValues = round?.reviewValues;
    if (!reviewValues) {
      return;
    }

    round.reviewValues = null;

    this.#telemetry.sendFillFieldReviewOutcomeTelemetry(
      round.flowId,
      round.decisions,
      { generated: reviewValues, submitted: fields }
    );
  }

  /**
   * Records what the model decided for each field of a form, now that the page
   * has reported which of them it filled.
   *
   * @param {string} formId
   * @param {Array<string>} filledFieldIds
   */
  #onFieldsFilled(formId, filledFieldIds) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    if (!round) {
      return;
    }

    this.#telemetry.sendFillFieldTelemetry(
      round.flowId,
      round.decisions,
      filledFieldIds
    );
  }

  /**
   * Records what became of the fields a round filled, now that the page has
   * reported the state each one ended in.
   *
   * @param {string} formId
   * @param {Array<{
   *   id: string,
   *   edited: boolean,
   *   isEmpty: boolean,
   *   filledLength: number,
   *   finalLength: number,
   * }>} fields
   */
  #onFieldOutcomes(formId, fields) {
    const round = this.#fieldDecisionsByFormId.get(formId);
    if (!round) {
      return;
    }

    this.#telemetry.sendFillFieldOutcomeTelemetry(
      round.flowId,
      round.decisions,
      fields
    );
  }

  /**
   * Builds the callbacks the controller reports its requests through.
   *
   * @returns {RequestObserver}
   */
  #getRequestObserver() {
    return {
      onRelevantTabsDispatched: (formId, request, modelInfo, threshold) =>
        this.#telemetry.startRelevantTabsRequest(
          this.#getFlowId(formId),
          request,
          modelInfo,
          threshold
        ),

      onRelevantTabsAnswered: (flow, response, tabsUsed) =>
        this.#telemetry.sendRelevantTabsResponseTelemetry(
          flow,
          response,
          tabsUsed
        ),

      onRelevantTabsFailed: (flow, error) =>
        this.#telemetry.sendRelevantTabsErrorTelemetry(flow, error),

      onClassifyDispatched: (formId, request, modelInfo) =>
        this.#telemetry.startClassifyRequest(
          this.#getFlowId(formId),
          request,
          modelInfo
        ),

      onClassifyAnswered: (flow, response) =>
        this.#telemetry.sendClassifyResponseTelemetry(flow, response),

      onClassifyFailed: (flow, error) =>
        this.#telemetry.sendClassifyErrorTelemetry(flow, error),

      onGenerateDispatched: (formId, request, dispatch) =>
        this.#telemetry.startGenerateRequest(
          this.#getFlowId(formId),
          request,
          dispatch
        ),

      onGenerateAnswered: (flow, result) => {
        const {
          formId,
          fieldsFilled,
          response,
          fields,
          formFields,
          classifications,
          tokensByFieldId,
          similarityByMemory,
        } = result;

        this.#telemetry.sendGenerateResponseTelemetry(
          flow,
          fieldsFilled,
          response,
          similarityByMemory
        );

        this.#fieldDecisionsByFormId.set(formId, {
          flowId: flow.flowId,
          decisions: this.#telemetry.resolveFieldDecisions({
            fields,
            formFields,
            classifications,
            tokensByFieldId,
            values: response,
          }),
          reviewValues: null,
        });
      },

      onGenerateFailed: (flow, error) =>
        this.#telemetry.sendGenerateErrorTelemetry(flow, error),
    };
  }
}
