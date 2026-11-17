"use strict";

const CC_URL =
  "https://example.org/browser/browser/extensions/formautofill/test/browser/creditCard/autocomplete_creditcard_basic.html";

const TEST_CARD_WITH_CSC = {
  "cc-name": "John Doe",
  "cc-number": "4111111111111111",
  "cc-exp-month": 4,
  "cc-exp-year": new Date().getFullYear() + 2,
  "cc-csc": "123",
};

async function getFieldValues(browser, properties) {
  return SpecialPowers.spawn(browser, [properties], props => {
    const result = {};
    for (const [selector, property] of Object.entries(props)) {
      result[selector] = content.document.querySelector(selector)[property];
    }
    return result;
  });
}

add_setup(async function () {
  // head.js force-enables OS auth for this directory, which would make filling
  // the card number block on a reauth prompt. Reauth is not what this file
  // tests, so revert the stub and turn it off.
  sinon.restore();
  const oldOSAuth = FormAutofillUtils.getOSAuthEnabled();
  FormAutofillUtils.setOSAuthEnabled(false);
  registerCleanupFunction(() => {
    FormAutofillUtils.setOSAuthEnabled(oldOSAuth);
  });

  // The feature is gated on rollout as well as on the user's own choice. Roll
  // it out for the whole file so each task can toggle the enabled pref alone.
  // The security code is supported only while the Rust store serves.
  await switchCreditCardStore({ rust: true });
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.creditCards.cvv.supported", "on"],
      ["extensions.formautofill.creditCards.cvv.enabled", true],
    ],
  });

  await removeAllRecords();
  await setStorage(TEST_CARD_WITH_CSC);
});

add_task(async function test_csc_round_trips_through_storage() {
  const cards = await getCreditCards();
  is(cards.length, 1, "One credit card is saved");
  is(cards[0]["cc-csc"], "123", "cc-csc survives the storage round trip");

  // The saved field names are recomputed after the storage change setStorage
  // waits for, not before it.
  await TestUtils.waitForCondition(
    () =>
      Services.ppmm.sharedData
        .get("FormAutofill:savedFieldNames")
        ?.has("cc-csc"),
    "A stored cc-csc reaches savedFieldNames with no special casing"
  );
  ok(true, "A stored cc-csc reaches savedFieldNames with no special casing");
});

add_task(async function test_no_cvv_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.creditCards.cvv.enabled", false]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async function (browser) {
      await SimpleTest.promiseFocus(browser);

      await focusAndWaitForFieldsIdentified(browser, "#cc-csc");
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await ensureNoAutocompletePopup(browser);

      await triggerAutofillAndPreview(
        browser,
        "#cc-number",
        async () => {
          const { "#cc-csc": preview } = await getFieldValues(browser, {
            "#cc-csc": "previewValue",
          });
          is(preview, "", "The security code is not previewed");
        },
        async () => {
          const values = await getFieldValues(browser, {
            "#cc-csc": "value",
            "#cc-number": "value",
          });
          is(values["#cc-csc"], "", "The security code is not filled");
          is(
            values["#cc-number"],
            "4111111111111111",
            "The rest of the card is still filled"
          );
        },
        async () => {
          const { "#cc-number": cleared } = await getFieldValues(browser, {
            "#cc-number": "value",
          });
          is(cleared, "", "Clearing the form clears the card number");
        }
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_no_cvv_while_json_store_serves() {
  const [{ guid }] = await getCreditCards();
  await switchCreditCardStore({ rust: false });
  // The JSON store takes no code while it serves, so the one it holds here is
  // left from before CVV needed the Rust store; write it in directly.
  formAutofillStorage.creditCards._data.find(r => r.guid == guid)["cc-csc"] =
    "123";
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.creditCards.cvv.enabled", true]],
  });

  const cards = await getCreditCards();
  is(cards.length, 1, "The card is served by the JSON store");
  ok(!("cc-csc" in cards[0]), "The code the JSON store holds is not read");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async function (browser) {
      await SimpleTest.promiseFocus(browser);

      await focusAndWaitForFieldsIdentified(browser, "#cc-csc");
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await ensureNoAutocompletePopup(browser);

      await triggerAutofillAndPreview(
        browser,
        "#cc-number",
        async () => {
          const { "#cc-csc": preview } = await getFieldValues(browser, {
            "#cc-csc": "previewValue",
          });
          is(preview, "", "The security code is not previewed");
        },
        async () => {
          const values = await getFieldValues(browser, {
            "#cc-csc": "value",
            "#cc-number": "value",
          });
          is(values["#cc-csc"], "", "The security code is not filled");
          is(
            values["#cc-number"],
            "4111111111111111",
            "The rest of the card is still filled"
          );
        },
        async () => {
          const { "#cc-number": cleared } = await getFieldValues(browser, {
            "#cc-number": "value",
          });
          is(cleared, "", "Clearing the form clears the card number");
        }
      );
    }
  );

  await SpecialPowers.popPrefEnv();
  await switchCreditCardStore({ rust: true });
});

add_task(async function test_cvv_autofills_when_pref_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.creditCards.cvv.enabled", true]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async function (browser) {
      await triggerAutofillAndPreview(
        browser,
        "#cc-csc",
        async () => {
          const { "#cc-csc": preview } = await getFieldValues(browser, {
            "#cc-csc": "previewValue",
          });
          is(preview, "•••", "The security code preview is masked");
        },
        async () => {
          const values = await getFieldValues(browser, {
            "#cc-csc": "value",
            "#cc-number": "value",
          });
          is(values["#cc-csc"], "123", "The real security code is filled");
          is(
            values["#cc-number"],
            "4111111111111111",
            "The rest of the card is filled too"
          );
        },
        async () => {
          const { "#cc-csc": cleared } = await getFieldValues(browser, {
            "#cc-csc": "value",
          });
          is(cleared, "", "Clearing the form clears the security code");
        }
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_cvv_entry_is_named_after_the_field() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.creditCards.cvv.enabled", true]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async function (browser) {
      await openPopupOn(browser, "#cc-csc");

      const ariaLabel =
        getDisplayedPopupItems(browser)[0].getAttribute("aria-label");
      is(
        ariaLabel,
        "Visa CVV ••••1111",
        "Entry is named after the field and identified by the card number"
      );
      ok(!ariaLabel.includes("123"), "The security code is not announced");

      await closePopup(browser);
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_saved_field_names_follow_turning_cvv_on() {
  const { FormAutofillStatus } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillParent.sys.mjs"
  );
  const savedFieldNames = () =>
    Services.ppmm.sharedData.get("FormAutofill:savedFieldNames");

  // Saved field names computed while CVV is off, as any storage change then
  // would compute them.
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.creditCards.cvv.enabled", false]],
  });
  await FormAutofillStatus.updateSavedFieldNames();
  await SpecialPowers.popPrefEnv();

  await TestUtils.waitForCondition(
    () => savedFieldNames()?.has("cc-csc"),
    "Turning CVV on offers the stored security code"
  );
  ok(true, "Turning CVV on offers the stored security code");
});
