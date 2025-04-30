/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_Context_h
#define mozilla_dom_cache_Context_h

#include "CacheCipherKeyManager.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/dom/cache/Types.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/StringifyUtils.h"
#include "nsCOMPtr.h"
#include "nsISupportsImpl.h"
#include "nsProxyRelease.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTObserverArray.h"

class nsIEventTarget;
class nsIThread;

namespace mozilla::dom {

namespace quota {

class ClientDirectoryLock;

}  // namespace quota

namespace cache {

class Action;
class Manager;

// The Context class is RAII-style class for managing IO operations within the
// Cache.
//
// When a Context is created it performs the complicated steps necessary to
// initialize the QuotaManager.  Action objects dispatched on the Context are
// delayed until this initialization is complete.  They are then allow to
// execute on any specified thread.  Once all references to the Context are
// gone, then the steps necessary to release the QuotaManager are performed.
// After initialization the Context holds a self reference, so it will stay
// alive until one of three conditions occur:
//
//  1) The Manager will call Context::AllowToClose() when all of the actors
//     have removed themselves as listener.  This means an idle context with
//     no active DOM objects will close gracefully.
//  2) The QuotaManager aborts all operations so it can delete the files.
//     In this case the QuotaManager calls Client::AbortOperationsForLocks()
//     which in turn cancels all existing Action objects and then marks the
//     Manager as invalid.
//  3) Browser shutdown occurs and the Manager calls Context::CancelAll().
//
// In either case, though, the Action objects must be destroyed first to
// allow the Context to be destroyed.
//
// While the Context performs operations asynchronously on threads, all of
// methods in its public interface must be called on the same thread
// originally used to create the Context.
//
// As an invariant, all Context objects must be destroyed before permitting
// the "profile-before-change" shutdown event to complete.  This is ensured
// via the code in ShutdownObserver.cpp.
class Context final : public SafeRefCounted<Context>, public Stringifyable {
  using ClientDirectoryLock = mozilla::dom::quota::ClientDirectoryLock;
  using ClientDirectoryLockHandle =
      mozilla::dom::quota::ClientDirectoryLockHandle;

 public:
  // Define a class allowing other threads to hold the Context alive.  This also
  // allows these other threads to safely close or cancel the Context.
  class ThreadsafeHandle final : public AtomicSafeRefCounted<ThreadsafeHandle> {
    friend class Context;

   public:
    explicit ThreadsafeHandle(SafeRefPtr<Context> aContext);
    ~ThreadsafeHandle();

    MOZ_DECLARE_REFCOUNTED_TYPENAME(cache::Context::ThreadsafeHandle)

    void AllowToClose();
    void InvalidateAndAllowToClose();

   private:
    void AllowToCloseOnOwningThread();
    void InvalidateAndAllowToCloseOnOwningThread();

    void ContextDestroyed(Context& aContext);

    // Cleared to allow the Context to close.  Only safe to access on
    // owning thread.
    SafeRefPtr<Context> mStrongRef;

    // Used to support cancelation even while the Context is already allowed
    // to close.  Cleared by ~Context() calling ContextDestroyed().  Only
    // safe to access on owning thread.
    Context* mWeakRef;

    nsCOMPtr<nsISerialEventTarget> mOwningEventTarget;
  };

  // Different objects hold references to the Context while some work is being
  // performed asynchronously.  These objects must implement the Activity
  // interface and register themselves with the AddActivity().  When they are
  // destroyed they must call RemoveActivity().  This allows the Context to
  // cancel any outstanding Activity work when the Context is cancelled.
  class Activity : public Stringifyable {
   public:
    virtual void Cancel() = 0;
    virtual bool MatchesCacheId(CacheId aCacheId) const = 0;
  };

  // Create a Context attached to the given Manager.  The given Action
  // will run on the QuotaManager IO thread.  Note, this Action must
  // be execute synchronously.
  static SafeRefPtr<Context> Create(SafeRefPtr<Manager> aManager,
                                    nsISerialEventTarget* aTarget,
                                    SafeRefPtr<Action> aInitAction,
                                    Maybe<Context&> aOldContext);

