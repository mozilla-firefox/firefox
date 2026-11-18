/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <vector>
#include "nss.h"
#include "pk11pub.h"
#include "pk11pqg.h"
#include "prerror.h"
#include "secerr.h"
#include "secoid.h"

#include "cpputil.h"
#include "nss_scoped_ptrs.h"
#include "gtest/gtest.h"
#include "databuffer.h"
#include "pk11_import_vectors.h"
#include "pk11_keygen.h"

namespace nss_test {

// This deleter deletes a set of objects, unlike the deleter on
// ScopedPK11GenericObject, which only deletes one.
struct PK11GenericObjectsDeleter {
  void operator()(PK11GenericObject* objs) {
    if (objs) {
      PK11_DestroyGenericObjects(objs);
    }
  }
};

class Pk11KeyImportTestBase : public ::testing::Test {
 public:
  Pk11KeyImportTestBase() = default;
  virtual ~Pk11KeyImportTestBase() = default;

  void SetUp() override {
    slot_.reset(PK11_GetInternalKeySlot());
    ASSERT_TRUE(slot_);

    static const uint8_t pw[] = "pw";
    SECItem pwItem = {siBuffer, toUcharPtr(pw), sizeof(pw)};
    password_.reset(SECITEM_DupItem(&pwItem));
  }

  void Test(const Pkcs11KeyPairGenerator& generator) {
    // Generate a key and export it.
    KeyType key_type = nullKey;
    ScopedSECKEYEncryptedPrivateKeyInfo key_info;
    ScopedSECItem public_value;
    GenerateAndExport(generator, &key_type, &key_info, &public_value);

    // Note: NSS is currently unable export wrapped DH keys, so this doesn't
    // test those beyond generate and verify.
    if (key_type == dhKey) {
      return;
    }
#ifdef NSS_DISABLE_DSA
    // if DSA is disabled, we can't generate a key, Generated will
    // have expected a failure, and checked the error codeso we are now.
    if (generator.mechanism() == CKM_DSA_KEY_PAIR_GEN) {
      return;
    }
#endif
    ASSERT_NE(nullptr, public_value);
    ASSERT_NE(nullptr, key_info);

    // Now import the encrypted key.
    static const uint8_t nick[] = "nick";
    SECItem nickname = {siBuffer, toUcharPtr(nick), sizeof(nick)};
    SECKEYPrivateKey* priv_tmp;
    SECStatus rv = PK11_ImportEncryptedPrivateKeyInfoAndReturnKey(
        slot_.get(), key_info.get(), password_.get(), &nickname,
        public_value.get(), PR_TRUE, PR_TRUE, key_type, 0, &priv_tmp, NULL);
    ASSERT_EQ(SECSuccess, rv) << "PK11_ImportEncryptedPrivateKeyInfo failed "
                              << PORT_ErrorToName(PORT_GetError());
    ScopedSECKEYPrivateKey priv_key(priv_tmp);
    ASSERT_NE(nullptr, priv_key);

    CheckForPublicKey(priv_key, public_value.get());
  }

