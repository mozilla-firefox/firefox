/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This code is made available to you under your choice of the following sets
 * of licensing terms:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/* Copyright 2013 Mozilla Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mozpkix/test/pkixtestutil.h"
#include "mozpkix/test/pkixtestnss.h"

#include <limits>

#include "cryptohi.h"
#include "keyhi.h"
#include "nss.h"
#include "pk11pqg.h"
#include "pk11pub.h"
#include "mozpkix/nss_scoped_ptrs.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixder.h"
#include "mozpkix/pkixutil.h"
#include "prinit.h"
#include "secerr.h"
#include "secitem.h"

namespace mozilla { namespace pkix { namespace test {

namespace {

TestKeyPair* GenerateKeyPairInner();

void
InitNSSIfNeeded()
{
  if (NSS_NoDB_Init(nullptr) != SECSuccess) {
    abort();
  }
}

static ScopedTestKeyPair reusedKeyPair;

PRStatus
InitReusedKeyPair()
{
  InitNSSIfNeeded();
  reusedKeyPair.reset(GenerateKeyPairInner());
  return reusedKeyPair ? PR_SUCCESS : PR_FAILURE;
}

class NSSTestKeyPair final : public TestKeyPair
{
public:
  NSSTestKeyPair(const TestPublicKeyAlgorithm& aPublicKeyAlg,
                 const ByteString& spk,
                 const ByteString& aEncryptedPrivateKey,
                 const ByteString& aEncryptionAlgorithm,
                 const ByteString& aEncryptionParams)
    : TestKeyPair(aPublicKeyAlg, spk)
    , encryptedPrivateKey(aEncryptedPrivateKey)
    , encryptionAlgorithm(aEncryptionAlgorithm)
    , encryptionParams(aEncryptionParams)
  {
  }

  Result SignData(const ByteString& tbs,
                  const TestSignatureAlgorithm& signatureAlgorithm,
                  /*out*/ ByteString& signature) const override
  {
    SECOidTag oidTag;
    KeyType keyType;
    if (signatureAlgorithm.publicKeyAlg == RSA_PKCS1()) {
      keyType = rsaKey;
      switch (signatureAlgorithm.digestAlg) {
        case TestDigestAlgorithmID::MD2:
          oidTag = SEC_OID_PKCS1_MD2_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::MD5:
          oidTag = SEC_OID_PKCS1_MD5_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::SHA1:
          oidTag = SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::SHA224:
          oidTag = SEC_OID_PKCS1_SHA224_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::SHA256:
          oidTag = SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::SHA384:
          oidTag = SEC_OID_PKCS1_SHA384_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::SHA512:
          oidTag = SEC_OID_PKCS1_SHA512_WITH_RSA_ENCRYPTION;
          break;
        case TestDigestAlgorithmID::NONE:
          return Result::FATAL_ERROR_INVALID_ARGS;
        MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
      }
    } else if (IsMLDSA(signatureAlgorithm.publicKeyAlg)) {
      // This signs with the parameter set of this key pair, whichever one
      // signatureAlgorithm names, so that tests can create signatures whose
      // algorithm does not match the signer's key.
      keyType = mldsaKey;
      if (publicKeyAlg == ML_DSA(MLDSAParameterSet::ml_dsa_44)) {
        oidTag = SEC_OID_ML_DSA_44;
      } else if (publicKeyAlg == ML_DSA(MLDSAParameterSet::ml_dsa_65)) {
        oidTag = SEC_OID_ML_DSA_65;
      } else if (publicKeyAlg == ML_DSA(MLDSAParameterSet::ml_dsa_87)) {
        oidTag = SEC_OID_ML_DSA_87;
      } else {
        return Result::FATAL_ERROR_INVALID_ARGS;
      }
    } else {
      abort();
    }

    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    if (!slot) {
      return MapPRErrorCodeToResult(PR_GetError());
    }
    SECItem encryptedPrivateKeyInfoItem = {
      siBuffer,
      const_cast<uint8_t*>(encryptedPrivateKey.data()),
      static_cast<unsigned int>(encryptedPrivateKey.length())
    };
    SECItem encryptionAlgorithmItem = {
      siBuffer,
      const_cast<uint8_t*>(encryptionAlgorithm.data()),
      static_cast<unsigned int>(encryptionAlgorithm.length())
    };
    SECItem encryptionParamsItem = {
      siBuffer,
      const_cast<uint8_t*>(encryptionParams.data()),
      static_cast<unsigned int>(encryptionParams.length())
    };
    SECKEYEncryptedPrivateKeyInfo encryptedPrivateKeyInfo = {
      nullptr,
      { encryptionAlgorithmItem, encryptionParamsItem },
      encryptedPrivateKeyInfoItem
    };
    SECItem passwordItem = { siBuffer, nullptr, 0 };
    SECItem publicValueItem = {
      siBuffer,
      const_cast<uint8_t*>(subjectPublicKey.data()),
      static_cast<unsigned int>(subjectPublicKey.length())
    };
    SECKEYPrivateKey* privateKey;
    if (PK11_ImportEncryptedPrivateKeyInfoAndReturnKey(
          slot.get(), &encryptedPrivateKeyInfo, &passwordItem, nullptr,
          &publicValueItem, false, false, keyType, KU_ALL, &privateKey,
          nullptr) != SECSuccess) {
      return MapPRErrorCodeToResult(PR_GetError());
    }
    ScopedSECKEYPrivateKey scopedPrivateKey(privateKey);
    SECItem signatureItem;
    if (SEC_SignData(&signatureItem, tbs.data(),
                     static_cast<int>(tbs.length()),
                     scopedPrivateKey.get(), oidTag) != SECSuccess) {
      return MapPRErrorCodeToResult(PR_GetError());
    }
    signature.assign(signatureItem.data, signatureItem.len);
    SECITEM_FreeItem(&signatureItem, false);
    return Success;
  }

  TestKeyPair* Clone() const override
  {
    return new (std::nothrow) NSSTestKeyPair(publicKeyAlg,
                                             subjectPublicKey,
                                             encryptedPrivateKey,
                                             encryptionAlgorithm,
                                             encryptionParams);
  }

