/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WorkerScope.h"

#include <stdio.h>

#include <new>
#include <utility>

#include "Crypto.h"
#include "GeckoProfiler.h"
#include "MainThreadUtils.h"
#include "ScriptLoader.h"
#include "js/CompilationAndEvaluation.h"
#include "js/CompileOptions.h"
#include "js/RealmOptions.h"
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/Value.h"
#include "js/Wrapper.h"
#include "js/loader/ModuleLoaderBase.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/BaseProfilerMarkersPrerequisites.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/CSPEvalChecker.h"
#include "mozilla/dom/CallbackDebuggerNotification.h"
#include "mozilla/dom/ClientSource.h"
#include "mozilla/dom/Clients.h"
#include "mozilla/dom/Console.h"
#include "mozilla/dom/CookieStore.h"
#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/DOMString.h"
#include "mozilla/dom/DebuggerNotification.h"
#include "mozilla/dom/DebuggerNotificationBinding.h"
#include "mozilla/dom/DebuggerNotificationManager.h"
#include "mozilla/dom/DedicatedWorkerGlobalScopeBinding.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/IDBFactory.h"
#include "mozilla/dom/ImageBitmap.h"
#include "mozilla/dom/ImageBitmapSource.h"
#include "mozilla/dom/MessagePortBinding.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseWorkerProxy.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SerializedStackHolder.h"
#include "mozilla/dom/ServiceWorker.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/dom/ServiceWorkerGlobalScopeBinding.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/ServiceWorkerRegistration.h"
#include "mozilla/dom/ServiceWorkerRegistrationDescriptor.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/SharedWorkerGlobalScopeBinding.h"
#include "mozilla/dom/SimpleGlobalObject.h"
#include "mozilla/dom/TestUtils.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/TimeoutManager.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/dom/VsyncWorkerChild.h"
#include "mozilla/dom/WebTaskSchedulerWorker.h"
#include "mozilla/dom/WindowOrWorkerGlobalScopeBinding.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerDebuggerGlobalScopeBinding.h"
#include "mozilla/dom/WorkerDocumentListener.h"
#include "mozilla/dom/WorkerGlobalScopeBinding.h"
#include "mozilla/dom/WorkerLocation.h"
#include "mozilla/dom/WorkerNavigator.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/cache/CacheStorage.h"
#include "mozilla/dom/cache/Types.h"
#include "mozilla/extensions/ExtensionBrowser.h"
#include "mozilla/fallible.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsAtom.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsGkAtoms.h"
#include "nsIEventTarget.h"
#include "nsIGlobalObject.h"
#include "nsIScriptError.h"
#include "nsISerialEventTarget.h"
#include "nsIWeakReference.h"
#include "nsJSUtils.h"
#include "nsLiteralString.h"
#include "nsQueryObject.h"
#include "nsRFPService.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTLiteralString.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"
#include "nsWrapperCacheInlines.h"
#include "nscore.h"
#include "xpcpublic.h"

#ifdef ANDROID
#  include <android/log.h>
#endif

#ifdef XP_WIN
#  undef PostMessage
#endif

using mozilla::dom::cache::CacheStorage;
using mozilla::dom::workerinternals::NamedWorkerGlobalScopeMixin;
using mozilla::ipc::BackgroundChild;
using mozilla::ipc::PBackgroundChild;
using mozilla::ipc::PrincipalInfo;

namespace mozilla::dom {

static mozilla::LazyLogModule sWorkerScopeLog("WorkerScope");

#ifdef LOG
#  undef LOG
#endif
#define LOG(args) MOZ_LOG(sWorkerScopeLog, LogLevel::Debug, args);

class WorkerScriptTimeoutHandler final : public ScriptTimeoutHandler {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(WorkerScriptTimeoutHandler,
                                           ScriptTimeoutHandler)

  WorkerScriptTimeoutHandler(JSContext* aCx, nsIGlobalObject* aGlobal,
                             const nsAString& aExpression)
      : ScriptTimeoutHandler(aCx, aGlobal, aExpression) {}

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* aExecutionReason) override;

 private:
  virtual ~WorkerScriptTimeoutHandler() = default;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(WorkerScriptTimeoutHandler,
                                   ScriptTimeoutHandler)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WorkerScriptTimeoutHandler)
NS_INTERFACE_MAP_END_INHERITING(ScriptTimeoutHandler)

NS_IMPL_ADDREF_INHERITED(WorkerScriptTimeoutHandler, ScriptTimeoutHandler)
NS_IMPL_RELEASE_INHERITED(WorkerScriptTimeoutHandler, ScriptTimeoutHandler)

bool WorkerScriptTimeoutHandler::Call(const char* aExecutionReason) {
  nsAutoMicroTask mt;
  AutoEntryScript aes(mGlobal, aExecutionReason, false);

  JSContext* cx = aes.cx();
  JS::CompileOptions options(cx);
  options.setFileAndLine(mCaller.FileName().get(), mCaller.mLine)
      .setNoScriptRval(true);
  options.setIntroductionType("domTimer");

  JS::Rooted<JS::Value> unused(cx);
  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.init(cx, mExpr.BeginReading(), mExpr.Length(),
                   JS::SourceOwnership::Borrowed) ||
      !JS::Evaluate(cx, options, srcBuf, &unused)) {
    if (!JS_IsExceptionPending(cx)) {
      return false;
    }
  }

  return true;
};

namespace workerinternals {
void NamedWorkerGlobalScopeMixin::GetName(DOMString& aName) const {
  aName.SetKnownLiveString(mName);
}
static const char* GetTimeoutReasonString(Timeout* aTimeout) {
  switch (aTimeout->mReason) {
    case Timeout::Reason::eTimeoutOrInterval:
      if (aTimeout->mIsInterval) {
        return "setInterval handler";
      }
      return "setTimeout handler";
    case Timeout::Reason::eIdleCallbackTimeout:
      return "setIdleCallback handler (timed out)";
    case Timeout::Reason::eAbortSignalTimeout:
      return "AbortSignal timeout";
    case Timeout::Reason::eDelayedWebTaskTimeout:
      return "delayedWebTaskCallback handler (timed out)";
    case Timeout::Reason::eJSTimeout:
      return "JS timeout";
  }
  MOZ_CRASH("Unexpected enum value");
  return "";
}
}  // namespace workerinternals

