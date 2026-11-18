/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The primary password prompt of the Rust credit card store. The store's key
 * lives in NSS, so once the user sets a primary password and the token is
 * locked, reading or writing a card number asks for it: filling a card,
 * saving one, and submitting a number a saved card ends in, which has to be
 * compared. Listing or removing cards, and offering to save a new one, ask for
 * nothing.
 */

const ENABLED_PREF = "extensions.formautofill.creditCards.storage.rust.enabled";
const ACTIVE_PREF = "extensions.formautofill.creditCards.storage.rust.active";

const { LoginTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/LoginTestUtils.sys.mjs"
);
const { FormAutofillPreferences } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillPreferences.sys.mjs"
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
  // Read a number while unlocked, which forgets a prompt an earlier task
  // cancelled.
  const [card] = await getCreditCards();
  await formAutofillStorage.creditCards.decryptField(card, "cc-number");
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
      // A cancelled prompt holds off the next one for this long.
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

add_task(async function test_filling_prompts_for_the_primary_password() {
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    let stop = answerPrimaryPasswordDialogs([]);
    await openPopupOn(browser, "#cc-name");
    Assert.equal(stop(), 0, "listing the cards does not prompt");
    Assert.ok(!token.isLoggedIn, "and leaves the token locked");

    const loggedIn = TestUtils.topicObserved("passwordmgr-crypto-login");
    stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
    await selectFirstEntry(browser);
    await loggedIn;
    await waitForAutofill(browser, "#cc-number", TEST_CARD["cc-number"]);
    Assert.equal(stop(), 1, "filling the card prompts once");

    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": TEST_CARD["cc-name"], "cc-number": TEST_CARD["cc-number"] },
      "the card fills"
    );
  });

  await removePasswordAndCards();
});

add_task(async function test_the_number_field_lists_cards_without_a_prompt() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([]);
    await openPopupOn(browser, "#cc-number");
    Assert.equal(
      getDisplayedPopupItems(browser, "[originaltype='autofill']").length,
      2,
      "both cards are offered on the number field"
    );
    await closePopup(browser);
    Assert.equal(stop(), 0, "without a prompt");
    Assert.ok(!token.isLoggedIn, "and the token stays locked");
  });

  await removePasswordAndCards();
});

add_task(async function test_forms_are_detected_without_a_prompt() {
  const { FormAutofillStatus } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillParent.sys.mjs"
  );
  await lockWithCards(TEST_CARD);

  const stop = answerPrimaryPasswordDialogs([]);
  Services.ppmm.sharedData.set("FormAutofill:savedFieldNames", new Set());
  await FormAutofillStatus.updateSavedFieldNames();
  const savedFieldNames = Services.ppmm.sharedData.get(
    "FormAutofill:savedFieldNames"
  );
  Assert.ok(
    savedFieldNames.has("cc-number"),
    "the saved card fields are shared with content"
  );
  Assert.equal(stop(), 0, "without a prompt");
  Assert.ok(!token.isLoggedIn, "and the token stays locked");

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    await openPopupOn(browser, "#cc-name");
    await closePopup(browser);
  });

  await removePasswordAndCards();
});

add_task(async function test_previewing_a_card_does_not_prompt() {
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([]);
    await openPopupOn(browser, "#cc-name");
    const previewed = TestUtils.topicObserved("formautofill-preview-complete");
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await previewed;
    const preview = await SpecialPowers.spawn(browser, [], () => {
      const number = content.document.getElementById("cc-number");
      return { state: number.autofillState, value: number.previewValue };
    });
    Assert.equal(preview.state, "preview", "the card is previewed");
    Assert.equal(
      preview.value.replace(/\D/g, ""),
      TEST_CARD["cc-number"].slice(-4),
      "with its masked number, showing only the last four digits"
    );

    await closePopup(browser);
    Assert.equal(stop(), 0, "previewing and dismissing ask nothing");
    Assert.ok(!token.isLoggedIn, "the token stays locked");
    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": "", "cc-number": "" },
      "nothing is filled"
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
    await selectFirstEntry(browser);
    await waitForAutofill(browser, "#cc-number", TEST_CARD["cc-number"]);
    Assert.equal(stop(), 2, "a wrong password is followed by another prompt");
  });

  await removePasswordAndCards();
});

add_task(async function test_filling_declined() {
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([null]);
    await openPopupOn(browser, "#cc-name");
    const canceled = TestUtils.topicObserved(
      "passwordmgr-crypto-loginCanceled"
    );
    await selectFirstEntry(browser);
    await canceled;
    Assert.equal(stop(), 1, "one prompt is shown");
    Assert.ok(!token.isLoggedIn, "the token stays locked");

    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": "", "cc-number": "" },
      "nothing is filled"
    );
  });

  await removePasswordAndCards();
});

