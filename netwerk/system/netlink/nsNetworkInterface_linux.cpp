/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNetworkInterface_linux.h"

#include <arpa/inet.h>

namespace {

nsCString GetIP(const in_addr* aAddr) {
  char addr[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, aAddr, addr, INET_ADDRSTRLEN);
  return nsCString(addr);
}

nsCString GetIP(const in6_addr* aAddr) {
  char addr[INET6_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET6, aAddr, addr, INET6_ADDRSTRLEN);
  return nsCString(addr);
}

}  // namespace

void LinuxNetworkInterface::AddIP(const in_addr* aAddr) {
  AddIPv4(GetIP(aAddr));
}

void LinuxNetworkInterface::AddIP(const in6_addr* aAddr) {
  AddIPv6(GetIP(aAddr));
}

void LinuxNetworkInterface::AddGW(const in_addr* aAddr) {
  AddGWv4(GetIP(aAddr));
}

void LinuxNetworkInterface::AddGW(const in6_addr* aAddr) {
  AddGWv6(GetIP(aAddr));
}
