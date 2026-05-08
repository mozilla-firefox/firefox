"use strict";

var { FormAutofillHeuristics } = ChromeUtils.importESModule(
  "resource://gre/modules/shared/FormAutofillHeuristics.sys.mjs"
);
var { LabelUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/shared/LabelUtils.sys.mjs"
);
var { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

const TESTCASES = [
  {
    description: "Input element in a label element",
    document: `<form>
                 <label> E-Mail
                   <input id="targetElement" type="text">
                 </label>
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["email", {}],
  },
  {
    description:
      "A label element is out of the form contains the related input",
    document: `<label for="targetElement"> E-Mail</label>
               <form>
                 <input id="targetElement" type="text">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["email", {}],
  },
  {
    description: "A label element contains span element",
    document: `<label for="targetElement">FOO<span>E-Mail</span>BAR</label>
               <form>
                 <input id="targetElement" type="text">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["email", {}],
  },
  {
    description: "The signature in 'name' attr of an input",
    document: `<input id="targetElement" name="email" type="text">`,
    elementId: "targetElement",
    expectedReturnValue: ["email", {}],
  },
  {
    description: "The signature in 'id' attr of an input",
    document: `<input id="targetElement_email" name="tel" type="text">`,
    elementId: "targetElement_email",
    expectedReturnValue: ["email", {}],
  },
  {
    description: "Select element in a label element",
    document: `<form>
                 <label> State
                   <select id="targetElement"></select>
                 </label>
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["address-level1", {}],
  },
  {
    description: "A select element without a form wrapped",
    document: `<label for="targetElement">State</label>
               <select id="targetElement"></select>`,
    elementId: "targetElement",
    expectedReturnValue: ["address-level1", {}],
  },
  {
    description: "address line input",
    document: `<label for="targetElement">street</label>
               <input id="targetElement" type="text">`,
    elementId: "targetElement",
    expectedReturnValue: ["street-address", {}],
  },
  {
    description: "CJK character - Traditional Chinese",
    document: `<label> 郵遞區號
                 <input id="targetElement" />
               </label>`,
    elementId: "targetElement",
    expectedReturnValue: ["postal-code", {}],
  },
  {
    description: "CJK character - Japanese",
    document: `<label> 郵便番号
                 <input id="targetElement" />
               </label>`,
    elementId: "targetElement",
    expectedReturnValue: ["postal-code", {}],
  },
  {
    description: "CJK character - Korean",
    document: `<label> 우편 번호
                 <input id="targetElement" />
               </label>`,
    elementId: "targetElement",
    expectedReturnValue: ["postal-code", {}],
  },
  {
    description: "",
    document: `<input id="targetElement" name="fullname">`,
    elementId: "targetElement",
    expectedReturnValue: ["name", {}],
  },
  {
    description: 'input element with "submit" type',
    document: `<input id="targetElement" type="submit" />`,
    elementId: "targetElement",
    expectedReturnValue: [[], {}],
  },
  {
    description: "The signature in 'name' attr of an email input",
    document: `<input id="targetElement" name="email" type="number">`,
    elementId: "targetElement",
    expectedReturnValue: ["email", {}],
  },
  {
    description: 'input element with "email" type',
    document: `<input id="targetElement" type="email" />`,
    elementId: "targetElement",
    expectedReturnValue: ["email", {}],
  },
  {
    description: "Exclude United State string",
    document: `<label>United State
                 <input id="targetElement" />
               </label>`,
    elementId: "targetElement",
    expectedReturnValue: [[], {}],
  },
  {
    description: '"County" field with "United State" string',
    document: `<label>United State County
                 <input id="targetElement" />
               </label>`,
    elementId: "targetElement",
    expectedReturnValue: ["address-level1", {}],
  },
  {
    description: '"city" field with double "United State" string',
    document: `<label>United State united sTATE city
                 <input id="targetElement" />
               </label>`,
    elementId: "targetElement",
    expectedReturnValue: ["address-level2", {}],
  },
  {
    description: "Verify credit card number",
    document: `<form>
                 <label for="targetElement"> Card Number</label>
                 <input id="targetElement" type="text">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["cc-number", { fathomConfidence: 1 }],
  },
  {
    description: "Identify credit card type field",
    document: `<form>
                 <label for="targetElement">Card Type</label>
                 <input id="targetElement" type="text">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["cc-type", {}],
  },
  {
    description: `Identify address field when contained in a form with autocomplete="off"`,
    document: `<form autocomplete="off">
                <input id="given-name">
               </form>`,
    elementId: "given-name",
    expectedReturnValue: ["given-name", {}],
  },
  {
    description: `Identify address field that has a placeholder but no label associated with it`,
    document: `<form>
                <input id="targetElement" placeholder="Name">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["name", {}],
  },
  {
    description: `Identify address field that has a placeholder, no associated label, and its autocomplete attribute is "off"`,
    document: `<form>
                <input id="targetElement" placeholder="Address" autocomplete="off">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["street-address", {}],
  },
  {
    description: `Identify address field that has a placeholder, no associated label, and the form's autocomplete attribute is "off"`,
    document: `<form autocomplete="off">
                <input id="targetElement" placeholder="Country">
               </form>`,
    elementId: "targetElement",
    expectedReturnValue: ["country", {}],
  },
];

add_setup(async function () {
  Services.prefs.setStringPref(
    "extensions.formautofill.creditCards.heuristics.fathom.testConfidence",
    "1"
  );

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(
      "extensions.formautofill.creditCards.heuristics.fathom.testConfidence"
    );
  });
});

