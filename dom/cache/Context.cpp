/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/Context.h"
#include "CacheCommon.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/dom/cache/Action.h"
#include "mozilla/dom/cache/FileUtils.h"
#include "mozilla/dom/cache/Manager.h"
#include "mozilla/dom/cache/ManagerId.h"
#include "mozilla/dom/quota/Assertions.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/PrincipalUtils.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ThreadUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/Maybe.h"
#include "mozIStorageConnection.h"
#include "NotifyUtils.h"
#include "nsIPrincipal.h"
#include "nsIRunnable.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"
#include "QuotaClientImpl.h"

namespace {

using mozilla::dom::cache::Action;
using mozilla::dom::cache::CacheDirectoryMetadata;

class NullAction final : public Action {
 public:
  NullAction() = default;

  virtual void RunOnTarget(mozilla::SafeRefPtr<Resolver> aResolver,
                           const mozilla::Maybe<CacheDirectoryMetadata>&, Data*,
                           const mozilla::Maybe<mozilla::dom::cache::CipherKey>&
                           /* aMaybeCipherKey */) override {
    // Resolve success immediately.  This Action does no actual work.
    MOZ_DIAGNOSTIC_ASSERT(aResolver);
    aResolver->Resolve(NS_OK);
  }
};

}  // namespace

namespace mozilla::dom::cache {

using mozilla::dom::quota::AssertIsOnIOThread;
using mozilla::dom::quota::ClientDirectoryLock;
using mozilla::dom::quota::ClientDirectoryLockHandle;
using mozilla::dom::quota::PERSISTENCE_TYPE_DEFAULT;
using mozilla::dom::quota::PersistenceType;
using mozilla::dom::quota::QuotaManager;
using mozilla::dom::quota::SleepIfEnabled;

class Context::Data final : public Action::Data {
 public:
  explicit Data(nsISerialEventTarget* aTarget) : mTarget(aTarget) {
    MOZ_DIAGNOSTIC_ASSERT(mTarget);
  }

  virtual mozIStorageConnection* GetConnection() const override {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());
    return mConnection;
  }

  virtual void SetConnection(mozIStorageConnection* aConn) override {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());
    MOZ_DIAGNOSTIC_ASSERT(!mConnection);
    mConnection = aConn;
    MOZ_DIAGNOSTIC_ASSERT(mConnection);
  }

 private:
  ~Data() {
    // We could proxy release our data here, but instead just assert.  The
    // Context code should guarantee that we are destroyed on the target
    // thread once the connection is initialized.  If we're not, then
    // QuotaManager might race and try to clear the origin out from under us.
    MOZ_ASSERT_IF(mConnection, mTarget->IsOnCurrentThread());
  }

  nsCOMPtr<nsISerialEventTarget> mTarget;
  nsCOMPtr<mozIStorageConnection> mConnection;

  // Threadsafe counting because we're created on the PBackground thread
  // and destroyed on the target IO thread.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Context::Data)
};

// Executed to perform the complicated dance of steps necessary to initialize
// the QuotaManager.  This must be performed for each origin before any disk
// IO occurrs.
class Context::QuotaInitRunnable final : public nsIRunnable {
 public:
  QuotaInitRunnable(SafeRefPtr<Context> aContext, SafeRefPtr<Manager> aManager,
                    Data* aData, nsISerialEventTarget* aTarget,
                    SafeRefPtr<Action> aInitAction)
      : mContext(std::move(aContext)),
        mThreadsafeHandle(mContext->CreateThreadsafeHandle()),
        mManager(std::move(aManager)),
        mData(aData),
        mTarget(aTarget),
        mInitAction(std::move(aInitAction)),
        mInitiatingEventTarget(GetCurrentSerialEventTarget()),
        mResult(NS_OK),
        mState(STATE_INIT),
        mCanceled(false) {
    MOZ_DIAGNOSTIC_ASSERT(mContext);
    MOZ_DIAGNOSTIC_ASSERT(mManager);
    MOZ_DIAGNOSTIC_ASSERT(mData);
    MOZ_DIAGNOSTIC_ASSERT(mTarget);
    MOZ_DIAGNOSTIC_ASSERT(mInitiatingEventTarget);
    MOZ_DIAGNOSTIC_ASSERT(mInitAction);
  }

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const {
    NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);

