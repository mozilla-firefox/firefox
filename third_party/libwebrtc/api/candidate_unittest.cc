/*
 *  Copyright 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/candidate.h"

#include <string>

#include "p2p/base/p2p_constants.h"
#include "rtc_base/gunit.h"

using webrtc::IceCandidateType;

namespace cricket {

TEST(CandidateTest, Id) {
  Candidate c;
  EXPECT_EQ(c.id().size(), 8u);
  std::string current_id = c.id();
  // Generate a new ID.
  c.generate_id();
  EXPECT_EQ(c.id().size(), 8u);
  EXPECT_NE(current_id, c.id());
}

TEST(CandidateTest, Component) {
  Candidate c;
  EXPECT_EQ(c.component(), ICE_CANDIDATE_COMPONENT_DEFAULT);
  c.set_component(ICE_CANDIDATE_COMPONENT_RTCP);
  EXPECT_EQ(c.component(), ICE_CANDIDATE_COMPONENT_RTCP);
}

TEST(CandidateTest, TypeName) {
  Candidate c;
  // The `type_name()` property defaults to "host".
  EXPECT_EQ(c.type_name(), "host");
  EXPECT_EQ(c.type(), IceCandidateType::kHost);

  c.set_type(IceCandidateType::kSrflx);
  EXPECT_EQ(c.type_name(), "srflx");
  EXPECT_EQ(c.type(), IceCandidateType::kSrflx);

  c.set_type(IceCandidateType::kPrflx);
  EXPECT_EQ(c.type_name(), "prflx");
  EXPECT_EQ(c.type(), IceCandidateType::kPrflx);

  c.set_type(IceCandidateType::kRelay);
  EXPECT_EQ(c.type_name(), "relay");
  EXPECT_EQ(c.type(), IceCandidateType::kRelay);
}

TEST(CandidateTest, Foundation) {
  Candidate c;
  EXPECT_TRUE(c.foundation().empty());
  c.set_protocol("udp");
  c.set_relay_protocol("udp");

  rtc::SocketAddress address("99.99.98.1", 1024);
  c.set_address(address);
  c.ComputeFoundation(c.address(), 1);
  std::string foundation1 = c.foundation();
  EXPECT_FALSE(foundation1.empty());

  // Change the tiebreaker.
  c.ComputeFoundation(c.address(), 2);
  std::string foundation2 = c.foundation();
  EXPECT_NE(foundation1, foundation2);

  // Provide a different base address.
  address.SetIP("100.100.100.1");
  c.ComputeFoundation(address, 1);  // Same tiebreaker as for foundation1.
  foundation2 = c.foundation();
  EXPECT_NE(foundation1, foundation2);

  // Consistency check (just in case the algorithm ever changes to random!).
  c.ComputeFoundation(c.address(), 1);
  foundation2 = c.foundation();
  EXPECT_EQ(foundation1, foundation2);

  // Changing the protocol should affect the foundation.
  auto prev_protocol = c.protocol();
  c.set_protocol("tcp");
  ASSERT_NE(prev_protocol, c.protocol());
  c.ComputeFoundation(c.address(), 1);
  EXPECT_NE(foundation1, c.foundation());
  c.set_protocol(prev_protocol);

  // Changing the relay protocol should affect the foundation.
  prev_protocol = c.relay_protocol();
  c.set_relay_protocol("tcp");
  ASSERT_NE(prev_protocol, c.relay_protocol());
  c.ComputeFoundation(c.address(), 1);
  EXPECT_NE(foundation1, c.foundation());
}

}  // namespace cricket
