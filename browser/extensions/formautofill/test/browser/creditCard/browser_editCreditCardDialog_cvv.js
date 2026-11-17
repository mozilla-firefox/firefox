/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const CVV_PREF = "extensions.formautofill.creditCards.cvv.enabled";

function getSecurityCodeField(win) {
  return win.document.querySelector("#cc-csc");
}

add_setup(async function () {
  // The feature is gated on rollout as well as on the user's own choice. Roll
  // it out for the whole file so each task can toggle CVV_PREF on its own.
  // The security code is supported only while the Rust store serves.
  await switchCreditCardStore({ rust: true });
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.creditCards.cvv.supported", "on"]],
  });

  let { formAutofillStorage } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillStorage.sys.mjs"
  );
  await formAutofillStorage.initialize();
});

add_task(async function test_securityCodeAbsentWhenPrefDisabled() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, false]] });

  await testDialog(EDIT_CREDIT_CARD_DIALOG_URL, win => {
    ok(
      !getSecurityCodeField(win),
      "The security code field is taken out of the form"
    );
    ok(
      win.document.querySelector("#cc-csc-container").hidden,
      "The security code container stays hidden"
    );
    win.document.querySelector("#cancel").click();
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_securityCodeAbsentWhileJsonStoreServes() {
  await switchCreditCardStore({ rust: false });
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });

  await testDialog(EDIT_CREDIT_CARD_DIALOG_URL, win => {
    ok(
      !getSecurityCodeField(win),
      "The security code field is taken out of the form"
    );
    win.document.querySelector("#cancel").click();
  });

  await SpecialPowers.popPrefEnv();
  await switchCreditCardStore({ rust: true });
});

add_task(async function test_securityCodeShownWhenPrefEnabled() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });

  await testDialog(EDIT_CREDIT_CARD_DIALOG_URL, async win => {
    let field = getSecurityCodeField(win);
    ok(field, "The security code field is in the form");
    ok(
      !win.document.querySelector("#cc-csc-container").hidden,
      "The security code container is shown"
    );
    await field.updateComplete;
    win.document.querySelector("#cancel").click();
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_savedSecurityCodeIsShown() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });

  const record = Object.assign({}, TEST_CREDIT_CARD_1, {
    guid: "saved-security-code",
    "cc-csc": "123",
  });

  await testDialog(
    EDIT_CREDIT_CARD_DIALOG_URL,
    async win => {
      let field = getSecurityCodeField(win);
      await field.updateComplete;
      is(field.value, "123", "The saved security code is shown");
      is(
        field.inputEl.value,
        "123",
        "The value reaches the inner input so it is editable"
      );
      win.document.querySelector("#cancel").click();
    },
    { record }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_savingWithCaptureOffKeepsSecurityCode() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "123" }));
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, false]] });
  let [card] = await getCreditCards();
  ok(!("cc-csc" in card), "The stored security code is not read back");

  await testDialog(
    EDIT_CREDIT_CARD_DIALOG_URL,
    win => {
      ok(!getSecurityCodeField(win), "The form has no security code field");
      EventUtils.synthesizeKey("VK_TAB", {}, win);
      EventUtils.synthesizeKey("VK_TAB", {}, win);
      EventUtils.synthesizeKey("VK_RIGHT", {}, win);
      EventUtils.synthesizeKey("test", {}, win);
      win.document.querySelector("#save").click();
    },
    { record: card }
  );

  await SpecialPowers.popPrefEnv();
  [card] = await getCreditCards();
  is(card["cc-name"], TEST_CREDIT_CARD_1["cc-name"] + "test", "The edit saved");
  is(card["cc-csc"], "123", "and the security code the form lacked is kept");

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_clearingSecurityCodeRemovesIt() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });
  await setStorage(Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": "123" }));
  let [card] = await getCreditCards();
  is(card["cc-csc"], "123", "The security code is stored");

  await testDialog(
    EDIT_CREDIT_CARD_DIALOG_URL,
    async win => {
      let field = getSecurityCodeField(win);
      await field.updateComplete;
      field.inputEl.focus();
      field.inputEl.select();
      EventUtils.synthesizeKey("KEY_Backspace", {}, win);
      is(field.value, "", "The security code field is cleared");
      win.document.querySelector("#save").click();
    },
    { record: card }
  );

  [card] = await getCreditCards();
  ok(!("cc-csc" in card), "The cleared security code is removed");

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_securityCodeLabelFollowsNetwork() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });

  await testDialog(EDIT_CREDIT_CARD_DIALOG_URL, async win => {
    const field = getSecurityCodeField(win);
    const ccNumber = win.document.querySelector("#cc-number");
    const expectLabel = async (label, message) => {
      await TestUtils.waitForCondition(() => field.label == label, message);
      ok(true, message);
    };

    await expectLabel(
      "Security code (optional)",
      "A new card names the security code generally"
    );

    for (const [number, label] of [
      ["4111111111111111", "CVV (optional)"],
      ["5555555555554444", "CVC (optional)"],
      ["378282246310005", "CID (optional)"],
    ]) {
      await ccNumber.updateComplete;
      ccNumber.inputEl.focus();
      ccNumber.inputEl.select();
      EventUtils.sendString(number, win);
      await expectLabel(label, `The label follows the network of ${number}`);
    }

    win.document.querySelector("#cancel").click();
  });

  await SpecialPowers.popPrefEnv();
});

