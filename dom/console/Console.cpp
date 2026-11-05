/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Console.h"

#include "ConsoleCommon.h"
#include "js/Array.h"               // JS::GetArrayLength, JS::NewArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineElement, JS_DefineProperty, JS_GetElement
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/JSObjectHolder.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_devtools.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BlobBinding.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/ConsoleBinding.h"
#include "mozilla/dom/ConsoleInstance.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/FunctionBinding.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PromiseBinding.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/StructuredCloneHolder.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/WorkletGlobalScope.h"
#include "mozilla/dom/WorkletImpl.h"
#include "mozilla/dom/WorkletThread.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMNavigationTiming.h"
#include "nsDocShell.h"
#include "nsGlobalWindowInner.h"
#include "nsIConsoleAPIStorage.h"
#include "nsIException.h"  // for nsIStackFrame
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadContext.h"
#include "nsISupportsPrimitives.h"
#include "nsIWebNavigation.h"
#include "nsIXPConnect.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsReadableUtils.h"
#include "xpcpublic.h"

// The maximum allowed number of concurrent timers per page.
#define MAX_PAGE_TIMERS 10000

// The maximum allowed number of concurrent counters per page.
#define MAX_PAGE_COUNTERS 10000

// The maximum stacktrace depth when populating the stacktrace array used for
// console.trace().
#define DEFAULT_MAX_STACKTRACE_DEPTH 200

// This tags are used in the Structured Clone Algorithm to move js values from
// worker thread to main thread
#define CONSOLE_TAG_BLOB JS_SCTAG_USER_MIN

// This value is taken from ConsoleAPIStorage.js
#define STORAGE_MAX_EVENTS 1000

using namespace mozilla::dom::exceptions;

namespace mozilla::dom {

struct ConsoleStructuredCloneData {
  nsCOMPtr<nsIGlobalObject> mGlobal;
  nsTArray<RefPtr<BlobImpl>> mBlobs;
};

static void ComposeAndStoreGroupName(JSContext* aCx,
                                     const Sequence<JS::Value>& aData,
                                     nsAString& aName,
                                     nsTArray<nsString>* aGroupStack);
static bool UnstoreGroupName(nsAString& aName, nsTArray<nsString>* aGroupStack);

static bool ProcessArguments(JSContext* aCx, const Sequence<JS::Value>& aData,
                             Sequence<JS::Value>& aSequence,
                             Sequence<nsString>& aStyles);

static JS::Value CreateCounterOrResetCounterValue(JSContext* aCx,
                                                  const nsAString& aCountLabel,
                                                  uint32_t aCountValue);

/**
 * Console API in workers uses the Structured Clone Algorithm to move any value
 * from the worker thread to the main-thread. Some object cannot be moved and,
 * in these cases, we convert them to strings.
 * It's not the best, but at least we are able to show something.
 */

class ConsoleCallData final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ConsoleCallData)

  ConsoleCallData(Console::MethodName aName, const nsAString& aString,
                  Console* aConsole)
      : mMutex("ConsoleCallData"),
        mConsoleID(aConsole->mConsoleID),
        mConsolePrefix(aConsole->mConsolePrefix),
        mMethodName(aName),
        mMicroSecondTimeStamp(JS_Now()),
        mStartTimerValue(0),
        mStartTimerStatus(Console::eTimerUnknown),
        mLogTimerDuration(0),
        mLogTimerStatus(Console::eTimerUnknown),
        mCountValue(MAX_PAGE_COUNTERS),
        mIDType(eUnknown),
        mOuterIDNumber(0),
        mInnerIDNumber(0),
        mMethodString(aString) {}

  void SetIDs(uint64_t aOuterID, uint64_t aInnerID) MOZ_REQUIRES(mMutex) {
    MOZ_ASSERT(mIDType == eUnknown);

    mOuterIDNumber = aOuterID;
    mInnerIDNumber = aInnerID;
    mIDType = eNumber;
  }

  void SetIDs(const nsAString& aOuterID, const nsAString& aInnerID)
      MOZ_REQUIRES(mMutex) {
    MOZ_ASSERT(mIDType == eUnknown);

    mOuterIDString = aOuterID;
    mInnerIDString = aInnerID;
    mIDType = eString;
  }

  void SetOriginAttributes(const OriginAttributes& aOriginAttributes)
      MOZ_REQUIRES(mMutex) {
    mOriginAttributes = aOriginAttributes;
  }

  void SetAddonId(nsIPrincipal* aPrincipal) MOZ_REQUIRES(mMutex) {
    nsAutoString addonId;
    aPrincipal->GetAddonId(addonId);

    mAddonId = std::move(addonId);
  }

  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(ConsoleCallData);
  }

  Mutex mMutex;

  const nsString mConsoleID MOZ_GUARDED_BY(mMutex);
  const nsString mConsolePrefix MOZ_GUARDED_BY(mMutex);

  const Console::MethodName mMethodName MOZ_GUARDED_BY(mMutex);
  int64_t mMicroSecondTimeStamp MOZ_GUARDED_BY(mMutex);

  // These values are set in the owning thread and they contain the timestamp of
  // when the new timer has started, the name of it and the status of the
  // creation of it. If status is false, something went wrong. User
  // DOMHighResTimeStamp instead mozilla::TimeStamp because we use
  // monotonicTimer from Performance.now();
  // They will be set on the owning thread and never touched again on that
  // thread. They will be used in order to create a ConsoleTimerStart dictionary
  // when console.time() is used.
  DOMHighResTimeStamp mStartTimerValue MOZ_GUARDED_BY(mMutex);
  nsString mStartTimerLabel MOZ_GUARDED_BY(mMutex);
  Console::TimerStatus mStartTimerStatus MOZ_GUARDED_BY(mMutex);

  // These values are set in the owning thread and they contain the duration,
  // the name and the status of the LogTimer method. If status is false,
  // something went wrong. They will be set on the owning thread and never
  // touched again on that thread. They will be used in order to create a
  // ConsoleTimerLogOrEnd dictionary. This members are set when
  // console.timeEnd() or console.timeLog() are called.
  double mLogTimerDuration MOZ_GUARDED_BY(mMutex);
  nsString mLogTimerLabel MOZ_GUARDED_BY(mMutex);
  Console::TimerStatus mLogTimerStatus MOZ_GUARDED_BY(mMutex);

  // These 2 values are set by IncreaseCounter or ResetCounter on the owning
  // thread and they are used by CreateCounterOrResetCounterValue.
  // These members are set when console.count() or console.countReset() are
  // called.
  nsString mCountLabel MOZ_GUARDED_BY(mMutex);
  uint32_t mCountValue MOZ_GUARDED_BY(mMutex);

  // The concept of outerID and innerID is misleading because when a
  // ConsoleCallData is created from a window, these are the window IDs, but
  // when the object is created from a SharedWorker, a ServiceWorker or a
  // subworker of a ChromeWorker these IDs are the type of worker and the
  // filename of the callee.
  // In Console.sys.mjs the ID is 'jsm'.
  enum { eString, eNumber, eUnknown } mIDType MOZ_GUARDED_BY(mMutex);

  uint64_t mOuterIDNumber MOZ_GUARDED_BY(mMutex);
  nsString mOuterIDString MOZ_GUARDED_BY(mMutex);

  uint64_t mInnerIDNumber MOZ_GUARDED_BY(mMutex);
  nsString mInnerIDString MOZ_GUARDED_BY(mMutex);

  OriginAttributes mOriginAttributes MOZ_GUARDED_BY(mMutex);

  nsString mAddonId MOZ_GUARDED_BY(mMutex);

  const nsString mMethodString MOZ_GUARDED_BY(mMutex);

  // Stack management is complicated, because we want to do it as
  // lazily as possible.  Therefore, we have the following behavior:
  // 1)  mTopStackFrame is initialized whenever we have any JS on the stack
  // 2)  mReifiedStack is initialized if we're created in a worker.
  // 3)  mStack is set (possibly to null if there is no JS on the stack) if
  //     we're created on main thread.
  Maybe<ConsoleStackEntry> mTopStackFrame MOZ_GUARDED_BY(mMutex);
  Maybe<nsTArray<ConsoleStackEntry>> mReifiedStack MOZ_GUARDED_BY(mMutex);
  nsCOMPtr<nsIStackFrame> mStack MOZ_GUARDED_BY(mMutex);

 private:
  ~ConsoleCallData() = default;

  NS_DECL_OWNINGTHREAD;
};

// MainThreadConsoleData instances are created on the Console thread and
// referenced from both main and Console threads in order to provide the same
// object for any ConsoleRunnables relating to the same Console.  A Console
// owns a MainThreadConsoleData; MainThreadConsoleData does not keep its
// Console alive.
class MainThreadConsoleData final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MainThreadConsoleData);

  JSObject* GetOrCreateSandbox(JSContext* aCx, nsIPrincipal* aPrincipal);
  // This method must receive aCx and aArguments in the same JS::Compartment.
  void ProcessCallData(JSContext* aCx, ConsoleCallData* aData,
                       const Sequence<JS::Value>& aArguments);

 private:
  ~MainThreadConsoleData() {
    NS_ReleaseOnMainThread("MainThreadConsoleData::mStorage",
                           mStorage.forget());
    NS_ReleaseOnMainThread("MainThreadConsoleData::mSandbox",
                           mSandbox.forget());
  }

  // All members, except for mRefCnt, are accessed only on the main thread,
  // except in MainThreadConsoleData destruction, at which point there are no
  // other references.
  nsCOMPtr<nsIConsoleAPIStorage> mStorage;
  RefPtr<JSObjectHolder> mSandbox;
  nsTArray<nsString> mGroupStack;
};

// This base class must be extended for Worker and for Worklet.
class ConsoleRunnable : public StructuredCloneHolderBase {
 public:
  ~ConsoleRunnable() override {
    MOZ_ASSERT(!mClonedData.mGlobal,
               "mClonedData.mGlobal is set and cleared in a main thread scope");
    // Clear the StructuredCloneHolderBase class.
    Clear();
  }

 protected:
  JSObject* CustomReadHandler(JSContext* aCx, JSStructuredCloneReader* aReader,
                              const JS::CloneDataPolicy& aCloneDataPolicy,
                              uint32_t aTag, uint32_t aIndex) override {
    AssertIsOnMainThread();

    if (aTag == CONSOLE_TAG_BLOB) {
      MOZ_ASSERT(mClonedData.mBlobs.Length() > aIndex);

      JS::Rooted<JS::Value> val(aCx);
      {
        nsCOMPtr<nsIGlobalObject> global = mClonedData.mGlobal;
        RefPtr<Blob> blob =
            Blob::Create(global, mClonedData.mBlobs.ElementAt(aIndex));
        if (!ToJSValue(aCx, blob, &val)) {
          return nullptr;
        }
      }

      return &val.toObject();
    }

    MOZ_CRASH("No other tags are supported.");
    return nullptr;
  }

