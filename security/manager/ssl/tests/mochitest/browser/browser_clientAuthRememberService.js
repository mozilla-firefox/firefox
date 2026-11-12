// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/
"use strict";

/**
 * Test certificate (i.e. build/pgo/certs/mochitest.client).
 *
 * @type {nsIX509Cert}
 */
var cert;
var cert2;
var cert3;

var sdr = Cc["@mozilla.org/security/sdr;1"].getService(Ci.nsISecretDecoderRing);
var certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

var deleted = false;

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

async function testHelper(connectURL, expectedURL) {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  await SpecialPowers.pushPrefEnv({
    set: [["security.default_personal_cert", "Ask Every Time"]],
  });

  BrowserTestUtils.startLoadingURIString(
    win.gBrowser.selectedBrowser,
    connectURL
  );

  await BrowserTestUtils.browserLoaded(
    win.gBrowser.selectedBrowser,
    false,
    expectedURL,
    true
  );
  let loadedURL = win.gBrowser.selectedBrowser.documentURI.spec;
  Assert.ok(
    loadedURL.startsWith(expectedURL),
    `Expected and actual URLs should match (got '${loadedURL}', expected '${expectedURL}')`
  );

  await win.close();

  // This clears the TLS session cache so we don't use a previously-established
  // ticket to connect and bypass selecting a client auth certificate in
  // subsequent tests.
  sdr.logout();
}

async function openRequireClientCert() {
  gClientAuthDialogService.chooseCertificateCalled = false;
  await testHelper(
    "https://requireclientcert.example.com:443",
    "https://requireclientcert.example.com/"
  );
}

async function openRequireClientCert2() {
  gClientAuthDialogService.chooseCertificateCalled = false;
  await testHelper(
    "https://requireclientcert-2.example.com:443",
    "https://requireclientcert-2.example.com/"
  );
}

// Mock implementation of nsIClientAuthRememberService
const gClientAuthRememberService = {
  forgetRememberedDecision(key) {
    deleted = true;
    Assert.equal(
      key,
      "exampleKey2",
      "Expected to get the same key that was passed in getDecisions()"
    );
  },

  getDecisions() {
    return [
      {
        asciiHost: "example.com",
        dbKey: cert.dbKey,
        entryKey: "exampleKey1",
      },
      {
        asciiHost: "example.org",
        dbKey: cert2.dbKey,
        entryKey: "exampleKey2",
      },
      {
        asciiHost: "example.test",
        dbKey: cert3.dbKey,
        entryKey: "exampleKey3",
      },
      {
        asciiHost: "unavailable.example.com",
        // This dbKey should not correspond to any real certificate. The first
        // 8 bytes have to be 0, followed by the lengths of the serial number
        // and issuer distinguished name, respectively, and then followed by
        // the bytes of the serial number and finally the encoded issuer
        // distinguished name. In this case, the serial number is a single 0
        // byte and the issuer distinguished name is a DER SEQUENCE of length 0
        // (the bytes 0x30 and 0).
        // See also the documentation in nsNSSCertificateDB::FindCertByDBKey.
        dbKey: "AAAAAAAAAAAAAAABAAAAAgAeAA==",
        entryKey: "exampleKey4",
      },
    ];
  },

  QueryInterface: ChromeUtils.generateQI(["nsIClientAuthRememberService"]),
};

const gClientAuthDialogService = {
  _chooseCertificateCalled: false,

  get chooseCertificateCalled() {
    return this._chooseCertificateCalled;
  },

  set chooseCertificateCalled(value) {
    this._chooseCertificateCalled = value;
  },

  chooseCertificate(hostname, certArray, loadContext, caNames, callback) {
    this.chooseCertificateCalled = true;
    callback.certificateChosen(certArray[0], true);
  },

  QueryInterface: ChromeUtils.generateQI([Ci.nsIClientAuthDialogService]),
};

