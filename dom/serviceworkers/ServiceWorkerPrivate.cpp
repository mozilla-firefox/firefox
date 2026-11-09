/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerPrivate.h"

#include <utility>

#include "MainThreadUtils.h"
#include "ServiceWorkerManager.h"
#include "ServiceWorkerRegistrationInfo.h"
#include "ServiceWorkerUtils.h"
#include "js/ErrorReport.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/Assertions.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/CycleCollectedJSContext.h"  // for MicroTaskRunnable
#include "mozilla/ErrorResult.h"
#include "mozilla/JSObjectHolder.h"
#include "mozilla/Maybe.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/RemoteLazyInputStreamStorage.h"
#include "mozilla/Result.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/Client.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientManager.h"
#include "mozilla/dom/DOMTypes.h"
#include "mozilla/dom/FetchEventOpChild.h"
#include "mozilla/dom/FetchUtil.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/InternalRequest.h"
#include "mozilla/dom/NotificationEvent.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/PushEventBinding.h"
#include "mozilla/dom/PushManager.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/dom/RemoteWorkerControllerChild.h"
#include "mozilla/dom/RemoteWorkerManager.h"  // RemoteWorkerManager::GetRemoteType
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ServiceWorkerBinding.h"
#include "mozilla/dom/ServiceWorkerLifetimeExtension.h"
#include "mozilla/dom/WorkerDebugger.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/extensions/WebExtensionPolicy.h"  // WebExtensionPolicy
#include "mozilla/glean/DomServiceworkersMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/CookieService.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsICacheInfoChannel.h"
#include "nsIChannel.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsINamed.h"
#include "nsINetworkInterceptController.h"
#include "nsIObserverService.h"
#include "nsIRedirectHistoryEntry.h"
#include "nsIReferrerInfo.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPriority.h"
#include "nsIURI.h"
#include "nsIUploadChannel.h"
#include "nsIUploadChannel2.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsRFPService.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"

extern mozilla::LazyLogModule sWorkerTelemetryLog;

#ifdef LOG
#  undef LOG
#endif
#define LOG(_args) MOZ_LOG(sWorkerTelemetryLog, LogLevel::Debug, _args);

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

namespace mozilla::dom {

uint32_t ServiceWorkerPrivate::sRunningServiceWorkers = 0;
uint32_t ServiceWorkerPrivate::sRunningServiceWorkersFetch = 0;
uint32_t ServiceWorkerPrivate::sRunningServiceWorkersMax = 0;
uint32_t ServiceWorkerPrivate::sRunningServiceWorkersFetchMax = 0;

/**
 * KeepAliveToken
 */
KeepAliveToken::KeepAliveToken(ServiceWorkerPrivate* aPrivate)
    : mPrivate(aPrivate) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrivate);
  mPrivate->AddToken();
}

KeepAliveToken::~KeepAliveToken() {
  MOZ_ASSERT(NS_IsMainThread());
  mPrivate->ReleaseToken();
}

NS_IMPL_ISUPPORTS0(KeepAliveToken)

/**
 * RAIIActorPtrHolder
 */
ServiceWorkerPrivate::RAIIActorPtrHolder::RAIIActorPtrHolder(
    already_AddRefed<RemoteWorkerControllerChild> aActor)
    : mActor(aActor) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(mActor->Manager());
}

ServiceWorkerPrivate::RAIIActorPtrHolder::~RAIIActorPtrHolder() {
  AssertIsOnMainThread();

  mDestructorPromiseHolder.ResolveIfExists(true, __func__);

  mActor->MaybeSendDelete();
}

RemoteWorkerControllerChild*
ServiceWorkerPrivate::RAIIActorPtrHolder::operator->() const {
  AssertIsOnMainThread();

  return get();
}

RemoteWorkerControllerChild* ServiceWorkerPrivate::RAIIActorPtrHolder::get()
    const {
  AssertIsOnMainThread();

  return mActor.get();
}

RefPtr<GenericPromise>
ServiceWorkerPrivate::RAIIActorPtrHolder::OnDestructor() {
  AssertIsOnMainThread();

  return mDestructorPromiseHolder.Ensure(__func__);
}

/**
 *  PendingFunctionEvent
 */
ServiceWorkerPrivate::PendingFunctionalEvent::PendingFunctionalEvent(
    ServiceWorkerPrivate* aOwner,
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration)
    : mOwner(aOwner), mRegistration(std::move(aRegistration)) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mOwner);
  MOZ_ASSERT(mOwner->mInfo);
  MOZ_ASSERT(mOwner->mInfo->State() == ServiceWorkerState::Activating);
  MOZ_ASSERT(mRegistration);
}

ServiceWorkerPrivate::PendingFunctionalEvent::~PendingFunctionalEvent() {
  AssertIsOnMainThread();
}

ServiceWorkerPrivate::PendingCookieChangeEvent::PendingCookieChangeEvent(
    ServiceWorkerPrivate* aOwner,
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    ServiceWorkerCookieChangeEventOpArgs&& aArgs)
    : PendingFunctionalEvent(aOwner, std::move(aRegistration)),
      mArgs(std::move(aArgs)) {
  AssertIsOnMainThread();
}

nsresult ServiceWorkerPrivate::PendingCookieChangeEvent::Send() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mOwner);
  MOZ_ASSERT(mOwner->mInfo);

  return mOwner->SendCookieChangeEventInternal(std::move(mRegistration),
                                               std::move(mArgs));
}

ServiceWorkerPrivate::PendingPushEvent::PendingPushEvent(
    ServiceWorkerPrivate* aOwner,
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    ServiceWorkerPushEventOpArgs&& aArgs)
    : PendingFunctionalEvent(aOwner, std::move(aRegistration)),
      mArgs(std::move(aArgs)) {
  AssertIsOnMainThread();
}

ServiceWorkerPrivate::PendingPushEvent::~PendingPushEvent() {
  mPromiseHolder.RejectIfExists(NS_ERROR_DOM_ABORT_ERR, __func__);
}

nsresult ServiceWorkerPrivate::PendingPushEvent::Send() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mOwner);
  MOZ_ASSERT(mOwner->mInfo);

  mOwner->SendPushEventInternal(std::move(mRegistration), std::move(mArgs))
      ->ChainTo(mPromiseHolder.Steal(), __func__);
  return NS_OK;
}

RefPtr<PushHandledPromise> ServiceWorkerPrivate::PendingPushEvent::Promise() {
  return mPromiseHolder.Ensure(__func__);
}

ServiceWorkerPrivate::PendingFetchEvent::PendingFetchEvent(
    ServiceWorkerPrivate* aOwner,
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    ParentToParentServiceWorkerFetchEventOpArgs&& aArgs,
    nsCOMPtr<nsIInterceptedChannel>&& aChannel,
    RefPtr<FetchServicePromises>&& aPreloadResponseReadyPromises)
    : PendingFunctionalEvent(aOwner, std::move(aRegistration)),
      mArgs(std::move(aArgs)),
      mChannel(std::move(aChannel)),
      mPreloadResponseReadyPromises(std::move(aPreloadResponseReadyPromises)) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mChannel);
}

nsresult ServiceWorkerPrivate::PendingFetchEvent::Send() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mOwner);
  MOZ_ASSERT(mOwner->mInfo);

  return mOwner->SendFetchEventInternal(
      std::move(mRegistration), std::move(mArgs), std::move(mChannel),
      std::move(mPreloadResponseReadyPromises));
}

ServiceWorkerPrivate::PendingFetchEvent::~PendingFetchEvent() {
  AssertIsOnMainThread();

  if (NS_WARN_IF(mChannel)) {
    mChannel->CancelInterception(NS_ERROR_INTERCEPTION_FAILED);
  }
}

namespace {

class HeaderFiller final : public nsIHttpHeaderVisitor {
 public:
  NS_DECL_ISUPPORTS

  explicit HeaderFiller(HeadersGuardEnum aGuard)
      : mInternalHeaders(new InternalHeaders(aGuard)) {
    MOZ_ASSERT(mInternalHeaders);
  }

  NS_IMETHOD
  VisitHeader(const nsACString& aHeader, const nsACString& aValue) override {
    ErrorResult result;
    mInternalHeaders->Append(aHeader, aValue, result);

    if (NS_WARN_IF(result.Failed())) {
      return result.StealNSResult();
    }

    return NS_OK;
  }

  RefPtr<InternalHeaders> Extract() {
    return RefPtr<InternalHeaders>(std::move(mInternalHeaders));
  }

 private:
  ~HeaderFiller() = default;