 private:
  SECItem GetPublicComponent(ScopedSECKEYPublicKey& pub_key) {
    SECItem null = {siBuffer, NULL, 0};
    switch (SECKEY_GetPublicKeyType(pub_key.get())) {
      case rsaKey:
      case rsaPssKey:
      case rsaOaepKey:
        return pub_key->u.rsa.modulus;
      case keaKey:
        return pub_key->u.kea.publicValue;
      case dsaKey:
        return pub_key->u.dsa.publicValue;
      case dhKey:
        return pub_key->u.dh.publicValue;
      case ecKey:
      case edKey:
      case ecMontKey:
        return pub_key->u.ec.publicValue;
      case kyberKey:
        return pub_key->u.kyber.publicValue;
      case mldsaKey:    /* add import tests when pkcs #8 support is added */
      case fortezzaKey: /* depricated */
      case nullKey:
        /* didn't use default here so we can catch new key types at compile time
         */
        break;
    }
    return null;
  }
  void CheckForPublicKey(const ScopedSECKEYPrivateKey& priv_key,
                         const SECItem* expected_public) {
    // Verify the public key exists.
    StackSECItem priv_id;
    KeyType type = SECKEY_GetPrivateKeyType(priv_key.get());
    SECStatus rv = PK11_ReadRawAttribute(PK11_TypePrivKey, priv_key.get(),
                                         CKA_ID, &priv_id);
    ASSERT_EQ(SECSuccess, rv) << "Couldn't read CKA_ID from private key: "
                              << PORT_ErrorToName(PORT_GetError());

    CK_ATTRIBUTE_TYPE value_type = CKA_VALUE;
    switch (type) {
      case rsaKey:
        value_type = CKA_MODULUS;
        break;

      case dhKey:
      case dsaKey:
        value_type = CKA_VALUE;
        break;

      case ecKey:
        value_type = CKA_EC_POINT;
        break;

      default:
        FAIL() << "unknown key type";
    }

    // Scan public key objects until we find one with the same CKA_ID as
    // priv_key
    std::unique_ptr<PK11GenericObject, PK11GenericObjectsDeleter> objs(
        PK11_FindGenericObjects(slot_.get(), CKO_PUBLIC_KEY));
    ASSERT_NE(nullptr, objs);
    for (PK11GenericObject* obj = objs.get(); obj != nullptr;
         obj = PK11_GetNextGenericObject(obj)) {
      StackSECItem pub_id;
      rv = PK11_ReadRawAttribute(PK11_TypeGeneric, obj, CKA_ID, &pub_id);
      if (rv != SECSuccess) {
        // Can't read CKA_ID from object.
        continue;
      }
      if (!SECITEM_ItemsAreEqual(&priv_id, &pub_id)) {
        // This isn't the object we're looking for.
        continue;
      }

      StackSECItem token;
      rv = PK11_ReadRawAttribute(PK11_TypeGeneric, obj, CKA_TOKEN, &token);
      ASSERT_EQ(SECSuccess, rv);
      ASSERT_EQ(1U, token.len);
      ASSERT_NE(0, token.data[0]);

      StackSECItem raw_value;
      SECItem decoded_value;
      rv = PK11_ReadRawAttribute(PK11_TypeGeneric, obj, value_type, &raw_value);
      ASSERT_EQ(SECSuccess, rv);
      SECItem value = raw_value;

      // Decode the EC_POINT and check the output against expected.
      // CKA_EC_POINT isn't stable, see Bug 1520649.
      ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
      ASSERT_TRUE(arena);
      if (value_type == CKA_EC_POINT) {
        // If this fails due to the noted inconsistency, we may need to
        // check the whole raw_value, or remove a leading UNCOMPRESSED_POINT tag
        rv = SEC_QuickDERDecodeItem(arena.get(), &decoded_value,
                                    SEC_ASN1_GET(SEC_OctetStringTemplate),
                                    &raw_value);
        ASSERT_EQ(SECSuccess, rv);
        value = decoded_value;
      }
      ASSERT_TRUE(SECITEM_ItemsAreEqual(expected_public, &value))
          << "expected: "
          << DataBuffer(expected_public->data, expected_public->len)
          << std::endl
          << "actual: " << DataBuffer(value.data, value.len) << std::endl;

      // Finally, convert the private to public and ensure it matches.
      ScopedSECKEYPublicKey pub_key(SECKEY_ConvertToPublicKey(priv_key.get()));
      ASSERT_TRUE(pub_key);
      SECItem converted_public = GetPublicComponent(pub_key);
      ASSERT_TRUE(converted_public.len != 0);

      ASSERT_TRUE(SECITEM_ItemsAreEqual(expected_public, &converted_public))
          << "expected: "
          << DataBuffer(expected_public->data, expected_public->len)
          << std::endl
          << "actual: "
          << DataBuffer(converted_public.data, converted_public.len)
          << std::endl;
    }
  }

