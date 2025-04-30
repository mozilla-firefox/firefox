"use strict";

const { ManageAddresses } = ChromeUtils.importESModule(
  "chrome://formautofill/content/manageDialog.mjs"
);

const { OSKeyStore } = ChromeUtils.importESModule(
  "resource://gre/modules/OSKeyStore.sys.mjs"
);
const { OSKeyStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/OSKeyStoreTestUtils.sys.mjs"
);

const { FormAutofillParent } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillParent.sys.mjs"
);

const { AutofillDoorhanger, AddressEditDoorhanger, AddressSaveDoorhanger } =
  ChromeUtils.importESModule(
    "resource://autofill/FormAutofillPrompter.sys.mjs"
  );

const { FormAutofillNameUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/shared/FormAutofillNameUtils.sys.mjs"
);

const { VALID_ADDRESS_FIELDS, VALID_CREDIT_CARD_FIELDS } =
  ChromeUtils.importESModule(
    "resource://autofill/FormAutofillStorageBase.sys.mjs"
  );

const { FormAutofillUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/shared/FormAutofillUtils.sys.mjs"
);

const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);

let { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

// Always pretend OS Auth is enabled in this dir.
if (
  gTestPath.includes("browser/creditCard") &&
  OSKeyStoreTestUtils.canTestOSKeyStoreLogin() &&
  OSKeyStore.canReauth()
) {
  info("Stubbing out getOSAuthEnabled so it always returns true");
  sinon.stub(FormAutofillUtils, "getOSAuthEnabled").returns(true);
  registerCleanupFunction(() => {
    sinon.restore();
  });
}

const MANAGE_ADDRESSES_DIALOG_URL =
  "chrome://formautofill/content/manageAddresses.xhtml";
const MANAGE_CREDIT_CARDS_DIALOG_URL =
  "chrome://formautofill/content/manageCreditCards.xhtml";
const EDIT_ADDRESS_DIALOG_URL =
  "chrome://formautofill/content/editAddress.xhtml";
const EDIT_CREDIT_CARD_DIALOG_URL =
  "chrome://formautofill/content/editCreditCard.xhtml";
const PRIVACY_PREF_URL = "about:preferences#privacy";

const HTTP_TEST_PATH = "/browser/browser/extensions/formautofill/test/browser/";
const BASE_URL = "http://mochi.test:8888" + HTTP_TEST_PATH;
const BASE_URL_HTTPS = "https://mochi.test" + HTTP_TEST_PATH;
const CROSS_ORIGIN_BASE_URL = "https://example.org" + HTTP_TEST_PATH;
const CROSS_ORIGIN_2_BASE_URL = "https://example.com" + HTTP_TEST_PATH;
const FORM_URL = BASE_URL + "autocomplete_basic.html";
const ADDRESS_FORM_URL =
  "https://example.org" +
  HTTP_TEST_PATH +
  "address/autocomplete_address_basic.html";
const ADDRESS_FORM_WITHOUT_AUTOCOMPLETE_URL =
  "https://example.org" +
  HTTP_TEST_PATH +
  "address/without_autocomplete_address_basic.html";
const ADDRESS_FORM_WITH_PAGE_NAVIGATION_BUTTONS =
  "https://example.org" +
  HTTP_TEST_PATH +
  "address/capture_address_on_page_navigation.html";
const FORM_IFRAME_SANDBOXED_URL =
  "https://example.org" + HTTP_TEST_PATH + "autocomplete_iframe_sandboxed.html";
const FORMS_WITH_DYNAMIC_FORM_CHANGE =
  "https://example.org" + HTTP_TEST_PATH + "dynamic_forms.html";
const FORMS_REPLACING_ALL_FIELDS_ON_INPUT =
  "https://example.org" + HTTP_TEST_PATH + "dynamic_forms.html";
const FORM_WITH_USER_INITIATED_FORM_CHANGE =
  "https://example.org" +
  HTTP_TEST_PATH +
  "dynamic_form_changing_on_user_interaction.html";
const FORMLESS_FIELDS_WITH_DYNAMIC_FORM_CHANGE_AFTER_NODE_MUTATIONS =
  "https://example.org" +
  HTTP_TEST_PATH +
  "dynamic_formless_fields_updated_due_to_node_mutations.html";
const FORMLESS_FIELDS_WITH_DYNAMIC_FORM_CHANGE_AFTER_VISIBILITY_STATE_CHANGE =
  "https://example.org" +
  HTTP_TEST_PATH +
  "dynamic_formless_fields_updated_due_to_visiblity_state_change.html";
const CREDITCARD_FORM_URL =
  "https://example.org" +
  HTTP_TEST_PATH +
  "creditCard/autocomplete_creditcard_basic.html";
const CREDITCARD_FORM_IFRAME_URL =
  "https://example.org" +
  HTTP_TEST_PATH +
  "creditCard/autocomplete_creditcard_iframe.html";
const CREDITCARD_FORM_COMBINED_EXPIRY_URL =
  "https://example.org" +
  HTTP_TEST_PATH +
  "creditCard/autocomplete_creditcard_cc_exp_field.html";
const CREDITCARD_FORM_WITHOUT_AUTOCOMPLETE_URL =
  "https://example.org" +
  HTTP_TEST_PATH +
  "creditCard/without_autocomplete_creditcard_basic.html";
const CREDITCARD_FORM_WITH_PAGE_NAVIGATION_BUTTONS =
  "https://example.org" +
  HTTP_TEST_PATH +
  "creditCard/capture_creditCard_on_page_navigation.html";
const EMPTY_URL = "https://example.org" + HTTP_TEST_PATH + "empty.html";

const TOP_LEVEL_HOST = "https://example.com";
const TOP_LEVEL_URL = TOP_LEVEL_HOST + HTTP_TEST_PATH;
const SAME_SITE_URL = "https://test1.example.com" + HTTP_TEST_PATH;
const CROSS_ORIGIN_URL = "https://example.net" + HTTP_TEST_PATH;
const CROSS_ORIGIN_2_URL = "https://example.org" + HTTP_TEST_PATH;

const ENABLED_AUTOFILL_ADDRESSES_PREF =
  "extensions.formautofill.addresses.enabled";
const ENABLED_AUTOFILL_ADDRESSES_CAPTURE_PREF =
  "extensions.formautofill.addresses.capture.enabled";
const AUTOFILL_ADDRESSES_AVAILABLE_PREF =
  "extensions.formautofill.addresses.supported";
const ENABLED_AUTOFILL_ADDRESSES_SUPPORTED_COUNTRIES_PREF =
  "extensions.formautofill.addresses.supportedCountries";
const AUTOFILL_CREDITCARDS_AVAILABLE_PREF =
  "extensions.formautofill.creditCards.supported";
const ENABLED_AUTOFILL_CREDITCARDS_PREF =
  "extensions.formautofill.creditCards.enabled";
const SUPPORTED_COUNTRIES_PREF = "extensions.formautofill.supportedCountries";
const SYNC_USERNAME_PREF = "services.sync.username";
const SYNC_ADDRESSES_PREF = "services.sync.engine.addresses";
const SYNC_CREDITCARDS_PREF = "services.sync.engine.creditcards";
const SYNC_CREDITCARDS_AVAILABLE_PREF =
  "services.sync.engine.creditcards.available";

// For iframe autofill tests
const SAME_ORIGIN_ALL_FIELDS =
  TOP_LEVEL_URL + "../fixtures/autocomplete_cc_mandatory_embeded.html";
const SAME_ORIGIN_CC_NUMBER =
  TOP_LEVEL_URL + "../fixtures/autocomplete_cc_number_embeded.html";
const SAME_ORIGIN_CC_NAME =
  TOP_LEVEL_URL + "../fixtures/autocomplete_cc_name_embeded.html";
const SAME_ORIGIN_CC_EXP =
  TOP_LEVEL_URL + "../fixtures/autocomplete_cc_exp_embeded.html";
const SAME_ORIGIN_CC_TYPE =
  TOP_LEVEL_URL + "../fixtures/autocomplete_cc_type_embeded.html";

const SAME_SITE_ALL_FIELDS =
  SAME_SITE_URL + "../fixtures/autocomplete_cc_mandatory_embeded.html";
const SAME_SITE_CC_NUMBER =
  SAME_SITE_URL + "../fixtures/autocomplete_cc_number_embeded.html";
const SAME_SITE_CC_NAME =
  SAME_SITE_URL + "../fixtures/autocomplete_cc_name_embeded.html";
const SAME_SITE_CC_EXP =
  SAME_SITE_URL + "../fixtures/autocomplete_cc_exp_embeded.html";
const SAME_SITE_CC_TYPE =
  SAME_SITE_URL + "../fixtures/autocomplete_cc_type_embeded.html";

const CROSS_ORIGIN_ALL_FIELDS =
  CROSS_ORIGIN_URL + "../fixtures/autocomplete_cc_mandatory_embeded.html";
const CROSS_ORIGIN_CC_NUMBER =
  CROSS_ORIGIN_URL + "../fixtures/autocomplete_cc_number_embeded.html";
const CROSS_ORIGIN_CC_NAME =
  CROSS_ORIGIN_URL + "../fixtures/autocomplete_cc_name_embeded.html";
const CROSS_ORIGIN_CC_EXP =
  CROSS_ORIGIN_URL + "../fixtures/autocomplete_cc_exp_embeded.html";
const CROSS_ORIGIN_CC_TYPE =
  CROSS_ORIGIN_URL + "../fixtures/autocomplete_cc_type_embeded.html";

const CROSS_ORIGIN_2_ALL_FIELDS =
  CROSS_ORIGIN_2_URL +
  "../fixtures/" +
  "autocomplete_cc_mandatory_embeded.html";
const CROSS_ORIGIN_2_CC_NUMBER =
  CROSS_ORIGIN_2_URL + "../fixtures/autocomplete_cc_number_embeded.html";
const CROSS_ORIGIN_2_CC_NAME =
  CROSS_ORIGIN_2_URL + "../fixtures/autocomplete_cc_name_embeded.html";
const CROSS_ORIGIN_2_CC_EXP =
  CROSS_ORIGIN_2_URL + "../fixtures/autocomplete_cc_exp_embeded.html";
const CROSS_ORIGIN_2_CC_TYPE =
  CROSS_ORIGIN_2_URL + "../fixtures/autocomplete_cc_type_embeded.html";

// Test profiles
const TEST_ADDRESS_1 = {
  "given-name": "John",
  "additional-name": "R.",
  "family-name": "Smith",
  organization: "World Wide Web Consortium",
  "street-address": "32 Vassar Street\nMIT Room 32-G524",
  "address-level2": "Cambridge",
  "address-level1": "MA",
  "postal-code": "02139",
  country: "US",
  tel: "+16172535702",
  email: "timbl@w3.org",
};

const TEST_ADDRESS_2 = {
  "given-name": "Anonymouse",
  "street-address": "Some Address",
  country: "US",
};

const TEST_ADDRESS_3 = {
  "given-name": "John",
  "street-address": "Other Address",
  "postal-code": "12345",
};

const TEST_ADDRESS_4 = {
  "given-name": "Timothy",
  "family-name": "Berners-Lee",
  organization: "World Wide Web Consortium",
  "street-address": "32 Vassar Street\nMIT Room 32-G524",
  country: "US",
  email: "timbl@w3.org",
};

// TODO: Number of field less than AUTOFILL_FIELDS_THRESHOLD
//       need to confirm whether this is intentional
const TEST_ADDRESS_5 = {
  tel: "+16172535702",
};

const TEST_ADDRESS_CA_1 = {
  "given-name": "John",
  "additional-name": "R.",
  "family-name": "Smith",
  organization: "Mozilla",
  "street-address": "163 W Hastings\nSuite 209",
  "address-level2": "Vancouver",
  "address-level1": "BC",
  "postal-code": "V6B 1H5",
  country: "CA",
  tel: "+17787851540",
  email: "timbl@w3.org",
};

const TEST_ADDRESS_DE_1 = {
  "given-name": "John",
  "additional-name": "R.",
  "family-name": "Smith",
  organization: "Mozilla",
  "street-address":
    "Geb\u00E4ude 3, 4. Obergeschoss\nSchlesische Stra\u00DFe 27",
  "address-level2": "Berlin",
  "postal-code": "10997",
  country: "DE",
  tel: "+4930983333000",
  email: "timbl@w3.org",
};

const TEST_ADDRESS_IE_1 = {
  "given-name": "Bob",
  "additional-name": "Z.",
  "family-name": "Builder",
  organization: "Best Co.",
  "street-address": "123 Kilkenny St.",
  "address-level3": "Some Townland",
  "address-level2": "Dublin",
  "address-level1": "D",
  "postal-code": "A65 F4E2",
  country: "IE",
  tel: "+13534564947391",
  email: "ie@example.com",
};

const TEST_CREDIT_CARD_1 = {
  "cc-name": "John Doe",
  "cc-number": "4111111111111111",
  "cc-exp-month": 4,
  "cc-exp-year": new Date().getFullYear(),
};

const TEST_CREDIT_CARD_2 = {
  "cc-name": "Timothy Berners-Lee",
  "cc-number": "4929001587121045",
  "cc-exp-month": 12,
  "cc-exp-year": new Date().getFullYear() + 10,
};

const TEST_CREDIT_CARD_3 = {
  "cc-number": "5103059495477870",
  "cc-exp-month": 1,
  "cc-exp-year": 2000,
};

const TEST_CREDIT_CARD_4 = {
  "cc-number": "5105105105105100",
};

const TEST_CREDIT_CARD_5 = {
  "cc-name": "Chris P. Bacon",
  "cc-number": "4012888888881881",
};

const MAIN_BUTTON = "button";
const SECONDARY_BUTTON = "secondaryButton";
const MENU_BUTTON = "menubutton";
const EDIT_ADDRESS_BUTTON = "edit";
const ADDRESS_MENU_BUTTON = "addressMenuButton";
const ADDRESS_MENU_LEARN_MORE = "learnMore";
const ADDRESS_MENU_PREFENCE = "preference";

/**
 * Collection of timeouts that are used to ensure something should not happen.
 */
const TIMEOUT_ENSURE_PROFILE_NOT_SAVED = 1000;
const TIMEOUT_ENSURE_CC_DIALOG_NOT_CLOSED = 500;
const TIMEOUT_ENSURE_AUTOCOMPLETE_NOT_SHOWN = 1000;
const TIMEOUT_ENSURE_DOORHANGER_NOT_SHOWN = 1000;

async function ensureCreditCardDialogNotClosed(win) {
  const unloadHandler = () => {
    ok(false, "Credit card dialog shouldn't be closed");
  };
  win.addEventListener("unload", unloadHandler);
  await new Promise(resolve =>
    setTimeout(resolve, TIMEOUT_ENSURE_CC_DIALOG_NOT_CLOSED)
  );
  win.removeEventListener("unload", unloadHandler);
}

function getDisplayedPopupItems(
  browser,
  selector = ".autocomplete-richlistitem"
) {
  info("getDisplayedPopupItems");
  const {
    autoCompletePopup: { richlistbox: itemsBox },
  } = browser;
  const listItemElems = itemsBox.querySelectorAll(selector);

  return [...listItemElems].filter(
    item => item.getAttribute("collapsed") != "true"
  );
}

async function sleep(ms = 500) {
  await new Promise(resolve => setTimeout(resolve, ms));
}

async function ensureNoAutocompletePopup(browser) {
  await new Promise(resolve =>
    setTimeout(resolve, TIMEOUT_ENSURE_AUTOCOMPLETE_NOT_SHOWN)
  );
  const items = getDisplayedPopupItems(browser);
  ok(!items.length, "Should not find autocomplete items");
}

async function ensureNoDoorhanger() {
  await new Promise(resolve =>
    setTimeout(resolve, TIMEOUT_ENSURE_DOORHANGER_NOT_SHOWN)
  );

  let notifications = PopupNotifications.panel.childNodes;
  ok(!notifications.length, "Should not find a doorhanger");
}

/**
 * Wait for "formautofill-storage-changed" events
 *
 * @param {Array<string>} eventTypes
 *        eventType must be one of the following:
 *        `add`, `update`, `remove`, `notifyUsed`, `removeAll`, `reconcile`
 *
 * @returns {Promise} resolves when all events are received
 */
async function waitForStorageChangedEvents(...eventTypes) {
  return Promise.all(
    eventTypes.map(type =>
      TestUtils.topicObserved(
        "formautofill-storage-changed",
        (subject, data) => {
          return data == type;
        }
      )
    )
  );
}

/**
 * Sets up a promise that resolves when the FormAutofillParent sends out a notification
 * that the field detection processes have completed in all FormAutofill children.
 *
 * @returns {Promise}
 */
async function getFieldDetectionCompletedPromiseResolver() {
  let fieldDetectionCompletedPromiseResolver;
  const fieldDetectionCompletedObserver = {
    fieldDetectionCompleted() {
      info(`All fields detected.`);
      fieldDetectionCompletedPromiseResolver();
      FormAutofillParent.removeMessageObserver(fieldDetectionCompletedObserver);
    },
  };

  return new Promise(resolve => {
    fieldDetectionCompletedPromiseResolver = resolve;
    FormAutofillParent.addMessageObserver(fieldDetectionCompletedObserver);
  });
}

/**
 * Wait until the element found matches the expected autofill value
 *
 * @param {object} target
 *        The target in which to run the task.
 * @param {string} selector
 *        A selector used to query the element.
 * @param {string} value
 *        The expected autofilling value for the element
 */
async function waitForAutofill(target, selector, value) {
  await SpecialPowers.spawn(
    target,
    [selector, value],
    async function (selector, val) {
      await ContentTaskUtils.waitForCondition(() => {
        let element = content.document.querySelector(selector);
        return element.value == val;
      }, "Autofill never fills");
    }
  );
}

/**
 * Waits for the subDialog to be loaded
 *
 * @param {Window} win The window of the dialog
 * @param {string} dialogUrl The url of the dialog that we are waiting for
 *
 * @returns {Promise} resolves when the sub dialog is loaded
 */
function waitForSubDialogLoad(win, dialogUrl) {
  return new Promise(resolve => {
    win.gSubDialog._dialogStack.addEventListener(
      "dialogopen",
      async function dialogopen(evt) {
        let cwin = evt.detail.dialog._frame.contentWindow;
        if (cwin.location != dialogUrl) {
          return;
        }
        content.gSubDialog._dialogStack.removeEventListener(
          "dialogopen",
          dialogopen
        );

        resolve(cwin);
      }
    );
  });
}

/**
 * Use this function when you want to update the value of elements in
 * a form and then submit the form. This function makes sure the form
 * is "identified" (`identifyAutofillFields` is called) before submitting
 * the form.
 * This is guaranteed by first focusing on an element in the form to trigger
 * the 'FormAutofill:FieldsIdentified' message.
 *
 * @param {object} target
 *        The target in which to run the task.
 * @param {object} args
 * @param {string} args.focusSelector
 *        A selector used to query the element to be focused
 * @param {string} args.formId
 *        The id of the form to be updated. This function uses "form" if
 *        this argument is not present
 * @param {string} args.formSelector
 *        A selector used to query the form element
 * @param {object} args.newValues
 *        Elements to be updated. Key is the element selector, value is the
 *        new value of the element.
 *
 * @param {boolean} submit
 *        Set to true to submit the form after the task is done, false otherwise.
 */
async function focusUpdateSubmitForm(target, args, submit = true) {
  let fieldsIdentifiedPromiseResolver;
  let fieldsIdentifiedObserver = {
    fieldsIdentified() {
      FormAutofillParent.removeMessageObserver(fieldsIdentifiedObserver);
      fieldsIdentifiedPromiseResolver();
    },
  };

  let fieldsIdentifiedPromise = new Promise(resolve => {
    fieldsIdentifiedPromiseResolver = resolve;
    FormAutofillParent.addMessageObserver(fieldsIdentifiedObserver);
  });

  let alreadyFocused = await SpecialPowers.spawn(target, [args], obj => {
    let focused = false;

    let form;
    if (obj.formSelector) {
      form = content.document.querySelector(obj.formSelector);
    } else {
      form = content.document.getElementById(obj.formId ?? "form");
    }
    form ||= content.document;

    let element = form.querySelector(obj.focusSelector);
    if (element != content.document.activeElement) {
      info(`focus on element (id=${element.id})`);
      element.focus();
    } else {
      focused = true;
    }

    for (const [selector, value] of Object.entries(obj.newValues)) {
      element = form.querySelector(selector);
      if (
        content.HTMLInputElement.isInstance(element) ||
        content.HTMLTextAreaElement.isInstance(element)
      ) {
        element.setUserInput(value);
      } else if (
        content.HTMLSelectElement.isInstance(element) &&
        Array.isArray(value)
      ) {
        element.multiple = true;
        [...element.options].forEach(option => {
          option.selected = value.includes(option.value);
        });
      } else {
        element.value = value;
      }
    }

    return focused;
  });

  if (alreadyFocused) {
    // If the element is already focused, assume the FieldsIdentified message
    // was sent before.
    FormAutofillParent.removeMessageObserver(fieldsIdentifiedObserver);
    fieldsIdentifiedPromiseResolver();
  }

  await fieldsIdentifiedPromise;

  if (submit) {
    await SpecialPowers.spawn(target, [args], obj => {
      let form;
      if (obj.formSelector) {
        form = content.document.querySelector(obj.formSelector);
      } else {
        form = content.document.getElementById(obj.formId ?? "form");
      }
      form ||= content.document;
      info(`submit form (id=${form.id})`);
      form.querySelector("input[type=submit]").click();
    });
  }
}

async function focusAndWaitForFieldsIdentified(browserOrContext, selector) {
  info("expecting the target input being focused and identified");
  /* eslint no-shadow: ["error", { "allow": ["selector", "previouslyFocused", "previouslyIdentified"] }] */

  // If the input is previously focused, no more notifications will be
  // sent as the notification goes along with focus event.
  let fieldsIdentifiedPromiseResolver;
  let fieldsIdentifiedObserver = {
    fieldsIdentified() {
      fieldsIdentifiedPromiseResolver();
    },
  };

  let fieldsIdentifiedPromise = new Promise(resolve => {
    fieldsIdentifiedPromiseResolver = resolve;
    FormAutofillParent.addMessageObserver(fieldsIdentifiedObserver);
  });

  const { previouslyFocused, previouslyIdentified } = await SpecialPowers.spawn(
    browserOrContext,
    [selector],
    async function (selector) {
      const { FormLikeFactory } = ChromeUtils.importESModule(
        "resource://gre/modules/FormLikeFactory.sys.mjs"
      );
      const input = content.document.querySelector(selector);
      const rootElement = FormLikeFactory.findRootForField(input);
      const previouslyFocused = content.document.activeElement == input;
      const previouslyIdentified = rootElement.hasAttribute(
        "test-formautofill-identified"
      );

      input.focus();

      return { previouslyFocused, previouslyIdentified };
    }
  );

  // Only wait for the fields identified notification if the
  // focus was not previously assigned to the input.
  if (previouslyFocused) {
    fieldsIdentifiedPromiseResolver();
  } else {
    info("!previouslyFocused");
  }

  // If a browsing context was supplied, focus its parent frame as well.
  if (
    BrowsingContext.isInstance(browserOrContext) &&
    browserOrContext.parent != browserOrContext
  ) {
    await SpecialPowers.spawn(
      browserOrContext.parent,
      [browserOrContext],
      async function (browsingContext) {
        browsingContext.embedderElement.focus();
      }
    );
  }

  if (previouslyIdentified) {
    info("previouslyIdentified");
    FormAutofillParent.removeMessageObserver(fieldsIdentifiedObserver);
    return previouslyFocused;
  }

  // Wait 500ms to ensure that "markAsAutofillField" is completely finished.
  await fieldsIdentifiedPromise;
  info("FieldsIdentified");
  FormAutofillParent.removeMessageObserver(fieldsIdentifiedObserver);

  await sleep();
  await SpecialPowers.spawn(browserOrContext, [], async function () {
    const { FormLikeFactory } = ChromeUtils.importESModule(
      "resource://gre/modules/FormLikeFactory.sys.mjs"
    );
    FormLikeFactory.findRootForField(
      content.document.activeElement
    ).setAttribute("test-formautofill-identified", "true");
  });

  return previouslyFocused;
}

/**
 * Run the task and wait until the autocomplete popup is opened.
 *
 * @param {object} browser A xul:browser.
 * @param {Function} taskFunction Task that will trigger the autocomplete popup
 */
async function runAndWaitForAutocompletePopupOpen(browser, taskFunction) {
  info("runAndWaitForAutocompletePopupOpen");
  let popupShown = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "shown"
  );

  // Run the task will open the autocomplete popup
  await taskFunction();

  await popupShown;
}

