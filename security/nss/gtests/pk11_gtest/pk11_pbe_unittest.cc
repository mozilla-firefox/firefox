/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"

#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"

namespace nss_test {

static unsigned char* ToUcharPtr(std::string& str) {
  return const_cast<unsigned char*>(
      reinterpret_cast<const unsigned char*>(str.c_str()));
}

class Pkcs11PbeTest : public ::testing::Test {
 public:
  void Derive(std::vector<uint8_t>& derived) {
    // Shared between test vectors.
    const unsigned int kIterations = 4096;
    std::string pass("passwordPASSWORDpassword");
    std::string salt("saltSALTsaltSALTsaltSALTsaltSALTsalt");

    // Derivation must succeed with the right values.
    EXPECT_TRUE(DeriveBytes(pass, salt, derived, kIterations));
  }

 private:
  bool DeriveBytes(std::string& pass, std::string& salt,
                   std::vector<uint8_t>& derived, unsigned int kIterations) {
    SECItem pass_item = {siBuffer, ToUcharPtr(pass),
                         static_cast<unsigned int>(pass.length())};
    SECItem salt_item = {siBuffer, ToUcharPtr(salt),
                         static_cast<unsigned int>(salt.length())};

    // Set up PBE params.
    ScopedSECAlgorithmID alg_id(PK11_CreatePBEAlgorithmID(
        SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_2KEY_TRIPLE_DES_CBC, kIterations,
        &salt_item));

    // Derive.
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    ScopedPK11SymKey sym_key(
        PK11_PBEKeyGen(slot.get(), alg_id.get(), &pass_item, false, nullptr));

    SECStatus rv = PK11_ExtractKeyValue(sym_key.get());
    EXPECT_EQ(rv, SECSuccess);

    SECItem* key_data = PK11_GetKeyData(sym_key.get());

    return key_data->len == derived.size() &&
           !memcmp(&derived[0], key_data->data, key_data->len);
  }
};

TEST_F(Pkcs11PbeTest, DeriveKnown) {
  std::vector<uint8_t> derived = {0x86, 0x6b, 0xce, 0xef, 0x26, 0xa4,
                                  0x4f, 0x02, 0x4a, 0x26, 0xcd, 0xd0,
                                  0x4f, 0x7c, 0x19, 0xad};

  Derive(derived);
}

// Test that excessive iteration counts are rejected.
TEST_F(Pkcs11PbeTest, ExcessiveIterationCountFails) {
  const unsigned int kIterations = 200000000;
  std::string pass("password");
  std::string salt("saltsalt");

  SECItem pass_item = {siBuffer, ToUcharPtr(pass),
                       static_cast<unsigned int>(pass.length())};
  SECItem salt_item = {siBuffer, ToUcharPtr(salt),
                       static_cast<unsigned int>(salt.length())};

  ScopedSECAlgorithmID alg_id(PK11_CreatePBEAlgorithmID(
      SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_2KEY_TRIPLE_DES_CBC, kIterations,
      &salt_item));
  ASSERT_TRUE(alg_id);

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  ScopedPK11SymKey sym_key(
      PK11_PBEKeyGen(slot.get(), alg_id.get(), &pass_item, false, nullptr));
  EXPECT_FALSE(sym_key);
}

// Construct a PBES2 AlgorithmIdentifier whose encryptionScheme is AES-128-GCM.
// (The algid is built from raw DER because NSS has no encoder for a GCM
// encryptionScheme), and ensure that PK11_GetPBECryptoMechanism() returns a
// well-formed CK_NSS_GCM_PARAMS.
TEST_F(Pkcs11PbeTest, Pbes2AesGcmCryptoMechanismParams) {
  // The 12 nonce bytes (all 0x42) are echoed back through the mechanism
  // parameter below, so we can check the nonce is reported verbatim.
  const uint8_t nonce[] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
                           0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
  const uint8_t algid_der[] = {
      0x30, 0x4d,
      // OID 1.2.840.113549.1.5.13 (pkcs5-PBES2)
      0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05, 0x0d, 0x30,
      0x40,
      // keyDerivationFunc AlgorithmIdentifier
      0x30, 0x1e,
      // OID 1.2.840.113549.1.5.12 (pkcs5-PBKDF2)
      0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05, 0x0c, 0x30,
      0x11,
      // salt
      0x04, 0x08, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
      // iterationCount 2048
      0x02, 0x02, 0x08, 0x00,
      // keyLength 16
      0x02, 0x01, 0x10,
      // encryptionScheme AlgorithmIdentifier
      0x30, 0x1e,
      // OID 2.16.840.1.101.3.4.1.6 (aes128-GCM)
      0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x01, 0x06,
      // GCMParameters SEQUENCE { nonce OCTET STRING, icvLen INTEGER }
      0x30, 0x11,
      // nonce (12 bytes, all 0x42)
      0x04, 0x0c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
      0x42, 0x42,
      // icvLen 12
      0x02, 0x01, 0x0c};

  ScopedPLArenaPool arena(PORT_NewArena(2048));
  ASSERT_TRUE(arena);
  SECAlgorithmID alg_id{};
  SECItem der = {siBuffer, const_cast<uint8_t*>(algid_der),
                 static_cast<unsigned int>(sizeof(algid_der))};
  ASSERT_EQ(SECSuccess,
            SEC_ASN1DecodeItem(arena.get(), &alg_id,
                               SEC_ASN1_GET(SECOID_AlgorithmIDTemplate), &der));

  // No password is needed to reach the GCM parameter construction.
  SECItem* param = nullptr;
  CK_MECHANISM_TYPE mech = PK11_GetPBECryptoMechanism(&alg_id, &param, nullptr);
  ASSERT_EQ(static_cast<CK_MECHANISM_TYPE>(CKM_AES_GCM), mech);
  ASSERT_TRUE(param);

  // The parameter must be exactly a CK_NSS_GCM_PARAMS ...
  ASSERT_EQ(sizeof(CK_NSS_GCM_PARAMS), param->len);
  CK_NSS_GCM_PARAMS* gcm = reinterpret_cast<CK_NSS_GCM_PARAMS*>(param->data);

  // ... whose pIv/ulIvLen describe the nonce from the algid, not the whole
  // struct misinterpreted as a raw IV.
  ASSERT_TRUE(gcm->pIv);
  ASSERT_EQ(sizeof(nonce), gcm->ulIvLen);

  // pIv must reference live memory holding that nonce verbatim: the parameter
  // has to be self-contained (nonce stored inside the returned allocation) so
  // that the caller can safely read it and pass it on to C_DecryptInit. A
  // dangling or stale pIv here would either mismatch or trip ASAN.
  EXPECT_EQ(0, memcmp(gcm->pIv, nonce, sizeof(nonce)));

  SECITEM_ZfreeItem(param, PR_TRUE);
}

}  // namespace nss_test
