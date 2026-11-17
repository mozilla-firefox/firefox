"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/extensions/formautofill/test/browser/creditCard/browser_telemetry_utils.js",
  this
);

const TEST_CARD_WITH_CSC = {
  "cc-name": "John Doe",
  "cc-number": "4111111111111111",
  "cc-exp-month": 4,
  "cc-exp-year": new Date().getFullYear() + 2,
  "cc-csc": "123",
};

add_setup(async function () {
  // head.js force-enables OS auth for this directory; turn it off so filling
  // does not block on a reauth prompt.
  sinon.restore();
  const oldOSAuth = FormAutofillUtils.getOSAuthEnabled();
  FormAutofillUtils.setOSAuthEnabled(false);
  registerCleanupFunction(() => {
    FormAutofillUtils.setOSAuthEnabled(oldOSAuth);
  });

  // The security code is supported only while the Rust store serves.
  await switchCreditCardStore({ rust: true });
  await SpecialPowers.pushPrefEnv({
    set: [
      [ENABLED_AUTOFILL_CREDITCARDS_PREF, true],
      ["extensions.formautofill.creditCards.cvv.supported", "on"],
      ["extensions.formautofill.creditCards.cvv.enabled", true],
      ["extensions.formautofill.heuristics.fillOnDynamicFormChanges", false],
    ],
  });

  await removeAllRecords();
  await setStorage(TEST_CARD_WITH_CSC);
  registerCleanupFunction(removeAllRecords);
});

async function autofillCardWithCsc(browser) {
  await openPopupOn(browser, "form #cc-name");
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
  await waitForAutofill(browser, "#cc-csc", TEST_CARD_WITH_CSC["cc-csc"]);
}

add_task(async function test_csc_filled_and_submitted() {
  await clearGleanTelemetry();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      await autofillCardWithCsc(browser);
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-number",
        newValues: {},
      });

      // flushing Glean data within withNewTab callback before tab removal (see Bug 1843178)
      await Services.fog.testFlushAllChildren();
    }
  );

  assertFormInteractionEventsInGlean([
    ccFormArgsv2("detected", buildccFormv2Extra({ cc_exp: "false" }, "true")),
    ccFormArgsv2("popup_shown", { field_name: "cc-name" }),
    ccFormArgsv2(
      "filled",
      buildccFormv2Extra({ cc_exp: "unavailable" }, "filled")
    ),
    ccFormArgsv2(
      "submitted",
      buildccFormv2Extra({ cc_exp: "unavailable" }, "autofilled")
    ),
  ]);
});

add_task(async function test_csc_modified_after_fill() {
  await clearGleanTelemetry();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      await autofillCardWithCsc(browser);
      await focusUpdateSubmitForm(
        browser,
        {
          focusSelector: "#cc-csc",
          newValues: { "#cc-csc": "456" },
        },
        false
      );

      // Typing in the security code field offers the saved cards again, as
      // typing in the number field does.
      await TestUtils.waitForCondition(async () => {
        await Services.fog.testFlushAllChildren();
        return (
          Glean.creditcard.filledModifiedCcFormV2.testGetValue()?.length &&
          Glean.creditcard.popupShownCcFormV2.testGetValue()?.length == 2
        );
      }, "Wait for the filled_modified event and the second popup");
      await closePopup(browser);
    }
  );

  const popups = Glean.creditcard.popupShownCcFormV2.testGetValue();
  Assert.deepEqual(
    popups.map(event => event.extra.field_name),
    ["cc-name", "cc-csc"],
    "The popup is shown on the name field, then on the security code field"
  );
  const fillFlowId =
    Glean.creditcard.filledCcFormV2.testGetValue()[0].extra.value;
  Assert.deepEqual(
    popups.map(event => event.extra.value),
    [fillFlowId, fillFlowId],
    "Both popups belong to the session that filled the form"
  );
  assertFormInteractionEventsInGlean([
    ccFormArgsv2("detected", buildccFormv2Extra({ cc_exp: "false" }, "true")),
    ccFormArgsv2(
      "filled",
      buildccFormv2Extra({ cc_exp: "unavailable" }, "filled")
    ),
    ccFormArgsv2("filled_modified", { field_name: "cc-csc" }),
  ]);
});