private:
  static bool IsMLDSA(const TestPublicKeyAlgorithm& publicKeyAlg)
  {
    return publicKeyAlg == ML_DSA(MLDSAParameterSet::ml_dsa_44) ||
           publicKeyAlg == ML_DSA(MLDSAParameterSet::ml_dsa_65) ||
           publicKeyAlg == ML_DSA(MLDSAParameterSet::ml_dsa_87);
  }

  const ByteString encryptedPrivateKey;
  const ByteString encryptionAlgorithm;
  const ByteString encryptionParams;
};

} // namespace

// This private function is also used by Gecko's PSM test framework
// (OCSPCommon.cpp).
TestKeyPair* CreateTestKeyPair(const TestPublicKeyAlgorithm publicKeyAlg,
                               const ScopedSECKEYPublicKey& publicKey,
                               const ScopedSECKEYPrivateKey& privateKey)
{
  ScopedCERTSubjectPublicKeyInfo
    spki(SECKEY_CreateSubjectPublicKeyInfo(publicKey.get()));
  if (!spki) {
    return nullptr;
  }
  SECItem spkDER = spki->subjectPublicKey;
  DER_ConvertBitString(&spkDER); // bits to bytes
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    return nullptr;
  }
  // Because NSSTestKeyPair isn't tracked by XPCOM and won't otherwise be aware
  // of shutdown, we don't have a way to release NSS resources at the
  // appropriate time. To work around this, NSSTestKeyPair doesn't hold on to
  // NSS resources. Instead, we export the generated private key part as an
  // encrypted blob (with an empty password and fairly lame encryption). When we
  // need to use it (e.g. to sign something), we decrypt it and create a
  // temporary key object.
  SECItem passwordItem = { siBuffer, nullptr, 0 };
  ScopedSECKEYEncryptedPrivateKeyInfo encryptedPrivateKey(
    PK11_ExportEncryptedPrivKeyInfo(
      slot.get(), SEC_OID_PKCS12_V2_PBE_WITH_SHA1_AND_3KEY_TRIPLE_DES_CBC,
      &passwordItem, privateKey.get(), 1, nullptr));
  if (!encryptedPrivateKey) {
    return nullptr;
  }

  return new (std::nothrow) NSSTestKeyPair(
    publicKeyAlg,
    ByteString(spkDER.data, spkDER.len),
    ByteString(encryptedPrivateKey->encryptedData.data,
               encryptedPrivateKey->encryptedData.len),
    ByteString(encryptedPrivateKey->algorithm.algorithm.data,
               encryptedPrivateKey->algorithm.algorithm.len),
    ByteString(encryptedPrivateKey->algorithm.parameters.data,
               encryptedPrivateKey->algorithm.parameters.len));
}

namespace {

TestKeyPair*
GenerateKeyPairInner()
{
  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    abort();
  }
  PK11RSAGenParams params;
  params.keySizeInBits = 2048;
  params.pe = 65537;

  // Bug 1012786: PK11_GenerateKeyPair can fail if there is insufficient
  // entropy to generate a random key. Attempting to add some entropy and
  // retrying appears to solve this issue.
  for (uint32_t retries = 0; retries < 10; retries++) {
    SECKEYPublicKey* publicKeyTemp = nullptr;
    ScopedSECKEYPrivateKey
      privateKey(PK11_GenerateKeyPair(slot.get(), CKM_RSA_PKCS_KEY_PAIR_GEN,
                                      &params, &publicKeyTemp, false, true,
                                      nullptr));
    ScopedSECKEYPublicKey publicKey(publicKeyTemp);
    if (privateKey) {
      return CreateTestKeyPair(RSA_PKCS1(), publicKey, privateKey);
    }

    assert(!publicKeyTemp);

    if (PR_GetError() != SEC_ERROR_PKCS11_FUNCTION_FAILED) {
      break;
    }

    // Since these keys are only for testing, we don't need them to be good,
    // random keys.
    // https://xkcd.com/221/
    static const uint8_t RANDOM_NUMBER[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    if (PK11_RandomUpdate(
          const_cast<void*>(reinterpret_cast<const void*>(RANDOM_NUMBER)),
          sizeof(RANDOM_NUMBER)) != SECSuccess) {
      break;
    }
  }

  abort();
}

} // namespace

