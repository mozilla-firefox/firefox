/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/RTCCertificate.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "ErrorList.h"
#include "MainThreadUtils.h"
#include "cert.h"
#include "cryptohi.h"
#include "js/StructuredClone.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "keyhi.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/CryptoBuffer.h"
#include "mozilla/dom/CryptoKey.h"
#include "mozilla/dom/KeyAlgorithmBinding.h"
#include "mozilla/dom/KeyAlgorithmProxy.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RTCCertificateBinding.h"
#include "mozilla/dom/StructuredCloneHolder.h"
#include "mozilla/dom/SubtleCryptoBinding.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/WebCryptoCommon.h"
#include "mozilla/dom/WebCryptoTask.h"
#include "mozilla/fallible.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsLiteralString.h"
#include "nsStringFlags.h"
#include "nsStringFwd.h"
#include "nsTLiteralString.h"
#include "pk11pub.h"
#include "plarena.h"
#include "sdp/SdpAttribute.h"
#include "secasn1.h"
#include "secasn1t.h"
#include "seccomon.h"
#include "secmodt.h"
#include "secoid.h"
#include "secoidt.h"
#include "transport/dtlsidentity.h"
#include "xpcpublic.h"

namespace mozilla::dom {

#define RTCCERTIFICATE_SC_VERSION 0x00000001

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(RTCCertificate, mGlobal)
NS_IMPL_CYCLE_COLLECTING_ADDREF(RTCCertificate)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RTCCertificate)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RTCCertificate)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// Note: explicit casts necessary to avoid
//       warning C4307: '*' : integral constant overflow
constexpr PRTime ONE_DAY_USEC = PRTime(PR_USEC_PER_SEC) * PRTime(60) /*sec*/
                                * PRTime(60) /*min*/ * PRTime(24) /*hours*/;
constexpr PRTime EXPIRATION_DEFAULT_USEC = ONE_DAY_USEC * PRTime(30);
constexpr PRTime EXPIRATION_SLACK_USEC = ONE_DAY_USEC;
constexpr PRTime EXPIRATION_MAX_USEC = ONE_DAY_USEC * PRTime(365) /*year*/;

const size_t RTCCertificateCommonNameLength = 16;
const size_t RTCCertificateMinRsaSize = 1024;

