/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <memory>
#include "nss.h"
#include "pk11pub.h"
#include "sechash.h"
#include "cryptohi.h"

#include "cpputil.h"
#include "gtest/gtest.h"
#include "json_reader.h"
#include "nss_scoped_ptrs.h"
#include "testvectors/curve25519-vectors.h"

#include "pk11_ecdsa_vectors.h"
#include "pk11_signature_test.h"
#include "pk11_keygen.h"

namespace nss_test {

CK_MECHANISM_TYPE
EcHashToComboMech(SECOidTag hash) {
  switch (hash) {
    case SEC_OID_SHA1:
      return CKM_ECDSA_SHA1;
    case SEC_OID_SHA224:
      return CKM_ECDSA_SHA224;
    case SEC_OID_SHA256:
      return CKM_ECDSA_SHA256;
    case SEC_OID_SHA384:
      return CKM_ECDSA_SHA384;
    case SEC_OID_SHA512:
      return CKM_ECDSA_SHA512;
    default:
      break;
  }
  return CKM_INVALID_MECHANISM;
}

class Pkcs11EcdsaTestBase : public Pk11SignatureTest {
 protected:
  Pkcs11EcdsaTestBase(SECOidTag hash_oid)
      : Pk11SignatureTest(CKM_ECDSA, hash_oid, EcHashToComboMech(hash_oid)) {}
};

struct Pkcs11EcdsaTestParams {
  SECOidTag hash_oid_;
  Pkcs11SignatureTestParams sig_params_;
};

class Pkcs11EcdsaTest
    : public Pkcs11EcdsaTestBase,
      public ::testing::WithParamInterface<Pkcs11EcdsaTestParams> {
 public:
  Pkcs11EcdsaTest() : Pkcs11EcdsaTestBase(GetParam().hash_oid_) {}
};

TEST_P(Pkcs11EcdsaTest, Verify) { Verify(GetParam().sig_params_); }

TEST_P(Pkcs11EcdsaTest, SignAndVerify) {
  SignAndVerify(GetParam().sig_params_);
}

TEST_P(Pkcs11EcdsaTest, ImportExport) {
  ImportExport(GetParam().sig_params_.pkcs8_);
}

static const Pkcs11EcdsaTestParams kEcdsaVectors[] = {
    {SEC_OID_SHA256,
     {DataBuffer(kP256Pkcs8, sizeof(kP256Pkcs8)),
      DataBuffer(kP256Spki, sizeof(kP256Spki)),
      DataBuffer(kP256Data, sizeof(kP256Data)),
      DataBuffer(kP256Signature, sizeof(kP256Signature))}},
    {SEC_OID_SHA256,
     {DataBuffer(kP256Pkcs8KeyLen30, sizeof(kP256Pkcs8KeyLen30)),
      DataBuffer(kP256SpkiKeyLen, sizeof(kP256SpkiKeyLen)),
      DataBuffer(kP256DataKeyLen, sizeof(kP256DataKeyLen)),
      DataBuffer(kP256SignatureKeyLen, sizeof(kP256SignatureKeyLen))}},
    {SEC_OID_SHA256,
     {DataBuffer(kP256Pkcs8KeyLen33, sizeof(kP256Pkcs8KeyLen33)),
      DataBuffer(kP256SpkiKeyLen, sizeof(kP256SpkiKeyLen)),
      DataBuffer(kP256DataKeyLen, sizeof(kP256DataKeyLen)),
      DataBuffer(kP256SignatureKeyLen, sizeof(kP256SignatureKeyLen))}},
    {SEC_OID_SHA384,
     {DataBuffer(kP384Pkcs8, sizeof(kP384Pkcs8)),
      DataBuffer(kP384Spki, sizeof(kP384Spki)),
      DataBuffer(kP384Data, sizeof(kP384Data)),
      DataBuffer(kP384Signature, sizeof(kP384Signature))}},
    {SEC_OID_SHA256,
     {DataBuffer(kP384Pkcs8KeyLen46, sizeof(kP384Pkcs8KeyLen46)),
      DataBuffer(kP384SpkiKeyLen, sizeof(kP384SpkiKeyLen)),
      DataBuffer(kP384DataKeyLen, sizeof(kP384DataKeyLen)),
      DataBuffer(kP384SignatureKeyLen, sizeof(kP384SignatureKeyLen))}},
    {SEC_OID_SHA256,
     {DataBuffer(kP384Pkcs8KeyLen49, sizeof(kP384Pkcs8KeyLen49)),
      DataBuffer(kP384SpkiKeyLen, sizeof(kP384SpkiKeyLen)),
      DataBuffer(kP384DataKeyLen, sizeof(kP384DataKeyLen)),
      DataBuffer(kP384SignatureKeyLen, sizeof(kP384SignatureKeyLen))}},
    {SEC_OID_SHA512,
     {DataBuffer(kP521Pkcs8, sizeof(kP521Pkcs8)),
      DataBuffer(kP521Spki, sizeof(kP521Spki)),
      DataBuffer(kP521Data, sizeof(kP521Data)),
      DataBuffer(kP521Signature, sizeof(kP521Signature))}},
    {SEC_OID_SHA256,
     {DataBuffer(kP521Pkcs8KeyLen64, sizeof(kP521Pkcs8KeyLen64)),
      DataBuffer(kP521SpkiKeyLen, sizeof(kP521SpkiKeyLen)),
      DataBuffer(kP521DataKeyLen, sizeof(kP521DataKeyLen)),
      DataBuffer(kP521SignatureKeyLen, sizeof(kP521SignatureKeyLen))}},
    {SEC_OID_SHA256,
     {DataBuffer(kP521Pkcs8KeyLen67, sizeof(kP521Pkcs8KeyLen67)),
      DataBuffer(kP521SpkiKeyLen, sizeof(kP521SpkiKeyLen)),
      DataBuffer(kP521DataKeyLen, sizeof(kP521DataKeyLen)),
      DataBuffer(kP521SignatureKeyLen, sizeof(kP521SignatureKeyLen))}}};

INSTANTIATE_TEST_SUITE_P(EcdsaSignVerify, Pkcs11EcdsaTest,
                         ::testing::ValuesIn(kEcdsaVectors));

class Pkcs11EcdsaSha256Test : public Pkcs11EcdsaTestBase {
 public:
  Pkcs11EcdsaSha256Test() : Pkcs11EcdsaTestBase(SEC_OID_SHA256) {}
};

// Importing a private key in PKCS#8 format must fail when the outer AlgID
// struct contains neither id-ecPublicKey nor a namedCurve parameter.
TEST_F(Pkcs11EcdsaSha256Test, ImportNoCurveOIDOrAlgorithmParams) {
  DataBuffer k(kP256Pkcs8NoCurveOIDOrAlgorithmParams,
               sizeof(kP256Pkcs8NoCurveOIDOrAlgorithmParams));
  EXPECT_FALSE(ImportPrivateKey(k));
};

TEST_F(Pkcs11EcdsaSha256Test, EmptySignature) {
  ScopedSECKEYPublicKey pub_key(
      ImportPublicKey(DataBuffer(kP256Spki, sizeof(kP256Spki))));
  ASSERT_TRUE(pub_key);

  VerifyEmptySignatureFails(pub_key.get(), CKM_ECDSA);
  VerifyEmptySignatureFails(pub_key.get(), CKM_ECDSA_SHA256);
}

// Importing a private key in PKCS#8 format must succeed when only the outer
// AlgID struct contains the namedCurve parameters.
TEST_F(Pkcs11EcdsaSha256Test, ImportOnlyAlgorithmParams) {
  DataBuffer k(kP256Pkcs8OnlyAlgorithmParams,
               sizeof(kP256Pkcs8OnlyAlgorithmParams));
  DataBuffer data(kP256Data, sizeof(kP256Data));
  DataBuffer sig;
  DataBuffer sig2;
  EXPECT_TRUE(ImportPrivateKeyAndSignHashedData(k, data, &sig, &sig2));
};

// Importing a private key in PKCS#8 format must succeed when the outer AlgID
// struct and the inner ECPrivateKey contain the same namedCurve parameters.
// The inner curveOID is always ignored, so only the outer one will be used.
TEST_F(Pkcs11EcdsaSha256Test, ImportMatchingCurveOIDAndAlgorithmParams) {
  DataBuffer k(kP256Pkcs8MatchingCurveOIDAndAlgorithmParams,
               sizeof(kP256Pkcs8MatchingCurveOIDAndAlgorithmParams));
  DataBuffer data(kP256Data, sizeof(kP256Data));
  DataBuffer sig;
  DataBuffer sig2;
  EXPECT_TRUE(ImportPrivateKeyAndSignHashedData(k, data, &sig, &sig2));
};

// Importing a private key in PKCS#8 format must succeed when the outer AlgID
// struct and the inner ECPrivateKey contain dissimilar namedCurve parameters.
// The inner curveOID is always ignored, so only the outer one will be used.
TEST_F(Pkcs11EcdsaSha256Test, ImportDissimilarCurveOIDAndAlgorithmParams) {
  DataBuffer k(kP256Pkcs8DissimilarCurveOIDAndAlgorithmParams,
               sizeof(kP256Pkcs8DissimilarCurveOIDAndAlgorithmParams));
  DataBuffer data(kP256Data, sizeof(kP256Data));
  DataBuffer sig;
  DataBuffer sig2;
  EXPECT_TRUE(ImportPrivateKeyAndSignHashedData(k, data, &sig, &sig2));
};

// Importing a private key in PKCS#8 format must fail when the outer ASN.1
// AlgorithmID struct contains only id-ecPublicKey but no namedCurve parameter.
TEST_F(Pkcs11EcdsaSha256Test, ImportNoAlgorithmParams) {
  DataBuffer k(kP256Pkcs8NoAlgorithmParams,
               sizeof(kP256Pkcs8NoAlgorithmParams));
  EXPECT_FALSE(ImportPrivateKey(k));
};

// Importing a private key in PKCS#8 format must fail when id-ecPublicKey is
// given (so we know it's an EC key) but the namedCurve parameter is unknown.
TEST_F(Pkcs11EcdsaSha256Test, ImportInvalidAlgorithmParams) {
  DataBuffer k(kP256Pkcs8InvalidAlgorithmParams,
               sizeof(kP256Pkcs8InvalidAlgorithmParams));
  EXPECT_FALSE(ImportPrivateKey(k));
};

// Importing a private key in PKCS#8 format with a point not on the curve will
// succeed. Using the contained public key however will fail when trying to
// import it before using it for any operation.
TEST_F(Pkcs11EcdsaSha256Test, ImportPointNotOnCurve) {
  DataBuffer k(kP256Pkcs8PointNotOnCurve, sizeof(kP256Pkcs8PointNotOnCurve));
  ScopedSECKEYPrivateKey privKey(ImportPrivateKey(k));
  ASSERT_TRUE(privKey);

  ScopedSECKEYPublicKey pubKey(SECKEY_ConvertToPublicKey(privKey.get()));
  // A FIPS token gets the key unwrapped, without a CKA_EC_POINT, and rejects
  // the point as soon as it has to produce the public key.
  if (PK11_IsFIPS()) {
    EXPECT_FALSE(pubKey);
    return;
  }
  ASSERT_TRUE(pubKey);

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  auto handle = PK11_ImportPublicKey(slot.get(), pubKey.get(), false);
  EXPECT_EQ(handle, static_cast<decltype(handle)>(CK_INVALID_HANDLE));
};

// Importing a private key in PKCS#8 without a public key will succeed
// if NSS supports public key derivation for this curve (P-256, P-384, P-521).
TEST_F(Pkcs11EcdsaSha256Test, ImportNoPublicKey) {
  DataBuffer k(kP256Pkcs8NoPublicKey, sizeof(kP256Pkcs8NoPublicKey));
  ScopedSECKEYPrivateKey privKey(ImportPrivateKey(k));
  ASSERT_TRUE(privKey);

  ScopedSECKEYPublicKey pubKey(SECKEY_ConvertToPublicKey(privKey.get()));
  ASSERT_TRUE(pubKey);

  ASSERT_NE(pubKey->u.ec.publicValue.len, 0ul);
};

// Importing a public key in SPKI format must fail when id-ecPublicKey is
// given (so we know it's an EC key) but the namedCurve parameter is missing.
TEST_F(Pkcs11EcdsaSha256Test, ImportSpkiNoAlgorithmParams) {
  DataBuffer k(kP256SpkiNoAlgorithmParams, sizeof(kP256SpkiNoAlgorithmParams));
  EXPECT_FALSE(ImportPublicKey(k));
}

// Importing a public key in SPKI format with a point not on the curve will
// succeed. Using the public key however will fail when trying to import
// it before using it for any operation.
TEST_F(Pkcs11EcdsaSha256Test, ImportSpkiPointNotOnCurve) {
  DataBuffer k(kP256SpkiPointNotOnCurve, sizeof(kP256SpkiPointNotOnCurve));
  ScopedSECKEYPublicKey pubKey(ImportPublicKey(k));
  ASSERT_TRUE(pubKey);

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  auto handle = PK11_ImportPublicKey(slot.get(), pubKey.get(), false);
  EXPECT_EQ(handle, static_cast<decltype(handle)>(CK_INVALID_HANDLE));
}

class Pkcs11EcdsaWycheproofTest : public ::testing::Test {
 protected:
  void Run(const std::string& name) {
    WycheproofHeader(name, "ECDSA", "ecdsa_verify_schema_v1.json",
                     [this](JsonReader& r) { RunGroup(r); });
  }