    return ToMaybeRef(mDirectoryLockHandle.get());
  }

  nsresult Dispatch() {
    NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);
    MOZ_DIAGNOSTIC_ASSERT(mState == STATE_INIT);

    mState = STATE_GET_INFO;
    nsresult rv = NS_DispatchToMainThread(this, nsIThread::DISPATCH_NORMAL);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mState = STATE_COMPLETE;
      Clear();
    }
    return rv;
  }

  void Cancel() {
    NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);
    MOZ_DIAGNOSTIC_ASSERT(!mCanceled);
    mCanceled = true;
    mInitAction->CancelOnInitiatingThread();
  }

  void DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle);

  void DirectoryLockFailed();

 private:
  class SyncResolver final : public Action::Resolver {
   public:
    SyncResolver() : mResolved(false), mResult(NS_OK) {}

    virtual void Resolve(nsresult aRv) override {
      MOZ_DIAGNOSTIC_ASSERT(!mResolved);
      mResolved = true;
      mResult = aRv;
    };

    bool Resolved() const { return mResolved; }
    nsresult Result() const { return mResult; }

   private:
    ~SyncResolver() = default;

    bool mResolved;
    nsresult mResult;

    NS_INLINE_DECL_REFCOUNTING(Context::QuotaInitRunnable::SyncResolver,
                               override)
  };

  ~QuotaInitRunnable() {
    MOZ_DIAGNOSTIC_ASSERT(mState == STATE_COMPLETE);
    MOZ_DIAGNOSTIC_ASSERT(!mContext);
    MOZ_DIAGNOSTIC_ASSERT(!mInitAction);
  }

  enum State {
    STATE_INIT,
    STATE_GET_INFO,
    STATE_CREATE_QUOTA_MANAGER,
    STATE_WAIT_FOR_DIRECTORY_LOCK,
    STATE_ENSURE_ORIGIN_INITIALIZED,
    STATE_RUN_ON_TARGET,
    STATE_RUNNING,
    STATE_COMPLETING,
    STATE_COMPLETE
  };

  void Complete(nsresult aResult) {
    MOZ_DIAGNOSTIC_ASSERT(mState == STATE_RUNNING || NS_FAILED(aResult));

    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(mResult));
    mResult = aResult;

    mState = STATE_COMPLETING;
    MOZ_ALWAYS_SUCCEEDS(
        mInitiatingEventTarget->Dispatch(this, nsIThread::DISPATCH_NORMAL));
  }

  void Clear() {
    NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);
    MOZ_DIAGNOSTIC_ASSERT(mContext);
    mContext = nullptr;
    mManager = nullptr;
    mInitAction = nullptr;
  }

  SafeRefPtr<Context> mContext;
  SafeRefPtr<ThreadsafeHandle> mThreadsafeHandle;
  SafeRefPtr<Manager> mManager;
  RefPtr<Data> mData;
  nsCOMPtr<nsISerialEventTarget> mTarget;
  SafeRefPtr<Action> mInitAction;
  nsCOMPtr<nsIEventTarget> mInitiatingEventTarget;
  nsresult mResult;
  Maybe<mozilla::ipc::PrincipalInfo> mPrincipalInfo;
  Maybe<CacheDirectoryMetadata> mDirectoryMetadata;
  ClientDirectoryLockHandle mDirectoryLockHandle;
  RefPtr<CipherKeyManager> mCipherKeyManager;
  State mState;
  Atomic<bool> mCanceled;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE
};

void Context::QuotaInitRunnable::DirectoryLockAcquired(
    ClientDirectoryLockHandle aLockHandle) {
  NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);
  MOZ_DIAGNOSTIC_ASSERT(aLockHandle);
  MOZ_DIAGNOSTIC_ASSERT(mState == STATE_WAIT_FOR_DIRECTORY_LOCK);
  MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockHandle);

  mDirectoryLockHandle = std::move(aLockHandle);

  MOZ_DIAGNOSTIC_ASSERT(mDirectoryLockHandle->Id() >= 0);
  mDirectoryMetadata->mDirectoryLockId = mDirectoryLockHandle->Id();

  if (mCanceled || mDirectoryLockHandle->Invalidated()) {
    Complete(NS_ERROR_ABORT);
    return;
  }

  QuotaManager* qm = QuotaManager::Get();
  MOZ_DIAGNOSTIC_ASSERT(qm);

  mState = STATE_ENSURE_ORIGIN_INITIALIZED;
  nsresult rv = qm->IOThread()->Dispatch(this, nsIThread::DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Complete(rv);
    return;
  }

  NotifyDatabaseWorkStarted();
}

void Context::QuotaInitRunnable::DirectoryLockFailed() {
  NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);
  MOZ_DIAGNOSTIC_ASSERT(mState == STATE_WAIT_FOR_DIRECTORY_LOCK);
  MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockHandle);

  NS_WARNING("Failed to acquire a directory lock!");

  Complete(NS_ERROR_FAILURE);
}

NS_IMPL_ISUPPORTS(mozilla::dom::cache::Context::QuotaInitRunnable, nsIRunnable);