async function waitForPopupEnabled(browser) {
  const {
    autoCompletePopup: { richlistbox: itemsBox },
  } = browser;
  info("Wait for list elements to become enabled");
  await BrowserTestUtils.waitForMutationCondition(
    itemsBox,
    { subtree: true, attributes: true, attributeFilter: ["disabled"] },
    () => !itemsBox.querySelectorAll(".autocomplete-richlistitem")[0].disabled
  );
}

// Wait for the popup state change notification to happen.
async function waitForAutoCompletePopupOpen(browser, taskFunction) {
  const popupShown = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "shown"
  );

  if (taskFunction) {
    await taskFunction();
  }

  return popupShown;
}

async function openPopupOn(browser, selector) {
  await SimpleTest.promiseFocus(browser);

  await runAndWaitForAutocompletePopupOpen(browser, async () => {
    const previouslyFocused = await focusAndWaitForFieldsIdentified(
      browser,
      selector
    );
    // If the field is already focused, we need to send a key event to
    // open the popup
    if (previouslyFocused || !selector.includes("cc-")) {
      info(`openPopupOn: before VK_DOWN on ${selector}`);
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    }
  });
}

async function openPopupOnSubframe(browser, frameBrowsingContext, selector) {
  await SimpleTest.promiseFocus(browser);

  await runAndWaitForAutocompletePopupOpen(browser, async () => {
    const previouslyFocused = await focusAndWaitForFieldsIdentified(
      frameBrowsingContext,
      selector
    );
    // If the field is already focused, we need to send a key event to
    // open the popup
    if (previouslyFocused || !selector.includes("cc-")) {
      info(`openPopupOnSubframe: before VK_DOWN on ${selector}`);
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, frameBrowsingContext);
    }
  });
}