class GenerateRTCCertificateTask : public GenerateAsymmetricKeyTask {
 public:
  GenerateRTCCertificateTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                             const ObjectOrString& aAlgorithm,
                             const Sequence<nsString>& aKeyUsages,
                             PRTime aExpires)
      : GenerateAsymmetricKeyTask(aGlobal, aCx, aAlgorithm, true, aKeyUsages),
        mExpires(aExpires),
        mAuthType(ssl_kea_null),
        mCertificate(nullptr),
        mSignatureAlg(SEC_OID_UNKNOWN) {
    if (NS_FAILED(mEarlyRv)) {
      // webrtc-pc says to throw NotSupportedError if we have passed "an
      // algorithm that the user agent cannot or will not use to generate a
      // certificate". This catches these cases.
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }
  }

 private:
  PRTime mExpires;
  SSLKEAType mAuthType;
  UniqueCERTCertificate mCertificate;
  SECOidTag mSignatureAlg;

  static CERTName* GenerateRandomName(PK11SlotInfo* aSlot) {
    uint8_t randomName[RTCCertificateCommonNameLength];
    SECStatus rv =
        PK11_GenerateRandomOnSlot(aSlot, randomName, sizeof(randomName));
    if (rv != SECSuccess) {
      return nullptr;
    }

    char buf[sizeof(randomName) * 2 + 4];
    strncpy(buf, "CN=", 4);
    for (size_t i = 0; i < sizeof(randomName); ++i) {
      snprintf(&buf[i * 2 + 3], 3, "%.2x", randomName[i]);
    }
    buf[sizeof(buf) - 1] = '\0';

    return CERT_AsciiToName(buf);
  }

  nsresult GenerateCertificate() {
    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    MOZ_ASSERT(slot.get());

    UniqueCERTName subjectName(GenerateRandomName(slot.get()));
    if (!subjectName) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    UniqueSECKEYPublicKey publicKey(mKeyPair->mPublicKey->GetPublicKey());
    UniqueCERTSubjectPublicKeyInfo spki(
        SECKEY_CreateSubjectPublicKeyInfo(publicKey.get()));
    if (!spki) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    UniqueCERTCertificateRequest certreq(
        CERT_CreateCertificateRequest(subjectName.get(), spki.get(), nullptr));
    if (!certreq) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    const PRTime now = PR_Now();
    const PRTime notBefore = now - EXPIRATION_SLACK_USEC;
    mExpires += now;

    UniqueCERTValidity validity(CERT_CreateValidity(notBefore, mExpires));
    if (!validity) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    unsigned long serial;
    // Note: This serial in principle could collide, but it's unlikely, and we
    // don't expect anyone to be validating certificates anyway.
    SECStatus rv = PK11_GenerateRandomOnSlot(
        slot.get(), reinterpret_cast<unsigned char*>(&serial), sizeof(serial));
    if (rv != SECSuccess) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    // NB: CERTCertificates created with CERT_CreateCertificate are not safe to
    // use with other NSS functions like CERT_DupCertificate.  The strategy
    // here is to create a tbsCertificate ("to-be-signed certificate"), encode
    // it, and sign it, resulting in a signed DER certificate that can be
    // decoded into a CERTCertificate.
    UniqueCERTCertificate tbsCertificate(CERT_CreateCertificate(
        serial, subjectName.get(), validity.get(), certreq.get()));
    if (!tbsCertificate) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    MOZ_ASSERT(mSignatureAlg != SEC_OID_UNKNOWN);
    PLArenaPool* arena = tbsCertificate->arena;

    rv = SECOID_SetAlgorithmID(arena, &tbsCertificate->signature, mSignatureAlg,
                               nullptr);
    if (rv != SECSuccess) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    // Set version to X509v3.
    *(tbsCertificate->version.data) = SEC_CERTIFICATE_VERSION_3;
    tbsCertificate->version.len = 1;

    SECItem innerDER = {siBuffer, nullptr, 0};
    if (!SEC_ASN1EncodeItem(arena, &innerDER, tbsCertificate.get(),
                            SEC_ASN1_GET(CERT_CertificateTemplate))) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    SECItem* certDer = PORT_ArenaZNew(arena, SECItem);
    if (!certDer) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    UniqueSECKEYPrivateKey privateKey(mKeyPair->mPrivateKey->GetPrivateKey());
    rv = SEC_DerSignData(arena, certDer, innerDER.data, innerDER.len,
                         privateKey.get(), mSignatureAlg);
    if (rv != SECSuccess) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    mCertificate.reset(CERT_NewTempCertificate(CERT_GetDefaultCertDB(), certDer,
                                               nullptr, false, true));
    if (!mCertificate) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }
    return NS_OK;
  }

  nsresult BeforeCrypto() override {
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1)) {
      // Double check that size is OK.
      auto sz = static_cast<size_t>(mRsaParams.keySizeInBits);
      if (sz < RTCCertificateMinRsaSize) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      KeyAlgorithmProxy& alg = mKeyPair->mPublicKey->Algorithm();
      if (alg.mType != KeyAlgorithmProxy::RSA ||
          !alg.mRsa.mHash.mName.EqualsLiteral(WEBCRYPTO_ALG_SHA256)) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      mSignatureAlg = SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION;
      mAuthType = ssl_kea_rsa;

    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
      // We only support good curves in WebCrypto.
      // If that ever changes, check that a good one was chosen.

      mSignatureAlg = SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE;
      mAuthType = ssl_kea_ecdh;
    } else {
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }
    return NS_OK;
  }

  nsresult DoCrypto() override {
    nsresult rv = GenerateAsymmetricKeyTask::DoCrypto();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GenerateCertificate();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  virtual void Resolve() override {
    // Make copies of the private key and certificate, otherwise, when this
    // object is deleted, the structures they reference will be deleted too.
    UniqueSECKEYPrivateKey key = mKeyPair->mPrivateKey->GetPrivateKey();
    CERTCertificate* cert = CERT_DupCertificate(mCertificate.get());
    RefPtr<RTCCertificate> result =
        new RTCCertificate(mResultPromise->GetParentObject(), key.release(),
                           cert, mAuthType, mExpires);
    mResultPromise->MaybeResolve(result);
  }
};

