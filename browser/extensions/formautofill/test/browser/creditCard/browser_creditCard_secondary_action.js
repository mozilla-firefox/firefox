"use strict";

const PREF = "browser.autocomplete.removeRecords.enabled";
const AC_L10N = new Localization(
  ["toolkit/main-window/autocomplete.ftl"],
  true
);
const CC_URL =
  "https://example.org/browser/browser/extensions/formautofill/test/browser/creditCard/autocomplete_creditcard_basic.html";

const DELETE_TOOLTIP = AC_L10N.formatValueSync(
  "autocomplete-delete-payment-method"
);

// Checks every autocomplete event since the last reset shares one flow id.
function assertOneFlow() {
  const events = [
    Glean.autocomplete.dropdownShown,
    Glean.autocomplete.dropdownClosed,
    Glean.autocomplete.recordInteraction,
    Glean.autocomplete.recordDialogInteraction,
  ].flatMap(metric => metric.testGetValue() ?? []);
  const flowIds = new Set(events.map(e => e.extra.flow_id));
  is(flowIds.size, 1, "Every event shares one flow id");
  const [flowId] = flowIds;
  ok(flowId, "The shared flow id is not empty");
  return flowId;
}

add_setup(async function setup_storage() {
  await setStorage(TEST_CREDIT_CARD_1);
});