async function closePopup(browser) {
  // Return if the popup isn't open.
  if (!browser.autoCompletePopup.popupOpen) {
    return;
  }

  let popupClosePromise = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "hidden"
  );

  await SpecialPowers.spawn(browser, [], async function () {
    content.document.activeElement.blur();
  });

  await popupClosePromise;
}

async function closePopupForSubframe(browser, frameBrowsingContext) {
  // Return if the popup isn't open.
  if (!browser.autoCompletePopup.popupOpen) {
    return;
  }

  const popupClosePromise = BrowserTestUtils.waitForPopupEvent(
    browser.autoCompletePopup,
    "hidden"
  );

  await SpecialPowers.spawn(frameBrowsingContext, [], async function () {
    content.document.activeElement.blur();
  });

  await popupClosePromise;
}

function emulateMessageToBrowser(name, data) {
  let actor =
    gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.getActor(
      "FormAutofill"
    );

  return actor.receiveMessage({ name, data });
}

function getRecords(data) {
  info(`expecting record retrievals: ${data.collectionName}`);
  return emulateMessageToBrowser("FormAutofill:GetRecords", data).then(
    result => result.records
  );
}

function getAddresses() {
  return getRecords({ collectionName: "addresses" });
}

async function ensureNoAddressSaved() {
  await new Promise(resolve =>
    setTimeout(resolve, TIMEOUT_ENSURE_PROFILE_NOT_SAVED)
  );
  const addresses = await getAddresses();
  is(addresses.length, 0, "No address was saved");
}

