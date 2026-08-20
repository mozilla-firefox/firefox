/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHostRecord_h_
#define nsHostRecord_h_

#include "PLDHashTable.h"
#include "mozilla/AtomicBitfields.h"
#include "mozilla/DataMutex.h"
#include "mozilla/LinkedList.h"
#include "mozilla/net/HTTPSSVC.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIDNSService.h"
#include "nsITRRSkipReason.h"

class nsHostRecord;
class nsHostResolver;

namespace mozilla {
namespace net {
class HostRecordQueue;
class TRR;
class TRRQuery;
}  // namespace net
}  // namespace mozilla

/**
 * This class is used to notify listeners when a ResolveHost operation is
 * complete. Classes that derive it must implement threadsafe nsISupports
 * to be able to use RefPtr with this class.
 */
class nsResolveHostCallback
    : public mozilla::LinkedListElement<RefPtr<nsResolveHostCallback>>,
      public nsISupports {
 public:
  /**
   * OnResolveHostComplete
   *
   * this function is called to complete a host lookup initiated by
   * nsHostResolver::ResolveHost.  it may be invoked recursively from
   * ResolveHost or on an unspecified background thread.
   *
   * NOTE: it is the responsibility of the implementor of this method
   * to handle the callback in a thread safe manner.
   *
   * @param resolver
   *        nsHostResolver object associated with this result
   * @param record
   *        the host record containing the results of the lookup
   * @param status
   *        if successful, |record| contains non-null results
   */
  virtual void OnResolveHostComplete(nsHostResolver* resolver,
                                     nsHostRecord* record, nsresult status) = 0;
  /**
   * EqualsAsyncListener
   *
   * Determines if the listener argument matches the listener member var.
   * For subclasses not implementing a member listener, should return false.
   * For subclasses having a member listener, the function should check if
   * they are the same.  Used for cases where a pointer to an object
   * implementing nsResolveHostCallback is unknown, but a pointer to
   * the original listener is known.
   *
   * @param aListener
   *        nsIDNSListener object associated with the original request
   */
  virtual bool EqualsAsyncListener(nsIDNSListener* aListener) = 0;

  virtual size_t SizeOfIncludingThis(mozilla::MallocSizeOf) const = 0;

 protected:
  virtual ~nsResolveHostCallback() = default;
};

struct nsHostKey {
  const nsCString host;
  const nsCString mTrrServer;
  uint16_t type = 0;
  mozilla::Atomic<nsIDNSService::DNSFlags> flags{
      nsIDNSService::RESOLVE_DEFAULT_FLAGS};
  uint16_t af = 0;
  bool pb = false;
  const nsCString originSuffix;
  explicit nsHostKey(const nsACString& host, const nsACString& aTrrServer,
                     uint16_t type, nsIDNSService::DNSFlags flags, uint16_t af,
                     bool pb, const nsACString& originSuffix);
  explicit nsHostKey(const nsHostKey& other);
  bool operator==(const nsHostKey& other) const;
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  PLDHashNumber Hash() const;
};

/**
 * nsHostRecord - ref counted object type stored in host resolver cache.
 */
