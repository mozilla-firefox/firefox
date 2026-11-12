/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaKeys.h"

#include "ChromiumCDMProxy.h"
#include "GMPCrashHelper.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/MediaKeyError.h"
#include "mozilla/dom/MediaKeyMessageEvent.h"
#include "mozilla/dom/MediaKeySession.h"
#include "mozilla/dom/MediaKeySessionBinding.h"
#include "mozilla/dom/MediaKeyStatusMap.h"
#include "mozilla/dom/MediaKeySystemAccess.h"
#include "mozilla/dom/MediaKeysBinding.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "nsContentTypeParser.h"
#include "nsContentUtils.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidDecoderModule.h"
#  include "mozilla/MediaDrmCDMProxy.h"
#  include "mozilla/RemoteCDMProxy.h"
#  include "mozilla/RemoteMediaManagerChild.h"
#  include "mozilla/StaticPrefs_media.h"
#endif
#ifdef MOZ_WMF_CDM
#  include "mozilla/WMFCDMProxy.h"
#endif

namespace mozilla::dom {

// We don't use NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE because we need to
// disconnect our MediaKeys instances from the inner window (mparent) before
// we unlink it.
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(MediaKeys)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(MediaKeys)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mKeySessions)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPromises)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingSessions)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(MediaKeys)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mElement)
  tmp->DisconnectInnerWindow();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mKeySessions)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPromises)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingSessions)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaKeys)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaKeys)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaKeys)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END

MediaKeys::MediaKeys(nsPIDOMWindowInner* aParent, const nsAString& aKeySystem,
                     const MediaKeySystemConfiguration& aConfig)
    : mParent(aParent),
      mKeySystem(aKeySystem),
      mCreatePromiseId(0),
      mConfig(aConfig) {
  EME_LOG("MediaKeys[{}] constructed keySystem={}", fmt::ptr(this),
          NS_ConvertUTF16toUTF8(mKeySystem).get());
}

MediaKeys::~MediaKeys() {
  MOZ_ASSERT(NS_IsMainThread());

  DisconnectInnerWindow();
  Shutdown();
  EME_LOG("MediaKeys[{}] destroyed", fmt::ptr(this));
}

NS_IMETHODIMP MediaKeys::Observe(nsISupports* aSubject, const char* aTopic,
                                 const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!strcmp(aTopic, kMediaKeysResponseTopic),
             "Should only listen for responses to MediaKey requests");
  EME_LOG("MediaKeys[{}] observing message with aTopic={} aData={}",
          fmt::ptr(this), aTopic, NS_ConvertUTF16toUTF8(aData).get());
  if (!strcmp(aTopic, kMediaKeysResponseTopic)) {
    if (!mProxy) {
      // This may happen if we're notified during shutdown or startup. If this
      // is happening outside of those scenarios there's a bug.
      EME_LOG(
          "MediaKeys[{}] can't notify CDM of observed message as mProxy is "
          "unset",
          fmt::ptr(this));
      return NS_OK;
    }

    if (u"capture-possible"_ns.Equals(aData)) {
      mProxy->NotifyOutputProtectionStatus(
          CDMProxy::OutputProtectionCheckStatus::CheckSuccessful,
          CDMProxy::OutputProtectionCaptureStatus::CapturePossilbe);
    } else if (u"capture-not-possible"_ns.Equals(aData)) {
      mProxy->NotifyOutputProtectionStatus(
          CDMProxy::OutputProtectionCheckStatus::CheckSuccessful,
          CDMProxy::OutputProtectionCaptureStatus::CaptureNotPossible);
    } else {
      MOZ_ASSERT_UNREACHABLE("No code paths should lead to the failure case");
      // This should be unreachable, but gracefully handle in case.
      mProxy->NotifyOutputProtectionStatus(
          CDMProxy::OutputProtectionCheckStatus::CheckFailed,
          CDMProxy::OutputProtectionCaptureStatus::Unused);
    }
  }
  return NS_OK;
}

void MediaKeys::ConnectInnerWindow() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsPIDOMWindowInner> innerWindowParent = GetParentObject();
  MOZ_ASSERT(innerWindowParent,
             "We should only be connecting when we have an inner window!");
  innerWindowParent->AddMediaKeysInstance(this);
}

