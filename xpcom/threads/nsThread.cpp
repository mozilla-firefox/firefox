/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsThread.h"

#include "base/message_loop.h"
#include "base/platform_thread.h"

// Chromium's logging can sometimes leak through...
#ifdef LOG
#  undef LOG
#endif

#include "GeckoProfiler.h"
#include "ThreadDelay.h"
#include "ThreadEventQueue.h"
#include "ThreadEventTarget.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/ChaosMode.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerRunnable.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticLocalPtr.h"
#include "mozilla/StaticPrefs_threads.h"
#include "mozilla/TaskController.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/glean/XpcomMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/MessageChannel.h"
#include "nsCOMPtr.h"
#include "nsExceptionHandler.h"
#include "nsFmtString.h"
#include "nsIClassInfoImpl.h"
#include "nsIObserverService.h"
#include "nsMemoryPressure.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadManager.h"
#include "nsThreadSyncDispatch.h"
#include "nsXPCOMPrivate.h"
#include "pratom.h"
#include "prerror.h"

#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED
#  include "mozilla/JSONStringWriteFuncs.h"
#  include "mozilla/JSONWriter.h"
#  include "prtime.h"
#endif

#ifdef XP_LINUX
#  ifdef __GLIBC__
#    include <gnu/libc-version.h>
#  endif
#  include <sched.h>
#  include <stdio.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#  include <sys/time.h>
#endif

#ifdef XP_WIN
#  include <winbase.h>

#  include "mozilla/DynamicallyLinkedFunctionPtr.h"

using GetCurrentThreadStackLimitsFn = void(WINAPI*)(PULONG_PTR LowLimit,
                                                    PULONG_PTR HighLimit);
#endif

#define HAVE_UALARM                                                        \
  _BSD_SOURCE ||                                                           \
      (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
          !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)

#if defined(XP_LINUX) && !defined(ANDROID) && defined(_GNU_SOURCE)
#  define HAVE_SCHED_SETAFFINITY
#endif

#ifdef XP_MACOSX
#  include <mach/mach.h>
#  include <mach/thread_policy.h>
#  include <sys/qos.h>

#  include "nsCocoaFeatures.h"
#endif

#ifdef MOZ_CANARY
#  include <execinfo.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <unistd.h>

#  include "nsXULAppAPI.h"
#endif

using namespace mozilla;

extern void InitThreadLocalVariables();

static LazyLogModule sThreadLog("nsThread");
#ifdef LOG
#  undef LOG
#endif
#define LOG(args) MOZ_LOG(sThreadLog, mozilla::LogLevel::Debug, args)

NS_DECL_CI_INTERFACE_GETTER(nsThread)

Array<char, nsThread::kRunnableNameBufSize> nsThread::sMainThreadRunnableName;

#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED

/* static */ StaticMutex nsThread::sShutdownAnnotationMutex;

static constexpr const char* PhaseName(
    nsThread::ShutdownAnnotationPhase aPhase) {
  switch (aPhase) {
    case nsThread::ShutdownAnnotationPhase::None:
      return "None";
    case nsThread::ShutdownAnnotationPhase::Joining:
      return "Joining";
    case nsThread::ShutdownAnnotationPhase::Recv:
      return "Recv";
    case nsThread::ShutdownAnnotationPhase::Ack:
      return "Ack";
  }
  return "?";
}

/* static */ nsCString nsThread::BuildShutdownAnnotationJson(
    const nsTArray<ShutdownHandshakeInfo>& aEntries) {
  nsCString out;
  JSONStringRefWriteFunc sink(out);
  JSONWriter w(sink);
  w.StartArrayElement();
  for (const auto& e : aEntries) {
    w.StartObjectElement();
    w.IntProperty("joining_tid", e.mJoiningTid);
    w.StringProperty("joining_name", MakeStringSpan(e.mJoiningName.get()));
    w.IntProperty("closing_tid", e.mClosingTid);
    w.StringProperty("closing_name", MakeStringSpan(e.mClosingName.get()));
    w.StringProperty("phase", MakeStringSpan(PhaseName(e.mPhase)));
    w.IntProperty("phase_since", e.mPhaseSinceEpochSec);
    w.EndObject();
  }
  w.EndArray();
  return out;
}

/* static */ nsCString nsThread::BuildContendedAnnotationJson() {
  nsCString out;
  JSONStringRefWriteFunc sink(out);
  JSONWriter w(sink);
  w.StartArrayElement();
  w.StartObjectElement();
  w.StringProperty("error", "contended");
  w.EndObject();
  w.EndArray();
  return out;
}

#endif  // NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED

//-----------------------------------------------------------------------------
// Because we do not have our own nsIFactory, we have to implement nsIClassInfo
// somewhat manually.

class nsThreadClassInfo : public nsIClassInfo {
 public:
  NS_DECL_ISUPPORTS_INHERITED  // no mRefCnt
      NS_DECL_NSICLASSINFO

      nsThreadClassInfo() = default;
};

NS_IMETHODIMP_(MozExternalRefCountType)
nsThreadClassInfo::AddRef() { return 2; }
NS_IMETHODIMP_(MozExternalRefCountType)
nsThreadClassInfo::Release() { return 1; }
NS_IMPL_QUERY_INTERFACE(nsThreadClassInfo, nsIClassInfo)

NS_IMETHODIMP
nsThreadClassInfo::GetInterfaces(nsTArray<nsIID>& aArray) {
  return NS_CI_INTERFACE_GETTER_NAME(nsThread)(aArray);
}

