/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"
#include "sechash.h"
#include "json_reader.h"

#include "databuffer.h"

#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"

#include "pk11_signature_test.h"
#include "pk11_rsapss_vectors.h"
#include "testvectors_base/test-structs.h"

namespace nss_test {

CK_MECHANISM_TYPE RsaPssMapCombo(SECOidTag hashOid) {
  switch (hashOid) {
    case SEC_OID_SHA1:
      return CKM_SHA1_RSA_PKCS_PSS;
    case SEC_OID_SHA224:
      return CKM_SHA224_RSA_PKCS_PSS;
    case SEC_OID_SHA256:
      return CKM_SHA256_RSA_PKCS_PSS;
    case SEC_OID_SHA384:
      return CKM_SHA384_RSA_PKCS_PSS;
    case SEC_OID_SHA512:
      return CKM_SHA512_RSA_PKCS_PSS;
    default:
      break;
  }
  return CKM_INVALID_MECHANISM;
}

class Pkcs11RsaPssTestBase : public Pk11SignatureTest {
 public:
  Pkcs11RsaPssTestBase(SECOidTag hashOid, CK_RSA_PKCS_MGF_TYPE mgf, int sLen)
      : Pk11SignatureTest(CKM_RSA_PKCS_PSS, hashOid, RsaPssMapCombo(hashOid)) {
    pss_params_.hashAlg = PK11_AlgtagToMechanism(hashOid);
    pss_params_.mgf = mgf;
    pss_params_.sLen = sLen;

    params_.type = siBuffer;
    params_.data = reinterpret_cast<unsigned char*>(&pss_params_);
    params_.len = sizeof(pss_params_);
  }

  const SECItem* parameters() const { return &params_; }

  void Verify(const RsaPssTestVector& vec) {
    Pkcs11SignatureTestParams params = {
        DataBuffer(), DataBuffer(vec.public_key.data(), vec.public_key.size()),
        DataBuffer(vec.msg.data(), vec.msg.size()),
        DataBuffer(vec.sig.data(), vec.sig.size())};

    Pk11SignatureTest::Verify(params, vec.valid);
  }

 private:
  CK_RSA_PKCS_PSS_PARAMS pss_params_;
  SECItem params_;
};

class Pkcs11RsaPssTest : public Pkcs11RsaPssTestBase {
 public:
  Pkcs11RsaPssTest()
      : Pkcs11RsaPssTestBase(SEC_OID_SHA1, CKG_MGF1_SHA1, SHA1_LENGTH) {}
};

class Pkcs11RsaPssTestWycheproof : public ::testing::Test {
 public:
  struct TestVector {
    uint64_t id;
    std::vector<uint8_t> msg;
    std::vector<uint8_t> sig;
    bool valid;
  };

  Pkcs11RsaPssTestWycheproof() {}

  void Run(const std::string& file) {
    WycheproofHeader("rsa_pss_" + file, "RSASSA-PSS",
                     "rsassa_pss_verify_schema_v1.json",
                     [this](JsonReader& r) { RunGroup(r); });
  }

  static void ReadTestAttr(TestVector& t, const std::string& n, JsonReader& r) {
    if (n == "msg") {
      t.msg = r.ReadHex();
    } else if (n == "sig") {
      t.sig = r.ReadHex();
    } else {
      FAIL() << "unknown key in test: " << n;
    }
  }

 private:
  class Pkcs11RsaPssTestWrap : public Pkcs11RsaPssTestBase {
   public:
    Pkcs11RsaPssTestWrap(SECOidTag hash, CK_RSA_PKCS_MGF_TYPE mgf, int s_len)
        : Pkcs11RsaPssTestBase(hash, mgf, s_len) {}

    void TestBody() {}

    void Verify(const Pkcs11SignatureTestParams& params, bool valid) {
      Pk11SignatureTest::Verify(params, valid);
    }
  };

  void RunTests(const std::vector<uint8_t>& public_key, SECOidTag hash,
                CK_RSA_PKCS_MGF_TYPE mgf, int s_len,
                const std::vector<TestVector>& tests) {
    ASSERT_NE(0u, public_key.size());
    ASSERT_NE(SEC_OID_UNKNOWN, hash);
    ASSERT_NE(CKM_INVALID_MECHANISM, mgf);
    ASSERT_NE(0u, tests.size());

    for (auto& v : tests) {
      std::cout << "Running tcid: " << v.id << std::endl;

      Pkcs11RsaPssTestWrap test(hash, mgf, s_len);
      Pkcs11SignatureTestParams params = {
          DataBuffer(), DataBuffer(public_key.data(), public_key.size()),
          DataBuffer(v.msg.data(), v.msg.size()),
          DataBuffer(v.sig.data(), v.sig.size())};
      test.Verify(params, v.valid);
    }
  }

