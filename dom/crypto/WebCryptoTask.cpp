/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WebCryptoTask.h"

#include "cryptohi.h"
#include "jsapi.h"
#include "mozilla/Utf8.h"
#include "mozilla/dom/CryptoBuffer.h"
#include "mozilla/dom/CryptoKey.h"
#include "mozilla/dom/KeyAlgorithmProxy.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/WebCryptoCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "nsNSSComponent.h"
#include "nsProxyRelease.h"
#include "pk11pub.h"
#include "secerr.h"

// Template taken from security/nss/lib/util/templates.c
// This (or SGN_EncodeDigestInfo) would ideally be exported
// by NSS and until that happens we have to keep our own copy.
MOZ_GLOBINIT const SEC_ASN1Template SGN_DigestInfoTemplate[] = {
    {SEC_ASN1_SEQUENCE, 0, nullptr, sizeof(SGNDigestInfo)},
    {SEC_ASN1_INLINE, offsetof(SGNDigestInfo, digestAlgorithm),
     SEC_ASN1_GET(SECOID_AlgorithmIDTemplate)},
    {SEC_ASN1_OCTET_STRING, offsetof(SGNDigestInfo, digest)},
    {
        0,
    }};

namespace mozilla::dom {

// Convenience functions for extracting / converting information

// OOM-safe CryptoBuffer initialization, suitable for constructors
#define ATTEMPT_BUFFER_INIT(dst, src)    \
  if (!dst.Assign(src)) {                \
    mEarlyRv = NS_ERROR_DOM_UNKNOWN_ERR; \
    return;                              \
  }

// OOM-safe CryptoBuffer-to-SECItem copy, suitable for DoCrypto
#define ATTEMPT_BUFFER_TO_SECITEM(arena, dst, src) \
  if (!src.ToSECItem(arena, dst)) {                \
    return NS_ERROR_DOM_UNKNOWN_ERR;               \
  }

// OOM-safe CryptoBuffer copy, suitable for DoCrypto
#define ATTEMPT_BUFFER_ASSIGN(dst, src) \
  if (!dst.Assign(src)) {               \
    return NS_ERROR_DOM_UNKNOWN_ERR;    \
  }

// Safety check for algorithms that use keys, suitable for constructors
#define CHECK_KEY_ALGORITHM(keyAlg, algName)         \
  {                                                  \
    if (!NORMALIZED_EQUALS(keyAlg.mName, algName)) { \
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;    \
      return;                                        \
    }                                                \
  }

class ClearException {
 public:
  explicit ClearException(JSContext* aCx) : mCx(aCx) {}

  ~ClearException() { JS_ClearPendingException(mCx); }

 private:
  JSContext* mCx;
};

template <class OOS>
static nsresult GetAlgorithmName(JSContext* aCx, const OOS& aAlgorithm,
                                 nsString& aName) {
  ClearException ce(aCx);

  if (aAlgorithm.IsString()) {
    // If string, then treat as algorithm name
    aName.Assign(aAlgorithm.GetAsString());
  } else {
    // Coerce to algorithm and extract name
    JS::Rooted<JS::Value> value(aCx,
                                JS::ObjectValue(*aAlgorithm.GetAsObject()));
    Algorithm alg;

    if (!alg.Init(aCx, value)) {
      return NS_ERROR_DOM_TYPE_MISMATCH_ERR;
    }

    aName = alg.mName;
  }

  if (!NormalizeToken(aName, aName)) {
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  return NS_OK;
}

template <class T, class OOS>
static nsresult Coerce(JSContext* aCx, T& aTarget, const OOS& aAlgorithm) {
  ClearException ce(aCx);

  if (!aAlgorithm.IsObject()) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*aAlgorithm.GetAsObject()));
  if (!aTarget.Init(aCx, value)) {
    return NS_ERROR_DOM_TYPE_MISMATCH_ERR;
  }

  return NS_OK;
}

inline size_t MapHashAlgorithmNameToBlockSize(const nsString& aName) {
  if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA1) ||
      aName.EqualsLiteral(WEBCRYPTO_ALG_SHA256)) {
    return 512;
  }

  if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA384) ||
      aName.EqualsLiteral(WEBCRYPTO_ALG_SHA512)) {
    return 1024;
  }

  return 0;
}

inline nsresult GetKeyLengthForAlgorithmIfSpecified(
    JSContext* aCx, const nsString& aAlgName, const ObjectOrString& aAlgorithm,
    Maybe<size_t>& aLength) {
  // Read AES key length from given algorithm object.
  if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
      aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
      aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM) ||
      aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW)) {
    RootedDictionary<AesDerivedKeyParams> params(aCx);
    nsresult rv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (params.mLength != 128 && params.mLength != 192 &&
        params.mLength != 256) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    aLength.emplace(params.mLength);
    return NS_OK;
  }

  // Read HMAC key length from given algorithm object or
  // determine key length as the block size of the given hash.
  if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_HMAC)) {
    RootedDictionary<HmacDerivedKeyParams> params(aCx);
    nsresult rv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Return the passed length, if any.
    if (params.mLength.WasPassed()) {
      aLength.emplace(params.mLength.Value());
      return NS_OK;
    }

    nsString hashName;
    if (NS_FAILED(GetAlgorithmName(aCx, params.mHash, hashName))) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    // Return the given hash algorithm's block size as the key length.
    size_t blockSize = MapHashAlgorithmNameToBlockSize(hashName);
    if (blockSize == 0) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    aLength.emplace(blockSize);
    return NS_OK;
  }

  return NS_OK;
}

inline nsresult GetKeyLengthForAlgorithm(JSContext* aCx,
                                         const nsString& aAlgName,
                                         const ObjectOrString& aAlgorithm,
                                         size_t& aLength) {
  Maybe<size_t> length;
  nsresult rv =
      GetKeyLengthForAlgorithmIfSpecified(aCx, aAlgName, aAlgorithm, length);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (length.isNothing()) {
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }
  aLength = *length;
  return NS_OK;
}

inline bool MapOIDTagToNamedCurve(SECOidTag aOIDTag, nsString& aResult) {
  switch (aOIDTag) {
    case SEC_OID_SECG_EC_SECP256R1:
      aResult.AssignLiteral(WEBCRYPTO_NAMED_CURVE_P256);
      break;
    case SEC_OID_SECG_EC_SECP384R1:
      aResult.AssignLiteral(WEBCRYPTO_NAMED_CURVE_P384);
      break;
    case SEC_OID_SECG_EC_SECP521R1:
      aResult.AssignLiteral(WEBCRYPTO_NAMED_CURVE_P521);
      break;
    case SEC_OID_ED25519_PUBLIC_KEY:
      aResult.AssignLiteral(WEBCRYPTO_NAMED_CURVE_ED25519);
      break;
    case SEC_OID_X25519:
      aResult.AssignLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519);
      break;
    default:
      return false;
  }

  return true;
}

inline SECOidTag MapHashAlgorithmNameToOID(const nsString& aName) {
  SECOidTag hashOID(SEC_OID_UNKNOWN);

  if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA1)) {
    hashOID = SEC_OID_SHA1;
  } else if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA256)) {
    hashOID = SEC_OID_SHA256;
  } else if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA384)) {
    hashOID = SEC_OID_SHA384;
  } else if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA512)) {
    hashOID = SEC_OID_SHA512;
  }

  return hashOID;
}

inline CK_MECHANISM_TYPE MapHashAlgorithmNameToMgfMechanism(
    const nsString& aName) {
  CK_MECHANISM_TYPE mech(UNKNOWN_CK_MECHANISM);

  if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA1)) {
    mech = CKG_MGF1_SHA1;
  } else if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA256)) {
    mech = CKG_MGF1_SHA256;
  } else if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA384)) {
    mech = CKG_MGF1_SHA384;
  } else if (aName.EqualsLiteral(WEBCRYPTO_ALG_SHA512)) {
    mech = CKG_MGF1_SHA512;
  }

  return mech;
}

// Implementation of WebCryptoTask methods

void WebCryptoTask::DispatchWithPromise(Promise* aResultPromise) {
  mResultPromise = aResultPromise;

  // Fail if an error was set during the constructor
  MAYBE_EARLY_FAIL(mEarlyRv)

  // Perform pre-NSS operations, and fail if they fail
  mEarlyRv = BeforeCrypto();
  MAYBE_EARLY_FAIL(mEarlyRv)

  // Skip dispatch if we're already done. Otherwise launch a CryptoTask
  if (mEarlyComplete) {
    CallCallback(mEarlyRv);
    return;
  }

  // Store calling thread
  mOriginalEventTarget = GetCurrentSerialEventTarget();

  // If we are running on a worker thread we must hold the worker
  // alive while we work on the thread pool.  Otherwise the worker
  // private may get torn down before we dispatch back to complete
  // the transaction.
  if (!NS_IsMainThread()) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    RefPtr<StrongWorkerRef> workerRef =
        StrongWorkerRef::Create(workerPrivate, "WebCryptoTask");
    if (NS_WARN_IF(!workerRef)) {
      mEarlyRv = NS_BINDING_ABORTED;
    } else {
      mWorkerRef = MakeRefPtr<ThreadSafeWorkerRef>(workerRef);
    }
  }
  MAYBE_EARLY_FAIL(mEarlyRv);

  // dispatch to thread pool

  if (!EnsureNSSInitializedChromeOrContent()) {
    mEarlyRv = NS_ERROR_FAILURE;
  }
  MAYBE_EARLY_FAIL(mEarlyRv);

  mEarlyRv = NS_DispatchBackgroundTask(this);
  MAYBE_EARLY_FAIL(mEarlyRv)
}

NS_IMETHODIMP
WebCryptoTask::Run() {
  // Run heavy crypto operations on the thread pool, off the original thread.
  if (!IsOnOriginalThread()) {
    mRv = CalculateResult();

    // Back to the original thread, i.e. continue below.
    mOriginalEventTarget->Dispatch(this, NS_DISPATCH_NORMAL);
    return NS_OK;
  }

  // We're now back on the calling thread.
  CallCallback(mRv);

  // Stop holding the worker thread alive now that the async work has
  // been completed.
  mWorkerRef = nullptr;

  return NS_OK;
}

nsresult WebCryptoTask::Cancel() {
  MOZ_ASSERT(IsOnOriginalThread());
  FailWithError(NS_BINDING_ABORTED);
  return NS_OK;
}

void WebCryptoTask::FailWithError(nsresult aRv) {
  MOZ_ASSERT(IsOnOriginalThread());

  if (aRv == NS_ERROR_DOM_TYPE_MISMATCH_ERR) {
    mResultPromise->MaybeRejectWithTypeError(
        "The operation could not be performed.");
  } else {
    // Blindly convert nsresult to DOMException
    // Individual tasks must ensure they pass the right values
    mResultPromise->MaybeReject(aRv);
  }
  // Manually release mResultPromise while we're on the main thread
  mResultPromise = nullptr;
  mWorkerRef = nullptr;
  Cleanup();
}

nsresult WebCryptoTask::CalculateResult() {
  MOZ_ASSERT(!IsOnOriginalThread());

  return DoCrypto();
}

void WebCryptoTask::CallCallback(nsresult rv) {
  MOZ_ASSERT(IsOnOriginalThread());
  if (NS_FAILED(rv)) {
    FailWithError(rv);
    return;
  }

  nsresult rv2 = AfterCrypto();
  if (NS_FAILED(rv2)) {
    FailWithError(rv2);
    return;
  }

  Resolve();
  // Manually release mResultPromise while we're on the main thread
  mResultPromise = nullptr;
  Cleanup();
}

// Some generic utility classes

class FailureTask : public WebCryptoTask {
 public:
  explicit FailureTask(nsresult aRv) { mEarlyRv = aRv; }
};

class ReturnArrayBufferViewTask : public WebCryptoTask {
 protected:
  CryptoBuffer mResult;

 private:
  // Returns mResult as an ArrayBufferView, or an error
  virtual void Resolve() override {
    TypedArrayCreator<ArrayBuffer> ret(mResult);
    mResultPromise->MaybeResolve(ret);
  }
};

class DeferredData {
 public:
  template <class T>
  void SetData(const T& aData) {
    mDataIsSet = mData.Assign(aData);
  }

 protected:
  DeferredData() : mDataIsSet(false) {}

  CryptoBuffer mData;
  bool mDataIsSet;
};

class AesTask : public ReturnArrayBufferViewTask, public DeferredData {
 public:
  AesTask(JSContext* aCx, const nsString& aAlgName,
          const ObjectOrString& aAlgorithm, CryptoKey& aKey, bool aEncrypt)
      : mMechanism(CKM_INVALID_MECHANISM),
        mTagLength(0),
        mCounterLength(0),
        mEncrypt(aEncrypt) {
    Init(aCx, aAlgName, aAlgorithm, aKey, aEncrypt);
  }

  AesTask(JSContext* aCx, const nsString& aAlgName,
          const ObjectOrString& aAlgorithm, CryptoKey& aKey,
          const CryptoOperationData& aData, bool aEncrypt)
      : mMechanism(CKM_INVALID_MECHANISM),
        mTagLength(0),
        mCounterLength(0),
        mEncrypt(aEncrypt) {
    Init(aCx, aAlgName, aAlgorithm, aKey, aEncrypt);
    SetData(aData);
  }

  void Init(JSContext* aCx, const nsString& aAlgName,
            const ObjectOrString& aAlgorithm, CryptoKey& aKey, bool aEncrypt) {
    if (!mSymKey.Assign(aKey.GetSymKey())) {
      mEarlyRv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }

    // Check that we got a reasonable key
    if ((mSymKey.Length() != 16) && (mSymKey.Length() != 24) &&
        (mSymKey.Length() != 32)) {
      mEarlyRv = NS_ERROR_DOM_DATA_ERR;
      return;
    }

    // Cache parameters depending on the specific algorithm
    if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC)) {
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_AES_CBC);

      mMechanism = CKM_AES_CBC_PAD;
      RootedDictionary<AesCbcParams> params(aCx);
      nsresult rv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(rv)) {
        mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
        return;
      }

      ATTEMPT_BUFFER_INIT(mIv, params.mIv)
      if (mIv.Length() != 16) {
        mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
        return;
      }
    } else if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR)) {
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_AES_CTR);

      mMechanism = CKM_AES_CTR;
      RootedDictionary<AesCtrParams> params(aCx);
      nsresult rv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(rv)) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }

      ATTEMPT_BUFFER_INIT(mIv, params.mCounter)
      if (mIv.Length() != 16) {
        mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
        return;
      }

      mCounterLength = params.mLength;
    } else if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM)) {
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_AES_GCM);

      mMechanism = CKM_AES_GCM;
      RootedDictionary<AesGcmParams> params(aCx);
      nsresult rv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(rv)) {
        mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
        return;
      }

      ATTEMPT_BUFFER_INIT(mIv, params.mIv)

      if (params.mAdditionalData.WasPassed()) {
        ATTEMPT_BUFFER_INIT(mAad, params.mAdditionalData.Value())
      }

      // 32, 64, 96, 104, 112, 120 or 128
      mTagLength = 128;
      if (params.mTagLength.WasPassed()) {
        mTagLength = params.mTagLength.Value();
        if ((mTagLength > 128) ||
            !(mTagLength == 32 || mTagLength == 64 ||
              (mTagLength >= 96 && mTagLength % 8 == 0))) {
          mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
          return;
        }
      }
    } else {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }
  }

 private:
  CK_MECHANISM_TYPE mMechanism;
  CryptoBuffer mSymKey;
  CryptoBuffer mIv;   // Initialization vector
  CryptoBuffer mAad;  // Additional Authenticated Data
  uint8_t mTagLength;
  uint8_t mCounterLength;
  bool mEncrypt;

  virtual nsresult DoCrypto() override {
    nsresult rv;

    if (!mDataIsSet) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Construct the parameters object depending on algorithm
    SECItem param = {siBuffer, nullptr, 0};
    CK_AES_CTR_PARAMS ctrParams;
    CK_GCM_PARAMS gcmParams;
    switch (mMechanism) {
      case CKM_AES_CBC_PAD:
        ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &param, mIv);
        break;
      case CKM_AES_CTR:
        ctrParams.ulCounterBits = mCounterLength;
        MOZ_ASSERT(mIv.Length() == 16);
        memcpy(&ctrParams.cb, mIv.Elements(), 16);
        param.type = siBuffer;
        param.data = (unsigned char*)&ctrParams;
        param.len = sizeof(ctrParams);
        break;
      case CKM_AES_GCM:
        gcmParams.pIv = mIv.Elements();
        gcmParams.ulIvLen = mIv.Length();
        gcmParams.ulIvBits = gcmParams.ulIvLen * 8;
        gcmParams.pAAD = mAad.Elements();
        gcmParams.ulAADLen = mAad.Length();
        gcmParams.ulTagBits = mTagLength;
        param.type = siBuffer;
        param.data = (unsigned char*)&gcmParams;
        param.len = sizeof(gcmParams);
        break;
      default:
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }

    // Import the key
    SECItem keyItem = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &keyItem, mSymKey);
    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    MOZ_ASSERT(slot.get());
    UniquePK11SymKey symKey(PK11_ImportSymKey(slot.get(), mMechanism,
                                              PK11_OriginUnwrap, CKA_ENCRYPT,
                                              &keyItem, nullptr));
    if (!symKey) {
      return NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }

    // Check whether the integer addition would overflow.
    if (std::numeric_limits<CryptoBuffer::size_type>::max() - 16 <
        mData.Length()) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    // Initialize the output buffer (enough space for padding / a full tag)
    if (!mResult.SetLength(mData.Length() + 16, fallible)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    uint32_t outLen = 0;

    // Perform the encryption/decryption
    if (mEncrypt) {
      rv = MapSECStatus(PK11_Encrypt(
          symKey.get(), mMechanism, &param, mResult.Elements(), &outLen,
          mResult.Length(), mData.Elements(), mData.Length()));
    } else {
      rv = MapSECStatus(PK11_Decrypt(
          symKey.get(), mMechanism, &param, mResult.Elements(), &outLen,
          mResult.Length(), mData.Elements(), mData.Length()));
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);

    mResult.TruncateLength(outLen);
    return rv;
  }
};