void MediaKeys::DisconnectInnerWindow() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!GetParentObject()) {
    // We don't have a parent. We've been cycle collected, or the window
    // already notified us of its destruction and we cleared the ref.
    return;
  }

  GetParentObject()->RemoveMediaKeysInstance(this);
}

void MediaKeys::OnInnerWindowDestroy() {
  MOZ_ASSERT(NS_IsMainThread());

  EME_LOG("MediaKeys[{}] OnInnerWindowDestroy()", fmt::ptr(this));

  // The InnerWindow should clear its reference to this object after this call,
  // so we don't need to explicitly call DisconnectInnerWindow before nulling.
  mParent = nullptr;

  // Don't call shutdown directly because (at time of writing) mProxy can
  // spin the event loop when it's shutdown. This can change the world state
  // in the middle of window destruction, which we do not want.
  GetMainThreadSerialEventTarget()->Dispatch(
      NewRunnableMethod("MediaKeys::Shutdown", this, &MediaKeys::Shutdown));
}

void MediaKeys::Terminated() {
  EME_LOG("MediaKeys[{}] CDM crashed unexpectedly", fmt::ptr(this));

  KeySessionHashMap keySessions;
  // Remove entries during iteration will screw it. Make a copy first.
  for (const RefPtr<MediaKeySession>& session : mKeySessions.Values()) {
    // XXX Could the RefPtr still be moved here?
    keySessions.InsertOrUpdate(session->GetSessionId(), RefPtr{session});
  }
  for (const RefPtr<MediaKeySession>& session : keySessions.Values()) {
    session->OnClosed(MediaKeySessionClosedReason::Internal_error);
  }
  keySessions.Clear();
  MOZ_ASSERT(mKeySessions.Count() == 0);

  // Notify the element about that CDM has terminated.
  if (mElement) {
    mElement->DecodeError(NS_ERROR_DOM_MEDIA_CDM_ERR);
  }

  Shutdown();
}

void MediaKeys::Shutdown() {
  // Hold a self reference to keep us alive after we clear the self reference
  // for each promise. This ensures we stay alive until we're done shutting
  // down.
  RefPtr<MediaKeys> selfReference = this;

  EME_LOG("MediaKeys[{}]::Shutdown()", fmt::ptr(this));
  if (mProxy) {
    RefPtr<CDMProxy> proxy = mProxy;
    proxy->Shutdown();
    mProxy = nullptr;
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService && mObserverAdded) {
    observerService->RemoveObserver(this, kMediaKeysResponseTopic);
  }

  for (const RefPtr<dom::DetailedPromise>& promise : mPromises.Values()) {
    promise->MaybeRejectWithInvalidStateError(
        "Promise still outstanding at MediaKeys shutdown");
    Release();
  }
  mPromises.Clear();
}

nsPIDOMWindowInner* MediaKeys::GetParentObject() const { return mParent; }

JSObject* MediaKeys::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return MediaKeys_Binding::Wrap(aCx, this, aGivenProto);
}

void MediaKeys::GetKeySystem(nsString& aOutKeySystem) const {
  aOutKeySystem.Assign(mKeySystem);
}

already_AddRefed<DetailedPromise> MediaKeys::SetServerCertificate(
    const ArrayBufferViewOrArrayBuffer& aCert, ErrorResult& aRv) {
  RefPtr<DetailedPromise> promise(
      MakePromise(aRv, "MediaKeys.setServerCertificate"_ns));
  if (aRv.Failed()) {
    return nullptr;
  }

  if (!mProxy) {
    NS_WARNING("Tried to use a MediaKeys without a CDM");
    promise->MaybeRejectWithInvalidStateError(
        "Null CDM in MediaKeys.setServerCertificate()");
    return promise.forget();
  }

  nsTArray<uint8_t> data;
  CopyArrayBufferViewOrArrayBufferData(aCert, data);
  if (data.IsEmpty()) {
    promise->MaybeRejectWithTypeError(
        "Empty certificate passed to MediaKeys.setServerCertificate()");
    return promise.forget();
  }

  mProxy->SetServerCertificate(StorePromise(promise), data);
  return promise.forget();
}

