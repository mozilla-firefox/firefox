// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/
"use strict";

// Tests various scenarios connecting to a server that requires client cert
// authentication. Also tests that nsIClientAuthDialogService.chooseCertificate
// is called at the appropriate times and with the correct arguments.

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

const DialogState = {
  // Assert that chooseCertificate() is never called.
  ASSERT_NOT_CALLED: "ASSERT_NOT_CALLED",
  // Return that the user selected the first given cert.
  RETURN_CERT_SELECTED: "RETURN_CERT_SELECTED",
  // Return that the user canceled.
  RETURN_CERT_NOT_SELECTED: "RETURN_CERT_NOT_SELECTED",
};

var sdr = Cc["@mozilla.org/security/sdr;1"].getService(Ci.nsISecretDecoderRing);
let cars = Cc["@mozilla.org/security/clientAuthRememberService;1"].getService(
  Ci.nsIClientAuthRememberService
);

var gExpectedClientCertificateChoices;

// Mock implementation of nsIClientAuthDialogService.
const gClientAuthDialogService = {
  _state: DialogState.ASSERT_NOT_CALLED,
  _rememberClientAuthCertificate: false,
  _chooseCertificateCalled: false,

  set state(newState) {
    info(`old state: ${this._state}`);
    this._state = newState;
    info(`new state: ${this._state}`);
  },

  get state() {
    return this._state;
  },

  set rememberClientAuthCertificate(value) {
    this._rememberClientAuthCertificate = value;
  },

  get rememberClientAuthCertificate() {
    return this._rememberClientAuthCertificate;
  },

  get chooseCertificateCalled() {
    return this._chooseCertificateCalled;
  },

  set chooseCertificateCalled(value) {
    this._chooseCertificateCalled = value;
  },

  chooseCertificate(hostname, certArray, loadContext, caNames, callback) {
    this.chooseCertificateCalled = true;
    Assert.notEqual(
      this.state,
      DialogState.ASSERT_NOT_CALLED,
      "chooseCertificate() should be called only when expected"
    );
    Assert.equal(
      hostname,
      "requireclientcert.example.com",
      "Hostname should be 'requireclientcert.example.com'"
    );

    // For mochitests, the cert at build/pgo/certs/mochitest.client should be
    // selectable as well as one of the PGO certs we loaded in `setup`, so we do
    // some brief checks to confirm this.
    Assert.notEqual(certArray, null, "Cert list should not be null");
    Assert.equal(
      certArray.length,
      gExpectedClientCertificateChoices,
      `${gExpectedClientCertificateChoices} certificates should be available`
    );

    for (let cert of certArray) {
      Assert.notEqual(cert, null, "Cert list should contain nsIX509Certs");
      Assert.equal(
        cert.issuerCommonName,
        "Temporary Certificate Authority",
        "cert should have expected issuer CN"
      );
    }

    if (this.state == DialogState.RETURN_CERT_SELECTED) {
      callback.certificateChosen(
        certArray[0],
        this.rememberClientAuthCertificate
      );
    } else {
      callback.certificateChosen(null, this.rememberClientAuthCertificate);
    }
  },

  QueryInterface: ChromeUtils.generateQI(["nsIClientAuthDialogService"]),
};

add_setup(async function () {
  let clientAuthDialogServiceCID = MockRegistrar.register(
    "@mozilla.org/security/ClientAuthDialogService;1",
    gClientAuthDialogService
  );
  registerCleanupFunction(() => {
    MockRegistrar.unregister(clientAuthDialogServiceCID);
  });

  // This CA has the expected keyCertSign and cRLSign usages. It should not be
  // presented for use as a client certificate.
  await readCertificate("pgo-ca-regular-usages.pem", "CTu,CTu,CTu");
  // This CA has all keyUsages. For compatibility with preexisting behavior, it
  // will be presented for use as a client certificate.
  await readCertificate("pgo-ca-all-usages.pem", "CTu,CTu,CTu");
  // This client certificate was issued by an intermediate that was issued by
  // the test CA. The server only lists the test CA's subject distinguished name
  // as an acceptible issuer name for client certificates. If the implementation
  // can determine that the test CA is a root CA for the client certificate and
  // thus is acceptible to use, it should be included in the chooseCertificate
  // callback. At the beginning of this test (speaking of this file as a whole),
  // the client is not aware of the intermediate, and so it is not available in
  // the callback.
  await readCertificate("client-cert-via-intermediate.pem", ",,");
  // This certificate has an id-kp-OCSPSigning EKU. Client certificates
  // shouldn't have this EKU, but there is at least one private PKI where they
  // do. For interoperability, such certificates will be presented for use.
  await readCertificate("client-cert-with-ocsp-signing.pem", ",,");
  gExpectedClientCertificateChoices = 3;
});

