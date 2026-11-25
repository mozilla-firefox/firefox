/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIGlobalObject.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/GlobalFreezeObserver.h"
#include "mozilla/GlobalTeardownObserver.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/FunctionBinding.h"
#include "mozilla/dom/Report.h"
#include "mozilla/dom/ReportDeliver.h"
#include "mozilla/dom/ReportingObserver.h"
#include "mozilla/dom/ServiceWorker.h"
#include "mozilla/dom/ServiceWorkerContainer.h"
#include "mozilla/dom/ServiceWorkerRegistration.h"
#include "mozilla/dom/WebTaskScheduler.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsThreadUtils.h"

using mozilla::AutoSlowOperation;
using mozilla::CycleCollectedJSContext;
using mozilla::DOMEventTargetHelper;
using mozilla::ErrorResult;
using mozilla::GlobalFreezeObserver;
using mozilla::GlobalTeardownObserver;
using mozilla::IgnoredErrorResult;
using mozilla::MallocSizeOf;
using mozilla::Maybe;
using mozilla::MicroTaskRunnable;
using mozilla::dom::BlobURLProtocolHandler;
using mozilla::dom::CallerType;
using mozilla::dom::ClientInfo;
using mozilla::dom::ClientState;
using mozilla::dom::Report;
using mozilla::dom::ReportingObserver;
using mozilla::dom::ServiceWorker;
using mozilla::dom::ServiceWorkerContainer;
using mozilla::dom::ServiceWorkerDescriptor;
using mozilla::dom::ServiceWorkerRegistration;
using mozilla::dom::ServiceWorkerRegistrationDescriptor;
using mozilla::dom::VoidFunction;

nsIGlobalObject::nsIGlobalObject() : mIsDying(false), mIsInnerWindow(false) {}

bool nsIGlobalObject::IsScriptForbidden(JSObject* aCallback,
                                        bool aIsJSImplementedWebIDL) const {
  if (mIsDying) {
    return true;
  }

  if (NS_IsMainThread()) {
    if (aIsJSImplementedWebIDL) {
      return false;
    }

    if (!xpc::Scriptability::AllowedIfExists(aCallback)) {
      return true;
    }
  }

  return false;
}

bool nsIGlobalObject::CanRunJSMicroTask(JSObject* aCallbackGlobal) const {
  auto* principal = PrincipalOrNull();
  if (principal && principal->IsSystemPrincipal()) {
    return !IsScriptForbidden(aCallbackGlobal, false);
  }

  if (NS_IsMainThread()) {
    return xpc::Scriptability::AllowedIfExists(aCallbackGlobal);
  }

  // For Workers continue skipping tasks when the worker is dying.
  return !mIsDying;
}

nsIGlobalObject::~nsIGlobalObject() {
  UnlinkObjectsInGlobal();
  DisconnectGlobalTeardownObservers();
  DisconnectGlobalFreezeObservers();
  MOZ_DIAGNOSTIC_ASSERT(mGlobalTeardownObservers.isEmpty());
  MOZ_DIAGNOSTIC_ASSERT(mGlobalFreezeObservers.isEmpty());
}

nsIPrincipal* nsIGlobalObject::PrincipalOrNull() const {
  JSObject* global = GetGlobalJSObjectPreserveColor();
  if (NS_WARN_IF(!global)) return nullptr;

  return nsContentUtils::ObjectPrincipal(global);
}

void nsIGlobalObject::RegisterHostObjectURI(const nsACString& aURI) {
  MOZ_ASSERT(!mHostObjectURIs.Contains(aURI));
  mHostObjectURIs.AppendElement(aURI);
}

void nsIGlobalObject::UnregisterHostObjectURI(const nsACString& aURI) {
  mHostObjectURIs.RemoveElement(aURI);
}

namespace {

class UnlinkHostObjectURIsRunnable final : public mozilla::Runnable {
 public:
  explicit UnlinkHostObjectURIsRunnable(nsTArray<nsCString>&& aURIs)
      : mozilla::Runnable("UnlinkHostObjectURIsRunnable"),
        mURIs(std::move(aURIs)) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    BlobURLProtocolHandler::RemoveDataEntries(mURIs);
    return NS_OK;
  }