  void RunGroup(JsonReader& r) {
    std::vector<uint8_t> public_key;
    SECOidTag hash = SEC_OID_UNKNOWN;
    CK_RSA_PKCS_MGF_TYPE mgf = CKM_INVALID_MECHANISM;
    int s_len = 0;
    std::vector<TestVector> tests;
    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }
      if (n == "keySize" || n == "publicKey" || n == "publicKeyAsn" ||
          n == "publicKeyJwk" || n == "publicKeyPem" || n == "source") {
        r.SkipValue();
      } else if (n == "publicKeyDer") {
        public_key = r.ReadHex();
      } else if (n == "mgf") {
        std::string s = r.ReadString();
        ASSERT_EQ(s, "MGF1");
      } else if (n == "mgfSha") {
        std::string s = r.ReadString();
        if (s == "SHA-1") {
          mgf = CKG_MGF1_SHA1;
        } else if (s == "SHA-224") {
          mgf = CKG_MGF1_SHA224;
        } else if (s == "SHA-256") {
          mgf = CKG_MGF1_SHA256;
        } else if (s == "SHA-384") {
          mgf = CKG_MGF1_SHA384;
        } else if (s == "SHA-512") {
          mgf = CKG_MGF1_SHA512;
        } else {
          FAIL() << "unsupported MGF hash";
        }
      } else if (n == "sLen") {
        s_len = static_cast<unsigned int>(r.ReadInt());
      } else if (n == "sha") {
        std::string s = r.ReadString();
        if (s == "SHA-1") {
          hash = SEC_OID_SHA1;
        } else if (s == "SHA-224") {
          hash = SEC_OID_SHA224;
        } else if (s == "SHA-256") {
          hash = SEC_OID_SHA256;
        } else if (s == "SHA-384") {
          hash = SEC_OID_SHA384;
        } else if (s == "SHA-512") {
          hash = SEC_OID_SHA512;
        } else {
          FAIL() << "unsupported hash";
        }
      } else if (n == "type") {
        ASSERT_EQ("RsassaPssVerify", r.ReadString());
      } else if (n == "tests") {
        WycheproofReadTests(r, &tests, ReadTestAttr);
      } else {
        FAIL() << "unknown test group attribute: " << n;
      }
    }

    RunTests(public_key, hash, mgf, s_len, tests);
  }
};

TEST_F(Pkcs11RsaPssTest, GenerateAndSignAndVerify) {
  // Sign data with a 1024-bit RSA key, using PSS/SHA-256.
  SECOidTag hashOid = SEC_OID_SHA256;
  CK_MECHANISM_TYPE hash_mech = CKM_SHA256;
  CK_RSA_PKCS_MGF_TYPE mgf = CKG_MGF1_SHA256;
  PK11RSAGenParams rsaGenParams = {1024, 0x10001};

  // Generate RSA key pair.
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  SECKEYPublicKey* pub_keyRaw = nullptr;
  ScopedSECKEYPrivateKey privKey(
      PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN, &rsaGenParams,
                           &pub_keyRaw, false, PK11_IsFIPS(), nullptr));
  ASSERT_TRUE(!!privKey && pub_keyRaw);
  ScopedSECKEYPublicKey pub_key(pub_keyRaw);

  // Generate random data to sign.
  uint8_t dataBuf[50];
  SECItem data = {siBuffer, dataBuf, sizeof(dataBuf)};
  unsigned int hLen = HASH_ResultLenByOidTag(hashOid);
  SECStatus rv = PK11_GenerateRandomOnSlot(slot.get(), data.data, data.len);
  EXPECT_EQ(rv, SECSuccess);

  // Allocate memory for the signature.
  std::vector<uint8_t> sigBuf(PK11_SignatureLen(privKey.get()));
  SECItem sig = {siBuffer, &sigBuf[0],
                 static_cast<unsigned int>(sigBuf.size())};

  // Set up PSS parameters.
  CK_RSA_PKCS_PSS_PARAMS pss_params = {hash_mech, mgf, hLen};
  SECItem params = {siBuffer, reinterpret_cast<unsigned char*>(&pss_params),
                    sizeof(pss_params)};

  // Sign.
  rv = PK11_SignWithMechanism(privKey.get(), mechanism(), &params, &sig, &data);
  EXPECT_EQ(rv, SECSuccess);

  // Verify.
  rv = PK11_VerifyWithMechanism(pub_key.get(), mechanism(), &params, &sig,
                                &data, nullptr);
  EXPECT_EQ(rv, SECSuccess);

  // Verification with modified data must fail.
  data.data[0] ^= 0xff;
  rv = PK11_VerifyWithMechanism(pub_key.get(), mechanism(), &params, &sig,
                                &data, nullptr);
  EXPECT_EQ(rv, SECFailure);

  // Verification with original data but the wrong signature must fail.
  data.data[0] ^= 0xff;  // Revert previous changes.
  sig.data[0] ^= 0xff;
  rv = PK11_VerifyWithMechanism(pub_key.get(), mechanism(), &params, &sig,
                                &data, nullptr);
  EXPECT_EQ(rv, SECFailure);
}

