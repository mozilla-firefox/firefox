/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implements a service used to access storage and communicate with content.
 *
 * A "fields" array is used to communicate with FormAutofillChild. Each item
 * represents a single input field in the content page as well as its
 * @autocomplete properties. The schema is as below. Please refer to
 * FormAutofillChild.js for more details.
 *
 * [
 *   {
 *     section,
 *     addressType,
 *     contactType,
 *     fieldName,
 *     value,
 *     index
 *   },
 *   {
 *     // ...
 *   }
 * ]
 */

// We expose a singleton from this module. Some tests may import the
// constructor via the system global.
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { FormAutofill } from "resource://autofill/FormAutofill.sys.mjs";
import { FormAutofillUtils } from "resource://gre/modules/shared/FormAutofillUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddressComponent: "resource://gre/modules/shared/AddressComponent.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  FormAutofillAddressSection:
    "resource://gre/modules/shared/FormAutofillSection.sys.mjs",
  FormAutofillCreditCardSection:
    "resource://gre/modules/shared/FormAutofillSection.sys.mjs",
  FormAutofillHeuristics:
    "resource://gre/modules/shared/FormAutofillHeuristics.sys.mjs",
  FormAutofillSection:
    "resource://gre/modules/shared/FormAutofillSection.sys.mjs",
  FormAutofillPreferences:
    "resource://autofill/FormAutofillPreferences.sys.mjs",
  FormAutofillPrompter: "resource://autofill/FormAutofillPrompter.sys.mjs",
  FirefoxRelay: "resource://gre/modules/FirefoxRelay.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  MLAutofill: "resource://autofill/MLAutofill.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  FormAutofill.defineLogGetter(lazy, "FormAutofillParent")
);

const { ENABLED_AUTOFILL_ADDRESSES_PREF, ENABLED_AUTOFILL_CREDITCARDS_PREF } =
  FormAutofill;

const { ADDRESSES_COLLECTION_NAME, CREDITCARDS_COLLECTION_NAME, FIELD_STATES } =
  FormAutofillUtils;

let gMessageObservers = new Set();

export let FormAutofillStatus = {
  _initialized: false,

  /**
   * Cache of the Form Autofill status (considering preferences and storage).
   */
  _active: null,

  /**
   * Initializes observers and registers the message handler.
   */
  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;

    Services.obs.addObserver(this, "privacy-pane-loaded");

    // Observing the pref and storage changes
    Services.prefs.addObserver(ENABLED_AUTOFILL_ADDRESSES_PREF, this);
    Services.obs.addObserver(this, "formautofill-storage-changed");

    // Only listen to credit card related preference if it is available
    if (FormAutofill.isAutofillCreditCardsAvailable) {
      Services.prefs.addObserver(ENABLED_AUTOFILL_CREDITCARDS_PREF, this);
    }
  },

  /**
   * Uninitializes FormAutofillStatus. This is for testing only.
   *
   * @private
   */
  uninit() {
    lazy.gFormAutofillStorage._saveImmediately();

    if (!this._initialized) {
      return;
    }
    this._initialized = false;

    this._active = null;

    Services.obs.removeObserver(this, "privacy-pane-loaded");
    Services.prefs.removeObserver(ENABLED_AUTOFILL_ADDRESSES_PREF, this);
    Services.wm.removeListener(this);

    if (FormAutofill.isAutofillCreditCardsAvailable) {
      Services.prefs.removeObserver(ENABLED_AUTOFILL_CREDITCARDS_PREF, this);
    }
  },

  get formAutofillStorage() {
    return lazy.gFormAutofillStorage;
  },

  /**
   * Broadcast the status to frames when the form autofill status changes.
   */
  onStatusChanged() {
    lazy.log.debug("onStatusChanged: Status changed to", this._active);
    Services.ppmm.sharedData.set("FormAutofill:enabled", this._active);
    // Sync autofill enabled to make sure the value is up-to-date
    // no matter when the new content process is initialized.
    Services.ppmm.sharedData.flush();
  },

  /**
   * Query preference and storage status to determine the overall status of the
   * form autofill feature.
   *
   * @returns {boolean} whether form autofill is active (enabled and has data)
   */
  computeStatus() {
    const savedFieldNames = Services.ppmm.sharedData.get(
      "FormAutofill:savedFieldNames"
    );

    return (
      (Services.prefs.getBoolPref(ENABLED_AUTOFILL_ADDRESSES_PREF) ||
        Services.prefs.getBoolPref(ENABLED_AUTOFILL_CREDITCARDS_PREF)) &&
      savedFieldNames &&
      savedFieldNames.size > 0
    );
  },

  /**
   * Update the status and trigger onStatusChanged, if necessary.
   */
  updateStatus() {
    lazy.log.debug("updateStatus");
    let wasActive = this._active;
    this._active = this.computeStatus();
    if (this._active !== wasActive) {
      this.onStatusChanged();
    }
  },

  async updateSavedFieldNames() {
    lazy.log.debug("updateSavedFieldNames");

    let savedFieldNames;
    const addressNames =
      await lazy.gFormAutofillStorage.addresses.getSavedFieldNames();

    // Don't access the credit cards store unless it is enabled.
    if (FormAutofill.isAutofillCreditCardsAvailable) {
      const creditCardNames =
        await lazy.gFormAutofillStorage.creditCards.getSavedFieldNames();
      savedFieldNames = new Set([...addressNames, ...creditCardNames]);
    } else {
      savedFieldNames = addressNames;
    }

    Services.ppmm.sharedData.set(
      "FormAutofill:savedFieldNames",
      savedFieldNames
    );
    Services.ppmm.sharedData.flush();

    this.updateStatus();
  },

  async observe(subject, topic, data) {
    lazy.log.debug("observe:", topic, "with data:", data);

    if (
      !FormAutofill.isAutofillCreditCardsAvailable &&
      !FormAutofill.isAutofillAddressesAvailable
    ) {
      return;
    }

    switch (topic) {
      case "privacy-pane-loaded": {
        let formAutofillPreferences = new lazy.FormAutofillPreferences();
        let document = subject.document;
        let prefFragment = formAutofillPreferences.init(document);
        let formAutofillGroupBox = document.getElementById(
          "formAutofillGroupBox"
        );
        formAutofillGroupBox.appendChild(prefFragment);
        break;
      }

      case "nsPref:changed": {
        // Observe pref changes and update _active cache if status is changed.
        this.updateStatus();
        break;
      }

      case "formautofill-storage-changed": {
        // Early exit if only metadata is changed
        if (data == "notifyUsed") {
          break;
        }

        await this.updateSavedFieldNames();
        break;
      }

      default: {
        throw new Error(
          `FormAutofillStatus: Unexpected topic observed: ${topic}`
        );
      }
    }
  },
};

