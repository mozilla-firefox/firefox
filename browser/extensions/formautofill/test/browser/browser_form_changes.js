/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const MOCK_STORAGE = [
  {
    name: "John Doe",
    organization: "Sesame Street",
    "address-level2": "Austin",
    tel: "+13453453456",
  },
  {
    name: "Foo Bar",
    organization: "Mozilla",
    "address-level2": "San Francisco",
    tel: "+16509030800",
  },
];

function makeAddressComment({ primary, secondary, status }) {
  return JSON.stringify({
    primary,
    secondary,
    status,
    ariaLabel: primary + " " + secondary + " " + status,
  });
}

async function removeInputField(browser, selector) {
  await SpecialPowers.spawn(browser, [{ selector }], async args => {
    content.document.querySelector(args.selector).remove();
  });
}

async function addInputField(browser, formId, className) {
  await SpecialPowers.spawn(browser, [{ formId, className }], async args => {
    const newElem = content.document.createElement("input");
    newElem.name = args.className;
    newElem.autocomplete = args.className;
    newElem.type = "text";
    const form = content.document.querySelector(`#${args.formId}`);
    form.appendChild(newElem);
  });
}

async function checkFieldsAutofilled(browser, formId, profile) {
  await SpecialPowers.spawn(browser, [{ formId, profile }], async args => {
    const elements = content.document.querySelectorAll(`#${args.formId} input`);
    for (const element of elements) {
      await ContentTaskUtils.waitForCondition(() => {
        return element.value == args.profile[element.name];
      });
      await ContentTaskUtils.waitForCondition(
        () => element.matches(":autofill"),
        `Checking #${element.id} highlight style`
      );
    }
  });
}

// Compare the comment on the autocomplete menu items to the expected comment.
// The profile field is not compared.
async function checkMenuEntries(
  browser,
  expectedValues,
  extraRows = 1,
  { checkComment = false } = {}
) {
  const expectedLength = expectedValues.length + extraRows;

  let actualValues;
  await BrowserTestUtils.waitForCondition(() => {
    actualValues = browser.autoCompletePopup.view.results;
    return actualValues.length == expectedLength;
  });
  is(actualValues.length, expectedLength, " Checking length of expected menu");

  for (let i = 0; i < expectedValues.length; i++) {
    if (checkComment) {
      const expectedValue = JSON.parse(expectedValues[i]);
      const actualValue = JSON.parse(actualValues[i].comment);
      for (const [key, value] of Object.entries(expectedValue)) {
        is(
          actualValue[key],
          value,
          `Checking menu entry #${i}, ${key} should be the same`
        );
      }
    } else {
      is(actualValues[i].label, expectedValues[i], "Checking menu entry #" + i);
    }
  }
}

const TEST_PAGE = `
<form id="form1">
  <p><label>organization: <input name="organization" autocomplete="organization" type="text"></label></p>
  <p><label>tel: <input name="tel" autocomplete="tel" type="text"></label></p>
  <p><label>name: <input name="name" autocomplete="name" type="text"></label></p>
</form>
<div id="form2">
  <p><label>organization: <input name="organization" autocomplete="organization" type="text"></label></p>
  <p><label>tel: <input name="tel" autocomplete="tel" type="text"></label></p>
  <p><label>name: <input name="name" autocomplete="name" type="text"></label></p>
</div>`;

async function checkFormChangeHappened(formId) {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: `https://example.com/document-builder.sjs?html=${encodeURIComponent(
        TEST_PAGE
      )}`,
    },
    async browser => {
      await openPopupOn(browser, `#${formId} input[name=tel]`);
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);

      await checkMenuEntries(
        browser,
        MOCK_STORAGE.map(address =>
          makeAddressComment({
            primary: address.tel,
            secondary: address.name,
            status: "Also autofills name, organization",
          })
        ),
        2,
        { checkComment: true }
      );

      await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);

      // Click the first entry of the autocomplete popup and make sure all fields are autofilled
      await checkFieldsAutofilled(browser, formId, MOCK_STORAGE[0]);

      const fieldsDetectedAfterFieldAdded =
        getFieldDetectionCompletedPromiseResolver();

      addInputField(browser, formId, "address-level2");

      await fieldsDetectedAfterFieldAdded;

      await openPopupOn(browser, `#${formId} input[name=name]`);

      // Click on an autofilled field would show an autocomplete popup with "clear form" entry
      await checkMenuEntries(
        browser,
        [
          "Clear Autofill Form", // Clear Autofill Form
          "Manage addresses", // FormAutofill Preference
        ],
        0
      );

      const fieldDetectedAfterRemovingField =
        getFieldDetectionCompletedPromiseResolver();

      // This is for checking the changes of element removed and added then.
      removeInputField(browser, `#${formId} input[name=address-level2]`);

      await fieldDetectedAfterRemovingField;

      const fieldDetectedAfterAddingField =
        getFieldDetectionCompletedPromiseResolver();

      addInputField(browser, formId, "address-level2");

      await fieldDetectedAfterAddingField;

      await openPopupOn(browser, `#${formId} input[name=address-level2]`);

      await checkMenuEntries(
        browser,
        MOCK_STORAGE.map(address =>
          makeAddressComment({
            primary: address["address-level2"],
            secondary: address.name,
            status: "Also autofills name, organization, phone",
          })
        ),
        2,
        { checkComment: true }
      );

      // Make sure everything is autofilled in the end
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
      await checkFieldsAutofilled(browser, formId, MOCK_STORAGE[0]);
    }
  );
}

add_setup(async function () {
  await setStorage(MOCK_STORAGE[0]);
  await setStorage(MOCK_STORAGE[1]);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.heuristics.detectDynamicFormChanges", true],
    ],
  });
});

add_task(async function check_change_happened_in_form() {
  await checkFormChangeHappened("form1");
});

add_task(async function check_change_happened_in_body() {
  await checkFormChangeHappened("form2");
});
