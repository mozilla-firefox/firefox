/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDrmRemoteCDMParent.h"

#include <limits>

#include "mozilla/CheckedInt.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/ProtobufReader.h"
#include "mozilla/RemoteMediaManagerParent.h"

namespace mozilla {

StaticAutoPtr<MediaDrmRemoteCDMParent::DrmCallbackMap>
    MediaDrmRemoteCDMParent::sCbMap;

/* static */
void MediaDrmRemoteCDMParent::InitializeStatics() {
  if (sCbMap) {
    return;
  }

  sCbMap = new DrmCallbackMap();
}

// Before Android 12, AMediaDrm_openSession returns AMEDIA_OK without a session
// ID when the plugin fails to open a session, for example because the device is
// not provisioned or because there are too many open sessions.
static media_status_t OpenSession(AMediaDrm* aDrm, AMediaDrmSessionId* aId) {
  *aId = {nullptr, 0};
  media_status_t status = AMediaDrm_openSession(aDrm, aId);
  if (!__builtin_available(android 31, *)) {
    if (status == AMEDIA_OK && (!aId->ptr || !aId->length)) {
      return AMEDIA_DRM_NOT_PROVISIONED;
    }
  }
  return status;
}

static MediaResult OpenSessionError(media_status_t aStatus, const char* aFor) {
  if (aStatus == AMEDIA_DRM_RESOURCE_BUSY) {
    return MediaResult(NS_ERROR_DOM_QUOTA_EXCEEDED_ERR,
                       RESULT_DETAIL("AMediaDrm_openSession failed for %s: too "
                                     "many open sessions (%d)",
                                     aFor, aStatus));
  }
  return MediaResult(
      NS_ERROR_DOM_INVALID_STATE_ERR,
      RESULT_DETAIL("AMediaDrm_openSession failed for %s %d", aFor, aStatus));
}

/* static */
void MediaDrmRemoteCDMParent::HandleEventCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    AMediaDrmEventType aEventType, int aExtra, const uint8_t* aData,
    size_t aDataSize) {
  EME_LOG("MediaDrmRemoteCDMParent::HandleEventCb -- drm {}, event {}",
          fmt::ptr(aDrm), static_cast<int>(aEventType));

  // Called from an internal NDK thread. We need to dispatch to the owning
  // thread of the actor with the same AMediaDrm object.
  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(aSessionId->ptr), aSessionId->length);

  size_t dataSize = aData ? aDataSize : 0;
  nsTArray<uint8_t> data(dataSize);
  if (dataSize) {
    data.AppendElements(aData, dataSize);
  }

  RemoteMediaManagerParent::Dispatch(NS_NewRunnableFunction(
      __func__,
      [aDrm, aEventType, aExtra, sessionIdStr = std::move(sessionIdStr),
       data = std::move(data)]() mutable {
        auto i = sCbMap->find(aDrm);
        if (i == sCbMap->end()) {
          return;
        }

        i->second->HandleEvent(std::move(sessionIdStr), aEventType, aExtra,
                               std::move(data));
      }));
}

/* static */
void MediaDrmRemoteCDMParent::HandleExpirationUpdateCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    int64_t aExpiryTimeInMS) {
  EME_LOG("MediaDrmRemoteCDMParent::HandleExpirationUpdateCb -- drm {}",
          fmt::ptr(aDrm));

  // Called from an internal NDK thread. We need to dispatch to the owning
  // thread of the actor with the same AMediaDrm object.
  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(aSessionId->ptr), aSessionId->length);

  RemoteMediaManagerParent::Dispatch(NS_NewRunnableFunction(
      __func__, [aDrm, aExpiryTimeInMS,
                 sessionIdStr = std::move(sessionIdStr)]() mutable {
        auto i = sCbMap->find(aDrm);
        if (i == sCbMap->end()) {
          return;
        }

        i->second->HandleExpirationUpdate(std::move(sessionIdStr),
                                          aExpiryTimeInMS);
      }));
}

