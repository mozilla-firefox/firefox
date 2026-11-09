// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Tests that starting a profile with a preexisting CRLite filter and stash
// works correctly. This is a copy of test_crlite_preexisting.js that uses
// tls-encoded filters instead of bincode-encoded filters. The tls encoding
// describes the same filter; only the on-disk serialization differs.

"use strict";

const CHECK_AT_TIME = new Date("2020-01-01T00:00:00Z").getTime() / 1000;

async function test_crlite_preexisting(ctMode) {
  Services.prefs.setIntPref(
    "security.pki.certificate_transparency.mode",
    ctMode
  );

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  let validCert = constructCertFromFile(
    "test_crlite_filters/valid.example.com.pem"
  );
  await checkCertErrorGenericAtTime(
    certdb,
    validCert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "valid.example.com",
    0
  );

  let revokedCert = constructCertFromFile(
    "test_crlite_filters/revoked.example.com.pem"
  );
  await checkCertErrorGenericAtTime(
    certdb,
    revokedCert,
    SEC_ERROR_REVOKED_CERTIFICATE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked.example.com",
    0
  );

  let revokedInDeltaCert = constructCertFromFile(
    "test_crlite_filters/revoked-in-delta.example.com.pem"
  );
  await checkCertErrorGenericAtTime(
    certdb,
    revokedInDeltaCert,
    SEC_ERROR_REVOKED_CERTIFICATE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked-in-delta.example.com",
    0
  );

  // This certificate has no embedded SCTs, but it should be considered revoked
  // if the appropriate SCT is side-loaded.
  let revokedNoSctCert = constructCertFromFile(
    "test_crlite_filters/revoked-no-sct.example.com.pem"
  );

  let sctFile = do_get_file(
    "test_crlite_filters/revoked-no-sct.example.com.sct",
    false
  );
  let sctBytes = readBinaryFile(sctFile);
  let sctList = new Uint8Array(2 + 2 + sctBytes.length);
  sctList[0] = (2 + sctBytes.length) / 256;
  sctList[1] = (2 + sctBytes.length) % 256;
  sctList[2] = sctBytes.length / 256;
  sctList[3] = sctBytes.length % 256;
  sctList.set(sctBytes, 4);

  await checkCertErrorGenericAtTime(
    certdb,
    revokedNoSctCert,
    PRErrorCodeSuccess,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked-no-sct.example.com",
    0,
    [] // no side-loaded SCTs
  );

  await checkCertErrorGenericAtTime(
    certdb,
    revokedNoSctCert,
    ctMode == CT_MODE_DISABLE
      ? PRErrorCodeSuccess
      : SEC_ERROR_REVOKED_CERTIFICATE,
    Ci.nsIX509CertDB.verifyUsageTLSServer,
    CHECK_AT_TIME,
    false,
    "revoked-no-sct.example.com",
    0,
    sctList
  );
}

add_task(async function () {
  Services.prefs.setIntPref(
    "security.pki.crlite_mode",
    CRLiteModeEnforcePrefValue
  );

  let securityStateDirectory = do_get_profile();
  securityStateDirectory.append("security_state");

  // For simplicity, re-use the tls-encoded filters from test_crlite_filters.js.
  do_get_file("test_crlite_filters/20200101-0-filter.tls").copyTo(
    securityStateDirectory,
    "crlite.filter"
  );

  do_get_file("test_crlite_filters/20200101-1-filter.delta.tls").copyTo(
    securityStateDirectory,
    "20201017-1-filter.delta"
  );

  let certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );

  // These need to be available for path building.
  let ca = addCertFromFile(certdb, "test_crlite_filters/ca.pem", "C,C,");
  ok(ca, "ca certificate should decode successfully");

  let issuerCert = addCertFromFile(certdb, "test_crlite_filters/int.pem", ",,");
  ok(issuerCert, "issuer certificate should decode successfully");

  // Mark CRLite filter as fresh
  await new Promise(resolve => {
    certStorage.testNoteCRLiteUpdateTime((rv, _) => {
      Assert.equal(rv, Cr.NS_OK, "marked filter as fresh");
      resolve();
    });
  });

  info(`testing with CT disabled`);
  await test_crlite_preexisting(CT_MODE_DISABLE);

  info(`testing with CT in telemetry only mode`);
  await test_crlite_preexisting(CT_MODE_COLLECT_TELEMETRY);

  info(`testing with CT enabled`);
  await test_crlite_preexisting(CT_MODE_ENFORCE);
});