 private:
  ~UnlinkHostObjectURIsRunnable() = default;

  const nsTArray<nsCString> mURIs;
};

}  // namespace

void nsIGlobalObject::UnlinkObjectsInGlobal() {
  if (!mHostObjectURIs.IsEmpty()) {
    // BlobURLProtocolHandler is main-thread only.
    if (NS_IsMainThread()) {
      BlobURLProtocolHandler::RemoveDataEntries(mHostObjectURIs);
      mHostObjectURIs.Clear();
    } else {
      RefPtr<UnlinkHostObjectURIsRunnable> runnable =
          new UnlinkHostObjectURIsRunnable(std::move(mHostObjectURIs));
      MOZ_ASSERT(mHostObjectURIs.IsEmpty());

      nsresult rv = NS_DispatchToMainThread(runnable);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to dispatch a runnable to the main-thread.");
      }
    }
  }

  // Will queue a task if not on main thread, as should be the case for workers.
  mozilla::dom::ReportDeliver::RemoveGlobalEndpoints(
      reinterpret_cast<uintptr_t>(this));

  ClearReports();
  mReportingObservers.Clear();
  mCountQueuingStrategySizeFunction = nullptr;
  mByteLengthQueuingStrategySizeFunction = nullptr;
  SetWebTaskSchedulingState(nullptr);
}

void nsIGlobalObject::TraverseObjectsInGlobal(
    nsCycleCollectionTraversalCallback& cb) {
  nsIGlobalObject* tmp = this;
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReportBuffer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReportingObservers)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCountQueuingStrategySizeFunction)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mByteLengthQueuingStrategySizeFunction)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWebTaskSchedulingState)
}

void nsIGlobalObject::SetWebTaskSchedulingState(
    mozilla::dom::WebTaskSchedulingState* aState) {
  // Keep CycleCollectedJSContext count of the globals that have a
  // WebTaskSchedulingState in sync. The context is null at thread shutdown, in
  // which case the count is going away with it anyway.
  if (!!mWebTaskSchedulingState != !!aState) {
    if (CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get()) {
      aState ? ccjs->NoteWebTaskSchedulingStateAdded()
             : ccjs->NoteWebTaskSchedulingStateRemoved();
    }
  }
  mWebTaskSchedulingState = aState;
}

void nsIGlobalObject::AddGlobalTeardownObserver(
    GlobalTeardownObserver* aObject) {
  MOZ_DIAGNOSTIC_ASSERT(aObject);
  MOZ_ASSERT(!aObject->isInList());
  mGlobalTeardownObservers.insertBack(aObject);
}

void nsIGlobalObject::RemoveGlobalTeardownObserver(
    GlobalTeardownObserver* aObject) {
  MOZ_DIAGNOSTIC_ASSERT(aObject);
  MOZ_ASSERT(aObject->isInList());
  MOZ_ASSERT(aObject->GetRelevantGlobal() == this);
  aObject->remove();
}

void nsIGlobalObject::AddGlobalFreezeObserver(GlobalFreezeObserver* aObserver) {
  MOZ_DIAGNOSTIC_ASSERT(aObserver);
  MOZ_ASSERT(!aObserver->isInList());
  mGlobalFreezeObservers.insertBack(aObserver);
}

void nsIGlobalObject::RemoveGlobalFreezeObserver(
    GlobalFreezeObserver* aObserver) {
  MOZ_DIAGNOSTIC_ASSERT(aObserver);
  MOZ_ASSERT(aObserver->isInList());
  aObserver->remove();
}