TEST_F(Pkcs11RsaPssTest, NoLeakWithInvalidExponent) {
  // Attempt to generate an RSA key with a public exponent of 1. This should
  // fail, but it shouldn't leak memory.
  PK11RSAGenParams rsaGenParams = {1024, 0x01};

  // Generate RSA key pair.
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  SECKEYPublicKey* pub_key = nullptr;
  SECKEYPrivateKey* privKey =
      PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN, &rsaGenParams,
                           &pub_key, false, PK11_IsFIPS(), nullptr);
  EXPECT_FALSE(privKey);
  EXPECT_FALSE(pub_key);
}

TEST_F(Pkcs11RsaPssTest, EmptySignature) {
  PK11RSAGenParams rsaGenParams = {1024, 0x10001};
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  SECKEYPublicKey* pub_keyRaw = nullptr;
  ScopedSECKEYPrivateKey privKey(
      PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN, &rsaGenParams,
                           &pub_keyRaw, false, PK11_IsFIPS(), nullptr));
  ASSERT_TRUE(!!privKey && pub_keyRaw);
  ScopedSECKEYPublicKey pub_key(pub_keyRaw);

  CK_RSA_PKCS_PSS_PARAMS pss_params = {CKM_SHA256, CKG_MGF1_SHA256,
                                       SHA256_LENGTH};
  SECItem params = {siBuffer, reinterpret_cast<unsigned char*>(&pss_params),
                    sizeof(pss_params)};
  VerifyEmptySignatureFails(pub_key.get(), CKM_RSA_PKCS_PSS, &params);
  VerifyEmptySignatureFails(pub_key.get(), CKM_SHA256_RSA_PKCS_PSS, &params);
}

class Pkcs11RsaPssVectorTest
    : public Pkcs11RsaPssTest,
      public ::testing::WithParamInterface<Pkcs11SignatureTestParams> {};

TEST_P(Pkcs11RsaPssVectorTest, Verify) {
  Pk11SignatureTest::Verify(GetParam());
}

TEST_P(Pkcs11RsaPssVectorTest, SignAndVerify) { SignAndVerify(GetParam()); }

#define VECTOR(pkcs8, spki, data, sig)                               \
  {DataBuffer(pkcs8, sizeof(pkcs8)), DataBuffer(spki, sizeof(spki)), \
   DataBuffer(data, sizeof(data)), DataBuffer(sig, sizeof(sig))}
#define VECTOR_N(n)                                                         \
  VECTOR(kTestVector##n##Pkcs8, kTestVector##n##Spki, kTestVector##n##Data, \
         kTestVector##n##Sig)

static const Pkcs11SignatureTestParams kRsaPssVectors[] = {
    // RSA-PSS test vectors, pss-vect.txt, Example 1.1: A 1024-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(1),
    // RSA-PSS test vectors, pss-vect.txt, Example 2.1: A 1025-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(2),
    // RSA-PSS test vectors, pss-vect.txt, Example 3.1: A 1026-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(3),
    // RSA-PSS test vectors, pss-vect.txt, Example 4.1: A 1027-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(4),
    // RSA-PSS test vectors, pss-vect.txt, Example 5.1: A 1028-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(5),
    // RSA-PSS test vectors, pss-vect.txt, Example 6.1: A 1029-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(6),
    // RSA-PSS test vectors, pss-vect.txt, Example 7.1: A 1030-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(7),
    // RSA-PSS test vectors, pss-vect.txt, Example 8.1: A 1031-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(8),
    // RSA-PSS test vectors, pss-vect.txt, Example 9.1: A 1536-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(9),
    // RSA-PSS test vectors, pss-vect.txt, Example 10.1: A 2048-bit RSA Key Pair
    // <ftp://ftp.rsasecurity.com/pub/pkcs/pkcs-1/pkcs-1v2-1-vec.zip>
    VECTOR_N(10)};

INSTANTIATE_TEST_SUITE_P(RsaPssSignVerify, Pkcs11RsaPssVectorTest,
                         ::testing::ValuesIn(kRsaPssVectors));

TEST_F(Pkcs11RsaPssTestWycheproof, RsaPss2048Sha1) { Run("2048_sha1_mgf1_20"); }
TEST_F(Pkcs11RsaPssTestWycheproof, RsaPss2048Sha256_0) {
  Run("2048_sha256_mgf1_0");
}
TEST_F(Pkcs11RsaPssTestWycheproof, RsaPss2048Sha256_32) {
  Run("2048_sha256_mgf1_32");
}
TEST_F(Pkcs11RsaPssTestWycheproof, RsaPss3072Sha256) {
  Run("3072_sha256_mgf1_32");
}
TEST_F(Pkcs11RsaPssTestWycheproof, RsaPss4096Sha256) {
  Run("4096_sha256_mgf1_32");
}
TEST_F(Pkcs11RsaPssTestWycheproof, RsaPss4096Sha512) {
  Run("4096_sha512_mgf1_32");
}
TEST_F(Pkcs11RsaPssTestWycheproof, RsaPssMisc) { Run("misc"); }

}  // namespace nss_test
