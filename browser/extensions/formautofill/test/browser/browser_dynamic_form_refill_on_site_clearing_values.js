/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.formautofill.addresses.supported", "on"]],
  });

  await setStorage(TEST_ADDRESS_1);

  registerCleanupFunction(async () => {
    await removeAllRecords();
  });
});

/**
 * Test that a previously autofilled element is re-filled if the site
 * cleared the element's value within the refill timeout threshold
 */
add_task(async function address_field_refilled_after_cleared_by_site() {
  await BrowserTestUtils.withNewTab(ADDRESS_FORM_URL, async browser => {
    const selectorToTriggerAutocompletion = "#organization";
    const elementValueToVerifyAutofill = TEST_ADDRESS_1.organization;

    info("Triggering autocompletion.");
    await openPopupOn(browser, selectorToTriggerAutocompletion);
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
    await waitForAutofill(
      browser,
      selectorToTriggerAutocompletion,
      elementValueToVerifyAutofill
    );

    await SpecialPowers.spawn(
      browser,
      [selectorToTriggerAutocompletion, TEST_ADDRESS_1.organization],
      async (organizationSelector, orgaInput) => {
        const organizationInput =
          content.document.querySelector(organizationSelector);

        const refillPromise = new Promise(resolve => {
          organizationInput.addEventListener("input", event => {
            if (event.target.value == orgaInput) {
              resolve();
            }
          });
        });

        info("Clearing previously autofilled input field");
        organizationInput.value = "";

        info("Waiting for cleared fields to be re-filled");
        await refillPromise;
      }
    );
  });

  ok(true, "Element was re-filled");
});

/**
 * Test that a previously autofilled element is not re-filled if the user clears
 * the element's value within the refill timeout threshold
 */
add_task(
  async function address_field_not_refilled_after_cleared_by_user_input() {
    const orgaValue = await BrowserTestUtils.withNewTab(
      ADDRESS_FORM_URL,
      async browser => {
        const selectorToTriggerAutocompletion = "#organization";
        const elementValueToVerifyAutofill = TEST_ADDRESS_1.organization;

        info("Triggering autocompletion.");
        await openPopupOn(browser, selectorToTriggerAutocompletion);
        await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
        await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
        await waitForAutofill(
          browser,
          selectorToTriggerAutocompletion,
          elementValueToVerifyAutofill
        );

        await SpecialPowers.spawn(
          browser,
          [selectorToTriggerAutocompletion],
          async organizationSelector => {
            const organizationInput =
              content.document.querySelector(organizationSelector);

            info("Simulating user clearing an input");
            organizationInput.setUserInput("");
          }
        );

        /* eslint-disable mozilla/no-arbitrary-setTimeout */
        await new Promise(resolve => {
          setTimeout(resolve, FormAutofill.refillOnSiteClearingFields);
        });

        return await SpecialPowers.spawn(
          browser,
          [selectorToTriggerAutocompletion],
          async organizationSelector => {
            return content.document.querySelector(organizationSelector).value;
          }
        );
      }
    );
    Assert.equal(orgaValue, "", "Element was not refilled");
  }
);