/* static */
void MediaDrmRemoteCDMParent::HandleKeysChangeCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    const AMediaDrmKeyStatus* aKeyStatus, size_t aNumKeys,
    bool aHasNewUsableKey) {
  EME_LOG("MediaDrmRemoteCDMParent::HandleKeysChangeCb -- drm {}",
          fmt::ptr(aDrm));

  // Called from an internal NDK thread. We need to dispatch to the owning
  // thread of the actor with the same AMediaDrm object.
  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(aSessionId->ptr), aSessionId->length);

  nsTArray<CDMKeyInfo> keyInfo(aNumKeys);
  if (aKeyStatus) {
    for (size_t i = 0; i < aNumKeys; ++i) {
      nsTArray<uint8_t> keyId(aKeyStatus[i].keyId.ptr,
                              aKeyStatus[i].keyId.length);

      dom::Optional<dom::MediaKeyStatus> keyStatus;
      switch (aKeyStatus[i].keyType) {
        case AMediaKeyStatusType::KEY_STATUS_TYPE_USABLE:
          keyStatus.Construct(dom::MediaKeyStatus::Usable);
          break;
        case AMediaKeyStatusType::KEY_STATUS_TYPE_EXPIRED:
          keyStatus.Construct(dom::MediaKeyStatus::Expired);
          break;
        case AMediaKeyStatusType::KEY_STATUS_TYPE_OUTPUTNOTALLOWED:
          keyStatus.Construct(dom::MediaKeyStatus::Output_restricted);
          break;
        case AMediaKeyStatusType::KEY_STATUS_TYPE_STATUSPENDING:
          keyStatus.Construct(dom::MediaKeyStatus::Status_pending);
          break;
        default:
          MOZ_FALLTHROUGH_ASSERT("Unhandled AMediaKeyStatusType!");
        case AMediaKeyStatusType::KEY_STATUS_TYPE_INTERNALERROR:
          keyStatus.Construct(dom::MediaKeyStatus::Internal_error);
          break;
      }

      keyInfo.AppendElement(CDMKeyInfo(std::move(keyId), std::move(keyStatus)));
    }
  }

  RemoteMediaManagerParent::Dispatch(NS_NewRunnableFunction(
      __func__, [aDrm, aHasNewUsableKey, sessionIdStr = std::move(sessionIdStr),
                 keyInfo = std::move(keyInfo)]() mutable {
        auto i = sCbMap->find(aDrm);
        if (i == sCbMap->end()) {
          return;
        }

        i->second->HandleKeysChange(std::move(sessionIdStr), aHasNewUsableKey,
                                    std::move(keyInfo));
      }));
}

MediaDrmRemoteCDMParent::MediaDrmRemoteCDMParent(const nsAString& aKeySystem)
    : mMutex("MediaDrmRemoteCDMParent::mMutex") {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::MediaDrmRemoteCDMParent",
          fmt::ptr(this));
  if (IsWidevineKeySystem(aKeySystem)) {
    mUuid = WIDEVINE_UUID;
  } else if (IsClearkeyKeySystem(aKeySystem)) {
    mUuid = CLEARKEY_UUID;
  }
}

MediaDrmRemoteCDMParent::~MediaDrmRemoteCDMParent() {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::~MediaDrmRemoteCDMParent",
          fmt::ptr(this));
  Destroy();
}

void MediaDrmRemoteCDMParent::ActorDestroy(ActorDestroyReason aWhy) {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::ActorDestroy", fmt::ptr(this));
  Destroy();
}

void MediaDrmRemoteCDMParent::Destroy() {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::Destroy -- drm {}", fmt::ptr(this),
          fmt::ptr(mDrm));

  mProvisionPromise.RejectIfExists(
      MediaResult(NS_ERROR_DOM_ABORT_ERR, "Destroyed"_ns), __func__);

  for (const auto& i : mSessions) {
    AMediaDrm_closeSession(mDrm, &i.second.id);
  }
  mSessions.clear();
  mCertificateQueue.Clear();

  {
    MutexAutoLock lock(mMutex);
    if (mCrypto) {
      mCrypto = nullptr;
      mCryptoSessionId = {};
    }
  }

  if (mDrm) {
    auto i = sCbMap->find(mDrm);
    if (i != sCbMap->end()) {
      sCbMap->erase(i);
    } else {
      MOZ_ASSERT_UNREACHABLE("Missing MediaDrm in sCbMap");
    }
    AMediaDrm_release(mDrm);
    mDrm = nullptr;
  }
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvInit(
    const RemoteCDMInitRequestIPDL& request, InitResolver&& aResolver) {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvInit", fmt::ptr(this));

  if (NS_WARN_IF(!mUuid)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Invalid uuid"_ns));
    return IPC_OK();
  }

  if (NS_WARN_IF(mDrm)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaDrm already initialized"_ns));
    return IPC_OK();
  }

  if (NS_WARN_IF(!AMediaCrypto_isCryptoSchemeSupported(mUuid))) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaCrypto_isCryptoSchemeSupported failed"_ns));
    return IPC_OK();
  }

  mDrm = AMediaDrm_createByUUID(mUuid);
  if (NS_WARN_IF(!mDrm)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaDrm_createByUUID failed"_ns));
    return IPC_OK();
  }

  media_status_t status =
      AMediaDrm_setPropertyString(mDrm, "securityLevel", "L3");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL("AMediaDrm_setPropertyString securityLevel failed %d",
                      status)));
    return IPC_OK();
  }

  status = AMediaDrm_setPropertyString(mDrm, "privacyMode", "enable");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL("AMediaDrm_setPropertyString privateMode failed %d",
                      status)));
    return IPC_OK();
  }

  status = AMediaDrm_setPropertyString(mDrm, "sessionSharing", "enable");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL("AMediaDrm_setPropertyString sessionSharing failed %d",
                      status)));
    return IPC_OK();
  }

  // Set per-origin ID if available.
  if (!request.originID().IsEmpty()) {
    EME_LOG(
        "[{}] MediaDrmRemoteCDMParent::RecvInit -- "
        "setting origin ID property ({:.4}...)",
        fmt::ptr(this), request.originID().get());
    media_status_t originStatus =
        AMediaDrm_setPropertyString(mDrm, "origin", request.originID().get());
    if (originStatus != AMEDIA_OK) {
      EME_LOG(
          "[{}] MediaDrmRemoteCDMParent::RecvInit -- "
          "AMediaDrm_setPropertyString origin ID failed: {}",
          fmt::ptr(this), static_cast<int>(originStatus));
    }
  }

  status = AMediaDrm_setOnEventListener(mDrm, HandleEventCb);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL("AMediaDrm_setOnEventListener failed %d", status)));
    return IPC_OK();
  }

  if (__builtin_available(android 29, *)) {
    status =
        AMediaDrm_setOnExpirationUpdateListener(mDrm, HandleExpirationUpdateCb);
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      aResolver(MediaResult(
          NS_ERROR_DOM_INVALID_STATE_ERR,
          RESULT_DETAIL("AMediaDrm_setOnExpirationUpdateListener failed %d",
                        status)));
      return IPC_OK();
    }

    status = AMediaDrm_setOnKeysChangeListener(mDrm, HandleKeysChangeCb);
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      aResolver(MediaResult(
          NS_ERROR_DOM_INVALID_STATE_ERR,
          RESULT_DETAIL("AMediaDrm_setOnKeysChangeListener failed %d",
                        status)));
      return IPC_OK();
    }
  }

  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvInit -- drm {}", fmt::ptr(this),
          fmt::ptr(mDrm));
  InitializeStatics();
  sCbMap->insert(std::pair{mDrm, this});
  aResolver(MediaResult(NS_OK));

  EnsureHasAMediaCrypto();
  return IPC_OK();
}