add_task(async function test_no_secondary_action_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, false]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async browser => {
      await openPopupOn(browser, "#cc-number");
      const rowItem = getDisplayedPopupItems(browser)[0].querySelector(
        "autocomplete-row-item"
      );
      is(
        rowItem.actions.secondary,
        null,
        "Payment rows have no secondary action when the pref is off"
      );
      ok(
        !rowItem.shadowRoot.querySelector("moz-button.secondary-action"),
        "No secondary action button is rendered"
      );
      await closePopup(browser);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_trash_button_when_pref_enabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async browser => {
      Services.fog.testResetFOG();
      await openPopupOn(browser, "#cc-number");
      const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
      is(shownEvents?.length, 1, "One dropdown shown event is recorded");
      is(
        shownEvents[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );

      const rowItem = getDisplayedPopupItems(browser)[0].querySelector(
        "autocomplete-row-item"
      );
      is(
        rowItem.actions.secondary.type,
        "delete",
        "Payment rows show a delete secondary action when the pref is on"
      );
      ok(
        !rowItem.actions.secondary.actions,
        "The trash is a single action, not a flyout"
      );

      const { label, tooltip } = rowItem.actions.secondary;
      is(tooltip, DELETE_TOOLTIP, "The tooltip stays short and omits the row");
      ok(
        label.includes(TEST_CREDIT_CARD_1["cc-number"].slice(-4)),
        `The accessible name names the row it belongs to, got "${label}"`
      );

      const button = rowItem.shadowRoot.querySelector(
        "moz-button.secondary-action"
      );
      ok(
        button.iconSrc.endsWith("delete.svg"),
        "The secondary action shows the trash icon"
      );
      await closePopup(browser);
      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      is(closedEvents?.length, 1, "One dropdown closed event is recorded");
      is(
        closedEvents[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );
      is(
        closedEvents[0].extra.close_reason,
        "dismissed",
        "Blurring the field records a dismissed close"
      );
      ok(closedEvents[0].extra.flow_id, "The event carries a flow id");
      is(
        closedEvents[0].extra.flow_id,
        shownEvents[0].extra.flow_id,
        "The closed event's flow id matches the shown event for the same dropdown"
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});

async function selectFirstRow(browser, item) {
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await TestUtils.waitForCondition(
    () => item.hasAttribute("selected"),
    "Wait for the first row to become active"
  );
}

// The Lit render lags the row's selected attribute, so the button can still be
// hidden when the row is already active. Wait it out before clicking, or the
// click falls through to the row and fills the form instead.
async function clickTrashButton(rowItem) {
  const button = rowItem.shadowRoot.querySelector(
    "moz-button.secondary-action"
  );

  await EventUtils.promiseElementReadyForUserInput(button, window, info);
  await TestUtils.waitForCondition(
    () => button.checkVisibility({ checkVisibilityCSS: true }),
    "Wait for the trash button to be visible"
  );
  EventUtils.synthesizeMouseAtCenter(button, {}, window);
}

// Opens the dropdown on a selected row with device sign in stubbed out, so the
// tests below never depend on a real OS prompt. `verifyArgs` records what the
// parent asked for, and `restore` puts the real implementation back.
// `hidesDropdown` closes the dropdown first, as a real OS prompt does.
async function openDeleteTarget(
  browser,
  { verified = true, hidesDropdown = false } = {}
) {
  const originalVerify = FormAutofillUtils.verifyUserOSAuth;
  const verifyArgs = [];
  FormAutofillUtils.verifyUserOSAuth = async (...args) => {
    verifyArgs.push(args);
    if (hidesDropdown) {
      const popup = browser.autoCompletePopup;
      popup.hidePopup();
      await TestUtils.waitForCondition(
        () => popup.state == "closed",
        "Wait for the dropdown to close before device sign in resolves"
      );
    }
    return verified;
  };

  await openPopupOn(browser, "#cc-number");
  const item = getDisplayedPopupItems(browser)[0];
  const rowItem = item.querySelector("autocomplete-row-item");
  await selectFirstRow(browser, item);

  return {
    rowItem,
    verifyArgs,
    restore: () => {
      FormAutofillUtils.verifyUserOSAuth = originalVerify;
    },
  };
}

async function acceptRemoval(rowItem) {
  await withStorageChange("remove", async () => {
    const dialogClosed = BrowserTestUtils.promiseAlertDialog("accept");
    await clickTrashButton(rowItem);
    await dialogClosed;
  });
}

add_task(async function test_delete_asks_for_device_sign_in_then_confirms() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  let sawDialog = false;

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async browser => {
      const { rowItem, verifyArgs, restore } = await openDeleteTarget(browser, {
        verified: true,
      });

      const dialogClosed = BrowserTestUtils.promiseAlertDialog(
        null,
        undefined,
        {
          callback: win => {
            sawDialog = true;
            const [title, message] = AC_L10N.formatValuesSync([
              { id: "autocomplete-delete-payment-method-title" },
              { id: "autocomplete-remove-record-message" },
            ]);
            is(
              win.document.getElementById("infoTitle").textContent,
              title,
              "The confirmation asks whether to remove the payment method"
            );
            is(
              win.document.getElementById("infoBody").textContent,
              message,
              "The confirmation warns the action cannot be undone"
            );
            win.document.querySelector("dialog").getButton("cancel").click();
          },
        }
      );

      Services.fog.testResetFOG();
      await clickTrashButton(rowItem);
      await dialogClosed;
      await TestUtils.waitForCondition(
        () => browser.autoCompletePopup.popupOpen,
        "Wait for the dropdown to come back after the confirmation"
      );

      const events = Glean.autocomplete.recordDialogInteraction.testGetValue();
      is(events?.length, 1, "One dialog interaction event is recorded");
      is(
        events[0].extra.interaction_type,
        "cancel",
        "Cancelling the dialog records a cancel interaction"
      );
      is(
        events[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );
      is(events[0].extra.update_type, "delete", "The update type is delete");

      const clickEvents = Glean.autocomplete.recordInteraction.testGetValue();
      is(clickEvents?.length, 1, "One interaction event is recorded");
      is(
        clickEvents[0].extra.interaction_type,
        "delete",
        "The trash button records a delete interaction"
      );
      is(
        clickEvents[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );
      ok(clickEvents[0].extra.flow_id, "The interaction has a flow id");
      is(
        events[0].extra.flow_id,
        clickEvents[0].extra.flow_id,
        "The dialog event shares the interaction's flow id"
      );

      const { FormAutofill } = ChromeUtils.importESModule(
        "resource://autofill/FormAutofill.sys.mjs"
      );
      is(verifyArgs.length, 1, "Device sign in was requested");
      is(
        verifyArgs[0][0],
        FormAutofill.AUTOFILL_CREDITCARDS_OS_AUTH_LOCKED_PREF,
        "Device sign in is gated on the payment methods reauth pref"
      );
      is(
        verifyArgs[0][4],
        false,
        "Removal does not let the OS key store generate a key it never reads"
      );
      ok(sawDialog, "The confirmation dialog was shown");
      ok(
        browser.autoCompletePopup.popupOpen,
        "The dropdown is restored once the confirmation is dismissed"
      );

      const cards = await getCreditCards();
      is(
        cards.length,
        1,
        "Cancelling the confirmation leaves the payment method in place"
      );

      restore();
      await closePopup(browser);
    }
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_delete_skips_confirm_when_device_sign_in_fails() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  let sawDialog = false;
  const observer = () => {
    sawDialog = true;
  };
  Services.obs.addObserver(observer, "common-dialog-loaded");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async browser => {
      Services.fog.testResetFOG();
      const { rowItem, verifyArgs, restore } = await openDeleteTarget(browser, {
        verified: false,
        hidesDropdown: true,
      });

      await clickTrashButton(rowItem);
      await TestUtils.waitForCondition(
        () => verifyArgs.length,
        "Wait for device sign in to be requested"
      );
      await TestUtils.waitForTick();

      ok(
        !sawDialog,
        "No confirmation is shown when device sign in is declined"
      );
      is(
        Glean.autocomplete.recordDialogInteraction.testGetValue(),
        null,
        "No dialog interaction is recorded when device sign in is declined"
      );
      const clickEvents = Glean.autocomplete.recordInteraction.testGetValue();
      is(clickEvents?.length, 1, "One interaction event is recorded");
      is(
        clickEvents[0].extra.interaction_type,
        "delete",
        "The trash button records a delete interaction before device sign in"
      );
      is(
        clickEvents[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );
      is(
        (await getCreditCards()).length,
        1,
        "Declining device sign in leaves the payment method in place"
      );

      await TestUtils.waitForCondition(
        () => browser.autoCompletePopup.popupOpen,
        "Wait for the dropdown to come back after device sign in"
      );
      restore();
      await closePopup(browser);
      Assert.deepEqual(
        Glean.autocomplete.dropdownShown
          .testGetValue()
          ?.map(e => e.extra.reopened_after_removal),
        ["false", "true"],
        "Only the automatic reopen is marked as reopened after a removal"
      );
      Assert.deepEqual(
        Glean.autocomplete.dropdownClosed
          .testGetValue()
          ?.map(e => e.extra.close_reason),
        ["removal_prompt", "dismissed"],
        "The device sign in prompt and the final close are both recorded"
      );
      is(
        assertOneFlow(),
        clickEvents[0].extra.flow_id,
        "The reopened dropdown keeps the interaction's flow id"
      );
    }
  );
  Services.obs.removeObserver(observer, "common-dialog-loaded");
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_delete_removes_the_payment_method() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await removeAllRecords();
  await setStorage(TEST_CREDIT_CARD_1, TEST_CREDIT_CARD_2);
  const before = await getCreditCards();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async browser => {
      Services.fog.testResetFOG();
      const { rowItem, restore } = await openDeleteTarget(browser);
      const rowCount = getDisplayedPopupItems(browser).length;

      await acceptRemoval(rowItem);

      const events = Glean.autocomplete.recordInteraction.testGetValue();
      is(events?.length, 1, "One interaction event is recorded");
      is(
        events[0].extra.interaction_type,
        "delete",
        "The trash button records a delete interaction"
      );
      is(
        events[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );
      const dialogEvents =
        Glean.autocomplete.recordDialogInteraction.testGetValue();
      is(dialogEvents?.length, 1, "One dialog interaction event is recorded");
      is(
        dialogEvents[0].extra.interaction_type,
        "confirm",
        "Accepting the dialog records a confirm interaction"
      );
      is(
        dialogEvents[0].extra.record_type,
        "payment",
        "The event names the payment record type"
      );
      is(
        dialogEvents[0].extra.update_type,
        "delete",
        "The update type is delete"
      );

      const remaining = await getCreditCards();
      is(
        remaining.length,
        1,
        "Confirming the removal deleted one payment method"
      );
      ok(
        before.some(card => !remaining.some(kept => kept.guid == card.guid)),
        "Exactly one of the stored payment methods is gone"
      );

      await TestUtils.waitForCondition(
        () => browser.autoCompletePopup.popupOpen,
        "Wait for the dropdown to come back after the removal"
      );
      await TestUtils.waitForCondition(
        () => getDisplayedPopupItems(browser).length == rowCount - 1,
        "Wait for the restored dropdown to drop the removed row"
      );

      // The last payment method: nothing is left to show, so no reopen.
      const item = getDisplayedPopupItems(browser)[0];
      await selectFirstRow(browser, item);
      await acceptRemoval(item.querySelector("autocomplete-row-item"));
      restore();
      await TestUtils.waitForCondition(
        () => !browser.autoCompletePopup.popupOpen,
        "Wait for the dropdown to stay closed after the last removal"
      );
      is(
        Glean.autocomplete.recordInteraction.testGetValue()?.length,
        2,
        "Two interaction events are recorded"
      );
      is(
        Glean.autocomplete.recordDialogInteraction.testGetValue()?.length,
        2,
        "Two dialog interaction events are recorded"
      );
      Assert.deepEqual(
        Glean.autocomplete.dropdownShown
          .testGetValue()
          ?.map(e => e.extra.reopened_after_removal),
        ["false", "true"],
        "The dropdown is reopened only after the first removal"
      );
      Assert.deepEqual(
        Glean.autocomplete.dropdownClosed
          .testGetValue()
          ?.map(e => e.extra.close_reason),
        ["removal_prompt", "removal_prompt"],
        "Each confirmation dialog closes the dropdown as removal_prompt"
      );
      assertOneFlow();

      await closePopup(browser);
    }
  );
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_deleting_the_last_payment_method_closes_the_dropdown() {
    await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
    await removeAllRecords();
    await setStorage(TEST_CREDIT_CARD_1);

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: CC_URL },
      async browser => {
        const { rowItem, restore } = await openDeleteTarget(browser);

        await acceptRemoval(rowItem);
        restore();

        is(
          (await getCreditCards()).length,
          0,
          "The last payment method was removed from storage"
        );
        await TestUtils.waitForCondition(
          () => !browser.autoCompletePopup.popupOpen,
          "Wait for the dropdown to be torn down"
        );
      }
    );
    await removeAllRecords();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_delete_refuses_an_unknown_guid() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await removeAllRecords();
  await setStorage(TEST_CREDIT_CARD_1);

  const originalVerify = FormAutofillUtils.verifyUserOSAuth;
  FormAutofillUtils.verifyUserOSAuth = () => Promise.resolve(true);

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CC_URL },
    async browser => {
      await openPopupOn(browser, "#cc-number");

      const actor =
        browser.browsingContext.currentWindowGlobal.getActor("FormAutofill");
      const dialogClosed = BrowserTestUtils.promiseAlertDialog("accept");
      await actor.onAutoCompleteEntrySelected("FormAutofill:DeleteCreditCard", {
        guid: "no-such-guid",
      });
      await dialogClosed;

      is(
        (await getCreditCards()).length,
        1,
        "A guid that names no stored payment method removes nothing"
      );

      await TestUtils.waitForCondition(
        () => browser.autoCompletePopup.popupOpen,
        "Wait for the dropdown to come back after the refused removal"
      );
      await closePopup(browser);
    }
  );
  FormAutofillUtils.verifyUserOSAuth = originalVerify;
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

// Removal reads no card number, so a card the current key can no longer open
// must still be removable -- which is when a user most wants to be rid of it.
// A decrypt that always fails stands in for that card: rotating the real store
// label would outlive this file and break the next one to save a card.
add_task(async function test_delete_removes_a_card_that_cannot_be_decrypted() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await removeAllRecords();
  await setStorage(TEST_CREDIT_CARD_1);

  const originalDecrypt = OSKeyStore.decrypt;
  let decryptCalls = 0;
  OSKeyStore.decrypt = () => {
    decryptCalls++;
    return Promise.reject(Components.Exception("", Cr.NS_ERROR_FAILURE));
  };

  try {
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: CC_URL },
      async browser => {
        const { rowItem, restore } = await openDeleteTarget(browser);

        await acceptRemoval(rowItem);
        restore();

        is(
          (await getCreditCards()).length,
          0,
          "A payment method whose number cannot be decrypted is still removed"
        );
        is(decryptCalls, 0, "The removal never reached for the card number");
      }
    );
  } finally {
    OSKeyStore.decrypt = originalDecrypt;
  }
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});