NS_IMETHODIMP
nsThreadClassInfo::GetScriptableHelper(nsIXPCScriptable** aResult) {
  *aResult = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadClassInfo::GetContractID(nsACString& aResult) {
  aResult.SetIsVoid(true);
  return NS_OK;
}

NS_IMETHODIMP
nsThreadClassInfo::GetClassDescription(nsACString& aResult) {
  aResult.SetIsVoid(true);
  return NS_OK;
}

NS_IMETHODIMP
nsThreadClassInfo::GetClassID(nsCID** aResult) {
  *aResult = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadClassInfo::GetFlags(uint32_t* aResult) {
  *aResult = THREADSAFE;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadClassInfo::GetClassIDNoAlloc(nsCID* aResult) {
  return NS_ERROR_NOT_AVAILABLE;
}

//-----------------------------------------------------------------------------

NS_IMPL_ADDREF(nsThread)
NS_IMPL_RELEASE(nsThread)
NS_INTERFACE_MAP_BEGIN(nsThread)
  NS_INTERFACE_MAP_ENTRY(nsIThread)
  NS_INTERFACE_MAP_ENTRY(nsIThreadInternal)
  NS_INTERFACE_MAP_ENTRY(nsIEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsISerialEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsISupportsPriority)
  NS_INTERFACE_MAP_ENTRY(nsIDirectTaskDispatcher)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIThread)
  if (aIID.Equals(NS_GET_IID(nsIClassInfo))) {
    static nsThreadClassInfo sThreadClassInfo;
    foundInterface = static_cast<nsIClassInfo*>(&sThreadClassInfo);
  } else
NS_INTERFACE_MAP_END
NS_IMPL_CI_INTERFACE_GETTER(nsThread, nsIThread, nsIThreadInternal,
                            nsIEventTarget, nsISerialEventTarget,
                            nsISupportsPriority)

//-----------------------------------------------------------------------------

// This event is responsible for notifying nsThread::Shutdown that it is time
// to call PR_JoinThread. It implements nsICancelableRunnable so that it can
// run on a DOM Worker thread (where all events must implement
// nsICancelableRunnable.)
class nsThreadShutdownAckEvent : public CancelableRunnable {
 public:
  explicit nsThreadShutdownAckEvent(NotNull<nsThreadShutdownContext*> aCtx)
      : CancelableRunnable("nsThreadShutdownAckEvent"),
        mShutdownContext(aCtx) {}
  NS_IMETHOD Run() override {
    mShutdownContext->mTerminatingThread->ShutdownComplete(mShutdownContext);
    return NS_OK;
  }
  nsresult Cancel() override { return Run(); }

 private:
  virtual ~nsThreadShutdownAckEvent() = default;

  NotNull<RefPtr<nsThreadShutdownContext>> mShutdownContext;
};

// This event is responsible for setting mShutdownContext
class nsThreadShutdownEvent : public Runnable {
 public:
  nsThreadShutdownEvent(NotNull<nsThread*> aThr,
                        NotNull<nsThreadShutdownContext*> aCtx)
      : Runnable("nsThreadShutdownEvent"),
        mThread(aThr),
        mShutdownContext(aCtx) {}
  NS_IMETHOD Run() override {
    // Creates a cycle between `mThread` and the shutdown context which will be
    // broken when the thread exits.
    mThread->mShutdownContext = mShutdownContext;
    MessageLoop::current()->Quit();
#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED
    {
      StaticMutexAutoLock lock(nsThread::sShutdownAnnotationMutex);
      mThread->mShutdownAnnotationPhase =
          nsThread::ShutdownAnnotationPhase::Recv;
      mThread->mShutdownPhaseSinceEpochSec = PR_Now() / PR_USEC_PER_SEC;
    }
#endif
    return NS_OK;
  }

 private:
  NotNull<RefPtr<nsThread>> mThread;
  NotNull<RefPtr<nsThreadShutdownContext>> mShutdownContext;
};

//-----------------------------------------------------------------------------

static void SetThreadAffinity(unsigned int cpu) {
#ifdef HAVE_SCHED_SETAFFINITY
  cpu_set_t cpus;
  CPU_ZERO(&cpus);
  CPU_SET(cpu, &cpus);
  sched_setaffinity(0, sizeof(cpus), &cpus);
  // Don't assert sched_setaffinity's return value because it intermittently (?)
  // fails with EINVAL on Linux x64 try runs.
#elif defined(XP_MACOSX)
  // OS X does not provide APIs to pin threads to specific processors, but you
  // can tag threads as belonging to the same "affinity set" and the OS will try
  // to run them on the same processor. To run threads on different processors,
  // tag them as belonging to different affinity sets. Tag 0, the default, means
  // "no affinity" so let's pretend each CPU has its own tag `cpu+1`.
  thread_affinity_policy_data_t policy;
  policy.affinity_tag = cpu + 1;
  kern_return_t kr = thread_policy_set(
      mach_thread_self(), THREAD_AFFINITY_POLICY, &policy.affinity_tag, 1);
  // Setting the thread affinity is not supported on ARM.
  MOZ_ALWAYS_TRUE(kr == KERN_SUCCESS || kr == KERN_NOT_SUPPORTED);
#elif defined(XP_WIN)
  MOZ_ALWAYS_TRUE(SetThreadIdealProcessor(GetCurrentThread(), cpu) !=
                  (DWORD)-1);
#endif
}

static void SetupCurrentThreadForChaosMode() {
  if (!ChaosMode::isActive(ChaosFeature::ThreadScheduling)) {
    return;
  }

#ifdef XP_LINUX
  // PR_SetThreadPriority doesn't really work since priorities >
  // PR_PRIORITY_NORMAL can't be set by non-root users. Instead we'll just use
  // setpriority(2) to set random 'nice values'. In regular Linux this is only
  // a dynamic adjustment so it still doesn't really do what we want, but tools
  // like 'rr' can be more aggressive about honoring these values.
  // Some of these calls may fail due to trying to lower the priority
  // (e.g. something may have already called setpriority() for this thread).
  // This makes it hard to have non-main threads with higher priority than the
  // main thread, but that's hard to fix. Tools like rr can choose to honor the
  // requested values anyway.
  // Use just 4 priorities so there's a reasonable chance of any two threads
  // having equal priority.
  setpriority(PRIO_PROCESS, 0, ChaosMode::randomUint32LessThan(4));
#else
  // We should set the affinity here but NSPR doesn't provide a way to expose
  // it.
  uint32_t priority = ChaosMode::randomUint32LessThan(PR_PRIORITY_LAST + 1);
  PR_SetThreadPriority(PR_GetCurrentThread(), PRThreadPriority(priority));
#endif

  // Force half the threads to CPU 0 so they compete for CPU
  if (ChaosMode::randomUint32LessThan(2)) {
    SetThreadAffinity(0);
  }
}

namespace {

struct ThreadInitData {
  RefPtr<nsThread> thread;
  nsCString name;
};

}  // namespace

void nsThread::MaybeRemoveFromThreadList() {
  nsThreadManager& tm = nsThreadManager::get();
  OffTheBooksMutexAutoLock mal(tm.ThreadListMutex());
  if (isInList()) {
    removeFrom(tm.ThreadList());
  }
}

/*static*/
void nsThread::ThreadFunc(void* aArg) {
  using mozilla::ipc::BackgroundChild;

  UniquePtr<ThreadInitData> initData(static_cast<ThreadInitData*>(aArg));
  RefPtr<nsThread>& self = initData->thread;

  MOZ_ASSERT(self->mEventTarget);
  MOZ_ASSERT(self->mEvents);

  // Note: see the comment in nsThread::Init, where we set these same values.
  DebugOnly<PRThread*> prev = self->mThread.exchange(PR_GetCurrentThread());
  MOZ_ASSERT(!prev || prev == PR_GetCurrentThread());
  self->mEventTarget->SetCurrentThread(self->mThread);
  SetupCurrentThreadForChaosMode();

  if (!initData->name.IsEmpty()) {
    NS_SetCurrentThreadName(initData->name.get());
  }

  self->InitCommon();

#ifdef XP_MACOSX
  if (nsCocoaFeatures::OnTahoeOrLater()) {
    // On macOS 26+, use "User Initiated" as the default quality of service.
    // It may make sense to do this on all versions of macOS.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
  }
#endif

  // Inform the ThreadManager
  nsThreadManager::get().RegisterCurrentThread(*self);

  mozilla::IOInterposer::RegisterCurrentThread();

  // This must come after the call to nsThreadManager::RegisterCurrentThread(),
  // because that call is needed to properly set up this thread as an nsThread,
  // which profiler_register_thread() requires. See bug 1347007.
  const bool registerWithProfiler = !initData->name.IsEmpty();
  if (registerWithProfiler) {
    PROFILER_REGISTER_THREAD(initData->name.get());
  }

  {
    // Scope for MessageLoop.
    MessageLoop loop(
#if defined(XP_WIN) || defined(XP_MACOSX)
        self->mIsUiThread ? MessageLoop::TYPE_MOZILLA_NONMAINUITHREAD
                          : MessageLoop::TYPE_MOZILLA_NONMAINTHREAD,
#else
        MessageLoop::TYPE_MOZILLA_NONMAINTHREAD,
#endif
        self);

    // Now, process incoming events...
    loop.Run();

    self->mEvents->RunShutdownTasks();

    BackgroundChild::CloseForCurrentThread();

    // NB: The main thread does not shut down here!  It shuts down via
    // nsThreadManager::Shutdown.

    // Do NS_ProcessPendingEvents but with special handling to set
    // mEventsAreDoomed atomically with the removal of the last event. The key
    // invariant here is that we will never permit PutEvent to succeed if the
    // event would be left in the queue after our final call to
    // NS_ProcessPendingEvents. We also have to keep processing events as long
    // as we have outstanding mRequestedShutdownContexts.
    while (true) {
      // Check and see if we're waiting on any threads.
      self->WaitForAllAsynchronousShutdowns();

      if (self->mEvents->ShutdownIfNoPendingEvents()) {
        break;
      }
      NS_ProcessPendingEvents(self);
    }
  }

  mozilla::IOInterposer::UnregisterCurrentThread();

  // Inform the threadmanager that this thread is going away
  nsThreadManager::get().UnregisterCurrentThread(*self);

  // The thread should only unregister itself if it was registered above.
  if (registerWithProfiler) {
    PROFILER_UNREGISTER_THREAD();
  }

  NotNull<RefPtr<nsThreadShutdownContext>> context =
      WrapNotNull(self->mShutdownContext);
  self->mShutdownContext = nullptr;
  MOZ_ASSERT(context->mTerminatingThread == self);

  // Take the joining thread from our shutdown context. This may have been
  // cleared by the joining thread if it decided to cancel waiting on us, in
  // which case we won't notify our caller, and leak.
  RefPtr<nsThread> joiningThread;
  {
    MutexAutoLock lock(context->mJoiningThreadMutex);
    joiningThread = context->mJoiningThread.forget();
    MOZ_RELEASE_ASSERT(joiningThread || context->mThreadLeaked);
  }
  if (joiningThread) {
    // Dispatch shutdown ACK
    nsCOMPtr<nsIRunnable> event = new nsThreadShutdownAckEvent(context);
    nsresult dispatch_ack_rv =
        joiningThread->Dispatch(event, NS_DISPATCH_NORMAL);

    // We do not expect this to ever happen, but If we cannot dispatch
    // the ack event, someone probably blocks waiting on us and will
    // crash with a hang later anyways. The best we can do is to tell
    // the world what happened right here.
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(dispatch_ack_rv));

#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED
    {
      StaticMutexAutoLock lock(sShutdownAnnotationMutex);
      self->mShutdownAnnotationPhase = ShutdownAnnotationPhase::Ack;
      self->mShutdownPhaseSinceEpochSec = PR_Now() / PR_USEC_PER_SEC;
    }
#endif
  } else {
    NS_WARNING(
        "nsThread exiting after StopWaitingAndLeakThread was called, thread "
        "resources will be leaked!");
  }

  // Release any observer of the thread here.
  self->SetObserver(nullptr);

  // The PRThread will be deleted in PR_JoinThread(), so clear references.
  self->mThread = nullptr;
  self->mEventTarget->ClearCurrentThread();
}

void nsThread::InitCommon() {
  mThreadId = uint32_t(PlatformThread::CurrentId());

  {
#if defined(XP_LINUX)
    pthread_attr_t attr;
    int res = pthread_attr_init(&attr);
    MOZ_RELEASE_ASSERT(!res);
    res = pthread_getattr_np(pthread_self(), &attr);
    MOZ_RELEASE_ASSERT(!res);

    size_t stackSize;
    res = pthread_attr_getstack(&attr, &mStackBase, &stackSize);
    MOZ_RELEASE_ASSERT(!res);

    // Glibc prior to 2.27 reports the stack size and base including the guard
    // region, so we need to compensate for it to get accurate accounting.
    // Also, this behavior difference isn't guarded by a versioned symbol, so we
    // actually need to check the runtime glibc version, not the version we were
    // compiled against.
    static bool sAdjustForGuardSize = ({
#  ifdef __GLIBC__
      unsigned major, minor;
      sscanf(gnu_get_libc_version(), "%u.%u", &major, &minor) < 2 ||
          major < 2 || (major == 2 && minor < 27);
#  else
      false;
#  endif
    });
    if (sAdjustForGuardSize) {
      size_t guardSize;
      res = pthread_attr_getguardsize(&attr, &guardSize);
      MOZ_RELEASE_ASSERT(!res);

      // Note: This assumes that the stack grows down, as is the case on all of
      // our tier 1 platforms. On platforms where the stack grows up, the
      // mStackBase adjustment is unnecessary, but doesn't cause any harm other
      // than under-counting stack memory usage by one page.
      mStackBase = reinterpret_cast<char*>(mStackBase) + guardSize;
      stackSize -= guardSize;
    }

    mStackSize = stackSize;

    // This is a bit of a hack.
    //
    // We really do want the NOHUGEPAGE flag on our thread stacks, since we
    // don't expect any of them to need anywhere near 2MB of space. But setting
    // it here is too late to have an effect, since the first stack page has
    // already been faulted in existence, and NSPR doesn't give us a way to set
    // it beforehand.
    //
    // What this does get us, however, is a different set of VM flags on our
    // thread stacks compared to normal heap memory. Which makes the Linux
    // kernel report them as separate regions, even when they are adjacent to
    // heap memory. This allows us to accurately track the actual memory
    // consumption of our allocated stacks.
    madvise(mStackBase, stackSize, MADV_NOHUGEPAGE);

    res = pthread_attr_destroy(&attr);
    MOZ_RELEASE_ASSERT(!res);
#elif defined(XP_WIN)
    static const StaticDynamicallyLinkedFunctionPtr<
        GetCurrentThreadStackLimitsFn>
        sGetStackLimits(L"kernel32.dll", "GetCurrentThreadStackLimits");

    if (sGetStackLimits) {
      ULONG_PTR stackBottom, stackTop;
      sGetStackLimits(&stackBottom, &stackTop);
      mStackBase = reinterpret_cast<void*>(stackBottom);
      mStackSize = stackTop - stackBottom;
    }
#endif
  }

  InitThreadLocalVariables();
}

//-----------------------------------------------------------------------------

#ifdef MOZ_CANARY
int sCanaryOutputFD = -1;
#endif

nsThread::nsThread(NotNull<SynchronizedEventQueue*> aQueue,
                   MainThreadFlag aMainThread,
                   nsIThreadManager::ThreadCreationOptions aOptions)
    : mEvents(aQueue.get()),
      mEventTarget(new ThreadEventTarget(
          mEvents.get(), aMainThread == MAIN_THREAD, aOptions.blockDispatch)),
      mOutstandingShutdownContexts(0),
      mShutdownContext(nullptr),
      mScriptObserver(nullptr),
      mThreadName("<uninitialized>"),
      mStackSize(aOptions.stackSize),
      mNestedEventLoopDepth(0),
      mShutdownRequired(false),
      mPriority(PRIORITY_NORMAL),
      mIsMainThread(aMainThread == MAIN_THREAD),
      mUseHangMonitor(aMainThread == MAIN_THREAD),
      mIsUiThread(aOptions.isUiThread),
      mIsAPoolThreadFreePtr(nullptr),
      mCanInvokeJS(false),
      mPerformanceCounterState(mNestedEventLoopDepth, mIsMainThread,
                               aOptions.longTaskLength) {
#if !(defined(XP_WIN) || defined(XP_MACOSX))
  MOZ_ASSERT(!mIsUiThread,
             "Non-main UI threads are only supported on Windows and macOS");
#endif
  if (mIsMainThread) {
    MOZ_ASSERT(!mIsUiThread,
               "Setting isUIThread is not supported for main threads");
    mozilla::TaskController::Get()->SetPerformanceCounterState(
        &mPerformanceCounterState);
  }
}

nsThread::nsThread()
    : mEvents(nullptr),
      mEventTarget(nullptr),
      mOutstandingShutdownContexts(0),
      mShutdownContext(nullptr),
      mScriptObserver(nullptr),
      mThreadName("<uninitialized>"),
      mStackSize(0),
      mNestedEventLoopDepth(0),
      mShutdownRequired(false),
      mPriority(PRIORITY_NORMAL),
      mIsMainThread(false),
      mUseHangMonitor(false),
      mIsUiThread(false),
      mCanInvokeJS(false),
      mPerformanceCounterState(mNestedEventLoopDepth) {
  MOZ_ASSERT(!NS_IsMainThread());
}

nsThread::~nsThread() {
  NS_ASSERTION(mOutstandingShutdownContexts == 0,
               "shouldn't be waiting on other threads to shutdown");

  MaybeRemoveFromThreadList();
}

nsresult nsThread::Init(const nsACString& aName) {
  MOZ_ASSERT(mEvents);
  MOZ_ASSERT(mEventTarget);
  MOZ_ASSERT(!mThread);

  SetThreadNameInternal(aName);

  PRThread* thread = nullptr;

  nsThreadManager& tm = nsThreadManager::get();
  {
    OffTheBooksMutexAutoLock lock(tm.ThreadListMutex());
    if (!tm.AllowNewXPCOMThreadsLocked()) {
      return NS_ERROR_NOT_INITIALIZED;
    }

    // We need to fully start the thread while holding the thread list lock, as
    // the next acquire of the lock could try to shut down this thread (e.g.
    // during xpcom shutdown), which would hang if `PR_CreateThread` failed.

    UniquePtr<ThreadInitData> initData(
        new ThreadInitData{this, nsCString(aName)});

    // ThreadFunc is responsible for setting mThread
    if (!(thread = PR_CreateThread(PR_USER_THREAD, ThreadFunc, initData.get(),
                                   PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                   PR_JOINABLE_THREAD, mStackSize))) {
      PRErrorCode prError = PR_GetError();
      PRInt32 osError = PR_GetOSError();
      CrashReporter::RecordAnnotationNSCString(
          CrashReporter::Annotation::ThreadLastCreateError,
          nsFmtCString("{}: prError={:#x} osError={:#x}", aName, prError,
                       osError));
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // The created thread now owns initData, so release our ownership of it.
    (void)initData.release();

    // The thread has successfully started, so we can mark it as requiring
    // shutdown & add it to the thread list.
    mShutdownRequired = true;
    tm.ThreadList().insertBack(this);
  }

  // Note: we set these both here and inside ThreadFunc, to what should be
  // the same value. This is because calls within ThreadFunc need these values
  // to be set, and our callers need these values to be set.
  DebugOnly<PRThread*> prev = mThread.exchange(thread);
  MOZ_ASSERT(!prev || prev == thread);

  mEventTarget->SetCurrentThread(thread);
  return NS_OK;
}

nsresult nsThread::InitCurrentThread() {
  mThread = PR_GetCurrentThread();

  nsThreadManager& tm = nsThreadManager::get();
  {
    OffTheBooksMutexAutoLock lock(tm.ThreadListMutex());
    // NOTE: We don't check AllowNewXPCOMThreads here, as threads initialized
    // this way do not need shutdown, so are OK to create after nsThreadManager
    // shutdown. In addition, the main thread is initialized this way, which
    // happens before AllowNewXPCOMThreads begins to return true.
    tm.ThreadList().insertBack(this);
  }

  SetupCurrentThreadForChaosMode();
  InitCommon();

  tm.RegisterCurrentThread(*this);
  return NS_OK;
}

void nsThread::GetThreadName(nsACString& aNameBuffer) {
  auto lock = mThreadName.Lock();
  aNameBuffer = lock.ref();
}

void nsThread::SetThreadNameInternal(const nsACString& aName) {
  auto lock = mThreadName.Lock();
  lock->Assign(aName);
}

//-----------------------------------------------------------------------------
// nsIEventTarget

NS_IMETHODIMP
nsThread::DispatchFromScript(nsIRunnable* aEvent, DispatchFlags aFlags) {
  return Dispatch(do_AddRef(aEvent), aFlags);
}

NS_IMETHODIMP
nsThread::Dispatch(already_AddRefed<nsIRunnable> aEvent, DispatchFlags aFlags) {
  MaybeLeakRefPtr<nsIRunnable> event(std::move(aEvent),
                                     aFlags & NS_DISPATCH_FALLIBLE);
  MOZ_ASSERT(mEventTarget);
  NS_ENSURE_TRUE(mEventTarget, NS_ERROR_NOT_IMPLEMENTED);

  LOG(("THRD(%p) Dispatch [%p %x]\n", this, event.get(), aFlags));

  return mEventTarget->Dispatch(event.forget(), aFlags);
}

NS_IMETHODIMP
nsThread::DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                          uint32_t aDelayMs) {
  MOZ_ASSERT(mEventTarget);
  NS_ENSURE_TRUE(mEventTarget, NS_ERROR_NOT_IMPLEMENTED);

  return mEventTarget->DelayedDispatch(std::move(aEvent), aDelayMs);
}

NS_IMETHODIMP
nsThread::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  MOZ_ASSERT(mEventTarget);
  NS_ENSURE_TRUE(mEventTarget, NS_ERROR_NOT_IMPLEMENTED);

  return mEventTarget->RegisterShutdownTask(aTask);
}

