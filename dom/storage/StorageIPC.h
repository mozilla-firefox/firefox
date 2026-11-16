/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_StorageIPC_h
#define mozilla_dom_StorageIPC_h

#include "LocalStorageCache.h"
#include "StorageDBThread.h"
#include "StorageObserver.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/PBackgroundLocalStorageCacheChild.h"
#include "mozilla/dom/PBackgroundLocalStorageCacheParent.h"
#include "mozilla/dom/PBackgroundSessionStorageCacheChild.h"
#include "mozilla/dom/PBackgroundSessionStorageCacheParent.h"
#include "mozilla/dom/PBackgroundSessionStorageManagerChild.h"
#include "mozilla/dom/PBackgroundSessionStorageManagerParent.h"
#include "mozilla/dom/PBackgroundStorageChild.h"
#include "mozilla/dom/PBackgroundStorageParent.h"
#include "mozilla/dom/PSessionStorageObserverChild.h"
#include "mozilla/dom/PSessionStorageObserverParent.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsTHashSet.h"

namespace mozilla {

class OriginAttributesPattern;

namespace ipc {

class BackgroundChildImpl;
class PrincipalInfo;

}  // namespace ipc

namespace dom {

class LocalStorageManager;
class PBackgroundStorageParent;
class PSessionStorageObserverParent;
class SessionStorageCache;
class SessionStorageCacheParent;
class SessionStorageManager;
class SessionStorageManagerParent;
class BackgroundSessionStorageManager;
class SessionStorageObserver;

class LocalStorageCacheChild final : public PBackgroundLocalStorageCacheChild {
  friend class mozilla::ipc::BackgroundChildImpl;
  friend class LocalStorageCache;
  friend class LocalStorageManager;

  // LocalStorageCache effectively owns this instance, although IPC handles its
  // allocation/deallocation.  When the LocalStorageCache destructor runs, it
  // will invoke SendDeleteMeInternal() which will trigger both instances to
  // drop their mutual references and cause IPC to destroy the actor after the
  // DeleteMe round-trip.
  LocalStorageCache* MOZ_NON_OWNING_REF mCache;

  NS_DECL_OWNINGTHREAD

 public:
  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(LocalStorageCacheChild);
  }

 private:
  // Only created by LocalStorageManager.
  explicit LocalStorageCacheChild(LocalStorageCache* aCache);

  // Only destroyed by mozilla::ipc::BackgroundChildImpl.
  ~LocalStorageCacheChild();

  // Only called by LocalStorageCache.
  void SendDeleteMeInternal();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvObserve(NotNull<nsIPrincipal*> aPrincipal,
                                      const uint32_t& aPrivateBrowsingId,
                                      const nsAString& aDocumentURI,
                                      const nsAString& aKey,
                                      const nsAString& aOldValue,
                                      const nsAString& aNewValue) override;
};

// Child side of the IPC protocol, exposes as DB interface but
// is responsible to send all requests to the parent process
// and expects asynchronous answers. Those are then transparently
// forwarded back to consumers on the child process.
class StorageDBChild final : public PBackgroundStorageChild {
  class ShutdownObserver;

  virtual ~StorageDBChild();

 public:
  StorageDBChild(LocalStorageManager* aManager, uint32_t aPrivateBrowsingId);

  static StorageDBChild* Get(uint32_t aPrivateBrowsingId);

  static StorageDBChild* GetOrCreate(uint32_t aPrivateBrowsingId);

  NS_INLINE_DECL_REFCOUNTING(StorageDBChild);

  void AddIPDLReference();
  void ReleaseIPDLReference();

  virtual nsresult Init();
  virtual nsresult Shutdown();

  virtual void AsyncPreload(LocalStorageCacheBridge* aCache,
                            bool aPriority = false);
  virtual void AsyncGetUsage(StorageUsageBridge* aUsage);

  virtual void SyncPreload(LocalStorageCacheBridge* aCache,
                           bool aForceSync = false);

  virtual nsresult AsyncAddItem(LocalStorageCacheBridge* aCache,
                                const nsAString& aKey, const nsAString& aValue);
  virtual nsresult AsyncUpdateItem(LocalStorageCacheBridge* aCache,
                                   const nsAString& aKey,
                                   const nsAString& aValue);
  virtual nsresult AsyncRemoveItem(LocalStorageCacheBridge* aCache,
                                   const nsAString& aKey);
  virtual nsresult AsyncClear(LocalStorageCacheBridge* aCache);