  void GenerateAndExport(const Pkcs11KeyPairGenerator& generator,
                         KeyType* key_type,
                         ScopedSECKEYEncryptedPrivateKeyInfo* key_info,
                         ScopedSECItem* public_value) {
    ScopedSECKEYPrivateKey priv_key;
    ScopedSECKEYPublicKey pub_key;
    generator.GenerateKey(&priv_key, &pub_key);
#ifdef NSS_DISABLE_DSA
    if (generator.mechanism() == CKM_DSA_KEY_PAIR_GEN) {
      ASSERT_FALSE(priv_key);
      return;
    }
#endif
    ASSERT_TRUE(priv_key);

    // Save the public value, which we will need on import
    SECItem* pub_val;
    KeyType t = SECKEY_GetPublicKeyType(pub_key.get());
    switch (t) {
      case rsaKey:
        pub_val = &pub_key->u.rsa.modulus;
        break;
      case dhKey:
        pub_val = &pub_key->u.dh.publicValue;
        break;
      case dsaKey:
        pub_val = &pub_key->u.dsa.publicValue;
        break;
      case ecKey:
        pub_val = &pub_key->u.ec.publicValue;
        break;
      default:
        FAIL() << "Unknown key type";
    }

    CheckForPublicKey(priv_key, pub_val);

    *key_type = t;
    // Note: NSS is currently unable export wrapped DH keys, so this doesn't
    // test those beyond generate and verify.
    if (t == dhKey) {
      return;
    }
    public_value->reset(SECITEM_DupItem(pub_val));

    // Wrap and export the key.
    ScopedSECKEYEncryptedPrivateKeyInfo epki(PK11_ExportEncryptedPrivKeyInfo(
        slot_.get(), SEC_OID_AES_256_CBC, password_.get(), priv_key.get(), 1,
        nullptr));
    ASSERT_NE(nullptr, epki) << "PK11_ExportEncryptedPrivKeyInfo failed: "
                             << PORT_ErrorToName(PORT_GetError());

    key_info->swap(epki);
  }

