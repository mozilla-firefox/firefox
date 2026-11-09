/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedWorker.h"

#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/MessageChannel.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/PMessagePort.h"
#include "mozilla/dom/RemoteWorkerTypes.h"
#include "mozilla/dom/SharedWorkerBinding.h"
#include "mozilla/dom/SharedWorkerChild.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/dom/WorkerBinding.h"
#include "mozilla/dom/WorkerLoadInfo.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CookieJarSettings.h"
#include "nsGlobalWindowInner.h"
#include "nsPIDOMWindow.h"

#ifdef XP_WIN
#  undef PostMessage
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

SharedWorker::SharedWorker(nsPIDOMWindowInner* aWindow,
                           SharedWorkerChild* aActor, MessagePort* aMessagePort)
    : DOMEventTargetHelper(aWindow),
      mWindow(aWindow),
      mActor(aActor),
      mMessagePort(aMessagePort),
      mFrozen(false) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aMessagePort);
}

SharedWorker::~SharedWorker() {
  AssertIsOnMainThread();
  Close();
}

// static
already_AddRefed<SharedWorker> SharedWorker::Constructor(
    const GlobalObject& aGlobal, const TrustedScriptURLOrUSVString& aScriptURL,
    const StringOrWorkerOptions& aOptions, ErrorResult& aRv) {
  AssertIsOnMainThread();

  if (aOptions.IsString()) {
    WorkerOptions options;
    options.mName = aOptions.GetAsString();
    return SharedWorker::Constructor(aGlobal, aScriptURL, options, aRv);
  }

  return SharedWorker::Constructor(aGlobal, aScriptURL,
                                   aOptions.GetAsWorkerOptions(), aRv);
}

