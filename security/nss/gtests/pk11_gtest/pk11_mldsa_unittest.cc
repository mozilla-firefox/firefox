/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "json_reader.h"
#include "keyhi.h"
#include "ml_dsat.h"
#include "nss_scoped_ptrs.h"
#include "pk11priv.h"
#include "pk11pub.h"
#include "pq_pkcs8.h"
#include "prerror.h"
#include "secasn1.h"
#include "secerr.h"
#include "secitem.h"
#include "secoid.h"
#include "pk11_import.h"

namespace nss_test {

// Wycheproof ML-DSA vectors driven through PKCS#11 rather than freebl. The
// same vectors are run against freebl directly in
// gtests/freebl_gtest/mldsa_unittest.cc; here they go in as the DER encoded
// keys that the files carry, so they also cover the SubjectPublicKeyInfo and
// PKCS#8 decoders, pk11wrap and softoken.

struct MlDsaTestVector {
  uint64_t id;
  bool valid;
  std::vector<uint8_t> msg;
  std::vector<uint8_t> ctx;
  std::vector<uint8_t> sig;
  bool has_msg = false;
  bool has_rnd = false;
};

static SECItem as_item(const std::vector<uint8_t>& v) {
  SECItem item = {siBuffer, const_cast<uint8_t*>(v.data()),
                  static_cast<unsigned int>(v.size())};
  return item;
}

class Pkcs11MlDsaWycheproofTest : public ::testing::Test {
 protected:
  typedef std::function<void(const MlDsaTestVector&)> Operation;

  void Run(const std::string& file, SECOidTag oid, const std::string& schema,
           Operation op) {
    oid_ = oid;
    op_ = op;
    WycheproofHeader(file, ParameterSetName(oid), schema,
                     [this](JsonReader& r) { RunGroup(r); });
  }

  void Verify(const MlDsaTestVector& t) {
    ScopedSECKEYPublicKey pub(ImportPublicKey(publicKeyDer_));
    if (!pub) {
      EXPECT_FALSE(t.valid) << "could not import the public key of a valid "
                               "vector: "
                            << PORT_ErrorToString(PORT_GetError());
      return;
    }

    SECItem msg = as_item(t.msg);
    SECItem sig = as_item(t.sig);
    SECItem param = SigningParams(t.ctx);
    SECStatus rv = PK11_VerifyWithMechanism(pub.get(), CKM_ML_DSA, &param, &sig,
                                            &msg, nullptr);
    EXPECT_EQ(t.valid ? SECSuccess : SECFailure, rv);
  }

  void Sign(const MlDsaTestVector& t) {
    // Neither external-mu signing (a test case with a mu but no message) nor
    // hedged signing with caller-supplied randomness can be expressed here.
    if (!t.has_msg || t.has_rnd) {
      return;
    }

    ScopedSECKEYPrivateKey priv(ImportPrivateKey(Pkcs8()));
    if (!priv) {
      EXPECT_FALSE(t.valid) << "could not import the private key of a valid "
                               "vector: "
                            << PORT_ErrorToString(PORT_GetError());
      return;
    }

    std::vector<uint8_t> sigbuf(MAX_ML_DSA_SIGNATURE_LEN);
    SECItem sig = {siBuffer, sigbuf.data(), (unsigned int)sigbuf.size()};
    SECItem msg = as_item(t.msg);
    SECItem param = SigningParams(t.ctx);

    SECStatus rv =
        PK11_SignWithMechanism(priv.get(), CKM_ML_DSA, &param, &sig, &msg);
    ASSERT_EQ(t.valid ? SECSuccess : SECFailure, rv);
    if (!t.valid) {
      return;
    }
    EXPECT_EQ(t.sig, std::vector<uint8_t>(sig.data, sig.data + sig.len));
  }

 private:
  static std::string ParameterSetName(SECOidTag oid) {
    switch (oid) {
      case SEC_OID_ML_DSA_44:
        return "ML-DSA-44";
      case SEC_OID_ML_DSA_65:
        return "ML-DSA-65";
      case SEC_OID_ML_DSA_87:
        return "ML-DSA-87";
      default:
        ADD_FAILURE() << "unsupported parameter set";
        return "";
    }
  }

