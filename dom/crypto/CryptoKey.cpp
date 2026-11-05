/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CryptoKey.h"

#include <cstddef>
#include <cstring>
#include <new>

#include "blapit.h"
#include "certt.h"
#include "js/StructuredClone.h"
#include "js/TypeDecls.h"
#include "keyhi.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/KeyAlgorithmBinding.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SubtleCryptoBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/WebCryptoCommon.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsLiteralString.h"
#include "nsNSSComponent.h"
#include "nsStringFlags.h"
#include "nsTArray.h"
#include "pk11pub.h"
#include "pkcs11t.h"
#include "plarena.h"
#include "prtypes.h"
#include "secasn1.h"
#include "secasn1t.h"
#include "seccomon.h"
#include "secdert.h"
#include "secitem.h"
#include "secmodt.h"
#include "secoid.h"
#include "secoidt.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CryptoKey, mGlobal)
NS_IMPL_CYCLE_COLLECTING_ADDREF(CryptoKey)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CryptoKey)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CryptoKey)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

nsresult StringToUsage(const nsString& aUsage, CryptoKey::KeyUsage& aUsageOut) {
  if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_ENCRYPT)) {
    aUsageOut = CryptoKey::ENCRYPT;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_DECRYPT)) {
    aUsageOut = CryptoKey::DECRYPT;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_SIGN)) {
    aUsageOut = CryptoKey::SIGN;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_VERIFY)) {
    aUsageOut = CryptoKey::VERIFY;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_DERIVEKEY)) {
    aUsageOut = CryptoKey::DERIVEKEY;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_DERIVEBITS)) {
    aUsageOut = CryptoKey::DERIVEBITS;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_WRAPKEY)) {
    aUsageOut = CryptoKey::WRAPKEY;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_UNWRAPKEY)) {
    aUsageOut = CryptoKey::UNWRAPKEY;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_ENCAPSULATEKEY)) {
    aUsageOut = CryptoKey::ENCAPSULATEKEY;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_ENCAPSULATEBITS)) {
    aUsageOut = CryptoKey::ENCAPSULATEBITS;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_DECAPSULATEKEY)) {
    aUsageOut = CryptoKey::DECAPSULATEKEY;
  } else if (aUsage.EqualsLiteral(WEBCRYPTO_KEY_USAGE_DECAPSULATEBITS)) {
    aUsageOut = CryptoKey::DECAPSULATEBITS;
  } else {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }
  return NS_OK;
}

// PK11_CreatePrivateKeyFromTemplate (NSS bug 2047310) creates the session
// object from the template and returns a SECKEYPrivateKey that owns it,
// replacing the old PK11_CreateGenericObject + PK11_FindKeyByKeyID
// ownership-transfer workaround that leaked a session object per key.
UniqueSECKEYPrivateKey PrivateKeyFromPrivateKeyTemplate(
    CK_ATTRIBUTE* aTemplate, CK_ULONG aTemplateSize) {
  UniquePK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    return nullptr;
  }
  return UniqueSECKEYPrivateKey(PK11_CreatePrivateKeyFromTemplate(
      slot.get(), aTemplate, aTemplateSize, nullptr));
}

CryptoKey::CryptoKey(nsIGlobalObject* aGlobal)
    : mGlobal(aGlobal),
      mAttributes(0),
      mSymKey(),
      mPrivateKey(nullptr),
      mPublicKey(nullptr) {}

JSObject* CryptoKey::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return CryptoKey_Binding::Wrap(aCx, this, aGivenProto);
}

void CryptoKey::GetType(nsString& aRetVal) const {
  uint32_t type = mAttributes & TYPE_MASK;
  switch (type) {
    case PUBLIC:
      aRetVal.AssignLiteral(WEBCRYPTO_KEY_TYPE_PUBLIC);
      break;
    case PRIVATE:
      aRetVal.AssignLiteral(WEBCRYPTO_KEY_TYPE_PRIVATE);
      break;
    case SECRET:
      aRetVal.AssignLiteral(WEBCRYPTO_KEY_TYPE_SECRET);
      break;
  }
}

bool CryptoKey::Extractable() const { return (mAttributes & EXTRACTABLE); }

void CryptoKey::GetAlgorithm(JSContext* aCx,
                             JS::MutableHandle<JSObject*> aRetVal,
                             ErrorResult& aRv) const {
  bool converted = false;
  JS::Rooted<JS::Value> val(aCx);
  switch (mAlgorithm.mType) {
    case KeyAlgorithmProxy::AES:
      converted = ToJSValue(aCx, mAlgorithm.mAes, &val);
      break;
    case KeyAlgorithmProxy::KDF:
      converted = ToJSValue(aCx, mAlgorithm.mKDF, &val);
      break;
    case KeyAlgorithmProxy::HMAC:
      converted = ToJSValue(aCx, mAlgorithm.mHmac, &val);
      break;
    case KeyAlgorithmProxy::RSA: {
      RootedDictionary<RsaHashedKeyAlgorithm> rsa(aCx);
      mAlgorithm.mRsa.ToKeyAlgorithm(aCx, rsa, aRv);
      if (aRv.Failed()) {
        return;
      }
      converted = ToJSValue(aCx, rsa, &val);
      break;
    }
    case KeyAlgorithmProxy::EC:
      converted = ToJSValue(aCx, mAlgorithm.mEc, &val);
      break;
    case KeyAlgorithmProxy::OKP:
      converted = ToJSValue(aCx, mAlgorithm.mEd, &val);
      break;
    case KeyAlgorithmProxy::MLKEM:
      converted = ToJSValue(aCx, mAlgorithm.mMlKem, &val);
      break;
  }
  if (!converted) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  aRetVal.set(&val.toObject());
}

void CryptoKey::GetUsages(nsTArray<nsString>& aRetVal) const {
  if (mAttributes & ENCRYPT) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_ENCRYPT));
  }
  if (mAttributes & DECRYPT) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_DECRYPT));
  }
  if (mAttributes & SIGN) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_SIGN));
  }
  if (mAttributes & VERIFY) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_VERIFY));
  }
  if (mAttributes & DERIVEKEY) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_DERIVEKEY));
  }
  if (mAttributes & DERIVEBITS) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_DERIVEBITS));
  }
  if (mAttributes & WRAPKEY) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_WRAPKEY));
  }
  if (mAttributes & UNWRAPKEY) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_UNWRAPKEY));
  }
  if (mAttributes & ENCAPSULATEKEY) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_ENCAPSULATEKEY));
  }
  if (mAttributes & ENCAPSULATEBITS) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_ENCAPSULATEBITS));
  }
  if (mAttributes & DECAPSULATEKEY) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_DECAPSULATEKEY));
  }
  if (mAttributes & DECAPSULATEBITS) {
    aRetVal.AppendElement(
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_USAGE_DECAPSULATEBITS));
  }
}

KeyAlgorithmProxy& CryptoKey::Algorithm() { return mAlgorithm; }

const KeyAlgorithmProxy& CryptoKey::Algorithm() const { return mAlgorithm; }