/**
 * Test helper for the tests below.
 *
 * @param {string} prefValue
 *        Value to set the "security.default_personal_cert" pref to.
 * @param {string} urlToNavigate
 *        The URL to navigate to.
 * @param {string} expectedURL
 *        If the connection is expected to load successfully, the URL that
 *        should load. If the connection is expected to fail and result in an
 *        error page, |undefined|.
 * @param {boolean} expectCallingChooseCertificate
 *        Determines whether we expect chooseCertificate to be called.
 * @param {object} options
 *        Optional options object to pass on to the window that gets opened.
 * @param {string} expectStringInPage
 *        Optional string that is expected to be in the content of the page
 *        once it loads.
 */
async function testHelper(
  prefValue,
  urlToNavigate,
  expectedURL,
  expectCallingChooseCertificate,
  options = undefined,
  expectStringInPage = undefined
) {
  gClientAuthDialogService.chooseCertificateCalled = false;
  await SpecialPowers.pushPrefEnv({
    set: [["security.default_personal_cert", prefValue]],
  });

  let win = await BrowserTestUtils.openNewBrowserWindow(options);

  BrowserTestUtils.startLoadingURIString(
    win.gBrowser.selectedBrowser,
    urlToNavigate
  );
  if (expectedURL) {
    await BrowserTestUtils.browserLoaded(
      win.gBrowser.selectedBrowser,
      false,
      "https://requireclientcert.example.com/",
      true
    );
    let loadedURL = win.gBrowser.selectedBrowser.documentURI.spec;
    Assert.ok(
      loadedURL.startsWith(expectedURL),
      `Expected and actual URLs should match (got '${loadedURL}', expected '${expectedURL}')`
    );
  } else {
    await new Promise(resolve => {
      let removeEventListener = BrowserTestUtils.addContentEventListener(
        win.gBrowser.selectedBrowser,
        "AboutNetErrorLoad",
        () => {
          removeEventListener();
          resolve();
        },
        { capture: false, wantUntrusted: true }
      );
    });
  }

  Assert.equal(
    gClientAuthDialogService.chooseCertificateCalled,
    expectCallingChooseCertificate,
    "chooseCertificate should have been called if we were expecting it to be called"
  );

  if (expectStringInPage) {
    let found = await SpecialPowers.spawn(
      win.gBrowser.selectedBrowser,
      [expectStringInPage],
      async function (expectedString) {
        const netErrorCard = content.document.querySelector("net-error-card");
        if (netErrorCard) {
          const card = netErrorCard.wrappedJSObject;
          await card.getUpdateComplete();
          return card.errorInfo.errorCodeString === expectedString;
        }
        return content.document.body.textContent.includes(expectedString);
      }
    );
    Assert.ok(
      found,
      `page should reference the string '${expectStringInPage}'`
    );
  }

  await win.close();

  // This clears the TLS session cache so we don't use a previously-established
  // ticket to connect and bypass selecting a client auth certificate in
  // subsequent tests.
  sdr.logout();
}

// Test that if a certificate is chosen automatically the connection succeeds,
// and that nsIClientAuthDialogService.chooseCertificate() is never called.
add_task(async function testCertChosenAutomatically() {
  gClientAuthDialogService.state = DialogState.ASSERT_NOT_CALLED;
  await testHelper(
    "Select Automatically",
    "https://requireclientcert.example.com/",
    "https://requireclientcert.example.com/",
    false
  );
  // This clears all saved client auth certificate state so we don't influence
  // subsequent tests.
  cars.clearRememberedDecisions();
});