  ScopedPK11SlotInfo slot_;
  ScopedSECItem password_;
};

class Pk11KeyImportTest
    : public Pk11KeyImportTestBase,
      public ::testing::WithParamInterface<CK_MECHANISM_TYPE> {
 public:
  Pk11KeyImportTest() = default;
  virtual ~Pk11KeyImportTest() = default;
};

TEST_P(Pk11KeyImportTest, GenerateExportImport) {
  Test(Pkcs11KeyPairGenerator(GetParam()));
}

// we go ahead and test for DSA, this only trigger key gen, which will
// check that we fail with a proper error code
INSTANTIATE_TEST_SUITE_P(Pk11KeyImportTest, Pk11KeyImportTest,
                         ::testing::Values(CKM_RSA_PKCS_KEY_PAIR_GEN,
                                           CKM_DSA_KEY_PAIR_GEN,
                                           CKM_DH_PKCS_KEY_PAIR_GEN));

class Pk11KeyImportTestEC : public Pk11KeyImportTestBase,
                            public ::testing::WithParamInterface<SECOidTag> {
 public:
  Pk11KeyImportTestEC() = default;
  virtual ~Pk11KeyImportTestEC() = default;
};

TEST_P(Pk11KeyImportTestEC, GenerateExportImport) {
  Test(Pkcs11KeyPairGenerator(CKM_EC_KEY_PAIR_GEN, GetParam()));
}

INSTANTIATE_TEST_SUITE_P(Pk11KeyImportTestEC, Pk11KeyImportTestEC,
                         ::testing::Values(SEC_OID_SECG_EC_SECP256R1,
                                           SEC_OID_SECG_EC_SECP384R1,
                                           SEC_OID_SECG_EC_SECP521R1,
                                           SEC_OID_CURVE25519));

struct Pkcs11CompressedECKeyTestParams {
  DataBuffer compressedKey;
  DataBuffer uncompressedKey;
};

class Pk11KeyImportTestECCompressed
    : public Pk11KeyImportTestBase,
      public ::testing::WithParamInterface<Pkcs11CompressedECKeyTestParams> {
 public:
  Pk11KeyImportTestECCompressed() = default;
  virtual ~Pk11KeyImportTestECCompressed() = default;
};

// Importing a private key in PKCS#8 format with a point not on the curve will
// succeed. Using the contained public key however will fail when trying to
// import it before using it for any operation.
TEST_P(Pk11KeyImportTestECCompressed, CompressedPointTest) {
  DataBuffer spki(GetParam().compressedKey);
  SECItem spki_item = {siBuffer, toUcharPtr(spki.data()),
                       static_cast<unsigned int>(spki.len())};

  ScopedCERTSubjectPublicKeyInfo cert_spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
  ASSERT_TRUE(cert_spki);
  ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
  ASSERT_TRUE(pub_key);

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  CK_OBJECT_HANDLE id =
      PK11_ImportPublicKey(slot.get(), pub_key.get(), PR_FALSE);
  ASSERT_NE(id, (unsigned int)CK_INVALID_HANDLE);

  StackSECItem publicDecoded;
  SECStatus rv = PK11_ReadRawAttribute(PK11_TypePubKey, pub_key.get(),
                                       CKA_EC_POINT, &publicDecoded);
  ASSERT_EQ(rv, SECSuccess);

  ScopedPLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  ASSERT_TRUE(arena);

  SECItem decodedItem;
  rv = SEC_QuickDERDecodeItem(arena.get(), &decodedItem,
                              SEC_ASN1_GET(SEC_OctetStringTemplate),
                              &publicDecoded);

  ASSERT_EQ(rv, SECSuccess);
  ASSERT_EQ(decodedItem.len, GetParam().uncompressedKey.len());
  ASSERT_EQ(0, PORT_Memcmp(decodedItem.data, GetParam().uncompressedKey.data(),
                           decodedItem.len));
};

static const Pkcs11CompressedECKeyTestParams kCompressedVectors[] = {
    {DataBuffer(kP256CompressedSpki, sizeof(kP256CompressedSpki)),
     DataBuffer(kP256Uncompressed, sizeof(kP256Uncompressed))},
    {DataBuffer(kP384CompressedSpki, sizeof(kP384CompressedSpki)),
     DataBuffer(kP384Uncompressed, sizeof(kP384Uncompressed))},
    {DataBuffer(kP521CompressedSpki, sizeof(kP521CompressedSpki)),
     DataBuffer(kP521Uncompressed, sizeof(kP521Uncompressed))}};

INSTANTIATE_TEST_SUITE_P(Pk11KeyImportTestECCompressed,
                         Pk11KeyImportTestECCompressed,
                         ::testing::ValuesIn(kCompressedVectors));

// Importing a key with 0x7 sign value instead of 0x02/0x03
TEST_F(Pk11KeyImportTestEC, CompressedPointWrongSign) {
  DataBuffer spki(kP256CompressedSpkiWrongSign,
                  sizeof(kP256CompressedSpkiWrongSign));
  SECItem spki_item = {siBuffer, toUcharPtr(spki.data()),
                       static_cast<unsigned int>(spki.len())};

  ScopedCERTSubjectPublicKeyInfo cert_spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
  ASSERT_TRUE(cert_spki);
  ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
  ASSERT_TRUE(pub_key);

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  CK_OBJECT_HANDLE id =
      PK11_ImportPublicKey(slot.get(), pub_key.get(), PR_FALSE);
  ASSERT_EQ(id, (unsigned int)CK_INVALID_HANDLE);
}

TEST_F(Pk11KeyImportTestEC, CompressedPointWrongLen) {
  DataBuffer spki(kP256CompressedSpkiWrongPointLength,
                  sizeof(kP256CompressedSpkiWrongPointLength));
  SECItem spki_item = {siBuffer, toUcharPtr(spki.data()),
                       static_cast<unsigned int>(spki.len())};

  ScopedCERTSubjectPublicKeyInfo cert_spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
  ASSERT_FALSE(cert_spki);
}

TEST_F(Pk11KeyImportTestEC, CompressedPointNotOnCurve) {
  DataBuffer spki(kP256CompressedSpkiNotOnCurve,
                  sizeof(kP256CompressedSpkiNotOnCurve));
  SECItem spki_item = {siBuffer, toUcharPtr(spki.data()),
                       static_cast<unsigned int>(spki.len())};

  ScopedCERTSubjectPublicKeyInfo cert_spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_item));
  ASSERT_TRUE(cert_spki);
  ScopedSECKEYPublicKey pub_key(SECKEY_ExtractPublicKey(cert_spki.get()));
  ASSERT_TRUE(pub_key);

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  CK_OBJECT_HANDLE id =
      PK11_ImportPublicKey(slot.get(), pub_key.get(), PR_FALSE);
  ASSERT_NE(id, (unsigned int)CK_INVALID_HANDLE);
}