CryptoKey::KeyType CryptoKey::GetKeyType() const {
  return static_cast<CryptoKey::KeyType>(mAttributes & TYPE_MASK);
}

nsresult CryptoKey::SetType(const nsString& aType) {
  mAttributes &= CLEAR_TYPE;
  if (aType.EqualsLiteral(WEBCRYPTO_KEY_TYPE_SECRET)) {
    mAttributes |= SECRET;
  } else if (aType.EqualsLiteral(WEBCRYPTO_KEY_TYPE_PUBLIC)) {
    mAttributes |= PUBLIC;
  } else if (aType.EqualsLiteral(WEBCRYPTO_KEY_TYPE_PRIVATE)) {
    mAttributes |= PRIVATE;
  } else {
    mAttributes |= UNKNOWN;
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  return NS_OK;
}

void CryptoKey::SetType(CryptoKey::KeyType aType) {
  mAttributes &= CLEAR_TYPE;
  mAttributes |= aType;
}

void CryptoKey::SetExtractable(bool aExtractable) {
  mAttributes &= CLEAR_EXTRACTABLE;
  if (aExtractable) {
    mAttributes |= EXTRACTABLE;
  }
}

// NSS exports private EC keys without the CKA_EC_POINT attribute, i.e. the
// public value. To properly export the private key to JWK or PKCS #8 we need
// the public key data though and so we use this method to augment a private
// key with data from the given public key.
nsresult CryptoKey::AddPublicKeyData(SECKEYPublicKey* aPublicKey) {
  // This should be a private key.
  MOZ_ASSERT(GetKeyType() == PRIVATE);
  // There should be a private NSS key with type 'EC', 'EC Montgomery' or 'ED'.
  MOZ_ASSERT(mPrivateKey &&
             (mPrivateKey->keyType == ecKey || mPrivateKey->keyType == edKey ||
              mPrivateKey->keyType == ecMontKey));
  // The given public key should have the same key type.
  MOZ_ASSERT(aPublicKey->keyType == mPrivateKey->keyType);

  // Read EC params.
  ScopedAutoSECItem params;
  SECStatus rv = PK11_ReadRawAttribute(PK11_TypePrivKey, mPrivateKey.get(),
                                       CKA_EC_PARAMS, &params);
  if (rv != SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  // Read private value.
  ScopedAutoSECItem value;
  rv = PK11_ReadRawAttribute(PK11_TypePrivKey, mPrivateKey.get(), CKA_VALUE,
                             &value);
  if (rv != SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  SECItem* point = &aPublicKey->u.ec.publicValue;
  CK_OBJECT_CLASS privateKeyValue = CKO_PRIVATE_KEY;
  CK_BBOOL falseValue = CK_FALSE;

  // ecKey corresponds to CKK_EC;
  // edKey corresponds to CKK_EC_EDWARDS key,
  // ecMontKey corresponds to CKK_EC_MONTGOMERY.
  // The other key types are not allowed.
  CK_KEY_TYPE ecValue;
  if (mPrivateKey->keyType == ecKey) {
    ecValue = CKK_EC;
  } else if (mPrivateKey->keyType == edKey) {
    ecValue = CKK_EC_EDWARDS;
  } else if (mPrivateKey->keyType == ecMontKey) {
    ecValue = CKK_EC_MONTGOMERY;
  } else {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  CK_ATTRIBUTE keyTemplate[9] = {
      {CKA_CLASS, &privateKeyValue, sizeof(privateKeyValue)},
      {CKA_KEY_TYPE, &ecValue, sizeof(ecValue)},
      {CKA_TOKEN, &falseValue, sizeof(falseValue)},
      {CKA_SENSITIVE, &falseValue, sizeof(falseValue)},
      {CKA_PRIVATE, &falseValue, sizeof(falseValue)},
      // PrivateKeyFromPrivateKeyTemplate sets the ID.
      {CKA_ID, nullptr, 0},
      {CKA_EC_PARAMS, params.data, params.len},
      {CKA_EC_POINT, point->data, point->len},
      {CKA_VALUE, value.data, value.len},
  };

  mPrivateKey =
      PrivateKeyFromPrivateKeyTemplate(keyTemplate, std::size(keyTemplate));
  NS_ENSURE_TRUE(mPrivateKey, NS_ERROR_DOM_OPERATION_ERR);

  return NS_OK;
}

void CryptoKey::ClearUsages() { mAttributes &= CLEAR_USAGES; }

nsresult CryptoKey::AddUsage(const nsString& aUsage) {
  KeyUsage usage;
  if (NS_FAILED(StringToUsage(aUsage, usage))) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  MOZ_ASSERT(usage & USAGES_MASK, "Usages should be valid");

  // This is harmless if usage is 0, so we don't repeat the assertion check
  AddUsage(usage);
  return NS_OK;
}

nsresult CryptoKey::AddAllowedUsage(const nsString& aUsage,
                                    const nsString& aAlgorithm) {
  return AddAllowedUsageIntersecting(aUsage, aAlgorithm, USAGES_MASK);
}

nsresult CryptoKey::AddAllowedUsageIntersecting(const nsString& aUsage,
                                                const nsString& aAlgorithm,
                                                uint32_t aUsageMask) {
  uint32_t allowedUsages = GetAllowedUsagesForAlgorithm(aAlgorithm);
  KeyUsage usage;
  if (NS_FAILED(StringToUsage(aUsage, usage))) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  if ((usage & allowedUsages) != usage) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  MOZ_ASSERT(usage & USAGES_MASK, "Usages should be valid");

  // This is harmless if usage is 0, so we don't repeat the assertion check
  if (usage & aUsageMask) {
    AddUsage(usage);
    return NS_OK;
  }

  return NS_OK;
}

void CryptoKey::AddUsage(CryptoKey::KeyUsage aUsage) { mAttributes |= aUsage; }

bool CryptoKey::HasAnyUsage() { return !!(mAttributes & USAGES_MASK); }

bool CryptoKey::HasUsage(CryptoKey::KeyUsage aUsage) {
  return !!(mAttributes & aUsage);
}

bool CryptoKey::HasUsageOtherThan(uint32_t aUsages) {
  return !!(mAttributes & USAGES_MASK & ~aUsages);
}

bool CryptoKey::IsRecognizedUsage(const nsString& aUsage) {
  KeyUsage dummy;
  nsresult rv = StringToUsage(aUsage, dummy);
  return NS_SUCCEEDED(rv);
}

bool CryptoKey::AllUsagesRecognized(const Sequence<nsString>& aUsages) {
  for (uint32_t i = 0; i < aUsages.Length(); ++i) {
    if (!IsRecognizedUsage(aUsages[i])) {
      return false;
    }
  }
  return true;
}

uint32_t CryptoKey::GetAllowedUsagesForAlgorithm(const nsString& aAlgorithm) {
  uint32_t allowedUsages = 0;
  if (aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_AES_CTR) ||
      aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_AES_CBC) ||
      aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_AES_GCM) ||
      aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_RSA_OAEP)) {
    allowedUsages = ENCRYPT | DECRYPT | WRAPKEY | UNWRAPKEY;
  } else if (aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_AES_KW)) {
    allowedUsages = WRAPKEY | UNWRAPKEY;
  } else if (aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_HMAC) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_RSA_PSS) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_ECDSA) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_ED25519)) {
    allowedUsages = SIGN | VERIFY;
  } else if (aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_ECDH) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_HKDF) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_PBKDF2) ||
             aAlgorithm.EqualsASCII(WEBCRYPTO_ALG_X25519)) {
    allowedUsages = DERIVEBITS | DERIVEKEY;
  } else if (IsMLKEMAlgorithm(aAlgorithm)) {
    allowedUsages =
        ENCAPSULATEBITS | ENCAPSULATEKEY | DECAPSULATEBITS | DECAPSULATEKEY;
  }
  return allowedUsages;
}

