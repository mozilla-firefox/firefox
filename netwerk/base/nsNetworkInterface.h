/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNetworkInterface_h
#define nsNetworkInterface_h

#include "mozilla/RefPtr.h"
#include "nsINetworkInterface.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::net {

class NetworkInterface {
 public:
  explicit NetworkInterface(const nsACString& aName);
  NetworkInterface(const NetworkInterface& aIntf) = default;
  NetworkInterface(NetworkInterface&& aIntf) = default;

  const nsCString& Name() const { return mName; }
  const nsCString& Mac() const { return mMAC; }

  const nsTArray<nsCString>& Gwv4() const { return mGwv4; }
  const nsTArray<nsCString>& Gwv6() const { return mGwv6; }
  const nsTArray<nsCString>& Ipv4() const { return mIpv4; }
  const nsTArray<nsCString>& Ipv6() const { return mIpv6; }

  // Formats the first six bytes of aAddr as the MAC address.
  void SetMAC(const uint8_t* aAddr);

  void AddIPv4(nsCString&& aIp) { mIpv4.AppendElement(std::move(aIp)); }
  void AddIPv6(nsCString&& aIp) { mIpv6.AppendElement(std::move(aIp)); }
  void AddGWv4(nsCString&& aIp) { mGwv4.AppendElement(std::move(aIp)); }
  void AddGWv6(nsCString&& aIp) { mGwv6.AppendElement(std::move(aIp)); }

 private:
  CopyableTArray<nsCString> mGwv4;
  CopyableTArray<nsCString> mGwv6;
  CopyableTArray<nsCString> mIpv4;
  CopyableTArray<nsCString> mIpv6;

  nsCString mName;
  nsCString mMAC;
};

class nsNetworkInterface final : public nsINetworkInterface {
  ~nsNetworkInterface() = default;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSINETWORKINTERFACE

  explicit nsNetworkInterface(NetworkInterface&& aIntf)
      : mIntf(std::move(aIntf)) {}

  // Shared implementation of nsINetworkLinkService::GetNetworkInterfaces():
  // appends an nsINetworkInterface wrapping a copy of each element of
  // aInterfaces to aNetworkInterfaces. Only available in the parent process.
  // The caller is responsible for holding whatever lock guards aInterfaces.
  static nsresult GetNetworkInterfaces(
      const nsTArray<NetworkInterface>& aInterfaces,
      nsTArray<RefPtr<nsINetworkInterface>>& aNetworkInterfaces);

 private:
  NetworkInterface mIntf;
};

}  // namespace mozilla::net

#endif  // !nsNetworkInterface_h
