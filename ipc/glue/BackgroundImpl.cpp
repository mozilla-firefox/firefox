/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BackgroundChild.h"
#include "BackgroundParent.h"

#include "BackgroundChildImpl.h"
#include "BackgroundParentImpl.h"
#include "MainThreadUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/ipc/BackgroundStarterChild.h"
#include "mozilla/ipc/BackgroundStarterParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/PBackgroundStarter.h"
#include "nsCOMPtr.h"
#include "nsIEventTarget.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsISupportsImpl.h"
#include "nsIThread.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "nsXPCOMPrivate.h"
#include "prthread.h"

#ifdef RELEASE_OR_BETA
#  define THREADSAFETY_ASSERT MOZ_ASSERT
#else
#  define THREADSAFETY_ASSERT MOZ_RELEASE_ASSERT
#endif

#define CRASH_IN_CHILD_PROCESS(_msg) \
  do {                               \
    if (XRE_IsParentProcess()) {     \
      MOZ_ASSERT(false, _msg);       \
    } else {                         \
      MOZ_CRASH(_msg);               \
    }                                \
  } while (0)

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;
using namespace mozilla::net;

namespace {

// This exists so that [Assert]IsOnBackgroundThread() can continue to work
// during shutdown. We can rely on TLS being 0 initialized, so only the
// background thread itself needs to ever set this flag.
static MOZ_THREAD_LOCAL(bool) sTLSIsOnBackgroundThread;

class ChildImpl;

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

void AssertIsOnMainThread() { THREADSAFETY_ASSERT(NS_IsMainThread()); }

// -----------------------------------------------------------------------------
// ParentImpl Declaration
// -----------------------------------------------------------------------------

class ParentImpl final : public BackgroundParentImpl {
  friend class ChildImpl;
  friend class mozilla::ipc::BackgroundParent;
  friend class mozilla::ipc::BackgroundStarterParent;

 private:
  class ShutdownObserver;

  // This is only modified on the main thread. It is null if the thread does not
  // exist or is shutting down.
  static StaticRefPtr<nsIThread> sBackgroundThread;

  // This is only modified on the main thread. It is true after the shutdown
  // observer is registered and is never unset thereafter.
  static bool sShutdownObserverRegistered;

  // This is only modified on the main thread. It prevents us from trying to
  // create the background thread after application shutdown has started.
  static bool sShutdownHasStarted;

  // null if this is a same-process actor.
  const RefPtr<ThreadsafeContentParentHandle> mContent;

  // Set at construction to indicate whether this parent actor corresponds to a
  // child actor in another process or to a child actor from a different thread
  // in the same process.
  const bool mIsOtherProcessActor;

 public:
  static already_AddRefed<nsISerialEventTarget> GetBackgroundThread() {
    AssertIsInMainProcess();
    THREADSAFETY_ASSERT(NS_IsMainThread() || IsOnBackgroundThread());
    return do_AddRef(sBackgroundThread);
  }

  static bool IsOnBackgroundThread() {
    MOZ_ASSERT(sTLSIsOnBackgroundThread.initialized());
    return sTLSIsOnBackgroundThread.get();
  }

  static void AssertIsOnBackgroundThread() {
    THREADSAFETY_ASSERT(IsOnBackgroundThread());
  }

  // `ParentImpl` instances need to be deleted on the main thread, despite IPC
  // controlling them on a background thread. Use `_WITH_DELETE_ON_MAIN_THREAD`
  // to force destruction to occur on the desired thread.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD(ParentImpl,
                                                                   override)

 private:
  // Forwarded from BackgroundParent.
  static bool IsOtherProcessActor(PBackgroundParent* aBackgroundActor);

  // Forwarded from BackgroundParent.
  static ThreadsafeContentParentHandle* GetContentParentHandle(
      PBackgroundParent* aBackgroundActor);

  // Forwarded from BackgroundParent.
  static uint64_t GetChildID(PBackgroundParent* aBackgroundActor);

  // Forwarded from BackgroundParent.
  static void KillHardAsync(PBackgroundParent* aBackgroundActor,
                            const nsACString& aReason);

  // Forwarded from BackgroundParent.
  static bool AllocStarter(ContentParent* aContent,
                           Endpoint<PBackgroundStarterParent>&& aEndpoint,
                           bool aCrossProcess = true);