  // Execute given action on the target once the quota manager has been
  // initialized.
  //
  // Only callable from the thread that created the Context.
  void Dispatch(SafeRefPtr<Action> aAction);

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const;

  CipherKeyManager& MutableCipherKeyManagerRef();

  const Maybe<CacheDirectoryMetadata>& MaybeCacheDirectoryMetadataRef() const;

  // Cancel any Actions running or waiting to run.  This should allow the
  // Context to be released and Listener::RemoveContext() will be called
  // when complete.
  //
  // Only callable from the thread that created the Context.
  void CancelAll();

  // True if CancelAll() has been called.
  bool IsCanceled() const;

  // Like CancelAll(), but also marks the Manager as "invalid".
  void Invalidate();

  // Remove any self references and allow the Context to be released when
  // there are no more Actions to process.
  void AllowToClose();

  // Cancel any Actions running or waiting to run that operate on the given
  // cache ID.
  //
  // Only callable from the thread that created the Context.
  void CancelForCacheId(CacheId aCacheId);

  void AddActivity(Activity& aActivity);
  void RemoveActivity(Activity& aActivity);

  // Tell the Context that some state information has been orphaned in the
  // data store and won't be cleaned up.  The Context will leave the marker
  // in place to trigger cleanup the next times its opened.
  void NoteOrphanedData();

 private:
  class Data;
  class QuotaInitRunnable;
  class ActionRunnable;

  enum State {
    STATE_CONTEXT_PREINIT,
    STATE_CONTEXT_INIT,
    STATE_CONTEXT_READY,
    STATE_CONTEXT_CANCELED
  };

  struct PendingAction {
    nsCOMPtr<nsIEventTarget> mTarget;
    SafeRefPtr<Action> mAction;
  };

  void Init(Maybe<Context&> aOldContext);
  void Start();
  void DispatchAction(SafeRefPtr<Action> aAction, bool aDoomData = false);
  void OnQuotaInit(nsresult aRv,
                   const Maybe<CacheDirectoryMetadata>& aDirectoryMetadata,
                   ClientDirectoryLockHandle aDirectoryLockHandle,
                   RefPtr<CipherKeyManager> aCipherKeyManager);

  SafeRefPtr<ThreadsafeHandle> CreateThreadsafeHandle();

  void SetNextContext(SafeRefPtr<Context> aNextContext);

  void DoomTargetData();

  void DoStringify(nsACString& aData) override;

  SafeRefPtr<Manager> mManager;
  nsCOMPtr<nsISerialEventTarget> mTarget;
  RefPtr<Data> mData;
  State mState;
  bool mOrphanedData;
  Maybe<CacheDirectoryMetadata> mDirectoryMetadata;
  RefPtr<QuotaInitRunnable> mInitRunnable;
  SafeRefPtr<Action> mInitAction;
  nsTArray<PendingAction> mPendingActions;

  // Weak refs since activites must remove themselves from this list before
  // being destroyed by calling RemoveActivity().
  nsTObserverArray<NotNull<Activity*>> mActivityList;

  // The ThreadsafeHandle may have a strong ref back to us.  This creates
  // a ref-cycle that keeps the Context alive.  The ref-cycle is broken
  // when ThreadsafeHandle::AllowToClose() is called.
  SafeRefPtr<ThreadsafeHandle> mThreadsafeHandle;

  ClientDirectoryLockHandle mDirectoryLockHandle;
  RefPtr<CipherKeyManager> mCipherKeyManager;
  SafeRefPtr<Context> mNextContext;

 public:
  // XXX Consider adding a private guard parameter.
  Context(SafeRefPtr<Manager> aManager, nsISerialEventTarget* aTarget,
          SafeRefPtr<Action> aInitAction);
  ~Context();

  NS_DECL_OWNINGTHREAD
  MOZ_DECLARE_REFCOUNTED_TYPENAME(cache::Context)
};

}  // namespace cache
}  // namespace mozilla::dom

#endif  // mozilla_dom_cache_Context_h
