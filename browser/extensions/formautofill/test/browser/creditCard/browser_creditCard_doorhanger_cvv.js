/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SUPPORTED_PREF = "extensions.formautofill.creditCards.cvv.supported";
const ENABLED_PREF = "extensions.formautofill.creditCards.cvv.enabled";

const CC_VALUES = {
  "#cc-name": "User 1",
  "#cc-number": "5577000055770004",
  "#cc-exp-month": "12",
  "#cc-exp-year": "2017",
  "#cc-csc": "123",
};

/**
 * Submit the credit card form and wait for the capture doorhanger. The
 * callback receives the doorhanger content once it is shown.
 */
async function withDoorhanger(newValues, callback) {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      const onPopupShown = waitForPopupShown();
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues,
      });
      await onPopupShown;

      await callback(
        getNotification().querySelector(".credit-card-capture-content")
      );
    }
  );
}

function descriptionL10nId(content) {
  const description = content.querySelector(".payments-doorhanger-message");
  return description.hidden ? null : description.getAttribute("data-l10n-id");
}

function cardDetailsL10nId(content) {
  return content.querySelector(".line-two").getAttribute("data-l10n-id");
}

async function formatLabel(id) {
  const [msg] = await document.l10n.formatMessages([{ id }]);
  return msg.attributes.find(attr => attr.name == "label").value;
}

/**
 * Submit a saved card with a new name, so the update doorhanger is shown.
 */
function withUpdateDoorhanger(securityCode, callback) {
  return withDoorhanger(
    {
      "#cc-name": "Mark Smith",
      "#cc-number": TEST_CREDIT_CARD_1["cc-number"],
      "#cc-exp-month": TEST_CREDIT_CARD_1["cc-exp-month"],
      "#cc-exp-year": TEST_CREDIT_CARD_1["cc-exp-year"],
      "#cc-csc": securityCode,
    },
    callback
  );
}

async function clickToggle(toggle) {
  const prefChanged = TestUtils.waitForPrefChange(ENABLED_PREF);
  toggle.click();
  await prefChanged;
  await toggle.updateComplete;
}

add_setup(async function () {
  // The security code is supported only while the Rust store serves.
  await switchCreditCardStore({ rust: true });
});

add_task(async function test_save_doorhanger_before_rollout() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "off"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    is(
      descriptionL10nId(content),
      "credit-card-save-doorhanger-description",
      "The doorhanger says the security code won't be saved"
    );
    ok(
      !content.querySelector("moz-checkbox, moz-toggle"),
      "No security codes control is shown"
    );
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_doorhanger_when_capture_is_off() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, false],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The description doesn't mention the security code"
    );

    const toggle = content.querySelector("moz-toggle");
    ok(toggle, "The security codes toggle is shown");
    await toggle.updateComplete;
    ok(!toggle.pressed, "The security codes toggle is off");
    is(
      toggle.getAttribute("data-l10n-id"),
      "credit-card-doorhanger-dont-save-security-codes-toggle",
      "The toggle says security codes aren't saved"
    );
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration",
      "The card details leave out the uncaptured security code"
    );

    await clickToggle(toggle);
    ok(
      Services.prefs.getBoolPref(ENABLED_PREF),
      "Turning the toggle on turns saving security codes back on"
    );
    is(
      toggle.getAttribute("data-l10n-id"),
      "credit-card-doorhanger-always-save-security-codes-toggle",
      "The toggle says security codes are saved"
    );

    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_doorhanger_while_json_store_serves() {
  await switchCreditCardStore({ rust: false });
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    is(
      descriptionL10nId(content),
      "credit-card-save-doorhanger-description",
      "The doorhanger says the security code won't be saved"
    );
    ok(
      !content.querySelector("moz-checkbox, moz-toggle"),
      "No security codes control is shown"
    );
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
  await switchCreditCardStore({ rust: true });
});

