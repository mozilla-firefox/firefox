/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  FormAutofill: "resource://autofill/FormAutofill.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  MLAutofill: "resource://autofill/MLAutofill.sys.mjs",
});

/**
 * Represents the detailed information about a form field, including
 * the inferred field name, the approach used for inferring, and additional metadata.
 */
export class FieldDetail {
  // Reference to the elemenet
  elementWeakRef = null;

  // The identifier generated via ContentDOMReference for the associated DOM element
  // of this field
  elementId = null;

  // The identifier generated via ContentDOMReference for the root element of
  // this field
  rootElementId = null;

  // If the element is an iframe, it is the id of the BrowsingContext of the iframe,
  // Otherwise, it is the id of the BrowsingContext the element is in
  browsingContextId = null;

  // string with `${element.id}/{element.name}`. This is only used for debugging.
  identifier = "";

  // tag name attribute of the element
  localName = null;

  // The inferred field name for this element.
  fieldName = null;

  // The approach we use to infer the information for this element
  // The possible values are "autocomplete", "fathom", and "regex-heuristic"
  reason = null;

  // This field could be a lookup field, for example, one that could be used to
  // search for an address or postal code and fill in other fields.
  isLookup = false;

  /*
   * The "section", "addressType", and "contactType" values are
   * used to identify the exact field when the serializable data is received
   * from the backend.  There cannot be multiple fields which have
   * the same exact combination of these values.
   */

  // Which section the field belongs to. The value comes from autocomplete attribute.
  // See https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#autofill-detail-tokens for more details
  section = "";
  addressType = "";
  contactType = "";
  credentialType = "";

  // When a field is split into N fields, we use part to record which field it is
  // For example, a credit card number field is split into 4 fields, the value of
  // "part" for the first cc-number field is 1, for the last one is 4.
  // If the field is not split, the value is null
  part = null;

  // Confidence value when the field name is inferred by "fathom"
  confidence = null;

  constructor(element) {
    this.elementWeakRef = new WeakRef(element);
  }

  get element() {
    return this.elementWeakRef.deref();
  }

  /**
   * Convert FieldDetail class to an object that is suitable for
   * sending over IPC. Avoid using this in other case.
   */
  toVanillaObject() {
    const json = { ...this };
    delete json.elementWeakRef;
    return json;
  }

  static fromVanillaObject(obj) {
    const element = lazy.FormAutofillUtils.getElementByIdentifier(
      obj.elementId
    );
    return element ? Object.assign(new FieldDetail(element), obj) : null;
  }

  static create(
    element,
    form,
    fieldName = null,
    {
      autocompleteInfo = null,
      fathomLabel = null,
      fathomConfidence = null,
      isVisible = true,
      mlHeaderInput = null,
      mlButtonInput = null,
      isLookup = false,
    } = {}
  ) {
    const fieldDetail = new FieldDetail(element);

    fieldDetail.elementId =
      lazy.FormAutofillUtils.getElementIdentifier(element);
    fieldDetail.rootElementId = lazy.FormAutofillUtils.getElementIdentifier(
      form.rootElement
    );
    fieldDetail.identifier = `${element.id}/${element.name}`;
    fieldDetail.localName = element.localName;

    if (Array.isArray(fieldName)) {
      fieldDetail.fieldName = fieldName[0] ?? "";
      fieldDetail.alternativeFieldName = fieldName[1] ?? "";
    } else {
      fieldDetail.fieldName = fieldName;
    }

    if (!fieldDetail.fieldName) {
      fieldDetail.reason = "unknown";
    } else if (autocompleteInfo) {
      fieldDetail.reason = "autocomplete";
      fieldDetail.section = autocompleteInfo.section;
      fieldDetail.addressType = autocompleteInfo.addressType;
      fieldDetail.contactType = autocompleteInfo.contactType;
      fieldDetail.credentialType = autocompleteInfo.credentialType;
      fieldDetail.sectionName =
        autocompleteInfo.section || autocompleteInfo.addressType;
    } else if (fathomConfidence) {
      fieldDetail.reason = "fathom";
      fieldDetail.confidence = fathomConfidence;

      // TODO: This should be removed once we support reference field info across iframe.
      // Temporarily add an addtional "the field is the only visible input" constraint
      // when determining whether a form has only a high-confidence cc-* field a valid
      // credit card section. We can remove this restriction once we are confident
      // about only using fathom.
      fieldDetail.isOnlyVisibleFieldWithHighConfidence = false;
      if (
        fieldDetail.confidence >
        lazy.FormAutofillUtils.ccFathomHighConfidenceThreshold
      ) {
        const root = element.form || element.ownerDocument;
        const inputs = root.querySelectorAll("input:not([type=hidden])");
        if (inputs.length == 1 && inputs[0] == element) {
          fieldDetail.isOnlyVisibleFieldWithHighConfidence = true;
        }
      }
    } else {
      fieldDetail.reason = "regex-heuristic";
    }

    try {
      fieldDetail.browsingContextId =
        element.localName == "iframe"
          ? element.browsingContext.id
          : BrowsingContext.getFromWindow(element.ownerGlobal).id;
    } catch {
      /* unit test doesn't have ownerGlobal */
    }

    fieldDetail.isVisible = isVisible;

    // Info required by heuristics
    fieldDetail.maxLength = element.maxLength;

    if (
      lazy.FormAutofill.isMLExperimentEnabled &&
      ["input", "select"].includes(element.localName)
    ) {
      fieldDetail.mlinput = lazy.MLAutofill.getMLMarkup(fieldDetail.element);
      fieldDetail.mlHeaderInput = mlHeaderInput;
      fieldDetail.mlButtonInput = mlButtonInput;
      fieldDetail.fathomLabel = fathomLabel;
      fieldDetail.fathomConfidence = fathomConfidence;
    }

    fieldDetail.isLookup = isLookup;

    return fieldDetail;
  }
}