  RefPtr<InternalHeaders> mInternalHeaders;
};

NS_IMPL_ISUPPORTS(HeaderFiller, nsIHttpHeaderVisitor)

Result<IPCInternalRequest, nsresult> GetIPCInternalRequest(
    nsIInterceptedChannel* aChannel) {
  AssertIsOnMainThread();

  nsCOMPtr<nsIURI> uri;
  MOZ_TRY(aChannel->GetSecureUpgradedChannelURI(getter_AddRefs(uri)));

  nsCOMPtr<nsIURI> uriNoFragment;
  MOZ_TRY(NS_GetURIWithoutRef(uri, getter_AddRefs(uriNoFragment)));

  nsCOMPtr<nsIChannel> underlyingChannel;
  MOZ_TRY(aChannel->GetChannel(getter_AddRefs(underlyingChannel)));

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(underlyingChannel);
  MOZ_ASSERT(httpChannel, "How come we don't have an HTTP channel?");

  nsCOMPtr<nsIHttpChannelInternal> internalChannel =
      do_QueryInterface(httpChannel);
  NS_ENSURE_TRUE(internalChannel, Err(NS_ERROR_NOT_AVAILABLE));

  nsCOMPtr<nsICacheInfoChannel> cacheInfoChannel =
      do_QueryInterface(underlyingChannel);

  nsAutoCString fragment;
  MOZ_TRY(uri->GetRef(fragment));

  nsAutoCString method;
  MOZ_TRY(httpChannel->GetRequestMethod(method));

  // This is safe due to static_asserts in ServiceWorkerManager.cpp
  uint32_t cacheModeInt;
  MOZ_ALWAYS_SUCCEEDS(internalChannel->GetFetchCacheMode(&cacheModeInt));
  RequestCache cacheMode = static_cast<RequestCache>(cacheModeInt);

  RequestMode requestMode =
      InternalRequest::MapChannelToRequestMode(underlyingChannel);

  // This is safe due to static_asserts in ServiceWorkerManager.cpp
  uint32_t redirectMode;
  MOZ_ALWAYS_SUCCEEDS(internalChannel->GetRedirectMode(&redirectMode));
  RequestRedirect requestRedirect = static_cast<RequestRedirect>(redirectMode);

  // request's priority is not copied by the new Request() constructor used by
  // a fetch() call while request's internal priority is. So let's use the
  // default, otherwise a fetch(event.request) from a worker on an intercepted
  // fetch event would adjust priority twice.
  // https://fetch.spec.whatwg.org/#dom-global-fetch
  // https://fetch.spec.whatwg.org/#dom-request
  RequestPriority requestPriority = RequestPriority::Auto;

  RequestCredentials requestCredentials =
      InternalRequest::MapChannelToRequestCredentials(underlyingChannel);

  nsAutoCString referrer;
  ReferrerPolicy referrerPolicy = ReferrerPolicy::_empty;
  ReferrerPolicy environmentReferrerPolicy = ReferrerPolicy::_empty;

  nsCOMPtr<nsIReferrerInfo> referrerInfo = httpChannel->GetReferrerInfo();
  if (referrerInfo) {
    referrerPolicy = referrerInfo->ReferrerPolicy();
    (void)referrerInfo->GetComputedReferrerSpec(referrer);
  }

  uint32_t loadFlags;
  MOZ_TRY(underlyingChannel->GetLoadFlags(&loadFlags));

  nsCOMPtr<nsILoadInfo> loadInfo = underlyingChannel->LoadInfo();
  nsContentPolicyType contentPolicyType = loadInfo->InternalContentPolicyType();

  int32_t internalPriority = nsISupportsPriority::PRIORITY_NORMAL;
  if (nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(underlyingChannel)) {
    p->GetPriority(&internalPriority);
  }

  nsAutoString integrity;
  MOZ_TRY(loadInfo->GetIntegrityMetadata(integrity));

  RefPtr<HeaderFiller> headerFiller =
      MakeRefPtr<HeaderFiller>(HeadersGuardEnum::Request);
  MOZ_TRY(httpChannel->VisitNonDefaultRequestHeaders(headerFiller));

  RefPtr<InternalHeaders> internalHeaders = headerFiller->Extract();

  ErrorResult result;
  internalHeaders->SetGuard(HeadersGuardEnum::Immutable, result);
  if (NS_WARN_IF(result.Failed())) {
    return Err(result.StealNSResult());
  }

  nsTArray<HeadersEntry> ipcHeaders;
  HeadersGuardEnum ipcHeadersGuard;
  internalHeaders->ToIPC(ipcHeaders, ipcHeadersGuard);

  nsAutoCString alternativeDataType;
  if (cacheInfoChannel &&
      !cacheInfoChannel->PreferredAlternativeDataTypes().IsEmpty()) {
    // TODO: the internal request probably needs all the preferred types.
    alternativeDataType.Assign(
        cacheInfoChannel->PreferredAlternativeDataTypes()[0].type());
  }

  Maybe<PrincipalInfo> principalInfo;
  Maybe<PrincipalInfo> interceptionPrincipalInfo;
  if (loadInfo->TriggeringPrincipal()) {
    principalInfo.emplace();
    interceptionPrincipalInfo.emplace();
    MOZ_ALWAYS_SUCCEEDS(PrincipalToPrincipalInfo(
        loadInfo->TriggeringPrincipal(), principalInfo.ptr()));
    MOZ_ALWAYS_SUCCEEDS(PrincipalToPrincipalInfo(
        loadInfo->TriggeringPrincipal(), interceptionPrincipalInfo.ptr()));
  }

  nsTArray<RedirectHistoryEntryInfo> redirectChain;
  for (const nsCOMPtr<nsIRedirectHistoryEntry>& redirectEntry :
       loadInfo->RedirectChain()) {
    RedirectHistoryEntryInfo* entry = redirectChain.AppendElement();
    MOZ_ALWAYS_SUCCEEDS(RHEntryToRHEntryInfo(redirectEntry, entry));
  }

  bool isThirdPartyChannel;
  // ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
      do_GetService(THIRDPARTYUTIL_CONTRACTID);
  if (thirdPartyUtil) {
    nsCOMPtr<nsIURI> uri;
    MOZ_TRY(underlyingChannel->GetURI(getter_AddRefs(uri)));
    MOZ_TRY(thirdPartyUtil->IsThirdPartyChannel(underlyingChannel, uri,
                                                &isThirdPartyChannel));
  }

  nsILoadInfo::CrossOriginEmbedderPolicy embedderPolicy =
      loadInfo->GetLoadingEmbedderPolicy();

  // Note: all the arguments are copied rather than moved, which would be more
  // efficient, because there's no move-friendly constructor generated.
  return IPCInternalRequest(
      method, {WrapNotNull(uriNoFragment.get())}, ipcHeadersGuard, ipcHeaders,
      Nothing(), -1, alternativeDataType, contentPolicyType, internalPriority,
      referrer, referrerPolicy, environmentReferrerPolicy, requestMode,
      requestCredentials, cacheMode, requestRedirect, requestPriority,
      integrity, /* keepalive */ false, /* hasStreamBody */ false, fragment,
      principalInfo, interceptionPrincipalInfo, contentPolicyType,
      redirectChain, isThirdPartyChannel, embedderPolicy);
}

nsresult MaybeStoreStreamForBackgroundThread(nsIInterceptedChannel* aChannel,
                                             IPCInternalRequest& aIPCRequest) {
  nsCOMPtr<nsIChannel> channel;
  MOZ_ALWAYS_SUCCEEDS(aChannel->GetChannel(getter_AddRefs(channel)));

  Maybe<BodyStreamVariant> body;
  nsCOMPtr<nsIUploadChannel2> uploadChannel = do_QueryInterface(channel);

  if (uploadChannel) {
    nsCOMPtr<nsIInputStream> uploadStream;
    RefPtr<net::HttpBaseChannel> httpBase = do_QueryObject(channel);
    if (httpBase && httpBase->UploadStreamIsStreaming()) {
      // Transfer streaming uploads to the worker; fallback returns its unread
      // body instead of replaying a clone. The channel gives up its reference
      // so that the stream, which serialization may drain and close
      // It is only restored if the worker returns it.
      uploadStream = httpBase->TakeUploadStream();
      // The length is only known once the stream ends.
      aIPCRequest.bodySize() = -1;
      aIPCRequest.hasStreamBody() = !!uploadStream;
    } else {
      MOZ_TRY(uploadChannel->CloneUploadStream(&aIPCRequest.bodySize(),
                                               getter_AddRefs(uploadStream)));
    }

    if (uploadStream) {
      Maybe<BodyStreamVariant>& body = aIPCRequest.body();
      body.emplace(ParentToParentStream());

      MOZ_TRY(
          nsID::GenerateUUIDInPlace(body->get_ParentToParentStream().uuid()));

      auto storageOrErr = RemoteLazyInputStreamStorage::Get();
      if (NS_WARN_IF(storageOrErr.isErr())) {
        return storageOrErr.unwrapErr();
      }

      auto storage = storageOrErr.unwrap();
      storage->AddStream(uploadStream, body->get_ParentToParentStream().uuid());
    }
  }

  return NS_OK;
}

}  // anonymous namespace

/**
 * ServiceWorkerPrivate
 */
ServiceWorkerPrivate::ServiceWorkerPrivate(ServiceWorkerInfo* aInfo)
    : mInfo(aInfo),
      mPendingSpawnLifetime(
          ServiceWorkerLifetimeExtension(NoLifetimeExtension{})),
      mDebuggerCount(0),
      mTokenCount(0),
      mLaunchCount(0) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aInfo);
  MOZ_ASSERT(!mControllerChild);

  mIdleWorkerTimer = NS_NewTimer();
  MOZ_ASSERT(mIdleWorkerTimer);

  (void)Initialize();
}

ServiceWorkerPrivate::~ServiceWorkerPrivate() {
  MOZ_ASSERT(!mTokenCount);
  MOZ_ASSERT(!mInfo);
  MOZ_ASSERT(!mControllerChild);
  MOZ_ASSERT(mIdlePromiseHolder.IsEmpty());

  mIdleWorkerTimer->Cancel();
}

