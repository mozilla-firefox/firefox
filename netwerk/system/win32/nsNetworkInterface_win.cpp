/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNetworkInterface_win.h"

#include <ws2tcpip.h>

namespace {

// Sets aIp to the textual form of aSockAddr and returns its address family,
// or AF_UNSPEC if it is neither IPv4 nor IPv6.
int GetIP(const SOCKET_ADDRESS& aSockAddr, nsCString& aIp) {
  const sockaddr* addr = aSockAddr.lpSockaddr;
  char buf[INET6_ADDRSTRLEN] = {0};
  if (addr->sa_family == AF_INET) {
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(addr)->sin_addr,
              buf, sizeof(buf));
  } else if (addr->sa_family == AF_INET6) {
    inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr,
              buf, sizeof(buf));
  } else {
    return AF_UNSPEC;
  }
  aIp.Assign(buf);
  return addr->sa_family;
}

}  // namespace

WinNetworkInterface::WinNetworkInterface(const IP_ADAPTER_ADDRESSES* aAdapter)
    : NetworkInterface(NS_ConvertUTF16toUTF8(aAdapter->FriendlyName)) {
  uint8_t macAddress[MAX_ADAPTER_ADDRESS_LENGTH] = {0};
  if (aAdapter->PhysicalAddressLength != 0) {
    memcpy(&macAddress, aAdapter->PhysicalAddress,
           aAdapter->PhysicalAddressLength);
  }
  SetMAC(macAddress);

  for (const IP_ADAPTER_UNICAST_ADDRESS* address =
           aAdapter->FirstUnicastAddress;
       address; address = address->Next) {
    AddIP(address->Address);
  }

  for (const IP_ADAPTER_GATEWAY_ADDRESS* gateway =
           aAdapter->FirstGatewayAddress;
       gateway; gateway = gateway->Next) {
    AddGW(gateway->Address);
  }
}

void WinNetworkInterface::AddIP(const SOCKET_ADDRESS& aSockAddr) {
  nsCString ip;
  int family = GetIP(aSockAddr, ip);
  if (family == AF_INET) {
    AddIPv4(std::move(ip));
  } else if (family == AF_INET6) {
    AddIPv6(std::move(ip));
  }
}

void WinNetworkInterface::AddGW(const SOCKET_ADDRESS& aSockAddr) {
  nsCString ip;
  int family = GetIP(aSockAddr, ip);
  if (family == AF_INET) {
    AddGWv4(std::move(ip));
  } else if (family == AF_INET6) {
    AddGWv6(std::move(ip));
  }
}
