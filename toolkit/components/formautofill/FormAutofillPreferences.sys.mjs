/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Injects the form autofill section into about:preferences.
 */

import { FormAutofill } from "resource://autofill/FormAutofill.sys.mjs";
import { FormAutofillUtils } from "resource://gre/modules/shared/FormAutofillUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
  formAutofillStorage: "resource://autofill/FormAutofillStorage.sys.mjs",
  ManageAddresses: "chrome://formautofill/content/manageDialog.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization(
      [
        "toolkit/formautofill/formAutofill.ftl",
        "branding/brand.ftl",
        "browser/preferences/preferences.ftl",
      ],
      true
    )
);

const EDIT_ADDRESS_URL = "chrome://formautofill/content/editAddress.xhtml";
const EDIT_CREDIT_CARD_URL =
  "chrome://formautofill/content/editCreditCard.xhtml";

export class FormAutofillPreferences {
  /**
   * Create the Form Autofill preference group.
   *
   * @param   {HTMLDocument} document
   * @returns {XULElement}
   */
  init(document) {
    this.createPreferenceGroup(document);
  }

  /**
   * Create Form Autofill preference group
   *
   * @param  {HTMLDocument} document
   */
  createPreferenceGroup(document) {
    const win = document.ownerGlobal;
    Services.obs.notifyObservers(win, "formautofill-preferences-initialized");
  }

  async initializePaymentsStorage() {
    await lazy.formAutofillStorage.initialize();
  }

  async initializeAddressesStorage() {
    await lazy.formAutofillStorage.initialize();
  }

  /**
   * Helper that sets OS Auth from the about:preferences, if authorized.
   *
   * @param  {object} win
   *          The browser window.
   * @param  {boolean} checked
   *          The new state to set OS auth for payments, which determines if its
   *          enabled or not. If not authorized, set to the current checked state.
   */
  static async trySetOSAuthEnabled(win, checked) {
    let messageText = await lazy.l10n.formatValueSync(
      "autofill-creditcard-os-dialog-message"
    );
    let captionText = await lazy.l10n.formatValueSync(
      "autofill-creditcard-os-auth-dialog-caption"
    );

    // Calling OSKeyStore.ensureLoggedIn() instead of FormAutofillUtils.verifyOSAuth()
    // since we want to authenticate user each time this setting is changed.

    // Note on Glean collection: because OSKeyStore.ensureLoggedIn() is not wrapped in
    // verifyOSAuth(), it will be documenting "success" for unsupported platforms
    // and won't record "fail_error", only "fail_user_canceled"
    let isAuthorized = (
      await lazy.OSKeyStore.ensureLoggedIn(messageText, captionText, win, false)
    ).authenticated;
    Glean.formautofill.promptShownOsReauth.record({
      trigger: "toggle_pref_os_auth",
      result: isAuthorized ? "success" : "fail_user_canceled",
    });

    if (!isAuthorized) {
      return;
    }

    // If target.checked is checked, enable OSAuth. Otherwise, reset the pref value.
    FormAutofillUtils.setOSAuthEnabled(checked);
    Glean.formautofill.requireOsReauthToggle.record({
      toggle_state: checked,
    });
  }

  async makePaymentsListItems() {
    const records = await lazy.formAutofillStorage.creditCards.getAll();

    let items = [];

    if (!records.length) {
      items = [
        {
          id: "no-payments-stored",
          l10nId: "payments-no-payments-stored-message",
          control: "moz-box-item",
          l10nArgs: {},
        },
      ];
    } else {
      items = records
        .sort(record => record.timeCreated)
        .map(record => {
          const config = {
            id: "payment-item",
            control: "moz-box-item",
            l10nId: "payment-moz-box-item",
            iconSrc: "chrome://browser/skin/payment-methods-16.svg",
            l10nArgs: {
              cardNumber: record["cc-number"].replace(/^(\*+)(\d+)$/, "$1 $2"),
              expDate: record["cc-exp"].replace(/^(\d{4})-\d{2}$/, "XX/$1"),
            },
            options: [
              {
                control: "moz-button",
                iconSrc: "chrome://global/skin/icons/delete.svg",
                type: "icon",
                l10nId: "payments-delete-payment-button-label",
                controlAttrs: {
                  slot: "actions",
                  action: "remove",
                  guid: record.guid,
                },
              },
              {
                control: "moz-button",
                iconSrc: "chrome://global/skin/icons/edit.svg",
                type: "icon",
                l10nId: "payments-edit-payment-button-label",
                controlAttrs: {
                  slot: "actions",
                  action: "edit",
                  guid: record.guid,
                },
              },
            ],
          };

          return config;
        });
    }

    return [
      {
        id: "payments-list-header",
        control: "moz-box-item",
        l10nId: "payments-list-header",
        slot: "header",
      },
      ...items,
    ];
  }

