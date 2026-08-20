/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIThreadPool.h"
#if defined(HAVE_RES_NINIT)
#  include <arpa/inet.h>
#  include <arpa/nameser.h>
#  include <netinet/in.h>
#  include <resolv.h>
#  include <sys/types.h>
#endif

#include <stdlib.h>

#include <ctime>

#include "GetAddrInfo.h"
#include "PLDHashTable.h"
#include "TRR.h"
#include "TRRQuery.h"
#include "TRRService.h"
#include "mozilla/Atomics.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/glean/NetwerkDnsMetrics.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "nsComponentManagerUtils.h"
#include "nsError.h"
#include "nsHostResolver.h"
#include "nsIOService.h"
#include "nsISupports.h"
#include "nsISupportsUtils.h"
#include "nsIThreadManager.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsQueryObject.h"
#include "nsThreadPool.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"
#include "nsXPCOMCIDInternal.h"
#include "prerror.h"
#include "prthread.h"
#include "prtime.h"
// Put DNSLogging.h at the end to avoid LOG being overwritten by other headers.
#include "DNSLogging.h"

#ifdef XP_WIN
#  include "mozilla/WindowsVersion.h"
#endif  // XP_WIN

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/jni/Utils.h"
#endif

#define IS_ADDR_TYPE(_type) ((_type) == nsIDNSService::RESOLVE_TYPE_DEFAULT)
#define IS_OTHER_TYPE(_type) ((_type) != nsIDNSService::RESOLVE_TYPE_DEFAULT)

using namespace mozilla;
using namespace mozilla::net;

// None of our implementations expose a TTL for negative responses, so we use a
// constant always.
static const unsigned int NEGATIVE_RECORD_LIFETIME = 60;

//----------------------------------------------------------------------------

// Use a persistent thread pool in order to avoid spinning up new threads all
// the time. In particular, thread creation results in a res_init() call from
// libc which is quite expensive.
//
// The pool dynamically grows between 0 and MaxResolverThreads() in size.
// Each native lookup dispatches a ResolveHostTask to the pool. The task
// dequeues a record, resolves it, and returns. The pool manages thread
// creation, idle timeouts, and shutdown. Priority gating (med/low work
// limited by mActiveAnyThreadCount) happens at dequeue time.

using namespace mozilla;

namespace mozilla::net {
LazyLogModule gHostResolverLog("nsHostResolver");
}  // namespace mozilla::net

//----------------------------------------------------------------------------

class DnsThreadListener final : public nsIThreadPoolListener {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITHREADPOOLLISTENER
 private:
  virtual ~DnsThreadListener() = default;
};

NS_IMETHODIMP
DnsThreadListener::OnThreadCreated() { return NS_OK; }

NS_IMETHODIMP
DnsThreadListener::OnThreadShuttingDown() {
  DNSThreadShutdown();
  return NS_OK;
}

NS_IMPL_ISUPPORTS(DnsThreadListener, nsIThreadPoolListener)

//----------------------------------------------------------------------------

mozilla::Atomic<bool, mozilla::Relaxed> sNativeHTTPSSupported{false};

NS_IMPL_ISUPPORTS0(nsHostResolver)

nsHostResolver::nsHostResolver() { mCreationTime = PR_Now(); }

nsHostResolver::~nsHostResolver() = default;

void nsHostResolver::FireCallbacks(const CallbackArray& aCallbacks,
                                   nsHostRecord* aRec, nsresult aStatus) {
  for (const auto& cb : aCallbacks) {
    cb->OnResolveHostComplete(this, aRec, aStatus);
  }
}

/* static */ void nsHostResolver::DrainCallbacks(
    mozilla::LinkedList<RefPtr<nsResolveHostCallback>>& aSrc,
    CallbackArray& aDst) {
  for (nsResolveHostCallback* c = aSrc.getFirst(); c;
       c = c->removeAndGetNext()) {
    aDst.AppendElement(c);
  }
}

nsresult nsHostResolver::Init() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_FAILED(GetAddrInfoInit())) {
    return NS_ERROR_FAILURE;
  }

  LOG(("nsHostResolver::Init this=%p", this));

  mShutdown = false;
  mNCS = NetworkConnectivityService::GetSingleton();

#if defined(HAVE_RES_NINIT)
  // We want to make sure the system is using the correct resolver settings,
  // so we force it to reload those settings whenever we startup a subsequent
  // nsHostResolver instance.  We assume that there is no reason to do this
  // for the first nsHostResolver instance since that is usually created
  // during application startup.
  static int initCount = 0;
  if (initCount++ > 0) {
    auto result = res_ninit(&_res);
    LOG(("nsHostResolver::Init > 'res_ninit' returned %d", result));
  }
#endif

  // We can configure the threadpool to keep threads alive for a while after
  // the last ResolveHostTask has been executed.
  int32_t poolTimeoutSecs =
      StaticPrefs::network_dns_resolver_thread_extra_idle_time_seconds();
  uint32_t poolTimeoutMs;
  if (poolTimeoutSecs < 0) {
    // This means never shut down the idle threads
    poolTimeoutMs = UINT32_MAX;
  } else {
    // We clamp down the idle time between 0 and one hour.
    poolTimeoutMs =
        std::clamp<uint32_t>(poolTimeoutSecs * 1000, 0, 3600 * 1000);
  }

#if defined(XP_WIN)
  // For some reason, the DNSQuery_A API doesn't work on Windows 10.
  // It returns a success code, but no records. We only allow
  // native HTTPS records on Win 11 for now.
  sNativeHTTPSSupported = mozilla::IsWin11OrLater();
#elif defined(MOZ_WIDGET_ANDROID)
  // android_res_nquery only got added in API level 29
  sNativeHTTPSSupported = jni::GetAPIVersion() >= 29;
#elif defined(XP_LINUX) || defined(XP_MACOSX)
  sNativeHTTPSSupported = true;
#endif
  LOG(("Native HTTPS records supported=%d", bool(sNativeHTTPSSupported)));

  nsCOMPtr<nsIThreadPool> threadPool = new nsThreadPool();
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetThreadLimit(MaxResolverThreads()));
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetIdleThreadLimit(8));
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetIdleThreadMaximumTimeout(poolTimeoutMs));
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetIdleThreadGraceTimeout(100));
  MOZ_ALWAYS_SUCCEEDS(
      threadPool->SetThreadStackSize(nsIThreadManager::kThreadPoolStackSize));
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetName("DNS Resolver"_ns));
  nsCOMPtr<nsIThreadPoolListener> listener = new DnsThreadListener();
  threadPool->SetListener(listener);
  mResolverThreads = ToRefPtr(std::move(threadPool));

  return NS_OK;
}

void nsHostResolver::ClearPendingQueue(
    LinkedList<RefPtr<nsHostRecord>>& aPendingQ) {
  // loop through pending queue, erroring out pending lookups.
  if (!aPendingQ.isEmpty()) {
    for (const RefPtr<nsHostRecord>& rec : aPendingQ) {
      rec->Cancel();
      if (rec->IsAddrRecord()) {
        CompleteLookup(rec, NS_ERROR_ABORT, nullptr, rec->pb, rec->originSuffix,
                       rec->mTRRSkippedReason, nullptr);
      } else {
        mozilla::net::TypeRecordResultType empty(Nothing{});
        CompleteLookupByType(rec, NS_ERROR_ABORT, empty, rec->mTRRSkippedReason,
                             0, rec->pb);
      }
    }
  }
}

//
// FlushCache() is what we call when the network has changed. We must not
// trust names that were resolved before this change. They may resolve
// differently now.
//
// This function removes all existing resolved host entries from the hash.
// Names that are in the pending queues can be left there. Entries in the
// cache that have 'Resolve' set true but not 'OnQueue' are being resolved
// right now, so we need to mark them to get re-resolved on completion!

void nsHostResolver::FlushCache(bool aTrrToo, bool aFlushEvictionQueue) {
  MutexAutoLock dbLock(mDBLock);
  MutexAutoLock queueLock(mQueue.mLock);

  if (aFlushEvictionQueue) {
    mQueue.FlushEvictionQ(mRecordDB);
  }

  // Refresh the cache entries that are resolving RIGHT now, remove the rest.
  for (auto iter = mRecordDB.Iter(); !iter.Done(); iter.Next()) {
    nsHostRecord* record = iter.UserData();
    // Try to remove the record, or mark it for refresh.
    // By-type records are from TRR. We do not need to flush those entry
    // when the network has change, because they are not local.
    if (record->IsAddrRecord()) {
      RefPtr<AddrHostRecord> addrRec = do_QueryObject(record);
      MOZ_ASSERT(addrRec);
      if (addrRec->RemoveOrRefresh(aTrrToo)) {
        mQueue.MaybeRemoveFromQ(record);
        LOG(("Removing (%s) Addr record from mRecordDB", record->host.get()));
        iter.Remove();
      }
    } else if (aTrrToo) {
      // remove by type records
      LOG(("Removing (%s) type record from mRecordDB", record->host.get()));
      iter.Remove();
    }
  }
}