 private:
  void RunGroup(JsonReader& r) {
    std::vector<EcdsaTestVector> tests;
    std::vector<uint8_t> public_key;
    SECOidTag hash_oid = SEC_OID_UNKNOWN;

    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }

      if (n == "publicKey" || n == "publicKeyPem" || n == "source") {
        r.SkipValue();
      } else if (n == "publicKeyDer") {
        public_key = r.ReadHex();
      } else if (n == "sha") {
        hash_oid = r.ReadHash();
      } else if (n == "type") {
        ASSERT_EQ("EcdsaVerify", r.ReadString());
      } else if (n == "tests") {
        WycheproofReadTests(r, &tests, ReadTestAttr, true,
                            [](EcdsaTestVector& t, const std::string&,
                               const std::vector<std::string>& flags) {
                              // NSS accepts signatures that are missing the
                              // leading zero on a positive integer.
                              if (std::find(flags.begin(), flags.end(),
                                            "MissingZero") != flags.end()) {
                                t.valid = true;
                              }
                            });
      } else {
        FAIL() << "unknown label in group: " << n;
      }
    }

    for (auto& t : tests) {
      std::cout << "Running test " << t.id << std::endl;
      t.public_key = public_key;
      t.hash_oid = hash_oid;
      Derive(t);
    }
  }

  static void ReadTestAttr(EcdsaTestVector& t, const std::string& n,
                           JsonReader& r) {
    if (n == "msg") {
      t.msg = r.ReadHex();
    } else if (n == "sig") {
      t.sig = r.ReadHex();
    } else {
      FAIL() << "unknown test key: " << n;
    }
  }

  void Derive(const EcdsaTestVector& vec) {
    SECItem spki_item = {siBuffer, toUcharPtr(vec.public_key.data()),
                         static_cast<unsigned int>(vec.public_key.size())};
    SECItem sig_item = {siBuffer, toUcharPtr(vec.sig.data()),
                        static_cast<unsigned int>(vec.sig.size())};

    DataBuffer hash;
    hash.Allocate(static_cast<size_t>(HASH_ResultLenByOidTag(vec.hash_oid)));
    SECStatus rv = PK11_HashBuf(vec.hash_oid, toUcharPtr(hash.data()),
                                toUcharPtr(vec.msg.data()), vec.msg.size());
    ASSERT_EQ(rv, SECSuccess);
    SECItem hash_item = {siBuffer, toUcharPtr(hash.data()),
                         static_cast<unsigned int>(hash.len())};

    ScopedCERTSubjectPublicKeyInfo cert_spki(
        SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
    ASSERT_TRUE(cert_spki);
    ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
    ASSERT_TRUE(pub_key);

    rv = VFY_VerifyDigestDirect(&hash_item, pub_key.get(), &sig_item,
                                SEC_OID_ANSIX962_EC_PUBLIC_KEY, vec.hash_oid,
                                nullptr);
    EXPECT_EQ(rv, vec.valid ? SECSuccess : SECFailure);
  };
};

