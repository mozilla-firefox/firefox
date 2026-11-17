"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["toolkit.osKeyStore.unofficialBuildOnlyLogin", ""]],
  });
});

add_task(async function test_fill_creditCard_but_cancel_login() {
  if (
    !OSKeyStore.canReauth() ||
    !OSKeyStoreTestUtils.canTestOSKeyStoreLogin()
  ) {
    todo(
      OSKeyStore.canReauth() && OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test login cancel."
    );
    return;
  }

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  await setStorage(TEST_CREDIT_CARD_2);

  let osKeyStoreLoginShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(false); // cancel
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      await openPopupOn(browser, "#cc-name");
      const ccItem = getDisplayedPopupItems(browser)[0];
      let popupClosePromise = BrowserTestUtils.waitForPopupEvent(
        browser.autoCompletePopup,
        "hidden"
      );
      EventUtils.synthesizeMouseAtCenter(ccItem, {});
      await Promise.all([osKeyStoreLoginShown, popupClosePromise]);

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

  await Services.fog.testFlushAllChildren();
  if (RUST_CREDIT_CARD_STORE) {
    // Re-authentication is a step of its own for this store, so a decline
    // aborts before anything is read and there is no decrypt to report. What
    // the user did is reported by promptShownOsReauth either way.
    const prompts = Glean.formautofill.promptShownOsReauth.testGetValue() ?? [];
    is(prompts.at(-1).extra.trigger, "autofill", "Recorded for the fill");
    is(
      prompts.at(-1).extra.result,
      "fail_user_canceled",
      "Recorded as a decline"
    );
    return;
  }
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
    Cr.NS_ERROR_ABORT.toString(),
    "Result was abort"
  );
});