void nsHostResolver::Shutdown() {
  LOG(("Shutting down host resolver.\n"));

  // Callbacks collected during ClearAll; fired outside the lock below.
  struct PendingAbort {
    RefPtr<nsHostRecord> rec;
    CallbackArray cbs;
    nsresult status;
  };
  nsTArray<PendingAbort> shutdownCallbacks;

  {
    MutexAutoLock dbLock(mDBLock);
    MutexAutoLock queueLock(mQueue.mLock);

    mShutdown = true;

    mQueue.ClearAll([&](nsHostRecord* aRec) MOZ_REQUIRES(mDBLock) MOZ_REQUIRES(
                        mQueue.mLock) {
      mQueue.mLock.AssertCurrentThreadOwns();
      CallbackArray cbs;
      nsresult status = NS_ERROR_ABORT;
      if (aRec->IsAddrRecord()) {
        CompleteLookupLocked(aRec, status, nullptr, aRec->pb,
                             aRec->originSuffix, aRec->mTRRSkippedReason,
                             nullptr, cbs);
      } else {
        mozilla::net::TypeRecordResultType empty(Nothing{});
        CompleteLookupByTypeLocked(aRec, status, empty, aRec->mTRRSkippedReason,
                                   0, aRec->pb, cbs);
      }
      if (!cbs.IsEmpty()) {
        shutdownCallbacks.AppendElement(
            PendingAbort{aRec, std::move(cbs), status});
      }
    });

    for (const auto& data : mRecordDB.Values()) {
      data->Cancel();
    }
    // empty host database
    mRecordDB.Clear();
  }

  // Fire collected callbacks now that the lock is released.
  for (auto& pending : shutdownCallbacks) {
    FireCallbacks(pending.cbs, pending.rec, pending.status);
  }

  mNCS = nullptr;

  // Shutdown the resolver threads, but with a timeout of 5 seconds (prefable).
  // If the timeout is exceeded, any stuck threads will be leaked.
  mResolverThreads->ShutdownWithTimeout(
      StaticPrefs::network_dns_resolver_shutdown_timeout_ms());

  {
    mozilla::DebugOnly<nsresult> rv = GetAddrInfoShutdown();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to shutdown GetAddrInfo");
  }
}

nsresult nsHostResolver::GetHostRecord(
    const nsACString& host, const nsACString& aTrrServer, uint16_t type,
    nsIDNSService::DNSFlags flags, uint16_t af, bool pb,
    const nsCString& originSuffix, nsHostRecord** result) {
  MutexAutoLock dbLock(mDBLock);
  nsHostKey key(host, aTrrServer, type, flags, af, pb, originSuffix);

  RefPtr<nsHostRecord> rec =
      mRecordDB.LookupOrInsertWith(key, [&] { return InitRecord(key); });
  if (rec->IsAddrRecord()) {
    RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
    if (addrRec->addr) {
      return NS_ERROR_FAILURE;
    }
  }

  if (rec->mResolving) {
    return NS_ERROR_FAILURE;
  }

  *result = rec.forget().take();
  return NS_OK;
}

nsHostRecord* nsHostResolver::InitRecord(const nsHostKey& key) {
  if (IS_ADDR_TYPE(key.type)) {
    return new AddrHostRecord(key);
  }
  return new TypeHostRecord(key);
}

namespace {
class NetAddrIPv6FirstComparator {
 public:
  static bool Equals(const NetAddr& aLhs, const NetAddr& aRhs) {
    return aLhs.raw.family == aRhs.raw.family;
  }
  static bool LessThan(const NetAddr& aLhs, const NetAddr& aRhs) {
    return aLhs.raw.family > aRhs.raw.family;
  }
};
}  // namespace

already_AddRefed<nsHostRecord> nsHostResolver::InitLoopbackRecord(
    const nsHostKey& key, nsresult* aRv) {
  MOZ_ASSERT(aRv);
  MOZ_ASSERT(IS_ADDR_TYPE(key.type));

  *aRv = NS_ERROR_FAILURE;
  RefPtr<nsHostRecord> rec = InitRecord(key);

  nsTArray<NetAddr> addresses;
  NetAddr addr;
  if (key.af == PR_AF_INET || key.af == PR_AF_UNSPEC) {
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(addr.InitFromString("127.0.0.1"_ns)));
    addresses.AppendElement(addr);
  }
  if (key.af == PR_AF_INET6 || key.af == PR_AF_UNSPEC) {
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(addr.InitFromString("::1"_ns)));
    addresses.AppendElement(addr);
  }

  if (StaticPrefs::network_dns_preferIPv6() && addresses.Length() > 1 &&
      addresses[0].IsIPAddrV4()) {
    // Sort IPv6 addresses first.
    addresses.Sort(NetAddrIPv6FirstComparator());
  }

  RefPtr<AddrInfo> ai =
      new AddrInfo(rec->host, DNSResolverType::Native, 0, std::move(addresses));

  RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
  MutexAutoLock lock(addrRec->addr_info_lock);
  addrRec->addr_info = ai;
  addrRec->SetExpiration(TimeStamp::NowLoRes(),
                         StaticPrefs::network_dnsCacheExpiration(),
                         StaticPrefs::network_dnsCacheExpirationGracePeriod());
  addrRec->negative = false;
  // Use the oldest possible timestamp, since the contents of this record never
  // change.
  addrRec->mLastUpdate = TimeStamp::ProcessCreation();

  *aRv = NS_OK;
  return rec.forget();
}

already_AddRefed<nsHostRecord> nsHostResolver::InitMockHTTPSRecord(
    const nsHostKey& key) {
  MOZ_ASSERT(IS_OTHER_TYPE(key.type));
  if (key.type != nsIDNSService::RESOLVE_TYPE_HTTPSSVC) {
    return nullptr;
  }

  RefPtr<nsHostRecord> rec = InitRecord(key);
  LOG(("InitMockHTTPSRecord host=%s\n", rec->host.get()));

  TypeRecordResultType result = AsVariant(mozilla::Nothing());
  uint32_t ttl = UINT32_MAX;
  nsresult rv =
      CreateAndResolveMockHTTPSRecord(rec->host, rec->flags, result, ttl);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  RefPtr<TypeHostRecord> typeRec = do_QueryObject(rec);
  MutexAutoLock lock(typeRec->mResultsLock);
  typeRec->mResults = result;
  typeRec->negative = false;
  return rec.forget();
}

// static
bool nsHostResolver::IsNativeHTTPSEnabled() {
  if (!StaticPrefs::network_dns_native_https_query()) {
    return false;
  }
#ifdef XP_WIN
  if (StaticPrefs::network_dns_native_https_query_win10()) {
    // If this pref is true, we allow resolving HTTPS records.
    // It might not work, or we might use the HTTPS override records.
    return true;
  }
#endif
  return sNativeHTTPSSupported;
}

