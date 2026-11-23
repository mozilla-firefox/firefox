/**
 * Test for LoginHelperDOM.isInferredLoginForm.
 */

"use strict";

const { LoginHelperDOM } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs"
);

const attributeTestData = [
  {
    testValues: ["", "form", "search", "signup", "sign-up", "sign/up"],
    expectation: false,
  },
  {
    testValues: [
      "Login",
      "Log in",
      "Log on",
      "Log-on",
      "Sign in",
      "Sigin",
      "Sign/in",
      "Sign-in",
      "Sign on",
      "Sign-on",
      "loginForm",
      "form-sign-in",
    ],
    expectation: true,
  },
];

const classNameTestData = [
  {
    testValues: [
      "",
      "inputTxt form-control",
      "user-input form-name",
      "text name mail",
      "form signup",
    ],
    expectation: false,
  },
  {
    testValues: ["login form"],
    expectation: true,
  },
];

const TESTCASES = [
  {
    description: "Test id attribute",
    update: (doc, v) => {
      doc.querySelector("form").setAttribute("id", v);
    },
    subtests: attributeTestData,
  },
  {
    description: "Test name attribute",
    update: (doc, v) => {
      doc.querySelector("form").setAttribute("name", v);
    },
    subtests: attributeTestData,
  },
  {
    description: "Test class attribute",
    update: (doc, v) => {
      doc.querySelector("form").setAttribute("class", v);
    },
    subtests: [...attributeTestData, ...classNameTestData],
  },
  {
    description: "Test aria-label attribute",
    update: (doc, v) => {
      doc.querySelector("form").setAttribute("aria-label", v);
    },
    subtests: attributeTestData,
  },
];

for (let testcase of TESTCASES) {
  info("Sanity checking the testcase: " + testcase.description);

  (function () {
    add_task(async function () {
      info("Starting testcase: " + testcase.description);

      for (let subtest of testcase.subtests) {
        const document = MockDocument.createTestDocument(
          "http://localhost:8080/test/",
          `<form id="id" name="name"></form>`
        );

        for (let value of subtest.testValues) {
          testcase.update(document, value);
          const ele = document.querySelector("form");
          const ret = LoginHelperDOM.isInferredLoginForm(ele);
          Assert.strictEqual(
            ret,
            subtest.expectation,
            `${testcase.description}, isInferredLoginForm doesn't return correct result while setting the value to ${value}`
          );
        }
      }
    });
  })();
}