already_AddRefed<DetailedPromise> MediaKeys::MakePromise(
    ErrorResult& aRv, const nsACString& aName) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetParentObject());
  if (!global) {
    NS_WARNING("Passed non-global to MediaKeys ctor!");
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  return DetailedPromise::Create(global, aRv, aName);
}

PromiseId MediaKeys::StorePromise(DetailedPromise* aPromise) {
  static uint32_t sEMEPromiseCount = 1;
  MOZ_ASSERT(aPromise);
  uint32_t id = sEMEPromiseCount++;

  EME_LOG("MediaKeys[{}]::StorePromise() id={}", fmt::ptr(this), id);

  // Keep MediaKeys alive for the lifetime of its promises. Any still-pending
  // promises are rejected in Shutdown().
  EME_LOG("MediaKeys[{}]::StorePromise() calling AddRef()", fmt::ptr(this));
  AddRef();

#ifdef DEBUG
  // We should not have already stored this promise!
  for (const RefPtr<dom::DetailedPromise>& promise : mPromises.Values()) {
    MOZ_ASSERT(promise != aPromise);
  }
#endif

  mPromises.InsertOrUpdate(id, RefPtr{aPromise});
  return id;
}

void MediaKeys::ConnectPendingPromiseIdWithToken(PromiseId aId,
                                                 uint32_t aToken) {
  // Should only be called from MediaKeySession::GenerateRequest.
  mPromiseIdToken.InsertOrUpdate(aId, aToken);
  EME_LOG(
      "MediaKeys[{}]::ConnectPendingPromiseIdWithToken() id={} => token({})",
      fmt::ptr(this), aId, aToken);
}

already_AddRefed<DetailedPromise> MediaKeys::RetrievePromise(PromiseId aId) {
  EME_LOG("MediaKeys[{}]::RetrievePromise(aId={})", fmt::ptr(this), aId);
  if (!mPromises.Contains(aId)) {
    EME_LOG(
        "MediaKeys[{}]::RetrievePromise(aId={}) tried to retrieve non-existent "
        "promise!",
        fmt::ptr(this), aId);
    NS_WARNING(nsPrintfCString(
                   "Tried to retrieve a non-existent promise id=%" PRIu32, aId)
                   .get());
    return nullptr;
  }
  RefPtr<DetailedPromise> promise;
  mPromises.Remove(aId, getter_AddRefs(promise));
  EME_LOG("MediaKeys[{}]::RetrievePromise(aId={}) calling Release()",
          fmt::ptr(this), aId);
  Release();
  return promise.forget();
}

void MediaKeys::RejectPromise(PromiseId aId, ErrorResult&& aException,
                              const nsCString& aReason) {
  uint32_t errorCodeAsInt = aException.ErrorCodeAsInt();
  EME_LOG("MediaKeys[{}]::RejectPromise({}, 0x{:x})", fmt::ptr(this), aId,
          errorCodeAsInt);

  RefPtr<DetailedPromise> promise(RetrievePromise(aId));
  if (!promise) {
    EME_LOG(
        "MediaKeys[{}]::RejectPromise({}, 0x{:x}) couldn't retrieve "
        "promise! Bailing!",
        fmt::ptr(this), aId, errorCodeAsInt);
    return;
  }
  RefPtr<MediaKeys> keys(this);

  // This promise could be a createSession or loadSession promise,
  // so we might have a pending session waiting to be resolved into
  // the promise on success. We've been directed to reject to promise,
  // so we can throw away the corresponding session object.
  uint32_t token = 0;
  if (mPromiseIdToken.Get(aId, &token)) {
    MOZ_ASSERT(mPendingSessions.Contains(token));
    mPendingSessions.Remove(token);
    mPromiseIdToken.Remove(aId);
  }

  MOZ_ASSERT(aException.Failed());
  promise->MaybeReject(std::move(aException), aReason);

  if (mCreatePromiseId == aId) {
    // Clear the create-promise id once it has settled; 0 is not a valid
    // promise id.
    mCreatePromiseId = 0;
    // Note: This will probably destroy the MediaKeys object!
    EME_LOG("MediaKeys[{}]::RejectPromise({}, 0x{:x}) calling Release()",
            fmt::ptr(this), aId, errorCodeAsInt);
    Release();
  }
}

