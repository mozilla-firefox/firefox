/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDrmProxySupport.h"

#include "MediaCodec.h"  // For MediaDrm::KeyStatus
#include "MediaDrmCDMCallbackProxy.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/java/MediaDrmProxyNatives.h"
#include "mozilla/java/SessionKeyInfoWrappers.h"

namespace mozilla {

LogModule* GetMDRMNLog() {
  static LazyLogModule log("MediaDrmProxySupport");
  return log;
}

class MediaDrmJavaCallbacksSupport
    : public java::MediaDrmProxy::NativeMediaDrmProxyCallbacks::Natives<
          MediaDrmJavaCallbacksSupport> {
 public:
  typedef java::MediaDrmProxy::NativeMediaDrmProxyCallbacks::Natives<
      MediaDrmJavaCallbacksSupport>
      MediaDrmProxyNativeCallbacks;
  using MediaDrmProxyNativeCallbacks::AttachNative;
  using MediaDrmProxyNativeCallbacks::DisposeNative;

  explicit MediaDrmJavaCallbacksSupport(
      UniquePtr<MediaDrmCDMCallbackProxy>&& aDecryptorProxyCallback)
      : mDecryptorProxyCallback(std::move(aDecryptorProxyCallback)) {
    MOZ_ASSERT(mDecryptorProxyCallback);
  }
  /*
   * Native implementation, called by Java.
   */
  void OnSessionCreated(int aCreateSessionToken, int aPromiseId,
                        jni::ByteArray::Param aSessionId,
                        jni::ByteArray::Param aRequest);

  void OnSessionUpdated(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionClosed(int aPromiseId, jni::ByteArray::Param aSessionId);

  void OnSessionMessage(
      jni::ByteArray::Param aSessionId,
      int /*mozilla::dom::MediaKeyMessageType*/ aSessionMessageType,
      jni::ByteArray::Param aRequest);

  void OnSessionError(jni::ByteArray::Param aSessionId,
                      jni::String::Param aMessage);

  void OnSessionBatchedKeyChanged(jni::ByteArray::Param,
                                  jni::ObjectArray::Param);

  void OnRejectPromise(int aPromiseId, jni::String::Param aMessage);

 private:
  UniquePtr<MediaDrmCDMCallbackProxy> mDecryptorProxyCallback;
};  // MediaDrmJavaCallbacksSupport

void MediaDrmJavaCallbacksSupport::OnSessionCreated(
    int aCreateSessionToken, int aPromiseId, jni::ByteArray::Param aSessionId,
    jni::ByteArray::Param aRequest) {
  MOZ_ASSERT(NS_IsMainThread());
  auto reqDataArray = aRequest->GetElements();
  nsCString sessionId(
      reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
      aSessionId->Length());
  MDRMN_LOG("SessionId({}) closed", sessionId.get());

  mDecryptorProxyCallback->SetSessionId(aCreateSessionToken, sessionId);
  mDecryptorProxyCallback->ResolvePromise(aPromiseId);
}

void MediaDrmJavaCallbacksSupport::OnSessionUpdated(
    int aPromiseId, jni::ByteArray::Param aSessionId) {
  MOZ_ASSERT(NS_IsMainThread());
  MDRMN_LOG(
      "SessionId({}) closed",
      nsCString(reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
                aSessionId->Length())
          .get());
  mDecryptorProxyCallback->ResolvePromise(aPromiseId);
}

void MediaDrmJavaCallbacksSupport::OnSessionClosed(
    int aPromiseId, jni::ByteArray::Param aSessionId) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString sessionId(
      reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
      aSessionId->Length());
  MDRMN_LOG("SessionId({}) closed", sessionId.get());
  mDecryptorProxyCallback->ResolvePromise(aPromiseId);
  mDecryptorProxyCallback->SessionClosed(sessionId);
}

void MediaDrmJavaCallbacksSupport::OnSessionMessage(
    jni::ByteArray::Param aSessionId,
    int /*mozilla::dom::MediaKeyMessageType*/ aMessageType,
    jni::ByteArray::Param aRequest) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString sessionId(
      reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
      aSessionId->Length());
  auto reqDataArray = aRequest->GetElements();

