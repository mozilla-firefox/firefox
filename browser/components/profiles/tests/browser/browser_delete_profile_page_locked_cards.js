/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { formAutofillStorage } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillStorage.sys.mjs"
);
const { LoginTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/LoginTestUtils.sys.mjs"
);

const RUST_CREDIT_CARDS_PREF =
  "extensions.formautofill.creditCards.storage.rust.enabled";

add_task(async function test_cards_are_counted_without_a_prompt() {
  await initGroupDatabase();
  await formAutofillStorage.initialize();
  const rustWas = Services.prefs.getBoolPref(RUST_CREDIT_CARDS_PREF, false);
  registerCleanupFunction(async () => {
    const token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
      Ci.nsIPKCS11Token
    );
    if (token.hasPassword) {
      await LoginTestUtils.primaryPassword.disable();
    }
    await formAutofillStorage.creditCards.removeAll();
    Services.prefs.setBoolPref(RUST_CREDIT_CARDS_PREF, rustWas);
    await formAutofillStorage._creditCardSwitch;
  });
  Services.prefs.setBoolPref(RUST_CREDIT_CARDS_PREF, true);
  await formAutofillStorage._creditCardSwitch;
  for (const number of ["4111111111111111", "5105105105105100"]) {
    await formAutofillStorage.creditCards.add({
      "cc-name": "John Doe",
      "cc-number": number,
      "cc-exp-month": 4,
      "cc-exp-year": 2030,
    });
  }
  await LoginTestUtils.primaryPassword.enable();

  let prompts = 0;
  const cancelPrompt = subject => {
    prompts++;
    subject.Dialog.ui.button1.click();
  };
  Services.obs.addObserver(cancelPrompt, "common-dialog-loaded");
  try {
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:deleteprofile" },
      async browser => {
        const autofillCount = await SpecialPowers.spawn(
          browser,
          [],
          async () => {
            const card = content.document.querySelector(
              "delete-profile-card"
            ).wrappedJSObject;
            await ContentTaskUtils.waitForCondition(
              () => card.initialized,
              "Waiting for delete-profile-card to be initialized"
            );
            return card.data.autofillCount;
          }
        );
        Assert.equal(autofillCount, 2, "the saved cards are counted");
      }
    );
  } finally {
    Services.obs.removeObserver(cancelPrompt, "common-dialog-loaded");
  }
  Assert.equal(prompts, 0, "without a primary password prompt");
});