nsresult nsHostResolver::ResolveHost(const nsACString& aHost,
                                     const nsACString& aTrrServer,
                                     int32_t aPort, uint16_t type,
                                     const OriginAttributes& aOriginAttributes,
                                     nsIDNSService::DNSFlags flags, uint16_t af,
                                     nsResolveHostCallback* aCallback) {
  nsAutoCString host(aHost);
  NS_ENSURE_TRUE(!host.IsEmpty(), NS_ERROR_UNEXPECTED);

  nsAutoCString originSuffix;
  aOriginAttributes.CreateSuffix(originSuffix);
  LOG(("Resolving host [%s]<%s>%s%s type %d. [this=%p]\n", host.get(),
       originSuffix.get(),
       flags & nsIDNSService::RESOLVE_BYPASS_CACHE ? " - bypassing cache" : "",
       flags & nsIDNSService::RESOLVE_REFRESH_CACHE ? " - refresh cache" : "",
       type, this));

  // When this pref is set, we always set the flag, to make sure consumers
  // that forget to set the flag don't end up being a cache miss.
  if (StaticPrefs::network_dns_always_ai_canonname()) {
    flags |= nsIDNSService::RESOLVE_CANONICAL_NAME;
  }

  // ensure that we are working with a valid hostname before proceeding.  see
  // bug 304904 for details.
  if (!net_IsValidDNSHost(host)) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  // If TRR is disabled we can return immediately if the native API is disabled
  if (!IsNativeHTTPSEnabled() && IS_OTHER_TYPE(type) &&
      Mode() == nsIDNSService::MODE_TRROFF) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  // Used to try to parse to an IP address literal.
  NetAddr tempAddr;
  if (IS_OTHER_TYPE(type) && (NS_SUCCEEDED(tempAddr.InitFromString(host)))) {
    // For by-type queries the host cannot be IP literal.
    return NS_ERROR_UNKNOWN_HOST;
  }

  RefPtr<nsResolveHostCallback> callback(aCallback);
  // if result is set inside the lock, then we need to issue the
  // callback before returning.
  RefPtr<nsHostRecord> result;
  nsresult status = NS_OK, rv = NS_OK;
  {
    MutexAutoLock dbLock(mDBLock);
    MutexAutoLock queueLock(mQueue.mLock);

    if (mShutdown) {
      return NS_ERROR_NOT_INITIALIZED;
    }

    // check to see if there is already an entry for this |host|
    // in the hash table.  if so, then check to see if we can't
    // just reuse the lookup result.  otherwise, if there are
    // any pending callbacks, then add to pending callbacks queue,
    // and return.  otherwise, add ourselves as first pending
    // callback, and proceed to do the lookup.

    Maybe<nsCString> originHost;
    if (StaticPrefs::network_dns_port_prefixed_qname_https_rr() &&
        type == nsIDNSService::RESOLVE_TYPE_HTTPSSVC && aPort != -1 &&
        aPort != 443) {
      originHost = Some(host);
      host = nsPrintfCString("_%d._https.%s", aPort, host.get());
      LOG(("  Using port prefixed host name [%s]", host.get()));
    }

    bool excludedFromTRR = false;
    if (TRRService::Get() &&
        TRRService::Get()->IsExcludedFromTRR(
            host, nsIDNSService::GetTRRModeFromFlags(flags))) {
      flags |= nsIDNSService::RESOLVE_DISABLE_TRR;
      flags |= nsIDNSService::RESOLVE_DISABLE_NATIVE_HTTPS_QUERY;
      excludedFromTRR = true;

      if (!aTrrServer.IsEmpty()) {
        return NS_ERROR_UNKNOWN_HOST;
      }
    }

    nsHostKey key(host, aTrrServer, type, flags, af,
                  (aOriginAttributes.IsPrivateBrowsing()), originSuffix);

    // Fast paths: resolve directly without touching the record DB.
    // `result` is set and the callback fires outside the lock.
    if (IS_ADDR_TYPE(type) && IsLoopbackHostname(host)) {
      nsresult initRv;
      result = InitLoopbackRecord(key, &initRv);
      if (NS_WARN_IF(NS_FAILED(initRv))) {
        return initRv;
      }
      MOZ_ASSERT(result);
    } else if (flags & nsIDNSService::RESOLVE_CREATE_MOCK_HTTPS_RR) {
      result = InitMockHTTPSRecord(key);
      status = result ? NS_OK : NS_ERROR_UNKNOWN_HOST;
    } else {
      RefPtr<nsHostRecord> rec =
          mRecordDB.LookupOrInsertWith(key, [&] { return InitRecord(key); });

      RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
      MOZ_ASSERT(rec, "Record should not be null");
      MOZ_ASSERT((IS_ADDR_TYPE(type) && rec->IsAddrRecord() && addrRec) ||
                 (IS_OTHER_TYPE(type) && !rec->IsAddrRecord()));

      if (IS_OTHER_TYPE(type) && originHost) {
        RefPtr<TypeHostRecord> typeRec = do_QueryObject(rec);
        MutexAutoLock lock(typeRec->mResultsLock);
        typeRec->mOriginHost = std::move(originHost);
      }

      if (excludedFromTRR) {
        rec->RecordReason(TRRSkippedReason::TRR_EXCLUDED);
      }

      TimeStamp now = TimeStamp::NowLoRes();

      // Happy Eyeballs issues per-family (A/AAAA) high-priority lookups and
      // reuses cached negatives (see AddrHostRecord::HasUsableResultInternal).
      // When both families are negative the failure is likely transient, so
      // force a refresh rather than let it stick across reloads. This only
      // applies to per-family lookups; an AF_UNSPEC lookup (e.g. the ordinary
      // DnsAndConnectSocket connect path) is not part of Happy Eyeballs and
      // must keep reusing its cached negative.
      if (IS_ADDR_TYPE(type) && addrRec && addrRec->negative &&
          (af == PR_AF_INET || af == PR_AF_INET6) && IsHighPriority(flags) &&
          StaticPrefs::network_http_happy_eyeballs_enabled() &&
          !OtherFamilyHasUsablePositiveResult(key, af, now, flags)) {
        flags |= nsIDNSService::RESOLVE_REFRESH_NEGATIVE_CACHE;
      }

      if (!(flags & nsIDNSService::RESOLVE_BYPASS_CACHE) &&
          rec->HasUsableResult(now, flags)) {
        result = FromCache(rec, host, type, status);
      } else if (addrRec && addrRec->addr) {
        // if the host name is an IP address literal and has been
        // parsed, go ahead and use it.
        LOG(("  Using cached address for IP Literal [%s].\n", host.get()));
        result = FromCachedIPLiteral(rec);
      } else if (addrRec && NS_SUCCEEDED(tempAddr.InitFromString(host))) {
        // try parsing the host name as an IP address literal to short
        // circuit full host resolution.  (this is necessary on some
        // platforms like Win9x.  see bug 219376 for more details.)
        LOG(("  Host is IP Literal [%s].\n", host.get()));
        result = FromIPLiteral(addrRec, tempAddr);
      } else if (mQueue.PendingCount() >= MAX_NON_PRIORITY_REQUESTS &&
                 !IsHighPriority(flags) && !rec->mResolving) {
        LOG(
            ("  Lookup queue full: dropping %s priority request for "
             "host [%s].\n",
             IsMediumPriority(flags) ? "medium" : "low", host.get()));
        if (IS_ADDR_TYPE(type)) {
          glean::dns::lookup_method.AccumulateSingleSample(METHOD_OVERFLOW);
        }
        // This is a lower priority request and we are swamped, so refuse it.
        rv = NS_ERROR_DNS_LOOKUP_QUEUE_FULL;

        // Check if the offline flag is set.
      } else if (flags & nsIDNSService::RESOLVE_OFFLINE) {
        LOG(("  Offline request for host [%s]; ignoring.\n", host.get()));
        rv = NS_ERROR_OFFLINE;

        // We do not have a valid result till here.
        // A/AAAA request can check for an alternative entry like AF_UNSPEC.
        // Otherwise we need to start a new query.
      } else if (!rec->mResolving) {
        result =
            FromUnspecEntry(rec, host, aTrrServer, originSuffix, type, flags,
                            af, aOriginAttributes.IsPrivateBrowsing(), status);
        // If this is a by-type request or if no valid record was found
        // in the cache or this is an AF_UNSPEC request, then start a
        // new lookup.
        if (!result) {
          LOG(("  No usable record in cache for host [%s] type %d.", host.get(),
               type));

          if (flags & nsIDNSService::RESOLVE_REFRESH_CACHE) {
            rec->Invalidate();
          }

          // Add callback to the list of pending callbacks.
          rec->mCallbacks.insertBack(callback);
          rec->flags = flags;
          rv = NameLookup(rec);
          if (IS_ADDR_TYPE(type)) {
            glean::dns::lookup_method.AccumulateSingleSample(
                METHOD_NETWORK_FIRST);
          }
          if (NS_FAILED(rv) && callback->isInList()) {
            callback->remove();
          } else {
            LOG(
                ("  DNS lookup for host [%s] blocking "
                 "pending 'getaddrinfo' or trr query: "
                 "callback [%p]",
                 host.get(), callback.get()));
          }
        }
      } else {
        LOG(
            ("  Host [%s] is being resolved. Appending callback "
             "[%p].",
             host.get(), callback.get()));

        rec->mCallbacks.insertBack(callback);

        if (rec && rec->onQueue()) {
          glean::dns::lookup_method.AccumulateSingleSample(
              METHOD_NETWORK_SHARED);

          // Consider the case where we are on a pending queue of
          // lower priority than the request is being made at.
          // In that case we should upgrade to the higher queue.

          if (IsHighPriority(flags) && !IsHighPriority(rec->flags)) {
            // Move from (low|med) to high.
            mQueue.MoveToAnotherPendingQ(rec, flags);
            rec->flags = flags;
            MaybeDispatchResolveHostTask();
          } else if (IsMediumPriority(flags) && IsLowPriority(rec->flags)) {
            // Move from low to med.
            mQueue.MoveToAnotherPendingQ(rec, flags);
            rec->flags = flags;
          }
        }
      }

      if (result && callback->isInList()) {
        callback->remove();
      }
    }  // else (main path)

  }  // lock

  if (result) {
    callback->OnResolveHostComplete(this, result, status);
  }

  return rv;
}

already_AddRefed<nsHostRecord> nsHostResolver::FromCache(
    nsHostRecord* aRec, const nsACString& aHost, uint16_t aType,
    nsresult& aStatus) {
  LOG(("  Using cached record for host [%s].\n",
       nsPromiseFlatCString(aHost).get()));

  // put reference to host record on stack...
  RefPtr<nsHostRecord> result = aRec;

  aRec->mFromStaleCache =
      aRec->CheckExpiration(TimeStamp::NowLoRes()) == nsHostRecord::EXP_GRACE;

  // For cached entries that are in the grace period or negative, use the cache
  // but start a new lookup in the background.
  //
  // Also records telemetry for type of cache hit (HIT/NEGATIVE_HIT/RENEWAL).
  ConditionallyRefreshRecord(aRec, aHost);

  if (aRec->negative) {
    LOG(("  Negative cache entry for host [%s].\n",
         nsPromiseFlatCString(aHost).get()));
    aStatus = NS_ERROR_UNKNOWN_HOST;
  } else if (StaticPrefs::network_dns_mru_to_tail()) {
    mQueue.MoveToEvictionQueueTail(aRec);
  }

  return result.forget();
}

already_AddRefed<nsHostRecord> nsHostResolver::FromCachedIPLiteral(
    nsHostRecord* aRec) {
  glean::dns::lookup_method.AccumulateSingleSample(METHOD_LITERAL);
  RefPtr<nsHostRecord> result = aRec;
  return result.forget();
}

already_AddRefed<nsHostRecord> nsHostResolver::FromIPLiteral(
    AddrHostRecord* aAddrRec, const NetAddr& aAddr) {
  // ok, just copy the result into the host record, and be
  // done with it! ;-)
  aAddrRec->addr = MakeUnique<NetAddr>(aAddr);
  glean::dns::lookup_method.AccumulateSingleSample(METHOD_LITERAL);
  // put reference to host record on stack...
  RefPtr<nsHostRecord> result = aAddrRec;
  return result.forget();
}

bool nsHostResolver::OtherFamilyHasUsablePositiveResult(
    const nsHostKey& aKey, uint16_t aAf, const mozilla::TimeStamp& aNow,
    nsIDNSService::DNSFlags aFlags) {
  uint16_t otherAf;
  if (aAf == PR_AF_INET) {
    otherAf = PR_AF_INET6;
  } else if (aAf == PR_AF_INET6) {
    otherAf = PR_AF_INET;
  } else {
    MOZ_ASSERT_UNREACHABLE("only called for per-family lookups");
    return false;
  }

  const nsHostKey key(aKey.host, aKey.mTrrServer,
                      nsIDNSService::RESOLVE_TYPE_DEFAULT, aFlags, otherAf,
                      aKey.pb, aKey.originSuffix);
  RefPtr<nsHostRecord> rec = mRecordDB.Get(key);
  if (!rec) {
    return false;
  }
  RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
  return addrRec && !addrRec->negative && rec->HasUsableResult(aNow, aFlags);
}