/**
 * A scanner for traversing all elements in a form. It also provides a
 * cursor (parsingIndex) to indicate which element is waiting for parsing.
 *
 * The scanner retrives the field detail by calling heuristics handlers
 * `inferFieldInfo` function.
 */
export class FieldScanner {
  #parsingIndex = 0;

  #fieldDetails = [];

  /**
   * Create a FieldScanner based on form elements with the existing
   * fieldDetails.
   *
   * @param {Array<FieldDetails>} fieldDetails
   *        An array of fieldDetail object to be scanned.
   */
  constructor(fieldDetails) {
    this.#fieldDetails = fieldDetails;
  }

  /**
   * This cursor means the index of the element which is waiting for parsing.
   *
   * @returns {number}
   *          The index of the element which is waiting for parsing.
   */
  get parsingIndex() {
    return this.#parsingIndex;
  }

  get parsingFinished() {
    return this.parsingIndex >= this.#fieldDetails.length;
  }

  /**
   * Move the parsingIndex to the next elements. Any elements behind this index
   * means the parsing tasks are finished.
   *
   * @param {number} index
   *        The latest index of elements waiting for parsing.
   */
  set parsingIndex(index) {
    if (index > this.#fieldDetails.length) {
      throw new Error("The parsing index is out of range.");
    }
    this.#parsingIndex = index;
  }

  /**
   * Retrieve the field detail by the index. If the field detail is not ready,
   * the elements will be traversed until matching the index.
   *
   * @param {number} index
   *        The index of the element that you want to retrieve.
   * @returns {object}
   *          The field detail at the specific index.
   */
  getFieldDetailByIndex(index) {
    if (index >= this.#fieldDetails.length) {
      return null;
    }

    return this.#fieldDetails[index];
  }

  /**
   * When a field detail should be changed its fieldName after parsing, use
   * this function to update the fieldName which is at a specific index.
   *
   * @param {number} index
   *        The index indicates a field detail to be updated.
   * @param {string} fieldName
   *        The new name of the field
   * @param {boolean} [ignoreAutocomplete=false]
   *        Whether to change the field name when the field name is determined by
   *        autocomplete attribute
   */
  updateFieldName(index, fieldName, ignoreAutocomplete = false) {
    if (index >= this.#fieldDetails.length) {
      throw new Error("Try to update the non-existing field detail.");
    }

    const fieldDetail = this.#fieldDetails[index];
    if (fieldDetail.fieldName == fieldName) {
      return;
    }

    if (!ignoreAutocomplete && fieldDetail.reason == "autocomplete") {
      return;
    }

    fieldDetail.fieldName = fieldName;
    fieldDetail.reason = "update-heuristic";
  }

  elementExisting(index) {
    return index < this.#fieldDetails.length;
  }
}

export default FieldScanner;
