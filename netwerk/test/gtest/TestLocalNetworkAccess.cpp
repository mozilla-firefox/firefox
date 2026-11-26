/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TestCommon.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/net/DNS.h"
#include "nsIOService.h"
#include "nsNetUtil.h"

TEST(TestNetAddrLNAUtil, IPAddressSpaceCategorization)
{
  /*--------------------------------------------------------------------------*
 | Network              | Description            | RFC       | Scope   |
 |----------------------|------------------------|-----------|------------------|
 127.0.0.0/8            | IPv4 Loopback          | RFC1122   | local   |
 10.0.0.0/8             | Private Use            | RFC1918   | private |
 100.64.0.0/10          | Carrier-Grade NAT      | RFC6598   | private |
 172.16.0.0/12          | Private Use            | RFC1918   | private |
 192.168.0.0/16         | Private Use            | RFC1918   | private |
 198.18.0.0/15          | Benchmarking           | RFC2544   | pref/local
 169.254.0.0/16         | Link Local             | RFC3927   | private |
 ::1/128                | IPv6 Loopback          | RFC4291   | local   |
 fc00::/7               | Unique Local           | RFC4193   | private |
 fe80::/10              | Link-Local Unicast     | RFC4291   | private |
 ::ffff:0:0/96          | IPv4-mapped            | RFC4291   | IPv4-mapped
 address space |
 *--------------------------------------------------------------------------*/
  using namespace mozilla::net;
  using namespace mozilla;

  struct TestCase {
    const char* mIp;
    nsILoadInfo::IPAddressSpace mExpectedSpace;
  };

  std::vector<TestCase> testCases = {
      // Local IPv4
      {"", nsILoadInfo::IPAddressSpace::Unknown},
      {"127.0.0.1", nsILoadInfo::IPAddressSpace::Local},
      {"198.18.0.0", StaticPrefs::network_lna_benchmarking_is_local()
                         ? nsILoadInfo::IPAddressSpace::Local
                         : nsILoadInfo::IPAddressSpace::Public},
      {"198.19.255.255", StaticPrefs::network_lna_benchmarking_is_local()
                             ? nsILoadInfo::IPAddressSpace::Local
                             : nsILoadInfo::IPAddressSpace::Public},

      // Private IPv4
      {"10.0.0.1", nsILoadInfo::IPAddressSpace::Private},
      {"100.64.0.1", nsILoadInfo::IPAddressSpace::Private},
      {"100.127.255.254", nsILoadInfo::IPAddressSpace::Private},
      {"172.16.0.1", nsILoadInfo::IPAddressSpace::Private},
      {"172.31.255.255", nsILoadInfo::IPAddressSpace::Private},
      {"192.168.1.1", nsILoadInfo::IPAddressSpace::Private},
      {"169.254.0.1", nsILoadInfo::IPAddressSpace::Private},
      {"169.254.255.254", nsILoadInfo::IPAddressSpace::Private},

      // IPv6 Local and Private
      {"::1", nsILoadInfo::IPAddressSpace::Local},       // Loopback
      {"fc00::", nsILoadInfo::IPAddressSpace::Private},  // Unique Local
      {"fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
       nsILoadInfo::IPAddressSpace::Private},
      {"fe80::1", nsILoadInfo::IPAddressSpace::Private},  // Link-local

      // IPv4-mapped IPv6 (mapped IPv4, should fall back to IPv4 classification)
      {"::ffff:127.0.0.1", nsILoadInfo::IPAddressSpace::Local},  // Loopback
      {"::ffff:127.0.0.2", nsILoadInfo::IPAddressSpace::Local},
      {"::ffff:127.255.255.254", nsILoadInfo::IPAddressSpace::Local},
      {"::ffff:10.0.0.1", nsILoadInfo::IPAddressSpace::Private},  // Private
      {"::ffff:100.64.0.1", nsILoadInfo::IPAddressSpace::Private},
      {"::ffff:100.127.255.254", nsILoadInfo::IPAddressSpace::Private},
      {"::ffff:100.128.0.1", nsILoadInfo::IPAddressSpace::Public},
      {"::ffff:1.1.1.1", nsILoadInfo::IPAddressSpace::Public},  // Public
      {"::ffff:198.18.0.0", StaticPrefs::network_lna_benchmarking_is_local()
                                ? nsILoadInfo::IPAddressSpace::Local
                                : nsILoadInfo::IPAddressSpace::Public},

      // Public IPv4
      {"8.8.8.8", nsILoadInfo::IPAddressSpace::Public},
      {"1.1.1.1", nsILoadInfo::IPAddressSpace::Public},

      // Public IPv6
      {"2001:4860:4860::8888", nsILoadInfo::IPAddressSpace::Public},
      {"2606:4700:4700::1111", nsILoadInfo::IPAddressSpace::Public}};

  for (const auto& testCase : testCases) {
    NetAddr addr;
    addr.InitFromString(nsCString(testCase.mIp));
    if (addr.raw.family == AF_INET) {
      EXPECT_EQ(addr.GetIpAddressSpace(), testCase.mExpectedSpace)
          << "Failed for IP: " << testCase.mIp;
    } else if (addr.raw.family == AF_INET6) {
      EXPECT_EQ(addr.GetIpAddressSpace(), testCase.mExpectedSpace)
          << "Failed for IP: " << testCase.mIp;
    } else {
      EXPECT_EQ(addr.GetIpAddressSpace(), nsILoadInfo::IPAddressSpace::Unknown)
          << "Failed for IP: " << testCase.mIp;
    }
  }
}