function getCreditCards() {
  return getRecords({ collectionName: "creditCards" });
}

async function saveAddress(address) {
  info("expecting address saved");
  let observePromise = TestUtils.topicObserved("formautofill-storage-changed");
  await emulateMessageToBrowser("FormAutofill:SaveAddress", { address });
  await observePromise;
}

async function saveCreditCard(creditcard) {
  info("expecting credit card saved");
  let creditcardClone = Object.assign({}, creditcard);
  let observePromise = TestUtils.topicObserved("formautofill-storage-changed");
  await emulateMessageToBrowser("FormAutofill:SaveCreditCard", {
    creditcard: creditcardClone,
  });
  await observePromise;
}

async function removeAddresses(guids) {
  info("expecting address removed");
  let observePromise = TestUtils.topicObserved("formautofill-storage-changed");
  await emulateMessageToBrowser("FormAutofill:RemoveAddresses", { guids });
  await observePromise;
}

async function removeCreditCards(guids) {
  info("expecting credit card removed");
  let observePromise = TestUtils.topicObserved("formautofill-storage-changed");
  await emulateMessageToBrowser("FormAutofill:RemoveCreditCards", { guids });
  await observePromise;
}

function getNotification(index = 0) {
  let notifications = PopupNotifications.panel.childNodes;
  ok(!!notifications.length, "at least one notification displayed");
  ok(true, notifications.length + " notification(s)");
  return notifications[index];
}