already_AddRefed<nsHostRecord> nsHostResolver::FromUnspecEntry(
    nsHostRecord* aRec, const nsACString& aHost, const nsACString& aTrrServer,
    const nsACString& aOriginSuffix, uint16_t aType,
    nsIDNSService::DNSFlags aFlags, uint16_t af, bool aPb, nsresult& aStatus) {
  RefPtr<nsHostRecord> result = nullptr;
  // If this is an IPV4 or IPV6 specific request, check if there is
  // an AF_UNSPEC entry we can use. Otherwise, hit the resolver...
  RefPtr<AddrHostRecord> addrRec = do_QueryObject(aRec);
  if (addrRec && !(aFlags & nsIDNSService::RESOLVE_BYPASS_CACHE) &&
      ((af == PR_AF_INET) || (af == PR_AF_INET6))) {
    // Check for an AF_UNSPEC entry.

    const nsHostKey unspecKey(aHost, aTrrServer,
                              nsIDNSService::RESOLVE_TYPE_DEFAULT, aFlags,
                              PR_AF_UNSPEC, aPb, aOriginSuffix);
    RefPtr<nsHostRecord> unspecRec = mRecordDB.Get(unspecKey);

    TimeStamp now = TimeStamp::NowLoRes();
    if (unspecRec && unspecRec->HasUsableResult(now, aFlags)) {
      MOZ_ASSERT(unspecRec->IsAddrRecord());

      RefPtr<AddrHostRecord> addrUnspecRec = do_QueryObject(unspecRec);
      MOZ_ASSERT(addrUnspecRec);
      LOG(("  Trying AF_UNSPEC entry for host [%s] af: %s.\n",
           PromiseFlatCString(aHost).get(),
           (af == PR_AF_INET) ? "AF_INET" : "AF_INET6"));

      // Scope the unspec lock to read and filter addresses, then
      // release it before acquiring addrRec's lock. Both are the same
      // lock class so holding them simultaneously trips the deadlock
      // detector.
      RefPtr<AddrInfo> filteredAddrInfo;
      {
        MutexAutoLock unspecLock(addrUnspecRec->addr_info_lock);
        MOZ_ASSERT(addrUnspecRec->addr_info || addrUnspecRec->negative,
                   "Entry should be resolved or negative.");
        if (addrUnspecRec->addr_info) {
          // Search for any valid address in the AF_UNSPEC entry
          // in the cache (not blocklisted and from the right
          // family).
          nsTArray<NetAddr> addresses;
          for (const auto& addr : addrUnspecRec->addr_info->Addresses()) {
            if ((af == addr.inet.family) &&
                !addrUnspecRec->Blocklisted(&addr)) {
              addresses.AppendElement(addr);
            }
          }
          if (!addresses.IsEmpty()) {
            filteredAddrInfo = new AddrInfo(
                addrUnspecRec->addr_info->Hostname(),
                addrUnspecRec->addr_info->CanonicalHostname(),
                addrUnspecRec->addr_info->ResolverType(),
                addrUnspecRec->addr_info->TRRType(), std::move(addresses));
          }
        }
      }

      {
        MutexAutoLock lock(addrRec->addr_info_lock);
        addrRec->addr_info = nullptr;
        addrRec->addr_info_gencnt++;
        if (unspecRec->negative) {
          aRec->negative = unspecRec->negative;
          aRec->CopyExpirationTimesAndFlagsFrom(unspecRec);
        } else if (filteredAddrInfo) {
          addrRec->addr_info = std::move(filteredAddrInfo);
          addrRec->addr_info_gencnt++;
          aRec->CopyExpirationTimesAndFlagsFrom(unspecRec);
        }
      }
      // Now check if we have a new record.
      if (aRec->HasUsableResult(now, aFlags)) {
        result = aRec;
        if (aRec->negative) {
          aStatus = NS_ERROR_UNKNOWN_HOST;
        }
        ConditionallyRefreshRecord(aRec, aHost);
      } else if (af == PR_AF_INET6) {
        // For AF_INET6, a new lookup means another AF_UNSPEC
        // lookup. We have already iterated through the
        // AF_UNSPEC addresses, so we mark this record as
        // negative.
        LOG(
            ("  No AF_INET6 in AF_UNSPEC entry: "
             "host [%s] unknown host.",
             nsPromiseFlatCString(aHost).get()));
        result = aRec;
        aRec->negative = true;
        aStatus = NS_ERROR_UNKNOWN_HOST;
        // this record has just been marked as negative so we record the
        // telemetry for it.
        glean::dns::lookup_method.AccumulateSingleSample(METHOD_NEGATIVE_HIT);
      }
    }
  }

  return result.forget();
}

void nsHostResolver::DetachCallback(
    const nsACString& host, const nsACString& aTrrServer, uint16_t aType,
    const OriginAttributes& aOriginAttributes, nsIDNSService::DNSFlags flags,
    uint16_t af, nsResolveHostCallback* aCallback, nsresult status) {
  RefPtr<nsHostRecord> rec;
  RefPtr<nsResolveHostCallback> callback(aCallback);

  {
    MutexAutoLock dbLock(mDBLock);
    MutexAutoLock queueLock(mQueue.mLock);

    nsAutoCString originSuffix;
    aOriginAttributes.CreateSuffix(originSuffix);

    nsHostKey key(host, aTrrServer, aType, flags, af,
                  (aOriginAttributes.IsPrivateBrowsing()), originSuffix);
    RefPtr<nsHostRecord> entry = mRecordDB.Get(key);
    if (entry) {
      // walk list looking for |callback|... we cannot assume
      // that it will be there!

      for (nsResolveHostCallback* c : entry->mCallbacks) {
        if (c == callback) {
          rec = entry;
          c->remove();
          break;
        }
      }
    }
  }

  // complete callback with the given status code; this would only be done if
  // the record was in the process of being resolved.
  if (rec) {
    callback->OnResolveHostComplete(this, rec, status);
  }
}

already_AddRefed<nsHostRecord> nsHostResolver::DequeueNextRecord() {
  mQueue.mLock.AssertCurrentThreadOwns();

#define SET_GET_TTL(var, val) \
  (var)->StoreGetTtl(StaticPrefs::network_dns_get_ttl() && (val))

  RefPtr<nsHostRecord> rec = mQueue.Dequeue(true);
  if (rec) {
    SET_GET_TTL(rec, false);
    return rec.forget();
  }

  if (mActiveAnyThreadCount < MaxResolverThreadsAnyPriority()) {
    rec = mQueue.Dequeue(false);
    if (rec) {
      MOZ_ASSERT(IsMediumPriority(rec->flags) || IsLowPriority(rec->flags));
      mActiveAnyThreadCount++;
      rec->StoreUsingAnyThread(true);
      SET_GET_TTL(rec, true);
      return rec.forget();
    }
  }

#undef SET_GET_TTL

  return nullptr;
}

void nsHostResolver::MaybeDispatchResolveHostTask() {
  mQueue.mLock.AssertCurrentThreadOwns();
  if (!mQueue.PendingCount() || mShutdown) {
    return;
  }
  nsCOMPtr<nsIRunnable> event =
      mozilla::NewRunnableMethod("nsHostResolver::ResolveHostTask", this,
                                 &nsHostResolver::ResolveHostTask);
  DebugOnly<nsresult> rv =
      mResolverThreads->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "MaybeDispatchResolveHostTask: Dispatch failed");
}

nsresult nsHostResolver::TrrLookup_unlocked(nsHostRecord* rec, TRR* pushedTRR) {
  MutexAutoLock queueLock(mQueue.mLock);
  return TrrLookup(rec, pushedTRR);
}

void nsHostResolver::MaybeRenewHostRecord(nsHostRecord* aRec) {
  MutexAutoLock queueLock(mQueue.mLock);
  mQueue.MaybeRenewHostRecord(aRec);
}

bool nsHostResolver::TRRServiceEnabledForRecord(nsHostRecord* aRec) {
  MOZ_ASSERT(aRec, "Record must not be empty");
  MOZ_ASSERT(aRec->mEffectiveTRRMode != nsIRequest::TRR_DEFAULT_MODE,
             "effective TRR mode must be computed before this call");
  if (!TRRService::Get()) {
    aRec->RecordReason(TRRSkippedReason::TRR_NO_GSERVICE);
    return false;
  }

  // We always try custom resolvers.
  if (!aRec->mTrrServer.IsEmpty()) {
    return true;
  }

  nsIRequest::TRRMode reqMode = aRec->mEffectiveTRRMode;
  if (TRRService::Get()->Enabled(reqMode)) {
    return true;
  }

  if (gIOService->InSleepMode()) {
    aRec->RecordReason(TRRSkippedReason::TRR_SYSTEM_SLEEP_MODE);
    return false;
  }
  if (NS_IsOffline()) {
    // If we are in the NOT_CONFIRMED state _because_ we lack connectivity,
    // then we should report that the browser is offline instead.
    aRec->RecordReason(TRRSkippedReason::TRR_BROWSER_IS_OFFLINE);
    return false;
  }

  auto hasConnectivity = [this]() -> bool {
    mQueue.mLock.AssertCurrentThreadOwns();
    if (!mNCS) {
      return true;
    }
    nsINetworkConnectivityService::ConnectivityState ipv4 = mNCS->GetIPv4();
    nsINetworkConnectivityService::ConnectivityState ipv6 = mNCS->GetIPv6();

    if (ipv4 == nsINetworkConnectivityService::OK ||
        ipv6 == nsINetworkConnectivityService::OK) {
      return true;
    }

    if (ipv4 == nsINetworkConnectivityService::UNKNOWN ||
        ipv6 == nsINetworkConnectivityService::UNKNOWN) {
      // One of the checks hasn't completed yet. Optimistically assume we'll
      // have network connectivity.
      return true;
    }

    return false;
  };

  if (!hasConnectivity()) {
    aRec->RecordReason(TRRSkippedReason::TRR_NO_CONNECTIVITY);
    return false;
  }

  bool isConfirmed = TRRService::Get()->IsConfirmed();
  if (!isConfirmed) {
    aRec->RecordReason(TRRSkippedReason::TRR_NOT_CONFIRMED);
  }

  return isConfirmed;
}