  async makeAddressesListItems() {
    const addresses = await lazy.formAutofillStorage.addresses.getAll();
    const records = addresses.slice().reverse();

    let items = [];

    if (!records.length) {
      items = [
        {
          id: "no-addresses-stored",
          l10nId: "addresses-no-addresses-stored-message",
          l10nArgs: {},
          control: "moz-box-item",
        },
      ];
    } else {
      items = records.map(record => {
        const addressFormatted = [
          record["street-address"],
          record["address-level2"],
          record["address-level1"],
          record.country,
          record["postal-code"],
        ]
          .filter(Boolean)
          .join(", ");

        const config = {
          id: "address-item",
          control: "moz-box-item",
          l10nId: "address-moz-box-item",
          iconSrc: "chrome://browser/skin/notification-icons/geo.svg",
          l10nArgs: {
            name: record.name || addressFormatted,
            address: record.name ? addressFormatted : "",
          },
          options: [
            {
              id: "delete-address-button",
              control: "moz-button",
              iconSrc: "chrome://global/skin/icons/delete.svg",
              type: "icon",
              l10nId: "addreses-delete-address-button-label",
              controlAttrs: {
                slot: "actions",
                action: "remove",
                guid: record.guid,
              },
            },
            {
              id: "edit-address-button",
              control: "moz-button",
              iconSrc: "chrome://global/skin/icons/edit.svg",
              type: "icon",
              l10nId: "addreses-edit-address-button-label",
              controlAttrs: {
                slot: "actions",
                action: "edit",
                guid: record.guid,
              },
            },
          ],
        };

        return config;
      });
    }

    return [
      {
        id: "addresses-list-header",
        control: "moz-box-item",
        l10nId: "addresses-list-header",
        slot: "header",
      },
      ...items,
    ];
  }

  /**
   * Open the browser window modal to prompt the user whether
   * or they want to remove their payment.
   *
   * @param  {string} guid
   *          The guid of the payment item we are prompting to remove.
   * @param  {object} browsingContext
   *          Browsing context to open the prompt in
   * @param  {string} title
   *          The title text displayed in the modal to prompt the user with
   * @param  {string} confirmBtn
   *        The text for confirming removing a payment method
   * @param  {string} cancelBtn
   *        The text for cancelling removing a payment method
   */
  async openRemovePaymentDialog(
    guid,
    browsingContext,
    title,
    confirmBtn,
    cancelBtn
  ) {
    const flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;
    const result = await Services.prompt.asyncConfirmEx(
      browsingContext,
      Services.prompt.MODAL_TYPE_INTERNAL_WINDOW,
      title,
      null,
      flags,
      confirmBtn,
      cancelBtn,
      null,
      null,
      false
    );

    const propBag = result.QueryInterface(Ci.nsIPropertyBag2);
    // Confirmed
    if (propBag.get("buttonNumClicked") === 0) {
      lazy.formAutofillStorage.creditCards.remove(guid);
    }
  }