class nsHostRecord : public mozilla::LinkedListElement<RefPtr<nsHostRecord>>,
                     public nsHostKey,
                     public nsISupports {
  using TRRSkippedReason = mozilla::net::TRRSkippedReason;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  virtual size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return 0;
  }

  // Returns the TRR mode encoded by the flags
  nsIRequest::TRRMode TRRMode();

  // Records the first reason that caused TRR to be skipped or to fail.
  void RecordReason(TRRSkippedReason reason) {
    if (mTRRSkippedReason == TRRSkippedReason::TRR_UNSET) {
      mTRRSkippedReason = reason;
    }
  }

  enum DnsPriority {
    DNS_PRIORITY_LOW = nsIDNSService::RESOLVE_PRIORITY_LOW,
    DNS_PRIORITY_MEDIUM = nsIDNSService::RESOLVE_PRIORITY_MEDIUM,
    DNS_PRIORITY_HIGH,
  };

  nsresult GetFromStaleCache(bool* aResult);

 protected:
  friend class nsHostResolver;
  friend class mozilla::net::HostRecordQueue;
  friend class mozilla::net::TRR;
  friend class mozilla::net::TRRQuery;

  using DNSResolverType = mozilla::net::DNSResolverType;

  explicit nsHostRecord(const nsHostKey& key);
  virtual ~nsHostRecord() = default;

  // Mark hostrecord as not usable
  void Invalidate();

  enum ExpirationStatus {
    EXP_VALID,
    EXP_GRACE,
    EXP_EXPIRED,
  };

  ExpirationStatus CheckExpiration(const mozilla::TimeStamp& now) const;

  // Convenience function for setting the timestamps above (mValidStart,
  // mValidEnd, and mGraceStart). valid and grace are durations in seconds.
  void SetExpiration(const mozilla::TimeStamp& now, unsigned int valid,
                     unsigned int grace);
  void CopyExpirationTimesAndFlagsFrom(const nsHostRecord* aFromHostRecord);

  // Checks if the record is usable (not expired and has a value)
  bool HasUsableResult(const mozilla::TimeStamp& now,
                       nsIDNSService::DNSFlags queryFlags =
                           nsIDNSService::RESOLVE_DEFAULT_FLAGS) const;

  static DnsPriority GetPriority(nsIDNSService::DNSFlags aFlags);

  virtual void Cancel();
  virtual bool HasUsableResultInternal(
      const mozilla::TimeStamp& now,
      nsIDNSService::DNSFlags queryFlags) const = 0;
  virtual bool RefreshForNegativeResponse() const { return true; }

  mozilla::LinkedList<RefPtr<nsResolveHostCallback>> mCallbacks;

  bool IsAddrRecord() const {
    return type == nsIDNSService::RESOLVE_TYPE_DEFAULT;
  }

  virtual void Reset() {
    mTRRSkippedReason = TRRSkippedReason::TRR_UNSET;
    mFirstTRRSkippedReason = TRRSkippedReason::TRR_UNSET;
    mTrrAttempts = 0;
    mTRRSuccess = false;
    mNativeSuccess = false;
    mResolverType = DNSResolverType::Native;
  }

  virtual void OnCompleteLookup() {}

  virtual void ResolveComplete() = 0;

  // true if pending and on the queue (not yet given to getaddrinfo())
  bool onQueue() { return LoadNative() && isInList(); }

  mozilla::TimeStamp mLastUpdate = mozilla::TimeStamp::NowLoRes();

  // When the record began being valid. Used mainly for bookkeeping.
  mozilla::TimeStamp mValidStart;

  // When the record is no longer valid (it's time of expiration)
  mozilla::TimeStamp mValidEnd;

  // When the record enters its grace period. This must be before mValidEnd.
  // If a record is in its grace period (and not expired), it will be used
  // but a request to refresh it will be made.
  mozilla::TimeStamp mGraceStart;

  mozilla::TimeDuration mTrrDuration;

  mozilla::Atomic<uint32_t, mozilla::Relaxed> mTtl{0};

  // The computed TRR mode that is actually used by the request.
  // It is set in nsHostResolver::NameLookup and is based on the mode of the
  // default resolver and the TRRMode encoded in the flags.
  // The mode into account if the TRR service is disabled,
  // parental controls are on, domain matches exclusion list, etc.
  mozilla::Atomic<nsIRequest::TRRMode> mEffectiveTRRMode{
      nsIRequest::TRR_DEFAULT_MODE};

  mozilla::Atomic<TRRSkippedReason> mTRRSkippedReason{
      TRRSkippedReason::TRR_UNSET};
  TRRSkippedReason mFirstTRRSkippedReason = TRRSkippedReason::TRR_UNSET;

  mozilla::DataMutex<RefPtr<mozilla::net::TRRQuery>> mTRRQuery;

  // counter of outstanding resolving calls
  mozilla::Atomic<int32_t> mResolving{0};

  // Number of times we've attempted TRR. Reset when we refresh.
  // TRR is attempted at most twice - first attempt and retry.
  mozilla::Atomic<int32_t> mTrrAttempts{0};

  // TRR was used on this record
  mozilla::Atomic<DNSResolverType> mResolverType{DNSResolverType::Native};

  // True if this record is a cache of a failed lookup.  Negative cache
  // entries are valid just like any other (though never for more than 60
  // seconds), but a use of that negative entry forces an asynchronous refresh.
  bool negative = false;

  // Whether the answer handed to the current consumer came from a stale
  // (past-TTL, grace-period) cache entry. Captured at serve time: true only
  // when a grace-period cache hit is returned, and cleared on every fresh
  // resolution.
  bool mFromStaleCache = false;

  // Explicitly expired
  bool mDoomed = false;

  // Whether this is resolved by TRR successfully or not.
  bool mTRRSuccess = false;

  // Whether this is resolved by native resolver successfully or not.
  bool mNativeSuccess = false;

  // When the lookups of this record started and their durations
  mozilla::TimeStamp mNativeStart;
  mozilla::TimeDuration mNativeDuration;

  // clang-format off
  MOZ_ATOMIC_BITFIELDS(mAtomicBitfields, 8, (
    // true if this record is being resolved "natively", which means that
    // it is either on the pending queue or owned by one of the worker threads.
    (uint16_t, Native, 1),
    (uint16_t, NativeUsed, 1),
    // true if off queue and contributing to mActiveAnyThreadCount
    (uint16_t, UsingAnyThread, 1),
    (uint16_t, GetTtl, 1),
    (uint16_t, ResolveAgain, 1)
  ))
  // clang-format on
};

