/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <climits>
#include <cstdint>
#include <memory>
#include "cryptohi.h"
#include "cpputil.h"
#include "databuffer.h"
#include "json_reader.h"
#include "gtest/gtest.h"
#include "nss.h"
#include "nss_scoped_ptrs.h"
#include "pk11pub.h"
#include "secerr.h"
#include "sechash.h"
#include "pk11_signature_test.h"
#include "testvectors/rsa_signature-vectors.h"

namespace nss_test {

CK_MECHANISM_TYPE RsaHashToComboMech(SECOidTag hash) {
  switch (hash) {
    case SEC_OID_SHA1:
      return CKM_SHA1_RSA_PKCS;
    case SEC_OID_SHA224:
      return CKM_SHA224_RSA_PKCS;
    case SEC_OID_SHA256:
      return CKM_SHA256_RSA_PKCS;
    case SEC_OID_SHA384:
      return CKM_SHA384_RSA_PKCS;
    case SEC_OID_SHA512:
      return CKM_SHA512_RSA_PKCS;
    default:
      break;
  }
  return CKM_INVALID_MECHANISM;
}

class Pkcs11RsaBaseTest : public Pk11SignatureTest {
 protected:
  Pkcs11RsaBaseTest(SECOidTag hashOid)
      : Pk11SignatureTest(CKM_RSA_PKCS, hashOid, RsaHashToComboMech(hashOid)) {}

  void Verify(const RsaSignatureTestVector& vec) {
    Pkcs11SignatureTestParams params = {
        DataBuffer(), DataBuffer(vec.public_key.data(), vec.public_key.size()),
        DataBuffer(vec.msg.data(), vec.msg.size()),
        DataBuffer(vec.sig.data(), vec.sig.size())};
    Pk11SignatureTest::Verify(params, (bool)vec.valid);
  }
};

class Pkcs11RsaPkcs1WycheproofTest : public ::testing::Test {
 protected:
  void Run(const std::string& name,
           const std::string& schema = "rsassa_pkcs1_verify_schema_v1.json") {
    WycheproofHeader(name, "RSASSA-PKCS1-v1_5", schema,
                     [this](JsonReader& r) { RunGroup(r); });
  }

  static void ReadTestAttr(RsaSignatureTestVector& t, const std::string& n,
                           JsonReader& r) {
    if (n == "msg") {
      t.msg = r.ReadHex();
    } else if (n == "sig") {
      t.sig = r.ReadHex();
    } else {
      FAIL() << "unknown test key: " << n;
    }
  }

  void RunGroup(JsonReader& r) {
    std::vector<RsaSignatureTestVector> tests;
    std::vector<uint8_t> public_key;
    SECOidTag hash_oid = SEC_OID_UNKNOWN;
    uint64_t keysize = 0;
    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }
      // rsa_signature_test.json has no successor in testvectors_v1/, so this
      // reads both the original and the v1 field names.
      if (n == "e" || n == "keyAsn" || n == "keyJwk" || n == "keyPem" ||
          n == "n" || n == "publicKey" || n == "publicKeyAsn" ||
          n == "publicKeyPem" || n == "source") {
        r.SkipValue();
      } else if (n == "keyDer" || n == "publicKeyDer") {
        public_key = r.ReadHex();
      } else if (n == "keysize" || n == "keySize") {
        keysize = r.ReadInt();
      } else if (n == "type") {
        ASSERT_EQ("RsassaPkcs1Verify", r.ReadString());
      } else if (n == "sha") {
        hash_oid = r.ReadHash();
      } else if (n == "tests") {
        WycheproofReadTests(
            r, &tests, ReadTestAttr, false,
            [keysize](RsaSignatureTestVector& t, const std::string& result,
                      const std::vector<std::string>& flags) {
              if (result == "acceptable" && keysize >= 1024 &&
                  std::find_if(flags.begin(), flags.end(), [](std::string v) {
                    return v == "SmallModulus" || v == "SmallPublicKey";
                  }) != flags.end()) {
                t.valid = true;
              };
            });
      } else {
        FAIL() << "unknown group label: " << n;
      }
    }