add_task(async function test_save_doorhanger_content() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    const notification = getNotification();
    is(
      notification.getAttribute("label"),
      await document.l10n.formatValue(
        "credit-card-save-payment-method-doorhanger-header"
      ),
      "The doorhanger asks to save the payment method"
    );
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The doorhanger shows the payment method description"
    );
    is(
      notification.getAttribute("secondarybuttonlabel"),
      await formatLabel("credit-card-capture-dismiss-button"),
      "The secondary button dismisses the doorhanger"
    );
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration-security-code",
      "The card details note the security code"
    );
    ok(
      content.querySelector(".payments-doorhanger-options-button"),
      "The edit button is shown"
    );

    const toggle = content.querySelector("moz-toggle");
    ok(toggle, "The security codes toggle is shown");
    ok(
      !content.querySelector("moz-checkbox"),
      "The security codes checkbox is not shown"
    );
    await toggle.updateComplete;
    ok(toggle.pressed, "The security codes toggle is on");
    is(
      toggle.getAttribute("data-l10n-id"),
      "credit-card-doorhanger-always-save-security-codes-toggle",
      "The toggle says security codes are saved"
    );

    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_doorhanger_toggle_follows_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;

    await clickToggle(toggle);
    ok(
      !Services.prefs.getBoolPref(ENABLED_PREF),
      "Turning the toggle off disables saving security codes"
    );
    is(
      toggle.getAttribute("data-l10n-id"),
      "credit-card-doorhanger-dont-save-security-codes-toggle",
      "The toggle says security codes aren't saved"
    );
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The description doesn't change when security codes aren't saved"
    );
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration",
      "The card details no longer note the security code"
    );

    await clickToggle(toggle);
    ok(
      Services.prefs.getBoolPref(ENABLED_PREF),
      "Turning the toggle on enables saving security codes"
    );
    is(
      toggle.getAttribute("data-l10n-id"),
      "credit-card-doorhanger-always-save-security-codes-toggle",
      "The toggle says security codes are saved"
    );
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The description stays the same"
    );
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration-security-code",
      "The card details note the security code again"
    );

    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_doorhanger_without_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  const valuesWithoutCode = { ...CC_VALUES };
  delete valuesWithoutCode["#cc-csc"];
  await withDoorhanger(valuesWithoutCode, async content => {
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The description doesn't mention the missing security code"
    );
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration",
      "The card details leave out the missing security code"
    );
    ok(
      content.querySelector("moz-toggle"),
      "The security codes toggle is still offered"
    );
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_with_toggle_on_stores_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;
    ok(toggle.pressed, "The security codes toggle is on");
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The doorhanger shows the payment method description"
    );

    const onChanged = waitForStorageChangedEvents("add");
    await clickDoorhangerButton(MAIN_BUTTON);
    await onChanged;
  });

  const creditCards = await getCreditCards();
  is(creditCards.length, 1, "One credit card is saved");
  is(creditCards[0]["cc-name"], "User 1", "The card is saved");
  is(
    creditCards[0]["cc-csc"],
    "123",
    "The security code is saved along with the card"
  );

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_with_toggle_off_stores_no_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;
    await clickToggle(toggle);
    ok(!toggle.pressed, "The security codes toggle is off");
    is(
      descriptionL10nId(content),
      "credit-card-save-payment-method-doorhanger-description",
      "The description doesn't change when security codes aren't saved"
    );
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration",
      "The card details no longer note the security code"
    );

    const onChanged = waitForStorageChangedEvents("add");
    await clickDoorhangerButton(MAIN_BUTTON);
    await onChanged;
  });

  ok(
    !Services.prefs.getBoolPref(ENABLED_PREF),
    "Saving security codes stays off"
  );

  // Storage hides security codes from reads while saving them is off, so turn
  // it back on to check that none was stored.
  await SpecialPowers.pushPrefEnv({ set: [[ENABLED_PREF, true]] });
  const creditCards = await getCreditCards();
  is(creditCards.length, 1, "One credit card is saved");
  is(creditCards[0]["cc-name"], "User 1", "The card is saved");
  ok(
    !creditCards[0]["cc-csc"],
    "The security code is not saved once the toggle is off"
  );

  await SpecialPowers.popPrefEnv();
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_dismiss_saves_nothing_and_keeps_toggle() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;
    await clickToggle(toggle);
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  ok(
    !Services.prefs.getBoolPref(ENABLED_PREF),
    "Dismissing keeps saving security codes turned off"
  );
  is((await getCreditCards()).length, 0, "Dismissing saves no card");

  await withDoorhanger(CC_VALUES, async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;
    ok(!toggle.pressed, "The toggle starts off, as it was left");
    await clickToggle(toggle);
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  ok(
    Services.prefs.getBoolPref(ENABLED_PREF),
    "Dismissing keeps saving security codes turned on"
  );
  is((await getCreditCards()).length, 0, "Dismissing saves no card");

  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_update_doorhanger_empty_field_keeps_security_code() {
    await SpecialPowers.pushPrefEnv({
      set: [
        [SUPPORTED_PREF, "on"],
        [ENABLED_PREF, true],
      ],
    });
    await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });

    await withDoorhanger(
      {
        "#cc-name": "Mark Smith",
        "#cc-number": TEST_CREDIT_CARD_1["cc-number"],
        "#cc-exp-month": TEST_CREDIT_CARD_1["cc-exp-month"],
        "#cc-exp-year": TEST_CREDIT_CARD_1["cc-exp-year"],
        "#cc-csc": "",
      },
      async () => {
        const onChanged = waitForStorageChangedEvents("update");
        await clickDoorhangerButton(MAIN_BUTTON);
        await onChanged;
      }
    );

    const creditCards = await getCreditCards();
    is(creditCards.length, 1, "The existing card is updated");
    is(creditCards[0]["cc-name"], "Mark Smith", "The card is updated");
    is(
      creditCards[0]["cc-csc"],
      "123",
      "A form submitted with an empty security code keeps the stored one"
    );

    await removeAllRecords();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_card_details_name_the_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  await withDoorhanger(CC_VALUES, async content => {
    const { id, args } = document.l10n.getAttributes(
      content.querySelector(".line-two")
    );
    is(
      id,
      "credit-card-doorhanger-card-details-name-expiration-security-code",
      "The card details note the security code"
    );
    is(
      args.securityCode,
      "CVC",
      "The security code is named the way Mastercard does"
    );
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_card_details_cover_every_combination() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  const { CreditCardSaveDoorhanger } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillPrompter.sys.mjs"
  );
  MozXULElement.insertFTLIfNeeded("toolkit/formautofill/formAutofill.ftl");

  for (const name of ["", "User 1"]) {
    for (const expiration of [false, true]) {
      for (const securityCode of ["", "123"]) {
        if (!name && !expiration && !securityCode) {
          continue;
        }
        const record = { "cc-number": "5577000055770004", "cc-name": name };
        if (expiration) {
          record["cc-exp-month"] = 12;
          record["cc-exp-year"] = 2030;
        }
        if (securityCode) {
          record["cc-csc"] = securityCode;
        }

        const doorhanger = new CreditCardSaveDoorhanger(
          gBrowser.selectedBrowser,
          {},
          record,
          "flow-id"
        );
        const line = document.createElement("div");
        doorhanger.appendCardDetails(line, name);

        const description = JSON.stringify({ name, expiration, securityCode });
        const { id, args } = document.l10n.getAttributes(line);
        ok(id, `The card details have a message for ${description}`);
        const text = await document.l10n.formatValue(id, args);
        is(
          text.includes("User 1"),
          !!name,
          `The name is shown only when there is one: ${text}`
        );
        is(
          text.includes("12/30"),
          expiration,
          `The expiration is shown only when there is one: ${text}`
        );
        is(
          text.includes("CVC"),
          !!securityCode,
          `The security code is noted only when there is one: ${text}`
        );
        ok(!text.includes("•••"), `The details have no hidden digits: ${text}`);
      }
    }
  }

  await SpecialPowers.popPrefEnv();
});

