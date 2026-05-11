"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/extensions/formautofill/test/browser/creditCard/browser_telemetry_utils.js",
  this
);

add_task(async function test_submit_creditCard_autofill() {
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    todo(
      OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test OS key store login on official builds."
    );
    return;
  }

  const cleanupFunc = await setupTask(
    {
      set: [[ENABLED_AUTOFILL_CREDITCARDS_PREF, true]],
    },
    TEST_CREDIT_CARD_1
  );

  let creditCards = await getCreditCards();
  Assert.equal(creditCards.length, 1, "1 credit card in storage");

  await openTabAndUseCreditCard(0, TEST_CREDIT_CARD_1);

  SpecialPowers.clearUserPref(ENABLED_AUTOFILL_CREDITCARDS_PREF);

  let expectedFormEvents = [
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
  ];

  assertFormInteractionEventsInGlean(expectedFormEvents);
  assertDetectedCcNumberFieldsCountInGlean([
    { label: "cc_number_fields_1", count: 1 },
  ]);

  await cleanupFunc();
});

add_task(async function test_clear_creditCard_autofill() {
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    todo(
      OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test OS key store login on official builds."
    );
    return;
  }

  await removeAllRecords();
  const cleanupFunc = await setupTask(
    {
      set: [[ENABLED_AUTOFILL_CREDITCARDS_PREF, true]],
    },
    TEST_CREDIT_CARD_1
  );

  let creditCards = await getCreditCards();
  Assert.equal(creditCards.length, 1, "1 credit card in storage");

  const tab = await openTabAndUseCreditCard(0, TEST_CREDIT_CARD_1, {
    closeTab: false,
    submitForm: false,
  });

  let expectedFormEvents = [
    ccFormArgsv2("detected", buildccFormv2Extra({ cc_exp: "false" }, "true")),
    ccFormArgsv2("popup_shown", { field_name: "cc-name" }),
    ccFormArgsv2(
      "filled",
      buildccFormv2Extra({ cc_exp: "unavailable" }, "filled")
    ),
  ];

  assertFormInteractionEventsInGlean(expectedFormEvents);
  assertDetectedCcNumberFieldsCountInGlean([
    { label: "cc_number_fields_1", count: 1 },
  ]);

  await clearGleanTelemetry();

  let browser = tab.linkedBrowser;

  await openPopupOn(browser, "#cc-number");

  // flushing Glean data before tab removal (see Bug 1843178)
  await Services.fog.testFlushAllChildren();

  expectedFormEvents = [
    ccFormArgsv2("popup_shown", { field_name: "cc-number" }),
  ];
  assertFormInteractionEventsInGlean(expectedFormEvents);

  await clearGleanTelemetry();

  let popupHidden = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "hidden"
  );

  // kPress Clear Form.
  await BrowserTestUtils.synthesizeKey("KEY_ArrowDown", {}, browser);
  await BrowserTestUtils.synthesizeKey("KEY_Enter", {}, browser);

  await popupHidden;

  let popupShown = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "shown"
  );

  await popupShown;

  // flushing Glean data before tab removal (see Bug 1843178)
  await Services.fog.testFlushAllChildren();

  expectedFormEvents = [
    ccFormArgsv2("cleared", { field_name: "cc-number" }),
    // popup is shown again because when the field is cleared and is focused,
    // we automatically triggers the popup.
    ccFormArgsv2("popup_shown", { field_name: "cc-number" }),
  ];

  assertFormInteractionEventsInGlean(expectedFormEvents);

  await BrowserTestUtils.removeTab(tab);
  await cleanupFunc();
});
