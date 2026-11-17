/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);
const { AutofillTelemetry } = ChromeUtils.importESModule(
  "resource://gre/modules/shared/AutofillTelemetry.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  CreditCard: "resource://gre/modules/CreditCard.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  formAutofillStorage: "resource://autofill/FormAutofillStorage.sys.mjs",
  FormAutofillPreferences:
    "resource://autofill/FormAutofillPreferences.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  FormAutofill.defineLogGetter(lazy, "manageAddresses")
);

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["	browser/preferences/formAutofill.ftl"], true)
);

class ManageRecords {
  constructor(subStorageName, elements) {
    this._storageInitPromise = lazy.formAutofillStorage.initialize();
    this._subStorageName = subStorageName;
    this._elements = elements;
    this._newRequest = false;
    this._isLoadingRecords = false;
    this.prefWin = window.opener;
    window.addEventListener("load", this, { once: true });
  }

  async init() {
    await this.loadRecords();
    this.attachEventListeners();
    // For testing only: Notify when the dialog is ready for interaction
    window.dispatchEvent(new CustomEvent("FormReadyForTests"));
  }

  uninit() {
    lazy.log.debug("uninit");
    this.detachEventListeners();
    this._elements = null;
  }

  /**
   * Get the selected options on the addresses element.
   *
   * @returns {Array<DOMElement>}
   */
  get _selectedOptions() {
    return Array.from(this._elements.records.selectedOptions);
  }

  /**
   * Get storage and ensure it has been initialized.
   *
   * @returns {object}
   */
  async getStorage() {
    await this._storageInitPromise;
    return lazy.formAutofillStorage[this._subStorageName];
  }

  /**
   * Load records and render them. This function is a wrapper for _loadRecords
   * to ensure any reentrant will be handled well.
   */
  async loadRecords() {
    // This function can be early returned when there is any reentrant happends.
    // "_newRequest" needs to be set to ensure all changes will be applied.
    if (this._isLoadingRecords) {
      this._newRequest = true;
      return;
    }
    this._isLoadingRecords = true;

    await this._loadRecords();

    // _loadRecords should be invoked again if there is any multiple entrant
    // during running _loadRecords(). This step ensures that the latest request
    // still is applied.
    while (this._newRequest) {
      this._newRequest = false;
      await this._loadRecords();
    }
    this._isLoadingRecords = false;

    // For testing only: Notify when records are loaded
    this._elements.records.dispatchEvent(new CustomEvent("RecordsLoaded"));
  }

  async _loadRecords() {
    let storage = await this.getStorage();
    let records = await storage.getAll();
    // Sort by last used time starting with most recent
    records.sort((a, b) => {
      let aLastUsed = a.timeLastUsed || a.timeLastModified;
      let bLastUsed = b.timeLastUsed || b.timeLastModified;
      return bLastUsed - aLastUsed;
    });
    await this.renderRecordElements(records);
    this.updateButtonsStates(this._selectedOptions.length);
  }

  /**
   * Render the records onto the page while maintaining selected options if
   * they still exist.
   *
   * @param  {Array<object>} records
   */
  async renderRecordElements(records) {
    let selectedGuids = this._selectedOptions.map(option => option.value);
    this.clearRecordElements();
    for (let record of records) {
      let { id, args, raw } = await this.getLabelInfo(record);
      let option = new Option(
        raw ?? "",
        record.guid,
        false,
        selectedGuids.includes(record.guid)
      );
      if (id) {
        document.l10n.setAttributes(option, id, args);
      }

      option.record = record;
      this._elements.records.appendChild(option);
    }
  }

  /**
   * Remove all existing record elements.
   */
  clearRecordElements() {
    const parentElement = this._elements.records;
    while (parentElement.lastChild) {
      parentElement.removeChild(parentElement.lastChild);
    }
  }

  /**
   * Remove records by selected options.
   *
   * @param  {Array<DOMElement>} options
   */
  async removeRecords(options) {
    let storage = await this.getStorage();
    // Pause listening to storage change event to avoid triggering `loadRecords`
    // when removing records
    Services.obs.removeObserver(this, "formautofill-storage-changed");

    for (let option of options) {
      storage.remove(option.value);
      option.remove();
    }
    this.updateButtonsStates(this._selectedOptions);

    // Resume listening to storage change event
    Services.obs.addObserver(this, "formautofill-storage-changed");
    // For testing only: notify record(s) has been removed
    this._elements.records.dispatchEvent(new CustomEvent("RecordsRemoved"));

    for (let i = 0; i < options.length; i++) {
      AutofillTelemetry.recordManageEvent(this.dataType, "delete");
    }
  }