// This class looks like an encrypt/decrypt task, like AesTask,
// but it is only exposed to wrapKey/unwrapKey, not encrypt/decrypt
class AesKwTask : public ReturnArrayBufferViewTask, public DeferredData {
 public:
  AesKwTask(JSContext*, const nsString&, const ObjectOrString&, CryptoKey& aKey,
            bool aEncrypt)
      : mMechanism(CKM_NSS_AES_KEY_WRAP), mEncrypt(aEncrypt) {
    Init(aKey, aEncrypt);
  }

  AesKwTask(JSContext*, const nsString&, const ObjectOrString&, CryptoKey& aKey,
            const CryptoOperationData& aData, bool aEncrypt)
      : mMechanism(CKM_NSS_AES_KEY_WRAP), mEncrypt(aEncrypt) {
    Init(aKey, aEncrypt);
    SetData(aData);
  }

  void Init(CryptoKey& aKey, bool aEncrypt) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_AES_KW);

    if (!mSymKey.Assign(aKey.GetSymKey())) {
      mEarlyRv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }

    // Check that we got a reasonable key
    if ((mSymKey.Length() != 16) && (mSymKey.Length() != 24) &&
        (mSymKey.Length() != 32)) {
      mEarlyRv = NS_ERROR_DOM_DATA_ERR;
      return;
    }
  }

 private:
  CK_MECHANISM_TYPE mMechanism;
  CryptoBuffer mSymKey;
  bool mEncrypt;

  virtual nsresult DoCrypto() override {
    nsresult rv;

    if (!mDataIsSet) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Check that the input is a multiple of 64 bits long
    if (mData.Length() == 0 || mData.Length() % 8 != 0) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Import the key
    SECItem keyItem = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &keyItem, mSymKey);
    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    MOZ_ASSERT(slot.get());
    UniquePK11SymKey symKey(PK11_ImportSymKey(slot.get(), mMechanism,
                                              PK11_OriginUnwrap, CKA_WRAP,
                                              &keyItem, nullptr));
    if (!symKey) {
      return NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }

    // Import the data to a SECItem
    SECItem dataItem = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &dataItem, mData);

    // Parameters for the fake keys
    CK_MECHANISM_TYPE fakeMechanism = CKM_SHA_1_HMAC;
    CK_ATTRIBUTE_TYPE fakeOperation = CKA_SIGN;

    if (mEncrypt) {
      // Import the data into a fake PK11SymKey structure
      UniquePK11SymKey keyToWrap(
          PK11_ImportSymKey(slot.get(), fakeMechanism, PK11_OriginUnwrap,
                            fakeOperation, &dataItem, nullptr));
      if (!keyToWrap) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      // Encrypt and return the wrapped key
      // AES-KW encryption results in a wrapped key 64 bits longer
      if (!mResult.SetLength(mData.Length() + 8, fallible)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
      SECItem resultItem = {siBuffer, mResult.Elements(),
                            (unsigned int)mResult.Length()};
      rv = MapSECStatus(PK11_WrapSymKey(mMechanism, nullptr, symKey.get(),
                                        keyToWrap.get(), &resultItem));
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);
    } else {
      // Decrypt the ciphertext into a temporary PK11SymKey
      // Unwrapped key should be 64 bits shorter
      int keySize = mData.Length() - 8;
      UniquePK11SymKey unwrappedKey(
          PK11_UnwrapSymKey(symKey.get(), mMechanism, nullptr, &dataItem,
                            fakeMechanism, fakeOperation, keySize));
      if (!unwrappedKey) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      // Export the key to get the cleartext
      rv = MapSECStatus(PK11_ExtractKeyValue(unwrappedKey.get()));
      if (NS_FAILED(rv)) {
        return NS_ERROR_DOM_UNKNOWN_ERR;
      }
      ATTEMPT_BUFFER_ASSIGN(mResult, PK11_GetKeyData(unwrappedKey.get()));
    }

    return rv;
  }
};

class RsaOaepTask : public ReturnArrayBufferViewTask, public DeferredData {
 public:
  RsaOaepTask(JSContext* aCx, const nsString&, const ObjectOrString& aAlgorithm,
              CryptoKey& aKey, bool aEncrypt)
      : mPrivKey(aKey.GetPrivateKey()),
        mPubKey(aKey.GetPublicKey()),
        mEncrypt(aEncrypt) {
    Init(aCx, aAlgorithm, aKey, aEncrypt);
  }

  RsaOaepTask(JSContext* aCx, const nsString&, const ObjectOrString& aAlgorithm,
              CryptoKey& aKey, const CryptoOperationData& aData, bool aEncrypt)
      : mPrivKey(aKey.GetPrivateKey()),
        mPubKey(aKey.GetPublicKey()),
        mEncrypt(aEncrypt) {
    Init(aCx, aAlgorithm, aKey, aEncrypt);
    SetData(aData);
  }

  void Init(JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
            bool aEncrypt) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_RSA_OAEP);

    if (mEncrypt) {
      if (!mPubKey) {
        mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
        return;
      }
      mStrength = SECKEY_PublicKeyStrength(mPubKey.get());
    } else {
      if (!mPrivKey) {
        mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
        return;
      }
      mStrength = PK11_GetPrivateModulusLen(mPrivKey.get());
    }

    // The algorithm could just be given as a string
    // in which case there would be no label specified.
    if (!aAlgorithm.IsString()) {
      RootedDictionary<RsaOaepParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }

      if (params.mLabel.WasPassed()) {
        ATTEMPT_BUFFER_INIT(mLabel, params.mLabel.Value());
      }
    }
    // Otherwise mLabel remains the empty octet string, as intended

    KeyAlgorithm& hashAlg = aKey.Algorithm().mRsa.mHash;
    mHashMechanism = KeyAlgorithmProxy::GetMechanism(hashAlg);
    mMgfMechanism = MapHashAlgorithmNameToMgfMechanism(hashAlg.mName);

    // Check we found appropriate mechanisms.
    if (mHashMechanism == UNKNOWN_CK_MECHANISM ||
        mMgfMechanism == UNKNOWN_CK_MECHANISM) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }
  }

 private:
  CK_MECHANISM_TYPE mHashMechanism;
  CK_MECHANISM_TYPE mMgfMechanism;
  UniqueSECKEYPrivateKey mPrivKey;
  UniqueSECKEYPublicKey mPubKey;
  CryptoBuffer mLabel;
  uint32_t mStrength;
  bool mEncrypt;

  virtual nsresult DoCrypto() override {
    nsresult rv;

    if (!mDataIsSet) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Ciphertext is an integer mod the modulus, so it will be
    // no longer than mStrength octets
    if (!mResult.SetLength(mStrength, fallible)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    CK_RSA_PKCS_OAEP_PARAMS oaepParams;
    oaepParams.source = CKZ_DATA_SPECIFIED;

    oaepParams.pSourceData = mLabel.Length() ? mLabel.Elements() : nullptr;
    oaepParams.ulSourceDataLen = mLabel.Length();

    oaepParams.mgf = mMgfMechanism;
    oaepParams.hashAlg = mHashMechanism;

    SECItem param;
    param.type = siBuffer;
    param.data = (unsigned char*)&oaepParams;
    param.len = sizeof(oaepParams);

    uint32_t outLen = 0;
    if (mEncrypt) {
      // PK11_PubEncrypt() checks the plaintext's length and fails if it is too
      // long to encrypt, i.e. if it is longer than (k - 2hLen - 2) with 'k'
      // being the length in octets of the RSA modulus n and 'hLen' being the
      // output length in octets of the chosen hash function.
      // <https://tools.ietf.org/html/rfc3447#section-7.1>
      rv = MapSECStatus(PK11_PubEncrypt(
          mPubKey.get(), CKM_RSA_PKCS_OAEP, &param, mResult.Elements(), &outLen,
          mResult.Length(), mData.Elements(), mData.Length(), nullptr));
    } else {
      rv = MapSECStatus(PK11_PrivDecrypt(
          mPrivKey.get(), CKM_RSA_PKCS_OAEP, &param, mResult.Elements(),
          &outLen, mResult.Length(), mData.Elements(), mData.Length()));
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);

    mResult.TruncateLength(outLen);
    return NS_OK;
  }
};

class HmacTask : public WebCryptoTask {
 public:
  HmacTask(JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
           const CryptoOperationData& aSignature,
           const CryptoOperationData& aData, bool aSign)
      : mMechanism(aKey.Algorithm().Mechanism()), mSign(aSign) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_HMAC);

    ATTEMPT_BUFFER_INIT(mData, aData);
    if (!aSign) {
      ATTEMPT_BUFFER_INIT(mSignature, aSignature);
    }

    if (!mSymKey.Assign(aKey.GetSymKey())) {
      mEarlyRv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }

    // Check that we got a symmetric key
    if (mSymKey.Length() == 0) {
      mEarlyRv = NS_ERROR_DOM_DATA_ERR;
      return;
    }
  }

 private:
  CK_MECHANISM_TYPE mMechanism;
  CryptoBuffer mSymKey;
  CryptoBuffer mData;
  CryptoBuffer mSignature;
  CryptoBuffer mResult;
  bool mSign;

  virtual nsresult DoCrypto() override {
    // Initialize the output buffer
    if (!mResult.SetLength(HASH_LENGTH_MAX, fallible)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Import the key
    uint32_t outLen;
    SECItem keyItem = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &keyItem, mSymKey);
    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    MOZ_ASSERT(slot.get());
    UniquePK11SymKey symKey(PK11_ImportSymKey(slot.get(), mMechanism,
                                              PK11_OriginUnwrap, CKA_SIGN,
                                              &keyItem, nullptr));
    if (!symKey) {
      return NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }

    // Compute the MAC
    SECItem param = {siBuffer, nullptr, 0};
    UniquePK11Context ctx(
        PK11_CreateContextBySymKey(mMechanism, CKA_SIGN, symKey.get(), &param));
    if (!ctx.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }
    nsresult rv = MapSECStatus(PK11_DigestBegin(ctx.get()));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);
    rv = MapSECStatus(
        PK11_DigestOp(ctx.get(), mData.Elements(), mData.Length()));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);
    rv = MapSECStatus(PK11_DigestFinal(ctx.get(), mResult.Elements(), &outLen,
                                       mResult.Length()));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);

    mResult.TruncateLength(outLen);
    return rv;
  }

  // Returns mResult as an ArrayBufferView, or an error
  virtual void Resolve() override {
    if (mSign) {
      // Return the computed MAC
      TypedArrayCreator<ArrayBuffer> ret(mResult);
      mResultPromise->MaybeResolve(ret);
    } else {
      // Compare the MAC to the provided signature
      // No truncation allowed
      bool equal = (mResult.Length() == mSignature.Length());
      if (equal) {
        int cmp = NSS_SecureMemcmp(mSignature.Elements(), mResult.Elements(),
                                   mSignature.Length());
        equal = (cmp == 0);
      }
      mResultPromise->MaybeResolve(equal);
    }
  }
};

class AsymmetricSignVerifyTask : public WebCryptoTask {
 public:
  AsymmetricSignVerifyTask(JSContext* aCx, const nsString& aAlgName,
                           const ObjectOrString& aAlgorithm, CryptoKey& aKey,
                           const CryptoOperationData& aSignature,
                           const CryptoOperationData& aData, bool aSign)
      : mOidTag(SEC_OID_UNKNOWN),
        mHashMechanism(UNKNOWN_CK_MECHANISM),
        mMgfMechanism(UNKNOWN_CK_MECHANISM),
        mPrivKey(aKey.GetPrivateKey()),
        mPubKey(aKey.GetPublicKey()),
        mSaltLength(0),
        mSign(aSign),
        mVerified(false),
        mAlgorithm(Algorithm::UNKNOWN) {
    ATTEMPT_BUFFER_INIT(mData, aData);
    if (!aSign) {
      ATTEMPT_BUFFER_INIT(mSignature, aSignature);
    }

    nsString hashAlgName;

    if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1)) {
      mAlgorithm = Algorithm::RSA_PKCS1;
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_RSASSA_PKCS1);
      hashAlgName = aKey.Algorithm().mRsa.mHash.mName;
    } else if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS)) {
      mAlgorithm = Algorithm::RSA_PSS;
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_RSA_PSS);

      KeyAlgorithm& hashAlg = aKey.Algorithm().mRsa.mHash;
      hashAlgName = hashAlg.mName;
      mHashMechanism = KeyAlgorithmProxy::GetMechanism(hashAlg);
      mMgfMechanism = MapHashAlgorithmNameToMgfMechanism(hashAlgName);

      // Check we found appropriate mechanisms.
      if (mHashMechanism == UNKNOWN_CK_MECHANISM ||
          mMgfMechanism == UNKNOWN_CK_MECHANISM) {
        mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        return;
      }

      RootedDictionary<RsaPssParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }

      mSaltLength = params.mSaltLength;
    } else if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
      mAlgorithm = Algorithm::ECDSA;
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_ECDSA);

      // For ECDSA, the hash name comes from the algorithm parameter
      RootedDictionary<EcdsaParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }

      mEarlyRv = GetAlgorithmName(aCx, params.mHash, hashAlgName);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        return;
      }
    } else if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
      mAlgorithm = Algorithm::ED25519;
      CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_ED25519);
    } else {
      // This shouldn't happen; CreateSignVerifyTask shouldn't create
      // one of these unless it's for the above algorithms.
      MOZ_ASSERT(false);
    }

    // Must have a valid algorithm by now.
    MOZ_ASSERT(mAlgorithm != Algorithm::UNKNOWN);

    // Determine hash algorithm to use.
    mOidTag = MapHashAlgorithmNameToOID(hashAlgName);

    if (mOidTag == SEC_OID_UNKNOWN && AlgorithmRequiresHashing(mAlgorithm)) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }

    // Check that we have the appropriate key
    if ((mSign && !mPrivKey) || (!mSign && !mPubKey)) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }
  }

 private:
  SECOidTag mOidTag;
  CK_MECHANISM_TYPE mHashMechanism;
  CK_MECHANISM_TYPE mMgfMechanism;
  UniqueSECKEYPrivateKey mPrivKey;
  UniqueSECKEYPublicKey mPubKey;
  CryptoBuffer mSignature;
  CryptoBuffer mData;
  uint32_t mSaltLength;
  bool mSign;
  bool mVerified;

  // The signature algorithm to use.
  enum class Algorithm : uint8_t {
    ECDSA,
    RSA_PKCS1,
    RSA_PSS,
    ED25519,
    UNKNOWN
  };
  Algorithm mAlgorithm;

  bool AlgorithmRequiresHashing(Algorithm aAlgorithm) {
    MOZ_ASSERT(aAlgorithm != Algorithm::UNKNOWN);
    /* Currently, only ED25519 does not require hashing.*/
    switch (aAlgorithm) {
      case Algorithm::ED25519:
        return false;
      case Algorithm::ECDSA:
      case Algorithm::RSA_PKCS1:
      case Algorithm::RSA_PSS:
      // Impossible
      case Algorithm::UNKNOWN:
        return true;
    }
    /*Also impossible, as all the algorithm options should be managed in the
     * switch. */
    return true;
  }

  virtual nsresult DoCrypto() override {
    SECStatus rv;
    UniqueSECItem hash;

    SECItem* params = nullptr;
    CK_MECHANISM_TYPE mech =
        PK11_MapSignKeyType(mSign ? mPrivKey->keyType : mPubKey->keyType);

    CK_RSA_PKCS_PSS_PARAMS rsaPssParams;
    SECItem rsaPssParamsItem = {
        siBuffer,
    };

    // Set up parameters for RSA-PSS.
    if (mAlgorithm == Algorithm::RSA_PSS) {
      rsaPssParams.hashAlg = mHashMechanism;
      rsaPssParams.mgf = mMgfMechanism;
      rsaPssParams.sLen = mSaltLength;

      rsaPssParamsItem.data = (unsigned char*)&rsaPssParams;
      rsaPssParamsItem.len = sizeof(rsaPssParams);
      params = &rsaPssParamsItem;

      mech = CKM_RSA_PKCS_PSS;
    }

    if (AlgorithmRequiresHashing(mAlgorithm)) {
      // Compute digest over given data.
      hash.reset(::SECITEM_AllocItem(nullptr, nullptr,
                                     HASH_ResultLenByOidTag(mOidTag)));

      if (!hash || !hash->data || hash->len > PR_INT32_MAX) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      rv = PK11_HashBuf(mOidTag, hash->data, mData.Elements(),
                        static_cast<PRInt32>(mData.Length()));
      NS_ENSURE_SUCCESS(MapSECStatus(rv), NS_ERROR_DOM_OPERATION_ERR);
    }

    // Wrap hash in a digest info template (RSA-PKCS1 only).
    if (mAlgorithm == Algorithm::RSA_PKCS1) {
      if (!hash) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      UniqueSGNDigestInfo di(
          SGN_CreateDigestInfo(mOidTag, hash->data, hash->len));
      if (!di) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      // Reuse |hash|.
      SECITEM_FreeItem(hash.get(), false);
      if (!SEC_ASN1EncodeItem(nullptr, hash.get(), di.get(),
                              SGN_DigestInfoTemplate)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
    }

    // Allocate SECItem to hold the signature.
    uint32_t len = mSign ? PK11_SignatureLen(mPrivKey.get()) : 0;
    UniqueSECItem sig(::SECITEM_AllocItem(nullptr, nullptr, len));
    if (!sig) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Buffer for signature/verification input.
    SECItem dataToOperateOn;
    if (mSign) {
      if (AlgorithmRequiresHashing(mAlgorithm)) {
        dataToOperateOn = {siBuffer, hash->data, hash->len};
      } else {
        dataToOperateOn = {siBuffer, mData.Elements(),
                           static_cast<unsigned int>(mData.Length())};
      }

      // Sign the hash.
      rv = PK11_SignWithMechanism(mPrivKey.get(), mech, params, sig.get(),
                                  &dataToOperateOn);
      NS_ENSURE_SUCCESS(MapSECStatus(rv), NS_ERROR_DOM_OPERATION_ERR);
      ATTEMPT_BUFFER_ASSIGN(mSignature, sig.get());
    } else {
      if (AlgorithmRequiresHashing(mAlgorithm)) {
        dataToOperateOn = {siBuffer, hash->data, hash->len};
      } else {
        dataToOperateOn = {siBuffer, mData.Elements(),
                           static_cast<unsigned int>(mData.Length())};
      }

      // Copy the given signature to the SECItem.
      if (!mSignature.ToSECItem(nullptr, sig.get())) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      // Verify the signature.
      rv = PK11_VerifyWithMechanism(mPubKey.get(), mech, params, sig.get(),
                                    &dataToOperateOn, nullptr);
      mVerified = NS_SUCCEEDED(MapSECStatus(rv));
    }

    return NS_OK;
  }

  virtual void Resolve() override {
    if (mSign) {
      TypedArrayCreator<ArrayBuffer> ret(mSignature);
      mResultPromise->MaybeResolve(ret);
    } else {
      mResultPromise->MaybeResolve(mVerified);
    }
  }
};

