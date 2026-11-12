/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/MediaDrmCDMProxy.h"

#include "MediaDrmCDMCallbackProxy.h"
#include "mozilla/EMEOriginID.h"
#include "mozilla/dom/MediaKeySession.h"

namespace mozilla {

MediaDrmSessionType ToMediaDrmSessionType(
    dom::MediaKeySessionType aSessionType) {
  switch (aSessionType) {
    case dom::MediaKeySessionType::Temporary:
      return kKeyStreaming;
    case dom::MediaKeySessionType::Persistent_license:
      return kKeyOffline;
    default:
      return kKeyStreaming;
  };
}

MediaDrmCDMProxy::MediaDrmCDMProxy(dom::MediaKeys* aKeys,
                                   const nsAString& aKeySystem,
                                   bool aDistinctiveIdentifierRequired,
                                   bool aPersistentStateRequired)
    : CDMProxy(aKeys, aKeySystem, aDistinctiveIdentifierRequired,
               aPersistentStateRequired),
      mCDM(nullptr),
      mShutdownCalled(false) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_COUNT_CTOR(MediaDrmCDMProxy);
}

MediaDrmCDMProxy::~MediaDrmCDMProxy() { MOZ_COUNT_DTOR(MediaDrmCDMProxy); }

void MediaDrmCDMProxy::Init(PromiseId aPromiseId, const nsAString& aOrigin,
                            const nsAString& aTopLevelOrigin,
                            const nsAString& aName) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  EME_LOG("MediaDrmCDMProxy::Init ({}, {}) {}",
          NS_ConvertUTF16toUTF8(aOrigin).get(),
          NS_ConvertUTF16toUTF8(aTopLevelOrigin).get(),
          NS_ConvertUTF16toUTF8(aName).get());

  GetEMEOriginID(mKeys->GetPrincipal())
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr{this}, aPromiseId](
              const media::PrincipalKeyPromise::ResolveOrRejectValue& aValue) {
            nsCString originID;
            if (aValue.IsResolve()) {
              originID = aValue.ResolveValue();
            }
            // On rejection, proceed without origin ID

            if (self->mKeys.IsNull()) {
              return;
            }

            // Create a thread to work with cdm.
            if (!self->mOwnerThread) {
              nsresult rv = NS_NewNamedThread(
                  "MDCDMThread", getter_AddRefs(self->mOwnerThread));
              if (NS_FAILED(rv)) {
                self->RejectPromiseWithStateError(
                    aPromiseId,
                    nsLiteralCString(
                        "Couldn't create CDM thread MediaDrmCDMProxy::Init"));
                return;
              }
            }

            self->mCDM = std::make_unique<MediaDrmProxySupport>(
                self->mKeySystem, originID);

            nsCOMPtr<nsIRunnable> task(NewRunnableMethod<uint32_t>(
                "MediaDrmCDMProxy::md_Init", self.get(),
                &MediaDrmCDMProxy::md_Init, aPromiseId));
            if (NS_FAILED(
                    self->mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL))) {
              self->RejectPromiseWithStateError(
                  aPromiseId,
                  "Failed to dispatch to CDM thread MediaDrmCDMProxy::Init"_ns);
            }
          });
}

void MediaDrmCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                                     dom::MediaKeySessionType aSessionType,
                                     PromiseId aPromiseId,
                                     const nsAString& aInitDataType,
                                     nsTArray<uint8_t>& aInitData) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);

  UniquePtr<CreateSessionData> data(new CreateSessionData());
  data->mSessionType = aSessionType;
  data->mCreateSessionToken = aCreateSessionToken;
  data->mPromiseId = aPromiseId;
  data->mInitDataType = NS_ConvertUTF16toUTF8(aInitDataType);
  data->mInitData = std::move(aInitData);

  nsCOMPtr<nsIRunnable> task(NewRunnableMethod<UniquePtr<CreateSessionData>&&>(
      "MediaDrmCDMProxy::md_CreateSession", this,
      &MediaDrmCDMProxy::md_CreateSession, std::move(data)));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void MediaDrmCDMProxy::LoadSession(PromiseId aPromiseId,
                                   dom::MediaKeySessionType aSessionType,
                                   const nsAString& aSessionId) {
  // TODO: Implement LoadSession.
  RejectPromiseWithStateError(
      aPromiseId, "Currently Fennec does not support LoadSession"_ns);
}

