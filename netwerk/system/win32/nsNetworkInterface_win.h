/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNetworkInterface_win_h
#define nsNetworkInterface_win_h

// clang-format off
#include <winsock2.h>
#include <iphlpapi.h>
// clang-format on

#include "nsNetworkInterface.h"

// NetworkInterface filled from an IP Helper adapter. It has no members of its
// own, so it can be moved into a NetworkInterface.
class WinNetworkInterface final : public mozilla::net::NetworkInterface {
 public:
  explicit WinNetworkInterface(const IP_ADAPTER_ADDRESSES* aAdapter);

  void AddIP(const SOCKET_ADDRESS& aSockAddr);
  void AddGW(const SOCKET_ADDRESS& aSockAddr);
};

static_assert(sizeof(WinNetworkInterface) ==
              sizeof(mozilla::net::NetworkInterface));

#endif  // !nsNetworkInterface_win_h
