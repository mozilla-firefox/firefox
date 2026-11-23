/**
 * Test for LoginHelperDOM.isInferredUsernameField and LoginHelperDOM.isInferredEmailField.
 */

"use strict";

const { LoginHelperDOM } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs"
);

const attributeTestData = [
  {
    testValues: [
      "",
      "name",
      "e-mail",
      "user",
      "user name",
      "userid",
      "lastname",
    ],
    expectation: "none",
  },
  {
    testValues: ["email", "EmaiL", "loginemail", "邮箱"],
    expectation: "email",
  },
  {
    testValues: ["username", "usErNaMe", "my username"],
    expectation: "username",
  },
  {
    testValues: ["usernameAndemail", "EMAILUSERNAME"],
    expectation: "username,email",
  },
];

const classNameTestData = [
  {
    testValues: [
      "inputTxt form-control",
      "user-input form-name",
      "text name mail",
    ],
    expectation: "none",
  },
  {
    testValues: ["input email", "signin-email form", "input form valid-Email"],
    expectation: "email",
  },
  {
    testValues: [
      "input username",
      "signup-username form",
      "input form my_username",
    ],
    expectation: "username",
  },
  {
    testValues: ["input text form username email"],
    expectation: "username,email",
  },
];

const labelTestData = [
  {
    testValues: [
      "First Name",
      "Last Name",
      "Company Name",
      "Password",
      "User Name",
    ],
    expectation: "none",
  },
  {
    testValues: ["Email:", "Email Address*"],
    expectation: "email",
  },
  {
    testValues: ["Username:", "choose a username"],
    expectation: "username",
  },
  {
    testValues: ["Username/Email", "username or email"],
    expectation: "username,email",
  },
];

const TESTCASES = [
  {
    description: "Test input type",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("type", v);
    },
    subtests: [
      {
        testValues: ["text", "url", "number", "username"],
        expectation: "none",
      },
      {
        testValues: ["email"],
        expectation: "email",
      },
    ],
  },
  {
    description: "Test autocomplete field",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("autocomplete", v);
    },
    subtests: [
      {
        testValues: [
          "off",
          "on",
          "name",
          "new-password",
          "current-password",
          "tel",
          "tel-national",
          "url",
        ],
        expectation: "none",
      },
      {
        testValues: ["email"],
        expectation: "email",
      },
      {
        testValues: ["username", "webauthn"],
        expectation: "username",
      },
    ],
  },
  {
    description: "Test id attribute",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("id", v);
    },
    subtests: attributeTestData,
  },
  {
    description: "Test name attribute",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("name", v);
    },
    subtests: attributeTestData,
  },
  {
    description: "Test class attribute",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("class", v);
    },
    subtests: [...attributeTestData, ...classNameTestData],
  },
  {
    description: "Test placeholder attribute",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("placeholder", v);
    },
    subtests: attributeTestData,
  },
  {
    description: "Test aria-label attribute",
    update: (doc, v) => {
      doc.querySelector("input").setAttribute("aria-label", v);
    },
    subtests: attributeTestData,
  },
  {
    description: "Test the first label",
    update: (doc, v) => {
      doc.getElementById("l1").textContent = v;
    },
    subtests: labelTestData,
  },
  {
    description: "Test the second label",
    update: (doc, v) => {
      doc.getElementById("l2").textContent = v;
    },
    subtests: labelTestData,

    // The username detection heuristic only examine the first label associated
    // with the input, so no matter what the data is for this label, it doesn't
    // affect the result.
    // We can update this testcase once we decide to support multiple labels.
    supported: false,
  },
];

for (let testcase of TESTCASES) {
  info("Sanity checking the testcase: " + testcase.description);

  (function () {
    add_task(async function () {
      info("Starting testcase: " + testcase.description);

      for (let subtest of testcase.subtests) {
        let document = MockDocument.createTestDocument(
          "http://localhost:8080/test/",
          `<label id="l1" for="id"></label>
           <label id="l2" for="id"></label>
           <input id="id" type="text" name="name">`
        );

        for (let value of subtest.testValues) {
          testcase.update(document, value);
          let ele = document.querySelector("input");

          let ret = LoginHelperDOM.isInferredUsernameField(ele);
          Assert.strictEqual(
            ret,
            testcase.supported !== false
              ? subtest.expectation.includes("username")
              : false,
            `${testcase.description}, isInferredUsernameField doesn't return correct result while setting the value to ${value}`
          );

          ret = LoginHelperDOM.isInferredEmailField(ele);
          Assert.strictEqual(
            ret,
            testcase.supported !== false
              ? subtest.expectation.includes("email")
              : false,
            `${testcase.description}, isInferredEmailField doesn't return correct result while setting the value to ${value}`
          );
        }
      }
    });
  })();
}
