/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <algorithm>
#include <vector>

#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <resolv.h>

#include "../../base/IPv6Utils.h"
#include "../LinkServiceCommon.h"
#include "../NetworkLinkServiceDefines.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Base64.h"
#include "mozilla/Components.h"
#include "mozilla/Logging.h"
#include "mozilla/SHA1.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsIObserverService.h"
#include "nsNetCID.h"
#include "nsNetworkLinkService.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"

#import <Cocoa/Cocoa.h>
#import <netinet/in.h>

#define NETWORK_NOTIFY_CHANGED_PREF "network.notify.changed"

using namespace mozilla;

static LazyLogModule gNotifyAddrLog("nsNotifyAddr");
#define LOG(args) MOZ_LOG(gNotifyAddrLog, mozilla::LogLevel::Debug, args)

// See bug 1584165. Sometimes the ARP table is empty or doesn't have
// the entry of gateway after the network change, so we'd like to delay
// the calaulation of network id.
static const uint32_t kNetworkIdDelayAfterChange = 3000;

// When you remove search domains from the settings page and hit Apply a
// network change event is generated, but res.dnsrch is not updated to the
// correct values. Thus, after a network change, we add a small delay to
// the runnable so the OS has the chance to update the values.
static const uint32_t kDNSSuffixDelayAfterChange = 50;

// If non-successful, extract the error code and return it.  This
// error code dance is inspired by
// http://developer.apple.com/technotes/tn/tn1145.html
static OSStatus getErrorCodeBool(Boolean success) {
  OSStatus err = noErr;
  if (!success) {
    int scErr = ::SCError();
    if (scErr == kSCStatusOK) {
      scErr = kSCStatusFailed;
    }
    err = scErr;
  }
  return err;
}

// If given a NULL pointer, return the error code.
static OSStatus getErrorCodePtr(const void* value) {
  return getErrorCodeBool(value != nullptr);
}

// Convenience function to allow NULL input.
static void CFReleaseSafe(CFTypeRef cf) {
  if (cf) {
    // "If cf is NULL, this will cause a runtime error and your
    // application will crash." / Apple docs
    ::CFRelease(cf);
  }
}

mozilla::Atomic<bool, mozilla::MemoryOrdering::Relaxed> sHasNonLocalIPv6{true};

NS_IMPL_ISUPPORTS(nsNetworkLinkService, nsINetworkLinkService, nsIObserver,
                  nsITimerCallback, nsINamed)

nsNetworkLinkService::nsNetworkLinkService()
    : mLinkUp(true),
      mStatusKnown(false),
      mReachability(nullptr),
      mCFRunLoop(nullptr),
      mRunLoopSource(nullptr),
      mStoreRef(nullptr),
      mMutex("nsNetworkLinkService::mMutex") {}

nsNetworkLinkService::~nsNetworkLinkService() = default;

NS_IMETHODIMP
nsNetworkLinkService::GetIsLinkUp(bool* aIsUp) {
  *aIsUp = mLinkUp;
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetLinkStatusKnown(bool* aIsUp) {
  *aIsUp = mStatusKnown;
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
  MutexAutoLock lock(mMutex);
  aNetworkID = mNetworkId;
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
  MutexAutoLock lock(mMutex);
  *aResult = MatchNetworkId(aNetworkID, aSeed, mUnseededNetworkId, mNetworkId,
                            aMatchingNetworkID);
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetPlatformDNSIndications(
    uint32_t* aPlatformDNSIndications) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void nsNetworkLinkService::GetDnsSuffixListInternal() {
  MOZ_ASSERT(!NS_IsMainThread());
  LOG(("GetDnsSuffixListInternal"));

  auto sendNotification = mozilla::MakeScopeExit([self = RefPtr{this}] {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "nsNetworkLinkService::GetDnsSuffixListInternal", [self]() {
          self->NotifyObservers(NS_DNS_SUFFIX_LIST_UPDATED_TOPIC, nullptr);
        }));
  });

  nsTArray<nsCString> result;

  struct __res_state res;
  if (res_ninit(&res) == 0) {
    for (int i = 0; i < MAXDNSRCH; i++) {
      if (!res.dnsrch[i]) {
        break;
      }
      LOG(("DNS search domain from [%s]\n", res.dnsrch[i]));
      result.AppendElement(nsCString(res.dnsrch[i]));
    }
    // Calls to res_ninit() should be matched with calls to res_ndestroy()
    // when available, as it is on macOS. This resolves bug 2039387. On
    // macOS 26.5 (and up?) a call to res_ninit() always triggers a call to
    // notify_register_check(). But only res_ndestroy() triggers a
    // corresponding call to notify_cancel(). res_nclose() doesn't. So
    // calling only res_nclose() here leaks resources on macOS 26.5. And it
    // eventually crashes our app when a "registration limit" is exceeded.
    res_ndestroy(&res);
  }

  MutexAutoLock lock(mMutex);
  mDNSSuffixList = std::move(result);
}

