/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"
#include "secoid.h"
#include "secpkcs5.h"

#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"
#include "pk11_import.h"

namespace nss_test {

static unsigned char* ToUcharPtr(std::string& str) {
  return const_cast<unsigned char*>(
      reinterpret_cast<const unsigned char*>(str.c_str()));
}

class Pkcs11Pbkdf2Test : public ::testing::Test {
 public:
  void Derive(std::vector<uint8_t>& derived, SECOidTag hash_alg) {
    // Shared between test vectors.
    const unsigned int kIterations = 4096;
    std::string pass("passwordPASSWORDpassword");
    std::string salt("saltSALTsaltSALTsaltSALTsaltSALTsalt");

    // Derivation must succeed with the right values.
    EXPECT_TRUE(DeriveBytes(pass, salt, derived, hash_alg, kIterations));

    // Derivation must fail when the password is bogus.
    std::string bogus_pass("PasswordPASSWORDpassword");
    EXPECT_FALSE(DeriveBytes(bogus_pass, salt, derived, hash_alg, kIterations));

    // Derivation must fail when the salt is bogus.
    std::string bogus_salt("SaltSALTsaltSALTsaltSALTsaltSALTsalt");
    EXPECT_FALSE(DeriveBytes(pass, bogus_salt, derived, hash_alg, kIterations));

    // Derivation must fail when using the wrong hash function.
    SECOidTag next_hash_alg = static_cast<SECOidTag>(hash_alg + 1);
    EXPECT_FALSE(DeriveBytes(pass, salt, derived, next_hash_alg, kIterations));

    // Derivation must fail when using the wrong number of kIterations.
    EXPECT_FALSE(DeriveBytes(pass, salt, derived, hash_alg, kIterations + 1));
  }

  void KeySizes(SECOidTag hash_alg) {
    // These tests will only validate the controls around the key sizes.
    // The resulting key is tested above, with valid key sizes.
    const unsigned int kIterations = 10;
    std::string pass("passwordPASSWORDpassword");
    std::string salt("saltSALTsaltSALTsaltSALTsaltSALTsalt");
    std::string salt_empty("");

    // Derivation must fail when using key sizes bigger than MAX_KEY_LEN.
    const int big_key_size = 768;
    EXPECT_FALSE(KeySizeParam(pass, salt, big_key_size, hash_alg, kIterations));

    // Zero is acceptable as key size and will be managed internally.
    const int zero_key_size = 0;
    EXPECT_TRUE(KeySizeParam(pass, salt, zero_key_size, hash_alg, kIterations));

    // Zero is acceptable as salt size and will be managed internally.
    EXPECT_TRUE(
        KeySizeParam(pass, salt_empty, zero_key_size, hash_alg, kIterations));

    // -1 will be set to 0 internally and this means that the key size will be
    // obtained from the template. If the template doesn't have this defined,
    // it must fail.
    const int minus_key_size = -1;
    EXPECT_FALSE(
        KeySizeParam(pass, salt, minus_key_size, hash_alg, kIterations));

    // Lower than -1 is not allowed, as -1 means no keyLen defined.
    const int negative_key_size = -10;
    EXPECT_FALSE(
        KeySizeParam(pass, salt, negative_key_size, hash_alg, kIterations));

    // Malformed inputs are handled without crashing
    EXPECT_FALSE(
        MalformedPass(pass, salt, big_key_size, hash_alg, kIterations));
    EXPECT_FALSE(
        MalformedSalt(pass, salt, big_key_size, hash_alg, kIterations));
  }

