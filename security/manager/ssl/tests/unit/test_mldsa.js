/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Tests that security.tls.enable_mldsa controls whether ML-DSA is used in
// TLS, and that NSS can read the ML-DSA certificates that pycert generates.

// Certificate file, DER encoding of the ML-DSA parameter set object
// identifier (2.16.840.1.101.3.4.3.{17,18,19}) and expected subject public
// key info length, for each parameter set.
const MLDSA_CERTS = [
  ["test_mldsa/default-ee.pem", "0609608648016503040311", 1334],
  ["test_mldsa/mldsa65-ee.pem", "0609608648016503040312", 1974],
  ["test_mldsa/mldsa87-ee.pem", "0609608648016503040313", 2614],
];

add_setup(async function () {
  do_get_profile();
  // The only end-entity certificate this server has is an ML-DSA-44 one, so
  // it has no signature algorithm to offer a client that doesn't do ML-DSA.
  await asyncStartTLSTestServer("BadCertAndPinningServer", "test_mldsa");
});

add_task(function test_mldsa_certificates_are_readable() {
  for (let [filename, oid, spkiLength] of MLDSA_CERTS) {
    let spki = constructCertFromFile(filename).subjectPublicKeyInfo;
    Assert.equal(
      spki.length,
      spkiLength,
      `${filename} should have a ${spkiLength} byte subjectPublicKeyInfo`
    );
    // The algorithm identifier is the first thing in the subject public key
    // info, and both of the enclosing SEQUENCE lengths are in long form.
    let encoded = spki
      .slice(6, 6 + oid.length / 2)
      .map(b => b.toString(16).padStart(2, "0"))
      .join("");
    Assert.equal(
      encoded,
      oid,
      `${filename} should identify its ML-DSA parameter set`
    );
  }
});

add_task(async function test_mldsa_disabled() {
  clearSessionCache();
  Services.prefs.setBoolPref("security.tls.enable_mldsa", false);
  // NSS reports the handshake_failure alert that the server sends when it
  // can't find a usable certificate as SSL_ERROR_NO_CYPHER_OVERLAP.
  await asyncConnectTo("ee.example.com", SSL_ERROR_NO_CYPHER_OVERLAP);
});

add_task(async function test_mldsa_enabled() {
  clearSessionCache();
  Services.prefs.setBoolPref("security.tls.enable_mldsa", true);
  await asyncConnectTo("ee.example.com", PRErrorCodeSuccess);
});