  bool CustomWriteHandler(JSContext* aCx, JSStructuredCloneWriter* aWriter,
                          JS::Handle<JSObject*> aObj,
                          bool* aSameProcessScopeRequired) override {
    RefPtr<Blob> blob;
    if (NS_SUCCEEDED(UNWRAP_OBJECT(Blob, aObj, blob))) {
      if (NS_WARN_IF(!JS_WriteUint32Pair(aWriter, CONSOLE_TAG_BLOB,
                                         mClonedData.mBlobs.Length()))) {
        return false;
      }

      mClonedData.mBlobs.AppendElement(blob->Impl());
      return true;
    }

    if (!JS_ObjectNotWritten(aWriter, aObj)) {
      return false;
    }

    JS::Rooted<JS::Value> value(aCx, JS::ObjectOrNullValue(aObj));
    JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, value));
    if (NS_WARN_IF(!jsString)) {
      return false;
    }

    if (NS_WARN_IF(!JS_WriteString(aWriter, jsString))) {
      return false;
    }

    return true;
  }

  // Helper method for CallData
  void ProcessCallData(JSContext* aCx, MainThreadConsoleData* aConsoleData,
                       ConsoleCallData* aCallData) {
    AssertIsOnMainThread();

    ConsoleCommon::ClearException ce(aCx);

    // This is the same policy as when writing from the other side, in
    // WriteData.
    JS::CloneDataPolicy cloneDataPolicy;
    cloneDataPolicy.allowIntraClusterClonableSharedObjects();
    cloneDataPolicy.allowSharedMemoryObjects();

    ErrorResult error;
    JS::Rooted<JS::Value> argumentsValue(aCx);
    Read(aCx, &argumentsValue, cloneDataPolicy, error);
    if (error.MaybeSetPendingException(aCx)) {
      return;
    }

    MOZ_ASSERT(argumentsValue.isObject());

    JS::Rooted<JSObject*> argumentsObj(aCx, &argumentsValue.toObject());

    uint32_t length;
    if (!JS::GetArrayLength(aCx, argumentsObj, &length)) {
      return;
    }

    Sequence<JS::Value> values;
    SequenceRooter<JS::Value> arguments(aCx, &values);

    for (uint32_t i = 0; i < length; ++i) {
      JS::Rooted<JS::Value> value(aCx);

      if (!JS_GetElement(aCx, argumentsObj, i, &value)) {
        return;
      }

      if (!values.AppendElement(value, fallible)) {
        return;
      }
    }

    MOZ_ASSERT(values.Length() == length);

    aConsoleData->ProcessCallData(aCx, aCallData, values);
  }

  // Generic
  bool WriteArguments(JSContext* aCx, const Sequence<JS::Value>& aArguments) {
    ConsoleCommon::ClearException ce(aCx);

    JS::Rooted<JSObject*> arguments(
        aCx, JS::NewArrayObject(aCx, aArguments.Length()));
    if (NS_WARN_IF(!arguments)) {
      return false;
    }

    JS::Rooted<JS::Value> arg(aCx);
    for (uint32_t i = 0; i < aArguments.Length(); ++i) {
      arg = aArguments[i];
      if (NS_WARN_IF(
              !JS_DefineElement(aCx, arguments, i, arg, JSPROP_ENUMERATE))) {
        return false;
      }
    }

    JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*arguments));
    return WriteData(aCx, value);
  }

  // Helper method for Profile calls
  void ProcessProfileData(JSContext* aCx, Console::MethodName aMethodName,
                          const nsAString& aAction) {
    AssertIsOnMainThread();

    ConsoleCommon::ClearException ce(aCx);

    IgnoredErrorResult error;
    JS::Rooted<JS::Value> argumentsValue(aCx);
    Read(aCx, &argumentsValue, error);
    mClonedData.mGlobal = nullptr;

    if (error.Failed()) {
      return;
    }

    MOZ_ASSERT(argumentsValue.isObject());
    JS::Rooted<JSObject*> argumentsObj(aCx, &argumentsValue.toObject());
    if (NS_WARN_IF(!argumentsObj)) {
      return;
    }

    uint32_t length;
    if (!JS::GetArrayLength(aCx, argumentsObj, &length)) {
      return;
    }

    Sequence<JS::Value> arguments;
    SequenceRooter<JS::Value> rooter(aCx, &arguments);

    for (uint32_t i = 0; i < length; ++i) {
      JS::Rooted<JS::Value> value(aCx);

      if (!JS_GetElement(aCx, argumentsObj, i, &value)) {
        return;
      }

      if (!arguments.AppendElement(value, fallible)) {
        return;
      }
    }

    Console::ProfileMethodMainthread(aCx, aAction, arguments);
  }

  bool WriteData(JSContext* aCx, JS::Handle<JS::Value> aValue) {
    // We use structuredClone to send the JSValue to the main-thread, in order
    // to store it into the Console API Service. The consumer will be the
    // console panel in the devtools and, because of this, we want to allow the
    // cloning of sharedArrayBuffers and WASM modules.
    JS::CloneDataPolicy cloneDataPolicy;
    cloneDataPolicy.allowIntraClusterClonableSharedObjects();
    cloneDataPolicy.allowSharedMemoryObjects();

    ErrorResult error;
    Write(aCx, aValue, JS::UndefinedHandleValue, cloneDataPolicy, error);
    if (NS_WARN_IF(error.MaybeSetPendingException(aCx))) {
      // Ignore the message.
      return false;
    }

    return true;
  }

  ConsoleStructuredCloneData mClonedData;
};

class ConsoleWorkletRunnable : public Runnable, public ConsoleRunnable {
 protected:
  explicit ConsoleWorkletRunnable(Console* aConsole)
      : Runnable("dom::console::ConsoleWorkletRunnable"),
        mConsoleData(aConsole->GetOrCreateMainThreadData()) {
    WorkletThread::AssertIsOnWorkletThread();
    nsCOMPtr<WorkletGlobalScope> global = do_QueryInterface(aConsole->mGlobal);
    MOZ_ASSERT(global);
    mWorkletImpl = global->Impl();
    MOZ_ASSERT(mWorkletImpl);
  }

  ~ConsoleWorkletRunnable() override = default;

 protected:
  RefPtr<MainThreadConsoleData> mConsoleData;

  RefPtr<WorkletImpl> mWorkletImpl;
};

// This runnable appends a CallData object into the Console queue running on
// the main-thread.
class ConsoleCallDataWorkletRunnable final : public ConsoleWorkletRunnable {
 public:
  static already_AddRefed<ConsoleCallDataWorkletRunnable> Create(
      JSContext* aCx, Console* aConsole, ConsoleCallData* aConsoleData,
      const Sequence<JS::Value>& aArguments) {
    WorkletThread::AssertIsOnWorkletThread();

    RefPtr<ConsoleCallDataWorkletRunnable> runnable =
        new ConsoleCallDataWorkletRunnable(aConsole, aConsoleData);

    if (!runnable->WriteArguments(aCx, aArguments)) {
      return nullptr;
    }

    return runnable.forget();
  }

 private:
  ConsoleCallDataWorkletRunnable(Console* aConsole, ConsoleCallData* aCallData)
      : ConsoleWorkletRunnable(aConsole), mCallData(aCallData) {
    WorkletThread::AssertIsOnWorkletThread();
    MOZ_ASSERT(aCallData);
    aCallData->AssertIsOnOwningThread();

    const WorkletLoadInfo& loadInfo = mWorkletImpl->LoadInfo();
    mCallData->SetIDs(loadInfo.OuterWindowID(), loadInfo.InnerWindowID());
  }

  ~ConsoleCallDataWorkletRunnable() override = default;

  NS_IMETHOD Run() override {
    AssertIsOnMainThread();
    AutoJSAPI jsapi;
    jsapi.Init();
    JSContext* cx = jsapi.cx();

    {
      MutexAutoLock lock(mCallData->mMutex);

      JSObject* sandbox =
          mConsoleData->GetOrCreateSandbox(cx, mWorkletImpl->Principal());
      JS::Rooted<JSObject*> global(cx, sandbox);
      if (NS_WARN_IF(!global)) {
        return NS_ERROR_FAILURE;
      }

      // The CreateSandbox call returns a proxy to the actual sandbox object. We
      // don't need a proxy here.
      global = js::UncheckedUnwrap(global);
      JSAutoRealm ar(cx, global);

      // We don't need to set a parent object in mCallData bacause there are not
      // DOM objects exposed to worklet.

      ProcessCallData(cx, mConsoleData, mCallData);
    }

    return NS_OK;
  }

  RefPtr<ConsoleCallData> mCallData;
};

class ConsoleWorkerRunnable : public WorkerProxyToMainThreadRunnable,
                              public ConsoleRunnable {
 public:
  explicit ConsoleWorkerRunnable(Console* aConsole)
      : mConsoleData(aConsole->GetOrCreateMainThreadData()) {}

  ~ConsoleWorkerRunnable() override = default;

  bool Dispatch(JSContext* aCx, const Sequence<JS::Value>& aArguments) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    if (NS_WARN_IF(!WriteArguments(aCx, aArguments))) {
      RunBackOnWorkerThreadForCleanup(workerPrivate);
      return false;
    }

    if (NS_WARN_IF(!WorkerProxyToMainThreadRunnable::Dispatch(workerPrivate))) {
      // RunBackOnWorkerThreadForCleanup() will be called by
      // WorkerProxyToMainThreadRunnable::Dispatch().
      return false;
    }

    return true;
  }

 protected:
  void RunOnMainThread(WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    AssertIsOnMainThread();

    // Walk up to our containing page
    WorkerPrivate* wp = aWorkerPrivate->GetTopLevelWorker();

    nsCOMPtr<nsPIDOMWindowInner> window = wp->GetWindow();
    if (!window) {
      RunWindowless(aWorkerPrivate);
    } else {
      RunWithWindow(aWorkerPrivate, window);
    }
  }

  void RunWithWindow(WorkerPrivate* aWorkerPrivate,
                     nsPIDOMWindowInner* aWindow) {
    MOZ_ASSERT(aWorkerPrivate);
    AssertIsOnMainThread();

    AutoJSAPI jsapi;
    MOZ_ASSERT(aWindow);

    RefPtr<nsGlobalWindowInner> win = nsGlobalWindowInner::Cast(aWindow);
    if (NS_WARN_IF(!jsapi.Init(win))) {
      return;
    }

    nsCOMPtr<nsPIDOMWindowOuter> outerWindow = aWindow->GetOuterWindow();
    if (NS_WARN_IF(!outerWindow)) {
      return;
    }

    RunConsole(jsapi.cx(), aWindow->AsGlobal(), aWorkerPrivate, outerWindow,
               aWindow);
  }

  void RunWindowless(WorkerPrivate* aWorkerPrivate) {
    MOZ_ASSERT(aWorkerPrivate);
    AssertIsOnMainThread();

    WorkerPrivate* wp = aWorkerPrivate->GetTopLevelWorker();

    MOZ_ASSERT(!wp->GetWindow());

    AutoJSAPI jsapi;
    jsapi.Init();

    JSContext* cx = jsapi.cx();

    JS::Rooted<JSObject*> global(
        cx, mConsoleData->GetOrCreateSandbox(cx, wp->GetPrincipal()));
    if (NS_WARN_IF(!global)) {
      return;
    }

    // The GetOrCreateSandbox call returns a proxy to the actual sandbox object.
    // We don't need a proxy here.
    global = js::UncheckedUnwrap(global);

    JSAutoRealm ar(cx, global);

    nsCOMPtr<nsIGlobalObject> globalObject = xpc::NativeGlobal(global);
    if (NS_WARN_IF(!globalObject)) {
      return;
    }

    RunConsole(cx, globalObject, aWorkerPrivate, nullptr, nullptr);
  }

  void RunBackOnWorkerThreadForCleanup(WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
  }

  // This method is called in the main-thread.
  virtual void RunConsole(JSContext* aCx, nsIGlobalObject* aGlobal,
                          WorkerPrivate* aWorkerPrivate,
                          nsPIDOMWindowOuter* aOuterWindow,
                          nsPIDOMWindowInner* aInnerWindow) = 0;

  bool ForMessaging() const override { return true; }

  RefPtr<MainThreadConsoleData> mConsoleData;
};

// This runnable appends a CallData object into the Console queue running on
// the main-thread.
class ConsoleCallDataWorkerRunnable final : public ConsoleWorkerRunnable {
 public:
  ConsoleCallDataWorkerRunnable(Console* aConsole, ConsoleCallData* aCallData)
      : ConsoleWorkerRunnable(aConsole), mCallData(aCallData) {
    MOZ_ASSERT(aCallData);
    mCallData->AssertIsOnOwningThread();
  }

 private:
  ~ConsoleCallDataWorkerRunnable() override = default;

  void RunConsole(JSContext* aCx, nsIGlobalObject* aGlobal,
                  WorkerPrivate* aWorkerPrivate,
                  nsPIDOMWindowOuter* aOuterWindow,
                  nsPIDOMWindowInner* aInnerWindow) override {
    MOZ_ASSERT(aGlobal);
    MOZ_ASSERT(aWorkerPrivate);
    AssertIsOnMainThread();

    // The windows have to run in parallel.
    MOZ_ASSERT(!!aOuterWindow == !!aInnerWindow);

    {
      MutexAutoLock lock(mCallData->mMutex);
      if (aOuterWindow) {
        mCallData->SetIDs(aOuterWindow->WindowID(), aInnerWindow->WindowID());
      } else {
        ConsoleStackEntry frame;
        if (mCallData->mTopStackFrame) {
          frame = *mCallData->mTopStackFrame;
        }

        nsCString id = frame.mFilename;
        nsString innerID;
        if (aWorkerPrivate->IsSharedWorker()) {
          innerID = u"SharedWorker"_ns;
        } else if (aWorkerPrivate->IsServiceWorker()) {
          innerID = u"ServiceWorker"_ns;
          // Use scope as ID so the webconsole can decide if the message should
          // show up per tab
          id = aWorkerPrivate->ServiceWorkerScope();
        } else {
          innerID = u"Worker"_ns;
        }

        mCallData->SetIDs(NS_ConvertUTF8toUTF16(id), innerID);
      }

      mClonedData.mGlobal = aGlobal;

      ProcessCallData(aCx, mConsoleData, mCallData);

      mClonedData.mGlobal = nullptr;
    }
  }

  RefPtr<ConsoleCallData> mCallData;
};

// This runnable calls ProfileMethod() on the console on the main-thread.
class ConsoleProfileWorkletRunnable final : public ConsoleWorkletRunnable {
 public:
  static already_AddRefed<ConsoleProfileWorkletRunnable> Create(
      JSContext* aCx, Console* aConsole, Console::MethodName aName,
      const nsAString& aAction, const Sequence<JS::Value>& aArguments) {
    WorkletThread::AssertIsOnWorkletThread();

    RefPtr<ConsoleProfileWorkletRunnable> runnable =
        new ConsoleProfileWorkletRunnable(aConsole, aName, aAction);

    if (!runnable->WriteArguments(aCx, aArguments)) {
      return nullptr;
    }

    return runnable.forget();
  }