nsresult ServiceWorkerPrivate::Initialize() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInfo);

  // Initialize() is only ever called from our constructor and there is no retry
  // mechanism, so on failure this ServiceWorkerPrivate can never become usable;
  // in particular mRemoteWorkerData would stay default-constructed, and its
  // OptionalServiceWorkerData union would fatally assert the first time
  // RefreshRemoteWorkerData() touched it. Neutralize ourselves by clearing
  // mInfo, which is the same state NoteDeadServiceWorkerInfo() establishes and
  // which SpawnWorkerIfNeeded() already refuses to act on, so that every
  // operation fails cleanly instead. For fetch that means the interception is
  // reset and the request goes to the network.
  //
  // Note that we run from within ServiceWorkerInfo's constructor, so mInfo
  // points at a not-yet-fully-constructed object; this only clears the pointer.
  //
  // Every failure return goes through fail() so that the scope exit can report
  // which step failed; give each one a distinct message.
  const char* failureReason = nullptr;
  auto fail = [&failureReason](const char* aReason, nsresult aRv) {
    failureReason = aReason;
    return aRv;
  };

  auto neutralizeOnFailure = MakeScopeExit([&] {
    NS_WARNING(nsPrintfCString("ServiceWorkerPrivate::Initialize failed: %s",
                               failureReason ? failureReason : "unknown")
                   .get());
    mInfo = nullptr;
  });

  nsCOMPtr<nsIPrincipal> principal = mInfo->Principal();

  nsCOMPtr<nsIURI> uri;
  auto* basePrin = BasePrincipal::Cast(principal);
  nsresult rv = basePrin->GetURI(getter_AddRefs(uri));

  if (NS_FAILED(rv)) {
    return fail("cannot get the URI of the principal", rv);
  }

  if (!uri) {
    return fail("the principal has no URI", NS_ERROR_FAILURE);
  }

  nsString id;
  rv = mInfo->GetId(id);

  if (NS_FAILED(rv)) {
    return fail("cannot get the id of the ServiceWorkerInfo", rv);
  }

  PrincipalInfo principalInfo;
  rv = PrincipalToPrincipalInfo(principal, &principalInfo);
  if (NS_FAILED(rv)) {
    return fail("cannot serialize the principal", rv);
  }

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

  if (!swm) {
    return fail("no ServiceWorkerManager instance", NS_ERROR_DOM_ABORT_ERR);
  }

  RefPtr<ServiceWorkerRegistrationInfo> regInfo =
      swm->GetRegistration(principal, mInfo->Scope());

  if (!regInfo) {
    return fail("no registration for the scope",
                NS_ERROR_DOM_INVALID_STATE_ERR);
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      net::CookieJarSettings::Create(principal);
  MOZ_ASSERT(cookieJarSettings);

  // We can populate the partitionKey and the fingerprinting protection
  // overrides using the originAttribute of the principal. If it has
  // partitionKey set, It's a foreign partitioned principal and it implies that
  // it's a third-party service worker. So, the cookieJarSettings can directly
  // use the partitionKey from it. For first-party case, we can populate the
  // partitionKey from the principal URI.
  Maybe<RFPTargetSet> overriddenFingerprintingSettingsArg;
  Maybe<RFPTargetSet> overriddenFingerprintingSettings;
  nsCOMPtr<nsIURI> firstPartyURI;
  bool foreignByAncestorContext = false;
  bool isOn3PCBExceptionList = false;
  // Firefox doesn't support service workers in PBM,
  // but we add this just so that when we do,
  // we can handle it correctly.
  bool isPBM = principal->GetIsInPrivateBrowsing();
  if (!principal->OriginAttributesRef().mPartitionKey.IsEmpty()) {
    net::CookieJarSettings::Cast(cookieJarSettings)
        ->SetPartitionKey(principal->OriginAttributesRef().mPartitionKey);

    // The service worker is for a third-party context, we get first-party
    // domain from the partitionKey and the third-party domain from the
    // principal of the service worker. Then, we can get the fingerprinting
    // protection overrides using them.
    nsAutoString scheme;
    nsAutoString pkBaseDomain;
    int32_t unused;
    bool _foreignByAncestorContext;

    if (OriginAttributes::ParsePartitionKey(
            principal->OriginAttributesRef().mPartitionKey, scheme,
            pkBaseDomain, unused, _foreignByAncestorContext)) {
      foreignByAncestorContext = _foreignByAncestorContext;
      rv = NS_NewURI(getter_AddRefs(firstPartyURI),
                     scheme + u"://"_ns + pkBaseDomain);
      if (NS_SUCCEEDED(rv)) {
        overriddenFingerprintingSettings =
            nsRFPService::GetOverriddenFingerprintingSettingsForURI(
                firstPartyURI, uri, isPBM);
        if (overriddenFingerprintingSettings.isSome()) {
          overriddenFingerprintingSettingsArg.emplace(
              overriddenFingerprintingSettings.ref());
        }

        RefPtr<net::CookieService> csSingleton =
            net::CookieService::GetSingleton();
        isOn3PCBExceptionList =
            csSingleton->ThirdPartyCookieBlockingExceptionsRef()
                .CheckExceptionForURIs(firstPartyURI, uri);
      }
    }
  } else if (!principal->OriginAttributesRef().mFirstPartyDomain.IsEmpty()) {
    // Using the first party domain to know the context of the service worker.
    // We will run into here if FirstPartyIsolation is enabled. In this case,
    // the PartitionKey won't get populated.
    // Because the service worker is only available in secure contexts, so we
    // don't need to consider http and only use https as scheme to create
    // the first-party URI
    rv = NS_NewURI(
        getter_AddRefs(firstPartyURI),
        u"https://"_ns + principal->OriginAttributesRef().mFirstPartyDomain);
    if (NS_SUCCEEDED(rv)) {
      // If the first party domain is not a third-party domain, the service
      // worker is running in first-party context.
      bool isThirdParty;
      rv = principal->IsThirdPartyURI(firstPartyURI, &isThirdParty);
      if (NS_FAILED(rv)) {
        return fail("cannot tell if the first-party URI is third-party", rv);
      }

      overriddenFingerprintingSettings =
          isThirdParty
              ? nsRFPService::GetOverriddenFingerprintingSettingsForURI(
                    firstPartyURI, uri, isPBM)
              : nsRFPService::GetOverriddenFingerprintingSettingsForURI(
                    uri, nullptr, isPBM);

      RefPtr<net::CookieService> csSingleton =
          net::CookieService::GetSingleton();
      isOn3PCBExceptionList =
          isThirdParty ? csSingleton->ThirdPartyCookieBlockingExceptionsRef()
                             .CheckExceptionForURIs(firstPartyURI, uri)
                       : false;

      if (overriddenFingerprintingSettings.isSome()) {
        overriddenFingerprintingSettingsArg.emplace(
            overriddenFingerprintingSettings.ref());
      }
    }
  } else {
    net::CookieJarSettings::Cast(cookieJarSettings)->SetPartitionKey(uri);
    firstPartyURI = uri;

    // The service worker is for a first-party context, we can use the uri of
    // the service worker as the first-party domain to get the fingerprinting
    // protection overrides.
    overriddenFingerprintingSettings =
        nsRFPService::GetOverriddenFingerprintingSettingsForURI(uri, nullptr,
                                                                isPBM);

    if (overriddenFingerprintingSettings.isSome()) {
      overriddenFingerprintingSettingsArg.emplace(
          overriddenFingerprintingSettings.ref());
    }
  }

  if (ContentBlockingAllowList::Check(principal, isPBM)) {
    net::CookieJarSettings::Cast(cookieJarSettings)
        ->SetIsOnContentBlockingAllowList(true);
  }

  bool shouldResistFingerprinting =
      nsContentUtils::ShouldResistFingerprinting_dangerous(
          principal,
          "Service Workers exist outside a Document or Channel; as a property "
          "of the domain (and origin attributes). We don't have a "
          "CookieJarSettings to perform the *nested check*, but we can rely on"
          "the FPI/dFPI partition key check. The WorkerPrivate's "
          "ShouldResistFingerprinting function for the ServiceWorker depends "
          "on this boolean and will also consider an explicit RFPTarget.",
          RFPTarget::IsAlwaysEnabledForPrecompute) &&
      !nsContentUtils::ETPSaysShouldNotResistFingerprinting(cookieJarSettings,
                                                            isPBM);

  if (shouldResistFingerprinting && NS_SUCCEEDED(rv) && firstPartyURI) {
    auto rfpKey = nsRFPService::GenerateKeyForServiceWorker(
        firstPartyURI, principal, foreignByAncestorContext);
    if (rfpKey.isSome()) {
      net::CookieJarSettings::Cast(cookieJarSettings)
          ->SetFingerprintingRandomizationKey(rfpKey.ref());
    }
  }

  net::CookieJarSettingsArgs cjsData;
  net::CookieJarSettings::Cast(cookieJarSettings)->Serialize(cjsData);

  nsCOMPtr<nsIPrincipal> partitionedPrincipal;
  rv = StoragePrincipalHelper::CreatePartitionedPrincipalForServiceWorker(
      principal, cookieJarSettings, getter_AddRefs(partitionedPrincipal));
  if (NS_FAILED(rv)) {
    return fail("cannot create the partitioned principal", rv);
  }

  PrincipalInfo partitionedPrincipalInfo;
  rv =
      PrincipalToPrincipalInfo(partitionedPrincipal, &partitionedPrincipalInfo);
  if (NS_FAILED(rv)) {
    return fail("cannot serialize the partitioned principal", rv);
  }

  StorageAccess storageAccess =
      StorageAllowedForServiceWorker(principal, cookieJarSettings);

  ServiceWorkerData serviceWorkerData;
  serviceWorkerData.cacheName() = mInfo->CacheName();
  serviceWorkerData.loadFlags() = static_cast<uint32_t>(
      mInfo->GetImportsLoadFlags() | nsIChannel::LOAD_BYPASS_SERVICE_WORKER);
  serviceWorkerData.id() = std::move(id);

  nsAutoCString domain;
  rv = uri->GetHost(domain);
  if (NS_FAILED(rv)) {
    return fail("cannot get the host of the URI", rv);
  }

  auto remoteType = RemoteWorkerManager::GetRemoteType(
      principal, WorkerKind::WorkerKindService,
      RemoteType::SharedWeb(principal->OriginAttributesRef()));
  if (remoteType.isErr()) {
    return fail("cannot get the remote type", remoteType.unwrapErr());
  }

  // Determine if the service worker is registered under a third-party context
  // by checking if it's running under a partitioned principal.
  bool isThirdPartyContextToTopWindow =
      !principal->OriginAttributesRef().mPartitionKey.IsEmpty();

  mClientInfo = ClientManager::CreateInfo(
      ClientType::Serviceworker,
      // The partitioned principal for ServiceWorkers is currently always
      // partitioned and so we only use it when in a third party context.
      isThirdPartyContextToTopWindow ? partitionedPrincipal : principal);
  if (mClientInfo.isNothing()) {
    return fail("cannot create the ClientInfo", NS_ERROR_DOM_INVALID_STATE_ERR);
  }

  mClientInfo->SetAgentClusterId(regInfo->AgentClusterId());
  mClientInfo->SetURL(mInfo->ScriptSpec());
  mClientInfo->SetFrameType(FrameType::None);

  WorkerOptions workerOptions;
  workerOptions.mCredentials = RequestCredentials::Omit;
  workerOptions.mType = mInfo->Type();

  // Build a copy of the ClientInfo for the IPC message with ipAddressSpace set
  // for LNA checks. We must NOT modify mClientInfo itself because it is used
  // for ServiceWorker lookups (GetServiceWorkerByClientInfo) via operator==,
  // which compares all fields including policyContainerArgs. Modifying
  // mClientInfo would break those lookups and prevent SWs from spawning.
  ClientInfo ipcClientInfo = mClientInfo.ref();
  mozilla::ipc::PolicyContainerArgs policyContainerArgs;
  policyContainerArgs.ipAddressSpace() =
      static_cast<nsILoadInfo::IPAddressSpace>(regInfo->GetIPAddressSpace());
  ipcClientInfo.SetPolicyContainerArgs(policyContainerArgs);

  mRemoteWorkerData = RemoteWorkerData(
      NS_ConvertUTF8toUTF16(mInfo->ScriptSpec()), uri, uri, workerOptions,
      /* loading principal */ principalInfo, principalInfo,
      partitionedPrincipalInfo,
      /* useRegularPrincipal */ true,

      // ServiceWorkers run as first-party, no storage-access permission needed.
      /* usingStorageAccess */ false,

      cjsData, domain,
      /* isSecureContext */ true,
      /* clientInfo */ ipcClientInfo.ToIPC(),

      // The RemoteWorkerData CTOR doesn't allow to set the referrerInfo via
      // already_AddRefed<>. Let's set it to null.
      /* referrerInfo */ nullptr,

      storageAccess, isThirdPartyContextToTopWindow, shouldResistFingerprinting,
      overriddenFingerprintingSettingsArg, isOn3PCBExceptionList,
      // Origin trials are associated to a window, so it doesn't make sense on
      // service workers.
      OriginTrials(), std::move(serviceWorkerData), regInfo->AgentClusterId(),
      remoteType.unwrap(),
      // Bug 2040904. Add support for language override for service workers.
      ""_ns, nsTArray<nsString>(),
      // Bug 2039330. Add support for timezone override for service workers.
      u""_ns);

  mRemoteWorkerData.referrerInfo() = MakeAndAddRef<ReferrerInfo>(nullptr);

  // This fills in the rest of mRemoteWorkerData.serviceWorkerData().
  RefreshRemoteWorkerData(regInfo);

  neutralizeOnFailure.release();
  return NS_OK;
}