  // Deterministic signing, so that a signature can be compared against the
  // expected one. Verification ignores the hedge variant but takes the same
  // parameter structure.
  SECItem SigningParams(const std::vector<uint8_t>& ctx) {
    signCtx_.hedgeVariant = CKH_DETERMINISTIC_REQUIRED;
    signCtx_.pContext = const_cast<uint8_t*>(ctx.data());
    signCtx_.ulContextLen = ctx.size();
    SECItem param = {siBuffer, (unsigned char*)&signCtx_, sizeof(signCtx_)};
    return param;
  }

  // The signing key to import. Most groups carry a PKCS#8; the rest give only
  // a raw seed or a raw expanded key, which have to be wrapped in one.
  std::vector<uint8_t> Pkcs8() {
    if (!privateKeyPkcs8_.empty()) {
      return privateKeyPkcs8_;
    }
    if (!privateSeed_.empty()) {
      return BuildPqPkcs8(oid_, PqSeedChoice(privateSeed_));
    }
    return BuildPqPkcs8(oid_, PqExpandedKeyChoice(privateKey_));
  }

  ScopedSECKEYPublicKey ImportPublicKey(const std::vector<uint8_t>& spki) {
    SECItem item = as_item(spki);
    ScopedCERTSubjectPublicKeyInfo info(
        SECKEY_DecodeDERSubjectPublicKeyInfo(&item));
    if (!info) {
      return nullptr;
    }
    return ScopedSECKEYPublicKey(SECKEY_ExtractPublicKey(info.get()));
  }

  ScopedSECKEYPrivateKey ImportPrivateKey(const std::vector<uint8_t>& pkcs8) {
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    EXPECT_TRUE(slot);
    if (!slot || pkcs8.empty()) {
      return nullptr;
    }

    SECItem item = as_item(pkcs8);
    // Without the seed, a sensitive key can't regenerate its public key, so
    // in FIPS mode hand that over.
    SECItem pub = as_item(publicKey_);
    const SECItem* pubValue =
        (PK11_IsFIPS() && !publicKey_.empty()) ? &pub : nullptr;
    SECKEYPrivateKey* key = nullptr;
    if (nss_test::ImportDERPrivateKeyInfoAndReturnKey(
            slot.get(), &item, nullptr, pubValue, false, false, KU_ALL, &key,
            nullptr) != SECSuccess) {
      return nullptr;
    }
    return ScopedSECKEYPrivateKey(key);
  }

  static void ReadTestAttr(MlDsaTestVector& t, const std::string& n,
                           JsonReader& r) {
    if (n == "msg") {
      t.msg = r.ReadHex();
      t.has_msg = true;
    } else if (n == "ctx") {
      t.ctx = r.ReadHex();
    } else if (n == "sig") {
      t.sig = r.ReadHex();
    } else if (n == "rnd") {
      r.SkipValue();
      t.has_rnd = true;
    } else if (n == "mu") {
      r.SkipValue();
    } else {
      FAIL() << "unsupported test case field: " << n;
    }
  }

  void RunGroup(JsonReader& r) {
    std::vector<MlDsaTestVector> tests;
    publicKeyDer_.clear();
    privateKeyPkcs8_.clear();
    privateKey_.clear();
    privateSeed_.clear();
    publicKey_.clear();

    while (r.NextItem()) {
      std::string n = r.ReadLabel();
      if (n == "") {
        break;
      }
      if (n == "publicKeyDer") {
        publicKeyDer_ = r.ReadHex();
      } else if (n == "privateKeyPkcs8") {
        // Null for groups whose signing key cannot be encoded.
        privateKeyPkcs8_ = ReadOptionalHex(r);
      } else if (n == "privateKey") {
        privateKey_ = r.ReadHex();
      } else if (n == "privateSeed") {
        privateSeed_ = r.ReadHex();
      } else if (n == "publicKey") {
        publicKey_ = ReadOptionalHex(r);
      } else if (n == "type" || n == "source") {
        r.SkipValue();
      } else if (n == "tests") {
        WycheproofReadTests(r, &tests, ReadTestAttr, false);
      } else {
        FAIL() << "unknown group label: " << n;
      }
    }

    for (auto& t : tests) {
      SCOPED_TRACE(testing::Message() << "tcId " << t.id);
      op_(t);
    }
  }