TEST_F(Pkcs11EcdsaWycheproofTest, P256) { Run("ecdsa_secp256r1_sha256"); }
TEST_F(Pkcs11EcdsaWycheproofTest, P256Sha512) { Run("ecdsa_secp256r1_sha512"); }
TEST_F(Pkcs11EcdsaWycheproofTest, P384) { Run("ecdsa_secp384r1_sha384"); }
TEST_F(Pkcs11EcdsaWycheproofTest, P384Sha512) { Run("ecdsa_secp384r1_sha512"); }
TEST_F(Pkcs11EcdsaWycheproofTest, P521) { Run("ecdsa_secp521r1_sha512"); }

class Pkcs11EcdsaRoundtripTest
    : public Pkcs11EcdsaTestBase,
      public ::testing::WithParamInterface<SECOidTag> {
 public:
  Pkcs11EcdsaRoundtripTest() : Pkcs11EcdsaTestBase(SEC_OID_SHA256) {}

 protected:
  void GenerateExportImportSignVerify(SECOidTag tag) {
    Pkcs11KeyPairGenerator generator(CKM_EC_KEY_PAIR_GEN, tag);
    ScopedSECKEYPrivateKey priv;
    ScopedSECKEYPublicKey pub;
    generator.GenerateKey(&priv, &pub, false);

    DataBuffer exported;
    ExportPrivateKey(&priv, exported);

    if (tag != SEC_OID_CURVE25519) {
      DataBuffer sig;
      DataBuffer sig2;
      DataBuffer data(kP256Data, sizeof(kP256Data));
      ASSERT_TRUE(
          ImportPrivateKeyAndSignHashedData(exported, data, &sig, &sig2));

      Verify(pub, data, sig);
    }
  }
};

