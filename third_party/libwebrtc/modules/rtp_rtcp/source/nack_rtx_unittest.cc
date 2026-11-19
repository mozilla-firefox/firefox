/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <span>

#include "absl/algorithm/container.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/rtp_headers.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/rtp_stream_receiver_controller.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "call/rtx_receive_stream.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "modules/rtp_rtcp/source/rtp_sender_video.h"
#include "rtc_base/rate_limiter.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {

constexpr int kVideoNackListSize = 30;
constexpr uint32_t kTestSsrc = 3456;
constexpr uint32_t kTestRtxSsrc = kTestSsrc + 1;
constexpr uint16_t kTestSequenceNumber = 2345;
constexpr uint32_t kTestNumberOfPackets = 1350;
constexpr int kTestNumberOfRtxPackets = 149;
constexpr int kNumFrames = 30;
constexpr int kPayloadType = 123;
constexpr int kRtxPayloadType = 98;
constexpr int64_t kMaxRttMs = 1000;

class VerifyingMediaStream : public RtpPacketSinkInterface {
 public:
  VerifyingMediaStream() {}

  void OnRtpPacket(const RtpPacketReceived& packet) override {
    if (!sequence_numbers_.empty())
      EXPECT_EQ(kTestSsrc, packet.Ssrc());

    sequence_numbers_.push_back(packet.SequenceNumber());
  }
  std::list<uint16_t> sequence_numbers_;
};

class DummySinkValidator : public RtpSinkValidator {
 public:
  void OnSinkAdded(RtpPacketSinkInterface* sink) override {}
  void OnSinkRemoved(RtpPacketSinkInterface* sink) override {}
  bool IsValidSink(RtpPacketSinkInterface* sink) const override { return true; }
};

class RtxLoopBackTransport : public Transport {
 public:
  RtxLoopBackTransport(TaskQueueBase* network_thread, uint32_t rtx_ssrc)
      : count_(0),
        packet_loss_(0),
        consecutive_drop_start_(0),
        consecutive_drop_end_(0),
        rtx_ssrc_(rtx_ssrc),
        count_rtx_ssrc_(0),
        module_(nullptr),
        stream_receiver_controller_(network_thread, &dummy_validator_) {}

  void SetSendModule(RtpRtcpInterface* rtpRtcpModule) {
    module_ = rtpRtcpModule;
  }

  void DropEveryNthPacket(int n) { packet_loss_ = n; }

  void DropConsecutivePackets(int start, int total) {
    consecutive_drop_start_ = start;
    consecutive_drop_end_ = start + total;
    packet_loss_ = 0;
  }

  bool SendRtp(std::span<const uint8_t> data,
               const PacketOptions& /* options */) override {
    count_++;
    RtpPacketReceived packet;
    if (!packet.Parse(data))
      return false;
    if (packet.Ssrc() == rtx_ssrc_) {
      count_rtx_ssrc_++;
    } else {
      // For non-RTX packets only.
      expected_sequence_numbers_.insert(expected_sequence_numbers_.end(),
                                        packet.SequenceNumber());
    }
    if (packet_loss_ > 0) {
      if ((count_ % packet_loss_) == 0) {
        return true;
      }
    } else if (count_ >= consecutive_drop_start_ &&
               count_ < consecutive_drop_end_) {
      return true;
    }
    EXPECT_TRUE(stream_receiver_controller_.OnRtpPacket(packet));
    return true;
  }

  bool SendRtcp(std::span<const uint8_t> data,
                const PacketOptions& /* options */) override {
    module_->IncomingRtcpPacket(data);
    return true;
  }
  int count_;
  int packet_loss_;
  int consecutive_drop_start_;
  int consecutive_drop_end_;
  uint32_t rtx_ssrc_;
  int count_rtx_ssrc_;
  RtpRtcpInterface* module_;
  DummySinkValidator dummy_validator_;
  RtpStreamReceiverController stream_receiver_controller_;
  std::set<uint16_t> expected_sequence_numbers_;
};

class RtpRtcpRtxNackTest : public ::testing::Test {
 protected:
  RtpRtcpRtxNackTest()
      : fake_clock_(123456),
        env_(CreateTestEnvironment({.time = &fake_clock_})),
        transport_(main_thread_.task_queue(), kTestRtxSsrc),
        rtx_stream_(env_,
                    &media_stream_,
                    rtx_associated_payload_types_,
                    kTestSsrc),
        retransmission_rate_limiter_(&fake_clock_, kMaxRttMs) {}
  ~RtpRtcpRtxNackTest() override {}

