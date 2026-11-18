/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include "nss.h"
#include "pk11pub.h"
#include "sechash.h"
#include "prerror.h"

#include "cpputil.h"
#include "nss_scoped_ptrs.h"
#include "databuffer.h"

#include "gtest/gtest.h"
#include "pk11_signature_test.h"
#include "pk11_import.h"

namespace nss_test {

ScopedSECKEYPrivateKey Pk11SignatureTest::ImportPrivateKey(
    const DataBuffer& pkcs8) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    ADD_FAILURE() << "No slot";
    return nullptr;
  }

  SECItem pkcs8Item = {siBuffer, toUcharPtr(pkcs8.data()),
                       static_cast<unsigned int>(pkcs8.len())};

  SECKEYPrivateKey* key = nullptr;
  SECStatus rv = nss_test::ImportDERPrivateKeyInfoAndReturnKey(
      slot.get(), &pkcs8Item, nullptr, nullptr, false, false, KU_ALL, &key,
      nullptr);

  if (rv != SECSuccess) {
    return nullptr;
  }

  return ScopedSECKEYPrivateKey(key);
}

ScopedSECKEYPrivateKey Pk11SignatureTest::ImportEncryptedPrivateKey(
    const DataBuffer& pkcs8, const SECItem* publicValue) {
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    ADD_FAILURE() << "No slot";
    return nullptr;
  }

  // encrypt they pkcs8 buffer, then unwrap it as a key. This tests
  // the softoken pkcs8 parsing
  unsigned char iv_data[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  };
  SECItem iv = {siBuffer, iv_data, sizeof(iv_data)};
  unsigned int len = 0;
  ScopedPK11SymKey wrappingKey(
      PK11_KeyGen(slot.get(), CKM_AES_CBC_PAD, nullptr, 24, nullptr));
  EXPECT_NE(wrappingKey, nullptr);
  SECStatus rv =
      PK11_Encrypt(wrappingKey.get(), CKM_AES_CBC_PAD, &iv, nullptr, &len, len,
                   pkcs8.data(), (unsigned int)pkcs8.len());
  EXPECT_EQ(SECSuccess, rv) << PORT_ErrorToName(PORT_GetError());
  ScopedSECItem encryptedKey(SECITEM_AllocItem(nullptr, nullptr, len));
  rv = PK11_Encrypt(wrappingKey.get(), CKM_AES_CBC_PAD, &iv, encryptedKey->data,
                    &len, len, pkcs8.data(), (unsigned int)pkcs8.len());
  EXPECT_EQ(SECSuccess, rv) << PORT_ErrorToName(PORT_GetError());
  encryptedKey->len = len;
  CK_ATTRIBUTE_TYPE usages[] = {CKA_SIGN};
  int usageCount = 1;
  return ScopedSECKEYPrivateKey(PK11_UnwrapPrivKey(
      slot.get(), wrappingKey.get(), CKM_AES_CBC_PAD, &iv, encryptedKey.get(),
      nullptr, publicValue, PR_FALSE, PR_TRUE, PK11_GetKeyType(mechanism(), 0),
      usages, usageCount, nullptr));
}

ScopedSECKEYPublicKey Pk11SignatureTest::ImportPublicKey(
    const DataBuffer& spki) {
  SECItem spkiItem = {siBuffer, toUcharPtr(spki.data()),
                      static_cast<unsigned int>(spki.len())};

  ScopedCERTSubjectPublicKeyInfo certSpki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spkiItem));
  if (!certSpki) {
    return nullptr;
  }

  return ScopedSECKEYPublicKey(SECKEY_ExtractPublicKey(certSpki.get()));
}

bool Pk11SignatureTest::SignRaw(ScopedSECKEYPrivateKey& privKey,
                                const DataBuffer& hash, DataBuffer* sig) {
  SECItem hashItem = {siBuffer, toUcharPtr(hash.data()),
                      static_cast<unsigned int>(hash.len())};
  unsigned int sigLen = PK11_SignatureLen(privKey.get());
  EXPECT_LT(0, (int)sigLen);
  sig->Allocate(static_cast<size_t>(sigLen));
  SECItem sigItem = {siBuffer, toUcharPtr(sig->data()),
                     static_cast<unsigned int>(sig->len())};
  SECStatus rv = PK11_SignWithMechanism(privKey.get(), mechanism_, parameters(),
                                        &sigItem, &hashItem);
  EXPECT_EQ(sigLen, sigItem.len);
  return rv == SECSuccess;
}

