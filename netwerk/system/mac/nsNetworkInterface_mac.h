/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNetworkInterface_mac_h
#define nsNetworkInterface_mac_h

#include <utility>

#include "nsNetworkInterface.h"
#include "nsTHashMap.h"

struct ifaddrs;
struct rt_msghdr;
struct sockaddr_dl;
struct sockaddr_in;
struct sockaddr_in6;

// NetworkInterface filled from getifaddrs() and the routing table. It has no
// members of its own, so it can be moved into a NetworkInterface.
class MacNetworkInterface final : public mozilla::net::NetworkInterface {
 public:
  // Builds the interface named aName from aIfap, with its gateways from
  // aRoutes.
  MacNetworkInterface(
      const char* aName,
      const nsTHashMap<nsCString, nsTArray<std::pair<int, nsCString>>>& aRoutes,
      const struct ifaddrs* aIfap);

  using NetworkInterface::SetMAC;
  void SetMAC(const struct sockaddr_dl* aLink);
  void AddIP(const struct sockaddr_in* aAddr);
  void AddIP(const struct sockaddr_in6* aAddr);
};

static_assert(sizeof(MacNetworkInterface) ==
              sizeof(mozilla::net::NetworkInterface));

bool getRoutesForNetworkInterfaces(
    struct rt_msghdr* rtm,
    nsTHashMap<nsCString, nsTArray<std::pair<int, nsCString>>>& ifNameAndIp,
    bool skipDstCheck);

#endif  // !nsNetworkInterface_mac_h
