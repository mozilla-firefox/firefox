/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"
#include "sechash.h"
#include "cryptohi.h"

#include "cpputil.h"
#include "json_reader.h"
#include "nss_scoped_ptrs.h"
#include "testvectors_base/test-structs.h"

#include "pk11_eddsa_vectors.h"
#include "pk11_signature_test.h"
#include "pk11_keygen.h"

namespace nss_test {
static const Pkcs11SignatureTestParams kEddsaVectors[] = {
    {DataBuffer(kEd25519Pkcs8_1, sizeof(kEd25519Pkcs8_1)),
     DataBuffer(kEd25519Spki_1, sizeof(kEd25519Spki_1)),
     DataBuffer(kEd25519Message_1, sizeof(kEd25519Message_1)),
     DataBuffer(kEd25519Signature_1, sizeof(kEd25519Signature_1))},

    {DataBuffer(kEd25519Pkcs8_2, sizeof(kEd25519Pkcs8_2)),
     DataBuffer(kEd25519Spki_2, sizeof(kEd25519Spki_2)),
     DataBuffer(kEd25519Message_2, sizeof(kEd25519Message_2)),
     DataBuffer(kEd25519Signature_2, sizeof(kEd25519Signature_2))},

    {DataBuffer(kEd25519Pkcs8_3, sizeof(kEd25519Pkcs8_3)),
     DataBuffer(kEd25519Spki_3, sizeof(kEd25519Spki_3)),
     DataBuffer(kEd25519Message_3, sizeof(kEd25519Message_3)),
     DataBuffer(kEd25519Signature_3, sizeof(kEd25519Signature_3))},

    {DataBuffer(kEd25519Pkcs8_4, sizeof(kEd25519Pkcs8_4)),
     DataBuffer(kEd25519Spki_4, sizeof(kEd25519Spki_4)),
     DataBuffer(kEd25519Message_4, sizeof(kEd25519Message_4)),
     DataBuffer(kEd25519Signature_4, sizeof(kEd25519Signature_4))}};

class Pkcs11EddsaTest
    : public Pk11SignatureTest,
      public ::testing::WithParamInterface<Pkcs11SignatureTestParams> {
 protected:
  Pkcs11EddsaTest() : Pk11SignatureTest(CKM_EDDSA) {}
};

TEST_P(Pkcs11EddsaTest, SignAndVerify) { SignAndVerifyRaw(GetParam()); }

TEST_P(Pkcs11EddsaTest, ImportExport) { ImportExport(GetParam().pkcs8_); }

TEST_P(Pkcs11EddsaTest, ImportConvertToPublic) {
  ScopedSECKEYPrivateKey privKey(ImportPrivateKey(GetParam().pkcs8_));
  ASSERT_TRUE(privKey);

  ScopedSECKEYPublicKey pubKey(SECKEY_ConvertToPublicKey(privKey.get()));
  ASSERT_TRUE(pubKey);
}

TEST_P(Pkcs11EddsaTest, ImportPublicCreateSubjectPKInfo) {
  ScopedSECKEYPrivateKey privKey(ImportPrivateKey(GetParam().pkcs8_));
  ASSERT_TRUE(privKey);

  ScopedSECKEYPublicKey pubKey(
      (SECKEYPublicKey*)SECKEY_ConvertToPublicKey(privKey.get()));
  ASSERT_TRUE(pubKey);

  ScopedSECItem der_spki(SECKEY_EncodeDERSubjectPublicKeyInfo(pubKey.get()));
  ASSERT_TRUE(der_spki);
  ASSERT_EQ(der_spki->len, GetParam().spki_.len());
  ASSERT_EQ(0, memcmp(der_spki->data, GetParam().spki_.data(), der_spki->len));
}

TEST_P(Pkcs11EddsaTest, EmptySignature) {
  ScopedSECKEYPublicKey pub_key(ImportPublicKey(GetParam().spki_));
  ASSERT_TRUE(pub_key);

  VerifyEmptySignatureFails(pub_key.get(), mechanism());
}

INSTANTIATE_TEST_SUITE_P(EddsaSignVerify, Pkcs11EddsaTest,
                         ::testing::ValuesIn(kEddsaVectors));

class Pkcs11EddsaRoundtripTest
    : public Pk11SignatureTest,
      public ::testing::WithParamInterface<Pkcs11SignatureTestParams> {
 protected:
  Pkcs11EddsaRoundtripTest() : Pk11SignatureTest(CKM_EDDSA) {}

 protected:
  void GenerateExportImportSignVerify(Pkcs11SignatureTestParams params) {
    Pkcs11KeyPairGenerator generator(CKM_EC_EDWARDS_KEY_PAIR_GEN);
    ScopedSECKEYPrivateKey priv;
    ScopedSECKEYPublicKey pub;
    generator.GenerateKey(&priv, &pub, false);

    DataBuffer exported;
    ExportPrivateKey(&priv, exported);

    ScopedSECKEYPrivateKey privKey(ImportPrivateKey(exported));
    ASSERT_NE(privKey, nullptr);
    DataBuffer sig;

    SignRaw(privKey, params.data_, &sig);
    Verify(pub, params.data_, sig);
  }
};

TEST_P(Pkcs11EddsaRoundtripTest, GenerateExportImportSignVerify) {
  GenerateExportImportSignVerify(GetParam());
}

INSTANTIATE_TEST_SUITE_P(EddsaRound, Pkcs11EddsaRoundtripTest,
                         ::testing::ValuesIn(kEddsaVectors));

class Pkcs11EddsaWycheproofTest : public ::testing::Test {
 protected:
  void Run(const std::string& name) {
    WycheproofHeader(name, "EDDSA", "eddsa_verify_schema_v1.json",
                     [this](JsonReader& r) { RunGroup(r); });
  }

