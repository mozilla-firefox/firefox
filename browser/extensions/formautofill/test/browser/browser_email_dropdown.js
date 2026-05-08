"use strict";

const PAGE_URL =
  "https://example.org/browser/browser/extensions/formautofill/test/fixtures/autocomplete_multiple_emails_checkout.html";

/**
 * These tests ensure that when a field (like email) is recognized by both
 * Login Manager and Form Autofill, the priority is handled correctly.
 * Since the fixture contains password fields, Login Manager is
 * prioritized over Form Autofill for the email field.
 */

add_task(
  async function test_email_field_shows_login_priority_with_saved_address() {
    await SpecialPowers.pushPrefEnv({
      set: [["signon.rememberSignons", true]],
    });

    // Even if an address is saved, the Login Manager takes priority because
    // the fixture contains password fields.
    await setStorage(TEST_ADDRESS_1);

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: PAGE_URL },
      async function (browser) {
        const focusInput = "#email";
        await focusAndWaitForFieldsIdentified(browser, "#given-name");
        await closePopup(browser);
        await openPopupOn(browser, focusInput);
        const item = getDisplayedPopupItems(browser)[0];

        is(
          item.getAttribute("ac-value"),
          "Manage Passwords",
          "Login Manager should have priority over Form Autofill when password fields are present"
        );

        await closePopup(browser);
      }
    );
  }
);

add_task(
  async function test_email_field_shows_login_dropdown_when_no_saved_address() {
    await removeAllRecords();
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: PAGE_URL },
      async function (browser) {
        const focusInput = "#email";
        await openPopupOn(browser, focusInput);
        const item = getDisplayedPopupItems(browser)[0];

        is(
          item.getAttribute("ac-value"),
          "Manage Passwords",
          "Login Manager should be shown when no addresses are saved"
        );

        await closePopup(browser);
      }
    );
  }
);

add_task(async function test_email_field_shows_login_priority_onfocus() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rememberSignons", true]],
  });
  await setStorage(TEST_ADDRESS_1);

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: PAGE_URL },
    async function (browser) {
      await SpecialPowers.spawn(browser, [], () => {
        content.document.getElementById("email").focus();
      });
      await runAndWaitForAutocompletePopupOpen(browser, () => {});

      const item = getDisplayedPopupItems(browser)[0];

      is(
        item.getAttribute("ac-value"),
        "Manage Passwords",
        "Login Manager should have priority on focus due to password fields"
      );

      await closePopup(browser);
    }
  );
});

add_task(
  async function test_email_field_shows_login_dropdown_when_no_saved_address_onfocus() {
    await removeAllRecords();
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: PAGE_URL },
      async function (browser) {
        await SpecialPowers.spawn(browser, [], () => {
          content.document.getElementById("email").focus();
        });
        await runAndWaitForAutocompletePopupOpen(browser, () => {});
        const item = getDisplayedPopupItems(browser)[0];

        is(
          item.getAttribute("ac-value"),
          "Manage Passwords",
          "Login Manager should be the result on focus"
        );

        await closePopup(browser);
      }
    );
  }
);

add_task(async function test_single_email_field_now_shows_address_autofill() {
  await removeAllRecords();
  await setStorage(TEST_ADDRESS_1);

  // To confirm that address autofill is working for a single email field, we use this fixture.
  const SINGLE_EMAIL_PAGE_URL =
    "https://example.org/browser/browser/extensions/formautofill/test/fixtures/autocomplete_single_email.html";

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: SINGLE_EMAIL_PAGE_URL },
    async function (browser) {
      const focusInput = "#email";
      await openPopupOn(browser, focusInput);

      const items = getDisplayedPopupItems(browser);

      is(
        items[0].getAttribute("ac-value"),
        TEST_ADDRESS_1.email,
        "The first result should be the saved email address"
      );

      is(
        items[1].getAttribute("ac-value"),
        "Manage addresses",
        "The second result should be the Manage addresses footer"
      );

      await closePopup(browser);
    }
  );
});