  static std::vector<uint8_t> ReadOptionalHex(JsonReader& r) {
    if (r.PeekValue() == 'n') {  // null
      r.SkipValue();
      return std::vector<uint8_t>();
    }
    return r.ReadHex();
  }

  SECOidTag oid_;
  Operation op_;
  CK_SIGN_ADDITIONAL_CONTEXT signCtx_;
  std::vector<uint8_t> publicKeyDer_;
  std::vector<uint8_t> privateKeyPkcs8_;
  std::vector<uint8_t> privateKey_;
  std::vector<uint8_t> privateSeed_;
  std::vector<uint8_t> publicKey_;
};

#define ML_DSA_WYCHEPROOF_TESTS(name, bits, oid)                             \
  TEST_F(Pkcs11MlDsaWycheproofTest, name##Verify) {                          \
    Run("mldsa_" #bits "_verify", oid, "mldsa_verify_schema.json",           \
        [this](const MlDsaTestVector& t) { Verify(t); });                    \
  }                                                                          \
  TEST_F(Pkcs11MlDsaWycheproofTest, name##SignSeed) {                        \
    Run("mldsa_" #bits "_sign_seed", oid, "mldsa_sign_seed_schema.json",     \
        [this](const MlDsaTestVector& t) { Sign(t); });                      \
  }                                                                          \
  TEST_F(Pkcs11MlDsaWycheproofTest, name##SignNoSeed) {                      \
    Run("mldsa_" #bits "_sign_noseed", oid, "mldsa_sign_noseed_schema.json", \
        [this](const MlDsaTestVector& t) { Sign(t); });                      \
  }

ML_DSA_WYCHEPROOF_TESTS(MlDsa44, 44, SEC_OID_ML_DSA_44)
ML_DSA_WYCHEPROOF_TESTS(MlDsa65, 65, SEC_OID_ML_DSA_65)
ML_DSA_WYCHEPROOF_TESTS(MlDsa87, 87, SEC_OID_ML_DSA_87)

// Lifetime tests for the MLDSAContext that softoken manages during a signing
// operation. These should be run under ASAN.
class Pkcs11MlDsaLifetimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    slot_.reset(PK11_GetInternalSlot());
    ASSERT_TRUE(slot_);

    CK_ML_DSA_PARAMETER_SET_TYPE paramSet = CKP_ML_DSA_44;
    SECKEYPublicKey* pub = nullptr;
    priv_.reset(PK11_GenerateKeyPair(slot_.get(), CKM_ML_DSA_KEY_PAIR_GEN,
                                     &paramSet, &pub, PR_FALSE, PK11_IsFIPS(),
                                     nullptr));
    pub_.reset(pub);
    ASSERT_TRUE(priv_);
    ASSERT_TRUE(pub_);

    signCtx_.hedgeVariant = CKH_DETERMINISTIC_REQUIRED;
    signCtx_.pContext = nullptr;
    signCtx_.ulContextLen = 0;
    param_.type = siBuffer;
    param_.data = (unsigned char*)&signCtx_;
    param_.len = sizeof(signCtx_);
  }

  // C_SignInit happens here, which is where softoken builds the MLDSAContext.
  PK11Context* StartSigning() {
    return PK11_CreateContextByPrivKey(CKM_ML_DSA, CKA_SIGN, priv_.get(),
                                       &param_);
  }

  static const unsigned char kMsg[6];
  ScopedPK11SlotInfo slot_;
  ScopedSECKEYPrivateKey priv_;
  ScopedSECKEYPublicKey pub_;
  CK_SIGN_ADDITIONAL_CONTEXT signCtx_;
  SECItem param_;
};

