/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  AutofillTelemetry: "resource://gre/modules/shared/AutofillTelemetry.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  FormAutofill: "resource://autofill/FormAutofill.sys.mjs",
  formAutofillStorage: "resource://autofill/FormAutofillStorage.sys.mjs",
});

class FormSection {
  #fieldDetails = [];

  #name = "";

  constructor(fieldDetails) {
    if (!fieldDetails.length) {
      throw new TypeError("A section should contain at least one field");
    }

    fieldDetails.forEach(field => this.addField(field));

    // The section type is the data type of its first recognized field.
    for (const fieldDetail of fieldDetails) {
      const typeId = lazy.AutofillDataTypes.typeIdForFieldName(
        fieldDetail.fieldName
      );
      if (typeId) {
        this.type = typeId;
        break;
      }
    }

    // Unknown-only sections default to address.
    this.type ||= lazy.AutofillDataTypes.ADDRESS;
  }

  get fieldDetails() {
    return this.#fieldDetails;
  }

  get name() {
    return this.#name;
  }

  addField(fieldDetail) {
    this.#name ||= fieldDetail.sectionName;
    this.#fieldDetails.push(fieldDetail);
  }
}

export class FormAutofillSection {
  /**
   * Record information for fields that are in this section
   */
  #fieldDetails = [];

  constructor(fieldDetails) {
    this.#fieldDetails = fieldDetails;

    ChromeUtils.defineLazyGetter(this, "log", () =>
      lazy.FormAutofill.defineLogGetter(this, "FormAutofillSection")
    );

    // Identifier used to correlate events relating to the same form
    this.flowId = Services.uuid.generateUUID().toString();
    this.log.debug(
      "Creating new credit card section with flowId =",
      this.flowId
    );
  }

  get fieldDetails() {
    return this.#fieldDetails;
  }

  get allFieldNames() {
    return this.fieldDetails.map(field => field.fieldName);
  }

  /**
   * Examine the section is a valid section or not based on its fieldDetails or
   * other information. This method must be overrided.
   *
   * @returns {boolean} True for a valid section, otherwise false
   */
  isValidSection() {
    throw new TypeError("isValidSection method must be overrided");
  }

  /**
   * Examine the section is an enabled section type or not based on its
   * preferences.
   *
   * @returns {boolean} True for an enabled section type, otherwise false
   */
  isEnabled() {
    return lazy.FormAutofill.isAutofillTypeEnabled(this.type);
  }

  /**
   * Examine the section is createable for storing the profile. This method
   * must be overrided.
   *
   * @param {object} _record The record for examining createable
   * @returns {boolean} True for the record is createable, otherwise false
   */
  isRecordCreatable(_record) {
    throw new TypeError("isRecordCreatable method must be overridden");
  }

  /**
   * Override this method if the profile is needed to be customized for
   * previewing values.
   *
   * @param {object} _profile
   *        A profile for pre-processing before previewing values.
   * @returns {boolean} Whether the profile should be previewed.
   */
  preparePreviewProfile(_profile) {
    return true;
  }

  /**
   * Override this method if the profile is needed to be customized for filling
   * values.
   *
   * @param {object} _profile
   *        A profile for pre-processing before filling values.
   * @returns {boolean} Whether the profile should be filled.
   */
  async prepareFillingProfile(_profile) {
    return true;
  }

  /**
   * The result is an array contains the sections with its belonging field details.
   *
   * @param   {Array<FieldDetails>} fieldDetails field detail array to be classified
   * @param   {object} options
   * @param   {boolean} [options.ignoreInvalidSection = false]
   *          True to keep invalid section in the return array. Only used by tests now
   * @param   {boolean} [options.ignoreUnknownField = true]
   *          False to keep unknown field in a section. Only used by developer tools now
   * @returns {Array<FormSection>} The array with the sections.
   */
  static classifySections(
    fieldDetails,
    { ignoreInvalidSection = false, ignoreUnknownField = true } = {}
  ) {
    // Bucket fields by their data type id, preserving order. 'current' refers
    // to the last bucket a field was added to; it determines the appropriate
    // bucket for unknown fields, defaulting to the address bucket for
    // simplicity.
    const fieldsByType = new Map(
      lazy.AutofillDataTypes.all.map(type => [type.id, []])
    );
    let current = fieldsByType.get(lazy.AutofillDataTypes.ADDRESS);
    for (const fieldDetail of fieldDetails) {
      const typeId = lazy.AutofillDataTypes.typeIdForFieldName(
        fieldDetail.fieldName
      );
      if (typeId) {
        current = fieldsByType.get(typeId);
      } else if (ignoreUnknownField) {
        continue;
      }
      current.push(fieldDetail);
    }

    const sections = [...fieldsByType.values()]
      .flatMap(fields => FormAutofillSection.groupFields(fields))
      .sort(
        (a, b) =>
          fieldDetails.indexOf(a.fieldDetails[0]) -
          fieldDetails.indexOf(b.fieldDetails[0])
      );

    const autofillableSections = [];
    for (const section of sections) {
      if (!section.fieldDetails.length) {
        continue;
      }

      const SectionClass = this.#sectionClassForType(section.type);
      const autofillableSection = new SectionClass(section.fieldDetails);

      if (ignoreInvalidSection && !autofillableSection.isValidSection()) {
        continue;
      }

      autofillableSections.push(autofillableSection);
    }
    return autofillableSections;
  }