// b020e996-f6ab-45e5-9bf5-1da71dd0053a
#define ADDRHOSTRECORD_IID \
  {0xb020e996, 0xf6ab, 0x45e5, {0x9b, 0xf5, 0x1d, 0xa7, 0x1d, 0xd0, 0x05, 0x3a}}

class AddrHostRecord final : public nsHostRecord {
  using Mutex = mozilla::Mutex;

 public:
  NS_INLINE_DECL_STATIC_IID(ADDRHOSTRECORD_IID)
  NS_DECL_ISUPPORTS_INHERITED

  /* a fully resolved host record has either a non-null |addr_info| or |addr|
   * field.  if |addr_info| is null, it implies that the |host| is an IP
   * address literal.  in which case, |addr| contains the parsed address.
   * otherwise, if |addr_info| is non-null, then it contains one or many
   * IP addresses corresponding to the given host name.  if both |addr_info|
   * and |addr| are null, then the given host has not yet been fully resolved.
   * |af| is the address family of the record we are querying for.
   */

  /* the lock protects |addr_info| and |addr_info_gencnt| because they
   * are mutable and accessed by the resolver worker thread and the
   * nsDNSService2 class.
   */
  mutable Mutex addr_info_lock{"AddrHostRecord.addr_info_lock"};
  // generation count of |addr_info|
  int addr_info_gencnt MOZ_GUARDED_BY(addr_info_lock) = 0;
  RefPtr<mozilla::net::AddrInfo> addr_info MOZ_GUARDED_BY(addr_info_lock);
  mozilla::UniquePtr<mozilla::net::NetAddr> addr;

  // hold addr_info_lock when calling the blocklist functions
  bool Blocklisted(const mozilla::net::NetAddr* query);
  void ResetBlocklist();
  void ReportUnusable(const mozilla::net::NetAddr* aAddress);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const override;

  nsIRequest::TRRMode EffectiveTRRMode() const { return mEffectiveTRRMode; }
  nsITRRSkipReason::value TrrSkipReason() const { return mTRRSkippedReason; }