  static bool CreateBackgroundThread();

  static void ShutdownBackgroundThread();

  // NOTE: ParentImpl could be used in 2 cases below.
  // 1. Within the parent process.
  // 2. Between parent process and content process.
  // |aContent| should be not null for case 2. For cases 1, it's null.
  explicit ParentImpl(ThreadsafeContentParentHandle* aContent,
                      bool aIsOtherProcessActor)
      : mContent(aContent), mIsOtherProcessActor(aIsOtherProcessActor) {
    AssertIsInMainProcess();
    MOZ_ASSERT_IF(!aIsOtherProcessActor, XRE_IsParentProcess());
  }

  ~ParentImpl() {
    AssertIsInMainProcess();
    AssertIsOnMainThread();
  }
};

// -----------------------------------------------------------------------------
// ChildImpl Declaration
// -----------------------------------------------------------------------------

class ChildImpl final : public BackgroundChildImpl {
  friend class mozilla::ipc::BackgroundChild;
  friend class mozilla::ipc::BackgroundChildImpl;
  friend class mozilla::ipc::BackgroundStarterChild;

  typedef base::ProcessId ProcessId;

  class ShutdownObserver;

 public:
  struct ThreadLocalInfo {
    ThreadLocalInfo() = default;

    RefPtr<ChildImpl> mActor;
    UniquePtr<BackgroundChildImpl::ThreadLocal> mConsumerThreadLocal;
#ifdef DEBUG
    bool mClosed = false;
#endif
  };

 private:
  // A thread-local index that is not valid.
  static constexpr unsigned int kBadThreadLocalIndex =
      static_cast<unsigned int>(-1);

  // ThreadInfoWrapper encapsulates ThreadLocalInfo and ThreadLocalIndex and
  // also provides some common functions for creating PBackground IPC actor.
  class ThreadInfoWrapper final {
    friend class ChildImpl;

   public:
    using ActorCreateFunc = void (*)(ThreadLocalInfo*, unsigned int,
                                     nsIEventTarget*, ChildImpl**);

    ThreadInfoWrapper() = default;

    void Startup() {
      MOZ_ASSERT(mThreadLocalIndex == kBadThreadLocalIndex,
                 "ThreadInfoWrapper::Startup() called more than once!");

      PRStatus status =
          PR_NewThreadPrivateIndex(&mThreadLocalIndex, ThreadLocalDestructor);
      MOZ_RELEASE_ASSERT(status == PR_SUCCESS,
                         "PR_NewThreadPrivateIndex failed!");

      MOZ_ASSERT(mThreadLocalIndex != kBadThreadLocalIndex);
    }

    void Shutdown() {
      if (sShutdownHasStarted) {
        MOZ_ASSERT_IF(mThreadLocalIndex != kBadThreadLocalIndex,
                      !PR_GetThreadPrivate(mThreadLocalIndex));
        return;
      }

      if (mThreadLocalIndex == kBadThreadLocalIndex) {
        return;
      }

      RefPtr<BackgroundStarterChild> starter;
      {
        auto lock = mStarter.Lock();
        starter = lock->forget();
      }
      if (starter) {
        CloseStarter(starter);
      }

      ThreadLocalInfo* threadLocalInfo;
#ifdef DEBUG
      threadLocalInfo =
          static_cast<ThreadLocalInfo*>(PR_GetThreadPrivate(mThreadLocalIndex));
      MOZ_ASSERT(!threadLocalInfo);
#endif

      threadLocalInfo = mMainThreadInfo;
      if (threadLocalInfo) {
#ifdef DEBUG
        MOZ_ASSERT(!threadLocalInfo->mClosed);
        threadLocalInfo->mClosed = true;
#endif

        ThreadLocalDestructor(threadLocalInfo);
        mMainThreadInfo = nullptr;
      }
    }

    template <typename Actor>
    void InitStarter(Actor* aActor) {
      AssertIsOnMainThread();

      // Create a pair of endpoints and send them to the other process.
      Endpoint<PBackgroundStarterParent> parent;
      Endpoint<PBackgroundStarterChild> child;
      MOZ_ALWAYS_SUCCEEDS(PBackgroundStarter::CreateEndpoints(
          aActor->OtherEndpointProcInfo(), EndpointProcInfo::Current(), &parent,
          &child));
      MOZ_ALWAYS_TRUE(aActor->SendInitBackground(std::move(parent)));

      InitStarter(std::move(child));
    }