  nsTArray<uint8_t> retRequest;
  retRequest.AppendElements(reinterpret_cast<uint8_t*>(reqDataArray.Elements()),
                            reqDataArray.Length());

  mDecryptorProxyCallback->SessionMessage(
      sessionId, static_cast<dom::MediaKeyMessageType>(aMessageType),
      retRequest);
}

void MediaDrmJavaCallbacksSupport::OnSessionError(
    jni::ByteArray::Param aSessionId, jni::String::Param aMessage) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString sessionId(
      reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
      aSessionId->Length());
  nsCString errorMessage = aMessage->ToCString();
  MDRMN_LOG("SessionId({})", sessionId.get());
  // TODO: We cannot get system error code from media drm API.
  // Currently use -1 as an error code.
  mDecryptorProxyCallback->SessionError(
      sessionId, NS_ERROR_DOM_INVALID_STATE_ERR, -1, errorMessage);
}

// TODO: MediaDrm.KeyStatus defined the status code not included
// dom::MediaKeyStatus::Released and dom::MediaKeyStatus::Output_downscaled.
// Should keep tracking for this if it will be changed in the future.
static dom::MediaKeyStatus MediaDrmKeyStatusToMediaKeyStatus(int aStatusCode) {
  using mozilla::java::sdk::MediaDrm;
  switch (aStatusCode) {
    case MediaDrm::KeyStatus::STATUS_USABLE:
      return dom::MediaKeyStatus::Usable;
    case MediaDrm::KeyStatus::STATUS_EXPIRED:
      return dom::MediaKeyStatus::Expired;
    case MediaDrm::KeyStatus::STATUS_OUTPUT_NOT_ALLOWED:
      return dom::MediaKeyStatus::Output_restricted;
    case MediaDrm::KeyStatus::STATUS_INTERNAL_ERROR:
      return dom::MediaKeyStatus::Internal_error;
    case MediaDrm::KeyStatus::STATUS_PENDING:
      return dom::MediaKeyStatus::Status_pending;
    default:
      return dom::MediaKeyStatus::Internal_error;
  }
}

void MediaDrmJavaCallbacksSupport::OnSessionBatchedKeyChanged(
    jni::ByteArray::Param aSessionId, jni::ObjectArray::Param aKeyInfos) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString sessionId(
      reinterpret_cast<char*>(aSessionId->GetElements().Elements()),
      aSessionId->Length());
  nsTArray<jni::Object::LocalRef> keyInfosObjectArray(aKeyInfos->GetElements());

  nsTArray<CDMKeyInfo> keyInfosArray;

  for (auto&& keyInfoObject : keyInfosObjectArray) {
    java::SessionKeyInfo::LocalRef keyInfo(std::move(keyInfoObject));
    mozilla::jni::ByteArray::LocalRef keyIdByteArray = keyInfo->KeyId();
    nsTArray<int8_t> keyIdInt8Array = keyIdByteArray->GetElements();
    // Cast nsTArray<int8_t> to nsTArray<uint8_t>
    nsTArray<uint8_t>* keyId =
        reinterpret_cast<nsTArray<uint8_t>*>(&keyIdInt8Array);
    auto keyStatus = keyInfo->Status();  // int32_t
    keyInfosArray.AppendElement(
        CDMKeyInfo(*keyId, dom::Optional<dom::MediaKeyStatus>(
                               MediaDrmKeyStatusToMediaKeyStatus(keyStatus))));
  }

  mDecryptorProxyCallback->BatchedKeyStatusChanged(sessionId, keyInfosArray);
}

void MediaDrmJavaCallbacksSupport::OnRejectPromise(
    int aPromiseId, jni::String::Param aMessage) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCString reason = aMessage->ToCString();
  MDRMN_LOG("OnRejectPromise aMessage({}) ", reason.get());
  // Current implementation assume all the reject from MediaDrm is due to
  // invalid state. Other cases should be handled before calling into
  // MediaDrmProxy API.
  ErrorResult rv;
  rv.ThrowInvalidStateError(reason);
  mDecryptorProxyCallback->RejectPromise(aPromiseId, std::move(rv), reason);
}