// The QuotaManager init state machine is represented in the following diagram:
//
//    +---------------+
//    |     Start     |      Resolve(error)
//    | (Orig Thread) +---------------------+
//    +-------+-------+                     |
//            |                             |
// +----------v-----------+                 |
// |       GetInfo        |  Resolve(error) |
// |    (Main Thread)     +-----------------+
// +----------+-----------+                 |
//            |                             |
// +----------v-----------+                 |
// |  CreateQuotaManager  |  Resolve(error) |
// |    (Orig Thread)     +-----------------+
// +----------+-----------+                 |
//            |                             |
// +----------v-----------+                 |
// | WaitForDirectoryLock |  Resolve(error) |
// |    (Orig Thread)     +-----------------+
// +----------+-----------+                 |
//            |                             |
// +----------v------------+                |
// |EnsureOriginInitialized| Resolve(error) |
// |   (Quota IO Thread)   +----------------+
// +----------+------------+                |
//            |                             |
// +----------v------------+                |
// |     RunOnTarget       | Resolve(error) |
// |   (Target Thread)     +----------------+
// +----------+------------+                |
//            |                             |
//  +---------v---------+            +------v------+
//  |      Running      |            |  Completing |
//  | (Target Thread)   +------------>(Orig Thread)|
//  +-------------------+            +------+------+
//                                          |
//                                    +-----v----+
//                                    | Complete |
//                                    +----------+
//
// The initialization process proceeds through the main states.  If an error
// occurs, then we transition to Completing state back on the original thread.
NS_IMETHODIMP
Context::QuotaInitRunnable::Run() {
  // May run on different threads depending on the state.  See individual
  // state cases for thread assertions.

  SafeRefPtr<SyncResolver> resolver = MakeSafeRefPtr<SyncResolver>();

  switch (mState) {
    // -----------------------------------
    case STATE_GET_INFO: {
      MOZ_ASSERT(NS_IsMainThread());

      auto res = [this]() -> Result<Ok, nsresult> {
        if (mCanceled) {
          return Err(NS_ERROR_ABORT);
        }

        nsCOMPtr<nsIPrincipal> principal = mManager->GetManagerId().Principal();

        mozilla::ipc::PrincipalInfo principalInfo;
        QM_TRY(
            MOZ_TO_RESULT(PrincipalToPrincipalInfo(principal, &principalInfo)));

        mPrincipalInfo.emplace(std::move(principalInfo));

        mState = STATE_CREATE_QUOTA_MANAGER;

        MOZ_ALWAYS_SUCCEEDS(
            mInitiatingEventTarget->Dispatch(this, nsIThread::DISPATCH_NORMAL));

        return Ok{};
      }();

      if (res.isErr()) {
        resolver->Resolve(res.inspectErr());
      }

      break;
    }
    // ----------------------------------
    case STATE_CREATE_QUOTA_MANAGER: {
      NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);

      if (mCanceled || QuotaManager::IsShuttingDown()) {
        resolver->Resolve(NS_ERROR_ABORT);
        break;
      }

      QM_TRY(QuotaManager::EnsureCreated(), QM_PROPAGATE,
             [&resolver](const auto rv) { resolver->Resolve(rv); });

      auto* const quotaManager = QuotaManager::Get();
      MOZ_DIAGNOSTIC_ASSERT(quotaManager);

      QM_TRY_UNWRAP(auto principalMetadata,
                    quota::GetInfoFromValidatedPrincipalInfo(*quotaManager,
                                                             *mPrincipalInfo));

      mDirectoryMetadata.emplace(std::move(principalMetadata));

      mState = STATE_WAIT_FOR_DIRECTORY_LOCK;

      quotaManager
          ->OpenClientDirectory({*mDirectoryMetadata, quota::Client::DOMCACHE})
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              [self = RefPtr(this)](
                  QuotaManager::ClientDirectoryLockHandlePromise::
                      ResolveOrRejectValue&& aValue) {
                if (aValue.IsResolve()) {
                  self->DirectoryLockAcquired(std::move(aValue.ResolveValue()));
                } else {
                  self->DirectoryLockFailed();
                }
              });

      break;
    }
    // ----------------------------------
    case STATE_ENSURE_ORIGIN_INITIALIZED: {
      AssertIsOnIOThread();

      auto res = [this]() -> Result<Ok, nsresult> {
        if (mCanceled) {
          return Err(NS_ERROR_ABORT);
        }

        QuotaManager* quotaManager = QuotaManager::Get();
        MOZ_DIAGNOSTIC_ASSERT(quotaManager);

        QM_TRY_UNWRAP(mDirectoryMetadata->mDir,
                      quotaManager->GetOrCreateTemporaryOriginDirectory(
                          *mDirectoryMetadata));

        auto* cacheQuotaClient = CacheQuotaClient::Get();
        MOZ_DIAGNOSTIC_ASSERT(cacheQuotaClient);

        mCipherKeyManager =
            cacheQuotaClient->GetOrCreateCipherKeyManager(*mDirectoryMetadata);

        SleepIfEnabled(
            StaticPrefs::dom_cache_databaseInitialization_pauseOnIOThreadMs());

        mState = STATE_RUN_ON_TARGET;

        MOZ_ALWAYS_SUCCEEDS(
            mTarget->Dispatch(this, nsIThread::DISPATCH_NORMAL));

        return Ok{};
      }();

      if (res.isErr()) {
        resolver->Resolve(res.inspectErr());
      }

      break;
    }
    // -------------------
    case STATE_RUN_ON_TARGET: {
      MOZ_ASSERT(mTarget->IsOnCurrentThread());

      mState = STATE_RUNNING;

      // Execute the provided initialization Action.  The Action must Resolve()
      // before returning.

      mInitAction->RunOnTarget(
          resolver.clonePtr(), mDirectoryMetadata, mData,
          mCipherKeyManager ? Some(mCipherKeyManager->Ensure()) : Nothing{});

      MOZ_DIAGNOSTIC_ASSERT(resolver->Resolved());

      mData = nullptr;

      // If the database was opened, then we should always succeed when creating
      // the marker file.  If it wasn't opened successfully, then no need to
      // create a marker file anyway.
      if (NS_SUCCEEDED(resolver->Result())) {
        MOZ_ALWAYS_SUCCEEDS(CreateMarkerFile(*mDirectoryMetadata));
      }

      break;
    }
    // -------------------
    case STATE_COMPLETING: {
      NS_ASSERT_OWNINGTHREAD(QuotaInitRunnable);
      mInitAction->CompleteOnInitiatingThread(mResult);

      mContext->OnQuotaInit(mResult, mDirectoryMetadata,
                            std::move(mDirectoryLockHandle),
                            std::move(mCipherKeyManager));

      mState = STATE_COMPLETE;

      // Explicitly cleanup here as the destructor could fire on any of
      // the threads we have bounced through.
      Clear();
      break;
    }
    // -----
    case STATE_WAIT_FOR_DIRECTORY_LOCK:
    default: {
      MOZ_CRASH("unexpected state in QuotaInitRunnable");
    }
  }

  if (resolver->Resolved()) {
    Complete(resolver->Result());
  }

  return NS_OK;
}

