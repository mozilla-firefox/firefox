/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteCDMProxy.h"

#include "mozilla/EMEOriginID.h"
#include "mozilla/RemoteDecodeUtils.h"
#include "mozilla/dom/MediaKeySession.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/MediaDrmProvisioningHelper.h"
#endif

namespace mozilla {

#define LOGD(msg, ...) \
  MOZ_LOG_FMT(gRemoteDecodeLog, LogLevel::Debug, msg, ##__VA_ARGS__)

RemoteCDMProxy::RemoteCDMProxy(
    nsCOMPtr<nsISerialEventTarget>&& aThread,
    RefPtr<GenericNonExclusivePromise>&& aIPDLPromise, RemoteMediaIn aLocation,
    dom::MediaKeys* aKeys, const nsAString& aKeySystem,
    bool aDistinctiveIdentifierRequired, bool aPersistentStateRequired)
    : CDMProxy(aKeys, aKeySystem, aDistinctiveIdentifierRequired,
               aPersistentStateRequired),
      mChild(MakeRefPtr<RemoteCDMChild>()),
      mThread(std::move(aThread)),
      mIPDLPromise(std::move(aIPDLPromise)),
      mLocation(aLocation) {
  mChild->Initialize(this);
}

RemoteCDMProxy::~RemoteCDMProxy() { Destroy(); }

void RemoteCDMProxy::Destroy() {
  mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [child = mChild]() { child->Destroy(); }));
}

void RemoteCDMProxy::OnProvision(
    const RemoteCDMProvisionRequestIPDL& aRequest,
    PRemoteCDMChild::ProvisionResolver&& aResolver) {
  LOGD("[{}] RemoteCDMProxy::OnProvision", fmt::ptr(this));
#ifdef MOZ_WIDGET_ANDROID
  auto helper =
      MakeRefPtr<MediaDrmProvisioningHelper>(aRequest, std::move(aResolver));
  helper->Provision();
#else
  aResolver(MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR));
#endif
}

void RemoteCDMProxy::OnSessionKeyStatus(const RemoteCDMKeyStatusIPDL& aMsg) {
  LOGD("[{}] RemoteCDMProxy::OnSessionKeyStatus", fmt::ptr(this));
  bool changed = false;
  {
    auto caps = mCapabilites.Lock();
    for (const auto& keyInfo : aMsg.keyInfo()) {
      changed |=
          caps->SetKeyStatus(keyInfo.mKeyId, aMsg.sessionId(), keyInfo.mStatus);
    }
  }

  if (!changed) {
    return;
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = aMsg.sessionId()]() {
        if (self->mKeys.IsNull()) {
          return;
        }
        if (RefPtr<dom::MediaKeySession> session =
                self->mKeys->GetSession(sessionId)) {
          session->DispatchKeyStatusesChange();
        }
      }));
}

void RemoteCDMProxy::OnSessionKeyExpiration(RemoteCDMKeyExpirationIPDL&& aMsg) {
  LOGD("[{}] RemoteCDMProxy::OnSessionKeyExpiration", fmt::ptr(this));
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, msg = std::move(aMsg)]() {
        if (self->mKeys.IsNull()) {
          return;
        }
        if (RefPtr<dom::MediaKeySession> session =
                self->mKeys->GetSession(msg.sessionId())) {
          session->SetExpiration(msg.expiredTimeMilliSecondsSinceEpoch());
        }
      }));
}

void RemoteCDMProxy::OnSessionKeyMessage(RemoteCDMKeyMessageIPDL&& aMsg) {
  LOGD("[{}] RemoteCDMProxy::OnSessionKeyMessage", fmt::ptr(this));
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, msg = std::move(aMsg)]() {
        if (self->mKeys.IsNull()) {
          return;
        }
        if (RefPtr<dom::MediaKeySession> session =
                self->mKeys->GetSession(msg.sessionId())) {
          session->DispatchKeyMessage(msg.type(), msg.message());
        }
      }));
}

