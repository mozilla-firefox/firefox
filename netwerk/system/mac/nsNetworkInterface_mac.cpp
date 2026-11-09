/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNetworkInterface_mac.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <algorithm>

#include "mozilla/Logging.h"

// Directly from nsNetworkLinkService.mm
#ifndef SA_SIZE
#  define SA_SIZE(sa)                                    \
    ((!(sa) || ((struct sockaddr*)(sa))->sa_len == 0)    \
         ? sizeof(uint32_t)                              \
         : 1 + ((((struct sockaddr*)(sa))->sa_len - 1) | \
                (sizeof(uint32_t) - 1)))
#endif

static mozilla::LazyLogModule gNotifyAddrLog("nsNotifyAddr");
#define LOG(args) MOZ_LOG(gNotifyAddrLog, mozilla::LogLevel::Debug, args)

namespace {

nsCString GetIP(const struct sockaddr_in* aAddr) {
  char addr[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &aAddr->sin_addr, addr, INET_ADDRSTRLEN);
  return nsCString(addr);
}

nsCString GetIP(const struct sockaddr_in6* aAddr) {
  struct in6_addr in6 = aAddr->sin6_addr;
  // The routing socket embeds the interface index in bytes 2 and 3 of
  // link-local addresses, which are always zero on the wire.
  if (IN6_IS_ADDR_LINKLOCAL(&in6) || IN6_IS_ADDR_MC_LINKLOCAL(&in6)) {
    in6.s6_addr[2] = 0;
    in6.s6_addr[3] = 0;
  }
  char addr[INET6_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET6, &in6, addr, INET6_ADDRSTRLEN);
  return nsCString(addr);
}

}  // namespace

MacNetworkInterface::MacNetworkInterface(
    const char* aName,
    const nsTHashMap<nsCString, nsTArray<std::pair<int, nsCString>>>& aRoutes,
    const struct ifaddrs* aIfap)
    : NetworkInterface(nsDependentCString(aName)) {
  if (const auto* routingEntry = aRoutes.Lookup(Name()).DataPtrOrNull()) {
    for (const auto& ipAddrPair : *routingEntry) {
      if (ipAddrPair.first == AF_INET) {
        AddGWv4(nsCString(ipAddrPair.second));
      } else if (ipAddrPair.first == AF_INET6) {
        AddGWv6(nsCString(ipAddrPair.second));
      }
    }
  }

  for (const struct ifaddrs* ifa = aIfap; ifa; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }

    if (strcmp(aName, ifa->ifa_name) != 0) {
      continue;
    }

    if (AF_INET6 == ifa->ifa_addr->sa_family) {
      AddIP((struct sockaddr_in6*)ifa->ifa_addr);
    } else if (AF_INET == ifa->ifa_addr->sa_family) {
      AddIP((struct sockaddr_in*)ifa->ifa_addr);
    }

    if (AF_LINK == ifa->ifa_addr->sa_family) {
      SetMAC((struct sockaddr_dl*)ifa->ifa_addr);
    }
  }
}

void MacNetworkInterface::SetMAC(const struct sockaddr_dl* aLink) {
  uint8_t mac_addr[6] = {0};
  memcpy(mac_addr, LLADDR(aLink),
         std::min<size_t>(aLink->sdl_alen, sizeof(mac_addr)));
  SetMAC(mac_addr);
}

void MacNetworkInterface::AddIP(const struct sockaddr_in* aAddr) {
  AddIPv4(GetIP(aAddr));
}

void MacNetworkInterface::AddIP(const struct sockaddr_in6* aAddr) {
  AddIPv6(GetIP(aAddr));
}

bool getRoutesForNetworkInterfaces(
    struct rt_msghdr* rtm,
    nsTHashMap<nsCString, nsTArray<std::pair<int, nsCString>>>& ifNameAndIp,
    bool skipDstCheck) {
  struct sockaddr* sa;
  if ((rtm->rtm_addrs & (RTA_DST | RTA_GATEWAY)) != (RTA_DST | RTA_GATEWAY)) {
    return false;
  }

  sa = reinterpret_cast<struct sockaddr*>(rtm + 1);

  struct sockaddr* destination =
      reinterpret_cast<struct sockaddr*>((char*)sa + RTAX_DST * SA_SIZE(sa));

  if (destination->sa_family != AF_INET && destination->sa_family != AF_INET6) {
    return false;
  }

  if (!skipDstCheck) {
    bool isDefault =
        destination->sa_family == AF_INET
            ? reinterpret_cast<const struct sockaddr_in*>(destination)
                      ->sin_addr.s_addr == INADDR_ANY
            : IN6_IS_ADDR_UNSPECIFIED(
                  &reinterpret_cast<const struct sockaddr_in6*>(destination)
                       ->sin6_addr);
    if (!isDefault) {
      return false;
    }
  }

  struct sockaddr* gateway = reinterpret_cast<struct sockaddr*>(
      (char*)sa + RTAX_GATEWAY * SA_SIZE(sa));

  if (gateway->sa_family != AF_INET && gateway->sa_family != AF_INET6) {
    return false;
  }

  nsCString ipAddr;
  if (gateway->sa_family == AF_INET) {
    ipAddr = GetIP(reinterpret_cast<const struct sockaddr_in*>(gateway));
  } else if (gateway->sa_family == AF_INET6) {
    ipAddr = GetIP(reinterpret_cast<const struct sockaddr_in6*>(gateway));
  }

  char buf[IFNAMSIZ] = {0};
  char* if_name = if_indextoname(rtm->rtm_index, buf);
  if (!if_name) {
    LOG(("getRoutes: AF_INET if_indextoname failed"));
    return false;
  }

  nsCString ifName = nsCString(if_name);
  std::pair<int, nsCString> ipAddrPair =
      std::make_pair(gateway->sa_family, ipAddr);

  auto& ifNameEntry = ifNameAndIp.LookupOrInsert(ifName);
  LOG(("getRoutes: ifNameEntry for %s", ifName.get()));
  if (!ifNameEntry.Contains(ipAddrPair)) {
    LOG(("getRoutes: ifNameEntry for %s does not contain %s, adding",
         ifName.get(), ipAddr.get()));
    ifNameEntry.AppendElement(ipAddrPair);
  }

  return true;
}