NS_IMPL_CYCLE_COLLECTION_CLASS(WorkerGlobalScopeBase)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(WorkerGlobalScopeBase,
                                                  DOMEventTargetHelper)
  tmp->AssertIsOnWorkerThread();
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mConsole)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mModuleLoader)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSerialEventTarget)
  tmp->TraverseObjectsInGlobal(cb);
  // If we already exited WorkerThreadPrimaryRunnable, we will find it
  // nullptr and there is nothing left to do here on the WorkerPrivate,
  // in particular the timeouts have already been canceled and unlinked.
  if (tmp->mWorkerPrivate) {
    tmp->mWorkerPrivate->TraverseTimeouts(cb);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(WorkerGlobalScopeBase,
                                                DOMEventTargetHelper)
  tmp->AssertIsOnWorkerThread();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mConsole)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mModuleLoader)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSerialEventTarget)
  tmp->UnlinkObjectsInGlobal();
  // If we already exited WorkerThreadPrimaryRunnable, we will find it
  // nullptr and there is nothing left to do here on the WorkerPrivate,
  // in particular the timeouts have already been canceled and unlinked.
  if (tmp->mWorkerPrivate) {
    tmp->mWorkerPrivate->UnlinkTimeouts();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(WorkerGlobalScopeBase,
                                               DOMEventTargetHelper)
  tmp->AssertIsOnWorkerThread();
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(WorkerGlobalScopeBase, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(WorkerGlobalScopeBase, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WorkerGlobalScopeBase)
  NS_INTERFACE_MAP_ENTRY(nsIGlobalObject)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

WorkerGlobalScopeBase::WorkerGlobalScopeBase(
    WorkerPrivate* aWorkerPrivate, UniquePtr<ClientSource> aClientSource)
    : mWorkerPrivate(aWorkerPrivate),
      mClientSource(std::move(aClientSource)),
      mSerialEventTarget(aWorkerPrivate->HybridEventTarget()) {
  mTimeoutManager = std::make_unique<dom::TimeoutManager>(
      *this, /* not used on workers */ 0, mSerialEventTarget,
      mWorkerPrivate->IsChromeWorker());
  LOG(("WorkerGlobalScopeBase::WorkerGlobalScopeBase [%p]", this));
  MOZ_ASSERT(mWorkerPrivate);
#ifdef DEBUG
  mWorkerPrivate->AssertIsOnWorkerThread();
  mWorkerThreadUsedOnlyForAssert = PR_GetCurrentThread();
#endif
  MOZ_ASSERT(mClientSource);

  MOZ_DIAGNOSTIC_ASSERT(
      mSerialEventTarget,
      "There should be an event target when a worker global is created.");

  // In workers, each DETH must have an owner. Because the global scope doesn't
  // have one, let's set it as owner of itself.
  BindToGlobal(static_cast<nsIGlobalObject*>(this));
}

WorkerGlobalScopeBase::~WorkerGlobalScopeBase() = default;

void WorkerGlobalScopeBase::InitModuleLoader(
    JS::loader::ModuleLoaderBase* aModuleLoader) {
  if (!mModuleLoader) {
    mModuleLoader = aModuleLoader;
  }
}

JSObject* WorkerGlobalScopeBase::GetGlobalJSObject() {
  AssertIsOnWorkerThread();
  return GetWrapper();
}

bool WorkerGlobalScopeBase::RunTimeoutHandler(mozilla::dom::Timeout* aTimeout) {
  // this is almost a copy of nsGlobalWindowInner::RunTimeoutHandler

  // Hold on to the timeout in case mExpr or mFunObj releases its
  // doc.
  // XXXbz Our caller guarantees it'll hold on to the timeout (because
  // we're MOZ_CAN_RUN_SCRIPT), so we can probably stop doing that...
  RefPtr<Timeout> timeout = aTimeout;
  Timeout* last_running_timeout = mTimeoutManager->BeginRunningTimeout(timeout);
  timeout->mRunning = true;

  uint32_t nestingLevel = mTimeoutManager->GetNestingLevelForWorker();
  mTimeoutManager->SetNestingLevelForWorker(timeout->mNestingLevel);

  const char* reason = workerinternals::GetTimeoutReasonString(timeout);

  bool abortIntervalHandler;
  {
    RefPtr<TimeoutHandler> handler(timeout->mScriptHandler);

    CallbackDebuggerNotificationGuard guard(
        this, timeout->mIsInterval
                  ? DebuggerNotificationType::SetIntervalCallback
                  : DebuggerNotificationType::SetTimeoutCallback);
    abortIntervalHandler = !handler->Call(reason);
  }

  // If we received an uncatchable exception, do not schedule the timeout again.
  // This allows the slow script dialog to break easy DoS attacks like
  // setInterval(function() { while(1); }, 100);
  if (abortIntervalHandler) {
    // If it wasn't an interval timer to begin with, this does nothing.  If it
    // was, we'll treat it as a timeout that we just ran and discard it when
    // we return.
    timeout->mIsInterval = false;
  }

  // We ignore any failures from calling EvaluateString() on the context or
  // Call() on a Function here since we're in a loop
  // where we're likely to be running timeouts whose OS timers
  // didn't fire in time and we don't want to not fire those timers
  // now just because execution of one timer failed. We can't
  // propagate the error to anyone who cares about it from this
  // point anyway, and the script context should have already reported
  // the script error in the usual way - so we just drop it.

  mTimeoutManager->SetNestingLevelForWorker(nestingLevel);

  mTimeoutManager->EndRunningTimeout(last_running_timeout);
  timeout->mRunning = false;

  return timeout->mCleared;
}

JSObject* WorkerGlobalScopeBase::GetGlobalJSObjectPreserveColor() const {
  AssertIsOnWorkerThread();
  return GetWrapperPreserveColor();
}

bool WorkerGlobalScopeBase::IsSharedMemoryAllowed() const {
  AssertIsOnWorkerThread();
  return mWorkerPrivate->IsSharedMemoryAllowed();
}

bool WorkerGlobalScopeBase::ShouldResistFingerprinting(
    RFPTarget aTarget) const {
  AssertIsOnWorkerThread();
  return mWorkerPrivate->ShouldResistFingerprinting(aTarget);
}

OriginTrials WorkerGlobalScopeBase::Trials() const {
  AssertIsOnWorkerThread();
  return mWorkerPrivate->Trials();
}

StorageAccess WorkerGlobalScopeBase::GetStorageAccess() {
  AssertIsOnWorkerThread();
  return mWorkerPrivate->StorageAccess();
}

nsICookieJarSettings* WorkerGlobalScopeBase::GetCookieJarSettings() {
  AssertIsOnWorkerThread();
  return mWorkerPrivate->CookieJarSettings();
}

nsIURI* WorkerGlobalScopeBase::GetBaseURI() const {
  return mWorkerPrivate->GetBaseURI();
}

Maybe<ClientInfo> WorkerGlobalScopeBase::GetClientInfo() const {
  return Some(mClientSource->Info());
}

Maybe<ClientState> WorkerGlobalScopeBase::GetClientState() const {
  Result<ClientState, ErrorResult> res = mClientSource->SnapshotState();
  if (res.isOk()) {
    return Some(res.unwrap());
  }

  res.unwrapErr().SuppressException();
  return Nothing();
}

Maybe<ServiceWorkerDescriptor> WorkerGlobalScopeBase::GetController() const {
  return mClientSource->GetController();
}

mozilla::Result<mozilla::ipc::PrincipalInfo, nsresult>
WorkerGlobalScopeBase::GetStorageKey() {
  AssertIsOnWorkerThread();

  const mozilla::ipc::PrincipalInfo& principalInfo =
      mWorkerPrivate->GetEffectiveStoragePrincipalInfo();

  // Block expanded and null principals, let content and system through.
  if (principalInfo.type() !=
          mozilla::ipc::PrincipalInfo::TContentPrincipalInfo &&
      principalInfo.type() !=
          mozilla::ipc::PrincipalInfo::TSystemPrincipalInfo) {
    return Err(NS_ERROR_DOM_SECURITY_ERR);
  }

  return principalInfo;
}

void WorkerGlobalScopeBase::Control(
    const ServiceWorkerDescriptor& aServiceWorker) {
  AssertIsOnWorkerThread();
  MOZ_DIAGNOSTIC_ASSERT(!mWorkerPrivate->IsChromeWorker());
  MOZ_DIAGNOSTIC_ASSERT(mWorkerPrivate->Kind() != WorkerKindService);

  if (IsBlobURI(mWorkerPrivate->GetBaseURI())) {
    // Blob URL workers can only become controlled by inheriting from
    // their parent.  Make sure to note this properly.
    mClientSource->InheritController(aServiceWorker);
  } else {
    // Otherwise this is a normal interception and we simply record the
    // controller locally.
    mClientSource->SetController(aServiceWorker);
  }
}

nsresult WorkerGlobalScopeBase::Dispatch(
    already_AddRefed<nsIRunnable> aRunnable) const {
  return SerialEventTarget()->Dispatch(std::move(aRunnable),
                                       NS_DISPATCH_NORMAL);
}

nsISerialEventTarget* WorkerGlobalScopeBase::SerialEventTarget() const {
  AssertIsOnWorkerThread();
  return mSerialEventTarget;
}

// See also AutoJSAPI::ReportException
void WorkerGlobalScopeBase::ReportError(JSContext* aCx,
                                        JS::Handle<JS::Value> aError,
                                        CallerType, ErrorResult& aRv) {
  JS::ErrorReportBuilder jsReport(aCx);
  JS::ExceptionStack exnStack(aCx, aError, nullptr);
  if (!jsReport.init(aCx, exnStack, JS::ErrorReportBuilder::NoSideEffects)) {
    return aRv.NoteJSContextException(aCx);
  }

  // Before invoking ReportError, put the exception back on the context,
  // because it may want to put it in its error events and has no other way
  // to get hold of it.  After we invoke ReportError, clear the exception on
  // cx(), just in case ReportError didn't.
  JS::SetPendingExceptionStack(aCx, exnStack);
  mWorkerPrivate->ReportError(aCx, jsReport.toStringResult(),
                              jsReport.report());
  JS_ClearPendingException(aCx);
}

void WorkerGlobalScopeBase::Atob(const nsAString& aAtob, nsAString& aOut,
                                 ErrorResult& aRv) const {
  AssertIsOnWorkerThread();
  aRv = nsContentUtils::Atob(aAtob, aOut);
}

void WorkerGlobalScopeBase::Btoa(const nsAString& aBtoa, nsAString& aOut,
                                 ErrorResult& aRv) const {
  AssertIsOnWorkerThread();
  aRv = nsContentUtils::Btoa(aBtoa, aOut);
}

already_AddRefed<Console> WorkerGlobalScopeBase::GetConsole(ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  if (!mConsole) {
    mConsole = Console::Create(mWorkerPrivate->GetJSContext(), nullptr, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  RefPtr<Console> console = mConsole;
  return console.forget();
}

uint64_t WorkerGlobalScopeBase::WindowID() const {
  return mWorkerPrivate->WindowID();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(WorkerGlobalScope)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(WorkerGlobalScope,
                                                  WorkerGlobalScopeBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCrypto)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPerformance)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWebTaskScheduler)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTrustedTypePolicyFactory)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLocation)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mNavigator)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFontFaceSet)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIndexedDB)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCacheStorage)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDebuggerNotificationManager)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(WorkerGlobalScope,
                                                WorkerGlobalScopeBase)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCrypto)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPerformance)
  if (tmp->mWebTaskScheduler) {
    tmp->mWebTaskScheduler->Disconnect();
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mWebTaskScheduler)
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTrustedTypePolicyFactory)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLocation)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mNavigator)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFontFaceSet)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIndexedDB)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCacheStorage)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDebuggerNotificationManager)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(WorkerGlobalScope,
                                               WorkerGlobalScopeBase)