void RemoteCDMProxy::Init(PromiseId aPromiseId, const nsAString& aOrigin,
                          const nsAString& aTopLevelOrigin,
                          const nsAString& aName) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mKeys.IsNull()) {
    return;
  }

  LOGD("[{}] RemoteCDMProxy::Init -- promise {}", fmt::ptr(this), aPromiseId);
  if (!mIPDLPromise) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMChild already initialized"_ns));
    return;
  }

  RefPtr<GenericNonExclusivePromise> ipdlPromise = mIPDLPromise;
  mIPDLPromise = nullptr;

  GetEMEOriginID(mKeys->GetPrincipal())
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr{this}, aPromiseId, ipdlPromise](
              const media::PrincipalKeyPromise::ResolveOrRejectValue& aValue) {
            if (self->mKeys.IsNull()) {
              return;
            }
            nsCString originID;
            if (aValue.IsResolve()) {
              originID = aValue.ResolveValue();
            }
            // On rejection, proceed without origin ID.
            ipdlPromise->Then(
                self->mThread, __func__,
                [self, aPromiseId, originID = std::move(originID)](
                    const GenericNonExclusivePromise::ResolveOrRejectValue&
                        aValue) {
                  LOGD("[{}] RemoteCDMChild::Init -- promise {} resolved {}",
                       fmt::ptr(self.get()), aPromiseId, aValue.IsResolve());

                  if (aValue.IsReject()) {
                    // The GPU process, or decoding in it, is disabled.
                    if (self->mLocation == RemoteMediaIn::GpuProcess &&
                        aValue.RejectValue() == NS_ERROR_NOT_AVAILABLE) {
                      self->FallbackToRDD(aPromiseId, originID);
                      return;
                    }
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                                    "PRemoteCDMChild ensure process fail"_ns));
                    return;
                  }
                  self->InitInternal(aPromiseId, originID);
                });
          });
}

void RemoteCDMProxy::FallbackToRDD(PromiseId aPromiseId,
                                   const nsCString& aOriginID) {
  LOGD("[{}] RemoteCDMProxy::FallbackToRDD -- promise {}", fmt::ptr(this),
       aPromiseId);
  MOZ_ASSERT(mLocation == RemoteMediaIn::GpuProcess);
  mLocation = RemoteMediaIn::RddProcess;
  RemoteMediaManagerChild::LaunchRDDProcessIfNeeded()->Then(
      mThread, __func__,
      [self = RefPtr{this}, aPromiseId, originID = nsCString(aOriginID)](
          const GenericNonExclusivePromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsReject()) {
          self->RejectPromise(
              aPromiseId,
              MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "PRemoteCDMChild ensure process fail"_ns));
          return;
        }
        self->InitInternal(aPromiseId, originID);
      });
}

void RemoteCDMProxy::InitInternal(PromiseId aPromiseId,
                                  const nsCString& aOriginID) {
  LOGD("[{}] RemoteCDMProxy::InitInternal -- promise {}", fmt::ptr(this),
       aPromiseId);
  RefPtr<RemoteMediaManagerChild> manager =
      RemoteMediaManagerChild::GetSingleton(mLocation);
  if (!manager) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMChild manager is not available"_ns));
    return;
  }

  LOGD("[{}] RemoteCDMProxy::InitInternal -- send constructor", fmt::ptr(this));
  if (!manager->SendPRemoteCDMConstructor(mChild, mKeySystem)) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMChild manager is unable to send"_ns));
    return;
  }

  LOGD("[{}] RemoteCDMProxy::InitInternal -- send init", fmt::ptr(this));
  mChild
      ->SendInit(RemoteCDMInitRequestIPDL(mDistinctiveIdentifierRequired,
                                          mPersistentStateRequired, aOriginID))
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr{this},
           aPromiseId](const PRemoteCDMChild::InitPromise::ResolveOrRejectValue&
                           aValue) {
            LOGD("[{}] RemoteCDMProxy::InitInternal -- promise {} resolved {}",
                 fmt::ptr(self.get()), aPromiseId, aValue.IsResolve());

            if (self->mKeys.IsNull()) {
              return;
            }

            if (aValue.IsReject()) {
              self->RejectPromise(
                  aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMProxy::SendInit IPC fail"_ns));
              return;
            }

            if (NS_FAILED(aValue.ResolveValue().Code())) {
              self->RejectPromise(aPromiseId, aValue.ResolveValue());
              return;
            }

            self->mKeys->OnCDMCreated(aPromiseId, 0);
          });
}

void RemoteCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                                   dom::MediaKeySessionType aSessionType,
                                   PromiseId aPromiseId,
                                   const nsAString& aInitDataType,
                                   nsTArray<uint8_t>& aInitData) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, aCreateSessionToken, aSessionType,
                 aPromiseId, initDataType = nsString(aInitDataType),
                 initData = std::move(aInitData)]() mutable {
        LOGD("[{}] RemoteCDMProxy::CreateSession -- promise {}",
             fmt::ptr(self.get()), aPromiseId);
        self->mChild
            ->SendCreateSession(RemoteCDMCreateSessionRequestIPDL(
                aSessionType, std::move(initDataType), std::move(initData)))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aCreateSessionToken,
                 aPromiseId](const PRemoteCDMChild::CreateSessionPromise::
                                 ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMProxy::SendCreateSession IPC fail"_ns));
                    return;
                  }

                  const auto& response = aValue.ResolveValue();
                  if (response.type() ==
                      RemoteCDMSessionResponseIPDL::TMediaResult) {
                    self->RejectPromise(aPromiseId, response.get_MediaResult());
                    return;
                  }

                  const auto& msg = response.get_RemoteCDMKeyMessageIPDL();
                  const auto& sessionId = msg.sessionId();
                  if (RefPtr<dom::MediaKeySession> session =
                          self->mKeys->GetPendingSession(aCreateSessionToken)) {
                    session->SetSessionId(sessionId);
                    session->DispatchKeyMessage(msg.type(), msg.message());
                  }

                  self->ResolvePromise(aPromiseId);
                });
      })));
}

void RemoteCDMProxy::LoadSession(PromiseId aPromiseId,
                                 dom::MediaKeySessionType aSessionType,
                                 const nsAString& aSessionId) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, aPromiseId, aSessionType,
                 sessionId = nsString(aSessionId)]() mutable {
        LOGD("[{}] RemoteCDMProxy::LoadSession -- promise {}",
             fmt::ptr(self.get()), aPromiseId);
        self->mChild
            ->SendLoadSession(RemoteCDMLoadSessionRequestIPDL(
                aSessionType, std::move(sessionId)))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](const PRemoteCDMChild::LoadSessionPromise::
                                       ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  self->mKeys->OnSessionLoaded(
                      aPromiseId, aValue.IsResolve() &&
                                      NS_SUCCEEDED(aValue.ResolveValue()));
                });
      })));
}

void RemoteCDMProxy::SetServerCertificate(PromiseId aPromiseId,
                                          nsTArray<uint8_t>& aCert) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__,
      [self = RefPtr{this}, aPromiseId, cert = std::move(aCert)]() mutable {
        LOGD("[{}] RemoteCDMProxy::SetServerCertificate -- promise {}",
             fmt::ptr(self.get()), aPromiseId);
        self->mChild->SendSetServerCertificate(std::move(cert))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](
                    const PRemoteCDMChild::SetServerCertificatePromise::
                        ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMProxy::SendSetServerCertificate IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMProxy::UpdateSession(const nsAString& aSessionId,
                                   PromiseId aPromiseId,
                                   nsTArray<uint8_t>& aResponse) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = nsString(aSessionId),
                 aPromiseId, response = std::move(aResponse)]() mutable {
        LOGD("[{}] RemoteCDMProxy::UpdateSession -- promise {}",
             fmt::ptr(self.get()), aPromiseId);
        self->mChild
            ->SendUpdateSession(RemoteCDMUpdateSessionRequestIPDL(
                std::move(sessionId), std::move(response)))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](const PRemoteCDMChild::UpdateSessionPromise::
                                       ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMProxy::SendUpdateSession IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMProxy::CloseSession(const nsAString& aSessionId,
                                  PromiseId aPromiseId) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = nsString(aSessionId),
                 aPromiseId]() mutable {
        LOGD("[{}] RemoteCDMProxy::CloseSession -- promise {}",
             fmt::ptr(self.get()), aPromiseId);
        self->mChild->SendCloseSession(sessionId)->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [self, aPromiseId,
             sessionId](const PRemoteCDMChild::CloseSessionPromise::
                            ResolveOrRejectValue& aValue) {
              if (self->mKeys.IsNull()) {
                return;
              }

              bool keyStatusesChange = false;
              {
                auto caps = self->mCapabilites.Lock();
                keyStatusesChange = caps->RemoveKeysForSession(sessionId);
              }
              if (RefPtr<dom::MediaKeySession> session =
                      self->mKeys->GetSession(sessionId)) {
                if (keyStatusesChange) {
                  session->DispatchKeyStatusesChange();
                }
                session->OnClosed(
                    dom::MediaKeySessionClosedReason::Closed_by_application);
              }

              if (aValue.IsReject()) {
                self->RejectPromise(
                    aPromiseId,
                    MediaResult(
                        NS_ERROR_DOM_INVALID_STATE_ERR,
                        "PRemoteCDMProxy::SendCloseSession IPC fail"_ns));
                return;
              }

              self->ResolveOrRejectPromise(aPromiseId, aValue.ResolveValue());
            });
      })));
}