// Runnable wrapper around Action objects dispatched on the Context.  This
// runnable executes the Action on the appropriate threads while the Context
// is initialized.
class Context::ActionRunnable final : public nsIRunnable,
                                      public Action::Resolver,
                                      public Context::Activity {
 public:
  ActionRunnable(SafeRefPtr<Context> aContext, Data* aData,
                 nsISerialEventTarget* aTarget, SafeRefPtr<Action> aAction,
                 const Maybe<CacheDirectoryMetadata>& aDirectoryMetadata,
                 RefPtr<CipherKeyManager> aCipherKeyManager)
      : mContext(std::move(aContext)),
        mData(aData),
        mTarget(aTarget),
        mAction(std::move(aAction)),
        mDirectoryMetadata(aDirectoryMetadata),
        mCipherKeyManager(std::move(aCipherKeyManager)),
        mInitiatingThread(GetCurrentSerialEventTarget()),
        mState(STATE_INIT),
        mResult(NS_OK),
        mExecutingRunOnTarget(false) {
    MOZ_DIAGNOSTIC_ASSERT(mContext);
    // mData may be nullptr
    MOZ_DIAGNOSTIC_ASSERT(mTarget);
    MOZ_DIAGNOSTIC_ASSERT(mAction);
    // mDirectoryMetadata.mDir may be nullptr if QuotaInitRunnable failed
    MOZ_DIAGNOSTIC_ASSERT(mInitiatingThread);
  }

  nsresult Dispatch() {
    NS_ASSERT_OWNINGTHREAD(ActionRunnable);
    MOZ_DIAGNOSTIC_ASSERT(mState == STATE_INIT);

    mState = STATE_RUN_ON_TARGET;
    nsresult rv = mTarget->Dispatch(this, nsIEventTarget::DISPATCH_NORMAL);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mState = STATE_COMPLETE;
      Clear();
    }
    return rv;
  }

  virtual bool MatchesCacheId(CacheId aCacheId) const override {
    NS_ASSERT_OWNINGTHREAD(ActionRunnable);
    return mAction->MatchesCacheId(aCacheId);
  }

  virtual void Cancel() override {
    NS_ASSERT_OWNINGTHREAD(ActionRunnable);
    mAction->CancelOnInitiatingThread();
  }

  virtual void Resolve(nsresult aRv) override {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());
    MOZ_DIAGNOSTIC_ASSERT(mState == STATE_RUNNING);

    mResult = aRv;

    // We ultimately must complete on the initiating thread, but bounce through
    // the current thread again to ensure that we don't destroy objects and
    // state out from under the currently running action's stack.
    mState = STATE_RESOLVING;

    // If we were resolved synchronously within Action::RunOnTarget() then we
    // can avoid a thread bounce and just resolve once RunOnTarget() returns.
    // The Run() method will handle this by looking at mState after
    // RunOnTarget() returns.
    if (mExecutingRunOnTarget) {
      return;
    }

    // Otherwise we are in an asynchronous resolve.  And must perform a thread
    // bounce to run on the target thread again.
    MOZ_ALWAYS_SUCCEEDS(mTarget->Dispatch(this, nsIThread::DISPATCH_NORMAL));
  }

 private:
  ~ActionRunnable() {
    MOZ_DIAGNOSTIC_ASSERT(mState == STATE_COMPLETE);
    MOZ_DIAGNOSTIC_ASSERT(!mContext);
    MOZ_DIAGNOSTIC_ASSERT(!mAction);
  }

  void DoStringify(nsACString& aData) override {
    aData.Append("ActionRunnable ("_ns +
                 //
                 "State:"_ns + IntToCString(mState) + kStringifyDelimiter +
                 //
                 "Action:"_ns + IntToCString(static_cast<bool>(mAction)) +
                 kStringifyDelimiter +
                 // TODO: We might want to have Action::Stringify, too.
                 //
                 "Context:"_ns + IntToCString(static_cast<bool>(mContext)) +
                 kStringifyDelimiter +
                 // We do not print out mContext as we most probably were called
                 // by its Stringify.
                 ")"_ns);
  }

  void Clear() {
    NS_ASSERT_OWNINGTHREAD(ActionRunnable);
    MOZ_DIAGNOSTIC_ASSERT(mContext);
    MOZ_DIAGNOSTIC_ASSERT(mAction);
    mContext->RemoveActivity(*this);
    mContext = nullptr;
    mAction = nullptr;
  }

  enum State {
    STATE_INIT,
    STATE_RUN_ON_TARGET,
    STATE_RUNNING,
    STATE_RESOLVING,
    STATE_COMPLETING,
    STATE_COMPLETE
  };

  SafeRefPtr<Context> mContext;
  RefPtr<Data> mData;
  nsCOMPtr<nsISerialEventTarget> mTarget;
  SafeRefPtr<Action> mAction;
  const Maybe<CacheDirectoryMetadata> mDirectoryMetadata;
  RefPtr<CipherKeyManager> mCipherKeyManager;
  nsCOMPtr<nsIEventTarget> mInitiatingThread;
  State mState;
  nsresult mResult;

  // Only accessible on target thread;
  bool mExecutingRunOnTarget;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE
};

NS_IMPL_ISUPPORTS(mozilla::dom::cache::Context::ActionRunnable, nsIRunnable);