class DigestTask : public ReturnArrayBufferViewTask {
 public:
  DigestTask(const nsString& aAlgName, const CryptoOperationData& aData) {
    ATTEMPT_BUFFER_INIT(mData, aData);

    mOidTag = MapHashAlgorithmNameToOID(aAlgName);
    MOZ_ASSERT(mOidTag != SEC_OID_UNKNOWN);
    if (mOidTag == SEC_OID_UNKNOWN) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }
  }

 private:
  SECOidTag mOidTag;
  CryptoBuffer mData;

  virtual nsresult DoCrypto() override {
    // Resize the result buffer
    uint32_t hashLen = HASH_ResultLenByOidTag(mOidTag);
    if (!mResult.SetLength(hashLen, fallible)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    // Compute the hash
    nsresult rv = MapSECStatus(PK11_HashBuf(mOidTag, mResult.Elements(),
                                            mData.Elements(), mData.Length()));
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    return rv;
  }
};

class ImportKeyTask : public WebCryptoTask {
 public:
  void Init(nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
            const ObjectOrString& aAlgorithm, bool aExtractable,
            const Sequence<nsString>& aKeyUsages) {
    mFormat = aFormat;
    mDataIsSet = false;
    mDataIsJwk = false;

    // This stuff pretty much always happens, so we'll do it here
    mKey = MakeRefPtr<CryptoKey>(aGlobal);
    mKey->SetExtractable(aExtractable);
    mKey->ClearUsages();
    for (uint32_t i = 0; i < aKeyUsages.Length(); ++i) {
      mEarlyRv = mKey->AddUsage(aKeyUsages[i]);
      if (NS_FAILED(mEarlyRv)) {
        return;
      }
    }

    mEarlyRv = GetAlgorithmName(aCx, aAlgorithm, mAlgName);
    if (NS_FAILED(mEarlyRv)) {
      mEarlyRv = NS_ERROR_DOM_DATA_ERR;
      return;
    }
  }

  static bool JwkCompatible(const JsonWebKey& aJwk, const CryptoKey* aKey) {
    // Check 'alg'
    if (!aJwk.mKty.EqualsLiteral(JWK_TYPE_OKP) &&
        !(aJwk.mKty.EqualsLiteral(JWK_TYPE_EC) &&
          aKey->Algorithm().Mechanism() == CKM_ECDH1_DERIVE) &&
        aJwk.mAlg.WasPassed() &&
        aJwk.mAlg.Value() != aKey->Algorithm().JwkAlg()) {
      return false;
    }

    // Check 'ext'
    if (aKey->Extractable() && aJwk.mExt.WasPassed() && !aJwk.mExt.Value()) {
      return false;
    }

    // Check 'key_ops'
    if (aJwk.mKey_ops.WasPassed()) {
      nsTArray<nsString> usages;
      aKey->GetUsages(usages);
      for (size_t i = 0; i < usages.Length(); ++i) {
        if (!aJwk.mKey_ops.Value().Contains(usages[i])) {
          return false;
        }
      }
    }

    // Individual algorithms may still have to check 'use'
    return true;
  }

  void SetKeyData(JSContext* aCx, JS::Handle<JSObject*> aKeyData) {
    mDataIsJwk = false;

    // Try ArrayBuffer
    RootedSpiderMonkeyInterface<ArrayBuffer> ab(aCx);
    if (ab.Init(aKeyData)) {
      if (!mKeyData.Assign(ab)) {
        mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      }
      return;
    }

    // Try ArrayBufferView
    RootedSpiderMonkeyInterface<ArrayBufferView> abv(aCx);
    if (abv.Init(aKeyData)) {
      if (!mKeyData.Assign(abv)) {
        mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      }
      return;
    }

    // Try JWK
    ClearException ce(aCx);
    JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*aKeyData));
    if (!mJwk.Init(aCx, value)) {
      mEarlyRv = NS_ERROR_DOM_DATA_ERR;
      return;
    }

    mDataIsJwk = true;
  }

  void SetKeyDataMaybeParseJWK(const CryptoBuffer& aKeyData) {
    if (!mKeyData.Assign(aKeyData)) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }

    mDataIsJwk = false;

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      nsDependentCSubstring utf8(
          (const char*)mKeyData.Elements(),
          (const char*)(mKeyData.Elements() + mKeyData.Length()));
      if (!IsUtf8(utf8)) {
        mEarlyRv = NS_ERROR_DOM_DATA_ERR;
        return;
      }

      nsString json = NS_ConvertUTF8toUTF16(utf8);
      if (!mJwk.Init(json)) {
        mEarlyRv = NS_ERROR_DOM_DATA_ERR;
        return;
      }

      mDataIsJwk = true;
    }
  }

  void SetRawKeyData(const CryptoBuffer& aKeyData) {
    if (!mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW)) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }

    if (!mKeyData.Assign(aKeyData)) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }

    mDataIsJwk = false;
  }

 protected:
  nsString mFormat;
  RefPtr<CryptoKey> mKey;
  CryptoBuffer mKeyData;
  bool mDataIsSet;
  bool mDataIsJwk;
  JsonWebKey mJwk;
  nsString mAlgName;

 private:
  virtual void Resolve() override { mResultPromise->MaybeResolve(mKey); }

  virtual void Cleanup() override { mKey = nullptr; }
};

class ImportSymmetricKeyTask : public ImportKeyTask {
 public:
  ImportSymmetricKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                         const nsAString& aFormat,
                         const ObjectOrString& aAlgorithm, bool aExtractable,
                         const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
  }

  ImportSymmetricKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                         const nsAString& aFormat,
                         const JS::Handle<JSObject*> aKeyData,
                         const ObjectOrString& aAlgorithm, bool aExtractable,
                         const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    SetKeyData(aCx, aKeyData);
    NS_ENSURE_SUCCESS_VOID(mEarlyRv);
    if (mDataIsJwk && !mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
      return;
    }
  }

  void Init(nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
            const ObjectOrString& aAlgorithm, bool aExtractable,
            const Sequence<nsString>& aKeyUsages) {
    ImportKeyTask::Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable,
                        aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    // This task only supports raw and JWK format.
    if (!mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
        !mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW)) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }

    // If this is an HMAC key, import the hash name
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_HMAC)) {
      RootedDictionary<HmacImportParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }
      mEarlyRv = GetAlgorithmName(aCx, params.mHash, mHashName);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }
    }
  }

  virtual nsresult BeforeCrypto() override {
    nsresult rv;
    // If we're doing a JWK import, import the key data
    if (mDataIsJwk) {
      if (!mJwk.mK.WasPassed()) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      // Import the key material
      rv = mKeyData.FromJwkBase64(mJwk.mK.Value());
      if (NS_FAILED(rv)) {
        return NS_ERROR_DOM_DATA_ERR;
      }
    }
    // Check that we have valid key data.
    if (mKeyData.Length() == 0 &&
        (!mAlgName.EqualsLiteral(WEBCRYPTO_ALG_PBKDF2) &&
         !mAlgName.EqualsLiteral(WEBCRYPTO_ALG_HKDF))) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    // Construct an appropriate KeyAlgorithm,
    // and verify that usages are appropriate
    if (mKeyData.Length() > UINT32_MAX / 8) {
      return NS_ERROR_DOM_DATA_ERR;
    }
    uint32_t length = 8 * mKeyData.Length();  // bytes to bits
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
        mAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
        mAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM) ||
        mAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW)) {
      if (mKey->HasUsageOtherThan(CryptoKey::ENCRYPT | CryptoKey::DECRYPT |
                                  CryptoKey::WRAPKEY | CryptoKey::UNWRAPKEY)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW) &&
          mKey->HasUsageOtherThan(CryptoKey::WRAPKEY | CryptoKey::UNWRAPKEY)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      if ((length != 128) && (length != 192) && (length != 256)) {
        return NS_ERROR_DOM_DATA_ERR;
      }
      mKey->Algorithm().MakeAes(mAlgName, length);

      if (mDataIsJwk && mJwk.mUse.WasPassed() &&
          !mJwk.mUse.Value().EqualsLiteral(JWK_USE_ENC)) {
        return NS_ERROR_DOM_DATA_ERR;
      }
    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_HKDF) ||
               mAlgName.EqualsLiteral(WEBCRYPTO_ALG_PBKDF2)) {
      if (mKey->HasUsageOtherThan(CryptoKey::DERIVEKEY |
                                  CryptoKey::DERIVEBITS)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }
      mKey->Algorithm().MakeKDF(mAlgName);

      if (mDataIsJwk && mJwk.mUse.WasPassed()) {
        // There is not a 'use' value consistent with PBKDF or HKDF
        return NS_ERROR_DOM_DATA_ERR;
      };
    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_HMAC)) {
      if (mKey->HasUsageOtherThan(CryptoKey::SIGN | CryptoKey::VERIFY)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      mKey->Algorithm().MakeHmac(length, mHashName);

      if (mKey->Algorithm().Mechanism() == UNKNOWN_CK_MECHANISM) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      if (mDataIsJwk && mJwk.mUse.WasPassed() &&
          !mJwk.mUse.Value().EqualsLiteral(JWK_USE_SIG)) {
        return NS_ERROR_DOM_DATA_ERR;
      }
    } else {
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }

    if (!mKey->HasAnyUsage()) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    if (NS_FAILED(mKey->SetSymKey(mKeyData))) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    mKey->SetType(CryptoKey::SECRET);

    if (mDataIsJwk && !JwkCompatible(mJwk, mKey)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    mEarlyComplete = true;
    return NS_OK;
  }

 private:
  nsString mHashName;
};

class ImportRsaKeyTask : public ImportKeyTask {
 public:
  ImportRsaKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                   const nsAString& aFormat, const ObjectOrString& aAlgorithm,
                   bool aExtractable, const Sequence<nsString>& aKeyUsages)
      : mModulusLength(0) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
  }

  ImportRsaKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                   const nsAString& aFormat, JS::Handle<JSObject*> aKeyData,
                   const ObjectOrString& aAlgorithm, bool aExtractable,
                   const Sequence<nsString>& aKeyUsages)
      : mModulusLength(0) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    SetKeyData(aCx, aKeyData);
    NS_ENSURE_SUCCESS_VOID(mEarlyRv);
    if (mDataIsJwk && !mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
      return;
    }
  }

  void Init(nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
            const ObjectOrString& aAlgorithm, bool aExtractable,
            const Sequence<nsString>& aKeyUsages) {
    ImportKeyTask::Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable,
                        aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    // If this is RSA with a hash, cache the hash name
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
        mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP) ||
        mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS)) {
      RootedDictionary<RsaHashedImportParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_DATA_ERR;
        return;
      }

      mEarlyRv = GetAlgorithmName(aCx, params.mHash, mHashName);
      if (NS_FAILED(mEarlyRv)) {
        mEarlyRv = NS_ERROR_DOM_DATA_ERR;
        return;
      }
    }

    // Check support for the algorithm and hash names
    CK_MECHANISM_TYPE mech1 = MapAlgorithmNameToMechanism(mAlgName);
    CK_MECHANISM_TYPE mech2 = MapAlgorithmNameToMechanism(mHashName);
    if ((mech1 == UNKNOWN_CK_MECHANISM) || (mech2 == UNKNOWN_CK_MECHANISM)) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }
  }

 private:
  nsString mHashName;
  uint32_t mModulusLength;
  CryptoBuffer mPublicExponent;

  virtual nsresult DoCrypto() override {
    // Import the key data itself
    UniqueSECKEYPublicKey pubKey;
    UniqueSECKEYPrivateKey privKey;
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI) ||
        (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
         !mJwk.mD.WasPassed())) {
      // Public key import
      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
        pubKey = CryptoKey::PublicKeyFromSpki(mKeyData);
      } else {
        pubKey = CryptoKey::PublicKeyFromJwk(mJwk);
      }

      if (!pubKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (NS_FAILED(mKey->SetPublicKey(pubKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PUBLIC);
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8) ||
               (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
                mJwk.mD.WasPassed())) {
      // Private key import
      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8)) {
        privKey = CryptoKey::PrivateKeyFromPkcs8(mKeyData);
      } else {
        privKey = CryptoKey::PrivateKeyFromJwk(mJwk);
      }

      if (!privKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (NS_FAILED(mKey->SetPrivateKey(privKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PRIVATE);
      pubKey = UniqueSECKEYPublicKey(SECKEY_ConvertToPublicKey(privKey.get()));
      if (!pubKey) {
        return NS_ERROR_DOM_UNKNOWN_ERR;
      }
    } else {
      // Invalid key format
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    if (pubKey->keyType != rsaKey) {
      return NS_ERROR_DOM_DATA_ERR;
    }
    // Extract relevant information from the public key
    mModulusLength = 8 * pubKey->u.rsa.modulus.len;
    if (!mPublicExponent.Assign(&pubKey->u.rsa.publicExponent)) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    return NS_OK;
  }

  virtual nsresult AfterCrypto() override {
    // Check permissions for the requested operation
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP)) {
      if ((mKey->GetKeyType() == CryptoKey::PUBLIC &&
           mKey->HasUsageOtherThan(CryptoKey::ENCRYPT | CryptoKey::WRAPKEY)) ||
          (mKey->GetKeyType() == CryptoKey::PRIVATE &&
           mKey->HasUsageOtherThan(CryptoKey::DECRYPT |
                                   CryptoKey::UNWRAPKEY))) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }
    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
               mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS)) {
      if ((mKey->GetKeyType() == CryptoKey::PUBLIC &&
           mKey->HasUsageOtherThan(CryptoKey::VERIFY)) ||
          (mKey->GetKeyType() == CryptoKey::PRIVATE &&
           mKey->HasUsageOtherThan(CryptoKey::SIGN))) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }
    }

    if (mKey->GetKeyType() == CryptoKey::PRIVATE && !mKey->HasAnyUsage()) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    // Set an appropriate KeyAlgorithm
    if (!mKey->Algorithm().MakeRsa(mAlgName, mModulusLength, mPublicExponent,
                                   mHashName)) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    if (mDataIsJwk && !JwkCompatible(mJwk, mKey)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    return NS_OK;
  }
};