  virtual void AsyncClearAll() {
    if (mOriginsHavingData) {
      mOriginsHavingData->Clear(); /* NO-OP on the child process otherwise */
    }
  }

  virtual void AsyncClearMatchingOrigin(const nsACString& aOriginNoSuffix) {
    MOZ_CRASH("Shouldn't be called!");
  }

  virtual void AsyncClearMatchingOriginAttributes(
      const OriginAttributesPattern& aPattern) {
    MOZ_CRASH("Shouldn't be called!");
  }

  virtual void AsyncFlush() { MOZ_CRASH("Shouldn't be called!"); }

  virtual bool ShouldPreloadOrigin(const nsACString& aOriginNoSuffix);

 private:
  mozilla::ipc::IPCResult RecvObserve(const nsACString& aTopic,
                                      const nsAString& aOriginAttributesPattern,
                                      const nsACString& aOriginScope) override;
  mozilla::ipc::IPCResult RecvLoadItem(const nsACString& aOriginSuffix,
                                       const nsACString& aOriginNoSuffix,
                                       const nsAString& aKey,
                                       const nsAString& aValue) override;
  mozilla::ipc::IPCResult RecvLoadDone(const nsACString& aOriginSuffix,
                                       const nsACString& aOriginNoSuffix,
                                       const nsresult& aRv) override;
  mozilla::ipc::IPCResult RecvOriginsHavingData(
      nsTArray<nsCString>&& aOrigins) override;
  mozilla::ipc::IPCResult RecvLoadUsage(const nsACString& aOriginNoSuffix,
                                        const int64_t& aUsage) override;
  mozilla::ipc::IPCResult RecvError(const nsresult& aRv) override;

  nsTHashSet<nsCString>& OriginsHavingData();

  // Held to get caches to forward answers to.
  RefPtr<LocalStorageManager> mManager;

  // Origins having data hash, for optimization purposes only
  UniquePtr<nsTHashSet<nsCString>> mOriginsHavingData;

  // List of caches waiting for preload.  This ensures the contract that
  // AsyncPreload call references the cache for time of the preload.
  nsTHashSet<RefPtr<LocalStorageCacheBridge>> mLoadingCaches;

  // Expected to be only 0 or 1.
  const uint32_t mPrivateBrowsingId;

  // Status of the remote database
  nsresult mStatus;

  bool mIPCOpen;
};

class SessionStorageObserverChild final : public PSessionStorageObserverChild {
  friend class SessionStorageManager;
  friend class SessionStorageObserver;

  // SessionStorageObserver effectively owns this instance, although IPC handles
  // its allocation/deallocation.  When the SessionStorageObserver destructor
  // runs, it will invoke SendDeleteMeInternal() which will trigger both
  // instances to drop their mutual references and cause IPC to destroy the
  // actor after the DeleteMe round-trip.
  SessionStorageObserver* MOZ_NON_OWNING_REF mObserver;

  NS_DECL_OWNINGTHREAD

 public:
  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(SessionStorageObserverChild);
  }

 private:
  // Only created by SessionStorageManager.
  explicit SessionStorageObserverChild(SessionStorageObserver* aObserver);

  ~SessionStorageObserverChild();

  // Only called by SessionStorageObserver.
  void SendDeleteMeInternal();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvObserve(const nsACString& aTopic,
                                      const nsAString& aOriginAttributesPattern,
                                      const nsACString& aOriginScope) override;
};

class SessionStorageCacheChild final
    : public PBackgroundSessionStorageCacheChild {
  friend class PBackgroundSessionStorageCacheChild;
  friend class SessionStorageCache;
  friend class SessionStorageManager;
  friend class mozilla::ipc::BackgroundChildImpl;

  // SessionStorageManagerChild effectively owns this instance, although IPC
  // handles its allocation/deallocation.  When the SessionStorageManager
  // destructor runs, it will invoke SendDeleteMeInternal() which will trigger
  // both instances to drop their mutual references and cause IPC to destroy the
  // actor after the DeleteMe round-trip.
  SessionStorageCache* MOZ_NON_OWNING_REF mCache;

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::SessionStorageCacheChild, override)

 public:
  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(SessionStorageCacheChild);
  }

 private:
  // Only created by SessionStorageManager.
  explicit SessionStorageCacheChild(SessionStorageCache* aCache);

  // Only destroyed by mozilla::ipc::BackgroundChildImpl.
  ~SessionStorageCacheChild();

  // Only called by SessionStorageCache.
  void SendDeleteMeInternal();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;
};

