// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/
"use strict";

// Tests that the cert download/import UI correctly identifies the cert being
// downloaded, and allows the trust of the cert to be specified.

/**
 * @typedef TestCase
 * @type {object}
 * @property {string} certFilename
 *           Filename of the cert for this test case.
 * @property {string} expectedDisplayString
 *           The string we expect the UI to display to represent the given cert.
 * @property {nsIX509Cert} cert
 *           Handle to the cert once read in setup().
 */

/**
 * A list of test cases representing certs that get "downloaded".
 *
 * @type {TestCase[]}
 */
const TEST_CASES = [
  { certFilename: "has-cn.pem", expectedDisplayString: "Foo", cert: null },
  {
    certFilename: "has-empty-subject.pem",
    expectedDisplayString: "Certificate Authority (unnamed)",
    cert: null,
  },
];

/**
 * Opens the cert download dialog.
 *
 * @param {nsIX509Cert} cert
 *        The cert to pass to the dialog for display.
 * @returns {Promise}
 *          A promise that resolves when the dialog has finished loading, with
 *          an array consisting of:
 *            1. The window of the opened dialog.
 *            2. The return value nsIWritablePropertyBag2 passed to the dialog.
 */
async function openCertDownloadDialog(cert) {
  let returnVals = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
    Ci.nsIWritablePropertyBag2
  );
  let win = window.openDialog(
    "chrome://pippki/content/downloadcert.xhtml",
    "",
    "",
    cert,
    returnVals
  );
  await TestUtils.topicObserved(
    "psm:download-cert-loaded",
    subject => subject == win
  );
  return [win, returnVals];
}

add_setup(async function () {
  for (let testCase of TEST_CASES) {
    testCase.cert = await readCertificate(testCase.certFilename, ",,");
    Assert.notEqual(
      testCase.cert,
      null,
      `'${testCase.certFilename}' should have been read`
    );
  }
});

// Test that the trust header message corresponds to the provided cert, and that
// the View Cert button launches the cert viewer for the provided cert.
add_task(async function testTrustHeaderAndViewCertButton() {
  for (let testCase of TEST_CASES) {
    let [win] = await openCertDownloadDialog(testCase.cert);
    Assert.deepEqual(
      win.document.l10n.getAttributes(
        win.document.getElementById("trustHeader")
      ),
      {
        id: "download-cert-trust-header",
        args: { caName: testCase.expectedDisplayString },
      },
      "Actual and expected trust header l10n attributes should match for " +
        `${testCase.certFilename}`
    );

    await BrowserTestUtils.closeWindow(win);
  }
});

// Test that the right values are returned when the dialog is accepted.
add_task(async function testAcceptDialogReturnValues() {
  let [win, retVals] = await openCertDownloadDialog(TEST_CASES[0].cert);
  win.document.getElementById("trustSSL").checked = true;
  win.document.getElementById("trustEmail").checked = false;
  info("Accepting dialog");
  win.document.getElementById("download_cert").acceptDialog();
  await BrowserTestUtils.windowClosed(win);

  Assert.ok(
    retVals.get("importConfirmed"),
    "Return value should signal user chose to import the cert"
  );
  Assert.ok(
    retVals.get("trustForSSL"),
    "Return value should signal SSL trust checkbox was checked"
  );
  Assert.ok(
    !retVals.get("trustForEmail"),
    "Return value should signal E-mail trust checkbox was unchecked"
  );
});

// Test that the right values are returned when the dialog is canceled.
add_task(async function testCancelDialogReturnValues() {
  let [win, retVals] = await openCertDownloadDialog(TEST_CASES[0].cert);
  info("Canceling dialog");
  win.document.getElementById("download_cert").cancelDialog();
  await BrowserTestUtils.windowClosed(win);

  Assert.ok(
    !retVals.get("importConfirmed"),
    "Return value should signal user chose not to import the cert"
  );
});