    void InitStarter(Endpoint<PBackgroundStarterChild>&& aEndpoint) {
      AssertIsOnMainThread();

      EndpointProcInfo otherProcInfo = aEndpoint.OtherEndpointProcInfo();

      nsCOMPtr<nsISerialEventTarget> taskQueue;
      MOZ_ALWAYS_SUCCEEDS(NS_CreateBackgroundTaskQueue(
          "PBackgroundStarter Queue", getter_AddRefs(taskQueue)));

      RefPtr starter =
          MakeRefPtr<BackgroundStarterChild>(otherProcInfo, taskQueue);

      taskQueue->Dispatch(NS_NewRunnableFunction(
          "PBackgroundStarterChild Init",
          [starter, endpoint = std::move(aEndpoint)]() mutable {
            MOZ_ALWAYS_TRUE(endpoint.Bind(starter));
          }));

      // Swap in the newly initialized `BackgroundStarterChild`, and close the
      // previous one if we're replacing an existing PBackgroundStarterChild
      // instance.
      RefPtr<BackgroundStarterChild> prevStarter;
      {
        auto lock = mStarter.Lock();
        prevStarter = lock->forget();
        *lock = starter.forget();
      }
      if (prevStarter) {
        CloseStarter(prevStarter);
      }
    }

    void CloseForCurrentThread() {
      MOZ_ASSERT(!NS_IsMainThread());

      if (mThreadLocalIndex == kBadThreadLocalIndex) {
        return;
      }

      auto* threadLocalInfo =
          static_cast<ThreadLocalInfo*>(PR_GetThreadPrivate(mThreadLocalIndex));

      if (!threadLocalInfo) {
        return;
      }

#ifdef DEBUG
      MOZ_ASSERT(!threadLocalInfo->mClosed);
      threadLocalInfo->mClosed = true;
#endif

      // Clearing the thread local will synchronously close the actor.
      DebugOnly<PRStatus> status =
          PR_SetThreadPrivate(mThreadLocalIndex, nullptr);
      MOZ_ASSERT(status == PR_SUCCESS);
    }

    PBackgroundChild* GetOrCreateForCurrentThread() {
      // Processes can be told to do final CC's during shutdown even though
      // they never finished starting (and thus call this), because they
      // hadn't gotten far enough to call Startup() before shutdown began.
      if (mThreadLocalIndex == kBadThreadLocalIndex) {
        NS_ERROR("BackgroundChild::Startup() was never called");
        return nullptr;
      }
      if (NS_IsMainThread() && ChildImpl::sShutdownHasStarted) {
        return nullptr;
      }

      auto* threadLocalInfo = NS_IsMainThread()
                                  ? mMainThreadInfo
                                  : static_cast<ThreadLocalInfo*>(
                                        PR_GetThreadPrivate(mThreadLocalIndex));

      if (!threadLocalInfo) {
        auto newInfo = std::make_unique<ThreadLocalInfo>();

        if (NS_IsMainThread()) {
          mMainThreadInfo = newInfo.get();
        } else {
          if (PR_SetThreadPrivate(mThreadLocalIndex, newInfo.get()) !=
              PR_SUCCESS) {
            CRASH_IN_CHILD_PROCESS("PR_SetThreadPrivate failed!");
            return nullptr;
          }
        }

        threadLocalInfo = newInfo.release();
      }

      if (threadLocalInfo->mActor) {
        return threadLocalInfo->mActor;
      }

      RefPtr<BackgroundStarterChild> starter;
      {
        auto lock = mStarter.Lock();
        starter = *lock;
      }
      if (!starter) {
        CRASH_IN_CHILD_PROCESS("No BackgroundStarterChild");
        return nullptr;
      }

      Endpoint<PBackgroundParent> parent;
      Endpoint<PBackgroundChild> child;
      nsresult rv;
      rv = PBackground::CreateEndpoints(starter->mOtherProcInfo,
                                        EndpointProcInfo::Current(), &parent,
                                        &child);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to create top level actor!");
        return nullptr;
      }