 private:
  ConsoleProfileWorkletRunnable(Console* aConsole, Console::MethodName aName,
                                const nsAString& aAction)
      : ConsoleWorkletRunnable(aConsole), mName(aName), mAction(aAction) {
    MOZ_ASSERT(aConsole);
  }

  NS_IMETHOD Run() override {
    AssertIsOnMainThread();

    AutoJSAPI jsapi;
    jsapi.Init();
    JSContext* cx = jsapi.cx();

    JSObject* sandbox =
        mConsoleData->GetOrCreateSandbox(cx, mWorkletImpl->Principal());
    JS::Rooted<JSObject*> global(cx, sandbox);
    if (NS_WARN_IF(!global)) {
      return NS_ERROR_FAILURE;
    }

    // The CreateSandbox call returns a proxy to the actual sandbox object. We
    // don't need a proxy here.
    global = js::UncheckedUnwrap(global);

    JSAutoRealm ar(cx, global);

    // We don't need to set a parent object in mCallData bacause there are not
    // DOM objects exposed to worklet.
    ProcessProfileData(cx, mName, mAction);

    return NS_OK;
  }

  Console::MethodName mName;
  nsString mAction;
};

// This runnable calls ProfileMethod() on the console on the main-thread.
class ConsoleProfileWorkerRunnable final : public ConsoleWorkerRunnable {
 public:
  ConsoleProfileWorkerRunnable(Console* aConsole, Console::MethodName aName,
                               const nsAString& aAction)
      : ConsoleWorkerRunnable(aConsole), mName(aName), mAction(aAction) {
    MOZ_ASSERT(aConsole);
  }

 private:
  void RunConsole(JSContext* aCx, nsIGlobalObject* aGlobal,
                  WorkerPrivate* aWorkerPrivate,
                  nsPIDOMWindowOuter* aOuterWindow,
                  nsPIDOMWindowInner* aInnerWindow) override {
    AssertIsOnMainThread();
    MOZ_ASSERT(aGlobal);

    mClonedData.mGlobal = aGlobal;

    ProcessProfileData(aCx, mName, mAction);

    mClonedData.mGlobal = nullptr;
  }

  Console::MethodName mName;
  nsString mAction;
};

NS_IMPL_CYCLE_COLLECTION_CLASS(Console)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Console)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mConsoleEventNotifier)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDumpFunction)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
  tmp->Shutdown();
  tmp->mArgumentStorage.clearAndFree();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Console)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mConsoleEventNotifier)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDumpFunction)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(Console)
  for (uint32_t i = 0; i < tmp->mArgumentStorage.length(); ++i) {
    tmp->mArgumentStorage[i].Trace(aCallbacks, aClosure);
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Console)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Console)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Console)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

/* static */
already_AddRefed<Console> Console::Create(JSContext* aCx,
                                          nsPIDOMWindowInner* aWindow,
                                          ErrorResult& aRv) {
  MOZ_ASSERT_IF(NS_IsMainThread(), aWindow);

  uint64_t outerWindowID = 0;
  uint64_t innerWindowID = 0;

  if (aWindow) {
    innerWindowID = aWindow->WindowID();

    // Without outerwindow any console message coming from this object will not
    // shown in the devtools webconsole. But this should be fine because
    // probably we are shutting down, or the window is CCed/GCed.
    nsPIDOMWindowOuter* outerWindow = aWindow->GetOuterWindow();
    if (outerWindow) {
      outerWindowID = outerWindow->WindowID();
    }
  }

  RefPtr<Console> console = new Console(aCx, nsGlobalWindowInner::Cast(aWindow),
                                        outerWindowID, innerWindowID);
  console->Initialize(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return console.forget();
}

/* static */
already_AddRefed<Console> Console::CreateForWorklet(JSContext* aCx,
                                                    nsIGlobalObject* aGlobal,
                                                    uint64_t aOuterWindowID,
                                                    uint64_t aInnerWindowID,
                                                    ErrorResult& aRv) {
  WorkletThread::AssertIsOnWorkletThread();

  RefPtr<Console> console =
      new Console(aCx, aGlobal, aOuterWindowID, aInnerWindowID);
  console->Initialize(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return console.forget();
}

Console::Console(JSContext* aCx, nsIGlobalObject* aGlobal,
                 uint64_t aOuterWindowID, uint64_t aInnerWindowID,
                 const nsAString& aLogModule, const nsAString& aPrefix)
    : mGlobal(aGlobal),
      mOuterID(aOuterWindowID),
      mInnerID(aInnerWindowID),
      mDumpToStdout(false),
      mLogModule(nullptr),
      mChromeInstance(false),
      mCurrentLogLevel(WebIDLLogLevelToInteger(ConsoleLogLevel::All)),
      mStatus(eUnknown),
      mCreationTimeStamp(TimeStamp::Now()) {
  // Let's enable the dumping to stdout by default for chrome.
  if (nsContentUtils::ThreadsafeIsSystemCaller(aCx)) {
    mDumpToStdout = StaticPrefs::devtools_console_stdout_chrome();
  } else {
    mDumpToStdout = StaticPrefs::devtools_console_stdout_content();
  }

  // An empty logModule defaults to "console".
  // DEPRECATED: An empty logModule can default to the prefix, for backward
  // compatibility with the callers written before logModule existed, such as
  // in comm-central.
  const nsAString& logModule = aLogModule.IsEmpty() ? aPrefix : aLogModule;
  mLogModule = logModule.IsEmpty()
                   ? LogModule::Get("console")
                   : LogModule::Get(NS_ConvertUTF16toUTF8(logModule).get());

  // A prefix that also names the module isn't printed again. This happens when
  // only a prefix was given, or when a caller supporting older versions, which
  // ignore logModule, gave logModule and prefix the same value.
  mMozLogPrefix = aPrefix.Equals(logModule) ? EmptyString() : aPrefix;
  // MOZ_LOG already prints the module name, but stdout and console events
  // don't, so their prefix starts with it.
  mConsolePrefix = logModule.IsEmpty() || mMozLogPrefix.IsEmpty()
                       ? logModule + mMozLogPrefix
                       : logModule + u": "_ns + mMozLogPrefix;

  mozilla::HoldJSObjects(this);
}

Console::~Console() {
  AssertIsOnOwningThread();
  Shutdown();
  mozilla::DropJSObjects(this);
}

void Console::Initialize(ErrorResult& aRv) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mStatus == eUnknown);

  if (NS_IsMainThread()) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (NS_WARN_IF(!obs)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    if (mInnerID) {
      aRv = obs->AddObserver(this, "inner-window-destroyed", true);
      if (NS_WARN_IF(aRv.Failed())) {
        return;
      }
    }

    aRv = obs->AddObserver(this, "memory-pressure", true);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }
  }

  mStatus = eInitialized;
}

void Console::Shutdown() {
  AssertIsOnOwningThread();

  if (mStatus == eUnknown || mStatus == eShuttingDown) {
    return;
  }

  if (NS_IsMainThread()) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, "inner-window-destroyed");
      obs->RemoveObserver(this, "memory-pressure");
    }
  }

  mTimerRegistry.Clear();
  mCounterRegistry.Clear();

  ClearStorage();
  mCallDataStorage.Clear();

  mStatus = eShuttingDown;
}

NS_IMETHODIMP
Console::Observe(nsISupports* aSubject, const char* aTopic,
                 const char16_t* aData) {
  AssertIsOnMainThread();

  if (!strcmp(aTopic, "inner-window-destroyed")) {
    nsCOMPtr<nsISupportsPRUint64> wrapper = do_QueryInterface(aSubject);
    NS_ENSURE_TRUE(wrapper, NS_ERROR_FAILURE);

    uint64_t innerID;
    nsresult rv = wrapper->GetData(&innerID);
    NS_ENSURE_SUCCESS(rv, rv);

    if (innerID == mInnerID) {
      Shutdown();
    }

    return NS_OK;
  }

  if (!strcmp(aTopic, "memory-pressure")) {
    ClearStorage();
    return NS_OK;
  }

  return NS_OK;
}

void Console::ClearStorage() {
  mCallDataStorage.Clear();
  mArgumentStorage.clearAndFree();
}

#define METHOD(name, string)                                          \
  /* static */ void Console::name(const GlobalObject& aGlobal,        \
                                  const Sequence<JS::Value>& aData) { \
    Method(aGlobal, Method##name, nsLiteralString(string), aData);    \
  }

METHOD(Log, u"log")
METHOD(Info, u"info")
METHOD(Warn, u"warn")
METHOD(Error, u"error")
METHOD(Exception, u"exception")
METHOD(Debug, u"debug")
METHOD(Table, u"table")
METHOD(Trace, u"trace")

// Displays an interactive listing of all the properties of an object.
METHOD(Dir, u"dir");
METHOD(Dirxml, u"dirxml");

METHOD(Group, u"group")
METHOD(GroupCollapsed, u"groupCollapsed")

#undef METHOD

/* static */
void Console::Clear(const GlobalObject& aGlobal) {
  const Sequence<JS::Value> data;
  Method(aGlobal, MethodClear, u"clear"_ns, data);
}

/* static */
void Console::GroupEnd(const GlobalObject& aGlobal) {
  const Sequence<JS::Value> data;
  Method(aGlobal, MethodGroupEnd, u"groupEnd"_ns, data);
}

/* static */
void Console::Time(const GlobalObject& aGlobal, const nsAString& aLabel) {
  StringMethod(aGlobal, aLabel, Sequence<JS::Value>(), MethodTime, u"time"_ns);
}

/* static */
void Console::TimeEnd(const GlobalObject& aGlobal, const nsAString& aLabel) {
  StringMethod(aGlobal, aLabel, Sequence<JS::Value>(), MethodTimeEnd,
               u"timeEnd"_ns);
}

/* static */
void Console::TimeLog(const GlobalObject& aGlobal, const nsAString& aLabel,
                      const Sequence<JS::Value>& aData) {
  StringMethod(aGlobal, aLabel, aData, MethodTimeLog, u"timeLog"_ns);
}

/* static */
void Console::StringMethod(const GlobalObject& aGlobal, const nsAString& aLabel,
                           const Sequence<JS::Value>& aData,
                           MethodName aMethodName,
                           const nsAString& aMethodString) {
  RefPtr<Console> console = GetConsole(aGlobal);
  if (!console) {
    return;
  }

  console->StringMethodInternal(aGlobal.Context(), aLabel, aData, aMethodName,
                                aMethodString);
}

void Console::StringMethodInternal(JSContext* aCx, const nsAString& aLabel,
                                   const Sequence<JS::Value>& aData,
                                   MethodName aMethodName,
                                   const nsAString& aMethodString) {
  ConsoleCommon::ClearException ce(aCx);

  Sequence<JS::Value> data;
  SequenceRooter<JS::Value> rooter(aCx, &data);

  JS::Rooted<JS::Value> value(aCx);
  if (!dom::ToJSValue(aCx, aLabel, &value)) {
    return;
  }

  if (!data.AppendElement(value, fallible)) {
    return;
  }

  for (uint32_t i = 0; i < aData.Length(); ++i) {
    if (!data.AppendElement(aData[i], fallible)) {
      return;
    }
  }

  MethodInternal(aCx, aMethodName, aMethodString, data);
}

/* static */
void Console::TimeStamp(const GlobalObject& aGlobal,
                        const JS::Handle<JS::Value> aData) {
  JSContext* cx = aGlobal.Context();

  ConsoleCommon::ClearException ce(cx);

  Sequence<JS::Value> data;
  SequenceRooter<JS::Value> rooter(cx, &data);

  if (aData.isString() && !data.AppendElement(aData, fallible)) {
    return;
  }

  Method(aGlobal, MethodTimeStamp, u"timeStamp"_ns, data);
}

/* static */
void Console::Profile(const GlobalObject& aGlobal,
                      const Sequence<JS::Value>& aData) {
  ProfileMethod(aGlobal, MethodProfile, u"profile"_ns, aData);
}

/* static */
void Console::ProfileEnd(const GlobalObject& aGlobal,
                         const Sequence<JS::Value>& aData) {
  ProfileMethod(aGlobal, MethodProfileEnd, u"profileEnd"_ns, aData);
}

/* static */
void Console::ProfileMethod(const GlobalObject& aGlobal, MethodName aName,
                            const nsAString& aAction,
                            const Sequence<JS::Value>& aData) {
  RefPtr<Console> console = GetConsole(aGlobal);
  if (!console) {
    return;
  }

  JSContext* cx = aGlobal.Context();
  console->ProfileMethodInternal(cx, aName, aAction, aData);
}

void Console::ProfileMethodInternal(JSContext* aCx, MethodName aMethodName,
                                    const nsAString& aAction,
                                    const Sequence<JS::Value>& aData) {
  if (ShouldLogToMozLog(aMethodName)) {
    LogToMozLog(aCx, aMethodName, aAction, aData, nullptr,
                DOMHighResTimeStamp(0.0));
  }

  if (!ShouldProceed(aMethodName)) {
    return;
  }
  MaybeExecuteDumpFunction(aCx, aMethodName, aAction, aData, nullptr,
                           DOMHighResTimeStamp(0.0));

  if (WorkletThread::IsOnWorkletThread()) {
    RefPtr<ConsoleProfileWorkletRunnable> runnable =
        ConsoleProfileWorkletRunnable::Create(aCx, this, aMethodName, aAction,
                                              aData);
    if (!runnable) {
      return;
    }

    NS_DispatchToMainThread(runnable.forget());
    return;
  }

  if (!NS_IsMainThread()) {
    // Here we are in a worker thread.
    RefPtr<ConsoleProfileWorkerRunnable> runnable =
        new ConsoleProfileWorkerRunnable(this, aMethodName, aAction);

    runnable->Dispatch(aCx, aData);
    return;
  }

  ProfileMethodMainthread(aCx, aAction, aData);
}

// static
void Console::ProfileMethodMainthread(JSContext* aCx, const nsAString& aAction,
                                      const Sequence<JS::Value>& aData) {
  MOZ_ASSERT(NS_IsMainThread());
  ConsoleCommon::ClearException ce(aCx);

  RootedDictionary<ConsoleProfileEvent> event(aCx);
  event.mAction = aAction;
  event.mChromeContext = nsContentUtils::ThreadsafeIsSystemCaller(aCx);

  event.mArguments.Construct();
  Sequence<JS::Value>& sequence = event.mArguments.Value();

  for (uint32_t i = 0; i < aData.Length(); ++i) {
    if (!sequence.AppendElement(aData[i], fallible)) {
      return;
    }
  }

  JS::Rooted<JS::Value> eventValue(aCx);
  if (!ToJSValue(aCx, event, &eventValue)) {
    return;
  }

  JS::Rooted<JSObject*> eventObj(aCx, &eventValue.toObject());
  MOZ_ASSERT(eventObj);

  if (!JS_DefineProperty(aCx, eventObj, "wrappedJSObject", eventValue,
                         JSPROP_ENUMERATE)) {
    return;
  }

  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  nsCOMPtr<nsISupports> wrapper;
  const nsIID& iid = NS_GET_IID(nsISupports);

  if (NS_FAILED(xpc->WrapJS(aCx, eventObj, iid, getter_AddRefs(wrapper)))) {
    return;
  }

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(wrapper, "console-api-profiler", nullptr);
  }
}