void MediaDrmCDMProxy::SetServerCertificate(PromiseId aPromiseId,
                                            nsTArray<uint8_t>& aCert) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);

  mOwnerThread->Dispatch(NewRunnableMethod<PromiseId, const nsTArray<uint8_t>>(
                             "MediaDrmCDMProxy::md_SetServerCertificate", this,
                             &MediaDrmCDMProxy::md_SetServerCertificate,
                             aPromiseId, std::move(aCert)),
                         NS_DISPATCH_NORMAL);
}

void MediaDrmCDMProxy::UpdateSession(const nsAString& aSessionId,
                                     PromiseId aPromiseId,
                                     nsTArray<uint8_t>& aResponse) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  UniquePtr<UpdateSessionData> data(new UpdateSessionData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);
  data->mResponse = std::move(aResponse);

  nsCOMPtr<nsIRunnable> task(NewRunnableMethod<UniquePtr<UpdateSessionData>&&>(
      "MediaDrmCDMProxy::md_UpdateSession", this,
      &MediaDrmCDMProxy::md_UpdateSession, std::move(data)));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void MediaDrmCDMProxy::CloseSession(const nsAString& aSessionId,
                                    PromiseId aPromiseId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerThread);
  NS_ENSURE_TRUE_VOID(!mKeys.IsNull());

  UniquePtr<SessionOpData> data(new SessionOpData());
  data->mPromiseId = aPromiseId;
  data->mSessionId = NS_ConvertUTF16toUTF8(aSessionId);

  nsCOMPtr<nsIRunnable> task(NewRunnableMethod<UniquePtr<SessionOpData>&&>(
      "MediaDrmCDMProxy::md_CloseSession", this,
      &MediaDrmCDMProxy::md_CloseSession, std::move(data)));
  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
}

void MediaDrmCDMProxy::RemoveSession(const nsAString& aSessionId,
                                     PromiseId aPromiseId) {
  // TODO: Implement RemoveSession.
  RejectPromiseWithStateError(
      aPromiseId, "Currently Fennec does not support RemoveSession"_ns);
}

void MediaDrmCDMProxy::QueryOutputProtectionStatus() {
  // TODO(bryce): determine if this is needed for Android and implement as
  // needed. See also `NotifyOutputProtectionStatus`.
}

void MediaDrmCDMProxy::NotifyOutputProtectionStatus(
    OutputProtectionCheckStatus aCheckStatus,
    OutputProtectionCaptureStatus aCaptureStatus) {
  // TODO(bryce): determine if this is needed for Android and implement as
  // needed. See also `QueryOutputProtectionStatus`.
}

void MediaDrmCDMProxy::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  mKeys.Clear();
  if (!mOwnerThread) {
    return;
  }
  nsCOMPtr<nsIRunnable> task(NewRunnableMethod(
      "MediaDrmCDMProxy::md_Shutdown", this, &MediaDrmCDMProxy::md_Shutdown));

  mOwnerThread->Dispatch(task, NS_DISPATCH_NORMAL);
  mOwnerThread->Shutdown();
  mOwnerThread = nullptr;
}

void MediaDrmCDMProxy::Terminated() {
  // TODO: Implement Terminated.
  // Should find a way to handle the case when remote side MediaDrm crashed.
}

void MediaDrmCDMProxy::OnSetSessionId(uint32_t aCreateSessionToken,
                                      const nsAString& aSessionId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }

  RefPtr<dom::MediaKeySession> session(
      mKeys->GetPendingSession(aCreateSessionToken));
  if (session) {
    session->SetSessionId(aSessionId);
  }
}

void MediaDrmCDMProxy::OnResolveLoadSessionPromise(uint32_t aPromiseId,
                                                   bool aSuccess) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  mKeys->OnSessionLoaded(aPromiseId, aSuccess);
}

void MediaDrmCDMProxy::OnSessionMessage(const nsAString& aSessionId,
                                        dom::MediaKeyMessageType aMessageType,
                                        const nsTArray<uint8_t>& aMessage) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->DispatchKeyMessage(aMessageType, aMessage);
  }
}

