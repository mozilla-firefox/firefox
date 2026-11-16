/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BroadcastChannel.h"

#include "BroadcastChannelChild.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/dom/BroadcastChannelBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/MessageEvent.h"
#include "mozilla/dom/MessageEventBinding.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/RefMessageBodyService.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SharedMessageBody.h"
#include "mozilla/dom/StructuredCloneHolder.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsGlobalWindowInner.h"
#include "nsICookieJarSettings.h"

#ifdef XP_WIN
#  undef PostMessage
#endif

namespace mozilla {

using namespace ipc;

namespace dom {

using namespace ipc;

namespace {

class CloseRunnable final : public DiscardableRunnable {
 public:
  explicit CloseRunnable(BroadcastChannel* aBC)
      : DiscardableRunnable("BroadcastChannel CloseRunnable"), mBC(aBC) {
    MOZ_ASSERT(mBC);
  }

  NS_IMETHOD Run() override {
    mBC->Shutdown();
    return NS_OK;
  }

 private:
  ~CloseRunnable() = default;

  RefPtr<BroadcastChannel> mBC;
};

class TeardownRunnable {
 protected:
  explicit TeardownRunnable(BroadcastChannelChild* aActor) : mActor(aActor) {
    MOZ_ASSERT(mActor);
  }

  void RunInternal() {
    MOZ_ASSERT(mActor);
    if (!mActor->IsActorDestroyed()) {
      mActor->SendClose();
    }
  }

 protected:
  virtual ~TeardownRunnable() = default;