  /**
   * Returns the concrete section class that handles a data type id, defaulting
   * to the address section for unknown types.
   *
   * @param {string} typeId An AutofillDataTypes id.
   * @returns {typeof FormAutofillSection}
   */
  static #sectionClassForType(typeId) {
    switch (typeId) {
      case lazy.AutofillDataTypes.CREDIT_CARD:
        return FormAutofillCreditCardSection;
      case lazy.AutofillDataTypes.PASSPORT:
        return FormAutofillPassportSection;
      default:
        return FormAutofillAddressSection;
    }
  }

  /**
   * Groups fields into sections based on:
   * 1. Their `sectionName` attribute.
   * 2. Whether the section already contains a field with the same `fieldName`,
   *    If so, a new section is created.
   *
   * @param {Array} fieldDetails An array of field detail objects.
   * @returns {Array} An array of FormSection objects.
   */
  static groupFields(fieldDetails) {
    let sections = [];

    // If there is only one duplicate overall, don't split that one field into
    // a separate section.
    let foundSectionName = false;
    let dontAddToNewSections = false;
    let numDuplicates = 0;
    let fieldTypes = new Set();
    for (let i = 0; i < fieldDetails.length; i++) {
      const cur = fieldDetails[i];
      // If any field has a section name specified, we should always add to
      // new sections.
      if (cur.sectionName) {
        foundSectionName = true;
        break;
      }

      if (cur.isVisible && fieldTypes.has(cur.fieldName)) {
        numDuplicates++;
      }
      fieldTypes.add(cur.fieldName);
    }

    if (!foundSectionName && numDuplicates <= 1) {
      dontAddToNewSections = true;
    }

    for (let i = 0; i < fieldDetails.length; i++) {
      const cur = fieldDetails[i];
      const [currentSection] = sections.slice(-1);

      // The section this field might be placed into.
      let candidateSection = null;

      // Use name group from autocomplete attribute (ex, section-xxx) to look for the section
      // we might place this field into.
      // If the field doesn't have a section name, the candidate section is the previous section.
      if (!currentSection || !cur.sectionName) {
        candidateSection = currentSection;
      } else if (cur.sectionName) {
        // If the field has a section name, the candidate section is the nearest section that
        // either shares the same name or lacks a name.
        for (let idx = sections.length - 1; idx >= 0; idx--) {
          if (!sections[idx].name || sections[idx].name == cur.sectionName) {
            candidateSection = sections[idx];
            break;
          }
        }
      }

      if (candidateSection) {
        let isDuplicate = false;
        if (!dontAddToNewSections) {
          // The field will still be placed in a new section if it is a duplicate of
          // an existing field, unless it is a duplicate of the previous field. This
          // allows for fields that might commonly appear twice such as a verification
          // email field, an invisible field that appears next to the user-visible field,
          // and simple cases where a page error where a field name is reused twice.
          let dupIndex = candidateSection.fieldDetails.findIndex(
            f =>
              f.fieldName == cur.fieldName &&
              f.isVisible &&
              cur.isVisible &&
              !f.isLookup
          );
          isDuplicate = dupIndex != -1;

          if (isDuplicate) {
            const [last] = candidateSection.fieldDetails.slice(-1);
            if (last.fieldName == cur.fieldName) {
              isDuplicate = false;
            } else if (
              lazy.AutofillDataTypes.fieldToSubCategory[cur.fieldName] == "name"
            ) {
              // If the duplicate field is in the "name" category (e.g., family-name, given-name),
              // we check whether all fields starting from the first duplicate also belong to the
              // name category. If they do, we don't consider the field a duplicate, since name
              // fields often appear in groups like family-name + given-name.
              isDuplicate = !candidateSection.fieldDetails
                .slice(dupIndex)
                .every(
                  f =>
                    lazy.AutofillDataTypes.fieldToSubCategory[f.fieldName] ===
                    "name"
                );
            }
          }
        }

        if (!isDuplicate) {
          candidateSection.addField(cur);
          continue;
        }
      }

      // Create a new section
      sections.push(new FormSection([cur]));
    }

    return sections;
  }

  /**
   * Return the record that is converted from the element's value.
   * The `valueByElementId` is passed by the child process.
   *
   * @returns {object} object keyed by field name, and values are field values.
   */
  createRecord(formFilledData) {
    if (!this.fieldDetails.length) {
      return {};
    }

    const data = {
      flowId: this.flowId,
      record: {},
    };

    for (const detail of this.fieldDetails) {
      // The security code is historically never captured. The CVV prefs let it
      // be saved along with the card, the same way shouldAutofillField() gates
      // autofilling it.
      if (
        detail.fieldName == "cc-csc" &&
        !lazy.FormAutofill.isAutofillCreditCardCVVEnabled
      ) {
        continue;
      }
      const { filledValue } = formFilledData.get(detail.elementId) ?? {};

      if (
        !filledValue ||
        filledValue.length > lazy.FormAutofillUtils.MAX_FIELD_VALUE_LENGTH
      ) {
        // Keep the property and preserve more information for updating
        data.record[detail.fieldName] = "";
      } else if (detail.part > 1) {
        // If there are multiple parts for the same field, concatenate the values.
        // This is now used in cases where the credit card number field
        // is split into multiple fields.
        data.record[detail.fieldName] += filledValue;
      } else {
        data.record[detail.fieldName] = filledValue;
      }
    }

    if (!this.isRecordCreatable(data.record)) {
      return null;
    }

    return data;
  }

  shouldAutofillField(fieldDetail) {
    // The security code is historically never autofilled. The in-progress CVV
    // pref lets it participate; otherwise keep the previous behavior.
    if (
      fieldDetail.fieldName == "cc-csc" &&
      !lazy.FormAutofill.isAutofillCreditCardCVVEnabled
    ) {
      return false;
    }

    // When both visible and invisible elements exist, we only autofill the
    // visible element.
    if (!fieldDetail.isVisible) {
      return !this.fieldDetails.some(
        field => field.fieldName == fieldDetail.fieldName && field.isVisible
      );
    }

    // Only fill a street address lookup field if it is the only street
    // address related field in this section. Similarly, for postal code
    // fields.
    if (fieldDetail.isLookup) {
      const STREET_FIELDS = [
        "street-address",
        "address-line1",
        "address-line2",
        "address-line3",
      ];

      let INTERESTED_FIELDS = [];
      if (STREET_FIELDS.includes(fieldDetail.fieldName)) {
        INTERESTED_FIELDS = STREET_FIELDS;
      } else if (fieldDetail.fieldName == "postal-code") {
        INTERESTED_FIELDS = ["postal-code"];
      }

      if (
        INTERESTED_FIELDS.length &&
        this.fieldDetails.some(
          field =>
            INTERESTED_FIELDS.includes(field.fieldName) &&
            field.isVisible &&
            !field.isLookup
        )
      ) {
        return false;
      }
    }

    return true;
  }

  /**
   * Heuristics to determine which fields to autofill when a section contains
   * multiple fields of the same type.
   */
  getAutofillFields() {
    return this.fieldDetails.filter(fieldDetail =>
      this.shouldAutofillField(fieldDetail)
    );
  }

  /*
   * For telemetry
   */
  onDetected() {
    if (!this.isValidSection()) {
      return;
    }

    lazy.AutofillTelemetry.recordFormInteractionEvent(
      "detected",
      this.flowId,
      this.fieldDetails
    );
  }

  onPopupOpened(elementId) {
    const fieldDetail = this.getFieldDetailByElementId(elementId);
    lazy.AutofillTelemetry.recordFormInteractionEvent(
      "popup_shown",
      this.flowId,
      [fieldDetail]
    );
  }

  onFilled(filledResult) {
    lazy.AutofillTelemetry.recordFormInteractionEvent(
      "filled",
      this.flowId,
      this.fieldDetails,
      filledResult
    );
  }

  onFilledOnFieldsUpdate(filledResult) {
    lazy.AutofillTelemetry.recordFormInteractionEvent(
      "filled_on_fields_update",
      this.flowId,
      this.fieldDetails,
      filledResult
    );
  }

  onFilledModified(elementId) {
    const fieldDetail = this.getFieldDetailByElementId(elementId);
    lazy.AutofillTelemetry.recordFormInteractionEvent(
      "filled_modified",
      this.flowId,
      [fieldDetail]
    );
  }

  onSubmitted(formFilledData) {
    this.submitted = true;

    lazy.AutofillTelemetry.recordFormInteractionEvent(
      "submitted",
      this.flowId,
      this.fieldDetails,
      formFilledData
    );
  }

  onCleared(elementId) {
    const fieldDetail = this.getFieldDetailByElementId(elementId);
    lazy.AutofillTelemetry.recordFormInteractionEvent("cleared", this.flowId, [
      fieldDetail,
    ]);
  }

  /**
   * Utility functions
   */
  getFieldDetailByElementId(elementId) {
    return this.fieldDetails.find(detail => detail.elementId == elementId);
  }

  /**
   * Groups an array of field details by their browsing context IDs.
   *
   * @param {Array} fieldDetails
   *        Array of fieldDetails object
   *
   * @returns {object}
   *        An object keyed by BrowsingContext Id, value is an array that
   *        contains all fieldDetails with the same BrowsingContext id.
   */
  static groupFieldDetailsByBrowsingContext(fieldDetails) {
    const detailsByBC = {};
    for (const fieldDetail of fieldDetails) {
      const bcid = fieldDetail.browsingContextId;
      if (detailsByBC[bcid]) {
        detailsByBC[bcid].push(fieldDetail);
      } else {
        detailsByBC[bcid] = [fieldDetail];
      }
    }
    return detailsByBC;
  }
}