TEST(TestNetAddrLNAUtil, DefaultAndOverrideTransitions)
{
  using mozilla::Preferences;
  using mozilla::net::NetAddr;
  using IPAddressSpace = nsILoadInfo::IPAddressSpace;
  struct TestCase {
    const char* ip;
    uint16_t port;
    IPAddressSpace defaultSpace;
    IPAddressSpace overrideSpace;
    const char* prefName;
  };

  std::vector<TestCase> testCases = {
      // Public -> Private
      {"8.8.8.8", 80, IPAddressSpace::Public, IPAddressSpace::Private,
       "network.lna.address_space.private.override"},

      // Public -> Local
      {"8.8.4.4", 53, IPAddressSpace::Public, IPAddressSpace::Local,
       "network.lna.address_space.local.override"},

      // Private -> Public
      {"192.168.0.1", 8080, IPAddressSpace::Private, IPAddressSpace::Public,
       "network.lna.address_space.public.override"},

      // Private -> Local
      {"10.0.0.1", 1234, IPAddressSpace::Private, IPAddressSpace::Local,
       "network.lna.address_space.local.override"},

      // Local -> Public
      {"127.0.0.1", 4444, IPAddressSpace::Local, IPAddressSpace::Public,
       "network.lna.address_space.public.override"},

      // Local -> Private
      {"127.0.0.1", 9999, IPAddressSpace::Local, IPAddressSpace::Private,
       "network.lna.address_space.private.override"},
  };

  for (const auto& tc : testCases) {
    NetAddr addr;
    addr.InitFromString(nsCString(tc.ip), tc.port);
    ASSERT_EQ(addr.GetIpAddressSpace(), tc.defaultSpace)
        << "Expected default space for " << tc.ip << ":" << tc.port;

    std::string overrideStr =
        std::string(tc.ip) + ":" + std::to_string(tc.port);
    Preferences::SetCString(tc.prefName, overrideStr.c_str());

    NetAddr overriddenAddr;
    overriddenAddr.InitFromString(nsCString(tc.ip), tc.port);
    ASSERT_EQ(overriddenAddr.GetIpAddressSpace(), tc.overrideSpace)
        << "Expected override to " << tc.overrideSpace << " for "
        << overrideStr;

    // Reset preference and confirm classification returns to default
    Preferences::SetCString(tc.prefName, ""_ns);
    NetAddr resetAddr;
    resetAddr.InitFromString(nsCString(tc.ip), tc.port);
    ASSERT_EQ(resetAddr.GetIpAddressSpace(), tc.defaultSpace)
        << "Expected reset back to default space for " << tc.ip;
  }
}