NS_IMETHODIMP
nsNetworkLinkService::GetDnsSuffixList(nsTArray<nsCString>& aDnsSuffixList) {
  aDnsSuffixList.Clear();

  MutexAutoLock lock(mMutex);
  aDnsSuffixList.AppendElements(mDNSSuffixList);
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetResolvers(nsTArray<RefPtr<nsINetAddr>>& aResolvers) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsNetworkLinkService::GetNativeResolvers(
    nsTArray<mozilla::net::NetAddr>& aResolvers) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

#ifndef SA_SIZE
#  define SA_SIZE(sa)                                    \
    ((!(sa) || ((struct sockaddr*)(sa))->sa_len == 0)    \
         ? sizeof(uint32_t)                              \
         : 1 + ((((struct sockaddr*)(sa))->sa_len - 1) | \
                (sizeof(uint32_t) - 1)))
#endif

static bool getMac(struct sockaddr_dl* sdl, char* buf, size_t bufsize) {
  unsigned char* mac;
  mac = (unsigned char*)LLADDR(sdl);

  if (sdl->sdl_alen != 6) {
    LOG(("networkid: unexpected MAC size %u", sdl->sdl_alen));
    return false;
  }

  snprintf(buf, bufsize, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return true;
}

/* If the IP matches, get the MAC and return true */
static bool matchIp(struct sockaddr_dl* sdl, struct sockaddr_inarp* addr,
                    char* ip, char* buf, size_t bufsize) {
  if (sdl->sdl_alen) {
    if (!strcmp(inet_ntoa(addr->sin_addr), ip)) {
      if (getMac(sdl, buf, bufsize)) {
        return true; /* done! */
      }
    }
  }
  return false; /* continue */
}

/*
 * Scan for the 'IP' address in the ARP table and store the corresponding MAC
 * address in 'mac'. The output buffer is 'maclen' bytes big.
 *
 * Returns 'true' if it found the IP and returns a MAC.
 */
static bool scanArp(char* ip, char* mac, size_t maclen) {
  int mib[6];
  char *lim, *next;
  int st;

  mib[0] = CTL_NET;
  mib[1] = PF_ROUTE;
  mib[2] = 0;
  mib[3] = AF_INET;
  mib[4] = NET_RT_FLAGS;
  mib[5] = RTF_LLINFO;

  size_t needed;
  auto allocateBuf = [&]() -> UniquePtr<char[]> {
    // calling sysctl with a null buffer to get the minimum buffer size
    if (sysctl(mib, 6, nullptr, &needed, nullptr, 0) < 0) {
      return nullptr;
    }

    if (needed == 0) {
      LOG(("scanArp: empty table"));
      return nullptr;
    }

    return std::make_unique<char[]>(needed);
  };

  UniquePtr<char[]> buf = allocateBuf();
  if (!buf) {
    return false;
  }

  st = sysctl(mib, 6, &buf[0], &needed, nullptr, 0);
  // If errno is ENOMEM, try to allocate a new buffer and try again.
  if (st != 0) {
    if (errno != ENOMEM) {
      return false;
    }

    buf = allocateBuf();
    if (!buf) {
      return false;
    }

    st = sysctl(mib, 6, &buf[0], &needed, nullptr, 0);
    if (st == -1) {
      return false;
    }
  }

  lim = &buf[needed];

  struct rt_msghdr* rtm;
  for (next = &buf[0]; next < lim; next += rtm->rtm_msglen) {
    rtm = reinterpret_cast<struct rt_msghdr*>(next);
    struct sockaddr_inarp* sin2 =
        reinterpret_cast<struct sockaddr_inarp*>(rtm + 1);
    struct sockaddr_dl* sdl =
        reinterpret_cast<struct sockaddr_dl*>((char*)sin2 + SA_SIZE(sin2));
    if (matchIp(sdl, sin2, ip, mac, maclen)) {
      return true;
    }
  }

  return false;
}

// Append the mac address of rtm to `stringsToHash`. If it's not in arp table,
// append ifname and IP address.
static bool parseHashKey(struct rt_msghdr* rtm,
                         nsTArray<nsCString>& stringsToHash,
                         bool skipDstCheck) {
  struct sockaddr* sa;
  struct sockaddr_in* sockin;
  char ip[INET_ADDRSTRLEN];

  // Ignore the routing table message without destination/gateway sockaddr.
  // Destination address is needed to check if the gateway is default or
  // overwritten by VPN. If yes, append the mac address or IP/interface name to
  // `stringsToHash`.
  if ((rtm->rtm_addrs & (RTA_DST | RTA_GATEWAY)) != (RTA_DST | RTA_GATEWAY)) {
    return false;
  }

  sa = reinterpret_cast<struct sockaddr*>(rtm + 1);

  struct sockaddr* destination =
      reinterpret_cast<struct sockaddr*>((char*)sa + RTAX_DST * SA_SIZE(sa));
  if (!destination || destination->sa_family != AF_INET) {
    return false;
  }

  sockin = reinterpret_cast<struct sockaddr_in*>(destination);

  inet_ntop(AF_INET, &sockin->sin_addr.s_addr, ip, sizeof(ip) - 1);

  if (!skipDstCheck && strcmp("0.0.0.0", ip)) {
    return false;
  }

  struct sockaddr* gateway = reinterpret_cast<struct sockaddr*>(
      (char*)sa + RTAX_GATEWAY * SA_SIZE(sa));

  if (!gateway) {
    return false;
  }
  if (gateway->sa_family == AF_INET) {
    sockin = reinterpret_cast<struct sockaddr_in*>(gateway);
    inet_ntop(AF_INET, &sockin->sin_addr.s_addr, ip, sizeof(ip) - 1);
    char mac[18];

    // TODO: cache the arp table instead of multiple system call.
    if (scanArp(ip, mac, sizeof(mac))) {
      stringsToHash.AppendElement(nsCString(mac));
    } else {
      // Can't find a real MAC address. This might be a VPN gateway.
      char buf[IFNAMSIZ] = {0};
      char* ifName = if_indextoname(rtm->rtm_index, buf);
      if (!ifName) {
        LOG(("parseHashKey: AF_INET if_indextoname failed"));
        return false;
      }

      stringsToHash.AppendElement(nsCString(ifName));
      stringsToHash.AppendElement(nsCString(ip));
    }
  } else if (gateway->sa_family == AF_LINK) {
    char buf[64];
    struct sockaddr_dl* sockdl = reinterpret_cast<struct sockaddr_dl*>(gateway);
    if (getMac(sockdl, buf, sizeof(buf))) {
      stringsToHash.AppendElement(nsCString(buf));
    } else {
      char buf[IFNAMSIZ] = {0};
      char* ifName = if_indextoname(rtm->rtm_index, buf);
      if (!ifName) {
        LOG(("parseHashKey: AF_LINK if_indextoname failed"));
        return false;
      }

      stringsToHash.AppendElement(nsCString(ifName));
    }
  }
  return true;
}

// It detects the IP of the default gateways in the routing table, then the MAC
// address of that IP in the ARP table before it hashes that string (to avoid
// information leakage).
bool nsNetworkLinkService::RoutingTable(nsTArray<nsCString>& aHash) {
  size_t needed;
  int mib[6];
  struct rt_msghdr* rtm;

  mib[0] = CTL_NET;
  mib[1] = PF_ROUTE;
  mib[2] = 0;
  mib[3] = 0;
  mib[4] = NET_RT_DUMP;
  mib[5] = 0;

  if (sysctl(mib, 6, nullptr, &needed, nullptr, 0) < 0) {
    return false;
  }

  UniquePtr<char[]> buf(new char[needed]);

  if (sysctl(mib, 6, &buf[0], &needed, nullptr, 0) < 0) {
    return false;
  }

  char* lim = &buf[0] + needed;
  bool rv = false;

  // `next + 1 < lim` ensures we have valid `rtm->rtm_msglen` which is an
  // unsigned short at the beginning of `rt_msghdr`.
  for (char* next = &buf[0]; next + 1 < lim; next += rtm->rtm_msglen) {
    rtm = reinterpret_cast<struct rt_msghdr*>(next);

    if (next + rtm->rtm_msglen > lim) {
      LOG(("Rt msg is truncated..."));
      break;
    }

    if (parseHashKey(rtm, aHash, false)) {
      rv = true;
    }
  }
  return rv;
}

// Detect the routing of network.netlink.route.check.IPv4
bool nsNetworkLinkService::RoutingFromKernel(nsTArray<nsCString>& aHash) {
  int sockfd;
  if ((sockfd = socket(AF_ROUTE, SOCK_RAW, 0)) == -1) {
    LOG(("RoutingFromKernel: Can create a socket for network id"));
    return false;
  }
  auto sockfd_guard = mozilla::MakeScopeExit([sockfd] { close(sockfd); });

  MOZ_ASSERT(!NS_IsMainThread());

  size_t needed = 1024;
  struct rt_msghdr* rtm;
  struct sockaddr_in* sin;
  UniquePtr<char[]> buf(new char[needed]);
  pid_t pid;
  int seq;

  rtm = reinterpret_cast<struct rt_msghdr*>(&buf[0]);
  memset(rtm, 0, sizeof(struct rt_msghdr));
  rtm->rtm_msglen = sizeof(struct rt_msghdr) + sizeof(struct sockaddr_in);
  rtm->rtm_version = RTM_VERSION;
  rtm->rtm_type = RTM_GET;
  rtm->rtm_addrs = RTA_DST;
  rtm->rtm_pid = (pid = getpid());
  rtm->rtm_seq = (seq = random());

  sin = reinterpret_cast<struct sockaddr_in*>(rtm + 1);
  memset(sin, 0, sizeof(struct sockaddr_in));
  sin->sin_len = sizeof(struct sockaddr_in);
  sin->sin_family = AF_INET;
  sin->sin_addr = mRouteCheckIPv4;

  if (write(sockfd, rtm, rtm->rtm_msglen) == -1) {
    LOG(("RoutingFromKernel: write() failed. No route to the predefine "
         "destincation"));
    return false;
  }

  do {
    ssize_t r;
    if ((r = read(sockfd, rtm, needed)) < 0) {
      LOG(("RoutingFromKernel: read() failed."));
      return false;
    }

    LOG(("RoutingFromKernel: read() rtm_type: %d (%d), rtm_pid: %d (%d), "
         "rtm_seq: %d (%d)\n",
         rtm->rtm_type, RTM_GET, rtm->rtm_pid, pid, rtm->rtm_seq, seq));
  } while (rtm->rtm_type != RTM_GET || rtm->rtm_pid != pid ||
           rtm->rtm_seq != seq);

  return parseHashKey(rtm, aHash, true);
}

// Figure out the current IPv4 "network identification" string.
bool nsNetworkLinkService::IPv4NetworkId(SHA1Sum* aSHA1) {
  nsTArray<nsCString> hash;
  if (!RoutingTable(hash)) {
    NS_WARNING("IPv4NetworkId: No default gateways");
  }

  if (!RoutingFromKernel(hash)) {
    NS_WARNING("IPv4NetworkId: No route to the predefined destination");
  }

  // We didn't get any valid hash key to generate network ID.
  if (hash.IsEmpty()) {
    LOG(("IPv4NetworkId: No valid hash key"));
    return false;
  }

  hash.Sort();
  for (uint32_t i = 0; i < hash.Length(); ++i) {
    LOG(("IPv4NetworkId: Hashing string for network id: %s", hash[i].get()));
    aSHA1->update(hash[i].get(), hash[i].Length());
  }

  return true;
}

//
// Sort and hash the prefixes and netmasks
//
void nsNetworkLinkService::HashSortedPrefixesAndNetmasks(
    std::vector<prefix_and_netmask> prefixAndNetmaskStore, SHA1Sum* sha1) {
  // getifaddrs does not guarantee the interfaces will always be in the same
  // order. We want to make sure the hash remains consistent Regardless of the
  // interface order.
  std::sort(prefixAndNetmaskStore.begin(), prefixAndNetmaskStore.end(),
            [](prefix_and_netmask a, prefix_and_netmask b) {
              // compare prefixStore
              int comparedPrefix = memcmp(&a.first, &b.first, sizeof(in6_addr));
              if (comparedPrefix == 0) {
                // compare netmaskStore
                return memcmp(&a.second, &b.second, sizeof(in6_addr)) < 0;
              }
              return comparedPrefix < 0;
            });

  for (const auto& prefixAndNetmask : prefixAndNetmaskStore) {
    sha1->update(&prefixAndNetmask.first, sizeof(in6_addr));
    sha1->update(&prefixAndNetmask.second, sizeof(in6_addr));
  }
}

bool nsNetworkLinkService::IPv6NetworkId(SHA1Sum* sha1) {
  struct ifaddrs* ifap;
  std::vector<prefix_and_netmask> prefixAndNetmaskStore;

  if (!getifaddrs(&ifap)) {
    bool hasNonLocalIPv6 = false;
    struct ifaddrs* ifa;
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) {
        continue;
      }
      if ((AF_INET6 == ifa->ifa_addr->sa_family) &&
          !(ifa->ifa_flags & (IFF_POINTOPOINT | IFF_LOOPBACK))) {
        // only IPv6 interfaces that aren't pointtopoint or loopback
        hasNonLocalIPv6 = true;
        struct sockaddr_in6* sin_netmask =
            (struct sockaddr_in6*)ifa->ifa_netmask;
        if (sin_netmask) {
          struct sockaddr_in6* sin_addr = (struct sockaddr_in6*)ifa->ifa_addr;
          int scope = net::utils::ipv6_scope(sin_addr->sin6_addr.s6_addr);
          if (scope == IPV6_SCOPE_GLOBAL) {
            struct in6_addr prefix;
            memset(&prefix, 0, sizeof(prefix));
            // Get the prefix by combining the address and netmask.
            for (size_t i = 0; i < sizeof(prefix); ++i) {
              prefix.s6_addr[i] = sin_addr->sin6_addr.s6_addr[i] &
                                  sin_netmask->sin6_addr.s6_addr[i];
            }

            // check if prefix and netmask was already found
            auto prefixAndNetmask =
                std::make_pair(prefix, sin_netmask->sin6_addr);
            auto foundPosition = std::find_if(
                prefixAndNetmaskStore.begin(), prefixAndNetmaskStore.end(),
                [&prefixAndNetmask](prefix_and_netmask current) {
                  return memcmp(&prefixAndNetmask.first, &current.first,
                                sizeof(in6_addr)) == 0 &&
                         memcmp(&prefixAndNetmask.second, &current.second,
                                sizeof(in6_addr)) == 0;
                });
            if (foundPosition != prefixAndNetmaskStore.end()) {
              continue;
            }
            prefixAndNetmaskStore.push_back(prefixAndNetmask);
          }
        }
      }
    }
    sHasNonLocalIPv6 = hasNonLocalIPv6;
    freeifaddrs(ifap);
  }
  if (prefixAndNetmaskStore.empty()) {
    LOG(("IPv6NetworkId failed"));
    return false;
  }

  nsNetworkLinkService::HashSortedPrefixesAndNetmasks(prefixAndNetmaskStore,
                                                      sha1);

  return true;
}