NS_IMETHODIMP
nsThread::UnregisterShutdownTask(nsITargetShutdownTask* aTask) {
  MOZ_ASSERT(mEventTarget);
  NS_ENSURE_TRUE(mEventTarget, NS_ERROR_NOT_IMPLEMENTED);

  return mEventTarget->UnregisterShutdownTask(aTask);
}

nsIEventTarget::FeatureFlags nsThread::GetFeatures() {
  return (mIsMainThread ? SUPPORTS_PRIORITIZATION : SUPPORTS_BASE) |
         (SUPPORTS_SHUTDOWN_TASKS | SUPPORTS_SHUTDOWN_TASK_DISPATCH);
}

NS_IMETHODIMP
nsThread::GetRunningEventDelay(TimeDuration* aDelay, TimeStamp* aStart) {
  if (mIsAPoolThreadFreePtr && *mIsAPoolThreadFreePtr) {
    // If there are idle or unstarted threads in the pool, a new event to the
    // pool would not be delayed at all (beyond thread wake / start time).
    *aDelay = TimeDuration();
    *aStart = TimeStamp();
  } else {
    *aDelay = mLastEventDelay;
    *aStart = mLastEventStart;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsThread::SetRunningEventDelay(TimeDuration aDelay, TimeStamp aStart) {
  mLastEventDelay = aDelay;
  mLastEventStart = aStart;
  return NS_OK;
}

NS_IMETHODIMP
nsThread::IsOnCurrentThread(bool* aResult) {
  if (mEventTarget) {
    return mEventTarget->IsOnCurrentThread(aResult);
  }
  *aResult = PR_GetCurrentThread() == mThread;
  return NS_OK;
}

NS_IMETHODIMP_(bool)
nsThread::IsOnCurrentThreadInfallible() {
  // This method is only going to be called if `mThread` is null, which
  // only happens when the thread has exited the event loop.  Therefore, when
  // we are called, we can never be on this thread.
  return false;
}

//-----------------------------------------------------------------------------
// nsIThread

NS_IMETHODIMP
nsThread::GetPRThread(PRThread** aResult) {
  PRThread* thread = mThread;  // atomic load
  *aResult = thread;
  return thread ? NS_OK : NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsThread::GetCanInvokeJS(bool* aResult) {
  *aResult = mCanInvokeJS;
  return NS_OK;
}

NS_IMETHODIMP
nsThread::SetCanInvokeJS(bool aCanInvokeJS) {
  mCanInvokeJS = aCanInvokeJS;
  return NS_OK;
}

NS_IMETHODIMP
nsThread::GetLastLongTaskEnd(TimeStamp* _retval) {
  *_retval = mPerformanceCounterState.LastLongTaskEnd();
  return NS_OK;
}

NS_IMETHODIMP
nsThread::GetLastLongNonIdleTaskEnd(TimeStamp* _retval) {
  *_retval = mPerformanceCounterState.LastLongNonIdleTaskEnd();
  return NS_OK;
}

NS_IMETHODIMP
nsThread::AsyncShutdown() {
  LOG(("THRD(%p) async shutdown\n", this));

  nsCOMPtr<nsIThreadShutdown> shutdown;
  BeginShutdown(getter_AddRefs(shutdown));
  return NS_OK;
}

NS_IMETHODIMP
nsThread::BeginShutdown(nsIThreadShutdown** aShutdown) {
  LOG(("THRD(%p) begin shutdown\n", this));

  MOZ_ASSERT(mEvents);
  MOZ_ASSERT(mEventTarget);
  MOZ_ASSERT(mThread != PR_GetCurrentThread());
  if (NS_WARN_IF(mThread == PR_GetCurrentThread())) {
    return NS_ERROR_UNEXPECTED;
  }

  // Prevent multiple calls to this method.
  if (!mShutdownRequired.compareExchange(true, false)) {
    return NS_ERROR_UNEXPECTED;
  }
  MOZ_ASSERT(mThread);

  RefPtr<nsThread> currentThread = nsThreadManager::get().GetCurrentThread();

  MOZ_DIAGNOSTIC_ASSERT(currentThread->EventQueue(),
                        "Shutdown() may only be called from an XPCOM thread");

  // Allocate a shutdown context, and record that we're waiting for it.
  RefPtr<nsThreadShutdownContext> context =
      new nsThreadShutdownContext(WrapNotNull(this), currentThread);

#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED
  {
    nsAutoCString joiningName;
    currentThread->GetThreadName(joiningName);
    StaticMutexAutoLock lock(sShutdownAnnotationMutex);
    mShutdownAnnotationPhase = ShutdownAnnotationPhase::Joining;
    mShutdownJoiningTid = currentThread->ThreadId();
    mShutdownPhaseSinceEpochSec = PR_Now() / PR_USEC_PER_SEC;
    mShutdownJoiningName = joiningName;
  }
#endif

  ++currentThread->mOutstandingShutdownContexts;

  nsCOMPtr<nsIRunnable> clearOutstanding = NS_NewRunnableFunction(
      "nsThread::ClearOutstandingShutdownContext",
      [currentThread] { --currentThread->mOutstandingShutdownContexts; });
  context->OnCompletion(clearOutstanding);

  // Set mShutdownContext and wake up the thread in case it is waiting for
  // events to process.
  RefPtr<nsIRunnable> event = MakeRefPtr<nsThreadShutdownEvent>(
      WrapNotNull(this), WrapNotNull(context));
  if (!mEvents->PutEvent(event, EventQueuePriority::Normal)) {
    // We do not expect this to happen. Let's collect some diagnostics.
    nsAutoCString threadName;
    GetThreadName(threadName);
    MOZ_CRASH_UNSAFE_PRINTF("Attempt to shutdown an already dead thread: %s",
                            threadName.get());
  }

  // We could still end up with other events being added after the shutdown
  // task, but that's okay because we process pending events in ThreadFunc
  // after setting mShutdownContext just before exiting.
  context.forget(aShutdown);
  return NS_OK;
}

void nsThread::ShutdownComplete(NotNull<nsThreadShutdownContext*> aContext) {
  MOZ_ASSERT(mEvents);
  MOZ_ASSERT(mEventTarget);
  MOZ_ASSERT(aContext->mTerminatingThread == this);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  {
    MutexAutoLock lock(aContext->mJoiningThreadMutex);

    // StopWaitingAndLeakThread is explicitely meant to not cause a
    // nsThreadShutdownAckEvent on the joining thread, which is the only
    // caller of ShutdownComplete.
    MOZ_DIAGNOSTIC_ASSERT(!aContext->mThreadLeaked);
  }
#endif

  MaybeRemoveFromThreadList();

  // Now, it should be safe to join without fear of dead-locking.
  PR_JoinThread(aContext->mTerminatingPRThread);
  MOZ_ASSERT(!mThread);

#ifdef DEBUG
  nsCOMPtr<nsIThreadObserver> obs = mEvents->GetObserver();
  MOZ_ASSERT(!obs, "Should have been cleared at shutdown!");
#endif

  aContext->MarkCompleted();
}

void nsThread::WaitForAllAsynchronousShutdowns() {
  // This is the motivating example for why SpinEventLoopUntil
  // has the template parameter we are providing here.
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "nsThread::WaitForAllAsynchronousShutdowns"_ns,
      [&]() { return mOutstandingShutdownContexts == 0; }, this);
}

#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED
/* static */ Maybe<nsTArray<nsThread::ShutdownHandshakeInfo>>
nsThread::CollectShutdownHandshakes() {
  nsTArray<ShutdownHandshakeInfo> entries;
  nsTArray<nsThread*> closingThreads;
  nsThreadManager& tm = nsThreadManager::get();

  // We can run on the terminator watchdog thread while the main thread is
  // hung, so we must never block: Init() holds the thread list lock across
  // PR_CreateThread, and waiting for it here would keep the watchdog from
  // reaching its crash. Report contention as Nothing so the caller can tell
  // it apart from "no handshakes in flight".
  OffTheBooksMutexAutoTryLock lock(tm.ThreadListMutex());
  if (!lock) {
    return Nothing();
  }

  {
    StaticMutexAutoTryLock alock(sShutdownAnnotationMutex);
    if (!alock) {
      return Nothing();
    }
    for (auto* thread : tm.ThreadList()) {
      if (thread->mShutdownAnnotationPhase == ShutdownAnnotationPhase::None) {
        continue;
      }
      ShutdownHandshakeInfo& info = *entries.AppendElement();
      info.mJoiningTid = thread->mShutdownJoiningTid;
      info.mJoiningName = thread->mShutdownJoiningName;
      info.mClosingTid = thread->mThreadId;
      info.mPhase = thread->mShutdownAnnotationPhase;
      info.mPhaseSinceEpochSec = thread->mShutdownPhaseSinceEpochSec;
      closingThreads.AppendElement(thread);
    }
  }

  // GetThreadName() takes mThreadName's mutex, and BeginShutdown() acquires
  // mThreadName before sShutdownAnnotationMutex, so we must call it outside
  // the annotation lock. ThreadListMutex is still held, keeping the pointers
  // valid.
  for (size_t i = 0; i < entries.Length(); ++i) {
    closingThreads[i]->GetThreadName(entries[i].mClosingName);
  }
  return Some(std::move(entries));
}

/* static */ void nsThread::CollectShutdownHangAnnotation() {
  Maybe<nsTArray<ShutdownHandshakeInfo>> entries = CollectShutdownHandshakes();
  if (!entries) {
    // Say that we could not read the state, so that contention does not read
    // as "no handshakes in flight".
    nsCString json = BuildContendedAnnotationJson();
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::ShuttingDownThreads, json);
    return;
  }
  if (entries->IsEmpty()) {
    CrashReporter::UnrecordAnnotation(
        CrashReporter::Annotation::ShuttingDownThreads);
    return;
  }
  nsCString json = BuildShutdownAnnotationJson(*entries);
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::ShuttingDownThreads, json);
}
#endif  // NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED

NS_IMETHODIMP
nsThread::Shutdown() {
  LOG(("THRD(%p) sync shutdown\n", this));

  nsCOMPtr<nsIThreadShutdown> context;
  nsresult rv = BeginShutdown(getter_AddRefs(context));
  if (NS_FAILED(rv)) {
    return NS_OK;  // The thread has already shut down.
  }

  // If we are going to hang here we want to see the thread's name
  nsAutoCString threadName;
  GetThreadName(threadName);

  // Process events on the current thread until we receive a shutdown ACK.
  // Allows waiting; ensure no locks are held that would deadlock us!
  SpinEventLoopUntil("nsThread::Shutdown: "_ns + threadName,
                     [&]() { return context->GetCompleted(); });

  return NS_OK;
}

NS_IMETHODIMP
nsThread::HasPendingEvents(bool* aResult) {
  if (NS_WARN_IF(PR_GetCurrentThread() != mThread)) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  if (mIsMainThread) {
    *aResult = TaskController::Get()->HasMainThreadPendingTasks();
  } else {
    *aResult = mEvents->HasPendingEvent();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsThread::HasPendingHighPriorityEvents(bool* aResult) {
  if (NS_WARN_IF(PR_GetCurrentThread() != mThread)) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  // This function appears to never be called anymore.
  *aResult = false;
  return NS_OK;
}

NS_IMETHODIMP
nsThread::DispatchToQueue(already_AddRefed<nsIRunnable> aEvent,
                          EventQueuePriority aQueue) {
  RefPtr<nsIRunnable> event = aEvent;

  if (NS_WARN_IF(!event)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!mEvents->PutEvent(event, aQueue)) {
    NS_WARNING(
        "An idle event was posted to a thread that will never run it "
        "(rejected)");
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

NS_IMETHODIMP nsThread::SetThreadQoS(nsIThread::QoSPriority aPriority) {
  if (!StaticPrefs::threads_use_low_power_enabled()) {
    return NS_OK;
  }
  // The approach here is to have a thread set itself for its QoS level,
  // so we assert if we aren't on the current thread.
  MOZ_ASSERT(IsOnCurrentThread(), "Can only change the current thread's QoS");

#if defined(XP_MACOSX)
  // Only arm64 macs may possess heterogeneous cores. On these, we can tell
  // a thread to set its own QoS status. On intel macs things should behave
  // normally, and the OS will ignore the QoS state of the thread.
  if (aPriority == nsIThread::QOS_PRIORITY_LOW) {
    pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
  } else if (NS_IsMainThread()) {
    // MacOS documentation specifies that a main thread should be initialized at
    // the USER_INTERACTIVE priority, so when we restore thread priorities the
    // main thread should be setting itself to this.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  } else {
    pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0);
  }
#endif
  // Do nothing if an OS-specific implementation is unavailable.
  return NS_OK;
}

#ifdef MOZ_CANARY
void canary_alarm_handler(int signum);

class Canary {
  // XXX ToDo: support nested loops
 public:
  Canary() {
    if (sCanaryOutputFD > 0 && EventLatencyIsImportant()) {
      signal(SIGALRM, canary_alarm_handler);
      ualarm(15000, 0);
    }
  }

  ~Canary() {
    if (sCanaryOutputFD != 0 && EventLatencyIsImportant()) {
      ualarm(0, 0);
    }
  }

  static bool EventLatencyIsImportant() {
    return NS_IsMainThread() && XRE_IsParentProcess();
  }
};

void canary_alarm_handler(int signum) {
  void* array[30];
  const char msg[29] = "event took too long to run:\n";
  // use write to be safe in the signal handler
  write(sCanaryOutputFD, msg, sizeof(msg));
  backtrace_symbols_fd(array, backtrace(array, 30), sCanaryOutputFD);
}

#endif

#define NOTIFY_EVENT_OBSERVERS(observers_, func_, params_)                 \
  do {                                                                     \
    if (!observers_.IsEmpty()) {                                           \
      for (nsCOMPtr<nsIThreadObserver> obs_ : observers_.ForwardRange()) { \
        obs_->func_ params_;                                               \
      }                                                                    \
    }                                                                      \
  } while (0)

size_t nsThread::ShallowSizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;
  if (mShutdownContext) {
    n += aMallocSizeOf(mShutdownContext);
  }
  return aMallocSizeOf(this) + aMallocSizeOf(mThread) + n;
}

size_t nsThread::SizeOfEventQueues(mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;
  if (mEventTarget) {
    // The size of mEvents is reported by mEventTarget.
    n += mEventTarget->SizeOfIncludingThis(aMallocSizeOf);
  }
  return n;
}

size_t nsThread::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return ShallowSizeOfIncludingThis(aMallocSizeOf) +
         SizeOfEventQueues(aMallocSizeOf);
}

NS_IMETHODIMP
nsThread::ProcessNextEvent(bool aMayWait, bool* aResult) {
  MOZ_ASSERT(mEvents);
  NS_ENSURE_TRUE(mEvents, NS_ERROR_NOT_IMPLEMENTED);

  LOG(("THRD(%p) ProcessNextEvent [%u %u]\n", this, aMayWait,
       mNestedEventLoopDepth));

  if (NS_WARN_IF(PR_GetCurrentThread() != mThread)) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  // The toplevel event loop normally blocks waiting for the next event, but
  // if we're trying to shut this thread down, we must exit the event loop
  // when the event queue is empty. This only applys to the toplevel event
  // loop! Nested event loops (e.g. during sync dispatch) are waiting for
  // some state change and must be able to block even if something has
  // requested shutdown of the thread. Otherwise we'll just busywait as we
  // endlessly look for an event, fail to find one, and repeat the nested
  // event loop since its state change hasn't happened yet.
  bool reallyWait = aMayWait && (mNestedEventLoopDepth > 0 || !ShuttingDown());

  Maybe<dom::AutoNoJSAPI> noJSAPI;

  if (mUseHangMonitor && reallyWait) {
    BackgroundHangMonitor().NotifyWait();
  }

  if (mIsMainThread) {
    DoMainThreadSpecificProcessing();
  }

#ifdef DEBUG
  BlockingResourceBase::AssertSafeToProcessEventLoop();
#endif

  ++mNestedEventLoopDepth;

  // We only want to create an AutoNoJSAPI on threads that actually do DOM
  // stuff (including workers).  Those are exactly the threads that have an
  // mScriptObserver.
  bool callScriptObserver = !!mScriptObserver;
  if (callScriptObserver) {
    noJSAPI.emplace();
    mScriptObserver->BeforeProcessTask(reallyWait);
  }

  DrainDirectTasks();

  nsCOMPtr<nsIThreadObserver> obs = mEvents->GetObserverOnThread();
  if (obs) {
    obs->OnProcessNextEvent(this, reallyWait);
  }

  NOTIFY_EVENT_OBSERVERS(EventQueue()->EventObservers(), OnProcessNextEvent,
                         (this, reallyWait));

  DrainDirectTasks();

#ifdef MOZ_CANARY
  Canary canary;
#endif
  nsresult rv = NS_OK;

  bool usingTaskController = mIsMainThread;
  {
    // Scope for |event| to make sure that its destructor fires while
    // mNestedEventLoopDepth has been incremented, since that destructor can
    // also do work.
    nsCOMPtr<nsIRunnable> event;
    if (usingTaskController) {
      event = TaskController::Get()->GetRunnableForMTTask(reallyWait);
    } else {
      event = mEvents->GetEvent(reallyWait, &mLastEventDelay);
    }

    *aResult = (event.get() != nullptr);

    if (event) {
      LOG(("THRD(%p) running [%p]\n", this, event.get()));

      Maybe<LogRunnable::Run> log;

      if (!usingTaskController) {
        log.emplace(event);
      }

      // Random delay, so the dispatching and running threads race in varying
      // order.
      DelayForChaosMode(ChaosFeature::TaskRunning);

      mozilla::TimeStamp now = mozilla::TimeStamp::Now();

      if (mUseHangMonitor) {
        BackgroundHangMonitor().NotifyActivity();
      }

      Maybe<PerformanceCounterState::Snapshot> snapshot;
      if (!usingTaskController) {
        snapshot.emplace(mPerformanceCounterState.RunnableWillRun(now, false));
      }

      mLastEventStart = now;

      if (!usingTaskController) {
        AUTO_PROFILE_FOLLOWING_RUNNABLE(event);
        event->Run();
      } else {
        // Avoid generating "Runnable" profiler markers for the
        // "TaskController::ExecutePendingMTTasks" runnables created
        // by TaskController, which already adds "Runnable" markers
        // when executing tasks.
        event->Run();
      }

      if (usingTaskController) {
        *aResult = TaskController::Get()->MTTaskRunnableProcessedTask();
      } else {
        mPerformanceCounterState.RunnableDidRun(EmptyCString(),
                                                std::move(snapshot.ref()));
      }

      // To cover the event's destructor code inside the LogRunnable span.
      event = nullptr;
    } else {
      mLastEventDelay = TimeDuration();
      mLastEventStart = TimeStamp();
      if (aMayWait) {
        MOZ_ASSERT(ShuttingDown(),
                   "This should only happen when shutting down");
        rv = NS_ERROR_UNEXPECTED;
      }
    }
  }

  DrainDirectTasks();

#ifdef MOZ_MEMORY
  if (usingTaskController) {
    // Check if there are any outstanding purges we should process. The purge
    // logic asserts to only ever be run on the main thread, which is the case
    // when using TaskController.
    // Translates to a No-Op if the pref memory.lazypurge.enable == false.
    //
    // In theory this is not perfect, as we cannot guarantee that some lonely
    // thread running will not cause an arena to want a new cleanup while the
    // main thread never awakes after it went idle. But in practice we assume
    // that most if not all activity on other threads will bounce back to the
    // main thread soon and/or other events hit the main thread regularly
    // enough in those processes we activate lazy purge for, such that this
    // does not matter.
    TaskController::Get()->MayScheduleMemoryCleanup();
  }
#endif

  NOTIFY_EVENT_OBSERVERS(EventQueue()->EventObservers(), AfterProcessNextEvent,
                         (this, *aResult));

  if (obs) {
    obs->AfterProcessNextEvent(this, *aResult);
  }

  // In case some EventObserver dispatched some direct tasks; process them
  // now.
  DrainDirectTasks();

  if (callScriptObserver) {
    if (mScriptObserver) {
      mScriptObserver->AfterProcessTask(mNestedEventLoopDepth);
    }
    noJSAPI.reset();
  }

  --mNestedEventLoopDepth;

  return rv;
}

//-----------------------------------------------------------------------------
// nsISupportsPriority

NS_IMETHODIMP
nsThread::GetPriority(int32_t* aPriority) {
  *aPriority = mPriority;
  return NS_OK;
}

NS_IMETHODIMP
nsThread::SetPriority(int32_t aPriority) {
  if (NS_WARN_IF(!mThread)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // NSPR defines the following four thread priorities:
  //   PR_PRIORITY_LOW
  //   PR_PRIORITY_NORMAL
  //   PR_PRIORITY_HIGH
  //   PR_PRIORITY_URGENT
  // We map the priority values defined on nsISupportsPriority to these
  // values.

  mPriority = aPriority;

  PRThreadPriority pri;
  if (mPriority <= PRIORITY_HIGHEST) {
    pri = PR_PRIORITY_URGENT;
  } else if (mPriority < PRIORITY_NORMAL) {
    pri = PR_PRIORITY_HIGH;
  } else if (mPriority > PRIORITY_NORMAL) {
    pri = PR_PRIORITY_LOW;
  } else {
    pri = PR_PRIORITY_NORMAL;
  }
  // If chaos mode is active, retain the randomly chosen priority
  if (!ChaosMode::isActive(ChaosFeature::ThreadScheduling)) {
    PR_SetThreadPriority(mThread, pri);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsThread::AdjustPriority(int32_t aDelta) {
  return SetPriority(mPriority + aDelta);
}

//-----------------------------------------------------------------------------
// nsIThreadInternal

NS_IMETHODIMP
nsThread::GetObserver(nsIThreadObserver** aObs) {
  MOZ_ASSERT(mEvents);
  NS_ENSURE_TRUE(mEvents, NS_ERROR_NOT_IMPLEMENTED);

  nsCOMPtr<nsIThreadObserver> obs = mEvents->GetObserver();
  obs.forget(aObs);
  return NS_OK;
}

NS_IMETHODIMP
nsThread::SetObserver(nsIThreadObserver* aObs) {
  MOZ_ASSERT(mEvents);
  NS_ENSURE_TRUE(mEvents, NS_ERROR_NOT_IMPLEMENTED);

  if (NS_WARN_IF(PR_GetCurrentThread() != mThread)) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  mEvents->SetObserver(aObs);
  return NS_OK;
}

uint32_t nsThread::RecursionDepth() const {
  MOZ_ASSERT(PR_GetCurrentThread() == mThread);
  return mNestedEventLoopDepth;
}

NS_IMETHODIMP
nsThread::AddObserver(nsIThreadObserver* aObserver) {
  MOZ_ASSERT(mEvents);
  NS_ENSURE_TRUE(mEvents, NS_ERROR_NOT_IMPLEMENTED);

  if (NS_WARN_IF(!aObserver)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (NS_WARN_IF(PR_GetCurrentThread() != mThread)) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  EventQueue()->AddObserver(aObserver);

  return NS_OK;
}

NS_IMETHODIMP
nsThread::RemoveObserver(nsIThreadObserver* aObserver) {
  MOZ_ASSERT(mEvents);
  NS_ENSURE_TRUE(mEvents, NS_ERROR_NOT_IMPLEMENTED);

  if (NS_WARN_IF(PR_GetCurrentThread() != mThread)) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  EventQueue()->RemoveObserver(aObserver);

  return NS_OK;
}

void nsThread::SetScriptObserver(
    mozilla::CycleCollectedJSContext* aScriptObserver) {
  if (!aScriptObserver) {
    mScriptObserver = nullptr;
    return;
  }

  MOZ_ASSERT(!mScriptObserver);
  mScriptObserver = aScriptObserver;
}

void NS_DispatchMemoryPressure();

void nsThread::DoMainThreadSpecificProcessing() const {
  MOZ_ASSERT(mIsMainThread);

  ipc::CancelCPOWs();

  // Fire a memory pressure notification, if one is pending.
  if (!ShuttingDown()) {
    NS_DispatchMemoryPressure();
  }
}

//-----------------------------------------------------------------------------
// nsIDirectTaskDispatcher

NS_IMETHODIMP
nsThread::DispatchDirectTask(already_AddRefed<nsIRunnable> aEvent) {
  if (!IsOnCurrentThread()) {
    return NS_ERROR_FAILURE;
  }
  mDirectTasks.AddTask(std::move(aEvent));
  return NS_OK;
}

NS_IMETHODIMP nsThread::DrainDirectTasks() {
  if (!IsOnCurrentThread()) {
    return NS_ERROR_FAILURE;
  }
  mDirectTasks.DrainTasks();
  return NS_OK;
}

NS_IMETHODIMP nsThread::HaveDirectTasks(bool* aValue) {
  if (!IsOnCurrentThread()) {
    return NS_ERROR_FAILURE;
  }

  *aValue = mDirectTasks.HaveTasks();
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsThreadShutdownContext, nsIThreadShutdown)

NS_IMETHODIMP
nsThreadShutdownContext::OnCompletion(nsIRunnable* aEvent) {
  if (mCompleted) {
    aEvent->Run();
  } else {
    mCompletionCallbacks.AppendElement(aEvent);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsThreadShutdownContext::GetCompleted(bool* aCompleted) {
  *aCompleted = mCompleted;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadShutdownContext::StopWaitingAndLeakThread() {
  // Take the joining thread from `mJoiningThread` so that the terminating
  // thread won't try to dispatch nsThreadShutdownAckEvent to us anymore.
  RefPtr<nsThread> joiningThread;
  {
    MutexAutoLock lock(mJoiningThreadMutex);
    if (!mJoiningThread) {
      // Shutdown is already being resolved, so there's nothing for us to do.
      return NS_ERROR_NOT_AVAILABLE;
    }
    joiningThread = mJoiningThread.forget();
    mThreadLeaked = true;
  }

  MOZ_DIAGNOSTIC_ASSERT(joiningThread->IsOnCurrentThread());

  MarkCompleted();

  return NS_OK;
}

nsThreadShutdownContext::~nsThreadShutdownContext() = default;

void nsThreadShutdownContext::MarkCompleted() {
  MOZ_ASSERT(!mCompleted);
  mCompleted = true;
  nsTArray<nsCOMPtr<nsIRunnable>> callbacks(std::move(mCompletionCallbacks));
  for (auto& callback : callbacks) {
    callback->Run();
  }
}

namespace mozilla {
PerformanceCounterState::Snapshot PerformanceCounterState::RunnableWillRun(
    TimeStamp aNow, bool aIsIdleRunnable) {
  if (mIsMainThread && IsNestedRunnable()) {
    // Flush out any accumulated time that should be accounted to the
    // current runnable before we start running a nested runnable.  Don't
    // do this for non-mainthread threads that may be running their own
    // event loops, like SocketThread.
    MaybeReportAccumulatedTime("nested runnable"_ns, aNow);
  }

  Snapshot snapshot(mCurrentEventLoopDepth, mCurrentRunnableIsIdleRunnable);

  mCurrentEventLoopDepth = mNestedEventLoopDepth;
  mCurrentRunnableIsIdleRunnable = aIsIdleRunnable;
  mCurrentTimeSliceStart = aNow;

  return snapshot;
}

void PerformanceCounterState::RunnableDidRun(const nsCString& aName,
                                             Snapshot&& aSnapshot) {
  // First thing: Restore our mCurrentEventLoopDepth so we can use
  // IsNestedRunnable().
  mCurrentEventLoopDepth = aSnapshot.mOldEventLoopDepth;

  // We may not need the current timestamp; don't bother computing it if we
  // don't.
  TimeStamp now;
  if (mLongTaskLength.isSome() || IsNestedRunnable()) {
    now = TimeStamp::Now();
  }
  if (mLongTaskLength.isSome()) {
    MaybeReportAccumulatedTime(aName, now);
  }

  // And now restore the rest of our state.
  mCurrentRunnableIsIdleRunnable = aSnapshot.mOldIsIdleRunnable;
  if (IsNestedRunnable()) {
    // Reset mCurrentTimeSliceStart to right now, so our parent runnable's
    // next slice can be properly accounted for.
    mCurrentTimeSliceStart = now;
  } else {
    // We are done at the outermost level; we are no longer in a timeslice.
    mCurrentTimeSliceStart = TimeStamp();
  }
}

struct LongTaskMarker : public BaseMarkerType<LongTaskMarker> {
  static constexpr const char* Name = "MainThreadLongTask";

  static constexpr bool ETWStoreName = true;

  using MS = MarkerSchema;
  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
};

void PerformanceCounterState::MaybeReportAccumulatedTime(const nsCString& aName,
                                                         TimeStamp aNow) {
  MOZ_ASSERT(mCurrentTimeSliceStart,
             "How did we get here if we're not in a timeslice?");
  if (!mLongTaskLength.isSome()) {
    return;
  }

  TimeDuration duration = aNow - mCurrentTimeSliceStart;
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  if (mIsMainThread && duration.ToMilliseconds() > LONGTASK_TELEMETRY_MS) {
    glean::event::longtask.MaybeTruncateAndGet(aName).AccumulateRawDuration(
        duration);
  }
#endif

  // Long tasks only matter on the main thread.
  if (duration.ToMilliseconds() >= mLongTaskLength.value()) {
    // Idle events (gc...) don't *really* count here
    if (!mCurrentRunnableIsIdleRunnable) {
      mLastLongNonIdleTaskEnd = aNow;
    }
    mLastLongTaskEnd = aNow;

    if (profiler_thread_is_being_profiled_for_markers()) {
      profiler_add_marker(mCurrentRunnableIsIdleRunnable
                              ? ProfilerString8View("LongIdleTask")
                              : ProfilerString8View("LongTask"),
                          geckoprofiler::category::OTHER,
                          MarkerTiming::Interval(mCurrentTimeSliceStart, aNow),
                          LongTaskMarker{});
    }
  }
}

}  // namespace mozilla