TestKeyPair*
GenerateKeyPair()
{
  InitNSSIfNeeded();
  return GenerateKeyPairInner();
}

TestKeyPair*
CloneReusedKeyPair()
{
  static PRCallOnceType initCallOnce;
  if (PR_CallOnce(&initCallOnce, InitReusedKeyPair) != PR_SUCCESS) {
    abort();
  }
  assert(reusedKeyPair);
  return reusedKeyPair->Clone();
}

TestKeyPair*
GenerateDSSKeyPair()
{
  InitNSSIfNeeded();

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    return nullptr;
  }

  ByteString p(DSS_P());
  ByteString q(DSS_Q());
  ByteString g(DSS_G());

  static const PQGParams PARAMS = {
    nullptr,
    { siBuffer,
      const_cast<uint8_t*>(p.data()),
      static_cast<unsigned int>(p.length())
    },
    { siBuffer,
      const_cast<uint8_t*>(q.data()),
      static_cast<unsigned int>(q.length())
    },
    { siBuffer,
      const_cast<uint8_t*>(g.data()),
      static_cast<unsigned int>(g.length())
    }
  };

  SECKEYPublicKey* publicKeyTemp = nullptr;
  ScopedSECKEYPrivateKey
    privateKey(PK11_GenerateKeyPair(slot.get(), CKM_DSA_KEY_PAIR_GEN,
                                    const_cast<PQGParams*>(&PARAMS),
                                    &publicKeyTemp, false, true, nullptr));
  if (!privateKey) {
    return nullptr;
  }
  ScopedSECKEYPublicKey publicKey(publicKeyTemp);
  return CreateTestKeyPair(DSS(), publicKey, privateKey);
}

TestKeyPair*
GenerateMLDSAKeyPair(MLDSAParameterSet parameterSet)
{
  InitNSSIfNeeded();

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    return nullptr;
  }

  CK_ML_DSA_PARAMETER_SET_TYPE ckParameterSet;
  switch (parameterSet) {
    case MLDSAParameterSet::ml_dsa_44:
      ckParameterSet = CKP_ML_DSA_44;
      break;
    case MLDSAParameterSet::ml_dsa_65:
      ckParameterSet = CKP_ML_DSA_65;
      break;
    case MLDSAParameterSet::ml_dsa_87:
      ckParameterSet = CKP_ML_DSA_87;
      break;
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }

  SECKEYPublicKey* publicKeyTemp = nullptr;
  ScopedSECKEYPrivateKey
    privateKey(PK11_GenerateKeyPair(slot.get(), CKM_ML_DSA_KEY_PAIR_GEN,
                                    &ckParameterSet, &publicKeyTemp, false,
                                    true, nullptr));
  if (!privateKey) {
    return nullptr;
  }
  ScopedSECKEYPublicKey publicKey(publicKeyTemp);
  return CreateTestKeyPair(ML_DSA(parameterSet), publicKey, privateKey);
}

Result
TestVerifyECDSASignedData(Input data, DigestAlgorithm digestAlgorithm,
    Input signature, Input subjectPublicKeyInfo)
{
  InitNSSIfNeeded();
  return VerifyECDSASignedDataNSS(data, digestAlgorithm, signature,
      subjectPublicKeyInfo, nullptr);
}

Result
TestVerifyRSAPKCS1SignedData(Input data, DigestAlgorithm digestAlgorithm,
    Input signature, Input subjectPublicKeyInfo)
{
  InitNSSIfNeeded();
  return VerifyRSAPKCS1SignedDataNSS(data, digestAlgorithm, signature,
      subjectPublicKeyInfo, nullptr);
}

Result
TestVerifyRSAPSSSignedData(Input data, DigestAlgorithm digestAlgorithm,
    Input signature, Input subjectPublicKeyInfo)
{
  InitNSSIfNeeded();
  return VerifyRSAPSSSignedDataNSS(data, digestAlgorithm, signature,
      subjectPublicKeyInfo, nullptr);
}

Result
TestVerifyMLDSASignedData(Input data, MLDSAParameterSet parameterSet,
    Input signature, Input subjectPublicKeyInfo)
{
  InitNSSIfNeeded();
  return VerifyMLDSASignedDataNSS(data, parameterSet, signature,
      subjectPublicKeyInfo, nullptr);
}

Result
TestDigestBuf(Input item,
              DigestAlgorithm digestAlg,
              /*out*/ uint8_t* digestBuf,
              size_t digestBufLen)
{
  InitNSSIfNeeded();
  return DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
}

} } } // namespace mozilla::pkix::test
