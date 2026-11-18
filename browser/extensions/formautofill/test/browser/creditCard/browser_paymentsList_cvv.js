/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Covers the read-only payment methods list on the redesigned settings pane,
// which builds its rows from FormAutofillPreferences.makePaymentsListItems()
// rather than from the manageCreditCards.xhtml dialog.

const { FormAutofillPreferences } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillPreferences.sys.mjs"
);

const SUPPORTED_PREF = "extensions.formautofill.creditCards.cvv.supported";
const CVV_PREF = "extensions.formautofill.creditCards.cvv.enabled";

async function getPaymentRow() {
  await FormAutofillPreferences.prototype.initializePaymentsStorage();
  const items = await FormAutofillPreferences.prototype.makePaymentsListItems();
  const rows = items.filter(item => item.id == "payment-item");
  is(rows.length, 1, "One payment method is listed");
  return rows[0];
}

add_setup(async function () {
  // Security codes are only handled once the feature is rolled out. Roll it out
  // for the whole file so each task can toggle CVV_PREF on its own.
  // The security code is supported only while the Rust store serves.
  await switchCreditCardStore({ rust: true });
  await SpecialPowers.pushPrefEnv({ set: [[SUPPORTED_PREF, "on"]] });
  await removeAllRecords();
});

add_task(async function test_indicationShownForSavedSecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "123" }));

  const row = await getPaymentRow();
  is(
    row.l10nId,
    "payment-moz-box-item-with-security-code-2",
    "The row uses the string that notes a saved security code"
  );
  ok(
    !JSON.stringify(row.l10nArgs).includes("123"),
    `The security code is not passed to Fluent: ${JSON.stringify(row.l10nArgs)}`
  );

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_noIndicationWhenPrefDisabled() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, false]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "123" }));

  const row = await getPaymentRow();
  is(
    row.l10nId,
    "payment-moz-box-item",
    "The row is left alone while the pref is disabled"
  );

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_noIndicationWithoutSavedSecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(TEST_CREDIT_CARD_1);

  const row = await getPaymentRow();
  is(
    row.l10nId,
    "payment-moz-box-item",
    "A card without a security code keeps the plain string"
  );

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_noIndicationForEmptySecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "" }));

  const row = await getPaymentRow();
  is(
    row.l10nId,
    "payment-moz-box-item",
    "An empty security code does not count as a saved one"
  );

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

// TEST_CREDIT_CARD_4 carries only a number, so cc-exp is absent and there is no
// expiry to join the indicator onto.
add_task(async function test_indicationWithoutExpiryDate() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_4, { "cc-csc": "123" }));

  const row = await getPaymentRow();
  is(
    row.l10nId,
    "payment-moz-box-item-security-code-only-2",
    "A card with no expiry uses the indicator-only string"
  );
  is(row.l10nArgs.expDate, "", "There is no expiry date to show");

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_noIndicationBeforeRollout() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "off"],
      [CVV_PREF, true],
    ],
  });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "123" }));

  const row = await getPaymentRow();
  is(
    row.l10nId,
    "payment-moz-box-item",
    "No security code is indicated before the feature is rolled out"
  );

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});
