/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Drives the Rust Happy Eyeballs engine through its FFI, with no sockets and no
// HappyEyeballsConnectionAttempt in between.
//
// Regression test for bug 2067654: the glue used to hand the engine a
// ThinVec<NetAddr> whose element type was an opaque zero-sized union, so the
// array was walked with a stride of 0 and every slot read the first address.
// The engine dropped the duplicates as already-attempted endpoints, leaving a
// single attempt and no failover to the addresses behind it.

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/net/HappyEyeballs.h"
#include "mozilla/net/happy_eyeballs_glue.h"
#include "nsCOMPtr.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"

namespace mozilla {
namespace net {

namespace {

happy_eyeballs::IpAddr V4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  happy_eyeballs::IpAddr ip{};
  ip.tag = happy_eyeballs::IpAddr::Tag::V4;
  ip.v4._0[0] = a;
  ip.v4._0[1] = b;
  ip.v4._0[2] = c;
  ip.v4._0[3] = d;
  return ip;
}

}  // namespace

TEST(HappyEyeballsNetAddrArray, MultiAddressARecordIsNotCollapsed)
{
  nsTArray<happy_eyeballs::IpAddr> aRecord;
  aRecord.AppendElement(V4(192, 0, 2, 1));
  aRecord.AppendElement(V4(192, 0, 2, 2));
  aRecord.AppendElement(V4(192, 0, 2, 3));

  // Ipv4Only asks for no AAAA, so nothing waits on the resolution delay, and
  // H3 off keeps this to one attempt per address.
  nsAutoCString origin("example.com");
  nsTArray<happy_eyeballs::AltSvc> altSvc;
  happy_eyeballs::HttpVersions versions{/* h1 */ true, /* h2 */ true,
                                        /* h3 */ false};

  // HappyEyeballs is excluded from the cbindgen export, so it lives in
  // mozilla::net rather than in the happy_eyeballs namespace.
  HappyEyeballs* raw = nullptr;
  ASSERT_EQ(happy_eyeballs::happy_eyeballs_create(
                (const HappyEyeballs**)&raw, &origin, 443, &altSvc,
                happy_eyeballs::IpPreference::Ipv4Only, versions),
            NS_OK);
  ASSERT_NE(raw, nullptr);
  // create() hands back an addrefed pointer, and the ASSERTs below return
  // early on failure, so let RefPtr own it.
  RefPtr<HappyEyeballs> he = dont_AddRef(raw);

  // Failing each attempt as it appears walks the address list without timers.
  nsTArray<nsCString> attempted;
  bool done = false;
  for (uint32_t i = 0; i < 32 && !done; ++i) {
    happy_eyeballs::Output event{};
    nsTArray<uint8_t> echConfig;
    nsCString hostname;
    ASSERT_EQ(happy_eyeballs::happy_eyeballs_process_output(
                  he.get(), &event, &echConfig, &hostname),
              NS_OK);

    switch (event.tag) {
      case happy_eyeballs::Output::Tag::SendDnsQuery: {
        const uint64_t id = event.send_dns_query.id;
        if (event.send_dns_query.record_type ==
            happy_eyeballs::DnsRecordType::A) {
          happy_eyeballs::happy_eyeballs_process_dns_response_a(
              he.get(), id, &aRecord, /* is_trr */ false, /* stale */ false);
        } else if (event.send_dns_query.record_type ==
                   happy_eyeballs::DnsRecordType::Aaaa) {
          nsTArray<happy_eyeballs::IpAddr> none;
          happy_eyeballs::happy_eyeballs_process_dns_response_aaaa(
              he.get(), id, &none, /* is_trr */ false, /* stale */ false);
        } else {
          nsTArray<happy_eyeballs::ServiceInfo> none;
          happy_eyeballs::happy_eyeballs_process_dns_response_https(
              he.get(), id, &none, /* is_trr */ false, /* stale */ false);
        }
        break;
      }
      case happy_eyeballs::Output::Tag::AttemptConnection: {
        ASSERT_EQ(event.attempt_connection.addr.tag,
                  happy_eyeballs::IpAddr::Tag::V4);
        const uint8_t* o = event.attempt_connection.addr.v4._0;
        attempted.AppendElement(
            nsCString(nsPrintfCString("%u.%u.%u.%u", o[0], o[1], o[2], o[3])));
        happy_eyeballs::happy_eyeballs_process_connection_result(
            he.get(), event.attempt_connection.id, NS_ERROR_CONNECTION_REFUSED);
        break;
      }
      case happy_eyeballs::Output::Tag::Timer:
      case happy_eyeballs::Output::Tag::CancelConnection:
        break;
      default:
        // None, Succeeded or Failed: nothing further to hand out.
        done = true;
        break;
    }
  }

  attempted.Sort();
  nsAutoCString joined;
  for (const auto& addr : attempted) {
    if (!joined.IsEmpty()) {
      joined.AppendLiteral(" ");
    }
    joined.Append(addr);
  }

  EXPECT_EQ(joined, "192.0.2.1 192.0.2.2 192.0.2.3"_ns);
}

}  // namespace net
}  // namespace mozilla