      RefPtr strongActor = MakeRefPtr<ChildImpl>();
      if (!child.Bind(strongActor)) {
        CRASH_IN_CHILD_PROCESS("Failed to bind ChildImpl!");
        return nullptr;
      }
      threadLocalInfo->mActor = strongActor.forget();

      // Dispatch to the background task queue to create the relevant actor in
      // the remote process.
      starter->mTaskQueue->Dispatch(NS_NewRunnableFunction(
          "PBackground GetOrCreateForCurrentThread",
          [starter, endpoint = std::move(parent)]() mutable {
            if (!starter->SendInitBackground(std::move(endpoint))) {
              NS_WARNING("Failed to create toplevel actor");
            }
          }));
      return threadLocalInfo->mActor;
    }

   private:
    static void CloseStarter(BackgroundStarterChild* aStarter) {
      aStarter->mTaskQueue->Dispatch(NS_NewRunnableFunction(
          "PBackgroundStarterChild Close",
          [starter = RefPtr{aStarter}] { starter->Close(); }));
    }

    // This is only modified on the main thread. It is the thread-local index
    // that we use to store the BackgroundChild for each thread.
    unsigned int mThreadLocalIndex = kBadThreadLocalIndex;

    // On the main thread, we store TLS in this global instead of in
    // mThreadLocalIndex. That way, cooperative main threads all share the same
    // thread info.
    ThreadLocalInfo* mMainThreadInfo = nullptr;

    // The starter which will be used to launch PBackground instances of this
    // type. Only modified on the main thread, but may be read by any thread
    // wanting to start background actors.
    StaticDataMutex<StaticRefPtr<BackgroundStarterChild>> mStarter{"mStarter"};
  };

  // For PBackground between parent and content process.
  static ThreadInfoWrapper sParentAndContentProcessThreadInfo;

  // This is only modified on the main thread. It prevents us from trying to
  // create the background thread after application shutdown has started.
  static bool sShutdownHasStarted;

#if defined(DEBUG) || !defined(RELEASE_OR_BETA)
  nsISerialEventTarget* mOwningEventTarget;
#endif

 public:
  static void Shutdown();

  void AssertIsOnOwningThread() {
    THREADSAFETY_ASSERT(mOwningEventTarget);

#ifdef RELEASE_OR_BETA
    DebugOnly<bool> current;
#else
    bool current;
#endif
    THREADSAFETY_ASSERT(
        NS_SUCCEEDED(mOwningEventTarget->IsOnCurrentThread(&current)));
    THREADSAFETY_ASSERT(current);
  }

  explicit ChildImpl()
#if defined(DEBUG) || !defined(RELEASE_OR_BETA)
      : mOwningEventTarget(GetCurrentSerialEventTarget())
#endif
  {
    AssertIsOnOwningThread();
  }

  // This type is threadsafe refcounted as actors managed by it may be destroyed
  // after the thread it is bound to dies, and hold a reference to this object.
  //
  // It is _not_ safe to use this type or any methods on it from off of the
  // thread it was created for.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ChildImpl, override)

 private:
  // Forwarded from BackgroundChild.
  static void Startup();

  // Forwarded from BackgroundChild.
  static PBackgroundChild* GetForCurrentThread();

  // Forwarded from BackgroundChild.
  static PBackgroundChild* GetOrCreateForCurrentThread();

  static void CloseForCurrentThread();

  // Forwarded from BackgroundChildImpl.
  static BackgroundChildImpl::ThreadLocal* GetThreadLocalForCurrentThread();

  // Forwarded from BackgroundChild.
  static void InitContentStarter(mozilla::dom::ContentChild* aContent);

  static void ThreadLocalDestructor(void* aThreadLocal);

  // This class is reference counted.
  ~ChildImpl() = default;
};

// -----------------------------------------------------------------------------
// ParentImpl Helper Declarations
// -----------------------------------------------------------------------------

class ParentImpl::ShutdownObserver final : public nsIObserver {
 public:
  ShutdownObserver() { AssertIsOnMainThread(); }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

 private:
  ~ShutdownObserver() { AssertIsOnMainThread(); }
};

// -----------------------------------------------------------------------------
// ChildImpl Helper Declarations
// -----------------------------------------------------------------------------

class ChildImpl::ShutdownObserver final : public nsIObserver {
 public:
  ShutdownObserver() { AssertIsOnMainThread(); }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