 private:
  RefPtr<BroadcastChannelChild> mActor;
};

class TeardownRunnableOnMainThread final : public Runnable,
                                           public TeardownRunnable {
 public:
  explicit TeardownRunnableOnMainThread(BroadcastChannelChild* aActor)
      : Runnable("TeardownRunnableOnMainThread"), TeardownRunnable(aActor) {}

  NS_IMETHOD Run() override {
    RunInternal();
    return NS_OK;
  }
};

class TeardownRunnableOnWorker final : public WorkerControlRunnable,
                                       public TeardownRunnable {
 public:
  TeardownRunnableOnWorker(WorkerPrivate* aWorkerPrivate,
                           BroadcastChannelChild* aActor)
      : WorkerControlRunnable("TeardownRunnableOnWorker"),
        TeardownRunnable(aActor) {}

  bool WorkerRun(JSContext*, WorkerPrivate*) override {
    RunInternal();
    return true;
  }

  bool PreDispatch(WorkerPrivate* aWorkerPrivate) override { return true; }

  void PostDispatch(WorkerPrivate* aWorkerPrivate,
                    bool aDispatchResult) override {}

  bool PreRun(WorkerPrivate* aWorkerPrivate) override { return true; }

  void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
               bool aRunResult) override {}
};

}  // namespace

BroadcastChannel::BroadcastChannel(nsIGlobalObject* aGlobal,
                                   const nsAString& aChannel,
                                   const nsID& aPortUUID)
    : DOMEventTargetHelper(aGlobal),
      mRefMessageBodyService(RefMessageBodyService::GetOrCreate()),
      mChannel(aChannel),
      mState(StateActive),
      mPortUUID(aPortUUID) {
  MOZ_ASSERT(aGlobal);
  KeepAliveIfHasListenersFor(nsGkAtoms::onmessage);
}

BroadcastChannel::~BroadcastChannel() {
  Shutdown();
  MOZ_ASSERT(!mWorkerRef);
}

JSObject* BroadcastChannel::WrapObject(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return BroadcastChannel_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */
already_AddRefed<BroadcastChannel>
BroadcastChannel::UnpartitionedTestingChannel(const GlobalObject& aGlobal,
                                              const nsAString& aChannel,
                                              ErrorResult& aRv) {
  // This function must not be used outside of automation.
  if (!xpc::IsInAutomation()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsID portUUID = {};
  aRv = nsID::GenerateUUIDInPlace(portUUID);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<BroadcastChannel> bc =
      new BroadcastChannel(global, aChannel, portUUID);

  nsCOMPtr<nsIPrincipal> unpartitionedPrincipal;

  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(global);
    if (NS_WARN_IF(!window)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(global);
    if (NS_WARN_IF(!sop)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    // We are *intentionally* getting the unpartitioned principal here for
    // testing purposes, but the correct thing to do normally is to get the
    // storage key/principal. Passerby, do not imitate this code.
    unpartitionedPrincipal = sop->GetPrincipal();
    if (!unpartitionedPrincipal) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
  } else {
    JSContext* cx = aGlobal.Context();

    WorkerPrivate* workerPrivate = GetWorkerPrivateFromContext(cx);
    MOZ_ASSERT(workerPrivate);

    RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
        workerPrivate, "BroadcastChannel", [bc]() { bc->Shutdown(); });
    // We are already shutting down the worker. Let's return a non-active
    // object.
    if (NS_WARN_IF(!workerRef)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    // We are *intentionally* getting the unpartitioned principal here for
    // testing purposes, but the correct thing to do normally is to get the
    // storage key/principal. Passerby, do not imitate this code.
    unpartitionedPrincipal = workerPrivate->GetPrincipal();

    bc->mWorkerRef = workerRef;
  }

  // Throw if this process wouldn't be allowed to access storage.
  if (!BackgroundChild::ValidatePrincipal(unpartitionedPrincipal, {})) {
    MOZ_ASSERT_UNREACHABLE(
        "ValidatePrincipal failure in UnpartitionedTestingChannel");
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // Register this component to PBackground.
  PBackgroundChild* actorChild = BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actorChild)) {
    // Firefox is probably shutting down. Let's return a 'generic' error.
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsAutoCString origin;
  aRv = unpartitionedPrincipal->GetOrigin(origin);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  nsString originForEvents;
  aRv = nsContentUtils::GetWebExposedOriginSerialization(unpartitionedPrincipal,
                                                         originForEvents);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  PBroadcastChannelChild* actor = actorChild->SendPBroadcastChannelConstructor(
      WrapNotNull(unpartitionedPrincipal.get()), origin, nsString(aChannel));
  if (!actor) {
    // The PBackground actor is shutting down, return a 'generic' error.
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  bc->mActor = static_cast<BroadcastChannelChild*>(actor);
  bc->mActor->SetParent(bc);
  bc->mOriginForEvents = std::move(originForEvents);

  return bc.forget();
}

/* static */
already_AddRefed<BroadcastChannel> BroadcastChannel::Constructor(
    const GlobalObject& aGlobal, const nsAString& aChannel, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsID portUUID = {};
  aRv = nsID::GenerateUUIDInPlace(portUUID);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<BroadcastChannel> bc =
      new BroadcastChannel(global, aChannel, portUUID);

  nsCOMPtr<nsIPrincipal> storagePrincipal;

  StorageAccess storageAccess;

  nsCOMPtr<nsICookieJarSettings> cjs;
  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(global);
    if (NS_WARN_IF(!window)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(global);
    if (NS_WARN_IF(!sop)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    storagePrincipal = sop->GetEffectiveStoragePrincipal();
    if (!storagePrincipal) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
    storageAccess = StorageAllowedForWindow(window);

    Document* doc = window->GetExtantDoc();
    if (doc) {
      cjs = doc->CookieJarSettings();
    }
  } else {
    JSContext* cx = aGlobal.Context();

    WorkerPrivate* workerPrivate = GetWorkerPrivateFromContext(cx);
    MOZ_ASSERT(workerPrivate);

    RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
        workerPrivate, "BroadcastChannel", [bc]() { bc->Shutdown(); });
    // We are already shutting down the worker. Let's return a non-active
    // object.
    if (NS_WARN_IF(!workerRef)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    storageAccess = workerPrivate->StorageAccess();

    storagePrincipal = workerPrivate->GetEffectiveStoragePrincipal();

    bc->mWorkerRef = workerRef;

    cjs = workerPrivate->CookieJarSettings();
  }

  // We want to allow opaque origins.
  if (!storagePrincipal->GetIsNullPrincipal() &&
      (storageAccess == StorageAccess::eDeny ||
       (ShouldPartitionStorage(storageAccess) &&
        !StoragePartitioningEnabled(storageAccess, cjs)))) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  // Throw if this process wouldn't be allowed to access storage.
  if (!BackgroundChild::ValidatePrincipal(storagePrincipal, {})) {
    MOZ_ASSERT_UNREACHABLE(
        "ValidatePrincipal failure in UnpartitionedTestingChannel");
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // Register this component to PBackground.
  PBackgroundChild* actorChild = BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actorChild)) {
    // Firefox is probably shutting down. Let's return a 'generic' error.
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsAutoCString origin;
  aRv = storagePrincipal->GetOrigin(origin);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  nsString originForEvents;
  aRv = nsContentUtils::GetWebExposedOriginSerialization(storagePrincipal,
                                                         originForEvents);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  PBroadcastChannelChild* actor = actorChild->SendPBroadcastChannelConstructor(
      WrapNotNull(storagePrincipal.get()), origin, nsString(aChannel));
  if (!actor) {
    // The PBackground actor is shutting down, return a 'generic' error.
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  bc->mActor = static_cast<BroadcastChannelChild*>(actor);
  bc->mActor->SetParent(bc);
  bc->mOriginForEvents = std::move(originForEvents);

  return bc.forget();
}

void BroadcastChannel::PostMessage(JSContext* aCx,
                                   JS::Handle<JS::Value> aMessage,
                                   ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  if (!global || !global->IsEligibleForMessaging()) {
    return;
  }

  if (mState != StateActive) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  Maybe<nsID> agentClusterId = global->GetAgentClusterId();

  auto data = MakeNotNull<RefPtr<SharedMessageBody>>(
      StructuredCloneHolder::TransferringNotSupported, agentClusterId);

  data->Write(aCx, aMessage, JS::UndefinedHandleValue, mPortUUID,
              mRefMessageBodyService, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  RemoveDocFromBFCache();

  mActor->SendPostMessage(data);
}

void BroadcastChannel::Close() {
  if (mState != StateActive) {
    return;
  }

  // We cannot call Shutdown() immediatelly because we could have some
  // postMessage runnable already dispatched. Instead, we change the state to
  // StateClosed and we shutdown the actor asynchrounsly.

  mState = StateClosed;
  RefPtr<CloseRunnable> runnable = new CloseRunnable(this);

  if (NS_FAILED(NS_DispatchToCurrentThread(runnable))) {
    NS_WARNING("Failed to dispatch to the current thread!");
  }
}

void BroadcastChannel::Shutdown() {
  mState = StateClosed;

  // The DTOR of this WorkerRef will release the worker for us.
  mWorkerRef = nullptr;

  if (mActor) {
    mActor->SetParent(nullptr);

    if (NS_IsMainThread()) {
      RefPtr<TeardownRunnableOnMainThread> runnable =
          new TeardownRunnableOnMainThread(mActor);
      NS_DispatchToCurrentThread(runnable);
    } else {
      WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
      MOZ_ASSERT(workerPrivate);

      RefPtr<TeardownRunnableOnWorker> runnable =
          new TeardownRunnableOnWorker(workerPrivate, mActor);
      runnable->Dispatch(workerPrivate);
    }

    mActor = nullptr;
  }

  IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onmessage);
}

void BroadcastChannel::RemoveDocFromBFCache() {
  if (!NS_IsMainThread()) {
    return;
  }

  if (nsPIDOMWindowInner* window = GetOwnerWindow()) {
    window->RemoveFromBFCacheSync();
  }
}

void BroadcastChannel::DisconnectFromOwner() {
  Shutdown();
  DOMEventTargetHelper::DisconnectFromOwner();
}

void BroadcastChannel::MessageReceived(SharedMessageBody* aData) {
  if (NS_FAILED(CheckCurrentGlobalCorrectness())) {
    RemoveDocFromBFCache();
    return;
  }

  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  if (!global || !global->IsEligibleForMessaging()) {
    return;
  }

  // Let's ignore messages after a close/shutdown.
  if (mState != StateActive) {
    return;
  }

  nsCOMPtr<nsIGlobalObject> globalObject;

  if (NS_IsMainThread()) {
    globalObject = GetParentObject();
  } else {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    globalObject = workerPrivate->GlobalScope();
  }

  AutoJSAPI jsapi;
  if (!globalObject || !jsapi.Init(globalObject)) {
    NS_WARNING("Failed to initialize AutoJSAPI object.");
    return;
  }

  JSContext* cx = jsapi.cx();

  IgnoredErrorResult rv;
  JS::Rooted<JS::Value> value(cx);

  aData->Read(cx, &value, mRefMessageBodyService,
              SharedMessageBody::ReadMethod::KeepRefMessageBody, rv);
  if (NS_WARN_IF(rv.Failed())) {
    JS_ClearPendingException(cx);
    DispatchError(cx);
    return;
  }

  RemoveDocFromBFCache();

  RootedDictionary<MessageEventInit> init(cx);
  init.mBubbles = false;
  init.mCancelable = false;
  init.mOrigin = mOriginForEvents;
  init.mData = value;

  RefPtr<MessageEvent> event =
      MessageEvent::Constructor(this, u"message"_ns, init);

  event->SetTrusted(true);

  DispatchEvent(*event);
}

void BroadcastChannel::MessageDelivered(const nsID& aMessageID,
                                        uint32_t aOtherBCs) {
  mRefMessageBodyService->SetMaxCount(aMessageID, aOtherBCs);
}

void BroadcastChannel::DispatchError(JSContext* aCx) {
  RootedDictionary<MessageEventInit> init(aCx);
  init.mBubbles = false;
  init.mCancelable = false;
  init.mOrigin = mOriginForEvents;

  RefPtr<Event> event =
      MessageEvent::Constructor(this, u"messageerror"_ns, init);
  event->SetTrusted(true);

  DispatchEvent(*event);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(BroadcastChannel)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BroadcastChannel,
                                                  DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BroadcastChannel,
                                                DOMEventTargetHelper)
  tmp->Shutdown();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BroadcastChannel)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BroadcastChannel, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BroadcastChannel, DOMEventTargetHelper)

}  // namespace dom
}  // namespace mozilla
