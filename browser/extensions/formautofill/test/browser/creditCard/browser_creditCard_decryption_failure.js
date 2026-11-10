"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["toolkit.osKeyStore.unofficialBuildOnlyLogin", ""]],
  });
});

add_task(async function test_fill_creditCard_with_failed_decryption() {
  if (RUST_CREDIT_CARD_STORE) {
    // A card is made unreadable here by re-keying the OS key store under it.
    // The Rust store's key is NSS's, so that leaves its cards readable and
    // there is no failure to observe. Its own unreadable-card coverage is in
    // test_rust_autofill_credit_card_adapter.js.
    info("The Rust store's key is not the one this test resets");
    return;
  }
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    todo(
      OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test OS key store login on official builds."
    );
    return;
  }

  Services.fog.testResetFOG();

  await setStorage(TEST_CREDIT_CARD_2);

  // We run setup on the OSKeyStore again. This will make decryption fail for
  // the card we just added because it was added in the old keystore.
  info("Resetting the OSKeyStore");
  await OSKeyStoreTestUtils.cleanup();
  OSKeyStoreTestUtils.setup();

  // We do not support reauth on Linux: Bug 1527745
  info("Setting up OS auth test promise (Win & Mac only)");
  let osKeyStoreLoginShown = Promise.resolve();
  if (OSKeyStore.canReauth()) {
    // The OS keystore unlock should succeed.
    osKeyStoreLoginShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
  }

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      info("Waiting for CC popup");
      await openPopupOn(browser, "#cc-name");

      const ccItem = getDisplayedPopupItems(browser)[0];
      let popupClosePromise = BrowserTestUtils.waitForPopupEvent(
        browser.autoCompletePopup,
        "hidden"
      );
      const autofillComplete = TestUtils.topicObserved(
        "formautofill-autofill-complete"
      );

      info("Synthesizing click on credit card");
      EventUtils.synthesizeMouseAtCenter(ccItem, {});
      info("Awaiting three events");
      await Promise.all([
        osKeyStoreLoginShown.then(() =>
          info("osKeyStoreLoginShown successful")
        ),
        popupClosePromise.then(() => info("popupClosePromise successful")),
        autofillComplete.then(() => info("autofillComplete successful")),
      ]);

      info("Checking that the CC was not auto-filled");
      await SpecialPowers.spawn(browser, [], async function () {
        is(content.document.querySelector("#cc-name").value, "", "Check name");
        is(
          content.document.querySelector("#cc-number").value,
          "",
          "Check number"
        );
      });
    }
  );

  // Telemetry should have registered a decryption error.
  await Services.fog.testFlushAllChildren();
  let testEvents = Glean.creditcard.osKeystoreDecrypt.testGetValue();
  is(testEvents.length, 1, "Event was recorded");
  is(testEvents[0].extra.trigger, "formautofill_cc", "Trigger was correct");
  is(
    testEvents[0].extra.isDecryptSuccess,
    "false",
    "Decryption was recorded as failed"
  );
  is(
    testEvents[0].extra.errorResult,
    Cr.NS_ERROR_FAILURE.toString(),
    "Result was abort"
  );
});

add_task(async function test_save_prompt_with_an_undecryptable_card() {
  if (RUST_CREDIT_CARD_STORE) {
    // As in test_fill_creditCard_with_failed_decryption. The Rust store's
    // coverage is test_a_card_another_key_wrote_is_no_duplicate.
    info("The Rust store's key is not the one this test resets");
    return;
  }
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    todo(
      OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test OS key store login on official builds."
    );
    return;
  }

  await removeAllRecords();
  await setStorage(TEST_CREDIT_CARD_2);

  info("Resetting the OSKeyStore");
  await OSKeyStoreTestUtils.cleanup();
  OSKeyStoreTestUtils.setup();

  let onChanged = waitForStorageChangedEvents("add");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      let onPopupShown = waitForPopupShown();
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-name": TEST_CREDIT_CARD_2["cc-name"],
          "#cc-number": TEST_CREDIT_CARD_2["cc-number"],
        },
      });

      await onPopupShown;
      await clickDoorhangerButton(MAIN_BUTTON);
    }
  );
  await onChanged;

  let creditCards = await getCreditCards();
  is(creditCards.length, 2, "The submitted card is saved as a new one");
  await removeAllRecords();
});