nsresult CryptoKey::SetSymKey(const CryptoBuffer& aSymKey) {
  if (!mSymKey.Assign(aSymKey)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

nsresult CryptoKey::SetPrivateKey(SECKEYPrivateKey* aPrivateKey) {
  if (!aPrivateKey) {
    mPrivateKey = nullptr;
    return NS_OK;
  }

  mPrivateKey = UniqueSECKEYPrivateKey(SECKEY_CopyPrivateKey(aPrivateKey));
  return mPrivateKey ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

nsresult CryptoKey::SetPublicKey(SECKEYPublicKey* aPublicKey) {
  if (!aPublicKey) {
    mPublicKey = nullptr;
    return NS_OK;
  }

  mPublicKey = UniqueSECKEYPublicKey(SECKEY_CopyPublicKey(aPublicKey));
  return mPublicKey ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

const CryptoBuffer& CryptoKey::GetSymKey() const { return mSymKey; }

UniqueSECKEYPrivateKey CryptoKey::GetPrivateKey() const {
  if (!mPrivateKey) {
    return nullptr;
  }
  return UniqueSECKEYPrivateKey(SECKEY_CopyPrivateKey(mPrivateKey.get()));
}

UniqueSECKEYPublicKey CryptoKey::GetPublicKey() const {
  if (!mPublicKey) {
    return nullptr;
  }
  return UniqueSECKEYPublicKey(SECKEY_CopyPublicKey(mPublicKey.get()));
}

// Serialization and deserialization convenience methods

UniqueSECKEYPrivateKey CryptoKey::PrivateKeyFromPkcs8(CryptoBuffer& aKeyData) {
  UniquePK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    return nullptr;
  }

  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return nullptr;
  }

  SECItem pkcs8Item = {siBuffer, nullptr, 0};
  if (!aKeyData.ToSECItem(arena.get(), &pkcs8Item)) {
    return nullptr;
  }

  // Allow everything, we enforce usage ourselves
  unsigned int usage = KU_ALL;

  SECKEYPrivateKey* privKey;
  SECStatus rv = PK11_ImportDERPrivateKeyInfoAndReturnKey(
      slot.get(), &pkcs8Item, nullptr, nullptr, false, false, usage, &privKey,
      nullptr);

  if (rv == SECFailure) {
    return nullptr;
  }

  return UniqueSECKEYPrivateKey(privKey);
}

UniqueSECKEYPublicKey CryptoKey::PublicKeyFromSpki(CryptoBuffer& aKeyData) {
  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return nullptr;
  }

  SECItem spkiItem = {siBuffer, nullptr, 0};
  if (!aKeyData.ToSECItem(arena.get(), &spkiItem)) {
    return nullptr;
  }

  UniqueCERTSubjectPublicKeyInfo spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spkiItem));
  if (!spki) {
    return nullptr;
  }

  bool isECDHAlgorithm =
      SECITEM_ItemsAreEqual(&SEC_OID_DATA_EC_DH, &spki->algorithm.algorithm);

  // Check for |id-ecDH|. Per old versions of the WebCrypto spec we must
  // support this OID but NSS does unfortunately not know it. Let's
  // change the algorithm to |id-ecPublicKey| to make NSS happy.
  if (isECDHAlgorithm) {
    SECOidTag oid = SEC_OID_ANSIX962_EC_PUBLIC_KEY;

    SECOidData* oidData = SECOID_FindOIDByTag(oid);
    if (!oidData) {
      return nullptr;
    }

    SECStatus rv = SECITEM_CopyItem(spki->arena, &spki->algorithm.algorithm,
                                    &oidData->oid);
    if (rv != SECSuccess) {
      return nullptr;
    }
  }

  UniqueSECKEYPublicKey tmp(SECKEY_ExtractPublicKey(spki.get()));
  if (!tmp.get() || !PublicKeyValid(tmp.get())) {
    return nullptr;
  }

  return UniqueSECKEYPublicKey(SECKEY_CopyPublicKey(tmp.get()));
}

nsresult CryptoKey::PrivateKeyToPkcs8(SECKEYPrivateKey* aPrivKey,
                                      CryptoBuffer& aRetVal) {
  UniqueSECItem pkcs8Item(PK11_ExportDERPrivateKeyInfo(aPrivKey, nullptr));
  if (!pkcs8Item.get()) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }
  if (!aRetVal.Assign(pkcs8Item.get())) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }
  return NS_OK;
}

nsresult CryptoKey::PublicKeyToSpki(SECKEYPublicKey* aPubKey,
                                    CryptoBuffer& aRetVal) {
  UniqueCERTSubjectPublicKeyInfo spki;

  spki.reset(SECKEY_CreateSubjectPublicKeyInfo(aPubKey));
  if (!spki) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  const SEC_ASN1Template* tpl = SEC_ASN1_GET(CERT_SubjectPublicKeyInfoTemplate);
  UniqueSECItem spkiItem(SEC_ASN1EncodeItem(nullptr, nullptr, spki.get(), tpl));

  if (!aRetVal.Assign(spkiItem.get())) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }
  return NS_OK;
}

SECItem* CreateECPointForCoordinates(const CryptoBuffer& aX,
                                     const CryptoBuffer& aY,
                                     PLArenaPool* aArena) {
  // Check that both points have the same length.
  if (aX.Length() != aY.Length()) {
    return nullptr;
  }

  // Create point.
  SECItem* point =
      ::SECITEM_AllocItem(aArena, nullptr, aX.Length() + aY.Length() + 1);
  if (!point) {
    return nullptr;
  }

  // Set point data.
  point->data[0] = EC_POINT_FORM_UNCOMPRESSED;
  memcpy(point->data + 1, aX.Elements(), aX.Length());
  memcpy(point->data + 1 + aX.Length(), aY.Elements(), aY.Length());

  return point;
}

