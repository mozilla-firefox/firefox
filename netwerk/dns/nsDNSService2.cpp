/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDNSService2.h"

#include "DNSAdditionalInfo.h"
#include "TRRService.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/net/ChildDNSService.h"
#include "mozilla/net/DNSListenerProxy.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsCRT.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsDNSPrefetch.h"
#include "nsError.h"
#include "nsICancelable.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIDNSListener.h"
#include "nsIDNSRecord.h"
#include "nsINetworkLinkService.h"
#include "nsIObliviousHttp.h"
#include "nsIObserverService.h"
#include "nsIPrefBranch.h"
#include "nsIProtocolProxyService.h"
#include "nsIXPConnect.h"
#include "nsNetAddr.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "prio.h"
#include "prmon.h"
#include "prnetdb.h"
#include "prsystem.h"
// Put DNSLogging.h at the end to avoid LOG being overwritten by other headers.
#include "DNSLogging.h"

using namespace mozilla;
using namespace mozilla::net;

static const char kPrefDnsCacheEntries[] = "network.dnsCacheEntries";
static const char kPrefDnsCacheExpiration[] = "network.dnsCacheExpiration";
static const char kPrefDnsCacheGrace[] =
    "network.dnsCacheExpirationGracePeriod";
static const char kPrefIPv4OnlyDomains[] = "network.dns.ipv4OnlyDomains";
static const char kPrefDnsLocalDomains[] = "network.dns.localDomains";
static const char kPrefDnsForceResolve[] = "network.dns.forceResolve";
static const char kPrefDnsNotifyResolution[] = "network.dns.notifyResolution";
static const char kPrefDnsMockHTTPSRRDomain[] =
    "network.dns.mock_HTTPS_RR_domain";

//-----------------------------------------------------------------------------

class nsDNSRecord : public nsIDNSAddrRecord {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDNSRECORD
  NS_DECL_NSIDNSADDRRECORD

  explicit nsDNSRecord(nsHostRecord* hostRecord, bool aFromStaleCache = false)
      : mFromStaleCache(aFromStaleCache) {
    mHostRecord = do_QueryObject(hostRecord);
  }

 private:
  virtual ~nsDNSRecord() = default;

  RefPtr<AddrHostRecord> mHostRecord;
  // Whether the answer this record represents was served from a stale
  // (grace-period) cache entry. Snapshotted at delivery time.
  const bool mFromStaleCache;
  // Since mIter is holding a weak reference to the NetAddr array we must
  // make sure it is not released. So we also keep a RefPtr to the AddrInfo
  // which is immutable.
  RefPtr<AddrInfo> mAddrInfo;
  nsTArray<NetAddr>::const_iterator mIter;
  const NetAddr* iter() {
    if (!mIter.GetArray()) {
      return nullptr;
    }
    if (mIter.GetArray()->end() == mIter) {
      return nullptr;
    }
    return &*mIter;
  }

  int mIterGenCnt = -1;  // the generation count of
                         // mHostRecord->addr_info when we
                         // start iterating
  bool mDone = false;
};

NS_IMPL_ISUPPORTS(nsDNSRecord, nsIDNSRecord, nsIDNSAddrRecord)