void RemoteCDMProxy::RemoveSession(const nsAString& aSessionId,
                                   PromiseId aPromiseId) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = nsString(aSessionId),
                 aPromiseId]() mutable {
        LOGD("[{}] RemoteCDMProxy::RemoveSession -- promise {}",
             fmt::ptr(self.get()), aPromiseId);
        self->mChild->SendRemoveSession(std::move(sessionId))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](const PRemoteCDMChild::RemoveSessionPromise::
                                       ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMProxy::SendRemoveSession IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMProxy::QueryOutputProtectionStatus() {}

void RemoteCDMProxy::NotifyOutputProtectionStatus(
    OutputProtectionCheckStatus aCheckStatus,
    OutputProtectionCaptureStatus aCaptureStatus) {}

void RemoteCDMProxy::Shutdown() {
  LOGD("[{}] RemoteCDMProxy::Shutdown", fmt::ptr(this));
  mKeys.Clear();
  Destroy();
}

void RemoteCDMProxy::Terminated() {
  LOGD("[{}] RemoteCDMProxy::Terminated", fmt::ptr(this));
  NS_DispatchToMainThread(
      NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
        if (!self->mKeys.IsNull()) {
          self->mKeys->Terminated();
        }
      }));
}

void RemoteCDMProxy::OnSetSessionId(uint32_t aCreateSessionToken,
                                    const nsAString& aSessionId) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::OnResolveLoadSessionPromise(uint32_t aPromiseId,
                                                 bool aSuccess) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::OnSessionMessage(const nsAString& aSessionId,
                                      dom::MediaKeyMessageType aMessageType,
                                      const nsTArray<uint8_t>& aMessage) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::OnExpirationChange(const nsAString& aSessionId,
                                        UnixTime aExpiryTime) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::OnSessionClosed(const nsAString& aSessionId,
                                     dom::MediaKeySessionClosedReason aReason) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::OnSessionError(const nsAString& aSessionId,
                                    nsresult aException, uint32_t aSystemCode,
                                    const nsAString& aMsg) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::OnRejectPromise(uint32_t aPromiseId,
                                     ErrorResult&& aException,
                                     const nsCString& aMsg) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

RefPtr<DecryptPromise> RemoteCDMProxy::Decrypt(MediaRawData* aSample) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
  return nullptr;
}

void RemoteCDMProxy::OnDecrypted(uint32_t aId, DecryptStatus aResult,
                                 const nsTArray<uint8_t>& aDecryptedData) {
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
}

void RemoteCDMProxy::RejectPromise(PromiseId aId, ErrorResult&& aException,
                                   const nsCString& aReason) {
  LOGD("[{}] RemoteCDMProxy::RejectPromise -- {}", fmt::ptr(this), aId);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mKeys.IsNull());
  mKeys->RejectPromise(aId, std::move(aException), aReason);
}

void RemoteCDMProxy::ResolvePromise(PromiseId aId) {
  LOGD("[{}] RemoteCDMProxy::ResolvePromise -- {}", fmt::ptr(this), aId);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mKeys.IsNull());
  mKeys->ResolvePromise(aId);
}

void RemoteCDMProxy::RejectPromise(PromiseId aId, const MediaResult& aResult) {
  MOZ_ASSERT(NS_FAILED(aResult.Code()));

  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction(__func__, [self = RefPtr{this}, aId, aResult]() {
          if (self->mKeys.IsNull()) {
            return;
          }
          self->RejectPromise(aId, aResult);
        }));
    return;
  }

  ErrorResult rv;
  aResult.ThrowTo(rv);
  RejectPromise(aId, std::move(rv), aResult.Message());
}

void RemoteCDMProxy::ResolveOrRejectPromise(PromiseId aId,
                                            const MediaResult& aResult) {
  if (aResult.Code() == NS_OK) {
    ResolvePromise(aId);
    return;
  }

  RejectPromise(aId, aResult);
}

void RemoteCDMProxy::OnKeyStatusesChange(const nsAString& aSessionId) {}

void RemoteCDMProxy::GetStatusForPolicy(
    PromiseId aPromiseId, const dom::HDCPVersion& aMinHdcpVersion) {
  RejectPromise(
      aPromiseId,
      MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                  "Currently Fennec does not support GetStatusForPolicy"));
}

#ifdef DEBUG
bool RemoteCDMProxy::IsOnOwnerThread() { return mThread->IsOnCurrentThread(); }
#endif

#undef LOGD

}  // namespace mozilla