function waitForPopupShown() {
  return BrowserTestUtils.waitForEvent(PopupNotifications.panel, "popupshown");
}

/**
 * Clicks the popup notification button and wait for popup hidden.
 *
 * @param {string} buttonType The button type in popup notification.
 * @param {number} index The action's index in menu list.
 */
async function clickDoorhangerButton(buttonType, index = 0) {
  let popuphidden = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popuphidden"
  );

  let button;
  if (buttonType == MAIN_BUTTON || buttonType == SECONDARY_BUTTON) {
    button = getNotification()[buttonType];
  } else if (buttonType == MENU_BUTTON) {
    // Click the dropmarker arrow and wait for the menu to show up.
    info("expecting notification menu button present");
    await BrowserTestUtils.waitForCondition(() => getNotification().menubutton);
    await sleep(2000); // menubutton needs extra time for binding
    let notification = getNotification();

    ok(notification.menubutton, "notification menupopup displayed");
    let dropdownPromise = BrowserTestUtils.waitForEvent(
      notification.menupopup,
      "popupshown"
    );

    notification.menubutton.click();
    info("expecting notification popup show up");
    await dropdownPromise;

    button = notification.querySelectorAll("menuitem")[index];
  }

  button.click();
  info("expecting notification popup hidden");
  await popuphidden;
}

async function clickAddressDoorhangerButton(buttonType, subType) {
  const notification = getNotification();
  let button;
  if (buttonType == EDIT_ADDRESS_BUTTON) {
    button = AddressSaveDoorhanger.editButton(notification);
  } else if (buttonType == ADDRESS_MENU_BUTTON) {
    const menu = AutofillDoorhanger.menuButton(notification);
    const menupopup = AutofillDoorhanger.menuPopup(notification);
    const promise = BrowserTestUtils.waitForEvent(menupopup, "popupshown");
    menu.click();
    await promise;
    if (subType == ADDRESS_MENU_PREFENCE) {
      button = AutofillDoorhanger.preferenceButton(notification);
    } else if (subType == ADDRESS_MENU_LEARN_MORE) {
      button = AutofillDoorhanger.learnMoreButton(notification);
    }
  } else {
    await clickDoorhangerButton(buttonType);
    return;
  }

  EventUtils.synthesizeMouseAtCenter(button, {});
}

function getDoorhangerCheckbox() {
  return getNotification().checkbox;
}

function getDoorhangerButton(button) {
  return getNotification()[button];
}

/**
 * Removes all addresses and credit cards from storage.
 *
 * **NOTE: If you add or update a record in a test, then you must wait for the
 * respective storage event to fire before calling this function.**
 * This is because this function doesn't guarantee that a record that
 * is about to be added or update will also be removed,
 * since the add or update is triggered by an asynchronous call.
 *
 * @see waitForStorageChangedEvents for more details about storage events to wait for
 */
async function removeAllRecords() {
  let addresses = await getAddresses();
  if (addresses.length) {
    await removeAddresses(addresses.map(address => address.guid));
  }
  let creditCards = await getCreditCards();
  if (creditCards.length) {
    await removeCreditCards(creditCards.map(cc => cc.guid));
  }
}

async function waitForFocusAndFormReady(win) {
  return Promise.all([
    new Promise(resolve => waitForFocus(resolve, win)),
    BrowserTestUtils.waitForEvent(win, "FormReadyForTests"),
  ]);
}

// Verify that the warning in the autocomplete popup has the expected text.
async function expectWarningText(browser, expectedText) {
  const {
    autoCompletePopup: { richlistbox: itemsBox },
  } = browser;
  let warningBox = itemsBox.querySelector(".ac-status");
  ok(warningBox.parentNode.disabled, "Got warning box and is disabled");

  await BrowserTestUtils.waitForMutationCondition(
    warningBox,
    { childList: true, characterData: true },
    () => warningBox.textContent == expectedText
  );
  ok(true, `Got expected warning text: ${expectedText}`);
}

async function testDialog(url, testFn, arg = undefined) {
  // Skip this step for test cards that lack an encrypted
  // number since they will fail to decrypt.
  if (
    url == EDIT_CREDIT_CARD_DIALOG_URL &&
    arg &&
    arg.record &&
    arg.record["cc-number-encrypted"]
  ) {
    arg.record = Object.assign({}, arg.record, {
      "cc-number": await OSKeyStore.decrypt(arg.record["cc-number-encrypted"]),
    });
  }
  const win = window.openDialog(url, null, "width=600,height=600", {
    ...arg,
    l10nStrings: ManageAddresses.getAddressL10nStrings(),
  });
  await waitForFocusAndFormReady(win);
  const unloadPromise = BrowserTestUtils.waitForEvent(win, "unload");
  await testFn(win);
  return unloadPromise;
}

/**
 * Initializes the test storage for a task.
 *
 * @param {...object} items Can either be credit card or address objects
 */
async function setStorage(...items) {
  for (let item of items) {
    if (item["cc-number"]) {
      await saveCreditCard(item);
    } else {
      await saveAddress(item);
    }
  }
}

function verifySectionAutofillResult(section, result, expectedSection) {
  const fieldDetails = section.fieldDetails;
  const expectedFieldDetails = expectedSection.fields;

  fieldDetails.forEach((field, fieldIndex) => {
    const expected = expectedFieldDetails[fieldIndex];
    Assert.equal(
      result.get(field.elementId).value,
      expected.autofill ?? "",
      `Autofilled value for element(identifier:${field.identifier}, field name:${field.fieldName}) should be equal`
    );
  });
}

function getSelectorFromFieldDetail(fieldDetail) {
  // identifier is set with `${element.id}/${element.name}`;
  const id = fieldDetail.identifier.split("/")[0];
  return `input#${id}, select#${id}, textarea#${id}`;
}

/**
 * Discards all recorded Glean telemetry in parent and child processes
 * and resets FOG and the Glean SDK.
 *
 * @param {boolean} onlyInParent Whether we only discard the metric data in the parent process
 *
 * Since the current method Services.fog.testResetFOG only discards metrics recorded in the parent process,
 * we would like to keep this option in our method as well.
 */
async function clearGleanTelemetry(onlyInParent = false) {
  if (!onlyInParent) {
    await Services.fog.testFlushAllChildren();
  }
  Services.fog.testResetFOG();
}

function fillEditDoorhanger(record) {
  const notification = getNotification();

  for (const [key, value] of Object.entries(record)) {
    const id = AddressEditDoorhanger.getInputId(key);
    const element = notification.querySelector(`#${id}`);
    element.value = value;
  }
}

// TODO: This function should be removed. We should make normalizeFields in
// FormAutofillStorageBase.sys.mjs static and using it directly
function normalizeAddressFields(record) {
  let normalized = { ...record };

  if (normalized.name != undefined) {
    let nameParts = FormAutofillNameUtils.splitName(normalized.name);
    normalized["given-name"] = nameParts.given;
    normalized["additional-name"] = nameParts.middle;
    normalized["family-name"] = nameParts.family;
    delete normalized.name;
  }
  return normalized;
}