// The ActionRunnable has a simpler state machine.  It basically needs to run
// the action on the target thread and then complete on the original thread.
//
//   +-------------+
//   |    Start    |
//   |(Orig Thread)|
//   +-----+-------+
//         |
// +-------v---------+
// |  RunOnTarget    |
// |Target IO Thread)+---+ Resolve()
// +-------+---------+   |
//         |             |
// +-------v----------+  |
// |     Running      |  |
// |(Target IO Thread)|  |
// +------------------+  |
//         | Resolve()   |
// +-------v----------+  |
// |     Resolving    <--+                   +-------------+
// |                  |                      |  Completing |
// |(Target IO Thread)+---------------------->(Orig Thread)|
// +------------------+                      +-------+-----+
//                                                   |
//                                                   |
//                                              +----v---+
//                                              |Complete|
//                                              +--------+
//
// Its important to note that synchronous actions will effectively Resolve()
// out of the Running state immediately.  Asynchronous Actions may remain
// in the Running state for some time, but normally the ActionRunnable itself
// does not see any execution there.  Its all handled internal to the Action.
NS_IMETHODIMP
Context::ActionRunnable::Run() {
  switch (mState) {
    // ----------------------
    case STATE_RUN_ON_TARGET: {
      MOZ_ASSERT(mTarget->IsOnCurrentThread());
      MOZ_DIAGNOSTIC_ASSERT(!mExecutingRunOnTarget);

      // Note that we are calling RunOnTarget().  This lets us detect
      // if Resolve() is called synchronously.
      AutoRestore<bool> executingRunOnTarget(mExecutingRunOnTarget);
      mExecutingRunOnTarget = true;

      mState = STATE_RUNNING;
      mAction->RunOnTarget(
          SafeRefPtrFromThis(), mDirectoryMetadata, mData,
          mCipherKeyManager ? Some(mCipherKeyManager->Ensure()) : Nothing{});

      mData = nullptr;

      // Resolve was called synchronously from RunOnTarget().  We can
      // immediately move to completing now since we are sure RunOnTarget()
      // completed.
      if (mState == STATE_RESOLVING) {
        // Use recursion instead of switch case fall-through...  Seems slightly
        // easier to understand.
        Run();
      }

      break;
    }
    // -----------------
    case STATE_RESOLVING: {
      MOZ_ASSERT(mTarget->IsOnCurrentThread());
      // The call to Action::RunOnTarget() must have returned now if we
      // are running on the target thread again.  We may now proceed
      // with completion.
      mState = STATE_COMPLETING;
      // Shutdown must be delayed until all Contexts are destroyed.  Crash
      // for this invariant violation.
      MOZ_ALWAYS_SUCCEEDS(
          mInitiatingThread->Dispatch(this, nsIThread::DISPATCH_NORMAL));
      break;
    }
    // -------------------
    case STATE_COMPLETING: {
      NS_ASSERT_OWNINGTHREAD(ActionRunnable);
      mAction->CompleteOnInitiatingThread(mResult);
      mState = STATE_COMPLETE;
      // Explicitly cleanup here as the destructor could fire on any of
      // the threads we have bounced through.
      Clear();
      break;
    }
    // -----------------
    default: {
      MOZ_CRASH("unexpected state in ActionRunnable");
      break;
    }
  }
  return NS_OK;
}

void Context::ThreadsafeHandle::AllowToClose() {
  if (mOwningEventTarget->IsOnCurrentThread()) {
    AllowToCloseOnOwningThread();
    return;
  }

  // Dispatch is guaranteed to succeed here because we block shutdown until
  // all Contexts have been destroyed.
  nsCOMPtr<nsIRunnable> runnable = NewRunnableMethod(
      "dom::cache::Context::ThreadsafeHandle::AllowToCloseOnOwningThread", this,
      &ThreadsafeHandle::AllowToCloseOnOwningThread);
  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(runnable.forget(),
                                                   nsIThread::DISPATCH_NORMAL));
}

void Context::ThreadsafeHandle::InvalidateAndAllowToClose() {
  if (mOwningEventTarget->IsOnCurrentThread()) {
    InvalidateAndAllowToCloseOnOwningThread();
    return;
  }

  // Dispatch is guaranteed to succeed here because we block shutdown until
  // all Contexts have been destroyed.
  nsCOMPtr<nsIRunnable> runnable = NewRunnableMethod(
      "dom::cache::Context::ThreadsafeHandle::"
      "InvalidateAndAllowToCloseOnOwningThread",
      this, &ThreadsafeHandle::InvalidateAndAllowToCloseOnOwningThread);
  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(runnable.forget(),
                                                   nsIThread::DISPATCH_NORMAL));
}

Context::ThreadsafeHandle::ThreadsafeHandle(SafeRefPtr<Context> aContext)
    : mStrongRef(std::move(aContext)),
      mWeakRef(mStrongRef.unsafeGetRawPtr()),
      mOwningEventTarget(GetCurrentSerialEventTarget()) {}

Context::ThreadsafeHandle::~ThreadsafeHandle() {
  // Normally we only touch mStrongRef on the owning thread.  This is safe,
  // however, because when we do use mStrongRef on the owning thread we are
  // always holding a strong ref to the ThreadsafeHandle via the owning
  // runnable.  So we cannot run the ThreadsafeHandle destructor simultaneously.
  if (!mStrongRef || mOwningEventTarget->IsOnCurrentThread()) {
    return;
  }

  // Dispatch in NS_ProxyRelease is guaranteed to succeed here because we block
  // shutdown until all Contexts have been destroyed. Therefore it is ok to have
  // MOZ_ALWAYS_SUCCEED here.
  MOZ_ALWAYS_SUCCEEDS(NS_ProxyRelease("Context::ThreadsafeHandle::mStrongRef",
                                      mOwningEventTarget, mStrongRef.forget()));
}