// returns error if no TRR resolve is issued
// it is impt this is not called while a native lookup is going on
nsresult nsHostResolver::TrrLookup(nsHostRecord* aRec, TRR* pushedTRR) {
  if (Mode() == nsIDNSService::MODE_TRROFF ||
      StaticPrefs::network_dns_disabled()) {
    return NS_ERROR_UNKNOWN_HOST;
  }
  LOG(("TrrLookup host:%s af:%" PRId16, aRec->host.get(), aRec->af));

  RefPtr<nsHostRecord> rec(aRec);
  mQueue.mLock.AssertCurrentThreadOwns();

  RefPtr<AddrHostRecord> addrRec;
  RefPtr<TypeHostRecord> typeRec;

  if (rec->IsAddrRecord()) {
    addrRec = do_QueryObject(rec);
    MOZ_ASSERT(addrRec);
  } else {
    typeRec = do_QueryObject(rec);
    MOZ_ASSERT(typeRec);
  }

  MOZ_ASSERT(!rec->mResolving);

  if (!TRRServiceEnabledForRecord(aRec)) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  mQueue.MaybeRenewHostRecord(rec);

  RefPtr<TRRQuery> query = new TRRQuery(this, rec);
  nsresult rv = query->DispatchLookup(pushedTRR);
  if (NS_FAILED(rv)) {
    rec->RecordReason(TRRSkippedReason::TRR_DID_NOT_MAKE_QUERY);
    return rv;
  }

  {
    auto lock = rec->mTRRQuery.Lock();
    MOZ_ASSERT(!lock.ref(), "TRR already in progress");
    lock.ref() = query;
  }

  rec->mResolving++;
  rec->mTrrAttempts++;
  rec->StoreNative(false);
  return NS_OK;
}

nsresult nsHostResolver::NativeLookup(nsHostRecord* aRec) {
  if (StaticPrefs::network_dns_disabled()) {
    return NS_ERROR_UNKNOWN_HOST;
  }
  LOG(("NativeLookup host:%s af:%" PRId16, aRec->host.get(), aRec->af));

  // If this is not a A/AAAA request, make sure native HTTPS is enabled.
  MOZ_ASSERT(aRec->IsAddrRecord() || IsNativeHTTPSEnabled());
  mQueue.mLock.AssertCurrentThreadOwns();

  if (aRec->type == nsIDNSService::RESOLVE_TYPE_HTTPSSVC &&
      TRRService::Get()->IsExcludedFromTRR(aRec->host, aRec->TRRMode())) {
    // If the host should be excluded from TRR
    // (meaning it's a local domain or in /etc/hosts)
    // then we probably shouldn't be using the HTTPS record for it either.
    // Or otherwise we shouldn't use the record for ECH.
    return NS_ERROR_UNKNOWN_HOST;
  }

  RefPtr<nsHostRecord> rec(aRec);

  rec->mNativeStart = TimeStamp::Now();

  // Add rec to one of the pending queues, possibly removing it from mEvictionQ.
  mQueue.MaybeRenewHostRecord(aRec);

  mQueue.InsertRecord(rec, rec->flags);

  rec->StoreNative(true);
  rec->StoreNativeUsed(true);
  rec->mResolving++;

  MaybeDispatchResolveHostTask();

  LOG(("  DNS thread counters: any-live=%d pending=%d\n",
       static_cast<uint32_t>(mActiveAnyThreadCount), mQueue.PendingCount()));

  return NS_OK;
}

// static
nsIDNSService::ResolverMode nsHostResolver::Mode() {
  if (TRRService::Get()) {
    return TRRService::Get()->Mode();
  }

  // If we don't have a TRR service just return MODE_TRROFF so we don't make
  // any TRR requests by mistake.
  return nsIDNSService::MODE_TRROFF;
}

nsIRequest::TRRMode nsHostRecord::TRRMode() {
  return nsIDNSService::GetTRRModeFromFlags(flags);
}

// static
void nsHostResolver::ComputeEffectiveTRRMode(nsHostRecord* aRec) {
  nsIDNSService::ResolverMode resolverMode = nsHostResolver::Mode();
  nsIRequest::TRRMode requestMode = aRec->TRRMode();

  // For domains that are excluded from TRR or when parental control is enabled,
  // we fallback to NativeLookup. This happens even in MODE_TRRONLY. By default
  // localhost and local are excluded (so we cover *.local hosts) See the
  // network.trr.excluded-domains pref.

  if (!TRRService::Get()) {
    aRec->RecordReason(TRRSkippedReason::TRR_NO_GSERVICE);
    aRec->mEffectiveTRRMode = requestMode;
    return;
  }

  if (!aRec->mTrrServer.IsEmpty()) {
    aRec->mEffectiveTRRMode = nsIRequest::TRR_ONLY_MODE;
    return;
  }

  if (TRRService::Get()->IsExcludedFromTRR(aRec->host, requestMode)) {
    aRec->RecordReason(TRRSkippedReason::TRR_EXCLUDED);
    aRec->mEffectiveTRRMode = nsIRequest::TRR_DISABLED_MODE;
    return;
  }

  if (TRRService::Get()->ParentalControlEnabled()) {
    aRec->RecordReason(TRRSkippedReason::TRR_PARENTAL_CONTROL);
    aRec->mEffectiveTRRMode = nsIRequest::TRR_DISABLED_MODE;
    return;
  }

  if (resolverMode == nsIDNSService::MODE_TRROFF) {
    aRec->RecordReason(TRRSkippedReason::TRR_OFF_EXPLICIT);
    aRec->mEffectiveTRRMode = nsIRequest::TRR_DISABLED_MODE;
    return;
  }

  if (requestMode == nsIRequest::TRR_DISABLED_MODE) {
    aRec->RecordReason(TRRSkippedReason::TRR_REQ_MODE_DISABLED);
    aRec->mEffectiveTRRMode = nsIRequest::TRR_DISABLED_MODE;
    return;
  }

  if ((requestMode == nsIRequest::TRR_DEFAULT_MODE &&
       resolverMode == nsIDNSService::MODE_NATIVEONLY)) {
    aRec->RecordReason(TRRSkippedReason::TRR_MODE_NOT_ENABLED);
    aRec->mEffectiveTRRMode = nsIRequest::TRR_DISABLED_MODE;
    return;
  }

  if (requestMode == nsIRequest::TRR_DEFAULT_MODE &&
      resolverMode == nsIDNSService::MODE_TRRFIRST) {
    aRec->mEffectiveTRRMode = nsIRequest::TRR_FIRST_MODE;
    return;
  }

  if (requestMode == nsIRequest::TRR_DEFAULT_MODE &&
      resolverMode == nsIDNSService::MODE_TRRONLY) {
    aRec->mEffectiveTRRMode = nsIRequest::TRR_ONLY_MODE;
    return;
  }

  aRec->mEffectiveTRRMode = requestMode;
}

// Kick-off a name resolve operation, using native resolver and/or TRR
nsresult nsHostResolver::NameLookup(nsHostRecord* rec) {
  LOG(("NameLookup host:%s af:%" PRId16, rec->host.get(), rec->af));
  mQueue.mLock.AssertCurrentThreadOwns();

  if (rec->flags & nsIDNSService::RESOLVE_IP_HINT) {
    LOG(("Skip lookup if nsIDNSService::RESOLVE_IP_HINT is set\n"));
    return NS_ERROR_UNKNOWN_HOST;
  }

  nsresult rv = NS_ERROR_UNKNOWN_HOST;
  if (rec->mResolving) {
    LOG(("NameLookup %s while already resolving\n", rec->host.get()));
    return NS_OK;
  }

  // Make sure we reset the reason each time we attempt to do a new lookup
  // so we don't wrongly report the reason for the previous one.
  rec->Reset();

  ComputeEffectiveTRRMode(rec);

  if (!rec->mTrrServer.IsEmpty()) {
    LOG(("NameLookup: %s use trr:%s", rec->host.get(), rec->mTrrServer.get()));
    if (rec->mEffectiveTRRMode != nsIRequest::TRR_ONLY_MODE) {
      return NS_ERROR_UNKNOWN_HOST;
    }

    if (rec->flags & nsIDNSService::RESOLVE_DISABLE_TRR) {
      LOG(("TRR with server and DISABLE_TRR flag. Returning error."));
      return NS_ERROR_UNKNOWN_HOST;
    }
    return TrrLookup(rec);
  }

  LOG(("NameLookup: %s effectiveTRRmode: %d flags: %X", rec->host.get(),
       static_cast<nsIRequest::TRRMode>(rec->mEffectiveTRRMode),
       static_cast<uint32_t>(rec->flags)));

  if (rec->flags & nsIDNSService::RESOLVE_DISABLE_TRR) {
    rec->RecordReason(TRRSkippedReason::TRR_DISABLED_FLAG);
  }

  bool serviceNotReady = !TRRServiceEnabledForRecord(rec);

  if (rec->mEffectiveTRRMode != nsIRequest::TRR_DISABLED_MODE &&
      !((rec->flags & nsIDNSService::RESOLVE_DISABLE_TRR)) &&
      !serviceNotReady) {
    rv = TrrLookup(rec);
  }

  if (rec->mEffectiveTRRMode == nsIRequest::TRR_DISABLED_MODE ||
      (rec->mEffectiveTRRMode == nsIRequest::TRR_FIRST_MODE &&
       (rec->flags & nsIDNSService::RESOLVE_DISABLE_TRR || serviceNotReady ||
        NS_FAILED(rv)))) {
    if (!rec->IsAddrRecord()) {
      if (!IsNativeHTTPSEnabled()) {
        return NS_ERROR_UNKNOWN_HOST;
      }

      if (rec->flags & nsIDNSService::RESOLVE_DISABLE_NATIVE_HTTPS_QUERY) {
        return NS_ERROR_UNKNOWN_HOST;
      }
    }

#ifdef DEBUG
    // If we use this branch then the mTRRUsed flag should not be set
    // Even if we did call TrrLookup above, the fact that it failed sync-ly
    // means that we didn't actually succeed in opening the channel.
    RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
    MOZ_ASSERT_IF(addrRec, addrRec->mResolverType == DNSResolverType::Native);
#endif

    rv = NativeLookup(rec);
  }

  return rv;
}

