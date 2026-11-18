/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const SUPPORTED_PREF = "extensions.formautofill.creditCards.cvv.supported";
const CVV_PREF = "extensions.formautofill.creditCards.cvv.enabled";
const DIALOG_SIZE = "width=600,height=400";

const TEST_CARD_WITH_CSC = Object.assign({}, TEST_CREDIT_CARD_1, {
  "cc-csc": "123",
});

async function withManageDialog(taskFn) {
  let win = window.openDialog(
    MANAGE_CREDIT_CARDS_DIALOG_URL,
    null,
    DIALOG_SIZE
  );
  await waitForFocusAndFormReady(win);
  try {
    await taskFn(win, win.document.querySelector("#credit-cards"));
  } finally {
    win.close();
  }
}

async function cleanupStorage(selRecords) {
  await removeCreditCards(
    Array.from(selRecords.options, option => option.value)
  );
}

add_setup(async function () {
  // Security codes are only handled once the feature is rolled out. Roll it out
  // for the whole file so each task can toggle CVV_PREF on its own.
  // The security code is supported only while the Rust store serves.
  await switchCreditCardStore({ rust: true });
  await SpecialPowers.pushPrefEnv({ set: [[SUPPORTED_PREF, "on"]] });
});

add_task(async function test_noIndicationBeforeRollout() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "off"],
      [CVV_PREF, true],
    ],
  });
  await setStorage(TEST_CARD_WITH_CSC);

  await withManageDialog(async (win, selRecords) => {
    is(selRecords.length, 1, "One credit card is shown");
    is(
      win.document.l10n.getAttributes(selRecords.options[0]).id,
      "credit-card-label-number-name-expiration-2",
      "The label is left unwrapped before the feature is rolled out"
    );
    await cleanupStorage(selRecords);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_noIndicationWhenPrefDisabled() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, false]] });
  await setStorage(TEST_CARD_WITH_CSC);

  await withManageDialog(async (win, selRecords) => {
    is(selRecords.length, 1, "One credit card is shown");
    is(
      win.document.l10n.getAttributes(selRecords.options[0]).id,
      "credit-card-label-number-name-expiration-2",
      "The label is left unwrapped while the pref is disabled"
    );
    ok(
      !selRecords.options[0].textContent.includes("123"),
      "The security code is not displayed"
    );
    await cleanupStorage(selRecords);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_indicationShownForSavedSecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(TEST_CARD_WITH_CSC);

  await withManageDialog(async (win, selRecords) => {
    is(selRecords.length, 1, "One credit card is shown");

    let option = selRecords.options[0];
    let l10nAttrs = win.document.l10n.getAttributes(option);
    is(
      l10nAttrs.id,
      "credit-card-label-with-security-code-2",
      "The label is wrapped by the security code string"
    );
    ok(
      l10nAttrs.args.label.includes("1111") &&
        l10nAttrs.args.label.includes("John Doe"),
      `The wrapped label carries the card description: ${l10nAttrs.args.label}`
    );
    ok(
      l10nAttrs.args.ariaLabel.includes("Visa"),
      `The wrapped aria-label still names the card type: ${l10nAttrs.args.ariaLabel}`
    );

    await win.document.l10n.translateElements([option]);
    ok(
      option.textContent.includes("CVV •••"),
      `The row indicates a saved security code: ${option.textContent}`
    );
    ok(
      !option.textContent.includes("123"),
      "The security code itself is never displayed"
    );
    let ariaLabel = option.getAttribute("aria-label");
    ok(ariaLabel, `The row has an accessibility label: ${ariaLabel}`);
    ok(
      ariaLabel.includes("CVV saved") && !ariaLabel.includes("123"),
      "The accessibility label indicates the code without revealing it"
    );

    await cleanupStorage(selRecords);
  });

  await SpecialPowers.popPrefEnv();
});

// cc-csc is not in VALID_CREDIT_CARD_FIELDS yet, so it round trips as an
// unknown field. _normalizeRecord() skips normalization for those before it
// gets to dropping empty values, so a cleared security code stays on the
// record as an empty string rather than being deleted.
add_task(async function test_noIndicationForEmptySecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "" }));

  await withManageDialog(async (win, selRecords) => {
    is(selRecords.length, 1, "One credit card is shown");

    let option = selRecords.options[0];
    is(
      win.document.l10n.getAttributes(option).id,
      "credit-card-label-number-name-expiration-2",
      "An empty security code does not count as a saved one"
    );

    await win.document.l10n.translateElements([option]);
    ok(
      !option.textContent.includes("CVV •••"),
      `The row carries no indication: ${option.textContent}`
    );

    await cleanupStorage(selRecords);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_noIndicationWithoutSavedSecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(TEST_CREDIT_CARD_1);

  await withManageDialog(async (win, selRecords) => {
    is(selRecords.length, 1, "One credit card is shown");

    let option = selRecords.options[0];
    is(
      win.document.l10n.getAttributes(option).id,
      "credit-card-label-number-name-expiration-2",
      "A card without a security code keeps the plain label"
    );

    await win.document.l10n.translateElements([option]);
    ok(
      !option.textContent.includes("CVV •••"),
      `The row carries no indication: ${option.textContent}`
    );

    await cleanupStorage(selRecords);
  });

  await SpecialPowers.popPrefEnv();
});