/* static */
void Console::Assert(const GlobalObject& aGlobal, bool aCondition,
                     const Sequence<JS::Value>& aData) {
  if (!aCondition) {
    Method(aGlobal, MethodAssert, u"assert"_ns, aData);
  }
}

/* static */
void Console::Count(const GlobalObject& aGlobal, const nsAString& aLabel) {
  StringMethod(aGlobal, aLabel, Sequence<JS::Value>(), MethodCount,
               u"count"_ns);
}

/* static */
void Console::CountReset(const GlobalObject& aGlobal, const nsAString& aLabel) {
  StringMethod(aGlobal, aLabel, Sequence<JS::Value>(), MethodCountReset,
               u"countReset"_ns);
}

namespace {

void StackFrameToStackEntry(JSContext* aCx, nsIStackFrame* aStackFrame,
                            ConsoleStackEntry& aStackEntry) {
  MOZ_ASSERT(aStackFrame);

  aStackFrame->GetFilename(aCx, aStackEntry.mFilename);
  aStackEntry.mSourceId = aStackFrame->GetSourceId(aCx);
  aStackEntry.mLineNumber = aStackFrame->GetLineNumber(aCx);
  aStackEntry.mColumnNumber = aStackFrame->GetColumnNumber(aCx);

  aStackFrame->GetName(aCx, aStackEntry.mFunctionName);

  nsString cause;
  aStackFrame->GetAsyncCause(aCx, cause);
  if (!cause.IsEmpty()) {
    aStackEntry.mAsyncCause.Construct(cause);
  }
}

void ReifyStack(JSContext* aCx, nsIStackFrame* aStack,
                nsTArray<ConsoleStackEntry>& aRefiedStack) {
  nsCOMPtr<nsIStackFrame> stack(aStack);

  while (stack) {
    ConsoleStackEntry& data = *aRefiedStack.AppendElement();
    StackFrameToStackEntry(aCx, stack, data);

    nsCOMPtr<nsIStackFrame> caller = stack->GetCaller(aCx);

    if (!caller) {
      caller = stack->GetAsyncCaller(aCx);
    }
    stack.swap(caller);
  }
}

}  // anonymous namespace

// Queue a call to a console method. See the CALL_DELAY constant.
/* static */
void Console::Method(const GlobalObject& aGlobal, MethodName aMethodName,
                     const nsAString& aMethodString,
                     const Sequence<JS::Value>& aData) {
  RefPtr<Console> console = GetConsole(aGlobal);
  if (!console) {
    return;
  }

  console->MethodInternal(aGlobal.Context(), aMethodName, aMethodString, aData);
}

struct ConsoleTimingMarker : public BaseMarkerType<ConsoleTimingMarker> {
  static constexpr const char* Name = "ConsoleTiming";
  static constexpr const char* Description =
      "Console timing markers created using the Console API methods "
      "console.time(), console.timeEnd(), and console.timeStamp().";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"label", MS::InputType::CString, "Label", MS::Format::String},
      {"entryType", MS::InputType::CString, "Entry Type", MS::Format::String}};

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* AllLabels = "{marker.data.label}";

  static constexpr MS::ETWMarkerGroup Group = MS::ETWMarkerGroup::UserMarkers;
};

void Console::MethodInternal(JSContext* aCx, MethodName aMethodName,
                             const nsAString& aMethodString,
                             const Sequence<JS::Value>& aData) {
  // Drop any further computation if the console **and* moz_log levels
  // are not matching the current call's level.
  if (!ShouldProceed(aMethodName) && !ShouldLogToMozLog(aMethodName)) {
    return;
  }

  AssertIsOnOwningThread();

  ConsoleCommon::ClearException ce(aCx);

  RefPtr<ConsoleCallData> callData =
      new ConsoleCallData(aMethodName, aMethodString, this);

  MutexAutoLock lock(callData->mMutex);

  if (!StoreCallData(aCx, callData, aData)) {
    return;
  }

  OriginAttributes oa;

  if (NS_IsMainThread()) {
    if (mGlobal) {
      // Save the principal's OriginAttributes in the console event data
      // so that we will be able to filter messages by origin attributes.
      nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(mGlobal);
      if (NS_WARN_IF(!sop)) {
        return;
      }

      nsCOMPtr<nsIPrincipal> principal = sop->GetPrincipal();
      if (NS_WARN_IF(!principal)) {
        return;
      }

      oa = principal->OriginAttributesRef();
      callData->SetAddonId(principal);

#ifdef DEBUG
      if (!principal->IsSystemPrincipal()) {
        nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(mGlobal);
        if (webNav) {
          nsCOMPtr<nsILoadContext> loadContext = do_QueryInterface(webNav);
          MOZ_ASSERT(loadContext);

          bool pb;
          if (NS_SUCCEEDED(loadContext->GetUsePrivateBrowsing(&pb))) {
            MOZ_ASSERT(pb == oa.IsPrivateBrowsing());
          }
        }
      }
#endif
    }
  } else if (WorkletThread::IsOnWorkletThread()) {
    nsCOMPtr<WorkletGlobalScope> global = do_QueryInterface(mGlobal);
    MOZ_ASSERT(global);
    oa = global->Impl()->OriginAttributesRef();
  } else {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    oa = workerPrivate->GetOriginAttributes();
  }

  callData->SetOriginAttributes(oa);

  JS::StackCapture captureMode =
      ShouldIncludeStackTrace(aMethodName)
          ? JS::StackCapture(JS::MaxFrames(DEFAULT_MAX_STACKTRACE_DEPTH))
          : JS::StackCapture(JS::FirstSubsumedFrame(aCx));
  nsCOMPtr<nsIStackFrame> stack = CreateStack(aCx, std::move(captureMode));

  if (stack) {
    callData->mTopStackFrame.emplace();
    StackFrameToStackEntry(aCx, stack, *callData->mTopStackFrame);
  }

  if (NS_IsMainThread()) {
    callData->mStack = stack;
  } else {
    // nsIStackFrame is not threadsafe, so we need to snapshot it now,
    // before we post our runnable to the main thread.
    callData->mReifiedStack.emplace();
    ReifyStack(aCx, stack, *callData->mReifiedStack);
  }

  DOMHighResTimeStamp monotonicTimer = 0.0;

  // Monotonic timer for 'time', 'timeLog' and 'timeEnd'
  if ((aMethodName == MethodTime || aMethodName == MethodTimeLog ||
       aMethodName == MethodTimeEnd || aMethodName == MethodTimeStamp) &&
      !MonotonicTimer(aCx, aMethodName, aData, &monotonicTimer)) {
    return;
  }

  if (ShouldLogToMozLog(aMethodName)) {
    LogToMozLog(aCx, aMethodName, aMethodString, aData, stack, monotonicTimer);
  }

  // Stop any further computation if we only had to log to moz_log
  if (!ShouldProceed(aMethodName)) {
    return;
  }

  if (aMethodName == MethodTime && !aData.IsEmpty()) {
    callData->mStartTimerStatus =
        StartTimer(aCx, aData[0], monotonicTimer, callData->mStartTimerLabel,
                   &callData->mStartTimerValue);
  }

  else if (aMethodName == MethodTimeEnd && !aData.IsEmpty()) {
    callData->mLogTimerStatus =
        LogTimer(aCx, aData[0], monotonicTimer, callData->mLogTimerLabel,
                 &callData->mLogTimerDuration, true /* Cancel timer */);
  }

  else if (aMethodName == MethodTimeLog && !aData.IsEmpty()) {
    callData->mLogTimerStatus =
        LogTimer(aCx, aData[0], monotonicTimer, callData->mLogTimerLabel,
                 &callData->mLogTimerDuration, false /* Cancel timer */);
  }

  else if (aMethodName == MethodTimeStamp &&
           profiler_thread_is_being_profiled_for_markers()) {
    nsAutoJSString label;
    if (!aData.IsEmpty()) {
      JS::Rooted<JS::Value> name(aCx, aData[0]);
      JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, name));
      if (!NS_WARN_IF(!jsString)) {
        (void)NS_WARN_IF(!label.init(aCx, jsString));
      }
    }
    profiler_add_marker("ConsoleTiming", geckoprofiler::category::DOM, {},
                        ConsoleTimingMarker{}, NS_ConvertUTF16toUTF8(label),
                        "timeStamp");
  }

  else if (aMethodName == MethodCount) {
    callData->mCountValue = IncreaseCounter(aCx, aData, callData->mCountLabel);
    if (!callData->mCountValue) {
      return;
    }
  }

  else if (aMethodName == MethodCountReset) {
    callData->mCountValue = ResetCounter(aCx, aData, callData->mCountLabel);
    if (callData->mCountLabel.IsEmpty()) {
      return;
    }
  }

  // Before processing this CallData differently, it's time to call the dump
  // function.
  //
  // Only log the stack trace for console.trace() and console.assert()
  if (aMethodName == MethodTrace || aMethodName == MethodAssert) {
    MaybeExecuteDumpFunction(aCx, aMethodName, aMethodString, aData, stack,
                             monotonicTimer);
  } else {
    MaybeExecuteDumpFunction(aCx, aMethodName, aMethodString, aData, nullptr,
                             monotonicTimer);
  }

  if (NS_IsMainThread()) {
    if (mInnerID) {
      callData->SetIDs(mOuterID, mInnerID);
    } else if (!mPassedInnerID.IsEmpty()) {
      callData->SetIDs(u"jsm"_ns, mPassedInnerID);
    } else {
      // Derive a window ID so inner-window-destroyed can clean up
      // this message. Check the stack first (a SavedFrame whose global
      // is a window), then fall back to scanning arguments.

      // Returns the inner window ID for an object whose global is a
      // window or a sandbox whose prototype is a window, or 0.
      auto WindowIDFromObject = [aCx](JSObject* aObj) -> uint64_t {
        JSObject* obj = js::UncheckedUnwrap(aObj);
        if (nsGlobalWindowInner* win = xpc::WindowGlobalOrNull(obj)) {
          return win->WindowID();
        }
        JSObject* global = JS::GetNonCCWObjectGlobal(obj);
        if (nsGlobalWindowInner* win = xpc::SandboxWindowOrNull(global, aCx)) {
          return win->WindowID();
        }
        return 0;
      };

      uint64_t innerID = 0;
      nsCOMPtr<nsIStackFrame> frame = callData->mStack;
      while (frame) {
        JS::Rooted<JS::Value> savedFrame(aCx);
        frame->GetNativeSavedFrame(&savedFrame);
        if (savedFrame.isObject()) {
          innerID = WindowIDFromObject(&savedFrame.toObject());
          if (innerID) {
            break;
          }
        }
        frame = frame->GetCaller(aCx);
      }
      if (!innerID) {
        for (const auto& arg : aData) {
          if (arg.isObject()) {
            innerID = WindowIDFromObject(&arg.toObject());
            if (innerID) {
              break;
            }
          }
        }
      }
      if (innerID) {
        nsAutoString idStr;
        idStr.AppendInt(innerID);
        callData->SetIDs(u"jsm"_ns, idStr);
      } else {
        nsAutoCString filename;
        if (callData->mTopStackFrame.isSome()) {
          filename = callData->mTopStackFrame->mFilename;
        }
        callData->SetIDs(u"jsm"_ns, NS_ConvertUTF8toUTF16(filename));
      }
    }

    GetOrCreateMainThreadData()->ProcessCallData(aCx, callData, aData);

    // Just because we don't want to expose
    // retrieveConsoleEvents/setConsoleEventHandler to main-thread, we can
    // cleanup the mCallDataStorage:
    UnstoreCallData(callData);
    return;
  }

  if (WorkletThread::IsOnWorkletThread()) {
    RefPtr<ConsoleCallDataWorkletRunnable> runnable =
        ConsoleCallDataWorkletRunnable::Create(aCx, this, callData, aData);
    if (!runnable) {
      return;
    }

    NS_DispatchToMainThread(runnable);
    return;
  }

  // We do this only in workers for now.
  NotifyHandler(aCx, aData, callData);

  if (StaticPrefs::dom_worker_console_dispatch_events_to_main_thread()) {
    RefPtr<ConsoleCallDataWorkerRunnable> runnable =
        new ConsoleCallDataWorkerRunnable(this, callData);
    (void)NS_WARN_IF(!runnable->Dispatch(aCx, aData));
  }
}