WorkerGlobalScope::~WorkerGlobalScope() = default;

void WorkerGlobalScope::NoteTerminating() {
  LOG(("WorkerGlobalScope::NoteTerminating [%p]", this));
  if (IsDying()) {
    return;
  }

  StartDying();
}

void WorkerGlobalScope::NoteShuttingDown() {
  MOZ_ASSERT(IsDying());
  LOG(("WorkerGlobalScope::NoteShuttingDown [%p]", this));

  if (mNavigator) {
    mNavigator->Invalidate();
    mNavigator = nullptr;
  }
}

Crypto* WorkerGlobalScope::GetCrypto(ErrorResult& aError) {
  AssertIsOnWorkerThread();

  if (!mCrypto) {
    mCrypto = new Crypto(this);
  }

  return mCrypto;
}

already_AddRefed<CacheStorage> WorkerGlobalScope::GetCaches(ErrorResult& aRv) {
  if (!mCacheStorage) {
    mCacheStorage = CacheStorage::CreateOnWorker(cache::DEFAULT_NAMESPACE, this,
                                                 mWorkerPrivate, aRv);
  }

  RefPtr<CacheStorage> ref = mCacheStorage;
  return ref.forget();
}

bool WorkerGlobalScope::IsSecureContext() const {
  bool globalSecure = JS::GetIsSecureContext(
      js::GetNonCCWObjectRealm(GetWrapperPreserveColor()));
  MOZ_ASSERT(globalSecure == mWorkerPrivate->IsSecureContext());
  return globalSecure;
}

already_AddRefed<WorkerLocation> WorkerGlobalScope::Location() {
  AssertIsOnWorkerThread();

  if (!mLocation) {
    mLocation = WorkerLocation::Create(mWorkerPrivate->GetBaseURI());
    MOZ_ASSERT(mLocation);
  }

  RefPtr<WorkerLocation> location = mLocation;
  return location.forget();
}

already_AddRefed<WorkerNavigator> WorkerGlobalScope::Navigator() {
  AssertIsOnWorkerThread();

  if (!mNavigator) {
    bool onLine = mWorkerPrivate->OnLine();
    if (mWorkerPrivate->ShouldResistFingerprinting(
            RFPTarget::NetworkConnection)) {
      onLine = true;
    }
    mNavigator = WorkerNavigator::Create(onLine);
    MOZ_ASSERT(mNavigator);
  }

  RefPtr<WorkerNavigator> navigator = mNavigator;
  return navigator.forget();
}

already_AddRefed<WorkerNavigator> WorkerGlobalScope::GetExistingNavigator()
    const {
  AssertIsOnWorkerThread();

  RefPtr<WorkerNavigator> navigator = mNavigator;
  return navigator.forget();
}

FontFaceSet* WorkerGlobalScope::GetFonts(ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  if (!mFontFaceSet) {
    mFontFaceSet = FontFaceSet::CreateForWorker(this, mWorkerPrivate);
    if (MOZ_UNLIKELY(!mFontFaceSet)) {
      aRv.ThrowInvalidStateError("Couldn't acquire worker reference");
      return nullptr;
    }
  }

  return mFontFaceSet;
}