TEST_P(Pkcs11EcdsaRoundtripTest, GenerateExportImportSignVerify) {
  GenerateExportImportSignVerify(GetParam());
}
INSTANTIATE_TEST_SUITE_P(Pkcs11EcdsaRoundtripTest, Pkcs11EcdsaRoundtripTest,
                         ::testing::Values(SEC_OID_SECG_EC_SECP256R1,
                                           SEC_OID_SECG_EC_SECP384R1,
                                           SEC_OID_SECG_EC_SECP521R1,
                                           SEC_OID_CURVE25519));

class Pkcs11EcdsaUnpaddedSignatureTest
    : public Pkcs11EcdsaTestBase,
      public ::testing::WithParamInterface<Pkcs11EcdsaTestParams> {
 public:
  Pkcs11EcdsaUnpaddedSignatureTest()
      : Pkcs11EcdsaTestBase(GetParam().hash_oid_) {}
};

static const Pkcs11EcdsaTestParams kEcdsaUnpaddedSignaturesVectors[] = {
    {SEC_OID_SHA512,
     {DataBuffer(NULL, 0),
      DataBuffer(kP256SpkiUnpaddedSig, sizeof(kP256SpkiUnpaddedSig)),
      DataBuffer(kP256DataUnpaddedSigLong, sizeof(kP256DataUnpaddedSigLong)),
      DataBuffer(kP256SignatureUnpaddedSigLong,
                 sizeof(kP256SignatureUnpaddedSigLong))}},
    {SEC_OID_SHA512,
     {DataBuffer(NULL, 0),
      DataBuffer(kP256SpkiUnpaddedSig, sizeof(kP256SpkiUnpaddedSig)),
      DataBuffer(kP256DataUnpaddedSigShort, sizeof(kP256DataUnpaddedSigShort)),
      DataBuffer(kP256SignatureUnpaddedSigShort,
                 sizeof(kP256SignatureUnpaddedSigShort))}},
    {SEC_OID_SHA512,
     {DataBuffer(NULL, 0),
      DataBuffer(kP384SpkiUnpaddedSig, sizeof(kP384SpkiUnpaddedSig)),
      DataBuffer(kP384DataUnpaddedSigLong, sizeof(kP384DataUnpaddedSigLong)),
      DataBuffer(kP384SignatureUnpaddedSigLong,
                 sizeof(kP384SignatureUnpaddedSigLong))}},
    {SEC_OID_SHA512,
     {DataBuffer(NULL, 0),
      DataBuffer(kP384SpkiUnpaddedSig, sizeof(kP384SpkiUnpaddedSig)),
      DataBuffer(kP384DataUnpaddedSigShort, sizeof(kP384DataUnpaddedSigShort)),
      DataBuffer(kP384SignatureUnpaddedSigShort,
                 sizeof(kP384SignatureUnpaddedSigShort))}},
    {SEC_OID_SHA512,
     {DataBuffer(NULL, 0),
      DataBuffer(kP521SpkiUnpaddedSig, sizeof(kP521SpkiUnpaddedSig)),
      DataBuffer(kP521DataUnpaddedSigLong, sizeof(kP521DataUnpaddedSigLong)),
      DataBuffer(kP521SignatureUnpaddedSigLong,
                 sizeof(kP521SignatureUnpaddedSigLong))}},
    {SEC_OID_SHA512,
     {DataBuffer(NULL, 0),
      DataBuffer(kP521SpkiUnpaddedSig, sizeof(kP521SpkiUnpaddedSig)),
      DataBuffer(kP521DataUnpaddedSigShort, sizeof(kP521DataUnpaddedSigShort)),
      DataBuffer(kP521SignatureUnpaddedSigShort,
                 sizeof(kP521SignatureUnpaddedSigShort))}}};

TEST_P(Pkcs11EcdsaUnpaddedSignatureTest, Verify) {
  Verify(GetParam().sig_params_);
}
INSTANTIATE_TEST_SUITE_P(EcdsaVerifyUnpaddedSignatures,
                         Pkcs11EcdsaUnpaddedSignatureTest,
                         ::testing::ValuesIn(kEcdsaUnpaddedSignaturesVectors));
}  // namespace nss_test
