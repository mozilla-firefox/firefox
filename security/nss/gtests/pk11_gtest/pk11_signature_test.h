/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <vector>
#include "prerror.h"
#include "nss.h"
#include "pk11pub.h"
#include "pk11priv.h"
#include "secerr.h"
#include "sechash.h"

#include "nss_scoped_ptrs.h"
#include "databuffer.h"

#include "gtest/gtest.h"
#include "pk11_import.h"
namespace nss_test {

// For test vectors.
struct Pkcs11SignatureTestParams {
  const DataBuffer pkcs8_;
  const DataBuffer spki_;
  const DataBuffer data_;
  const DataBuffer signature_;
};

// A zero-length signature is never valid. It has to be reported as a bad
// signature, not as an error in the arguments, which callers such as
// mozilla::pkix take to mean an internal error.
//
// PK11_VerifyWithMechanism passes the signature to C_VerifySignatureInit,
// which copies it. A copy of an empty signature has no buffer, so this also
// covers C_Verify and C_VerifyFinal being called with a NULL, zero-length
// signature.
inline void VerifyEmptySignatureFails(SECKEYPublicKey* pub,
                                      CK_MECHANISM_TYPE mech,
                                      const SECItem* params = nullptr) {
  std::vector<uint8_t> data(32, 0x2a);
  SECItem dataItem = {siBuffer, data.data(),
                      static_cast<unsigned int>(data.size())};

  // Both an empty buffer and no buffer at all.
  uint8_t buf[1] = {0};
  for (uint8_t* sigData : {buf, static_cast<uint8_t*>(nullptr)}) {
    SCOPED_TRACE(sigData ? "non-NULL signature data" : "NULL signature data");
    SECItem sigItem = {siBuffer, sigData, 0};
    PORT_SetError(0);
    EXPECT_EQ(SECFailure, PK11_VerifyWithMechanism(pub, mech, params, &sigItem,
                                                   &dataItem, nullptr));
    EXPECT_EQ(SEC_ERROR_BAD_SIGNATURE, PORT_GetError())
        << PORT_ErrorToName(PORT_GetError());
  }
}

class Pk11SignatureTest : public ::testing::Test {
 protected:
  Pk11SignatureTest(CK_MECHANISM_TYPE mech, SECOidTag hash_oid,
                    CK_MECHANISM_TYPE combo)
      : mechanism_(mech), hash_oid_(hash_oid), combo_(combo) {
    skip_raw_ = false;
    skip_digest_ = false;
  }

  Pk11SignatureTest(CK_MECHANISM_TYPE mech) : mechanism_(mech) {
    skip_digest_ = true;
  }

  virtual const SECItem* parameters() const { return nullptr; }
  CK_MECHANISM_TYPE mechanism() const { return mechanism_; }
  void setSkipRaw(bool skip_raw) { skip_raw_ = true; }

  bool ExportPrivateKey(ScopedSECKEYPrivateKey* key, DataBuffer& pkcs8) {
    SECItem* pkcs8Item = nss_test::ExportDERPrivateKeyInfo(key->get(), nullptr);
    if (!pkcs8Item) {
      return false;
    }
    pkcs8.Assign(pkcs8Item->data, pkcs8Item->len);
    SECITEM_ZfreeItem(pkcs8Item, PR_TRUE);
    return true;
  }

  ScopedSECKEYPrivateKey ImportPrivateKey(const DataBuffer& pkcs8);
  ScopedSECKEYPrivateKey ImportEncryptedPrivateKey(const DataBuffer& pkcs8);
  ScopedSECKEYPublicKey ImportPublicKey(const DataBuffer& spki);

  bool ComputeHash(const DataBuffer& data, DataBuffer* hash) {
    hash->Allocate(static_cast<size_t>(HASH_ResultLenByOidTag(hash_oid_)));
    SECStatus rv =
        PK11_HashBuf(hash_oid_, hash->data(), data.data(), data.len());
    return rv == SECSuccess;
  }