OnErrorEventHandlerNonNull* WorkerGlobalScope::GetOnerror() {
  AssertIsOnWorkerThread();

  EventListenerManager* elm = GetExistingListenerManager();
  return elm ? elm->GetOnErrorEventHandler() : nullptr;
}

void WorkerGlobalScope::SetOnerror(OnErrorEventHandlerNonNull* aHandler) {
  AssertIsOnWorkerThread();

  EventListenerManager* elm = GetOrCreateListenerManager();
  if (elm) {
    elm->SetEventHandler(aHandler);
  }
}

void WorkerGlobalScope::ImportScripts(
    JSContext* aCx, const Sequence<OwningTrustedScriptURLOrString>& aScriptURLs,
    nsIPrincipal* aSubjectPrincipal, ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  UniquePtr<SerializedStackHolder> stack;
  if (mWorkerPrivate->IsWatchedByDevTools()) {
    stack = GetCurrentStackForNetMonitor(aCx);
  }

  {
    nsTArray<nsString> scriptURLs;
    nsCOMPtr<nsIGlobalObject> pinnedGlobal = this;
    for (const auto& scriptURL : aScriptURLs) {
      constexpr nsLiteralString sink = u"WorkerGlobalScope importScripts"_ns;
      Maybe<nsAutoString> compliantStringHolder;
      const nsAString* compliantString =
          TrustedTypeUtils::GetTrustedTypesCompliantString(
              scriptURL, sink, kTrustedTypesOnlySinkGroup, *pinnedGlobal,
              aSubjectPrincipal, compliantStringHolder, aRv);
      if (aRv.Failed()) {
        return;
      }
      scriptURLs.AppendElement(*compliantString);
    }
    AUTO_PROFILER_MARKER_TEXT(
        "ImportScripts", JS, MarkerStack::Capture(),
        profiler_thread_is_being_profiled_for_markers()
            ? StringJoin(","_ns, scriptURLs,
                         [](nsACString& dest, const auto& scriptUrl) {
                           AppendUTF16toUTF8(
                               Substring(
                                   scriptUrl, 0,
                                   std::min(size_t(128), scriptUrl.Length())),
                               dest);
                         })
            : nsAutoCString{});
    workerinternals::Load(mWorkerPrivate, std::move(stack), scriptURLs,
                          WorkerScript, aRv);
  }
}

int32_t WorkerGlobalScope::SetTimeout(
    JSContext* aCx, const FunctionOrTrustedScriptOrString& aHandler,
    const int32_t aTimeout, const Sequence<JS::Value>& aArguments,
    nsIPrincipal* aSubjectPrincipal, ErrorResult& aRv) {
  return SetTimeoutOrInterval(aCx, aHandler, aTimeout, aArguments, false,
                              aSubjectPrincipal, aRv);
}

void WorkerGlobalScope::ClearTimeout(int32_t aHandle) {
  AssertIsOnWorkerThread();

  DebuggerNotificationDispatch(this, DebuggerNotificationType::ClearTimeout);

  mWorkerPrivate->ClearTimeout(aHandle, Timeout::Reason::eTimeoutOrInterval);
}

int32_t WorkerGlobalScope::SetInterval(
    JSContext* aCx, const FunctionOrTrustedScriptOrString& aHandler,
    const int32_t aTimeout, const Sequence<JS::Value>& aArguments,
    nsIPrincipal* aSubjectPrincipal, ErrorResult& aRv) {
  return SetTimeoutOrInterval(aCx, aHandler, aTimeout, aArguments, true,
                              aSubjectPrincipal, aRv);
}

void WorkerGlobalScope::ClearInterval(int32_t aHandle) {
  AssertIsOnWorkerThread();

  DebuggerNotificationDispatch(this, DebuggerNotificationType::ClearInterval);

  mWorkerPrivate->ClearTimeout(aHandle, Timeout::Reason::eTimeoutOrInterval);
}

int32_t WorkerGlobalScope::SetTimeoutOrInterval(
    JSContext* aCx, const FunctionOrTrustedScriptOrString& aHandler,
    const int32_t aTimeout, const Sequence<JS::Value>& aArguments,
    bool aIsInterval, nsIPrincipal* aSubjectPrincipal, ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  DebuggerNotificationDispatch(
      this, aIsInterval ? DebuggerNotificationType::SetInterval
                        : DebuggerNotificationType::SetTimeout);

  if (aHandler.IsFunction()) {
    nsTArray<JS::Heap<JS::Value>> args;
    if (!args.AppendElements(aArguments, fallible)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return 0;
    }
    RefPtr<TimeoutHandler> handler = new CallbackTimeoutHandler(
        aCx, this, &aHandler.GetAsFunction(), std::move(args));
    return mWorkerPrivate->SetTimeout(aCx, handler, aTimeout, aIsInterval,
                                      Timeout::Reason::eTimeoutOrInterval, aRv);
  }

  constexpr nsLiteralString sinkSetTimeout = u"WorkerGlobalScope setTimeout"_ns;
  constexpr nsLiteralString sinkSetInterval =
      u"WorkerGlobalScope setInterval"_ns;
  Maybe<nsAutoString> compliantStringHolder;
  nsCOMPtr<nsIGlobalObject> pinnedGlobal = this;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aHandler, aIsInterval ? sinkSetInterval : sinkSetTimeout,
          kTrustedTypesOnlySinkGroup, *pinnedGlobal, aSubjectPrincipal,
          compliantStringHolder, aRv);
  if (aRv.Failed()) {
    return 0;
  }

  bool allowEval = false;
  aRv = CSPEvalChecker::CheckForWorker(aCx, mWorkerPrivate, *compliantString,
                                       &allowEval);
  if (NS_WARN_IF(aRv.Failed()) || !allowEval) {
    return 0;
  }

  RefPtr<TimeoutHandler> handler =
      new WorkerScriptTimeoutHandler(aCx, this, *compliantString);

  return mWorkerPrivate->SetTimeout(aCx, handler, aTimeout, aIsInterval,
                                    Timeout::Reason::eTimeoutOrInterval, aRv);
}

bool WorkerGlobalScope::HasScheduledNormalOrHighPriorityWebTasks() const {
  if (!mWebTaskScheduler) {
    return false;
  }
  return mWebTaskScheduler->HasScheduledNormalOrHighPriorityWebTasks();
}

void WorkerGlobalScope::GetOrigin(nsAString& aOrigin) const {
  AssertIsOnWorkerThread();
  nsContentUtils::GetWebExposedOriginSerialization(
      mWorkerPrivate->GetPrincipal(), aOrigin);
}

bool WorkerGlobalScope::CrossOriginIsolated() const {
  return mWorkerPrivate->CrossOriginIsolated();
}