void ServiceWorkerPrivate::RegenerateClientInfo() {
  // inductively, this object can only still be alive after Initialize() if the
  // mClientInfo was correctly initialized; a failed Initialize() clears mInfo,
  // which stops us from ever spawning a worker and therefore from getting here.
  MOZ_DIAGNOSTIC_ASSERT(mClientInfo.isSome());

  // Preserve the ipAddressSpace from the current RemoteWorkerData clientInfo
  // before re-creating mClientInfo, so that LNA checks continue to work on
  // subsequent spawns. mClientInfo itself must not carry policyContainerArgs
  // (see Initialize() comment), so we apply it only to the IPC copy.
  nsILoadInfo::IPAddressSpace ipAddressSpace = nsILoadInfo::Unknown;
  ClientInfo current(mRemoteWorkerData.clientInfo());
  if (const auto& args = current.GetPolicyContainerArgs()) {
    ipAddressSpace = args->ipAddressSpace();
  }

  mClientInfo = ClientManager::CreateInfo(
      ClientType::Serviceworker, mClientInfo->GetPrincipal().unwrap().get());

  if (ipAddressSpace != nsILoadInfo::Unknown) {
    ClientInfo ipcClientInfo = mClientInfo.ref();
    mozilla::ipc::PolicyContainerArgs policyContainerArgs;
    policyContainerArgs.ipAddressSpace() = ipAddressSpace;
    ipcClientInfo.SetPolicyContainerArgs(policyContainerArgs);
    mRemoteWorkerData.clientInfo() = ipcClientInfo.ToIPC();
  } else {
    mRemoteWorkerData.clientInfo() = mClientInfo.ref().ToIPC();
  }
}

nsresult ServiceWorkerPrivate::CheckScriptEvaluation(
    const ServiceWorkerLifetimeExtension& aLifetimeExtension,
    RefPtr<LifeCycleEventCallback> aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCallback);

  RefPtr<ServiceWorkerPrivate> self = this;

  /**
   * We need to capture the actor associated with the current Service Worker so
   * we can terminate it if script evaluation failed.
   */
  nsresult rv = SpawnWorkerIfNeeded(aLifetimeExtension);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    aCallback->SetResult(false);
    aCallback->Run();

    return rv;
  }

  MOZ_ASSERT(mControllerChild);

  RefPtr<RAIIActorPtrHolder> holder = mControllerChild;

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      ServiceWorkerCheckScriptEvaluationOpArgs(), aLifetimeExtension);
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = std::move(self), holder = std::move(holder),
       callback = aCallback](ServiceWorkerOpResult&& aResult) mutable {
        if (aResult.type() == ServiceWorkerOpResult::
                                  TServiceWorkerCheckScriptEvaluationOpResult) {
          auto& result =
              aResult.get_ServiceWorkerCheckScriptEvaluationOpResult();

          if (result.workerScriptExecutedSuccessfully()) {
            self->SetHandlesFetch(result.fetchHandlerWasAdded());
            if (self->mHandlesFetch == Unknown) {
              self->mHandlesFetch =
                  result.fetchHandlerWasAdded() ? Enabled : Disabled;
              // Update telemetry for # of running SW - the already-running SW
              // handles fetch
              if (self->mHandlesFetch == Enabled) {
                self->UpdateRunning(0, 1);
              }
            }

            callback->SetResult(result.workerScriptExecutedSuccessfully());
            callback->Run();
            return;
          }
        }

        /**
         * If script evaluation failed, first terminate the Service Worker
         * before invoking the callback.
         */
        MOZ_ASSERT_IF(aResult.type() == ServiceWorkerOpResult::Tnsresult,
                      NS_FAILED(aResult.get_nsresult()));

        // If a termination operation was already issued using `holder`...
        if (self->mControllerChild != holder) {
          holder->OnDestructor()->Then(
              GetCurrentSerialEventTarget(), __func__,
              [callback = std::move(callback)](
                  const GenericPromise::ResolveOrRejectValue&) {
                callback->SetResult(false);
                callback->Run();
              });

          return;
        }

        RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
        MOZ_ASSERT(swm);

        auto shutdownStateId = swm->MaybeInitServiceWorkerShutdownProgress();

        RefPtr<GenericNonExclusivePromise> promise =
            self->ShutdownInternal(shutdownStateId);

        swm->BlockShutdownOn(promise, shutdownStateId);

        promise->Then(
            GetCurrentSerialEventTarget(), __func__,
            [callback = std::move(callback)](
                const GenericNonExclusivePromise::ResolveOrRejectValue&) {
              callback->SetResult(false);
              callback->Run();
            });
      },
      [callback = aCallback] {
        callback->SetResult(false);
        callback->Run();
      });

  return NS_OK;
}

nsresult ServiceWorkerPrivate::SendMessageEvent(
    ipc::StructuredCloneData* aData,
    const ServiceWorkerLifetimeExtension& aLifetimeExtension,
    const PostMessageSource& aSource) {
  AssertIsOnMainThread();

  auto scopeExit = MakeScopeExit([&] { Shutdown(); });

  PBackgroundChild* bgChild = BackgroundChild::GetForCurrentThread();

  if (NS_WARN_IF(!bgChild)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  ServiceWorkerMessageEventOpArgs args;
  args.source() = aSource;
  args.clonedData() = aData;

  scopeExit.release();

  RefPtr<ServiceWorkerOpPromise> opPromise =
      ExecServiceWorkerOp(std::move(args), aLifetimeExtension);
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
      },
      [] {});

  return NS_OK;
}

nsresult ServiceWorkerPrivate::SendLifeCycleEvent(
    const nsAString& aEventType,
    const ServiceWorkerLifetimeExtension& aLifetimeExtension,
    const RefPtr<LifeCycleEventCallback>& aCallback) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aCallback);

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      ServiceWorkerLifeCycleEventOpArgs(nsString(aEventType)),
      aLifetimeExtension);
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [callback = aCallback](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);

        callback->SetResult(NS_SUCCEEDED(aResult.get_nsresult()));
        callback->Run();
      },
      [callback = aCallback] {
        callback->SetResult(false);
        callback->Run();
      });

  return NS_OK;
}

nsresult ServiceWorkerPrivate::SendCookieChangeEvent(
    const net::CookieStruct& aCookie, bool aCookieDeleted,
    RefPtr<ServiceWorkerRegistrationInfo> aRegistration) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aRegistration);

  // mInfo is cleared both when our ServiceWorkerInfo dies and when Initialize()
  // failed, and unlike the ops below we dereference it before delegating to
  // SpawnWorkerIfNeeded(), which is where that is normally caught.
  if (NS_WARN_IF(!mInfo)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  ServiceWorkerCookieChangeEventOpArgs args;
  args.cookie() = aCookie;
  args.deleted() = aCookieDeleted;

  if (mInfo->State() == ServiceWorkerState::Activating) {
    UniquePtr<PendingFunctionalEvent> pendingEvent =
        std::make_unique<PendingCookieChangeEvent>(
            this, std::move(aRegistration), std::move(args));

    mPendingFunctionalEvents.AppendElement(std::move(pendingEvent));

    return NS_OK;
  }

  MOZ_ASSERT(mInfo->State() == ServiceWorkerState::Activated);

  return SendCookieChangeEventInternal(std::move(aRegistration),
                                       std::move(args));
}

