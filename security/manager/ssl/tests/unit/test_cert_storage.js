/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// This test checks a number of things:
// * it ensures that data loaded from revocations.txt on startup is present
// * it ensures that data served from OneCRL are persisted correctly
// * it ensures that items in the CertBlocklist are seen as revoked by the
//   cert verifier
// * it does a sanity check to ensure other cert verifier behavior is
//   unmodified

const { RemoteSecuritySettings } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/RemoteSecuritySettings.sys.mjs"
);

// First, we need to setup appInfo for the blocklist service to work
var id = "xpcshell@tests.mozilla.org";
var appName = "XPCShell";
var version = "1";
var platformVersion = "1.9.2";
const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);
updateAppInfo({
  name: appName,
  ID: id,
  version,
  platformVersion: platformVersion ? platformVersion : "1.0",
  crashReporter: true,
});

// we need to ensure we setup revocation data before certDB, or we'll start with
// no revocation.txt in the profile
var gProfile = do_get_profile();

var gRevocations = gProfile.clone();
gRevocations.append("revocations.txt");
if (!gRevocations.exists()) {
  let existing = do_get_file("test_onecrl/sample_revocations.txt", false);
  existing.copyTo(gProfile, "revocations.txt");
}

var certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

const certBlocklist = [
  // test with some bad data ...
  {
    issuerName: "Some nonsense in issuer",
    serialNumber: "AkHVNA==",
  },
  {
    issuerName: "MA0xCzAJBgNVBAMMAmNh",
    serialNumber: "some nonsense in serial",
  },
  {
    issuerName: "and serial",
    serialNumber: "some nonsense in both issuer",
  },
  // some mixed
  // In these case, the issuer name and the valid serialNumber correspond
  // to test-int.pem in bad_certs/
  {
    issuerName: "MBIxEDAOBgNVBAMMB1Rlc3QgQ0E=",
    serialNumber: "oops! more nonsense.",
  },
  {
    issuerName: "MBIxEDAOBgNVBAMMB1Rlc3QgQ0E=",
    serialNumber: "ZSuNTV5Ngi1A5RSkCkCRd6fZ3sM=",
  },
  // ... and some good
  // In this case, the issuer name and the valid serialNumber correspond
  // to other-test-ca.pem in bad_certs/ (for testing root revocation)
  {
    issuerName: "MBgxFjAUBgNVBAMMDU90aGVyIHRlc3QgQ0E=",
    serialNumber: "adOFCYMS/nqQ3tCvQzINT8fdE+s=",
  },
  // These items correspond to an entry in sample_revocations.txt where:
  // isser name is the base-64 encoded subject DN for the shared Test
  // Intermediate and the serialNumbers are base-64 encoded 78 and 31,
  // respectively.
  // We need this to ensure that existing items are retained if they're
  // also in the blocklist
  {
    issuerName: "MBwxGjAYBgNVBAMMEVRlc3QgSW50ZXJtZWRpYXRl",
    serialNumber: "Tg==",
  },
  {
    issuerName: "MBwxGjAYBgNVBAMMEVRlc3QgSW50ZXJtZWRpYXRl",
    serialNumber: "Hw==",
  },
  // This item revokes same-issuer-ee.pem by subject and pubKeyHash.
  {
    subject: "MCIxIDAeBgNVBAMMF0Fub3RoZXIgVGVzdCBFbmQtZW50aXR5",
    pubKeyHash: "VCIlmPM9NkgFQtrs4Oa5TeFcDu6MWRTKSNdePEhOgD8=",
  },
];

