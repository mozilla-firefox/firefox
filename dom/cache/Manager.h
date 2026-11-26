/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_Manager_h
#define mozilla_dom_cache_Manager_h

#include "CacheCommon.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/dom/cache/Types.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/StringifyUtils.h"
#include "nsCOMPtr.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTArray.h"

class nsIInputStream;
class nsIThread;

namespace mozilla {

class ErrorResult;

namespace dom {

namespace quota {

class ClientDirectoryLock;

}  // namespace quota

namespace cache {

class CacheOpArgs;
class CacheOpResult;
class CacheRequestResponse;
class Context;
class ManagerId;
struct SavedRequest;
struct SavedResponse;
class StreamList;

// The Manager is class is responsible for performing all of the underlying
// work for a Cache or CacheStorage operation.  The DOM objects and IPC actors
// are basically just plumbing to get the request to the right Manager object
// running in the parent process.
//
// There should be exactly one Manager object for each origin or app using the
// Cache API.  This uniqueness is defined by the ManagerId equality operator.
// The uniqueness is enforced by the Manager GetOrCreate() factory method.
//
// The life cycle of Manager objects is somewhat complex.  While code may
// hold a strong reference to the Manager, it will invalidate itself once it
// believes it has become completely idle.  This is currently determined when
// all of the following conditions occur:
//
//  1) There are no more Manager::Listener objects registered with the Manager
//     by performing a Cache or Storage operation.
//  2) There are no more CacheId references noted via Manager::AddRefCacheId().
//  3) There are no more BodyId references noted via Manager::AddRefBodyId().
//
// In order to keep your Manager alive you should perform an operation to set
// a Listener, call AddRefCacheId(), or call AddRefBodyId().
//
// Even once a Manager becomes invalid, however, it may still continue to
// exist.  This is allowed so that any in-progress Actions can gracefully
// complete.
//
// As an invariant, all Manager objects must cease all IO before shutdown.  This
// is enforced by the Manager::Factory.  If content still holds references to
// Cache DOM objects during shutdown, then all operations will begin rejecting.
class Manager final : public SafeRefCounted<Manager>, public Stringifyable {
  using Client = quota::Client;
  using ClientDirectoryLock = quota::ClientDirectoryLock;

 public:
  // Callback interface implemented by clients of Manager, such as CacheParent
  // and CacheStorageParent.  In general, if you call a Manager method you
  // should expect to receive exactly one On*() callback.  For example, if
  // you call Manager::CacheMatch(), then you should expect to receive
  // OnCacheMatch() back in response.
  //
  // Listener objects are set on a per-operation basis.  So you pass the
  // Listener to a call like Manager::CacheMatch().  Once set in this way,
  // the Manager will continue to reference the Listener until RemoveListener()
  // is called.  This is done to allow the same listener to be used for
  // multiple operations simultaneously without having to maintain an exact
  // count of operations-in-flight.
  //
  // Note, the Manager only holds weak references to Listener objects.
  // Listeners must call Manager::RemoveListener() before they are destroyed
  // to clear these weak references.
  //
  // All public methods should be invoked on the same thread used to create
  // the Manager.
  class Listener {
   public:
    // convenience routines
    void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult);

    void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult,
                      CacheId aOpenedCacheId);

    void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult,
                      const SavedResponse& aSavedResponse,
                      StreamList& aStreamList);

    void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult,
                      const nsTArray<SavedResponse>& aSavedResponseList,
                      StreamList& aStreamList);

