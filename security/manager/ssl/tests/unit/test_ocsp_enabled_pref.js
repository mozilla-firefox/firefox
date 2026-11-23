// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/
"use strict";

// Checks that the security.OCSP.enabled pref correctly controls OCSP fetching
// behavior.

do_get_profile(); // Must be called before getting nsIX509CertDB
const gCertDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

const SERVER_PORT = 8888;

function certFromFile(filename) {
  return constructCertFromFile(`test_ev_certs/${filename}.pem`);
}

function loadCert(certName, trustString) {
  addCertFromFile(gCertDB, `test_ev_certs/${certName}.pem`, trustString);
}

function getFailingOCSPResponder() {
  return getFailingHttpServer(SERVER_PORT, ["www.example.com"]);
}

function getOCSPResponder(expectedCertNames) {
  return startOCSPResponder(
    SERVER_PORT,
    "www.example.com",
    "test_ev_certs",
    expectedCertNames,
    []
  );
}

// Tests that in ocspOff mode, OCSP fetches are never done.
async function testOff() {
  Services.prefs.setIntPref("security.OCSP.enabled", 0);
  info("Setting security.OCSP.enabled to 0");

  // EV chains should verify successfully and get EV status.
  clearOCSPCache();
  let ocspResponder = getFailingOCSPResponder();
  await checkEVStatus(
    gCertDB,
    certFromFile("test-oid-path-ee"),
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    gEVExpected
  );
  await stopOCSPResponder(ocspResponder);

  // A DV chain should verify successfully.
  clearOCSPCache();
  ocspResponder = getFailingOCSPResponder();
  await checkCertErrorGeneric(
    gCertDB,
    certFromFile("non-ev-root-path-ee"),
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await stopOCSPResponder(ocspResponder);
}

// Tests that in ocspOn mode, OCSP fetches are done for both EV and DV certs.
async function testOn() {
  Services.prefs.setIntPref("security.OCSP.enabled", 1);
  info("Setting security.OCSP.enabled to 1");

  // EV chains should verify successfully and get EV status.
  clearOCSPCache();
  let ocspResponder = getOCSPResponder(["test-oid-path-ee"]);
  await checkEVStatus(
    gCertDB,
    certFromFile("test-oid-path-ee"),
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    gEVExpected
  );
  await stopOCSPResponder(ocspResponder);

  // If a successful OCSP response is fetched, then a DV chain should verify
  // successfully.
  clearOCSPCache();
  ocspResponder = getOCSPResponder(["non-ev-root-path-ee"]);
  await checkCertErrorGeneric(
    gCertDB,
    certFromFile("non-ev-root-path-ee"),
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await stopOCSPResponder(ocspResponder);
}

async function testCRLiteEnforced() {
  Services.prefs.setBoolPref("security.OCSP.require", false);
  info("Setting security.OCSP.require to false");

  Services.prefs.setIntPref("security.OCSP.enabled", 1);
  info("Setting security.OCSP.enabled to 1");

  Services.prefs.setIntPref("security.pki.crlite_mode", 2);
  info("Setting security.pki.crlite_mode to 2");

  // When CRLite is enforced, OCSP requests should be made for DV certs that do
  // not chain to a builtin root.
  clearOCSPCache();
  let ocspResponder = getOCSPResponder(["non-ev-root-path-ee"]);
  await checkCertErrorGeneric(
    gCertDB,
    certFromFile("non-ev-root-path-ee"),
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await stopOCSPResponder(ocspResponder);

  // The rest of the tests here use "security.test.built_in_root_hash", which
  // only works in debug builds.
  if (!AppConstants.DEBUG) {
    return;
  }

  // When CRLite is enforced and OCSP is not required, OCSP requests should not
  // be made for DV certs that chain to a builtin root.
  Services.prefs.setBoolPref("security.OCSP.require", false);
  info("Setting security.OCSP.require to false");
  let nonEVRootCert = certFromFile("non-evroot-ca");
  Services.prefs.setCharPref(
    "security.test.built_in_root_hash",
    nonEVRootCert.sha256Fingerprint
  );
  info(
    "Setting security.test.built_in_root_hash to " +
      nonEVRootCert.sha256Fingerprint
  );

  clearOCSPCache();
  ocspResponder = getOCSPResponder([]);
  await checkCertErrorGeneric(
    gCertDB,
    certFromFile("non-ev-root-path-ee"),
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await stopOCSPResponder(ocspResponder);

  // CRLite does not cover client or S/MIME certificates, so OCSP requests
  // should still be made for those that chain to a builtin root.
  for (let usage of [
    Ci.nsIX509CertDB.verifyUsageTLSClient,
    Ci.nsIX509CertDB.verifyUsageEmailSigner,
    Ci.nsIX509CertDB.verifyUsageEmailRecipient,
  ]) {
    clearOCSPCache();
    ocspResponder = getOCSPResponder(["non-ev-root-path-ee"]);
    await checkCertErrorGeneric(
      gCertDB,
      certFromFile("non-ev-root-path-ee"),
      PRErrorCodeSuccess,
      usage
    );
    await stopOCSPResponder(ocspResponder);
  }

  // When CRLite is enforced and OCSP is not required, OCSP requests should not
  // be made for EV certs that chain to a builtin root.
  Services.prefs.setBoolPref("security.OCSP.require", false);
  info("Setting security.OCSP.require to false");
  let evroot = certFromFile("evroot");
  Services.prefs.setCharPref(
    "security.test.built_in_root_hash",
    evroot.sha256Fingerprint
  );
  info(
    "Setting security.test.built_in_root_hash to " + evroot.sha256Fingerprint
  );

  clearOCSPCache();
  ocspResponder = getOCSPResponder([]);
  await checkEVStatus(
    gCertDB,
    certFromFile("test-oid-path-ee"),
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    gEVExpected
  );
  await stopOCSPResponder(ocspResponder);

  // When CRLite is enforced and OCSP is required, OCSP requests should be made
  // for DV certs.
  Services.prefs.setBoolPref("security.OCSP.require", true);
  info("Setting security.OCSP.require to true");
  clearOCSPCache();
  ocspResponder = getOCSPResponder(["non-ev-root-path-ee"]);
  await checkCertErrorGeneric(
    gCertDB,
    certFromFile("non-ev-root-path-ee"),
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await stopOCSPResponder(ocspResponder);

  // When CRLite is enforced and OCSP is required, OCSP requests should be made
  // for EV certs.
  Services.prefs.setBoolPref("security.OCSP.require", true);
  info("Setting security.OCSP.require to true");
  Services.prefs.setCharPref(
    "security.test.built_in_root_hash",
    evroot.sha256Fingerprint
  );
  info(
    "Setting security.test.built_in_root_hash to " + evroot.sha256Fingerprint
  );

  clearOCSPCache();
  ocspResponder = getOCSPResponder(["test-oid-path-ee"]);
  await checkEVStatus(
    gCertDB,
    certFromFile("test-oid-path-ee"),
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    gEVExpected
  );
  await stopOCSPResponder(ocspResponder);
}

// Tests that in ocspEVOnly mode, OCSP fetches are done for EV certs only.
async function testEVOnly() {
  Services.prefs.setIntPref("security.OCSP.enabled", 2);
  info("Setting security.OCSP.enabled to 2");

  // If a successful OCSP response is fetched, then an EV chain should verify
  // successfully and get EV status as well.
  clearOCSPCache();
  let ocspResponder = gEVExpected
    ? getOCSPResponder(["test-oid-path-ee"])
    : getFailingOCSPResponder();
  await checkEVStatus(
    gCertDB,
    certFromFile("test-oid-path-ee"),
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    gEVExpected
  );
  await stopOCSPResponder(ocspResponder);

  // A DV chain should verify successfully even without doing OCSP fetches.
  clearOCSPCache();
  ocspResponder = getFailingOCSPResponder();
  await checkCertErrorGeneric(
    gCertDB,
    certFromFile("non-ev-root-path-ee"),
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
  await stopOCSPResponder(ocspResponder);
}

add_task(async function () {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("security.OCSP.enabled");
    Services.prefs.clearUserPref("security.OCSP.require");
    Services.prefs.clearUserPref("security.pki.crlite_mode");
    Services.prefs.clearUserPref("security.test.built_in_root_hash");
  });
  Services.prefs.setCharPref("network.dns.localDomains", "www.example.com");
  // Enable hard fail to ensure chains that should only succeed because they get
  // a good OCSP response do not succeed due to soft fail leniency.
  Services.prefs.setBoolPref("security.OCSP.require", true);

  loadCert("evroot", "CTu,,");
  loadCert("test-oid-path-int", ",,");
  loadCert("non-evroot-ca", "CTu,C,");
  loadCert("non-ev-root-path-int", ",,");

  await testOff();
  await testOn();
  await testEVOnly();
  await testCRLiteEnforced();
});