add_task(async function test_managing_cards_does_not_prompt() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([]);
  const win = window.openDialog(MANAGE_CREDIT_CARDS_DIALOG_URL);
  await waitForFocusAndFormReady(win);
  const records = win.document.querySelector("#credit-cards");
  await TestUtils.waitForCondition(
    () => records.length == 2,
    "both cards are listed"
  );

  EventUtils.synthesizeMouseAtCenter(records.children[0], {}, win);
  EventUtils.synthesizeMouseAtCenter(
    win.document.querySelector("#remove"),
    {},
    win
  );
  await BrowserTestUtils.waitForEvent(records, "RecordsRemoved");
  Assert.equal(records.length, 1, "a card is removed");
  win.close();

  Assert.equal(
    (await getCreditCards()).length,
    1,
    "and read back by the parent process"
  );
  Assert.equal(stop(), 0, "without a prompt");
  Assert.ok(!token.isLoggedIn, "and the token stays locked");

  await removePasswordAndCards();
});

const NEW_CARD_VALUES = {
  "#cc-name": "User 1",
  "#cc-number": "5577000055770004",
  "#cc-exp-month": "12",
  "#cc-exp-year": "2030",
};

add_task(async function test_save_prompts_for_the_primary_password() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
  const onAdded = waitForStorageChangedEvents("add");
  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const onPopupShown = waitForPopupShown();
    await focusUpdateSubmitForm(browser, {
      focusSelector: "#cc-name",
      newValues: NEW_CARD_VALUES,
    });
    await onPopupShown;
    Assert.ok(
      !token.isLoggedIn,
      "a number no saved card ends in is offered without a prompt"
    );
    await clickDoorhangerButton(MAIN_BUTTON);
  });
  await onAdded;
  Assert.equal(stop(), 1, "saving it prompts once");

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

add_task(async function test_not_saving_does_not_prompt() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([]);
  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const onPopupShown = waitForPopupShown();
    await focusUpdateSubmitForm(browser, {
      focusSelector: "#cc-name",
      newValues: NEW_CARD_VALUES,
    });
    await onPopupShown;
    await clickDoorhangerButton(SECONDARY_BUTTON);
  });
  Assert.equal(stop(), 0, "declining the doorhanger shows no prompt");
  Assert.ok(!token.isLoggedIn, "the token stays locked");
  Assert.equal(
    await formAutofillStorage.creditCards.refreshCount(),
    2,
    "nothing is saved"
  );

  await removePasswordAndCards();
});

add_task(async function test_save_declined() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([null]);
  const canceled = TestUtils.topicObserved("passwordmgr-crypto-loginCanceled");
  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const onPopupShown = waitForPopupShown();
    await focusUpdateSubmitForm(browser, {
      focusSelector: "#cc-name",
      newValues: NEW_CARD_VALUES,
    });
    await onPopupShown;
    await clickDoorhangerButton(MAIN_BUTTON);
    await canceled;
  });
  Assert.equal(stop(), 1, "one prompt is shown");
  Assert.ok(!token.isLoggedIn, "the token stays locked");
  // Counting does not decrypt, so it answers while the token is locked.
  Assert.equal(
    await formAutofillStorage.creditCards.refreshCount(),
    2,
    "nothing is saved"
  );

  await removePasswordAndCards();
});

add_task(async function test_resubmitting_a_saved_number_prompts_to_compare() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  // A saved card ends in the same four digits, so only the number itself
  // tells whether this is that card.
  const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
  const onUsed = waitForStorageChangedEvents("notifyUsed");
  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    await focusUpdateSubmitForm(browser, {
      focusSelector: "#cc-name",
      newValues: {
        "#cc-name": TEST_CARD["cc-name"],
        "#cc-number": TEST_CARD["cc-number"],
        "#cc-exp-month": String(TEST_CARD["cc-exp-month"]),
        "#cc-exp-year": String(TEST_CARD["cc-exp-year"]),
      },
    });
    await onUsed;
  });
  await ensureNoDoorhanger();
  Assert.equal(stop(), 1, "comparing against the saved card prompts once");

  await removePasswordAndCards();
});

add_task(async function test_payments_list_does_not_prompt() {
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([]);
  await FormAutofillPreferences.prototype.initializePaymentsStorage();
  const items = await FormAutofillPreferences.prototype.makePaymentsListItems();
  Assert.equal(
    items.filter(item => item.id == "payment-item").length,
    2,
    "the settings payment methods list shows both cards"
  );
  Assert.equal(stop(), 0, "without a prompt");
  Assert.ok(!token.isLoggedIn, "and the token stays locked");

  await removePasswordAndCards();
});