RefPtr<MediaDrmRemoteCDMParent::InternalPromise>
MediaDrmRemoteCDMParent::EnsureHasAMediaCrypto(
    bool aProvisioned /* = false */) {
  if (NS_WARN_IF(!mDrm)) {
    return InternalPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Missing AMediaDrm"_ns),
        __func__);
  }

  if (HasCrypto()) {
    return InternalPromise::CreateAndResolve(true, __func__);
  }

  EME_LOG("[{}] MediaDrmRemoteCDMParent::EnsureHasAMediaCrypto -- open session",
          fmt::ptr(this));

  media_status_t status = OpenSession(mDrm, &mCryptoSessionId);
  if (status == AMEDIA_DRM_NOT_PROVISIONED) {
    if (aProvisioned) {
      return InternalPromise::CreateAndReject(
          MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                      "AMediaDrm_openSession failed for crypto after "
                      "provisioning, not provisioned or no session ID"_ns),
          __func__);
    }

    // Try once more even if this provisioning failed, because another actor
    // may have provisioned the device in the meantime.
    return EnsureProvisioned()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self = RefPtr{this}](const InternalPromise::ResolveOrRejectValue&) {
          return self->EnsureHasAMediaCrypto(/* aProvisioned */ true);
        });
  }

  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return InternalPromise::CreateAndReject(OpenSessionError(status, "crypto"),
                                            __func__);
  }

  AMediaCrypto* crypto =
      AMediaCrypto_new(mUuid, mCryptoSessionId.ptr, mCryptoSessionId.length);
  if (NS_WARN_IF(!crypto)) {
    AMediaDrm_closeSession(mDrm, &mCryptoSessionId);
    mCryptoSessionId = {};
    return InternalPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                    "AMediaCrypto_new failed"_ns),
        __func__);
  }

  MutexAutoLock lock(mMutex);
  mCrypto = MakeRefPtr<MediaDrmCrypto>(crypto);
  return InternalPromise::CreateAndResolve(true, __func__);
}

static Span<const uint8_t> TrimKeyRequest(Span<const uint8_t> aRequest) {
  if (__builtin_available(android 29, *)) {
    return aRequest;
  }

  Span<const uint8_t> request = widevine::TrimKeyRequest(aRequest);
  if (request.Length() != aRequest.Length()) {
    EME_LOG("TrimKeyRequest -- removing {} bytes",
            aRequest.Length() - request.Length());
  }
  return request;
}

