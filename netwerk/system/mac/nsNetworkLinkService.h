/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSNETWORKLINKSERVICEMAC_H_
#define NSNETWORKLINKSERVICEMAC_H_

#include <SystemConfiguration/SCNetworkReachability.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>

#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/SHA1.h"
#include "nsINetworkLinkService.h"
#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsString.h"

using prefix_and_netmask = std::pair<in6_addr, in6_addr>;

class nsNetworkLinkService : public nsINetworkLinkService,
                             public nsIObserver,
                             public nsITimerCallback,
                             public nsINamed {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSINETWORKLINKSERVICE
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  nsNetworkLinkService();

  nsresult Init();
  nsresult Shutdown();

  static void HashSortedPrefixesAndNetmasks(
      std::vector<prefix_and_netmask> prefixAndNetmaskStore,
      mozilla::SHA1Sum* sha1);

 protected:
  virtual ~nsNetworkLinkService();

 private:
  bool mLinkUp;
  bool mStatusKnown;

  SCNetworkReachabilityRef mReachability;
  CFRunLoopRef mCFRunLoop;
  CFRunLoopSourceRef mRunLoopSource;
  SCDynamicStoreRef mStoreRef;

  bool IPv4NetworkId(mozilla::SHA1Sum* sha1);
  bool IPv6NetworkId(mozilla::SHA1Sum* sha1);

  void UpdateReachability();
  void OnIPConfigChanged();
  void OnNetworkIdChanged();
  void OnReachabilityChanged();
  void NotifyObservers(const char* aTopic, const char* aData);
  static void ReachabilityChanged(SCNetworkReachabilityRef target,
                                  SCNetworkConnectionFlags flags, void* info);
  static void NetworkConfigChanged(SCDynamicStoreRef store,
                                   CFArrayRef changedKeys, void* info);
  void calculateNetworkIdWithDelay(uint32_t aDelay);
  void calculateNetworkIdInternal(void);
  void DNSConfigChanged(uint32_t aDelayMs);
  void GetDnsSuffixListInternal();
  bool RoutingFromKernel(nsTArray<nsCString>& aHash);
  bool RoutingTable(nsTArray<nsCString>& aHash);

  mozilla::Mutex mMutex;
  nsCString mNetworkId MOZ_GUARDED_BY(mMutex);
  // Hash state of the inputs of mNetworkId before SeedNetworkId() was
  // applied, for MatchNetworkID().
  mozilla::Maybe<mozilla::SHA1Sum> mUnseededNetworkId MOZ_GUARDED_BY(mMutex);
  nsTArray<nsCString> mDNSSuffixList MOZ_GUARDED_BY(mMutex);

  // The timer used to delay the calculation of network id since it takes some
  // time to discover the gateway's MAC address.
  nsCOMPtr<nsITimer> mNetworkIdTimer;

  // Scheduled timers used to delay querying of the DNS suffix list when
  // triggered by a network change. Guarded by mMutex.
  nsTArray<nsCOMPtr<nsITimer>> mDNSConfigChangedTimers MOZ_GUARDED_BY(mMutex);

  // IP address used to check the route for public traffic.
  struct in_addr mRouteCheckIPv4;
};

#endif /* NSNETWORKLINKSERVICEMAC_H_ */