add_task(async function test_a_security_code_is_listed_without_a_prompt() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.creditCards.cvv.supported", "on"],
      ["extensions.formautofill.creditCards.cvv.enabled", true],
    ],
  });
  await lockWithCards({ ...TEST_CARD, "cc-csc": "123" });

  let stop = answerPrimaryPasswordDialogs([]);
  await FormAutofillPreferences.prototype.initializePaymentsStorage();
  const [row] = (
    await FormAutofillPreferences.prototype.makePaymentsListItems()
  ).filter(item => item.id == "payment-item");
  Assert.equal(
    row.l10nId,
    "payment-moz-box-item-with-security-code-3",
    "the settings list notes the saved security code"
  );
  Assert.equal(stop(), 0, "without a prompt");

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    stop = answerPrimaryPasswordDialogs([]);
    await openPopupOn(browser, "#cc-csc");
    Assert.equal(stop(), 0, "the card is offered for the code field too");
    Assert.ok(!token.isLoggedIn, "and the token stays locked");

    stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
    await selectFirstEntry(browser);
    await waitForAutofill(browser, "#cc-csc", "123");
    Assert.equal(stop(), 1, "filling prompts once for the number and the code");
    Assert.equal(
      (await readForm(browser))["cc-number"],
      TEST_CARD["cc-number"],
      "and fills the number as well"
    );
  });

  await removePasswordAndCards();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_editing_prompts_once_for_both_secrets() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.creditCards.cvv.supported", "on"],
      ["extensions.formautofill.creditCards.cvv.enabled", true],
    ],
  });
  await lockWithCards({ ...TEST_CARD, "cc-csc": "123" });
  const [card] = await getCreditCards();

  const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
  const shown = await new Promise(resolve =>
    FormAutofillPreferences.openEditCreditCardDialog(card, {
      gSubDialog: { open: (_url, _features, { record }) => resolve(record) },
    })
  );
  Assert.equal(stop(), 1, "opening the edit dialog prompts once");
  Assert.equal(
    shown["cc-number"],
    TEST_CARD["cc-number"],
    "and shows the number"
  );
  Assert.equal(shown["cc-csc"], "123", "and the security code, in the clear");

  await removePasswordAndCards();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_a_card_deleted_after_listing_fills_nothing() {
  await lockWithCards(TEST_CARD);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([]);
    await openPopupOn(browser, "#cc-name");
    const [{ guid }] = await formAutofillStorage.creditCards.getAll();
    await formAutofillStorage.creditCards.remove(guid);

    const completed = TestUtils.topicObserved("formautofill-autofill-complete");
    await selectFirstEntry(browser);
    await completed;
    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": "", "cc-number": "" },
      "a card deleted since the dropdown opened fills nothing"
    );
    Assert.equal(stop(), 0, "and asks nothing");
  });

  await removePasswordAndCards();
});

add_task(async function test_a_declined_prompt_blocks_nothing_afterwards() {
  // At its default, so a decline that autofill remembered would show here.
  await SpecialPowers.pushPrefEnv({ clear: [[REPROMPT_TIMEOUT_PREF]] });
  await lockWithCards(TEST_CARD, TEST_ADDRESS_1);

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([null]);
    await openPopupOn(browser, "#cc-name");
    const canceled = TestUtils.topicObserved(
      "passwordmgr-crypto-loginCanceled"
    );
    await selectFirstEntry(browser);
    await canceled;
    Assert.equal(stop(), 1, "filling asks once, and is declined");
    Assert.deepEqual(
      await readForm(browser),
      { "cc-name": "", "cc-number": "" },
      "nothing is filled"
    );
  });
  Assert.ok(!token.isLoggedIn, "the token stays locked");

  await BrowserTestUtils.withNewTab(FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([]);
    await openPopupOn(browser, "#organization");
    Assert.ok(
      getDisplayedPopupItems(browser).length,
      "the address dropdown still opens"
    );
    await closePopup(browser);
    Assert.equal(stop(), 0, "without a prompt");
  });

  await BrowserTestUtils.withNewTab(CREDITCARD_FORM_URL, async browser => {
    const stop = answerPrimaryPasswordDialogs([PRIMARY_PASSWORD]);
    await openPopupOn(browser, "#cc-name");
    await selectFirstEntry(browser);
    await waitForAutofill(browser, "#cc-number", TEST_CARD["cc-number"]);
    Assert.equal(stop(), 1, "the next fill asks again, and fills");
  });

  await removePasswordAndCards();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_counting_cards_for_targeting_does_not_prompt() {
  const { ASRouterTargeting } = ChromeUtils.importESModule(
    "resource:///modules/asrouter/ASRouterTargeting.sys.mjs"
  );
  await lockWithCards(TEST_CARD, OTHER_CARD);

  const stop = answerPrimaryPasswordDialogs([]);
  Assert.equal(
    await ASRouterTargeting.Environment.creditCardsSaved,
    2,
    "the creditCardsSaved targeting attribute counts the cards"
  );
  Assert.equal(stop(), 0, "without a prompt");
  Assert.ok(!token.isLoggedIn, "and the token stays locked");

  await removePasswordAndCards();
});