// static
already_AddRefed<SharedWorker> SharedWorker::Constructor(
    const GlobalObject& aGlobal, const TrustedScriptURLOrUSVString& aScriptURL,
    const WorkerOptions& aOptions, ErrorResult& aRv) {
  AssertIsOnMainThread();

  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(window);

  if (!window->IsCurrentInnerWindow()) {
    aRv.ThrowInvalidStateError(
        "Cannot create worker for a going to be discarded document");
    return nullptr;
  }

  // Our current idiom is that storage-related APIs specialize for the system
  // principal themselves, which is consistent with StorageAllowedForwindow not
  // specializing for the system principal.  Without this specialization we
  // would end up with ePrivateBrowsing for system principaled private browsing
  // windows which is explicitly not what we want.  System Principal code always
  // should have access to storage.  It may make sense to enhance
  // StorageAllowedForWindow in the future to handle this after comprehensive
  // auditing.
  nsCOMPtr<nsIPrincipal> principal = aGlobal.GetSubjectPrincipal();
  StorageAccess storageAllowed;
  if (principal && principal->IsSystemPrincipal()) {
    storageAllowed = StorageAccess::eAllow;
  } else {
    storageAllowed = StorageAllowedForWindow(window);
  }

  if (storageAllowed == StorageAccess::eDeny) {
    aRv.ThrowSecurityError("StorageAccess denied.");
    return nullptr;
  }

  if (ShouldPartitionStorage(storageAllowed) &&
      !StoragePartitioningEnabled(
          storageAllowed, window->GetExtantDoc()->CookieJarSettings())) {
    aRv.ThrowSecurityError("StoragePartitioning not enabled.");
    return nullptr;
  }

  // Assert that the principal private browsing state matches the
  // StorageAccess value.
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (storageAllowed == StorageAccess::ePrivateBrowsing) {
    uint32_t privateBrowsingId = 0;
    if (principal) {
      MOZ_ALWAYS_SUCCEEDS(principal->GetPrivateBrowsingId(&privateBrowsingId));
    }
    MOZ_DIAGNOSTIC_ASSERT(privateBrowsingId != 0);
  }
#endif  // MOZ_DIAGNOSTIC_ASSERT_ENABLED

  PBackgroundChild* actorChild = BackgroundChild::GetOrCreateForCurrentThread();
  if (!actorChild || !actorChild->CanSend()) {
    aRv.ThrowSecurityError("PBackground not available.");
    return nullptr;
  }

  JSContext* cx = aGlobal.Context();

  constexpr nsLiteralString sink = u"SharedWorker constructor"_ns;
  Maybe<nsAutoString> compliantStringHolder;
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(window);
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aScriptURL, sink, kTrustedTypesOnlySinkGroup, *global, principal,
          compliantStringHolder, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  WorkerLoadInfo loadInfo;
  aRv = WorkerPrivate::GetLoadInfo(cx, window, nullptr, *compliantString,
                                   aOptions.mType, aOptions.mCredentials, false,
                                   WorkerPrivate::OverrideLoadGroup,
                                   WorkerKindShared, &loadInfo);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  PrincipalInfo principalInfo;
  aRv = PrincipalToPrincipalInfo(loadInfo.mPrincipal, &principalInfo);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  PrincipalInfo loadingPrincipalInfo;
  aRv = PrincipalToPrincipalInfo(loadInfo.mLoadingPrincipal,
                                 &loadingPrincipalInfo);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // Here, the PartitionedPrincipal is always equal to the SharedWorker's
  // principal because the channel is not opened yet, and, because of this, it's
  // not classified. We need to force the correct originAttributes.
  //
  // The sharedWorker's principal could be a null principal, e.g. loading a
  // data url. In this case, we don't need to force the OAs for the partitioned
  // principal because creating storage from a null principal will fail anyway.
  // We should only do this for content principals.
  //
  // You can find more details in StoragePrincipalHelper.h
  if (ShouldPartitionStorage(storageAllowed) &&
      BasePrincipal::Cast(loadInfo.mPrincipal)->IsContentPrincipal()) {
    nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(window);
    if (!sop) {
      aRv.ThrowSecurityError("ScriptObjectPrincipal not available.");
      return nullptr;
    }

    nsIPrincipal* windowPrincipal = sop->GetPrincipal();
    if (!windowPrincipal) {
      aRv.ThrowSecurityError("WindowPrincipal not available.");
      return nullptr;
    }

    nsIPrincipal* windowPartitionedPrincipal = sop->PartitionedPrincipal();
    if (!windowPartitionedPrincipal) {
      aRv.ThrowSecurityError("WindowPartitionedPrincipal not available.");
      return nullptr;
    }

    if (!windowPrincipal->Equals(windowPartitionedPrincipal)) {
      loadInfo.mPartitionedPrincipal =
          BasePrincipal::Cast(loadInfo.mPrincipal)
              ->CloneForcingOriginAttributes(
                  BasePrincipal::Cast(windowPartitionedPrincipal)
                      ->OriginAttributesRef());
    }
  }

  // Throw early if this process would not be allowed to start a shared worker.
  if (!BackgroundChild::ValidatePrincipal(loadInfo.mLoadingPrincipal, {})) {
    MOZ_ASSERT_UNREACHABLE(
        "ValidatePrincipal failure in SharedWorker::Constructor");
    aRv.ThrowSecurityError("SharedWorker access not available.");
    return nullptr;
  }

  PrincipalInfo partitionedPrincipalInfo;
  if (loadInfo.mPrincipal->Equals(loadInfo.mPartitionedPrincipal)) {
    partitionedPrincipalInfo = principalInfo;
  } else {
    aRv = PrincipalToPrincipalInfo(loadInfo.mPartitionedPrincipal,
                                   &partitionedPrincipalInfo);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  // We don't actually care about this MessageChannel, but we use it to 'steal'
  // its 2 connected ports.
  RefPtr<MessageChannel> channel = MessageChannel::Constructor(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  UniqueMessagePortId portIdentifier;
  channel->Port1()->CloneAndDisentangle(portIdentifier);

  // Register this component to PBackground.
  bool isSecureContext = JS::GetIsSecureContext(js::GetContextRealm(cx));

  Maybe<ClientInfo> clientInfo = window->GetClientInfo();
  if (clientInfo.isNothing()) {
    aRv.ThrowInvalidStateError("Cannot create worker without a client");
    return nullptr;
  }

  nsID agentClusterId = nsID::GenerateUUID();

  net::CookieJarSettingsArgs cjsData;
  MOZ_ASSERT(loadInfo.mCookieJarSettings);
  net::CookieJarSettings::Cast(loadInfo.mCookieJarSettings)->Serialize(cjsData);

  Maybe<RFPTargetSet> overriddenFingerprintingSettingsArg;
  if (loadInfo.mOverriddenFingerprintingSettings.isSome()) {
    overriddenFingerprintingSettingsArg.emplace(
        loadInfo.mOverriddenFingerprintingSettings.ref());
  }

  RemoteWorkerData remoteWorkerData(
      nsString(*compliantString), loadInfo.mBaseURI,
      loadInfo.mResolvedScriptURI, aOptions, loadingPrincipalInfo,
      principalInfo, partitionedPrincipalInfo, loadInfo.mUseRegularPrincipal,
      loadInfo.mUsingStorageAccess, cjsData, loadInfo.mDomain, isSecureContext,
      clientInfo->ToIPC(), loadInfo.mReferrerInfo, storageAllowed,
      AntiTrackingUtils::IsThirdPartyWindow(window, nullptr),
      loadInfo.mShouldResistFingerprinting, overriddenFingerprintingSettingsArg,
      loadInfo.mIsOn3PCBExceptionList,
      OriginTrials::FromWindow(nsGlobalWindowInner::Cast(window)),
      void_t() /* OptionalServiceWorkerData */, agentClusterId,
      RemoteType(RemoteType::Kind::WebContent) /* ignored */,
      loadInfo.mLanguageOverrideLocale, loadInfo.mLanguageOverride.Clone(),
      loadInfo.mTimezoneOverride);

  PSharedWorkerChild* pActor = actorChild->SendPSharedWorkerConstructor(
      remoteWorkerData, loadInfo.mWindowID, portIdentifier.release());
  if (!pActor) {
    MOZ_ASSERT_UNREACHABLE("We already checked PBackground above.");
    aRv.ThrowSecurityError("PBackground not available.");
    return nullptr;
  }

  RefPtr<SharedWorkerChild> actor = static_cast<SharedWorkerChild*>(pActor);

  RefPtr<SharedWorker> sharedWorker =
      new SharedWorker(window, actor, channel->Port2());

  // Let's inform the window about this SharedWorker.
  nsGlobalWindowInner::Cast(window)->StoreSharedWorker(sharedWorker);
  actor->SetParent(sharedWorker);

  if (nsGlobalWindowInner::Cast(window)->IsSuspended()) {
    sharedWorker->Suspend();
  }

  return sharedWorker.forget();
}

MessagePort* SharedWorker::Port() {
  AssertIsOnMainThread();
  return mMessagePort;
}

void SharedWorker::Freeze() {
  AssertIsOnMainThread();
  MOZ_ASSERT(!IsFrozen());

  if (mFrozen) {
    return;
  }

  mFrozen = true;

  if (mActor) {
    mActor->SendFreeze();
  }
}

void SharedWorker::Thaw() {
  AssertIsOnMainThread();
  MOZ_ASSERT(IsFrozen());

  if (!mFrozen) {
    return;
  }

  mFrozen = false;

  if (mActor) {
    mActor->SendThaw();
  }

  if (!mFrozenEvents.IsEmpty()) {
    nsTArray<RefPtr<Event>> events = std::move(mFrozenEvents);

    for (uint32_t index = 0; index < events.Length(); index++) {
      RefPtr<Event>& event = events[index];
      MOZ_ASSERT(event);

      RefPtr<EventTarget> target = event->GetTarget();
      ErrorResult rv;
      target->DispatchEvent(*event, rv);
      if (rv.Failed()) {
        NS_WARNING("Failed to dispatch event!");
      }
    }
  }
}

void SharedWorker::QueueEvent(Event* aEvent) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aEvent);
  MOZ_ASSERT(IsFrozen());

  mFrozenEvents.AppendElement(aEvent);
}

