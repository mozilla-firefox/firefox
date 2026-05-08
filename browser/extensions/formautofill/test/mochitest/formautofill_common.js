/* import-globals-from ../../../../../testing/mochitest/tests/SimpleTest/SimpleTest.js */
/* import-globals-from ../../../../../testing/mochitest/tests/SimpleTest/EventUtils.js */
/* import-globals-from ../../../../../toolkit/components/satchel/test/satchel_common.js */
/* eslint-disable no-unused-vars */
// Despite a use of `spawnChrome` and thus ChromeUtils, we can't use isInstance
// here as it gets used in plain mochitests which don't have the ChromeOnly
// APIs for it.
/* eslint-disable mozilla/use-isInstance */

"use strict";

let formFillChromeScript;
let defaultTextColor;
let defaultDisabledTextColor;
let expectingPopup = null;

const { FormAutofillUtils } = SpecialPowers.ChromeUtils.importESModule(
  "resource://gre/modules/shared/FormAutofillUtils.sys.mjs"
);

const { OSKeyStore } = SpecialPowers.ChromeUtils.importESModule(
  "resource://gre/modules/OSKeyStore.sys.mjs"
);

async function sleep(ms = 500, reason = "Intentionally wait for UI ready") {
  SimpleTest.requestFlakyTimeout(reason);
  await new Promise(resolve => setTimeout(resolve, ms));
}

async function focusAndWaitForFieldsIdentified(
  input,
  mustBeIdentified = false
) {
  info("expecting the target input being focused and indentified");
  if (typeof input === "string") {
    input = document.querySelector(input);
  }
  // Use the same root heuristic as AutofillFormFactory: in iframes, ignore <form>
  // and use the document root instead (since FormAutofillHandler uses document root).
  const rootElement =
    (window === window.top && input.form) ||
    input.ownerDocument.documentElement;
  const previouslyFocused = input == document.activeElement;

  let fieldsIdentifiedPromise = new Promise(resolve => {
    formFillChromeScript.addMessageListener(
      "FormAutofillTest:FieldsIdentified",
      function onIdentified() {
        formFillChromeScript.removeMessageListener(
          "FormAutofillTest:FieldsIdentified",
          onIdentified
        );
        resolve();
      }
    );
  });

  input.focus();

  if (mustBeIdentified) {
    rootElement.removeAttribute("test-formautofill-identified");
  }
  if (rootElement.hasAttribute("test-formautofill-identified")) {
    return previouslyFocused;
  }
  if (!previouslyFocused) {
    await fieldsIdentifiedPromise;
  }
  // In order to ensure that "markAsAutofillField" is fully executed, a short period
  // of timeout is still required.
  await sleep(300, "Guarantee asynchronous identifyAutofillFields is invoked");
  rootElement.setAttribute("test-formautofill-identified", "true");
  return previouslyFocused;
}

async function setInput(selector, value, userInput = false) {
  const input = document.querySelector("input" + selector);
  if (userInput) {
    SpecialPowers.wrap(input).setUserInput(value);
  } else {
    input.value = value;
  }
  await focusAndWaitForFieldsIdentified(input);

  return input;
}

function clickOnElement(selector) {
  let element = document.querySelector(selector);

  if (!element) {
    throw new Error("Can not find the element");
  }

  SimpleTest.executeSoon(() => element.click());
}

// The equivalent helper function to getAdaptedProfiles in
// FormAutofillSection.sys.mjs that transforms the given profile to expected
// filled profile.
function _getAdaptedProfile(profile) {
  const adaptedProfile = Object.assign({}, profile);

  if (profile["street-address"]) {
    adaptedProfile["street-address"] = FormAutofillUtils.toOneLineAddress(
      profile["street-address"]
    );
  }

  return adaptedProfile;
}

async function checkFieldHighlighted(elem, expectedValue) {
  let isHighlightApplied;
  await SimpleTest.promiseWaitForCondition(function checkHighlight() {
    isHighlightApplied = elem.matches(":autofill");
    return isHighlightApplied === expectedValue;
  }, `Checking #${elem.id} highlight style`);

  is(isHighlightApplied, expectedValue, `Checking #${elem.id} highlight style`);
}