void MediaKeys::OnSessionIdReady(MediaKeySession* aSession) {
  if (!aSession) {
    NS_WARNING("Invalid MediaKeySession passed to OnSessionIdReady()");
    return;
  }
  if (mKeySessions.Contains(aSession->GetSessionId())) {
    NS_WARNING("MediaKeySession's made ready multiple times!");
    return;
  }
  if (mPendingSessions.Contains(aSession->Token())) {
    NS_WARNING(
        "MediaKeySession made ready when it wasn't waiting to be ready!");
    return;
  }
  if (aSession->GetSessionId().IsEmpty()) {
    NS_WARNING(
        "MediaKeySession with invalid sessionId passed to OnSessionIdReady()");
    return;
  }
  mKeySessions.InsertOrUpdate(aSession->GetSessionId(), RefPtr{aSession});
}

void MediaKeys::ResolvePromise(PromiseId aId) {
  EME_LOG("MediaKeys[{}]::ResolvePromise({})", fmt::ptr(this), aId);

  RefPtr<DetailedPromise> promise(RetrievePromise(aId));
  MOZ_ASSERT(!mPromises.Contains(aId));
  if (!promise) {
    return;
  }
  RefPtr<MediaKeys> keys(this);

  uint32_t token = 0;
  if (!mPromiseIdToken.Get(aId, &token)) {
    promise->MaybeResolveWithUndefined();
    return;
  } else if (!mPendingSessions.Contains(token)) {
    // Pending session for CreateSession() should be removed when sessionId
    // is ready.
    promise->MaybeResolveWithUndefined();
    mPromiseIdToken.Remove(aId);
    return;
  }
  mPromiseIdToken.Remove(aId);

  // We should only resolve LoadSession calls via this path,
  // not CreateSession() promises.
  RefPtr<MediaKeySession> session;
  mPendingSessions.Remove(token, getter_AddRefs(session));
  if (!session || session->GetSessionId().IsEmpty()) {
    NS_WARNING("Received activation for non-existent session!");
    promise->MaybeRejectWithInvalidAccessError(
        "CDM LoadSession() returned a different session ID than requested");
    return;
  }
  mKeySessions.InsertOrUpdate(session->GetSessionId(), RefPtr{session});
  promise->MaybeResolve(session);
}

class MediaKeysGMPCrashHelper : public GMPCrashHelper {
 public:
  explicit MediaKeysGMPCrashHelper(MediaKeys* aMediaKeys)
      : mMediaKeys(aMediaKeys) {
    MOZ_ASSERT(NS_IsMainThread());  // WeakPtr isn't thread safe.
  }
  already_AddRefed<nsPIDOMWindowInner> GetPluginCrashedEventTarget() override {
    MOZ_ASSERT(NS_IsMainThread());  // WeakPtr isn't thread safe.
    EME_LOG("MediaKeysGMPCrashHelper::GetPluginCrashedEventTarget()");
    return (mMediaKeys && mMediaKeys->GetParentObject())
               ? do_AddRef(mMediaKeys->GetParentObject())
               : nullptr;
  }

 private:
  WeakPtr<MediaKeys> mMediaKeys;
};

already_AddRefed<CDMProxy> MediaKeys::CreateCDMProxy() {
  const bool isHardwareDecryptionSupported =
      IsHardwareDecryptionSupported(mConfig) ||
      DoesKeySystemSupportHardwareDecryption(mKeySystem);
  EME_LOG("MediaKeys[{}]::CreateCDMProxy(), isHardwareDecryptionSupported={}",
          fmt::ptr(this), isHardwareDecryptionSupported);
  RefPtr<CDMProxy> proxy;
#ifdef MOZ_WIDGET_ANDROID
  if (IsWidevineKeySystem(mKeySystem)) {
    if (AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
      proxy = new MediaDrmCDMProxy(
          this, mKeySystem,
          mConfig.mDistinctiveIdentifier == MediaKeysRequirement::Required,
          mConfig.mPersistentState == MediaKeysRequirement::Required);
    } else {
      proxy = RemoteMediaManagerChild::CreateCDM(
          this, mKeySystem,
          mConfig.mDistinctiveIdentifier == MediaKeysRequirement::Required,
          mConfig.mPersistentState == MediaKeysRequirement::Required);
    }
  } else
#endif
#ifdef MOZ_WMF_CDM
      if (IsPlayReadyKeySystemAndSupported(mKeySystem) ||
          IsWidevineExperimentKeySystemAndSupported(mKeySystem) ||
          (IsWidevineKeySystem(mKeySystem) && isHardwareDecryptionSupported) ||
          IsWMFClearKeySystemAndSupported(mKeySystem)) {
    proxy = new WMFCDMProxy(this, mKeySystem, mConfig);
  } else
#endif
  {
    proxy = new ChromiumCDMProxy(
        this, mKeySystem, new MediaKeysGMPCrashHelper(this),
        mConfig.mDistinctiveIdentifier == MediaKeysRequirement::Required,
        mConfig.mPersistentState == MediaKeysRequirement::Required);
  }
  return proxy.forget();
}