void nsNetworkLinkService::calculateNetworkIdWithDelay(uint32_t aDelay) {
  MOZ_ASSERT(NS_IsMainThread());

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownNetTeardown)) {
    return;
  }

  if (aDelay) {
    if (mNetworkIdTimer) {
      LOG(("Restart the network id timer."));
      mNetworkIdTimer->Cancel();
    } else {
      LOG(("Create the network id timer."));
      mNetworkIdTimer = NS_NewTimer();
    }
    mNetworkIdTimer->InitWithCallback(this, aDelay, nsITimer::TYPE_ONE_SHOT);
    return;
  }

  nsCOMPtr<nsIEventTarget> target;
  target = mozilla::components::StreamTransport::Service();
  if (!target) {
    return;
  }

  MOZ_ALWAYS_SUCCEEDS(target->Dispatch(
      NewRunnableMethod("nsNetworkLinkService::calculateNetworkIdInternal",
                        this,
                        &nsNetworkLinkService::calculateNetworkIdInternal),
      NS_DISPATCH_NORMAL));
}

NS_IMETHODIMP
nsNetworkLinkService::Notify(nsITimer* aTimer) {
  MOZ_ASSERT(aTimer == mNetworkIdTimer);

  mNetworkIdTimer = nullptr;
  calculateNetworkIdWithDelay(0);
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetName(nsACString& aName) {
  aName.AssignLiteral("nsNetworkLinkService");
  return NS_OK;
}

void nsNetworkLinkService::calculateNetworkIdInternal(void) {
  MOZ_ASSERT(!NS_IsMainThread(), "Should not be called on the main thread");
  SHA1Sum sha1;
  bool idChanged = false;
  bool found4 = IPv4NetworkId(&sha1);
  bool found6 = IPv6NetworkId(&sha1);

  if (found4 || found6) {
    nsAutoCString output;
    SHA1Sum unseeded = sha1;
    SeedNetworkId(sha1);
    uint8_t digest[SHA1Sum::kHashSize];
    sha1.finish(digest);
    nsAutoCString newString(reinterpret_cast<char*>(digest),
                            SHA1Sum::kHashSize);
    nsresult rv = Base64Encode(newString, output);
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
    LOG(("networkid: id %s\n", output.get()));
    MutexAutoLock lock(mMutex);
    mUnseededNetworkId = mozilla::Some(unseeded);
    if (mNetworkId != output) {
      // new id
      if (found4 && !found6) {
        glean::network::id.AccumulateSingleSample(1);  // IPv4 only
      } else if (!found4 && found6) {
        glean::network::id.AccumulateSingleSample(3);  // IPv6 only
      } else {
        glean::network::id.AccumulateSingleSample(4);  // Both!
      }
      mNetworkId = output;
      idChanged = true;
    } else {
      // same id
      LOG(("Same network id"));
      glean::network::id.AccumulateSingleSample(2);
    }
  } else {
    // no id
    LOG(("No network id"));
    MutexAutoLock lock(mMutex);
    if (!mNetworkId.IsEmpty()) {
      mNetworkId.Truncate();
      idChanged = true;
      glean::network::id.AccumulateSingleSample(0);
    }
    mUnseededNetworkId.reset();
  }

  if (idChanged) {
    RefPtr<nsNetworkLinkService> self = this;

    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "nsNetworkLinkService::calculateNetworkIdInternal",
        [self]() { self->OnNetworkIdChanged(); }));
  }
}