 private:
  ~ShutdownObserver() { AssertIsOnMainThread(); }
};

}  // namespace

namespace mozilla {
namespace ipc {

bool IsOnBackgroundThread() { return ParentImpl::IsOnBackgroundThread(); }

#ifdef DEBUG

void AssertIsOnBackgroundThread() { ParentImpl::AssertIsOnBackgroundThread(); }

#endif  // DEBUG

}  // namespace ipc
}  // namespace mozilla

// -----------------------------------------------------------------------------
// BackgroundParent Public Methods
// -----------------------------------------------------------------------------

// static
already_AddRefed<nsISerialEventTarget> BackgroundParent::GetBackgroundThread() {
  return ParentImpl::GetBackgroundThread();
}

// static
bool BackgroundParent::IsOtherProcessActor(
    PBackgroundParent* aBackgroundActor) {
  return ParentImpl::IsOtherProcessActor(aBackgroundActor);
}

// static
ThreadsafeContentParentHandle* BackgroundParent::GetContentParentHandle(
    PBackgroundParent* aBackgroundActor) {
  return ParentImpl::GetContentParentHandle(aBackgroundActor);
}

// static
LoadedOriginSet* BackgroundParent::GetLoadedOrigins(
    PBackgroundParent* aBackgroundActor) {
  ThreadsafeContentParentHandle* handle =
      GetContentParentHandle(aBackgroundActor);
  return handle ? handle->LoadedOrigins() : nullptr;
}

// static
uint64_t BackgroundParent::GetChildID(PBackgroundParent* aBackgroundActor) {
  return ParentImpl::GetChildID(aBackgroundActor);
}

// static
mozilla::dom::RemoteType BackgroundParent::GetRemoteType(
    PBackgroundParent* aBackgroundActor) {
  ThreadsafeContentParentHandle* handle =
      GetContentParentHandle(aBackgroundActor);
  return handle ? handle->GetRemoteType()
                : mozilla::dom::RemoteType::NotRemote();
}

// static
bool BackgroundParent::ValidatePrincipal(
    PBackgroundParent* aBackgroundActor, nsIPrincipal* aPrincipal,
    const EnumSet<ValidatePrincipalOptions>& aOptions) {
  RefPtr<ThreadsafeContentParentHandle> handle =
      GetContentParentHandle(aBackgroundActor);
  return !handle || handle->ValidatePrincipal(aPrincipal, aOptions);
}

// static
bool BackgroundParent::ValidatePrincipalInfo(
    PBackgroundParent* aBackgroundActor, const PrincipalInfo& aPrincipal,
    const EnumSet<ValidatePrincipalOptions>& aOptions) {
  auto result = PrincipalInfoToPrincipal(aPrincipal);
  if (NS_WARN_IF(result.isErr())) {
    return false;
  }

  return ValidatePrincipal(aBackgroundActor, result.inspect(), aOptions);
}

// static
void BackgroundParent::KillHardAsync(PBackgroundParent* aBackgroundActor,
                                     const nsACString& aReason) {
  ParentImpl::KillHardAsync(aBackgroundActor, aReason);
}

// static
bool BackgroundParent::AllocStarter(
    ContentParent* aContent, Endpoint<PBackgroundStarterParent>&& aEndpoint) {
  return ParentImpl::AllocStarter(aContent, std::move(aEndpoint));
}

// -----------------------------------------------------------------------------
// BackgroundChild Public Methods
// -----------------------------------------------------------------------------

// static
void BackgroundChild::Startup() { ChildImpl::Startup(); }

// static
PBackgroundChild* BackgroundChild::GetForCurrentThread() {
  return ChildImpl::GetForCurrentThread();
}

// static
PBackgroundChild* BackgroundChild::GetOrCreateForCurrentThread() {
  return ChildImpl::GetOrCreateForCurrentThread();
}

// static
void BackgroundChild::CloseForCurrentThread() {
  ChildImpl::CloseForCurrentThread();
}

// static
void BackgroundChild::InitContentStarter(ContentChild* aContent) {
  ChildImpl::InitContentStarter(aContent);
}

// static
bool BackgroundChild::ValidatePrincipal(
    nsIPrincipal* aPrincipal,
    const EnumSet<ValidatePrincipalOptions>& aOptions) {
  RefPtr<LoadedOriginSet> loadedOrigins = CurrentLoadedOriginSet();
  return !loadedOrigins ||
         loadedOrigins->ValidatePrincipal(aPrincipal, aOptions);
}