MainThreadConsoleData* Console::GetOrCreateMainThreadData() {
  AssertIsOnOwningThread();

  if (!mMainThreadData) {
    mMainThreadData = new MainThreadConsoleData();
  }

  return mMainThreadData;
}

// We store information to lazily compute the stack in the reserved slots of
// LazyStackGetter.  The first slot always stores a JS object: it's either the
// JS wrapper of the nsIStackFrame or the actual reified stack representation.
// The second slot is a PrivateValue() holding an nsIStackFrame* when we haven't
// reified the stack yet, or an UndefinedValue() otherwise.
enum { SLOT_STACKOBJ, SLOT_RAW_STACK };

bool LazyStackGetter(JSContext* aCx, unsigned aArgc, JS::Value* aVp) {
  JS::CallArgs args = CallArgsFromVp(aArgc, aVp);
  JS::Rooted<JSObject*> callee(aCx, &args.callee());

  JS::Value v = js::GetFunctionNativeReserved(&args.callee(), SLOT_RAW_STACK);
  if (v.isUndefined()) {
    // Already reified.
    args.rval().set(js::GetFunctionNativeReserved(callee, SLOT_STACKOBJ));
    return true;
  }

  nsIStackFrame* stack = reinterpret_cast<nsIStackFrame*>(v.toPrivate());
  nsTArray<ConsoleStackEntry> reifiedStack;
  ReifyStack(aCx, stack, reifiedStack);

  JS::Rooted<JS::Value> stackVal(aCx);
  if (NS_WARN_IF(!ToJSValue(aCx, reifiedStack, &stackVal))) {
    return false;
  }

  MOZ_ASSERT(stackVal.isObject());

  js::SetFunctionNativeReserved(callee, SLOT_STACKOBJ, stackVal);
  js::SetFunctionNativeReserved(callee, SLOT_RAW_STACK, JS::UndefinedValue());

  args.rval().set(stackVal);
  return true;
}

void MainThreadConsoleData::ProcessCallData(
    JSContext* aCx, ConsoleCallData* aData,
    const Sequence<JS::Value>& aArguments) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aData);
  aData->mMutex.AssertCurrentThreadOwns();

  JS::Rooted<JS::Value> eventValue(aCx);

  // We want to create a console event object and pass it to our
  // nsIConsoleAPIStorage implementation.  We want to define some accessor
  // properties on this object, and those will need to keep an nsIStackFrame
  // alive.  But nsIStackFrame cannot be wrapped in an untrusted scope.  And
  // further, passing untrusted objects to system code is likely to run afoul of
  // Object Xrays.  So we want to wrap in a system-principal scope here.  But
  // which one?  We could cheat and try to get the underlying JSObject* of
  // mStorage, but that's a bit fragile.  Instead, we just use the junk scope,
  // with explicit permission from the XPConnect module owner.  If you're
  // tempted to do that anywhere else, talk to said module owner first.

  // aCx and aArguments are in the same compartment.
  JS::Rooted<JSObject*> targetScope(aCx, xpc::PrivilegedJunkScope());
  if (NS_WARN_IF(!Console::PopulateConsoleNotificationInTheTargetScope(
          aCx, aArguments, targetScope, &eventValue, aData, &mGroupStack))) {
    return;
  }

  if (!mStorage) {
    mStorage = do_GetService("@mozilla.org/consoleAPI-storage;1");
  }

  if (!mStorage) {
    NS_WARNING("Failed to get the ConsoleAPIStorage service.");
    return;
  }

  nsAutoString innerID;

  MOZ_ASSERT(aData->mIDType != ConsoleCallData::eUnknown);
  if (aData->mIDType == ConsoleCallData::eString) {
    innerID = aData->mInnerIDString;
  } else {
    MOZ_ASSERT(aData->mIDType == ConsoleCallData::eNumber);
    innerID.AppendInt(aData->mInnerIDNumber);
  }

  if (aData->mMethodName == Console::MethodClear) {
    DebugOnly<nsresult> rv = mStorage->ClearEvents(innerID);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "ClearEvents failed");
  }

  if (NS_FAILED(mStorage->RecordEvent(innerID, eventValue))) {
    NS_WARNING("Failed to record a console event.");
  }
}

/* static */
bool Console::PopulateConsoleNotificationInTheTargetScope(
    JSContext* aCx, const Sequence<JS::Value>& aArguments,
    JS::Handle<JSObject*> aTargetScope,
    JS::MutableHandle<JS::Value> aEventValue, ConsoleCallData* aData,
    nsTArray<nsString>* aGroupStack) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aData);
  MOZ_ASSERT(aTargetScope);
  MOZ_ASSERT(JS_IsGlobalObject(aTargetScope));

  aData->mMutex.AssertCurrentThreadOwns();

  ConsoleStackEntry frame;
  if (aData->mTopStackFrame) {
    frame = *aData->mTopStackFrame;
  }

  ConsoleCommon::ClearException ce(aCx);
  RootedDictionary<ConsoleEvent> event(aCx);

  event.mAddonId = aData->mAddonId;

  event.mID.Construct();
  event.mInnerID.Construct();

  event.mChromeContext = nsContentUtils::ThreadsafeIsSystemCaller(aCx);

  if (aData->mIDType == ConsoleCallData::eString) {
    event.mID.Value().SetAsString() = aData->mOuterIDString;
    event.mInnerID.Value().SetAsString() = aData->mInnerIDString;
  } else if (aData->mIDType == ConsoleCallData::eNumber) {
    event.mID.Value().SetAsUnsignedLongLong() = aData->mOuterIDNumber;
    event.mInnerID.Value().SetAsUnsignedLongLong() = aData->mInnerIDNumber;
  } else {
    // aData->mIDType can be eUnknown when we dispatch notifications via
    // mConsoleEventNotifier.
    event.mID.Value().SetAsUnsignedLongLong() = 0;
    event.mInnerID.Value().SetAsUnsignedLongLong() = 0;
  }

  event.mConsoleID = aData->mConsoleID;
  event.mLevel = aData->mMethodString;
  // TODO: Pass the log module and the prefix separately.
  event.mPrefix = aData->mConsolePrefix;

  NS_GetSanitizedSpecFromSpec(frame.mFilename, event.mFilename);

  event.mSourceId = frame.mSourceId;
  event.mLineNumber = frame.mLineNumber;
  event.mColumnNumber = frame.mColumnNumber;
  event.mFunctionName = frame.mFunctionName;
  event.mTimeStamp = aData->mMicroSecondTimeStamp / PR_USEC_PER_MSEC;
  event.mMicroSecondTimeStamp = aData->mMicroSecondTimeStamp;
  event.mPrivate = aData->mOriginAttributes.IsPrivateBrowsing();

  switch (aData->mMethodName) {
    case MethodLog:
    case MethodInfo:
    case MethodWarn:
    case MethodError:
    case MethodException:
    case MethodDebug:
    case MethodAssert:
    case MethodGroup:
    case MethodGroupCollapsed:
    case MethodTrace:
      event.mArguments.Construct();
      event.mStyles.Construct();
      if (NS_WARN_IF(!ProcessArguments(aCx, aArguments,
                                       event.mArguments.Value(),
                                       event.mStyles.Value()))) {
        return false;
      }

      break;

    default:
      event.mArguments.Construct();
      if (NS_WARN_IF(
              !event.mArguments.Value().AppendElements(aArguments, fallible))) {
        return false;
      }
  }

  if (aData->mMethodName == MethodGroup ||
      aData->mMethodName == MethodGroupCollapsed) {
    ComposeAndStoreGroupName(aCx, event.mArguments.Value(), event.mGroupName,
                             aGroupStack);
  }

  else if (aData->mMethodName == MethodGroupEnd) {
    if (!UnstoreGroupName(event.mGroupName, aGroupStack)) {
      return false;
    }
  }

  else if (aData->mMethodName == MethodTime && !aArguments.IsEmpty()) {
    event.mTimer = CreateStartTimerValue(aCx, aData->mStartTimerLabel,
                                         aData->mStartTimerStatus);
  }

  else if ((aData->mMethodName == MethodTimeEnd ||
            aData->mMethodName == MethodTimeLog) &&
           !aArguments.IsEmpty()) {
    event.mTimer = CreateLogOrEndTimerValue(aCx, aData->mLogTimerLabel,
                                            aData->mLogTimerDuration,
                                            aData->mLogTimerStatus);
  }

  else if (aData->mMethodName == MethodCount ||
           aData->mMethodName == MethodCountReset) {
    event.mCounter = CreateCounterOrResetCounterValue(aCx, aData->mCountLabel,
                                                      aData->mCountValue);
  }

  JSAutoRealm ar2(aCx, aTargetScope);

  if (NS_WARN_IF(!ToJSValue(aCx, event, aEventValue))) {
    return false;
  }

  JS::Rooted<JSObject*> eventObj(aCx, &aEventValue.toObject());
  if (NS_WARN_IF(!JS_DefineProperty(aCx, eventObj, "wrappedJSObject", eventObj,
                                    JSPROP_ENUMERATE))) {
    return false;
  }

  if (ShouldIncludeStackTrace(aData->mMethodName)) {
    // Now define the "stacktrace" property on eventObj.  There are two cases
    // here.  Either we came from a worker and have a reified stack, or we want
    // to define a getter that will lazily reify the stack.
    if (aData->mReifiedStack) {
      JS::Rooted<JS::Value> stacktrace(aCx);
      if (NS_WARN_IF(!ToJSValue(aCx, *aData->mReifiedStack, &stacktrace)) ||
          NS_WARN_IF(!JS_DefineProperty(aCx, eventObj, "stacktrace", stacktrace,
                                        JSPROP_ENUMERATE))) {
        return false;
      }
    } else {
      JSFunction* fun =
          js::NewFunctionWithReserved(aCx, LazyStackGetter, 0, 0, "stacktrace");
      if (NS_WARN_IF(!fun)) {
        return false;
      }

      JS::Rooted<JSObject*> funObj(aCx, JS_GetFunctionObject(fun));

      // We want to store our stack in the function and have it stay alive.  But
      // we also need sane access to the C++ nsIStackFrame.  So store both a JS
      // wrapper and the raw pointer: the former will keep the latter alive.
      JS::Rooted<JS::Value> stackVal(aCx);
      nsresult rv = nsContentUtils::WrapNative(aCx, aData->mStack, &stackVal);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return false;
      }

      js::SetFunctionNativeReserved(funObj, SLOT_STACKOBJ, stackVal);
      js::SetFunctionNativeReserved(funObj, SLOT_RAW_STACK,
                                    JS::PrivateValue(aData->mStack.get()));

      if (NS_WARN_IF(!JS_DefineProperty(aCx, eventObj, "stacktrace", funObj,
                                        nullptr, JSPROP_ENUMERATE))) {
        return false;
      }
    }
  }

  return true;
}

namespace {

// Helper method for ProcessArguments. Flushes output, if non-empty, to
// aSequence.
bool FlushOutput(JSContext* aCx, Sequence<JS::Value>& aSequence,
                 nsString& aOutput) {
  if (!aOutput.IsEmpty()) {
    JS::Rooted<JSString*> str(
        aCx, JS_NewUCStringCopyN(aCx, aOutput.get(), aOutput.Length()));
    if (NS_WARN_IF(!str)) {
      return false;
    }

    if (NS_WARN_IF(!aSequence.AppendElement(JS::StringValue(str), fallible))) {
      return false;
    }

    aOutput.Truncate();
  }

  return true;
}

}  // namespace