TESTCASES.forEach(testcase => {
  add_task(async function () {
    info("Starting testcase: " + testcase.description);

    let doc = MockDocument.createTestDocument(
      "http://localhost:8080/test/",
      testcase.document
    );

    let element = doc.getElementById(testcase.elementId);
    let value = FormAutofillHeuristics.inferFieldInfo(element);
    delete value[2]; // ignore extra argument

    Assert.deepEqual(value, testcase.expectedReturnValue);
    LabelUtils.clearLabelMap();
  });
});

add_task(async function test_regexp_list() {
  info("Verify the fieldName support for select element.");
  let SUPPORT_LIST = {
    email: null, // email
    "tel-extension": null, // tel-extension
    phone: null, // tel
    organization: null, // organization
    "street-address": null, // street-address
    address1: null, // address-line1
    address2: null, // address-line2
    address3: null, // address-line3
    city: "address-level2",
    region: "address-level1",
    "postal-code": null, // postal-code
    country: "country",
    fullname: null, // name
    fname: null, // given-name
    mname: null, // additional-name
    lname: null, // family-name
    cardholder: null, // cc-name
    "cc-number": null, // cc-number
    addmonth: "cc-exp-month",
    addyear: "cc-exp-year",
  };
  for (let label of Object.keys(SUPPORT_LIST)) {
    let testcase = {
      description: `A select element supports ${label} or not`,
      document: `<select id="${label}"></select>`,
      elementId: label,
      expectedReturnValue: SUPPORT_LIST[label]
        ? [SUPPORT_LIST[label], {}]
        : [[], {}],
    };
    info(testcase.description);
    info(testcase.document);
    let doc = MockDocument.createTestDocument(
      "http://localhost:8080/test/",
      testcase.document
    );

    let element = doc.getElementById(testcase.elementId);
    let value = FormAutofillHeuristics.inferFieldInfo(element);
    delete value[2]; // ignore extra argument

    Assert.deepEqual(value, testcase.expectedReturnValue, label);
  }
  LabelUtils.clearLabelMap();
});

add_task(async function test_autofill_creditCards_autocomplete_off_pref() {
  let document = `<form autocomplete="off">
                    <label for="targetElement"> Card Number</label>
                    <input id="targetElement" type="text">
                  </form>`;
  let expected = [[], {}];
  info(`Set pref so that credit card autofill respects autocomplete="off"`);
  Services.prefs.setBoolPref(
    FormAutofill.AUTOFILL_CREDITCARDS_AUTOCOMPLETE_OFF_PREF,
    false
  );
  let doc = MockDocument.createTestDocument(
    "http://localhost:8080/test/",
    document
  );
  let element = doc.getElementById("targetElement");
  let value = FormAutofillHeuristics.inferFieldInfo(element);
  delete value[2]; // ignore extra argument

  Assert.deepEqual(value, expected);
  document = `<form>
                <label for="targetElement"> Card Number</label>
                <input id="targetElement" type="text">
              </form>`;
  expected = ["cc-number", { fathomConfidence: 1 }];
  info(
    `Set pref so that credit card autofill does not respect autocomplete="off"`
  );
  Services.prefs.setBoolPref(
    FormAutofill.AUTOFILL_CREDITCARDS_AUTOCOMPLETE_OFF_PREF,
    true
  );
  doc = MockDocument.createTestDocument(
    "http://localhost:8080/test/",
    document
  );
  element = doc.getElementById("targetElement");
  value = FormAutofillHeuristics.inferFieldInfo(element);
  delete value[2]; // ignore extra argument

  Assert.deepEqual(value, expected);
  Services.prefs.clearUserPref(
    FormAutofill.AUTOFILL_CREDITCARDS_AUTOCOMPLETE_OFF_PREF
  );
});

add_task(async function test_autofill_addresses_autocomplete_off_pref() {
  let document = `<form autocomplete="off">
                    <input id="given-name">
                  </form>`;
  let expected = [[], {}];
  info(`Set pref so that address autofill respects autocomplete="off"`);
  Services.prefs.setBoolPref(
    FormAutofill.AUTOFILL_ADDRESSES_AUTOCOMPLETE_OFF_PREF,
    false
  );
  let doc = MockDocument.createTestDocument(
    "http://localhost:8080/test/",
    document
  );
  let element = doc.getElementById("given-name");
  let value = FormAutofillHeuristics.inferFieldInfo(element);
  delete value[2]; // ignore extra argument

  Assert.deepEqual(value, expected);
  document = `<form>
                <input id="given-name">
              </form>`;
  expected = ["given-name", {}];
  info(`Set pref so that address autofill does not respect autocomplete="off"`);
  Services.prefs.setBoolPref(
    FormAutofill.AUTOFILL_ADDRESSES_AUTOCOMPLETE_OFF_PREF,
    true
  );
  doc = MockDocument.createTestDocument(
    "http://localhost:8080/test/",
    document
  );
  element = doc.getElementById("given-name");
  value = FormAutofillHeuristics.inferFieldInfo(element);
  delete value[2]; // ignore extra argument

  Assert.deepEqual(value, expected);
  Services.prefs.clearUserPref(
    FormAutofill.AUTOFILL_ADDRESSES_AUTOCOMPLETE_OFF_PREF
  );
});