const unsigned char Pkcs11MlDsaLifetimeTest::kMsg[6] = {'m', 'l', '-',
                                                        'd', 's', 'a'};

TEST_F(Pkcs11MlDsaLifetimeTest, DestroyKeyDuringSignOperation) {
  ScopedPK11Context ctx(StartSigning());
  ASSERT_TRUE(ctx);

  ASSERT_EQ(SECSuccess, PK11_DestroyObject(slot_.get(), priv_->pkcs11ID));

  ASSERT_EQ(SECSuccess, PK11_DigestOp(ctx.get(), kMsg, sizeof(kMsg)));

  std::vector<unsigned char> sigbuf(ML_DSA_44_SIGNATURE_LEN);
  unsigned int sigLen = 0;
  ASSERT_EQ(SECSuccess,
            PK11_DigestFinal(ctx.get(), sigbuf.data(), &sigLen,
                             static_cast<unsigned int>(sigbuf.size())));
  EXPECT_EQ(static_cast<unsigned int>(ML_DSA_44_SIGNATURE_LEN), sigLen);

  SECItem sig = {siBuffer, sigbuf.data(), sigLen};
  SECItem msg = {siBuffer, const_cast<unsigned char*>(kMsg), sizeof(kMsg)};
  EXPECT_EQ(SECSuccess, PK11_VerifyWithMechanism(pub_.get(), CKM_ML_DSA,
                                                 &param_, &sig, &msg, nullptr));
}

TEST_F(Pkcs11MlDsaLifetimeTest, AbandonedSignOperationDoesNotLeak) {
  ScopedPK11Context ctx(StartSigning());
  ASSERT_TRUE(ctx);

  ASSERT_EQ(SECSuccess, PK11_DigestOp(ctx.get(), kMsg, sizeof(kMsg)));
}

// The mechanism and key type tables. These are one-line mappings, but every
// caller that has to get from one of these to another goes through them.
// PK11_GetKeyMechanism and PK11_GetKeyGenWithSize have ML-DSA arms too, but
// neither is exported from nss3, so they cannot be reached from here.
TEST(Pkcs11MlDsaMechanismTest, KeyTypeAndMechanismMappings) {
  EXPECT_EQ(static_cast<CK_KEY_TYPE>(CKK_ML_DSA),
            PK11_GetKeyType(CKM_ML_DSA, 0));
  EXPECT_EQ(static_cast<CK_KEY_TYPE>(CKK_ML_DSA),
            PK11_GetKeyType(CKM_ML_DSA_KEY_PAIR_GEN, 0));
  EXPECT_EQ(static_cast<CK_MECHANISM_TYPE>(CKM_ML_DSA),
            PK11_MapSignKeyType(mldsaKey));
}