export class FormAutofillAddressSection extends FormAutofillSection {
  get type() {
    return lazy.AutofillDataTypes.ADDRESS;
  }

  isValidSection() {
    return lazy.FormAutofillUtils.isValidSection(this.fieldDetails);
  }

  isRecordCreatable(record) {
    const country = lazy.FormAutofillUtils.identifyCountryCode(
      record.country || record["country-name"]
    );
    if (
      country &&
      !lazy.FormAutofill.isAutofillTypeAvailableInCountry(this.type, country)
    ) {
      // We don't want to save data in the wrong fields due to not having proper
      // heuristic regexes in countries we don't yet support.
      this.log.warn(
        "isRecordCreatable: Country not supported:",
        record.country
      );
      return false;
    }

    // Multiple name or tel fields are treat as 1 field while countng whether
    // the number of fields exceed the valid address secton threshold
    const categories = Object.entries(record)
      .filter(e => !!e[1])
      .map(e => lazy.AutofillDataTypes.fieldToSubCategory[e[0]]);

    return (
      categories.reduce(
        (acc, category) =>
          ["name", "tel"].includes(category) && acc.includes(category)
            ? acc
            : [...acc, category],
        []
      ).length >= lazy.FormAutofillUtils.AUTOFILL_FIELDS_THRESHOLD
    );
  }
}

