/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLDNSPrefetch.h"

#include "base/basictypes.h"
#include "mozilla/Components.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/HTMLLinkElement.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsCOMPtr.h"
#include "nsICancelable.h"
#include "nsIDNSListener.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIObserverService.h"
#include "nsIProtocolHandler.h"
#include "nsITimer.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"

using namespace mozilla::net;

namespace mozilla::dom {

class NoOpDNSListener final : public nsIDNSListener {
  // This class exists to give a safe callback no-op DNSListener
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDNSLISTENER

  NoOpDNSListener() = default;

 private:
  ~NoOpDNSListener() = default;
};

NS_IMPL_ISUPPORTS(NoOpDNSListener, nsIDNSListener)

NS_IMETHODIMP
NoOpDNSListener::OnLookupComplete(nsICancelable* request, nsIDNSRecord* rec,
                                  nsresult status) {
  return NS_OK;
}

// This is just a (size) optimization and could be avoided by storing the
// SupportsDNSPrefetch pointer of the element in the prefetch queue, but given
// we need this for GetURIForDNSPrefetch...
static SupportsDNSPrefetch& ToSupportsDNSPrefetch(Element& aElement) {
  if (auto* link = HTMLLinkElement::FromNode(aElement)) {
    return *link;
  }
  auto* anchor = HTMLAnchorElement::FromNode(aElement);
  MOZ_DIAGNOSTIC_ASSERT(anchor);
  return *anchor;
}

nsIURI* SupportsDNSPrefetch::GetURIForDNSPrefetch(Element& aElement) {
  MOZ_ASSERT(&ToSupportsDNSPrefetch(aElement) == this);
  if (auto* link = HTMLLinkElement::FromNode(aElement)) {
    return link->GetURI();
  }
  auto* anchor = HTMLAnchorElement::FromNode(aElement);
  MOZ_DIAGNOSTIC_ASSERT(anchor);
  return anchor->GetURI();
}

class DeferredDNSPrefetches final : public nsIWebProgressListener,
                                    public nsSupportsWeakReference,
                                    public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSIOBSERVER

  DeferredDNSPrefetches();

  void Activate();
  nsresult Add(nsIDNSService::DNSFlags flags, SupportsDNSPrefetch&, Element&);

  void RemoveUnboundLinks();

 private:
  ~DeferredDNSPrefetches();
  void Flush();

  void SubmitQueue();
  void SubmitQueueEntry(Element& aElement, nsIDNSService::DNSFlags aFlags);

  uint16_t mHead;
  uint16_t mTail;
  uint32_t mActiveLoaderCount;

  nsCOMPtr<nsITimer> mTimer;
  bool mTimerArmed;
  static void Tick(nsITimer* aTimer, void* aClosure);

  static const int sMaxDeferred = 512;  // keep power of 2 for masking
  static const int sMaxDeferredMask = (sMaxDeferred - 1);

  struct deferred_entry {
    nsIDNSService::DNSFlags mFlags;
    // SupportsDNSPrefetch clears this raw pointer in Destroyed().
    Element* mElement;
  } mEntries[sMaxDeferred];
};

static NS_DEFINE_CID(kDNSServiceCID, NS_DNSSERVICE_CID);
static bool sInitialized = false;
static nsIDNSService* sDNSService = nullptr;
static DeferredDNSPrefetches* sPrefetches = nullptr;
static NoOpDNSListener* sDNSListener = nullptr;

nsresult HTMLDNSPrefetch::Initialize() {
  if (sInitialized) {
    NS_WARNING("Initialize() called twice");
    return NS_OK;
  }

  sPrefetches = new DeferredDNSPrefetches();
  NS_ADDREF(sPrefetches);

  sDNSListener = new NoOpDNSListener();
  NS_ADDREF(sDNSListener);

  sPrefetches->Activate();

  if (IsNeckoChild()) NeckoChild::InitNeckoChild();

  sInitialized = true;
  return NS_OK;
}

