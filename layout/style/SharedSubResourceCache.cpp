/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSubResourceCache.h"

#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Services.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/CacheablePerformanceTimingData.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PerformanceResourceTimingBinding.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/PerformanceTiming.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "nsCOMPtr.h"
#include "nsDOMNavigationTiming.h"
#include "nsHttpResponseHead.h"
#include "nsIHttpChannel.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIRequest.h"
#include "nsITimedChannel.h"
#include "nsPIDOMWindow.h"
#include "nsQueryObject.h"

namespace mozilla {

namespace SharedSubResourceCacheUtils {

void AddMemoryPressureAndSafeBrowsingObserver(nsIObserver* aObserver) {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (!obsService) {
    return;
  }
  obsService->AddObserver(aObserver, "memory-pressure", false);
  obsService->AddObserver(aObserver, "memory-pressure-stop", false);
  if (XRE_IsParentProcess()) {
    obsService->AddObserver(aObserver, "safebrowsing-update-finished", false);
  }
}

void RemoveMemoryPressureAndSafeBrowsingObserver(nsIObserver* aObserver) {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (!obsService) {
    return;
  }
  obsService->RemoveObserver(aObserver, "memory-pressure");
  obsService->RemoveObserver(aObserver, "memory-pressure-stop");
  if (XRE_IsParentProcess()) {
    obsService->RemoveObserver(aObserver, "safebrowsing-update-finished");
  }
}

void AddPerformanceEntryForCache(
    const nsString& aEntryName, const nsString& aInitiatorType,
    const SubResourceNetworkMetadataHolder* aNetworkMetadata,
    TimeStamp aStartTime, TimeStamp aEndTime, dom::Document* aDocument) {
  MOZ_ASSERT(aDocument);

  if (!aNetworkMetadata || !aNetworkMetadata->GetPerfData()) {
    return;
  }

  nsPIDOMWindowInner* win = aDocument->GetInnerWindow();
  if (!win) {
    return;
  }
  RefPtr<dom::Performance> performance = win->GetPerformance();
  if (!performance) {
    return;
  }

  // TODO: Bug 1751383.
  auto renderBlocking = dom::RenderBlockingStatusType::Non_blocking;

  UniquePtr<dom::PerformanceTimingData> data(
      dom::PerformanceTimingData::Create(*aNetworkMetadata->GetPerfData(), 0,
                                         aStartTime, aEndTime, renderBlocking));
  if (!data) {
    return;
  }

  dom::PerformanceStorage* storage = performance->AsPerformanceStorage();
  MOZ_ASSERT(storage);
  storage->AddEntry(aEntryName, aInitiatorType, std::move(data));
}

bool ShouldClearEntry(nsIURI* aEntryURI, nsIPrincipal* aEntryPartitionPrincipal,
                      const Maybe<bool>& aChrome,
                      const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                      const Maybe<nsCString>& aSchemelessSite,
                      const Maybe<OriginAttributesPattern>& aPattern,
                      const Maybe<nsCString>& aURL) {
  if (aChrome.isSome()) {
    RefPtr<nsIURI> uri = aEntryURI;
    if (!uri) {
      // If there's no uri (inline resource) try to use the principal URI.
      uri = aEntryPartitionPrincipal->GetURI();
    }
    const bool isChrome = [&] {
      if (uri && (uri->SchemeIs("chrome") || uri->SchemeIs("resource"))) {
        return true;
      }
      if (!aEntryURI && aEntryPartitionPrincipal->IsSystemPrincipal()) {
        return true;
      }
      return false;
    }();

    if (*aChrome != isChrome) {
      return false;
    }

    if (!aPrincipal && !aSchemelessSite && !aURL) {
      return true;
    }
  }

  if (aURL) {
    if (!aEntryURI) {
      // Inline resources have no URL.
      return false;
    }
    nsAutoCString spec;
    nsresult rv = aEntryURI->GetSpec(spec);
    if (NS_FAILED(rv)) {
      return false;
    }
    return spec == *aURL;
  }

  if (aPrincipal && aEntryPartitionPrincipal->Equals(aPrincipal.ref())) {
    return true;
  }
  if (!aSchemelessSite) {
    return false;
  }
  // Clear by site.
  // Clear entries with site. This includes entries which are partitioned
  // under other top level sites (= have a partitionKey set).
  nsAutoCString principalBaseDomain;
  nsresult rv = aEntryPartitionPrincipal->GetBaseDomain(principalBaseDomain);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }
  if (principalBaseDomain.Equals(aSchemelessSite.ref()) &&
      aPattern.ref().Matches(aEntryPartitionPrincipal->OriginAttributesRef())) {
    return true;
  }

  // Clear entries partitioned under aSchemelessSite. We need to add the
  // partition key filter to aPattern so that we include any OA filtering
  // specified by the caller. For example the caller may pass aPattern = {
  // privateBrowsingId: 1 } which means we may only clear partitioned
  // private browsing data.
  OriginAttributesPattern patternWithPartitionKey(aPattern.ref());
  patternWithPartitionKey.mPartitionKeyPattern.Construct();
  patternWithPartitionKey.mPartitionKeyPattern.Value().mBaseDomain.Construct(
      NS_ConvertUTF8toUTF16(aSchemelessSite.ref()));

  return patternWithPartitionKey.Matches(
      aEntryPartitionPrincipal->OriginAttributesRef());
}

}  // namespace SharedSubResourceCacheUtils

SubResourceNetworkMetadataHolder::~SubResourceNetworkMetadataHolder() = default;

SubResourceNetworkMetadataHolder::SubResourceNetworkMetadataHolder(
    nsIRequest* aRequest) {
  nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(aRequest);
  if (timedChannel) {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
    mPerfData.emplace(timedChannel, httpChannel);
  }

  RefPtr<net::HttpBaseChannel> httpBaseChannel = do_QueryObject(aRequest);
  if (httpBaseChannel) {
    mResponseHead = httpBaseChannel->MaybeCloneResponseHeadForCachedResource();
  }
}

size_t SubResourceNetworkMetadataHolder::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;
  if (mPerfData) {
    n += mPerfData->SizeOfExcludingThis(aMallocSizeOf);
  }
  if (mResponseHead) {
    mResponseHead->SizeOfIncludingThis(aMallocSizeOf);
  }
  return n;
}

}  // namespace mozilla