nsresult CheckEDKeyLen(const CryptoBuffer& p) {
  /* Ed25519 keys are 32 bytes long.
    See: https://datatracker.ietf.org/doc/html/rfc8032 Introduction. */
  uint32_t lengthEDPrivatePublicKey = 32;
  if (p.Length() != lengthEDPrivatePublicKey) {
    /* We do not use this error code, we only check is the function returns
     * NS_OK or not. */
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  return NS_OK;
}

SECItem* CreateEDPointForXCoordinate(const CryptoBuffer& aX,
                                     PLArenaPool* aArena) {
  if (NS_FAILED(CheckEDKeyLen(aX))) {
    return nullptr;
  }

  // Create point.
  SECItem* point = ::SECITEM_AllocItem(aArena, nullptr, aX.Length());
  if (!point) {
    return nullptr;
  }

  // Set point data.
  memcpy(point->data, aX.Elements(), aX.Length());
  return point;
}

UniqueSECKEYPrivateKey CryptoKey::PrivateKeyFromJwk(const JsonWebKey& aJwk,
                                                    const nsString aAlgName) {
  CK_OBJECT_CLASS privateKeyValue = CKO_PRIVATE_KEY;
  CK_BBOOL falseValue = CK_FALSE;

  if (aJwk.mKty.EqualsLiteral(JWK_TYPE_EC)) {
    // Verify that all of the required parameters are present
    CryptoBuffer x, y, d;
    if (!aJwk.mCrv.WasPassed() || !aJwk.mX.WasPassed() ||
        NS_FAILED(x.FromJwkBase64(aJwk.mX.Value())) || !aJwk.mY.WasPassed() ||
        NS_FAILED(y.FromJwkBase64(aJwk.mY.Value())) || !aJwk.mD.WasPassed() ||
        NS_FAILED(d.FromJwkBase64(aJwk.mD.Value()))) {
      return nullptr;
    }

    nsString namedCurve;
    if (!NormalizeToken(aJwk.mCrv.Value(), namedCurve)) {
      return nullptr;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return nullptr;
    }

    // Create parameters.
    SECItem* params = CreateECParamsForCurve(namedCurve, arena.get());
    if (!params) {
      return nullptr;
    }

    SECItem* ecPoint = CreateECPointForCoordinates(x, y, arena.get());
    if (!ecPoint) {
      return nullptr;
    }

    // Populate template from parameters
    CK_KEY_TYPE ecValue = CKK_EC;
    CK_ATTRIBUTE keyTemplate[9] = {
        {CKA_CLASS, &privateKeyValue, sizeof(privateKeyValue)},
        {CKA_KEY_TYPE, &ecValue, sizeof(ecValue)},
        {CKA_TOKEN, &falseValue, sizeof(falseValue)},
        {CKA_SENSITIVE, &falseValue, sizeof(falseValue)},
        {CKA_PRIVATE, &falseValue, sizeof(falseValue)},
        // PrivateKeyFromPrivateKeyTemplate sets the ID.
        {CKA_ID, nullptr, 0},
        {CKA_EC_PARAMS, params->data, params->len},
        {CKA_EC_POINT, ecPoint->data, ecPoint->len},
        {CKA_VALUE, (void*)d.Elements(), (CK_ULONG)d.Length()},
    };

    return PrivateKeyFromPrivateKeyTemplate(keyTemplate,
                                            std::size(keyTemplate));
  }

  if (aJwk.mKty.EqualsLiteral(JWK_TYPE_RSA)) {
    // Verify that all of the required parameters are present
    CryptoBuffer n, e, d, p, q, dp, dq, qi;
    if (!aJwk.mN.WasPassed() || NS_FAILED(n.FromJwkBase64(aJwk.mN.Value())) ||
        !aJwk.mE.WasPassed() || NS_FAILED(e.FromJwkBase64(aJwk.mE.Value())) ||
        !aJwk.mD.WasPassed() || NS_FAILED(d.FromJwkBase64(aJwk.mD.Value())) ||
        !aJwk.mP.WasPassed() || NS_FAILED(p.FromJwkBase64(aJwk.mP.Value())) ||
        !aJwk.mQ.WasPassed() || NS_FAILED(q.FromJwkBase64(aJwk.mQ.Value())) ||
        !aJwk.mDp.WasPassed() ||
        NS_FAILED(dp.FromJwkBase64(aJwk.mDp.Value())) ||
        !aJwk.mDq.WasPassed() ||
        NS_FAILED(dq.FromJwkBase64(aJwk.mDq.Value())) ||
        !aJwk.mQi.WasPassed() ||
        NS_FAILED(qi.FromJwkBase64(aJwk.mQi.Value()))) {
      return nullptr;
    }

    // Populate template from parameters
    CK_KEY_TYPE rsaValue = CKK_RSA;
    CK_ATTRIBUTE keyTemplate[14] = {
        {CKA_CLASS, &privateKeyValue, sizeof(privateKeyValue)},
        {CKA_KEY_TYPE, &rsaValue, sizeof(rsaValue)},
        {CKA_TOKEN, &falseValue, sizeof(falseValue)},
        {CKA_SENSITIVE, &falseValue, sizeof(falseValue)},
        {CKA_PRIVATE, &falseValue, sizeof(falseValue)},
        // PrivateKeyFromPrivateKeyTemplate sets the ID.
        {CKA_ID, nullptr, 0},
        {CKA_MODULUS, (void*)n.Elements(), (CK_ULONG)n.Length()},
        {CKA_PUBLIC_EXPONENT, (void*)e.Elements(), (CK_ULONG)e.Length()},
        {CKA_PRIVATE_EXPONENT, (void*)d.Elements(), (CK_ULONG)d.Length()},
        {CKA_PRIME_1, (void*)p.Elements(), (CK_ULONG)p.Length()},
        {CKA_PRIME_2, (void*)q.Elements(), (CK_ULONG)q.Length()},
        {CKA_EXPONENT_1, (void*)dp.Elements(), (CK_ULONG)dp.Length()},
        {CKA_EXPONENT_2, (void*)dq.Elements(), (CK_ULONG)dq.Length()},
        {CKA_COEFFICIENT, (void*)qi.Elements(), (CK_ULONG)qi.Length()},
    };

    return PrivateKeyFromPrivateKeyTemplate(keyTemplate,
                                            std::size(keyTemplate));
  }

  if (aJwk.mKty.EqualsLiteral(JWK_TYPE_OKP)) {
    CryptoBuffer x, d;

    if (!aJwk.mCrv.WasPassed() || !aJwk.mX.WasPassed() ||
        NS_FAILED(x.FromJwkBase64(aJwk.mX.Value())) || !aJwk.mD.WasPassed() ||
        NS_FAILED(d.FromJwkBase64(aJwk.mD.Value()))) {
      return nullptr;
    }

    nsString namedCurve;
    if (!NormalizeToken(aJwk.mCrv.Value(), namedCurve)) {
      return nullptr;
    }

    if (!namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519) &&
        !namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519)) {
      return nullptr;
    }

    MOZ_ASSERT(!aAlgName.IsEmpty());

    if (namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519) &&
        !aAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
      return nullptr;
    }

    if (namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519) &&
        !aAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
      return nullptr;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return nullptr;
    }

    // Create parameters.
    SECItem* params = CreateECParamsForCurve(namedCurve, arena.get());
    if (!params) {
      return nullptr;
    }

    SECItem* ecPoint = CreateEDPointForXCoordinate(x, arena.get());
    if (!ecPoint) {
      return nullptr;
    }

    if (CheckEDKeyLen(d) != NS_OK) {
      return nullptr;
    }

    // Populate template from parameters
    CK_KEY_TYPE ecValue;
    if (namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519)) {
      ecValue = CKK_EC_EDWARDS;
    } else if (namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519)) {
      ecValue = CKK_EC_MONTGOMERY;
    } else {
      return nullptr;
    }

    CK_ATTRIBUTE keyTemplate[9] = {
        {CKA_CLASS, &privateKeyValue, sizeof(privateKeyValue)},
        {CKA_KEY_TYPE, &ecValue, sizeof(ecValue)},
        {CKA_TOKEN, &falseValue, sizeof(falseValue)},
        {CKA_SENSITIVE, &falseValue, sizeof(falseValue)},
        {CKA_PRIVATE, &falseValue, sizeof(falseValue)},
        // PrivateKeyFromPrivateKeyTemplate sets the ID.
        {CKA_ID, nullptr, 0},
        {CKA_EC_PARAMS, params->data, params->len},
        {CKA_EC_POINT, ecPoint->data, ecPoint->len},
        {CKA_VALUE, (void*)d.Elements(), (CK_ULONG)d.Length()},
    };

    return PrivateKeyFromPrivateKeyTemplate(keyTemplate,
                                            std::size(keyTemplate));
  }

  return nullptr;
}

