/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

add_task(async function test_save_doorhanger_shown_no_profile() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      let onPopupShown = waitForPopupShown();

      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-name": "User 1",
          "#cc-number": "5577000055770004",
          "#cc-exp-month": "12",
          "#cc-exp-year": "2017",
          "#cc-type": "mastercard",
        },
      });

      await onPopupShown;
    }
  );
});

add_task(async function test_save_doorhanger_shown_different_card_number() {
  await setStorage(TEST_CREDIT_CARD_1);
  let creditCards = await getCreditCards();
  is(creditCards.length, 1, "1 credit card in storage");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      let onPopupShown = waitForPopupShown();
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-number": TEST_CREDIT_CARD_2["cc-number"],
        },
      });

      await onPopupShown;
    }
  );
  await removeAllRecords();
});

add_task(async function test_update_doorhanger_shown_different_card_name() {
  await setStorage(TEST_CREDIT_CARD_1);
  let creditCards = await getCreditCards();
  is(creditCards.length, 1, "1 credit card in storage");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      let onPopupShown = waitForPopupShown();
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-name": "Mark Smith",
          "#cc-number": TEST_CREDIT_CARD_1["cc-number"],
          "#cc-exp-month": TEST_CREDIT_CARD_1["cc-exp-month"],
          "#cc-exp-year": TEST_CREDIT_CARD_1["cc-exp-year"],
        },
      });

      await onPopupShown;
    }
  );
  await removeAllRecords();
});

add_task(async function test_update_doorhanger_shown_different_card_expiry() {
  await setStorage(TEST_CREDIT_CARD_1);
  let creditCards = await getCreditCards();
  is(creditCards.length, 1, "1 credit card in storage");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      let onPopupShown = waitForPopupShown();
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-name": TEST_CREDIT_CARD_1["cc-name"],
          "#cc-number": TEST_CREDIT_CARD_1["cc-number"],
          "#cc-exp-month": "12",
          "#cc-exp-year": "2099",
        },
      });

      await onPopupShown;
    }
  );
  await removeAllRecords();
});

add_task(async function test_doorhanger_not_shown_when_autofill_untouched() {
  if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
    todo(
      OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
      "Cannot test OS key store login on official builds."
    );
    return;
  }

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  Services.telemetry.clearEvents();

  await setStorage(TEST_CREDIT_CARD_1);
  let creditCards = await getCreditCards();
  is(creditCards.length, 1, "1 credit card in storage");

  let osKeyStoreLoginShown = null;
  let onUsed = waitForStorageChangedEvents("notifyUsed");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      if (OSKeyStore.canReauth()) {
        osKeyStoreLoginShown = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
      }
      await openPopupOn(browser, "form #cc-name");
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
      if (osKeyStoreLoginShown) {
        await osKeyStoreLoginShown;
      }
      await waitForAutofill(browser, "#cc-name", "John Doe");

      await SpecialPowers.spawn(browser, [], async function () {
        let form = content.document.getElementById("form");
        form.querySelector("input[type=submit]").click();
      });

      await sleep(1000);
      is(PopupNotifications.panel.state, "closed", "Doorhanger is hidden");
    }
  );
  await onUsed;

  creditCards = await getCreditCards();
  is(creditCards.length, 1, "Still 1 credit card");
  is(creditCards[0].timesUsed, 1, "timesUsed field set to 1");
  await removeAllRecords();

  await Services.fog.testFlushAllChildren();
  let testEvents = Glean.creditcard.osKeystoreDecrypt.testGetValue();
  is(testEvents.length, 1, "Event was recorded");
  is(testEvents[0].extra.trigger, "autofill", "Trigger was correct");
  is(
    testEvents[0].extra.isDecryptSuccess,
    "true",
    "Decryption was recorded as success"
  );
  is(testEvents[0].extra.errorResult, "0", "Result was no error");
});

add_task(async function test_doorhanger_not_shown_when_fill_duplicate() {
  await setStorage(TEST_CREDIT_CARD_1);
  let creditCards = await getCreditCards();
  is(creditCards.length, 1, "1 credit card in storage");

  let onUsed = waitForStorageChangedEvents("notifyUsed");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-name": "John Doe",
          "#cc-number": "4111111111111111",
          "#cc-exp-month": "4",
          "#cc-exp-year": new Date().getFullYear(),
          "#cc-type": "visa",
        },
      });

      await sleep(1000);
      is(PopupNotifications.panel.state, "closed", "Doorhanger is hidden");
    }
  );
  await onUsed;

  creditCards = await getCreditCards();
  is(creditCards.length, 1, "Still 1 credit card in storage");
  is(
    creditCards[0]["cc-name"],
    TEST_CREDIT_CARD_1["cc-name"],
    "Verify the name field"
  );
  is(creditCards[0].timesUsed, 1, "timesUsed field set to 1");
  await removeAllRecords();
});

