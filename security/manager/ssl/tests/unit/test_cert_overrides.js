// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
"use strict";

// Tests the certificate overrides we allow.
// each certOverrideTest will do the following:
// 1. Attempt to connect to the given host. This should fail with the
//    given error.
// 2. Add an override for that host/port/certificate.
// 3. Connect again. This should succeed.

do_get_profile();
let gCertDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

// Enable the collection (during test) for all products so even products
// that don't collect the data will be able to run the test without failure.
Services.prefs.setBoolPref(
  "toolkit.telemetry.testing.overrideProductsCheck",
  true
);

function check_telemetry() {
  let histogram = Glean.ssl.certErrorOverrides.testGetValue();
  equal(histogram.values[0] || 0, 0, "Should have 0 unclassified values");
  equal(
    histogram.values[2],
    9,
    "Actual and expected SEC_ERROR_UNKNOWN_ISSUER values should match"
  );
  equal(
    histogram.values[3],
    1,
    "Actual and expected SEC_ERROR_CA_CERT_INVALID values should match"
  );
  equal(
    histogram.values[4] || 0,
    0,
    "Actual and expected SEC_ERROR_UNTRUSTED_ISSUER values should match"
  );
  equal(
    histogram.values[5],
    1,
    "Actual and expected SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE values should match"
  );
  equal(
    histogram.values[6] || 0,
    0,
    "Actual and expected SEC_ERROR_UNTRUSTED_CERT values should match"
  );
  equal(
    histogram.values[7] || 0,
    0,
    "Actual and expected SEC_ERROR_INADEQUATE_KEY_USAGE values should match"
  );
  equal(
    histogram.values[8],
    2,
    "Actual and expected SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED values should match"
  );
  equal(
    histogram.values[9],
    gIsDebugBuild ? 9 : 8,
    "Actual and expected SSL_ERROR_BAD_CERT_DOMAIN values should match"
  );
  equal(
    histogram.values[10],
    1,
    "Actual and expected SEC_ERROR_EXPIRED_CERTIFICATE values should match"
  );
  equal(
    histogram.values[11],
    2,
    "Actual and expected MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY values should match"
  );
  equal(
    histogram.values[12],
    1,
    "Actual and expected MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA values should match"
  );
  equal(
    histogram.values[13],
    1,
    "Actual and expected MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE values should match"
  );
  equal(
    histogram.values[14],
    1,
    "Actual and expected MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE values should match"
  );
  equal(
    histogram.values[15],
    1,
    "Actual and expected MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE values should match"
  );
  equal(
    histogram.values[16],
    2,
    "Actual and expected SEC_ERROR_INVALID_TIME values should match"
  );
  equal(
    histogram.values[17],
    1,
    "Actual and expected MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME values should match"
  );
  equal(
    histogram.values[19],
    4,
    "Actual and expected MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT values should match"
  );
  equal(
    histogram.values[20],
    1,
    "Actual and expected MOZILLA_PKIX_ERROR_MITM_DETECTED values should match"
  );

  let keySizeHistogram = Glean.cert.chainKeySizeStatus.testGetValue();
  equal(
    keySizeHistogram.values[0] || 0,
    0,
    "Actual and expected unchecked key size values should match"
  );
  equal(
    keySizeHistogram.values[1],
    gIsDebugBuild ? 17 : 15,
    "Actual and expected successful verifications of 2048-bit keys should match"
  );
  equal(
    keySizeHistogram.values[2] || 0,
    0,
    "Actual and expected successful verifications of 1024-bit keys should match"
  );
  equal(
    keySizeHistogram.values[3],
    68,
    "Actual and expected verification failures unrelated to key size should match"
  );
}