// Getting an ML-DSA key into and out of storage: softoken's PKCS#8 packaging
// and unwrapping, and the permanent (token) object paths. The Wycheproof tests
// above import PKCS#8 blobs that the vector files carry, always as session
// objects, so none of this is reached from there.
class Pkcs11MlDsaStorageTest
    : public ::testing::TestWithParam<CK_ML_DSA_PARAMETER_SET_TYPE> {
 protected:
  void SetUp() override {
    slot_.reset(PK11_GetInternalKeySlot());
    ASSERT_TRUE(slot_);
    ASSERT_EQ(SECSuccess, PK11_Authenticate(slot_.get(), PR_TRUE, nullptr))
        << PORT_ErrorToString(PORT_GetError());

    CK_ML_DSA_PARAMETER_SET_TYPE paramSet = GetParam();
    SECKEYPublicKey* pub = nullptr;
    priv_.reset(PK11_GenerateKeyPair(slot_.get(), CKM_ML_DSA_KEY_PAIR_GEN,
                                     &paramSet, &pub, PR_FALSE, PK11_IsFIPS(),
                                     nullptr));
    pub_.reset(pub);
    ASSERT_TRUE(priv_);
    ASSERT_TRUE(pub_);

    static const unsigned char pw[] = "pw";
    SECItem pwItem = {siBuffer, const_cast<unsigned char*>(pw), sizeof(pw)};
    password_.reset(SECITEM_DupItem(&pwItem));
    ASSERT_TRUE(password_);

    signCtx_.hedgeVariant = CKH_HEDGE_PREFERRED;
    signCtx_.pContext = nullptr;
    signCtx_.ulContextLen = 0;
    param_.type = siBuffer;
    param_.data = (unsigned char*)&signCtx_;
    param_.len = sizeof(signCtx_);
  }

  // A key that came back out of storage has to still be the same key, so sign
  // with it and verify against the public half of the pair it came from.
  void ExpectPairs(SECKEYPrivateKey* priv, SECKEYPublicKey* pub) {
    std::vector<unsigned char> sigbuf(MAX_ML_DSA_SIGNATURE_LEN);
    SECItem sig = {siBuffer, sigbuf.data(),
                   static_cast<unsigned int>(sigbuf.size())};
    SECItem msg = {siBuffer, const_cast<unsigned char*>(kMsg), sizeof(kMsg)};
    ASSERT_EQ(SECSuccess,
              PK11_SignWithMechanism(priv, CKM_ML_DSA, &param_, &sig, &msg))
        << PORT_ErrorToString(PORT_GetError());
    EXPECT_EQ(SECSuccess, PK11_VerifyWithMechanism(pub, CKM_ML_DSA, &param_,
                                                   &sig, &msg, nullptr))
        << PORT_ErrorToString(PORT_GetError());
  }

  static SECOidTag OidTag(CK_ML_DSA_PARAMETER_SET_TYPE paramSet) {
    switch (paramSet) {
      case CKP_ML_DSA_44:
        return SEC_OID_ML_DSA_44;
      case CKP_ML_DSA_65:
        return SEC_OID_ML_DSA_65;
      case CKP_ML_DSA_87:
        return SEC_OID_ML_DSA_87;
      default:
        ADD_FAILURE() << "unsupported parameter set";
        return SEC_OID_UNKNOWN;
    }
  }

  std::vector<uint8_t> BuildBothPkcs8(const std::vector<uint8_t>& seed,
                                      const std::vector<uint8_t>& key) {
    return BuildPqPkcs8(OidTag(GetParam()), PqBothChoice(seed, key));
  }

  SECKEYPrivateKey* ImportPkcs8(const std::vector<uint8_t>& pkcs8) {
    SECItem item = {siBuffer, const_cast<uint8_t*>(pkcs8.data()),
                    static_cast<unsigned int>(pkcs8.size())};
    SECKEYPrivateKey* key = nullptr;
    if (nss_test::ImportDERPrivateKeyInfoAndReturnKey(
            slot_.get(), &item, nullptr, nullptr, PR_FALSE, PR_FALSE, KU_ALL,
            &key, nullptr) != SECSuccess) {
      return nullptr;
    }
    return key;
  }

  static const unsigned char kMsg[6];
  ScopedPK11SlotInfo slot_;
  ScopedSECKEYPrivateKey priv_;
  ScopedSECKEYPublicKey pub_;
  ScopedSECItem password_;
  CK_SIGN_ADDITIONAL_CONTEXT signCtx_;
  SECItem param_;
};

const unsigned char Pkcs11MlDsaStorageTest::kMsg[6] = {'m', 'l', '-',
                                                       'd', 's', 'a'};