already_AddRefed<DetailedPromise> MediaKeys::Init(ErrorResult& aRv) {
  EME_LOG("MediaKeys[{}]::Init()", fmt::ptr(this));
  RefPtr<DetailedPromise> promise(MakePromise(aRv, "MediaKeys::Init()"_ns));
  if (aRv.Failed()) {
    return nullptr;
  }

  // Determine principal (at creation time) of the MediaKeys object.
  nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(GetParentObject());
  if (!sop) {
    promise->MaybeRejectWithInvalidStateError(
        "Couldn't get script principal in MediaKeys::Init");
    return promise.forget();
  }
  mPrincipal = sop->GetPrincipal();

  // Begin figuring out the top level principal.
  nsCOMPtr<nsPIDOMWindowInner> window = GetParentObject();

  // If we're in a top level document, getting the top level principal is easy.
  // However, we're not in a top level doc this becomes more complicated. If
  // we're not top level we need to get the top level principal, this can be
  // done by reading the principal of the load info, which we can get of a
  // document's channel.
  //
  // There is an edge case we need to watch out for here where this code can be
  // run in an about:blank document before it has done its async load. In this
  // case the document will not yet have a load info. We address this below by
  // walking up a level in the window context chain. See
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1675360
  // for more info.
  Document* document = window->GetExtantDoc();
  if (!document) {
    NS_WARNING("Failed to get document when creating MediaKeys");
    promise->MaybeRejectWithInvalidStateError(
        "Couldn't get document in MediaKeys::Init");
    return promise.forget();
  }

  WindowGlobalChild* windowGlobalChild = window->GetWindowGlobalChild();
  if (!windowGlobalChild) {
    NS_WARNING("Failed to get window global child when creating MediaKeys");
    promise->MaybeRejectWithInvalidStateError(
        "Couldn't get window global child in MediaKeys::Init");
    return promise.forget();
  }

  if (windowGlobalChild->SameOriginWithTop()) {
    // We're in the same origin as the top window context, so our principal
    // is also the top principal.
    mTopLevelPrincipal = mPrincipal;
  } else {
    // We have a different origin than the top doc, try and find the top level
    // principal by looking it up via load info, which we read off a channel.
    nsIChannel* channel = document->GetChannel();

    WindowContext* windowContext = document->GetWindowContext();
    if (!windowContext) {
      NS_WARNING("Failed to get window context when creating MediaKeys");
      promise->MaybeRejectWithInvalidStateError(
          "Couldn't get window context in MediaKeys::Init");
      return promise.forget();
    }
    while (!channel) {
      // We don't have a channel, this can happen if we're in an about:blank
      // page that hasn't yet had its async load performed. Try and get
      // the channel from our parent doc. We should be able to do this because
      // an about:blank is considered the same origin as its parent. We do this
      // recursively to cover pages do silly things like nesting blank iframes
      // and not waiting for loads.

      // Move our window context up a level.
      windowContext = windowContext->GetParentWindowContext();
      if (!windowContext || !windowContext->GetExtantDoc()) {
        NS_WARNING(
            "Failed to get parent window context's document when creating "
            "MediaKeys");
        promise->MaybeRejectWithInvalidStateError(
            "Couldn't get parent window context's document in "
            "MediaKeys::Init (likely due to an nested about about:blank frame "
            "that hasn't loaded yet)");
        return promise.forget();
      }

      Document* parentDoc = windowContext->GetExtantDoc();
      channel = parentDoc->GetChannel();
    }

    MOZ_RELEASE_ASSERT(
        channel, "Should either have a channel or should have returned by now");

    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    MOZ_RELEASE_ASSERT(loadInfo, "Channels should always have LoadInfo");
    mTopLevelPrincipal = loadInfo->GetTopLevelPrincipal();
    if (!mTopLevelPrincipal) {
      NS_WARNING("Failed to get top level principal when creating MediaKeys");
      promise->MaybeRejectWithInvalidStateError(
          "Couldn't get top level principal in MediaKeys::Init");
      return promise.forget();
    }
  }

  // We should have figured out our top level principal.
  if (!mPrincipal || !mTopLevelPrincipal) {
    NS_WARNING("Failed to get principals when creating MediaKeys");
    promise->MaybeRejectWithInvalidStateError(
        "Couldn't get principal(s) in MediaKeys::Init");
    return promise.forget();
  }

  nsAutoCString origin;
  nsresult rv = mPrincipal->GetOrigin(origin);
  if (NS_FAILED(rv)) {
    promise->MaybeRejectWithInvalidStateError(
        "Couldn't get principal origin string in MediaKeys::Init");
    return promise.forget();
  }
  nsAutoCString topLevelOrigin;
  rv = mTopLevelPrincipal->GetOrigin(topLevelOrigin);
  if (NS_FAILED(rv)) {
    promise->MaybeRejectWithInvalidStateError(
        "Couldn't get top-level principal origin string in MediaKeys::Init");
    return promise.forget();
  }

  EME_LOG("MediaKeys[{}]::Create() ({}, {})", fmt::ptr(this), origin.get(),
          topLevelOrigin.get());

  mProxy = CreateCDMProxy();
  if (!mProxy) {
    promise->MaybeRejectWithNotSupportedError(
        "Couldn't create a CDM proxy in MediaKeys::Init");
    return promise.forget();
  }

  // The CDMProxy's initialization is asynchronous. The MediaKeys is
  // refcounted, and its instance is returned to JS by promise once
  // it's been initialized. No external refs exist to the MediaKeys while
  // we're waiting for the promise to be resolved, so we must hold a
  // reference to the new MediaKeys object until it's been created,
  // or its creation has failed. Store the id of the promise returned
  // here, and hold a self-reference until that promise is resolved or
  // rejected.
  MOZ_ASSERT(!mCreatePromiseId, "Should only be created once!");
  mCreatePromiseId = StorePromise(promise);
  EME_LOG("MediaKeys[{}]::Init() calling AddRef()", fmt::ptr(this));
  AddRef();
  mProxy->Init(mCreatePromiseId, NS_ConvertUTF8toUTF16(origin),
               NS_ConvertUTF8toUTF16(topLevelOrigin),
               KeySystemToProxyName(mKeySystem));

  ConnectInnerWindow();

  return promise.forget();
}