// static
bool BackgroundChild::ValidatePrincipalInfo(
    const PrincipalInfo& aPrincipalInfo,
    const EnumSet<ValidatePrincipalOptions>& aOptions) {
  auto result = PrincipalInfoToPrincipal(aPrincipalInfo);
  if (NS_WARN_IF(result.isErr())) {
    return false;
  }

  return ValidatePrincipal(result.inspect(), aOptions);
}

// -----------------------------------------------------------------------------
// BackgroundChildImpl Public Methods
// -----------------------------------------------------------------------------

// static
BackgroundChildImpl::ThreadLocal*
BackgroundChildImpl::GetThreadLocalForCurrentThread() {
  return ChildImpl::GetThreadLocalForCurrentThread();
}

// -----------------------------------------------------------------------------
// ParentImpl Static Members
// -----------------------------------------------------------------------------

StaticRefPtr<nsIThread> ParentImpl::sBackgroundThread;

bool ParentImpl::sShutdownObserverRegistered = false;

bool ParentImpl::sShutdownHasStarted = false;

// -----------------------------------------------------------------------------
// ChildImpl Static Members
// -----------------------------------------------------------------------------

MOZ_GLOBINIT ChildImpl::ThreadInfoWrapper
    ChildImpl::sParentAndContentProcessThreadInfo;

bool ChildImpl::sShutdownHasStarted = false;

// -----------------------------------------------------------------------------
// ParentImpl Implementation
// -----------------------------------------------------------------------------

// static
bool ParentImpl::IsOtherProcessActor(PBackgroundParent* aBackgroundActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aBackgroundActor);

  return static_cast<ParentImpl*>(aBackgroundActor)->mIsOtherProcessActor;
}

// static
ThreadsafeContentParentHandle* ParentImpl::GetContentParentHandle(
    PBackgroundParent* aBackgroundActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aBackgroundActor);

  return static_cast<ParentImpl*>(aBackgroundActor)->mContent.get();
}

// static
uint64_t ParentImpl::GetChildID(PBackgroundParent* aBackgroundActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aBackgroundActor);

  auto actor = static_cast<ParentImpl*>(aBackgroundActor);
  if (actor->mContent) {
    return actor->mContent->ChildID();
  }

  return 0;
}

// static
void ParentImpl::KillHardAsync(PBackgroundParent* aBackgroundActor,
                               const nsACString& aReason) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aBackgroundActor);
  MOZ_ASSERT(BackgroundParent::IsOtherProcessActor(aBackgroundActor));

  RefPtr<ThreadsafeContentParentHandle> handle =
      GetContentParentHandle(aBackgroundActor);
  MOZ_ASSERT(handle);

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
      NS_NewRunnableFunction(
          "ParentImpl::KillHardAsync",
          [handle = std::move(handle), reason = nsCString{aReason}]() {
            mozilla::AssertIsOnMainThread();

            if (RefPtr<ContentParent> contentParent =
                    handle->GetContentParent()) {
              contentParent->KillHard(reason.get());
            }
          }),
      NS_DISPATCH_NORMAL));

  // After we've scheduled killing of the remote process, also ensure we induce
  // a connection error in the IPC channel to immediately stop all IPC
  // communication on this channel.
  if (aBackgroundActor->CanSend()) {
    aBackgroundActor->GetIPCChannel()->InduceConnectionError();
  }
}

// static
bool ParentImpl::AllocStarter(ContentParent* aContent,
                              Endpoint<PBackgroundStarterParent>&& aEndpoint,
                              bool aCrossProcess) {
  AssertIsInMainProcess();
  AssertIsOnMainThread();

  MOZ_ASSERT(aEndpoint.IsValid());

  if (!sBackgroundThread && !CreateBackgroundThread()) {
    NS_WARNING("Failed to create background thread!");
    return false;
  }

  RefPtr actor = MakeRefPtr<BackgroundStarterParent>(
      aContent ? aContent->ThreadsafeHandle() : nullptr, aCrossProcess);

  if (NS_FAILED(sBackgroundThread->Dispatch(
          NS_NewRunnableFunction(
              "BackgroundStarterParent::ConnectActorRunnable",
              [actor = std::move(actor),
               endpoint = std::move(aEndpoint)]() mutable {
                MOZ_ASSERT(endpoint.IsValid());
                MOZ_ALWAYS_TRUE(endpoint.Bind(actor));
              }),
          NS_DISPATCH_FALLIBLE))) {
    NS_WARNING("Failed to dispatch connect runnable!");
  }

  return true;
}