TEST(TestNetAddrLNAUtil, ShouldSkipDomainForLNA)
{
  using mozilla::Preferences;
  // Get nsIOService instance
  mozilla::net::nsIOService* ioService = mozilla::net::gIOService;
  ASSERT_NE(ioService, nullptr);

  // Test with empty preference (should not skip any domains)
  Preferences::SetCString("network.lna.skip-domains", ""_ns);
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("example.com"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("test.example.com"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("localhost"_ns));

  // Test exact domain matching
  Preferences::SetCString("network.lna.skip-domains",
                          "example.com,test.org"_ns);
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("example.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("test.org"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("sub.example.com"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("example.org"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("notexample.com"_ns));

  // Test wildcard domain matching
  Preferences::SetCString("network.lna.skip-domains",
                          "*.example.com,*.test.org"_ns);
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("sub.example.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("deep.sub.example.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("api.test.org"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "example.com"_ns));  // Should match exact domain too
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "test.org"_ns));  // Should match exact domain too
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("example.net"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("notexample.com"_ns));

  // Test more suffix wildcard patterns
  Preferences::SetCString("network.lna.skip-domains",
                          "*.local,*.internal,*.test"_ns);
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("server.local"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("api.internal"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("service.test"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("deep.subdomain.local"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "local"_ns));  // Should match exact domain too
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "internal"_ns));  // Should match exact domain too
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("local.example.com"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("localhost"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("example.com"_ns));

  // Test mixed patterns (exact and suffix wildcard)
  Preferences::SetCString(
      "network.lna.skip-domains",
      "localhost,*.dev.local,*.staging.com,production.example.com"_ns);
  // Exact matches
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("localhost"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("production.example.com"_ns));
  // Suffix wildcard matches
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("api.dev.local"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("web.dev.local"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("dev.local"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("test.staging.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("api.staging.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("staging.com"_ns));
  // Non-matches
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("example.com"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("dev.example.com"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("staging.example.com"_ns));

  // Test with whitespace and empty entries
  Preferences::SetCString(
      "network.lna.skip-domains",
      " example.com , , *.test.local , admin.internal  "_ns);
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("example.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("api.test.local"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("admin.internal"_ns));
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("test.com"_ns));

  // Test invalid patterns (unknown patterns treated as exact match)
  Preferences::SetCString("network.lna.skip-domains",
                          "example.com,invalid.pattern"_ns);
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "example.com"_ns));  // Valid exact match
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "invalid.pattern"_ns));  // Treated as exact match
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA("test.com"_ns));  // No match

  // Test case sensitivity
  Preferences::SetCString("network.lna.skip-domains",
                          "Example.COM,*.Test.ORG"_ns);
  EXPECT_TRUE(
      ioService->ShouldSkipDomainForLNA("Example.COM"_ns));  // Exact case match
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA(
      "example.com"_ns));  // Different case (no match)
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA(
      "api.Test.ORG"_ns));  // Wildcard case match
  EXPECT_FALSE(ioService->ShouldSkipDomainForLNA(
      "api.test.org"_ns));  // Different case (no match)

  // Test plain "*" matches all domains
  Preferences::SetCString("network.lna.skip-domains", "*"_ns);
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("example.com"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("test.org"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("localhost"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("any.domain.here"_ns));
  EXPECT_TRUE(ioService->ShouldSkipDomainForLNA("server.local"_ns));

  // Reset preference for cleanup
  Preferences::SetCString("network.lna.skip-domains", ""_ns);
}