void MediaKeys::OnCDMCreated(PromiseId aId, const uint32_t aPluginId) {
  EME_LOG("MediaKeys[{}]::OnCDMCreated(aId={}, aPluginId={})", fmt::ptr(this),
          aId, aPluginId);
  RefPtr<DetailedPromise> promise(RetrievePromise(aId));
  if (!promise) {
    return;
  }
  RefPtr<MediaKeys> keys(this);

  promise->MaybeResolve(keys);
  if (mCreatePromiseId == aId) {
    // Clear the create-promise id once it has settled; 0 is not a valid
    // promise id.
    mCreatePromiseId = 0;
    EME_LOG(
        "MediaKeys[{}]::OnCDMCreated(aId={}, aPluginId={}) calling Release()",
        fmt::ptr(this), aId, aPluginId);
    Release();
  }

  MediaKeySystemAccess::NotifyObservers(mParent, mKeySystem,
                                        MediaKeySystemStatus::Cdm_created);
}

static bool IsSessionTypeSupported(const MediaKeySessionType aSessionType,
                                   const MediaKeySystemConfiguration& aConfig) {
  if (aSessionType == MediaKeySessionType::Temporary) {
    // Temporary is always supported.
    return true;
  }
  if (!aConfig.mSessionTypes.WasPassed()) {
    // No other session types supported.
    return false;
  }
  return aConfig.mSessionTypes.Value().Contains(ToString(aSessionType));
}