// static
bool ParentImpl::CreateBackgroundThread() {
  AssertIsInMainProcess();
  AssertIsOnMainThread();
  MOZ_ASSERT(!sBackgroundThread);

  if (sShutdownHasStarted) {
    NS_WARNING(
        "Trying to create background thread after shutdown has "
        "already begun!");
    return false;
  }

  if (!sShutdownObserverRegistered) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (NS_WARN_IF(!obs)) {
      return false;
    }

    nsCOMPtr<nsIObserver> observer = new ShutdownObserver();

    nsresult rv = obs->AddObserver(
        observer, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID, false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }

    sShutdownObserverRegistered = true;
  }

  nsCOMPtr<nsIThread> thread;
  if (NS_FAILED(NS_NewNamedThread(
          "IPDL Background", getter_AddRefs(thread),
          NS_NewRunnableFunction(
              "Background::ParentImpl::CreateBackgroundThreadRunnable", []() {
                MOZ_ASSERT(sTLSIsOnBackgroundThread.initialized());
                sTLSIsOnBackgroundThread.set(true);
              })))) {
    NS_WARNING("NS_NewNamedThread failed!");
    return false;
  }

  sBackgroundThread = thread.forget();

  return true;
}

// static
void ParentImpl::ShutdownBackgroundThread() {
  AssertIsInMainProcess();
  AssertIsOnMainThread();
  MOZ_ASSERT(sShutdownHasStarted);

  if (nsCOMPtr<nsIThread> thread = sBackgroundThread.forget()) {
    // Shutdown will process all remaining events.
    MOZ_ALWAYS_SUCCEEDS(thread->Shutdown());
  }
}

NS_IMPL_ISUPPORTS(ParentImpl::ShutdownObserver, nsIObserver)

NS_IMETHODIMP
ParentImpl::ShutdownObserver::Observe(nsISupports* /*aSubject*/,
                                      const char* aTopic,
                                      const char16_t* /*aData*/) {
  AssertIsInMainProcess();
  AssertIsOnMainThread();
  MOZ_ASSERT(!sShutdownHasStarted);
  MOZ_ASSERT(!strcmp(aTopic, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID));

  sShutdownHasStarted = true;

  // Do this first before calling (and spinning the event loop in)
  // ShutdownBackgroundThread().
  ChildImpl::Shutdown();

  ShutdownBackgroundThread();

  return NS_OK;
}

BackgroundStarterParent::BackgroundStarterParent(
    ThreadsafeContentParentHandle* aContent, bool aCrossProcess)
    : mCrossProcess(aCrossProcess), mContent(aContent) {
  AssertIsOnMainThread();
  AssertIsInMainProcess();
  MOZ_ASSERT_IF(!mCrossProcess, !mContent);
  MOZ_ASSERT_IF(!mCrossProcess, XRE_IsParentProcess());
}

IPCResult BackgroundStarterParent::RecvInitBackground(
    Endpoint<PBackgroundParent>&& aEndpoint) {
  AssertIsOnBackgroundThread();

  if (!aEndpoint.IsValid()) {
    return IPC_FAIL(this,
                    "Cannot initialize PBackground with invalid endpoint");
  }

  RefPtr<ParentImpl> actor = new ParentImpl(mContent, mCrossProcess);

  (void)aEndpoint.Bind(actor);
  return IPC_OK();
}

// -----------------------------------------------------------------------------
// ChildImpl Implementation
// -----------------------------------------------------------------------------