  /**
   * Enable/disable the Edit and Remove buttons based on number of selected
   * options.
   *
   * @param  {number} selectedCount
   */
  updateButtonsStates(selectedCount) {
    lazy.log.debug("updateButtonsStates:", selectedCount);
    if (selectedCount == 0) {
      this._elements.edit.setAttribute("disabled", "disabled");
      this._elements.remove.setAttribute("disabled", "disabled");
    } else if (selectedCount == 1) {
      this._elements.edit.removeAttribute("disabled");
      this._elements.remove.removeAttribute("disabled");
    } else if (selectedCount > 1) {
      this._elements.edit.setAttribute("disabled", "disabled");
      this._elements.remove.removeAttribute("disabled");
    }
    this._elements.add.disabled = !Services.prefs.getBoolPref(
      `extensions.formautofill.${this._subStorageName}.enabled`
    );
  }

  /**
   * Handle events
   *
   * @param  {DOMEvent} event
   */
  handleEvent(event) {
    switch (event.type) {
      case "load": {
        this.init();
        break;
      }
      case "click": {
        this.handleClick(event);
        break;
      }
      case "change": {
        this.updateButtonsStates(this._selectedOptions.length);
        break;
      }
      case "unload": {
        this.uninit();
        break;
      }
      case "keypress": {
        this.handleKeyPress(event);
        break;
      }
      case "contextmenu": {
        event.preventDefault();
        break;
      }
    }
  }

  /**
   * Handle click events
   *
   * @param  {DOMEvent} event
   */
  handleClick(event) {
    if (event.target == this._elements.remove) {
      this.removeRecords(this._selectedOptions);
    } else if (event.target == this._elements.add) {
      this.openEditDialog();
    } else if (
      event.target == this._elements.edit ||
      (event.target.parentNode == this._elements.records && event.detail > 1)
    ) {
      this.openEditDialog(this._selectedOptions[0].record);
    }
  }

  /**
   * Handle key press events
   *
   * @param  {DOMEvent} event
   */
  handleKeyPress(event) {
    if (event.keyCode == KeyEvent.DOM_VK_ESCAPE) {
      window.close();
    }
    if (event.keyCode == KeyEvent.DOM_VK_DELETE) {
      this.removeRecords(this._selectedOptions);
    }
  }

  observe(_subject, topic, _data) {
    switch (topic) {
      case "formautofill-storage-changed": {
        this.loadRecords();
      }
    }
  }

  /**
   * Attach event listener
   */
  attachEventListeners() {
    window.addEventListener("unload", this, { once: true });
    window.addEventListener("keypress", this);
    window.addEventListener("contextmenu", this);
    this._elements.records.addEventListener("change", this);
    this._elements.records.addEventListener("click", this);
    this._elements.controlsContainer.addEventListener("click", this);
    Services.obs.addObserver(this, "formautofill-storage-changed");
  }

  /**
   * Remove event listener
   */
  detachEventListeners() {
    window.removeEventListener("keypress", this);
    window.removeEventListener("contextmenu", this);
    this._elements.records.removeEventListener("change", this);
    this._elements.records.removeEventListener("click", this);
    this._elements.controlsContainer.removeEventListener("click", this);
    Services.obs.removeObserver(this, "formautofill-storage-changed");
  }
}

export class ManageAddresses extends ManageRecords {
  dataType = lazy.AutofillDataTypes.ADDRESS;

  constructor(elements) {
    super("addresses", elements);
    elements.add.setAttribute(
      "search-l10n-ids",
      lazy.FormAutofillUtils.EDIT_ADDRESS_L10N_IDS.join(",")
    );
    AutofillTelemetry.recordManageEvent(this.dataType, "show");
  }

  static getAddressL10nStrings() {
    const l10nIds = [
      ...lazy.FormAutofillUtils.MANAGE_ADDRESSES_L10N_IDS,
      ...lazy.FormAutofillUtils.EDIT_ADDRESS_L10N_IDS,
    ];

    return l10nIds.reduce(
      (acc, id) => ({
        ...acc,
        [id]: lazy.l10n.formatValueSync(id),
      }),
      {}
    );
  }