bool ReadAndEncodeAttribute(SECKEYPrivateKey* aKey,
                            CK_ATTRIBUTE_TYPE aAttribute,
                            Optional<nsString>& aDst) {
  ScopedAutoSECItem item;
  if (PK11_ReadRawAttribute(PK11_TypePrivKey, aKey, aAttribute, &item) !=
      SECSuccess) {
    return false;
  }

  CryptoBuffer buffer;
  if (!buffer.Assign(&item)) {
    return false;
  }

  if (NS_FAILED(buffer.ToJwkBase64(aDst.Value()))) {
    return false;
  }

  return true;
}

bool OKPKeyToJwk(const SECItem* aEcParams, const SECItem* aPublicValue,
                 JsonWebKey& aRetVal) {
  aRetVal.mX.Construct();

  SECOidTag tag;
  if (!FindOIDTagForEncodedParameters(aEcParams, &tag)) {
    return false;
  }

  uint32_t flen;
  switch (tag) {
    case SEC_OID_ED25519_PUBLIC_KEY:
      flen = 32;
      aRetVal.mCrv.Construct(
          NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_NAMED_CURVE_ED25519));
      break;
    case SEC_OID_X25519:
      flen = 32;
      aRetVal.mCrv.Construct(
          NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_NAMED_CURVE_CURVE25519));
      break;
    default:
      return false;
  }

  /* No compression is used. */
  if (aPublicValue->len != flen) {
    return false;
  }

  CryptoBuffer x;
  if (!x.Assign(aPublicValue) || NS_FAILED(x.ToJwkBase64(aRetVal.mX.Value()))) {
    return false;
  }

  aRetVal.mKty = NS_LITERAL_STRING_FROM_CSTRING(JWK_TYPE_OKP);
  return true;
}

bool ECKeyToJwk(const PK11ObjectType aKeyType, void* aKey,
                const SECItem* aEcParams, const SECItem* aPublicValue,
                JsonWebKey& aRetVal) {
  aRetVal.mX.Construct();
  aRetVal.mY.Construct();

  // Check that the given EC parameters are valid.
  SECOidTag tag;
  if (!FindOIDTagForEncodedParameters(aEcParams, &tag)) {
    return false;
  }

  uint32_t flen;
  switch (tag) {
    case SEC_OID_SECG_EC_SECP256R1:
      flen = 32;  // bytes
      aRetVal.mCrv.Construct(
          NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_NAMED_CURVE_P256));
      break;
    case SEC_OID_SECG_EC_SECP384R1:
      flen = 48;  // bytes
      aRetVal.mCrv.Construct(
          NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_NAMED_CURVE_P384));
      break;
    case SEC_OID_SECG_EC_SECP521R1:
      flen = 66;  // bytes
      aRetVal.mCrv.Construct(
          NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_NAMED_CURVE_P521));
      break;
    default:
      return false;
  }

  // No support for compressed points.
  if (aPublicValue->data[0] != EC_POINT_FORM_UNCOMPRESSED) {
    return false;
  }

  // Check length of uncompressed point coordinates.
  if (aPublicValue->len != (2 * flen + 1)) {
    return false;
  }

  UniqueSECItem ecPointX(::SECITEM_AllocItem(nullptr, nullptr, flen));
  UniqueSECItem ecPointY(::SECITEM_AllocItem(nullptr, nullptr, flen));
  if (!ecPointX || !ecPointY) {
    return false;
  }

  // Extract point data.
  memcpy(ecPointX->data, aPublicValue->data + 1, flen);
  memcpy(ecPointY->data, aPublicValue->data + 1 + flen, flen);

  CryptoBuffer x, y;
  if (!x.Assign(ecPointX.get()) ||
      NS_FAILED(x.ToJwkBase64(aRetVal.mX.Value())) ||
      !y.Assign(ecPointY.get()) ||
      NS_FAILED(y.ToJwkBase64(aRetVal.mY.Value()))) {
    return false;
  }

  aRetVal.mKty = NS_LITERAL_STRING_FROM_CSTRING(JWK_TYPE_EC);
  return true;
}