class SessionStorageManagerChild final
    : public PBackgroundSessionStorageManagerChild {
  friend class PBackgroundSessionStorageManagerChild;
  friend class SessionStorage;
  friend class SessionStorageManager;
  friend class mozilla::ipc::BackgroundChildImpl;

  // SessionStorageManager effectively owns this instance, although IPC handles
  // its allocation/deallocation.  When the SessionStorageManager destructor
  // runs, it will invoke SendDeleteMeInternal() which will trigger both
  // instances to drop their mutual references and cause IPC to destroy the
  // actor after the DeleteMe round-trip.
  SessionStorageManager* MOZ_NON_OWNING_REF mSSManager;

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::SessionStorageManagerChild, override)

 public:
  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(SessionStorageManagerChild);
  }

 private:
  // Only created by SessionStorage.
  explicit SessionStorageManagerChild(SessionStorageManager* aSSManager);

  // Only destroyed by mozilla::ipc::BackgroundChildImpl.
  ~SessionStorageManagerChild();

  // Only called by SessionStorageManager.
  void SendDeleteMeInternal();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvClearStoragesForOrigin(
      const nsACString& aOriginAttrs, const nsACString& aOriginKey) override;
};

class LocalStorageCacheParent final
    : public PBackgroundLocalStorageCacheParent {
  const nsCOMPtr<nsIPrincipal> mPrincipal;
  const nsCString mOriginKey;
  uint32_t mPrivateBrowsingId;
  bool mActorDestroyed;

 public:
  // Created in AllocPBackgroundLocalStorageCacheParent.
  LocalStorageCacheParent(nsIPrincipal* aPrincipal,
                          const nsACString& aOriginKey,
                          uint32_t aPrivateBrowsingId);

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::LocalStorageCacheParent)

  nsIPrincipal* Principal() const { return mPrincipal; }

 private:
  // Reference counted.
  ~LocalStorageCacheParent();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvNotify(const nsAString& aDocumentURI,
                                     const nsAString& aKey,
                                     const nsAString& aOldValue,
                                     const nsAString& aNewValue) override;
};

// Receives async requests from child processes and is responsible
// to send back responses from the DB thread.  Exposes as a fake
// LocalStorageCache consumer.
// Also responsible for forwardning all chrome operation notifications
// such as cookie cleaning etc to the child process.
class StorageDBParent final : public PBackgroundStorageParent {
  class ObserverSink;

  virtual ~StorageDBParent();

 public:
  StorageDBParent(const nsAString& aProfilePath, uint32_t aPrivateBrowsingId);

  void Init();

  NS_IMETHOD_(MozExternalRefCountType) AddRef(void);
  NS_IMETHOD_(MozExternalRefCountType) Release(void);

  void AddIPDLReference();
  void ReleaseIPDLReference();

  bool IPCOpen() { return mIPCOpen; }

 public:
  // Fake cache class receiving async callbacks from DB thread, sending
  // them back to appropriate cache object on the child process.
  class CacheParentBridge : public LocalStorageCacheBridge {
   public:
    CacheParentBridge(StorageDBParent* aParentDB,
                      const nsACString& aOriginSuffix,
                      const nsACString& aOriginNoSuffix)
        : mOwningEventTarget(GetCurrentSerialEventTarget()),
          mParent(aParentDB),
          mOriginSuffix(aOriginSuffix),
          mOriginNoSuffix(aOriginNoSuffix),
          mLoaded(false),
          mLoadedCount(0) {}
    virtual ~CacheParentBridge() = default;

    // LocalStorageCacheBridge
    virtual nsCString Origin() const override;
    virtual const nsCString& OriginNoSuffix() const override {
      return mOriginNoSuffix;
    }
    virtual const nsCString& OriginSuffix() const override {
      return mOriginSuffix;
    }
    virtual bool Loaded() override { return mLoaded; }
    virtual uint32_t LoadedCount() override { return mLoadedCount; }

