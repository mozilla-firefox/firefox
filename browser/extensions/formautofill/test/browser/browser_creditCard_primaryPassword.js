/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The primary password prompt of the Rust credit card store. The store's key
 * lives in NSS and it decrypts every card it reads, so once the user sets a
 * primary password and the token is locked, listing the cards asks for it, and
 * so does saving one.
 */

const ENABLED_PREF = "extensions.formautofill.creditCards.storage.rust.enabled";
const ACTIVE_PREF = "extensions.formautofill.creditCards.storage.rust.active";

const { LoginTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/LoginTestUtils.sys.mjs"
);

const PRIMARY_PASSWORD = LoginTestUtils.primaryPassword.primaryPassword;
const REPROMPT_TIMEOUT_PREF = "signon.masterPasswordReprompt.timeout_ms";

const token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
  Ci.nsIPKCS11Token
);

const TEST_CARD = {
  "cc-name": "John Doe",
  "cc-number": "4111111111111111",
  "cc-exp-month": 4,
  "cc-exp-year": 2030,
};

const OTHER_CARD = {
  "cc-name": "Jane Roe",
  "cc-number": "5105105105105100",
  "cc-exp-month": 12,
  "cc-exp-year": 2031,
};

/**
 * Answer every primary password dialog that opens until the returned function
 * is called.
 *
 * @param {Array<?string>} answers The password to enter into each dialog, in
 *   order, or null to cancel it.
 * @returns {Function} Stops answering and returns the number of dialogs shown.
 */
function answerPrimaryPasswordDialogs(answers) {
  let shown = 0;
  const observer = subject => {
    const dialog = subject.Dialog;
    const answer = answers[shown++];
    if (answer == null) {
      dialog.ui.button1.click();
      return;
    }
    SpecialPowers.wrap(dialog.ui.password1Textbox).setUserInput(answer);
    dialog.ui.button0.click();
  };
  Services.obs.addObserver(observer, "common-dialog-loaded");
  return () => {
    Services.obs.removeObserver(observer, "common-dialog-loaded");
    return shown;
  };
}

/**
 * Save `cards` while the token is unlocked, then set the primary password and
 * lock the token.
 *
 * @param {...object} cards
 */
async function lockWithCards(...cards) {
  await setStorage(...cards);
  // Read while unlocked, which forgets a prompt an earlier task cancelled.
  await getCreditCards();
  await LoginTestUtils.primaryPassword.enable();
  Assert.ok(!token.isLoggedIn, "the token is locked");
}

async function removePasswordAndCards() {
  if (!token.isLoggedIn) {
    const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
    try {
      await token.login(false);
    } finally {
      stop();
    }
  }
  await LoginTestUtils.primaryPassword.disable();
  await removeAllRecords();
}

/**
 * Select the first entry of the open autocomplete popup.
 *
 * @param {object} browser
 */
async function selectFirstEntry(browser) {
  const items = getDisplayedPopupItems(browser);
  Assert.ok(items.length, "the saved cards are listed");
  const popupHidden = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "hidden"
  );
  EventUtils.synthesizeMouseAtCenter(items[0], {});
  await popupHidden;
}

function readForm(browser) {
  return SpecialPowers.spawn(browser, [], () =>
    Object.fromEntries(
      ["cc-name", "cc-number"].map(id => [
        id,
        content.document.getElementById(id).value,
      ])
    )
  );
}

add_setup(async function () {
  // The OS re-authentication prompt is a step of its own before the number is
  // read, and not what is under test.
  const osAuthWas = FormAutofillUtils.getOSAuthEnabled();
  FormAutofillUtils.setOSAuthEnabled(false);

  const enabledWas = Services.prefs.getBoolPref(ENABLED_PREF, false);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.creditCards.enabled", true],
      // A cancelled prompt holds off the dropdown's next one for this long.
      // Only test_listing_declined is about that.
      [REPROMPT_TIMEOUT_PREF, 0],
    ],
  });
  await removeAllRecords();
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await formAutofillStorage._creditCardSwitch;
  Assert.ok(
    Services.prefs.getBoolPref(ACTIVE_PREF, false),
    "the Rust store serves credit cards"
  );

  registerCleanupFunction(async () => {
    FormAutofillUtils.setOSAuthEnabled(osAuthWas);
    await removeAllRecords();
    Services.prefs.setBoolPref(ENABLED_PREF, enabledWas);
    await formAutofillStorage._creditCardSwitch;
  });
});

