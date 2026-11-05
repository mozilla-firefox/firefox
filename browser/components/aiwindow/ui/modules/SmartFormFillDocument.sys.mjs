/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { SUPPORTED_INPUT_TYPES } from "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({ logModule: "SmartFormFillDocument" });
});

ChromeUtils.defineESModuleGetters(lazy, {
  FormLikeFactory: "resource://gre/modules/FormLikeFactory.sys.mjs",
  FormAutofillHeuristics:
    "resource://gre/modules/shared/FormAutofillHeuristics.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  SmartFormFillUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillUtils.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "MIN_FORM_FIELDS",
  "browser.smartwindow.smartformfill.minFormFields",
  4
);

const INPUTS_SELECTOR = "input, textarea";

// NOTE: Field attribute changes are not observed in the initial version.
// Changes affecting grouping or classification can leave cached data stale.
const MUTATION_OBSERVER_OPTIONS = {
  attributes: false,
  childList: true,
  subtree: true,
};

/**
 * @typedef {{
 *  action: string,
 *  autocomplete: string,
 *  ownerDocument: Document,
 *  rootElement: HTMLElement,
 *  elements: Array<HTMLElement>,
 * }} FormLike
 */

/**
 * @typedef {import("resource://gre/modules/shared/FieldScanner.sys.mjs").FieldDetail} FieldDetail
 */

/** @typedef {import("moz-src:///browser/components/aiwindow/models/SmartFormFillModel.sys.mjs").FieldData} FieldData */
/** @typedef {import("moz-src:///browser/components/aiwindow/ui/modules/SmartFormFillUtils.sys.mjs").SmartFormFillUtils} SmartFormFillUtils */

/**
 * @typedef {HTMLInputElement | HTMLTextAreaElement} SmartFormFillField
 */

/**
 * @typedef {{
 *   id: string,
 *   fields: Array<{
 *     id: string,
 *     edited: boolean,
 *     isEmpty: boolean,
 *     filledLength: number,
 *     finalLength: number,
 *   }>,
 * }} FieldOutcomes The state the page saw for the fields one fill wrote to
 */

/**
 * @typedef {{
 *  formId?: string,
 *  formLike: FormLike,
 *  fields: Array<SmartFormFillField>,
 *  fieldDetailsList?: Array<FieldDetail>,
 * }} FormGroup
 */

/**
 * @typedef {{
 *   formId: string,
 *   formFields: Array<SffFormField>
 * }} SffForm
 */

/**
 * @typedef {{
 *   fieldId: string,
 *   field: SmartFormFillField,
 *   details: FieldDetail | null
 * }} SffFormField
 */

/**
 * @typedef {object} FormData
 * @property {string} id The form id
 * @property {Array<FieldData>} fields The list of fields for the form
 */

/**
 * @typedef {object} FocusedForm
 * @property {string} id Stable identifier for the focused form.
 * @property {Array<FieldData>} fields Serializable fields belonging to the
 * focused form.
 * @property {Set<string>} emptyFieldIds IDs of fields that can be filled.
 * @property {string} focusedFieldId ID of the field that has focus.
 */

/**
 * @typedef {object} FillFormOperationResult
 * @property {boolean} hasErrors Whether a valid field failed to be filled.
 * @property {boolean} cancelled Whether filling was cancelled before completion.
 * @property {number} filledFieldCount Number of fields successfully filled.
 */

/**
 * SmartFormFillDocument
 */
export class SmartFormFillDocument {
  /**
   * Whether the module has completed initializing
   *
   * @type {boolean}
   */
  #initialized;

  /**
   * Whether the module has been destroyed
   *
   * @type {boolean}
   */
  #destroyed;

  /**
   * The document element that this instance manages
   *
   * @type {Document | null}
   */
  #doc;

  /**
   * Nearby-text utilities for the managed document.
   *
   * @type {SmartFormFillUtils | null}
   */
  #utils;

  /**
   * A map of form IDs to FormGroup objects
   *
   * @type {Map<string, FormGroup> | null}
   */
  #forms;

  /**
   * A map of form root elements to FormGroup objects
   *
   * @type {Map<HTMLElement, FormGroup> | null}
   */
  #formRoots;

  /**
   * MutationObserver to watch for form inputs that are added
   *
   * @type {MutationObserver | null}
   */
  #observer;

  /**
   * Root elements being observed for DOM mutations
   *
   * @type {WeakSet<HTMLElement | ShadowRoot> | null}
   */
  #observedRoots;

  /**
   * Form counter to generate form IDs
   *
   * @type {number}
   */
  #formCounter;

  /**
   * Field IDs map by SmartFormFillField
   *
   * @type {WeakMap<SmartFormFillField, string> | null}
   */
  #fieldIds;

  /**
   * SmartFormFillField by field id
   *
   * @type {Map<string, SmartFormFillField> | null}
   */
  #fieldsById;

  /**
   * Field counter used to create stable field ids
   *
   * @type {number}
   */
  #fieldCounter;

  /**
   * Generation of the current form-filling operation.
   *
   * @type {number}
   */
  #fillGeneration;

  /**
   * Callback to handle dynamic form updates after page load
   *
   * @type {((formDataList: Array<FormData>) => void) | null}
   */
  #onFormUpdate;

  /**
   * Callback to report what became of the fields that were filled
   *
   * @type {((outcomes: FieldOutcomes) => void) | null}
   */
  #onFieldOutcomes;

  /**
   * Callback to report which fields a fill wrote to
   *
   * @type {((filled: { id: string, fieldIds: Array<string> }) => void) | null}
   */
  #onFieldsFilled;

  /**
   * The fields the latest fill wrote to, and whether the user has typed in them
   * since. No filled value is kept, only its length: an input event is what
   * tells a field the user edited from one they left alone.
   *
   * @type {Map<string, { formId: string, edited: boolean, length: number }> | null}
   */
  #filledFields;

  /**
   * Map to collect affected groups during rapid
   * form mutations.
   *
   * @type {Map<HTMLElement, FormGroup> | null}
   */
  #formUpdateAffectedGroupsMap;

  /**
   * Pending form-update timeout
   *
   * @type {number | null}
   */
  #formUpdateTimeout;

  /**
   * Creates a manager for a document.
   *
   * @param {Document} doc
   */
  constructor(doc) {
    this.#initialized = false;
    this.#destroyed = false;
    this.#doc = doc;
    this.#utils = new lazy.SmartFormFillUtils();
    this.#forms = new Map();
    this.#formRoots = new Map();
    this.#observer = null;
    this.#observedRoots = new WeakSet();
    this.#formCounter = 0;
    this.#fieldIds = new WeakMap();
    this.#fieldsById = new Map();
    this.#fieldCounter = 0;
    this.#fillGeneration = 0;
    this.#onFormUpdate = null;
    this.#onFieldOutcomes = null;
    this.#onFieldsFilled = null;
    this.#filledFields = new Map();
    this.#formUpdateAffectedGroupsMap = null;
    this.#formUpdateTimeout = null;
  }

  /**
   * Initialize SmartFormFieldDocument, sets up mutation observer
   * and runs initial field detection.
   *
   * @param {((formDataList: Array<FormData>) => void) | null} onFormUpdate Callback to handle form updates
   * @param {((outcomes: FieldOutcomes) => void) | null} [onFieldOutcomes] Callback
   * to report what became of the fields that were filled
   * @param {((filled: { id: string, fieldIds: Array<string> }) => void) | null}
   * [onFieldsFilled] Callback to report which fields a fill wrote to
   *
   * @returns {Promise<void>}
   */
  async initialize(
    onFormUpdate,
    onFieldOutcomes = null,
    onFieldsFilled = null
  ) {
    if (this.#initialized || this.#destroyed) {
      return;
    }

    try {
      this.#monitorDocument();
      this.#monitorFilledFields();
      await this.#detectFields();

      if (this.#destroyed) {
        return;
      }

      this.#onFormUpdate = onFormUpdate;
      this.#onFieldOutcomes = onFieldOutcomes;
      this.#onFieldsFilled = onFieldsFilled;

      this.#initialized = true;
    } catch (error) {
      if (!this.#destroyed) {
        lazy.console.error(
          "Smart Form Fill document initialization failed, SFF is not available",
          error
        );

        this.destroy();
      }

      throw error;
    }
  }

  /**
   * Module tear down.
   */
  destroy() {
    if (this.#destroyed) {
      return;
    }

    this.#stopMonitoringFilledFields();
    this.#destroyed = true;
    ++this.#fillGeneration;

    this.#formCounter = 0;
    this.#fieldCounter = 0;

    if (this.#formUpdateTimeout) {
      this.#doc.defaultView.clearTimeout(this.#formUpdateTimeout);
    }

    this.#doc = null;
    this.#utils = null;

    this.#forms.clear();
    this.#forms = null;

    this.#formRoots.clear();
    this.#formRoots = null;

    this.#observer?.disconnect();
    this.#observer = null;
    this.#observedRoots = null;

    this.#fieldsById.clear();
    this.#fieldsById = null;

    this.#fieldIds = null;

    this.#onFormUpdate = null;
    this.#onFieldOutcomes = null;

    this.#filledFields.clear();
    this.#filledFields = null;

    this.#formUpdateTimeout = null;
    this.#formUpdateAffectedGroupsMap = null;
  }

  /**
   * Gets the form data in a serializable form in the schema
   * expected by the LLM.
   *
   * @returns {Array<FormData>}
   */
  getFormData() {
    const forms = this.#getForms();

    return forms.map(({ formId: id, formFields }) => {
      const fields = formFields.map(formField => this.#getFieldData(formField));

      return { id, fields };
    });
  }

  /**
   * Gets the focused form and its empty field IDs. Null when the focused
   * field belongs to no tracked form, or to one that no longer has enough
   * fields the user can edit for Smart Form Fill to handle it.
   *
   * @returns {FocusedForm | null}
   */
  getFocusedForm() {
    const field = this.#getFocusedField();
    if (!field) {
      return null;
    }

    const rootElement = lazy.FormLikeFactory.findRootForField(field);
    const group = this.#formRoots.get(rootElement);
    if (
      !group?.formId ||
      !group.fields.includes(field) ||
      !this.#hasEnoughEditableFields(group)
    ) {
      return null;
    }

    const emptyFieldIds = new Set(
      group.fields
        .filter(
          formField =>
            this.#isSupportedField(formField) &&
            this.#isFillableField(formField)
        )
        .map(formField => this.#getFieldId(formField))
    );

    const formData = this.getFormData().find(({ id }) => id === group.formId);
    if (!formData) {
      return null;
    }

    return {
      id: formData.id,
      fields: formData.fields,
      emptyFieldIds,
      focusedFieldId: this.#getFieldId(field),
    };
  }

  /**
   * Fills form fields, allows the operation to be cancelled between fields.
   *
   * @param {object} param
   * @param {string} param.id The stable Form ID
   * @param {Array<{id: string, value: string}>} param.fields
   *   The reviewed fill instructions.
   *
   * @returns {Promise<FillFormOperationResult>}
   *   The result of the filling operation.
   */
  async fillForm({ id, fields }) {
    const group = this.#forms?.get(id);

    if (!group || !Array.isArray(fields)) {
      return {
        hasErrors: false,
        cancelled: false,
        filledFieldCount: 0,
      };
    }

    const generation = ++this.#fillGeneration;
    let hasErrors = false;
    let cancelled = false;
    const filledFieldIds = new Set();

    Services.obs.notifyObservers(null, "autofill-fill-starting");

    try {
      const formFields = new Set(group.fields);

      for (const { id: fieldId, value } of fields) {
        // stopFilling() invalidates the generation captured by this operation.
        if (this.#destroyed || generation !== this.#fillGeneration) {
          cancelled = true;
          break;
        }

        if (typeof value === "string" && !filledFieldIds.has(fieldId)) {
          const field = this.#fieldsById.get(fieldId);

          if (field && field.isConnected && formFields.has(field)) {
            let valid = false;
            try {
              valid =
                lazy.FormLikeFactory.findRootForField(field) ===
                  group.formLike.rootElement &&
                this.#isSupportedField(field) &&
                this.#isFillableField(field);
            } catch (error) {
              // Validation errors intentionally skip the field without making
              // the overall fill operation fail.
              lazy.console.error(
                "Could not validate Smart Form Fill field",
                error
              );
            }

            if (valid) {
              try {
                field.setUserInput(value);
                filledFieldIds.add(fieldId);
              } catch (error) {
                hasErrors = true;
                lazy.console.error(
                  "Could not fill Smart Form Fill field",
                  error
                );
              }

              if (filledFieldIds.has(fieldId)) {
                try {
                  field.autofillState =
                    lazy.FormAutofillUtils.FIELD_STATES.AUTO_FILLED;
                } catch (error) {
                  lazy.console.error(
                    "Could not mark Smart Form Fill field as autofilled",
                    error
                  );
                }

                // Tracked after setUserInput, so the input event it dispatches is not
                // mistaken for the user typing.
                this.#filledFields.set(fieldId, {
                  formId: id,
                  edited: false,
                  length: value.length,
                });
              }
            }
          }
        }

        // Cancelling can leave the form partly filled. If another await is added before
        // filling a field, follow it with:
        // if (this.#destroyed || generation !== this.#fillGeneration) {
        //   cancelled = true;
        //   break;
        // }
        await this.#allowCancellationCheck();

        if (this.#destroyed || generation !== this.#fillGeneration) {
          cancelled = true;
          break;
        }
      }

      this.#onFieldsFilled?.({ id, fieldIds: [...filledFieldIds] });
    } finally {
      Services.obs.notifyObservers(null, "autofill-fill-complete");
    }

    return {
      hasErrors,
      cancelled,
      filledFieldCount: filledFieldIds.size,
    };
  }

  /**
   * Cancels the current form-filling operation.
   *
   * This is not currently exposed in the UI because filling generally
   * completes too quickly for a stop action to be useful. It remains available
   * in case cancellation UX is added later.
   *
   * @returns {void}
   */
  stopFilling() {
    ++this.#fillGeneration;
  }

  /**
   * Allows pending cancellation messages to be processed before the next check.
   *
   * @returns {Promise<void>}
   */
  #allowCancellationCheck() {
    return new Promise(resolve => Services.tm.dispatchToMainThread(resolve));
  }

  /**
   * Routes the events that tell what became of a filled field.
   *
   * @param {Event} event
   */
  handleEvent(event) {
    // Nothing was filled, so no event can end a fill or edit one.
    if (!this.#filledFields?.size) {
      return;
    }

    switch (event.type) {
      case "input":
        this.#onFilledFieldInput(event.target);
        break;

      // The user left one filled field.
      case "focusout":
        this.#reportOutcomes(field => field === event.target);
        break;

      // Every filled field of the submitted form is done.
      case "submit":
        this.#reportOutcomes(
          field => lazy.FormLikeFactory.findRootForField(field) === event.target
        );
        break;

      // The page is going away, so nothing else will be reported.
      case "pagehide":
        this.#reportOutcomes(() => true);
        break;
    }
  }

  /**
   * Watches for the user editing a filled field and for the events that end a
   * fill. Listens in the system group so page scripts cannot hide them by
   * stopping propagation.
   *
   * @private
   */
  #monitorFilledFields() {
    const options = { mozSystemGroup: true };

    this.#doc.addEventListener("input", this, options);
    this.#doc.addEventListener("focusout", this, options);
    this.#doc.addEventListener("submit", this, options);
    this.#doc.defaultView?.addEventListener("pagehide", this, options);
  }

  /**
   * @private
   */
  #stopMonitoringFilledFields() {
    const options = { mozSystemGroup: true };

    this.#doc.removeEventListener("input", this, options);
    this.#doc.removeEventListener("focusout", this, options);
    this.#doc.removeEventListener("submit", this, options);
    this.#doc.defaultView?.removeEventListener("pagehide", this, options);
  }

  /**
   * Marks a filled field as edited by the user.
   *
   * @param {HTMLElement} target
   *
   * @private
   */
  #onFilledFieldInput(target) {
    const fieldId = this.#fieldIds.get(target);
    const filled = fieldId && this.#filledFields.get(fieldId);

    if (filled) {
      filled.edited = true;
    }
  }

  /**
   * Reports the state of the filled fields the predicate matches, and stops
   * tracking them, so each field is reported once by whichever event ends its
   * fill first. Reports what the page saw; naming those states is up to the
   * parent process.
   *
   * @param {(field: HTMLElement) => boolean} matches
   *
   * @private
   */
  #reportOutcomes(matches) {
    if (!this.#onFieldOutcomes) {
      return;
    }

    const statesByFormId = new Map();

    for (const [fieldId, { formId, edited, length }] of this.#filledFields) {
      const field = this.#fieldsById.get(fieldId);
      if (!field || !matches(field)) {
        continue;
      }

      if (!statesByFormId.has(formId)) {
        statesByFormId.set(formId, []);
      }
      statesByFormId.get(formId).push({
        id: fieldId,
        edited,
        isEmpty: field.value.trim() === "",
        filledLength: length,
        finalLength: field.value.length,
      });

      this.#filledFields.delete(fieldId);
    }

    for (const [formId, fields] of statesByFormId) {
      this.#onFieldOutcomes({ id: formId, fields });
    }
  }

  /**
   * Gets list of supported fields that were detected
   *
   * @returns {Array<SmartFormFillField>}
   */
  getSupportedFields() {
    return Array.from(this.#forms.values()).flatMap(({ fields }) =>
      fields.filter(field => this.#isSupportedField(field))
    );
  }

  /**
   * Whether the field is current supported
   *
   * @param {HTMLElement} field
   *
   * @returns {boolean}
   */
  isSupportedField(field) {
    return this.#isSupportedField(field);
  }

  /**
   * Whether Smart Form Fill should offer to fill a field. Three things have to
   * hold: the field is of a supported type, it is still empty, and it belongs
   * to a form group Smart Form Fill handles.
   *
   * @param {HTMLElement} field
   *
   * @returns {boolean}
   */
  shouldOfferFill(field) {
    if (!this.#isSupportedField(field) || !this.#isFillableField(field)) {
      return false;
    }

    let group;
    try {
      group = this.#formRoots.get(lazy.FormLikeFactory.findRootForField(field));
    } catch {
      return false;
    }

    return !!group?.formId && this.#hasEnoughEditableFields(group);
  }

  /**
   * Serializes a form field for model requests.
   *
   * @param {SffFormField} formField
   *
   * @returns {FieldData}
   *
   * @private
   */
  #getFieldData({ fieldId: id, field, details }) {
    return {
      id,
      label: this.#getFieldLabel(field),
      name: field.name,
      formHistoryName: field.name || field.id,
      inputType: field.type,
      placeholder: field.placeholder,
      autocomplete: field.autocomplete,
      maxlength: field.maxLength > -1 ? field.maxLength : null,
      options: [],
      textBefore: this.#utils.findNearbyText(field),
      textAfter: this.#utils.findNearbyText(field, false),
      ...(details?.fieldName && {
        localGuess: details.fieldName,
        localSource: details.reason,
      }),
      ...(details?.confidence && {
        localConfidence: details.confidence,
      }),
    };
  }

  /**
   * Normalize values that have repeated spaces or need trim
   *
   * @param {string | null | undefined} value
   *
   * @returns {string} Normalized string
   */
  #normalize(value) {
    return value?.replace(/\s+/g, " ").trim() ?? "";
  }

  /**
   * Gets a label for the field, either by associated label,
   * aria-labelledby, or aria-label
   *
   * @param {SmartFormFillField} field
   *
   * @returns {string}
   */
  #getFieldLabel(field) {
    let label = Array.from(field.labels ?? [])
      .map(labelEl => this.#normalize(labelEl.textContent))
      .filter(Boolean)
      .join(" ");

    if (label) {
      return label;
    }

    const root = field.getRootNode();
    label = field
      .getAttribute("aria-labelledby")
      ?.split(/\s+/)
      .map(id => this.#normalize(root.getElementById?.(id)?.textContent))
      .filter(Boolean)
      .join(" ");

    return label || this.#normalize(field.getAttribute("aria-label"));
  }

  /**
   * @param {SmartFormFillField} field
   *
   * @returns {string}
   */
  #getFieldId(field) {
    let fieldId = this.#fieldIds.get(field);

    if (!fieldId) {
      fieldId = `f_${++this.#fieldCounter}`;

      this.#fieldIds.set(field, fieldId);
      this.#fieldsById.set(fieldId, field);
    }

    return fieldId;
  }

  /**
   * Gets the Smart Form Fill supported fields for
   * each form and any available field details.
   *
   * @returns {Array<SffForm>}
   */
  #getForms() {
    const forms = [];

    for (const [formId, formGroup] of this.#forms.entries()) {
      const { fields, fieldDetailsList = [] } = formGroup;
      const fieldMap = this.#toFieldMap(fieldDetailsList);

      const formFields = fields
        .filter(field => this.#isSupportedField(field))
        .map(field => {
          const fieldId = this.#getFieldId(field);
          const details = fieldMap.get(field) ?? null;
          return { fieldId, field, details };
        });

      if (formFields.length) {
        forms.push({ formId, formFields });
      }
    }

    return forms;
  }

  /**
   * Converts array of FieldDetail objects to a Map for lookups
   *
   * @param {Array<FieldDetail>} fieldDetailsList
   *
   * @returns {Map<HTMLElement, FieldDetail>}
   *
   * @private
   */
  #toFieldMap(fieldDetailsList) {
    return new Map(
      fieldDetailsList.map(fieldDetails => [fieldDetails.element, fieldDetails])
    );
  }

  /**
   * Checks if the element is supported by Smart Form Fill
   *
   * @param {HTMLElement} element
   *
   * @returns {boolean}
   *
   * @private
   */
  #isSupportedField(element) {
    // A combobox input with no name is the widget's filter box rather than the
    // value it submits, so filling it leaves the selection untouched. Sites
    // that style a real <select> instead of rebuilding one are already out.
    if (element?.closest?.('[role="combobox"]') && !element.name) {
      return false;
    }

    return (
      HTMLTextAreaElement.isInstance(element) ||
      (HTMLInputElement.isInstance(element) &&
        SUPPORTED_INPUT_TYPES.includes(element.type))
    );
  }

  /**
   * Gets the focused supported field.
   *
   * @returns {SmartFormFillField | null}
   *
   * @private
   */
  #getFocusedField() {
    let field = this.#doc.activeElement;

    while (field?.shadowRoot?.activeElement) {
      field = field.shadowRoot.activeElement;
    }

    return this.#isSupportedField(field) ? field : null;
  }

  /**
   * Checks whether the user can currently edit a field.
   *
   * @param {SmartFormFillField} field
   *
   * @returns {boolean}
   *
   * @private
   */
  #isEditableField(field) {
    return (
      lazy.FormAutofillUtils.isFieldVisible(field) &&
      !field.disabled &&
      !field.readOnly
    );
  }

  /**
   * Checks whether a field can currently be filled.
   *
   * @param {SmartFormFillField} field
   *
   * @returns {boolean}
   *
   * @private
   */
  #isFillableField(field) {
    return field.value === "" && this.#isEditableField(field);
  }

  /**
   * Initializes the MutationObserver used to track fields added to or
   * removed from the document and discovered shadow roots.
   *
   * @private
   */
  #monitorDocument() {
    this.#observer = new this.#doc.defaultView.MutationObserver(mutations => {
      this.#onMutation(mutations);
    });

    // Observed on the document rather than its element: a framework that
    // hydrates the whole document can replace documentElement, which would
    // strand the observer on a detached tree and stop Smart Form Fill from
    // ever seeing the replacement fields.
    this.#observeRoot(this.#doc);
  }

  /**
   * Adds an element as a root to be observed for changes
   * so shadow roots can be observed for fields.
   *
   * @param {HTMLElement | ShadowRoot} root
   *
   * @private
   */
  #observeRoot(root) {
    if (this.#observedRoots.has(root)) {
      return;
    }

    this.#observedRoots.add(root);
    this.#observer.observe(root, MUTATION_OBSERVER_OPTIONS);
  }

  /**
   * Removes fields that are disconnected or no longer associated with their
   * tracked form root, and adds changed groups to the affected groups map.
   *
   * @param {Map<HTMLElement, FormGroup>} affectedGroups
   *
   * @private
   */
  #removeStaleFields(affectedGroups) {
    for (const [rootElement, group] of this.#formRoots) {
      const currentFields = group.fields.filter(
        field =>
          field.isConnected &&
          lazy.FormLikeFactory.findRootForField(field) === rootElement
      );

      if (currentFields.length !== group.fields.length) {
        // Clear IDs for fields no longer in group
        const currentFieldSet = new Set(currentFields);
        for (const field of group.fields) {
          if (currentFieldSet.has(field)) {
            continue;
          }

          const fieldId = this.#fieldIds.get(field);
          if (fieldId) {
            this.#fieldsById.delete(fieldId);
            this.#fieldIds.delete(field);
          }
        }

        group.fields.splice(0, group.fields.length, ...currentFields);
        affectedGroups.set(rootElement, group);
      }
    }
  }

  /**
   * Collects fields recursively from element nodes added by a
   * mutation, including fields in shadow roots.
   *
   * @param {MutationRecord} mutation
   *
   * @returns {Array<SmartFormFillField>}
   *
   * @private
   */
  #getAddedFields(mutation) {
    const addedFields = Array.from(mutation.addedNodes)
      .filter(addedNode => !!addedNode.matches)
      .flatMap(addedNode => Array.from(this.#getFields(addedNode)));

    return addedFields;
  }

  /**
   * Adds a field to its existing form group, or creates and registers a new
   * group for the field's root element.
   *
   * @param {SmartFormFillField} field
   *
   * @returns {FormGroup}
   *
   * @private
   */
  #addFieldToGroup(field) {
    const formLike = lazy.FormLikeFactory.createFromField(field);

    let group = this.#formRoots.get(formLike.rootElement);

    if (!group) {
      group = { formLike, fields: [] };

      Object.defineProperty(formLike, "elements", {
        value: group.fields,
        enumerable: true,
      });

      this.#formRoots.set(formLike.rootElement, group);
    }

    if (!group.fields.includes(field)) {
      group.fields.push(field);
    }

    return group;
  }

  /**
   * Check if a form group has the minimum number of fields of a type Smart
   * Form Fill supports. Field types do not change, so this answer only goes
   * stale when fields are added or removed, which is observed.
   *
   * @param {FormGroup} group
   *
   * @returns {boolean}
   *
   * @private
   */
  #hasEnoughSupportedFields(group) {
    const supportedFieldCount = group.fields.reduce((count, field) => {
      if (this.#isSupportedField(field)) {
        count += 1;
      }

      return count;
    }, 0);

    return supportedFieldCount >= lazy.MIN_FORM_FIELDS;
  }

  /**
   * Check if a form group still has the minimum number of fields the user can
   * edit right now. Answered on demand rather than cached, because the CSS
   * that hides a field changes without a mutation to observe.
   *
   * @param {FormGroup} group
   *
   * @returns {boolean}
   *
   * @private
   */
  #hasEnoughEditableFields(group) {
    const editableFieldCount = group.fields.reduce((count, field) => {
      if (this.#isSupportedField(field) && this.#isEditableField(field)) {
        count += 1;
      }

      return count;
    }, 0);

    return editableFieldCount >= lazy.MIN_FORM_FIELDS;
  }

  /**
   * Reclassifies affected form groups and registers the ones that meet the
   * minimum field count. Groups that no longer meet it are unregistered, so
   * nothing else in Smart Form Fill can reach them, but stay tracked until
   * they are left without fields.
   *
   * @param {Map<HTMLElement, FormGroup>} affectedGroups
   *
   * @private
   */
  #updateFormGroups(affectedGroups) {
    for (const affectedGroup of affectedGroups.values()) {
      if (!this.#hasEnoughSupportedFields(affectedGroup)) {
        if (affectedGroup.formId) {
          this.#forms.delete(affectedGroup.formId);
          affectedGroup.formId = undefined;
        }

        if (!affectedGroup.fields.length) {
          this.#formRoots.delete(affectedGroup.formLike.rootElement);
        }
        continue;
      }

      const fieldDetailsList = lazy.FormAutofillHeuristics.getFormInfo(
        affectedGroup.formLike,
        false
      );

      if (!affectedGroup.formId) {
        const formId = `form_${++this.#formCounter}`;
        affectedGroup.formId = formId;

        this.#forms.set(formId, affectedGroup);
      }

      affectedGroup.fieldDetailsList = fieldDetailsList;
    }
  }

  /**
   * Handles document mutations by removing stale fields, tracking newly added
   * fields, and updating classification data for affected form groups.
   *
   * @param {Array<MutationRecord>} mutations
   *
   * @private
   */
  #onMutation(mutations) {
    const affectedGroups = new Map();

    if (
      mutations.some(
        mutation =>
          mutation.type === "childList" && mutation.removedNodes.length
      )
    ) {
      this.#removeStaleFields(affectedGroups);
    }

    for (const mutation of mutations) {
      if (mutation.type !== "childList") {
        continue;
      }

      const addedFields = this.#getAddedFields(mutation);

      for (const addedField of addedFields) {
        if (!addedField.isConnected) {
          continue;
        }

        const group = this.#addFieldToGroup(addedField);
        affectedGroups.set(group.formLike.rootElement, group);
      }
    }

    if (!affectedGroups.size) {
      return;
    }

    if (this.#formUpdateTimeout) {
      this.#doc.defaultView.clearTimeout(this.#formUpdateTimeout);
    }

    this.#formUpdateAffectedGroupsMap = new Map([
      ...(this.#formUpdateAffectedGroupsMap ?? []),
      ...affectedGroups,
    ]);

    this.#formUpdateTimeout = this.#doc.defaultView.setTimeout(() => {
      const groups = this.#formUpdateAffectedGroupsMap;
      this.#formUpdateAffectedGroupsMap = null;

      this.#formUpdateTimeout = null;

      this.#triggerFormUpdate(groups);
    }, 300);
  }

  /**
   * Triggers the form updates
   *
   * @param {Map<HTMLElement, FormGroup>} affectedGroups
   */
  #triggerFormUpdate(affectedGroups) {
    this.#utils.clearCache();
    this.#updateFormGroups(affectedGroups);

    if (typeof this.#onFormUpdate === "function") {
      this.#onFormUpdate(this.getFormData());
    }
  }

  /**
   * Detects, groups, and locally classifies fields present during initialization.
   *
   * @returns {Promise<void>}
   *
   * @private
   */
  async #detectFields() {
    for (const field of this.#getFields(this.#doc)) {
      this.#addFieldToGroup(field);
    }

    this.#updateFormGroups(this.#formRoots);
  }

  /**
   * Recursively yields Smart Form Fill fields from a document, element, or
   * shadow root and registers discovered open shadow roots for observation.
   *
   * @param {Document | Element | ShadowRoot} rootElement
   * @yields {SmartFormFillField}
   *
   * @private
   */
  *#getFields(rootElement) {
    if (rootElement.matches && rootElement.matches(INPUTS_SELECTOR)) {
      yield rootElement;
    }

    if (rootElement.shadowRoot) {
      this.#observeRoot(rootElement.shadowRoot);
      yield* this.#getFields(rootElement.shadowRoot);
    }

    for (const element of rootElement.querySelectorAll("*")) {
      if (element.matches(INPUTS_SELECTOR)) {
        yield element;
      }

      if (element.shadowRoot) {
        this.#observeRoot(element.shadowRoot);
        yield* this.#getFields(element.shadowRoot);
      }
    }
  }
}
