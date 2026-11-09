/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNetworkLinkService.h"

#include "../LinkServiceCommon.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsIObserverService.h"
#include "nsNetAddr.h"
#include "nsString.h"

using namespace mozilla;

static LazyLogModule gNotifyAddrLog("nsNetworkLinkService");
#define LOG(args) MOZ_LOG(gNotifyAddrLog, mozilla::LogLevel::Debug, args)

NS_IMPL_ISUPPORTS(nsNetworkLinkService, nsINetworkLinkService, nsIObserver)

nsNetworkLinkService::nsNetworkLinkService() : mStatusIsKnown(false) {}

NS_IMETHODIMP
nsNetworkLinkService::GetIsLinkUp(bool* aIsUp) {
  if (!mNetlinkSvc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mNetlinkSvc->GetIsLinkUp(aIsUp);
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetLinkStatusKnown(bool* aIsKnown) {
  *aIsKnown = mStatusIsKnown;
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetLinkType(uint32_t* aLinkType) {
  NS_ENSURE_ARG_POINTER(aLinkType);

  // XXX This function has not yet been implemented for this platform
  *aLinkType = nsINetworkLinkService::LINK_TYPE_UNKNOWN;
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetNetworkID(nsACString& aNetworkID) {
  if (!mNetlinkSvc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mNetlinkSvc->GetNetworkID(aNetworkID);
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetNetworkIDSeed(nsACString& aSeed) {
  aSeed = GetNetworkIdSeed();
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::MatchNetworkID(const nsACString& aNetworkID,
                                     const nsACString& aSeed,
                                     nsACString& aMatchingNetworkID,
                                     bool* aResult) {
  if (!mNetlinkSvc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aResult = mNetlinkSvc->MatchNetworkID(aNetworkID, aSeed, aMatchingNetworkID);
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetDnsSuffixList(nsTArray<nsCString>& aDnsSuffixList) {
  if (!mNetlinkSvc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return mNetlinkSvc->GetDnsSuffixList(aDnsSuffixList);
}

NS_IMETHODIMP
nsNetworkLinkService::GetResolvers(nsTArray<RefPtr<nsINetAddr>>& aResolvers) {
  nsTArray<mozilla::net::NetAddr> addresses;
  nsresult rv = GetNativeResolvers(addresses);
  if (NS_FAILED(rv)) {
    return rv;
  }

  for (const auto& addr : addresses) {
    aResolvers.AppendElement(MakeRefPtr<nsNetAddr>(&addr));
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetNativeResolvers(
    nsTArray<mozilla::net::NetAddr>& aResolvers) {
  if (!mNetlinkSvc) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return mNetlinkSvc->GetResolvers(aResolvers);
}

NS_IMETHODIMP
nsNetworkLinkService::GetPlatformDNSIndications(
    uint32_t* aPlatformDNSIndications) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNetworkLinkService::Observe(nsISupports* subject, const char* topic,
                              const char16_t* data) {
  if (!strcmp("xpcom-shutdown-threads", topic)) {
    Shutdown();
  }

  return NS_OK;
}

nsresult nsNetworkLinkService::Init() {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;
  rv = observerService->AddObserver(this, "xpcom-shutdown-threads", false);
  NS_ENSURE_SUCCESS(rv, rv);

  mNetlinkSvc = new mozilla::net::NetlinkService();
  rv = mNetlinkSvc->Init(this);
  if (NS_FAILED(rv)) {
    mNetlinkSvc = nullptr;
    LOG(("Cannot initialize NetlinkService [rv=0x%08" PRIx32 "]",
         static_cast<uint32_t>(rv)));
    return rv;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult nsNetworkLinkService::Shutdown() {
  // remove xpcom shutdown observer
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, "xpcom-shutdown-threads");
  }

  if (mNetlinkSvc) {
    mNetlinkSvc->Shutdown();
    mNetlinkSvc = nullptr;
  }

  return NS_OK;
}

void nsNetworkLinkService::OnNetworkChanged() {
  if (StaticPrefs::network_notify_changed()) {
    RefPtr<nsNetworkLinkService> self = this;
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "nsNetworkLinkService::OnNetworkChanged", [self]() {
          self->NotifyObservers(NS_NETWORK_LINK_TOPIC,
                                NS_NETWORK_LINK_DATA_CHANGED);
        }));
  }
}

void nsNetworkLinkService::OnNetworkIDChanged() {
  RefPtr<nsNetworkLinkService> self = this;
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsNetworkLinkService::OnNetworkIDChanged", [self]() {
        self->NotifyObservers(NS_NETWORK_ID_CHANGED_TOPIC, nullptr);
      }));
}

void nsNetworkLinkService::OnLinkUp() {
  RefPtr<nsNetworkLinkService> self = this;
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("nsNetworkLinkService::OnLinkUp", [self]() {
        self->NotifyObservers(NS_NETWORK_LINK_TOPIC, NS_NETWORK_LINK_DATA_UP);
      }));
}

void nsNetworkLinkService::OnLinkDown() {
  RefPtr<nsNetworkLinkService> self = this;
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("nsNetworkLinkService::OnLinkDown", [self]() {
        self->NotifyObservers(NS_NETWORK_LINK_TOPIC, NS_NETWORK_LINK_DATA_DOWN);
      }));
}

void nsNetworkLinkService::OnLinkStatusKnown() { mStatusIsKnown = true; }

void nsNetworkLinkService::OnDnsSuffixListUpdated() {
  RefPtr<nsNetworkLinkService> self = this;
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsNetworkLinkService::OnDnsSuffixListUpdated", [self]() {
        self->NotifyObservers(NS_DNS_SUFFIX_LIST_UPDATED_TOPIC, nullptr);
      }));
}

/* Sends the given event. Assumes aTopic/aData never goes out of scope (static
 * strings are ideal).
 */
void nsNetworkLinkService::NotifyObservers(const char* aTopic,
                                           const char* aData) {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("nsNetworkLinkService::NotifyObservers: topic:%s data:%s\n", aTopic,
       aData ? aData : ""));

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  if (observerService) {
    observerService->NotifyObservers(
        static_cast<nsINetworkLinkService*>(this), aTopic,
        aData ? NS_ConvertASCIItoUTF16(aData).get() : nullptr);
  }
}

// static
bool nsINetworkLinkService::HasNonLocalIPv6Address() {
  return mozilla::net::NetlinkService::HasNonLocalIPv6Address();
}

NS_IMETHODIMP
nsNetworkLinkService::GetNetworkInterfaces(
    nsTArray<RefPtr<nsINetworkInterface>>& aNetworkInterfaces) {
  if (!mNetlinkSvc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return mNetlinkSvc->GetNetworkInterfaces(aNetworkInterfaces);
}
