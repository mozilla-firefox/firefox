/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNetworkInterface_linux_h
#define nsNetworkInterface_linux_h

#include <netinet/in.h>

#include "nsNetworkInterface.h"

// NetworkInterface filled from Netlink addresses. It has no members of its own,
// so it can be moved into a NetworkInterface.
class LinuxNetworkInterface final : public mozilla::net::NetworkInterface {
 public:
  using NetworkInterface::NetworkInterface;

  void AddIP(const in_addr* aAddr);
  void AddIP(const in6_addr* aAddr);
  void AddGW(const in_addr* aAddr);
  void AddGW(const in6_addr* aAddr);
};

static_assert(sizeof(LinuxNetworkInterface) ==
              sizeof(mozilla::net::NetworkInterface));

#endif  // !nsNetworkInterface_linux_h
