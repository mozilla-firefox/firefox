/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "cert.h"
#include "certdb.h"
#include "der_encode.h"
#include "nss_scoped_ptrs.h"
#include "prerror.h"
#include "secerr.h"
#include "secasn1t.h"

using nss_test::Bytes;
using nss_test::Seq;

// clang-format off
static const uint8_t kAlgorithmID[] = {
    0x30, 0x0d,
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b,
    0x05, 0x00,
};
static const uint8_t kIssuerName[] = {
    0x30, 0x0f,
    0x31, 0x0d,
    0x30, 0x0b,
    0x06, 0x03, 0x55, 0x04, 0x03,
    0x0c, 0x04, 0x54, 0x65, 0x73, 0x74,
};
static const uint8_t kUTCTime[] = {
    0x17, 0x0d,
    0x32, 0x35, 0x30, 0x31, 0x30, 0x31,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a,
};
// clang-format on

// CRL entry: SEQUENCE { INTEGER serial, UTCTime revocationDate }
static Bytes MakeCrlEntry(uint8_t serial) {
  Bytes content = {0x02, 0x01, serial};
  content.insert(content.end(), kUTCTime, kUTCTime + sizeof(kUTCTime));
  return Seq(content);
}

// Build a complete DER-encoded SignedCRL with |num_entries| revoked certs.
static Bytes MakeSignedCrl(size_t num_entries) {
  // TBSCertList content: algorithmID + issuer + thisUpdate [+ entries]
  Bytes tbs_content;
  tbs_content.insert(tbs_content.end(), kAlgorithmID,
                     kAlgorithmID + sizeof(kAlgorithmID));
  tbs_content.insert(tbs_content.end(), kIssuerName,
                     kIssuerName + sizeof(kIssuerName));
  tbs_content.insert(tbs_content.end(), kUTCTime, kUTCTime + sizeof(kUTCTime));

  if (num_entries > 0) {
    Bytes entries_content;
    for (size_t i = 0; i < num_entries; i++) {
      auto entry = MakeCrlEntry(static_cast<uint8_t>((i + 1) & 0xff));
      entries_content.insert(entries_content.end(), entry.begin(), entry.end());
    }
    Bytes entries_seq = Seq(entries_content);
    tbs_content.insert(tbs_content.end(), entries_seq.begin(),
                       entries_seq.end());
  }

  Bytes tbs = Seq(tbs_content);

  // BIT STRING signature (dummy: 0x00 padding byte + 0xff)
  uint8_t sig[] = {0x03, 0x02, 0x00, 0xff};

  Bytes signed_content;
  signed_content.insert(signed_content.end(), tbs.begin(), tbs.end());
  signed_content.insert(signed_content.end(), kAlgorithmID,
                        kAlgorithmID + sizeof(kAlgorithmID));
  signed_content.insert(signed_content.end(), sig, sig + sizeof(sig));

  return Seq(signed_content);
}

class CrlDecodeTest : public ::testing::Test {};

TEST_F(CrlDecodeTest, BasicCrlDecodeNoEntries) {
  auto crl_der = MakeSignedCrl(0);
  SECItem item = {siBuffer, crl_der.data(),
                  static_cast<unsigned int>(crl_der.size())};
  CERTSignedCrl* crl = CERT_DecodeDERCrlWithFlags(nullptr, &item, SEC_CRL_TYPE,
                                                  CRL_DECODE_DEFAULT_OPTIONS);
  ASSERT_NE(nullptr, crl);
  SEC_DestroyCrl(crl);
}

TEST_F(CrlDecodeTest, CrlDecodeWithEntries) {
  auto crl_der = MakeSignedCrl(3);
  SECItem item = {siBuffer, crl_der.data(),
                  static_cast<unsigned int>(crl_der.size())};
  CERTSignedCrl* crl = CERT_DecodeDERCrlWithFlags(nullptr, &item, SEC_CRL_TYPE,
                                                  CRL_DECODE_DEFAULT_OPTIONS);
  ASSERT_NE(nullptr, crl);
  ASSERT_NE(nullptr, crl->crl.entries);
  ASSERT_NE(nullptr, crl->crl.entries[0]);
  ASSERT_NE(nullptr, crl->crl.entries[1]);
  ASSERT_NE(nullptr, crl->crl.entries[2]);
  ASSERT_EQ(nullptr, crl->crl.entries[3]);
  SEC_DestroyCrl(crl);
}

TEST_F(CrlDecodeTest, KeyFromDERCrl) {
  auto crl_der = MakeSignedCrl(3);
  SECItem item = {siBuffer, crl_der.data(),
                  static_cast<unsigned int>(crl_der.size())};
  ScopedPLArenaPool arena(PORT_NewArena(1024));
  ASSERT_NE(nullptr, arena);
  SECItem key = {siBuffer, nullptr, 0};
  ASSERT_EQ(SECSuccess, CERT_KeyFromDERCrl(arena.get(), &item, &key));
  ASSERT_EQ(sizeof(kIssuerName), key.len);
  EXPECT_EQ(0, memcmp(kIssuerName, key.data, key.len));
}

TEST_F(CrlDecodeTest, CrlDecodeSkipEntriesThenComplete) {
  auto crl_der = MakeSignedCrl(2);
  SECItem item = {siBuffer, crl_der.data(),
                  static_cast<unsigned int>(crl_der.size())};
  CERTSignedCrl* crl = CERT_DecodeDERCrlWithFlags(nullptr, &item, SEC_CRL_TYPE,
                                                  CRL_DECODE_SKIP_ENTRIES);
  ASSERT_NE(nullptr, crl);
  ASSERT_EQ(nullptr, crl->crl.entries);
  ASSERT_EQ(SECSuccess, CERT_CompleteCRLDecodeEntries(crl));
  ASSERT_NE(nullptr, crl->crl.entries);
  ASSERT_NE(nullptr, crl->crl.entries[0]);
  ASSERT_NE(nullptr, crl->crl.entries[1]);
  ASSERT_EQ(nullptr, crl->crl.entries[2]);
  SEC_DestroyCrl(crl);
}
