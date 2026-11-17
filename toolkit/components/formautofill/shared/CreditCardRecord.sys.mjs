/* eslint-disable no-useless-concat */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { CreditCard } from "resource://gre/modules/CreditCard.sys.mjs";
import { FormAutofillNameUtils } from "resource://gre/modules/shared/FormAutofillNameUtils.sys.mjs";
import { FormAutofill } from "resource://autofill/FormAutofill.sys.mjs";

/**
 * The CreditCardRecord class serves to handle and normalize internal credit card records.
 * Unlike the CreditCard class, which represents actual card data, CreditCardRecord is used
 * for processing and consistent data representation.
 */
export class CreditCardRecord {
  /**
   * The fields a store keeps encrypted, mapped to the field its ciphertext is
   * kept under.
   */
  static #ENCRYPTED_FIELDS = {
    "cc-number": "cc-number-encrypted",
  };

  /**
   * Where a record keeps the ciphertext of an encrypted field.
   *
   * @param {string} field An encrypted field, e.g. "cc-number".
   * @returns {string} The field its ciphertext is kept under.
   */
  static ciphertextField(field) {
    const ciphertextField = this.#ENCRYPTED_FIELDS[field];
    if (!ciphertextField) {
      throw new Error(`${field} is not stored encrypted`);
    }
    return ciphertextField;
  }

  /**
   * Drop the security code unless capturing and storing it is enabled. Every
   * store applies this to each record it reads out, unless the read is raw.
   *
   * @param {object} creditCard
   */
  static stripSecurityCodeWhenDisabled(creditCard) {
    if (!FormAutofill.isAutofillCreditCardCVVEnabled) {
      delete creditCard["cc-csc"];
    }
  }

  /**
   * Computes derived fields from the basic fields in the CreditCard object.
   *
   * @param {object} creditCard The credit card object
   */
  static computeFields(creditCard) {
    this.#computeCCNameFields(creditCard);
    this.#computeCCExpirationDateFields(creditCard);
    this.#computeCCTypeField(creditCard);
  }

  static #computeCCExpirationDateFields(creditCard) {
    if (!("cc-exp" in creditCard)) {
      if (creditCard["cc-exp-month"] && creditCard["cc-exp-year"]) {
        creditCard["cc-exp"] =
          String(creditCard["cc-exp-year"]) +
          "-" +
          String(creditCard["cc-exp-month"]).padStart(2, "0");
      } else {
        creditCard["cc-exp"] = "";
      }
    }
  }

  static #computeCCNameFields(creditCard) {
    if (!("cc-given-name" in creditCard)) {
      const nameParts = FormAutofillNameUtils.splitName(creditCard["cc-name"]);
      creditCard["cc-given-name"] = nameParts.given;
      creditCard["cc-additional-name"] = nameParts.middle;
      creditCard["cc-family-name"] = nameParts.family;
    }
  }

  static #computeCCTypeField(creditCard) {
    const type = CreditCard.getType(creditCard["cc-number"]);
    if (type) {
      creditCard["cc-type"] = type;
    }
  }

  /**
   * Normalizes credit card fields by removing derived fields from the CreditCard, leaving the basic fields.
   *
   * @param {object} creditCard The credit card object
   */
  static normalizeFields(creditCard) {
    this.#normalizeCCNameFields(creditCard);
    this.#normalizeCCNumberFields(creditCard);
    this.#normalizeCCExpirationDateFields(creditCard);
    this.#normalizeCCTypeFields(creditCard);
    this.#normalizeCCSecurityCodeFields(creditCard);
  }

  static #normalizeCCNameFields(creditCard) {
    if (!creditCard["cc-name"]) {
      creditCard["cc-name"] = FormAutofillNameUtils.joinNameParts({
        given: creditCard["cc-given-name"] ?? "",
        middle: creditCard["cc-additional-name"] ?? "",
        family: creditCard["cc-family-name"] ?? "",
      });
    }

    delete creditCard["cc-given-name"];
    delete creditCard["cc-additional-name"];
    delete creditCard["cc-family-name"];
  }

  static #normalizeCCNumberFields(creditCard) {
    if (!("cc-number" in creditCard)) {
      return;
    }

    if (!CreditCard.isValidNumber(creditCard["cc-number"])) {
      delete creditCard["cc-number"];
      return;
    }

    const card = new CreditCard({ number: creditCard["cc-number"] });
    creditCard["cc-number"] = card.number;
  }

  static #normalizeCCExpirationDateFields(creditCard) {
    let normalizedExpiration = CreditCard.normalizeExpiration({
      expirationMonth: creditCard["cc-exp-month"],
      expirationYear: creditCard["cc-exp-year"],
      expirationString: creditCard["cc-exp"],
    });

    creditCard["cc-exp-month"] = normalizedExpiration.month ?? "";
    creditCard["cc-exp-year"] = normalizedExpiration.year ?? "";
    delete creditCard["cc-exp"];
  }

  static #normalizeCCTypeFields(creditCard) {
    // Let's overwrite the credit card type with auto-detect algorithm
    creditCard["cc-type"] = CreditCard.getType(creditCard["cc-number"]) ?? "";
  }

  static #normalizeCCSecurityCodeFields(creditCard) {
    this.stripSecurityCodeWhenDisabled(creditCard);
    // An empty value is kept: an update reads it as clearing the stored code.
    if (!creditCard["cc-csc"]) {
      return;
    }
    const securityCode = String(creditCard["cc-csc"]).trim();
    if (/^\d{3,4}$/.test(securityCode)) {
      creditCard["cc-csc"] = securityCode;
    } else {
      delete creditCard["cc-csc"];
    }
  }
}