RefPtr<MediaDrmRemoteCDMParent::InternalPromise>
MediaDrmRemoteCDMParent::EnsureProvisioned() {
  if (NS_WARN_IF(!mDrm)) {
    return InternalPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Missing AMediaDrm"_ns),
        __func__);
  }

  // There may already be a provision request outstanding.
  bool outstanding = !mProvisionPromise.IsEmpty();
  RefPtr<InternalPromise> p = mProvisionPromise.Ensure(__func__);
  if (outstanding) {
    return p;
  }

  EME_LOG("[{}] MediaDrmRemoteCDMParent::EnsureProvisioned -- get request",
          fmt::ptr(this));

  // AMediaDrm_getProvisionRequest requires the size to be non-zero. It does not
  // use the value for anything besides verification and overwrites in the
  // success case.
  size_t provisionRequestSize = std::numeric_limits<size_t>::max();
  const uint8_t* provisionRequest = nullptr;
  const char* serverUrl = nullptr;
  media_status_t status = AMediaDrm_getProvisionRequest(
      mDrm, &provisionRequest, &provisionRequestSize, &serverUrl);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    EME_LOG(
        "[{}] MediaDrmRemoteCDMParent::EnsureProvisioned -- failed drm {} "
        "provisionRequest {} size {} serverUrl {} ({}) status {}",
        fmt::ptr(this), fmt::ptr(mDrm), fmt::ptr(provisionRequest),
        provisionRequestSize, fmt::ptr(serverUrl), serverUrl ? serverUrl : "",
        static_cast<int>(status));
    mProvisionPromise.Reject(
        MediaResult(
            NS_ERROR_DOM_INVALID_STATE_ERR,
            RESULT_DETAIL("AMediaDrm_getProvisionRequest failed %d", status)),
        __func__);
    return p;
  }

  SendProvision(RemoteCDMProvisionRequestIPDL(
                    NS_ConvertUTF8toUTF16(serverUrl),
                    nsTArray<uint8_t>(provisionRequest, provisionRequestSize)))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}](RemoteCDMProvisionResponseIPDL&& aResponse) {
            if (aResponse.type() ==
                RemoteCDMProvisionResponseIPDL::TMediaResult) {
              EME_LOG(
                  "[{}] MediaDrmRemoteCDMParent::EnsureProvisioned -- response "
                  "failed",
                  fmt::ptr(self.get()));
              // The error code comes from the content process, so do not pass
              // it on to the page as is.
              self->mProvisionPromise.RejectIfExists(
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              aResponse.get_MediaResult().Message()),
                  __func__);
              return;
            }

            if (!self->mDrm) {
              return;
            }

            media_status_t status = AMediaDrm_provideProvisionResponse(
                self->mDrm, aResponse.get_ArrayOfuint8_t().Elements(),
                aResponse.get_ArrayOfuint8_t().Length());
            if (NS_WARN_IF(status != AMEDIA_OK)) {
              EME_LOG(
                  "[{}] MediaDrmRemoteCDMParent::EnsureProvisioned -- response "
                  "invalid",
                  fmt::ptr(self.get()));
              self->mProvisionPromise.RejectIfExists(
                  MediaResult(
                      NS_ERROR_DOM_INVALID_STATE_ERR,
                      RESULT_DETAIL(
                          "AMediaDrm_provideProvisionResponse failed %d",
                          status)),
                  __func__);
              return;
            }

            EME_LOG(
                "[{}] MediaDrmRemoteCDMParent::EnsureProvisioned -- success",
                fmt::ptr(self.get()));
            self->mProvisionPromise.ResolveIfExists(true, __func__);
          },
          [self = RefPtr{this}](const mozilla::ipc::ResponseRejectReason&) {
            self->mProvisionPromise.RejectIfExists(
                MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                            "Provisioning IPC failed"_ns),
                __func__);
          });
  return p;
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvCreateSession(
    RemoteCDMCreateSessionRequestIPDL&& aRequest,
    CreateSessionResolver&& aResolver) {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvCreateSession", fmt::ptr(this));
  DoCreateSession(std::move(aRequest), std::move(aResolver), false);
  return IPC_OK();
}