// Lazily load the storage JSM to avoid disk I/O until absolutely needed.
// Once storage is loaded we need to update saved field names and inform content processes.
ChromeUtils.defineLazyGetter(lazy, "gFormAutofillStorage", () => {
  let { formAutofillStorage } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillStorage.sys.mjs"
  );
  lazy.log.debug("Loading formAutofillStorage");

  formAutofillStorage.initialize().then(() => {
    // Update the saved field names to compute the status and update child processes.
    FormAutofillStatus.updateSavedFieldNames();
  });

  return formAutofillStorage;
});

export class FormAutofillParent extends JSWindowActorParent {
  constructor() {
    super();
    FormAutofillStatus.init();

    // This object maintains data that should be shared among all
    // FormAutofillParent actors in the same DOM tree.
    this._topLevelCache = {
      sectionsByRootId: new Map(),
      filledResult: new Map(),
      submittedData: new Map(),
    };
  }

  get topLevelCache() {
    let actor;
    try {
      actor =
        this.browsingContext.top == this.browsingContext
          ? this
          : FormAutofillParent.getActor(this.browsingContext.top);
    } catch {}
    actor ||= this;
    return actor._topLevelCache;
  }

  get sectionsByRootId() {
    return this.topLevelCache.sectionsByRootId;
  }

  get filledResult() {
    return this.topLevelCache.filledResult;
  }

  get submittedData() {
    return this.topLevelCache.submittedData;
  }
  /**
   * Handles the message coming from FormAutofillChild.
   *
   * @param   {object} message
   * @param   {string} message.name The name of the message.
   * @param   {object} message.data The data of the message.
   */
  async receiveMessage({ name, data }) {
    if (
      !FormAutofill.isAutofillCreditCardsAvailable &&
      !FormAutofill.isAutofillAddressesAvailable
    ) {
      return undefined;
    }

    switch (name) {
      case "FormAutofill:InitStorage": {
        await lazy.gFormAutofillStorage.initialize();
        await FormAutofillStatus.updateSavedFieldNames();
        break;
      }
      case "FormAutofill:GetRecords": {
        const records = await this.getRecords(data);
        return { records };
      }
      case "FormAutofill:OnFormSubmit": {
        const { rootElementId, formFilledData } = data;
        this.notifyMessageObservers("onFormSubmitted", data);
        this.onFormSubmit(rootElementId, formFilledData);
        break;
      }

      case "FormAutofill:FieldsIdentified":
        this.notifyMessageObservers("fieldsIdentified", data);
        break;

      case "FormAutofill:OnFieldsDetected":
        await this.onFieldsDetected(
          data,
          "FormAutofill:onFieldsDetectedComplete"
        );
        break;
      case "FormAutofill:OnFieldsUpdated":
        await this.onFieldsDetected(
          data,
          "FormAutofill:onFieldsUpdatedComplete"
        );
        break;
      case "FormAutofill:FieldFilledModified": {
        this.onFieldFilledModified(data);
        break;
      }
      case "FormAutofill:FieldsUpdatedDuringAutofill": {
        // TODO bug 1953231: The parent should introduce profile ids, so that
        // the child can simply send a profile id instead of the whole profile data
        const { elementId, profile } = data;
        this.onFieldsUpdatedDuringAutofill(elementId, profile);
        break;
      }

      // The remaining Save and Remove messages are invoked only by tests.
      case "FormAutofill:SaveAddress": {
        if (data.guid) {
          await lazy.gFormAutofillStorage.addresses.update(
            data.guid,
            data.address
          );
        } else {
          await lazy.gFormAutofillStorage.addresses.add(data.address);
        }
        break;
      }
      case "FormAutofill:SaveCreditCard": {
        // Setting the first parameter of OSKeyStore.ensurLoggedIn as false
        // since this case only called in tests. Also the reason why we're not calling FormAutofill.verifyUserOSAuth.
        if (!(await lazy.OSKeyStore.ensureLoggedIn(false)).authenticated) {
          lazy.log.warn("User canceled encryption login");
          return undefined;
        }
        await lazy.gFormAutofillStorage.creditCards.add(data.creditcard);
        break;
      }
      case "FormAutofill:RemoveAddresses": {
        data.guids.forEach(guid =>
          lazy.gFormAutofillStorage.addresses.remove(guid)
        );
        break;
      }
      case "FormAutofill:RemoveCreditCards": {
        data.guids.forEach(guid =>
          lazy.gFormAutofillStorage.creditCards.remove(guid)
        );
        break;
      }
    }

    return undefined;
  }