void WorkerGlobalScope::Dump(const Optional<nsAString>& aString) const {
  AssertIsOnWorkerThread();

  if (!aString.WasPassed()) {
    return;
  }

  if (!nsJSUtils::DumpEnabled()) {
    return;
  }

  NS_ConvertUTF16toUTF8 str(aString.Value());

  MOZ_LOG(nsContentUtils::DOMDumpLog(), LogLevel::Debug,
          ("[Worker.Dump] %s", str.get()));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "Gecko", "%s", str.get());
#endif
  fputs(str.get(), stdout);
  fflush(stdout);
}

Performance* WorkerGlobalScope::GetPerformance() {
  AssertIsOnWorkerThread();

  if (!mPerformance) {
    mPerformance = Performance::CreateForWorker(this);
  }

  return mPerformance;
}

bool WorkerGlobalScope::IsInAutomation(JSContext* aCx, JSObject* /* unused */) {
  return GetWorkerPrivateFromContext(aCx)->IsInAutomation();
}

void WorkerGlobalScope::GetJSTestingFunctions(
    JSContext* aCx, JS::MutableHandle<JSObject*> aFunctions, ErrorResult& aRv) {
  JSObject* obj = js::GetTestingFunctions(aCx);
  if (!obj) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  aFunctions.set(obj);
}

already_AddRefed<Promise> WorkerGlobalScope::Fetch(
    const RequestOrUTF8String& aInput, const RequestInit& aInit,
    CallerType aCallerType, ErrorResult& aRv) {
  return FetchRequest(this, aInput, aInit, aCallerType, aRv);
}

already_AddRefed<IDBFactory> WorkerGlobalScope::GetIndexedDB(
    JSContext* aCx, ErrorResult& aErrorResult) {
  AssertIsOnWorkerThread();

  RefPtr<IDBFactory> indexedDB = mIndexedDB;

  if (!indexedDB) {
    StorageAccess access = mWorkerPrivate->StorageAccess();

    bool allowed = true;
    if (access == StorageAccess::eDeny) {
      NS_WARNING("IndexedDB is not allowed in this worker!");
      allowed = false;
    }

    if (ShouldPartitionStorage(access) &&
        !StoragePartitioningEnabled(access,
                                    mWorkerPrivate->CookieJarSettings())) {
      NS_WARNING("IndexedDB is not allowed in this worker!");
      allowed = false;
    }

    auto windowID = mWorkerPrivate->WindowID();

    auto principalInfoPtr =
        allowed ? std::make_unique<PrincipalInfo>(
                      mWorkerPrivate->GetEffectiveStoragePrincipalInfo())
                : nullptr;
    auto res = IDBFactory::CreateForWorker(this, std::move(principalInfoPtr),
                                           windowID);

    if (NS_WARN_IF(res.isErr())) {
      aErrorResult = res.unwrapErr();
      return nullptr;
    }

    indexedDB = res.unwrap();
    mIndexedDB = indexedDB;
  }

  return indexedDB.forget();
}

WebTaskScheduler* WorkerGlobalScope::Scheduler() {
  mWorkerPrivate->AssertIsOnWorkerThread();

  if (!mWebTaskScheduler) {
    mWebTaskScheduler = WebTaskScheduler::CreateForWorker(mWorkerPrivate);
  }

  MOZ_ASSERT(mWebTaskScheduler);
  return mWebTaskScheduler;
}

WebTaskScheduler* WorkerGlobalScope::GetExistingScheduler() const {
  return mWebTaskScheduler;
}

already_AddRefed<Promise> WorkerGlobalScope::CreateImageBitmap(
    const ImageBitmapSource& aImage, const ImageBitmapOptions& aOptions,
    ErrorResult& aRv) {
  return ImageBitmap::Create(this, aImage, Nothing(), aOptions, aRv);
}

already_AddRefed<Promise> WorkerGlobalScope::CreateImageBitmap(
    const ImageBitmapSource& aImage, int32_t aSx, int32_t aSy, int32_t aSw,
    int32_t aSh, const ImageBitmapOptions& aOptions, ErrorResult& aRv) {
  return ImageBitmap::Create(
      this, aImage, Some(gfx::IntRect(aSx, aSy, aSw, aSh)), aOptions, aRv);
}

// https://html.spec.whatwg.org/#structured-cloning
void WorkerGlobalScope::StructuredClone(
    JSContext* aCx, JS::Handle<JS::Value> aValue,
    const StructuredSerializeOptions& aOptions,
    JS::MutableHandle<JS::Value> aRetval, ErrorResult& aError) {
  nsContentUtils::StructuredClone(aCx, this, aValue, aOptions, aRetval, aError);
}

mozilla::dom::DebuggerNotificationManager*
WorkerGlobalScope::GetOrCreateDebuggerNotificationManager() {
  if (!mDebuggerNotificationManager) {
    mDebuggerNotificationManager = new DebuggerNotificationManager(this);
  }

  return mDebuggerNotificationManager;
}

mozilla::dom::DebuggerNotificationManager*
WorkerGlobalScope::GetExistingDebuggerNotificationManager() {
  return mDebuggerNotificationManager;
}

Maybe<EventCallbackDebuggerNotificationType>
WorkerGlobalScope::GetDebuggerNotificationType() const {
  return Some(EventCallbackDebuggerNotificationType::Global);
}

already_AddRefed<ServiceWorkerContainer>
WorkerGlobalScope::GetServiceWorkerContainer() {
  return RefPtr(Navigator())->ServiceWorker();
}

RefPtr<ServiceWorker> WorkerGlobalScope::GetOrCreateServiceWorker(
    const ServiceWorkerDescriptor& aDescriptor) {
  RefPtr<ServiceWorker> ref;
  ForEachGlobalTeardownObserver(
      [&](GlobalTeardownObserver* aObserver, bool* aDoneOut) {
        RefPtr<ServiceWorker> sw = do_QueryObject(aObserver);
        if (!sw || !sw->Descriptor().Matches(aDescriptor)) {
          return;
        }

        ref = std::move(sw);
        *aDoneOut = true;
      });

  if (!ref) {
    ref = ServiceWorker::Create(this, aDescriptor);
  }

  return ref;
}

RefPtr<ServiceWorkerRegistration>
WorkerGlobalScope::GetServiceWorkerRegistration(
    const ServiceWorkerRegistrationDescriptor& aDescriptor) const {
  AssertIsOnWorkerThread();
  RefPtr<ServiceWorkerRegistration> ref;
  ForEachGlobalTeardownObserver(
      [&](GlobalTeardownObserver* aObserver, bool* aDoneOut) {
        RefPtr<ServiceWorkerRegistration> swr = do_QueryObject(aObserver);
        if (!swr || !swr->MatchesDescriptor(aDescriptor)) {
          return;
        }

        ref = std::move(swr);
        *aDoneOut = true;
      });
  return ref;
}

RefPtr<ServiceWorkerRegistration>
WorkerGlobalScope::GetOrCreateServiceWorkerRegistration(
    const ServiceWorkerRegistrationDescriptor& aDescriptor) {
  AssertIsOnWorkerThread();
  RefPtr<ServiceWorkerRegistration> ref =
      GetServiceWorkerRegistration(aDescriptor);
  if (!ref) {
    ref = ServiceWorkerRegistration::CreateForWorker(mWorkerPrivate, this,
                                                     aDescriptor);
  }
  return ref;
}