void MediaDrmRemoteCDMParent::DoCreateSession(
    RemoteCDMCreateSessionRequestIPDL&& aRequest,
    CreateSessionResolver&& aResolver, bool aProvisioned) {
  // If we are still provisioning, then the remote side should have queued the
  // requests.
  if (NS_WARN_IF(!mDrm)) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Missing AMediaDrm"_ns));
    return;
  }

  if (!HasCrypto()) {
    EnsureHasAMediaCrypto()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self = RefPtr{this}, request = std::move(aRequest),
         resolver = std::move(aResolver), aProvisioned](
            const InternalPromise::ResolveOrRejectValue& aValue) mutable {
          if (aValue.IsReject()) {
            resolver(aValue.RejectValue());
            return;
          }

          self->DoCreateSession(std::move(request), std::move(resolver),
                                aProvisioned);
        });
    return;
  }

  auto provisionThenRetry = [this](RemoteCDMCreateSessionRequestIPDL&& aRequest,
                                   CreateSessionResolver&& aResolver) {
    EnsureProvisioned()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self = RefPtr{this}, request = std::move(aRequest),
         resolver = std::move(aResolver)](
            const InternalPromise::ResolveOrRejectValue&) mutable {
          self->DoCreateSession(std::move(request), std::move(resolver), true);
        });
  };

  AMediaDrmSessionId sessionId;
  media_status_t status = OpenSession(mDrm, &sessionId);
  if (status == AMEDIA_DRM_NOT_PROVISIONED) {
    if (aProvisioned) {
      aResolver(
          MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                      "AMediaDrm_openSession failed for session after "
                      "provisioning, not provisioned or no session ID"_ns));
      return;
    }
    provisionThenRetry(std::move(aRequest), std::move(aResolver));
    return;
  }

  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(OpenSessionError(status, "session"));
    return;
  }

  NS_ConvertUTF16toUTF8 mimeType(aRequest.initDataType());

  const uint8_t* keyRequestData = nullptr;
  size_t keyRequestSize = 0;
  status =
      AMediaDrm_getKeyRequest(mDrm, &sessionId, aRequest.initData().Elements(),
                              aRequest.initData().Length(), mimeType.get(),
                              AMediaDrmKeyType::KEY_TYPE_STREAMING, nullptr, 0,
                              &keyRequestData, &keyRequestSize);

  if (status == AMEDIA_DRM_NOT_PROVISIONED) {
    AMediaDrm_closeSession(mDrm, &sessionId);
    if (aProvisioned) {
      aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                            "AMediaDrm_getKeyRequest reports not provisioned "
                            "after provisioning"_ns));
      return;
    }
    provisionThenRetry(std::move(aRequest), std::move(aResolver));
    return;
  }

  if (NS_WARN_IF(status != AMEDIA_OK)) {
    AMediaDrm_closeSession(mDrm, &sessionId);
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL("AMediaDrm_getKeyRequest failed %d", status)));
    return;
  }

  auto keyRequest = TrimKeyRequest(Span(keyRequestData, keyRequestSize));

  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(sessionId.ptr), sessionId.length);
  SessionEntry entry{sessionId, std::move(mimeType)};
  entry.awaitingCertificate = widevine::IsServiceCertificateRequest(keyRequest);
  mSessions[sessionIdStr] = std::move(entry);
  aResolver(RemoteCDMKeyMessageIPDL(std::move(sessionIdStr),
                                    MediaKeyMessageType::License_request,
                                    nsTArray<uint8_t>(keyRequest)));
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvLoadSession(
    const RemoteCDMLoadSessionRequestIPDL& aRequest,
    LoadSessionResolver&& aResolver) {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvLoadSession", fmt::ptr(this));
  aResolver(MediaResult(NS_ERROR_NOT_IMPLEMENTED));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvUpdateSession(
    const RemoteCDMUpdateSessionRequestIPDL& aRequest,
    UpdateSessionResolver&& aResolver) {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvUpdateSession", fmt::ptr(this));

  const auto i = mSessions.find(aRequest.sessionId());
  if (i == mSessions.end()) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Invalid session id"_ns));
    return IPC_OK();
  }

  MOZ_ASSERT(mDrm);
  MOZ_ASSERT(HasCrypto());

  Span<const uint8_t> response = aRequest.response();
  AMediaDrmKeySetId keySetId{};
  media_status_t status = AMediaDrm_provideKeyResponse(
      mDrm, &i->second.id, response.Elements(), response.Length(), &keySetId);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL("AMediaDrm_provideKeyResponse failed %d", status)));
    return IPC_OK();
  }

  if (i->second.awaitingCertificate) {
    i->second.awaitingCertificate = false;
    if (!__builtin_available(android 29, *)) {
      mCertificateQueue.AppendElement(aRequest.sessionId());
    }
  }

  // Android before 29 cannot report key statuses, so assume that the keys in
  // a license are usable.
  if (!__builtin_available(android 29, *)) {
    if (!widevine::IsServiceCertificateResponse(response)) {
      nsTArray<CDMKeyInfo> keyInfo;
      for (auto& keyId : widevine::ParseLicenseKeyIds(response)) {
        dom::Optional<dom::MediaKeyStatus> keyStatus;
        keyStatus.Construct(dom::MediaKeyStatus::Usable);
        keyInfo.AppendElement(
            CDMKeyInfo(std::move(keyId), std::move(keyStatus)));
      }

      if (!keyInfo.IsEmpty()) {
        (void)SendOnSessionKeyStatus(RemoteCDMKeyStatusIPDL(
            nsString(aRequest.sessionId()), std::move(keyInfo)));
      }
    }
  }

  aResolver(MediaResult(NS_OK));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvRemoveSession(
    const nsString& aSessionId, RemoveSessionResolver&& aResolver) {
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvRemoveSession", fmt::ptr(this));
  aResolver(MediaResult(NS_ERROR_NOT_IMPLEMENTED));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvCloseSession(
    const nsString& aSessionId, CloseSessionResolver&& aResolver) {
  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvCloseSession -- invalid session",
            fmt::ptr(this));
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Invalid session id"_ns));
    return IPC_OK();
  }

  MOZ_ASSERT(mDrm);
  MOZ_ASSERT(HasCrypto());

  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvCloseSession -- closeSession",
          fmt::ptr(this));
  media_status_t status = AMediaDrm_closeSession(mDrm, &i->second.id);
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvCloseSession -- status {}",
          fmt::ptr(this), static_cast<int>(status));
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                    RESULT_DETAIL("AMediaDrm_closeSession failed %d", status)));
  } else {
    aResolver(MediaResult(NS_OK));
  }

  mCertificateQueue.RemoveElement(aSessionId);
  mSessions.erase(i);
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmRemoteCDMParent::RecvSetServerCertificate(
    mozilla::Span<uint8_t const> aCertificate,
    SetServerCertificateResolver&& aResolver) {
  if (NS_WARN_IF(!mDrm)) {
    EME_LOG(
        "[{}] MediaDrmRemoteCDMParent::RecvSetServerCertificate -- not init",
        fmt::ptr(this));
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Missing AMediaDrm"_ns));
    return IPC_OK();
  }

  EME_LOG(
      "[{}] MediaDrmRemoteCDMParent::RecvSetServerCertificate -- "
      "setPropertyByteArray",
      fmt::ptr(this));
  media_status_t status = AMediaDrm_setPropertyByteArray(
      mDrm, "serviceCertificate", aCertificate.Elements(),
      aCertificate.Length());
  EME_LOG("[{}] MediaDrmRemoteCDMParent::RecvSetServerCertificate -- status {}",
          fmt::ptr(this), static_cast<int>(status));
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(
        NS_ERROR_DOM_INVALID_STATE_ERR,
        RESULT_DETAIL(
            "AMediaDrm_setPropertyByteArray serviceCertificate failed %d",
            status)));
    return IPC_OK();
  }

  aResolver(MediaResult(NS_OK));
  return IPC_OK();
}

