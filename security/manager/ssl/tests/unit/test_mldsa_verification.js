/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Tests verification of certificate chains that contain ML-DSA keys and
// signatures, and that security.tls.enable_mldsa controls whether they are
// accepted.

do_get_profile(); // must be called before getting nsIX509CertDB
const certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

const PARAMETER_SETS = ["mldsa44", "mldsa65", "mldsa87"];

function certFromFile(name) {
  return constructCertFromFile(`test_mldsa_verification/${name}.pem`);
}

function checkEndEntity(name, expectedError) {
  return checkCertErrorGeneric(
    certdb,
    certFromFile(name),
    expectedError,
    Ci.nsIX509CertDB.verifyUsageTLSServer
  );
}

add_setup(function () {
  for (let parameterSet of PARAMETER_SETS) {
    addCertFromFile(
      certdb,
      `test_mldsa_verification/root-${parameterSet}.pem`,
      "CTu,,"
    );
    addCertFromFile(
      certdb,
      `test_mldsa_verification/int-${parameterSet}.pem`,
      ",,"
    );
  }
  addCertFromFile(certdb, "test_mldsa_verification/root-rsa.pem", "CTu,,");
});

add_task(async function test_mldsa_enabled() {
  Services.prefs.setBoolPref("security.tls.enable_mldsa", true);

  for (let parameterSet of PARAMETER_SETS) {
    // A chain that is ML-DSA throughout.
    await checkEndEntity(
      `ee-${parameterSet}-int-${parameterSet}`,
      PRErrorCodeSuccess
    );
    // An ML-DSA end-entity key, signed with RSA.
    await checkEndEntity(`ee-${parameterSet}-root-rsa`, PRErrorCodeSuccess);
  }

  // Classical end-entity keys, signed with ML-DSA.
  await checkEndEntity("ee-ecdsa-int-mldsa65", PRErrorCodeSuccess);
  await checkEndEntity("ee-rsa-int-mldsa87", PRErrorCodeSuccess);

  // The signature algorithm names ML-DSA-44, but the issuer's key is
  // ML-DSA-65.
  await checkEndEntity(
    "ee-wrong-parameter-set-int-mldsa65",
    MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH
  );
  // The signature algorithm is ECDSA, but the issuer's key is ML-DSA-65.
  await checkEndEntity(
    "ee-ecdsa-algorithm-int-mldsa65",
    SEC_ERROR_BAD_SIGNATURE
  );
});

add_task(async function test_mldsa_disabled() {
  Services.prefs.setBoolPref("security.tls.enable_mldsa", false);

  for (let parameterSet of PARAMETER_SETS) {
    await checkEndEntity(
      `ee-${parameterSet}-int-${parameterSet}`,
      SEC_ERROR_UNSUPPORTED_KEYALG
    );
    await checkEndEntity(
      `ee-${parameterSet}-root-rsa`,
      SEC_ERROR_UNSUPPORTED_KEYALG
    );
  }
  await checkEndEntity("ee-ecdsa-int-mldsa65", SEC_ERROR_UNSUPPORTED_KEYALG);
  await checkEndEntity("ee-rsa-int-mldsa87", SEC_ERROR_UNSUPPORTED_KEYALG);
});