    for (auto& t : tests) {
      Pkcs11RsaBaseTestWrap test(hash_oid);
      t.hash_oid = hash_oid;
      t.public_key = public_key;
      test.Run(t);
    }
  }

 private:
  class Pkcs11RsaBaseTestWrap : public Pkcs11RsaBaseTest {
   public:
    Pkcs11RsaBaseTestWrap(SECOidTag hash) : Pkcs11RsaBaseTest(hash) {}
    void TestBody() {}

    void Verify1(const RsaSignatureTestVector& vec) {
      SECItem spki_item = {siBuffer, toUcharPtr(vec.public_key.data()),
                           static_cast<unsigned int>(vec.public_key.size())};

      ScopedCERTSubjectPublicKeyInfo cert_spki(
          SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
      ASSERT_TRUE(cert_spki);

      ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
      ASSERT_TRUE(pub_key);

      DataBuffer hash;
      hash.Allocate(static_cast<size_t>(HASH_ResultLenByOidTag(vec.hash_oid)));
      SECStatus rv = PK11_HashBuf(vec.hash_oid, toUcharPtr(hash.data()),
                                  toUcharPtr(vec.msg.data()), vec.msg.size());
      ASSERT_EQ(rv, SECSuccess);

      // Verify.
      SECItem hash_item = {siBuffer, toUcharPtr(hash.data()),
                           static_cast<unsigned int>(hash.len())};
      SECItem sig_item = {siBuffer, toUcharPtr(vec.sig.data()),
                          static_cast<unsigned int>(vec.sig.size())};

      rv = VFY_VerifyDigestDirect(&hash_item, pub_key.get(), &sig_item,
                                  SEC_OID_PKCS1_RSA_ENCRYPTION, vec.hash_oid,
                                  nullptr);
      EXPECT_EQ(rv, vec.valid ? SECSuccess : SECFailure);
    };

    void Run(const RsaSignatureTestVector& vec) {
      /* Using VFY_ interface */
      Verify1(vec);
      /* Using PKCS #11 interface */
      setSkipRaw(true);
      Verify(vec);
    }
  };
};

/* Test that PKCS #1 v1.5 verification requires a minimum of 8B
 * of padding, per-RFC3447. The padding formula is
 * `pad_len = em_len - t_len - 3`, where em_len is the octet length
 * of the RSA modulus and t_len is the length of the `DigestInfo ||
 * Hash(message)` sequence. For SHA512, t_len is 83. We'll tweak the
 * modulus size to test with a pad_len of 8 (valid) and 6 (invalid):
 *   em_len = `8 + 83 + 3` = `94*8` = 752b
 *   em_len = `6 + 83 + 3` = `92*8` = 736b
 * Use 6 as the invalid value since modLen % 16 must be zero.
 */