static void MakeFormatString(nsCString& aFormat, int32_t aInteger,
                             int32_t aMantissa, char aCh) {
  aFormat.Append('%');
  if (aInteger >= 0) {
    aFormat.AppendInt(aInteger);
  }

  if (aMantissa >= 0) {
    aFormat.Append('.');
    aFormat.AppendInt(aMantissa);
  }

  aFormat.Append(aCh);
}

// If the first JS::Value of the array is a string, this method uses it to
// format a string. The supported sequences are:
//   %s    - string
//   %d,%i - integer
//   %f    - double
//   %o,%O - a JS object.
//   %c    - style string.
// The output is an array where any object is a separated item, the rest is
// unified in a format string.
// Example if the input is:
//   "string: %s, integer: %d, object: %o, double: %f", 's', 1, window, 0.9
// The output will be:
//   [ "string: s, integer: 1, object: ", window, ", double: 0.9" ]
//
// The aStyles array is populated with the style strings that the function
// finds based the format string. The index of the styles matches the indexes
// of elements that need the custom styling from aSequence. For elements with
// no custom styling the array is padded with null elements.
static bool ProcessArguments(JSContext* aCx, const Sequence<JS::Value>& aData,
                             Sequence<JS::Value>& aSequence,
                             Sequence<nsString>& aStyles) {
  // This method processes the arguments as format strings (%d, %i, %s...)
  // only if the first element of them is a valid and not-empty string.

  if (aData.IsEmpty()) {
    return true;
  }

  if (aData.Length() == 1 || !aData[0].isString()) {
    return aSequence.AppendElements(aData, fallible);
  }

  JS::Rooted<JS::Value> format(aCx, aData[0]);
  JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, format));
  if (NS_WARN_IF(!jsString)) {
    return false;
  }

  nsAutoJSString string;
  if (NS_WARN_IF(!string.init(aCx, jsString))) {
    return false;
  }

  if (string.IsEmpty()) {
    return aSequence.AppendElements(aData, fallible);
  }

  nsString::const_iterator start, end;
  string.BeginReading(start);
  string.EndReading(end);

  nsString output;
  uint32_t index = 1;

  while (start != end) {
    if (*start != '%') {
      output.Append(*start);
      ++start;
      continue;
    }

    ++start;
    if (start == end) {
      output.Append('%');
      break;
    }

    if (*start == '%') {
      output.Append(*start);
      ++start;
      continue;
    }

    nsAutoString tmp;
    tmp.Append('%');

    int32_t integer = -1;
    int32_t mantissa = -1;

    // Let's parse %<number>.<number> for %d and %f
    if (*start >= '0' && *start <= '9') {
      integer = 0;

      do {
        integer = integer * 10 + *start - '0';
        tmp.Append(*start);
        ++start;
      } while (*start >= '0' && *start <= '9' && start != end);
    }

    if (start == end) {
      output.Append(tmp);
      break;
    }

    if (*start == '.') {
      tmp.Append(*start);
      ++start;

      if (start == end) {
        output.Append(tmp);
        break;
      }

      // '.' must be followed by a number.
      if (*start < '0' || *start > '9') {
        output.Append(tmp);
        continue;
      }

      mantissa = 0;

      do {
        mantissa = mantissa * 10 + *start - '0';
        tmp.Append(*start);
        ++start;
      } while (*start >= '0' && *start <= '9' && start != end);

      if (start == end) {
        output.Append(tmp);
        break;
      }
    }

    char ch = *start;
    tmp.Append(ch);
    ++start;

    switch (ch) {
      case 'o':
      case 'O': {
        if (NS_WARN_IF(!FlushOutput(aCx, aSequence, output))) {
          return false;
        }

        JS::Rooted<JS::Value> v(aCx);
        if (index < aData.Length()) {
          v = aData[index++];
        }

        if (NS_WARN_IF(!aSequence.AppendElement(v, fallible))) {
          return false;
        }

        break;
      }

      case 'c': {
        // If there isn't any output but there's already a style, then
        // discard the previous style and use the next one instead.
        if (output.IsEmpty() && !aStyles.IsEmpty()) {
          aStyles.RemoveLastElement();
        }

        if (NS_WARN_IF(!FlushOutput(aCx, aSequence, output))) {
          return false;
        }

        if (index < aData.Length()) {
          JS::Rooted<JS::Value> v(aCx, aData[index++]);
          JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, v));
          if (NS_WARN_IF(!jsString)) {
            return false;
          }

          int32_t diff = aSequence.Length() - aStyles.Length();
          if (diff > 0) {
            for (int32_t i = 0; i < diff; i++) {
              if (NS_WARN_IF(!aStyles.AppendElement(VoidString(), fallible))) {
                return false;
              }
            }
          }

          nsAutoJSString string;
          if (NS_WARN_IF(!string.init(aCx, jsString))) {
            return false;
          }

          if (NS_WARN_IF(!aStyles.AppendElement(string, fallible))) {
            return false;
          }
        }
        break;
      }

      case 's':
        if (index < aData.Length()) {
          JS::Rooted<JS::Value> value(aCx, aData[index++]);
          JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, value));
          if (NS_WARN_IF(!jsString)) {
            return false;
          }

          nsAutoJSString v;
          if (NS_WARN_IF(!v.init(aCx, jsString))) {
            return false;
          }

          output.Append(v);
        }
        break;

      case 'd':
      case 'i':
        if (index < aData.Length()) {
          JS::Rooted<JS::Value> value(aCx, aData[index++]);

          if (value.isBigInt()) {
            JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, value));
            if (NS_WARN_IF(!jsString)) {
              return false;
            }

            nsAutoJSString v;
            if (NS_WARN_IF(!v.init(aCx, jsString))) {
              return false;
            }
            output.Append(v);
            break;
          }

          int32_t v;
          if (NS_WARN_IF(!JS::ToInt32(aCx, value, &v))) {
            return false;
          }

          nsCString format;
          MakeFormatString(format, integer, mantissa, 'd');
          output.AppendPrintf(format.get(), v);
        }
        break;

      case 'f':
        if (index < aData.Length()) {
          JS::Rooted<JS::Value> value(aCx, aData[index++]);

          double v;
          if (NS_WARN_IF(!JS::ToNumber(aCx, value, &v))) {
            return false;
          }

          // nspr returns "nan", but we want to expose it as "NaN"
          if (std::isnan(v)) {
            output.AppendFloat(v);
          } else {
            nsCString format;
            MakeFormatString(format, integer, std::min(mantissa, 15), 'f');
            output.AppendPrintf(format.get(), v);
          }
        }
        break;

      default:
        output.Append(tmp);
        break;
    }
  }

  if (NS_WARN_IF(!FlushOutput(aCx, aSequence, output))) {
    return false;
  }

  // Discard trailing style element if there is no output to apply it to.
  if (aStyles.Length() > aSequence.Length()) {
    aStyles.TruncateLength(aSequence.Length());
  }

  // The rest of the array, if unused by the format string.
  for (; index < aData.Length(); ++index) {
    if (NS_WARN_IF(!aSequence.AppendElement(aData[index], fallible))) {
      return false;
    }
  }

  return true;
}

// Stringify and Concat all the JS::Value in a single string using ' ' as
// separator. The new group name will be stored in aGroupStack array.
static void ComposeAndStoreGroupName(JSContext* aCx,
                                     const Sequence<JS::Value>& aData,
                                     nsAString& aName,
                                     nsTArray<nsString>* aGroupStack) {
  StringJoinAppend(
      aName, u" "_ns, aData, [aCx](nsAString& dest, const JS::Value& valueRef) {
        JS::Rooted<JS::Value> value(aCx, valueRef);
        JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, value));
        if (!jsString) {
          return;
        }

        nsAutoJSString string;
        if (!string.init(aCx, jsString)) {
          return;
        }

        dest.Append(string);
      });

  aGroupStack->AppendElement(aName);
}

// Remove the last group name and return that name. It returns false if
// aGroupStack is empty.
static bool UnstoreGroupName(nsAString& aName,
                             nsTArray<nsString>* aGroupStack) {
  if (aGroupStack->IsEmpty()) {
    return false;
  }

  aName = aGroupStack->PopLastElement();
  return true;
}

Console::TimerStatus Console::StartTimer(JSContext* aCx, const JS::Value& aName,
                                         DOMHighResTimeStamp aTimestamp,
                                         nsAString& aTimerLabel,
                                         DOMHighResTimeStamp* aTimerValue) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aTimerValue);

  *aTimerValue = 0;

  if (NS_WARN_IF(mTimerRegistry.Count() >= MAX_PAGE_TIMERS)) {
    return eTimerMaxReached;
  }

  JS::Rooted<JS::Value> name(aCx, aName);
  JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, name));
  if (NS_WARN_IF(!jsString)) {
    return eTimerJSException;
  }

  nsAutoJSString label;
  if (NS_WARN_IF(!label.init(aCx, jsString))) {
    return eTimerJSException;
  }

  aTimerLabel = label;

  if (mTimerRegistry.WithEntryHandle(label, [&](auto&& entry) {
        if (entry) {
          return true;
        }
        entry.Insert(aTimestamp);
        return false;
      })) {
    return eTimerAlreadyExists;
  }

  *aTimerValue = aTimestamp;
  return eTimerDone;
}

/* static */
JS::Value Console::CreateStartTimerValue(JSContext* aCx,
                                         const nsAString& aTimerLabel,
                                         TimerStatus aTimerStatus) {
  MOZ_ASSERT(aTimerStatus != eTimerUnknown);

  if (aTimerStatus != eTimerDone) {
    return CreateTimerError(aCx, aTimerLabel, aTimerStatus);
  }

  RootedDictionary<ConsoleTimerStart> timer(aCx);

  timer.mName = aTimerLabel;

  JS::Rooted<JS::Value> value(aCx);
  if (!ToJSValue(aCx, timer, &value)) {
    return JS::UndefinedValue();
  }

  return value;
}

Console::TimerStatus Console::LogTimer(JSContext* aCx, const JS::Value& aName,
                                       DOMHighResTimeStamp aTimestamp,
                                       nsAString& aTimerLabel,
                                       double* aTimerDuration,
                                       bool aCancelTimer) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aTimerDuration);

  *aTimerDuration = 0;

  JS::Rooted<JS::Value> name(aCx, aName);
  JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, name));
  if (NS_WARN_IF(!jsString)) {
    return eTimerJSException;
  }

  nsAutoJSString key;
  if (NS_WARN_IF(!key.init(aCx, jsString))) {
    return eTimerJSException;
  }

  aTimerLabel = key;

  DOMHighResTimeStamp value = 0;

  if (aCancelTimer) {
    if (!mTimerRegistry.Remove(key, &value)) {
      NS_WARNING("mTimerRegistry entry not found");
      return eTimerDoesntExist;
    }
  } else {
    if (!mTimerRegistry.Get(key, &value)) {
      NS_WARNING("mTimerRegistry entry not found");
      return eTimerDoesntExist;
    }
  }

  *aTimerDuration = aTimestamp - value;

  if (aCancelTimer && profiler_thread_is_being_profiled_for_markers()) {
    mozilla::TimeStamp creationTimeStamp = GetCreationTimeStamp();
    if (!creationTimeStamp.IsNull()) {
      mozilla::TimeStamp startTimeStamp =
          creationTimeStamp + TimeDuration::FromMilliseconds(value);
      mozilla::TimeStamp endTimeStamp =
          creationTimeStamp + TimeDuration::FromMilliseconds(aTimestamp);
      profiler_add_marker("ConsoleTiming", geckoprofiler::category::DOM,
                          MarkerTiming::Interval(startTimeStamp, endTimeStamp),
                          ConsoleTimingMarker{},
                          NS_ConvertUTF16toUTF8(aTimerLabel), "time");
    }
  }

  return eTimerDone;
}

/* static */
JS::Value Console::CreateLogOrEndTimerValue(JSContext* aCx,
                                            const nsAString& aLabel,
                                            double aDuration,
                                            TimerStatus aStatus) {
  if (aStatus != eTimerDone) {
    return CreateTimerError(aCx, aLabel, aStatus);
  }

  RootedDictionary<ConsoleTimerLogOrEnd> timer(aCx);
  timer.mName = aLabel;
  timer.mDuration = aDuration;

  JS::Rooted<JS::Value> value(aCx);
  if (!ToJSValue(aCx, timer, &value)) {
    return JS::UndefinedValue();
  }

  return value;
}

/* static */
JS::Value Console::CreateTimerError(JSContext* aCx, const nsAString& aLabel,
                                    TimerStatus aStatus) {
  MOZ_ASSERT(aStatus != eTimerUnknown && aStatus != eTimerDone);

  RootedDictionary<ConsoleTimerError> error(aCx);

  error.mName = aLabel;

  switch (aStatus) {
    case eTimerAlreadyExists:
      error.mError.AssignLiteral("timerAlreadyExists");
      break;

    case eTimerDoesntExist:
      error.mError.AssignLiteral("timerDoesntExist");
      break;

    case eTimerJSException:
      error.mError.AssignLiteral("timerJSError");
      break;

    case eTimerMaxReached:
      error.mError.AssignLiteral("maxTimersExceeded");
      break;

    default:
      MOZ_CRASH("Unsupported status");
      break;
  }

  JS::Rooted<JS::Value> value(aCx);
  if (!ToJSValue(aCx, error, &value)) {
    return JS::UndefinedValue();
  }

  return value;
}