// static
void ChildImpl::Startup() {
  // This happens on the main thread but before XPCOM has started so we can't
  // assert that we're being called on the main thread here.

  sParentAndContentProcessThreadInfo.Startup();

  sTLSIsOnBackgroundThread.infallibleInit();

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  MOZ_RELEASE_ASSERT(observerService);

  nsCOMPtr<nsIObserver> observer = new ShutdownObserver();

  nsresult rv = observerService->AddObserver(
      observer, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID, false);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));

  // Initialize a starter actor to allow starting PBackground within the parent
  // process.
  if (XRE_IsParentProcess()) {
    Endpoint<PBackgroundStarterParent> parent;
    Endpoint<PBackgroundStarterChild> child;
    MOZ_ALWAYS_SUCCEEDS(PBackgroundStarter::CreateEndpoints(
        EndpointProcInfo::Current(), EndpointProcInfo::Current(), &parent,
        &child));

    MOZ_ALWAYS_TRUE(ParentImpl::AllocStarter(nullptr, std::move(parent),
                                             /* aCrossProcess */ false));
    sParentAndContentProcessThreadInfo.InitStarter(std::move(child));
  }
}

// static
void ChildImpl::Shutdown() {
  AssertIsOnMainThread();

  sParentAndContentProcessThreadInfo.Shutdown();

  sShutdownHasStarted = true;
}

// static
PBackgroundChild* ChildImpl::GetForCurrentThread() {
  MOZ_ASSERT(sParentAndContentProcessThreadInfo.mThreadLocalIndex !=
             kBadThreadLocalIndex);

  auto threadLocalInfo =
      NS_IsMainThread()
          ? sParentAndContentProcessThreadInfo.mMainThreadInfo
          : static_cast<ThreadLocalInfo*>(PR_GetThreadPrivate(
                sParentAndContentProcessThreadInfo.mThreadLocalIndex));

  if (!threadLocalInfo) {
    return nullptr;
  }

  return threadLocalInfo->mActor;
}

/* static */
PBackgroundChild* ChildImpl::GetOrCreateForCurrentThread() {
  MOZ_ASSERT(!ParentImpl::IsOnBackgroundThread(),
             "Creating a PBackgroundChild on the PBackground thread makes a "
             "same-thread connection, which can cause hangs");
  return sParentAndContentProcessThreadInfo.GetOrCreateForCurrentThread();
}

// static
void ChildImpl::CloseForCurrentThread() {
  MOZ_ASSERT(!NS_IsMainThread(),
             "PBackground for the main thread should be shut down via "
             "ChildImpl::Shutdown().");

  sParentAndContentProcessThreadInfo.CloseForCurrentThread();
}

// static
BackgroundChildImpl::ThreadLocal* ChildImpl::GetThreadLocalForCurrentThread() {
  MOZ_ASSERT(sParentAndContentProcessThreadInfo.mThreadLocalIndex !=
                 kBadThreadLocalIndex,
             "BackgroundChild::Startup() was never called!");

  auto threadLocalInfo =
      NS_IsMainThread()
          ? sParentAndContentProcessThreadInfo.mMainThreadInfo
          : static_cast<ThreadLocalInfo*>(PR_GetThreadPrivate(
                sParentAndContentProcessThreadInfo.mThreadLocalIndex));

  if (!threadLocalInfo) {
    return nullptr;
  }

  if (!threadLocalInfo->mConsumerThreadLocal) {
    threadLocalInfo->mConsumerThreadLocal =
        std::make_unique<BackgroundChildImpl::ThreadLocal>();
  }

  return threadLocalInfo->mConsumerThreadLocal.get();
}

// static
void ChildImpl::InitContentStarter(mozilla::dom::ContentChild* aContent) {
  sParentAndContentProcessThreadInfo.InitStarter(aContent);
}

// static
void ChildImpl::ThreadLocalDestructor(void* aThreadLocal) {
  auto threadLocalInfo = static_cast<ThreadLocalInfo*>(aThreadLocal);

  if (threadLocalInfo) {
    MOZ_ASSERT(threadLocalInfo->mClosed);

    if (threadLocalInfo->mActor) {
      threadLocalInfo->mActor->Close();
      MOZ_ASSERT(!threadLocalInfo->mActor->CanSend(),
                 "Actor should be destroyed");
    }

    delete threadLocalInfo;
  }
}

NS_IMPL_ISUPPORTS(ChildImpl::ShutdownObserver, nsIObserver)

NS_IMETHODIMP
ChildImpl::ShutdownObserver::Observe(nsISupports* /*aSubject*/,
                                     const char* aTopic,
                                     const char16_t* /*aData*/) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!strcmp(aTopic, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID));

  ChildImpl::Shutdown();

  return NS_OK;
}