add_task(async function test_listing_prompts_for_the_primary_password() {
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const loggedIn = TestUtils.topicObserved("passwordmgr-crypto-login");
    const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
    await openPopupOn(browser, "#cc-name");
    await loggedIn;
    Assert.ok(token.isLoggedIn, "listing the cards unlocks the token");

    await selectFirstEntry(browser);
    await waitForAutofill(browser, "#cc-number", TEST_CARD["cc-number"]);
    Assert.equal(stop(), 1, "and filling asks nothing more");

    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": TEST_CARD["cc-name"], "cc-number": TEST_CARD["cc-number"] },
      "the card fills"
    );
  });

  await removePasswordAndCards();
});

add_task(async function test_wrong_password_prompts_again() {
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([
      "not-the-primary-password",
      PRIMARY_PASSWORD,
    ]);
    await openPopupOn(browser, "#cc-name");
    Assert.equal(stop(), 2, "a wrong password is followed by another prompt");
    await selectFirstEntry(browser);
    await waitForAutofill(browser, "#cc-number", TEST_CARD["cc-number"]);
  });

  await removePasswordAndCards();
});

add_task(async function test_listing_declined() {
  await SpecialPowers.pushPrefEnv({ set: [[REPROMPT_TIMEOUT_PREF, 60000]] });
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const canceled = TestUtils.topicObserved(
      "passwordmgr-crypto-loginCanceled"
    );
    const stop = answerPrimaryPasswordDialogs([null]);
    await SimpleTest.promiseFocus(browser);
    await focusAndWaitForFieldsIdentified(browser, "#cc-name");
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await canceled;
    await ensureNoAutocompletePopup(browser);
    // Closing the prompt hands focus back to the field, which searches again.
    Assert.equal(stop(), 1, "one prompt is shown, and not shown again");
    Assert.ok(!token.isLoggedIn, "the token stays locked");

    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": "", "cc-number": "" },
      "nothing is filled"
    );
  });

  await removePasswordAndCards();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_save_prompts_for_the_primary_password() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
  const onAdded = waitForStorageChangedEvents("add");
  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const onPopupShown = waitForPopupShown();
    await focusUpdateSubmitForm(browser, {
      focusSelector: "#cc-name",
      newValues: {
        "#cc-name": "User 1",
        "#cc-number": "5577000055770004",
        "#cc-exp-month": "12",
        "#cc-exp-year": "2030",
      },
    });
    await onPopupShown;
    await clickDoorhangerButton(MAIN_BUTTON);
  });
  await onAdded;
  Assert.equal(
    stop(),
    1,
    "comparing against every saved card and saving prompts once"
  );

  const cards = await getCreditCards();
  Assert.equal(cards.length, 3, "the card is saved");
  const saved = cards.find(card => card["cc-name"] == "User 1");
  Assert.equal(
    await formAutofillStorage.creditCards.decryptField(saved, "cc-number"),
    "5577000055770004",
    "with its number"
  );

  await removePasswordAndCards();
});

add_task(async function test_save_declined() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([null]);
  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const canceled = TestUtils.topicObserved(
      "passwordmgr-crypto-loginCanceled"
    );
    await focusUpdateSubmitForm(browser, {
      focusSelector: "#cc-name",
      newValues: {
        "#cc-name": "User 1",
        "#cc-number": "5577000055770004",
        "#cc-exp-month": "12",
        "#cc-exp-year": "2030",
      },
    });
    await canceled;
    await ensureNoDoorhanger();
  });
  Assert.equal(stop(), 1, "one decline answers for every saved card");
  Assert.ok(!token.isLoggedIn, "the token stays locked");
  // Counting does not decrypt, so it answers while the token is locked.
  Assert.equal(
    await formAutofillStorage.creditCards.refreshCount(),
    2,
    "nothing is saved"
  );

  await removePasswordAndCards();
});