bool Pk11SignatureTest::DigestAndSign(ScopedSECKEYPrivateKey& privKey,
                                      const DataBuffer& data, DataBuffer* sig) {
  unsigned int sigLen = PK11_SignatureLen(privKey.get());
  bool result = true;
  EXPECT_LT(0, (int)sigLen);
  sig->Allocate(static_cast<size_t>(sigLen));

  // test the hash and verify interface */
  PK11Context* context = PK11_CreateContextByPrivKey(
      combo_, CKA_SIGN, privKey.get(), parameters());
  if (context == NULL) {
    ADD_FAILURE() << "Failed to sign data: couldn't create context"
                  << "\n"
                  << "mech=0x" << std::hex << combo_ << "\n"
                  << "Error: " << PORT_ErrorToString(PORT_GetError());
    return false;
  }
  SECStatus rv = PK11_DigestOp(context, data.data(), data.len());
  if (rv != SECSuccess) {
    ADD_FAILURE() << "Failed to sign data: Update failed\n"
                  << "Error: " << PORT_ErrorToString(PORT_GetError());
    PK11_DestroyContext(context, PR_TRUE);
    return false;
  }
  unsigned int len = sigLen;
  rv = PK11_DigestFinal(context, sig->data(), &len, sigLen);
  if (rv != SECSuccess) {
    ADD_FAILURE() << "Failed to sign data: final failed\n"
                  << "Error: " << PORT_ErrorToString(PORT_GetError());
    result = false;
  }
  if (len != sigLen) {
    ADD_FAILURE() << "sign data: unexpected len " << len << "expected"
                  << sigLen;
    result = false;
  }
  PK11_DestroyContext(context, PR_TRUE);
  return result;
}

bool Pk11SignatureTest::ImportPrivateKeyAndSignHashedData(
    const DataBuffer& pkcs8, const DataBuffer& data, DataBuffer* sig,
    DataBuffer* sig2) {
  ScopedSECKEYPrivateKey privKey(ImportPrivateKey(pkcs8));
  if (!privKey) {
    return false;
  }

  DataBuffer hash;
  if (!ComputeHash(data, &hash)) {
    ADD_FAILURE() << "Failed to compute hash";
    return false;
  }
  if (!SignRaw(privKey, hash, sig)) {
    ADD_FAILURE() << "Failed to sign hashed data";
    return false;
  }
  if (!DigestAndSign(privKey, data, sig2)) {
    /* failure was already added by SignData, with an error message */
    return false;
  }
  return true;
}

void Pk11SignatureTest::Verify(ScopedSECKEYPublicKey& pubKey,
                               const DataBuffer& data, const DataBuffer& sig,
                               bool valid) {
  SECStatus rv;

  SECItem sigItem = {siBuffer, toUcharPtr(sig.data()),
                     static_cast<unsigned int>(sig.len())};

  if (skip_digest_) {
    SECItem dataItem = {siBuffer, toUcharPtr(data.data()),
                        static_cast<unsigned int>(data.len())};
    rv = PK11_VerifyWithMechanism(pubKey.get(), mechanism_, parameters(),
                                  &sigItem, &dataItem, nullptr);
    EXPECT_EQ(rv, valid ? SECSuccess : SECFailure);
    return;
  }

  DataBuffer hash;
  /* RSA single shot requires encoding the hash before calling
   * VerifyWithMechanism. We already check that mechanism
   * with the VFY_ interface, so just do the combined hash/Verify
   * in that case */
  if (!skip_raw_) {
    ASSERT_TRUE(ComputeHash(data, &hash));

    // Verify.
    SECItem hashItem = {siBuffer, toUcharPtr(hash.data()),
                        static_cast<unsigned int>(hash.len())};
    rv = PK11_VerifyWithMechanism(pubKey.get(), mechanism_, parameters(),
                                  &sigItem, &hashItem, nullptr);
    EXPECT_EQ(rv, valid ? SECSuccess : SECFailure);
  }

  // test the hash and verify interface */
  PK11Context* context = PK11_CreateContextByPubKey(
      combo_, CKA_VERIFY, pubKey.get(), parameters(), NULL);
  /* we assert here because we'll crash if we try to continue
   * without a context. */
  ASSERT_NE((void*)context, (void*)NULL)
      << "CreateContext failed Error:" << PORT_ErrorToString(PORT_GetError())
      << "\n";
  rv = PK11_DigestOp(context, data.data(), data.len());
  /* expect success unconditionally here */
  EXPECT_EQ(rv, SECSuccess);
  unsigned int len;
  rv = PK11_DigestFinal(context, sigItem.data, &len, sigItem.len);
  EXPECT_EQ(rv, valid ? SECSuccess : SECFailure)
      << "verify failed Error:" << PORT_ErrorToString(PORT_GetError()) << "\n";
  PK11_DestroyContext(context, PR_TRUE);
}
}  // namespace nss_test
