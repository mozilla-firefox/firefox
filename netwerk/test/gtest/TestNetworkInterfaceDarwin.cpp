/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "gtest/gtest.h"
#include "nsNetworkInterface_mac.h"

namespace {

using RoutesMap = nsTHashMap<nsCString, nsTArray<std::pair<int, nsCString>>>;

sockaddr_in IPv4(const char* aAddr) {
  sockaddr_in sin = {};
  sin.sin_len = sizeof(sin);
  sin.sin_family = AF_INET;
  inet_pton(AF_INET, aAddr, &sin.sin_addr);
  return sin;
}

sockaddr_in6 IPv6(const char* aAddr) {
  sockaddr_in6 sin6 = {};
  sin6.sin6_len = sizeof(sin6);
  sin6.sin6_family = AF_INET6;
  inet_pton(AF_INET6, aAddr, &sin6.sin6_addr);
  return sin6;
}

// A routing message for lo0 laid out like the kernel's: the header followed by
// the destination and gateway sockaddrs, each padded to 4 bytes.
class RouteMessage {
 public:
  RouteMessage(const void* aDst, const void* aGw,
               int aAddrs = RTA_DST | RTA_GATEWAY) {
    rt_msghdr* rtm = Header();
    rtm->rtm_addrs = aAddrs;
    rtm->rtm_index = if_nametoindex("lo0");

    char* next = reinterpret_cast<char*>(rtm + 1);
    for (const void* addr : {aDst, aGw}) {
      uint8_t len = static_cast<const sockaddr*>(addr)->sa_len;
      memcpy(next, addr, len);
      next += 1 + ((len - 1) | (sizeof(uint32_t) - 1));
    }
  }

  rt_msghdr* Header() { return reinterpret_cast<rt_msghdr*>(mBuffer); }

 private:
  alignas(rt_msghdr) char mBuffer[sizeof(rt_msghdr) +
                                  2 * sizeof(sockaddr_storage)] = {};
};

void ExpectRoute(RoutesMap& aRoutes, int aFamily, const char* aGateway) {
  const auto* entry = aRoutes.Lookup("lo0"_ns).DataPtrOrNull();
  ASSERT_TRUE(entry);
  ASSERT_EQ(entry->Length(), 1u);
  EXPECT_EQ((*entry)[0].first, aFamily);
  EXPECT_TRUE((*entry)[0].second.EqualsASCII(aGateway))
      << (*entry)[0].second.get();
}

}  // namespace