nsresult CryptoKey::PrivateKeyToJwk(SECKEYPrivateKey* aPrivKey,
                                    JsonWebKey& aRetVal) {
  switch (aPrivKey->keyType) {
    case rsaKey: {
      aRetVal.mN.Construct();
      aRetVal.mE.Construct();
      aRetVal.mD.Construct();
      aRetVal.mP.Construct();
      aRetVal.mQ.Construct();
      aRetVal.mDp.Construct();
      aRetVal.mDq.Construct();
      aRetVal.mQi.Construct();

      if (!ReadAndEncodeAttribute(aPrivKey, CKA_MODULUS, aRetVal.mN) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_PUBLIC_EXPONENT, aRetVal.mE) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_PRIVATE_EXPONENT, aRetVal.mD) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_PRIME_1, aRetVal.mP) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_PRIME_2, aRetVal.mQ) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_EXPONENT_1, aRetVal.mDp) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_EXPONENT_2, aRetVal.mDq) ||
          !ReadAndEncodeAttribute(aPrivKey, CKA_COEFFICIENT, aRetVal.mQi)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      aRetVal.mKty = NS_LITERAL_STRING_FROM_CSTRING(JWK_TYPE_RSA);
      return NS_OK;
    }

    case edKey:
    case ecMontKey: {
      // Read EC params.
      ScopedAutoSECItem params;
      SECStatus rv = PK11_ReadRawAttribute(PK11_TypePrivKey, aPrivKey,
                                           CKA_EC_PARAMS, &params);
      if (rv != SECSuccess) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      // Read public point Q.
      ScopedAutoSECItem ecPoint;
      rv = PK11_ReadRawAttribute(PK11_TypePrivKey, aPrivKey, CKA_EC_POINT,
                                 &ecPoint);

      if (rv != SECSuccess) {
        // SECKEY_ConvertToPublicKey will try to derive public key
        UniqueSECKEYPublicKey pubKey =
            UniqueSECKEYPublicKey(SECKEY_ConvertToPublicKey(aPrivKey));
        rv = PK11_ReadRawAttribute(PK11_TypePubKey, pubKey.get(), CKA_EC_POINT,
                                   &ecPoint);

        if (rv != SECSuccess) {
          return NS_ERROR_DOM_OPERATION_ERR;
        }
      }

      if (!OKPKeyToJwk(&params, &ecPoint, aRetVal)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      aRetVal.mD.Construct();

      // Read private value.
      if (!ReadAndEncodeAttribute(aPrivKey, CKA_VALUE, aRetVal.mD)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      return NS_OK;
    }
    case ecKey: {
      // Read EC params.
      ScopedAutoSECItem params;
      SECStatus rv = PK11_ReadRawAttribute(PK11_TypePrivKey, aPrivKey,
                                           CKA_EC_PARAMS, &params);
      if (rv != SECSuccess) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      // Read public point Q.
      ScopedAutoSECItem ecPoint;
      rv = PK11_ReadRawAttribute(PK11_TypePrivKey, aPrivKey, CKA_EC_POINT,
                                 &ecPoint);
      if (rv != SECSuccess) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      if (!ECKeyToJwk(PK11_TypePrivKey, aPrivKey, &params, &ecPoint, aRetVal)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      aRetVal.mD.Construct();

      // Read private value.
      if (!ReadAndEncodeAttribute(aPrivKey, CKA_VALUE, aRetVal.mD)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      return NS_OK;
    }

    case kyberKey: {
      CryptoBuffer seed;
      nsresult rv = PrivateMLKEMKeyToSeed(aPrivKey, seed);
      if (NS_FAILED(rv)) {
        return rv;
      }

      UniqueSECKEYPublicKey pubKey(SECKEY_ConvertToPublicKey(aPrivKey));
      if (!pubKey || pubKey->keyType != kyberKey) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      rv = PublicKeyToJwk(pubKey.get(), aRetVal);
      if (NS_FAILED(rv)) {
        return rv;
      }

      aRetVal.mPriv.Construct();
      if (NS_FAILED(seed.ToJwkBase64(aRetVal.mPriv.Value()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
      return NS_OK;
    }
    default:
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }
}

/* The function is used to determine a key type from the curve. */
KeyType KeyTypeFromCurveName(const nsAString& aNamedCurve) {
  KeyType t = nullKey;
  if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_P256) ||
      aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_P384) ||
      aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_P521)) {
    t = ecKey;
  } else if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519)) {
    t = edKey;
  } else if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519)) {
    t = ecMontKey;
  }
  return t;
}

/* The function is used for EC and ED keys. */
UniqueSECKEYPublicKey CreateECPublicKey(const SECItem* aKeyData,
                                        const nsAString& aNamedCurve) {
  if (!EnsureNSSInitializedChromeOrContent()) {
    return nullptr;
  }

  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return nullptr;
  }

  // It's important that this be a UniqueSECKEYPublicKey, as this ensures that
  // SECKEY_DestroyPublicKey will be called on it. If this doesn't happen, when
  // CryptoKey::PublicKeyValid is called on it and it gets moved to the internal
  // PKCS#11 slot, it will leak a reference to the slot.
  UniqueSECKEYPublicKey key(PORT_ArenaZNew(arena.get(), SECKEYPublicKey));
  if (!key) {
    return nullptr;
  }

  // Transfer arena ownership to the key.
  key->arena = arena.release();
  key->keyType = KeyTypeFromCurveName(aNamedCurve);
  if (key->keyType != ecKey && key->keyType != edKey &&
      key->keyType != ecMontKey) {
    return nullptr;
  }

  key->pkcs11Slot = nullptr;
  key->pkcs11ID = CK_INVALID_HANDLE;

  // Create curve parameters.
  SECItem* params = CreateECParamsForCurve(aNamedCurve, key->arena);
  if (!params) {
    return nullptr;
  }
  key->u.ec.DEREncodedParams = *params;

  // Set public point.
  SECStatus ret =
      SECITEM_CopyItem(key->arena, &key->u.ec.publicValue, aKeyData);
  if (NS_WARN_IF(ret != SECSuccess)) {
    return nullptr;
  }

  // Ensure the given point is on the curve.
  if (!CryptoKey::PublicKeyValid(key.get())) {
    return nullptr;
  }

  return key;
}

UniqueSECKEYPublicKey CryptoKey::PublicKeyFromJwk(const JsonWebKey& aJwk,
                                                  const nsString aAlgName) {
  if (aJwk.mKty.EqualsLiteral(JWK_TYPE_RSA)) {
    // Verify that all of the required parameters are present
    CryptoBuffer n, e;
    if (!aJwk.mN.WasPassed() || NS_FAILED(n.FromJwkBase64(aJwk.mN.Value())) ||
        !aJwk.mE.WasPassed() || NS_FAILED(e.FromJwkBase64(aJwk.mE.Value()))) {
      return nullptr;
    }

    // Transcode to a DER RSAPublicKey structure
    struct RSAPublicKeyData {
      SECItem n;
      SECItem e;
    };
    const RSAPublicKeyData input = {
        {siUnsignedInteger, n.Elements(), (unsigned int)n.Length()},
        {siUnsignedInteger, e.Elements(), (unsigned int)e.Length()}};
    const SEC_ASN1Template rsaPublicKeyTemplate[] = {
        {SEC_ASN1_SEQUENCE, 0, nullptr, sizeof(RSAPublicKeyData)},
        {
            SEC_ASN1_INTEGER,
            offsetof(RSAPublicKeyData, n),
        },
        {
            SEC_ASN1_INTEGER,
            offsetof(RSAPublicKeyData, e),
        },
        {
            0,
        }};

    UniqueSECItem pkDer(
        SEC_ASN1EncodeItem(nullptr, nullptr, &input, rsaPublicKeyTemplate));
    if (!pkDer.get()) {
      return nullptr;
    }

    return UniqueSECKEYPublicKey(
        SECKEY_ImportDERPublicKey(pkDer.get(), CKK_RSA));
  }

  if (aJwk.mKty.EqualsLiteral(JWK_TYPE_EC)) {
    // Verify that all of the required parameters are present
    CryptoBuffer x, y;
    if (!aJwk.mCrv.WasPassed() || !aJwk.mX.WasPassed() ||
        NS_FAILED(x.FromJwkBase64(aJwk.mX.Value())) || !aJwk.mY.WasPassed() ||
        NS_FAILED(y.FromJwkBase64(aJwk.mY.Value()))) {
      return nullptr;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return nullptr;
    }

    // Create point.
    SECItem* point = CreateECPointForCoordinates(x, y, arena.get());
    if (!point) {
      return nullptr;
    }

    nsString namedCurve;
    if (!NormalizeToken(aJwk.mCrv.Value(), namedCurve)) {
      return nullptr;
    }

    return CreateECPublicKey(point, namedCurve);
  }

  if (aJwk.mKty.EqualsLiteral(JWK_TYPE_OKP)) {
    // Verify that all of the required parameters are present
    CryptoBuffer x;
    if (!aJwk.mCrv.WasPassed() || !aJwk.mX.WasPassed() ||
        NS_FAILED(x.FromJwkBase64(aJwk.mX.Value()))) {
      return nullptr;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return nullptr;
    }

    // Create point.
    SECItem* point = CreateEDPointForXCoordinate(x, arena.get());
    if (!point) {
      return nullptr;
    }

    nsString namedCurve;
    if (!NormalizeToken(aJwk.mCrv.Value(), namedCurve)) {
      return nullptr;
    }

    if (!namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519) &&
        !namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519)) {
      return nullptr;
    }

    MOZ_ASSERT(!aAlgName.IsEmpty());
    if (namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519) &&
        !aAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
      return nullptr;
    }

    if (namedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519) &&
        !aAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
      return nullptr;
    }

    return CreateECPublicKey(point, namedCurve);
  }

  return nullptr;
}