async function checkFormFieldsStyle(profile, isPreviewing = true) {
  const elems = document.querySelectorAll("input, select");

  for (const elem of elems) {
    let fillableValue;
    let previewValue;
    let isElementEligible =
      FormAutofillUtils.isCreditCardOrAddressFieldType(elem) &&
      FormAutofillUtils.isFieldAutofillable(elem);
    if (!isElementEligible) {
      fillableValue = "";
      previewValue = "";
    } else {
      fillableValue = profile && profile[elem.id];
      previewValue =
        (isPreviewing && fillableValue?.toString().replaceAll("*", "•")) || "";
    }
    await checkFieldHighlighted(elem, !!fillableValue);
  }
}

function checkFieldValue(elem, expectedValue) {
  if (typeof elem === "string") {
    elem = document.querySelector(elem);
  }
  is(elem.value, String(expectedValue), "Checking " + elem.id + " field");
}

async function triggerAutofillAndCheckProfile(profile) {
  let adaptedProfile = _getAdaptedProfile(profile);
  const promises = [];
  for (const [fieldName, value] of Object.entries(adaptedProfile)) {
    info(`triggerAutofillAndCheckProfile: ${fieldName}`);
    const element = document.getElementById(fieldName);
    const expectingEvent =
      document.activeElement == element ? "input" : "change";
    const checkFieldAutofilled = Promise.all([
      new Promise(resolve => {
        let beforeInputFired = false;
        let hadEditor = SpecialPowers.wrap(element).hasEditor;
        element.addEventListener(
          "beforeinput",
          event => {
            beforeInputFired = true;
            is(
              event.inputType,
              "insertReplacementText",
              'inputType value should be "insertReplacementText"'
            );
            is(
              event.data,
              String(value),
              `data value of "beforeinput" should be "${value}"`
            );
            is(
              event.dataTransfer,
              null,
              'dataTransfer of "beforeinput" should be null'
            );
            is(
              event.getTargetRanges().length,
              0,
              'getTargetRanges() of "beforeinput" should return empty array'
            );
            is(
              event.cancelable,
              SpecialPowers.getBoolPref(
                "dom.input_event.allow_to_cancel_set_user_input"
              ),
              `"beforeinput" event should be cancelable on ${element.tagName} unless it's suppressed by the pref`
            );
            is(
              event.bubbles,
              true,
              `"beforeinput" event should always bubble on ${element.tagName}`
            );
            resolve();
          },
          { once: true }
        );
        element.addEventListener(
          "input",
          event => {
            if (
              (element.tagName == "INPUT" && element.type == "text") ||
              element.tagName == "TEXTAREA"
            ) {
              if (hadEditor) {
                ok(
                  beforeInputFired,
                  `"beforeinput" event should've been fired before "input" event on ${element.tagName}`
                );
              } else {
                ok(
                  beforeInputFired,
                  `"beforeinput" event should've been fired before "input" event on ${element.tagName}`
                );
              }
              ok(
                event instanceof InputEvent,
                `"input" event should be dispatched with InputEvent interface on ${element.tagName}`
              );
              is(
                event.inputType,
                "insertReplacementText",
                'inputType value should be "insertReplacementText"'
              );
              is(event.data, String(value), `data value should be "${value}"`);
              is(event.dataTransfer, null, "dataTransfer should be null");
              is(
                event.getTargetRanges().length,
                0,
                "getTargetRanges() should return empty array"
              );
            } else {
              ok(
                !beforeInputFired,
                `"beforeinput" event shouldn't be fired on ${element.tagName}`
              );
              ok(
                event instanceof Event && !(event instanceof UIEvent),
                `"input" event should be dispatched with Event interface on ${element.tagName}`
              );
            }
            is(
              event.cancelable,
              false,
              `"input" event should be never cancelable on ${element.tagName}`
            );
            is(
              event.bubbles,
              true,
              `"input" event should always bubble on ${element.tagName}`
            );
            resolve();
          },
          { once: true }
        );
      }),
      new Promise(resolve =>
        element.addEventListener(expectingEvent, resolve, { once: true })
      ),
    ]).then(() => checkFieldValue(element, value));

    promises.push(checkFieldAutofilled);
  }
  // Press Enter key and trigger form autofill.
  synthesizeKey("KEY_Enter");

  return Promise.all(promises);
}

async function onStorageChanged(type) {
  info(`expecting the storage changed: ${type}`);
  return new Promise(resolve => {
    formFillChromeScript.addMessageListener(
      "formautofill-storage-changed",
      function onChanged(data) {
        formFillChromeScript.removeMessageListener(
          "formautofill-storage-changed",
          onChanged
        );
        is(data.data, type, `Receive ${type} storage changed event`);
        resolve();
      }
    );
  });
}