// Internally, specifying "port" -1 is the same as port 443. This tests that.
function run_port_equivalency_test(inPort, outPort) {
  Assert.ok(
    (inPort == 443 && outPort == -1) || (inPort == -1 && outPort == 443),
    "The two specified ports must be -1 and 443 (in any order)"
  );
  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  let cert = constructCertFromFile("bad_certs/default-ee.pem");
  let expectedTemporary = true;
  certOverrideService.rememberValidityOverride(
    "example.com",
    inPort,
    {},
    cert,
    expectedTemporary
  );
  let actualTemporary = {};
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "example.com",
      outPort,
      {},
      cert,
      actualTemporary
    ),
    `override set on port ${inPort} should match port ${outPort}`
  );
  equal(
    actualTemporary.value,
    expectedTemporary,
    "input override temporary value should match output temporary value"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride("example.com", 563, {}, cert, {}),
    `override set on port ${inPort} should not match port 563`
  );
  certOverrideService.clearValidityOverride("example.com", inPort, {});
  Assert.ok(
    !certOverrideService.hasMatchingOverride(
      "example.com",
      outPort,
      {},
      cert,
      {}
    ),
    `override cleared on port ${inPort} should match port ${outPort}`
  );
}

add_task(async function run_test() {
  Services.fog.initializeFOG();
  Services.fog.testResetFOG();

  run_port_equivalency_test(-1, 443);
  run_port_equivalency_test(443, -1);

  Services.prefs.setIntPref("security.OCSP.enabled", 1);

  await asyncStartTLSTestServer("BadCertAndPinningServer", "bad_certs", true);

  let fakeOCSPResponder = new HttpServer();
  fakeOCSPResponder.registerPrefixHandler("/", function (request, response) {
    response.setStatusLine(request.httpVersion, 500, "Internal Server Error");
  });
  fakeOCSPResponder.start(8888);

  await run_simple_tests();
  await run_localhost_tests();
  await run_combo_tests();
  await run_distrust_tests();

  await new Promise(resolve => {
    fakeOCSPResponder.stop(resolve);
  });

  await check_telemetry();
});