void MediaDrmCDMProxy::OnExpirationChange(const nsAString& aSessionId,
                                          UnixTime aExpiryTime) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->SetExpiration(static_cast<double>(aExpiryTime));
  }
}

void MediaDrmCDMProxy::OnSessionClosed(
    const nsAString& aSessionId, dom::MediaKeySessionClosedReason aReason) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->OnClosed(aReason);
  }
}

void MediaDrmCDMProxy::OnSessionError(const nsAString& aSessionId,
                                      nsresult aException, uint32_t aSystemCode,
                                      const nsAString& aMsg) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->DispatchKeyError(aSystemCode);
  }
}

void MediaDrmCDMProxy::OnRejectPromise(uint32_t aPromiseId,
                                       ErrorResult&& aException,
                                       const nsCString& aMsg) {
  MOZ_ASSERT(NS_IsMainThread());
  RejectPromise(aPromiseId, std::move(aException), aMsg);
}

RefPtr<DecryptPromise> MediaDrmCDMProxy::Decrypt(MediaRawData* aSample) {
  MOZ_ASSERT_UNREACHABLE("Fennec could not handle decrypting individually");
  return nullptr;
}

void MediaDrmCDMProxy::OnDecrypted(uint32_t aId, DecryptStatus aResult,
                                   const nsTArray<uint8_t>& aDecryptedData) {
  MOZ_ASSERT_UNREACHABLE("Fennec could not handle decrypted event");
}

void MediaDrmCDMProxy::RejectPromise(PromiseId aId, ErrorResult&& aException,
                                     const nsCString& aReason) {
  if (NS_IsMainThread()) {
    if (!mKeys.IsNull()) {
      mKeys->RejectPromise(aId, std::move(aException), aReason);
    }
  } else {
    nsCOMPtr<nsIRunnable> task(
        new RejectPromiseTask(this, aId, std::move(aException), aReason));
    mMainThread->Dispatch(task.forget(), NS_DISPATCH_NORMAL);
  }
}

void MediaDrmCDMProxy::RejectPromiseWithStateError(PromiseId aId,
                                                   const nsCString& aReason) {
  ErrorResult rv;
  rv.ThrowInvalidStateError(aReason);
  RejectPromise(aId, std::move(rv), aReason);
}

void MediaDrmCDMProxy::ResolvePromise(PromiseId aId) {
  if (NS_IsMainThread()) {
    if (!mKeys.IsNull()) {
      mKeys->ResolvePromise(aId);
    } else {
      NS_WARNING("MediaDrmCDMProxy unable to resolve promise!");
    }
  } else {
    nsCOMPtr<nsIRunnable> task;
    task =
        NewRunnableMethod<PromiseId>("MediaDrmCDMProxy::ResolvePromise", this,
                                     &MediaDrmCDMProxy::ResolvePromise, aId);
    mMainThread->Dispatch(task.forget(), NS_DISPATCH_NORMAL);
  }
}

template <typename T>
void MediaDrmCDMProxy::ResolvePromiseWithResult(PromiseId aId,
                                                const T& aResult) {
  if (NS_IsMainThread()) {
    if (!mKeys.IsNull()) {
      mKeys->ResolvePromiseWithResult(aId, aResult);
    } else {
      NS_WARNING("MediaDrmCDMProxy unable to resolve promise!");
    }
    return;
  }

  nsCOMPtr<nsIRunnable> task;
  task = NewRunnableMethod<PromiseId, T>(
      "MediaDrmCDMProxy::ResolvePromiseWithResult", this,
      &MediaDrmCDMProxy::ResolvePromiseWithResult<T>, aId, aResult);
  mMainThread->Dispatch(task.forget(), NS_DISPATCH_NORMAL);
}

void MediaDrmCDMProxy::OnKeyStatusesChange(const nsAString& aSessionId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }
  RefPtr<dom::MediaKeySession> session(mKeys->GetSession(aSessionId));
  if (session) {
    session->DispatchKeyStatusesChange();
  }
}