// C_WrapKey on a private key packages it as a PKCS#8 first, and C_UnwrapKey
// takes one apart again.
TEST_P(Pkcs11MlDsaStorageTest, WrapAndUnwrap) {
  ScopedPK11SymKey kek(
      PK11_KeyGen(slot_.get(), CKM_AES_CBC, nullptr, 16, nullptr));
  ASSERT_TRUE(kek);
  ScopedSECItem wrapParam(PK11_ParamFromIV(CKM_NSS_AES_KEY_WRAP_PAD, nullptr));
  ASSERT_TRUE(wrapParam);

  // Room for the largest signing key plus its seed, PKCS#8 framing and padding.
  ScopedSECItem wrapped(SECITEM_AllocItem(nullptr, nullptr, 8192));
  ASSERT_TRUE(wrapped);
  ASSERT_EQ(SECSuccess,
            PK11_WrapPrivKey(slot_.get(), kek.get(), priv_.get(),
                             CKM_NSS_AES_KEY_WRAP_PAD, wrapParam.get(),
                             wrapped.get(), nullptr))
      << PORT_ErrorToString(PORT_GetError());

  ScopedSECKEYPrivateKey unwrapped(PK11_UnwrapPrivKeyByKeyType(
      slot_.get(), kek.get(), CKM_NSS_AES_KEY_WRAP_PAD, wrapParam.get(),
      wrapped.get(), nullptr, &pub_->u.mldsa.publicValue, PR_FALSE,
      PK11_IsFIPS(), mldsaKey, KU_ALL, nullptr));
  ASSERT_TRUE(unwrapped) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(mldsaKey, unwrapped->keyType);
  ExpectPairs(unwrapped.get(), pub_.get());
}

// C_WrapKey packages a key that has a seed as the seed CHOICE alone.
TEST_P(Pkcs11MlDsaStorageTest, WrapWritesTheSeedChoice) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens don't reveal CKA_SEED";
  }
  ScopedSECItem seed(SECITEM_AllocItem(nullptr, nullptr, 0));
  ASSERT_TRUE(seed);
  ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv_.get(),
                                              CKA_SEED, seed.get()))
      << PORT_ErrorToString(PORT_GetError());
  std::vector<uint8_t> seedBytes(seed->data, seed->data + seed->len);

  ScopedPK11SymKey kek(
      PK11_KeyGen(slot_.get(), CKM_AES_CBC, nullptr, 16, nullptr));
  ASSERT_TRUE(kek);
  ScopedSECItem wrapParam(PK11_ParamFromIV(CKM_NSS_AES_KEY_WRAP_PAD, nullptr));
  ASSERT_TRUE(wrapParam);
  ScopedSECItem wrapped(SECITEM_AllocItem(nullptr, nullptr, 8192));
  ASSERT_TRUE(wrapped);
  ASSERT_EQ(SECSuccess,
            PK11_WrapPrivKey(slot_.get(), kek.get(), priv_.get(),
                             CKM_NSS_AES_KEY_WRAP_PAD, wrapParam.get(),
                             wrapped.get(), nullptr))
      << PORT_ErrorToString(PORT_GetError());

  std::vector<uint8_t> pkcs8(wrapped->len);
  unsigned int pkcs8Len = 0;
  ASSERT_EQ(SECSuccess, PK11_Decrypt(kek.get(), CKM_NSS_AES_KEY_WRAP_PAD,
                                     wrapParam.get(), pkcs8.data(), &pkcs8Len,
                                     pkcs8.size(), wrapped->data, wrapped->len))
      << PORT_ErrorToString(PORT_GetError());
  pkcs8.resize(pkcs8Len);
  EXPECT_EQ(BuildPqPkcs8(OidTag(GetParam()), PqSeedChoice(seedBytes)), pkcs8);
}

// Importing as a permanent object also creates the matching public key object,
// which is what makes the key findable later.
TEST_P(Pkcs11MlDsaStorageTest, ExportEncryptedAndImportAsTokenKey) {
  ScopedSECKEYEncryptedPrivateKeyInfo epki(PK11_ExportEncryptedPrivKeyInfo(
      slot_.get(), SEC_OID_AES_256_CBC, password_.get(), priv_.get(), 1,
      nullptr));
  ASSERT_TRUE(epki) << PORT_ErrorToString(PORT_GetError());

  static const unsigned char nick[] = "ml-dsa token key";
  SECItem nickname = {siBuffer, const_cast<unsigned char*>(nick), sizeof(nick)};
  SECKEYPrivateKey* imported = nullptr;
  ASSERT_EQ(SECSuccess,
            PK11_ImportEncryptedPrivateKeyInfoAndReturnKey(
                slot_.get(), epki.get(), password_.get(), &nickname,
                &pub_->u.mldsa.publicValue, PR_TRUE /* isPerm */,
                PR_TRUE /* isPrivate */, mldsaKey, KU_ALL, &imported, nullptr))
      << PORT_ErrorToString(PORT_GetError());
  ScopedSECKEYPrivateKey tokenKey(imported);
  ASSERT_TRUE(tokenKey);
  EXPECT_EQ(mldsaKey, tokenKey->keyType);
  ExpectPairs(tokenKey.get(), pub_.get());

  EXPECT_EQ(SECSuccess,
            PK11_DeleteTokenPrivateKey(tokenKey.release(), PR_TRUE));
}