 private:
  bool DeriveBytes(std::string& pass, std::string& salt,
                   std::vector<uint8_t>& derived, SECOidTag hash_alg,
                   unsigned int kIterations) {
    SECItem pass_item = {siBuffer, ToUcharPtr(pass),
                         static_cast<unsigned int>(pass.length())};
    SECItem salt_item = {siBuffer, ToUcharPtr(salt),
                         static_cast<unsigned int>(salt.length())};

    // Set up PBKDF2 params.
    ScopedSECAlgorithmID alg_id(
        PK11_CreatePBEV2AlgorithmID(SEC_OID_PKCS5_PBKDF2, hash_alg, hash_alg,
                                    derived.size(), kIterations, &salt_item));

    // Derive.
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    ScopedPK11SymKey sym_key(
        PK11_PBEKeyGen(slot.get(), alg_id.get(), &pass_item, false, nullptr));

    SECStatus rv = nss_test::ExtractKeyValue(sym_key.get());
    EXPECT_EQ(rv, SECSuccess);

    SECItem* key_data = nss_test::GetKeyData(sym_key.get());
    return !memcmp(&derived[0], key_data->data, key_data->len);
  }

  bool GenerateKey(SECItem pass_item, SECItem salt_item, const int key_size,
                   SECOidTag hash_alg, unsigned int kIterations) {
    // Set up PBKDF2 params.
    ScopedSECAlgorithmID alg_id(
        PK11_CreatePBEV2AlgorithmID(SEC_OID_PKCS5_PBKDF2, hash_alg, hash_alg,
                                    key_size, kIterations, &salt_item));

    // Try to generate a key with the defined params.
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    ScopedPK11SymKey sym_key(
        PK11_PBEKeyGen(slot.get(), alg_id.get(), &pass_item, false, nullptr));

    // Should be nullptr if fail.
    return sym_key.get();
  }

  bool KeySizeParam(std::string& pass, std::string& salt, const int key_size,
                    SECOidTag hash_alg, unsigned int kIterations) {
    SECItem pass_item = {siBuffer, ToUcharPtr(pass),
                         static_cast<unsigned int>(pass.length())};
    SECItem salt_item = {siBuffer, ToUcharPtr(salt),
                         static_cast<unsigned int>(salt.length())};

    return GenerateKey(pass_item, salt_item, key_size, hash_alg, kIterations);
  }

  bool MalformedSalt(std::string& pass, std::string& salt, const int key_size,
                     SECOidTag hash_alg, unsigned int kIterations) {
    SECItem pass_item = {siBuffer, ToUcharPtr(pass),
                         static_cast<unsigned int>(pass.length())};
    SECItem salt_item = {siBuffer, nullptr, 0};

    return GenerateKey(pass_item, salt_item, key_size, hash_alg, kIterations);
  }

