/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef pq_pkcs8_h__
#define pq_pkcs8_h__

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

#include "secasn1.h"
#include "secoid.h"

namespace nss_test {

// Hand-built OneAsymmetricKey blobs for the post-quantum algorithms. ML-DSA
// and ML-KEM share one encoding here, as they do in NSS: the same
// SECKEY_PQPrivateKey*Template, the same tag dispatch in
// lib/pk11wrap/pk11pk12.c, and the same templates in lib/softoken/lowkey.c.
//
// These build blobs that NSS's own encoder would not produce, which is the
// point -- they are for driving the decoders and the consistency checks behind
// them.

// Append a DER tag and length, then the value.
inline void DerTlv(std::vector<uint8_t>* out, uint8_t tag, const uint8_t* value,
                   size_t len) {
  out->push_back(tag);
  if (len < 0x80) {
    out->push_back(static_cast<uint8_t>(len));
  } else if (len < 0x100) {
    out->push_back(0x81);
    out->push_back(static_cast<uint8_t>(len));
  } else {
    ASSERT_LT(len, 0x10000u);
    out->push_back(0x82);
    out->push_back(static_cast<uint8_t>(len >> 8));
    out->push_back(static_cast<uint8_t>(len));
  }
  out->insert(out->end(), value, value + len);
}

// The contents of an AlgorithmIdentifier: the OID, then |parameters| as raw
// DER. RFC 9881 (ML-DSA) and RFC 9935 (ML-KEM) require the parameters to be
// absent, so non-empty |parameters| build an invalid key.
inline std::vector<uint8_t> PqAlgorithmIdentifier(
    SECOidTag oid, const std::vector<uint8_t>& parameters) {
  SECOidData* oidData = SECOID_FindOIDByTag(oid);
  EXPECT_TRUE(oidData);
  if (!oidData) {
    return std::vector<uint8_t>();
  }

  std::vector<uint8_t> algorithm;
  DerTlv(&algorithm, SEC_ASN1_OBJECT_ID, oidData->oid.data, oidData->oid.len);
  algorithm.insert(algorithm.end(), parameters.begin(), parameters.end());
  return algorithm;
}

/* Wrap an already encoded private key CHOICE in a OneAsymmetricKey:
 *
 *   SEQUENCE {
 *     INTEGER 0,
 *     SEQUENCE { OBJECT IDENTIFIER <alg>, <parameters> },
 *     OCTET STRING { <choice> }
 *   }
 *
 * Build the CHOICE with PqSeedChoice, PqExpandedKeyChoice or PqBothChoice.
 * `parameters` is raw DER and is empty for a valid key. */
inline std::vector<uint8_t> BuildPqPkcs8(
    SECOidTag oid, const std::vector<uint8_t>& choice,
    const std::vector<uint8_t>& parameters = {}) {
  std::vector<uint8_t> algorithm = PqAlgorithmIdentifier(oid, parameters);
  if (algorithm.empty()) {
    return std::vector<uint8_t>();
  }

  std::vector<uint8_t> body;
  const uint8_t version = 0;
  DerTlv(&body, SEC_ASN1_INTEGER, &version, sizeof(version));
  DerTlv(&body, SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE, algorithm.data(),
         algorithm.size());
  DerTlv(&body, SEC_ASN1_OCTET_STRING, choice.data(), choice.size());

  std::vector<uint8_t> pkcs8;
  DerTlv(&pkcs8, SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE, body.data(),
         body.size());
  return pkcs8;
}

// SEQUENCE { AlgorithmIdentifier, BIT STRING { <publicKey> } }
inline std::vector<uint8_t> BuildPqSpki(
    SECOidTag oid, const std::vector<uint8_t>& publicKey,
    const std::vector<uint8_t>& parameters = {}) {
  std::vector<uint8_t> algorithm = PqAlgorithmIdentifier(oid, parameters);
  if (algorithm.empty()) {
    return std::vector<uint8_t>();
  }

  std::vector<uint8_t> bits(1, 0);  // no unused bits
  bits.insert(bits.end(), publicKey.begin(), publicKey.end());

  std::vector<uint8_t> body;
  DerTlv(&body, SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE, algorithm.data(),
         algorithm.size());
  DerTlv(&body, SEC_ASN1_BIT_STRING, bits.data(), bits.size());

  std::vector<uint8_t> spki;
  DerTlv(&spki, SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE, body.data(),
         body.size());
  return spki;
}

// seed [0] IMPLICIT OCTET STRING
inline std::vector<uint8_t> PqSeedChoice(const std::vector<uint8_t>& seed) {
  std::vector<uint8_t> choice;
  DerTlv(&choice, SEC_ASN1_CONTEXT_SPECIFIC | 0, seed.data(), seed.size());
  return choice;
}

// expandedKey OCTET STRING
inline std::vector<uint8_t> PqExpandedKeyChoice(
    const std::vector<uint8_t>& key) {
  std::vector<uint8_t> choice;
  DerTlv(&choice, SEC_ASN1_OCTET_STRING, key.data(), key.size());
  return choice;
}

// both SEQUENCE { OCTET STRING seed, OCTET STRING expandedKey }
inline std::vector<uint8_t> PqBothChoice(const std::vector<uint8_t>& seed,
                                         const std::vector<uint8_t>& key) {
  std::vector<uint8_t> inner;
  DerTlv(&inner, SEC_ASN1_OCTET_STRING, seed.data(), seed.size());
  DerTlv(&inner, SEC_ASN1_OCTET_STRING, key.data(), key.size());

  std::vector<uint8_t> choice;
  DerTlv(&choice, SEC_ASN1_CONSTRUCTED | SEC_ASN1_SEQUENCE, inner.data(),
         inner.size());
  return choice;
}

}  // namespace nss_test

#endif  // pq_pkcs8_h__