// Moving a key onto a slot reads it back out attribute by attribute, so the
// attribute list for an ML-DSA private key has to be right.
TEST_P(Pkcs11MlDsaStorageTest, LoadPrivKeyOntoASlot) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens don't reveal private key attributes";
  }
  ScopedSECKEYPrivateKey loaded(PK11_LoadPrivKey(
      slot_.get(), priv_.get(), pub_.get(), PR_FALSE, PR_FALSE));
  ASSERT_TRUE(loaded) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(mldsaKey, loaded->keyType);
  ExpectPairs(loaded.get(), pub_.get());
}

// Copying a token key to a session key is a C_CopyObject on the token side,
// which reassembles the key from its stored attributes rather than copying an
// in-memory object.
TEST_P(Pkcs11MlDsaStorageTest, TokenKeyCopiesToASessionKey) {
  CK_ML_DSA_PARAMETER_SET_TYPE paramSet = GetParam();
  SECKEYPublicKey* pub = nullptr;
  ScopedSECKEYPrivateKey tokenPriv(
      PK11_GenerateKeyPair(slot_.get(), CKM_ML_DSA_KEY_PAIR_GEN, &paramSet,
                           &pub, PR_TRUE /* token */, PK11_IsFIPS(), nullptr));
  ScopedSECKEYPublicKey tokenPub(pub);
  ASSERT_TRUE(tokenPriv) << PORT_ErrorToString(PORT_GetError());
  ASSERT_TRUE(tokenPub);

  ScopedSECKEYPrivateKey sessionPriv(
      PK11_CopyTokenPrivKeyToSessionPrivKey(slot_.get(), tokenPriv.get()));
  ASSERT_TRUE(sessionPriv) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(mldsaKey, sessionPriv->keyType);
  ExpectPairs(sessionPriv.get(), tokenPub.get());

  EXPECT_EQ(SECSuccess,
            PK11_DeleteTokenPrivateKey(tokenPriv.release(), PR_TRUE));
  EXPECT_EQ(SECSuccess, PK11_DeleteTokenPublicKey(tokenPub.release()));
}

// RFC 9881 lets a PKCS#8 carry the seed and the expanded key together, which
// is the form older NSS versions wrote. Nothing forces the two to agree, so
// softoken re-derives the key from the seed and rejects the pair if they
// differ. If it did not, a corrupted or hostile blob would be signed with
// under a seed that does not produce it. The Wycheproof vectors cannot reach
// this: their PKCS#8 blobs are all seed-only, and the no-seed files carry no
// PKCS#8 at all.
TEST_P(Pkcs11MlDsaStorageTest, BothEncodedKeyChecksSeedAgainstExpandedKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens don't reveal CKA_SEED or CKA_VALUE";
  }
  ScopedSECItem seed(SECITEM_AllocItem(nullptr, nullptr, 0));
  ScopedSECItem value(SECITEM_AllocItem(nullptr, nullptr, 0));
  ASSERT_TRUE(seed);
  ASSERT_TRUE(value);
  ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv_.get(),
                                              CKA_SEED, seed.get()))
      << PORT_ErrorToString(PORT_GetError());
  ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv_.get(),
                                              CKA_VALUE, value.get()))
      << PORT_ErrorToString(PORT_GetError());
  ASSERT_EQ(static_cast<unsigned int>(ML_DSA_SEED_LEN), seed->len);
  ASSERT_NE(0U, value->len);

  std::vector<uint8_t> seedBytes(seed->data, seed->data + seed->len);
  std::vector<uint8_t> keyBytes(value->data, value->data + value->len);

  // The matching pair has to import and work, otherwise the rejections below
  // would only be telling us the encoding is wrong.
  ScopedSECKEYPrivateKey good(ImportPkcs8(BuildBothPkcs8(seedBytes, keyBytes)));
  ASSERT_TRUE(good) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(mldsaKey, good->keyType);
  ExpectPairs(good.get(), pub_.get());

  // A seed that expands to something else.
  std::vector<uint8_t> badSeed = seedBytes;
  badSeed[0] ^= 0x01;
  EXPECT_FALSE(ImportPkcs8(BuildBothPkcs8(badSeed, keyBytes)));

  // ...and the same disagreement reached from the other side.
  std::vector<uint8_t> badKey = keyBytes;
  badKey[0] ^= 0x01;
  EXPECT_FALSE(ImportPkcs8(BuildBothPkcs8(seedBytes, badKey)));

  // A truncated expanded key disagrees on length rather than content, which is
  // the other half of the check.
  std::vector<uint8_t> shortKey(keyBytes.begin(), keyBytes.end() - 1);
  EXPECT_FALSE(ImportPkcs8(BuildBothPkcs8(seedBytes, shortKey)));
}