TEST(RsaPkcs1Test, Pkcs1MinimumPadding) {
#define RSA_SHORT_KEY_LENGTH 736
/* if our minimum supported key length is big enough to handle
 * our largest Hash function, we can't test a short length */
#if RSA_MIN_MODULUS_BITS < RSA_SHORT_KEY_LENGTH
  const size_t kRsaShortKeyBits = RSA_SHORT_KEY_LENGTH;
  const size_t kRsaKeyBits = 752;
  static const std::vector<uint8_t> kMsg{'T', 'E', 'S', 'T'};
  static const std::vector<uint8_t> kSha512DigestInfo{
      0x30, 0x51, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
      0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};
  static const std::vector<uint8_t> kMsgSha512{
      0x7B, 0xFA, 0x95, 0xA6, 0x88, 0x92, 0x4C, 0x47, 0xC7, 0xD2, 0x23,
      0x81, 0xF2, 0x0C, 0xC9, 0x26, 0xF5, 0x24, 0xBE, 0xAC, 0xB1, 0x3F,
      0x84, 0xE2, 0x03, 0xD4, 0xBD, 0x8C, 0xB6, 0xBA, 0x2F, 0xCE, 0x81,
      0xC5, 0x7A, 0x5F, 0x05, 0x9B, 0xF3, 0xD5, 0x09, 0x92, 0x64, 0x87,
      0xBD, 0xE9, 0x25, 0xB3, 0xBC, 0xEE, 0x06, 0x35, 0xE4, 0xF7, 0xBA,
      0xEB, 0xA0, 0x54, 0xE5, 0xDB, 0xA6, 0x96, 0xB2, 0xBF};

  ScopedSECKEYPrivateKey short_priv, good_priv;
  ScopedSECKEYPublicKey short_pub, good_pub;
  PK11RSAGenParams rsa_params;
  rsa_params.keySizeInBits = kRsaShortKeyBits;
  rsa_params.pe = 65537;

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);
  SECKEYPublicKey* p_pub_tmp = nullptr;
  short_priv.reset(PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN,
                                        &rsa_params, &p_pub_tmp, false,
                                        PK11_IsFIPS(), nullptr));
  short_pub.reset(p_pub_tmp);

  rsa_params.keySizeInBits = kRsaKeyBits;
  good_priv.reset(PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN,
                                       &rsa_params, &p_pub_tmp, false,
                                       PK11_IsFIPS(), nullptr));
  good_pub.reset(p_pub_tmp);

  size_t em_len = kRsaShortKeyBits / 8;
  size_t t_len = kSha512DigestInfo.size() + kMsgSha512.size();
  size_t pad_len = em_len - t_len - 3;
  ASSERT_EQ(6U, pad_len);

  std::vector<uint8_t> invalid_pkcs;
  invalid_pkcs.push_back(0x00);
  invalid_pkcs.push_back(0x01);
  invalid_pkcs.insert(invalid_pkcs.end(), pad_len, 0xff);
  invalid_pkcs.insert(invalid_pkcs.end(), 1, 0x00);
  invalid_pkcs.insert(invalid_pkcs.end(), kSha512DigestInfo.begin(),
                      kSha512DigestInfo.end());
  invalid_pkcs.insert(invalid_pkcs.end(), kMsgSha512.begin(), kMsgSha512.end());
  ASSERT_EQ(em_len, invalid_pkcs.size());

  // Sign it indirectly. Signing functions check for a proper pad_len.
  std::vector<uint8_t> sig(em_len);
  uint32_t sig_len;
  SECStatus rv =
      PK11_PubDecryptRaw(short_priv.get(), sig.data(), &sig_len, sig.size(),
                         invalid_pkcs.data(), invalid_pkcs.size());
  EXPECT_EQ(SECSuccess, rv);

  // Verify it.
  DataBuffer hash;
  hash.Allocate(static_cast<size_t>(HASH_ResultLenByOidTag(SEC_OID_SHA512)));
  rv = PK11_HashBuf(SEC_OID_SHA512, toUcharPtr(hash.data()),
                    toUcharPtr(kMsg.data()), kMsg.size());
  ASSERT_EQ(rv, SECSuccess);
  SECItem hash_item = {siBuffer, toUcharPtr(hash.data()),
                       static_cast<unsigned int>(hash.len())};
  SECItem sig_item = {siBuffer, toUcharPtr(sig.data()), sig_len};
  /* don't let policy foil us */
  NSS_OptionSet(NSS_KEY_SIZE_POLICY_CLEAR_FLAGS,
                NSS_KEY_SIZE_POLICY_VERIFY_FLAG);
  rv = VFY_VerifyDigestDirect(&hash_item, short_pub.get(), &sig_item,
                              SEC_OID_PKCS1_RSA_ENCRYPTION, SEC_OID_SHA512,
                              nullptr);
  EXPECT_EQ(SECFailure, rv);
  EXPECT_EQ(SEC_ERROR_BAD_SIGNATURE, PORT_GetError());

  // Repeat the test with the sufficiently-long key.
  em_len = kRsaKeyBits / 8;
  t_len = kSha512DigestInfo.size() + kMsgSha512.size();
  pad_len = em_len - t_len - 3;
  ASSERT_EQ(8U, pad_len);

  std::vector<uint8_t> valid_pkcs;
  valid_pkcs.push_back(0x00);
  valid_pkcs.push_back(0x01);
  valid_pkcs.insert(valid_pkcs.end(), pad_len, 0xff);
  valid_pkcs.insert(valid_pkcs.end(), 1, 0x00);
  valid_pkcs.insert(valid_pkcs.end(), kSha512DigestInfo.begin(),
                    kSha512DigestInfo.end());
  valid_pkcs.insert(valid_pkcs.end(), kMsgSha512.begin(), kMsgSha512.end());
  ASSERT_EQ(em_len, valid_pkcs.size());

  // Sign it the same way as above (even though we could use sign APIs now).
  sig.resize(em_len);
  rv = PK11_PubDecryptRaw(good_priv.get(), sig.data(), &sig_len, sig.size(),
                          valid_pkcs.data(), valid_pkcs.size());
  EXPECT_EQ(SECSuccess, rv);

  // Verify it.
  sig_item = {siBuffer, toUcharPtr(sig.data()), sig_len};
  rv = VFY_VerifyDigestDirect(&hash_item, good_pub.get(), &sig_item,
                              SEC_OID_PKCS1_RSA_ENCRYPTION, SEC_OID_SHA512,
                              nullptr);
  EXPECT_EQ(SECSuccess, rv);