mozilla::dom::StorageManager* WorkerGlobalScope::GetStorageManager() {
  return RefPtr(Navigator())->Storage();
}

// https://html.spec.whatwg.org/multipage/web-messaging.html#eligible-for-messaging
// * a WorkerGlobalScope object whose closing flag is false and whose worker
//   is not a suspendable worker.
bool WorkerGlobalScope::IsEligibleForMessaging() {
  return mIsEligibleForMessaging;
}

void WorkerGlobalScope::ReportToConsole(
    uint32_t aErrorFlags, const nsCString& aCategory, PropertiesFile aFile,
    const nsCString& aMessageName, const nsTArray<nsString>& aParams,
    const mozilla::SourceLocation& aLocation) {
  WorkerPrivate::ReportErrorToConsole(aErrorFlags, aCategory, aFile,
                                      aMessageName, aParams, aLocation);
}

void WorkerGlobalScope::StorageAccessPermissionGranted() {
  // Reset the IndexedDB factory.
  mIndexedDB = nullptr;

  // Reset DOM Cache
  mCacheStorage = nullptr;
}

TrustedTypePolicyFactory* WorkerGlobalScope::TrustedTypes() {
  AssertIsOnWorkerThread();
  if (!mTrustedTypePolicyFactory) {
    mTrustedTypePolicyFactory = MakeRefPtr<TrustedTypePolicyFactory>(this);
  }

  return mTrustedTypePolicyFactory;
}

bool WorkerGlobalScope::WindowInteractionAllowed() const {
  AssertIsOnWorkerThread();
  return mWindowInteractionsAllowed > 0;
}

void WorkerGlobalScope::AllowWindowInteraction() {
  AssertIsOnWorkerThread();
  mWindowInteractionsAllowed++;
}

