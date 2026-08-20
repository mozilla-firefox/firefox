/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHostResolver_h_
#define nsHostResolver_h_

#include "GetAddrInfo.h"
#include "HostRecordQueue.h"
#include "PLDHashTable.h"
#include "mozilla/Atomics.h"
#include "mozilla/CondVar.h"
#include "mozilla/DataMutex.h"
#include "mozilla/Mutex.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/DNSByTypeRecord.h"
#include "mozilla/net/DashboardTypes.h"
#include "mozilla/net/NetworkConnectivityService.h"
#include "nsHostRecord.h"
#include "nsIDNSListener.h"
#include "nsISupportsImpl.h"
#include "nsIThreadPool.h"
#include "nsRefPtrHashtable.h"
#include "nsTArray.h"
#include "nscore.h"
#include "prnetdb.h"

namespace mozilla {
namespace net {
class TRR;
class TRRQuery;

// Clamped to 1 so DequeueNextRecord can always make progress.
static inline uint32_t MaxResolverThreadsAnyPriority() {
  return std::max(StaticPrefs::network_dns_max_any_priority_threads(), 1u);
}

static inline uint32_t MaxResolverThreadsHighPriority() {
  return StaticPrefs::network_dns_max_high_priority_threads();
}

// The sum guarantees that MaxResolverThreadsHighPriority() threads are
// always available for high-priority work even when all any-priority
// slots are occupied by blocking lookups.
static inline uint32_t MaxResolverThreads() {
  return MaxResolverThreadsAnyPriority() + MaxResolverThreadsHighPriority();
}

}  // namespace net
}  // namespace mozilla

#define TRR_DISABLED(x)                       \
  (((x) == nsIDNSService::MODE_NATIVEONLY) || \
   ((x) == nsIDNSService::MODE_TRROFF))

#define MAX_NON_PRIORITY_REQUESTS 150

class AHostResolver {
 public:
  AHostResolver() = default;
  virtual ~AHostResolver() = default;
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  enum LookupStatus {
    LOOKUP_OK,
    LOOKUP_RESOLVEAGAIN,
  };

  virtual LookupStatus CompleteLookup(nsHostRecord*, nsresult,
                                      mozilla::net::AddrInfo*, bool pb,
                                      const nsACString& aOriginsuffix,
                                      mozilla::net::TRRSkippedReason aReason,
                                      mozilla::net::TRR*) = 0;
  virtual LookupStatus CompleteLookupByType(
      nsHostRecord*, nsresult, mozilla::net::TypeRecordResultType& aResult,
      mozilla::net::TRRSkippedReason aReason, uint32_t aTtl, bool pb) = 0;
  virtual nsresult GetHostRecord(const nsACString& host,
                                 const nsACString& aTrrServer, uint16_t type,
                                 nsIDNSService::DNSFlags flags, uint16_t af,
                                 bool pb, const nsCString& originSuffix,
                                 nsHostRecord** result) {
    return NS_ERROR_FAILURE;
  }
  virtual nsresult TrrLookup_unlocked(nsHostRecord*,
                                      mozilla::net::TRR* pushedTRR = nullptr) {
    return NS_ERROR_FAILURE;
  }
  virtual void MaybeRenewHostRecord(nsHostRecord* aRec) {}
};

/**
 * nsHostResolver - an asynchronous host name resolver.
 */
class nsHostResolver : public nsISupports, public AHostResolver {
  using CondVar = mozilla::CondVar;
  using Mutex = mozilla::Mutex;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  /**
   * creates an addref'd instance of a nsHostResolver object.
   */
  static nsresult Create(nsHostResolver** result);
  /**
   * puts the resolver in the shutdown state, which will cause any pending
   * callbacks to be detached.  any future calls to ResolveHost will fail.
   */
  void Shutdown();

  /**
   * resolve the given hostname and originAttributes asynchronously.  the caller
   * can synthesize a synchronous host lookup using a lock and a cvar.  as noted
   * above the callback will occur re-entrantly from an unspecified thread.  the
   * host lookup cannot be canceled (cancelation can be layered above this by
   * having the callback implementation return without doing anything).
   */
  nsresult ResolveHost(const nsACString& aHost, const nsACString& trrServer,
                       int32_t aPort, uint16_t type,
                       const mozilla::OriginAttributes& aOriginAttributes,
                       nsIDNSService::DNSFlags flags, uint16_t af,
                       nsResolveHostCallback* callback);

  nsHostRecord* InitRecord(const nsHostKey& key);
  mozilla::net::NetworkConnectivityService* GetNCS() {
    return mNCS;
  }  // This is actually a singleton

  /**
   * return a resolved hard coded loopback dns record for the specified key
   */
  already_AddRefed<nsHostRecord> InitLoopbackRecord(const nsHostKey& key,
                                                    nsresult* aRv);

  /**
   * return a mock HTTPS record
   */
  already_AddRefed<nsHostRecord> InitMockHTTPSRecord(const nsHostKey& key);