NS_IMETHODIMP
nsNetworkLinkService::Observe(nsISupports* subject, const char* topic,
                              const char16_t* data) {
  if (!strcmp(topic, "xpcom-shutdown")) {
    Shutdown();
  }

  return NS_OK;
}

/* static */
void nsNetworkLinkService::NetworkConfigChanged(SCDynamicStoreRef aStoreREf,
                                                CFArrayRef aChangedKeys,
                                                void* aInfo) {
  LOG(("nsNetworkLinkService::NetworkConfigChanged"));

  bool ipConfigChanged = false;
  bool dnsConfigChanged = false;
  bool skipOnIPConfigChanged = false;
  for (CFIndex i = 0; i < CFArrayGetCount(aChangedKeys); ++i) {
    CFStringRef key =
        static_cast<CFStringRef>(CFArrayGetValueAtIndex(aChangedKeys, i));
    // Don't call OnIPConfigChanged() if aChangedKeys contains any EAPOL
    // notifications. This works around a crash bug deep in macOS system
    // code. Our observers should still get reachability notifications, so
    // this shouldn't interfere with network functionality.
    if (CFStringHasSuffix(key, CFSTR("EAPOL"))) {
      CFIndex keyMaxLength =
          CFStringGetMaximumSizeForEncoding(CFStringGetLength(key),
                                            kCFStringEncodingUTF8) +
          1;
      char* keyBuffer = static_cast<char*>(moz_xmalloc(keyMaxLength));
      keyBuffer[0] = 0;
      CFStringGetCString(key, keyBuffer, keyMaxLength, kCFStringEncodingUTF8);
      LOG(("Skipping OnIPConfigChanged() on changed key %s", keyBuffer));
      free(keyBuffer);
      skipOnIPConfigChanged = true;
    }
    if (CFStringHasSuffix(key, kSCEntNetIPv4) ||
        CFStringHasSuffix(key, kSCEntNetIPv6)) {
      ipConfigChanged = true;
    }
    if (CFStringHasSuffix(key, kSCEntNetDNS)) {
      dnsConfigChanged = true;
    }
  }

  nsNetworkLinkService* service = static_cast<nsNetworkLinkService*>(aInfo);
  if (ipConfigChanged && !skipOnIPConfigChanged) {
    service->OnIPConfigChanged();
  }

  if (dnsConfigChanged) {
    service->DNSConfigChanged(kDNSSuffixDelayAfterChange);
  }
}