// A public key on |curve| that isn't in any slot yet, taken from a key pair
// generated on it.
static ScopedSECKEYPublicKey PublicKeyOnCurve(SECOidTag curve) {
  CK_MECHANISM_TYPE mech;
  switch (curve) {
    case SEC_OID_ED25519_PUBLIC_KEY:
      mech = CKM_EC_EDWARDS_KEY_PAIR_GEN;
      break;
    case SEC_OID_X25519:
    case SEC_OID_CURVE25519:
      mech = CKM_EC_MONTGOMERY_KEY_PAIR_GEN;
      break;
    default:
      mech = CKM_EC_KEY_PAIR_GEN;
      break;
  }
  ScopedSECKEYPrivateKey priv;
  ScopedSECKEYPublicKey pub;
  Pkcs11KeyPairGenerator(mech, curve).GenerateKey(&priv, &pub);
  if (!pub) {
    return nullptr;
  }
  return ScopedSECKEYPublicKey(SECKEY_CopyPublicKey(pub.get()));
}

// Create a session Edwards or Montgomery private key from just its curve and
// a 32-byte private value.
static ScopedSECKEYPrivateKey CreateRawKey(PK11SlotInfo* slot, CK_KEY_TYPE type,
                                           SECOidTag curve) {
  std::vector<uint8_t> params = EcParamsForCurve(curve);
  std::vector<uint8_t> value(32, 0x42);
  CK_OBJECT_CLASS keyClass = CKO_PRIVATE_KEY;
  CK_BBOOL ckfalse = CK_FALSE;
  CK_ATTRIBUTE templ[] = {
      {CKA_CLASS, &keyClass, sizeof(keyClass)},
      {CKA_KEY_TYPE, &type, sizeof(type)},
      {CKA_TOKEN, &ckfalse, sizeof(ckfalse)},
      {CKA_EC_PARAMS, params.data(), static_cast<CK_ULONG>(params.size())},
      {CKA_VALUE, value.data(), static_cast<CK_ULONG>(value.size())}};
  return ScopedSECKEYPrivateKey(PK11_CreatePrivateKeyFromTemplate(
      slot, templ, PR_ARRAY_SIZE(templ), nullptr));
}

struct Pk11EcKeyTypeCurveParams {
  CK_KEY_TYPE keyType;
  SECOidTag curve;
  bool accepted;
};

// Reject curves that do not match the key type, while accepting both the
// RFC 8410 and legacy curve25519 OIDs for Montgomery keys.
class Pk11EcKeyTypeCurveTest
    : public ::testing::TestWithParam<Pk11EcKeyTypeCurveParams> {};

TEST_P(Pk11EcKeyTypeCurveTest, CreatePrivateKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens take no raw private key templates";
  }
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  ScopedSECKEYPrivateKey key =
      CreateRawKey(slot.get(), GetParam().keyType, GetParam().curve);
  PRErrorCode err = PORT_GetError();
  EXPECT_EQ(GetParam().accepted, !!key);
  if (!GetParam().accepted) {
    EXPECT_EQ(SEC_ERROR_INVALID_KEY, err) << PORT_ErrorToName(err);
  }
}

TEST_P(Pk11EcKeyTypeCurveTest, ImportPublicKey) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  std::vector<uint8_t> params = EcParamsForCurve(GetParam().curve);
  ScopedSECKEYPublicKey pub = PublicKeyOnCurve(GetParam().curve);
  ASSERT_TRUE(pub);
  pub->keyType = GetParam().keyType == CKK_EC_EDWARDS ? edKey : ecMontKey;
  pub->u.ec.DEREncodedParams = {siBuffer, params.data(),
                                static_cast<unsigned int>(params.size())};

  CK_OBJECT_HANDLE id = PK11_ImportPublicKey(slot.get(), pub.get(), PR_FALSE);
  PRErrorCode err = PORT_GetError();
  EXPECT_EQ(GetParam().accepted, id != CK_INVALID_HANDLE);
  if (!GetParam().accepted) {
    EXPECT_EQ(SEC_ERROR_INVALID_KEY, err) << PORT_ErrorToName(err);
  }
}