  async openEditCreditCardDialog(guid, window) {
    const creditCard = await lazy.formAutofillStorage.creditCards.get(guid);
    return FormAutofillPreferences.openEditCreditCardDialog(creditCard, window);
  }
  /**
   * Open the edit credit card dialog to create/edit a credit card.
   *
   * @param  {object} creditCard
   *         The credit card we want to edit.
   */
  static async openEditCreditCardDialog(creditCard, window) {
    // Ask for reauth if user is trying to edit an existing credit card.
    if (creditCard) {
      const promptMessage = FormAutofillUtils.reauthOSPromptMessage(
        "autofill-edit-payment-method-os-prompt-macos",
        "autofill-edit-payment-method-os-prompt-windows",
        "autofill-edit-payment-method-os-prompt-other"
      );
      let verified;
      let result;
      try {
        verified = await FormAutofillUtils.verifyUserOSAuth(
          FormAutofill.AUTOFILL_CREDITCARDS_OS_AUTH_LOCKED_PREF,
          promptMessage
        );
        result = verified ? "success" : "fail_user_canceled";
      } catch (ex) {
        result = "fail_error";
        throw ex;
      } finally {
        Glean.formautofill.promptShownOsReauth.record({
          trigger: "edit",
          result,
        });
      }
      if (!verified) {
        return;
      }
    }

    let decryptedCCNumObj = {};
    let errorResult = 0;
    if (creditCard && creditCard["cc-number-encrypted"]) {
      try {
        decryptedCCNumObj["cc-number"] = await lazy.OSKeyStore.decrypt(
          creditCard["cc-number-encrypted"],
          "formautofill_cc"
        );
      } catch (ex) {
        errorResult = ex.result;
        if (ex.result == Cr.NS_ERROR_ABORT) {
          // User shouldn't be ask to reauth here, but it could happen.
          // Return here and skip opening the dialog.
          return;
        }
        // We've got ourselves a real error.
        // Recover from encryption error so the user gets a chance to re-enter
        // unencrypted credit card number.
        decryptedCCNumObj["cc-number"] = "";
        console.error(ex);
      } finally {
        Glean.creditcard.osKeystoreDecrypt.record({
          isDecryptSuccess: errorResult === 0,
          errorResult,
          trigger: "edit",
        });
      }
    }
    let decryptedCreditCard = Object.assign({}, creditCard, decryptedCCNumObj);
    window.gSubDialog.open(
      EDIT_CREDIT_CARD_URL,
      { features: "resizable=no" },
      {
        record: decryptedCreditCard,
      }
    );
  }
  /**
   * Open the browser window modal to prompt the user whether
   * or they want to remove their address.
   *
   * @param  {string} guid
   *          The guid of the address item we are prompting to remove.
   * @param  {object} browsingContext
   *          Browsing context to open the prompt in
   * @param  {string} title
   *          The title text displayed in the modal to prompt the user with
   * @param  {string} confirmBtn
   *        The text for confirming the removal of an address
   * @param  {string} cancelBtn
   *        The text for cancelling removal of an address
   */
  async openRemoveAddressDialog(
    guid,
    browsingContext,
    title,
    confirmBtn,
    cancelBtn
  ) {
    const flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;
    const result = await Services.prompt.asyncConfirmEx(
      browsingContext,
      Services.prompt.MODAL_TYPE_INTERNAL_WINDOW,
      title,
      null,
      flags,
      confirmBtn,
      cancelBtn,
      null,
      null,
      false
    );

    const propBag = result.QueryInterface(Ci.nsIPropertyBag2);
    // Confirmed
    if (propBag.get("buttonNumClicked") === 0) {
      lazy.formAutofillStorage.addresses.remove(guid);
    }
  }

  async openEditAddressDialog(guid, window) {
    const address = await lazy.formAutofillStorage.addresses.get(guid);
    return FormAutofillPreferences.openEditAddressDialog(address, window);
  }
  /**
   * Open the edit address dialog to create/edit an address.
   *
   * @param  {object} address
   *         The address we want to edit.
   */
  static async openEditAddressDialog(address, window) {
    window.gSubDialog.open(EDIT_ADDRESS_URL, undefined, {
      record: address ?? undefined,
      // Don't validate in preferences since it's fine for fields to be missing
      // for autofill purposes. For PaymentRequest addresses get more validation.
      noValidate: true,
      l10nStrings: lazy.ManageAddresses.getAddressL10nStrings(),
    });
  }

  static openPaymentPreference() {
    const win = Services.wm.getMostRecentBrowserWindow();
    win.openPreferences("privacy-payment-methods-autofill");
  }

  static openAddressPreference() {
    const win = Services.wm.getMostRecentBrowserWindow();
    win.openPreferences("privacy-address-autofill");
  }
}
