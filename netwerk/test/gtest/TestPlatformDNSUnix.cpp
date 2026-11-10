/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <netinet/in.h>
#include <resolv.h>

#include "GetAddrInfo.h"
#include "gtest/gtest.h"

using namespace mozilla::net;

#ifdef HAVE_RES_NINIT

// The per-thread resolver state caches the nameservers from resolv.conf, so it
// must be reloaded after a network change.
TEST(TestPlatformDNSUnix, ResStateReinitializedOnNetworkChange)
{
  struct __res_state* res = GetThreadResState();
  ASSERT_TRUE(res);

  // A value res_ninit never produces, to detect whether it ran again.
  res->retry = 99;
  res = GetThreadResState();
  ASSERT_TRUE(res);
  EXPECT_EQ(res->retry, 99);

  DNSNetworkChanged();
  res = GetThreadResState();
  ASSERT_TRUE(res);
  EXPECT_NE(res->retry, 99);

  DNSThreadShutdown();
}

#endif