void nsIGlobalObject::ForEachGlobalTeardownObserver(
    const std::function<void(GlobalTeardownObserver*, bool* aDoneOut)>& aFunc)
    const {
  // Protect against the function call triggering a mutation of the list
  // while we are iterating by copying the observer references to a temporary
  // list.
  AutoTArray<RefPtr<GlobalTeardownObserver>, 64> targetList;
  for (const GlobalTeardownObserver* observer : mGlobalTeardownObservers) {
    targetList.AppendElement(const_cast<GlobalTeardownObserver*>(observer));
  }

  // Iterate the target list and call the function on each one.
  bool done = false;
  for (auto& target : targetList) {
    // Check to see if a previous iteration's callback triggered the removal
    // of this target as a side-effect.  If it did, then just ignore it.
    if (target->GetRelevantGlobal() != this) {
      continue;
    }
    aFunc(target, &done);
    if (done) {
      break;
    }
  }
}

void nsIGlobalObject::DisconnectGlobalTeardownObservers() {
  ForEachGlobalTeardownObserver(
      [&](GlobalTeardownObserver* aTarget, bool* aDoneOut) {
        aTarget->DisconnectFromOwner();

        // Calling DisconnectFromOwner() should result in
        // RemoveGlobalTeardownObserver() being called.
        MOZ_DIAGNOSTIC_ASSERT(aTarget->GetRelevantGlobal() != this);
      });
}

void nsIGlobalObject::ForEachGlobalFreezeObserver(
    const std::function<void(GlobalFreezeObserver*, bool* aDoneOut)>& aFunc)
    const {
  // Protect against the function call triggering a mutation of the list
  // while we are iterating by copying the observer references to a temporary
  // list.
  AutoTArray<RefPtr<GlobalFreezeObserver>, 64> targetList;
  for (const GlobalFreezeObserver* observer : mGlobalFreezeObservers) {
    targetList.AppendElement(const_cast<GlobalFreezeObserver*>(observer));
  }

  // Iterate the target list and call the function on each one.
  bool done = false;
  for (auto& target : targetList) {
    // Check to see if a previous iteration's callback triggered the removal
    // of this target as a side-effect.  If it did, then just ignore it.
    if (!target->Observing()) {
      continue;
    }
    aFunc(target, &done);
    if (done) {
      break;
    }
  }
}

void nsIGlobalObject::DisconnectGlobalFreezeObservers() {
  ForEachGlobalFreezeObserver(
      [&](GlobalFreezeObserver* aTarget, bool* aDoneOut) {
        aTarget->DisconnectFreezeObserver();
      });
}

void nsIGlobalObject::NotifyGlobalFrozen() {
  ForEachGlobalFreezeObserver(
      [&](GlobalFreezeObserver* aTarget, bool* aDoneOut) {
        aTarget->FrozenCallback(this);
      });
}

void nsIGlobalObject::NotifyGlobalThawed() {
  ForEachGlobalFreezeObserver(
      [&](GlobalFreezeObserver* aTarget, bool* aDoneOut) {
        aTarget->ThawedCallback(this);
      });
}

nsIURI* nsIGlobalObject::GetBaseURI() const { return nullptr; }

Maybe<ClientInfo> nsIGlobalObject::GetClientInfo() const {
  // By default globals do not expose themselves as a client.  Only real
  // window and worker globals are currently considered clients.
  return Maybe<ClientInfo>();
}

Maybe<ClientState> nsIGlobalObject::GetClientState() const {
  // By default globals do not expose themselves as a client.  Only real
  // window and worker globals are currently considered clients.
  return Maybe<ClientState>();
}

Maybe<nsID> nsIGlobalObject::GetAgentClusterId() const {
  Maybe<ClientInfo> ci = GetClientInfo();
  if (ci.isSome()) {
    return ci.value().AgentClusterId();
  }
  return mozilla::Nothing();
}

Maybe<ServiceWorkerDescriptor> nsIGlobalObject::GetController() const {
  // By default globals do not have a service worker controller.  Only real
  // window and worker globals can currently be controlled as a client.
  return Maybe<ServiceWorkerDescriptor>();
}

already_AddRefed<ServiceWorkerContainer>
nsIGlobalObject::GetServiceWorkerContainer() {
  return nullptr;
}