nsresult ServiceWorkerPrivate::SendCookieChangeEventInternal(
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    ServiceWorkerCookieChangeEventOpArgs&& aArgs) {
  MOZ_ASSERT(aRegistration);

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      std::move(aArgs),
      ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [registration = aRegistration](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);

        registration->MaybeScheduleTimeCheckAndUpdate();
      },
      [registration = aRegistration]() {
        registration->MaybeScheduleTimeCheckAndUpdate();
      });

  return NS_OK;
}

RefPtr<PushHandledPromise> ServiceWorkerPrivate::SendPushEvent(
    const nsAString& aMessageId, const Maybe<nsTArray<uint8_t>>& aData,
    RefPtr<ServiceWorkerRegistrationInfo> aRegistration) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aRegistration);

  // mInfo is cleared both when our ServiceWorkerInfo dies and when Initialize()
  // failed, and unlike the ops below we dereference it before delegating to
  // SpawnWorkerIfNeeded(), which is where that is normally caught.
  if (NS_WARN_IF(!mInfo)) {
    return PushHandledPromise::CreateAndReject(NS_ERROR_DOM_INVALID_STATE_ERR,
                                               __func__);
  }

  ServiceWorkerPushEventOpArgs args;
  args.messageId() = nsString(aMessageId);

  if (aData) {
    args.data() = aData.ref();
  } else {
    args.data() = void_t();
  }

  if (mInfo->State() == ServiceWorkerState::Activating) {
    auto pendingEvent = std::make_unique<PendingPushEvent>(
        this, std::move(aRegistration), std::move(args));
    RefPtr<PushHandledPromise> promise = pendingEvent->Promise();

    mPendingFunctionalEvents.AppendElement(std::move(pendingEvent));

    return promise;
  }

  MOZ_ASSERT(mInfo->State() == ServiceWorkerState::Activated);

  return SendPushEventInternal(std::move(aRegistration), std::move(args));
}

RefPtr<PushHandledPromise> ServiceWorkerPrivate::SendPushEventInternal(
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    ServiceWorkerPushEventOpArgs&& aArgs) {
  MOZ_ASSERT(aRegistration);

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      std::move(aArgs),
      ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));

  return opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [registration = std::move(aRegistration)](
          ServiceWorkerOpPromise::ResolveOrRejectValue&& aResult)
          -> RefPtr<PushHandledPromise> {
        registration->MaybeScheduleTimeCheckAndUpdate();

        if (aResult.IsReject()) {
          return PushHandledPromise::CreateAndReject(aResult.RejectValue(),
                                                     __func__);
        }

        MOZ_ASSERT(aResult.ResolveValue().type() ==
                   ServiceWorkerOpResult::Tnsresult);

        return PushHandledPromise::CreateAndResolve(Ok(), __func__);
      });
}

nsresult ServiceWorkerPrivate::SendPushSubscriptionChangeEvent(
    const RefPtr<nsIPushSubscription>& aOldSubscription) {
  AssertIsOnMainThread();

  ServiceWorkerPushSubscriptionChangeEventOpArgs args{};
  if (aOldSubscription) {
    PushSubscriptionData oldSubscription{};
    MOZ_TRY(GetSubscriptionParams(aOldSubscription, oldSubscription.endpoint(),
                                  oldSubscription.rawP256dhKey(),
                                  oldSubscription.authSecret(),
                                  oldSubscription.appServerKey()));
    args.oldSubscription().emplace(oldSubscription);
  }

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      std::move(args), ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
      },
      [] {});

  return NS_OK;
}

nsresult ServiceWorkerPrivate::SendNotificationClickEvent(
    const IPCNotification& aNotification, const nsAString& aAction) {
  MOZ_ASSERT(NS_IsMainThread());

  ServiceWorkerNotificationClickEventOpArgs clickArgs;
  clickArgs.notification() = aNotification;
  clickArgs.action() = aAction;

  ServiceWorkerNotificationEventOpArgs args(std::move(clickArgs));

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      std::move(args), ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
      },
      [] {});

  return NS_OK;
}

nsresult ServiceWorkerPrivate::SendNotificationCloseEvent(
    const IPCNotification& aNotification) {
  MOZ_ASSERT(NS_IsMainThread());

  ServiceWorkerNotificationCloseEventOpArgs closeArgs;
  closeArgs.notification() = aNotification;

  ServiceWorkerNotificationEventOpArgs args(std::move(closeArgs));

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      std::move(args), ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
      },
      [] {});

  return NS_OK;
}

nsresult ServiceWorkerPrivate::SendFetchEvent(
    nsCOMPtr<nsIInterceptedChannel> aChannel, nsILoadGroup* aLoadGroup,
    const nsAString& aClientId, const nsAString& aResultingClientId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aChannel);

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (NS_WARN_IF(!mInfo || !swm)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIChannel> channel;
  nsresult rv = aChannel->GetChannel(getter_AddRefs(channel));
  NS_ENSURE_SUCCESS(rv, rv);
  bool isNonSubresourceRequest =
      nsContentUtils::IsNonSubresourceRequest(channel);

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  if (isNonSubresourceRequest) {
    registration = swm->GetRegistration(mInfo->Principal(), mInfo->Scope());
  } else {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();

    // We'll check for a null registration below rather than an error code here.
    (void)swm->GetClientRegistration(loadInfo->GetClientInfo().ref(),
                                     getter_AddRefs(registration));
  }

  // Its possible the registration is removed between starting the interception
  // and actually dispatching the fetch event.  In these cases we simply
  // want to restart the original network request.  Since this is a normal
  // condition we handle the reset here instead of returning an error which
  // would in turn trigger a console report.
  if (!registration) {
    nsresult rv = aChannel->ResetInterception(false);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to resume intercepted network request");
      aChannel->CancelInterception(rv);
    }
    return NS_OK;
  }

  // Handle Fetch algorithm - step 16. If the service worker didn't register
  // any fetch event handlers, then abort the interception and maybe trigger
  // the soft update algorithm.
  if (!mInfo->HandlesFetch()) {
    nsresult rv = aChannel->ResetInterception(false);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to resume intercepted network request");
      aChannel->CancelInterception(rv);
    }

    // Trigger soft updates if necessary.
    registration->MaybeScheduleTimeCheckAndUpdate();

    return NS_OK;
  }

  auto scopeExit = MakeScopeExit([&] {
    aChannel->CancelInterception(NS_ERROR_INTERCEPTION_FAILED);
    Shutdown();
  });

  IPCInternalRequest request = MOZ_TRY(GetIPCInternalRequest(aChannel));

  scopeExit.release();

  bool preloadNavigation = isNonSubresourceRequest &&
                           request.method().LowerCaseEqualsASCII("get") &&
                           registration->GetNavigationPreloadState().enabled();

  RefPtr<FetchServicePromises> preloadResponsePromises;
  if (preloadNavigation) {
    preloadResponsePromises = SetupNavigationPreload(aChannel, registration);
  }

  ParentToParentServiceWorkerFetchEventOpArgs args(
      ServiceWorkerFetchEventOpArgsCommon(
          mInfo->ScriptSpec(), request, nsString(aClientId),
          nsString(aResultingClientId), isNonSubresourceRequest,
          preloadNavigation, mInfo->TestingInjectCancellation()),
      Nothing(), Nothing(), Nothing());

  if (mInfo->State() == ServiceWorkerState::Activating) {
    UniquePtr<PendingFunctionalEvent> pendingEvent =
        std::make_unique<PendingFetchEvent>(
            this, std::move(registration), std::move(args), std::move(aChannel),
            std::move(preloadResponsePromises));

    mPendingFunctionalEvents.AppendElement(std::move(pendingEvent));

    return NS_OK;
  }

  MOZ_ASSERT(mInfo->State() == ServiceWorkerState::Activated);

  return SendFetchEventInternal(std::move(registration), std::move(args),
                                std::move(aChannel),
                                std::move(preloadResponsePromises));
}

nsresult ServiceWorkerPrivate::SendFetchEventInternal(
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    ParentToParentServiceWorkerFetchEventOpArgs&& aArgs,
    nsCOMPtr<nsIInterceptedChannel>&& aChannel,
    RefPtr<FetchServicePromises>&& aPreloadResponseReadyPromises) {
  AssertIsOnMainThread();

  auto scopeExit = MakeScopeExit([&] { Shutdown(); });

  if (NS_WARN_IF(!mInfo)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  MOZ_TRY(SpawnWorkerIfNeeded(
      ServiceWorkerLifetimeExtension(FullLifetimeExtension{})));
  MOZ_TRY(MaybeStoreStreamForBackgroundThread(
      aChannel, aArgs.common().internalRequest()));

  scopeExit.release();

  MOZ_ASSERT(mControllerChild);

  RefPtr<RAIIActorPtrHolder> holder = mControllerChild;

  FetchEventOpChild::SendFetchEvent(
      mControllerChild->get(), std::move(aArgs), std::move(aChannel),
      std::move(aRegistration), std::move(aPreloadResponseReadyPromises),
      CreateEventKeepAliveToken())
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [holder = std::move(holder)](
                 const GenericPromise::ResolveOrRejectValue& aResult) {
               (void)NS_WARN_IF(aResult.IsReject());
             });

  return NS_OK;
}

Result<RefPtr<ServiceWorkerPrivate::PromiseExtensionWorkerHasListener>,
       nsresult>