async function run_simple_tests() {
  await certOverrideTest("expired.example.com", SEC_ERROR_EXPIRED_CERTIFICATE);
  await certOverrideTest(
    "notyetvalid.example.com",
    MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE
  );
  await certOverrideTest("before-epoch.example.com", SEC_ERROR_INVALID_TIME);
  await certOverrideTest(
    "before-epoch-self-signed.example.com",
    MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT
  );
  await certOverrideTest(
    "selfsigned.example.com",
    MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT
  );
  await certOverrideTest("unknownissuer.example.com", SEC_ERROR_UNKNOWN_ISSUER);
  await certOverrideTest(
    "expiredissuer.example.com",
    SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE
  );
  await certOverrideTest(
    "notyetvalidissuer.example.com",
    MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE
  );
  await certOverrideTest(
    "before-epoch-issuer.example.com",
    SEC_ERROR_INVALID_TIME
  );
  await certOverrideTest(
    "md5signature.example.com",
    SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  );
  await certOverrideTest(
    "emptyissuername.example.com",
    MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME
  );
  // This has name information in the subject alternative names extension,
  // but not the subject common name.
  await certOverrideTest("mismatch.example.com", SSL_ERROR_BAD_CERT_DOMAIN);
  // This has name information in the subject common name but not the subject
  // alternative names extension.
  await certOverrideTest("mismatch-CN.example.com", SSL_ERROR_BAD_CERT_DOMAIN);

  // A Microsoft IIS utility generates self-signed certificates with
  // properties similar to the one this "host" will present.
  await certOverrideTest(
    "selfsigned-inadequateEKU.example.com",
    MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT
  );

  await preventedCertOverrideTest(
    "inadequatekeyusage.example.com",
    SEC_ERROR_INADEQUATE_KEY_USAGE
  );

  // Test triggering the MitM detection. We don't set-up a proxy here. Just
  // set the pref. Without the pref set we expect an unkown issuer error.
  await certOverrideTest("mitm.example.com", SEC_ERROR_UNKNOWN_ISSUER);
  Services.prefs.setStringPref(
    "security.pki.mitm_canary_issuer",
    "CN=Test MITM Root"
  );
  let certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  certOverrideService.clearValidityOverride("mitm.example.com", 8443, {});
  await certOverrideTest("mitm.example.com", MOZILLA_PKIX_ERROR_MITM_DETECTED);
  Services.prefs.setStringPref(
    "security.pki.mitm_canary_issuer",
    "CN=Other MITM Root"
  );
  certOverrideService.clearValidityOverride("mitm.example.com", 8443, {});
  // If the canary issuer doesn't match the one we see, we exepct and unknown
  // issuer error.
  await certOverrideTest("mitm.example.com", SEC_ERROR_UNKNOWN_ISSUER);
  // If security.pki.mitm_canary_issuer.enabled is false, there should always
  // be an unknown issuer error.
  Services.prefs.setBoolPref("security.pki.mitm_canary_issuer.enabled", false);
  certOverrideService.clearValidityOverride("mitm.example.com", 8443, {});
  await certOverrideTest("mitm.example.com", SEC_ERROR_UNKNOWN_ISSUER);
  Services.prefs.clearUserPref("security.pki.mitm_canary_issuer");

  // The issuer is untrusted, which is overridable, but NSS rejects
  // certificates with unknown critical extensions while parsing them, before
  // PSM verifies them, so the non-overridable error is reported.
  let rootCert = addCertFromFile(gCertDB, "bad_certs/test-ca.pem", ",,");
  await preventedCertOverrideTest(
    "nsCertTypeCritical.example.com",
    SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION
  );
  setCertTrust(rootCert, "CTu,,");

  // Bug 990603: Apache documentation has recommended generating a self-signed
  // test certificate with basic constraints: CA:true. For compatibility, this
  // is a scenario in which an override is allowed.
  await certOverrideTest(
    "self-signed-end-entity-with-cA-true.example.com",
    MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT
  );

  await certOverrideTest(
    "ca-used-as-end-entity.example.com",
    MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY
  );

  // If an X.509 version 1 certificate is not a trust anchor, we will
  // encounter an overridable error.
  await certOverrideTest(
    "end-entity-issued-by-v1-cert.example.com",
    MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA
  );
  // Clear the override.
  certOverrideService.clearValidityOverride(
    "end-entity-issued-by-v1-cert.example.com",
    8443,
    {}
  );
  // If we make that certificate a trust anchor, the connection will succeed.
  let v1Cert = addCertFromFile(gCertDB, "bad_certs/v1Cert.pem", "CTu,,");
  clearSessionCache();
  await asyncConnectTo(
    "end-entity-issued-by-v1-cert.example.com",
    PRErrorCodeSuccess
  );
  // Reset the trust for that certificate.
  setCertTrust(v1Cert, ",,");
  clearSessionCache();

  // Due to compatibility issues, we allow overrides for certificates issued by
  // certificates that are not valid CAs.
  await certOverrideTest(
    "end-entity-issued-by-non-CA.example.com",
    SEC_ERROR_CA_CERT_INVALID
  );

  // This host presents a 1016-bit RSA key.
  await certOverrideTest(
    "inadequate-key-size-ee.example.com",
    MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE
  );

  // The test root is not a built-in (by default), so the invalid dNSName entry
  // in the subject alternative name extension is skipped.
  await asyncConnectTo(
    "ipAddressAsDNSNameInSAN.example.com",
    PRErrorCodeSuccess
  );

  if (gIsDebugBuild) {
    // Treat the test root like a built-in.
    let rootCert = constructCertFromFile("bad_certs/test-ca.pem");
    Services.prefs.setCharPref(
      "security.test.built_in_root_hash",
      rootCert.sha256Fingerprint
    );
    // If the root is a built-in, the invalid dNSName entry in the subject
    // alternative name extension is not skipped, and this result in an error.
    await certOverrideTest(
      "ipAddressAsDNSNameInSAN.example.com",
      SSL_ERROR_BAD_CERT_DOMAIN
    );
    // Reset the test root's built-in status.
    Services.prefs.clearUserPref("security.test.built_in_root_hash");
  }

  await certOverrideTest("noValidNames.example.com", SSL_ERROR_BAD_CERT_DOMAIN);
  await certOverrideTest(
    "badSubjectAltNames.example.com",
    SSL_ERROR_BAD_CERT_DOMAIN
  );

  await certOverrideTest(
    "bug413909.xn--hxajbheg2az3al.xn--jxalpdlp",
    SEC_ERROR_UNKNOWN_ISSUER
  );
  // At this point, the override for bug413909.xn--hxajbheg2az3al.xn--jxalpdlp
  // is still valid. Do some additional tests relating to IDN handling.
  let uri = Services.io.newURI(
    "https://bug413909.xn--hxajbheg2az3al.xn--jxalpdlp"
  );
  let cert = constructCertFromFile("bad_certs/idn-certificate.pem");
  Assert.ok(
    certOverrideService.hasMatchingOverride(uri.asciiHost, 8443, {}, cert, {}),
    "IDN certificate should have matching override using ascii host"
  );
  Assert.throws(
    () =>
      !certOverrideService.hasMatchingOverride(
        uri.displayHost,
        8443,
        {},
        cert,
        {}
      ),
    /NS_ERROR_ILLEGAL_VALUE/,
    "IDN certificate should not have matching override using (non-ascii) host"
  );
  let invalidHost = uri.asciiHost.replace(/./g, c =>
    String.fromCharCode(c.charCodeAt(0) | 0x100)
  );
  Assert.throws(
    () =>
      !certOverrideService.hasMatchingOverride(invalidHost, 8443, {}, cert, {}),
    /NS_ERROR_ILLEGAL_VALUE/,
    "hasMatchingOverride should not truncate high-bytes"
  );

  // Add a bunch of overrides...
  cert = constructCertFromFile("bad_certs/default-ee.pem");
  certOverrideService.rememberValidityOverride(
    "example.com",
    443,
    {},
    cert,
    false
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride("example.com", 443, {}, cert, {}),
    "Should have added override for example.com:443"
  );
  certOverrideService.rememberValidityOverride(
    "example.com",
    80,
    {},
    cert,
    false
  );
  certOverrideService.rememberValidityOverride("::1", 80, {}, cert, false);
  Assert.ok(
    certOverrideService.hasMatchingOverride("example.com", 80, {}, cert, {}),
    "Should have added override for example.com:80"
  );
  certOverrideService.rememberValidityOverride(
    "example.org",
    443,
    {},
    cert,
    false
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride("example.org", 443, {}, cert, {}),
    "Should have added override for example.org:443"
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride("::1", 80, {}, cert, {}),
    "Should have added override for [::1]:80"
  );
  // When in a private browsing context, overrides added in non-private
  // contexts should match (but not vice-versa).
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "example.org",
      443,
      { privateBrowsingId: 1 },
      cert,
      {}
    ),
    "Should have override for example.org:443 with privateBrowsingId 1"
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "example.org",
      443,
      { privateBrowsingId: 2 },
      cert,
      {}
    ),
    "Should have override for example.org:443 with privateBrowsingId 2"
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "example.org",
      443,
      { firstPartyDomain: "example.org", userContextId: 1 },
      cert,
      {}
    ),
    "Should ignore firstPartyDomain and userContextId when checking overrides"
  );
  certOverrideService.rememberValidityOverride(
    "example.org",
    80,
    {},
    cert,
    true
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride("example.org", 80, {}, cert, {}),
    "Should have added override for example.org:80"
  );
  certOverrideService.rememberValidityOverride(
    "test.example.org",
    443,
    { firstPartyDomain: "example.org", userContextId: 1 },
    cert,
    false
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "test.example.org",
      443,
      {},
      cert,
      {}
    ),
    "Should ignore firstPartyDomain and userContextId when adding overrides"
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "test.example.org",
      443,
      { firstPartyDomain: "example.com", userContextId: 2 },
      cert,
      {}
    ),
    "Should ignore firstPartyDomain and userContextId when checking overrides"
  );
  certOverrideService.rememberValidityOverride(
    "example.test",
    443,
    { privateBrowsingId: 1 },
    cert,
    false
  );
  Assert.ok(
    certOverrideService.hasMatchingOverride(
      "example.test",
      443,
      { privateBrowsingId: 1 },
      cert,
      {}
    ),
    "Should have added override for example.test:443 with privateBrowsingId 1"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride(
      "example.test",
      443,
      { privateBrowsingId: 2 },
      cert,
      {}
    ),
    "Should not have override for example.test:443 with privateBrowsingId 2"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride("example.test", 443, {}, cert, {}),
    "Should not have override for example.test:443 with non-private OriginAttributes"
  );
  // Clear them all...
  certOverrideService.clearAllOverrides();

  // And ensure they're all gone.
  Assert.ok(
    !certOverrideService.hasMatchingOverride("example.com", 443, {}, cert, {}),
    "Should have removed override for example.com:443"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride("example.com", 80, {}, cert, {}),
    "Should have removed override for example.com:80"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride("example.org", 443, {}, cert, {}),
    "Should have removed override for example.org:443"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride("example.org", 80, {}, cert, {}),
    "Should have removed override for example.org:80"
  );
  Assert.ok(
    !certOverrideService.hasMatchingOverride(
      "example.org",
      443,
      { privateBrowsingId: 1 },
      cert,
      {}
    ),
    "Should have removed override for example.org:443 with privateBrowsingId 1"
  );
}

