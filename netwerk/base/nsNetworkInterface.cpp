/* vim: et ts=2 sw=2 tw=80
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNetworkInterface.h"

#include "nsPrintfCString.h"
#include "nsXULAppAPI.h"

namespace mozilla::net {

NetworkInterface::NetworkInterface(const nsACString& aName) : mName(aName) {}

void NetworkInterface::SetMAC(const uint8_t* aAddr) {
  mMAC = nsPrintfCString("%02x:%02x:%02x:%02x:%02x:%02x", aAddr[0], aAddr[1],
                         aAddr[2], aAddr[3], aAddr[4], aAddr[5]);
}

NS_IMPL_ISUPPORTS(nsNetworkInterface, nsINetworkInterface)

// static
nsresult nsNetworkInterface::GetNetworkInterfaces(
    const nsTArray<NetworkInterface>& aInterfaces,
    nsTArray<RefPtr<nsINetworkInterface>>& aNetworkInterfaces) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  for (const auto& intf : aInterfaces) {
    aNetworkInterfaces.AppendElement(
        mozilla::MakeRefPtr<nsNetworkInterface>(NetworkInterface(intf)));
  }
  return NS_OK;
}

NS_IMETHODIMP nsNetworkInterface::GetName(nsACString& aName) {
  aName = mIntf.Name();
  return NS_OK;
}

NS_IMETHODIMP nsNetworkInterface::GetMac(nsACString& aMac) {
  aMac = mIntf.Mac();
  return NS_OK;
}

NS_IMETHODIMP nsNetworkInterface::GetGwv4(nsTArray<nsCString>& aGwv4) {
  aGwv4 = mIntf.Gwv4().Clone();
  return NS_OK;
}

NS_IMETHODIMP nsNetworkInterface::GetGwv6(nsTArray<nsCString>& aGwv6) {
  aGwv6 = mIntf.Gwv6().Clone();
  return NS_OK;
}

NS_IMETHODIMP nsNetworkInterface::GetIpv4(nsTArray<nsCString>& aIpv4) {
  aIpv4 = mIntf.Ipv4().Clone();
  return NS_OK;
}

NS_IMETHODIMP nsNetworkInterface::GetIpv6(nsTArray<nsCString>& aIpv6) {
  aIpv6 = mIntf.Ipv6().Clone();
  return NS_OK;
}

}  // namespace mozilla::net