// Like about:logins, a device sign in is reused for 5 minutes in the same tab,
// until the user switches tabs.
add_task(
  async function test_recent_device_sign_in_is_reused_until_tab_switch() {
    await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
    await removeAllRecords();
    await setStorage(TEST_CREDIT_CARD_1);
    const { AutocompleteRemoveRecord } = ChromeUtils.importESModule(
      "resource://gre/modules/AutocompleteRemoveRecord.sys.mjs"
    );
    AutocompleteRemoveRecord.resetAuthForTesting();

    const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, CC_URL);
    const browser = tab.linkedBrowser;
    const popup = browser.autoCompletePopup;
    let verified = false;
    let verifyCalls = 0;
    const originalVerify = FormAutofillUtils.verifyUserOSAuth;
    // A real OS prompt takes focus and closes the dropdown before it resolves.
    FormAutofillUtils.verifyUserOSAuth = async () => {
      verifyCalls++;
      popup.hidePopup();
      await TestUtils.waitForCondition(
        () => popup.state == "closed",
        "Wait for the dropdown to close before device sign in resolves"
      );
      return verified;
    };

    const waitForReopen = () =>
      TestUtils.waitForCondition(
        () => popup.popupOpen,
        "Wait for the dropdown to come back after the removal"
      );
    const clickFirstTrash = async () => {
      const item = getDisplayedPopupItems(browser)[0];
      await selectFirstRow(browser, item);
      await clickTrashButton(item.querySelector("autocomplete-row-item"));
    };
    const deleteAndCancel = async () => {
      const dialogClosed = BrowserTestUtils.promiseAlertDialog("cancel");
      await clickFirstTrash();
      await dialogClosed;
      await waitForReopen();
    };

    try {
      await openPopupOn(browser, "#cc-number");
      await clickFirstTrash();
      await TestUtils.waitForCondition(
        () => verifyCalls == 1,
        "Wait for the declined device sign in"
      );
      await waitForReopen();

      verified = true;
      await deleteAndCancel();
      is(verifyCalls, 2, "A declined device sign in is not reused");

      await deleteAndCancel();
      is(verifyCalls, 2, "A recent device sign in in the same tab is reused");
      await closePopup(browser);

      const otherTab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );
      await BrowserTestUtils.switchTab(gBrowser, tab);
      BrowserTestUtils.removeTab(otherTab);
      await openPopupOn(browser, "#cc-number");
      await deleteAndCancel();
      is(verifyCalls, 3, "Switching tabs drops the recent device sign in");
      await closePopup(browser);

      is(
        (await getCreditCards()).length,
        1,
        "Cancelling every confirmation keeps the payment method"
      );
    } finally {
      FormAutofillUtils.verifyUserOSAuth = originalVerify;
      AutocompleteRemoveRecord.resetAuthForTesting();
      BrowserTestUtils.removeTab(tab);
    }
    await removeAllRecords();
    await SpecialPowers.popPrefEnv();
  }
);