MediaDrmProxySupport::MediaDrmProxySupport(const nsAString& aKeySystem,
                                           const nsACString& aOriginID)
    : mKeySystem(aKeySystem), mDestroyed(false) {
  mJavaCallbacks = java::MediaDrmProxy::NativeMediaDrmProxyCallbacks::New();

  mBridgeProxy = java::MediaDrmProxy::Create(mKeySystem, mJavaCallbacks);

  if (!mBridgeProxy) {
    MDRMN_LOG("Failed to create the MediaDrm bridge");
    return;
  }

  mMediaDrmStubId = mBridgeProxy->GetStubId()->ToString();

  if (!aOriginID.IsEmpty()) {
    mBridgeProxy->SetOriginID(aOriginID);
    MDRMN_LOG("Have origin ID ({:.4})", PromiseFlatCString(aOriginID).get());
  } else {
    MDRMN_LOG("Origin ID is empty");
  }
}

MediaDrmProxySupport::~MediaDrmProxySupport() {
  MOZ_ASSERT(mDestroyed, "Shutdown() should be called before !!");
  MediaDrmJavaCallbacksSupport::DisposeNative(mJavaCallbacks);
}

nsresult MediaDrmProxySupport::Init(
    UniquePtr<MediaDrmCDMCallbackProxy>&& aCallback) {
  MOZ_ASSERT(mJavaCallbacks);

  MediaDrmJavaCallbacksSupport::AttachNative(
      mJavaCallbacks,
      std::make_unique<MediaDrmJavaCallbacksSupport>(std::move(aCallback)));
  return mBridgeProxy != nullptr ? NS_OK : NS_ERROR_FAILURE;
}

void MediaDrmProxySupport::CreateSession(uint32_t aCreateSessionToken,
                                         uint32_t aPromiseId,
                                         const nsCString& aInitDataType,
                                         const nsTArray<uint8_t>& aInitData,
                                         MediaDrmSessionType aSessionType) {
  MOZ_ASSERT(mBridgeProxy);

  auto initDataBytes = mozilla::jni::ByteArray::New(
      reinterpret_cast<const int8_t*>(aInitData.Elements()),
      aInitData.Length());
  // TODO: aSessionType is not used here.
  // Refer to
  // http://androidxref.com/5.1.1_r6/xref/external/chromium_org/media/base/android/java/src/org/chromium/media/MediaDrmBridge.java#420
  // it is hard code to streaming type.
  mBridgeProxy->CreateSession(aCreateSessionToken, aPromiseId,
                              NS_ConvertUTF8toUTF16(aInitDataType),
                              initDataBytes);
}

void MediaDrmProxySupport::UpdateSession(uint32_t aPromiseId,
                                         const nsCString& aSessionId,
                                         const nsTArray<uint8_t>& aResponse) {
  MOZ_ASSERT(mBridgeProxy);

  auto response = mozilla::jni::ByteArray::New(
      reinterpret_cast<const int8_t*>(aResponse.Elements()),
      aResponse.Length());
  mBridgeProxy->UpdateSession(aPromiseId, NS_ConvertUTF8toUTF16(aSessionId),
                              response);
}

void MediaDrmProxySupport::CloseSession(uint32_t aPromiseId,
                                        const nsCString& aSessionId) {
  MOZ_ASSERT(mBridgeProxy);

  mBridgeProxy->CloseSession(aPromiseId, NS_ConvertUTF8toUTF16(aSessionId));
}

void MediaDrmProxySupport::Shutdown() {
  if (mDestroyed) {
    return;
  }

  if (mBridgeProxy) {
    mBridgeProxy->Destroy();
  }
  mDestroyed = true;
}

bool MediaDrmProxySupport::SetServerCertificate(
    const nsTArray<uint8_t>& aCert) {
  jni::ByteArray::LocalRef cert = jni::ByteArray::New(
      reinterpret_cast<const int8_t*>(aCert.Elements()), aCert.Length());
  return mBridgeProxy->SetServerCertificate(cert);
}

}  // namespace mozilla