  void SetUp() override {
    CreateSendModule(kRtxOff);
    media_receiver_ = transport_.stream_receiver_controller_.CreateReceiver(
        kTestSsrc, &media_stream_);

    for (size_t n = 0; n < sizeof(payload_data); n++) {
      payload_data[n] = n % 10;
    }
  }

  // Creates the RTP module, which sends RTX with `rtx_mode`, and the
  // RTPSenderVideo that uses it.
  void CreateSendModule(RtxMode rtx_mode) {
    // `rtp_sender_video_` uses the module, which uses `receive_statistics_`,
    // so they are deleted in that order before being replaced.
    rtp_sender_video_ = nullptr;
    rtp_rtcp_module_ = nullptr;
    RtpRtcpInterface::Configuration configuration;
    configuration.audio = false;
    receive_statistics_ = ReceiveStatistics::Create(&fake_clock_);
    configuration.receive_statistics = receive_statistics_.get();
    configuration.outgoing_transport = &transport_;
    configuration.retransmission_rate_limiter = &retransmission_rate_limiter_;
    configuration.local_media_ssrc = kTestSsrc;
    configuration.rtx_send_ssrc = kTestRtxSsrc;
    configuration.rtx_mode = rtx_mode;
    configuration.rtx_payload_types = {{kPayloadType, kRtxPayloadType}};
    configuration.rtp_state = RtpState{.start_timestamp = 111111};
    configuration.rtcp_mode = RtcpMode::kCompound;
    rtp_rtcp_module_ =
        ModuleRtpRtcpImpl2::CreateSendModule(env_, configuration);
    RTPSenderVideo::Config video_config;
    video_config.clock = &fake_clock_;
    video_config.rtp_sender = rtp_rtcp_module_->RtpSender();
    video_config.field_trials = &env_.field_trials();
    rtp_sender_video_ = std::make_unique<RTPSenderVideo>(video_config);
    rtp_rtcp_module_->SetStorePacketsStatus(true, 600);
    EXPECT_EQ(0, rtp_rtcp_module_->SetSendingStatus(true));
    rtp_rtcp_module_->SetSequenceNumber(kTestSequenceNumber);

    // Used for NACK processing.
    rtp_rtcp_module_->SetRemoteSSRC(kTestSsrc);

    transport_.SetSendModule(rtp_rtcp_module_.get());
  }

  int BuildNackList(uint16_t* nack_list) {
    media_stream_.sequence_numbers_.sort();
    std::list<uint16_t> missing_sequence_numbers;
    std::list<uint16_t>::iterator it = media_stream_.sequence_numbers_.begin();

    while (it != media_stream_.sequence_numbers_.end()) {
      uint16_t sequence_number_1 = *it;
      ++it;
      if (it != media_stream_.sequence_numbers_.end()) {
        uint16_t sequence_number_2 = *it;
        // Add all missing sequence numbers to list
        for (uint16_t i = sequence_number_1 + 1; i < sequence_number_2; ++i) {
          missing_sequence_numbers.push_back(i);
        }
      }
    }
    int n = 0;
    for (it = missing_sequence_numbers.begin();
         it != missing_sequence_numbers.end(); ++it) {
      nack_list[n++] = (*it);
    }
    return n;
  }

  bool ExpectedPacketsReceived() {
    std::list<uint16_t> received_sorted;
    absl::c_copy(media_stream_.sequence_numbers_,
                 std::back_inserter(received_sorted));
    received_sorted.sort();
    return absl::c_equal(received_sorted,
                         transport_.expected_sequence_numbers_);
  }

  void RunRtxTest(RtxMode rtx_method, int loss) {
    rtx_receiver_ = transport_.stream_receiver_controller_.CreateReceiver(
        kTestRtxSsrc, &rtx_stream_);
    CreateSendModule(rtx_method);
    transport_.DropEveryNthPacket(loss);
    uint32_t timestamp = 3000;
    uint16_t nack_list[kVideoNackListSize];
    for (int frame = 0; frame < kNumFrames; ++frame) {
      RTPVideoHeader video_header;
      EXPECT_TRUE(rtp_rtcp_module_->OnSendingRtpFrame(timestamp, timestamp / 90,
                                                      kPayloadType, false));
      video_header.frame_type = VideoFrameType::kVideoFrameDelta;
      EXPECT_TRUE(rtp_sender_video_->SendVideoFrame(
          kPayloadType, VideoCodecType::kVideoCodecGeneric,
          RtpTimestampWithOffset{timestamp},
          /*capture_time=*/Timestamp::Millis(timestamp / 90), payload_data,
          sizeof(payload_data), video_header, TimeDelta::Zero(), {}));
      // Min required delay until retransmit = 5 + RTT ms (RTT = 0).
      fake_clock_.AdvanceTimeMilliseconds(5);
      int length = BuildNackList(nack_list);
      if (length > 0)
        rtp_rtcp_module_->SendNACK(nack_list, length);
      fake_clock_.AdvanceTimeMilliseconds(28);  //  33ms - 5ms delay.
      // Prepare next frame.
      timestamp += 3000;
    }
    media_stream_.sequence_numbers_.sort();
  }