#else
  GTEST_SKIP();
#endif
}

TEST(RsaPkcs1Test, RequireNullParameter) {
  // The test vectors may be verified with:
  //
  // openssl rsautl -keyform der -pubin -inkey spki.bin -in sig.bin | der2ascii
  // openssl rsautl -keyform der -pubin -inkey spki.bin -in sig2.bin | der2ascii

  // Import public key.
  SECItem spki_item = {siBuffer, toUcharPtr(kSpki), sizeof(kSpki)};
  ScopedCERTSubjectPublicKeyInfo cert_spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
  ASSERT_TRUE(cert_spki);
  ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
  ASSERT_TRUE(pub_key);

  SECItem hash = {siBuffer, toUcharPtr(kHash), sizeof(kHash)};

  // kSignature is a valid signature.
  SECItem sig_item = {siBuffer, toUcharPtr(kSignature), sizeof(kSignature)};
  SECStatus rv = VFY_VerifyDigestDirect(&hash, pub_key.get(), &sig_item,
                                        SEC_OID_PKCS1_RSA_ENCRYPTION,
                                        SEC_OID_SHA256, nullptr);
  EXPECT_EQ(SECSuccess, rv);

  // kSignatureInvalid is not.
  sig_item = {siBuffer, toUcharPtr(kSignatureInvalid),
              sizeof(kSignatureInvalid)};
  rv = VFY_VerifyDigestDirect(&hash, pub_key.get(), &sig_item,
                              SEC_OID_PKCS1_RSA_ENCRYPTION, SEC_OID_SHA256,
                              nullptr);
#ifdef NSS_PKCS1_AllowMissingParameters
  EXPECT_EQ(SECSuccess, rv);
#else
  EXPECT_EQ(SECFailure, rv);
#endif
}

TEST(RsaPkcs1Test, EmptySignature) {
  SECItem spki_item = {siBuffer, toUcharPtr(kSpki), sizeof(kSpki)};
  ScopedCERTSubjectPublicKeyInfo cert_spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
  ASSERT_TRUE(cert_spki);
  ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
  ASSERT_TRUE(pub_key);

  VerifyEmptySignatureFails(pub_key.get(), CKM_RSA_PKCS);
  VerifyEmptySignatureFails(pub_key.get(), CKM_SHA256_RSA_PKCS);
}

// Bug 2068388: public exponents wider than 32 bits must be rejected.
TEST(RsaPkcs1Test, KeyGenPublicExponentTooLarge) {
#if ULONG_MAX <= PR_UINT32_MAX
  GTEST_SKIP() << "unsigned long cannot hold an exponent above 32 bits";
#else
  PK11RSAGenParams rsa_params = {1024,
                                 static_cast<unsigned long>(PR_UINT32_MAX) + 1};
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);
  SECKEYPublicKey* pub = nullptr;
  ScopedSECKEYPrivateKey priv(
      PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN, &rsa_params,
                           &pub, false, PK11_IsFIPS(), nullptr));
  EXPECT_FALSE(priv);
  EXPECT_FALSE(pub);
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());
#endif
}

TEST_F(Pkcs11RsaPkcs1WycheproofTest, Pkcs11RsaPkcs1WycheproofTest) {
  Run("rsa_signature", "rsassa_pkcs1_verify_schema.json");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa2048Sha224) {
  Run("rsa_signature_2048_sha224");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa2048Sha256) {
  Run("rsa_signature_2048_sha256");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa2048Sha512) {
  Run("rsa_signature_2048_sha512");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa3072Sha256) {
  Run("rsa_signature_3072_sha256");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa3072Sha384) {
  Run("rsa_signature_3072_sha384");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa3072Sha512) {
  Run("rsa_signature_3072_sha512");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa4096Sha384) {
  Run("rsa_signature_4096_sha384");
}
TEST_F(Pkcs11RsaPkcs1WycheproofTest, Rsa4096Sha512) {
  Run("rsa_signature_4096_sha512");
}

}  // namespace nss_test