class ImportEcKeyTask : public ImportKeyTask {
 public:
  ImportEcKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                  const nsAString& aFormat, const ObjectOrString& aAlgorithm,
                  bool aExtractable, const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
  }

  ImportEcKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                  const nsAString& aFormat, JS::Handle<JSObject*> aKeyData,
                  const ObjectOrString& aAlgorithm, bool aExtractable,
                  const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    SetKeyData(aCx, aKeyData);
    NS_ENSURE_SUCCESS_VOID(mEarlyRv);
  }

  void Init(nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
            const ObjectOrString& aAlgorithm, bool aExtractable,
            const Sequence<nsString>& aKeyUsages) {
    ImportKeyTask::Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable,
                        aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW) ||
        mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      RootedDictionary<EcKeyImportParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv) || !params.mNamedCurve.WasPassed()) {
        mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
        return;
      }

      if (!NormalizeToken(params.mNamedCurve.Value(), mNamedCurve)) {
        mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        return;
      }
    }
  }

 private:
  nsString mNamedCurve;

  virtual nsresult DoCrypto() override {
    // Import the key data itself
    UniqueSECKEYPublicKey pubKey;
    UniqueSECKEYPrivateKey privKey;

    if ((mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
         mJwk.mD.WasPassed()) ||
        mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8)) {
      // Private key import
      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
        privKey = CryptoKey::PrivateKeyFromJwk(mJwk);
        if (!privKey) {
          return NS_ERROR_DOM_DATA_ERR;
        }
      } else {
        privKey = CryptoKey::PrivateKeyFromPkcs8(mKeyData);
        if (!privKey) {
          return NS_ERROR_DOM_DATA_ERR;
        }

        ScopedAutoSECItem ecParams;
        if (PK11_ReadRawAttribute(PK11_TypePrivKey, privKey.get(),
                                  CKA_EC_PARAMS, &ecParams) != SECSuccess) {
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        }

        SECOidTag tag;
        if (!FindOIDTagForEncodedParameters(&ecParams, &tag)) {
          return NS_ERROR_DOM_DATA_ERR;
        }

        // Find a matching and supported named curve.
        if (!MapOIDTagToNamedCurve(tag, mNamedCurve)) {
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        }
      }

      if (NS_FAILED(mKey->SetPrivateKey(privKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PRIVATE);
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW) ||
               mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI) ||
               (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
                !mJwk.mD.WasPassed())) {
      // Public key import
      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW)) {
        pubKey = CryptoKey::PublicECKeyFromRaw(mKeyData, mNamedCurve);
      } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
        pubKey = CryptoKey::PublicKeyFromSpki(mKeyData);
      } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
        pubKey = CryptoKey::PublicKeyFromJwk(mJwk);
      } else {
        MOZ_ASSERT(false);
      }

      if (!pubKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
        if (pubKey->keyType != ecKey) {
          return NS_ERROR_DOM_DATA_ERR;
        }
        if (!CheckEncodedParameters(&pubKey->u.ec.DEREncodedParams)) {
          return NS_ERROR_DOM_OPERATION_ERR;
        }

        SECOidTag tag;
        if (!FindOIDTagForEncodedParameters(&pubKey->u.ec.DEREncodedParams,
                                            &tag)) {
          return NS_ERROR_DOM_DATA_ERR;
        }

        // Find a matching and supported named curve.
        if (!MapOIDTagToNamedCurve(tag, mNamedCurve)) {
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        }
      }

      if (NS_FAILED(mKey->SetPublicKey(pubKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PUBLIC);
    } else {
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }

    // Checking the 'crv' consistency
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      // the curve stated in 'crv field'
      nsString namedCurveFromCrv;
      if (!NormalizeToken(mJwk.mCrv.Value(), namedCurveFromCrv)) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      // https://w3c.github.io/webcrypto/#ecdh-operations
      // https://w3c.github.io/webcrypto/#ecdsa-operations
      // If namedCurve is not equal to the namedCurve member of
      // normalizedAlgorithm (mNamedCurve in our case), throw a DataError.
      if (!mNamedCurve.Equals(namedCurveFromCrv)) {
        return NS_ERROR_DOM_DATA_ERR;
      }
    }
    return NS_OK;
  }

  virtual nsresult AfterCrypto() override {
    uint32_t privateAllowedUsages = 0, publicAllowedUsages = 0;
    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDH)) {
      privateAllowedUsages = CryptoKey::DERIVEBITS | CryptoKey::DERIVEKEY;
      publicAllowedUsages = 0;
    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
      privateAllowedUsages = CryptoKey::SIGN;
      publicAllowedUsages = CryptoKey::VERIFY;
    }

    // Check permissions for the requested operation
    if ((mKey->GetKeyType() == CryptoKey::PRIVATE &&
         mKey->HasUsageOtherThan(privateAllowedUsages)) ||
        (mKey->GetKeyType() == CryptoKey::PUBLIC &&
         mKey->HasUsageOtherThan(publicAllowedUsages))) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    if (mKey->GetKeyType() == CryptoKey::PRIVATE && !mKey->HasAnyUsage()) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    mKey->Algorithm().MakeEc(mAlgName, mNamedCurve);

    if (mDataIsJwk && !JwkCompatible(mJwk, mKey)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    return NS_OK;
  }
};

class ImportOKPKeyTask : public ImportKeyTask {
 public:
  ImportOKPKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                   const nsAString& aFormat, const ObjectOrString& aAlgorithm,
                   bool aExtractable, const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
  }

  ImportOKPKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                   const nsAString& aFormat, JS::Handle<JSObject*> aKeyData,
                   const ObjectOrString& aAlgorithm, bool aExtractable,
                   const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    SetKeyData(aCx, aKeyData);
    NS_ENSURE_SUCCESS_VOID(mEarlyRv);
  }

  void Init(nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
            const ObjectOrString& aAlgorithm, bool aExtractable,
            const Sequence<nsString>& aKeyUsages) {
    ImportKeyTask::Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable,
                        aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW)) {
      // Construct an appropriate KeyAlgorithm
      if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
        mNamedCurve.AssignLiteral(WEBCRYPTO_NAMED_CURVE_ED25519);
      } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
        mNamedCurve.AssignLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519);
      } else {
        mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        return;
      }
    }
  }

 private:
  nsString mNamedCurve;

  virtual nsresult DoCrypto() override {
    // Import the key data itself
    UniqueSECKEYPublicKey pubKey;
    UniqueSECKEYPrivateKey privKey;

    if ((mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
         mJwk.mD.WasPassed()) ||
        mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8)) {
      // Private key import
      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
        privKey = CryptoKey::PrivateKeyFromJwk(mJwk, mAlgName);
        if (!privKey) {
          return NS_ERROR_DOM_DATA_ERR;
        }
      } else {
        privKey = CryptoKey::PrivateKeyFromPkcs8(mKeyData);
        if (!privKey) {
          return NS_ERROR_DOM_DATA_ERR;
        }

        ScopedAutoSECItem ecParams;
        if (PK11_ReadRawAttribute(PK11_TypePrivKey, privKey.get(),
                                  CKA_EC_PARAMS, &ecParams) != SECSuccess) {
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        }

        SECOidTag tag;
        if (!FindOIDTagForEncodedParameters(&ecParams, &tag)) {
          return NS_ERROR_DOM_DATA_ERR;
        }

        // Find a matching and supported named curve.
        if (!MapOIDTagToNamedCurve(tag, mNamedCurve)) {
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        }
      }

      if (NS_FAILED(mKey->SetPrivateKey(privKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PRIVATE);
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW) ||
               mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI) ||
               (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
                !mJwk.mD.WasPassed())) {
      // Public key import
      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW)) {
        pubKey = CryptoKey::PublicOKPKeyFromRaw(mKeyData, mNamedCurve);
      } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
        pubKey = CryptoKey::PublicKeyFromSpki(mKeyData);
      } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
        pubKey = CryptoKey::PublicKeyFromJwk(mJwk, mAlgName);
      } else {
        MOZ_ASSERT(false);
      }

      if (!pubKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
        if (pubKey->keyType != edKey && pubKey->keyType != ecMontKey) {
          return NS_ERROR_DOM_DATA_ERR;
        }
        if (!CheckEncodedParameters(&pubKey->u.ec.DEREncodedParams)) {
          return NS_ERROR_DOM_OPERATION_ERR;
        }

        SECOidTag tag;
        if (!FindOIDTagForEncodedParameters(&pubKey->u.ec.DEREncodedParams,
                                            &tag)) {
          return NS_ERROR_DOM_OPERATION_ERR;
        }

        // Find a matching and supported named curve.
        if (!MapOIDTagToNamedCurve(tag, mNamedCurve)) {
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        }
      }

      if (NS_FAILED(mKey->SetPublicKey(pubKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PUBLIC);
    } else {
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    }

    // Extract 'crv' and "alg" parameter from JWKs.
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
        if (mJwk.mAlg.WasPassed() &&
            !mJwk.mAlg.Value().EqualsLiteral(JWK_ALG_EDDSA) &&
            !mJwk.mAlg.Value().EqualsLiteral(JWK_ALG_ED25519)) {
          return NS_ERROR_DOM_DATA_ERR;
        }
      }
      if (!NormalizeToken(mJwk.mCrv.Value(), mNamedCurve)) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }
    }

    return NS_OK;
  }

  virtual nsresult AfterCrypto() override {
    // Only Ed25519 is supported.
    uint32_t privateAllowedUsages = 0;
    uint32_t publicAllowedUsages = 0;

    if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
      privateAllowedUsages = CryptoKey::DERIVEKEY | CryptoKey::DERIVEBITS;
      publicAllowedUsages = 0;
    } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
      privateAllowedUsages = CryptoKey::SIGN;
      publicAllowedUsages = CryptoKey::VERIFY;
    }

    // Check permissions for the requested operation
    if ((mKey->GetKeyType() == CryptoKey::PUBLIC &&
         mKey->HasUsageOtherThan(publicAllowedUsages))) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    if ((mKey->GetKeyType() == CryptoKey::PRIVATE &&
         mKey->HasUsageOtherThan(privateAllowedUsages))) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    if (mKey->GetKeyType() == CryptoKey::PRIVATE && !mKey->HasAnyUsage()) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    mKey->Algorithm().MakeOKP(mAlgName);

    if (mDataIsJwk && !JwkCompatible(mJwk, mKey)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    return NS_OK;
  }
};