void MediaDrmRemoteCDMParent::HandleEvent(nsString&& aSessionId,
                                          AMediaDrmEventType aEventType,
                                          int aExtra, nsTArray<uint8_t>&& aData,
                                          bool aProvisioned) {
  // Before Android 10, key request events have no session ID, so use the
  // session that was given a service certificate first.
  if (!__builtin_available(android 29, *)) {
    if (aSessionId.IsEmpty() &&
        aEventType == AMediaDrmEventType::EVENT_KEY_REQUIRED) {
      if (mCertificateQueue.IsEmpty()) {
        EME_LOG(
            "[{}] MediaDrmRemoteCDMParent::HandleEvent -- no session for event",
            fmt::ptr(this));
        return;
      }
      aSessionId = std::move(mCertificateQueue[0]);
      mCertificateQueue.RemoveElementAt(0);
    }
  }

  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    EME_LOG("[{}] MediaDrmRemoteCDMParent::HandleEvent -- session not found",
            fmt::ptr(this));
    return;
  }

  EME_LOG("[{}] MediaDrmRemoteCDMParent::HandleEvent -- event {}",
          fmt::ptr(this), static_cast<int>(aEventType));
  MOZ_ASSERT(mDrm);

  switch (aEventType) {
    case AMediaDrmEventType::EVENT_PROVISION_REQUIRED:
      EnsureProvisioned();
      break;
    case AMediaDrmEventType::EVENT_KEY_REQUIRED: {
      const uint8_t* keyRequestData = nullptr;
      size_t keyRequestSize = 0;
      AMediaDrmKeyRequestType keyRequestType =
          AMediaDrmKeyRequestType::KEY_REQUEST_TYPE_INITIAL;
      media_status_t status;

      if (__builtin_available(android 33, *)) {
        status = AMediaDrm_getKeyRequestWithDefaultUrlAndType(
            mDrm, &i->second.id, aData.Elements(), aData.Length(),
            i->second.mimeType.get(), AMediaDrmKeyType::KEY_TYPE_STREAMING,
            nullptr, 0, &keyRequestData, &keyRequestSize, nullptr,
            &keyRequestType);
      } else {
        status = AMediaDrm_getKeyRequest(
            mDrm, &i->second.id, aData.Elements(), aData.Length(),
            i->second.mimeType.get(), AMediaDrmKeyType::KEY_TYPE_STREAMING,
            nullptr, 0, &keyRequestData, &keyRequestSize);
      }

      if (status == AMEDIA_DRM_NOT_PROVISIONED) {
        if (aProvisioned) {
          EME_LOG(
              "[{}] MediaDrmRemoteCDMParent::HandleEvent -- getKeyRequest "
              "requests provisioning again after complete",
              fmt::ptr(this));
          return;
        }

        EnsureProvisioned()->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self = RefPtr{this}, sessionId = std::move(aSessionId),
             data = std::move(aData), aEventType,
             aExtra](const InternalPromise::ResolveOrRejectValue&) mutable {
              self->HandleEvent(std::move(sessionId), aEventType, aExtra,
                                std::move(data), true);
            });
        return;
      }

      if (NS_WARN_IF(status != AMEDIA_OK)) {
        EME_LOG(
            "[{}] MediaDrmRemoteCDMParent::HandleEvent -- getKeyRequest "
            "failed: {}",
            fmt::ptr(this), static_cast<int>(status));
        return;
      }

      auto keyRequest = TrimKeyRequest(Span(keyRequestData, keyRequestSize));

      dom::MediaKeyMessageType keyMessageType;
      switch (keyRequestType) {
        case AMediaDrmKeyRequestType::KEY_REQUEST_TYPE_NONE:
          // Already have what we need.
          return;
        case AMediaDrmKeyRequestType::KEY_REQUEST_TYPE_RELEASE:
          keyMessageType = MediaKeyMessageType::License_release;
          break;
        case AMediaDrmKeyRequestType::KEY_REQUEST_TYPE_RENEWAL:
          keyMessageType = MediaKeyMessageType::License_renewal;
          break;
        case AMediaDrmKeyRequestType::KEY_REQUEST_TYPE_UPDATE:
          // Not directly equivalent but needs an additional license request.
          keyMessageType = MediaKeyMessageType::License_request;
          break;
        default:
          MOZ_FALLTHROUGH_ASSERT("Unhandled AMediaDrmKeyRequestType");
        case AMediaDrmKeyRequestType::KEY_REQUEST_TYPE_INITIAL:
          keyMessageType = MediaKeyMessageType::License_request;
          break;
      }

      (void)SendOnSessionKeyMessage(
          RemoteCDMKeyMessageIPDL(std::move(aSessionId), keyMessageType,
                                  nsTArray<uint8_t>(keyRequest)));
    } break;
    case AMediaDrmEventType::EVENT_KEY_EXPIRED:
      break;
    case AMediaDrmEventType::EVENT_VENDOR_DEFINED:
      break;
    case AMediaDrmEventType::EVENT_SESSION_RECLAIMED:
      break;
    default:
      break;
  }
}