TEST_P(Pk11EcKeyTypeCurveTest, GenerateKeyPair) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  CK_MECHANISM_TYPE mech = GetParam().keyType == CKK_EC_EDWARDS
                               ? CKM_EC_EDWARDS_KEY_PAIR_GEN
                               : CKM_EC_MONTGOMERY_KEY_PAIR_GEN;
  std::vector<uint8_t> paramsBuf = EcParamsForCurve(GetParam().curve);
  SECItem params = {siBuffer, paramsBuf.data(),
                    static_cast<unsigned int>(paramsBuf.size())};
  SECKEYPublicKey* rawPub = nullptr;
  // A FIPS token only generates sensitive private keys.
  ScopedSECKEYPrivateKey priv(
      PK11_GenerateKeyPair(slot.get(), mech, &params, &rawPub, PR_FALSE,
                           PK11_IsFIPS() ? PR_TRUE : PR_FALSE, nullptr));
  PRErrorCode err = PORT_GetError();
  ScopedSECKEYPublicKey pub(rawPub);
  EXPECT_EQ(GetParam().accepted, !!priv);
  EXPECT_EQ(GetParam().accepted, !!pub);
  if (!GetParam().accepted) {
    EXPECT_EQ(SEC_ERROR_INVALID_KEY, err) << PORT_ErrorToName(err);
  }
}

static const Pk11EcKeyTypeCurveParams kEcKeyTypeCurves[] = {
    {CKK_EC_EDWARDS, SEC_OID_ED25519_PUBLIC_KEY, true},
    {CKK_EC_EDWARDS, SEC_OID_X25519, false},
    {CKK_EC_EDWARDS, SEC_OID_CURVE25519, false},
    {CKK_EC_EDWARDS, SEC_OID_SECG_EC_SECP256R1, false},
    {CKK_EC_MONTGOMERY, SEC_OID_X25519, true},
    {CKK_EC_MONTGOMERY, SEC_OID_CURVE25519, true},
    {CKK_EC_MONTGOMERY, SEC_OID_ED25519_PUBLIC_KEY, false},
    {CKK_EC_MONTGOMERY, SEC_OID_SECG_EC_SECP256R1, false},
};

INSTANTIATE_TEST_SUITE_P(RejectMismatchedECKeyTypeAndCurve,
                         Pk11EcKeyTypeCurveTest,
                         ::testing::ValuesIn(kEcKeyTypeCurves));

TEST_F(Pk11KeyImportTestEC, RejectInvalidECParamsLength) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  for (const auto& param : kEcKeyTypeCurves) {
    if (!param.accepted) {
      continue;
    }
    SCOPED_TRACE(param.curve);
    std::vector<uint8_t> params = EcParamsForCurve(param.curve);
    ScopedSECKEYPublicKey pub = PublicKeyOnCurve(param.curve);
    ASSERT_TRUE(pub);
    --params[1];
    pub->u.ec.DEREncodedParams = {siBuffer, params.data(),
                                  static_cast<unsigned int>(params.size())};
    CK_OBJECT_HANDLE id = PK11_ImportPublicKey(slot.get(), pub.get(), PR_FALSE);
    PRErrorCode err = PORT_GetError();
    EXPECT_EQ(static_cast<CK_OBJECT_HANDLE>(CK_INVALID_HANDLE), id);
    EXPECT_EQ(SEC_ERROR_INVALID_KEY, err) << PORT_ErrorToName(err);
  }
}

// A Montgomery key named by the older curve25519 OID gets the same public key
// as one named by the RFC 8410 OID.
TEST_F(Pk11KeyImportTestEC, MontgomeryCurve25519PublicKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens take no raw private key templates";
  }
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  ASSERT_TRUE(slot);

  ScopedSECKEYPrivateKey x25519 =
      CreateRawKey(slot.get(), CKK_EC_MONTGOMERY, SEC_OID_X25519);
  ASSERT_TRUE(x25519);
  ScopedSECKEYPrivateKey curve25519 =
      CreateRawKey(slot.get(), CKK_EC_MONTGOMERY, SEC_OID_CURVE25519);
  ASSERT_TRUE(curve25519);

  ScopedSECKEYPublicKey x25519Pub(SECKEY_ConvertToPublicKey(x25519.get()));
  ASSERT_TRUE(x25519Pub);
  ScopedSECKEYPublicKey curve25519Pub(
      SECKEY_ConvertToPublicKey(curve25519.get()));
  ASSERT_TRUE(curve25519Pub) << PORT_ErrorToName(PORT_GetError());
  EXPECT_EQ(ecMontKey, curve25519Pub->keyType);
  EXPECT_TRUE(SECITEM_ItemsAreEqual(&x25519Pub->u.ec.publicValue,
                                    &curve25519Pub->u.ec.publicValue));
}

}  // namespace nss_test