class ImportMLKEMKeyTask : public ImportKeyTask {
 public:
  ImportMLKEMKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                     const nsAString& aFormat, const ObjectOrString& aAlgorithm,
                     bool aExtractable, const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
  }

  ImportMLKEMKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                     const nsAString& aFormat, JS::Handle<JSObject*> aKeyData,
                     const ObjectOrString& aAlgorithm, bool aExtractable,
                     const Sequence<nsString>& aKeyUsages) {
    Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable, aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    SetKeyData(aCx, aKeyData);
    NS_ENSURE_SUCCESS_VOID(mEarlyRv);
    if (mDataIsJwk && !mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
      return;
    }
  }

  void Init(nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
            const ObjectOrString& aAlgorithm, bool aExtractable,
            const Sequence<nsString>& aKeyUsages) {
    ImportKeyTask::Init(aGlobal, aCx, aFormat, aAlgorithm, aExtractable,
                        aKeyUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (!GetMLKEMParams(mAlgName, mMLKEMParams)) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }

    mKey->Algorithm().MakeMlKem(mAlgName);
  }

 private:
  MLKEMParams mMLKEMParams;

  virtual nsresult DoCrypto() override {
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_PUBLIC)) {
      // If usages contains an entry which is not "encapsulateKey" or
      // "encapsulateBits" then throw a SyntaxError.
      if (mKey->HasUsageOtherThan(CryptoKey::ENCAPSULATEKEY |
                                  CryptoKey::ENCAPSULATEBITS)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      UniqueSECKEYPublicKey pubKey =
          CryptoKey::PublicMLKEMKeyFromRaw(mKeyData, mMLKEMParams);
      if (!pubKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (NS_FAILED(mKey->SetPublicKey(pubKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PUBLIC);
      return NS_OK;
    }

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
      // If usages contains an entry which is not "encapsulateKey" or
      // "encapsulateBits" then throw a SyntaxError.
      if (mKey->HasUsageOtherThan(CryptoKey::ENCAPSULATEKEY |
                                  CryptoKey::ENCAPSULATEBITS)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      UniqueSECKEYPublicKey pubKey = CryptoKey::PublicKeyFromSpki(mKeyData);
      if (!pubKey || pubKey->keyType != kyberKey ||
          pubKey->u.kyber.params != mMLKEMParams.mKyberParams) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (NS_FAILED(mKey->SetPublicKey(pubKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PUBLIC);
      return NS_OK;
    }

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_SEED)) {
      // If usages contains an entry which is not "decapsulateKey" or
      // "decapsulateBits" then throw a SyntaxError.
      if (mKey->HasUsageOtherThan(CryptoKey::DECAPSULATEKEY |
                                  CryptoKey::DECAPSULATEBITS)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      UniqueSECKEYPrivateKey privKey =
          CryptoKey::PrivateMLKEMKeyFromSeed(mKeyData, mMLKEMParams);
      if (!privKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (NS_FAILED(mKey->SetPrivateKey(privKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PRIVATE);

      // If the [[type]] internal slot of result is "secret" or "private" and
      // usages is empty, then throw a SyntaxError.
      if (!mKey->HasAnyUsage()) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }
      return NS_OK;
    }

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8)) {
      // If usages contains an entry which is not "decapsulateKey" or
      // "decapsulateBits" then throw a SyntaxError.
      if (mKey->HasUsageOtherThan(CryptoKey::DECAPSULATEKEY |
                                  CryptoKey::DECAPSULATEBITS)) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }

      UniqueSECKEYPrivateKey privKey = CryptoKey::PrivateKeyFromPkcs8(mKeyData);
      if (!privKey || !MatchesParameterSet(privKey.get())) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      // WebCrypto does not allow the expandedKey form, which has no seed.
      CryptoBuffer seed;
      if (NS_FAILED(CryptoKey::PrivateMLKEMKeyToSeed(privKey.get(), seed))) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      if (NS_FAILED(mKey->SetPrivateKey(privKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PRIVATE);

      // If the [[type]] internal slot of result is "secret" or "private" and
      // usages is empty, then throw a SyntaxError.
      if (!mKey->HasAnyUsage()) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }
      return NS_OK;
    }

    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      return ImportJwk();
    }

    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  bool MatchesParameterSet(SECKEYPrivateKey* aPrivKey) {
    UniqueSECKEYPublicKey pubKey(SECKEY_ConvertToPublicKey(aPrivKey));
    return pubKey && pubKey->keyType == kyberKey &&
           pubKey->u.kyber.params == mMLKEMParams.mKyberParams;
  }

  nsresult ImportJwk() {
    // If the priv field of jwk is present and if usages contains an entry
    // which is not "decapsulateKey" or "decapsulateBits" then throw a
    // SyntaxError.
    if (mJwk.mPriv.WasPassed() &&
        mKey->HasUsageOtherThan(CryptoKey::DECAPSULATEKEY |
                                CryptoKey::DECAPSULATEBITS)) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    // If the priv field of jwk is not present and if usages contains an entry
    // which is not "encapsulateKey" or "encapsulateBits" then throw a
    // SyntaxError.
    if (!mJwk.mPriv.WasPassed() &&
        mKey->HasUsageOtherThan(CryptoKey::ENCAPSULATEKEY |
                                CryptoKey::ENCAPSULATEBITS)) {
      return NS_ERROR_DOM_SYNTAX_ERR;
    }

    if (!mJwk.mKty.EqualsLiteral(JWK_TYPE_AKP) || !mJwk.mAlg.WasPassed() ||
        !mJwk.mAlg.Value().Equals(mAlgName)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    // If usages is non-empty and the use field of jwk is present and is not
    // equal to "enc", then throw a DataError.
    if (mKey->HasAnyUsage() && mJwk.mUse.WasPassed() &&
        !mJwk.mUse.Value().EqualsLiteral(JWK_USE_ENC)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    if (!JwkCompatible(mJwk, mKey)) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    if (mJwk.mPriv.WasPassed()) {
      CryptoBuffer seed;
      if (NS_FAILED(seed.FromJwkBase64(mJwk.mPriv.Value()))) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      UniqueSECKEYPrivateKey privKey =
          CryptoKey::PrivateMLKEMKeyFromSeed(seed, mMLKEMParams);
      if (!privKey) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      // The spec requires "pub" to match the key derived from "priv".
      UniqueSECKEYPublicKey derived(SECKEY_ConvertToPublicKey(privKey.get()));
      if (!derived || derived->keyType != kyberKey) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      CryptoBuffer derivedPub;
      if (NS_FAILED(
              CryptoKey::PublicMLKEMKeyToRaw(derived.get(), derivedPub))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      CryptoBuffer pub;
      if (!mJwk.mPub.WasPassed() ||
          NS_FAILED(pub.FromJwkBase64(mJwk.mPub.Value())) ||
          derivedPub != pub) {
        return NS_ERROR_DOM_DATA_ERR;
      }

      if (NS_FAILED(mKey->SetPrivateKey(privKey.get()))) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      mKey->SetType(CryptoKey::PRIVATE);

      // If the [[type]] internal slot of result is "secret" or "private" and
      // usages is empty, then throw a SyntaxError.
      if (!mKey->HasAnyUsage()) {
        return NS_ERROR_DOM_SYNTAX_ERR;
      }
      return NS_OK;
    }

    CryptoBuffer pub;
    if (!mJwk.mPub.WasPassed() ||
        NS_FAILED(pub.FromJwkBase64(mJwk.mPub.Value()))) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    UniqueSECKEYPublicKey pubKey =
        CryptoKey::PublicMLKEMKeyFromRaw(pub, mMLKEMParams);
    if (!pubKey) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    if (NS_FAILED(mKey->SetPublicKey(pubKey.get()))) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    mKey->SetType(CryptoKey::PUBLIC);
    return NS_OK;
  }
};

class ExportKeyTask : public WebCryptoTask {
 public:
  ExportKeyTask(const nsAString& aFormat, CryptoKey& aKey)
      : mFormat(aFormat),
        mPrivateKey(aKey.GetPrivateKey()),
        mPublicKey(aKey.GetPublicKey()),
        mKeyType(aKey.GetKeyType()),
        mExtractable(aKey.Extractable()),
        mAlg(aKey.Algorithm().JwkAlg()) {
    aKey.GetUsages(mKeyUsages);

    if (!mSymKey.Assign(aKey.GetSymKey())) {
      mEarlyRv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }
  }

 protected:
  nsString mFormat;
  CryptoBuffer mSymKey;
  UniqueSECKEYPrivateKey mPrivateKey;
  UniqueSECKEYPublicKey mPublicKey;
  CryptoKey::KeyType mKeyType;
  bool mExtractable;
  nsString mAlg;
  nsTArray<nsString> mKeyUsages;
  CryptoBuffer mResult;
  JsonWebKey mJwk;

 private:
  virtual nsresult DoCrypto() override {
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW)) {
      if (mPublicKey && mPublicKey->keyType == dhKey) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      if (mPublicKey &&
          (mPublicKey->keyType == ecKey || mPublicKey->keyType == edKey ||
           mPublicKey->keyType == ecMontKey)) {
        nsresult rv = CryptoKey::PublicECKeyToRaw(mPublicKey.get(), mResult);
        if (NS_FAILED(rv)) {
          return NS_ERROR_DOM_OPERATION_ERR;
        }
        return NS_OK;
      }

      if (!mResult.Assign(mSymKey)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      if (mResult.Length() == 0) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      return NS_OK;
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8)) {
      if (!mPrivateKey) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      switch (mPrivateKey->keyType) {
        case rsaKey:
        case edKey:
        case ecKey:
        case ecMontKey:
        case kyberKey: {
          nsresult rv =
              CryptoKey::PrivateKeyToPkcs8(mPrivateKey.get(), mResult);
          if (NS_FAILED(rv)) {
            return NS_ERROR_DOM_OPERATION_ERR;
          }
          return NS_OK;
        }
        default:
          return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_PUBLIC)) {
      if (mKeyType != CryptoKey::PUBLIC) {
        return NS_ERROR_DOM_INVALID_ACCESS_ERR;
      }

      if (!mPublicKey || mPublicKey->keyType != kyberKey) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      return CryptoKey::PublicMLKEMKeyToRaw(mPublicKey.get(), mResult);
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_SEED)) {
      if (mKeyType != CryptoKey::PRIVATE) {
        return NS_ERROR_DOM_INVALID_ACCESS_ERR;
      }

      if (!mPrivateKey || mPrivateKey->keyType != kyberKey) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      return CryptoKey::PrivateMLKEMKeyToSeed(mPrivateKey.get(), mResult);
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI)) {
      if (!mPublicKey) {
        return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      }

      return CryptoKey::PublicKeyToSpki(mPublicKey.get(), mResult);
    } else if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      if (mKeyType == CryptoKey::SECRET) {
        nsString k;
        nsresult rv = mSymKey.ToJwkBase64(k);
        if (NS_FAILED(rv)) {
          return NS_ERROR_DOM_OPERATION_ERR;
        }
        mJwk.mK.Construct(k);
        mJwk.mKty = NS_LITERAL_STRING_FROM_CSTRING(JWK_TYPE_SYMMETRIC);
      } else if (mKeyType == CryptoKey::PUBLIC) {
        if (!mPublicKey) {
          return NS_ERROR_DOM_UNKNOWN_ERR;
        }

        nsresult rv = CryptoKey::PublicKeyToJwk(mPublicKey.get(), mJwk);
        if (NS_FAILED(rv)) {
          return rv;
        }
      } else if (mKeyType == CryptoKey::PRIVATE) {
        if (!mPrivateKey) {
          return NS_ERROR_DOM_UNKNOWN_ERR;
        }

        nsresult rv = CryptoKey::PrivateKeyToJwk(mPrivateKey.get(), mJwk);
        if (NS_FAILED(rv)) {
          return rv;
        }
      }

      if (!mAlg.IsEmpty()) {
        mJwk.mAlg.Construct(mAlg);
      }

      mJwk.mExt.Construct(mExtractable);

      mJwk.mKey_ops.Construct();
      if (!mJwk.mKey_ops.Value().AppendElements(mKeyUsages, fallible)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      return NS_OK;
    }

    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  // Returns mResult as an ArrayBufferView or JWK, as appropriate
  virtual void Resolve() override {
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      mResultPromise->MaybeResolve(mJwk);
      return;
    }

    TypedArrayCreator<ArrayBuffer> ret(mResult);
    mResultPromise->MaybeResolve(ret);
  }
};

class GenerateSymmetricKeyTask : public WebCryptoTask {
 public:
  GenerateSymmetricKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                           const nsString& aAlgName,
                           const ObjectOrString& aAlgorithm, bool aExtractable,
                           const Sequence<nsString>& aKeyUsages) {
    // Create an empty key and set easy attributes
    mKey = MakeRefPtr<CryptoKey>(aGlobal);
    mKey->SetExtractable(aExtractable);
    mKey->SetType(CryptoKey::SECRET);

    // Construct an appropriate KeyAlorithm
    if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
        aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
        aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM) ||
        aAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW)) {
      mEarlyRv = GetKeyLengthForAlgorithm(aCx, aAlgName, aAlgorithm, mLength);
      if (NS_FAILED(mEarlyRv)) {
        return;
      }
      mKey->Algorithm().MakeAes(aAlgName, mLength);

    } else if (aAlgName.EqualsLiteral(WEBCRYPTO_ALG_HMAC)) {
      RootedDictionary<HmacKeyGenParams> params(aCx);
      mEarlyRv = Coerce(aCx, params, aAlgorithm);
      if (NS_FAILED(mEarlyRv)) {
        return;
      }

      nsString hashName;
      mEarlyRv = GetAlgorithmName(aCx, params.mHash, hashName);
      if (NS_FAILED(mEarlyRv)) {
        return;
      }

      if (params.mLength.WasPassed()) {
        mLength = params.mLength.Value();
      } else {
        mLength = MapHashAlgorithmNameToBlockSize(hashName);
      }

      if (mLength == 0) {
        mEarlyRv = NS_ERROR_DOM_DATA_ERR;
        return;
      }

      mKey->Algorithm().MakeHmac(mLength, hashName);
    } else {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }

    // Add key usages
    mKey->ClearUsages();
    for (uint32_t i = 0; i < aKeyUsages.Length(); ++i) {
      mEarlyRv = mKey->AddAllowedUsageIntersecting(aKeyUsages[i], aAlgName);
      if (NS_FAILED(mEarlyRv)) {
        return;
      }
    }
    if (!mKey->HasAnyUsage()) {
      mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
      return;
    }

    mLength = mLength >> 3;  // bits to bytes
    mMechanism = mKey->Algorithm().Mechanism();
    // SetSymKey done in Resolve, after we've done the keygen
  }

 private:
  RefPtr<CryptoKey> mKey;
  size_t mLength;
  CK_MECHANISM_TYPE mMechanism;
  CryptoBuffer mKeyData;

  virtual nsresult DoCrypto() override {
    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    MOZ_ASSERT(slot.get());

    UniquePK11SymKey symKey(
        PK11_KeyGen(slot.get(), mMechanism, nullptr, mLength, nullptr));
    if (!symKey) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    nsresult rv = MapSECStatus(PK11_ExtractKeyValue(symKey.get()));
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    // This doesn't leak, because the SECItem* returned by PK11_GetKeyData
    // just refers to a buffer managed by symKey.  The assignment copies the
    // data, so mKeyData manages one copy, while symKey manages another.
    ATTEMPT_BUFFER_ASSIGN(mKeyData, PK11_GetKeyData(symKey.get()));
    return NS_OK;
  }

  virtual void Resolve() override {
    if (NS_SUCCEEDED(mKey->SetSymKey(mKeyData))) {
      mResultPromise->MaybeResolve(mKey);
    } else {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
    }
  }

  virtual void Cleanup() override { mKey = nullptr; }
};

class DeriveX25519BitsTask : public ReturnArrayBufferViewTask {
 public:
  DeriveX25519BitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                       CryptoKey& aKey, const Nullable<uint32_t>& aLength)
      : mLength(aLength), mPrivKey(aKey.GetPrivateKey()) {
    Init(aCx, aAlgorithm, aKey);
  }

  DeriveX25519BitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                       CryptoKey& aKey, const nsString& aTargetAlgName,
                       const ObjectOrString& aTargetAlgorithm)
      : mPrivKey(aKey.GetPrivateKey()) {
    Maybe<size_t> lengthInBits;
    mEarlyRv = GetKeyLengthForAlgorithmIfSpecified(
        aCx, aTargetAlgName, aTargetAlgorithm, lengthInBits);
    if (lengthInBits.isNothing()) {
      mLength.SetNull();
    } else {
      mLength.SetValue(*lengthInBits);
    }
    if (NS_SUCCEEDED(mEarlyRv)) {
      Init(aCx, aAlgorithm, aKey);
    }
  }

  void Init(JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_X25519);

    // Check that we have a private key.
    if (!mPrivKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }

    // If specified, length must be a multiple of 8.
    if (!mLength.IsNull()) {
      if (mLength.Value() % 8) {
        mEarlyRv = NS_ERROR_DOM_DATA_ERR;
        return;
      }
      mLength.SetValue(mLength.Value() >> 3);  // bits to bytes
    }

    // Retrieve the peer's public key.
    RootedDictionary<EcdhKeyDeriveParams> params(aCx);
    mEarlyRv = Coerce(aCx, params, aAlgorithm);

    if (NS_FAILED(mEarlyRv)) {
      /* The returned code is installed by Coerce function. */
      return;
    }

    CHECK_KEY_ALGORITHM(params.mPublic->Algorithm(), WEBCRYPTO_ALG_X25519);

    CryptoKey* publicKey = params.mPublic;
    mPubKey = publicKey->GetPublicKey();
    if (!mPubKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }
  }

 private:
  Nullable<uint32_t> mLength;
  UniqueSECKEYPrivateKey mPrivKey;
  UniqueSECKEYPublicKey mPubKey;

  virtual nsresult DoCrypto() override {
    // CKM_SHA512_HMAC and CKA_SIGN are key type and usage attributes of the
    // derived symmetric key and don't matter because we ignore them anyway.

    // Derive Bits requires checking that the generated key is not all-zero
    // value. See:
    // https://wicg.github.io/webcrypto-secure-curves/#x25519-operations This
    // step is performed internally inside PK11_PubDeriveWithKDF function.
    UniquePK11SymKey symKey(
        PK11_PubDeriveWithKDF(mPrivKey.get(), mPubKey.get(), PR_FALSE, nullptr,
                              nullptr, CKM_ECDH1_DERIVE, CKM_SHA512_HMAC,
                              CKA_DERIVE, 0, CKD_NULL, nullptr, nullptr));

    if (!symKey.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    nsresult rv = MapSECStatus(PK11_ExtractKeyValue(symKey.get()));
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // This doesn't leak, because the SECItem* returned by PK11_GetKeyData
    // just refers to a buffer managed by symKey. The assignment copies the
    // data, so mResult manages one copy, while symKey manages another.
    ATTEMPT_BUFFER_ASSIGN(mResult, PK11_GetKeyData(symKey.get()));

    if (!mLength.IsNull()) {
      if (mLength.Value() > mResult.Length()) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
      if (!mResult.SetLength(mLength.Value(), fallible)) {
        return NS_ERROR_DOM_UNKNOWN_ERR;
      }
    }

    return NS_OK;
  }
};

GenerateAsymmetricKeyTask::GenerateAsymmetricKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const nsString& aAlgName,
    const ObjectOrString& aAlgorithm, bool aExtractable,
    const Sequence<nsString>& aKeyUsages)
    : mKeyPair(std::make_unique<CryptoKeyPair>()),
      mMechanism(CKM_INVALID_MECHANISM),
      mRsaParams(),
      mDhParams(),
      mMLKEMParameterSet() {
  Init(aGlobal, aCx, aAlgName, aAlgorithm, aExtractable, aKeyUsages);
}

GenerateAsymmetricKeyTask::GenerateAsymmetricKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const ObjectOrString& aAlgorithm,
    bool aExtractable, const Sequence<nsString>& aKeyUsages)
    : mKeyPair(std::make_unique<CryptoKeyPair>()),
      mMechanism(CKM_INVALID_MECHANISM),
      mRsaParams(),
      mDhParams(),
      mMLKEMParameterSet() {
  nsString algName;
  mEarlyRv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(mEarlyRv)) {
    return;
  }

  Init(aGlobal, aCx, algName, aAlgorithm, aExtractable, aKeyUsages);
}