uint32_t Console::IncreaseCounter(JSContext* aCx,
                                  const Sequence<JS::Value>& aArguments,
                                  nsAString& aCountLabel) {
  AssertIsOnOwningThread();

  ConsoleCommon::ClearException ce(aCx);

  MOZ_ASSERT(!aArguments.IsEmpty());

  JS::Rooted<JS::Value> labelValue(aCx, aArguments[0]);
  JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, labelValue));
  if (!jsString) {
    return 0;  // We cannot continue.
  }

  nsAutoJSString string;
  if (!string.init(aCx, jsString)) {
    return 0;  // We cannot continue.
  }

  aCountLabel = string;

  const bool maxCountersReached = mCounterRegistry.Count() >= MAX_PAGE_COUNTERS;
  return mCounterRegistry.WithEntryHandle(
      aCountLabel, [maxCountersReached](auto&& entry) -> uint32_t {
        if (entry) {
          ++entry.Data();
        } else {
          if (maxCountersReached) {
            return MAX_PAGE_COUNTERS;
          }
          entry.Insert(1);
        }
        return entry.Data();
      });
}

uint32_t Console::ResetCounter(JSContext* aCx,
                               const Sequence<JS::Value>& aArguments,
                               nsAString& aCountLabel) {
  AssertIsOnOwningThread();

  ConsoleCommon::ClearException ce(aCx);

  MOZ_ASSERT(!aArguments.IsEmpty());

  JS::Rooted<JS::Value> labelValue(aCx, aArguments[0]);
  JS::Rooted<JSString*> jsString(aCx, JS::ToString(aCx, labelValue));
  if (!jsString) {
    return 0;  // We cannot continue.
  }

  nsAutoJSString string;
  if (!string.init(aCx, jsString)) {
    return 0;  // We cannot continue.
  }

  aCountLabel = string;

  if (mCounterRegistry.Remove(aCountLabel)) {
    return 0;
  }

  // Let's return something different than 0 if the key doesn't exist.
  return MAX_PAGE_COUNTERS;
}

// This method generates a ConsoleCounter dictionary as JS::Value. If
// aCountValue is == MAX_PAGE_COUNTERS it generates a ConsoleCounterError
// instead. See IncreaseCounter.
// * aCx - this is the context that will root the returned value.
// * aCountLabel - this label must be what IncreaseCounter received as
//                 aTimerLabel.
// * aCountValue - the return value of IncreaseCounter.
static JS::Value CreateCounterOrResetCounterValue(JSContext* aCx,
                                                  const nsAString& aCountLabel,
                                                  uint32_t aCountValue) {
  ConsoleCommon::ClearException ce(aCx);

  if (aCountValue == MAX_PAGE_COUNTERS) {
    RootedDictionary<ConsoleCounterError> error(aCx);
    error.mLabel = aCountLabel;
    error.mError.AssignLiteral("counterDoesntExist");

    JS::Rooted<JS::Value> value(aCx);
    if (!ToJSValue(aCx, error, &value)) {
      return JS::UndefinedValue();
    }

    return value;
  }

  RootedDictionary<ConsoleCounter> data(aCx);
  data.mLabel = aCountLabel;
  data.mCount = aCountValue;

  JS::Rooted<JS::Value> value(aCx);
  if (!ToJSValue(aCx, data, &value)) {
    return JS::UndefinedValue();
  }

  return value;
}

/* static */
bool Console::ShouldIncludeStackTrace(MethodName aMethodName) {
  switch (aMethodName) {
    case MethodError:
    case MethodException:
    case MethodAssert:
    case MethodTrace:
      return true;
    default:
      return false;
  }
}

JSObject* MainThreadConsoleData::GetOrCreateSandbox(JSContext* aCx,
                                                    nsIPrincipal* aPrincipal) {
  AssertIsOnMainThread();

  if (!mSandbox) {
    nsIXPConnect* xpc = nsContentUtils::XPConnect();
    MOZ_ASSERT(xpc, "This should never be null!");

    JS::Rooted<JSObject*> sandbox(aCx);
    nsresult rv = xpc->CreateSandbox(aCx, aPrincipal, sandbox.address());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    mSandbox = new JSObjectHolder(aCx, sandbox);
  }

  return mSandbox->GetJSObject();
}

bool Console::StoreCallData(JSContext* aCx, ConsoleCallData* aCallData,
                            const Sequence<JS::Value>& aArguments) {
  AssertIsOnOwningThread();

  if (mIsRetrievingConsoleEvent) {
    return false;
  }

  if (NS_WARN_IF(!mArgumentStorage.growBy(1))) {
    return false;
  }
  if (!mArgumentStorage.end()[-1].Initialize(aCx, aArguments)) {
    mArgumentStorage.shrinkBy(1);
    return false;
  }

  MOZ_ASSERT(aCallData);
  MOZ_ASSERT(!mCallDataStorage.Contains(aCallData));

  mCallDataStorage.AppendElement(aCallData);

  MOZ_ASSERT(mCallDataStorage.Length() == mArgumentStorage.length());

  if (mCallDataStorage.Length() > STORAGE_MAX_EVENTS) {
    mCallDataStorage.RemoveElementAt(0);
    mArgumentStorage.erase(&mArgumentStorage[0]);
  }
  return true;
}

void Console::UnstoreCallData(ConsoleCallData* aCallData) {
  AssertIsOnOwningThread();

  MOZ_ASSERT(aCallData);
  MOZ_ASSERT(mCallDataStorage.Length() == mArgumentStorage.length());

  if (mIsRetrievingConsoleEvent) {
    return;
  }

  size_t index = mCallDataStorage.IndexOf(aCallData);
  // It can be that mCallDataStorage has been already cleaned in case the
  // processing of the argument of some Console methods triggers the
  // window.close().
  if (index == mCallDataStorage.NoIndex) {
    return;
  }

  mCallDataStorage.RemoveElementAt(index);
  mArgumentStorage.erase(&mArgumentStorage[index]);
}

void Console::NotifyHandler(JSContext* aCx,
                            const Sequence<JS::Value>& aArguments,
                            ConsoleCallData* aCallData) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aCallData);

  if (!mConsoleEventNotifier) {
    return;
  }

  JS::Rooted<JS::Value> value(aCx);

  JS::Rooted<JSObject*> callableGlobal(
      aCx, mConsoleEventNotifier->CallbackGlobalOrNull());
  if (NS_WARN_IF(!callableGlobal)) {
    return;
  }

  // aCx and aArguments are in the same compartment because this method is
  // called directly when a Console.something() runs.
  // mConsoleEventNotifier->CallbackGlobal() is the scope where value will be
  // sent to.
  if (NS_WARN_IF(!PopulateConsoleNotificationInTheTargetScope(
          aCx, aArguments, callableGlobal, &value, aCallData, &mGroupStack))) {
    return;
  }

  JS::Rooted<JS::Value> ignored(aCx);
  RefPtr<AnyCallback> notifier(mConsoleEventNotifier);
  notifier->Call(value, &ignored);
}

void Console::RetrieveConsoleEvents(JSContext* aCx,
                                    nsTArray<JS::Value>& aEvents,
                                    ErrorResult& aRv) {
  AssertIsOnOwningThread();

  // We don't want to expose this functionality to main-thread yet.
  MOZ_ASSERT(!NS_IsMainThread());

  JS::Rooted<JSObject*> targetScope(aCx, JS::CurrentGlobalOrNull(aCx));

  AutoRestore<bool> retrievingGuard(mIsRetrievingConsoleEvent);
  mIsRetrievingConsoleEvent = true;

  for (uint32_t i = 0; i < mArgumentStorage.length(); ++i) {
    JS::Rooted<JS::Value> value(aCx);

    JS::Rooted<JSObject*> sequenceScope(aCx, mArgumentStorage[i].Global());
    JSAutoRealm ar(aCx, sequenceScope);

    Sequence<JS::Value> sequence;
    SequenceRooter<JS::Value> arguments(aCx, &sequence);

    if (!mArgumentStorage[i].PopulateArgumentsSequence(sequence)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    // Here we have aCx and sequence in the same compartment.
    // targetScope is the destination scope and value will be populated in its
    // compartment.
    {
      RefPtr<ConsoleCallData> callData = mCallDataStorage[i];
      MutexAutoLock lock(callData->mMutex);
      if (NS_WARN_IF(!PopulateConsoleNotificationInTheTargetScope(
              aCx, sequence, targetScope, &value, callData, &mGroupStack))) {
        aRv.Throw(NS_ERROR_FAILURE);
        return;
      }
    }

    aEvents.AppendElement(value);
  }
}

void Console::SetConsoleEventHandler(AnyCallback* aHandler) {
  AssertIsOnOwningThread();

  // We don't want to expose this functionality to main-thread yet.
  MOZ_ASSERT(!NS_IsMainThread());

  mConsoleEventNotifier = aHandler;
}

void Console::AssertIsOnOwningThread() const {
  NS_ASSERT_OWNINGTHREAD(Console);
}

bool Console::IsShuttingDown() const {
  MOZ_ASSERT(mStatus != eUnknown);
  return mStatus == eShuttingDown;
}

/* static */
already_AddRefed<Console> Console::GetConsole(const GlobalObject& aGlobal) {
  ErrorResult rv;
  RefPtr<Console> console = GetConsoleInternal(aGlobal, rv);
  if (NS_WARN_IF(rv.Failed()) || !console) {
    rv.SuppressException();
    return nullptr;
  }

  console->AssertIsOnOwningThread();

  if (console->IsShuttingDown()) {
    return nullptr;
  }

  return console.forget();
}

/* static */
already_AddRefed<Console> Console::GetConsoleInternal(
    const GlobalObject& aGlobal, ErrorResult& aRv) {
  // Window
  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> innerWindow =
        do_QueryInterface(aGlobal.GetAsSupports());

    // we are probably running a chrome script.
    if (!innerWindow) {
      RefPtr<Console> console = new Console(aGlobal.Context(), nullptr, 0, 0);
      console->Initialize(aRv);
      if (NS_WARN_IF(aRv.Failed())) {
        return nullptr;
      }

      return console.forget();
    }

    nsGlobalWindowInner* window = nsGlobalWindowInner::Cast(innerWindow);
    return window->GetConsole(aGlobal.Context(), aRv);
  }

  // Worklet
  nsCOMPtr<WorkletGlobalScope> workletScope =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (workletScope) {
    WorkletThread::AssertIsOnWorkletThread();
    return workletScope->GetConsole(aGlobal.Context(), aRv);
  }

  // Workers
  MOZ_ASSERT(!NS_IsMainThread());

  JSContext* cx = aGlobal.Context();
  WorkerPrivate* workerPrivate = GetWorkerPrivateFromContext(cx);
  MOZ_ASSERT(workerPrivate);

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (NS_WARN_IF(!global)) {
    return nullptr;
  }

  WorkerGlobalScope* scope = workerPrivate->GlobalScope();
  MOZ_ASSERT(scope);

  // Normal worker scope.
  if (scope == global) {
    return scope->GetConsole(aRv);
  }

  // Debugger worker scope

  WorkerDebuggerGlobalScope* debuggerScope =
      workerPrivate->DebuggerGlobalScope();
  MOZ_ASSERT(debuggerScope);
  MOZ_ASSERT(debuggerScope == global, "Which kind of global do we have?");

  return debuggerScope->GetConsole(aRv);
}

bool Console::MonotonicTimer(JSContext* aCx, MethodName aMethodName,
                             const Sequence<JS::Value>& aData,
                             DOMHighResTimeStamp* aTimeStamp) {
  if (nsCOMPtr<nsPIDOMWindowInner> innerWindow = do_QueryInterface(mGlobal)) {
    nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(innerWindow);
    MOZ_ASSERT(win);

    RefPtr<Performance> performance = win->GetPerformance();
    if (!performance) {
      return false;
    }

    *aTimeStamp = performance->Now();
    return true;
  }

  if (NS_IsMainThread()) {
    *aTimeStamp = (TimeStamp::Now() - mCreationTimeStamp).ToMilliseconds();
    return true;
  }

  if (nsCOMPtr<WorkletGlobalScope> workletGlobal = do_QueryInterface(mGlobal)) {
    *aTimeStamp = workletGlobal->TimeStampToDOMHighRes(TimeStamp::Now());
    return true;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);

  *aTimeStamp = workerPrivate->TimeStampToDOMHighRes(TimeStamp::Now());
  return true;
}