    void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult,
                      const nsTArray<SavedRequest>& aSavedRequestList,
                      StreamList& aStreamList);

    struct StreamInfo {
      const nsTArray<SavedResponse>& mSavedResponseList;
      const nsTArray<SavedRequest>& mSavedRequestList;
      StreamList& mStreamList;
    };

    // interface to be implemented
    virtual void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult,
                              CacheId aOpenedCacheId,
                              const Maybe<StreamInfo>& aStreamInfo) {}

   protected:
    ~Listener() = default;
  };

  enum State { Open, Closing };

  static Result<SafeRefPtr<Manager>, nsresult> AcquireCreateIfNonExistent(
      const SafeRefPtr<ManagerId>& aManagerId);

  static void InitiateShutdown();

  static bool IsShutdownAllComplete();

  static nsCString GetShutdownStatus();

  // Cancel actions for given DirectoryLock ids.
  static void Abort(const Client::DirectoryLockIdTable& aDirectoryLockIds);

  // Cancel all actions.
  static void AbortAll();

  // Must be called by Listener objects before they are destroyed.
  void RemoveListener(Listener* aListener);

  // Must be called by Context objects before they are destroyed.
  void RemoveContext(Context& aContext);

  // Forwards to Context::NoteOrphanedData(), if the Context is still alive.
  void NoteOrphanedData();

  // Marks the Manager "invalid".  Once the Context completes no new operations
  // will be permitted with this Manager.  New actors will get a new Manager.
  void NoteClosing();

  State GetState() const;

  // If an actor represents a long term reference to a cache or body stream,
  // then they must call AddRefCacheId() or AddRefBodyId().  This will
  // cause the Manager to keep the backing data store alive for the given
  // object.  The actor must then call ReleaseCacheId() or ReleaseBodyId()
  // exactly once for every AddRef*() call it made.  Any delayed deletion
  // will then be performed.
  void AddRefCacheId(CacheId aCacheId);
  void ReleaseCacheId(CacheId aCacheId);
  void AddRefBodyId(const nsID& aBodyId);
  void ReleaseBodyId(const nsID& aBodyId);

  const ManagerId& GetManagerId() const;

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const;

  // Methods to allow a StreamList to register themselves with the Manager.
  // StreamList objects must call RemoveStreamList() before they are destroyed.
  void AddStreamList(StreamList& aStreamList);
  void RemoveStreamList(StreamList& aStreamList);

  void ExecuteCacheOp(Listener* aListener, CacheId aCacheId,
                      const CacheOpArgs& aOpArgs);
  void ExecutePutAll(
      Listener* aListener, CacheId aCacheId,
      const nsTArray<CacheRequestResponse>& aPutList,
      const nsTArray<nsCOMPtr<nsIInputStream>>& aRequestStreamList,
      const nsTArray<nsCOMPtr<nsIInputStream>>& aResponseStreamList);

  void ExecuteStorageOp(Listener* aListener, Namespace aNamespace,
                        const CacheOpArgs& aOpArgs);

  void ExecuteOpenStream(Listener* aListener, InputStreamResolver&& aResolver,
                         const nsID& aBodyId);

  void NoteStreamOpenComplete(const nsID& aBodyId, ErrorResult&& aRv,
                              nsCOMPtr<nsIInputStream>&& aBodyStream);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  void RecordMayNotDeleteCSCP(
      mozilla::ipc::ActorId aCacheStreamControlParentId);
  void RecordHaveDeletedCSCP(mozilla::ipc::ActorId aCacheStreamControlParentId);
#endif

 private:
  class Factory;
  class BaseAction;
  class DeleteOrphanedCacheAction;

  class CacheMatchAction;
  class CacheMatchAllAction;
  class CachePutAllAction;
  class CacheDeleteAction;
  class CacheKeysAction;

  class StorageMatchAction;
  class StorageHasAction;
  class StorageOpenAction;
  class StorageDeleteAction;
  class StorageKeysAction;

  class OpenStreamAction;

  using ListenerId = uint64_t;

  void Init(Maybe<Manager&> aOldManager);
  void Shutdown();

  void Abort();

  ListenerId SaveListener(Listener* aListener);
  Listener* GetListener(ListenerId aListenerId) const;

  bool SetCacheIdOrphanedIfRefed(CacheId aCacheId);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool IsCacheIdOrphaned(CacheId aCacheId) const;
#endif
  bool SetBodyIdOrphanedIfRefed(const nsID& aBodyId);
  void NoteOrphanedBodyIdList(const nsTArray<nsID>& aDeletedBodyIdList);

  void MaybeAllowContextToClose();

  SafeRefPtr<ManagerId> mManagerId;
  nsCOMPtr<nsIThread> mIOThread;

  // Weak reference cleared by RemoveContext() in Context destructor.
  Context* MOZ_NON_OWNING_REF mContext;

  // Weak references cleared by RemoveListener() in Listener destructors.
  struct ListenerEntry {
    ListenerEntry() : mId(UINT64_MAX), mListener(nullptr) {}

    ListenerEntry(ListenerId aId, Listener* aListener)
        : mId(aId), mListener(aListener) {}

    ListenerId mId;
    Listener* mListener;
  };

  class ListenerEntryIdComparator {
   public:
    bool Equals(const ListenerEntry& aA, const ListenerId& aB) const {
      return aA.mId == aB;
    }
  };

  class ListenerEntryListenerComparator {
   public:
    bool Equals(const ListenerEntry& aA, const Listener* aB) const {
      return aA.mListener == aB;
    }
  };

  using ListenerList = nsTArray<ListenerEntry>;
  ListenerList mListeners;
  static ListenerId sNextListenerId;

  // Weak references cleared by RemoveStreamList() in StreamList destructors.
  nsTArray<NotNull<StreamList*>> mStreamLists;

  bool mShuttingDown;
  State mState;

  struct CacheIdRefCounter {
    CacheId mCacheId;
    MozRefCountType mCount;
    bool mOrphaned;
  };
  nsTArray<CacheIdRefCounter> mCacheIdRefs;

  struct BodyIdRefCounter {
    nsID mBodyId;
    MozRefCountType mCount;
    bool mOrphaned;
  };
  nsTArray<BodyIdRefCounter> mBodyIdRefs;

  struct ConstructorGuard {};

  void DoStringify(nsACString& aData) override;

 public:
  Manager(SafeRefPtr<ManagerId> aManagerId, nsIThread* aIOThread,
          const ConstructorGuard&);
  ~Manager();

  NS_DECL_OWNINGTHREAD
  MOZ_DECLARE_REFCOUNTED_TYPENAME(cache::Manager)
};

}  // namespace cache
}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_cache_Manager_h
