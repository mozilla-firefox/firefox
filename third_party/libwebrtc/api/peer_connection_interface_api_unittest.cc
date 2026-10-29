/*
 *  Copyright 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Tests for functions solely defined in the PeerConnectionInterface.

#include "absl/strings/str_cat.h"
#include "api/peer_connection_interface.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

TEST(Stringify, SignalingState) {
  PeerConnectionInterface::SignalingState state =
      PeerConnectionInterface::kHaveLocalOffer;
  EXPECT_EQ(PeerConnectionInterface::AsString(state), "have-local-offer");
  EXPECT_EQ(absl::StrCat(state), "have-local-offer");
}

TEST(Stringify, IceGatheringState) {
  PeerConnectionInterface::IceGatheringState state =
      PeerConnectionInterface::kIceGatheringComplete;
  EXPECT_EQ(PeerConnectionInterface::AsString(state), "complete");
  EXPECT_EQ(absl::StrCat(state), "complete");
}

TEST(Stringify, PeerConnectionState) {
  PeerConnectionInterface::PeerConnectionState state =
      PeerConnectionInterface::PeerConnectionState::kDisconnected;
  EXPECT_EQ(PeerConnectionInterface::AsString(state), "disconnected");
  EXPECT_EQ(absl::StrCat(state), "disconnected");
}

TEST(Stringify, IceConnectionState) {
  PeerConnectionInterface::IceConnectionState state =
      PeerConnectionInterface::kIceConnectionClosed;
  EXPECT_EQ(PeerConnectionInterface::AsString(state), "closed");
  EXPECT_EQ(absl::StrCat(state), "closed");
}

}  // namespace
}  // namespace webrtc