async function verifyConfirmationHint(
  browser,
  forceClose,
  anchorID = "identity-icon-box"
) {
  let hintElem = browser.ownerGlobal.ConfirmationHint._panel;
  let popupshown = BrowserTestUtils.waitForPopupEvent(hintElem, "shown");
  let popuphidden;

  if (!forceClose) {
    popuphidden = BrowserTestUtils.waitForPopupEvent(hintElem, "hidden");
  }

  await popupshown;
  try {
    Assert.equal(hintElem.state, "open", "hint popup is open");
    Assert.ok(
      BrowserTestUtils.isVisible(hintElem.anchorNode),
      "hint anchorNode is visible"
    );
    Assert.equal(
      hintElem.anchorNode.id,
      anchorID,
      "Hint should be anchored on the expected notification icon"
    );
    info("verifyConfirmationHint, hint is shown and has its anchorNode");
    if (forceClose) {
      await closePopup(hintElem);
    } else {
      info("verifyConfirmationHint, assertion ok, wait for poopuphidden");
      await popuphidden;
      info("verifyConfirmationHint, hintElem popup is hidden");
    }
  } catch (ex) {
    Assert.ok(false, "Confirmation hint not shown: " + ex.message);
  } finally {
    info("verifyConfirmationHint promise finalized");
  }
}

async function showAddressDoorhanger(browser, values = null) {
  const defaultValues = {
    "#given-name": "John",
    "#family-name": "Doe",
    "#organization": "Mozilla",
    "#street-address": "123 Sesame Street",
  };

  const onPopupShown = waitForPopupShown();
  const promise = BrowserTestUtils.browserLoaded(browser);
  await focusUpdateSubmitForm(browser, {
    focusSelector: "#given-name",
    newValues: values ?? defaultValues,
  });
  await promise;
  await onPopupShown;
}

async function findContext(browser, selector) {
  const contexts =
    browser.browsingContext.top.getAllBrowsingContextsInSubtree();
  for (const context of contexts) {
    const find = await SpecialPowers.spawn(
      context,
      [selector],
      async selector => {
        // This is a workaround to address issues when there are multiple
        // elements with the same id and name on a page. This is a common pattern
        // when sites use multiple iframes for credit card fields. Each iframe
        // contains all the CC-related fields, but only one of the fields is visible.
        // TODO: replace the following with an approach that can precisely find the
        // element we want without basing on visibility.
        const e = content.document.querySelector(selector);
        if (
          e &&
          (content.HTMLInputElement.isInstance(e) ||
            content.HTMLTextAreaElement.isInstance(e))
        ) {
          return !!(
            e.checkVisibility({
              checkOpacity: true,
              checkVisibilityCSS: true,
            }) && e.getAttribute("aria-hidden") != "true"
          );
        }
        return !!e;
      }
    );
    if (find) {
      return context;
    }
  }

  return null;
}

async function verifyCaptureRecord(guid, expectedRecord) {
  let fields;
  let record = (await getAddresses()).find(addr => addr.guid == guid);
  if (record) {
    fields = VALID_ADDRESS_FIELDS;
  } else {
    record = (await getCreditCards()).find(cc => cc.guid == guid);
    if (record) {
      fields = VALID_CREDIT_CARD_FIELDS;
    } else {
      Assert.ok(false, "Cannot find record by guid");
    }
  }

  for (const field of fields) {
    Assert.equal(record[field], expectedRecord[field], `${field} is the same`);
  }
}

// Compare the saved addresses with the expected addresses.
async function expectSavedAddresses(expectedAddresses) {
  const addresses = await expectSavedAddressesCount(expectedAddresses.length);

  for (let i = 0; i < expectedAddresses.length; i++) {
    for (const [key, value] of Object.entries(expectedAddresses[i])) {
      is(addresses[i][key] ?? "", value, `field ${key} should be equal`);
    }
  }
  return addresses;
}

// Compare the number of saved addresses with the expected saved address count.
async function expectSavedAddressesCount(expectedCount) {
  const addresses = await getAddresses();
  is(
    addresses.length,
    expectedCount,
    `${addresses.length} address in the storage`
  );
  return addresses;
}

async function verifyPreviewResult(browser, section, expectedSection) {
  info(`Verify preview result`);
  const fieldDetails = section.fieldDetails;
  const expectedFieldDetails = expectedSection.fields;

  for (let i = 0; i < fieldDetails.length; i++) {
    const selector = getSelectorFromFieldDetail(fieldDetails[i]);
    const context = await findContext(browser, selector);
    let expected = expectedFieldDetails[i].autofill ?? "";
    if (fieldDetails[i].fieldName == "cc-number" && expected.length) {
      expected = "•".repeat(expected.length - 4) + expected.slice(-4);
    }

    await SpecialPowers.spawn(context, [{ expected, selector }], async obj => {
      const element = content.document.querySelector(obj.selector);
      if (content.HTMLSelectElement.isInstance(element)) {
        if (obj.expected) {
          for (let idx = 0; idx < element.options.length; idx++) {
            if (element.options[idx].value == obj.expected) {
              obj.expected = element.options[idx].text;
              break;
            }
          }
        } else {
          obj.expected = "";
        }
      }
      Assert.equal(
        element.previewValue,
        obj.expected,
        `element ${obj.selector} previewValue is the same ${element.previewValue}`
      );
    });
  }
}

async function verifyAutofillResult(browser, section, expectedSection) {
  info(`Verify autofill result`);
  const fieldDetails = section.fieldDetails;
  const expectedFieldDetails = expectedSection.fields;

  for (let i = 0; i < fieldDetails.length; i++) {
    const selector = getSelectorFromFieldDetail(fieldDetails[i]);
    const context = await findContext(browser, selector);
    const expected = expectedFieldDetails[i].autofill ?? "";
    await SpecialPowers.spawn(context, [{ expected, selector }], async obj => {
      const element = content.document.querySelector(obj.selector);
      if (content.HTMLSelectElement.isInstance(element)) {
        if (!obj.expected) {
          obj.expected = element.options[0].value;
        }
      }
      Assert.equal(
        element.value,
        obj.expected,
        `element ${obj.selector} value is the same ${element.value}`
      );
    });
  }
}

async function verifyClearResult(browser, section) {
  info(`Verify clear form result`);
  const fieldDetails = section.fieldDetails;

  for (let i = 0; i < fieldDetails.length; i++) {
    const selector = getSelectorFromFieldDetail(fieldDetails[i]);
    const context = await findContext(browser, selector);
    const expected = "";
    await SpecialPowers.spawn(context, [{ expected, selector }], async obj => {
      const element = content.document.querySelector(obj.selector);
      if (content.HTMLSelectElement.isInstance(element)) {
        obj.expected = element.options[0].value;
      }
      Assert.equal(
        element.value,
        obj.expected,
        `element ${obj.selector} value is the same ${element.value}`
      );
    });
  }
}