void WorkerGlobalScope::ConsumeWindowInteraction() {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(mWindowInteractionsAllowed);
  mWindowInteractionsAllowed--;
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(DedicatedWorkerGlobalScope,
                                   WorkerGlobalScope, mFrameRequestManager)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(DedicatedWorkerGlobalScope,
                                               WorkerGlobalScope)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(DedicatedWorkerGlobalScope,
                                               WorkerGlobalScope)

DedicatedWorkerGlobalScope::DedicatedWorkerGlobalScope(
    WorkerPrivate* aWorkerPrivate, UniquePtr<ClientSource> aClientSource,
    const nsString& aName)
    : WorkerGlobalScope(std::move(aWorkerPrivate), std::move(aClientSource)),
      NamedWorkerGlobalScopeMixin(aName) {}

bool DedicatedWorkerGlobalScope::WrapGlobalObject(
    JSContext* aCx, JS::MutableHandle<JSObject*> aReflector) {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(!mWorkerPrivate->IsSharedWorker());

  JS::RealmOptions options;
  mWorkerPrivate->CopyJSRealmOptions(options);

  xpc::SetPrefableRealmOptions(options);

  return DedicatedWorkerGlobalScope_Binding::Wrap(
      aCx, this, this, options,
      nsJSPrincipals::get(mWorkerPrivate->GetPrincipal()), aReflector);
}

void DedicatedWorkerGlobalScope::PostMessage(
    JSContext* aCx, JS::Handle<JS::Value> aMessage,
    const Sequence<JSObject*>& aTransferable, ErrorResult& aRv) {
  AssertIsOnWorkerThread();
  mWorkerPrivate->PostMessageToParent(aCx, aMessage, aTransferable, aRv);
}

void DedicatedWorkerGlobalScope::PostMessage(
    JSContext* aCx, JS::Handle<JS::Value> aMessage,
    const StructuredSerializeOptions& aOptions, ErrorResult& aRv) {
  AssertIsOnWorkerThread();
  mWorkerPrivate->PostMessageToParent(aCx, aMessage, aOptions.mTransfer, aRv);
}

void DedicatedWorkerGlobalScope::Close() {
  AssertIsOnWorkerThread();
  mWorkerPrivate->CloseInternal();
}

uint32_t DedicatedWorkerGlobalScope::RequestAnimationFrame(
    FrameRequestCallback& aCallback, ErrorResult& aError) {
  AssertIsOnWorkerThread();

  DebuggerNotificationDispatch(this,
                               DebuggerNotificationType::RequestAnimationFrame);

  // Ensure the worker is associated with a window.
  if (mWorkerPrivate->WindowID() == UINT64_MAX) {
    aError.ThrowNotSupportedError("Worker has no associated owner Window");
    return 0;
  }

  if (!mVsyncChild) {
    PBackgroundChild* bgChild = BackgroundChild::GetOrCreateForCurrentThread();
    mVsyncChild = MakeRefPtr<VsyncWorkerChild>();

    if (!bgChild || !mVsyncChild->Initialize(mWorkerPrivate) ||
        !bgChild->SendPVsyncConstructor(mVsyncChild)) {
      mVsyncChild->Destroy();
      mVsyncChild = nullptr;
      aError.ThrowNotSupportedError(
          "Worker failed to register for vsync to drive event loop");
      return 0;
    }
  }

  if (!mDocListener) {
    mDocListener = WorkerDocumentListener::Create(mWorkerPrivate);
    if (!mDocListener) {
      aError.ThrowNotSupportedError(
          "Worker failed to register for document visibility events");
      return 0;
    }
  }

  uint32_t handle = 0;
  aError = mFrameRequestManager.Schedule(aCallback, &handle);
  if (!aError.Failed() && mDocumentVisible) {
    mVsyncChild->TryObserve();
  }
  return handle;
}

void DedicatedWorkerGlobalScope::CancelAnimationFrame(uint32_t aHandle,
                                                      ErrorResult& aError) {
  AssertIsOnWorkerThread();

  DebuggerNotificationDispatch(this,
                               DebuggerNotificationType::CancelAnimationFrame);

  // Ensure the worker is associated with a window.
  if (mWorkerPrivate->WindowID() == UINT64_MAX) {
    aError.ThrowNotSupportedError("Worker has no associated owner Window");
    return;
  }

  mFrameRequestManager.Cancel(aHandle);
  if (mVsyncChild && mFrameRequestManager.IsEmpty()) {
    mVsyncChild->TryUnobserve();
  }
}

void DedicatedWorkerGlobalScope::OnDocumentVisible(bool aVisible) {
  AssertIsOnWorkerThread();

  mDocumentVisible = aVisible;

  // We only change state immediately when we become visible. If we become
  // hidden, then we wait for the next vsync tick to apply that.
  if (aVisible && !mFrameRequestManager.IsEmpty()) {
    mVsyncChild->TryObserve();
  }
}

void DedicatedWorkerGlobalScope::OnVsync(const VsyncEvent& aVsync) {
  AssertIsOnWorkerThread();

  if (mFrameRequestManager.IsEmpty() || !mDocumentVisible) {
    // If we ever receive a vsync event, and there are still no callbacks to
    // process, or we remain hidden, we should disable observing them. By
    // waiting an extra tick, we ensure we minimize extra IPC for content that
    // does not call requestFrameAnimation directly during the callback, or
    // that is rapidly toggling between hidden and visible.
    mVsyncChild->TryUnobserve();
    return;
  }

  RefPtr<DedicatedWorkerGlobalScope> scope(this);
  CallbackDebuggerNotificationGuard guard(
      scope, DebuggerNotificationType::RequestAnimationFrameCallback);

  // This is similar to what we do in nsRefreshDriver::RunFrameRequestCallbacks
  // and Performance::TimeStampToDOMHighResForRendering in order to have the
  // same behaviour for requestAnimationFrame on both the main and worker
  // threads.
  DOMHighResTimeStamp timeStamp = 0;
  if (!aVsync.mTime.IsNull()) {
    timeStamp = mWorkerPrivate->TimeStampToDOMHighRes(aVsync.mTime);
    // 0 is an inappropriate mixin for this this area; however CSS Animations
    // needs to have it's Time Reduction Logic refactored, so it's currently
    // only clamping for RFP mode. RFP mode gives a much lower time precision,
    // so we accept the security leak here for now.
    timeStamp = nsRFPService::ReduceTimePrecisionAsMSecsRFPOnly(
        timeStamp, 0, this->GetRTPCallerType());
  }

  FrameRequestManager::FiringCallbacks callbacks(mFrameRequestManager);

  for (auto& callback : callbacks.mList) {
    if (callback.mCancelled) {
      continue;
    }

    // MOZ_KnownLive is OK, because the stack array `callbacks` keeps the
    // callback alive and the mCallback strong reference can't be mutated by
    // the call.
    LogFrameRequestCallback::Run run(callback.mCallback);
    MOZ_KnownLive(callback.mCallback)->Call(timeStamp);
  }
}

SharedWorkerGlobalScope::SharedWorkerGlobalScope(
    WorkerPrivate* aWorkerPrivate, UniquePtr<ClientSource> aClientSource,
    const nsString& aName)
    : WorkerGlobalScope(std::move(aWorkerPrivate), std::move(aClientSource)),
      NamedWorkerGlobalScopeMixin(aName) {}

bool SharedWorkerGlobalScope::WrapGlobalObject(
    JSContext* aCx, JS::MutableHandle<JSObject*> aReflector) {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(mWorkerPrivate->IsSharedWorker());

  JS::RealmOptions options;
  mWorkerPrivate->CopyJSRealmOptions(options);

  return SharedWorkerGlobalScope_Binding::Wrap(
      aCx, this, this, options,
      nsJSPrincipals::get(mWorkerPrivate->GetPrincipal()), aReflector);
}

void SharedWorkerGlobalScope::Close() {
  AssertIsOnWorkerThread();
  mWorkerPrivate->CloseInternal();
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(ServiceWorkerGlobalScope, WorkerGlobalScope,
                                   mClients, mExtensionBrowser, mRegistration,
                                   mCookieStore)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ServiceWorkerGlobalScope)
NS_INTERFACE_MAP_END_INHERITING(WorkerGlobalScope)

NS_IMPL_ADDREF_INHERITED(ServiceWorkerGlobalScope, WorkerGlobalScope)
NS_IMPL_RELEASE_INHERITED(ServiceWorkerGlobalScope, WorkerGlobalScope)

ServiceWorkerGlobalScope::ServiceWorkerGlobalScope(
    WorkerPrivate* aWorkerPrivate, UniquePtr<ClientSource> aClientSource,
    const ServiceWorkerRegistrationDescriptor& aRegistrationDescriptor)
    : WorkerGlobalScope(std::move(aWorkerPrivate), std::move(aClientSource)),
      mScope(NS_ConvertUTF8toUTF16(aRegistrationDescriptor.Scope()))

      // Eagerly create the registration because we will need to receive
      // updates about the state of the registration.  We can't wait until
      // first access to start receiving these.
      ,
      mRegistration(
          GetOrCreateServiceWorkerRegistration(aRegistrationDescriptor)) {}

ServiceWorkerGlobalScope::~ServiceWorkerGlobalScope() = default;

bool ServiceWorkerGlobalScope::WrapGlobalObject(
    JSContext* aCx, JS::MutableHandle<JSObject*> aReflector) {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(mWorkerPrivate->IsServiceWorker());

  JS::RealmOptions options;
  mWorkerPrivate->CopyJSRealmOptions(options);

  return ServiceWorkerGlobalScope_Binding::Wrap(
      aCx, this, this, options,
      nsJSPrincipals::get(mWorkerPrivate->GetPrincipal()), aReflector);
}

already_AddRefed<Clients> ServiceWorkerGlobalScope::GetClients() {
  if (!mClients) {
    mClients = new Clients(this);
  }

  RefPtr<Clients> ref = mClients;
  return ref.forget();
}

ServiceWorkerRegistration* ServiceWorkerGlobalScope::Registration() {
  return mRegistration;
}

EventHandlerNonNull* ServiceWorkerGlobalScope::GetOnfetch() {
  AssertIsOnWorkerThread();

  return GetEventHandler(nsGkAtoms::onfetch);
}

namespace {

class ReportFetchListenerWarningRunnable final : public Runnable {
  const nsCString mScope;
  mozilla::JSCallingLocation mCaller;

 public:
  explicit ReportFetchListenerWarningRunnable(const nsString& aScope)
      : mozilla::Runnable("ReportFetchListenerWarningRunnable"),
        mScope(NS_ConvertUTF16toUTF8(aScope)) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    JSContext* cx = workerPrivate->GetJSContext();
    MOZ_ASSERT(cx);

    mCaller = JSCallingLocation::Get(cx);
  }

  NS_IMETHOD
  Run() override {
    AssertIsOnMainThread();

    ServiceWorkerManager::LocalizeAndReportToAllClients(
        mScope, "ServiceWorkerNoFetchHandler", nsTArray<nsString>{},
        nsIScriptError::warningFlag, mCaller.FileName(), u""_ns, mCaller.mLine,
        mCaller.mColumn);

    return NS_OK;
  }
};

}  // anonymous namespace

void ServiceWorkerGlobalScope::NoteFetchHandlerWasAdded() const {
  if (mWorkerPrivate->WorkerScriptExecutedSuccessfully()) {
    RefPtr<Runnable> r = new ReportFetchListenerWarningRunnable(mScope);
    mWorkerPrivate->DispatchToMainThreadForMessaging(r.forget());
  }
  mWorkerPrivate->SetFetchHandlerWasAdded();
}

void ServiceWorkerGlobalScope::SetOnfetch(
    mozilla::dom::EventHandlerNonNull* aCallback) {
  AssertIsOnWorkerThread();

  if (aCallback) {
    NoteFetchHandlerWasAdded();
  }
  SetEventHandler(nsGkAtoms::onfetch, aCallback);
}

void ServiceWorkerGlobalScope::EventListenerAdded(nsAtom* aType) {
  AssertIsOnWorkerThread();

  if (aType == nsGkAtoms::onfetch) {
    NoteFetchHandlerWasAdded();
  }
}