  // For a third-party frame, we only autofill when the frame is same origin
  // with the frame that triggers autofill.
  isBCSameOrigin(browsingContext) {
    return this.manager.documentPrincipal.equals(
      browsingContext.currentWindowGlobal.documentPrincipal
    );
  }

  static getActor(browsingContext) {
    return browsingContext?.currentWindowGlobal?.getActor("FormAutofill");
  }

  get formOrigin() {
    return lazy.LoginHelper.getLoginOrigin(
      this.manager.documentPrincipal?.originNoSuffix
    );
  }

  /**
   * Recursively identifies autofillable fields within each sub-frame of the
   * given browsing context.
   *
   * This function iterates through all sub-frames and uses the provided
   * browsing context to locate and identify fields that are eligible for
   * autofill. It handles both the top-level context and any nested
   * iframes, aggregating all identified fields into a single array.
   *
   * @param {BrowsingContext} browsingContext
   *        The browsing context where autofill fields are to be identified.
   * @param {string} focusedBCId
   *        The browsing context ID of the <iframe> within the top-level context
   *        that contains the currently focused field. Null if this call is
   *        triggered from the top-level.
   * @param {Array} alreadyIdentifiedFields
   *        An array of previously identified fields for the current actor.
   *        This serves as a cache to avoid redundant field identification.
   *
   * @returns {Promise<Array>}
   *        A promise that resolves to an array containing two elements:
   *        1. An array of FieldDetail objects representing detected fields.
   *        2. The root element ID.
   */
  async identifyAllSubTreeFields(
    browsingContext,
    focusedBCId,
    alreadyIdentifiedFields,
    msg
  ) {
    let identifiedFieldsIncludeIframe = [];
    try {
      const actor = FormAutofillParent.getActor(browsingContext);
      if (actor == this) {
        identifiedFieldsIncludeIframe = alreadyIdentifiedFields;
      } else {
        msg ||= "FormAutofill:IdentifyFields";
        identifiedFieldsIncludeIframe = await actor.sendQuery(msg, {
          focusedBCId,
        });
      }
    } catch (e) {
      console.error("There was an error identifying fields: ", e.message);
    }

    if (!identifiedFieldsIncludeIframe.length) {
      return [[], null];
    }

    const rootElementId = identifiedFieldsIncludeIframe[0].rootElementId;

    const subTreeDetails = [];
    for (const field of identifiedFieldsIncludeIframe) {
      if (field.localName != "iframe") {
        subTreeDetails.push(field);
        continue;
      }

      const iframeBC = BrowsingContext.get(field.browsingContextId);
      const [fields] = await this.identifyAllSubTreeFields(
        iframeBC,
        focusedBCId,
        alreadyIdentifiedFields,
        msg
      );
      subTreeDetails.push(...fields);
    }
    return [subTreeDetails, rootElementId];
  }

  /**
   * After collecting all the fields, we apply heuristics to:
   * 1. Update field names based on the context of surrounding fields.
   *    For instance, a field named 'name' might be renamed to 'cc-name' if
   *    it follows a field named 'cc-number'.
   * 2. Identify and classify address and credit card sections. Sections
   *    are used to group fields that should be autofilled together.
   *
   * @param {Array<FieldDetail>} fieldDetails
   *        An array of the identified fields.
   * @param {object} options
   *        options to parse to 'classifySections'
   */
  static parseAndClassifyFields(fieldDetails, options = {}) {
    lazy.FormAutofillHeuristics.parseAndUpdateFieldNamesParent(fieldDetails);

    // At this point we have identified all the fields that are under the same
    // root element. We can run section classification heuristic now.
    return lazy.FormAutofillSection.classifySections(fieldDetails, options);
  }

  /**
   * When a field is detected, identify fields in other frames, if they exist.
   * To ensure that the identified fields across frames still follow the document
   * order, we traverse from the top-level window and recursively identify fields
   * in subframes.
   *
   * @param {Array} fieldsIncludeIframe
   *        Array of FieldDetail objects of detected fields (include iframes).
   * @param {string} message that is sent to the children
   *                          - On initial fields detection: "FormAutofill:onFieldsDetectedComplete"
   *                          - On dynamic form change detection:  "FormAutofill:onFieldsUpdatedComplete"
   */
  async onFieldsDetected(fieldsIncludeIframe, message) {
    // If the detected fields are not in the top-level, identify the <iframe> in
    // the top-level that contains the detected fields. This is necessary to determine
    // the root element of this form. For non-top-level frames, the focused <iframe>
    // is not needed because, in the case of iframes, the root element is always
    // the frame itself (we disregard <form> elements within <iframes>).
    let focusedBCId;
    const topBC = this.browsingContext.top;
    if (this.browsingContext != topBC) {
      let bc = this.browsingContext;
      while (bc.parent != topBC) {
        bc = bc.parent;
      }
      focusedBCId = bc.id;
    }

    const [fieldDetails, rootElementId] = await this.identifyAllSubTreeFields(
      topBC,
      focusedBCId,
      fieldsIncludeIframe
    );

    // Now we have collected all the fields for the form, run parsing heuristics
    // to update the field name based on surrounding fields.
    const sections = FormAutofillParent.parseAndClassifyFields(fieldDetails);

    this.sectionsByRootId.set(rootElementId, sections);

    // Note that 'onFieldsDetected' is not only called when a form is detected,
    // but also called when the elements in a form are changed. When the elements
    // in a form are changed, we treat the "updated" section as a new detected section.
    sections.forEach(section => section.onDetected());

    if (FormAutofill.isMLExperimentEnabled) {
      sections.forEach(section => lazy.MLAutofill.runInference(section));
    }

    // Inform all the child actors of the updated 'fieldDetails'
    const detailsByBC =
      lazy.FormAutofillSection.groupFieldDetailsByBrowsingContext(fieldDetails);
    for (const [bcId, fds] of Object.entries(detailsByBC)) {
      try {
        const actor = FormAutofillParent.getActor(BrowsingContext.get(bcId));
        await actor.sendQuery(message, {
          fds,
        });
      } catch (e) {
        console.error(
          `There was an error sending ${message} message`,
          e.message
        );
      }
    }

    // This is for testing purpose only which sends a notification to indicate that the
    // form has been identified, and ready to open popup.
    this.notifyMessageObservers("fieldsIdentified");

    // This is for testing purposes, to know that the fields were
    // identified after a form change
    this.notifyMessageObservers("fieldDetectionCompleted");
  }