void GenerateAsymmetricKeyTask::Init(nsIGlobalObject* aGlobal, JSContext* aCx,
                                     const nsString& aAlgName,
                                     const ObjectOrString& aAlgorithm,
                                     bool aExtractable,
                                     const Sequence<nsString>& aKeyUsages) {
  mAlgName = aAlgName;

  mArena = UniquePLArenaPool(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!mArena) {
    mEarlyRv = NS_ERROR_DOM_UNKNOWN_ERR;
    return;
  }

  // Create an empty key pair and set easy attributes
  mKeyPair->mPrivateKey = MakeRefPtr<CryptoKey>(aGlobal);
  mKeyPair->mPublicKey = MakeRefPtr<CryptoKey>(aGlobal);

  // Construct an appropriate KeyAlorithm
  uint32_t privateAllowedUsages = 0, publicAllowedUsages = 0;
  if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
      mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP) ||
      mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS)) {
    RootedDictionary<RsaHashedKeyGenParams> params(aCx);
    mEarlyRv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    // Pull relevant info
    uint32_t modulusLength = params.mModulusLength;
    CryptoBuffer publicExponent;
    ATTEMPT_BUFFER_INIT(publicExponent, params.mPublicExponent);
    nsString hashName;
    mEarlyRv = GetAlgorithmName(aCx, params.mHash, hashName);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    // Create algorithm
    if (!mKeyPair->mPublicKey->Algorithm().MakeRsa(mAlgName, modulusLength,
                                                   publicExponent, hashName)) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }
    if (!mKeyPair->mPrivateKey->Algorithm().MakeRsa(mAlgName, modulusLength,
                                                    publicExponent, hashName)) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }
    mMechanism = CKM_RSA_PKCS_KEY_PAIR_GEN;

    // Set up params struct
    mRsaParams.keySizeInBits = modulusLength;
    bool converted = publicExponent.GetBigIntValue(mRsaParams.pe);
    if (!converted) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }
  } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDH) ||
             mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
    RootedDictionary<EcKeyGenParams> params(aCx);
    mEarlyRv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(mEarlyRv)) {
      mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
      return;
    }

    if (!NormalizeToken(params.mNamedCurve, mNamedCurve)) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }

    // Create algorithm.
    mKeyPair->mPublicKey->Algorithm().MakeEc(mAlgName, mNamedCurve);
    mKeyPair->mPrivateKey->Algorithm().MakeEc(mAlgName, mNamedCurve);
    mMechanism = CKM_EC_KEY_PAIR_GEN;
  }

  else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
    mKeyPair->mPublicKey->Algorithm().MakeOKP(mAlgName);
    mKeyPair->mPrivateKey->Algorithm().MakeOKP(mAlgName);
    mMechanism = CKM_EC_MONTGOMERY_KEY_PAIR_GEN;
    mNamedCurve.AssignLiteral(WEBCRYPTO_NAMED_CURVE_CURVE25519);
  }

  else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
    mKeyPair->mPublicKey->Algorithm().MakeOKP(mAlgName);
    mKeyPair->mPrivateKey->Algorithm().MakeOKP(mAlgName);
    mMechanism = CKM_EC_EDWARDS_KEY_PAIR_GEN;
    mNamedCurve.AssignLiteral(WEBCRYPTO_NAMED_CURVE_ED25519);
  }

  else if (IsMLKEMAlgorithm(mAlgName)) {
    MLKEMParams mlKemParams;
    if (!GetMLKEMParams(mAlgName, mlKemParams)) {
      mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
      return;
    }

    mKeyPair->mPublicKey->Algorithm().MakeMlKem(mAlgName);
    mKeyPair->mPrivateKey->Algorithm().MakeMlKem(mAlgName);
    mMechanism = CKM_ML_KEM_KEY_PAIR_GEN;
    mMLKEMParameterSet = mlKemParams.mParameterSet;
  }

  else {
    mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
    return;
  }

  // Set key usages.
  if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
      mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS) ||
      mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA) ||
      mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
    privateAllowedUsages = CryptoKey::SIGN;
    publicAllowedUsages = CryptoKey::VERIFY;
  } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP)) {
    privateAllowedUsages = CryptoKey::DECRYPT | CryptoKey::UNWRAPKEY;
    publicAllowedUsages = CryptoKey::ENCRYPT | CryptoKey::WRAPKEY;
  } else if (mAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDH) ||
             mAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
    privateAllowedUsages = CryptoKey::DERIVEKEY | CryptoKey::DERIVEBITS;
    publicAllowedUsages = 0;
  } else if (IsMLKEMAlgorithm(mAlgName)) {
    privateAllowedUsages =
        CryptoKey::DECAPSULATEKEY | CryptoKey::DECAPSULATEBITS;
    publicAllowedUsages =
        CryptoKey::ENCAPSULATEKEY | CryptoKey::ENCAPSULATEBITS;
  } else {
    MOZ_ASSERT(false);  // This shouldn't happen.
  }

  mKeyPair->mPrivateKey->SetExtractable(aExtractable);
  mKeyPair->mPrivateKey->SetType(CryptoKey::PRIVATE);

  mKeyPair->mPublicKey->SetExtractable(true);
  mKeyPair->mPublicKey->SetType(CryptoKey::PUBLIC);

  mKeyPair->mPrivateKey->ClearUsages();
  mKeyPair->mPublicKey->ClearUsages();
  for (uint32_t i = 0; i < aKeyUsages.Length(); ++i) {
    mEarlyRv = mKeyPair->mPrivateKey->AddAllowedUsageIntersecting(
        aKeyUsages[i], mAlgName, privateAllowedUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    mEarlyRv = mKeyPair->mPublicKey->AddAllowedUsageIntersecting(
        aKeyUsages[i], mAlgName, publicAllowedUsages);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }
  }
}

nsresult GenerateAsymmetricKeyTask::DoCrypto() {
  MOZ_ASSERT(mKeyPair);

  UniquePK11SlotInfo slot(PK11_GetInternalSlot());
  MOZ_ASSERT(slot.get());

  void* param;
  switch (mMechanism) {
    case CKM_RSA_PKCS_KEY_PAIR_GEN:
      param = &mRsaParams;
      break;
    case CKM_DH_PKCS_KEY_PAIR_GEN:
      param = &mDhParams;
      break;
    case CKM_EC_MONTGOMERY_KEY_PAIR_GEN:
    case CKM_EC_EDWARDS_KEY_PAIR_GEN:
    case CKM_EC_KEY_PAIR_GEN: {
      param = CreateECParamsForCurve(mNamedCurve, mArena.get());
      if (!param) {
        return NS_ERROR_DOM_UNKNOWN_ERR;
      }
      break;
    }
    case CKM_ML_KEM_KEY_PAIR_GEN:
      param = &mMLKEMParameterSet;
      break;
    default:
      return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  mPrivateKey = UniqueSECKEYPrivateKey(PK11_GenerateKeyPair(
      slot.get(), mMechanism, param, TempPtrToSetter(&mPublicKey), PR_FALSE,
      PR_FALSE, nullptr));

  if (!mPrivateKey.get() || !mPublicKey.get()) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  // If no usages ended up being allowed, SyntaxError
  if (!mKeyPair->mPrivateKey->HasAnyUsage()) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  nsresult rv = mKeyPair->mPrivateKey->SetPrivateKey(mPrivateKey.get());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);
  rv = mKeyPair->mPublicKey->SetPublicKey(mPublicKey.get());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);
  // PK11_GenerateKeyPair() does not set a CKA_EC_POINT attribute on the
  // private key, we need this later when exporting to PKCS8 and JWK though.
  if (mMechanism == CKM_EC_KEY_PAIR_GEN ||
      mMechanism == CKM_EC_MONTGOMERY_KEY_PAIR_GEN ||
      mMechanism == CKM_EC_EDWARDS_KEY_PAIR_GEN) {
    rv = mKeyPair->mPrivateKey->AddPublicKeyData(mPublicKey.get());
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_OPERATION_ERR);
  }

  return NS_OK;
}

void GenerateAsymmetricKeyTask::Resolve() {
  mResultPromise->MaybeResolve(*mKeyPair);
}

void GenerateAsymmetricKeyTask::Cleanup() { mKeyPair = nullptr; }

class DeriveHkdfBitsTask : public ReturnArrayBufferViewTask {
 public:
  DeriveHkdfBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                     CryptoKey& aKey, const Nullable<uint32_t>& aLength)
      : mMechanism(CKM_INVALID_MECHANISM) {
    Init(aCx, aAlgorithm, aKey, aLength);
  }

  DeriveHkdfBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                     CryptoKey& aKey, const nsString& aTargetAlgName,
                     const ObjectOrString& aTargetAlgorithm)
      : mLengthInBits(0), mLengthInBytes(0), mMechanism(CKM_INVALID_MECHANISM) {
    size_t length;
    mEarlyRv =
        GetKeyLengthForAlgorithm(aCx, aTargetAlgName, aTargetAlgorithm, length);

    const Nullable<uint32_t> keyLength(length);
    if (NS_SUCCEEDED(mEarlyRv)) {
      Init(aCx, aAlgorithm, aKey, keyLength);
    }
  }

  void Init(JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
            const Nullable<uint32_t>& aLength) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_HKDF);

    if (!mSymKey.Assign(aKey.GetSymKey())) {
      mEarlyRv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }

    RootedDictionary<HkdfParams> params(aCx);
    mEarlyRv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(mEarlyRv)) {
      mEarlyRv = NS_ERROR_DOM_TYPE_MISMATCH_ERR;
      return;
    }

    // length must be non-null and multiple of eight.
    if (aLength.IsNull() || aLength.Value() % 8 != 0) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }

    // Extract the hash algorithm.
    nsString hashName;
    mEarlyRv = GetAlgorithmName(aCx, params.mHash, hashName);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    // Check the given hash algorithm.
    switch (MapAlgorithmNameToMechanism(hashName)) {
      case CKM_SHA_1:
        mMechanism = CKM_NSS_HKDF_SHA1;
        break;
      case CKM_SHA256:
        mMechanism = CKM_NSS_HKDF_SHA256;
        break;
      case CKM_SHA384:
        mMechanism = CKM_NSS_HKDF_SHA384;
        break;
      case CKM_SHA512:
        mMechanism = CKM_NSS_HKDF_SHA512;
        break;
      default:
        mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        return;
    }

    ATTEMPT_BUFFER_INIT(mSalt, params.mSalt)
    ATTEMPT_BUFFER_INIT(mInfo, params.mInfo)
    mLengthInBytes = ceil((double)aLength.Value() / 8);
    mLengthInBits = aLength.Value();
  }

 private:
  size_t mLengthInBits;
  size_t mLengthInBytes;
  CryptoBuffer mSalt;
  CryptoBuffer mInfo;
  CryptoBuffer mSymKey;
  CK_MECHANISM_TYPE mMechanism;

  virtual nsresult DoCrypto() override {
    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // Import the key
    SECItem keyItem = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &keyItem, mSymKey);

    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    if (!slot.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    UniquePK11SymKey baseKey(PK11_ImportSymKey(slot.get(), mMechanism,
                                               PK11_OriginUnwrap, CKA_WRAP,
                                               &keyItem, nullptr));
    if (!baseKey) {
      return NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }

    // We are going to return an empty string, so we can skip the rest.
    if (mLengthInBits == 0) {
      mResult.Clear();
      return NS_OK;
    }

    SECItem salt = {siBuffer, nullptr, 0};
    SECItem info = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &salt, mSalt);
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &info, mInfo);

    CK_NSS_HKDFParams hkdfParams = {true, salt.data, salt.len,
                                    true, info.data, info.len};
    SECItem params = {siBuffer, (unsigned char*)&hkdfParams,
                      sizeof(hkdfParams)};

    // CKM_SHA512_HMAC and CKA_SIGN are key type and usage attributes of the
    // derived symmetric key and don't matter because we ignore them anyway.
    UniquePK11SymKey symKey(PK11_Derive(baseKey.get(), mMechanism, &params,
                                        CKM_SHA512_HMAC, CKA_SIGN,
                                        mLengthInBytes));

    if (!symKey.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    nsresult rv = MapSECStatus(PK11_ExtractKeyValue(symKey.get()));
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // This doesn't leak, because the SECItem* returned by PK11_GetKeyData
    // just refers to a buffer managed by symKey. The assignment copies the
    // data, so mResult manages one copy, while symKey manages another.
    ATTEMPT_BUFFER_ASSIGN(mResult, PK11_GetKeyData(symKey.get()));

    if (mLengthInBytes > mResult.Length()) {
      return NS_ERROR_DOM_DATA_ERR;
    }

    if (!mResult.SetLength(mLengthInBytes, fallible)) {
      return NS_ERROR_DOM_UNKNOWN_ERR;
    }

    return NS_OK;
  }
};

class DerivePbkdfBitsTask : public ReturnArrayBufferViewTask {
 public:
  DerivePbkdfBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                      CryptoKey& aKey, const Nullable<uint32_t>& aLength)
      : mHashOidTag(SEC_OID_UNKNOWN) {
    Init(aCx, aAlgorithm, aKey, aLength);
  }

  DerivePbkdfBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                      CryptoKey& aKey, const nsString& aTargetAlgName,
                      const ObjectOrString& aTargetAlgorithm)
      : mLength(0), mIterations(0), mHashOidTag(SEC_OID_UNKNOWN) {
    size_t length;
    mEarlyRv =
        GetKeyLengthForAlgorithm(aCx, aTargetAlgName, aTargetAlgorithm, length);

    const Nullable<uint32_t> keyLength(length);
    if (NS_SUCCEEDED(mEarlyRv)) {
      Init(aCx, aAlgorithm, aKey, keyLength);
    }
  }

  void Init(JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
            const Nullable<uint32_t>& aLength) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_PBKDF2);

    if (!mSymKey.Assign(aKey.GetSymKey())) {
      mEarlyRv = NS_ERROR_OUT_OF_MEMORY;
      return;
    }

    RootedDictionary<Pbkdf2Params> params(aCx);
    mEarlyRv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(mEarlyRv)) {
      mEarlyRv = NS_ERROR_DOM_SYNTAX_ERR;
      return;
    }

    // length must be non-null and multiple of eight.
    if (aLength.IsNull() || aLength.Value() % 8) {
      mEarlyRv = NS_ERROR_DOM_OPERATION_ERR;
      return;
    }

    // Extract the hash algorithm.
    nsString hashName;
    mEarlyRv = GetAlgorithmName(aCx, params.mHash, hashName);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    // Check the given hash algorithm.
    switch (MapAlgorithmNameToMechanism(hashName)) {
      case CKM_SHA_1:
        mHashOidTag = SEC_OID_HMAC_SHA1;
        break;
      case CKM_SHA256:
        mHashOidTag = SEC_OID_HMAC_SHA256;
        break;
      case CKM_SHA384:
        mHashOidTag = SEC_OID_HMAC_SHA384;
        break;
      case CKM_SHA512:
        mHashOidTag = SEC_OID_HMAC_SHA512;
        break;
      default:
        mEarlyRv = NS_ERROR_DOM_NOT_SUPPORTED_ERR;
        return;
    }

    ATTEMPT_BUFFER_INIT(mSalt, params.mSalt)
    mLength = aLength.Value() >> 3;  // bits to bytes
    mIterations = params.mIterations;
  }

 private:
  size_t mLength;
  size_t mIterations;
  CryptoBuffer mSalt;
  CryptoBuffer mSymKey;
  SECOidTag mHashOidTag;

  virtual nsresult DoCrypto() override {
    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    SECItem salt = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &salt, mSalt);
    // PK11_CreatePBEV2AlgorithmID will "helpfully" create PBKDF2 parameters
    // with a random salt if given a SECItem* that is either null or has a null
    // data pointer. This obviously isn't what we want, so we have to fake it
    // out by passing in a SECItem* with a non-null data pointer but with zero
    // length.
    if (!salt.data) {
      MOZ_ASSERT(salt.len == 0);
      salt.data =
          reinterpret_cast<unsigned char*>(PORT_ArenaAlloc(arena.get(), 1));
      if (!salt.data) {
        return NS_ERROR_DOM_UNKNOWN_ERR;
      }
    }

    // Always pass in cipherAlg=SEC_OID_HMAC_SHA1 (i.e. PBMAC1) as this
    // parameter is unused for key generation. It is currently only used
    // for PBKDF2 authentication or key (un)wrapping when specifying an
    // encryption algorithm (PBES2).
    UniqueSECAlgorithmID algID(
        PK11_CreatePBEV2AlgorithmID(SEC_OID_PKCS5_PBKDF2, SEC_OID_HMAC_SHA1,
                                    mHashOidTag, mLength, mIterations, &salt));

    if (!algID) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // We are going to return an empty string, so we can skip the rest.
    if (mLength == 0) {
      mResult.Clear();
      return NS_OK;
    }

    UniquePK11SlotInfo slot(PK11_GetInternalSlot());
    if (!slot.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    SECItem keyItem = {siBuffer, nullptr, 0};
    ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &keyItem, mSymKey);

    UniquePK11SymKey symKey(
        PK11_PBEKeyGen(slot.get(), algID.get(), &keyItem, false, nullptr));
    if (!symKey.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    nsresult rv = MapSECStatus(PK11_ExtractKeyValue(symKey.get()));
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // This doesn't leak, because the SECItem* returned by PK11_GetKeyData
    // just refers to a buffer managed by symKey. The assignment copies the
    // data, so mResult manages one copy, while symKey manages another.
    ATTEMPT_BUFFER_ASSIGN(mResult, PK11_GetKeyData(symKey.get()));
    return NS_OK;
  }
};