function verify_cert(file, expectedError) {
  let ee = constructCertFromFile(file);
  return checkCertErrorGeneric(
    certDB,
    ee,
    expectedError,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
}

// The certificate blocklist currently only applies to TLS server certificates.
async function verify_non_tls_usage_succeeds(file) {
  let ee = constructCertFromFile(file);
  await checkCertErrorGeneric(
    certDB,
    ee,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSClient
  );
  await checkCertErrorGeneric(
    certDB,
    ee,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageEmailSigner
  );
  await checkCertErrorGeneric(
    certDB,
    ee,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageEmailRecipient
  );
}

function load_cert(cert, trust) {
  let file = "bad_certs/" + cert + ".pem";
  addCertFromFile(certDB, file, trust);
}

async function update_blocklist() {
  const { OneCRLBlocklistClient } = RemoteSecuritySettings.init();

  const fakeEvent = {
    current: certBlocklist, // with old .txt revocations.
    deleted: [],
    created: certBlocklist, // with new cert storage.
    updated: [],
  };
  await OneCRLBlocklistClient.emit("sync", { data: fakeEvent });
  // Save the last check timestamp, used by cert_storage to assert
  // if the blocklist is «fresh».
  Services.prefs.setIntPref(
    OneCRLBlocklistClient.lastCheckTimePref,
    Math.floor(Date.now() / 1000)
  );
}

function run_test() {
  // import the certificates we need
  load_cert("test-ca", "CTu,CTu,CTu");
  load_cert("test-int", ",,");
  load_cert("other-test-ca", "CTu,CTu,CTu");

  add_task(async function () {
    // check some existing items in revocations.txt are blocked.
    // This test corresponds to:
    // issuer: MBIxEDAOBgNVBAMMB1Rlc3QgQ0E= (CN=Test CA)
    // serial: Kg== (42)
    let file = "test_onecrl/ee-revoked-by-revocations-txt.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // This test corresponds to:
    // issuer: MBwxGjAYBgNVBAMMEVRlc3QgSW50ZXJtZWRpYXRl (CN=Test Intermediate)
    // serial: Tg== (78)
    file = "test_onecrl/another-ee-revoked-by-revocations-txt.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // And this test corresponds to:
    // issuer: MBwxGjAYBgNVBAMMEVRlc3QgSW50ZXJtZWRpYXRl (CN=Test Intermediate)
    // serial: Hw== (31)
    // (we test this issuer twice to ensure we can read multiple serials)
    file = "test_onecrl/another-ee-revoked-by-revocations-txt-serial-2.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // Test that a certificate revoked by subject and public key hash in
    // revocations.txt is revoked
    // subject: MCsxKTAnBgNVBAMMIEVFIFJldm9rZWQgQnkgU3ViamVjdCBhbmQgUHViS2V5
    // (CN=EE Revoked By Subject and PubKey)
    // pubkeyhash: VCIlmPM9NkgFQtrs4Oa5TeFcDu6MWRTKSNdePEhOgD8= (this is the
    // shared RSA SPKI)
    file = "test_onecrl/ee-revoked-by-subject-and-pubkey.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // Soon we'll load a blocklist which revokes test-int.pem, which issued
    // test-int-ee.pem.
    // Check the cert validates before we load the blocklist
    file = "test_onecrl/test-int-ee.pem";
    await verify_cert(file, PRErrorCodeSuccess);

    // The blocklist also revokes other-test-ca.pem, which issued
    // other-ca-ee.pem. Check the cert validates before we load the blocklist
    file = "bad_certs/other-issuer-ee.pem";
    await verify_cert(file, PRErrorCodeSuccess);

    // The blocklist will revoke same-issuer-ee.pem via subject / pubKeyHash.
    // Check the cert validates before we load the blocklist
    file = "test_onecrl/same-issuer-ee.pem";
    await verify_cert(file, PRErrorCodeSuccess);
  });

  // blocklist load is async so we must use add_test from here
  add_task(update_blocklist);

  add_task(async function () {
    // The blocklist will be loaded now. Let's check the data is sane.
    // In particular, we should still have the revoked issuer / serial pair
    // that was in revocations.txt but not the blocklist.
    let file = "test_onecrl/ee-revoked-by-revocations-txt.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // We should also still have the revoked issuer / serial pairs that were in
    // revocations.txt and are also in the blocklist.
    file = "test_onecrl/another-ee-revoked-by-revocations-txt.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);
    file = "test_onecrl/another-ee-revoked-by-revocations-txt-serial-2.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // The cert revoked by subject and pubkeyhash should still be revoked.
    file = "test_onecrl/ee-revoked-by-subject-and-pubkey.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);

    // Check the blocklisted intermediate now causes a failure
    file = "test_onecrl/test-int-ee.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);
    await verify_non_tls_usage_succeeds(file);

    // Check the ee with the blocklisted root also causes a failure
    file = "bad_certs/other-issuer-ee.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);
    await verify_non_tls_usage_succeeds(file);

    // Check the ee blocked by subject / pubKey causes a failure
    file = "test_onecrl/same-issuer-ee.pem";
    await verify_cert(file, SEC_ERROR_REVOKED_CERTIFICATE);
    await verify_non_tls_usage_succeeds(file);

    // Check a non-blocklisted chain still validates OK
    file = "bad_certs/default-ee.pem";
    await verify_cert(file, PRErrorCodeSuccess);

    // Check a bad cert is still bad (unknown issuer)
    file = "bad_certs/unknownissuer.pem";
    await verify_cert(file, SEC_ERROR_UNKNOWN_ISSUER);
  });

  run_next_test();
}