    virtual bool LoadItem(const nsAString& aKey,
                          const nsAString& aValue) override;
    virtual void LoadDone(nsresult aRv) override;
    virtual void LoadWait() override;

    NS_IMETHOD_(void)
    Release(void) override;

   private:
    void Destroy();

    nsCOMPtr<nsISerialEventTarget> mOwningEventTarget;
    RefPtr<StorageDBParent> mParent;
    nsCString mOriginSuffix, mOriginNoSuffix;
    bool mLoaded;
    uint32_t mLoadedCount;
  };

  // Fake usage class receiving async callbacks from DB thread
  class UsageParentBridge : public StorageUsageBridge {
   public:
    UsageParentBridge(StorageDBParent* aParentDB,
                      const nsACString& aOriginScope)
        : mOwningEventTarget(GetCurrentSerialEventTarget()),
          mParent(aParentDB),
          mOriginScope(aOriginScope) {}
    virtual ~UsageParentBridge() = default;

    // StorageUsageBridge
    virtual const nsCString& OriginScope() override { return mOriginScope; }
    virtual void LoadUsage(const int64_t usage) override;

    NS_IMETHOD_(MozExternalRefCountType)
    Release(void) override;

   private:
    void Destroy();

    nsCOMPtr<nsISerialEventTarget> mOwningEventTarget;
    RefPtr<StorageDBParent> mParent;
    nsCString mOriginScope;
  };

 private:
  // IPC
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvAsyncPreload(const nsACString& aOriginSuffix,
                                           const nsACString& aOriginNoSuffix,
                                           const bool& aPriority) override;
  mozilla::ipc::IPCResult RecvPreload(const nsACString& aOriginSuffix,
                                      const nsACString& aOriginNoSuffix,
                                      const uint32_t& aAlreadyLoadedCount,
                                      nsTArray<nsString>* aKeys,
                                      nsTArray<nsString>* aValues,
                                      nsresult* aRv) override;
  mozilla::ipc::IPCResult RecvAsyncGetUsage(
      const nsACString& aOriginNoSuffix) override;
  mozilla::ipc::IPCResult RecvAsyncAddItem(const nsACString& aOriginSuffix,
                                           const nsACString& aOriginNoSuffix,
                                           const nsAString& aKey,
                                           const nsAString& aValue) override;
  mozilla::ipc::IPCResult RecvAsyncUpdateItem(const nsACString& aOriginSuffix,
                                              const nsACString& aOriginNoSuffix,
                                              const nsAString& aKey,
                                              const nsAString& aValue) override;
  mozilla::ipc::IPCResult RecvAsyncRemoveItem(const nsACString& aOriginSuffix,
                                              const nsACString& aOriginNoSuffix,
                                              const nsAString& aKey) override;
  mozilla::ipc::IPCResult RecvAsyncClear(
      const nsACString& aOriginSuffix,
      const nsACString& aOriginNoSuffix) override;
  mozilla::ipc::IPCResult RecvAsyncFlush() override;

  mozilla::ipc::IPCResult RecvStartup() override;
  mozilla::ipc::IPCResult RecvClearAll() override;
  mozilla::ipc::IPCResult RecvClearMatchingOrigin(
      const nsACString& aOriginNoSuffix) override;
  mozilla::ipc::IPCResult RecvClearMatchingOriginAttributes(
      const OriginAttributesPattern& aPattern) override;

  void Observe(const nsACString& aTopic, const nsAString& aOriginAttrPattern,
               const nsACString& aOriginScope);

 private:
  CacheParentBridge* NewCache(const nsACString& aOriginSuffix,
                              const nsACString& aOriginNoSuffix);

  RefPtr<ObserverSink> mObserverSink;

  // A hack to deal with deadlock between the parent process main thread and
  // background thread when invoking StorageDBThread::GetOrCreate because it
  // cannot safely perform a synchronous dispatch back to the main thread
  // (because we are already synchronously doing things on the stack).
  // Populated for the same process actors, empty for other process actors.
  nsString mProfilePath;

  // Expected to be only 0 or 1.
  const uint32_t mPrivateBrowsingId;

  ThreadSafeAutoRefCnt mRefCnt;
  NS_DECL_OWNINGTHREAD