async function run_localhost_tests() {
  await certOverrideTest("localhost", SEC_ERROR_UNKNOWN_ISSUER);
  await certOverrideTest("127.0.0.1", SSL_ERROR_BAD_CERT_DOMAIN);
  await certOverrideTest("::1", SSL_ERROR_BAD_CERT_DOMAIN);
}

async function run_combo_tests() {
  await certOverrideTest(
    "mismatch-expired.example.com",
    SSL_ERROR_BAD_CERT_DOMAIN
  );
  await certOverrideTest(
    "mismatch-notYetValid.example.com",
    SSL_ERROR_BAD_CERT_DOMAIN
  );
  await certOverrideTest(
    "mismatch-untrusted.example.com",
    SEC_ERROR_UNKNOWN_ISSUER
  );
  await certOverrideTest(
    "untrusted-expired.example.com",
    SEC_ERROR_UNKNOWN_ISSUER
  );
  await certOverrideTest(
    "mismatch-untrusted-expired.example.com",
    SEC_ERROR_UNKNOWN_ISSUER
  );

  await certOverrideTest(
    "md5signature-expired.example.com",
    SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  );

  await certOverrideTest(
    "ca-used-as-end-entity-name-mismatch.example.com",
    MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY
  );
}

async function run_distrust_tests() {
  // Before we specifically distrust this certificate, it should be trusted.
  await asyncConnectTo("untrusted.example.com", PRErrorCodeSuccess);

  await run_distrust_test(
    "bad_certs/default-ee.pem",
    "untrusted.example.com",
    SEC_ERROR_UNTRUSTED_CERT
  );

  await run_distrust_test(
    "bad_certs/other-test-ca.pem",
    "untrustedissuer.example.com",
    SEC_ERROR_UNTRUSTED_ISSUER
  );

  await run_distrust_test(
    "bad_certs/test-ca.pem",
    "ca-used-as-end-entity.example.com",
    SEC_ERROR_UNTRUSTED_ISSUER
  );
}

async function run_distrust_test(certFileName, hostName, expectedResult) {
  // Add an entry to the NSS certDB that says to distrust the cert
  let certToDistrust = addCertFromFile(gCertDB, certFileName, "pu,,");
  clearSessionCache();
  await preventedCertOverrideTest(hostName, expectedResult);
  // Reset the cert's trust.
  setCertTrust(certToDistrust, "u,,");
}