ServiceWorkerPrivate::WakeForExtensionAPIEvent(
    const nsAString& aExtensionAPINamespace,
    const nsAString& aExtensionAPIEventName) {
  AssertIsOnMainThread();

  ServiceWorkerExtensionAPIEventOpArgs args;
  args.apiNamespace() = nsString(aExtensionAPINamespace);
  args.apiEventName() = nsString(aExtensionAPIEventName);

  auto promise =
      MakeRefPtr<PromiseExtensionWorkerHasListener::Private>(__func__);

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      std::move(args), ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(
            aResult.type() ==
            ServiceWorkerOpResult::TServiceWorkerExtensionAPIEventOpResult);
        auto& result = aResult.get_ServiceWorkerExtensionAPIEventOpResult();
        promise->Resolve(result.extensionAPIEventListenerWasAdded(), __func__);
      },
      [promise]() { promise->Reject(NS_ERROR_FAILURE, __func__); });

  RefPtr<PromiseExtensionWorkerHasListener> outPromise(promise);
  return outPromise;
}

nsresult ServiceWorkerPrivate::SpawnWorkerIfNeeded(
    const ServiceWorkerLifetimeExtension& aLifetimeExtension) {
  AssertIsOnMainThread();

  // We don't need to spawn if we already have a spawned, non-terminated worker.
  if (mControllerChild) {
    // We only need to renew the keepalive token if we actually want to extend
    // the worker's lifetime; we don't for termination requests.
    if (aLifetimeExtension.LifetimeExtendsIntoTheFuture()) {
      RenewKeepAliveToken(aLifetimeExtension);
    }
    return NS_OK;
  }

  if (!mInfo) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  // Don't spawn the ServiceWorker if we don't want to extend its life.
  if (NS_WARN_IF(!aLifetimeExtension.LifetimeExtendsIntoTheFuture())) {
    return NS_ERROR_DOM_TIMEOUT_ERR;
  }

  mServiceWorkerLaunchTimeStart = TimeStamp::Now();

  PBackgroundChild* bgChild = BackgroundChild::GetForCurrentThread();

  if (NS_WARN_IF(!bgChild)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  // If the worker principal is an extension principal, then we should not spawn
  // a worker if there is no WebExtensionPolicy associated to that principal
  // or if the WebExtensionPolicy is not active.
  auto* principal = mInfo->Principal();
  if (principal->SchemeIs("moz-extension")) {
    auto* addonPolicy = BasePrincipal::Cast(principal)->AddonPolicy();
    if (!addonPolicy || !addonPolicy->Active()) {
      NS_WARNING(
          "Trying to wake up a service worker for a disabled webextension.");
      return NS_ERROR_DOM_INVALID_STATE_ERR;
    }
  }

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

  if (NS_WARN_IF(!swm)) {
    return NS_ERROR_DOM_ABORT_ERR;
  }

  RefPtr<ServiceWorkerRegistrationInfo> regInfo =
      swm->GetRegistration(principal, mInfo->Scope());

  if (NS_WARN_IF(!regInfo)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  RefreshRemoteWorkerData(regInfo);

  mLaunchCount++;

  RefPtr<RemoteWorkerControllerChild> controllerChild =
      new RemoteWorkerControllerChild(this);

  if (NS_WARN_IF(!bgChild->SendPRemoteWorkerControllerConstructor(
          controllerChild, mRemoteWorkerData))) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  mPendingSpawnLifetime = aLifetimeExtension;

  mControllerChild = new RAIIActorPtrHolder(controllerChild.forget());

  // Update Running count here because we may Terminate before we get
  // CreationSucceeded().  We'll update if it handles Fetch if that changes
  // (
  UpdateRunning(1, mHandlesFetch == Enabled ? 1 : 0);

  return NS_OK;
}

void ServiceWorkerPrivate::TerminateWorker(
    Maybe<RefPtr<Promise>> aMaybePromise) {
  MOZ_ASSERT(NS_IsMainThread());
  mIdleWorkerTimer->Cancel();
  mIdleDeadline = TimeStamp();
  // We call the shutdown method prior to dropping mIdleKeepAliveToken in order
  // to ensure that the passed-in promise tracks the shutdown of the current
  // worker.
  //
  // More detail: Dropping the token can cause re-entrance to this method via
  // ReleaseToken if it is not already the method calling.  Shutdown() is
  // idempotent except for the promise we pass in; it will only be chained to
  // track the actual termination if mControllerChild is not null.  On the
  // second call when mControllerChild is null, it will resolved immediately
  // with undefined.  The call from ReleaseToken does not pass a Promise and
  // does not care, so it goes second.
  //
  // We of course could hold onto the underlying shutdown promise until it
  // resolves so that new calls could chain, but because it's conceptually
  // possible to have multiple spawns and shutdowns in flight and our promise
  // argument is really only for testing / devtools where we only expect a
  // single actively involved party at a time, this way works sufficiently.
  Shutdown(std::move(aMaybePromise));
  // As per the above, this may potentially
  mIdleKeepAliveToken = nullptr;
}

void ServiceWorkerPrivate::NoteDeadServiceWorkerInfo() {
  MOZ_ASSERT(NS_IsMainThread());

  TerminateWorker();
  mInfo = nullptr;
}

void ServiceWorkerPrivate::UpdateState(ServiceWorkerState aState) {
  AssertIsOnMainThread();

  if (!mControllerChild) {
    return;
  }

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      ServiceWorkerUpdateStateOpArgs(aState),
      // Lifecycle events potentially update the lifetime for ServiceWorkers
      // controlling a page, but there's no need to update the lifetime to tell
      // a SW that its state has changed.
      ServiceWorkerLifetimeExtension(NoLifetimeExtension{}));
  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
      },
      [] {});

  if (aState != ServiceWorkerState::Activated) {
    return;
  }

  for (auto& event : mPendingFunctionalEvents) {
    (void)NS_WARN_IF(NS_FAILED(event->Send()));
  }

  mPendingFunctionalEvents.Clear();
}

void ServiceWorkerPrivate::UpdateIsOnContentBlockingAllowList(
    bool aOnContentBlockingAllowList) {
  AssertIsOnMainThread();

  if (!mControllerChild) {
    return;
  }

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      ServiceWorkerUpdateIsOnContentBlockingAllowListOpArgs(
          aOnContentBlockingAllowList),
      ServiceWorkerLifetimeExtension(NoLifetimeExtension{}));

  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
      },
      [] {});
}

nsresult ServiceWorkerPrivate::GetDebugger(nsIWorkerDebugger** aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aResult);

  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult ServiceWorkerPrivate::AttachDebugger() {
  MOZ_ASSERT(NS_IsMainThread());

  // When the first debugger attaches to a worker, we spawn a worker if needed,
  // and cancel the idle timeout. The idle timeout should not be reset until
  // the last debugger detached from the worker.
  if (!mDebuggerCount) {
    nsresult rv = SpawnWorkerIfNeeded(
        ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
    NS_ENSURE_SUCCESS(rv, rv);

    /**
     * Renewing the idle KeepAliveToken for spawning workers happens
     * asynchronously, rather than synchronously.
     * The asynchronous renewal is because the actual spawning of workers occurs
     * in a content process, so we will only renew once notified that the worker
     * has been successfully created
     *
     * This means that the DevTools way of starting up a worker by calling
     * `AttachDebugger` immediately followed by `DetachDebugger` will spawn and
     * immediately terminate a worker (because `mTokenCount` is possibly 0
     * due to the idle KeepAliveToken being created asynchronously). So, just
     * renew the KeepAliveToken right now.
     */
    RenewKeepAliveToken(
        ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
    mIdleWorkerTimer->Cancel();
  }

  ++mDebuggerCount;

  return NS_OK;
}

nsresult ServiceWorkerPrivate::DetachDebugger() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mDebuggerCount) {
    return NS_ERROR_UNEXPECTED;
  }

  --mDebuggerCount;

  // When the last debugger detaches from a worker, we either reset the idle
  // timeout, or terminate the worker if there are no more active tokens.
  if (!mDebuggerCount) {
    if (mTokenCount) {
      ResetIdleTimeout(ServiceWorkerLifetimeExtension(FullLifetimeExtension{}));
    } else {
      TerminateWorker();
    }
  }

  return NS_OK;
}

bool ServiceWorkerPrivate::IsIdle() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mTokenCount == 0 || (mTokenCount == 1 && mIdleKeepAliveToken);
}

RefPtr<GenericPromise> ServiceWorkerPrivate::GetIdlePromise() {
#ifdef DEBUG
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!IsIdle());
  MOZ_ASSERT(!mIdlePromiseObtained, "Idle promise may only be obtained once!");
  mIdlePromiseObtained = true;
#endif

  RefPtr<GenericPromise> promise = mIdlePromiseHolder.Ensure(__func__);
  mIdlePromiseHolder.UseDirectTaskDispatch(__func__);

  return promise;
}

namespace {

class ServiceWorkerPrivateTimerCallback final : public nsITimerCallback,
                                                public nsINamed {
 public:
  using Method = void (ServiceWorkerPrivate::*)(nsITimer*);

  ServiceWorkerPrivateTimerCallback(ServiceWorkerPrivate* aServiceWorkerPrivate,
                                    Method aMethod)
      : mServiceWorkerPrivate(aServiceWorkerPrivate), mMethod(aMethod) {}

  NS_IMETHOD
  Notify(nsITimer* aTimer) override {
    (mServiceWorkerPrivate->*mMethod)(aTimer);
    mServiceWorkerPrivate = nullptr;
    return NS_OK;
  }

  NS_IMETHOD
  GetName(nsACString& aName) override {
    aName.AssignLiteral("ServiceWorkerPrivateTimerCallback");
    return NS_OK;
  }

 private:
  ~ServiceWorkerPrivateTimerCallback() = default;

  RefPtr<ServiceWorkerPrivate> mServiceWorkerPrivate;
  Method mMethod;

  NS_DECL_THREADSAFE_ISUPPORTS
};

NS_IMPL_ISUPPORTS(ServiceWorkerPrivateTimerCallback, nsITimerCallback,
                  nsINamed);

}  // anonymous namespace