mozilla::TimeStamp Console::GetCreationTimeStamp() const {
  if (nsCOMPtr<nsPIDOMWindowInner> innerWindow = do_QueryInterface(mGlobal)) {
    nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(innerWindow);
    MOZ_ASSERT(win);

    RefPtr<Performance> performance = win->GetPerformance();
    if (performance) {
      return performance->CreationTimeStamp();
    }
    return mozilla::TimeStamp();
  }

  if (NS_IsMainThread()) {
    return mCreationTimeStamp;
  }

  if (nsCOMPtr<WorkletGlobalScope> workletGlobal = do_QueryInterface(mGlobal)) {
    return workletGlobal->CreationTimeStamp();
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  if (workerPrivate) {
    return workerPrivate->CreationTimeStamp();
  }

  return mozilla::TimeStamp();
}

/* static */
already_AddRefed<ConsoleInstance> Console::CreateInstance(
    const GlobalObject& aGlobal, const ConsoleInstanceOptions& aOptions) {
  RefPtr<ConsoleInstance> console =
      new ConsoleInstance(aGlobal.Context(), aOptions);
  return console.forget();
}

void Console::StringifyElement(Element* aElement, nsAString& aOut) {
  aOut.AppendLiteral("<");
  aOut.Append(aElement->LocalName());
  uint32_t attrCount = aElement->GetAttrCount();
  nsAutoString idAttr;
  nsAutoString classAttr;
  nsAutoString nameAttr;
  nsAutoString otherAttrs;
  for (uint32_t i = 0; i < attrCount; i++) {
    BorrowedAttrInfo attrInfo = aElement->GetAttrInfoAt(i);
    nsAutoString attrValue;
    attrInfo.mValue->ToString(attrValue);

    const nsAttrName* attrName = attrInfo.mName;
    if (attrName->Equals(nsGkAtoms::id)) {
      idAttr.AppendLiteral(" id=\"");
      idAttr.Append(attrValue);
      idAttr.AppendLiteral("\"");
    } else if (attrName->Equals(nsGkAtoms::_class)) {
      classAttr.AppendLiteral(" class=\"");
      classAttr.Append(attrValue);
      classAttr.AppendLiteral("\"");
    } else if (attrName->Equals(nsGkAtoms::name)) {
      nameAttr.AppendLiteral(" name=\"");
      nameAttr.Append(attrValue);
      nameAttr.AppendLiteral("\"");
    } else {
      nsAutoString attrNameStr;
      attrName->GetQualifiedName(attrNameStr);
      otherAttrs.AppendLiteral(" ");
      otherAttrs.Append(attrNameStr);
      otherAttrs.AppendLiteral("=\"");
      otherAttrs.Append(attrValue);
      otherAttrs.AppendLiteral("\"");
    }
  }
  if (!idAttr.IsEmpty()) {
    aOut.Append(idAttr);
  }
  if (!classAttr.IsEmpty()) {
    aOut.Append(classAttr);
  }
  if (!nameAttr.IsEmpty()) {
    aOut.Append(nameAttr);
  }
  if (!otherAttrs.IsEmpty()) {
    aOut.Append(otherAttrs);
  }
  aOut.AppendLiteral(">");
}

void Console::LogToMozLog(JSContext* aCx, MethodName aMethodName,
                          const nsAString& aMethodString,
                          const Sequence<JS::Value>& aData,
                          nsIStackFrame* aStack,
                          DOMHighResTimeStamp aMonotonicTimer) {
  nsString message = GetDumpMessage(aCx, aMethodName, aMethodString, aData,
                                    aStack, aMonotonicTimer, true);

  MOZ_LOG(mLogModule, ConsoleMethodNameToMozLog(aMethodName),
          ("%s", NS_ConvertUTF16toUTF8(message).get()));
}

void Console::MaybeExecuteDumpFunction(JSContext* aCx, MethodName aMethodName,
                                       const nsAString& aMethodString,
                                       const Sequence<JS::Value>& aData,
                                       nsIStackFrame* aStack,
                                       DOMHighResTimeStamp aMonotonicTimer) {
  if (!mDumpFunction && !mDumpToStdout) {
    return;
  }
  nsString message = GetDumpMessage(aCx, aMethodName, aMethodString, aData,
                                    aStack, aMonotonicTimer, false);

  ExecuteDumpFunction(message);
}

nsString Console::GetDumpMessage(JSContext* aCx, MethodName aMethodName,
                                 const nsAString& aMethodString,
                                 const Sequence<JS::Value>& aData,
                                 nsIStackFrame* aStack,
                                 DOMHighResTimeStamp aMonotonicTimer,
                                 bool aIsForMozLog) {
  const nsString& prefix = aIsForMozLog ? mMozLogPrefix : mConsolePrefix;
  nsString message = (aIsForMozLog ? u""_ns : u"console."_ns) + aMethodString +
                     u": "_ns + prefix + (prefix.IsEmpty() ? u""_ns : u": "_ns);

  for (uint32_t i = 0; i < aData.Length(); ++i) {
    JS::Rooted<JS::Value> v(aCx, aData[i]);
    if (v.isObject()) {
      Element* element = nullptr;
      if (NS_SUCCEEDED(UNWRAP_OBJECT(Element, &v, element))) {
        if (i != 0) {
          message.AppendLiteral(" ");
        }
        StringifyElement(element, message);
        continue;
      }
    }

    JS::Rooted<JSString*> jsString(aCx, JS_ValueToSource(aCx, v));
    if (!jsString) {
      continue;
    }

    nsAutoJSString string;
    if (NS_WARN_IF(!string.init(aCx, jsString))) {
      return message;
    }

    if (i != 0) {
      message.AppendLiteral(" ");
    }

    if (string.EqualsLiteral("({})")) {
      // ({}) is a generic serialization, possibly from XPC_WN_Shared_ToSource.
      // Such a serialization is rather unhelpful, so try .toString() instead.
      // When the input is a Components.Exception instance, the result will
      // contain the error message, code and stack, which eases debugging.
      JS::Rooted<JSString*> jsString2(aCx, JS::ToString(aCx, v));
      nsAutoJSString string2;
      if (jsString2 && string2.init(aCx, jsString2)) {
        message.Append(string2);
        continue;
      }
    }
    message.Append(string);
  }

  if (aMethodName == MethodTime || aMethodName == MethodTimeEnd) {
    message.AppendLiteral(" @ ");
    message.AppendFloat(aMonotonicTimer);
  }

  message.AppendLiteral("\n");

  // When logging to MOZ_LOG, only dump stack willingly when MOZ_LOG includes
  // "jsstacks".
  if (!aIsForMozLog || mLogModule->GetLogJSStacks()) {
    // aStack can be null.
    nsCOMPtr<nsIStackFrame> stack(aStack);

    while (stack) {
      nsAutoCString filename;
      stack->GetFilename(aCx, filename);

      AppendUTF8toUTF16(filename, message);
      message.AppendLiteral(" ");

      message.AppendInt(stack->GetLineNumber(aCx));
      message.AppendLiteral(" ");

      nsAutoString functionName;
      stack->GetName(aCx, functionName);

      message.Append(functionName);
      message.AppendLiteral("\n");

      nsCOMPtr<nsIStackFrame> caller = stack->GetCaller(aCx);

      if (!caller) {
        caller = stack->GetAsyncCaller(aCx);
      }

      stack.swap(caller);
    }
  }

  return message;
}

void Console::ExecuteDumpFunction(const nsAString& aMessage) {
  if (mDumpFunction) {
    RefPtr<ConsoleInstanceDumpCallback> dumpFunction(mDumpFunction);
    dumpFunction->Call(aMessage);
    return;
  }

  NS_ConvertUTF16toUTF8 str(aMessage);
  MOZ_LOG(nsContentUtils::DOMDumpLog(), LogLevel::Debug, ("%s", str.get()));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "Gecko", "%s", str.get());
#endif
  fputs(str.get(), stdout);
  fflush(stdout);
}

bool Console::ShouldLogToMozLog(MethodName aName) const {
  return mLogModule->ShouldLog(ConsoleMethodNameToMozLog(aName));
}

bool Console::ShouldLogToMozLog(ConsoleLogLevel aLevel) const {
  return mLogModule->ShouldLog(
      ConsoleLevelIntegerToMozLog(WebIDLLogLevelToInteger(aLevel)));
}

bool Console::ShouldProceed(MethodName aName) const {
  return mCurrentLogLevel <= ConsoleMethodNameToInteger(aName);
}

// Maps between log level systems:
// - WebIDLLogLevel (ConsoleInstance.webidl): defines levels for most (not all)
//   console method names, plus "All" and "Off"
// - Console integer level: 1 (verbose) to 5 (critical), used internally
// - MOZ_LOG levels: LogLevel::Verbose (5) to LogLevel::Error (1), reverse order
// Some duplication exists across mapping methods for performance on hot paths.

uint32_t Console::WebIDLLogLevelToInteger(ConsoleLogLevel aLevel) const {
  switch (aLevel) {
    case ConsoleLogLevel::All:
      return 0;
    case ConsoleLogLevel::Trace:
      return 1;
    case ConsoleLogLevel::Debug:
      return 2;
    case ConsoleLogLevel::Clear:
    case ConsoleLogLevel::Dir:
    case ConsoleLogLevel::Dirxml:
    case ConsoleLogLevel::Group:
    case ConsoleLogLevel::GroupEnd:
    case ConsoleLogLevel::Info:
    case ConsoleLogLevel::Log:
    case ConsoleLogLevel::Profile:
    case ConsoleLogLevel::ProfileEnd:
    case ConsoleLogLevel::Time:
    case ConsoleLogLevel::TimeEnd:
    case ConsoleLogLevel::TimeLog:
      return 3;
    case ConsoleLogLevel::Warn:
      return 4;
    case ConsoleLogLevel::Error:
      return 5;
    case ConsoleLogLevel::Off:
      return UINT32_MAX;
    default:
      MOZ_CRASH(
          "ConsoleLogLevel is out of sync with the Console implementation!");
      return 0;
  }
}

uint32_t Console::ConsoleMethodNameToInteger(MethodName aName) const {
  switch (aName) {
    case MethodTrace:
      return 1;
    case MethodDebug:
      return 2;
    case MethodClear:
    case MethodCount:
    case MethodCountReset:
    case MethodDir:
    case MethodDirxml:
    case MethodGroup:
    case MethodGroupCollapsed:
    case MethodGroupEnd:
    case MethodInfo:
    case MethodLog:
    case MethodProfile:
    case MethodProfileEnd:
    case MethodTable:
    case MethodTime:
    case MethodTimeEnd:
    case MethodTimeLog:
    case MethodTimeStamp:
      return 3;
    case MethodWarn:
      return 4;
    case MethodAssert:
    case MethodError:
    case MethodException:
      return 5;
    default:
      MOZ_CRASH("MethodName is out of sync with the Console implementation!");
      return 0;
  }
}

LogLevel Console::ConsoleMethodNameToMozLog(MethodName aName) const {
  switch (aName) {
    case MethodTrace:
      return LogLevel::Verbose;
    case MethodDebug:
      return LogLevel::Debug;
    case MethodClear:
    case MethodCount:
    case MethodCountReset:
    case MethodDir:
    case MethodDirxml:
    case MethodGroup:
    case MethodGroupCollapsed:
    case MethodGroupEnd:
    case MethodInfo:
    case MethodLog:
    case MethodProfile:
    case MethodProfileEnd:
    case MethodTable:
    case MethodTime:
    case MethodTimeEnd:
    case MethodTimeLog:
    case MethodTimeStamp:
      return LogLevel::Info;
    case MethodWarn:
      return LogLevel::Warning;
    case MethodAssert:
    case MethodError:
    case MethodException:
      return LogLevel::Error;
    default:
      MOZ_CRASH("MethodName is out of sync with the Console implementation!");
      return LogLevel::Disabled;
  }
}

LogLevel Console::ConsoleLevelIntegerToMozLog(uint32_t aLevel) const {
  switch (aLevel) {
    case 5:
      return LogLevel::Error;
    case 4:
      return LogLevel::Warning;
    case 3:
      return LogLevel::Info;
    case 2:
      return LogLevel::Debug;
    case 1:
      return LogLevel::Verbose;
    case 0:
      return LogLevel::Verbose;
    case UINT32_MAX:
      return LogLevel::Disabled;
    default:
      MOZ_CRASH(
          "Unexpected console integer level in the Console implementation!");
      return LogLevel::Disabled;
  }
}

bool Console::ArgumentData::Initialize(JSContext* aCx,
                                       const Sequence<JS::Value>& aArguments) {
  mGlobal = JS::CurrentGlobalOrNull(aCx);

  if (NS_WARN_IF(!mArguments.AppendElements(aArguments, fallible))) {
    return false;
  }

  return true;
}

void Console::ArgumentData::Trace(const TraceCallbacks& aCallbacks,
                                  void* aClosure) {
  ArgumentData* tmp = this;
  for (uint32_t i = 0; i < mArguments.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mArguments[i])
  }

  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mGlobal)
}

bool Console::ArgumentData::PopulateArgumentsSequence(
    Sequence<JS::Value>& aSequence) const {
  AssertIsOnOwningThread();

  for (uint32_t i = 0; i < mArguments.Length(); ++i) {
    if (NS_WARN_IF(!aSequence.AppendElement(mArguments[i], fallible))) {
      return false;
    }
  }

  return true;
}

}  // namespace mozilla::dom