  /**
   * removes the specified callback from the nsHostRecord for the given
   * hostname, originAttributes, flags, and address family.  these parameters
   * should correspond to the parameters passed to ResolveHost.  this function
   * executes the callback if the callback is still pending with the given
   * status.
   */
  void DetachCallback(const nsACString& hostname, const nsACString& trrServer,
                      uint16_t type,
                      const mozilla::OriginAttributes& aOriginAttributes,
                      nsIDNSService::DNSFlags flags, uint16_t af,
                      nsResolveHostCallback* callback, nsresult status);

  /**
   * Cancels an async request associated with the hostname, originAttributes,
   * flags, address family and listener.  Cancels first callback found which
   * matches these criteria.  These parameters should correspond to the
   * parameters passed to ResolveHost.  If this is the last callback associated
   * with the host record, it is removed from any request queues it might be on.
   */
  void CancelAsyncRequest(const nsACString& host, const nsACString& trrServer,
                          uint16_t type,
                          const mozilla::OriginAttributes& aOriginAttributes,
                          nsIDNSService::DNSFlags flags, uint16_t af,
                          nsIDNSListener* aListener, nsresult status);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  /**
   * Flush the DNS cache.
   */
  void FlushCache(bool aTrrToo, bool aFlushEvictionQueue = false);

  LookupStatus CompleteLookup(nsHostRecord*, nsresult, mozilla::net::AddrInfo*,
                              bool pb, const nsACString& aOriginsuffix,
                              mozilla::net::TRRSkippedReason aReason,
                              mozilla::net::TRR* aTRRRequest) override;
  LookupStatus CompleteLookupByType(nsHostRecord*, nsresult,
                                    mozilla::net::TypeRecordResultType& aResult,
                                    mozilla::net::TRRSkippedReason aReason,
                                    uint32_t aTtl, bool pb) override;
  nsresult GetHostRecord(const nsACString& host, const nsACString& trrServer,
                         uint16_t type, nsIDNSService::DNSFlags flags,
                         uint16_t af, bool pb, const nsCString& originSuffix,
                         nsHostRecord** result) override;
  nsresult TrrLookup_unlocked(nsHostRecord*,
                              mozilla::net::TRR* pushedTRR = nullptr) override;
  static nsIDNSService::ResolverMode Mode();

  virtual void MaybeRenewHostRecord(nsHostRecord* aRec) override;

  // Records true if the TRR service is enabled for the record's effective
  // TRR mode. Also records the TRRSkipReason when the TRR service is not
  // available/enabled.
  bool TRRServiceEnabledForRecord(nsHostRecord* aRec)
      MOZ_REQUIRES(mQueue.mLock);

 private:
  explicit nsHostResolver();
  virtual ~nsHostResolver();

  using CallbackArray = nsTArray<RefPtr<nsResolveHostCallback>>;

  // Invoke all callbacks in aCallbacks with the given record and status.
  // Must be called outside any nsHostResolver lock.
  void FireCallbacks(const CallbackArray& aCallbacks, nsHostRecord* aRec,
                     nsresult aStatus);

  // Move all callbacks from a LinkedList into a CallbackArray.
  static void DrainCallbacks(
      mozilla::LinkedList<RefPtr<nsResolveHostCallback>>& aSrc,
      CallbackArray& aDst);

  bool DoRetryTRR(AddrHostRecord* aAddrRec) MOZ_REQUIRES(mQueue.mLock);
  bool MaybeRetryTRRLookup(
      AddrHostRecord* aAddrRec, nsresult aFirstAttemptStatus,
      mozilla::net::TRRSkippedReason aFirstAttemptSkipReason,
      nsresult aChannelStatus) MOZ_REQUIRES(mQueue.mLock);

  LookupStatus CompleteLookupLocked(nsHostRecord*, nsresult&,
                                    mozilla::net::AddrInfo*, bool pb,
                                    const nsACString& aOriginsuffix,
                                    mozilla::net::TRRSkippedReason aReason,
                                    mozilla::net::TRR* aTRRRequest,
                                    CallbackArray& aCallbacks)
      MOZ_REQUIRES(mDBLock) MOZ_REQUIRES(mQueue.mLock);
  LookupStatus CompleteLookupByTypeLocked(
      nsHostRecord*, nsresult&, mozilla::net::TypeRecordResultType& aResult,
      mozilla::net::TRRSkippedReason aReason, uint32_t aTtl, bool pb,
      CallbackArray& aCallbacks) MOZ_REQUIRES(mDBLock)
      MOZ_REQUIRES(mQueue.mLock);
  nsresult Init();
  static void ComputeEffectiveTRRMode(nsHostRecord* aRec);
  nsresult NativeLookup(nsHostRecord* aRec) MOZ_REQUIRES(mQueue.mLock);
  nsresult TrrLookup(nsHostRecord*, mozilla::net::TRR* pushedTRR = nullptr)
      MOZ_REQUIRES(mQueue.mLock);