void ServiceWorkerPrivate::NoteIdleWorkerCallback(nsITimer* aTimer) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(aTimer == mIdleWorkerTimer, "Invalid timer!");

  // Release ServiceWorkerPrivate's token, since the grace period has ended.
  mIdleKeepAliveToken = nullptr;
  // Null out our deadline as well.
  mIdleDeadline = TimeStamp();

  if (mControllerChild) {
    // If we still have a living worker at this point it means that either there
    // are pending waitUntil promises or the worker is doing some long-running
    // computation. Wait a bit more until we forcibly terminate the worker.
    uint32_t timeout =
        Preferences::GetInt("dom.serviceWorkers.idle_extended_timeout");
    nsCOMPtr<nsITimerCallback> cb = new ServiceWorkerPrivateTimerCallback(
        this, &ServiceWorkerPrivate::TerminateWorkerCallback);
    DebugOnly<nsresult> rv = mIdleWorkerTimer->InitWithCallback(
        cb, timeout, nsITimer::TYPE_ONE_SHOT);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
}

void ServiceWorkerPrivate::TerminateWorkerCallback(nsITimer* aTimer) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(aTimer == this->mIdleWorkerTimer, "Invalid timer!");

  // mInfo must be non-null at this point because NoteDeadServiceWorkerInfo
  // which zeroes it calls TerminateWorker which cancels our timer which will
  // ensure we don't get invoked even if the nsTimerEvent is in the event queue.
  // The other place which zeroes mInfo, a failed Initialize(), stops us from
  // ever spawning a worker and therefore from ever arming this timer.
  ServiceWorkerManager::LocalizeAndReportToAllClients(
      mInfo->Scope(), "ServiceWorkerGraceTimeoutTermination",
      nsTArray<nsString>{NS_ConvertUTF8toUTF16(mInfo->Scope())});

  TerminateWorker();
}

void ServiceWorkerPrivate::RenewKeepAliveToken(
    const ServiceWorkerLifetimeExtension& aLifetimeExtension) {
  // We should have an active worker if we're renewing the keep alive token.
  MOZ_ASSERT(mControllerChild);

  // If there is at least one debugger attached to the worker, the idle worker
  // timeout was canceled when the first debugger attached to the worker. It
  // should not be reset until the last debugger detaches from the worker.
  if (!mDebuggerCount) {
    ResetIdleTimeout(aLifetimeExtension);
  }

  if (!mIdleKeepAliveToken) {
    mIdleKeepAliveToken = new KeepAliveToken(this);
  }
}

void ServiceWorkerPrivate::ResetIdleTimeout(
    const ServiceWorkerLifetimeExtension& aLifetimeExtension) {
  TimeStamp now = TimeStamp::NowLoRes();
  TimeStamp existing = mIdleDeadline;
  // Normalize the extension, returning a Null TimeStamp if the lifetime
  // extension does not actually extend our lifetime.
  TimeStamp normalizedExtension = aLifetimeExtension.match(
      // No extension means no extension!
      [](const NoLifetimeExtension& nle) { return TimeStamp(); },
      [&existing, &now](const PropagatedLifetimeExtension& ple) {
        // Ignore null deadlines or deadlines that are in the past.
        if (ple.mDeadline.IsNull() || ple.mDeadline < now) {
          return TimeStamp();
        }
        // Use this new deadline if our existing deadline is null or the
        // received deadline is after our current deadline.
        if (existing.IsNull() || ple.mDeadline > existing) {
          return ple.mDeadline;
        }
        // (This means our existing deadline extends further into the future so
        // we don't want to change our deadline.)
        return TimeStamp();
      },
      [&now](const FullLifetimeExtension& fle) {
        return now + TimeDuration::FromMilliseconds(Preferences::GetInt(
                         "dom.serviceWorkers.idle_timeout"));
      });

  if (normalizedExtension.IsNull()) {
    // Convert the unlikely situation where we are trying to reset the timeout
    // without extension and where we have no existing timeout into a 0 timeout.
    // This is important because we don't want to let the ServiceWorker live
    // forever!
    MOZ_ASSERT(!existing.IsNull());
    if (NS_WARN_IF(existing.IsNull())) {
      normalizedExtension = now;
    } else {
      // Return without altering the deadline or churning the timer.
      return;
    }
  }

  mIdleDeadline = normalizedExtension;

  nsCOMPtr<nsITimerCallback> cb = new ServiceWorkerPrivateTimerCallback(
      this, &ServiceWorkerPrivate::NoteIdleWorkerCallback);
  // We don't need high resolution but TimeDuration provides better type safety.
  DebugOnly<nsresult> rv = mIdleWorkerTimer->InitHighResolutionWithCallback(
      cb, mIdleDeadline - now, nsITimer::TYPE_ONE_SHOT);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void ServiceWorkerPrivate::AddToken() {
  MOZ_ASSERT(NS_IsMainThread());
  ++mTokenCount;
}

void ServiceWorkerPrivate::ReleaseToken() {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mTokenCount > 0);
  --mTokenCount;

  if (IsIdle()) {
    mIdlePromiseHolder.ResolveIfExists(true, __func__);

    if (!mTokenCount) {
      TerminateWorker();
    }

    // mInfo can be nullptr here if NoteDeadServiceWorkerInfo() is called while
    // the KeepAliveToken is being proxy released as a runnable.
    else if (mInfo) {
      RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
      if (swm) {
        swm->WorkerIsIdle(mInfo);
      }
    }
  }
}

already_AddRefed<KeepAliveToken>
ServiceWorkerPrivate::CreateEventKeepAliveToken() {
  MOZ_ASSERT(NS_IsMainThread());

  // When the WorkerPrivate is in a separate process, we first hold a normal
  // KeepAliveToken. Then, after we're notified that the worker is alive, we
  // create the idle KeepAliveToken.
  MOZ_ASSERT(mIdleKeepAliveToken || mControllerChild);

  RefPtr<KeepAliveToken> ref = new KeepAliveToken(this);
  return ref.forget();
}

void ServiceWorkerPrivate::SetHandlesFetch(bool aValue) {
  MOZ_ASSERT(NS_IsMainThread());

  if (NS_WARN_IF(!mInfo)) {
    return;
  }

  mInfo->SetHandlesFetch(aValue);
}

RefPtr<GenericPromise> ServiceWorkerPrivate::SetSkipWaitingFlag() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInfo);

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

  if (!swm) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  RefPtr<ServiceWorkerRegistrationInfo> regInfo =
      swm->GetRegistration(mInfo->Principal(), mInfo->Scope());

  if (!regInfo) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  mInfo->SetSkipWaitingFlag();

  RefPtr<GenericPromise::Private> promise =
      new GenericPromise::Private(__func__);

  // The ServiceWorker calling skipWaiting on itself is not a basis for lifetime
  // extension on its own.  `TryToActivate` will upgrade the lifetime to a full
  // extension iff there are any controlled pages.
  auto lifetime = ServiceWorkerLifetimeExtension(NoLifetimeExtension{});

  regInfo->TryToActivateAsync(lifetime,
                              [promise] { promise->Resolve(true, __func__); });

  return promise;
}

/* static */
void ServiceWorkerPrivate::UpdateRunning(int32_t aDelta, int32_t aFetchDelta) {
  // Record values for time we were running at the current values
  RefPtr<ServiceWorkerManager> manager(ServiceWorkerManager::GetInstance());
  manager->RecordTelemetry(sRunningServiceWorkers, sRunningServiceWorkersFetch);

  MOZ_ASSERT(((int64_t)sRunningServiceWorkers) + aDelta >= 0);
  sRunningServiceWorkers += aDelta;
  if (sRunningServiceWorkers > sRunningServiceWorkersMax) {
    sRunningServiceWorkersMax = sRunningServiceWorkers;
    LOG(("ServiceWorker max now %d", sRunningServiceWorkersMax));
  }
  MOZ_ASSERT(((int64_t)sRunningServiceWorkersFetch) + aFetchDelta >= 0);
  sRunningServiceWorkersFetch += aFetchDelta;
  if (sRunningServiceWorkersFetch > sRunningServiceWorkersFetchMax) {
    sRunningServiceWorkersFetchMax = sRunningServiceWorkersFetch;
    LOG(("ServiceWorker Fetch max now %d", sRunningServiceWorkersFetchMax));
  }
  LOG(("ServiceWorkers running now %d/%d", sRunningServiceWorkers,
       sRunningServiceWorkersFetch));
}

void ServiceWorkerPrivate::CreationFailed() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mControllerChild);

  if (mRemoteWorkerData.remoteType().IsWebServiceWorker()) {
    glean::service_worker::isolated_launch_time.AccumulateRawDuration(
        TimeStamp::Now() - mServiceWorkerLaunchTimeStart);
  } else {
    glean::service_worker::launch_time.AccumulateRawDuration(
        TimeStamp::Now() - mServiceWorkerLaunchTimeStart);
  }

  mPendingSpawnLifetime = ServiceWorkerLifetimeExtension(NoLifetimeExtension{});
  Shutdown();
}