void Context::ThreadsafeHandle::AllowToCloseOnOwningThread() {
  MOZ_ASSERT(mOwningEventTarget->IsOnCurrentThread());

  // A Context "closes" when its ref count drops to zero.  Dropping this
  // strong ref is necessary, but not sufficient for the close to occur.
  // Any outstanding IO will continue and keep the Context alive.  Once
  // the Context is idle, it will be destroyed.

  // First, tell the context to flush any target thread shared data.  This
  // data must be released on the target thread prior to running the Context
  // destructor.  This will schedule an Action which ensures that the
  // ~Context() is not immediately executed when we drop the strong ref.
  if (mStrongRef) {
    mStrongRef->DoomTargetData();
  }

  // Now drop our strong ref and let Context finish running any outstanding
  // Actions.
  mStrongRef = nullptr;
}

void Context::ThreadsafeHandle::InvalidateAndAllowToCloseOnOwningThread() {
  MOZ_ASSERT(mOwningEventTarget->IsOnCurrentThread());
  // Cancel the Context through the weak reference.  This means we can
  // allow the Context to close by dropping the strong ref, but then
  // still cancel ongoing IO if necessary.
  if (mWeakRef) {
    mWeakRef->Invalidate();
  }
  // We should synchronously have AllowToCloseOnOwningThread called when
  // the Context is canceled.
  MOZ_DIAGNOSTIC_ASSERT(!mStrongRef);
}

void Context::ThreadsafeHandle::ContextDestroyed(Context& aContext) {
  MOZ_ASSERT(mOwningEventTarget->IsOnCurrentThread());
  MOZ_DIAGNOSTIC_ASSERT(!mStrongRef);
  MOZ_DIAGNOSTIC_ASSERT(mWeakRef);
  MOZ_DIAGNOSTIC_ASSERT(mWeakRef == &aContext);
  mWeakRef = nullptr;
}

// static
SafeRefPtr<Context> Context::Create(SafeRefPtr<Manager> aManager,
                                    nsISerialEventTarget* aTarget,
                                    SafeRefPtr<Action> aInitAction,
                                    Maybe<Context&> aOldContext) {
  auto context = MakeSafeRefPtr<Context>(std::move(aManager), aTarget,
                                         std::move(aInitAction));
  context->Init(aOldContext);
  return context;
}

Context::Context(SafeRefPtr<Manager> aManager, nsISerialEventTarget* aTarget,
                 SafeRefPtr<Action> aInitAction)
    : mManager(std::move(aManager)),
      mTarget(aTarget),
      mData(new Data(aTarget)),
      mState(STATE_CONTEXT_PREINIT),
      mOrphanedData(false),
      mInitAction(std::move(aInitAction)) {
  MOZ_DIAGNOSTIC_ASSERT(mManager);
  MOZ_DIAGNOSTIC_ASSERT(mTarget);
}

void Context::Dispatch(SafeRefPtr<Action> aAction) {
  NS_ASSERT_OWNINGTHREAD(Context);
  MOZ_DIAGNOSTIC_ASSERT(aAction);
  MOZ_DIAGNOSTIC_ASSERT(mState != STATE_CONTEXT_CANCELED);

  if (mState == STATE_CONTEXT_CANCELED) {
    return;
  }

  if (mState == STATE_CONTEXT_INIT || mState == STATE_CONTEXT_PREINIT) {
    PendingAction* pending = mPendingActions.AppendElement();
    pending->mAction = std::move(aAction);
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(mState == STATE_CONTEXT_READY);
  DispatchAction(std::move(aAction));
}

Maybe<ClientDirectoryLock&> Context::MaybeDirectoryLockRef() const {
  NS_ASSERT_OWNINGTHREAD(Context);

  if (mState == STATE_CONTEXT_PREINIT) {
    MOZ_DIAGNOSTIC_ASSERT(!mInitRunnable);
    MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockHandle);

    return Nothing();
  }

  if (mState == STATE_CONTEXT_INIT) {
    MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockHandle);

    return mInitRunnable->MaybeDirectoryLockRef();
  }

  return ToMaybeRef(mDirectoryLockHandle.get());
}

CipherKeyManager& Context::MutableCipherKeyManagerRef() {
  MOZ_ASSERT(mTarget->IsOnCurrentThread());
  MOZ_DIAGNOSTIC_ASSERT(mCipherKeyManager);

  return *mCipherKeyManager;
}

const Maybe<CacheDirectoryMetadata>& Context::MaybeCacheDirectoryMetadataRef()
    const {
  MOZ_ASSERT(mTarget->IsOnCurrentThread());
  return mDirectoryMetadata;
}

void Context::CancelAll() {
  NS_ASSERT_OWNINGTHREAD(Context);

  // In PREINIT state we have not dispatch the init action yet.  Just
  // forget it.
  if (mState == STATE_CONTEXT_PREINIT) {
    MOZ_DIAGNOSTIC_ASSERT(!mInitRunnable);
    mInitAction = nullptr;

    // In INIT state we have dispatched the runnable, but not received the
    // async completion yet.  Cancel the runnable, but don't forget about it
    // until we get OnQuotaInit() callback.
  } else if (mState == STATE_CONTEXT_INIT) {
    mInitRunnable->Cancel();
  }

  mState = STATE_CONTEXT_CANCELED;
  // Allow completion of pending actions in Context::OnQuotaInit
  if (!mInitRunnable) {
    mPendingActions.Clear();
  }
  for (const auto& activity : mActivityList.ForwardRange()) {
    activity->Cancel();
  }
  AllowToClose();
}