  // True when IPC channel is open and Send*() methods are OK to use.
  bool mIPCOpen;
};

class SessionStorageObserverParent final : public PSessionStorageObserverParent,
                                           public StorageObserverSink {
  bool mActorDestroyed;

 public:
  // Created in AllocPSessionStorageObserverParent.
  SessionStorageObserverParent();

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::SessionStorageObserverParent)

 private:
  // Reference counted.
  ~SessionStorageObserverParent();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  // StorageObserverSink
  nsresult Observe(const char* aTopic, const nsAString& aOriginAttrPattern,
                   const nsACString& aOriginScope) override;
};

class SessionStorageCacheParent final
    : public PBackgroundSessionStorageCacheParent {
  friend class PBackgroundSessionStorageCacheParent;
  const mozilla::ipc::PrincipalInfo mPrincipalInfo;
  const nsCString mOriginKey;

  RefPtr<SessionStorageManagerParent> mManagerActor;
  FlippedOnce<false> mLoadReceived;

 public:
  SessionStorageCacheParent(const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
                            const nsACString& aOriginKey,
                            SessionStorageManagerParent* aActor);

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::SessionStorageCacheParent, override)

  const mozilla::ipc::PrincipalInfo& PrincipalInfo() const {
    return mPrincipalInfo;
  }
  const nsACString& OriginKey() const { return mOriginKey; }

 private:
  ~SessionStorageCacheParent();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvLoad(nsTArray<SSSetItemInfo>* aData) override;

  mozilla::ipc::IPCResult RecvCheckpoint(
      nsTArray<SSWriteInfo>&& aWriteInfos) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;
};

class SessionStorageManagerParent final
    : public PBackgroundSessionStorageManagerParent {
  friend class PBackgroundSessionStorageManagerParent;

  RefPtr<BackgroundSessionStorageManager> mBackgroundManager;

 public:
  explicit SessionStorageManagerParent(uint64_t aTopContextId);

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::SessionStorageManagerParent,
                             override)

  already_AddRefed<PBackgroundSessionStorageCacheParent>
  AllocPBackgroundSessionStorageCacheParent(
      const PrincipalInfo& aPrincipalInfo,
      const nsACString& aOriginKey) override;

  BackgroundSessionStorageManager* GetManager() const;

  mozilla::ipc::IPCResult RecvClearStorages(
      const OriginAttributesPattern& aPattern, const nsACString& aOriginScope,
      const DomainMatchingMode& aMode) override;

 private:
  ~SessionStorageManagerParent();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;
};

PBackgroundLocalStorageCacheParent* AllocPBackgroundLocalStorageCacheParent(
    nsIPrincipal* aPrincipal, const nsACString& aOriginKey,
    const uint32_t& aPrivateBrowsingId);

mozilla::ipc::IPCResult RecvPBackgroundLocalStorageCacheConstructor(
    mozilla::ipc::PBackgroundParent* aBackgroundActor,
    PBackgroundLocalStorageCacheParent* aActor, const nsACString& aOriginKey,
    const uint32_t& aPrivateBrowsingId);

bool DeallocPBackgroundLocalStorageCacheParent(
    PBackgroundLocalStorageCacheParent* aActor);

PBackgroundStorageParent* AllocPBackgroundStorageParent(
    const nsAString& aProfilePath, const uint32_t& aPrivateBrowsingId);

mozilla::ipc::IPCResult RecvPBackgroundStorageConstructor(
    PBackgroundStorageParent* aActor, const nsAString& aProfilePath,
    const uint32_t& aPrivateBrowsingId);

bool DeallocPBackgroundStorageParent(PBackgroundStorageParent* aActor);

PSessionStorageObserverParent* AllocPSessionStorageObserverParent();

bool RecvPSessionStorageObserverConstructor(
    PSessionStorageObserverParent* aActor);

bool DeallocPSessionStorageObserverParent(
    PSessionStorageObserverParent* aActor);

already_AddRefed<PBackgroundSessionStorageCacheParent>
AllocPBackgroundSessionStorageCacheParent(
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
    const nsACString& aOriginKey);

already_AddRefed<PBackgroundSessionStorageManagerParent>
AllocPBackgroundSessionStorageManagerParent(const uint64_t& aTopContextId);
}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_StorageIPC_h
