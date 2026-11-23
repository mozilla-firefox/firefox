/**
 * Test for LoginAutoComplete.isProbablyANewPasswordField.
 */

"use strict";

const { LoginHelperDOM } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs"
);

// TODO: create a fake window for the test document to pass fathom.isVisible check.
// We should consider moving these tests to mochitest because many fathom
// signals rely on visibility, position, etc., of the test element (See Bug 1712699),
// which is not supported in xpcshell-test.
function makeDocumentVisibleToFathom(doc) {
  let win = {
    getComputedStyle() {
      return {
        overflow: "visible",
        visibility: "visible",
      };
    },
  };
  Object.defineProperty(doc, "defaultView", {
    value: win,
  });
  return doc;
}

function labelledByDocument() {
  let doc = MockDocument.createTestDocument(
    "http://localhost:8080/test/",
    `<div>
       <label id="paper-input-label-2">Password</label>
       <input aria-labelledby="paper-input-label-2" type="password">
     </div>`
  );
  let div = doc.querySelector("div");
  // Put the div contents inside shadow DOM.
  div.attachShadow({ mode: "open" }).append(...div.children);
  return doc;
}
const LABELLEDBY_SHADOW_TESTCASE = labelledByDocument();

const TESTCASES = [
  // Note there is no test case for `<input type="password" autocomplete="new-password">`
  // since isProbablyANewPasswordField explicitly does not run in that case.
  {
    description: "Basic login form",
    document: `
      <h1>Sign in</h1>
      <form>
        <label>Username: <input type="text" name="username"></label>
        <label>Password: <input type="password" name="password"></label>
        <input type="submit" value="Sign in">
      </form>
    `,
    expectedResult: [false],
  },
  {
    description: "Basic registration form",
    document: `
      <h1>Create account</h1>
      <form>
        <label>Username: <input type="text" name="username"></label>
        <label>Password: <input type="password" name="new-password"></label>
        <input type="submit" value="Register">
      </form>
    `,
    expectedResult: [true],
  },
  {
    // TODO: Add <placeholder="confirm"> to "confirm-passowrd" password field so fathom can recognize it
    // as a new password field. Currently, the fathom rules don't really work well in xpcshell-test
    // because signals rely on visibility, position doesn't work. If we move this test to mochitest, we should
    // be able to remove the interim solution (See Bug 1712699).
    description: "Basic password change form",
    document: `
      <h1>Change password</h1>
      <form>
        <label>Current Password: <input type="password" name="current-password"></label>
        <label>New Password: <input type="password" name="new-password"></label>
        <label>Confirm Password: <input type="password" name="confirm-password" placeholder="confirm"></label>
        <input type="submit" value="Save">
      </form>
    `,
    expectedResult: [false, true, true],
  },
  {
    description: "Basic login 'form' without a form element",
    document: `
      <h1>Sign in</h1>
      <label>Username: <input type="text" name="username"></label>
      <label>Password: <input type="password" name="password"></label>
      <input type="submit" value="Sign in">
    `,
    expectedResult: [false],
  },
  {
    description: "Basic registration 'form' without a form element",
    document: `
      <h1>Create account</h1>
      <label>Username: <input type="text" name="username"></label>
      <label>Password: <input type="password" name="new-password"></label>
      <input type="submit" value="Register">
    `,
    expectedResult: [true],
  },
  {
    description: "Basic password change 'form' without a form element",
    document: `
      <h1>Change password</h1>
      <label>Current Password: <input type="password" name="current-password"></label>
      <label>New Password: <input type="password" name="new-password"></label>
      <label>Confirm Password: <input type="password" name="confirm-password"></label>
      <input type="submit" value="Save">
    `,
    expectedResult: [false, true, true],
  },
  {
    description: "Password field with aria-labelledby inside shadow DOM",
    document: LABELLEDBY_SHADOW_TESTCASE,
    inputs: LABELLEDBY_SHADOW_TESTCASE.querySelector(
      "div"
    ).shadowRoot.querySelectorAll("input[type='password']"),
    expectedResult: [false],
  },
];

add_task(async function test_returns_false_when_pref_disabled() {
  const threshold = Services.prefs.getStringPref(
    NEW_PASSWORD_HEURISTIC_ENABLED_PREF
  );

  info("Temporarily disabling new-password heuristic pref");
  Services.prefs.setStringPref(NEW_PASSWORD_HEURISTIC_ENABLED_PREF, "-1");

  // Use registration form test case, where we know it should return true if enabled
  const testcase = TESTCASES[1];
  info("Starting testcase: " + testcase.description);
  const document = Document.isInstance(testcase.document)
    ? testcase.document
    : MockDocument.createTestDocument(
        "http://localhost:8080/test/",
        testcase.document
      );
  for (let [i, input] of testcase.inputs ||
    document.querySelectorAll(`input[type="password"]`).entries()) {
    const result = LoginHelperDOM.isProbablyANewPasswordField(input);
    Assert.strictEqual(
      result,
      false,
      `When the pref is set to disable, the result is always false, e.g. for the testcase, ${testcase.description} ${i}`
    );
  }

  info("Re-enabling new-password heuristic pref");
  Services.prefs.setStringPref(NEW_PASSWORD_HEURISTIC_ENABLED_PREF, threshold);
});

for (let testcase of TESTCASES) {
  info("Sanity checking the testcase: " + testcase.description);

  (function () {
    add_task(async function () {
      info("Starting testcase: " + testcase.description);
      let document = Document.isInstance(testcase.document)
        ? testcase.document
        : MockDocument.createTestDocument(
            "http://localhost:8080/test/",
            testcase.document
          );

      document = makeDocumentVisibleToFathom(document);
      const results = [];
      for (let input of testcase.inputs ||
        document.querySelectorAll(`input[type="password"]`)) {
        const result = LoginHelperDOM.isProbablyANewPasswordField(input);
        results.push(result);
      }

      for (let i = 0; i < testcase.expectedResult.length; i++) {
        let expectedResult = testcase.expectedResult[i];
        Assert.strictEqual(
          results[i],
          expectedResult,
          `In the test case, ${testcase.description}, check if password field #${i} is a new password field.`
        );
      }
    });
  })();
}
