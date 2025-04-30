"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/extensions/formautofill/test/browser/creditCard/browser_telemetry_utils.js",
  this
);

add_setup(async function () {
  await clearGleanTelemetry();
});

add_task(async function test_submit_creditCard_update() {
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    todo(
      OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test OS key store login on official builds."
    );
    return;
  }

  async function test_per_command(command, idx, expectChanged = undefined) {
    await SpecialPowers.pushPrefEnv({
      set: [[ENABLED_AUTOFILL_CREDITCARDS_PREF, true]],
    });

    await setStorage(TEST_CREDIT_CARD_1);
    let creditCards = await getCreditCards();
    Assert.equal(creditCards.length, 1, "1 credit card in storage");

    let osKeyStoreLoginShown = null;
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: CREDITCARD_FORM_URL },
      async function (browser) {
        if (OSKeyStore.canReauth()) {
          osKeyStoreLoginShown =
            OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
        }
        let onPopupShown = waitForPopupShown();
        let onChanged;
        if (expectChanged !== undefined) {
          onChanged = TestUtils.topicObserved("formautofill-storage-changed");
        }

        await openPopupOn(browser, "form #cc-name");
        await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
        await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
        if (osKeyStoreLoginShown) {
          await osKeyStoreLoginShown;
        }

        await waitForAutofill(browser, "#cc-name", "John Doe");

        /* eslint-disable mozilla/no-arbitrary-setTimeout */
        await new Promise(resolve => {
          setTimeout(resolve, FormAutofill.fillOnDynamicFormChangeTimeout);
        });

        await focusUpdateSubmitForm(browser, {
          focusSelector: "#cc-name",
          newValues: {
            "#cc-exp-year": "2019",
          },
        });
        await onPopupShown;
        await clickDoorhangerButton(command, idx);
        if (expectChanged !== undefined) {
          await onChanged;
          TelemetryTestUtils.assertScalar(
            TelemetryTestUtils.getProcessScalars("parent"),
            "formautofill.creditCards.autofill_profiles_count",
            expectChanged,
            `There should be ${expectChanged} profile(s) stored and recorded in Legacy Telemetry.`
          );
          Assert.equal(
            expectChanged,
            Glean.formautofillCreditcards.autofillProfilesCount.testGetValue(),
            `There should be ${expectChanged} profile(s) stored.`
          );
        }
        // flushing Glean data within withNewTab callback before tab removal (see Bug 1843178)
        await Services.fog.testFlushAllChildren();
      }
    );

    SpecialPowers.clearUserPref(ENABLED_AUTOFILL_CREDITCARDS_PREF);

    await removeAllRecords();
  }

  const expectedFormInteractionEvents = [
    ccFormArgsv2("detected", buildccFormv2Extra({ cc_exp: "false" }, "true")),
    ccFormArgsv2("popup_shown", { field_name: "cc-name" }),
    ccFormArgsv2(
      "filled",
      buildccFormv2Extra({ cc_exp: "unavailable" }, "filled")
    ),
    ccFormArgsv2("filled_modified", { field_name: "cc-exp-year" }),
    ccFormArgsv2(
      "submitted",
      buildccFormv2Extra(
        { cc_exp: "unavailable", cc_exp_year: "user_filled" },
        "autofilled"
      )
    ),
  ];

  await clearGleanTelemetry();

  await test_per_command(MAIN_BUTTON, undefined, 1);

  await assertTelemetry(undefined, [
    ...expectedFormInteractionEvents,
    ["creditcard", "show", "update_doorhanger"],
    ["creditcard", "update", "update_doorhanger"],
  ]);

  assertFormInteractionEventsInGlean(expectedFormInteractionEvents);
  assertDetectedCcNumberFieldsCountInGlean([
    { label: "cc_number_fields_1", count: 1 },
  ]);

  await clearGleanTelemetry();

  await test_per_command(SECONDARY_BUTTON, undefined, 2);

  await assertTelemetry(undefined, [
    ...expectedFormInteractionEvents,
    ["creditcard", "show", "update_doorhanger"],
    ["creditcard", "save", "update_doorhanger"],
  ]);

  assertFormInteractionEventsInGlean(expectedFormInteractionEvents);
  assertDetectedCcNumberFieldsCountInGlean([
    { label: "cc_number_fields_1", count: 1 },
  ]);
});