template <class DeriveBitsTask>
class DeriveKeyTask : public DeriveBitsTask {
 public:
  DeriveKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                const ObjectOrString& aAlgorithm, CryptoKey& aBaseKey,
                const nsString& aDerivedKeyAlgName,
                const ObjectOrString& aDerivedKeyType, bool aExtractable,
                const Sequence<nsString>& aKeyUsages)
      : DeriveBitsTask(aCx, aAlgorithm, aBaseKey, aDerivedKeyAlgName,
                       aDerivedKeyType) {
    if (NS_FAILED(this->mEarlyRv)) {
      return;
    }

    constexpr auto format =
        NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_FORMAT_RAW);
    mTask = MakeRefPtr<ImportSymmetricKeyTask>(
        aGlobal, aCx, format, aDerivedKeyType, aExtractable, aKeyUsages);
  }

 protected:
  RefPtr<ImportSymmetricKeyTask> mTask;

 private:
  virtual void Resolve() override {
    mTask->SetRawKeyData(this->mResult);
    mTask->DispatchWithPromise(this->mResultPromise);
  }

  virtual void Cleanup() override { mTask = nullptr; }
};
class DeriveEcdhBitsTask : public ReturnArrayBufferViewTask {
 public:
  DeriveEcdhBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                     CryptoKey& aKey, const Nullable<uint32_t>& aLength)
      : mLengthInBits(aLength), mPrivKey(aKey.GetPrivateKey()) {
    Init(aCx, aAlgorithm, aKey);
  }

  DeriveEcdhBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                     CryptoKey& aKey, const nsString& aTargetAlgName,
                     const ObjectOrString& aTargetAlgorithm)
      : mPrivKey(aKey.GetPrivateKey()) {
    Maybe<size_t> lengthInBits;
    mEarlyRv = GetKeyLengthForAlgorithmIfSpecified(
        aCx, aTargetAlgName, aTargetAlgorithm, lengthInBits);
    if (lengthInBits.isNothing()) {
      mLengthInBits.SetNull();
    } else {
      mLengthInBits.SetValue(*lengthInBits);
    }
    if (NS_SUCCEEDED(mEarlyRv)) {
      Init(aCx, aAlgorithm, aKey);
    }
  }

  void Init(JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey) {
    CHECK_KEY_ALGORITHM(aKey.Algorithm(), WEBCRYPTO_ALG_ECDH);

    // Check that we have a private key.
    if (!mPrivKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }

    // Retrieve the peer's public key.
    RootedDictionary<EcdhKeyDeriveParams> params(aCx);
    mEarlyRv = Coerce(aCx, params, aAlgorithm);
    if (NS_FAILED(mEarlyRv)) {
      /* The returned code is installed by Coerce function. */
      return;
    }

    CryptoKey* publicKey = params.mPublic;
    mPubKey = publicKey->GetPublicKey();
    if (!mPubKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }

    CHECK_KEY_ALGORITHM(publicKey->Algorithm(), WEBCRYPTO_ALG_ECDH);

    // Both keys must use the same named curve.
    nsString curve1 = aKey.Algorithm().mEc.mNamedCurve;
    nsString curve2 = publicKey->Algorithm().mEc.mNamedCurve;

    if (!curve1.Equals(curve2)) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }
  }

 private:
  Nullable<uint32_t> mLengthInBits;
  UniqueSECKEYPrivateKey mPrivKey;
  UniqueSECKEYPublicKey mPubKey;

  virtual nsresult DoCrypto() override {
    // CKM_SHA512_HMAC and CKA_SIGN are key type and usage attributes of the
    // derived symmetric key and don't matter because we ignore them anyway.
    UniquePK11SymKey symKey(
        PK11_PubDeriveWithKDF(mPrivKey.get(), mPubKey.get(), PR_FALSE, nullptr,
                              nullptr, CKM_ECDH1_DERIVE, CKM_SHA512_HMAC,
                              CKA_SIGN, 0, CKD_NULL, nullptr, nullptr));

    if (!symKey.get()) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    nsresult rv = MapSECStatus(PK11_ExtractKeyValue(symKey.get()));
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_OPERATION_ERR;
    }

    // This doesn't leak, because the SECItem* returned by PK11_GetKeyData
    // just refers to a buffer managed by symKey. The assignment copies the
    // data, so mResult manages one copy, while symKey manages another.
    ATTEMPT_BUFFER_ASSIGN(mResult, PK11_GetKeyData(symKey.get()));

    if (!mLengthInBits.IsNull()) {
      size_t length = mLengthInBits.Value();
      size_t lengthInBytes = ceil((double)length / 8);  // bits to bytes
      if (lengthInBytes > mResult.Length()) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      if (!mResult.SetLength(lengthInBytes, fallible)) {
        return NS_ERROR_DOM_UNKNOWN_ERR;
      }

      // If the number of bits to derive is not a multiple of 8 we need to
      // zero out the remaining bits that were derived but not requested.
      if (length % 8) {
        mResult[mResult.Length() - 1] &= 0xff << (8 - (length % 8));
      }
    }

    return NS_OK;
  }
};

static nsresult MLKEMEncapsulate(SECKEYPublicKey* aPubKey,
                                 CryptoBuffer& aSharedKey,
                                 CryptoBuffer& aCiphertext) {
  UniquePK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  // PK11_Encapsulate() needs a key that lives on a slot, but
  // SECKEY_CopyPublicKey() drops the session object that the CryptoKey was
  // built from, so put the key back on the internal slot first.
  if (PK11_ImportPublicKey(slot.get(), aPubKey, PR_FALSE) ==
      CK_INVALID_HANDLE) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  PK11SymKey* sharedSecret = nullptr;
  SECItem* ciphertext = nullptr;
  if (PK11_Encapsulate(aPubKey, CKM_HKDF_DERIVE,
                       PK11_ATTR_SESSION | PK11_ATTR_PUBLIC |
                           PK11_ATTR_INSENSITIVE | PK11_ATTR_EXTRACTABLE,
                       CKF_DERIVE, &sharedSecret, &ciphertext) != SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }
  UniquePK11SymKey symKey(sharedSecret);
  UniqueSECItem ciphertextItem(ciphertext);

  if (PK11_ExtractKeyValue(symKey.get()) != SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  // PK11_GetKeyData() returns a buffer owned by symKey, which the assignment
  // copies.
  ATTEMPT_BUFFER_ASSIGN(aSharedKey, PK11_GetKeyData(symKey.get()));
  ATTEMPT_BUFFER_ASSIGN(aCiphertext, ciphertextItem.get());
  return NS_OK;
}

static nsresult MLKEMDecapsulate(SECKEYPrivateKey* aPrivKey,
                                 const CryptoBuffer& aCiphertext,
                                 CryptoBuffer& aSharedKey) {
  UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
  if (!arena) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  SECItem ciphertext = {siBuffer, nullptr, 0};
  ATTEMPT_BUFFER_TO_SECITEM(arena.get(), &ciphertext, aCiphertext);

  PK11SymKey* sharedSecret = nullptr;
  if (PK11_Decapsulate(aPrivKey, &ciphertext, CKM_HKDF_DERIVE,
                       PK11_ATTR_SESSION | PK11_ATTR_PUBLIC |
                           PK11_ATTR_INSENSITIVE | PK11_ATTR_EXTRACTABLE,
                       CKF_DERIVE, &sharedSecret) != SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }
  UniquePK11SymKey symKey(sharedSecret);

  if (PK11_ExtractKeyValue(symKey.get()) != SECSuccess) {
    return NS_ERROR_DOM_OPERATION_ERR;
  }

  ATTEMPT_BUFFER_ASSIGN(aSharedKey, PK11_GetKeyData(symKey.get()));
  return NS_OK;
}

// Normalizes aAlgorithm and checks it against the key it is to be used with,
// as the encapsulate and decapsulate operations all require.
static nsresult CheckMLKEMAlgorithmAndKey(JSContext* aCx,
                                          const ObjectOrString& aAlgorithm,
                                          const CryptoKey& aKey) {
  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!IsMLKEMAlgorithm(algName)) {
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  if (!algName.Equals(aKey.Algorithm().mName)) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }

  return NS_OK;
}

class EncapsulateBitsTask : public WebCryptoTask {
 public:
  EncapsulateBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                      CryptoKey& aKey)
      : mPublicKey(aKey.GetPublicKey()) {
    mEarlyRv = CheckMLKEMAlgorithmAndKey(aCx, aAlgorithm, aKey);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (!mPublicKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }
  }

 private:
  UniqueSECKEYPublicKey mPublicKey;
  CryptoBuffer mSharedKey;
  CryptoBuffer mCiphertext;

  virtual nsresult DoCrypto() override {
    return MLKEMEncapsulate(mPublicKey.get(), mSharedKey, mCiphertext);
  }

  virtual void Resolve() override {
    nsIGlobalObject* global = mResultPromise->GetGlobalObject();
    AutoJSAPI jsapi;
    if (!global || !jsapi.Init(global)) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }
    JSContext* cx = jsapi.cx();

    IgnoredErrorResult rv;
    JS::Rooted<JSObject*> sharedKey(cx, mSharedKey.ToArrayBuffer(cx, rv));
    if (rv.Failed()) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }

    JS::Rooted<JSObject*> ciphertext(cx, mCiphertext.ToArrayBuffer(cx, rv));
    if (rv.Failed()) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }

    RootedDictionary<EncapsulatedBits> result(cx);
    if (!result.mSharedKey.Init(sharedKey) ||
        !result.mCiphertext.Init(ciphertext)) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }

    mResultPromise->MaybeResolve(result);
  }
};

class DecapsulateBitsTask : public ReturnArrayBufferViewTask {
 public:
  DecapsulateBitsTask(JSContext* aCx, const ObjectOrString& aAlgorithm,
                      CryptoKey& aKey, const CryptoOperationData& aCiphertext)
      : mPrivateKey(aKey.GetPrivateKey()) {
    mEarlyRv = CheckMLKEMAlgorithmAndKey(aCx, aAlgorithm, aKey);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (!mPrivateKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }

    ATTEMPT_BUFFER_INIT(mCiphertext, aCiphertext);
  }

 private:
  UniqueSECKEYPrivateKey mPrivateKey;
  CryptoBuffer mCiphertext;

  virtual nsresult DoCrypto() override {
    return MLKEMDecapsulate(mPrivateKey.get(), mCiphertext, mResult);
  }
};

// ImportSymmetricKeyTask builds the shared key in BeforeCrypto(), but the key
// material only exists once the KEM operation has run, so these tasks defer
// that step to AfterCrypto().
class EncapsulateKeyTask : public ImportSymmetricKeyTask {
 public:
  EncapsulateKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                     const ObjectOrString& aAlgorithm, CryptoKey& aKey,
                     const ObjectOrString& aSharedKeyAlgorithm,
                     bool aExtractable, const Sequence<nsString>& aKeyUsages)
      : ImportSymmetricKeyTask(
            aGlobal, aCx,
            NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_FORMAT_RAW),
            aSharedKeyAlgorithm, aExtractable, aKeyUsages),
        mPublicKey(aKey.GetPublicKey()) {
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    mEarlyRv = CheckMLKEMAlgorithmAndKey(aCx, aAlgorithm, aKey);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (!mPublicKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }
  }

 private:
  UniqueSECKEYPublicKey mPublicKey;
  CryptoBuffer mCiphertext;

  virtual nsresult BeforeCrypto() override { return NS_OK; }

  virtual nsresult DoCrypto() override {
    return MLKEMEncapsulate(mPublicKey.get(), mKeyData, mCiphertext);
  }

  virtual nsresult AfterCrypto() override {
    return ImportSymmetricKeyTask::BeforeCrypto();
  }

  virtual void Resolve() override {
    nsIGlobalObject* global = mResultPromise->GetGlobalObject();
    AutoJSAPI jsapi;
    if (!global || !jsapi.Init(global)) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }
    JSContext* cx = jsapi.cx();

    IgnoredErrorResult rv;
    JS::Rooted<JSObject*> ciphertext(cx, mCiphertext.ToArrayBuffer(cx, rv));
    if (rv.Failed()) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }

    RootedDictionary<EncapsulatedKey> result(cx);
    result.mSharedKey = mKey;
    if (!result.mCiphertext.Init(ciphertext)) {
      mResultPromise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
      return;
    }

    mResultPromise->MaybeResolve(result);
  }
};

class DecapsulateKeyTask : public ImportSymmetricKeyTask {
 public:
  DecapsulateKeyTask(nsIGlobalObject* aGlobal, JSContext* aCx,
                     const ObjectOrString& aAlgorithm, CryptoKey& aKey,
                     const CryptoOperationData& aCiphertext,
                     const ObjectOrString& aSharedKeyAlgorithm,
                     bool aExtractable, const Sequence<nsString>& aKeyUsages)
      : ImportSymmetricKeyTask(
            aGlobal, aCx,
            NS_LITERAL_STRING_FROM_CSTRING(WEBCRYPTO_KEY_FORMAT_RAW),
            aSharedKeyAlgorithm, aExtractable, aKeyUsages),
        mPrivateKey(aKey.GetPrivateKey()) {
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    mEarlyRv = CheckMLKEMAlgorithmAndKey(aCx, aAlgorithm, aKey);
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    if (!mPrivateKey) {
      mEarlyRv = NS_ERROR_DOM_INVALID_ACCESS_ERR;
      return;
    }

    ATTEMPT_BUFFER_INIT(mCiphertext, aCiphertext);
  }

 private:
  UniqueSECKEYPrivateKey mPrivateKey;
  CryptoBuffer mCiphertext;

  virtual nsresult BeforeCrypto() override { return NS_OK; }

  virtual nsresult DoCrypto() override {
    return MLKEMDecapsulate(mPrivateKey.get(), mCiphertext, mKeyData);
  }

  virtual nsresult AfterCrypto() override {
    return ImportSymmetricKeyTask::BeforeCrypto();
  }
};

template <class KeyEncryptTask>
class WrapKeyTask : public ExportKeyTask {
 public:
  WrapKeyTask(JSContext* aCx, const nsAString& aFormat, CryptoKey& aKey,
              CryptoKey& aWrappingKey, const nsString& aWrapAlgName,
              const ObjectOrString& aWrapAlgorithm)
      : ExportKeyTask(aFormat, aKey) {
    if (NS_FAILED(mEarlyRv)) {
      return;
    }

    mTask = MakeRefPtr<KeyEncryptTask>(aCx, aWrapAlgName, aWrapAlgorithm,
                                       aWrappingKey, true);
  }

 private:
  RefPtr<KeyEncryptTask> mTask;

  virtual nsresult AfterCrypto() override {
    // If wrapping JWK, stringify the JSON
    if (mFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK)) {
      nsAutoString json;
      if (!mJwk.ToJSON(json)) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }

      NS_ConvertUTF16toUTF8 utf8(json);
      if (!mResult.Assign((const uint8_t*)utf8.BeginReading(), utf8.Length())) {
        return NS_ERROR_DOM_OPERATION_ERR;
      }
    }

    return NS_OK;
  }

  virtual void Resolve() override {
    mTask->SetData(mResult);
    mTask->DispatchWithPromise(mResultPromise);
  }

  virtual void Cleanup() override { mTask = nullptr; }
};

template <class KeyEncryptTask>
class UnwrapKeyTask : public KeyEncryptTask {
 public:
  UnwrapKeyTask(JSContext* aCx, const ArrayBufferViewOrArrayBuffer& aWrappedKey,
                CryptoKey& aUnwrappingKey, const nsString& aUnwrapAlgName,
                const ObjectOrString& aUnwrapAlgorithm,
                already_AddRefed<ImportKeyTask> aTask)
      : KeyEncryptTask(aCx, aUnwrapAlgName, aUnwrapAlgorithm, aUnwrappingKey,
                       aWrappedKey, false),
        mTask(aTask) {}

 private:
  RefPtr<ImportKeyTask> mTask;

  virtual void Resolve() override {
    mTask->SetKeyDataMaybeParseJWK(KeyEncryptTask::mResult);
    mTask->DispatchWithPromise(KeyEncryptTask::mResultPromise);
  }

  virtual void Cleanup() override { mTask = nullptr; }
};

// Task creation methods for WebCryptoTask