function verifySectionFieldDetails(sections, expectedSectionsInfo) {
  sections.forEach((section, index) => {
    const expectedSection = expectedSectionsInfo[index];

    const fieldDetails = section.fieldDetails;
    const expectedFieldDetails = expectedSection.fields;

    info(`section[${index}] ${expectedSection.description ?? ""}:`);
    info(`FieldName Prediction Results: ${fieldDetails.map(i => i.fieldName)}`);
    info(
      `FieldName Expected Results:   ${expectedFieldDetails.map(
        detail => detail.fieldName
      )}`
    );
    Assert.equal(
      fieldDetails.length,
      expectedFieldDetails.length,
      `Expected field count.`
    );

    fieldDetails.forEach((fieldDetail, fieldIndex) => {
      const expectedFieldDetail = expectedFieldDetails[fieldIndex];

      const expected = {
        ...{
          reason: "autocomplete",
          section: "",
          contactType: "",
          addressType: "",
          part: undefined,
        },
        ...expectedSection.default,
        ...expectedFieldDetail,
      };

      const keys = [
        "reason",
        "section",
        "contactType",
        "addressType",
        "fieldName",
        "part",
      ];

      for (const key of keys) {
        const expectedValue = expected[key];
        const actualValue = fieldDetail[key];
        Assert.equal(
          actualValue,
          expectedValue,
          `[${fieldDetail.fieldName}]: ${key} should be equal, expect ${expectedValue}, got ${actualValue}`
        );
      }
    });

    Assert.equal(
      section.isValidSection(),
      !expectedSection.invalid,
      `Should be an ${expectedSection.invalid ? "invalid" : "valid"} section`
    );
  });
}

async function triggerAutofillAndPreview(
  browser,
  selector,
  previewCallback,
  autofillCallback,
  clearCallback
) {
  const focusedContext = await findContext(browser, selector);

  if (focusedContext == focusedContext.top) {
    info(`Open the popup`);
    await openPopupOn(browser, selector);
  } else {
    info(`Open the popup on subframe`);
    await openPopupOnSubframe(browser, focusedContext, selector);
  }

  // Preview
  info(`Send key down to trigger preview`);
  let promise = TestUtils.topicObserved("formautofill-preview-complete");
  const firstItem = getDisplayedPopupItems(browser)[0];
  if (!firstItem.selected) {
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, focusedContext);
  }
  await promise;
  await previewCallback();

  // Autofill
  info(`Send key return to trigger autofill`);

  promise = TestUtils.topicObserved("formautofill-autofill-complete");
  await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, focusedContext);

  await promise;
  await autofillCallback();

  // Clear Form
  if (focusedContext == focusedContext.top) {
    info(`Open the popup again for clearing form`);
    await openPopupOn(browser, selector);
  } else {
    info(`Open the popup on subframe again for clearing form`);
    await openPopupOnSubframe(browser, focusedContext, selector);
  }

  info(`Send key down and return to clear form`);
  promise = TestUtils.topicObserved("formautofill-clear-form-complete");
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, focusedContext);
  await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, focusedContext);
  await promise;
  await clearCallback();
}

async function triggerCapture(browser, submitButtonSelector, fillSelectors) {
  for (const [selector, value] of Object.entries(fillSelectors)) {
    const context = await findContext(browser, selector);
    await SpecialPowers.spawn(context, [{ selector, value }], obj => {
      const element = content.document.querySelector(obj.selector);
      if (content.HTMLInputElement.isInstance(element)) {
        element.setUserInput(obj.value);
      } else if (
        content.HTMLSelectElement.isInstance(element) &&
        Array.isArray(obj.value)
      ) {
        element.multiple = true;
        [...element.options].forEach(option => {
          option.selected = obj.value.includes(option.value);
        });
      } else {
        element.value = obj.value;
      }
    });
  }

  const onAdded = waitForStorageChangedEvents("add");
  const onPopupShown = waitForPopupShown();
  submitButtonSelector ||= "input[type=submit]";
  const context = await findContext(browser, submitButtonSelector);
  await SpecialPowers.spawn(context, [submitButtonSelector], selector => {
    content.document.querySelector(selector).click();
  });
  await onPopupShown;
  await clickDoorhangerButton(MAIN_BUTTON);

  const [subject] = (await onAdded)[0];
  return subject.wrappedJSObject.guid;
}

/**
 * Runs heuristics test for form autofill on given patterns.
 *
 * @param {Array<object>} patterns
 *        An array of test patterns to run the heuristics test on.
 * @param {string} patterns.description
 *        Description of this heuristic test
 * @param {string} patterns.fixurePath
 *        The path of the test document
 * @param {string} patterns.fixureData
 *        Test document by string. Use either fixurePath or fixtureData.
 * @param {Array} patterns.prefs
 *        Array of preferences to be set before running the test.
 * @param {object} patterns.profile
 *        The profile to autofill. This is required only when running autofill test
 * @param {Array} patterns.region
 *        Region to assign before running the test
 * @param {Array} patterns.expectedResult
 *        The expected result of this heuristic test. See below for detailed explanation
 * @param {Function} patterns.onTestStart
 *        Function that is executed before the test starts. This runs after the form
 *        field has been focused.
 * @param {Function} patterns.onTestComplete
 *        Function that is executed when the test is complete, but before the tab is closed.
 *        This can be used by the test to verify the status after running the test.
 *
 * @param {string} patterns.autofillTrigger
 *        The selector to find the element to trigger the autocomplete popup.
 *        Currently we only supports id selector so the value must start with `#`.
 *        This parameter is only used when `options.testAutofill` is set.
 *
 * @param {string} patterns.submitButtonSelector
 *        The selector to find the submit button for capture test. This parameter
 *        is only used when `options.testCapture` is set.
 * @param {object} patterns.captureFillValue
 *        An object that is keyed by selector, and the value to be set for the element
 *        that is found by matching selector before submitting the form. This parameter
 *        is only used when `options.testCapture` is set.
 * @param {object} patterns.captureExpectedRecord
 *        The expected saved record after capturing the form. Keyed by field name. This
 *        parameter is only used when `options.testCapture` is set.
 * @param {object} patterns.only
 *        This parameter is used solely for debugging purposes. When set to true,
 *        it restricts the execution to only the specified testcase.
 *
 * @param {string} [fixturePathPrefix=""]
 *        The prefix to the path of fixture files.
 * @param {object} [options={ testAutofill: false, testCapture: false }]
 *        An options object containing additional configuration for running the test.
 * @param {boolean} [options.testAutofill]
 *        When set to true, the following tests will be run:
 *        1. Trigger preview and verify the preview result
 *        2. Trigger autofill and verify the autofill result
 *        3. Trigger clear form and verify the clear result
 * @param {boolean} [options.testCapture]
 *        When set to true, the test submits the form after autofilling test finishes.
 *        Before submitting the form, the test first filles value if `captureFillValue`
 *        is set then submits the form. This test then verifies that the capture
 *        doorhanger appears, and the doorhanger captures the expected value (captureExpectedRecord).
 *
 * @returns {Promise} A promise that resolves when all the tests are completed.
 *
 * The `patterns.expectedResult` array contains test data for different address or credit card sections.
 * Each section in the array is represented by an object and can include the following properties:
 * - description (optional): A string describing the section, primarily used for debugging purposes.
 * - default (optional): An object that sets the default values for all the fields within this section.
 *            The default object contains the same keys as the individual field objects.
 * - fields: An array of field details (class FieldDetails) within the section.
 *
 * Each field object can have the following keys:
 * - fieldName: The name of the field (e.g., "street-name", "cc-name" or "cc-number").
 * - reason: The reason for the field value (e.g., "autocomplete", "regex-heuristic" or "fathom").
 * - section: The section to which the field belongs (e.g., "billing", "shipping").
 * - part: The part of the field.
 * - contactType: The contact type of the field.
 * - addressType: The address type of the field.
 * - autofill: Set the expected autofill value when running autofill test
 *
 * For more information on the field object properties, refer to the FieldDetails class.
 *
 * Example test data:
 * add_heuristic_tests(
 * [{
 *   description: "first test pattern",
 *   fixuturePath: "autocomplete_off.html",
 *   profile: {organization: "Mozilla", country: "US", tel: "123"},
 *   expectedResult: [
 *   {
 *     description: "First section"
 *     fields: [
 *       { fieldName: "organization", reason: "autocomplete", autofill: "Mozilla" },
 *       { fieldName: "country", reason: "regex-heuristic", autofill: "US" },
 *       { fieldName: "tel", reason: "regex-heuristic", autofill: "123" },
 *     ]
 *   },
 *   {
 *     default: {
 *       reason: "regex-heuristic",
 *       section: "billing",
 *     },
 *     fields: [
 *       { fieldName: "cc-number", reason: "fathom" },
 *       { fieldName: "cc-nane" },
 *       { fieldName: "cc-exp" },
 *     ],
 *    }],
 *  },
 *  {
 *    // second test pattern //
 *  }
 * ],
 * "/fixturepath",
 * {
 *   testAutofill: true,
 *   testCapture: true,
 * }  // test options
 * )
 */