  bool MalformedPass(std::string& pass, std::string& salt, const int key_size,
                     SECOidTag hash_alg, unsigned int kIterations) {
    SECItem pass_item = {siBuffer, nullptr, 0};
    SECItem salt_item = {siBuffer, ToUcharPtr(salt),
                         static_cast<unsigned int>(salt.length())};

    return GenerateKey(pass_item, salt_item, key_size, hash_alg, kIterations);
  }
};

// RFC 6070 <http://tools.ietf.org/html/rfc6070>
TEST_F(Pkcs11Pbkdf2Test, DeriveKnown1) {
  std::vector<uint8_t> derived = {0x3d, 0x2e, 0xec, 0x4f, 0xe4, 0x1c, 0x84,
                                  0x9b, 0x80, 0xc8, 0xd8, 0x36, 0x62, 0xc0,
                                  0xe4, 0x4a, 0x8b, 0x29, 0x1a, 0x96, 0x4c,
                                  0xf2, 0xf0, 0x70, 0x38};

  Derive(derived, SEC_OID_HMAC_SHA1);
}

// https://stackoverflow.com/questions/5130513/pbkdf2-hmac-sha2-test-vectors
TEST_F(Pkcs11Pbkdf2Test, DeriveKnown2) {
  std::vector<uint8_t> derived = {
      0x34, 0x8c, 0x89, 0xdb, 0xcb, 0xd3, 0x2b, 0x2f, 0x32, 0xd8,
      0x14, 0xb8, 0x11, 0x6e, 0x84, 0xcf, 0x2b, 0x17, 0x34, 0x7e,
      0xbc, 0x18, 0x00, 0x18, 0x1c, 0x4e, 0x2a, 0x1f, 0xb8, 0xdd,
      0x53, 0xe1, 0xc6, 0x35, 0x51, 0x8c, 0x7d, 0xac, 0x47, 0xe9};

  Derive(derived, SEC_OID_HMAC_SHA256);
}

TEST_F(Pkcs11Pbkdf2Test, KeyLenSizes) {
  // The size controls are regardless of the algorithms.
  KeySizes(SEC_OID_HMAC_SHA256);
}

struct LongPasswordVector {
  SECOidTag prf;
  size_t pass_len;
  std::string expected_hex;
};

class Pkcs11Pbkdf2LongPasswordTest
    : public ::testing::TestWithParam<LongPasswordVector> {};

// Passwords longer than the HMAC block size, including ones longer than
// CK_PKCS5_PBKD2_PARAMS_PTR_BOUNDARY.
TEST_P(Pkcs11Pbkdf2LongPasswordTest, Derive) {
  const LongPasswordVector& v = GetParam();
  std::vector<uint8_t> pass(v.pass_len);
  for (size_t i = 0; i < pass.size(); i++) {
    pass[i] = static_cast<uint8_t>(i * 7 + 1);
  }
  std::string salt("saltSALTsaltSALT");
  SECItem pass_item = {siBuffer, pass.data(),
                       static_cast<unsigned int>(pass.size())};
  SECItem salt_item = {siBuffer, ToUcharPtr(salt),
                       static_cast<unsigned int>(salt.length())};

  ScopedSECAlgorithmID alg_id(PK11_CreatePBEV2AlgorithmID(
      SEC_OID_PKCS5_PBKDF2, v.prf, v.prf, 32, 2, &salt_item));
  ASSERT_TRUE(alg_id);
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ScopedPK11SymKey sym_key(
      PK11_PBEKeyGen(slot.get(), alg_id.get(), &pass_item, false, nullptr));
  ASSERT_TRUE(sym_key);
  ASSERT_EQ(SECSuccess, nss_test::ExtractKeyValue(sym_key.get()));
  SECItem* key_data = nss_test::GetKeyData(sym_key.get());
  ASSERT_TRUE(key_data);

  std::vector<uint8_t> expected;
  for (size_t i = 0; i < v.expected_hex.size(); i += 2) {
    expected.push_back(static_cast<uint8_t>(
        std::stoul(v.expected_hex.substr(i, 2), nullptr, 16)));
  }
  EXPECT_EQ(expected, std::vector<uint8_t>(key_data->data,
                                           key_data->data + key_data->len));
}

// Generated with Python's hashlib.pbkdf2_hmac(prf, pass, salt, 2, 32).
static const LongPasswordVector kLongPasswordVectors[] = {
    {SEC_OID_HMAC_SHA1, 65,
     "d4b73678eb985b634ac5dc5d4f284c2001baaca72b244e909f41b4a79d78824f"},
    {SEC_OID_HMAC_SHA256, 8193,
     "fad1775f2f741f06e5e2fc2cbd2ff4f9b08b53a38737dccbc1802394b31795f4"},
    {SEC_OID_HMAC_SHA256, 20000,
     "a180f9b10caa259868f365636916ef8bcf1c9bd2f593189c8783f745f878ddbc"},
    {SEC_OID_HMAC_SHA512, 129,
     "dc89f536a723b266903ed61219214072786537a09edd88398d744ac89af90fb7"},
    {SEC_OID_HMAC_SHA512, 8193,
     "63a0c6bbbba3474105d15713896edee4229d189929115c40e3a3909e164f2635"},
};

INSTANTIATE_TEST_SUITE_P(LongPasswords, Pkcs11Pbkdf2LongPasswordTest,
                         ::testing::ValuesIn(kLongPasswordVectors));

// Helper: build a SECAlgorithmID for PBKDF2 from raw DER-encoded parameters.
// The returned algid references the input buffer (no copy), so params must
// outlive any use of the algid.
static SECAlgorithmID MakePbkdf2AlgId(uint8_t* params,
                                      unsigned int params_len) {
  SECAlgorithmID algid = {};
  SECOidData* oid = SECOID_FindOIDByTag(SEC_OID_PKCS5_PBKDF2);
  if (oid) {
    algid.algorithm = oid->oid;
  }
  algid.parameters.type = siBuffer;
  algid.parameters.data = params;
  algid.parameters.len = params_len;
  return algid;
}

// Bug 2012680 - DER_GetInteger error handling in pbe_PK11AlgidToParam.
// A 9-byte positive INTEGER overflows long; the patched code must reject it.
// (Note: 0xff*9 would just be -1 in non-minimal encoding; we need a value
// whose magnitude genuinely exceeds LONG_MAX.)
TEST_F(Pkcs11Pbkdf2Test, MalformedIterationOverflow) {
  // clang-format off
  uint8_t params[] = {
    0x30, 0x15,                                                      // SEQUENCE (21)
    0x04, 0x08, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,    // OCTET STRING salt
    0x02, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // INTEGER iteration
    0x00,                                                            //   (9 bytes, 2^64)
  };
  // clang-format on

  SECAlgorithmID algid = MakePbkdf2AlgId(params, sizeof(params));
  ScopedSECItem result(PK11_ParamFromAlgid(&algid));
  EXPECT_FALSE(result);
}

// Bug 2012680 - A zero-length INTEGER triggers SEC_ERROR_INPUT_LEN.
TEST_F(Pkcs11Pbkdf2Test, MalformedIterationZeroLength) {
  // clang-format off
  uint8_t params[] = {
    0x30, 0x0c,                                                      // SEQUENCE (12)
    0x04, 0x08, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,    // OCTET STRING salt
    0x02, 0x00,                                                      // INTEGER (0 bytes)
  };
  // clang-format on

  SECAlgorithmID algid = MakePbkdf2AlgId(params, sizeof(params));
  ScopedSECItem result(PK11_ParamFromAlgid(&algid));
  EXPECT_FALSE(result);
}

// Bug 2012680 - DER_GetInteger error handling in sec_pkcs5v2_key_length.
// Iteration is valid but keyLength overflows.
TEST_F(Pkcs11Pbkdf2Test, MalformedKeyLengthOverflow) {
  // clang-format off
  uint8_t params[] = {
    0x30, 0x19,                                                      // SEQUENCE (25)
    0x04, 0x08, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,    // OCTET STRING salt
    0x02, 0x02, 0x10, 0x00,                                          // INTEGER iter=4096
    0x02, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // INTEGER keyLength
    0x00,                                                            //   (9 bytes, 2^64)
  };
  // clang-format on

  SECAlgorithmID algid = MakePbkdf2AlgId(params, sizeof(params));
  EXPECT_EQ(-1, SEC_PKCS5GetKeyLength(&algid));
}

// Bug 2012680 - Zero-length keyLength INTEGER triggers an error.
TEST_F(Pkcs11Pbkdf2Test, MalformedKeyLengthZeroLength) {
  // clang-format off
  uint8_t params[] = {
    0x30, 0x10,                                                      // SEQUENCE (16)
    0x04, 0x08, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,    // OCTET STRING salt
    0x02, 0x02, 0x10, 0x00,                                          // INTEGER iter=4096
    0x02, 0x00,                                                      // INTEGER keyLen (0 bytes)
  };
  // clang-format on

  SECAlgorithmID algid = MakePbkdf2AlgId(params, sizeof(params));
  EXPECT_EQ(-1, SEC_PKCS5GetKeyLength(&algid));
}

}  // namespace nss_test