void MediaDrmRemoteCDMParent::HandleExpirationUpdate(nsString&& aSessionId,
                                                     int64_t aExpiryTimeInMS) {
  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    EME_LOG(
        "[{}] MediaDrmRemoteCDMParent::HandleExpirationUpdate -- session not "
        "found",
        fmt::ptr(this));
    return;
  }

  EME_LOG("[{}] MediaDrmRemoteCDMParent::HandleExpirationUpdate",
          fmt::ptr(this));
  (void)SendOnSessionKeyExpiration(RemoteCDMKeyExpirationIPDL(
      std::move(aSessionId), static_cast<double>(aExpiryTimeInMS)));
}

void MediaDrmRemoteCDMParent::HandleKeysChange(
    nsString&& aSessionId, bool aHasNewUsableKey,
    nsTArray<CDMKeyInfo>&& aKeyInfo) {
  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    EME_LOG(
        "[{}] MediaDrmRemoteCDMParent::HandleKeysChange -- session not found",
        fmt::ptr(this));
    return;
  }

  EME_LOG("[{}] MediaDrmRemoteCDMParent::HandleKeysChange", fmt::ptr(this));
  (void)SendOnSessionKeyStatus(
      RemoteCDMKeyStatusIPDL(std::move(aSessionId), std::move(aKeyInfo)));
}

