/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implements doorhanger singleton that wraps up the PopupNotifications and handles
 * the doorhager UI for formautofill related features.
 */

import { AutofillDataTypes } from "resource://gre/modules/shared/AutofillDataTypes.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Address: "resource://gre/modules/GeckoViewAutocomplete.sys.mjs",
  CreditCard: "resource://gre/modules/GeckoViewAutocomplete.sys.mjs",
  GeckoViewAutocomplete: "resource://gre/modules/GeckoViewAutocomplete.sys.mjs",
  GeckoViewPrompter:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPrompter.sys.mjs",
});

// Sync with Autocomplete.SaveOption.Hint in Autocomplete.java
const CreditCardStorageHint = {
  NONE: 0,
  GENERATED: 1 << 0,
  LOW_CONFIDENCE: 1 << 1,
};

export let FormAutofillPrompter = {
  _createMessage(creditCards) {
    let hint = CreditCardStorageHint.NONE;
    return {
      // Sync with PromptController
      type: "Autocomplete:Save:CreditCard",
      hint,
      creditCards,
    };
  },

  _createAddressMessage(addresses) {
    return {
      // Sync with PromptController
      type: "Autocomplete:Save:Address",
      addresses,
    };
  },

  async promptToSave(type, browser, storage, flowId, { oldRecord, newRecord }) {
    const config = {
      [AutofillDataTypes.ADDRESS]: {
        message: record =>
          this._createAddressMessage([lazy.Address.fromGecko(record)]),
        onSave: value => lazy.GeckoViewAutocomplete.onAddressSave(value),
      },
      [AutofillDataTypes.CREDIT_CARD]: {
        message: record =>
          this._createMessage([lazy.CreditCard.fromGecko(record)]),
        onSave: value => lazy.GeckoViewAutocomplete.onCreditCardSave(value),
      },
    }[type];

    // Android only supports capturing addresses and credit cards.
    if (!config) {
      return;
    }

    if (oldRecord) {
      newRecord = { ...oldRecord, ...newRecord };
    }

    const prompt = new lazy.GeckoViewPrompter(browser.documentGlobal);
    prompt.asyncShowPrompt(config.message(newRecord), result => {
      const selected = result?.selection?.value;
      if (!selected) {
        return;
      }
      config.onSave(selected);
    });
  },
};