export class FormAutofillCreditCardSection extends FormAutofillSection {
  get type() {
    return lazy.AutofillDataTypes.CREDIT_CARD;
  }

  /**
   * Determine whether a set of cc fields identified by our heuristics form a
   * valid credit card section.
   * There are 3 different cases when a field is considered a credit card field
   * 1. Identified by autocomplete attribute. ex <input autocomplete="cc-number">
   * 2. Identified by fathom.
   * 3. Identified by regex-based heurstic. There is no confidence value in thise case.
   *
   * A form is considered a valid credit card form when one of the following condition
   * is met:
   * A. One of the cc field is identified by autocomplete (case 1).
   * B. One of the cc field is identified by fathom (case 2 or 3), and there is also
   *    another cc field found by any of our heuristic (case 2, 3).
   *
   * @returns {boolean} True for a valid section, otherwise false
   */
  isValidSection() {
    let ccNumberDetail = null;
    let ccNameDetail = null;
    let ccExpiryDetail = null;

    for (let detail of this.fieldDetails) {
      switch (detail.fieldName) {
        case "cc-number":
          ccNumberDetail = detail;
          break;
        case "cc-name":
        case "cc-given-name":
        case "cc-additional-name":
        case "cc-family-name":
          ccNameDetail = detail;
          break;
        case "cc-exp":
        case "cc-exp-month":
        case "cc-exp-year":
          ccExpiryDetail = detail;
          break;
      }
    }

    // Condition A. Always trust autocomplete attribute. A section is considered a valid
    // cc section as long as a field has autocomplete=cc-number, cc-name or cc-exp*
    if (
      ccNumberDetail?.reason == "autocomplete" ||
      ccNameDetail?.reason == "autocomplete" ||
      ccExpiryDetail?.reason == "autocomplete"
    ) {
      return true;
    }

    // Condition B. One of the field is identified by fathom, if this section also
    // contains another cc field found by our heuristic (Case 2, 3, or 4), we consider
    // this section a valid credit card seciton
    if (ccNumberDetail?.reason == "fathom") {
      if (ccNameDetail || ccExpiryDetail) {
        return true;
      }
    } else if (ccNameDetail?.reason == "fathom") {
      if (ccNumberDetail || ccExpiryDetail) {
        return true;
      }
    }

    return false;
  }