nsresult HTMLDNSPrefetch::Shutdown() {
  if (!sInitialized) {
    NS_WARNING("Not Initialized");
    return NS_OK;
  }
  sInitialized = false;
  NS_IF_RELEASE(sDNSService);
  NS_IF_RELEASE(sPrefetches);
  NS_IF_RELEASE(sDNSListener);

  return NS_OK;
}

static bool EnsureDNSService() {
  if (sDNSService) {
    return true;
  }

  NS_IF_RELEASE(sDNSService);
  nsresult rv;
  rv = CallGetService(kDNSServiceCID, &sDNSService);
  if (NS_FAILED(rv)) {
    return false;
  }

  return !!sDNSService;
}

bool HTMLDNSPrefetch::IsAllowed(Document* aDocument) {
  // Do not use prefetch if the document's node principal is the system
  // principal.
  nsCOMPtr<nsIPrincipal> principal = aDocument->NodePrincipal();
  if (principal->IsSystemPrincipal()) {
    return false;
  }

  // There is no need to do prefetch on non UI scenarios such as XMLHttpRequest.
  return aDocument->IsDNSPrefetchAllowed() && aDocument->GetWindow();
}

static nsIDNSService::DNSFlags GetDNSFlagsFromElement(Element& aElement) {
  nsIChannel* channel = aElement.OwnerDoc()->GetChannel();
  if (!channel) {
    return nsIDNSService::RESOLVE_DEFAULT_FLAGS;
  }
  return nsIDNSService::GetFlagsFromTRRMode(channel->GetTRRMode());
}