add_task(
  async function test_doorhanger_not_shown_when_autofill_then_fill_everything_duplicate() {
    if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
      todo(
        OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
        "Cannot test OS key store login on official builds."
      );
      return;
    }

    await setStorage(TEST_CREDIT_CARD_1, TEST_CREDIT_CARD_2);
    let creditCards = await getCreditCards();
    is(creditCards.length, 2, "2 credit card in storage");
    let osKeyStoreLoginShown = null;
    let onUsed = waitForStorageChangedEvents("notifyUsed");
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: CREDITCARD_FORM_URL },
      async function (browser) {
        if (OSKeyStore.canReauth()) {
          osKeyStoreLoginShown =
            OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
        }
        await openPopupOn(browser, "form #cc-number");
        await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
        await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
        await waitForAutofill(
          browser,
          "#cc-number",
          TEST_CREDIT_CARD_1["cc-number"]
        );

        /* eslint-disable mozilla/no-arbitrary-setTimeout */
        await new Promise(resolve => {
          setTimeout(resolve, FormAutofill.fillOnDynamicFormChangeTimeout);
        });

        await focusUpdateSubmitForm(browser, {
          focusSelector: "#cc-name",
          newValues: {
            // Change number to the second credit card number
            "#cc-number": TEST_CREDIT_CARD_2["cc-number"],
            "#cc-name": TEST_CREDIT_CARD_2["cc-name"],
            "#cc-exp-month": TEST_CREDIT_CARD_2["cc-exp-month"],
            "#cc-exp-year": TEST_CREDIT_CARD_2["cc-exp-year"],
          },
        });

        await sleep(1000);
        is(PopupNotifications.panel.state, "closed", "Doorhanger is hidden");
        if (osKeyStoreLoginShown) {
          await osKeyStoreLoginShown;
        }
      }
    );
    await onUsed;

    creditCards = await getCreditCards();
    is(creditCards.length, 2, "Still 2 credit card");
    await removeAllRecords();
  }
);

add_task(
  async function test_doorhanger_not_shown_when_autofill_then_fill_number_duplicate() {
    if (!OSKeyStoreTestUtils.canTestOSKeyStoreLogin()) {
      todo(
        OSKeyStoreTestUtils.canTestOSKeyStoreLogin(),
        "Cannot test OS key store login on official builds."
      );
      return;
    }

    await setStorage(TEST_CREDIT_CARD_1, {
      ...TEST_CREDIT_CARD_1,
      ...{ "cc-number": "5105105105105100" },
    });

    let creditCards = await getCreditCards();
    is(creditCards.length, 2, "2 credit card in storage");
    let osKeyStoreLoginShown = null;
    let onUsed = waitForStorageChangedEvents("notifyUsed");
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: CREDITCARD_FORM_URL },
      async function (browser) {
        if (OSKeyStore.canReauth()) {
          osKeyStoreLoginShown =
            OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
        }
        await openPopupOn(browser, "form #cc-number");
        await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
        await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
        await waitForAutofill(
          browser,
          "#cc-number",
          TEST_CREDIT_CARD_1["cc-number"]
        );

        /* eslint-disable mozilla/no-arbitrary-setTimeout */
        await new Promise(resolve => {
          setTimeout(resolve, FormAutofill.fillOnDynamicFormChangeTimeout);
        });

        await focusUpdateSubmitForm(browser, {
          focusSelector: "#cc-name",
          newValues: {
            // Change number to the second credit card number
            "#cc-number": "5105105105105100",
          },
        });

        await sleep(1000);
        is(PopupNotifications.panel.state, "closed", "Doorhanger is hidden");
        if (osKeyStoreLoginShown) {
          await osKeyStoreLoginShown;
        }
      }
    );
    await onUsed;

    creditCards = await getCreditCards();
    is(creditCards.length, 2, "Still 2 credit card");
    await removeAllRecords();
  }
);

add_task(async function test_update_doorhanger_shown_when_fill_mergeable() {
  await setStorage(TEST_CREDIT_CARD_3);
  let creditCards = await getCreditCards();
  is(creditCards.length, 1, "1 credit card in storage");

  let onChanged = waitForStorageChangedEvents("update", "notifyUsed");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async function (browser) {
      let onPopupShown = waitForPopupShown();
      await focusUpdateSubmitForm(browser, {
        focusSelector: "#cc-name",
        newValues: {
          "#cc-name": "User 3",
          "#cc-number": "5103059495477870",
          "#cc-exp-month": "1",
          "#cc-exp-year": "2000",
        },
      });

      await onPopupShown;
      await clickDoorhangerButton(MAIN_BUTTON);
    }
  );
  await onChanged;

  creditCards = await getCreditCards();
  is(creditCards.length, 1, "Still 1 credit card in storage");
  is(creditCards[0]["cc-name"], "User 3", "Verify the name field");
  await removeAllRecords();
});