  nsresult GetTtl(uint32_t* aResult);
  nsresult GetLastUpdate(mozilla::TimeStamp* aLastUpdate);

 private:
  friend class nsHostResolver;
  friend class mozilla::net::HostRecordQueue;
  friend class mozilla::net::TRR;
  friend class mozilla::net::TRRQuery;

  explicit AddrHostRecord(const nsHostKey& key);
  ~AddrHostRecord();

  // Checks if the record is usable (not expired and has a value).
  bool HasUsableResultInternal(
      const mozilla::TimeStamp& now,
      nsIDNSService::DNSFlags queryFlags) const override;

  bool RemoveOrRefresh(bool aTrrToo);  // Mark records currently being resolved
                                       // as needed to resolve again.

  // Saves the skip reason of a first-attempt TRR lookup and clears
  // it to prepare for a retry attempt.
  void NotifyRetryingTrr();

  static DnsPriority GetPriority(nsIDNSService::DNSFlags aFlags);

  virtual void Reset() override {
    nsHostRecord::Reset();
    StoreNativeUsed(false);
  }

  virtual void OnCompleteLookup() override {
    nsHostRecord::OnCompleteLookup();
    // This should always be cleared when a request is completed.
    StoreNative(false);
  }

  void ResolveComplete() override;

  // a list of addresses associated with this record that have been reported
  // as unusable. the list is kept as a set of strings to make it independent
  // of gencnt.
  nsTArray<nsCString> mUnusableItems MOZ_GUARDED_BY(addr_info_lock);
};

// 77b786a7-04be-44f2-987c-ab8aa96676e0
#define TYPEHOSTRECORD_IID \
  {0x77b786a7, 0x04be, 0x44f2, {0x98, 0x7c, 0xab, 0x8a, 0xa9, 0x66, 0x76, 0xe0}}

class TypeHostRecord final : public nsHostRecord,
                             public nsIDNSTXTRecord,
                             public nsIDNSHTTPSSVCRecord,
                             public mozilla::net::DNSHTTPSSVCRecordBase {
 public:
  NS_INLINE_DECL_STATIC_IID(TYPEHOSTRECORD_IID)
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDNSTXTRECORD
  NS_DECL_NSIDNSHTTPSSVCRECORD

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const override;
  uint32_t GetType();
  mozilla::net::TypeRecordResultType GetResults();

 private:
  friend class nsHostResolver;
  friend class mozilla::net::TRR;
  friend class mozilla::net::TRRQuery;

  explicit TypeHostRecord(const nsHostKey& key);
  ~TypeHostRecord();

  // Checks if the record is usable (not expired and has a value)
  bool HasUsableResultInternal(
      const mozilla::TimeStamp& now,
      nsIDNSService::DNSFlags queryFlags) const override;
  bool RefreshForNegativeResponse() const override;

  void ResolveComplete() override;

  mozilla::net::TypeRecordResultType mResults MOZ_GUARDED_BY(mResultsLock) =
      AsVariant(mozilla::Nothing());
  mutable mozilla::Mutex mResultsLock{"TypeHostRecord.mResultsLock"};

  mozilla::Maybe<nsCString> mOriginHost MOZ_GUARDED_BY(mResultsLock);
  bool mAllRecordsExcluded = false;
};

static inline bool IsHighPriority(nsIDNSService::DNSFlags flags) {
  return !(flags & (nsHostRecord::DNS_PRIORITY_LOW |
                    nsHostRecord::DNS_PRIORITY_MEDIUM));
}

static inline bool IsMediumPriority(nsIDNSService::DNSFlags flags) {
  return flags & nsHostRecord::DNS_PRIORITY_MEDIUM;
}

static inline bool IsLowPriority(nsIDNSService::DNSFlags flags) {
  return flags & nsHostRecord::DNS_PRIORITY_LOW;
}

#endif  // nsHostRecord_h_