nsresult nsHostResolver::ConditionallyRefreshRecord(nsHostRecord* rec,
                                                    const nsACString& host) {
  const bool refreshNegative =
      rec->negative && StaticPrefs::network_dns_refresh_negative_addr_on_use();
  if ((rec->CheckExpiration(TimeStamp::NowLoRes()) == nsHostRecord::EXP_GRACE ||
       refreshNegative) &&
      !rec->mResolving && rec->RefreshForNegativeResponse()) {
    LOG(("  Using %s cache entry for host [%s] but starting async renewal.",
         rec->negative ? "negative" : "positive",
         PromiseFlatCString(host).get()));
    NameLookup(rec);

    if (rec->IsAddrRecord()) {
      if (!rec->negative) {
        glean::dns::lookup_method.AccumulateSingleSample(METHOD_RENEWAL);
      } else {
        glean::dns::lookup_method.AccumulateSingleSample(METHOD_NEGATIVE_HIT);
      }
    }
  } else if (rec->IsAddrRecord()) {
    // it could be that the record is negative, but we haven't entered the above
    // if condition due to the second expression being false. In that case we
    // need to record the telemetry for the negative record here.
    if (!rec->negative) {
      glean::dns::lookup_method.AccumulateSingleSample(METHOD_HIT);
    } else {
      glean::dns::lookup_method.AccumulateSingleSample(METHOD_NEGATIVE_HIT);
    }
  }

  return NS_OK;
}

void nsHostResolver::PrepareRecordExpirationAddrRecord(
    AddrHostRecord* rec) const {
  MOZ_ASSERT(((bool)rec->addr_info) != rec->negative);
  mQueue.mLock.AssertCurrentThreadOwns();
  if (!rec->addr_info) {
    rec->SetExpiration(TimeStamp::NowLoRes(), NEGATIVE_RECORD_LIFETIME, 0);
    LOG(("Caching host [%s] negative record for %u seconds.\n", rec->host.get(),
         NEGATIVE_RECORD_LIFETIME));
    return;
  }

  unsigned int lifetime = StaticPrefs::network_dnsCacheExpiration();
  unsigned int grace = StaticPrefs::network_dnsCacheExpirationGracePeriod();

  if (rec->addr_info && rec->addr_info->TTL() != AddrInfo::NO_TTL_DATA) {
    lifetime = rec->addr_info->TTL();
  }

  rec->SetExpiration(TimeStamp::NowLoRes(), lifetime, grace);
  LOG(("Caching host [%s] record for %u seconds (grace %d).", rec->host.get(),
       lifetime, grace));
}

static bool different_rrset(AddrInfo* rrset1, AddrInfo* rrset2) {
  if (!rrset1 || !rrset2) {
    return true;
  }

  LOG(("different_rrset %s\n", rrset1->Hostname().get()));

  if (rrset1->ResolverType() != rrset2->ResolverType()) {
    return true;
  }

  if (rrset1->TRRType() != rrset2->TRRType()) {
    return true;
  }

  if (rrset1->Addresses().Length() != rrset2->Addresses().Length()) {
    LOG(("different_rrset true due to length change\n"));
    return true;
  }

  nsTArray<NetAddr> orderedSet1 = rrset1->Addresses().Clone();
  nsTArray<NetAddr> orderedSet2 = rrset2->Addresses().Clone();
  orderedSet1.Sort();
  orderedSet2.Sort();

  bool eq = orderedSet1 == orderedSet2;
  if (!eq) {
    LOG(("different_rrset true due to content change\n"));
  } else {
    LOG(("different_rrset false\n"));
  }
  return !eq;
}

void nsHostResolver::AddToEvictionQ(nsHostRecord* rec) {
  mQueue.AddToEvictionQ(rec, StaticPrefs::network_dnsCacheEntries(), mRecordDB);
}

// After a first lookup attempt with TRR in mode 2, we may:
// - If network.trr.retry_on_recoverable_errors is false, retry with native.
// - If network.trr.retry_on_recoverable_errors is true:
//   - Retry with native if the first attempt failed because we got NXDOMAIN, an
//     unreachable address (TRR_DISABLED_FLAG), or we skipped TRR because
//     Confirmation failed.
//   - Trigger a "RetryTRR" Confirmation which will start a fresh
//     connection for TRR, and then retry the lookup with TRR.
//   - If the second attempt failed, fallback to native if
//     network.trr.strict_native_fallback is false.
// Returns true if we retried with either TRR or Native.
bool nsHostResolver::MaybeRetryTRRLookup(
    AddrHostRecord* aAddrRec, nsresult aFirstAttemptStatus,
    TRRSkippedReason aFirstAttemptSkipReason, nsresult aChannelStatus) {
  if (NS_FAILED(aFirstAttemptStatus) &&
      (aChannelStatus == NS_ERROR_PROXY_UNAUTHORIZED ||
       aChannelStatus == NS_ERROR_PROXY_AUTHENTICATION_FAILED) &&
      aAddrRec->mEffectiveTRRMode == nsIRequest::TRR_ONLY_MODE) {
    LOG(("MaybeRetryTRRLookup retry because of proxy connect failed"));
    TRRService::Get()->DontUseTRRThread();
    return DoRetryTRR(aAddrRec);
  }

  if (NS_SUCCEEDED(aFirstAttemptStatus) ||
      aAddrRec->mEffectiveTRRMode != nsIRequest::TRR_FIRST_MODE ||
      aFirstAttemptStatus == NS_ERROR_DEFINITIVE_UNKNOWN_HOST) {
    return false;
  }

  MOZ_ASSERT(!aAddrRec->mResolving);
  if (!StaticPrefs::network_trr_retry_on_recoverable_errors()) {
    LOG(("nsHostResolver::MaybeRetryTRRLookup retrying with native"));

    // Trigger a confirmation retry, in order to cycle connection if needed
    TRRService::Get()->RetryTRRConfirm();
    return NS_SUCCEEDED(NativeLookup(aAddrRec));
  }

  if (IsFailedConfirmationOrNoConnectivity(aFirstAttemptSkipReason) ||
      IsNonRecoverableTRRSkipReason(aFirstAttemptSkipReason) ||
      IsBlockedTRRRequest(aFirstAttemptSkipReason)) {
    LOG(
        ("nsHostResolver::MaybeRetryTRRLookup retrying with native in strict "
         "mode, skip reason was %d",
         static_cast<uint32_t>(aFirstAttemptSkipReason)));
    return NS_SUCCEEDED(NativeLookup(aAddrRec));
  }

  if (aAddrRec->mTrrAttempts > 1) {
    if (!StaticPrefs::network_trr_strict_native_fallback()) {
      LOG(
          ("nsHostResolver::MaybeRetryTRRLookup retry failed. Using "
           "native."));
      return NS_SUCCEEDED(NativeLookup(aAddrRec));
    }

    if (aFirstAttemptSkipReason == TRRSkippedReason::TRR_TIMEOUT &&
        StaticPrefs::network_trr_strict_native_fallback_allow_timeouts()) {
      LOG(
          ("nsHostResolver::MaybeRetryTRRLookup retry timed out. Using "
           "native."));
      return NS_SUCCEEDED(NativeLookup(aAddrRec));
    }
    LOG(("nsHostResolver::MaybeRetryTRRLookup mTrrAttempts>1, not retrying."));
    return false;
  }

  LOG(
      ("nsHostResolver::MaybeRetryTRRLookup triggering Confirmation and "
       "retrying with TRR, skip reason was %d",
       static_cast<uint32_t>(aFirstAttemptSkipReason)));
  TRRService::Get()->RetryTRRConfirm();

  return DoRetryTRR(aAddrRec);
}

bool nsHostResolver::DoRetryTRR(AddrHostRecord* aAddrRec) {
  {
    // Clear out the old query
    auto trrQuery = aAddrRec->mTRRQuery.Lock();
    trrQuery.ref() = nullptr;
  }

  if (NS_SUCCEEDED(TrrLookup(aAddrRec, nullptr /* pushedTRR */))) {
    aAddrRec->NotifyRetryingTrr();
    return true;
  }

  return false;
}

//
// CompleteLookup() checks if the resolving should be redone and if so it
// returns LOOKUP_RESOLVEAGAIN, but only if 'status' is not NS_ERROR_ABORT.
nsHostResolver::LookupStatus nsHostResolver::CompleteLookup(
    nsHostRecord* rec, nsresult status, AddrInfo* aNewRRSet, bool pb,
    const nsACString& aOriginsuffix, TRRSkippedReason aReason,
    mozilla::net::TRR* aTRRRequest) {
  CallbackArray callbacks;
  LookupStatus result;
  {
    MutexAutoLock dbLock(mDBLock);
    MutexAutoLock queueLock(mQueue.mLock);
    result = CompleteLookupLocked(rec, status, aNewRRSet, pb, aOriginsuffix,
                                  aReason, aTRRRequest, callbacks);
  }
  FireCallbacks(callbacks, rec, status);
  return result;
}