async function checkPlainUpdateContent(content, { withSecurityCode }) {
  const notification = getNotification();
  is(
    notification.getAttribute("label"),
    await document.l10n.formatValue(
      "credit-card-update-saved-payment-method-doorhanger-header"
    ),
    "The doorhanger asks to update the payment method"
  );
  is(descriptionL10nId(content), null, "No description is shown");
  ok(
    !content.querySelector("moz-toggle, moz-checkbox"),
    "No security codes control is shown"
  );
  is(
    notification.getAttribute("buttonlabel"),
    await formatLabel("credit-card-capture-update-payment-method-button"),
    "The main button updates the card"
  );
  is(
    notification.getAttribute("secondarybuttonlabel"),
    await formatLabel("credit-card-capture-save-as-new-payment-method-button"),
    "The secondary button saves a new payment method"
  );
  ok(
    content.querySelector(".payments-doorhanger-options-button"),
    "The edit button is shown"
  );
  is(
    cardDetailsL10nId(content),
    withSecurityCode
      ? "credit-card-doorhanger-card-details-name-expiration-security-code"
      : "credit-card-doorhanger-card-details-name-expiration",
    withSecurityCode
      ? "The card details note the stored security code"
      : "The card details have no security code"
  );
}

add_task(async function test_update_keeps_the_stored_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  // The form's security code field is submitted empty, and the same code.
  for (const securityCode of ["", "123"]) {
    await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });

    await withUpdateDoorhanger(securityCode, async content => {
      await checkPlainUpdateContent(content, { withSecurityCode: true });

      const onChanged = waitForStorageChangedEvents("update");
      await clickDoorhangerButton(MAIN_BUTTON);
      await onChanged;
    });

    const creditCards = await getCreditCards();
    is(creditCards.length, 1, "The existing card is updated");
    is(creditCards[0]["cc-name"], "Mark Smith", "The card is updated");
    is(
      creditCards[0]["cc-csc"],
      "123",
      `The stored security code is kept when "${securityCode}" is submitted`
    );

    await removeAllRecords();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_update_card_without_a_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  await setStorage(TEST_CREDIT_CARD_1);

  await withUpdateDoorhanger("", async content => {
    await checkPlainUpdateContent(content, { withSecurityCode: false });

    const onChanged = waitForStorageChangedEvents("update");
    await clickDoorhangerButton(MAIN_BUTTON);
    await onChanged;
  });

  const creditCards = await getCreditCards();
  is(creditCards.length, 1, "The existing card is updated");
  is(creditCards[0]["cc-name"], "Mark Smith", "The card is updated");
  ok(!creditCards[0]["cc-csc"], "The card still has no security code");

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_update_doorhanger_before_rollout() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "off"],
      [ENABLED_PREF, true],
    ],
  });
  await setStorage(TEST_CREDIT_CARD_1);

  await withUpdateDoorhanger("", async content => {
    is(
      getNotification().getAttribute("label"),
      await document.l10n.formatValue("credit-card-update-doorhanger-header"),
      "The doorhanger keeps the previous header"
    );
    is(
      descriptionL10nId(content),
      "credit-card-update-doorhanger-description",
      "The doorhanger keeps the previous description"
    );

    const onChanged = waitForStorageChangedEvents("update");
    await clickDoorhangerButton(MAIN_BUTTON);
    await onChanged;
  });

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