function makeAddressComment({ primary, secondary }) {
  return JSON.stringify({
    primary,
    secondary,
    ariaLabel: primary + " " + secondary,
  });
}

// Compare the labels on the autocomplete menu items to the expected labels.
function checkMenuEntries(expectedValues, extraRows = 1) {
  let actualValues = getMenuEntries().labels;
  let expectedLength = expectedValues.length + extraRows;

  is(actualValues.length, expectedLength, " Checking length of expected menu");
  for (let i = 0; i < expectedValues.length; i++) {
    is(actualValues[i], expectedValues[i], " Checking menu entry #" + i);
  }
}

// Compare the comment on the autocomplete menu items to the expected comment.
// The profile field is not compared.
function checkMenuEntriesComment(expectedValues, extraRows = 1) {
  let actualValues = getMenuEntries().comments;
  let expectedLength = expectedValues.length + extraRows;

  is(actualValues.length, expectedLength, " Checking length of expected menu");
  for (let i = 0; i < expectedValues.length; i++) {
    const expectedValue = JSON.parse(expectedValues[i]);
    const actualValue = JSON.parse(actualValues[i]);
    for (const [key, value] of Object.entries(expectedValue)) {
      is(
        actualValue[key],
        value,
        ` Checking menu entry #${i}, ${key} should be the same`
      );
    }
  }
}

function invokeAsyncChromeTask(message, payload = {}) {
  info(`expecting the chrome task finished: ${message}`);
  return formFillChromeScript.sendQuery(message, payload);
}

async function addAddress(address) {
  await invokeAsyncChromeTask("FormAutofillTest:AddAddress", { address });
  await sleep();
}

async function removeAddress(guid) {
  return invokeAsyncChromeTask("FormAutofillTest:RemoveAddress", { guid });
}

async function updateAddress(guid, address) {
  return invokeAsyncChromeTask("FormAutofillTest:UpdateAddress", {
    address,
    guid,
  });
}

async function checkAddresses(expectedAddresses) {
  return invokeAsyncChromeTask("FormAutofillTest:CheckAddresses", {
    expectedAddresses,
  });
}

async function cleanUpAddresses() {
  return invokeAsyncChromeTask("FormAutofillTest:CleanUpAddresses");
}

async function addCreditCard(creditcard) {
  await invokeAsyncChromeTask("FormAutofillTest:AddCreditCard", { creditcard });
  await sleep();
}

async function removeCreditCard(guid) {
  return invokeAsyncChromeTask("FormAutofillTest:RemoveCreditCard", { guid });
}

async function checkCreditCards(expectedCreditCards) {
  return invokeAsyncChromeTask("FormAutofillTest:CheckCreditCards", {
    expectedCreditCards,
  });
}

async function cleanUpCreditCards() {
  return invokeAsyncChromeTask("FormAutofillTest:CleanUpCreditCards");
}

async function cleanUpStorage() {
  await cleanUpAddresses();
  await cleanUpCreditCards();
}

async function canTestOSKeyStoreLogin() {
  let { canTest } = await invokeAsyncChromeTask(
    "FormAutofillTest:CanTestOSKeyStoreLogin"
  );
  return canTest;
}

/**
 * This function should be used along with the `waitForOSKeyStoreLogin` API.
 * See the comment in `waitForOSKeyStoreLogin` for more details.
 */
async function waitForOSKeyStoreLoginTestSetupComplete() {
  if (
    !(await SpecialPowers.spawnChrome([], () => {
      // Need to re-import this because we're running in the parent.
      // eslint-disable-next-line no-shadow
      const { FormAutofillUtils } = ChromeUtils.importESModule(
        "resource://gre/modules/shared/FormAutofillUtils.sys.mjs"
      );

      return FormAutofillUtils.getOSAuthEnabled();
    }))
  ) {
    return;
  }

  await SimpleTest.promiseWaitForCondition(async () => {
    return await SpecialPowers.spawnChrome([], () => {
      const { OSKeyStoreTestUtils } = ChromeUtils.importESModule(
        "resource://testing-common/OSKeyStoreTestUtils.sys.mjs"
      );

      return Services.prefs.getStringPref(
        OSKeyStoreTestUtils.TEST_ONLY_REAUTH,
        ""
      );
    });
  });
}