already_AddRefed<MediaKeySession> MediaKeys::CreateSession(
    MediaKeySessionType aSessionType, ErrorResult& aRv) {
  EME_LOG("MediaKeys[{}]::CreateSession(aSessionType={})", fmt::ptr(this),
          static_cast<uint8_t>(aSessionType));
  if (!IsSessionTypeSupported(aSessionType, mConfig)) {
    EME_LOG("MediaKeys[{}]::CreateSession() failed, unsupported session type",
            fmt::ptr(this));
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }

  if (!mProxy) {
    NS_WARNING("Tried to use a MediaKeys which lost its CDM");
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  EME_LOG("MediaKeys[{}] Creating session", fmt::ptr(this));

  const bool isHardwareDecryption =
      IsHardwareDecryptionSupported(mConfig) ||
      DoesKeySystemSupportHardwareDecryption(mKeySystem);
  RefPtr<MediaKeySession> session =
      new MediaKeySession(GetParentObject(), this, mKeySystem, aSessionType,
                          isHardwareDecryption, aRv);

  if (aRv.Failed()) {
    return nullptr;
  }
  DDLINKCHILD("session", session.get());

  // Add session to the set of sessions awaiting their sessionId being ready.
  EME_LOG(
      "MediaKeys[{}]::CreateSession(aSessionType={}) putting session with "
      "token={} into mPendingSessions",
      fmt::ptr(this), static_cast<uint8_t>(aSessionType), session->Token());
  mPendingSessions.InsertOrUpdate(session->Token(), RefPtr{session});

  return session.forget();
}

void MediaKeys::OnSessionLoaded(PromiseId aId, bool aSuccess) {
  EME_LOG("MediaKeys[{}]::OnSessionLoaded() resolve promise id={}",
          fmt::ptr(this), aId);

  ResolvePromiseWithResult(aId, aSuccess);
}

void MediaKeys::OnSessionClosed(MediaKeySession* aSession) {
  nsAutoString id;
  aSession->GetSessionId(id);
  mKeySessions.Remove(id);
}

already_AddRefed<MediaKeySession> MediaKeys::GetSession(
    const nsAString& aSessionId) {
  RefPtr<MediaKeySession> session;
  mKeySessions.Get(aSessionId, getter_AddRefs(session));
  return session.forget();
}

already_AddRefed<MediaKeySession> MediaKeys::GetPendingSession(
    uint32_t aToken) {
  EME_LOG("MediaKeys[{}]::GetPendingSession(aToken={})", fmt::ptr(this),
          aToken);
  RefPtr<MediaKeySession> session;
  mPendingSessions.Get(aToken, getter_AddRefs(session));
  mPendingSessions.Remove(aToken);
  return session.forget();
}

bool MediaKeys::IsBoundToMediaElement() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mElement != nullptr;
}

nsresult MediaKeys::Bind(HTMLMediaElement* aElement) {
  MOZ_ASSERT(NS_IsMainThread());
  if (IsBoundToMediaElement()) {
    return NS_ERROR_FAILURE;
  }

  mElement = aElement;

  return NS_OK;
}

void MediaKeys::Unbind() {
  MOZ_ASSERT(NS_IsMainThread());
  mElement = nullptr;
}