/**
 * Submit a saved card unchanged apart from its security code, so the security
 * code update doorhanger is shown.
 */
function withSecurityCodeUpdateDoorhanger(securityCode, callback) {
  return withDoorhanger(
    {
      "#cc-name": TEST_CREDIT_CARD_1["cc-name"],
      "#cc-number": TEST_CREDIT_CARD_1["cc-number"],
      "#cc-exp-month": TEST_CREDIT_CARD_1["cc-exp-month"],
      "#cc-exp-year": TEST_CREDIT_CARD_1["cc-exp-year"],
      "#cc-csc": securityCode,
    },
    callback
  );
}

async function checkSecurityCodeUpdateContent(content) {
  const notification = getNotification();
  is(
    notification.getAttribute("label"),
    await document.l10n.formatValue(
      "credit-card-save-security-code-doorhanger-header"
    ),
    "The doorhanger asks to save the security code"
  );
  is(
    descriptionL10nId(content),
    "credit-card-save-payment-method-doorhanger-description",
    "The doorhanger shows the payment method description"
  );
  is(
    cardDetailsL10nId(content),
    "credit-card-doorhanger-card-details-name-expiration-security-code",
    "The card details note the security code"
  );
  ok(
    content.querySelector(".payments-doorhanger-options-button"),
    "The edit button is shown"
  );

  const toggle = content.querySelector("moz-toggle");
  ok(toggle, "The security codes toggle is shown");
  await toggle.updateComplete;
  ok(toggle.pressed, "The security codes toggle is on");
  is(
    toggle.getAttribute("data-l10n-id"),
    "credit-card-doorhanger-always-save-security-codes-toggle",
    "The toggle says security codes are saved"
  );

  is(
    notification.getAttribute("buttonlabel"),
    await formatLabel("credit-card-capture-save-button"),
    "The main button saves the security code"
  );
  is(
    notification.getAttribute("secondarybuttonlabel"),
    await formatLabel("credit-card-capture-dismiss-button"),
    "The secondary button dismisses the doorhanger"
  );
  ok(
    !notification.hasAttribute("dropmarkerhidden"),
    "The dismiss button has a dropdown"
  );
}