async function typeInto(win, field, text) {
  await field.updateComplete;
  field.inputEl.focus();
  field.inputEl.select();
  if (text) {
    EventUtils.sendString(text, win);
  } else {
    EventUtils.synthesizeKey("KEY_Backspace", {}, win);
  }
}

async function editSecurityCode({ storedCode, typedCode, cancel = false }) {
  let card;
  if (storedCode !== undefined) {
    await setStorage(
      Object.assign({}, TEST_CREDIT_CARD_1, { "cc-csc": storedCode })
    );
    [card] = await getCreditCards();
  }
  Services.fog.testResetFOG();

  await testDialog(
    EDIT_CREDIT_CARD_DIALOG_URL,
    async win => {
      if (!card) {
        EventUtils.synthesizeKey("VK_TAB", {}, win);
        EventUtils.synthesizeKey(TEST_CREDIT_CARD_1["cc-number"], {}, win);
        EventUtils.synthesizeKey("VK_TAB", {}, win);
        EventUtils.synthesizeKey(TEST_CREDIT_CARD_1["cc-name"], {}, win);
        EventUtils.synthesizeKey("VK_TAB", {}, win);
        EventUtils.synthesizeKey(
          "0" + TEST_CREDIT_CARD_1["cc-exp-month"].toString(),
          {},
          win
        );
        await win.document.activeElement.updateComplete;
        EventUtils.synthesizeKey("VK_TAB", {}, win);
        EventUtils.synthesizeKey(
          TEST_CREDIT_CARD_1["cc-exp-year"].toString(),
          {},
          win
        );
        await win.document.activeElement.updateComplete;
      }
      if (typedCode !== undefined) {
        await typeInto(win, getSecurityCodeField(win), typedCode);
      }
      win.document.querySelector(cancel ? "#cancel" : "#save").click();
    },
    card ? { record: card } : {}
  );

  let events = Glean.formautofillCreditcards.cvvManageAction.testGetValue();
  await removeAllRecords();
  return (events ?? []).map(e => e.extra.action);
}

add_task(async function test_securityCodeTelemetry() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, true]] });

  Assert.deepEqual(
    await editSecurityCode({ typedCode: "123" }),
    ["add"],
    "Saving a new card with a code records add"
  );
  Assert.deepEqual(
    await editSecurityCode({ storedCode: "", typedCode: "123" }),
    ["add"],
    "Saving a code for a card that had none records add"
  );
  Assert.deepEqual(
    await editSecurityCode({ storedCode: "123", typedCode: "456" }),
    ["edit"],
    "Changing the stored code records edit"
  );
  Assert.deepEqual(
    await editSecurityCode({ storedCode: "123", typedCode: "" }),
    ["clear"],
    "Removing the stored code records clear"
  );
  Assert.deepEqual(
    await editSecurityCode({ storedCode: "123", typedCode: "123" }),
    [],
    "An unchanged code records nothing"
  );
  Assert.deepEqual(
    await editSecurityCode({}),
    [],
    "A new card without a code records nothing"
  );
  Assert.deepEqual(
    await editSecurityCode({
      storedCode: "123",
      typedCode: "456",
      cancel: true,
    }),
    [],
    "Cancelling records nothing"
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_securityCodeTelemetryPrefDisabled() {
  await SpecialPowers.pushPrefEnv({ set: [[CVV_PREF, false]] });

  Assert.deepEqual(
    await editSecurityCode({ storedCode: "123" }),
    [],
    "Saving without the security code field records nothing"
  );

  await SpecialPowers.popPrefEnv();
});