// Note: We do not perform algorithm normalization as a monolithic process,
// as described in the spec.  Instead:
// * Each method handles its slice of the supportedAlgorithms structure
// * Task constructors take care of:
//    * Coercing the algorithm to the proper concrete type
//    * Cloning subordinate data items
//    * Cloning input data as needed
//
// Thus, support for different algorithms is determined by the if-statements
// below, rather than a data structure.
//
// This results in algorithm normalization coming after some other checks,
// and thus slightly more steps being done synchronously than the spec calls
// for.  But none of these steps is especially time-consuming.

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateEncryptDecryptTask(
    JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
    const CryptoOperationData& aData, bool aEncrypt) {
  // Ensure key is usable for this operation
  if ((aEncrypt && !aKey.HasUsage(CryptoKey::ENCRYPT)) ||
      (!aEncrypt && !aKey.HasUsage(CryptoKey::DECRYPT))) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  if (algName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM)) {
    return MakeAndAddRef<AesTask>(aCx, algName, aAlgorithm, aKey, aData,
                                  aEncrypt);
  } else if (algName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP)) {
    return MakeAndAddRef<RsaOaepTask>(aCx, algName, aAlgorithm, aKey, aData,
                                      aEncrypt);
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateSignVerifyTask(
    JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
    const CryptoOperationData& aSignature, const CryptoOperationData& aData,
    bool aSign) {
  // Ensure key is usable for this operation
  if ((aSign && !aKey.HasUsage(CryptoKey::SIGN)) ||
      (!aSign && !aKey.HasUsage(CryptoKey::VERIFY))) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  if (algName.EqualsLiteral(WEBCRYPTO_ALG_HMAC)) {
    return MakeAndAddRef<HmacTask>(aCx, aAlgorithm, aKey, aSignature, aData,
                                   aSign);
  } else if (algName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
    return MakeAndAddRef<AsymmetricSignVerifyTask>(
        aCx, algName, aAlgorithm, aKey, aSignature, aData, aSign);
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateDigestTask(
    JSContext* aCx, const ObjectOrString& aAlgorithm,
    const CryptoOperationData& aData) {
  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  if (algName.EqualsLiteral(WEBCRYPTO_ALG_SHA1) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_SHA256) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_SHA384) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_SHA512)) {
    return MakeAndAddRef<DigestTask>(algName, aData);
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateImportKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
    JS::Handle<JSObject*> aKeyData, const ObjectOrString& aAlgorithm,
    bool aExtractable, const Sequence<nsString>& aKeyUsages) {
  // Verify that the format is recognized
  if (!aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_PUBLIC) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_SEED)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  // Verify that aKeyUsages does not contain an unrecognized value
  if (!CryptoKey::AllUsagesRecognized(aKeyUsages)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  // SPEC-BUG: PBKDF2 is not supposed to be supported for this operation.
  // However, the spec should be updated to allow it.
  if (algName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_PBKDF2) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_HKDF) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_HMAC)) {
    return MakeAndAddRef<ImportSymmetricKeyTask>(
        aGlobal, aCx, aFormat, aKeyData, aAlgorithm, aExtractable, aKeyUsages);
  } else if (algName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS)) {
    return MakeAndAddRef<ImportRsaKeyTask>(
        aGlobal, aCx, aFormat, aKeyData, aAlgorithm, aExtractable, aKeyUsages);
  } else if (algName.EqualsLiteral(WEBCRYPTO_ALG_ECDH) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
    return MakeAndAddRef<ImportEcKeyTask>(aGlobal, aCx, aFormat, aKeyData,
                                          aAlgorithm, aExtractable, aKeyUsages);
  } else if (algName.EqualsLiteral(WEBCRYPTO_ALG_X25519) ||
             algName.EqualsLiteral(WEBCRYPTO_ALG_ED25519)) {
    return MakeAndAddRef<ImportOKPKeyTask>(
        aGlobal, aCx, aFormat, aKeyData, aAlgorithm, aExtractable, aKeyUsages);
  } else if (IsMLKEMAlgorithm(algName)) {
    return MakeAndAddRef<ImportMLKEMKeyTask>(
        aGlobal, aCx, aFormat, aKeyData, aAlgorithm, aExtractable, aKeyUsages);
  } else {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  }
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateExportKeyTask(
    const nsAString& aFormat, CryptoKey& aKey) {
  // Verify that the format is recognized
  if (!aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_PUBLIC) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_SEED)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  // Verify that the key is extractable
  if (!aKey.Extractable()) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  // Verify that the algorithm supports export
  // SPEC-BUG: PBKDF2 is not supposed to be supported for this operation.
  // However, the spec should be updated to allow it.
  nsString algName = aKey.Algorithm().mName;
  if (algName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_PBKDF2) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_HMAC) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_RSA_PSS) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_ECDH) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_ED25519) ||
      algName.EqualsLiteral(WEBCRYPTO_ALG_X25519) ||
      IsMLKEMAlgorithm(algName)) {
    return MakeAndAddRef<ExportKeyTask>(aFormat, aKey);
  }
  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateGenerateKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const ObjectOrString& aAlgorithm,
    bool aExtractable, const Sequence<nsString>& aKeyUsages) {
  if (!CryptoKey::AllUsagesRecognized(aKeyUsages)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_AES_CBC) ||
      algName.EqualsASCII(WEBCRYPTO_ALG_AES_CTR) ||
      algName.EqualsASCII(WEBCRYPTO_ALG_AES_GCM) ||
      algName.EqualsASCII(WEBCRYPTO_ALG_AES_KW) ||
      algName.EqualsASCII(WEBCRYPTO_ALG_HMAC)) {
    return MakeAndAddRef<GenerateSymmetricKeyTask>(
        aGlobal, aCx, algName, aAlgorithm, aExtractable, aKeyUsages);
  } else if (algName.EqualsASCII(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
             algName.EqualsASCII(WEBCRYPTO_ALG_RSA_OAEP) ||
             algName.EqualsASCII(WEBCRYPTO_ALG_RSA_PSS) ||
             algName.EqualsASCII(WEBCRYPTO_ALG_ECDH) ||
             algName.EqualsASCII(WEBCRYPTO_ALG_ECDSA) ||
             algName.EqualsASCII(WEBCRYPTO_ALG_ED25519) ||
             algName.EqualsASCII(WEBCRYPTO_ALG_X25519) ||
             IsMLKEMAlgorithm(algName)) {
    return MakeAndAddRef<GenerateAsymmetricKeyTask>(
        aGlobal, aCx, algName, aAlgorithm, aExtractable, aKeyUsages);
  } else {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  }
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateDeriveKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const ObjectOrString& aAlgorithm,
    CryptoKey& aBaseKey, const ObjectOrString& aDerivedKeyType,
    bool aExtractable, const Sequence<nsString>& aKeyUsages) {
  // Ensure baseKey is usable for this operation
  if (!aBaseKey.HasUsage(CryptoKey::DERIVEKEY)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  // Verify that aKeyUsages does not contain an unrecognized value
  if (!CryptoKey::AllUsagesRecognized(aKeyUsages)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  nsString derivedKeyAlgName;
  if (NS_FAILED(GetAlgorithmName(aCx, aDerivedKeyType, derivedKeyAlgName))) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_HKDF)) {
    return MakeAndAddRef<DeriveKeyTask<DeriveHkdfBitsTask>>(
        aGlobal, aCx, aAlgorithm, aBaseKey, derivedKeyAlgName, aDerivedKeyType,
        aExtractable, aKeyUsages);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_X25519)) {
    return MakeAndAddRef<DeriveKeyTask<DeriveX25519BitsTask>>(
        aGlobal, aCx, aAlgorithm, aBaseKey, derivedKeyAlgName, aDerivedKeyType,
        aExtractable, aKeyUsages);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_PBKDF2)) {
    return MakeAndAddRef<DeriveKeyTask<DerivePbkdfBitsTask>>(
        aGlobal, aCx, aAlgorithm, aBaseKey, derivedKeyAlgName, aDerivedKeyType,
        aExtractable, aKeyUsages);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_ECDH)) {
    return MakeAndAddRef<DeriveKeyTask<DeriveEcdhBitsTask>>(
        aGlobal, aCx, aAlgorithm, aBaseKey, derivedKeyAlgName, aDerivedKeyType,
        aExtractable, aKeyUsages);
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateDeriveBitsTask(
    JSContext* aCx, const ObjectOrString& aAlgorithm, CryptoKey& aKey,
    const Nullable<uint32_t>& aLength) {
  // Ensure baseKey is usable for this operation
  if (!aKey.HasUsage(CryptoKey::DERIVEBITS)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  nsString algName;
  nsresult rv = GetAlgorithmName(aCx, aAlgorithm, algName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_PBKDF2)) {
    return MakeAndAddRef<DerivePbkdfBitsTask>(aCx, aAlgorithm, aKey, aLength);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_ECDH)) {
    return MakeAndAddRef<DeriveEcdhBitsTask>(aCx, aAlgorithm, aKey, aLength);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_HKDF)) {
    return MakeAndAddRef<DeriveHkdfBitsTask>(aCx, aAlgorithm, aKey, aLength);
  }

  if (algName.EqualsASCII(WEBCRYPTO_ALG_X25519)) {
    return MakeAndAddRef<DeriveX25519BitsTask>(aCx, aAlgorithm, aKey, aLength);
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateEncapsulateBitsTask(
    JSContext* aCx, const ObjectOrString& aAlgorithm,
    CryptoKey& aEncapsulationKey) {
  if (!aEncapsulationKey.HasUsage(CryptoKey::ENCAPSULATEBITS)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  return MakeAndAddRef<EncapsulateBitsTask>(aCx, aAlgorithm, aEncapsulationKey);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateEncapsulateKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const ObjectOrString& aAlgorithm,
    CryptoKey& aEncapsulationKey, const ObjectOrString& aSharedKeyAlgorithm,
    bool aExtractable, const Sequence<nsString>& aKeyUsages) {
  if (!aEncapsulationKey.HasUsage(CryptoKey::ENCAPSULATEKEY)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  if (!CryptoKey::AllUsagesRecognized(aKeyUsages)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  return MakeAndAddRef<EncapsulateKeyTask>(
      aGlobal, aCx, aAlgorithm, aEncapsulationKey, aSharedKeyAlgorithm,
      aExtractable, aKeyUsages);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateDecapsulateBitsTask(
    JSContext* aCx, const ObjectOrString& aAlgorithm,
    CryptoKey& aDecapsulationKey, const CryptoOperationData& aCiphertext) {
  if (!aDecapsulationKey.HasUsage(CryptoKey::DECAPSULATEBITS)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  return MakeAndAddRef<DecapsulateBitsTask>(aCx, aAlgorithm, aDecapsulationKey,
                                            aCiphertext);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateDecapsulateKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const ObjectOrString& aAlgorithm,
    CryptoKey& aDecapsulationKey, const CryptoOperationData& aCiphertext,
    const ObjectOrString& aSharedKeyAlgorithm, bool aExtractable,
    const Sequence<nsString>& aKeyUsages) {
  if (!aDecapsulationKey.HasUsage(CryptoKey::DECAPSULATEKEY)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  if (!CryptoKey::AllUsagesRecognized(aKeyUsages)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  return MakeAndAddRef<DecapsulateKeyTask>(
      aGlobal, aCx, aAlgorithm, aDecapsulationKey, aCiphertext,
      aSharedKeyAlgorithm, aExtractable, aKeyUsages);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateWrapKeyTask(
    JSContext* aCx, const nsAString& aFormat, CryptoKey& aKey,
    CryptoKey& aWrappingKey, const ObjectOrString& aWrapAlgorithm) {
  // Verify that the format is recognized
  if (!aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_SPKI) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_PKCS8) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_JWK) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_PUBLIC) &&
      !aFormat.EqualsLiteral(WEBCRYPTO_KEY_FORMAT_RAW_SEED)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  // Ensure wrappingKey is usable for this operation
  if (!aWrappingKey.HasUsage(CryptoKey::WRAPKEY)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  // Ensure key is extractable
  if (!aKey.Extractable()) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  nsString wrapAlgName;
  nsresult rv = GetAlgorithmName(aCx, aWrapAlgorithm, wrapAlgName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  if (wrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
      wrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
      wrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM)) {
    return MakeAndAddRef<WrapKeyTask<AesTask>>(aCx, aFormat, aKey, aWrappingKey,
                                               wrapAlgName, aWrapAlgorithm);
  } else if (wrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW)) {
    return MakeAndAddRef<WrapKeyTask<AesKwTask>>(
        aCx, aFormat, aKey, aWrappingKey, wrapAlgName, aWrapAlgorithm);
  } else if (wrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP)) {
    return MakeAndAddRef<WrapKeyTask<RsaOaepTask>>(
        aCx, aFormat, aKey, aWrappingKey, wrapAlgName, aWrapAlgorithm);
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

already_AddRefed<WebCryptoTask> WebCryptoTask::CreateUnwrapKeyTask(
    nsIGlobalObject* aGlobal, JSContext* aCx, const nsAString& aFormat,
    const ArrayBufferViewOrArrayBuffer& aWrappedKey, CryptoKey& aUnwrappingKey,
    const ObjectOrString& aUnwrapAlgorithm,
    const ObjectOrString& aUnwrappedKeyAlgorithm, bool aExtractable,
    const Sequence<nsString>& aKeyUsages) {
  // Ensure key is usable for this operation
  if (!aUnwrappingKey.HasUsage(CryptoKey::UNWRAPKEY)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_INVALID_ACCESS_ERR);
  }

  // Verify that aKeyUsages does not contain an unrecognized value
  if (!CryptoKey::AllUsagesRecognized(aKeyUsages)) {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_SYNTAX_ERR);
  }

  nsString keyAlgName;
  nsresult rv = GetAlgorithmName(aCx, aUnwrappedKeyAlgorithm, keyAlgName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }

  CryptoOperationData dummy;
  RefPtr<ImportKeyTask> importTask;
  if (keyAlgName.EqualsASCII(WEBCRYPTO_ALG_AES_CBC) ||
      keyAlgName.EqualsASCII(WEBCRYPTO_ALG_AES_CTR) ||
      keyAlgName.EqualsASCII(WEBCRYPTO_ALG_AES_GCM) ||
      keyAlgName.EqualsASCII(WEBCRYPTO_ALG_AES_KW) ||
      keyAlgName.EqualsASCII(WEBCRYPTO_ALG_HKDF) ||
      keyAlgName.EqualsASCII(WEBCRYPTO_ALG_HMAC)) {
    importTask = MakeAndAddRef<ImportSymmetricKeyTask>(
        aGlobal, aCx, aFormat, aUnwrappedKeyAlgorithm, aExtractable,
        aKeyUsages);
  } else if (keyAlgName.EqualsASCII(WEBCRYPTO_ALG_RSASSA_PKCS1) ||
             keyAlgName.EqualsASCII(WEBCRYPTO_ALG_RSA_OAEP) ||
             keyAlgName.EqualsASCII(WEBCRYPTO_ALG_RSA_PSS)) {
    importTask = MakeAndAddRef<ImportRsaKeyTask>(aGlobal, aCx, aFormat,
                                                 aUnwrappedKeyAlgorithm,
                                                 aExtractable, aKeyUsages);
  } else if (keyAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDH) ||
             keyAlgName.EqualsLiteral(WEBCRYPTO_ALG_ECDSA)) {
    importTask = MakeAndAddRef<ImportEcKeyTask>(aGlobal, aCx, aFormat,
                                                aUnwrappedKeyAlgorithm,
                                                aExtractable, aKeyUsages);
  } else if (keyAlgName.EqualsLiteral(WEBCRYPTO_ALG_ED25519) ||
             keyAlgName.EqualsLiteral(WEBCRYPTO_ALG_X25519)) {
    importTask = MakeAndAddRef<ImportOKPKeyTask>(aGlobal, aCx, aFormat,
                                                 aUnwrappedKeyAlgorithm,
                                                 aExtractable, aKeyUsages);
  } else if (IsMLKEMAlgorithm(keyAlgName)) {
    importTask = MakeAndAddRef<ImportMLKEMKeyTask>(aGlobal, aCx, aFormat,
                                                   aUnwrappedKeyAlgorithm,
                                                   aExtractable, aKeyUsages);
  } else {
    return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  }

  nsString unwrapAlgName;
  rv = GetAlgorithmName(aCx, aUnwrapAlgorithm, unwrapAlgName);
  if (NS_FAILED(rv)) {
    return MakeAndAddRef<FailureTask>(rv);
  }
  if (unwrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CBC) ||
      unwrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_CTR) ||
      unwrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_GCM)) {
    return MakeAndAddRef<UnwrapKeyTask<AesTask>>(
        aCx, aWrappedKey, aUnwrappingKey, unwrapAlgName, aUnwrapAlgorithm,
        importTask.forget());
  } else if (unwrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_AES_KW)) {
    return MakeAndAddRef<UnwrapKeyTask<AesKwTask>>(
        aCx, aWrappedKey, aUnwrappingKey, unwrapAlgName, aUnwrapAlgorithm,
        importTask.forget());
  } else if (unwrapAlgName.EqualsLiteral(WEBCRYPTO_ALG_RSA_OAEP)) {
    return MakeAndAddRef<UnwrapKeyTask<RsaOaepTask>>(
        aCx, aWrappedKey, aUnwrappingKey, unwrapAlgName, aUnwrapAlgorithm,
        importTask.forget());
  }

  return MakeAndAddRef<FailureTask>(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
}

WebCryptoTask::WebCryptoTask()
    : CancelableRunnable("WebCryptoTask"),
      mEarlyRv(NS_OK),
      mEarlyComplete(false),
      mOriginalEventTarget(nullptr),
      mRv(NS_ERROR_NOT_INITIALIZED) {}

WebCryptoTask::~WebCryptoTask() = default;

}  // namespace mozilla::dom