  /**
   * Re-filling fields after a form change occured
   * immediately after the last filling process
   *
   * @param {string} elementId element id of focused element that triggered
   *                           the initial autocompletion process
   * @param {object} profile that was used for the previous autofill action
   *                         causing the form change
   */
  async onFieldsUpdatedDuringAutofill(elementId, profile) {
    const section = this.getSectionByElementId(elementId);
    const msg = "FormAutofill:FillFieldsOnFormChange";
    const fields = section.getAutofillFields();
    const result = await this.#triggerAutofillActionInChildren(
      msg,
      elementId,
      fields,
      profile
    );
    result.forEach((value, key) => {
      const filledField = this.filledResult.get(key);
      const isFilledOnFieldsUpdate =
        !filledField || filledField.filledState != FIELD_STATES.AUTO_FILLED;
      this.filledResult.set(key, value);
      value.isFilledOnFieldsUpdate = isFilledOnFieldsUpdate;
    });
    section.onFilledOnFieldsUpdate(result);

    // For testing only
    Services.obs.notifyObservers(
      null,
      "formautofill-fill-after-form-change-complete"
    );
  }

  /**
   * Called when a form is submitted
   *
   * @param {string} rootElementId
   *        The id of the root element. If the form
   * @param {object} formFilledData
   *        An object keyed by element id, and the value is an object that
   *        includes the following properties:
   *          - filledState: The autofill state of the element.
   *          - filledValue: The value of the element.
   *        See `collectFormFilledData` in FormAutofillHandler.
   */
  async onFormSubmit(rootElementId, formFilledData) {
    const submittedSections = this.sectionsByRootId.values().find(sections => {
      const details = sections.flatMap(s => s.fieldDetails).flat();
      return details.some(detail => detail.rootElementId == rootElementId);
    });

    if (!submittedSections) {
      return;
    }

    const address = [];
    const creditCard = [];

    // Caching the submitted data as actors may be destroyed immediately after
    // submission.
    this.submittedData.set(rootElementId, formFilledData);

    for (const section of submittedSections) {
      const submittedResult = new Map();
      const autofillFields = section.getAutofillFields();
      const detailsByBC =
        lazy.FormAutofillSection.groupFieldDetailsByBrowsingContext(
          autofillFields
        );
      for (const [bcId, fieldDetails] of Object.entries(detailsByBC)) {
        try {
          // Fields within the same section that share the same browsingContextId
          // should also share the same rootElementId.
          const rootEId = fieldDetails[0].rootElementId;

          let result = this.submittedData.get(rootEId);
          if (!result) {
            const actor = FormAutofillParent.getActor(
              BrowsingContext.get(bcId)
            );
            result = await actor.sendQuery("FormAutofill:GetFilledInfo", {
              rootElementId: rootEId,
            });
          }
          result.forEach((value, key) => submittedResult.set(key, value));
        } catch (e) {
          console.error("There was an error submitting: ", e.message);
          return;
        }
      }

      // At this point, it's possible to discover that this section has already
      // been submitted since submission events may be triggered concurrently by
      // multiple actors.
      if (section.submitted) {
        continue;
      }
      section.onSubmitted(submittedResult);

      const secRecord = section.createRecord(submittedResult);
      if (!secRecord) {
        continue;
      }

      if (section instanceof lazy.FormAutofillAddressSection) {
        address.push(secRecord);
      } else if (section instanceof lazy.FormAutofillCreditCardSection) {
        creditCard.push(secRecord);
      } else {
        throw new Error("Unknown section type");
      }
    }

    try {
      // The child is ignoring any detected field updates during a form submission.
      // So we're notifying the child that the form submission is completed. Additionally the child
      // disconnects any form change observers from the submitted form/fields.
      this.sendAsyncMessage("FormAutofill:onFormSubmissionComplete", {
        rootElementId,
      });
    } catch (e) {
      // The child might be destroyed immediately after submission
    }

    const browser = this.manager?.browsingContext.top.embedderElement;
    if (!browser) {
      return;
    }

    // Transmit the telemetry immediately in the meantime form submitted, and handle
    // these pending doorhangers later.
    await Promise.all(
      [
        await Promise.all(
          address.map(addrRecord => this._onAddressSubmit(addrRecord, browser))
        ),
        await Promise.all(
          creditCard.map(ccRecord =>
            this._onCreditCardSubmit(ccRecord, browser)
          )
        ),
      ]
        .map(pendingDoorhangers => {
          return pendingDoorhangers.filter(
            pendingDoorhanger =>
              !!pendingDoorhanger && typeof pendingDoorhanger == "function"
          );
        })
        .map(pendingDoorhangers =>
          (async () => {
            for (const showDoorhanger of pendingDoorhangers) {
              await showDoorhanger();
            }
          })()
        )
    );
  }

  /**
   * Get the records from profile store and return results back to content
   * process. It will decrypt the credit card number and append
   * "cc-number-decrypted" to each record if OSKeyStore isn't set.
   *
   * This is static as a unit test calls this.
   *
   * @param  {object} data
   * @param  {string} data.searchString
   *         The typed string for filtering out the matched records.
   * @param  {string} data.collectionName
   *         The name used to specify which collection to retrieve records.
   * @param  {string} data.fieldName
   *         The field name to search.
   */
  async getRecords({ searchString, collectionName, fieldName }) {
    // Derive the collection name from field name if it doesn't exist
    collectionName ||=
      FormAutofillUtils.getCollectionNameFromFieldName(fieldName);

    const collection = lazy.gFormAutofillStorage[collectionName];
    if (!collection) {
      return [];
    }

    const records = await collection.getAll();

    // Add testing records if exists
    records.push(...this.#getTemporaryRecordForTab(collectionName));

    if (!fieldName || !records.length) {
      return records;
    }

    // We don't filter "cc-number"
    if (collectionName == CREDITCARDS_COLLECTION_NAME) {
      if (fieldName == "cc-number") {
        return records.filter(record => !!record["cc-number"]);
      }
    }

    const lcSearchString = searchString.toLowerCase();
    return records.filter(record => {
      const fieldValue = record[fieldName];
      if (!fieldValue) {
        return false;
      }

      if (
        collectionName == ADDRESSES_COLLECTION_NAME &&
        !FormAutofill.isAutofillAddressesAvailableInCountry(record.country)
      ) {
        // Address autofill isn't supported for the record's country so we don't
        // want to attempt to potentially incorrectly fill the address fields.
        return false;
      }

      return (
        !lcSearchString ||
        String(fieldValue).toLowerCase().startsWith(lcSearchString)
      );
    });
  }

  /*
   * Capture-related functions
   */

  async _onAddressSubmit(address, browser) {
    if (!FormAutofill.isAutofillAddressesEnabled) {
      return false;
    }

    const storage = lazy.gFormAutofillStorage.addresses;

    // Make sure record is normalized before comparing with records in the storage
    try {
      storage._normalizeRecord(address.record);
    } catch (_e) {
      return false;
    }

    const newAddress = new lazy.AddressComponent(
      address.record,
      // Invalid address fields in the address form will not be captured.
      { ignoreInvalid: true }
    );

    // Exams all stored record to determine whether to show the prompt or not.
    let mergeableFields = [];
    let preserveFields = [];
    let oldRecord = {};

    for (const record of await storage.getAll()) {
      const savedAddress = new lazy.AddressComponent(record);
      // filter invalid field
      const result = newAddress.compare(savedAddress);

      // If any of the fields in the new address are different from the corresponding fields
      // in the saved address, the two addresses are considered different. For example, if
      // the name, email, country are the same but the street address is different, the two
      // addresses are not considered the same.
      if (Object.values(result).includes("different")) {
        continue;
      }

      // If none of the fields in the new address are mergeable, the new address is considered
      // a duplicate of a local address. Therefore, we don't need to capture this address.
      const fields = Object.entries(result)
        .filter(v => ["superset", "similar"].includes(v[1]))
        .map(v => v[0]);
      if (!fields.length) {
        lazy.log.debug(
          "A duplicated address record is found, do not show the prompt"
        );
        storage.notifyUsed(record.guid);
        return false;
      }

      // If the new address is neither a duplicate of the saved address nor a different address.
      // There must be at least one field we can merge, show the update doorhanger
      lazy.log.debug(
        "A mergeable address record is found, show the update prompt"
      );

      // If one record has fewer mergeable fields compared to another, it suggests greater similarity
      // to the merged record. In such cases, we opt for the record with the fewest mergeable fields.
      // TODO: Bug 1830841. Add a testcase
      if (!mergeableFields.length || mergeableFields > fields.length) {
        mergeableFields = fields;
        preserveFields = Object.entries(result)
          .filter(v => ["same", "subset"].includes(v[1]))
          .map(v => v[0]);
        oldRecord = record;
      }
    }

    // Find a mergeable old record, construct the new record by only copying mergeable fields
    // from the new address.
    let newRecord = {};
    if (mergeableFields.length) {
      // TODO: This is only temporarily, should be removed after Bug 1836438 is fixed
      if (mergeableFields.includes("name")) {
        mergeableFields.push("given-name", "additional-name", "family-name");
      }
      mergeableFields.forEach(f => {
        if (f in newAddress.record) {
          newRecord[f] = newAddress.record[f];
        }
      });

      if (preserveFields.includes("name")) {
        preserveFields.push("given-name", "additional-name", "family-name");
      }
      preserveFields.forEach(f => {
        if (f in oldRecord) {
          newRecord[f] = oldRecord[f];
        }
      });
    } else {
      newRecord = newAddress.record;
    }

    if (!this._shouldShowSaveAddressPrompt(newAddress.record)) {
      return false;
    }

    return async () => {
      await lazy.FormAutofillPrompter.promptToSaveAddress(
        browser,
        storage,
        address.flowId,
        { oldRecord, newRecord }
      );
    };
  }

  async _onCreditCardSubmit(creditCard, browser) {
    const storage = lazy.gFormAutofillStorage.creditCards;

    // Make sure record is normalized before comparing with records in the storage
    try {
      storage._normalizeRecord(creditCard.record);
    } catch (_e) {
      return false;
    }

    // If the record alreay exists in the storage, don't bother showing the prompt
    const matchRecord = (
      await storage.getMatchRecords(creditCard.record).next()
    ).value;
    if (matchRecord) {
      storage.notifyUsed(matchRecord.guid);
      return false;
    }

    // Suppress the pending doorhanger from showing up if user disabled credit card in previous doorhanger.
    if (!FormAutofill.isAutofillCreditCardsEnabled) {
      return false;
    }

    // Overwrite the guid if there is a duplicate
    const duplicateRecord =
      (await storage.getDuplicateRecords(creditCard.record).next()).value ?? {};

    return async () => {
      await lazy.FormAutofillPrompter.promptToSaveCreditCard(
        browser,
        storage,
        creditCard.flowId,
        { oldRecord: duplicateRecord, newRecord: creditCard.record }
      );
    };
  }

  _shouldShowSaveAddressPrompt(record) {
    if (!FormAutofill.isAutofillAddressesCaptureEnabled) {
      return false;
    }

    // Do not save address for regions that we don't support
    if (!FormAutofill.isAutofillAddressesAvailableInCountry(record.country)) {
      lazy.log.debug(
        `Do not show the address capture prompt for unsupported regions - ${record.country}`
      );
      return false;
    }

    // Display the address capture doorhanger only when the submitted form contains all
    // the required fields. This approach is implemented to prevent excessive prompting.
    let requiredFields = FormAutofill.addressCaptureRequiredFields;
    requiredFields ??=
      FormAutofillUtils.getFormFormat(record.country).countryRequiredFields ??
      [];

    if (!requiredFields.every(field => field in record)) {
      lazy.log.debug(
        "Do not show the address capture prompt when the submitted form doesn't contain all the required fields"
      );
      return false;
    }

    return true;
  }

  /*
   * AutoComplete-related functions
   */

  /**
   * Retrieves autocomplete entries for a given search string and data context.
   *
   * @param {string} searchString
   *                 The search string used to filter autocomplete entries.
   * @param {object} options
   * @param {string} options.fieldName
   *                 The name of the field for which autocomplete entries are being fetched.
   * @param {string} options.elementId
   *                 The id of the element for which we are searching for an autocomplete entry.
   * @param {string} options.scenarioName
   *                 The scenario name used in the autocomplete operation to fetch external entries.
   * @returns {Promise<object>} A promise that resolves to an object containing two properties: `records` and `externalEntries`.
   *         `records` is an array of autofill records from the form's internal data, sorted by `timeLastUsed`.
   *         `externalEntries` is an array of external autocomplete items fetched based on the scenario.
   *         `allFieldNames` is an array containing all the matched field name found in this section.
   */
  async searchAutoCompleteEntries(searchString, options) {
    const { fieldName, elementId, scenarioName } = options;

    const section = this.getSectionByElementId(elementId);
    if (!section.isValidSection() || !section.isEnabled()) {
      return null;
    }

    const fieldDetail = section.getFieldDetailByElementId(elementId);
    if (!section.shouldAutofillField(fieldDetail)) {
      return null;
    }

    const relayPromise = lazy.FirefoxRelay.autocompleteItemsAsync({
      origin: this.formOrigin,
      scenarioName,
      hasInput: !!searchString?.length,
    });

    // Retrieve information for the autocomplete entry
    const recordsPromise = this.getRecords({
      searchString,
      fieldName,
    });

    const [records, externalEntries] = await Promise.all([
      recordsPromise,
      relayPromise,
    ]);

    // Sort addresses by timeLastUsed for showing the lastest used address at top.
    records.sort((a, b) => b.timeLastUsed - a.timeLastUsed);
    return { records, externalEntries, allFieldNames: section.allFieldNames };
  }

  /**
   * This function is called when an autocomplete entry that is provided by
   * formautofill is selected by the user.
   */
  async onAutoCompleteEntrySelected(message, data) {
    switch (message) {
      case "FormAutofill:OpenPreferences": {
        const win = lazy.BrowserWindowTracker.getTopWindow();
        win.openPreferences("privacy-form-autofill");
        break;
      }

      case "FormAutofill:ClearForm": {
        this.clearForm(data.focusElementId);
        break;
      }

      case "FormAutofill:FillForm": {
        this.autofillFields(data.focusElementId, data.profile);
        break;
      }

      default: {
        lazy.log.debug("Unsupported autocomplete message:", message);
        break;
      }
    }
  }

  onAutoCompletePopupOpened(elementId) {
    const section = this.getSectionByElementId(elementId);
    section?.onPopupOpened(elementId);
  }

  onAutoCompleteEntryClearPreview(message, data) {
    this.previewFields(data.focusElementId, null);
  }

  onAutoCompleteEntryHovered(message, data) {
    if (message == "FormAutofill:FillForm") {
      this.previewFields(data.focusElementId, data.profile);
    } else {
      // Make sure the preview is cleared when users select an entry
      // that doesn't support preview.
      this.previewFields(data.focusElementId, null);
    }
  }

  // Credit card number will only be filled when it is same-origin with the frame that
  // triggers the autofilling.
  #FIELDS_FILLED_WHEN_SAME_ORIGIN = ["cc-number"];

  /**
   * Determines if the field should be autofilled based on its origin.
   *
   * @param {BorwsingContext} bc
   *        The browsing context the field is in.
   * @param {object} fieldDetail
   *        The Field detail of the field to be autofilled.
   *
   * @returns {boolean}
   *        Returns true if the field should be autofilled, false otherwise.
   */
  shouldAutofill(bc, fieldDetail) {
    const isSameOrigin = this.isBCSameOrigin(bc);

    // Autofill always applies to frames that are the same origin as the triggered frame.
    if (isSameOrigin) {
      return true;
    }

    // Relaxed autofill rule is controlled by a preference.
    if (!FormAutofill.autofillSameOriginWithTop) {
      return false;
    }

    // Relaxed autofill restrictions: for fields other than the credit card number,
    // if the field is in a top-level frame or in a first-party origin iframe,
    // autofill is allowed.
    if (this.#FIELDS_FILLED_WHEN_SAME_ORIGIN.includes(fieldDetail.fieldName)) {
      return false;
    }

    return FormAutofillUtils.isBCSameOriginWithTop(bc);
  }

  /**
   * Trigger the autofill-related action in child processes that are within
   * this section.
   *
   * @param {string} message
   *        The message to be sent to the child processes to trigger the corresponding
   *        action.
   * @param {string} focusedId
   *        The ID of the element that initially triggers the autofill action.
   * @param {Array<FieldDetails>} fieldDetails
   *        An array of fieldDetails to be autocompleted/previewed/cleared.
   * @param {object} profile
   *        The profile data used for autofilling the fields.
   */
  async #triggerAutofillActionInChildren(
    message,
    focusedId,
    fieldDetails,
    profile
  ) {
    const detailsByBC =
      lazy.FormAutofillSection.groupFieldDetailsByBrowsingContext(fieldDetails);

    const result = new Map();
    const entries = Object.entries(detailsByBC);

    // Since we focus on the element when setting its autofill value, we need to ensure
    // the frame that contains the focused input is the last one that runs autofill. Doing
    // this guarantees the focused element remains the focused one after autofilling.
    const index = entries.findIndex(e =>
      e[1].some(f => f.elementId == focusedId)
    );
    if (index != -1) {
      const entry = entries.splice(index, 1)[0];
      entries.push(entry);
    }

    for (const [bcId, bcFieldDetails] of entries) {
      const bc = BrowsingContext.get(bcId);

      // For sensitive fields, we ONLY fill them when they are same-origin with
      // the triggered frame.
      const ids = bcFieldDetails
        .filter(detail => this.shouldAutofill(bc, detail))
        .map(detail => detail.elementId);

      if (!ids.length) {
        // No elements in this browsing context to trigger the action on
        continue;
      }

      try {
        const actor = FormAutofillParent.getActor(bc);
        const ret = await actor.sendQuery(message, {
          focusedId: bc == this.manager.browsingContext ? focusedId : null,
          ids,
          profile,
        });
        if (ret instanceof Map) {
          ret.forEach((value, key) => result.set(key, value));
        }
      } catch (e) {
        console.error("There was an error autofilling: ", e.message);
      }
    }

    return result;
  }

  /**
   * Previews autofill results for the section containing the triggered element
   * using the selected user profile.
   *
   * @param {string} elementId
   *        The id of the element that triggers the autofill preview
   * @param {object} profile
   *        The user-selected profile data to be used for the autofill preview
   */
  async previewFields(elementId, profile) {
    const section = this.getSectionByElementId(elementId);

    if (!(await section.preparePreviewProfile(profile))) {
      lazy.log.debug("profile cannot be previewed");
      return;
    }

    const msg = "FormAutofill:PreviewFields";
    const fields = section.getAutofillFields();
    await this.#triggerAutofillActionInChildren(
      msg,
      elementId,
      fields,
      profile
    );

    // For testing only
    Services.obs.notifyObservers(null, "formautofill-preview-complete");
  }

  /**
   * Autofill results for the section containing the triggered element.
   * using the selected user profile.
   *
   * @param {string} elementId
   *        The id of the element that triggers the autofill.
   * @param {object} profile
   *        The user-selected profile data to be used for the autofill
   */
  async autofillFields(elementId, profile) {
    const section = this.getSectionByElementId(elementId);
    if (!(await section.prepareFillingProfile(profile))) {
      lazy.log.debug("profile cannot be filled");
      // For testing only
      Services.obs.notifyObservers(null, "formautofill-autofill-complete");
      return;
    }

    if (AppConstants.platform !== "android") {
      lazy.NimbusFeatures["address-autofill-feature"].recordExposureEvent({
        once: true,
      });
    }

    const msg = "FormAutofill:FillFields";
    const fields = section.getAutofillFields();
    const result = await this.#triggerAutofillActionInChildren(
      msg,
      elementId,
      fields,
      profile
    );

    result.forEach((value, key) => {
      this.filledResult.set(key, value);
      value.isFilledOnFieldsUpdate = false;
    });
    section.onFilled(result);

    // For testing only
    Services.obs.notifyObservers(null, "formautofill-autofill-complete");
  }

  /**
   * Clears autofill results for the section containing the triggered element.
   *
   * @param {string} elementId
   *        The id of the element that triggers the clear action.
   */
  async clearForm(elementId) {
    const section = this.getSectionByElementId(elementId);

    section.onCleared(elementId);

    section.fieldDetails.forEach(field => {
      if (!this.filledResult.has(field.elementId)) {
        return;
      }
      this.filledResult.get(field.elementId).filledState = FIELD_STATES.NORMAL;
    });

    const msg = "FormAutofill:ClearFilledFields";
    // On a form clearing action, we don't use section.getAutofillFields to retrieve
    // the elements that are about to be cleared, but we consider all fieldDetails regardless
    // of their visiblity state in order to also clear invisible but previously autocompleted fields
    const fields = section.fieldDetails;
    await this.#triggerAutofillActionInChildren(msg, elementId, fields);

    // For testing only
    Services.obs.notifyObservers(null, "formautofill-clear-form-complete");
  }

  /**
   * Called when a autofilled fields is modified by the user.
   *
   * @param {string} elementId
   *        The id of the element that users modify its value after autofilling.
   */
  onFieldFilledModified(elementId) {
    if (!this.filledResult?.get(elementId)) {
      return;
    }

    this.filledResult.get(elementId).filledState = FIELD_STATES.NORMAL;

    const section = this.getSectionByElementId(elementId);

    // For telemetry
    section?.onFilledModified(elementId);

    // Restore <select> fields to their initial state once we know
    // that the user intends to manually clear the filled form.
    const fieldDetails = section.fieldDetails;
    const selects = fieldDetails.filter(field => field.localName == "select");
    if (selects.length) {
      const inputs = fieldDetails.filter(
        field =>
          this.filledResult.has(field.elementId) && field.localName == "input"
      );
      if (
        inputs.every(
          field =>
            this.filledResult.get(field.elementId).filledState ==
            FIELD_STATES.NORMAL
        )
      ) {
        const ids = selects.map(field => field.elementId);
        this.sendAsyncMessage("FormAutofill:ClearFilledFields", { ids });
      }
    }
  }

  getSectionByElementId(elementId) {
    for (const sections of this.sectionsByRootId.values()) {
      const section = sections.find(s =>
        s.getFieldDetailByElementId(elementId)
      );
      if (section) {
        return section;
      }
    }
    return null;
  }

  static addMessageObserver(observer) {
    gMessageObservers.add(observer);
  }

  static removeMessageObserver(observer) {
    gMessageObservers.delete(observer);
  }

  notifyMessageObservers(callbackName, data) {
    for (let observer of gMessageObservers) {
      try {
        if (callbackName in observer) {
          observer[callbackName](
            data,
            this.manager.browsingContext.topChromeWindow
          );
        }
      } catch (ex) {
        console.error(ex);
      }
    }
  }

  /**
   * Autofill Developer Tools Related API.
   * API Below are used by autofill developer tools.
   * Do not change the function name or argument unless we are going to update
   * the autofill developer tool as well.
   */

  /**
   * Autofill Developer Tool API to inspect the autofill fields in this
   * tab.
   *
   * @param {Array<object>} overwriteFieldDetails
   *        A list of FieldDetail object to overwrite the detected result.
   *        This is used by the developer tool to correct the inspected
   *        result.
   * @returns {Array<object>}
   *        A list of sections representing the inspected result for this page.
   */
  async inspectFields(overwriteFieldDetails = []) {
    // Start with inspecting the fields in the top-level
    const topBC = this.browsingContext.top;
    const actor = FormAutofillParent.getActor(topBC);
    const fields = await actor.sendQuery("FormAutofill:InspectFields");

    if (!fields.length) {
      return [];
    }

    // Group fields that belong to the same form.
    const fieldsByForm = [];
    const rootElementIdByFormIndex = {};
    for (const field of fields) {
      let index = rootElementIdByFormIndex[field.rootElementId];
      if (index == undefined) {
        index = fieldsByForm.length;
        rootElementIdByFormIndex[field.rootElementId] = index;
        fieldsByForm.push([]);
      }
      fieldsByForm[index].push(field);
    }

    // Use `onFieldsDetected` function to simulate the behavior that when
    // users click on a field, we will also identify fields that are in an <iframe>
    const allSections = [];
    for (const formFields of fieldsByForm) {
      const msg = "FormAutofill:InspectFields";
      const [fieldDetails] = await this.identifyAllSubTreeFields(
        topBC,
        null,
        formFields,
        msg
      );

      fieldDetails.forEach(field => {
        const overwriteField = overwriteFieldDetails.find(
          ow => ow.inspectId == field.inspectId
        );
        if (overwriteField) {
          Object.assign(field, overwriteField);
        }
      });

      const formSections = FormAutofillParent.parseAndClassifyFields(
        fieldDetails,
        { ignoreUnknownField: false }
      );
      if (formSections.length) {
        allSections.push(formSections);
      }
    }
    return allSections;
  }

  #getTemporaryRecordForTab(collectionName) {
    // The temporary record is stored in the top-level actor.
    const topBC = this.browsingContext.top;
    const actor = FormAutofillParent.getActor(topBC);
    return actor?.temporaryRecords?.[collectionName] ?? [];
  }

  /**
   * Autofill Developer Tools Related API:
   * Add test records for this tab.
   *
   * @param {Array<object>} records
   *        A list of address or credit card records
   */
  async setTemporaryRecordsForTab(records) {
    const topBC = this.browsingContext.top;
    const actor = FormAutofillParent.getActor(topBC);
    actor.temporaryRecords = {
      [ADDRESSES_COLLECTION_NAME]: [],
      [CREDITCARDS_COLLECTION_NAME]: [],
    };

    for (const record of records) {
      const fields = Object.keys(record);
      if (!fields.length) {
        continue;
      }
      const collection = FormAutofillUtils.getCollectionNameFromFieldName(
        fields[0]
      );
      const storage =
        collection == ADDRESSES_COLLECTION_NAME
          ? lazy.gFormAutofillStorage.addresses
          : lazy.gFormAutofillStorage.creditCards;
      // Since we don't define the pattern for the passed 'record',
      // we need to normalize it first.
      storage._normalizeRecord(record);
      await storage.computeFields(record);

      actor.temporaryRecords[collection]?.push(record);
    }
  }
}