  test::RunLoop main_thread_;
  SimulatedClock fake_clock_;
  const Environment env_;
  std::unique_ptr<ReceiveStatistics> receive_statistics_;
  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_module_;
  std::unique_ptr<RTPSenderVideo> rtp_sender_video_;
  RtxLoopBackTransport transport_;
  const std::map<int, int> rtx_associated_payload_types_ = {
      {kRtxPayloadType, kPayloadType}};
  VerifyingMediaStream media_stream_;
  RtxReceiveStream rtx_stream_;
  uint8_t payload_data[65000];
  RateLimiter retransmission_rate_limiter_;
  std::unique_ptr<RtpStreamReceiverInterface> media_receiver_;
  std::unique_ptr<RtpStreamReceiverInterface> rtx_receiver_;
};

TEST_F(RtpRtcpRtxNackTest, LongNackList) {
  const int kNumPacketsToDrop = 900;
  const int kNumRequiredRtcp = 4;
  uint32_t timestamp = 3000;
  uint16_t nack_list[kNumPacketsToDrop];
  // Disable StorePackets to be able to set a larger packet history.
  rtp_rtcp_module_->SetStorePacketsStatus(false, 0);
  // Enable StorePackets with a packet history of 2000 packets.
  rtp_rtcp_module_->SetStorePacketsStatus(true, 2000);
  // Drop 900 packets from the second one so that we get a NACK list which is
  // big enough to require 4 RTCP packets to be fully transmitted to the sender.
  transport_.DropConsecutivePackets(2, kNumPacketsToDrop);
  // Send 30 frames which at the default size is roughly what we need to get
  // enough packets.
  for (int frame = 0; frame < kNumFrames; ++frame) {
    RTPVideoHeader video_header;
    EXPECT_TRUE(rtp_rtcp_module_->OnSendingRtpFrame(timestamp, timestamp / 90,
                                                    kPayloadType, false));
    video_header.frame_type = VideoFrameType::kVideoFrameDelta;
    EXPECT_TRUE(rtp_sender_video_->SendVideoFrame(
        kPayloadType, VideoCodecType::kVideoCodecGeneric,
        RtpTimestampWithOffset{timestamp}, Timestamp::Millis(timestamp / 90),
        payload_data, sizeof(payload_data), video_header, TimeDelta::Zero(),
        {}));
    // Prepare next frame.
    timestamp += 3000;
    fake_clock_.AdvanceTimeMilliseconds(33);
  }
  EXPECT_FALSE(transport_.expected_sequence_numbers_.empty());
  EXPECT_FALSE(media_stream_.sequence_numbers_.empty());
  size_t last_receive_count = media_stream_.sequence_numbers_.size();
  int length = BuildNackList(nack_list);
  for (int i = 0; i < kNumRequiredRtcp - 1; ++i) {
    rtp_rtcp_module_->SendNACK(nack_list, length);
    EXPECT_GT(media_stream_.sequence_numbers_.size(), last_receive_count);
    last_receive_count = media_stream_.sequence_numbers_.size();
    EXPECT_FALSE(ExpectedPacketsReceived());
  }
  rtp_rtcp_module_->SendNACK(nack_list, length);
  EXPECT_GT(media_stream_.sequence_numbers_.size(), last_receive_count);
  EXPECT_TRUE(ExpectedPacketsReceived());
}

TEST_F(RtpRtcpRtxNackTest, RtxNack) {
  RunRtxTest(kRtxRetransmitted, 10);
  EXPECT_EQ(kTestSequenceNumber, *(media_stream_.sequence_numbers_.begin()));
  EXPECT_EQ(kTestSequenceNumber + kTestNumberOfPackets - 1,
            *(media_stream_.sequence_numbers_.rbegin()));
  EXPECT_EQ(kTestNumberOfPackets, media_stream_.sequence_numbers_.size());
  EXPECT_EQ(kTestNumberOfRtxPackets, transport_.count_rtx_ssrc_);
  EXPECT_TRUE(ExpectedPacketsReceived());
}

}  // namespace webrtc