void MediaKeys::CheckIsElementCapturePossible() {
  MOZ_ASSERT(NS_IsMainThread());
  EME_LOG("MediaKeys[{}]::IsElementCapturePossible()", fmt::ptr(this));
  // Note, HTMLMediaElement prevents capture of its content via Capture APIs
  // on the element if it has a media keys attached (see bug 1071482). So we
  // don't need to check those cases here (they are covered by tests).

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  if (!observerService) {
    // This can happen if we're in shutdown which means we may be going away
    // soon anyway, but respond saying capture is possible since we can't
    // forward the check further.
    if (mProxy) {
      mProxy->NotifyOutputProtectionStatus(
          CDMProxy::OutputProtectionCheckStatus::CheckFailed,
          CDMProxy::OutputProtectionCaptureStatus::Unused);
    }
    return;
  }
  if (!mObserverAdded) {
    nsresult rv =
        observerService->AddObserver(this, kMediaKeysResponseTopic, false);
    if (NS_FAILED(rv)) {
      if (mProxy) {
        mProxy->NotifyOutputProtectionStatus(
            CDMProxy::OutputProtectionCheckStatus::CheckFailed,
            CDMProxy::OutputProtectionCaptureStatus::Unused);
      }
      return;
    }
    mObserverAdded = true;
  }

  if (mCaptureCheckRequestJson.IsEmpty()) {
    // Lazily populate the JSON the first time we need it.
    JSONStringWriteFunc<nsAutoCString> json;
    JSONWriter jw{json};
    jw.Start();
    jw.StringProperty("status", "is-capture-possible");
    jw.StringProperty("keySystem", NS_ConvertUTF16toUTF8(mKeySystem));
    jw.End();
    mCaptureCheckRequestJson = NS_ConvertUTF8toUTF16(json.StringCRef());
  }

  MOZ_DIAGNOSTIC_ASSERT(!mCaptureCheckRequestJson.IsEmpty());
  observerService->NotifyObservers(mParent.get(), kMediaKeysRequestTopic,
                                   mCaptureCheckRequestJson.get());
}

void MediaKeys::GetSessionsInfo(nsString& sessionsInfo) {
  for (const auto& keySession : mKeySessions.Values()) {
    nsString sessionID;
    keySession->GetSessionId(sessionID);
    sessionsInfo.AppendLiteral("(sid=");
    sessionsInfo.Append(sessionID);
    MediaKeyStatusMap* keyStatusMap = keySession->KeyStatuses();
    for (uint32_t i = 0; i < keyStatusMap->GetIterableLength(); i++) {
      nsString keyID = keyStatusMap->GetKeyIDAsHexString(i);
      sessionsInfo.AppendLiteral("(kid=");
      sessionsInfo.Append(keyID);
      sessionsInfo.AppendLiteral(" status=");
      sessionsInfo.AppendASCII(GetEnumString(keyStatusMap->GetValueAtIndex(i)));
      sessionsInfo.AppendLiteral(")");
    }
    sessionsInfo.AppendLiteral(")");
  }
}

// https://w3c.github.io/encrypted-media/#dom-mediakeys-getstatusforpolicy
already_AddRefed<Promise> MediaKeys::GetStatusForPolicy(
    const MediaKeysPolicy& aPolicy, ErrorResult& aRv) {
  RefPtr<DetailedPromise> promise(
      MakePromise(aRv, "MediaKeys::GetStatusForPolicy()"_ns));
  if (aRv.Failed()) {
    return nullptr;
  }

  // 1. If policy has no present members, return a promise rejected with a newly
  // created TypeError.
  if (!aPolicy.mMinHdcpVersion.WasPassed()) {
    promise->MaybeRejectWithTypeError("No minHdcpVersion in MediaKeysPolicy");
    return promise.forget();
  }

  if (!mProxy) {
    NS_WARNING("Tried to use a MediaKeys without a CDM");
    promise->MaybeRejectWithInvalidStateError(
        "Null CDM in MediaKeys.GetStatusForPolicy()");
    return promise.forget();
  }

  EME_LOG("GetStatusForPolicy minHdcpVersion = {}.",
          GetEnumString(aPolicy.mMinHdcpVersion.Value()).get());
  mProxy->GetStatusForPolicy(StorePromise(promise),
                             aPolicy.mMinHdcpVersion.Value());
  return promise.forget();
}

void MediaKeys::ResolvePromiseWithKeyStatus(PromiseId aId,
                                            MediaKeyStatus aMediaKeyStatus) {
  RefPtr<DetailedPromise> promise(RetrievePromise(aId));
  if (!promise) {
    return;
  }
  RefPtr<MediaKeys> keys(this);
  EME_LOG(
      "MediaKeys[{}]::ResolvePromiseWithKeyStatus() resolve promise id={}, "
      "keystatus={}",
      fmt::ptr(this), aId, static_cast<uint8_t>(aMediaKeyStatus));
  promise->MaybeResolve(aMediaKeyStatus);
}

nsCString MediaKeys::GetMediaKeySystemConfigurationString() const {
  return MediaKeySystemAccess::ToCString(mConfig);
}

}  // namespace mozilla::dom