nsresult CryptoKey::PublicKeyToJwk(SECKEYPublicKey* aPubKey,
                                   JsonWebKey& aRetVal) {
  switch (aPubKey->keyType) {
    case rsaKey: {
      CryptoBuffer n, e;
      aRetVal.mN.Construct();
      aRetVal.mE.Construct();

      if (!n.Assign(&aPubKey->u.rsa.modulus) ||
          !e.Assign(&aPubKey->u.rsa.publicExponent) ||
          NS_FAILED(n.ToJwkBase64(aRetVal.mN.Value())) ||
          NS_FAILED(e.ToJwkBase64(aRetVal.mE.Value()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      aRetVal.mKty = NS_LITERAL_STRING_FROM_CSTRING(JWK_TYPE_RSA);
      return NS_OK;
    }
    case edKey:
    case ecMontKey:
      if (!OKPKeyToJwk(&aPubKey->u.ec.DEREncodedParams,
                       &aPubKey->u.ec.publicValue, aRetVal)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
      return NS_OK;
    case ecKey: {
      if (!ECKeyToJwk(PK11_TypePubKey, aPubKey, &aPubKey->u.ec.DEREncodedParams,
                      &aPubKey->u.ec.publicValue, aRetVal)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
      return NS_OK;
    }
    case kyberKey: {
      CryptoBuffer pub;
      aRetVal.mPub.Construct();
      if (NS_FAILED(PublicMLKEMKeyToRaw(aPubKey, pub)) ||
          NS_FAILED(pub.ToJwkBase64(aRetVal.mPub.Value()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      aRetVal.mKty = NS_LITERAL_STRING_FROM_CSTRING(JWK_TYPE_AKP);
      return NS_OK;
    }
    default:
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }
}

bool PublicKeyHasCorrectLengthAndEncoding(const nsString& aNamedCurve,
                                          const SECItem* key) {
  uint32_t flen;
  if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_P256)) {
    flen = 32;  // bytes
  } else if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_P384)) {
    flen = 48;  // bytes
  } else if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_P521)) {
    flen = 66;  // bytes
  } else {
    return false;
  }

  // Here we have 2 possible inputs, either we've received an uncompressed point
  // then the length is 1 + flen (x) + flen (y) and the 0th byte is
  // EC_POINT_FORM_UNCOMPRESSED or we work with the compressed point then the
  // length is 1 + flen (x) and the 0th byte is either
  // EC_POINT_FORM_COMPRESSED_Y0 or EC_POINT_FORM_COMPRESSED_Y1

  bool correctUncompressed = (key->len == 2 * flen + 1) &&
                             (key->data[0] == EC_POINT_FORM_UNCOMPRESSED);
  bool correctCompressed = (key->len == flen + 1) &&
                           ((key->data[0] == EC_POINT_FORM_COMPRESSED_Y0) ||
                            (key->data[0] == EC_POINT_FORM_COMPRESSED_Y1));

  return correctCompressed || correctUncompressed;
}

UniqueSECKEYPublicKey CryptoKey::PublicECKeyFromRaw(
    CryptoBuffer& aKeyData, const nsString& aNamedCurve) {
  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return nullptr;
  }

  SECItem rawItem = {siBuffer, nullptr, 0};
  if (!aKeyData.ToSECItem(arena.get(), &rawItem)) {
    return nullptr;
  }

  if (!PublicKeyHasCorrectLengthAndEncoding(aNamedCurve, &rawItem)) {
    return nullptr;
  }

  return CreateECPublicKey(&rawItem, aNamedCurve);
}

nsresult CryptoKey::PublicECKeyToRaw(SECKEYPublicKey* aPubKey,
                                     CryptoBuffer& aRetVal) {
  if (!aRetVal.Assign(&aPubKey->u.ec.publicValue)) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }
  return NS_OK;
}

UniqueSECKEYPublicKey CryptoKey::PublicOKPKeyFromRaw(
    CryptoBuffer& aKeyData, const nsString& aNamedCurve) {
  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return nullptr;
  }

  SECItem rawItem = {siBuffer, nullptr, 0};
  if (!aKeyData.ToSECItem(arena.get(), &rawItem)) {
    return nullptr;
  }

  uint32_t flen;
  if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_ED25519)) {
    flen = 32;  // bytes
  } else if (aNamedCurve.EqualsLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519)) {
    flen = 32;
  } else {
    return nullptr;
  }

  if (rawItem.len != flen) {
    return nullptr;
  }

  return CreateECPublicKey(&rawItem, aNamedCurve);
}

UniqueSECKEYPublicKey CryptoKey::PublicMLKEMKeyFromRaw(
    CryptoBuffer& aKeyData, const MLKEMParams& aMLKEMParams) {
  if (aKeyData.Length() != aMLKEMParams.mPublicKeyLength) {
    return nullptr;
  }

  if (!EnsureNSSInitializedChromeOrContent()) {
    return nullptr;
  }

  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return nullptr;
  }

  UniqueSECKEYPublicKey key(PORT_ArenaZNew(arena.get(), SECKEYPublicKey));
  if (!key) {
    return nullptr;
  }

  key->arena = arena.release();
  key->keyType = kyberKey;
  key->pkcs11Slot = nullptr;
  key->pkcs11ID = CK_INVALID_HANDLE;
  key->u.kyber.params = aMLKEMParams.mKyberParams;

  if (!aKeyData.ToSECItem(key->arena, &key->u.kyber.publicValue)) {
    return nullptr;
  }

  if (!PublicKeyValid(key.get())) {
    return nullptr;
  }

  return key;
}

nsresult CryptoKey::PublicMLKEMKeyToRaw(SECKEYPublicKey* aPubKey,
                                        CryptoBuffer& aRetVal) {
  if (aPubKey->keyType != kyberKey) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  if (!aRetVal.Assign(&aPubKey->u.kyber.publicValue)) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }
  return NS_OK;
}

