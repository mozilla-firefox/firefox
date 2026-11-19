/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_stream_receiver_controller.h"

#include <cstdint>
#include <memory>

#include "call/rtp_packet_sink_interface.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

constexpr uint32_t kSsrc = 111;

class FakeRtpPacketSink : public RtpPacketSinkInterface {
 public:
  void OnRtpPacket(const RtpPacketReceived& packet) override { ++packets_; }

  int packets() const { return packets_; }

 private:
  int packets_ = 0;
};

TEST(RtpStreamReceiverControllerTest, DeliversPacketsToRegisteredSink) {
  test::RunLoop loop;
  RtpStreamReceiverController controller(loop.task_queue());

  FakeRtpPacketSink sink;
  std::unique_ptr<RtpStreamReceiverInterface> receiver =
      controller.CreateReceiver(kSsrc, &sink);

  RtpPacketReceived packet;
  packet.SetSsrc(kSsrc);
  EXPECT_EQ(controller.ResolveSink(packet), &sink);
  EXPECT_TRUE(controller.OnRtpPacket(packet));
  EXPECT_EQ(sink.packets(), 1);
}

TEST(RtpStreamReceiverControllerTest, StopsDeliveringWhenReceiverIsDestroyed) {
  test::RunLoop loop;
  RtpStreamReceiverController controller(loop.task_queue());

  FakeRtpPacketSink sink;
  std::unique_ptr<RtpStreamReceiverInterface> receiver =
      controller.CreateReceiver(kSsrc, &sink);
  EXPECT_FALSE(controller.IsEmpty());

  // Destroying the receiver unregisters the sink synchronously, so a packet
  // that arrives afterwards can no longer resolve to it.
  receiver.reset();
  EXPECT_TRUE(controller.IsEmpty());

  RtpPacketReceived packet;
  packet.SetSsrc(kSsrc);
  EXPECT_EQ(controller.ResolveSink(packet), nullptr);
  EXPECT_FALSE(controller.OnRtpPacket(packet));
  EXPECT_EQ(sink.packets(), 0);
}

}  // namespace
}  // namespace webrtc