// Export writes the seed CHOICE, like the wrap path.
TEST_P(Pkcs11MlDsaStorageTest, ExportPkcs8WritesTheSeedChoice) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens don't reveal CKA_SEED";
  }
  ScopedSECItem seed(SECITEM_AllocItem(nullptr, nullptr, 0));
  ASSERT_TRUE(seed);
  ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv_.get(),
                                              CKA_SEED, seed.get()))
      << PORT_ErrorToString(PORT_GetError());
  std::vector<uint8_t> seedBytes(seed->data, seed->data + seed->len);

  ScopedSECItem der(PK11_ExportDERPrivateKeyInfo(priv_.get(), nullptr));
  ASSERT_TRUE(der) << PORT_ErrorToString(PORT_GetError());
  std::vector<uint8_t> exported(der->data, der->data + der->len);
  EXPECT_EQ(BuildPqPkcs8(OidTag(GetParam()), PqSeedChoice(seedBytes)),
            exported);

  ScopedSECKEYPrivateKey reimported(ImportPkcs8(exported));
  ASSERT_TRUE(reimported) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(mldsaKey, reimported->keyType);
  ExpectPairs(reimported.get(), pub_.get());
}

TEST_P(Pkcs11MlDsaStorageTest, ExportPkcs8FallsBackToTheExpandedKey) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens don't reveal CKA_VALUE";
  }
  ScopedSECItem value(SECITEM_AllocItem(nullptr, nullptr, 0));
  ASSERT_TRUE(value);
  ASSERT_EQ(SECSuccess, PK11_ReadRawAttribute(PK11_TypePrivKey, priv_.get(),
                                              CKA_VALUE, value.get()))
      << PORT_ErrorToString(PORT_GetError());
  std::vector<uint8_t> keyBytes(value->data, value->data + value->len);
  SECOidTag oid = OidTag(GetParam());

  ScopedSECKEYPrivateKey seedless(
      ImportPkcs8(BuildPqPkcs8(oid, PqExpandedKeyChoice(keyBytes))));
  ASSERT_TRUE(seedless) << PORT_ErrorToString(PORT_GetError());

  ScopedSECItem der(PK11_ExportDERPrivateKeyInfo(seedless.get(), nullptr));
  ASSERT_TRUE(der) << PORT_ErrorToString(PORT_GetError());
  EXPECT_EQ(BuildPqPkcs8(oid, PqExpandedKeyChoice(keyBytes)),
            std::vector<uint8_t>(der->data, der->data + der->len));
}

INSTANTIATE_TEST_SUITE_P(Pkcs11MlDsaStorageTest, Pkcs11MlDsaStorageTest,
                         ::testing::Values(CKP_ML_DSA_44, CKP_ML_DSA_65,
                                           CKP_ML_DSA_87));

}  // namespace nss_test