add_task(async function test_save_new_or_changed_security_code() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });

  for (const storedCode of [undefined, "123"]) {
    const stored = { ...TEST_CREDIT_CARD_1 };
    if (storedCode) {
      stored["cc-csc"] = storedCode;
    }
    await setStorage(stored);

    await withSecurityCodeUpdateDoorhanger("456", async content => {
      await checkSecurityCodeUpdateContent(content);

      const onChanged = waitForStorageChangedEvents("update");
      await clickDoorhangerButton(MAIN_BUTTON);
      await onChanged;
    });

    const creditCards = await getCreditCards();
    is(creditCards.length, 1, "The existing card is updated");
    is(
      creditCards[0]["cc-csc"],
      "456",
      storedCode
        ? "The changed security code replaces the stored one"
        : "The new security code is saved with the card"
    );

    await removeAllRecords();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_security_code_toggle_switches_to_plain_update() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });

  await withSecurityCodeUpdateDoorhanger("456", async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;

    await clickToggle(toggle);
    ok(
      !Services.prefs.getBoolPref(ENABLED_PREF),
      "Turning the toggle off disables saving security codes"
    );
    is(
      toggle.getAttribute("data-l10n-id"),
      "credit-card-doorhanger-dont-save-security-codes-toggle",
      "The toggle says security codes aren't saved"
    );

    const notification = getNotification();
    is(
      notification.getAttribute("label"),
      await document.l10n.formatValue(
        "credit-card-update-saved-payment-method-doorhanger-header"
      ),
      "The doorhanger asks to update the payment method"
    );
    is(descriptionL10nId(content), null, "No description is shown");
    is(
      cardDetailsL10nId(content),
      "credit-card-doorhanger-card-details-name-expiration",
      "The card details no longer note the security code"
    );
    is(
      notification.getAttribute("buttonlabel"),
      await formatLabel("credit-card-capture-update-payment-method-button"),
      "The main button updates the card"
    );
    is(
      notification.getAttribute("secondarybuttonlabel"),
      await formatLabel(
        "credit-card-capture-save-as-new-payment-method-button"
      ),
      "The secondary button saves a new payment method"
    );
    ok(
      notification.hasAttribute("dropmarkerhidden"),
      "The secondary button has no dropdown"
    );

    await clickToggle(toggle);
    await checkSecurityCodeUpdateContent(content);

    await clickToggle(toggle);
    const onChanged = waitForStorageChangedEvents("update");
    await clickDoorhangerButton(MAIN_BUTTON);
    await onChanged;
  });

  // Storage hides security codes from reads while saving them is off.
  await SpecialPowers.pushPrefEnv({ set: [[ENABLED_PREF, true]] });
  const creditCards = await getCreditCards();
  is(creditCards.length, 1, "The existing card is updated");
  is(
    creditCards[0]["cc-csc"],
    "123",
    "Updating with the toggle off keeps the stored security code"
  );

  await SpecialPowers.popPrefEnv();
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_security_code_toggle_off_saves_new_card() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });

  await withSecurityCodeUpdateDoorhanger("456", async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;
    await clickToggle(toggle);

    const onChanged = waitForStorageChangedEvents("add");
    await clickDoorhangerButton(SECONDARY_BUTTON);
    await onChanged;
  });

  await SpecialPowers.pushPrefEnv({ set: [[ENABLED_PREF, true]] });
  const creditCards = await getCreditCards();
  is(creditCards.length, 2, "A new payment method is saved");
  ok(
    creditCards.every(card => card["cc-csc"] != "456"),
    "The new security code isn't saved"
  );

  await SpecialPowers.popPrefEnv();
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_security_code_update_dismissed() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });

  await withSecurityCodeUpdateDoorhanger("456", async () => {
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  const creditCards = await getCreditCards();
  is(creditCards.length, 1, "No card is added");
  is(creditCards[0]["cc-csc"], "123", "The stored security code is unchanged");

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_security_code_doorhanger_reopens_in_current_state() {
    await SpecialPowers.pushPrefEnv({
      set: [
        [SUPPORTED_PREF, "on"],
        [ENABLED_PREF, true],
      ],
    });
    await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });

    // The panel is rebuilt from the notification every time it opens, such as
    // when the user comes back to the tab or clicks the anchor, so the toggle has
    // to keep the notification itself up to date, not just the open panel.
    async function checkNotification(expected, message) {
      const { notification } = getNotification();
      is(
        notification.message,
        await document.l10n.formatValue(expected.header),
        `${message}: header`
      );
      is(
        notification.mainAction.label,
        await formatLabel(expected.main),
        `${message}: main button`
      );
      is(
        notification.secondaryActions[0].label,
        await formatLabel(expected.secondary),
        `${message}: secondary button`
      );
      is(
        notification.secondaryActions.length,
        expected.dropdown ? 2 : 1,
        `${message}: dropdown`
      );
    }

    await withSecurityCodeUpdateDoorhanger("456", async content => {
      const toggle = content.querySelector("moz-toggle");
      await toggle.updateComplete;

      await clickToggle(toggle);
      await checkNotification(
        {
          header: "credit-card-update-saved-payment-method-doorhanger-header",
          main: "credit-card-capture-update-payment-method-button",
          secondary: "credit-card-capture-save-as-new-payment-method-button",
          dropdown: false,
        },
        "With the toggle off"
      );

      await clickToggle(toggle);
      await checkNotification(
        {
          header: "credit-card-save-security-code-doorhanger-header",
          main: "credit-card-capture-save-button",
          secondary: "credit-card-capture-dismiss-button",
          dropdown: true,
        },
        "With the toggle back on"
      );

      await clickToggle(toggle);
      const onChanged = waitForStorageChangedEvents("update");
      await clickDoorhangerButton(MAIN_BUTTON);
      await onChanged;
    });

    await SpecialPowers.pushPrefEnv({ set: [[ENABLED_PREF, true]] });
    const creditCards = await getCreditCards();
    is(
      creditCards[0]["cc-csc"],
      "123",
      "Updating with the toggle off keeps the stored security code"
    );

    await SpecialPowers.popPrefEnv();
    await removeAllRecords();
    await SpecialPowers.popPrefEnv();
  }
);