void nsNetworkLinkService::DNSConfigChanged(uint32_t aDelayMs) {
  LOG(("nsNetworkLinkService::DNSConfigChanged"));
  nsCOMPtr<nsIEventTarget> target;
  target = mozilla::components::StreamTransport::Service();
  if (!target) {
    return;
  }
  if (aDelayMs) {
    MutexAutoLock lock(mMutex);
    nsCOMPtr<nsITimer> timer;
    MOZ_ALWAYS_SUCCEEDS(NS_NewTimerWithCallback(
        getter_AddRefs(timer),
        [self = RefPtr{this}](nsITimer* aTimer) {
          self->GetDnsSuffixListInternal();

          MutexAutoLock lock(self->mMutex);
          self->mDNSConfigChangedTimers.RemoveElement(aTimer);
        },
        TimeDuration::FromMilliseconds(aDelayMs), nsITimer::TYPE_ONE_SHOT,
        "nsNetworkLinkService::GetDnsSuffixListInternal"_ns, target));
    mDNSConfigChangedTimers.AppendElement(timer);
  } else {
    MOZ_ALWAYS_SUCCEEDS(target->Dispatch(NS_NewRunnableFunction(
        "nsNetworkLinkService::GetDnsSuffixListInternal",
        [self = RefPtr{this}]() { self->GetDnsSuffixListInternal(); })));
  }
}