// Test that if the user doesn't choose a certificate, the connection fails and
// an error page is displayed.
add_task(async function testCertNotChosenByUser() {
  gClientAuthDialogService.state = DialogState.RETURN_CERT_NOT_SELECTED;
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    undefined,
    true,
    undefined,
    // bug 1818556: ssltunnel doesn't behave as expected here on Windows
    AppConstants.platform != "win"
      ? "SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT"
      : undefined
  );
  cars.clearRememberedDecisions();
});

// Test that if the user chooses a certificate the connection suceeeds.
add_task(async function testCertChosenByUser() {
  gClientAuthDialogService.state = DialogState.RETURN_CERT_SELECTED;
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    "https://requireclientcert.example.com/",
    true
  );
  cars.clearRememberedDecisions();
});

// Test that the cancel decision is remembered correctly
add_task(async function testEmptyCertChosenByUser() {
  gClientAuthDialogService.state = DialogState.RETURN_CERT_NOT_SELECTED;
  gClientAuthDialogService.rememberClientAuthCertificate = true;
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    undefined,
    true
  );
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    undefined,
    false
  );
  cars.clearRememberedDecisions();
});

// Test that if the user chooses a certificate in a private browsing window,
// configures Firefox to remember this certificate for the duration of the
// session, closes that window (and thus all private windows), reopens a private
// window, and visits that site again, they are re-asked for a certificate (i.e.
// any state from the previous private session should be gone). Similarly, after
// closing that private window, if the user opens a non-private window, they
// again should be asked to choose a certificate (i.e. private state should not
// be remembered/used in non-private contexts).
add_task(async function testClearPrivateBrowsingState() {
  gClientAuthDialogService.rememberClientAuthCertificate = true;
  gClientAuthDialogService.state = DialogState.RETURN_CERT_SELECTED;
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    "https://requireclientcert.example.com/",
    true,
    {
      private: true,
    }
  );
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    "https://requireclientcert.example.com/",
    true,
    {
      private: true,
    }
  );
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    "https://requireclientcert.example.com/",
    true
  );
  // NB: we don't `cars.clearRememberedDecisions()` in between the two calls to
  // `testHelper` because that would clear all client auth certificate state and
  // obscure what we're testing (that Firefox properly clears the relevant state
  // when the last private window closes).
  cars.clearRememberedDecisions();
});

// Test that 3rd party certificates are taken into account when filtering client
// certificates based on the acceptible CA list sent by the server.
add_task(async function testCertFilteringWithIntermediate() {
  let intermediateBytes = await IOUtils.readUTF8(
    getTestFilePath("intermediate.pem")
  ).then(
    pem => {
      let base64 = pemToBase64(pem);
      let bin = atob(base64);
      let bytes = [];
      for (let i = 0; i < bin.length; i++) {
        bytes.push(bin.charCodeAt(i));
      }
      return bytes;
    },
    error => {
      throw error;
    }
  );
  let nssComponent = Cc["@mozilla.org/psm;1"].getService(Ci.nsINSSComponent);
  nssComponent.addEnterpriseIntermediate(intermediateBytes);
  gExpectedClientCertificateChoices = 4;
  gClientAuthDialogService.state = DialogState.RETURN_CERT_SELECTED;
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert.example.com/",
    "https://requireclientcert.example.com/",
    true
  );
  cars.clearRememberedDecisions();
  // Changing this pref resets the added intermediate. Whichever of the two
  // changes sets it to true re-imports the OS enterprise roots on a background
  // task, which would drop an intermediate added by a later run if it finished
  // after it.
  let imported = TestUtils.topicObserved("psm:enterprise-certs-imported");
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "security.enterprise_roots.enabled",
        !Services.prefs.getBoolPref("security.enterprise_roots.enabled"),
      ],
    ],
  });
  await SpecialPowers.popPrefEnv();
  await imported;
});

// Test that if the server certificate does not validate successfully,
// nsIClientAuthDialogService.chooseCertificate() is never called.
add_task(async function testNoDialogForUntrustedServerCertificate() {
  gClientAuthDialogService.state = DialogState.ASSERT_NOT_CALLED;
  await testHelper(
    "Ask Every Time",
    "https://requireclientcert-untrusted.example.com/",
    undefined,
    false
  );
  // This clears all saved client auth certificate state so we don't influence
  // subsequent tests.
  cars.clearRememberedDecisions();
});