RefPtr<ServiceWorker> nsIGlobalObject::GetOrCreateServiceWorker(
    const ServiceWorkerDescriptor& aDescriptor) {
  MOZ_DIAGNOSTIC_CRASH("this global should not have any service workers");
  return nullptr;
}

RefPtr<ServiceWorkerRegistration> nsIGlobalObject::GetServiceWorkerRegistration(
    const mozilla::dom::ServiceWorkerRegistrationDescriptor& aDescriptor)
    const {
  MOZ_DIAGNOSTIC_CRASH("this global should not have any service workers");
  return nullptr;
}

RefPtr<ServiceWorkerRegistration>
nsIGlobalObject::GetOrCreateServiceWorkerRegistration(
    const ServiceWorkerRegistrationDescriptor& aDescriptor) {
  MOZ_DIAGNOSTIC_ASSERT(
      false, "this global should not have any service worker registrations");
  return nullptr;
}

mozilla::StorageAccess nsIGlobalObject::GetStorageAccess() {
  return mozilla::StorageAccess::eDeny;
}

nsICookieJarSettings* nsIGlobalObject::GetCookieJarSettings() {
  return nullptr;
}

nsPIDOMWindowInner* nsIGlobalObject::GetAsInnerWindow() {
  if (MOZ_LIKELY(mIsInnerWindow)) {
    return static_cast<nsPIDOMWindowInner*>(
        static_cast<nsGlobalWindowInner*>(this));
  }
  return nullptr;
}

size_t nsIGlobalObject::ShallowSizeOfExcludingThis(MallocSizeOf aSizeOf) const {
  size_t rtn = mHostObjectURIs.ShallowSizeOfExcludingThis(aSizeOf);
  return rtn;
}

class QueuedMicrotask : public MicroTaskRunnable {
 public:
  QueuedMicrotask(nsIGlobalObject* aGlobal, VoidFunction& aCallback)
      : mGlobal(aGlobal), mCallback(&aCallback) {}

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Run(AutoSlowOperation& aAso) final {
    IgnoredErrorResult rv;
    MOZ_KnownLive(mCallback)->Call(static_cast<ErrorResult&>(rv));
  }

  bool Suppressed() final { return mGlobal->IsInSyncOperation(); }

 private:
  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<VoidFunction> mCallback;
};

void nsIGlobalObject::QueueMicrotask(VoidFunction& aCallback) {
  CycleCollectedJSContext* context = CycleCollectedJSContext::Get();
  if (context) {
    RefPtr<MicroTaskRunnable> mt = new QueuedMicrotask(this, aCallback);
    context->DispatchToMicroTask(mt.forget());
  }
}

void nsIGlobalObject::RegisterReportingObserver(ReportingObserver* aObserver,
                                                bool aBuffered) {
  MOZ_ASSERT(aObserver);

  if (mReportingObservers.Contains(aObserver)) {
    return;
  }

  if (NS_WARN_IF(
          !mReportingObservers.AppendElement(aObserver, mozilla::fallible))) {
    return;
  }

  if (!aBuffered) {
    return;
  }

  for (const auto& report : mReportBuffer) {
    aObserver->MaybeReport(report);
  }
}

void nsIGlobalObject::UnregisterReportingObserver(
    ReportingObserver* aObserver) {
  MOZ_ASSERT(aObserver);
  mReportingObservers.RemoveElement(aObserver);
}

void nsIGlobalObject::BroadcastReport(Report* aReport) {
  MOZ_ASSERT(aReport);

  for (ReportingObserver* observer : mReportingObservers) {
    observer->MaybeReport(aReport);
  }

  if (NS_WARN_IF(!mReportBuffer.AppendElement(aReport, mozilla::fallible))) {
    return;
  }

  uint32_t& count = mReportPerTypeCount.LookupOrInsert(aReport->Type());
  ++count;

  const uint32_t maxReportCount =
      mozilla::StaticPrefs::dom_reporting_delivering_maxReports();
  nsAtom* reportType = aReport->Type();

  for (size_t i = 0u; count > maxReportCount && i < mReportBuffer.Length();) {
    if (mReportBuffer[i]->Type() == reportType) {
      mReportBuffer.RemoveElementAt(i);
      --count;
    } else {
      ++i;
    }
  }
}