bool Context::IsCanceled() const {
  NS_ASSERT_OWNINGTHREAD(Context);
  return mState == STATE_CONTEXT_CANCELED;
}

void Context::Invalidate() {
  NS_ASSERT_OWNINGTHREAD(Context);
  mManager->NoteClosing();
  CancelAll();
}

void Context::AllowToClose() {
  NS_ASSERT_OWNINGTHREAD(Context);
  if (mThreadsafeHandle) {
    mThreadsafeHandle->AllowToClose();
  }
}

void Context::CancelForCacheId(CacheId aCacheId) {
  NS_ASSERT_OWNINGTHREAD(Context);

  // Remove matching pending actions
  mPendingActions.RemoveElementsBy([aCacheId](const auto& pendingAction) {
    return pendingAction.mAction->MatchesCacheId(aCacheId);
  });

  // Cancel activities and let them remove themselves
  for (const auto& activity : mActivityList.ForwardRange()) {
    if (activity->MatchesCacheId(aCacheId)) {
      activity->Cancel();
    }
  }
}

Context::~Context() {
  NS_ASSERT_OWNINGTHREAD(Context);
  MOZ_DIAGNOSTIC_ASSERT(mManager);
  MOZ_DIAGNOSTIC_ASSERT(!mData);

  if (mThreadsafeHandle) {
    mThreadsafeHandle->ContextDestroyed(*this);
  }

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  // Note, this may set the mOrphanedData flag.
  mManager->RemoveContext(*this);

  if (mDirectoryMetadata && mDirectoryMetadata->mDir && !mOrphanedData) {
    MOZ_ALWAYS_SUCCEEDS(DeleteMarkerFile(*mDirectoryMetadata));
  }

  if (mNextContext) {
    mNextContext->Start();
  }
}

void Context::Init(Maybe<Context&> aOldContext) {
  NS_ASSERT_OWNINGTHREAD(Context);

  if (aOldContext) {
    aOldContext->SetNextContext(SafeRefPtrFromThis());
    return;
  }

  Start();
}

void Context::Start() {
  NS_ASSERT_OWNINGTHREAD(Context);

  // Previous context closing delayed our start, but then we were canceled.
  // In this case, just do nothing here.
  if (mState == STATE_CONTEXT_CANCELED) {
    MOZ_DIAGNOSTIC_ASSERT(!mInitRunnable);
    MOZ_DIAGNOSTIC_ASSERT(!mInitAction);
    // If we can't initialize the quota subsystem we will never be able to
    // clear our shared data object via the target IO thread.  Instead just
    // clear it here to maintain the invariant that the shared data is
    // cleared before Context destruction.
    mData = nullptr;
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(mState == STATE_CONTEXT_PREINIT);
  MOZ_DIAGNOSTIC_ASSERT(!mInitRunnable);

  mInitRunnable =
      new QuotaInitRunnable(SafeRefPtrFromThis(), mManager.clonePtr(), mData,
                            mTarget, std::move(mInitAction));
  mState = STATE_CONTEXT_INIT;

  nsresult rv = mInitRunnable->Dispatch();
  if (NS_FAILED(rv)) {
    // Shutdown must be delayed until all Contexts are destroyed.  Shutdown
    // must also prevent any new Contexts from being constructed.  Crash
    // for this invariant violation.
    MOZ_CRASH("Failed to dispatch QuotaInitRunnable.");
  }
}

void Context::DispatchAction(SafeRefPtr<Action> aAction, bool aDoomData) {
  NS_ASSERT_OWNINGTHREAD(Context);

  auto runnable = MakeSafeRefPtr<ActionRunnable>(
      SafeRefPtrFromThis(), mData, mTarget, std::move(aAction),
      mDirectoryMetadata, mCipherKeyManager);

  if (aDoomData) {
    mData = nullptr;
  }

  nsresult rv = runnable->Dispatch();
  if (NS_FAILED(rv)) {
    // Shutdown must be delayed until all Contexts are destroyed.  Crash
    // for this invariant violation.
    MOZ_CRASH("Failed to dispatch ActionRunnable to target thread.");
  }
  AddActivity(*runnable);
}

void Context::OnQuotaInit(
    nsresult aRv, const Maybe<CacheDirectoryMetadata>& aDirectoryMetadata,
    ClientDirectoryLockHandle aDirectoryLockHandle,
    RefPtr<CipherKeyManager> aCipherKeyManager) {
  NS_ASSERT_OWNINGTHREAD(Context);

  MOZ_DIAGNOSTIC_ASSERT(mInitRunnable);
  mInitRunnable = nullptr;

  MOZ_DIAGNOSTIC_ASSERT(!mDirectoryMetadata);
  mDirectoryMetadata = aDirectoryMetadata;

  // Always save the directory lock to ensure QuotaManager does not shutdown
  // before the Context has gone away.
  MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockHandle);
  mDirectoryLockHandle = std::move(aDirectoryLockHandle);

  MOZ_DIAGNOSTIC_ASSERT(!mCipherKeyManager);
  mCipherKeyManager = std::move(aCipherKeyManager);

  // If we opening the context failed, but we were not explicitly canceled,
  // still treat the entire context as canceled.  We don't want to allow
  // new actions to be dispatched.  We also cannot leave the context in
  // the INIT state after failing to open.
  if (NS_FAILED(aRv)) {
    mState = STATE_CONTEXT_CANCELED;
  }

  if (mState == STATE_CONTEXT_CANCELED) {
    if (NS_SUCCEEDED(aRv)) {
      aRv = NS_ERROR_ABORT;
    }
    for (uint32_t i = 0; i < mPendingActions.Length(); ++i) {
      mPendingActions[i].mAction->CompleteOnInitiatingThread(aRv);
    }
    mPendingActions.Clear();
    mThreadsafeHandle->AllowToClose();
    // Context will destruct after return here and last ref is released.
    return;
  }

  // We could only assert below if quota initialization was a success which
  // is ensured by NS_FAILED(aRv) above
  MOZ_DIAGNOSTIC_ASSERT(mDirectoryMetadata);
  MOZ_DIAGNOSTIC_ASSERT(mDirectoryLockHandle);
  MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockHandle->Invalidated());
  MOZ_DIAGNOSTIC_ASSERT_IF(mDirectoryMetadata->mIsPrivate, mCipherKeyManager);

  MOZ_DIAGNOSTIC_ASSERT(mState == STATE_CONTEXT_INIT);
  mState = STATE_CONTEXT_READY;

  for (uint32_t i = 0; i < mPendingActions.Length(); ++i) {
    DispatchAction(std::move(mPendingActions[i].mAction));
  }
  mPendingActions.Clear();
}