NS_IMETHODIMP
nsDNSRecord::GetCanonicalName(nsACString& result) {
  // this method should only be called if we have a CNAME
  NS_ENSURE_TRUE(mHostRecord->flags & nsIDNSService::RESOLVE_CANONICAL_NAME,
                 NS_ERROR_NOT_AVAILABLE);

  MutexAutoLock lock(mHostRecord->addr_info_lock);

  // if the record is for an IP address literal, then the canonical
  // host name is the IP address literal.
  if (!mHostRecord->addr_info) {
    result = mHostRecord->host;
    return NS_OK;
  }

  if (mHostRecord->addr_info->CanonicalHostname().IsEmpty()) {
    result = mHostRecord->addr_info->Hostname();
  } else {
    result = mHostRecord->addr_info->CanonicalHostname();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::IsTRR(bool* retval) {
  MutexAutoLock lock(mHostRecord->addr_info_lock);
  if (mHostRecord->addr_info) {
    *retval = mHostRecord->addr_info->IsTRR();
  } else {
    *retval = false;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetTrrFetchDuration(double* aTime) {
  MutexAutoLock lock(mHostRecord->addr_info_lock);
  if (mHostRecord->addr_info && mHostRecord->addr_info->IsTRR()) {
    *aTime = mHostRecord->addr_info->GetTrrFetchDuration();
  } else {
    *aTime = 0;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetTrrFetchDurationNetworkOnly(double* aTime) {
  MutexAutoLock lock(mHostRecord->addr_info_lock);
  if (mHostRecord->addr_info && mHostRecord->addr_info->IsTRR()) {
    *aTime = mHostRecord->addr_info->GetTrrFetchDurationNetworkOnly();
  } else {
    *aTime = 0;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetNextAddr(uint16_t port, NetAddr* addr) {
  if (mDone) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mHostRecord->addr_info_lock.Lock();
  if (mHostRecord->addr_info) {
    if (mIterGenCnt != mHostRecord->addr_info_gencnt) {
      // mHostRecord->addr_info has changed, restart the iteration.
      mIter = nsTArray<NetAddr>::const_iterator();
      mIterGenCnt = mHostRecord->addr_info_gencnt;
      // Make sure to hold a RefPtr to the AddrInfo so we can iterate through
      // the NetAddr array.
      mAddrInfo = mHostRecord->addr_info;
    }

    bool startedFresh = !iter();

    do {
      if (!iter()) {
        mIter = mAddrInfo->Addresses().begin();
      } else {
        mIter++;
      }
    } while (iter() && mHostRecord->Blocklisted(iter()));

    if (!iter() && startedFresh) {
      // If everything was blocklisted we want to reset the blocklist (and
      // likely relearn it) and return the first address. That is better
      // than nothing.
      mHostRecord->ResetBlocklist();
      mIter = mAddrInfo->Addresses().begin();
    }

    if (iter()) {
      *addr = *mIter;
    }

    mHostRecord->addr_info_lock.Unlock();

    if (!iter()) {
      mDone = true;
      mIter = nsTArray<NetAddr>::const_iterator();
      mAddrInfo = nullptr;
      mIterGenCnt = -1;
      return NS_ERROR_NOT_AVAILABLE;
    }
  } else {
    mHostRecord->addr_info_lock.Unlock();

    if (!mHostRecord->addr) {
      // Both mHostRecord->addr_info and mHostRecord->addr are null.
      // This can happen if mHostRecord->addr_info expired and the
      // attempt to reresolve it failed.
      return NS_ERROR_NOT_AVAILABLE;
    }
    *addr = *mHostRecord->addr;
    mDone = true;
  }

  // set given port
  port = htons(port);
  if (addr->raw.family == AF_INET) {
    addr->inet.port = port;
  } else if (addr->raw.family == AF_INET6) {
    addr->inet6.port = port;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetAddresses(nsTArray<NetAddr>& aAddressArray) {
  if (mDone) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mHostRecord->addr_info_lock.Lock();
  if (mHostRecord->addr_info) {
    for (const auto& address : mHostRecord->addr_info->Addresses()) {
      NetAddr* addr = aAddressArray.AppendElement(address);
      if (addr->raw.family == AF_INET) {
        addr->inet.port = 0;
      } else if (addr->raw.family == AF_INET6) {
        addr->inet6.port = 0;
      }
    }
    mHostRecord->addr_info_lock.Unlock();
  } else {
    mHostRecord->addr_info_lock.Unlock();

    if (!mHostRecord->addr) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    NetAddr* addr = aAddressArray.AppendElement(NetAddr());
    *addr = *mHostRecord->addr;
    if (addr->raw.family == AF_INET) {
      addr->inet.port = 0;
    } else if (addr->raw.family == AF_INET6) {
      addr->inet6.port = 0;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetScriptableNextAddr(uint16_t port, nsINetAddr** result) {
  NetAddr addr;
  nsresult rv = GetNextAddr(port, &addr);
  if (NS_FAILED(rv)) {
    return rv;
  }

  RefPtr<nsNetAddr> netaddr = new nsNetAddr(&addr);
  netaddr.forget(result);

  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetNextAddrAsString(nsACString& result) {
  NetAddr addr;
  nsresult rv = GetNextAddr(0, &addr);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (addr.ToString(result)) {
    return NS_OK;
  }
  NS_ERROR("NetAddrToString failed unexpectedly");
  return NS_ERROR_FAILURE;  // conversion failed for some reason
}

NS_IMETHODIMP
nsDNSRecord::HasMore(bool* result) {
  if (mDone) {
    *result = false;
    return NS_OK;
  }

  nsTArray<NetAddr>::const_iterator iterCopy = mIter;
  int iterGenCntCopy = mIterGenCnt;
  RefPtr<AddrInfo> addrInfoCopy = mAddrInfo;

  NetAddr addr;
  *result = NS_SUCCEEDED(GetNextAddr(0, &addr));

  mIter = iterCopy;
  mIterGenCnt = iterGenCntCopy;
  mAddrInfo = std::move(addrInfoCopy);
  mDone = false;

  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::Rewind() {
  mIter = nsTArray<NetAddr>::const_iterator();
  mIterGenCnt = -1;
  mDone = false;
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::ReportUnusable(uint16_t aPort) {
  // right now we don't use the port in the blocklist

  MutexAutoLock lock(mHostRecord->addr_info_lock);

  // Check that we are using a real addr_info (as opposed to a single
  // constant address), and that the generation count is valid. Otherwise,
  // ignore the report.

  if (mHostRecord->addr_info && mIterGenCnt == mHostRecord->addr_info_gencnt &&
      iter()) {
    mHostRecord->ReportUnusable(iter());
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetEffectiveTRRMode(nsIRequest::TRRMode* aMode) {
  *aMode = mHostRecord->EffectiveTRRMode();
  return NS_OK;
}

NS_IMETHODIMP nsDNSRecord::GetTrrSkipReason(
    nsITRRSkipReason::value* aTrrSkipReason) {
  *aTrrSkipReason = mHostRecord->TrrSkipReason();
  return NS_OK;
}

NS_IMETHODIMP
nsDNSRecord::GetTtl(uint32_t* aTtl) { return mHostRecord->GetTtl(aTtl); }

NS_IMETHODIMP
nsDNSRecord::GetLastUpdate(mozilla::TimeStamp* aLastUpdate) {
  MutexAutoLock lock(mHostRecord->addr_info_lock);
  return mHostRecord->GetLastUpdate(aLastUpdate);
}

NS_IMETHODIMP
nsDNSRecord::GetFromStaleCache(bool* aResult) {
  NS_ENSURE_ARG(aResult);
  *aResult = mFromStaleCache;
  return NS_OK;
}

class nsDNSByTypeRecord : public nsIDNSByTypeRecord,
                          public nsIDNSTXTRecord,
                          public nsIDNSHTTPSSVCRecord {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDNSRECORD
  NS_DECL_NSIDNSBYTYPERECORD
  NS_DECL_NSIDNSTXTRECORD
  NS_DECL_NSIDNSHTTPSSVCRECORD

  explicit nsDNSByTypeRecord(nsHostRecord* hostRecord,
                             bool aFromStaleCache = false)
      : mFromStaleCache(aFromStaleCache) {
    mHostRecord = do_QueryObject(hostRecord);
  }

 private:
  virtual ~nsDNSByTypeRecord() = default;
  RefPtr<TypeHostRecord> mHostRecord;
  // See nsDNSRecord::mFromStaleCache.
  const bool mFromStaleCache;
};

NS_IMPL_ISUPPORTS(nsDNSByTypeRecord, nsIDNSRecord, nsIDNSByTypeRecord,
                  nsIDNSTXTRecord, nsIDNSHTTPSSVCRecord)

NS_IMETHODIMP
nsDNSByTypeRecord::GetType(uint32_t* aType) {
  *aType = mHostRecord->GetType();
  return NS_OK;
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetRecords(CopyableTArray<nsCString>& aRecords) {
  // deep copy
  return mHostRecord->GetRecords(aRecords);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetRecordsAsOneString(nsACString& aRecords) {
  // deep copy
  return mHostRecord->GetRecordsAsOneString(aRecords);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetRecords(nsTArray<RefPtr<nsISVCBRecord>>& aRecords) {
  return mHostRecord->GetRecords(aRecords);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetServiceModeRecord(bool aNoHttp2, bool aNoHttp3,
                                        nsISVCBRecord** aRecord) {
  return mHostRecord->GetServiceModeRecord(aNoHttp2, aNoHttp3, aRecord);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetServiceModeRecordWithCname(bool aNoHttp2, bool aNoHttp3,
                                                 const nsACString& aCname,
                                                 nsISVCBRecord** aRecord) {
  return mHostRecord->GetServiceModeRecordWithCname(aNoHttp2, aNoHttp3, aCname,
                                                    aRecord);
}

NS_IMETHODIMP
nsDNSByTypeRecord::IsTRR(bool* aResult) { return mHostRecord->IsTRR(aResult); }

NS_IMETHODIMP
nsDNSByTypeRecord::GetAllRecords(bool aNoHttp2, bool aNoHttp3,
                                 const nsACString& aCname,
                                 nsTArray<RefPtr<nsISVCBRecord>>& aResult) {
  return mHostRecord->GetAllRecords(aNoHttp2, aNoHttp3, aCname, aResult);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetAllRecordsWithEchConfig(
    bool aNoHttp2, bool aNoHttp3, const nsACString& aCname,
    bool* aAllRecordsHaveEchConfig, bool* aAllRecordsInH3ExcludedList,
    nsTArray<RefPtr<nsISVCBRecord>>& aResult) {
  return mHostRecord->GetAllRecordsWithEchConfig(
      aNoHttp2, aNoHttp3, aCname, aAllRecordsHaveEchConfig,
      aAllRecordsInH3ExcludedList, aResult);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetHasIPAddresses(bool* aResult) {
  return mHostRecord->GetHasIPAddresses(aResult);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetAllRecordsExcluded(bool* aResult) {
  return mHostRecord->GetAllRecordsExcluded(aResult);
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetResults(mozilla::net::TypeRecordResultType* aResults) {
  *aResults = mHostRecord->GetResults();
  return NS_OK;
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetFromStaleCache(bool* aResult) {
  NS_ENSURE_ARG(aResult);
  *aResult = mFromStaleCache;
  return NS_OK;
}

NS_IMETHODIMP
nsDNSByTypeRecord::GetTtl(uint32_t* aTtl) { return mHostRecord->GetTtl(aTtl); }

//-----------------------------------------------------------------------------

class nsDNSAsyncRequest final : public nsResolveHostCallback,
                                public nsICancelable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICANCELABLE

  nsDNSAsyncRequest(nsHostResolver* res, const nsACString& host,
                    const nsACString& trrServer, uint16_t type,
                    const OriginAttributes& attrs, nsIDNSListener* listener,
                    nsIDNSService::DNSFlags flags, uint16_t af)
      : mResolver(res),
        mHost(host),
        mTrrServer(trrServer),
        mType(type),
        mOriginAttributes(attrs),
        mListener(listener),
        mFlags(flags),
        mAF(af) {}

  void OnResolveHostComplete(nsHostResolver*, nsHostRecord*, nsresult,
                             bool aFromStaleCache) override;
  // Returns TRUE if the DNS listener arg is the same as the member listener
  // Used in Cancellations to remove DNS requests associated with a
  // particular hostname and nsIDNSListener
  bool EqualsAsyncListener(nsIDNSListener* aListener) override;

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf) const override;

  RefPtr<nsHostResolver> mResolver;
  nsCString mHost;       // hostname we're resolving
  nsCString mTrrServer;  // A trr server to be used.
  uint16_t mType = 0;
  const OriginAttributes
      mOriginAttributes;  // The originAttributes for this resolving
  nsCOMPtr<nsIDNSListener> mListener;
  nsIDNSService::DNSFlags mFlags = nsIDNSService::RESOLVE_DEFAULT_FLAGS;
  uint16_t mAF = 0;

 private:
  virtual ~nsDNSAsyncRequest() = default;
};

NS_IMPL_ISUPPORTS(nsDNSAsyncRequest, nsICancelable)

void nsDNSAsyncRequest::OnResolveHostComplete(nsHostResolver* resolver,
                                              nsHostRecord* hostRecord,
                                              nsresult status,
                                              bool aFromStaleCache) {
  // need to have an owning ref when we issue the callback to enable
  // the caller to be able to addref/release multiple times without
  // destroying the record prematurely.
  nsCOMPtr<nsIDNSRecord> rec;
  if (NS_SUCCEEDED(status) ||
      mFlags & nsIDNSService::RESOLVE_WANT_RECORD_ON_ERROR) {
    MOZ_ASSERT(hostRecord, "no host record");
    if (!hostRecord) {
      mListener->OnLookupComplete(this, nullptr, NS_ERROR_UNKNOWN_HOST);
      mListener = nullptr;
      return;
    }
    if (hostRecord->type != nsDNSService::RESOLVE_TYPE_DEFAULT) {
      rec = new nsDNSByTypeRecord(hostRecord, aFromStaleCache);
    } else {
      rec = new nsDNSRecord(hostRecord, aFromStaleCache);
    }
  }

  LOG(("OnResolveHostComplete: %s", mHost.get()));
  mListener->OnLookupComplete(this, rec, status);
  mListener = nullptr;
}

bool nsDNSAsyncRequest::EqualsAsyncListener(nsIDNSListener* aListener) {
  uintptr_t originalListenerAddr = reinterpret_cast<uintptr_t>(mListener.get());
  RefPtr<DNSListenerProxy> wrapper = do_QueryObject(mListener);
  if (wrapper) {
    originalListenerAddr = wrapper->GetOriginalListenerAddress();
  }

  uintptr_t listenerAddr = reinterpret_cast<uintptr_t>(aListener);
  return (listenerAddr == originalListenerAddr);
}

size_t nsDNSAsyncRequest::SizeOfIncludingThis(MallocSizeOf mallocSizeOf) const {
  size_t n = mallocSizeOf(this);

  // The following fields aren't measured.
  // - mHost, because it's a non-owning pointer
  // - mResolver, because it's a non-owning pointer
  // - mListener, because it's a non-owning pointer

  return n;
}

NS_IMETHODIMP
nsDNSAsyncRequest::Cancel(nsresult reason) {
  NS_ENSURE_ARG(NS_FAILED(reason));
  MOZ_DIAGNOSTIC_ASSERT(mResolver, "mResolver should not be null");
  mResolver->DetachCallback(mHost, mTrrServer, mType, mOriginAttributes, mFlags,
                            mAF, this, reason);
  return NS_OK;
}

//-----------------------------------------------------------------------------

class DNSCacheRequest : public nsResolveHostCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  DNSCacheRequest() = default;

  void OnResolveHostComplete(nsHostResolver* resolver, nsHostRecord* hostRecord,
                             nsresult status, bool aFromStaleCache) override {
    mStatus = status;
    mHostRecord = hostRecord;
    mFromStaleCache = aFromStaleCache;
  }

  bool EqualsAsyncListener(nsIDNSListener* aListener) override {
    // Sync request: no listener to compare
    return false;
  }

  size_t SizeOfIncludingThis(
      mozilla::MallocSizeOf mallocSizeOf) const override {
    size_t n = mallocSizeOf(this);

    // The following fields aren't measured.
    // - mHostRecord, because it's a non-owning pointer

    // Measurement of the following members may be added later if DMD finds it
    // is worthwhile:
    // - nsDNSSyncRequest::mMonitor

    return n;
  }

  nsresult mStatus = NS_OK;
  RefPtr<nsHostRecord> mHostRecord;
  bool mFromStaleCache = false;

 protected:
  virtual ~DNSCacheRequest() = default;
};

NS_IMPL_ISUPPORTS0(DNSCacheRequest)

class nsDNSSyncRequest : public DNSCacheRequest {
 public:
  explicit nsDNSSyncRequest(PRMonitor* mon) : mMonitor(mon) {}

  void OnResolveHostComplete(nsHostResolver*, nsHostRecord*, nsresult,
                             bool aFromStaleCache) override;

  bool mDone = false;

 private:
  virtual ~nsDNSSyncRequest() = default;

  PRMonitor* mMonitor = nullptr;
};

void nsDNSSyncRequest::OnResolveHostComplete(nsHostResolver* resolver,
                                             nsHostRecord* hostRecord,
                                             nsresult status,
                                             bool aFromStaleCache) {
  // store results, and wake up nsDNSService::Resolve to process results.
  PR_EnterMonitor(mMonitor);
  mDone = true;
  DNSCacheRequest::OnResolveHostComplete(resolver, hostRecord, status,
                                         aFromStaleCache);
  PR_Notify(mMonitor);
  PR_ExitMonitor(mMonitor);
}

class NotifyDNSResolution : public Runnable {
 public:
  explicit NotifyDNSResolution(const nsACString& aHostname)
      : mozilla::Runnable("NotifyDNSResolution"), mHostname(aHostname) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->NotifyObservers(nullptr, "dns-resolution-request",
                           NS_ConvertUTF8toUTF16(mHostname).get());
    }
    return NS_OK;
  }

 private:
  nsCString mHostname;
};

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
NS_IMPL_ISUPPORTS_INHERITED(nsDNSService, DNSServiceBase, nsIDNSService,
                            nsPIDNSService, nsIMemoryReporter)

/******************************************************************************
 * nsDNSService impl:
 * singleton instance ctor/dtor methods
 ******************************************************************************/
static StaticRefPtr<nsDNSService> gDNSService;
static Atomic<bool> gInited(false);

// Note: be careful of races!  Called from multiple threads
already_AddRefed<nsIDNSService> GetOrInitDNSService() {
  if (gInited) {
    return nsDNSService::GetXPCOMSingleton();
  }

  nsCOMPtr<nsIDNSService> dns = nullptr;
  auto initTask = [&dns]() {
    // In case someone inited it while we were waiting
    if (gInited) {
      dns = nsDNSService::GetXPCOMSingleton();
      return;
    }
    dns = do_GetService(NS_DNSSERVICE_CID);
  };
  if (!NS_IsMainThread()) {
    // Forward to the main thread synchronously.
    RefPtr<nsIThread> mainThread = do_GetMainThread();
    if (!mainThread) {
      return nullptr;
    }

    SyncRunnable::DispatchToThread(
        mainThread, NS_NewRunnableFunction("GetOrInitDNSService", initTask));
  } else {
    initTask();
  }

  return dns.forget();
}

already_AddRefed<nsIDNSService> nsDNSService::GetXPCOMSingleton() {
  auto getDNSHelper = []() -> already_AddRefed<nsIDNSService> {
    if (XRE_IsParentProcess()) {
      return GetSingleton();
    }

    if (XRE_IsContentProcess()) {
      return ChildDNSService::GetSingleton();
    }

    return nullptr;
  };

  if (gInited) {
    return getDNSHelper();
  }

  nsCOMPtr<nsIDNSService> dns = getDNSHelper();
  if (dns) {
    gInited = true;
  }
  return dns.forget();
}

already_AddRefed<nsDNSService> nsDNSService::GetSingleton() {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!gDNSService) {
    if (!NS_IsMainThread()) {
      return nullptr;
    }
    gDNSService = new nsDNSService();
    if (NS_SUCCEEDED(gDNSService->Init())) {
      ClearOnShutdown(&gDNSService);
    } else {
      gDNSService = nullptr;
    }
  }

  return do_AddRef(gDNSService);
}

void nsDNSService::ReadPrefs(const char* name) {
  DNSServiceBase::ReadPrefs(name);

  bool tmpbool;

  // DNSservice prefs
  if (!name || !strcmp(name, kPrefDnsNotifyResolution)) {
    if (NS_SUCCEEDED(
            Preferences::GetBool(kPrefDnsNotifyResolution, &tmpbool))) {
      mNotifyResolution = tmpbool;
    }
  }
  if (!name || !strcmp(name, kPrefIPv4OnlyDomains)) {
    MutexAutoLock lock(mLock);
    Preferences::GetCString(kPrefIPv4OnlyDomains, mIPv4OnlyDomains);
  }
  if (!name || !strcmp(name, kPrefDnsLocalDomains)) {
    nsCString localDomains;
    Preferences::GetCString(kPrefDnsLocalDomains, localDomains);
    MutexAutoLock lock(mLock);
    mLocalDomains.Clear();
    for (const auto& token :
         nsCCharSeparatedTokenizerTemplate<NS_IsAsciiWhitespace,
                                           nsTokenizerFlags::SeparatorOptional>(
             localDomains, ',')
             .ToRange()) {
      mLocalDomains.Insert(token);
    }
  }
  if (!name || !strcmp(name, kPrefDnsForceResolve)) {
    Preferences::GetCString(kPrefDnsForceResolve, mForceResolve);
    mForceResolveOn = !mForceResolve.IsEmpty();
  }
  if (!name || !strcmp(name, kPrefDnsMockHTTPSRRDomain)) {
    nsCString mockHTTPSRRDomain;
    Preferences::GetCString(kPrefDnsMockHTTPSRRDomain, mockHTTPSRRDomain);
    if (mockHTTPSRRDomain.IsEmpty()) {
      mHasMockHTTPSRRDomainSet = false;
    } else {
      mHasMockHTTPSRRDomainSet = true;
      MutexAutoLock lock(mLock);
      mMockHTTPSRRDomain = std::move(mockHTTPSRRDomain);
    }
  }
}

NS_IMETHODIMP
nsDNSService::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  ReadPrefs(nullptr);

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->AddObserver(this, "last-pb-context-exited", false);
    observerService->AddObserver(this, NS_NETWORK_LINK_TOPIC, false);
    observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  }

  RefPtr<nsHostResolver> res;
  nsresult rv = nsHostResolver::Create(getter_AddRefs(res));
  if (NS_SUCCEEDED(rv)) {
    // now, set all of our member variables while holding the lock
    MutexAutoLock lock(mLock);
    MOZ_ASSERT(!mResolver);
    mResolver = res;
  }

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    // register as prefs observer
    prefs->AddObserver(kPrefDnsCacheEntries, this, false);
    // [pref-trie-audit] "network.dnsCacheExpiration" is an ambiguous prefix of
    // "network.dnsCacheExpirationGracePeriod"; triggers only for the exact pref
    // (grace period has its own AddObserver on the next line).
    prefs->AddObserver(kPrefDnsCacheExpiration, this, false);
    prefs->AddObserver(kPrefDnsCacheGrace, this, false);
    prefs->AddObserver(kPrefIPv4OnlyDomains, this, false);
    prefs->AddObserver(kPrefDnsLocalDomains, this, false);
    prefs->AddObserver(kPrefDnsForceResolve, this, false);
    prefs->AddObserver(kPrefDnsNotifyResolution, this, false);
    prefs->AddObserver(kPrefDnsMockHTTPSRRDomain, this, false);
    AddPrefObserver(prefs);
  }

  nsDNSPrefetch::Initialize(this);

  RegisterWeakMemoryReporter(this);

  nsCOMPtr<nsIObliviousHttpService> ohttpService(
      do_GetService("@mozilla.org/network/oblivious-http-service;1"));

  mTrrService = new TRRService();
  bool httpsEnabled;
  {
    MutexAutoLock lock(mLock);
    httpsEnabled = mResolver->IsNativeHTTPSEnabled();
  }
  if (NS_FAILED(mTrrService->Init(httpsEnabled))) {
    mTrrService = nullptr;
  }

  if (mTrrService && httpsEnabled) {
    mTrrService->ReadEtcHostsFile();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::Shutdown() {
  UnregisterWeakMemoryReporter(this);

  RefPtr<nsHostResolver> res;
  {
    MutexAutoLock lock(mLock);
    res = std::move(mResolver);
  }
  if (res) {
    // Shutdown outside lock.
    res->Shutdown();
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, NS_NETWORK_LINK_TOPIC);
    observerService->RemoveObserver(this, "last-pb-context-exited");
    observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  }

  return NS_OK;
}

bool nsDNSService::GetOffline() const {
  bool offline = false;
  nsCOMPtr<nsIIOService> io = do_GetService(NS_IOSERVICE_CONTRACTID);
  if (io) {
    io->GetOffline(&offline);
  }
  return offline;
}

NS_IMETHODIMP
nsDNSService::GetPrefetchEnabled(bool* outVal) {
  MutexAutoLock lock(mLock);
  *outVal = !mDisablePrefetch;
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::SetPrefetchEnabled(bool inVal) {
  MutexAutoLock lock(mLock);
  mDisablePrefetch = !inVal;
  return NS_OK;
}

already_AddRefed<nsHostResolver> nsDNSService::GetResolverLocked() {
  MutexAutoLock lock(mLock);
  return do_AddRef(mResolver);
}

nsresult nsDNSService::PreprocessHostname(bool aLocalDomain,
                                          const nsACString& aInput,
                                          nsACString& aACE) {
  // Enforce RFC 7686, whatever the case of the name, and with or without the
  // trailing dot of its fully qualified form.
  if (StaticPrefs::network_dns_blockDotOnion()) {
    uint32_t length = aInput.Length();
    while (length && aInput[length - 1] == '.') {
      --length;
    }
    if (StringEndsWith(Substring(aInput, 0, length), ".onion"_ns,
                       nsCaseInsensitiveCStringComparator)) {
      return NS_ERROR_UNKNOWN_HOST;
    }
  }

  if (aLocalDomain) {
    aACE.AssignLiteral("localhost");
    return NS_OK;
  }

  if (mTrrService && mTrrService->MaybeBootstrap(aInput, aACE)) {
    return NS_OK;
  }

  if (mForceResolveOn) {
    MutexAutoLock lock(mLock);
    if (!aInput.LowerCaseEqualsASCII("localhost") &&
        !aInput.LowerCaseEqualsASCII("127.0.0.1")) {
      aACE.Assign(mForceResolve);
      return NS_OK;
    }
  }

  if (!NS_SUCCEEDED(NS_DomainToASCIIAllowAnyGlyphfulASCII(aInput, aACE))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

bool nsDNSService::IsLocalDomain(const nsACString& aHostname) const {
  bool localDomain = mLocalDomains.Contains(aHostname);
  if (StringEndsWith(aHostname, "."_ns)) {
    localDomain = localDomain || mLocalDomains.Contains(Substring(
                                     aHostname, 0, aHostname.Length() - 1));
  }
  return localDomain;
}

nsresult nsDNSService::AsyncResolveInternal(
    const nsACString& aHostname, uint16_t type, nsIDNSService::DNSFlags flags,
    nsIDNSAdditionalInfo* aInfo, nsIDNSListener* aListener,
    nsIEventTarget* target_, const OriginAttributes& aOriginAttributes,
    nsICancelable** result) {
  // grab reference to global host resolver and IDN service.  beware
  // simultaneous shutdown!!
  RefPtr<nsHostResolver> res;
  nsCOMPtr<nsIEventTarget> target = target_;
  nsCOMPtr<nsIDNSListener> listener = aListener;
  bool localDomain = false;
  {
    MutexAutoLock lock(mLock);

    if (mDisablePrefetch && (flags & RESOLVE_SPECULATE)) {
      return NS_ERROR_DNS_LOOKUP_QUEUE_FULL;
    }

    res = mResolver;

    localDomain = IsLocalDomain(aHostname);
  }

  if (mNotifyResolution) {
    NS_DispatchToMainThread(new NotifyDNSResolution(aHostname));
  }

  if (!res) {
    return NS_ERROR_OFFLINE;
  }

  if ((type != RESOLVE_TYPE_DEFAULT) && (type != RESOLVE_TYPE_TXT) &&
      (type != RESOLVE_TYPE_HTTPSSVC)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (DNSForbiddenByActiveProxy(aHostname, flags)) {
    // nsHostResolver returns NS_ERROR_UNKNOWN_HOST for lots of reasons.
    // We use a different error code to differentiate this failure and to make
    // it clear(er) where this error comes from.
    return NS_ERROR_UNKNOWN_PROXY_HOST;
  }

  nsCString hostname;
  nsresult rv = PreprocessHostname(localDomain, aHostname, hostname);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (GetOffline() && (!StaticPrefs::network_dns_offline_localhost() ||
                       !hostname.LowerCaseEqualsASCII("localhost"))) {
    flags |= RESOLVE_OFFLINE;
  }

  // make sure JS callers get notification on the main thread
  nsCOMPtr<nsIXPConnectWrappedJS> wrappedListener = do_QueryInterface(listener);
  if (wrappedListener && !target) {
    target = GetMainThreadSerialEventTarget();
  }

  if (target) {
    listener = new DNSListenerProxy(listener, target);
  }

  uint16_t af =
      (type != RESOLVE_TYPE_DEFAULT) ? 0 : GetAFForLookup(hostname, flags);

  MOZ_ASSERT(listener);
  RefPtr<nsDNSAsyncRequest> req =
      new nsDNSAsyncRequest(res, hostname, DNSAdditionalInfo::URL(aInfo), type,
                            aOriginAttributes, listener, flags, af);
  if (!req) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (type == RESOLVE_TYPE_HTTPSSVC && mHasMockHTTPSRRDomainSet) {
    MutexAutoLock lock(mLock);
    if (req->mHost == mMockHTTPSRRDomain) {
      flags |= nsIDNSService::RESOLVE_CREATE_MOCK_HTTPS_RR;
    }
  }

  rv = res->ResolveHost(req->mHost, DNSAdditionalInfo::URL(aInfo),
                        DNSAdditionalInfo::Port(aInfo), type,
                        req->mOriginAttributes, flags, af, req);
  req.forget(result);
  return rv;
}

nsresult nsDNSService::CancelAsyncResolveInternal(
    const nsACString& aHostname, uint16_t aType, nsIDNSService::DNSFlags aFlags,
    nsIDNSAdditionalInfo* aInfo, nsIDNSListener* aListener, nsresult aReason,
    const OriginAttributes& aOriginAttributes) {
  // grab reference to global host resolver and IDN service.  beware
  // simultaneous shutdown!!
  RefPtr<nsHostResolver> res;
  bool localDomain = false;
  {
    MutexAutoLock lock(mLock);

    if (mDisablePrefetch && (aFlags & RESOLVE_SPECULATE)) {
      return NS_ERROR_DNS_LOOKUP_QUEUE_FULL;
    }

    res = mResolver;
    localDomain = IsLocalDomain(aHostname);
  }
  if (!res) {
    return NS_ERROR_OFFLINE;
  }

  nsCString hostname;
  nsresult rv = PreprocessHostname(localDomain, aHostname, hostname);
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint16_t af =
      (aType != RESOLVE_TYPE_DEFAULT) ? 0 : GetAFForLookup(hostname, aFlags);

  res->CancelAsyncRequest(hostname, DNSAdditionalInfo::URL(aInfo), aType,
                          aOriginAttributes, aFlags, af, aListener, aReason);
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::AsyncResolve(const nsACString& aHostname,
                           nsIDNSService::ResolveType aType,
                           nsIDNSService::DNSFlags flags,
                           nsIDNSAdditionalInfo* aInfo,
                           nsIDNSListener* listener, nsIEventTarget* target_,
                           JS::Handle<JS::Value> aOriginAttributes,
                           JSContext* aCx, uint8_t aArgc,
                           nsICancelable** result) {
  OriginAttributes attrs;

  LOG(("DNSService::AsyncResolve %s", PromiseFlatCString(aHostname).get()));
  if (aArgc == 1) {
    if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  return AsyncResolveInternal(aHostname, aType, flags, aInfo, listener, target_,
                              attrs, result);
}

NS_IMETHODIMP
nsDNSService::AsyncResolveNative(
    const nsACString& aHostname, nsIDNSService::ResolveType aType,
    nsIDNSService::DNSFlags flags, nsIDNSAdditionalInfo* aInfo,
    nsIDNSListener* aListener, nsIEventTarget* target_,
    const OriginAttributes& aOriginAttributes, nsICancelable** result) {
  return AsyncResolveInternal(aHostname, aType, flags, aInfo, aListener,
                              target_, aOriginAttributes, result);
}

NS_IMETHODIMP
nsDNSService::NewAdditionalInfo(const nsACString& aTrrURL, int32_t aPort,
                                nsIDNSAdditionalInfo** aInfo) {
  RefPtr<DNSAdditionalInfo> res = new DNSAdditionalInfo(aTrrURL, aPort);
  res.forget(aInfo);
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::CancelAsyncResolve(const nsACString& aHostname,
                                 nsIDNSService::ResolveType aType,
                                 nsIDNSService::DNSFlags aFlags,
                                 nsIDNSAdditionalInfo* aInfo,
                                 nsIDNSListener* aListener, nsresult aReason,
                                 JS::Handle<JS::Value> aOriginAttributes,
                                 JSContext* aCx, uint8_t aArgc) {
  OriginAttributes attrs;

  if (aArgc == 1) {
    if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  return CancelAsyncResolveInternal(aHostname, aType, aFlags, aInfo, aListener,
                                    aReason, attrs);
}

NS_IMETHODIMP
nsDNSService::CancelAsyncResolveNative(
    const nsACString& aHostname, nsIDNSService::ResolveType aType,
    nsIDNSService::DNSFlags aFlags, nsIDNSAdditionalInfo* aInfo,
    nsIDNSListener* aListener, nsresult aReason,
    const OriginAttributes& aOriginAttributes) {
  return CancelAsyncResolveInternal(aHostname, aType, aFlags, aInfo, aListener,
                                    aReason, aOriginAttributes);
}

NS_IMETHODIMP
nsDNSService::ResolveNative(const nsACString& aHostname,
                            nsIDNSService::DNSFlags flags,
                            const OriginAttributes& aOriginAttributes,
                            nsIDNSRecord** result) {
  // Synchronous resolution is not allowed on the main thread.
  // However, if RESOLVE_OFFLINE is set, we're only reading from the DNS cache,
  // so it's safe to allow this on the main thread.
  if (NS_IsMainThread() && !(flags & nsIDNSService::RESOLVE_OFFLINE)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return ResolveInternal(aHostname, flags, aOriginAttributes, result);
}

nsresult nsDNSService::ResolveInternal(
    const nsACString& aHostname, nsIDNSService::DNSFlags flags,
    const OriginAttributes& aOriginAttributes, nsIDNSRecord** result) {
  // grab reference to global host resolver and IDN service.  beware
  // simultaneous shutdown!!
  RefPtr<nsHostResolver> res;
  bool localDomain = false;
  {
    MutexAutoLock lock(mLock);
    res = mResolver;
    localDomain = IsLocalDomain(aHostname);
  }

  if (mNotifyResolution) {
    NS_DispatchToMainThread(new NotifyDNSResolution(aHostname));
  }

  NS_ENSURE_TRUE(res, NS_ERROR_OFFLINE);

  nsCString hostname;
  nsresult rv = PreprocessHostname(localDomain, aHostname, hostname);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (GetOffline() && (!StaticPrefs::network_dns_offline_localhost() ||
                       !hostname.LowerCaseEqualsASCII("localhost"))) {
    flags |= RESOLVE_OFFLINE;
  }

  if (DNSForbiddenByActiveProxy(aHostname, flags)) {
    return NS_ERROR_UNKNOWN_PROXY_HOST;
  }

  //  Since RESOLVE_OFFLINE is set, we can use DNSCacheRequest to retrieve the
  //  cached result directly.
  if (flags & RESOLVE_OFFLINE) {
    RefPtr<DNSCacheRequest> req = new DNSCacheRequest();
    uint16_t af = GetAFForLookup(hostname, flags);
    rv = res->ResolveHost(hostname, ""_ns, -1, RESOLVE_TYPE_DEFAULT,
                          aOriginAttributes, flags, af, req);
    if (NS_SUCCEEDED(rv)) {
      RefPtr<nsDNSRecord> rec =
          new nsDNSRecord(req->mHostRecord, req->mFromStaleCache);
      rec.forget(result);
    }
    return rv;
  }

  //
  // sync resolve: since the host resolver only works asynchronously, we need
  // to use a mutex and a condvar to wait for the result.  however, since the
  // result may be in the resolvers cache, we might get called back recursively
  // on the same thread.  so, our mutex needs to be re-entrant.  in other words,
  // we need to use a monitor! ;-)
  //

  PRMonitor* mon = PR_NewMonitor();
  if (!mon) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  PR_EnterMonitor(mon);
  RefPtr<nsDNSSyncRequest> syncReq = new nsDNSSyncRequest(mon);

  uint16_t af = GetAFForLookup(hostname, flags);

  // TRR uses the main thread for the HTTPS channel to the DoH server.
  // If this were to block the main thread while waiting for TRR it would
  // likely cause a deadlock. Instead we intentionally choose to not use TRR
  // for this.
  if (NS_IsMainThread()) {
    flags |= RESOLVE_DISABLE_TRR;
  }

  rv = res->ResolveHost(hostname, ""_ns, -1, RESOLVE_TYPE_DEFAULT,
                        aOriginAttributes, flags, af, syncReq);
  if (NS_SUCCEEDED(rv)) {
    // wait for result
    while (!syncReq->mDone) {
      PR_Wait(mon, PR_INTERVAL_NO_TIMEOUT);
    }

    if (NS_FAILED(syncReq->mStatus)) {
      rv = syncReq->mStatus;
    } else {
      NS_ASSERTION(syncReq->mHostRecord, "no host record");
      RefPtr<nsDNSRecord> rec =
          new nsDNSRecord(syncReq->mHostRecord, syncReq->mFromStaleCache);
      rec.forget(result);
    }
  }

  PR_ExitMonitor(mon);
  PR_DestroyMonitor(mon);
  return rv;
}

NS_IMETHODIMP
nsDNSService::GetMyHostName(nsACString& result) {
  char name[100];
  if (PR_GetSystemInfo(PR_SI_HOSTNAME, name, sizeof(name)) == PR_SUCCESS) {
    result = name;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDNSService::Observe(nsISupports* subject, const char* topic,
                      const char16_t* data) {
  bool flushCache = false;
  RefPtr<nsHostResolver> resolver = GetResolverLocked();

  if (!strcmp(topic, NS_NETWORK_LINK_TOPIC)) {
    nsAutoCString converted = NS_ConvertUTF16toUTF8(data);
    if (!strcmp(converted.get(), NS_NETWORK_LINK_DATA_CHANGED)) {
      flushCache = true;
    }
  } else if (!strcmp(topic, "last-pb-context-exited")) {
    flushCache = true;
  } else if (!strcmp(topic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    ReadPrefs(NS_ConvertUTF16toUTF8(data).get());
    NS_ENSURE_TRUE(resolver, NS_ERROR_NOT_INITIALIZED);
  } else if (!strcmp(topic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    Shutdown();
  }

  if (flushCache && resolver) {
    resolver->FlushCache(false);
    return NS_OK;
  }

  return NS_OK;
}

uint16_t nsDNSService::GetAFForLookup(const nsACString& host,
                                      nsIDNSService::DNSFlags flags) {
  if (StaticPrefs::network_dns_disableIPv6() ||
      (flags & RESOLVE_DISABLE_IPV6)) {
    return PR_AF_INET;
  }

  MutexAutoLock lock(mLock);

  uint16_t af = PR_AF_UNSPEC;

  if (!mIPv4OnlyDomains.IsEmpty()) {
    const char *domain, *domainEnd, *end;
    uint32_t hostLen, domainLen;

    // see if host is in one of the IPv4-only domains
    domain = mIPv4OnlyDomains.BeginReading();
    domainEnd = mIPv4OnlyDomains.EndReading();

    nsACString::const_iterator hostStart;
    host.BeginReading(hostStart);
    hostLen = host.Length();

    do {
      // skip any whitespace
      while (*domain == ' ' || *domain == '\t') {
        ++domain;
      }

      // find end of this domain in the string
      end = strchr(domain, ',');
      if (!end) {
        end = domainEnd;
      }

      // to see if the hostname is in the domain, check if the domain
      // matches the end of the hostname.
      domainLen = end - domain;
      if (domainLen && hostLen >= domainLen) {
        const char* hostTail = hostStart.get() + hostLen - domainLen;
        if (nsCRT::strncasecmp(domain, hostTail, domainLen) == 0) {
          // now, make sure either that the hostname is a direct match or
          // that the hostname begins with a dot.
          if (hostLen == domainLen || *hostTail == '.' ||
              *(hostTail - 1) == '.') {
            af = PR_AF_INET;
            break;
          }
        }
      }

      domain = end + 1;
    } while (*end);
  }

  if ((af != PR_AF_INET) && (flags & RESOLVE_DISABLE_IPV4)) {
    af = PR_AF_INET6;
  }

  return af;
}

NS_IMETHODIMP
nsDNSService::GetDNSCacheEntries(
    nsTArray<mozilla::net::DNSCacheEntries>* args) {
  RefPtr<nsHostResolver> resolver = GetResolverLocked();
  NS_ENSURE_TRUE(resolver, NS_ERROR_NOT_INITIALIZED);
  resolver->GetDNSCacheEntries(args);
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::ClearCache(bool aTrrToo) {
  RefPtr<nsHostResolver> resolver = GetResolverLocked();
  NS_ENSURE_TRUE(resolver, NS_ERROR_NOT_INITIALIZED);
  resolver->FlushCache(aTrrToo, true);
  return NS_OK;
}

// For testing purposes only
NS_IMETHODIMP
nsDNSService::ReloadParentalControlEnabled() {
  if (mTrrService) {
    mTrrService->mParentalControlEnabled =
        TRRService::ReloadParentalControlsEnabled();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::SetDetectedTrrURI(const nsACString& aURI) {
  if (mTrrService) {
    mTrrService->SetDetectedTrrURI(aURI);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::SetHeuristicDetectionResult(nsITRRSkipReason::value aValue) {
  if (mTrrService) {
    mTrrService->SetHeuristicDetectionResult(aValue);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::GetHeuristicDetectionResult(nsITRRSkipReason::value* aValue) {
  if (!mTrrService) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aValue = mTrrService->GetHeuristicDetectionResult();
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::GetTRRSkipReasonName(nsITRRSkipReason::value aValue,
                                   nsACString& aName) {
  return mozilla::net::GetTRRSkipReasonName(aValue, aName);
}

NS_IMETHODIMP
nsDNSService::GetCurrentTrrURI(nsACString& aURI) {
  if (mTrrService) {
    mTrrService->GetURI(aURI);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::GetCurrentTrrMode(nsIDNSService::ResolverMode* aMode) {
  *aMode = nsIDNSService::MODE_NATIVEONLY;  // The default mode.
  if (mTrrService) {
    *aMode = mTrrService->Mode();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::GetCurrentTrrConfirmationState(uint32_t* aConfirmationState) {
  *aConfirmationState = uint32_t(TRRService::CONFIRM_OFF);
  if (mTrrService) {
    *aConfirmationState = mTrrService->ConfirmationState();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::GetTrrDomain(nsACString& aTRRDomain) {
  aTRRDomain.Truncate();
  nsAutoCString url;
  if (mTrrService) {
    mTrrService->GetURI(url);
  }
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), url);
  if (NS_FAILED(rv)) {
    // An empty TRR domain in case of invalid URL.
    return NS_OK;
  }
  return uri->GetHost(aTRRDomain);
}

nsresult nsDNSService::GetTRRDomainKey(nsACString& aTRRDomain) {
  aTRRDomain = TRRService::ProviderKey();
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::SetHttp3FirstForServer(const nsACString& aServer, bool aEnabled) {
  if (mTrrService) {
    mTrrService->SetHttp3FirstForServer(aServer, aEnabled);
  }
  return NS_OK;
}

size_t nsDNSService::SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) {
  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - mIDN
  // - mLock

  size_t n = mallocSizeOf(this);
  MutexAutoLock lock(mLock);
  n += mResolver ? mResolver->SizeOfIncludingThis(mallocSizeOf) : 0;
  n += mIPv4OnlyDomains.SizeOfExcludingThisIfUnshared(mallocSizeOf);
  n += mLocalDomains.SizeOfExcludingThis(mallocSizeOf);
  n += mFailedSVCDomainNames.ShallowSizeOfExcludingThis(mallocSizeOf);
  for (const auto& data : mFailedSVCDomainNames.Values()) {
    n += data->ShallowSizeOfExcludingThis(mallocSizeOf);
    for (const auto& name : *data) {
      n += name.SizeOfExcludingThisIfUnshared(mallocSizeOf);
    }
  }
  return n;
}

MOZ_DEFINE_MALLOC_SIZE_OF(DNSServiceMallocSizeOf)

NS_IMETHODIMP
nsDNSService::CollectReports(nsIHandleReportCallback* aHandleReport,
                             nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT("explicit/network/dns-service", KIND_HEAP, UNITS_BYTES,
                     SizeOfIncludingThis(DNSServiceMallocSizeOf),
                     "Memory used for the DNS service.");

  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::ReportFailedSVCDomainName(const nsACString& aOwnerName,
                                        const nsACString& aSVCDomainName) {
  MutexAutoLock lock(mLock);

  mFailedSVCDomainNames.GetOrInsertNew(aOwnerName, 1)
      ->AppendElement(aSVCDomainName);
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::IsSVCDomainNameFailed(const nsACString& aOwnerName,
                                    const nsACString& aSVCDomainName,
                                    bool* aResult) {
  NS_ENSURE_ARG(aResult);

  MutexAutoLock lock(mLock);
  *aResult = false;
  nsTArray<nsCString>* failedList = mFailedSVCDomainNames.Get(aOwnerName);
  if (!failedList) {
    return NS_OK;
  }

  *aResult = failedList->Contains(aSVCDomainName);
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::ResetExcludedSVCDomainName(const nsACString& aOwnerName) {
  MutexAutoLock lock(mLock);
  mFailedSVCDomainNames.Remove(aOwnerName);
  return NS_OK;
}

NS_IMETHODIMP
nsDNSService::GetLastConfirmationStatus(nsresult* aConfirmationStatus) {
  if (!mTrrService) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aConfirmationStatus = mTrrService->LastConfirmationStatus();
  return NS_OK;
}

NS_IMETHODIMP nsDNSService::GetLastConfirmationSkipReason(
    TRRSkippedReason* aSkipReason) {
  if (!mTrrService) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aSkipReason = mTrrService->LastConfirmationSkipReason();
  return NS_OK;
}

namespace mozilla::net {
nsresult GetTRRSkipReasonName(TRRSkippedReason aReason, nsACString& aName) {
  static_assert(TRRSkippedReason::TRR_UNSET == 0);
  static_assert(TRRSkippedReason::TRR_OK == 1);
  static_assert(TRRSkippedReason::TRR_NO_GSERVICE == 2);
  static_assert(TRRSkippedReason::TRR_PARENTAL_CONTROL == 3);
  static_assert(TRRSkippedReason::TRR_OFF_EXPLICIT == 4);
  static_assert(TRRSkippedReason::TRR_REQ_MODE_DISABLED == 5);
  static_assert(TRRSkippedReason::TRR_MODE_NOT_ENABLED == 6);
  static_assert(TRRSkippedReason::TRR_FAILED == 7);
  static_assert(TRRSkippedReason::TRR_MODE_UNHANDLED_DEFAULT == 8);
  static_assert(TRRSkippedReason::TRR_MODE_UNHANDLED_DISABLED == 9);
  static_assert(TRRSkippedReason::TRR_DISABLED_FLAG == 10);
  static_assert(TRRSkippedReason::TRR_TIMEOUT == 11);
  static_assert(TRRSkippedReason::TRR_CHANNEL_DNS_FAIL == 12);
  static_assert(TRRSkippedReason::TRR_BROWSER_IS_OFFLINE == 13);
  static_assert(TRRSkippedReason::TRR_NOT_CONFIRMED == 14);
  static_assert(TRRSkippedReason::TRR_DID_NOT_MAKE_QUERY == 15);
  static_assert(TRRSkippedReason::TRR_UNKNOWN_CHANNEL_FAILURE == 16);
  static_assert(TRRSkippedReason::TRR_HOST_BLOCKED_TEMPORARY == 17);
  static_assert(TRRSkippedReason::TRR_SEND_FAILED == 18);
  static_assert(TRRSkippedReason::TRR_NET_RESET == 19);
  static_assert(TRRSkippedReason::TRR_NET_TIMEOUT == 20);
  static_assert(TRRSkippedReason::TRR_NET_REFUSED == 21);
  static_assert(TRRSkippedReason::TRR_NET_INTERRUPT == 22);
  static_assert(TRRSkippedReason::TRR_NET_INADEQ_SEQURITY == 23);
  static_assert(TRRSkippedReason::TRR_NO_ANSWERS == 24);
  static_assert(TRRSkippedReason::TRR_DECODE_FAILED == 25);
  static_assert(TRRSkippedReason::TRR_EXCLUDED == 26);
  static_assert(TRRSkippedReason::TRR_SERVER_RESPONSE_ERR == 27);
  static_assert(TRRSkippedReason::TRR_RCODE_FAIL == 28);
  static_assert(TRRSkippedReason::TRR_NO_CONNECTIVITY == 29);
  static_assert(TRRSkippedReason::TRR_NXDOMAIN == 30);
  static_assert(TRRSkippedReason::TRR_REQ_CANCELLED == 31);
  static_assert(TRRSkippedReason::ODOH_KEY_NOT_USABLE == 32);
  static_assert(TRRSkippedReason::ODOH_UPDATE_KEY_FAILED == 33);
  static_assert(TRRSkippedReason::ODOH_KEY_NOT_AVAILABLE == 34);
  static_assert(TRRSkippedReason::ODOH_ENCRYPTION_FAILED == 35);
  static_assert(TRRSkippedReason::ODOH_DECRYPTION_FAILED == 36);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_GOOGLE_SAFESEARCH ==
                37);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_YOUTUBE_SAFESEARCH ==
                38);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_ZSCALER_CANARY == 39);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_CANARY == 40);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_MODIFIED_ROOTS == 41);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_PARENTAL_CONTROLS ==
                42);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_THIRD_PARTY_ROOTS ==
                43);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_ENTERPRISE_POLICY ==
                44);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_VPN == 45);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_PROXY == 46);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_NRPT == 47);
  static_assert(TRRSkippedReason::TRR_BAD_URL == 48);
  static_assert(TRRSkippedReason::TRR_SYSTEM_SLEEP_MODE == 49);
  static_assert(TRRSkippedReason::TRR_HEURISTIC_TRIPPED_PRIVATE_DNS == 50);

  switch (aReason) {
    case TRRSkippedReason::TRR_UNSET:
      aName = "TRR_UNSET"_ns;
      break;
    case TRRSkippedReason::TRR_OK:
      aName = "TRR_OK"_ns;
      break;
    case TRRSkippedReason::TRR_NO_GSERVICE:
      aName = "TRR_NO_GSERVICE"_ns;
      break;
    case TRRSkippedReason::TRR_PARENTAL_CONTROL:
      aName = "TRR_PARENTAL_CONTROL"_ns;
      break;
    case TRRSkippedReason::TRR_OFF_EXPLICIT:
      aName = "TRR_OFF_EXPLICIT"_ns;
      break;
    case TRRSkippedReason::TRR_REQ_MODE_DISABLED:
      aName = "TRR_REQ_MODE_DISABLED"_ns;
      break;
    case TRRSkippedReason::TRR_MODE_NOT_ENABLED:
      aName = "TRR_MODE_NOT_ENABLED"_ns;
      break;
    case TRRSkippedReason::TRR_FAILED:
      aName = "TRR_FAILED"_ns;
      break;
    case TRRSkippedReason::TRR_MODE_UNHANDLED_DEFAULT:
      aName = "TRR_MODE_UNHANDLED_DEFAULT"_ns;
      break;
    case TRRSkippedReason::TRR_MODE_UNHANDLED_DISABLED:
      aName = "TRR_MODE_UNHANDLED_DISABLED"_ns;
      break;
    case TRRSkippedReason::TRR_DISABLED_FLAG:
      aName = "TRR_DISABLED_FLAG"_ns;
      break;
    case TRRSkippedReason::TRR_TIMEOUT:
      aName = "TRR_TIMEOUT"_ns;
      break;
    case TRRSkippedReason::TRR_CHANNEL_DNS_FAIL:
      aName = "TRR_CHANNEL_DNS_FAIL"_ns;
      break;
    case TRRSkippedReason::TRR_BROWSER_IS_OFFLINE:
      aName = "TRR_BROWSER_IS_OFFLINE"_ns;
      break;
    case TRRSkippedReason::TRR_NOT_CONFIRMED:
      aName = "TRR_NOT_CONFIRMED"_ns;
      break;
    case TRRSkippedReason::TRR_DID_NOT_MAKE_QUERY:
      aName = "TRR_DID_NOT_MAKE_QUERY"_ns;
      break;
    case TRRSkippedReason::TRR_UNKNOWN_CHANNEL_FAILURE:
      aName = "TRR_UNKNOWN_CHANNEL_FAILURE"_ns;
      break;
    case TRRSkippedReason::TRR_HOST_BLOCKED_TEMPORARY:
      aName = "TRR_HOST_BLOCKED_TEMPORARY"_ns;
      break;
    case TRRSkippedReason::TRR_SEND_FAILED:
      aName = "TRR_SEND_FAILED"_ns;
      break;
    case TRRSkippedReason::TRR_NET_RESET:
      aName = "TRR_NET_RESET"_ns;
      break;
    case TRRSkippedReason::TRR_NET_TIMEOUT:
      aName = "TRR_NET_TIMEOUT"_ns;
      break;
    case TRRSkippedReason::TRR_NET_REFUSED:
      aName = "TRR_NET_REFUSED"_ns;
      break;
    case TRRSkippedReason::TRR_NET_INTERRUPT:
      aName = "TRR_NET_INTERRUPT"_ns;
      break;
    case TRRSkippedReason::TRR_NET_INADEQ_SEQURITY:
      aName = "TRR_NET_INADEQ_SEQURITY"_ns;
      break;
    case TRRSkippedReason::TRR_NO_ANSWERS:
      aName = "TRR_NO_ANSWERS"_ns;
      break;
    case TRRSkippedReason::TRR_DECODE_FAILED:
      aName = "TRR_DECODE_FAILED"_ns;
      break;
    case TRRSkippedReason::TRR_EXCLUDED:
      aName = "TRR_EXCLUDED"_ns;
      break;
    case TRRSkippedReason::TRR_SERVER_RESPONSE_ERR:
      aName = "TRR_SERVER_RESPONSE_ERR"_ns;
      break;
    case TRRSkippedReason::TRR_RCODE_FAIL:
      aName = "TRR_RCODE_FAIL"_ns;
      break;
    case TRRSkippedReason::TRR_NO_CONNECTIVITY:
      aName = "TRR_NO_CONNECTIVITY"_ns;
      break;
    case TRRSkippedReason::TRR_NXDOMAIN:
      aName = "TRR_NXDOMAIN"_ns;
      break;
    case TRRSkippedReason::TRR_REQ_CANCELLED:
      aName = "TRR_REQ_CANCELLED"_ns;
      break;
    case TRRSkippedReason::ODOH_KEY_NOT_USABLE:
      aName = "ODOH_KEY_NOT_USABLE"_ns;
      break;
    case TRRSkippedReason::ODOH_UPDATE_KEY_FAILED:
      aName = "ODOH_UPDATE_KEY_FAILED"_ns;
      break;
    case TRRSkippedReason::ODOH_KEY_NOT_AVAILABLE:
      aName = "ODOH_KEY_NOT_AVAILABLE"_ns;
      break;
    case TRRSkippedReason::ODOH_ENCRYPTION_FAILED:
      aName = "ODOH_ENCRYPTION_FAILED"_ns;
      break;
    case TRRSkippedReason::ODOH_DECRYPTION_FAILED:
      aName = "ODOH_DECRYPTION_FAILED"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_GOOGLE_SAFESEARCH:
      aName = "TRR_HEURISTIC_TRIPPED_GOOGLE_SAFESEARCH"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_YOUTUBE_SAFESEARCH:
      aName = "TRR_HEURISTIC_TRIPPED_YOUTUBE_SAFESEARCH"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_ZSCALER_CANARY:
      aName = "TRR_HEURISTIC_TRIPPED_ZSCALER_CANARY"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_CANARY:
      aName = "TRR_HEURISTIC_TRIPPED_CANARY"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_MODIFIED_ROOTS:
      aName = "TRR_HEURISTIC_TRIPPED_MODIFIED_ROOTS"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_PARENTAL_CONTROLS:
      aName = "TRR_HEURISTIC_TRIPPED_PARENTAL_CONTROLS"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_THIRD_PARTY_ROOTS:
      aName = "TRR_HEURISTIC_TRIPPED_THIRD_PARTY_ROOTS"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_ENTERPRISE_POLICY:
      aName = "TRR_HEURISTIC_TRIPPED_ENTERPRISE_POLICY"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_VPN:
      aName = "TRR_HEURISTIC_TRIPPED_VPN"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_PROXY:
      aName = "TRR_HEURISTIC_TRIPPED_PROXY"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_NRPT:
      aName = "TRR_HEURISTIC_TRIPPED_NRPT"_ns;
      break;
    case TRRSkippedReason::TRR_BAD_URL:
      aName = "TRR_BAD_URL"_ns;
      break;
    case TRRSkippedReason::TRR_SYSTEM_SLEEP_MODE:
      aName = "TRR_SYSTEM_SLEEP_MODE"_ns;
      break;
    case TRRSkippedReason::TRR_HEURISTIC_TRIPPED_PRIVATE_DNS:
      aName = "TRR_HEURISTIC_TRIPPED_PRIVATE_DNS"_ns;
      break;
    default:
      MOZ_ASSERT(false, "Unknown value");
  }

  return NS_OK;
}
}  // namespace mozilla::net