add_task(async function testRememberedDecisionsUI() {
  cert = await findCertByCommonName("Mochitest client");
  cert2 = await readCertificate("pgo-ca-all-usages.pem", ",,");
  cert3 = await readCertificate("client-cert-via-intermediate.pem", ",,");
  isnot(cert, null, "Should be able to find the test client cert");
  isnot(cert2, null, "Should be able to find pgo-ca-all-usages.pem");
  isnot(cert3, null, "Should be able to find client-cert-via-intermediate.pem");

  let clientAuthRememberServiceCID = MockRegistrar.register(
    "@mozilla.org/security/clientAuthRememberService;1",
    gClientAuthRememberService
  );

  let win = await openCertManager();

  let rememberedList = win.document.getElementById("rememberedList");

  await TestUtils.waitForCondition(
    () => !!rememberedList.querySelectorAll("richlistitem").length,
    "rememberedList is populated"
  );

  let listItems = rememberedList.querySelectorAll("richlistitem");
  Assert.equal(
    listItems.length,
    4,
    "rememberedList has expected number of items"
  );

  let labels = win.document
    .getElementById("rememberedList")
    .querySelectorAll("label");

  Assert.equal(
    labels.length,
    12,
    "rememberedList has expected number of labels"
  );

  await TestUtils.waitForCondition(
    () => !!labels[10].textContent.length,
    "Localized label is populated"
  );

  let expectedHosts = [
    "example.com",
    "example.org",
    "example.test",
    "unavailable.example.com",
  ];
  let hosts = [
    labels[0].value,
    labels[3].value,
    labels[6].value,
    labels[9].value,
  ];
  let expectedNames = [
    cert.commonName,
    cert2.commonName,
    cert3.commonName,
    "(Unavailable)",
  ];
  let names = [
    labels[1].value,
    labels[4].value,
    labels[7].value,
    labels[10].textContent,
  ];
  let expectedSerialNumbers = [
    cert.serialNumber,
    cert2.serialNumber,
    cert3.serialNumber,
    "(Unavailable)",
  ];
  let serialNumbers = [
    labels[2].value,
    labels[5].value,
    labels[8].value,
    labels[11].textContent,
  ];

  for (let i = 0; i < listItems.length; i++) {
    Assert.equal(hosts[i], expectedHosts[i], "got expected asciiHost");
    Assert.equal(names[i], expectedNames[i], "got expected commonName");
    Assert.equal(
      serialNumbers[i],
      expectedSerialNumbers[i],
      "got expected serialNumber"
    );
  }

  win.document.getElementById("rememberedList").selectedIndex = 1;
  win.document.getElementById("remembered_deleteButton").click();

  Assert.ok(deleted, "Expected forgetRememberedDecision() to get called");

  win.document.getElementById("certmanager").acceptDialog();
  await BrowserTestUtils.windowClosed(win);

  MockRegistrar.unregister(clientAuthRememberServiceCID);
});

add_task(async function testDeletingRememberedDecisions() {
  let clientAuthDialogServiceCID = MockRegistrar.register(
    "@mozilla.org/security/ClientAuthDialogService;1",
    gClientAuthDialogService
  );
  let cars = Cc["@mozilla.org/security/clientAuthRememberService;1"].getService(
    Ci.nsIClientAuthRememberService
  );
  registerCleanupFunction(() => cars.clearRememberedDecisions());

  await openRequireClientCert();
  Assert.ok(
    gClientAuthDialogService.chooseCertificateCalled,
    "chooseCertificate should have been called if visiting 'requireclientcert.example.com' for the first time"
  );

  await openRequireClientCert();
  Assert.ok(
    !gClientAuthDialogService.chooseCertificateCalled,
    "chooseCertificate should not have been called if visiting 'requireclientcert.example.com' for the second time"
  );

  await openRequireClientCert2();
  Assert.ok(
    gClientAuthDialogService.chooseCertificateCalled,
    "chooseCertificate should have been called if visiting 'requireclientcert-2.example.com' for the first time"
  );

  let originAttributes = { privateBrowsingId: 0 };
  cars.deleteDecisionsByHost("requireclientcert.example.com", originAttributes);

  await openRequireClientCert();
  Assert.ok(
    gClientAuthDialogService.chooseCertificateCalled,
    "chooseCertificate should have been called after removing all remembered decisions for 'requireclientcert.example.com'"
  );

  await openRequireClientCert2();
  Assert.ok(
    !gClientAuthDialogService.chooseCertificateCalled,
    "chooseCertificate should not have been called if visiting 'requireclientcert-2.example.com' for the second time"
  );

  MockRegistrar.unregister(clientAuthDialogServiceCID);
});