void Context::AddActivity(Activity& aActivity) {
  NS_ASSERT_OWNINGTHREAD(Context);
  MOZ_ASSERT(!mActivityList.Contains(&aActivity));
  mActivityList.AppendElement(WrapNotNullUnchecked(&aActivity));
}

void Context::RemoveActivity(Activity& aActivity) {
  NS_ASSERT_OWNINGTHREAD(Context);
  MOZ_ALWAYS_TRUE(mActivityList.RemoveElement(&aActivity));
  MOZ_ASSERT(!mActivityList.Contains(&aActivity));
}

void Context::NoteOrphanedData() {
  NS_ASSERT_OWNINGTHREAD(Context);
  // This may be called more than once
  mOrphanedData = true;
}

SafeRefPtr<Context::ThreadsafeHandle> Context::CreateThreadsafeHandle() {
  NS_ASSERT_OWNINGTHREAD(Context);
  if (!mThreadsafeHandle) {
    mThreadsafeHandle = MakeSafeRefPtr<ThreadsafeHandle>(SafeRefPtrFromThis());
  }
  return mThreadsafeHandle.clonePtr();
}

void Context::SetNextContext(SafeRefPtr<Context> aNextContext) {
  NS_ASSERT_OWNINGTHREAD(Context);
  MOZ_DIAGNOSTIC_ASSERT(aNextContext);
  MOZ_DIAGNOSTIC_ASSERT(!mNextContext);
  mNextContext = std::move(aNextContext);
}

void Context::DoomTargetData() {
  NS_ASSERT_OWNINGTHREAD(Context);
  MOZ_DIAGNOSTIC_ASSERT(mData);

  // We are about to drop our reference to the Data.  We need to ensure that
  // the ~Context() destructor does not run until contents of Data have been
  // released on the Target thread.

  // Dispatch a no-op Action.  This will hold the Context alive through a
  // roundtrip to the target thread and back to the owning thread.  The
  // ref to the Data object is cleared on the owning thread after creating
  // the ActionRunnable, but before dispatching it.
  DispatchAction(MakeSafeRefPtr<NullAction>(), true /* doomed data */);

  MOZ_DIAGNOSTIC_ASSERT(!mData);
}

void Context::DoStringify(nsACString& aData) {
  NS_ASSERT_OWNINGTHREAD(Context);

  aData.Append(
      "Context "_ns + kStringifyStartInstance +
      //
      "State:"_ns + IntToCString(mState) + kStringifyDelimiter +
      //
      "OrphanedData:"_ns + IntToCString(mOrphanedData) + kStringifyDelimiter +
      //
      "InitRunnable:"_ns + IntToCString(static_cast<bool>(mInitRunnable)) +
      kStringifyDelimiter +
      //
      "InitAction:"_ns + IntToCString(static_cast<bool>(mInitAction)) +
      kStringifyDelimiter +
      //
      "PendingActions:"_ns +
      IntToCString(static_cast<uint64_t>(mPendingActions.Length())) +
      kStringifyDelimiter +
      //
      "ActivityList:"_ns +
      IntToCString(static_cast<uint64_t>(mActivityList.Length())));

  if (mActivityList.Length() > 0) {
    aData.Append(kStringifyStartSet);
    for (auto activity : mActivityList.ForwardRange()) {
      activity->Stringify(aData);
      aData.Append(kStringifyDelimiter);
    }
    aData.Append(kStringifyEndSet);
  };

  aData.Append(kStringifyDelimiter +
               //
               "DirectoryLock:"_ns +
               IntToCString(static_cast<bool>(mDirectoryLockHandle)) +
               kStringifyDelimiter +
               //
               "NextContext:"_ns +
               IntToCString(static_cast<bool>(mNextContext)) +
               //
               kStringifyEndInstance);

  if (mNextContext) {
    aData.Append(kStringifyDelimiter);
    mNextContext->Stringify(aData);
  };

  aData.Append(kStringifyEndInstance);
}

}  // namespace mozilla::dom::cache