UniqueSECKEYPrivateKey CryptoKey::PrivateMLKEMKeyFromSeed(
    const CryptoBuffer& aSeed, const MLKEMParams& aMLKEMParams) {
  if (aSeed.Length() != KYBER_KEYPAIR_COIN_BYTES) {
    return nullptr;
  }

  CK_OBJECT_CLASS privateKeyValue = CKO_PRIVATE_KEY;
  CK_KEY_TYPE mlKemValue = CKK_ML_KEM;
  CK_BBOOL falseValue = CK_FALSE;
  CK_ML_KEM_PARAMETER_SET_TYPE parameterSet = aMLKEMParams.mParameterSet;

  // Softoken derives CKA_VALUE from the seed when it is not supplied.
  CK_ATTRIBUTE keyTemplate[7] = {
      {CKA_CLASS, &privateKeyValue, sizeof(privateKeyValue)},
      {CKA_KEY_TYPE, &mlKemValue, sizeof(mlKemValue)},
      {CKA_TOKEN, &falseValue, sizeof(falseValue)},
      {CKA_SENSITIVE, &falseValue, sizeof(falseValue)},
      {CKA_PRIVATE, &falseValue, sizeof(falseValue)},
      {CKA_PARAMETER_SET, &parameterSet, sizeof(parameterSet)},
      {CKA_SEED, (void*)aSeed.Elements(), (CK_ULONG)aSeed.Length()},
  };

  return PrivateKeyFromPrivateKeyTemplate(keyTemplate, std::size(keyTemplate));
}

nsresult CryptoKey::PrivateMLKEMKeyToSeed(SECKEYPrivateKey* aPrivKey,
                                          CryptoBuffer& aRetVal) {
  ScopedAutoSECItem seed;
  if (PK11_ReadRawAttribute(PK11_TypePrivKey, aPrivKey, CKA_SEED, &seed) !=
      SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  if (seed.len != KYBER_KEYPAIR_COIN_BYTES) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  if (!aRetVal.Assign(&seed)) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  return NS_OK;
}

bool PublicECKeyEncoded(SECKEYPublicKey* aPubKey) {
  if (!aPubKey) {
    return false;
  }

  SECItem* publicValue = &aPubKey->u.ec.publicValue;
  if (!publicValue || !publicValue->data || publicValue->len == 0) {
    return false;
  }

  if (publicValue->data[0] == EC_POINT_FORM_COMPRESSED_Y0 ||
      publicValue->data[0] == EC_POINT_FORM_COMPRESSED_Y1) {
    return true;
  }

  return false;
}

bool CryptoKey::PublicKeyValid(SECKEYPublicKey* aPubKey) {
  UniquePK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot.get()) {
    return false;
  }

  // This assumes that NSS checks the validity of a public key when
  // it is imported into a PKCS#11 module, and returns CK_INVALID_HANDLE
  // if it is invalid.
  CK_OBJECT_HANDLE id = PK11_ImportPublicKey(slot.get(), aPubKey, PR_FALSE);
  if (id == CK_INVALID_HANDLE) {
    return false;
  }

  // It is possible that the public key was in the decompressed form
  // Thus we need to read the attribute to retrieve the key
  if (aPubKey->keyType == ecKey && PublicECKeyEncoded(aPubKey)) {
    ScopedAutoSECItem encodedPublicKey;
    // Independently from whether the key was decompressed or not,
    // the raw attribute is stored encoded.
    SECStatus rv = PK11_ReadRawAttribute(PK11_TypePubKey, aPubKey, CKA_EC_POINT,
                                         &encodedPublicKey);
    if (NS_WARN_IF(rv != SECSuccess)) {
      return false;
    }

    SECItem decoded;
    rv = SEC_QuickDERDecodeItem(aPubKey->arena, &decoded,
                                SEC_ASN1_GET(SEC_OctetStringTemplate),
                                &encodedPublicKey);
    if (NS_WARN_IF(rv != SECSuccess)) {
      return false;
    }

    // Updating the public key
    rv = SECITEM_CopyItem(aPubKey->arena, &aPubKey->u.ec.publicValue, &decoded);
    if (NS_WARN_IF(rv != SECSuccess)) {
      return false;
    }
  }

  return true;
}

bool CryptoKey::WriteStructuredClone(JSContext* aCX,
                                     JSStructuredCloneWriter* aWriter) const {
  // Write in five pieces
  // 1. Attributes
  // 2. Symmetric key as raw (if present)
  // 3. Private key as pkcs8, or as its seed for ML-KEM (if present)
  // 4. Public key as spki (if present)
  // 5. Algorithm in whatever form it chooses
  CryptoBuffer priv, pub;

  if (mPrivateKey) {
    if (mPrivateKey->keyType == kyberKey) {
      if (NS_FAILED(
              CryptoKey::PrivateMLKEMKeyToSeed(mPrivateKey.get(), priv))) {
        return false;
      }
    } else if (NS_FAILED(
                   CryptoKey::PrivateKeyToPkcs8(mPrivateKey.get(), priv))) {
      return false;
    }
  }

  if (mPublicKey) {
    if (NS_FAILED(CryptoKey::PublicKeyToSpki(mPublicKey.get(), pub))) {
      return false;
    }
  }

  return JS_WriteUint32Pair(aWriter, mAttributes, CRYPTOKEY_SC_VERSION) &&
         WriteBuffer(aWriter, mSymKey) && WriteBuffer(aWriter, priv) &&
         WriteBuffer(aWriter, pub) && mAlgorithm.WriteStructuredClone(aWriter);
}

// static
already_AddRefed<CryptoKey> CryptoKey::ReadStructuredClone(
    JSContext* aCx, nsIGlobalObject* aGlobal,
    JSStructuredCloneReader* aReader) {
  // Ensure that NSS is initialized.
  if (!EnsureNSSInitializedChromeOrContent()) {
    return nullptr;
  }

  RefPtr<CryptoKey> key = new CryptoKey(aGlobal);

  uint32_t version;
  CryptoBuffer sym, priv, pub;

  bool read = JS_ReadUint32Pair(aReader, &key->mAttributes, &version) &&
              (version == CRYPTOKEY_SC_VERSION) && ReadBuffer(aReader, sym) &&
              ReadBuffer(aReader, priv) && ReadBuffer(aReader, pub) &&
              key->mAlgorithm.ReadStructuredClone(aReader);
  if (!read) {
    return nullptr;
  }

  if (sym.Length() > 0 && !key->mSymKey.Assign(sym)) {
    return nullptr;
  }
  if (priv.Length() > 0) {
    MLKEMParams mlKemParams;
    if (GetMLKEMParams(key->mAlgorithm.mName, mlKemParams)) {
      key->mPrivateKey = CryptoKey::PrivateMLKEMKeyFromSeed(priv, mlKemParams);
    } else {
      key->mPrivateKey = CryptoKey::PrivateKeyFromPkcs8(priv);
    }
  }
  if (pub.Length() > 0) {
    key->mPublicKey = CryptoKey::PublicKeyFromSpki(pub);
  }

  // Ensure that what we've read is consistent
  // If the attributes indicate a key type, should have a key of that type
  if (!((key->GetKeyType() == SECRET && key->mSymKey.Length() > 0) ||
        (key->GetKeyType() == PRIVATE && key->mPrivateKey) ||
        (key->GetKeyType() == PUBLIC && key->mPublicKey))) {
    return nullptr;
  }

  return key.forget();
}

}  // namespace mozilla::dom