// A reload, or leaving and coming back through the bfcache, asks for a new
// device sign in.
add_task(async function test_recent_device_sign_in_is_dropped_on_navigation() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await removeAllRecords();
  await setStorage(TEST_CREDIT_CARD_1);
  const { AutocompleteRemoveRecord } = ChromeUtils.importESModule(
    "resource://gre/modules/AutocompleteRemoveRecord.sys.mjs"
  );
  AutocompleteRemoveRecord.resetAuthForTesting();

  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, CC_URL);
  const browser = tab.linkedBrowser;
  const popup = browser.autoCompletePopup;
  let verifyCalls = 0;
  const originalVerify = FormAutofillUtils.verifyUserOSAuth;
  // A real OS prompt takes focus and closes the dropdown before it resolves.
  FormAutofillUtils.verifyUserOSAuth = async () => {
    verifyCalls++;
    popup.hidePopup();
    await TestUtils.waitForCondition(
      () => popup.state == "closed",
      "Wait for the dropdown to close before device sign in resolves"
    );
    return true;
  };
  const deleteAndCancel = async () => {
    await openPopupOn(browser, "#cc-number");
    const dialogClosed = BrowserTestUtils.promiseAlertDialog("cancel");
    const item = getDisplayedPopupItems(browser)[0];
    await selectFirstRow(browser, item);
    await clickTrashButton(item.querySelector("autocomplete-row-item"));
    await dialogClosed;
    await TestUtils.waitForCondition(
      () => popup.popupOpen,
      "Wait for the dropdown to come back after the removal"
    );
    await closePopup(browser);
  };

  try {
    await deleteAndCancel();
    await deleteAndCancel();
    is(verifyCalls, 1, "A recent device sign in on the same page is reused");

    await BrowserTestUtils.reloadTab(tab);
    await deleteAndCancel();
    is(verifyCalls, 2, "A reload drops the recent device sign in");

    const innerWindowID = browser.innerWindowID;
    BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
    await BrowserTestUtils.browserLoaded(browser);
    const wentBack = BrowserTestUtils.waitForLocationChange(gBrowser, CC_URL);
    browser.goBack();
    await wentBack;
    is(
      browser.innerWindowID,
      innerWindowID,
      "Going back restores the page from the bfcache"
    );
    await deleteAndCancel();
    is(verifyCalls, 3, "Leaving the page drops the recent device sign in");
  } finally {
    FormAutofillUtils.verifyUserOSAuth = originalVerify;
    AutocompleteRemoveRecord.resetAuthForTesting();
    BrowserTestUtils.removeTab(tab);
  }
  await removeAllRecords();
  await SpecialPowers.popPrefEnv();
});