void nsIGlobalObject::NotifyReportingObservers() {
  for (auto& observer : mReportingObservers.Clone()) {
    // MOZ_KnownLive because the clone of 'mReportingObservers' is guaranteed to
    // keep it alive.
    //
    // This can go away once
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1620312 is fixed.
    MOZ_KnownLive(observer)->MaybeNotify();
  }
}

void nsIGlobalObject::RemoveReportRecords() {
  ClearReports();

  for (auto& observer : mReportingObservers) {
    observer->ForgetReports();
  }
}

already_AddRefed<mozilla::dom::Function>
nsIGlobalObject::GetCountQueuingStrategySizeFunction() {
  return do_AddRef(mCountQueuingStrategySizeFunction);
}

void nsIGlobalObject::SetCountQueuingStrategySizeFunction(
    mozilla::dom::Function* aFunction) {
  mCountQueuingStrategySizeFunction = aFunction;
}

already_AddRefed<mozilla::dom::Function>
nsIGlobalObject::GetByteLengthQueuingStrategySizeFunction() {
  return do_AddRef(mByteLengthQueuingStrategySizeFunction);
}

void nsIGlobalObject::SetByteLengthQueuingStrategySizeFunction(
    mozilla::dom::Function* aFunction) {
  mByteLengthQueuingStrategySizeFunction = aFunction;
}

mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult>
nsIGlobalObject::GetStorageKey() {
  return mozilla::Err(NS_ERROR_NOT_AVAILABLE);
}

mozilla::Result<bool, nsresult> nsIGlobalObject::HasEqualStorageKey(
    const mozilla::ipc::PrincipalInfo& aStorageKey) {
  auto result = GetStorageKey();
  if (result.isErr()) {
    return result.propagateErr();
  }

  const auto& storageKey = result.inspect();

  return mozilla::ipc::StorageKeysEqual(storageKey, aStorageKey);
}

mozilla::RTPCallerType nsIGlobalObject::GetRTPCallerType() const {
  if (PrincipalOrNull() && PrincipalOrNull()->IsSystemPrincipal()) {
    return RTPCallerType::SystemPrincipal;
  }

  if (ShouldResistFingerprinting(RFPTarget::ReduceTimerPrecision)) {
    return RTPCallerType::ResistFingerprinting;
  }

  if (CrossOriginIsolated()) {
    return RTPCallerType::CrossOriginIsolated;
  }

  return RTPCallerType::Normal;
}

bool nsIGlobalObject::ShouldResistFingerprinting(CallerType aCallerType,
                                                 RFPTarget aTarget) const {
  return aCallerType != CallerType::System &&
         ShouldResistFingerprinting(aTarget);
}

bool nsIGlobalObject::IsRFPTargetActive(const nsAString& aTargetName,
                                        mozilla::ErrorResult& aRv) {
  MOZ_ASSERT(mozilla::StaticPrefs::privacy_fingerprintingProtection_testing());

  Maybe<RFPTarget> target = mozilla::nsRFPService::TextToRFPTarget(aTargetName);
  if (NS_WARN_IF(!target)) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return false;
  }

  return ShouldResistFingerprinting(*target);
}

void nsIGlobalObject::ReportToConsole(
    uint32_t aErrorFlags, const nsCString& aCategory, PropertiesFile aFile,
    const nsCString& aMessageName, const nsTArray<nsString>& aParams,
    const mozilla::SourceLocation& aLocation) {
  // We pass nullptr for the document because nsGlobalWindowInner handles the
  // case where it should be non-null.  We also expect the worker impl to
  // override.
  nsContentUtils::ReportToConsole(aErrorFlags, aCategory, nullptr, aFile,
                                  aMessageName.get(), aParams, aLocation);
}

void nsIGlobalObject::ClearReports() {
  mReportBuffer.Clear();
  mReportPerTypeCount.Clear();
}