  // Kick-off a name resolve operation, using native resolver and/or TRR
  nsresult NameLookup(nsHostRecord* aRec) MOZ_REQUIRES(mQueue.mLock);
  // Try to dequeue the next record without blocking.
  already_AddRefed<nsHostRecord> DequeueNextRecord() MOZ_REQUIRES(mQueue.mLock);
  // Dispatch a ResolveHostTask if there's pending work.
  void MaybeDispatchResolveHostTask() MOZ_REQUIRES(mQueue.mLock);

  // Cancels host records in the pending queue and also
  // calls CompleteLookup with the NS_ERROR_ABORT result code.
  void ClearPendingQueue(mozilla::LinkedList<RefPtr<nsHostRecord>>& aPendingQ);

  /**
   * Starts a new lookup in the background for cached entries that are in the
   * grace period or that are are negative.
   *
   * Also records telemetry for type of cache hit (HIT/NEGATIVE_HIT/RENEWAL).
   * Requires both mDBLock (for reading cache) and mQueue.mLock (for queueing
   * work).
   */
  nsresult ConditionallyRefreshRecord(nsHostRecord* rec, const nsACString& host)
      MOZ_REQUIRES(mDBLock) MOZ_REQUIRES(mQueue.mLock);

  void OnResolveComplete(nsHostRecord* aRec) MOZ_REQUIRES(mDBLock)
      MOZ_REQUIRES(mQueue.mLock);

  void AddToEvictionQ(nsHostRecord* rec) MOZ_REQUIRES(mDBLock)
      MOZ_REQUIRES(mQueue.mLock);

  void ResolveHostTask();

  // Resolve the host from the DNS cache.
  already_AddRefed<nsHostRecord> FromCache(nsHostRecord* aRec,
                                           const nsACString& aHost,
                                           uint16_t aType, nsresult& aStatus)
      MOZ_REQUIRES(mDBLock) MOZ_REQUIRES(mQueue.mLock);
  // Called when the host name is an IP address and has been passed.
  already_AddRefed<nsHostRecord> FromCachedIPLiteral(nsHostRecord* aRec);
  // Like the above function, but the host name is not parsed to NetAddr yet.
  already_AddRefed<nsHostRecord> FromIPLiteral(
      AddrHostRecord* aAddrRec, const mozilla::net::NetAddr& aAddr);
  // Called to check if we have an AF_UNSPEC entry in the cache.
  already_AddRefed<nsHostRecord> FromUnspecEntry(
      nsHostRecord* aRec, const nsACString& aHost, const nsACString& aTrrServer,
      const nsACString& aOriginSuffix, uint16_t aType,
      nsIDNSService::DNSFlags aFlags, uint16_t af, bool aPb, nsresult& aStatus)
      MOZ_REQUIRES(mDBLock) MOZ_REQUIRES(mQueue.mLock);

  bool OtherFamilyHasUsablePositiveResult(const nsHostKey& aKey, uint16_t aAf,
                                          const mozilla::TimeStamp& aNow,
                                          nsIDNSService::DNSFlags aFlags)
      MOZ_REQUIRES(mDBLock);

  enum {
    METHOD_HIT = 1,
    METHOD_RENEWAL = 2,
    METHOD_NEGATIVE_HIT = 3,
    METHOD_LITERAL = 4,
    METHOD_OVERFLOW = 5,
    METHOD_NETWORK_FIRST = 6,
    METHOD_NETWORK_SHARED = 7
  };

  // mutable so SizeOfIncludingThis can be const
  // Protects mRecordDB. When held together with mQueue.mLock, always acquire
  // mDBLock first.
  mutable Mutex mDBLock{"nsHostResolver.mDBLock"};
  mozilla::net::HostRecordQueue mQueue;
  nsRefPtrHashtable<nsGenericHashKey<nsHostKey>, nsHostRecord> mRecordDB
      MOZ_GUARDED_BY(mDBLock);
  PRTime mCreationTime;

  RefPtr<nsIThreadPool> mResolverThreads;
  RefPtr<mozilla::net::NetworkConnectivityService>
      mNCS;  // reference to a singleton
  // TODO: mShutdown is set under mQueue.mLock but read without it in a
  // telemetry guard. Could be made non-atomic + MOZ_GUARDED_BY if that
  // guard is removed.
  mozilla::Atomic<bool> mShutdown{true};
  uint32_t mActiveAnyThreadCount MOZ_GUARDED_BY(mQueue.mLock) = 0;

  // Set the expiration time stamps appropriately.
  void PrepareRecordExpirationAddrRecord(AddrHostRecord* rec) const
      MOZ_REQUIRES(rec->addr_info_lock);

 public:
  /*
   * Called by the networking dashboard via the DnsService2
   */
  void GetDNSCacheEntries(nsTArray<mozilla::net::DNSCacheEntries>*);

  static bool IsNativeHTTPSEnabled();
};

#endif  // nsHostResolver_h_
