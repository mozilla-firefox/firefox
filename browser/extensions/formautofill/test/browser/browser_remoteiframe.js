/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

const IFRAME_URL_PATH = BASE_URL + "autocomplete_iframe.html";

// Start by adding a few addresses to storage.
add_task(async function setup_storage() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AUTOFILL_ADDRESSES_AVAILABLE_PREF, "on"],
      [ENABLED_AUTOFILL_ADDRESSES_PREF, true],
      [ENABLED_AUTOFILL_ADDRESSES_CAPTURE_PREF, true],
      // set capture required fields to empty to make testcase simpler
      ["extensions.formautofill.addresses.capture.requiredFields", ""],
    ],
  });
  await setStorage(TEST_ADDRESS_2, TEST_ADDRESS_4, TEST_ADDRESS_5);
});

// Verify that form fillin works in a remote iframe, and that changing
// a field updates storage.
add_task(async function test_iframe_autocomplete() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    IFRAME_URL_PATH,
    true
  );
  let browser = tab.linkedBrowser;
  let iframeBC = browser.browsingContext.children[1];
  await openPopupOnSubframe(browser, iframeBC, "#street-address");

  // Highlight the first item in the list. We want to verify
  // that the warning text is correct to ensure that the preview is
  // performed properly.

  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, iframeBC);
  await expectWarningText(browser, "Autofills address");

  // Highlight and select the second item in the list
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, iframeBC);
  await expectWarningText(browser, "Also autofills organization, email");

  EventUtils.synthesizeKey("VK_RETURN", {});

  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(resolve => {
    setTimeout(resolve, FormAutofill.fillOnDynamicFormChangeTimeout);
  });

  let onLoaded = BrowserTestUtils.browserLoaded(browser, true);
  await SpecialPowers.spawn(iframeBC, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      return (
        content.document.getElementById("street-address").value ==
          "32 Vassar Street MIT Room 32-G524" &&
        content.document.getElementById("country").value == "US" &&
        content.document.getElementById("organization").value ==
          "World Wide Web Consortium"
      );
    });
  });

  const onPopupShown = waitForPopupShown();
  await focusUpdateSubmitForm(iframeBC, {
    focusSelector: "#organization",
    newValues: {
      "#tel": "+16172535702",
    },
  });
  await onPopupShown;
  await onLoaded;

  let onUpdated = waitForStorageChangedEvents("update");
  await clickDoorhangerButton(MAIN_BUTTON);
  await onUpdated;

  // Check that the tel number was updated properly.
  let addresses = await getAddresses();
  is(addresses.length, 3, "Still 3 address in storage");
  is(addresses[1].tel, "+16172535702", "Verify the tel field");

  // Fill in the details again and then clear the form from the dropdown.
  await openPopupOnSubframe(browser, iframeBC, "#street-address");
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, iframeBC);
  EventUtils.synthesizeKey("VK_RETURN", {});

  await waitForAutofill(iframeBC, "#tel", "+16172535702");

  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(resolve => {
    setTimeout(resolve, FormAutofill.fillOnDynamicFormChangeTimeout);
  });

  // Open the dropdown and select the Clear Form item.
  await openPopupOnSubframe(browser, iframeBC, "#street-address");
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, iframeBC);
  EventUtils.synthesizeKey("VK_RETURN", {});

  await SpecialPowers.spawn(iframeBC, [], async function () {
    await ContentTaskUtils.waitForCondition(() => {
      return (
        content.document.getElementById("street-address").value == "" &&
        content.document.getElementById("country").value == "" &&
        content.document.getElementById("organization").value == ""
      );
    });
  });

  await BrowserTestUtils.removeTab(tab);
});

// Choose preferences from the autocomplete dropdown within an iframe.
add_task(async function test_iframe_autocomplete_preferences() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    IFRAME_URL_PATH,
    true
  );
  let browser = tab.linkedBrowser;
  let iframeBC = browser.browsingContext.children[1];
  await openPopupOnSubframe(browser, iframeBC, "#organization");

  await expectWarningText(browser, "Also autofills address, phone, email");

  const prefTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    PRIVACY_PREF_URL
  );

  // Select the preferences item.
  EventUtils.synthesizeKey("VK_DOWN", {});
  EventUtils.synthesizeKey("VK_DOWN", {});
  EventUtils.synthesizeKey("VK_RETURN", {});

  info(`expecting tab: about:preferences#privacy opened`);
  const prefTab = await prefTabPromise;
  info(`expecting tab: about:preferences#privacy removed`);
  BrowserTestUtils.removeTab(prefTab);

  await BrowserTestUtils.removeTab(tab);
});