already_AddRefed<Promise> ServiceWorkerGlobalScope::SkipWaiting(
    ErrorResult& aRv) {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(mWorkerPrivate->IsServiceWorker());

  RefPtr<Promise> promise = Promise::Create(this, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  using MozPromiseType =
      decltype(mWorkerPrivate->SetServiceWorkerSkipWaitingFlag())::element_type;
  auto holder = MakeRefPtr<DOMMozPromiseRequestHolder<MozPromiseType>>(this);

  mWorkerPrivate->SetServiceWorkerSkipWaitingFlag()
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [holder, promise](const MozPromiseType::ResolveOrRejectValue&) {
               holder->Complete();
               promise->MaybeResolveWithUndefined();
             })
      ->Track(*holder);

  return promise.forget();
}

SafeRefPtr<extensions::ExtensionBrowser>
ServiceWorkerGlobalScope::AcquireExtensionBrowser() {
  if (!mExtensionBrowser) {
    mExtensionBrowser = MakeSafeRefPtr<extensions::ExtensionBrowser>(this);
  }

  return mExtensionBrowser.clonePtr();
}

already_AddRefed<CookieStore> ServiceWorkerGlobalScope::CookieStore() {
  if (!mCookieStore) {
    mCookieStore = CookieStore::Create(this);
  }

  return do_AddRef(mCookieStore);
}

bool WorkerDebuggerGlobalScope::WrapGlobalObject(
    JSContext* aCx, JS::MutableHandle<JSObject*> aReflector) {
  AssertIsOnWorkerThread();

  JS::RealmOptions options;
  mWorkerPrivate->CopyJSRealmOptions(options);

  return WorkerDebuggerGlobalScope_Binding::Wrap(
      aCx, this, this, options,
      nsJSPrincipals::get(mWorkerPrivate->GetPrincipal()), aReflector);
}

void WorkerDebuggerGlobalScope::GetGlobal(JSContext* aCx,
                                          JS::MutableHandle<JSObject*> aGlobal,
                                          ErrorResult& aRv) {
  WorkerGlobalScope* scope = mWorkerPrivate->GetOrCreateGlobalScope(aCx);
  if (!scope) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  aGlobal.set(scope->GetWrapper());
}

void WorkerDebuggerGlobalScope::CreateSandbox(
    JSContext* aCx, const nsAString& aName, JS::Handle<JSObject*> aPrototype,
    JS::MutableHandle<JSObject*> aResult, ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  aResult.set(nullptr);

  JS::Rooted<JS::Value> protoVal(aCx);
  protoVal.setObjectOrNull(aPrototype);
  JS::Rooted<JSObject*> sandbox(
      aCx,
      SimpleGlobalObject::Create(
          SimpleGlobalObject::GlobalType::WorkerDebuggerSandbox, protoVal));

  if (!sandbox) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  if (!JS_WrapObject(aCx, &sandbox)) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  aResult.set(sandbox);
}

void WorkerDebuggerGlobalScope::LoadSubScript(
    JSContext* aCx, const nsAString& aURL,
    const Optional<JS::Handle<JSObject*>>& aSandbox, ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  Maybe<JSAutoRealm> ar;
  if (aSandbox.WasPassed()) {
    // We only care about worker debugger sandbox objects here, so
    // CheckedUnwrapStatic is fine.
    JS::Rooted<JSObject*> sandbox(aCx,
                                  js::CheckedUnwrapStatic(aSandbox.Value()));
    if (!sandbox || !IsWorkerDebuggerSandbox(sandbox)) {
      aRv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }

    ar.emplace(aCx, sandbox);
  }

  nsTArray<nsString> urls;
  urls.AppendElement(aURL);
  workerinternals::Load(mWorkerPrivate, nullptr, urls, DebuggerScript, aRv);
}

void WorkerDebuggerGlobalScope::EnterEventLoop() {
  // We're on the worker thread here, and WorkerPrivate's refcounting is
  // non-threadsafe: you can only do it on the parent thread.  What that
  // means in practice is that we're relying on it being kept alive while
  // we run.  Hopefully.
  MOZ_KnownLive(mWorkerPrivate)->EnterDebuggerEventLoop();
}

void WorkerDebuggerGlobalScope::LeaveEventLoop() {
  mWorkerPrivate->LeaveDebuggerEventLoop();
}

void WorkerDebuggerGlobalScope::PostMessage(const nsAString& aMessage) {
  mWorkerPrivate->PostMessageToDebugger(aMessage);
}

void WorkerDebuggerGlobalScope::SetImmediate(Function& aHandler,
                                             ErrorResult& aRv) {
  mWorkerPrivate->SetDebuggerImmediate(aHandler, aRv);
}

void WorkerDebuggerGlobalScope::ReportError(JSContext* aCx,
                                            const nsAString& aMessage) {
  auto caller = JSCallingLocation::Get(aCx);
  mWorkerPrivate->ReportErrorToDebugger(caller.FileName(), caller.mLine,
                                        aMessage);
}

void WorkerDebuggerGlobalScope::RetrieveConsoleEvents(
    JSContext* aCx, nsTArray<JS::Value>& aEvents, ErrorResult& aRv) {
  WorkerGlobalScope* scope = mWorkerPrivate->GetOrCreateGlobalScope(aCx);
  if (!scope) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<Console> console = scope->GetConsole(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  console->RetrieveConsoleEvents(aCx, aEvents, aRv);
}

void WorkerDebuggerGlobalScope::ClearConsoleEvents(JSContext* aCx,
                                                   ErrorResult& aRv) {
  WorkerGlobalScope* scope = mWorkerPrivate->GetOrCreateGlobalScope(aCx);
  if (!scope) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<Console> console = scope->GetConsoleIfExists();
  if (console) {
    console->ClearStorage();
  }
}

void WorkerDebuggerGlobalScope::SetConsoleEventHandler(JSContext* aCx,
                                                       AnyCallback* aHandler,
                                                       ErrorResult& aRv) {
  WorkerGlobalScope* scope = mWorkerPrivate->GetOrCreateGlobalScope(aCx);
  if (!scope) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<Console> console = scope->GetConsole(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  console->SetConsoleEventHandler(aHandler);
}

void WorkerDebuggerGlobalScope::Dump(JSContext* aCx,
                                     const Optional<nsAString>& aString) const {
  WorkerGlobalScope* scope = mWorkerPrivate->GetOrCreateGlobalScope(aCx);
  if (scope) {
    scope->Dump(aString);
  }
}

bool IsWorkerGlobal(JSObject* object) {
  return IS_INSTANCE_OF(WorkerGlobalScope, object);
}

bool IsWorkerDebuggerGlobal(JSObject* object) {
  return IS_INSTANCE_OF(WorkerDebuggerGlobalScope, object);
}

bool IsWorkerDebuggerSandbox(JSObject* object) {
  return SimpleGlobalObject::SimpleGlobalType(object) ==
         SimpleGlobalObject::GlobalType::WorkerDebuggerSandbox;
}

}  // namespace mozilla::dom