nsresult nsNetworkLinkService::Init(void) {
  nsresult rv;

  nsCOMPtr<nsIObserverService> observerService;
  observerService = mozilla::components::Observer::Service(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = observerService->AddObserver(this, "xpcom-shutdown", false);
  NS_ENSURE_SUCCESS(rv, rv);

  if (inet_pton(AF_INET, ROUTE_CHECK_IPV4, &mRouteCheckIPv4) != 1) {
    LOG(("Cannot parse address " ROUTE_CHECK_IPV4));
    MOZ_DIAGNOSTIC_CRASH("Cannot parse address " ROUTE_CHECK_IPV4);
    return NS_ERROR_UNEXPECTED;
  }

  // If the network reachability API can reach 0.0.0.0 without
  // requiring a connection, there is a network interface available.
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = AF_INET;
  mReachability = ::SCNetworkReachabilityCreateWithAddress(
      nullptr, (struct sockaddr*)&addr);
  if (!mReachability) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  SCNetworkReachabilityContext context = {0, this, nullptr, nullptr, nullptr};
  if (!::SCNetworkReachabilitySetCallback(mReachability, ReachabilityChanged,
                                          &context)) {
    NS_WARNING("SCNetworkReachabilitySetCallback failed.");
    ::CFRelease(mReachability);
    mReachability = nullptr;
    return NS_ERROR_NOT_AVAILABLE;
  }

  SCDynamicStoreContext storeContext = {0, this, nullptr, nullptr, nullptr};
  mStoreRef =
      ::SCDynamicStoreCreate(nullptr, CFSTR("IPAndDNSChangeCallbackSCF"),
                             NetworkConfigChanged, &storeContext);

  CFStringRef patterns[6] = {nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr};
  OSStatus err = getErrorCodePtr(mStoreRef);
  if (err == noErr) {
    // This pattern is "State:/Network/Service/[^/]+/IPv4".
    patterns[0] = ::SCDynamicStoreKeyCreateNetworkServiceEntity(
        nullptr, kSCDynamicStoreDomainState, kSCCompAnyRegex, kSCEntNetIPv4);
    // This pattern is "State:/Network/Service/[^/]+/IPv6".
    patterns[1] = ::SCDynamicStoreKeyCreateNetworkServiceEntity(
        nullptr, kSCDynamicStoreDomainState, kSCCompAnyRegex, kSCEntNetIPv6);
    // This pattern is "State:/Network/Service/[^/]+/DNS".
    patterns[2] = ::SCDynamicStoreKeyCreateNetworkServiceEntity(
        nullptr, kSCDynamicStoreDomainState, kSCCompAnyRegex, kSCEntNetDNS);
    // This pattern is "Setup:/Network/Service/[^/]+/DNS".
    patterns[3] = ::SCDynamicStoreKeyCreateNetworkServiceEntity(
        nullptr, kSCDynamicStoreDomainSetup, kSCCompAnyRegex, kSCEntNetDNS);
    // We want to be able to ignore some groups of notifications that contain
    // "EAPOL" keys. This works around a crash bug deep in macOS system code.
    // This pattern is "State:/Network/Service/[^/]+/EAPOL".
    patterns[4] = ::SCDynamicStoreKeyCreateNetworkServiceEntity(
        nullptr, kSCDynamicStoreDomainState, kSCCompAnyRegex, CFSTR("EAPOL"));
    // This pattern is "State:/Network/Interface/[^/]+/EAPOL".
    patterns[5] = ::SCDynamicStoreKeyCreateNetworkInterfaceEntity(
        nullptr, kSCDynamicStoreDomainState, kSCCompAnyRegex, CFSTR("EAPOL"));
    if (!patterns[0] || !patterns[1] || !patterns[2] || !patterns[3] ||
        !patterns[4] || !patterns[5]) {
      err = -1;
    }
  }

  CFArrayRef patternList = nullptr;
  // Create a pattern list containing just one pattern,
  // then tell SCF that we want to watch changes in keys
  // that match that pattern list, then create our run loop
  // source.
  if (err == noErr) {
    patternList = ::CFArrayCreate(nullptr, (const void**)patterns, 6,
                                  &kCFTypeArrayCallBacks);
    if (!patternList) {
      err = -1;
    }
  }
  if (err == noErr) {
    err = getErrorCodeBool(
        ::SCDynamicStoreSetNotificationKeys(mStoreRef, nullptr, patternList));
  }

  if (err == noErr) {
    mRunLoopSource = ::SCDynamicStoreCreateRunLoopSource(nullptr, mStoreRef, 0);
    err = getErrorCodePtr(mRunLoopSource);
  }

  CFReleaseSafe(patterns[0]);
  CFReleaseSafe(patterns[1]);
  CFReleaseSafe(patterns[2]);
  CFReleaseSafe(patterns[3]);
  CFReleaseSafe(patterns[4]);
  CFReleaseSafe(patterns[5]);
  CFReleaseSafe(patternList);

  if (err != noErr) {
    CFReleaseSafe(mStoreRef);
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Get the current run loop.  This service is initialized at startup,
  // so we shouldn't run in to any problems with modal dialog run loops.
  mCFRunLoop = [[NSRunLoop currentRunLoop] getCFRunLoop];
  if (!mCFRunLoop) {
    NS_WARNING("Could not get current run loop.");
    ::CFRelease(mReachability);
    mReachability = nullptr;
    return NS_ERROR_NOT_AVAILABLE;
  }
  ::CFRetain(mCFRunLoop);

  ::CFRunLoopAddSource(mCFRunLoop, mRunLoopSource, kCFRunLoopDefaultMode);

  if (!::SCNetworkReachabilityScheduleWithRunLoop(mReachability, mCFRunLoop,
                                                  kCFRunLoopDefaultMode)) {
    NS_WARNING("SCNetworkReachabilityScheduleWIthRunLoop failed.");
    ::CFRelease(mReachability);
    mReachability = nullptr;
    ::CFRelease(mCFRunLoop);
    mCFRunLoop = nullptr;
    return NS_ERROR_NOT_AVAILABLE;
  }
  UpdateReachability();

  calculateNetworkIdWithDelay(0);

  DNSConfigChanged(0);

  return NS_OK;
}

nsresult nsNetworkLinkService::Shutdown() {
  if (!::SCNetworkReachabilityUnscheduleFromRunLoop(mReachability, mCFRunLoop,
                                                    kCFRunLoopDefaultMode)) {
    NS_WARNING("SCNetworkReachabilityUnscheduleFromRunLoop failed.");
  }

  CFRunLoopRemoveSource(mCFRunLoop, mRunLoopSource, kCFRunLoopDefaultMode);

  ::CFRelease(mReachability);
  mReachability = nullptr;

  ::CFRelease(mCFRunLoop);
  mCFRunLoop = nullptr;

  ::CFRelease(mStoreRef);
  mStoreRef = nullptr;

  ::CFRelease(mRunLoopSource);
  mRunLoopSource = nullptr;

  if (mNetworkIdTimer) {
    mNetworkIdTimer->Cancel();
    mNetworkIdTimer = nullptr;
  }

  nsTArray<nsCOMPtr<nsITimer>> dnsConfigChangedTimers;
  {
    MutexAutoLock lock(mMutex);
    dnsConfigChangedTimers = std::move(mDNSConfigChangedTimers);
    mDNSConfigChangedTimers.Clear();
  }
  for (const auto& timer : dnsConfigChangedTimers) {
    timer->Cancel();
  }

  return NS_OK;
}

void nsNetworkLinkService::UpdateReachability() {
  if (!mReachability) {
    return;
  }

  SCNetworkConnectionFlags flags;
  if (!::SCNetworkReachabilityGetFlags(mReachability, &flags)) {
    mStatusKnown = false;
    return;
  }

  bool reachable = (flags & kSCNetworkFlagsReachable) != 0;
  bool needsConnection = (flags & kSCNetworkFlagsConnectionRequired) != 0;

  mLinkUp = (reachable && !needsConnection);
  mStatusKnown = true;
}

void nsNetworkLinkService::OnIPConfigChanged() {
  MOZ_ASSERT(NS_IsMainThread());

  calculateNetworkIdWithDelay(kNetworkIdDelayAfterChange);
  if (!StaticPrefs::network_notify_changed()) {
    return;
  }

  NotifyObservers(NS_NETWORK_LINK_TOPIC, NS_NETWORK_LINK_DATA_CHANGED);
}

void nsNetworkLinkService::OnNetworkIdChanged() {
  MOZ_ASSERT(NS_IsMainThread());

  NotifyObservers(NS_NETWORK_ID_CHANGED_TOPIC, nullptr);
}

void nsNetworkLinkService::OnReachabilityChanged() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mStatusKnown) {
    NotifyObservers(NS_NETWORK_LINK_TOPIC, NS_NETWORK_LINK_DATA_UNKNOWN);
    return;
  }

  NotifyObservers(NS_NETWORK_LINK_TOPIC, mLinkUp ? NS_NETWORK_LINK_DATA_UP
                                                 : NS_NETWORK_LINK_DATA_DOWN);
}

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

/* static */
void nsNetworkLinkService::ReachabilityChanged(SCNetworkReachabilityRef target,
                                               SCNetworkConnectionFlags flags,
                                               void* info) {
  LOG(("nsNetworkLinkService::ReachabilityChanged"));
  nsNetworkLinkService* service = static_cast<nsNetworkLinkService*>(info);

  service->UpdateReachability();
  service->OnReachabilityChanged();
  service->calculateNetworkIdWithDelay(kNetworkIdDelayAfterChange);
  // If a new interface is up or the order of interfaces is changed, we should
  // update the DNS suffix list.
  service->DNSConfigChanged(0);
}

// static
bool nsINetworkLinkService::HasNonLocalIPv6Address() {
  return sHasNonLocalIPv6;
}