  isRecordCreatable(record) {
    return (
      record["cc-number"] &&
      lazy.FormAutofillUtils.isCCNumber(record["cc-number"])
    );
  }

  /**
   * Customize for previewing profile
   *
   * @param {object} profile
   *        A profile for pre-processing before previewing values.
   * @returns {boolean} Whether the profile should be filled.
   * @override
   */
  preparePreviewProfile(profile) {
    if (!profile) {
      return true;
    }

    // Always show the decrypted credit card number when Master Password is
    // disabled.
    if (profile["cc-number-decrypted"]) {
      profile["cc-number"] = profile["cc-number-decrypted"];
    } else if (/^\d+$/.test(profile["cc-number"])) {
      // If cc-number is an unmasked digit-only number, prepend mask characters
      // so that fields with a maxlength of four correctly show only the last 4
      // digits in the preview.
      profile["cc-number"] = "****" + profile["cc-number"];
    }

    // Never reveal the security code in the preview. The real value is still
    // used when filling.
    if (profile["cc-csc"]) {
      profile["cc-csc"] = "*".repeat(profile["cc-csc"].toString().length);
    }

    return true;
  }

  /**
   * Customize for filling profile
   *
   * @param {object} profile
   *        A profile for pre-processing before filling values.
   * @returns {boolean} Whether the profile should be filled.
   * @override
   */
  async prepareFillingProfile(profile) {
    const promptMessage = lazy.FormAutofillUtils.reauthOSPromptMessage(
      "autofill-use-payment-method-os-prompt-macos",
      "autofill-use-payment-method-os-prompt-windows",
      "autofill-use-payment-method-os-prompt-other"
    );

    let decrypted;
    try {
      decrypted = await this.getDecryptedString(
        profile,
        "cc-number",
        promptMessage
      );
    } catch (ex) {
      Glean.formautofill.promptShownOsReauth.record({
        trigger: "autofill",
        result:
          ex.result == Cr.NS_ERROR_ABORT ? "fail_user_canceled" : "fail_error",
      });
      return false;
    }

    if (decrypted === null) {
      // The record holds no encrypted number, so the user was asked nothing
      // and there is nothing to put in its place.
      return true;
    }

    Glean.formautofill.promptShownOsReauth.record({
      trigger: "autofill",
      result: "success",
    });
    profile["cc-number"] = decrypted;
    return true;
  }

  async getDecryptedString(record, field, reauth) {
    if (!lazy.FormAutofillUtils.getOSAuthEnabled()) {
      this.log.debug("Reauth is disabled");
      reauth = false;
    }
    return lazy.formAutofillStorage.creditCards.decryptField(record, field, {
      reauth,
    });
  }
}

export class FormAutofillPassportSection extends FormAutofillSection {
  get type() {
    return lazy.AutofillDataTypes.PASSPORT;
  }

  isValidSection() {
    // The passport number is the required field of a passport section, so a
    // section is valid as long as it contains one.
    return this.fieldDetails.some(
      detail => detail.fieldName == "passport-number"
    );
  }

  isRecordCreatable(record) {
    return !!record["passport-number"];
  }
}