  /**
   * Open the edit address dialog to create/edit an address.
   *
   * @param  {object} address [optional]
   */
  openEditDialog(address) {
    return lazy.FormAutofillPreferences.openEditAddressDialog(
      address,
      this.prefWin
    );
  }

  getLabelInfo(address) {
    return { raw: lazy.FormAutofillUtils.getAddressLabel(address) };
  }
}

export class ManageCreditCards extends ManageRecords {
  dataType = lazy.AutofillDataTypes.CREDIT_CARD;

  constructor(elements) {
    super("creditCards", elements);
    elements.add.setAttribute(
      "search-l10n-ids",
      lazy.FormAutofillUtils.EDIT_CREDITCARD_L10N_IDS.join(",")
    );

    this._isDecrypted = false;
    AutofillTelemetry.recordManageEvent(this.dataType, "show");
  }

  /**
   * Open the edit address dialog to create/edit a credit card.
   *
   * @param  {object} creditCard [optional]
   */
  async openEditDialog(creditCard) {
    return lazy.FormAutofillPreferences.openEditCreditCardDialog(
      creditCard,
      this.prefWin
    );
  }

  /**
   * Get credit card display label. It should display masked numbers and the
   * cardholder's name, separated by a comma.
   *
   * @param {object} creditCard
   * @returns {Promise<string>}
   */
  async getLabelInfo(creditCard) {
    // The card type is displayed visually using an image. For a11y, we need
    // to expose it as text. We do this using aria-label. However,
    // aria-label overrides the text content, so we must include that also.
    // Since the text content is generated by Fluent, aria-label must be
    // generated by Fluent also.
    const type = creditCard["cc-type"];
    const typeL10nId = lazy.CreditCard.getNetworkL10nId(type);
    const typeName = typeL10nId
      ? await document.l10n.formatValue(typeL10nId)
      : (type ?? ""); // Unknown card type
    const labelInfo = lazy.CreditCard.getLabelInfo({
      name: creditCard["cc-name"],
      number: creditCard["cc-number"],
      month: creditCard["cc-exp-month"],
      year: creditCard["cc-exp-year"],
      type: typeName,
    });

    if (!FormAutofill.isAutofillCreditCardCVVEnabled || !creditCard["cc-csc"]) {
      return labelInfo;
    }
    return this.#withSecurityCodeLabel(labelInfo, creditCard);
  }

  /**
   * Wrap a credit card label so that it notes a security code is saved for the
   * card. The wrapping string takes the label as an argument, so the label has
   * to be formatted here rather than by the element it ends up on.
   *
   * @param {object} labelInfo
   *        The pair returned by CreditCard.getLabelInfo().
   * @param {string} labelInfo.id
   *        Fluent id of the label to wrap.
   * @param {object} labelInfo.args
   *        Fluent arguments for that label.
   * @param {object} creditCard
   *        The card, whose network decides what the security code is called.
   * @returns {Promise<object>} An { id, args } pair for the wrapping string.
   */
  async #withSecurityCodeLabel({ id, args }, creditCard) {
    const [message] = await document.l10n.formatMessages([{ id, args }]);
    const ariaLabel = message?.attributes?.find(
      attribute => attribute.name == "aria-label"
    )?.value;
    return {
      id: "credit-card-label-with-security-code-2",
      args: {
        label: message?.value ?? "",
        ariaLabel: ariaLabel ?? message?.value ?? "",
        securityCode: lazy.CreditCard.formatSecurityCodeName(creditCard),
      },
    };
  }

  async renderRecordElements(records) {
    // Revert back to encrypted form when re-rendering happens
    this._isDecrypted = false;
    // Display third-party card icons when possible
    this._elements.records.classList.toggle(
      "branded",
      AppConstants.MOZILLA_OFFICIAL
    );
    await super.renderRecordElements(records);

    let options = this._elements.records.options;
    for (let option of options) {
      let record = option.record;
      if (record && record["cc-type"]) {
        option.setAttribute("cc-type", record["cc-type"]);
      } else {
        option.removeAttribute("cc-type");
      }
    }
  }

  updateButtonsStates(selectedCount) {
    super.updateButtonsStates(selectedCount);
  }

  handleClick(event) {
    super.handleClick(event);
  }
}