void SharedWorker::Close() {
  AssertIsOnMainThread();

  if (mWindow) {
    nsGlobalWindowInner::Cast(mWindow)->ForgetSharedWorker(this);
    mWindow = nullptr;
  }

  if (mActor) {
    mActor->SendClose();
    mActor->SetParent(nullptr);
    mActor = nullptr;
  }

  if (mMessagePort) {
    mMessagePort->Close();
  }
}

void SharedWorker::Suspend() {
  if (mActor) {
    mActor->SendSuspend();
  }
}

void SharedWorker::Resume() {
  if (mActor) {
    mActor->SendResume();
  }
}

void SharedWorker::UpdateLanguageOverride(
    const nsACString& aLanguageOverride, const nsTArray<nsString>& aLanguages) {
  AssertIsOnMainThread();

  if (mActor) {
    mActor->SendSetLocaleOverride(aLanguageOverride, aLanguages);
  }
}

void SharedWorker::UpdateTimezoneOverride(const nsAString& aTimezoneOverride) {
  AssertIsOnMainThread();

  if (mActor) {
    mActor->SendUpdateTimezoneOverride(aTimezoneOverride);
  }
}

void SharedWorker::PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                               const Sequence<JSObject*>& aTransferable,
                               ErrorResult& aRv) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mMessagePort);

  mMessagePort->PostMessage(aCx, aMessage, aTransferable, aRv);
}

NS_IMPL_ADDREF_INHERITED(SharedWorker, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(SharedWorker, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SharedWorker)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_CLASS(SharedWorker)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SharedWorker,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMessagePort)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFrozenEvents)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SharedWorker,
                                                DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMessagePort)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFrozenEvents)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

JSObject* SharedWorker::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnMainThread();

  return SharedWorker_Binding::Wrap(aCx, this, aGivenProto);
}

void SharedWorker::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  AssertIsOnMainThread();

  if (IsFrozen()) {
    RefPtr<Event> event = aVisitor.mDOMEvent;
    if (!event) {
      event = EventDispatcher::CreateEvent(aVisitor.mEvent->mOriginalTarget,
                                           aVisitor.mPresContext,
                                           aVisitor.mEvent, u""_ns);
    }

    QueueEvent(event);

    aVisitor.mCanHandle = false;
    aVisitor.SetParentTarget(nullptr, false);
    return;
  }

  DOMEventTargetHelper::GetEventTargetParent(aVisitor);
}

void SharedWorker::DisconnectFromOwner() {
  Close();
  DOMEventTargetHelper::DisconnectFromOwner();
}

void SharedWorker::ErrorPropagation(nsresult aError) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(NS_FAILED(aError));

  RefPtr<AsyncEventDispatcher> errorEvent =
      new AsyncEventDispatcher(this, u"error"_ns, CanBubble::eNo);
  errorEvent->PostDOMEvent();

  Close();
}