  bool SignRaw(ScopedSECKEYPrivateKey& privKey, const DataBuffer& hash,
               DataBuffer* sig);
  bool DigestAndSign(ScopedSECKEYPrivateKey& privKey, const DataBuffer& data,
                     DataBuffer* sig);
  bool ImportPrivateKeyAndSignHashedData(const DataBuffer& pkcs8,
                                         const DataBuffer& data,
                                         DataBuffer* sig, DataBuffer* sig2);

  /* most primitive verify implemented in pk11_signature_test.cpp */
  void Verify(ScopedSECKEYPublicKey& pubKey, const DataBuffer& data,
              const DataBuffer& sig, bool valid);

  /* quick helper functions that use the primitive verify */
  void Verify(ScopedSECKEYPublicKey& pubKey, const DataBuffer& data,
              const DataBuffer& sig) {
    Verify(pubKey, data, sig, true);
  }

  void Verify(const Pkcs11SignatureTestParams& params, const DataBuffer& sig,
              bool valid) {
    ScopedSECKEYPublicKey pubKey(ImportPublicKey(params.spki_));
    ASSERT_TRUE(pubKey);
    Verify(pubKey, params.data_, sig, valid);
  }

  void Verify(const Pkcs11SignatureTestParams& params, bool valid) {
    Verify(params, params.signature_, valid);
  }

  void Verify(const Pkcs11SignatureTestParams& params) {
    Verify(params, params.signature_, true);
  }

  void SignAndVerify(const Pkcs11SignatureTestParams& params) {
    DataBuffer sig;
    DataBuffer sig2;
    ASSERT_TRUE(ImportPrivateKeyAndSignHashedData(params.pkcs8_, params.data_,
                                                  &sig, &sig2));
    Verify(params, sig, true);
    Verify(params, sig2, true);
  }

  void SignAndVerifyRaw(const Pkcs11SignatureTestParams& params) {
    ScopedSECKEYPrivateKey privKey(ImportPrivateKey(params.pkcs8_));
    ASSERT_NE(privKey, nullptr);
    DataBuffer sig;
    SignRaw(privKey, params.data_, &sig);
    EXPECT_EQ(sig, params.signature_);
    Verify(params, sig, true);
    ScopedSECKEYPrivateKey privKey2(ImportEncryptedPrivateKey(params.pkcs8_));
    SignRaw(privKey2, params.data_, &sig);
    EXPECT_EQ(sig, params.signature_);
    Verify(params, sig, true);
  }

  // Importing a private key in PKCS#8 format and reexporting it should
  // result in the same binary representation.
  void ImportExport(const DataBuffer& k) {
    DataBuffer exported;
    ScopedSECKEYPrivateKey key = ImportPrivateKey(k);
    ExportPrivateKey(&key, exported);

    // we currently can't export v2 key format so,
    // if the key came in as a v2 key skip
    // the comparison. We can still export
    // the key, but it will now be a v1 key
    SECItem derpki = {siBuffer, (unsigned char*)k.data(),
                      (unsigned int)k.len()};
    ScopedSECKEYPrivateKeyInfo privKeyInfo(
        _PK11_DERPrivateKeyToPrivateKeyInfo(&derpki));
    EXPECT_NE(privKeyInfo, nullptr) << PORT_ErrorToName(PORT_GetError()) << ": "
                                    << PORT_ErrorToString(PORT_GetError());
    if (privKeyInfo && (privKeyInfo.get()->version.len == 1) &&
        (privKeyInfo.get()->version.data[0] == 1)) {
      return;
    }
    EXPECT_EQ(k, exported);
  }

 private:
  CK_MECHANISM_TYPE mechanism_;
  SECOidTag hash_oid_;
  CK_MECHANISM_TYPE combo_;
  bool skip_raw_;
  bool skip_digest_;
};

}  // namespace nss_test