already_AddRefed<MediaDrmCryptoInfo> MediaDrmRemoteCDMParent::CreateCryptoInfo(
    MediaRawData* aSample) {
  MOZ_ASSERT(mDrm);

  if (NS_WARN_IF(!aSample)) {
    return nullptr;
  }

  const CryptoSample& cryptoObj = aSample->mCrypto;
  if (!cryptoObj.IsEncrypted()) {
    return nullptr;
  }

  uint32_t numSubSamples = std::min<uint32_t>(
      cryptoObj.mPlainSizes.Length(), cryptoObj.mEncryptedSizes.Length());
  MOZ_ASSERT(numSubSamples <= INT32_MAX);

  // Deep copy the plain and encrypted sizes so we can modify them.
  nsTArray<size_t> plainSizes(cryptoObj.mPlainSizes.Length());
  nsTArray<size_t> encryptedSizes(cryptoObj.mEncryptedSizes.Length());
  if (numSubSamples > 0) {
    uint32_t totalSubSamplesSize = 0;
    for (const auto& size : cryptoObj.mPlainSizes) {
      plainSizes.AppendElement(size);
      totalSubSamplesSize += size;
    }
    for (const auto& size : cryptoObj.mEncryptedSizes) {
      encryptedSizes.AppendElement(size);
      totalSubSamplesSize += size;
    }

    auto codecSpecificDataSize =
        CheckedInt<size_t>(aSample->Size()) - totalSubSamplesSize;
    if (!codecSpecificDataSize.isValid()) {
      MOZ_ASSERT_UNREACHABLE("totalSubSamplesSize greater than sample size");
      return nullptr;
    }

    // Size of codec specific data("CSD") for Android java::sdk::MediaCodec
    // usage should be included in the 1st plain size if it exists.
    if (codecSpecificDataSize.value() && !plainSizes.IsEmpty()) {
      // This shouldn't overflow as the the plain size should be UINT16_MAX at
      // most, and the CSD should never be that large. Checked int acts like a
      // diagnostic assert here to help catch if we ever have insane inputs.
      auto newLeadingPlainSize = codecSpecificDataSize + plainSizes[0];
      if (!newLeadingPlainSize.isValid()) {
        MOZ_ASSERT_UNREACHABLE("newLeadingPlainSize overflowed");
        return nullptr;
      }
      plainSizes[0] = newLeadingPlainSize.value();
    }
  } else {
    // MediaCodec expects us to provide at least one subsample. Whole-block full
    // sample encryption does not carry subsample information, so we need to
    // synthesize it by creating a subsample as big as the sample itself. See
    // bug 1759936.
    numSubSamples = 1;
    plainSizes.AppendElement(0);
    encryptedSizes.AppendElement(aSample->Size());
  }

  const CopyableTArray<uint8_t>* srcIV;
  cryptoinfo_mode_t mode;
  switch (cryptoObj.mCryptoScheme) {
    case CryptoScheme::None:
      mode = AMEDIACODECRYPTOINFO_MODE_CLEAR;
      srcIV = &cryptoObj.mIV;
      break;
    case CryptoScheme::Cenc:
      mode = AMEDIACODECRYPTOINFO_MODE_AES_CTR;
      srcIV = &cryptoObj.mIV;
      break;
    case CryptoScheme::Cbcs:
    case CryptoScheme::Cbcs_1_9:
      mode = AMEDIACODECRYPTOINFO_MODE_AES_CBC;
      srcIV = &cryptoObj.mConstantIV;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled CryptoScheme!");
      return nullptr;
  }

  uint8_t key[16] = {};
  uint8_t iv[16] = {};

  if (NS_WARN_IF(srcIV->Length() > sizeof(iv))) {
    MOZ_ASSERT_UNREACHABLE("IV too big for Android!");
    return nullptr;
  }

  if (NS_WARN_IF(cryptoObj.mKeyId.Length() > sizeof(key))) {
    MOZ_ASSERT_UNREACHABLE("Key too big for Android!");
    return nullptr;
  }

  if (!srcIV->IsEmpty()) {
    memcpy(iv, srcIV->Elements(), srcIV->Length());
  }

  if (!cryptoObj.mKeyId.IsEmpty()) {
    memcpy(key, cryptoObj.mKeyId.Elements(), cryptoObj.mKeyId.Length());
  }

  AMediaCodecCryptoInfo* cryptoInfo = AMediaCodecCryptoInfo_new(
      static_cast<int32_t>(numSubSamples), key, iv, mode, plainSizes.Elements(),
      encryptedSizes.Elements());
  if (NS_WARN_IF(!cryptoInfo)) {
    MOZ_ASSERT_UNREACHABLE("Failed to create AMediaCodecCryptoInfo");
    return nullptr;
  }

  if (mode == AMEDIACODECRYPTOINFO_MODE_AES_CBC) {
    cryptoinfo_pattern_t pattern = {};
    pattern.encryptBlocks = cryptoObj.mCryptByteBlock;
    pattern.skipBlocks = cryptoObj.mSkipByteBlock;
    AMediaCodecCryptoInfo_setPattern(cryptoInfo, &pattern);
  }

  return MakeAndAddRef<MediaDrmCryptoInfo>(cryptoInfo);
}

MediaDrmCrypto::~MediaDrmCrypto() { AMediaCrypto_delete(mCrypto); }

MediaDrmCryptoInfo::~MediaDrmCryptoInfo() {
  AMediaCodecCryptoInfo_delete(mCryptoInfo);
}

/* static */
void MediaDrmRemoteCDMParent::UnprovisionMediaDrmOrigins(
    const nsTArray<nsCString>& aOriginKeys) {
  EME_LOG("UnprovisionMediaDrmOrigins: unprovisioning {} origin(s)",
          aOriginKeys.Length());

  for (const auto& originKey : aOriginKeys) {
    AMediaDrm* drm = AMediaDrm_createByUUID(WIDEVINE_UUID);
    if (!drm) {
      EME_LOG("UnprovisionMediaDrmOrigins: failed to create AMediaDrm");
      continue;
    }

    media_status_t status =
        AMediaDrm_setPropertyString(drm, "origin", originKey.get());
    if (status != AMEDIA_OK) {
      EME_LOG("UnprovisionMediaDrmOrigins: setPropertyString origin failed: {}",
              static_cast<int>(status));
      AMediaDrm_release(drm);
      continue;
    }

    static constexpr char kUnprovision[] = "unprovision";
    status = AMediaDrm_provideProvisionResponse(
        drm, reinterpret_cast<const uint8_t*>(kUnprovision),
        sizeof(kUnprovision) - 1);
    if (status != AMEDIA_OK) {
      EME_LOG(
          "UnprovisionMediaDrmOrigins: provideProvisionResponse failed: {} "
          "(may already be unprovisioned)",
          static_cast<int>(status));
    }

    AMediaDrm_release(drm);
  }
}

}  // namespace mozilla