 private:
  void RunGroup(JsonReader& r) {
    std::vector<EddsaTestVector> tests;
    std::vector<uint8_t> public_key;

    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }

      if (n == "publicKey" || n == "publicKeyJwk" || n == "publicKeyPem" ||
          n == "source") {
        r.SkipValue();
      } else if (n == "publicKeyDer") {
        public_key = r.ReadHex();
      } else if (n == "type") {
        ASSERT_EQ("EddsaVerify", r.ReadString());
      } else if (n == "tests") {
        WycheproofReadTests(r, &tests, ReadTestAttr);
      } else {
        FAIL() << "unknown label in group: " << n;
      }
    }

    for (auto& t : tests) {
      std::cout << "Running test " << t.id << std::endl;
      t.public_key = public_key;
      Derive(t);
    }
  }

  static void ReadTestAttr(EddsaTestVector& t, const std::string& n,
                           JsonReader& r) {
    if (n == "msg") {
      t.msg = r.ReadHex();
    } else if (n == "sig") {
      t.sig = r.ReadHex();
    } else {
      FAIL() << "unknown test key: " << n;
    }
  }

  void Derive(const EddsaTestVector& vec) {
    SECItem spki_item = {siBuffer, toUcharPtr(vec.public_key.data()),
                         static_cast<unsigned int>(vec.public_key.size())};
    SECItem sig_item = {siBuffer, toUcharPtr(vec.sig.data()),
                        static_cast<unsigned int>(vec.sig.size())};
    SECItem msg_item = {siBuffer, toUcharPtr(vec.msg.data()),
                        static_cast<unsigned int>(vec.msg.size())};

    ScopedCERTSubjectPublicKeyInfo cert_spki(
        SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
    ASSERT_TRUE(cert_spki);

    ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
    ASSERT_TRUE(pub_key);

    SECStatus rv = PK11_VerifyWithMechanism(pub_key.get(), CKM_EDDSA, nullptr,
                                            &sig_item, &msg_item, nullptr);
    EXPECT_EQ(rv, vec.valid ? SECSuccess : SECFailure);
    if (!vec.valid) {
      // Including signatures of the wrong length.
      EXPECT_EQ(SEC_ERROR_BAD_SIGNATURE, PORT_GetError())
          << "test " << vec.id << ": " << PORT_ErrorToName(PORT_GetError());
    }
  };
};

TEST_F(Pkcs11EddsaWycheproofTest, Ed25519) { Run("ed25519"); }

}  // namespace nss_test