static PRTime ReadExpires(JSContext* aCx, const ObjectOrString& aOptions,
                          ErrorResult& aRv) {
  // This conversion might fail, but we don't really care; use the default.
  // If this isn't an object, or it doesn't coerce into the right type,
  // then we won't get the |expires| value.  Either will be caught later.
  RTCCertificateExpiration expiration;
  if (!aOptions.IsObject()) {
    return EXPIRATION_DEFAULT_USEC;
  }
  JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*aOptions.GetAsObject()));
  if (!expiration.Init(aCx, value)) {
    aRv.NoteJSContextException(aCx);
    return 0;
  }

  if (!expiration.mExpires.WasPassed()) {
    return EXPIRATION_DEFAULT_USEC;
  }
  static const uint64_t max =
      static_cast<uint64_t>(EXPIRATION_MAX_USEC / PR_USEC_PER_MSEC);
  if (expiration.mExpires.Value() > max) {
    return EXPIRATION_MAX_USEC;
  }
  return static_cast<PRTime>(expiration.mExpires.Value() * PR_USEC_PER_MSEC);
}

already_AddRefed<Promise> RTCCertificate::GenerateCertificate(
    const GlobalObject& aGlobal, const ObjectOrString& aOptions,
    ErrorResult& aRv, JS::Compartment* aCompartment) {
  nsIGlobalObject* global = xpc::NativeGlobal(aGlobal.Get());
  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  Sequence<nsString> usages;
  if (!usages.AppendElement(u"sign"_ns, fallible)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  PRTime expires = ReadExpires(aGlobal.Context(), aOptions, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  RefPtr task = MakeRefPtr<GenerateRTCCertificateTask>(
      global, aGlobal.Context(), aOptions, usages, expires);
  task->DispatchWithPromise(p);
  return p.forget();
}

RTCCertificate::RTCCertificate(nsIGlobalObject* aGlobal)
    : mGlobal(aGlobal),
      mPrivateKey(nullptr),
      mCertificate(nullptr),
      mAuthType(ssl_kea_null),
      mExpires(0) {}

RTCCertificate::RTCCertificate(nsIGlobalObject* aGlobal,
                               SECKEYPrivateKey* aPrivateKey,
                               CERTCertificate* aCertificate,
                               SSLKEAType aAuthType, PRTime aExpires)
    : mGlobal(aGlobal),
      mPrivateKey(aPrivateKey),
      mCertificate(aCertificate),
      mAuthType(aAuthType),
      mExpires(aExpires) {}

void RTCCertificate::GetFingerprints(
    nsTArray<dom::RTCDtlsFingerprint>& aFingerprintsOut) {
  // if we have a cert and haven't already built the fingerprints
  if (mCertificate && mFingerprints.Length() == 0) {
    DtlsDigest digest(DtlsIdentity::DEFAULT_HASH_ALGORITHM);
    nsresult rv = DtlsIdentity::ComputeFingerprint(mCertificate, &digest);
    if (NS_FAILED(rv)) {
      // Safe to return early here since we didn't already have fingerprints
      // and the call to DtlsIdentity::ComputeFingerprint failed.
      return;
    }
    RTCDtlsFingerprint fingerprint;
    fingerprint.mAlgorithm.Construct(NS_ConvertASCIItoUTF16(digest.algorithm_));

    std::string value =
        SdpFingerprintAttributeList::FormatFingerprint(digest.value_);
    // Sadly, the SDP fingerprint is expected to be all uppercase hex,
    // while the RTC fingerprint is expected to be all lowercase hex.
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    fingerprint.mValue.Construct(NS_ConvertASCIItoUTF16(value));

    mFingerprints.AppendElement(fingerprint);
  }

  aFingerprintsOut = mFingerprints.Clone();
}

RefPtr<DtlsIdentity> RTCCertificate::CreateDtlsIdentity() const {
  if (!mPrivateKey || !mCertificate) {
    return nullptr;
  }
  UniqueSECKEYPrivateKey key(SECKEY_CopyPrivateKey(mPrivateKey.get()));
  UniqueCERTCertificate cert(CERT_DupCertificate(mCertificate.get()));
  RefPtr id =
      MakeRefPtr<DtlsIdentity>(std::move(key), std::move(cert), mAuthType);
  return id;
}

JSObject* RTCCertificate::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return RTCCertificate_Binding::Wrap(aCx, this, aGivenProto);
}

bool RTCCertificate::WritePrivateKey(JSStructuredCloneWriter* aWriter) const {
  JsonWebKey jwk;
  nsresult rv = CryptoKey::PrivateKeyToJwk(mPrivateKey.get(), jwk);
  if (NS_FAILED(rv)) {
    return false;
  }
  nsString json;
  if (!jwk.ToJSON(json)) {
    return false;
  }
  return StructuredCloneHolder::WriteString(aWriter, json);
}

bool RTCCertificate::WriteCertificate(JSStructuredCloneWriter* aWriter) const {
  UniqueCERTCertificateList certs(CERT_CertListFromCert(mCertificate.get()));
  if (!certs || certs->len <= 0) {
    return false;
  }
  if (!JS_WriteUint32Pair(aWriter, certs->certs[0].len, 0)) {
    return false;
  }
  return JS_WriteBytes(aWriter, certs->certs[0].data, certs->certs[0].len);
}

bool RTCCertificate::WriteStructuredClone(
    JSContext* aCx, JSStructuredCloneWriter* aWriter) const {
  if (!mPrivateKey || !mCertificate) {
    return false;
  }

  return JS_WriteUint32Pair(aWriter, RTCCERTIFICATE_SC_VERSION, mAuthType) &&
         JS_WriteUint32Pair(aWriter, (mExpires >> 32) & 0xffffffff,
                            mExpires & 0xffffffff) &&
         WritePrivateKey(aWriter) && WriteCertificate(aWriter);
}

bool RTCCertificate::ReadPrivateKey(JSStructuredCloneReader* aReader) {
  nsString json;
  if (!StructuredCloneHolder::ReadString(aReader, json)) {
    return false;
  }
  JsonWebKey jwk;
  if (!jwk.Init(json)) {
    return false;
  }
  mPrivateKey = CryptoKey::PrivateKeyFromJwk(jwk);
  return !!mPrivateKey;
}

bool RTCCertificate::ReadCertificate(JSStructuredCloneReader* aReader) {
  CryptoBuffer cert;
  if (!ReadBuffer(aReader, cert) || cert.Length() == 0) {
    return false;
  }

  SECItem der = {siBuffer, cert.Elements(),
                 static_cast<unsigned int>(cert.Length())};
  mCertificate.reset(CERT_NewTempCertificate(CERT_GetDefaultCertDB(), &der,
                                             nullptr, true, true));
  return !!mCertificate;
}

// static
already_AddRefed<RTCCertificate> RTCCertificate::ReadStructuredClone(
    JSContext* aCx, nsIGlobalObject* aGlobal,
    JSStructuredCloneReader* aReader) {
  if (!NS_IsMainThread()) {
    // These objects are mainthread-only.
    return nullptr;
  }
  uint32_t version, authType;
  if (!JS_ReadUint32Pair(aReader, &version, &authType) ||
      version != RTCCERTIFICATE_SC_VERSION) {
    return nullptr;
  }
  RefPtr<RTCCertificate> cert = new RTCCertificate(aGlobal);
  if (authType == ssl_kea_null || authType >= ssl_kea_size) {
    return nullptr;
  }
  cert->mAuthType = static_cast<SSLKEAType>(authType);

  uint32_t high, low;
  if (!JS_ReadUint32Pair(aReader, &high, &low)) {
    return nullptr;
  }
  cert->mExpires = static_cast<PRTime>(high) << 32 | low;
  // make sure mExpires is not more than EXPIRATION_MAX_USEC from now
  const PRTime now = PR_Now();
  if (cert->mExpires <= now || cert->mExpires - now > EXPIRATION_MAX_USEC) {
    return nullptr;
  }

  if (!cert->ReadPrivateKey(aReader) || !cert->ReadCertificate(aReader)) {
    return nullptr;
  }

  return cert.forget();
}

}  // namespace mozilla::dom
