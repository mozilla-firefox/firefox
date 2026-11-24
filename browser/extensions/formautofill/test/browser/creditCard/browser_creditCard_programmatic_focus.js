/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Bug 1963301: a field focused by the page's script must not open the
// autocomplete popup by itself, only once the user interacts with it.

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.skipProgrammaticCheckForTests", false]],
  });
  await setStorage(TEST_CREDIT_CARD_1);
  registerCleanupFunction(async () => {
    await removeAllRecords();
  });
});

add_task(async function test_programmatic_focus_does_not_open_popup() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async browser => {
      await SimpleTest.promiseFocus(browser);
      await focusAndWaitForFieldsIdentified(browser, "#cc-number");
      await ensureNoAutocompletePopup(browser);
    }
  );
});

add_task(async function test_user_click_opens_popup() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async browser => {
      await SimpleTest.promiseFocus(browser);
      await waitForAutoCompletePopupOpen(browser, () =>
        BrowserTestUtils.synthesizeMouseAtCenter("#cc-number", {}, browser)
      );
      ok(
        getDisplayedPopupItems(browser).length,
        "Clicking the field opens the popup"
      );
      await closePopup(browser);
    }
  );
});

add_task(async function test_user_key_after_programmatic_focus_opens_popup() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: CREDITCARD_FORM_URL },
    async browser => {
      await SimpleTest.promiseFocus(browser);
      await focusAndWaitForFieldsIdentified(browser, "#cc-number");
      await ensureNoAutocompletePopup(browser);

      await waitForAutoCompletePopupOpen(browser, () =>
        BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser)
      );
      ok(
        getDisplayedPopupItems(browser).length,
        "Pressing a key in the field opens the popup"
      );
      await closePopup(browser);
    }
  );
});