async function waitForCvvSavingToggledEvents(count) {
  await TestUtils.waitForCondition(
    () =>
      (Glean.formautofillCreditcards.cvvSavingToggled.testGetValue() ?? [])
        .length == count,
    `Waiting for ${count} cvv_saving_toggled events`
  );
  return Glean.formautofillCreditcards.cvvSavingToggled
    .testGetValue()
    .map(event => event.extra);
}

add_task(async function test_save_doorhanger_toggle_records_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  Services.fog.testResetFOG();

  await withDoorhanger(CC_VALUES, async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;

    await clickToggle(toggle);
    await clickToggle(toggle);
    Assert.deepEqual(
      await waitForCvvSavingToggledEvents(2),
      [
        { enabled: "false", source: "save_prompt", had_saved_cvv: "false" },
        { enabled: "true", source: "save_prompt", had_saved_cvv: "false" },
      ],
      "Turning saving security codes off and on is recorded"
    );

    await clickDoorhangerButton(SECONDARY_BUTTON);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_update_doorhanger_toggle_records_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SUPPORTED_PREF, "on"],
      [ENABLED_PREF, true],
    ],
  });
  await setStorage({ ...TEST_CREDIT_CARD_1, "cc-csc": "123" });
  Services.fog.testResetFOG();

  await withSecurityCodeUpdateDoorhanger("456", async content => {
    const toggle = content.querySelector("moz-toggle");
    await toggle.updateComplete;

    await clickToggle(toggle);
    Assert.deepEqual(
      await waitForCvvSavingToggledEvents(1),
      [{ enabled: "false", source: "update_prompt", had_saved_cvv: "true" }],
      "Turning saving security codes off with a saved code is recorded"
    );
  });

  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});