nsIDNSService::DNSFlags HTMLDNSPrefetch::PriorityToDNSServiceFlags(
    Priority aPriority) {
  switch (aPriority) {
    case Priority::Low:
      return nsIDNSService::RESOLVE_PRIORITY_LOW;
    case Priority::Medium:
      return nsIDNSService::RESOLVE_PRIORITY_MEDIUM;
    case Priority::High:
      return nsIDNSService::RESOLVE_DEFAULT_FLAGS;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown priority");
  return nsIDNSService::RESOLVE_DEFAULT_FLAGS;
}

nsresult HTMLDNSPrefetch::DeferPrefetch(SupportsDNSPrefetch& aSupports,
                                        Element& aElement, Priority aPriority) {
  MOZ_ASSERT(&ToSupportsDNSPrefetch(aElement) == &aSupports);
  if (!(sInitialized && sPrefetches && sDNSListener) || !EnsureDNSService()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return sPrefetches->Add(
      GetDNSFlagsFromElement(aElement) | PriorityToDNSServiceFlags(aPriority),
      aSupports, aElement);
}

// Issues the speculative address prefetch for `aHost`. When Happy Eyeballs is
// enabled, issues two per-family lookups (A and AAAA) instead of one AF_UNSPEC
// lookup so a later HE connection can reuse them; the AAAA lookup is skipped
// when IPv6 is disabled. Returns the result of the first issued lookup.
static nsresult IssueSpeculativeAddrPrefetch(
    const nsACString& aHost, nsIDNSService::DNSFlags aFlags,
    const OriginAttributes& aOriginAttributes) {
  auto resolve = [&](nsIDNSService::DNSFlags aResolveFlags) -> nsresult {
    nsCOMPtr<nsICancelable> tmpOutstanding;
    return sDNSService->AsyncResolveNative(
        aHost, nsIDNSService::RESOLVE_TYPE_DEFAULT,
        aResolveFlags | nsIDNSService::RESOLVE_SPECULATE, nullptr, sDNSListener,
        nullptr, aOriginAttributes, getter_AddRefs(tmpOutstanding));
  };

  if (!StaticPrefs::network_http_happy_eyeballs_enabled()) {
    return resolve(aFlags);
  }

  nsresult rv = resolve(aFlags | nsIDNSService::RESOLVE_DISABLE_IPV6);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!StaticPrefs::network_dns_disableIPv6()) {
    rv = resolve(aFlags | nsIDNSService::RESOLVE_DISABLE_IPV4);
  }
  return rv;
}

// Cancels a prefetch issued by IssueSpeculativeAddrPrefetch, cancelling both
// per-family lookups when Happy Eyeballs is enabled to match what was issued.
static nsresult CancelSpeculativeAddrPrefetch(
    const nsACString& aHost, nsIDNSService::DNSFlags aFlags, nsresult aReason,
    const OriginAttributes& aOriginAttributes) {
  auto cancel = [&](nsIDNSService::DNSFlags aResolveFlags) -> nsresult {
    return sDNSService->CancelAsyncResolveNative(
        aHost, nsIDNSService::RESOLVE_TYPE_DEFAULT,
        aResolveFlags | nsIDNSService::RESOLVE_SPECULATE, nullptr, sDNSListener,
        aReason, aOriginAttributes);
  };

  if (!StaticPrefs::network_http_happy_eyeballs_enabled()) {
    return cancel(aFlags);
  }

  nsresult rv = cancel(aFlags | nsIDNSService::RESOLVE_DISABLE_IPV6);
  if (!StaticPrefs::network_dns_disableIPv6()) {
    (void)cancel(aFlags | nsIDNSService::RESOLVE_DISABLE_IPV4);
  }
  return rv;
}

nsresult HTMLDNSPrefetch::Prefetch(
    const nsACString& hostname,
    const OriginAttributes& aPartitionedPrincipalOriginAttributes,
    nsIDNSService::DNSFlags flags) {
  if (IsNeckoChild()) {
    // We need to check IsEmpty() because net_IsValidDNSHost()
    // considers empty strings to be valid hostnames
    if (!hostname.IsEmpty() && net_IsValidDNSHost(hostname)) {
      // during shutdown gNeckoChild might be null
      if (gNeckoChild) {
        gNeckoChild->SendHTMLDNSPrefetch(
            hostname, aPartitionedPrincipalOriginAttributes, flags);
      }
    }
    return NS_OK;
  }

  if (!(sInitialized && sPrefetches && sDNSListener) || !EnsureDNSService())
    return NS_ERROR_NOT_AVAILABLE;

  if (StaticPrefs::network_dns_upgrade_with_https_rr() ||
      StaticPrefs::network_dns_use_https_rr_as_altsvc()) {
    nsCOMPtr<nsICancelable> tmpOutstanding;
    (void)sDNSService->AsyncResolveNative(
        hostname, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
        flags | nsIDNSService::RESOLVE_SPECULATE, nullptr, sDNSListener,
        nullptr, aPartitionedPrincipalOriginAttributes,
        getter_AddRefs(tmpOutstanding));
  }

  return IssueSpeculativeAddrPrefetch(hostname, flags,
                                      aPartitionedPrincipalOriginAttributes);
}

nsresult HTMLDNSPrefetch::Prefetch(
    const nsACString& hostname,
    const OriginAttributes& aPartitionedPrincipalOriginAttributes,
    nsIRequest::TRRMode aMode, Priority aPriority) {
  return Prefetch(hostname, aPartitionedPrincipalOriginAttributes,
                  nsIDNSService::GetFlagsFromTRRMode(aMode) |
                      PriorityToDNSServiceFlags(aPriority));
}

nsresult HTMLDNSPrefetch::CancelPrefetch(SupportsDNSPrefetch& aSupports,
                                         Element& aElement, Priority aPriority,
                                         nsresult aReason) {
  MOZ_ASSERT(&ToSupportsDNSPrefetch(aElement) == &aSupports);

  if (!(sInitialized && sPrefetches && sDNSListener) || !EnsureDNSService()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsIDNSService::DNSFlags flags =
      GetDNSFlagsFromElement(aElement) | PriorityToDNSServiceFlags(aPriority);

  nsIURI* uri = aSupports.GetURIForDNSPrefetch(aElement);
  if (!uri) {
    return NS_OK;
  }

  nsAutoCString hostname;
  uri->GetAsciiHost(hostname);

  OriginAttributes oa;
  StoragePrincipalHelper::GetOriginAttributesForNetworkState(
      aElement.OwnerDoc(), oa);

  return CancelPrefetch(hostname, oa, flags, aReason);
}

nsresult HTMLDNSPrefetch::CancelPrefetch(
    const nsACString& hostname,
    const OriginAttributes& aPartitionedPrincipalOriginAttributes,
    nsIDNSService::DNSFlags flags, nsresult aReason) {
  // Forward this request to Necko Parent if we're a child process
  if (IsNeckoChild()) {
    // We need to check IsEmpty() because net_IsValidDNSHost()
    // considers empty strings to be valid hostnames
    if (!hostname.IsEmpty() && net_IsValidDNSHost(hostname)) {
      // during shutdown gNeckoChild might be null
      if (gNeckoChild && gNeckoChild->CanSend()) {
        gNeckoChild->SendCancelHTMLDNSPrefetch(
            hostname, aPartitionedPrincipalOriginAttributes, flags, aReason);
      }
    }
    return NS_OK;
  }

  if (!(sInitialized && sPrefetches && sDNSListener) || !EnsureDNSService()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Forward cancellation to DNS service
  nsresult rv = CancelSpeculativeAddrPrefetch(
      hostname, flags, aReason, aPartitionedPrincipalOriginAttributes);

  if (StaticPrefs::network_dns_upgrade_with_https_rr() ||
      StaticPrefs::network_dns_use_https_rr_as_altsvc()) {
    (void)sDNSService->CancelAsyncResolveNative(
        hostname, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
        flags | nsIDNSService::RESOLVE_SPECULATE,
        nullptr,  // AdditionalInfo
        sDNSListener, aReason, aPartitionedPrincipalOriginAttributes);
  }
  return rv;
}

nsresult HTMLDNSPrefetch::CancelPrefetch(
    const nsACString& hostname,
    const OriginAttributes& aPartitionedPrincipalOriginAttributes,
    nsIRequest::TRRMode aTRRMode, Priority aPriority, nsresult aReason) {
  return CancelPrefetch(hostname, aPartitionedPrincipalOriginAttributes,
                        nsIDNSService::GetFlagsFromTRRMode(aTRRMode) |
                            PriorityToDNSServiceFlags(aPriority),
                        aReason);
}

void HTMLDNSPrefetch::ElementDestroyed(Element& aElement,
                                       SupportsDNSPrefetch& aSupports) {
  MOZ_ASSERT(&ToSupportsDNSPrefetch(aElement) == &aSupports);
  MOZ_ASSERT(aSupports.IsInDNSPrefetch());
  if (sPrefetches) {
    // Clean up all the possible links at once.
    sPrefetches->RemoveUnboundLinks();
  }
}

void HTMLDNSPrefetch::SendRequest(Element& aElement,
                                  nsIDNSService::DNSFlags aFlags) {
  auto& supports = ToSupportsDNSPrefetch(aElement);

  nsIURI* uri = supports.GetURIForDNSPrefetch(aElement);
  if (!uri) {
    return;
  }

  nsAutoCString hostName;
  uri->GetAsciiHost(hostName);
  if (hostName.IsEmpty()) {
    return;
  }

  bool isLocalResource = false;
  nsresult rv = NS_URIChainHasFlags(
      uri, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE, &isLocalResource);
  if (NS_FAILED(rv) || isLocalResource) {
    return;
  }

  OriginAttributes oa;
  StoragePrincipalHelper::GetOriginAttributesForNetworkState(
      aElement.OwnerDoc(), oa);

  if (IsNeckoChild()) {
    // during shutdown gNeckoChild might be null
    if (gNeckoChild) {
      gNeckoChild->SendHTMLDNSPrefetch(hostName, oa, aFlags);
    }
  } else {
    // Fetch HTTPS RR if needed.
    if (StaticPrefs::network_dns_upgrade_with_https_rr() ||
        StaticPrefs::network_dns_use_https_rr_as_altsvc()) {
      nsCOMPtr<nsICancelable> tmpOutstanding;
      sDNSService->AsyncResolveNative(
          hostName, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
          aFlags | nsIDNSService::RESOLVE_SPECULATE, nullptr, sDNSListener,
          nullptr, oa, getter_AddRefs(tmpOutstanding));
    }

    rv = IssueSpeculativeAddrPrefetch(hostName, aFlags, oa);
    if (NS_FAILED(rv)) {
      return;
    }
  }

  // Tell element that deferred prefetch was requested.
  supports.DNSPrefetchRequestStarted();
}

void SupportsDNSPrefetch::TryDNSPrefetch(
    Element& aOwner, HTMLDNSPrefetch::PrefetchSource aSource) {
  MOZ_ASSERT(aOwner.IsInComposedDoc());
  if (HTMLDNSPrefetch::IsAllowed(aOwner.OwnerDoc())) {
    if (!(sInitialized && sDNSListener) || !EnsureDNSService()) {
      return;
    }

    if (aSource == HTMLDNSPrefetch::PrefetchSource::AnchorSpeculativePrefetch) {
      HTMLDNSPrefetch::DeferPrefetch(*this, aOwner,
                                     HTMLDNSPrefetch::Priority::Low);
    } else if (aSource == HTMLDNSPrefetch::PrefetchSource::LinkDnsPrefetch) {
      HTMLDNSPrefetch::SendRequest(
          aOwner, GetDNSFlagsFromElement(aOwner) |
                      HTMLDNSPrefetch::PriorityToDNSServiceFlags(
                          HTMLDNSPrefetch::Priority::High));
    } else {
      MOZ_ASSERT_UNREACHABLE("Unknown DNS prefetch type");
    }
  }
}

void SupportsDNSPrefetch::CancelDNSPrefetch(Element& aOwner) {
  // If prefetch was deferred, clear flag and move on
  if (mDNSPrefetchDeferred) {
    mDNSPrefetchDeferred = false;
    // Else if prefetch was requested, clear flag and send cancellation
  } else if (mDNSPrefetchRequested) {
    mDNSPrefetchRequested = false;
    // Possible that hostname could have changed since binding, but since this
    // covers common cases, most DNS prefetch requests will be canceled
    HTMLDNSPrefetch::CancelPrefetch(
        *this, aOwner, HTMLDNSPrefetch::Priority::Low, NS_ERROR_ABORT);
  }
}

DeferredDNSPrefetches::DeferredDNSPrefetches()
    : mHead(0), mTail(0), mActiveLoaderCount(0), mTimerArmed(false) {
  mTimer = NS_NewTimer();
}

DeferredDNSPrefetches::~DeferredDNSPrefetches() {
  if (mTimerArmed) {
    mTimerArmed = false;
    mTimer->Cancel();
  }

  Flush();
}

NS_IMPL_ISUPPORTS(DeferredDNSPrefetches, nsIWebProgressListener,
                  nsISupportsWeakReference, nsIObserver)

void DeferredDNSPrefetches::Flush() {
  for (; mHead != mTail; mTail = (mTail + 1) & sMaxDeferredMask) {
    Element* element = mEntries[mTail].mElement;
    if (element) {
      ToSupportsDNSPrefetch(*element).ClearIsInDNSPrefetch();
    }
    mEntries[mTail].mElement = nullptr;
  }
}

nsresult DeferredDNSPrefetches::Add(nsIDNSService::DNSFlags flags,
                                    SupportsDNSPrefetch& aSupports,
                                    Element& aElement) {
  // The FIFO has no lock, so it can only be accessed on main thread
  NS_ASSERTION(NS_IsMainThread(),
               "DeferredDNSPrefetches::Add must be on main thread");

  aSupports.DNSPrefetchRequestDeferred();

  if (((mHead + 1) & sMaxDeferredMask) == mTail) {
    return NS_ERROR_DNS_LOOKUP_QUEUE_FULL;
  }

  aSupports.SetIsInDNSPrefetch();
  mEntries[mHead].mFlags = flags;
  mEntries[mHead].mElement = &aElement;
  mHead = (mHead + 1) & sMaxDeferredMask;

  if (!mActiveLoaderCount && !mTimerArmed && mTimer) {
    mTimerArmed = true;
    mTimer->InitWithNamedFuncCallback(
        Tick, this, 2000, nsITimer::TYPE_ONE_SHOT,
        "HTMLDNSPrefetch::DeferredDNSPrefetches::Tick"_ns);
  }

  return NS_OK;
}

static bool BuildPrefetchArgs(SupportsDNSPrefetch& aSupports, Element& aElement,
                              nsIDNSService::DNSFlags aFlags,
                              HTMLDNSPrefetchArgs& aOut) {
  nsIURI* uri = aSupports.GetURIForDNSPrefetch(aElement);
  if (!uri) {
    return false;
  }
  nsAutoCString hostName;
  uri->GetAsciiHost(hostName);
  if (hostName.IsEmpty()) {
    return false;
  }
  bool isLocalResource = false;
  nsresult rv = NS_URIChainHasFlags(
      uri, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE, &isLocalResource);
  if (NS_FAILED(rv) || isLocalResource) {
    return false;
  }
  OriginAttributes oa;
  StoragePrincipalHelper::GetOriginAttributesForNetworkState(
      aElement.OwnerDoc(), oa);
  aOut = HTMLDNSPrefetchArgs(hostName, oa, aFlags);
  return true;
}

void DeferredDNSPrefetches::SubmitQueue() {
  NS_ASSERTION(NS_IsMainThread(),
               "DeferredDNSPrefetches::SubmitQueue must be on main thread");
  if (!EnsureDNSService()) {
    return;
  }

  if (IsNeckoChild()) {
    nsTArray<HTMLDNSPrefetchArgs> batch;
    for (; mHead != mTail; mTail = (mTail + 1) & sMaxDeferredMask) {
      Element* element = mEntries[mTail].mElement;
      nsIDNSService::DNSFlags flags = mEntries[mTail].mFlags;
      mEntries[mTail].mElement = nullptr;
      if (!element) {
        continue;
      }
      auto& supports = ToSupportsDNSPrefetch(*element);
      supports.ClearIsInDNSPrefetch();
      if (!supports.IsDNSPrefetchRequestDeferred()) {
        continue;
      }
      HTMLDNSPrefetchArgs args;
      if (BuildPrefetchArgs(supports, *element, flags, args)) {
        supports.DNSPrefetchRequestStarted();
        batch.AppendElement(std::move(args));
      }
    }
    if (!batch.IsEmpty() && gNeckoChild) {
      gNeckoChild->SendHTMLDNSPrefetchBatch(std::move(batch));
    }
  } else {
    for (; mHead != mTail; mTail = (mTail + 1) & sMaxDeferredMask) {
      Element* element = mEntries[mTail].mElement;
      if (!element) {
        continue;
      }
      SubmitQueueEntry(*element, mEntries[mTail].mFlags);
      mEntries[mTail].mElement = nullptr;
    }
  }

  if (mTimerArmed) {
    mTimerArmed = false;
    mTimer->Cancel();
  }
}

void DeferredDNSPrefetches::SubmitQueueEntry(Element& aElement,
                                             nsIDNSService::DNSFlags aFlags) {
  auto& supports = ToSupportsDNSPrefetch(aElement);
  supports.ClearIsInDNSPrefetch();

  // Only prefetch here if request was deferred and deferral not cancelled
  if (!supports.IsDNSPrefetchRequestDeferred()) {
    return;
  }

  HTMLDNSPrefetch::SendRequest(aElement, aFlags);
}

void DeferredDNSPrefetches::Activate() {
  // Register as an observer for the document loader
  nsCOMPtr<nsIWebProgress> progress = components::DocLoader::Service();
  if (progress)
    progress->AddProgressListener(this, nsIWebProgress::NOTIFY_STATE_DOCUMENT);

  // Register as an observer for xpcom shutdown events so we can drop any
  // element refs
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService)
    observerService->AddObserver(this, "xpcom-shutdown", true);
}

void DeferredDNSPrefetches::RemoveUnboundLinks() {
  uint16_t tail = mTail;
  while (mHead != tail) {
    Element* element = mEntries[tail].mElement;
    if (element && !element->IsInComposedDoc()) {
      ToSupportsDNSPrefetch(*element).ClearIsInDNSPrefetch();
      mEntries[tail].mElement = nullptr;
    }
    tail = (tail + 1) & sMaxDeferredMask;
  }
}

// nsITimer related method

void DeferredDNSPrefetches::Tick(nsITimer* aTimer, void* aClosure) {
  auto* self = static_cast<DeferredDNSPrefetches*>(aClosure);

  NS_ASSERTION(NS_IsMainThread(),
               "DeferredDNSPrefetches::Tick must be on main thread");
  NS_ASSERTION(self->mTimerArmed, "Timer is not armed");

  self->mTimerArmed = false;

  // If the queue is not submitted here because there are outstanding pages
  // being loaded, there is no need to rearm the timer as the queue will be
  // submtited when those loads complete.
  if (!self->mActiveLoaderCount) {
    self->SubmitQueue();
  }
}

//////////// nsIWebProgressListener methods

NS_IMETHODIMP
DeferredDNSPrefetches::OnStateChange(nsIWebProgress* aWebProgress,
                                     nsIRequest* aRequest,
                                     uint32_t progressStateFlags,
                                     nsresult aStatus) {
  // The FIFO has no lock, so it can only be accessed on main thread
  NS_ASSERTION(NS_IsMainThread(),
               "DeferredDNSPrefetches::OnStateChange must be on main thread");

  if (progressStateFlags & STATE_IS_DOCUMENT) {
    if (progressStateFlags & STATE_STOP) {
      // Initialization may have missed a STATE_START notification, so do
      // not go negative
      if (mActiveLoaderCount) mActiveLoaderCount--;

      if (!mActiveLoaderCount) {
        SubmitQueue();
      }
    } else if (progressStateFlags & STATE_START)
      mActiveLoaderCount++;
  }

  return NS_OK;
}

NS_IMETHODIMP
DeferredDNSPrefetches::OnProgressChange(nsIWebProgress* aProgress,
                                        nsIRequest* aRequest,
                                        int32_t curSelfProgress,
                                        int32_t maxSelfProgress,
                                        int32_t curTotalProgress,
                                        int32_t maxTotalProgress) {
  return NS_OK;
}

NS_IMETHODIMP
DeferredDNSPrefetches::OnLocationChange(nsIWebProgress* aWebProgress,
                                        nsIRequest* aRequest, nsIURI* location,
                                        uint32_t aFlags) {
  return NS_OK;
}

NS_IMETHODIMP
DeferredDNSPrefetches::OnStatusChange(nsIWebProgress* aWebProgress,
                                      nsIRequest* aRequest, nsresult aStatus,
                                      const char16_t* aMessage) {
  return NS_OK;
}

NS_IMETHODIMP
DeferredDNSPrefetches::OnSecurityChange(nsIWebProgress* aWebProgress,
                                        nsIRequest* aRequest, uint32_t aState) {
  return NS_OK;
}

NS_IMETHODIMP
DeferredDNSPrefetches::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                              nsIRequest* aRequest,
                                              uint32_t aEvent) {
  return NS_OK;
}

//////////// nsIObserver method

NS_IMETHODIMP
DeferredDNSPrefetches::Observe(nsISupports* subject, const char* topic,
                               const char16_t* data) {
  if (!strcmp(topic, "xpcom-shutdown")) Flush();

  return NS_OK;
}

}  // namespace mozilla::dom