/**
 * This API returns a promise that will be resolved when
 * `waitForOSKeyStoreLogin` in `OSKeyStoreTestUtils.sys.mjs` completes.
 * It is common to use it as follows:
 *   const promise = waitForOSKeyStoreLogin();
 *   triggerOSReauth();  // Code that triggers OS re-authentication
 *   await promise;
 *
 * However, the timing to switch to using test OS re-auth after calling this
 * function is asynchronous, which means triggering OS re-auth right after
 * this API may still activate the real OS re-auth popup. To avoid that, you
 * need to call `await waitForOSKeyStoreLoginTestSetupComplete()` before
 * triggering OS re-auth.
 */
async function waitForOSKeyStoreLogin(login = false) {
  // Need to fetch this from the parent in order for it to be correct.
  if (
    !(await SpecialPowers.spawnChrome([], () => {
      // Need to re-import this because we're running in the parent.
      // eslint-disable-next-line no-shadow
      const { FormAutofillUtils } = ChromeUtils.importESModule(
        "resource://gre/modules/shared/FormAutofillUtils.sys.mjs"
      );

      return FormAutofillUtils.getOSAuthEnabled();
    }))
  ) {
    return;
  }

  await invokeAsyncChromeTask("FormAutofillTest:OSKeyStoreLogin", { login });
}

function patchRecordCCNumber(record) {
  const ccNumberFmt = "••••" + record.cc["cc-number"].substr(-4);

  return {
    cc: Object.assign({}, record.cc, { ccNumberFmt }),
    expected: record.expected,
  };
}

// Utils for registerPopupShownListener(in satchel_common.js) that handles dropdown popup
// Please call "initPopupListener()" in your test and "await expectPopup()"
// if you want to wait for dropdown menu displayed.
function expectPopup() {
  info("expecting a popup");
  return new Promise(resolve => {
    expectingPopup = resolve;
  });
}

function notExpectPopup(ms = 500) {
  info("not expecting a popup");
  return new Promise((resolve, reject) => {
    expectingPopup = reject.bind(this, "Unexpected Popup");
    // TODO: We don't have an event to notify no popup showing, so wait for 500
    // ms (in default) to predict any unexpected popup showing.
    setTimeout(resolve, ms);
  });
}

function popupShownListener() {
  info("popup shown for test ");
  if (expectingPopup) {
    expectingPopup();
    expectingPopup = null;
  }
}

function initPopupListener() {
  registerPopupShownListener(popupShownListener);
}

async function triggerPopupAndHoverItem(fieldSelector, selectIndex) {
  const promise = expectPopup();
  const previouslyFocused =
    await focusAndWaitForFieldsIdentified(fieldSelector);
  // If the field is already focused, we need to send a key event to
  // open the popup
  if (previouslyFocused) {
    info(`triggerPopupAndHoverItem: send key to open the popup`);
    synthesizeKey("KEY_ArrowDown");
  }
  await promise;
  for (let i = 0; i <= selectIndex; i++) {
    synthesizeKey("KEY_ArrowDown");
  }
  await notifySelectedIndex(selectIndex);
}

function formAutoFillCommonSetup() {
  // Remove the /creditCard path segement when referenced from the 'creditCard' subdirectory.
  let chromeURL = SimpleTest.getTestFileURL(
    "formautofill_parent_utils.js"
  ).replace(/\/creditCard/, "");
  formFillChromeScript = SpecialPowers.loadChromeScript(chromeURL);
  formFillChromeScript.addMessageListener("onpopupshown", ({ results }) => {
    gLastAutoCompleteResults = results;
    if (gPopupShownListener) {
      gPopupShownListener({ results });
    }
  });

  add_setup(async () => {
    info(`expecting the storage setup`);
    await formFillChromeScript.sendQuery("setup");
  });

  SimpleTest.registerCleanupFunction(async () => {
    info(`expecting the storage cleanup`);
    await formFillChromeScript.sendQuery("cleanup");

    formFillChromeScript.destroy();
    expectingPopup = null;
  });

  document.addEventListener(
    "DOMContentLoaded",
    function () {
      defaultTextColor = window
        .getComputedStyle(document.querySelector("input"))
        .getPropertyValue("color");

      // This is needed for test_formautofill_preview_highlight.html to work properly
      let disabledInput = document.querySelector(`input[disabled]`);
      if (disabledInput) {
        defaultDisabledTextColor = window
          .getComputedStyle(disabledInput)
          .getPropertyValue("color");
      }
    },
    { once: true }
  );
}

/*
 * Extremely over-simplified detection of card type from card number just for
 * our tests. This is needed to test the aria-label of credit card menu entries.
 */
function getCCTypeName(creditCard) {
  return creditCard["cc-number"][0] == "4" ? "Visa" : "MasterCard";
}

formAutoFillCommonSetup();