void ServiceWorkerPrivate::CreationSucceeded() {
  AssertIsOnMainThread();
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mInfo);

  // It's possible for a request to terminate the worker to happen while the
  // worker is starting up, in which case we do not want to renew the keepalive
  // timer and we probably don't want to update the telemetry below either.
  if (NS_WARN_IF(!mControllerChild)) {
    mPendingSpawnLifetime =
        ServiceWorkerLifetimeExtension(NoLifetimeExtension{});
    return;
  }

  if (mRemoteWorkerData.remoteType().IsWebServiceWorker()) {
    glean::service_worker::isolated_launch_time.AccumulateRawDuration(
        TimeStamp::Now() - mServiceWorkerLaunchTimeStart);
  } else {
    glean::service_worker::launch_time.AccumulateRawDuration(
        TimeStamp::Now() - mServiceWorkerLaunchTimeStart);
  }

  RenewKeepAliveToken(mPendingSpawnLifetime);
  mPendingSpawnLifetime = ServiceWorkerLifetimeExtension(NoLifetimeExtension{});

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  nsCOMPtr<nsIPrincipal> principal = mInfo->Principal();
  RefPtr<ServiceWorkerRegistrationInfo> regInfo =
      swm->GetRegistration(principal, mInfo->Scope());
  if (regInfo) {
    // If it's already set, we're done and the running count is already set
    if (mHandlesFetch == Unknown) {
      if (regInfo->GetActive()) {
        mHandlesFetch =
            regInfo->GetActive()->HandlesFetch() ? Enabled : Disabled;
        if (mHandlesFetch == Enabled) {
          UpdateRunning(0, 1);
        }
      }
      // else we're likely still in Evaluating state, and don't know if it
      // handles fetch.  If so, defer updating the counter for Fetch until we
      // finish evaluation.  We already updated the Running count for All in
      // SpawnWorkerIfNeeded().
    }
  }
}

void ServiceWorkerPrivate::ErrorReceived(const ErrorValue& aError) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInfo);
  MOZ_ASSERT(mControllerChild);

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  MOZ_ASSERT(swm);

  ServiceWorkerInfo* info = mInfo;

  swm->HandleError(nullptr, info->Principal(), info->Scope(),
                   info->ScriptSpec(), u""_ns, ""_ns, u""_ns, 0, 0,
                   nsIScriptError::errorFlag, JSEXN_ERR);
}

void ServiceWorkerPrivate::Terminated() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInfo);
  MOZ_ASSERT(mControllerChild);

  Shutdown();
}

void ServiceWorkerPrivate::RefreshRemoteWorkerData(
    const RefPtr<ServiceWorkerRegistrationInfo>& aRegistration) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInfo);

  ServiceWorkerData& serviceWorkerData =
      mRemoteWorkerData.serviceWorkerData().get_ServiceWorkerData();
  serviceWorkerData.descriptor() = mInfo->Descriptor().ToIPC();
  serviceWorkerData.registrationDescriptor() =
      aRegistration->Descriptor().ToIPC();
}

RefPtr<FetchServicePromises> ServiceWorkerPrivate::SetupNavigationPreload(
    nsCOMPtr<nsIInterceptedChannel>& aChannel,
    const RefPtr<ServiceWorkerRegistrationInfo>& aRegistration) {
  MOZ_ASSERT(XRE_IsParentProcess());
  AssertIsOnMainThread();

  // create IPC request from the intercepted channel.
  auto result = GetIPCInternalRequest(aChannel);
  if (result.isErr()) {
    return nullptr;
  }
  IPCInternalRequest ipcRequest = result.unwrap();

  // Step 1. Clone the request for preload
  // Create the InternalResponse from the created IPCRequest.
  SafeRefPtr<InternalRequest> preloadRequest =
      MakeSafeRefPtr<InternalRequest>(ipcRequest);
  // Copy the request body from uploadChannel
  nsCOMPtr<nsIUploadChannel2> uploadChannel = do_QueryInterface(aChannel);
  if (uploadChannel) {
    nsCOMPtr<nsIInputStream> uploadStream;
    nsresult rv = uploadChannel->CloneUploadStream(
        &ipcRequest.bodySize(), getter_AddRefs(uploadStream));
    // Fail to get the request's body, stop navigation preload by returning
    // nullptr.
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return FetchService::NetworkErrorResponse(rv);
    }
    preloadRequest->SetBody(uploadStream, ipcRequest.bodySize());
  }

  // Set SkipServiceWorker for the navigation preload request
  preloadRequest->SetSkipServiceWorker();

  // Step 2. Append Service-Worker-Navigation-Preload header with
  //         registration->GetNavigationPreloadState().headerValue() on
  //         request's header list.
  IgnoredErrorResult err;
  auto headersGuard = preloadRequest->Headers()->Guard();
  preloadRequest->Headers()->SetGuard(HeadersGuardEnum::None, err);
  preloadRequest->Headers()->Append(
      "Service-Worker-Navigation-Preload"_ns,
      aRegistration->GetNavigationPreloadState().headerValue(), err);
  preloadRequest->Headers()->SetGuard(headersGuard, err);

  // Step 3. Perform fetch through FetchService with the cloned request
  if (!err.Failed()) {
    nsCOMPtr<nsIChannel> underlyingChannel;
    MOZ_ALWAYS_SUCCEEDS(
        aChannel->GetChannel(getter_AddRefs(underlyingChannel)));
    RefPtr<FetchService> fetchService = FetchService::GetInstance();
    return fetchService->Fetch(AsVariant(FetchService::NavigationPreloadArgs{
        std::move(preloadRequest), underlyingChannel}));
  }
  return FetchService::NetworkErrorResponse(NS_ERROR_UNEXPECTED);
}

void ServiceWorkerPrivate::Shutdown(Maybe<RefPtr<Promise>>&& aMaybePromise) {
  AssertIsOnMainThread();

  if (mControllerChild) {
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

    MOZ_ASSERT(swm,
               "All Service Workers should start shutting down before the "
               "ServiceWorkerManager does!");

    auto shutdownStateId = swm->MaybeInitServiceWorkerShutdownProgress();

    RefPtr<GenericNonExclusivePromise> promise =
        ShutdownInternal(shutdownStateId);
    swm->BlockShutdownOn(promise, shutdownStateId);
    if (aMaybePromise.isSome() && aMaybePromise.ref()) {
      promise->Then(
          GetCurrentSerialEventTarget(), __func__,
          [listener = aMaybePromise.ref()] {
            listener->MaybeResolveWithUndefined();
          },
          [listener = aMaybePromise.ref()] {
            listener->MaybeResolveWithUndefined();
          });
    }
  } else if (aMaybePromise.isSome() && aMaybePromise.ref()) {
    aMaybePromise.ref()->MaybeResolveWithUndefined();
  }

  MOZ_ASSERT(!mControllerChild);
}

RefPtr<GenericNonExclusivePromise> ServiceWorkerPrivate::ShutdownInternal(
    uint32_t aShutdownStateId) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mControllerChild);

  mPendingFunctionalEvents.Clear();

  mControllerChild->get()->RevokeObserver(this);

  if (StaticPrefs::dom_serviceWorkers_testing_enabled()) {
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    if (os) {
      os->NotifyObservers(nullptr, "service-worker-shutdown", nullptr);
    }
  }

  RefPtr<GenericNonExclusivePromise::Private> promise =
      new GenericNonExclusivePromise::Private(__func__);

  RefPtr<ServiceWorkerOpPromise> opPromise = ExecServiceWorkerOp(
      ServiceWorkerTerminateWorkerOpArgs(aShutdownStateId),
      // It doesn't make sense to extend the lifetime in this case.  This will
      // also ensure that we don't try and spawn the ServiceWorker, but as our
      // assert at the top of this method makes clear, we don't expect to be in
      // that situation.
      ServiceWorkerLifetimeExtension(NoLifetimeExtension{}));

  opPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](ServiceWorkerOpResult&& aResult) {
        MOZ_ASSERT(aResult.type() == ServiceWorkerOpResult::Tnsresult);
        promise->Resolve(true, __func__);
      },
      [promise]() { promise->Reject(NS_ERROR_DOM_ABORT_ERR, __func__); });

  /**
   * After dispatching a termination operation, no new operations should
   * be routed through this actor anymore so we can drop the controller
   * reference.  This also means that the next time SpawnWorkerIfNeeded is
   * invoked we will spawn a new worker, creating a new mControllerChild.
   */
  mControllerChild = nullptr;
  // Create a new ClientInfo for the next time we potentially spawn this
  // ServiceWorker.  We do this now rather than immediately before spawning the
  // ServiceWorker so it's possible to know what the client id will be before
  // triggering the next spawn.
  RegenerateClientInfo();

  // Update here, since Evaluation failures directly call ShutdownInternal
  UpdateRunning(-1, mHandlesFetch == Enabled ? -1 : 0);

  return promise;
}

RefPtr<ServiceWorkerOpPromise> ServiceWorkerPrivate::ExecServiceWorkerOp(
    ServiceWorkerOpArgs&& aArgs,
    const ServiceWorkerLifetimeExtension& aLifetimeExtension) {
  AssertIsOnMainThread();
  MOZ_ASSERT(
      aArgs.type() !=
          ServiceWorkerOpArgs::TParentToChildServiceWorkerFetchEventOpArgs,
      "FetchEvent operations should be sent through FetchEventOp(Proxy) "
      "actors!");

  nsresult rv = SpawnWorkerIfNeeded(aLifetimeExtension);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return ServiceWorkerOpPromise::CreateAndReject(rv, __func__);
  }

  MOZ_ASSERT(mControllerChild);

  RefPtr<ServiceWorkerPrivate> self = this;
  RefPtr<RAIIActorPtrHolder> holder = mControllerChild;
  RefPtr<KeepAliveToken> token =
      aArgs.type() == ServiceWorkerOpArgs::TServiceWorkerTerminateWorkerOpArgs
          ? nullptr
          : CreateEventKeepAliveToken();

  /**
   * NOTE: moving `aArgs` won't do anything until IPDL `SendMethod()` methods
   * can accept rvalue references rather than just const references.
   */
  RefPtr<ServiceWorkerOpPromise> result =
      mControllerChild->get()->SendExecServiceWorkerOp(aArgs)->MapErr(
          GetCurrentSerialEventTarget(), __func__,
          [self = std::move(self), holder = std::move(holder),
           token = std::move(token)](
              PRemoteWorkerControllerChild::ExecServiceWorkerOpPromise::
                  RejectValueType&& aResult) { return NS_ERROR_FAILURE; });

  return result;
}

}  // namespace mozilla::dom