void MediaDrmCDMProxy::GetStatusForPolicy(
    PromiseId aPromiseId, const dom::HDCPVersion& aMinHdcpVersion) {
  // TODO: Implement GetStatusForPolicy.
  constexpr auto err =
      "Currently Fennec does not support GetStatusForPolicy"_ns;

  ErrorResult rv;
  rv.ThrowNotSupportedError(err);
  RejectPromise(aPromiseId, std::move(rv), err);
}

#ifdef DEBUG
bool MediaDrmCDMProxy::IsOnOwnerThread() {
  return NS_GetCurrentThread() == mOwnerThread;
}
#endif

const nsString& MediaDrmCDMProxy::GetMediaDrmStubId() const {
  MOZ_ASSERT(mCDM);
  return mCDM->GetMediaDrmStubId();
}

void MediaDrmCDMProxy::OnCDMCreated(uint32_t aPromiseId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mKeys.IsNull()) {
    return;
  }

  if (mCDM) {
    mKeys->OnCDMCreated(aPromiseId, 0);
    return;
  }

  // No CDM? Just reject the promise.
  constexpr auto err = "Null CDM in OnCDMCreated()"_ns;
  ErrorResult rv;
  rv.ThrowInvalidStateError(err);
  mKeys->RejectPromise(aPromiseId, std::move(rv), err);
}

void MediaDrmCDMProxy::md_Init(uint32_t aPromiseId) {
  MOZ_ASSERT(IsOnOwnerThread());
  MOZ_ASSERT(mCDM);

  UniquePtr<MediaDrmCDMCallbackProxy> callback(
      new MediaDrmCDMCallbackProxy(this));
  if (NS_FAILED(mCDM->Init(std::move(callback)))) {
    RejectPromiseWithStateError(
        aPromiseId, "Couldn't create the MediaDrm bridge in md_Init"_ns);
    return;
  }

  nsCOMPtr<nsIRunnable> task(
      NewRunnableMethod<uint32_t>("MediaDrmCDMProxy::OnCDMCreated", this,
                                  &MediaDrmCDMProxy::OnCDMCreated, aPromiseId));
  mMainThread->Dispatch(task.forget(), NS_DISPATCH_NORMAL);
}

void MediaDrmCDMProxy::md_CreateSession(UniquePtr<CreateSessionData>&& aData) {
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromiseWithStateError(aData->mPromiseId,
                                "Null CDM in md_CreateSession"_ns);
    return;
  }

  mCDM->CreateSession(aData->mCreateSessionToken, aData->mPromiseId,
                      aData->mInitDataType, aData->mInitData,
                      ToMediaDrmSessionType(aData->mSessionType));
}

void MediaDrmCDMProxy::md_SetServerCertificate(PromiseId aPromiseId,
                                               const nsTArray<uint8_t>& aCert) {
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromiseWithStateError(aPromiseId,
                                "Null CDM in md_SetServerCertificate"_ns);
    return;
  }

  if (mCDM->SetServerCertificate(aCert)) {
    ResolvePromiseWithResult(aPromiseId, true);
  } else {
    RejectPromiseWithStateError(
        aPromiseId, "MediaDrmCDMProxy unable to set server certificate"_ns);
  }
}

void MediaDrmCDMProxy::md_UpdateSession(UniquePtr<UpdateSessionData>&& aData) {
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromiseWithStateError(aData->mPromiseId,
                                "Null CDM in md_UpdateSession"_ns);
    return;
  }
  mCDM->UpdateSession(aData->mPromiseId, aData->mSessionId, aData->mResponse);
}

void MediaDrmCDMProxy::md_CloseSession(UniquePtr<SessionOpData>&& aData) {
  MOZ_ASSERT(IsOnOwnerThread());

  if (!mCDM) {
    RejectPromiseWithStateError(aData->mPromiseId,
                                "Null CDM in md_CloseSession"_ns);
    return;
  }
  mCDM->CloseSession(aData->mPromiseId, aData->mSessionId);
}

void MediaDrmCDMProxy::md_Shutdown() {
  MOZ_ASSERT(IsOnOwnerThread());
  MOZ_ASSERT(mCDM);
  if (mShutdownCalled) {
    return;
  }
  mShutdownCalled = true;
  mCDM->Shutdown();
  mCDM = nullptr;
}

}  // namespace mozilla