TEST(TestNetworkInterfaceDarwin, IPv4DefaultRoute)
{
  auto dst = IPv4("0.0.0.0");
  auto gw = IPv4("192.0.2.1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  ExpectRoute(routes, AF_INET, "192.0.2.1");
}

TEST(TestNetworkInterfaceDarwin, IPv6DefaultRoute)
{
  auto dst = IPv6("::");
  auto gw = IPv6("2001:db8::1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  ExpectRoute(routes, AF_INET6, "2001:db8::1");
}

TEST(TestNetworkInterfaceDarwin, IPv4RouteViaIPv6Gateway)
{
  auto dst = IPv4("0.0.0.0");
  auto gw = IPv6("2001:db8::1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  ExpectRoute(routes, AF_INET6, "2001:db8::1");

  MacNetworkInterface intf("lo0", routes, nullptr);
  EXPECT_TRUE(intf.Gwv4().IsEmpty());
  ASSERT_EQ(intf.Gwv6().Length(), 1u);
  EXPECT_TRUE(intf.Gwv6()[0].EqualsLiteral("2001:db8::1"));
}

TEST(TestNetworkInterfaceDarwin, IPv6RouteViaIPv4Gateway)
{
  auto dst = IPv6("::");
  auto gw = IPv4("192.0.2.1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  ExpectRoute(routes, AF_INET, "192.0.2.1");

  MacNetworkInterface intf("lo0", routes, nullptr);
  EXPECT_TRUE(intf.Gwv6().IsEmpty());
  ASSERT_EQ(intf.Gwv4().Length(), 1u);
  EXPECT_TRUE(intf.Gwv4()[0].EqualsLiteral("192.0.2.1"));
}

TEST(TestNetworkInterfaceDarwin, LinkLocalGatewayScopeIsCleared)
{
  auto dst = IPv6("::");
  // The routing socket embeds the interface index in bytes 2 and 3.
  auto gw = IPv6("fe80:b::1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  ExpectRoute(routes, AF_INET6, "fe80::1");
}

TEST(TestNetworkInterfaceDarwin, NonDefaultRoute)
{
  auto dst = IPv4("198.51.100.0");
  auto gw = IPv4("192.0.2.1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_FALSE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  EXPECT_FALSE(routes.Contains("lo0"_ns));

  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, true));
  ExpectRoute(routes, AF_INET, "192.0.2.1");
}

TEST(TestNetworkInterfaceDarwin, LinkGatewayIsIgnored)
{
  auto dst = IPv4("0.0.0.0");
  sockaddr_dl gw = {};
  gw.sdl_len = sizeof(gw);
  gw.sdl_family = AF_LINK;
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_FALSE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  EXPECT_FALSE(routes.Contains("lo0"_ns));
}

TEST(TestNetworkInterfaceDarwin, MissingGatewayIsIgnored)
{
  auto dst = IPv4("0.0.0.0");
  auto gw = IPv4("192.0.2.1");
  RouteMessage msg(&dst, &gw, RTA_DST);
  RoutesMap routes;
  EXPECT_FALSE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  EXPECT_FALSE(routes.Contains("lo0"_ns));
}

TEST(TestNetworkInterfaceDarwin, DuplicateRouteIsAddedOnce)
{
  auto dst = IPv4("0.0.0.0");
  auto gw = IPv4("192.0.2.1");
  RouteMessage msg(&dst, &gw);
  RoutesMap routes;
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  EXPECT_TRUE(getRoutesForNetworkInterfaces(msg.Header(), routes, false));
  ExpectRoute(routes, AF_INET, "192.0.2.1");
}

namespace {

// Returns the MAC of a lo0 interface whose link-layer address is aAddr, after
// a link name of aNameLen bytes.
nsCString MacFromLinkAddress(std::initializer_list<uint8_t> aAddr,
                             uint8_t aNameLen = 0) {
  sockaddr_dl link = {};
  link.sdl_len = sizeof(link);
  link.sdl_family = AF_LINK;
  link.sdl_nlen = aNameLen;
  link.sdl_alen = aAddr.size();
  memset(link.sdl_data, 'x', aNameLen);
  memcpy(LLADDR(&link), aAddr.begin(), aAddr.size());

  ifaddrs ifa = {};
  ifa.ifa_name = const_cast<char*>("lo0");
  ifa.ifa_addr = reinterpret_cast<sockaddr*>(&link);

  RoutesMap routes;
  return MacNetworkInterface("lo0", routes, &ifa).Mac();
}

}  // namespace

TEST(TestNetworkInterfaceDarwin, EthernetLinkAddress)
{
  EXPECT_STREQ(
      MacFromLinkAddress({0x02, 0x00, 0x5e, 0x10, 0x00, 0x01}, 3).get(),
      "02:00:5e:10:00:01");
}

TEST(TestNetworkInterfaceDarwin, EmptyLinkAddress)
{
  EXPECT_STREQ(MacFromLinkAddress({}).get(), "00:00:00:00:00:00");
}

TEST(TestNetworkInterfaceDarwin, ShortLinkAddressIsZeroPadded)
{
  EXPECT_STREQ(MacFromLinkAddress({0xab, 0xcd}).get(), "ab:cd:00:00:00:00");
}

TEST(TestNetworkInterfaceDarwin, LongLinkAddressIsTruncated)
{
  EXPECT_STREQ(
      MacFromLinkAddress({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08})
          .get(),
      "01:02:03:04:05:06");
}