async function add_heuristic_tests(
  patterns,
  fixturePathPrefix = "",
  options = { testAutofill: false, testCapture: false }
) {
  async function runTest(testPattern) {
    const TEST_URL = testPattern.fixtureData
      ? TOP_LEVEL_HOST +
        `/document-builder.sjs?html=${encodeURIComponent(
          testPattern.fixtureData
        )}`
      : `${TOP_LEVEL_URL}../${fixturePathPrefix}${testPattern.fixturePath}`;

    info(`Test "${testPattern.description}"`);

    let regionInfo = null;
    if (testPattern.region) {
      regionInfo = { home: Region._home, current: Region._current };

      const region = testPattern.region;
      Region._setCurrentRegion(region);
      Region._setHomeRegion(region);
    }

    if (testPattern.prefs) {
      await SpecialPowers.pushPrefEnv({
        set: testPattern.prefs,
      });
    }

    if (testPattern.profile) {
      await setStorage(testPattern.profile);
    }

    await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
      await SimpleTest.promiseFocus(browser);

      info(`Focus on each field in the test document`);
      const contexts =
        browser.browsingContext.getAllBrowsingContextsInSubtree();

      // This is a workaround for when we set focus on elements across iframes (in the previous step).
      // The popup is not refreshed, and consequently, it does not receive key events needed to trigger
      // the autocomplete popup.
      const sleepAfterFocus = contexts.length > 1;

      for (const context of contexts) {
        await SpecialPowers.spawn(context, [], async () => {
          const elements = Array.from(
            content.document.querySelectorAll("input, select")
          );
          // Focus on each field in the test document to trigger autofill field detection
          // on all the fields.
          elements.forEach(element => {
            element.focus();
          });
        });

        try {
          await BrowserTestUtils.synthesizeKey("VK_ESCAPE", {}, context);
          if (sleepAfterFocus) {
            await sleep();
          }
        } catch (e) {
          // Error occurs when sending a key event to an invisible iframe, ignore the error.
        }
      }

      if (testPattern.onTestStart) {
        await testPattern.onTestStart();
      }

      info(`Waiting for expected section count`);
      const actor =
        browser.browsingContext.currentWindowGlobal.getActor("FormAutofill");
      await BrowserTestUtils.waitForCondition(() => {
        const sections = Array.from(actor.sectionsByRootId.values()).flat();
        return sections.length == testPattern.expectedResult.length;
      }, "Expected section count.");

      // Verify the identified fields in each section.
      info(`Verify the identified fields in each section`);
      const sections = Array.from(actor.sectionsByRootId.values()).flat();
      verifySectionFieldDetails(sections, testPattern.expectedResult);

      // Verify the autofilled value.
      if (options.testAutofill) {
        info(`test preview, autofill, and clear form`);
        let section;
        let expected;
        let autofillTrigger = testPattern.autofillTrigger;
        if (autofillTrigger) {
          const idx = testPattern.expectedResult.findIndex(expectedSection =>
            expectedSection.fields.some(field => "autofill" in field)
          );
          section = sections[idx];
          expected = testPattern.expectedResult[idx];
        } else {
          section = sections[0];
          expected = testPattern.expectedResult[0];
          autofillTrigger = getSelectorFromFieldDetail(section.fieldDetails[0]);
        }

        await triggerAutofillAndPreview(
          browser,
          autofillTrigger,
          async () => verifyPreviewResult(browser, section, expected),
          async () => verifyAutofillResult(browser, section, expected),
          async () => verifyClearResult(browser, section)
        );
      }

      if (options.testCapture) {
        info(`test capture`);
        const guid = await triggerCapture(
          browser,
          testPattern.submitButtonSelector,
          testPattern.captureFillValue
        );
        verifyCaptureRecord(guid, testPattern.captureExpectedRecord);
        await removeAllRecords();
      }

      if (testPattern.onTestComplete) {
        await testPattern.onTestComplete();
      }
    });

    if (testPattern.profile) {
      await removeAllRecords();
    }

    if (testPattern.prefs) {
      await SpecialPowers.popPrefEnv();
    }

    if (regionInfo) {
      Region._setCurrentRegion(regionInfo.home);
      Region._setHomeRegion(regionInfo.current);
    }
  }

  const only = patterns.find(pattern => !!pattern.only);
  if (only) {
    add_task(() => runTest(only));
    return;
  }

  patterns.forEach(testPattern => {
    add_task(() => runTest(testPattern));
  });
}

async function add_capture_heuristic_tests(patterns, fixturePathPrefix = "") {
  const oldValue = FormAutofillUtils.getOSAuthEnabled(
    FormAutofillUtils.AUTOFILL_CREDITCARDS_REAUTH_PREF
  );

  FormAutofillUtils.setOSAuthEnabled(
    FormAutofillUtils.AUTOFILL_CREDITCARDS_REAUTH_PREF,
    false
  );

  registerCleanupFunction(() => {
    FormAutofillUtils.setOSAuthEnabled(
      FormAutofillUtils.AUTOFILL_CREDITCARDS_REAUTH_PREF,
      oldValue
    );
  });

  add_heuristic_tests(patterns, fixturePathPrefix, { testCapture: true });
}

async function add_autofill_heuristic_tests(patterns, fixturePathPrefix = "") {
  const oldValue = FormAutofillUtils.getOSAuthEnabled(
    FormAutofillUtils.AUTOFILL_CREDITCARDS_REAUTH_PREF
  );

  FormAutofillUtils.setOSAuthEnabled(
    FormAutofillUtils.AUTOFILL_CREDITCARDS_REAUTH_PREF,
    false
  );

  registerCleanupFunction(() => {
    FormAutofillUtils.setOSAuthEnabled(
      FormAutofillUtils.AUTOFILL_CREDITCARDS_REAUTH_PREF,
      oldValue
    );
  });

  add_heuristic_tests(patterns, fixturePathPrefix, { testAutofill: true });
}

add_setup(function () {
  OSKeyStoreTestUtils.setup();
});

registerCleanupFunction(async () => {
  await removeAllRecords();
  await OSKeyStoreTestUtils.cleanup();
});