nsHostResolver::LookupStatus nsHostResolver::CompleteLookupLocked(
    nsHostRecord* rec, nsresult& status, AddrInfo* aNewRRSet, bool pb,
    const nsACString& aOriginsuffix, TRRSkippedReason aReason,
    mozilla::net::TRR* aTRRRequest, CallbackArray& aCallbacks) {
  MOZ_ASSERT(rec);
  MOZ_ASSERT(rec->pb == pb);
  MOZ_ASSERT(rec->IsAddrRecord());

  rec->mFromStaleCache = false;

  RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
  MOZ_ASSERT(addrRec);

  RefPtr<AddrInfo> newRRSet(aNewRRSet);
  MOZ_ASSERT(NS_FAILED(status) || newRRSet->Addresses().Length() > 0);

  DNSResolverType type =
      newRRSet ? newRRSet->ResolverType() : DNSResolverType::Native;

  if (NS_FAILED(status)) {
    newRRSet = nullptr;
  }

  if (addrRec->LoadResolveAgain() && (status != NS_ERROR_ABORT) &&
      type == DNSResolverType::Native) {
    LOG(("nsHostResolver record %p resolve again due to flushcache\n",
         addrRec.get()));
    addrRec->StoreResolveAgain(false);
    return LOOKUP_RESOLVEAGAIN;
  }

  MOZ_ASSERT(addrRec->mResolving);
  addrRec->mResolving--;
  LOG((
      "nsHostResolver::CompleteLookup %s %p %X resolver=%d stillResolving=%d\n",
      addrRec->host.get(), aNewRRSet, (unsigned int)status, (int)type,
      int(addrRec->mResolving)));

  if (type != DNSResolverType::Native) {
    if (NS_FAILED(status) && status != NS_ERROR_UNKNOWN_HOST &&
        status != NS_ERROR_DEFINITIVE_UNKNOWN_HOST) {
      // the errors are not failed resolves, that means
      // something else failed, consider this as *TRR not used*
      // for actually trying to resolve the host
      addrRec->mResolverType = DNSResolverType::Native;
    }

    if (NS_FAILED(status)) {
      if (aReason != TRRSkippedReason::TRR_UNSET) {
        addrRec->RecordReason(aReason);
      } else {
        // Unknown failed reason.
        addrRec->RecordReason(TRRSkippedReason::TRR_FAILED);
      }
    } else {
      addrRec->mTRRSuccess = true;
      addrRec->RecordReason(TRRSkippedReason::TRR_OK);
    }

    nsresult channelStatus = aTRRRequest->ChannelStatus();
    if (MaybeRetryTRRLookup(addrRec, status, aReason, channelStatus)) {
      MOZ_ASSERT(addrRec->mResolving);
      return LOOKUP_OK;
    }

    if (!addrRec->mTRRSuccess) {
      // no TRR success
      newRRSet = nullptr;
    }

    if (NS_FAILED(status)) {
      // This is the error that consumers expect.
      status = NS_ERROR_UNKNOWN_HOST;
    }
  } else {  // native resolve completed
    if (addrRec->LoadUsingAnyThread()) {
      mActiveAnyThreadCount--;
      addrRec->StoreUsingAnyThread(false);
      // An any-priority slot freed up. Dispatch a new task to pick up
      // blocked med/low work rather than looping in the current task,
      // so the pool manages thread scheduling.
      MaybeDispatchResolveHostTask();
    }

    addrRec->mNativeSuccess = static_cast<bool>(newRRSet);
    if (addrRec->mNativeSuccess) {
      addrRec->mNativeDuration = TimeStamp::Now() - addrRec->mNativeStart;
    }
  }

  addrRec->OnCompleteLookup();

  // update record fields.  We might have a addrRec->addr_info already if a
  // previous lookup result expired and we're reresolving it or we get
  // a late second TRR response.
  if (!mShutdown) {
    // Use a raw pointer so MOZ_REQUIRES(rec->addr_info_lock) on
    // PrepareRecordExpirationAddrRecord can be verified by the analyzer.
    auto* rawPtr = addrRec.get();
    MutexAutoLock lock(rawPtr->addr_info_lock);
    RefPtr<AddrInfo> old_addr_info;
    bool isDifferentRRSet = different_rrset(rawPtr->addr_info, newRRSet);
    if (isDifferentRRSet) {
      LOG(("nsHostResolver record %p new gencnt\n", rawPtr));
      old_addr_info = rawPtr->addr_info;
      rawPtr->addr_info = std::move(newRRSet);
      rawPtr->addr_info_gencnt++;
      rawPtr->mLastUpdate = TimeStamp::NowLoRes();
    } else {
      if (rawPtr->addr_info && newRRSet) {
        auto builder = rawPtr->addr_info->Build();
        builder.SetTTL(newRRSet->TTL());
        // Update trr timings
        builder.SetTrrFetchDuration(newRRSet->GetTrrFetchDuration());
        builder.SetTrrFetchDurationNetworkOnly(
            newRRSet->GetTrrFetchDurationNetworkOnly());

        rawPtr->addr_info = builder.Finish();
        rawPtr->addr_info_gencnt++;
      }
      old_addr_info = std::move(newRRSet);
    }
    rawPtr->negative = !rawPtr->addr_info;

    if (rawPtr->addr_info && StaticPrefs::network_dns_preferIPv6() &&
        rawPtr->addr_info->Addresses().Length() > 1 &&
        rawPtr->addr_info->Addresses()[0].IsIPAddrV4()) {
      // Sort IPv6 addresses first.
      auto builder = rawPtr->addr_info->Build();
      builder.SortAddresses(NetAddrIPv6FirstComparator());
      rawPtr->addr_info = builder.Finish();
      rawPtr->addr_info_gencnt++;
    }

    PrepareRecordExpirationAddrRecord(rawPtr);
  }

  if (LOG_ENABLED()) {
    MutexAutoLock lock(addrRec->addr_info_lock);
    if (addrRec->addr_info) {
      for (const auto& elem : addrRec->addr_info->Addresses()) {
        LOG(("CompleteLookup: %s has %s\n", addrRec->host.get(),
             elem.ToString().get()));
      }
    } else {
      LOG(("CompleteLookup: %s has NO address\n", addrRec->host.get()));
    }
  }

  LOG(("nsHostResolver record %p calling back dns users status:%X\n",
       addrRec.get(), int(status)));

  DrainCallbacks(rec->mCallbacks, aCallbacks);

  OnResolveComplete(rec);

#ifdef DNSQUERY_AVAILABLE
  // Unless the result is from TRR, resolve again to get TTL
  bool hasNativeResult = false;
  {
    MutexAutoLock lock(addrRec->addr_info_lock);
    if (addrRec->addr_info && !addrRec->addr_info->IsTRR()) {
      hasNativeResult = true;
    }
  }
  if (hasNativeResult && !mShutdown && !addrRec->LoadGetTtl() &&
      !rec->mResolving && StaticPrefs::network_dns_get_ttl()) {
    LOG(("Issuing second async lookup for TTL for host [%s].",
         addrRec->host.get()));
    addrRec->flags =
        (addrRec->flags & ~nsIDNSService::RESOLVE_PRIORITY_MEDIUM) |
        nsIDNSService::RESOLVE_PRIORITY_LOW;
    DebugOnly<nsresult> rv = NativeLookup(rec);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "Could not issue second async lookup for TTL.");
  }
#endif
  return LOOKUP_OK;
}

nsHostResolver::LookupStatus nsHostResolver::CompleteLookupByType(
    nsHostRecord* rec, nsresult status,
    mozilla::net::TypeRecordResultType& aResult, TRRSkippedReason aReason,
    uint32_t aTtl, bool pb) {
  CallbackArray callbacks;
  LookupStatus result;
  {
    MutexAutoLock dbLock(mDBLock);
    MutexAutoLock queueLock(mQueue.mLock);
    result = CompleteLookupByTypeLocked(rec, status, aResult, aReason, aTtl, pb,
                                        callbacks);
  }
  FireCallbacks(callbacks, rec, status);
  return result;
}

nsHostResolver::LookupStatus nsHostResolver::CompleteLookupByTypeLocked(
    nsHostRecord* rec, nsresult& status,
    mozilla::net::TypeRecordResultType& aResult, TRRSkippedReason aReason,
    uint32_t aTtl, bool pb, CallbackArray& aCallbacks) {
  MOZ_ASSERT(rec);
  MOZ_ASSERT(rec->pb == pb);
  MOZ_ASSERT(!rec->IsAddrRecord());

  rec->mFromStaleCache = false;

  if (rec->LoadNative()) {
    // If this was resolved using the native resolver
    // we also need to update the global count.
    if (rec->LoadUsingAnyThread()) {
      mActiveAnyThreadCount--;
      rec->StoreUsingAnyThread(false);
      // See comment in CompleteLookupLocked.
      MaybeDispatchResolveHostTask();
    }
  }

  RefPtr<TypeHostRecord> typeRec = do_QueryObject(rec);
  MOZ_ASSERT(typeRec);

  MOZ_ASSERT(typeRec->mResolving);
  typeRec->mResolving--;

  if (NS_FAILED(status)) {
    if (status != NS_ERROR_UNKNOWN_HOST &&
        status != NS_ERROR_DEFINITIVE_UNKNOWN_HOST) {
      // See nsHostResolver::CompleteLookupLocked. Use the same logic as
      // AddrRecord here.
      typeRec->mResolverType = DNSResolverType::Native;
    }
    LOG(("nsHostResolver::CompleteLookupByType record %p [%s] status %x\n",
         typeRec.get(), typeRec->host.get(), (unsigned int)status));
    typeRec->SetExpiration(
        TimeStamp::NowLoRes(),
        StaticPrefs::network_dns_negative_ttl_for_type_record(), 0);
    MOZ_ASSERT(aResult.is<TypeRecordEmpty>());
    status = NS_ERROR_UNKNOWN_HOST;
    typeRec->negative = true;
    if (aReason != TRRSkippedReason::TRR_UNSET) {
      typeRec->RecordReason(aReason);
    } else {
      // Unknown failed reason.
      typeRec->RecordReason(TRRSkippedReason::TRR_FAILED);
    }
  } else {
    size_t recordCount = 0;
    if (aResult.is<TypeRecordTxt>()) {
      recordCount = aResult.as<TypeRecordTxt>().Length();
    } else if (aResult.is<TypeRecordHTTPSSVC>()) {
      recordCount = aResult.as<TypeRecordHTTPSSVC>().Length();
    }
    LOG(
        ("nsHostResolver::CompleteLookupByType record %p [%s], number of "
         "records %zu\n",
         typeRec.get(), typeRec->host.get(), recordCount));
    MutexAutoLock typeLock(typeRec->mResultsLock);
    typeRec->mResults = aResult;
    typeRec->SetExpiration(
        TimeStamp::NowLoRes(), aTtl,
        StaticPrefs::network_dnsCacheExpirationGracePeriod());
    typeRec->negative = false;
    typeRec->mTRRSuccess = !rec->LoadNative();
    typeRec->mNativeSuccess = rec->LoadNative();
    MOZ_ASSERT(aReason != TRRSkippedReason::TRR_UNSET);
    typeRec->RecordReason(aReason);
  }

  LOG(("nsHostResolver::CompleteLookupByType record %p collecting callbacks\n",
       typeRec.get()));

  DrainCallbacks(typeRec->mCallbacks, aCallbacks);

  OnResolveComplete(rec);

  return LOOKUP_OK;
}

