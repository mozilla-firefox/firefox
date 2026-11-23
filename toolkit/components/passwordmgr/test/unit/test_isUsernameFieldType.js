/**
 * Test for LoginHelperDOM.isUsernameFieldType
 */

"use strict";

const { LoginHelperDOM } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs"
);

const autocompleteTypes = {
  "": true,
  on: true,
  off: true,
  name: false,
  "unrecognized-type": true,
  "given-name": false,
  "additional-name": false,
  "family-name": false,
  nickname: false,
  username: true,
  "new-password": false,
  "current-password": false,
  "organization-title": false,
  organization: false,
  "street-address": false,
  "address-line1": false,
  "address-line2": false,
  "address-line3": false,
  "address-level4": false,
  "address-level3": false,
  "address-level2": false,
  "address-level1": false,
  country: false,
  "country-name": false,
  "postal-code": false,
  "cc-name": false,
  "cc-given-name": false,
  "cc-additional-name": false,
  "cc-family-name": false,
  "cc-number": false,
  "cc-exp": false,
  "cc-exp-month": false,
  "cc-exp-year": false,
  "cc-csc": false,
  "cc-type": false,
  "transaction-currency": false,
  "transaction-amount": false,
  language: false,
  bday: false,
  "bday-day": false,
  "bday-month": false,
  "bday-year": false,
  sex: false,
  url: false,
  photo: false,
  tel: true,
  "tel-country-code": false,
  "tel-national": true,
  "tel-area-code": false,
  "tel-local": false,
  "tel-local-prefix": false,
  "tel-local-suffix": false,
  "tel-extension": false,
  email: true,
  impp: false,
};

const TESTCASES = [
  {
    description: "type=text",
    document: `<input type="text">`,
    expected: true,
  },
  {
    description: "type=email, no autocomplete attribute",
    document: `<input type="email">`,
    expected: true,
  },
  {
    description: "type=url, no autocomplete attribute",
    document: `<input type="url">`,
    expected: true,
  },
  {
    description: "type=tel, no autocomplete attribute",
    document: `<input type="tel">`,
    expected: true,
  },
  {
    description: "type=number, no autocomplete attribute",
    document: `<input type="number">`,
    expected: true,
  },
  {
    description: "type=search, no autocomplete attribute",
    document: `<input type="search">`,
    expected: true,
  },
  {
    description: "type=range, no autocomplete attribute",
    document: `<input type="range">`,
    expected: false,
  },
  {
    description: "type=date, no autocomplete attribute",
    document: `<input type="date">`,
    expected: false,
  },
  {
    description: "type=month, no autocomplete attribute",
    document: `<input type="month">`,
    expected: false,
  },
  {
    description: "type=week, no autocomplete attribute",
    document: `<input type="week">`,
    expected: false,
  },
  {
    description: "type=time, no autocomplete attribute",
    document: `<input type="time">`,
    expected: false,
  },
  {
    description: "type=datetime, no autocomplete attribute",
    document: `<input type="datetime">`,
    expected: false,
  },
  {
    description: "type=datetime-local, no autocomplete attribute",
    document: `<input type="datetime-local">`,
    expected: false,
  },
  {
    description: "type=color, no autocomplete attribute",
    document: `<input type="color">`,
    expected: false,
  },
];

for (let [name, expected] of Object.entries(autocompleteTypes)) {
  TESTCASES.push({
    description: `type=text autocomplete=${name}`,
    document: `<input type="text" autocomplete="${name}">`,
    expected,
  });
}

TESTCASES.forEach(testcase => {
  add_task(async function () {
    info("Starting testcase: " + testcase.description);
    let document = MockDocument.createTestDocument(
      "http://localhost:8080/test/",
      testcase.document
    );
    let input = document.querySelector("input");
    Assert.equal(
      LoginHelperDOM.isUsernameFieldType(input),
      testcase.expected,
      testcase.description
    );
  });
});