void nsHostResolver::OnResolveComplete(nsHostRecord* aRec) {
  if (!aRec->mResolving && !mShutdown) {
    {
      auto trrQuery = aRec->mTRRQuery.Lock();
      if (trrQuery.ref()) {
        aRec->mTrrDuration = trrQuery.ref()->Duration();
      }
      trrQuery.ref() = nullptr;
    }
    aRec->ResolveComplete();

    AddToEvictionQ(aRec);
  }
}

void nsHostResolver::CancelAsyncRequest(
    const nsACString& host, const nsACString& aTrrServer, uint16_t aType,
    const OriginAttributes& aOriginAttributes, nsIDNSService::DNSFlags flags,
    uint16_t af, nsIDNSListener* aListener, nsresult status)

{
  CallbackArray callbacks;
  RefPtr<nsHostRecord> rec;

  {
    MutexAutoLock dbLock(mDBLock);
    MutexAutoLock queueLock(mQueue.mLock);

    nsAutoCString originSuffix;
    aOriginAttributes.CreateSuffix(originSuffix);

    // Lookup the host record associated with host, flags & address family

    nsHostKey key(host, aTrrServer, aType, flags, af,
                  (aOriginAttributes.IsPrivateBrowsing()), originSuffix);
    rec = mRecordDB.Get(key);
    if (rec) {
      for (RefPtr<nsResolveHostCallback> c : rec->mCallbacks) {
        if (c->EqualsAsyncListener(aListener)) {
          c->remove();
          callbacks.AppendElement(std::move(c));
          break;
        }
      }

      // If there are no more callbacks, remove the hash table entry
      if (rec->mCallbacks.isEmpty()) {
        mRecordDB.Remove(*static_cast<nsHostKey*>(rec.get()));
        // If record is on a Queue, remove it
        mQueue.MaybeRemoveFromQ(rec);
      }
    }
  }

  FireCallbacks(callbacks, rec, status);
}

size_t nsHostResolver::SizeOfIncludingThis(MallocSizeOf mallocSizeOf) const {
  MutexAutoLock dbLock(mDBLock);

  size_t n = mallocSizeOf(this);

  n += mRecordDB.ShallowSizeOfExcludingThis(mallocSizeOf);
  for (const auto& entry : mRecordDB.Values()) {
    n += entry->SizeOfIncludingThis(mallocSizeOf);
  }

  // The following fields aren't measured.
  // - mHighQ, mMediumQ, mLowQ, mEvictionQ, because they just point to
  //   nsHostRecords that also pointed to by entries |mRecordDB|, and
  //   measured when |mRecordDB| is measured.

  return n;
}

void nsHostResolver::ResolveHostTask() {
  RefPtr<nsHostRecord> rec;
  {
    MutexAutoLock queueLock(mQueue.mLock);
    if (mShutdown) {
      return;
    }
    rec = DequeueNextRecord();
  }
  if (!rec) {
    return;
  }

  RefPtr<AddrInfo> ai;

  // Loop only for LOOKUP_RESOLVEAGAIN (same host needs re-resolution).
  do {
    LOG1(("DNS resolve task - Calling getaddrinfo for host [%s].\n",
          rec->host.get()));

    TimeStamp startTime = TimeStamp::Now();
    bool getTtl = rec->LoadGetTtl();
    TimeDuration inQueue = startTime - rec->mNativeStart;
    glean::dns::native_queuing.AccumulateRawDuration(inQueue);

    if (!rec->IsAddrRecord()) {
      LOG(("byType on DNS thread"));
      TypeRecordResultType result = AsVariant(mozilla::Nothing());
      uint32_t ttl = UINT32_MAX;
      nsresult status = ResolveHTTPSRecord(rec->host, rec->flags, result, ttl);
      mozilla::glean::networking::dns_native_count
          .EnumGet(rec->pb
                       ? glean::networking::DnsNativeCountLabel::eHttpsPrivate
                       : glean::networking::DnsNativeCountLabel::eHttpsRegular)
          .Add(1);
      // Capture native success/duration; ResolveComplete records it as the
      // HTTPS RR lookup time (keyed "native").
      rec->mNativeSuccess = NS_SUCCEEDED(status);
      rec->mNativeDuration = TimeStamp::Now() - startTime;
      CompleteLookupByType(rec, status, result, rec->mTRRSkippedReason, ttl,
                           rec->pb);
      return;
    }

    nsresult status =
        GetAddrInfo(rec->host, rec->af, rec->flags, getter_AddRefs(ai), getTtl);

    mozilla::glean::networking::dns_native_count
        .EnumGet(rec->pb ? glean::networking::DnsNativeCountLabel::ePrivate
                         : glean::networking::DnsNativeCountLabel::eRegular)
        .Add(1);

    if (RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec)) {
      if (!mShutdown) {
        TimeDuration elapsed = TimeStamp::Now() - startTime;
        int gencnt;
        {
          MutexAutoLock lock(addrRec->addr_info_lock);
          gencnt = addrRec->addr_info_gencnt;
        }
        if (NS_SUCCEEDED(status)) {
          if (!gencnt) {
            glean::networking::dns_lookup_time.AccumulateRawDuration(elapsed);
          } else if (!getTtl) {
            glean::networking::dns_renewal_time.AccumulateRawDuration(elapsed);
          } else {
            glean::networking::dns_renewal_time_for_ttl.AccumulateRawDuration(
                elapsed);
          }
        } else {
          glean::networking::dns_failed_lookup_time.AccumulateRawDuration(
              elapsed);
        }
      }
    }

    LOG1(("DNS resolve task - lookup completed for host [%s]: %s.\n",
          rec->host.get(), ai ? "success" : "failure: unknown host"));

    if (LOOKUP_RESOLVEAGAIN ==
        CompleteLookup(rec, status, ai, rec->pb, rec->originSuffix,
                       rec->mTRRSkippedReason, nullptr)) {
      LOG(("DNS resolve task - Re-resolving host [%s].\n", rec->host.get()));
    } else {
      rec = nullptr;
    }
  } while (rec);
}

nsresult nsHostResolver::Create(nsHostResolver** result) {
  RefPtr<nsHostResolver> res = new nsHostResolver();

  nsresult rv = res->Init();
  if (NS_FAILED(rv)) {
    return rv;
  }

  res.forget(result);
  return NS_OK;
}

void nsHostResolver::GetDNSCacheEntries(nsTArray<DNSCacheEntries>* args) {
  MutexAutoLock dbLock(mDBLock);
  for (const auto& recordEntry : mRecordDB) {
    // We don't pay attention to address literals, only resolved domains.
    // Also require a host.
    nsHostRecord* rec = recordEntry.GetWeak();
    MOZ_ASSERT(rec, "rec should never be null here!");

    if (!rec) {
      continue;
    }

    DNSCacheEntries info;
    info.resolveType = rec->type;
    info.hostname = rec->host;
    info.family = rec->af;
    if (rec->mValidEnd.IsNull()) {
      continue;
    }
    info.expiration =
        (int64_t)(rec->mValidEnd - TimeStamp::NowLoRes()).ToSeconds();
    if (info.expiration <= 0) {
      // We only need valid DNS cache entries
      continue;
    }

    info.originAttributesSuffix = recordEntry.GetKey().originSuffix;
    info.flags = nsPrintfCString("%u|0x%x|%u|%d|%s", rec->type,
                                 static_cast<uint32_t>(rec->flags), rec->af,
                                 rec->pb, rec->mTrrServer.get());

    RefPtr<AddrHostRecord> addrRec = do_QueryObject(rec);
    if (addrRec) {
      MutexAutoLock lock(addrRec->addr_info_lock);
      if (addrRec->addr_info) {
        for (const auto& addr : addrRec->addr_info->Addresses()) {
          nsCString addrStr;
          if (addr.ToString(addrStr)) {
            info.hostaddr.AppendElement(std::move(addrStr));
          }
        }
        info.TRR = addrRec->addr_info->IsTRR();
      }
    }

    args->AppendElement(std::move(info));
  }
}

#undef LOG
#undef LOG_ENABLED
